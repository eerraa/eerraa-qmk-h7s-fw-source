/*
Copyright 2012,2013 Jun Wako <wakojun@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "progmem.h"
#include "keyboard.h"
#include "keycode.h"
#include "action_code.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TAP_CODE_DELAY
#    define TAP_CODE_DELAY 0
#endif
// V260911R3: macOS 호환 기본값을 유지하고, keyboard tap 유지 시간은 USB 큐가 처리한다.
#ifndef TAP_HOLD_CAPS_DELAY
#    define TAP_HOLD_CAPS_DELAY 80
#endif

/* tapping count and state */
typedef struct {
    bool    interrupted : 1;
    bool    reserved2 : 1;
    bool    reserved1 : 1;
    bool    reserved0 : 1;
    uint8_t count : 4;
} tap_t;

/* Key event container for recording */
typedef struct {
    keyevent_t event;
    /* Only direct matrix ingress may opt in to same-scan report construction. */
    uint32_t report_scan_token;
#ifdef TAPDANCE_ENABLE
    /* The event already owns its native 32-bit time; only cancellation and
     * ingress identity need extra metadata while LT/MT holds the record. */
    uint32_t tap_dance_epoch;
    uint16_t tap_dance_keycode;
    bool tap_dance_epoch_valid;
    /* A dance's own quantum keycode, named by tap_dance_keycode: its output,
     * never an input of any dance. */
    bool tap_dance_injected;
#endif
#ifndef NO_ACTION_TAPPING
    tap_t tap;
#endif
#if defined(COMBO_ENABLE) || defined(REPEAT_KEY_ENABLE)
    uint16_t keycode;
#endif
    /* Runtime press identity and its resolved mapping survive deferred replay. */
    uint16_t press_generation;
    uint16_t resolved_keycode;
    bool resolved_keycode_valid;
} keyrecord_t;

/* Execute action per keyevent */
void action_exec(keyevent_t event);
void action_exec_physical(keyevent_t event, uint32_t scan_token);

/* action for key */
action_t action_for_key(uint8_t layer, keypos_t key);
action_t action_for_keycode(uint16_t keycode);

/* keyboard-specific key event (pre)processing */
bool process_record_quantum(keyrecord_t *record);

/* Utilities for actions.  */
#if !defined(NO_ACTION_LAYER) && !defined(STRICT_LAYER_RELEASE)
extern bool disable_action_cache;
#endif

/* Code for handling one-handed key modifiers. */
#ifdef SWAP_HANDS_ENABLE
extern bool                   swap_hands;
extern const keypos_t PROGMEM hand_swap_config[MATRIX_ROWS][MATRIX_COLS];
#    if (MATRIX_COLS <= 8)
typedef uint8_t swap_state_row_t;
#    elif (MATRIX_COLS <= 16)
typedef uint16_t swap_state_row_t;
#    elif (MATRIX_COLS <= 32)
typedef uint32_t swap_state_row_t;
#    else
#        error "MATRIX_COLS: invalid value"
#    endif

/**
 * @brief Enable swap hands
 */
void swap_hands_on(void);
/**
 * @brief Disable swap hands
 */
void swap_hands_off(void);
/**
 * @brief Toggle swap hands enable state
 */
void swap_hands_toggle(void);
/**
 * @brief Get the swap hands enable state
 *
 * @return true
 * @return false
 */
bool is_swap_hands_on(void);

void process_hand_swap(keyevent_t *record);
#endif

void process_record_nocache(keyrecord_t *record);
void process_record(keyrecord_t *record);
void process_record_handler(keyrecord_t *record);
void post_process_record_quantum(keyrecord_t *record);
void process_action(keyrecord_t *record, action_t action);
void register_mouse(uint8_t mouse_keycode, bool pressed);
void register_code(uint8_t code);
void unregister_code(uint8_t code);
void tap_code(uint8_t code);
void tap_code_delay(uint8_t code, uint16_t delay);
// V260911R3: keyboard tap의 최소 리포트 간격. 논리 키 해제나 메인 루프를 지연하지 않는다.
void tap_code_wait(uint16_t code, uint16_t delay);
void register_mods(uint8_t mods);
void unregister_mods(uint8_t mods);
void register_weak_mods(uint8_t mods);
void unregister_weak_mods(uint8_t mods);
// void set_mods(uint8_t mods);
void clear_keyboard(void);
void clear_keyboard_but_mods(void);
void clear_keyboard_but_mods_and_keys(void);
void layer_switch(uint8_t new_layer);
bool is_tap_record(keyrecord_t *record);
bool is_tap_action(action_t action);

#ifndef NO_ACTION_TAPPING
void process_record_tap_hint(keyrecord_t *record);
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers

#ifdef ACTION_DEBUG
#    include "debug.h"
#    include "print.h"
#    define ac_dprintf(...) dprintf(__VA_ARGS__)
#else
#    define ac_dprintf(...) \
        do {                \
        } while (0)
#endif

void debug_event(keyevent_t event);
void debug_record(keyrecord_t record);
void debug_action(action_t action);

#ifdef __cplusplus
}
#endif
