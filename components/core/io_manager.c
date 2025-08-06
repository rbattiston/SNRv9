/**
 * @file io_manager.c
 * @brief IO Manager implementation for SNRv9 Irrigation Control System
 */

#include "io_manager.h"
#include "debug_config.h"
#include "psram_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char* TAG = DEBUG_IO_MANAGER_TAG;

/**
 * @brief Find IO point index by ID
 */
static int find_point_index(io_manager_t* manager, const char* point_id) {
    for (int i = 0; i < manager->active_point_count; i++) {
        if (strcmp(manager->point_ids[i], point_id) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Apply signal conditioning to raw value
 */
static float apply_signal_conditioning(const io_point_config_t* config, 
                                     io_point_runtime_state_t* state, 
                                     float raw_value) {
    if (!config->signal_config.enabled) {
        return raw_value;
    }
    
    float conditioned = raw_value;
    
    // Apply offset
    conditioned += config->signal_config.offset;
    
    // Apply gain
    conditioned *= config->signal_config.gain;
    
    // Apply scaling factor
    conditioned *= config->signal_config.scaling_factor;
    
    // Apply SMA filtering if enabled
    if (config->signal_config.filter_type == SIGNAL_FILTER_SMA && 
        config->signal_config.sma_window_size > 0) {
        
        int window_size = config->signal_config.sma_window_size;
        if (window_size > 32) window_size = 32; // Limit to buffer size
        
        // Add new sample to circular buffer
        state->sma_buffer[state->sma_index] = conditioned;
        
        // Update sum
        if (state->sma_count < window_size) {
            state->sma_sum += conditioned;
            state->sma_count++;
        } else {
            // Remove oldest sample and add new one
            int old_index = (state->sma_index + 1) % window_size;
            state->sma_sum = state->sma_sum - state->sma_buffer[old_index] + conditioned;
        }
        
        // Update index
        state->sma_index = (state->sma_index + 1) % window_size;
        
        // Calculate average
        if (state->sma_count > 0) {
            conditioned = state->sma_sum / state->sma_count;
        }
    }
    
    // Apply precision rounding
    if (config->signal_config.precision_digits >= 0) {
        float multiplier = powf(10.0f, config->signal_config.precision_digits);
        conditioned = roundf(conditioned * multiplier) / multiplier;
    }
    
    return conditioned;
}


/**
 * @brief Configure IO points from configuration
 */
static esp_err_t configure_io_points(io_manager_t* manager) {
    ESP_LOGI(TAG, "Starting IO point configuration...");
    manager->active_point_count = 0;
    
    // Allocate configuration array in PSRAM to avoid stack overflow
    size_t config_array_size = CONFIG_MAX_IO_POINTS * sizeof(io_point_config_t);
    ESP_LOGI(TAG, "Allocating %zu bytes in PSRAM for IO configuration array", config_array_size);
    
    io_point_config_t* configs = psram_smart_malloc(config_array_size, ALLOC_LARGE_BUFFER);
    if (!configs) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes in PSRAM for IO config", config_array_size);
        return ESP_ERR_NO_MEM;
    }
    
    int config_count = 0;
    
    ESP_LOGI(TAG, "Requesting IO points from configuration manager...");
    esp_err_t ret = config_manager_get_all_io_points(manager->config_manager, 
                                                    configs, CONFIG_MAX_IO_POINTS, 
                                                    &config_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get IO points from config manager: %s", esp_err_to_name(ret));
        psram_smart_free(configs);
        return ret;
    }
    
    ESP_LOGI(TAG, "Configuration manager returned %d IO points", config_count);
    
    if (config_count == 0) {
        ESP_LOGW(TAG, "No IO points found in configuration!");
        return ESP_OK; // Not an error, just no points configured
    }
    
    // Configure each IO point
    for (int i = 0; i < config_count && i < IO_MANAGER_MAX_POINTS; i++) {
        const io_point_config_t* config = &configs[i];
        
        ESP_LOGI(TAG, "Configuring IO point [%d]: %s (type: %d, pin: %d)", 
                 i, config->id, config->type, config->pin);
        
        // Store point ID mapping
        strncpy(manager->point_ids[i], config->id, CONFIG_MAX_ID_LENGTH - 1);
        manager->point_ids[i][CONFIG_MAX_ID_LENGTH - 1] = '\0';
        
        // Initialize runtime state
        io_point_runtime_state_t* state = &manager->runtime_states[i];
        memset(state, 0, sizeof(io_point_runtime_state_t));
        
        // Configure hardware based on type
        switch (config->type) {
            case IO_POINT_TYPE_GPIO_AI:
                if (config->pin >= 0) {
                    ESP_LOGI(TAG, "  Configuring GPIO analog input on pin %d", config->pin);
                    gpio_handler_configure_analog(&manager->gpio_handler, config->pin);
                } else {
                    ESP_LOGW(TAG, "  Invalid pin %d for GPIO AI point %s", config->pin, config->id);
                }
                break;
                
            case IO_POINT_TYPE_GPIO_BI:
                if (config->pin >= 0) {
                    ESP_LOGI(TAG, "  Configuring GPIO binary input on pin %d", config->pin);
                    gpio_handler_configure_input(&manager->gpio_handler, config->pin, true);
                } else {
                    ESP_LOGW(TAG, "  Invalid pin %d for GPIO BI point %s", config->pin, config->id);
                }
                break;
                
            case IO_POINT_TYPE_GPIO_BO:
                if (config->pin >= 0) {
                    // SAFETY: Always start with safe state (OFF), gpio_handler now enforces this
                    ESP_LOGI(TAG, "  Configuring GPIO binary output on pin %d (SAFE INIT)", config->pin);
                    gpio_handler_configure_output(&manager->gpio_handler, config->pin, false);  // Always start OFF
                    
                    // Initialize runtime state to match safe hardware state
                    state->digital_state = false;  // OFF state
                    state->raw_value = 0.0f;
                    state->conditioned_value = 0.0f;
                } else {
                    ESP_LOGW(TAG, "  Invalid pin %d for GPIO BO point %s", config->pin, config->id);
                }
                break;
                
            case IO_POINT_TYPE_SHIFT_REG_BI:
                ESP_LOGI(TAG, "  Configuring shift register binary input (chip: %d, bit: %d)", 
                         config->chip_index, config->bit_index);
                break;
                
            case IO_POINT_TYPE_SHIFT_REG_BO:
                ESP_LOGI(TAG, "  Configuring shift register binary output (chip: %d, bit: %d) (SAFE INIT)", 
                         config->chip_index, config->bit_index);
                
                // SAFETY: Initialize shift register output to safe state (OFF)
                // The shift register handler already initializes all outputs to 0 during init
                // Just ensure runtime state matches the safe hardware state
                state->digital_state = false;  // OFF state
                state->raw_value = 0.0f;
                state->conditioned_value = 0.0f;
                break;
                
            default:
                ESP_LOGW(TAG, "  Unknown IO point type: %d", config->type);
                break;
        }
        
        manager->active_point_count++;
    }
    
    ESP_LOGI(TAG, "IO point configuration complete: %d points configured", manager->active_point_count);
    
    // Free PSRAM allocation
    psram_smart_free(configs);
    ESP_LOGI(TAG, "PSRAM configuration array freed");
    
    return ESP_OK;
}

/**
 * @brief Update analog input point
 */
static esp_err_t update_analog_input(io_manager_t* manager, int point_index) {
    io_point_config_t config;
    esp_err_t ret = config_manager_get_io_point_config(manager->config_manager, 
                                                      manager->point_ids[point_index], 
                                                      &config);
    if (ret != ESP_OK) {
        return ret;
    }
    
    io_point_runtime_state_t* state = &manager->runtime_states[point_index];
    
    // Read ADC value using GPIO handler
    int adc_raw = 0;
    ret = gpio_handler_read_analog(&manager->gpio_handler, config.pin, &adc_raw);
    if (ret != ESP_OK) {
        state->error_state = true;
        state->error_count++;
        return ret;
    }
    
    // Convert raw ADC value to engineering units
    float raw_value = (float)adc_raw;
    
    // Scale to engineering units based on range
    float range_span = config.range_max - config.range_min;
    float normalized = raw_value / 4095.0f; // Normalize to 0-1
    raw_value = config.range_min + (normalized * range_span);
    
    // Apply signal conditioning
    float conditioned_value = apply_signal_conditioning(&config, state, raw_value);
    
    // Update state
    state->raw_value = raw_value;
    state->conditioned_value = conditioned_value;
    state->error_state = false;
    state->last_update_time = esp_timer_get_time();
    state->update_count++;
    
    return ESP_OK;
}

/**
 * @brief Update binary input point
 */
static esp_err_t update_binary_input(io_manager_t* manager, int point_index) {
    io_point_config_t config;
    esp_err_t ret = config_manager_get_io_point_config(manager->config_manager, 
                                                      manager->point_ids[point_index], 
                                                      &config);
    if (ret != ESP_OK) {
        return ret;
    }
    
    io_point_runtime_state_t* state = &manager->runtime_states[point_index];
    bool digital_state = false;
    
    if (config.type == IO_POINT_TYPE_GPIO_BI) {
        // Read GPIO
        ret = gpio_handler_read_digital(&manager->gpio_handler, config.pin, &digital_state);
    } else if (config.type == IO_POINT_TYPE_SHIFT_REG_BI) {
        // Read shift register
        ret = shift_register_get_input_bit(&manager->shift_register_handler, 
                                          config.chip_index, config.bit_index, 
                                          &digital_state);
    }
    
    if (ret != ESP_OK) {
        state->error_state = true;
        state->error_count++;
        return ret;
    }
    
    // Apply inversion if configured
    if (config.is_inverted) {
        digital_state = !digital_state;
    }
    
    // Update state
    state->digital_state = digital_state;
    state->raw_value = digital_state ? 1.0f : 0.0f;
    state->conditioned_value = state->raw_value;
    state->error_state = false;
    state->last_update_time = esp_timer_get_time();
    state->update_count++;
    
    return ESP_OK;
}

/**
 * @brief IO polling task
 */
static void io_polling_task(void* parameter) {
    io_manager_t* manager = (io_manager_t*)parameter;
    TickType_t last_wake_time = xTaskGetTickCount();
    
#ifdef DEBUG_IO_MANAGER
    ESP_LOGI(TAG, "IO polling task started");
#endif
    
    while (manager->polling_task_running) {
        // Take mutex for state access
        if (xSemaphoreTake(manager->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            // Read shift register inputs first
            shift_register_read_inputs(&manager->shift_register_handler);
            
            // Update all input points
            for (int i = 0; i < manager->active_point_count; i++) {
                io_point_config_t config;
                if (config_manager_get_io_point_config(manager->config_manager, 
                                                      manager->point_ids[i], 
                                                      &config) == ESP_OK) {
                    
                    switch (config.type) {
                        case IO_POINT_TYPE_GPIO_AI:
                            update_analog_input(manager, i);
                            break;
                            
                        case IO_POINT_TYPE_GPIO_BI:
                        case IO_POINT_TYPE_SHIFT_REG_BI:
                            update_binary_input(manager, i);
                            break;
                            
                        default:
                            // Output points don't need updating
                            break;
                    }
                }
            }
            
            manager->update_cycle_count++;
            manager->last_update_time = esp_timer_get_time();
            
            xSemaphoreGive(manager->state_mutex);
        }
        
        // Wait for next polling interval
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(1000)); // 1 second default
    }
    
#ifdef DEBUG_IO_MANAGER
    ESP_LOGI(TAG, "IO polling task stopped");
#endif
    
    vTaskDelete(NULL);
}

esp_err_t io_manager_init(io_manager_t* manager, config_manager_t* config_manager) {
    if (!manager || !config_manager) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize structure
    memset(manager, 0, sizeof(io_manager_t));
    manager->config_manager = config_manager;
    
    // Create state mutex
    manager->state_mutex = xSemaphoreCreateMutex();
    if (!manager->state_mutex) {
#ifdef DEBUG_IO_MANAGER
        ESP_LOGE(TAG, "Failed to create state mutex");
#endif
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize GPIO handler
    esp_err_t ret = gpio_handler_init(&manager->gpio_handler);
    if (ret != ESP_OK) {
#ifdef DEBUG_IO_MANAGER
        ESP_LOGE(TAG, "Failed to initialize GPIO handler: %s", esp_err_to_name(ret));
#endif
        vSemaphoreDelete(manager->state_mutex);
        return ret;
    }
    
    // Get shift register configuration and initialize if needed
    shift_register_config_t sr_config;
    ret = config_manager_get_shift_register_config(config_manager, &sr_config);
    if (ret == ESP_OK && (sr_config.num_input_registers > 0 || sr_config.num_output_registers > 0)) {
        ret = shift_register_handler_init(&manager->shift_register_handler, &sr_config);
        if (ret != ESP_OK) {
#ifdef DEBUG_IO_MANAGER
            ESP_LOGE(TAG, "Failed to initialize shift register handler: %s", esp_err_to_name(ret));
#endif
            gpio_handler_destroy(&manager->gpio_handler);
            vSemaphoreDelete(manager->state_mutex);
            return ret;
        }
    }
    
    
    // Configure IO points
    ret = configure_io_points(manager);
    if (ret != ESP_OK) {
#ifdef DEBUG_IO_MANAGER
        ESP_LOGE(TAG, "Failed to configure IO points: %s", esp_err_to_name(ret));
#endif
        shift_register_handler_destroy(&manager->shift_register_handler);
        gpio_handler_destroy(&manager->gpio_handler);
        vSemaphoreDelete(manager->state_mutex);
        return ret;
    }
    
    manager->initialized = true;
    
#ifdef DEBUG_IO_MANAGER
    ESP_LOGI(TAG, "IO Manager initialized with %d points", manager->active_point_count);
#endif
    
    return ESP_OK;
}

esp_err_t io_manager_start_polling(io_manager_t* manager, uint32_t polling_interval_ms, 
                                  UBaseType_t task_priority, uint32_t task_stack_size) {
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (manager->polling_task_running) {
        return ESP_ERR_INVALID_STATE; // Already running
    }
    
    manager->polling_task_running = true;
    
    BaseType_t result = xTaskCreate(io_polling_task, "io_polling", 
                                   task_stack_size, manager, 
                                   task_priority, &manager->polling_task_handle);
    
    if (result != pdPASS) {
        manager->polling_task_running = false;
#ifdef DEBUG_IO_MANAGER
        ESP_LOGE(TAG, "Failed to create polling task");
#endif
        return ESP_ERR_NO_MEM;
    }
    
#ifdef DEBUG_IO_MANAGER
    ESP_LOGI(TAG, "IO polling task started (interval: %lu ms)", polling_interval_ms);
#endif
    
    return ESP_OK;
}

esp_err_t io_manager_stop_polling(io_manager_t* manager) {
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!manager->polling_task_running) {
        return ESP_OK; // Already stopped
    }
    
    manager->polling_task_running = false;
    
    // Wait for task to finish
    if (manager->polling_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100)); // Give task time to exit
        manager->polling_task_handle = NULL;
    }
    
#ifdef DEBUG_IO_MANAGER
    ESP_LOGI(TAG, "IO polling task stopped");
#endif
    
    return ESP_OK;
}

esp_err_t io_manager_set_binary_output(io_manager_t* manager, const char* point_id, bool state) {
    if (!manager || !manager->initialized || !point_id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get point configuration
    io_point_config_t config;
    esp_err_t ret = config_manager_get_io_point_config(manager->config_manager, point_id, &config);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Verify it's a binary output
    if (config.type != IO_POINT_TYPE_GPIO_BO && config.type != IO_POINT_TYPE_SHIFT_REG_BO) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Apply inversion if configured
    bool hardware_state = config.is_inverted ? !state : state;
    
    // Set hardware state
    if (config.type == IO_POINT_TYPE_GPIO_BO) {
        ret = gpio_handler_write_digital(&manager->gpio_handler, config.pin, hardware_state);
    } else if (config.type == IO_POINT_TYPE_SHIFT_REG_BO) {
        ret = shift_register_set_output_bit(&manager->shift_register_handler, 
                                           config.chip_index, config.bit_index, 
                                           hardware_state);
        if (ret == ESP_OK) {
            // Write to hardware
            ret = shift_register_write_outputs(&manager->shift_register_handler);
        }
    }
    
    if (ret == ESP_OK) {
        // Update runtime state
        int point_index = find_point_index(manager, point_id);
        if (point_index >= 0) {
            if (xSemaphoreTake(manager->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                io_point_runtime_state_t* runtime_state = &manager->runtime_states[point_index];
                runtime_state->digital_state = state;
                runtime_state->raw_value = state ? 1.0f : 0.0f;
                runtime_state->conditioned_value = runtime_state->raw_value;
                runtime_state->last_update_time = esp_timer_get_time();
                runtime_state->update_count++;
                xSemaphoreGive(manager->state_mutex);
            }
        }
    }
    
    return ret;
}

esp_err_t io_manager_get_binary_output(io_manager_t* manager, const char* point_id, bool* state) {
    if (!manager || !manager->initialized || !point_id || !state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int point_index = find_point_index(manager, point_id);
    if (point_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (xSemaphoreTake(manager->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *state = manager->runtime_states[point_index].digital_state;
        xSemaphoreGive(manager->state_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t io_manager_get_analog_conditioned(io_manager_t* manager, const char* point_id, float* value) {
    if (!manager || !manager->initialized || !point_id || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int point_index = find_point_index(manager, point_id);
    if (point_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (xSemaphoreTake(manager->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *value = manager->runtime_states[point_index].conditioned_value;
        xSemaphoreGive(manager->state_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t io_manager_get_statistics(io_manager_t* manager, uint32_t* update_cycles, 
                                   uint32_t* total_errors, uint64_t* last_update_time) {
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (update_cycles) *update_cycles = manager->update_cycle_count;
    if (total_errors) *total_errors = manager->total_error_count;
    if (last_update_time) *last_update_time = manager->last_update_time;
    
    return ESP_OK;
}

void io_manager_destroy(io_manager_t* manager) {
    if (manager && manager->initialized) {
        // Stop polling task
        io_manager_stop_polling(manager);
        
        // Cleanup handlers
        shift_register_handler_destroy(&manager->shift_register_handler);
        gpio_handler_destroy(&manager->gpio_handler);
        
        
        // Cleanup mutex
        if (manager->state_mutex) {
            vSemaphoreDelete(manager->state_mutex);
            manager->state_mutex = NULL;
        }
        
        manager->initialized = false;
        
#ifdef DEBUG_IO_MANAGER
        ESP_LOGI(TAG, "IO Manager destroyed");
#endif
    }
}

// Simplified implementations for remaining functions
esp_err_t io_manager_update_inputs(io_manager_t* manager) {
    // This would be called by the polling task
    return ESP_OK;
}

esp_err_t io_manager_get_binary_input(io_manager_t* manager, const char* point_id, bool* state) {
    return io_manager_get_binary_output(manager, point_id, state); // Same logic
}

esp_err_t io_manager_get_analog_raw(io_manager_t* manager, const char* point_id, float* value) {
    if (!manager || !manager->initialized || !point_id || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int point_index = find_point_index(manager, point_id);
    if (point_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (xSemaphoreTake(manager->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *value = manager->runtime_states[point_index].raw_value;
        xSemaphoreGive(manager->state_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t io_manager_get_runtime_state(io_manager_t* manager, const char* point_id, io_point_runtime_state_t* state) {
    if (!manager || !manager->initialized || !point_id || !state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int point_index = find_point_index(manager, point_id);
    if (point_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (xSemaphoreTake(manager->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *state = manager->runtime_states[point_index];
        xSemaphoreGive(manager->state_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t io_manager_get_all_point_ids(io_manager_t* manager, char point_ids[][CONFIG_MAX_ID_LENGTH], 
                                      int max_points, int* actual_count) {
    if (!manager || !manager->initialized || !point_ids || !actual_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int count = (manager->active_point_count < max_points) ? manager->active_point_count : max_points;
    
    for (int i = 0; i < count; i++) {
        strncpy(point_ids[i], manager->point_ids[i], CONFIG_MAX_ID_LENGTH - 1);
        point_ids[i][CONFIG_MAX_ID_LENGTH - 1] = '\0';
    }
    
    *actual_count = count;
    return ESP_OK;
}

esp_err_t io_manager_reload_config(io_manager_t* manager) {
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Stop polling during reconfiguration
    bool was_polling = manager->polling_task_running;
    if (was_polling) {
        io_manager_stop_polling(manager);
    }
    
    // Reconfigure IO points
    esp_err_t ret = configure_io_points(manager);
    
    // Restart polling if it was running
    if (was_polling && ret == ESP_OK) {
        io_manager_start_polling(manager, 1000, 2, 4096);
    }
    
    return ret;
}
