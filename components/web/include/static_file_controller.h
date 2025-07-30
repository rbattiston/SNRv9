/**
 * @file static_file_controller.h
 * @brief Static File Controller for SNRv9 Irrigation Control System
 * 
 * This module provides static file serving capabilities including HTML, CSS, and JavaScript
 * files with proper MIME type detection and HTTP caching for optimal performance.
 */

#ifndef STATIC_FILE_CONTROLLER_H
#define STATIC_FILE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * PUBLIC CONSTANTS AND MACROS
 * =============================================================================
 */

#define STATIC_FILE_MAX_PATH_LENGTH     256
#define STATIC_FILE_MAX_MIME_LENGTH     64
#define STATIC_FILE_CACHE_MAX_AGE       3600    ///< Cache max age in seconds (1 hour)
#define STATIC_FILE_ETAG_LENGTH         16      ///< ETag string length
#define STATIC_FILE_MAX_EXTENSIONS      32      ///< Maximum supported file extensions
#define STATIC_FILE_GZIP_MIN_SIZE       1024    ///< Minimum size for gzip compression

/* =============================================================================
 * PUBLIC TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief Static file controller statistics
 */
typedef struct {
    uint32_t total_requests;        ///< Total static file requests
    uint32_t successful_requests;   ///< Successful file serves
    uint32_t failed_requests;       ///< Failed requests (404, etc.)
    uint32_t cache_hits;            ///< Requests served from cache
    uint32_t bytes_served;          ///< Total bytes served
    uint32_t last_request_time;     ///< Timestamp of last request (ms)
} static_file_stats_t;

/**
 * @brief MIME type mapping structure
 */
typedef struct {
    const char *extension;          ///< File extension (e.g., ".html")
    const char *mime_type;          ///< MIME type (e.g., "text/html")
    bool cacheable;                 ///< Whether file type should be cached
    bool compressible;              ///< Whether file type can be compressed
    uint32_t cache_max_age;         ///< Cache max age for this file type (seconds)
} mime_type_mapping_t;

/**
 * @brief Cache entry structure for ETag and conditional requests
 */
typedef struct {
    char filename[STATIC_FILE_MAX_PATH_LENGTH];  ///< File name/path
    char etag[STATIC_FILE_ETAG_LENGTH];          ///< ETag value
    uint32_t content_hash;                       ///< Content hash for ETag generation
    uint32_t last_modified;                      ///< Last modified timestamp
    size_t content_length;                       ///< Content length
    uint32_t access_count;                       ///< Number of times accessed
    uint32_t last_access;                        ///< Last access timestamp
} cache_entry_t;

/**
 * @brief Advanced caching configuration
 */
typedef struct {
    bool etag_enabled;              ///< Enable ETag support
    bool conditional_requests;      ///< Enable If-None-Match/If-Modified-Since
    bool compression_enabled;       ///< Enable gzip compression
    uint32_t default_cache_age;     ///< Default cache age (seconds)
    uint32_t max_cache_entries;     ///< Maximum cache entries
} cache_config_t;

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the static file controller
 * 
 * This function initializes the static file controller and must be called
 * before registering any static file handlers.
 * 
 * @return true if initialization successful, false otherwise
 */
bool static_file_controller_init(void);

/**
 * @brief Register static file handlers with the web server
 * 
 * This function registers all static file URI handlers with the provided
 * HTTP server handle.
 * 
 * @param server HTTP server handle
 * @return true if registration successful, false otherwise
 */
bool static_file_controller_register_handlers(httpd_handle_t server);

/**
 * @brief Unregister static file handlers from the web server
 * 
 * @param server HTTP server handle
 * @return true if unregistration successful, false otherwise
 */
bool static_file_controller_unregister_handlers(httpd_handle_t server);

/**
 * @brief Get static file controller statistics
 * 
 * @param stats Pointer to statistics structure to fill
 * @return true if statistics retrieved successfully, false otherwise
 */
bool static_file_controller_get_stats(static_file_stats_t *stats);

/**
 * @brief Reset static file controller statistics
 * 
 * Resets all counters and statistics to zero.
 */
void static_file_controller_reset_stats(void);

/**
 * @brief Print static file controller status and statistics
 * 
 * Prints comprehensive static file controller information to console.
 */
void static_file_controller_print_status(void);

/**
 * @brief Get MIME type for a file extension
 * 
 * @param extension File extension (e.g., ".html", ".css", ".js")
 * @param mime_type Buffer to store MIME type
 * @param mime_type_size Size of mime_type buffer
 * @return true if MIME type found, false otherwise
 */
bool static_file_controller_get_mime_type(const char *extension, 
                                         char *mime_type, 
                                         size_t mime_type_size);

/**
 * @brief Check if a file type should be cached
 * 
 * @param extension File extension (e.g., ".html", ".css", ".js")
 * @return true if file type should be cached, false otherwise
 */
bool static_file_controller_is_cacheable(const char *extension);

/**
 * @brief Serve embedded content with proper headers
 * 
 * Helper function to serve embedded content (HTML, CSS, JS) with
 * appropriate MIME type and caching headers.
 * 
 * @param req HTTP request handle
 * @param content Content to serve
 * @param content_length Length of content
 * @param mime_type MIME type of content
 * @param cacheable Whether content should be cached
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t static_file_controller_serve_embedded(httpd_req_t *req,
                                               const char *content,
                                               size_t content_length,
                                               const char *mime_type,
                                               bool cacheable);

/**
 * @brief Configure advanced caching settings
 * 
 * @param config Caching configuration structure
 * @return true if configuration applied successfully, false otherwise
 */
bool static_file_controller_configure_cache(const cache_config_t *config);

/**
 * @brief Get current cache configuration
 * 
 * @param config Pointer to configuration structure to fill
 * @return true if configuration retrieved successfully, false otherwise
 */
bool static_file_controller_get_cache_config(cache_config_t *config);

/**
 * @brief Check if file extension is compressible
 * 
 * @param extension File extension (e.g., ".html", ".css", ".js")
 * @return true if file type can be compressed, false otherwise
 */
bool static_file_controller_is_compressible(const char *extension);

/**
 * @brief Get cache max age for file extension
 * 
 * @param extension File extension (e.g., ".html", ".css", ".js")
 * @return Cache max age in seconds, 0 if not cacheable
 */
uint32_t static_file_controller_get_cache_max_age(const char *extension);

/**
 * @brief Generate ETag for content
 * 
 * @param content Content data
 * @param content_length Length of content
 * @param etag Buffer to store ETag (minimum STATIC_FILE_ETAG_LENGTH)
 * @return true if ETag generated successfully, false otherwise
 */
bool static_file_controller_generate_etag(const char *content,
                                         size_t content_length,
                                         char *etag);

/**
 * @brief Check if request has matching ETag (conditional request)
 * 
 * @param req HTTP request handle
 * @param etag ETag to compare against
 * @return true if ETags match (304 Not Modified should be sent), false otherwise
 */
bool static_file_controller_check_etag_match(httpd_req_t *req, const char *etag);

/**
 * @brief Serve content with advanced caching support
 * 
 * Enhanced version of serve_embedded with ETag, conditional requests,
 * and compression support.
 * 
 * @param req HTTP request handle
 * @param filename File name for cache tracking
 * @param content Content to serve
 * @param content_length Length of content
 * @param mime_type MIME type of content
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t static_file_controller_serve_with_cache(httpd_req_t *req,
                                                 const char *filename,
                                                 const char *content,
                                                 size_t content_length,
                                                 const char *mime_type);

/**
 * @brief Get cache statistics
 * 
 * @param total_entries Pointer to store total cache entries
 * @param hit_rate Pointer to store cache hit rate (0.0-1.0)
 * @return true if statistics retrieved successfully, false otherwise
 */
bool static_file_controller_get_cache_stats(uint32_t *total_entries, float *hit_rate);

/**
 * @brief Clear cache entries
 * 
 * Clears all cache entries and resets cache statistics.
 */
void static_file_controller_clear_cache(void);

#ifdef __cplusplus
}
#endif

#endif /* STATIC_FILE_CONTROLLER_H */
