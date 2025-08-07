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

/**
 * @brief Debug output tag for web server
 */
#define DEBUG_WEB_SERVER_TAG "WEB_SERVER"

/**
 * @brief Debug output tag for static file controller
 */
#define DEBUG_STATIC_FILE_TAG "STATIC_FILE"

/**
 * @brief Debug output tag for system controller
 */
#define DEBUG_SYSTEM_CONTROLLER_TAG "SYSTEM_CTRL"

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
 * @brief Enable/disable WiFi monitoring debug output
 * Set to 1 to enable WiFi connection monitoring, 0 to disable
 */
#define DEBUG_WIFI_MONITORING 1

/**
 * @brief WiFi status report interval in milliseconds
 * How often to output WiFi status to serial
 */
#define DEBUG_WIFI_REPORT_INTERVAL_MS 30000

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

/* =============================================================================
 * IO SYSTEM DEBUG CONFIGURATION
 * =============================================================================
 */

/**
 * @brief Enable/disable IO Manager debug output
 * Set to 1 to enable IO manager debugging, 0 to disable
 */
#define DEBUG_IO_MANAGER 1

/**
 * @brief Enable/disable Configuration Manager debug output
 * Set to 1 to enable config manager debugging, 0 to disable
 */
#define DEBUG_CONFIG_MANAGER 1

/**
 * @brief Enable/disable GPIO Handler debug output
 * Set to 1 to enable GPIO handler debugging, 0 to disable
 */
#define DEBUG_GPIO_HANDLER 1

/**
 * @brief Enable/disable Shift Register Handler debug output
 * Set to 1 to enable shift register debugging, 0 to disable
 */
#define DEBUG_SHIFT_REGISTER 1

/**
 * @brief Enable/disable verbose shift register operations
 * Set to 1 for detailed bit-level operations, 0 for summary only
 */
#define DEBUG_SHIFT_REGISTER_VERBOSE 0

/**
 * @brief Enable/disable Signal Conditioner debug output
 * Set to 1 to enable signal processing debugging, 0 to disable
 */
#define DEBUG_SIGNAL_CONDITIONER 1

/**
 * @brief Enable/disable verbose signal processing
 * Set to 1 for detailed processing steps, 0 for summary only
 */
#define DEBUG_SIGNAL_CONDITIONER_VERBOSE 0

/**
 * @brief Enable/disable IO State Manager debug output
 * Set to 1 to enable state manager debugging, 0 to disable
 */
#define DEBUG_IO_STATE_MANAGER 1

/**
 * @brief Enable/disable Alarm System debug output
 * Set to 1 to enable alarm system debugging, 0 to disable
 */
#define DEBUG_ALARM_SYSTEM 1

/**
 * @brief Enable/disable IO Web Controllers debug output
 * Set to 1 to enable IO controller debugging, 0 to disable
 */
#define DEBUG_IO_CONTROLLERS 1

/**
 * @brief Enable/disable IO Test Manager debug output
 * Set to 1 to enable IO test manager debugging, 0 to disable
 */
#define DEBUG_IO_TEST_MANAGER 1

/* =============================================================================
 * IO SYSTEM TIMING CONFIGURATION
 * =============================================================================
 */

/**
 * @brief IO polling interval in milliseconds
 * How often to read all inputs and update states
 */
#define DEBUG_IO_POLLING_INTERVAL_MS 1000

/**
 * @brief IO status report interval in milliseconds
 * How often to output IO system status to serial
 */
#define DEBUG_IO_REPORT_INTERVAL_MS 10000

/**
 * @brief Alarm check interval in milliseconds
 * How often to check for alarm conditions
 */
#define DEBUG_ALARM_CHECK_INTERVAL_MS 5000

/* =============================================================================
 * IO SYSTEM OUTPUT TAGS
 * =============================================================================
 */

/**
 * @brief Debug output tag for IO Manager
 */
#define DEBUG_IO_MANAGER_TAG "IO_MANAGER"

/**
 * @brief Debug output tag for Configuration Manager
 */
#define DEBUG_CONFIG_MANAGER_TAG "CONFIG_MGR"

/**
 * @brief Debug output tag for GPIO Handler
 */
#define DEBUG_GPIO_HANDLER_TAG "GPIO_HANDLER"

/**
 * @brief Debug output tag for Shift Register Handler
 */
#define DEBUG_SHIFT_REGISTER_TAG "SHIFT_REG"

/**
 * @brief Debug output tag for Signal Conditioner
 */
#define DEBUG_SIGNAL_CONDITIONER_TAG "SIGNAL_COND"

/**
 * @brief Debug output tag for IO State Manager
 */
#define DEBUG_IO_STATE_MANAGER_TAG "IO_STATE"

/**
 * @brief Debug output tag for Alarm System
 */
#define DEBUG_ALARM_SYSTEM_TAG "ALARM_SYS"

/**
 * @brief Debug output tag for IO Controllers
 */
#define DEBUG_IO_CONTROLLERS_TAG "IO_CTRL"

/**
 * @brief Debug output tag for IO Test Manager
 */
#define DEBUG_IO_TEST_MANAGER_TAG "IO_TEST"

/* =============================================================================
 * PSRAM TESTING DEBUG CONFIGURATION
 * =============================================================================
 */

/**
 * @brief Enable/disable PSRAM comprehensive testing
 * Set to 1 to enable full test suite, 0 to disable completely
 */
#define DEBUG_PSRAM_COMPREHENSIVE_TESTING 0

/**
 * @brief Enable/disable PSRAM quick testing
 * Set to 1 to enable quick test, 0 to disable
 */
#define DEBUG_PSRAM_QUICK_TESTING 1

/**
 * @brief PSRAM test execution delay in milliseconds
 * How long to wait after startup before running comprehensive tests
 */
#define DEBUG_PSRAM_TEST_DELAY_MS 15000

/**
 * @brief Enable/disable verbose PSRAM test output
 * Set to 1 for detailed test phase output, 0 for summary only
 */
#define DEBUG_PSRAM_TEST_VERBOSE 1

/**
 * @brief Enable/disable PSRAM allocation strategy debugging
 * Set to 1 to debug allocation strategy tests, 0 to disable
 */
#define DEBUG_PSRAM_ALLOCATION_STRATEGY 1

/**
 * @brief Enable/disable PSRAM task creation debugging
 * Set to 1 to debug task creation tests, 0 to disable
 */
#define DEBUG_PSRAM_TASK_CREATION 1

/**
 * @brief Enable/disable PSRAM memory access debugging
 * Set to 1 to debug memory access patterns, 0 to disable
 */
#define DEBUG_PSRAM_MEMORY_ACCESS 1

/**
 * @brief Enable/disable PSRAM health check debugging
 * Set to 1 to debug health check operations, 0 to disable
 */
#define DEBUG_PSRAM_HEALTH_CHECK 1

/**
 * @brief Enable/disable PSRAM safety checks
 * Set to 1 to enable runtime safety validation, 0 to disable
 */
#define DEBUG_PSRAM_SAFETY_CHECKS 1

/* =============================================================================
 * PSRAM TESTING OUTPUT TAGS
 * =============================================================================
 */

/**
 * @brief Debug output tag for PSRAM testing
 */
#define DEBUG_PSRAM_TEST_TAG "PSRAM_TEST"

/**
 * @brief Debug output tag for PSRAM manager
 */
#define DEBUG_PSRAM_MGR_TAG "PSRAM_MGR"

/**
 * @brief Debug output tag for PSRAM safety
 */
#define DEBUG_PSRAM_SAFETY_TAG "PSRAM_SAFETY"

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_CONFIG_H */
