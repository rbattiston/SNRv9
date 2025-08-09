/**
 * @file time_controller.h
 * @brief Time Management Controller header for SNRv9 Irrigation Control System
 */

#ifndef TIME_CONTROLLER_H
#define TIME_CONTROLLER_H

#include "esp_http_server.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * PUBLIC TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief Time controller status enumeration
 */
typedef enum {
    TIME_CONTROLLER_NOT_INITIALIZED = 0,
    TIME_CONTROLLER_INITIALIZED,
    TIME_CONTROLLER_ERROR
} time_controller_status_t;

/**
 * @brief Time controller statistics structure
 */
typedef struct {
    uint32_t endpoints_registered;
    uint32_t total_requests;
    uint32_t successful_requests;
    uint32_t failed_requests;
    uint32_t last_request_time;
} time_controller_stats_t;

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the time management controller
 * 
 * @param server_handle HTTP server handle
 * @return true if initialization successful, false otherwise
 */
bool time_controller_init(httpd_handle_t server_handle);

/**
 * @brief Get time controller statistics
 * 
 * @param stats Pointer to statistics structure to fill
 * @return true if statistics retrieved successfully, false otherwise
 */
bool time_controller_get_stats(time_controller_stats_t *stats);

/**
 * @brief Reset time controller statistics
 */
void time_controller_reset_stats(void);

/**
 * @brief Get time controller status
 * 
 * @return Current controller status
 */
time_controller_status_t time_controller_get_status(void);

/**
 * @brief Print time controller status to console
 */
void time_controller_print_status(void);

/* =============================================================================
 * API ENDPOINT HANDLERS
 * =============================================================================
 */

/**
 * @brief Handle GET /api/time/status requests
 * 
 * @param req HTTP request
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t time_status_handler(httpd_req_t *req);

/**
 * @brief Handle POST /api/time/ntp/config requests
 * 
 * @param req HTTP request
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ntp_config_handler(httpd_req_t *req);

/**
 * @brief Handle POST /api/time/ntp/sync requests
 * 
 * @param req HTTP request
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ntp_sync_handler(httpd_req_t *req);

/**
 * @brief Handle POST /api/time/manual requests
 * 
 * @param req HTTP request
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t manual_time_handler(httpd_req_t *req);

/**
 * @brief Handle GET /api/time/timezones requests
 * 
 * @param req HTTP request
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t timezones_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif // TIME_CONTROLLER_H
