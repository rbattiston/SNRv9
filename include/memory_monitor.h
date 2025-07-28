/**
 * @file memory_monitor.h
 * @brief Memory monitoring system interface for SNRv9 Irrigation Control System
 * 
 * This module provides comprehensive memory monitoring capabilities including
 * heap usage tracking, memory trending, and diagnostic reporting.
 */

#ifndef MEMORY_MONITOR_H
#define MEMORY_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "debug_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief Memory statistics structure
 */
typedef struct {
    uint32_t free_heap;          /**< Current free heap memory in bytes */
    uint32_t minimum_free_heap;  /**< Minimum free heap ever recorded in bytes */
    uint32_t total_heap;         /**< Total heap size in bytes */
    uint32_t largest_free_block; /**< Largest contiguous free block in bytes */
    uint32_t timestamp_ms;       /**< Timestamp when sample was taken */
} memory_stats_t;

/**
 * @brief Memory trend data structure
 */
typedef struct {
    memory_stats_t samples[DEBUG_MEMORY_HISTORY_SIZE]; /**< Historical samples */
    uint16_t write_index;                              /**< Current write position */
    uint16_t sample_count;                             /**< Number of valid samples */
    bool buffer_full;                                  /**< True when buffer has wrapped */
} memory_trend_t;

/**
 * @brief Memory monitor status
 */
typedef enum {
    MEMORY_MONITOR_STOPPED = 0,
    MEMORY_MONITOR_RUNNING,
    MEMORY_MONITOR_ERROR
} memory_monitor_status_t;

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the memory monitoring system
 * 
 * This function sets up the memory monitoring task and initializes
 * all data structures. Must be called before any other memory monitor functions.
 * 
 * @return true if initialization successful, false otherwise
 */
bool memory_monitor_init(void);

/**
 * @brief Start the memory monitoring system
 * 
 * Begins periodic memory sampling and reporting based on configuration.
 * 
 * @return true if started successfully, false otherwise
 */
bool memory_monitor_start(void);

/**
 * @brief Stop the memory monitoring system
 * 
 * Stops all monitoring activities but preserves collected data.
 * 
 * @return true if stopped successfully, false otherwise
 */
bool memory_monitor_stop(void);

/**
 * @brief Get current memory monitoring status
 * 
 * @return Current status of the memory monitor
 */
memory_monitor_status_t memory_monitor_get_status(void);

/**
 * @brief Get current memory statistics
 * 
 * Retrieves the most recent memory statistics without waiting for
 * the next scheduled sample.
 * 
 * @param stats Pointer to structure to receive current stats
 * @return true if stats retrieved successfully, false otherwise
 */
bool memory_monitor_get_current_stats(memory_stats_t *stats);

/**
 * @brief Get memory trend data
 * 
 * Retrieves the complete historical memory trend data.
 * 
 * @param trend Pointer to structure to receive trend data
 * @return true if trend data retrieved successfully, false otherwise
 */
bool memory_monitor_get_trend_data(memory_trend_t *trend);

/**
 * @brief Force immediate memory report to serial output
 * 
 * Triggers an immediate memory report regardless of the configured
 * reporting interval.
 */
void memory_monitor_force_report(void);

/**
 * @brief Calculate memory usage percentage
 * 
 * @param stats Pointer to memory statistics
 * @return Memory usage as percentage (0-100)
 */
uint8_t memory_monitor_calc_usage_percent(const memory_stats_t *stats);

/**
 * @brief Calculate memory fragmentation percentage
 * 
 * @param stats Pointer to memory statistics
 * @return Fragmentation as percentage (0-100)
 */
uint8_t memory_monitor_calc_fragmentation_percent(const memory_stats_t *stats);

/**
 * @brief Get memory trend summary
 * 
 * Calculates summary statistics from the trend data including
 * average, minimum, and maximum values.
 * 
 * @param avg_free Average free memory over trend period
 * @param min_free Minimum free memory in trend period
 * @param max_free Maximum free memory in trend period
 * @return true if summary calculated successfully, false otherwise
 */
bool memory_monitor_get_trend_summary(uint32_t *avg_free, uint32_t *min_free, uint32_t *max_free);

/**
 * @brief Reset memory trend data
 * 
 * Clears all historical trend data and resets counters.
 */
void memory_monitor_reset_trend_data(void);

/**
 * @brief Enable/disable memory monitoring at runtime
 * 
 * Allows dynamic control of memory monitoring without recompilation.
 * 
 * @param enable true to enable monitoring, false to disable
 */
void memory_monitor_set_enabled(bool enable);

/**
 * @brief Check if memory monitoring is enabled
 * 
 * @return true if monitoring is enabled, false otherwise
 */
bool memory_monitor_is_enabled(void);

/* =============================================================================
 * DIAGNOSTIC FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Print detailed memory report to console
 * 
 * Outputs comprehensive memory information including current stats,
 * trend summary, and fragmentation analysis.
 */
void memory_monitor_print_detailed_report(void);

/**
 * @brief Print memory trend graph to console
 * 
 * Outputs a simple ASCII graph showing memory usage over time.
 */
void memory_monitor_print_trend_graph(void);

/**
 * @brief Check for potential memory leaks
 * 
 * Analyzes trend data to detect patterns that might indicate memory leaks.
 * 
 * @return true if potential leak detected, false otherwise
 */
bool memory_monitor_check_for_leaks(void);

#ifdef __cplusplus
}
#endif

#endif /* MEMORY_MONITOR_H */
