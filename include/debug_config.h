/**
 * @file debug_config.h
 * @brief Central debug configuration for SNRv9 Irrigation Control System
 * 
 * This file contains all debug-related configuration flags and settings.
 * Use these defines to control debug output throughout the system.
 */

#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * MEMORY MONITORING DEBUG CONFIGURATION
 * =============================================================================
 */

/**
 * @brief Enable/disable memory monitoring system
 * Set to 1 to enable memory monitoring, 0 to disable completely
 */
#define DEBUG_MEMORY_MONITOR 1

/**
 * @brief Enable/disable detailed memory output
 * Set to 1 for detailed output, 0 for summary only
 */
#define DEBUG_MEMORY_DETAILED 1

/**
 * @brief Enable/disable task tracking
 * Set to 1 to enable task monitoring, 0 to disable
 */
#define DEBUG_TASK_TRACKING 1

/**
 * @brief Enable/disable memory trending
 * Set to 1 to enable historical data collection, 0 to disable
 */
#define DEBUG_MEMORY_TRENDING 1

/* =============================================================================
 * TIMING CONFIGURATION
 * =============================================================================
 */

/**
 * @brief Memory monitoring report interval in milliseconds
 * How often to output memory statistics to serial
 */
#define DEBUG_MEMORY_REPORT_INTERVAL_MS 5000

/**
 * @brief Task monitoring report interval in milliseconds
 * How often to output task statistics to serial
 */
#define DEBUG_TASK_REPORT_INTERVAL_MS 10000

/**
 * @brief Memory sampling interval in milliseconds
 * How often to collect memory data for trending
 */
#define DEBUG_MEMORY_SAMPLE_INTERVAL_MS 1000

/* =============================================================================
 * DATA STORAGE CONFIGURATION
 * =============================================================================
 */

/**
 * @brief Number of memory samples to store for trending
 * Larger values use more RAM but provide longer history
 */
#define DEBUG_MEMORY_HISTORY_SIZE 100

/**
 * @brief Maximum number of tasks to track
 * Should be set higher than expected maximum task count
 */
#define DEBUG_MAX_TASKS_TRACKED 20

/* =============================================================================
 * OUTPUT FORMATTING
 * =============================================================================
 */

/**
 * @brief Enable timestamps in debug output
 * Set to 1 to include timestamps, 0 to disable
 */
#define DEBUG_INCLUDE_TIMESTAMPS 1

/**
 * @brief Debug output tag for memory monitoring
 */
#define DEBUG_MEMORY_TAG "MEMORY"

/**
 * @brief Debug output tag for task monitoring
 */
#define DEBUG_TASK_TAG "TASK"

/* =============================================================================
 * FUTURE EXPANSION DEBUG FLAGS
 * =============================================================================
 */

/**
 * @brief Enable/disable irrigation system debug output
 * Reserved for future irrigation-specific debugging
 */
#define DEBUG_IRRIGATION_SYSTEM 0

/**
 * @brief Enable/disable sensor debug output
 * Reserved for future sensor debugging
 */
#define DEBUG_SENSORS 0

/**
 * @brief Enable/disable web server debug output
 * Reserved for future web server debugging
 */
#define DEBUG_WEB_SERVER 0

/**
 * @brief Enable/disable data logging debug output
 * Reserved for future data logging debugging
 */
#define DEBUG_DATA_LOGGING 0

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_CONFIG_H */
