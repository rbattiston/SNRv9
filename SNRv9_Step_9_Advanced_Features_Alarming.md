# SNRv9 Complete Alarming System - ESP32 ESP-IDF Architecture

## Overview

The SNRv9 Alarming System is a comprehensive monitoring and alerting framework built specifically for ESP32 ESP-IDF framework that provides real-time detection and management of alarm conditions on Analog Input (AI) points. The system integrates seamlessly with the existing SNRv9 IO system and scheduling framework to provide sensor-driven irrigation control with comprehensive safety monitoring.

## ESP-IDF Core Architecture Components

### 1. Alarm Manager Component (`components/core/alarm_manager/`)

#### ESP-IDF Component Structure

**Header File: `include/alarm_manager.h`**
```c
#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "io_manager.h"
#include "event_logger.h"

// Alarm Types
typedef enum {
    ALARM_TYPE_NONE = 0,
    ALARM_TYPE_RATE_OF_CHANGE = 1,
    ALARM_TYPE_DISCONNECTED = 2,
    ALARM_TYPE_MAX_VALUE = 3,
    ALARM_TYPE_STUCK_SIGNAL = 4
} alarm_type_t;

// Alarm State
typedef enum {
    ALARM_STATE_TRUSTED_NO_ALARM = 0,
    ALARM_STATE_PENDING_ALARM = 1,
    ALARM_STATE_OFFICIAL_ALARM = 2,
    ALARM_STATE_UNTRUSTED_ALARM = 3,
    ALARM_STATE_PHYSICALLY_CLEARED = 4,
    ALARM_STATE_MANUAL_RESET_REQUIRED = 5,
    ALARM_STATE_TRUST_RECOVERY = 6
} alarm_state_t;

// Alarm Rule Configuration
typedef struct {
    // Rate of Change
    bool check_rate_of_change;
    float rate_of_change_threshold;
    
    // Disconnected
    bool check_disconnected;
    float disconnected_threshold;
    
    // Max Value
    bool check_max_value;
    float max_value_threshold;
    
    // Stuck Signal
    bool check_stuck_signal;
    uint32_t stuck_signal_window_samples;
    float stuck_signal_delta_threshold;
    
    // Alarm Behavior
    uint32_t alarm_persistence_samples;
    float alarm_clear_hysteresis_value;
    bool requires_manual_reset;
    uint32_t samples_to_clear_alarm_condition;
    uint32_t consecutive_good_samples_to_restore_trust;
} alarm_rule_config_t;

// Alarm Configuration
typedef struct {
    bool enabled;
    uint32_t history_samples_for_analysis;
    alarm_rule_config_t rules;
} alarm_config_t;

// Alarm Runtime State
typedef struct {
    bool in_alarm;
    alarm_type_t current_alarm_type;
    time_t alarm_active_since;
    bool is_trusted;
    bool alarm_condition_physically_cleared;
    alarm_type_t pending_alarm_type;
    uint32_t pending_alarm_consecutive_samples;
    uint32_t consecutive_samples_condition_false;
    uint32_t consecutive_samples_no_alarm_condition_met;
} alarm_runtime_state_t;

// Alarm Status Information
typedef struct {
    bool in_alarm;
    char current_alarm_type[32];
    bool is_trusted;
    bool alarm_condition_physically_cleared;
    time_t alarm_active_since;
    float current_value;
    char units[16];
    bool requires_manual_reset;
} alarm_status_info_t;

// Core API
esp_err_t alarm_manager_init(void);
esp_err_t alarm_manager_deinit(void);
esp_err_t alarm_manager_start(void);
esp_err_t alarm_manager_stop(void);

// Alarm Processing
void alarm_manager_task(void *pvParameters);
esp_err_t alarm_manager_process_alarms(void);

// Alarm Status
esp_err_t alarm_manager_get_status(const char *point_id, alarm_status_info_t *status);
esp_err_t alarm_manager_is_point_in_alarm(const char *point_id, bool *in_alarm);
esp_err_t alarm_manager_reset_alarm(const char *point_id);

// Configuration
esp_err_t alarm_manager_get_config(const char *point_id, alarm_config_t *config);
esp_err_t alarm_manager_set_config(const char *point_id, const alarm_config_t *config);

// AutoPilot Integration
esp_err_t alarm_manager_add_autopilot_sensor(const char *sensor_id);
esp_err_t alarm_manager_is_autopilot_sensor_in_alarm(const char *sensor_id, bool *in_alarm);

#endif // ALARM_MANAGER_H
```

#### ESP-IDF Implementation Features

**FreeRTOS Task Integration**:
```c
#define ALARM_MANAGER_TASK_STACK_SIZE 4096
#define ALARM_MANAGER_TASK_PRIORITY 2
#define ALARM_PROCESSING_INTERVAL_MS 2000

static TaskHandle_t alarm_manager_task_handle = NULL;
static SemaphoreHandle_t alarm_manager_mutex = NULL;
static bool alarm_manager_running = false;

void alarm_manager_task(void *pvParameters) {
    TickType_t last_execution = 0;
    const TickType_t execution_interval = pdMS_TO_TICKS(ALARM_PROCESSING_INTERVAL_MS);
    
    ESP_LOGI(TAG, "Alarm Manager task started");
    
    while (1) {
        TickType_t current_tick = xTaskGetTickCount();
        
        // Process alarms every 2 seconds
        if ((current_tick - last_execution) >= execution_interval) {
            xSemaphoreTake(alarm_manager_mutex, portMAX_DELAY);
            
            if (alarm_manager_running) {
                esp_err_t err = alarm_manager_process_alarms();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Alarm processing failed: %s", esp_err_to_name(err));
                }
                last_execution = current_tick;
            }
            
            xSemaphoreGive(alarm_manager_mutex);
        }
        
        // Yield CPU to prevent watchdog timeout
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t alarm_manager_init(void) {
    // Create mutex
    alarm_manager_mutex = xSemaphoreCreateMutex();
    if (alarm_manager_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize monitored points list
    esp_err_t err = initialize_monitored_points();
    if (err != ESP_OK) {
        vSemaphoreDelete(alarm_manager_mutex);
        return err;
    }
    
    // Create task
    BaseType_t result = xTaskCreate(
        alarm_manager_task,
        "alarm_manager",
        ALARM_MANAGER_TASK_STACK_SIZE,
        NULL,
        ALARM_MANAGER_TASK_PRIORITY,
        &alarm_manager_task_handle
    );
    
    if (result != pdPASS) {
        vSemaphoreDelete(alarm_manager_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Alarm Manager initialized successfully");
    return ESP_OK;
}
```

### 2. Alarm Detection Logic (ESP-IDF Implementation)

#### Rate of Change Detection

**ESP-IDF Implementation**:
```c
static bool is_condition_rate_of_change(const alarm_config_t *config,
                                       const io_point_runtime_state_t *current_state,
                                       const conditioned_value_entry_t *history,
                                       size_t history_size) {
    if (!config->rules.check_rate_of_change || history_size < 2) {
        return false;
    }
    
    const conditioned_value_entry_t *latest = &history[history_size - 1];
    const conditioned_value_entry_t *previous = &history[history_size - 2];
    
    if (isnan(latest->value) || isnan(previous->value)) {
        return false;
    }
    
    float value_delta = latest->value - previous->value;
    uint32_t time_delta_ms = latest->timestamp - previous->timestamp;
    
    if (time_delta_ms == 0) {
        return false;
    }
    
    float rate_of_change_per_second = (value_delta / (float)time_delta_ms) * 1000.0f;
    
    bool condition_met = fabsf(rate_of_change_per_second) > config->rules.rate_of_change_threshold;
    
    if (condition_met) {
        ESP_LOGW(TAG, "Rate of change alarm condition: %.2f/s > %.2f/s", 
                 rate_of_change_per_second, config->rules.rate_of_change_threshold);
    }
    
    return condition_met;
}
```

#### Disconnected Detection

**ESP-IDF Implementation**:
```c
static bool is_condition_disconnected(const alarm_config_t *config,
                                     const io_point_runtime_state_t *current_state) {
    if (!config->rules.check_disconnected) {
        return false;
    }
    
    if (isnan(current_state->conditioned_value)) {
        ESP_LOGW(TAG, "Disconnected alarm condition: NaN value detected");
        return true;
    }
    
    bool condition_met = current_state->conditioned_value <= config->rules.disconnected_threshold;
    
    if (condition_met) {
        ESP_LOGW(TAG, "Disconnected alarm condition: %.2f <= %.2f", 
                 current_state->conditioned_value, config->rules.disconnected_threshold);
    }
    
    return condition_met;
}
```

#### Maximum Value Detection

**ESP-IDF Implementation**:
```c
static bool is_condition_max_value(const alarm_config_t *config,
                                  const io_point_runtime_state_t *current_state) {
    if (!config->rules.check_max_value) {
        return false;
    }
    
    if (isnan(current_state->conditioned_value)) {
        return false;
    }
    
    bool condition_met = current_state->conditioned_value >= config->rules.max_value_threshold;
    
    if (condition_met) {
        ESP_LOGW(TAG, "Max value alarm condition: %.2f >= %.2f", 
                 current_state->conditioned_value, config->rules.max_value_threshold);
    }
    
    return condition_met;
}
```

#### Stuck Signal Detection

**ESP-IDF Implementation**:
```c
static bool is_condition_stuck_signal(const alarm_config_t *config,
                                     const io_point_runtime_state_t *current_state,
                                     const conditioned_value_entry_t *history,
                                     size_t history_size) {
    if (!config->rules.check_stuck_signal) {
        return false;
    }
    
    uint32_t window_size = config->rules.stuck_signal_window_samples;
    if (history_size < window_size) {
        return false;
    }
    
    float min_value = NAN, max_value = NAN;
    size_t valid_samples = 0;
    
    // Analyze the last 'window_size' samples
    for (size_t i = 0; i < window_size; i++) {
        const conditioned_value_entry_t *sample = &history[history_size - 1 - i];
        
        if (!isnan(sample->value)) {
            if (isnan(min_value) || sample->value < min_value) {
                min_value = sample->value;
            }
            if (isnan(max_value) || sample->value > max_value) {
                max_value = sample->value;
            }
            valid_samples++;
        }
    }
    
    if (valid_samples < window_size || isnan(min_value) || isnan(max_value)) {
        return false;
    }
    
    float delta = max_value - min_value;
    bool condition_met = delta < config->rules.stuck_signal_delta_threshold;
    
    if (condition_met) {
        ESP_LOGW(TAG, "Stuck signal alarm condition: delta %.2f < %.2f", 
                 delta, config->rules.stuck_signal_delta_threshold);
    }
    
    return condition_met;
}
```

### 3. Alarm State Management (ESP-IDF Implementation)

#### State Transition Engine

**ESP-IDF State Management**:
```c
static esp_err_t process_alarm_state_machine(const char *point_id,
                                           const alarm_config_t *config,
                                           alarm_runtime_state_t *alarm_state,
                                           const io_point_runtime_state_t *io_state) {
    bool any_condition_met = false;
    alarm_type_t detected_alarm_type = ALARM_TYPE_NONE;
    
    // Get sensor history
    conditioned_value_entry_t *history = NULL;
    size_t history_size = 0;
    esp_err_t err = io_manager_get_point_history(point_id, &history, &history_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get history for point %s: %s", point_id, esp_err_to_name(err));
        return err;
    }
    
    // Check all alarm conditions
    if (is_condition_rate_of_change(config, io_state, history, history_size)) {
        any_condition_met = true;
        detected_alarm_type = ALARM_TYPE_RATE_OF_CHANGE;
    } else if (is_condition_disconnected(config, io_state)) {
        any_condition_met = true;
        detected_alarm_type = ALARM_TYPE_DISCONNECTED;
    } else if (is_condition_max_value(config, io_state)) {
        any_condition_met = true;
        detected_alarm_type = ALARM_TYPE_MAX_VALUE;
    } else if (is_condition_stuck_signal(config, io_state, history, history_size)) {
        any_condition_met = true;
        detected_alarm_type = ALARM_TYPE_STUCK_SIGNAL;
    }
    
    // Process state transitions
    if (!alarm_state->in_alarm && alarm_state->is_trusted && any_condition_met) {
        // Trusted, no alarm -> Pending alarm
        if (alarm_state->pending_alarm_type == detected_alarm_type) {
            alarm_state->pending_alarm_consecutive_samples++;
        } else {
            alarm_state->pending_alarm_type = detected_alarm_type;
            alarm_state->pending_alarm_consecutive_samples = 1;
        }
        
        // Check if persistence threshold met
        if (alarm_state->pending_alarm_consecutive_samples >= config->rules.alarm_persistence_samples) {
            trigger_official_alarm(point_id, alarm_state, detected_alarm_type);
        }
    } else if (alarm_state->in_alarm && !any_condition_met) {
        // In alarm, condition cleared
        alarm_state->consecutive_samples_condition_false++;
        
        if (alarm_state->consecutive_samples_condition_false >= config->rules.samples_to_clear_alarm_condition) {
            alarm_state->alarm_condition_physically_cleared = true;
            
            if (!config->rules.requires_manual_reset) {
                clear_alarm_automatically(point_id, alarm_state);
            }
        }
    } else if (!any_condition_met) {
        // No conditions met - reset pending alarm
        alarm_state->pending_alarm_type = ALARM_TYPE_NONE;
        alarm_state->pending_alarm_consecutive_samples = 0;
        alarm_state->consecutive_samples_condition_false = 0;
    }
    
    // Process trust restoration for untrusted sensors
    if (!alarm_state->is_trusted) {
        evaluate_trust_restoration(alarm_state, config, any_condition_met);
    }
    
    free(history);
    return ESP_OK;
}
```

#### Alarm Triggering

**ESP-IDF Alarm Triggering**:
```c
static void trigger_official_alarm(const char *point_id,
                                  alarm_runtime_state_t *alarm_state,
                                  alarm_type_t alarm_type) {
    alarm_state->in_alarm = true;
    alarm_state->current_alarm_type = alarm_type;
    alarm_state->alarm_active_since = time(NULL);
    alarm_state->is_trusted = false;
    alarm_state->alarm_condition_physically_cleared = false;
    alarm_state->pending_alarm_type = ALARM_TYPE_NONE;
    alarm_state->pending_alarm_consecutive_samples = 0;
    alarm_state->consecutive_samples_condition_false = 0;
    
    // Log alarm event
    char alarm_type_str[32];
    alarm_type_to_string(alarm_type, alarm_type_str, sizeof(alarm_type_str));
    
    ESP_LOGW(TAG, "ALARM TRIGGERED: %s for point %s", alarm_type_str, point_id);
    
    // Create alarm details JSON
    cJSON *alarm_details = cJSON_CreateObject();
    cJSON_AddStringToObject(alarm_details, "alarmType", alarm_type_str);
    cJSON_AddNumberToObject(alarm_details, "triggeredAt", alarm_state->alarm_active_since);
    
    char *details_json = cJSON_Print(alarm_details);
    event_logger_log_alarm_event(point_id, "", LOG_EVENT_ALARM_TRIGGERED, details_json);
    
    free(details_json);
    cJSON_Delete(alarm_details);
}
```

#### Alarm Clearing

**ESP-IDF Alarm Clearing**:
```c
static void clear_alarm_automatically(const char *point_id,
                                     alarm_runtime_state_t *alarm_state) {
    alarm_type_t cleared_alarm_type = alarm_state->current_alarm_type;
    
    alarm_state->in_alarm = false;
    alarm_state->current_alarm_type = ALARM_TYPE_NONE;
    alarm_state->alarm_active_since = 0;
    alarm_state->alarm_condition_physically_cleared = false;
    alarm_state->consecutive_samples_condition_false = 0;
    
    // Log alarm cleared event
    char alarm_type_str[32];
    alarm_type_to_string(cleared_alarm_type, alarm_type_str, sizeof(alarm_type_str));
    
    ESP_LOGI(TAG, "ALARM CLEARED: %s for point %s (automatic)", alarm_type_str, point_id);
    
    // Create alarm cleared details JSON
    cJSON *clear_details = cJSON_CreateObject();
    cJSON_AddStringToObject(clear_details, "alarmType", alarm_type_str);
    cJSON_AddNumberToObject(clear_details, "clearedAt", time(NULL));
    cJSON_AddStringToObject(clear_details, "clearanceMethod", "AUTO");
    
    char *details_json = cJSON_Print(clear_details);
    event_logger_log_alarm_event(point_id, "", LOG_EVENT_ALARM_CLEARED, details_json);
    
    free(details_json);
    cJSON_Delete(clear_details);
}

esp_err_t alarm_manager_reset_alarm(const char *point_id) {
    if (point_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(alarm_manager_mutex, portMAX_DELAY);
    
    alarm_runtime_state_t alarm_state;
    esp_err_t err = get_alarm_runtime_state(point_id, &alarm_state);
    if (err != ESP_OK) {
        xSemaphoreGive(alarm_manager_mutex);
        return err;
    }
    
    if (!alarm_state.in_alarm) {
        xSemaphoreGive(alarm_manager_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    alarm_type_t cleared_alarm_type = alarm_state.current_alarm_type;
    
    // Clear alarm state
    alarm_state.in_alarm = false;
    alarm_state.current_alarm_type = ALARM_TYPE_NONE;
    alarm_state.alarm_active_since = 0;
    alarm_state.alarm_condition_physically_cleared = false;
    alarm_state.consecutive_samples_condition_false = 0;
    
    // Save updated state
    err = set_alarm_runtime_state(point_id, &alarm_state);
    if (err != ESP_OK) {
        xSemaphoreGive(alarm_manager_mutex);
        return err;
    }
    
    xSemaphoreGive(alarm_manager_mutex);
    
    // Log manual reset event
    char alarm_type_str[32];
    alarm_type_to_string(cleared_alarm_type, alarm_type_str, sizeof(alarm_type_str));
    
    ESP_LOGI(TAG, "ALARM RESET: %s for point %s (manual)", alarm_type_str, point_id);
    
    // Create alarm cleared details JSON
    cJSON *clear_details = cJSON_CreateObject();
    cJSON_AddStringToObject(clear_details, "alarmType", alarm_type_str);
    cJSON_AddNumberToObject(clear_details, "clearedAt", time(NULL));
    cJSON_AddStringToObject(clear_details, "clearanceMethod", "MANUAL");
    
    char *details_json = cJSON_Print(clear_details);
    event_logger_log_alarm_event(point_id, "", LOG_EVENT_ALARM_CLEARED, details_json);
    
    free(details_json);
    cJSON_Delete(clear_details);
    
    return ESP_OK;
}
```

### 4. Trust and Recovery System (ESP-IDF Implementation)

#### Trust Restoration

**ESP-IDF Trust Management**:
```c
static void evaluate_trust_restoration(alarm_runtime_state_t *alarm_state,
                                      const alarm_config_t *config,
                                      bool any_condition_met) {
    if (alarm_state->is_trusted) {
        return;
    }
    
    if (any_condition_met) {
        alarm_state->consecutive_samples_no_alarm_condition_met = 0;
    } else {
        alarm_state->consecutive_samples_no_alarm_condition_met++;
        
        if (alarm_state->consecutive_samples_no_alarm_condition_met >= 
            config->rules.consecutive_good_samples_to_restore_trust) {
            
            alarm_state->is_trusted = true;
            alarm_state->consecutive_samples_no_alarm_condition_met = 0;
            
            ESP_LOGI(TAG, "Trust restored for sensor (consecutive good samples reached)");
        }
    }
}
```

### 5. Web Interface Integration (`components/web/alarm_controller/`)

#### ESP-IDF HTTP Server Integration

**Header File: `include/alarm_controller.h`**
```c
#ifndef ALARM_CONTROLLER_H
#define ALARM_CONTROLLER_H

#include "esp_http_server.h"
#include "alarm_manager.h"

// Core API
esp_err_t alarm_controller_init(void);
esp_err_t alarm_controller_register_endpoints(httpd_handle_t server);

// HTTP Handlers
esp_err_t alarm_status_get_handler(httpd_req_t *req);
esp_err_t alarm_config_get_handler(httpd_req_t *req);
esp_err_t alarm_config_put_handler(httpd_req_t *req);
esp_err_t alarm_reset_post_handler(httpd_req_t *req);

#endif // ALARM_CONTROLLER_H
```

#### RESTful API Implementation

**Alarm Status Endpoint**:
```c
// GET /api/io/point/{pointId}/alarmstatus
esp_err_t alarm_status_get_handler(httpd_req_t *req) {
    // Extract point ID from URI
    char point_id[32];
    esp_err_t err = extract_point_id_from_uri(req->uri, point_id, sizeof(point_id));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid point ID");
        return ESP_FAIL;
    }
    
    // Get alarm status
    alarm_status_info_t status;
    err = alarm_manager_get_status(point_id, &status);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get alarm status");
        return ESP_FAIL;
    }
    
    // Create JSON response
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "inAlarm", status.in_alarm);
    cJSON_AddStringToObject(json, "currentAlarmType", status.current_alarm_type);
    cJSON_AddBoolToObject(json, "isTrusted", status.is_trusted);
    cJSON_AddBoolToObject(json, "alarmConditionPhysicallyCleared", status.alarm_condition_physically_cleared);
    cJSON_AddNumberToObject(json, "alarmActiveSince", status.alarm_active_since);
    cJSON_AddNumberToObject(json, "currentValue", status.current_value);
    cJSON_AddStringToObject(json, "units", status.units);
    cJSON_AddBoolToObject(json, "requiresManualReset", status.requires_manual_reset);
    
    // Wrap in success response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddItemToObject(response, "data", json);
    
    char *json_string = cJSON_Print(response);
    cJSON_Delete(response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    free(json_string);
    return ESP_OK;
}
```

### 6. AutoPilot Integration (ESP-IDF Implementation)

#### Sensor Alarm Checking for Schedule System

**ESP-IDF AutoPilot Integration**:
```c
esp_err_t alarm_manager_is_autopilot_sensor_in_alarm(const char *sensor_id, bool *in_alarm) {
    if (sensor_id == NULL || in_alarm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(alarm_manager_mutex, portMAX_DELAY);
    
    alarm_runtime_state_t alarm_state;
    esp_err_t err = get_alarm_runtime_state(sensor_id, &alarm_state);
    if (err != ESP_OK) {
        xSemaphoreGive(alarm_manager_mutex);
        *in_alarm = false; // Default to no alarm if sensor not found
        return ESP_OK;
    }
    
    *in_alarm = alarm_state.in_alarm;
    
    xSemaphoreGive(alarm_manager_mutex);
    return ESP_OK;
}

esp_err_t alarm_manager_add_autopilot_sensor(const char *sensor_id) {
    if (sensor_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Verify sensor exists in IO system
    io_point_config_t point_config;
    esp_err_t err = io_manager_get_point_config(sensor_id, &point_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot add AutoPilot sensor %s - not found in IO system", sensor_id);
        return err;
    }
    
    if (point_config.point_type != IO_POINT_TYPE_ANALOG_INPUT) {
        ESP_LOGE(TAG, "Cannot add AutoPilot sensor %s - not an analog input", sensor_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Add to monitored points list
    err = add_monitored_point(sensor_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add AutoPilot sensor %s to monitoring: %s", 
                 sensor_id, esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "AutoPilot sensor %s added to alarm monitoring", sensor_id);
    return ESP_OK;
}
```

#### Schedule System Integration

**Integration with Schedule Executor**:
```c
// In schedule_executor.c - AutoPilot window processing
static bool should_trigger_autopilot_watering(const char *sensor_id, 
                                             float trigger_setpoint,
                                             float current_value) {
    // Check if sensor is in alarm state
    bool sensor_in_alarm = false;
    esp_err_t err = alarm_manager_is_autopilot_sensor_in_alarm(sensor_id, &sensor_in_alarm);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to check alarm status for sensor %s: %s", 
                 sensor_id, esp_err_to_name(err));
        return false;
    }
    
    if (sensor_in_alarm) {
        ESP_LOGW(TAG, "AutoPilot watering blocked - sensor %s is in alarm state", sensor_id);
        return false;
    }
    
    // Normal trigger logic
    return current_value > trigger_setpoint;
}
```

### 7. Configuration Persistence (ESP-IDF Implementation)

#### LittleFS Storage Integration

**ESP-IDF Configuration Storage**:
```c
#define ALARM_CONFIG_DIR "/data/alarm_configs"
#define ALARM_STATE_DIR "/data/alarm_states"

esp_err_t alarm_manager_save_config(const char *point_id, const alarm_config_t *config) {
    if (point_id == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Create config directory if it doesn't exist
    struct stat st;
    if (stat(ALARM_CONFIG_DIR, &st) != 0) {
        if (mkdir(ALARM_CONFIG_DIR, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create alarm config directory");
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    // Create config file path
    char config_path[256];
    snprintf(config_path, sizeof(config_path), "%s/%s.json", ALARM_CONFIG_DIR, point_id);
    
    // Create JSON configuration
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "enabled", config->enabled);
    cJSON_AddNumberToObject(json, "historySamplesForAnalysis", config->history_samples_for_analysis);
    
    // Add rules object
    cJSON *rules = cJSON_CreateObject();
    cJSON_AddBoolToObject(rules, "checkRateOfChange", config->rules.check_rate_of_change);
    cJSON_AddNumberToObject(rules, "rateOfChangeThreshold", config->rules.rate_of_change_threshold);
    cJSON_AddBoolToObject(rules, "checkDisconnected", config->rules.check_disconnected);
    cJSON_AddNumberToObject(rules, "disconnectedThreshold", config->rules.disconnected_threshold);
    cJSON_AddBoolToObject(rules, "checkMaxValue", config->rules.check_max_value);
    cJSON_AddNumberToObject(rules, "maxValueThreshold", config->rules.max_value_threshold);
    cJSON_AddBoolToObject(rules, "checkStuckSignal", config->rules.check_stuck_signal);
    cJSON_AddNumberToObject(rules, "stuckSignalWindowSamples", config->rules.stuck_signal_window_samples);
    cJSON_AddNumberToObject(rules, "stuckSignalDeltaThreshold", config->rules.stuck_signal_delta_threshold);
    cJSON_AddNumberToObject(rules, "alarmPersistenceSamples", config->rules.alarm_persistence_samples);
    cJSON_AddNumberToObject(rules, "alarmClearHysteresisValue", config->rules.alarm_clear_hysteresis_value);
    cJSON_AddBoolToObject(rules, "requiresManualReset", config->rules.requires_manual_reset);
    cJSON_AddNumberToObject(rules, "samplesToClearAlarmCondition", config->rules.samples_to_clear_alarm_condition);
    cJSON_AddNumberToObject(rules, "consecutiveGoodSamplesToRestoreTrust", config->rules.consecutive_good_samples_to_restore_trust);
    
    cJSON_AddItemToObject(json, "rules", rules);
    
    // Write to file
    char *json_string = cJSON_Print(json);
    FILE *file = fopen(config_path, "w");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open config file for writing: %s", config_path);
        free(json_string);
        cJSON_Delete(json);
        return ESP_ERR_INVALID_STATE;
    }
    
    fprintf(file, "%s", json_string);
    fclose(file);
    
    free(json_string);
    cJSON_Delete(json);
    
    ESP_LOGI(TAG, "Alarm configuration saved for point %s", point_id);
    return ESP_OK;
}

esp_err_t alarm_manager_load_config(const char *point_id, alarm_config_t *config) {
    if (point_id == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char config_path[256];
    snprintf(config_path, sizeof(config_path), "%s/%s.json", ALARM_CONFIG_DIR, point_id);
    
    FILE *file = fopen(config_path, "r");
    if (!file) {
        // Return default configuration if file doesn't exist
        *config = get_default_alarm_config();
        return ESP_OK;
    }
    
    // Read file content
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *json_string = malloc(file_size + 1);
    if (!json_string) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    
    fread(json_string, 1, file_size, file);
    json_string[file_size] = '\0';
    fclose(file);
    
    // Parse JSON
    cJSON *json = cJSON_Parse(json_string);
    free(json_string);
    
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse alarm config JSON for point %s", point_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Extract configuration
    esp_err_t err = parse_alarm_config_from_json(json, config);
    cJSON_Delete(json);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to extract alarm config for point %s", point_id);
        return err;
    }
    
    ESP_LOGI(TAG, "Alarm configuration loaded for point %s", point_id);
    return ESP_OK;
}
```

### 8. ESP-IDF Component Architecture

#### Component Configuration

**Alarm Manager CMakeLists.txt**:
```cmake
idf_component_register(
    SRCS "alarm_manager.c" "alarm_detection.c" "alarm_state_machine.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        "esp_littlefs"
        "esp_timer"
        "freertos"
        "io_manager"
        "event_logger"
        "json"
    PRIV_REQUIRES
        "esp_system"
        "esp_crc"
)
```

**Web Alarm Controller CMakeLists.txt**:
```cmake
idf_component_register(
    SRCS "alarm_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        "esp_http_server"
        "json"
        "alarm_manager"
        "auth_manager"
    PRIV_REQUIRES
        "esp_system"
)
```

### 9. Memory Management and Performance

#### ESP-IDF Memory Optimization

**Memory Pool for Alarm States**:
```c
#define MAX_MONITORED_POINTS 32

typedef struct {
    char point_id[32];
    alarm_config_t config;
    alarm_runtime_state_t state;
    bool in_use;
} monitored_point_entry_t;

static monitored_point_entry_t monitored_points[MAX_MONITORED_POINTS];
static SemaphoreHandle_t monitored_points_mutex = NULL;

esp_err_t initialize_monitored_points(void) {
    monitored_points_mutex = xSemaphoreCreateMutex();
    if (monitored_points_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(monitored_points, 0, sizeof(monitored_points));
    
    ESP_LOGI(TAG, "Monitored points pool initialized (%d slots)", MAX_MONITORED_POINTS);
    return ESP_OK;
}

esp_err_t add_monitored_point(const char *point_id) {
    if (point_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(monitored_points_mutex, portMAX_DELAY);
    
    // Check if already exists
    for (size_t i = 0; i < MAX_MONITORED_POINTS; i++) {
        if (monitored_points[i].in_use && 
            strcmp(monitored_points[i].point_id, point_id) == 0) {
            xSemaphoreGive(monitored_points_mutex);
            return ESP_OK; // Already exists
        }
    }
    
    // Find free slot
    for (size_t i = 0; i < MAX_MONITORED_POINTS; i++) {
        if (!monitored_points[i].in_use) {
            strncpy(monitored_points[i].point_id, point_id, sizeof(monitored_points[i].point_id) - 1);
            monitored_points[i].config = get_default_alarm_config();
            memset(&monitored_points[i].state, 0, sizeof(monitored_points[i].state));
            monitored_points[i].state.is_trusted = true; // Start trusted
            monitored_points[i].in_use = true;
            
            xSemaphoreGive(monitored_points_mutex);
            
            // Load configuration from storage
            alarm_manager_load_config(point_id, &monitored_points[i].config);
            
            ESP_LOGI(TAG, "Added monitored point %s (slot %d)", point_id, i);
            return ESP_OK;
        }
    }
    
    xSemaphoreGive(monitored_points_mutex);
    ESP_LOGE(TAG, "No free slots for monitored point %s", point_id);
    return ESP_ERR_NO_MEM;
}
```

#### Performance Considerations

**ESP-IDF Performance Patterns**:
- **Task Priority**: Alarm Manager (2) - Lower than IO processing but higher than monitoring
- **Processing Interval**: 2 seconds - Balance between responsiveness and CPU usage
- **Memory Efficiency**: Fixed-size pools prevent fragmentation
- **CPU Yielding**: Regular `vTaskDelay()` calls prevent watchdog timeouts

### 10. Error Handling and Logging

#### ESP-IDF Error Patterns

**Comprehensive Error Handling**:
```c
esp_err_t alarm_manager_process_alarms(void) {
    esp_err_t overall_result = ESP_OK;
    int processed_count = 0;
    int error_count = 0;
    
    xSemaphoreTake(monitored_points_mutex, portMAX_DELAY);
    
    for (size_t i = 0; i < MAX_MONITORED_POINTS; i++) {
        if (!monitored_points[i].in_use) {
            continue;
        }
        
        const char *point_id = monitored_points[i].point_id;
        
        // Get current IO state
        io_point_runtime_state_t io_state;
        esp_err_t err = io_manager_get_point_state(point_id, &io_state);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get IO state for point %s: %s", 
                     point_id, esp_err_to_name(err));
            error_count++;
            continue;
        }
        
        // Process alarm state machine
        err = process_alarm_state_machine(point_id, 
                                        &monitored_points[i].config,
                                        &monitored_points[i].state,
                                        &io_state);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Alarm processing failed for point %s: %s", 
                     point_id, esp_err_to_name(err));
            error_count++;
            overall_result = err;
        } else {
            processed_count++;
        }
        
        // Yield CPU to prevent watchdog timeout
        if ((i % 8) == 7) {
            xSemaphoreGive(monitored_points_mutex);
            vTaskDelay(pdMS_TO_TICKS(10));
            xSemaphoreTake(monitored_points_mutex, portMAX_DELAY);
        }
    }
    
    xSemaphoreGive(monitored_points_mutex);
    
    if (processed_count > 0 || error_count > 0) {
        ESP_LOGI(TAG, "Alarm processing complete: %d processed, %d errors", 
                 processed_count, error_count);
    }
    
    return overall_result;
}
```

### 11. Integration with SNRv9 System

#### Main Application Integration

**Integration in main.c**:
```c
// In main.c
esp_err_t initialize_alarm_system(void) {
    ESP_LOGI(TAG, "Initializing Alarm Manager...");
    
    esp_err_t err = alarm_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Alarm Manager: %s", esp_err_to_name(err));
        return err;
    }
    
    // Start alarm processing
    err = alarm_manager_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Alarm Manager: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Alarm Manager initialized and started successfully");
    return ESP_OK;
}
```

#### Web Server Integration

**Endpoint Registration**:
```c
// In web_server_manager.c
esp_err_t register_alarm_endpoints(httpd_handle_t server) {
    httpd_uri_t alarm_status_uri = {
        .uri = "/api/io/point/*/alarmstatus",
        .method = HTTP_GET,
        .handler = alarm_status_get_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t alarm_config_get_uri = {
        .uri = "/api/io/point/*/alarmconfig",
        .method = HTTP_GET,
        .handler = alarm_config_get_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t alarm_config_put_uri = {
        .uri = "/api/io/point/*/alarmconfig",
        .method = HTTP_PUT,
        .handler = alarm_config_put_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t alarm_reset_uri = {
        .uri = "/api/io/point/*/resetalarm",
        .method = HTTP_POST,
        .handler = alarm_reset_post_handler,
        .user_ctx = NULL
    };
    
    esp_err_t err = httpd_register_uri_handler(server, &alarm_status_uri);
    if (err != ESP_OK) return err;
    
    err = httpd_register_uri_handler(server, &alarm_config_get_uri);
    if (err != ESP_OK) return err;
    
    err = httpd_register_uri_handler(server, &alarm_config_put_uri);
    if (err != ESP_OK) return err;
    
    err = httpd_register_uri_handler(server, &alarm_reset_uri);
    if (err != ESP_OK) return err;
    
    ESP_LOGI(TAG, "Alarm endpoints registered successfully");
    return ESP_OK;
}
```

## Summary

This ESP-IDF Alarming System provides:

### **Core Features**
- **Real-time Alarm Detection**: 4 alarm types with configurable thresholds
- **State Management**: Comprehensive state machine with trust/recovery system
- **AutoPilot Integration**: Prevents irrigation when sensors are in alarm
- **Web Interface**: RESTful API for configuration and control
- **Persistent Configuration**: LittleFS storage for alarm settings

### **ESP-IDF Optimizations**
- **FreeRTOS Integration**: Dedicated task with proper priority and stack management
- **Memory Efficiency**: Fixed-size pools and optimized data structures
- **Thread Safety**: Comprehensive mutex protection with timeouts
- **Error Handling**: Consistent ESP-IDF error codes and logging
- **Performance**: 2-second processing interval with CPU yielding

### **Production Features**
- **Watchdog Protection**: Regular CPU yielding prevents timeouts
- **Graceful Degradation**: System continues operation on component failures
- **Comprehensive Logging**: ESP-IDF logging integration with detailed diagnostics
- **Configuration Persistence**: Automatic save/load of alarm configurations

The system integrates seamlessly with the SNRv9 Time Management, Scheduling, and IO systems to provide a complete irrigation control solution with comprehensive safety monitoring and alerting capabilities optimized for the ESP32 platform.
```

**Alarm Configuration Endpoint**:
```c
// PUT /api/io/point/{pointId}/alarmconfig
esp_err_t alarm_config_put_handler(httpd_req_t *req) {
    // Authentication check
    if (!auth_check_session(req, AUTH_ROLE_MANAGER)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
        return ESP_FAIL;
    }
    
    // Extract point ID from URI
    char point_id[32];
    esp_err_t err = extract_point_id_from_uri(req->uri, point_id, sizeof(point_id));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid point ID");
        return ESP_FAIL;
    }
    
    // Parse JSON body
    char *content = malloc(req->content_len + 1);
    if (!content) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    
    int ret = httpd_req_recv(req, content, req->content_len);
    if (ret <= 0) {
        free(content);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    // Parse alarm configuration
    alarm_config_t config;
    err = parse_alarm_config_json(content, &config);
    free(content);
    
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid alarm configuration format");
        return ESP_FAIL;
    }
    
    // Set alarm configuration
    err = alarm_manager_set_config(point_id, &config);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set alarm configuration");
        return ESP_FAIL;
    }
    
    // Create success response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "Alarm configuration updated successfully");
    
    char *json_string = cJSON_Print(response);
    cJSON_Delete(response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    free(json_string);
    return ESP_OK;
}
```

**Manual Reset Endpoint**:
```c
// POST /api/io/point/{pointId}/resetalarm
esp_err_t alarm_reset_post_handler(httpd_req_t *req) {
    // Authentication check
    if (!auth_check_session(req, AUTH_ROLE_MANAGER)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
        return ESP_FAIL;
    }
    
    // Extract point ID from URI
    char point_id[32];
    esp_err_t err = extract_point_id_from_uri(req->uri, point_id, sizeof(point_id));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid point ID");
        return ESP_FAIL;
    }
    
    // Reset alarm
    err = alarm_manager_reset_alarm(point_id);
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No active alarm to reset");
        return ESP_FAIL;
    } else if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to reset alarm");
        return ESP_FAIL;
    }
    
    // Create success response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "Alarm reset successfully");
    
    char *json_string = cJSON_Print(response);
    cJSON_Delete(response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json
