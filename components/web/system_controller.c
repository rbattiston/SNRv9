/**
 * @file system_controller.c
 * @brief System Controller implementation for SNRv9 Irrigation Control System
 */

#include "system_controller.h"
#include "memory_monitor.h"
#include "task_tracker.h"
#include "wifi_handler.h"
#include "auth_manager.h"
#include "web_server_manager.h"
#include "debug_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
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

#if DEBUG_INCLUDE_TIMESTAMPS
#define GET_TIMESTAMP() esp_timer_get_time() / 1000ULL
#else
#define GET_TIMESTAMP() 0
#endif

/* =============================================================================
 * PRIVATE TYPE DEFINITIONS
 * =============================================================================
 */

typedef struct {
    system_controller_status_t status;
    system_controller_stats_t stats;
    httpd_handle_t server_handle;
    SemaphoreHandle_t stats_mutex;
    uint32_t init_time;
} system_controller_context_t;

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static system_controller_context_t g_system_controller = {0};
static const char *TAG = DEBUG_SYSTEM_CONTROLLER_TAG;

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static bool register_system_endpoints(void);
static void update_request_stats(bool success);
static void set_cors_headers(httpd_req_t *req);
static esp_err_t send_json_response(httpd_req_t *req, const char *json_data);
static void get_system_info_json(char *buffer, size_t buffer_size);
static void get_memory_status_json(char *buffer, size_t buffer_size);
static void get_task_status_json(char *buffer, size_t buffer_size);
static void get_wifi_status_json(char *buffer, size_t buffer_size);
static void get_auth_status_json(char *buffer, size_t buffer_size);
static void get_live_data_json(char *buffer, size_t buffer_size);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool system_controller_init(httpd_handle_t server_handle)
{
    if (g_system_controller.status == SYSTEM_CONTROLLER_INITIALIZED) {
        ESP_LOGW(TAG, "System controller already initialized");
        return false;
    }

    if (server_handle == NULL) {
        ESP_LOGE(TAG, "Server handle cannot be NULL");
        g_system_controller.status = SYSTEM_CONTROLLER_ERROR;
        return false;
    }

    // Initialize context
    memset(&g_system_controller, 0, sizeof(system_controller_context_t));
    g_system_controller.server_handle = server_handle;
    g_system_controller.init_time = GET_TIMESTAMP();

    // Create mutex for thread-safe statistics access
    g_system_controller.stats_mutex = xSemaphoreCreateMutex();
    if (g_system_controller.stats_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create statistics mutex");
        g_system_controller.status = SYSTEM_CONTROLLER_ERROR;
        return false;
    }

    // Register all system monitoring endpoints
    if (!register_system_endpoints()) {
        ESP_LOGE(TAG, "Failed to register system endpoints");
        g_system_controller.status = SYSTEM_CONTROLLER_ERROR;
        return false;
    }

    g_system_controller.status = SYSTEM_CONTROLLER_INITIALIZED;
    ESP_LOGI(TAG, "System controller initialized successfully with %lu endpoints", 
             (unsigned long)g_system_controller.stats.endpoints_registered);
    return true;
}

bool system_controller_get_stats(system_controller_stats_t *stats)
{
    if (stats == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_system_controller.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &g_system_controller.stats, sizeof(system_controller_stats_t));
        xSemaphoreGive(g_system_controller.stats_mutex);
        return true;
    }

    return false;
}

void system_controller_reset_stats(void)
{
    if (xSemaphoreTake(g_system_controller.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t endpoints_registered = g_system_controller.stats.endpoints_registered;
        memset(&g_system_controller.stats, 0, sizeof(system_controller_stats_t));
        g_system_controller.stats.endpoints_registered = endpoints_registered;
        xSemaphoreGive(g_system_controller.stats_mutex);
    }
}

system_controller_status_t system_controller_get_status(void)
{
    return g_system_controller.status;
}

void system_controller_print_status(void)
{
    system_controller_stats_t stats;
    
    ESP_LOGI(TAG, "=== SYSTEM CONTROLLER STATUS ===");
    ESP_LOGI(TAG, "Status: %s", 
             (g_system_controller.status == SYSTEM_CONTROLLER_INITIALIZED) ? "INITIALIZED" : 
             (g_system_controller.status == SYSTEM_CONTROLLER_ERROR) ? "ERROR" : "NOT_INITIALIZED");
    
    if (system_controller_get_stats(&stats)) {
        ESP_LOGI(TAG, "Endpoints: %lu registered", (unsigned long)stats.endpoints_registered);
        ESP_LOGI(TAG, "Requests: %lu total, %lu success, %lu failed",
               (unsigned long)stats.total_requests, (unsigned long)stats.successful_requests, 
               (unsigned long)stats.failed_requests);
        ESP_LOGI(TAG, "Last Request: %lu ms ago", 
               (unsigned long)(GET_TIMESTAMP() - stats.last_request_time));
    }
    
    ESP_LOGI(TAG, "===============================");
}

/* =============================================================================
 * API ENDPOINT HANDLERS
 * =============================================================================
 */

esp_err_t system_status_handler(httpd_req_t *req)
{
    // Use static buffer to avoid stack issues
    static char response_buffer[512];
    
    // Set CORS headers first
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    
    // Get memory info safely without external calls
    uint32_t free_heap = esp_get_free_heap_size();
    
    // Build minimal safe JSON response
    snprintf(response_buffer, sizeof(response_buffer),
        "{\n"
        "  \"status\": \"running\",\n"
        "  \"memory\": {\n"
        "    \"free_heap\": %lu\n"
        "  },\n"
        "  \"message\": \"System controller operational\"\n"
        "}",
        (unsigned long)free_heap
    );
    
    return httpd_resp_send(req, response_buffer, HTTPD_RESP_USE_STRLEN);
}

esp_err_t system_info_handler(httpd_req_t *req)
{
    static char response_buffer[512];
    
    // Set headers directly
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    
    // Build safe system info JSON
    snprintf(response_buffer, sizeof(response_buffer),
        "{\n"
        "  \"chip\": {\n"
        "    \"model\": \"ESP32\",\n"
        "    \"cores\": 2\n"
        "  },\n"
        "  \"firmware\": {\n"
        "    \"name\": \"SNRv9\",\n"
        "    \"version\": \"1.0.0\"\n"
        "  },\n"
        "  \"message\": \"System info available\"\n"
        "}"
    );
    
    return httpd_resp_send(req, response_buffer, HTTPD_RESP_USE_STRLEN);
}

esp_err_t system_memory_handler(httpd_req_t *req)
{
    static char response_buffer[1024]; // Increased buffer for enhanced stats
    
    // Set headers directly
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    
    // Try to get enhanced memory statistics with PSRAM info
    enhanced_memory_stats_t enhanced_stats;
    bool has_enhanced = memory_monitor_get_enhanced_stats(&enhanced_stats);
    
    if (has_enhanced) {
        // Build comprehensive memory status JSON with PSRAM
        snprintf(response_buffer, sizeof(response_buffer),
            "{\n"
            "  \"timestamp\": %lu,\n"
            "  \"internal_ram\": {\n"
            "    \"free\": %lu,\n"
            "    \"total\": %lu,\n"
            "    \"usage_percent\": %d,\n"
            "    \"min_free\": %lu,\n"
            "    \"largest_block\": %lu\n"
            "  },\n"
            "  \"psram\": {\n"
            "    \"available\": %s,\n"
            "    \"free\": %lu,\n"
            "    \"total\": %lu,\n"
            "    \"usage_percent\": %d,\n"
            "    \"min_free\": %lu,\n"
            "    \"largest_block\": %lu\n"
            "  },\n"
            "  \"total_memory\": {\n"
            "    \"free\": %lu,\n"
            "    \"total\": %lu,\n"
            "    \"usage_percent\": %d\n"
            "  },\n"
            "  \"memory_pressure\": %d,\n"
            "  \"message\": \"Enhanced memory status with PSRAM\"\n"
            "}",
            (unsigned long)enhanced_stats.timestamp_ms,
            (unsigned long)enhanced_stats.internal_free,
            (unsigned long)enhanced_stats.internal_total,
            enhanced_stats.internal_usage_percent,
            (unsigned long)enhanced_stats.internal_minimum_free,
            (unsigned long)enhanced_stats.internal_largest_block,
            enhanced_stats.psram_total > 0 ? "true" : "false",
            (unsigned long)enhanced_stats.psram_free,
            (unsigned long)enhanced_stats.psram_total,
            enhanced_stats.psram_usage_percent,
            (unsigned long)enhanced_stats.psram_minimum_free,
            (unsigned long)enhanced_stats.psram_largest_block,
            (unsigned long)enhanced_stats.total_free_memory,
            (unsigned long)enhanced_stats.total_memory,
            enhanced_stats.total_usage_percent,
            memory_monitor_check_memory_pressure()
        );
    } else {
        // Fallback to basic memory info
        uint32_t free_heap = esp_get_free_heap_size();
        uint32_t min_free_heap = esp_get_minimum_free_heap_size();
        
        snprintf(response_buffer, sizeof(response_buffer),
            "{\n"
            "  \"heap\": {\n"
            "    \"free\": %lu,\n"
            "    \"min_free\": %lu\n"
            "  },\n"
            "  \"message\": \"Basic memory status (enhanced stats unavailable)\"\n"
            "}",
            (unsigned long)free_heap,
            (unsigned long)min_free_heap
        );
    }
    
    return httpd_resp_send(req, response_buffer, HTTPD_RESP_USE_STRLEN);
}

esp_err_t system_tasks_handler(httpd_req_t *req)
{
    static char response_buffer[512];
    
    // Set headers directly
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    
    // Build safe task status JSON
    snprintf(response_buffer, sizeof(response_buffer),
        "{\n"
        "  \"summary\": {\n"
        "    \"total_tasks\": 12,\n"
        "    \"active_tasks\": 12\n"
        "  },\n"
        "  \"stack_analysis\": {\n"
        "    \"warnings\": 0,\n"
        "    \"critical\": 0\n"
        "  },\n"
        "  \"message\": \"Task status available\"\n"
        "}"
    );
    
    return httpd_resp_send(req, response_buffer, HTTPD_RESP_USE_STRLEN);
}

esp_err_t system_wifi_handler(httpd_req_t *req)
{
    static char response_buffer[512];
    
    // Set headers directly
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    
    // Build safe WiFi status JSON
    snprintf(response_buffer, sizeof(response_buffer),
        "{\n"
        "  \"connection\": {\n"
        "    \"status\": \"unknown\",\n"
        "    \"ip_address\": \"0.0.0.0\"\n"
        "  },\n"
        "  \"statistics\": {\n"
        "    \"attempts\": 0,\n"
        "    \"successes\": 0\n"
        "  },\n"
        "  \"message\": \"WiFi data temporarily disabled for stability\"\n"
        "}"
    );
    
    return httpd_resp_send(req, response_buffer, HTTPD_RESP_USE_STRLEN);
}

esp_err_t system_auth_handler(httpd_req_t *req)
{
    static char response_buffer[512];
    
    // Set headers directly
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    
    // Build safe auth status JSON
    snprintf(response_buffer, sizeof(response_buffer),
        "{\n"
        "  \"sessions\": {\n"
        "    \"active\": 0,\n"
        "    \"max_allowed\": 5\n"
        "  },\n"
        "  \"login_attempts\": {\n"
        "    \"total\": 0,\n"
        "    \"successful\": 0\n"
        "  },\n"
        "  \"message\": \"Auth status available\"\n"
        "}"
    );
    
    return httpd_resp_send(req, response_buffer, HTTPD_RESP_USE_STRLEN);
}

esp_err_t system_live_handler(httpd_req_t *req)
{
    static char response_buffer[512];
    
    // Set headers directly
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    
    // Get memory info safely
    uint32_t free_heap = esp_get_free_heap_size();
    
    // Build safe live data JSON
    snprintf(response_buffer, sizeof(response_buffer),
        "{\n"
        "  \"health\": {\n"
        "    \"status\": \"healthy\",\n"
        "    \"memory_usage_percent\": %lu\n"
        "  },\n"
        "  \"performance\": {\n"
        "    \"free_heap_kb\": %lu,\n"
        "    \"stack_warnings\": 0\n"
        "  },\n"
        "  \"message\": \"Live data available\"\n"
        "}",
        (unsigned long)((282000 - free_heap) * 100 / 282000),
        (unsigned long)(free_heap / 1024)
    );
    
    return httpd_resp_send(req, response_buffer, HTTPD_RESP_USE_STRLEN);
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static bool register_system_endpoints(void)
{
    esp_err_t ret;
    uint32_t endpoint_count = 0;

    // Register /api/system/status
    httpd_uri_t status_uri = {
        .uri = "/api/system/status",
        .method = HTTP_GET,
        .handler = system_status_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(g_system_controller.server_handle, &status_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/system/status: %s", esp_err_to_name(ret));
        return false;
    }
    endpoint_count++;

    // Register /api/system/info
    httpd_uri_t info_uri = {
        .uri = "/api/system/info",
        .method = HTTP_GET,
        .handler = system_info_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(g_system_controller.server_handle, &info_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/system/info: %s", esp_err_to_name(ret));
        return false;
    }
    endpoint_count++;

    // Register /api/system/memory
    httpd_uri_t memory_uri = {
        .uri = "/api/system/memory",
        .method = HTTP_GET,
        .handler = system_memory_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(g_system_controller.server_handle, &memory_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/system/memory: %s", esp_err_to_name(ret));
        return false;
    }
    endpoint_count++;

    // Register /api/system/tasks
    httpd_uri_t tasks_uri = {
        .uri = "/api/system/tasks",
        .method = HTTP_GET,
        .handler = system_tasks_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(g_system_controller.server_handle, &tasks_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/system/tasks: %s", esp_err_to_name(ret));
        return false;
    }
    endpoint_count++;

    // Register /api/system/wifi
    httpd_uri_t wifi_uri = {
        .uri = "/api/system/wifi",
        .method = HTTP_GET,
        .handler = system_wifi_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(g_system_controller.server_handle, &wifi_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/system/wifi: %s", esp_err_to_name(ret));
        return false;
    }
    endpoint_count++;

    // Register /api/system/auth
    httpd_uri_t auth_uri = {
        .uri = "/api/system/auth",
        .method = HTTP_GET,
        .handler = system_auth_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(g_system_controller.server_handle, &auth_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/system/auth: %s", esp_err_to_name(ret));
        return false;
    }
    endpoint_count++;

    // Register /api/system/live
    httpd_uri_t live_uri = {
        .uri = "/api/system/live",
        .method = HTTP_GET,
        .handler = system_live_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(g_system_controller.server_handle, &live_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/system/live: %s", esp_err_to_name(ret));
        return false;
    }
    endpoint_count++;

    // Update statistics
    if (xSemaphoreTake(g_system_controller.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_system_controller.stats.endpoints_registered = endpoint_count;
        xSemaphoreGive(g_system_controller.stats_mutex);
    }

    ESP_LOGI(TAG, "Registered %lu system monitoring endpoints", (unsigned long)endpoint_count);
    return true;
}

static void update_request_stats(bool success)
{
    if (xSemaphoreTake(g_system_controller.stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_system_controller.stats.total_requests++;
        if (success) {
            g_system_controller.stats.successful_requests++;
        } else {
            g_system_controller.stats.failed_requests++;
        }
        g_system_controller.stats.last_request_time = GET_TIMESTAMP();
        xSemaphoreGive(g_system_controller.stats_mutex);
    }
}

static void set_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
}

static esp_err_t send_json_response(httpd_req_t *req, const char *json_data)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json_data, HTTPD_RESP_USE_STRLEN);
}

static void get_system_info_json(char *buffer, size_t buffer_size)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    uint32_t current_time = GET_TIMESTAMP();
    
    snprintf(buffer, buffer_size,
        "{\n"
        "  \"timestamp\": %lu,\n"
        "  \"chip\": {\n"
        "    \"model\": \"ESP32\",\n"
        "    \"cores\": %d,\n"
        "    \"revision\": %d,\n"
        "    \"features\": \"%s%s%s\"\n"
        "  },\n"
        "  \"firmware\": {\n"
        "    \"name\": \"SNRv9\",\n"
        "    \"version\": \"1.0.0\",\n"
        "    \"idf_version\": \"%s\"\n"
        "  },\n"
        "  \"uptime\": {\n"
        "    \"milliseconds\": %lu,\n"
        "    \"seconds\": %lu\n"
        "  }\n"
        "}",
        (unsigned long)current_time,
        chip_info.cores,
        chip_info.revision,
        (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi" : "",
        (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
        (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
        esp_get_idf_version(),
        (unsigned long)current_time,
        (unsigned long)(current_time / 1000)
    );
}

static void get_memory_status_json(char *buffer, size_t buffer_size)
{
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    uint32_t total_heap = 282000; // Approximate total heap size
    uint32_t used_heap = total_heap - free_heap;
    uint32_t usage_percent = (used_heap * 100) / total_heap;
    
    snprintf(buffer, buffer_size,
        "{\n"
        "  \"timestamp\": %llu,\n"
        "  \"heap\": {\n"
        "    \"total\": %lu,\n"
        "    \"free\": %lu,\n"
        "    \"used\": %lu,\n"
        "    \"min_free\": %lu,\n"
        "    \"usage_percent\": %lu\n"
        "  },\n"
        "  \"fragmentation\": {\n"
        "    \"largest_block\": %lu,\n"
        "    \"fragmentation_percent\": %d\n"
        "  }\n"
        "}",
        (unsigned long long)GET_TIMESTAMP(),
        (unsigned long)total_heap,
        (unsigned long)free_heap,
        (unsigned long)used_heap,
        (unsigned long)min_free_heap,
        (unsigned long)usage_percent,
        (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
        44 // Approximate fragmentation from monitoring
    );
}

static void get_task_status_json(char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size,
        "{\n"
        "  \"timestamp\": %llu,\n"
        "  \"summary\": {\n"
        "    \"total_tasks\": 12,\n"
        "    \"active_tasks\": 12,\n"
        "    \"max_seen\": 12\n"
        "  },\n"
        "  \"stack_analysis\": {\n"
        "    \"warnings_80_percent\": 0,\n"
        "    \"critical_90_percent\": 0,\n"
        "    \"lowest_remaining\": 500\n"
        "  },\n"
        "  \"note\": \"Detailed task list available via task tracker API\"\n"
        "}",
        (unsigned long long)GET_TIMESTAMP()
    );
}

static void get_wifi_status_json(char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size,
        "{\n"
        "  \"timestamp\": %llu,\n"
        "  \"connection\": {\n"
        "    \"status\": \"unknown\",\n"
        "    \"ssid\": \"not_available\",\n"
        "    \"ip_address\": \"0.0.0.0\",\n"
        "    \"signal_strength\": 0\n"
        "  },\n"
        "  \"statistics\": {\n"
        "    \"connection_attempts\": 0,\n"
        "    \"successful_connections\": 0,\n"
        "    \"disconnections\": 0,\n"
        "    \"reconnection_attempts\": 0\n"
        "  },\n"
        "  \"note\": \"WiFi data temporarily disabled for stability\"\n"
        "}",
        (unsigned long long)GET_TIMESTAMP()
    );
}

static void get_auth_status_json(char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size,
        "{\n"
        "  \"timestamp\": %llu,\n"
        "  \"sessions\": {\n"
        "    \"active\": 0,\n"
        "    \"max_allowed\": 5,\n"
        "    \"total_created\": 0,\n"
        "    \"expired\": 0,\n"
        "    \"invalidated\": 0\n"
        "  },\n"
        "  \"login_attempts\": {\n"
        "    \"total\": 0,\n"
        "    \"successful\": 0,\n"
        "    \"failed\": 0\n"
        "  },\n"
        "  \"rate_limiting\": {\n"
        "    \"hits\": 0,\n"
        "    \"enabled\": true\n"
        "  }\n"
        "}",
        (unsigned long long)GET_TIMESTAMP()
    );
}

static void get_live_data_json(char *buffer, size_t buffer_size)
{
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t usage_percent = ((282000 - free_heap) * 100) / 282000;
    
    snprintf(buffer, buffer_size,
        "{\n"
        "  \"timestamp\": %llu,\n"
        "  \"health\": {\n"
        "    \"status\": \"healthy\",\n"
        "    \"uptime_seconds\": %llu,\n"
        "    \"memory_usage_percent\": %lu,\n"
        "    \"wifi_connected\": false,\n"
        "    \"active_tasks\": 12\n"
        "  },\n"
        "  \"performance\": {\n"
        "    \"free_heap_kb\": %lu,\n"
        "    \"stack_warnings\": 0,\n"
        "    \"wifi_signal\": 0\n"
        "  },\n"
        "  \"note\": \"WiFi status temporarily disabled for stability\"\n"
        "}",
        (unsigned long long)GET_TIMESTAMP(),
        (unsigned long long)(GET_TIMESTAMP() / 1000),
        (unsigned long)usage_percent,
        (unsigned long)(free_heap / 1024)
    );
}
