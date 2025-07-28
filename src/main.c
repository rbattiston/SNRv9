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
 * @brief Demo task to show memory monitoring in action
 */
static void demo_task(void *pvParameters)
{
    const char *task_name = (const char *)pvParameters;
    uint32_t counter = 0;
    
    ESP_LOGI(TAG, "Demo task '%s' started", task_name);
    
    while (1) {
        // Simulate some work
        counter++;
        
        // Allocate and free some memory to show memory changes
        if (counter % 10 == 0) {
            void *temp_mem = malloc(1024);
            if (temp_mem) {
                // Use the memory briefly
                memset(temp_mem, 0xAA, 1024);
                vTaskDelay(pdMS_TO_TICKS(100));
                free(temp_mem);
            }
        }
        
        // Print a message every 30 seconds
        if (counter % 300 == 0) {
            ESP_LOGI(TAG, "Demo task '%s' counter: %u", task_name, (unsigned int)counter);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Task creation callback for demonstration
 */
static void on_task_created(const task_info_t *task)
{
    ESP_LOGI(TAG, "Task created: %s (Priority: %u)", 
             task->name, (unsigned int)task->priority);
}

/**
 * @brief Task deletion callback for demonstration
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
    
    // Create some demo tasks to show the monitoring in action
    // Using adequate stack sizes to prevent overflow (minimum 2048 bytes for ESP32)
    xTaskCreate(demo_task, "demo_task_1", 2048, "Task1", 2, NULL);
    xTaskCreate(demo_task, "demo_task_2", 2048, "Task2", 3, NULL);
    xTaskCreate(demo_task, "demo_task_3", 2048, "Task3", 1, NULL);
    
    ESP_LOGI(TAG, "Demo tasks created - monitoring system will track them");
    
    // Main application loop
    uint32_t loop_counter = 0;
    while (1) {
        loop_counter++;
        
        // Print detailed reports every 60 seconds
        if (loop_counter % 600 == 0) {
            ESP_LOGI(TAG, "=== PERIODIC SYSTEM REPORT ===");
            memory_monitor_print_detailed_report();
            task_tracker_print_detailed_report();
            task_tracker_print_stack_analysis();
            
            // Check for potential memory leaks
            if (memory_monitor_check_for_leaks()) {
                ESP_LOGW(TAG, "Potential memory leak detected!");
            }
        }
        
        // Force immediate reports every 20 seconds for demonstration
        if (loop_counter % 200 == 0) {
            ESP_LOGI(TAG, "--- Quick Status Check ---");
            memory_monitor_force_report();
            task_tracker_print_summary();
            
            // Check for stack warnings more frequently
            task_tracker_check_stack_warnings();
        }
        
        // Check stack warnings every 5 seconds for early detection
        if (loop_counter % 50 == 0) {
            task_tracker_check_stack_warnings();
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
