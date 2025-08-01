/**
 * @file shift_register_handler.h
 * @brief Shift Register Handler for SNRv9 Irrigation Control System
 * 
 * Provides hardware abstraction for 74HC595 (output) and 74HC165 (input)
 * shift register chains for IO expansion.
 */

#ifndef SHIFT_REGISTER_HANDLER_H
#define SHIFT_REGISTER_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of shift register chips supported
 */
#define SHIFT_REGISTER_MAX_CHIPS 8


/**
 * @brief Shift Register Handler Structure
 */
typedef struct {
    bool initialized;                                           ///< Initialization status
    shift_register_config_t config;                           ///< Configuration
    uint8_t output_states[SHIFT_REGISTER_MAX_CHIPS];          ///< Output register states
    uint8_t input_states[SHIFT_REGISTER_MAX_CHIPS];           ///< Input register states
    SemaphoreHandle_t mutex;                                   ///< Thread safety mutex
    uint32_t read_count;                                       ///< Number of read operations
    uint32_t write_count;                                      ///< Number of write operations
    uint32_t error_count;                                      ///< Number of errors
} shift_register_handler_t;

/**
 * @brief Initialize shift register handler
 * 
 * @param handler Pointer to shift register handler structure
 * @param config Pointer to configuration structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t shift_register_handler_init(shift_register_handler_t* handler, const shift_register_config_t* config);

/**
 * @brief Read all input shift registers
 * 
 * Updates internal input state buffer with current values from all input shift registers.
 * This operation is thread-safe and performs parallel load followed by serial read.
 * 
 * @param handler Pointer to shift register handler structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t shift_register_read_inputs(shift_register_handler_t* handler);

/**
 * @brief Write all output shift registers
 * 
 * Writes current output state buffer to all output shift registers.
 * This operation is thread-safe and performs serial write followed by latch.
 * 
 * @param handler Pointer to shift register handler structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t shift_register_write_outputs(shift_register_handler_t* handler);

/**
 * @brief Set output bit state
 * 
 * @param handler Pointer to shift register handler structure
 * @param chip_index Chip index (0-based)
 * @param bit_index Bit index within chip (0-7)
 * @param state Bit state (true = HIGH, false = LOW)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t shift_register_set_output_bit(shift_register_handler_t* handler, int chip_index, int bit_index, bool state);

/**
 * @brief Get output bit state
 * 
 * @param handler Pointer to shift register handler structure
 * @param chip_index Chip index (0-based)
 * @param bit_index Bit index within chip (0-7)
 * @param state Pointer to store bit state
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t shift_register_get_output_bit(shift_register_handler_t* handler, int chip_index, int bit_index, bool* state);

/**
 * @brief Get input bit state
 * 
 * @param handler Pointer to shift register handler structure
 * @param chip_index Chip index (0-based)
 * @param bit_index Bit index within chip (0-7)
 * @param state Pointer to store bit state
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t shift_register_get_input_bit(shift_register_handler_t* handler, int chip_index, int bit_index, bool* state);

/**
 * @brief Set entire output register byte
 * 
 * @param handler Pointer to shift register handler structure
 * @param chip_index Chip index (0-based)
 * @param value 8-bit value to set
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t shift_register_set_output_byte(shift_register_handler_t* handler, int chip_index, uint8_t value);

/**
 * @brief Get entire output register byte
 * 
 * @param handler Pointer to shift register handler structure
 * @param chip_index Chip index (0-based)
 * @param value Pointer to store 8-bit value
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t shift_register_get_output_byte(shift_register_handler_t* handler, int chip_index, uint8_t* value);

/**
 * @brief Get entire input register byte
 * 
 * @param handler Pointer to shift register handler structure
 * @param chip_index Chip index (0-based)
 * @param value Pointer to store 8-bit value
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t shift_register_get_input_byte(shift_register_handler_t* handler, int chip_index, uint8_t* value);

/**
 * @brief Get shift register statistics
 * 
 * @param handler Pointer to shift register handler structure
 * @param reads Pointer to store read count (can be NULL)
 * @param writes Pointer to store write count (can be NULL)
 * @param errors Pointer to store error count (can be NULL)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t shift_register_get_statistics(shift_register_handler_t* handler, uint32_t* reads, uint32_t* writes, uint32_t* errors);

/**
 * @brief Destroy shift register handler and cleanup resources
 * 
 * @param handler Pointer to shift register handler structure
 */
void shift_register_handler_destroy(shift_register_handler_t* handler);

#ifdef __cplusplus
}
#endif

#endif // SHIFT_REGISTER_HANDLER_H
