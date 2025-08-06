# Technical Context - SNRv9 Implementation

## Technology Stack

### Core Platform
- **Microcontroller**: ESP32-D0WD-V3 (Dual-core Xtensa LX6, 240MHz)
- **Memory**: 320KB SRAM, 2MB Flash
- **Connectivity**: WiFi 802.11 b/g/n, Bluetooth Classic/BLE
- **Development Framework**: ESP-IDF 5.4.1
- **Real-Time OS**: FreeRTOS (integrated with ESP-IDF)

### Development Environment
- **IDE**: PlatformIO (VS Code extension)
- **Build System**: CMake (via PlatformIO)
- **Toolchain**: Xtensa ESP32 GCC 14.2.0
- **Programming Language**: C (C99 standard)
- **Version Control**: Git (GitHub repository)

### Key Libraries and Components
- **ESP-IDF Components**:
  - `freertos`: Real-time operating system
  - `esp_log`: Logging system
  - `esp_timer`: High-resolution timer
  - `heap`: Memory management
  - `esp_system`: System utilities
  - `esp_http_server`: HTTP server functionality (Step 2+). **Note**: This component has a very basic wildcard router. See "Web Server API Development" section for critical limitations and required patterns.
  - `esp_wifi`: WiFi connectivity (Step 1+)
  - `esp_event`: Event handling system
  - `nvs_flash`: Non-volatile storage
  - `littlefs`: Filesystem for flash storage

## Development Setup

### Project Structure
```
SNRv9/
├── include/           # Header files
│   ├── debug_config.h    # Debug configuration
│   ├── memory_monitor.h  # Memory monitoring interface
│   ├── task_tracker.h    # Task tracking interface
│   └── storage_manager.h # Filesystem management
├── src/               # Source files
│   ├── main.c            # Application entry point
│   ├── memory_monitor.c  # Memory monitoring implementation
│   ├── task_tracker.c    # Task tracking implementation
│   └── storage_manager.c # Filesystem management
├── memory-bank/       # Project documentation
├── platformio.ini     # PlatformIO configuration
├── CMakeLists.txt     # Build configuration
└── sdkconfig.defaults # ESP-IDF default configuration
```

### Build Configuration
- **Platform**: espressif32 (6.11.0)
- **Board**: esp32dev
- **Framework**: espidf
- **Monitor Speed**: 115200 baud
- **Upload Protocol**: esptool

### Compiler Settings
- **Optimization**: Release mode (-Os)
- **Debug Info**: Enabled for development
- **Warnings**: High warning level enabled
- **Standards**: C99 compliance

## Technical Constraints

### Hardware Limitations
- **RAM**: 320KB total (shared between heap, stack, and static allocation)
- **Flash**: 2MB available
- **CPU**: Dual-core but currently using single-core approach
- **Real-time Requirements**: FreeRTOS tick rate and task scheduling

### Memory Management
- **Heap Management**: ESP-IDF heap allocator with multiple regions
- **Stack Allocation**: Fixed-size stacks per task (minimum 2048 bytes for ESP32)
- **Static vs Dynamic**: Preference for static allocation where possible
- **Memory Regions**: DRAM, D/IRAM, IRAM with different characteristics

### Performance Considerations
- **Task Priorities**: Careful priority assignment to prevent starvation
- **Interrupt Handling**: ISR-safe functions required for interrupt context
- **Watchdog Timers**: System and task watchdogs must be fed regularly
- **Power Management**: Considerations for low-power operation

## Dependencies

### ESP-IDF Framework Dependencies
```c
#include "freertos/FreeRTOS.h"     // Core RTOS functionality
#include "freertos/task.h"         // Task management
#include "freertos/semphr.h"       // Semaphores and mutexes
#include "esp_log.h"               // Logging system
#include "esp_timer.h"             // High-resolution timers
#include "esp_system.h"            // System utilities
#include "esp_chip_info.h"         // Hardware information
#include "esp_littlefs.h"          // LittleFS filesystem support
```

### Standard C Library Dependencies
```c
#include <stdio.h>                 // Standard I/O
#include <string.h>                // String manipulation
#include <stdlib.h>                // Memory allocation
#include <stdint.h>                // Fixed-width integers
#include <stdbool.h>               // Boolean type
```

## Configuration Management

### Debug Configuration System
**File**: `include/debug_config.h`
- Centralized control for all debug features
- Compile-time flags for production deployment
- Configurable reporting intervals and thresholds
- Timestamp control for debug output

### Key Configuration Parameters
```c
#define DEBUG_MEMORY_MONITORING     1    // Enable memory monitoring
#define DEBUG_TASK_TRACKING        1    // Enable task tracking
#define DEBUG_INCLUDE_TIMESTAMPS   1    // Include timestamps in output
#define DEBUG_MEMORY_REPORT_INTERVAL_MS  30000  // Memory report interval
#define DEBUG_TASK_REPORT_INTERVAL_MS    10000  // Task report interval
#define DEBUG_MAX_TASKS_TRACKED    16    // Maximum tasks to track
```

## Build and Deployment

### Build Process
1. **Configuration**: PlatformIO reads `platformio.ini`
2. **Preprocessing**: Header files processed, macros expanded
3. **Compilation**: C source files compiled to object files
4. **Linking**: Object files linked with ESP-IDF libraries
5. **Binary Generation**: ELF file converted to flashable binary
6. **Size Analysis**: Memory usage analysis performed

### Deployment Process
1. **Upload**: Binary uploaded via esptool over serial connection
2. **Verification**: Flash content verified against uploaded binary
3. **Reset**: Hardware reset to start new firmware
4. **Monitoring**: Serial monitor for debug output and verification

### Memory Usage Analysis
- **RAM Usage**: Currently 4.4% (14,352 bytes of 327,680 bytes) - optimized after demo removal
- **Flash Usage**: Currently 19.2% (200,860 bytes of 1,048,576 bytes) - reduced after cleanup
- **Stack Allocation**: System tasks only (demo tasks removed, ready for irrigation control)
- **Task Count**: 5 tasks (main, 2 idle, 2 ipc) - streamlined for production

## Development Practices

### Code Quality Standards
- **Naming Conventions**: Snake_case for functions and variables
- **Documentation**: Doxygen-style comments for all public functions
- **Error Handling**: Consistent error checking and reporting
- **Thread Safety**: Mutex protection for shared data structures

### Testing Approach
- **Unit Testing**: Individual module testing (planned)
- **Integration Testing**: System-level testing with real hardware
- **Stress Testing**: Extended operation testing for memory leaks
- **Performance Testing**: Resource usage monitoring under load

### Version Control Strategy
- **Repository**: GitHub (https://github.com/rbattiston/SNRv9.git)
- **Branching**: Feature branches for development
- **Commits**: Descriptive commit messages with issue references
- **Documentation**: Memory bank maintained alongside code changes

### Web Server API Development
**CRITICAL: ESP-IDF HTTP Server Limitations (Discovered August 5, 2025)**
- **Wildcard Routing**: The `esp_http_server` component's wildcard (`*`) is extremely limited.
  - It only matches a **single path segment**.
  - It **cannot** be used in the middle of a URI pattern (e.g., `/api/items/*/action` is invalid).
- **Required Pattern**: Due to these limitations, the only reliable method for creating RESTful APIs with dynamic IDs is to **dynamically generate and register the full, explicit URI for each endpoint at startup**.
- **Reference**: See the "Dynamic and Explicit Route Registration Pattern" in `memory-bank/systemPatterns.md` for the full implementation details. All future API development must follow this pattern to avoid 404 errors.
