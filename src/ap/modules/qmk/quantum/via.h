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
#include "keycodes.h"

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
#define VIA_PROTOCOL_VERSION ERA_WIRE_VIA_PROTOCOL_VERSION

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
    id_keycodes_version    = 0x06,
};

enum era_keyboard_value_id {
    id_era_state_sync = ERA_WIRE_STATE_SYNC_SELECTORS_CURRENT,
};

enum via_channel_id {
    id_custom_channel         = 0,
    id_qmk_backlight_channel  = 1,
    id_qmk_rgblight_channel   = 2,
    id_qmk_rgb_matrix_channel = 3,
    id_qmk_audio_channel      = 4,
    id_qmk_led_matrix_channel = 5,
};

enum era_channel_id {
    id_qmk_version            = ERA_WIRE_CHANNELS_H7S_VERSION,
    id_qmk_system             = ERA_WIRE_CHANNELS_H7S_SYSTEM,
    id_qmk_kill_switch_lr     = ERA_WIRE_CHANNELS_H7S_SOCD_LR,
    id_qmk_kill_switch_ud     = ERA_WIRE_CHANNELS_H7S_SOCD_UD,
    id_qmk_kkuk               = ERA_WIRE_CHANNELS_H7S_KKUK,
    id_qmk_usb_polling        = ERA_WIRE_CHANNELS_H7S_USB_POLLING,
    id_qmk_key_response       = ERA_WIRE_CHANNELS_H7S_DEBOUNCE,
    id_qmk_tapping            = ERA_WIRE_CHANNELS_H7S_TAPPING,
    id_qmk_tapdance           = ERA_WIRE_CHANNELS_H7S_TAPDANCE,
    id_qmk_mousekey           = ERA_WIRE_CHANNELS_H7S_MOUSEKEY,
    id_qmk_rgb_sleep          = ERA_WIRE_CHANNELS_H7S_RGB_SLEEP,
};

#ifdef __cplusplus
#    define ERA_WIRE_ASSERT(condition, message) static_assert(condition, message)
#else
#    define ERA_WIRE_ASSERT(condition, message) _Static_assert(condition, message)
#endif
#define ERA_WIRE_ASSERT_OFFICIAL(name, value) ERA_WIRE_ASSERT(name == value, #name " differs from the ERA wire registry");
ERA_WIRE_VIA_OFFICIAL_COMMANDS(ERA_WIRE_ASSERT_OFFICIAL)
ERA_WIRE_VIA_OFFICIAL_KEYBOARD_VALUES(ERA_WIRE_ASSERT_OFFICIAL)
ERA_WIRE_VIA_OFFICIAL_CHANNELS(ERA_WIRE_ASSERT_OFFICIAL)
ERA_WIRE_ASSERT(VIA_PROTOCOL_VERSION == ERA_WIRE_VIA_PROTOCOL_VERSION, "VIA protocol version differs from the registry");
ERA_WIRE_ASSERT(QMK_KEYCODES_VERSION_BCD == ERA_WIRE_VIA_KEYCODES_VERSION, "VIA keycodes version differs from the registry");
#undef ERA_WIRE_ASSERT_OFFICIAL
#undef ERA_WIRE_ASSERT

enum via_qmk_backlight_value {
    id_qmk_backlight_brightness = 1,
    id_qmk_backlight_effect     = 2,
};

enum via_qmk_rgblight_value {
    id_qmk_rgblight_brightness   = 1,
    id_qmk_rgblight_effect       = 2,
    id_qmk_rgblight_effect_speed = 3,
    id_qmk_rgblight_color        = 4,
    id_qmk_velocikey_toggle      = ERA_WIRE_RGBLIGHT_H7S_VELOCIKEY,
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
    id_qmk_usb_bootmode_select = ERA_WIRE_USB_POLLING_H7S_BOOTMODE_SELECT,
    id_qmk_usb_bootmode_apply  = ERA_WIRE_USB_POLLING_H7S_BOOTMODE_APPLY,
    id_qmk_usb_polling_current = ERA_WIRE_USB_POLLING_H7S_CURRENT,
};

// V260901R1: 소비자 없는 STATUS를 제거하고 KEY RESPONSE value ID를 설정값 1~4로 한정
enum via_qmk_key_response_value {
    id_qmk_debounce_mode        = ERA_WIRE_DEBOUNCE_MODE,
    id_qmk_debounce_time_single = ERA_WIRE_DEBOUNCE_TIME_SINGLE,
    id_qmk_debounce_time_pre    = ERA_WIRE_DEBOUNCE_TIME_PRE,
    id_qmk_debounce_time_post   = ERA_WIRE_DEBOUNCE_TIME_POST,
};

// V251123R4: VIA TAPPING 설정 value ID 매핑
enum via_qmk_tapping_value {
    id_qmk_tapping_global_term             = ERA_WIRE_TAPPING_GLOBAL_TERM,
    id_qmk_tapping_permissive_hold         = ERA_WIRE_TAPPING_PERMISSIVE_HOLD,
    id_qmk_tapping_hold_on_other_key_press = ERA_WIRE_TAPPING_HOLD_ON_OTHER_KEY_PRESS,
    id_qmk_tapping_retro_tapping           = ERA_WIRE_TAPPING_RETRO_TAPPING,
    id_qmk_tapping_global_term_exact       = ERA_WIRE_TAPPING_GLOBAL_TERM_EXACT,
};

// V251124R8: VIA TAPDANCE value ID 매핑
enum via_qmk_tapdance_value {
    id_qmk_tapdance_1_tap   = ERA_WIRE_TAPDANCE_H7S_TAP_1,
    id_qmk_tapdance_1_hold  = ERA_WIRE_TAPDANCE_H7S_HOLD_1,
    id_qmk_tapdance_1_dtap  = ERA_WIRE_TAPDANCE_H7S_DOUBLE_TAP_1,
    id_qmk_tapdance_1_thold = ERA_WIRE_TAPDANCE_H7S_TAP_HOLD_1,
    id_qmk_tapdance_1_term  = ERA_WIRE_TAPDANCE_H7S_TERM_1,

    id_qmk_tapdance_2_tap   = ERA_WIRE_TAPDANCE_H7S_TAP_2,
    id_qmk_tapdance_2_hold  = ERA_WIRE_TAPDANCE_H7S_HOLD_2,
    id_qmk_tapdance_2_dtap  = ERA_WIRE_TAPDANCE_H7S_DOUBLE_TAP_2,
    id_qmk_tapdance_2_thold = ERA_WIRE_TAPDANCE_H7S_TAP_HOLD_2,
    id_qmk_tapdance_2_term  = ERA_WIRE_TAPDANCE_H7S_TERM_2,

    id_qmk_tapdance_3_tap   = ERA_WIRE_TAPDANCE_H7S_TAP_3,
    id_qmk_tapdance_3_hold  = ERA_WIRE_TAPDANCE_H7S_HOLD_3,
    id_qmk_tapdance_3_dtap  = ERA_WIRE_TAPDANCE_H7S_DOUBLE_TAP_3,
    id_qmk_tapdance_3_thold = ERA_WIRE_TAPDANCE_H7S_TAP_HOLD_3,
    id_qmk_tapdance_3_term  = ERA_WIRE_TAPDANCE_H7S_TERM_3,

    id_qmk_tapdance_4_tap   = ERA_WIRE_TAPDANCE_H7S_TAP_4,
    id_qmk_tapdance_4_hold  = ERA_WIRE_TAPDANCE_H7S_HOLD_4,
    id_qmk_tapdance_4_dtap  = ERA_WIRE_TAPDANCE_H7S_DOUBLE_TAP_4,
    id_qmk_tapdance_4_thold = ERA_WIRE_TAPDANCE_H7S_TAP_HOLD_4,
    id_qmk_tapdance_4_term  = ERA_WIRE_TAPDANCE_H7S_TERM_4,

    id_qmk_tapdance_5_tap   = ERA_WIRE_TAPDANCE_H7S_TAP_5,
    id_qmk_tapdance_5_hold  = ERA_WIRE_TAPDANCE_H7S_HOLD_5,
    id_qmk_tapdance_5_dtap  = ERA_WIRE_TAPDANCE_H7S_DOUBLE_TAP_5,
    id_qmk_tapdance_5_thold = ERA_WIRE_TAPDANCE_H7S_TAP_HOLD_5,
    id_qmk_tapdance_5_term  = ERA_WIRE_TAPDANCE_H7S_TERM_5,

    id_qmk_tapdance_6_tap   = ERA_WIRE_TAPDANCE_H7S_TAP_6,
    id_qmk_tapdance_6_hold  = ERA_WIRE_TAPDANCE_H7S_HOLD_6,
    id_qmk_tapdance_6_dtap  = ERA_WIRE_TAPDANCE_H7S_DOUBLE_TAP_6,
    id_qmk_tapdance_6_thold = ERA_WIRE_TAPDANCE_H7S_TAP_HOLD_6,
    id_qmk_tapdance_6_term  = ERA_WIRE_TAPDANCE_H7S_TERM_6,

    id_qmk_tapdance_7_tap   = ERA_WIRE_TAPDANCE_H7S_TAP_7,
    id_qmk_tapdance_7_hold  = ERA_WIRE_TAPDANCE_H7S_HOLD_7,
    id_qmk_tapdance_7_dtap  = ERA_WIRE_TAPDANCE_H7S_DOUBLE_TAP_7,
    id_qmk_tapdance_7_thold = ERA_WIRE_TAPDANCE_H7S_TAP_HOLD_7,
    id_qmk_tapdance_7_term  = ERA_WIRE_TAPDANCE_H7S_TERM_7,

    id_qmk_tapdance_8_tap   = ERA_WIRE_TAPDANCE_H7S_TAP_8,
    id_qmk_tapdance_8_hold  = ERA_WIRE_TAPDANCE_H7S_HOLD_8,
    id_qmk_tapdance_8_dtap  = ERA_WIRE_TAPDANCE_H7S_DOUBLE_TAP_8,
    id_qmk_tapdance_8_thold = ERA_WIRE_TAPDANCE_H7S_TAP_HOLD_8,
    id_qmk_tapdance_8_term  = ERA_WIRE_TAPDANCE_H7S_TERM_8,

    id_qmk_tapdance_1_term_exact = ERA_WIRE_TAPDANCE_H7S_TERM_EXACT_1,
    id_qmk_tapdance_2_term_exact = ERA_WIRE_TAPDANCE_H7S_TERM_EXACT_2,
    id_qmk_tapdance_3_term_exact = ERA_WIRE_TAPDANCE_H7S_TERM_EXACT_3,
    id_qmk_tapdance_4_term_exact = ERA_WIRE_TAPDANCE_H7S_TERM_EXACT_4,
    id_qmk_tapdance_5_term_exact = ERA_WIRE_TAPDANCE_H7S_TERM_EXACT_5,
    id_qmk_tapdance_6_term_exact = ERA_WIRE_TAPDANCE_H7S_TERM_EXACT_6,
    id_qmk_tapdance_7_term_exact = ERA_WIRE_TAPDANCE_H7S_TERM_EXACT_7,
    id_qmk_tapdance_8_term_exact = ERA_WIRE_TAPDANCE_H7S_TERM_EXACT_8,
    id_qmk_tapdance_1_mode = ERA_WIRE_TAPDANCE_H7S_MODE_1,
    id_qmk_tapdance_2_mode = ERA_WIRE_TAPDANCE_H7S_MODE_2,
    id_qmk_tapdance_3_mode = ERA_WIRE_TAPDANCE_H7S_MODE_3,
    id_qmk_tapdance_4_mode = ERA_WIRE_TAPDANCE_H7S_MODE_4,
    id_qmk_tapdance_5_mode = ERA_WIRE_TAPDANCE_H7S_MODE_5,
    id_qmk_tapdance_6_mode = ERA_WIRE_TAPDANCE_H7S_MODE_6,
    id_qmk_tapdance_7_mode = ERA_WIRE_TAPDANCE_H7S_MODE_7,
    id_qmk_tapdance_8_mode = ERA_WIRE_TAPDANCE_H7S_MODE_8,
    id_qmk_tapdance_1_hold_term = ERA_WIRE_TAPDANCE_H7S_HOLD_TERM_1,
    id_qmk_tapdance_2_hold_term = ERA_WIRE_TAPDANCE_H7S_HOLD_TERM_2,
    id_qmk_tapdance_3_hold_term = ERA_WIRE_TAPDANCE_H7S_HOLD_TERM_3,
    id_qmk_tapdance_4_hold_term = ERA_WIRE_TAPDANCE_H7S_HOLD_TERM_4,
    id_qmk_tapdance_5_hold_term = ERA_WIRE_TAPDANCE_H7S_HOLD_TERM_5,
    id_qmk_tapdance_6_hold_term = ERA_WIRE_TAPDANCE_H7S_HOLD_TERM_6,
    id_qmk_tapdance_7_hold_term = ERA_WIRE_TAPDANCE_H7S_HOLD_TERM_7,
    id_qmk_tapdance_8_hold_term = ERA_WIRE_TAPDANCE_H7S_HOLD_TERM_8,
    id_qmk_tapdance_1_hold_other = ERA_WIRE_TAPDANCE_H7S_HOLD_OTHER_1,
    id_qmk_tapdance_2_hold_other = ERA_WIRE_TAPDANCE_H7S_HOLD_OTHER_2,
    id_qmk_tapdance_3_hold_other = ERA_WIRE_TAPDANCE_H7S_HOLD_OTHER_3,
    id_qmk_tapdance_4_hold_other = ERA_WIRE_TAPDANCE_H7S_HOLD_OTHER_4,
    id_qmk_tapdance_5_hold_other = ERA_WIRE_TAPDANCE_H7S_HOLD_OTHER_5,
    id_qmk_tapdance_6_hold_other = ERA_WIRE_TAPDANCE_H7S_HOLD_OTHER_6,
    id_qmk_tapdance_7_hold_other = ERA_WIRE_TAPDANCE_H7S_HOLD_OTHER_7,
    id_qmk_tapdance_8_hold_other = ERA_WIRE_TAPDANCE_H7S_HOLD_OTHER_8,


};

// V260823R1: VIA MOUSE 제어 값 ID (참조 QMK era_mousekey_via.c와 동일한 1..6 배치)
enum via_qmk_mousekey_value {
    id_qmk_mousekey_cursor_min_speed    = ERA_WIRE_MOUSEKEY_CURSOR_MIN_SPEED,
    id_qmk_mousekey_cursor_max_speed    = ERA_WIRE_MOUSEKEY_CURSOR_MAX_SPEED,
    id_qmk_mousekey_cursor_acceleration = ERA_WIRE_MOUSEKEY_CURSOR_ACCELERATION,
    id_qmk_mousekey_cursor_interval     = ERA_WIRE_MOUSEKEY_CURSOR_INTERVAL,
    id_qmk_mousekey_wheel_interval      = ERA_WIRE_MOUSEKEY_WHEEL_INTERVAL,
    id_qmk_mousekey_wheel_acceleration  = ERA_WIRE_MOUSEKEY_WHEEL_ACCELERATION,
};

// V260901R1: VIA RGB SLEEP. 공식 JSON은 value 1 분 프리셋을 유지하고,
//            Custom VIA는 additive value 2의 BE16 exact seconds를 사용한다. 저장은 공통 uint16 초.
enum via_qmk_rgb_sleep_value {
    id_qmk_rgb_sleep_timeout       = ERA_WIRE_RGB_SLEEP_H7S_TIMEOUT,
    id_qmk_rgb_sleep_timeout_exact = ERA_WIRE_RGB_SLEEP_H7S_TIMEOUT_EXACT,
    id_qmk_rgb_sleep_enable        = ERA_WIRE_RGB_SLEEP_H7S_ENABLE,
};

// V251012R2: 커스텀 인디케이터 제어 값 ID
enum via_qmk_custom_indicator_value {
    id_qmk_custom_ind_selec      = ERA_WIRE_INDICATORS_H7S_INDICATOR_1_SELECT,
    id_qmk_custom_ind_brightness = ERA_WIRE_INDICATORS_H7S_INDICATOR_1_BRIGHTNESS,
    id_qmk_custom_ind_color      = ERA_WIRE_INDICATORS_H7S_INDICATOR_1_COLOR,
    id_qmk_custom_ind_2_selec      = ERA_WIRE_INDICATORS_H7S_INDICATOR_2_SELECT,
    id_qmk_custom_ind_2_brightness = ERA_WIRE_INDICATORS_H7S_INDICATOR_2_BRIGHTNESS,
    id_qmk_custom_ind_2_color      = ERA_WIRE_INDICATORS_H7S_INDICATOR_2_COLOR,
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
// Called only for an actual EEPROM GET-visible byte change.
void     via_eeprom_note_change(uint32_t address, uint32_t length);
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
