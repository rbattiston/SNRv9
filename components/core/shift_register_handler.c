/**
 * @file shift_register_handler.c
 * @brief Shift Register Handler implementation for SNRv9 Irrigation Control System
 */

#include "shift_register_handler.h"
#include "debug_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = DEBUG_SHIFT_REGISTER_TAG;

esp_err_t shift_register_handler_init(shift_register_handler_t* handler, const shift_register_config_t* config) {
    if (!handler || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize structure
    memset(handler, 0, sizeof(shift_register_handler_t));
    handler->config = *config;
    
    // Create mutex
    handler->mutex = xSemaphoreCreateMutex();
    if (!handler->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Configure GPIO pins for output shift registers
    if (config->num_output_registers > 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << config->output_clock_pin) | 
                           (1ULL << config->output_latch_pin) | 
                           (1ULL << config->output_data_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure output GPIO pins: %s", esp_err_to_name(ret));
            vSemaphoreDelete(handler->mutex);
            return ret;
        }
        
        // Configure enable pin if specified
        if (config->output_enable_pin >= 0) {
            gpio_config_t enable_conf = {
                .pin_bit_mask = (1ULL << config->output_enable_pin),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE
            };
            
            ret = gpio_config(&enable_conf);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to configure output enable pin: %s", esp_err_to_name(ret));
                vSemaphoreDelete(handler->mutex);
                return ret;
            }
            
            // SAFETY FIRST: Disable outputs during initialization (active low, so HIGH = disabled)
            gpio_set_level(config->output_enable_pin, 1);
        }
        
        // Initialize output pins
        gpio_set_level(config->output_clock_pin, 0);
        gpio_set_level(config->output_latch_pin, 0);
        gpio_set_level(config->output_data_pin, 0);
        
        // CRITICAL SAFETY: Initialize all outputs to safe state (OFF) following reference example
        // This ensures hardware matches software state before enabling outputs
        memset(handler->output_states, 0, sizeof(handler->output_states));
        
        // CRITICAL FIX: Set initialized flag BEFORE calling write function to avoid circular dependency
        handler->initialized = true;
        
        // Write safe state to hardware immediately (like Send_74HC595(0) in reference)
        esp_err_t write_ret = shift_register_write_outputs(handler);
        if (write_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write safe state to shift registers: %s", esp_err_to_name(write_ret));
            handler->initialized = false;  // Reset on failure
            vSemaphoreDelete(handler->mutex);
            return write_ret;
        }
        
        // NOW it's safe to enable outputs (following reference pattern)
        if (config->output_enable_pin >= 0) {
            gpio_set_level(config->output_enable_pin, 0);  // LOW = enabled
            
#ifdef DEBUG_SHIFT_REGISTER
            ESP_LOGI(TAG, "Shift register outputs enabled after safe state initialization");
#endif
        }
    }
    
    // Configure GPIO pins for input shift registers
    if (config->num_input_registers > 0) {
        // Clock and load pins as outputs
        gpio_config_t clock_load_conf = {
            .pin_bit_mask = (1ULL << config->input_clock_pin) | 
                           (1ULL << config->input_load_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        
        esp_err_t ret = gpio_config(&clock_load_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure input clock/load pins: %s", esp_err_to_name(ret));
            vSemaphoreDelete(handler->mutex);
            return ret;
        }
        
        // Data pin as input
        gpio_config_t data_conf = {
            .pin_bit_mask = (1ULL << config->input_data_pin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        
        ret = gpio_config(&data_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure input data pin: %s", esp_err_to_name(ret));
            vSemaphoreDelete(handler->mutex);
            return ret;
        }
        
        // Initialize input pins
        gpio_set_level(config->input_clock_pin, 1);  // Clock idle high
        gpio_set_level(config->input_load_pin, 1);   // Load idle high
        
        // Set initialized flag if not already set (for input-only configurations)
        if (!handler->initialized) {
            handler->initialized = true;
        }
    } else {
        // For output-only configurations, ensure initialized flag is set
        if (!handler->initialized) {
            handler->initialized = true;
        }
    }
    
#ifdef DEBUG_SHIFT_REGISTER
    ESP_LOGI(TAG, "Shift register handler initialized (out: %d, in: %d)", 
             config->num_output_registers, config->num_input_registers);
#endif
    
    return ESP_OK;
}

esp_err_t shift_register_read_inputs(shift_register_handler_t* handler) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (handler->config.num_input_registers == 0) {
        return ESP_OK; // No input registers configured
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    // Parallel load - capture all inputs
    gpio_set_level(handler->config.input_load_pin, 0);
    esp_rom_delay_us(5); // 5μs delay for load
    gpio_set_level(handler->config.input_load_pin, 1);
    esp_rom_delay_us(5); // 5μs delay for stabilization
    
    // Serial read from all registers
    for (int chip = handler->config.num_input_registers - 1; chip >= 0; chip--) {
        uint8_t byte_value = 0;
        
        for (int bit = 7; bit >= 0; bit--) {
            // Clock low
            gpio_set_level(handler->config.input_clock_pin, 0);
            esp_rom_delay_us(1); // 1μs delay
            
            // Read data bit
            int bit_value = gpio_get_level(handler->config.input_data_pin);
            if (bit_value) {
                byte_value |= (1 << bit);
            }
            
            // Clock high
            gpio_set_level(handler->config.input_clock_pin, 1);
            esp_rom_delay_us(1); // 1μs delay
        }
        
        handler->input_states[chip] = byte_value;
    }
    
    handler->read_count++;
    xSemaphoreGive(handler->mutex);
    
    return ESP_OK;
}

esp_err_t shift_register_write_outputs(shift_register_handler_t* handler) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (handler->config.num_output_registers == 0) {
        return ESP_OK; // No output registers configured
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    // Latch low to prepare for data
    gpio_set_level(handler->config.output_latch_pin, 0);
    
    // Serial write to all registers (MSB first, highest chip first)
    for (int chip = handler->config.num_output_registers - 1; chip >= 0; chip--) {
        uint8_t byte_value = handler->output_states[chip];
        
        for (int bit = 7; bit >= 0; bit--) {
            // Set data bit
            int bit_value = (byte_value >> bit) & 0x01;
            gpio_set_level(handler->config.output_data_pin, bit_value);
            esp_rom_delay_us(1); // 1μs setup time
            
            // Clock pulse
            gpio_set_level(handler->config.output_clock_pin, 1);
            esp_rom_delay_us(1); // 1μs high time
            gpio_set_level(handler->config.output_clock_pin, 0);
            esp_rom_delay_us(1); // 1μs low time
        }
    }
    
    // Latch high to update outputs
    gpio_set_level(handler->config.output_latch_pin, 1);
    esp_rom_delay_us(5); // 5μs delay for latch
    
    handler->write_count++;
    xSemaphoreGive(handler->mutex);
    
    return ESP_OK;
}

esp_err_t shift_register_set_output_bit(shift_register_handler_t* handler, int chip_index, int bit_index, bool state) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (chip_index < 0 || chip_index >= handler->config.num_output_registers || 
        bit_index < 0 || bit_index >= 8) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    if (state) {
        handler->output_states[chip_index] |= (1 << bit_index);
    } else {
        handler->output_states[chip_index] &= ~(1 << bit_index);
    }
    
    xSemaphoreGive(handler->mutex);
    
    return ESP_OK;
}

esp_err_t shift_register_get_output_bit(shift_register_handler_t* handler, int chip_index, int bit_index, bool* state) {
    if (!handler || !handler->initialized || !state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (chip_index < 0 || chip_index >= handler->config.num_output_registers || 
        bit_index < 0 || bit_index >= 8) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    *state = (handler->output_states[chip_index] >> bit_index) & 0x01;
    
    xSemaphoreGive(handler->mutex);
    
    return ESP_OK;
}

esp_err_t shift_register_get_input_bit(shift_register_handler_t* handler, int chip_index, int bit_index, bool* state) {
    if (!handler || !handler->initialized || !state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (chip_index < 0 || chip_index >= handler->config.num_input_registers || 
        bit_index < 0 || bit_index >= 8) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    *state = (handler->input_states[chip_index] >> bit_index) & 0x01;
    
    xSemaphoreGive(handler->mutex);
    
    return ESP_OK;
}

esp_err_t shift_register_set_output_byte(shift_register_handler_t* handler, int chip_index, uint8_t value) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (chip_index < 0 || chip_index >= handler->config.num_output_registers) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    handler->output_states[chip_index] = value;
    
    xSemaphoreGive(handler->mutex);
    
    return ESP_OK;
}

esp_err_t shift_register_get_output_byte(shift_register_handler_t* handler, int chip_index, uint8_t* value) {
    if (!handler || !handler->initialized || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (chip_index < 0 || chip_index >= handler->config.num_output_registers) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    *value = handler->output_states[chip_index];
    
    xSemaphoreGive(handler->mutex);
    
    return ESP_OK;
}

esp_err_t shift_register_get_input_byte(shift_register_handler_t* handler, int chip_index, uint8_t* value) {
    if (!handler || !handler->initialized || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (chip_index < 0 || chip_index >= handler->config.num_input_registers) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    *value = handler->input_states[chip_index];
    
    xSemaphoreGive(handler->mutex);
    
    return ESP_OK;
}

esp_err_t shift_register_get_statistics(shift_register_handler_t* handler, uint32_t* reads, uint32_t* writes, uint32_t* errors) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (reads) *reads = handler->read_count;
    if (writes) *writes = handler->write_count;
    if (errors) *errors = handler->error_count;
    
    return ESP_OK;
}

void shift_register_handler_destroy(shift_register_handler_t* handler) {
    if (handler && handler->initialized) {
        if (handler->mutex) {
            vSemaphoreDelete(handler->mutex);
            handler->mutex = NULL;
        }
        
        handler->initialized = false;
        
#ifdef DEBUG_SHIFT_REGISTER
        ESP_LOGI(TAG, "Shift register handler destroyed (reads: %lu, writes: %lu, errors: %lu)", 
                 handler->read_count, handler->write_count, handler->error_count);
#endif
    }
}
