# System Patterns - SNRv9 Architecture

## System Architecture Overview

### Layered Architecture
```
┌─────────────────────────────────────────┐
│           Application Layer             │
│  (Irrigation Logic, Web Server, etc.)  │
├─────────────────────────────────────────┤
│          Monitoring Layer               │
│   (Memory Monitor, Task Tracker)       │
├─────────────────────────────────────────┤
│           Hardware Layer                │
│    (ESP-IDF, FreeRTOS, Hardware)       │
└─────────────────────────────────────────┘
```

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
