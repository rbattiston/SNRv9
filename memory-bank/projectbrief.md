# SNRv9 Irrigation Control System - Project Brief

## Project Overview
SNRv9 is a commercial-grade irrigation control system built on the ESP32 platform using ESP-IDF framework and FreeRTOS. The system is designed for high reliability, stability, and comprehensive monitoring capabilities suitable for commercial deployment.

## Core Requirements
1. **Relay/Solenoid Control**: Precise control of irrigation valves and pumps
2. **Sensor Monitoring**: Real-time monitoring of soil moisture, temperature, and other environmental sensors
3. **Web Server**: HTTP interface for remote monitoring and control
4. **Data Recording & Trending**: Historical data storage and analysis capabilities
5. **Memory Monitoring**: Comprehensive memory usage tracking and leak detection
6. **Task Monitoring**: FreeRTOS task lifecycle and stack usage monitoring

## Technical Foundation
- **Platform**: ESP32 (ESP32-D0WD-V3)
- **Framework**: ESP-IDF 5.4.1
- **RTOS**: FreeRTOS (integrated with ESP-IDF)
- **Development Environment**: PlatformIO
- **Language**: C
- **Memory**: 320KB RAM, 4MB Flash (note: current board has 2MB flash)

## Quality Standards
- **Commercial Grade**: Production-ready reliability and stability
- **Comprehensive Monitoring**: All system resources tracked and analyzed
- **Proactive Issue Detection**: Early warning systems for potential problems
- **Configurable Debugging**: Production-safe debug output control
- **Memory Safety**: Stack overflow prevention and memory leak detection

## Current Development Phase
**Phase 1: Foundation Systems** (COMPLETED)
- Memory monitoring system with leak detection
- Task tracking with stack overflow prevention
- Configurable debug output system
- Early warning mechanisms for critical conditions

## Success Criteria
- Zero system crashes or reboots under normal operation
- Real-time visibility into all system resources
- Proactive detection of potential issues before they cause failures
- Production-ready code quality and documentation
- Scalable architecture for future feature additions

## Project Goals
Transform this ESP32 project into a robust, commercial-grade irrigation control system with enterprise-level monitoring, reliability, and maintainability.
