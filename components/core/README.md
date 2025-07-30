# Core Component

## Overview
The Core component provides essential system monitoring and reliability features for the SNRv9 irrigation control system. This component implements comprehensive memory and task monitoring capabilities that are critical for commercial-grade embedded system operation.

## Features

### Memory Monitoring
- **Real-time heap tracking**: Continuous monitoring of free/used memory
- **Memory leak detection**: Trend analysis to identify potential leaks
- **Fragmentation analysis**: Monitoring of memory fragmentation levels
- **Configurable reporting**: Detailed and summary report formats
- **Thread-safe operation**: Mutex-protected data access for multi-task environments

### Task Monitoring
- **Task lifecycle tracking**: Monitor creation, deletion, and state changes
- **Stack overflow prevention**: Early warning system with multi-level alerts
- **Stack usage analysis**: Real-time calculation of stack usage percentages
- **Task state monitoring**: Track running, ready, blocked, and suspended states
- **Performance impact**: <1% CPU overhead for comprehensive monitoring

## Architecture

### Components
- `memory_monitor.c/h`: Memory usage tracking and leak detection
- `task_tracker.c/h`: FreeRTOS task monitoring and stack analysis

### Integration Points
- **Debug Configuration**: Controlled via `debug_config.h` for production builds
- **FreeRTOS Integration**: Uses task callbacks and system state APIs
- **ESP-IDF Integration**: Leverages ESP32-specific memory management functions

## Usage

### Initialization
```c
#include "memory_monitor.h"
#include "task_tracker.h"

// Initialize monitoring systems
memory_monitor_init();
task_tracker_init();

// Start monitoring tasks
memory_monitor_start();
task_tracker_start();
```

### Reporting
```c
// Generate detailed reports
memory_monitor_report_detailed();
task_tracker_report_detailed();

// Generate summary reports
memory_monitor_report_summary();
task_tracker_report_summary();

// Check for critical conditions
task_tracker_check_stack_warnings();
```

## Configuration

### Debug Control
Monitoring behavior is controlled via `debug_config.h`:
- `MEMORY_MONITOR_ENABLED`: Enable/disable memory monitoring
- `TASK_TRACKER_ENABLED`: Enable/disable task tracking
- `DEBUG_TIMESTAMPS_ENABLED`: Add timestamps to debug output

### Warning Thresholds
Stack usage warnings are issued at:
- **70%**: NOTICE level
- **80%**: WARNING level  
- **90%**: CRITICAL level with "OVERFLOW IMMINENT" alert

### Reporting Intervals
Default monitoring frequencies:
- Memory reports: 30 seconds
- Task reports: 10 seconds
- Stack warnings: 5 seconds

## Dependencies
- FreeRTOS (task management and synchronization)
- ESP-IDF esp_timer (high-resolution timestamps)
- ESP-IDF esp_system (memory management functions)

## Thread Safety
All monitoring functions are thread-safe and can be called from any task context. Internal data structures are protected by FreeRTOS mutexes with 100ms timeout to prevent deadlocks.

## Performance Impact
The monitoring system is designed for minimal performance impact:
- Monitoring tasks run at low priority (priority 1)
- Data collection uses efficient sampling techniques
- Configurable intervals balance visibility vs. overhead
- Zero impact when disabled via debug configuration

## Commercial Deployment
This component is designed for production use with:
- Compile-time debug control for zero overhead in production
- Comprehensive error handling and graceful degradation
- Industrial-grade reliability and stability
- Extensive documentation and maintainable code structure
