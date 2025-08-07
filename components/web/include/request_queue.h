/**
 * @file request_queue.h
 * @brief Request queue management for SNRv9 priority system
 * 
 * This module provides priority-based request queuing with PSRAM optimization
 * and comprehensive monitoring capabilities.
 */

#ifndef REQUEST_QUEUE_H
#define REQUEST_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_server.h"
#include "../../../include/debug_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * CONSTANTS AND CONFIGURATION
 * =============================================================================
 */

/**
 * @brief Maximum number of requests per priority queue
 * Large capacity enabled by PSRAM allocation
 */
#define MAX_QUEUED_REQUESTS_PER_PRIORITY 200

/**
 * @brief Default request timeout in milliseconds
 */
#define DEFAULT_REQUEST_TIMEOUT_MS 30000

/**
 * @brief Emergency mode timeout (shorter for critical operations)
 */
#define EMERGENCY_REQUEST_TIMEOUT_MS 5000

/**
 * @brief Maximum request buffer size for large operations
 */
#define MAX_REQUEST_BUFFER_SIZE 16384

/* =============================================================================
 * TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief Request priority levels (highest to lowest)
 */
typedef enum {
    REQUEST_PRIORITY_EMERGENCY = 0,    /**< Emergency stop, safety shutdowns */
    REQUEST_PRIORITY_IO_CRITICAL,      /**< Real-time IO operations */
    REQUEST_PRIORITY_AUTHENTICATION,   /**< Login/logout operations */
    REQUEST_PRIORITY_UI_CRITICAL,      /**< Dashboard updates, status checks */
    REQUEST_PRIORITY_NORMAL,           /**< Standard web requests */
    REQUEST_PRIORITY_BACKGROUND,       /**< Logging, statistics, file uploads */
    REQUEST_PRIORITY_MAX               /**< Number of priority levels */
} request_priority_t;

/**
 * @brief Request context for processing
 */
typedef struct {
    httpd_req_t *request;           /**< Original HTTP request - internal RAM */
    request_priority_t priority;    /**< Request priority - internal RAM */
    uint32_t timestamp;             /**< Enqueue timestamp - internal RAM */
    uint32_t timeout_ms;            /**< Request timeout - internal RAM */
    
    // Large buffers allocated in PSRAM
    char *request_buffer;           /**< Large HTTP request data - PSRAM */
    char *response_buffer;          /**< Large HTTP response data - PSRAM */
    size_t buffer_size;             /**< Size of allocated buffers */
    void *processing_context;       /**< Complex processing data - PSRAM */
    
    // Request identification
    char request_id[16];            /**< Unique request identifier */
    bool is_processed;              /**< Processing completion flag */
    uint32_t processing_start_time; /**< Processing start timestamp */
} request_context_t;

/**
 * @brief Queued request structure
 */
typedef struct {
    request_context_t *context;     /**< Request context */
    uint32_t enqueue_time;          /**< Time when request was queued */
    bool is_valid;                  /**< Validity flag for queue management */
} queued_request_t;

/**
 * @brief Priority queue structure with PSRAM optimization
 */
typedef struct {
    // Metadata in internal RAM for fast access
    volatile uint16_t head;         /**< Queue head index */
    volatile uint16_t tail;         /**< Queue tail index */
    volatile uint16_t count;        /**< Current queue depth */
    uint16_t max_capacity;          /**< Maximum queue capacity */
    SemaphoreHandle_t mutex;        /**< Thread safety mutex */
    SemaphoreHandle_t semaphore;    /**< Queue availability semaphore */
    
    // Large data structures in PSRAM
    queued_request_t *requests;     /**< Request array - PSRAM allocated */
    
    // Statistics and monitoring
    uint32_t total_enqueued;        /**< Total requests enqueued */
    uint32_t total_dequeued;        /**< Total requests dequeued */
    uint32_t total_timeouts;        /**< Total request timeouts */
    uint32_t peak_depth;            /**< Peak queue depth */
    uint32_t last_activity_time;    /**< Last queue activity timestamp */
} priority_queue_t;

/**
 * @brief Queue statistics structure
 */
typedef struct {
    uint32_t current_depth;         /**< Current queue depth */
    uint32_t max_capacity;          /**< Maximum queue capacity */
    uint32_t total_enqueued;        /**< Total requests enqueued */
    uint32_t total_dequeued;        /**< Total requests dequeued */
    uint32_t total_timeouts;        /**< Total request timeouts */
    uint32_t peak_depth;            /**< Peak queue depth */
    uint32_t average_wait_time_ms;  /**< Average wait time in queue */
    uint32_t last_activity_time;    /**< Last queue activity */
    float utilization_percent;      /**< Queue utilization percentage */
} queue_stats_t;

/**
 * @brief Queue manager configuration
 */
typedef struct {
    uint16_t queue_capacity[REQUEST_PRIORITY_MAX];  /**< Capacity per priority */
    uint32_t default_timeout_ms;                    /**< Default request timeout */
    uint32_t emergency_timeout_ms;                  /**< Emergency timeout */
    bool enable_psram_allocation;                   /**< Use PSRAM for queues */
    bool enable_statistics;                         /**< Enable statistics collection */
    uint32_t cleanup_interval_ms;                   /**< Queue cleanup interval */
} queue_manager_config_t;

/* =============================================================================
 * DEBUG MACROS
 * =============================================================================
 */

#if DEBUG_REQUEST_PRIORITY && DEBUG_INCLUDE_TIMESTAMPS
#define QUEUE_DEBUG_LOG(tag, format, ...) \
    ESP_LOGI(tag, "[%llu] " format, esp_timer_get_time()/1000, ##__VA_ARGS__)
#elif DEBUG_REQUEST_PRIORITY
#define QUEUE_DEBUG_LOG(tag, format, ...) \
    ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define QUEUE_DEBUG_LOG(tag, format, ...) do {} while(0)
#endif

#if DEBUG_QUEUE_MANAGEMENT
#define QUEUE_DEBUG(format, ...) \
    ESP_LOGD(DEBUG_QUEUE_TAG, format, ##__VA_ARGS__)
#else
#define QUEUE_DEBUG(format, ...) do {} while(0)
#endif

#if DEBUG_PRIORITY_PSRAM
#define TRACK_PSRAM_ALLOC(component, size, ptr) \
    do { \
        if (psram_is_psram_ptr(ptr)) { \
            ESP_LOGD(DEBUG_PRIORITY_MANAGER_TAG, "PSRAM alloc: %s = %zu bytes at %p", \
                     component, size, ptr); \
        } else { \
            ESP_LOGD(DEBUG_PRIORITY_MANAGER_TAG, "Internal RAM alloc: %s = %zu bytes at %p", \
                     component, size, ptr); \
        } \
    } while(0)
#else
#define TRACK_PSRAM_ALLOC(component, size, ptr) do {} while(0)
#endif

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the request queue system
 * 
 * Sets up priority queues with PSRAM allocation and initializes
 * all necessary synchronization primitives.
 * 
 * @param config Queue manager configuration
 * @return true if initialization successful, false otherwise
 */
bool request_queue_init(const queue_manager_config_t *config);

/**
 * @brief Cleanup and deinitialize the request queue system
 * 
 * Frees all allocated memory and destroys synchronization primitives.
 */
void request_queue_cleanup(void);

/**
 * @brief Create a new request context
 * 
 * Allocates request context with PSRAM buffers for large data.
 * 
 * @param req HTTP request pointer
 * @param priority Request priority level
 * @param buffer_size Size of request/response buffers
 * @return Pointer to request context, or NULL if allocation failed
 */
request_context_t* request_queue_create_context(httpd_req_t *req, 
                                                request_priority_t priority,
                                                size_t buffer_size);

/**
 * @brief Free a request context
 * 
 * Properly frees all allocated memory including PSRAM buffers.
 * 
 * @param context Request context to free
 */
void request_queue_free_context(request_context_t *context);

/**
 * @brief Enqueue a request for processing
 * 
 * Adds request to appropriate priority queue with timeout handling.
 * 
 * @param context Request context to enqueue
 * @return ESP_OK if successful, error code otherwise
 */
esp_err_t request_queue_enqueue(request_context_t *context);

/**
 * @brief Dequeue the highest priority request
 * 
 * Removes and returns the highest priority request available.
 * 
 * @param timeout_ms Maximum time to wait for a request
 * @return Pointer to request context, or NULL if timeout/error
 */
request_context_t* request_queue_dequeue(uint32_t timeout_ms);

/**
 * @brief Dequeue a request from specific priority level
 * 
 * Removes and returns a request from the specified priority queue.
 * 
 * @param priority Priority level to dequeue from
 * @param timeout_ms Maximum time to wait for a request
 * @return Pointer to request context, or NULL if timeout/error
 */
request_context_t* request_queue_dequeue_priority(request_priority_t priority, 
                                                  uint32_t timeout_ms);

/**
 * @brief Get queue statistics for a priority level
 * 
 * @param priority Priority level to get stats for
 * @param stats Pointer to structure to receive statistics
 * @return true if stats retrieved successfully, false otherwise
 */
bool request_queue_get_stats(request_priority_t priority, queue_stats_t *stats);

/**
 * @brief Get total queue depth across all priorities
 * 
 * @return Total number of queued requests
 */
uint32_t request_queue_get_total_depth(void);

/**
 * @brief Check if any queues have requests
 * 
 * @return true if any queue has pending requests, false otherwise
 */
bool request_queue_has_pending_requests(void);

/**
 * @brief Clean up expired requests
 * 
 * Removes and frees requests that have exceeded their timeout.
 * 
 * @return Number of requests cleaned up
 */
uint32_t request_queue_cleanup_expired(void);

/**
 * @brief Get queue depth for specific priority
 * 
 * @param priority Priority level to check
 * @return Current queue depth for the priority level
 */
uint16_t request_queue_get_depth(request_priority_t priority);

/**
 * @brief Check if a priority queue is full
 * 
 * @param priority Priority level to check
 * @return true if queue is full, false otherwise
 */
bool request_queue_is_full(request_priority_t priority);

/**
 * @brief Check if a priority queue is empty
 * 
 * @param priority Priority level to check
 * @return true if queue is empty, false otherwise
 */
bool request_queue_is_empty(request_priority_t priority);

/**
 * @brief Convert priority enum to string
 * 
 * @param priority Priority level
 * @return String representation of priority
 */
const char* request_queue_priority_to_string(request_priority_t priority);

/**
 * @brief Generate unique request ID
 * 
 * Creates a unique identifier for request tracking.
 * 
 * @param buffer Buffer to store the ID
 * @param buffer_size Size of the buffer
 * @return true if ID generated successfully, false otherwise
 */
bool request_queue_generate_id(char *buffer, size_t buffer_size);

/* =============================================================================
 * MONITORING AND DEBUG FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Print detailed queue status report
 * 
 * Outputs comprehensive information about all priority queues.
 */
void request_queue_print_status_report(void);

/**
 * @brief Print queue statistics
 * 
 * Shows performance metrics and utilization for all queues.
 */
void request_queue_print_statistics(void);

/**
 * @brief Reset queue statistics
 * 
 * Clears all statistical counters for fresh monitoring.
 */
void request_queue_reset_statistics(void);

/**
 * @brief Enable/disable queue monitoring
 * 
 * Controls whether queue statistics are collected.
 * 
 * @param enable true to enable monitoring, false to disable
 */
void request_queue_set_monitoring_enabled(bool enable);

/**
 * @brief Check queue health
 * 
 * Performs basic health checks on all priority queues.
 * 
 * @return true if all queues are healthy, false if issues detected
 */
bool request_queue_health_check(void);

#ifdef __cplusplus
}
#endif

#endif /* REQUEST_QUEUE_H */
