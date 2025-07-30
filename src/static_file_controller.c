/**
 * @file static_file_controller.c
 * @brief Static File Controller implementation for SNRv9 Irrigation Control System
 */

#include "static_file_controller.h"
#include "debug_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* =============================================================================
 * PRIVATE CONSTANTS AND MACROS
 * =============================================================================
 */

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
    static_file_stats_t stats;
    SemaphoreHandle_t stats_mutex;
    cache_config_t cache_config;
    cache_entry_t cache_entries[16];  // Small cache for ESP32
    uint32_t cache_entry_count;
    SemaphoreHandle_t cache_mutex;
    bool initialized;
} static_file_context_t;

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static static_file_context_t g_static_file = {0};
static const char *TAG = "STATIC_FILE";

/* Advanced MIME type mappings with caching and compression settings */
static const mime_type_mapping_t g_mime_mappings[] = {
    // Text files - compressible, short cache for dynamic content
    {".html", "text/html", false, true, 300},                    // 5 minutes
    {".htm",  "text/html", false, true, 300},                    // 5 minutes
    {".txt",  "text/plain", false, true, 300},                   // 5 minutes
    {".xml",  "application/xml", false, true, 300},              // 5 minutes
    {".json", "application/json", false, true, 60},              // 1 minute (API responses)
    
    // Static assets - compressible, long cache
    {".css",  "text/css", true, true, 86400},                    // 24 hours
    {".js",   "application/javascript", true, true, 86400},      // 24 hours
    {".svg",  "image/svg+xml", true, true, 86400},               // 24 hours
    
    // Images - not compressible (already compressed), long cache
    {".ico",  "image/x-icon", true, false, 604800},              // 7 days
    {".png",  "image/png", true, false, 604800},                 // 7 days
    {".jpg",  "image/jpeg", true, false, 604800},                // 7 days
    {".jpeg", "image/jpeg", true, false, 604800},                // 7 days
    {".gif",  "image/gif", true, false, 604800},                 // 7 days
    {".webp", "image/webp", true, false, 604800},                // 7 days
    
    // Fonts - not compressible, very long cache
    {".woff", "font/woff", true, false, 2592000},                // 30 days
    {".woff2", "font/woff2", true, false, 2592000},              // 30 days
    {".ttf",  "font/ttf", true, false, 2592000},                 // 30 days
    {".otf",  "font/otf", true, false, 2592000},                 // 30 days
    
    {NULL, NULL, false, false, 0}  // Sentinel
};

/* =============================================================================
 * CONSTANTS
 * =============================================================================
 */

#define LITTLEFS_BASE_PATH "/littlefs"
#define MAX_FILE_SIZE 65536  // 64KB max file size for ESP32

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t file_handler(httpd_req_t *req);
static void update_request_stats(bool success, size_t bytes_served);
static const char* get_file_extension(const char *path);
static esp_err_t serve_file_from_data(httpd_req_t *req, const char *filename);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool static_file_controller_init(void)
{
    if (g_static_file.initialized) {
        ESP_LOGW(TAG, "Static file controller already initialized");
        return false;
    }

    // Create mutex for thread-safe statistics access
    g_static_file.stats_mutex = xSemaphoreCreateMutex();
    if (g_static_file.stats_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create statistics mutex");
        return false;
    }

    // Create mutex for thread-safe cache access
    g_static_file.cache_mutex = xSemaphoreCreateMutex();
    if (g_static_file.cache_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create cache mutex");
        vSemaphoreDelete(g_static_file.stats_mutex);
        return false;
    }

    // Initialize statistics
    memset(&g_static_file.stats, 0, sizeof(static_file_stats_t));
    
    // Initialize cache configuration with defaults
    g_static_file.cache_config.etag_enabled = true;
    g_static_file.cache_config.conditional_requests = true;
    g_static_file.cache_config.compression_enabled = false;  // Disabled for now (ESP32 CPU constraints)
    g_static_file.cache_config.default_cache_age = STATIC_FILE_CACHE_MAX_AGE;
    g_static_file.cache_config.max_cache_entries = 16;
    
    // Initialize cache entries
    memset(g_static_file.cache_entries, 0, sizeof(g_static_file.cache_entries));
    g_static_file.cache_entry_count = 0;
    
    g_static_file.initialized = true;
    ESP_LOGI(TAG, "Static file controller initialized successfully with advanced caching");
    return true;
}

bool static_file_controller_register_handlers(httpd_handle_t server)
{
    if (!g_static_file.initialized) {
        ESP_LOGE(TAG, "Static file controller not initialized");
        return false;
    }

    if (server == NULL) {
        ESP_LOGE(TAG, "Invalid server handle");
        return false;
    }

    // Register root path handler
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };

    esp_err_t ret = httpd_register_uri_handler(server, &root_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register root handler: %s", esp_err_to_name(ret));
        return false;
    }

    // Register individual file handlers
    httpd_uri_t test_uri = {
        .uri = "/test.html",
        .method = HTTP_GET,
        .handler = file_handler,
        .user_ctx = NULL
    };

    ret = httpd_register_uri_handler(server, &test_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register test.html handler: %s", esp_err_to_name(ret));
        return false;
    }

    httpd_uri_t app_js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = file_handler,
        .user_ctx = NULL
    };

    ret = httpd_register_uri_handler(server, &app_js_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register app.js handler: %s", esp_err_to_name(ret));
        return false;
    }

    httpd_uri_t style_css_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = file_handler,
        .user_ctx = NULL
    };

    ret = httpd_register_uri_handler(server, &style_css_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register style.css handler: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Static file handlers registered successfully");
    return true;
}

bool static_file_controller_unregister_handlers(httpd_handle_t server)
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Invalid server handle");
        return false;
    }

    esp_err_t ret = httpd_unregister_uri_handler(server, "/", HTTP_GET);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister root handler: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Static file handlers unregistered successfully");
    return true;
}

bool static_file_controller_get_stats(static_file_stats_t *stats)
{
    if (stats == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_static_file.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &g_static_file.stats, sizeof(static_file_stats_t));
        xSemaphoreGive(g_static_file.stats_mutex);
        return true;
    }

    return false;
}

void static_file_controller_reset_stats(void)
{
    if (xSemaphoreTake(g_static_file.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&g_static_file.stats, 0, sizeof(static_file_stats_t));
        xSemaphoreGive(g_static_file.stats_mutex);
    }
}

void static_file_controller_print_status(void)
{
    uint32_t timestamp = GET_TIMESTAMP();
    static_file_stats_t stats;
    
    printf(TIMESTAMP_FORMAT "%s: === STATIC FILE CONTROLLER STATUS ===\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
    
    if (static_file_controller_get_stats(&stats)) {
        printf(TIMESTAMP_FORMAT "%s: Total Requests: %lu\n",
               FORMAT_TIMESTAMP(timestamp), TAG, (unsigned long)stats.total_requests);
        
        printf(TIMESTAMP_FORMAT "%s: Successful: %lu, Failed: %lu\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               (unsigned long)stats.successful_requests, (unsigned long)stats.failed_requests);
        
        printf(TIMESTAMP_FORMAT "%s: Cache Hits: %lu, Bytes Served: %lu\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               (unsigned long)stats.cache_hits, (unsigned long)stats.bytes_served);
    }
    
    printf(TIMESTAMP_FORMAT "%s: =====================================\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
}

bool static_file_controller_get_mime_type(const char *extension, 
                                         char *mime_type, 
                                         size_t mime_type_size)
{
    if (extension == NULL || mime_type == NULL || mime_type_size == 0) {
        return false;
    }

    for (int i = 0; g_mime_mappings[i].extension != NULL; i++) {
        if (strcmp(extension, g_mime_mappings[i].extension) == 0) {
            strncpy(mime_type, g_mime_mappings[i].mime_type, mime_type_size - 1);
            mime_type[mime_type_size - 1] = '\0';
            return true;
        }
    }

    // Default to text/plain for unknown extensions
    strncpy(mime_type, "text/plain", mime_type_size - 1);
    mime_type[mime_type_size - 1] = '\0';
    return false;
}

bool static_file_controller_is_cacheable(const char *extension)
{
    if (extension == NULL) {
        return false;
    }

    for (int i = 0; g_mime_mappings[i].extension != NULL; i++) {
        if (strcmp(extension, g_mime_mappings[i].extension) == 0) {
            return g_mime_mappings[i].cacheable;
        }
    }

    return false;
}

esp_err_t static_file_controller_serve_embedded(httpd_req_t *req,
                                               const char *content,
                                               size_t content_length,
                                               const char *mime_type,
                                               bool cacheable)
{
    if (req == NULL || content == NULL || mime_type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t timestamp = GET_TIMESTAMP();
    
    // Set content type
    esp_err_t ret = httpd_resp_set_type(req, mime_type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set content type: %s", esp_err_to_name(ret));
        update_request_stats(false, 0);
        return ret;
    }

    // Set caching headers if cacheable
    if (cacheable) {
        char cache_header[64];
        snprintf(cache_header, sizeof(cache_header), "max-age=%d", STATIC_FILE_CACHE_MAX_AGE);
        httpd_resp_set_hdr(req, "Cache-Control", cache_header);
        httpd_resp_set_hdr(req, "ETag", "\"static-v1\"");
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
        httpd_resp_set_hdr(req, "Expires", "0");
    }

    // Add CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    // Send content
    ret = httpd_resp_send(req, content, content_length);
    
    if (ret == ESP_OK) {
        update_request_stats(true, content_length);
        printf(TIMESTAMP_FORMAT "%s: Served embedded content (%zu bytes, %s)\n",
               FORMAT_TIMESTAMP(timestamp), TAG, content_length, mime_type);
    } else {
        update_request_stats(false, 0);
        ESP_LOGE(TAG, "Failed to send embedded content: %s", esp_err_to_name(ret));
    }

    return ret;
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static esp_err_t root_handler(httpd_req_t *req)
{
    uint32_t timestamp = GET_TIMESTAMP();
    
    printf(TIMESTAMP_FORMAT "%s: Root path requested from %s\n",
           FORMAT_TIMESTAMP(timestamp), TAG, 
           req->aux ? (char*)req->aux : "unknown");

    // Serve index.html from LittleFS
    return serve_file_from_data(req, "index.html");
}

static void update_request_stats(bool success, size_t bytes_served)
{
    if (xSemaphoreTake(g_static_file.stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_static_file.stats.total_requests++;
        if (success) {
            g_static_file.stats.successful_requests++;
            g_static_file.stats.bytes_served += bytes_served;
        } else {
            g_static_file.stats.failed_requests++;
        }
        g_static_file.stats.last_request_time = GET_TIMESTAMP();
        xSemaphoreGive(g_static_file.stats_mutex);
    }
}

static const char* get_file_extension(const char *path)
{
    if (path == NULL) {
        return NULL;
    }

    const char *dot = strrchr(path, '.');
    if (dot == NULL || dot == path) {
        return NULL;
    }

    return dot;
}

static esp_err_t serve_file_from_data(httpd_req_t *req, const char *filename)
{
    uint32_t timestamp = GET_TIMESTAMP();
    
    printf(TIMESTAMP_FORMAT "%s: Attempting to serve file from LittleFS: %s\n",
           FORMAT_TIMESTAMP(timestamp), TAG, filename);

    // Construct full file path
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "%s/%s", LITTLEFS_BASE_PATH, filename);
    
    // Open file from LittleFS
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        printf(TIMESTAMP_FORMAT "%s: Failed to open file: %s (errno: %d)\n",
               FORMAT_TIMESTAMP(timestamp), TAG, file_path, errno);
        
        // Send 404 Not Found
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/html");
        
        const char *not_found_html = 
            "<!DOCTYPE html>\n"
            "<html><head><title>404 Not Found</title></head>\n"
            "<body><h1>404 Not Found</h1>\n"
            "<p>The requested file was not found on this server.</p>\n"
            "<p><a href=\"/\">Return to main page</a></p>\n"
            "</body></html>";
        
        update_request_stats(false, 0);
        return httpd_resp_send(req, not_found_html, strlen(not_found_html));
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > MAX_FILE_SIZE) {
        printf(TIMESTAMP_FORMAT "%s: Invalid file size: %ld bytes for %s\n",
               FORMAT_TIMESTAMP(timestamp), TAG, file_size, filename);
        fclose(file);
        
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "text/html");
        
        const char *error_html = 
            "<!DOCTYPE html>\n"
            "<html><head><title>500 Internal Server Error</title></head>\n"
            "<body><h1>500 Internal Server Error</h1>\n"
            "<p>File size error or file too large.</p>\n"
            "</body></html>";
        
        update_request_stats(false, 0);
        return httpd_resp_send(req, error_html, strlen(error_html));
    }
    
    // Allocate buffer for file content
    char *file_content = malloc(file_size + 1);
    if (file_content == NULL) {
        printf(TIMESTAMP_FORMAT "%s: Failed to allocate memory for file: %s\n",
               FORMAT_TIMESTAMP(timestamp), TAG, filename);
        fclose(file);
        
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "text/html");
        
        const char *error_html = 
            "<!DOCTYPE html>\n"
            "<html><head><title>500 Internal Server Error</title></head>\n"
            "<body><h1>500 Internal Server Error</h1>\n"
            "<p>Memory allocation failed.</p>\n"
            "</body></html>";
        
        update_request_stats(false, 0);
        return httpd_resp_send(req, error_html, strlen(error_html));
    }
    
    // Read file content
    size_t bytes_read = fread(file_content, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != file_size) {
        printf(TIMESTAMP_FORMAT "%s: Failed to read complete file: %s (read %zu of %ld bytes)\n",
               FORMAT_TIMESTAMP(timestamp), TAG, filename, bytes_read, file_size);
        free(file_content);
        
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "text/html");
        
        const char *error_html = 
            "<!DOCTYPE html>\n"
            "<html><head><title>500 Internal Server Error</title></head>\n"
            "<body><h1>500 Internal Server Error</h1>\n"
            "<p>File read error.</p>\n"
            "</body></html>";
        
        update_request_stats(false, 0);
        return httpd_resp_send(req, error_html, strlen(error_html));
    }
    
    // Null-terminate the content
    file_content[file_size] = '\0';
    
    // Determine MIME type from file extension
    const char *extension = get_file_extension(filename);
    char mime_type[64] = "text/plain";  // Default
    
    if (extension != NULL) {
        static_file_controller_get_mime_type(extension, mime_type, sizeof(mime_type));
    }
    
    printf(TIMESTAMP_FORMAT "%s: Successfully read file %s (%ld bytes, %s)\n",
           FORMAT_TIMESTAMP(timestamp), TAG, filename, file_size, mime_type);
    
    // Serve the file content with caching
    esp_err_t result = static_file_controller_serve_with_cache(req, filename,
                                                              file_content, 
                                                              file_size,
                                                              mime_type);
    
    // Free the allocated memory
    free(file_content);
    
    return result;
}

static esp_err_t file_handler(httpd_req_t *req)
{
    uint32_t timestamp = GET_TIMESTAMP();
    
    printf(TIMESTAMP_FORMAT "%s: File request: %s\n",
           FORMAT_TIMESTAMP(timestamp), TAG, req->uri);
    
    // Extract filename from URI (skip leading slash)
    const char *filename = req->uri + 1;
    
    return serve_file_from_data(req, filename);
}

/* =============================================================================
 * ADVANCED CACHING FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool static_file_controller_is_compressible(const char *extension)
{
    if (extension == NULL) {
        return false;
    }

    for (int i = 0; g_mime_mappings[i].extension != NULL; i++) {
        if (strcmp(extension, g_mime_mappings[i].extension) == 0) {
            return g_mime_mappings[i].compressible;
        }
    }

    return false;
}

uint32_t static_file_controller_get_cache_max_age(const char *extension)
{
    if (extension == NULL) {
        return 0;
    }

    for (int i = 0; g_mime_mappings[i].extension != NULL; i++) {
        if (strcmp(extension, g_mime_mappings[i].extension) == 0) {
            return g_mime_mappings[i].cache_max_age;
        }
    }

    return g_static_file.cache_config.default_cache_age;
}

bool static_file_controller_generate_etag(const char *content,
                                         size_t content_length,
                                         char *etag)
{
    if (content == NULL || etag == NULL || content_length == 0) {
        return false;
    }

    // Simple hash-based ETag generation (FNV-1a hash)
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < content_length; i++) {
        hash ^= (uint8_t)content[i];
        hash *= 16777619U;
    }

    // Include content length in hash for uniqueness
    hash ^= (uint32_t)content_length;
    hash *= 16777619U;

    snprintf(etag, STATIC_FILE_ETAG_LENGTH, "\"%08lx\"", (unsigned long)hash);
    return true;
}

bool static_file_controller_check_etag_match(httpd_req_t *req, const char *etag)
{
    if (req == NULL || etag == NULL) {
        return false;
    }

    // Check If-None-Match header
    size_t header_len = httpd_req_get_hdr_value_len(req, "If-None-Match");
    if (header_len > 0) {
        char *if_none_match = malloc(header_len + 1);
        if (if_none_match != NULL) {
            esp_err_t ret = httpd_req_get_hdr_value_str(req, "If-None-Match", 
                                                       if_none_match, header_len + 1);
            if (ret == ESP_OK) {
                bool match = (strcmp(if_none_match, etag) == 0);
                free(if_none_match);
                return match;
            }
            free(if_none_match);
        }
    }

    return false;
}

esp_err_t static_file_controller_serve_with_cache(httpd_req_t *req,
                                                 const char *filename,
                                                 const char *content,
                                                 size_t content_length,
                                                 const char *mime_type)
{
    if (req == NULL || filename == NULL || content == NULL || mime_type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t timestamp = GET_TIMESTAMP();
    
    // Generate ETag if enabled
    char etag[STATIC_FILE_ETAG_LENGTH] = {0};
    bool etag_generated = false;
    
    if (g_static_file.cache_config.etag_enabled) {
        etag_generated = static_file_controller_generate_etag(content, content_length, etag);
        
        if (etag_generated) {
            printf(TIMESTAMP_FORMAT "%s: Generated ETag %s for %s\n",
                   FORMAT_TIMESTAMP(timestamp), TAG, etag, filename);
        }
    }

    // Check for conditional requests
    if (g_static_file.cache_config.conditional_requests && etag_generated) {
        if (static_file_controller_check_etag_match(req, etag)) {
            printf(TIMESTAMP_FORMAT "%s: ETag match for %s, sending 304 Not Modified\n",
                   FORMAT_TIMESTAMP(timestamp), TAG, filename);
            
            // Send 304 Not Modified
            httpd_resp_set_status(req, "304 Not Modified");
            httpd_resp_set_hdr(req, "ETag", etag);
            
            // Update cache hit statistics
            if (xSemaphoreTake(g_static_file.stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                g_static_file.stats.cache_hits++;
                g_static_file.stats.total_requests++;
                g_static_file.stats.successful_requests++;
                xSemaphoreGive(g_static_file.stats_mutex);
            }
            
            return httpd_resp_send(req, NULL, 0);
        }
    }

    // Set content type
    esp_err_t ret = httpd_resp_set_type(req, mime_type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set content type: %s", esp_err_to_name(ret));
        update_request_stats(false, 0);
        return ret;
    }

    // Get file extension for cache settings
    const char *extension = get_file_extension(filename);
    bool cacheable = static_file_controller_is_cacheable(extension);
    uint32_t cache_max_age = static_file_controller_get_cache_max_age(extension);

    // Set caching headers
    if (cacheable && cache_max_age > 0) {
        char cache_header[64];
        snprintf(cache_header, sizeof(cache_header), "max-age=%lu, public", 
                (unsigned long)cache_max_age);
        httpd_resp_set_hdr(req, "Cache-Control", cache_header);
        
        if (etag_generated) {
            httpd_resp_set_hdr(req, "ETag", etag);
        }
        
        printf(TIMESTAMP_FORMAT "%s: Set cache headers for %s (max-age=%lu)\n",
               FORMAT_TIMESTAMP(timestamp), TAG, filename, (unsigned long)cache_max_age);
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
        httpd_resp_set_hdr(req, "Expires", "0");
    }

    // Add CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    // Send content
    ret = httpd_resp_send(req, content, content_length);
    
    if (ret == ESP_OK) {
        update_request_stats(true, content_length);
        printf(TIMESTAMP_FORMAT "%s: Served %s with advanced caching (%zu bytes, %s)\n",
               FORMAT_TIMESTAMP(timestamp), TAG, filename, content_length, mime_type);
    } else {
        update_request_stats(false, 0);
        ESP_LOGE(TAG, "Failed to send content: %s", esp_err_to_name(ret));
    }

    return ret;
}

bool static_file_controller_configure_cache(const cache_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_static_file.cache_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(&g_static_file.cache_config, config, sizeof(cache_config_t));
        xSemaphoreGive(g_static_file.cache_mutex);
        
        ESP_LOGI(TAG, "Cache configuration updated: ETag=%s, Conditional=%s, Compression=%s",
                 config->etag_enabled ? "enabled" : "disabled",
                 config->conditional_requests ? "enabled" : "disabled",
                 config->compression_enabled ? "enabled" : "disabled");
        
        return true;
    }

    return false;
}

bool static_file_controller_get_cache_config(cache_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_static_file.cache_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(config, &g_static_file.cache_config, sizeof(cache_config_t));
        xSemaphoreGive(g_static_file.cache_mutex);
        return true;
    }

    return false;
}

bool static_file_controller_get_cache_stats(uint32_t *total_entries, float *hit_rate)
{
    if (total_entries == NULL || hit_rate == NULL) {
        return false;
    }

    static_file_stats_t stats;
    if (static_file_controller_get_stats(&stats)) {
        *total_entries = g_static_file.cache_entry_count;
        
        if (stats.total_requests > 0) {
            *hit_rate = (float)stats.cache_hits / (float)stats.total_requests;
        } else {
            *hit_rate = 0.0f;
        }
        
        return true;
    }

    return false;
}

void static_file_controller_clear_cache(void)
{
    if (xSemaphoreTake(g_static_file.cache_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(g_static_file.cache_entries, 0, sizeof(g_static_file.cache_entries));
        g_static_file.cache_entry_count = 0;
        xSemaphoreGive(g_static_file.cache_mutex);
        
        ESP_LOGI(TAG, "Cache cleared");
    }
}
