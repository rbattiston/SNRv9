# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources}
                       REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_littlefs)

# Create a partition image for LittleFS.
# This will create an empty image. To add files, create a directory
# (e.g., 'data/') and add "data" as the last argument.
littlefs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT)
