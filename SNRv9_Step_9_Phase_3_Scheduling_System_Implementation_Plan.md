# SNRv9 Step 9 Phase 3: Scheduling System Implementation Plan

## Overview

Phase 3 implements a **real-time irrigation controller** with **to-the-second timing accuracy** using dedicated FreeRTOS tasks. This is a sophisticated system with four event types, binary storage optimization, and comprehensive recovery capabilities that integrates seamlessly with the existing SNRv9 infrastructure.

## Current Status

**Foundation Complete**:
- ✅ **Phase 1**: PSRAM Infrastructure Extension (August 7, 2025)
- ✅ **Phase 2**: Time Management System (August 8, 2025)
- 🔄 **Phase 3**: Scheduling System (READY TO IMPLEMENT)

**Dependencies Satisfied**:
- ✅ Time Management System provides reliable time source
- ✅ PSRAM Manager extended with `PSRAM_ALLOC_SCHEDULING` category (1MB allocation)
- ✅ IO Manager provides BO control and calibration data
- ✅ Component architecture supports new scheduling components
- ✅ Build system patterns established for integration

## Implementation Structure

### **Phase 3A: Core Schedule Manager Component (Week 1)**

#### **3A.1 Create Core Component Structure**
**New Files to Create**:
- `components/core/schedule_manager.c`
- `components/core/include/schedule_manager.h`
- `components/core/CMakeLists.txt` (update)

#### **3A.2 Data Structures Implementation**

**Core Schedule Instance Structure**:
```c
typedef struct {
    char id[32];                 // Unique instance ID
    char template_id[32];        // Source template ID (if any)
    char bo_id[16];              // Target Binary Output ID
    char start_date[11];         // YYYY-MM-DD format
    char end_date[11];           // YYYY-MM-DD format
    int priority;                // Lower number = higher priority
    char lights_on_time[6];      // HH:MM format
    char lights_off_time[6];     // HH:MM format
    
    // Event arrays for all 4 event types
    duration_autopilot_window_t *duration_autopilot_windows;
    size_t duration_autopilot_count;
    volume_autopilot_window_t *volume_autopilot_windows;
    size_t volume_autopilot_count;
    duration_prescheduled_event_t *duration_prescheduled_events;
    size_t duration_prescheduled_count;
    volume_prescheduled_event_t *volume_prescheduled_events;
    size_t volume_prescheduled_count;
    
    uint32_t version;            // For optimistic locking
    uint32_t crc32;              // Data integrity check
} schedule_instance_t;
```

**Four Event Types**:
1. **Duration AutoPilot**: Sensor-driven watering for specified duration
2. **Volume AutoPilot**: Sensor-driven watering for specified volume
3. **Duration Prescheduled**: Time-driven watering for specified duration
4. **Volume Prescheduled**: Time-driven watering for specified volume

#### **3A.3 PSRAM Integration**
**PSRAM Allocation Strategy** (1MB total):
```c
esp_err_t schedule_manager_init(void) {
    // Schedule entries array in PSRAM (600KB)
    schedules = psram_manager_allocate_for_category(PSRAM_ALLOC_SCHEDULING,
                                                   sizeof(schedule_entry_t) * MAX_SCHEDULES,
                                                   (void**)&schedules);
    
    // Cron parsing context in PSRAM (200KB)
    cron_context = psram_manager_allocate_for_category(PSRAM_ALLOC_SCHEDULING,
                                                      sizeof(cron_parser_context_t),
                                                      (void**)&cron_context);
    
    // AutoPilot calculation buffers in PSRAM (200KB)
    autopilot_ctx = psram_manager_allocate_for_category(PSRAM_ALLOC_SCHEDULING,
                                                       sizeof(autopilot_context_t),
                                                       (void**)&autopilot_ctx);
    
    // Fallback to RAM if PSRAM allocation fails
    if (!schedules || !cron_context || !autopilot_ctx) {
        ESP_LOGW(TAG, "PSRAM allocation failed, using RAM fallback");
        // Implement fallback to regular malloc
    }
    
    return ESP_OK;
}
```

#### **3A.4 Binary Storage System**
**Key Features**:
- Custom `.schb` format (60-80% smaller than JSON)
- CRC32 checksums using ESP-IDF's `esp_crc` component
- Version headers for optimistic locking
- LittleFS integration for persistence

**Storage Locations**:
- Templates: `/data/schedule_templates/`
- Instances: `/data/schedule_instances/`

**Binary Storage Implementation**:
```c
typedef struct {
    char magic[4];               // 'SCHB'
    uint16_t version;            // Format version
    uint32_t data_size;          // Size of data section
    uint32_t crc32;              // CRC32 of data section
    uint8_t reserved[16];        // Future expansion
} __attribute__((packed)) schedule_binary_header_t;
```

#### **3A.5 Core API Functions**
```c
// Template Management
esp_err_t schedule_manager_save_template(const schedule_instance_t *template);
esp_err_t schedule_manager_load_template(const char *template_id, schedule_instance_t *template);
esp_err_t schedule_manager_delete_template(const char *template_id);
esp_err_t schedule_manager_list_templates(char ***template_ids, size_t *count);

// Instance Management
esp_err_t schedule_manager_save_instance(const schedule_instance_t *instance);
esp_err_t schedule_manager_load_instance(const char *instance_id, schedule_instance_t *instance);
esp_err_t schedule_manager_delete_instance(const char *instance_id);
esp_err_t schedule_manager_get_active_instance_for_bo(const char *bo_id, const char *current_date, schedule_instance_t *instance);

// Utility Functions
esp_err_t schedule_manager_validate_instance(const schedule_instance_t *instance);
esp_err_t schedule_manager_calculate_volume_duration(const char *bo_id, float volume_ml, int *duration_seconds);
bool schedule_manager_is_time_in_window(const char *current_time, const char *start_time, const char *end_time);
```

### **Phase 3B: Schedule Executor Component (Week 2)**

#### **3B.1 Create Executor Component**
**New Files to Create**:
- `components/core/schedule_executor.c`
- `components/core/include/schedule_executor.h`

#### **3B.2 Core Execution Engine**
**Main Features**:
- **60-second evaluation cycle** using dedicated FreeRTOS task
- **Priority-based conflict resolution** when multiple instances overlap
- **Dry run mode** for testing without hardware activation
- **Self-contained watering tasks** with **to-the-second precision**

**FreeRTOS Task Structure**:
```c
// Main executor task (Priority 3, Stack 4096)
void schedule_executor_task(void *pvParameters) {
    TickType_t last_execution = 0;
    const TickType_t execution_interval = pdMS_TO_TICKS(60 * 1000); // 60 seconds
    
    while (1) {
        TickType_t current_tick = xTaskGetTickCount();
        
        // Execute schedules every 60 seconds
        if ((current_tick - last_execution) >= execution_interval) {
            xSemaphoreTake(schedule_executor_mutex, portMAX_DELAY);
            
            if (current_state == SCHEDULE_EXECUTOR_RUNNING || 
                current_state == SCHEDULE_EXECUTOR_DRY_RUN) {
                
                execute_schedules();
                last_execution = current_tick;
            }
            
            xSemaphoreGive(schedule_executor_mutex);
        }
        
        // Yield CPU to prevent watchdog timeout
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 second
    }
}

// Individual watering tasks (Priority 4, Stack 2048)
void watering_task(void *pvParameters) {
    // Self-contained task with to-the-second timing precision
    // Handles precise timing, logging, cleanup
    // Automatic task destruction after completion
}
```

#### **3B.3 Self-Contained Watering Tasks**
**Key Innovation**: Each watering event gets its own FreeRTOS task

**Benefits**:
- **Precise Timing**: To-the-second accuracy using FreeRTOS `vTaskDelay()`
- **Isolation**: Task failures don't affect other operations  
- **Resource Management**: Automatic cleanup prevents memory leaks

**Watering Task Implementation**:
```c
void watering_task(void *pvParameters) {
    watering_task_params_t *params = (watering_task_params_t *)pvParameters;
    
    ESP_LOGI(TAG, "Watering task started for BO %s, duration: %d seconds", 
             params->bo_id, params->duration_seconds);
    
    // 1. Immediately open the BO (unless dry run mode)
    if (!dry_run_mode) {
        esp_err_t err = io_manager_set_bo_state(params->bo_id, true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to activate BO %s: %s", params->bo_id, esp_err_to_name(err));
            goto cleanup;
        }
    }
    
    // 2. Log start event
    event_logger_log_watering_start(params->bo_id, params->event_type, params->duration_seconds);
    
    // 3. Wait for exact duration (to-the-second precision)
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t duration_ticks = pdMS_TO_TICKS(params->duration_seconds * 1000);
    vTaskDelay(duration_ticks);
    
    // 4. Close the BO (unless dry run mode)
    if (!dry_run_mode) {
        esp_err_t err = io_manager_set_bo_state(params->bo_id, false);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deactivate BO %s: %s", params->bo_id, esp_err_to_name(err));
        }
    }
    
    // 5. Log end event with actual duration
    TickType_t end_tick = xTaskGetTickCount();
    uint32_t actual_duration_ms = pdTICKS_TO_MS(end_tick - start_tick);
    event_logger_log_watering_end(params->bo_id, params->event_type, actual_duration_ms);
    
    // 6. Handle AutoPilot settling period if needed
    if (params->is_autopilot && params->settling_time_minutes > 0) {
        add_to_settling_set(params->bo_id, params->settling_time_minutes);
    }

cleanup:
    // 7. Notify ScheduleExecutor and cleanup
    schedule_executor_notify_watering_complete(params->bo_id);
    watering_task_params_pool_release(params);
    vTaskDelete(NULL); // Self-destruct
}
```

**Memory Pool Management**:
```c
// Task parameter pool (8 concurrent watering tasks max)
#define WATERING_TASK_POOL_SIZE 8

typedef struct {
    watering_task_params_t pool[WATERING_TASK_POOL_SIZE];
    bool in_use[WATERING_TASK_POOL_SIZE];
    SemaphoreHandle_t mutex;
} watering_task_params_pool_t;
```

#### **3B.4 Schedule Evaluation Logic**
**Priority-Based Conflict Resolution**:
```c
esp_err_t schedule_manager_get_active_instance_for_bo(const char *bo_id, const char *current_date, schedule_instance_t *instance) {
    // Get all instances for this BO
    // Filter instances that cover current date
    // Sort by priority (lower number = higher priority)
    // Return highest priority instance
}
```

**Four Event Type Processing**:
1. **AutoPilot Windows**: Check sensor values against trigger setpoints
2. **Prescheduled Events**: Check current time against event start times
3. **Lighting Control**: Continuous state management for photoperiods
4. **Volume Conversion**: Convert volume events to duration using BO calibration

### **Phase 3C: Event Logging & Recovery System (Week 2)**

#### **3C.1 Event Logger Integration**
**New Files to Create** (if not exists):
- `components/core/event_logger.c`
- `components/core/include/event_logger.h`

**Event Types for Scheduling**:
```c
typedef enum {
    LOG_EVENT_PRESCHEDULED_WATER_START = 0,
    LOG_EVENT_PRESCHEDULED_WATER_END = 1,
    LOG_EVENT_AUTOPILOT_WATER_START = 2,
    LOG_EVENT_AUTOPILOT_WATER_END = 3,
    LOG_EVENT_MANUAL_WATER_START = 4,
    LOG_EVENT_MANUAL_WATER_END = 5,
    LOG_EVENT_RECOVERY_WATER_START = 6,
    LOG_EVENT_RECOVERY_WATER_END = 7,
    LOG_EVENT_LIGHTS_ON = 8,
    LOG_EVENT_LIGHTS_OFF = 9,
    LOG_EVENT_SCHEDULE_EXECUTION_START = 10,
    LOG_EVENT_SCHEDULE_EXECUTION_END = 11,
    LOG_EVENT_AUTOPILOT_SETTLING_START = 12,
    LOG_EVENT_AUTOPILOT_SETTLING_END = 13
} log_event_type_t;
```

#### **3C.2 Power Outage Recovery**
**Critical Feature**: Automatic restart of interrupted irrigation

**Recovery Implementation**:
```c
esp_err_t schedule_executor_perform_recovery_check(void) {
    ESP_LOGI(TAG, "Performing power outage recovery check");
    
    // Get all configured BOs
    char **bo_ids = NULL;
    size_t bo_count = 0;
    esp_err_t err = io_manager_get_bo_list(&bo_ids, &bo_count);
    
    // Check each BO for incomplete events
    for (size_t i = 0; i < bo_count; i++) {
        log_entry_t *incomplete_events = NULL;
        size_t incomplete_count = 0;
        
        err = event_logger_find_incomplete_events(bo_ids[i], &incomplete_events, &incomplete_count);
        if (err == ESP_OK && incomplete_count > 0) {
            // Process incomplete events
            for (size_t j = 0; j < incomplete_count; j++) {
                int remaining_seconds = 0;
                err = event_logger_calculate_remaining_duration(&incomplete_events[j], &remaining_seconds);
                
                if (err == ESP_OK && remaining_seconds > 30) { // Minimum threshold
                    // Create recovery watering task
                    spawn_recovery_watering_task(bo_ids[i], remaining_seconds);
                }
            }
        }
        
        // Yield CPU to prevent watchdog timeout
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    return ESP_OK;
}
```

### **Phase 3D: Time Management Integration (Week 2)**

#### **3D.1 Time Manager Dependency**
**Integration Points**:
- Use existing `time_manager_now()` for current time
- Leverage timezone support for accurate scheduling
- Integrate with NTP reliability system for time validation

**Time Utilities**:
```c
// Current time formatting using Time Manager
static char* get_current_time_string(void) {
    time_t current_time = time_manager_now();
    struct tm *tm_info = localtime(&current_time);
    
    static char time_str[6];
    strftime(time_str, sizeof(time_str), "%H:%M", tm_info);
    return time_str;
}

static char* get_current_date_string(void) {
    time_t current_time = time_manager_now();
    struct tm *tm_info = localtime(&current_time);
    
    static char date_str[11];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", tm_info);
    return date_str;
}

// Time window checking
bool schedule_manager_is_time_in_window(const char *current_time, 
                                       const char *start_time, 
                                       const char *end_time) {
    // Parse time strings and check if current time falls within window
    // Handle overnight windows (e.g., 22:00 to 06:00)
}
```

### **Phase 3E: Volume-to-Duration Conversion (Week 3)**

#### **3E.1 BO Calibration Integration**
**Key Feature**: Convert volume events to duration using BO calibration data

**Calibration Data Structure**:
```c
typedef struct {
    float ml_h2o_per_second_per_plant;    // Direct calibration (preferred)
    float lph_per_emitter_flow;           // Component-based fallback
    int num_emitters_per_plant;
    float flow_rate_ml_per_second;        // Calculated field
    bool is_calibrated;
    char calibration_notes[64];
    time_t calibration_date;
} io_point_calibration_t;
```

**Volume Conversion Implementation**:
```c
esp_err_t schedule_manager_calculate_volume_duration(const char *bo_id, float volume_ml, int *duration_seconds) {
    io_point_config_t bo_config;
    esp_err_t err = io_manager_get_point_config(bo_id, &bo_config);
    if (err != ESP_OK) {
        return err;
    }
    
    if (!bo_config.is_calibrated || bo_config.flow_rate_ml_per_second <= 0.0f) {
        ESP_LOGW(TAG, "BO %s not calibrated for volume calculations", bo_id);
        return ESP_ERR_INVALID_STATE;
    }
    
    *duration_seconds = (int)(volume_ml / bo_config.flow_rate_ml_per_second);
    return ESP_OK;
}
```

**Calibration Calculation**:
```c
esp_err_t calculate_flow_rate(io_point_calibration_t *calibration) {
    // Priority 1: Direct mL/s calibration
    if (calibration->ml_h2o_per_second_per_plant > 0.0f) {
        calibration->flow_rate_ml_per_second = calibration->ml_h2o_per_second_per_plant;
        calibration->is_calibrated = true;
        return ESP_OK;
    }
    
    // Priority 2: Calculate from LPH components
    if (calibration->lph_per_emitter_flow > 0.0f && calibration->num_emitters_per_plant > 0) {
        float total_lph = calibration->lph_per_emitter_flow * calibration->num_emitters_per_plant;
        calibration->flow_rate_ml_per_second = total_lph * 1000.0f / 3600.0f;
        calibration->is_calibrated = true;
        return ESP_OK;
    }
    
    // No valid calibration data
    calibration->is_calibrated = false;
    return ESP_ERR_INVALID_ARG;
}
```

### **Phase 3F: Web API Controller (Week 3)**

#### **3F.1 Schedule Controller Component**
**New Files to Create**:
- `components/web/schedule_controller.c`
- `components/web/include/schedule_controller.h`

#### **3F.2 RESTful API Endpoints**

**Template Management**:
- `GET /api/schedule_templates` - List all templates
- `POST /api/schedule_templates` - Create new template
- `GET /api/schedule_templates/{id}` - Get specific template
- `PUT /api/schedule_templates/{id}` - Update template
- `DELETE /api/schedule_templates/{id}` - Delete template

**Instance Management**:
- `GET /api/schedule_instances` - List all instances
- `POST /api/schedule_instances` - Create new instance
- `GET /api/schedule_instances/{id}` - Get specific instance
- `PUT /api/schedule_instances/{id}` - Update instance
- `DELETE /api/schedule_instances/{id}` - Delete instance

**Execution Control**:
- `GET /api/schedule/execution/status` - Current execution status
- `POST /api/schedule/execution/start` - Start schedule execution
- `POST /api/schedule/execution/stop` - Stop schedule execution
- `POST /api/schedule/execution/dry-run` - Enable/disable dry run mode

#### **3F.3 Example Endpoint Implementation**
```c
// GET /api/schedule/execution/status
esp_err_t schedule_execution_status_handler(httpd_req_t *req) {
    schedule_executor_state_t state = schedule_executor_get_state();
    
    cJSON *json = cJSON_CreateObject();
    
    switch (state) {
        case SCHEDULE_EXECUTOR_STOPPED:
            cJSON_AddStringToObject(json, "status", "stopped");
            break;
        case SCHEDULE_EXECUTOR_RUNNING:
            cJSON_AddStringToObject(json, "status", "running");
            break;
        case SCHEDULE_EXECUTOR_DRY_RUN:
            cJSON_AddStringToObject(json, "status", "dry_run");
            break;
    }
    
    cJSON_AddBoolToObject(json, "dry_run_mode", state == SCHEDULE_EXECUTOR_DRY_RUN);
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    free(json_string);
    return ESP_OK;
}
```

#### **3F.4 Authentication Integration**
**Security**: All modification endpoints require `AUTH_ROLE_MANAGER`
```c
// Authentication check pattern
if (!auth_check_session(req, AUTH_ROLE_MANAGER)) {
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
    return ESP_FAIL;
}
```

### **Phase 3G: Safety & Recovery Systems (Week 3)**

#### **3G.1 Boot-Time Safety**
**Implementation**:
```c
esp_err_t io_manager_ensure_all_bos_off(void) {
    int success_count = 0;
    int total_bos = 0;
    
    char **bo_ids = NULL;
    size_t bo_count = 0;
    esp_err_t err = io_manager_get_bo_list(&bo_ids, &bo_count);
    
    for (size_t i = 0; i < bo_count; i++) {
        total_bos++;
        err = io_manager_set_bo_state(bo_ids[i], false);
        if (err == ESP_OK) {
            success_count++;
        }
    }
    
    ESP_LOGI(TAG, "Boot-time BO shutdown complete: %d/%d BOs turned OFF successfully", 
             success_count, total_bos);
    
    return (success_count == total_bos) ? ESP_OK : ESP_ERR_INVALID_STATE;
}
```

#### **3G.2 Configuration Safety**
**Pattern**: Turn off BO before configuration changes
```c
esp_err_t schedule_manager_update_instance_safe(const char *instance_id, 
                                               const schedule_instance_t *new_instance) {
    // 1. SAFETY: Turn off BO before configuration changes
    esp_err_t err = io_manager_set_bo_state(new_instance->bo_id, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Warning: Failed to turn off BO %s before configuration change", 
                 new_instance->bo_id);
    }
    
    // 2. Apply configuration changes
    err = schedule_manager_save_instance(new_instance);
    if (err != ESP_OK) {
        return err;
    }
    
    // 3. Schedule execution will restore appropriate state
    ESP_LOGI(TAG, "Schedule instance %s updated safely", instance_id);
    return ESP_OK;
}
```

#### **3G.3 Watchdog Protection**
**Critical**: Regular CPU yielding to prevent watchdog timeouts
```c
// In all loops processing multiple BOs
for (size_t i = 0; i < bo_count; i++) {
    process_bo_schedule(bo_ids[i], current_date_str, current_time_str);
    
    // CRITICAL: Yield CPU to prevent watchdog timeout
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

## Integration Points

### **Component Updates Required**:

#### **1. Update Core Component CMakeLists.txt**
```cmake
# components/core/CMakeLists.txt
idf_component_register(
    SRCS "memory_monitor.c" "task_tracker.c" "psram_manager.c" "psram_task_examples.c" "psram_test_suite.c" "time_manager.c" "schedule_manager.c" "schedule_executor.c" "event_logger.c"
    INCLUDE_DIRS "include"
    REQUIRES "freertos" "esp_timer" "esp_system" "esp_littlefs" "esp_crc" "json" "lwip"
)
```

#### **2. Update Web Component CMakeLists.txt**
```cmake
# components/web/CMakeLists.txt
idf_component_register(
    SRCS "web_server_manager.c" "static_file_controller.c" "system_controller.c" "auth_controller.c" "time_controller.c" "schedule_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_http_server" "esp_littlefs" "json" "core" "storage" "network" "esp_timer" "esp_system" "esp_wifi"
)
```

#### **3. Update Main Application (src/main.c)**
```c
// Add to app_main() after time manager initialization
ESP_LOGI(TAG, "Initializing Schedule Management System...");
if (schedule_manager_init() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize Schedule Management System");
    return;
}

ESP_LOGI(TAG, "Initializing Schedule Executor...");
if (schedule_executor_init() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize Schedule Executor");
    return;
}

ESP_LOGI(TAG, "Performing power outage recovery check...");
if (schedule_executor_perform_recovery_check() != ESP_OK) {
    ESP_LOGW(TAG, "Power outage recovery check completed with warnings");
}

ESP_LOGI(TAG, "Starting Schedule Executor...");
if (schedule_executor_start() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start Schedule Executor");
    return;
}
```

#### **4. Update Web Server Manager**
```c
// Add to web_server_manager.c endpoint registration
esp_err_t register_all_endpoints(httpd_handle_t server) {
    // ... existing endpoints
    
    // Schedule Controller endpoints
    err = schedule_controller_register_endpoints(server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register schedule controller endpoints: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}
```

## Expected Build Results

### **Memory Usage Targets**:
- **RAM Usage**: ~36-37% (increase from current 35.6%)
- **Flash Usage**: ~39-40% (increase from current 38.1%)
- **PSRAM Usage**: 1MB allocated for scheduling (25% of 4MB total)

### **Performance Characteristics**:
- **Schedule Evaluation**: Every 60 seconds
- **Timing Precision**: To-the-second accuracy using FreeRTOS tasks
- **Concurrent Watering**: Up to 8 simultaneous watering tasks
- **Recovery Time**: <30 seconds on power restoration

## Implementation Timeline

### **Week 1: Core Foundation**
- **Phase 3A**: Schedule Manager component with data structures and PSRAM integration
- Binary storage system with CRC32 validation
- Template and instance management functions
- Core API implementation

### **Week 2: Execution Engine**
- **Phase 3B**: Schedule Executor with FreeRTOS task framework
- **Phase 3C**: Event logging and power outage recovery
- **Phase 3D**: Time management integration
- Self-contained watering tasks with to-the-second precision

### **Week 3: Advanced Features**
- **Phase 3E**: Volume-to-duration conversion with BO calibration
- **Phase 3F**: Web API controller with RESTful endpoints
- **Phase 3G**: Safety systems and watchdog protection
- Authentication integration

### **Week 4: Integration & Testing**
- Main application integration
- Web server endpoint registration
- Comprehensive testing and validation
- Documentation updates

## Success Criteria

### **Functional Requirements**:
✅ **Four Event Types**: Duration/Volume × AutoPilot/Prescheduled all operational
✅ **Precise Timing**: To-the-second accuracy for all watering events
✅ **Power Recovery**: Automatic restart of interrupted irrigation
✅ **Conflict Resolution**: Priority-based scheduling when instances overlap
✅ **Safety Systems**: Boot-time shutdown, configuration safety, watchdog protection

### **Technical Requirements**:
✅ **PSRAM Integration**: 1MB allocation with fallback to RAM
✅ **Binary Storage**: 60-80% space savings over JSON with CRC32 validation
✅ **Web APIs**: Complete RESTful interface for all schedule operations
✅ **Authentication**: Manager-level security for all modifications
✅ **Performance**: <2 seconds response times, stable operation under load

### **Quality Gates**:
✅ **Build Success**: Clean compilation with no errors
✅ **Memory Efficiency**: Target RAM usage <37%, Flash usage <40%
✅ **System Stability**: Zero crashes during extended operation
✅ **Integration**: Seamless integration with existing SNRv9 infrastructure
✅ **Documentation**: Complete API documentation and implementation notes

## Schedule Types & Capabilities

### **AutoPilot Windows (Sensor-Driven)**

#### **Duration-Based AutoPilot**
```c
duration_autopilot_window_t window = {
    .start_time = "08:00",
    .end_time = "18:00",
    .trigger_setpoint = 25.0f,      // Soil moisture > 25.0 kPa
    .dose_duration = 300,           // Water for 300 seconds
    .settling_time = 60,            // 60 minutes settling
    .sensor_id = "AI_01"            // Associated sensor
};
```

#### **Volume-Based AutoPilot**
```c
volume_autopilot_window
