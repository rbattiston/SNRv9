/**
 * @file auth_manager.h
 * @brief Authentication Manager for SNRv9 Irrigation Control System
 * 
 * Provides session-based authentication with role-based access control (RBAC).
 * Designed for ESP32 constraints while maintaining production-grade security.
 */

#ifndef AUTH_MANAGER_H
#define AUTH_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * CONSTANTS AND CONFIGURATION
 * =============================================================================
 */

#define AUTH_SESSION_TOKEN_LENGTH   32      ///< Session token length (hex string)
#define AUTH_USERNAME_MAX_LENGTH    32      ///< Maximum username length
#define AUTH_PASSWORD_MAX_LENGTH    64      ///< Maximum password length
#define AUTH_MAX_CONCURRENT_SESSIONS 5      ///< Maximum concurrent sessions
#define AUTH_SESSION_TIMEOUT_MS     (30 * 60 * 1000)  ///< 30 minutes default timeout
#define AUTH_MAX_LOGIN_ATTEMPTS     5       ///< Maximum login attempts before rate limiting
#define AUTH_RATE_LIMIT_WINDOW_MS   (5 * 60 * 1000)   ///< 5 minute rate limit window

/* =============================================================================
 * TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief User roles for role-based access control
 */
typedef enum {
    AUTH_ROLE_NONE = 0,     ///< No access
    AUTH_ROLE_VIEWER,       ///< Read-only access to system status and monitoring
    AUTH_ROLE_MANAGER,      ///< Can modify configuration and control irrigation
    AUTH_ROLE_OWNER         ///< Full administrative access including user management
} auth_role_t;

/**
 * @brief Authentication result codes
 */
typedef enum {
    AUTH_RESULT_SUCCESS = 0,        ///< Authentication successful
    AUTH_RESULT_INVALID_CREDENTIALS,///< Invalid username or password
    AUTH_RESULT_SESSION_EXPIRED,    ///< Session has expired
    AUTH_RESULT_SESSION_NOT_FOUND,  ///< Session token not found
    AUTH_RESULT_RATE_LIMITED,       ///< Too many login attempts
    AUTH_RESULT_MAX_SESSIONS,       ///< Maximum concurrent sessions reached
    AUTH_RESULT_INVALID_ROLE,       ///< Insufficient role permissions
    AUTH_RESULT_SYSTEM_ERROR        ///< Internal system error
} auth_result_t;

/**
 * @brief Session information structure
 */
typedef struct {
    char session_token[AUTH_SESSION_TOKEN_LENGTH + 1];  ///< Session token (null-terminated)
    char username[AUTH_USERNAME_MAX_LENGTH + 1];        ///< Username (null-terminated)
    auth_role_t role;                                   ///< User role
    uint64_t created_time;                              ///< Session creation time (ms since boot)
    uint64_t last_activity;                             ///< Last activity time (ms since boot)
    uint32_t request_count;                             ///< Number of requests in this session
    bool is_active;                                     ///< Session active flag
} auth_session_info_t;

/**
 * @brief User credentials structure (for hardcoded users initially)
 */
typedef struct {
    char username[AUTH_USERNAME_MAX_LENGTH + 1];        ///< Username
    char password[AUTH_PASSWORD_MAX_LENGTH + 1];        ///< Password (plaintext for now)
    auth_role_t role;                                   ///< User role
    bool is_enabled;                                    ///< User enabled flag
} auth_user_t;

/**
 * @brief Authentication statistics
 */
typedef struct {
    uint32_t total_login_attempts;      ///< Total login attempts
    uint32_t successful_logins;         ///< Successful logins
    uint32_t failed_logins;             ///< Failed login attempts
    uint32_t sessions_created;          ///< Total sessions created
    uint32_t sessions_expired;          ///< Sessions expired due to timeout
    uint32_t sessions_invalidated;      ///< Sessions manually invalidated
    uint32_t rate_limit_hits;           ///< Rate limit violations
    uint32_t active_sessions;           ///< Currently active sessions
    uint64_t last_login_time;           ///< Last successful login time
    uint64_t last_failed_login_time;    ///< Last failed login time
} auth_stats_t;

/**
 * @brief Authentication configuration
 */
typedef struct {
    uint32_t session_timeout_ms;        ///< Session timeout in milliseconds
    uint32_t max_concurrent_sessions;   ///< Maximum concurrent sessions
    uint32_t max_login_attempts;        ///< Maximum login attempts before rate limiting
    uint32_t rate_limit_window_ms;      ///< Rate limit window in milliseconds
    bool require_secure_cookies;        ///< Require secure cookies (HTTPS only)
    bool enable_session_logging;        ///< Enable detailed session logging
} auth_config_t;

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the authentication manager
 * 
 * Sets up session storage, user database, and security parameters.
 * Must be called before any other auth functions.
 * 
 * @return true if initialization successful, false otherwise
 */
bool auth_manager_init(void);

/**
 * @brief Deinitialize the authentication manager
 * 
 * Cleans up resources and invalidates all active sessions.
 */
void auth_manager_deinit(void);

/**
 * @brief Authenticate user and create session
 * 
 * Validates credentials and creates a new session if successful.
 * Implements rate limiting and concurrent session management.
 * 
 * @param username User's username
 * @param password User's password
 * @param session_token Output buffer for session token (must be AUTH_SESSION_TOKEN_LENGTH + 1)
 * @param role Output parameter for user's role
 * @return Authentication result code
 */
auth_result_t auth_manager_login(const char *username, 
                                const char *password,
                                char *session_token,
                                auth_role_t *role);

/**
 * @brief Validate session and update activity
 * 
 * Checks if session token is valid and updates last activity time.
 * 
 * @param session_token Session token to validate
 * @param session_info Output parameter for session information (optional, can be NULL)
 * @return Authentication result code
 */
auth_result_t auth_manager_validate_session(const char *session_token,
                                           auth_session_info_t *session_info);

/**
 * @brief Check if session has required role
 * 
 * Validates session and checks if user has sufficient role permissions.
 * 
 * @param session_token Session token to check
 * @param required_role Minimum required role
 * @return Authentication result code
 */
auth_result_t auth_manager_check_role(const char *session_token,
                                     auth_role_t required_role);

/**
 * @brief Logout user and invalidate session
 * 
 * Removes session from active sessions and cleans up resources.
 * 
 * @param session_token Session token to invalidate
 * @return Authentication result code
 */
auth_result_t auth_manager_logout(const char *session_token);

/**
 * @brief Get session information
 * 
 * Retrieves detailed information about an active session.
 * 
 * @param session_token Session token to query
 * @param session_info Output parameter for session information
 * @return Authentication result code
 */
auth_result_t auth_manager_get_session_info(const char *session_token,
                                           auth_session_info_t *session_info);

/**
 * @brief Get authentication statistics
 * 
 * Retrieves current authentication system statistics.
 * 
 * @param stats Output parameter for statistics
 * @return true if successful, false otherwise
 */
bool auth_manager_get_stats(auth_stats_t *stats);

/**
 * @brief Reset authentication statistics
 * 
 * Resets all authentication statistics to zero.
 */
void auth_manager_reset_stats(void);

/**
 * @brief Clean up expired sessions
 * 
 * Removes sessions that have exceeded the timeout period.
 * Called automatically but can be called manually for immediate cleanup.
 * 
 * @return Number of sessions cleaned up
 */
uint32_t auth_manager_cleanup_expired_sessions(void);

/**
 * @brief Get list of active sessions
 * 
 * Retrieves information about all currently active sessions.
 * 
 * @param sessions Output array for session information
 * @param max_sessions Maximum number of sessions to return
 * @param session_count Output parameter for actual number of sessions returned
 * @return true if successful, false otherwise
 */
bool auth_manager_get_active_sessions(auth_session_info_t *sessions,
                                     uint32_t max_sessions,
                                     uint32_t *session_count);

/**
 * @brief Configure authentication parameters
 * 
 * Updates authentication system configuration.
 * 
 * @param config New configuration parameters
 * @return true if successful, false otherwise
 */
bool auth_manager_configure(const auth_config_t *config);

/**
 * @brief Get current authentication configuration
 * 
 * Retrieves current authentication system configuration.
 * 
 * @param config Output parameter for configuration
 * @return true if successful, false otherwise
 */
bool auth_manager_get_config(auth_config_t *config);

/**
 * @brief Print authentication system status
 * 
 * Outputs current authentication status to debug console.
 * Respects debug configuration settings.
 */
void auth_manager_print_status(void);

/**
 * @brief Convert authentication result to string
 * 
 * Converts auth_result_t enum to human-readable string.
 * 
 * @param result Authentication result code
 * @return String representation of result
 */
const char* auth_manager_result_to_string(auth_result_t result);

/**
 * @brief Convert role to string
 * 
 * Converts auth_role_t enum to human-readable string.
 * 
 * @param role User role
 * @return String representation of role
 */
const char* auth_manager_role_to_string(auth_role_t role);

/**
 * @brief Add hardcoded user (development/testing)
 * 
 * Adds a user to the hardcoded user database.
 * This is for initial development - will be replaced with persistent storage later.
 * 
 * @param username Username
 * @param password Password
 * @param role User role
 * @return true if successful, false otherwise
 */
bool auth_manager_add_hardcoded_user(const char *username,
                                     const char *password,
                                     auth_role_t role);

#ifdef __cplusplus
}
#endif

#endif // AUTH_MANAGER_H
