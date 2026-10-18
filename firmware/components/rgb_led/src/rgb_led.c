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

/* Lighting v2: one frame loop renders every effect.
 * Each frame computes the 16 key colours from the effect, the time and the settings, adds the
 * press highlight, applies the brightness and refreshes the strip once (no clear between frames,
 * which made the old rainbow flicker). */

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/rmt.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "led_strip.h"
#include "rgb_led.h"
#include "nvs_flash.h"
#include "nvs_funcs.h"
#include "keymap.h"

static const char *TAG = "RGB_LEDs";

#define LAYER_COLORS_NAMESPACE "led_keys"
#define PRESS_FADE_MS 600.0f   // reactive highlight duration
#define RIPPLE_LIFE_MS 1500.0f // ripple duration
#define MAX_RIPPLES 4

led_strip_t *rgb_key;
led_strip_t *rgb_notif;

/// @brief Latest lighting settings for the LED task (length 1, overwritten)
QueueHandle_t keyled_q;

static const led_effect_info_t effect_info[LED_EFFECT_COUNT] = {
    {"Off", ""},
    {"Reactive", "color1,speed"},
    {"Colour cycle", "speed"},
    {"Rainbow wave", "speed"},
    {"Solid", "color1"},
    {"Per-key", "keys"},
    {"Breathing", "color1,speed"},
    {"Ripple", "color1,color2,speed"},
    {"Gradient", "color1,color2,speed"},
    {"Twinkle", "color1,color2,speed"},
    {"Layer colour", "layer"},
};

// Default layer colours, by layer position
static const uint8_t layer_palette[8][3] = {
    {255, 120, 0}, {0, 140, 255}, {0, 220, 90}, {200, 0, 255},
    {255, 0, 60}, {255, 210, 0}, {0, 230, 220}, {255, 255, 255},
};

// Press events, written by the key task and read by the LED task
static volatile uint32_t press_ms[RGB_LED_KEYBOARD_NUMBER];
static volatile struct
{
    uint8_t key;
    uint32_t ms;
} ripples[MAX_RIPPLES];
static volatile uint8_t ripple_next = 0;

// Key colours shown by the per-key and layer colour effects
static SemaphoreHandle_t colors_lock = NULL;
static rgb_layer_colors_t shown_colors;
static volatile bool reload_colors = true;
static volatile bool idle_off = false; // rgb_led_set_idle(): all LEDs dark while the device is idle

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

const led_effect_info_t *rgb_led_effect_info(int effect)
{
    return (effect >= 0 && effect < LED_EFFECT_COUNT) ? &effect_info[effect] : NULL;
}

/**
 * @brief HSV to RGB conversion (kept for older callers)
 *
 * @param h  hue 0-360
 * @param s  saturation 0-100
 * @param v  value 0-100
 */
void hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360; // h -> [0,360]
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    // RGB adjustment amount by hue
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i)
    {
    case 0:
        *r = rgb_max;
        *g = rgb_min + rgb_adj;
        *b = rgb_min;
        break;
    case 1:
        *r = rgb_max - rgb_adj;
        *g = rgb_max;
        *b = rgb_min;
        break;
    case 2:
        *r = rgb_min;
        *g = rgb_max;
        *b = rgb_min + rgb_adj;
        break;
    case 3:
        *r = rgb_min;
        *g = rgb_max - rgb_adj;
        *b = rgb_max;
        break;
    case 4:
        *r = rgb_min + rgb_adj;
        *g = rgb_min;
        *b = rgb_max;
        break;
    default:
        *r = rgb_max;
        *g = rgb_min;
        *b = rgb_max - rgb_adj;
        break;
    }
}

// Fully saturated colour for a hue in degrees
static void hue_rgb(float hue, uint8_t out[3])
{
    uint32_t r, g, b;
    hsv2rgb((uint32_t)fmodf(hue, 360.0f), 100, 100, &r, &g, &b);
    out[0] = r;
    out[1] = g;
    out[2] = b;
}

static void scale(uint8_t c[3], float k)
{
    for (int i = 0; i < 3; i++)
        c[i] = (uint8_t)(c[i] * k);
}

static void mix(uint8_t c[3], const uint8_t to[3], float k)
{
    if (k < 0)
        k = 0;
    if (k > 1)
        k = 1;
    for (int i = 0; i < 3; i++)
        c[i] = (uint8_t)(c[i] + (to[i] - c[i]) * k);
}

void rgb_notification_led_init(void)
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(NOTIFICATION_RGB_GPIO, RMT_TX_CHANNEL_NOTIFICATION);
    // set counter clock to 40MHz
    config.clk_div = 2;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    // install ws2812 driver
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(RGB_LED_NOTIFICATION_NUMBER, (led_strip_dev_t)config.channel);
    rgb_notif = led_strip_new_rmt_ws2812(&strip_config);
    if (!rgb_notif)
    {
        ESP_LOGE(TAG, "Install notification LEDs failed");
    }
    // Clear LED strip (turn off all LEDs)
    ESP_ERROR_CHECK(rgb_notif->clear(rgb_notif, 100));
}

void rgb_key_led_init(void)
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(KEYBOARD_RGB_GPIO, RMT_TX_CHANNEL_KEYPAD);

    // set counter clock to 40MHz
    config.clk_div = 2;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    // install ws2812 driver
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(RGB_LED_KEYBOARD_NUMBER, (led_strip_dev_t)config.channel);
    rgb_key = led_strip_new_rmt_ws2812(&strip_config);
    if (!rgb_key)
    {
        ESP_LOGE(TAG, "Install key LEDs failed");
    }
    // Clear LED strip (turn off all LEDs)
    ESP_ERROR_CHECK(rgb_key->clear(rgb_key, 100));

    if (colors_lock == NULL)
        colors_lock = xSemaphoreCreateMutex();
    if (keyled_q == NULL)
        keyled_q = xQueueCreate(1, sizeof(rgb_mode_t)); // latest settings win, see rgb_led_apply
}

void rgb_led_apply(const rgb_mode_t *mode)
{
    if (keyled_q != NULL)
        xQueueOverwrite(keyled_q, mode);
}

void rgb_key_led_press(uint8_t row, uint8_t col)
{
    uint8_t key = (row << 2) + col;
    if (key >= RGB_LED_KEYBOARD_NUMBER)
        return;
    uint32_t t = now_ms();
    press_ms[key] = t;
    uint8_t slot = ripple_next;
    ripples[slot].key = key;
    ripples[slot].ms = t;
    ripple_next = (slot + 1) % MAX_RIPPLES;
}

void rgb_led_layer_changed(void)
{
    reload_colors = true;
}

void rgb_led_preview_layer_colors(const char *uuid, const rgb_layer_colors_t *colors)
{
    if (colors_lock == NULL)
        return;
    xSemaphoreTake(colors_lock, portMAX_DELAY);
    shown_colors = *colors;
    xSemaphoreGive(colors_lock);
    reload_colors = false; // keep showing the preview until the layer changes
}

// ---------------------------------------------------------------------------------------------
// Per-layer key colours in NVS: blob "k_<uuid>" in namespace "led_keys"

static void colors_key(const char *uuid, char key[16])
{
    snprintf(key, 16, "k_%.12s", uuid);
}

bool rgb_layer_colors_load(const char *uuid, int layer_index, const uint16_t key_map[4][4],
                           const rgb_mode_t *mode, rgb_layer_colors_t *out)
{
    nvs_handle_t nvs;
    char key[16];
    colors_key(uuid, key);
    if (nvs_open(LAYER_COLORS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK)
    {
        size_t size = sizeof(*out);
        esp_err_t res = nvs_get_blob(nvs, key, out, &size);
        nvs_close(nvs);
        if (res == ESP_OK && size == sizeof(*out))
            return true;
    }
    // Defaults: assigned keys in color1, empty keys off (as the old "solid per key" mode)
    for (int i = 0; i < RGB_LED_KEYBOARD_NUMBER; i++)
    {
        bool assigned = key_map != NULL && key_map[i / 4][i % 4] != 0;
        for (int c = 0; c < 3; c++)
            out->keys[i][c] = assigned ? mode->rgb[c] : 0;
    }
    memcpy(out->layer, layer_palette[(layer_index < 0 ? 0 : layer_index) % 8], 3);
    return false;
}

esp_err_t rgb_layer_colors_save(const char *uuid, const rgb_layer_colors_t *colors)
{
    nvs_handle_t nvs;
    char key[16];
    colors_key(uuid, key);
    esp_err_t res = nvs_open(LAYER_COLORS_NAMESPACE, NVS_READWRITE, &nvs);
    if (res != ESP_OK)
        return res;
    res = nvs_set_blob(nvs, key, colors, sizeof(*colors));
    if (res == ESP_OK)
        res = nvs_commit(nvs);
    nvs_close(nvs);
    return res;
}

esp_err_t rgb_layer_colors_erase(const char *uuid)
{
    nvs_handle_t nvs;
    char key[16];
    colors_key(uuid, key);
    esp_err_t res = nvs_open(LAYER_COLORS_NAMESPACE, NVS_READWRITE, &nvs);
    if (res != ESP_OK)
        return res;
    res = nvs_erase_key(nvs, key);
    if (res == ESP_ERR_NVS_NOT_FOUND)
        res = ESP_OK;
    if (res == ESP_OK)
        res = nvs_commit(nvs);
    nvs_close(nvs);
    return res;
}

esp_err_t rgb_layer_colors_erase_all(void)
{
    nvs_handle_t nvs;
    esp_err_t res = nvs_open(LAYER_COLORS_NAMESPACE, NVS_READWRITE, &nvs);
    if (res != ESP_OK)
        return res;
    res = nvs_erase_all(nvs);
    if (res == ESP_OK)
        res = nvs_commit(nvs);
    nvs_close(nvs);
    return res;
}

// Load the colours of the active layer into shown_colors
static void load_active_layer_colors(const rgb_mode_t *mode)
{
    rgb_layer_colors_t colors;
    layouts_lock();
    if (current_layout < layers_num)
        rgb_layer_colors_load(key_layouts[current_layout].uuid_str, current_layout,
                              (const uint16_t(*)[4])key_layouts[current_layout].key_map, mode, &colors);
    else
        rgb_layer_colors_load("", 0, NULL, mode, &colors);
    layouts_unlock();

    xSemaphoreTake(colors_lock, portMAX_DELAY);
    shown_colors = colors;
    xSemaphoreGive(colors_lock);
}

// ---------------------------------------------------------------------------------------------
// Rendering

static float speed_factor(const rgb_mode_t *mode)
{
    return 0.2f + mode->speed / 64.0f; // 0.2x to 4.2x
}

static void render(const rgb_mode_t *mode, uint32_t t, uint8_t out[RGB_LED_KEYBOARD_NUMBER][3], uint8_t notif[3])
{
    static float twinkle[RGB_LED_KEYBOARD_NUMBER];
    static uint8_t twinkle_color[RGB_LED_KEYBOARD_NUMBER];
    const float sp = speed_factor(mode);
    const float pi2 = 6.2831853f;

    rgb_layer_colors_t colors;
    xSemaphoreTake(colors_lock, portMAX_DELAY);
    colors = shown_colors;
    xSemaphoreGive(colors_lock);

    for (int i = 0; i < RGB_LED_KEYBOARD_NUMBER; i++)
    {
        int row = i / 4, col = i % 4;
        uint8_t *c = out[i];
        memset(c, 0, 3);

        switch (mode->mode)
        {
        case LED_EFFECT_COLOR_CYCLE:
            hue_rgb(t * sp * 0.036f, c); // one turn in 10 s at 1x
            break;
        case LED_EFFECT_RAINBOW_WAVE:
            hue_rgb(t * sp * 0.072f + col * 70.0f + row * 25.0f, c);
            break;
        case LED_EFFECT_SOLID:
            memcpy(c, mode->rgb, 3);
            break;
        case LED_EFFECT_PER_KEY:
            memcpy(c, colors.keys[i], 3);
            break;
        case LED_EFFECT_BREATHING:
            memcpy(c, mode->rgb, 3);
            scale(c, 0.08f + 0.92f * (0.5f - 0.5f * cosf(pi2 * t * sp / 5000.0f)));
            break;
        case LED_EFFECT_RIPPLE:
        {
            memcpy(c, mode->rgb, 3);
            scale(c, 0.12f);
            for (int r = 0; r < MAX_RIPPLES; r++)
            {
                float age = (float)(t - ripples[r].ms);
                if (ripples[r].ms == 0 || age > RIPPLE_LIFE_MS)
                    continue;
                int pr = ripples[r].key / 4, pc = ripples[r].key % 4;
                float dist = sqrtf((float)((row - pr) * (row - pr) + (col - pc) * (col - pc)));
                float radius = age / 1000.0f * 3.0f * sp;
                float k = (1.0f - fabsf(dist - radius) / 0.9f) * (1.0f - age / RIPPLE_LIFE_MS);
                if (k > 0)
                    mix(c, mode->rgb2, k);
            }
            break;
        }
        case LED_EFFECT_GRADIENT:
        {
            memcpy(c, mode->rgb, 3);
            float k = 0.5f + 0.5f * sinf(pi2 * ((col + row * 0.5f) / 5.0f + t * sp / 8000.0f));
            mix(c, mode->rgb2, k);
            break;
        }
        case LED_EFFECT_TWINKLE:
        {
            // each frame a key may light up; lit keys fade out
            if (twinkle[i] < 0.02f && (esp_random() % 1000) < (uint32_t)(4 + 12 * sp))
            {
                twinkle[i] = 1.0f;
                twinkle_color[i] = esp_random() & 1;
            }
            memcpy(c, twinkle_color[i] ? mode->rgb2 : mode->rgb, 3);
            scale(c, twinkle[i]);
            twinkle[i] *= 1.0f - 0.04f * sp;
            break;
        }
        case LED_EFFECT_LAYER_COLOR:
            memcpy(c, colors.layer, 3);
            break;
        default: // off and reactive: dark background
            break;
        }

        // Press highlight: the reactive effect in color1, the overlay on any effect in color2
        bool highlight = mode->mode == LED_EFFECT_REACTIVE || (mode->reactive && mode->mode != LED_EFFECT_OFF);
        if (highlight && press_ms[i] != 0)
        {
            float fade = PRESS_FADE_MS / (0.5f + sp * 0.5f);
            float age = (float)(t - press_ms[i]);
            if (age < fade)
                mix(c, mode->mode == LED_EFFECT_REACTIVE ? mode->rgb : mode->rgb2, 1.0f - age / fade);
        }
        scale(c, idle_off ? 0.0f : mode->brightness / 255.0f);
    }

    // notification LEDs: the colour of the active layer
    memcpy(notif, colors.layer, 3);
    scale(notif, (mode->mode == LED_EFFECT_OFF || idle_off) ? 0.0f : mode->brightness / 255.0f);
}

void rgb_led_set_idle(bool idle)
{
    idle_off = idle;
}

void key_led_modes(void)
{
    rgb_mode_t mode = {0};
    nvs_load_led_mode(&mode);
    uint8_t frame[RGB_LED_KEYBOARD_NUMBER][3];
    uint8_t notif[3];
    uint8_t last_notif[3] = {1, 1, 1};

    while (true)
    {
        if (xQueueReceive(keyled_q, &mode, 0))
        {
            ESP_LOGI(TAG, "effect %d brightness %d speed %d reactive %d", mode.mode, mode.brightness, mode.speed, mode.reactive);
            reload_colors = true; // default key colours depend on color1
        }
        if (reload_colors)
        {
            reload_colors = false;
            load_active_layer_colors(&mode);
        }

        render(&mode, now_ms(), frame, notif);
        for (int i = 0; i < RGB_LED_KEYBOARD_NUMBER; i++)
            rgb_key->set_pixel(rgb_key, i, frame[i][0], frame[i][1], frame[i][2]);
        rgb_key->refresh(rgb_key, 100);

        if (memcmp(notif, last_notif, 3) != 0)
        {
            for (int i = 0; i < RGB_LED_NOTIFICATION_NUMBER; i++)
                rgb_notif->set_pixel(rgb_notif, i, notif[0], notif[1], notif[2]);
            rgb_notif->refresh(rgb_notif, 100);
            memcpy(last_notif, notif, 3);
        }
        vTaskDelay(pdMS_TO_TICKS(RGB_LED_REFRESH_SPEED));
    }
}
