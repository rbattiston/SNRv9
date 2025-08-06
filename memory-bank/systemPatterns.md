# System Patterns - SNRv9 Architecture

## System Architecture Overview

### Component-Based Architecture (Updated January 30, 2025)
```
┌─────────────────────────────────────────────────────────────────┐
│                     Application Layer                          │
│              (Irrigation Logic, Web APIs)                      │
├─────────────────────────────────────────────────────────────────┤
│                   Component Layer                              │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ │
│  │    WEB      │ │   NETWORK   │ │   STORAGE   │ │    CORE     │ │
│  │ Component   │ │ Component   │ │ Component   │ │ Component   │ │
│  └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│                   Hardware Layer                               │
│              (ESP-IDF, FreeRTOS, Hardware)                     │
└─────────────────────────────────────────────────────────────────┘
```

### Component Dependencies
```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│    WEB      │───▶│   STORAGE   │───▶│    CORE     │
│ Component   │    │ Component   │    │ Component   │
└─────────────┘    └─────────────┘    └─────────────┘
       │                                      ▲
       │           ┌─────────────┐            │
       └──────────▶│   NETWORK   │────────────┘
                   │ Component   │
                   └─────────────┘
```
**Dependency Rules**:
- **CORE**: No dependencies (base component)
- **NETWORK**: Depends on CORE
- **STORAGE**: Depends on CORE  
- **WEB**: Depends on CORE, STORAGE, NETWORK

## Component Filesystem Organization (January 30, 2025)

### Directory Structure
```
components/
├── core/
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── include/
│   │   ├── debug_config.h
│   │   ├── memory_monitor.h
│   │   └── task_tracker.h
│   ├── memory_monitor.c
│   └── task_tracker.c
├── network/
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── include/
│   │   ├── debug_config.h (copy)
│   │   └── wifi_handler.h
│   └── wifi_handler.c
├── storage/
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── include/
│   │   ├── auth_manager.h
│   │   ├── debug_config.h (copy)
│   │   └── storage_manager.h
│   ├── auth_manager.c
│   └── storage_manager.c
└── web/
    ├── CMakeLists.txt
    ├── README.md
    ├── include/
    │   ├── auth_controller.h
    │   ├── debug_config.h (copy)
    │   ├── static_file_controller.h
    │   ├── system_controller.h
    │   └── web_server_manager.h
    ├── auth_controller.c
    ├── static_file_controller.c
    ├── system_controller.c
    └── web_server_manager.c
```

### Component Responsibilities

#### CORE Component
**Purpose**: Foundation monitoring and debugging infrastructure
**Files**:
- `memory_monitor.c/h`: Heap usage tracking and leak detection
- `task_tracker.c/h`: FreeRTOS task monitoring and stack analysis
- `psram_manager.c/h`: PSRAM detection, allocation strategies, and health monitoring
- `psram_task_examples.c/h`: Demonstration tasks showing PSRAM usage patterns
- `psram_test_suite.c/h`: Comprehensive testing framework for PSRAM functionality
- `debug_config.h`: Master configuration for all debug output

**CMakeLists.txt Configuration**:
```cmake
idf_component_register(
    SRCS "memory_monitor.c" "task_tracker.c"
    INCLUDE_DIRS "include"
    REQUIRES "freertos" "esp_timer" "esp_system"
)
```

#### NETWORK Component
**Purpose**: WiFi connectivity and network management
**Files**:
- `wifi_handler.c/h`: WiFi station mode with auto-reconnection
- `debug_config.h`: Local copy for component independence

**CMakeLists.txt Configuration**:
```cmake
idf_component_register(
    SRCS "wifi_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES "core" "esp_wifi" "esp_netif" "esp_event" "nvs_flash" "lwip" "esp_timer" "esp_system"
)
```

#### STORAGE Component
**Purpose**: Filesystem and authentication management
**Files**:
- `storage_manager.c/h`: LittleFS filesystem abstraction
- `auth_manager.c/h`: Session-based authentication system
- `debug_config.h`: Local copy for component independence

**CMakeLists.txt Configuration**:
```cmake
idf_component_register(
    SRCS "storage_manager.c" "auth_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES "core" "esp_littlefs" "nvs_flash" "esp_partition" "esp_timer" "esp_system"
)
```

#### WEB Component
**Purpose**: HTTP server and web API implementation
**Files**:
- `web_server_manager.c/h`: ESP-IDF HTTP server lifecycle management
- `static_file_controller.c/h`: Static file serving with advanced caching
- `system_controller.c/h`: System status and monitoring APIs
- `auth_controller.c/h`: Authentication endpoints
- `debug_config.h`: Local copy for component independence

**CMakeLists.txt Configuration**:
```cmake
idf_component_register(
    SRCS "web_server_manager.c" "static_file_controller.c" "system_controller.c" "auth_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_http_server" "esp_littlefs" "json" "core" "storage" "network" "esp_timer" "esp_system" "esp_wifi"
)
```

### Critical Implementation Details

#### Debug Configuration Management
**Issue Resolved**: Duplicate `debug_config.h` files caused compilation conflicts
**Solution**: Each component maintains its own copy of `debug_config.h`
**Rationale**: 
- Ensures component independence during development
- Prevents cross-component include path issues
- Allows per-component debug configuration if needed
- Simplifies build system configuration

#### Component Dependency Resolution
**Pattern**: ESP-IDF component system with explicit REQUIRES declarations
**Implementation**:
- Each component declares dependencies in CMakeLists.txt
- Build system automatically resolves include paths
- Circular dependencies prevented by design
- Clean separation of concerns

#### File Organization Principles
1. **Single Responsibility**: Each component has a clear, focused purpose
2. **Dependency Hierarchy**: Dependencies flow in one direction (no cycles)
3. **Include Isolation**: Each component's headers are self-contained
4. **Build Independence**: Components can be built and tested separately

### Migration Notes (January 30, 2025)

#### Files Moved During Reorganization
**From `src/` to Components**:
- `memory_monitor.c` → `components/core/memory_monitor.c`
- `task_tracker.c` → `components/core/task_tracker.c`
- `wifi_handler.c` → `components/network/wifi_handler.c`
- `storage_manager.c` → `components/storage/storage_manager.c`
- `auth_manager.c` → `components/storage/auth_manager.c`
- `web_server_manager.c` → `components/web/web_server_manager.c`
- `static_file_controller.c` → `components/web/static_file_controller.c`
- `system_controller.c` → `components/web/system_controller.c`
- `auth_controller.c` → `components/web/auth_controller.c`

**From `include/` to Component Includes**:
- `memory_monitor.h` → `components/core/include/memory_monitor.h`
- `task_tracker.h` → `components/core/include/task_tracker.h`
- `wifi_handler.h` → `components/network/include/wifi_handler.h`
- `storage_manager.h` → `components/storage/include/storage_manager.h`
- `auth_manager.h` → `components/storage/include/auth_manager.h`
- `web_server_manager.h` → `components/web/include/web_server_manager.h`
- `static_file_controller.h` → `components/web/include/static_file_controller.h`
- `system_controller.h` → `components/web/include/system_controller.h`
- `auth_controller.h` → `components/web/include/auth_controller.h`

#### Build System Validation
**Compilation Test Results**:
- Memory Usage: RAM: 14.4% (47,204 bytes), Flash: 34.7% (978,667 bytes)
- All components compile successfully
- No circular dependency issues
- All include paths resolved correctly

### Future Development Guidelines

#### Adding New Components
1. Create component directory under `components/`
2. Add `CMakeLists.txt` with proper REQUIRES declarations
3. Create `include/` subdirectory for headers
4. Add `README.md` documenting component purpose
5. Copy `debug_config.h` if debug output needed
6. Test compilation after each file addition

#### Modifying Existing Components
1. Respect component boundaries - avoid cross-component file access
2. Update CMakeLists.txt if adding new dependencies
3. Test compilation after changes
4. Update component README.md if interface changes
5. Maintain debug_config.h consistency across components

#### Component Interface Design
1. Keep public APIs minimal and focused
2. Use proper include guards in all headers
3. Document all public functions with Doxygen comments
4. Avoid exposing internal data structures
5. Design for testability and modularity

This component architecture provides a solid foundation for future development while maintaining the existing functionality and performance characteristics.

### Core Design Patterns

#### 1. Monitoring Pattern
**Pattern**: Centralized monitoring with configurable output
- **Implementation**: Separate monitoring modules (memory_monitor, task_tracker)
- **Key Features**:
  - Thread-safe data collection using mutexes
  - Configurable debug output via compile-time flags
  - Periodic reporting with customizable intervals
  - Early warning systems with threshold-based alerts

#### 2. Configuration Pattern
**Pattern**: Centralized configuration management
- **Implementation**: `debug_config.h` header with compile-time controls
- **Key Features**:
  - Single point of control for all debug settings
  - Easy enable/disable of monitoring systems
  - Configurable reporting intervals and thresholds
  - Production-safe defaults

#### 3. Resource Management Pattern
**Pattern**: Proactive resource monitoring and protection
- **Implementation**: Stack overflow prevention, memory leak detection
- **Key Features**:
  - Continuous monitoring of critical resources
  - Early warning systems before resource exhaustion
  - Automatic cleanup and error recovery where possible

#### 4. Filesystem Management Pattern
**Pattern**: Modular filesystem abstraction
- **Implementation**: `storage_manager` component wrapping LittleFS
- **Key Features**:
  - Initializes and mounts the filesystem partition
  - Provides a simple interface for file operations
  - Handles formatting on mount failure
  - Integrates with the system logging pattern

#### 5. PSRAM Management Pattern
**Pattern**: Smart memory allocation with PSRAM utilization
- **Implementation**: `psram_manager` component with allocation strategies and task creation framework
- **Key Features**:
  - ESP32-D0WD 8MB PSRAM detection and utilization (4MB mapped)
  - Smart allocation strategies (Critical, Large Buffer, Cache, Normal)
  - Task creation framework with configurable stack placement
  - Comprehensive health monitoring and statistics tracking
  - Thread-safe operations with fallback mechanisms
  - Production-ready testing suite with 100% validation coverage

## Component Relationships

### Memory Monitor Module
```
memory_monitor.c
├── Tracks heap usage (free, used, fragmentation)
├── Detects memory leaks via trend analysis
├── Provides detailed and summary reporting
└── Integrates with debug_config for output control
```

### Task Tracker Module
```
task_tracker.c
├── Monitors all FreeRTOS tasks
├── Tracks stack usage and detects overflow conditions
├── Provides task lifecycle callbacks
├── Generates stack analysis reports with warnings
└── Thread-safe data access via mutexes
```

### Storage Manager Module
```
storage_manager.c
├── Initializes and mounts the LittleFS partition
├── Wraps esp_littlefs functions for system integration
├── Handles filesystem formatting on mount failure
└── Provides logging consistent with system patterns
```

### PSRAM Manager Module
```
psram_manager.c
├── PSRAM detection and capability assessment
├── Smart allocation strategies (Critical, Large Buffer, Cache, Normal)
├── Task creation framework with configurable stack placement
├── Health monitoring and functionality validation
├── Statistics tracking (success/failure rates, allocation counts)
├── Thread-safe operations with mutex protection
└── Fallback mechanisms for allocation failures
```

### PSRAM Task Examples Module
```
psram_task_examples.c
├── Demonstration of PSRAM allocation strategies
├── Critical task implementation (internal RAM)
├── Data processing task implementation (PSRAM stack)
├── Web server task implementation (PSRAM stack)
├── Memory usage pattern examples
└── Integration with existing monitoring systems
```

### PSRAM Test Suite Module
```
psram_test_suite.c
├── 7-phase comprehensive testing framework
├── PSRAM detection and availability validation
├── Allocation strategy testing across all priority levels
├── Task creation testing with different stack strategies
├── Memory usage monitoring and analysis
├── Health check and functionality validation
└── Statistics reporting and success rate analysis
```

### Debug Configuration System
```
debug_config.h
├── Centralized control for all monitoring systems
├── Compile-time flags for production deployment
├── Configurable reporting intervals
└── Timestamp control for debug output
```

## Key Technical Decisions

### 1. Thread Safety Strategy
- **Decision**: Use FreeRTOS mutexes for all shared data structures
- **Rationale**: Prevents data corruption in multi-task environment
- **Implementation**: Timeout-based mutex acquisition (100ms) to prevent deadlocks

### 2. Memory Management Strategy
- **Decision**: Monitor but don't interfere with normal allocation/deallocation
- **Rationale**: Maintains system performance while providing visibility
- **Implementation**: Periodic sampling of heap statistics

### 3. Stack Monitoring Strategy
- **Decision**: Use FreeRTOS high water mark tracking
- **Rationale**: Leverages built-in RTOS capabilities for accuracy
- **Implementation**: Calculate usage percentages and issue warnings at 70%, 80%, 90%

### 4. Debug Output Strategy
- **Decision**: Compile-time configurable debug output
- **Rationale**: Zero performance impact in production builds
- **Implementation**: Macro-based system with timestamp support

## Error Handling Patterns

### 1. Graceful Degradation
- Monitoring systems continue operating even if individual components fail
- Non-critical errors logged but don't stop system operation
- Fallback mechanisms for resource allocation failures

### 2. Early Warning System
- Threshold-based alerts before critical conditions
- Multiple warning levels (Notice, Warning, Critical)
- Proactive detection prevents system failures

### 3. Resource Protection
- Stack overflow prevention through adequate sizing
- Memory leak detection through trend analysis
- Automatic recovery mechanisms where possible

## Scalability Considerations

### 1. Modular Design
- Each monitoring system is independent and can be enabled/disabled
- New monitoring modules can be added without affecting existing systems
- Configuration system scales to accommodate new parameters

### 2. Performance Impact
- Monitoring overhead kept minimal (<1% CPU usage)
- Configurable reporting intervals to balance visibility vs. performance
- Thread-safe design allows monitoring to run on separate cores

### 3. Memory Footprint
- Fixed-size data structures to prevent dynamic allocation overhead
- Configurable limits for tracked items (tasks, memory samples)
- Efficient data structures optimized for embedded systems

## Integration Patterns

### 1. Initialization Sequence
```c
1. Initialize monitoring systems (memory_monitor_init, task_tracker_init)
2. Initialize filesystem (storage_manager_init)
3. Start monitoring tasks (memory_monitor_start, task_tracker_start)
4. Register callbacks for task lifecycle events
5. System ready for web server implementation (following webServerImplementationPlan.md)
```

### 2. ESP-IDF HTTP Server Integration Pattern
**REFERENCE**: `memory-bank/webServerImplementationPlan.md` for detailed architecture

**ESP-IDF HTTP Server Architecture**:
- WebServerManager using `esp_http_server.h` component
- URI handlers registered with `httpd_register_uri_handler()`
- Event system integration with `ESP_HTTP_SERVER_EVENT`
- Configuration using `httpd_config_t` structure
- Integration with existing monitoring systems

**Request Flow Pattern**:
```
HTTP Request → ESP-IDF HTTP Server → URI Handler → Controller → Manager → HTTP Response
```

**ESP-IDF Integration Points**:
- Server lifecycle events logged through ESP-IDF event system
- Task monitoring integration (server runs in dedicated task)
- Memory monitoring of HTTP server heap usage
- Configuration following ESP-IDF best practices

**CRITICAL: Dynamic and Explicit Route Registration Pattern (Discovered August 5, 2025)**
**Problem**: The ESP-IDF HTTP server's wildcard router (`*`) is extremely limited. It cannot match multi-segment paths (e.g., `/api/io/points/ID_1/set`) and does not support wildcards in the middle of a URI pattern (e.g., `/api/io/points/*/set`).
**Solution**: Bypass the wildcard limitations by dynamically generating and registering an exact-match route for every controllable endpoint at startup.
**Implementation (`io_test_controller.c`)**:
1.  **Read Configuration**: At initialization, get a list of all configured IO points from the `io_manager`.
2.  **Iterate and Generate**: Loop through each point. If it's a controllable output, dynamically allocate and create the full, explicit URI string (e.g., `/api/io/points/SR_OUT_0_0/set`).
3.  **Register Exact Route**: Create a new `httpd_uri_t` for the generated string and register it with `httpd_register_uri_handler`.
4.  **Result**: The server has a hardcoded list of all valid routes, ensuring 100% reliable matching without relying on the faulty wildcard system. This is the required pattern for all complex, multi-endpoint APIs in this project.

### 3. Periodic Reporting
- Automatic reports based on configurable intervals
- Manual report triggering for debugging
- Multiple report formats (summary, detailed, analysis)
- Web API endpoints for real-time monitoring data

### 4. Callback Integration
- Task creation/deletion callbacks for lifecycle tracking
- Memory threshold callbacks for leak detection
- Stack warning callbacks for overflow prevention
- Web server task integration with existing monitoring

## Industrial I/O Architecture Patterns

### Discovered Architecture (January 30, 2025)
**Source**: `io_config full_ES32D26 wEnabledProcessing.json` and `SNRv8_IO_Configuration_and_Processing_Architecture.md`

### 1. Configuration-Driven Hardware Pattern
**Pattern**: JSON configuration drives all hardware initialization and behavior
- **Implementation**: Comprehensive JSON schema defining I/O points, signal processing, and alarm rules
- **Key Features**:
  - Hardware abstraction through configuration
  - Runtime reconfiguration without code changes
  - Type-safe I/O point definitions
  - Validation and error checking

### 2. Shift Register I/O Pattern
**Pattern**: Scalable I/O expansion using shift register chains
- **Hardware**: 74HC595 (outputs) + 74HC165 (inputs)
- **Implementation**: Thread-safe bit manipulation with mutex protection
- **Key Features**:
  - 8 relay outputs (SOLENOID/LIGHTING types)
  - 8 digital inputs with inversion support
  - Atomic read/write operations
  - Hardware timing requirements (5μs load pulse, 1μs clock transitions)

### 3. Signal Processing Pipeline Pattern
**Pattern**: Multi-stage signal conditioning for analog inputs
- **Pipeline**: Raw ADC → Offset → Gain → Scaling → Lookup Table → Precision → Filtering
- **Implementation**: Configurable per-channel processing
- **Key Features**:
  - SMA filtering with configurable window size
  - Linear interpolation lookup tables
  - Gain/offset compensation
  - Precision control (decimal places)

### 4. Alarm System Pattern
**Pattern**: Multi-type alarm detection with state management
- **Alarm Types**: Rate of change, disconnection, stuck signal, max value
- **Implementation**: Historical analysis with configurable thresholds
- **Key Features**:
  - Persistence requirements (multiple samples)
  - Hysteresis for alarm clearing
  - Trust system for sensor reliability
  - Manual reset capability

### 5. Thread-Safe State Management Pattern
**Pattern**: Centralized state repository with mutex protection
- **Implementation**: IOStateManager with copy-based access
- **Key Features**:
  - Prevents lock contention through data copying
  - Timeout-based mutex acquisition
  - Separate configuration and runtime state maps
  - Atomic state updates

### 6. Hardware Abstraction Layer Pattern
**Pattern**: Unified interface for different I/O types
- **Types**: GPIO_AI, GPIO_BO, SHIFT_REG_BI, SHIFT_REG_BO
- **Implementation**: Polymorphic I/O point handling
- **Key Features**:
  - Type-specific behavior (SOLENOID vs LIGHTING)
  - Safety validation for outputs
  - Calibration data integration
  - Flow rate calculations

### 7. Safety Interlock Pattern
**Pattern**: Multi-level safety system for industrial control
- **Implementation**: Boot-time shutdown, manual override timeouts, conflict detection
- **Key Features**:
  - Emergency stop functionality
  - Configuration change safety protocols
  - Manual override with automatic timeout
  - Audit trail for all operations

### 8. Real-Time Processing Pattern
**Pattern**: Dedicated FreeRTOS tasks for time-critical operations
- **Task Structure**:
  - I/O Polling Task (1000ms) - Input reading and signal conditioning
  - Alarm Processing Task (5000ms) - Alarm analysis and state management
  - Schedule Executor Task (60000ms) - Irrigation control logic
- **Key Features**:
  - Priority-based task scheduling
  - Yield calls prevent watchdog timeouts
  - Resource-aware task design

## Integration Architecture

### Configuration + I/O Framework Integration (Step 6)
**Pattern**: Combined configuration management and hardware control
- **ConfigManager**: Loads and serves JSON configuration
- **IOManager**: Implements hardware abstraction based on configuration
- **Controllers**: REST APIs for configuration and I/O control
- **Integration Points**:
  - Configuration validation before hardware changes
  - Runtime reconfiguration with safety checks
  - Monitoring integration for all I/O operations
  - Storage persistence for configuration changes

### Web Server Integration Pattern
**Pattern**: RESTful API for industrial I/O control
- **Configuration API**: `/api/config/*` endpoints for configuration management
- **I/O Control API**: `/api/io/*` endpoints for real-time control and monitoring
- **Safety Integration**: All web operations subject to safety validation
- **Real-Time Data**: WebSocket or polling for live I/O status

This industrial-grade architecture represents a significant evolution from simple irrigation control to a comprehensive automation platform suitable for commercial applications.
