/**
 * @file alarm_manager.h
 * @brief Alarm Management System for SNRv9 Irrigation Control System
 * 
 * Provides comprehensive alarm monitoring for analog inputs including
 * rate of change, disconnection, stuck signal, and max value detection.
 */

#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Alarm types supported by the system
 */
typedef enum {
    ALARM_TYPE_RATE_OF_CHANGE = 0,      ///< Rapid signal change detection
    ALARM_TYPE_DISCONNECTED,            ///< Sensor disconnection detection
    ALARM_TYPE_MAX_VALUE,               ///< Over-range detection
    ALARM_TYPE_STUCK_SIGNAL,            ///< Unchanging signal detection
    ALARM_TYPE_COUNT                    ///< Number of alarm types
} alarm_type_t;

/**
 * @brief Alarm state for a single point
 */
typedef struct {
    bool active[ALARM_TYPE_COUNT];              ///< Active alarm flags
    uint32_t activation_count[ALARM_TYPE_COUNT]; ///< Number of activations
    uint64_t activation_time[ALARM_TYPE_COUNT];  ///< Activation timestamps
    uint32_t persistence_count[ALARM_TYPE_COUNT]; ///< Persistence counters
    uint32_t clear_count[ALARM_TYPE_COUNT];      ///< Clear condition counters
    uint32_t good_samples_count;                 ///< Consecutive good samples
    bool trust_restored;                         ///< Trust status after alarm
    float last_values[20];                       ///< History buffer for analysis
    int history_index;                           ///< Current history index
    int history_count;                           ///< Number of samples in history
} alarm_state_t;

/**
 * @brief Alarm Manager Structure
 */
typedef struct {
    bool initialized;                           ///< Initialization status
    
    // Configuration
    config_manager_t* config_manager;          ///< Configuration manager
    
    // Alarm states for all points
    alarm_state_t point_alarms[CONFIG_MAX_IO_POINTS]; ///< Alarm states
    char point_ids[CONFIG_MAX_IO_POINTS][CONFIG_MAX_ID_LENGTH]; ///< Point ID mapping
    int active_point_count;                     ///< Number of monitored points
    
    // Thread safety
    SemaphoreHandle_t alarm_mutex;              ///< Alarm state mutex
    
    // Statistics
    uint32_t total_alarm_count;                 ///< Total alarms triggered
    uint32_t check_cycle_count;                 ///< Number of check cycles
    uint64_t last_check_time;                   ///< Last check timestamp
    
    // Task management
    TaskHandle_t alarm_task_handle;             ///< Alarm task handle
    bool alarm_task_running;                    ///< Alarm task status
} alarm_manager_t;

/**
 * @brief Initialize Alarm Manager
 * 
 * @param manager Pointer to alarm manager structure
 * @param config_manager Pointer to configuration manager
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t alarm_manager_init(alarm_manager_t* manager, config_manager_t* config_manager);

/**
 * @brief Start alarm monitoring task
 * 
 * @param manager Pointer to alarm manager structure
 * @param check_interval_ms Alarm check interval in milliseconds
 * @param task_priority Task priority
 * @param task_stack_size Task stack size
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t alarm_manager_start_monitoring(alarm_manager_t* manager, uint32_t check_interval_ms,
                                        UBaseType_t task_priority, uint32_t task_stack_size);

/**
 * @brief Stop alarm monitoring task
 * 
 * @param manager Pointer to alarm manager structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t alarm_manager_stop_monitoring(alarm_manager_t* manager);

/**
 * @brief Update alarm analysis with new analog value
 * 
 * @param manager Pointer to alarm manager structure
 * @param point_id IO point ID
 * @param conditioned_value New conditioned analog value
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t alarm_manager_update_value(alarm_manager_t* manager, const char* point_id, float conditioned_value);

/**
 * @brief Check all alarm conditions for a point
 * 
 * @param manager Pointer to alarm manager structure
 * @param point_id IO point ID
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t alarm_manager_check_point(alarm_manager_t* manager, const char* point_id);

/**
 * @brief Get alarm status for a point
 * 
 * @param manager Pointer to alarm manager structure
 * @param point_id IO point ID
 * @param alarm_type Type of alarm to check
 * @param is_active Pointer to store alarm status
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t alarm_manager_get_alarm_status(alarm_manager_t* manager, const char* point_id, 
                                        alarm_type_t alarm_type, bool* is_active);

/**
 * @brief Get all active alarms for a point
 * 
 * @param manager Pointer to alarm manager structure
 * @param point_id IO point ID
 * @param active_alarms Array to store active alarm flags
 * @param alarm_count Number of alarm types (should be ALARM_TYPE_COUNT)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t alarm_manager_get_all_alarms(alarm_manager_t* manager, const char* point_id,
                                      bool* active_alarms, int alarm_count);

/**
 * @brief Acknowledge alarm (for manual reset types)
 * 
 * @param manager Pointer to alarm manager structure
 * @param point_id IO point ID
 * @param alarm_type Type of alarm to acknowledge
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t alarm_manager_acknowledge_alarm(alarm_manager_t* manager, const char* point_id, alarm_type_t alarm_type);

/**
 * @brief Get alarm statistics
 * 
 * @param manager Pointer to alarm manager structure
 * @param total_alarms Pointer to store total alarm count (can be NULL)
 * @param check_cycles Pointer to store check cycle count (can be NULL)
 * @param last_check_time Pointer to store last check time (can be NULL)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t alarm_manager_get_statistics(alarm_manager_t* manager, uint32_t* total_alarms,
                                      uint32_t* check_cycles, uint64_t* last_check_time);

/**
 * @brief Reload alarm configuration
 * 
 * @param manager Pointer to alarm manager structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t alarm_manager_reload_config(alarm_manager_t* manager);

/**
 * @brief Destroy alarm manager and cleanup resources
 * 
 * @param manager Pointer to alarm manager structure
 */
void alarm_manager_destroy(alarm_manager_t* manager);

#ifdef __cplusplus
}
#endif

#endif // ALARM_MANAGER_H
