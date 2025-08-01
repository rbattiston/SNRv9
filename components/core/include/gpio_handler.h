/**
 * @file gpio_handler.h
 * @brief GPIO Handler for SNRv9 Irrigation Control System
 * 
 * Provides hardware abstraction for ESP32 GPIO operations including
 * digital input/output and analog input (ADC) functionality.
 */

#ifndef GPIO_HANDLER_H
#define GPIO_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of GPIO pins that can be tracked
 */
#define GPIO_HANDLER_MAX_PINS 40

/**
 * @brief GPIO Handler Structure
 * 
 * Maintains state and configuration for all GPIO operations
 */
typedef struct {
    bool initialized;                    ///< Initialization status
    uint64_t configured_pins;           ///< Bitmask of configured pins
    uint64_t input_pins;                ///< Bitmask of input pins
    uint64_t output_pins;               ///< Bitmask of output pins
    uint64_t analog_pins;               ///< Bitmask of analog pins
    uint32_t read_count;                ///< Number of read operations
    uint32_t write_count;               ///< Number of write operations
    uint32_t error_count;               ///< Number of errors encountered
} gpio_handler_t;

/**
 * @brief Initialize GPIO handler
 * 
 * @param handler Pointer to GPIO handler structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t gpio_handler_init(gpio_handler_t* handler);

/**
 * @brief Configure a pin as digital input
 * 
 * @param handler Pointer to GPIO handler structure
 * @param pin GPIO pin number
 * @param pullup Enable internal pullup resistor
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t gpio_handler_configure_input(gpio_handler_t* handler, int pin, bool pullup);

/**
 * @brief Configure a pin as digital output
 * 
 * @param handler Pointer to GPIO handler structure
 * @param pin GPIO pin number
 * @param initial_state Initial output state (true = HIGH, false = LOW)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t gpio_handler_configure_output(gpio_handler_t* handler, int pin, bool initial_state);

/**
 * @brief Configure a pin as analog input
 * 
 * @param handler Pointer to GPIO handler structure
 * @param pin GPIO pin number (must be valid ADC pin)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t gpio_handler_configure_analog(gpio_handler_t* handler, int pin);

/**
 * @brief Read digital pin value
 * 
 * @param handler Pointer to GPIO handler structure
 * @param pin GPIO pin number
 * @param value Pointer to store read value
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t gpio_handler_read_digital(gpio_handler_t* handler, int pin, bool* value);

/**
 * @brief Write digital pin value
 * 
 * @param handler Pointer to GPIO handler structure
 * @param pin GPIO pin number
 * @param value Value to write (true = HIGH, false = LOW)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t gpio_handler_write_digital(gpio_handler_t* handler, int pin, bool value);

/**
 * @brief Read analog pin value
 * 
 * @param handler Pointer to GPIO handler structure
 * @param pin GPIO pin number
 * @param value Pointer to store raw ADC value (0-4095)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t gpio_handler_read_analog(gpio_handler_t* handler, int pin, int* value);

/**
 * @brief Get pin configuration information
 * 
 * @param handler Pointer to GPIO handler structure
 * @param pin GPIO pin number
 * @param is_input Pointer to store input status (can be NULL)
 * @param is_output Pointer to store output status (can be NULL)
 * @param is_analog Pointer to store analog status (can be NULL)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t gpio_handler_get_pin_info(gpio_handler_t* handler, int pin, bool* is_input, bool* is_output, bool* is_analog);

/**
 * @brief Get GPIO handler statistics
 * 
 * @param handler Pointer to GPIO handler structure
 * @param reads Pointer to store read count (can be NULL)
 * @param writes Pointer to store write count (can be NULL)
 * @param errors Pointer to store error count (can be NULL)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t gpio_handler_get_statistics(gpio_handler_t* handler, uint32_t* reads, uint32_t* writes, uint32_t* errors);

/**
 * @brief Destroy GPIO handler and cleanup resources
 * 
 * @param handler Pointer to GPIO handler structure
 */
void gpio_handler_destroy(gpio_handler_t* handler);

#ifdef __cplusplus
}
#endif

#endif // GPIO_HANDLER_H
