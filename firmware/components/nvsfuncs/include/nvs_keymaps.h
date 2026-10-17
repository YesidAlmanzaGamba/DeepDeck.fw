/*
 * nvs_keymaps.h
 *
 *  Created on: 19 Sep 2018
 *      Author: gal
 */

#ifndef NVS_KEYMAPS_H_
#define NVS_KEYMAPS_H_
#include <keyboard_config.h>
#include "uuid.h"


//*************new*********

typedef struct dd_layer_str {
	char name[MAX_LAYOUT_NAME_LENGTH];                     // Name up to 14 characters
	uint16_t key_map[MATRIX_ROWS][MATRIX_COLS];            // Key maps
	char key_map_names[MATRIX_ROWS][MATRIX_COLS][7];       // Name of each key up to 6 characters
	uint16_t left_encoder_map[ENCODER_SIZE];               // Map ofr left encoder
	uint16_t right_encoder_map[ENCODER_SIZE];              // Map for rigth encoder
	bool active;
	char uuid_str[SHORT_UUID_STR_LEN];
} dd_layer;

// Layer layout stored by firmware <= 0.5.7, which still had the gesture sensor map.
// Only used to migrate layers already saved in NVS to dd_layer.
#define LEGACY_GESTURE_SIZE 6
typedef struct dd_layer_v1_str {
	char name[MAX_LAYOUT_NAME_LENGTH];
	uint16_t key_map[MATRIX_ROWS][MATRIX_COLS];
	char key_map_names[MATRIX_ROWS][MATRIX_COLS][7];
	uint16_t left_encoder_map[ENCODER_SIZE];
	uint16_t right_encoder_map[ENCODER_SIZE];
	uint16_t gesture_map[LEGACY_GESTURE_SIZE];
	bool active;
	char uuid_str[SHORT_UUID_STR_LEN];
} dd_layer_v1;


// typedef struct dd_macros_str {
// 	uint16_t key[USER_MACROS_NUM][MACRO_LEN];  				//200 keys ---> MACRO_LEN = 5       
// 	char name[USER_MACROS_NUM][USER_MACRO_NAME_LEN];       //  200 macros ---> Name of each macro up to 32 characters
// 	int keycode[USER_MACROS_NUM];
// } dd_macros;

typedef struct dd_macros_str {
	uint16_t key[MACRO_LEN];  				//200 keys ---> MACRO_LEN = 5       
	char name[USER_MACRO_NAME_LEN];       //  200 macros ---> Name of each macro up to 32 characters
	int keycode;
} dd_macros;


extern dd_macros *user_macros;
extern dd_layer *key_layouts;

//amount of arrays
extern uint8_t layers_num;
extern int total_macros;

#endif /* NVS_KEYMAPS_H_ */



