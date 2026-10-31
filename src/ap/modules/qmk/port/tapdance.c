#include "tapdance.h"
#include "tapping_term_policy.h"


#ifdef TAPDANCE_ENABLE

#include <string.h>
#include "port.h"
#include "quantum.h"
#include "process_keycode/process_tap_dance.h"
#include "timer.h"                                        // V251127R1: Tap Dance 내부 가상 keyevent 타임스탬프
#include "wait.h"
#include "era_state_sync.h"                               // V260821R1: exact/legacy term·action 변경 시 CONFIG revision


#define TAPDANCE_SIGNATURE        (0x4E414454UL)   // "TDAN" // V251124R8: Tap Dance EEPROM 시그니처
#define TAPDANCE_VERSION          (1U)
#define TAPDANCE_VALUE_STRIDE     (5U)
#define TAPDANCE_VALUE_MAX_ID     (TAPDANCE_SLOT_COUNT * TAPDANCE_VALUE_STRIDE)
#define TAPDANCE_MODE_ID_BASE    (49U)
#define TAPDANCE_MODE_TAG        (0xD2U)
#define TAPDANCE_HOLD_TERM_ID_BASE (57U)
#define TAPDANCE_HOLD_OTHER_ID_BASE (65U)
#define TAPDANCE_TIMING_TAG (0xD3U)
#define TAPDANCE_TIMING_SIGNATURE (0x4D544454UL)
#define TAPDANCE_FIELD_TERM       (4U)
#define TAPDANCE_EXACT_TERM_ID_BASE (41U)                 // V260821R1: TD0 exact = 41 … TD7 exact = 48
#define TAPDANCE_EXACT_TERM_ID_MAX  (TAPDANCE_EXACT_TERM_ID_BASE + TAPDANCE_SLOT_COUNT - 1U)

enum
{
  SINGLE_TAP = 1,
  SINGLE_HOLD,
  DOUBLE_TAP,
  DOUBLE_HOLD,
  DOUBLE_SINGLE_TAP,
  MORE_TAPS
};


typedef struct PACKED
{
  uint16_t actions[TAPDANCE_ACTION_COUNT];
  uint16_t term_ms;
} tapdance_slot_storage_t;

typedef struct PACKED
{
  tapdance_slot_storage_t slots[TAPDANCE_SLOT_COUNT];
  uint8_t                 version;
  uint8_t                 reserved[3];
  uint32_t                signature;
} tapdance_storage_t;

typedef struct PACKED
{
  uint16_t hold_ms[TAPDANCE_SLOT_COUNT];
  uint8_t hold_on_other;
  uint8_t version;
  uint16_t reserved;
  uint32_t signature;
} tapdance_timing_storage_t;

_Static_assert(sizeof(tapdance_timing_storage_t) == 24, "Tap Dance timing storage size changed.");

static tapdance_timing_storage_t tapdance_timing;

typedef struct
{
  uint16_t actions[TAPDANCE_ACTION_COUNT];
  uint16_t term_ms;
} tapdance_slot_state_t;

typedef enum
{
  TAPDANCE_ACTION_NONE = 0,
  TAPDANCE_ACTION_TAP,
  TAPDANCE_ACTION_HOLD,
  TAPDANCE_ACTION_DOUBLE_TAP,
  TAPDANCE_ACTION_TAP_HOLD,
} tapdance_action_type_t;

typedef struct
{
  uint8_t slot_index;
} tapdance_user_data_t;

typedef struct
{
  tapdance_action_type_t active_action;
  uint16_t               active_keycode;
  action_t               active_qmk_action;
  bool                   active_is_tap;                   // V251127R1: tap/hold 경로 구분
  bool                   direct;
  uint8_t                mode;
  uint16_t               actions[TAPDANCE_ACTION_COUNT];
  uint8_t                first_tap_mods;
  uint16_t               term_ms;
  uint16_t               hold_ms;
  bool                   hold_on_other;
} tapdance_runtime_state_t;

typedef struct
{
  uint16_t on_tap;
  uint16_t on_hold;
  uint16_t on_double_tap;
  uint16_t on_tap_hold;
  uint16_t term_ms;
} tapdance_entry_t;

static void tapdance_on_each_release(tap_dance_state_t *state, void *user_data);
static void tapdance_on_each_tap(tap_dance_state_t *state, void *user_data);
static void tapdance_on_dance_finished(tap_dance_state_t *state, void *user_data);
static void tapdance_on_reset(tap_dance_state_t *state, void *user_data);

static tapdance_storage_t       tapdance_storage = {0};
static tapdance_slot_state_t    tapdance_state[TAPDANCE_SLOT_COUNT];
static tapdance_runtime_state_t tapdance_runtime[TAP_DANCE_MAX_SIMULTANEOUS];
static tapdance_user_data_t     tapdance_user_data[TAPDANCE_SLOT_COUNT] =
{
  { .slot_index = 0 },
  { .slot_index = 1 },
  { .slot_index = 2 },
  { .slot_index = 3 },
  { .slot_index = 4 },
  { .slot_index = 5 },
  { .slot_index = 6 },
  { .slot_index = 7 },
};

tap_dance_action_t tap_dance_actions[TAPDANCE_SLOT_COUNT] =
{
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, tapdance_on_each_release}, .user_data = &tapdance_user_data[0] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, tapdance_on_each_release}, .user_data = &tapdance_user_data[1] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, tapdance_on_each_release}, .user_data = &tapdance_user_data[2] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, tapdance_on_each_release}, .user_data = &tapdance_user_data[3] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, tapdance_on_each_release}, .user_data = &tapdance_user_data[4] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, tapdance_on_each_release}, .user_data = &tapdance_user_data[5] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, tapdance_on_each_release}, .user_data = &tapdance_user_data[6] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, tapdance_on_each_release}, .user_data = &tapdance_user_data[7] },
};

_Static_assert(sizeof(tapdance_slot_storage_t) == 10, "EECONFIG out of spec.");  // V251124R8: 슬롯 크기 고정
_Static_assert(sizeof(tapdance_storage_t) == 88, "EECONFIG out of spec.");        // V251124R8: USER 데이터 슬롯 크기 고정


EECONFIG_DEBOUNCE_HELPER(tapdance, EECONFIG_USER_TAPDANCE, tapdance_storage);
EECONFIG_DEBOUNCE_HELPER(tapdance_timing, EECONFIG_USER_TAPDANCE_TIMING, tapdance_timing);

static void tapdance_timing_defaults(void)
{
  memset(&tapdance_timing, 0, sizeof(tapdance_timing));
  tapdance_timing.version = 1;
  tapdance_timing.signature = TAPDANCE_TIMING_SIGNATURE;
  eeconfig_flag_tapdance_timing(true);
}



static uint8_t               tapdance_slot_index(uint8_t value_id);
static uint8_t               tapdance_field_index(uint8_t value_id);
static bool                  tapdance_is_exact_term_id(uint8_t value_id);
static bool                  tapdance_is_storage_valid(const tapdance_storage_t *storage);
static uint16_t              tapdance_normalize_term(uint16_t term_ms);
static uint16_t              tapdance_legacy_units(uint16_t term_ms);
static void                  tapdance_apply_defaults_locked(void);
static void                  tapdance_sync_state_from_storage(void);
static void                  tapdance_commit(bool changed);
static bool                  tapdance_keycode_is_valid(uint16_t keycode);
static bool                  tapdance_set_value(uint8_t value_id, uint8_t *value_data, uint8_t length);
static void                  tapdance_get_value(uint8_t value_id, uint8_t *value_data, uint8_t length);
static void                  tapdance_load_entry(uint8_t slot_index, tapdance_entry_t *entry);
static uint8_t               tapdance_step(const tap_dance_state_t *state);
static void                  tapdance_run_action(const tap_dance_state_t *state, uint16_t keycode, action_t action, bool pressed, bool is_tap);
static void                  tapdance_register_keycode(const tap_dance_state_t *state, uint16_t keycode, bool is_tap);
static void                  tapdance_unregister_keycode(const tap_dance_state_t *state, uint16_t keycode, bool is_tap);
static void                  tapdance_tap_keycode(const tap_dance_state_t *state, uint16_t keycode, bool is_tap);
static void                  tapdance_set_runtime(const tap_dance_state_t *state, tapdance_action_type_t action, uint16_t keycode, bool is_tap);
static uint16_t              tapdance_tap_width_ms(uint16_t keycode);                                  // V260911R5: 합성 탭 폭 규칙


void tapdance_init(void)
{
  /* Release executed actions before resetting the runtime metadata. */
  tap_dance_cancel_all();
  memset(tapdance_runtime, 0, sizeof(tapdance_runtime));
  eeconfig_init_tapdance();
  eeconfig_init_tapdance_timing();
  if (tapdance_timing.signature != TAPDANCE_TIMING_SIGNATURE || tapdance_timing.version != 1 || tapdance_timing.reserved != 0)
  {
    tapdance_timing_defaults();
    eeconfig_flush_tapdance_timing(true);
  }

  if (tapdance_is_storage_valid(&tapdance_storage) == false)
  {
    tapdance_apply_defaults_locked();                      // V251124R8: 손상 슬롯 기본값 복원
    eeconfig_flush_tapdance(true);
  }

  tapdance_sync_state_from_storage();
}

bool tapdance_handle_via_command(uint8_t *data, uint8_t length)
{
  if (data == NULL || length < 4U)
  {
    return false;
  }

  uint8_t *command_id = &(data[0]);
  uint8_t *value_id   = &(data[2]);
  uint8_t *value_data = &(data[3]);
  bool     handled    = false;

  if ((*command_id == id_custom_set_value || *command_id == id_custom_get_value) &&
      (tapdance_is_exact_term_id(*value_id) || (*value_id >= TAPDANCE_HOLD_TERM_ID_BASE && *value_id < TAPDANCE_HOLD_OTHER_ID_BASE)) && length < 5U)
  {
    *command_id = id_unhandled;
    return false;
  }

  switch (*command_id)
  {
    case id_custom_set_value:
      handled = tapdance_set_value(*value_id, value_data, length);
      if (handled)
      {
        tapdance_get_value(*value_id, value_data, length);  // V251124R8: VIA echo 유지
      }
      break;

    case id_custom_get_value:
      tapdance_get_value(*value_id, value_data, length);
      handled = true;
      break;

    case id_custom_save:
      tapdance_storage_flush(true);
      handled = true;
      break;

    default:
      handled = false;
      break;
  }

  if (handled == false)
  {
    *command_id = id_unhandled;
  }
  return handled;
}

void tapdance_storage_stage_defaults(void)
{
  tapdance_storage_t previous = tapdance_storage;
  tapdance_timing_storage_t previous_timing = tapdance_timing;
  uint8_t previous_dirty = dirty_tapdance;
  uint8_t previous_timing_dirty = dirty_tapdance_timing;
  // Held actions remain owned by the current runtime until the complete reset is published.
  tapdance_apply_defaults_locked();
  tapdance_timing_defaults();
  tapdance_storage_flush(true);
  tapdance_storage = previous;
  tapdance_timing = previous_timing;
  dirty_tapdance = previous_dirty;
  dirty_tapdance_timing = previous_timing_dirty;
}

void tapdance_storage_apply_defaults(void)
{
  /* A reset retires held dances while their executed actions still exist. */
  tap_dance_cancel_all();
  memset(tapdance_runtime, 0, sizeof(tapdance_runtime));
  tapdance_apply_defaults_locked();                        // V251124R8: USER 초기화 시 기본값 기록
  tapdance_timing_defaults();
  tapdance_sync_state_from_storage();
}

void tapdance_storage_flush(bool force)
{
  eeconfig_flush_tapdance(force);
  eeconfig_flush_tapdance_timing(force);
}

uint16_t tapdance_get_term_ms(uint16_t keycode)
{
  uint8_t slot_index = QK_TAP_DANCE_GET_INDEX(keycode);
  uint16_t term_ms;

  if (slot_index >= TAPDANCE_SLOT_COUNT)
  {
    return ERA_TERM_DEFAULT_MS;
  }

  term_ms = tapdance_state[slot_index].term_ms;
  if (!era_term_exact_valid(term_ms))
  {
    term_ms = ERA_TERM_DEFAULT_MS;                       // V251124R8: 비정상 값 방어
  }
  return term_ms;
}

/* One edge of an executed action. The record carries the dance's own position
 * as a tick event: position-keyed QMK state such as retro tapping sees this
 * input, never whatever sits at (0,0), and no dance takes it for an input. A
 * keycode without a QMK action runs through the quantum handlers. */
static void tapdance_run_action(const tap_dance_state_t *state, uint16_t keycode, action_t action, bool pressed, bool is_tap)
{
  keyrecord_t record = {0};

  record.event.key = state->key;
  record.event.pressed = pressed;
  record.event.time = timer_read32();
#ifndef NO_ACTION_TAPPING
  record.tap.count = is_tap ? 1U : 0U;
#endif
#if defined(COMBO_ENABLE) || defined(REPEAT_KEY_ENABLE)
  record.keycode = keycode;
#endif

  if (action.code == ACTION_NO)
  {
    tap_dance_run_quantum_keycode(&record, keycode);
  }
  else
  {
    process_action(&record, action);
  }
}

static void tapdance_register_keycode(const tap_dance_state_t *state, uint16_t keycode, bool is_tap)
{
  if (tapdance_keycode_is_valid(keycode))
  {
    tapdance_run_action(state, keycode, action_for_keycode(keycode), true, is_tap);
  }
}

static void tapdance_unregister_keycode(const tap_dance_state_t *state, uint16_t keycode, bool is_tap)
{
  if (tapdance_keycode_is_valid(keycode))
  {
    tapdance_run_action(state, keycode, action_for_keycode(keycode), false, is_tap);
  }
}

static void tapdance_tap_keycode(const tap_dance_state_t *state, uint16_t keycode, bool is_tap)
{
  if (tapdance_keycode_is_valid(keycode) == false)
  {
    return;
  }

  tapdance_register_keycode(state, keycode, is_tap);
  // V260911R5: 합성 탭의 폭은 어느 경로가 만들었든 QMK 규칙 하나다(tapdance_tap_width_ms).
  // V260911R3: TD와 LT의 호스트 유지 시간을 같은 전송 계층에 맡긴다.
  tap_code_wait(keycode, tapdance_tap_width_ms(keycode));
  tapdance_unregister_keycode(state, keycode, is_tap);
}

// V260911R5: 합성 탭의 최소 유지 시간. QMK의 tap_code()와 LT/MT가 쓰는 규칙 그대로라 같은 keycode가
// TD 슬롯에서도 같은 폭을 갖는다. Caps Lock은 macOS가 짧은 탭을 무시하므로 TAP_HOLD_CAPS_DELAY다.
static uint16_t tapdance_tap_width_ms(uint16_t keycode)
{
  return (keycode == KC_CAPS_LOCK) ? TAP_HOLD_CAPS_DELAY : TAP_CODE_DELAY;
}

static void tapdance_set_runtime(const tap_dance_state_t *state, tapdance_action_type_t action, uint16_t keycode, bool is_tap)
{
  if (state->runtime_index >= TAP_DANCE_MAX_SIMULTANEOUS)
  {
    return;
  }

  tapdance_runtime[state->runtime_index].active_action  = action;
  tapdance_runtime[state->runtime_index].active_keycode = keycode;
  tapdance_runtime[state->runtime_index].active_qmk_action = action_for_keycode(keycode);
  tapdance_runtime[state->runtime_index].active_is_tap  = is_tap;    // V251127R1: release 경로 보존
}

/* The tag keeps untouched records in their original fallback semantics. */
static uint8_t tapdance_mode(uint8_t slot)
{
  if (slot >= TAPDANCE_SLOT_COUNT || tapdance_storage.reserved[1] != TAPDANCE_MODE_TAG) return 0;
  return (tapdance_storage.reserved[slot < 4U ? 0 : 2] >> ((slot % 4U) * 2U)) & 3U;
}

static void tapdance_store_mode(uint8_t slot, uint8_t mode)
{
  if (tapdance_storage.reserved[1] != TAPDANCE_MODE_TAG) memset(tapdance_storage.reserved, 0, sizeof(tapdance_storage.reserved));
  tapdance_storage.reserved[1] = TAPDANCE_MODE_TAG;
  uint8_t *bits = &tapdance_storage.reserved[slot < 4U ? 0 : 2];
  uint8_t shift = (slot % 4U) * 2U;
  *bits = (*bits & ~(3U << shift)) | (mode << shift);
}

static bool tapdance_has_override(const tapdance_runtime_state_t *runtime, uint8_t action)
{
  return runtime->actions[action] != KC_TRANSPARENT;
}

uint16_t tapdance_decision_term(uint8_t slot, uint8_t runtime_index, bool pressed)
{
  if (runtime_index < TAP_DANCE_MAX_SIMULTANEOUS && tapdance_runtime[runtime_index].mode != 0U)
  {
    const tapdance_runtime_state_t *runtime = &tapdance_runtime[runtime_index];
    return pressed ? runtime->hold_ms : runtime->term_ms;
  }
  return tapdance_get_term_ms(TD(slot));
}

bool tapdance_hold_on_interrupt(uint8_t runtime_index, uint8_t count, bool pressed)
{
  if (!pressed || runtime_index >= TAP_DANCE_MAX_SIMULTANEOUS) return false;
  const tapdance_runtime_state_t *runtime = &tapdance_runtime[runtime_index];
  if (runtime->mode == 0U || !runtime->hold_on_other) return false;
  if (count == 1U) return !runtime->direct && tapdance_has_override(runtime, 1);
  return count == 2U && (tapdance_has_override(runtime, 3) || tapdance_has_override(runtime, 1));
}

/* Release an already-sent physical edge without synthesizing a tap width. */
static void tapdance_on_each_release(tap_dance_state_t *state, void *user_data)
{
  (void)user_data;
  tapdance_runtime_state_t *runtime = &tapdance_runtime[state->runtime_index];
  if (!runtime->direct || state->finished) return;
  if (state->count == 1U && tapdance_keycode_is_valid(runtime->active_keycode))
  {
    tapdance_run_action(state, runtime->active_keycode, runtime->active_qmk_action, false, true);
    runtime->active_keycode = KC_NO;
    runtime->active_action = TAPDANCE_ACTION_NONE;
  }
}

static void tapdance_on_each_tap(tap_dance_state_t *state, void *user_data)
{
  tapdance_user_data_t *user = (tapdance_user_data_t *)user_data;
  tapdance_entry_t      entry = {0};

  if (user == NULL || user->slot_index >= TAPDANCE_SLOT_COUNT)
  {
    return;
  }

  if (state->count == 1U && state->runtime_index < TAP_DANCE_MAX_SIMULTANEOUS)
  {
    /* A Double Hold's embedded tap stands for this first press. */
    tapdance_runtime[state->runtime_index].first_tap_mods = state->weak_mods;
  }
  tapdance_load_entry(user->slot_index, &entry);
  tapdance_runtime_state_t *runtime = &tapdance_runtime[state->runtime_index];
  if (state->count == 1U)
  {
    runtime->mode = tapdance_mode(user->slot_index);
    runtime->term_ms = entry.term_ms;
    runtime->hold_ms = tapdance_timing.hold_ms[user->slot_index] ? tapdance_timing.hold_ms[user->slot_index] : entry.term_ms;
    runtime->hold_on_other = (tapdance_timing.hold_on_other >> user->slot_index) & 1U;
    memcpy(runtime->actions, tapdance_state[user->slot_index].actions, sizeof(runtime->actions));
    runtime->direct = runtime->mode == 2U || (runtime->mode == 1U &&
        !tapdance_has_override(runtime, 1) && !tapdance_has_override(runtime, 2) && !tapdance_has_override(runtime, 3));
  }
  if (runtime->direct)
  {
    if (state->count == 1U && tapdance_keycode_is_valid(entry.on_tap))
    {
      tapdance_set_runtime(state, TAPDANCE_ACTION_TAP, entry.on_tap, true);
      tapdance_register_keycode(state, entry.on_tap, true);
    }
    if (state->count == 1U)
    {
      if (!tapdance_has_override(runtime, 2) && !tapdance_has_override(runtime, 3)) state->finished = true;
      /* This edge already consumed its modifiers; finishing the first press
       * must not replay them after their physical owner has released. */
      state->weak_mods = 0;
#ifndef NO_ACTION_ONESHOT
      state->oneshot_mods = 0;
#endif
    }
    return;
  }
  if (!tapdance_keycode_is_valid(entry.on_tap))
  {
    return;
  }

  if (state->count == 3U)
  {
    tapdance_tap_keycode(state, entry.on_tap, true);
    tapdance_tap_keycode(state, entry.on_tap, true);
    tapdance_tap_keycode(state, entry.on_tap, true);
  }
  else if (state->count > 3U)
  {
    tapdance_tap_keycode(state, entry.on_tap, true);
  }
}

static void tapdance_on_dance_finished(tap_dance_state_t *state, void *user_data)
{
  tapdance_user_data_t *user = (tapdance_user_data_t *)user_data;
  tapdance_entry_t      entry = {0};
  uint8_t               slot_index;
  uint8_t               step;
  tapdance_runtime_state_t *runtime;

  if (user == NULL)
  {
    return;
  }

  slot_index = user->slot_index;
  if (slot_index >= TAPDANCE_SLOT_COUNT)
  {
    return;
  }

  tapdance_load_entry(slot_index, &entry);
  step = tapdance_step(state);
  runtime = &tapdance_runtime[state->runtime_index];

  if (runtime->mode != 0U)
  {
    /* Immediate mode has already emitted the first physical press. All
     * other decisions use the captured actions, including explicit KC_NO. */
    if (runtime->direct && state->count == 1U) return;
    bool hold = state->pressed && !state->interrupted;
    uint16_t keycode = runtime->actions[0];
    bool prefix_tap = false;
    if (state->count == 1U)
    {
      if (hold && tapdance_has_override(runtime, 1)) keycode = runtime->actions[1];
    }
    else if (state->count == 2U)
    {
      uint8_t role = hold ? 3 : 2;
      if (!state->interrupted && tapdance_has_override(runtime, role)) keycode = runtime->actions[role];
      else
      {
        prefix_tap = !runtime->direct;
        if (hold && tapdance_has_override(runtime, 1)) keycode = runtime->actions[1];
      }
    }
    else return;
    if (prefix_tap)
    {
      uint8_t mods = runtime->first_tap_mods | state->weak_mods;
      add_weak_mods(mods);
      tapdance_tap_keycode(state, runtime->actions[0], true);
      del_weak_mods(mods);
      if (!hold) add_weak_mods(state->weak_mods);
      send_keyboard_report();
    }
    tapdance_set_runtime(state, hold ? TAPDANCE_ACTION_HOLD : TAPDANCE_ACTION_TAP, keycode, !hold);
    tapdance_register_keycode(state, keycode, !hold);
    return;
  }

  runtime->active_action  = TAPDANCE_ACTION_NONE;
  runtime->active_keycode = KC_NO;
  runtime->active_is_tap  = false;

  switch (step)
  {
    case SINGLE_TAP:
      if (tapdance_keycode_is_valid(entry.on_tap))
      {
        tapdance_register_keycode(state, entry.on_tap, true);
        tapdance_set_runtime(state, TAPDANCE_ACTION_TAP, entry.on_tap, true);
      }
      break;

    case SINGLE_HOLD:
      if (tapdance_keycode_is_valid(entry.on_hold))
      {
        tapdance_register_keycode(state, entry.on_hold, false);
        tapdance_set_runtime(state, TAPDANCE_ACTION_HOLD, entry.on_hold, false);
      }
      else if (tapdance_keycode_is_valid(entry.on_tap))
      {
        tapdance_register_keycode(state, entry.on_tap, false);
        tapdance_set_runtime(state, TAPDANCE_ACTION_HOLD, entry.on_tap, false);
      }
      break;

    case DOUBLE_TAP:
      if (tapdance_keycode_is_valid(entry.on_double_tap))
      {
        tapdance_register_keycode(state, entry.on_double_tap, true);
        tapdance_set_runtime(state, TAPDANCE_ACTION_DOUBLE_TAP, entry.on_double_tap, true);
      }
      else if (tapdance_keycode_is_valid(entry.on_tap))
      {
        tapdance_tap_keycode(state, entry.on_tap, true);
        tapdance_register_keycode(state, entry.on_tap, true);               // V251127R1: Vial 폴백 유지 + tap 경로 적용
        tapdance_set_runtime(state, TAPDANCE_ACTION_DOUBLE_TAP, entry.on_tap, true);
      }
      break;

    case DOUBLE_HOLD:
      if (tapdance_keycode_is_valid(entry.on_tap_hold))
      {
        tapdance_register_keycode(state, entry.on_tap_hold, false);
        tapdance_set_runtime(state, TAPDANCE_ACTION_TAP_HOLD, entry.on_tap_hold, false);
      }
      else
      {
        if (tapdance_keycode_is_valid(entry.on_tap))
        {
          /* The embedded tap stands for the first press and, like any tap,
           * carries the modifiers held at either press. The hold that follows
           * keeps only live owners. */
          const uint8_t mods = runtime->first_tap_mods | state->weak_mods;

          add_weak_mods(mods);
          tapdance_tap_keycode(state, entry.on_tap, true);
          del_weak_mods(mods);
          send_keyboard_report();
        }

        if (tapdance_keycode_is_valid(entry.on_hold))
        {
          tapdance_register_keycode(state, entry.on_hold, false);
          tapdance_set_runtime(state, TAPDANCE_ACTION_TAP_HOLD, entry.on_hold, false);
        }
        else if (tapdance_keycode_is_valid(entry.on_tap))
        {
          tapdance_register_keycode(state, entry.on_tap, false);
          tapdance_set_runtime(state, TAPDANCE_ACTION_TAP_HOLD, entry.on_tap, false);
        }
      }
      break;

    case DOUBLE_SINGLE_TAP:
      if (tapdance_keycode_is_valid(entry.on_tap))
      {
        tapdance_tap_keycode(state, entry.on_tap, true);
        tapdance_register_keycode(state, entry.on_tap, true);
        tapdance_set_runtime(state, TAPDANCE_ACTION_DOUBLE_TAP, entry.on_tap, true);
      }
      break;

    case MORE_TAPS:
    default:
      break;
  }
}

static void tapdance_on_reset(tap_dance_state_t *state, void *user_data)
{
  tapdance_user_data_t *user = (tapdance_user_data_t *)user_data;
  tapdance_runtime_state_t *runtime;

  (void)state;

  if (user == NULL)
  {
    return;
  }

  runtime = &tapdance_runtime[state->runtime_index];

  if (tapdance_keycode_is_valid(runtime->active_keycode))
  {
    // V260911R5: 판정이 등록한 탭은 합성 폭만큼 유지를 요청한다. 물리 홀드의 해제는 스위치가 이미 폭을 준 것이라 요청하지 않는다.
    // V260911R3: 논리 해제는 지금 완료한다. 예약된 USB 리포트가 이후 입력의 키 상태를 건드리지 않는다.
    if (runtime->active_is_tap && !(runtime->direct && state->count == 1U))
    {
      tap_code_wait(runtime->active_keycode, tapdance_tap_width_ms(runtime->active_keycode));
    }
    /* Each execution receives its own up; resource ownership preserves peers. */
    tapdance_run_action(state, runtime->active_keycode, runtime->active_qmk_action, false, runtime->active_is_tap);
  }

  runtime->active_action  = TAPDANCE_ACTION_NONE;
  runtime->active_keycode = KC_NO;
  runtime->active_is_tap  = false;
  runtime->direct = false;
  runtime->mode = 0;
}

static bool tapdance_is_exact_term_id(uint8_t value_id)
{
  return value_id >= TAPDANCE_EXACT_TERM_ID_BASE && value_id <= TAPDANCE_EXACT_TERM_ID_MAX;
}

static uint8_t tapdance_slot_index(uint8_t value_id)
{
  if (tapdance_is_exact_term_id(value_id))
  {
    return (uint8_t)(value_id - TAPDANCE_EXACT_TERM_ID_BASE);
  }
  if (value_id < 1U || value_id > TAPDANCE_VALUE_MAX_ID)
  {
    return TAPDANCE_SLOT_COUNT;
  }
  return (uint8_t)((value_id - 1U) / TAPDANCE_VALUE_STRIDE);
}

static uint8_t tapdance_field_index(uint8_t value_id)
{
  return (uint8_t)((value_id - 1U) % TAPDANCE_VALUE_STRIDE);
}

static bool tapdance_is_storage_valid(const tapdance_storage_t *storage)
{
  if (storage->signature != TAPDANCE_SIGNATURE)
  {
    return false;
  }
  if (storage->version != TAPDANCE_VERSION)
  {
    return false;
  }

  for (uint8_t i = 0; i < TAPDANCE_SLOT_COUNT; i++)
  {
    if (!era_term_exact_valid(storage->slots[i].term_ms))
    {
      return false;
    }
  }
  if (storage->reserved[1] != TAPDANCE_MODE_TAG &&
      (storage->reserved[0] != 0U || storage->reserved[1] != 0U || storage->reserved[2] != 0U)) return false;
  if (storage->reserved[1] == TAPDANCE_MODE_TAG)
  {
    for (uint8_t slot = 0; slot < TAPDANCE_SLOT_COUNT; ++slot)
    {
      uint8_t mode = (storage->reserved[slot < 4U ? 0 : 2] >> ((slot % 4U) * 2U)) & 3U;
      if (mode == 3U || (mode == 2U && storage->slots[slot].actions[1] != KC_TRANSPARENT)) return false;
    }
  }
  return true;
}

static uint16_t tapdance_normalize_term(uint16_t term_ms)
{
  return era_term_legacy_normalize(term_ms);
}

static uint16_t tapdance_legacy_units(uint16_t term_ms)
{
  return era_term_legacy_units(term_ms);
}

static void tapdance_apply_defaults_locked(void)
{
  for (uint8_t i = 0; i < TAPDANCE_SLOT_COUNT; i++)
  {
    for (uint8_t a = 0; a < TAPDANCE_ACTION_COUNT; a++)
    {
      tapdance_storage.slots[i].actions[a] = KC_NO;
    }
    tapdance_storage.slots[i].term_ms = ERA_TERM_DEFAULT_MS;
  }

  memset(tapdance_storage.reserved, 0, sizeof(tapdance_storage.reserved));
  tapdance_storage.version   = TAPDANCE_VERSION;
  tapdance_storage.signature = TAPDANCE_SIGNATURE;
  eeconfig_flag_tapdance(true);
}

static void tapdance_sync_state_from_storage(void)
{
  for (uint8_t i = 0; i < TAPDANCE_SLOT_COUNT; i++)
  {
    tapdance_slot_storage_t *slot_storage = &tapdance_storage.slots[i];
    tapdance_slot_state_t   *slot_state   = &tapdance_state[i];

    slot_state->term_ms = slot_storage->term_ms;                // V260821R1: load/sync에서 20ms 격자로 되돌리지 않는다
    for (uint8_t a = 0; a < TAPDANCE_ACTION_COUNT; a++)
    {
      slot_state->actions[a] = slot_storage->actions[a];
    }
  }
}

static void tapdance_commit(bool changed)
{
  tapdance_sync_state_from_storage();
  if (changed)
  {
    eeconfig_flag_tapdance(true);
    era_state_sync_bump_config();  // V260821R1: GET이 새 값을 돌려준 뒤에만 CONFIG revision
  }
}

static bool tapdance_keycode_is_valid(uint16_t keycode)
{
  if (keycode == KC_NO || keycode == KC_TRANSPARENT)
  {
    return false;
  }
  return true;
}

bool tapdance_should_finish_immediate(uint8_t slot_index, uint8_t tap_count, uint8_t runtime_index)
{
  if (slot_index >= TAPDANCE_SLOT_COUNT)
  {
    return false;
  }

  if (runtime_index < TAP_DANCE_MAX_SIMULTANEOUS && tapdance_runtime[runtime_index].mode != 0U)
  {
    const tapdance_runtime_state_t *runtime = &tapdance_runtime[runtime_index];
    return tap_count == 2U || (tap_count == 1U && !tapdance_has_override(runtime, 2) && !tapdance_has_override(runtime, 3));
  }

  tapdance_slot_state_t *slot_state = &tapdance_state[slot_index];
  bool has_double   = tapdance_keycode_is_valid(slot_state->actions[2]);
  bool has_tap_hold = tapdance_keycode_is_valid(slot_state->actions[3]);

  /* A release decides a dance as soon as no later press could change the
   * outcome: without On Double Tap and On Tap-Hold the first release is a
   * single tap, and with On Double Tap the second release is a double tap.
   * Vial waits for Term in both cases; deciding at the release removes only
   * that delay. Term still decides every hold. */
  if (tap_count == 1U && has_double == false && has_tap_hold == false)
  {
    return true;
  }
  if (tap_count == 2U && has_double)
  {
    return true;
  }

  return false;
}

static bool tapdance_set_value(uint8_t value_id, uint8_t *value_data, uint8_t length)
{
  uint8_t  slot_index;
  uint8_t  field_index;
  uint16_t term_ms;
  uint16_t keycode;
  bool     changed = false;

  if (value_data == NULL || length < 4U)
  {
    return false;                                               // V251124R8: VIA 패킷 최소 길이 확인
  }

  if (value_id >= TAPDANCE_HOLD_TERM_ID_BASE && value_id < TAPDANCE_HOLD_OTHER_ID_BASE + TAPDANCE_SLOT_COUNT)
  {
    if (value_id < TAPDANCE_HOLD_OTHER_ID_BASE)
    {
      if (length < 5U) return false;
      slot_index = value_id - TAPDANCE_HOLD_TERM_ID_BASE;
      term_ms = ((uint16_t)value_data[0] << 8) | value_data[1];
      changed = tapdance_timing.hold_ms[slot_index] != term_ms;
      tapdance_timing.hold_ms[slot_index] = term_ms;
    }
    else
    {
      if (value_data[0] > 1U) return false;
      uint8_t mask = 1U << (value_id - TAPDANCE_HOLD_OTHER_ID_BASE);
      uint8_t next = value_data[0] ? tapdance_timing.hold_on_other | mask : tapdance_timing.hold_on_other & ~mask;
      changed = next != tapdance_timing.hold_on_other;
      tapdance_timing.hold_on_other = next;
    }
    if (changed) { eeconfig_flag_tapdance_timing(true); era_state_sync_bump_config(); }
    return true;
  }

  if (value_id >= TAPDANCE_MODE_ID_BASE && value_id < TAPDANCE_MODE_ID_BASE + TAPDANCE_SLOT_COUNT)
  {
    slot_index = value_id - TAPDANCE_MODE_ID_BASE;
    if (value_data[0] > 2U || (value_data[0] == 2U && tapdance_storage.slots[slot_index].actions[1] != KC_TRANSPARENT)) return false;
    changed = tapdance_mode(slot_index) != value_data[0];
    tapdance_store_mode(slot_index, value_data[0]);
    tapdance_commit(changed);
    return true;
  }

  if (tapdance_is_exact_term_id(value_id))
  {
    if (length < 5U)
    {
      return false;
    }
    slot_index = tapdance_slot_index(value_id);
    term_ms    = ((uint16_t)value_data[0] << 8) | (uint16_t)value_data[1];
    if (!era_term_exact_valid(term_ms))
    {
      return false;
    }
    changed = (tapdance_storage.slots[slot_index].term_ms != term_ms);
    tapdance_storage.slots[slot_index].term_ms = term_ms;
    tapdance_commit(changed);
    return true;
  }

  slot_index = tapdance_slot_index(value_id);
  field_index = tapdance_field_index(value_id);

  if (slot_index >= TAPDANCE_SLOT_COUNT)
  {
    return false;
  }

  if (field_index < TAPDANCE_ACTION_COUNT)
  {
    keycode = ((uint16_t)value_data[0] << 8) | (uint16_t)value_data[1];
    if (field_index == 1U && tapdance_mode(slot_index) == 2U && keycode != KC_TRANSPARENT) return false;
    changed = (tapdance_storage.slots[slot_index].actions[field_index] != keycode);
    tapdance_storage.slots[slot_index].actions[field_index] = keycode;
  }
  else if (field_index == TAPDANCE_FIELD_TERM)
  {
    term_ms = tapdance_normalize_term((uint16_t)value_data[0] * ERA_TERM_LEGACY_UNIT_MS);
    changed = (tapdance_storage.slots[slot_index].term_ms != term_ms);
    tapdance_storage.slots[slot_index].term_ms = term_ms;
  }
  else
  {
    return false;
  }

  tapdance_commit(changed);
  return true;
}

static void tapdance_get_value(uint8_t value_id, uint8_t *value_data, uint8_t length)
{
  uint8_t slot_index;
  uint8_t field_index;

  if (value_data == NULL || length < 4U)
  {
    return;                                                    // V251124R8: VIA 응답 버퍼 최소 길이 확인
  }

  if (value_id >= TAPDANCE_HOLD_TERM_ID_BASE && value_id < TAPDANCE_HOLD_OTHER_ID_BASE)
  {
    if (length < 5U) return;
    uint16_t value = tapdance_timing.hold_ms[value_id - TAPDANCE_HOLD_TERM_ID_BASE];
    value_data[0] = value >> 8;
    value_data[1] = value & 0xff;
    if (length >= 6U) value_data[2] = TAPDANCE_TIMING_TAG;
    return;
  }
  if (value_id >= TAPDANCE_HOLD_OTHER_ID_BASE && value_id < TAPDANCE_HOLD_OTHER_ID_BASE + TAPDANCE_SLOT_COUNT)
  {
    value_data[0] = (tapdance_timing.hold_on_other >> (value_id - TAPDANCE_HOLD_OTHER_ID_BASE)) & 1U;
    if (length >= 5U) value_data[1] = TAPDANCE_TIMING_TAG;
    return;
  }
  if (value_id >= TAPDANCE_MODE_ID_BASE && value_id < TAPDANCE_MODE_ID_BASE + TAPDANCE_SLOT_COUNT)
  {
    value_data[0] = tapdance_mode(value_id - TAPDANCE_MODE_ID_BASE);
    if (length >= 5U) value_data[1] = TAPDANCE_MODE_TAG;
    if (length >= 6U) value_data[2] = TAPDANCE_TIMING_TAG;
    return;
  }

  if (tapdance_is_exact_term_id(value_id))
  {
    uint16_t term_ms;

    slot_index = tapdance_slot_index(value_id);
    term_ms    = tapdance_state[slot_index].term_ms;
    value_data[0] = (uint8_t)(term_ms >> 8);
    if (length >= 5U)
    {
      value_data[1] = (uint8_t)(term_ms & 0xFF);
    }
    return;
  }

  slot_index = tapdance_slot_index(value_id);
  field_index = tapdance_field_index(value_id);

  if (slot_index >= TAPDANCE_SLOT_COUNT)
  {
    value_data[0] = 0U;
    value_data[1] = 0U;
    return;
  }

  if (field_index < TAPDANCE_ACTION_COUNT)
  {
    uint16_t keycode = tapdance_state[slot_index].actions[field_index];
    value_data[0] = (uint8_t)(keycode >> 8);
    value_data[1] = (uint8_t)(keycode & 0xFF);
  }
  else if (field_index == TAPDANCE_FIELD_TERM)
  {
    value_data[0] = (uint8_t)tapdance_legacy_units(tapdance_state[slot_index].term_ms);  // V260821R1: floor-20ms
    value_data[1] = 0U;
  }
  else
  {
    value_data[0] = 0U;
    value_data[1] = 0U;
  }
}

static void tapdance_load_entry(uint8_t slot_index, tapdance_entry_t *entry)
{
  if (entry == NULL || slot_index >= TAPDANCE_SLOT_COUNT)
  {
    return;
  }

  entry->on_tap        = tapdance_state[slot_index].actions[0];
  entry->on_hold       = tapdance_state[slot_index].actions[1];
  entry->on_double_tap = tapdance_state[slot_index].actions[2];
  entry->on_tap_hold   = tapdance_state[slot_index].actions[3];
  entry->term_ms       = tapdance_state[slot_index].term_ms;
}

static uint8_t tapdance_step(const tap_dance_state_t *state)
{
  if (state == NULL)
  {
    return SINGLE_TAP;
  }

  if (state->count == 1U)
  {
    if (state->interrupted || !state->pressed)
    {
      return SINGLE_TAP;
    }
    return SINGLE_HOLD;
  }
  else if (state->count == 2U)
  {
    if (state->interrupted)
    {
      return DOUBLE_SINGLE_TAP;
    }
    if (state->pressed)
    {
      return DOUBLE_HOLD;
    }
    return DOUBLE_TAP;
  }

  return MORE_TAPS;
}

#endif
