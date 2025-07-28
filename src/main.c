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
    
    ESP_LOGI(TAG, "Memory monitoring system started successfully");
    ESP_LOGI(TAG, "System ready for irrigation control implementation");
    
    // Main application loop
    uint32_t loop_counter = 0;
    while (1) {
        loop_counter++;
        
        // Periodic system health checks (every 5 minutes)
        if (loop_counter % 3000 == 0) {
            ESP_LOGI(TAG, "=== SYSTEM HEALTH CHECK ===");
            memory_monitor_print_detailed_report();
            task_tracker_print_detailed_report();
            
            // Check for potential memory leaks
            if (memory_monitor_check_for_leaks()) {
                ESP_LOGW(TAG, "Potential memory leak detected!");
            }
        }
        
        // Check stack warnings every 30 seconds for safety
        if (loop_counter % 300 == 0) {
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
