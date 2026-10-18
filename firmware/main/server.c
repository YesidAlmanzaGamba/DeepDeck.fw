#include <string.h>
#include <stdlib.h>
#include "nvs.h"
#include "server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "keymap.h"
#include "nvs_flash.h"
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_http_server.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "cJSON.h"
#include "keyboard_config.h"
#include "nvs_keymaps.h"
#include "key_definitions.h"
#include "nvs_funcs.h"
#include "keypress_handles.h"
#include "config_api.h"

#define CONFIG_HTTP_MAX_BODY 8192
// #include "mdns.h"
#include "esp_vfs.h"

#define DEBUG

#define ROWS 4
#define COLS 4

static const char *REST_TAG = "portal-api";
static const char *TAG = "webserver";
extern xSemaphoreHandle Wifi_initSemaphore;

#define REST_CHECK(a, str, goto_tag, ...)                                              \
	do                                                                                 \
	{                                                                                  \
		if (!(a))                                                                      \
		{                                                                              \
			ESP_LOGE(REST_TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
			goto goto_tag;                                                             \
		}                                                                              \
	} while (0)

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

typedef struct rest_server_context
{
	char base_path[ESP_VFS_PATH_MAX + 1];
	char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

void *json_malloc(size_t size)
{
	return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

void json_free(void *ptr)
{
	heap_caps_free(ptr);
}

void json_response(char *j_response)
{
	cJSON *response_json = cJSON_CreateObject();
	cJSON *item_reason = cJSON_CreateString("reason");
	cJSON_AddItemToObject(response_json, " TO DO ", item_reason);
	cJSON *item_message = cJSON_CreateString("message");
	cJSON_AddItemToObject(response_json, "TO DO", item_message);

	j_response = cJSON_Print(response_json);
	if (j_response == NULL)
	{
		abort();
	}

	cJSON_Delete(response_json);
}


/* The /api handlers are thin wrappers around the configuration core (config_api.c): they turn the
 * query or body into params, call the method and send its result as the body, so the web page and
 * the Command Center share the same code. */

static void http_cors(httpd_req_t *req)
{
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

// Read the whole body (httpd_req_recv may return less than content_len) and parse it
static cJSON *http_body_json(httpd_req_t *req)
{
	if (req->content_len == 0 || req->content_len > CONFIG_HTTP_MAX_BODY)
		return NULL;
	char *buf = malloc(req->content_len + 1);
	if (buf == NULL)
		return NULL;
	size_t received = 0;
	while (received < req->content_len)
	{
		int n = httpd_req_recv(req, buf + received, req->content_len - received);
		if (n == HTTPD_SOCK_ERR_TIMEOUT)
			continue;
		if (n <= 0)
		{
			free(buf);
			return NULL;
		}
		received += n;
	}
	buf[received] = '\0';
	cJSON *json = cJSON_Parse(buf);
	free(buf);
	return json;
}

// {"<key>": "<value of ?key= in the URL>"}, or an empty object when it is missing
static cJSON *http_query_params(httpd_req_t *req, const char *key)
{
	cJSON *params = cJSON_CreateObject();
	size_t len = httpd_req_get_url_query_len(req) + 1;
	if (len > 1)
	{
		char *query = malloc(len);
		char value[64];
		if (query != NULL && httpd_req_get_url_query_str(req, query, len) == ESP_OK &&
			httpd_query_key_value(query, key, value, sizeof(value)) == ESP_OK)
			cJSON_AddStringToObject(params, key, value);
		free(query);
	}
	return params;
}

// Run a method and send its result, or {"error":{...}} with a matching HTTP status. Takes params.
static esp_err_t http_call(httpd_req_t *req, const char *method, cJSON *params)
{
	http_cors(req);
	config_error_t err;
	cJSON *result = config_api_call(method, params, &err);
	cJSON *body = result;
	if (result == NULL)
	{
		httpd_resp_set_status(req, config_err_http_status(err.code));
		body = cJSON_CreateObject();
		cJSON *error = cJSON_AddObjectToObject(body, "error");
		cJSON_AddStringToObject(error, "code", config_err_name(err.code));
		cJSON_AddStringToObject(error, "message", err.message);
	}
	char *text = cJSON_PrintUnformatted(body);
	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, text != NULL ? text : "{}");
	free(text);
	cJSON_Delete(body);
	cJSON_Delete(params);
	config_api_run_deferred(); // e.g. a restart, now that the response is out
	return ESP_OK;
}

static esp_err_t http_call_with_body(httpd_req_t *req, const char *method)
{
	cJSON *params = http_body_json(req);
	if (params == NULL)
	{
		http_cors(req);
		httpd_resp_set_status(req, HTTPD_400);
		httpd_resp_sendstr(req, "{\"error\":{\"code\":\"bad_request\",\"message\":\"body is not JSON\"}}");
		return ESP_OK;
	}
	return http_call(req, method, params);
}

// static void json_error_generator(char *string, char *error_str)
// {
// 	sprintf(string, "{\"error\":\"%s\"}", error_str);
// }

/* An HTTP GET handler */

/**
 * @brief Handler for Wifi Connection
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t connect_url_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "connect handler");
	char *buffer;
	char *string = NULL;
	json_response(string);
	size_t buf_len;
	buf_len = (req->content_len) + 1;
	buffer = malloc(buf_len);
	httpd_req_recv(req, buffer, req->content_len);

	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"));
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	cJSON *payload = cJSON_Parse(buffer);

	if (NULL == payload)
	{
		const char *err = cJSON_GetErrorPtr();
		if (err != NULL)
		{
			ESP_LOGE(TAG, "Error parsing json before %s", err);
			cJSON_Delete(payload);

			httpd_resp_set_status(req, "500");
			return -1;
		}
	}

	cJSON *ssid = cJSON_GetObjectItem(payload, "ssid");
	cJSON *pass = cJSON_GetObjectItem(payload, "pass");

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);

	if (cJSON_IsString(ssid) && (ssid->valuestring != NULL))
	{
		printf("WIFI SSID \"%s\"\n", ssid->valuestring);

		if (cJSON_IsString(pass) && (pass->valuestring != NULL))
		{
			printf("WIFI PASS \"%s\"\n", pass->valuestring);

			nvs_flash_init();
			nvs_handle_t nvs;
			nvs_open("wifiCreds", NVS_READWRITE, &nvs);
			nvs_set_str(nvs, "ssid", ssid->valuestring);
			nvs_set_str(nvs, "pass", pass->valuestring);
			nvs_close(nvs);
			ESP_LOGI("nvs", "Wifi Credentials Saved");
			httpd_resp_set_status(req, HTTPD_200);
			httpd_resp_send(req, NULL, 0);
		}
	}

	else
	{
		httpd_resp_set_status(req, HTTPD_400);
		httpd_resp_send(req, NULL, 0);
		free(buffer);
		return ESP_OK;
	}

	free(buffer);
	free(string);

	wifi_reset = true;
	xSemaphoreGive(Wifi_initSemaphore);
	return ESP_OK;
}

esp_err_t config_url_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "HTTP GET  --> /api/config");

	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*")); //
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

	uint8_t layers_num = nvs_read_num_layers();
	char *string = NULL;
	cJSON *monitor = cJSON_CreateObject();
	if (monitor == NULL)
	{
		httpd_resp_set_status(req, HTTPD_400);
		httpd_resp_send(req, NULL, 0);
		return ESP_OK;
	}

	cJSON_AddItemToObject(monitor, "ssid", cJSON_CreateString("ssid"));
	cJSON_AddItemToObject(monitor, "FWVersion", cJSON_CreateString(FIRMWARE_VERSION));
	cJSON_AddItemToObject(monitor, "Mac", cJSON_CreateString("MAC"));

	string = cJSON_Print(monitor);
	if (string == NULL)
	{
		fprintf(stderr, "Failed to print monitor.\n");
	}

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);
	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_send(req, NULL, 0);

	cJSON_Delete(monitor);
	free(string);

	return ESP_OK;
}

/**
 *
 *
 * END POINTS MACROS
 *
 *
 *
 */

/**
 * @brief Read DeepDeck macros
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t get_macros_url_handler(httpd_req_t *req)
{
	ESP_LOGW("", "Free memory: %d bytes", esp_get_free_heap_size());

	ESP_LOGI(TAG, "HTTP GET MACROS INFO --> /api/macros");
	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"));
	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type"));

	int index = 0;
	cJSON *macro_data = NULL;
	cJSON *macro_name = NULL;
	cJSON *macro_keycode = NULL;
	cJSON *macro_key = NULL;

	cJSON *macro_object = cJSON_CreateObject();
	if (macro_object == NULL)
	{
		// abort();

		const char *err = cJSON_GetErrorPtr();
		if (err != NULL)
		{
			ESP_LOGE(TAG, "Error parsing json before %s", err);
			cJSON_Delete(macro_object);
			httpd_resp_set_status(req, "500");
			httpd_resp_send(req, NULL, 0);
			return -1;
		}
	}

	cJSON *array = cJSON_CreateArray();
	if (array == NULL)
	{
		// abort();

		const char *err_ = cJSON_GetErrorPtr();
		if (err_ != NULL)
		{
			ESP_LOGE(TAG, "Error parsing json before %s", err_);
			cJSON_Delete(macro_object);
			httpd_resp_set_status(req, "500");
			httpd_resp_send(req, NULL, 0);
			return -1;
		}
	}

	cJSON_AddItemToObject(macro_object, "macros", array);

	for (index = 0; index < total_macros; ++index)
	{
		macro_data = cJSON_CreateObject();
		if (macro_data == NULL)
			abort();
		cJSON_AddItemToArray(array, macro_data);

		macro_name = cJSON_CreateString((user_macros[index].name));
		if (macro_name == NULL)
			abort();
		cJSON_AddItemToObject(macro_data, "name", macro_name);

		macro_keycode = cJSON_CreateNumber(user_macros[index].keycode);
		if (macro_keycode == NULL)
			abort();
		cJSON_AddItemToObject(macro_data, "keycode", macro_keycode);

		macro_key = cJSON_CreateArray();
		if (macro_key == NULL)
			abort();
		cJSON_AddItemToObject(macro_data, "key", macro_key);
		for (int i = 0; i < MACRO_LEN; i++)
		{
			cJSON_AddItemToArray(macro_key, cJSON_CreateNumber(user_macros[index].key[i]));
		}
	}

	char *string = NULL;
	// string = malloc(strlen(cJSON_Print(macro_object)) + 1);
	string = cJSON_Print(macro_object);
	if (string == NULL)
		abort();

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);
	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_send(req, NULL, 0);

	free(string);
	cJSON_Delete(macro_object);

	return ESP_OK;
}

/**
 * @brief Create new macro
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t create_macro_url_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "HTTP POST CREATE MACRO --> /api/macros");
	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"));
	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type"));

	char *buf;
	size_t buf_len;
	char *string = NULL;
	json_response(string);

	buf_len = (req->content_len) + 1;
	buf = malloc(buf_len);
	httpd_req_recv(req, buf, req->content_len);

	dd_macros new_macro;
	cJSON *payload = cJSON_Parse(buf);

	if (NULL == payload)
	{
		const char *err = cJSON_GetErrorPtr();
		if (err != NULL)
		{
			ESP_LOGE(TAG, "Error parsing json before %s", err);
			cJSON_Delete(payload);
			free(buf);
			free(buf);
			httpd_resp_set_status(req, "500");
			return -1;
		}
	}

	cJSON *name = cJSON_GetObjectItem(payload, "name");
	if (cJSON_IsString(name) && (name->valuestring != NULL))
	{
		if (strlen(name->valuestring) < 6)
			strcpy(new_macro.name, name->valuestring);
		else
			strcpy(new_macro.name, "  ");
	}
	cJSON *keycode = cJSON_GetObjectItem(payload, "keycode");
	if (cJSON_IsNumber(keycode))
	{
		new_macro.keycode = keycode->valueint;
	}
	cJSON *key = cJSON_GetObjectItem(payload, "key");
	if (cJSON_IsArray(key))
	{
		for (int i = 0; i < MACRO_LEN; i++)
		{
			cJSON *item = cJSON_GetArrayItem(key, i);
			if (cJSON_IsNumber(item))
			{
				if (item->valueint >= 0 || item->valueint <= 1000)
					new_macro.key[i] = item->valueint;
				else
					new_macro.key[i] = 0;
			}
		}
	}

	ESP_LOGI(TAG, "new_macro.name: %s, new_macro.keycode: %d, new_macro.key:{%d,%d,%d,%d,%d} ", new_macro.name, new_macro.keycode, new_macro.key[0], new_macro.key[1], new_macro.key[2], new_macro.key[3], new_macro.key[4]);

	esp_err_t error;
	error = nvs_create_new_macro(new_macro);
	if (error != ESP_OK)
	{
		ESP_LOGE(TAG, "nvs_create_new_macro error %s", esp_err_to_name(error));
		httpd_resp_set_status(req, HTTPD_400);
	}
	else
	{

		httpd_resp_set_status(req, HTTPD_200);
	}
	cJSON_Delete(payload);
	free(buf);
	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);
	httpd_resp_send(req, NULL, 0);
	return ESP_OK;
}

/**
 * @brief Delete a macro
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t delete_macro_url_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "HTTP DELETE MACRO --> /api/macros");
	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"));
	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type"));
	char *string = NULL;
	json_response(string);

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);
	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_send(req, NULL, 0);

	free(string);

	return ESP_OK;
}

/**
 * @brief Update a macro url handler object
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t update_macro_url_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "HTTP UPDATE MACRO --> /api/macros");
	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"));
	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type"));

	char *buf;
	size_t buf_len;
	char *string = NULL;
	json_response(string);

	buf_len = (req->content_len) + 1;
	buf = malloc(buf_len);
	httpd_req_recv(req, buf, req->content_len);

	dd_macros new_macro;
	cJSON *payload = cJSON_Parse(buf);

	if (NULL == payload)
	{
		const char *err = cJSON_GetErrorPtr();
		if (err != NULL)
		{
			ESP_LOGE(TAG, "Error parsing json before %s", err);
			cJSON_Delete(payload);
			free(buf);
			httpd_resp_set_status(req, "500");
			return -1;
		}
	}

	cJSON *name = cJSON_GetObjectItem(payload, "name");
	if (cJSON_IsString(name) && (name->valuestring != NULL))
	{
		if (strlen(name->valuestring) < 7)
			strcpy(new_macro.name, name->valuestring);
		else
			strcpy(new_macro.name, "**");
	}
	cJSON *keycode = cJSON_GetObjectItem(payload, "keycode");
	if (cJSON_IsNumber(keycode))
	{
		new_macro.keycode = keycode->valueint;
	}
	cJSON *key = cJSON_GetObjectItem(payload, "key");
	if (cJSON_IsArray(key))
	{
		for (int i = 0; i < MACRO_LEN; i++)
		{
			cJSON *item = cJSON_GetArrayItem(key, i);
			if (cJSON_IsNumber(item))
			{
				if (item->valueint >= 0 || item->valueint <= 1000)
					new_macro.key[i] = item->valueint;
				else
					new_macro.key[i] = 0;
			}
		}
	}

	ESP_LOGI(TAG, "new_macro.name: %s, new_macro.keycode: %d, new_macro.key:{%d,%d,%d,%d,%d} ", new_macro.name, new_macro.keycode, new_macro.key[0], new_macro.key[1], new_macro.key[2], new_macro.key[3], new_macro.key[4]);
	esp_err_t error;
	error = nvs_update_macro(new_macro);
	if (error != ESP_OK)
	{
		ESP_LOGE(TAG, "nvs_update_macro error %s", esp_err_to_name(error));
		httpd_resp_set_status(req, HTTPD_400);
	}
	else
	{

		httpd_resp_set_status(req, HTTPD_200);
	}
	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);
	httpd_resp_send(req, NULL, 0);
	cJSON_Delete(payload);
	free(buf);

	return ESP_OK;
}

/**
 * @brief Restore default macros
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t restore_default_macro_url_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "HTTP RESTORE DEFAULT MACROS --> /api/macros/restore");
	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*")); //
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

	char *string = NULL;
	json_response(string);
	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);
	esp_err_t error = nvs_restore_default_macros();
	if (error == ESP_OK)
	{
		httpd_resp_set_status(req, HTTPD_200);
	}
	else
	{
		httpd_resp_set_status(req, HTTPD_400);
	}
	httpd_resp_send(req, NULL, 0);

	return ESP_OK;
}

/**
 * ENPOINTS LAYERS
 *
 */

/**
 * @brief Get the layer url handler object
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t get_layer_url_handler(httpd_req_t *req)
{
	return http_call(req, "layers.get", http_query_params(req, "uuid"));
}

/**
 * @brief Get the layerName url handler object
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t get_layerName_url_handler(httpd_req_t *req)
{
	return http_call(req, "layers.list", NULL);
}

/**
 * @brief Delete layer
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t delete_layer_url_handler(httpd_req_t *req)
{
	return http_call(req, "layers.delete", http_query_params(req, "uuid"));
}

esp_err_t options_handler(httpd_req_t *req)
{
	// Set CORS headers
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, GET, PUT, DELETE, OPTIONS");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

	// Respond with a 204 status (No Content)
	httpd_resp_set_status(req, "204");
	httpd_resp_send(req, NULL, 0);

	return ESP_OK;
}

esp_err_t options_restore_handler(httpd_req_t *req)
{
	// Set CORS headers
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "PUT");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

	// Respond with a 204 status (No Content)
	httpd_resp_set_status(req, "204");
	httpd_resp_send(req, NULL, 0);

	return ESP_OK;
}


/**
 * @brief Update layer
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t update_layer_url_handler(httpd_req_t *req)
{
	return http_call_with_body(req, "layers.update");
}

/**
 * @brief Create a new layer
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t create_layer_url_handler(httpd_req_t *req)
{
	return http_call_with_body(req, "layers.create");
}

/**
 * @brief Restore Default Layouts
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t restore_default_layer_url_handler(httpd_req_t *req)
{
	return http_call(req, "layers.restore", NULL);
}

/**
 * @brief Change Keyboard LED Color
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t change_keyboard_led_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "HTTP POST  CHANGE LED MODE --> /api/led");

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	rgb_mode_t led_mode = {0};

	// Read the URI line and get the host
	char *buf;
	size_t buf_len;
	char *string = NULL;

	buf_len = (req->content_len) + 1;
	buf = malloc(buf_len);
	httpd_req_recv(req, buf, req->content_len);

	cJSON *payload = cJSON_Parse(buf);
#ifdef DEBUG
	string = cJSON_Print(payload);
	if (string == NULL)
		fprintf(stderr, "Failed to print monitor.\n");

	ESP_LOGE("+", "%s", string);
#endif

	cJSON *mode = cJSON_GetObjectItem(payload, "mode");
	if (cJSON_IsNumber(mode))
	{
		led_mode.mode = mode->valueint;
	}

	cJSON *hue = cJSON_GetObjectItem(payload, "H");
	if (cJSON_IsNumber(hue))
	{
		led_mode.H = hue->valueint;
	}
	cJSON *saturation = cJSON_GetObjectItem(payload, "S");
	if (cJSON_IsNumber(hue))
	{
		led_mode.S = saturation->valueint;
	}
	cJSON *value = cJSON_GetObjectItem(payload, "V");
	if (cJSON_IsNumber(value))
	{
		led_mode.V = value->valueint;
	}

	cJSON *speed = cJSON_GetObjectItem(payload, "speed");
	if (cJSON_IsNumber(speed))
	{
		led_mode.speed = speed->valueint;
	}

	if ((led_mode.mode == 4) || (led_mode.mode == 5))
	{
		cJSON *rgb_color = cJSON_GetObjectItem(payload, "rgb");
		if (cJSON_IsArray(rgb_color))
		{
			for (int i = 0; i < 3; i++)
			{
				cJSON *item = cJSON_GetArrayItem(rgb_color, i);
				if (cJSON_IsNumber(item))
				{
					led_mode.rgb[i] = item->valueint;
					// ESP_LOGE("+", "led_mode[%d] = %d", (i + 2), led_mode.rgb[i]);
				}
				else
				{
					httpd_resp_set_status(req, HTTPD_400);
					httpd_resp_send(req, NULL, 0);
				}
			}
		}
	}
	else
	{
		nvs_load_rgb_color(&led_mode);
	}

	json_response(string);
	free(buf);
	cJSON_Delete(payload);
	nvs_save_led_mode(led_mode);

	xQueueSend(keyled_q, &led_mode, 0);

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);
	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_send(req, NULL, 0);

	return ESP_OK;
}

/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
	return ESP_OK;
	// if (strcmp("/hello", req->uri) == 0)
	// {
	// 	httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
	// 						"/hello URI is not available");
	// 	/* Return ESP_OK to keep underlying socket open */
	// 	return ESP_OK;
	// }
	// else if (strcmp("/echo", req->uri) == 0)
	// {
	// 	httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
	// 						"/echo URI is not available");
	// 	/* Return ESP_FAIL to close underlying socket */
	// 	return ESP_FAIL;
	// }
	// /* For any other URI send 404 and close socket */
	// httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
	// return ESP_FAIL;
}

/////////////////////////////////////////////////////////////////////////////////////////////
/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
	const char *type = "text/plain";
	if (CHECK_FILE_EXTENSION(filepath, ".html"))
	{
		type = "text/html";
	}
	else if (CHECK_FILE_EXTENSION(filepath, ".js"))
	{
		type = "application/javascript";
	}
	else if (CHECK_FILE_EXTENSION(filepath, ".css"))
	{
		type = "text/css";
	}
	else if (CHECK_FILE_EXTENSION(filepath, ".png"))
	{
		type = "image/png";
	}
	else if (CHECK_FILE_EXTENSION(filepath, ".ico"))
	{
		type = "image/x-icon";
	}
	else if (CHECK_FILE_EXTENSION(filepath, ".svg"))
	{
		type = "text/xml";
	}
	else if (CHECK_FILE_EXTENSION(filepath, ".gzip"))
	{
		type = "application/x-gzip";
	}

	return httpd_resp_set_type(req, type);
}

/* Send HTTP response with the contents of the requested file */
static esp_err_t rest_common_get_handler(httpd_req_t *req)
{

	char filepath[FILE_PATH_MAX];

	rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
	strlcpy(filepath, rest_context->base_path, sizeof(filepath));

	ESP_LOGI(REST_TAG, "URI %s", req->uri);

	if (req->uri[strlen(req->uri) - 1] == '/')
	{
		strlcat(filepath, "/index.html", sizeof(filepath));
	}
	else
	{
		strlcat(filepath, req->uri, sizeof(filepath));
	}

	set_content_type_from_file(req, filepath);

	int fd = open(filepath, O_RDONLY, 0);
	if (fd == -1)
	{
		strlcat(filepath, ".gz", sizeof(filepath));
		fd = open(filepath, O_RDONLY, 0);
		if (fd == -1)
		{
			ESP_LOGE(REST_TAG, "Failed to open file : %s", filepath);
			/* Respond with 500 Internal Server Error */
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
			return ESP_FAIL;
		}
		httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
	}

	char *chunk = rest_context->scratch;
	ssize_t read_bytes;
	do
	{
		/* Read file in chunks into the scratch buffer */
		read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
		if (read_bytes == -1)
		{
			ESP_LOGE(REST_TAG, "Failed to read file : %s", filepath);
		}
		else if (read_bytes > 0)
		{
			/* Send the buffer contents as HTTP response chunk */
			if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK)
			{
				close(fd);
				ESP_LOGE(REST_TAG, "File sending failed!");
				/* Abort sending file */
				httpd_resp_sendstr_chunk(req, NULL);
				/* Respond with 500 Internal Server Error */
				httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
				return ESP_FAIL;
			}
		}
	} while (read_bytes > 0);
	/* Close file after sending complete */
	close(fd);
	ESP_LOGI(REST_TAG, "File sending complete");
	/* Respond with an empty chunk to signal HTTP response completion */
	httpd_resp_send_chunk(req, NULL, 0);
	return ESP_OK;
}

/**
 * @brief
 *
 * @return httpd_handle_t
 */
httpd_handle_t start_webserver(const char *base_path)
{
	REST_CHECK(base_path, "wrong base path", err);
	rest_server_context_t *rest_context = calloc(1, sizeof(rest_server_context_t));
	REST_CHECK(rest_context, "No memory for rest context", err);
	strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

	httpd_handle_t server = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.max_uri_handlers = 20;
	config.stack_size = 1024 * 10;
	config.uri_match_fn = httpd_uri_match_wildcard;

	// Start the httpd server
	// ESP_ERROR_CHECK(httpd_start(&server, &config));
	REST_CHECK(httpd_start(&server, &config) == ESP_OK, "Start server failed", err_start);

	ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

	httpd_uri_t connect_url = {.uri = "/api/connect", .method = HTTP_POST, .handler = connect_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &connect_url);

	httpd_uri_t get_config_url = {.uri = "/api/config", .method = HTTP_GET, .handler = config_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &get_config_url);

	////////LED
	httpd_uri_t change_led_color_url = {.uri = "/api/led", .method = HTTP_POST, .handler = change_keyboard_led_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &change_led_color_url);
	httpd_uri_t change_led_color_url__ = {.uri = "/api/led", .method = HTTP_OPTIONS, .handler = change_keyboard_led_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &change_led_color_url__);

	///////LAYERS

	httpd_uri_t get_layer_url = {.uri = "/api/layers", .method = HTTP_GET, .handler = get_layer_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &get_layer_url);
	httpd_uri_t get_layerName_url = {.uri = "/api/layers/layer_names", .method = HTTP_GET, .handler = get_layerName_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &get_layerName_url);
	httpd_uri_t delete_layer_url = {.uri = "/api/layers", .method = HTTP_DELETE, .handler = delete_layer_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &delete_layer_url);
	httpd_uri_t create_layer_url = {.uri = "/api/layers", .method = HTTP_POST, .handler = create_layer_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &create_layer_url);
	httpd_uri_t layer_options_url = {.uri = "/api/layers", .method = HTTP_OPTIONS, .handler = options_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &layer_options_url);

	httpd_uri_t update_layer_url = {.uri = "/api/layers", .method = HTTP_PUT, .handler = update_layer_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &update_layer_url);

	httpd_uri_t restore_default_layer_url = {.uri = "/api/layers/restore", .method = HTTP_PUT, .handler = restore_default_layer_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &restore_default_layer_url);
	httpd_uri_t layer_restore_options_url = {.uri = "/api/layers/restore", .method = HTTP_OPTIONS, .handler = options_restore_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &layer_restore_options_url);

	// MACROS
	httpd_uri_t get_macros_url = {.uri = "/api/macros", .method = HTTP_GET, .handler = get_macros_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &get_macros_url);
	httpd_uri_t create_macro_url = {.uri = "/api/macros", .method = HTTP_POST, .handler = create_macro_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &create_macro_url);
	httpd_uri_t delete_macro_url = {.uri = "/api/macros", .method = HTTP_DELETE, .handler = delete_macro_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &delete_macro_url);
	httpd_uri_t update_macro_url = {.uri = "/api/macros", .method = HTTP_PUT, .handler = update_macro_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &update_macro_url);
	httpd_uri_t option_macros_url = {.uri = "/api/macros", .method = HTTP_OPTIONS, .handler = options_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &option_macros_url);
	httpd_uri_t restore_all_macro_url = {.uri = "/api/macros/restore", .method = HTTP_POST, .handler = restore_default_macro_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &restore_all_macro_url);

	/* URI handler for getting web server files */
	httpd_uri_t common_get_uri = {
		.uri = "/*",
		//.uri = "/esp-portal/*",
		.method = HTTP_GET,
		.handler = rest_common_get_handler,
		.user_ctx = rest_context};
	httpd_register_uri_handler(server, &common_get_uri);

	return server;
err_start:
	free(rest_context);
err:
	return NULL;
}
/**
 * @brief
 *
 * @param server
 */
void stop_webserver(httpd_handle_t server)
{
	// Stop the httpd server
	httpd_stop(server);
}
