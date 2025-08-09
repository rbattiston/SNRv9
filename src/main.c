/**
 * @file main.c
 * @brief Main application entry point for SNRv9 Irrigation Control System
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "memory_monitor.h"
#include "task_tracker.h"
#include "psram_manager.h"
#include "psram_task_examples.h"
#include "psram_test_suite.h"
#include "wifi_handler.h"
#include "web_server_manager.h"
#include "auth_manager.h"
#include "auth_controller.h"
#include "storage_manager.h"
#include "config_manager.h"
#include "io_manager.h"
#include "io_test_controller.h"
#include "debug_config.h"
#include "request_priority_manager.h"
#include "request_queue.h"
#include "request_priority_test_suite.h"
#include "time_manager.h"

static const char *TAG = "SNRv9_MAIN";

// Global IO system instances
static config_manager_t config_manager;
static io_manager_t io_manager;

// PSRAM test timer handle
#if DEBUG_PSRAM_COMPREHENSIVE_TESTING
static esp_timer_handle_t psram_test_timer = NULL;
#endif

#if DEBUG_PSRAM_COMPREHENSIVE_TESTING
/**
 * @brief Progressive heap integrity check with task yielding
 * 
 * Performs heap integrity checking in stages with task yields to prevent
 * watchdog timeouts during comprehensive heap debugging.
 */
static bool progressive_heap_integrity_check(void) {
    ESP_LOGI(DEBUG_PSRAM_SAFETY_TAG, "Starting progressive heap integrity check...");
    
    // Step 1: Quick basic check of internal heap
    esp_task_wdt_reset();
    if (!heap_caps_check_integrity(MALLOC_CAP_INTERNAL, false)) {
        ESP_LOGE(DEBUG_PSRAM_SAFETY_TAG, "Internal heap basic check failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Step 2: Quick basic check of PSRAM heap
    esp_task_wdt_reset();
    if (!heap_caps_check_integrity(MALLOC_CAP_SPIRAM, false)) {
        ESP_LOGE(DEBUG_PSRAM_SAFETY_TAG, "PSRAM heap basic check failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Step 3: More comprehensive check with watchdog management
    ESP_LOGI(DEBUG_PSRAM_SAFETY_TAG, "Performing comprehensive heap check...");
    esp_task_wdt_reset();
    
    // Use non-verbose mode to reduce processing time
    bool comprehensive_result = heap_caps_check_integrity_all(false);
    
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow system recovery
    
    ESP_LOGI(DEBUG_PSRAM_SAFETY_TAG, "Progressive heap integrity check completed: %s", 
             comprehensive_result ? "PASS" : "FAIL");
    return comprehensive_result;
}

/**
 * @brief Report test progress and reset watchdog
 */
static void report_test_progress(const char* stage, int current, int total) {
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Progress: %s [%d/%d] - %d%% complete", 
             stage, current, total, (current * 100) / total);
    esp_task_wdt_reset(); // Reset watchdog during progress reporting
    vTaskDelay(pdMS_TO_TICKS(10)); // Brief yield
}

/**
 * @brief Delayed comprehensive PSRAM test execution with yielding
 * 
 * This function is called by a timer after system stabilization to run
 * the comprehensive PSRAM test suite with enhanced safety checks and
 * task yielding to prevent watchdog timeouts.
 */
static void delayed_psram_comprehensive_test(void* arg) {
    // Add current task to watchdog to prevent "task not found" errors
    esp_err_t wdt_result = esp_task_wdt_add(NULL);
    if (wdt_result != ESP_OK && wdt_result != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(DEBUG_PSRAM_TEST_TAG, "Failed to add timer task to watchdog: %s", esp_err_to_name(wdt_result));
    }

#if DEBUG_PSRAM_TEST_VERBOSE
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Starting delayed comprehensive PSRAM test");
#endif
    
#if DEBUG_PSRAM_SAFETY_CHECKS
    // Check system health before running intensive tests
    UBaseType_t main_stack_remaining = uxTaskGetStackHighWaterMark(NULL);
    size_t free_heap = esp_get_free_heap_size();
    
    ESP_LOGI(DEBUG_PSRAM_SAFETY_TAG, "Pre-test safety check:");
    ESP_LOGI(DEBUG_PSRAM_SAFETY_TAG, "  Main task stack remaining: %d bytes", main_stack_remaining);
    ESP_LOGI(DEBUG_PSRAM_SAFETY_TAG, "  Free heap: %d bytes", free_heap);
    
    if (main_stack_remaining < 1000) {
        ESP_LOGW(DEBUG_PSRAM_SAFETY_TAG, "Skipping comprehensive test - insufficient main task stack");
        goto cleanup;
    }
    
    if (free_heap < 100000) {
        ESP_LOGW(DEBUG_PSRAM_SAFETY_TAG, "Skipping comprehensive test - insufficient free heap");
        goto cleanup;
    }
    
    // Use progressive heap integrity check instead of blocking check
    if (!progressive_heap_integrity_check()) {
        ESP_LOGE(DEBUG_PSRAM_SAFETY_TAG, "Progressive heap integrity check failed before test - aborting");
        goto cleanup;
    }
    ESP_LOGI(DEBUG_PSRAM_SAFETY_TAG, "Pre-test heap integrity: PASS");
#endif
    
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "System safety checks passed - proceeding with comprehensive test");
    report_test_progress("Initialization", 1, 6);
    
    if (psram_run_comprehensive_test_suite_with_yields()) {
        ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Comprehensive PSRAM test suite: PASS");
        report_test_progress("Test Suite", 6, 6);
    } else {
        ESP_LOGE(DEBUG_PSRAM_TEST_TAG, "Comprehensive PSRAM test suite: FAIL");
    }
    
#if DEBUG_PSRAM_SAFETY_CHECKS
    // Post-test system validation using progressive check
    report_test_progress("Post-test validation", 5, 6);
    if (!progressive_heap_integrity_check()) {
        ESP_LOGE(DEBUG_PSRAM_SAFETY_TAG, "Progressive heap integrity check failed after test");
    } else {
        ESP_LOGI(DEBUG_PSRAM_SAFETY_TAG, "Post-test heap integrity: PASS");
    }
#endif
    
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Comprehensive PSRAM test completed successfully");

cleanup:
    // Remove task from watchdog before exiting
    esp_task_wdt_delete(NULL);
}
#endif

/**
 * @brief Task creation callback for system monitoring
 */
static void on_task_created(const task_info_t *task)
{
    // ESP_LOGI(TAG, "Task created: %s (Priority: %u)", task->name, (unsigned int)task->priority);
}

/**
 * @brief Task deletion callback for system monitoring
 */
static void on_task_deleted(const task_info_t *task)
{
    ESP_LOGI(TAG, "Task deleted: %s", task->name);
}

#if DEBUG_PRIORITY_TEST_SUITE
/**
 * @brief Run a simple priority validation test
 * 
 * This function tests the basic functionality of the priority system by:
 * 1. Running a short test scenario to verify priority ordering
 * 2. Checking that all processing tasks are working
 * 3. Validating queue operations
 */
static void run_priority_validation_test(void) {
    ESP_LOGI(TAG, "=== STARTING PRIORITY VALIDATION TEST ===");
    
    // Check if test suite is available
    if (!priority_test_suite_health_check()) {
        ESP_LOGW(TAG, "Priority test suite health check failed - skipping test");
        return;
    }
    
    // Run a short normal operation test (30 seconds)
    ESP_LOGI(TAG, "Running 30-second priority validation test...");
    esp_err_t result = priority_test_suite_run_scenario(TEST_SCENARIO_NORMAL_OPERATION, 30000);
    
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Priority validation test started successfully");
        
        // Wait for test to complete
        uint32_t wait_time = 0;
        while (priority_test_suite_is_running() && wait_time < 35000) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            wait_time += 1000;
            
            // Print progress every 10 seconds
            if (wait_time % 10000 == 0) {
                ESP_LOGI(TAG, "Priority test progress: %lu/30 seconds", wait_time / 1000);
                priority_test_suite_print_status();
            }
        }
        
        // Get final results
        test_result_summary_t summary;
        if (priority_test_suite_get_summary(&summary)) {
            ESP_LOGI(TAG, "=== PRIORITY VALIDATION TEST RESULTS ===");
            ESP_LOGI(TAG, "Test Completed: %s", summary.test_completed_successfully ? "SUCCESS" : "FAILED");
            ESP_LOGI(TAG, "Total Requests: Generated=%lu, Processed=%lu, Dropped=%lu", 
                     summary.total_requests_generated, summary.total_requests_processed, 
                     summary.total_requests_dropped);
            ESP_LOGI(TAG, "Average Processing Time: %.2f ms", summary.average_processing_time_ms);
            
            if (summary.total_requests_generated > 0) {
                float success_rate = (float)summary.total_requests_processed / summary.total_requests_generated * 100.0f;
                ESP_LOGI(TAG, "Success Rate: %.1f%%", success_rate);
                
                if (success_rate >= 95.0f && summary.test_completed_successfully) {
                    ESP_LOGI(TAG, "🎉 PRIORITY SYSTEM VALIDATION: PASS");
                } else {
                    ESP_LOGW(TAG, "⚠️  PRIORITY SYSTEM VALIDATION: MARGINAL (low success rate)");
                }
            } else {
                ESP_LOGW(TAG, "⚠️  PRIORITY SYSTEM VALIDATION: NO REQUESTS PROCESSED");
            }
            
            if (summary.failure_reason) {
                ESP_LOGW(TAG, "Failure Reason: %s", summary.failure_reason);
            }
        } else {
            ESP_LOGE(TAG, "Failed to get test summary");
        }
        
        // Print detailed statistics
        priority_test_suite_print_statistics();
        
    } else {
        ESP_LOGE(TAG, "Failed to start priority validation test: %s", esp_err_to_name(result));
    }
    
    ESP_LOGI(TAG, "=== PRIORITY VALIDATION TEST COMPLETE ===");
}
#endif // DEBUG_PRIORITY_TEST_SUITE


void app_main(void)
{
    ESP_LOGI(TAG, "SNRv9 Irrigation Control System Starting...");
    ESP_LOGI(TAG, "Memory monitoring system initialization");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32 Chip: %s, %d cores, WiFi%s%s, Rev %d",
             CONFIG_IDF_TARGET,
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
             chip_info.revision);
    
    // Initialize memory monitoring system
    if (!memory_monitor_init()) {
        ESP_LOGE(TAG, "Failed to initialize memory monitor");
        return;
    }
    
    // Initialize task tracking system
    if (!task_tracker_init()) {
        ESP_LOGE(TAG, "Failed to initialize task tracker");
        return;
    }
    
    // Initialize PSRAM management system
    ESP_LOGI(TAG, "Initializing PSRAM manager...");
    if (!psram_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize PSRAM manager");
        return;
    }
    
    // Extend PSRAM manager for Step 9 features
    ESP_LOGI(TAG, "Extending PSRAM manager for Step 9 features...");
    if (psram_manager_extend_for_step9() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to extend PSRAM manager for Step 9");
        return;
    }
    
    // Initialize Time Management System (Step 9 Phase 2)
    ESP_LOGI(TAG, "Initializing Time Management System...");
    if (time_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Time Management System");
        return;
    }
    
    // Initialize WiFi handler system
    ESP_LOGI(TAG, "Initializing WiFi handler...");
    if (!wifi_handler_init()) {
        ESP_LOGE(TAG, "Failed to initialize WiFi handler");
        return;
    }
    
    // Initialize authentication manager
    ESP_LOGI(TAG, "Initializing authentication manager...");
    if (!auth_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize authentication manager");
        return;
    }
    
    // Initialize web server manager
    ESP_LOGI(TAG, "Initializing web server manager...");
    if (!web_server_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize web server manager");
        return;
    }

    // Initialize storage manager
    ESP_LOGI(TAG, "Initializing storage manager...");
    if (storage_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize storage manager");
        // Continuing without storage, but functionality will be limited
    } else {
        // Test file operations
        FILE *f = fopen("/littlefs/boot_count.txt", "r+");
        if (f == NULL) {
            // File doesn't exist, create it
            f = fopen("/littlefs/boot_count.txt", "w");
            if (f != NULL) {
                fprintf(f, "1\n");
                ESP_LOGI(TAG, "Created boot_count.txt, boot count: 1");
                fclose(f);
            }
        } else {
            // File exists, increment boot count
            int count = 0;
            fscanf(f, "%d", &count);
            count++;
            rewind(f);
            fprintf(f, "%d\n", count);
            ESP_LOGI(TAG, "Incremented boot count to: %d", count);
            fclose(f);
        }
    }
    
    // Initialize configuration manager
    ESP_LOGI(TAG, "Initializing configuration manager...");
    if (config_manager_init(&config_manager, "/io_config.json") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize configuration manager");
        return;
    }
    
    // Load configuration from file
    ESP_LOGI(TAG, "Loading IO configuration...");
    if (config_manager_load(&config_manager) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load IO configuration");
        return;
    }
    
    // Initialize IO manager
    ESP_LOGI(TAG, "Initializing IO manager...");
    if (io_manager_init(&io_manager, &config_manager) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize IO manager");
        return;
    }
    
    // Start IO polling
    ESP_LOGI(TAG, "Starting IO polling task...");
    if (io_manager_start_polling(&io_manager, 1000, 2, 4096) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start IO polling task");
        return;
    }

    // Initialize IO test controller with IO manager (AFTER IO manager is fully initialized)
    ESP_LOGI(TAG, "Initializing IO test controller...");
    if (io_test_controller_init(&io_manager) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize IO test controller");
        return;
    }

#if DEBUG_REQUEST_PRIORITY
    // Initialize Request Priority Management System
    ESP_LOGI(TAG, "Initializing request priority management system...");
    
    // Create default configurations
    queue_manager_config_t queue_config = {0};
    priority_manager_config_t priority_config = {0};
    
    // Get default priority manager configuration (includes queue config)
    request_priority_get_default_config(&priority_config);
    
    // Extract queue configuration from priority config
    queue_config = priority_config.queue_config;
    
    // Initialize request queue system
    if (!request_queue_init(&queue_config)) {
        ESP_LOGE(TAG, "Failed to initialize request queue system");
        return;
    }
    
    // Initialize request priority manager
    if (!request_priority_manager_init(&priority_config)) {
        ESP_LOGE(TAG, "Failed to initialize request priority manager");
        return;
    }

#if DEBUG_PRIORITY_TEST_SUITE
    // Initialize priority test suite (conditionally)
    ESP_LOGI(TAG, "Initializing request priority test suite...");
    if (!priority_test_suite_init(NULL)) {
        ESP_LOGW(TAG, "Failed to initialize priority test suite (non-critical)");
    } else {
        ESP_LOGI(TAG, "Priority test suite initialized successfully");
    }
#endif // DEBUG_PRIORITY_TEST_SUITE

    ESP_LOGI(TAG, "Request priority management system initialized successfully");
#endif // DEBUG_REQUEST_PRIORITY
    
    // Register task lifecycle callbacks
    task_tracker_register_creation_callback(on_task_created);
    task_tracker_register_deletion_callback(on_task_deleted);
    
    // Start monitoring systems
    if (!memory_monitor_start()) {
        ESP_LOGE(TAG, "Failed to start memory monitor");
        return;
    }
    
    if (!task_tracker_start()) {
        ESP_LOGE(TAG, "Failed to start task tracker");
        return;
    }
    
    // Start WiFi handler system
    ESP_LOGI(TAG, "Starting WiFi handler...");
    if (!wifi_handler_start()) {
        ESP_LOGE(TAG, "Failed to start WiFi handler");
        return;
    }
    
    ESP_LOGI(TAG, "All systems started successfully");
    
#if DEBUG_PSRAM_QUICK_TESTING
    // Quick test runs immediately for basic validation
    ESP_LOGI(TAG, "Running quick PSRAM test...");
    if (psram_quick_test()) {
        ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Quick PSRAM test: PASS");
    } else {
        ESP_LOGW(DEBUG_PSRAM_TEST_TAG, "Quick PSRAM test: FAIL");
    }
#endif

#if DEBUG_PSRAM_COMPREHENSIVE_TESTING
    // Schedule comprehensive test after system stabilization
    ESP_LOGI(TAG, "Scheduling comprehensive PSRAM test in %d ms", DEBUG_PSRAM_TEST_DELAY_MS);
    
    // Create timer for delayed test execution
    esp_timer_create_args_t timer_args = {
        .callback = &delayed_psram_comprehensive_test,
        .name = "psram_test_timer"
    };
    esp_err_t timer_result = esp_timer_create(&timer_args, &psram_test_timer);
    if (timer_result == ESP_OK) {
        esp_timer_start_once(psram_test_timer, DEBUG_PSRAM_TEST_DELAY_MS * 1000); // Convert to microseconds
        ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Comprehensive PSRAM test scheduled successfully");
    } else {
        ESP_LOGE(DEBUG_PSRAM_TEST_TAG, "Failed to create PSRAM test timer: %s", esp_err_to_name(timer_result));
    }
#endif
    
    ESP_LOGI(TAG, "WiFi connecting to S3CURE_WIFI...");
    
    // Wait for WiFi connection before starting web server
    bool web_server_started = false;
    bool priority_test_completed = false;
    ESP_LOGI(TAG, "System ready for irrigation control implementation");
    
    // Main application loop
    uint32_t loop_counter = 0;
    while (1) {
        loop_counter++;
        
        // Start web server once WiFi is connected
        if (!web_server_started && wifi_handler_is_connected()) {
            ESP_LOGI(TAG, "WiFi connected, starting web server...");
            if (web_server_manager_start()) {
                ESP_LOGI(TAG, "Web server started successfully");
                web_server_started = true;
            } else {
                ESP_LOGE(TAG, "Failed to start web server");
            }
        }
        
#if DEBUG_PRIORITY_TEST_SUITE
        // Run priority validation test once after web server starts
        if (web_server_started && !priority_test_completed && loop_counter > 50) {
            // Wait a bit for system to stabilize after web server start
            run_priority_validation_test();
            priority_test_completed = true;
        }
#endif // DEBUG_PRIORITY_TEST_SUITE
        
        // Detailed system reports every 60 seconds
        if (loop_counter % 600 == 0) {
            ESP_LOGI(TAG, "=== SYSTEM HEALTH CHECK ===");
            memory_monitor_print_detailed_report();
            psram_manager_print_detailed_report();
            psram_manager_print_allocation_stats();
            task_tracker_print_detailed_report();
            task_tracker_print_stack_analysis();
            wifi_handler_print_detailed_report();
            if (web_server_started) {
                web_server_manager_print_status();
                auth_manager_print_status();
            }

#if DEBUG_REQUEST_PRIORITY
            // Request priority system health check
            if (request_priority_health_check()) {
                request_priority_print_status_report();
                request_queue_print_statistics();
            } else {
                ESP_LOGW(TAG, "Request priority system health check failed!");
            }

#if DEBUG_PRIORITY_TEST_SUITE
            // Priority test suite status (if running)
            if (priority_test_suite_is_running()) {
                priority_test_suite_print_status();
            }
#endif // DEBUG_PRIORITY_TEST_SUITE
#endif // DEBUG_REQUEST_PRIORITY
            
            // Check for potential memory leaks
            if (memory_monitor_check_for_leaks()) {
                ESP_LOGW(TAG, "Potential memory leak detected!");
            }
            
            // Check PSRAM health
            if (!psram_manager_health_check()) {
                ESP_LOGW(TAG, "PSRAM health check failed!");
            }
        }
        
        // Regular status updates every 20 seconds
        if (loop_counter % 200 == 0) {
            ESP_LOGI(TAG, "--- System Status ---");
            memory_monitor_force_report();
            task_tracker_print_summary();
            wifi_handler_print_summary();
        }
        
        // Check stack warnings every 5 seconds for safety
        if (loop_counter % 50 == 0) {
            task_tracker_check_stack_warnings();
        }
        
        // TODO: Add irrigation control logic here
        // - Check sensor readings
        // - Process irrigation schedules
        // - Control relays/solenoids
        // - Handle web server requests
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
