idf_component_register(
    SRCS "web_server_manager.c"
         "static_file_controller.c"
         "system_controller.c"
         "auth_controller.c"
         "io_test_controller.c"
         "request_priority_manager.c"
         "request_queue.c"
         "request_priority_test_suite.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_http_server" "esp_littlefs" "json" "core" "storage" "network" "esp_timer" "esp_system" "esp_wifi"
)
