/**
 * @file request_queue.c
 * @brief Request queue management implementation for SNRv9 priority system
 * 
 * This module provides priority-based request queuing with PSRAM optimization
 * and comprehensive monitoring capabilities.
 */

#include "request_queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "psram_manager.h"
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * PRIVATE CONSTANTS
 * =============================================================================
 */

#define QUEUE_MUTEX_TIMEOUT_MS 100
#define REQUEST_ID_PREFIX "req_"
#define CLEANUP_BATCH_SIZE 10

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static priority_queue_t priority_queues[REQUEST_PRIORITY_MAX];
static queue_manager_config_t queue_config;
static bool is_initialized = false;
static bool monitoring_enabled = true;
static SemaphoreHandle_t global_mutex = NULL;
static uint32_t next_request_id = 1;

/* Priority level names for debugging */
static const char* priority_names[REQUEST_PRIORITY_MAX] = {
    "EMERGENCY",
    "IO_CRITICAL", 
    "AUTHENTICATION",
    "UI_CRITICAL",
    "NORMAL",
    "BACKGROUND"
};

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static bool init_priority_queue(priority_queue_t *queue, uint16_t capacity);
static void cleanup_priority_queue(priority_queue_t *queue);
static bool is_queue_full_unsafe(priority_queue_t *queue);
static bool is_queue_empty_unsafe(priority_queue_t *queue);
static esp_err_t enqueue_unsafe(priority_queue_t *queue, request_context_t *context);
static request_context_t* dequeue_unsafe(priority_queue_t *queue);
static void update_queue_stats(priority_queue_t *queue, bool enqueue_operation);
static uint32_t get_current_time_ms(void);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool request_queue_init(const queue_manager_config_t *config) {
    if (is_initialized) {
        ESP_LOGW(DEBUG_QUEUE_TAG, "Request queue system already initialized");
        return true;
    }
    
    if (!config) {
        ESP_LOGE(DEBUG_QUEUE_TAG, "Invalid configuration provided");
        return false;
    }
    
    QUEUE_DEBUG("Initializing request queue system");
    
    // Copy configuration
    memcpy(&queue_config, config, sizeof(queue_manager_config_t));
    
    // Create global mutex
    global_mutex = xSemaphoreCreateMutex();
    if (!global_mutex) {
        ESP_LOGE(DEBUG_QUEUE_TAG, "Failed to create global mutex");
        return false;
    }
    
    // Initialize all priority queues
    bool success = true;
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        uint16_t capacity = config->queue_capacity[i];
        if (capacity == 0) {
            capacity = MAX_QUEUED_REQUESTS_PER_PRIORITY / 2; // Default capacity
        }
        
        if (!init_priority_queue(&priority_queues[i], capacity)) {
            ESP_LOGE(DEBUG_QUEUE_TAG, "Failed to initialize %s priority queue", 
                     priority_names[i]);
            success = false;
            break;
        }
        
        QUEUE_DEBUG("Initialized %s queue with capacity %d", 
                   priority_names[i], capacity);
    }
    
    if (!success) {
        // Cleanup on failure
        request_queue_cleanup();
        return false;
    }
    
    is_initialized = true;
    monitoring_enabled = config->enable_statistics;
    
    QUEUE_DEBUG_LOG(DEBUG_QUEUE_TAG, "Request queue system initialized successfully");
    return true;
}

void request_queue_cleanup(void) {
    if (!is_initialized) {
        return;
    }
    
    QUEUE_DEBUG("Cleaning up request queue system");
    
    // Cleanup all priority queues
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        cleanup_priority_queue(&priority_queues[i]);
    }
    
    // Destroy global mutex
    if (global_mutex) {
        vSemaphoreDelete(global_mutex);
        global_mutex = NULL;
    }
    
    is_initialized = false;
    monitoring_enabled = false;
    
    QUEUE_DEBUG("Request queue system cleanup complete");
}

request_context_t* request_queue_create_context(httpd_req_t *req, 
                                                request_priority_t priority,
                                                size_t buffer_size) {
    if (!req || priority >= REQUEST_PRIORITY_MAX) {
        ESP_LOGE(DEBUG_QUEUE_TAG, "Invalid parameters for context creation");
        return NULL;
    }
    
    // Limit buffer size
    if (buffer_size > MAX_REQUEST_BUFFER_SIZE) {
        buffer_size = MAX_REQUEST_BUFFER_SIZE;
    }
    
    // Allocate context structure in internal RAM for fast access
    request_context_t *context = heap_caps_malloc(sizeof(request_context_t), 
                                                  MALLOC_CAP_INTERNAL);
    if (!context) {
        ESP_LOGE(DEBUG_QUEUE_TAG, "Failed to allocate request context");
        return NULL;
    }
    
    // Initialize context
    memset(context, 0, sizeof(request_context_t));
    context->request = req;
    context->priority = priority;
    context->timestamp = get_current_time_ms();
    context->buffer_size = buffer_size;
    context->is_processed = false;
    context->processing_start_time = 0;
    
    // Set timeout based on priority
    if (priority == REQUEST_PRIORITY_EMERGENCY) {
        context->timeout_ms = EMERGENCY_REQUEST_TIMEOUT_MS;
    } else {
        context->timeout_ms = queue_config.default_timeout_ms;
    }
    
    // Generate unique request ID
    if (!request_queue_generate_id(context->request_id, sizeof(context->request_id))) {
        ESP_LOGW(DEBUG_QUEUE_TAG, "Failed to generate request ID, using default");
        snprintf(context->request_id, sizeof(context->request_id), "req_%lu", 
                context->timestamp);
    }
    
    // Allocate large buffers in PSRAM if enabled and available
    if (queue_config.enable_psram_allocation && buffer_size > 0) {
        context->request_buffer = psram_smart_malloc(buffer_size, ALLOC_LARGE_BUFFER);
        context->response_buffer = psram_smart_malloc(buffer_size, ALLOC_LARGE_BUFFER);
        
        TRACK_PSRAM_ALLOC("request_buffer", buffer_size, context->request_buffer);
        TRACK_PSRAM_ALLOC("response_buffer", buffer_size, context->response_buffer);
        
        if (!context->request_buffer || !context->response_buffer) {
            ESP_LOGW(DEBUG_QUEUE_TAG, "PSRAM allocation failed, using internal RAM");
            
            // Fallback to internal RAM with smaller buffers
            if (context->request_buffer) {
                psram_smart_free(context->request_buffer);
            }
            if (context->response_buffer) {
                psram_smart_free(context->response_buffer);
            }
            
            size_t fallback_size = buffer_size > 4096 ? 4096 : buffer_size;
            context->request_buffer = heap_caps_malloc(fallback_size, MALLOC_CAP_INTERNAL);
            context->response_buffer = heap_caps_malloc(fallback_size, MALLOC_CAP_INTERNAL);
            context->buffer_size = fallback_size;
        }
    } else if (buffer_size > 0) {
        // Use internal RAM
        context->request_buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL);
        context->response_buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL);
    }
    
    // Check buffer allocation
    if (buffer_size > 0 && (!context->request_buffer || !context->response_buffer)) {
        ESP_LOGE(DEBUG_QUEUE_TAG, "Failed to allocate request buffers");
        request_queue_free_context(context);
        return NULL;
    }
    
    QUEUE_DEBUG("Created context %s for %s priority (buffers: %zu bytes)", 
               context->request_id, priority_names[priority], buffer_size);
    
    return context;
}

void request_queue_free_context(request_context_t *context) {
    if (!context) {
        return;
    }
    
    QUEUE_DEBUG("Freeing context %s", context->request_id);
    
    // Free buffers
    if (context->request_buffer) {
        psram_smart_free(context->request_buffer);
    }
    if (context->response_buffer) {
        psram_smart_free(context->response_buffer);
    }
    if (context->processing_context) {
        psram_smart_free(context->processing_context);
    }
    
    // Free context structure
    heap_caps_free(context);
}

esp_err_t request_queue_enqueue(request_context_t *context) {
    if (!is_initialized || !context || context->priority >= REQUEST_PRIORITY_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    priority_queue_t *queue = &priority_queues[context->priority];
    
    // Take queue mutex
    if (xSemaphoreTake(queue->mutex, pdMS_TO_TICKS(QUEUE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(DEBUG_QUEUE_TAG, "Failed to acquire mutex for %s queue", 
                 priority_names[context->priority]);
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t result = enqueue_unsafe(queue, context);
    
    // Release mutex
    xSemaphoreGive(queue->mutex);
    
    if (result == ESP_OK) {
        // Signal semaphore to wake up waiting processors
        xSemaphoreGive(queue->semaphore);
        
        QUEUE_DEBUG("Enqueued %s to %s queue (depth: %d/%d)", 
                   context->request_id, priority_names[context->priority],
                   queue->count, queue->max_capacity);
    }
    
    return result;
}

request_context_t* request_queue_dequeue(uint32_t timeout_ms) {
    if (!is_initialized) {
        return NULL;
    }
    
    // Try to dequeue from highest priority first
    for (int priority = 0; priority < REQUEST_PRIORITY_MAX; priority++) {
        request_context_t *context = request_queue_dequeue_priority(
            (request_priority_t)priority, 0); // No wait for priority scan
        
        if (context) {
            QUEUE_DEBUG("Dequeued %s from %s queue", 
                       context->request_id, priority_names[priority]);
            return context;
        }
    }
    
    // If no requests available and timeout specified, wait on highest priority queue
    if (timeout_ms > 0) {
        return request_queue_dequeue_priority(REQUEST_PRIORITY_EMERGENCY, timeout_ms);
    }
    
    return NULL;
}

request_context_t* request_queue_dequeue_priority(request_priority_t priority, 
                                                  uint32_t timeout_ms) {
    if (!is_initialized || priority >= REQUEST_PRIORITY_MAX) {
        return NULL;
    }
    
    priority_queue_t *queue = &priority_queues[priority];
    
    // Wait for semaphore if timeout specified
    if (timeout_ms > 0) {
        if (xSemaphoreTake(queue->semaphore, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            return NULL; // Timeout
        }
    }
    
    // Take queue mutex
    if (xSemaphoreTake(queue->mutex, pdMS_TO_TICKS(QUEUE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return NULL;
    }
    
    request_context_t *context = dequeue_unsafe(queue);
    
    // Release mutex
    xSemaphoreGive(queue->mutex);
    
    return context;
}

bool request_queue_get_stats(request_priority_t priority, queue_stats_t *stats) {
    if (!is_initialized || priority >= REQUEST_PRIORITY_MAX || !stats) {
        return false;
    }
    
    priority_queue_t *queue = &priority_queues[priority];
    
    // Take mutex for consistent read
    if (xSemaphoreTake(queue->mutex, pdMS_TO_TICKS(QUEUE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    
    // Copy statistics
    stats->current_depth = queue->count;
    stats->max_capacity = queue->max_capacity;
    stats->total_enqueued = queue->total_enqueued;
    stats->total_dequeued = queue->total_dequeued;
    stats->total_timeouts = queue->total_timeouts;
    stats->peak_depth = queue->peak_depth;
    stats->last_activity_time = queue->last_activity_time;
    
    // Calculate derived statistics
    if (queue->total_dequeued > 0) {
        // This is a simplified calculation - in a real implementation,
        // you'd track actual wait times
        stats->average_wait_time_ms = 100; // Placeholder
    } else {
        stats->average_wait_time_ms = 0;
    }
    
    stats->utilization_percent = (float)queue->count / queue->max_capacity * 100.0f;
    
    xSemaphoreGive(queue->mutex);
    return true;
}

uint32_t request_queue_get_total_depth(void) {
    if (!is_initialized) {
        return 0;
    }
    
    uint32_t total = 0;
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        total += priority_queues[i].count;
    }
    return total;
}

bool request_queue_has_pending_requests(void) {
    return request_queue_get_total_depth() > 0;
}

uint32_t request_queue_cleanup_expired(void) {
    if (!is_initialized) {
        return 0;
    }
    
    uint32_t cleaned_count = 0;
    
    for (int priority = 0; priority < REQUEST_PRIORITY_MAX; priority++) {
        priority_queue_t *queue = &priority_queues[priority];
        
        if (xSemaphoreTake(queue->mutex, pdMS_TO_TICKS(QUEUE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            continue;
        }
        
        // Check requests for expiration (simplified implementation)
        // In a full implementation, you'd iterate through the queue
        // and remove expired requests
        
        xSemaphoreGive(queue->mutex);
    }
    
    if (cleaned_count > 0) {
        QUEUE_DEBUG("Cleaned up %lu expired requests", cleaned_count);
    }
    
    return cleaned_count;
}

uint16_t request_queue_get_depth(request_priority_t priority) {
    if (!is_initialized || priority >= REQUEST_PRIORITY_MAX) {
        return 0;
    }
    
    return priority_queues[priority].count;
}

bool request_queue_is_full(request_priority_t priority) {
    if (!is_initialized || priority >= REQUEST_PRIORITY_MAX) {
        return true;
    }
    
    priority_queue_t *queue = &priority_queues[priority];
    return queue->count >= queue->max_capacity;
}

bool request_queue_is_empty(request_priority_t priority) {
    if (!is_initialized || priority >= REQUEST_PRIORITY_MAX) {
        return true;
    }
    
    return priority_queues[priority].count == 0;
}

const char* request_queue_priority_to_string(request_priority_t priority) {
    if (priority >= REQUEST_PRIORITY_MAX) {
        return "UNKNOWN";
    }
    return priority_names[priority];
}

bool request_queue_generate_id(char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 16) {
        return false;
    }
    
    // Take global mutex for ID generation
    if (global_mutex && xSemaphoreTake(global_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        snprintf(buffer, buffer_size, "%s%08lx", REQUEST_ID_PREFIX, next_request_id++);
        xSemaphoreGive(global_mutex);
        return true;
    }
    
    // Fallback without mutex
    snprintf(buffer, buffer_size, "%s%08lx", REQUEST_ID_PREFIX, 
             (uint32_t)(esp_timer_get_time() & 0xFFFFFFFF));
    return true;
}

/* =============================================================================
 * MONITORING AND DEBUG FUNCTIONS
 * =============================================================================
 */

void request_queue_print_status_report(void) {
    if (!is_initialized) {
        ESP_LOGI(DEBUG_QUEUE_TAG, "Request queue system not initialized");
        return;
    }
    
    ESP_LOGI(DEBUG_QUEUE_TAG, "=== REQUEST QUEUE STATUS REPORT ===");
    
    uint32_t total_depth = 0;
    uint32_t total_capacity = 0;
    
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        priority_queue_t *queue = &priority_queues[i];
        
        ESP_LOGI(DEBUG_QUEUE_TAG, "%s: %d/%d requests (peak: %lu)", 
                 priority_names[i], queue->count, queue->max_capacity, queue->peak_depth);
        
        total_depth += queue->count;
        total_capacity += queue->max_capacity;
    }
    
    ESP_LOGI(DEBUG_QUEUE_TAG, "Total: %lu/%lu requests (%.1f%% utilization)", 
             total_depth, total_capacity, 
             total_capacity > 0 ? (float)total_depth / total_capacity * 100.0f : 0.0f);
    
    ESP_LOGI(DEBUG_QUEUE_TAG, "Monitoring: %s", monitoring_enabled ? "ENABLED" : "DISABLED");
}

void request_queue_print_statistics(void) {
    if (!is_initialized) {
        return;
    }
    
    ESP_LOGI(DEBUG_QUEUE_TAG, "=== REQUEST QUEUE STATISTICS ===");
    
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        priority_queue_t *queue = &priority_queues[i];
        
        ESP_LOGI(DEBUG_QUEUE_TAG, "%s: enqueued=%lu, dequeued=%lu, timeouts=%lu", 
                 priority_names[i], queue->total_enqueued, 
                 queue->total_dequeued, queue->total_timeouts);
    }
}

void request_queue_reset_statistics(void) {
    if (!is_initialized) {
        return;
    }
    
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        priority_queue_t *queue = &priority_queues[i];
        
        if (xSemaphoreTake(queue->mutex, pdMS_TO_TICKS(QUEUE_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            queue->total_enqueued = 0;
            queue->total_dequeued = 0;
            queue->total_timeouts = 0;
            queue->peak_depth = queue->count;
            xSemaphoreGive(queue->mutex);
        }
    }
    
    QUEUE_DEBUG("Queue statistics reset");
}

void request_queue_set_monitoring_enabled(bool enable) {
    monitoring_enabled = enable;
    QUEUE_DEBUG("Queue monitoring %s", enable ? "enabled" : "disabled");
}

bool request_queue_health_check(void) {
    if (!is_initialized) {
        return false;
    }
    
    bool healthy = true;
    
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        priority_queue_t *queue = &priority_queues[i];
        
        // Check if mutex is valid
        if (!queue->mutex || !queue->semaphore) {
            ESP_LOGE(DEBUG_QUEUE_TAG, "%s queue has invalid synchronization objects", 
                     priority_names[i]);
            healthy = false;
        }
        
        // Check if queue data is valid
        if (!queue->requests) {
            ESP_LOGE(DEBUG_QUEUE_TAG, "%s queue has invalid data pointer", 
                     priority_names[i]);
            healthy = false;
        }
        
        // Check for reasonable queue depth
        if (queue->count > queue->max_capacity) {
            ESP_LOGE(DEBUG_QUEUE_TAG, "%s queue count exceeds capacity", 
                     priority_names[i]);
            healthy = false;
        }
    }
    
    return healthy;
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static bool init_priority_queue(priority_queue_t *queue, uint16_t capacity) {
    if (!queue || capacity == 0) {
        return false;
    }
    
    memset(queue, 0, sizeof(priority_queue_t));
    
    // Initialize metadata
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->max_capacity = capacity;
    
    // Create synchronization objects
    queue->mutex = xSemaphoreCreateMutex();
    queue->semaphore = xSemaphoreCreateCounting(capacity, 0);
    
    if (!queue->mutex || !queue->semaphore) {
        cleanup_priority_queue(queue);
        return false;
    }
    
    // Allocate queue data in PSRAM if available
    size_t queue_size = capacity * sizeof(queued_request_t);
    
    if (queue_config.enable_psram_allocation) {
        queue->requests = psram_smart_malloc(queue_size, ALLOC_LARGE_BUFFER);
        TRACK_PSRAM_ALLOC("priority_queue", queue_size, queue->requests);
    }
    
    if (!queue->requests) {
        // Fallback to internal RAM
        queue->requests = heap_caps_malloc(queue_size, MALLOC_CAP_INTERNAL);
    }
    
    if (!queue->requests) {
        cleanup_priority_queue(queue);
        return false;
    }
    
    // Initialize queue entries
    memset(queue->requests, 0, queue_size);
    
    return true;
}

static void cleanup_priority_queue(priority_queue_t *queue) {
    if (!queue) {
        return;
    }
    
    // Free any remaining request contexts
    if (queue->requests) {
        for (uint16_t i = 0; i < queue->max_capacity; i++) {
            if (queue->requests[i].context) {
                request_queue_free_context(queue->requests[i].context);
            }
        }
        
        psram_smart_free(queue->requests);
        queue->requests = NULL;
    }
    
    // Destroy synchronization objects
    if (queue->mutex) {
        vSemaphoreDelete(queue->mutex);
        queue->mutex = NULL;
    }
    
    if (queue->semaphore) {
        vSemaphoreDelete(queue->semaphore);
        queue->semaphore = NULL;
    }
    
    memset(queue, 0, sizeof(priority_queue_t));
}

static bool is_queue_full_unsafe(priority_queue_t *queue) {
    return queue->count >= queue->max_capacity;
}

static bool is_queue_empty_unsafe(priority_queue_t *queue) {
    return queue->count == 0;
}

static esp_err_t enqueue_unsafe(priority_queue_t *queue, request_context_t *context) {
    if (is_queue_full_unsafe(queue)) {
        return ESP_ERR_NO_MEM;
    }
    
    // Add to queue
    queue->requests[queue->tail].context = context;
    queue->requests[queue->tail].enqueue_time = get_current_time_ms();
    queue->requests[queue->tail].is_valid = true;
    
    queue->tail = (queue->tail + 1) % queue->max_capacity;
    queue->count++;
    
    // Update statistics
    update_queue_stats(queue, true);
    
    return ESP_OK;
}

static request_context_t* dequeue_unsafe(priority_queue_t *queue) {
    if (is_queue_empty_unsafe(queue)) {
        return NULL;
    }
    
    request_context_t *context = queue->requests[queue->head].context;
    queue->requests[queue->head].context = NULL;
    queue->requests[queue->head].is_valid = false;
    
    queue->head = (queue->head + 1) % queue->max_capacity;
    queue->count--;
    
    // Update statistics
    update_queue_stats(queue, false);
    
    return context;
}

static void update_queue_stats(priority_queue_t *queue, bool enqueue_operation) {
    if (!monitoring_enabled) {
        return;
    }
    
    uint32_t current_time = get_current_time_ms();
    queue->last_activity_time = current_time;
    
    if (enqueue_operation) {
        queue->total_enqueued++;
        if (queue->count > queue->peak_depth) {
            queue->peak_depth = queue->count;
        }
    } else {
        queue->total_dequeued++;
    }
}

static uint32_t get_current_time_ms(void) {
    return esp_timer_get_time() / 1000;
}
