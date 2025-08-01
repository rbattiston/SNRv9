/**
 * @file alarm_manager.c
 * @brief Alarm Management System Implementation for SNRv9 Irrigation Control System
 */

#include "alarm_manager.h"
#include "debug_config.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

#ifdef DEBUG_ALARM_SYSTEM
static const char* TAG = DEBUG_ALARM_SYSTEM_TAG;
#endif

// Forward declarations
static void alarm_monitoring_task(void* pvParameters);
static esp_err_t alarm_check_rate_of_change(alarm_manager_t* manager, int point_index, const alarm_config_t* config);
static esp_err_t alarm_check_disconnected(alarm_manager_t* manager, int point_index, const alarm_config_t* config);
static esp_err_t alarm_check_max_value(alarm_manager_t* manager, int point_index, const alarm_config_t* config);
static esp_err_t alarm_check_stuck_signal(alarm_manager_t* manager, int point_index, const alarm_config_t* config);
static int alarm_find_point_index(alarm_manager_t* manager, const char* point_id);
static void alarm_activate(alarm_manager_t* manager, int point_index, alarm_type_t alarm_type);
static void alarm_clear(alarm_manager_t* manager, int point_index, alarm_type_t alarm_type);

esp_err_t alarm_manager_init(alarm_manager_t* manager, config_manager_t* config_manager)
{
    if (!manager || !config_manager) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef DEBUG_ALARM_SYSTEM
    printf("[%s] Initializing alarm manager...\n", TAG);
#endif

    // Clear structure
    memset(manager, 0, sizeof(alarm_manager_t));

    // Create mutex
    manager->alarm_mutex = xSemaphoreCreateMutex();
    if (!manager->alarm_mutex) {
#ifdef DEBUG_ALARM_SYSTEM
        printf("[%s] Failed to create alarm mutex\n", TAG);
#endif
        return ESP_ERR_NO_MEM;
    }

    // Store configuration manager reference
    manager->config_manager = config_manager;

    // Load IO configuration and initialize alarm states
    io_config_t io_config;
    esp_err_t ret = config_manager_get_io_config(config_manager, &io_config);
    if (ret != ESP_OK) {
#ifdef DEBUG_ALARM_SYSTEM
        printf("[%s] Failed to load IO configuration: %s\n", TAG, esp_err_to_name(ret));
#endif
        vSemaphoreDelete(manager->alarm_mutex);
        return ret;
    }

    // Initialize alarm states for points with alarm configuration
    manager->active_point_count = 0;
    for (int i = 0; i < io_config.point_count && manager->active_point_count < CONFIG_MAX_IO_POINTS; i++) {
        const io_point_config_t* point = &io_config.points[i];
        
        // Only monitor analog inputs with alarm configuration enabled
        if (point->type == IO_POINT_TYPE_GPIO_AI && point->alarm_config.enabled) {
            // Store point ID
            strncpy(manager->point_ids[manager->active_point_count], point->id, CONFIG_MAX_ID_LENGTH - 1);
            manager->point_ids[manager->active_point_count][CONFIG_MAX_ID_LENGTH - 1] = '\0';
            
            // Initialize alarm state
            alarm_state_t* state = &manager->point_alarms[manager->active_point_count];
            memset(state, 0, sizeof(alarm_state_t));
            state->trust_restored = true; // Start with trust
            
#ifdef DEBUG_ALARM_SYSTEM
            printf("[%s] Initialized alarm monitoring for point '%s'\n", TAG, point->id);
#endif
            
            manager->active_point_count++;
        }
    }

    manager->initialized = true;

#ifdef DEBUG_ALARM_SYSTEM
    printf("[%s] Alarm manager initialized with %d monitored points\n", TAG, manager->active_point_count);
#endif

    return ESP_OK;
}

esp_err_t alarm_manager_start_monitoring(alarm_manager_t* manager, uint32_t check_interval_ms,
                                        UBaseType_t task_priority, uint32_t task_stack_size)
{
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (manager->alarm_task_running) {
        return ESP_ERR_INVALID_STATE; // Already running
    }

    // Create alarm monitoring task
    BaseType_t result = xTaskCreate(
        alarm_monitoring_task,
        "alarm_monitor",
        task_stack_size,
        manager,
        task_priority,
        &manager->alarm_task_handle
    );

    if (result != pdPASS) {
#ifdef DEBUG_ALARM_SYSTEM
        printf("[%s] Failed to create alarm monitoring task\n", TAG);
#endif
        return ESP_ERR_NO_MEM;
    }

    manager->alarm_task_running = true;

#ifdef DEBUG_ALARM_SYSTEM
    printf("[%s] Alarm monitoring task started (interval: %lu ms, priority: %lu, stack: %lu)\n", 
           TAG, check_interval_ms, task_priority, task_stack_size);
#endif

    return ESP_OK;
}

esp_err_t alarm_manager_stop_monitoring(alarm_manager_t* manager)
{
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!manager->alarm_task_running) {
        return ESP_OK; // Already stopped
    }

    // Delete task
    if (manager->alarm_task_handle) {
        vTaskDelete(manager->alarm_task_handle);
        manager->alarm_task_handle = NULL;
    }

    manager->alarm_task_running = false;

#ifdef DEBUG_ALARM_SYSTEM
    printf("[%s] Alarm monitoring task stopped\n", TAG);
#endif

    return ESP_OK;
}

esp_err_t alarm_manager_update_value(alarm_manager_t* manager, const char* point_id, float conditioned_value)
{
    if (!manager || !manager->initialized || !point_id) {
        return ESP_ERR_INVALID_ARG;
    }

    // Find point index
    int point_index = alarm_find_point_index(manager, point_id);
    if (point_index < 0) {
        return ESP_ERR_NOT_FOUND; // Point not monitored
    }

    // Update history buffer (thread-safe)
    if (xSemaphoreTake(manager->alarm_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        alarm_state_t* state = &manager->point_alarms[point_index];
        
        // Add value to history buffer (circular)
        state->last_values[state->history_index] = conditioned_value;
        state->history_index = (state->history_index + 1) % 20;
        
        if (state->history_count < 20) {
            state->history_count++;
        }

        xSemaphoreGive(manager->alarm_mutex);
    } else {
#ifdef DEBUG_ALARM_SYSTEM
        printf("[%s] Failed to acquire mutex for value update\n", TAG);
#endif
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t alarm_manager_check_point(alarm_manager_t* manager, const char* point_id)
{
    if (!manager || !manager->initialized || !point_id) {
        return ESP_ERR_INVALID_ARG;
    }

    // Find point index
    int point_index = alarm_find_point_index(manager, point_id);
    if (point_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    // Get alarm configuration
    io_point_config_t point_config;
    esp_err_t ret = config_manager_get_point_config(manager->config_manager, point_id, &point_config);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!point_config.alarm_config.enabled) {
        return ESP_OK; // Alarms disabled for this point
    }

    const alarm_config_t* alarm_config = &point_config.alarm_config;

    // Check each alarm type
    if (alarm_config->rules.check_rate_of_change) {
        alarm_check_rate_of_change(manager, point_index, alarm_config);
    }

    if (alarm_config->rules.check_disconnected) {
        alarm_check_disconnected(manager, point_index, alarm_config);
    }

    if (alarm_config->rules.check_max_value) {
        alarm_check_max_value(manager, point_index, alarm_config);
    }

    if (alarm_config->rules.check_stuck_signal) {
        alarm_check_stuck_signal(manager, point_index, alarm_config);
    }

    return ESP_OK;
}

esp_err_t alarm_manager_get_alarm_status(alarm_manager_t* manager, const char* point_id, 
                                        alarm_type_t alarm_type, bool* is_active)
{
    if (!manager || !manager->initialized || !point_id || !is_active || alarm_type >= ALARM_TYPE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    int point_index = alarm_find_point_index(manager, point_id);
    if (point_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(manager->alarm_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *is_active = manager->point_alarms[point_index].active[alarm_type];
        xSemaphoreGive(manager->alarm_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t alarm_manager_get_statistics(alarm_manager_t* manager, uint32_t* total_alarms,
                                      uint32_t* check_cycles, uint64_t* last_check_time)
{
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(manager->alarm_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (total_alarms) *total_alarms = manager->total_alarm_count;
        if (check_cycles) *check_cycles = manager->check_cycle_count;
        if (last_check_time) *last_check_time = manager->last_check_time;
        
        xSemaphoreGive(manager->alarm_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

void alarm_manager_destroy(alarm_manager_t* manager)
{
    if (!manager) {
        return;
    }

    // Stop monitoring task
    alarm_manager_stop_monitoring(manager);

    // Delete mutex
    if (manager->alarm_mutex) {
        vSemaphoreDelete(manager->alarm_mutex);
        manager->alarm_mutex = NULL;
    }

    // Clear structure
    memset(manager, 0, sizeof(alarm_manager_t));

#ifdef DEBUG_ALARM_SYSTEM
    printf("[%s] Alarm manager destroyed\n", TAG);
#endif
}

// Private functions

static void alarm_monitoring_task(void* pvParameters)
{
    alarm_manager_t* manager = (alarm_manager_t*)pvParameters;
    
#ifdef DEBUG_ALARM_SYSTEM
    printf("[%s] Alarm monitoring task started\n", TAG);
#endif

    while (manager->alarm_task_running) {
        // Check all monitored points
        for (int i = 0; i < manager->active_point_count; i++) {
            alarm_manager_check_point(manager, manager->point_ids[i]);
        }

        // Update statistics
        if (xSemaphoreTake(manager->alarm_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            manager->check_cycle_count++;
            manager->last_check_time = esp_timer_get_time();
            xSemaphoreGive(manager->alarm_mutex);
        }

        // Wait for next check interval
        vTaskDelay(pdMS_TO_TICKS(DEBUG_ALARM_CHECK_INTERVAL_MS));
    }

#ifdef DEBUG_ALARM_SYSTEM
    printf("[%s] Alarm monitoring task ended\n", TAG);
#endif

    vTaskDelete(NULL);
}

static esp_err_t alarm_check_rate_of_change(alarm_manager_t* manager, int point_index, const alarm_config_t* config)
{
    if (xSemaphoreTake(manager->alarm_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    alarm_state_t* state = &manager->point_alarms[point_index];
    
    // Need at least 2 samples for rate of change
    if (state->history_count < 2) {
        xSemaphoreGive(manager->alarm_mutex);
        return ESP_OK;
    }

    // Get current and previous values
    int current_idx = (state->history_index - 1 + 20) % 20;
    int previous_idx = (state->history_index - 2 + 20) % 20;
    
    float current_value = state->last_values[current_idx];
    float previous_value = state->last_values[previous_idx];
    
    float rate_of_change = fabsf(current_value - previous_value);
    
    if (rate_of_change > config->rules.rate_of_change_threshold) {
        // Alarm condition detected
        state->persistence_count[ALARM_TYPE_RATE_OF_CHANGE]++;
        
        if (state->persistence_count[ALARM_TYPE_RATE_OF_CHANGE] >= config->rules.alarm_persistence_samples) {
            if (!state->active[ALARM_TYPE_RATE_OF_CHANGE]) {
                alarm_activate(manager, point_index, ALARM_TYPE_RATE_OF_CHANGE);
            }
        }
    } else {
        // No alarm condition
        state->clear_count[ALARM_TYPE_RATE_OF_CHANGE]++;
        
        if (state->clear_count[ALARM_TYPE_RATE_OF_CHANGE] >= config->rules.samples_to_clear_alarm_condition) {
            if (state->active[ALARM_TYPE_RATE_OF_CHANGE]) {
                alarm_clear(manager, point_index, ALARM_TYPE_RATE_OF_CHANGE);
            }
            state->persistence_count[ALARM_TYPE_RATE_OF_CHANGE] = 0;
        }
    }

    xSemaphoreGive(manager->alarm_mutex);
    return ESP_OK;
}

static esp_err_t alarm_check_disconnected(alarm_manager_t* manager, int point_index, const alarm_config_t* config)
{
    if (xSemaphoreTake(manager->alarm_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    alarm_state_t* state = &manager->point_alarms[point_index];
    
    if (state->history_count < 1) {
        xSemaphoreGive(manager->alarm_mutex);
        return ESP_OK;
    }

    // Get current value
    int current_idx = (state->history_index - 1 + 20) % 20;
    float current_value = state->last_values[current_idx];
    
    if (current_value <= config->rules.disconnected_threshold) {
        // Alarm condition detected
        state->persistence_count[ALARM_TYPE_DISCONNECTED]++;
        
        if (state->persistence_count[ALARM_TYPE_DISCONNECTED] >= config->rules.alarm_persistence_samples) {
            if (!state->active[ALARM_TYPE_DISCONNECTED]) {
                alarm_activate(manager, point_index, ALARM_TYPE_DISCONNECTED);
            }
        }
    } else {
        // No alarm condition
        state->clear_count[ALARM_TYPE_DISCONNECTED]++;
        
        if (state->clear_count[ALARM_TYPE_DISCONNECTED] >= config->rules.samples_to_clear_alarm_condition) {
            if (state->active[ALARM_TYPE_DISCONNECTED]) {
                alarm_clear(manager, point_index, ALARM_TYPE_DISCONNECTED);
            }
            state->persistence_count[ALARM_TYPE_DISCONNECTED] = 0;
        }
    }

    xSemaphoreGive(manager->alarm_mutex);
    return ESP_OK;
}

static esp_err_t alarm_check_max_value(alarm_manager_t* manager, int point_index, const alarm_config_t* config)
{
    if (xSemaphoreTake(manager->alarm_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    alarm_state_t* state = &manager->point_alarms[point_index];
    
    if (state->history_count < 1) {
        xSemaphoreGive(manager->alarm_mutex);
        return ESP_OK;
    }

    // Get current value
    int current_idx = (state->history_index - 1 + 20) % 20;
    float current_value = state->last_values[current_idx];
    
    if (current_value >= config->rules.max_value_threshold) {
        // Alarm condition detected
        state->persistence_count[ALARM_TYPE_MAX_VALUE]++;
        
        if (state->persistence_count[ALARM_TYPE_MAX_VALUE] >= config->rules.alarm_persistence_samples) {
            if (!state->active[ALARM_TYPE_MAX_VALUE]) {
                alarm_activate(manager, point_index, ALARM_TYPE_MAX_VALUE);
            }
        }
    } else {
        // No alarm condition
        state->clear_count[ALARM_TYPE_MAX_VALUE]++;
        
        if (state->clear_count[ALARM_TYPE_MAX_VALUE] >= config->rules.samples_to_clear_alarm_condition) {
            if (state->active[ALARM_TYPE_MAX_VALUE]) {
                alarm_clear(manager, point_index, ALARM_TYPE_MAX_VALUE);
            }
            state->persistence_count[ALARM_TYPE_MAX_VALUE] = 0;
        }
    }

    xSemaphoreGive(manager->alarm_mutex);
    return ESP_OK;
}

static esp_err_t alarm_check_stuck_signal(alarm_manager_t* manager, int point_index, const alarm_config_t* config)
{
    if (xSemaphoreTake(manager->alarm_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    alarm_state_t* state = &manager->point_alarms[point_index];
    
    // Need enough samples for stuck signal detection
    int required_samples = config->rules.stuck_signal_window_samples;
    if (state->history_count < required_samples) {
        xSemaphoreGive(manager->alarm_mutex);
        return ESP_OK;
    }

    // Check if signal has been stuck within the window
    bool signal_stuck = true;
    float reference_value = state->last_values[(state->history_index - 1 + 20) % 20];
    
    for (int i = 1; i < required_samples; i++) {
        int idx = (state->history_index - 1 - i + 20) % 20;
        float value = state->last_values[idx];
        
        if (fabsf(value - reference_value) > config->rules.stuck_signal_delta_threshold) {
            signal_stuck = false;
            break;
        }
    }
    
    if (signal_stuck) {
        // Alarm condition detected
        state->persistence_count[ALARM_TYPE_STUCK_SIGNAL]++;
        
        if (state->persistence_count[ALARM_TYPE_STUCK_SIGNAL] >= config->rules.alarm_persistence_samples) {
            if (!state->active[ALARM_TYPE_STUCK_SIGNAL]) {
                alarm_activate(manager, point_index, ALARM_TYPE_STUCK_SIGNAL);
            }
        }
    } else {
        // No alarm condition
        state->clear_count[ALARM_TYPE_STUCK_SIGNAL]++;
        
        if (state->clear_count[ALARM_TYPE_STUCK_SIGNAL] >= config->rules.samples_to_clear_alarm_condition) {
            if (state->active[ALARM_TYPE_STUCK_SIGNAL]) {
                alarm_clear(manager, point_index, ALARM_TYPE_STUCK_SIGNAL);
            }
            state->persistence_count[ALARM_TYPE_STUCK_SIGNAL] = 0;
        }
    }

    xSemaphoreGive(manager->alarm_mutex);
    return ESP_OK;
}

static int alarm_find_point_index(alarm_manager_t* manager, const char* point_id)
{
    for (int i = 0; i < manager->active_point_count; i++) {
        if (strcmp(manager->point_ids[i], point_id) == 0) {
            return i;
        }
    }
    return -1;
}

static void alarm_activate(alarm_manager_t* manager, int point_index, alarm_type_t alarm_type)
{
    alarm_state_t* state = &manager->point_alarms[point_index];
    
    if (!state->active[alarm_type]) {
        state->active[alarm_type] = true;
        state->activation_count[alarm_type]++;
        state->activation_time[alarm_type] = esp_timer_get_time();
        state->trust_restored = false;
        manager->total_alarm_count++;

#ifdef DEBUG_ALARM_SYSTEM
        printf("[%s] ALARM ACTIVATED: Point '%s', Type %d\n", 
               TAG, manager->point_ids[point_index], alarm_type);
#endif
    }
}

static void alarm_clear(alarm_manager_t* manager, int point_index, alarm_type_t alarm_type)
{
    alarm_state_t* state = &manager->point_alarms[point_index];
    
    if (state->active[alarm_type]) {
        state->active[alarm_type] = false;
        state->clear_count[alarm_type] = 0;
        
        // Check if trust should be restored
        state->good_samples_count++;
        // Trust restoration logic would be implemented here based on configuration

#ifdef DEBUG_ALARM_SYSTEM
        printf("[%s] ALARM CLEARED: Point '%s', Type %d\n", 
               TAG, manager->point_ids[point_index], alarm_type);
#endif
    }
}
