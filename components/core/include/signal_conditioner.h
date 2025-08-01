/**
 * @file signal_conditioner.h
 * @brief Signal Conditioning for SNRv9 Irrigation Control System
 * 
 * Provides signal conditioning algorithms including filtering, scaling,
 * lookup table interpolation, and precision control for analog inputs.
 */

#ifndef SIGNAL_CONDITIONER_H
#define SIGNAL_CONDITIONER_H

#include <stdint.h>
#include <stdbool.h>
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Apply signal conditioning to raw analog value
 * 
 * Applies the complete signal conditioning pipeline:
 * 1. Offset application
 * 2. Gain application  
 * 3. Scaling factor
 * 4. Lookup table interpolation (if enabled)
 * 5. Precision rounding
 * 6. SMA filtering (if enabled)
 * 
 * @param raw_value Raw analog input value (0-4095 for 12-bit ADC)
 * @param config Signal configuration
 * @param sma_buffer SMA filter buffer
 * @param sma_index Current SMA buffer index (modified)
 * @param sma_count Number of samples in SMA buffer (modified)
 * @param sma_sum Running sum for SMA calculation (modified)
 * @return float Conditioned signal value
 */
float signal_conditioner_apply(float raw_value, 
                              const signal_config_t* config,
                              float* sma_buffer,
                              int* sma_index,
                              int* sma_count,
                              float* sma_sum);

/**
 * @brief Apply lookup table interpolation
 * 
 * Performs linear interpolation using the configured lookup table.
 * If input is outside table range, returns nearest boundary value.
 * 
 * @param input Input value
 * @param config Signal configuration with lookup table
 * @return float Interpolated output value
 */
float signal_conditioner_lookup_table(float input, const signal_config_t* config);

/**
 * @brief Apply Simple Moving Average filter
 * 
 * Updates the SMA filter with a new sample and returns the filtered value.
 * 
 * @param new_sample New sample to add to filter
 * @param sma_buffer SMA filter buffer
 * @param sma_index Current SMA buffer index (modified)
 * @param sma_count Number of samples in SMA buffer (modified)
 * @param sma_sum Running sum for SMA calculation (modified)
 * @param window_size SMA window size
 * @return float Filtered value
 */
float signal_conditioner_sma_filter(float new_sample,
                                   float* sma_buffer,
                                   int* sma_index,
                                   int* sma_count,
                                   float* sma_sum,
                                   int window_size);

/**
 * @brief Round value to specified precision
 * 
 * @param value Value to round
 * @param precision_digits Number of decimal places
 * @return float Rounded value
 */
float signal_conditioner_round_precision(float value, int precision_digits);

/**
 * @brief Initialize SMA filter state
 * 
 * @param sma_buffer SMA filter buffer to initialize
 * @param sma_index SMA buffer index to initialize
 * @param sma_count SMA sample count to initialize
 * @param sma_sum SMA running sum to initialize
 * @param buffer_size Size of SMA buffer
 */
void signal_conditioner_init_sma(float* sma_buffer,
                                int* sma_index,
                                int* sma_count,
                                float* sma_sum,
                                int buffer_size);

/**
 * @brief Validate signal configuration
 * 
 * @param config Signal configuration to validate
 * @return bool True if configuration is valid, false otherwise
 */
bool signal_conditioner_validate_config(const signal_config_t* config);

#ifdef __cplusplus
}
#endif

#endif // SIGNAL_CONDITIONER_H
