/**
 * @file web_server_manager.c
 * @brief Web Server Manager implementation for SNRv9 Irrigation Control System
 */

#include "web_server_manager.h"
#include "auth_controller.h"
#include "debug_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_server.h"
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
    web_server_status_t status;
    web_server_config_t config;
    web_server_stats_t stats;
    httpd_handle_t server_handle;
    SemaphoreHandle_t stats_mutex;
    uint32_t start_time;
    bool initialized;
} web_server_context_t;

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static web_server_context_t g_web_server = {0};
static const char *TAG = DEBUG_WEB_SERVER_TAG;

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static esp_err_t status_handler(httpd_req_t *req);
static bool register_api_handlers(void);
static void update_request_stats(bool success);
static void get_system_status_json(char *buffer, size_t buffer_size);
static const char* status_to_string(web_server_status_t status);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool web_server_manager_init(void)
{
    web_server_config_t default_config;
    web_server_manager_get_default_config(&default_config);
    return web_server_manager_init_with_config(&default_config);
}

bool web_server_manager_init_with_config(const web_server_config_t *config)
{
    if (g_web_server.initialized) {
        ESP_LOGW(TAG, "Web server manager already initialized");
        return false;
    }

    if (config == NULL) {
        ESP_LOGE(TAG, "Configuration cannot be NULL");
        return false;
    }

    // Initialize context
    memset(&g_web_server, 0, sizeof(web_server_context_t));
    memcpy(&g_web_server.config, config, sizeof(web_server_config_t));
    g_web_server.status = WEB_SERVER_STOPPED;

    // Create mutex for thread-safe statistics access
    g_web_server.stats_mutex = xSemaphoreCreateMutex();
    if (g_web_server.stats_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create statistics mutex");
        return false;
    }

    g_web_server.initialized = true;
    ESP_LOGI(TAG, "Web server manager initialized successfully (port: %d)", config->port);
    return true;
}

bool web_server_manager_start(void)
{
    if (g_web_server.status != WEB_SERVER_STOPPED) {
        ESP_LOGW(TAG, "Web server already started or starting");
        return false;
    }

    g_web_server.status = WEB_SERVER_STARTING;
    uint32_t timestamp = GET_TIMESTAMP();

    ESP_LOGI(TAG, "Starting web server on port %d", g_web_server.config.port);

    // Initialize static file controller
    if (!static_file_controller_init()) {
        ESP_LOGE(TAG, "Failed to initialize static file controller");
        g_web_server.status = WEB_SERVER_ERROR;
        return false;
    }

    // Configure HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = g_web_server.config.port;
    config.max_uri_handlers = g_web_server.config.max_uri_handlers;
    config.max_open_sockets = g_web_server.config.max_open_sockets;
    config.task_priority = g_web_server.config.task_priority;
    config.stack_size = g_web_server.config.task_stack_size;

    // Start HTTP server
    esp_err_t ret = httpd_start(&g_web_server.server_handle, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        g_web_server.status = WEB_SERVER_ERROR;
        return false;
    }

    // Register API handlers
    if (!register_api_handlers()) {
        ESP_LOGE(TAG, "Failed to register API handlers");
        httpd_stop(g_web_server.server_handle);
        g_web_server.server_handle = NULL;
        g_web_server.status = WEB_SERVER_ERROR;
        return false;
    }

    // Register static file handlers
    if (!static_file_controller_register_handlers(g_web_server.server_handle)) {
        ESP_LOGE(TAG, "Failed to register static file handlers");
        httpd_stop(g_web_server.server_handle);
        g_web_server.server_handle = NULL;
        g_web_server.status = WEB_SERVER_ERROR;
        return false;
    }

    // Initialize authentication controller
    if (!auth_controller_init(g_web_server.server_handle)) {
        ESP_LOGE(TAG, "Failed to initialize authentication controller");
        httpd_stop(g_web_server.server_handle);
        g_web_server.server_handle = NULL;
        g_web_server.status = WEB_SERVER_ERROR;
        return false;
    }

    // Initialize statistics
    if (xSemaphoreTake(g_web_server.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&g_web_server.stats, 0, sizeof(web_server_stats_t));
        g_web_server.start_time = timestamp;
        xSemaphoreGive(g_web_server.stats_mutex);
    }

    g_web_server.status = WEB_SERVER_RUNNING;

    ESP_LOGI(TAG, "Web server started successfully with static file support");

    return true;
}

bool web_server_manager_stop(void)
{
    if (g_web_server.status != WEB_SERVER_RUNNING) {
        ESP_LOGW(TAG, "Web server not running");
        return true;
    }

    g_web_server.status = WEB_SERVER_STOPPING;

    // Unregister static file handlers
    if (g_web_server.server_handle != NULL) {
        static_file_controller_unregister_handlers(g_web_server.server_handle);
    }

    // Stop HTTP server
    if (g_web_server.server_handle != NULL) {
        esp_err_t ret = httpd_stop(g_web_server.server_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop HTTP server: %s", esp_err_to_name(ret));
            g_web_server.status = WEB_SERVER_ERROR;
            return false;
        }
        g_web_server.server_handle = NULL;
    }

    g_web_server.status = WEB_SERVER_STOPPED;
    ESP_LOGI(TAG, "Web server stopped successfully");
    return true;
}

web_server_status_t web_server_manager_get_status(void)
{
    return g_web_server.status;
}

bool web_server_manager_get_stats(web_server_stats_t *stats)
{
    if (stats == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_web_server.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &g_web_server.stats, sizeof(web_server_stats_t));
        
        // Update uptime
        if (g_web_server.status == WEB_SERVER_RUNNING) {
            uint32_t current_time = GET_TIMESTAMP();
            stats->uptime_seconds = (current_time - g_web_server.start_time) / 1000;
        }
        
        xSemaphoreGive(g_web_server.stats_mutex);
        return true;
    }

    return false;
}

void web_server_manager_reset_stats(void)
{
    if (xSemaphoreTake(g_web_server.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&g_web_server.stats, 0, sizeof(web_server_stats_t));
        g_web_server.start_time = GET_TIMESTAMP();
        xSemaphoreGive(g_web_server.stats_mutex);
    }
}

bool web_server_manager_is_running(void)
{
    return g_web_server.status == WEB_SERVER_RUNNING;
}

httpd_handle_t web_server_manager_get_handle(void)
{
    if (g_web_server.status == WEB_SERVER_RUNNING) {
        return g_web_server.server_handle;
    }
    return NULL;
}

void web_server_manager_print_status(void)
{
    web_server_stats_t stats;
    
    ESP_LOGI(TAG, "=== WEB SERVER STATUS ===");
    ESP_LOGI(TAG, "Status: %s", status_to_string(g_web_server.status));
    ESP_LOGI(TAG, "Port: %d", g_web_server.config.port);
    
    if (web_server_manager_get_stats(&stats)) {
        ESP_LOGI(TAG, "Uptime: %lu seconds", (unsigned long)stats.uptime_seconds);
        ESP_LOGI(TAG, "Requests: %lu total, %lu success, %lu failed",
               (unsigned long)stats.total_requests, (unsigned long)stats.successful_requests, (unsigned long)stats.failed_requests);
        ESP_LOGI(TAG, "Connections: %lu active, %lu max seen",
               (unsigned long)stats.active_connections, (unsigned long)stats.max_connections_seen);
    }
    
    ESP_LOGI(TAG, "========================");
}

void web_server_manager_get_default_config(web_server_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->port = WEB_SERVER_DEFAULT_PORT;
    config->max_uri_handlers = WEB_SERVER_MAX_URI_HANDLERS;
    config->max_open_sockets = WEB_SERVER_MAX_OPEN_SOCKETS;
    config->task_stack_size = WEB_SERVER_TASK_STACK_SIZE;
    config->task_priority = WEB_SERVER_TASK_PRIORITY;
    config->enable_cors = true;
    config->enable_logging = true;
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static bool register_api_handlers(void)
{
    if (g_web_server.server_handle == NULL) {
        return false;
    }

    // Register /api/status handler
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL
    };

    esp_err_t ret = httpd_register_uri_handler(g_web_server.server_handle, &status_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register status handler: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "API handlers registered successfully");
    return true;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char response_buffer[1024];
    
    // Update request statistics
    update_request_stats(true);
    
    // Set content type to JSON
    httpd_resp_set_type(req, "application/json");
    
    // Add CORS headers if enabled
    if (g_web_server.config.enable_cors) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
    }
    
    // Generate system status JSON
    get_system_status_json(response_buffer, sizeof(response_buffer));
    
    // Send response
    esp_err_t ret = httpd_resp_send(req, response_buffer, HTTPD_RESP_USE_STRLEN);
    
    if (g_web_server.config.enable_logging) {
        ESP_LOGI(TAG, "GET /api/status - %s", (ret == ESP_OK) ? "200 OK" : "500 Error");
    }
    
    return ret;
}

static void update_request_stats(bool success)
{
    if (xSemaphoreTake(g_web_server.stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_web_server.stats.total_requests++;
        if (success) {
            g_web_server.stats.successful_requests++;
        } else {
            g_web_server.stats.failed_requests++;
        }
        g_web_server.stats.last_request_time = GET_TIMESTAMP();
        xSemaphoreGive(g_web_server.stats_mutex);
    }
}

static void get_system_status_json(char *buffer, size_t buffer_size)
{
    web_server_stats_t stats;
    bool stats_available = web_server_manager_get_stats(&stats);
    uint32_t current_time = GET_TIMESTAMP();
    
    snprintf(buffer, buffer_size,
        "{\n"
        "  \"system\": {\n"
        "    \"name\": \"SNRv9 Irrigation Control\",\n"
        "    \"version\": \"1.0.0\",\n"
        "    \"timestamp\": %lu,\n"
        "    \"uptime_ms\": %lu\n"
        "  },\n"
        "  \"web_server\": {\n"
        "    \"status\": \"%s\",\n"
        "    \"port\": %d,\n"
        "    \"uptime_seconds\": %lu,\n"
        "    \"total_requests\": %lu,\n"
        "    \"successful_requests\": %lu,\n"
        "    \"failed_requests\": %lu\n"
        "  },\n"
        "  \"memory\": {\n"
        "    \"free_heap\": %u,\n"
        "    \"min_free_heap\": %u\n"
        "  }\n"
        "}",
        (unsigned long)current_time,
        (unsigned long)current_time,
        status_to_string(g_web_server.status),
        g_web_server.config.port,
        (unsigned long)(stats_available ? stats.uptime_seconds : 0),
        (unsigned long)(stats_available ? stats.total_requests : 0),
        (unsigned long)(stats_available ? stats.successful_requests : 0),
        (unsigned long)(stats_available ? stats.failed_requests : 0),
        (unsigned int)esp_get_free_heap_size(),
        (unsigned int)esp_get_minimum_free_heap_size()
    );
}

static const char* status_to_string(web_server_status_t status)
{
    switch (status) {
        case WEB_SERVER_STOPPED:  return "stopped";
        case WEB_SERVER_STARTING: return "starting";
        case WEB_SERVER_RUNNING:  return "running";
        case WEB_SERVER_STOPPING: return "stopping";
        case WEB_SERVER_ERROR:    return "error";
        default:                  return "unknown";
    }
}
