idf_component_register(
    SRCS "memory_monitor.c"
         "task_tracker.c"
         "psram_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES "freertos"
             "esp_timer"
             "esp_system"
)
