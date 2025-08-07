/**
 * @file request_priority_test_suite.h
 * @brief Request Priority Management Test Suite for SNRv9
 * 
 * This module provides comprehensive testing capabilities for the request priority
 * management system, including load testing, priority validation, and performance
 * monitoring with conditionally compiled test scenarios.
 */

#ifndef REQUEST_PRIORITY_TEST_SUITE_H
#define REQUEST_PRIORITY_TEST_SUITE_H

#include "../../../include/debug_config.h"

#if DEBUG_PRIORITY_TEST_SUITE

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "request_priority_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * CONSTANTS AND CONFIGURATION
 * =============================================================================
 */

#define PRIORITY_TEST_TAG "PRIORITY_TEST"

// Test suite configuration defaults
#define DEFAULT_TEST_DURATION_MS 60000
#define DEFAULT_REPORT_INTERVAL_MS 5000
#define DEFAULT_LOAD_GEN_RATE_RPS 10
#define DEFAULT_LOAD_GEN_PAYLOAD_SIZE 2048

// Task configuration
#define TEST_TASK_STACK_SIZE 3072
#define TEST_TASK_PRIORITY 2
#define TEST_TASK_CORE_AFFINITY 0

// Test limits and safety
#define MAX_TEST_DURATION_MS 300000  // 5 minutes maximum
#define MAX_LOAD_GEN_RATE_RPS 100
#define MAX_PAYLOAD_SIZE 32768       // 32KB maximum
#define MIN_REPORT_INTERVAL_MS 1000

// Request simulation timing (in milliseconds)
#define EMERGENCY_SIM_INTERVAL_MIN 30000
#define EMERGENCY_SIM_INTERVAL_MAX 60000
#define IO_CONTROL_SIM_INTERVAL_MIN 5000
#define IO_CONTROL_SIM_INTERVAL_MAX 10000
#define AUTH_SIM_INTERVAL_MIN 15000
#define AUTH_SIM_INTERVAL_MAX 30000
#define DASHBOARD_SIM_INTERVAL_MIN 2000
#define DASHBOARD_SIM_INTERVAL_MAX 5000
#define BACKGROUND_SIM_INTERVAL_MIN 1000
#define BACKGROUND_SIM_INTERVAL_MAX 2000

/* =============================================================================
 * TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief Test scenario types
 */
typedef enum {
    TEST_SCENARIO_NORMAL_OPERATION = 0,  ///< Balanced mix of all request types
    TEST_SCENARIO_HIGH_LOAD,             ///< High request rate to trigger load shedding
    TEST_SCENARIO_EMERGENCY_MODE,        ///< Emergency mode activation testing
    TEST_SCENARIO_MEMORY_STRESS,         ///< Large payload memory testing
    TEST_SCENARIO_QUEUE_SATURATION,      ///< Queue overflow testing
    TEST_SCENARIO_CUSTOM,                ///< User-defined scenario
    TEST_SCENARIO_MAX
} test_scenario_t;

/**
 * @brief Test task types
 */
typedef enum {
    TEST_TASK_EMERGENCY_SIM = 0,    ///< Emergency request simulator
    TEST_TASK_IO_CONTROL_SIM,       ///< IO control request simulator
    TEST_TASK_AUTH_SIM,             ///< Authentication request simulator
    TEST_TASK_DASHBOARD_SIM,        ///< Dashboard update simulator
    TEST_TASK_BACKGROUND_SIM,       ///< Background operation simulator
    TEST_TASK_LOAD_GENERATOR,       ///< Configurable load generator
    TEST_TASK_MAX
} test_task_type_t;

/**
 * @brief Test task configuration
 */
typedef struct {
    test_task_type_t task_type;
    const char *task_name;
    bool enabled;
    uint32_t interval_min_ms;
    uint32_t interval_max_ms;
    size_t payload_size;
    request_priority_t priority;
    TaskHandle_t task_handle;
} test_task_config_t;

/**
 * @brief Load generator configuration
 */
typedef struct {
    uint32_t requests_per_second;
    size_t payload_size;
    request_priority_t priority;
    bool variable_priority;
    bool variable_payload_size;
} load_generator_config_t;

/**
 * @brief Emergency mode test configuration
 */
typedef struct {
    bool auto_trigger;
    uint32_t trigger_delay_ms;
    uint32_t emergency_timeout_ms;
    bool test_timeout_recovery;
} emergency_test_config_t;

/**
 * @brief Complete test suite configuration
 */
typedef struct {
    // Test control
    uint32_t test_duration_ms;
    uint32_t report_interval_ms;
    test_scenario_t active_scenario;
    
    // Task enables
    bool enable_emergency_sim;
    bool enable_io_control_sim;
    bool enable_auth_sim;
    bool enable_dashboard_sim;
    bool enable_background_sim;
    bool enable_load_generator;
    
    // Load generator settings
    load_generator_config_t load_gen_config;
    
    // Emergency mode testing
    emergency_test_config_t emergency_config;
    
    // Advanced options
    bool enable_statistics_logging;
    bool enable_detailed_timing;
    bool enable_memory_tracking;
    bool auto_cleanup_on_completion;
} priority_test_config_t;

/**
 * @brief Test execution statistics
 */
typedef struct {
    // Test timing
    uint32_t test_start_time;
    uint32_t test_duration_ms;
    uint32_t elapsed_time_ms;
    
    // Request statistics by priority
    uint32_t requests_generated[REQUEST_PRIORITY_MAX];
    uint32_t requests_processed[REQUEST_PRIORITY_MAX];
    uint32_t requests_dropped[REQUEST_PRIORITY_MAX];
    uint32_t requests_timeout[REQUEST_PRIORITY_MAX];
    
    // Processing timing
    uint32_t total_processing_time_ms[REQUEST_PRIORITY_MAX];
    uint32_t min_processing_time_ms[REQUEST_PRIORITY_MAX];
    uint32_t max_processing_time_ms[REQUEST_PRIORITY_MAX];
    
    // System metrics
    uint32_t peak_queue_depth[REQUEST_PRIORITY_MAX];
    uint32_t load_shedding_events;
    uint32_t emergency_mode_activations;
    uint32_t memory_allocation_failures;
    
    // Task performance
    uint32_t task_iterations[TEST_TASK_MAX];
    uint32_t task_errors[TEST_TASK_MAX];
    
    // Current state
    test_scenario_t current_scenario;
    bool is_running;
    bool emergency_mode_active;
    uint8_t current_system_load;
} test_execution_stats_t;

/**
 * @brief Test result summary
 */
typedef struct {
    bool test_completed_successfully;
    uint32_t total_requests_generated;
    uint32_t total_requests_processed;
    uint32_t total_requests_dropped;
    float average_processing_time_ms;
    float system_load_average;
    uint32_t peak_memory_usage_bytes;
    const char *failure_reason;
} test_result_summary_t;

/* =============================================================================
 * DEBUG MACROS
 * =============================================================================
 */

#if DEBUG_PRIORITY_TEST_SUITE
    #define PRIORITY_TEST_DEBUG(format, ...) \
        ESP_LOGI(PRIORITY_TEST_TAG, "[%lld] " format, esp_timer_get_time() / 1000, ##__VA_ARGS__)
    
    #define PRIORITY_TEST_ERROR(format, ...) \
        ESP_LOGE(PRIORITY_TEST_TAG, "[%lld] ERROR: " format, esp_timer_get_time() / 1000, ##__VA_ARGS__)
    
    #define PRIORITY_TEST_WARN(format, ...) \
        ESP_LOGW(PRIORITY_TEST_TAG, "[%lld] WARN: " format, esp_timer_get_time() / 1000, ##__VA_ARGS__)
    
    #define PRIORITY_TEST_SCENARIO_LOG(scenario, format, ...) \
        ESP_LOGI(PRIORITY_TEST_TAG, "[%s] " format, test_scenario_to_string(scenario), ##__VA_ARGS__)
#else
    #define PRIORITY_TEST_DEBUG(format, ...)
    #define PRIORITY_TEST_ERROR(format, ...)
    #define PRIORITY_TEST_WARN(format, ...)
    #define PRIORITY_TEST_SCENARIO_LOG(scenario, format, ...)
#endif

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the priority test suite
 * @param config Test configuration (NULL for defaults)
 * @return true if initialization successful, false otherwise
 */
bool priority_test_suite_init(const priority_test_config_t *config);

/**
 * @brief Start the test suite with specified scenario
 * @param scenario Test scenario to run
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t priority_test_suite_start(test_scenario_t scenario);

/**
 * @brief Stop the currently running test suite
 */
void priority_test_suite_stop(void);

/**
 * @brief Check if test suite is currently running
 * @return true if running, false otherwise
 */
bool priority_test_suite_is_running(void);

/**
 * @brief Get current test execution statistics
 * @param stats Pointer to statistics structure to fill
 * @return true if statistics retrieved successfully, false otherwise
 */
bool priority_test_suite_get_stats(test_execution_stats_t *stats);

/**
 * @brief Get test result summary
 * @param summary Pointer to summary structure to fill
 * @return true if summary retrieved successfully, false otherwise
 */
bool priority_test_suite_get_summary(test_result_summary_t *summary);

/**
 * @brief Print current test status to console
 */
void priority_test_suite_print_status(void);

/**
 * @brief Print detailed test statistics to console
 */
void priority_test_suite_print_statistics(void);

/**
 * @brief Print test result summary to console
 */
void priority_test_suite_print_summary(void);

/**
 * @brief Reset test statistics
 */
void priority_test_suite_reset_statistics(void);

/**
 * @brief Cleanup test suite resources
 */
void priority_test_suite_cleanup(void);

/* =============================================================================
 * SCENARIO CONTROL FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Run a specific test scenario for specified duration
 * @param scenario Test scenario to run
 * @param duration_ms Duration to run test (0 for default)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t priority_test_suite_run_scenario(test_scenario_t scenario, uint32_t duration_ms);

/**
 * @brief Manually trigger emergency mode for testing
 * @param timeout_ms Emergency mode timeout (0 for no timeout)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t priority_test_suite_trigger_emergency_mode(uint32_t timeout_ms);

/**
 * @brief Set load generator parameters
 * @param requests_per_second Request generation rate
 * @param payload_size Size of generated requests
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t priority_test_suite_set_load_level(uint32_t requests_per_second, size_t payload_size);

/**
 * @brief Enable or disable specific test tasks
 * @param task_type Type of test task
 * @param enable true to enable, false to disable
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t priority_test_suite_enable_task(test_task_type_t task_type, bool enable);

/* =============================================================================
 * UTILITY FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Get default test configuration
 * @param config Pointer to configuration structure to fill
 */
void priority_test_suite_get_default_config(priority_test_config_t *config);

/**
 * @brief Convert test scenario enum to string
 * @param scenario Test scenario
 * @return String representation of scenario
 */
const char* test_scenario_to_string(test_scenario_t scenario);

/**
 * @brief Convert test task type enum to string
 * @param task_type Test task type
 * @return String representation of task type
 */
const char* test_task_type_to_string(test_task_type_t task_type);

/**
 * @brief Validate test configuration
 * @param config Configuration to validate
 * @return true if configuration is valid, false otherwise
 */
bool priority_test_suite_validate_config(const priority_test_config_t *config);

/**
 * @brief Get test suite health status
 * @return true if test suite is healthy, false otherwise
 */
bool priority_test_suite_health_check(void);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_PRIORITY_TEST_SUITE

#endif // REQUEST_PRIORITY_TEST_SUITE_H
