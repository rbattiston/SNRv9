/**
 * @file auth_manager.c
 * @brief Authentication Manager implementation for SNRv9 Irrigation Control System
 */

#include "auth_manager.h"
#include "debug_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

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

#define AUTH_MAX_HARDCODED_USERS    10      ///< Maximum hardcoded users
#define AUTH_SESSION_CLEANUP_INTERVAL_MS (5 * 60 * 1000)  ///< 5 minutes

/* =============================================================================
 * PRIVATE TYPE DEFINITIONS
 * =============================================================================
 */

typedef struct {
    auth_session_info_t sessions[AUTH_MAX_CONCURRENT_SESSIONS];
    auth_user_t hardcoded_users[AUTH_MAX_HARDCODED_USERS];
    auth_stats_t stats;
    auth_config_t config;
    uint32_t hardcoded_user_count;
    uint64_t last_cleanup_time;
    uint32_t failed_login_attempts;
    uint64_t rate_limit_reset_time;
    SemaphoreHandle_t mutex;
    bool initialized;
} auth_context_t;

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static auth_context_t g_auth = {0};
static const char *TAG = "AUTH_MANAGER";

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static void generate_session_token(char *token);
static bool find_user(const char *username, auth_user_t *user);
static int find_session_index(const char *session_token);
static int find_free_session_slot(void);
static void cleanup_session_at_index(int index);
static bool is_rate_limited(void);
static void update_rate_limit(bool successful_login);
static uint64_t get_current_time_ms(void);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool auth_manager_init(void)
{
    if (g_auth.initialized) {
        ESP_LOGW(TAG, "Authentication manager already initialized");
        return false;
    }

    // Create mutex for thread-safe access
    g_auth.mutex = xSemaphoreCreateMutex();
    if (g_auth.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create authentication mutex");
        return false;
    }

    // Initialize configuration with defaults
    g_auth.config.session_timeout_ms = AUTH_SESSION_TIMEOUT_MS;
    g_auth.config.max_concurrent_sessions = AUTH_MAX_CONCURRENT_SESSIONS;
    g_auth.config.max_login_attempts = AUTH_MAX_LOGIN_ATTEMPTS;
    g_auth.config.rate_limit_window_ms = AUTH_RATE_LIMIT_WINDOW_MS;
    g_auth.config.require_secure_cookies = false;  // HTTP for development
    g_auth.config.enable_session_logging = true;

    // Initialize session storage
    memset(g_auth.sessions, 0, sizeof(g_auth.sessions));
    
    // Initialize user storage
    memset(g_auth.hardcoded_users, 0, sizeof(g_auth.hardcoded_users));
    g_auth.hardcoded_user_count = 0;

    // Initialize statistics
    memset(&g_auth.stats, 0, sizeof(auth_stats_t));

    // Initialize rate limiting
    g_auth.failed_login_attempts = 0;
    g_auth.rate_limit_reset_time = get_current_time_ms() + g_auth.config.rate_limit_window_ms;
    g_auth.last_cleanup_time = get_current_time_ms();

    // Add default admin user
    auth_manager_add_hardcoded_user("admin", "admin", AUTH_ROLE_OWNER);
    auth_manager_add_hardcoded_user("manager", "manager", AUTH_ROLE_MANAGER);
    auth_manager_add_hardcoded_user("viewer", "viewer", AUTH_ROLE_VIEWER);

    g_auth.initialized = true;
    
    uint32_t timestamp = GET_TIMESTAMP();
    printf(TIMESTAMP_FORMAT "%s: Authentication manager initialized successfully\n",
           FORMAT_TIMESTAMP(timestamp), TAG);
    printf(TIMESTAMP_FORMAT "%s: Default users: admin/admin (OWNER), manager/manager (MANAGER), viewer/viewer (VIEWER)\n",
           FORMAT_TIMESTAMP(timestamp), TAG);

    return true;
}

void auth_manager_deinit(void)
{
    if (!g_auth.initialized) {
        return;
    }

    if (xSemaphoreTake(g_auth.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // Invalidate all active sessions
        for (int i = 0; i < AUTH_MAX_CONCURRENT_SESSIONS; i++) {
            if (g_auth.sessions[i].is_active) {
                cleanup_session_at_index(i);
            }
        }

        g_auth.initialized = false;
        xSemaphoreGive(g_auth.mutex);
    }

    vSemaphoreDelete(g_auth.mutex);
    ESP_LOGI(TAG, "Authentication manager deinitialized");
}

auth_result_t auth_manager_login(const char *username, 
                                const char *password,
                                char *session_token,
                                auth_role_t *role)
{
    if (!g_auth.initialized || username == NULL || password == NULL || 
        session_token == NULL || role == NULL) {
        return AUTH_RESULT_SYSTEM_ERROR;
    }

    uint32_t timestamp = GET_TIMESTAMP();
    auth_result_t result = AUTH_RESULT_SYSTEM_ERROR;

    if (xSemaphoreTake(g_auth.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_auth.stats.total_login_attempts++;

        // Check rate limiting
        if (is_rate_limited()) {
            g_auth.stats.rate_limit_hits++;
            result = AUTH_RESULT_RATE_LIMITED;
            printf(TIMESTAMP_FORMAT "%s: Login rate limited for user: %s\n",
                   FORMAT_TIMESTAMP(timestamp), TAG, username);
            goto cleanup;
        }

        // Find user in hardcoded database
        auth_user_t user;
        if (!find_user(username, &user)) {
            g_auth.stats.failed_logins++;
            g_auth.stats.last_failed_login_time = get_current_time_ms();
            update_rate_limit(false);
            result = AUTH_RESULT_INVALID_CREDENTIALS;
            printf(TIMESTAMP_FORMAT "%s: Invalid username: %s\n",
                   FORMAT_TIMESTAMP(timestamp), TAG, username);
            goto cleanup;
        }

        // Validate password
        if (strcmp(password, user.password) != 0) {
            g_auth.stats.failed_logins++;
            g_auth.stats.last_failed_login_time = get_current_time_ms();
            update_rate_limit(false);
            result = AUTH_RESULT_INVALID_CREDENTIALS;
            printf(TIMESTAMP_FORMAT "%s: Invalid password for user: %s\n",
                   FORMAT_TIMESTAMP(timestamp), TAG, username);
            goto cleanup;
        }

        // Check if user is enabled
        if (!user.is_enabled) {
            g_auth.stats.failed_logins++;
            result = AUTH_RESULT_INVALID_CREDENTIALS;
            printf(TIMESTAMP_FORMAT "%s: User disabled: %s\n",
                   FORMAT_TIMESTAMP(timestamp), TAG, username);
            goto cleanup;
        }

        // Find free session slot
        int session_index = find_free_session_slot();
        if (session_index < 0) {
            result = AUTH_RESULT_MAX_SESSIONS;
            printf(TIMESTAMP_FORMAT "%s: Maximum concurrent sessions reached for user: %s\n",
                   FORMAT_TIMESTAMP(timestamp), TAG, username);
            goto cleanup;
        }

        // Generate session token
        generate_session_token(session_token);

        // Create session
        auth_session_info_t *session = &g_auth.sessions[session_index];
        strncpy(session->session_token, session_token, AUTH_SESSION_TOKEN_LENGTH);
        session->session_token[AUTH_SESSION_TOKEN_LENGTH] = '\0';
        strncpy(session->username, username, AUTH_USERNAME_MAX_LENGTH);
        session->username[AUTH_USERNAME_MAX_LENGTH] = '\0';
        session->role = user.role;
        session->created_time = get_current_time_ms();
        session->last_activity = session->created_time;
        session->request_count = 0;
        session->is_active = true;

        // Update statistics
        g_auth.stats.successful_logins++;
        g_auth.stats.sessions_created++;
        g_auth.stats.active_sessions++;
        g_auth.stats.last_login_time = session->created_time;
        update_rate_limit(true);

        *role = user.role;
        result = AUTH_RESULT_SUCCESS;

        printf(TIMESTAMP_FORMAT "%s: User logged in successfully: %s (role: %s, session: %.8s...)\n",
               FORMAT_TIMESTAMP(timestamp), TAG, username, 
               auth_manager_role_to_string(user.role), session_token);

cleanup:
        xSemaphoreGive(g_auth.mutex);
    }

    return result;
}

auth_result_t auth_manager_validate_session(const char *session_token,
                                           auth_session_info_t *session_info)
{
    if (!g_auth.initialized || session_token == NULL) {
        return AUTH_RESULT_SYSTEM_ERROR;
    }

    auth_result_t result = AUTH_RESULT_SYSTEM_ERROR;

    if (xSemaphoreTake(g_auth.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Clean up expired sessions first
        auth_manager_cleanup_expired_sessions();

        int session_index = find_session_index(session_token);
        if (session_index < 0) {
            result = AUTH_RESULT_SESSION_NOT_FOUND;
            goto cleanup;
        }

        auth_session_info_t *session = &g_auth.sessions[session_index];
        
        // Check if session has expired
        uint64_t current_time = get_current_time_ms();
        if (current_time - session->last_activity > g_auth.config.session_timeout_ms) {
            cleanup_session_at_index(session_index);
            result = AUTH_RESULT_SESSION_EXPIRED;
            goto cleanup;
        }

        // Update last activity
        session->last_activity = current_time;
        session->request_count++;

        // Copy session info if requested
        if (session_info != NULL) {
            memcpy(session_info, session, sizeof(auth_session_info_t));
        }

        result = AUTH_RESULT_SUCCESS;

cleanup:
        xSemaphoreGive(g_auth.mutex);
    }

    return result;
}

auth_result_t auth_manager_check_role(const char *session_token,
                                     auth_role_t required_role)
{
    auth_session_info_t session_info;
    auth_result_t result = auth_manager_validate_session(session_token, &session_info);
    
    if (result != AUTH_RESULT_SUCCESS) {
        return result;
    }

    if (session_info.role < required_role) {
        uint32_t timestamp = GET_TIMESTAMP();
        printf(TIMESTAMP_FORMAT "%s: Insufficient role for user %s: has %s, requires %s\n",
               FORMAT_TIMESTAMP(timestamp), TAG, session_info.username,
               auth_manager_role_to_string(session_info.role),
               auth_manager_role_to_string(required_role));
        return AUTH_RESULT_INVALID_ROLE;
    }

    return AUTH_RESULT_SUCCESS;
}

auth_result_t auth_manager_logout(const char *session_token)
{
    if (!g_auth.initialized || session_token == NULL) {
        return AUTH_RESULT_SYSTEM_ERROR;
    }

    uint32_t timestamp = GET_TIMESTAMP();
    auth_result_t result = AUTH_RESULT_SYSTEM_ERROR;

    if (xSemaphoreTake(g_auth.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int session_index = find_session_index(session_token);
        if (session_index < 0) {
            result = AUTH_RESULT_SESSION_NOT_FOUND;
            goto cleanup;
        }

        auth_session_info_t *session = &g_auth.sessions[session_index];
        printf(TIMESTAMP_FORMAT "%s: User logged out: %s (session: %.8s...)\n",
               FORMAT_TIMESTAMP(timestamp), TAG, session->username, session_token);

        cleanup_session_at_index(session_index);
        g_auth.stats.sessions_invalidated++;
        result = AUTH_RESULT_SUCCESS;

cleanup:
        xSemaphoreGive(g_auth.mutex);
    }

    return result;
}

auth_result_t auth_manager_get_session_info(const char *session_token,
                                           auth_session_info_t *session_info)
{
    if (session_info == NULL) {
        return AUTH_RESULT_SYSTEM_ERROR;
    }

    return auth_manager_validate_session(session_token, session_info);
}

bool auth_manager_get_stats(auth_stats_t *stats)
{
    if (!g_auth.initialized || stats == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_auth.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Update active session count
        g_auth.stats.active_sessions = 0;
        for (int i = 0; i < AUTH_MAX_CONCURRENT_SESSIONS; i++) {
            if (g_auth.sessions[i].is_active) {
                g_auth.stats.active_sessions++;
            }
        }

        memcpy(stats, &g_auth.stats, sizeof(auth_stats_t));
        xSemaphoreGive(g_auth.mutex);
        return true;
    }

    return false;
}

void auth_manager_reset_stats(void)
{
    if (!g_auth.initialized) {
        return;
    }

    if (xSemaphoreTake(g_auth.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t active_sessions = g_auth.stats.active_sessions;
        memset(&g_auth.stats, 0, sizeof(auth_stats_t));
        g_auth.stats.active_sessions = active_sessions;  // Preserve active session count
        xSemaphoreGive(g_auth.mutex);
    }
}

uint32_t auth_manager_cleanup_expired_sessions(void)
{
    if (!g_auth.initialized) {
        return 0;
    }

    uint32_t cleaned_up = 0;
    uint64_t current_time = get_current_time_ms();

    // Only cleanup if enough time has passed since last cleanup
    if (current_time - g_auth.last_cleanup_time < AUTH_SESSION_CLEANUP_INTERVAL_MS) {
        return 0;
    }

    for (int i = 0; i < AUTH_MAX_CONCURRENT_SESSIONS; i++) {
        if (g_auth.sessions[i].is_active) {
            if (current_time - g_auth.sessions[i].last_activity > g_auth.config.session_timeout_ms) {
                uint32_t timestamp = GET_TIMESTAMP();
                printf(TIMESTAMP_FORMAT "%s: Session expired for user: %s (session: %.8s...)\n",
                       FORMAT_TIMESTAMP(timestamp), TAG, 
                       g_auth.sessions[i].username, g_auth.sessions[i].session_token);
                
                cleanup_session_at_index(i);
                g_auth.stats.sessions_expired++;
                cleaned_up++;
            }
        }
    }

    g_auth.last_cleanup_time = current_time;
    return cleaned_up;
}

bool auth_manager_get_active_sessions(auth_session_info_t *sessions,
                                     uint32_t max_sessions,
                                     uint32_t *session_count)
{
    if (!g_auth.initialized || sessions == NULL || session_count == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_auth.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *session_count = 0;
        
        for (int i = 0; i < AUTH_MAX_CONCURRENT_SESSIONS && *session_count < max_sessions; i++) {
            if (g_auth.sessions[i].is_active) {
                memcpy(&sessions[*session_count], &g_auth.sessions[i], sizeof(auth_session_info_t));
                (*session_count)++;
            }
        }

        xSemaphoreGive(g_auth.mutex);
        return true;
    }

    return false;
}

bool auth_manager_configure(const auth_config_t *config)
{
    if (!g_auth.initialized || config == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_auth.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(&g_auth.config, config, sizeof(auth_config_t));
        xSemaphoreGive(g_auth.mutex);
        
        ESP_LOGI(TAG, "Authentication configuration updated");
        return true;
    }

    return false;
}

bool auth_manager_get_config(auth_config_t *config)
{
    if (!g_auth.initialized || config == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_auth.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(config, &g_auth.config, sizeof(auth_config_t));
        xSemaphoreGive(g_auth.mutex);
        return true;
    }

    return false;
}

void auth_manager_print_status(void)
{
    if (!g_auth.initialized) {
        return;
    }

    uint32_t timestamp = GET_TIMESTAMP();
    auth_stats_t stats;
    
    printf(TIMESTAMP_FORMAT "%s: === AUTHENTICATION MANAGER STATUS ===\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
    
    if (auth_manager_get_stats(&stats)) {
        printf(TIMESTAMP_FORMAT "%s: Total Login Attempts: %lu (Success: %lu, Failed: %lu)\n",
               FORMAT_TIMESTAMP(timestamp), TAG, 
               (unsigned long)stats.total_login_attempts,
               (unsigned long)stats.successful_logins,
               (unsigned long)stats.failed_logins);
        
        printf(TIMESTAMP_FORMAT "%s: Active Sessions: %lu/%d\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               (unsigned long)stats.active_sessions, AUTH_MAX_CONCURRENT_SESSIONS);
        
        printf(TIMESTAMP_FORMAT "%s: Sessions Created: %lu, Expired: %lu, Invalidated: %lu\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               (unsigned long)stats.sessions_created,
               (unsigned long)stats.sessions_expired,
               (unsigned long)stats.sessions_invalidated);
        
        printf(TIMESTAMP_FORMAT "%s: Rate Limit Hits: %lu\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               (unsigned long)stats.rate_limit_hits);
    }
    
    printf(TIMESTAMP_FORMAT "%s: Hardcoded Users: %lu/%d\n",
           FORMAT_TIMESTAMP(timestamp), TAG,
           (unsigned long)g_auth.hardcoded_user_count, AUTH_MAX_HARDCODED_USERS);
    
    printf(TIMESTAMP_FORMAT "%s: =====================================\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
}

const char* auth_manager_result_to_string(auth_result_t result)
{
    switch (result) {
        case AUTH_RESULT_SUCCESS: return "SUCCESS";
        case AUTH_RESULT_INVALID_CREDENTIALS: return "INVALID_CREDENTIALS";
        case AUTH_RESULT_SESSION_EXPIRED: return "SESSION_EXPIRED";
        case AUTH_RESULT_SESSION_NOT_FOUND: return "SESSION_NOT_FOUND";
        case AUTH_RESULT_RATE_LIMITED: return "RATE_LIMITED";
        case AUTH_RESULT_MAX_SESSIONS: return "MAX_SESSIONS";
        case AUTH_RESULT_INVALID_ROLE: return "INVALID_ROLE";
        case AUTH_RESULT_SYSTEM_ERROR: return "SYSTEM_ERROR";
        default: return "UNKNOWN";
    }
}

const char* auth_manager_role_to_string(auth_role_t role)
{
    switch (role) {
        case AUTH_ROLE_NONE: return "NONE";
        case AUTH_ROLE_VIEWER: return "VIEWER";
        case AUTH_ROLE_MANAGER: return "MANAGER";
        case AUTH_ROLE_OWNER: return "OWNER";
        default: return "UNKNOWN";
    }
}

bool auth_manager_add_hardcoded_user(const char *username,
                                     const char *password,
                                     auth_role_t role)
{
    if (!g_auth.initialized || username == NULL || password == NULL) {
        return false;
    }

    if (strlen(username) > AUTH_USERNAME_MAX_LENGTH || 
        strlen(password) > AUTH_PASSWORD_MAX_LENGTH) {
        return false;
    }

    if (xSemaphoreTake(g_auth.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (g_auth.hardcoded_user_count >= AUTH_MAX_HARDCODED_USERS) {
            xSemaphoreGive(g_auth.mutex);
            return false;
        }

        // Check if user already exists
        for (uint32_t i = 0; i < g_auth.hardcoded_user_count; i++) {
            if (strcmp(g_auth.hardcoded_users[i].username, username) == 0) {
                xSemaphoreGive(g_auth.mutex);
                return false;  // User already exists
            }
        }

        // Add new user
        auth_user_t *user = &g_auth.hardcoded_users[g_auth.hardcoded_user_count];
        strncpy(user->username, username, AUTH_USERNAME_MAX_LENGTH);
        user->username[AUTH_USERNAME_MAX_LENGTH] = '\0';
        strncpy(user->password, password, AUTH_PASSWORD_MAX_LENGTH);
        user->password[AUTH_PASSWORD_MAX_LENGTH] = '\0';
        user->role = role;
        user->is_enabled = true;

        g_auth.hardcoded_user_count++;
        xSemaphoreGive(g_auth.mutex);

        uint32_t timestamp = GET_TIMESTAMP();
        printf(TIMESTAMP_FORMAT "%s: Added hardcoded user: %s (role: %s)\n",
               FORMAT_TIMESTAMP(timestamp), TAG, username, auth_manager_role_to_string(role));
        
        return true;
    }

    return false;
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static void generate_session_token(char *token)
{
    // Generate 16 random bytes and convert to hex string
    uint8_t random_bytes[16];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    
    for (int i = 0; i < 16; i++) {
        sprintf(&token[i * 2], "%02x", random_bytes[i]);
    }
    token[AUTH_SESSION_TOKEN_LENGTH] = '\0';
}

static bool find_user(const char *username, auth_user_t *user)
{
    for (uint32_t i = 0; i < g_auth.hardcoded_user_count; i++) {
        if (strcmp(g_auth.hardcoded_users[i].username, username) == 0) {
            if (user != NULL) {
                memcpy(user, &g_auth.hardcoded_users[i], sizeof(auth_user_t));
            }
            return true;
        }
    }
    return false;
}

static int find_session_index(const char *session_token)
{
    for (int i = 0; i < AUTH_MAX_CONCURRENT_SESSIONS; i++) {
        if (g_auth.sessions[i].is_active && 
            strcmp(g_auth.sessions[i].session_token, session_token) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_free_session_slot(void)
{
    for (int i = 0; i < AUTH_MAX_CONCURRENT_SESSIONS; i++) {
        if (!g_auth.sessions[i].is_active) {
            return i;
        }
    }
    return -1;
}

static void cleanup_session_at_index(int index)
{
    if (index >= 0 && index < AUTH_MAX_CONCURRENT_SESSIONS) {
        memset(&g_auth.sessions[index], 0, sizeof(auth_session_info_t));
        if (g_auth.stats.active_sessions > 0) {
            g_auth.stats.active_sessions--;
        }
    }
}

static bool is_rate_limited(void)
{
    uint64_t current_time = get_current_time_ms();
    
    // Reset rate limit window if expired
    if (current_time >= g_auth.rate_limit_reset_time) {
        g_auth.failed_login_attempts = 0;
        g_auth.rate_limit_reset_time = current_time + g_auth.config.rate_limit_window_ms;
        return false;
    }
    
    return g_auth.failed_login_attempts >= g_auth.config.max_login_attempts;
}

static void update_rate_limit(bool successful_login)
{
    if (successful_login) {
        g_auth.failed_login_attempts = 0;  // Reset on successful login
    } else {
        g_auth.failed_login_attempts++;
    }
}

static uint64_t get_current_time_ms(void)
{
    return esp_timer_get_time() / 1000ULL;
}
