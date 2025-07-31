idf_component_register(
    SRCS "memory_monitor.c"
         "task_tracker.c"
         "psram_manager.c"
         "psram_task_examples.c"
         "psram_test_suite.c"
    INCLUDE_DIRS "include"
    REQUIRES "freertos"
             "esp_timer"
             "esp_system"
)
