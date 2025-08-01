/**
 * @file signal_conditioner.c
 * @brief Signal Conditioning Implementation for SNRv9 Irrigation Control System
 */

#include "signal_conditioner.h"
#include "debug_config.h"
#include <math.h>
#include <string.h>

#ifdef DEBUG_SIGNAL_CONDITIONER
static const char* TAG = DEBUG_SIGNAL_CONDITIONER_TAG;
#endif

float signal_conditioner_apply(float raw_value, 
                              const signal_config_t* config,
                              float* sma_buffer,
                              int* sma_index,
                              int* sma_count,
                              float* sma_sum)
{
    if (!config || !config->enabled) {
        return raw_value;
    }

    float conditioned_value = raw_value;

#ifdef DEBUG_SIGNAL_CONDITIONER
    printf("[%s] Starting signal conditioning: raw=%.3f\n", TAG, raw_value);
#endif

    // Step 1: Apply offset
    conditioned_value += config->offset;

    // Step 2: Apply gain
    conditioned_value *= config->gain;

    // Step 3: Apply scaling factor
    conditioned_value *= config->scaling_factor;

#ifdef DEBUG_SIGNAL_CONDITIONER
    printf("[%s] After offset/gain/scaling: %.3f\n", TAG, conditioned_value);
#endif

    // Step 4: Apply lookup table interpolation (if enabled)
    if (config->lookup_table_enabled && config->lookup_table_count > 0) {
        conditioned_value = signal_conditioner_lookup_table(conditioned_value, config);
#ifdef DEBUG_SIGNAL_CONDITIONER
        printf("[%s] After lookup table: %.3f\n", TAG, conditioned_value);
#endif
    }

    // Step 5: Apply precision rounding
    conditioned_value = signal_conditioner_round_precision(conditioned_value, config->precision_digits);

    // Step 6: Apply SMA filtering (if enabled)
    if (config->filter_type == SIGNAL_FILTER_SMA && config->sma_window_size > 1) {
        conditioned_value = signal_conditioner_sma_filter(conditioned_value,
                                                         sma_buffer,
                                                         sma_index,
                                                         sma_count,
                                                         sma_sum,
                                                         config->sma_window_size);
#ifdef DEBUG_SIGNAL_CONDITIONER
        printf("[%s] After SMA filter: %.3f\n", TAG, conditioned_value);
#endif
    }

#ifdef DEBUG_SIGNAL_CONDITIONER
    printf("[%s] Final conditioned value: %.3f\n", TAG, conditioned_value);
#endif

    return conditioned_value;
}

float signal_conditioner_lookup_table(float input, const signal_config_t* config)
{
    if (!config || !config->lookup_table_enabled || config->lookup_table_count < 2) {
        return input;
    }

    // Handle edge cases
    if (input <= config->lookup_table[0].input) {
        return config->lookup_table[0].output;
    }
    
    if (input >= config->lookup_table[config->lookup_table_count - 1].input) {
        return config->lookup_table[config->lookup_table_count - 1].output;
    }

    // Find the interpolation segment
    for (int i = 0; i < config->lookup_table_count - 1; i++) {
        float x1 = config->lookup_table[i].input;
        float y1 = config->lookup_table[i].output;
        float x2 = config->lookup_table[i + 1].input;
        float y2 = config->lookup_table[i + 1].output;

        if (input >= x1 && input <= x2) {
            // Linear interpolation: y = y1 + (y2-y1) * (x-x1) / (x2-x1)
            if (x2 == x1) {
                return y1; // Avoid division by zero
            }
            
            float interpolated = y1 + (y2 - y1) * (input - x1) / (x2 - x1);
            
#ifdef DEBUG_SIGNAL_CONDITIONER
            printf("[%s] Lookup interpolation: input=%.3f, x1=%.3f, y1=%.3f, x2=%.3f, y2=%.3f, result=%.3f\n",
                   TAG, input, x1, y1, x2, y2, interpolated);
#endif
            
            return interpolated;
        }
    }

    // Should not reach here, but return input as fallback
    return input;
}

float signal_conditioner_sma_filter(float new_sample,
                                   float* sma_buffer,
                                   int* sma_index,
                                   int* sma_count,
                                   float* sma_sum,
                                   int window_size)
{
    if (!sma_buffer || !sma_index || !sma_count || !sma_sum || window_size <= 1) {
        return new_sample;
    }

    // Ensure window size doesn't exceed buffer size
    if (window_size > 16) { // Based on our reduced buffer size
        window_size = 16;
    }

    // Remove old sample from sum if buffer is full
    if (*sma_count >= window_size) {
        *sma_sum -= sma_buffer[*sma_index];
    }

    // Add new sample
    sma_buffer[*sma_index] = new_sample;
    *sma_sum += new_sample;

    // Update count (up to window size)
    if (*sma_count < window_size) {
        (*sma_count)++;
    }

    // Calculate average
    float average = *sma_sum / (*sma_count);

    // Update index (circular buffer)
    *sma_index = (*sma_index + 1) % window_size;

#ifdef DEBUG_SIGNAL_CONDITIONER
    printf("[%s] SMA filter: new_sample=%.3f, count=%d, sum=%.3f, average=%.3f\n",
           TAG, new_sample, *sma_count, *sma_sum, average);
#endif

    return average;
}

float signal_conditioner_round_precision(float value, int precision_digits)
{
    if (precision_digits < 0) {
        precision_digits = 0;
    }
    if (precision_digits > 6) {
        precision_digits = 6; // Reasonable limit for float precision
    }

    float multiplier = powf(10.0f, (float)precision_digits);
    return roundf(value * multiplier) / multiplier;
}

void signal_conditioner_init_sma(float* sma_buffer,
                                int* sma_index,
                                int* sma_count,
                                float* sma_sum,
                                int buffer_size)
{
    if (!sma_buffer || !sma_index || !sma_count || !sma_sum) {
        return;
    }

    // Clear buffer
    memset(sma_buffer, 0, buffer_size * sizeof(float));
    
    // Reset state
    *sma_index = 0;
    *sma_count = 0;
    *sma_sum = 0.0f;

#ifdef DEBUG_SIGNAL_CONDITIONER
    printf("[%s] SMA filter initialized with buffer size %d\n", TAG, buffer_size);
#endif
}

bool signal_conditioner_validate_config(const signal_config_t* config)
{
    if (!config) {
        return false;
    }

    // Validate SMA window size
    if (config->filter_type == SIGNAL_FILTER_SMA) {
        if (config->sma_window_size < 1 || config->sma_window_size > 16) {
#ifdef DEBUG_SIGNAL_CONDITIONER
            printf("[%s] Invalid SMA window size: %d (must be 1-16)\n", TAG, config->sma_window_size);
#endif
            return false;
        }
    }

    // Validate precision digits
    if (config->precision_digits < 0 || config->precision_digits > 6) {
#ifdef DEBUG_SIGNAL_CONDITIONER
        printf("[%s] Invalid precision digits: %d (must be 0-6)\n", TAG, config->precision_digits);
#endif
        return false;
    }

    // Validate lookup table
    if (config->lookup_table_enabled) {
        if (config->lookup_table_count < 2 || config->lookup_table_count > CONFIG_MAX_LOOKUP_ENTRIES) {
#ifdef DEBUG_SIGNAL_CONDITIONER
            printf("[%s] Invalid lookup table count: %d (must be 2-%d)\n", 
                   TAG, config->lookup_table_count, CONFIG_MAX_LOOKUP_ENTRIES);
#endif
            return false;
        }

        // Check that lookup table is sorted by input values
        for (int i = 1; i < config->lookup_table_count; i++) {
            if (config->lookup_table[i].input <= config->lookup_table[i-1].input) {
#ifdef DEBUG_SIGNAL_CONDITIONER
                printf("[%s] Lookup table not sorted at index %d\n", TAG, i);
#endif
                return false;
            }
        }
    }

    // Validate history buffer size
    if (config->history_buffer_size < 1 || config->history_buffer_size > 1000) {
#ifdef DEBUG_SIGNAL_CONDITIONER
        printf("[%s] Invalid history buffer size: %d (must be 1-1000)\n", TAG, config->history_buffer_size);
#endif
        return false;
    }

#ifdef DEBUG_SIGNAL_CONDITIONER
    printf("[%s] Signal configuration validation passed\n", TAG);
#endif

    return true;
}
