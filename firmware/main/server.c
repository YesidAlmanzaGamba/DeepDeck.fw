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
	// stores the credentials, then the device restarts to join the network (deferred until the reply is sent)
	return http_call_with_body(req, "wifi.set");
}

esp_err_t config_url_handler(httpd_req_t *req)
{
	// device.info, plus the field names the web page reads (FWVersion, Mac, ssid)
	http_cors(req);
	config_error_t err;
	cJSON *info = config_api_call("device.info", NULL, &err);
	if (info == NULL)
		return http_call(req, "device.info", NULL);
	cJSON_AddStringToObject(info, "FWVersion", FIRMWARE_VERSION);
	cJSON_AddStringToObject(info, "Mac", cJSON_GetObjectItem(info, "mac")->valuestring);
	cJSON *ssid = cJSON_GetObjectItem(cJSON_GetObjectItem(info, "wifi"), "ssid");
	cJSON_AddStringToObject(info, "ssid", cJSON_IsString(ssid) ? ssid->valuestring : "");
	char *text = cJSON_PrintUnformatted(info);
	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, text != NULL ? text : "{}");
	free(text);
	cJSON_Delete(info);
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
	return http_call(req, "macros.list", NULL);
}

/**
 * @brief Create new macro
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t create_macro_url_handler(httpd_req_t *req)
{
	return http_call_with_body(req, "macros.create");
}

/**
 * @brief Delete a macro
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t delete_macro_url_handler(httpd_req_t *req)
{
	// DELETE /api/macros?keycode=512. It used to answer 200 without deleting anything.
	cJSON *params = http_query_params(req, "keycode");
	cJSON *keycode = cJSON_GetObjectItem(params, "keycode");
	if (cJSON_IsString(keycode))
		cJSON_ReplaceItemInObject(params, "keycode", cJSON_CreateNumber(atoi(keycode->valuestring)));
	return http_call(req, "macros.delete", params);
}

/**
 * @brief Update a macro url handler object
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t update_macro_url_handler(httpd_req_t *req)
{
	return http_call_with_body(req, "macros.update");
}

/**
 * @brief Restore default macros
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t restore_default_macro_url_handler(httpd_req_t *req)
{
	return http_call(req, "macros.restore", NULL);
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
	if (req->method == HTTP_OPTIONS)
		return options_handler(req); // the route is also registered for the CORS preflight
	return http_call_with_body(req, "led.set");
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
