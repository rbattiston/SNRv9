/**
 * @file wifi_handler.c
 * @brief WiFi connection management implementation for SNRv9 Irrigation Control System
 * 
 * This module implements WiFi station mode connectivity with auto-reconnection,
 * status monitoring, and integration with the existing monitoring infrastructure.
 */

#include "wifi_handler.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * CONSTANTS AND CONFIGURATION
 * =============================================================================
 */

static const char *TAG = "wifi_handler";

// WiFi Configuration
#define WIFI_SSID               "Secure Office"
#define WIFI_PASSWORD           "Hoyt1000!"
#define WIFI_MAXIMUM_RETRY      5
#define WIFI_RECONNECT_DELAY_MS 5000
#define WIFI_TASK_STACK_SIZE    6144  // Increased from 4096
#define WIFI_TASK_PRIORITY      1

// Event bits for WiFi events
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT          BIT1

// Reporting intervals (configurable via debug_config.h)
#if DEBUG_WIFI_MONITORING
#define WIFI_REPORT_INTERVAL_MS     DEBUG_WIFI_REPORT_INTERVAL_MS
#define WIFI_STATUS_CHECK_INTERVAL_MS 5000
#else
#define WIFI_REPORT_INTERVAL_MS     60000  // 1 minute default
#define WIFI_STATUS_CHECK_INTERVAL_MS 10000 // 10 seconds default
#endif

/* =============================================================================
 * PRIVATE DATA STRUCTURES
 * =============================================================================
 */

/**
 * @brief WiFi handler internal state
 */
typedef struct {
    wifi_handler_status_t handler_status;
    wifi_stats_t stats;
    bool enabled;
    bool auto_reconnect;
    uint32_t retry_count;
    uint32_t connection_start_time;
    TaskHandle_t wifi_task_handle;
    SemaphoreHandle_t stats_mutex;
    EventGroupHandle_t wifi_event_group;
    esp_netif_t *sta_netif;
} wifi_handler_state_t;

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static wifi_handler_state_t g_wifi_state = {
    .handler_status = WIFI_HANDLER_STOPPED,
    .stats = {0},
    .enabled = true,
    .auto_reconnect = true,
    .retry_count = 0,
    .connection_start_time = 0,
    .wifi_task_handle = NULL,
    .stats_mutex = NULL,
    .wifi_event_group = NULL,
    .sta_netif = NULL
};

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void wifi_monitoring_task(void *pvParameters);
static void wifi_update_status(wifi_status_t new_status);
static void wifi_increment_stat(uint32_t *stat);
static uint32_t wifi_get_current_time_sec(void);
static bool wifi_init_netif(void);
static bool wifi_init_config(void);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool wifi_handler_init(void)
{
    esp_err_t ret;

    // Initialize NVS (required for WiFi)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create mutex for thread-safe statistics access
    g_wifi_state.stats_mutex = xSemaphoreCreateMutex();
    if (g_wifi_state.stats_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create stats mutex");
        return false;
    }

    // Create event group for WiFi events
    g_wifi_state.wifi_event_group = xEventGroupCreate();
    if (g_wifi_state.wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        vSemaphoreDelete(g_wifi_state.stats_mutex);
        return false;
    }

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize network interface
    if (!wifi_init_netif()) {
        ESP_LOGE(TAG, "Failed to initialize network interface");
        return false;
    }

    // Initialize WiFi configuration
    if (!wifi_init_config()) {
        ESP_LOGE(TAG, "Failed to initialize WiFi configuration");
        return false;
    }

    // Initialize statistics
    memset(&g_wifi_state.stats, 0, sizeof(wifi_stats_t));
    g_wifi_state.stats.current_status = WIFI_STATUS_DISCONNECTED;

    g_wifi_state.handler_status = WIFI_HANDLER_STOPPED;

#if DEBUG_WIFI_MONITORING && DEBUG_INCLUDE_TIMESTAMPS
    ESP_LOGI(TAG, "[%lu] WiFi handler initialized successfully", wifi_get_current_time_sec());
#elif DEBUG_WIFI_MONITORING
    ESP_LOGI(TAG, "WiFi handler initialized successfully");
#endif

    return true;
}

bool wifi_handler_start(void)
{
    if (g_wifi_state.handler_status == WIFI_HANDLER_RUNNING) {
        ESP_LOGW(TAG, "WiFi handler already running");
        return true;
    }

    if (!g_wifi_state.enabled) {
        ESP_LOGW(TAG, "WiFi handler is disabled");
        return false;
    }

    // Start WiFi
    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return false;
    }

    // Create WiFi monitoring task
    BaseType_t task_created = xTaskCreate(
        wifi_monitoring_task,
        "wifi_monitor",
        WIFI_TASK_STACK_SIZE,
        NULL,
        WIFI_TASK_PRIORITY,
        &g_wifi_state.wifi_task_handle
    );

    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi monitoring task");
        esp_wifi_stop();
        return false;
    }

    g_wifi_state.handler_status = WIFI_HANDLER_RUNNING;

#if DEBUG_WIFI_MONITORING && DEBUG_INCLUDE_TIMESTAMPS
    ESP_LOGI(TAG, "[%lu] WiFi handler started successfully", wifi_get_current_time_sec());
#elif DEBUG_WIFI_MONITORING
    ESP_LOGI(TAG, "WiFi handler started successfully");
#endif

    return true;
}

bool wifi_handler_stop(void)
{
    if (g_wifi_state.handler_status == WIFI_HANDLER_STOPPED) {
        ESP_LOGW(TAG, "WiFi handler already stopped");
        return true;
    }

    // Stop WiFi monitoring task
    if (g_wifi_state.wifi_task_handle != NULL) {
        vTaskDelete(g_wifi_state.wifi_task_handle);
        g_wifi_state.wifi_task_handle = NULL;
    }

    // Stop WiFi
    esp_wifi_stop();

    // Update status
    wifi_update_status(WIFI_STATUS_DISABLED);
    g_wifi_state.handler_status = WIFI_HANDLER_STOPPED;

#if DEBUG_WIFI_MONITORING && DEBUG_INCLUDE_TIMESTAMPS
    ESP_LOGI(TAG, "[%lu] WiFi handler stopped", wifi_get_current_time_sec());
#elif DEBUG_WIFI_MONITORING
    ESP_LOGI(TAG, "WiFi handler stopped");
#endif

    return true;
}

wifi_handler_status_t wifi_handler_get_status(void)
{
    return g_wifi_state.handler_status;
}

wifi_status_t wifi_handler_get_wifi_status(void)
{
    wifi_status_t status;
    
    if (xSemaphoreTake(g_wifi_state.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        status = g_wifi_state.stats.current_status;
        xSemaphoreGive(g_wifi_state.stats_mutex);
    } else {
        status = WIFI_STATUS_ERROR;
    }
    
    return status;
}

bool wifi_handler_get_stats(wifi_stats_t *stats)
{
    if (stats == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_wifi_state.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &g_wifi_state.stats, sizeof(wifi_stats_t));
        xSemaphoreGive(g_wifi_state.stats_mutex);
        return true;
    }

    return false;
}

bool wifi_handler_force_connect(void)
{
    if (g_wifi_state.handler_status != WIFI_HANDLER_RUNNING) {
        return false;
    }

    esp_err_t ret = esp_wifi_connect();
    if (ret == ESP_OK) {
        wifi_update_status(WIFI_STATUS_CONNECTING);
        wifi_increment_stat(&g_wifi_state.stats.connection_attempts);
        g_wifi_state.connection_start_time = wifi_get_current_time_sec();
        return true;
    }

    return false;
}

bool wifi_handler_force_disconnect(void)
{
    if (g_wifi_state.handler_status != WIFI_HANDLER_RUNNING) {
        return false;
    }

    g_wifi_state.auto_reconnect = false;
    esp_err_t ret = esp_wifi_disconnect();
    return (ret == ESP_OK);
}

bool wifi_handler_is_connected(void)
{
    return (wifi_handler_get_wifi_status() == WIFI_STATUS_CONNECTED);
}

bool wifi_handler_get_ip_address(char *ip_str, size_t max_len)
{
    if (ip_str == NULL || max_len < 16) {
        return false;
    }

    if (!wifi_handler_is_connected()) {
        strncpy(ip_str, "0.0.0.0", max_len - 1);
        ip_str[max_len - 1] = '\0';
        return false;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(g_wifi_state.sta_netif, &ip_info);
    if (ret == ESP_OK) {
        snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
        return true;
    }

    strncpy(ip_str, "0.0.0.0", max_len - 1);
    ip_str[max_len - 1] = '\0';
    return false;
}

bool wifi_handler_get_mac_address(char *mac_str, size_t max_len)
{
    if (mac_str == NULL || max_len < 18) {
        return false;
    }

    uint8_t mac[6];
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (ret == ESP_OK) {
        snprintf(mac_str, max_len, "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return true;
    }

    strncpy(mac_str, "00:00:00:00:00:00", max_len - 1);
    mac_str[max_len - 1] = '\0';
    return false;
}

void wifi_handler_reset_stats(void)
{
    if (xSemaphoreTake(g_wifi_state.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        wifi_status_t current_status = g_wifi_state.stats.current_status;
        int8_t current_rssi = g_wifi_state.stats.signal_strength_rssi;
        
        memset(&g_wifi_state.stats, 0, sizeof(wifi_stats_t));
        g_wifi_state.stats.current_status = current_status;
        g_wifi_state.stats.signal_strength_rssi = current_rssi;
        
        xSemaphoreGive(g_wifi_state.stats_mutex);
    }
}

void wifi_handler_set_enabled(bool enable)
{
    g_wifi_state.enabled = enable;
    
    if (!enable && g_wifi_state.handler_status == WIFI_HANDLER_RUNNING) {
        wifi_handler_stop();
    }
}

bool wifi_handler_is_enabled(void)
{
    return g_wifi_state.enabled;
}

void wifi_handler_print_detailed_report(void)
{
#if DEBUG_WIFI_MONITORING
    wifi_stats_t stats;
    char ip_str[16];
    char mac_str[18];
    
    if (!wifi_handler_get_stats(&stats)) {
        ESP_LOGW(TAG, "Failed to get WiFi statistics");
        return;
    }
    
    wifi_handler_get_ip_address(ip_str, sizeof(ip_str));
    wifi_handler_get_mac_address(mac_str, sizeof(mac_str));
    
    const char* status_str;
    switch (stats.current_status) {
        case WIFI_STATUS_DISCONNECTED: status_str = "DISCONNECTED"; break;
        case WIFI_STATUS_CONNECTING: status_str = "CONNECTING"; break;
        case WIFI_STATUS_CONNECTED: status_str = "CONNECTED"; break;
        case WIFI_STATUS_RECONNECTING: status_str = "RECONNECTING"; break;
        case WIFI_STATUS_ERROR: status_str = "ERROR"; break;
        case WIFI_STATUS_DISABLED: status_str = "DISABLED"; break;
        default: status_str = "UNKNOWN"; break;
    }

#if DEBUG_INCLUDE_TIMESTAMPS
    ESP_LOGI(TAG, "[%lu] === WiFi Detailed Report ===", wifi_get_current_time_sec());
#else
    ESP_LOGI(TAG, "=== WiFi Detailed Report ===");
#endif
    ESP_LOGI(TAG, "Status: %s", status_str);
    ESP_LOGI(TAG, "SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "IP Address: %s", ip_str);
    ESP_LOGI(TAG, "MAC Address: %s", mac_str);
    ESP_LOGI(TAG, "Signal Strength: %d dBm", stats.signal_strength_rssi);
    ESP_LOGI(TAG, "Connection Attempts: %lu", stats.connection_attempts);
    ESP_LOGI(TAG, "Successful Connections: %lu", stats.successful_connections);
    ESP_LOGI(TAG, "Disconnections: %lu", stats.disconnection_count);
    ESP_LOGI(TAG, "Reconnection Attempts: %lu", stats.reconnection_attempts);
    ESP_LOGI(TAG, "Total Connected Time: %lu seconds", stats.total_connected_time);
    ESP_LOGI(TAG, "Handler Status: %s", 
             g_wifi_state.handler_status == WIFI_HANDLER_RUNNING ? "RUNNING" :
             g_wifi_state.handler_status == WIFI_HANDLER_STOPPED ? "STOPPED" : "ERROR");
    ESP_LOGI(TAG, "Auto-reconnect: %s", g_wifi_state.auto_reconnect ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "=== End WiFi Report ===");
#endif
}

void wifi_handler_print_summary(void)
{
#if DEBUG_WIFI_MONITORING
    wifi_stats_t stats;
    char ip_str[16];
    
    if (!wifi_handler_get_stats(&stats)) {
        ESP_LOGW(TAG, "Failed to get WiFi statistics for summary");
        return;
    }
    
    wifi_handler_get_ip_address(ip_str, sizeof(ip_str));
    
    const char* status_str;
    switch (stats.current_status) {
        case WIFI_STATUS_CONNECTED: status_str = "CONNECTED"; break;
        case WIFI_STATUS_CONNECTING: status_str = "CONNECTING"; break;
        case WIFI_STATUS_RECONNECTING: status_str = "RECONNECTING"; break;
        case WIFI_STATUS_DISCONNECTED: status_str = "DISCONNECTED"; break;
        case WIFI_STATUS_ERROR: status_str = "ERROR"; break;
        case WIFI_STATUS_DISABLED: status_str = "DISABLED"; break;
        default: status_str = "UNKNOWN"; break;
    }

#if DEBUG_INCLUDE_TIMESTAMPS
    ESP_LOGI(TAG, "[%lu] WiFi: %s | IP: %s | RSSI: %d dBm | Connections: %lu/%lu", 
             wifi_get_current_time_sec(), status_str, ip_str, stats.signal_strength_rssi,
             stats.successful_connections, stats.connection_attempts);
#else
    ESP_LOGI(TAG, "WiFi: %s | IP: %s | RSSI: %d dBm | Connections: %lu/%lu", 
             status_str, ip_str, stats.signal_strength_rssi,
             stats.successful_connections, stats.connection_attempts);
#endif
#endif
}

void wifi_handler_force_report(void)
{
    wifi_handler_print_detailed_report();
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        wifi_update_status(WIFI_STATUS_CONNECTING);
        wifi_increment_stat(&g_wifi_state.stats.connection_attempts);
        g_wifi_state.connection_start_time = wifi_get_current_time_sec();
        
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        
        wifi_increment_stat(&g_wifi_state.stats.disconnection_count);
        
        if (g_wifi_state.auto_reconnect && g_wifi_state.retry_count < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            g_wifi_state.retry_count++;
            wifi_increment_stat(&g_wifi_state.stats.reconnection_attempts);
            wifi_update_status(WIFI_STATUS_RECONNECTING);
            
#if DEBUG_WIFI_MONITORING && DEBUG_INCLUDE_TIMESTAMPS
            ESP_LOGI(TAG, "[%lu] WiFi disconnected (reason: %d), retry attempt %lu/%d", 
                     wifi_get_current_time_sec(), event->reason, g_wifi_state.retry_count, WIFI_MAXIMUM_RETRY);
#elif DEBUG_WIFI_MONITORING
            ESP_LOGI(TAG, "WiFi disconnected (reason: %d), retry attempt %lu/%d", 
                     event->reason, g_wifi_state.retry_count, WIFI_MAXIMUM_RETRY);
#endif
        } else {
            xEventGroupSetBits(g_wifi_state.wifi_event_group, WIFI_FAIL_BIT);
            wifi_update_status(WIFI_STATUS_ERROR);
            
#if DEBUG_WIFI_MONITORING && DEBUG_INCLUDE_TIMESTAMPS
            ESP_LOGE(TAG, "[%lu] WiFi connection failed after %d retries", 
                     wifi_get_current_time_sec(), WIFI_MAXIMUM_RETRY);
#elif DEBUG_WIFI_MONITORING
            ESP_LOGE(TAG, "WiFi connection failed after %d retries", WIFI_MAXIMUM_RETRY);
#endif
        }
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        g_wifi_state.retry_count = 0;
        wifi_increment_stat(&g_wifi_state.stats.successful_connections);
        
        if (xSemaphoreTake(g_wifi_state.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_wifi_state.stats.last_connection_time = wifi_get_current_time_sec();
            xSemaphoreGive(g_wifi_state.stats_mutex);
        }
        
        wifi_update_status(WIFI_STATUS_CONNECTED);
        xEventGroupSetBits(g_wifi_state.wifi_event_group, WIFI_CONNECTED_BIT);
        
#if DEBUG_WIFI_MONITORING && DEBUG_INCLUDE_TIMESTAMPS
        ESP_LOGI(TAG, "[%lu] WiFi connected successfully, IP: " IPSTR, 
                 wifi_get_current_time_sec(), IP2STR(&event->ip_info.ip));
#elif DEBUG_WIFI_MONITORING
        ESP_LOGI(TAG, "WiFi connected successfully, IP: " IPSTR, IP2STR(&event->ip_info.ip));
#endif
    }
}

static void wifi_monitoring_task(void *pvParameters)
{
    TickType_t last_report_time = xTaskGetTickCount();
    TickType_t last_status_check = xTaskGetTickCount();
    uint32_t last_connected_time = 0;
    
    while (1) {
        TickType_t current_time = xTaskGetTickCount();
        
        // Update connection time if connected
        if (wifi_handler_is_connected()) {
            uint32_t current_sec = wifi_get_current_time_sec();
            if (last_connected_time > 0) {
                uint32_t elapsed = current_sec - last_connected_time;
                if (xSemaphoreTake(g_wifi_state.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    g_wifi_state.stats.total_connected_time += elapsed;
                    xSemaphoreGive(g_wifi_state.stats_mutex);
                }
            }
            last_connected_time = current_sec;
        } else {
            last_connected_time = 0;
        }
        
        // Update signal strength if connected
        if ((current_time - last_status_check) >= pdMS_TO_TICKS(WIFI_STATUS_CHECK_INTERVAL_MS)) {
            if (wifi_handler_is_connected()) {
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    if (xSemaphoreTake(g_wifi_state.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        g_wifi_state.stats.signal_strength_rssi = ap_info.rssi;
                        xSemaphoreGive(g_wifi_state.stats_mutex);
                    }
                }
            }
            last_status_check = current_time;
        }
        
        // Periodic reporting
        if ((current_time - last_report_time) >= pdMS_TO_TICKS(WIFI_REPORT_INTERVAL_MS)) {
            wifi_handler_print_summary();
            last_report_time = current_time;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every second
    }
}

static void wifi_update_status(wifi_status_t new_status)
{
    if (xSemaphoreTake(g_wifi_state.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_wifi_state.stats.current_status = new_status;
        xSemaphoreGive(g_wifi_state.stats_mutex);
    }
}

static void wifi_increment_stat(uint32_t *stat)
{
    if (xSemaphoreTake(g_wifi_state.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        (*stat)++;
        xSemaphoreGive(g_wifi_state.stats_mutex);
    }
}

static uint32_t wifi_get_current_time_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

static bool wifi_init_netif(void)
{
    g_wifi_state.sta_netif = esp_netif_create_default_wifi_sta();
    if (g_wifi_state.sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default WiFi STA interface");
        return false;
    }
    return true;
}

static bool wifi_init_config(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        return false;
    }

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    // Configure WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    return true;
}
