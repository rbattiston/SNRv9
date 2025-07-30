idf_component_register(
    SRCS "memory_monitor.c"
         "task_tracker.c"
    INCLUDE_DIRS "include"
    REQUIRES "freertos"
             "esp_timer"
             "esp_system"
)
