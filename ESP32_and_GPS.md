
https://www.datasheethub.com/wp-content/uploads/2022/08/NEO6MV2-GPS-Module-Datasheet.pdf

# ESP32-Based GPS Time Sync for Irrigation Devices (ESP-IDF / PlatformIO)

## 🧱 Project Roles

### Time Server ESP32
- Equipped with a NEO-6M GPS module
- Retrieves UTC time from GPS via UART
- Scans for nearby irrigation ESP32s (in AP mode)
- Pushes time via HTTP request

### Irrigation ESP32s
- Operate in WiFi Access Point mode
- Expose a `/settime` HTTP endpoint
- Receive and apply time using `settimeofday()`

---

## 1. GPS UART Setup (Time Server)

```c
#define GPS_UART_NUM UART_NUM_1
#define GPS_TXD (GPIO_NUM_17)
#define GPS_RXD (GPIO_NUM_16)
#define BUF_SIZE (1024)

void init_gps_uart() {
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_driver_install(GPS_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(GPS_UART_NUM, &uart_config);
    uart_set_pin(GPS_UART_NUM, GPS_TXD, GPS_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}
```

---

## 2. Scan for Nearby Irrigation ESP32 APs

```c
void scan_for_irrigation_aps() {
    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = false
    };
    esp_wifi_scan_start(&scan_config, true);

    uint16_t num_aps = 20;
    wifi_ap_record_t ap_records[20];
    esp_wifi_scan_get_ap_records(&num_aps, ap_records);

    for (int i = 0; i < num_aps; ++i) {
        if (strstr((char*)ap_records[i].ssid, "IRRIG") != NULL) {
            // Try to connect and send time
        }
    }
}
```

---

## 3. HTTP GET to Push Time

```c
void send_time_to_esp(const char* ip, struct tm* timeinfo) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s/settime?hh=%02d&mm=%02d&ss=%02d",
             ip, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI("HTTP", "Time sent to %s", ip);
    } else {
        ESP_LOGE("HTTP", "Failed to send time: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}
```

---

## 4. HTTP Server on Irrigation ESP32

```c
esp_err_t settime_handler(httpd_req_t *req) {
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[8];
        int hh = 0, mm = 0, ss = 0;

        if (httpd_query_key_value(query, "hh", param, sizeof(param)) == ESP_OK) hh = atoi(param);
        if (httpd_query_key_value(query, "mm", param, sizeof(param)) == ESP_OK) mm = atoi(param);
        if (httpd_query_key_value(query, "ss", param, sizeof(param)) == ESP_OK) ss = atoi(param);

        struct tm t = {
            .tm_year = 2023 - 1900,
            .tm_mon = 0,
            .tm_mday = 1,
            .tm_hour = hh,
            .tm_min = mm,
            .tm_sec = ss
        };

        struct timeval now = { .tv_sec = mktime(&t) };
        settimeofday(&now, NULL);
    }

    httpd_resp_send(req, "Time Set", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

---

## 🔁 Sync Flow

1. Time Server gets GPS time.
2. Scans for APs like "IRRIG-XX".
3. Connects to each, sends time via HTTP.
4. Irrigation ESP32 updates its time.

---

## 🛠 Notes

- Use `esp_log_level_set("*", ESP_LOG_DEBUG);` to assist debugging.
- Schedule periodic syncs (e.g., hourly).
- Use `esp_timer_get_time()` to track time between syncs.

---

Generated for ESP-IDF + PlatformIO development in Visual Studio Code.
