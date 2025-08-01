/**
 * @file io_manager.h
 * @brief IO Manager for SNRv9 Irrigation Control System
 * 
 * Central coordinator for all IO operations including GPIO, shift registers,
 * signal conditioning, and alarm monitoring.
 */

#ifndef IO_MANAGER_H
#define IO_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gpio_handler.h"
#include "shift_register_handler.h"
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of IO points supported
 */
#define IO_MANAGER_MAX_POINTS 32

/**
 * @brief IO Point Runtime State
 */
typedef struct {
    float raw_value;                    ///< Raw ADC/digital value
    float conditioned_value;            ///< Signal conditioned value
    bool digital_state;                 ///< Digital state (for binary points)
    bool error_state;                   ///< Error condition present
    uint64_t last_update_time;          ///< Last update timestamp (microseconds)
    uint32_t update_count;              ///< Number of updates
    uint32_t error_count;               ///< Number of errors
    
    // Signal conditioning state
    float sma_buffer[32];               ///< SMA filter buffer (increased to match config max)
    int sma_index;                      ///< Current SMA buffer index
    int sma_count;                      ///< Number of samples in SMA buffer
    float sma_sum;                      ///< Running sum for SMA calculation
    
    // Alarm state
    bool alarm_active;                  ///< Alarm currently active
    uint32_t alarm_count;               ///< Number of alarm activations
    uint64_t alarm_start_time;          ///< Alarm start timestamp
} io_point_runtime_state_t;

/**
 * @brief IO Manager Structure
 */
typedef struct {
    bool initialized;                                           ///< Initialization status
    
    // Hardware handlers
    gpio_handler_t gpio_handler;                               ///< GPIO handler
    shift_register_handler_t shift_register_handler;           ///< Shift register handler
    
    // Configuration
    config_manager_t* config_manager;                          ///< Configuration manager
    io_config_t current_config;                                ///< Current IO configuration
    
    // Runtime state
    io_point_runtime_state_t runtime_states[IO_MANAGER_MAX_POINTS]; ///< Runtime states
    int active_point_count;                                    ///< Number of active points
    char point_ids[IO_MANAGER_MAX_POINTS][CONFIG_MAX_ID_LENGTH]; ///< Point ID mapping
    
    // Thread safety
    SemaphoreHandle_t state_mutex;                             ///< State access mutex
    
    // Statistics
    uint32_t update_cycle_count;                               ///< Number of update cycles
    uint32_t total_error_count;                                ///< Total error count
    uint64_t last_update_time;                                 ///< Last update timestamp
    
    // Task management
    TaskHandle_t polling_task_handle;                          ///< Polling task handle
    bool polling_task_running;                                 ///< Polling task status
} io_manager_t;

/**
 * @brief Initialize IO Manager
 * 
 * @param manager Pointer to IO manager structure
 * @param config_manager Pointer to configuration manager
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_init(io_manager_t* manager, config_manager_t* config_manager);

/**
 * @brief Start IO polling task
 * 
 * @param manager Pointer to IO manager structure
 * @param polling_interval_ms Polling interval in milliseconds
 * @param task_priority Task priority
 * @param task_stack_size Task stack size
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_start_polling(io_manager_t* manager, uint32_t polling_interval_ms, 
                                  UBaseType_t task_priority, uint32_t task_stack_size);

/**
 * @brief Stop IO polling task
 * 
 * @param manager Pointer to IO manager structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_stop_polling(io_manager_t* manager);

/**
 * @brief Update all input points (manual update)
 * 
 * @param manager Pointer to IO manager structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_update_inputs(io_manager_t* manager);

/**
 * @brief Set binary output state
 * 
 * @param manager Pointer to IO manager structure
 * @param point_id IO point ID
 * @param state Desired state (true = ON, false = OFF)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_set_binary_output(io_manager_t* manager, const char* point_id, bool state);

/**
 * @brief Get binary output state
 * 
 * @param manager Pointer to IO manager structure
 * @param point_id IO point ID
 * @param state Pointer to store current state
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_get_binary_output(io_manager_t* manager, const char* point_id, bool* state);

/**
 * @brief Get binary input state
 * 
 * @param manager Pointer to IO manager structure
 * @param point_id IO point ID
 * @param state Pointer to store current state
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_get_binary_input(io_manager_t* manager, const char* point_id, bool* state);

/**
 * @brief Get analog input raw value
 * 
 * @param manager Pointer to IO manager structure
 * @param point_id IO point ID
 * @param value Pointer to store raw value
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_get_analog_raw(io_manager_t* manager, const char* point_id, float* value);

/**
 * @brief Get analog input conditioned value
 * 
 * @param manager Pointer to IO manager structure
 * @param point_id IO point ID
 * @param value Pointer to store conditioned value
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_get_analog_conditioned(io_manager_t* manager, const char* point_id, float* value);

/**
 * @brief Get IO point runtime state
 * 
 * @param manager Pointer to IO manager structure
 * @param point_id IO point ID
 * @param state Pointer to store runtime state
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_get_runtime_state(io_manager_t* manager, const char* point_id, io_point_runtime_state_t* state);

/**
 * @brief Get all active IO point IDs
 * 
 * @param manager Pointer to IO manager structure
 * @param point_ids Array to store point IDs
 * @param max_points Maximum number of points to return
 * @param actual_count Pointer to store actual number of points returned
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_get_all_point_ids(io_manager_t* manager, char point_ids[][CONFIG_MAX_ID_LENGTH], 
                                      int max_points, int* actual_count);

/**
 * @brief Reload configuration
 * 
 * @param manager Pointer to IO manager structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_reload_config(io_manager_t* manager);

/**
 * @brief Get IO manager statistics
 * 
 * @param manager Pointer to IO manager structure
 * @param update_cycles Pointer to store update cycle count (can be NULL)
 * @param total_errors Pointer to store total error count (can be NULL)
 * @param last_update_time Pointer to store last update time (can be NULL)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t io_manager_get_statistics(io_manager_t* manager, uint32_t* update_cycles, 
                                   uint32_t* total_errors, uint64_t* last_update_time);

/**
 * @brief Destroy IO manager and cleanup resources
 * 
 * @param manager Pointer to IO manager structure
 */
void io_manager_destroy(io_manager_t* manager);

#ifdef __cplusplus
}
#endif

#endif // IO_MANAGER_H
