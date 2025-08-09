#include "storage_manager.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>

static const char *TAG = "storage_manager";

esp_err_t storage_manager_init(void)
{
    esp_err_t ret;
    
    // Initialize NVS first (required for time manager and other components)
    ESP_LOGI(TAG, "Initializing NVS flash...");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_LOGW(TAG, "NVS partition needs to be erased, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS flash: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "NVS flash initialized successfully");

    // Initialize LittleFS
    ESP_LOGI(TAG, "Initializing LittleFS");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    ESP_LOGI(TAG, "LittleFS initialized successfully");
    
    // List all files in the filesystem for debugging
    storage_manager_list_filesystem();
    
    return ESP_OK;
}

/**
 * @brief Recursively lists directory contents with detailed information
 * 
 * @param path Directory path to list
 * @param depth Current recursion depth for indentation
 * @param file_count Pointer to file counter
 * @param total_size Pointer to total size accumulator
 */
static void list_directory_recursive(const char* path, int depth, int* file_count, size_t* total_size)
{
    DIR* dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGW(TAG, "Failed to open directory: %s", path);
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip current and parent directory entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Build full path
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        // Get file statistics
        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0) {
            // Create indentation for directory structure
            char indent[32] = "";
            for (int i = 0; i < depth; i++) {
                strcat(indent, "  ");
            }

            if (S_ISDIR(file_stat.st_mode)) {
                // Directory entry
                ESP_LOGI(TAG, "%s%s/ (directory)", indent, entry->d_name);
                // Recursively list subdirectory
                list_directory_recursive(full_path, depth + 1, file_count, total_size);
            } else {
                // Regular file entry
                ESP_LOGI(TAG, "%s%s (%ld bytes)", indent, entry->d_name, file_stat.st_size);
                (*file_count)++;
                *total_size += file_stat.st_size;
            }
        } else {
            ESP_LOGW(TAG, "Failed to get stats for: %s", full_path);
        }
    }

    closedir(dir);
}

void storage_manager_list_filesystem(void)
{
    ESP_LOGI(TAG, "=== LittleFS Directory Listing ===");
    
    int file_count = 0;
    size_t total_size = 0;
    
    // List contents starting from root
    list_directory_recursive("/littlefs", 0, &file_count, &total_size);
    
    // Print summary statistics
    ESP_LOGI(TAG, "=== Summary ===");
    ESP_LOGI(TAG, "Total files: %d", file_count);
    ESP_LOGI(TAG, "Total size: %zu bytes", total_size);
    
    // Get filesystem usage information
    size_t total_space = 0, used_space = 0;
    esp_err_t ret = esp_littlefs_info("storage", &total_space, &used_space);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Filesystem: %zu/%zu bytes used (%.1f%% full)", 
                 used_space, total_space, (float)used_space / total_space * 100.0);
        ESP_LOGI(TAG, "Available: %zu bytes", total_space - used_space);
    } else {
        ESP_LOGW(TAG, "Failed to get filesystem info: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "=== End Directory Listing ===");
}

esp_err_t storage_manager_read_file(const char* file_path, char** content, size_t* size)
{
    if (!file_path || !content || !size) {
        return ESP_ERR_INVALID_ARG;
    }

    // Build full path
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "/littlefs/%s", file_path);

    // Open file for reading
    FILE* file = fopen(full_path, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", full_path);
        return ESP_ERR_NOT_FOUND;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size < 0) {
        ESP_LOGE(TAG, "Failed to get file size: %s", full_path);
        fclose(file);
        return ESP_FAIL;
    }

    // Allocate buffer for file contents
    char* buffer = malloc(file_size + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for file contents");
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    // Read file contents
    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        ESP_LOGE(TAG, "Failed to read complete file: %s (read %zu of %ld bytes)", 
                 full_path, bytes_read, file_size);
        free(buffer);
        return ESP_FAIL;
    }

    // Null-terminate the buffer
    buffer[file_size] = '\0';

    // Return results
    *content = buffer;
    *size = (size_t)file_size;

    ESP_LOGI(TAG, "Successfully read file: %s (%zu bytes)", file_path, (size_t)file_size);
    return ESP_OK;
}
