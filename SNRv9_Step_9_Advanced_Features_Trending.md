# SNRv9 Complete Trending System - ESP32 ESP-IDF Architecture

## Overview

The SNRv9 Trending System is a comprehensive data collection and visualization framework built specifically for ESP32 ESP-IDF framework that provides real-time sensor data trending capabilities while maintaining strict memory constraints and system stability. The system integrates seamlessly with the existing SNRv9 IO system, Time Management, and web server infrastructure to provide production-grade data logging and visualization.

## ESP-IDF Core Architecture Components

### 1. Trending Manager Component (`components/core/trending_manager/`)

#### ESP-IDF Component Structure

**Header File: `include/trending_manager.h`**
```c
#ifndef TRENDING_MANAGER_H
#define TRENDING_MANAGER_H

#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_psram.h"
#include "io_manager.h"
#include "time_manager.h"
#include "event_logger.h"

// Trending Point Types
typedef enum {
    TRENDING_POINT_TYPE_AI = 1,
    TRENDING_POINT_TYPE_BI = 2,
    TRENDING_POINT_TYPE_BO = 3
} trending_point_type_t;

// Trending Configuration
typedef struct {
    char point_id[32];
    bool enabled;
    trending_point_type_t point_type;
    uint32_t sample_interval_seconds;  // AI only: 10s to 3600s
    bool auto_rotate_when_full;
    bool auto_flush_to_disk;
    char display_name[64];
    char units[16];
    
    // Runtime state
    uint32_t last_sample_time;
    uint32_t current_record_count;
    uint32_t historical_record_count;
    bool buffer_needs_flush;
    size_t cached_file_size;
    uint32_t last_file_size_update;
} trending_config_t;

// Trending Data Point
typedef struct {
    uint32_t timestamp;           // Unix timestamp (seconds)
    uint32_t relative_timestamp;  // Seconds since base timestamp
    float value;                  // AI: conditioned value, BI/BO: 0.0/1.0
    uint8_t flags;               // Future use
    bool is_state_change;        // true for BI/BO edges, false for AI samples
} trending_data_point_t;

// File Header Structure
typedef struct {
    char magic[4];               // 'TRND'
    uint16_t version;            // Format version
    uint8_t point_type;          // 1=AI, 2=BI, 3=BO
    uint8_t padding;
    uint32_t sample_interval;    // AI only, 0 for BI/BO
    uint32_t max_file_size;      // In bytes
    uint32_t base_timestamp;     // First sample timestamp
    uint32_t record_count;       // Current number of records
    uint8_t reserved[12];        // Future use
} __attribute__((packed)) trending_file_header_t;

// Buffer Allocation Structure
typedef struct {
    char point_id[32];
    trending_point_type_t point_type;
    uint8_t *current_buffer;
    uint8_t *historical_buffer;
    uint32_t current_record_count;
    uint32_t historical_record_count;
    bool needs_flush;
    uint32_t last_sample_time;
    size_t buffer_size;
} trending_buffer_allocation_t;

// File Metadata
typedef struct {
    char point_id[32];
    char file_path[128];
    size_t file_size_bytes;
    uint32_t record_count;
    uint32_t oldest_timestamp;
    uint32_t newest_timestamp;
    trending_point_type_t point_type;
    bool file_exists;
    char display_size[16];       // "156.2 KB"
    char data_span[32];          // "3 days, 14 hours"
} trending_file_metadata_t;

// Core API
esp_err_t trending_manager_init(void);
esp_err_t trending_manager_deinit(void);
esp_err_t trending_manager_start(void);
esp_err_t trending_manager_stop(void);

// Configuration Management
esp_err_t trending_manager_enable_point(const char *point_id, const trending_config_t *config);
esp_err_t trending_manager_disable_point(const char *point_id);
esp_err_t trending_manager_get_config(const char *point_id, trending_config_t *config);
esp_err_t trending_manager_set_config(const char *point_id, const trending_config_t *config);
esp_err_t trending_manager_list_configs(trending_config_t **configs, size_t *count);

// Data Collection
esp_err_t trending_manager_collect_ai_sample(const char *point_id, float conditioned_value);
esp_err_t trending_manager_collect_state_change(const char *point_id, bool state);

// File Management
esp_err_t trending_manager_get_file_metadata(const char *point_id, trending_file_metadata_t *metadata);
esp_err_t trending_manager_list_files(trending_file_metadata_t **files, size_t *count);
esp_err_t trending_manager_delete_point_data(const char *point_id);
esp_err_t trending_manager_flush_buffers(void);

// Data Access
esp_err_t trending_manager_get_records(const char *point_id, trending_data_point_t **records, size_t *count);
esp_err_t trending_manager_get_raw_file_data(const char *point_id, uint8_t **data, size_t *size);

// System Status
esp_err_t trending_manager_get_system_status(size_t *psram_used, size_t *psram_available, 
                                           size_t *heap_used, bool *memory_healthy);

// Task Functions
void trending_data_collection_task(void *pvParameters);
void trending_file_flush_task(void *pvParameters);
void trending_heap_monitor_task(void *pvParameters);

#endif // TRENDING_MANAGER_H
```

#### ESP-IDF Implementation Features

**FreeRTOS Task Integration**:
```c
#define TRENDING_DATA_COLLECTION_TASK_STACK_SIZE 4096
#define TRENDING_FILE_FLUSH_TASK_STACK_SIZE 4096
#define TRENDING_HEAP_MONITOR_TASK_STACK_SIZE 2048

#define TRENDING_DATA_COLLECTION_TASK_PRIORITY 1
#define TRENDING_FILE_FLUSH_TASK_PRIORITY 0
#define TRENDING_HEAP_MONITOR_TASK_PRIORITY 0

#define DATA_COLLECTION_INTERVAL_MS 30000  // 30 seconds
#define FILE_FLUSH_INTERVAL_MS 600000      // 10 minutes
#define HEAP_MONITOR_INTERVAL_MS 120000    // 2 minutes

static TaskHandle_t trending_data_collection_task_handle = NULL;
static TaskHandle_t trending_file_flush_task_handle = NULL;
static TaskHandle_t trending_heap_monitor_task_handle = NULL;
static SemaphoreHandle_t trending_manager_mutex = NULL;
static bool trending_manager_running = false;

void trending_data_collection_task(void *pvParameters) {
    TickType_t last_execution = 0;
    const TickType_t execution_interval = pdMS_TO_TICKS(DATA_COLLECTION_INTERVAL_MS);
    
    ESP_LOGI(TAG, "Trending data collection task started");
    
    while (1) {
        TickType_t current_tick = xTaskGetTickCount();
        
        // Process data collection every 30 seconds
        if ((current_tick - last_execution) >= execution_interval) {
            xSemaphoreTake(trending_manager_mutex, portMAX_DELAY);
            
            if (trending_manager_running) {
                esp_err_t err = process_data_collection();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Data collection failed: %s", esp_err_to_name(err));
                }
                last_execution = current_tick;
            }
            
            xSemaphoreGive(trending_manager_mutex);
        }
        
        // Yield CPU to prevent watchdog timeout
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t trending_manager_init(void) {
    // Create mutex
    trending_manager_mutex = xSemaphoreCreateMutex();
    if (trending_manager_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize PSRAM buffer manager
    esp_err_t err = init_psram_buffer_manager();
    if (err != ESP_OK) {
        vSemaphoreDelete(trending_manager_mutex);
        return err;
    }
    
    // Initialize configuration storage
    err = init_configuration_storage();
    if (err != ESP_OK) {
        deinit_psram_buffer_manager();
        vSemaphoreDelete(trending_manager_mutex);
        return err;
    }
    
    // Create tasks
    BaseType_t result = xTaskCreate(
        trending_data_collection_task,
        "trending_data",
        TRENDING_DATA_COLLECTION_TASK_STACK_SIZE,
        NULL,
        TRENDING_DATA_COLLECTION_TASK_PRIORITY,
        &trending_data_collection_task_handle
    );
    
    if (result != pdPASS) {
        deinit_configuration_storage();
        deinit_psram_buffer_manager();
        vSemaphoreDelete(trending_manager_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    result = xTaskCreate(
        trending_file_flush_task,
        "trending_flush",
        TRENDING_FILE_FLUSH_TASK_STACK_SIZE,
        NULL,
        TRENDING_FILE_FLUSH_TASK_PRIORITY,
        &trending_file_flush_task_handle
    );
    
    if (result != pdPASS) {
        vTaskDelete(trending_data_collection_task_handle);
        deinit_configuration_storage();
        deinit_psram_buffer_manager();
        vSemaphoreDelete(trending_manager_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    result = xTaskCreate(
        trending_heap_monitor_task,
        "trending_heap",
        TRENDING_HEAP_MONITOR_TASK_STACK_SIZE,
        NULL,
        TRENDING_HEAP_MONITOR_TASK_PRIORITY,
        &trending_heap_monitor_task_handle
    );
    
    if (result != pdPASS) {
        vTaskDelete(trending_file_flush_task_handle);
        vTaskDelete(trending_data_collection_task_handle);
        deinit_configuration_storage();
        deinit_psram_buffer_manager();
        vSemaphoreDelete(trending_manager_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Trending Manager initialized successfully");
    return ESP_OK;
}
```

### 2. PSRAM Buffer Management (ESP-IDF Implementation)

#### PSRAM Allocation Strategy

**ESP-IDF PSRAM Integration**:
```c
// PSRAM allocation constants
#define TRENDING_AI_FILE_SIZE_BYTES        (100 * 1024)  // 100KB per AI file
#define TRENDING_BIBO_FILE_SIZE_BYTES      (30 * 1024)   // 30KB per BI/BO file
#define TRENDING_HEADER_SIZE               32
#define TRENDING_AI_RECORD_SIZE            9   // 4+4+1 bytes
#define TRENDING_BIBO_RECORD_SIZE          5   // 4+1 bytes

// Maximum records per file type
#define TRENDING_AI_MAX_RECORDS            ((TRENDING_AI_FILE_SIZE_BYTES - TRENDING_HEADER_SIZE) / TRENDING_AI_RECORD_SIZE)
#define TRENDING_BIBO_MAX_RECORDS          ((TRENDING_BIBO_FILE_SIZE_BYTES - TRENDING_HEADER_SIZE) / TRENDING_BIBO_RECORD_SIZE)

// PSRAM allocation sizes (current + historical buffers)
#define TRENDING_AI_TOTAL_PSRAM_PER_POINT  (TRENDING_AI_FILE_SIZE_BYTES * 2)      // 200KB per AI point
#define TRENDING_BIBO_TOTAL_PSRAM_PER_POINT (TRENDING_BIBO_FILE_SIZE_BYTES * 2)   // 60KB per BI/BO point

// System limits
#define TRENDING_MAX_AI_POINTS             8   // 8 * 200KB = 1.6MB
#define TRENDING_MAX_BIBO_POINTS           16  // 16 * 60KB = 0.96MB
#define TRENDING_TOTAL_PSRAM_BUDGET        (3 * 1024 * 1024)  // 3MB total PSRAM budget

static trending_buffer_allocation_t buffer_allocations[TRENDING_MAX_AI_POINTS + TRENDING_MAX_BIBO_POINTS];
static size_t active_allocations = 0;
static SemaphoreHandle_t buffer_manager_mutex = NULL;
static uint8_t *psram_base = NULL;
static size_t psram_used = 0;

esp_err_t init_psram_buffer_manager(void) {
    // Check PSRAM availability
    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "PSRAM not initialized - trending system requires PSRAM");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    size_t psram_size = esp_psram_get_size();
    if (psram_size < TRENDING_TOTAL_PSRAM_BUDGET) {
        ESP_LOGW(TAG, "PSRAM size (%d bytes) less than trending budget (%d bytes)", 
                 psram_size, TRENDING_TOTAL_PSRAM_BUDGET);
    }
    
    // Create buffer manager mutex
    buffer_manager_mutex = xSemaphoreCreateMutex();
    if (buffer_manager_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // Allocate PSRAM base
    psram_base = (uint8_t*)ps_malloc(TRENDING_TOTAL_PSRAM_BUDGET);
    if (psram_base == NULL) {
        vSemaphoreDelete(buffer_manager_mutex);
        ESP_LOGE(TAG, "Failed to allocate PSRAM for trending system");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize allocation tracking
    memset(buffer_allocations, 0, sizeof(buffer_allocations));
    active_allocations = 0;
    psram_used = 0;
    
    ESP_LOGI(TAG, "PSRAM buffer manager initialized: %d bytes allocated", TRENDING_TOTAL_PSRAM_BUDGET);
    return ESP_OK;
}

esp_err_t allocate_point_buffers(const char *point_id, trending_point_type_t point_type) {
    if (point_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(buffer_manager_mutex, portMAX_DELAY);
    
    // Check if already allocated
    for (size_t i = 0; i < active_allocations; i++) {
        if (strcmp(buffer_allocations[i].point_id, point_id) == 0) {
            xSemaphoreGive(buffer_manager_mutex);
            return ESP_OK; // Already allocated
        }
    }
    
    // Check system limits
    size_t ai_count = 0, bibo_count = 0;
    for (size_t i = 0; i < active_allocations; i++) {
        if (buffer_allocations[i].point_type == TRENDING_POINT_TYPE_AI) {
            ai_count++;
        } else {
            bibo_count++;
        }
    }
    
    if (point_type == TRENDING_POINT_TYPE_AI && ai_count >= TRENDING_MAX_AI_POINTS) {
        xSemaphoreGive(buffer_manager_mutex);
        ESP_LOGE(TAG, "Maximum AI trending points (%d) reached", TRENDING_MAX_AI_POINTS);
        return ESP_ERR_NO_MEM;
    }
    
    if (point_type != TRENDING_POINT_TYPE_AI && bibo_count >= TRENDING_MAX_BIBO_POINTS) {
        xSemaphoreGive(buffer_manager_mutex);
        ESP_LOGE(TAG, "Maximum BI/BO trending points (%d) reached", TRENDING_MAX_BIBO_POINTS);
        return ESP_ERR_NO_MEM;
    }
    
    // Calculate buffer size
    size_t buffer_size = (point_type == TRENDING_POINT_TYPE_AI) ? 
                        TRENDING_AI_TOTAL_PSRAM_PER_POINT : 
                        TRENDING_BIBO_TOTAL_PSRAM_PER_POINT;
    
    // Check PSRAM availability
    if (psram_used + buffer_size > TRENDING_TOTAL_PSRAM_BUDGET) {
        xSemaphoreGive(buffer_manager_mutex);
        ESP_LOGE(TAG, "Insufficient PSRAM for point %s", point_id);
        return ESP_ERR_NO_MEM;
    }
    
    // Allocate buffers
    trending_buffer_allocation_t *allocation = &buffer_allocations[active_allocations];
    strncpy(allocation->point_id, point_id, sizeof(allocation->point_id) - 1);
    allocation->point_type = point_type;
    allocation->current_buffer = psram_base + psram_used;
    allocation->historical_buffer = allocation->current_buffer + (buffer_size / 2);
    allocation->current_record_count = 0;
    allocation->historical_record_count = 0;
    allocation->needs_flush = false;
    allocation->last_sample_time = 0;
    allocation->buffer_size = buffer_size / 2; // Each buffer is half the total
    
    psram_used += buffer_size;
    active_allocations++;
    
    xSemaphoreGive(buffer_manager_mutex);
    
    ESP_LOGI(TAG, "Allocated buffers for point %s: %d bytes (PSRAM used: %d/%d)", 
             point_id, buffer_size, psram_used, TRENDING_TOTAL_PSRAM_BUDGET);
    return ESP_OK;
}
```

#### Buffer-to-File Synchronization

**ESP-IDF File Operations**:
```c
esp_err_t flush_buffer_to_file(const char *point_id) {
    if (point_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(buffer_manager_mutex, portMAX_DELAY);
    
    // Find buffer allocation
    trending_buffer_allocation_t *allocation = NULL;
    for (size_t i = 0; i < active_allocations; i++) {
        if (strcmp(buffer_allocations[i].point_id, point_id) == 0) {
            allocation = &buffer_allocations[i];
            break;
        }
    }
    
    if (allocation == NULL) {
        xSemaphoreGive(buffer_manager_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    if (!allocation->needs_flush) {
        xSemaphoreGive(buffer_manager_mutex);
        return ESP_OK; // Nothing to flush
    }
    
    // Create file path
    char file_path[128];
    snprintf(file_path, sizeof(file_path), "/data/trending/%s.trnd", point_id);
    
    // Create trending directory if it doesn't exist
    struct stat st;
    if (stat("/data/trending", &st) != 0) {
        if (mkdir("/data/trending", 0755) != 0) {
            xSemaphoreGive(buffer_manager_mutex);
            ESP_LOGE(TAG, "Failed to create trending directory");
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    // Open file for writing
    FILE *file = fopen(file_path, "wb");
    if (file == NULL) {
        xSemaphoreGive(buffer_manager_mutex);
        ESP_LOGE(TAG, "Failed to open file for writing: %s", file_path);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Write file header
    trending_file_header_t header = {
        .magic = {'T', 'R', 'N', 'D'},
        .version = 1,
        .point_type = allocation->point_type,
        .padding = 0,
        .sample_interval = 0, // Will be set based on config
        .max_file_size = (allocation->point_type == TRENDING_POINT_TYPE_AI) ? 
                        TRENDING_AI_FILE_SIZE_BYTES : TRENDING_BIBO_FILE_SIZE_BYTES,
        .base_timestamp = 0, // Will be set to first record timestamp
        .record_count = allocation->current_record_count,
        .reserved = {0}
    };
    
    size_t written = fwrite(&header, sizeof(header), 1, file);
    if (written != 1) {
        fclose(file);
        xSemaphoreGive(buffer_manager_mutex);
        ESP_LOGE(TAG, "Failed to write file header");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Write data records with yielding to prevent watchdog timeout
    size_t record_size = (allocation->point_type == TRENDING_POINT_TYPE_AI) ? 
                        TRENDING_AI_RECORD_SIZE : TRENDING_BIBO_RECORD_SIZE;
    
    for (uint32_t i = 0; i < allocation->current_record_count; i++) {
        uint8_t *record_data = allocation->current_buffer + TRENDING_HEADER_SIZE + (i * record_size);
        
        written = fwrite(record_data, record_size, 1, file);
        if (written != 1) {
            fclose(file);
            xSemaphoreGive(buffer_manager_mutex);
            ESP_LOGE(TAG, "Failed to write record %d", i);
            return ESP_ERR_INVALID_STATE;
        }
        
        // Yield every 100 records to prevent watchdog timeout
        if ((i % 100) == 99) {
            xSemaphoreGive(buffer_manager_mutex);
            vTaskDelay(pdMS_TO_TICKS(1));
            xSemaphoreTake(buffer_manager_mutex, portMAX_DELAY);
        }
    }
    
    fclose(file);
    
    // Mark buffer as flushed
    allocation->needs_flush = false;
    
    xSemaphoreGive(buffer_manager_mutex);
    
    ESP_LOGI(TAG, "Flushed %d records for point %s to file", 
             allocation->current_record_count, point_id);
    return ESP_OK;
}
```

### 3. Data Collection Engine (ESP-IDF Implementation)

#### AI Sample Collection

**ESP-IDF AI Data Collection**:
```c
esp_err_t trending_manager_collect_ai_sample(const char *point_id, float conditioned_value) {
    if (point_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if time is synced before proceeding
    if (!time_manager_is_ntp_synced()) {
        return ESP_ERR_INVALID_STATE; // Skip sample if time not synced
    }
    
    xSemaphoreTake(trending_manager_mutex, portMAX_DELAY);
    
    // Find trending configuration
    trending_config_t config;
    esp_err_t err = get_trending_config(point_id, &config);
    if (err != ESP_OK || !config.enabled) {
        xSemaphoreGive(trending_manager_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Check sample interval
    uint32_t current_time = time(NULL);
    if (current_time - config.last_sample_time < config.sample_interval_seconds) {
        xSemaphoreGive(trending_manager_mutex);
        return ESP_OK; // Not time for next sample yet
    }
    
    // Find buffer allocation
    trending_buffer_allocation_t *allocation = find_buffer_allocation(point_id);
    if (allocation == NULL) {
        xSemaphoreGive(trending_manager_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Check if buffer is full
    if (allocation->current_record_count >= TRENDING_AI_MAX_RECORDS) {
        // Rotate to historical buffer
        err = rotate_buffer_to_historical(point_id);
        if (err != ESP_OK) {
            xSemaphoreGive(trending_manager_mutex);
            return err;
        }
    }
    
    // Create data point
    trending_data_point_t data_point = {
        .timestamp = current_time,
        .relative_timestamp = current_time, // Will be adjusted based on base timestamp
        .value = conditioned_value,
        .flags = 0,
        .is_state_change = false
    };
    
    // Add to buffer
    err = add_data_point_to_buffer(allocation, &data_point);
    if (err != ESP_OK) {
        xSemaphoreGive(trending_manager_mutex);
        return err;
    }
    
    // Update configuration
    config.last_sample_time = current_time;
    config.current_record_count = allocation->current_record_count;
    config.buffer_needs_flush = true;
    set_trending_config(point_id, &config);
    
    allocation->needs_flush = true;
    
    xSemaphoreGive(trending_manager_mutex);
    
    ESP_LOGD(TAG, "Collected AI sample for %s: %.2f", point_id, conditioned_value);
    return ESP_OK;
}
```

#### BI/BO State Change Collection

**ESP-IDF State Change Collection**:
```c
esp_err_t trending_manager_collect_state_change(const char *point_id, bool state) {
    if (point_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if time is synced before proceeding
    if (!time_manager_is_ntp_synced()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTake(trending_manager_mutex, portMAX_DELAY);
    
    // Find trending configuration
    trending_config_t config;
    esp_err_t err = get_trending_config(point_id, &config);
    if (err != ESP_OK || !config.enabled) {
        xSemaphoreGive(trending_manager_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Find buffer allocation
    trending_buffer_allocation_t *allocation = find_buffer_allocation(point_id);
    if (allocation == NULL) {
        xSemaphoreGive(trending_manager_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Check if buffer is full
    if (allocation->current_record_count >= TRENDING_BIBO_MAX_RECORDS) {
        // Rotate to historical buffer
        err = rotate_buffer_to_historical(point_id);
        if (err != ESP_OK) {
            xSemaphoreGive(trending_manager_mutex);
            return err;
        }
    }
    
    // Create data point
    uint32_t current_time = time(NULL);
    trending_data_point_t data_point = {
        .timestamp = current_time,
        .relative_timestamp = current_time,
        .value = state ? 1.0f : 0.0f,
        .flags = 0,
        .is_state_change = true
    };
    
    // Add to buffer
    err = add_data_point_to_buffer(allocation, &data_point);
    if (err != ESP_OK) {
        xSemaphoreGive(trending_manager_mutex);
        return err;
    }
    
    // Update configuration
    config.last_sample_time = current_time;
    config.current_record_count = allocation->current_record_count;
    config.buffer_needs_flush = true;
    set_trending_config(point_id, &config);
    
    allocation->needs_flush = true;
    
    xSemaphoreGive(trending_manager_mutex);
    
    ESP_LOGD(TAG, "Collected state change for %s: %s", point_id, state ? "ON" : "OFF");
    return ESP_OK;
}
```

### 4. Web Interface Integration (`components/web/trending_controller/`)

#### ESP-IDF HTTP Server Integration

**Header File: `include/trending_controller.h`**
```c
#ifndef TRENDING_CONTROLLER_H
#define TRENDING_CONTROLLER_H

#include "esp_http_server.h"
#include "trending_manager.h"

// Core API
esp_err_t trending_controller_init(void);
esp_err_t trending_controller_register_endpoints(httpd_handle_t server);

// HTTP Handlers
esp_err_t trending_config_get_handler(httpd_req_t *req);
esp_err_t trending_config_post_handler(httpd_req_t *req);
esp_err_t trending_points_get_handler(httpd_req_t *req);
esp_err_t trending_status_get_handler(httpd_req_t *req);
esp_err_t trending_data_get_handler(httpd_req_t *req);
esp_err_t trending_list_get_handler(httpd_req_t *req);
esp_err_t trending_metadata_get_handler(httpd_req_t *req);
esp_err_t trending_records_get_handler(httpd_req_t *req);
esp_err_t trending_delete_handler(httpd_req_t *req);

#endif // TRENDING_CONTROLLER_H
```

#### RESTful API Implementation

**Configuration Management**:
```c
// GET /api/trending/config
esp_err_t trending_config_get_handler(httpd_req_t *req) {
    trending_config_t *configs = NULL;
    size_t config_count = 0;
    
    esp_err_t err = trending_manager_list_configs(&configs, &config_count);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get trending configurations");
        return ESP_FAIL;
    }
    
    // Create JSON response
    cJSON *json_array = cJSON_CreateArray();
    
    for (size_t i = 0; i < config_count; i++) {
        cJSON *config_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(config_obj, "pointId", configs[i].point_id);
        cJSON_AddBoolToObject(config_obj, "enabled", configs[i].enabled);
        cJSON_AddNumberToObject(config_obj, "pointType", configs[i].point_type);
        cJSON_AddNumberToObject(config_obj, "sampleIntervalSeconds", configs[i].sample_interval_seconds);
        cJSON_AddBoolToObject(config_obj, "autoRotateWhenFull", configs[i].auto_rotate_when_full);
        cJSON_AddBoolToObject(config_obj, "autoFlushToDisk", configs[i].auto_flush_to_disk);
        cJSON_AddStringToObject(config_obj, "displayName", configs[i].display_name);
        cJSON_AddStringToObject(config_obj, "units", configs[i].units);
        cJSON_AddNumberToObject(config_obj, "currentRecordCount", configs[i].current_record_count);
        cJSON_AddNumberToObject(config_obj, "historicalRecordCount", configs[i].historical_record_count);
        
        cJSON_AddItemToArray(json_array, config_obj);
    }
    
    // Wrap in success response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddItemToObject(response, "data", json_array);
    
    char *json_string = cJSON_Print(response);
    cJSON_Delete(response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    free(json_string);
    free(configs);
    return ESP_OK;
}
```

**Data Retrieval**:
```c
// GET /api/trending/data/{pointId}
esp_err_t trending_data_get_handler(httpd_req_t *req) {
    // Extract point ID from URI
    char point_id[32];
    esp_err_t err = extract_point_id_from_uri(req->uri, point_id, sizeof(point_id));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid point ID");
        return ESP_FAIL;
    }
    
    // Get trending records
    trending_data_point_t *records = NULL;
    size_t record_count = 0;
    
    err = trending_manager_get_records(point_id, &records, &record_count);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get trending data");
        return ESP_FAIL;
    }
    
    // Create JSON response
    cJSON *json_array = cJSON_CreateArray();
    
    for (size_t i = 0; i < record_count; i++) {
        cJSON *record_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(record_obj, "timestamp", records[i].timestamp);
        cJSON_AddNumberToObject(record_obj, "value", records[i].value);
        cJSON_AddBoolToObject(record_obj, "isStateChange", records[i].is_state_change);
        
        cJSON_AddItemToArray(json_array, record_obj);
    }
    
    // Wrap in success response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddNumberToObject(response, "recordCount", record_count);
    cJSON_AddItemToObject(response, "data", json_array);
    
    char *json_string = cJSON_Print(response);
    cJSON_Delete(response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    free(json_string);
    free(records);
    return ESP_OK;
}
```

### 5. Configuration Persistence (ESP-IDF Implementation)

#### LittleFS Storage Integration

**ESP-IDF Configuration Storage**:
```c
#define TRENDING_CONFIG_DIR "/data/trending_configs"

esp_err_t trending_manager_save_config(const char *point_id, const trending_config_t *config) {
    if (point_id == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Create config directory if it doesn't exist
    struct stat st;
    if (stat(TRENDING_CONFIG_DIR, &st) != 0) {
        if (mkdir(TRENDING_CONFIG_DIR, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create trending config directory");
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    // Create config file path
    char config_path[256];
    snprintf(config_path, sizeof(config_path), "%s/%s.json", TRENDING_CONFIG_DIR, point_id);
    
    // Create JSON configuration
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "pointId", config->point_id);
    cJSON_AddBoolToObject(json, "enabled", config->enabled);
    cJSON_AddNumberToObject(json, "pointType", config->point_type);
    cJSON_AddNumberToObject(json, "sampleIntervalSeconds", config->sample_interval_seconds);
    cJSON_AddBoolToObject(json, "autoRotateWhenFull", config->auto_rotate_when_full);
    cJSON_AddBoolToObject(json, "autoFlushToDisk", config->auto_flush_to_disk);
    cJSON_AddStringToObject(json, "displayName", config->display_name);
    cJSON_AddStringToObject(json, "units", config->units);
    
    // Write to file
    char *json_string = cJSON_Print(json);
    FILE *file = fopen(config_path, "w");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open config file for writing: %s", config_path);
        free(json_string);
        cJSON_Delete(json);
        return ESP_ERR_INVALID_STATE;
    }
    
    fprintf(file, "%s", json_string);
    fclose(file);
    
    free(json_string);
    cJSON_Delete(json);
    
    ESP_LOGI(TAG, "Trending configuration saved for point %s", point_id);
    return ESP_OK;
}

esp_err_t trending_manager_load_config(const char *point_id, trending_config_t *config) {
    if (point_id == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char config_path[256];
    snprintf(config_path, sizeof(config_path), "%s/%s.json", TRENDING_CONFIG_DIR, point_id);
    
    FILE *file = fopen(config_path, "r");
    if (!file) {
        // Return default configuration if file doesn't exist
        *config = get_default_trending_config(point_id);
        return ESP_OK;
    }
    
    // Read file content
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *json_string = malloc(file_size + 1);
    if (!json_string) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    
    fread(json_string, 1, file_size, file);
    json_string[file_size] = '\0';
    fclose(file);
    
    // Parse JSON
    cJSON *json = cJSON_Parse(json_string);
    free(json_string);
    
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse trending config JSON for point %s", point_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Extract configuration
    esp_err_t err = parse_trending_config_from_json(json, config);
    cJSON_Delete(json);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to extract trending config for point %s", point_id);
        return err;
    }
    
    ESP_LOGI(TAG, "Trending configuration loaded for point %s", point_id);
    return ESP_OK;
}
```

### 6. ESP-IDF Component Architecture

#### Component Configuration

**Trending Manager CMakeLists.txt**:
```cmake
idf_component_register(
    SRCS "trending_manager.c" "trending_buffer_manager.c" "trending_data_collection.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        "esp_littlefs"
        "esp_timer"
        "esp_psram"
        "freertos"
        "io_manager"
        "time_manager"
        "event_logger"
        "json"
    PRIV_REQUIRES
        "esp_system"
        "esp_crc"
)
```

**Web Trending Controller CMakeLists.txt**:
```cmake
idf_component_register(
    SRCS "trending_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        "esp_http_server"
        "json"
        "trending_manager"
        "auth_manager"
    PRIV_REQUIRES
        "esp_system"
)
```

### 7. Memory Management and Performance

#### ESP-IDF Memory Optimization

**Memory Pool for Trending Data**:
```c
#define MAX_TRENDING_POINTS 24

typedef struct {
    char point_id[32];
    trending_config_t config;
    bool in_use;
} trending_point_entry_t;

static trending_point_entry_t trending_points[MAX_TRENDING_POINTS];
static SemaphoreHandle_t trending_points_mutex = NULL;

esp_err_t initialize_trending_points(void) {
    trending_points_mutex = xSemaphoreCreateMutex();
    if (trending_points_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(trending_points, 0, sizeof(trending_points));
    
    ESP_LOGI(TAG, "Trending points pool initialized (%d slots)", MAX_TRENDING_POINTS);
    return ESP_OK;
}

esp_err_t add_trending_point(const char *point_id, const trending_config_t *config) {
    if (point_id == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(trending_points_mutex, portMAX_DELAY);
    
    // Check if already exists
    for (size_t i = 0; i < MAX_TRENDING_POINTS; i++) {
        if (trending_points[i].in_use && 
            strcmp(trending_points[i].point_id, point_id) == 0) {
            trending_points[i].config = *config;
            xSemaphoreGive(trending_points_mutex);
            return ESP_OK; // Updated existing
        }
    }
    
    // Find free slot
    for (size_t i = 0; i < MAX_TRENDING_POINTS; i++) {
        if (!trending_points[i].in_use) {
            strncpy(trending_points[i].point_id, point_id, sizeof(trending_points[i].point_id) - 1);
            trending_points[i].config = *config;
            trending_points[i].in_use = true;
            
            xSemaphoreGive(trending_points_mutex);
            
            // Allocate PSRAM buffers
            esp_err_t err = allocate_point_buffers(point_id, config->point_type);
            if (err != ESP_OK) {
                // Remove from trending points if buffer allocation fails
                trending_points[i].in_use = false;
                return err;
            }
            
            ESP_LOGI(TAG, "Added trending point %s (slot %d)", point_id, i);
            return ESP_OK;
        }
    }
    
    xSemaphoreGive(trending_points_mutex);
    ESP_LOGE(TAG, "No free slots for trending point %s", point_id);
    return ESP_ERR_NO_MEM;
}
```

#### Performance Considerations

**ESP-IDF Performance Patterns**:
- **Task Priority**: Data Collection (1), File Flush (0), Heap Monitor (0)
- **Collection Interval**: 30 seconds - Balance between data resolution and CPU usage
- **File Flush Interval**: 10 minutes - Minimize flash wear while ensuring data persistence
- **Memory Efficiency**: PSRAM allocation prevents heap fragmentation
- **CPU Yielding**: Regular `vTaskDelay()` calls prevent watchdog timeouts

### 8. Integration with SNRv9 System

#### Main Application Integration

**Integration in main.c**:
```c
// In main.c
esp_err_t initialize_trending_system(void) {
    ESP_LOGI(TAG, "Initializing Trending Manager...");
    
    esp_err_t err = trending_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Trending Manager: %s", esp_err_to_name(err));
        return err;
    }
    
    // Start trending system
    err = trending_manager_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Trending Manager: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Trending Manager initialized and started successfully");
    return ESP_OK;
}
```

#### IO System Integration

**Data Collection Integration**:
```c
// In io_manager.c - Called when AI values are updated
static void notify_trending_system_ai_update(const char *point_id, float conditioned_value) {
    esp_err_t err = trending_manager_collect_ai_sample(point_id, conditioned_value);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to collect trending sample for %s: %s", 
                 point_id, esp_err_to_name(err));
    }
}

// In io_manager.c - Called when BI/BO states change
static void notify_trending_system_state_change(const char *point_id, bool state) {
    esp_err_t err = trending_manager_collect_state_change(point_id, state);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to collect trending state change for %s: %s", 
                 point_id, esp_err_to_name(err));
    }
}
```

#### Web Server Integration

**Endpoint Registration**:
```c
// In web_server_manager.c
esp_err_t register_trending_endpoints(httpd_handle_t server) {
    httpd_uri_t trending_config_uri = {
        .uri = "/api/trending/config",
        .method = HTTP_GET,
        .handler = trending_config_get_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t trending_data_uri = {
        .uri = "/api/trending/data/*",
        .method = HTTP_GET,
        .handler = trending_data_get_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t trending_status_uri = {
        .uri = "/api/trending/status",
        .method = HTTP_GET,
        .handler = trending_status_get_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t trending_metadata_uri = {
        .uri = "/api/trending/metadata",
        .method = HTTP_GET,
        .handler = trending_metadata_get_handler,
        .user_ctx = NULL
    };
    
    esp_err_t err = httpd_register_uri_handler(server, &trending_config_uri);
    if (err != ESP_OK) return err;
    
    err = httpd_register_uri_handler(server, &trending_data_uri);
    if (err != ESP_OK) return err;
    
    err = httpd_register_uri_handler(server, &trending_status_uri);
    if (err != ESP_OK) return err;
    
    err = httpd_register_uri_handler(server, &trending_metadata_uri);
    if (err != ESP_OK) return err;
    
    ESP_LOGI(TAG, "Trending endpoints registered successfully");
    return ESP_OK;
}
```

## Summary

This ESP-IDF Trending System provides:

### **Core Features**
- **Real-time Data Collection**: AI samples and BI/BO state changes with configurable intervals
- **PSRAM Buffer Management**: Efficient memory usage with 3MB PSRAM budget
- **File Persistence**: LittleFS storage with automatic rotation and flushing
- **Web Interface**: RESTful API for configuration and data retrieval
- **Time Synchronization**: NTP-dependent data collection for accurate timestamps

### **ESP-IDF Optimizations**
- **FreeRTOS Integration**: Three dedicated tasks with appropriate priorities
- **Memory Efficiency**: PSRAM allocation prevents heap fragmentation
- **Thread Safety**: Comprehensive mutex protection with timeouts
- **Error Handling**: Consistent ESP-IDF error codes and logging
- **Performance**: Optimized intervals and CPU yielding for system stability

### **Production Features**
- **Watchdog Protection**: Regular CPU yielding prevents timeouts
- **Graceful Degradation**: System continues operation on component failures
- **Comprehensive Logging**: ESP-IDF logging integration with detailed diagnostics
- **Configuration Persistence**: Automatic save/load of trending configurations
- **Memory Monitoring**: Dedicated heap monitor task for system health

### **System Limits**
- **AI Points**: 8 points maximum (200KB PSRAM each)
- **BI/BO Points**: 16 points maximum (60KB PSRAM each)
- **Total PSRAM Budget**: 3MB allocated for trending system
- **File Sizes**: 100KB per AI file, 30KB per BI/BO file
- **Sample Intervals**: 10 seconds to 1 hour for AI points

The system integrates seamlessly with the SNRv9 Time Management, Scheduling, Alarming, and IO systems to provide a complete irrigation control solution with comprehensive data logging and visualization capabilities optimized for the ESP32 platform.
