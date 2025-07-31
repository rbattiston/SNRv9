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
} psram_info_t;

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

#ifdef __cplusplus
}
#endif

#endif /* PSRAM_MANAGER_H */
