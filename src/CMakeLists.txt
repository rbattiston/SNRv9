# Main application component
# Only includes main.c since all other functionality has been moved to components

idf_component_register(SRCS "main.c"
                       REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_littlefs 
                               core network web storage)

# Create a partition image for LittleFS.
# This will create an empty image. To add files, create a directory
# (e.g., 'data/') and add "data" as the last argument.
littlefs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT)
