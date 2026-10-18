/**
 * @file config_task.c
 * @brief Runs Command Center requests from BLE and USB one at a time, and sends device events.
 *
 * Lines arrive from the BLE stack callback and the UART reader. Both only copy them into a queue,
 * because a request may write flash or scan WiFi for seconds, which must not block the BLE stack.
 */

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "keyboard_config.h"
#include "nvs_keymaps.h"
#include "nvs_funcs.h"
#include "config_service.h"
#include "serial_transport.h"
#include "config_api.h"
#include "config_task.h"
#include "oled_tasks.h"

#define TAG "config_task"
#define CONFIG_QUEUE_LEN 8
#define CONFIG_TASK_STACK 6144
#define CONFIG_TASK_PRIORITY 3

typedef enum
{
	VIA_BLE,
	VIA_USB,
	VIA_ALL, // events
} via_t;

typedef struct
{
	via_t via;
	bool is_event;
	char *line;
} config_msg_t;

static QueueHandle_t config_q = NULL;

static void enqueue(via_t via, bool is_event, const char *line, size_t len)
{
	if (config_q == NULL)
		return;
	config_msg_t msg = {.via = via, .is_event = is_event, .line = malloc(len + 1)};
	if (msg.line == NULL)
		return;
	memcpy(msg.line, line, len);
	msg.line[len] = '\0';
	if (xQueueSend(config_q, &msg, 0) != pdTRUE)
	{
		ESP_LOGW(TAG, "queue full, message dropped");
		free(msg.line);
	}
}

static void on_ble_line(const char *line, size_t len) { enqueue(VIA_BLE, false, line, len); }
static void on_usb_line(const char *line, size_t len) { enqueue(VIA_USB, false, line, len); }

static void send(via_t via, const char *line)
{
	size_t len = strlen(line);
	if (via == VIA_BLE || via == VIA_ALL)
		config_service_send_line(line, len); // fails quietly when no app is connected
	if (via == VIA_USB || via == VIA_ALL)
		serial_transport_send_line(line, len);
}

static void config_task(void *arg)
{
	config_msg_t msg;
	while (1)
	{
		if (xQueueReceive(config_q, &msg, portMAX_DELAY) != pdTRUE)
			continue;

		if (msg.is_event)
		{
			send(msg.via, msg.line);
		}
		else
		{
			char *response = config_api_handle_line(msg.line);
			if (response != NULL)
			{
				send(msg.via, response);
				free(response);
			}
			config_api_run_deferred();
		}
		free(msg.line);
	}
}

void config_task_start(void)
{
	config_api_init();
	config_q = xQueueCreate(CONFIG_QUEUE_LEN, sizeof(config_msg_t));
	xTaskCreate(config_task, "config_task", CONFIG_TASK_STACK, NULL, CONFIG_TASK_PRIORITY, NULL);
	config_service_set_line_handler(on_ble_line);
	serial_transport_start(on_usb_line);
}

void config_task_broadcast_event(const char *event, cJSON *data)
{
	cJSON *msg = cJSON_CreateObject();
	cJSON_AddStringToObject(msg, "event", event);
	cJSON_AddItemToObject(msg, "data", data != NULL ? data : cJSON_CreateObject());
	char *line = cJSON_PrintUnformatted(msg);
	cJSON_Delete(msg);
	if (line != NULL)
	{
		enqueue(VIA_ALL, true, line, strlen(line));
		free(line);
	}
}

void config_task_layer_changed(bool hold)
{
	cJSON *data = cJSON_CreateObject();
	layouts_lock();
	cJSON_AddNumberToObject(data, "index", current_layout);
	if (current_layout < layers_num)
	{
		cJSON_AddStringToObject(data, "uuid", key_layouts[current_layout].uuid_str);
		cJSON_AddStringToObject(data, "name", key_layouts[current_layout].name);
	}
	layouts_unlock();
	cJSON_AddBoolToObject(data, "hold", hold);
	config_task_broadcast_event("layer.changed", data);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
	// The station retries several times: report the loss of a connection once, not every retry
	static bool connected = false;
	bool got_ip = (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP);
	if (!got_ip && !connected)
		return;
	connected = got_ip;
#ifdef OLED_ENABLE
	oled_set_wifi_connected(got_ip);
#endif

	cJSON *data = cJSON_CreateObject();
	if (got_ip)
	{
		char ip[16];
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
		cJSON_AddStringToObject(data, "state", "connected");
		cJSON_AddStringToObject(data, "ip", ip);
	}
	else
	{
		cJSON_AddStringToObject(data, "state", "connecting");
		cJSON_AddStringToObject(data, "ip", "");
	}
	config_task_broadcast_event("wifi.changed", data);
}

void config_task_register_wifi_events(void)
{
	esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event, NULL);
	esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event, NULL);
}
