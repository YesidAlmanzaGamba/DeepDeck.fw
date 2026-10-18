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
// macros.*  Macro N is stored as NVS "macro_<N+1>" and triggered by key code MACRO_BASE_VAL + N.

static cJSON *macro_to_json(const dd_macros *macro)
{
	cJSON *json = cJSON_CreateObject();
	cJSON_AddNumberToObject(json, "keycode", macro->keycode);
	cJSON_AddStringToObject(json, "name", macro->name);
	cJSON *keys = cJSON_AddArrayToObject(json, "key");
	for (int i = 0; i < MACRO_LEN; i++)
		cJSON_AddItemToArray(keys, cJSON_CreateNumber(macro->key[i]));
	return json;
}

static bool macro_from_json(cJSON *json, dd_macros *macro, config_error_t *err)
{
	memset(macro, 0, sizeof(*macro));
	if (!copy_name(macro->name, sizeof(macro->name), cJSON_GetObjectItem(json, "name")))
	{
		fail(err, CONFIG_ERR_BAD_REQUEST, "name: up to 9 characters");
		return false;
	}
	cJSON *keys = cJSON_GetObjectItem(json, "key");
	if (!cJSON_IsArray(keys) || cJSON_GetArraySize(keys) > MACRO_LEN)
	{
		fail(err, CONFIG_ERR_BAD_REQUEST, "key: up to 5 key codes");
		return false;
	}
	for (int i = 0; i < cJSON_GetArraySize(keys); i++)
	{
		cJSON *code = cJSON_GetArrayItem(keys, i);
		// HID key codes and modifiers (0xE0-0xE7). The old check (x >= 0 || x <= 1000) was always true.
		if (!cJSON_IsNumber(code) || code->valueint < 0 || code->valueint > 0xFF)
		{
			fail(err, CONFIG_ERR_BAD_REQUEST, "key: codes 0-255");
			return false;
		}
		macro->key[i] = code->valueint;
	}
	return true;
}

// Index of the macro with this key code, -1 if there is none
static int macro_index(cJSON *params, config_error_t *err)
{
	cJSON *keycode = cJSON_GetObjectItem(params, "keycode");
	if (!cJSON_IsNumber(keycode))
	{
		fail(err, CONFIG_ERR_BAD_REQUEST, "missing keycode");
		return -1;
	}
	int index = keycode->valueint - MACRO_BASE_VAL;
	if (index < 0 || index >= total_macros)
	{
		fail(err, CONFIG_ERR_NOT_FOUND, "macro not found");
		return -1;
	}
	return index;
}

static cJSON *m_macros_list(cJSON *params, config_error_t *err)
{
	cJSON *result = cJSON_CreateObject();
	cJSON *list = cJSON_AddArrayToObject(result, "macros");
	for (int i = 0; i < total_macros; i++)
		cJSON_AddItemToArray(list, macro_to_json(&user_macros[i]));
	return result;
}

static cJSON *m_macros_create(cJSON *params, config_error_t *err)
{
	dd_macros macro;
	if (!macro_from_json(params, &macro, err))
		return NULL;
	// nvs_create_new_macro appends: the key code is decided by the position, not by the client
	macro.keycode = MACRO_BASE_VAL + total_macros;
	if (macro.keycode > MACRO_HOLD_MAX_VAL)
		return fail(err, CONFIG_ERR_LIMIT, "maximum number of macros reached");
	if (nvs_create_new_macro(macro) != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "could not store the macro");
	cJSON *result = cJSON_CreateObject();
	cJSON_AddNumberToObject(result, "keycode", macro.keycode);
	return result;
}

static cJSON *m_macros_update(cJSON *params, config_error_t *err)
{
	int index = macro_index(params, err);
	if (index < 0)
		return NULL;
	dd_macros macro;
	if (!macro_from_json(params, &macro, err))
		return NULL;
	macro.keycode = MACRO_BASE_VAL + index;
	if (nvs_update_macro(macro) != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "could not store the macro");
	return cJSON_CreateObject();
}

static cJSON *m_macros_delete(cJSON *params, config_error_t *err)
{
	int index = macro_index(params, err);
	if (index < 0)
		return NULL;
	// Macros are addressed by position, so a deleted macro is emptied rather than removed
	dd_macros empty = {0};
	empty.keycode = MACRO_BASE_VAL + index;
	if (nvs_update_macro(empty) != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "could not clear the macro");
	return cJSON_CreateObject();
}

static cJSON *m_macros_restore(cJSON *params, config_error_t *err)
{
	if (nvs_restore_default_macros() != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "could not restore the default macros");
	return cJSON_CreateObject();
}

// ---------------------------------------------------------------------------------------------
// led.*

static cJSON *led_to_json(const rgb_mode_t *mode)
{
	cJSON *json = cJSON_CreateObject();
	cJSON_AddNumberToObject(json, "mode", mode->mode);
	cJSON_AddNumberToObject(json, "H", mode->H);
	cJSON_AddNumberToObject(json, "S", mode->S);
	cJSON_AddNumberToObject(json, "V", mode->V);
	cJSON_AddNumberToObject(json, "speed", mode->speed);
	cJSON *rgb = cJSON_AddArrayToObject(json, "rgb");
	for (int i = 0; i < 3; i++)
		cJSON_AddItemToArray(rgb, cJSON_CreateNumber(mode->rgb[i]));
	return json;
}

static bool read_byte(cJSON *params, const char *key, uint8_t *out, config_error_t *err)
{
	cJSON *item = cJSON_GetObjectItem(params, key);
	if (item == NULL)
		return true; // optional: keep the current value
	if (!cJSON_IsNumber(item) || item->valueint < 0 || item->valueint > 255)
	{
		char message[40];
		snprintf(message, sizeof(message), "%s: 0-255", key);
		fail(err, CONFIG_ERR_BAD_REQUEST, message);
		return false;
	}
	*out = item->valueint;
	return true;
}

static cJSON *m_led_get(cJSON *params, config_error_t *err)
{
	rgb_mode_t mode = {0};
	nvs_load_led_mode(&mode);
	return led_to_json(&mode);
}

static cJSON *m_led_set(cJSON *params, config_error_t *err)
{
	rgb_mode_t mode = {0};
	nvs_load_led_mode(&mode); // fields left out keep their stored value
	if (!read_byte(params, "mode", &mode.mode, err) || !read_byte(params, "H", &mode.H, err) ||
		!read_byte(params, "S", &mode.S, err) || !read_byte(params, "V", &mode.V, err) ||
		!read_byte(params, "speed", &mode.speed, err))
		return NULL;
	if (mode.mode > 5)
		return fail(err, CONFIG_ERR_BAD_REQUEST, "mode: 0-5");

	cJSON *rgb = cJSON_GetObjectItem(params, "rgb");
	if (rgb != NULL)
	{
		if (!cJSON_IsArray(rgb) || cJSON_GetArraySize(rgb) != 3)
			return fail(err, CONFIG_ERR_BAD_REQUEST, "rgb: [r,g,b]");
		for (int i = 0; i < 3; i++)
		{
			cJSON *c = cJSON_GetArrayItem(rgb, i);
			if (!cJSON_IsNumber(c) || c->valueint < 0 || c->valueint > 255)
				return fail(err, CONFIG_ERR_BAD_REQUEST, "rgb: 0-255");
			mode.rgb[i] = c->valueint;
		}
	}
	if (nvs_save_led_mode(mode) != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "could not store the LED mode");
#ifdef RGB_LEDS
	xQueueSend(keyled_q, &mode, 0);
#endif
	return cJSON_CreateObject();
}

// ---------------------------------------------------------------------------------------------
// device.restart / device.set_name

static void restart_now(void)
{
	nvs_flush_current_layer();
	vTaskDelay(pdMS_TO_TICKS(300)); // let the response leave over BLE/USB
	esp_restart();
}

static cJSON *m_device_restart(cJSON *params, config_error_t *err)
{
	defer(restart_now);
	return cJSON_CreateObject();
}

static cJSON *m_device_set_name(cJSON *params, config_error_t *err)
{
	cJSON *name = cJSON_GetObjectItem(params, "name");
	if (!cJSON_IsString(name) || strlen(name->valuestring) == 0 || strlen(name->valuestring) > 20)
		return fail(err, CONFIG_ERR_BAD_REQUEST, "name: 1 to 20 characters");
	for (const char *c = name->valuestring; *c; c++)
	{
		if (*c < 0x20 || *c > 0x7e)
			return fail(err, CONFIG_ERR_BAD_REQUEST, "name: printable ASCII only");
	}
	nvs_handle_t nvs;
	if (nvs_open("config_c", NVS_READWRITE, &nvs) != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "NVS");
	esp_err_t res = nvs_set_str(nvs, "btname", name->valuestring);
	if (res == ESP_OK)
		res = nvs_commit(nvs);
	nvs_close(nvs);
	if (res != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "could not store the name");
	return cJSON_CreateObject();
}

// ---------------------------------------------------------------------------------------------
// wifi.*

extern SemaphoreHandle_t Wifi_initSemaphore; // main.c
extern bool wifi_reset;						 // wifi_handles.c

static const char *auth_name(wifi_auth_mode_t auth)
{
	switch (auth)
	{
	case WIFI_AUTH_OPEN:
		return "OPEN";
	case WIFI_AUTH_WEP:
		return "WEP";
	case WIFI_AUTH_WPA_PSK:
		return "WPA_PSK";
	case WIFI_AUTH_WPA2_PSK:
		return "WPA2_PSK";
	case WIFI_AUTH_WPA_WPA2_PSK:
		return "WPA_WPA2_PSK";
	case WIFI_AUTH_WPA3_PSK:
		return "WPA3_PSK";
	case WIFI_AUTH_WPA2_WPA3_PSK:
		return "WPA2_WPA3_PSK";
	default:
		return "OTHER";
	}
}

static cJSON *m_wifi_status(cJSON *params, config_error_t *err)
{
	return wifi_status_json();
}

static cJSON *m_wifi_scan(cJSON *params, config_error_t *err)
{
	wifi_mode_t mode;
	if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL)
		return fail(err, CONFIG_ERR_INTERNAL, "WiFi is off");
	// A scan needs the station interface: in AP mode, add it for the duration of the scan
	if (mode == WIFI_MODE_AP)
		esp_wifi_set_mode(WIFI_MODE_APSTA);

	wifi_scan_config_t scan = {.show_hidden = false};
	esp_err_t res = esp_wifi_scan_start(&scan, true);
	uint16_t count = 0;
	wifi_ap_record_t *records = NULL;
	if (res == ESP_OK && esp_wifi_scan_get_ap_num(&count) == ESP_OK && count > 0)
	{
		if (count > 20)
			count = 20;
		records = calloc(count, sizeof(wifi_ap_record_t));
		if (records == NULL || esp_wifi_scan_get_ap_records(&count, records) != ESP_OK)
			count = 0;
	}
	if (mode == WIFI_MODE_AP)
		esp_wifi_set_mode(WIFI_MODE_AP);
	if (res != ESP_OK)
	{
		free(records);
		return fail(err, CONFIG_ERR_INTERNAL, esp_err_to_name(res));
	}

	cJSON *result = cJSON_CreateObject();
	cJSON *aps = cJSON_AddArrayToObject(result, "aps");
	for (int i = 0; i < count; i++)
	{
		cJSON *ap = cJSON_CreateObject();
		cJSON_AddStringToObject(ap, "ssid", (const char *)records[i].ssid);
		cJSON_AddNumberToObject(ap, "rssi", records[i].rssi);
		cJSON_AddNumberToObject(ap, "channel", records[i].primary);
		cJSON_AddStringToObject(ap, "auth", auth_name(records[i].authmode));
		cJSON_AddItemToArray(aps, ap);
	}
	free(records);
	return result;
}

// Same as the web page's /api/connect: the WiFi task restarts the device to join the new network
static void apply_wifi_credentials(void)
{
	vTaskDelay(pdMS_TO_TICKS(300)); // let the response leave first
	nvs_flush_current_layer();
	wifi_reset = true;
	if (Wifi_initSemaphore != NULL)
		xSemaphoreGive(Wifi_initSemaphore);
}

static cJSON *m_wifi_set(cJSON *params, config_error_t *err)
{
	cJSON *ssid = cJSON_GetObjectItem(params, "ssid");
	cJSON *pass = cJSON_GetObjectItem(params, "pass");
	if (!cJSON_IsString(ssid) || strlen(ssid->valuestring) == 0 || strlen(ssid->valuestring) > 32)
		return fail(err, CONFIG_ERR_BAD_REQUEST, "ssid: 1 to 32 characters");
	if (!cJSON_IsString(pass) || strlen(pass->valuestring) > 64)
		return fail(err, CONFIG_ERR_BAD_REQUEST, "pass: up to 64 characters");

	nvs_handle_t nvs;
	if (nvs_open("wifiCreds", NVS_READWRITE, &nvs) != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "NVS");
	esp_err_t res = nvs_set_str(nvs, "ssid", ssid->valuestring);
	if (res == ESP_OK)
		res = nvs_set_str(nvs, "pass", pass->valuestring);
	if (res == ESP_OK)
		res = nvs_commit(nvs);
	nvs_close(nvs);
	if (res != ESP_OK)
		return fail(err, CONFIG_ERR_INTERNAL, "could not store the credentials");

	ESP_LOGI(TAG, "WiFi credentials stored for \"%s\"", ssid->valuestring); // never log the password
	defer(apply_wifi_credentials);
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
	{"macros.list", m_macros_list},
	{"macros.create", m_macros_create},
	{"macros.update", m_macros_update},
	{"macros.delete", m_macros_delete},
	{"macros.restore", m_macros_restore},
	{"led.get", m_led_get},
	{"led.set", m_led_set},
	{"device.restart", m_device_restart},
	{"device.set_name", m_device_set_name},
	{"wifi.status", m_wifi_status},
	{"wifi.scan", m_wifi_scan},
	{"wifi.set", m_wifi_set},
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
