/**
 * @file actions.c
 * @brief Mod-tap, tap-dance and sequences (R6.7). See actions.h.
 *
 * Runs inside the key task (actions_press/release/tick are called from check_key_state), except
 * sequences, which have their own task so a long one does not stop the keys from being scanned.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "actions.h"
#include "keyboard_config.h"
#include "key_definitions.h"
#include "keypress_handles.h"
#include "nvs_keymaps.h"
#include "nvs_funcs.h"
#include "hal_ble.h"

#define TAG "actions"
#define NS_TAPDANCE "tapdance"
#define NS_SEQUENCE "seq"
#define INJECT_FIRST 18 // report slots 18-22 are free (2 + 16 keys): taps and chords go there
#define INJECT_SLOTS 5
#define KEY_POSITIONS 16

// ------------------------------------------------------------------------------------------------
// Taps queued for the report: a chord of up to 5 keys plus modifiers, pressed for one scan

typedef struct
{
	uint8_t mods;
	uint8_t keys[INJECT_SLOTS];
} chord_t;

static QueueHandle_t tap_q;
static tapdance_t tapdance[TAPDANCE_NUM]; // loaded by actions_init
static chord_t tap_now;		 // in the report during this scan
static uint8_t tap_phase = 0; // 0 idle, 1 pressed (in the report), 2 released for one scan

static uint8_t mod_bit(uint16_t key)
{
	return (key >= KC_LCTRL && key <= KC_RGUI) ? (1 << (key - KC_LCTRL)) : 0;
}

static void chord_add(chord_t *c, uint16_t key)
{
	if (mod_bit(key))
	{
		c->mods |= mod_bit(key);
		return;
	}
	for (int i = 0; i < INJECT_SLOTS; i++)
	{
		if (c->keys[i] == 0)
		{
			c->keys[i] = (uint8_t)key;
			return;
		}
	}
}

static void send_media(uint16_t code)
{
	uint8_t media[2] = {0, 0};
	switch (code)
	{
	case KC_MEDIA_NEXT_TRACK:
		media[1] = 10;
		break;
	case KC_MEDIA_PREV_TRACK:
		media[1] = 11;
		break;
	case KC_MEDIA_STOP:
		media[1] = 12;
		break;
	case KC_MEDIA_PLAY_PAUSE:
		media[1] = 5;
		break;
	case KC_AUDIO_MUTE:
		media[1] = 1;
		break;
	case KC_AUDIO_VOL_UP:
		media[0] = 1 << 6;
		break;
	case KC_AUDIO_VOL_DOWN:
		media[0] = 1 << 7;
		break;
	default:
		return;
	}
	if (media_q == NULL)
		return;
	xQueueSend(media_q, media, 0);
	memset(media, 0, sizeof(media));
	xQueueSend(media_q, media, 0);
}

void actions_tap_code(uint16_t code)
{
	chord_t c = {0};
	if (code == KC_NO)
		return;
	if (code >= MODTAP_BASE && code <= MODTAP_MAX)
		code = code & 0xFF; // tapped: just the key
	if (code >= TAPDANCE_BASE && code < TAPDANCE_BASE + TAPDANCE_NUM)
	{
		uint16_t tap = tapdance[code - TAPDANCE_BASE].tap; // e.g. on a knob: its tap action
		if (tap < TAPDANCE_BASE || tap >= TAPDANCE_BASE + TAPDANCE_NUM)
			actions_tap_code(tap);
		return;
	}
	if (code >= SEQUENCE_BASE && code < SEQUENCE_BASE + SEQUENCE_NUM)
	{
		actions_run_sequence(code - SEQUENCE_BASE);
		return;
	}
	if (code >= KC_MEDIA_NEXT_TRACK && code <= KC_AUDIO_VOL_DOWN)
	{
		send_media(code);
		return;
	}
	if (code > LAYER_ADJUST_MIN && code < LAYER_ADJUST_MAX)
	{
		layer_adjust(code);
		return;
	}
	if (code >= LAYER_HOLD_BASE_VAL && code <= LAYER_HOLD_MAX_VAL)
	{
		if (code - LAYER_HOLD_BASE_VAL < layers_num)
			set_current_layout(code - LAYER_HOLD_BASE_VAL); // a tap cannot hold: switch to it
		return;
	}
	if (code >= MACRO_BASE_VAL && code <= MACRO_HOLD_MAX_VAL)
	{
		layouts_lock();
		if (code - MACRO_BASE_VAL < total_macros)
		{
			for (int i = 0; i < MACRO_LEN; i++)
				if (user_macros[code - MACRO_BASE_VAL].key[i] != KC_NO)
					chord_add(&c, user_macros[code - MACRO_BASE_VAL].key[i]);
		}
		layouts_unlock();
	}
	else if (code < 0xE8)
	{
		chord_add(&c, code);
	}
	else
	{
		return;
	}
	if (tap_q != NULL)
		xQueueSend(tap_q, &c, 0);
}

// ------------------------------------------------------------------------------------------------
// Per key position: mod-tap and tap-dance state

typedef enum
{
	ST_IDLE = 0,
	ST_DOWN,	 // pressed, not yet decided
	ST_HOLD,	 // held past the tapping term (or, for mod-tap, another key came)
	ST_WAIT,	 // tap-dance: released once, waiting for a second tap
	ST_DOWN2,	 // tap-dance: pressed a second time
} key_state_t;

typedef struct
{
	uint16_t code;
	key_state_t state;
	int64_t t_press;
	int64_t t_release;
	uint8_t hold_mods; // modifiers held while ST_HOLD
	uint8_t hold_key;	// a plain key held while ST_HOLD (tap-dance hold action)
} key_action_t;

static key_action_t keys[KEY_POSITIONS];

static int64_t now_ms(void)
{
	return esp_timer_get_time() / 1000;
}

bool actions_handles(uint16_t code)
{
	return (code >= MODTAP_BASE && code <= MODTAP_MAX) ||
		   (code >= TAPDANCE_BASE && code < TAPDANCE_BASE + TAPDANCE_NUM) ||
		   (code >= SEQUENCE_BASE && code < SEQUENCE_BASE + SEQUENCE_NUM);
}

static bool is_modtap(uint16_t code)
{
	return code >= MODTAP_BASE && code <= MODTAP_MAX;
}

static tapdance_t *td_of(uint16_t code)
{
	return &tapdance[code - TAPDANCE_BASE];
}

// Hold action of a tap-dance: a key or modifier stays down while held; anything else fires once
static void start_hold(key_action_t *k)
{
	k->state = ST_HOLD;
	if (is_modtap(k->code))
	{
		k->hold_mods = 1 << ((k->code >> 8) & 0x07);
		return;
	}
	uint16_t hold = td_of(k->code)->hold;
	if (hold == KC_NO)
		hold = td_of(k->code)->tap; // no hold action: a long press is a tap
	if (mod_bit(hold))
		k->hold_mods = mod_bit(hold);
	else if (hold > KC_NO && hold < 0xE8 && !(hold >= KC_MEDIA_NEXT_TRACK && hold <= KC_AUDIO_VOL_DOWN))
		k->hold_key = (uint8_t)hold;
	else
		actions_tap_code(hold);
}

void actions_press(uint8_t pos, uint16_t code)
{
	if (pos >= KEY_POSITIONS)
		return;
	key_action_t *k = &keys[pos];
	if (code >= SEQUENCE_BASE && code < SEQUENCE_BASE + SEQUENCE_NUM)
	{
		actions_run_sequence(code - SEQUENCE_BASE);
		return;
	}
	if (k->state == ST_WAIT && k->code == code)
	{
		k->state = ST_DOWN2;
		return;
	}
	if (k->state == ST_WAIT) // a different action now on this key (layer changed): finish the old one
		actions_tap_code(td_of(k->code)->tap);
	memset(k, 0, sizeof(*k));
	k->code = code;
	k->state = ST_DOWN;
	k->t_press = now_ms();
}

void actions_release(uint8_t pos)
{
	if (pos >= KEY_POSITIONS)
		return;
	key_action_t *k = &keys[pos];
	switch (k->state)
	{
	case ST_DOWN:
		if (is_modtap(k->code))
		{
			actions_tap_code(k->code & 0xFF);
			k->state = ST_IDLE;
		}
		else if (td_of(k->code)->double_tap == KC_NO)
		{
			actions_tap_code(td_of(k->code)->tap); // no double tap: no need to wait
			k->state = ST_IDLE;
		}
		else
		{
			k->state = ST_WAIT;
			k->t_release = now_ms();
		}
		break;
	case ST_DOWN2:
		actions_tap_code(td_of(k->code)->double_tap);
		k->state = ST_IDLE;
		break;
	case ST_HOLD:
		k->hold_mods = 0;
		k->hold_key = 0;
		k->state = ST_IDLE;
		break;
	default:
		break;
	}
}

void actions_other_key_pressed(void)
{
	for (int i = 0; i < KEY_POSITIONS; i++)
	{
		if (keys[i].state == ST_DOWN && is_modtap(keys[i].code))
			start_hold(&keys[i]);
	}
}

uint8_t actions_tick(uint8_t *report)
{
	int64_t t = now_ms();
	uint8_t mods = 0;
	for (int i = 0; i < KEY_POSITIONS; i++)
	{
		key_action_t *k = &keys[i];
		if (k->state == ST_DOWN && t - k->t_press > TAPPING_TERM_MS)
			start_hold(k);
		else if (k->state == ST_WAIT && t - k->t_release > TAPPING_TERM_MS)
		{
			actions_tap_code(td_of(k->code)->tap);
			k->state = ST_IDLE;
		}
		if (k->state == ST_HOLD)
		{
			mods |= k->hold_mods;
			// a tap-dance hold key sits in the key's own report slot (unused: the key is not plain)
			report[2 + i] = k->hold_key;
		}
	}

	// queued taps: one scan pressed, one scan released, then the next
	if (tap_phase == 1)
	{
		tap_phase = 2;
		memset(&tap_now, 0, sizeof(tap_now));
	}
	else if (tap_phase == 2 || tap_phase == 0)
	{
		tap_phase = (tap_q != NULL && xQueueReceive(tap_q, &tap_now, 0) == pdTRUE) ? 1 : 0;
		if (tap_phase == 0)
			memset(&tap_now, 0, sizeof(tap_now));
	}
	memcpy(report + INJECT_FIRST, tap_now.keys, INJECT_SLOTS);
	return mods | tap_now.mods;
}

// ------------------------------------------------------------------------------------------------
// Sequences: their own task, reports straight to the BLE keyboard queue

static QueueHandle_t seq_q;

// US layout: the key and whether Shift is needed for each printable ASCII character
static bool ascii_key(char ch, uint8_t *key, bool *shift)
{
	static const char *row_digits = "1234567890";
	static const char *shifted_digits = "!@#$%^&*()";
	static const struct
	{
		char plain, shifted;
		uint8_t key;
	} symbols[] = {
		{'-', '_', 45}, {'=', '+', 46}, {'[', '{', 47}, {']', '}', 48}, {'\\', '|', 49},
		{';', ':', 51}, {'\'', '"', 52}, {'`', '~', 53}, {',', '<', 54}, {'.', '>', 55}, {'/', '?', 56},
	};
	*shift = false;
	if (ch >= 'a' && ch <= 'z')
		*key = KC_A + (ch - 'a');
	else if (ch >= 'A' && ch <= 'Z')
	{
		*key = KC_A + (ch - 'A');
		*shift = true;
	}
	else if (strchr(row_digits, ch) && ch)
		*key = KC_1 + (strchr(row_digits, ch) - row_digits);
	else if (strchr(shifted_digits, ch) && ch)
	{
		*key = KC_1 + (strchr(shifted_digits, ch) - shifted_digits);
		*shift = true;
	}
	else if (ch == ' ')
		*key = KC_SPACE;
	else if (ch == '\n')
		*key = KC_ENTER;
	else if (ch == '\t')
		*key = KC_TAB;
	else
	{
		for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++)
		{
			if (symbols[i].plain == ch || symbols[i].shifted == ch)
			{
				*key = symbols[i].key;
				*shift = symbols[i].shifted == ch;
				return true;
			}
		}
		return false;
	}
	return true;
}

static void send_report(uint8_t mods, const uint8_t *keys6)
{
	uint8_t report[REPORT_LEN] = {0};
	report[0] = mods;
	if (keys6 != NULL)
		memcpy(report + 2, keys6, 6);
	if (keyboard_q != NULL)
		xQueueSend(keyboard_q, report, pdMS_TO_TICKS(100));
	vTaskDelay(pdMS_TO_TICKS(12)); // one report per connection event or so
}

static void press_release(uint8_t mods, const uint8_t *keys6)
{
	send_report(mods, keys6);
	send_report(0, NULL);
}

static void run_steps(const sequence_t *seq)
{
	size_t i = 0;
	while (i < seq->len)
	{
		uint8_t type = seq->steps[i];
		uint8_t n = (i + 1 < seq->len) ? seq->steps[i + 1] : 0;
		if (type == SEQ_STEP_KEYS && i + 2 + n <= seq->len)
		{
			uint8_t mods = 0, keys6[6] = {0}, k = 0;
			for (int j = 0; j < n; j++)
			{
				uint8_t key = seq->steps[i + 2 + j];
				if (mod_bit(key))
					mods |= mod_bit(key);
				else if (k < 6)
					keys6[k++] = key;
			}
			press_release(mods, keys6);
			i += 2 + n;
		}
		else if (type == SEQ_STEP_DELAY && i + 3 <= seq->len)
		{
			vTaskDelay(pdMS_TO_TICKS(seq->steps[i + 1] | (seq->steps[i + 2] << 8)));
			i += 3;
		}
		else if (type == SEQ_STEP_TEXT && i + 2 + n <= seq->len)
		{
			for (int j = 0; j < n; j++)
			{
				uint8_t key;
				bool shift;
				if (!ascii_key((char)seq->steps[i + 2 + j], &key, &shift))
					continue;
				uint8_t keys6[6] = {key};
				press_release(shift ? mod_bit(KC_LSHIFT) : 0, keys6);
			}
			i += 2 + n;
		}
		else
		{
			ESP_LOGW(TAG, "bad step %d at %d: sequence stopped", type, (int)i);
			return;
		}
	}
}

static void sequence_task(void *arg)
{
	uint8_t n;
	sequence_t *seq = malloc(sizeof(sequence_t));
	while (seq != NULL)
	{
		if (xQueueReceive(seq_q, &n, portMAX_DELAY) != pdTRUE)
			continue;
		if (actions_sequence_get(n, seq) && seq->len > 0)
		{
			ESP_LOGI(TAG, "sequence %d \"%s\"", n, seq->name);
			run_steps(seq);
		}
	}
	vTaskDelete(NULL);
}

void actions_run_sequence(uint8_t n)
{
	if (n < SEQUENCE_NUM && seq_q != NULL)
		xQueueSend(seq_q, &n, 0);
}

// ------------------------------------------------------------------------------------------------
// Storage

tapdance_t actions_tapdance_get(uint8_t n)
{
	tapdance_t none = {0};
	return n < TAPDANCE_NUM ? tapdance[n] : none;
}

esp_err_t actions_tapdance_set(uint8_t n, const tapdance_t *td)
{
	if (n >= TAPDANCE_NUM)
		return ESP_ERR_INVALID_ARG;
	tapdance_t table[TAPDANCE_NUM];
	memcpy(table, tapdance, sizeof(table));
	table[n] = *td;
	nvs_handle_t nvs;
	esp_err_t err = nvs_open(NS_TAPDANCE, NVS_READWRITE, &nvs);
	if (err != ESP_OK)
		return err;
	err = nvs_set_blob(nvs, "td", table, sizeof(table));
	if (err == ESP_OK)
		err = nvs_commit(nvs);
	nvs_close(nvs);
	if (err == ESP_OK)
		tapdance[n] = *td;
	return err;
}

bool actions_sequence_get(uint8_t n, sequence_t *out)
{
	memset(out, 0, sizeof(*out));
	if (n >= SEQUENCE_NUM)
		return false;
	nvs_handle_t nvs;
	if (nvs_open(NS_SEQUENCE, NVS_READONLY, &nvs) != ESP_OK)
		return false;
	char key[8];
	snprintf(key, sizeof(key), "s%d", n);
	size_t size = sizeof(*out);
	bool ok = nvs_get_blob(nvs, key, out, &size) == ESP_OK && size == sizeof(*out);
	nvs_close(nvs);
	if (!ok)
		memset(out, 0, sizeof(*out));
	return ok;
}

esp_err_t actions_sequence_set(uint8_t n, const sequence_t *seq)
{
	if (n >= SEQUENCE_NUM)
		return ESP_ERR_INVALID_ARG;
	nvs_handle_t nvs;
	esp_err_t err = nvs_open(NS_SEQUENCE, NVS_READWRITE, &nvs);
	if (err != ESP_OK)
		return err;
	char key[8];
	snprintf(key, sizeof(key), "s%d", n);
	if (seq->len == 0 && seq->name[0] == '\0')
	{
		err = nvs_erase_key(nvs, key);
		if (err == ESP_ERR_NVS_NOT_FOUND)
			err = ESP_OK;
	}
	else
	{
		err = nvs_set_blob(nvs, key, seq, sizeof(*seq));
	}
	if (err == ESP_OK)
		err = nvs_commit(nvs);
	nvs_close(nvs);
	return err;
}

void actions_init(void)
{
	nvs_handle_t nvs;
	if (nvs_open(NS_TAPDANCE, NVS_READONLY, &nvs) == ESP_OK)
	{
		size_t size = sizeof(tapdance);
		if (nvs_get_blob(nvs, "td", tapdance, &size) != ESP_OK || size != sizeof(tapdance))
			memset(tapdance, 0, sizeof(tapdance));
		nvs_close(nvs);
	}
	tap_q = xQueueCreate(16, sizeof(chord_t));
	seq_q = xQueueCreate(4, sizeof(uint8_t));
	xTaskCreate(sequence_task, "sequences", 3072, NULL, 3, NULL);
}
