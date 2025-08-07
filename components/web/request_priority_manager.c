/**
 * @file request_priority_manager.c
 * @brief Request priority management system implementation for SNRv9
 * 
 * This module provides comprehensive request priority classification, queue management,
 * and processing task coordination with PSRAM optimization and load balancing.
 */

#include "request_priority_manager.h"
#include "request_queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
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

#define PRIORITY_MANAGER_MUTEX_TIMEOUT_MS 100
#define TASK_NOTIFICATION_TIMEOUT_MS 1000
#define HEALTH_CHECK_INTERVAL_MS 30000
#define STATISTICS_UPDATE_INTERVAL_MS 5000
#define EMERGENCY_MODE_CHECK_INTERVAL_MS 1000

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static priority_manager_config_t manager_config;
static processing_task_config_t task_configs[TASK_TYPE_MAX];
static TaskHandle_t processing_tasks[TASK_TYPE_MAX];
static bool is_initialized = false;
static bool monitoring_enabled = true;
static system_mode_t current_system_mode = SYSTEM_MODE_NORMAL;
static SemaphoreHandle_t system_mutex = NULL;
static uint32_t system_start_time = 0;
static uint32_t emergency_mode_start_time = 0;
static uint32_t emergency_mode_timeout = 0;

/* Priority system statistics */
static priority_stats_t system_stats;

/* Debug statistics (only compiled when debugging enabled) */
#if DEBUG_REQUEST_TIMING
priority_debug_stats_t debug_stats[REQUEST_PRIORITY_MAX];
#endif

/* Task type names for debugging */
static const char* task_type_names[TASK_TYPE_MAX] = {
    "CRITICAL",
    "NORMAL", 
    "BACKGROUND"
};

/* System mode names for debugging */
static const char* system_mode_names[] = {
    "NORMAL",
    "EMERGENCY",
    "LOAD_SHEDDING",
    "MAINTENANCE"
};

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static bool init_processing_tasks(void);
static void cleanup_processing_tasks(void);
static bool create_processing_task(processing_task_type_t task_type);
static bool classify_request_by_uri(const char *uri, classification_result_t *result);
static bool classify_request_by_method(httpd_method_t method, classification_result_t *result);
static void update_system_statistics(void);
static bool check_emergency_mode_timeout(void);
static uint8_t calculate_system_load(void);
static void handle_emergency_mode_transition(bool entering);
static uint32_t get_current_time_ms(void);
static void feed_watchdog_if_needed(void);

/* Debug and safety functions */
static bool is_valid_task_handle(TaskHandle_t handle);
static void log_task_handle_operation(const char *operation, processing_task_type_t task_type, TaskHandle_t handle);
static bool safe_get_task_state(TaskHandle_t handle, eTaskState *state);
static bool safe_get_stack_high_water(TaskHandle_t handle, UBaseType_t *stack_high_water);

/* Processing task functions */
static void critical_task_function(void *pvParameters);
static void normal_task_function(void *pvParameters);
static void background_task_function(void *pvParameters);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool request_priority_manager_init(const priority_manager_config_t *config) {
    if (is_initialized) {
        ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "Priority manager already initialized");
        return true;
    }
    
    if (!config) {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "Invalid configuration provided");
        return false;
    }
    
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Initializing request priority manager");
    
    // Copy configuration
    memcpy(&manager_config, config, sizeof(priority_manager_config_t));
    
    // Create system mutex
    system_mutex = xSemaphoreCreateMutex();
    if (!system_mutex) {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "Failed to create system mutex");
        return false;
    }
    
    // Initialize request queue system
    if (!request_queue_init(&config->queue_config)) {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "Failed to initialize request queue system");
        vSemaphoreDelete(system_mutex);
        return false;
    }
    
    // Initialize statistics
    memset(&system_stats, 0, sizeof(priority_stats_t));
    system_stats.current_mode = SYSTEM_MODE_NORMAL;
    system_start_time = get_current_time_ms();
    
#if DEBUG_REQUEST_TIMING
    // Initialize debug statistics
    memset(debug_stats, 0, sizeof(debug_stats));
#endif
    
    // CRITICAL FIX: Set initialized flag BEFORE creating tasks to prevent race condition
    is_initialized = true;
    monitoring_enabled = config->enable_statistics;
    current_system_mode = SYSTEM_MODE_NORMAL;
    
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "RACE_CONDITION_FIX: Set is_initialized=true BEFORE task creation");
    
    // Initialize processing tasks (now that is_initialized is true)
    if (!init_processing_tasks()) {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "Failed to initialize processing tasks");
        // Reset initialization state on failure
        is_initialized = false;
        monitoring_enabled = false;
        current_system_mode = SYSTEM_MODE_NORMAL;
        request_queue_cleanup();
        vSemaphoreDelete(system_mutex);
        return false;
    }
    
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Request priority manager initialized successfully");
    return true;
}

void request_priority_manager_cleanup(void) {
    if (!is_initialized) {
        return;
    }
    
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Cleaning up request priority manager");
    
    // Stop processing tasks
    cleanup_processing_tasks();
    
    // Cleanup request queue system
    request_queue_cleanup();
    
    // Destroy system mutex
    if (system_mutex) {
        vSemaphoreDelete(system_mutex);
        system_mutex = NULL;
    }
    
    is_initialized = false;
    monitoring_enabled = false;
    current_system_mode = SYSTEM_MODE_NORMAL;
    
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Priority manager cleanup complete");
}

bool request_priority_classify(httpd_req_t *req, classification_result_t *result) {
    if (!req || !result) {
        return false;
    }
    
    // Initialize result
    memset(result, 0, sizeof(classification_result_t));
    result->priority = REQUEST_PRIORITY_NORMAL; // Default priority
    result->estimated_processing_time_ms = 1000; // Default estimate
    result->requires_authentication = false;
    result->is_emergency_request = false;
    result->classification_reason = "default";
    
    // Get URI for classification
    const char *uri = req->uri;
    if (!uri) {
        return false;
    }
    
    CLASSIFICATION_DEBUG("Classifying request: %s", uri);
    
    // Classify by URI pattern
    if (classify_request_by_uri(uri, result)) {
        CLASSIFICATION_DEBUG("Request %s classified as %s (reason: %s)", 
                           uri, request_queue_priority_to_string(result->priority),
                           result->classification_reason);
        return true;
    }
    
    // Classify by HTTP method
    if (classify_request_by_method(req->method, result)) {
        CLASSIFICATION_DEBUG("Request %s classified as %s by method (reason: %s)", 
                           uri, request_queue_priority_to_string(result->priority),
                           result->classification_reason);
        return true;
    }
    
    // Default classification
    result->classification_reason = "default_normal";
    CLASSIFICATION_DEBUG("Request %s using default classification: %s", 
                       uri, request_queue_priority_to_string(result->priority));
    
    return true;
}

esp_err_t request_priority_queue_request(httpd_req_t *req, request_priority_t priority) {
    if (!is_initialized || !req || priority >= REQUEST_PRIORITY_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check system mode
    if (current_system_mode == SYSTEM_MODE_EMERGENCY && 
        priority > REQUEST_PRIORITY_IO_CRITICAL) {
        ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "Dropping non-critical request in emergency mode");
        return ESP_ERR_NOT_ALLOWED;
    }
    
    if (current_system_mode == SYSTEM_MODE_LOAD_SHEDDING && 
        priority >= REQUEST_PRIORITY_BACKGROUND) {
        ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "Dropping background request due to load shedding");
        return ESP_ERR_NOT_ALLOWED;
    }
    
    // Adjust priority based on system load if enabled
    if (manager_config.enable_load_balancing) {
        priority = request_priority_adjust_for_load(priority);
    }
    
    // Create request context
    size_t buffer_size = 4096; // Default buffer size
    request_context_t *context = request_queue_create_context(req, priority, buffer_size);
    if (!context) {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "Failed to create request context");
        return ESP_ERR_NO_MEM;
    }
    
    // Enqueue request
    esp_err_t result = request_queue_enqueue(context);
    if (result != ESP_OK) {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "Failed to enqueue request: %s", 
                 esp_err_to_name(result));
        request_queue_free_context(context);
        return result;
    }
    
    // Update statistics
    if (monitoring_enabled) {
        system_stats.requests_by_priority[priority]++;
    }
    
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Queued request %s with %s priority", 
                      context->request_id, request_queue_priority_to_string(priority));
    
    return ESP_OK;
}

void request_priority_process_queues(processing_task_type_t task_type) {
    if (!is_initialized || task_type >= TASK_TYPE_MAX) {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "LOOP_DEBUG: Invalid parameters - is_initialized=%d, task_type=%d", 
                 is_initialized, task_type);
        return;
    }
    
    processing_task_config_t *config = &task_configs[task_type];
    uint32_t last_health_check = 0;
    uint32_t last_stats_update = 0;
    uint32_t loop_iteration = 0;
    
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "LOOP_DEBUG: Starting %s processing task", 
             task_type_names[task_type]);
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "LOOP_DEBUG: About to enter while(true) loop");
    
    while (true) {
        loop_iteration++;
        
        if (loop_iteration <= 5 || (loop_iteration % 100) == 0) {
            ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "LOOP_DEBUG: %s task iteration %lu", 
                     task_type_names[task_type], loop_iteration);
        }
        uint32_t current_time = get_current_time_ms();
        
        // Periodic health checks
        if (current_time - last_health_check > HEALTH_CHECK_INTERVAL_MS) {
            if (!request_queue_health_check()) {
                ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "Queue health check failed in %s task", 
                         task_type_names[task_type]);
            }
            last_health_check = current_time;
        }
        
        // Periodic statistics update
        if (monitoring_enabled && current_time - last_stats_update > STATISTICS_UPDATE_INTERVAL_MS) {
            update_system_statistics();
            last_stats_update = current_time;
        }
        
        // Check emergency mode timeout
        if (current_system_mode == SYSTEM_MODE_EMERGENCY) {
            check_emergency_mode_timeout();
        }
        
        // Try to dequeue and process requests
        bool processed_request = false;
        
        // Process requests within this task's priority range
        for (request_priority_t priority = config->min_priority; 
             priority <= config->max_priority; priority++) {
            
            request_context_t *context = request_queue_dequeue_priority(priority, 100);
            if (context) {
                uint32_t processing_start = get_current_time_ms();
                context->processing_start_time = processing_start;
                
                PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, 
                                  "Processing request %s (%s priority) in %s task", 
                                  context->request_id, 
                                  request_queue_priority_to_string(priority),
                                  task_type_names[task_type]);
                
                // Process the request (simplified - in real implementation, 
                // this would call the appropriate controller)
                esp_err_t process_result = ESP_OK;
                
                // Simulate processing time based on priority
                uint32_t processing_time = 50; // Base processing time
                if (priority == REQUEST_PRIORITY_EMERGENCY) {
                    processing_time = 10; // Very fast
                } else if (priority == REQUEST_PRIORITY_BACKGROUND) {
                    processing_time = 200; // Slower
                }
                
                vTaskDelay(pdMS_TO_TICKS(processing_time));
                
                uint32_t processing_end = get_current_time_ms();
                uint32_t total_processing_time = processing_end - processing_start;
                
                // Update timing statistics
                UPDATE_TIMING_STATS(priority, total_processing_time);
                
                // Update system statistics
                if (monitoring_enabled) {
                    system_stats.total_requests_processed++;
                    if (system_stats.average_processing_time[priority] == 0) {
                        system_stats.average_processing_time[priority] = total_processing_time;
                    } else {
                        // Simple moving average
                        system_stats.average_processing_time[priority] = 
                            (system_stats.average_processing_time[priority] + total_processing_time) / 2;
                    }
                }
                
                // Mark as processed
                context->is_processed = true;
                
                // Free context
                request_queue_free_context(context);
                
                processed_request = true;
                
                PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, 
                                  "Completed request processing in %lu ms", 
                                  total_processing_time);
                
                // Feed watchdog
                feed_watchdog_if_needed();
                
                // Yield if processing took too long
                if (total_processing_time > manager_config.load_config.heavy_operation_threshold_ms) {
                    LOAD_BALANCE_DEBUG("Heavy operation detected, yielding CPU");
                    taskYIELD();
                }
                
                break; // Process one request at a time
            }
        }
        
        // If no requests processed, wait a bit
        if (!processed_request) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        // Check for task deletion request (only break on explicit stop signal)
        uint32_t notification = ulTaskNotifyTake(pdFALSE, 0);
        
        // Log ALL notifications for debugging
        if (notification > 0) {
            ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "NOTIFICATION_DEBUG: %s task received notification = 0x%08lx", 
                     task_type_names[task_type], notification);
        }
        
        if (notification == 0xFFFFFFFF) { // Explicit stop signal
            ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "TASK_EXIT_REASON: %s task received explicit stop signal (0xFFFFFFFF)", 
                     task_type_names[task_type]);
            break;
        } else if (notification > 0) {
            ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "TASK_EXIT_REASON: %s task received unexpected notification 0x%08lx - CONTINUING", 
                     task_type_names[task_type], notification);
            // Don't break - continue processing
        }
    }
    
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Exiting %s processing task", 
                      task_type_names[task_type]);
    
    vTaskDelete(NULL);
}

bool request_priority_get_stats(priority_stats_t *stats) {
    if (!is_initialized || !stats) {
        return false;
    }
    
    if (xSemaphoreTake(system_mutex, pdMS_TO_TICKS(PRIORITY_MANAGER_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    
    // Update current queue depths
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        system_stats.queue_depth[i] = request_queue_get_depth((request_priority_t)i);
    }
    
    // Update system uptime
    system_stats.system_uptime_ms = get_current_time_ms() - system_start_time;
    system_stats.last_update_time = get_current_time_ms();
    system_stats.current_mode = current_system_mode;
    system_stats.cpu_utilization_percent = calculate_system_load();
    
    // Copy statistics
    memcpy(stats, &system_stats, sizeof(priority_stats_t));
    
    xSemaphoreGive(system_mutex);
    return true;
}

bool request_priority_set_system_mode(system_mode_t mode) {
    if (!is_initialized || mode >= 4) { // Assuming 4 modes
        return false;
    }
    
    if (xSemaphoreTake(system_mutex, pdMS_TO_TICKS(PRIORITY_MANAGER_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    
    system_mode_t old_mode = current_system_mode;
    current_system_mode = mode;
    
    xSemaphoreGive(system_mutex);
    
    if (old_mode != mode) {
        ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "System mode changed: %s -> %s", 
                 system_mode_names[old_mode], system_mode_names[mode]);
        
        // Handle mode-specific actions
        if (mode == SYSTEM_MODE_EMERGENCY) {
            handle_emergency_mode_transition(true);
        } else if (old_mode == SYSTEM_MODE_EMERGENCY) {
            handle_emergency_mode_transition(false);
        }
    }
    
    return true;
}

system_mode_t request_priority_get_system_mode(void) {
    return current_system_mode;
}

bool request_priority_enter_emergency_mode(uint32_t timeout_ms) {
    if (!is_initialized) {
        return false;
    }
    
    emergency_mode_start_time = get_current_time_ms();
    emergency_mode_timeout = timeout_ms;
    
    bool result = request_priority_set_system_mode(SYSTEM_MODE_EMERGENCY);
    
    if (result && monitoring_enabled) {
        system_stats.emergency_mode_activations++;
    }
    
    EMERGENCY_MODE_DEBUG(true);
    
    return result;
}

bool request_priority_exit_emergency_mode(void) {
    if (!is_initialized || current_system_mode != SYSTEM_MODE_EMERGENCY) {
        return false;
    }
    
    emergency_mode_start_time = 0;
    emergency_mode_timeout = 0;
    
    bool result = request_priority_set_system_mode(SYSTEM_MODE_NORMAL);
    
    EMERGENCY_MODE_DEBUG(false);
    
    return result;
}

void request_priority_enable_load_shedding(bool enable) {
    if (!is_initialized) {
        return;
    }
    
    if (enable && current_system_mode == SYSTEM_MODE_NORMAL) {
        request_priority_set_system_mode(SYSTEM_MODE_LOAD_SHEDDING);
        if (monitoring_enabled) {
            system_stats.load_shedding_activations++;
        }
        LOAD_BALANCE_DEBUG("Load shedding enabled");
    } else if (!enable && current_system_mode == SYSTEM_MODE_LOAD_SHEDDING) {
        request_priority_set_system_mode(SYSTEM_MODE_NORMAL);
        LOAD_BALANCE_DEBUG("Load shedding disabled");
    }
}

request_priority_t request_priority_adjust_for_load(request_priority_t base_priority) {
    if (!is_initialized || !manager_config.enable_load_balancing) {
        return base_priority;
    }
    
    uint8_t load_percent = calculate_system_load();
    
    // If system load is high, demote non-critical requests
    if (load_percent > manager_config.load_config.load_shedding_threshold) {
        if (base_priority == REQUEST_PRIORITY_NORMAL) {
            LOAD_BALANCE_DEBUG("Demoting NORMAL request to BACKGROUND due to high load (%d%%)", 
                              load_percent);
            return REQUEST_PRIORITY_BACKGROUND;
        }
        if (base_priority == REQUEST_PRIORITY_UI_CRITICAL) {
            LOAD_BALANCE_DEBUG("Demoting UI_CRITICAL request to NORMAL due to high load (%d%%)", 
                              load_percent);
            return REQUEST_PRIORITY_NORMAL;
        }
    }
    
    return base_priority;
}

bool request_priority_is_high_load(void) {
    if (!is_initialized) {
        return false;
    }
    
    uint8_t load_percent = calculate_system_load();
    return load_percent > manager_config.load_config.load_shedding_threshold;
}

uint8_t request_priority_get_load_percentage(void) {
    if (!is_initialized) {
        return 0;
    }
    
    return calculate_system_load();
}

uint32_t request_priority_flush_all_queues(uint32_t timeout_ms) {
    if (!is_initialized) {
        return 0;
    }
    
    uint32_t processed_count = 0;
    uint32_t start_time = get_current_time_ms();
    
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Flushing all queues (timeout: %lu ms)", 
                      timeout_ms);
    
    // Process all queued requests
    while (request_queue_has_pending_requests()) {
        uint32_t current_time = get_current_time_ms();
        if (timeout_ms > 0 && (current_time - start_time) > timeout_ms) {
            ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "Queue flush timeout reached");
            break;
        }
        
        request_context_t *context = request_queue_dequeue(100);
        if (context) {
            // Simple processing - just mark as processed and free
            context->is_processed = true;
            request_queue_free_context(context);
            processed_count++;
        } else {
            break; // No more requests
        }
    }
    
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Flushed %lu requests", processed_count);
    return processed_count;
}

void request_priority_get_default_config(priority_manager_config_t *config) {
    if (!config) {
        return;
    }
    
    memset(config, 0, sizeof(priority_manager_config_t));
    
    // Queue configuration
    config->queue_config.default_timeout_ms = DEFAULT_REQUEST_TIMEOUT_MS;
    config->queue_config.emergency_timeout_ms = EMERGENCY_REQUEST_TIMEOUT_MS;
    config->queue_config.enable_psram_allocation = true;
    config->queue_config.enable_statistics = true;
    config->queue_config.cleanup_interval_ms = 30000;
    
    // Set default queue capacities
    config->queue_config.queue_capacity[REQUEST_PRIORITY_EMERGENCY] = 50;
    config->queue_config.queue_capacity[REQUEST_PRIORITY_IO_CRITICAL] = 100;
    config->queue_config.queue_capacity[REQUEST_PRIORITY_AUTHENTICATION] = 50;
    config->queue_config.queue_capacity[REQUEST_PRIORITY_UI_CRITICAL] = 100;
    config->queue_config.queue_capacity[REQUEST_PRIORITY_NORMAL] = 200;
    config->queue_config.queue_capacity[REQUEST_PRIORITY_BACKGROUND] = 100;
    
    // Load protection configuration
    config->load_config.max_processing_time_ms = MAX_PROCESSING_TIME_MS;
    config->load_config.watchdog_feed_interval_ms = WATCHDOG_FEED_INTERVAL_MS;
    config->load_config.enable_yield_on_heavy_ops = true;
    config->load_config.heavy_operation_threshold_ms = HEAVY_OPERATION_THRESHOLD_MS;
    config->load_config.enable_load_shedding = true;
    config->load_config.load_shedding_threshold = LOAD_SHEDDING_THRESHOLD_PERCENT;
    
    // Task configurations
    // Critical task
    config->task_configs[TASK_TYPE_CRITICAL].task_type = TASK_TYPE_CRITICAL;
    config->task_configs[TASK_TYPE_CRITICAL].task_name = "req_critical";
    config->task_configs[TASK_TYPE_CRITICAL].stack_size = CRITICAL_TASK_STACK_SIZE;
    config->task_configs[TASK_TYPE_CRITICAL].priority = CRITICAL_TASK_PRIORITY;
    config->task_configs[TASK_TYPE_CRITICAL].core_affinity = 1;
    config->task_configs[TASK_TYPE_CRITICAL].use_psram_stack = false;
    config->task_configs[TASK_TYPE_CRITICAL].min_priority = REQUEST_PRIORITY_EMERGENCY;
    config->task_configs[TASK_TYPE_CRITICAL].max_priority = REQUEST_PRIORITY_IO_CRITICAL;
    
    // Normal task
    config->task_configs[TASK_TYPE_NORMAL].task_type = TASK_TYPE_NORMAL;
    config->task_configs[TASK_TYPE_NORMAL].task_name = "req_normal";
    config->task_configs[TASK_TYPE_NORMAL].stack_size = NORMAL_TASK_STACK_SIZE;
    config->task_configs[TASK_TYPE_NORMAL].priority = NORMAL_TASK_PRIORITY;
    config->task_configs[TASK_TYPE_NORMAL].core_affinity = 0;
    config->task_configs[TASK_TYPE_NORMAL].use_psram_stack = true;
    config->task_configs[TASK_TYPE_NORMAL].min_priority = REQUEST_PRIORITY_AUTHENTICATION;
    config->task_configs[TASK_TYPE_NORMAL].max_priority = REQUEST_PRIORITY_UI_CRITICAL;
    
    // Background task
    config->task_configs[TASK_TYPE_BACKGROUND].task_type = TASK_TYPE_BACKGROUND;
    config->task_configs[TASK_TYPE_BACKGROUND].task_name = "req_background";
    config->task_configs[TASK_TYPE_BACKGROUND].stack_size = BACKGROUND_TASK_STACK_SIZE;
    config->task_configs[TASK_TYPE_BACKGROUND].priority = BACKGROUND_TASK_PRIORITY;
    config->task_configs[TASK_TYPE_BACKGROUND].core_affinity = 0;
    config->task_configs[TASK_TYPE_BACKGROUND].use_psram_stack = true;
    config->task_configs[TASK_TYPE_BACKGROUND].min_priority = REQUEST_PRIORITY_NORMAL;
    config->task_configs[TASK_TYPE_BACKGROUND].max_priority = REQUEST_PRIORITY_BACKGROUND;
    
    // General settings
    config->enable_emergency_mode = true;
    config->enable_load_balancing = true;
    config->enable_statistics = true;
    config->statistics_report_interval_ms = DEBUG_PRIORITY_REPORT_INTERVAL_MS;
    config->health_check_interval_ms = HEALTH_CHECK_INTERVAL_MS;
}

/* =============================================================================
 * MONITORING AND DEBUG FUNCTIONS
 * =============================================================================
 */

void request_priority_print_status_report(void) {
    if (!is_initialized) {
        ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "Priority manager not initialized");
        return;
    }
    
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "=== REQUEST PRIORITY MANAGER STATUS ===");
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "System Mode: %s", 
             system_mode_names[current_system_mode]);
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "System Load: %d%%", calculate_system_load());
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "Total Queued: %lu requests", 
             request_queue_get_total_depth());
    
    // Print queue status
    request_queue_print_status_report();
    
    // Print task status using task tracker's proven method
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "=== PROCESSING TASKS ===");
    
    // Use uxTaskGetSystemState() like task tracker for safe task access
    UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_status_array = pvPortMalloc(num_tasks * sizeof(TaskStatus_t));
    
    if (task_status_array != NULL) {
        UBaseType_t actual_tasks = uxTaskGetSystemState(task_status_array, num_tasks, NULL);
        
        // Find our processing tasks in the system state
        for (int i = 0; i < TASK_TYPE_MAX; i++) {
            bool task_found = false;
            
            // Search for our task by name in the system state
            for (UBaseType_t j = 0; j < actual_tasks; j++) {
                if (strcmp(task_status_array[j].pcTaskName, task_configs[i].task_name) == 0) {
                    TaskStatus_t *status = &task_status_array[j];
                    
                    const char *state_str = "Unknown";
                    switch (status->eCurrentState) {
                        case eRunning:   state_str = "Running"; break;
                        case eReady:     state_str = "Ready"; break;
                        case eBlocked:   state_str = "Blocked"; break;
                        case eSuspended: state_str = "Suspended"; break;
                        case eDeleted:   state_str = "Deleted"; break;
                        default:         state_str = "Invalid"; break;
                    }
                    
                    uint32_t stack_high_water = status->usStackHighWaterMark * sizeof(StackType_t);
                    uint32_t stack_used = task_configs[i].stack_size - stack_high_water;
                    uint8_t stack_usage_pct = (stack_used * 100) / task_configs[i].stack_size;
                    
                    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, 
                             "%s Task: Handle=%p State=%s Priority=%u Stack=%u/%u(%u%%) Runtime=%lu", 
                             task_type_names[i], status->xHandle, state_str, 
                             (unsigned int)status->uxCurrentPriority,
                             (unsigned int)stack_used, (unsigned int)task_configs[i].stack_size, 
                             stack_usage_pct, status->ulRunTimeCounter);
                    
                    task_found = true;
                    break;
                }
            }
            
            if (!task_found) {
                ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "%s Task: Not found in system state (may have exited)", 
                         task_type_names[i]);
            }
        }
        
        vPortFree(task_status_array);
    } else {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "Failed to allocate memory for task status array");
        
        // Fallback: just show handle presence without accessing TCB
        for (int i = 0; i < TASK_TYPE_MAX; i++) {
            ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "%s Task: Handle=%p (memory allocation failed for detailed status)", 
                     task_type_names[i], processing_tasks[i]);
        }
    }
}

void request_priority_print_statistics(void) {
    if (!is_initialized) {
        return;
    }
    
    priority_stats_t stats;
    if (!request_priority_get_stats(&stats)) {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "Failed to get statistics");
        return;
    }
    
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "=== PRIORITY SYSTEM STATISTICS ===");
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "Total Processed: %lu requests", 
             stats.total_requests_processed);
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "Dropped Requests: %lu", stats.dropped_requests);
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "Timeout Requests: %lu", stats.timeout_requests);
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "Emergency Activations: %lu", 
             stats.emergency_mode_activations);
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "Load Shedding Activations: %lu", 
             stats.load_shedding_activations);
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "System Uptime: %lu ms", stats.system_uptime_ms);
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "CPU Utilization: %.1f%%", 
             stats.cpu_utilization_percent);
    
    // Print per-priority statistics
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "%s: %lu requests, avg %lu ms", 
                 request_queue_priority_to_string((request_priority_t)i),
                 stats.requests_by_priority[i], stats.average_processing_time[i]);
    }
    
    // Print debug statistics if enabled
#if DEBUG_REQUEST_TIMING
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "=== DEBUG TIMING STATISTICS ===");
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        if (debug_stats[i].request_count > 0) {
            ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, 
                     "%s Debug: count=%lu, avg=%lu ms, min=%lu ms, max=%lu ms, slow=%lu", 
                     request_queue_priority_to_string((request_priority_t)i),
                     debug_stats[i].request_count,
                     debug_stats[i].total_processing_time / debug_stats[i].request_count,
                     debug_stats[i].min_processing_time,
                     debug_stats[i].max_processing_time,
                     debug_stats[i].slow_request_count);
        }
    }
#endif
}

void request_priority_reset_statistics(void) {
    if (!is_initialized) {
        return;
    }
    
    if (xSemaphoreTake(system_mutex, pdMS_TO_TICKS(PRIORITY_MANAGER_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        memset(&system_stats, 0, sizeof(priority_stats_t));
        system_stats.current_mode = current_system_mode;
        system_start_time = get_current_time_ms();
        
#if DEBUG_REQUEST_TIMING
        memset(debug_stats, 0, sizeof(debug_stats));
#endif
        
        xSemaphoreGive(system_mutex);
    }
    
    // Reset queue statistics
    request_queue_reset_statistics();
    
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Priority system statistics reset");
}

void request_priority_set_monitoring_enabled(bool enable) {
    monitoring_enabled = enable;
    request_queue_set_monitoring_enabled(enable);
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Priority monitoring %s", 
                      enable ? "enabled" : "disabled");
}

bool request_priority_health_check(void) {
    if (!is_initialized) {
        return false;
    }
    
    bool healthy = true;
    
    // Check queue health
    if (!request_queue_health_check()) {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "Queue health check failed");
        healthy = false;
    }
    
    // Check processing tasks using safe system state method
    UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_status_array = pvPortMalloc(num_tasks * sizeof(TaskStatus_t));
    
    if (task_status_array != NULL) {
        UBaseType_t actual_tasks = uxTaskGetSystemState(task_status_array, num_tasks, NULL);
        
        // Check each of our processing tasks
        for (int i = 0; i < TASK_TYPE_MAX; i++) {
            bool task_found = false;
            
            // Search for our task by name in the system state
            for (UBaseType_t j = 0; j < actual_tasks; j++) {
                if (strcmp(task_status_array[j].pcTaskName, task_configs[i].task_name) == 0) {
                    TaskStatus_t *status = &task_status_array[j];
                    
                    if (status->eCurrentState == eDeleted || status->eCurrentState == eInvalid) {
                        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "%s task is not running (state: %d)", 
                                 task_type_names[i], status->eCurrentState);
                        processing_tasks[i] = NULL; // Clear invalid handle
                        healthy = false;
                    }
                    
                    task_found = true;
                    break;
                }
            }
            
            if (!task_found) {
                ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "%s task not found in system state", 
                         task_type_names[i]);
                processing_tasks[i] = NULL; // Clear handle for missing task
                healthy = false;
            }
        }
        
        vPortFree(task_status_array);
    } else {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "Failed to allocate memory for health check");
        healthy = false;
    }
    
    // Check system mutex
    if (!system_mutex) {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "System mutex is NULL");
        healthy = false;
    }
    
    return healthy;
}

bool request_priority_get_task_info(processing_task_type_t task_type,
                                    TaskHandle_t *task_handle,
                                    uint32_t *stack_high_water_mark) {
    if (!is_initialized || task_type >= TASK_TYPE_MAX) {
        return false;
    }
    
    if (task_handle) {
        *task_handle = processing_tasks[task_type];
    }
    
    if (stack_high_water_mark && processing_tasks[task_type]) {
        *stack_high_water_mark = uxTaskGetStackHighWaterMark(processing_tasks[task_type]) * sizeof(StackType_t);
    }
    
    return processing_tasks[task_type] != NULL;
}

const char* request_priority_mode_to_string(system_mode_t mode) {
    if (mode >= 4) {
        return "UNKNOWN";
    }
    return system_mode_names[mode];
}

const char* request_priority_task_type_to_string(processing_task_type_t task_type) {
    if (task_type >= TASK_TYPE_MAX) {
        return "UNKNOWN";
    }
    return task_type_names[task_type];
}

/* =============================================================================
 * ADVANCED FEATURES (PLACEHOLDER IMPLEMENTATIONS)
 * =============================================================================
 */

bool request_priority_register_custom_classifier(const char *uri_pattern,
                                                 bool (*classifier_func)(httpd_req_t *, classification_result_t *)) {
    // Placeholder for future implementation
    ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "Custom classifier registration not yet implemented");
    return false;
}

bool request_priority_set_uri_override(const char *uri_pattern, request_priority_t priority) {
    // Placeholder for future implementation
    ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "URI priority override not yet implemented");
    return false;
}

bool request_priority_remove_uri_override(const char *uri_pattern) {
    // Placeholder for future implementation
    ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "URI priority override removal not yet implemented");
    return false;
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static bool init_processing_tasks(void) {
    // Copy task configurations
    memcpy(task_configs, manager_config.task_configs, sizeof(task_configs));
    
    // Initialize task handles
    memset(processing_tasks, 0, sizeof(processing_tasks));
    
    // Create processing tasks
    for (int i = 0; i < TASK_TYPE_MAX; i++) {
        if (!create_processing_task((processing_task_type_t)i)) {
            ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "Failed to create %s task", 
                     task_type_names[i]);
            cleanup_processing_tasks();
            return false;
        }
    }
    
    return true;
}

static void cleanup_processing_tasks(void) {
    // Stop all processing tasks
    for (int i = 0; i < TASK_TYPE_MAX; i++) {
        if (processing_tasks[i]) {
            // Send explicit stop notification
            xTaskNotify(processing_tasks[i], 0xFFFFFFFF, eSetValueWithOverwrite);
            
            // Wait a bit for graceful shutdown
            vTaskDelay(pdMS_TO_TICKS(100));
            
            // Force delete if still running
            eTaskState task_state = eTaskGetState(processing_tasks[i]);
            if (task_state != eDeleted && task_state != eInvalid) {
                vTaskDelete(processing_tasks[i]);
                vTaskDelay(pdMS_TO_TICKS(50)); // Allow cleanup
            }
            
            processing_tasks[i] = NULL;
        }
    }
}

static bool create_processing_task(processing_task_type_t task_type) {
    if (task_type >= TASK_TYPE_MAX) {
        return false;
    }
    
    processing_task_config_t *config = &task_configs[task_type];
    
    // Select task function based on type
    TaskFunction_t task_function = NULL;
    switch (task_type) {
        case TASK_TYPE_CRITICAL:
            task_function = critical_task_function;
            break;
        case TASK_TYPE_NORMAL:
            task_function = normal_task_function;
            break;
        case TASK_TYPE_BACKGROUND:
            task_function = background_task_function;
            break;
        default:
            return false;
    }
    
    // Create task with PSRAM stack if configured
    BaseType_t result;
    if (config->use_psram_stack) {
        // For now, disable PSRAM stack creation to avoid the NULL handle issue
        // Use internal RAM stack instead for stability
        ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "PSRAM stack requested for %s task, using internal RAM for stability", 
                 config->task_name);
        
        result = xTaskCreatePinnedToCore(
            task_function,
            config->task_name,
            config->stack_size,
            (void*)task_type,
            config->priority,
            &processing_tasks[task_type],
            config->core_affinity
        );
    } else {
        // Create task with internal RAM stack
        result = xTaskCreatePinnedToCore(
            task_function,
            config->task_name,
            config->stack_size,
            (void*)task_type,
            config->priority,
            &processing_tasks[task_type],
            config->core_affinity
        );
    }
    
    if (result != pdPASS) {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "Failed to create %s task", config->task_name);
        log_task_handle_operation("CREATE_FAILED", task_type, NULL);
        return false;
    }
    
    // Log successful task creation with detailed information
    log_task_handle_operation("CREATE_SUCCESS", task_type, processing_tasks[task_type]);
    
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Created %s task (stack: %lu bytes, priority: %d, handle: %p)", 
                      config->task_name, config->stack_size, config->priority, processing_tasks[task_type]);
    
    // Verify the task handle immediately after creation
    if (processing_tasks[task_type]) {
        eTaskState initial_state;
        if (safe_get_task_state(processing_tasks[task_type], &initial_state)) {
            ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "TASK_CREATE_VERIFY: %s task created successfully with state %d", 
                     config->task_name, initial_state);
        } else {
            ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "TASK_CREATE_VERIFY: %s task handle invalid immediately after creation!", 
                     config->task_name);
            return false;
        }
    } else {
        ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "TASK_CREATE_VERIFY: %s task handle is NULL after creation!", 
                 config->task_name);
        return false;
    }
    
    return true;
}

static void critical_task_function(void *pvParameters) {
    processing_task_type_t task_type = (processing_task_type_t)pvParameters;
    
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "TASK_LIFECYCLE: Critical task starting (handle=%p)", xTaskGetCurrentTaskHandle());
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "TASK_LIFECYCLE: Task parameter = %d", (int)task_type);
    
    // Call the main processing function
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "TASK_LIFECYCLE: Critical task about to call request_priority_process_queues");
    request_priority_process_queues(task_type);
    
    // If we reach here, the processing loop has exited
    ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "TASK_EXIT: Critical task processing loop exited unexpectedly!");
    ESP_LOGE(DEBUG_PRIORITY_MANAGER_TAG, "TASK_EXIT: This indicates the task received a stop signal or crashed");
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "TASK_EXIT: Critical task calling vTaskDelete");
    
    // CRITICAL: Must call vTaskDelete before returning to prevent FreeRTOS abort
    vTaskDelete(NULL);
}

static void normal_task_function(void *pvParameters) {
    processing_task_type_t task_type = (processing_task_type_t)pvParameters;
    
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Normal task starting");
    
    // Call the main processing function
    request_priority_process_queues(task_type);
    
    // If we reach here, the processing loop has exited
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Normal task exiting - calling vTaskDelete");
    
    // CRITICAL: Must call vTaskDelete before returning to prevent FreeRTOS abort
    vTaskDelete(NULL);
}

static void background_task_function(void *pvParameters) {
    processing_task_type_t task_type = (processing_task_type_t)pvParameters;
    
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Background task starting");
    
    // Call the main processing function
    request_priority_process_queues(task_type);
    
    // If we reach here, the processing loop has exited
    PRIORITY_DEBUG_LOG(DEBUG_PRIORITY_MANAGER_TAG, "Background task exiting - calling vTaskDelete");
    
    // CRITICAL: Must call vTaskDelete before returning to prevent FreeRTOS abort
    vTaskDelete(NULL);
}

static bool classify_request_by_uri(const char *uri, classification_result_t *result) {
    if (!uri || !result) {
        return false;
    }
    
    // Emergency requests
    if (strstr(uri, "/api/emergency") || strstr(uri, "/emergency-stop")) {
        result->priority = REQUEST_PRIORITY_EMERGENCY;
        result->estimated_processing_time_ms = 50;
        result->is_emergency_request = true;
        result->classification_reason = "emergency_uri";
        return true;
    }
    
    // IO Critical requests
    if (strstr(uri, "/api/io/points/") && strstr(uri, "/set")) {
        result->priority = REQUEST_PRIORITY_IO_CRITICAL;
        result->estimated_processing_time_ms = 100;
        result->classification_reason = "io_control_uri";
        return true;
    }
    
    if (strstr(uri, "/api/irrigation/zones/") && strstr(uri, "/activate")) {
        result->priority = REQUEST_PRIORITY_IO_CRITICAL;
        result->estimated_processing_time_ms = 200;
        result->classification_reason = "irrigation_control_uri";
        return true;
    }
    
    // Authentication requests
    if (strstr(uri, "/api/auth/")) {
        result->priority = REQUEST_PRIORITY_AUTHENTICATION;
        result->estimated_processing_time_ms = 500;
        result->requires_authentication = false; // Auth endpoints don't require auth
        result->classification_reason = "auth_uri";
        return true;
    }
    
    // UI Critical requests
    if (strstr(uri, "/api/status") || strstr(uri, "/api/dashboard/")) {
        result->priority = REQUEST_PRIORITY_UI_CRITICAL;
        result->estimated_processing_time_ms = 300;
        result->classification_reason = "ui_critical_uri";
        return true;
    }
    
    if (strstr(uri, "/api/io/points") && !strstr(uri, "/set")) {
        result->priority = REQUEST_PRIORITY_UI_CRITICAL;
        result->estimated_processing_time_ms = 200;
        result->classification_reason = "io_status_uri";
        return true;
    }
    
    // Background requests
    if (strstr(uri, "/api/logs/") || strstr(uri, "/api/statistics/")) {
        result->priority = REQUEST_PRIORITY_BACKGROUND;
        result->estimated_processing_time_ms = 2000;
        result->classification_reason = "background_uri";
        return true;
    }
    
    // Static files
    if (strstr(uri, ".css") || strstr(uri, ".js") || strstr(uri, ".html") || 
        strstr(uri, ".png") || strstr(uri, ".jpg") || strstr(uri, ".ico")) {
        result->priority = REQUEST_PRIORITY_NORMAL;
        result->estimated_processing_time_ms = 100;
        result->classification_reason = "static_file_uri";
        return true;
    }
    
    return false; // No classification found
}

static bool classify_request_by_method(httpd_method_t method, classification_result_t *result) {
    if (!result) {
        return false;
    }
    
    // POST requests are generally higher priority than GET
    if (method == HTTP_POST) {
        result->priority = REQUEST_PRIORITY_UI_CRITICAL;
        result->estimated_processing_time_ms = 800;
        result->classification_reason = "post_method";
        return true;
    }
    
    // PUT requests for updates
    if (method == HTTP_PUT) {
        result->priority = REQUEST_PRIORITY_UI_CRITICAL;
        result->estimated_processing_time_ms = 600;
        result->classification_reason = "put_method";
        return true;
    }
    
    // DELETE requests
    if (method == HTTP_DELETE) {
        result->priority = REQUEST_PRIORITY_NORMAL;
        result->estimated_processing_time_ms = 400;
        result->classification_reason = "delete_method";
        return true;
    }
    
    // GET requests are normal priority
    if (method == HTTP_GET) {
        result->priority = REQUEST_PRIORITY_NORMAL;
        result->estimated_processing_time_ms = 300;
        result->classification_reason = "get_method";
        return true;
    }
    
    return false;
}

static void update_system_statistics(void) {
    if (!monitoring_enabled) {
        return;
    }
    
    // Update queue depths
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        system_stats.queue_depth[i] = request_queue_get_depth((request_priority_t)i);
    }
    
    // Update system metrics
    system_stats.cpu_utilization_percent = calculate_system_load();
    system_stats.last_update_time = get_current_time_ms();
}

static bool check_emergency_mode_timeout(void) {
    if (current_system_mode != SYSTEM_MODE_EMERGENCY || emergency_mode_timeout == 0) {
        return false;
    }
    
    uint32_t current_time = get_current_time_ms();
    if ((current_time - emergency_mode_start_time) > emergency_mode_timeout) {
        ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "Emergency mode timeout reached, returning to normal");
        request_priority_exit_emergency_mode();
        return true;
    }
    
    return false;
}

static uint8_t calculate_system_load(void) {
    // Simplified load calculation based on queue depths
    uint32_t total_queued = request_queue_get_total_depth();
    uint32_t total_capacity = 0;
    
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        total_capacity += manager_config.queue_config.queue_capacity[i];
    }
    
    if (total_capacity == 0) {
        return 0;
    }
    
    uint8_t load_percent = (total_queued * 100) / total_capacity;
    return load_percent > 100 ? 100 : load_percent;
}

static void handle_emergency_mode_transition(bool entering) {
    if (entering) {
        // Flush non-critical queues
        ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "Entering emergency mode - flushing non-critical queues");
        
        // In a real implementation, you might want to:
        // - Cancel non-critical operations
        // - Increase critical task priorities
        // - Reduce system resource usage
        
    } else {
        ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "Exiting emergency mode - resuming normal operation");
        
        // In a real implementation, you might want to:
        // - Restore normal task priorities
        // - Resume normal resource usage
        // - Clear emergency flags
    }
}

static uint32_t get_current_time_ms(void) {
    return esp_timer_get_time() / 1000;
}

static void feed_watchdog_if_needed(void) {
    static uint32_t last_watchdog_feed = 0;
    uint32_t current_time = get_current_time_ms();
    
    if (current_time - last_watchdog_feed > manager_config.load_config.watchdog_feed_interval_ms) {
        // Feed task watchdog if enabled
        esp_task_wdt_reset();
        last_watchdog_feed = current_time;
    }
}

/* =============================================================================
 * DEBUG AND SAFETY FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static bool is_valid_task_handle(TaskHandle_t handle) {
    if (!handle) {
        ESP_LOGD(DEBUG_PRIORITY_MANAGER_TAG, "HANDLE_CHECK: NULL handle");
        return false;
    }
    
    // Check if handle points to valid memory regions
    if (!esp_ptr_internal(handle) && !esp_ptr_in_dram(handle) && !esp_ptr_in_iram(handle)) {
        ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "HANDLE_CHECK: Handle %p not in valid memory region", handle);
        return false;
    }
    
    // Additional check for poison patterns
    uint32_t *handle_ptr = (uint32_t*)handle;
    if (*handle_ptr == 0xa5a5a5a5 || *handle_ptr == 0xdeadbeef || *handle_ptr == 0x00000000) {
        ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "HANDLE_CHECK: Handle %p contains poison pattern 0x%08lx", 
                 handle, *handle_ptr);
        return false;
    }
    
    ESP_LOGD(DEBUG_PRIORITY_MANAGER_TAG, "HANDLE_CHECK: Handle %p appears valid", handle);
    return true;
}

static void log_task_handle_operation(const char *operation, processing_task_type_t task_type, TaskHandle_t handle) {
    ESP_LOGI(DEBUG_PRIORITY_MANAGER_TAG, "TASK_HANDLE_%s: %s = %p", 
             operation, task_type_names[task_type], handle);
}

static bool safe_get_task_state(TaskHandle_t handle, eTaskState *state) {
    if (!handle || !state) {
        ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "SAFE_GET_STATE: Invalid parameters (handle=%p, state=%p)", 
                 handle, state);
        return false;
    }
    
    if (!is_valid_task_handle(handle)) {
        ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "SAFE_GET_STATE: Invalid handle %p", handle);
        return false;
    }
    
    ESP_LOGD(DEBUG_PRIORITY_MANAGER_TAG, "SAFE_GET_STATE: About to call eTaskGetState on handle %p", handle);
    
    // Try to get task state - this is where the crash occurs
    *state = eTaskGetState(handle);
    
    ESP_LOGD(DEBUG_PRIORITY_MANAGER_TAG, "SAFE_GET_STATE: eTaskGetState returned %d for handle %p", 
             *state, handle);
    
    return true;
}

static bool safe_get_stack_high_water(TaskHandle_t handle, UBaseType_t *stack_high_water) {
    if (!handle || !stack_high_water) {
        ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "SAFE_GET_STACK: Invalid parameters (handle=%p, stack=%p)", 
                 handle, stack_high_water);
        return false;
    }
    
    if (!is_valid_task_handle(handle)) {
        ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "SAFE_GET_STACK: Invalid handle %p", handle);
        return false;
    }
    
    ESP_LOGD(DEBUG_PRIORITY_MANAGER_TAG, "SAFE_GET_STACK: About to call uxTaskGetStackHighWaterMark on handle %p", handle);
    
    *stack_high_water = uxTaskGetStackHighWaterMark(handle);
    
    ESP_LOGD(DEBUG_PRIORITY_MANAGER_TAG, "SAFE_GET_STACK: uxTaskGetStackHighWaterMark returned %u for handle %p", 
             (unsigned int)*stack_high_water, handle);
    
    return true;
}
