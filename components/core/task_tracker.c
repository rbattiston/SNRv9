/**
 * @file task_tracker.c
 * @brief Task tracking and monitoring implementation for SNRv9 Irrigation Control System
 */

#include "task_tracker.h"
#include "debug_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * PRIVATE CONSTANTS AND MACROS
 * =============================================================================
 */

#define TASK_TRACKER_TASK_STACK_SIZE    3072  // Increased from 2048
#define TASK_TRACKER_TASK_PRIORITY      1
#define TASK_TRACKER_TASK_NAME          "task_tracker"

// Stack size constants for known tasks
#define DEFAULT_STACK_SIZE              2048
#define MEMORY_MONITOR_STACK_SIZE       3072
#define WIFI_MONITOR_STACK_SIZE         6144
#define TASK_TRACKER_STACK_SIZE         3072

#if DEBUG_INCLUDE_TIMESTAMPS
#define TIMESTAMP_FORMAT "[%02d:%02d:%02d.%03d] "
#define GET_TIMESTAMP() esp_timer_get_time() / 1000ULL
#define FORMAT_TIMESTAMP(ms) \
    (int)((ms / 3600000) % 24), \
    (int)((ms / 60000) % 60), \
    (int)((ms / 1000) % 60), \
    (int)(ms % 1000)
#else
#define TIMESTAMP_FORMAT ""
#define GET_TIMESTAMP() 0
#define FORMAT_TIMESTAMP(ms) 
#endif

/* =============================================================================
 * PRIVATE TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief Registered stack size entry
 */
typedef struct {
    char task_name[configMAX_TASK_NAME_LEN];
    uint32_t stack_size;
    bool is_valid;
} registered_stack_size_t;

typedef struct {
    task_tracker_status_t status;
    TaskHandle_t tracker_task_handle;
    SemaphoreHandle_t data_mutex;
    task_info_t task_list[DEBUG_MAX_TASKS_TRACKED];
    task_tracking_stats_t stats;
    bool enabled;
    uint32_t last_report_time;
    uint32_t last_update_time;
    void (*creation_callback)(const task_info_t *task);
    void (*deletion_callback)(const task_info_t *task);
    registered_stack_size_t registered_stacks[DEBUG_MAX_TASKS_TRACKED];
} task_tracker_context_t;

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static task_tracker_context_t g_task_tracker = {0};
static const char *TAG = DEBUG_TASK_TAG;

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static void task_tracker_task(void *pvParameters);
static void update_task_list(void);
static void print_task_report(void);
static task_state_t freertos_state_to_task_state(eTaskState state);
static const char* task_state_to_string(task_state_t state);
static void calculate_task_stats(void);
static int find_task_by_handle(TaskHandle_t handle);
static int find_empty_slot(void);
static uint32_t estimate_task_stack_size(const char *task_name);
static uint32_t get_registered_stack_size_unsafe(const char *task_name);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool task_tracker_init(void)
{
    if (g_task_tracker.status != TASK_TRACKER_STOPPED) {
        ESP_LOGW(TAG, "Task tracker already initialized");
        return false;
    }

    // Initialize context
    memset(&g_task_tracker, 0, sizeof(task_tracker_context_t));
    g_task_tracker.status = TASK_TRACKER_STOPPED;
    g_task_tracker.enabled = (DEBUG_TASK_TRACKING == 1);

    // Create mutex for thread-safe access
    g_task_tracker.data_mutex = xSemaphoreCreateMutex();
    if (g_task_tracker.data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create data mutex");
        g_task_tracker.status = TASK_TRACKER_ERROR;
        return false;
    }

    // Initialize task list
    for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
        g_task_tracker.task_list[i].is_valid = false;
    }

    ESP_LOGI(TAG, "Task tracker initialized successfully");
    return true;
}

bool task_tracker_start(void)
{
    if (g_task_tracker.status == TASK_TRACKER_RUNNING) {
        ESP_LOGW(TAG, "Task tracker already running");
        return true;
    }

    if (g_task_tracker.status == TASK_TRACKER_ERROR) {
        ESP_LOGE(TAG, "Cannot start task tracker - in error state");
        return false;
    }

    if (!g_task_tracker.enabled) {
        ESP_LOGI(TAG, "Task tracker disabled by configuration");
        return true;
    }

    // Create tracking task
    BaseType_t result = xTaskCreate(
        task_tracker_task,
        TASK_TRACKER_TASK_NAME,
        TASK_TRACKER_TASK_STACK_SIZE,
        NULL,
        TASK_TRACKER_TASK_PRIORITY,
        &g_task_tracker.tracker_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task tracker task");
        g_task_tracker.status = TASK_TRACKER_ERROR;
        return false;
    }

    g_task_tracker.status = TASK_TRACKER_RUNNING;
    g_task_tracker.last_report_time = GET_TIMESTAMP();
    g_task_tracker.last_update_time = GET_TIMESTAMP();

    ESP_LOGI(TAG, "Task tracker started successfully");
    return true;
}

bool task_tracker_stop(void)
{
    if (g_task_tracker.status != TASK_TRACKER_RUNNING) {
        ESP_LOGW(TAG, "Task tracker not running");
        return true;
    }

    // Delete tracking task
    if (g_task_tracker.tracker_task_handle != NULL) {
        vTaskDelete(g_task_tracker.tracker_task_handle);
        g_task_tracker.tracker_task_handle = NULL;
    }

    g_task_tracker.status = TASK_TRACKER_STOPPED;
    ESP_LOGI(TAG, "Task tracker stopped");
    return true;
}

task_tracker_status_t task_tracker_get_status(void)
{
    return g_task_tracker.status;
}

void task_tracker_update(void)
{
    if (!g_task_tracker.enabled) {
        return;
    }

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        update_task_list();
        calculate_task_stats();
        xSemaphoreGive(g_task_tracker.data_mutex);
    }
}

bool task_tracker_get_task_info(const char *task_name, task_info_t *info)
{
    if (task_name == NULL || info == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (g_task_tracker.task_list[i].is_valid &&
                strcmp(g_task_tracker.task_list[i].name, task_name) == 0) {
                memcpy(info, &g_task_tracker.task_list[i], sizeof(task_info_t));
                xSemaphoreGive(g_task_tracker.data_mutex);
                return true;
            }
        }
        xSemaphoreGive(g_task_tracker.data_mutex);
    }

    return false;
}

bool task_tracker_get_all_tasks(task_info_t *task_list, uint16_t max_tasks, uint16_t *num_tasks)
{
    if (task_list == NULL || num_tasks == NULL || max_tasks == 0) {
        return false;
    }

    *num_tasks = 0;

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED && *num_tasks < max_tasks; i++) {
            if (g_task_tracker.task_list[i].is_valid) {
                memcpy(&task_list[*num_tasks], &g_task_tracker.task_list[i], sizeof(task_info_t));
                (*num_tasks)++;
            }
        }
        xSemaphoreGive(g_task_tracker.data_mutex);
        return true;
    }

    return false;
}

bool task_tracker_get_stats(task_tracking_stats_t *stats)
{
    if (stats == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &g_task_tracker.stats, sizeof(task_tracking_stats_t));
        xSemaphoreGive(g_task_tracker.data_mutex);
        return true;
    }

    return false;
}

void task_tracker_force_report(void)
{
    if (!g_task_tracker.enabled) {
        return;
    }

    task_tracker_update();
    print_task_report();
}

uint8_t task_tracker_calc_stack_usage_pct(const task_info_t *info)
{
    if (info == NULL || info->stack_size == 0) {
        return 0;
    }

    return (uint8_t)((info->stack_used * 100) / info->stack_size);
}

bool task_tracker_find_highest_stack_usage(task_info_t *info)
{
    if (info == NULL) {
        return false;
    }

    bool found = false;
    uint8_t highest_usage = 0;

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (g_task_tracker.task_list[i].is_valid) {
                uint8_t usage = task_tracker_calc_stack_usage_pct(&g_task_tracker.task_list[i]);
                if (usage > highest_usage) {
                    highest_usage = usage;
                    memcpy(info, &g_task_tracker.task_list[i], sizeof(task_info_t));
                    found = true;
                }
            }
        }
        xSemaphoreGive(g_task_tracker.data_mutex);
    }

    return found;
}

bool task_tracker_find_lowest_remaining_stack(task_info_t *info)
{
    if (info == NULL) {
        return false;
    }

    bool found = false;
    uint32_t lowest_remaining = UINT32_MAX;

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (g_task_tracker.task_list[i].is_valid) {
                if (g_task_tracker.task_list[i].stack_high_water_mark < lowest_remaining) {
                    lowest_remaining = g_task_tracker.task_list[i].stack_high_water_mark;
                    memcpy(info, &g_task_tracker.task_list[i], sizeof(task_info_t));
                    found = true;
                }
            }
        }
        xSemaphoreGive(g_task_tracker.data_mutex);
    }

    return found;
}

uint16_t task_tracker_check_stack_overflow(uint8_t threshold_pct)
{
    uint16_t count = 0;

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (g_task_tracker.task_list[i].is_valid) {
                uint8_t usage = task_tracker_calc_stack_usage_pct(&g_task_tracker.task_list[i]);
                if (usage >= threshold_pct) {
                    count++;
                }
            }
        }
        xSemaphoreGive(g_task_tracker.data_mutex);
    }

    return count;
}

void task_tracker_reset_stats(void)
{
    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&g_task_tracker.stats, 0, sizeof(task_tracking_stats_t));
        xSemaphoreGive(g_task_tracker.data_mutex);
    }
}

void task_tracker_set_enabled(bool enable)
{
    g_task_tracker.enabled = enable;
    if (!enable && g_task_tracker.status == TASK_TRACKER_RUNNING) {
        task_tracker_stop();
    }
}

bool task_tracker_is_enabled(void)
{
    return g_task_tracker.enabled;
}

void task_tracker_print_detailed_report(void)
{
    if (!g_task_tracker.enabled) {
        return;
    }

    task_tracker_update();

    uint32_t timestamp = GET_TIMESTAMP();
    printf(TIMESTAMP_FORMAT "%s: === DETAILED TASK REPORT ===\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        printf(TIMESTAMP_FORMAT "%s: %-12s %-8s %-8s %-8s %-10s %s\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               "Task", "Priority", "State", "Stack%", "Remaining", "Name");
        
        printf(TIMESTAMP_FORMAT "%s: %s\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               "------------------------------------------------------------");

        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (g_task_tracker.task_list[i].is_valid) {
                task_info_t *task = &g_task_tracker.task_list[i];
                uint8_t usage_pct = task_tracker_calc_stack_usage_pct(task);
                
                printf(TIMESTAMP_FORMAT "%s: %-12s %-8u %-8s %-7u%% %-10u %s\n",
                       FORMAT_TIMESTAMP(timestamp), TAG,
                       task->name,
                       (unsigned int)task->priority,
                       task_state_to_string(task->state),
                       usage_pct,
                       (unsigned int)task->stack_high_water_mark,
                       task->name);
            }
        }

        printf(TIMESTAMP_FORMAT "%s: Total Tasks: %u, Active: %u, Max Seen: %u\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               g_task_tracker.stats.total_tasks,
               g_task_tracker.stats.active_tasks,
               g_task_tracker.stats.max_tasks_seen);

        xSemaphoreGive(g_task_tracker.data_mutex);
    }

    printf(TIMESTAMP_FORMAT "%s: ================================\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
}

void task_tracker_print_summary(void)
{
    if (!g_task_tracker.enabled) {
        return;
    }

    uint32_t timestamp = GET_TIMESTAMP();
    printf(TIMESTAMP_FORMAT "%s: Tasks=%u Active=%u MaxSeen=%u WorstStack=%lu%%\n",
           FORMAT_TIMESTAMP(timestamp), TAG,
           (unsigned int)g_task_tracker.stats.total_tasks,
           (unsigned int)g_task_tracker.stats.active_tasks,
           (unsigned int)g_task_tracker.stats.max_tasks_seen,
           (unsigned long)g_task_tracker.stats.worst_stack_usage_pct);
}

void task_tracker_check_stack_warnings(void)
{
    if (!g_task_tracker.enabled) {
        return;
    }

    uint32_t timestamp = GET_TIMESTAMP();
    
    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (g_task_tracker.task_list[i].is_valid) {
                task_info_t *task = &g_task_tracker.task_list[i];
                
                // Use the actual calculated stack usage from the task structure
                uint8_t usage_pct = task_tracker_calc_stack_usage_pct(task);
                uint32_t stack_used = task->stack_used;
                uint32_t stack_total = task->stack_size;
                
                // Issue warnings based on usage
                if (usage_pct >= 90) {
                    printf(TIMESTAMP_FORMAT "%s: *** CRITICAL *** Task '%s' stack usage: %u%% (%u/%u bytes) - OVERFLOW IMMINENT!\n",
                           FORMAT_TIMESTAMP(timestamp), TAG,
                           task->name, usage_pct, (unsigned int)stack_used, (unsigned int)stack_total);
                } else if (usage_pct >= 80) {
                    printf(TIMESTAMP_FORMAT "%s: ** WARNING ** Task '%s' stack usage: %u%% (%u/%u bytes) - Monitor closely\n",
                           FORMAT_TIMESTAMP(timestamp), TAG,
                           task->name, usage_pct, (unsigned int)stack_used, (unsigned int)stack_total);
                } else if (usage_pct >= 70) {
                    printf(TIMESTAMP_FORMAT "%s: * NOTICE * Task '%s' stack usage: %u%% (%u/%u bytes)\n",
                           FORMAT_TIMESTAMP(timestamp), TAG,
                           task->name, usage_pct, (unsigned int)stack_used, (unsigned int)stack_total);
                }
            }
        }
        xSemaphoreGive(g_task_tracker.data_mutex);
    }
}

void task_tracker_print_stack_analysis(void)
{
    if (!g_task_tracker.enabled) {
        return;
    }

    task_tracker_update();

    uint32_t timestamp = GET_TIMESTAMP();
    printf(TIMESTAMP_FORMAT "%s: === STACK ANALYSIS ===\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);

    // Check for tasks with high stack usage
    uint16_t warning_count = task_tracker_check_stack_overflow(80);
    uint16_t critical_count = task_tracker_check_stack_overflow(90);

    printf(TIMESTAMP_FORMAT "%s: Stack Usage Warnings (>80%%): %u\n",
           FORMAT_TIMESTAMP(timestamp), TAG, warning_count);
    
    printf(TIMESTAMP_FORMAT "%s: Stack Usage Critical (>90%%): %u\n",
           FORMAT_TIMESTAMP(timestamp), TAG, critical_count);

    // Find task with lowest remaining stack
    task_info_t lowest_task;
    if (task_tracker_find_lowest_remaining_stack(&lowest_task)) {
        printf(TIMESTAMP_FORMAT "%s: Lowest Remaining Stack: %s (%u bytes)\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               lowest_task.name, (unsigned int)lowest_task.stack_high_water_mark);
    }

    // Check for immediate warnings
    task_tracker_check_stack_warnings();

    printf(TIMESTAMP_FORMAT "%s: ===================\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
}

void task_tracker_register_creation_callback(void (*callback)(const task_info_t *task))
{
    g_task_tracker.creation_callback = callback;
}

void task_tracker_register_deletion_callback(void (*callback)(const task_info_t *task))
{
    g_task_tracker.deletion_callback = callback;
}

/* =============================================================================
 * STACK SIZE REGISTRATION FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool task_tracker_register_stack_size(const char *task_name, uint32_t stack_size)
{
    if (task_name == NULL || stack_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters for stack size registration");
        return false;
    }

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Check if task is already registered
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (g_task_tracker.registered_stacks[i].is_valid &&
                strcmp(g_task_tracker.registered_stacks[i].task_name, task_name) == 0) {
                // Update existing registration
                g_task_tracker.registered_stacks[i].stack_size = stack_size;
                xSemaphoreGive(g_task_tracker.data_mutex);
                ESP_LOGI(TAG, "Updated registered stack size for task '%s': %u bytes", task_name, (unsigned int)stack_size);
                return true;
            }
        }

        // Find empty slot for new registration
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (!g_task_tracker.registered_stacks[i].is_valid) {
                strncpy(g_task_tracker.registered_stacks[i].task_name, task_name, configMAX_TASK_NAME_LEN - 1);
                g_task_tracker.registered_stacks[i].task_name[configMAX_TASK_NAME_LEN - 1] = '\0';
                g_task_tracker.registered_stacks[i].stack_size = stack_size;
                g_task_tracker.registered_stacks[i].is_valid = true;
                xSemaphoreGive(g_task_tracker.data_mutex);
                ESP_LOGI(TAG, "Registered stack size for task '%s': %u bytes", task_name, (unsigned int)stack_size);
                return true;
            }
        }

        xSemaphoreGive(g_task_tracker.data_mutex);
        ESP_LOGW(TAG, "No free slots for stack size registration");
        return false;
    }

    ESP_LOGE(TAG, "Failed to acquire mutex for stack size registration");
    return false;
}

bool task_tracker_update_stack_size(const char *task_name, uint32_t stack_size)
{
    if (task_name == NULL || stack_size == 0) {
        return false;
    }

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (g_task_tracker.registered_stacks[i].is_valid &&
                strcmp(g_task_tracker.registered_stacks[i].task_name, task_name) == 0) {
                g_task_tracker.registered_stacks[i].stack_size = stack_size;
                xSemaphoreGive(g_task_tracker.data_mutex);
                ESP_LOGI(TAG, "Updated stack size for task '%s': %u bytes", task_name, (unsigned int)stack_size);
                return true;
            }
        }
        xSemaphoreGive(g_task_tracker.data_mutex);
    }

    return false;
}

uint32_t task_tracker_get_registered_stack_size(const char *task_name)
{
    if (task_name == NULL) {
        return 0;
    }

    uint32_t stack_size = 0;

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (g_task_tracker.registered_stacks[i].is_valid &&
                strcmp(g_task_tracker.registered_stacks[i].task_name, task_name) == 0) {
                stack_size = g_task_tracker.registered_stacks[i].stack_size;
                break;
            }
        }
        xSemaphoreGive(g_task_tracker.data_mutex);
    }

    return stack_size;
}

bool task_tracker_unregister_stack_size(const char *task_name)
{
    if (task_name == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (g_task_tracker.registered_stacks[i].is_valid &&
                strcmp(g_task_tracker.registered_stacks[i].task_name, task_name) == 0) {
                memset(&g_task_tracker.registered_stacks[i], 0, sizeof(registered_stack_size_t));
                xSemaphoreGive(g_task_tracker.data_mutex);
                ESP_LOGI(TAG, "Unregistered stack size for task '%s'", task_name);
                return true;
            }
        }
        xSemaphoreGive(g_task_tracker.data_mutex);
    }

    return false;
}

bool task_tracker_has_registered_stack_size(const char *task_name)
{
    if (task_name == NULL) {
        return false;
    }

    bool found = false;

    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (g_task_tracker.registered_stacks[i].is_valid &&
                strcmp(g_task_tracker.registered_stacks[i].task_name, task_name) == 0) {
                found = true;
                break;
            }
        }
        xSemaphoreGive(g_task_tracker.data_mutex);
    }

    return found;
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static void task_tracker_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Task tracker task started");

    while (g_task_tracker.status == TASK_TRACKER_RUNNING) {
        uint32_t current_time = GET_TIMESTAMP();

        // Update task list periodically
        if ((current_time - g_task_tracker.last_update_time) >= 1000) {
            task_tracker_update();
            g_task_tracker.last_update_time = current_time;
        }

        // Print report if it's time
        if ((current_time - g_task_tracker.last_report_time) >= DEBUG_TASK_REPORT_INTERVAL_MS) {
            print_task_report();
            g_task_tracker.last_report_time = current_time;
        }

        // Sleep for a short time to prevent busy waiting
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "Task tracker task ended");
    vTaskDelete(NULL);
}

static void update_task_list(void)
{
    UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_status_array = pvPortMalloc(num_tasks * sizeof(TaskStatus_t));
    
    if (task_status_array == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for task status array");
        return;
    }

    // Get current task information from FreeRTOS
    UBaseType_t actual_tasks = uxTaskGetSystemState(task_status_array, num_tasks, NULL);

    // Mark all current entries as potentially stale
    for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
        if (g_task_tracker.task_list[i].is_valid) {
            g_task_tracker.task_list[i].is_valid = false;
        }
    }

    // Update task information
    for (UBaseType_t i = 0; i < actual_tasks; i++) {
        TaskStatus_t *status = &task_status_array[i];
        
        // Find existing entry or create new one
        int slot = find_task_by_handle(status->xHandle);
        if (slot == -1) {
            slot = find_empty_slot();
            if (slot == -1) {
                ESP_LOGW(TAG, "No free slots for task tracking");
                continue;
            }
            
            // New task detected
            if (g_task_tracker.creation_callback) {
                // Will call callback after updating the entry
            }
        }

        task_info_t *task = &g_task_tracker.task_list[slot];
        
        // Update task information
        task->handle = status->xHandle;
        strncpy(task->name, status->pcTaskName, configMAX_TASK_NAME_LEN - 1);
        task->name[configMAX_TASK_NAME_LEN - 1] = '\0';
        task->priority = status->uxCurrentPriority;
        task->state = freertos_state_to_task_state(status->eCurrentState);
        task->runtime_counter = status->ulRunTimeCounter;
        task->stack_high_water_mark = status->usStackHighWaterMark * sizeof(StackType_t);
        task->stack_size = estimate_task_stack_size(task->name);
        
        // Prevent integer underflow and validate stack calculations
        if (task->stack_high_water_mark <= task->stack_size) {
            task->stack_used = task->stack_size - task->stack_high_water_mark;
        } else {
            // High water mark is larger than estimated stack size - use a conservative estimate
            task->stack_used = task->stack_size * 80 / 100; // Assume 80% usage
            ESP_LOGW(TAG, "Task '%s': High water mark (%u) > estimated stack (%u), using conservative estimate",
                     task->name, (unsigned int)task->stack_high_water_mark, (unsigned int)task->stack_size);
        }
        task->creation_time = GET_TIMESTAMP();
        task->is_valid = true;

        // Call creation callback for new tasks
        if (slot >= 0 && g_task_tracker.creation_callback) {
            g_task_tracker.creation_callback(task);
        }
    }

    // Check for deleted tasks
    for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
        if (!g_task_tracker.task_list[i].is_valid && 
            g_task_tracker.task_list[i].handle != NULL) {
            
            // Task was deleted
            if (g_task_tracker.deletion_callback) {
                g_task_tracker.deletion_callback(&g_task_tracker.task_list[i]);
            }
            
            // Clear the entry
            memset(&g_task_tracker.task_list[i], 0, sizeof(task_info_t));
        }
    }

    vPortFree(task_status_array);
}

static void print_task_report(void)
{
    if (!g_task_tracker.enabled) {
        return;
    }

    uint32_t timestamp = GET_TIMESTAMP();
    
    if (xSemaphoreTake(g_task_tracker.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
            if (g_task_tracker.task_list[i].is_valid) {
                task_info_t *task = &g_task_tracker.task_list[i];
                uint8_t usage_pct = task_tracker_calc_stack_usage_pct(task);
                
                printf(TIMESTAMP_FORMAT "%s: %s Stack=%u/%u(%u%%) State=%s Priority=%u\n",
                       FORMAT_TIMESTAMP(timestamp), TAG,
                       task->name,
                       (unsigned int)task->stack_used,
                       (unsigned int)task->stack_size,
                       usage_pct,
                       task_state_to_string(task->state),
                       (unsigned int)task->priority);
            }
        }
        xSemaphoreGive(g_task_tracker.data_mutex);
    }
}

static task_state_t freertos_state_to_task_state(eTaskState state)
{
    switch (state) {
        case eRunning:    return TASK_STATE_RUNNING;
        case eReady:      return TASK_STATE_READY;
        case eBlocked:    return TASK_STATE_BLOCKED;
        case eSuspended:  return TASK_STATE_SUSPENDED;
        case eDeleted:    return TASK_STATE_DELETED;
        default:          return TASK_STATE_INVALID;
    }
}

static const char* task_state_to_string(task_state_t state)
{
    switch (state) {
        case TASK_STATE_RUNNING:   return "Running";
        case TASK_STATE_READY:     return "Ready";
        case TASK_STATE_BLOCKED:   return "Blocked";
        case TASK_STATE_SUSPENDED: return "Suspended";
        case TASK_STATE_DELETED:   return "Deleted";
        default:                   return "Invalid";
    }
}

static void calculate_task_stats(void)
{
    memset(&g_task_tracker.stats, 0, sizeof(task_tracking_stats_t));
    
    uint32_t worst_usage = 0;
    
    for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
        if (g_task_tracker.task_list[i].is_valid) {
            g_task_tracker.stats.total_tasks++;
            g_task_tracker.stats.active_tasks++;
            g_task_tracker.stats.total_stack_allocated += g_task_tracker.task_list[i].stack_size;
            g_task_tracker.stats.total_stack_used += g_task_tracker.task_list[i].stack_used;
            
            uint8_t usage_pct = task_tracker_calc_stack_usage_pct(&g_task_tracker.task_list[i]);
            if (usage_pct > worst_usage) {
                worst_usage = usage_pct;
                strncpy(g_task_tracker.stats.worst_stack_task, 
                       g_task_tracker.task_list[i].name, 
                       configMAX_TASK_NAME_LEN - 1);
            }
        }
    }
    
    g_task_tracker.stats.worst_stack_usage_pct = worst_usage;
    
    if (g_task_tracker.stats.active_tasks > g_task_tracker.stats.max_tasks_seen) {
        g_task_tracker.stats.max_tasks_seen = g_task_tracker.stats.active_tasks;
    }
}

static int find_task_by_handle(TaskHandle_t handle)
{
    for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
        if (g_task_tracker.task_list[i].handle == handle) {
            return i;
        }
    }
    return -1;
}

static int find_empty_slot(void)
{
    for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
        if (!g_task_tracker.task_list[i].is_valid && 
            g_task_tracker.task_list[i].handle == NULL) {
            return i;
        }
    }
    return -1;
}

static uint32_t get_registered_stack_size_unsafe(const char *task_name)
{
    if (task_name == NULL) {
        return 0;
    }

    // This function assumes the mutex is already held by the caller
    for (int i = 0; i < DEBUG_MAX_TASKS_TRACKED; i++) {
        if (g_task_tracker.registered_stacks[i].is_valid &&
            strcmp(g_task_tracker.registered_stacks[i].task_name, task_name) == 0) {
            return g_task_tracker.registered_stacks[i].stack_size;
        }
    }

    return 0;
}

static uint32_t estimate_task_stack_size(const char *task_name)
{
    if (task_name == NULL) {
        return DEFAULT_STACK_SIZE;
    }

    // First check if we have a registered stack size for this task
    // Use the unsafe version since we're already holding the mutex in update_task_list()
    uint32_t registered_size = get_registered_stack_size_unsafe(task_name);
    if (registered_size > 0) {
        return registered_size;
    }

    // Use configuration-based stack sizes where available
    if (strcmp(task_name, "main") == 0) {
        #ifdef CONFIG_ESP_MAIN_TASK_STACK_SIZE
            return CONFIG_ESP_MAIN_TASK_STACK_SIZE;
        #else
            return 3584; // Fallback for older ESP-IDF versions
        #endif
    } else if (strcmp(task_name, "tiT") == 0) {
        #ifdef CONFIG_LWIP_TCPIP_TASK_STACK_SIZE
            return CONFIG_LWIP_TCPIP_TASK_STACK_SIZE;
        #else
            return 4096; // Fallback
        #endif
    } else if (strcmp(task_name, "sys_evt") == 0) {
        #ifdef CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE
            return CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE;
        #else
            return 2304; // Fallback
        #endif
    } else if (strcmp(task_name, "esp_timer") == 0) {
        #ifdef CONFIG_ESP_TIMER_TASK_STACK_SIZE
            return CONFIG_ESP_TIMER_TASK_STACK_SIZE;
        #else
            return 3584; // Fallback
        #endif
    } else if (strcmp(task_name, "ipc0") == 0 || strcmp(task_name, "ipc1") == 0) {
        #ifdef CONFIG_ESP_IPC_TASK_STACK_SIZE
            return CONFIG_ESP_IPC_TASK_STACK_SIZE;
        #else
            return 1024; // Fallback
        #endif
    } else if (strcmp(task_name, "Tmr Svc") == 0) {
        #ifdef CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH
            return CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH;
        #else
            return 2048; // Fallback
        #endif
    } else if (strncmp(task_name, "IDLE", 4) == 0) {
        #ifdef CONFIG_FREERTOS_IDLE_TASK_STACKSIZE
            return CONFIG_FREERTOS_IDLE_TASK_STACKSIZE;
        #else
            return 1536; // Fallback
        #endif
    }
    
    // Known task stack sizes based on our local configuration
    else if (strcmp(task_name, "task_tracker") == 0) {
        return TASK_TRACKER_STACK_SIZE;
    } else if (strcmp(task_name, "mem_monitor") == 0) {
        return MEMORY_MONITOR_STACK_SIZE;
    } else if (strcmp(task_name, "wifi_monitor") == 0) {
        return WIFI_MONITOR_STACK_SIZE;
    } else if (strcmp(task_name, "wifi") == 0) {
        return 6656; // ESP-IDF WiFi task stack size (not easily configurable)
    }

    // Default for unknown tasks
    return DEFAULT_STACK_SIZE;
}
