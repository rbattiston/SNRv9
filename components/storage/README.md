# Storage Component

This component handles all storage-related functionality for the SNRv9 irrigation system.

## Files

### Source Files
- `storage_manager.c` - Main storage management functionality
- `auth_manager.c` - Authentication and user management

### Header Files
- `include/storage_manager.h` - Storage manager interface
- `include/auth_manager.h` - Authentication manager interface

## Functionality

### Storage Manager
- File system operations
- Configuration storage
- Data persistence
- LittleFS integration

### Authentication Manager
- User authentication
- Session management
- Security tokens
- Access control

## Dependencies
- ESP-IDF LittleFS component
- ESP timer for session management
- Core component for system utilities

## Usage
This component is used by the web component for handling user authentication and by the main application for configuration storage.
