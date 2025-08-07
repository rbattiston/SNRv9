# ESP32 Web Server Architecture Implementation Plan

## Project Overview

This document outlines the step-by-step implementation plan for building a sophisticated ESP32 Web Server Architecture on the SNRv9 irrigation control system. The architecture follows a controller-based pattern inspired by the SNRv8 project, designed for high reliability, scalability, and production-grade performance.

## Current Foundation

### Existing Strengths
- **Robust Monitoring Systems**: Memory monitoring, task tracking, stack overflow prevention
- **Production-Ready Architecture**: Configurable debug output, comprehensive error handling
- **System Stability**: No crashes, optimized memory usage (4.4% RAM, 19.2% Flash)
- **Clean Foundation**: Demo tasks removed, main loop ready for implementation
- **ESP-IDF 5.4.1 + FreeRTOS**: Perfect platform for web server architecture

### Technical Foundation
- **Platform**: ESP32-D0WD-V3 with ESP-IDF 5.4.1
- **Memory**: 320KB RAM, 2MB Flash (plenty of headroom)
- **Monitoring**: Comprehensive resource tracking and early warning systems
- **Task Management**: FreeRTOS with proven stack management (2048+ bytes)

## Implementation Roadmap

### Step 1: WiFi Foundation
**Component**: WiFiHandler/WiFiManager
**Priority**: Critical Foundation

**Implementation Details**:
- Basic WiFi connection management with auto-reconnection
- Hardcoded credentials initially (configurable in later steps)
- Connection status monitoring and reporting
- Integration with existing monitoring system
- Proper error handling and recovery mechanisms

**Success Criteria**:
- Stable WiFi connection established
- Connection status visible in monitoring output
- Auto-reconnection on network drops
- No impact on existing system stability

**Files to Create**:
- `include/wifi_handler.h`
- `src/wifi_handler.c`
- Integration in `main.c`

---

### Step 2: ESP-IDF HTTP Server Foundation
**Component**: WebServerManager (ESP-IDF based)
**Priority**: Critical Foundation

**Implementation Details**:
- Initialize ESP-IDF HTTP Server (`esp_http_server.h`) on port 80
- Use `httpd_config_t` with `HTTPD_DEFAULT_CONFIG()` for configuration
- Basic server start/stop functionality using `httpd_start()`/`httpd_stop()`
- Simple JSON status endpoint for testing (`/api/status`)
- Integration with ESP-IDF event system for server lifecycle events
- Integration with existing task monitoring (server runs in its own task)
- Proper task configuration with 4096-byte stack size (following ESP-IDF patterns)
- URI handler registration using `httpd_register_uri_handler()`

**Success Criteria**:
- HTTP server responds to requests using ESP-IDF components
- Basic `/api/status` endpoint returns JSON response
- Server events logged through ESP-IDF event system
- Server task tracked by existing monitoring system
- No memory leaks or stack issues
- Follows official ESP-IDF documentation patterns

**Files to Create**:
- `include/web_server_manager.h`
- `src/web_server_manager.c`
- Update `main.c` for server initialization
- Update `CMakeLists.txt` to include `esp_http_server` dependency

**ESP-IDF Integration**:
- **Component Dependency**: `esp_http_server`
- **Event Integration**: `ESP_HTTP_SERVER_EVENT` handling
- **Configuration**: Standard `httpd_config_t` structure
- **URI Handlers**: `httpd_uri_t` structure for endpoint registration
- **Documentation Reference**: ESP-IDF HTTP Server API documentation

---

### Step 3: Static File Controller ✅ COMPLETE
**Component**: StaticFileController
**Priority**: High
**Status**: Successfully implemented with advanced features

**Implementation Completed**:
- ✅ Advanced HTTP caching system with ETag generation
- ✅ Conditional requests (304 Not Modified responses)
- ✅ Sophisticated MIME type handling (15+ file types)
- ✅ File-type specific cache policies (CSS: 24hrs, Images: 7 days, Fonts: 30 days)
- ✅ Thread-safe cache management with mutexes
- ✅ Production-grade performance optimizations
- ✅ Browser cache integration (verified with network tab)
- ✅ CORS headers for cross-origin compatibility
- ✅ Comprehensive statistics and monitoring
- ✅ Graceful error handling and 404 responses

**Success Criteria Met**:
- ✅ Static files served correctly with advanced caching
- ✅ Proper caching headers reduce bandwidth (verified: CSS loads from cache with 0ms)
- ✅ Enhanced web interface accessible with professional styling
- ✅ File serving optimized for ESP32 performance with zero impact

**Files Created**:
- ✅ `include/static_file_controller.h` - Complete interface with advanced caching APIs
- ✅ `src/static_file_controller.c` - Full implementation with ETag and conditional requests
- ✅ Enhanced test.html with caching validation tools
- ✅ Professional CSS styling with cache optimization
- ✅ JavaScript with cache monitoring capabilities

**Real-World Validation**:
- Browser network tab shows "(memory cache)" for CSS files
- 0ms load time for cached content
- Production-grade HTTP caching headers
- ETag generation and conditional request handling

---

### Step 4: Authentication Foundation
**Component**: AuthManager + AuthController
**Priority**: High

**Implementation Details**:
- Simple session-based authentication system
- Login/logout REST endpoints
- Session validation middleware
- Basic user roles (VIEWER, MANAGER, OWNER - hardcoded initially)
- Secure cookie-based session management

**Success Criteria**:
- Login/logout functionality working
- Session validation protecting endpoints
- Role-based access control functional
- Secure session handling

**Files to Create**:
- `include/auth_manager.h`
- `src/auth_manager.c`
- `include/auth_controller.h`
- `src/auth_controller.c`

---

### Step 5: System Monitoring Web Interface
**Component**: SystemController
**Priority**: High

**Implementation Details**:
- Web endpoints for system status (`/api/system/status`)
- JSON API for memory monitoring data
- Task tracking data via REST API
- Real-time system health dashboard
- Integration with existing monitoring systems

**Success Criteria**:
- System data accessible via web API
- JSON responses properly formatted
- Real-time monitoring data available
- Dashboard shows live system status

**Files to Create**:
- `include/system_controller.h`
- `src/system_controller.c`
- Dashboard HTML/JS for system monitoring

---

### Step 6: Configuration Management + I/O Framework (Combined Priority Step)
**Component**: ConfigManager + IOManager + ConfigController + IOController
**Priority**: High (Current Step)

**Implementation Details**:

**ConfigManager**: Industrial-grade configuration management
- Load and serve sophisticated IO configuration JSON (`io_config full_ES32D26 wEnabledProcessing.json`)
- Support for shift register configuration, analog input settings, alarm rules
- Thread-safe configuration access with validation
- Integration with storage_manager for persistence
- Real-time configuration updates with hardware re-initialization

**IOManager**: Hardware abstraction for industrial I/O
- **Shift Register I/O**: 8 relay outputs + 8 digital inputs via 74HC595/74HC165
- **Analog Inputs**: 6 channels (4x 0-20mA, 2x 0-10V) with signal conditioning
- **Signal Processing**: SMA filtering, lookup tables, scaling, gain/offset
- **Alarm System**: Rate of change, disconnection, stuck signal, max value detection
- **Thread Safety**: FreeRTOS mutex protection for all I/O operations

**ConfigController**: REST API for configuration management
- `GET /api/config` - Retrieve complete IO configuration
- `GET /api/config/io/{point_id}` - Get specific I/O point configuration
- `PUT /api/config/io/{point_id}` - Update I/O point configuration
- `POST /api/config/reload` - Reload configuration from storage
- `GET /api/config/hardware` - Get shift register and hardware settings

**IOController**: REST API for I/O control and monitoring
- `GET /api/io/points` - List all configured I/O points
- `GET /api/io/analog/{point_id}` - Get analog input value with conditioning
- `PUT /api/io/binary/{point_id}` - Set binary output state (with safety checks)
- `GET /api/io/alarms` - Get active alarm conditions
- `GET /api/io/status` - Get comprehensive I/O system status

**Hardware Architecture** (from existing JSON config):
- **Shift Register Outputs**: 8 relays (SOLENOID/LIGHTING types) with flow calibration
- **Shift Register Inputs**: 8 digital inputs with inversion support
- **ADC Channels**: GPIO pins 32-36, 39 with sophisticated signal processing
- **Signal Conditioning**: Per-channel filtering, scaling, alarm monitoring
- **Safety Features**: Boot-time shutdown, manual override timeouts, interlocking

**Success Criteria**:
- IO configuration JSON loaded and served via web API
- Shift register I/O operational with thread-safe access
- Analog inputs reading with signal conditioning applied
- Alarm system monitoring all configured points
- Configuration changes applied without system restart
- All I/O operations integrated with existing monitoring systems

**Files to Create**:
- `include/config_manager.h` - Configuration management with IO schema support
- `src/config_manager.c` - JSON loading, validation, and serving
- `include/io_manager.h` - Hardware abstraction for shift registers and ADC
- `src/io_manager.c` - I/O operations with signal processing and alarms
- `include/config_controller.h` - Configuration REST API
- `src/config_controller.c` - Configuration web endpoints
- `include/io_controller.h` - I/O control REST API
- `src/io_controller.c` - I/O monitoring and control endpoints

---

### Step 7: Request Priority Management
**Component**: RequestPriorityManager
**Priority**: Medium

**Implementation Details**:
- Request classification system (AUTHENTICATION, UI_CRITICAL, NORMAL, BACKGROUND)
- Priority-based request handling
- Heavy operation deferral to prevent watchdog timeouts
- Performance optimization for ESP32 constraints
- Load balancing and request queuing

**Success Criteria**:
- No watchdog timeouts under load
- Critical requests prioritized correctly
- Heavy operations deferred appropriately
- System remains responsive under load

**Files to Create**:
- `include/request_priority_manager.h`
- `src/request_priority_manager.c`
- Integration with WebServerManager

---

### Step 8: Storage Foundation ✅ COMPLETE
**Component**: StorageManager
**Priority**: High
**Status**: Successfully implemented with LittleFS integration (January 28, 2025)

**Implementation Completed**:
- ✅ LittleFS Integration: Complete filesystem integration with esp_littlefs component
- ✅ Partition Configuration: 256KB dedicated storage partition in partitions.csv
- ✅ Storage Manager Component: Abstraction layer for filesystem operations
- ✅ Configuration Updates: platformio.ini, sdkconfig.defaults, CMakeLists.txt configured
- ✅ Functionality Verification: Boot count test validates filesystem operations
- ✅ Production Ready: Reliable file system operations for configuration and logging

**Success Criteria Met**:
- ✅ Storage manager component operational and tested
- ✅ 256KB dedicated partition configured and working
- ✅ File system operations working reliably
- ✅ Ready for IO configuration persistence and event logging
- ✅ Integration with existing monitoring systems complete

**Files Created**:
- ✅ `components/storage/storage_manager.c/h` - Complete storage abstraction layer
- ✅ `partitions.csv` - Custom partition scheme with dedicated storage partition
- ✅ Updated build configuration for LittleFS integration

**Technical Achievement**: Complete persistent storage foundation enabling configuration management, event logging, and data persistence for production deployment.

---

### Step 9: Event Logging System
**Component**: EventLogger + LogController
**Priority**: Medium

**Implementation Details**:
- System event logging with timestamps
- Web-based log viewing (`/api/logs/*`)
- Raw log file serving (performance optimized)
- Log rotation and management
- Integration with existing debug system

**Success Criteria**:
- Events logged with proper timestamps
- Logs accessible via web interface
- Raw file serving for large logs
- Log rotation prevents storage overflow

**Files to Create**:
- `include/event_logger.h`
- `src/event_logger.c`
- `include/log_controller.h`
- `src/log_controller.c`

---

### Step 10: Dashboard Controller
**Component**: DashboardController
**Priority**: Medium

**Implementation Details**:
- Real-time dashboard data API (`/api/dashboard/*`)
- System status aggregation
- Performance metrics collection
- Live data updates (polling-based initially)
- Client-side data processing for heavy operations

**Success Criteria**:
- Dashboard shows real-time system data
- Performance metrics updated live
- Client-side processing prevents server overload
- Dashboard responsive and informative

**Files to Create**:
- `include/dashboard_controller.h`
- `src/dashboard_controller.c`
- Dashboard HTML/CSS/JS interface

---

### Step 11: User Management
**Component**: UserController + UserManager
**Priority**: Low

**Implementation Details**:
- User account management system
- Role-based access control (RBAC)
- User CRUD operations via web interface (`/api/users/*`)
- Password management and security
- Account enable/disable functionality

**Success Criteria**:
- User accounts manageable via web
- RBAC properly enforced
- Password security implemented
- User management interface functional

**Files to Create**:
- `include/user_manager.h`
- `src/user_manager.c`
- `include/user_controller.h`
- `src/user_controller.c`

---

### Step 12: Advanced Irrigation Control
**Component**: IrrigationManager + IrrigationController
**Priority**: Medium (Elevated from previous Step 13)

**Implementation Details**:
- **IrrigationManager**: Core irrigation logic and scheduling
  - Zone management based on BO configuration (SOLENOID types)
  - Flow rate calculations using `lphPerEmitterFlow` and `numEmittersPerPlant`
  - Volume-to-duration conversion for precise irrigation timing
  - AutoPilot sensor integration for feedback-controlled irrigation
  - Safety interlocks and emergency shutoff procedures
  - Schedule execution with conflict resolution

- **IrrigationController**: Irrigation-specific REST API
  - `GET /api/irrigation/zones` - List all irrigation zones with status
  - `PUT /api/irrigation/zones/{zone_id}/activate` - Manual zone control with safety checks
  - `GET /api/irrigation/schedule` - Current irrigation schedule and next events
  - `PUT /api/irrigation/schedule` - Update irrigation schedule with validation
  - `GET /api/irrigation/sensors` - Sensor readings for irrigation decision-making
  - `POST /api/irrigation/emergency-stop` - Emergency shutdown of all irrigation

- **Advanced Features**:
  - Integration with existing alarm system for sensor-based control
  - Flow rate monitoring and leak detection via analog inputs
  - Weather data integration for smart irrigation adjustment
  - Historical irrigation data collection and analysis
  - Mobile-friendly API for remote irrigation management

**Success Criteria**:
- Complete irrigation system control via web interface
- Automated scheduling with sensor feedback integration
- Safety systems prevent over-watering and equipment damage
- Real-time monitoring of irrigation operations
- Integration with existing I/O and alarm systems

**Files to Create**:
- `include/irrigation_manager.h`
- `src/irrigation_manager.c`
- `include/irrigation_controller.h`
- `src/irrigation_controller.c`
- Irrigation dashboard HTML/CSS/JS interface

## Implementation Principles

### Development Standards
- **Incremental Development**: Build and test each component thoroughly before proceeding
- **Monitoring Integration**: All new components must integrate with existing monitoring systems
- **Memory Safety**: Follow established stack size patterns (2048+ bytes minimum)
- **Error Handling**: Comprehensive error checking and graceful degradation
- **Documentation**: Update memory bank documentation after each step

### Performance Requirements
- **Client-Side Heavy Processing**: Store binary data on server, process on client
- **Request Priority Management**: Prevent watchdog timeouts through proper prioritization
- **Memory Efficiency**: Maintain <50% RAM usage throughout implementation
- **Monitoring Overhead**: Keep total monitoring overhead <5% CPU usage

### Quality Gates
Each step must meet these criteria before proceeding:

1. **Functionality**: All planned features working correctly
2. **Stability**: No crashes or system failures under normal operation
3. **Integration**: Seamless integration with existing monitoring systems
4. **Performance**: No significant impact on system responsiveness
5. **Memory Safety**: No memory leaks or stack overflow conditions
6. **Testing**: Minimum 1-hour continuous operation test
7. **Documentation**: Memory bank updated with new patterns and decisions

### Testing Approach
- **Unit Testing**: Individual component testing
- **Integration Testing**: System-level testing with real hardware
- **Extended Operation**: Continuous operation testing for stability
- **Load Testing**: Performance under various request loads
- **Resource Monitoring**: Continuous tracking of memory and CPU usage

## Architecture Patterns

### Controller-Based Pattern
- Each functional domain gets its own controller
- Dependency injection for manager classes
- Clean separation of concerns
- Scalable and maintainable architecture

### Request Handling Pattern
```
Request → Authentication → Priority Classification → Controller → Manager → Response
```

### Security Architecture
- Session-based authentication with secure cookies
- Role-based access control (RBAC)
- Input validation and sanitization
- CSRF protection for state-changing operations

### Performance Optimization
- HTTP caching for static files
- Raw file serving for large datasets
- Client-side processing for heavy operations
- Request prioritization and deferral

## Integration with Existing Systems

### Monitoring Integration
- Web server tasks tracked by existing task monitoring
- Memory usage monitored by existing memory monitoring
- Debug output controlled by existing debug configuration
- Early warning systems extended to web server components

### Safety Integration
- Hardware safety interlocks maintained
- Stack overflow prevention applied to all web server tasks
- Resource exhaustion protection
- Graceful degradation on component failures

## Success Metrics

### Technical Metrics
- **Uptime**: >99.9% (target: zero unplanned restarts)
- **Memory Efficiency**: <50% RAM usage at full implementation
- **Response Time**: <1 second for all user interface operations
- **Reliability**: Zero data corruption or system state inconsistencies

### Development Metrics
- **Implementation Speed**: One step per development session
- **Quality**: Zero regressions in existing functionality
- **Documentation**: 100% coverage of new components in memory bank
- **Testing**: All components pass extended operation testing

## Next Steps

**Immediate Action**: Begin with Step 1 (WiFi Foundation)
- Lowest risk to existing stable system
- Foundation for all subsequent web functionality
- Clear success criteria and testing approach
- Builds on existing monitoring infrastructure

This implementation plan provides a clear, incremental path to building a sophisticated ESP32 web server while maintaining the production-grade reliability and monitoring capabilities already established in the SNRv9 system.
