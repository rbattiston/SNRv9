/**
 * @file memory_monitor.c
 * @brief Memory monitoring system implementation for SNRv9 Irrigation Control System
 */

#include "memory_monitor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * PRIVATE CONSTANTS AND MACROS
 * =============================================================================
 */

#define MEMORY_MONITOR_TASK_STACK_SIZE  3072  // Increased from 2048
#define MEMORY_MONITOR_TASK_PRIORITY    1
#define MEMORY_MONITOR_TASK_NAME        "mem_monitor"

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
    memory_monitor_status_t status;
    TaskHandle_t monitor_task_handle;
    SemaphoreHandle_t data_mutex;
    memory_trend_t trend_data;
    memory_stats_t current_stats;
    bool enabled;
    uint32_t last_report_time;
    uint32_t last_sample_time;
} memory_monitor_context_t;

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static memory_monitor_context_t g_mem_monitor = {0};
static const char *TAG = DEBUG_MEMORY_TAG;

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static void memory_monitor_task(void *pvParameters);
static void collect_memory_stats(memory_stats_t *stats);
static void add_sample_to_trend(const memory_stats_t *stats);
static void print_memory_report(const memory_stats_t *stats);
static const char* format_bytes(uint32_t bytes, char *buffer, size_t buffer_size);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool memory_monitor_init(void)
{
    if (g_mem_monitor.status != MEMORY_MONITOR_STOPPED) {
        ESP_LOGW(TAG, "Memory monitor already initialized");
        return false;
    }

    // Initialize context
    memset(&g_mem_monitor, 0, sizeof(memory_monitor_context_t));
    g_mem_monitor.status = MEMORY_MONITOR_STOPPED;
    g_mem_monitor.enabled = (DEBUG_MEMORY_MONITOR == 1);

    // Create mutex for thread-safe access
    g_mem_monitor.data_mutex = xSemaphoreCreateMutex();
    if (g_mem_monitor.data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create data mutex");
        g_mem_monitor.status = MEMORY_MONITOR_ERROR;
        return false;
    }

    // Initialize trend data
    g_mem_monitor.trend_data.write_index = 0;
    g_mem_monitor.trend_data.sample_count = 0;
    g_mem_monitor.trend_data.buffer_full = false;

    ESP_LOGI(TAG, "Memory monitor initialized successfully");
    return true;
}

bool memory_monitor_start(void)
{
    if (g_mem_monitor.status == MEMORY_MONITOR_RUNNING) {
        ESP_LOGW(TAG, "Memory monitor already running");
        return true;
    }

    if (g_mem_monitor.status == MEMORY_MONITOR_ERROR) {
        ESP_LOGE(TAG, "Cannot start memory monitor - in error state");
        return false;
    }

    if (!g_mem_monitor.enabled) {
        ESP_LOGI(TAG, "Memory monitor disabled by configuration");
        return true;
    }

    // Create monitoring task
    BaseType_t result = xTaskCreate(
        memory_monitor_task,
        MEMORY_MONITOR_TASK_NAME,
        MEMORY_MONITOR_TASK_STACK_SIZE,
        NULL,
        MEMORY_MONITOR_TASK_PRIORITY,
        &g_mem_monitor.monitor_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create memory monitor task");
        g_mem_monitor.status = MEMORY_MONITOR_ERROR;
        return false;
    }

    g_mem_monitor.status = MEMORY_MONITOR_RUNNING;
    g_mem_monitor.last_report_time = GET_TIMESTAMP();
    g_mem_monitor.last_sample_time = GET_TIMESTAMP();

    ESP_LOGI(TAG, "Memory monitor started successfully");
    return true;
}

bool memory_monitor_stop(void)
{
    if (g_mem_monitor.status != MEMORY_MONITOR_RUNNING) {
        ESP_LOGW(TAG, "Memory monitor not running");
        return true;
    }

    // Delete monitoring task
    if (g_mem_monitor.monitor_task_handle != NULL) {
        vTaskDelete(g_mem_monitor.monitor_task_handle);
        g_mem_monitor.monitor_task_handle = NULL;
    }

    g_mem_monitor.status = MEMORY_MONITOR_STOPPED;
    ESP_LOGI(TAG, "Memory monitor stopped");
    return true;
}

memory_monitor_status_t memory_monitor_get_status(void)
{
    return g_mem_monitor.status;
}

bool memory_monitor_get_current_stats(memory_stats_t *stats)
{
    if (stats == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_mem_monitor.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        collect_memory_stats(stats);
        xSemaphoreGive(g_mem_monitor.data_mutex);
        return true;
    }

    return false;
}

bool memory_monitor_get_trend_data(memory_trend_t *trend)
{
    if (trend == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_mem_monitor.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(trend, &g_mem_monitor.trend_data, sizeof(memory_trend_t));
        xSemaphoreGive(g_mem_monitor.data_mutex);
        return true;
    }

    return false;
}

void memory_monitor_force_report(void)
{
    if (!g_mem_monitor.enabled) {
        return;
    }

    memory_stats_t stats;
    if (memory_monitor_get_current_stats(&stats)) {
        print_memory_report(&stats);
    }
}

uint8_t memory_monitor_calc_usage_percent(const memory_stats_t *stats)
{
    if (stats == NULL || stats->total_heap == 0) {
        return 0;
    }

    uint32_t used = stats->total_heap - stats->free_heap;
    return (uint8_t)((used * 100) / stats->total_heap);
}

uint8_t memory_monitor_calc_fragmentation_percent(const memory_stats_t *stats)
{
    if (stats == NULL || stats->free_heap == 0) {
        return 0;
    }

    uint32_t fragmented = stats->free_heap - stats->largest_free_block;
    return (uint8_t)((fragmented * 100) / stats->free_heap);
}

bool memory_monitor_get_trend_summary(uint32_t *avg_free, uint32_t *min_free, uint32_t *max_free)
{
    if (avg_free == NULL || min_free == NULL || max_free == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_mem_monitor.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint16_t count = g_mem_monitor.trend_data.sample_count;
        if (count == 0) {
            xSemaphoreGive(g_mem_monitor.data_mutex);
            return false;
        }

        uint64_t sum = 0;
        uint32_t min_val = UINT32_MAX;
        uint32_t max_val = 0;

        for (uint16_t i = 0; i < count; i++) {
            uint32_t free_heap = g_mem_monitor.trend_data.samples[i].free_heap;
            sum += free_heap;
            if (free_heap < min_val) min_val = free_heap;
            if (free_heap > max_val) max_val = free_heap;
        }

        *avg_free = (uint32_t)(sum / count);
        *min_free = min_val;
        *max_free = max_val;

        xSemaphoreGive(g_mem_monitor.data_mutex);
        return true;
    }

    return false;
}

void memory_monitor_reset_trend_data(void)
{
    if (xSemaphoreTake(g_mem_monitor.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_mem_monitor.trend_data.write_index = 0;
        g_mem_monitor.trend_data.sample_count = 0;
        g_mem_monitor.trend_data.buffer_full = false;
        xSemaphoreGive(g_mem_monitor.data_mutex);
    }
}

void memory_monitor_set_enabled(bool enable)
{
    g_mem_monitor.enabled = enable;
    if (!enable && g_mem_monitor.status == MEMORY_MONITOR_RUNNING) {
        memory_monitor_stop();
    }
}

bool memory_monitor_is_enabled(void)
{
    return g_mem_monitor.enabled;
}

void memory_monitor_print_detailed_report(void)
{
    if (!g_mem_monitor.enabled) {
        return;
    }

    memory_stats_t stats;
    if (!memory_monitor_get_current_stats(&stats)) {
        return;
    }

    char buffer[32];
    uint32_t timestamp = GET_TIMESTAMP();

    printf(TIMESTAMP_FORMAT "%s: === DETAILED MEMORY REPORT ===\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
    
    printf(TIMESTAMP_FORMAT "%s: Free Heap: %s\n", 
           FORMAT_TIMESTAMP(timestamp), TAG, format_bytes(stats.free_heap, buffer, sizeof(buffer)));
    
    printf(TIMESTAMP_FORMAT "%s: Min Free: %s\n", 
           FORMAT_TIMESTAMP(timestamp), TAG, format_bytes(stats.minimum_free_heap, buffer, sizeof(buffer)));
    
    printf(TIMESTAMP_FORMAT "%s: Total Heap: %s\n", 
           FORMAT_TIMESTAMP(timestamp), TAG, format_bytes(stats.total_heap, buffer, sizeof(buffer)));
    
    printf(TIMESTAMP_FORMAT "%s: Largest Block: %s\n", 
           FORMAT_TIMESTAMP(timestamp), TAG, format_bytes(stats.largest_free_block, buffer, sizeof(buffer)));
    
    printf(TIMESTAMP_FORMAT "%s: Usage: %d%%, Fragmentation: %d%%\n", 
           FORMAT_TIMESTAMP(timestamp), TAG, 
           memory_monitor_calc_usage_percent(&stats),
           memory_monitor_calc_fragmentation_percent(&stats));

    // Print trend summary if available
    uint32_t avg_free, min_free, max_free;
    if (memory_monitor_get_trend_summary(&avg_free, &min_free, &max_free)) {
        printf(TIMESTAMP_FORMAT "%s: Trend - Avg: %s, Min: %s, Max: %s\n", 
               FORMAT_TIMESTAMP(timestamp), TAG,
               format_bytes(avg_free, buffer, sizeof(buffer)),
               format_bytes(min_free, buffer, sizeof(buffer)),
               format_bytes(max_free, buffer, sizeof(buffer)));
    }

    printf(TIMESTAMP_FORMAT "%s: ================================\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
}

void memory_monitor_print_trend_graph(void)
{
    // Implementation for ASCII graph - simplified for now
    printf(TIMESTAMP_FORMAT "%s: Memory trend graph not yet implemented\n", 
           FORMAT_TIMESTAMP(GET_TIMESTAMP()), TAG);
}

bool memory_monitor_check_for_leaks(void)
{
    // Simple leak detection based on trend analysis
    uint32_t avg_free, min_free, max_free;
    if (!memory_monitor_get_trend_summary(&avg_free, &min_free, &max_free)) {
        return false;
    }

    // If minimum is significantly lower than average, potential leak
    if (min_free < (avg_free * 80 / 100)) {
        return true;
    }

    return false;
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static void memory_monitor_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Memory monitor task started");

    while (g_mem_monitor.status == MEMORY_MONITOR_RUNNING) {
        uint32_t current_time = GET_TIMESTAMP();

        // Collect memory sample if it's time
        if (DEBUG_MEMORY_TRENDING && 
            (current_time - g_mem_monitor.last_sample_time) >= DEBUG_MEMORY_SAMPLE_INTERVAL_MS) {
            
            memory_stats_t stats;
            collect_memory_stats(&stats);
            add_sample_to_trend(&stats);
            g_mem_monitor.last_sample_time = current_time;
        }

        // Print report if it's time
        if ((current_time - g_mem_monitor.last_report_time) >= DEBUG_MEMORY_REPORT_INTERVAL_MS) {
            memory_stats_t stats;
            collect_memory_stats(&stats);
            print_memory_report(&stats);
            g_mem_monitor.last_report_time = current_time;
        }

        // Sleep for a short time to prevent busy waiting
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Memory monitor task ended");
    vTaskDelete(NULL);
}

static void collect_memory_stats(memory_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    stats->free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    stats->minimum_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    stats->total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    stats->largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    stats->timestamp_ms = GET_TIMESTAMP();

    // Update current stats in context
    if (xSemaphoreTake(g_mem_monitor.data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(&g_mem_monitor.current_stats, stats, sizeof(memory_stats_t));
        xSemaphoreGive(g_mem_monitor.data_mutex);
    }
}

static void add_sample_to_trend(const memory_stats_t *stats)
{
    if (stats == NULL || !DEBUG_MEMORY_TRENDING) {
        return;
    }

    if (xSemaphoreTake(g_mem_monitor.data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memory_trend_t *trend = &g_mem_monitor.trend_data;
        
        // Add sample to ring buffer
        memcpy(&trend->samples[trend->write_index], stats, sizeof(memory_stats_t));
        
        // Update indices
        trend->write_index = (trend->write_index + 1) % DEBUG_MEMORY_HISTORY_SIZE;
        
        if (!trend->buffer_full) {
            trend->sample_count++;
            if (trend->sample_count >= DEBUG_MEMORY_HISTORY_SIZE) {
                trend->buffer_full = true;
            }
        }
        
        xSemaphoreGive(g_mem_monitor.data_mutex);
    }
}

static void print_memory_report(const memory_stats_t *stats)
{
    if (stats == NULL || !g_mem_monitor.enabled) {
        return;
    }

    char free_buf[16], min_buf[16];
    uint32_t timestamp = GET_TIMESTAMP();

    if (DEBUG_MEMORY_DETAILED) {
        printf(TIMESTAMP_FORMAT "%s: Free=%s Min=%s Usage=%d%% Frag=%d%%\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               format_bytes(stats->free_heap, free_buf, sizeof(free_buf)),
               format_bytes(stats->minimum_free_heap, min_buf, sizeof(min_buf)),
               memory_monitor_calc_usage_percent(stats),
               memory_monitor_calc_fragmentation_percent(stats));
    } else {
        printf(TIMESTAMP_FORMAT "%s: Free=%s Min=%s\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               format_bytes(stats->free_heap, free_buf, sizeof(free_buf)),
               format_bytes(stats->minimum_free_heap, min_buf, sizeof(min_buf)));
    }
}

static const char* format_bytes(uint32_t bytes, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return "N/A";
    }

    if (bytes >= 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1fMB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buffer, buffer_size, "%.1fKB", bytes / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%uB", (unsigned int)bytes);
    }

    return buffer;
}
