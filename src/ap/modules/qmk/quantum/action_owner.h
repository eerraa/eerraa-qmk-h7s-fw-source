#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Owner allocation is compile-time only; no TD runtime types enter this layer. */
#if defined(TAPDANCE_ENABLE) || defined(ERA_MACRO_ENABLE)
#    define ACTION_OWNERSHIP_ENABLE
#    ifdef TAPDANCE_ENABLE
#        ifndef TAP_DANCE_MAX_SIMULTANEOUS
#            define TAP_DANCE_MAX_SIMULTANEOUS (MATRIX_ROWS * MATRIX_COLS + 8)
#        endif
#        define ACTION_OWNER_TAP_DANCE_COUNT TAP_DANCE_MAX_SIMULTANEOUS
#    else
#        define ACTION_OWNER_TAP_DANCE_COUNT 0U
#    endif
#    ifdef ERA_MACRO_ENABLE
#        define ACTION_OWNER_COUNT (ACTION_OWNER_TAP_DANCE_COUNT + 2U)
#        define ACTION_OWNER_MACRO_PERSISTENT ACTION_OWNER_TAP_DANCE_COUNT
#        define ACTION_OWNER_MACRO_TEMPORARY (ACTION_OWNER_TAP_DANCE_COUNT + 1U)
#    else
#        define ACTION_OWNER_COUNT ACTION_OWNER_TAP_DANCE_COUNT
#    endif

typedef uint8_t action_owner_t;
#    define ACTION_OWNER_REGULAR UINT8_MAX
#    define ACTION_USAGE_PAGE_SYSTEM 0U
#    define ACTION_USAGE_PAGE_CONSUMER 1U

/* Select returns a token accepted unchanged by a later select, including across
 * nested producers. Unscoped QMK output retains its ordinary bit-state semantics. */
action_owner_t action_owner_select(action_owner_t owner);
action_owner_t action_owner_current(void);
/* Per-owner operations address allocated tokens. Ordinary output keeps QMK
 * clear_mods/clear_keys/reset semantics instead of a per-input reference count. */
bool action_owner_has_outputs(action_owner_t owner);
void action_owner_clear_mods(action_owner_t owner);
void action_owner_clear_keys(action_owner_t owner);
void action_owner_clear_hid(action_owner_t owner);
/* Remove contributions without sending reports; mouse state retirement supports
 * the MOUSEKEY backend. The caller owns publication and transport/session policy;
 * the current scope is preserved. Direct host/report edits do not acquire owners. */
void action_owner_release(action_owner_t owner);
void action_ownership_reset_keys(void);
void action_ownership_reset_hid(void);
bool action_mouse_update(uint8_t code, bool pressed);
/* Single-slot pages: DOWN selects its usage; UP retains a live current usage,
 * then falls back to ordinary output, then the first surviving owner token. */
uint16_t action_usage_update(uint8_t page, uint16_t usage, uint16_t current, bool pressed);
uint16_t action_owned_usage(uint8_t page, uint16_t current);
#endif

#ifdef __cplusplus
}
#endif
