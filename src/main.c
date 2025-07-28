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
#include "wifi_handler.h"
#include "web_server_manager.h"
#include "debug_config.h"

static const char *TAG = "SNRv9_MAIN";

/**
 * @brief Task creation callback for system monitoring
 */
static void on_task_created(const task_info_t *task)
{
    ESP_LOGI(TAG, "Task created: %s (Priority: %u)", 
             task->name, (unsigned int)task->priority);
}

/**
 * @brief Task deletion callback for system monitoring
 */
static void on_task_deleted(const task_info_t *task)
{
    ESP_LOGI(TAG, "Task deleted: %s", task->name);
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
    
    // Initialize WiFi handler system
    ESP_LOGI(TAG, "Initializing WiFi handler...");
    if (!wifi_handler_init()) {
        ESP_LOGE(TAG, "Failed to initialize WiFi handler");
        return;
    }
    
    // Initialize web server manager
    ESP_LOGI(TAG, "Initializing web server manager...");
    if (!web_server_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize web server manager");
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
            task_tracker_print_detailed_report();
            task_tracker_print_stack_analysis();
            wifi_handler_print_detailed_report();
            if (web_server_started) {
                web_server_manager_print_status();
            }
            
            // Check for potential memory leaks
            if (memory_monitor_check_for_leaks()) {
                ESP_LOGW(TAG, "Potential memory leak detected!");
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
