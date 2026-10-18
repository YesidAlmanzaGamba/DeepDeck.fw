/**
 * @file config_service.h
 * @brief BLE GATT service used by the DeepDeck Command Center app to configure the device.
 *
 * The service carries text lines (one JSON message per line) in both directions:
 * - RX characteristic: the app writes chunks of at most MTU-3 bytes; chunks are joined until '\n'.
 * - TX characteristic: the device notifies chunks of at most MTU-3 bytes; the app joins them until '\n'.
 * Both require an encrypted (bonded) link.
 */

#ifndef CONFIG_SERVICE_H_
#define CONFIG_SERVICE_H_

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_gatts_api.h"

/// GATT application id of the config service (HID uses 0x1812, battery 0x180f)
#define CONFIG_SERVICE_APP_ID 0x00DD

/// Longest line accepted from the app, including the terminating '\n'
#define CONFIG_SERVICE_MAX_LINE 4096

/// Called with each complete line received from the app, without the '\n'
typedef void (*config_service_line_cb_t)(const char *line, size_t len);

/** @brief Register the GATT application. Call after the HID and battery apps are registered. */
esp_err_t config_service_register_app(void);

/** @brief Set the function called for every complete line received over BLE. */
void config_service_set_line_handler(config_service_line_cb_t handler);

/**
 * @brief Send one line to the app (a '\n' is appended), split into notifications.
 * @return ESP_ERR_INVALID_STATE if no app is connected or it did not enable notifications.
 */
esp_err_t config_service_send_line(const char *line, size_t len);

/**
 * @brief Tell a bonded host that the GATT database changed, so it discovers the config service.
 * Hosts paired with older firmware cached a database without it (Windows does, and refuses an
 * uncached discovery on a connected keyboard). Call once the link is encrypted.
 */
void config_service_indicate_service_changed(esp_bd_addr_t remote_bda);

/**
 * @brief GATT event hook, called by the GATT dispatcher before the HID profile.
 * @return true if the event belonged to the config service and must not reach the HID profile.
 */
bool config_service_gatts_event(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param);

#endif /* CONFIG_SERVICE_H_ */
