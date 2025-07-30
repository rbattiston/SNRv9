# Web Component

This component handles all web server functionality for the SNRv9 irrigation system.

## Files

### Source Files
- `web_server_manager.c` - Main web server management
- `static_file_controller.c` - Static file serving with advanced caching
- `system_controller.c` - System status and control API endpoints
- `auth_controller.c` - Authentication API endpoints

### Header Files
- `include/web_server_manager.h` - Web server manager interface
- `include/static_file_controller.h` - Static file controller interface
- `include/system_controller.h` - System controller interface
- `include/auth_controller.h` - Authentication controller interface
- `include/wifi_handler.h` - WiFi handler interface (copied from network component)

## Functionality

### Web Server Manager
- HTTP server initialization and management
- Route registration and handling
- Server lifecycle management

### Static File Controller
- Static file serving (HTML, CSS, JS, images)
- Advanced ETag-based caching system
- File-type specific cache policies
- Conditional request handling (304 Not Modified)
- Thread-safe cache management

### System Controller
- System status API endpoints
- Health monitoring endpoints
- System configuration endpoints
- Real-time system information

### Authentication Controller
- Login/logout API endpoints
- Session management
- Token validation
- User authentication flow

## Dependencies
- ESP-IDF HTTP Server component
- ESP-IDF LittleFS component
- JSON component for API responses
- Core component for system monitoring
- Storage component for authentication
- Network component for WiFi status
- ESP Timer and ESP System components

## Usage
This component provides the complete web interface for the irrigation system, including both the web UI (static files) and REST API endpoints for system control and monitoring.

## Features
- Production-grade HTTP caching with ETag support
- RESTful API design
- Secure authentication system
- Real-time system monitoring
- Responsive web interface
