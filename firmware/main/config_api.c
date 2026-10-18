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
#include "nvs_funcs.h"
#include "keymap.h"
#include "keypress_handles.h"
#include "rgb_led.h"
#include "uuid.h"
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
// layers.*

static const char *ROW_KEYS[MATRIX_ROWS] = {"row0", "row1", "row2", "row3"};

static int find_layer(const char *uuid)
{
	for (int pos = 0; pos < layers_num; pos++)
	{
		if (strcmp(key_layouts[pos].uuid_str, uuid) == 0)
			return pos;
	}
	return -1;
}

static cJSON *get_uuid_param(cJSON *params, config_error_t *err, int *pos)
{
	cJSON *uuid = cJSON_GetObjectItem(params, "uuid");
	if (!cJSON_IsString(uuid))
		return fail(err, CONFIG_ERR_BAD_REQUEST, "missing uuid");
	*pos = find_layer(uuid->valuestring);
	if (*pos < 0)
		return fail(err, CONFIG_ERR_NOT_FOUND, "layer not found");
	return uuid;
}

static cJSON *layer_to_json(int pos)
{
	const dd_layer *layer = &key_layouts[pos];
	cJSON *json = cJSON_CreateObject();
	cJSON_AddStringToObject(json, "uuid", layer->uuid_str);
	cJSON_AddStringToObject(json, "name", layer->name);
	cJSON_AddBoolToObject(json, "active", layer->active);
	for (int row = 0; row < MATRIX_ROWS; row++)
	{
		cJSON *keys = cJSON_AddArrayToObject(json, ROW_KEYS[row]);
		for (int col = 0; col < MATRIX_COLS; col++)
		{
			cJSON *key = cJSON_CreateObject();
			cJSON_AddStringToObject(key, "name", layer->key_map_names[row][col]);
			cJSON_AddNumberToObject(key, "key_code", layer->key_map[row][col]);
			cJSON_AddItemToArray(keys, key);
		}
	}
	const uint16_t *maps[2] = {layer->left_encoder_map, layer->right_encoder_map};
	const char *map_keys[2] = {"left_encoder_map", "right_encoder_map"};
	for (int m = 0; m < 2; m++)
	{
		cJSON *map = cJSON_AddObjectToObject(json, map_keys[m]);
		for (int i = 0; i < ENCODER_SIZE; i++)
			cJSON_AddNumberToObject(map, encoder_items_names[i], maps[m][i]);
	}
	return json;
}

static bool copy_name(char *dst, size_t size, cJSON *src)
{
	if (!cJSON_IsString(src) || strlen(src->valuestring) >= size)
		return false;
	strcpy(dst, src->valuestring);
	return true;
}

// Fill `layer` from its JSON. Every field is checked: the HTTP handlers this replaces wrote encoder
// maps without bounds and dereferenced missing rows.
static bool layer_from_json(cJSON *json, dd_layer *layer, config_error_t *err)
{
	memset(layer, 0, sizeof(*layer));
	if (!copy_name(layer->name, sizeof(layer->name), cJSON_GetObjectItem(json, "name")) || layer->name[0] == '\0')
	{
		fail(err, CONFIG_ERR_BAD_REQUEST, "name: 1 to 14 characters");
		return false;
	}
	layer->active = cJSON_IsTrue(cJSON_GetObjectItem(json, "active"));

	for (int row = 0; row < MATRIX_ROWS; row++)
	{
		cJSON *keys = cJSON_GetObjectItem(json, ROW_KEYS[row]);
		if (!cJSON_IsArray(keys) || cJSON_GetArraySize(keys) != MATRIX_COLS)
		{
			fail(err, CONFIG_ERR_BAD_REQUEST, "rows: row0..row3 with 4 keys each");
			return false;
		}
		for (int col = 0; col < MATRIX_COLS; col++)
		{
			cJSON *key = cJSON_GetArrayItem(keys, col);
			cJSON *code = cJSON_GetObjectItem(key, "key_code");
			if (!copy_name(layer->key_map_names[row][col], sizeof(layer->key_map_names[row][col]), cJSON_GetObjectItem(key, "name")) ||
				!cJSON_IsNumber(code) || code->valueint < 0 || code->valueint > UINT16_MAX)
			{
				fail(err, CONFIG_ERR_BAD_REQUEST, "key: name up to 6 characters and a key_code");
				return false;
			}
			layer->key_map[row][col] = code->valueint;
		}
	}

	uint16_t *maps[2] = {layer->left_encoder_map, layer->right_encoder_map};
	const char *map_keys[2] = {"left_encoder_map", "right_encoder_map"};
	for (int m = 0; m < 2; m++)
	{
		cJSON *map = cJSON_GetObjectItem(json, map_keys[m]);
		if (!cJSON_IsObject(map))
		{
			fail(err, CONFIG_ERR_BAD_REQUEST, "missing encoder map");
			return false;
		}
		// by name: the old handlers relied on the order of the JSON keys
		for (int i = 0; i < ENCODER_SIZE; i++)
		{
			cJSON *code = cJSON_GetObjectItem(map, encoder_items_names[i]);
			maps[m][i] = (cJSON_IsNumber(code) && code->valueint >= 0 && code->valueint <= UINT16_MAX) ? code->valueint : 0;
		}
	}
	return true;
}

static void refresh_leds(void)
{
#ifdef RGB_LEDS
	rgb_mode_t led_mode;
	nvs_load_led_mode(&led_mode);
	xQueueSend(keyled_q, &led_mode, 0);
#endif
}

// Layers are re-ordered in NVS (active ones first) when one is written: keep the same layer active
// when it still exists, instead of jumping back to layer 0 as the old handlers did
static void reselect_layer(const char *uuid)
{
	int pos = find_layer(uuid);
	set_current_layout(pos >= 0 ? pos : 0);
	refresh_leds();
}

static void current_uuid(char *out)
{
	strcpy(out, current_layout < layers_num ? key_layouts[current_layout].uuid_str : "");
}

static cJSON *m_layers_list(cJSON *params, config_error_t *err)
{
	cJSON *result = cJSON_CreateObject();
	cJSON *data = cJSON_AddArrayToObject(result, "data");
	for (int pos = 0; pos < layers_num; pos++)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", key_layouts[pos].name);
		cJSON_AddNumberToObject(item, "pos", pos);
		cJSON_AddBoolToObject(item, "active", key_layouts[pos].active);
		cJSON_AddStringToObject(item, "uuid", key_layouts[pos].uuid_str);
		cJSON_AddItemToArray(data, item);
	}
	return result;
}

static cJSON *m_layers_get(cJSON *params, config_error_t *err)
{
	int pos;
	if (get_uuid_param(params, err, &pos) == NULL)
		return NULL;
	return layer_to_json(pos);
}

static cJSON *m_layers_create(cJSON *params, config_error_t *err)
{
	dd_layer layer;
	if (!layer_from_json(params, &layer, err))
		return NULL;
	if (layers_num >= MAX_LAYOUT_NUMBER)
		return fail(err, CONFIG_ERR_LIMIT, "maximum number of layers reached");

	cJSON *uuid = cJSON_GetObjectItem(params, "uuid");
	if (cJSON_IsString(uuid) && uuid->valuestring[0] != '\0')
	{
		if (!copy_name(layer.uuid_str, sizeof(layer.uuid_str), uuid))
			return fail(err, CONFIG_ERR_BAD_REQUEST, "uuid: up to 6 characters");
	}
	else
	{
		uuid_t uu;
		uuid_generate(uu);
		short_uuid_unparse(uu, layer.uuid_str);
	}
	if (find_layer(layer.uuid_str) >= 0)
		return fail(err, CONFIG_ERR_BAD_REQUEST, "uuid already used");

	char active_uuid[SHORT_UUID_STR_LEN];
	current_uuid(active_uuid);
	if (nvs_create_new_layer(layer) != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "could not store the layer");
	reselect_layer(active_uuid);

	cJSON *result = cJSON_CreateObject();
	cJSON_AddStringToObject(result, "uuid", layer.uuid_str);
	return result;
}

static cJSON *m_layers_update(cJSON *params, config_error_t *err)
{
	int pos;
	cJSON *uuid = get_uuid_param(params, err, &pos);
	if (uuid == NULL)
		return NULL;
	dd_layer layer;
	if (!layer_from_json(params, &layer, err))
		return NULL;
	strcpy(layer.uuid_str, key_layouts[pos].uuid_str);

	char active_uuid[SHORT_UUID_STR_LEN];
	current_uuid(active_uuid);
	if (nvs_write_layer(layer, pos) != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "could not store the layer");
	reselect_layer(active_uuid);
	return cJSON_CreateObject();
}

static cJSON *m_layers_delete(cJSON *params, config_error_t *err)
{
	int pos;
	if (get_uuid_param(params, err, &pos) == NULL)
		return NULL;
	if (layers_num <= 1)
		return fail(err, CONFIG_ERR_LIMIT, "the last layer cannot be deleted");

	char active_uuid[SHORT_UUID_STR_LEN];
	current_uuid(active_uuid);
	if (nvs_delete_layer(pos) != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "could not delete the layer");
	reselect_layer(active_uuid);
	return cJSON_CreateObject();
}

static cJSON *m_layers_restore(cJSON *params, config_error_t *err)
{
	if (nvs_restore_default_layers() != ESP_OK || nvs_restore_default_macros() != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "could not restore the defaults");
	set_current_layout(0);
	refresh_leds();
	return cJSON_CreateObject();
}

static cJSON *m_layers_set_active(cJSON *params, config_error_t *err)
{
	int pos;
	if (get_uuid_param(params, err, &pos) == NULL)
		return NULL;
	if (!key_layouts[pos].active)
		return fail(err, CONFIG_ERR_BAD_REQUEST, "layer is disabled");
	set_current_layout(pos);
	refresh_leds();
	return cJSON_CreateObject();
}

// ---------------------------------------------------------------------------------------------
// Dispatch

static const config_method_t methods[] = {
	{"device.info", m_device_info},
	{"layers.list", m_layers_list},
	{"layers.get", m_layers_get},
	{"layers.create", m_layers_create},
	{"layers.update", m_layers_update},
	{"layers.delete", m_layers_delete},
	{"layers.restore", m_layers_restore},
	{"layers.set_active", m_layers_set_active},
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
