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

#include "process_tap_dance.h"
#include "quantum.h"
#include "action_layer.h"
#include "action_tapping.h"
#include "action_util.h"
#include "timer.h"
#include "wait.h"
#include "tapdance.h"

#ifdef TAPDANCE_ENABLE
uint8_t tap_dance_action_scope(action_t action) {
    const uint8_t previous = action_owner_current();
    if (previous >= ACTION_OWNER_TAP_DANCE_COUNT) return previous;
    bool momentary = action.kind.id == ACT_LMODS || action.kind.id == ACT_RMODS || action.kind.id == ACT_LAYER_MODS;
    if (action.kind.id == ACT_LAYER_TAP || action.kind.id == ACT_LAYER_TAP_EXT)
        momentary = action.layer_tap.code == OP_ON_OFF || action.layer_tap.code < OP_TAP_TOGGLE;
    if (action.kind.id == ACT_LMODS_TAP || action.kind.id == ACT_RMODS_TAP)
        momentary = action.layer_tap.code != MODS_TAP_TOGGLE;
    if (action.kind.id == ACT_USAGE || action.kind.id == ACT_MOUSEKEY) momentary = true;
    if (!momentary) action_owner_select(ACTION_OWNER_REGULAR);
    return previous;
}
#endif

/* Only an undecided dance owns the interruption cursor. Finished actions
 * remain in the bounded pool until their own input is released. */
static tap_dance_state_t *active_td;
static tap_dance_state_t tap_dance_states[TAP_DANCE_MAX_SIMULTANEOUS];
_Static_assert(TAP_DANCE_MAX_SIMULTANEOUS > 0 && TAP_DANCE_MAX_SIMULTANEOUS <= UINT8_MAX,
               "Tap Dance owner indices must fit in uint8_t");
#ifdef TAPDANCE_ENABLE
#    define TD_AUX_START TAP_DANCE_MATRIX_STATES
#else
#    define TD_AUX_START 0
#endif
#ifdef TAPDANCE_ENABLE
static uint32_t input_epoch;
uint32_t tap_dance_input_epoch(void) { return input_epoch; }
/* The dance whose callback is running. Its own output can clear the
 * keyboard (a quantum keycode): every other dance retires at once, and this
 * one keeps running, as in Vial, so no state is reset under its callback. */
static tap_dance_state_t *callback_state;
#endif

static bool tap_dance_owns_event(const tap_dance_state_t *state, const keyrecord_t *record) {
    return state->in_use && state->type == record->event.type && KEYEQ(state->key, record->event.key);
}

static tap_dance_state_t *tap_dance_find_owner(const keyrecord_t *record) {
#ifdef TAPDANCE_ENABLE
    if (IS_KEYEVENT(record->event)) {
        if (record->event.key.row >= MATRIX_ROWS || record->event.key.col >= MATRIX_COLS) return NULL;
        const uint16_t i = record->event.key.row * MATRIX_COLS + record->event.key.col;
        if (i >= TAP_DANCE_MAX_SIMULTANEOUS) return NULL;
        return tap_dance_owns_event(&tap_dance_states[i], record) ? &tap_dance_states[i] : NULL;
    }
#endif
    for (uint8_t i = TD_AUX_START; i < TAP_DANCE_MAX_SIMULTANEOUS; ++i) {
        if (tap_dance_owns_event(&tap_dance_states[i], record)) return &tap_dance_states[i];
    }
    return NULL;
}

static uint32_t tap_dance_event_time(const keyrecord_t *record) {
    return record->event.time;
}

static uint16_t tap_dance_count(void) { return TAPDANCE_SLOT_COUNT; }
static tap_dance_action_t *tap_dance_get(uint8_t index) {
    return index < TAPDANCE_SLOT_COUNT ? &tap_dance_actions[index] : NULL;
}

uint16_t tap_dance_owned_keycode(const keyrecord_t *record) {
    if (record->event.pressed || !IS_EVENT(record->event)) return KC_NO;
    const tap_dance_state_t *state = tap_dance_find_owner(record);
    if (state && state->pressed
#ifdef TAPDANCE_ENABLE
        && (state->cancelled || !record->tap_dance_epoch_valid || (int32_t)(record->tap_dance_epoch - state->input_epoch) >= 0)
#endif
    ) return TD(state->index);
    return KC_NO;
}

bool tap_dance_owns_press(const keyrecord_t *record) {
    if (!record->event.pressed || !IS_EVENT(record->event)) return false;
    const tap_dance_state_t *state = tap_dance_find_owner(record);
    return state && state->pressed && !state->cancelled;
}

__attribute__((weak)) uint16_t tap_dance_remap_keycode(uint16_t keycode) {
    return keycode;
}

uint16_t tap_dance_get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    (void)record;
    return tapdance_get_term_ms(keycode);
}

bool tap_dance_finish_on_release(const tap_dance_action_t *action, const tap_dance_state_t *state) {
    (void)action;
    return tapdance_should_finish_immediate(state->index, state->count, state->runtime_index);
}

static tap_dance_state_t *tap_dance_get_or_allocate_state(uint8_t tap_dance_idx, const keyrecord_t *record, bool allocate) {
    if (tap_dance_idx >= tap_dance_count()) return NULL;
    tap_dance_state_t *state = tap_dance_find_owner(record);
    if (state) return state->index == tap_dance_idx ? state : NULL;
    if (!allocate) return NULL;

    uint16_t i = TD_AUX_START;
#ifdef TAPDANCE_ENABLE
    if (IS_KEYEVENT(record->event)) {
        if (record->event.key.row >= MATRIX_ROWS || record->event.key.col >= MATRIX_COLS) return NULL;
        i = record->event.key.row * MATRIX_COLS + record->event.key.col;
    } else
#endif
    {
        while (i < TAP_DANCE_MAX_SIMULTANEOUS && tap_dance_states[i].in_use) ++i;
    }
    if (i >= TAP_DANCE_MAX_SIMULTANEOUS || tap_dance_states[i].in_use) return NULL;
    state = &tap_dance_states[i];
    state->index = tap_dance_idx;
    state->key = record->event.key;
    state->type = record->event.type;
    state->runtime_index = (uint8_t)i;
    state->in_use = true;
#ifdef TAPDANCE_ENABLE
    state->input_epoch = record->tap_dance_epoch_valid ? record->tap_dance_epoch : input_epoch;
#endif
    return state;
}

tap_dance_state_t *tap_dance_get_state(uint8_t tap_dance_idx) {
    if (active_td && active_td->index == tap_dance_idx) {
        return active_td;
    }
    for (uint8_t i = 0; i < TAP_DANCE_MAX_SIMULTANEOUS; ++i) {
        if (tap_dance_states[i].in_use && tap_dance_states[i].index == tap_dance_idx) {
            return &tap_dance_states[i];
        }
    }
    return NULL;
}

void tap_dance_pair_on_each_tap(tap_dance_state_t *state, void *user_data) {
    tap_dance_pair_t *pair = (tap_dance_pair_t *)user_data;

    if (state->count == 2) {
        register_code16(pair->kc2);
        state->finished = true;
    }
}

void tap_dance_pair_finished(tap_dance_state_t *state, void *user_data) {
    tap_dance_pair_t *pair = (tap_dance_pair_t *)user_data;

    register_code16(pair->kc1);
}

void tap_dance_pair_reset(tap_dance_state_t *state, void *user_data) {
    tap_dance_pair_t *pair = (tap_dance_pair_t *)user_data;

    if (state->count == 1) {
        wait_ms(TAP_CODE_DELAY);
        unregister_code16(pair->kc1);
    } else if (state->count == 2) {
        unregister_code16(pair->kc2);
    }
}

void tap_dance_dual_role_on_each_tap(tap_dance_state_t *state, void *user_data) {
    tap_dance_dual_role_t *pair = (tap_dance_dual_role_t *)user_data;

    if (state->count == 2) {
        layer_move(pair->layer);
        state->finished = true;
    }
}

void tap_dance_dual_role_finished(tap_dance_state_t *state, void *user_data) {
    tap_dance_dual_role_t *pair = (tap_dance_dual_role_t *)user_data;

    if (state->count == 1) {
        register_code16(pair->kc);
    } else if (state->count == 2) {
        pair->layer_function(pair->layer);
    }
}

void tap_dance_dual_role_reset(tap_dance_state_t *state, void *user_data) {
    tap_dance_dual_role_t *pair = (tap_dance_dual_role_t *)user_data;

    if (state->count == 1) {
        wait_ms(TAP_CODE_DELAY);
        unregister_code16(pair->kc);
    }
}

static inline void _process_tap_dance_action_fn(tap_dance_state_t *state, void *user_data, tap_dance_user_fn_t fn) {
    if (fn) {
#ifdef TAPDANCE_ENABLE
        uint8_t previous = action_owner_select(state->runtime_index);
#endif
#ifdef TAPDANCE_ENABLE
        tap_dance_state_t *outer = callback_state;
        callback_state          = state;
#endif
        fn(state, user_data);
#ifdef TAPDANCE_ENABLE
        callback_state = outer;
#endif
#ifdef TAPDANCE_ENABLE
        action_owner_select(previous);
#endif
    }
}

static inline void process_tap_dance_action_on_each_tap(tap_dance_action_t *action, tap_dance_state_t *state) {
    state->count++;
    state->weak_mods = get_mods();
    state->weak_mods |= get_weak_mods();
#ifndef NO_ACTION_ONESHOT
    state->oneshot_mods = get_oneshot_mods();
#endif
    _process_tap_dance_action_fn(state, action->user_data, action->fn.on_each_tap);
}

static inline void process_tap_dance_action_on_each_release(tap_dance_action_t *action, tap_dance_state_t *state) {
    _process_tap_dance_action_fn(state, action->user_data, action->fn.on_each_release);
}

static inline void process_tap_dance_action_on_reset(tap_dance_action_t *action, tap_dance_state_t *state) {
#ifdef TAPDANCE_ENABLE
    uint8_t previous = action_owner_select(state->runtime_index);
#endif
    _process_tap_dance_action_fn(state, action->user_data, action->fn.on_reset);
    del_weak_mods(state->weak_mods);
#ifndef NO_ACTION_ONESHOT
    del_mods(state->oneshot_mods);
#endif
#ifdef TAPDANCE_ENABLE
    action_owner_clear_mods(state->runtime_index);
    action_owner_clear_keys(state->runtime_index);
    action_owner_clear_hid(state->runtime_index);
#ifndef NO_ACTION_LAYER
    tap_dance_clear_owner_layers(state->runtime_index);
#endif
    action_owner_select(previous);
#endif
    send_keyboard_report();
    // Clear the tap dance state and mark it as unused
    memset(state, 0, sizeof(tap_dance_state_t));
}

static inline void process_tap_dance_action_on_dance_finished(tap_dance_action_t *action, tap_dance_state_t *state) {
    if (!state->finished) {
        state->finished = true;
#ifdef TAPDANCE_ENABLE
        uint8_t previous = action_owner_select(state->runtime_index);
#endif
#ifdef TAPDANCE_ENABLE
        /* Tap-time modifiers are replayed for a tap, as in QMK. A hold keeps
         * only live owners, so a released modifier cannot linger on it. */
        if (!state->pressed || state->interrupted)
#endif
            add_weak_mods(state->weak_mods);
#ifndef NO_ACTION_ONESHOT
        add_mods(state->oneshot_mods);
#endif
        send_keyboard_report();
        _process_tap_dance_action_fn(state, action->user_data, action->fn.on_dance_finished);
#ifdef TAPDANCE_ENABLE
        action_owner_select(previous);
#endif
    }
    if (active_td == state) {
        active_td = NULL;
    }
    if (!state->pressed) {
        // There will not be a key release event, so reset now.
        process_tap_dance_action_on_reset(action, state);
    }
}

uint16_t tap_dance_get_decision_term(const tap_dance_state_t *state) {
    return tapdance_decision_term(state->index, state->runtime_index, state->pressed);
}

bool tap_dance_hold_on_interrupt(const tap_dance_state_t *state) {
    return tapdance_hold_on_interrupt(state->runtime_index, state->count, state->pressed);
}

static bool tap_dance_expired(const tap_dance_state_t *state, uint32_t now) {
    const uint32_t elapsed = now - state->last_tap_time;
    /* A replayed older event cannot advance a newer dance. The supported
     * uint16_t term is far shorter than the unambiguous 32-bit half-range. */
    return elapsed < 0x80000000UL && elapsed > tap_dance_get_decision_term(state);
}

bool preprocess_tap_dance(uint16_t keycode, keyrecord_t *record) {
    tap_dance_action_t *action;
    tap_dance_state_t  *state;

#ifdef TAPDANCE_ENABLE
    /* A dance's own output neither advances nor interrupts a dance. */
    if (record->tap_dance_injected) return false;
#endif
    /* Process the event's deadline before turning its held state into up.
     * A scan gap must not turn an already-expired hold into a release tap.
     * Queued short taps still use their original event times, not dispatch. */
    if (active_td && tap_dance_expired(active_td, tap_dance_event_time(record))) {
        process_tap_dance_action_on_dance_finished(tap_dance_get(active_td->index), active_td);
        return record->event.pressed;
    }
    if (!record->event.pressed) return false;

    if (!active_td || (keycode == TD(active_td->index) && tap_dance_owns_event(active_td, record))) return false;

    state  = active_td;
    action = tap_dance_get(state->index);
    if (state == NULL) {
        return false;
    }
    state->interrupted          = !tap_dance_hold_on_interrupt(state);
    state->interrupting_keycode = keycode;
    process_tap_dance_action_on_dance_finished(action, state);

    // Tap dance actions can leave some weak mods active (e.g., if the tap dance is mapped to a keycode with
    // modifiers), but these weak mods should not affect the keypress which interrupted the tap dance.
    clear_weak_mods();

    // Signal that a tap dance has been finished due to being interrupted,
    // therefore the keymap lookup for the currently processed event needs to
    // be repeated with the current layer state that might have been updated by
    // the finished tap dance.
    return true;
}

bool process_tap_dance(uint16_t keycode, keyrecord_t *record) {
    uint8_t             td_index;
    tap_dance_action_t *action;
    tap_dance_state_t  *state;

    switch (keycode) {
        case QK_TAP_DANCE ... QK_TAP_DANCE_MAX:
            td_index = QK_TAP_DANCE_GET_INDEX(keycode);
            if (td_index >= tap_dance_count()) {
                return false;
            }
            action = tap_dance_get(td_index);
            state  = tap_dance_get_or_allocate_state(td_index, record, record->event.pressed);
            if (state == NULL) {
                return false;
            }
            /* A repeated down is not an additional tap and an unmatched up
             * cannot reset another physical owner of the same configuration. */
            if (state->pressed == record->event.pressed) {
                return false;
            }
            if (state->cancelled) {
                if (!record->event.pressed) memset(state, 0, sizeof(*state));
                return false;
            }
            state->pressed = record->event.pressed;
            if (record->event.pressed) {
                state->last_tap_time = tap_dance_event_time(record);
                process_tap_dance_action_on_each_tap(action, state);
                active_td = state->finished || !state->in_use ? NULL : state;
            } else {
                if (!state->finished && tap_dance_finish_on_release(action, state)) {
                    process_tap_dance_action_on_dance_finished(action, state);
                }
                process_tap_dance_action_on_each_release(action, state);
                if (state->finished) {
                    if (active_td == state) {
                        active_td = NULL;
                    }
                    process_tap_dance_action_on_reset(action, state);
                }
            }

            break;
    }

    return true;
}

void tap_dance_task(void) {
    tap_dance_action_t *action;
    tap_dance_state_t  *state;

    if (!active_td || !tap_dance_expired(active_td, timer_read32())) return;

    state  = active_td;
    action = tap_dance_get(state->index);
    if (state != NULL && !state->interrupted) {
        process_tap_dance_action_on_dance_finished(action, state);
    }
}

void reset_tap_dance(tap_dance_state_t *state) {
    if (!state || !state->in_use) return;
    if (active_td == state) {
        active_td = NULL;
    }
    process_tap_dance_action_on_reset(tap_dance_get(state->index), state);
}

#ifdef TAPDANCE_ENABLE
bool tap_dance_discard_retired_record(keyrecord_t *record) {
    if (!record->tap_dance_epoch_valid) return false;
    tap_dance_state_t *owner = tap_dance_find_owner(record);
    if (record->event.pressed && owner && owner->cancelled) {
        /* A new physical press supersedes a cancelled position, including
         * one whose up was lost with an overflowed waiting buffer. */
        memset(owner, 0, sizeof(*owner));
        owner = NULL;
    }
    if (record->tap_dance_epoch == input_epoch) return false;

    /* Do not flush the LT/MT queue. Only the TD input that predates the
     * boundary is retired, and its up must not leak into a changed keymap. */
    if (!record->event.pressed) {
        /* An up is a TD up only if its down left a dance here; queue order
         * makes that dance the one this boundary cancelled. */
        if (!owner) return false;
        if (owner->cancelled) {
            memset(owner, 0, sizeof(*owner));
            return true;
        }
        /* An up older than the dance at its position belongs to an
         * earlier press. One that is not (a dance started after the
         * boundary inside the same dispatch) is that dance's own. */
        return (int32_t)(record->tap_dance_epoch - owner->input_epoch) < 0;
    }

    uint16_t keycode = record->tap_dance_keycode;
#    if !defined(NO_ACTION_LAYER) && !defined(STRICT_LAYER_RELEASE)
    /* The ingress identity only outlives a VIA rewrite of the entry it read.
     * An unchanged entry resolves on the current layer, like any queued key. */
    if (IS_QK_TAP_DANCE(keycode) && tap_dance_remap_keycode(keymap_key_to_keycode(read_source_layers_cache(record->event.key), record->event.key)) == keycode)
        keycode = KC_NO;
#    endif
    if (!IS_QK_TAP_DANCE(keycode)) keycode = tap_dance_remap_keycode(get_record_keycode(record, true));
    if (!IS_QK_TAP_DANCE(keycode)) return false;
    if (owner) return true; /* A newer owner at this position is never modified. */
    tap_dance_state_t *state = tap_dance_get_or_allocate_state(TD_INDEX(keycode), record, true);
    if (state) state->pressed = state->cancelled = true;
    return true;
}
#endif
void tap_dance_cancel_all(void) {
#ifdef TAPDANCE_ENABLE
    tap_dance_state_t *const exempt      = callback_state;
    tap_dance_state_t *const keep_active = exempt && active_td == exempt ? active_td : NULL;
    ++input_epoch;
#endif
    active_td = NULL;
    for (uint8_t i = 0; i < TAP_DANCE_MAX_SIMULTANEOUS; ++i) {
        tap_dance_state_t *state = &tap_dance_states[i];
        if (!state->in_use || state->cancelled) continue;
#ifdef TAPDANCE_ENABLE
        if (state == exempt) continue;
#endif
        tap_dance_state_t owner = *state;
        reset_tap_dance(state);
        if (owner.pressed) {
            state->key = owner.key;
            state->type = owner.type;
            state->index = owner.index;
            state->runtime_index = i;
#ifdef TAPDANCE_ENABLE
            state->input_epoch = owner.input_epoch;
#endif
            state->in_use = state->pressed = state->cancelled = true;
        }
    }
#ifdef TAPDANCE_ENABLE
    if (keep_active && keep_active->in_use) active_td = keep_active;
#endif
}

#ifdef TAPDANCE_ENABLE
/* A keycode without a QMK action -- a VIA macro, a lighting or other quantum
 * keycode -- runs through the quantum handlers as the dance's own record, as
 * Vial runs it. A dance never starts another dance from its output. */
void tap_dance_run_quantum_keycode(keyrecord_t *record, uint16_t keycode) {
    if (IS_QK_TAP_DANCE(tap_dance_remap_keycode(keycode))) return;
    record->tap_dance_keycode  = keycode;
    record->tap_dance_injected = true;
    process_record_quantum(record);
}
#endif
