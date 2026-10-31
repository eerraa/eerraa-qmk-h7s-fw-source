/* Copyright 2019 Jason Williams (Wilba)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "eeconfig.h" // for EECONFIG_SIZE
#include "action.h"

// Keyboard level code can change where VIA stores the magic.
// The magic is the build date YYMMDD encoded as BCD in 3 bytes,
// thus installing firmware built on a different date to the one
// already installed can be detected and the EEPROM data is reset.
// The only reason this is important is in case EEPROM usage changes
// and the EEPROM was not explicitly reset by bootmagic lite.
#ifndef VIA_EEPROM_MAGIC_ADDR
#    define VIA_EEPROM_MAGIC_ADDR (EECONFIG_SIZE)
#endif

#define VIA_EEPROM_LAYOUT_OPTIONS_ADDR (VIA_EEPROM_MAGIC_ADDR + 3)

// Changing the layout options size after release will invalidate EEPROM,
// but this is something that should be set correctly on initial implementation.
// 1 byte is enough for most uses (i.e. 8 binary states, or 6 binary + 1 ternary/quaternary )
#ifndef VIA_EEPROM_LAYOUT_OPTIONS_SIZE
#    define VIA_EEPROM_LAYOUT_OPTIONS_SIZE 1
#endif

// Allow override of the layout options default value.
// This requires advanced knowledge of how VIA stores layout options
// and is only really useful for setting a boolean layout option
// state to true by default.
#ifndef VIA_EEPROM_LAYOUT_OPTIONS_DEFAULT
#    define VIA_EEPROM_LAYOUT_OPTIONS_DEFAULT 0x00000000
#endif

// The end of the EEPROM memory used by VIA
// By default, dynamic keymaps will start at this if there is no
// custom config
#define VIA_EEPROM_CUSTOM_CONFIG_ADDR (VIA_EEPROM_LAYOUT_OPTIONS_ADDR + VIA_EEPROM_LAYOUT_OPTIONS_SIZE)

#ifndef VIA_EEPROM_CUSTOM_CONFIG_SIZE
#    define VIA_EEPROM_CUSTOM_CONFIG_SIZE 0
#endif

#define VIA_EEPROM_CONFIG_END (VIA_EEPROM_CUSTOM_CONFIG_ADDR + VIA_EEPROM_CUSTOM_CONFIG_SIZE)

// This is changed only when the command IDs change,
// so VIA Configurator can detect compatible firmware.
#define VIA_PROTOCOL_VERSION 0x000C

// This is a version number for the firmware for the keyboard.
// It can be used to ensure the VIA keyboard definition and the firmware
// have the same version, especially if there are changes to custom values.
// Define this in config.h to override and bump this number.
// This is *not* required if the keyboard is only using basic functionality
// and not using custom values for lighting, rotary encoders, etc.
#ifndef VIA_FIRMWARE_VERSION
#    define VIA_FIRMWARE_VERSION 0x00000000
#endif

enum via_command_id {
    id_get_protocol_version                 = 0x01, // always 0x01
    id_get_keyboard_value                   = 0x02,
    id_set_keyboard_value                   = 0x03,
    id_dynamic_keymap_get_keycode           = 0x04,
    id_dynamic_keymap_set_keycode           = 0x05,
    id_dynamic_keymap_reset                 = 0x06,
    id_custom_set_value                     = 0x07,
    id_custom_get_value                     = 0x08,
    id_custom_save                          = 0x09,
    id_eeprom_reset                         = 0x0A,
    id_bootloader_jump                      = 0x0B,
    id_dynamic_keymap_macro_get_count       = 0x0C,
    id_dynamic_keymap_macro_get_buffer_size = 0x0D,
    id_dynamic_keymap_macro_get_buffer      = 0x0E,
    id_dynamic_keymap_macro_set_buffer      = 0x0F,
    id_dynamic_keymap_macro_reset           = 0x10,
    id_dynamic_keymap_get_layer_count       = 0x11,
    id_dynamic_keymap_get_buffer            = 0x12,
    id_dynamic_keymap_set_buffer            = 0x13,
    id_dynamic_keymap_get_encoder           = 0x14,
    id_dynamic_keymap_set_encoder           = 0x15,
    id_unhandled                            = 0xFF,
};

enum via_keyboard_value_id {
    id_uptime              = 0x01,
    id_layout_options      = 0x02,
    id_switch_matrix_state = 0x03,
    id_firmware_version    = 0x04,
    id_device_indication   = 0x05,
    id_era_state_sync      = 0x06,  // V260821R1: GET_KEYBOARD_VALUE 리비전 봉투
    // 0x07 is reserved for the retired ERA USB diagnostics selector.
};

enum via_channel_id {
    id_custom_channel         = 0,
    id_qmk_backlight_channel  = 1,
    id_qmk_rgblight_channel   = 2,
    id_qmk_rgb_matrix_channel = 3,
    id_qmk_audio_channel      = 4,
    id_qmk_led_matrix_channel = 5,
    id_qmk_version            = 8,
    id_qmk_system             = 9,
    id_qmk_kill_switch_lr     = 10,
    id_qmk_kill_switch_ud     = 11,
    id_qmk_kkuk               = 12,
    id_qmk_usb_polling        = 13,  // V251108R1: BootMode 전용 채널. value id 3(구 USB 모니터 토글)은 V260823R2에서 폐기 — 재사용 금지, docs/contract_usb.md
    id_qmk_key_response       = 14,  // V251115R1: VIA 디바운스 프로필 제어 채널
    id_qmk_tapping            = 15,  // V251123R4: VIA TAPPING 제어 채널
    id_qmk_tapdance           = 16,  // V251124R8: VIA TAPDANCE 제어 채널
    id_qmk_mousekey           = 17,  // V260823R1: VIA MOUSE 제어 채널 (참조 QMK 13번은 H7S에서 USB POLLING이 점유)
    id_qmk_rgb_sleep          = 18,  // V260901R1: VIA RGB SLEEP 타임아웃 채널
};

enum via_qmk_backlight_value {
    id_qmk_backlight_brightness = 1,
    id_qmk_backlight_effect     = 2,
};

enum via_qmk_rgblight_value {
    id_qmk_rgblight_brightness   = 1,
    id_qmk_rgblight_effect       = 2,
    id_qmk_rgblight_effect_speed = 3,
    id_qmk_rgblight_color        = 4,
    id_qmk_velocikey_toggle      = 5,  // V251123R1: VIA에서 Velocikey 토글 노출
};

enum via_qmk_rgb_matrix_value {
    id_qmk_rgb_matrix_brightness   = 1,
    id_qmk_rgb_matrix_effect       = 2,
    id_qmk_rgb_matrix_effect_speed = 3,
    id_qmk_rgb_matrix_color        = 4,
};

enum via_qmk_led_matrix_value {
    id_qmk_led_matrix_brightness   = 1,
    id_qmk_led_matrix_effect       = 2,
    id_qmk_led_matrix_effect_speed = 3,
};

enum via_qmk_audio_value {
    id_qmk_audio_enable        = 1,
    id_qmk_audio_clicky_enable = 2,
};

// V251108R1: Brick60 USB POLLING 메뉴 value ID 매핑
enum via_qmk_usb_polling_value {
    id_qmk_usb_bootmode_select = 1,
    id_qmk_usb_bootmode_apply  = 2,
    // Value 3 remains reserved.
    id_qmk_usb_polling_current = 4,
};

// V260901R1: 소비자 없는 STATUS를 제거하고 KEY RESPONSE value ID를 설정값 1~4로 한정
enum via_qmk_key_response_value {
    id_qmk_debounce_mode        = 1,
    id_qmk_debounce_time_single = 2,
    id_qmk_debounce_time_pre    = 3,
    id_qmk_debounce_time_post   = 4,
};

// V251123R4: VIA TAPPING 설정 value ID 매핑
enum via_qmk_tapping_value {
    id_qmk_tapping_global_term             = 1,
    id_qmk_tapping_permissive_hold         = 2,
    id_qmk_tapping_hold_on_other_key_press = 3,
    id_qmk_tapping_retro_tapping           = 4,
    id_qmk_tapping_global_term_exact       = 5,  // V260821R1: 2-byte BE exact ms
};

// V251124R8: VIA TAPDANCE value ID 매핑
enum via_qmk_tapdance_value {
    id_qmk_tapdance_1_tap   = 1,
    id_qmk_tapdance_1_hold  = 2,
    id_qmk_tapdance_1_dtap  = 3,
    id_qmk_tapdance_1_thold = 4,
    id_qmk_tapdance_1_term  = 5,

    id_qmk_tapdance_2_tap   = 6,
    id_qmk_tapdance_2_hold  = 7,
    id_qmk_tapdance_2_dtap  = 8,
    id_qmk_tapdance_2_thold = 9,
    id_qmk_tapdance_2_term  = 10,

    id_qmk_tapdance_3_tap   = 11,
    id_qmk_tapdance_3_hold  = 12,
    id_qmk_tapdance_3_dtap  = 13,
    id_qmk_tapdance_3_thold = 14,
    id_qmk_tapdance_3_term  = 15,

    id_qmk_tapdance_4_tap   = 16,
    id_qmk_tapdance_4_hold  = 17,
    id_qmk_tapdance_4_dtap  = 18,
    id_qmk_tapdance_4_thold = 19,
    id_qmk_tapdance_4_term  = 20,

    id_qmk_tapdance_5_tap   = 21,
    id_qmk_tapdance_5_hold  = 22,
    id_qmk_tapdance_5_dtap  = 23,
    id_qmk_tapdance_5_thold = 24,
    id_qmk_tapdance_5_term  = 25,

    id_qmk_tapdance_6_tap   = 26,
    id_qmk_tapdance_6_hold  = 27,
    id_qmk_tapdance_6_dtap  = 28,
    id_qmk_tapdance_6_thold = 29,
    id_qmk_tapdance_6_term  = 30,

    id_qmk_tapdance_7_tap   = 31,
    id_qmk_tapdance_7_hold  = 32,
    id_qmk_tapdance_7_dtap  = 33,
    id_qmk_tapdance_7_thold = 34,
    id_qmk_tapdance_7_term  = 35,

    id_qmk_tapdance_8_tap   = 36,
    id_qmk_tapdance_8_hold  = 37,
    id_qmk_tapdance_8_dtap  = 38,
    id_qmk_tapdance_8_thold = 39,
    id_qmk_tapdance_8_term  = 40,

    id_qmk_tapdance_1_term_exact = 41,  // V260821R1: TD0–TD7 exact ms, channel 16
    id_qmk_tapdance_2_term_exact = 42,
    id_qmk_tapdance_3_term_exact = 43,
    id_qmk_tapdance_4_term_exact = 44,
    id_qmk_tapdance_5_term_exact = 45,
    id_qmk_tapdance_6_term_exact = 46,
    id_qmk_tapdance_7_term_exact = 47,
    id_qmk_tapdance_8_term_exact = 48,
    id_qmk_tapdance_1_mode = 49,
    id_qmk_tapdance_2_mode = 50,
    id_qmk_tapdance_3_mode = 51,
    id_qmk_tapdance_4_mode = 52,
    id_qmk_tapdance_5_mode = 53,
    id_qmk_tapdance_6_mode = 54,
    id_qmk_tapdance_7_mode = 55,
    id_qmk_tapdance_8_mode = 56,
    id_qmk_tapdance_1_hold_term = 57,
    id_qmk_tapdance_2_hold_term = 58,
    id_qmk_tapdance_3_hold_term = 59,
    id_qmk_tapdance_4_hold_term = 60,
    id_qmk_tapdance_5_hold_term = 61,
    id_qmk_tapdance_6_hold_term = 62,
    id_qmk_tapdance_7_hold_term = 63,
    id_qmk_tapdance_8_hold_term = 64,
    id_qmk_tapdance_1_hold_other = 65,
    id_qmk_tapdance_2_hold_other = 66,
    id_qmk_tapdance_3_hold_other = 67,
    id_qmk_tapdance_4_hold_other = 68,
    id_qmk_tapdance_5_hold_other = 69,
    id_qmk_tapdance_6_hold_other = 70,
    id_qmk_tapdance_7_hold_other = 71,
    id_qmk_tapdance_8_hold_other = 72,


};

// V260823R1: VIA MOUSE 제어 값 ID (참조 QMK era_mousekey_via.c와 동일한 1..6 배치)
enum via_qmk_mousekey_value {
    id_qmk_mousekey_cursor_min_speed    = 1,
    id_qmk_mousekey_cursor_max_speed    = 2,
    id_qmk_mousekey_cursor_acceleration = 3,
    id_qmk_mousekey_cursor_interval     = 4,
    id_qmk_mousekey_wheel_interval      = 5,
    id_qmk_mousekey_wheel_acceleration  = 6,
};

// V260901R1: VIA RGB SLEEP. 공식 JSON은 value 1 분 프리셋을 유지하고,
//            Custom VIA는 additive value 2의 BE16 exact seconds를 사용한다. 저장은 공통 uint16 초.
enum via_qmk_rgb_sleep_value {
    id_qmk_rgb_sleep_timeout       = 1,
    id_qmk_rgb_sleep_timeout_exact = 2,
    id_qmk_rgb_sleep_enable        = 3,
};

// V251012R2: 커스텀 인디케이터 제어 값 ID
enum via_qmk_custom_indicator_value {
    id_qmk_custom_ind_selec      = 1,
    id_qmk_custom_ind_brightness = 2,
    id_qmk_custom_ind_color      = 3,
    id_qmk_custom_ind_2_selec      = 4,  // V260310R1: BRICK65 Indicator 2는 custom channel 내 별도 value ID 사용
    id_qmk_custom_ind_2_brightness = 5,
    id_qmk_custom_ind_2_color      = 6,
};

// Can be called in an overriding via_init_kb() to test if keyboard level code usage of
// EEPROM is invalid and use/save defaults.
bool via_eeprom_is_valid(void);

// Sets VIA/keyboard level usage of EEPROM to valid/invalid
// Keyboard level code (eg. via_init_kb()) should not call this
void via_eeprom_set_valid(bool valid);

// Called by QMK core to initialize dynamic keymaps etc.
void eeconfig_init_via(void);
bool eeconfig_init_via_checked(void);
void eeconfig_publish_via_defaults(void);
void via_init(void);

// Used by VIA to store and retrieve the layout options.
uint32_t via_get_layout_options(void);
void     via_set_layout_options(uint32_t value);
void     via_set_layout_options_kb(uint32_t value);

// Used by VIA to tell a device to flash LEDs (or do something else) when that
// device becomes the active device being configured, on startup or switching
// between devices.
void via_set_device_indication(uint8_t value);

// Called by QMK core to process VIA-specific keycodes.
bool process_record_via(uint16_t keycode, keyrecord_t *record);

// These are made external so that keyboard level custom value handlers can use them.
#if defined(BACKLIGHT_ENABLE)
void via_qmk_backlight_command(uint8_t *data, uint8_t length);
void via_qmk_backlight_set_value(uint8_t *data);
void via_qmk_backlight_get_value(uint8_t *data);
void via_qmk_backlight_save(void);
#endif

#if defined(RGBLIGHT_ENABLE)
void via_qmk_rgblight_command(uint8_t *data, uint8_t length);
void via_qmk_rgblight_set_value(uint8_t *data);
void via_qmk_rgblight_get_value(uint8_t *data);
void via_qmk_rgblight_save(void);
#endif

#if defined(RGB_MATRIX_ENABLE)
void via_qmk_rgb_matrix_command(uint8_t *data, uint8_t length);
void via_qmk_rgb_matrix_set_value(uint8_t *data);
void via_qmk_rgb_matrix_get_value(uint8_t *data);
void via_qmk_rgb_matrix_save(void);
#endif

#if defined(LED_MATRIX_ENABLE)
void via_qmk_led_matrix_command(uint8_t *data, uint8_t length);
void via_qmk_led_matrix_set_value(uint8_t *data);
void via_qmk_led_matrix_get_value(uint8_t *data);
void via_qmk_led_matrix_save(void);
#endif

#if defined(AUDIO_ENABLE)
void via_qmk_audio_command(uint8_t *data, uint8_t length);
void via_qmk_audio_set_value(uint8_t *data);
void via_qmk_audio_get_value(uint8_t *data);
void via_qmk_audio_save(void);
#endif
