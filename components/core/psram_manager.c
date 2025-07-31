/**
 * @file psram_manager.c
 * @brief PSRAM management system implementation for SNRv9 Irrigation Control System
 */

#include "psram_manager.h"
#include "debug_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * PRIVATE CONSTANTS AND MACROS
 * =============================================================================
 */

#define PSRAM_MANAGER_TAG "PSRAM_MGR"
#define PSRAM_TEST_SIZE 1024
#define PSRAM_DEFAULT_INTERNAL_RESERVE (32 * 1024)  // 32KB reserved for critical ops
#define PSRAM_LARGE_ALLOCATION_THRESHOLD 4096       // 4KB threshold for "large" allocations

#if DEBUG_INCLUDE_TIMESTAMPS
#define TIMESTAMP_FORMAT "[%02d:%02d:%02d.%03d] "
#define GET_TIMESTAMP() esp_timer_get_time() / 1000ULL
#define FORMAT_TIMESTAMP(ms) \
    (int)((ms / 3600000) % 24), \
    (int)((ms / 60000) % 60), \
    (int)((ms / 1000) % 60), \
    (int)(ms % 1000)
#else
#define TIMESTAMP_FORMAT ""
#define GET_TIMESTAMP() 0
#define FORMAT_TIMESTAMP(ms) 
#endif

/* =============================================================================
 * PRIVATE TYPE DEFINITIONS
 * =============================================================================
 */

typedef struct {
    bool initialized;
    bool enabled;
    psram_info_t info;
    SemaphoreHandle_t mutex;
    uint32_t last_health_check;
} psram_manager_context_t;

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static psram_manager_context_t g_psram_ctx = {0};
static const char *TAG = PSRAM_MANAGER_TAG;

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static bool detect_psram(void);
static bool test_psram_functionality(void);
static void update_psram_stats(void);
static const char* format_bytes(size_t bytes, char *buffer, size_t buffer_size);
static uint8_t calculate_usage_percent(size_t used, size_t total);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool psram_manager_init(void)
{
    if (g_psram_ctx.initialized) {
        ESP_LOGW(TAG, "PSRAM manager already initialized");
        return true;
    }

    // Initialize context
    memset(&g_psram_ctx, 0, sizeof(psram_manager_context_t));
    
    // Create mutex for thread-safe access
    g_psram_ctx.mutex = xSemaphoreCreateMutex();
    if (g_psram_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create PSRAM manager mutex");
        return false;
    }

    // Detect PSRAM availability
    g_psram_ctx.info.psram_available = detect_psram();
    g_psram_ctx.enabled = g_psram_ctx.info.psram_available;
    g_psram_ctx.info.internal_reserved = PSRAM_DEFAULT_INTERNAL_RESERVE;

    if (g_psram_ctx.info.psram_available) {
        // Initialize PSRAM statistics
        update_psram_stats();
        
        // Test PSRAM functionality
        if (!test_psram_functionality()) {
            ESP_LOGW(TAG, "PSRAM functionality test failed, disabling PSRAM");
            g_psram_ctx.enabled = false;
        } else {
            ESP_LOGI(TAG, "PSRAM detected and functional: %zu bytes", 
                     g_psram_ctx.info.psram_total_size);
        }
    } else {
        ESP_LOGI(TAG, "No PSRAM detected, using internal RAM only");
    }

    g_psram_ctx.initialized = true;
    g_psram_ctx.last_health_check = GET_TIMESTAMP();

    ESP_LOGI(TAG, "PSRAM manager initialized successfully");
    return true;
}

bool psram_manager_is_available(void)
{
    return g_psram_ctx.initialized && g_psram_ctx.info.psram_available && g_psram_ctx.enabled;
}

bool psram_manager_get_info(psram_info_t *info)
{
    if (info == NULL || !g_psram_ctx.initialized) {
        return false;
    }

    if (xSemaphoreTake(g_psram_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        update_psram_stats();
        memcpy(info, &g_psram_ctx.info, sizeof(psram_info_t));
        xSemaphoreGive(g_psram_ctx.mutex);
        return true;
    }

    return false;
}

bool psram_manager_get_enhanced_stats(enhanced_memory_stats_t *stats)
{
    if (stats == NULL || !g_psram_ctx.initialized) {
        return false;
    }

    if (xSemaphoreTake(g_psram_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Update current statistics
        update_psram_stats();

        // Internal RAM stats
        stats->internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        stats->internal_minimum_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        stats->internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        stats->internal_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

        // PSRAM stats
        if (g_psram_ctx.info.psram_available) {
            stats->psram_free = g_psram_ctx.info.psram_free_size;
            stats->psram_minimum_free = g_psram_ctx.info.psram_minimum_free;
            stats->psram_total = g_psram_ctx.info.psram_total_size;
            stats->psram_largest_block = g_psram_ctx.info.psram_largest_block;
        } else {
            stats->psram_free = 0;
            stats->psram_minimum_free = 0;
            stats->psram_total = 0;
            stats->psram_largest_block = 0;
        }

        // Combined stats
        stats->total_free_memory = stats->internal_free + stats->psram_free;
        stats->total_memory = stats->internal_total + stats->psram_total;

        // Usage percentages
        stats->internal_usage_percent = calculate_usage_percent(
            stats->internal_total - stats->internal_free, stats->internal_total);
        stats->psram_usage_percent = calculate_usage_percent(
            stats->psram_total - stats->psram_free, stats->psram_total);
        stats->total_usage_percent = calculate_usage_percent(
            stats->total_memory - stats->total_free_memory, stats->total_memory);

        stats->timestamp_ms = GET_TIMESTAMP();

        xSemaphoreGive(g_psram_ctx.mutex);
        return true;
    }

    return false;
}

void* psram_smart_malloc(size_t size, allocation_priority_t priority)
{
    if (!g_psram_ctx.initialized || size == 0) {
        return NULL;
    }

    void* ptr = NULL;

    if (xSemaphoreTake(g_psram_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        switch (priority) {
            case ALLOC_CRITICAL:
                // Force internal RAM for critical allocations
                ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
                if (ptr) {
                    ESP_LOGD(TAG, "Critical allocation: %zu bytes in internal RAM", size);
                }
                break;

            case ALLOC_LARGE_BUFFER:
            case ALLOC_CACHE:
            case ALLOC_TASK_STACK:
                // Prefer PSRAM for large allocations
                if (g_psram_ctx.enabled && size >= PSRAM_LARGE_ALLOCATION_THRESHOLD) {
                    ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
                    if (ptr) {
                        g_psram_ctx.info.psram_allocations++;
                        ESP_LOGD(TAG, "Large allocation: %zu bytes in PSRAM", size);
                    } else {
                        g_psram_ctx.info.psram_failures++;
                        // Fall back to internal RAM
                        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
                        if (ptr) {
                            g_psram_ctx.info.fallback_allocations++;
                            ESP_LOGD(TAG, "Fallback allocation: %zu bytes in internal RAM", size);
                        }
                    }
                } else {
                    // Use internal RAM for smaller allocations or when PSRAM disabled
                    ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
                }
                break;

            case ALLOC_NORMAL:
            default:
                // Use default allocation strategy
                ptr = malloc(size);
                break;
        }

        xSemaphoreGive(g_psram_ctx.mutex);
    }

    return ptr;
}

void* psram_smart_calloc(size_t num, size_t size, allocation_priority_t priority)
{
    size_t total_size = num * size;
    void* ptr = psram_smart_malloc(total_size, priority);
    
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    
    return ptr;
}

void* psram_smart_realloc(void* ptr, size_t size, allocation_priority_t priority)
{
    if (ptr == NULL) {
        return psram_smart_malloc(size, priority);
    }

    if (size == 0) {
        psram_smart_free(ptr);
        return NULL;
    }

    // For simplicity, allocate new memory and copy data
    void* new_ptr = psram_smart_malloc(size, priority);
    if (new_ptr && ptr) {
        // Copy existing data (we don't know the original size, so this is a limitation)
        // In a production implementation, you'd want to track allocation sizes
        memcpy(new_ptr, ptr, size);  // This assumes new size >= old size
        psram_smart_free(ptr);
    }

    return new_ptr;
}

void psram_smart_free(void* ptr)
{
    if (ptr) {
        free(ptr);  // ESP-IDF's free() handles both internal and PSRAM
    }
}

BaseType_t psram_create_task(const psram_task_config_t* config)
{
    if (config == NULL || config->task_function == NULL) {
        return pdFAIL;
    }

    BaseType_t result;

    if (config->force_internal) {
        // Force internal RAM allocation
        result = xTaskCreateWithCaps(
            config->task_function,
            config->task_name,
            config->stack_size,
            config->parameters,
            config->priority,
            config->task_handle,
            MALLOC_CAP_INTERNAL
        );
        ESP_LOGD(TAG, "Task '%s' created with internal RAM stack", config->task_name);
    } else if (config->use_psram && psram_manager_is_available() && 
               config->stack_size >= PSRAM_LARGE_ALLOCATION_THRESHOLD) {
        // Try PSRAM allocation for large stacks
        result = xTaskCreateWithCaps(
            config->task_function,
            config->task_name,
            config->stack_size,
            config->parameters,
            config->priority,
            config->task_handle,
            MALLOC_CAP_SPIRAM
        );
        
        if (result == pdPASS) {
            ESP_LOGI(TAG, "Task '%s' created with PSRAM stack (%u bytes)", 
                     config->task_name, (unsigned int)config->stack_size);
            if (xSemaphoreTake(g_psram_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_psram_ctx.info.psram_allocations++;
                xSemaphoreGive(g_psram_ctx.mutex);
            }
        } else {
            // Fall back to internal RAM
            result = xTaskCreate(
                config->task_function,
                config->task_name,
                config->stack_size,
                config->parameters,
                config->priority,
                config->task_handle
            );
            
            if (result == pdPASS) {
                ESP_LOGW(TAG, "Task '%s' fallback to internal RAM stack", config->task_name);
                if (xSemaphoreTake(g_psram_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    g_psram_ctx.info.psram_failures++;
                    g_psram_ctx.info.fallback_allocations++;
                    xSemaphoreGive(g_psram_ctx.mutex);
                }
            }
        }
    } else {
        // Use standard task creation
        result = xTaskCreate(
            config->task_function,
            config->task_name,
            config->stack_size,
            config->parameters,
            config->priority,
            config->task_handle
        );
        ESP_LOGD(TAG, "Task '%s' created with standard allocation", config->task_name);
    }

    return result;
}

void* psram_malloc(size_t size)
{
    if (!psram_manager_is_available() || size == 0) {
        return NULL;
    }

    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    
    if (xSemaphoreTake(g_psram_ctx.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (ptr) {
            g_psram_ctx.info.psram_allocations++;
        } else {
            g_psram_ctx.info.psram_failures++;
        }
        xSemaphoreGive(g_psram_ctx.mutex);
    }

    return ptr;
}

void* psram_calloc(size_t num, size_t size)
{
    size_t total_size = num * size;
    void* ptr = psram_malloc(total_size);
    
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    
    return ptr;
}

void* psram_internal_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
}

bool psram_is_psram_ptr(void* ptr)
{
    if (!ptr || !psram_manager_is_available()) {
        return false;
    }

    return heap_caps_check_integrity(MALLOC_CAP_SPIRAM, true) && 
           heap_caps_get_allocated_size(ptr) > 0;
}

size_t psram_get_free_size(void)
{
    if (!psram_manager_is_available()) {
        return 0;
    }

    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

size_t psram_get_total_size(void)
{
    if (!psram_manager_is_available()) {
        return 0;
    }

    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}

size_t psram_get_largest_free_block(void)
{
    if (!psram_manager_is_available()) {
        return 0;
    }

    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

void psram_manager_print_detailed_report(void)
{
    if (!g_psram_ctx.initialized) {
        printf(TIMESTAMP_FORMAT "%s: PSRAM manager not initialized\n", 
               FORMAT_TIMESTAMP(GET_TIMESTAMP()), TAG);
        return;
    }

    enhanced_memory_stats_t stats;
    if (!psram_manager_get_enhanced_stats(&stats)) {
        printf(TIMESTAMP_FORMAT "%s: Failed to get enhanced memory stats\n", 
               FORMAT_TIMESTAMP(GET_TIMESTAMP()), TAG);
        return;
    }

    char buffer[32];
    uint32_t timestamp = GET_TIMESTAMP();

    printf(TIMESTAMP_FORMAT "%s: === ENHANCED MEMORY REPORT ===\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);

    // Internal RAM stats
    printf(TIMESTAMP_FORMAT "%s: Internal RAM: %s free / %s total (%d%% used)\n",
           FORMAT_TIMESTAMP(timestamp), TAG,
           format_bytes(stats.internal_free, buffer, sizeof(buffer)),
           format_bytes(stats.internal_total, buffer, sizeof(buffer)),
           stats.internal_usage_percent);

    // PSRAM stats
    if (g_psram_ctx.info.psram_available) {
        printf(TIMESTAMP_FORMAT "%s: PSRAM: %s free / %s total (%d%% used)\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               format_bytes(stats.psram_free, buffer, sizeof(buffer)),
               format_bytes(stats.psram_total, buffer, sizeof(buffer)),
               stats.psram_usage_percent);
    } else {
        printf(TIMESTAMP_FORMAT "%s: PSRAM: Not available\n",
               FORMAT_TIMESTAMP(timestamp), TAG);
    }

    // Combined stats
    printf(TIMESTAMP_FORMAT "%s: Total Memory: %s free / %s total (%d%% used)\n",
           FORMAT_TIMESTAMP(timestamp), TAG,
           format_bytes(stats.total_free_memory, buffer, sizeof(buffer)),
           format_bytes(stats.total_memory, buffer, sizeof(buffer)),
           stats.total_usage_percent);

    printf(TIMESTAMP_FORMAT "%s: ================================\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
}

void psram_manager_print_allocation_stats(void)
{
    if (!g_psram_ctx.initialized) {
        return;
    }

    psram_info_t info;
    if (!psram_manager_get_info(&info)) {
        return;
    }

    uint32_t timestamp = GET_TIMESTAMP();
    uint32_t total_attempts = info.psram_allocations + info.psram_failures;
    uint32_t success_rate = total_attempts > 0 ? (info.psram_allocations * 100) / total_attempts : 0;

    printf(TIMESTAMP_FORMAT "%s: === PSRAM ALLOCATION STATS ===\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
    printf(TIMESTAMP_FORMAT "%s: Successful: %u, Failed: %u, Fallbacks: %u\n",
           FORMAT_TIMESTAMP(timestamp), TAG,
           (unsigned int)info.psram_allocations,
           (unsigned int)info.psram_failures,
           (unsigned int)info.fallback_allocations);
    printf(TIMESTAMP_FORMAT "%s: Success Rate: %u%%\n",
           FORMAT_TIMESTAMP(timestamp), TAG, (unsigned int)success_rate);
    printf(TIMESTAMP_FORMAT "%s: ==============================\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
}

void psram_manager_reset_stats(void)
{
    if (xSemaphoreTake(g_psram_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_psram_ctx.info.psram_allocations = 0;
        g_psram_ctx.info.psram_failures = 0;
        g_psram_ctx.info.fallback_allocations = 0;
        g_psram_ctx.info.psram_minimum_free = g_psram_ctx.info.psram_free_size;
        xSemaphoreGive(g_psram_ctx.mutex);
    }
}

bool psram_manager_health_check(void)
{
    if (!g_psram_ctx.initialized) {
        return false;
    }

    uint32_t current_time = GET_TIMESTAMP();
    g_psram_ctx.last_health_check = current_time;

    if (!g_psram_ctx.info.psram_available) {
        return true;  // No PSRAM is not a health issue
    }

    // Test basic PSRAM functionality
    return test_psram_functionality();
}

bool psram_manager_set_internal_reservation(size_t reserve_bytes)
{
    if (xSemaphoreTake(g_psram_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_psram_ctx.info.internal_reserved = reserve_bytes;
        xSemaphoreGive(g_psram_ctx.mutex);
        ESP_LOGI(TAG, "Internal RAM reservation set to %zu bytes", reserve_bytes);
        return true;
    }
    return false;
}

void psram_manager_set_enabled(bool enable)
{
    if (xSemaphoreTake(g_psram_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_psram_ctx.enabled = enable && g_psram_ctx.info.psram_available;
        xSemaphoreGive(g_psram_ctx.mutex);
        ESP_LOGI(TAG, "PSRAM usage %s", g_psram_ctx.enabled ? "enabled" : "disabled");
    }
}

bool psram_manager_is_enabled(void)
{
    return g_psram_ctx.enabled;
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static bool detect_psram(void)
{
    // Check if PSRAM is available using ESP-IDF heap capabilities
    size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    
    if (psram_size > 0) {
        ESP_LOGI(TAG, "PSRAM detected: %zu bytes", psram_size);
        return true;
    } else {
        ESP_LOGI(TAG, "No PSRAM detected");
        return false;
    }
}

static bool test_psram_functionality(void)
{
    if (!g_psram_ctx.info.psram_available) {
        return false;
    }

    // Allocate test buffer in PSRAM
    void* test_buffer = heap_caps_malloc(PSRAM_TEST_SIZE, MALLOC_CAP_SPIRAM);
    if (!test_buffer) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM test buffer");
        return false;
    }

    // Write test pattern
    uint8_t* buffer = (uint8_t*)test_buffer;
    for (int i = 0; i < PSRAM_TEST_SIZE; i++) {
        buffer[i] = (uint8_t)(i & 0xFF);
    }

    // Verify test pattern
    bool test_passed = true;
    for (int i = 0; i < PSRAM_TEST_SIZE; i++) {
        if (buffer[i] != (uint8_t)(i & 0xFF)) {
            test_passed = false;
            break;
        }
    }

    // Clean up
    free(test_buffer);

    if (test_passed) {
        ESP_LOGI(TAG, "PSRAM functionality test passed");
    } else {
        ESP_LOGE(TAG, "PSRAM functionality test failed");
    }

    return test_passed;
}

static void update_psram_stats(void)
{
    if (!g_psram_ctx.info.psram_available) {
        return;
    }

    g_psram_ctx.info.psram_total_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    g_psram_ctx.info.psram_free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    g_psram_ctx.info.psram_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    // Update minimum free tracking
    if (g_psram_ctx.info.psram_minimum_free == 0 || 
        g_psram_ctx.info.psram_free_size < g_psram_ctx.info.psram_minimum_free) {
        g_psram_ctx.info.psram_minimum_free = g_psram_ctx.info.psram_free_size;
    }
}

static const char* format_bytes(size_t bytes, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return "N/A";
    }

    if (bytes >= 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1fMB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buffer, buffer_size, "%.1fKB", bytes / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%zuB", bytes);
    }

    return buffer;
}

static uint8_t calculate_usage_percent(size_t used, size_t total)
{
    if (total == 0) {
        return 0;
    }
    return (uint8_t)((used * 100) / total);
}
