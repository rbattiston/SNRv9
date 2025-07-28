/**
 * @file auth_controller.h
 * @brief Authentication Controller for SNRv9 Web Server
 * 
 * Provides REST API endpoints for authentication functionality.
 * Integrates with AuthManager for session management and web server for HTTP handling.
 */

#ifndef AUTH_CONTROLLER_H
#define AUTH_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "auth_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * CONSTANTS AND CONFIGURATION
 * =============================================================================
 */

#define AUTH_COOKIE_NAME            "session_token"     ///< Session cookie name
#define AUTH_COOKIE_MAX_AGE         (30 * 60)          ///< Cookie max age (30 minutes)
#define AUTH_JSON_BUFFER_SIZE       512                 ///< JSON response buffer size
#define AUTH_REQUEST_BUFFER_SIZE    256                 ///< Request body buffer size

/* =============================================================================
 * TYPE DEFINITIONS
 * =============================================================================
 */

/**
 * @brief Authentication middleware result
 */
typedef enum {
    AUTH_MIDDLEWARE_ALLOW = 0,      ///< Request allowed to proceed
    AUTH_MIDDLEWARE_DENY,           ///< Request denied (authentication failed)
    AUTH_MIDDLEWARE_ERROR           ///< System error during authentication
} auth_middleware_result_t;

/**
 * @brief Login request structure
 */
typedef struct {
    char username[AUTH_USERNAME_MAX_LENGTH + 1];    ///< Username from request
    char password[AUTH_PASSWORD_MAX_LENGTH + 1];    ///< Password from request
} auth_login_request_t;

/**
 * @brief Login response structure
 */
typedef struct {
    bool success;                                   ///< Login success flag
    char message[128];                              ///< Response message
    char session_token[AUTH_SESSION_TOKEN_LENGTH + 1]; ///< Session token (if successful)
    auth_role_t role;                               ///< User role (if successful)
    uint64_t expires_at;                            ///< Session expiration time
} auth_login_response_t;

/**
 * @brief Session status response structure
 */
typedef struct {
    bool authenticated;                             ///< Authentication status
    char username[AUTH_USERNAME_MAX_LENGTH + 1];   ///< Username (if authenticated)
    auth_role_t role;                               ///< User role (if authenticated)
    uint64_t created_time;                          ///< Session creation time
    uint64_t last_activity;                         ///< Last activity time
    uint32_t request_count;                         ///< Request count in session
    uint64_t expires_at;                            ///< Session expiration time
} auth_status_response_t;

/* =============================================================================
 * PUBLIC FUNCTION DECLARATIONS
 * =============================================================================
 */

/**
 * @brief Initialize the authentication controller
 * 
 * Sets up authentication endpoints and middleware.
 * Must be called after auth_manager_init() and web server initialization.
 * 
 * @param server HTTP server handle
 * @return true if initialization successful, false otherwise
 */
bool auth_controller_init(httpd_handle_t server);

/**
 * @brief Deinitialize the authentication controller
 * 
 * Cleans up resources and unregisters endpoints.
 */
void auth_controller_deinit(void);

/**
 * @brief Authentication middleware for protecting endpoints
 * 
 * Validates session token from cookie or Authorization header.
 * Can be used to protect other endpoints that require authentication.
 * 
 * @param req HTTP request handle
 * @param required_role Minimum required role (AUTH_ROLE_NONE for any authenticated user)
 * @param session_info Output parameter for session information (optional, can be NULL)
 * @return Authentication middleware result
 */
auth_middleware_result_t auth_controller_middleware(httpd_req_t *req,
                                                   auth_role_t required_role,
                                                   auth_session_info_t *session_info);

/**
 * @brief Extract session token from request
 * 
 * Looks for session token in cookies first, then Authorization header.
 * 
 * @param req HTTP request handle
 * @param session_token Output buffer for session token (must be AUTH_SESSION_TOKEN_LENGTH + 1)
 * @return true if session token found, false otherwise
 */
bool auth_controller_extract_session_token(httpd_req_t *req, char *session_token);

/**
 * @brief Set session cookie in response
 * 
 * Sets secure session cookie with appropriate flags.
 * 
 * @param req HTTP request handle
 * @param session_token Session token to set in cookie
 * @return ESP_OK if successful, error code otherwise
 */
esp_err_t auth_controller_set_session_cookie(httpd_req_t *req, const char *session_token);

/**
 * @brief Clear session cookie in response
 * 
 * Sets cookie with empty value and immediate expiration.
 * 
 * @param req HTTP request handle
 * @return ESP_OK if successful, error code otherwise
 */
esp_err_t auth_controller_clear_session_cookie(httpd_req_t *req);

/**
 * @brief Send JSON response
 * 
 * Helper function to send JSON responses with proper headers.
 * 
 * @param req HTTP request handle
 * @param status_code HTTP status code
 * @param json_data JSON string to send
 * @return ESP_OK if successful, error code otherwise
 */
esp_err_t auth_controller_send_json_response(httpd_req_t *req, 
                                            int status_code,
                                            const char *json_data);

/**
 * @brief Send error response
 * 
 * Helper function to send standardized error responses.
 * 
 * @param req HTTP request handle
 * @param status_code HTTP status code
 * @param error_message Error message
 * @return ESP_OK if successful, error code otherwise
 */
esp_err_t auth_controller_send_error_response(httpd_req_t *req,
                                             int status_code,
                                             const char *error_message);

/**
 * @brief Parse JSON login request
 * 
 * Parses JSON request body for login credentials.
 * 
 * @param req HTTP request handle
 * @param login_req Output parameter for parsed login request
 * @return true if parsing successful, false otherwise
 */
bool auth_controller_parse_login_request(httpd_req_t *req, auth_login_request_t *login_req);

/**
 * @brief Create login response JSON
 * 
 * Creates JSON response for login endpoint.
 * 
 * @param response Login response structure
 * @param json_buffer Output buffer for JSON string
 * @param buffer_size Size of output buffer
 * @return true if JSON creation successful, false otherwise
 */
bool auth_controller_create_login_response_json(const auth_login_response_t *response,
                                               char *json_buffer,
                                               size_t buffer_size);

/**
 * @brief Create status response JSON
 * 
 * Creates JSON response for status endpoint.
 * 
 * @param response Status response structure
 * @param json_buffer Output buffer for JSON string
 * @param buffer_size Size of output buffer
 * @return true if JSON creation successful, false otherwise
 */
bool auth_controller_create_status_response_json(const auth_status_response_t *response,
                                                char *json_buffer,
                                                size_t buffer_size);

/**
 * @brief Get authentication statistics as JSON
 * 
 * Creates JSON response with authentication system statistics.
 * 
 * @param json_buffer Output buffer for JSON string
 * @param buffer_size Size of output buffer
 * @return true if JSON creation successful, false otherwise
 */
bool auth_controller_get_stats_json(char *json_buffer, size_t buffer_size);

/**
 * @brief Validate request content type
 * 
 * Checks if request has proper JSON content type.
 * 
 * @param req HTTP request handle
 * @return true if content type is valid, false otherwise
 */
bool auth_controller_validate_content_type(httpd_req_t *req);

/**
 * @brief Add CORS headers to response
 * 
 * Adds Cross-Origin Resource Sharing headers for web compatibility.
 * 
 * @param req HTTP request handle
 * @return ESP_OK if successful, error code otherwise
 */
esp_err_t auth_controller_add_cors_headers(httpd_req_t *req);

/* =============================================================================
 * HTTP ENDPOINT HANDLERS (Internal use - registered automatically)
 * =============================================================================
 */

/**
 * @brief POST /api/auth/login - User login endpoint
 * 
 * Authenticates user credentials and creates session.
 * 
 * Request Body (JSON):
 * {
 *   "username": "string",
 *   "password": "string"
 * }
 * 
 * Response (JSON):
 * {
 *   "success": boolean,
 *   "message": "string",
 *   "role": "string",
 *   "expires_at": number
 * }
 * 
 * @param req HTTP request handle
 * @return ESP_OK if handled successfully
 */
esp_err_t auth_controller_login_handler(httpd_req_t *req);

/**
 * @brief POST /api/auth/logout - User logout endpoint
 * 
 * Invalidates current session and clears cookie.
 * 
 * Response (JSON):
 * {
 *   "success": boolean,
 *   "message": "string"
 * }
 * 
 * @param req HTTP request handle
 * @return ESP_OK if handled successfully
 */
esp_err_t auth_controller_logout_handler(httpd_req_t *req);

/**
 * @brief GET /api/auth/status - Session status endpoint
 * 
 * Returns current authentication status and session information.
 * 
 * Response (JSON):
 * {
 *   "authenticated": boolean,
 *   "username": "string",
 *   "role": "string",
 *   "created_time": number,
 *   "last_activity": number,
 *   "request_count": number,
 *   "expires_at": number
 * }
 * 
 * @param req HTTP request handle
 * @return ESP_OK if handled successfully
 */
esp_err_t auth_controller_status_handler(httpd_req_t *req);

/**
 * @brief GET /api/auth/validate - Session validation endpoint
 * 
 * Validates session token and returns basic validation result.
 * Used by other controllers for authentication middleware.
 * 
 * Response (JSON):
 * {
 *   "valid": boolean,
 *   "role": "string"
 * }
 * 
 * @param req HTTP request handle
 * @return ESP_OK if handled successfully
 */
esp_err_t auth_controller_validate_handler(httpd_req_t *req);

/**
 * @brief GET /api/auth/stats - Authentication statistics endpoint
 * 
 * Returns authentication system statistics (requires MANAGER role).
 * 
 * Response (JSON):
 * {
 *   "total_login_attempts": number,
 *   "successful_logins": number,
 *   "failed_logins": number,
 *   "active_sessions": number,
 *   "sessions_created": number,
 *   "sessions_expired": number,
 *   "sessions_invalidated": number,
 *   "rate_limit_hits": number
 * }
 * 
 * @param req HTTP request handle
 * @return ESP_OK if handled successfully
 */
esp_err_t auth_controller_stats_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif // AUTH_CONTROLLER_H
