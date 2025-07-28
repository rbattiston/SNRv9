/**
 * @file auth_controller.c
 * @brief Authentication Controller implementation for SNRv9 Web Server
 */

#include "auth_controller.h"
#include "debug_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <cJSON.h>

/* =============================================================================
 * PRIVATE CONSTANTS AND MACROS
 * =============================================================================
 */

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
 * PRIVATE VARIABLES
 * =============================================================================
 */

static httpd_handle_t g_server = NULL;
static const char *TAG = "AUTH_CONTROLLER";

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static uint64_t get_current_time_ms(void);
static size_t get_header_value(httpd_req_t *req, const char *header_name, char *buffer, size_t buffer_size);
static bool extract_cookie_value(const char *cookie_header, const char *cookie_name, char *value, size_t value_size);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool auth_controller_init(httpd_handle_t server)
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Invalid server handle");
        return false;
    }

    g_server = server;

    // Register authentication endpoints
    httpd_uri_t login_uri = {
        .uri = "/api/auth/login",
        .method = HTTP_POST,
        .handler = auth_controller_login_handler,
        .user_ctx = NULL
    };

    httpd_uri_t logout_uri = {
        .uri = "/api/auth/logout",
        .method = HTTP_POST,
        .handler = auth_controller_logout_handler,
        .user_ctx = NULL
    };

    httpd_uri_t status_uri = {
        .uri = "/api/auth/status",
        .method = HTTP_GET,
        .handler = auth_controller_status_handler,
        .user_ctx = NULL
    };

    httpd_uri_t validate_uri = {
        .uri = "/api/auth/validate",
        .method = HTTP_GET,
        .handler = auth_controller_validate_handler,
        .user_ctx = NULL
    };

    httpd_uri_t stats_uri = {
        .uri = "/api/auth/stats",
        .method = HTTP_GET,
        .handler = auth_controller_stats_handler,
        .user_ctx = NULL
    };

    // Register all endpoints
    if (httpd_register_uri_handler(server, &login_uri) != ESP_OK ||
        httpd_register_uri_handler(server, &logout_uri) != ESP_OK ||
        httpd_register_uri_handler(server, &status_uri) != ESP_OK ||
        httpd_register_uri_handler(server, &validate_uri) != ESP_OK ||
        httpd_register_uri_handler(server, &stats_uri) != ESP_OK) {
        
        ESP_LOGE(TAG, "Failed to register authentication endpoints");
        return false;
    }

    uint32_t timestamp = GET_TIMESTAMP();
    printf(TIMESTAMP_FORMAT "%s: Authentication controller initialized successfully\n",
           FORMAT_TIMESTAMP(timestamp), TAG);
    printf(TIMESTAMP_FORMAT "%s: Registered endpoints: /api/auth/login, /api/auth/logout, /api/auth/status, /api/auth/validate, /api/auth/stats\n",
           FORMAT_TIMESTAMP(timestamp), TAG);

    return true;
}

void auth_controller_deinit(void)
{
    if (g_server != NULL) {
        // Unregister endpoints
        httpd_unregister_uri_handler(g_server, "/api/auth/login", HTTP_POST);
        httpd_unregister_uri_handler(g_server, "/api/auth/logout", HTTP_POST);
        httpd_unregister_uri_handler(g_server, "/api/auth/status", HTTP_GET);
        httpd_unregister_uri_handler(g_server, "/api/auth/validate", HTTP_GET);
        httpd_unregister_uri_handler(g_server, "/api/auth/stats", HTTP_GET);
        
        g_server = NULL;
        ESP_LOGI(TAG, "Authentication controller deinitialized");
    }
}

auth_middleware_result_t auth_controller_middleware(httpd_req_t *req,
                                                   auth_role_t required_role,
                                                   auth_session_info_t *session_info)
{
    if (req == NULL) {
        return AUTH_MIDDLEWARE_ERROR;
    }

    char session_token[AUTH_SESSION_TOKEN_LENGTH + 1];
    if (!auth_controller_extract_session_token(req, session_token)) {
        return AUTH_MIDDLEWARE_DENY;
    }

    auth_result_t result = auth_manager_validate_session(session_token, session_info);
    if (result != AUTH_RESULT_SUCCESS) {
        return AUTH_MIDDLEWARE_DENY;
    }

    // Check role if required
    if (required_role != AUTH_ROLE_NONE) {
        result = auth_manager_check_role(session_token, required_role);
        if (result != AUTH_RESULT_SUCCESS) {
            return AUTH_MIDDLEWARE_DENY;
        }
    }

    return AUTH_MIDDLEWARE_ALLOW;
}

bool auth_controller_extract_session_token(httpd_req_t *req, char *session_token)
{
    if (req == NULL || session_token == NULL) {
        return false;
    }

    // First try to get from Cookie header
    char cookie_header[512];
    if (get_header_value(req, "Cookie", cookie_header, sizeof(cookie_header)) > 0) {
        if (extract_cookie_value(cookie_header, AUTH_COOKIE_NAME, session_token, AUTH_SESSION_TOKEN_LENGTH + 1)) {
            return true;
        }
    }

    // Try Authorization header as fallback
    char auth_header[AUTH_SESSION_TOKEN_LENGTH + 32];
    if (get_header_value(req, "Authorization", auth_header, sizeof(auth_header)) > 0) {
        // Look for "Bearer <token>" format
        if (strncmp(auth_header, "Bearer ", 7) == 0) {
            strncpy(session_token, auth_header + 7, AUTH_SESSION_TOKEN_LENGTH);
            session_token[AUTH_SESSION_TOKEN_LENGTH] = '\0';
            return true;
        }
    }

    return false;
}

esp_err_t auth_controller_set_session_cookie(httpd_req_t *req, const char *session_token)
{
    if (req == NULL || session_token == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char cookie_header[256];
    snprintf(cookie_header, sizeof(cookie_header),
             "%s=%s; Max-Age=%d; Path=/; HttpOnly; SameSite=Strict",
             AUTH_COOKIE_NAME, session_token, AUTH_COOKIE_MAX_AGE);

    return httpd_resp_set_hdr(req, "Set-Cookie", cookie_header);
}

esp_err_t auth_controller_clear_session_cookie(httpd_req_t *req)
{
    if (req == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char cookie_header[128];
    snprintf(cookie_header, sizeof(cookie_header),
             "%s=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict",
             AUTH_COOKIE_NAME);

    return httpd_resp_set_hdr(req, "Set-Cookie", cookie_header);
}

esp_err_t auth_controller_send_json_response(httpd_req_t *req, 
                                            int status_code,
                                            const char *json_data)
{
    if (req == NULL || json_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set status code
    char status_str[16];
    snprintf(status_str, sizeof(status_str), "%d", status_code);
    httpd_resp_set_status(req, status_str);

    // Set content type
    httpd_resp_set_type(req, "application/json");

    // Add CORS headers
    auth_controller_add_cors_headers(req);

    // Send response
    return httpd_resp_send(req, json_data, strlen(json_data));
}

esp_err_t auth_controller_send_error_response(httpd_req_t *req,
                                             int status_code,
                                             const char *error_message)
{
    if (req == NULL || error_message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char json_response[256];
    snprintf(json_response, sizeof(json_response),
             "{\"success\":false,\"error\":\"%s\"}", error_message);

    return auth_controller_send_json_response(req, status_code, json_response);
}

bool auth_controller_parse_login_request(httpd_req_t *req, auth_login_request_t *login_req)
{
    if (req == NULL || login_req == NULL) {
        return false;
    }

    // Read request body
    char content[AUTH_REQUEST_BUFFER_SIZE];
    int content_len = httpd_req_recv(req, content, sizeof(content) - 1);
    if (content_len <= 0) {
        return false;
    }
    content[content_len] = '\0';

    // Parse JSON
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        return false;
    }

    bool success = false;
    cJSON *username_json = cJSON_GetObjectItem(json, "username");
    cJSON *password_json = cJSON_GetObjectItem(json, "password");

    if (cJSON_IsString(username_json) && cJSON_IsString(password_json)) {
        strncpy(login_req->username, username_json->valuestring, AUTH_USERNAME_MAX_LENGTH);
        login_req->username[AUTH_USERNAME_MAX_LENGTH] = '\0';
        strncpy(login_req->password, password_json->valuestring, AUTH_PASSWORD_MAX_LENGTH);
        login_req->password[AUTH_PASSWORD_MAX_LENGTH] = '\0';
        success = true;
    }

    cJSON_Delete(json);
    return success;
}

bool auth_controller_create_login_response_json(const auth_login_response_t *response,
                                               char *json_buffer,
                                               size_t buffer_size)
{
    if (response == NULL || json_buffer == NULL) {
        return false;
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return false;
    }

    cJSON_AddBoolToObject(json, "success", response->success);
    cJSON_AddStringToObject(json, "message", response->message);

    if (response->success) {
        cJSON_AddStringToObject(json, "role", auth_manager_role_to_string(response->role));
        cJSON_AddNumberToObject(json, "expires_at", (double)response->expires_at);
    }

    char *json_string = cJSON_Print(json);
    bool success = false;
    
    if (json_string != NULL && strlen(json_string) < buffer_size) {
        strcpy(json_buffer, json_string);
        success = true;
    }

    if (json_string != NULL) {
        free(json_string);
    }
    cJSON_Delete(json);
    return success;
}

bool auth_controller_create_status_response_json(const auth_status_response_t *response,
                                                char *json_buffer,
                                                size_t buffer_size)
{
    if (response == NULL || json_buffer == NULL) {
        return false;
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return false;
    }

    cJSON_AddBoolToObject(json, "authenticated", response->authenticated);

    if (response->authenticated) {
        cJSON_AddStringToObject(json, "username", response->username);
        cJSON_AddStringToObject(json, "role", auth_manager_role_to_string(response->role));
        cJSON_AddNumberToObject(json, "created_time", (double)response->created_time);
        cJSON_AddNumberToObject(json, "last_activity", (double)response->last_activity);
        cJSON_AddNumberToObject(json, "request_count", response->request_count);
        cJSON_AddNumberToObject(json, "expires_at", (double)response->expires_at);
    }

    char *json_string = cJSON_Print(json);
    bool success = false;
    
    if (json_string != NULL && strlen(json_string) < buffer_size) {
        strcpy(json_buffer, json_string);
        success = true;
    }

    if (json_string != NULL) {
        free(json_string);
    }
    cJSON_Delete(json);
    return success;
}

bool auth_controller_get_stats_json(char *json_buffer, size_t buffer_size)
{
    if (json_buffer == NULL) {
        return false;
    }

    auth_stats_t stats;
    if (!auth_manager_get_stats(&stats)) {
        return false;
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return false;
    }

    cJSON_AddNumberToObject(json, "total_login_attempts", stats.total_login_attempts);
    cJSON_AddNumberToObject(json, "successful_logins", stats.successful_logins);
    cJSON_AddNumberToObject(json, "failed_logins", stats.failed_logins);
    cJSON_AddNumberToObject(json, "active_sessions", stats.active_sessions);
    cJSON_AddNumberToObject(json, "sessions_created", stats.sessions_created);
    cJSON_AddNumberToObject(json, "sessions_expired", stats.sessions_expired);
    cJSON_AddNumberToObject(json, "sessions_invalidated", stats.sessions_invalidated);
    cJSON_AddNumberToObject(json, "rate_limit_hits", stats.rate_limit_hits);
    cJSON_AddNumberToObject(json, "last_login_time", (double)stats.last_login_time);
    cJSON_AddNumberToObject(json, "last_failed_login_time", (double)stats.last_failed_login_time);

    char *json_string = cJSON_Print(json);
    bool success = false;
    
    if (json_string != NULL && strlen(json_string) < buffer_size) {
        strcpy(json_buffer, json_string);
        success = true;
    }

    if (json_string != NULL) {
        free(json_string);
    }
    cJSON_Delete(json);
    return success;
}

bool auth_controller_validate_content_type(httpd_req_t *req)
{
    if (req == NULL) {
        return false;
    }

    char content_type[64];
    if (get_header_value(req, "Content-Type", content_type, sizeof(content_type)) > 0) {
        return strstr(content_type, "application/json") != NULL;
    }

    return false;
}

esp_err_t auth_controller_add_cors_headers(httpd_req_t *req)
{
    if (req == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");

    return ESP_OK;
}

/* =============================================================================
 * HTTP ENDPOINT HANDLERS
 * =============================================================================
 */

esp_err_t auth_controller_login_handler(httpd_req_t *req)
{
    uint32_t timestamp = GET_TIMESTAMP();
    printf(TIMESTAMP_FORMAT "%s: Login request from %s\n",
           FORMAT_TIMESTAMP(timestamp), TAG, "client");

    // Validate content type
    if (!auth_controller_validate_content_type(req)) {
        return auth_controller_send_error_response(req, 400, "Invalid content type");
    }

    // Parse login request
    auth_login_request_t login_req;
    if (!auth_controller_parse_login_request(req, &login_req)) {
        return auth_controller_send_error_response(req, 400, "Invalid JSON format");
    }

    // Attempt login
    auth_login_response_t response = {0};
    char session_token[AUTH_SESSION_TOKEN_LENGTH + 1];
    auth_role_t role;

    auth_result_t result = auth_manager_login(login_req.username, login_req.password, 
                                             session_token, &role);

    response.success = (result == AUTH_RESULT_SUCCESS);
    response.role = role;
    response.expires_at = get_current_time_ms() + AUTH_SESSION_TIMEOUT_MS;

    switch (result) {
        case AUTH_RESULT_SUCCESS:
            strcpy(response.message, "Login successful");
            strncpy(response.session_token, session_token, AUTH_SESSION_TOKEN_LENGTH);
            response.session_token[AUTH_SESSION_TOKEN_LENGTH] = '\0';
            break;
        case AUTH_RESULT_INVALID_CREDENTIALS:
            strcpy(response.message, "Invalid username or password");
            break;
        case AUTH_RESULT_RATE_LIMITED:
            strcpy(response.message, "Too many login attempts, please try again later");
            break;
        case AUTH_RESULT_MAX_SESSIONS:
            strcpy(response.message, "Maximum concurrent sessions reached");
            break;
        default:
            strcpy(response.message, "Login failed");
            break;
    }

    // Create JSON response
    char json_buffer[AUTH_JSON_BUFFER_SIZE];
    if (!auth_controller_create_login_response_json(&response, json_buffer, sizeof(json_buffer))) {
        return auth_controller_send_error_response(req, 500, "Failed to create response");
    }

    // Set session cookie if login successful
    if (response.success) {
        auth_controller_set_session_cookie(req, session_token);
    }

    int status_code = response.success ? 200 : 401;
    return auth_controller_send_json_response(req, status_code, json_buffer);
}

esp_err_t auth_controller_logout_handler(httpd_req_t *req)
{
    uint32_t timestamp = GET_TIMESTAMP();
    printf(TIMESTAMP_FORMAT "%s: Logout request\n",
           FORMAT_TIMESTAMP(timestamp), TAG);

    // Extract session token
    char session_token[AUTH_SESSION_TOKEN_LENGTH + 1];
    bool has_session = auth_controller_extract_session_token(req, session_token);

    // Logout if session exists
    if (has_session) {
        auth_manager_logout(session_token);
    }

    // Clear session cookie
    auth_controller_clear_session_cookie(req);

    // Send response
    char json_response[] = "{\"success\":true,\"message\":\"Logged out successfully\"}";
    return auth_controller_send_json_response(req, 200, json_response);
}

esp_err_t auth_controller_status_handler(httpd_req_t *req)
{
    auth_status_response_t response = {0};

    // Extract session token
    char session_token[AUTH_SESSION_TOKEN_LENGTH + 1];
    if (auth_controller_extract_session_token(req, session_token)) {
        // Validate session
        auth_session_info_t session_info;
        auth_result_t result = auth_manager_validate_session(session_token, &session_info);
        
        if (result == AUTH_RESULT_SUCCESS) {
            response.authenticated = true;
            strncpy(response.username, session_info.username, AUTH_USERNAME_MAX_LENGTH);
            response.username[AUTH_USERNAME_MAX_LENGTH] = '\0';
            response.role = session_info.role;
            response.created_time = session_info.created_time;
            response.last_activity = session_info.last_activity;
            response.request_count = session_info.request_count;
            response.expires_at = session_info.created_time + AUTH_SESSION_TIMEOUT_MS;
        }
    }

    // Create JSON response
    char json_buffer[AUTH_JSON_BUFFER_SIZE];
    if (!auth_controller_create_status_response_json(&response, json_buffer, sizeof(json_buffer))) {
        return auth_controller_send_error_response(req, 500, "Failed to create response");
    }

    return auth_controller_send_json_response(req, 200, json_buffer);
}

esp_err_t auth_controller_validate_handler(httpd_req_t *req)
{
    bool valid = false;
    auth_role_t role = AUTH_ROLE_NONE;

    // Extract and validate session token
    char session_token[AUTH_SESSION_TOKEN_LENGTH + 1];
    if (auth_controller_extract_session_token(req, session_token)) {
        auth_session_info_t session_info;
        auth_result_t result = auth_manager_validate_session(session_token, &session_info);
        
        if (result == AUTH_RESULT_SUCCESS) {
            valid = true;
            role = session_info.role;
        }
    }

    // Create JSON response
    char json_response[128];
    snprintf(json_response, sizeof(json_response),
             "{\"valid\":%s,\"role\":\"%s\"}",
             valid ? "true" : "false",
             auth_manager_role_to_string(role));

    return auth_controller_send_json_response(req, 200, json_response);
}

esp_err_t auth_controller_stats_handler(httpd_req_t *req)
{
    // Check authentication and role
    auth_middleware_result_t auth_result = auth_controller_middleware(req, AUTH_ROLE_MANAGER, NULL);
    
    if (auth_result == AUTH_MIDDLEWARE_DENY) {
        return auth_controller_send_error_response(req, 401, "Authentication required");
    } else if (auth_result == AUTH_MIDDLEWARE_ERROR) {
        return auth_controller_send_error_response(req, 500, "Authentication error");
    }

    // Get statistics
    char json_buffer[AUTH_JSON_BUFFER_SIZE];
    if (!auth_controller_get_stats_json(json_buffer, sizeof(json_buffer))) {
        return auth_controller_send_error_response(req, 500, "Failed to get statistics");
    }

    return auth_controller_send_json_response(req, 200, json_buffer);
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static uint64_t get_current_time_ms(void)
{
    return esp_timer_get_time() / 1000ULL;
}

static size_t get_header_value(httpd_req_t *req, const char *header_name, char *buffer, size_t buffer_size)
{
    size_t header_len = httpd_req_get_hdr_value_len(req, header_name);
    if (header_len > 0 && header_len < buffer_size) {
        if (httpd_req_get_hdr_value_str(req, header_name, buffer, buffer_size) == ESP_OK) {
            return header_len;
        }
    }
    return 0;
}

static bool extract_cookie_value(const char *cookie_header, const char *cookie_name, char *value, size_t value_size)
{
    if (cookie_header == NULL || cookie_name == NULL || value == NULL) {
        return false;
    }

    char search_pattern[64];
    snprintf(search_pattern, sizeof(search_pattern), "%s=", cookie_name);
    
    const char *cookie_start = strstr(cookie_header, search_pattern);
    if (cookie_start == NULL) {
        return false;
    }

    cookie_start += strlen(search_pattern);
    const char *cookie_end = strchr(cookie_start, ';');
    
    size_t cookie_len;
    if (cookie_end != NULL) {
        cookie_len = cookie_end - cookie_start;
    } else {
        cookie_len = strlen(cookie_start);
    }

    if (cookie_len >= value_size) {
        return false;
    }

    strncpy(value, cookie_start, cookie_len);
    value[cookie_len] = '\0';
    
    return true;
}
