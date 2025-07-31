/**
 * @file psram_task_examples.c
 * @brief Example implementations showing PSRAM-aware task creation for SNRv9
 * 
 * This file demonstrates how to use the PSRAM manager to create tasks with
 * intelligent memory allocation, maximizing system memory efficiency.
 */

#include "psram_manager.h"
#include "debug_config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "PSRAM_EXAMPLES";

/* =============================================================================
 * EXAMPLE TASK FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Example web server task that benefits from PSRAM allocation
 */
static void web_server_task_example(void *pvParameters)
{
    ESP_LOGI(TAG, "Web server task started with PSRAM stack");
    
    // Allocate large HTTP buffers in PSRAM
    char* request_buffer = psram_smart_malloc(32 * 1024, ALLOC_LARGE_BUFFER);
    char* response_buffer = psram_smart_malloc(64 * 1024, ALLOC_LARGE_BUFFER);
    char* file_cache = psram_smart_malloc(256 * 1024, ALLOC_CACHE);
    
    if (request_buffer && response_buffer && file_cache) {
        ESP_LOGI(TAG, "Successfully allocated large buffers in PSRAM");
        
        // Simulate web server work
        while (1) {
            // Process HTTP requests using PSRAM buffers
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } else {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffers");
    }
    
    // Clean up
    psram_smart_free(request_buffer);
    psram_smart_free(response_buffer);
    psram_smart_free(file_cache);
    
    vTaskDelete(NULL);
}

/**
 * @brief Example data processing task using PSRAM for large datasets
 */
static void data_processing_task_example(void *pvParameters)
{
    ESP_LOGI(TAG, "Data processing task started with PSRAM stack");
    
    // Allocate large data processing buffers
    float* sensor_data = psram_smart_malloc(10000 * sizeof(float), ALLOC_LARGE_BUFFER);
    uint8_t* image_buffer = psram_smart_malloc(640 * 480 * 3, ALLOC_LARGE_BUFFER);
    
    if (sensor_data && image_buffer) {
        ESP_LOGI(TAG, "Successfully allocated data processing buffers in PSRAM");
        
        // Simulate data processing
        for (int i = 0; i < 10000; i++) {
            sensor_data[i] = i * 0.1f;
        }
        
        ESP_LOGI(TAG, "Data processing completed");
    } else {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffers for data processing");
    }
    
    // Clean up
    psram_smart_free(sensor_data);
    psram_smart_free(image_buffer);
    
    vTaskDelete(NULL);
}

/**
 * @brief Example critical task that must use internal RAM
 */
static void critical_task_example(void *pvParameters)
{
    ESP_LOGI(TAG, "Critical task started with internal RAM stack");
    
    // Allocate critical data in internal RAM for fast access
    uint32_t* critical_data = psram_smart_malloc(1024, ALLOC_CRITICAL);
    
    if (critical_data) {
        ESP_LOGI(TAG, "Critical data allocated in internal RAM");
        
        // Simulate critical operations
        for (int i = 0; i < 256; i++) {
            critical_data[i] = i * 2;
        }
        
        ESP_LOGI(TAG, "Critical operations completed");
    }
    
    psram_smart_free(critical_data);
    vTaskDelete(NULL);
}

/* =============================================================================
 * PUBLIC EXAMPLE FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Create example web server task using PSRAM
 */
bool psram_create_web_server_task_example(void)
{
    psram_task_config_t config = {
        .task_function = web_server_task_example,
        .task_name = "web_server_psram",
        .stack_size = 8192,  // Large stack for web server
        .parameters = NULL,
        .priority = 5,
        .task_handle = NULL,
        .use_psram = true,
        .force_internal = false
    };
    
    BaseType_t result = psram_create_task(&config);
    if (result == pdPASS) {
        ESP_LOGI(TAG, "Web server task created successfully with PSRAM");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to create web server task");
        return false;
    }
}

/**
 * @brief Create example data processing task using PSRAM
 */
bool psram_create_data_processing_task_example(void)
{
    psram_task_config_t config = {
        .task_function = data_processing_task_example,
        .task_name = "data_proc_psram",
        .stack_size = 6144,  // Large stack for data processing
        .parameters = NULL,
        .priority = 3,
        .task_handle = NULL,
        .use_psram = true,
        .force_internal = false
    };
    
    BaseType_t result = psram_create_task(&config);
    if (result == pdPASS) {
        ESP_LOGI(TAG, "Data processing task created successfully with PSRAM");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to create data processing task");
        return false;
    }
}

/**
 * @brief Create example critical task using internal RAM
 */
bool psram_create_critical_task_example(void)
{
    psram_task_config_t config = {
        .task_function = critical_task_example,
        .task_name = "critical_internal",
        .stack_size = 2048,  // Smaller stack for critical task
        .parameters = NULL,
        .priority = 10,  // High priority
        .task_handle = NULL,
        .use_psram = false,
        .force_internal = true  // Force internal RAM for critical operations
    };
    
    BaseType_t result = psram_create_task(&config);
    if (result == pdPASS) {
        ESP_LOGI(TAG, "Critical task created successfully with internal RAM");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to create critical task");
        return false;
    }
}

/**
 * @brief Demonstrate PSRAM allocation strategies
 */
void psram_demonstrate_allocation_strategies(void)
{
    ESP_LOGI(TAG, "=== PSRAM ALLOCATION STRATEGY DEMONSTRATION ===");
    
    // Test different allocation priorities
    void* critical_ptr = psram_smart_malloc(1024, ALLOC_CRITICAL);
    void* normal_ptr = psram_smart_malloc(2048, ALLOC_NORMAL);
    void* large_buffer_ptr = psram_smart_malloc(32768, ALLOC_LARGE_BUFFER);
    void* cache_ptr = psram_smart_malloc(65536, ALLOC_CACHE);
    
    ESP_LOGI(TAG, "Critical allocation (1KB): %s", 
             critical_ptr ? "SUCCESS" : "FAILED");
    ESP_LOGI(TAG, "Normal allocation (2KB): %s", 
             normal_ptr ? "SUCCESS" : "FAILED");
    ESP_LOGI(TAG, "Large buffer allocation (32KB): %s", 
             large_buffer_ptr ? "SUCCESS" : "FAILED");
    ESP_LOGI(TAG, "Cache allocation (64KB): %s", 
             cache_ptr ? "SUCCESS" : "FAILED");
    
    // Check which allocations went to PSRAM
    ESP_LOGI(TAG, "Critical ptr in PSRAM: %s", 
             psram_is_psram_ptr(critical_ptr) ? "YES" : "NO");
    ESP_LOGI(TAG, "Large buffer ptr in PSRAM: %s", 
             psram_is_psram_ptr(large_buffer_ptr) ? "YES" : "NO");
    ESP_LOGI(TAG, "Cache ptr in PSRAM: %s", 
             psram_is_psram_ptr(cache_ptr) ? "YES" : "NO");
    
    // Clean up
    psram_smart_free(critical_ptr);
    psram_smart_free(normal_ptr);
    psram_smart_free(large_buffer_ptr);
    psram_smart_free(cache_ptr);
    
    ESP_LOGI(TAG, "=== DEMONSTRATION COMPLETE ===");
}

/**
 * @brief Run all PSRAM examples
 */
void psram_run_all_examples(void)
{
    if (!psram_manager_is_available()) {
        ESP_LOGW(TAG, "PSRAM not available, examples will use internal RAM");
    }
    
    ESP_LOGI(TAG, "Running PSRAM task creation examples...");
    
    // Demonstrate allocation strategies
    psram_demonstrate_allocation_strategies();
    
    // Create example tasks
    psram_create_web_server_task_example();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    psram_create_data_processing_task_example();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    psram_create_critical_task_example();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "All PSRAM examples completed");
}

/**
 * @brief Show current PSRAM usage and statistics
 */
void psram_show_usage_example(void)
{
    enhanced_memory_stats_t stats;
    if (psram_manager_get_enhanced_stats(&stats)) {
        ESP_LOGI(TAG, "=== CURRENT MEMORY USAGE ===");
        ESP_LOGI(TAG, "Internal RAM: %u%% used (%u KB free)",
                 stats.internal_usage_percent,
                 (unsigned int)(stats.internal_free / 1024));
        ESP_LOGI(TAG, "PSRAM: %u%% used (%u KB free)",
                 stats.psram_usage_percent,
                 (unsigned int)(stats.psram_free / 1024));
        ESP_LOGI(TAG, "Total Memory: %u%% used (%u KB free)",
                 stats.total_usage_percent,
                 (unsigned int)(stats.total_free_memory / 1024));
    }
    
    psram_info_t info;
    if (psram_manager_get_info(&info)) {
        ESP_LOGI(TAG, "=== PSRAM ALLOCATION STATISTICS ===");
        ESP_LOGI(TAG, "Successful allocations: %u",
                 (unsigned int)info.psram_allocations);
        ESP_LOGI(TAG, "Failed allocations: %u",
                 (unsigned int)info.psram_failures);
        ESP_LOGI(TAG, "Fallback allocations: %u",
                 (unsigned int)info.fallback_allocations);
    }
}
