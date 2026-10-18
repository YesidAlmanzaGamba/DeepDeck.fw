/**
 * @file device_settings.h
 * @brief Screen and idle settings (device.settings.* in docs/protocol.md), NVS namespace "settings".
 */

#ifndef DEVICE_SETTINGS_H_
#define DEVICE_SETTINGS_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct
{
	uint8_t screen_brightness;	   // OLED contrast, 0-255
	uint16_t screen_off_minutes;   // turn the screen off after this many idle minutes; 0 = never
	bool leds_off_with_screen;	   // the LEDs go dark with the screen
} device_settings_t;

/** @brief The current settings (loaded at boot, defaults if none stored). */
device_settings_t device_settings_get(void);

/** @brief Store and apply. */
esp_err_t device_settings_set(const device_settings_t *settings);

/** @brief Load from NVS. Call once at boot, before the OLED task starts. */
void device_settings_init(void);

/** @brief A key, knob or layer change: wakes the screen and restarts the idle timer. */
void user_activity(void);

/**
 * @brief Run from the OLED task (it owns the display): applies the brightness and turns the
 * screen and LEDs off or on as the idle time requires.
 */
void device_settings_idle_check(void);

/** @brief Seconds since the last key, knob or layer change; whether the screen is off for it. */
unsigned device_settings_idle_seconds(void);
bool device_settings_screen_off(void);

#endif
