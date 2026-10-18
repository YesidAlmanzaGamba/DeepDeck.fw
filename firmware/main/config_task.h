/**
 * @file config_task.h
 * @brief Runs Command Center requests from BLE and USB one at a time, and sends device events.
 */

#ifndef CONFIG_TASK_H_
#define CONFIG_TASK_H_

#include <stdbool.h>
#include "cJSON.h"

/** @brief Start the task and the BLE and USB transports. */
void config_task_start(void);

/**
 * @brief Send an event to every connected client. Safe to call from any task (not from an ISR).
 * @param data event payload; ownership is taken (it is deleted here)
 */
void config_task_broadcast_event(const char *event, cJSON *data);

/** @brief Send a "layer.changed" event for the current layer. */
void config_task_layer_changed(bool hold);

/** @brief Hook WiFi and IP events to "wifi.changed". Call after the default event loop exists. */
void config_task_register_wifi_events(void);

#endif /* CONFIG_TASK_H_ */
