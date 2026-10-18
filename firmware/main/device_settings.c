/**
 * @file device_settings.c
 * @brief Screen brightness, screen off when idle, LEDs with it. See device_settings.h.
 */

#include "device_settings.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "keyboard_config.h"
#include "rgb_led.h"
#ifdef OLED_ENABLE
#include "oled_tasks.h"
#endif

#define TAG "settings"
#define NAMESPACE "settings"

static device_settings_t current = {.screen_brightness = 255, .screen_off_minutes = 0, .leds_off_with_screen = false};
static volatile int64_t last_activity_us = 0;
static volatile bool apply_pending = true; // brightness to send to the display
static bool screen_off = false;

device_settings_t device_settings_get(void)
{
	return current;
}

void device_settings_init(void)
{
	nvs_handle_t nvs;
	if (nvs_open(NAMESPACE, NVS_READONLY, &nvs) == ESP_OK)
	{
		uint8_t u8;
		uint16_t u16;
		if (nvs_get_u8(nvs, "scr_bri", &u8) == ESP_OK)
			current.screen_brightness = u8;
		if (nvs_get_u16(nvs, "scr_off", &u16) == ESP_OK)
			current.screen_off_minutes = u16;
		if (nvs_get_u8(nvs, "led_idle", &u8) == ESP_OK)
			current.leds_off_with_screen = u8 != 0;
		nvs_close(nvs);
	}
	last_activity_us = esp_timer_get_time();
	apply_pending = true;
}

esp_err_t device_settings_set(const device_settings_t *settings)
{
	nvs_handle_t nvs;
	esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &nvs);
	if (err != ESP_OK)
		return err;
	err = nvs_set_u8(nvs, "scr_bri", settings->screen_brightness);
	if (err == ESP_OK)
		err = nvs_set_u16(nvs, "scr_off", settings->screen_off_minutes);
	if (err == ESP_OK)
		err = nvs_set_u8(nvs, "led_idle", settings->leds_off_with_screen);
	if (err == ESP_OK)
		err = nvs_commit(nvs);
	nvs_close(nvs);
	if (err == ESP_OK)
	{
		current = *settings;
		apply_pending = true;
		user_activity(); // show the result: the screen comes on with the new brightness
	}
	return err;
}

void user_activity(void)
{
	last_activity_us = esp_timer_get_time();
}

void device_settings_idle_check(void)
{
	bool idle = current.screen_off_minutes > 0 &&
				esp_timer_get_time() - last_activity_us > (int64_t)current.screen_off_minutes * 60 * 1000000;
#ifdef OLED_ENABLE
	if (apply_pending)
	{
		apply_pending = false;
		u8g2_SetContrast(&u8g2, current.screen_brightness);
	}
	if (idle != screen_off)
		u8g2_SetPowerSave(&u8g2, idle);
#endif
	if (idle != screen_off)
	{
		screen_off = idle;
		rgb_led_set_idle(idle && current.leds_off_with_screen);
		ESP_LOGI(TAG, "screen %s", idle ? "off (idle)" : "on");
	}
	else if (!idle)
	{
		rgb_led_set_idle(false); // the LED option may have been switched off while idle
	}
}
