/**
 * @file psram_test_suite.h
 * @brief Comprehensive PSRAM testing suite interface for SNRv9 Irrigation Control System
 * 
 * This header provides function declarations for comprehensive PSRAM testing,
 * including task creation, memory allocation, and system validation.
 */

#ifndef PSRAM_TEST_SUITE_H
#define PSRAM_TEST_SUITE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Test PSRAM manager initialization and basic functionality
 * 
 * Validates PSRAM detection, manager initialization, and basic
 * information retrieval functions.
 * 
 * @return true if all basic functionality tests pass, false otherwise
 */
bool psram_test_basic_functionality(void);

/**
 * @brief Test PSRAM allocation strategies
 * 
 * Validates different allocation priorities and memory placement
 * strategies (critical, large buffer, cache, normal).
 * 
 * @return true if all allocation strategy tests pass, false otherwise
 */
bool psram_test_allocation_strategies(void);

/**
 * @brief Test PSRAM task creation functionality
 * 
 * Validates task creation with different stack allocation strategies
 * including PSRAM stacks, internal RAM stacks, and standard allocation.
 * 
 * @return true if all task creation tests pass, false otherwise
 */
bool psram_test_task_creation(void);

/**
 * @brief Test PSRAM health check functionality
 * 
 * Validates PSRAM health monitoring and diagnostic reporting functions.
 * 
 * @return true if health check passes, false otherwise
 */
bool psram_test_health_check(void);

/**
 * @brief Test PSRAM under memory pressure
 * 
 * Validates PSRAM behavior under high memory usage conditions,
 * including allocation failures and memory integrity.
 * 
 * @return true if memory pressure tests pass, false otherwise
 */
bool psram_test_memory_pressure(void);

/**
 * @brief Run comprehensive PSRAM test suite
 * 
 * Executes all PSRAM tests in sequence and provides a comprehensive
 * validation of the entire PSRAM management system.
 * 
 * @return true if all tests pass, false if any test fails
 */
bool psram_run_comprehensive_test_suite(void);

/**
 * @brief Run comprehensive PSRAM test suite with task yielding
 * 
 * Executes all PSRAM tests in sequence with strategic task yields and
 * watchdog resets to prevent system timeouts during intensive testing
 * with comprehensive heap debugging enabled.
 * 
 * @return true if all tests pass, false if any test fails
 */
bool psram_run_comprehensive_test_suite_with_yields(void);

/**
 * @brief Quick PSRAM functionality test
 * 
 * Performs a quick validation of basic PSRAM functionality including
 * initialization, allocation, and task creation. Suitable for regular
 * system health checks.
 * 
 * @return true if quick test passes, false otherwise
 */
bool psram_quick_test(void);

#ifdef __cplusplus
}
#endif

#endif /* PSRAM_TEST_SUITE_H */
