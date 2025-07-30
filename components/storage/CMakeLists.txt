idf_component_register(
    SRCS "storage_manager.c"
         "auth_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_littlefs" "nvs_flash" "esp_partition" "esp_timer" "esp_system"
)
