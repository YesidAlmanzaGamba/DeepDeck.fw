/**
 * @file config_api.c
 * @brief Configuration core shared by BLE, USB and HTTP. Methods and objects: docs/protocol.md
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_bt_device.h"
#include "keyboard_config.h"
#include "nvs_keymaps.h"
#include "hal_ble.h"
#include "config_api.h"

#define TAG "config_api"

typedef cJSON *(*config_method_fn)(cJSON *params, config_error_t *err);

typedef struct
{
	const char *name;
	config_method_fn fn;
} config_method_t;

static SemaphoreHandle_t api_lock = NULL;
static void (*deferred_action)(void) = NULL;

// ---------------------------------------------------------------------------------------------
// Helpers

static cJSON *fail(config_error_t *err, config_err_code_t code, const char *message)
{
	err->code = code;
	strlcpy(err->message, message, sizeof(err->message));
	return NULL;
}

// The current transport call wants this to run once its response is out (restart, WiFi apply)
static void defer(void (*action)(void))
{
	deferred_action = action;
}

const char *config_err_name(config_err_code_t code)
{
	switch (code)
	{
	case CONFIG_OK:
		return "ok";
	case CONFIG_ERR_BAD_REQUEST:
		return "bad_request";
	case CONFIG_ERR_NOT_FOUND:
		return "not_found";
	case CONFIG_ERR_LIMIT:
		return "limit";
	case CONFIG_ERR_UNKNOWN_METHOD:
		return "unknown_method";
	default:
		return "internal";
	}
}

const char *config_err_http_status(config_err_code_t code)
{
	switch (code)
	{
	case CONFIG_OK:
		return "200 OK";
	case CONFIG_ERR_NOT_FOUND:
		return "404 Not Found";
	case CONFIG_ERR_BAD_REQUEST:
	case CONFIG_ERR_LIMIT:
	case CONFIG_ERR_UNKNOWN_METHOD:
		return "400 Bad Request";
	default:
		return "500 Internal Server Error";
	}
}

// WiFi state read from the driver, not from the half-maintained flags in wifi_handles.c
static cJSON *wifi_status_json(void)
{
	cJSON *wifi = cJSON_CreateObject();
	wifi_mode_t mode = WIFI_MODE_NULL;
	if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL)
	{
		cJSON_AddStringToObject(wifi, "state", "off");
		return wifi;
	}
	if (mode == WIFI_MODE_AP)
	{
		cJSON_AddStringToObject(wifi, "state", "ap");
		return wifi;
	}

	wifi_ap_record_t ap;
	if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK)
	{
		cJSON_AddStringToObject(wifi, "state", "connecting");
		return wifi;
	}

	char ip[16] = "";
	esp_netif_ip_info_t ip_info;
	esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	if (sta != NULL && esp_netif_get_ip_info(sta, &ip_info) == ESP_OK && ip_info.ip.addr != 0)
		snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ip_info.ip));

	cJSON_AddStringToObject(wifi, "state", ip[0] ? "connected" : "connecting");
	cJSON_AddStringToObject(wifi, "ssid", (const char *)ap.ssid);
	cJSON_AddStringToObject(wifi, "ip", ip);
	cJSON_AddNumberToObject(wifi, "rssi", ap.rssi);
	return wifi;
}

// ---------------------------------------------------------------------------------------------
// device.*

static cJSON *m_device_info(cJSON *params, config_error_t *err)
{
	cJSON *info = cJSON_CreateObject();
	cJSON_AddNumberToObject(info, "proto", CONFIG_PROTOCOL_VERSION);
	cJSON_AddStringToObject(info, "fw", FIRMWARE_VERSION);
	cJSON_AddStringToObject(info, "name", halBLEGetDeviceName());

	char mac[18] = "";
	const uint8_t *addr = esp_bt_dev_get_address();
	if (addr != NULL)
		snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	cJSON_AddStringToObject(info, "mac", mac);
	cJSON_AddNumberToObject(info, "uptime_s", (double)(esp_timer_get_time() / 1000000));
	cJSON_AddNumberToObject(info, "heap", esp_get_free_heap_size());

	cJSON *layer = cJSON_AddObjectToObject(info, "layer");
	cJSON_AddNumberToObject(layer, "index", current_layout);
	if (current_layout < layers_num)
	{
		cJSON_AddStringToObject(layer, "uuid", key_layouts[current_layout].uuid_str);
		cJSON_AddStringToObject(layer, "name", key_layouts[current_layout].name);
	}
	cJSON_AddItemToObject(info, "wifi", wifi_status_json());
	return info;
}

// ---------------------------------------------------------------------------------------------
// Dispatch

static const config_method_t methods[] = {
	{"device.info", m_device_info},
};

void config_api_init(void)
{
	if (api_lock == NULL)
		api_lock = xSemaphoreCreateRecursiveMutex();
}

cJSON *config_api_call(const char *method, cJSON *params, config_error_t *err)
{
	err->code = CONFIG_OK;
	err->message[0] = '\0';
	if (method == NULL)
		return fail(err, CONFIG_ERR_BAD_REQUEST, "missing method");

	const config_method_t *entry = NULL;
	for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++)
	{
		if (strcmp(methods[i].name, method) == 0)
		{
			entry = &methods[i];
			break;
		}
	}
	if (entry == NULL)
		return fail(err, CONFIG_ERR_UNKNOWN_METHOD, method);

	xSemaphoreTakeRecursive(api_lock, portMAX_DELAY);
	cJSON *result = entry->fn(params, err);
	xSemaphoreGiveRecursive(api_lock);

	if (result == NULL && err->code == CONFIG_OK)
		fail(err, CONFIG_ERR_INTERNAL, "out of memory");
	if (err->code != CONFIG_OK)
		ESP_LOGW(TAG, "%s: %s (%s)", method, config_err_name(err->code), err->message);
	return result;
}

void config_api_run_deferred(void)
{
	void (*action)(void) = deferred_action;
	deferred_action = NULL;
	if (action != NULL)
		action();
}

char *config_api_handle_line(const char *line)
{
	config_error_t err = {CONFIG_OK, ""};
	cJSON *result = NULL;
	cJSON *request = cJSON_Parse(line);
	cJSON *id = NULL;

	if (!cJSON_IsObject(request))
	{
		fail(&err, CONFIG_ERR_BAD_REQUEST, "not a JSON object");
	}
	else
	{
		id = cJSON_GetObjectItem(request, "id");
		cJSON *method = cJSON_GetObjectItem(request, "method");
		cJSON *params = cJSON_GetObjectItem(request, "params");
		if (!cJSON_IsString(method))
			fail(&err, CONFIG_ERR_BAD_REQUEST, "missing method");
		else
			result = config_api_call(method->valuestring, params, &err);
	}

	cJSON *response = cJSON_CreateObject();
	if (cJSON_IsNumber(id))
		cJSON_AddNumberToObject(response, "id", id->valuedouble);
	else
		cJSON_AddNullToObject(response, "id");

	if (result != NULL)
	{
		cJSON_AddItemToObject(response, "result", result);
	}
	else
	{
		cJSON *error = cJSON_AddObjectToObject(response, "error");
		cJSON_AddStringToObject(error, "code", config_err_name(err.code));
		cJSON_AddStringToObject(error, "message", err.message);
	}

	char *out = cJSON_PrintUnformatted(response);
	cJSON_Delete(response);
	cJSON_Delete(request);
	return out;
}
