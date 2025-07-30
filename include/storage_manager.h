#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "esp_err.h"

/**
 * @brief Initializes the LittleFS filesystem.
 *
 * This function initializes the LittleFS virtual filesystem. It mounts the
 * partition labeled "storage" at the base path "/littlefs". If the mount
 * fails, it will format the partition and attempt to mount it again.
 *
 * @return ESP_OK on success, or an error code from esp_vfs_littlefs_register on failure.
 */
esp_err_t storage_manager_init(void);

/**
 * @brief Lists all files and directories in the LittleFS filesystem.
 *
 * This function recursively traverses the entire LittleFS filesystem and
 * prints detailed information about each file and directory found, including
 * file paths, sizes, and summary statistics.
 */
void storage_manager_list_filesystem(void);

#endif // STORAGE_MANAGER_H
