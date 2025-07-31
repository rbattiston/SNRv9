/**
 * @file psram_task_examples.h
 * @brief Header file for PSRAM task creation examples
 */

#ifndef PSRAM_TASK_EXAMPLES_H
#define PSRAM_TASK_EXAMPLES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create example web server task using PSRAM
 */
bool psram_create_web_server_task_example(void);

/**
 * @brief Create example data processing task using PSRAM
 */
bool psram_create_data_processing_task_example(void);

/**
 * @brief Create example critical task using internal RAM
 */
bool psram_create_critical_task_example(void);

/**
 * @brief Demonstrate PSRAM allocation strategies
 */
void psram_demonstrate_allocation_strategies(void);

/**
 * @brief Run all PSRAM examples
 */
void psram_run_all_examples(void);

/**
 * @brief Show current PSRAM usage and statistics
 */
void psram_show_usage_example(void);

#ifdef __cplusplus
}
#endif

#endif /* PSRAM_TASK_EXAMPLES_H */
