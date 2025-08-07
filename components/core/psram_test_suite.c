/**
 * @file psram_test_suite.c
 * @brief Comprehensive PSRAM testing suite for SNRv9 Irrigation Control System
 * 
 * This file provides comprehensive testing functions to validate PSRAM functionality,
 * task creation, and memory allocation strategies.
 */

#include "psram_manager.h"
#include "psram_task_examples.h"
#include "debug_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "PSRAM_TEST";

/* =============================================================================
 * TEST TASK FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Test task that validates PSRAM stack allocation
 */
static void psram_stack_test_task(void *pvParameters)
{
    uint32_t test_id = (uint32_t)pvParameters;
    ESP_LOGI(TAG, "PSRAM stack test task %u started", (unsigned int)test_id);
    
    // Allocate some stack variables to test stack functionality
    char large_buffer[1024];
    memset(large_buffer, 0xAA, sizeof(large_buffer));
    
    // Verify stack data integrity
    bool integrity_ok = true;
    for (int i = 0; i < sizeof(large_buffer); i++) {
        if (large_buffer[i] != 0xAA) {
            integrity_ok = false;
            break;
        }
    }
    
    ESP_LOGI(TAG, "Task %u stack integrity: %s", 
             (unsigned int)test_id, integrity_ok ? "PASS" : "FAIL");
    
    // Test PSRAM allocation from within the task
    void* psram_ptr = psram_smart_malloc(8192, ALLOC_LARGE_BUFFER);
    if (psram_ptr) {
        ESP_LOGI(TAG, "Task %u PSRAM allocation: SUCCESS", (unsigned int)test_id);
        
        // Test memory access
        memset(psram_ptr, 0x55, 8192);
        bool mem_ok = true;
        uint8_t* test_ptr = (uint8_t*)psram_ptr;
        for (int i = 0; i < 8192; i++) {
            if (test_ptr[i] != 0x55) {
                mem_ok = false;
                break;
            }
        }
        
        ESP_LOGI(TAG, "Task %u PSRAM memory test: %s", 
                 (unsigned int)test_id, mem_ok ? "PASS" : "FAIL");
        
        psram_smart_free(psram_ptr);
    } else {
        ESP_LOGW(TAG, "Task %u PSRAM allocation: FAILED", (unsigned int)test_id);
    }
    
    // Run for a short time to validate stability
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGD(TAG, "Task %u iteration %d", (unsigned int)test_id, i);
    }
    
    ESP_LOGI(TAG, "PSRAM stack test task %u completed successfully", (unsigned int)test_id);
    vTaskDelete(NULL);
}

/**
 * @brief Test task for internal RAM forced allocation
 */
static void internal_ram_test_task(void *pvParameters)
{
    uint32_t test_id = (uint32_t)pvParameters;
    ESP_LOGI(TAG, "Internal RAM test task %u started", (unsigned int)test_id);
    
    // Test critical allocation (should be in internal RAM)
    void* critical_ptr = psram_smart_malloc(2048, ALLOC_CRITICAL);
    if (critical_ptr) {
        ESP_LOGI(TAG, "Task %u critical allocation: SUCCESS", (unsigned int)test_id);
        
        // Verify it's NOT in PSRAM
        bool is_psram = psram_is_psram_ptr(critical_ptr);
        ESP_LOGI(TAG, "Task %u critical ptr in PSRAM: %s (should be NO)", 
                 (unsigned int)test_id, is_psram ? "YES" : "NO");
        
        psram_smart_free(critical_ptr);
    } else {
        ESP_LOGE(TAG, "Task %u critical allocation: FAILED", (unsigned int)test_id);
    }
    
    ESP_LOGI(TAG, "Internal RAM test task %u completed", (unsigned int)test_id);
    vTaskDelete(NULL);
}

/* =============================================================================
 * PUBLIC TEST FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Test PSRAM manager initialization and basic functionality
 */
bool psram_test_basic_functionality(void)
{
    ESP_LOGI(TAG, "=== PSRAM BASIC FUNCTIONALITY TEST ===");
    
    // Test manager initialization
    if (!psram_manager_init()) {
        ESP_LOGE(TAG, "PSRAM manager initialization failed");
        return false;
    }
    
    ESP_LOGI(TAG, "PSRAM manager initialization: PASS");
    
    // Test availability check
    bool available = psram_manager_is_available();
    ESP_LOGI(TAG, "PSRAM availability: %s", available ? "YES" : "NO");
    
    // Test info retrieval
    psram_info_t info;
    if (psram_manager_get_info(&info)) {
        ESP_LOGI(TAG, "PSRAM info retrieval: PASS");
        ESP_LOGI(TAG, "  Available: %s", info.psram_available ? "YES" : "NO");
        ESP_LOGI(TAG, "  Total size: %zu bytes", info.psram_total_size);
        ESP_LOGI(TAG, "  Free size: %zu bytes", info.psram_free_size);
    } else {
        ESP_LOGW(TAG, "PSRAM info retrieval: FAILED");
    }
    
    // Test enhanced stats
    enhanced_memory_stats_t stats;
    if (psram_manager_get_enhanced_stats(&stats)) {
        ESP_LOGI(TAG, "Enhanced stats retrieval: PASS");
        ESP_LOGI(TAG, "  Internal RAM usage: %u%%", stats.internal_usage_percent);
        ESP_LOGI(TAG, "  PSRAM usage: %u%%", stats.psram_usage_percent);
        ESP_LOGI(TAG, "  Total memory usage: %u%%", stats.total_usage_percent);
    } else {
        ESP_LOGW(TAG, "Enhanced stats retrieval: FAILED");
    }
    
    ESP_LOGI(TAG, "=== BASIC FUNCTIONALITY TEST COMPLETE ===");
    return true;
}

/**
 * @brief Test PSRAM allocation strategies
 */
bool psram_test_allocation_strategies(void)
{
#if DEBUG_PSRAM_ALLOCATION_STRATEGY
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== PSRAM ALLOCATION STRATEGIES TEST ===");
#endif
    
    // Test critical allocation (should be internal RAM)
#if DEBUG_PSRAM_MEMORY_ACCESS
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Testing critical allocation strategy...");
#endif
    void* critical_ptr = psram_smart_malloc(1024, ALLOC_CRITICAL);
    bool critical_test = (critical_ptr != NULL);
    if (critical_ptr) {
#if DEBUG_PSRAM_MEMORY_ACCESS
        // Validate memory address before using
        ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Critical allocation at: 0x%08lx", (unsigned long)critical_ptr);
        
        // Safe memory test
        volatile uint32_t* test_ptr = (volatile uint32_t*)critical_ptr;
        *test_ptr = 0xDEADBEEF;
        if (*test_ptr == 0xDEADBEEF) {
            ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Critical allocation memory test: PASS");
        } else {
            ESP_LOGE(DEBUG_PSRAM_SAFETY_TAG, "Critical allocation memory test: FAIL");
        }
#endif
        bool is_psram = psram_is_psram_ptr(critical_ptr);
        ESP_LOGI(TAG, "Critical allocation in PSRAM: %s (should be NO)", 
                 is_psram ? "YES" : "NO");
        psram_smart_free(critical_ptr);
    }
    
    // Test large buffer allocation (should prefer PSRAM if available)
#if DEBUG_PSRAM_MEMORY_ACCESS
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Testing large buffer allocation strategy...");
#endif
    void* large_ptr = psram_smart_malloc(32768, ALLOC_LARGE_BUFFER);
    bool large_test = (large_ptr != NULL);
    if (large_ptr) {
#if DEBUG_PSRAM_MEMORY_ACCESS
        ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Large buffer allocation at: 0x%08lx", (unsigned long)large_ptr);
        
        // Test memory access pattern
        memset(large_ptr, 0x55, 1024);  // Test first 1KB only for safety
        uint8_t* check_ptr = (uint8_t*)large_ptr;
        if (check_ptr[0] == 0x55 && check_ptr[1023] == 0x55) {
            ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Large buffer memory access: PASS");
        } else {
            ESP_LOGE(DEBUG_PSRAM_SAFETY_TAG, "Large buffer memory access: FAIL");
        }
#endif
        bool is_psram = psram_is_psram_ptr(large_ptr);
        ESP_LOGI(TAG, "Large buffer allocation in PSRAM: %s", 
                 is_psram ? "YES" : "NO");
        psram_smart_free(large_ptr);
    }
    
    // Test cache allocation
#if DEBUG_PSRAM_MEMORY_ACCESS
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Testing cache allocation strategy...");
#endif
    void* cache_ptr = psram_smart_malloc(16384, ALLOC_CACHE);
    bool cache_test = (cache_ptr != NULL);
    if (cache_ptr) {
#if DEBUG_PSRAM_MEMORY_ACCESS
        ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Cache allocation at: 0x%08lx", (unsigned long)cache_ptr);
#endif
        bool is_psram = psram_is_psram_ptr(cache_ptr);
        ESP_LOGI(TAG, "Cache allocation in PSRAM: %s", 
                 is_psram ? "YES" : "NO");
        psram_smart_free(cache_ptr);
    }
    
    // Test normal allocation
#if DEBUG_PSRAM_MEMORY_ACCESS
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Testing normal allocation strategy...");
#endif
    void* normal_ptr = psram_smart_malloc(2048, ALLOC_NORMAL);
    bool normal_test = (normal_ptr != NULL);
    if (normal_ptr) {
#if DEBUG_PSRAM_MEMORY_ACCESS
        ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Normal allocation at: 0x%08lx", (unsigned long)normal_ptr);
#endif
        psram_smart_free(normal_ptr);
    }
    
#if DEBUG_PSRAM_ALLOCATION_STRATEGY
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Allocation test results:");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "  Critical: %s", critical_test ? "PASS" : "FAIL");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "  Large buffer: %s", large_test ? "PASS" : "FAIL");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "  Cache: %s", cache_test ? "PASS" : "FAIL");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "  Normal: %s", normal_test ? "PASS" : "FAIL");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== ALLOCATION STRATEGIES TEST COMPLETE ===");
#endif
    return critical_test && large_test && cache_test && normal_test;
}

/**
 * @brief Test PSRAM task creation functionality with enhanced safety
 */
bool psram_test_task_creation(void)
{
#if DEBUG_PSRAM_TASK_CREATION
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== PSRAM TASK CREATION TEST ===");
#endif
    
    bool all_tests_passed = true;
    TaskHandle_t task_handles[2] = {NULL, NULL};  // Track task handles for cleanup
    
    // Test 1: Create task with PSRAM stack (reduced complexity)
#if DEBUG_PSRAM_TASK_CREATION
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Creating task '%s' with stack size %d", "psram_test_1", 4096);
#endif
    psram_task_config_t psram_config = {
        .task_function = psram_stack_test_task,
        .task_name = "psram_test_1",
        .stack_size = 4096,  // Reduced from 6144 for safety
        .parameters = (void*)1,
        .priority = 3,
        .task_handle = &task_handles[0],
        .use_psram = true,
        .force_internal = false
    };
    
    // Pre-allocation safety check
    size_t free_heap_before = esp_get_free_heap_size();
    if (free_heap_before < 50000) {
        ESP_LOGW(TAG, "Insufficient heap for task creation test, skipping");
        return false;
    }
    
    BaseType_t result1 = psram_create_task(&psram_config);
    ESP_LOGI(TAG, "PSRAM task creation: %s", result1 == pdPASS ? "PASS" : "FAIL");
    if (result1 != pdPASS) {
        all_tests_passed = false;
    } else {
        // Validate task handle
        if (task_handles[0] == NULL) {
            ESP_LOGE(TAG, "Task created but handle is NULL");
            all_tests_passed = false;
        }
    }
    
    // Wait for first task to stabilize before creating second
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_task_wdt_reset();
    
    // Test 2: Create task with forced internal RAM (simplified)
#if DEBUG_PSRAM_TASK_CREATION
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Creating task '%s' with stack size %d (internal RAM)", "internal_test_1", 2048);
#endif
    psram_task_config_t internal_config = {
        .task_function = internal_ram_test_task,
        .task_name = "internal_test_1",
        .stack_size = 2048,  // Reduced from 3072 for safety
        .parameters = (void*)2,
        .priority = 3,
        .task_handle = &task_handles[1],
        .use_psram = false,
        .force_internal = true
    };
    
    BaseType_t result2 = psram_create_task(&internal_config);
    ESP_LOGI(TAG, "Internal RAM task creation: %s", result2 == pdPASS ? "PASS" : "FAIL");
    if (result2 != pdPASS) {
        all_tests_passed = false;
    } else {
        // Validate task handle
        if (task_handles[1] == NULL) {
            ESP_LOGE(TAG, "Task created but handle is NULL");
            all_tests_passed = false;
        }
    }
    
    // Wait for tasks to complete with watchdog management
#if DEBUG_PSRAM_TASK_CREATION
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "Waiting for test tasks to complete...");
#endif
    for (int i = 0; i < 20; i++) {  // 2 second total wait
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_task_wdt_reset();
    }
    
    // Validate task handles are still valid (tasks should have self-deleted)
    for (int i = 0; i < 2; i++) {
        if (task_handles[i] != NULL) {
            // Check if task still exists
            eTaskState task_state = eTaskGetState(task_handles[i]);
            if (task_state != eDeleted) {
                ESP_LOGW(TAG, "Task %d still running, state: %d", i, task_state);
            }
        }
    }
    
#if DEBUG_PSRAM_TASK_CREATION
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== TASK CREATION TEST COMPLETE ===");
#endif
    return all_tests_passed;
}

/**
 * @brief Test PSRAM health check functionality
 */
bool psram_test_health_check(void)
{
    ESP_LOGI(TAG, "=== PSRAM HEALTH CHECK TEST ===");
    
    bool health_ok = psram_manager_health_check();
    ESP_LOGI(TAG, "PSRAM health check: %s", health_ok ? "PASS" : "FAIL");
    
    // Print detailed reports
    psram_manager_print_detailed_report();
    psram_manager_print_allocation_stats();
    
    ESP_LOGI(TAG, "=== HEALTH CHECK TEST COMPLETE ===");
    return health_ok;
}

/**
 * @brief Test PSRAM under memory pressure
 */
bool psram_test_memory_pressure(void)
{
    ESP_LOGI(TAG, "=== PSRAM MEMORY PRESSURE TEST ===");
    
    const int num_allocations = 10;
    void* allocations[num_allocations];
    int successful_allocations = 0;
    
    // Allocate multiple large buffers
    for (int i = 0; i < num_allocations; i++) {
        allocations[i] = psram_smart_malloc(32768, ALLOC_LARGE_BUFFER);
        if (allocations[i]) {
            successful_allocations++;
            ESP_LOGD(TAG, "Allocation %d: SUCCESS", i);
        } else {
            ESP_LOGD(TAG, "Allocation %d: FAILED", i);
        }
    }
    
    ESP_LOGI(TAG, "Successful allocations: %d/%d", successful_allocations, num_allocations);
    
    // Test memory access on successful allocations
    bool memory_integrity = true;
    for (int i = 0; i < num_allocations; i++) {
        if (allocations[i]) {
            memset(allocations[i], 0x33, 32768);
            uint8_t* test_ptr = (uint8_t*)allocations[i];
            for (int j = 0; j < 1024; j++) {  // Test first 1KB
                if (test_ptr[j] != 0x33) {
                    memory_integrity = false;
                    break;
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "Memory integrity: %s", memory_integrity ? "PASS" : "FAIL");
    
    // Clean up allocations
    for (int i = 0; i < num_allocations; i++) {
        if (allocations[i]) {
            psram_smart_free(allocations[i]);
        }
    }
    
    ESP_LOGI(TAG, "=== MEMORY PRESSURE TEST COMPLETE ===");
    return memory_integrity && (successful_allocations > 0);
}

/**
 * @brief Run comprehensive PSRAM test suite
 */
bool psram_run_comprehensive_test_suite(void)
{
#if DEBUG_PSRAM_TEST_VERBOSE
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "========================================");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "STARTING COMPREHENSIVE PSRAM TEST SUITE");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "========================================");
#endif

#if DEBUG_PSRAM_SAFETY_CHECKS
    // Pre-test system validation
    esp_err_t heap_check = heap_caps_check_integrity_all(true);
    if (heap_check != ESP_OK) {
        ESP_LOGE(DEBUG_PSRAM_SAFETY_TAG, "Heap integrity check failed before test - aborting");
        return false;
    }
    ESP_LOGI(DEBUG_PSRAM_SAFETY_TAG, "Pre-test heap integrity: PASS");
#endif
    
    bool all_tests_passed = true;
    
    // Phase 1: Basic functionality
#if DEBUG_PSRAM_TEST_VERBOSE
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== PHASE 1: BASIC FUNCTIONALITY TEST ===");
#endif
    if (!psram_test_basic_functionality()) {
        all_tests_passed = false;
    }
    
    // Add delay between phases for system recovery
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Phase 2: Allocation strategies
#if DEBUG_PSRAM_ALLOCATION_STRATEGY
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== PHASE 2: ALLOCATION STRATEGIES TEST ===");
#endif
    if (!psram_test_allocation_strategies()) {
        all_tests_passed = false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Phase 3: Task creation
#if DEBUG_PSRAM_TASK_CREATION
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== PHASE 3: TASK CREATION TEST ===");
#endif
    if (!psram_test_task_creation()) {
        all_tests_passed = false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Phase 4: Health check
#if DEBUG_PSRAM_HEALTH_CHECK
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== PHASE 4: HEALTH CHECK TEST ===");
#endif
    if (!psram_test_health_check()) {
        all_tests_passed = false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Phase 5: Memory pressure
#if DEBUG_PSRAM_TEST_VERBOSE
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== PHASE 5: MEMORY PRESSURE TEST ===");
#endif
    if (!psram_test_memory_pressure()) {
        all_tests_passed = false;
    }

#if DEBUG_PSRAM_SAFETY_CHECKS
    // Post-test system validation
    heap_check = heap_caps_check_integrity_all(true);
    if (heap_check != ESP_OK) {
        ESP_LOGE(DEBUG_PSRAM_SAFETY_TAG, "Heap integrity check failed after test");
    } else {
        ESP_LOGI(DEBUG_PSRAM_SAFETY_TAG, "Post-test heap integrity: PASS");
    }
#endif

#if DEBUG_PSRAM_TEST_VERBOSE
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "========================================");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "COMPREHENSIVE TEST SUITE COMPLETE");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "OVERALL RESULT: %s", all_tests_passed ? "PASS" : "FAIL");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "========================================");
#endif
    
    return all_tests_passed;
}

/**
 * @brief Run comprehensive PSRAM test suite with task yielding
 * 
 * This version includes strategic task yields and watchdog resets to prevent
 * system timeouts during intensive testing with comprehensive heap debugging.
 */
bool psram_run_comprehensive_test_suite_with_yields(void)
{
#if DEBUG_PSRAM_TEST_VERBOSE
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "========================================");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "STARTING YIELDING PSRAM TEST SUITE");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "========================================");
#endif
    
    bool all_tests_passed = true;
    
    // Phase 1: Basic functionality with yielding
#if DEBUG_PSRAM_TEST_VERBOSE
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== PHASE 1: BASIC FUNCTIONALITY TEST ===");
#endif
    esp_task_wdt_reset();
    if (!psram_test_basic_functionality()) {
        all_tests_passed = false;
    }
    
    // Extended delay between phases for system recovery
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_task_wdt_reset();

    // Phase 2: Allocation strategies with yielding
#if DEBUG_PSRAM_ALLOCATION_STRATEGY
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== PHASE 2: ALLOCATION STRATEGIES TEST ===");
#endif
    esp_task_wdt_reset();
    if (!psram_test_allocation_strategies()) {
        all_tests_passed = false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_task_wdt_reset();

    // Phase 3: Task creation with yielding
#if DEBUG_PSRAM_TASK_CREATION
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== PHASE 3: TASK CREATION TEST ===");
#endif
    esp_task_wdt_reset();
    if (!psram_test_task_creation()) {
        all_tests_passed = false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_task_wdt_reset();

    // Phase 4: Health check with yielding
#if DEBUG_PSRAM_HEALTH_CHECK
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== PHASE 4: HEALTH CHECK TEST ===");
#endif
    esp_task_wdt_reset();
    if (!psram_test_health_check()) {
        all_tests_passed = false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_task_wdt_reset();

    // Phase 5: Memory pressure with yielding
#if DEBUG_PSRAM_TEST_VERBOSE
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "=== PHASE 5: MEMORY PRESSURE TEST ===");
#endif
    esp_task_wdt_reset();
    if (!psram_test_memory_pressure()) {
        all_tests_passed = false;
    }
    
    // Final system recovery delay
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_task_wdt_reset();

#if DEBUG_PSRAM_TEST_VERBOSE
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "========================================");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "YIELDING TEST SUITE COMPLETE");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "OVERALL RESULT: %s", all_tests_passed ? "PASS" : "FAIL");
    ESP_LOGI(DEBUG_PSRAM_TEST_TAG, "========================================");
#endif
    
    return all_tests_passed;
}

/**
 * @brief Quick PSRAM functionality test
 */
bool psram_quick_test(void)
{
    ESP_LOGI(TAG, "=== QUICK PSRAM TEST ===");
    
    // Initialize if not already done
    if (!psram_manager_init()) {
        ESP_LOGE(TAG, "PSRAM manager initialization failed");
        return false;
    }
    
    // Test basic allocation
    void* test_ptr = psram_smart_malloc(4096, ALLOC_LARGE_BUFFER);
    if (!test_ptr) {
        ESP_LOGW(TAG, "PSRAM allocation failed, testing internal RAM fallback");
        test_ptr = psram_smart_malloc(4096, ALLOC_CRITICAL);
    }
    
    bool test_passed = false;
    if (test_ptr) {
        // Test memory access
        memset(test_ptr, 0xCC, 4096);
        uint8_t* check_ptr = (uint8_t*)test_ptr;
        test_passed = (check_ptr[0] == 0xCC && check_ptr[4095] == 0xCC);
        
        ESP_LOGI(TAG, "Memory allocation and access: %s", test_passed ? "PASS" : "FAIL");
        psram_smart_free(test_ptr);
    } else {
        ESP_LOGE(TAG, "Memory allocation: FAIL");
    }
    
    // Test task creation
    psram_task_config_t quick_config = {
        .task_function = psram_stack_test_task,
        .task_name = "quick_test",
        .stack_size = 3072,
        .parameters = (void*)99,
        .priority = 3,
        .task_handle = NULL,
        .use_psram = true,
        .force_internal = false
    };
    
    BaseType_t task_result = psram_create_task(&quick_config);
    bool task_test_passed = (task_result == pdPASS);
    ESP_LOGI(TAG, "Task creation: %s", task_test_passed ? "PASS" : "FAIL");
    
    if (task_test_passed) {
        vTaskDelay(pdMS_TO_TICKS(2000));  // Wait for task to complete
    }
    
    bool overall_result = test_passed && task_test_passed;
    ESP_LOGI(TAG, "=== QUICK TEST RESULT: %s ===", overall_result ? "PASS" : "FAIL");
    
    return overall_result;
}
