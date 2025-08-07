/**
 * @file request_priority_test_suite.c
 * @brief Request Priority Management Test Suite Implementation for SNRv9
 * 
 * This module provides comprehensive testing capabilities for the request priority
 * management system, including load testing, priority validation, and performance
 * monitoring with conditionally compiled test scenarios.
 */

#include "request_priority_test_suite.h"

#if DEBUG_PRIORITY_TEST_SUITE

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "request_priority_manager.h"
#include "request_queue.h"
#include "psram_manager.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* =============================================================================
 * PRIVATE CONSTANTS
 * =============================================================================
 */

#define TEST_SUITE_MUTEX_TIMEOUT_MS 100
#define TEST_TASK_NOTIFICATION_TIMEOUT_MS 1000
#define MOCK_REQUEST_URI_MAX_LEN 64
#define MOCK_REQUEST_BUFFER_SIZE 1024

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static priority_test_config_t test_config;
static test_task_config_t task_configs[TEST_TASK_MAX];
static test_execution_stats_t execution_stats;
static test_result_summary_t result_summary;
static bool is_initialized = false;
static bool is_running = false;
static SemaphoreHandle_t test_mutex = NULL;
static TaskHandle_t monitor_task_handle = NULL;

/* Test scenario names for debugging */
static const char* scenario_names[TEST_SCENARIO_MAX] = {
    "NORMAL_OPERATION",
    "HIGH_LOAD",
    "EMERGENCY_MODE",
    "MEMORY_STRESS",
    "QUEUE_SATURATION",
    "CUSTOM"
};

/* Test task names for debugging */
static const char* task_type_names[TEST_TASK_MAX] = {
    "EMERGENCY_SIM",
    "IO_CONTROL_SIM",
    "AUTH_SIM",
    "DASHBOARD_SIM",
    "BACKGROUND_SIM",
    "LOAD_GENERATOR"
};

/* Mock URI patterns for different request types */
static const char* mock_uris[REQUEST_PRIORITY_MAX][3] = {
    // EMERGENCY
    {"/api/emergency/stop", "/emergency-shutdown", "/api/emergency/alert"},
    // IO_CRITICAL
    {"/api/io/points/1/set", "/api/irrigation/zones/1/activate", "/api/io/points/2/set"},
    // AUTHENTICATION
    {"/api/auth/login", "/api/auth/logout", "/api/auth/refresh"},
    // UI_CRITICAL
    {"/api/status", "/api/dashboard/data", "/api/io/points"},
    // NORMAL
    {"/index.html", "/style.css", "/app.js"},
    // BACKGROUND
    {"/api/logs/download", "/api/statistics/export", "/api/backup/create"}
};

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static bool init_test_tasks(void);
static void cleanup_test_tasks(void);
static bool create_test_task(test_task_type_t task_type);
static void stop_test_task(test_task_type_t task_type);

static void test_monitor_task(void *pvParameters);
static void emergency_sim_task(void *pvParameters);
static void io_control_sim_task(void *pvParameters);
static void auth_sim_task(void *pvParameters);
static void dashboard_sim_task(void *pvParameters);
static void background_sim_task(void *pvParameters);
static void load_generator_task(void *pvParameters);

static esp_err_t generate_mock_request(request_priority_t priority, size_t payload_size);
static httpd_req_t* create_mock_request(const char *uri, httpd_method_t method, size_t content_length);
static void free_mock_request(httpd_req_t *req);
static uint32_t get_random_interval(uint32_t min_ms, uint32_t max_ms);
static uint32_t get_current_time_ms(void);
static void update_test_statistics(request_priority_t priority, bool success, uint32_t processing_time_ms);
static void configure_scenario_settings(test_scenario_t scenario);
static bool validate_test_configuration(const priority_test_config_t *config);

static bool validate_test_configuration(const priority_test_config_t *config) {
    return priority_test_suite_validate_config(config);
}

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool priority_test_suite_init(const priority_test_config_t *config) {
    if (is_initialized) {
        PRIORITY_TEST_WARN("Test suite already initialized");
        return true;
    }
    
    PRIORITY_TEST_DEBUG("Initializing request priority test suite");
    
    // Use provided config or defaults
    if (config) {
        if (!validate_test_configuration(config)) {
            PRIORITY_TEST_ERROR("Invalid test configuration provided");
            return false;
        }
        memcpy(&test_config, config, sizeof(priority_test_config_t));
    } else {
        priority_test_suite_get_default_config(&test_config);
    }
    
    // Create test mutex
    test_mutex = xSemaphoreCreateMutex();
    if (!test_mutex) {
        PRIORITY_TEST_ERROR("Failed to create test mutex");
        return false;
    }
    
    // Initialize statistics
    memset(&execution_stats, 0, sizeof(test_execution_stats_t));
    memset(&result_summary, 0, sizeof(test_result_summary_t));
    
    // Initialize minimum processing times to maximum value
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        execution_stats.min_processing_time_ms[i] = UINT32_MAX;
    }
    
    // Initialize task configurations
    if (!init_test_tasks()) {
        PRIORITY_TEST_ERROR("Failed to initialize test tasks");
        vSemaphoreDelete(test_mutex);
        return false;
    }
    
    is_initialized = true;
    PRIORITY_TEST_DEBUG("Test suite initialized successfully");
    return true;
}

esp_err_t priority_test_suite_start(test_scenario_t scenario) {
    if (!is_initialized) {
        PRIORITY_TEST_ERROR("Test suite not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (is_running) {
        PRIORITY_TEST_WARN("Test suite already running, stopping current test");
        priority_test_suite_stop();
        vTaskDelay(pdMS_TO_TICKS(1000)); // Allow cleanup
    }
    
    if (scenario >= TEST_SCENARIO_MAX) {
        PRIORITY_TEST_ERROR("Invalid test scenario: %d", scenario);
        return ESP_ERR_INVALID_ARG;
    }
    
    PRIORITY_TEST_SCENARIO_LOG(scenario, "Starting test scenario: %s", scenario_names[scenario]);
    
    // Take mutex for configuration
    if (xSemaphoreTake(test_mutex, pdMS_TO_TICKS(TEST_SUITE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        PRIORITY_TEST_ERROR("Failed to acquire test mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    // Configure scenario-specific settings
    configure_scenario_settings(scenario);
    
    // Reset statistics
    memset(&execution_stats, 0, sizeof(test_execution_stats_t));
    execution_stats.test_start_time = get_current_time_ms();
    execution_stats.test_duration_ms = test_config.test_duration_ms;
    execution_stats.current_scenario = scenario;
    execution_stats.is_running = true;
    
    // Initialize minimum processing times
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        execution_stats.min_processing_time_ms[i] = UINT32_MAX;
    }
    
    // Create test tasks based on configuration
    bool task_creation_success = true;
    for (int i = 0; i < TEST_TASK_MAX; i++) {
        if (task_configs[i].enabled) {
            if (!create_test_task((test_task_type_t)i)) {
                PRIORITY_TEST_ERROR("Failed to create %s task", task_type_names[i]);
                task_creation_success = false;
                break;
            }
        }
    }
    
    if (!task_creation_success) {
        cleanup_test_tasks();
        xSemaphoreGive(test_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // Create monitor task
    BaseType_t result = xTaskCreatePinnedToCore(
        test_monitor_task,
        "test_monitor",
        TEST_TASK_STACK_SIZE,
        NULL,
        TEST_TASK_PRIORITY + 1, // Higher priority than test tasks
        &monitor_task_handle,
        TEST_TASK_CORE_AFFINITY
    );
    
    if (result != pdPASS) {
        PRIORITY_TEST_ERROR("Failed to create monitor task");
        cleanup_test_tasks();
        xSemaphoreGive(test_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    is_running = true;
    xSemaphoreGive(test_mutex);
    
    PRIORITY_TEST_DEBUG("Test scenario %s started successfully", scenario_names[scenario]);
    return ESP_OK;
}

void priority_test_suite_stop(void) {
    if (!is_initialized || !is_running) {
        return;
    }
    
    PRIORITY_TEST_DEBUG("Stopping test suite");
    
    if (xSemaphoreTake(test_mutex, pdMS_TO_TICKS(TEST_SUITE_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        is_running = false;
        execution_stats.is_running = false;
        xSemaphoreGive(test_mutex);
    }
    
    // Stop monitor task
    if (monitor_task_handle) {
        xTaskNotifyGive(monitor_task_handle);
        vTaskDelay(pdMS_TO_TICKS(100)); // Allow graceful shutdown
        
        if (eTaskGetState(monitor_task_handle) != eDeleted) {
            vTaskDelete(monitor_task_handle);
        }
        monitor_task_handle = NULL;
    }
    
    // Stop all test tasks
    cleanup_test_tasks();
    
    // Generate final summary
    if (test_config.enable_statistics_logging) {
        priority_test_suite_print_summary();
    }
    
    PRIORITY_TEST_DEBUG("Test suite stopped");
}

bool priority_test_suite_is_running(void) {
    return is_running;
}

bool priority_test_suite_get_stats(test_execution_stats_t *stats) {
    if (!is_initialized || !stats) {
        return false;
    }
    
    if (xSemaphoreTake(test_mutex, pdMS_TO_TICKS(TEST_SUITE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    
    // Update elapsed time
    if (is_running) {
        execution_stats.elapsed_time_ms = get_current_time_ms() - execution_stats.test_start_time;
    }
    
    // Copy statistics
    memcpy(stats, &execution_stats, sizeof(test_execution_stats_t));
    
    xSemaphoreGive(test_mutex);
    return true;
}

bool priority_test_suite_get_summary(test_result_summary_t *summary) {
    if (!is_initialized || !summary) {
        return false;
    }
    
    if (xSemaphoreTake(test_mutex, pdMS_TO_TICKS(TEST_SUITE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    
    // Calculate summary statistics
    result_summary.total_requests_generated = 0;
    result_summary.total_requests_processed = 0;
    result_summary.total_requests_dropped = 0;
    
    uint32_t total_processing_time = 0;
    uint32_t processed_count = 0;
    
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        result_summary.total_requests_generated += execution_stats.requests_generated[i];
        result_summary.total_requests_processed += execution_stats.requests_processed[i];
        result_summary.total_requests_dropped += execution_stats.requests_dropped[i];
        
        if (execution_stats.requests_processed[i] > 0) {
            total_processing_time += execution_stats.total_processing_time_ms[i];
            processed_count += execution_stats.requests_processed[i];
        }
    }
    
    result_summary.average_processing_time_ms = processed_count > 0 ? 
        (float)total_processing_time / processed_count : 0.0f;
    
    result_summary.system_load_average = execution_stats.current_system_load;
    result_summary.peak_memory_usage_bytes = 0; // Would need memory tracking integration
    
    result_summary.test_completed_successfully = 
        (execution_stats.elapsed_time_ms >= execution_stats.test_duration_ms) &&
        (result_summary.total_requests_processed > 0);
    
    if (!result_summary.test_completed_successfully) {
        if (result_summary.total_requests_processed == 0) {
            result_summary.failure_reason = "No requests processed";
        } else if (execution_stats.elapsed_time_ms < execution_stats.test_duration_ms) {
            result_summary.failure_reason = "Test stopped prematurely";
        } else {
            result_summary.failure_reason = "Unknown failure";
        }
    } else {
        result_summary.failure_reason = NULL;
    }
    
    // Copy summary
    memcpy(summary, &result_summary, sizeof(test_result_summary_t));
    
    xSemaphoreGive(test_mutex);
    return true;
}

void priority_test_suite_print_status(void) {
    if (!is_initialized) {
        ESP_LOGI(PRIORITY_TEST_TAG, "Test suite not initialized");
        return;
    }
    
    test_execution_stats_t stats;
    if (!priority_test_suite_get_stats(&stats)) {
        ESP_LOGE(PRIORITY_TEST_TAG, "Failed to get test statistics");
        return;
    }
    
    ESP_LOGI(PRIORITY_TEST_TAG, "=== PRIORITY TEST SUITE STATUS ===");
    ESP_LOGI(PRIORITY_TEST_TAG, "Test Duration: %lu/%lu seconds", 
             stats.elapsed_time_ms / 1000, stats.test_duration_ms / 1000);
    ESP_LOGI(PRIORITY_TEST_TAG, "Current Scenario: %s", scenario_names[stats.current_scenario]);
    ESP_LOGI(PRIORITY_TEST_TAG, "Status: %s", stats.is_running ? "RUNNING" : "STOPPED");
    
    ESP_LOGI(PRIORITY_TEST_TAG, "Queue Depths:");
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        uint16_t depth = request_queue_get_depth((request_priority_t)i);
        ESP_LOGI(PRIORITY_TEST_TAG, "  %s: %d requests", 
                 request_queue_priority_to_string((request_priority_t)i), depth);
    }
    
    ESP_LOGI(PRIORITY_TEST_TAG, "Processing Statistics:");
    ESP_LOGI(PRIORITY_TEST_TAG, "  Total Generated: %lu requests", 
             stats.requests_generated[0] + stats.requests_generated[1] + 
             stats.requests_generated[2] + stats.requests_generated[3] + 
             stats.requests_generated[4] + stats.requests_generated[5]);
    ESP_LOGI(PRIORITY_TEST_TAG, "  Total Processed: %lu requests", 
             stats.requests_processed[0] + stats.requests_processed[1] + 
             stats.requests_processed[2] + stats.requests_processed[3] + 
             stats.requests_processed[4] + stats.requests_processed[5]);
    ESP_LOGI(PRIORITY_TEST_TAG, "  Dropped Requests: %lu", 
             stats.requests_dropped[0] + stats.requests_dropped[1] + 
             stats.requests_dropped[2] + stats.requests_dropped[3] + 
             stats.requests_dropped[4] + stats.requests_dropped[5]);
    ESP_LOGI(PRIORITY_TEST_TAG, "  Emergency Activations: %lu", stats.emergency_mode_activations);
    ESP_LOGI(PRIORITY_TEST_TAG, "  Load Shedding Events: %lu", stats.load_shedding_events);
    
    ESP_LOGI(PRIORITY_TEST_TAG, "System Load: %d%%", stats.current_system_load);
    ESP_LOGI(PRIORITY_TEST_TAG, "Emergency Mode: %s", 
             stats.emergency_mode_active ? "ACTIVE" : "INACTIVE");
}

void priority_test_suite_print_statistics(void) {
    if (!is_initialized) {
        return;
    }
    
    test_execution_stats_t stats;
    if (!priority_test_suite_get_stats(&stats)) {
        return;
    }
    
    ESP_LOGI(PRIORITY_TEST_TAG, "=== DETAILED TEST STATISTICS ===");
    
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        if (stats.requests_generated[i] > 0) {
            uint32_t avg_time = stats.requests_processed[i] > 0 ? 
                stats.total_processing_time_ms[i] / stats.requests_processed[i] : 0;
            
            ESP_LOGI(PRIORITY_TEST_TAG, "%s Priority:", 
                     request_queue_priority_to_string((request_priority_t)i));
            ESP_LOGI(PRIORITY_TEST_TAG, "  Generated: %lu, Processed: %lu, Dropped: %lu", 
                     stats.requests_generated[i], stats.requests_processed[i], 
                     stats.requests_dropped[i]);
            ESP_LOGI(PRIORITY_TEST_TAG, "  Timing: avg=%lu ms, min=%lu ms, max=%lu ms", 
                     avg_time, 
                     stats.min_processing_time_ms[i] == UINT32_MAX ? 0 : stats.min_processing_time_ms[i],
                     stats.max_processing_time_ms[i]);
            ESP_LOGI(PRIORITY_TEST_TAG, "  Peak Queue Depth: %lu", stats.peak_queue_depth[i]);
        }
    }
    
    ESP_LOGI(PRIORITY_TEST_TAG, "Task Performance:");
    for (int i = 0; i < TEST_TASK_MAX; i++) {
        if (task_configs[i].enabled) {
            ESP_LOGI(PRIORITY_TEST_TAG, "  %s: %lu iterations, %lu errors", 
                     task_type_names[i], stats.task_iterations[i], stats.task_errors[i]);
        }
    }
}

void priority_test_suite_print_summary(void) {
    test_result_summary_t summary;
    if (!priority_test_suite_get_summary(&summary)) {
        ESP_LOGE(PRIORITY_TEST_TAG, "Failed to get test summary");
        return;
    }
    
    ESP_LOGI(PRIORITY_TEST_TAG, "=== TEST RESULT SUMMARY ===");
    ESP_LOGI(PRIORITY_TEST_TAG, "Test Completed: %s", 
             summary.test_completed_successfully ? "SUCCESS" : "FAILED");
    
    if (!summary.test_completed_successfully && summary.failure_reason) {
        ESP_LOGI(PRIORITY_TEST_TAG, "Failure Reason: %s", summary.failure_reason);
    }
    
    ESP_LOGI(PRIORITY_TEST_TAG, "Total Requests: Generated=%lu, Processed=%lu, Dropped=%lu", 
             summary.total_requests_generated, summary.total_requests_processed, 
             summary.total_requests_dropped);
    ESP_LOGI(PRIORITY_TEST_TAG, "Average Processing Time: %.2f ms", 
             summary.average_processing_time_ms);
    ESP_LOGI(PRIORITY_TEST_TAG, "System Load Average: %.1f%%", summary.system_load_average);
    
    if (summary.total_requests_generated > 0) {
        float success_rate = (float)summary.total_requests_processed / summary.total_requests_generated * 100.0f;
        ESP_LOGI(PRIORITY_TEST_TAG, "Success Rate: %.1f%%", success_rate);
    }
}

void priority_test_suite_reset_statistics(void) {
    if (!is_initialized) {
        return;
    }
    
    if (xSemaphoreTake(test_mutex, pdMS_TO_TICKS(TEST_SUITE_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        memset(&execution_stats, 0, sizeof(test_execution_stats_t));
        memset(&result_summary, 0, sizeof(test_result_summary_t));
        
        // Reset minimum processing times
        for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
            execution_stats.min_processing_time_ms[i] = UINT32_MAX;
        }
        
        xSemaphoreGive(test_mutex);
    }
    
    PRIORITY_TEST_DEBUG("Test statistics reset");
}

void priority_test_suite_cleanup(void) {
    if (!is_initialized) {
        return;
    }
    
    PRIORITY_TEST_DEBUG("Cleaning up test suite");
    
    // Stop if running
    if (is_running) {
        priority_test_suite_stop();
    }
    
    // Cleanup tasks
    cleanup_test_tasks();
    
    // Destroy mutex
    if (test_mutex) {
        vSemaphoreDelete(test_mutex);
        test_mutex = NULL;
    }
    
    is_initialized = false;
    PRIORITY_TEST_DEBUG("Test suite cleanup complete");
}

/* =============================================================================
 * SCENARIO CONTROL FUNCTIONS
 * =============================================================================
 */

esp_err_t priority_test_suite_run_scenario(test_scenario_t scenario, uint32_t duration_ms) {
    if (duration_ms > 0) {
        test_config.test_duration_ms = duration_ms;
    }
    
    return priority_test_suite_start(scenario);
}

esp_err_t priority_test_suite_trigger_emergency_mode(uint32_t timeout_ms) {
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    PRIORITY_TEST_DEBUG("Triggering emergency mode (timeout: %lu ms)", timeout_ms);
    
    bool result = request_priority_enter_emergency_mode(timeout_ms);
    if (result) {
        execution_stats.emergency_mode_activations++;
        execution_stats.emergency_mode_active = true;
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

esp_err_t priority_test_suite_set_load_level(uint32_t requests_per_second, size_t payload_size) {
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (requests_per_second > MAX_LOAD_GEN_RATE_RPS || payload_size > MAX_PAYLOAD_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    
    test_config.load_gen_config.requests_per_second = requests_per_second;
    test_config.load_gen_config.payload_size = payload_size;
    
    PRIORITY_TEST_DEBUG("Load level set to %lu RPS, %zu bytes payload", 
                       requests_per_second, payload_size);
    
    return ESP_OK;
}

esp_err_t priority_test_suite_enable_task(test_task_type_t task_type, bool enable) {
    if (!is_initialized || task_type >= TEST_TASK_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    task_configs[task_type].enabled = enable;
    
    PRIORITY_TEST_DEBUG("Task %s %s", task_type_names[task_type], 
                       enable ? "enabled" : "disabled");
    
    return ESP_OK;
}

/* =============================================================================
 * UTILITY FUNCTIONS
 * =============================================================================
 */

void priority_test_suite_get_default_config(priority_test_config_t *config) {
    if (!config) {
        return;
    }
    
    memset(config, 0, sizeof(priority_test_config_t));
    
    // Test control
    config->test_duration_ms = DEBUG_PRIORITY_TEST_DURATION_MS;
    config->report_interval_ms = DEBUG_PRIORITY_TEST_REPORT_INTERVAL_MS;
    config->active_scenario = TEST_SCENARIO_NORMAL_OPERATION;
    
    // Task enables (all enabled by default)
    config->enable_emergency_sim = true;
    config->enable_io_control_sim = true;
    config->enable_auth_sim = true;
    config->enable_dashboard_sim = true;
    config->enable_background_sim = true;
    config->enable_load_generator = false; // Disabled by default
    
    // Load generator settings
    config->load_gen_config.requests_per_second = DEBUG_PRIORITY_TEST_LOAD_RATE_RPS;
    config->load_gen_config.payload_size = DEBUG_PRIORITY_TEST_PAYLOAD_SIZE;
    config->load_gen_config.priority = REQUEST_PRIORITY_NORMAL;
    config->load_gen_config.variable_priority = false;
    config->load_gen_config.variable_payload_size = false;
    
    // Emergency mode testing
    config->emergency_config.auto_trigger = false;
    config->emergency_config.trigger_delay_ms = 30000;
    config->emergency_config.emergency_timeout_ms = DEBUG_PRIORITY_TEST_EMERGENCY_TIMEOUT_MS;
    config->emergency_config.test_timeout_recovery = true;
    
    // Advanced options
    config->enable_statistics_logging = DEBUG_PRIORITY_TEST_STATISTICS;
    config->enable_detailed_timing = DEBUG_PRIORITY_TEST_DETAILED;
    config->enable_memory_tracking = DEBUG_PRIORITY_TEST_MEMORY;
    config->auto_cleanup_on_completion = DEBUG_PRIORITY_TEST_AUTO_CLEANUP;
}

const char* test_scenario_to_string(test_scenario_t scenario) {
    if (scenario >= TEST_SCENARIO_MAX) {
        return "UNKNOWN";
    }
    return scenario_names[scenario];
}

const char* test_task_type_to_string(test_task_type_t task_type) {
    if (task_type >= TEST_TASK_MAX) {
        return "UNKNOWN";
    }
    return task_type_names[task_type];
}

bool priority_test_suite_validate_config(const priority_test_config_t *config) {
    if (!config) {
        return false;
    }
    
    // Validate test duration
    if (config->test_duration_ms == 0 || config->test_duration_ms > MAX_TEST_DURATION_MS) {
        PRIORITY_TEST_ERROR("Invalid test duration: %lu ms", config->test_duration_ms);
        return false;
    }
    
    // Validate report interval
    if (config->report_interval_ms < MIN_REPORT_INTERVAL_MS) {
        PRIORITY_TEST_ERROR("Invalid report interval: %lu ms", config->report_interval_ms);
        return false;
    }
    
    // Validate load generator settings
    if (config->load_gen_config.requests_per_second > MAX_LOAD_GEN_RATE_RPS) {
        PRIORITY_TEST_ERROR("Invalid load rate: %lu RPS", config->load_gen_config.requests_per_second);
        return false;
    }
    
    if (config->load_gen_config.payload_size > MAX_PAYLOAD_SIZE) {
        PRIORITY_TEST_ERROR("Invalid payload size: %zu bytes", config->load_gen_config.payload_size);
        return false;
    }
    
    return true;
}

bool priority_test_suite_health_check(void) {
    if (!is_initialized) {
        return false;
    }
    
    // Check mutex
    if (!test_mutex) {
        return false;
    }
    
    // Check if running tasks are healthy
    if (is_running) {
        for (int i = 0; i < TEST_TASK_MAX; i++) {
            if (task_configs[i].enabled && task_configs[i].task_handle) {
                eTaskState state = eTaskGetState(task_configs[i].task_handle);
                if (state == eDeleted || state == eInvalid) {
                    return false;
                }
            }
        }
    }
    
    return true;
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static bool init_test_tasks(void) {
    // Initialize task configurations
    memset(task_configs, 0, sizeof(task_configs));
    
    // Emergency simulator
    task_configs[TEST_TASK_EMERGENCY_SIM] = (test_task_config_t){
        .task_type = TEST_TASK_EMERGENCY_SIM,
        .task_name = "emergency_sim",
        .enabled = test_config.enable_emergency_sim,
        .interval_min_ms = EMERGENCY_SIM_INTERVAL_MIN,
        .interval_max_ms = EMERGENCY_SIM_INTERVAL_MAX,
        .payload_size = 512,
        .priority = REQUEST_PRIORITY_EMERGENCY,
        .task_handle = NULL
    };
    
    // IO control simulator
    task_configs[TEST_TASK_IO_CONTROL_SIM] = (test_task_config_t){
        .task_type = TEST_TASK_IO_CONTROL_SIM,
        .task_name = "io_control_sim",
        .enabled = test_config.enable_io_control_sim,
        .interval_min_ms = IO_CONTROL_SIM_INTERVAL_MIN,
        .interval_max_ms = IO_CONTROL_SIM_INTERVAL_MAX,
        .payload_size = 1024,
        .priority = REQUEST_PRIORITY_IO_CRITICAL,
        .task_handle = NULL
    };
    
    // Authentication simulator
    task_configs[TEST_TASK_AUTH_SIM] = (test_task_config_t){
        .task_type = TEST_TASK_AUTH_SIM,
        .task_name = "auth_sim",
        .enabled = test_config.enable_auth_sim,
        .interval_min_ms = AUTH_SIM_INTERVAL_MIN,
        .interval_max_ms = AUTH_SIM_INTERVAL_MAX,
        .payload_size = 1024,
        .priority = REQUEST_PRIORITY_AUTHENTICATION,
        .task_handle = NULL
    };
    
    // Dashboard simulator
    task_configs[TEST_TASK_DASHBOARD_SIM] = (test_task_config_t){
        .task_type = TEST_TASK_DASHBOARD_SIM,
        .task_name = "dashboard_sim",
        .enabled = test_config.enable_dashboard_sim,
        .interval_min_ms = DASHBOARD_SIM_INTERVAL_MIN,
        .interval_max_ms = DASHBOARD_SIM_INTERVAL_MAX,
        .payload_size = 2048,
        .priority = REQUEST_PRIORITY_UI_CRITICAL,
        .task_handle = NULL
    };
    
    // Background simulator
    task_configs[TEST_TASK_BACKGROUND_SIM] = (test_task_config_t){
        .task_type = TEST_TASK_BACKGROUND_SIM,
        .task_name = "background_sim",
        .enabled = test_config.enable_background_sim,
        .interval_min_ms = BACKGROUND_SIM_INTERVAL_MIN,
        .interval_max_ms = BACKGROUND_SIM_INTERVAL_MAX,
        .payload_size = 4096,
        .priority = REQUEST_PRIORITY_BACKGROUND,
        .task_handle = NULL
    };
    
    // Load generator
    task_configs[TEST_TASK_LOAD_GENERATOR] = (test_task_config_t){
        .task_type = TEST_TASK_LOAD_GENERATOR,
        .task_name = "load_generator",
        .enabled = test_config.enable_load_generator,
        .interval_min_ms = 100, // High frequency
        .interval_max_ms = 1000,
        .payload_size = test_config.load_gen_config.payload_size,
        .priority = test_config.load_gen_config.priority,
        .task_handle = NULL
    };
    
    return true;
}

static void cleanup_test_tasks(void) {
    for (int i = 0; i < TEST_TASK_MAX; i++) {
        stop_test_task((test_task_type_t)i);
    }
}

static bool create_test_task(test_task_type_t task_type) {
    if (task_type >= TEST_TASK_MAX) {
        return false;
    }
    
    test_task_config_t *config = &task_configs[task_type];
    
    // Select task function
    TaskFunction_t task_function = NULL;
    switch (task_type) {
        case TEST_TASK_EMERGENCY_SIM:
            task_function = emergency_sim_task;
            break;
        case TEST_TASK_IO_CONTROL_SIM:
            task_function = io_control_sim_task;
            break;
        case TEST_TASK_AUTH_SIM:
            task_function = auth_sim_task;
            break;
        case TEST_TASK_DASHBOARD_SIM:
            task_function = dashboard_sim_task;
            break;
        case TEST_TASK_BACKGROUND_SIM:
            task_function = background_sim_task;
            break;
        case TEST_TASK_LOAD_GENERATOR:
            task_function = load_generator_task;
            break;
        default:
            return false;
    }
    
    // Create task
    BaseType_t result = xTaskCreatePinnedToCore(
        task_function,
        config->task_name,
        TEST_TASK_STACK_SIZE,
        (void*)task_type,
        TEST_TASK_PRIORITY,
        &config->task_handle,
        TEST_TASK_CORE_AFFINITY
    );
    
    if (result != pdPASS) {
        PRIORITY_TEST_ERROR("Failed to create task %s", config->task_name);
        return false;
    }
    
    PRIORITY_TEST_DEBUG("Created test task: %s", config->task_name);
    return true;
}

static void stop_test_task(test_task_type_t task_type) {
    if (task_type >= TEST_TASK_MAX) {
        return;
    }
    
    test_task_config_t *config = &task_configs[task_type];
    
    if (config->task_handle) {
        // Send stop notification
        xTaskNotifyGive(config->task_handle);
        
        // Wait for graceful shutdown
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Force delete if still running
        if (eTaskGetState(config->task_handle) != eDeleted) {
            vTaskDelete(config->task_handle);
        }
        
        config->task_handle = NULL;
        PRIORITY_TEST_DEBUG("Stopped test task: %s", config->task_name);
    }
}

/* =============================================================================
 * TEST TASK IMPLEMENTATIONS
 * =============================================================================
 */

static void test_monitor_task(void *pvParameters) {
    uint32_t last_report_time = 0;
    
    PRIORITY_TEST_DEBUG("Test monitor task started");
    
    while (is_running) {
        uint32_t current_time = get_current_time_ms();
        
        // Check for test timeout
        if (current_time - execution_stats.test_start_time >= execution_stats.test_duration_ms) {
            PRIORITY_TEST_DEBUG("Test duration completed, stopping test");
            priority_test_suite_stop();
            break;
        }
        
        // Periodic status reports
        if (current_time - last_report_time >= test_config.report_interval_ms) {
            if (test_config.enable_statistics_logging) {
                priority_test_suite_print_status();
            }
            last_report_time = current_time;
        }
        
        // Update current system load
        execution_stats.current_system_load = request_priority_get_load_percentage();
        execution_stats.emergency_mode_active = 
            (request_priority_get_system_mode() == SYSTEM_MODE_EMERGENCY);
        
        // Check for stop notification
        if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(1000)) > 0) {
            break;
        }
    }
    
    PRIORITY_TEST_DEBUG("Test monitor task exiting");
    vTaskDelete(NULL);
}

static void emergency_sim_task(void *pvParameters) {
    test_task_type_t task_type = (test_task_type_t)pvParameters;
    
    PRIORITY_TEST_DEBUG("Emergency simulator task started");
    
    while (is_running) {
        // Generate emergency request
        esp_err_t result = generate_mock_request(REQUEST_PRIORITY_EMERGENCY, 
                                                task_configs[task_type].payload_size);
        
        if (result == ESP_OK) {
            execution_stats.task_iterations[task_type]++;
        } else {
            execution_stats.task_errors[task_type]++;
        }
        
        // Wait for next interval
        uint32_t interval = get_random_interval(task_configs[task_type].interval_min_ms,
                                               task_configs[task_type].interval_max_ms);
        
        if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(interval)) > 0) {
            break; // Stop notification received
        }
    }
    
    PRIORITY_TEST_DEBUG("Emergency simulator task exiting");
    vTaskDelete(NULL);
}

static void io_control_sim_task(void *pvParameters) {
    test_task_type_t task_type = (test_task_type_t)pvParameters;
    
    PRIORITY_TEST_DEBUG("IO control simulator task started");
    
    while (is_running) {
        // Generate IO control request
        esp_err_t result = generate_mock_request(REQUEST_PRIORITY_IO_CRITICAL, 
                                                task_configs[task_type].payload_size);
        
        if (result == ESP_OK) {
            execution_stats.task_iterations[task_type]++;
        } else {
            execution_stats.task_errors[task_type]++;
        }
        
        // Wait for next interval
        uint32_t interval = get_random_interval(task_configs[task_type].interval_min_ms,
                                               task_configs[task_type].interval_max_ms);
        
        if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(interval)) > 0) {
            break; // Stop notification received
        }
    }
    
    PRIORITY_TEST_DEBUG("IO control simulator task exiting");
    vTaskDelete(NULL);
}

static void auth_sim_task(void *pvParameters) {
    test_task_type_t task_type = (test_task_type_t)pvParameters;
    
    PRIORITY_TEST_DEBUG("Authentication simulator task started");
    
    while (is_running) {
        // Generate authentication request
        esp_err_t result = generate_mock_request(REQUEST_PRIORITY_AUTHENTICATION, 
                                                task_configs[task_type].payload_size);
        
        if (result == ESP_OK) {
            execution_stats.task_iterations[task_type]++;
        } else {
            execution_stats.task_errors[task_type]++;
        }
        
        // Wait for next interval
        uint32_t interval = get_random_interval(task_configs[task_type].interval_min_ms,
                                               task_configs[task_type].interval_max_ms);
        
        if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(interval)) > 0) {
            break; // Stop notification received
        }
    }
    
    PRIORITY_TEST_DEBUG("Authentication simulator task exiting");
    vTaskDelete(NULL);
}

static void dashboard_sim_task(void *pvParameters) {
    test_task_type_t task_type = (test_task_type_t)pvParameters;
    
    PRIORITY_TEST_DEBUG("Dashboard simulator task started");
    
    while (is_running) {
        // Generate dashboard request
        esp_err_t result = generate_mock_request(REQUEST_PRIORITY_UI_CRITICAL, 
                                                task_configs[task_type].payload_size);
        
        if (result == ESP_OK) {
            execution_stats.task_iterations[task_type]++;
        } else {
            execution_stats.task_errors[task_type]++;
        }
        
        // Wait for next interval
        uint32_t interval = get_random_interval(task_configs[task_type].interval_min_ms,
                                               task_configs[task_type].interval_max_ms);
        
        if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(interval)) > 0) {
            break; // Stop notification received
        }
    }
    
    PRIORITY_TEST_DEBUG("Dashboard simulator task exiting");
    vTaskDelete(NULL);
}

static void background_sim_task(void *pvParameters) {
    test_task_type_t task_type = (test_task_type_t)pvParameters;
    
    PRIORITY_TEST_DEBUG("Background simulator task started");
    
    while (is_running) {
        // Generate background request
        esp_err_t result = generate_mock_request(REQUEST_PRIORITY_BACKGROUND, 
                                                task_configs[task_type].payload_size);
        
        if (result == ESP_OK) {
            execution_stats.task_iterations[task_type]++;
        } else {
            execution_stats.task_errors[task_type]++;
        }
        
        // Wait for next interval
        uint32_t interval = get_random_interval(task_configs[task_type].interval_min_ms,
                                               task_configs[task_type].interval_max_ms);
        
        if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(interval)) > 0) {
            break; // Stop notification received
        }
    }
    
    PRIORITY_TEST_DEBUG("Background simulator task exiting");
    vTaskDelete(NULL);
}

static void load_generator_task(void *pvParameters) {
    test_task_type_t task_type = (test_task_type_t)pvParameters;
    
    PRIORITY_TEST_DEBUG("Load generator task started");
    
    uint32_t requests_per_second = test_config.load_gen_config.requests_per_second;
    uint32_t interval_ms = requests_per_second > 0 ? 1000 / requests_per_second : 1000;
    
    while (is_running) {
        // Generate load request
        request_priority_t priority = test_config.load_gen_config.priority;
        
        // Variable priority if enabled
        if (test_config.load_gen_config.variable_priority) {
            priority = (request_priority_t)(esp_random() % REQUEST_PRIORITY_MAX);
        }
        
        // Variable payload size if enabled
        size_t payload_size = test_config.load_gen_config.payload_size;
        if (test_config.load_gen_config.variable_payload_size) {
            payload_size = 512 + (esp_random() % (test_config.load_gen_config.payload_size - 512));
        }
        
        esp_err_t result = generate_mock_request(priority, payload_size);
        
        if (result == ESP_OK) {
            execution_stats.task_iterations[task_type]++;
        } else {
            execution_stats.task_errors[task_type]++;
        }
        
        // Wait for next interval
        if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(interval_ms)) > 0) {
            break; // Stop notification received
        }
    }
    
    PRIORITY_TEST_DEBUG("Load generator task exiting");
    vTaskDelete(NULL);
}

/* =============================================================================
 * HELPER FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static esp_err_t generate_mock_request(request_priority_t priority, size_t payload_size) {
    // Select random URI for this priority
    int uri_index = esp_random() % 3;
    const char *uri = mock_uris[priority][uri_index];
    
    // Create mock request
    httpd_req_t *req = create_mock_request(uri, HTTP_GET, payload_size);
    if (!req) {
        execution_stats.requests_dropped[priority]++;
        return ESP_ERR_NO_MEM;
    }
    
    // Update statistics
    execution_stats.requests_generated[priority]++;
    
    // Queue the request
    esp_err_t result = request_priority_queue_request(req, priority);
    
    if (result == ESP_OK) {
        execution_stats.requests_processed[priority]++;
        update_test_statistics(priority, true, 100); // Mock processing time
    } else {
        execution_stats.requests_dropped[priority]++;
        update_test_statistics(priority, false, 0);
    }
    
    // Free mock request
    free_mock_request(req);
    
    return result;
}

static httpd_req_t* create_mock_request(const char *uri, httpd_method_t method, size_t content_length) {
    // Allocate request structure
    httpd_req_t *req = heap_caps_malloc(sizeof(httpd_req_t), MALLOC_CAP_INTERNAL);
    if (!req) {
        return NULL;
    }
    
    memset(req, 0, sizeof(httpd_req_t));
    
    // Set basic fields
    req->method = method;
    // Copy URI into the array field (uri is an array, not a pointer)
    size_t uri_len = strlen(uri);
    if (uri_len < sizeof(req->uri)) {
        strcpy((char*)req->uri, uri);
    } else {
        // Truncate if too long
        strncpy((char*)req->uri, uri, sizeof(req->uri) - 1);
        ((char*)req->uri)[sizeof(req->uri) - 1] = '\0';
    }
    req->content_len = content_length;
    
    return req;
}

static void free_mock_request(httpd_req_t *req) {
    if (req) {
        heap_caps_free(req);
    }
}

static uint32_t get_random_interval(uint32_t min_ms, uint32_t max_ms) {
    if (min_ms >= max_ms) {
        return min_ms;
    }
    return min_ms + (esp_random() % (max_ms - min_ms));
}

static uint32_t get_current_time_ms(void) {
    return esp_timer_get_time() / 1000;
}

static void update_test_statistics(request_priority_t priority, bool success, uint32_t processing_time_ms) {
    if (priority >= REQUEST_PRIORITY_MAX) {
        return;
    }
    
    if (success) {
        execution_stats.total_processing_time_ms[priority] += processing_time_ms;
        
        if (processing_time_ms < execution_stats.min_processing_time_ms[priority]) {
            execution_stats.min_processing_time_ms[priority] = processing_time_ms;
        }
        
        if (processing_time_ms > execution_stats.max_processing_time_ms[priority]) {
            execution_stats.max_processing_time_ms[priority] = processing_time_ms;
        }
    }
    
    // Update peak queue depth
    uint16_t current_depth = request_queue_get_depth(priority);
    if (current_depth > execution_stats.peak_queue_depth[priority]) {
        execution_stats.peak_queue_depth[priority] = current_depth;
    }
}

static void configure_scenario_settings(test_scenario_t scenario) {
    switch (scenario) {
        case TEST_SCENARIO_NORMAL_OPERATION:
            // Default settings - all tasks enabled at normal rates
            break;
            
        case TEST_SCENARIO_HIGH_LOAD:
            // Enable load generator for high load testing
            test_config.enable_load_generator = true;
            test_config.load_gen_config.requests_per_second = 50;
            test_config.load_gen_config.payload_size = 4096;
            break;
            
        case TEST_SCENARIO_EMERGENCY_MODE:
            // Trigger emergency mode automatically
            test_config.emergency_config.auto_trigger = true;
            test_config.emergency_config.trigger_delay_ms = 10000; // 10 seconds
            break;
            
        case TEST_SCENARIO_MEMORY_STRESS:
            // Large payloads to stress memory allocation
            for (int i = 0; i < TEST_TASK_MAX; i++) {
                task_configs[i].payload_size *= 4; // 4x larger payloads
            }
            test_config.enable_load_generator = true;
            test_config.load_gen_config.payload_size = 16384; // 16KB payloads
            break;
            
        case TEST_SCENARIO_QUEUE_SATURATION:
            // High frequency requests to saturate queues
            test_config.enable_load_generator = true;
            test_config.load_gen_config.requests_per_second = 100;
            
            // Reduce intervals for all simulators
            for (int i = 0; i < TEST_TASK_MAX; i++) {
                task_configs[i].interval_min_ms /= 10;
                task_configs[i].interval_max_ms /= 10;
            }
            break;
            
        case TEST_SCENARIO_CUSTOM:
            // User-defined scenario - no automatic changes
            break;
            
        default:
            break;
    }
}

#endif // DEBUG_PRIORITY_TEST_SUITE
