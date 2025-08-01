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

/**
 * @brief Reads a file from the LittleFS filesystem.
 *
 * This function reads the entire contents of a file into a dynamically
 * allocated buffer. The caller is responsible for freeing the buffer.
 *
 * @param file_path Path to the file to read (relative to /littlefs)
 * @param content Pointer to store the allocated buffer containing file contents
 * @param size Pointer to store the size of the file in bytes
 * @return ESP_OK on success, error code on failure
 */
esp_err_t storage_manager_read_file(const char* file_path, char** content, size_t* size);

#endif // STORAGE_MANAGER_H
