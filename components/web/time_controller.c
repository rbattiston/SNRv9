/**
 * @file time_controller.c
 * @brief Time Management Controller implementation for SNRv9 Irrigation Control System
 */

#include "time_controller.h"
#include "time_manager.h"
#include "debug_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
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

#if DEBUG_TIME_CONTROLLER
#define TIME_CTRL_TAG DEBUG_TIME_CONTROLLER_TAG
#else
#define TIME_CTRL_TAG ""
#endif

#define MAX_REQUEST_BODY_SIZE 1024
#define MAX_RESPONSE_SIZE 2048

/* =============================================================================
 * PRIVATE TYPE DEFINITIONS
 * =============================================================================
 */

typedef struct {
    time_controller_status_t status;
    time_controller_stats_t stats;
    httpd_handle_t server_handle;
    SemaphoreHandle_t stats_mutex;
    uint32_t init_time;
} time_controller_context_t;

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static time_controller_context_t g_time_controller = {0};
static const char *TAG = "TIME_CTRL";

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static bool register_time_endpoints(void);
static void update_request_stats(bool success);
static void set_cors_headers(httpd_req_t *req);
static esp_err_t send_json_response(httpd_req_t *req, const char *json_data);
static esp_err_t send_error_response(httpd_req_t *req, int status_code, const char *message);
static char* read_request_body(httpd_req_t *req);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool time_controller_init(httpd_handle_t server_handle)
{
    if (g_time_controller.status == TIME_CONTROLLER_INITIALIZED) {
        ESP_LOGW(TAG, "Time controller already initialized");
        return false;
    }

    if (server_handle == NULL) {
        ESP_LOGE(TAG, "Server handle cannot be NULL");
        g_time_controller.status = TIME_CONTROLLER_ERROR;
        return false;
    }

    // Initialize context
    memset(&g_time_controller, 0, sizeof(time_controller_context_t));
    g_time_controller.server_handle = server_handle;
    g_time_controller.init_time = GET_TIMESTAMP();

    // Create mutex for thread-safe statistics access
    g_time_controller.stats_mutex = xSemaphoreCreateMutex();
    if (g_time_controller.stats_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create statistics mutex");
        g_time_controller.status = TIME_CONTROLLER_ERROR;
        return false;
    }

    // Register all time management endpoints
    if (!register_time_endpoints()) {
        ESP_LOGE(TAG, "Failed to register time endpoints");
        g_time_controller.status = TIME_CONTROLLER_ERROR;
        return false;
    }

    g_time_controller.status = TIME_CONTROLLER_INITIALIZED;
    ESP_LOGI(TAG, "Time controller initialized successfully with %lu endpoints", 
             (unsigned long)g_time_controller.stats.endpoints_registered);
    return true;
}

bool time_controller_get_stats(time_controller_stats_t *stats)
{
    if (stats == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_time_controller.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &g_time_controller.stats, sizeof(time_controller_stats_t));
        xSemaphoreGive(g_time_controller.stats_mutex);
        return true;
    }

    return false;
}

void time_controller_reset_stats(void)
{
    if (xSemaphoreTake(g_time_controller.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t endpoints_registered = g_time_controller.stats.endpoints_registered;
        memset(&g_time_controller.stats, 0, sizeof(time_controller_stats_t));
        g_time_controller.stats.endpoints_registered = endpoints_registered;
        xSemaphoreGive(g_time_controller.stats_mutex);
    }
}

time_controller_status_t time_controller_get_status(void)
{
    return g_time_controller.status;
}

void time_controller_print_status(void)
{
    time_controller_stats_t stats;
    
    ESP_LOGI(TAG, "=== TIME CONTROLLER STATUS ===");
    ESP_LOGI(TAG, "Status: %s", 
             (g_time_controller.status == TIME_CONTROLLER_INITIALIZED) ? "INITIALIZED" : 
             (g_time_controller.status == TIME_CONTROLLER_ERROR) ? "ERROR" : "NOT_INITIALIZED");
    
    if (time_controller_get_stats(&stats)) {
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

esp_err_t time_status_handler(httpd_req_t *req)
{
    static char response_buffer[MAX_RESPONSE_SIZE];
    
    ESP_LOGI(TAG, "GET /api/time/status");
    
    // Set CORS headers
    set_cors_headers(req);
    
    // Get time status from time manager
    time_status_t time_status;
    esp_err_t err = time_manager_get_status(&time_status);
    
    if (err != ESP_OK) {
        update_request_stats(false);
        return send_error_response(req, 500, "Failed to get time status");
    }
    
    // Get formatted time string
    char time_string[64];
    time_manager_get_formatted_time(time_string, sizeof(time_string), NULL);
    
    // Determine time reliability and status
    bool time_reliable = time_manager_is_time_reliable();
    const char *time_source = "none";
    const char *sync_status = "not_set";
    
    if (time_status.stats.current_source == TIME_SOURCE_NTP) {
        time_source = "ntp";
        sync_status = (time_status.status == TIME_MANAGER_NTP_SYNCED) ? "synced" : 
                     (time_status.status == TIME_MANAGER_NTP_SYNCING) ? "syncing" : "error";
    } else if (time_status.stats.current_source == TIME_SOURCE_MANUAL) {
        time_source = "manual";
        sync_status = "manual";
    }
    
    // Check if time is epoch (not set)
    bool is_epoch_time = (time_status.current_time < 946684800); // Year 2000
    
    // Build JSON response with enhanced status information
    snprintf(response_buffer, sizeof(response_buffer),
        "{\n"
        "  \"timestamp\": %llu,\n"
        "  \"current_time\": {\n"
        "    \"unix_timestamp\": %lld,\n"
        "    \"iso_string\": \"%s\",\n"
        "    \"local_time\": \"%s\",\n"
        "    \"utc_time\": \"%s\",\n"
        "    \"timezone\": \"%s\",\n"
        "    \"is_valid\": %s,\n"
        "    \"is_epoch\": %s\n"
        "  },\n"
        "  \"sync_status\": \"%s\",\n"
        "  \"ntp\": {\n"
        "    \"enabled\": %s,\n"
        "    \"available\": %s,\n"
        "    \"last_sync\": %lld,\n"
        "    \"sync_count\": %lu,\n"
        "    \"failed_count\": %lu\n"
        "  },\n"
        "  \"timezone\": {\n"
        "    \"name\": \"%s\",\n"
        "    \"offset_seconds\": %d,\n"
        "    \"dst_active\": %s\n"
        "  },\n"
        "  \"system\": {\n"
        "    \"time_source\": \"%s\",\n"
        "    \"time_reliable\": %s,\n"
        "    \"manager_status\": %d,\n"
        "    \"uptime_seconds\": %llu,\n"
        "    \"wifi_connected\": %s\n"
        "  },\n"
        "  \"statistics\": {\n"
        "    \"total_sync_attempts\": %lu,\n"
        "    \"successful_syncs\": %lu,\n"
        "    \"failed_syncs\": %lu,\n"
        "    \"manual_time_sets\": %lu\n"
        "  }\n"
        "}",
        (unsigned long long)GET_TIMESTAMP(),
        (long long)time_status.current_time,
        time_string,
        time_string, // local_time (same as iso_string for now)
        time_string, // utc_time (will be converted)
        time_status.timezone_info.name,
        time_reliable ? "true" : "false",
        is_epoch_time ? "true" : "false",
        sync_status,
        time_status.ntp_available ? "true" : "false",
        time_status.ntp_available ? "true" : "false",
        (long long)time_status.stats.last_sync_time,
        (unsigned long)time_status.stats.successful_syncs,
        (unsigned long)time_status.stats.failed_syncs,
        time_status.timezone_info.name,
        time_status.timezone_info.utc_offset_seconds,
        time_status.timezone_info.dst_active ? "true" : "false",
        time_source,
        time_reliable ? "true" : "false",
        (int)time_status.status,
        (unsigned long long)(GET_TIMESTAMP() / 1000),
        time_status.wifi_connected ? "true" : "false",
        (unsigned long)time_status.stats.total_sync_attempts,
        (unsigned long)time_status.stats.successful_syncs,
        (unsigned long)time_status.stats.failed_syncs,
        (unsigned long)time_status.stats.manual_time_sets
    );
    
    update_request_stats(true);
    return send_json_response(req, response_buffer);
}

esp_err_t ntp_config_handler(httpd_req_t *req)
{
#if DEBUG_TIME_CONTROLLER
    ESP_LOGI(TIME_CTRL_TAG, "POST /api/time/ntp/config - Starting NTP configuration");
#endif
    
    // Set CORS headers
    set_cors_headers(req);
    
    // Read request body
    char *body = read_request_body(req);
    if (body == NULL) {
#if DEBUG_TIME_CONTROLLER
        ESP_LOGE(TIME_CTRL_TAG, "Failed to read request body");
#endif
        update_request_stats(false);
        return send_error_response(req, 400, "Failed to read request body");
    }
    
#if DEBUG_TIME_CONTROLLER
    ESP_LOGI(TIME_CTRL_TAG, "Request body received: %s", body);
#endif
    
    // Parse JSON
    cJSON *json = cJSON_Parse(body);
    free(body);
    
    if (json == NULL) {
#if DEBUG_TIME_CONTROLLER
        ESP_LOGE(TIME_CTRL_TAG, "Failed to parse JSON");
#endif
        update_request_stats(false);
        return send_error_response(req, 400, "Invalid JSON format");
    }
    
    // Extract configuration parameters - support both old and new formats
    cJSON *primary_server_item = cJSON_GetObjectItem(json, "primary_server");
    cJSON *backup_server_item = cJSON_GetObjectItem(json, "backup_server");
    cJSON *sync_interval_item = cJSON_GetObjectItem(json, "sync_interval_minutes");
    cJSON *timezone_item = cJSON_GetObjectItem(json, "timezone");
    cJSON *enabled_item = cJSON_GetObjectItem(json, "enabled");
    
    // Legacy support for old format
    cJSON *server_item = cJSON_GetObjectItem(json, "server");
    
#if DEBUG_TIME_CONTROLLER
    ESP_LOGI(TIME_CTRL_TAG, "Parsed JSON - primary_server: %s, backup_server: %s, sync_interval: %d, timezone: %s, enabled: %s",
             (primary_server_item && cJSON_IsString(primary_server_item)) ? primary_server_item->valuestring : "null",
             (backup_server_item && cJSON_IsString(backup_server_item)) ? backup_server_item->valuestring : "null",
             (sync_interval_item && cJSON_IsNumber(sync_interval_item)) ? (int)sync_interval_item->valuedouble : -1,
             (timezone_item && cJSON_IsString(timezone_item)) ? timezone_item->valuestring : "null",
             (enabled_item && cJSON_IsBool(enabled_item)) ? (cJSON_IsTrue(enabled_item) ? "true" : "false") : "null");
#endif
    
    esp_err_t err = ESP_OK;
    bool config_changed = false;
    
    // Set primary NTP server if provided
    if (primary_server_item && cJSON_IsString(primary_server_item)) {
#if DEBUG_TIME_CONTROLLER
        ESP_LOGI(TIME_CTRL_TAG, "Setting primary NTP server: %s", primary_server_item->valuestring);
#endif
        char servers[2][64];
        strncpy(servers[0], primary_server_item->valuestring, sizeof(servers[0]) - 1);
        servers[0][sizeof(servers[0]) - 1] = '\0';
        
        int server_count = 1;
        
        // Add backup server if provided
        if (backup_server_item && cJSON_IsString(backup_server_item) && strlen(backup_server_item->valuestring) > 0) {
#if DEBUG_TIME_CONTROLLER
            ESP_LOGI(TIME_CTRL_TAG, "Setting backup NTP server: %s", backup_server_item->valuestring);
#endif
            strncpy(servers[1], backup_server_item->valuestring, sizeof(servers[1]) - 1);
            servers[1][sizeof(servers[1]) - 1] = '\0';
            server_count = 2;
        }
        
        err = time_manager_set_ntp_servers(servers, server_count);
        if (err != ESP_OK) {
#if DEBUG_TIME_CONTROLLER
            ESP_LOGE(TIME_CTRL_TAG, "Failed to set NTP servers: %s", esp_err_to_name(err));
#endif
            cJSON_Delete(json);
            update_request_stats(false);
            return send_error_response(req, 500, "Failed to set NTP servers");
        }
#if DEBUG_TIME_CONTROLLER
        ESP_LOGI(TIME_CTRL_TAG, "NTP servers set successfully (%d servers)", server_count);
#endif
        config_changed = true;
    }
    // Legacy support for single server field
    else if (server_item && cJSON_IsString(server_item)) {
#if DEBUG_TIME_CONTROLLER
        ESP_LOGI(TIME_CTRL_TAG, "Setting NTP server (legacy format): %s", server_item->valuestring);
#endif
        char servers[1][64];
        strncpy(servers[0], server_item->valuestring, sizeof(servers[0]) - 1);
        servers[0][sizeof(servers[0]) - 1] = '\0';
        err = time_manager_set_ntp_servers(servers, 1);
        if (err != ESP_OK) {
#if DEBUG_TIME_CONTROLLER
            ESP_LOGE(TIME_CTRL_TAG, "Failed to set NTP server: %s", esp_err_to_name(err));
#endif
            cJSON_Delete(json);
            update_request_stats(false);
            return send_error_response(req, 500, "Failed to set NTP server");
        }
#if DEBUG_TIME_CONTROLLER
        ESP_LOGI(TIME_CTRL_TAG, "NTP server set successfully (legacy)");
#endif
        config_changed = true;
    }
    
    // Set sync interval if provided
    if (sync_interval_item && cJSON_IsNumber(sync_interval_item)) {
        int interval_minutes = (int)sync_interval_item->valuedouble;
        if (interval_minutes > 0 && interval_minutes <= 1440) { // Max 24 hours
#if DEBUG_TIME_CONTROLLER
            ESP_LOGI(TIME_CTRL_TAG, "Setting sync interval: %d minutes", interval_minutes);
#endif
            err = time_manager_set_auto_sync(true, interval_minutes * 60); // Convert to seconds
            if (err != ESP_OK) {
#if DEBUG_TIME_CONTROLLER
                ESP_LOGE(TIME_CTRL_TAG, "Failed to set sync interval: %s", esp_err_to_name(err));
#endif
                cJSON_Delete(json);
                update_request_stats(false);
                return send_error_response(req, 500, "Failed to set sync interval");
            }
#if DEBUG_TIME_CONTROLLER
            ESP_LOGI(TIME_CTRL_TAG, "Sync interval set successfully");
#endif
            config_changed = true;
        } else {
#if DEBUG_TIME_CONTROLLER
            ESP_LOGW(TIME_CTRL_TAG, "Invalid sync interval: %d minutes (must be 1-1440)", interval_minutes);
#endif
        }
    }
    
    // Set timezone if provided
    if (timezone_item && cJSON_IsString(timezone_item)) {
#if DEBUG_TIME_CONTROLLER
        ESP_LOGI(TIME_CTRL_TAG, "Setting timezone: %s", timezone_item->valuestring);
#endif
        err = time_manager_set_timezone(timezone_item->valuestring);
        if (err != ESP_OK) {
#if DEBUG_TIME_CONTROLLER
            ESP_LOGE(TIME_CTRL_TAG, "Failed to set timezone: %s", esp_err_to_name(err));
#endif
            cJSON_Delete(json);
            update_request_stats(false);
            return send_error_response(req, 500, "Failed to set timezone");
        }
#if DEBUG_TIME_CONTROLLER
        ESP_LOGI(TIME_CTRL_TAG, "Timezone set successfully");
#endif
        config_changed = true;
    }
    
    // Enable/disable auto sync if provided
    if (enabled_item && cJSON_IsBool(enabled_item)) {
        bool enabled = cJSON_IsTrue(enabled_item);
#if DEBUG_TIME_CONTROLLER
        ESP_LOGI(TIME_CTRL_TAG, "Setting auto sync: %s", enabled ? "enabled" : "disabled");
#endif
        err = time_manager_set_auto_sync(enabled, 0); // Use current interval
        if (err != ESP_OK) {
#if DEBUG_TIME_CONTROLLER
            ESP_LOGE(TIME_CTRL_TAG, "Failed to configure auto sync: %s", esp_err_to_name(err));
#endif
            cJSON_Delete(json);
            update_request_stats(false);
            return send_error_response(req, 500, "Failed to configure auto sync");
        }
#if DEBUG_TIME_CONTROLLER
        ESP_LOGI(TIME_CTRL_TAG, "Auto sync configured successfully");
#endif
        config_changed = true;
    }
    
    cJSON_Delete(json);
    
    if (err != ESP_OK) {
#if DEBUG_TIME_CONTROLLER
        ESP_LOGE(TIME_CTRL_TAG, "Overall configuration failed: %s", esp_err_to_name(err));
#endif
        update_request_stats(false);
        return send_error_response(req, 500, "Failed to configure NTP");
    }
    
    if (!config_changed) {
#if DEBUG_TIME_CONTROLLER
        ESP_LOGW(TIME_CTRL_TAG, "No valid configuration parameters found in request");
#endif
        update_request_stats(false);
        return send_error_response(req, 400, "No valid configuration parameters provided");
    }
    
#if DEBUG_TIME_CONTROLLER
    ESP_LOGI(TIME_CTRL_TAG, "NTP configuration completed successfully - checking if sync should be triggered");
    
    // Check WiFi status and trigger sync if appropriate
    time_status_t time_status;
    esp_err_t status_err = time_manager_get_status(&time_status);
    if (status_err == ESP_OK) {
        ESP_LOGI(TIME_CTRL_TAG, "Current time status - WiFi: %s, Time reliable: %s, Manager status: %d",
                 time_status.wifi_connected ? "connected" : "disconnected",
                 time_manager_is_time_reliable() ? "yes" : "no",
                 (int)time_status.status);
        
        // Force immediate NTP sync if WiFi is connected
        if (time_status.wifi_connected) {
            ESP_LOGI(TIME_CTRL_TAG, "WiFi connected - forcing immediate NTP sync");
            esp_err_t sync_err = time_manager_force_ntp_sync(10000); // 10 second timeout
            if (sync_err == ESP_OK) {
                ESP_LOGI(TIME_CTRL_TAG, "NTP sync initiated successfully");
            } else {
                ESP_LOGW(TIME_CTRL_TAG, "Failed to initiate NTP sync: %s", esp_err_to_name(sync_err));
            }
        } else {
            ESP_LOGW(TIME_CTRL_TAG, "WiFi not connected - NTP sync not possible");
        }
    } else {
        ESP_LOGE(TIME_CTRL_TAG, "Failed to get time status: %s", esp_err_to_name(status_err));
    }
#endif
    
    // Send success response
    const char *success_response = 
        "{\n"
        "  \"status\": \"success\",\n"
        "  \"message\": \"NTP configuration updated successfully\"\n"
        "}";
    
    update_request_stats(true);
    return send_json_response(req, success_response);
}

esp_err_t ntp_sync_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/time/ntp/sync");
    
    // Set CORS headers
    set_cors_headers(req);
    
    // Force NTP synchronization
    esp_err_t err = time_manager_force_ntp_sync(0); // Use default timeout
    
    if (err != ESP_OK) {
        update_request_stats(false);
        return send_error_response(req, 500, "Failed to initiate NTP sync");
    }
    
    // Send success response
    const char *success_response = 
        "{\n"
        "  \"status\": \"success\",\n"
        "  \"message\": \"NTP synchronization initiated\"\n"
        "}";
    
    update_request_stats(true);
    return send_json_response(req, success_response);
}

esp_err_t manual_time_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/time/manual");
    
    // Set CORS headers
    set_cors_headers(req);
    
    // Read request body
    char *body = read_request_body(req);
    if (body == NULL) {
        update_request_stats(false);
        return send_error_response(req, 400, "Failed to read request body");
    }
    
    // Parse JSON
    cJSON *json = cJSON_Parse(body);
    free(body);
    
    if (json == NULL) {
        update_request_stats(false);
        return send_error_response(req, 400, "Invalid JSON format");
    }
    
    // Extract time parameters
    cJSON *timestamp_item = cJSON_GetObjectItem(json, "timestamp");
    cJSON *timezone_item = cJSON_GetObjectItem(json, "timezone");
    
    if (!timestamp_item || !cJSON_IsNumber(timestamp_item)) {
        cJSON_Delete(json);
        update_request_stats(false);
        return send_error_response(req, 400, "Missing or invalid timestamp");
    }
    
    time_t timestamp = (time_t)timestamp_item->valuedouble;
    const char *timezone = NULL;
    
    if (timezone_item && cJSON_IsString(timezone_item)) {
        timezone = timezone_item->valuestring;
    }
    
    cJSON_Delete(json);
    
    // Manual time setting is no longer supported - NTP-only time source
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;
    
    // Set timezone separately if provided
    if (err == ESP_OK && timezone != NULL) {
        err = time_manager_set_timezone(timezone);
    }
    
    if (err != ESP_OK) {
        update_request_stats(false);
        return send_error_response(req, 500, "Failed to set manual time");
    }
    
    // Send success response
    const char *success_response = 
        "{\n"
        "  \"status\": \"success\",\n"
        "  \"message\": \"Manual time set successfully\"\n"
        "}";
    
    update_request_stats(true);
    return send_json_response(req, success_response);
}

esp_err_t timezones_handler(httpd_req_t *req)
{
    static char response_buffer[MAX_RESPONSE_SIZE];
    
    ESP_LOGI(TAG, "GET /api/time/timezones");
    
    // Set CORS headers
    set_cors_headers(req);
    
    // Build timezone list response
    snprintf(response_buffer, sizeof(response_buffer),
        "{\n"
        "  \"timezones\": [\n"
        "    {\"name\": \"UTC\", \"offset\": 0, \"description\": \"Coordinated Universal Time\"},\n"
        "    {\"name\": \"EST5EDT\", \"offset\": -18000, \"description\": \"US Eastern Time\"},\n"
        "    {\"name\": \"CST6CDT\", \"offset\": -21600, \"description\": \"US Central Time\"},\n"
        "    {\"name\": \"MST7MDT\", \"offset\": -25200, \"description\": \"US Mountain Time\"},\n"
        "    {\"name\": \"PST8PDT\", \"offset\": -28800, \"description\": \"US Pacific Time\"},\n"
        "    {\"name\": \"CET-1CEST\", \"offset\": 3600, \"description\": \"Central European Time\"},\n"
        "    {\"name\": \"JST-9\", \"offset\": 32400, \"description\": \"Japan Standard Time\"},\n"
        "    {\"name\": \"AEST-10AEDT\", \"offset\": 36000, \"description\": \"Australian Eastern Time\"}\n"
        "  ],\n"
        "  \"count\": 8\n"
        "}"
    );
    
    update_request_stats(true);
    return send_json_response(req, response_buffer);
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static bool register_time_endpoints(void)
{
    esp_err_t ret;
    uint32_t endpoint_count = 0;

    // Register /api/time/status
    httpd_uri_t status_uri = {
        .uri = "/api/time/status",
        .method = HTTP_GET,
        .handler = time_status_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(g_time_controller.server_handle, &status_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/time/status: %s", esp_err_to_name(ret));
        return false;
    }
    endpoint_count++;

    // Register /api/time/ntp/config
    httpd_uri_t ntp_config_uri = {
        .uri = "/api/time/ntp/config",
        .method = HTTP_POST,
        .handler = ntp_config_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(g_time_controller.server_handle, &ntp_config_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/time/ntp/config: %s", esp_err_to_name(ret));
        return false;
    }
    endpoint_count++;

    // Register /api/time/ntp/sync
    httpd_uri_t ntp_sync_uri = {
        .uri = "/api/time/ntp/sync",
        .method = HTTP_POST,
        .handler = ntp_sync_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(g_time_controller.server_handle, &ntp_sync_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/time/ntp/sync: %s", esp_err_to_name(ret));
        return false;
    }
    endpoint_count++;

    // Register /api/time/manual
    httpd_uri_t manual_time_uri = {
        .uri = "/api/time/manual",
        .method = HTTP_POST,
        .handler = manual_time_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(g_time_controller.server_handle, &manual_time_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/time/manual: %s", esp_err_to_name(ret));
        return false;
    }
    endpoint_count++;

    // Register /api/time/timezones
    httpd_uri_t timezones_uri = {
        .uri = "/api/time/timezones",
        .method = HTTP_GET,
        .handler = timezones_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(g_time_controller.server_handle, &timezones_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/time/timezones: %s", esp_err_to_name(ret));
        return false;
    }
    endpoint_count++;

    // Update statistics
    if (xSemaphoreTake(g_time_controller.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_time_controller.stats.endpoints_registered = endpoint_count;
        xSemaphoreGive(g_time_controller.stats_mutex);
    }

    ESP_LOGI(TAG, "Registered %lu time management endpoints", (unsigned long)endpoint_count);
    return true;
}

static void update_request_stats(bool success)
{
    if (xSemaphoreTake(g_time_controller.stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_time_controller.stats.total_requests++;
        if (success) {
            g_time_controller.stats.successful_requests++;
        } else {
            g_time_controller.stats.failed_requests++;
        }
        g_time_controller.stats.last_request_time = GET_TIMESTAMP();
        xSemaphoreGive(g_time_controller.stats_mutex);
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

static esp_err_t send_error_response(httpd_req_t *req, int status_code, const char *message)
{
    char status_str[16];
    char response[256];
    
    snprintf(status_str, sizeof(status_str), "%d", status_code);
    snprintf(response, sizeof(response),
        "{\n"
        "  \"error\": true,\n"
        "  \"status_code\": %d,\n"
        "  \"message\": \"%s\"\n"
        "}",
        status_code, message
    );
    
    httpd_resp_set_status(req, status_str);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static char* read_request_body(httpd_req_t *req)
{
    size_t content_length = req->content_len;
    
    if (content_length == 0 || content_length > MAX_REQUEST_BODY_SIZE) {
        ESP_LOGE(TAG, "Invalid content length: %zu", content_length);
        return NULL;
    }
    
    char *buffer = malloc(content_length + 1);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for request body");
        return NULL;
    }
    
    int received = httpd_req_recv(req, buffer, content_length);
    if (received <= 0) {
        ESP_LOGE(TAG, "Failed to receive request body");
        free(buffer);
        return NULL;
    }
    
    buffer[received] = '\0';
    return buffer;
}
