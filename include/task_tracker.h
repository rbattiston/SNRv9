/**
 * @file task_tracker.h
 * @brief Task tracking and monitoring interface for SNRv9 Irrigation Control System
 * 
 * This module provides comprehensive FreeRTOS task monitoring capabilities including
 * task lifecycle tracking, stack usage monitoring, and performance analysis.
 */

#ifndef TASK_TRACKER_H
#define TASK_TRACKER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "debug_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief Task state enumeration (mirrors FreeRTOS eTaskState)
 */
typedef enum {
    TASK_STATE_RUNNING = 0,    /**< Task is currently running */
    TASK_STATE_READY,          /**< Task is ready to run */
    TASK_STATE_BLOCKED,        /**< Task is blocked */
    TASK_STATE_SUSPENDED,      /**< Task is suspended */
    TASK_STATE_DELETED,        /**< Task has been deleted */
    TASK_STATE_INVALID         /**< Invalid/unknown state */
} task_state_t;

/**
 * @brief Task information structure
 */
typedef struct {
    TaskHandle_t handle;              /**< FreeRTOS task handle */
    char name[configMAX_TASK_NAME_LEN]; /**< Task name */
    uint32_t stack_size;              /**< Total stack size in bytes */
    uint32_t stack_high_water_mark;   /**< Minimum free stack ever (bytes) */
    uint32_t stack_used;              /**< Current stack usage (bytes) */
    UBaseType_t priority;             /**< Task priority */
    task_state_t state;               /**< Current task state */
    uint32_t runtime_counter;         /**< Total runtime in ticks */
    uint32_t creation_time;           /**< Time when task was created */
    bool is_valid;                    /**< True if this entry contains valid data */
} task_info_t;

/**
 * @brief Task tracking statistics
 */
typedef struct {
    uint16_t total_tasks;             /**< Total number of tasks tracked */
    uint16_t active_tasks;            /**< Number of currently active tasks */
    uint16_t max_tasks_seen;          /**< Maximum tasks seen simultaneously */
    uint32_t total_stack_allocated;   /**< Total stack memory allocated */
    uint32_t total_stack_used;        /**< Total stack memory currently used */
    uint32_t worst_stack_usage_pct;   /**< Worst stack usage percentage seen */
    char worst_stack_task[configMAX_TASK_NAME_LEN]; /**< Name of task with worst stack usage */
} task_tracking_stats_t;

/**
 * @brief Task tracker status
 */
typedef enum {
    TASK_TRACKER_STOPPED = 0,
    TASK_TRACKER_RUNNING,
    TASK_TRACKER_ERROR
} task_tracker_status_t;

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the task tracking system
 * 
 * Sets up the task tracking system and initializes all data structures.
 * Must be called before any other task tracker functions.
 * 
 * @return true if initialization successful, false otherwise
 */
bool task_tracker_init(void);

/**
 * @brief Start the task tracking system
 * 
 * Begins periodic task monitoring and reporting based on configuration.
 * 
 * @return true if started successfully, false otherwise
 */
bool task_tracker_start(void);

/**
 * @brief Stop the task tracking system
 * 
 * Stops all tracking activities but preserves collected data.
 * 
 * @return true if stopped successfully, false otherwise
 */
bool task_tracker_stop(void);

/**
 * @brief Get current task tracking status
 * 
 * @return Current status of the task tracker
 */
task_tracker_status_t task_tracker_get_status(void);

/**
 * @brief Update task information
 * 
 * Scans all current tasks and updates the tracking database.
 * Called automatically by the tracking task, but can be called manually.
 */
void task_tracker_update(void);

/**
 * @brief Get information for a specific task
 * 
 * @param task_name Name of the task to get information for
 * @param info Pointer to structure to receive task information
 * @return true if task found and info retrieved, false otherwise
 */
bool task_tracker_get_task_info(const char *task_name, task_info_t *info);

/**
 * @brief Get information for all tracked tasks
 * 
 * @param task_list Array to receive task information
 * @param max_tasks Maximum number of tasks that can fit in the array
 * @param num_tasks Pointer to receive actual number of tasks returned
 * @return true if successful, false otherwise
 */
bool task_tracker_get_all_tasks(task_info_t *task_list, uint16_t max_tasks, uint16_t *num_tasks);

/**
 * @brief Get task tracking statistics
 * 
 * @param stats Pointer to structure to receive statistics
 * @return true if stats retrieved successfully, false otherwise
 */
bool task_tracker_get_stats(task_tracking_stats_t *stats);

/**
 * @brief Force immediate task report to serial output
 * 
 * Triggers an immediate task report regardless of the configured
 * reporting interval.
 */
void task_tracker_force_report(void);

/**
 * @brief Calculate stack usage percentage for a task
 * 
 * @param info Pointer to task information
 * @return Stack usage as percentage (0-100)
 */
uint8_t task_tracker_calc_stack_usage_pct(const task_info_t *info);

/**
 * @brief Find task with highest stack usage
 * 
 * @param info Pointer to structure to receive task information
 * @return true if task found, false if no tasks tracked
 */
bool task_tracker_find_highest_stack_usage(task_info_t *info);

/**
 * @brief Find task with lowest remaining stack
 * 
 * @param info Pointer to structure to receive task information
 * @return true if task found, false if no tasks tracked
 */
bool task_tracker_find_lowest_remaining_stack(task_info_t *info);

/**
 * @brief Check for stack overflow conditions
 * 
 * Analyzes all tracked tasks for potential stack overflow conditions.
 * 
 * @param threshold_pct Stack usage percentage threshold (0-100)
 * @return Number of tasks exceeding the threshold
 */
uint16_t task_tracker_check_stack_overflow(uint8_t threshold_pct);

/**
 * @brief Reset task tracking statistics
 * 
 * Clears all accumulated statistics but preserves current task data.
 */
void task_tracker_reset_stats(void);

/**
 * @brief Enable/disable task tracking at runtime
 * 
 * Allows dynamic control of task tracking without recompilation.
 * 
 * @param enable true to enable tracking, false to disable
 */
void task_tracker_set_enabled(bool enable);

/**
 * @brief Check if task tracking is enabled
 * 
 * @return true if tracking is enabled, false otherwise
 */
bool task_tracker_is_enabled(void);

/* =============================================================================
 * DIAGNOSTIC FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Print detailed task report to console
 * 
 * Outputs comprehensive task information including stack usage,
 * priorities, and states for all tracked tasks.
 */
void task_tracker_print_detailed_report(void);

/**
 * @brief Print task summary to console
 * 
 * Outputs a concise summary of task tracking statistics.
 */
void task_tracker_print_summary(void);

/**
 * @brief Check for stack usage warnings
 * 
 * Checks all tasks for stack usage warnings and outputs alerts
 * for tasks approaching stack limits.
 */
void task_tracker_check_stack_warnings(void);

/**
 * @brief Print stack usage analysis to console
 * 
 * Outputs detailed stack usage analysis including warnings
 * for tasks approaching stack limits.
 */
void task_tracker_print_stack_analysis(void);

/**
 * @brief Register task creation callback
 * 
 * Registers a callback function to be called when a new task is detected.
 * Useful for logging task lifecycle events.
 * 
 * @param callback Function to call when task is created (can be NULL)
 */
void task_tracker_register_creation_callback(void (*callback)(const task_info_t *task));

/**
 * @brief Register task deletion callback
 * 
 * Registers a callback function to be called when a task is deleted.
 * 
 * @param callback Function to call when task is deleted (can be NULL)
 */
void task_tracker_register_deletion_callback(void (*callback)(const task_info_t *task));

#ifdef __cplusplus
}
#endif

#endif /* TASK_TRACKER_H */
