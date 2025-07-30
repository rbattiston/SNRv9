/**
 * @file web_server_manager.h
 * @brief Web Server Manager for SNRv9 Irrigation Control System
 * 
 * This module provides HTTP server functionality using ESP-IDF's official
 * esp_http_server component for maximum reliability and long-term support.
 */

#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_http_server.h"
#include "static_file_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * PUBLIC CONSTANTS AND MACROS
 * =============================================================================
 */

#define WEB_SERVER_DEFAULT_PORT         80
#define WEB_SERVER_MAX_URI_HANDLERS     20
#define WEB_SERVER_MAX_OPEN_SOCKETS     7
#define WEB_SERVER_TASK_STACK_SIZE      4096
#define WEB_SERVER_TASK_PRIORITY        1
#define WEB_SERVER_TASK_NAME            "web_server"

/* =============================================================================
 * PUBLIC TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief Web server status enumeration
 */
typedef enum {
    WEB_SERVER_STOPPED = 0,     ///< Server is stopped
    WEB_SERVER_STARTING,        ///< Server is starting up
    WEB_SERVER_RUNNING,         ///< Server is running normally
    WEB_SERVER_STOPPING,       ///< Server is shutting down
    WEB_SERVER_ERROR           ///< Server encountered an error
} web_server_status_t;

/**
 * @brief Web server statistics structure
 */
typedef struct {
    uint32_t total_requests;        ///< Total requests served
    uint32_t successful_requests;   ///< Successful requests (2xx responses)
    uint32_t failed_requests;       ///< Failed requests (4xx/5xx responses)
    uint32_t active_connections;    ///< Currently active connections
    uint32_t max_connections_seen;  ///< Maximum concurrent connections seen
    uint32_t uptime_seconds;        ///< Server uptime in seconds
    uint32_t last_request_time;     ///< Timestamp of last request (ms)
} web_server_stats_t;

/**
 * @brief Web server configuration structure
 */
typedef struct {
    uint16_t port;                  ///< Server port (default: 80)
    uint16_t max_uri_handlers;      ///< Maximum URI handlers
    uint16_t max_open_sockets;      ///< Maximum open sockets
    uint32_t task_stack_size;       ///< Server task stack size
    uint8_t task_priority;          ///< Server task priority
    bool enable_cors;               ///< Enable CORS headers
    bool enable_logging;            ///< Enable request logging
} web_server_config_t;

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the web server manager
 * 
 * This function initializes the web server manager with default configuration.
 * Must be called before any other web server functions.
 * 
 * @return true if initialization successful, false otherwise
 */
bool web_server_manager_init(void);

/**
 * @brief Initialize the web server manager with custom configuration
 * 
 * @param config Pointer to configuration structure
 * @return true if initialization successful, false otherwise
 */
bool web_server_manager_init_with_config(const web_server_config_t *config);

/**
 * @brief Start the web server
 * 
 * This function starts the HTTP server and begins listening for connections.
 * The server will be started in a separate FreeRTOS task.
 * 
 * @return true if server started successfully, false otherwise
 */
bool web_server_manager_start(void);

/**
 * @brief Stop the web server
 * 
 * This function stops the HTTP server and cleans up resources.
 * 
 * @return true if server stopped successfully, false otherwise
 */
bool web_server_manager_stop(void);

/**
 * @brief Get current web server status
 * 
 * @return Current server status
 */
web_server_status_t web_server_manager_get_status(void);

/**
 * @brief Get web server statistics
 * 
 * @param stats Pointer to statistics structure to fill
 * @return true if statistics retrieved successfully, false otherwise
 */
bool web_server_manager_get_stats(web_server_stats_t *stats);

/**
 * @brief Reset web server statistics
 * 
 * Resets all counters and statistics to zero.
 */
void web_server_manager_reset_stats(void);

/**
 * @brief Check if web server is running
 * 
 * @return true if server is running, false otherwise
 */
bool web_server_manager_is_running(void);

/**
 * @brief Get server handle for advanced operations
 * 
 * This function returns the internal server handle for advanced
 * operations like adding custom URI handlers.
 * 
 * @return Server handle or NULL if server not running
 */
httpd_handle_t web_server_manager_get_handle(void);

/**
 * @brief Print server status and statistics
 * 
 * Prints comprehensive server information to console.
 */
void web_server_manager_print_status(void);

/**
 * @brief Get default web server configuration
 * 
 * @param config Pointer to configuration structure to fill with defaults
 */
void web_server_manager_get_default_config(web_server_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVER_MANAGER_H */
