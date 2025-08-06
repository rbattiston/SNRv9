/**
 * @file io_test_controller.c
 * @brief IO Test Controller implementation for SNRv9 Irrigation Control System
 */

#include "io_test_controller.h"
#include "debug_config.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char* TAG = "IO_TEST_CTRL";

// Global IO manager reference
static io_manager_t* g_io_manager = NULL;

/**
 * @brief Parse point ID from URI
 */
static esp_err_t parse_point_id_from_uri(const char* uri, char* point_id, size_t max_len) {
    // URI format: /api/io/points/{point_id} or /api/io/points/{point_id}/set
    const char* prefix = "/api/io/points/";
    size_t prefix_len = strlen(prefix);
    
    if (strncmp(uri, prefix, prefix_len) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char* id_start = uri + prefix_len;
    const char* id_end = strchr(id_start, '/');  // Look for next '/' (for /set)
    if (!id_end) {
        id_end = strchr(id_start, '?');  // Look for query parameters
    }
    if (!id_end) {
        id_end = id_start + strlen(id_start);  // End of string
    }
    
    size_t id_len = id_end - id_start;
    if (id_len >= max_len || id_len == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    strncpy(point_id, id_start, id_len);
    point_id[id_len] = '\0';
    
    return ESP_OK;
}

/**
 * @brief Convert IO point type to string
 */
static const char* io_point_type_to_string(io_point_type_t type) {
    switch (type) {
        case IO_POINT_TYPE_GPIO_AI: return "GPIO_AI";
        case IO_POINT_TYPE_GPIO_BI: return "GPIO_BI";
        case IO_POINT_TYPE_GPIO_BO: return "GPIO_BO";
        case IO_POINT_TYPE_SHIFT_REG_BI: return "SHIFT_REG_BI";
        case IO_POINT_TYPE_SHIFT_REG_BO: return "SHIFT_REG_BO";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Convert BO type to string
 */
static const char* bo_type_to_string(bo_type_t type) {
    switch (type) {
        case BO_TYPE_SOLENOID: return "SOLENOID";
        case BO_TYPE_LIGHTING: return "LIGHTING";
        case BO_TYPE_PUMP: return "PUMP";
        case BO_TYPE_FAN: return "FAN";
        case BO_TYPE_HEATER: return "HEATER";
        case BO_TYPE_GENERIC: return "GENERIC";
        default: return "UNKNOWN";
    }
}

esp_err_t io_test_get_all_points(httpd_req_t *req) {
    if (!g_io_manager) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "IO Manager not initialized", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Get all point IDs
    char point_ids[32][CONFIG_MAX_ID_LENGTH];
    int point_count = 0;
    
    esp_err_t ret = io_manager_get_all_point_ids(g_io_manager, point_ids, 32, &point_count);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed to get IO points", HTTPD_RESP_USE_STRLEN);
        return ret;
    }
    
    // Create JSON response
    cJSON *json = cJSON_CreateObject();
    cJSON *points_array = cJSON_CreateArray();
    
    for (int i = 0; i < point_count; i++) {
        // Get point configuration
        io_point_config_t config;
        ret = config_manager_get_io_point_config(g_io_manager->config_manager, point_ids[i], &config);
        if (ret != ESP_OK) continue;
        
        // Get runtime state
        io_point_runtime_state_t state;
        ret = io_manager_get_runtime_state(g_io_manager, point_ids[i], &state);
        if (ret != ESP_OK) continue;
        
        // Create point object
        cJSON *point = cJSON_CreateObject();
        cJSON_AddStringToObject(point, "id", config.id);
        cJSON_AddStringToObject(point, "name", config.name);
        cJSON_AddStringToObject(point, "description", config.description);
        cJSON_AddStringToObject(point, "type", io_point_type_to_string(config.type));
        cJSON_AddNumberToObject(point, "pin", config.pin);
        cJSON_AddNumberToObject(point, "chipIndex", config.chip_index);
        cJSON_AddNumberToObject(point, "bitIndex", config.bit_index);
        cJSON_AddBoolToObject(point, "isInverted", config.is_inverted);
        
        // Add BO specific fields
        if (config.type == IO_POINT_TYPE_GPIO_BO || config.type == IO_POINT_TYPE_SHIFT_REG_BO) {
            cJSON_AddStringToObject(point, "boType", bo_type_to_string(config.bo_type));
            cJSON_AddNumberToObject(point, "flowRateMLPerSecond", config.flow_rate_ml_per_second);
            cJSON_AddBoolToObject(point, "isCalibrated", config.is_calibrated);
        }
        
        // Add runtime state
        cJSON *runtime = cJSON_CreateObject();
        cJSON_AddNumberToObject(runtime, "rawValue", state.raw_value);
        cJSON_AddNumberToObject(runtime, "conditionedValue", state.conditioned_value);
        cJSON_AddBoolToObject(runtime, "digitalState", state.digital_state);
        cJSON_AddBoolToObject(runtime, "errorState", state.error_state);
        cJSON_AddNumberToObject(runtime, "lastUpdateTime", (double)state.last_update_time);
        cJSON_AddNumberToObject(runtime, "updateCount", state.update_count);
        cJSON_AddNumberToObject(runtime, "errorCount", state.error_count);
        cJSON_AddBoolToObject(runtime, "alarmActive", state.alarm_active);
        cJSON_AddItemToObject(point, "runtime", runtime);
        
        cJSON_AddItemToArray(points_array, point);
    }
    
    cJSON_AddItemToObject(json, "points", points_array);
    cJSON_AddNumberToObject(json, "totalCount", point_count);
    cJSON_AddStringToObject(json, "status", "success");
    
    // Send response
    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    // Cleanup
    free(json_string);
    cJSON_Delete(json);
    
    return ESP_OK;
}

esp_err_t io_test_get_point(httpd_req_t *req) {
    if (!g_io_manager) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "IO Manager not initialized", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Parse point ID from URI
    char point_id[CONFIG_MAX_ID_LENGTH];
    esp_err_t ret = parse_point_id_from_uri(req->uri, point_id, sizeof(point_id));
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid point ID", HTTPD_RESP_USE_STRLEN);
        return ret;
    }
    
    // Get point configuration
    io_point_config_t config;
    ret = config_manager_get_io_point_config(g_io_manager->config_manager, point_id, &config);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Point not found", HTTPD_RESP_USE_STRLEN);
        return ret;
    }
    
    // Get runtime state
    io_point_runtime_state_t state;
    ret = io_manager_get_runtime_state(g_io_manager, point_id, &state);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed to get runtime state", HTTPD_RESP_USE_STRLEN);
        return ret;
    }
    
    // Create JSON response
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "id", config.id);
    cJSON_AddStringToObject(json, "name", config.name);
    cJSON_AddStringToObject(json, "description", config.description);
    cJSON_AddStringToObject(json, "type", io_point_type_to_string(config.type));
    cJSON_AddNumberToObject(json, "pin", config.pin);
    cJSON_AddNumberToObject(json, "chipIndex", config.chip_index);
    cJSON_AddNumberToObject(json, "bitIndex", config.bit_index);
    cJSON_AddBoolToObject(json, "isInverted", config.is_inverted);
    
    // Add BO specific fields
    if (config.type == IO_POINT_TYPE_GPIO_BO || config.type == IO_POINT_TYPE_SHIFT_REG_BO) {
        cJSON_AddStringToObject(json, "boType", bo_type_to_string(config.bo_type));
        cJSON_AddNumberToObject(json, "flowRateMLPerSecond", config.flow_rate_ml_per_second);
        cJSON_AddBoolToObject(json, "isCalibrated", config.is_calibrated);
    }
    
    // Add runtime state
    cJSON *runtime = cJSON_CreateObject();
    cJSON_AddNumberToObject(runtime, "rawValue", state.raw_value);
    cJSON_AddNumberToObject(runtime, "conditionedValue", state.conditioned_value);
    cJSON_AddBoolToObject(runtime, "digitalState", state.digital_state);
    cJSON_AddBoolToObject(runtime, "errorState", state.error_state);
    cJSON_AddNumberToObject(runtime, "lastUpdateTime", (double)state.last_update_time);
    cJSON_AddNumberToObject(runtime, "updateCount", state.update_count);
    cJSON_AddNumberToObject(runtime, "errorCount", state.error_count);
    cJSON_AddBoolToObject(runtime, "alarmActive", state.alarm_active);
    cJSON_AddItemToObject(json, "runtime", runtime);
    
    cJSON_AddStringToObject(json, "status", "success");
    
    // Send response
    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    // Cleanup
    free(json_string);
    cJSON_Delete(json);
    
    return ESP_OK;
}

esp_err_t io_test_set_output(httpd_req_t *req) {
    if (!g_io_manager) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "IO Manager not initialized", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Parse point ID from URI
    char point_id[CONFIG_MAX_ID_LENGTH];
    esp_err_t ret = parse_point_id_from_uri(req->uri, point_id, sizeof(point_id));
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid point ID", HTTPD_RESP_USE_STRLEN);
        return ret;
    }
    
    // Read request body
    char content[256];
    int content_len = httpd_req_recv(req, content, sizeof(content) - 1);
    if (content_len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid request body", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }
    content[content_len] = '\0';
    
    // Parse JSON
    cJSON *json = cJSON_Parse(content);
    if (!json) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *state_item = cJSON_GetObjectItem(json, "state");
    if (!state_item || !cJSON_IsBool(state_item)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Missing or invalid 'state' field", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }
    
    bool state = cJSON_IsTrue(state_item);
    cJSON_Delete(json);
    
    // Set output state
    ret = io_manager_set_binary_output(g_io_manager, point_id, state);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed to set output state", HTTPD_RESP_USE_STRLEN);
        return ret;
    }
    
    // Create success response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "pointId", point_id);
    cJSON_AddBoolToObject(response, "state", state);
    cJSON_AddStringToObject(response, "message", "Output state updated successfully");
    
    char *json_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    // Cleanup
    free(json_string);
    cJSON_Delete(response);
    
    return ESP_OK;
}

esp_err_t io_test_get_statistics(httpd_req_t *req) {
    if (!g_io_manager) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "IO Manager not initialized", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Get IO manager statistics
    uint32_t update_cycles = 0;
    uint32_t total_errors = 0;
    uint64_t last_update_time = 0;
    
    esp_err_t ret = io_manager_get_statistics(g_io_manager, &update_cycles, &total_errors, &last_update_time);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed to get statistics", HTTPD_RESP_USE_STRLEN);
        return ret;
    }
    
    // Create JSON response
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "success");
    cJSON_AddNumberToObject(json, "updateCycles", update_cycles);
    cJSON_AddNumberToObject(json, "totalErrors", total_errors);
    cJSON_AddNumberToObject(json, "lastUpdateTime", (double)last_update_time);
    cJSON_AddBoolToObject(json, "pollingActive", g_io_manager->polling_task_running);
    cJSON_AddNumberToObject(json, "activePointCount", g_io_manager->active_point_count);
    
    // Send response
    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    // Cleanup
    free(json_string);
    cJSON_Delete(json);
    
    return ESP_OK;
}

esp_err_t io_test_controller_init(io_manager_t* io_manager) {
    if (!io_manager) {
        return ESP_ERR_INVALID_ARG;
    }
    
    g_io_manager = io_manager;
    
    ESP_LOGI(TAG, "IO Test Controller initialized with IO manager reference");
    
    return ESP_OK;
}

esp_err_t io_test_controller_register_routes(httpd_handle_t server) {
    if (!server || !g_io_manager) {
        ESP_LOGE(TAG, "Server handle or IO manager is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting dynamic IO test controller route registration...");

    // Base routes
    httpd_uri_t get_all_points_uri = {
        .uri = "/api/io/points",
        .method = HTTP_GET,
        .handler = io_test_get_all_points,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &get_all_points_uri);
    ESP_LOGI(TAG, "Registered: GET /api/io/points");

    httpd_uri_t get_statistics_uri = {
        .uri = "/api/io/statistics",
        .method = HTTP_GET,
        .handler = io_test_get_statistics,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &get_statistics_uri);
    ESP_LOGI(TAG, "Registered: GET /api/io/statistics");

    // Dynamically register routes for each IO point
    char point_ids[32][CONFIG_MAX_ID_LENGTH];
    int point_count = 0;
    io_manager_get_all_point_ids(g_io_manager, point_ids, 32, &point_count);

    for (int i = 0; i < point_count; i++) {
        io_point_config_t config;
        if (config_manager_get_io_point_config(g_io_manager->config_manager, point_ids[i], &config) == ESP_OK) {
            
            // Register GET /api/io/points/{id}
            char* get_uri = malloc(strlen("/api/io/points/") + strlen(config.id) + 1);
            sprintf(get_uri, "/api/io/points/%s", config.id);
            
            httpd_uri_t get_point_uri = {
                .uri = get_uri,
                .method = HTTP_GET,
                .handler = io_test_get_point,
                .user_ctx = (void*)get_uri // Pass URI to be freed later
            };
            httpd_register_uri_handler(server, &get_point_uri);
            ESP_LOGI(TAG, "Registered: GET %s", get_uri);

            // If it's a binary output, register the POST .../set route
            if (config.type == IO_POINT_TYPE_GPIO_BO || config.type == IO_POINT_TYPE_SHIFT_REG_BO) {
                char* set_uri = malloc(strlen("/api/io/points/") + strlen(config.id) + strlen("/set") + 1);
                sprintf(set_uri, "/api/io/points/%s/set", config.id);

                httpd_uri_t set_output_uri = {
                    .uri = set_uri,
                    .method = HTTP_POST,
                    .handler = io_test_set_output,
                    .user_ctx = (void*)set_uri // Pass URI to be freed later
                };
                httpd_register_uri_handler(server, &set_output_uri);
                ESP_LOGI(TAG, "Registered: POST %s", set_uri);
            }
        }
    }

    ESP_LOGI(TAG, "IO Test Controller routes registered successfully");
    return ESP_OK;
}
