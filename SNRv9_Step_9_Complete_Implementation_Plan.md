# SNRv9 Step 9 Complete Implementation Plan - Advanced Features with PSRAM Integration

## 🎯 Current Status: Phase 2 Complete - Ready for Phase 3 Scheduling System

**Progress**: 2 of 8 phases complete (25%)
- ✅ **Phase 1**: PSRAM Infrastructure Extension (August 7, 2025)
- ✅ **Phase 2**: Time Management System (August 8, 2025)
- 🔄 **Phase 3**: Scheduling System (NEXT - Ready to implement)

**Foundation Established**: 
- PSRAM manager extended with Step 9 allocation categories
- Time management system fully operational with NTP sync, timezone support, and web interface
- Build success: RAM 35.6%, Flash 38.1%
- All dependencies satisfied for Phase 3 implementation

## Overview

This is a comprehensive implementation plan for Step 9 Advanced Features, integrating all four systems (Time Management, Scheduling, Alarming, Trending) with optimized PSRAM usage leveraging the existing SNRv9 infrastructure. This plan builds upon the detailed system architectures defined in:

- **Time Management**: `SNRv9_Step_9_Advanced_Features_Time_Management.md`
- **Scheduling**: `SNRv9_Step_9_Advanced_Features_Scheduling.md` 
- **Alarming**: `SNRv9_Step_9_Advanced_Features_Alarming.md`
- **Trending**: `SNRv9_Step_9_Advanced_Features_Trending.md`

## Implementation Strategy

### **Sequential Implementation Approach**
Build systems in dependency order to ensure each component has the foundation it needs:

```
Time Management → Scheduling → Alarming → Trending
      ↓              ↓           ↓          ↓
   (Foundation)  (Depends on   (Depends   (Depends on
                   Time)      on Time +    all three)
                              Scheduling)
```

## ✅ Phase 1: PSRAM Infrastructure Extension (COMPLETE - Week 1)
**Date Completed**: August 7, 2025

### **1.1 Extend Existing PSRAM Manager**
**Files to Modify**: `components/core/psram_manager.h`, `components/core/psram_manager.c`

**Add Step 9 Allocation Categories**:
```c
// Extend existing psram_allocation_strategy_t enum
typedef enum {
    PSRAM_ALLOC_CRITICAL = 0,        // Existing
    PSRAM_ALLOC_LARGE_BUFFER = 1,    // Existing  
    PSRAM_ALLOC_CACHE = 2,           // Existing
    PSRAM_ALLOC_NORMAL = 3,          // Existing
    
    // NEW: Step 9 allocations
    PSRAM_ALLOC_TIME_MGMT = 4,       // 128KB - Timezone DB, NTP history
    PSRAM_ALLOC_SCHEDULING = 5,      // 1MB - Schedule storage, cron parsing
    PSRAM_ALLOC_ALARMING = 6,        // 256KB - Alarm states, history
    PSRAM_ALLOC_TRENDING = 7,        // 2MB - Data buffers (reduced from 3MB)
    PSRAM_ALLOC_WEB_BUFFERS = 8      // 512KB - HTTP response buffers
} psram_allocation_strategy_t;
```

**Extend Statistics Tracking**:
```c
// Add to existing psram_statistics_t
typedef struct {
    // Existing fields...
    size_t total_allocations;
    size_t successful_allocations;
    
    // NEW: Step 9 category tracking
    size_t time_mgmt_bytes;
    size_t scheduling_bytes;
    size_t alarming_bytes;
    size_t trending_bytes;
    size_t web_buffer_bytes;
    
    // Per-category allocation counts
    uint32_t time_mgmt_allocations;
    uint32_t scheduling_allocations;
    uint32_t alarming_allocations;
    uint32_t web_buffer_allocations;
} psram_statistics_t;
```

**Add Category Management Functions**:
```c
esp_err_t psram_manager_allocate_for_category(psram_allocation_strategy_t category, 
                                             size_t size, void** ptr);
esp_err_t psram_manager_get_category_usage(psram_allocation_strategy_t category, 
                                          size_t* used, size_t* allocated);
esp_err_t psram_manager_get_step9_status(psram_step9_status_t* status);
```

### **1.2 Update Existing Monitoring Integration**
**Files to Modify**: `components/web/system_controller.c`

**Extend System Status API**:
```c
// Add Step 9 PSRAM metrics to /api/system/status
{
  "psram": {
    "totalUsed": 3932160,
    "totalAvailable": 4194304,
    "categories": {
      "timeManagement": {"used": 131072, "allocations": 3},
      "scheduling": {"used": 1048576, "allocations": 12},
      "alarming": {"used": 262144, "allocations": 8},
      "trending": {"used": 2097152, "allocations": 24},
      "webBuffers": {"used": 524288, "allocations": 4}
    }
  }
}
```

## ✅ Phase 2: Time Management System (COMPLETE - Week 2)
**Date Completed**: August 8, 2025

**Implementation Status**: FULLY OPERATIONAL
- ✅ Core time management component with PSRAM integration
- ✅ Web API controller with 5 REST endpoints
- ✅ Professional web interface with real-time updates
- ✅ Five-state reliability system with NTP-only time source
- ✅ WiFi event integration with automatic sync
- ✅ Enhanced debug logging and timezone selection
- ✅ Build success: RAM 35.6%, Flash 38.1%

### **2.1 Core Time Management Component**
**New Files**: `components/core/time_manager.c/h`

**Key Features** (from `SNRv9_Step_9_Advanced_Features_Time_Management.md`):
- ESP-IDF SNTP integration with native `esp_sntp` component
- Manual time setting capability when NTP unavailable
- Full POSIX timezone support with automatic DST handling
- Thread-safe operations with FreeRTOS mutex protection
- NVS persistence for all settings
- WiFi event integration for automatic sync attempts

**PSRAM Integration**:
```c
// Use existing PSRAM manager patterns
esp_err_t time_manager_init(void) {
    // Allocate timezone database in PSRAM
    timezone_db = psram_manager_allocate_for_category(PSRAM_ALLOC_TIME_MGMT,
                                                     sizeof(timezone_info_t) * MAX_TIMEZONES,
                                                     (void**)&timezone_db);
    
    // Allocate NTP history in PSRAM  
    ntp_history = psram_manager_allocate_for_category(PSRAM_ALLOC_TIME_MGMT,
                                                     sizeof(ntp_sync_record_t) * MAX_NTP_HISTORY,
                                                     (void**)&ntp_history);
    
    // Follow existing fallback pattern
    if (!timezone_db || !ntp_history) {
        ESP_LOGW(TAG, "PSRAM allocation failed, using RAM fallback");
        // Implement fallback to regular malloc
    }
    
    return ESP_OK;
}
```

**FreeRTOS Tasks**:
- `time_manager_task` (Priority 2, Stack 3072) - NTP synchronization and status monitoring

### **2.2 Web API Integration**
**New Files**: `components/web/time_controller.c/h`

**REST Endpoints**:
- `GET /api/time/status` - Current time, sync status, timezone info
- `POST /api/time/ntp/config` - Set NTP server and timezone
- `POST /api/time/ntp/sync` - Force NTP synchronization
- `POST /api/time/manual` - Set time manually
- `GET /api/time/timezones` - Get list of common timezone configurations

## Phase 3: Scheduling System (Week 3)

### **3.1 Core Scheduling Component**
**New Files**: `components/core/schedule_manager.c/h`, `components/core/schedule_executor.c/h`

**Key Features** (from `SNRv9_Step_9_Advanced_Features_Scheduling.md`):
- Real-time irrigation controller with millisecond-precision timing
- Schedule templates & instances with binary storage optimization
- Self-contained watering tasks using dedicated FreeRTOS tasks
- AutoPilot windows (sensor-driven) and prescheduled events (time-driven)
- Power outage recovery with event logging integration
- Volume-to-duration conversion with BO calibration

**PSRAM Integration**:
```c
// Large schedule storage in PSRAM
esp_err_t schedule_manager_init(void) {
    // Schedule entries array
    schedules = psram_manager_allocate_for_category(PSRAM_ALLOC_SCHEDULING,
                                                   sizeof(schedule_entry_t) * MAX_SCHEDULES,
                                                   (void**)&schedules);
    
    // Cron parsing context
    cron_context = psram_manager_allocate_for_category(PSRAM_ALLOC_SCHEDULING,
                                                      sizeof(cron_parser_context_t),
                                                      (void**)&cron_context);
    
    // AutoPilot calculation buffers
    autopilot_ctx = psram_manager_allocate_for_category(PSRAM_ALLOC_SCHEDULING,
                                                       sizeof(autopilot_context_t),
                                                       (void**)&autopilot_ctx);
    
    return ESP_OK;
}
```

**FreeRTOS Tasks**:
- `schedule_executor_task` (Priority 3, Stack 4096) - Main schedule execution
- `watering_task` instances (Priority 4, Stack 2048) - Individual watering events

**Dependencies**: Requires Time Management for accurate scheduling

### **3.2 Web API Integration**
**New Files**: `components/web/schedule_controller.c/h`

**REST Endpoints**:
- `GET /api/schedule_templates` - List all schedule templates
- `POST /api/schedule_templates` - Create new template
- `GET /api/schedule_instances` - List schedule instances
- `POST /api/schedule_instances` - Create new instance
- `GET /api/schedule/execution/status` - Execution status
- `POST /api/schedule/execution/start` - Start schedule execution
- `POST /api/schedule/execution/stop` - Stop schedule execution

## Phase 4: Alarming System (Week 4)

### **4.1 Core Alarming Component**
**New Files**: `components/core/alarm_manager.c/h`

**Key Features** (from `SNRv9_Step_9_Advanced_Features_Alarming.md`):
- Real-time alarm detection with 4 alarm types (rate of change, disconnected, max value, stuck signal)
- Comprehensive state management with trust/recovery system
- AutoPilot integration to prevent irrigation when sensors are in alarm
- Configurable alarm rules with persistence and hysteresis
- Manual reset capability for critical alarms

**PSRAM Integration**:
```c
// Alarm system PSRAM allocation
esp_err_t alarm_manager_init(void) {
    // Alarm states array
    alarm_states = psram_manager_allocate_for_category(PSRAM_ALLOC_ALARMING,
                                                      sizeof(alarm_state_t) * MAX_ALARM_POINTS,
                                                      (void**)&alarm_states);
    
    // Alarm history buffer
    alarm_history = psram_manager_allocate_for_category(PSRAM_ALLOC_ALARMING,
                                                       sizeof(alarm_event_t) * MAX_ALARM_EVENTS,
                                                       (void**)&alarm_history);
    
    // Trust/recovery tracking
    trust_context = psram_manager_allocate_for_category(PSRAM_ALLOC_ALARMING,
                                                       sizeof(trust_recovery_context_t),
                                                       (void**)&trust_context);
    
    return ESP_OK;
}
```

**FreeRTOS Tasks**:
- `alarm_manager_task` (Priority 2, Stack 4096) - Real-time alarm processing

**Dependencies**: Requires Time Management and integrates with Scheduling for AutoPilot blocking

### **4.2 Web API Integration**
**New Files**: `components/web/alarm_controller.c/h`

**REST Endpoints**:
- `GET /api/io/point/{pointId}/alarmstatus` - Current alarm status
- `GET /api/io/point/{pointId}/alarmconfig` - Alarm configuration
- `PUT /api/io/point/{pointId}/alarmconfig` - Update alarm settings
- `POST /api/io/point/{pointId}/resetalarm` - Manual alarm reset

## Phase 5: Trending System (Week 5)

### **5.1 Core Trending Component**
**New Files**: `components/core/trending_manager.c/h`

**Key Features** (from `SNRv9_Step_9_Advanced_Features_Trending.md`):
- Real-time data collection for AI samples and BI/BO state changes
- PSRAM buffer management with LittleFS file persistence
- Configurable sample intervals and data retention policies
- Binary file format with CRC32 integrity checking
- Web-based data visualization support

**PSRAM Integration** (Optimized Budget):
```c
// Trending system uses reduced 2MB PSRAM budget
esp_err_t trending_manager_init(void) {
    // Use existing PSRAM allocation for trending buffers
    psram_base = psram_manager_allocate_for_category(PSRAM_ALLOC_TRENDING,
                                                    TRENDING_REDUCED_PSRAM_BUDGET, // 2MB
                                                    (void**)&psram_base);
    
    // Initialize buffer management using existing patterns
    return init_trending_buffer_manager();
}
```

**FreeRTOS Tasks**:
- `trending_data_collection_task` (Priority 1, Stack 4096) - Data collection
- `trending_file_flush_task` (Priority 0, Stack 4096) - File persistence
- `trending_heap_monitor_task` (Priority 0, Stack 2048) - Memory monitoring

**Dependencies**: Requires Time Management, integrates with Scheduling and Alarming

### **5.2 Web API Integration**
**New Files**: `components/web/trending_controller.c/h`

**REST Endpoints**:
- `GET /api/trending/config` - Trending configuration
- `POST /api/trending/config` - Update trending settings
- `GET /api/trending/data/{pointId}` - Get trending data
- `GET /api/trending/metadata` - File metadata and statistics
- `DELETE /api/trending/data/{pointId}` - Delete trending data
- `POST /api/trending/flush` - Force buffer flush

## Phase 6: Web Buffer Optimization (Week 6)

### **6.1 Large Response Buffer Management**
**Files to Modify**: All web controllers

**PSRAM Integration for Web Responses**:
```c
// Large HTTP response buffers in PSRAM
esp_err_t init_web_response_buffers(void) {
    // Large JSON response buffer (256KB)
    large_response_buffer = psram_manager_allocate_for_category(PSRAM_ALLOC_WEB_BUFFERS,
                                                               LARGE_RESPONSE_BUFFER_SIZE,
                                                               (void**)&large_response_buffer);
    
    // Trending data response buffer (128KB)
    trending_response_buffer = psram_manager_allocate_for_category(PSRAM_ALLOC_WEB_BUFFERS,
                                                                  TRENDING_RESPONSE_BUFFER_SIZE,
                                                                  (void**)&trending_response_buffer);
    
    // Configuration export buffer (128KB)
    config_export_buffer = psram_manager_allocate_for_category(PSRAM_ALLOC_WEB_BUFFERS,
                                                              CONFIG_EXPORT_BUFFER_SIZE,
                                                              (void**)&config_export_buffer);
    
    return ESP_OK;
}
```

**Update All Controllers**:
- Trending Controller: Use PSRAM buffers for large data responses
- Schedule Controller: Use PSRAM buffers for schedule exports
- Alarm Controller: Use PSRAM buffers for alarm reports
- System Controller: Use PSRAM buffers for comprehensive status

## Phase 7: System Integration and Testing (Week 7)

### **7.1 Main Application Integration**
**Files to Modify**: `src/main.c`

**Initialization Sequence**:
```c
esp_err_t initialize_step9_systems(void) {
    ESP_LOGI(TAG, "Initializing Step 9 Advanced Features...");
    
    // Phase 1: Extend PSRAM Manager
    esp_err_t err = psram_manager_extend_for_step9();
    if (err != ESP_OK) return err;
    
    // Phase 2: Time Management (Foundation)
    err = time_manager_init();
    if (err != ESP_OK) return err;
    
    // Phase 3: Scheduling (Depends on Time)
    err = schedule_manager_init();
    if (err != ESP_OK) return err;
    
    err = schedule_executor_init();
    if (err != ESP_OK) return err;
    
    // Phase 4: Alarming (Depends on Time + Scheduling)
    err = alarm_manager_init();
    if (err != ESP_OK) return err;
    
    // Phase 5: Trending (Depends on all three)
    err = trending_manager_init();
    if (err != ESP_OK) return err;
    
    // Phase 6: Web Buffer Optimization
    err = init_web_response_buffers();
    if (err != ESP_OK) return err;
    
    // Start all systems
    err = start_step9_systems();
    if (err != ESP_OK) return err;
    
    ESP_LOGI(TAG, "Step 9 Advanced Features initialized successfully");
    return ESP_OK;
}
```

### **7.2 Web Server Integration**
**Files to Modify**: `components/web/web_server_manager.c`

**Register All Step 9 Endpoints**:
```c
esp_err_t register_step9_endpoints(httpd_handle_t server) {
    esp_err_t err;
    
    // Time Management endpoints
    err = time_controller_register_endpoints(server);
    if (err != ESP_OK) return err;
    
    // Scheduling endpoints
    err = schedule_controller_register_endpoints(server);
    if (err != ESP_OK) return err;
    
    // Alarming endpoints
    err = alarm_controller_register_endpoints(server);
    if (err != ESP_OK) return err;
    
    // Trending endpoints
    err = trending_controller_register_endpoints(server);
    if (err != ESP_OK) return err;
    
    ESP_LOGI(TAG, "Step 9 web endpoints registered successfully");
    return ESP_OK;
}
```

### **7.3 Comprehensive Testing Framework**
**New Files**: `components/core/step9_test_suite.c/h`

**Test Categories**:
- **PSRAM Integration Tests**: Verify all Step 9 systems use PSRAM correctly
- **Time Management Tests**: NTP sync, timezone handling, manual time setting
- **Scheduling Tests**: Cron parsing, AutoPilot windows, conflict detection
- **Alarming Tests**: Real-time detection, state management, trust system
- **Trending Tests**: Data collection, buffer management, file persistence
- **Integration Tests**: Cross-system communication and dependencies
- **Performance Tests**: Memory usage, CPU overhead, response times
- **Stress Tests**: Extended operation, high load, error recovery

## Phase 8: Performance Optimization and Production Readiness (Week 8)

### **8.1 Memory Usage Optimization**
**Expected Results**:
```
Internal RAM Usage (Before Step 9):
- Current: 33.8% (110,784 bytes)
- After PSRAM Migration: ~25% (82,000 bytes)
- RAM Freed: ~28KB for real-time operations

PSRAM Usage (Total 4MB mapped):
- Time Management: 128KB (3%)
- Scheduling: 1MB (25%)  
- Alarming: 256KB (6%)
- Trending: 2MB (50%) - reduced from 3MB
- Web Buffers: 512KB (13%)
- Reserve: 128KB (3%)
Total: 4MB (100%)
```

### **8.2 Performance Monitoring Integration**
**Files to Modify**: `components/core/memory_monitor.c`, `components/core/task_tracker.c`

**Enhanced Monitoring**:
- Step 9 task monitoring with stack usage analysis
- PSRAM category usage tracking and alerts
- Cross-system performance metrics
- Real-time dashboard integration

### **8.3 Production Configuration**
**Files to Modify**: `include/debug_config.h`

**Step 9 Debug Configuration**:
```c
// Step 9 Advanced Features Debug Configuration
#define DEBUG_TIME_MANAGEMENT          1
#define DEBUG_SCHEDULING_SYSTEM        1  
#define DEBUG_ALARMING_SYSTEM          1
#define DEBUG_TRENDING_SYSTEM          1
#define DEBUG_STEP9_PSRAM_USAGE        1

// Production-safe defaults
#define STEP9_MONITORING_INTERVAL_MS   60000  // 1 minute
#define STEP9_DETAILED_LOGGING         0     // Disable in production
```

## Implementation Timeline Summary

| Week | Phase | Focus | Status | Deliverables |
|------|-------|-------|--------|--------------|
| 1 | PSRAM Extension | Foundation | ✅ **COMPLETE** | Extended PSRAM manager, monitoring integration |
| 2 | Time Management | Core timing | ✅ **COMPLETE** | NTP sync, timezone handling, web APIs |
| 3 | Scheduling | Automation | 🔄 **NEXT** | Cron scheduling, AutoPilot, web control |
| 4 | Alarming | Safety | ⏳ Pending | Real-time monitoring, state management, web alerts |
| 5 | Trending | Data collection | ⏳ Pending | PSRAM buffers, file persistence, web visualization |
| 6 | Web Optimization | Performance | ⏳ Pending | Large response buffers, optimized APIs |
| 7 | Integration | System testing | ⏳ Pending | Complete integration, comprehensive testing |
| 8 | Production | Optimization | ⏳ Pending | Performance tuning, production configuration |

## Success Criteria

### **Technical Metrics**:
- ✅ **Memory Efficiency**: <25% internal RAM usage (target: ~28KB freed)
- ✅ **PSRAM Utilization**: 100% of 4MB mapped PSRAM efficiently used
- ✅ **System Stability**: Zero crashes during 24-hour continuous operation
- ✅ **Response Performance**: <2 seconds for all web API responses
- ✅ **Real-time Performance**: <100ms for alarm detection and scheduling

### **Functional Requirements**:
- ✅ **Time Management**: NTP sync, timezone support, manual time setting
- ✅ **Scheduling**: Full cron support, AutoPilot windows, manual overrides
- ✅ **Alarming**: Real-time detection, state management, web integration
- ✅ **Trending**: Data collection, PSRAM buffers, web visualization
- ✅ **Web Integration**: Complete REST APIs for all systems

### **Quality Gates**:
- ✅ **Code Coverage**: >80% test coverage for all Step 9 components
- ✅ **Documentation**: Complete API documentation and memory bank updates
- ✅ **Performance**: Comprehensive benchmarking and optimization
- ✅ **Production Readiness**: Debug configuration, monitoring integration

## Component Dependencies

### **ESP-IDF Component Requirements**:
```cmake
# Core components
REQUIRES 
    "esp_littlefs"      # File system
    "esp_timer"         # High-resolution timing
    "esp_psram"         # PSRAM management
    "freertos"          # Real-time OS
    "esp_netif"         # Network interface
    "esp_wifi"          # WiFi connectivity
    "nvs_flash"         # Non-volatile storage
    "lwip"              # Network stack
    "json"              # JSON parsing
    "esp_http_server"   # Web server
    "esp_crc"           # CRC calculations

# Existing SNRv9 components
    "core"              # Memory monitoring, task tracking, PSRAM manager
    "io_manager"        # IO system integration
    "auth_manager"      # Authentication
    "event_logger"      # Event logging
```

## Integration Points

### **Cross-System Dependencies**:
1. **Time Management → Scheduling**: Accurate time source for schedule execution
2. **Time Management → Alarming**: Timestamps for alarm events
3. **Time Management → Trending**: Time synchronization for data collection
4. **Scheduling → Alarming**: AutoPilot blocking when sensors in alarm
5. **Alarming → Trending**: Alarm events included in trending data
6. **All Systems → Web APIs**: Unified web interface for configuration and monitoring

### **Existing SNRv9 Integration**:
1. **IO Manager**: Data source for alarming and trending, control target for scheduling
2. **Event Logger**: Centralized logging for all Step 9 events
3. **Auth Manager**: Security for all web endpoints
4. **PSRAM Manager**: Memory allocation for all Step 9 systems
5. **Memory Monitor**: Enhanced monitoring for Step 9 components
6. **Task Tracker**: Monitoring of all Step 9 FreeRTOS tasks

## Risk Mitigation

### **Memory Constraints**:
- **Risk**: PSRAM budget exceeded
- **Mitigation**: Reduced trending allocation from 3MB to 2MB, comprehensive monitoring
- **Fallback**: RAM fallback for critical components

### **System Complexity**:
- **Risk**: Integration issues between systems
- **Mitigation**: Sequential implementation with dependency validation
- **Testing**: Comprehensive integration test suite

### **Performance Impact**:
- **Risk**: CPU overhead from multiple systems
- **Mitigation**: Optimized task priorities, regular CPU yielding
- **Monitoring**: Real-time performance tracking

This comprehensive plan delivers a complete Step 9 Advanced Features implementation with optimized PSRAM usage, leveraging existing infrastructure while providing production-ready irrigation control capabilities with time management, scheduling, alarming, and trending systems fully integrated into the SNRv9 platform.
