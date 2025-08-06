/**
 * @file gpio_handler.c
 * @brief GPIO Handler implementation for SNRv9 Irrigation Control System
 */

#include "gpio_handler.h"
#include "debug_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include <string.h>

static const char* TAG = DEBUG_GPIO_HANDLER_TAG;

esp_err_t gpio_handler_init(gpio_handler_t* handler) {
    if (!handler) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize structure
    memset(handler, 0, sizeof(gpio_handler_t));
    
    // Initialize ADC1 for analog inputs
    esp_err_t ret = adc1_config_width(ADC_WIDTH_BIT_12);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC width: %s", esp_err_to_name(ret));
        return ret;
    }
    
    handler->initialized = true;
    
#ifdef DEBUG_GPIO_HANDLER
    ESP_LOGI(TAG, "GPIO handler initialized");
#endif
    
    return ESP_OK;
}

esp_err_t gpio_handler_configure_input(gpio_handler_t* handler, int pin, bool pullup) {
    if (!handler || !handler->initialized || pin < 0) {
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
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d as input: %s", pin, esp_err_to_name(ret));
        return ret;
    }
    
#ifdef DEBUG_GPIO_HANDLER
    ESP_LOGI(TAG, "Configured GPIO %d as input (pullup: %s)", pin, pullup ? "enabled" : "disabled");
#endif
    
    return ESP_OK;
}

esp_err_t gpio_handler_configure_output(gpio_handler_t* handler, int pin, bool initial_state) {
    if (!handler || !handler->initialized || pin < 0) {
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
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d as output: %s", pin, esp_err_to_name(ret));
        return ret;
    }
    
    // SAFETY: Always set to safe state (OFF/LOW) first, regardless of initial_state parameter
    // This ensures outputs start in a known safe state for irrigation control
    ret = gpio_set_level(pin, 0);  // Always start LOW (OFF)
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set safe state for GPIO %d: %s", pin, esp_err_to_name(ret));
        return ret;
    }
    
    // Only set to requested initial state if it's not the safe state
    if (initial_state) {
        ret = gpio_set_level(pin, 1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set initial state for GPIO %d: %s", pin, esp_err_to_name(ret));
            return ret;
        }
    }
    
#ifdef DEBUG_GPIO_HANDLER
    ESP_LOGI(TAG, "Configured GPIO %d as output (safe init, final: %s)", pin, initial_state ? "HIGH" : "LOW");
#endif
    
    return ESP_OK;
}

esp_err_t gpio_handler_configure_analog(gpio_handler_t* handler, int pin) {
    if (!handler || !handler->initialized || pin < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Convert GPIO pin to ADC channel
    adc1_channel_t channel;
    switch (pin) {
        case 32: channel = ADC1_CHANNEL_4; break;
        case 33: channel = ADC1_CHANNEL_5; break;
        case 34: channel = ADC1_CHANNEL_6; break;
        case 35: channel = ADC1_CHANNEL_7; break;
        case 36: channel = ADC1_CHANNEL_0; break;
        case 39: channel = ADC1_CHANNEL_3; break;
        default:
            ESP_LOGE(TAG, "GPIO %d is not a valid ADC pin", pin);
            return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = adc1_config_channel_atten(channel, ADC_ATTEN_DB_11);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel for GPIO %d: %s", pin, esp_err_to_name(ret));
        return ret;
    }
    
#ifdef DEBUG_GPIO_HANDLER
    ESP_LOGI(TAG, "Configured GPIO %d as analog input (ADC channel %d)", pin, channel);
#endif
    
    return ESP_OK;
}

esp_err_t gpio_handler_read_digital(gpio_handler_t* handler, int pin, bool* state) {
    if (!handler || !handler->initialized || pin < 0 || !state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int level = gpio_get_level(pin);
    *state = (level != 0);
    
    return ESP_OK;
}

esp_err_t gpio_handler_write_digital(gpio_handler_t* handler, int pin, bool state) {
    if (!handler || !handler->initialized || pin < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = gpio_set_level(pin, state ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO %d to %s: %s", pin, state ? "HIGH" : "LOW", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t gpio_handler_read_analog(gpio_handler_t* handler, int pin, int* raw_value) {
    if (!handler || !handler->initialized || pin < 0 || !raw_value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Convert GPIO pin to ADC channel
    adc1_channel_t channel;
    switch (pin) {
        case 32: channel = ADC1_CHANNEL_4; break;
        case 33: channel = ADC1_CHANNEL_5; break;
        case 34: channel = ADC1_CHANNEL_6; break;
        case 35: channel = ADC1_CHANNEL_7; break;
        case 36: channel = ADC1_CHANNEL_0; break;
        case 39: channel = ADC1_CHANNEL_3; break;
        default:
            ESP_LOGE(TAG, "GPIO %d is not a valid ADC pin", pin);
            return ESP_ERR_INVALID_ARG;
    }
    
    int adc_reading = adc1_get_raw(channel);
    if (adc_reading < 0) {
        ESP_LOGE(TAG, "Failed to read ADC from GPIO %d", pin);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    *raw_value = adc_reading;
    return ESP_OK;
}

void gpio_handler_destroy(gpio_handler_t* handler) {
    if (handler && handler->initialized) {
        handler->initialized = false;
        
#ifdef DEBUG_GPIO_HANDLER
        ESP_LOGI(TAG, "GPIO handler destroyed");
#endif
    }
}
