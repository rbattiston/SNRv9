/**
 * @file io_test_controller.h
 * @brief IO Test Controller for SNRv9 Irrigation Control System
 * 
 * Provides web endpoints for testing and monitoring IO points.
 */

#ifndef IO_TEST_CONTROLLER_H
#define IO_TEST_CONTROLLER_H

#include <esp_err.h>
#include <esp_http_server.h>
#include "io_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize IO test controller
 * 
 * @param io_manager Pointer to IO manager instance
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_test_controller_init(io_manager_t* io_manager);

/**
 * @brief Register IO test routes with HTTP server
 * 
 * @param server HTTP server handle
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_test_controller_register_routes(httpd_handle_t server);

/**
 * @brief Get all IO points status
 * 
 * @param req HTTP request
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_test_get_all_points(httpd_req_t *req);

/**
 * @brief Get specific IO point status
 * 
 * @param req HTTP request
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_test_get_point(httpd_req_t *req);

/**
 * @brief Set binary output state
 * 
 * @param req HTTP request
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_test_set_output(httpd_req_t *req);

/**
 * @brief Get IO system statistics
 * 
 * @param req HTTP request
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_test_get_statistics(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif // IO_TEST_CONTROLLER_H
