/**
 * @file request_priority_manager.h
 * @brief Request priority management system for SNRv9 Irrigation Control System
 * 
 * This module provides comprehensive request priority classification, queue management,
 * and processing task coordination with PSRAM optimization and load balancing.
 */

#ifndef REQUEST_PRIORITY_MANAGER_H
#define REQUEST_PRIORITY_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "request_queue.h"
#include "../../../include/debug_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * CONSTANTS AND CONFIGURATION
 * =============================================================================
 */

/**
 * @brief Maximum number of processing tasks
 */
#define MAX_PROCESSING_TASKS 3

/**
 * @brief Default processing task stack sizes
 */
#define CRITICAL_TASK_STACK_SIZE 4096
#define NORMAL_TASK_STACK_SIZE 8192
#define BACKGROUND_TASK_STACK_SIZE 12288

/**
 * @brief Task priorities for FreeRTOS
 */
#define CRITICAL_TASK_PRIORITY 10
#define NORMAL_TASK_PRIORITY 5
#define BACKGROUND_TASK_PRIORITY 2

/**
 * @brief Load protection thresholds
 */
#define HEAVY_OPERATION_THRESHOLD_MS 500
#define WATCHDOG_FEED_INTERVAL_MS 1000
#define MAX_PROCESSING_TIME_MS 30000

/**
 * @brief Emergency mode configuration
 */
#define EMERGENCY_MODE_TIMEOUT_MS 60000
#define LOAD_SHEDDING_THRESHOLD_PERCENT 80

/* =============================================================================
 * TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief Processing task types
 */
typedef enum {
    TASK_TYPE_CRITICAL = 0,     /**< Handles EMERGENCY and IO_CRITICAL */
    TASK_TYPE_NORMAL,           /**< Handles AUTHENTICATION and UI_CRITICAL */
    TASK_TYPE_BACKGROUND,       /**< Handles NORMAL and BACKGROUND */
    TASK_TYPE_MAX
} processing_task_type_t;

/**
 * @brief System operating modes
 */
typedef enum {
    SYSTEM_MODE_NORMAL = 0,     /**< Normal operation mode */
    SYSTEM_MODE_EMERGENCY,      /**< Emergency mode - critical requests only */
    SYSTEM_MODE_LOAD_SHEDDING,  /**< Load shedding - drop low priority requests */
    SYSTEM_MODE_MAINTENANCE     /**< Maintenance mode - limited processing */
} system_mode_t;

/**
 * @brief Load protection configuration
 */
typedef struct {
    uint32_t max_processing_time_ms;        /**< Maximum time per request type */
    uint32_t watchdog_feed_interval_ms;     /**< Watchdog feeding interval */
    bool enable_yield_on_heavy_ops;         /**< Yield CPU during heavy operations */
    uint32_t heavy_operation_threshold_ms;  /**< Threshold for heavy operations */
    bool enable_load_shedding;              /**< Enable automatic load shedding */
    uint8_t load_shedding_threshold;        /**< Load percentage for shedding */
} load_protection_config_t;

/**
 * @brief Processing task configuration
 */
typedef struct {
    processing_task_type_t task_type;       /**< Task type identifier */
    const char *task_name;                  /**< Task name for debugging */
    uint32_t stack_size;                    /**< Task stack size */
    UBaseType_t priority;                   /**< FreeRTOS task priority */
    BaseType_t core_affinity;               /**< CPU core affinity */
    bool use_psram_stack;                   /**< Use PSRAM for task stack */
    request_priority_t min_priority;        /**< Minimum priority handled */
    request_priority_t max_priority;        /**< Maximum priority handled */
    TaskHandle_t task_handle;               /**< Task handle */
} processing_task_config_t;

/**
 * @brief Priority system statistics
 */
typedef struct {
    uint32_t requests_by_priority[REQUEST_PRIORITY_MAX];    /**< Requests per priority */
    uint32_t average_processing_time[REQUEST_PRIORITY_MAX]; /**< Average processing time */
    uint32_t queue_depth[REQUEST_PRIORITY_MAX];             /**< Current queue depths */
    uint32_t dropped_requests;                              /**< Total dropped requests */
    uint32_t timeout_requests;                              /**< Total timeout requests */
    uint32_t emergency_mode_activations;                    /**< Emergency mode count */
    uint32_t load_shedding_activations;                     /**< Load shedding count */
    uint32_t total_requests_processed;                      /**< Total processed */
    uint32_t system_uptime_ms;                              /**< System uptime */
    system_mode_t current_mode;                             /**< Current system mode */
    float cpu_utilization_percent;                          /**< CPU utilization */
    uint32_t last_update_time;                              /**< Last stats update */
} priority_stats_t;

/**
 * @brief Request classification result
 */
typedef struct {
    request_priority_t priority;            /**< Classified priority */
    uint32_t estimated_processing_time_ms;  /**< Estimated processing time */
    bool requires_authentication;           /**< Authentication required */
    bool is_emergency_request;              /**< Emergency request flag */
    const char *classification_reason;      /**< Reason for classification */
} classification_result_t;

/**
 * @brief Priority manager configuration
 */
typedef struct {
    queue_manager_config_t queue_config;           /**< Queue configuration */
    load_protection_config_t load_config;          /**< Load protection config */
    processing_task_config_t task_configs[TASK_TYPE_MAX]; /**< Task configurations */
    bool enable_emergency_mode;                     /**< Enable emergency mode */
    bool enable_load_balancing;                     /**< Enable load balancing */
    bool enable_statistics;                         /**< Enable statistics */
    uint32_t statistics_report_interval_ms;        /**< Stats report interval */
    uint32_t health_check_interval_ms;              /**< Health check interval */
} priority_manager_config_t;

/* =============================================================================
 * DEBUG MACROS
 * =============================================================================
 */

#if DEBUG_REQUEST_PRIORITY && DEBUG_INCLUDE_TIMESTAMPS
#define PRIORITY_DEBUG_LOG(tag, format, ...) \
    ESP_LOGI(tag, "[%llu] " format, esp_timer_get_time()/1000, ##__VA_ARGS__)
#elif DEBUG_REQUEST_PRIORITY
#define PRIORITY_DEBUG_LOG(tag, format, ...) \
    ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define PRIORITY_DEBUG_LOG(tag, format, ...) do {} while(0)
#endif

#if DEBUG_REQUEST_CLASSIFICATION
#define CLASSIFICATION_DEBUG(format, ...) \
    ESP_LOGD(DEBUG_CLASSIFICATION_TAG, format, ##__VA_ARGS__)
#else
#define CLASSIFICATION_DEBUG(format, ...) do {} while(0)
#endif

#if DEBUG_LOAD_BALANCING
#define LOAD_BALANCE_DEBUG(format, ...) \
    ESP_LOGD(DEBUG_LOAD_BALANCE_TAG, format, ##__VA_ARGS__)
#else
#define LOAD_BALANCE_DEBUG(format, ...) do {} while(0)
#endif

#if DEBUG_EMERGENCY_MODE
#define EMERGENCY_MODE_DEBUG(entering) \
    do { \
        if (entering) { \
            ESP_LOGW(DEBUG_EMERGENCY_TAG, "ENTERING EMERGENCY MODE - Only critical requests will be processed"); \
        } else { \
            ESP_LOGI(DEBUG_EMERGENCY_TAG, "EXITING EMERGENCY MODE - Normal request processing resumed"); \
        } \
    } while(0)
#else
#define EMERGENCY_MODE_DEBUG(entering) do {} while(0)
#endif

#if DEBUG_REQUEST_TIMING
typedef struct {
    uint32_t request_count;
    uint32_t total_processing_time;
    uint32_t min_processing_time;
    uint32_t max_processing_time;
    uint32_t slow_request_count;
    uint32_t timeout_count;
} priority_debug_stats_t;

extern priority_debug_stats_t debug_stats[REQUEST_PRIORITY_MAX];

#define UPDATE_TIMING_STATS(priority, time_ms) \
    do { \
        debug_stats[priority].request_count++; \
        debug_stats[priority].total_processing_time += time_ms; \
        if (time_ms < debug_stats[priority].min_processing_time || \
            debug_stats[priority].min_processing_time == 0) { \
            debug_stats[priority].min_processing_time = time_ms; \
        } \
        if (time_ms > debug_stats[priority].max_processing_time) { \
            debug_stats[priority].max_processing_time = time_ms; \
        } \
        if (time_ms > DEBUG_SLOW_REQUEST_THRESHOLD_MS) { \
            debug_stats[priority].slow_request_count++; \
        } \
    } while(0)
#else
#define UPDATE_TIMING_STATS(priority, time_ms) do {} while(0)
#endif

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the request priority management system
 * 
 * Sets up all priority queues, processing tasks, and monitoring systems.
 * 
 * @param config Priority manager configuration
 * @return true if initialization successful, false otherwise
 */
bool request_priority_manager_init(const priority_manager_config_t *config);

/**
 * @brief Cleanup and deinitialize the priority management system
 * 
 * Stops all tasks, frees memory, and cleans up resources.
 */
void request_priority_manager_cleanup(void);

/**
 * @brief Classify an incoming HTTP request
 * 
 * Analyzes the request URI, method, and headers to determine priority.
 * 
 * @param req HTTP request to classify
 * @param result Pointer to structure to receive classification result
 * @return true if classification successful, false otherwise
 */
bool request_priority_classify(httpd_req_t *req, classification_result_t *result);

/**
 * @brief Queue a request for priority processing
 * 
 * Creates request context and adds to appropriate priority queue.
 * 
 * @param req HTTP request to queue
 * @param priority Request priority level
 * @return ESP_OK if successful, error code otherwise
 */
esp_err_t request_priority_queue_request(httpd_req_t *req, request_priority_t priority);

/**
 * @brief Process queued requests (main processing loop)
 * 
 * Continuously processes requests from priority queues.
 * This function is called by processing tasks.
 * 
 * @param task_type Type of processing task
 */
void request_priority_process_queues(processing_task_type_t task_type);

/**
 * @brief Get priority system statistics
 * 
 * @param stats Pointer to structure to receive statistics
 * @return true if stats retrieved successfully, false otherwise
 */
bool request_priority_get_stats(priority_stats_t *stats);

/**
 * @brief Set system operating mode
 * 
 * Changes the system operating mode (normal, emergency, etc.).
 * 
 * @param mode New system mode
 * @return true if mode change successful, false otherwise
 */
bool request_priority_set_system_mode(system_mode_t mode);

/**
 * @brief Get current system operating mode
 * 
 * @return Current system mode
 */
system_mode_t request_priority_get_system_mode(void);

/**
 * @brief Enter emergency mode
 * 
 * Switches to emergency mode where only critical requests are processed.
 * 
 * @param timeout_ms Time to stay in emergency mode (0 = indefinite)
 * @return true if emergency mode activated, false otherwise
 */
bool request_priority_enter_emergency_mode(uint32_t timeout_ms);

/**
 * @brief Exit emergency mode
 * 
 * Returns to normal operation mode.
 * 
 * @return true if emergency mode exited, false otherwise
 */
bool request_priority_exit_emergency_mode(void);

/**
 * @brief Enable/disable load shedding
 * 
 * Controls automatic dropping of low-priority requests under high load.
 * 
 * @param enable true to enable load shedding, false to disable
 */
void request_priority_enable_load_shedding(bool enable);

/**
 * @brief Adjust priority based on system load
 * 
 * Dynamically adjusts request priority based on current system load.
 * 
 * @param base_priority Original request priority
 * @return Adjusted priority level
 */
request_priority_t request_priority_adjust_for_load(request_priority_t base_priority);

/**
 * @brief Check if system is under high load
 * 
 * @return true if system load is high, false otherwise
 */
bool request_priority_is_high_load(void);

/**
 * @brief Get system load percentage
 * 
 * @return Current system load as percentage (0-100)
 */
uint8_t request_priority_get_load_percentage(void);

/**
 * @brief Force processing of all queued requests
 * 
 * Processes all pending requests regardless of priority.
 * Used during system shutdown or maintenance.
 * 
 * @param timeout_ms Maximum time to spend processing
 * @return Number of requests processed
 */
uint32_t request_priority_flush_all_queues(uint32_t timeout_ms);

/**
 * @brief Get default priority manager configuration
 * 
 * Returns a default configuration suitable for most applications.
 * 
 * @param config Pointer to configuration structure to fill
 */
void request_priority_get_default_config(priority_manager_config_t *config);

/* =============================================================================
 * MONITORING AND DEBUG FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Print comprehensive priority system status
 * 
 * Outputs detailed information about queues, tasks, and performance.
 */
void request_priority_print_status_report(void);

/**
 * @brief Print priority system statistics
 * 
 * Shows performance metrics and utilization statistics.
 */
void request_priority_print_statistics(void);

/**
 * @brief Reset all priority system statistics
 * 
 * Clears all statistical counters for fresh monitoring.
 */
void request_priority_reset_statistics(void);

/**
 * @brief Enable/disable priority system monitoring
 * 
 * Controls whether statistics and monitoring are active.
 * 
 * @param enable true to enable monitoring, false to disable
 */
void request_priority_set_monitoring_enabled(bool enable);

/**
 * @brief Perform priority system health check
 * 
 * Checks the health of all queues, tasks, and system components.
 * 
 * @return true if system is healthy, false if issues detected
 */
bool request_priority_health_check(void);

/**
 * @brief Get processing task information
 * 
 * Returns information about a specific processing task.
 * 
 * @param task_type Type of task to query
 * @param task_handle Pointer to receive task handle
 * @param stack_high_water_mark Pointer to receive stack usage
 * @return true if task info retrieved, false otherwise
 */
bool request_priority_get_task_info(processing_task_type_t task_type,
                                    TaskHandle_t *task_handle,
                                    uint32_t *stack_high_water_mark);

/**
 * @brief Convert system mode to string
 * 
 * @param mode System mode
 * @return String representation of mode
 */
const char* request_priority_mode_to_string(system_mode_t mode);

/**
 * @brief Convert task type to string
 * 
 * @param task_type Task type
 * @return String representation of task type
 */
const char* request_priority_task_type_to_string(processing_task_type_t task_type);

/* =============================================================================
 * ADVANCED FEATURES
 * =============================================================================
 */

/**
 * @brief Register custom request classifier
 * 
 * Allows registration of custom classification logic for specific URIs.
 * 
 * @param uri_pattern URI pattern to match
 * @param classifier_func Custom classifier function
 * @return true if registration successful, false otherwise
 */
bool request_priority_register_custom_classifier(const char *uri_pattern,
                                                 bool (*classifier_func)(httpd_req_t *, classification_result_t *));

/**
 * @brief Set priority override for specific URI
 * 
 * Forces a specific priority for requests matching the URI pattern.
 * 
 * @param uri_pattern URI pattern to match
 * @param priority Priority to assign
 * @return true if override set successfully, false otherwise
 */
bool request_priority_set_uri_override(const char *uri_pattern, request_priority_t priority);

/**
 * @brief Remove priority override for URI
 * 
 * Removes a previously set priority override.
 * 
 * @param uri_pattern URI pattern to remove override for
 * @return true if override removed, false otherwise
 */
bool request_priority_remove_uri_override(const char *uri_pattern);

#ifdef __cplusplus
}
#endif

#endif /* REQUEST_PRIORITY_MANAGER_H */
