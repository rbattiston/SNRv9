/**
 * @file time_manager.h
 * @brief Time Management System for SNRv9 Irrigation Control System
 * 
 * This component provides comprehensive time management capabilities including:
 * - ESP-IDF SNTP integration with native esp_sntp component
 * - NTP-only time source with five-state reliability tracking
 * - Full POSIX timezone support with automatic DST handling
 * - Thread-safe operations with FreeRTOS mutex protection
 * - NVS persistence for all settings
 * - WiFi event integration for automatic sync attempts
 * - Time uncertainty flags for data collection during sync failures
 */

#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include "esp_err.h"
#include "esp_sntp.h"
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * CONSTANTS AND CONFIGURATION
 * =============================================================================
 */

#define TIME_MANAGER_MAX_NTP_SERVERS        3
#define TIME_MANAGER_MAX_TIMEZONE_LEN       64
#define TIME_MANAGER_MAX_NTP_HISTORY        50
#define TIME_MANAGER_NTP_TIMEOUT_MS         10000
#define TIME_MANAGER_SYNC_RETRY_INTERVAL_S  300    // 5 minutes
#define TIME_MANAGER_MAX_SYNC_RETRIES       5

/* =============================================================================
 * TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief Time reliability state enumeration
 */
typedef enum {
    TIME_NOT_SET = 0,           ///< No NTP sync yet, time unreliable
    TIME_SYNCING = 1,           ///< First sync in progress, time unreliable
    TIME_GOOD = 2,              ///< Time reliable, syncs successful
    TIME_GOOD_SYNC_FAILED = 3,  ///< Time reliable but recent sync failed
    TIME_UPDATING = 4           ///< Sync in progress from good state
} time_reliability_state_t;

/**
 * @brief Time manager status enumeration (legacy compatibility)
 */
typedef enum {
    TIME_MANAGER_NOT_INITIALIZED = 0,
    TIME_MANAGER_INITIALIZED,
    TIME_MANAGER_NTP_SYNCING,
    TIME_MANAGER_NTP_SYNCED,
    TIME_MANAGER_ERROR
} time_manager_status_t;

/**
 * @brief NTP synchronization status
 */
typedef enum {
    NTP_SYNC_STATUS_RESET = 0,
    NTP_SYNC_STATUS_COMPLETED,
    NTP_SYNC_STATUS_IN_PROGRESS,
    NTP_SYNC_STATUS_FAILED
} ntp_sync_status_t;

/**
 * @brief Time source enumeration
 */
typedef enum {
    TIME_SOURCE_NONE = 0,
    TIME_SOURCE_NTP,
    TIME_SOURCE_MANUAL,
    TIME_SOURCE_RTC
} time_source_t;

/**
 * @brief NTP synchronization record
 */
typedef struct {
    uint64_t timestamp_ms;          ///< When sync occurred (milliseconds since boot)
    time_t sync_time;               ///< Synchronized time (Unix timestamp)
    ntp_sync_status_t status;       ///< Sync status
    uint32_t sync_duration_ms;      ///< How long sync took
    char server_used[64];           ///< Which NTP server was used
} ntp_sync_record_t;

/**
 * @brief Timezone information structure
 */
typedef struct {
    char name[32];                  ///< Timezone name (e.g., "America/New_York")
    char posix_tz[64];             ///< POSIX timezone string
    char description[128];          ///< Human-readable description
    int utc_offset_seconds;         ///< Current UTC offset in seconds
    bool dst_active;                ///< Whether DST is currently active
} timezone_info_t;

/**
 * @brief Time manager configuration
 */
typedef struct {
    char ntp_servers[TIME_MANAGER_MAX_NTP_SERVERS][64];  ///< NTP server URLs
    uint8_t ntp_server_count;                            ///< Number of configured servers
    char timezone[TIME_MANAGER_MAX_TIMEZONE_LEN];        ///< Current timezone
    bool auto_sync_enabled;                              ///< Enable automatic NTP sync
    uint32_t sync_interval_s;                            ///< Sync interval in seconds
} time_manager_config_t;

/**
 * @brief Time manager statistics
 */
typedef struct {
    uint32_t total_sync_attempts;    ///< Total NTP sync attempts
    uint32_t successful_syncs;       ///< Successful NTP syncs
    uint32_t failed_syncs;          ///< Failed NTP syncs
    uint32_t manual_time_sets;      ///< Manual time setting count
    time_t last_sync_time;          ///< Last successful sync time
    uint64_t last_sync_timestamp;   ///< Last sync timestamp (ms since boot)
    time_source_t current_source;   ///< Current time source
    uint32_t uptime_at_last_sync;   ///< System uptime at last sync
} time_manager_stats_t;

/**
 * @brief Comprehensive time status
 */
typedef struct {
    time_manager_status_t status;    ///< Current manager status
    time_t current_time;            ///< Current system time
    timezone_info_t timezone_info;  ///< Current timezone information
    time_manager_stats_t stats;     ///< Statistics
    bool ntp_available;             ///< NTP service availability
    bool wifi_connected;            ///< WiFi connection status
    uint32_t next_sync_in_s;        ///< Seconds until next auto sync
} time_status_t;

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the time manager system
 * 
 * This function initializes the time management system including:
 * - PSRAM allocation for timezone database and NTP history
 * - NVS storage initialization for persistent settings
 * - SNTP client configuration
 * - WiFi event handler registration
 * - Time manager task creation
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_init(void);

/**
 * @brief Deinitialize the time manager system
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_deinit(void);

/**
 * @brief Get current time manager status
 * 
 * @param[out] status Pointer to status structure to fill
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_get_status(time_status_t *status);

/**
 * @brief Configure NTP servers
 * 
 * @param[in] servers Array of NTP server URLs
 * @param[in] count Number of servers (max TIME_MANAGER_MAX_NTP_SERVERS)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_set_ntp_servers(const char servers[][64], uint8_t count);

/**
 * @brief Set timezone
 * 
 * @param[in] timezone POSIX timezone string (e.g., "EST5EDT,M3.2.0,M11.1.0")
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_set_timezone(const char *timezone);

/**
 * @brief Force NTP synchronization
 * 
 * @param[in] timeout_ms Timeout in milliseconds (0 for default)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_force_ntp_sync(uint32_t timeout_ms);


/**
 * @brief Enable or disable automatic NTP synchronization
 * 
 * @param[in] enabled True to enable, false to disable
 * @param[in] interval_s Sync interval in seconds (0 for default)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_set_auto_sync(bool enabled, uint32_t interval_s);

/**
 * @brief Get current time with timezone information
 * 
 * @param[out] current_time Pointer to time_t to fill with current time
 * @param[out] tz_info Pointer to timezone info (can be NULL)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_get_current_time(time_t *current_time, timezone_info_t *tz_info);

/**
 * @brief Get formatted time string
 * 
 * @param[out] buffer Buffer to write formatted time
 * @param[in] buffer_size Size of buffer
 * @param[in] format strftime format string (NULL for default ISO 8601)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_get_formatted_time(char *buffer, size_t buffer_size, const char *format);

/**
 * @brief Get NTP synchronization history
 * 
 * @param[out] history Array to fill with sync records
 * @param[in] max_records Maximum number of records to return
 * @param[out] actual_count Actual number of records returned
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_get_ntp_history(ntp_sync_record_t *history, uint32_t max_records, uint32_t *actual_count);

/**
 * @brief Get list of common timezone configurations
 * 
 * @param[out] timezones Array to fill with timezone info
 * @param[in] max_timezones Maximum number of timezones to return
 * @param[out] actual_count Actual number of timezones returned
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_get_timezone_list(timezone_info_t *timezones, uint32_t max_timezones, uint32_t *actual_count);

/**
 * @brief Get current time reliability state
 * 
 * @return Current time reliability state
 */
time_reliability_state_t time_manager_get_reliability_state(void);

/**
 * @brief Check if time is synchronized and reliable
 * 
 * This function returns true for TIME_GOOD and TIME_GOOD_SYNC_FAILED states.
 * Time is considered reliable once first NTP sync succeeds, even if subsequent
 * syncs fail, until power cycle.
 * 
 * @return true if time is synchronized and reliable, false otherwise
 */
bool time_manager_is_time_reliable(void);

/**
 * @brief Get time uncertainty flag for data collection
 * 
 * @return true if time is uncertain (TIME_GOOD_SYNC_FAILED), false otherwise
 */
bool time_manager_get_time_uncertainty_flag(void);

/**
 * @brief Get time reliability status string
 * 
 * @param[out] buffer Buffer to write status string
 * @param[in] buffer_size Size of buffer
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_get_reliability_status_string(char *buffer, size_t buffer_size);

/**
 * @brief Get time manager statistics
 * 
 * @param[out] stats Pointer to statistics structure to fill
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_get_statistics(time_manager_stats_t *stats);

/**
 * @brief Reset time manager statistics
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t time_manager_reset_statistics(void);

/**
 * @brief Print time manager status to console
 */
void time_manager_print_status(void);

#ifdef __cplusplus
}
#endif

#endif // TIME_MANAGER_H
