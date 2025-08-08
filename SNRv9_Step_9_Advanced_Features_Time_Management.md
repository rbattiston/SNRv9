# SNRv9 Time Management System - ESP32 ESP-IDF Architecture

## Overview

The SNRv9 Time Management system is a sophisticated, multi-layered system designed specifically for ESP32 ESP-IDF framework that handles time synchronization, persistence, and web-based configuration. This system provides reliable time services for the entire SNRv9 irrigation control system, with particular emphasis on schedule execution accuracy, user-friendly configuration, and robust operation in embedded environments.

## 1. ESP-IDF Core Backend Components

### TimeManager Component (`components/core/time_manager/`)

The heart of the time system, implemented as an ESP-IDF component providing thread-safe time management with these key capabilities:

#### Core Features
- **ESP-IDF SNTP Integration**: Native ESP-IDF `esp_sntp` component for NTP synchronization
- **Manual Time Setting**: Ability to set time manually when NTP is unavailable
- **Timezone Support**: Full POSIX timezone string support with automatic DST handling via `setenv()` and `tzset()`
- **Thread Safety**: All operations protected by FreeRTOS mutex (`SemaphoreHandle_t`)
- **NVS Persistence**: All settings stored in ESP32 NVS using `nvs_flash` component
- **WiFi Event Integration**: Automatic sync attempts on WiFi connection events

#### Header File: `include/time_manager.h`
```c
#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <time.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "esp_err.h"

typedef enum {
    TIME_STATUS_NOT_CONFIGURED = 0,
    TIME_STATUS_MANUAL_SET = 1,
    TIME_STATUS_NTP_SYNCING = 2,
    TIME_STATUS_NTP_SYNCED = 3,
    TIME_STATUS_NTP_FAILED = 4
} time_status_t;

typedef struct {
    time_t current_time;
    char formatted_time[32];
    time_status_t status;
    char ntp_server[64];
    char timezone[64];
    bool manual_time_set;
    time_t last_ntp_sync;
    char status_string[32];
} time_status_info_t;

// Core API
esp_err_t time_manager_init(void);
esp_err_t time_manager_deinit(void);
void time_manager_task(void *pvParameters);

// Time Operations
time_t time_manager_now(void);
esp_err_t time_manager_get_status(time_status_info_t *status);
esp_err_t time_manager_set_manual_time(struct tm *time_info);

// Configuration
esp_err_t time_manager_set_ntp_server(const char *server);
esp_err_t time_manager_set_timezone(const char *timezone);
esp_err_t time_manager_sync_ntp(void);

// Utility Functions
esp_err_t time_manager_format_time(time_t timestamp, char *buffer, size_t buffer_size);
bool time_manager_is_configured(void);

#endif // TIME_MANAGER_H
```

#### Key Implementation Features

**ESP-IDF SNTP Integration**:
```c
#include "esp_sntp.h"
#include "esp_netif.h"

static void sntp_sync_notification_cb(struct timeval *tv) {
    // Update internal sync status
    xSemaphoreTake(time_mutex, portMAX_DELAY);
    last_ntp_sync_time = tv->tv_sec;
    current_status = TIME_STATUS_NTP_SYNCED;
    xSemaphoreGive(time_mutex);
    
    ESP_LOGI(TAG, "NTP sync completed: %s", ctime(&tv->tv_sec));
}

esp_err_t time_manager_sync_ntp(void) {
    if (esp_netif_get_nr_of_ifs() == 0) {
        ESP_LOGW(TAG, "No network interface available for NTP sync");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(sntp_sync_notification_cb);
    
    current_status = TIME_STATUS_NTP_SYNCING;
    esp_sntp_init();
    
    return ESP_OK;
}
```

**NVS Configuration Persistence**:
```c
#include "nvs_flash.h"
#include "nvs.h"

#define NVS_NAMESPACE "time_config"

static esp_err_t save_config_to_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;
    
    // Save configuration
    nvs_set_str(nvs_handle, "ntp_server", ntp_server);
    nvs_set_str(nvs_handle, "timezone", timezone_str);
    nvs_set_u8(nvs_handle, "manual_set", manual_time_set ? 1 : 0);
    nvs_set_u8(nvs_handle, "configured", 1);
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err;
}

static esp_err_t load_config_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        // Set defaults
        strcpy(ntp_server, "pool.ntp.org");
        strcpy(timezone_str, "UTC0");
        return ESP_OK;
    }
    
    size_t required_size;
    
    // Load NTP server
    required_size = sizeof(ntp_server);
    nvs_get_str(nvs_handle, "ntp_server", ntp_server, &required_size);
    
    // Load timezone
    required_size = sizeof(timezone_str);
    nvs_get_str(nvs_handle, "timezone", timezone_str, &required_size);
    
    // Load flags
    uint8_t temp;
    if (nvs_get_u8(nvs_handle, "manual_set", &temp) == ESP_OK) {
        manual_time_set = (temp == 1);
    }
    
    nvs_close(nvs_handle);
    return ESP_OK;
}
```

**FreeRTOS Task Integration**:
```c
#define TIME_MANAGER_TASK_STACK_SIZE 3072
#define TIME_MANAGER_TASK_PRIORITY 2

static TaskHandle_t time_manager_task_handle = NULL;
static SemaphoreHandle_t time_mutex = NULL;

void time_manager_task(void *pvParameters) {
    TickType_t last_sync_attempt = 0;
    const TickType_t sync_interval = pdMS_TO_TICKS(5 * 60 * 1000); // 5 minutes
    
    while (1) {
        TickType_t current_tick = xTaskGetTickCount();
        
        // Periodic NTP sync if configured and connected
        if (!manual_time_set && 
            (current_tick - last_sync_attempt) >= sync_interval) {
            
            if (wifi_is_connected()) {
                ESP_LOGI(TAG, "Attempting periodic NTP sync");
                time_manager_sync_ntp();
                last_sync_attempt = current_tick;
            }
        }
        
        // Update status based on SNTP state
        xSemaphoreTake(time_mutex, portMAX_DELAY);
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            if (current_status == TIME_STATUS_NTP_SYNCING) {
                current_status = TIME_STATUS_NTP_SYNCED;
            }
        }
        xSemaphoreGive(time_mutex);
        
        // Yield CPU to prevent watchdog timeout
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 second
    }
}

esp_err_t time_manager_init(void) {
    // Create mutex
    time_mutex = xSemaphoreCreateMutex();
    if (time_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // Load configuration
    esp_err_t err = load_config_from_nvs();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load config from NVS: %s", esp_err_to_name(err));
    }
    
    // Apply timezone
    setenv("TZ", timezone_str, 1);
    tzset();
    
    // Create task
    BaseType_t result = xTaskCreate(
        time_manager_task,
        "time_manager",
        TIME_MANAGER_TASK_STACK_SIZE,
        NULL,
        TIME_MANAGER_TASK_PRIORITY,
        &time_manager_task_handle
    );
    
    if (result != pdPASS) {
        vSemaphoreDelete(time_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Time Manager initialized successfully");
    return ESP_OK;
}
```

#### Component Configuration: `CMakeLists.txt`
```cmake
idf_component_register(
    SRCS "time_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        "esp_netif" 
        "esp_wifi"
        "nvs_flash"
        "lwip"
        "core"  # For existing monitoring integration
    PRIV_REQUIRES
        "esp_timer"
        "esp_system"
)
```

### TimeController Component (`components/web/time_controller/`)

Web API controller providing REST endpoints for time management using ESP-IDF HTTP server:

#### ESP-IDF HTTP Server Integration
```c
#include "esp_http_server.h"
#include "cJSON.h"
#include "time_manager.h"

// GET /api/time/status
static esp_err_t time_status_handler(httpd_req_t *req) {
    time_status_info_t status;
    esp_err_t err = time_manager_get_status(&status);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get time status");
        return ESP_FAIL;
    }
    
    // Create JSON response
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "currentTime", status.current_time);
    cJSON_AddStringToObject(json, "formattedTime", status.formatted_time);
    cJSON_AddStringToObject(json, "status", status.status_string);
    cJSON_AddBoolToObject(json, "ntpSynced", status.status == TIME_STATUS_NTP_SYNCED);
    cJSON_AddStringToObject(json, "ntpServer", status.ntp_server);
    cJSON_AddStringToObject(json, "timezone", status.timezone);
    cJSON_AddBoolToObject(json, "manualTimeSet", status.manual_time_set);
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    free(json_string);
    return ESP_OK;
}

// POST /api/time/manual
static esp_err_t time_manual_set_handler(httpd_req_t *req) {
    // Authentication check
    if (!auth_check_session(req, AUTH_ROLE_MANAGER)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
        return ESP_FAIL;
    }
    
    // Parse JSON body
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // Extract time components
    struct tm time_info = {0};
    time_info.tm_year = cJSON_GetObjectItem(json, "year")->valueint - 1900;
    time_info.tm_mon = cJSON_GetObjectItem(json, "month")->valueint - 1;
    time_info.tm_mday = cJSON_GetObjectItem(json, "day")->valueint;
    time_info.tm_hour = cJSON_GetObjectItem(json, "hour")->valueint;
    time_info.tm_min = cJSON_GetObjectItem(json, "minute")->valueint;
    time_info.tm_sec = cJSON_GetObjectItem(json, "second")->valueint;
    
    cJSON_Delete(json);
    
    // Set time
    esp_err_t err = time_manager_set_manual_time(&time_info);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set time");
        return ESP_FAIL;
    }
    
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// POST /api/time/ntp/config
static esp_err_t time_ntp_config_handler(httpd_req_t *req) {
    // Authentication check
    if (!auth_check_session(req, AUTH_ROLE_MANAGER)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
        return ESP_FAIL;
    }
    
    // Parse JSON body for NTP server and timezone
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // Extract configuration
    cJSON *ntp_server_item = cJSON_GetObjectItem(json, "ntpServer");
    cJSON *timezone_item = cJSON_GetObjectItem(json, "timezone");
    
    if (!cJSON_IsString(ntp_server_item) || !cJSON_IsString(timezone_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing required fields");
        return ESP_FAIL;
    }
    
    // Set configuration
    esp_err_t err1 = time_manager_set_ntp_server(ntp_server_item->valuestring);
    esp_err_t err2 = time_manager_set_timezone(timezone_item->valuestring);
    
    cJSON_Delete(json);
    
    if (err1 != ESP_OK || err2 != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set configuration");
        return ESP_FAIL;
    }
    
    // Trigger sync
    time_manager_sync_ntp();
    
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

#### API Endpoints
- `GET /api/time/status`: Complete time status (current time, sync status, etc.)
- `GET /api/time/configured`: Simple boolean check if time has been configured
- `POST /api/time/ntp/config`: Set NTP server and timezone
- `POST /api/time/ntp/sync`: Trigger manual NTP synchronization
- `POST /api/time/manual`: Set time manually
- `GET /api/time/timezones`: Get list of common timezone configurations

#### Security Integration
- All endpoints require authentication via session cookies
- Role-based access control (VIEWER for status, MANAGER for configuration changes)
- Comprehensive permission checking with detailed debug logging

#### Timezone Support
Built-in support for common timezones including US zones, European zones, and Asia-Pacific:
```c
static const timezone_info_t common_timezones[] = {
    {"US Eastern", "EST5EDT,M3.2.0/2,M11.1.0"},
    {"US Central", "CST6CDT,M3.2.0/2,M11.1.0"},
    {"US Mountain", "MST7MDT,M3.2.0/2,M11.1.0"},
    {"US Pacific", "PST8PDT,M3.2.0/2,M11.1.0"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Asia/Tokyo", "JST-9"},
    {"UTC", "UTC0"}
};
```

## 2. Frontend Components

### Time Configuration Page (`data/features/time_config/`)

#### Main Interface (`time_config_main.js`)
- **Dual Mode Support**: NTP synchronization vs Manual time setting
- **Real-time Updates**: Live time display with 1-second refresh
- **Configuration Forms**: Separate panels for NTP and manual configuration
- **Status Indicators**: Visual badges showing sync status
- **Preset Buttons**: Quick-access buttons for common configurations

#### API Integration (`time_config_api.js`)
Clean JavaScript API wrapper for all time endpoints:
```javascript
class TimeConfigAPI {
    static async getTimeStatus() {
        const response = await fetch('/api/time/status');
        return await response.json();
    }
    
    static async setNtpConfig(ntpServer, timezone) {
        const response = await fetch('/api/time/ntp/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ntpServer, timezone })
        });
        return await response.json();
    }
    
    static async triggerNtpSync() {
        const response = await fetch('/api/time/ntp/sync', { method: 'POST' });
        return await response.json();
    }
    
    static async setManualTime(year, month, day, hour, minute, second) {
        const response = await fetch('/api/time/manual', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ year, month, day, hour, minute, second })
        });
        return await response.json();
    }
    
    static async getTimezones() {
        const response = await fetch('/api/time/timezones');
        return await response.json();
    }
}
```

### Navigation Widget (`time_nav_widget.js`)

Sidebar time display widget appearing on all pages:

#### Features
- **Live Time Display**: Current date and time with 1-second updates
- **Status Indicator**: Color-coded dot showing sync status
- **Clickable Navigation**: Links to time configuration page
- **Error Handling**: Graceful fallback for network issues

#### Status Indicators
- Green (Synced): NTP synchronized
- Blue (Manual): Manually set time
- Yellow (Syncing): NTP sync in progress
- Red (Error): WiFi disconnected or sync failed

## 3. ESP-IDF System Integration Points

### Main Application Integration (`src/main.c`)

Time system initialization in the main ESP-IDF application:

```c
#include "time_manager.h"

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize WiFi and other components...
    
    // Initialize Time Manager
    ESP_ERROR_CHECK(time_manager_init());
    
    // Initialize web server and register time endpoints
    // ... rest of initialization
}
```

### WiFi Event Integration

ESP-IDF WiFi event integration for automatic NTP sync:

```c
#include "esp_event.h"
#include "esp_wifi.h"

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected - attempting NTP sync");
        // WiFi connected - attempt NTP sync if configured
        if (!time_manager_is_manual_mode()) {
            time_manager_sync_ntp();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected - NTP sync unavailable");
        // Update time status to reflect network unavailability
    }
}

// Register event handler
ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
```

### Schedule System Integration

The time system is deeply integrated with the scheduling system:

#### ScheduleExecutor Integration
- Uses `time_manager_now()` for all schedule timing decisions
- Provides consistent time source across all schedule operations

#### Event Logging Integration
- All time-related events logged via `EventLogger`
- Configuration changes tracked with user attribution
- NTP sync events and failures logged for diagnostics

### Web Server Integration

Time endpoints registered in `WebServerManager`:
```c
#include "time_controller.h"

void register_time_endpoints(httpd_handle_t server) {
    httpd_uri_t time_status_uri = {
        .uri = "/api/time/status",
        .method = HTTP_GET,
        .handler = time_status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &time_status_uri);
    
    httpd_uri_t time_manual_uri = {
        .uri = "/api/time/manual",
        .method = HTTP_POST,
        .handler = time_manual_set_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &time_manual_uri);
    
    httpd_uri_t time_ntp_config_uri = {
        .uri = "/api/time/ntp/config",
        .method = HTTP_POST,
        .handler = time_ntp_config_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &time_ntp_config_uri);
    
    // Register other time endpoints...
}
```

## 4. Advanced ESP-IDF Features

### Timezone Handling

Full POSIX timezone support with automatic DST transitions using ESP-IDF's built-in support:

#### Features
- Standard timezone strings (e.g., "EST5EDT,M3.2.0/2,M11.1.0")
- Automatic daylight saving time transitions via `setenv()` and `tzset()`
- Custom timezone support via POSIX strings
- Immediate application of timezone changes

```c
esp_err_t time_manager_set_timezone(const char *timezone) {
    if (timezone == NULL || strlen(timezone) >= sizeof(timezone_str)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(time_mutex, portMAX_DELAY);
    
    // Apply timezone immediately
    setenv("TZ", timezone, 1);
    tzset();
    
    // Store in configuration
    strcpy(timezone_str, timezone);
    
    xSemaphoreGive(time_mutex);
    
    // Persist to NVS
    return save_config_to_nvs();
}
```

### Persistence and Recovery

Robust state management across power cycles using ESP-IDF NVS:

#### NVS Storage
- All configuration persisted to ESP32 Non-Volatile Storage
- Automatic loading on boot with fallback defaults
- Separate flags for manual vs NTP mode
- Time configuration status tracking

#### Boot Sequence
1. Initialize NVS flash
2. Load persisted settings from NVS
3. Apply timezone configuration via `setenv()` and `tzset()`
4. Initialize with safe defaults
5. Attempt initial NTP sync if WiFi available

### Power Management Integration

ESP-IDF power management configuration for time accuracy:

```c
#include "esp_pm.h"

// Configure power management to maintain time accuracy
esp_pm_config_esp32_t pm_config = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 80,
    .light_sleep_enable = false  // Disable for time accuracy
};
esp_pm_configure(&pm_config);
```

## 5. Error Handling and Diagnostics

### Network Resilience
- Automatic retry logic for failed NTP requests (5-minute intervals)
- WiFi disconnection detection via ESP-IDF event system
- Timeout handling for NTP requests (10-second timeout via SNTP configuration)
- Graceful degradation when network unavailable

### Debug and Monitoring

Comprehensive debug output controlled by `DEBUG_TIME_MANAGER` in `debug_config.h`:

```c
// In debug_config.h
#define DEBUG_TIME_MANAGER 1
#define DEBUG_TIME_VERBOSE 0

#if DEBUG_TIME_MANAGER
    #define TIME_LOGI(format, ...) ESP_LOGI(TAG, format, ##__VA_ARGS__)
    #define TIME_LOGW(format, ...) ESP_LOGW(TAG, format, ##__VA_ARGS__)
    #define TIME_LOGE(format, ...) ESP_LOGE(TAG, format, ##__VA_ARGS__)
#else
    #define TIME_LOGI(format, ...)
    #define TIME_LOGW(format, ...)
    #define TIME_LOGE(format, ...)
#endif
```

Debug output includes:
- NTP sync attempts and results
- Configuration changes with before/after values
- Time calculation details
- Mutex operations and thread safety verification
- WiFi event handling

### Status Reporting

Rich status information available via API:
```json
{
  "currentTime": 1691234567,
  "formattedTime": "2023-08-05 14:22:47",
  "ntpSynced": true,
  "ntpStatus": "Synced",
  "lastNtpSyncMillis": 1234567,
  "ntpServer": "pool.ntp.org",
  "timezone": "EST5EDT,M3.2.0/2,M11.1.0",
  "manualTimeSet": false
}
```

### Integration with Existing Monitoring

```c
// Add to existing monitoring reports
void system_health_report(void) {
    time_status_info_t time_status;
    if (time_manager_get_status(&time_status) == ESP_OK) {
        ESP_LOGI(TAG, "Time Status: %s, Current: %s", 
                 time_status.status_string, time_status.formatted_time);
    }
    
    // ... existing monitoring code
}
```

## 6. Security and Access Control

### Authentication Integration
- Session-based authentication for all configuration endpoints
- Role-based access control (VIEWER vs MANAGER permissions)
- Secure cookie handling with proper parsing
- Comprehensive permission checking with audit logging

### Configuration Protection
- Validation of all time inputs (year 2000-2100, valid date/time ranges)
- NTP server validation and error handling
- Timezone string validation
- Input sanitization for all web endpoints

## 7. User Interface Features

### Time Configuration Page
- **Mode Selection**: Radio buttons for NTP vs Manual time setting
- **NTP Configuration**: Dropdown for common NTP servers with custom input option
- **Timezone Selection**: Dropdown for common timezones with custom POSIX string support
- **Manual Time Setting**: Date and time pickers for manual configuration
- **Status Display**: Real-time sync status and last sync information
- **Manual Sync**: Button to trigger immediate NTP synchronization

### Navigation Integration
- **Persistent Time Display**: Current time and date visible on all pages
- **Status Indicators**: Visual feedback for sync status in navigation sidebar
- **Quick Access**: Click time display to navigate to configuration page
- **Error States**: Clear indication when time sync issues occur

## 8. ESP-IDF Technical Implementation Details

### Thread Safety
- All TimeManager operations protected by FreeRTOS mutex (`SemaphoreHandle_t`)
- Safe concurrent access from multiple tasks
- Proper resource cleanup and error handling
- 100ms mutex timeouts following project patterns

### Memory Management
- **Stack Size**: 3072 bytes for time manager task (following ESP32 patterns)
- **NVS Efficiency**: Minimal writes, only on configuration changes
- **JSON Handling**: Use cJSON for lightweight JSON processing
- **String Buffers**: Fixed-size buffers to prevent fragmentation

### Performance Considerations
- **Periodic Sync**: 5-minute intervals to balance accuracy and performance
- **Event-Driven**: WiFi connection triggers immediate sync attempt
- **Mutex Timeouts**: 100ms timeouts following project patterns
- **Task Yielding**: Regular `vTaskDelay()` calls to prevent watchdog timeouts
- **CPU Efficiency**: Lightweight time calculations for frequent access
- **Network Resilience**: 10-second SNTP timeout with automatic retry

### Component Dependencies

The time manager component requires these ESP-IDF components:
- `esp_netif`: Network interface management
- `esp_wifi`: WiFi event integration
- `nvs_flash`: Configuration persistence
- `lwip`: Network stack for SNTP
- `json`: JSON parsing for web API
- `esp_http_server`: Web endpoint registration

This ESP-IDF time management system represents a production-ready, enterprise-grade solution providing reliable time services for the entire SNRv9 irrigation control system. It leverages ESP-IDF's native components for optimal integration, performance, and reliability while maintaining all the sophisticated features required for schedule execution accuracy, user-friendly configuration, and robust operation in embedded environments.
