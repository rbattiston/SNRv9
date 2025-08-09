/**
 * @file psram_manager.h
 * @brief PSRAM management system interface for SNRv9 Irrigation Control System
 * 
 * This module provides comprehensive PSRAM detection, allocation, and management
 * capabilities to maximize memory efficiency and system performance.
 */

#ifndef PSRAM_MANAGER_H
#define PSRAM_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "debug_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief PSRAM information structure
 */
typedef struct {
    bool psram_available;           /**< True if PSRAM is detected and available */
    size_t psram_total_size;        /**< Total PSRAM size in bytes */
    size_t psram_free_size;         /**< Current free PSRAM size in bytes */
    size_t psram_minimum_free;      /**< Minimum free PSRAM ever recorded */
    size_t psram_largest_block;     /**< Largest contiguous PSRAM block */
    size_t internal_reserved;       /**< Internal RAM reserved for critical operations */
    uint32_t psram_allocations;     /**< Number of successful PSRAM allocations */
    uint32_t psram_failures;        /**< Number of failed PSRAM allocations */
    uint32_t fallback_allocations;  /**< Number of fallbacks to internal RAM */
    
    // NEW: Step 9 category tracking
    size_t time_mgmt_bytes;         /**< Bytes allocated for time management */
    size_t scheduling_bytes;        /**< Bytes allocated for scheduling */
    size_t alarming_bytes;          /**< Bytes allocated for alarming */
    size_t trending_bytes;          /**< Bytes allocated for trending */
    size_t web_buffer_bytes;        /**< Bytes allocated for web buffers */
    
    // Per-category allocation counts
    uint32_t time_mgmt_allocations;     /**< Time management allocation count */
    uint32_t scheduling_allocations;    /**< Scheduling allocation count */
    uint32_t alarming_allocations;      /**< Alarming allocation count */
    uint32_t trending_allocations;      /**< Trending allocation count */
    uint32_t web_buffer_allocations;    /**< Web buffer allocation count */
} psram_info_t;

/**
 * @brief Step 9 PSRAM status structure
 */
typedef struct {
    // Category usage
    size_t time_mgmt_used;          /**< Time management bytes used */
    size_t scheduling_used;         /**< Scheduling bytes used */
    size_t alarming_used;           /**< Alarming bytes used */
    size_t trending_used;           /**< Trending bytes used */
    size_t web_buffer_used;         /**< Web buffer bytes used */
    
    // Category allocation counts
    uint32_t time_mgmt_count;       /**< Time management allocation count */
    uint32_t scheduling_count;      /**< Scheduling allocation count */
    uint32_t alarming_count;        /**< Alarming allocation count */
    uint32_t trending_count;        /**< Trending allocation count */
    uint32_t web_buffer_count;      /**< Web buffer allocation count */
    
    // Total Step 9 usage
    size_t total_step9_bytes;       /**< Total Step 9 bytes allocated */
    uint32_t total_step9_allocations; /**< Total Step 9 allocations */
    
    uint32_t timestamp_ms;          /**< Timestamp of status */
} psram_step9_status_t;

/**
 * @brief Memory allocation priority levels
 */
typedef enum {
    ALLOC_CRITICAL = 0,     /**< Critical - force internal RAM */
    ALLOC_NORMAL,           /**< Normal - use default allocation strategy */
    ALLOC_LARGE_BUFFER,     /**< Large buffer - prefer PSRAM */
    ALLOC_CACHE,            /**< Cache data - prefer PSRAM */
    ALLOC_TASK_STACK        /**< Task stack - prefer PSRAM for large stacks */
} allocation_priority_t;

/**
 * @brief PSRAM allocation strategy categories for Step 9 Advanced Features
 */
typedef enum {
    PSRAM_ALLOC_CRITICAL = 0,        /**< Existing - Critical operations */
    PSRAM_ALLOC_LARGE_BUFFER = 1,    /**< Existing - Large buffer allocations */
    PSRAM_ALLOC_CACHE = 2,           /**< Existing - Cache data */
    PSRAM_ALLOC_NORMAL = 3,          /**< Existing - Normal allocations */
    
    // NEW: Step 9 allocation categories
    PSRAM_ALLOC_TIME_MGMT = 4,       /**< Time Management - 128KB - Timezone DB, NTP history */
    PSRAM_ALLOC_SCHEDULING = 5,      /**< Scheduling - 1MB - Schedule storage, cron parsing */
    PSRAM_ALLOC_ALARMING = 6,        /**< Alarming - 256KB - Alarm states, history */
    PSRAM_ALLOC_TRENDING = 7,        /**< Trending - 2MB - Data buffers (reduced from 3MB) */
    PSRAM_ALLOC_WEB_BUFFERS = 8      /**< Web Buffers - 512KB - HTTP response buffers */
} psram_allocation_strategy_t;

/**
 * @brief Enhanced memory statistics including PSRAM
 */
typedef struct {
    // Internal RAM stats
    uint32_t internal_free;
    uint32_t internal_minimum_free;
    uint32_t internal_total;
    uint32_t internal_largest_block;
    
    // PSRAM stats
    uint32_t psram_free;
    uint32_t psram_minimum_free;
    uint32_t psram_total;
    uint32_t psram_largest_block;
    
    // Combined stats
    uint32_t total_free_memory;
    uint32_t total_memory;
    
    // Usage percentages
    uint8_t internal_usage_percent;
    uint8_t psram_usage_percent;
    uint8_t total_usage_percent;
    
    uint32_t timestamp_ms;
} enhanced_memory_stats_t;

/**
 * @brief Task configuration with PSRAM support
 */
typedef struct {
    TaskFunction_t task_function;   /**< Task function pointer */
    const char* task_name;          /**< Task name */
    uint32_t stack_size;            /**< Stack size in bytes */
    void* parameters;               /**< Task parameters */
    UBaseType_t priority;           /**< Task priority */
    TaskHandle_t* task_handle;      /**< Pointer to task handle */
    bool use_psram;                 /**< True to allocate stack in PSRAM */
    bool force_internal;            /**< True to force internal RAM allocation */
} psram_task_config_t;

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the PSRAM management system
 * 
 * Detects PSRAM availability, initializes management structures,
 * and sets up allocation policies.
 * 
 * @return true if initialization successful, false otherwise
 */
bool psram_manager_init(void);

/**
 * @brief Check if PSRAM is available and functional
 * 
 * @return true if PSRAM is available, false otherwise
 */
bool psram_manager_is_available(void);

/**
 * @brief Get current PSRAM information
 * 
 * @param info Pointer to structure to receive PSRAM info
 * @return true if info retrieved successfully, false otherwise
 */
bool psram_manager_get_info(psram_info_t *info);

/**
 * @brief Get enhanced memory statistics including PSRAM
 * 
 * @param stats Pointer to structure to receive enhanced stats
 * @return true if stats retrieved successfully, false otherwise
 */
bool psram_manager_get_enhanced_stats(enhanced_memory_stats_t *stats);

/**
 * @brief Smart memory allocation with priority-based selection
 * 
 * Automatically chooses the optimal memory type based on allocation
 * priority and current memory conditions.
 * 
 * @param size Size in bytes to allocate
 * @param priority Allocation priority level
 * @return Pointer to allocated memory, or NULL if allocation failed
 */
void* psram_smart_malloc(size_t size, allocation_priority_t priority);

/**
 * @brief Smart memory allocation with zero initialization
 * 
 * @param num Number of elements
 * @param size Size of each element
 * @param priority Allocation priority level
 * @return Pointer to allocated and zeroed memory, or NULL if allocation failed
 */
void* psram_smart_calloc(size_t num, size_t size, allocation_priority_t priority);

/**
 * @brief Smart memory reallocation
 * 
 * @param ptr Pointer to existing memory block
 * @param size New size in bytes
 * @param priority Allocation priority level
 * @return Pointer to reallocated memory, or NULL if reallocation failed
 */
void* psram_smart_realloc(void* ptr, size_t size, allocation_priority_t priority);

/**
 * @brief Free memory allocated by PSRAM manager
 * 
 * @param ptr Pointer to memory to free
 */
void psram_smart_free(void* ptr);

/**
 * @brief Create task with PSRAM-aware stack allocation
 * 
 * Creates a FreeRTOS task with intelligent stack allocation based on
 * configuration and PSRAM availability.
 * 
 * @param config Task configuration including PSRAM preferences
 * @return pdPASS if task created successfully, pdFAIL otherwise
 */
BaseType_t psram_create_task(const psram_task_config_t* config);

/**
 * @brief Allocate memory specifically in PSRAM
 * 
 * Forces allocation in PSRAM, fails if PSRAM not available or full.
 * 
 * @param size Size in bytes to allocate
 * @return Pointer to PSRAM memory, or NULL if allocation failed
 */
void* psram_malloc(size_t size);

/**
 * @brief Allocate and zero memory specifically in PSRAM
 * 
 * @param num Number of elements
 * @param size Size of each element
 * @return Pointer to PSRAM memory, or NULL if allocation failed
 */
void* psram_calloc(size_t num, size_t size);

/**
 * @brief Allocate memory specifically in internal RAM
 * 
 * Forces allocation in internal RAM for critical operations.
 * 
 * @param size Size in bytes to allocate
 * @return Pointer to internal RAM, or NULL if allocation failed
 */
void* psram_internal_malloc(size_t size);

/**
 * @brief Check if a pointer points to PSRAM
 * 
 * @param ptr Pointer to check
 * @return true if pointer is in PSRAM, false otherwise
 */
bool psram_is_psram_ptr(void* ptr);

/**
 * @brief Get free PSRAM size
 * 
 * @return Free PSRAM size in bytes, 0 if PSRAM not available
 */
size_t psram_get_free_size(void);

/**
 * @brief Get total PSRAM size
 * 
 * @return Total PSRAM size in bytes, 0 if PSRAM not available
 */
size_t psram_get_total_size(void);

/**
 * @brief Get largest free PSRAM block
 * 
 * @return Largest free PSRAM block in bytes, 0 if PSRAM not available
 */
size_t psram_get_largest_free_block(void);

/* =============================================================================
 * DIAGNOSTIC AND MONITORING FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Print detailed PSRAM status report
 * 
 * Outputs comprehensive PSRAM information including usage statistics,
 * allocation counts, and performance metrics.
 */
void psram_manager_print_detailed_report(void);

/**
 * @brief Print PSRAM allocation statistics
 * 
 * Shows allocation success/failure rates and fallback statistics.
 */
void psram_manager_print_allocation_stats(void);

/**
 * @brief Reset PSRAM statistics counters
 * 
 * Resets allocation counters and minimum free tracking.
 */
void psram_manager_reset_stats(void);

/**
 * @brief Check PSRAM health and performance
 * 
 * Performs basic PSRAM functionality tests and reports any issues.
 * 
 * @return true if PSRAM is healthy, false if issues detected
 */
bool psram_manager_health_check(void);

/**
 * @brief Set internal RAM reservation size
 * 
 * Configures how much internal RAM to reserve for critical operations.
 * 
 * @param reserve_bytes Bytes to reserve in internal RAM
 * @return true if reservation set successfully, false otherwise
 */
bool psram_manager_set_internal_reservation(size_t reserve_bytes);

/**
 * @brief Enable/disable PSRAM usage at runtime
 * 
 * Allows dynamic control of PSRAM usage without recompilation.
 * 
 * @param enable true to enable PSRAM usage, false to disable
 */
void psram_manager_set_enabled(bool enable);

/**
 * @brief Check if PSRAM usage is enabled
 * 
 * @return true if PSRAM usage is enabled, false otherwise
 */
bool psram_manager_is_enabled(void);

/* =============================================================================
 * STEP 9 ADVANCED FEATURES FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Allocate memory for specific Step 9 category
 * 
 * Allocates memory in PSRAM for Step 9 advanced features with category tracking.
 * 
 * @param category Step 9 allocation category
 * @param size Size in bytes to allocate
 * @param ptr Pointer to receive allocated memory address
 * @return ESP_OK if allocation successful, ESP_ERR_* otherwise
 */
esp_err_t psram_manager_allocate_for_category(psram_allocation_strategy_t category, 
                                             size_t size, void** ptr);

/**
 * @brief Get category usage statistics
 * 
 * @param category Step 9 allocation category
 * @param used Pointer to receive bytes used for category
 * @param allocated Pointer to receive total allocations for category
 * @return ESP_OK if successful, ESP_ERR_* otherwise
 */
esp_err_t psram_manager_get_category_usage(psram_allocation_strategy_t category, 
                                          size_t* used, size_t* allocated);

/**
 * @brief Get Step 9 PSRAM status
 * 
 * @param status Pointer to structure to receive Step 9 status
 * @return ESP_OK if successful, ESP_ERR_* otherwise
 */
esp_err_t psram_manager_get_step9_status(psram_step9_status_t* status);

/**
 * @brief Extend PSRAM manager for Step 9 features
 * 
 * Initializes Step 9 category tracking and prepares for advanced feature allocation.
 * 
 * @return ESP_OK if successful, ESP_ERR_* otherwise
 */
esp_err_t psram_manager_extend_for_step9(void);

/**
 * @brief Print Step 9 PSRAM usage report
 * 
 * Outputs detailed Step 9 category usage statistics and allocation information.
 */
void psram_manager_print_step9_report(void);

#ifdef __cplusplus
}
#endif

#endif /* PSRAM_MANAGER_H */
