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
