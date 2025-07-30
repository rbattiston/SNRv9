# Network Component

This component handles all network-related functionality for the SNRv9 Irrigation Control System.

## Features

- WiFi connection management
- Network status monitoring
- Connection retry logic
- Network event handling

## Files

- `wifi_handler.c` - WiFi connection and management implementation
- `include/wifi_handler.h` - WiFi handler interface

## Dependencies

- esp_wifi - ESP-IDF WiFi driver
- esp_netif - Network interface abstraction
- esp_event - Event handling system
- nvs_flash - Non-volatile storage for WiFi credentials
- lwip - Lightweight IP stack

## Usage

Initialize the WiFi handler during system startup and use the provided API to manage network connections.
