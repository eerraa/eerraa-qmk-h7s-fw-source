/* Copyright 2016 Jack Humbert
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

#include <stdint.h>
#include <stdbool.h>
#include "action.h"
#include "action_owner.h"
#include "quantum_keycodes.h"

#ifdef TAPDANCE_ENABLE
#    define TAP_DANCE_MATRIX_STATES (MATRIX_ROWS * MATRIX_COLS)
#endif
#ifndef TAP_DANCE_MAX_SIMULTANEOUS
#    define TAP_DANCE_MAX_SIMULTANEOUS 3
#endif

#ifdef TAPDANCE_ENABLE
/* TD momentary action/layer policy stays separate from shared HID ownership. */
uint8_t tap_dance_action_scope(action_t action);
void tap_dance_clear_owner_layers(uint8_t owner);
#endif

typedef struct {
    keypos_t key;
    keyevent_type_t type;
    uint32_t last_tap_time;
#ifdef TAPDANCE_ENABLE
    uint32_t input_epoch;
#endif
    uint8_t runtime_index;
    uint16_t interrupting_keycode;
    uint8_t  count;
    uint8_t  weak_mods;
#ifndef NO_ACTION_ONESHOT
    uint8_t oneshot_mods;
#endif
    bool    pressed : 1;
    bool    finished : 1;
    bool    interrupted : 1;
    bool    in_use : 1;
    bool    cancelled : 1;
    uint8_t index;
} tap_dance_state_t;

typedef void (*tap_dance_user_fn_t)(tap_dance_state_t *state, void *user_data);

typedef struct tap_dance_action_t {
    struct {
        tap_dance_user_fn_t on_each_tap;
        tap_dance_user_fn_t on_dance_finished;
        tap_dance_user_fn_t on_reset;
        tap_dance_user_fn_t on_each_release;
    } fn;
    void *user_data;
} tap_dance_action_t;

typedef struct {
    uint16_t kc1;
    uint16_t kc2;
} tap_dance_pair_t;

typedef struct {
    uint16_t kc;
    uint8_t  layer;
    void (*layer_function)(uint8_t);
} tap_dance_dual_role_t;

#define ACTION_TAP_DANCE_DOUBLE(kc1, kc2)                                                               \
    {                                                                                                   \
        .fn        = {tap_dance_pair_on_each_tap, tap_dance_pair_finished, tap_dance_pair_reset, NULL}, \
        .user_data = (void *)&((tap_dance_pair_t){kc1, kc2}),                                           \
    }

#define ACTION_TAP_DANCE_LAYER_MOVE(kc, layer)                                                                         \
    {                                                                                                                  \
        .fn        = {tap_dance_dual_role_on_each_tap, tap_dance_dual_role_finished, tap_dance_dual_role_reset, NULL}, \
        .user_data = (void *)&((tap_dance_dual_role_t){kc, layer, layer_move}),                                        \
    }

#define ACTION_TAP_DANCE_LAYER_TOGGLE(kc, layer)                                            \
    {                                                                                       \
        .fn        = {NULL, tap_dance_dual_role_finished, tap_dance_dual_role_reset, NULL}, \
        .user_data = (void *)&((tap_dance_dual_role_t){kc, layer, layer_invert}),           \
    }

#define ACTION_TAP_DANCE_FN(user_fn)              \
    {                                             \
        .fn        = {NULL, user_fn, NULL, NULL}, \
        .user_data = NULL,                        \
    }

#define ACTION_TAP_DANCE_FN_ADVANCED(user_fn_on_each_tap, user_fn_on_dance_finished, user_fn_on_dance_reset) \
    {                                                                                                        \
        .fn        = {user_fn_on_each_tap, user_fn_on_dance_finished, user_fn_on_dance_reset, NULL},         \
        .user_data = NULL,                                                                                   \
    }

#define ACTION_TAP_DANCE_FN_ADVANCED_WITH_RELEASE(user_fn_on_each_tap, user_fn_on_each_release, user_fn_on_dance_finished, user_fn_on_dance_reset) \
    {                                                                                                                                              \
        .fn        = {user_fn_on_each_tap, user_fn_on_dance_finished, user_fn_on_dance_reset, user_fn_on_each_release},                            \
        .user_data = NULL,                                                                                                                         \
    }

#define TD_INDEX(code) QK_TAP_DANCE_GET_INDEX(code)
#define TAP_DANCE_KEYCODE(state) TD((state)->index)

extern tap_dance_action_t tap_dance_actions[];

void reset_tap_dance(tap_dance_state_t *state);
void tap_dance_cancel_all(void);
#ifdef TAPDANCE_ENABLE
uint32_t tap_dance_input_epoch(void);
bool tap_dance_discard_retired_record(keyrecord_t *record);
void tap_dance_run_quantum_keycode(keyrecord_t *record, uint16_t keycode);
#endif

tap_dance_state_t *tap_dance_get_state(uint8_t tap_dance_idx);
uint16_t tap_dance_remap_keycode(uint16_t keycode);
uint16_t tap_dance_owned_keycode(const keyrecord_t *record);
bool tap_dance_owns_press(const keyrecord_t *record);
uint16_t tap_dance_get_tapping_term(uint16_t keycode, keyrecord_t *record);
uint16_t tap_dance_get_decision_term(const tap_dance_state_t *state);
bool tap_dance_hold_on_interrupt(const tap_dance_state_t *state);

/* To be used internally */

bool preprocess_tap_dance(uint16_t keycode, keyrecord_t *record);
bool process_tap_dance(uint16_t keycode, keyrecord_t *record);
void tap_dance_task(void);

void tap_dance_pair_on_each_tap(tap_dance_state_t *state, void *user_data);
void tap_dance_pair_finished(tap_dance_state_t *state, void *user_data);
void tap_dance_pair_reset(tap_dance_state_t *state, void *user_data);

void tap_dance_dual_role_on_each_tap(tap_dance_state_t *state, void *user_data);
void tap_dance_dual_role_finished(tap_dance_state_t *state, void *user_data);
void tap_dance_dual_role_reset(tap_dance_state_t *state, void *user_data);
