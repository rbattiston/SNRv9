/**
 * @file config_manager.h
 * @brief Configuration Manager for SNRv9 Irrigation Control System
 * 
 * Manages loading, parsing, and validation of IO configuration from JSON files.
 * Provides thread-safe access to configuration data for the IO system.
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum string lengths for configuration
 */
#define CONFIG_MAX_ID_LENGTH 32
#define CONFIG_MAX_NAME_LENGTH 64
#define CONFIG_MAX_DESCRIPTION_LENGTH 128
#define CONFIG_MAX_UNITS_LENGTH 16
#define CONFIG_MAX_NOTES_LENGTH 256

/**
 * @brief Maximum number of IO points supported
 */
#define CONFIG_MAX_IO_POINTS 32

/**
 * @brief Maximum number of lookup table entries
 */
#define CONFIG_MAX_LOOKUP_ENTRIES 16

/**
 * @brief IO Point Types
 */
typedef enum {
    IO_POINT_TYPE_GPIO_AI = 0,      ///< GPIO Analog Input
    IO_POINT_TYPE_GPIO_BI,          ///< GPIO Binary Input
    IO_POINT_TYPE_GPIO_BO,          ///< GPIO Binary Output
    IO_POINT_TYPE_SHIFT_REG_BI,     ///< Shift Register Binary Input
    IO_POINT_TYPE_SHIFT_REG_BO      ///< Shift Register Binary Output
} io_point_type_t;

/**
 * @brief Binary Output Types
 */
typedef enum {
    BO_TYPE_SOLENOID = 0,           ///< Irrigation solenoid valve
    BO_TYPE_LIGHTING,               ///< Grow light control
    BO_TYPE_PUMP,                   ///< Water pump control
    BO_TYPE_FAN,                    ///< Ventilation fan
    BO_TYPE_HEATER,                 ///< Heating element
    BO_TYPE_GENERIC                 ///< Generic binary output
} bo_type_t;

/**
 * @brief Signal Filter Types
 */
typedef enum {
    SIGNAL_FILTER_NONE = 0,         ///< No filtering
    SIGNAL_FILTER_SMA               ///< Simple Moving Average
} signal_filter_type_t;

/**
 * @brief Lookup Table Entry
 */
typedef struct {
    float input;                    ///< Input value
    float output;                   ///< Output value
} lookup_table_entry_t;

/**
 * @brief Signal Configuration
 */
typedef struct {
    bool enabled;                                           ///< Enable signal conditioning
    signal_filter_type_t filter_type;                      ///< Filter type
    float gain;                                             ///< Gain multiplier
    float offset;                                           ///< Offset value
    float scaling_factor;                                   ///< Scaling factor
    int sma_window_size;                                    ///< SMA window size
    int precision_digits;                                   ///< Precision digits
    char units[CONFIG_MAX_UNITS_LENGTH];                    ///< Engineering units
    int history_buffer_size;                                ///< History buffer size
    bool lookup_table_enabled;                             ///< Enable lookup table
    int lookup_table_count;                                 ///< Number of lookup entries
    lookup_table_entry_t lookup_table[CONFIG_MAX_LOOKUP_ENTRIES]; ///< Lookup table
} signal_config_t;

/**
 * @brief Alarm Rules Configuration
 */
typedef struct {
    bool check_rate_of_change;                             ///< Enable rate of change alarm
    float rate_of_change_threshold;                        ///< Rate of change threshold
    bool check_disconnected;                               ///< Enable disconnected alarm
    float disconnected_threshold;                          ///< Disconnected threshold
    bool check_max_value;                                  ///< Enable max value alarm
    float max_value_threshold;                             ///< Max value threshold
    bool check_stuck_signal;                               ///< Enable stuck signal alarm
    int stuck_signal_window_samples;                       ///< Stuck signal window
    float stuck_signal_delta_threshold;                    ///< Stuck signal delta threshold
    int alarm_persistence_samples;                         ///< Alarm persistence samples
    float alarm_clear_hysteresis_value;                    ///< Alarm clear hysteresis
    bool requires_manual_reset;                            ///< Requires manual reset
    int samples_to_clear_alarm_condition;                  ///< Samples to clear alarm
    int consecutive_good_samples_to_restore_trust;         ///< Samples to restore trust
} alarm_rules_t;

/**
 * @brief Alarm Configuration
 */
typedef struct {
    bool enabled;                                          ///< Enable alarm system
    int history_samples_for_analysis;                     ///< History samples for analysis
    alarm_rules_t rules;                                   ///< Alarm rules
} alarm_config_t;

/**
 * @brief IO Point Configuration
 */
typedef struct {
    char id[CONFIG_MAX_ID_LENGTH];                         ///< Unique identifier
    char name[CONFIG_MAX_NAME_LENGTH];                     ///< Display name
    char description[CONFIG_MAX_DESCRIPTION_LENGTH];       ///< Description
    io_point_type_t type;                                  ///< IO point type
    int pin;                                               ///< GPIO pin number (for GPIO types)
    int chip_index;                                        ///< Chip index (for shift register types)
    int bit_index;                                         ///< Bit index (for shift register types)
    bool is_inverted;                                      ///< Invert logic
    float range_min;                                       ///< Minimum range value (AI only)
    float range_max;                                       ///< Maximum range value (AI only)
    
    // Binary Output specific configuration
    bo_type_t bo_type;                                     ///< Binary output type
    float lph_per_emitter_flow;                            ///< Liters per hour per emitter
    int num_emitters_per_plant;                            ///< Number of emitters per plant
    float ml_h2o_per_second_per_plant;                     ///< ML water per second per plant
    char autopilot_sensor_id[CONFIG_MAX_ID_LENGTH];        ///< AutoPilot sensor ID
    float flow_rate_ml_per_second;                         ///< Flow rate in ML per second
    bool is_calibrated;                                    ///< Calibration status
    char calibration_notes[CONFIG_MAX_NOTES_LENGTH];       ///< Calibration notes
    uint64_t calibration_date;                             ///< Calibration date (timestamp)
    bool enable_schedule_execution;                        ///< Enable schedule execution
    bool persist_scheduled_state_on_reboot;                ///< Persist state on reboot
    bool allow_manual_override;                            ///< Allow manual override
    int manual_override_timeout;                           ///< Manual override timeout (seconds)
    
    signal_config_t signal_config;                         ///< Signal conditioning config
    alarm_config_t alarm_config;                           ///< Alarm configuration
} io_point_config_t;

/**
 * @brief Shift Register Configuration
 */
typedef struct {
    int output_clock_pin;                                  ///< Output clock pin
    int output_latch_pin;                                  ///< Output latch pin
    int output_data_pin;                                   ///< Output data pin
    int output_enable_pin;                                 ///< Output enable pin (-1 to disable)
    int input_clock_pin;                                   ///< Input clock pin
    int input_load_pin;                                    ///< Input load pin
    int input_data_pin;                                    ///< Input data pin
    int num_output_registers;                              ///< Number of output registers
    int num_input_registers;                               ///< Number of input registers
} shift_register_config_t;

/**
 * @brief Complete IO Configuration
 */
typedef struct {
    shift_register_config_t shift_register_config;         ///< Shift register configuration
    int io_point_count;                                    ///< Number of IO points
    io_point_config_t io_points[CONFIG_MAX_IO_POINTS];     ///< IO point configurations
} io_config_t;

/**
 * @brief Configuration Manager Structure
 */
typedef struct {
    bool initialized;                                      ///< Initialization status
    io_config_t config;                                    ///< Current configuration
    char config_file_path[256];                            ///< Configuration file path
    uint32_t load_count;                                   ///< Number of loads
    uint32_t save_count;                                   ///< Number of saves
    uint32_t error_count;                                  ///< Number of errors
} config_manager_t;

/**
 * @brief Initialize configuration manager
 * 
 * @param manager Pointer to configuration manager structure
 * @param config_file_path Path to configuration file
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t config_manager_init(config_manager_t* manager, const char* config_file_path);

/**
 * @brief Load configuration from file
 * 
 * @param manager Pointer to configuration manager structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t config_manager_load(config_manager_t* manager);

/**
 * @brief Save configuration to file
 * 
 * @param manager Pointer to configuration manager structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t config_manager_save(config_manager_t* manager);

/**
 * @brief Get shift register configuration
 * 
 * @param manager Pointer to configuration manager structure
 * @param config Pointer to store shift register configuration
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t config_manager_get_shift_register_config(config_manager_t* manager, shift_register_config_t* config);

/**
 * @brief Get IO point configuration by ID
 * 
 * @param manager Pointer to configuration manager structure
 * @param id IO point ID
 * @param config Pointer to store IO point configuration
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t config_manager_get_io_point_config(config_manager_t* manager, const char* id, io_point_config_t* config);

/**
 * @brief Get all IO point configurations
 * 
 * @param manager Pointer to configuration manager structure
 * @param configs Array to store IO point configurations
 * @param max_configs Maximum number of configurations to return
 * @param actual_count Pointer to store actual number of configurations returned
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t config_manager_get_all_io_points(config_manager_t* manager, io_point_config_t* configs, int max_configs, int* actual_count);

/**
 * @brief Update IO point configuration
 * 
 * @param manager Pointer to configuration manager structure
 * @param config Pointer to IO point configuration to update
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t config_manager_update_io_point(config_manager_t* manager, const io_point_config_t* config);

/**
 * @brief Validate configuration
 * 
 * @param manager Pointer to configuration manager structure
 * @return esp_err_t ESP_OK if valid, error code if invalid
 */
esp_err_t config_manager_validate(config_manager_t* manager);

/**
 * @brief Get configuration manager statistics
 * 
 * @param manager Pointer to configuration manager structure
 * @param loads Pointer to store load count (can be NULL)
 * @param saves Pointer to store save count (can be NULL)
 * @param errors Pointer to store error count (can be NULL)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t config_manager_get_statistics(config_manager_t* manager, uint32_t* loads, uint32_t* saves, uint32_t* errors);

/**
 * @brief Destroy configuration manager and cleanup resources
 * 
 * @param manager Pointer to configuration manager structure
 */
void config_manager_destroy(config_manager_t* manager);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H
