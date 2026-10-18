/**
 * @file wifi_handles.c
 * @brief WiFi station: join the stored network, retry while it is away. See wifi_handles.h.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs.h"

#include "wifi_handles.h"
#include "config_task.h"

static const char *TAG = "wifi";

#define CREDS_NAMESPACE "wifiCreds"
#define FAST_RETRIES 5			   // reconnect at once this many times...
#define SLOW_RETRY_US (30 * 1000000) // ...then every 30 s

static bool s_have_creds = false;
static bool s_leaving = false; // we disconnect on purpose: do not retry
static int s_retries = 0;
static esp_timer_handle_t s_retry_timer;

static void connect_now(void *arg)
{
	if (!s_have_creds)
		return;
	esp_err_t err = esp_wifi_connect();
	// no disconnect event follows a refused attempt (e.g. WiFi not started yet): try again later
	if (err != ESP_OK && err != ESP_ERR_WIFI_CONN)
	{
		esp_timer_stop(s_retry_timer);
		esp_timer_start_once(s_retry_timer, SLOW_RETRY_US);
	}
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
	{
		s_retries = 0;
		connect_now(NULL);
	}
	else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
	{
		wifi_event_sta_disconnected_t *event = data;
		if (s_leaving || !s_have_creds)
		{
			s_leaving = false;
			return;
		}
		if (s_retries < FAST_RETRIES)
		{
			s_retries++;
			ESP_LOGI(TAG, "disconnected (reason %d), retry %d", event->reason, s_retries);
			connect_now(NULL);
		}
		else
		{
			if (s_retries == FAST_RETRIES)
				ESP_LOGW(TAG, "network not reachable, retrying every 30 s");
			s_retries = FAST_RETRIES + 1;
			esp_timer_stop(s_retry_timer);
			esp_timer_start_once(s_retry_timer, SLOW_RETRY_US);
		}
	}
	else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
	{
		ip_event_got_ip_t *event = data;
		ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&event->ip_info.ip));
		s_retries = 0;
	}
}

// Read the stored credentials into the station config; false when there are none
static bool load_credentials(wifi_config_t *cfg)
{
	nvs_handle_t nvs;
	if (nvs_open(CREDS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
		return false;
	size_t ssid_len = sizeof(cfg->sta.ssid);
	size_t pass_len = sizeof(cfg->sta.password);
	bool ok = nvs_get_str(nvs, "ssid", (char *)cfg->sta.ssid, &ssid_len) == ESP_OK && ssid_len > 1 &&
			  nvs_get_str(nvs, "pass", (char *)cfg->sta.password, &pass_len) == ESP_OK;
	nvs_close(nvs);
	return ok;
}

static void station_config(wifi_config_t *cfg)
{
	cfg->sta.threshold.authmode = WIFI_AUTH_OPEN;
	cfg->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
}

void wifi_start(void)
{
	ESP_ERROR_CHECK(esp_netif_init());
	esp_err_t res = esp_event_loop_create_default();
	if (res != ESP_OK && res != ESP_ERR_INVALID_STATE)
		ESP_ERROR_CHECK(res);
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&init));
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL));
	config_task_register_wifi_events(); // "wifi.changed" events and the OLED icon

	const esp_timer_create_args_t timer = {.callback = connect_now, .name = "wifi_retry"};
	ESP_ERROR_CHECK(esp_timer_create(&timer, &s_retry_timer));

	wifi_config_t cfg = {0};
	s_have_creds = load_credentials(&cfg);
	station_config(&cfg);
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
	ESP_ERROR_CHECK(esp_wifi_start());
	if (s_have_creds)
		ESP_LOGI(TAG, "joining \"%s\"", (char *)cfg.sta.ssid); // never log the password
	else
		ESP_LOGI(TAG, "no network stored: set one from the Command Center");
}

esp_err_t wifi_set_credentials(const char *ssid, const char *pass)
{
	nvs_handle_t nvs;
	esp_err_t res = nvs_open(CREDS_NAMESPACE, NVS_READWRITE, &nvs);
	if (res != ESP_OK)
		return res;
	res = nvs_set_str(nvs, "ssid", ssid);
	if (res == ESP_OK)
		res = nvs_set_str(nvs, "pass", pass);
	if (res == ESP_OK)
		res = nvs_commit(nvs);
	nvs_close(nvs);
	if (res != ESP_OK)
		return res;

	wifi_config_t cfg = {0};
	strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
	strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
	station_config(&cfg);

	esp_timer_stop(s_retry_timer);
	s_leaving = true;
	if (esp_wifi_disconnect() != ESP_OK)
		s_leaving = false; // was not connected: no disconnect event will come
	res = esp_wifi_set_config(WIFI_IF_STA, &cfg);
	s_have_creds = true;
	s_retries = 0;
	esp_wifi_connect();
	ESP_LOGI(TAG, "joining \"%s\"", ssid);
	return res;
}

esp_err_t wifi_forget(void)
{
	nvs_handle_t nvs;
	esp_err_t res = nvs_open(CREDS_NAMESPACE, NVS_READWRITE, &nvs);
	if (res != ESP_OK)
		return res;
	nvs_erase_all(nvs);
	res = nvs_commit(nvs);
	nvs_close(nvs);

	s_have_creds = false;
	esp_timer_stop(s_retry_timer);
	esp_wifi_disconnect();
	wifi_config_t cfg = {0};
	station_config(&cfg);
	esp_wifi_set_config(WIFI_IF_STA, &cfg);
	ESP_LOGI(TAG, "network forgotten");
	return res;
}

bool wifi_has_credentials(void)
{
	return s_have_creds;
}
