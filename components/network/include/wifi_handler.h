/**
 * @file wifi_handler.h
 * @brief WiFi connection management interface for SNRv9 Irrigation Control System
 * 
 * This module provides WiFi station mode connectivity with auto-reconnection,
 * status monitoring, and integration with the existing monitoring infrastructure.
 */

#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "debug_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief WiFi connection status enumeration
 */
typedef enum {
    WIFI_STATUS_DISCONNECTED = 0,  /**< WiFi is disconnected */
    WIFI_STATUS_CONNECTING,        /**< WiFi is attempting to connect */
    WIFI_STATUS_CONNECTED,         /**< WiFi is connected */
    WIFI_STATUS_RECONNECTING,      /**< WiFi is attempting to reconnect */
    WIFI_STATUS_ERROR,             /**< WiFi is in error state */
    WIFI_STATUS_DISABLED           /**< WiFi is disabled */
} wifi_status_t;

/**
 * @brief WiFi connection statistics
 */
typedef struct {
    uint32_t connection_attempts;      /**< Total connection attempts */
    uint32_t successful_connections;   /**< Successful connections */
    uint32_t disconnection_count;      /**< Number of disconnections */
    uint32_t reconnection_attempts;    /**< Reconnection attempts */
    uint32_t last_connection_time;     /**< Last successful connection time */
    uint32_t total_connected_time;     /**< Total time connected (seconds) */
    int8_t signal_strength_rssi;       /**< Current signal strength (dBm) */
    wifi_status_t current_status;      /**< Current connection status */
} wifi_stats_t;

/**
 * @brief WiFi handler status
 */
typedef enum {
    WIFI_HANDLER_STOPPED = 0,
    WIFI_HANDLER_RUNNING,
    WIFI_HANDLER_ERROR
} wifi_handler_status_t;

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the WiFi handler system
 * 
 * Sets up the WiFi handler, initializes ESP-IDF WiFi components,
 * and prepares for connection management.
 * 
 * @return true if initialization successful, false otherwise
 */
bool wifi_handler_init(void);

/**
 * @brief Start the WiFi handler system
 * 
 * Begins WiFi connection attempts and starts monitoring.
 * 
 * @return true if started successfully, false otherwise
 */
bool wifi_handler_start(void);

/**
 * @brief Stop the WiFi handler system
 * 
 * Stops WiFi connection and monitoring but preserves statistics.
 * 
 * @return true if stopped successfully, false otherwise
 */
bool wifi_handler_stop(void);

/**
 * @brief Get current WiFi handler status
 * 
 * @return Current status of the WiFi handler
 */
wifi_handler_status_t wifi_handler_get_status(void);

/**
 * @brief Get current WiFi connection status
 * 
 * @return Current WiFi connection status
 */
wifi_status_t wifi_handler_get_wifi_status(void);

/**
 * @brief Get WiFi connection statistics
 * 
 * @param stats Pointer to structure to receive statistics
 * @return true if stats retrieved successfully, false otherwise
 */
bool wifi_handler_get_stats(wifi_stats_t *stats);

/**
 * @brief Force immediate WiFi connection attempt
 * 
 * Triggers an immediate connection attempt regardless of current state.
 * 
 * @return true if connection attempt initiated, false otherwise
 */
bool wifi_handler_force_connect(void);

/**
 * @brief Force WiFi disconnection
 * 
 * Disconnects from WiFi and stops auto-reconnection until restart.
 * 
 * @return true if disconnection initiated, false otherwise
 */
bool wifi_handler_force_disconnect(void);

/**
 * @brief Check if WiFi is connected
 * 
 * @return true if WiFi is connected, false otherwise
 */
bool wifi_handler_is_connected(void);

/**
 * @brief Get WiFi IP address
 * 
 * @param ip_str Buffer to receive IP address string (minimum 16 bytes)
 * @param max_len Maximum length of the buffer
 * @return true if IP address retrieved, false otherwise
 */
bool wifi_handler_get_ip_address(char *ip_str, size_t max_len);

/**
 * @brief Get WiFi MAC address
 * 
 * @param mac_str Buffer to receive MAC address string (minimum 18 bytes)
 * @param max_len Maximum length of the buffer
 * @return true if MAC address retrieved, false otherwise
 */
bool wifi_handler_get_mac_address(char *mac_str, size_t max_len);

/**
 * @brief Reset WiFi statistics
 * 
 * Clears all accumulated statistics but preserves current connection state.
 */
void wifi_handler_reset_stats(void);

/**
 * @brief Enable/disable WiFi handler at runtime
 * 
 * Allows dynamic control of WiFi functionality without recompilation.
 * 
 * @param enable true to enable WiFi, false to disable
 */
void wifi_handler_set_enabled(bool enable);

/**
 * @brief Check if WiFi handler is enabled
 * 
 * @return true if WiFi handler is enabled, false otherwise
 */
bool wifi_handler_is_enabled(void);

/* =============================================================================
 * DIAGNOSTIC FUNCTIONS
 * =============================================================================
 */

/**
 * @brief Print detailed WiFi status report to console
 * 
 * Outputs comprehensive WiFi information including connection status,
 * signal strength, IP address, and statistics.
 */
void wifi_handler_print_detailed_report(void);

/**
 * @brief Print WiFi summary to console
 * 
 * Outputs a concise summary of WiFi connection status and key statistics.
 */
void wifi_handler_print_summary(void);

/**
 * @brief Force immediate WiFi status report
 * 
 * Triggers an immediate WiFi status report regardless of the configured
 * reporting interval.
 */
void wifi_handler_force_report(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_HANDLER_H */
