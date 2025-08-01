/**
 * @file gpio_handler.c
 * @brief GPIO Handler implementation for SNRv9 Irrigation Control System
 * 
 * Provides hardware abstraction for ESP32 GPIO operations including
 * digital input/output and analog input (ADC) functionality.
 */

#include "gpio_handler.h"
#include "debug_config.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = DEBUG_GPIO_HANDLER_TAG;

/**
 * @brief Pin to ADC channel mapping for ESP32
 * 
 * @param pin GPIO pin number
 * @return adc1_channel_t ADC channel or -1 if invalid
 */
static adc1_channel_t pin_to_adc_channel(int pin) {
    switch (pin) {
        case 36: return ADC1_CHANNEL_0;
        case 37: return ADC1_CHANNEL_1;
        case 38: return ADC1_CHANNEL_2;
        case 39: return ADC1_CHANNEL_3;
        case 32: return ADC1_CHANNEL_4;
        case 33: return ADC1_CHANNEL_5;
        case 34: return ADC1_CHANNEL_6;
        case 35: return ADC1_CHANNEL_7;
        default: return -1;
    }
}

/**
 * @brief Validate GPIO pin number
 * 
 * @param pin GPIO pin number
 * @return true if valid, false otherwise
 */
static bool is_valid_gpio_pin(int pin) {
    return (pin >= 0 && pin < GPIO_NUM_MAX);
}

esp_err_t gpio_handler_init(gpio_handler_t* handler) {
    if (!handler) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize structure
    memset(handler, 0, sizeof(gpio_handler_t));
    
    // Configure ADC for analog inputs
    esp_err_t ret = adc1_config_width(ADC_WIDTH_BIT_12);
    if (ret != ESP_OK) {
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGE(TAG, "Failed to configure ADC width: %s", esp_err_to_name(ret));
#endif
        return ret;
    }
    
    handler->initialized = true;
    
#ifdef DEBUG_GPIO_HANDLER
    ESP_LOGI(TAG, "GPIO handler initialized successfully");
#endif
    
    return ESP_OK;
}

esp_err_t gpio_handler_configure_input(gpio_handler_t* handler, int pin, bool pullup) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!is_valid_gpio_pin(pin)) {
        handler->error_count++;
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGE(TAG, "Invalid GPIO pin number: %d", pin);
#endif
        return ESP_ERR_INVALID_ARG;
    }
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        handler->configured_pins |= (1ULL << pin);
        handler->input_pins |= (1ULL << pin);
        
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGI(TAG, "Configured pin %d as input (pullup: %s)", pin, pullup ? "enabled" : "disabled");
#endif
    } else {
        handler->error_count++;
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGE(TAG, "Failed to configure pin %d as input: %s", pin, esp_err_to_name(ret));
#endif
    }
    
    return ret;
}

esp_err_t gpio_handler_configure_output(gpio_handler_t* handler, int pin, bool initial_state) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!is_valid_gpio_pin(pin)) {
        handler->error_count++;
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGE(TAG, "Invalid GPIO pin number: %d", pin);
#endif
        return ESP_ERR_INVALID_ARG;
    }
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        // Set initial state
        gpio_set_level(pin, initial_state ? 1 : 0);
        
        handler->configured_pins |= (1ULL << pin);
        handler->output_pins |= (1ULL << pin);
        
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGI(TAG, "Configured pin %d as output (initial: %s)", pin, initial_state ? "HIGH" : "LOW");
#endif
    } else {
        handler->error_count++;
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGE(TAG, "Failed to configure pin %d as output: %s", pin, esp_err_to_name(ret));
#endif
    }
    
    return ret;
}

esp_err_t gpio_handler_configure_analog(gpio_handler_t* handler, int pin) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!is_valid_gpio_pin(pin)) {
        handler->error_count++;
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGE(TAG, "Invalid GPIO pin number: %d", pin);
#endif
        return ESP_ERR_INVALID_ARG;
    }
    
    adc1_channel_t channel = pin_to_adc_channel(pin);
    if (channel < 0) {
        handler->error_count++;
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGE(TAG, "Pin %d is not a valid ADC pin", pin);
#endif
        return ESP_ERR_INVALID_ARG;
    }
    
    // Configure ADC channel with 11dB attenuation (0-3.3V range)
    esp_err_t ret = adc1_config_channel_atten(channel, ADC_ATTEN_DB_11);
    if (ret == ESP_OK) {
        handler->configured_pins |= (1ULL << pin);
        handler->analog_pins |= (1ULL << pin);
        
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGI(TAG, "Configured pin %d as analog input (ADC channel %d)", pin, channel);
#endif
    } else {
        handler->error_count++;
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGE(TAG, "Failed to configure pin %d as analog: %s", pin, esp_err_to_name(ret));
#endif
    }
    
    return ret;
}

esp_err_t gpio_handler_read_digital(gpio_handler_t* handler, int pin, bool* value) {
    if (!handler || !handler->initialized || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!is_valid_gpio_pin(pin)) {
        handler->error_count++;
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!(handler->input_pins & (1ULL << pin))) {
        handler->error_count++;
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGE(TAG, "Pin %d is not configured as input", pin);
#endif
        return ESP_ERR_INVALID_STATE;
    }
    
    *value = gpio_get_level(pin) ? true : false;
    handler->read_count++;
    
    return ESP_OK;
}

esp_err_t gpio_handler_write_digital(gpio_handler_t* handler, int pin, bool value) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!is_valid_gpio_pin(pin)) {
        handler->error_count++;
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!(handler->output_pins & (1ULL << pin))) {
        handler->error_count++;
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGE(TAG, "Pin %d is not configured as output", pin);
#endif
        return ESP_ERR_INVALID_STATE;
    }
    
    gpio_set_level(pin, value ? 1 : 0);
    handler->write_count++;
    
    return ESP_OK;
}

esp_err_t gpio_handler_read_analog(gpio_handler_t* handler, int pin, int* value) {
    if (!handler || !handler->initialized || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!is_valid_gpio_pin(pin)) {
        handler->error_count++;
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!(handler->analog_pins & (1ULL << pin))) {
        handler->error_count++;
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGE(TAG, "Pin %d is not configured as analog input", pin);
#endif
        return ESP_ERR_INVALID_STATE;
    }
    
    adc1_channel_t channel = pin_to_adc_channel(pin);
    if (channel < 0) {
        handler->error_count++;
        return ESP_ERR_INVALID_ARG;
    }
    
    *value = adc1_get_raw(channel);
    handler->read_count++;
    
    return ESP_OK;
}

esp_err_t gpio_handler_get_pin_info(gpio_handler_t* handler, int pin, bool* is_input, bool* is_output, bool* is_analog) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!is_valid_gpio_pin(pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (is_input) *is_input = (handler->input_pins & (1ULL << pin)) != 0;
    if (is_output) *is_output = (handler->output_pins & (1ULL << pin)) != 0;
    if (is_analog) *is_analog = (handler->analog_pins & (1ULL << pin)) != 0;
    
    return ESP_OK;
}

esp_err_t gpio_handler_get_statistics(gpio_handler_t* handler, uint32_t* reads, uint32_t* writes, uint32_t* errors) {
    if (!handler || !handler->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (reads) *reads = handler->read_count;
    if (writes) *writes = handler->write_count;
    if (errors) *errors = handler->error_count;
    
    return ESP_OK;
}

void gpio_handler_destroy(gpio_handler_t* handler) {
    if (handler && handler->initialized) {
        handler->initialized = false;
        handler->configured_pins = 0;
        handler->input_pins = 0;
        handler->output_pins = 0;
        handler->analog_pins = 0;
        
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGI(TAG, "GPIO handler destroyed (reads: %lu, writes: %lu, errors: %lu)", 
                 handler->read_count, handler->write_count, handler->error_count);
#endif
    }
}
