/**
 * @file config_manager.c
 * @brief Configuration Manager implementation for SNRv9 Irrigation Control System
 */

#include "config_manager.h"
#include "storage_manager.h"
#include "debug_config.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = DEBUG_CONFIG_MANAGER_TAG;

/**
 * @brief Convert string to IO point type
 */
static io_point_type_t string_to_io_point_type(const char* str) {
    if (strcmp(str, "GPIO_AI") == 0) return IO_POINT_TYPE_GPIO_AI;
    if (strcmp(str, "GPIO_BI") == 0) return IO_POINT_TYPE_GPIO_BI;
    if (strcmp(str, "GPIO_BO") == 0) return IO_POINT_TYPE_GPIO_BO;
    if (strcmp(str, "SHIFT_REG_BI") == 0) return IO_POINT_TYPE_SHIFT_REG_BI;
    if (strcmp(str, "SHIFT_REG_BO") == 0) return IO_POINT_TYPE_SHIFT_REG_BO;
    return IO_POINT_TYPE_GPIO_AI; // Default
}

/**
 * @brief Convert string to BO type
 */
static bo_type_t string_to_bo_type(const char* str) {
    if (strcmp(str, "SOLENOID") == 0) return BO_TYPE_SOLENOID;
    if (strcmp(str, "LIGHTING") == 0) return BO_TYPE_LIGHTING;
    if (strcmp(str, "PUMP") == 0) return BO_TYPE_PUMP;
    if (strcmp(str, "FAN") == 0) return BO_TYPE_FAN;
    if (strcmp(str, "HEATER") == 0) return BO_TYPE_HEATER;
    return BO_TYPE_GENERIC; // Default
}

/**
 * @brief Convert string to signal filter type
 */
static signal_filter_type_t string_to_filter_type(const char* str) {
    if (strcmp(str, "SMA") == 0) return SIGNAL_FILTER_SMA;
    return SIGNAL_FILTER_NONE; // Default
}

/**
 * @brief Parse shift register configuration from JSON
 */
static esp_err_t parse_shift_register_config(cJSON* json, shift_register_config_t* config) {
    cJSON* sr_config = cJSON_GetObjectItem(json, "shiftRegisterConfig");
    if (!sr_config) {
        return ESP_ERR_NOT_FOUND;
    }
    
    cJSON* item;
    
    item = cJSON_GetObjectItem(sr_config, "outputClockPin");
    config->output_clock_pin = item ? item->valueint : -1;
    
    item = cJSON_GetObjectItem(sr_config, "outputLatchPin");
    config->output_latch_pin = item ? item->valueint : -1;
    
    item = cJSON_GetObjectItem(sr_config, "outputDataPin");
    config->output_data_pin = item ? item->valueint : -1;
    
    item = cJSON_GetObjectItem(sr_config, "outputEnablePin");
    config->output_enable_pin = item ? item->valueint : -1;
    
    item = cJSON_GetObjectItem(sr_config, "inputClockPin");
    config->input_clock_pin = item ? item->valueint : -1;
    
    item = cJSON_GetObjectItem(sr_config, "inputLoadPin");
    config->input_load_pin = item ? item->valueint : -1;
    
    item = cJSON_GetObjectItem(sr_config, "inputDataPin");
    config->input_data_pin = item ? item->valueint : -1;
    
    item = cJSON_GetObjectItem(sr_config, "numOutputRegisters");
    config->num_output_registers = item ? item->valueint : 0;
    
    item = cJSON_GetObjectItem(sr_config, "numInputRegisters");
    config->num_input_registers = item ? item->valueint : 0;
    
    return ESP_OK;
}

/**
 * @brief Parse signal configuration from JSON
 */
static void parse_signal_config(cJSON* json, signal_config_t* config) {
    cJSON* item;
    
    item = cJSON_GetObjectItem(json, "enabled");
    config->enabled = item ? cJSON_IsTrue(item) : false;
    
    item = cJSON_GetObjectItem(json, "filterType");
    config->filter_type = item ? string_to_filter_type(item->valuestring) : SIGNAL_FILTER_NONE;
    
    item = cJSON_GetObjectItem(json, "gain");
    config->gain = item ? (float)item->valuedouble : 1.0f;
    
    item = cJSON_GetObjectItem(json, "offset");
    config->offset = item ? (float)item->valuedouble : 0.0f;
    
    item = cJSON_GetObjectItem(json, "scalingFactor");
    config->scaling_factor = item ? (float)item->valuedouble : 1.0f;
    
    item = cJSON_GetObjectItem(json, "smaWindowSize");
    config->sma_window_size = item ? item->valueint : 5;
    
    item = cJSON_GetObjectItem(json, "precisionDigits");
    config->precision_digits = item ? item->valueint : 2;
    
    item = cJSON_GetObjectItem(json, "units");
    if (item && item->valuestring) {
        strncpy(config->units, item->valuestring, CONFIG_MAX_UNITS_LENGTH - 1);
        config->units[CONFIG_MAX_UNITS_LENGTH - 1] = '\0';
    } else {
        config->units[0] = '\0';
    }
    
    item = cJSON_GetObjectItem(json, "historyBufferSize");
    config->history_buffer_size = item ? item->valueint : 100;
    
    item = cJSON_GetObjectItem(json, "lookupTableEnabled");
    config->lookup_table_enabled = item ? cJSON_IsTrue(item) : false;
    
    // Initialize lookup table count to 0
    config->lookup_table_count = 0;
}

/**
 * @brief Parse alarm configuration from JSON
 */
static void parse_alarm_config(cJSON* json, alarm_config_t* config) {
    cJSON* item;
    
    item = cJSON_GetObjectItem(json, "enabled");
    config->enabled = item ? cJSON_IsTrue(item) : false;
    
    item = cJSON_GetObjectItem(json, "historySamplesForAnalysis");
    config->history_samples_for_analysis = item ? item->valueint : 20;
    
    // Parse alarm rules
    cJSON* rules = cJSON_GetObjectItem(json, "rules");
    if (rules) {
        alarm_rules_t* r = &config->rules;
        
        item = cJSON_GetObjectItem(rules, "checkRateOfChange");
        r->check_rate_of_change = item ? cJSON_IsTrue(item) : false;
        
        item = cJSON_GetObjectItem(rules, "rateOfChangeThreshold");
        r->rate_of_change_threshold = item ? (float)item->valuedouble : 50.0f;
        
        item = cJSON_GetObjectItem(rules, "checkDisconnected");
        r->check_disconnected = item ? cJSON_IsTrue(item) : false;
        
        item = cJSON_GetObjectItem(rules, "disconnectedThreshold");
        r->disconnected_threshold = item ? (float)item->valuedouble : 0.5f;
        
        item = cJSON_GetObjectItem(rules, "checkMaxValue");
        r->check_max_value = item ? cJSON_IsTrue(item) : false;
        
        item = cJSON_GetObjectItem(rules, "maxValueThreshold");
        r->max_value_threshold = item ? (float)item->valuedouble : 4090.0f;
        
        item = cJSON_GetObjectItem(rules, "checkStuckSignal");
        r->check_stuck_signal = item ? cJSON_IsTrue(item) : false;
        
        item = cJSON_GetObjectItem(rules, "stuckSignalWindowSamples");
        r->stuck_signal_window_samples = item ? item->valueint : 10;
        
        item = cJSON_GetObjectItem(rules, "stuckSignalDeltaThreshold");
        r->stuck_signal_delta_threshold = item ? (float)item->valuedouble : 1.0f;
        
        item = cJSON_GetObjectItem(rules, "alarmPersistenceSamples");
        r->alarm_persistence_samples = item ? item->valueint : 1;
        
        item = cJSON_GetObjectItem(rules, "alarmClearHysteresisValue");
        r->alarm_clear_hysteresis_value = item ? (float)item->valuedouble : 5.0f;
        
        item = cJSON_GetObjectItem(rules, "requiresManualReset");
        r->requires_manual_reset = item ? cJSON_IsTrue(item) : false;
        
        item = cJSON_GetObjectItem(rules, "samplesToClearAlarmCondition");
        r->samples_to_clear_alarm_condition = item ? item->valueint : 3;
        
        item = cJSON_GetObjectItem(rules, "consecutiveGoodSamplesToRestoreTrust");
        r->consecutive_good_samples_to_restore_trust = item ? item->valueint : 5;
    }
}

/**
 * @brief Parse IO point configuration from JSON
 */
static esp_err_t parse_io_point(cJSON* json, io_point_config_t* config) {
    cJSON* item;
    
    // Clear the configuration
    memset(config, 0, sizeof(io_point_config_t));
    
    // Required fields
    item = cJSON_GetObjectItem(json, "id");
    if (!item || !item->valuestring) return ESP_ERR_INVALID_ARG;
    strncpy(config->id, item->valuestring, CONFIG_MAX_ID_LENGTH - 1);
    
    item = cJSON_GetObjectItem(json, "type");
    if (!item || !item->valuestring) return ESP_ERR_INVALID_ARG;
    config->type = string_to_io_point_type(item->valuestring);
    
    // Optional fields
    item = cJSON_GetObjectItem(json, "name");
    if (item && item->valuestring) {
        strncpy(config->name, item->valuestring, CONFIG_MAX_NAME_LENGTH - 1);
    }
    
    item = cJSON_GetObjectItem(json, "description");
    if (item && item->valuestring) {
        strncpy(config->description, item->valuestring, CONFIG_MAX_DESCRIPTION_LENGTH - 1);
    }
    
    item = cJSON_GetObjectItem(json, "pin");
    config->pin = item ? item->valueint : -1;
    
    item = cJSON_GetObjectItem(json, "chipIndex");
    config->chip_index = item ? item->valueint : 0;
    
    item = cJSON_GetObjectItem(json, "bitIndex");
    config->bit_index = item ? item->valueint : 0;
    
    item = cJSON_GetObjectItem(json, "isInverted");
    config->is_inverted = item ? cJSON_IsTrue(item) : false;
    
    item = cJSON_GetObjectItem(json, "rangeMin");
    config->range_min = item ? (float)item->valuedouble : 0.0f;
    
    item = cJSON_GetObjectItem(json, "rangeMax");
    config->range_max = item ? (float)item->valuedouble : 100.0f;
    
    // Binary Output specific fields
    item = cJSON_GetObjectItem(json, "boType");
    config->bo_type = item ? string_to_bo_type(item->valuestring) : BO_TYPE_GENERIC;
    
    item = cJSON_GetObjectItem(json, "lphPerEmitterFlow");
    config->lph_per_emitter_flow = item ? (float)item->valuedouble : 0.0f;
    
    item = cJSON_GetObjectItem(json, "numEmittersPerPlant");
    config->num_emitters_per_plant = item ? item->valueint : 0;
    
    item = cJSON_GetObjectItem(json, "flowRateMLPerSecond");
    config->flow_rate_ml_per_second = item ? (float)item->valuedouble : 0.0f;
    
    item = cJSON_GetObjectItem(json, "isCalibrated");
    config->is_calibrated = item ? cJSON_IsTrue(item) : false;
    
    item = cJSON_GetObjectItem(json, "enableScheduleExecution");
    config->enable_schedule_execution = item ? cJSON_IsTrue(item) : true;
    
    item = cJSON_GetObjectItem(json, "allowManualOverride");
    config->allow_manual_override = item ? cJSON_IsTrue(item) : true;
    
    item = cJSON_GetObjectItem(json, "manualOverrideTimeout");
    config->manual_override_timeout = item ? item->valueint : 3600;
    
    // Parse signal configuration
    cJSON* signal_config = cJSON_GetObjectItem(json, "signalConfig");
    if (signal_config) {
        parse_signal_config(signal_config, &config->signal_config);
    }
    
    // Parse alarm configuration
    cJSON* alarm_config = cJSON_GetObjectItem(json, "alarmConfig");
    if (alarm_config) {
        parse_alarm_config(alarm_config, &config->alarm_config);
    }
    
    return ESP_OK;
}

esp_err_t config_manager_init(config_manager_t* manager, const char* config_file_path) {
    if (!manager || !config_file_path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(manager, 0, sizeof(config_manager_t));
    strncpy(manager->config_file_path, config_file_path, sizeof(manager->config_file_path) - 1);
    
    manager->initialized = true;
    
#ifdef DEBUG_CONFIG_MANAGER
    ESP_LOGI(TAG, "Config manager initialized with file: %s", config_file_path);
#endif
    
    return ESP_OK;
}

esp_err_t config_manager_load(config_manager_t* manager) {
    if (!manager || !manager->initialized) {
        ESP_LOGE(TAG, "Config manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Loading configuration from: %s", manager->config_file_path);
    
    // Read file content
    char* file_content = NULL;
    size_t file_size = 0;
    
    esp_err_t ret = storage_manager_read_file(manager->config_file_path, &file_content, &file_size);
    if (ret != ESP_OK) {
        manager->error_count++;
        ESP_LOGE(TAG, "Failed to read config file '%s': %s", manager->config_file_path, esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Successfully read config file: %zu bytes", file_size);
    
    // Parse JSON
    cJSON* json = cJSON_Parse(file_content);
    if (!json) {
        manager->error_count++;
        ESP_LOGE(TAG, "Failed to parse JSON config - invalid JSON format");
        ESP_LOGE(TAG, "JSON content preview (first 200 chars): %.200s", file_content);
        free(file_content);
        return ESP_ERR_INVALID_ARG;
    }
    
    free(file_content);
    ESP_LOGI(TAG, "Successfully parsed JSON configuration");
    
    // Clear current configuration
    memset(&manager->config, 0, sizeof(io_config_t));
    
    // Parse shift register configuration
    ret = parse_shift_register_config(json, &manager->config.shift_register_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No shift register config found in JSON");
    } else {
        ESP_LOGI(TAG, "Loaded shift register config: %d output registers, %d input registers", 
                 manager->config.shift_register_config.num_output_registers,
                 manager->config.shift_register_config.num_input_registers);
    }
    
    // Parse IO points
    cJSON* io_points = cJSON_GetObjectItem(json, "ioPoints");
    if (!io_points) {
        ESP_LOGE(TAG, "No 'ioPoints' array found in JSON");
        cJSON_Delete(json);
        return ESP_ERR_NOT_FOUND;
    }
    
    if (!cJSON_IsArray(io_points)) {
        ESP_LOGE(TAG, "'ioPoints' is not an array in JSON");
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }
    
    int array_size = cJSON_GetArraySize(io_points);
    ESP_LOGI(TAG, "Found ioPoints array with %d items", array_size);
    
    int parsed_count = 0;
    int failed_count = 0;
    
    for (int i = 0; i < array_size && parsed_count < CONFIG_MAX_IO_POINTS; i++) {
        cJSON* point_json = cJSON_GetArrayItem(io_points, i);
        if (point_json) {
            io_point_config_t* point_config = &manager->config.io_points[parsed_count];
            esp_err_t parse_ret = parse_io_point(point_json, point_config);
            if (parse_ret == ESP_OK) {
                ESP_LOGI(TAG, "  [%d] Parsed IO point: %s (type: %d)", 
                         parsed_count, point_config->id, point_config->type);
                parsed_count++;
            } else {
                ESP_LOGW(TAG, "  [%d] Failed to parse IO point at index %d: %s", 
                         failed_count, i, esp_err_to_name(parse_ret));
                failed_count++;
            }
        } else {
            ESP_LOGW(TAG, "  [%d] NULL IO point at index %d", failed_count, i);
            failed_count++;
        }
    }
    
    manager->config.io_point_count = parsed_count;
    
    ESP_LOGI(TAG, "Configuration loading complete:");
    ESP_LOGI(TAG, "  - Successfully parsed: %d IO points", parsed_count);
    ESP_LOGI(TAG, "  - Failed to parse: %d IO points", failed_count);
    ESP_LOGI(TAG, "  - Total in file: %d IO points", array_size);
    
    if (parsed_count == 0) {
        ESP_LOGE(TAG, "No IO points were successfully parsed!");
    }
    
    cJSON_Delete(json);
    manager->load_count++;
    
    return ESP_OK;
}

esp_err_t config_manager_get_shift_register_config(config_manager_t* manager, shift_register_config_t* config) {
    if (!manager || !manager->initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *config = manager->config.shift_register_config;
    return ESP_OK;
}

esp_err_t config_manager_get_io_point_config(config_manager_t* manager, const char* id, io_point_config_t* config) {
    if (!manager || !manager->initialized || !id || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < manager->config.io_point_count; i++) {
        if (strcmp(manager->config.io_points[i].id, id) == 0) {
            *config = manager->config.io_points[i];
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t config_manager_get_all_io_points(config_manager_t* manager, io_point_config_t* configs, int max_configs, int* actual_count) {
    if (!manager || !manager->initialized || !configs || !actual_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int count = (manager->config.io_point_count < max_configs) ? manager->config.io_point_count : max_configs;
    
    for (int i = 0; i < count; i++) {
        configs[i] = manager->config.io_points[i];
    }
    
    *actual_count = count;
    return ESP_OK;
}

esp_err_t config_manager_validate(config_manager_t* manager) {
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Basic validation - check for duplicate IDs
    for (int i = 0; i < manager->config.io_point_count; i++) {
        for (int j = i + 1; j < manager->config.io_point_count; j++) {
            if (strcmp(manager->config.io_points[i].id, manager->config.io_points[j].id) == 0) {
#ifdef DEBUG_CONFIG_MANAGER
                ESP_LOGE(TAG, "Duplicate IO point ID: %s", manager->config.io_points[i].id);
#endif
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    
    return ESP_OK;
}

esp_err_t config_manager_get_statistics(config_manager_t* manager, uint32_t* loads, uint32_t* saves, uint32_t* errors) {
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (loads) *loads = manager->load_count;
    if (saves) *saves = manager->save_count;
    if (errors) *errors = manager->error_count;
    
    return ESP_OK;
}

void config_manager_destroy(config_manager_t* manager) {
    if (manager && manager->initialized) {
        manager->initialized = false;
        
#ifdef DEBUG_CONFIG_MANAGER
        ESP_LOGI(TAG, "Config manager destroyed (loads: %lu, saves: %lu, errors: %lu)", 
                 manager->load_count, manager->save_count, manager->error_count);
#endif
    }
}

// Simplified implementations for remaining functions
esp_err_t config_manager_save(config_manager_t* manager) {
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    manager->save_count++;
    return ESP_OK; // TODO: Implement JSON serialization
}

esp_err_t config_manager_update_io_point(config_manager_t* manager, const io_point_config_t* config) {
    if (!manager || !manager->initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find and update existing point
    for (int i = 0; i < manager->config.io_point_count; i++) {
        if (strcmp(manager->config.io_points[i].id, config->id) == 0) {
            manager->config.io_points[i] = *config;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}
