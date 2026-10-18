/**
 * @file config_api.h
 * @brief Configuration core of the DeepDeck, shared by BLE, USB and HTTP. See docs/protocol.md.
 *
 * Every configuration action is a method, e.g. "layers.get", that takes a cJSON params object and
 * returns a cJSON result. Calls are serialised by a mutex, whatever transport they come from.
 */

#ifndef CONFIG_API_H_
#define CONFIG_API_H_

#include <stdbool.h>
#include "cJSON.h"

#define CONFIG_PROTOCOL_VERSION 1

typedef enum
{
	CONFIG_OK = 0,
	CONFIG_ERR_BAD_REQUEST,
	CONFIG_ERR_NOT_FOUND,
	CONFIG_ERR_LIMIT,
	CONFIG_ERR_UNKNOWN_METHOD,
	CONFIG_ERR_INTERNAL,
} config_err_code_t;

typedef struct
{
	config_err_code_t code;
	char message[80];
} config_error_t;

/** @brief Snapshot of the device state, shown by device.info and by the on-device info screen. */
typedef struct
{
	char name[41];
	char mac[18];
	bool ble_connected; // a host (the PC) is connected
	bool app_connected; // the Command Center is connected over BLE
	char wifi_state[12]; // "off", "ap", "connecting", "connected"
	char ssid[33];
	char ip[16];
	int rssi;
	unsigned heap;
	unsigned uptime_s;
	unsigned layers;
	unsigned layer_index;
	char layer_name[16];
} device_status_t;

void device_status_get(device_status_t *status);

/**
 * @brief Text lines for one page of the on-device info screen (menu "Device info").
 * @return the number of lines written; lines[0] is the page title
 */
int device_status_lines(int page, char lines[][24], int max_lines);

/** @brief Create the mutex. Call once at boot, before any transport starts. */
void config_api_init(void);

/**
 * @brief Run one method.
 * @param params may be NULL
 * @param err set when NULL is returned
 * @return the result (the caller deletes it), or NULL on error
 */
cJSON *config_api_call(const char *method, cJSON *params, config_error_t *err);

/**
 * @brief Run a deferred action (restart, apply WiFi credentials) registered by the last call, if any.
 * Transports call it after the response was sent, so the client still gets the answer.
 */
void config_api_run_deferred(void);

/**
 * @brief Handle one request line {"id","method","params"} and build the response line.
 * @return the response (the caller frees it), never NULL except when out of memory
 */
char *config_api_handle_line(const char *line);

/** @brief Protocol name of an error code, e.g. "not_found". */
const char *config_err_name(config_err_code_t code);

/** @brief HTTP status line used by the web API for an error code, e.g. "404 Not Found". */
const char *config_err_http_status(config_err_code_t code);

#endif /* CONFIG_API_H_ */
