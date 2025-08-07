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
 * REQUEST PRIORITY MANAGEMENT DEBUG CONFIGURATION
 * =============================================================================
 */

/**
 * @brief Enable/disable request priority management debug output
 * Set to 1 to enable priority system debugging, 0 to disable
 */
#define DEBUG_REQUEST_PRIORITY 1

/**
 * @brief Enable/disable detailed request classification logging
 * Set to 1 to log every request classification decision, 0 to disable
 */
#define DEBUG_REQUEST_CLASSIFICATION 1

/**
 * @brief Enable/disable queue management debug output
 * Set to 1 to enable queue depth and operation logging, 0 to disable
 */
#define DEBUG_QUEUE_MANAGEMENT 1

/**
 * @brief Enable/disable request processing timing
 * Set to 1 to log processing times for each request, 0 to disable
 */
#define DEBUG_REQUEST_TIMING 1

/**
 * @brief Enable/disable load balancing debug output
 * Set to 1 to enable load balancing decision logging, 0 to disable
 */
#define DEBUG_LOAD_BALANCING 1

/**
 * @brief Enable/disable PSRAM allocation tracking for priority system
 * Set to 1 to track PSRAM usage by priority components, 0 to disable
 */
#define DEBUG_PRIORITY_PSRAM 1

/**
 * @brief Enable/disable emergency mode debug output
 * Set to 1 to log emergency mode transitions, 0 to disable
 */
#define DEBUG_EMERGENCY_MODE 1

/**
 * @brief Priority system statistics report interval in milliseconds
 * How often to output priority system statistics to serial
 */
#define DEBUG_PRIORITY_REPORT_INTERVAL_MS 15000

/**
 * @brief Queue depth monitoring interval in milliseconds
 * How often to check and report queue depths
 */
#define DEBUG_QUEUE_MONITOR_INTERVAL_MS 5000

/**
 * @brief Request timing threshold in milliseconds
 * Log requests that take longer than this threshold
 */
#define DEBUG_SLOW_REQUEST_THRESHOLD_MS 1000

/**
 * @brief Maximum number of timing samples to store
 * For performance analysis and trending
 */
#define DEBUG_TIMING_HISTORY_SIZE 50

/**
 * @brief Debug output tag for request priority manager
 */
#define DEBUG_PRIORITY_MANAGER_TAG "REQ_PRIORITY"

/**
 * @brief Debug output tag for request queues
 */
#define DEBUG_QUEUE_TAG "REQ_QUEUE"

/**
 * @brief Debug output tag for request classification
 */
#define DEBUG_CLASSIFICATION_TAG "REQ_CLASS"

/**
 * @brief Debug output tag for load balancing
 */
#define DEBUG_LOAD_BALANCE_TAG "LOAD_BAL"

/**
 * @brief Debug output tag for emergency operations
 */
#define DEBUG_EMERGENCY_TAG "EMERGENCY"

/* =============================================================================
 * REQUEST PRIORITY TEST SUITE DEBUG CONFIGURATION
 * =============================================================================
 */

/**
 * @brief Enable/disable request priority test suite
 * Set to 1 to enable test suite compilation and execution, 0 to disable completely
 * When disabled, test suite code is not compiled (zero memory/flash impact)
 */
#define DEBUG_PRIORITY_TEST_SUITE 1

/**
 * @brief Default test duration in milliseconds
 * How long each test scenario runs by default
 */
#define DEBUG_PRIORITY_TEST_DURATION_MS 60000

/**
 * @brief Test status report interval in milliseconds
 * How often to output test progress and statistics
 */
#define DEBUG_PRIORITY_TEST_REPORT_INTERVAL_MS 5000

/**
 * @brief Enable/disable detailed test task logging
 * Set to 1 to log individual test task operations, 0 for summary only
 */
#define DEBUG_PRIORITY_TEST_DETAILED 1

/**
 * @brief Enable/disable test scenario transition logging
 * Set to 1 to log when test scenarios start/stop, 0 to disable
 */
#define DEBUG_PRIORITY_TEST_SCENARIOS 1

/**
 * @brief Enable/disable test statistics collection
 * Set to 1 to collect detailed timing and performance statistics, 0 to disable
 */
#define DEBUG_PRIORITY_TEST_STATISTICS 1

/**
 * @brief Enable/disable test memory tracking
 * Set to 1 to track memory usage during tests, 0 to disable
 */
#define DEBUG_PRIORITY_TEST_MEMORY 1

/**
 * @brief Test load generator default rate (requests per second)
 * Default rate for load generation testing
 */
#define DEBUG_PRIORITY_TEST_LOAD_RATE_RPS 10

/**
 * @brief Test load generator default payload size in bytes
 * Default size for generated test requests
 */
#define DEBUG_PRIORITY_TEST_PAYLOAD_SIZE 2048

/**
 * @brief Emergency mode test timeout in milliseconds
 * How long to stay in emergency mode during testing
 */
#define DEBUG_PRIORITY_TEST_EMERGENCY_TIMEOUT_MS 30000

/**
 * @brief Enable/disable automatic test cleanup
 * Set to 1 to automatically cleanup test resources on completion, 0 for manual cleanup
 */
#define DEBUG_PRIORITY_TEST_AUTO_CLEANUP 1

/**
 * @brief Maximum test duration in milliseconds (safety limit)
 * Tests will be automatically stopped after this duration
 */
#define DEBUG_PRIORITY_TEST_MAX_DURATION_MS 300000

/**
 * @brief Debug output tag for priority test suite
 */
#define DEBUG_PRIORITY_TEST_TAG "PRIORITY_TEST"


#ifdef __cplusplus
}
#endif

#endif /* DEBUG_CONFIG_H */
