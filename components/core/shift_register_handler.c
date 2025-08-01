/**
 * @file shift_register_handler.c
 * @brief Shift Register Handler implementation for SNRv9 Irrigation Control System
 * 
 * Provides hardware abstraction for 74HC595 (output) and 74HC165 (input)
 * shift register chains for IO expansion.
 */

#include "shift_register_handler.h"
#include "debug_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = DEBUG_SHIFT_REGISTER_TAG;

/**
 * @brief Validate chip and bit indices
 * 
 * @param chip_index Chip index to validate
 * @param bit_index Bit index to validate
 * @param max_chips Maximum number of chips
 * @return true if valid, false otherwise
 */
static bool validate_indices(int chip_index, int bit_index, int max_chips) {
    return (chip_index >= 0 && chip_index < max_chips && 
            bit_index >= 0 && bit_index < 8);
}

/**
 * @brief Configure GPIO pins for shift registers
 * 
 * @param handler Pointer to shift register handler structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
static esp_err_t configure_pins(shift_register_handler_t* handler) {
    esp_err_t ret = ESP_OK;
    
    // Configure output shift register pins
    if (handler->config.num_output_registers > 0) {
        // Clock pin
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << handler->config.output_clock_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) return ret;
        gpio_set_level(handler->config.output_clock_pin, 0);
        
        // Latch pin
        io_conf.pin_bit_mask = (1ULL << handler->config.output_latch_pin);
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) return ret;
        gpio_set_level(handler->config.output_latch_pin, 0);
        
        // Data pin
        io_conf.pin_bit_mask = (1ULL << handler->config.output_data_pin);
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) return ret;
        gpio_set_level(handler->config.output_data_pin, 0);
        
        // Output enable pin (optional)
        if (handler->config.output_enable_pin >= 0) {
            io_conf.pin_bit_mask = (1ULL << handler->config.output_enable_pin);
            ret = gpio_config(&io_conf);
            if (ret != ESP_OK) return ret;
            gpio_set_level(handler->config.output_enable_pin, 0); // Enable outputs (active low)
        }
    }
    
    // Configure input shift register pins
    if (handler->config.num_input_registers > 0) {
        // Clock pin
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << handler->config.input_clock_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) return ret;
        gpio_set_level(handler->config.input_clock_pin, 1); // Idle high
        
        // Load pin
        io_conf.pin_bit_mask = (1ULL << handler->config.input_load_pin);
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) return ret;
        gpio_set_level(handler->config.input_load_pin, 1); // Idle high
        
        // Data pin (input)
        io_conf.pin_bit_mask = (1ULL << handler->config.input_data_pin);
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) return ret;
    }
    
    return ESP_OK;
}

esp_err_t shift_register_handler_init(shift_register_handler_t* handler, const shift_register_config_t* config) {
    if (!handler || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->num_output_registers > SHIFT_REGISTER_MAX_CHIPS || 
        config->num_input_registers > SHIFT_REGISTER_MAX_CHIPS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize structure
    memset(handler, 0, sizeof(shift_register_handler_t));
    handler->config = *config;
    
    // Create mutex
    handler->mutex = xSemaphoreCreateMutex();
    if (!handler->mutex) {
#ifdef DEBUG_SHIFT_REGISTER
        ESP_LOGE(TAG, "Failed to create mutex");
#endif
        return ESP_ERR_NO_MEM;
    }
    
    // Configure GPIO pins
    esp_err_t ret = configure_pins(handler);
    if (ret != ESP_OK) {
#ifdef DEBUG_SHIFT_REGISTER
        ESP_LOGE(TAG, "Failed to configure pins: %s", esp_err_to_name(ret));
#endif
        vSemaphoreDelete(handler->mutex);
        return ret;
    }
    
    // Initialize all outputs to 0
    memset(handler->output_states, 0, sizeof(handler->output_states));
    
    // Perform initial write to clear outputs
    if (handler->config.num_output_registers > 0) {
        shift_register_write_outputs(handler);
    }
    
    handler->initialized = true;
    
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
        return ESP_OK; // Nothing to read
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    // Parallel load phase - capture all inputs
    gpio_set_level(handler->config.input_load_pin, 0);  // Start load
    vTaskDelay(pdMS_TO_TICKS(1));                       // Allow settling
    gpio_set_level(handler->config.input_load_pin, 1);  // Complete load
    vTaskDelay(pdMS_TO_TICKS(1));                       // Stabilization
    
    // Serial read phase - read from highest chip to lowest
    for (int chip = handler->config.num_input_registers - 1; chip >= 0; chip--) {
        uint8_t chip_value = 0;
        
        for (int bit = 0; bit < 8; bit++) {
            // Read data bit
            int bit_value = gpio_get_level(handler->config.input_data_pin);
            
            // Clock pulse to shift next bit
            gpio_set_level(handler->config.input_clock_pin, 0);
            vTaskDelay(pdMS_TO_TICKS(1));
            gpio_set_level(handler->config.input_clock_pin, 1);
            
            // Assemble byte (MSB first)
            chip_value = (chip_value << 1) | (bit_value ? 1 : 0);
        }
        
        handler->input_states[chip] = chip_value;
        
#ifdef DEBUG_SHIFT_REGISTER_VERBOSE
        ESP_LOGI(TAG, "Read input chip %d: 0x%02X", chip, chip_value);
#endif
    }
    
    handler->read_count++;
    xSemaphoreGive(handler->mutex);
    
#ifdef DEBUG_SHIFT_REGISTER
    ESP_LOGI(TAG, "Read %d input registers", handler->config.num_input_registers);
#endif
    
    return ESP_OK;
}

esp_err_t shift_register_write_outputs(shift_register_handler_t* handler) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (handler->config.num_output_registers == 0) {
        return ESP_OK; // Nothing to write
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    // Prepare latch (disable outputs during update)
    gpio_set_level(handler->config.output_latch_pin, 0);
    
    // Serial write phase - write from highest chip to lowest
    for (int chip = handler->config.num_output_registers - 1; chip >= 0; chip--) {
        uint8_t chip_value = handler->output_states[chip];
        
        for (int bit = 7; bit >= 0; bit--) {
            // Set data bit (MSB first)
            int bit_value = (chip_value >> bit) & 1;
            gpio_set_level(handler->config.output_data_pin, bit_value);
            
            // Clock pulse to shift bit
            gpio_set_level(handler->config.output_clock_pin, 1);
            vTaskDelay(pdMS_TO_TICKS(1));
            gpio_set_level(handler->config.output_clock_pin, 0);
        }
        
#ifdef DEBUG_SHIFT_REGISTER_VERBOSE
        ESP_LOGI(TAG, "Wrote output chip %d: 0x%02X", chip, chip_value);
#endif
    }
    
    // Latch data (enable outputs)
    gpio_set_level(handler->config.output_latch_pin, 1);
    
    handler->write_count++;
    xSemaphoreGive(handler->mutex);
    
#ifdef DEBUG_SHIFT_REGISTER
    ESP_LOGI(TAG, "Wrote %d output registers", handler->config.num_output_registers);
#endif
    
    return ESP_OK;
}

esp_err_t shift_register_set_output_bit(shift_register_handler_t* handler, int chip_index, int bit_index, bool state) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!validate_indices(chip_index, bit_index, handler->config.num_output_registers)) {
        handler->error_count++;
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
    
#ifdef DEBUG_SHIFT_REGISTER_VERBOSE
    ESP_LOGI(TAG, "Set output bit [%d:%d] = %s", chip_index, bit_index, state ? "HIGH" : "LOW");
#endif
    
    return ESP_OK;
}

esp_err_t shift_register_get_output_bit(shift_register_handler_t* handler, int chip_index, int bit_index, bool* state) {
    if (!handler || !handler->initialized || !state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!validate_indices(chip_index, bit_index, handler->config.num_output_registers)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    *state = (handler->output_states[chip_index] >> bit_index) & 1;
    
    xSemaphoreGive(handler->mutex);
    
    return ESP_OK;
}

esp_err_t shift_register_get_input_bit(shift_register_handler_t* handler, int chip_index, int bit_index, bool* state) {
    if (!handler || !handler->initialized || !state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!validate_indices(chip_index, bit_index, handler->config.num_input_registers)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    *state = (handler->input_states[chip_index] >> bit_index) & 1;
    
    xSemaphoreGive(handler->mutex);
    
    return ESP_OK;
}

esp_err_t shift_register_set_output_byte(shift_register_handler_t* handler, int chip_index, uint8_t value) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (chip_index < 0 || chip_index >= handler->config.num_output_registers) {
        handler->error_count++;
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        handler->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    handler->output_states[chip_index] = value;
    
    xSemaphoreGive(handler->mutex);
    
#ifdef DEBUG_SHIFT_REGISTER_VERBOSE
    ESP_LOGI(TAG, "Set output byte [%d] = 0x%02X", chip_index, value);
#endif
    
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
