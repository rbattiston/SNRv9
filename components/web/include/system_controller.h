/**
 * @file system_controller.h
 * @brief System Controller for SNRv9 Irrigation Control System
 * 
 * Provides REST API endpoints for system monitoring data including:
 * - System status and information
 * - Memory monitoring statistics
 * - Task tracking information
 * - WiFi status and statistics
 * - Authentication system statistics
 * - Real-time combined data for dashboards
 */

#ifndef SYSTEM_CONTROLLER_H
#define SYSTEM_CONTROLLER_H

#include <stdbool.h>
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * PUBLIC CONSTANTS
 * =============================================================================
 */

#define SYSTEM_CONTROLLER_MAX_RESPONSE_SIZE    4096
#define SYSTEM_CONTROLLER_MAX_ENDPOINTS        10

/* =============================================================================
 * PUBLIC TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief System controller initialization status
 */
typedef enum {
    SYSTEM_CONTROLLER_NOT_INITIALIZED = 0,
    SYSTEM_CONTROLLER_INITIALIZED,
    SYSTEM_CONTROLLER_ERROR
} system_controller_status_t;

/**
 * @brief System controller statistics
 */
typedef struct {
    uint32_t total_requests;           ///< Total API requests served
    uint32_t successful_requests;      ///< Successful API requests
    uint32_t failed_requests;          ///< Failed API requests
    uint32_t last_request_time;        ///< Timestamp of last request
    uint32_t endpoints_registered;     ///< Number of registered endpoints
} system_controller_stats_t;

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the system controller
 * 
 * Registers all system monitoring API endpoints with the HTTP server.
 * Must be called after the HTTP server is started.
 * 
 * @param server_handle HTTP server handle
 * @return true if initialization successful, false otherwise
 */
bool system_controller_init(httpd_handle_t server_handle);

/**
 * @brief Get system controller statistics
 * 
 * @param stats Pointer to statistics structure to fill
 * @return true if statistics retrieved successfully, false otherwise
 */
bool system_controller_get_stats(system_controller_stats_t *stats);

/**
 * @brief Reset system controller statistics
 */
void system_controller_reset_stats(void);

/**
 * @brief Get system controller status
 * 
 * @return Current controller status
 */
system_controller_status_t system_controller_get_status(void);

/**
 * @brief Print system controller status and statistics
 */
void system_controller_print_status(void);

/* =============================================================================
 * API ENDPOINT HANDLERS (Internal use)
 * =============================================================================
 */

/**
 * @brief Handle GET /api/system/status requests
 * 
 * Returns overall system health summary including:
 * - System uptime and basic info
 * - Memory usage summary
 * - Task count and status
 * - WiFi connection status
 * - Authentication system status
 * 
 * @param req HTTP request handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t system_status_handler(httpd_req_t *req);

/**
 * @brief Handle GET /api/system/info requests
 * 
 * Returns detailed system information including:
 * - ESP32 chip information
 * - Firmware version and build info
 * - System uptime and timestamps
 * - Hardware capabilities
 * 
 * @param req HTTP request handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t system_info_handler(httpd_req_t *req);

/**
 * @brief Handle GET /api/system/memory requests
 * 
 * Returns detailed memory monitoring data including:
 * - Free heap and minimum free heap
 * - Memory usage percentage
 * - Fragmentation statistics
 * - Memory allocation trends
 * 
 * @param req HTTP request handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t system_memory_handler(httpd_req_t *req);

/**
 * @brief Handle GET /api/system/tasks requests
 * 
 * Returns detailed task information including:
 * - All running tasks with priorities
 * - Stack usage for each task
 * - Task states and runtime statistics
 * - Stack warnings and analysis
 * 
 * @param req HTTP request handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t system_tasks_handler(httpd_req_t *req);

/**
 * @brief Handle GET /api/system/wifi requests
 * 
 * Returns WiFi status and statistics including:
 * - Connection status and IP address
 * - Signal strength and network info
 * - Connection statistics and history
 * - WiFi handler status
 * 
 * @param req HTTP request handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t system_wifi_handler(httpd_req_t *req);

/**
 * @brief Handle GET /api/system/auth requests
 * 
 * Returns authentication system statistics including:
 * - Login attempt statistics
 * - Active session information
 * - Rate limiting statistics
 * - User management status
 * 
 * @param req HTTP request handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t system_auth_handler(httpd_req_t *req);

/**
 * @brief Handle GET /api/system/live requests
 * 
 * Returns combined real-time data for dashboard updates including:
 * - Key system metrics
 * - Current status indicators
 * - Performance counters
 * - Health indicators
 * 
 * @param req HTTP request handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t system_live_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_CONTROLLER_H
