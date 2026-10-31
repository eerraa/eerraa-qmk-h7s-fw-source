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
#include <limits.h>

#include "host.h"
#include "keycode.h"
#include "keyboard.h"
#include "mousekey.h"
#include "programmable_button.h"
#include "command.h"
#include "led.h"
#include "action_layer.h"
#include "action_tapping.h"
#include "action_util.h"
#include "action.h"
#include "wait.h"
#include "keycode_config.h"
#include "debug.h"
#include "quantum.h"

#ifdef BACKLIGHT_ENABLE
#    include "backlight.h"
#endif

#ifdef POINTING_DEVICE_ENABLE
#    include "pointing_device.h"
#endif

#if defined(ENCODER_ENABLE) && defined(ENCODER_MAP_ENABLE) && defined(SWAP_HANDS_ENABLE)
#    include "encoder.h"
#endif

int tp_buttons;

#if defined(RETRO_TAPPING) || defined(RETRO_TAPPING_PER_KEY) || (defined(AUTO_SHIFT_ENABLE) && defined(RETRO_SHIFT))
bool     retro_tap_primed   = false;
uint16_t retro_tap_curr_key = 0;
#    if !(defined(AUTO_SHIFT_ENABLE) && defined(RETRO_SHIFT))
uint8_t retro_tap_curr_mods = 0;
uint8_t retro_tap_next_mods = 0;
#    endif
#endif

#if defined(AUTO_SHIFT_ENABLE) && defined(RETRO_SHIFT) && !defined(NO_ACTION_TAPPING)
#    include "process_auto_shift.h"
#endif

#ifdef HOLD_ON_OTHER_KEY_PRESS_PER_KEY
__attribute__((weak)) bool get_hold_on_other_key_press(uint16_t keycode, keyrecord_t *record) {
    return false;
}
#endif

#ifdef RETRO_TAPPING_PER_KEY
__attribute__((weak)) bool get_retro_tapping(uint16_t keycode, keyrecord_t *record) {
    return false;
}
#endif

/** \brief Called to execute an action.
 *
 * FIXME: Needs documentation.
 */
static void action_exec_with_scan(keyevent_t event, uint32_t scan_token) {
    const uint32_t updates_before = host_keyboard_key_update_count();
    if (scan_token == 0U) host_keyboard_end_key_scan(0U);
#ifndef NO_ACTION_LAYER
    const layer_state_t layers_before = layer_state;
#endif
    const layer_state_t default_layers_before = default_layer_state;
    if (IS_EVENT(event)) {
        ac_dprintf("\n---- action_exec: start -----\n");
        ac_dprintf("EVENT: ");
        debug_event(event);
        ac_dprintf("\n");
#if defined(RETRO_TAPPING) || defined(RETRO_TAPPING_PER_KEY) || (defined(AUTO_SHIFT_ENABLE) && defined(RETRO_SHIFT))
        uint16_t event_keycode = get_event_keycode(event, false);
        if (event.pressed) {
            retro_tap_primed   = false;
            retro_tap_curr_key = event_keycode;
        } else if (retro_tap_curr_key == event_keycode) {
            retro_tap_primed = true;
        }
#endif
    }

    if (event.pressed) {
        // clear the potential weak mods left by previously pressed keys
        clear_weak_mods();
    }

#ifdef SWAP_HANDS_ENABLE
    // Swap hands handles both keys and encoders, if ENCODER_MAP_ENABLE is defined.
    if (IS_EVENT(event)) {
        process_hand_swap(&event);
    }
#endif

    keyrecord_t record = {.event = event, .report_scan_token = scan_token};
#ifdef TAPDANCE_ENABLE
    if (IS_EVENT(event)) {
        record.tap_dance_epoch = tap_dance_input_epoch();
        record.tap_dance_epoch_valid = true;
    }
#endif

#ifndef NO_ACTION_ONESHOT
    if (keymap_config.oneshot_enable) {
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
        if (has_oneshot_layer_timed_out()) {
            clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED);
        }
        if (has_oneshot_mods_timed_out()) {
            clear_oneshot_mods();
        }
#        ifdef SWAP_HANDS_ENABLE
        if (has_oneshot_swaphands_timed_out()) {
            clear_oneshot_swaphands();
        }
#        endif
#    endif
    }
#endif

#ifndef NO_ACTION_TAPPING
#    if defined(AUTO_SHIFT_ENABLE) && defined(RETRO_SHIFT)
    if (event.pressed) {
        retroshift_poll_time(&event);
    }
#    endif
    if (IS_NOEVENT(record.event) || pre_process_record_quantum(&record)) {
        action_tapping_process(record);
    }
#else
    if (IS_NOEVENT(record.event) || pre_process_record_quantum(&record)) {
        process_record(&record);
    }
    if (IS_EVENT(record.event)) {
        ac_dprintf("processed: ");
        debug_record(record);
        dprintln();
    }
#endif
    bool barrier = host_keyboard_key_update_count() == updates_before || default_layer_state != default_layers_before;
#ifndef NO_ACTION_LAYER
    barrier = barrier || layer_state != layers_before;
#endif
    if (scan_token != 0U && barrier) host_keyboard_end_key_scan(scan_token);
}

void action_exec(keyevent_t event) {
    action_exec_with_scan(event, 0U);
}

void action_exec_physical(keyevent_t event, uint32_t scan_token) {
    action_exec_with_scan(event, IS_KEYEVENT(event) ? scan_token : 0U);
}

#ifdef SWAP_HANDS_ENABLE
extern const keypos_t PROGMEM hand_swap_config[MATRIX_ROWS][MATRIX_COLS];
#    ifdef ENCODER_MAP_ENABLE
extern const uint8_t PROGMEM encoder_hand_swap_config[NUM_ENCODERS];
#    endif // ENCODER_MAP_ENABLE

bool swap_hands = false;
bool swap_held  = false;

bool should_swap_hands(size_t index, uint8_t *swap_state, bool pressed) {
    size_t  array_index = index / (CHAR_BIT);
    size_t  bit_index   = index % (CHAR_BIT);
    uint8_t bit_val     = 1 << bit_index;
    bool    do_swap     = pressed ? swap_hands : swap_state[array_index] & bit_val;
    return do_swap;
}

void set_swap_hands_state(size_t index, uint8_t *swap_state, bool on) {
    size_t  array_index = index / (CHAR_BIT);
    size_t  bit_index   = index % (CHAR_BIT);
    uint8_t bit_val     = 1 << bit_index;
    if (on) {
        swap_state[array_index] |= bit_val;
    } else {
        swap_state[array_index] &= ~bit_val;
    }
}

void swap_hands_on(void) {
    swap_hands = true;
}

void swap_hands_off(void) {
    swap_hands = false;
}

void swap_hands_toggle(void) {
    swap_hands = !swap_hands;
}

bool is_swap_hands_on(void) {
    return swap_hands;
}

/** \brief Process Hand Swap
 *
 * FIXME: Needs documentation.
 */
void process_hand_swap(keyevent_t *event) {
    keypos_t pos = event->key;
    if (IS_KEYEVENT(*event) && pos.row < MATRIX_ROWS && pos.col < MATRIX_COLS) {
        static uint8_t matrix_swap_state[((MATRIX_ROWS * MATRIX_COLS) + (CHAR_BIT)-1) / (CHAR_BIT)];
        size_t         index   = (size_t)(pos.row * MATRIX_COLS) + pos.col;
        bool           do_swap = should_swap_hands(index, matrix_swap_state, event->pressed);
        if (do_swap) {
            event->key.row = pgm_read_byte(&hand_swap_config[pos.row][pos.col].row);
            event->key.col = pgm_read_byte(&hand_swap_config[pos.row][pos.col].col);
            set_swap_hands_state(index, matrix_swap_state, true);
        } else {
            set_swap_hands_state(index, matrix_swap_state, false);
        }
    }
#    ifdef ENCODER_MAP_ENABLE
    else if (IS_ENCODEREVENT(*event) && (pos.row == KEYLOC_ENCODER_CW || pos.row == KEYLOC_ENCODER_CCW)) {
        static uint8_t encoder_swap_state[((NUM_ENCODERS) + (CHAR_BIT)-1) / (CHAR_BIT)];
        size_t         index   = pos.col;
        bool           do_swap = should_swap_hands(index, encoder_swap_state, event->pressed);
        if (do_swap) {
            event->key.row = pos.row;
            event->key.col = pgm_read_byte(&encoder_hand_swap_config[pos.col]);
            set_swap_hands_state(index, encoder_swap_state, true);
        } else {
            set_swap_hands_state(index, encoder_swap_state, false);
        }
    }
#    endif // ENCODER_MAP_ENABLE
}
#endif

#if !defined(NO_ACTION_LAYER) && !defined(STRICT_LAYER_RELEASE)
bool disable_action_cache = false;

void process_record_nocache(keyrecord_t *record) {
    disable_action_cache = true;
    process_record(record);
    disable_action_cache = false;
}
#else
void process_record_nocache(keyrecord_t *record) {
    process_record(record);
}
#endif

__attribute__((weak)) bool process_record_quantum(keyrecord_t *record) {
    return true;
}

__attribute__((weak)) void post_process_record_quantum(keyrecord_t *record) {}

#ifndef NO_ACTION_TAPPING
/** \brief Allows for handling tap-hold actions immediately instead of waiting for TAPPING_TERM or another keypress.
 *
 * FIXME: Needs documentation.
 */
void process_record_tap_hint(keyrecord_t *record) {
    if (!IS_KEYEVENT(record->event)) {
        return;
    }

    action_t action = layer_switch_get_action(record->event.key);

    switch (action.kind.id) {
#    ifdef SWAP_HANDS_ENABLE
        case ACT_SWAP_HANDS:
            switch (action.swap.code) {
                case OP_SH_ONESHOT:
                    break;
                case OP_SH_TAP_TOGGLE:
                default:
                    swap_hands = !swap_hands;
                    swap_held  = true;
            }
            break;
#    endif
    }
}
#endif

/** \brief Take a key event (key press or key release) and processes it.
 *
 * FIXME: Needs documentation.
 */
void process_record(keyrecord_t *record) {
#ifdef TAPDANCE_ENABLE
    if (IS_EVENT(record->event) && tap_dance_discard_retired_record(record)) return;
#endif
    if (IS_NOEVENT(record->event)) {
        return;
    }
#ifdef TAPDANCE_ENABLE
    /* The dance releases its executed action. The up still passes QMK's
     * action path, but a live remap of this position must not supply it. */
    const bool td_owned_release = tap_dance_owned_keycode(record) != KC_NO;
#endif

    if (!process_record_quantum(record)) {
#ifndef NO_ACTION_ONESHOT
        if (is_oneshot_layer_active() && record->event.pressed && keymap_config.oneshot_enable) {
            clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED);
        }
#endif
        return;
    }

#ifdef TAPDANCE_ENABLE
    if (td_owned_release)
        process_action(record, (action_t){.code = ACTION_NO});
    else
#endif
        process_record_handler(record);
    post_process_record_quantum(record);
}

void process_record_handler(keyrecord_t *record) {
#if defined(COMBO_ENABLE) || defined(REPEAT_KEY_ENABLE)
    action_t action;
    if (record->keycode) {
        action = action_for_keycode(record->keycode);
    } else {
        action = store_or_get_action(record->event.pressed, record->event.key);
    }
#else
    action_t action = store_or_get_action(record->event.pressed, record->event.key);
#endif
    ac_dprintf("ACTION: ");
    debug_action(action);
#ifndef NO_ACTION_LAYER
    ac_dprintf(" layer_state: ");
    layer_debug();
    ac_dprintf(" default_layer_state: ");
    default_layer_debug();
#endif
    ac_dprintf("\n");

    process_action(record, action);
}

/**
 * @brief handles all the messy mouse stuff
 *
 * Handles all the edgecases and special stuff that is needed for coexistense
 * of the multiple mouse subsystems.
 *
 * @param mouse_keycode[in] uint8_t mouse keycode
 * @param pressed[in] bool
 */

void register_mouse(uint8_t mouse_keycode, bool pressed) {
#ifdef TAP_DANCE_OWNED_ACTIONS
    /* Another input still holding this mouse code keeps it down. */
    if (!tap_dance_mouse_update(mouse_keycode, pressed)) return;
#endif
#ifdef MOUSEKEY_ENABLE
    // if mousekeys is enabled, let it do the brunt of the work
    if (pressed) {
        mousekey_on(mouse_keycode);
    } else {
        mousekey_off(mouse_keycode);
    }
    // should mousekeys send report, or does something else handle this?
    switch (mouse_keycode) {
#    if defined(PS2_MOUSE_ENABLE) || defined(POINTING_DEVICE_ENABLE)
        case KC_MS_BTN1 ... KC_MS_BTN8:
            // let pointing device handle the buttons
            // expand if/when it handles more of the code
#        if defined(POINTING_DEVICE_ENABLE)
            pointing_device_keycode_handler(mouse_keycode, pressed);
#        endif
            break;
#    endif
        default:
            mousekey_send();
            break;
    }
#elif defined(POINTING_DEVICE_ENABLE)
    // if mousekeys isn't enabled, and pointing device is enabled, then
    // let pointing device do all the heavy lifting, then
    if (IS_MOUSE_KEYCODE(mouse_keycode)) {
        pointing_device_keycode_handler(mouse_keycode, pressed);
    }
#endif

#ifdef PS2_MOUSE_ENABLE
    // make sure that ps2 mouse has button report synced
    if (KC_MS_BTN1 <= mouse_keycode && mouse_keycode <= KC_MS_BTN3) {
        uint8_t tmp_button_msk = MOUSE_BTN_MASK(mouse_keycode - KC_MS_BTN1);
        tp_buttons             = pressed ? tp_buttons | tmp_button_msk : tp_buttons & ~tmp_button_msk;
    }
#endif
}

#ifdef EXTRAKEY_ENABLE
/* Each extra report holds one usage; ownership decides whether an up may clear it. */
static void send_system_usage(uint16_t usage, bool pressed) {
#    ifdef TAP_DANCE_OWNED_ACTIONS
    if (!tap_dance_usage_update(TD_USAGE_SYSTEM, usage, host_last_system_usage(), pressed)) return;
#    endif
    host_system_send(pressed ? usage : 0);
}

static void send_consumer_usage(uint16_t usage, bool pressed) {
#    ifdef TAP_DANCE_OWNED_ACTIONS
    if (!tap_dance_usage_update(TD_USAGE_CONSUMER, usage, host_last_consumer_usage(), pressed)) return;
#    endif
    host_consumer_send(pressed ? usage : 0);
}
#endif

/** \brief Take an action and processes it.
 *
 * FIXME: Needs documentation.
 */
static bool report_key_update_allowed(const keyrecord_t *record, action_t action) {
#ifdef COMBO_ENABLE
    // Failed combo candidates can replay a saved KEY_EVENT with its old scan token.
    return false;
#endif
    const uint8_t code = action.key.code;
    if (record->report_scan_token == 0U || !IS_KEYEVENT(record->event) ||
        record->event.key.row >= MATRIX_ROWS || record->event.key.col >= MATRIX_COLS ||
        (action.kind.id != ACT_LMODS && action.kind.id != ACT_RMODS) || action.key.mods != 0U ||
        !IS_BASIC_KEYCODE(code) || code == KC_CAPS_LOCK || code == KC_NUM_LOCK || code == KC_SCROLL_LOCK ||
        code == KC_LOCKING_CAPS_LOCK || code == KC_LOCKING_NUM_LOCK || code == KC_LOCKING_SCROLL_LOCK)
        return false;
#ifndef NO_ACTION_TAPPING
    if (record->tap.count != 0U) return false;
#endif
#ifdef TAPDANCE_ENABLE
    if (record->tap_dance_injected || tap_dance_owned_keycode(record) != KC_NO) return false;
#endif
#ifdef TAP_DANCE_OWNED_ACTIONS
    if (tap_dance_action_get_owner() != UINT8_MAX) return false;
#endif
    if (get_mods() != 0U || get_weak_mods() != 0U) return false;
#ifndef NO_ACTION_ONESHOT
    if (get_oneshot_mods() != 0U || get_oneshot_locked_mods() != 0U || is_oneshot_layer_active()) return false;
#endif
#ifdef KILL_SWITCH_ENABLE
    if (kill_switch_is_use(code)) return false;
#endif
    /* register_code emits a separate up/down pair for an already held usage. */
    return !record->event.pressed || !is_key_pressed(code);
}

void process_action(keyrecord_t *record, action_t action) {
#ifdef TAP_DANCE_OWNED_ACTIONS
    uint8_t td_previous_owner = tap_dance_action_get_owner();
    bool td_momentary = action.kind.id == ACT_LMODS || action.kind.id == ACT_RMODS || action.kind.id == ACT_LAYER_MODS;
    if (action.kind.id == ACT_LAYER_TAP || action.kind.id == ACT_LAYER_TAP_EXT)
        td_momentary = action.layer_tap.code == OP_ON_OFF || action.layer_tap.code < OP_TAP_TOGGLE;
    if (action.kind.id == ACT_LMODS_TAP || action.kind.id == ACT_RMODS_TAP)
        td_momentary = action.layer_tap.code != MODS_TAP_TOGGLE;
    if (action.kind.id == ACT_USAGE || action.kind.id == ACT_MOUSEKEY) td_momentary = true;
    if (!td_momentary) tap_dance_action_set_owner(UINT8_MAX);
#endif
    keyevent_t event = record->event;
#ifndef NO_ACTION_TAPPING
    uint8_t tap_count = record->tap.count;
#endif
    const bool key_update = report_key_update_allowed(record, action);
    if (!key_update) host_keyboard_end_key_scan(0U);

#ifndef NO_ACTION_ONESHOT
    bool do_release_oneshot = false;
    // notice we only clear the one shot layer if the pressed key is not a modifier.
    if (is_oneshot_layer_active() && event.pressed &&
        (action.kind.id == ACT_USAGE || !(IS_MODIFIER_KEYCODE(action.key.code)
#    ifndef NO_ACTION_TAPPING
                                          || ((action.kind.id == ACT_LMODS_TAP || action.kind.id == ACT_RMODS_TAP) && (action.layer_tap.code <= MODS_TAP_TOGGLE || tap_count == 0))
#    endif
                                              ))
#    ifdef SWAP_HANDS_ENABLE
        && !(action.kind.id == ACT_SWAP_HANDS && action.swap.code == OP_SH_ONESHOT)
#    endif
        && keymap_config.oneshot_enable) {
        clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED);
        do_release_oneshot = !is_oneshot_layer_active();
    }
#endif

    switch (action.kind.id) {
        /* Key and Mods */
        case ACT_LMODS:
        case ACT_RMODS: {
            uint8_t mods = (action.kind.id == ACT_LMODS) ? action.key.mods : action.key.mods << 4;
            if (event.pressed) {
                if (mods) {
                    if (IS_MODIFIER_KEYCODE(action.key.code) || action.key.code == KC_NO) {
                        // e.g. LSFT(KC_LEFT_GUI): we don't want the LSFT to be weak as it would make it useless.
                        // This also makes LSFT(KC_LEFT_GUI) behave exactly the same as LGUI(KC_LEFT_SHIFT).
                        // Same applies for some keys like KC_MEH which are declared as MEH(KC_NO).
                        add_mods(mods);
                    } else {
                        add_weak_mods(mods);
                    }
                    send_keyboard_report();
                }
                if (key_update) host_keyboard_begin_key_update(record->report_scan_token, action.key.code, true);
                register_code(action.key.code);
                if (key_update) host_keyboard_end_key_update();
            } else {
                if (key_update) host_keyboard_begin_key_update(record->report_scan_token, action.key.code, false);
                unregister_code(action.key.code);
                if (key_update) host_keyboard_end_key_update();
                if (mods) {
                    if (IS_MODIFIER_KEYCODE(action.key.code) || action.key.code == KC_NO) {
                        del_mods(mods);
                    } else {
                        del_weak_mods(mods);
                    }
                    send_keyboard_report();
                }
            }
        } break;
        case ACT_LMODS_TAP:
        case ACT_RMODS_TAP: {
#ifndef NO_ACTION_TAPPING
            uint8_t mods = (action.kind.id == ACT_LMODS_TAP) ? action.key.mods : action.key.mods << 4;
            switch (action.layer_tap.code) {
#    ifndef NO_ACTION_ONESHOT
                case MODS_ONESHOT:
                    // Oneshot modifier
                    if (!keymap_config.oneshot_enable) {
                        if (event.pressed) {
                            if (mods) {
                                if (IS_MODIFIER_KEYCODE(action.key.code) || action.key.code == KC_NO) {
                                    // e.g. LSFT(KC_LGUI): we don't want the LSFT to be weak as it would make it useless.
                                    // This also makes LSFT(KC_LGUI) behave exactly the same as LGUI(KC_LSFT).
                                    // Same applies for some keys like KC_MEH which are declared as MEH(KC_NO).
                                    add_mods(mods);
                                } else {
                                    add_weak_mods(mods);
                                }
                                send_keyboard_report();
                            }
                            register_code(action.key.code);
                        } else {
                            unregister_code(action.key.code);
                            if (mods) {
                                if (IS_MODIFIER_KEYCODE(action.key.code) || action.key.code == KC_NO) {
                                    del_mods(mods);
                                } else {
                                    del_weak_mods(mods);
                                }
                                send_keyboard_report();
                            }
                        }
                    } else {
                        if (event.pressed) {
                            if (tap_count == 0) {
                                // Not a tap, but a hold: register the held mod
                                ac_dprintf("MODS_TAP: Oneshot: 0\n");
                                register_mods(mods);
                            } else if (tap_count == 1) {
                                ac_dprintf("MODS_TAP: Oneshot: start\n");
                                add_oneshot_mods(mods);
#        if defined(ONESHOT_TAP_TOGGLE) && ONESHOT_TAP_TOGGLE > 1
                            } else if (tap_count == ONESHOT_TAP_TOGGLE) {
                                ac_dprintf("MODS_TAP: Toggling oneshot");
                                register_mods(mods);
                                del_oneshot_mods(mods);
                                add_oneshot_locked_mods(mods);
#        endif
                            }
                        } else {
                            if (tap_count == 0) {
                                // Release hold: unregister the held mod and its variants
                                unregister_mods(mods);
                                del_oneshot_mods(mods);
                                del_oneshot_locked_mods(mods);
#        if defined(ONESHOT_TAP_TOGGLE) && ONESHOT_TAP_TOGGLE > 1
                            } else if (tap_count == 1 && (mods & get_mods())) {
                                unregister_mods(mods);
                                del_oneshot_mods(mods);
                                del_oneshot_locked_mods(mods);
#        endif
                            }
                        }
                    }
                    break;
#    endif
                case MODS_TAP_TOGGLE:
                    if (event.pressed) {
                        if (tap_count <= TAPPING_TOGGLE) {
                            register_mods(mods);
                        }
                    } else {
                        if (tap_count < TAPPING_TOGGLE) {
                            unregister_mods(mods);
                        }
                    }
                    break;
                default:
                    if (event.pressed) {
                        if (tap_count > 0) {
#    ifdef HOLD_ON_OTHER_KEY_PRESS
                            if (
#        ifdef HOLD_ON_OTHER_KEY_PRESS_PER_KEY
                                get_hold_on_other_key_press(get_event_keycode(record->event, false), record) &&
#        endif
                                record->tap.interrupted) {
                                ac_dprintf("mods_tap: tap: cancel: add_mods\n");
                                // ad hoc: set 0 to cancel tap
                                record->tap.count = 0;
                                register_mods(mods);
                            } else
#    endif
                            {
                                ac_dprintf("MODS_TAP: Tap: register_code\n");
                                register_code(action.key.code);
                            }
                        } else {
                            ac_dprintf("MODS_TAP: No tap: add_mods\n");
                            register_mods(mods);
                        }
                    } else {
                        if (tap_count > 0) {
                            ac_dprintf("MODS_TAP: Tap: unregister_code\n");
                            if (action.layer_tap.code == KC_CAPS_LOCK) {
                                tap_code_wait(action.key.code, TAP_HOLD_CAPS_DELAY);
                            } else {
                                tap_code_wait(action.key.code, TAP_CODE_DELAY);
                            }
                            unregister_code(action.key.code);
                        } else {
                            ac_dprintf("MODS_TAP: No tap: add_mods\n");
#    if defined(RETRO_TAPPING) && defined(DUMMY_MOD_NEUTRALIZER_KEYCODE)
                            // Send a dummy keycode to neutralize flashing modifiers
                            // if the key was held and then released with no interruptions.
                            uint16_t ev_kc = get_event_keycode(event, false);
                            if (retro_tap_primed && retro_tap_curr_key == ev_kc) {
                                neutralize_flashing_modifiers(get_mods());
                            }
#    endif
                            unregister_mods(mods);
                        }
                    }
                    break;
            }
#endif // NO_ACTION_TAPPING
        } break;
#ifdef EXTRAKEY_ENABLE
        /* other HID usage */
        case ACT_USAGE:
            switch (action.usage.page) {
                case PAGE_SYSTEM:
                    send_system_usage(action.usage.code, event.pressed);
                    break;
                case PAGE_CONSUMER:
                    send_consumer_usage(action.usage.code, event.pressed);
                    break;
            }
            break;
#endif // EXTRAKEY_ENABLE
        /* Mouse key */
        case ACT_MOUSEKEY:
            register_mouse(action.key.code, event.pressed);
            break;
#ifndef NO_ACTION_LAYER
        case ACT_LAYER:
            if (action.layer_bitop.on == 0) {
                /* Default Layer Bitwise Operation */
                if (!event.pressed) {
                    uint8_t       shift = action.layer_bitop.part * 4;
                    layer_state_t bits  = ((layer_state_t)action.layer_bitop.bits) << shift;
                    layer_state_t mask  = (action.layer_bitop.xbit) ? ~(((layer_state_t)0xf) << shift) : 0;
                    switch (action.layer_bitop.op) {
                        case OP_BIT_AND:
                            default_layer_and(bits | mask);
                            break;
                        case OP_BIT_OR:
                            default_layer_or(bits | mask);
                            break;
                        case OP_BIT_XOR:
                            default_layer_xor(bits | mask);
                            break;
                        case OP_BIT_SET:
                            default_layer_set(bits | mask);
                            break;
                    }
                }
            } else {
                /* Layer Bitwise Operation */
                if (event.pressed ? (action.layer_bitop.on & ON_PRESS) : (action.layer_bitop.on & ON_RELEASE)) {
                    uint8_t       shift = action.layer_bitop.part * 4;
                    layer_state_t bits  = ((layer_state_t)action.layer_bitop.bits) << shift;
                    layer_state_t mask  = (action.layer_bitop.xbit) ? ~(((layer_state_t)0xf) << shift) : 0;
                    switch (action.layer_bitop.op) {
                        case OP_BIT_AND:
                            layer_and(bits | mask);
                            break;
                        case OP_BIT_OR:
                            layer_or(bits | mask);
                            break;
                        case OP_BIT_XOR:
                            layer_xor(bits | mask);
                            break;
                        case OP_BIT_SET:
                            layer_state_set(bits | mask);
                            break;
                    }
                }
            }
            break;
        case ACT_LAYER_MODS:
            if (event.pressed) {
                layer_on(action.layer_mods.layer);
                register_mods(action.layer_mods.mods);
            } else {
                unregister_mods(action.layer_mods.mods);
                layer_off(action.layer_mods.layer);
            }
            break;
        case ACT_LAYER_TAP:
        case ACT_LAYER_TAP_EXT:
            switch (action.layer_tap.code) {
#    ifndef NO_ACTION_TAPPING
                case OP_TAP_TOGGLE:
                    /* tap toggle */
                    if (event.pressed) {
                        if (tap_count < TAPPING_TOGGLE) {
                            layer_invert(action.layer_tap.val);
                        }
                    } else {
                        if (tap_count <= TAPPING_TOGGLE) {
                            layer_invert(action.layer_tap.val);
                        }
                    }
                    break;
#    endif
                case OP_ON_OFF:
                    event.pressed ? layer_on(action.layer_tap.val) : layer_off(action.layer_tap.val);
                    break;
                case OP_OFF_ON:
                    event.pressed ? layer_off(action.layer_tap.val) : layer_on(action.layer_tap.val);
                    break;
                case OP_SET_CLEAR:
                    event.pressed ? layer_move(action.layer_tap.val) : layer_clear();
                    break;
#    if !defined(NO_ACTION_ONESHOT) && !defined(NO_ACTION_TAPPING)
                case OP_ONESHOT:
                    // Oneshot modifier
                    if (!keymap_config.oneshot_enable) {
                        if (event.pressed) {
                            layer_on(action.layer_tap.val);
                        } else {
                            layer_off(action.layer_tap.val);
                        }
                    } else {
#        if defined(ONESHOT_TAP_TOGGLE) && ONESHOT_TAP_TOGGLE > 1
                        do_release_oneshot = false;
                        if (event.pressed) {
                            if (get_oneshot_layer_state() == ONESHOT_TOGGLED) {
                                reset_oneshot_layer();
                                layer_off(action.layer_tap.val);
                                break;
                            } else if (tap_count < ONESHOT_TAP_TOGGLE) {
                                set_oneshot_layer(action.layer_tap.val, ONESHOT_START);
                            }
                        } else {
                            if (tap_count >= ONESHOT_TAP_TOGGLE) {
                                reset_oneshot_layer();
                                set_oneshot_layer(action.layer_tap.val, ONESHOT_TOGGLED);
                            } else {
                                clear_oneshot_layer_state(ONESHOT_PRESSED);
                            }
                        }
#        else
                        if (event.pressed) {
                            set_oneshot_layer(action.layer_tap.val, ONESHOT_START);
                        } else {
                            clear_oneshot_layer_state(ONESHOT_PRESSED);
                            if (tap_count > 1) {
                                clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED);
                            }
                        }
#        endif
                    }
#    else  // NO_ACTION_ONESHOT && NO_ACTION_TAPPING
                    if (event.pressed) {
                        layer_on(action.layer_tap.val);
                    } else {
                        layer_off(action.layer_tap.val);
                    }
#    endif // !defined(NO_ACTION_ONESHOT) && !defined(NO_ACTION_TAPPING)
                    break;
                default:
#    ifndef NO_ACTION_TAPPING /* tap key */
                    if (event.pressed) {
                        if (tap_count > 0) {
                            ac_dprintf("KEYMAP_TAP_KEY: Tap: register_code\n");
                            register_code(action.layer_tap.code);
                        } else {
                            ac_dprintf("KEYMAP_TAP_KEY: No tap: On on press\n");
                            layer_on(action.layer_tap.val);
                        }
                    } else {
                        if (tap_count > 0) {
                            ac_dprintf("KEYMAP_TAP_KEY: Tap: unregister_code\n");
                            if (action.layer_tap.code == KC_CAPS_LOCK) {
                                tap_code_wait(action.layer_tap.code, TAP_HOLD_CAPS_DELAY);
                            } else {
                                tap_code_wait(action.layer_tap.code, TAP_CODE_DELAY);
                            }
                            unregister_code(action.layer_tap.code);
                        } else {
                            ac_dprintf("KEYMAP_TAP_KEY: No tap: Off on release\n");
                            layer_off(action.layer_tap.val);
                        }
                    }
#    else
                    if (event.pressed) {
                        ac_dprintf("KEYMAP_TAP_KEY: Tap: register_code\n");
                        register_code(action.layer_tap.code);
                    } else {
                        ac_dprintf("KEYMAP_TAP_KEY: Tap: unregister_code\n");
                        if (action.layer_tap.code == KC_CAPS) {
                            tap_code_wait(action.layer_tap.code, TAP_HOLD_CAPS_DELAY);
                        } else {
                            tap_code_wait(action.layer_tap.code, TAP_CODE_DELAY);
                        }
                        unregister_code(action.layer_tap.code);
                    }
#    endif
                    break;
            }
            break;
#endif // NO_ACTION_LAYER

#ifdef SWAP_HANDS_ENABLE
        case ACT_SWAP_HANDS:
            switch (action.swap.code) {
                case OP_SH_TOGGLE:
                    if (event.pressed) {
                        swap_hands = !swap_hands;
                    }
                    break;
                case OP_SH_ON_OFF:
                    swap_hands = event.pressed;
                    break;
                case OP_SH_OFF_ON:
                    swap_hands = !event.pressed;
                    break;
                case OP_SH_ON:
                    if (!event.pressed) {
                        swap_hands = true;
                    }
                    break;
                case OP_SH_OFF:
                    if (!event.pressed) {
                        swap_hands = false;
                    }
                    break;
#    ifndef NO_ACTION_ONESHOT
                case OP_SH_ONESHOT:
                    if (event.pressed) {
                        set_oneshot_swaphands();
                    } else {
                        release_oneshot_swaphands();
                    }
                    break;
#    endif

#    ifndef NO_ACTION_TAPPING
                case OP_SH_TAP_TOGGLE:
                    /* tap toggle */

                    if (event.pressed) {
                        if (swap_held) {
                            swap_held = false;
                        } else {
                            swap_hands = !swap_hands;
                        }
                    } else {
                        if (tap_count < TAPPING_TOGGLE) {
                            swap_hands = !swap_hands;
                        }
                    }
                    break;
                default:
                    /* tap key */
                    if (tap_count > 0) {
                        if (swap_held) {
                            swap_hands = !swap_hands; // undo hold set up in _tap_hint
                            swap_held  = false;
                        }
                        if (event.pressed) {
                            register_code(action.swap.code);
                        } else {
                            tap_code_wait(action.swap.code, TAP_CODE_DELAY);
                            unregister_code(action.swap.code);
                            *record = (keyrecord_t){}; // hack: reset tap mode
                        }
                    } else {
                        if (swap_held && !event.pressed) {
                            swap_hands = !swap_hands; // undo hold set up in _tap_hint
                            swap_held  = false;
                        }
                    }
#    endif
            }
#endif
        default:
            break;
    }

#ifndef NO_ACTION_LAYER
    // if this event is a layer action, update the leds
    switch (action.kind.id) {
        case ACT_LAYER:
        case ACT_LAYER_MODS:
#    ifndef NO_ACTION_TAPPING
        case ACT_LAYER_TAP:
        case ACT_LAYER_TAP_EXT:
#    endif
            led_set(host_keyboard_leds());
#    ifndef NO_ACTION_ONESHOT
            // don't release the key
            do_release_oneshot = false;
#    endif
            break;
        default:
            break;
    }
#endif

#ifndef NO_ACTION_TAPPING
#    if defined(RETRO_TAPPING) || defined(RETRO_TAPPING_PER_KEY) || (defined(AUTO_SHIFT_ENABLE) && defined(RETRO_SHIFT))
    if (is_tap_action(action)) {
        if (event.pressed) {
            if (tap_count > 0) {
                retro_tap_primed = false;
            } else {
#        if !(defined(AUTO_SHIFT_ENABLE) && defined(RETRO_SHIFT))
                retro_tap_curr_mods = retro_tap_next_mods;
                retro_tap_next_mods = get_mods();
#        endif
            }
        } else {
            uint16_t event_keycode = get_event_keycode(event, false);
#        if !(defined(AUTO_SHIFT_ENABLE) && defined(RETRO_SHIFT))
            uint8_t curr_mods = get_mods();
#        endif
            if (tap_count > 0) {
                retro_tap_primed = false;
            } else if (retro_tap_curr_key == event_keycode) {
                if (
#        ifdef RETRO_TAPPING_PER_KEY
                    get_retro_tapping(event_keycode, record) &&
#        endif
                    retro_tap_primed) {
#        if defined(AUTO_SHIFT_ENABLE) && defined(RETRO_SHIFT)
                    process_auto_shift(action.layer_tap.code, record);
#        else
                    /* ERA: add only the tap-time modifiers that are up now, so
                     * the tap cannot release a modifier its owner still holds. */
                    const uint8_t retro_mods = retro_tap_curr_mods & ~get_mods();
                    register_mods(retro_mods);
                    tap_code_wait(action.layer_tap.code, TAP_CODE_DELAY);
                    tap_code(action.layer_tap.code);
                    tap_code_wait(action.layer_tap.code, TAP_CODE_DELAY);
                    unregister_mods(retro_mods);
#        endif
                }
                retro_tap_primed = false;
            }
#        if !(defined(AUTO_SHIFT_ENABLE) && defined(RETRO_SHIFT))
            retro_tap_next_mods = curr_mods;
#        endif
        }
    }
#    endif
#endif

#ifdef SWAP_HANDS_ENABLE
#    ifndef NO_ACTION_ONESHOT
    if (event.pressed && !(action.kind.id == ACT_SWAP_HANDS && action.swap.code == OP_SH_ONESHOT)) {
        use_oneshot_swaphands();
    }
#    endif
#endif

#ifndef NO_ACTION_ONESHOT
    /* Because we switch layers after a oneshot event, we need to release the
     * key before we leave the layer or no key up event will be generated.
     */
    if (do_release_oneshot && !(get_oneshot_layer_state() & ONESHOT_PRESSED)
#    ifdef TAPDANCE_ENABLE
        /* A dance owns its input until the physical up; the one-shot layer is
         * already consumed, and an early up would turn every hold into a tap. */
        && !tap_dance_owns_press(record)
#    endif
    ) {
        record->event.pressed = false;
        layer_on(get_oneshot_layer());
        process_record(record);
        layer_off(get_oneshot_layer());
    }
#endif
#ifdef TAP_DANCE_OWNED_ACTIONS
    tap_dance_action_set_owner(td_previous_owner);
#endif
}

/** \brief Utilities for actions. (FIXME: Needs better description)
 *
 * FIXME: Needs documentation.
 */
__attribute__((weak)) void register_code(uint8_t code) {
    if (code == KC_NO) {
        return;

#ifdef LOCKING_SUPPORT_ENABLE
    } else if (KC_LOCKING_CAPS_LOCK == code) {
#    ifdef LOCKING_RESYNC_ENABLE
        // Resync: ignore if caps lock already is on
        if (host_keyboard_led_state().caps_lock) return;
#    endif
        add_key(KC_CAPS_LOCK);
        send_keyboard_report();
        tap_code_wait(KC_CAPS_LOCK, TAP_HOLD_CAPS_DELAY);
        del_key(KC_CAPS_LOCK);
        send_keyboard_report();

    } else if (KC_LOCKING_NUM_LOCK == code) {
#    ifdef LOCKING_RESYNC_ENABLE
        if (host_keyboard_led_state().num_lock) return;
#    endif
        add_key(KC_NUM_LOCK);
        send_keyboard_report();
        wait_ms(100);
        del_key(KC_NUM_LOCK);
        send_keyboard_report();

    } else if (KC_LOCKING_SCROLL_LOCK == code) {
#    ifdef LOCKING_RESYNC_ENABLE
        if (host_keyboard_led_state().scroll_lock) return;
#    endif
        add_key(KC_SCROLL_LOCK);
        send_keyboard_report();
        wait_ms(100);
        del_key(KC_SCROLL_LOCK);
        send_keyboard_report();
#endif

    } else if (IS_BASIC_KEYCODE(code)) {
        // TODO: should push command_proc out of this block?
        if (command_proc(code)) return;

        // Force a new key press if the key is already pressed
        // without this, keys with the same keycode, but different
        // modifiers will be reported incorrectly, see issue #1708
        if (is_key_pressed(code)) {
#ifdef TAP_DANCE_OWNED_ACTIONS
            /* Every new down is its own keystroke, as in QMK. The up is
             * report-only, so another input that holds the usage keeps it. */
            del_key_from_report(code);
#else
            del_key(code);
#endif
            send_keyboard_report();
        }
        add_key(code);
        send_keyboard_report();
    } else if (IS_MODIFIER_KEYCODE(code)) {
        add_mods(MOD_BIT(code));
        send_keyboard_report();

#ifdef EXTRAKEY_ENABLE
    } else if (IS_SYSTEM_KEYCODE(code)) {
        send_system_usage(KEYCODE2SYSTEM(code), true);
    } else if (IS_CONSUMER_KEYCODE(code)) {
        send_consumer_usage(KEYCODE2CONSUMER(code), true);
#endif

    } else if (IS_MOUSE_KEYCODE(code)) {
        register_mouse(code, true);
    }
}

/** \brief Utilities for actions. (FIXME: Needs better description)
 *
 * FIXME: Needs documentation.
 */
__attribute__((weak)) void unregister_code(uint8_t code) {
    if (code == KC_NO) {
        return;

#ifdef LOCKING_SUPPORT_ENABLE
    } else if (KC_LOCKING_CAPS_LOCK == code) {
#    ifdef LOCKING_RESYNC_ENABLE
        // Resync: ignore if caps lock already is off
        if (!host_keyboard_led_state().caps_lock) return;
#    endif
        add_key(KC_CAPS_LOCK);
        send_keyboard_report();
        del_key(KC_CAPS_LOCK);
        send_keyboard_report();

    } else if (KC_LOCKING_NUM_LOCK == code) {
#    ifdef LOCKING_RESYNC_ENABLE
        if (!host_keyboard_led_state().num_lock) return;
#    endif
        add_key(KC_NUM_LOCK);
        send_keyboard_report();
        del_key(KC_NUM_LOCK);
        send_keyboard_report();

    } else if (KC_LOCKING_SCROLL_LOCK == code) {
#    ifdef LOCKING_RESYNC_ENABLE
        if (!host_keyboard_led_state().scroll_lock) return;
#    endif
        add_key(KC_SCROLL_LOCK);
        send_keyboard_report();
        del_key(KC_SCROLL_LOCK);
        send_keyboard_report();
#endif

    } else if (IS_BASIC_KEYCODE(code)) {
        del_key(code);
        send_keyboard_report();
    } else if (IS_MODIFIER_KEYCODE(code)) {
        del_mods(MOD_BIT(code));
        send_keyboard_report();

#ifdef EXTRAKEY_ENABLE
    } else if (IS_SYSTEM_KEYCODE(code)) {
        send_system_usage(KEYCODE2SYSTEM(code), false);
    } else if (IS_CONSUMER_KEYCODE(code)) {
        send_consumer_usage(KEYCODE2CONSUMER(code), false);
#endif

    } else if (IS_MOUSE_KEYCODE(code)) {
        register_mouse(code, false);
    }
}

// V260911R3: 키보드 리포트만 비차단으로 유지한다. 다른 HID 인터페이스의 tap 의미는 보존한다.
void tap_code_wait(uint16_t code, uint16_t delay)
{
  if (delay == 0U)
  {
    return;
  }
  uint8_t basic = (uint8_t)code;
  if (code <= QK_MODS_MAX && (IS_BASIC_KEYCODE(basic) || IS_MODIFIER_KEYCODE(basic) ||
      (basic == KC_NO && code > UINT8_MAX)))
  {
    host_keyboard_delay(delay);
  }
  else
  {
    wait_ms(delay);
  }
}

/** \brief Tap a keycode with a delay.
 *
 * \param code The basic keycode to tap.
 * \param delay 호스트에 보이는 tap의 최소 유지 시간(ms). H7S keyboard 경로는 논리 해제 후에도 USB 스냅샷으로 유지한다.
 */
__attribute__((weak)) void tap_code_delay(uint8_t code, uint16_t delay) {
    register_code(code);
    tap_code_wait(code, delay);
    unregister_code(code);
}

/** \brief Tap a keycode with the default delay.
 *
 * \param code The basic keycode to tap. If `code` is `KC_CAPS_LOCK`, the delay will be `TAP_HOLD_CAPS_DELAY`, otherwise `TAP_CODE_DELAY`, if defined.
 */
__attribute__((weak)) void tap_code(uint8_t code) {
    tap_code_delay(code, code == KC_CAPS_LOCK ? TAP_HOLD_CAPS_DELAY : TAP_CODE_DELAY);
}

/** \brief Adds the given physically pressed modifiers and sends a keyboard report immediately.
 *
 * \param mods A bitfield of modifiers to register.
 */
__attribute__((weak)) void register_mods(uint8_t mods) {
    if (mods) {
        add_mods(mods);
        send_keyboard_report();
    }
}

/** \brief Removes the given physically pressed modifiers and sends a keyboard report immediately.
 *
 * \param mods A bitfield of modifiers to unregister.
 */
__attribute__((weak)) void unregister_mods(uint8_t mods) {
    if (mods) {
        del_mods(mods);
        send_keyboard_report();
    }
}

/** \brief Adds the given weak modifiers and sends a keyboard report immediately.
 *
 * \param mods A bitfield of modifiers to register.
 */
__attribute__((weak)) void register_weak_mods(uint8_t mods) {
    if (mods) {
        add_weak_mods(mods);
        send_keyboard_report();
    }
}

/** \brief Removes the given weak modifiers and sends a keyboard report immediately.
 *
 * \param mods A bitfield of modifiers to unregister.
 */
__attribute__((weak)) void unregister_weak_mods(uint8_t mods) {
    if (mods) {
        del_weak_mods(mods);
        send_keyboard_report();
    }
}

/** \brief Utilities for actions. (FIXME: Needs better description)
 *
 * FIXME: Needs documentation.
 */
void clear_keyboard(void) {
#ifdef TAP_DANCE_OWNED_ACTIONS
    tap_dance_cancel_all();
#endif
    clear_mods();
    clear_keyboard_but_mods();
}

/** \brief Utilities for actions. (FIXME: Needs better description)
 *
 * FIXME: Needs documentation.
 */
void clear_keyboard_but_mods(void) {
#ifdef TAP_DANCE_OWNED_ACTIONS
    tap_dance_clear_key_ownership();
#endif
    clear_keys();
    clear_keyboard_but_mods_and_keys();
}

/** \brief Utilities for actions. (FIXME: Needs better description)
 *
 * FIXME: Needs documentation.
 */
void clear_keyboard_but_mods_and_keys(void) {
#ifdef TAP_DANCE_OWNED_ACTIONS
    tap_dance_clear_hid_ownership();
#endif
#ifdef EXTRAKEY_ENABLE
    host_system_send(0);
    host_consumer_send(0);
#endif
    clear_weak_mods();
    send_keyboard_report();
#ifdef MOUSEKEY_ENABLE
    mousekey_clear();
    mousekey_send();
#endif
#ifdef PROGRAMMABLE_BUTTON_ENABLE
    programmable_button_clear();
#endif
}

/** \brief Utilities for actions. (FIXME: Needs better description)
 *
 * FIXME: Needs documentation.
 */
bool is_tap_record(keyrecord_t *record) {
    if (IS_NOEVENT(record->event)) {
        return false;
    }

#if defined(COMBO_ENABLE) || defined(REPEAT_KEY_ENABLE)
    action_t action;
    if (record->keycode) {
        action = action_for_keycode(record->keycode);
    } else {
        action = layer_switch_get_action(record->event.key);
    }
#else
    action_t action = layer_switch_get_action(record->event.key);
#endif
    return is_tap_action(action);
}

/** \brief Utilities for actions. (FIXME: Needs better description)
 *
 * FIXME: Needs documentation.
 */
bool is_tap_action(action_t action) {
    switch (action.kind.id) {
        case ACT_LMODS_TAP:
        case ACT_RMODS_TAP:
        case ACT_LAYER_TAP:
        case ACT_LAYER_TAP_EXT:
            switch (action.layer_tap.code) {
                case KC_NO ... KC_RIGHT_GUI:
                case OP_TAP_TOGGLE:
                case OP_ONESHOT:
                    return true;
            }
            return false;
        case ACT_SWAP_HANDS:
            switch (action.swap.code) {
                case KC_NO ... KC_RIGHT_GUI:
                case OP_SH_TAP_TOGGLE:
                    return true;
            }
            return false;
    }
    return false;
}

/** \brief Debug print (FIXME: Needs better description)
 *
 * FIXME: Needs documentation.
 */
void debug_event(keyevent_t event) {
    ac_dprintf("%04X%c(%u)", (event.key.row << 8 | event.key.col), (event.pressed ? 'd' : 'u'), event.time);
}
/** \brief Debug print (FIXME: Needs better description)
 *
 * FIXME: Needs documentation.
 */
void debug_record(keyrecord_t record) {
    debug_event(record.event);
#ifndef NO_ACTION_TAPPING
    ac_dprintf(":%u%c", record.tap.count, (record.tap.interrupted ? '-' : ' '));
#endif
}

/** \brief Debug print (FIXME: Needs better description)
 *
 * FIXME: Needs documentation.
 */
void debug_action(action_t action) {
    switch (action.kind.id) {
        case ACT_LMODS:
            ac_dprintf("ACT_LMODS");
            break;
        case ACT_RMODS:
            ac_dprintf("ACT_RMODS");
            break;
        case ACT_LMODS_TAP:
            ac_dprintf("ACT_LMODS_TAP");
            break;
        case ACT_RMODS_TAP:
            ac_dprintf("ACT_RMODS_TAP");
            break;
        case ACT_USAGE:
            ac_dprintf("ACT_USAGE");
            break;
        case ACT_MOUSEKEY:
            ac_dprintf("ACT_MOUSEKEY");
            break;
        case ACT_LAYER:
            ac_dprintf("ACT_LAYER");
            break;
        case ACT_LAYER_MODS:
            ac_dprintf("ACT_LAYER_MODS");
            break;
        case ACT_LAYER_TAP:
            ac_dprintf("ACT_LAYER_TAP");
            break;
        case ACT_LAYER_TAP_EXT:
            ac_dprintf("ACT_LAYER_TAP_EXT");
            break;
        case ACT_SWAP_HANDS:
            ac_dprintf("ACT_SWAP_HANDS");
            break;
        default:
            ac_dprintf("UNKNOWN");
            break;
    }
    ac_dprintf("[%X:%02X]", action.kind.param >> 8, action.kind.param & 0xff);
}
