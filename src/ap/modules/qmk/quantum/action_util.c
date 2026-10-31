/*
Copyright 2013 Jun Wako <wakojun@gmail.com>

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
#include "host.h"
#include "report.h"
#include "debug.h"
#include "action_util.h"
#include "action_layer.h"
#include "timer.h"
#include "keycode_config.h"
#ifdef MOUSEKEY_ENABLE
#    include "mousekey.h"
#endif
#include <string.h>

extern keymap_config_t keymap_config;

static uint8_t real_mods = 0;
static uint8_t weak_mods = 0;
#ifdef ACTION_OWNERSHIP_ENABLE
_Static_assert(ACTION_OWNER_COUNT < UINT8_MAX, "Owned action IDs must fit in one byte");
static action_owner_t current_owner = ACTION_OWNER_REGULAR;
static uint8_t owned_real_mods[ACTION_OWNER_COUNT];
static uint8_t owned_weak_mods[ACTION_OWNER_COUNT];
static uint8_t owned_real_union, owned_weak_union;
static uint8_t owned_real_counts[8], owned_weak_counts[8];
/* Ordinary QMK usages remain bit state, not per-key reference counts. Only
 * TD and macro contributions have owners; the report shape stays unchanged. */
static uint8_t regular_keys[32];
static uint8_t owned_keys[ACTION_OWNER_COUNT][32];
static uint8_t owned_key_counts[256];
static uint8_t owner_key_counts[ACTION_OWNER_COUNT];

void add_key(uint8_t key) {
    const uint8_t bit = (uint8_t)(1U << (key & 7));
    if (current_owner < ACTION_OWNER_COUNT) {
        if (!(owned_keys[current_owner][key >> 3] & bit)) {
            owned_keys[current_owner][key >> 3] |= bit;
            ++owned_key_counts[key];
            ++owner_key_counts[current_owner];
        }
    } else {
        regular_keys[key >> 3] |= bit;
    }
    add_key_to_report(key);
}

void del_key(uint8_t key) {
    const uint8_t bit = (uint8_t)(1U << (key & 7));
    if (current_owner < ACTION_OWNER_COUNT) {
        if (owned_keys[current_owner][key >> 3] & bit) {
            owned_keys[current_owner][key >> 3] &= (uint8_t)~bit;
            --owned_key_counts[key];
            --owner_key_counts[current_owner];
        }
    } else {
        regular_keys[key >> 3] &= (uint8_t)~bit;
    }
    if (!owned_key_counts[key] && !(regular_keys[key >> 3] & bit)) del_key_from_report(key);
}

void action_owner_clear_keys(action_owner_t owner) {
    if (owner >= ACTION_OWNER_COUNT || !owner_key_counts[owner]) return;
    const uint8_t previous = current_owner;
    current_owner = owner;
    for (uint16_t key = 0; key < 256; ++key) {
        if (owned_keys[owner][key >> 3] & (1U << (key & 7))) del_key((uint8_t)key);
    }
    current_owner = previous;
}

void action_ownership_reset_keys(void) {
    memset(regular_keys, 0, sizeof(regular_keys));
    memset(owned_keys, 0, sizeof(owned_keys));
    memset(owned_key_counts, 0, sizeof(owned_key_counts));
    memset(owner_key_counts, 0, sizeof(owner_key_counts));
}

action_owner_t action_owner_select(action_owner_t owner) {
    uint8_t previous = current_owner;
    current_owner = owner < ACTION_OWNER_COUNT ? owner : ACTION_OWNER_REGULAR;
    return previous;
}
action_owner_t action_owner_current(void) { return current_owner; }
/* Cost is bounded by the eight HID modifier bits, not the matrix size. */
static void action_replace_mods(uint8_t *owned, uint8_t mods, uint8_t *counts, uint8_t *combined) {
    const uint8_t changed = *owned ^ mods;
    for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t bit = (uint8_t)(1U << i);
        if (!(changed & bit)) continue;
        if (mods & bit) {
            ++counts[i];
            *combined |= bit;
        } else if (--counts[i] == 0) {
            *combined &= (uint8_t)~bit;
        }
    }
    *owned = mods;
}
void action_owner_clear_mods(action_owner_t owner) {
    if (owner < ACTION_OWNER_COUNT) {
        action_replace_mods(&owned_real_mods[owner], 0, owned_real_counts, &owned_real_union);
        action_replace_mods(&owned_weak_mods[owner], 0, owned_weak_counts, &owned_weak_union);
    }
}

/* Ordinary mouse usages are bit state; explicit owner contributions are counted.
 * System/consumer reports each represent only one selected usage. */
#    define ACTION_MOUSE_CODES (KC_MS_ACCEL2 - KC_MS_UP + 1)
_Static_assert(ACTION_MOUSE_CODES <= 32, "Mouse ownership uses one 32-bit mask");
static uint32_t regular_mouse;
static uint32_t owned_mouse[ACTION_OWNER_COUNT];
static uint8_t owned_mouse_counts[ACTION_MOUSE_CODES];
static uint16_t regular_usage[2];
static uint16_t owned_usage[ACTION_OWNER_COUNT][2];
static uint8_t owned_usage_owners[2];

bool action_mouse_update(uint8_t code, bool pressed) {
    if (code < KC_MS_UP || code > KC_MS_ACCEL2) return true;
    const uint8_t i = (uint8_t)(code - KC_MS_UP);
    const uint32_t bit = 1UL << i;
    if (current_owner < ACTION_OWNER_COUNT) {
        uint32_t *owned = &owned_mouse[current_owner];
        if (pressed && !(*owned & bit)) {
            *owned |= bit;
            ++owned_mouse_counts[i];
        } else if (!pressed && (*owned & bit)) {
            *owned &= ~bit;
            --owned_mouse_counts[i];
        }
    } else {
        regular_mouse = pressed ? regular_mouse | bit : regular_mouse & ~bit;
    }
    return pressed || (!owned_mouse_counts[i] && !(regular_mouse & bit));
}

/* Only output mutations/reconciliation scan these bounded page contributions. */
static bool action_usage_held(uint8_t page, uint16_t usage) {
    for (uint8_t i = 0; owned_usage_owners[page] && i < ACTION_OWNER_COUNT; ++i) {
        if (owned_usage[i][page] == usage) return true;
    }
    return false;
}

uint16_t action_owned_usage(uint8_t page, uint16_t current) {
    if (page > ACTION_USAGE_PAGE_CONSUMER) return 0;
    if (current && (regular_usage[page] == current || action_usage_held(page, current))) return current;
    if (regular_usage[page]) return regular_usage[page];
    for (uint8_t owner = 0; owned_usage_owners[page] && owner < ACTION_OWNER_COUNT; ++owner) {
        if (owned_usage[owner][page]) return owned_usage[owner][page];
    }
    return 0;
}

uint16_t action_usage_update(uint8_t page, uint16_t usage, uint16_t current, bool pressed) {
    if (page > ACTION_USAGE_PAGE_CONSUMER) return 0;
    const bool scoped = current_owner < ACTION_OWNER_COUNT;
    uint16_t *held = scoped ? &owned_usage[current_owner][page] : &regular_usage[page];
    if (pressed) {
        if (scoped && !*held && usage) ++owned_usage_owners[page];
        if (scoped && *held && !usage) --owned_usage_owners[page];
        *held = usage;
        return usage;
    }
    if (*held == usage && usage) {
        *held = 0;
        if (scoped) --owned_usage_owners[page];
    }
    return action_owned_usage(page, current);
}

void action_owner_clear_hid(action_owner_t owner) {
    if (owner >= ACTION_OWNER_COUNT) return;
    const uint8_t previous = current_owner;
    current_owner = owner;
    for (uint8_t i = 0; owned_mouse[owner] && i < ACTION_MOUSE_CODES; ++i) {
        if (owned_mouse[owner] & (1UL << i)) register_mouse((uint8_t)(KC_MS_UP + i), false);
    }
    for (uint8_t page = 0; page < 2; ++page) {
        if (!owned_usage[owner][page]) continue;
        owned_usage[owner][page] = 0;
        --owned_usage_owners[page];
#    ifdef EXTRAKEY_ENABLE
        if (page == ACTION_USAGE_PAGE_SYSTEM)
            host_system_send(action_owned_usage(page, host_last_system_usage()));
        else
            host_consumer_send(action_owned_usage(page, host_last_consumer_usage()));
#    endif
    }
    current_owner = previous;
}

void action_ownership_reset_hid(void) {
    regular_mouse = 0;
    memset(owned_mouse, 0, sizeof(owned_mouse));
    memset(owned_mouse_counts, 0, sizeof(owned_mouse_counts));
    memset(regular_usage, 0, sizeof(regular_usage));
    memset(owned_usage, 0, sizeof(owned_usage));
    memset(owned_usage_owners, 0, sizeof(owned_usage_owners));
}

bool action_owner_has_outputs(action_owner_t owner) {
    return owner < ACTION_OWNER_COUNT && (owner_key_counts[owner] || owned_real_mods[owner] ||
           owned_weak_mods[owner] || owned_mouse[owner] || owned_usage[owner][0] || owned_usage[owner][1]);
}

void action_owner_release(action_owner_t owner) {
    if (owner >= ACTION_OWNER_COUNT) return;
    const action_owner_t previous = action_owner_select(owner);
    action_owner_clear_keys(owner);
    action_owner_clear_mods(owner);
    for (uint8_t i = 0; owned_mouse[owner] && i < ACTION_MOUSE_CODES; ++i) {
        if (!(owned_mouse[owner] & (1UL << i))) continue;
        const uint8_t code = (uint8_t)(KC_MS_UP + i);
        if (action_mouse_update(code, false)) {
#    ifdef MOUSEKEY_ENABLE
            mousekey_off(code);
#    endif
        }
    }
    for (uint8_t page = 0; page < 2; ++page) {
        if (owned_usage[owner][page]) {
            owned_usage[owner][page] = 0;
            --owned_usage_owners[page];
        }
    }
    action_owner_select(previous);
}
#endif

#ifdef KEY_OVERRIDE_ENABLE
static uint8_t weak_override_mods = 0;
static uint8_t suppressed_mods    = 0;
#endif

// TODO: pointer variable is not needed
// report_keyboard_t keyboard_report = {};
report_keyboard_t *keyboard_report = &(report_keyboard_t){};
#ifdef NKRO_ENABLE
report_nkro_t *nkro_report = &(report_nkro_t){};
#endif

#ifndef ACTION_OWNERSHIP_ENABLE
extern inline void add_key(uint8_t key);
extern inline void del_key(uint8_t key);
#endif
extern inline void clear_keys(void);

#ifndef NO_ACTION_ONESHOT
static uint8_t oneshot_mods        = 0;
static uint8_t oneshot_locked_mods = 0;
uint8_t        get_oneshot_locked_mods(void) {
    return oneshot_locked_mods;
}
void add_oneshot_locked_mods(uint8_t mods) {
    if ((oneshot_locked_mods & mods) != mods) {
        oneshot_locked_mods |= mods;
        oneshot_locked_mods_changed_kb(oneshot_locked_mods);
    }
}
void set_oneshot_locked_mods(uint8_t mods) {
    if (mods != oneshot_locked_mods) {
        oneshot_locked_mods = mods;
        oneshot_locked_mods_changed_kb(oneshot_locked_mods);
    }
}
void clear_oneshot_locked_mods(void) {
    if (oneshot_locked_mods) {
        oneshot_locked_mods = 0;
        oneshot_locked_mods_changed_kb(oneshot_locked_mods);
    }
}
void del_oneshot_locked_mods(uint8_t mods) {
    if (oneshot_locked_mods & mods) {
        oneshot_locked_mods &= ~mods;
        oneshot_locked_mods_changed_kb(oneshot_locked_mods);
    }
}
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
static uint16_t oneshot_time = 0;
bool            has_oneshot_mods_timed_out(void) {
    return TIMER_DIFF_16(timer_read(), oneshot_time) >= ONESHOT_TIMEOUT;
}
#    else
bool has_oneshot_mods_timed_out(void) {
    return false;
}
#    endif
#endif

/* oneshot layer */
#ifndef NO_ACTION_ONESHOT
/** \brief oneshot_layer_data bits
 * LLLL LSSS
 * where:
 *   L => are layer bits
 *   S => oneshot state bits
 */
static uint8_t oneshot_layer_data = 0;

inline uint8_t get_oneshot_layer(void) {
    return oneshot_layer_data >> 3;
}
inline uint8_t get_oneshot_layer_state(void) {
    return oneshot_layer_data & 0b111;
}

#    ifdef SWAP_HANDS_ENABLE
enum {
    SHO_OFF,
    SHO_ACTIVE,  // Swap hands button was pressed, and we didn't send any swapped keys yet
    SHO_PRESSED, // Swap hands button is currently pressed
    SHO_USED,    // Swap hands button is still pressed, and we already sent swapped keys
} swap_hands_oneshot = SHO_OFF;
#    endif

#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
static uint16_t oneshot_layer_time = 0;
inline bool     has_oneshot_layer_timed_out(void) {
    return TIMER_DIFF_16(timer_read(), oneshot_layer_time) >= ONESHOT_TIMEOUT && !(get_oneshot_layer_state() & ONESHOT_TOGGLED);
}
#        ifdef SWAP_HANDS_ENABLE
static uint16_t oneshot_swaphands_time = 0;
inline bool     has_oneshot_swaphands_timed_out(void) {
    return TIMER_DIFF_16(timer_read(), oneshot_swaphands_time) >= ONESHOT_TIMEOUT && (swap_hands_oneshot == SHO_ACTIVE);
}
#        endif
#    endif

#    ifdef SWAP_HANDS_ENABLE

void set_oneshot_swaphands(void) {
    swap_hands_oneshot = SHO_PRESSED;
    swap_hands         = true;
#        if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
    oneshot_swaphands_time = timer_read();
    if (oneshot_layer_time != 0) {
        oneshot_layer_time = oneshot_swaphands_time;
    }
#        endif
}

void release_oneshot_swaphands(void) {
    if (swap_hands_oneshot == SHO_PRESSED) {
        swap_hands_oneshot = SHO_ACTIVE;
    }
    if (swap_hands_oneshot == SHO_USED) {
        clear_oneshot_swaphands();
    }
}

void use_oneshot_swaphands(void) {
    if (swap_hands_oneshot == SHO_PRESSED) {
        swap_hands_oneshot = SHO_USED;
    }
    if (swap_hands_oneshot == SHO_ACTIVE) {
        clear_oneshot_swaphands();
    }
}

void clear_oneshot_swaphands(void) {
    swap_hands_oneshot = SHO_OFF;
    swap_hands         = false;
#        if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
    oneshot_swaphands_time = 0;
#        endif
}

#    endif

/** \brief Set oneshot layer
 *
 * FIXME: needs doc
 */
/* One-shot layer state is global, so its layer bit is never a TD contribution,
 * even when a TD callback starts or consumes it. */
static void oneshot_layer_switch(uint8_t layer, bool on) {
#    ifdef ACTION_OWNERSHIP_ENABLE
    const uint8_t owner = action_owner_select(UINT8_MAX);
#    endif
    if (on) {
        layer_on(layer);
    } else {
        layer_off(layer);
    }
#    ifdef ACTION_OWNERSHIP_ENABLE
    action_owner_select(owner);
#    endif
}

void set_oneshot_layer(uint8_t layer, uint8_t state) {
    if (keymap_config.oneshot_enable) {
        oneshot_layer_data = layer << 3 | state;
        oneshot_layer_switch(layer, true);
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
        oneshot_layer_time = timer_read();
#    endif
        oneshot_layer_changed_kb(get_oneshot_layer());
    } else {
        oneshot_layer_switch(layer, true);
    }
}
/** \brief Reset oneshot layer
 *
 * FIXME: needs doc
 */
void reset_oneshot_layer(void) {
    oneshot_layer_data = 0;
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
    oneshot_layer_time = 0;
#    endif
    oneshot_layer_changed_kb(get_oneshot_layer());
}
/** \brief Clear oneshot layer
 *
 * FIXME: needs doc
 */
void clear_oneshot_layer_state(oneshot_fullfillment_t state) {
    uint8_t start_state = oneshot_layer_data;
    oneshot_layer_data &= ~state;
    if ((!get_oneshot_layer_state() && start_state != oneshot_layer_data) && keymap_config.oneshot_enable) {
        oneshot_layer_switch(get_oneshot_layer(), false);
        reset_oneshot_layer();
    }
}
/** \brief Is oneshot layer active
 *
 * FIXME: needs doc
 */
bool is_oneshot_layer_active(void) {
    return get_oneshot_layer_state();
}

/** \brief set oneshot
 *
 * FIXME: needs doc
 */
void oneshot_set(bool active) {
    if (keymap_config.oneshot_enable != active) {
        keymap_config.oneshot_enable = active;
        eeconfig_update_keymap(keymap_config.raw);
        clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED);
        dprintf("Oneshot: active: %d\n", active);
    }
}

/** \brief toggle oneshot
 *
 * FIXME: needs doc
 */
void oneshot_toggle(void) {
    oneshot_set(!keymap_config.oneshot_enable);
}

/** \brief enable oneshot
 *
 * FIXME: needs doc
 */
void oneshot_enable(void) {
    oneshot_set(true);
}

/** \brief disable oneshot
 *
 * FIXME: needs doc
 */
void oneshot_disable(void) {
    oneshot_set(false);
}

bool is_oneshot_enabled(void) {
    return keymap_config.oneshot_enable;
}

#endif

static uint8_t get_mods_for_report(void) {
    uint8_t mods = get_mods() | get_weak_mods();

#ifndef NO_ACTION_ONESHOT
    if (oneshot_mods) {
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
        if (has_oneshot_mods_timed_out()) {
            dprintf("Oneshot: timeout\n");
            clear_oneshot_mods();
        }
#    endif
        mods |= oneshot_mods;
        if (has_anykey()) {
            clear_oneshot_mods();
        }
    }
#endif

#ifdef KEY_OVERRIDE_ENABLE
    // These need to be last to be able to properly control key overrides
    mods &= ~suppressed_mods;
    mods |= weak_override_mods;
#endif

    return mods;
}

__attribute__((weak)) void keyboard_report_filter(report_keyboard_t *report) {
    (void)report;
}

static void send_6kro_report(bool force) {
    keyboard_report->mods = get_mods_for_report();

    /* Static like keyboard_report itself, so the host driver sees the same lifetime. */
    static report_keyboard_t report;
    memcpy(&report, keyboard_report, sizeof(report_keyboard_t));
    keyboard_report_filter(&report);

#ifdef PROTOCOL_VUSB
    (void)force;
    host_keyboard_send(&report);
#else
    static report_keyboard_t last_report;

    /* Only send the report if there are changes to propagate to the host. */
    if (force || memcmp(&report, &last_report, sizeof(report_keyboard_t)) != 0 || host_keyboard_report_needs_send()) {
        memcpy(&last_report, &report, sizeof(report_keyboard_t));
        host_keyboard_send(&report);
    }
#endif
}

#ifdef NKRO_ENABLE
__attribute__((weak)) void nkro_report_filter(report_nkro_t *report) {
    (void)report;
}

static void send_nkro_report(bool force) {
    nkro_report->mods = get_mods_for_report();

    static report_nkro_t report;
    memcpy(&report, nkro_report, sizeof(report_nkro_t));
    nkro_report_filter(&report);

    static report_nkro_t last_report;

    /* Only send the report if there are changes to propagate to the host. */
    if (force || memcmp(&report, &last_report, sizeof(report_nkro_t)) != 0) {
        memcpy(&last_report, &report, sizeof(report_nkro_t));
        host_nkro_send(&report);
    }
}
#endif

/** \brief Send keyboard report
 *
 * FIXME: needs doc
 */
static void send_keyboard_report_internal(bool force) {
#ifdef NKRO_ENABLE
    if (keyboard_protocol && keymap_config.nkro) {
        send_nkro_report(force);
    } else {
        send_6kro_report(force);
    }
#else
    send_6kro_report(force);
#endif
}

void send_keyboard_report(void) {
    send_keyboard_report_internal(false);
}

void send_keyboard_report_force(void) {
    send_keyboard_report_internal(true);
}

/** \brief Get mods
 *
 * FIXME: needs doc
 */
uint8_t get_mods(void) {
#ifdef ACTION_OWNERSHIP_ENABLE
    return real_mods | owned_real_union;
#else
    return real_mods;
#endif
}
/** \brief add mods
 *
 * FIXME: needs doc
 */
void add_mods(uint8_t mods) {
#ifdef ACTION_OWNERSHIP_ENABLE
    if (current_owner < ACTION_OWNER_COUNT) {
        action_replace_mods(&owned_real_mods[current_owner], owned_real_mods[current_owner] | mods, owned_real_counts, &owned_real_union);
        return;
    }
#endif
    real_mods |= mods;
}
/** \brief del mods
 *
 * FIXME: needs doc
 */
void del_mods(uint8_t mods) {
#ifdef ACTION_OWNERSHIP_ENABLE
    if (current_owner < ACTION_OWNER_COUNT) {
        action_replace_mods(&owned_real_mods[current_owner], owned_real_mods[current_owner] & (uint8_t)~mods, owned_real_counts, &owned_real_union);
        return;
    }
#endif
    real_mods &= ~mods;
}
/** \brief set mods
 *
 * FIXME: needs doc
 */
void set_mods(uint8_t mods) {
#ifdef ACTION_OWNERSHIP_ENABLE
    if (current_owner < ACTION_OWNER_COUNT) {
        action_replace_mods(&owned_real_mods[current_owner], mods, owned_real_counts, &owned_real_union);
        return;
    }
#endif
    real_mods = mods;
}
/** \brief clear mods
 *
 * FIXME: needs doc
 */
void clear_mods(void) {
    real_mods = 0;
}

/** \brief get weak mods
 *
 * FIXME: needs doc
 */
uint8_t get_weak_mods(void) {
#ifdef ACTION_OWNERSHIP_ENABLE
    return weak_mods | owned_weak_union;
#else
    return weak_mods;
#endif
}
/** \brief add weak mods
 *
 * FIXME: needs doc
 */
void add_weak_mods(uint8_t mods) {
#ifdef ACTION_OWNERSHIP_ENABLE
    if (current_owner < ACTION_OWNER_COUNT) {
        action_replace_mods(&owned_weak_mods[current_owner], owned_weak_mods[current_owner] | mods, owned_weak_counts, &owned_weak_union);
        return;
    }
#endif
    weak_mods |= mods;
}
/** \brief del weak mods
 *
 * FIXME: needs doc
 */
void del_weak_mods(uint8_t mods) {
#ifdef ACTION_OWNERSHIP_ENABLE
    if (current_owner < ACTION_OWNER_COUNT) {
        action_replace_mods(&owned_weak_mods[current_owner], owned_weak_mods[current_owner] & (uint8_t)~mods, owned_weak_counts, &owned_weak_union);
        return;
    }
#endif
    weak_mods &= ~mods;
}
/** \brief set weak mods
 *
 * FIXME: needs doc
 */
void set_weak_mods(uint8_t mods) {
#ifdef ACTION_OWNERSHIP_ENABLE
    if (current_owner < ACTION_OWNER_COUNT) {
        action_replace_mods(&owned_weak_mods[current_owner], mods, owned_weak_counts, &owned_weak_union);
        return;
    }
#endif
    weak_mods = mods;
}
/** \brief clear weak mods
 *
 * FIXME: needs doc
 */
void clear_weak_mods(void) {
#ifdef ACTION_OWNERSHIP_ENABLE
    /* Preserve QMK's clear-on-next-press rule for weak (not held) modifiers. */
    memset(owned_weak_mods, 0, sizeof(owned_weak_mods));
    memset(owned_weak_counts, 0, sizeof(owned_weak_counts));
    owned_weak_union = 0;
#endif
    weak_mods = 0;
}

#ifdef KEY_OVERRIDE_ENABLE
/** \brief set weak mods used by key overrides. DO not call this manually
 */
void set_weak_override_mods(uint8_t mods) {
    weak_override_mods = mods;
}
/** \brief clear weak mods used by key overrides. DO not call this manually
 */
void clear_weak_override_mods(void) {
    weak_override_mods = 0;
}

/** \brief set suppressed mods used by key overrides. DO not call this manually
 */
void set_suppressed_override_mods(uint8_t mods) {
    suppressed_mods = mods;
}
/** \brief clear suppressed mods used by key overrides. DO not call this manually
 */
void clear_suppressed_override_mods(void) {
    suppressed_mods = 0;
}
#endif

#ifndef NO_ACTION_ONESHOT
/** \brief get oneshot mods
 *
 * FIXME: needs doc
 */
uint8_t get_oneshot_mods(void) {
    return oneshot_mods;
}

void add_oneshot_mods(uint8_t mods) {
    if ((oneshot_mods & mods) != mods) {
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
        oneshot_time = timer_read();
#    endif
        oneshot_mods |= mods;
        oneshot_mods_changed_kb(mods);
    }
}

void del_oneshot_mods(uint8_t mods) {
    if (oneshot_mods & mods) {
        oneshot_mods &= ~mods;
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
        oneshot_time = oneshot_mods ? timer_read() : 0;
#    endif
        oneshot_mods_changed_kb(oneshot_mods);
    }
}

/** \brief set oneshot mods
 *
 * FIXME: needs doc
 */
void set_oneshot_mods(uint8_t mods) {
    if (keymap_config.oneshot_enable) {
        if (oneshot_mods != mods) {
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
            oneshot_time = timer_read();
#    endif
            oneshot_mods = mods;
            oneshot_mods_changed_kb(mods);
        }
    }
}

/** \brief clear oneshot mods
 *
 * FIXME: needs doc
 */
void clear_oneshot_mods(void) {
    if (oneshot_mods) {
        oneshot_mods = 0;
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
        oneshot_time = 0;
#    endif
        oneshot_mods_changed_kb(oneshot_mods);
    }
}
#endif

/** \brief Called when the one shot modifiers have been changed.
 *
 * \param mods Contains the active modifiers active after the change.
 */
__attribute__((weak)) void oneshot_locked_mods_changed_user(uint8_t mods) {}

/** \brief Called when the locked one shot modifiers have been changed.
 *
 * \param mods Contains the active modifiers active after the change.
 */
__attribute__((weak)) void oneshot_locked_mods_changed_kb(uint8_t mods) {
    oneshot_locked_mods_changed_user(mods);
}

/** \brief Called when the one shot modifiers have been changed.
 *
 * \param mods Contains the active modifiers active after the change.
 */
__attribute__((weak)) void oneshot_mods_changed_user(uint8_t mods) {}

/** \brief Called when the one shot modifiers have been changed.
 *
 * \param mods Contains the active modifiers active after the change.
 */
__attribute__((weak)) void oneshot_mods_changed_kb(uint8_t mods) {
    oneshot_mods_changed_user(mods);
}

/** \brief Called when the one shot layers have been changed.
 *
 * \param layer Contains the layer that is toggled on, or zero when toggled off.
 */
__attribute__((weak)) void oneshot_layer_changed_user(uint8_t layer) {}

/** \brief Called when the one shot layers have been changed.
 *
 * \param layer Contains the layer that is toggled on, or zero when toggled off.
 */
__attribute__((weak)) void oneshot_layer_changed_kb(uint8_t layer) {
    oneshot_layer_changed_user(layer);
}

/** \brief inspect keyboard state
 *
 * FIXME: needs doc
 */
uint8_t has_anymod(void) {
    return bitpop(real_mods);
}

#ifdef DUMMY_MOD_NEUTRALIZER_KEYCODE
/** \brief Send a dummy keycode in between the register and unregister event of a modifier key, to neutralize the "flashing modifiers" phenomenon.
 *
 * \param active_mods 8-bit packed bit-array describing the currently active modifiers (in the format GASCGASC).
 *
 * Certain QMK features like  key overrides or retro tap must unregister a previously
 * registered modifier before sending another keycode but this can trigger undesired
 * keyboard shortcuts if the clean tap of a single modifier key is bound to an action
 * on the host OS, as is for example the case for the left GUI key on Windows, which
 * opens the Start Menu when tapped.
 */
void neutralize_flashing_modifiers(uint8_t active_mods) {
    // In most scenarios, the flashing modifiers phenomenon is a problem
    // only for a subset of modifier masks.
    const static uint8_t mods_to_neutralize[] = MODS_TO_NEUTRALIZE;
    const static uint8_t n_mods               = ARRAY_SIZE(mods_to_neutralize);
    for (uint8_t i = 0; i < n_mods; ++i) {
        if (active_mods == mods_to_neutralize[i]) {
            tap_code(DUMMY_MOD_NEUTRALIZER_KEYCODE);
            break;
        }
    }
}
#endif
