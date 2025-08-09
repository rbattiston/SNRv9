/**
 * @file time_manager.c
 * @brief Time Management System implementation for SNRv9 Irrigation Control System
 */

#include "time_manager.h"
#include "psram_manager.h"
#include "debug_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <sys/time.h>

/* =============================================================================
 * PRIVATE CONSTANTS AND MACROS
 * =============================================================================
 */

#if DEBUG_TIME_MANAGEMENT
#define TIME_MANAGER_TAG "TIME_MGR"
#else
#define TIME_MANAGER_TAG ""
#endif

#define TIME_MANAGER_TASK_STACK_SIZE        3072
#define TIME_MANAGER_TASK_PRIORITY          2
#define TIME_MANAGER_TASK_CORE              1

#define TIME_MANAGER_NVS_NAMESPACE          "time_mgr"
#define TIME_MANAGER_NVS_CONFIG_KEY         "config"
#define TIME_MANAGER_NVS_STATS_KEY          "stats"

#define TIME_MANAGER_DEFAULT_SYNC_INTERVAL  3600    // 1 hour
#define TIME_MANAGER_WIFI_WAIT_TIMEOUT_MS   30000   // 30 seconds

// Event group bits
#define TIME_MANAGER_WIFI_CONNECTED_BIT     BIT0
#define TIME_MANAGER_NTP_SYNC_DONE_BIT      BIT1
#define TIME_MANAGER_SHUTDOWN_BIT           BIT2

/* =============================================================================
 * PRIVATE TYPE DEFINITIONS
 * =============================================================================
 */

typedef struct {
    time_manager_status_t status;
    time_manager_config_t config;
    time_manager_stats_t stats;
    
    // PSRAM allocated data
    timezone_info_t *timezone_db;
    ntp_sync_record_t *ntp_history;
    uint32_t ntp_history_count;
    uint32_t ntp_history_index;
    
    // Synchronization
    SemaphoreHandle_t mutex;
    EventGroupHandle_t event_group;
    TaskHandle_t task_handle;
    
    // State tracking
    bool wifi_connected;
    bool ntp_initialized;
    uint64_t last_sync_attempt_ms;
    uint64_t next_auto_sync_ms;
    
    // NTP callback state
    volatile bool ntp_sync_in_progress;
    volatile ntp_sync_status_t last_ntp_status;
    uint64_t ntp_sync_start_ms;
    
    // Five-state reliability tracking
    time_reliability_state_t reliability_state;
    bool first_sync_achieved;          // Persistent flag in NVS
    time_t last_successful_sync;       // Last good NTP sync
    uint32_t consecutive_sync_failures; // Count of recent failures
    bool time_uncertain_flag;          // For data collection marking
    
} time_manager_context_t;

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static time_manager_context_t g_time_manager = {0};
static const char *TAG = TIME_MANAGER_TAG;

// Common timezone database (stored in PSRAM)
static const timezone_info_t common_timezones[] = {
    {"UTC", "UTC0", "Coordinated Universal Time", 0, false},
    {"EST", "EST5EDT,M3.2.0,M11.1.0", "Eastern Standard Time", -18000, false},
    {"CST", "CST6CDT,M3.2.0,M11.1.0", "Central Standard Time", -21600, false},
    {"MST", "MST7MDT,M3.2.0,M11.1.0", "Mountain Standard Time", -25200, false},
    {"PST", "PST8PDT,M3.2.0,M11.1.0", "Pacific Standard Time", -28800, false},
    {"GMT", "GMT0", "Greenwich Mean Time", 0, false},
    {"CET", "CET-1CEST,M3.5.0,M10.5.0/3", "Central European Time", 3600, false},
    {"JST", "JST-9", "Japan Standard Time", 32400, false},
    {"AEST", "AEST-10AEDT,M10.1.0,M4.1.0/3", "Australian Eastern Standard Time", 36000, false},
    {"IST", "IST-5:30", "India Standard Time", 19800, false}
};

#define COMMON_TIMEZONE_COUNT (sizeof(common_timezones) / sizeof(common_timezones[0]))

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static esp_err_t time_manager_load_config(void);
static esp_err_t time_manager_save_config(void);
static esp_err_t time_manager_load_stats(void);
static esp_err_t time_manager_save_stats(void);
static esp_err_t time_manager_allocate_psram(void);
static void time_manager_task(void *pvParameters);
static void time_manager_sntp_sync_notification_cb(struct timeval *tv);
static void time_manager_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t time_manager_start_ntp_sync(void);
static esp_err_t time_manager_stop_ntp_sync(void);
static void time_manager_add_ntp_history_record(ntp_sync_status_t status, uint32_t duration_ms, const char *server);
static esp_err_t time_manager_update_timezone_info(timezone_info_t *tz_info);
static bool time_manager_is_wifi_connected(void);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

esp_err_t time_manager_init(void)
{
    if (g_time_manager.status != TIME_MANAGER_NOT_INITIALIZED) {
        ESP_LOGW(TAG, "Time manager already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing time management system...");

    // Initialize context
    memset(&g_time_manager, 0, sizeof(time_manager_context_t));
    
    // Initialize five-state reliability tracking
    g_time_manager.reliability_state = TIME_NOT_SET;
    g_time_manager.first_sync_achieved = false;
    g_time_manager.last_successful_sync = 0;
    g_time_manager.consecutive_sync_failures = 0;
    g_time_manager.time_uncertain_flag = true; // Uncertain until first sync
    
    // Create mutex for thread-safe operations
    g_time_manager.mutex = xSemaphoreCreateMutex();
    if (g_time_manager.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create event group for synchronization
    g_time_manager.event_group = xEventGroupCreate();
    if (g_time_manager.event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        vSemaphoreDelete(g_time_manager.mutex);
        return ESP_ERR_NO_MEM;
    }

    // Allocate PSRAM for timezone database and NTP history
    esp_err_t err = time_manager_allocate_psram();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM: %s", esp_err_to_name(err));
        vEventGroupDelete(g_time_manager.event_group);
        vSemaphoreDelete(g_time_manager.mutex);
        return err;
    }

    // Load configuration from NVS
    err = time_manager_load_config();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load config, using defaults: %s", esp_err_to_name(err));
        
        // Set default configuration
        strcpy(g_time_manager.config.ntp_servers[0], "pool.ntp.org");
        strcpy(g_time_manager.config.ntp_servers[1], "time.nist.gov");
        strcpy(g_time_manager.config.ntp_servers[2], "time.google.com");
        g_time_manager.config.ntp_server_count = 3;
        strcpy(g_time_manager.config.timezone, "UTC0");
        g_time_manager.config.auto_sync_enabled = true;
        g_time_manager.config.sync_interval_s = TIME_MANAGER_DEFAULT_SYNC_INTERVAL;
    }

    // Load statistics from NVS
    err = time_manager_load_stats();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load stats, starting fresh: %s", esp_err_to_name(err));
        memset(&g_time_manager.stats, 0, sizeof(time_manager_stats_t));
    }

    // Set timezone
    setenv("TZ", g_time_manager.config.timezone, 1);
    tzset();

    // Register WiFi event handler
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &time_manager_wifi_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(err));
    }

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &time_manager_wifi_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(err));
    }

    // Create time manager task
    BaseType_t task_created = xTaskCreatePinnedToCore(
        time_manager_task,
        "time_manager",
        TIME_MANAGER_TASK_STACK_SIZE,
        NULL,
        TIME_MANAGER_TASK_PRIORITY,
        &g_time_manager.task_handle,
        TIME_MANAGER_TASK_CORE
    );

    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create time manager task");
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &time_manager_wifi_event_handler);
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &time_manager_wifi_event_handler);
        vEventGroupDelete(g_time_manager.event_group);
        vSemaphoreDelete(g_time_manager.mutex);
        return ESP_FAIL;
    }

    g_time_manager.status = TIME_MANAGER_INITIALIZED;
    ESP_LOGI(TAG, "Time management system initialized successfully");

    return ESP_OK;
}

esp_err_t time_manager_deinit(void)
{
    if (g_time_manager.status == TIME_MANAGER_NOT_INITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing time management system...");

    // Signal shutdown to task
    if (g_time_manager.event_group) {
        xEventGroupSetBits(g_time_manager.event_group, TIME_MANAGER_SHUTDOWN_BIT);
    }

    // Wait for task to finish
    if (g_time_manager.task_handle) {
        vTaskDelete(g_time_manager.task_handle);
        g_time_manager.task_handle = NULL;
    }

    // Stop NTP sync
    time_manager_stop_ntp_sync();

    // Unregister event handlers
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &time_manager_wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &time_manager_wifi_event_handler);

    // Save final statistics
    time_manager_save_stats();

    // Clean up synchronization objects
    if (g_time_manager.event_group) {
        vEventGroupDelete(g_time_manager.event_group);
        g_time_manager.event_group = NULL;
    }

    if (g_time_manager.mutex) {
        vSemaphoreDelete(g_time_manager.mutex);
        g_time_manager.mutex = NULL;
    }

    g_time_manager.status = TIME_MANAGER_NOT_INITIALIZED;
    ESP_LOGI(TAG, "Time management system deinitialized");

    return ESP_OK;
}

esp_err_t time_manager_get_status(time_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(g_time_manager.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Fill status structure
    status->status = g_time_manager.status;
    time(&status->current_time);
    memcpy(&status->stats, &g_time_manager.stats, sizeof(time_manager_stats_t));
    status->ntp_available = g_time_manager.ntp_initialized;
    status->wifi_connected = g_time_manager.wifi_connected;

    // Calculate next sync time
    if (g_time_manager.config.auto_sync_enabled && g_time_manager.next_auto_sync_ms > 0) {
        uint64_t current_ms = esp_timer_get_time() / 1000ULL;
        if (g_time_manager.next_auto_sync_ms > current_ms) {
            status->next_sync_in_s = (g_time_manager.next_auto_sync_ms - current_ms) / 1000;
        } else {
            status->next_sync_in_s = 0;
        }
    } else {
        status->next_sync_in_s = 0;
    }

    // Update timezone information
    time_manager_update_timezone_info(&status->timezone_info);

    xSemaphoreGive(g_time_manager.mutex);
    return ESP_OK;
}

esp_err_t time_manager_set_ntp_servers(const char servers[][64], uint8_t count)
{
    if (servers == NULL || count == 0 || count > TIME_MANAGER_MAX_NTP_SERVERS) {
#if DEBUG_TIME_MANAGEMENT
        ESP_LOGE(TAG, "Invalid NTP server parameters: servers=%p, count=%d", servers, count);
#endif
        return ESP_ERR_INVALID_ARG;
    }

#if DEBUG_TIME_MANAGEMENT
    ESP_LOGI(TAG, "Setting %d NTP servers:", count);
    for (uint8_t i = 0; i < count; i++) {
        ESP_LOGI(TAG, "  Server %d: %s", i, servers[i]);
    }
#endif

    if (xSemaphoreTake(g_time_manager.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
#if DEBUG_TIME_MANAGEMENT
        ESP_LOGE(TAG, "Failed to acquire mutex for NTP server configuration");
#endif
        return ESP_ERR_TIMEOUT;
    }

    // Update configuration
    for (uint8_t i = 0; i < count; i++) {
        strncpy(g_time_manager.config.ntp_servers[i], servers[i], 63);
        g_time_manager.config.ntp_servers[i][63] = '\0';
#if DEBUG_TIME_MANAGEMENT
        ESP_LOGI(TAG, "Configured NTP server %d: %s", i, g_time_manager.config.ntp_servers[i]);
#endif
    }
    g_time_manager.config.ntp_server_count = count;

    // Save configuration
    esp_err_t err = time_manager_save_config();
    if (err != ESP_OK) {
#if DEBUG_TIME_MANAGEMENT
        ESP_LOGE(TAG, "Failed to save NTP server configuration: %s", esp_err_to_name(err));
#endif
    } else {
#if DEBUG_TIME_MANAGEMENT
        ESP_LOGI(TAG, "NTP server configuration saved successfully");
#endif
    }

    xSemaphoreGive(g_time_manager.mutex);

    ESP_LOGI(TAG, "Updated NTP servers (%d configured)", count);
    return err;
}

esp_err_t time_manager_set_timezone(const char *timezone)
{
    if (timezone == NULL) {
#if DEBUG_TIMEZONE_CONFIG
        ESP_LOGE(TAG, "Invalid timezone parameter: NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }

#if DEBUG_TIMEZONE_CONFIG
    ESP_LOGI(TAG, "Setting timezone to: %s", timezone);
#endif

    if (xSemaphoreTake(g_time_manager.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
#if DEBUG_TIMEZONE_CONFIG
        ESP_LOGE(TAG, "Failed to acquire mutex for timezone configuration");
#endif
        return ESP_ERR_TIMEOUT;
    }

    // Update configuration
    strncpy(g_time_manager.config.timezone, timezone, TIME_MANAGER_MAX_TIMEZONE_LEN - 1);
    g_time_manager.config.timezone[TIME_MANAGER_MAX_TIMEZONE_LEN - 1] = '\0';

#if DEBUG_TIMEZONE_CONFIG
    ESP_LOGI(TAG, "Timezone configuration updated: %s", g_time_manager.config.timezone);
#endif

    // Apply timezone
    setenv("TZ", g_time_manager.config.timezone, 1);
    tzset();

#if DEBUG_TIMEZONE_CONFIG
    ESP_LOGI(TAG, "Timezone environment variable set and tzset() called");
#endif

    // Save configuration
    esp_err_t err = time_manager_save_config();
    if (err != ESP_OK) {
#if DEBUG_TIMEZONE_CONFIG
        ESP_LOGE(TAG, "Failed to save timezone configuration: %s", esp_err_to_name(err));
#endif
    } else {
#if DEBUG_TIMEZONE_CONFIG
        ESP_LOGI(TAG, "Timezone configuration saved successfully");
#endif
    }

    xSemaphoreGive(g_time_manager.mutex);

    ESP_LOGI(TAG, "Updated timezone to: %s", timezone);
    return err;
}

esp_err_t time_manager_force_ntp_sync(uint32_t timeout_ms)
{
    if (g_time_manager.status == TIME_MANAGER_NOT_INITIALIZED) {
#if DEBUG_NTP_SYNC_DETAILED
        ESP_LOGE(TAG, "Cannot force NTP sync: time manager not initialized");
#endif
        return ESP_ERR_INVALID_STATE;
    }

#if DEBUG_NTP_SYNC_DETAILED
    ESP_LOGI(TAG, "Force NTP sync requested with timeout: %lu ms", (unsigned long)timeout_ms);
#endif

    if (!time_manager_is_wifi_connected()) {
#if DEBUG_NTP_SYNC_DETAILED
        ESP_LOGW(TAG, "Cannot sync NTP: WiFi not connected");
#endif
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

#if DEBUG_NTP_SYNC_DETAILED
    ESP_LOGI(TAG, "WiFi connected - proceeding with NTP sync");
#endif

    ESP_LOGI(TAG, "Forcing NTP synchronization...");

    // Start NTP sync
    esp_err_t err = time_manager_start_ntp_sync();
    if (err != ESP_OK) {
#if DEBUG_NTP_SYNC_DETAILED
        ESP_LOGE(TAG, "Failed to start NTP sync: %s", esp_err_to_name(err));
#endif
        return err;
    }

#if DEBUG_NTP_SYNC_DETAILED
    ESP_LOGI(TAG, "NTP sync started successfully - waiting for completion");
#endif

    // Wait for sync completion or timeout
    uint32_t wait_timeout = (timeout_ms > 0) ? timeout_ms : TIME_MANAGER_NTP_TIMEOUT_MS;
#if DEBUG_NTP_SYNC_DETAILED
    ESP_LOGI(TAG, "Waiting for NTP sync completion (timeout: %lu ms)", (unsigned long)wait_timeout);
#endif

    EventBits_t bits = xEventGroupWaitBits(
        g_time_manager.event_group,
        TIME_MANAGER_NTP_SYNC_DONE_BIT,
        pdTRUE,  // Clear on exit
        pdFALSE, // Wait for any bit
        pdMS_TO_TICKS(wait_timeout)
    );

    if (bits & TIME_MANAGER_NTP_SYNC_DONE_BIT) {
#if DEBUG_NTP_SYNC_DETAILED
        ESP_LOGI(TAG, "NTP sync completed successfully within timeout");
#endif
        ESP_LOGI(TAG, "NTP synchronization completed");
        return ESP_OK;
    } else {
#if DEBUG_NTP_SYNC_DETAILED
        ESP_LOGW(TAG, "NTP sync timed out after %lu ms", (unsigned long)wait_timeout);
#endif
        ESP_LOGW(TAG, "NTP synchronization timed out");
        return ESP_ERR_TIMEOUT;
    }
}


esp_err_t time_manager_set_auto_sync(bool enabled, uint32_t interval_s)
{
    if (xSemaphoreTake(g_time_manager.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    g_time_manager.config.auto_sync_enabled = enabled;
    
    if (interval_s > 0) {
        g_time_manager.config.sync_interval_s = interval_s;
    }

    // Update next sync time if enabling
    if (enabled) {
        uint64_t current_ms = esp_timer_get_time() / 1000ULL;
        g_time_manager.next_auto_sync_ms = current_ms + (g_time_manager.config.sync_interval_s * 1000ULL);
    } else {
        g_time_manager.next_auto_sync_ms = 0;
    }

    esp_err_t err = time_manager_save_config();

    xSemaphoreGive(g_time_manager.mutex);

    ESP_LOGI(TAG, "Auto sync %s (interval: %lu seconds)", 
             enabled ? "enabled" : "disabled", 
             (unsigned long)g_time_manager.config.sync_interval_s);

    return err;
}

esp_err_t time_manager_get_current_time(time_t *current_time, timezone_info_t *tz_info)
{
    if (current_time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    time(current_time);

    if (tz_info != NULL) {
        return time_manager_update_timezone_info(tz_info);
    }

    return ESP_OK;
}

esp_err_t time_manager_get_formatted_time(char *buffer, size_t buffer_size, const char *format)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    time_t now;
    time(&now);
    
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    const char *fmt = format ? format : "%Y-%m-%dT%H:%M:%S%z";
    
    size_t written = strftime(buffer, buffer_size, fmt, &timeinfo);
    if (written == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t time_manager_get_ntp_history(ntp_sync_record_t *history, uint32_t max_records, uint32_t *actual_count)
{
    if (history == NULL || actual_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(g_time_manager.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint32_t records_to_copy = (g_time_manager.ntp_history_count < max_records) ? 
                               g_time_manager.ntp_history_count : max_records;

    // Copy most recent records
    for (uint32_t i = 0; i < records_to_copy; i++) {
        uint32_t index = (g_time_manager.ntp_history_index - 1 - i + TIME_MANAGER_MAX_NTP_HISTORY) % TIME_MANAGER_MAX_NTP_HISTORY;
        memcpy(&history[i], &g_time_manager.ntp_history[index], sizeof(ntp_sync_record_t));
    }

    *actual_count = records_to_copy;

    xSemaphoreGive(g_time_manager.mutex);
    return ESP_OK;
}

esp_err_t time_manager_get_timezone_list(timezone_info_t *timezones, uint32_t max_timezones, uint32_t *actual_count)
{
    if (timezones == NULL || actual_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t count = (COMMON_TIMEZONE_COUNT < max_timezones) ? COMMON_TIMEZONE_COUNT : max_timezones;
    
    for (uint32_t i = 0; i < count; i++) {
        memcpy(&timezones[i], &common_timezones[i], sizeof(timezone_info_t));
    }

    *actual_count = count;
    return ESP_OK;
}

time_reliability_state_t time_manager_get_reliability_state(void)
{
    if (g_time_manager.status == TIME_MANAGER_NOT_INITIALIZED) {
        return TIME_NOT_SET;
    }
    
    return g_time_manager.reliability_state;
}

bool time_manager_is_time_reliable(void)
{
    if (g_time_manager.status == TIME_MANAGER_NOT_INITIALIZED) {
        return false;
    }

    // Time is reliable for TIME_GOOD and TIME_GOOD_SYNC_FAILED states
    time_reliability_state_t state = g_time_manager.reliability_state;
    return (state == TIME_GOOD || state == TIME_GOOD_SYNC_FAILED);
}

bool time_manager_get_time_uncertainty_flag(void)
{
    if (g_time_manager.status == TIME_MANAGER_NOT_INITIALIZED) {
        return true; // Uncertain when not initialized
    }
    
    return g_time_manager.time_uncertain_flag;
}

esp_err_t time_manager_get_reliability_status_string(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *status_strings[] = {
        "Time Not Set",
        "Syncing...",
        "Time Synchronized",
        "Sync Failed - Using Internal Clock",
        "Updating Time..."
    };
    
    time_reliability_state_t state = g_time_manager.reliability_state;
    if (state >= 0 && state < (sizeof(status_strings) / sizeof(status_strings[0]))) {
        strncpy(buffer, status_strings[state], buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return ESP_OK;
    }
    
    strncpy(buffer, "Unknown State", buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return ESP_ERR_INVALID_STATE;
}

esp_err_t time_manager_get_statistics(time_manager_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(g_time_manager.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memcpy(stats, &g_time_manager.stats, sizeof(time_manager_stats_t));

    xSemaphoreGive(g_time_manager.mutex);
    return ESP_OK;
}

esp_err_t time_manager_reset_statistics(void)
{
    if (xSemaphoreTake(g_time_manager.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Reset statistics but preserve current source
    time_source_t current_source = g_time_manager.stats.current_source;
    memset(&g_time_manager.stats, 0, sizeof(time_manager_stats_t));
    g_time_manager.stats.current_source = current_source;

    // Reset NTP history
    g_time_manager.ntp_history_count = 0;
    g_time_manager.ntp_history_index = 0;

    esp_err_t err = time_manager_save_stats();

    xSemaphoreGive(g_time_manager.mutex);

    ESP_LOGI(TAG, "Statistics reset");
    return err;
}

void time_manager_print_status(void)
{
    time_status_t status;
    if (time_manager_get_status(&status) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get status");
        return;
    }

    ESP_LOGI(TAG, "=== TIME MANAGER STATUS ===");
    ESP_LOGI(TAG, "Status: %d", status.status);
    ESP_LOGI(TAG, "Current time: %ld", (long)status.current_time);
    ESP_LOGI(TAG, "Timezone: %s", status.timezone_info.name);
    ESP_LOGI(TAG, "WiFi connected: %s", status.wifi_connected ? "Yes" : "No");
    ESP_LOGI(TAG, "NTP available: %s", status.ntp_available ? "Yes" : "No");
    ESP_LOGI(TAG, "Sync attempts: %lu", (unsigned long)status.stats.total_sync_attempts);
    ESP_LOGI(TAG, "Successful syncs: %lu", (unsigned long)status.stats.successful_syncs);
    ESP_LOGI(TAG, "Failed syncs: %lu", (unsigned long)status.stats.failed_syncs);
    ESP_LOGI(TAG, "Manual time sets: %lu", (unsigned long)status.stats.manual_time_sets);
    ESP_LOGI(TAG, "Next sync in: %lu seconds", (unsigned long)status.next_sync_in_s);
    ESP_LOGI(TAG, "===========================");
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static esp_err_t time_manager_allocate_psram(void)
{
    // Allocate timezone database in PSRAM
    esp_err_t err = psram_manager_allocate_for_category(
        PSRAM_ALLOC_TIME_MGMT,
        sizeof(timezone_info_t) * COMMON_TIMEZONE_COUNT,
        (void**)&g_time_manager.timezone_db
    );

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PSRAM allocation failed for timezone DB, using RAM fallback");
        g_time_manager.timezone_db = malloc(sizeof(timezone_info_t) * COMMON_TIMEZONE_COUNT);
        if (g_time_manager.timezone_db == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    // Copy common timezones to allocated memory
    memcpy(g_time_manager.timezone_db, common_timezones, sizeof(common_timezones));

    // Allocate NTP history in PSRAM
    err = psram_manager_allocate_for_category(
        PSRAM_ALLOC_TIME_MGMT,
        sizeof(ntp_sync_record_t) * TIME_MANAGER_MAX_NTP_HISTORY,
        (void**)&g_time_manager.ntp_history
    );

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PSRAM allocation failed for NTP history, using RAM fallback");
        g_time_manager.ntp_history = malloc(sizeof(ntp_sync_record_t) * TIME_MANAGER_MAX_NTP_HISTORY);
        if (g_time_manager.ntp_history == NULL) {
            free(g_time_manager.timezone_db);
            return ESP_ERR_NO_MEM;
        }
    }

    // Initialize NTP history
    memset(g_time_manager.ntp_history, 0, sizeof(ntp_sync_record_t) * TIME_MANAGER_MAX_NTP_HISTORY);

    ESP_LOGI(TAG, "PSRAM allocated for time management (timezone DB + NTP history)");
    return ESP_OK;
}

static esp_err_t time_manager_load_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(TIME_MANAGER_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = sizeof(time_manager_config_t);
    err = nvs_get_blob(nvs_handle, TIME_MANAGER_NVS_CONFIG_KEY, &g_time_manager.config, &required_size);
    
    nvs_close(nvs_handle);
    return err;
}

static esp_err_t time_manager_save_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(TIME_MANAGER_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(nvs_handle, TIME_MANAGER_NVS_CONFIG_KEY, &g_time_manager.config, sizeof(time_manager_config_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    return err;
}

static esp_err_t time_manager_load_stats(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(TIME_MANAGER_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = sizeof(time_manager_stats_t);
    err = nvs_get_blob(nvs_handle, TIME_MANAGER_NVS_STATS_KEY, &g_time_manager.stats, &required_size);
    
    nvs_close(nvs_handle);
    return err;
}

static esp_err_t time_manager_save_stats(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(TIME_MANAGER_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(nvs_handle, TIME_MANAGER_NVS_STATS_KEY, &g_time_manager.stats, sizeof(time_manager_stats_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    return err;
}

static void time_manager_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Time manager task started");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t task_period = pdMS_TO_TICKS(1000); // 1 second
    
    while (true) {
        // Check for shutdown signal
        EventBits_t bits = xEventGroupWaitBits(
            g_time_manager.event_group,
            TIME_MANAGER_SHUTDOWN_BIT,
            pdFALSE, // Don't clear on exit
            pdFALSE, // Wait for any bit
            0        // No timeout
        );
        
        if (bits & TIME_MANAGER_SHUTDOWN_BIT) {
            ESP_LOGI(TAG, "Time manager task shutting down");
            break;
        }
        
        // Check WiFi connection status
        bool wifi_connected = time_manager_is_wifi_connected();
        if (wifi_connected != g_time_manager.wifi_connected) {
            g_time_manager.wifi_connected = wifi_connected;
            if (wifi_connected) {
                ESP_LOGI(TAG, "WiFi connected, NTP sync available");
                xEventGroupSetBits(g_time_manager.event_group, TIME_MANAGER_WIFI_CONNECTED_BIT);
            } else {
                ESP_LOGW(TAG, "WiFi disconnected, NTP sync unavailable");
                xEventGroupClearBits(g_time_manager.event_group, TIME_MANAGER_WIFI_CONNECTED_BIT);
            }
        }
        
        // Check for automatic NTP sync
        if (g_time_manager.config.auto_sync_enabled && wifi_connected) {
            uint64_t current_ms = esp_timer_get_time() / 1000ULL;
            
            if (g_time_manager.next_auto_sync_ms > 0 && current_ms >= g_time_manager.next_auto_sync_ms) {
                ESP_LOGI(TAG, "Automatic NTP sync triggered");
                
                esp_err_t err = time_manager_start_ntp_sync();
                if (err == ESP_OK) {
                    // Schedule next sync
                    g_time_manager.next_auto_sync_ms = current_ms + (g_time_manager.config.sync_interval_s * 1000ULL);
                } else {
                    ESP_LOGW(TAG, "Failed to start automatic NTP sync: %s", esp_err_to_name(err));
                    // Handle sync failure for reliability state
                    if (xSemaphoreTake(g_time_manager.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        g_time_manager.stats.failed_syncs++;
                        g_time_manager.consecutive_sync_failures++;
                        
                        // Update reliability state if we had good time before
                        if (g_time_manager.first_sync_achieved && g_time_manager.reliability_state == TIME_GOOD) {
                            g_time_manager.reliability_state = TIME_GOOD_SYNC_FAILED;
                            g_time_manager.time_uncertain_flag = true;
                            ESP_LOGW(TAG, "NTP sync failed - state: TIME_GOOD_SYNC_FAILED");
                        }
                        
                        xSemaphoreGive(g_time_manager.mutex);
                    }
                    
                    // Retry in 5 minutes
                    g_time_manager.next_auto_sync_ms = current_ms + (TIME_MANAGER_SYNC_RETRY_INTERVAL_S * 1000ULL);
                }
            }
        }
        
        // Check for NTP sync timeout
        if (g_time_manager.ntp_sync_in_progress) {
            uint64_t current_ms = esp_timer_get_time() / 1000ULL;
            uint64_t sync_duration_ms = current_ms - g_time_manager.ntp_sync_start_ms;
            
            if (sync_duration_ms > TIME_MANAGER_NTP_TIMEOUT_MS) {
                ESP_LOGW(TAG, "NTP sync timeout after %llu ms", sync_duration_ms);
                
                // Handle sync timeout
                if (xSemaphoreTake(g_time_manager.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    g_time_manager.stats.failed_syncs++;
                    g_time_manager.consecutive_sync_failures++;
                    g_time_manager.ntp_sync_in_progress = false;
                    g_time_manager.last_ntp_status = NTP_SYNC_STATUS_FAILED;
                    
                    // Update reliability state based on sync failure
                    if (g_time_manager.first_sync_achieved) {
                        // Had good time before, now sync failed
                        g_time_manager.reliability_state = TIME_GOOD_SYNC_FAILED;
                        g_time_manager.time_uncertain_flag = true;
                        ESP_LOGW(TAG, "NTP sync timeout - state: TIME_GOOD_SYNC_FAILED");
                    } else {
                        // First sync failed, back to not set
                        g_time_manager.reliability_state = TIME_NOT_SET;
                        g_time_manager.time_uncertain_flag = true;
                        ESP_LOGW(TAG, "First NTP sync timeout - state: TIME_NOT_SET");
                    }
                    
                    // Add failed sync to history
                    time_manager_add_ntp_history_record(NTP_SYNC_STATUS_FAILED, (uint32_t)sync_duration_ms, "timeout");
                    time_manager_save_stats();
                    
                    xSemaphoreGive(g_time_manager.mutex);
                }
                
                // Stop the failed sync
                time_manager_stop_ntp_sync();
            }
        }
        
        // Task delay
        vTaskDelayUntil(&last_wake_time, task_period);
    }
    
    ESP_LOGI(TAG, "Time manager task ended");
    vTaskDelete(NULL);
}

static void time_manager_sntp_sync_notification_cb(struct timeval *tv)
{
    if (tv == NULL) {
        return;
    }
    
    uint64_t sync_end_ms = esp_timer_get_time() / 1000ULL;
    uint32_t sync_duration = (uint32_t)(sync_end_ms - g_time_manager.ntp_sync_start_ms);
    
    ESP_LOGI(TAG, "NTP sync completed in %lu ms", (unsigned long)sync_duration);
    
    // Update statistics and reliability state
    if (xSemaphoreTake(g_time_manager.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_time_manager.stats.successful_syncs++;
        g_time_manager.stats.last_sync_time = tv->tv_sec;
        g_time_manager.stats.last_sync_timestamp = sync_end_ms;
        g_time_manager.stats.current_source = TIME_SOURCE_NTP;
        g_time_manager.status = TIME_MANAGER_NTP_SYNCED;
        g_time_manager.ntp_sync_in_progress = false;
        g_time_manager.last_ntp_status = NTP_SYNC_STATUS_COMPLETED;
        
        // Update five-state reliability tracking
        g_time_manager.last_successful_sync = tv->tv_sec;
        g_time_manager.consecutive_sync_failures = 0;
        g_time_manager.time_uncertain_flag = false; // Time is now certain
        
        // Check if this is the first successful sync
        if (!g_time_manager.first_sync_achieved) {
            g_time_manager.first_sync_achieved = true;
            g_time_manager.reliability_state = TIME_GOOD;
            ESP_LOGI(TAG, "First NTP sync achieved - time is now reliable");
        } else {
            // Subsequent successful sync
            g_time_manager.reliability_state = TIME_GOOD;
            ESP_LOGI(TAG, "NTP sync successful - time reliability maintained");
        }
        
        // Add to history
        time_manager_add_ntp_history_record(NTP_SYNC_STATUS_COMPLETED, sync_duration, "ntp_server");
        
        // Save statistics
        time_manager_save_stats();
        
        xSemaphoreGive(g_time_manager.mutex);
    }
    
    // Signal completion
    xEventGroupSetBits(g_time_manager.event_group, TIME_MANAGER_NTP_SYNC_DONE_BIT);
}

static void time_manager_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi connected");
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "WiFi disconnected");
                xEventGroupClearBits(g_time_manager.event_group, TIME_MANAGER_WIFI_CONNECTED_BIT);
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP address, WiFi fully connected");
        xEventGroupSetBits(g_time_manager.event_group, TIME_MANAGER_WIFI_CONNECTED_BIT);
        
        // ALWAYS trigger immediate NTP sync when WiFi connects (regardless of auto_sync setting)
        // This ensures we get proper time as soon as possible
        ESP_LOGI(TAG, "WiFi connected - forcing immediate NTP sync");
        uint64_t current_ms = esp_timer_get_time() / 1000ULL;
        g_time_manager.next_auto_sync_ms = current_ms + 2000; // 2 seconds delay for WiFi to stabilize
        
        // Also enable auto sync if it wasn't already enabled
        if (!g_time_manager.config.auto_sync_enabled) {
            ESP_LOGI(TAG, "Enabling auto sync due to WiFi connection");
            g_time_manager.config.auto_sync_enabled = true;
            time_manager_save_config();
        }
    }
}

static esp_err_t time_manager_start_ntp_sync(void)
{
    if (g_time_manager.ntp_sync_in_progress) {
        ESP_LOGW(TAG, "NTP sync already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Initialize SNTP if not already done
    if (!g_time_manager.ntp_initialized) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        
        // Set NTP servers
        for (uint8_t i = 0; i < g_time_manager.config.ntp_server_count; i++) {
            esp_sntp_setservername(i, g_time_manager.config.ntp_servers[i]);
        }
        
        // Set sync notification callback
        esp_sntp_set_time_sync_notification_cb(time_manager_sntp_sync_notification_cb);
        
        g_time_manager.ntp_initialized = true;
    }
    
    // Start SNTP
    esp_sntp_init();
    
    // Update state and reliability tracking
    g_time_manager.ntp_sync_in_progress = true;
    g_time_manager.ntp_sync_start_ms = esp_timer_get_time() / 1000ULL;
    g_time_manager.last_sync_attempt_ms = g_time_manager.ntp_sync_start_ms;
    g_time_manager.status = TIME_MANAGER_NTP_SYNCING;
    
    // Update five-state reliability tracking
    if (xSemaphoreTake(g_time_manager.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_time_manager.stats.total_sync_attempts++;
        
        // Update reliability state based on current state
        if (!g_time_manager.first_sync_achieved) {
            // First sync attempt
            g_time_manager.reliability_state = TIME_SYNCING;
            ESP_LOGI(TAG, "First NTP sync attempt - state: TIME_SYNCING");
        } else {
            // Subsequent sync from good state
            g_time_manager.reliability_state = TIME_UPDATING;
            ESP_LOGI(TAG, "NTP sync update attempt - state: TIME_UPDATING");
        }
        
        xSemaphoreGive(g_time_manager.mutex);
    }
    
    ESP_LOGI(TAG, "NTP sync started");
    return ESP_OK;
}

static esp_err_t time_manager_stop_ntp_sync(void)
{
    if (g_time_manager.ntp_initialized) {
        esp_sntp_stop();
        g_time_manager.ntp_initialized = false;
        g_time_manager.ntp_sync_in_progress = false;
        ESP_LOGI(TAG, "NTP sync stopped");
    }
    
    return ESP_OK;
}

static void time_manager_add_ntp_history_record(ntp_sync_status_t status, uint32_t duration_ms, const char *server)
{
    if (g_time_manager.ntp_history == NULL) {
        return;
    }
    
    ntp_sync_record_t *record = &g_time_manager.ntp_history[g_time_manager.ntp_history_index];
    
    record->timestamp_ms = esp_timer_get_time() / 1000ULL;
    time(&record->sync_time);
    record->status = status;
    record->sync_duration_ms = duration_ms;
    strncpy(record->server_used, server ? server : "unknown", sizeof(record->server_used) - 1);
    record->server_used[sizeof(record->server_used) - 1] = '\0';
    
    // Update index and count
    g_time_manager.ntp_history_index = (g_time_manager.ntp_history_index + 1) % TIME_MANAGER_MAX_NTP_HISTORY;
    if (g_time_manager.ntp_history_count < TIME_MANAGER_MAX_NTP_HISTORY) {
        g_time_manager.ntp_history_count++;
    }
}

static esp_err_t time_manager_update_timezone_info(timezone_info_t *tz_info)
{
    if (tz_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get current timezone information
    time_t now;
    time(&now);
    struct tm local_tm, utc_tm;
    localtime_r(&now, &local_tm);
    gmtime_r(&now, &utc_tm);
    
    // Calculate UTC offset by comparing local and UTC time
    time_t local_time = mktime(&local_tm);
    time_t utc_time = mktime(&utc_tm);
    int32_t utc_offset_seconds = (int32_t)(local_time - utc_time);
    
    // Find matching timezone in database
    for (uint32_t i = 0; i < COMMON_TIMEZONE_COUNT; i++) {
        if (strstr(g_time_manager.config.timezone, common_timezones[i].name) != NULL) {
            memcpy(tz_info, &common_timezones[i], sizeof(timezone_info_t));
            
            // Update current offset and DST status
            tz_info->utc_offset_seconds = utc_offset_seconds;
            tz_info->dst_active = (local_tm.tm_isdst > 0);
            
            return ESP_OK;
        }
    }
    
    // Default timezone info if not found
    strncpy(tz_info->name, "Unknown", sizeof(tz_info->name) - 1);
    strncpy(tz_info->posix_tz, g_time_manager.config.timezone, sizeof(tz_info->posix_tz) - 1);
    strncpy(tz_info->description, "Custom timezone", sizeof(tz_info->description) - 1);
    tz_info->utc_offset_seconds = utc_offset_seconds;
    tz_info->dst_active = (local_tm.tm_isdst > 0);
    
    return ESP_OK;
}

static bool time_manager_is_wifi_connected(void)
{
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}
