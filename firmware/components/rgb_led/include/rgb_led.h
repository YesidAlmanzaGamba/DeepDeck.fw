// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RGB_LED_H_
#define RGB_LED_H_

#include <stdbool.h>
#include "led_strip.h"
#include "esp_err.h"

#define RMT_TX_CHANNEL_KEYPAD RMT_CHANNEL_0
#define RMT_TX_CHANNEL_NOTIFICATION RMT_CHANNEL_1

#define RGB_LED_REFRESH_SPEED  (20) // ms per frame (50 fps)
#define KEYBOARD_RGB_GPIO       17
#define NOTIFICATION_RGB_GPIO   23
#define RGB_LED_KEYBOARD_NUMBER 16
#define RGB_LED_NOTIFICATION_NUMBER 2

/* Lighting effects. Ids 0-5 keep the meaning they had before lighting v2, so stored settings
 * still work. The names and the parameters each one uses are listed by rgb_led_effect_info(). */
typedef enum
{
    LED_EFFECT_OFF = 0,
    LED_EFFECT_REACTIVE,      // dark, keys light up in color1 when pressed and fade
    LED_EFFECT_COLOR_CYCLE,   // the whole board cycles through the hues
    LED_EFFECT_RAINBOW_WAVE,  // a rainbow moving across the keys
    LED_EFFECT_SOLID,         // color1
    LED_EFFECT_PER_KEY,       // the custom colours of the active layer
    LED_EFFECT_BREATHING,     // color1 fading in and out
    LED_EFFECT_RIPPLE,        // a ring in color2 spreading from the pressed key, over a dim color1
    LED_EFFECT_GRADIENT,      // color1 to color2, moving
    LED_EFFECT_TWINKLE,       // random keys sparkle in color1 / color2
    LED_EFFECT_LAYER_COLOR,   // the whole board in the colour of the active layer
    LED_EFFECT_COUNT
} led_effect_t;

extern QueueHandle_t keyled_q;

/* Lighting settings, stored in NVS namespace "led_mode". `mode` is the effect (led_effect_t);
 * H, S and V are kept for the web page, which still sends them. */
typedef struct rgb_mode_t {
        uint8_t mode;
        uint8_t H;
        uint8_t S;
        uint8_t V;
        uint8_t speed;
        uint8_t rgb[3];     // color1
        uint8_t rgb2[3];    // color2
        uint8_t brightness; // 0-255, applied to every effect
        uint8_t reactive;   // 1: pressed keys flash in color2 on top of any effect
    }rgb_mode_t;

/* Per-layer key colours: 16 keys and one colour for the whole layer (effect "layer colour"). */
typedef struct
{
    uint8_t keys[RGB_LED_KEYBOARD_NUMBER][3];
    uint8_t layer[3];
} rgb_layer_colors_t;

void rgb_notification_led_init(void);
void rgb_key_led_init(void);

/** @brief Report a key press (for the reactive and ripple effects). Call on press only. */
void rgb_key_led_press(uint8_t row, uint8_t col);

void hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b);

/** @brief LED task body: renders the active effect, never returns. */
void key_led_modes(void);

/** @brief Apply lighting settings now (latest wins; does not store them). */
void rgb_led_apply(const rgb_mode_t *mode);

/** @brief The active layer changed: reload its key colours. Safe from any task. */
void rgb_led_layer_changed(void);

/** @brief Show these key colours for `uuid` until the next layer change (live preview). */
void rgb_led_preview_layer_colors(const char *uuid, const rgb_layer_colors_t *colors);

/**
 * @brief Key colours of a layer: the stored ones, or the defaults (keys with a key code in color1,
 * empty keys off, a palette colour for the layer). Reads key_layouts, so take layouts_lock first.
 * @param layer_index position of the layer, used for the default palette colour
 * @param key_map key codes of the layer, used for the default key colours
 * @return true if custom colours are stored for this layer
 */
bool rgb_layer_colors_load(const char *uuid, int layer_index, const uint16_t key_map[4][4],
                           const rgb_mode_t *mode, rgb_layer_colors_t *out);
esp_err_t rgb_layer_colors_save(const char *uuid, const rgb_layer_colors_t *colors);
esp_err_t rgb_layer_colors_erase(const char *uuid);
esp_err_t rgb_layer_colors_erase_all(void);

typedef struct
{
    const char *name;
    const char *uses; // comma separated: color1, color2, speed
} led_effect_info_t;

/** @brief Name and parameters of an effect, NULL past LED_EFFECT_COUNT. */
const led_effect_info_t *rgb_led_effect_info(int effect);

#endif /* RGB_LED_H_ */
