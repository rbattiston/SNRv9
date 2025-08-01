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

static const char *TAG = "SNRv9_MAIN";

// Global IO system instances
static config_manager_t config_manager;
static io_manager_t io_manager;

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

/**
 * @brief Comprehensive PSRAM functionality test
 * 
 * This function performs a thorough test of PSRAM capabilities including:
 * - PSRAM detection and availability
 * - Allocation strategy testing
 * - Task creation with PSRAM stacks
 * - Memory usage monitoring
 * - Performance validation
 */
static void run_psram_comprehensive_test(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    PSRAM COMPREHENSIVE TEST SUITE");
    ESP_LOGI(TAG, "========================================");
    
    // Phase 1: Basic PSRAM Detection
    ESP_LOGI(TAG, "Phase 1: PSRAM Detection and Availability");
    ESP_LOGI(TAG, "----------------------------------------");
    
    if (psram_manager_is_available()) {
        ESP_LOGI(TAG, "✓ PSRAM is available and functional");
        
        psram_info_t info;
        if (psram_manager_get_info(&info)) {
            ESP_LOGI(TAG, "✓ PSRAM Total Size: %u KB", 
                     (unsigned int)(info.psram_total_size / 1024));
            ESP_LOGI(TAG, "✓ PSRAM Free Size: %u KB", 
                     (unsigned int)(info.psram_free_size / 1024));
            ESP_LOGI(TAG, "✓ PSRAM Largest Block: %u KB", 
                     (unsigned int)(info.psram_largest_block / 1024));
        }
    } else {
        ESP_LOGW(TAG, "⚠ PSRAM not available - tests will use internal RAM");
    }
    
    // Phase 2: Allocation Strategy Testing
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Phase 2: Allocation Strategy Testing");
    ESP_LOGI(TAG, "------------------------------------");
    psram_demonstrate_allocation_strategies();
    
    // Phase 3: Memory Statistics Before Task Creation
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Phase 3: Memory Statistics (Before Task Creation)");
    ESP_LOGI(TAG, "------------------------------------------------");
    psram_show_usage_example();
    
    // Phase 4: Task Creation Testing
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Phase 4: PSRAM Task Creation Testing");
    ESP_LOGI(TAG, "------------------------------------");
    
    // Test critical task creation (should use internal RAM)
    ESP_LOGI(TAG, "Creating critical task (internal RAM)...");
    if (psram_create_critical_task_example()) {
        ESP_LOGI(TAG, "✓ Critical task created successfully");
    } else {
        ESP_LOGE(TAG, "✗ Critical task creation failed");
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000)); // Let task run and complete
    
    // Test data processing task (should use PSRAM if available)
    ESP_LOGI(TAG, "Creating data processing task (PSRAM preferred)...");
    if (psram_create_data_processing_task_example()) {
        ESP_LOGI(TAG, "✓ Data processing task created successfully");
    } else {
        ESP_LOGE(TAG, "✗ Data processing task creation failed");
    }
    
    vTaskDelay(pdMS_TO_TICKS(3000)); // Let task run and complete
    
    // Test web server task (should use PSRAM if available)
    ESP_LOGI(TAG, "Creating web server task (PSRAM preferred)...");
    if (psram_create_web_server_task_example()) {
        ESP_LOGI(TAG, "✓ Web server task created successfully");
    } else {
        ESP_LOGE(TAG, "✗ Web server task creation failed");
    }
    
    vTaskDelay(pdMS_TO_TICKS(3000)); // Let task run and complete
    
    // Phase 5: Memory Statistics After Task Creation
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Phase 5: Memory Statistics (After Task Creation)");
    ESP_LOGI(TAG, "-----------------------------------------------");
    psram_show_usage_example();
    
    // Phase 6: PSRAM Health Check
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Phase 6: PSRAM Health Check");
    ESP_LOGI(TAG, "---------------------------");
    if (psram_manager_health_check()) {
        ESP_LOGI(TAG, "✓ PSRAM health check passed");
    } else {
        ESP_LOGW(TAG, "⚠ PSRAM health check failed");
    }
    
    // Phase 7: Allocation Statistics
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Phase 7: Final Allocation Statistics");
    ESP_LOGI(TAG, "-----------------------------------");
    psram_manager_print_allocation_stats();
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    PSRAM TEST SUITE COMPLETED");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
}

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

    // Initialize IO test controller with IO manager
    ESP_LOGI(TAG, "Initializing IO test controller...");
    if (io_test_controller_init(&io_manager) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize IO test controller");
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
    
    // Run comprehensive PSRAM test suite
    ESP_LOGI(TAG, "Running PSRAM comprehensive test suite...");
    run_psram_comprehensive_test();
    
    ESP_LOGI(TAG, "WiFi connecting to S3CURE_WIFI...");
    
    // Wait for WiFi connection before starting web server
    bool web_server_started = false;
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
