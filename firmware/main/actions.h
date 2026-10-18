/**
 * @file actions.h
 * @brief Keys that do more than one thing (R6.7): mod-tap, tap-dance and sequences.
 *
 * Key codes (docs/protocol.md):
 * - mod-tap   0x2000 + modifier * 256 + key: tap = the key, hold = the modifier (0 LCtrl .. 7 RGui)
 * - tap-dance 0x3000 + n (n < 16): one action for a tap, one for a double tap, one for a hold
 * - sequence  0x4000 + n (n < 32): key chords, pauses and typed text, one after the other
 */

#ifndef ACTIONS_H_
#define ACTIONS_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MODTAP_BASE 0x2000
#define MODTAP_MAX 0x27FF
#define TAPDANCE_BASE 0x3000
#define TAPDANCE_NUM 16
#define SEQUENCE_BASE 0x4000
#define SEQUENCE_NUM 32
#define TAPPING_TERM_MS 200

#define SEQUENCE_NAME_LEN 16
#define SEQUENCE_MAX_BYTES 240 // encoded steps, see actions.c

typedef struct
{
	uint16_t tap;
	uint16_t double_tap;
	uint16_t hold;
} tapdance_t;

typedef struct
{
	char name[SEQUENCE_NAME_LEN];
	uint8_t len;
	uint8_t steps[SEQUENCE_MAX_BYTES];
} sequence_t;

/** @brief Load the tap-dance table and start the sequence runner. Call once at boot. */
void actions_init(void);

/** @brief True for codes handled here (mod-tap, tap-dance, sequence). */
bool actions_handles(uint16_t code);

/** @brief Key position pos (0-15) with one of those codes was pressed / released. */
void actions_press(uint8_t pos, uint16_t code);
void actions_release(uint8_t pos);

/** @brief A plain key was pressed: a pending mod-tap becomes its modifier (typing Ctrl+C with it). */
void actions_other_key_pressed(void);

/**
 * @brief Once per matrix scan, from the key task: timeouts, and the keys and modifiers these
 * actions add to the report (slots 18-22 of the 23-byte report).
 * @return the modifier bits to OR into the report
 */
uint8_t actions_tick(uint8_t *report);

/** @brief Do the "tap" of any action code, from any task (knobs, the app). */
void actions_tap_code(uint16_t code);

/** @brief Run sequence n in the background. */
void actions_run_sequence(uint8_t n);

tapdance_t actions_tapdance_get(uint8_t n);
esp_err_t actions_tapdance_set(uint8_t n, const tapdance_t *td);
bool actions_sequence_get(uint8_t n, sequence_t *out);
esp_err_t actions_sequence_set(uint8_t n, const sequence_t *seq);

/* Sequence step encoding (sequence_t.steps):
 *   0x01 n k1..kn   press the keys k (HID codes, modifiers 224-231 included) together, then release
 *   0x02 lo hi      wait lo + 256*hi milliseconds
 *   0x03 n c1..cn   type the ASCII text c (as on a US keyboard layout)
 */
#define SEQ_STEP_KEYS 0x01
#define SEQ_STEP_DELAY 0x02
#define SEQ_STEP_TEXT 0x03

#endif
