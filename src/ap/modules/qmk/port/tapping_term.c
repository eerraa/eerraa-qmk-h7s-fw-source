#include "tapping_term.h"
#include "tapping_term_policy.h"

#ifdef G_TERM_ENABLE

#include "port.h"
#include "quantum.h"
#include "era_state_sync.h"  // V260821R1: exact/legacy term 변경 시 CONFIG revision


#define TAPPING_TERM_SIGNATURE      (0x50415447UL)   // "GTAP"
#define TAPPING_TERM_VERSION        (1U)


typedef struct
{
  uint16_t tapping_term_ms;
  uint8_t  permissive_hold;
  uint8_t  hold_on_other_key_press;
  uint8_t  retro_tapping;
  uint8_t  version;
  uint32_t signature;
} tapping_term_storage_t;

_Static_assert(sizeof(tapping_term_storage_t) == 12, "EECONFIG out of spec.");  // V251123R4: 슬롯 크기 고정


typedef struct
{
  uint16_t tapping_term_ms;
  bool     permissive_hold;
  bool     hold_on_other_key_press;
  bool     retro_tapping;
} tapping_term_state_t;


static tapping_term_storage_t tapping_term_storage = {0};
static tapping_term_state_t   tapping_term_state =
{
  .tapping_term_ms         = ERA_TERM_DEFAULT_MS,
  .permissive_hold         = false,
  .hold_on_other_key_press = false,
  .retro_tapping           = false,
};


EECONFIG_DEBOUNCE_HELPER(tapping_term, EECONFIG_USER_TAPPING_TERM, tapping_term_storage);


static uint16_t tapping_term_normalize(uint16_t term_ms);
static uint16_t tapping_term_legacy_units(uint16_t term_ms);
static bool     tapping_term_is_storage_valid(const tapping_term_storage_t *storage);
static void     tapping_term_apply_defaults_locked(void);
static void     tapping_term_sync_state_from_storage(void);
static void     tapping_term_commit(bool changed);
static bool     tapping_term_set_value(uint8_t id, uint8_t *value_data, uint8_t length);
static void     tapping_term_get_value(uint8_t id, uint8_t *value_data, uint8_t length);


void tapping_term_init(void)
{
  eeconfig_init_tapping_term();

  if (tapping_term_is_storage_valid(&tapping_term_storage) == false)
  {
    tapping_term_apply_defaults_locked();                   // V251123R4: 손상 슬롯 복원
    eeconfig_flush_tapping_term(true);
  }

  tapping_term_sync_state_from_storage();
}

bool tapping_term_handle_via_command(uint8_t *data, uint8_t length)
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
      *value_id == id_qmk_tapping_global_term_exact && length < 5U)
  {
    *command_id = id_unhandled;
    return false;
  }

  switch (*command_id)
  {
    case id_custom_set_value:
      handled = tapping_term_set_value(*value_id, value_data, length);
      if (handled)
      {
        tapping_term_get_value(*value_id, value_data, length);      // V251123R4: VIA echo 유지
      }
      break;

    case id_custom_get_value:
      tapping_term_get_value(*value_id, value_data, length);
      handled = true;
      break;

    case id_custom_save:
      tapping_term_storage_flush(true);
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

void tapping_term_storage_stage_defaults(void)
{
  tapping_term_storage_t previous = tapping_term_storage;
  uint8_t previous_dirty = dirty_tapping_term;
  tapping_term_apply_defaults_locked();
  eeconfig_flush_tapping_term(true);
  tapping_term_storage = previous;
  dirty_tapping_term = previous_dirty;
}

void tapping_term_storage_apply_defaults(void)
{
  tapping_term_apply_defaults_locked();                     // V251123R4: EEPROM 초기화 시 기본값 적용
  tapping_term_sync_state_from_storage();
}

void tapping_term_storage_flush(bool force)
{
  eeconfig_flush_tapping_term(force);
}

uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;

  return tapping_term_state.tapping_term_ms;
}

bool get_permissive_hold(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;

  return tapping_term_state.permissive_hold;
}

bool get_hold_on_other_key_press(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;

  return tapping_term_state.hold_on_other_key_press;
}

bool get_retro_tapping(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;

  return tapping_term_state.retro_tapping;
}

static uint16_t tapping_term_normalize(uint16_t term_ms)
{
  return era_term_legacy_normalize(term_ms);
}

static uint16_t tapping_term_legacy_units(uint16_t term_ms)
{
  return era_term_legacy_units(term_ms);
}

static bool tapping_term_is_storage_valid(const tapping_term_storage_t *storage)
{
  if (storage->signature != TAPPING_TERM_SIGNATURE)
  {
    return false;
  }
  if (storage->version != TAPPING_TERM_VERSION)
  {
    return false;
  }
  if (!era_term_exact_valid(storage->tapping_term_ms))
  {
    return false;
  }
  if (storage->permissive_hold > 1U || storage->hold_on_other_key_press > 1U || storage->retro_tapping > 1U)
  {
    return false;
  }
  return true;
}

static void tapping_term_apply_defaults_locked(void)
{
  tapping_term_storage.tapping_term_ms         = ERA_TERM_DEFAULT_MS;
  tapping_term_storage.permissive_hold         = 0U;
  tapping_term_storage.hold_on_other_key_press = 0U;
  tapping_term_storage.retro_tapping           = 0U;
  tapping_term_storage.version                 = TAPPING_TERM_VERSION;
  tapping_term_storage.signature               = TAPPING_TERM_SIGNATURE;
  eeconfig_flag_tapping_term(true);
}

static void tapping_term_sync_state_from_storage(void)
{
  bool dirty = false;

  tapping_term_state.tapping_term_ms         = tapping_term_storage.tapping_term_ms;  // V260821R1: load/sync에서 20ms 격자로 되돌리지 않는다
  tapping_term_state.permissive_hold         = tapping_term_storage.permissive_hold != 0U;
  tapping_term_state.hold_on_other_key_press = tapping_term_storage.hold_on_other_key_press != 0U;
  tapping_term_state.retro_tapping           = tapping_term_storage.retro_tapping != 0U;

  uint8_t permissive_hold = tapping_term_state.permissive_hold ? 1U : 0U;
  uint8_t hold_on_other   = tapping_term_state.hold_on_other_key_press ? 1U : 0U;
  uint8_t retro_tapping   = tapping_term_state.retro_tapping ? 1U : 0U;

  if (tapping_term_storage.permissive_hold != permissive_hold)
  {
    tapping_term_storage.permissive_hold = permissive_hold;
    dirty = true;
  }
  if (tapping_term_storage.hold_on_other_key_press != hold_on_other)
  {
    tapping_term_storage.hold_on_other_key_press = hold_on_other;
    dirty = true;
  }
  if (tapping_term_storage.retro_tapping != retro_tapping)
  {
    tapping_term_storage.retro_tapping = retro_tapping;
    dirty = true;
  }

  if (dirty)
  {
    eeconfig_flag_tapping_term(true);                       // V251123R4: 정규화 시 dirty 플래그 기록
  }
}

static void tapping_term_commit(bool changed)
{
  tapping_term_sync_state_from_storage();
  if (changed)
  {
    eeconfig_flag_tapping_term(true);
    era_state_sync_bump_config();  // V260821R1: GET이 새 값을 돌려준 뒤에만 CONFIG revision
  }
}

static bool tapping_term_set_value(uint8_t id, uint8_t *value_data, uint8_t length)
{
  bool     changed = false;
  uint16_t term_ms;
  uint8_t  flag;

  if (value_data == NULL || length < 4U)
  {
    return false;                                                     // V251123R5: VIA 패킷 최소 길이 보강
  }

  switch (id)
  {
    case id_qmk_tapping_global_term:
    {
      term_ms = tapping_term_normalize((uint16_t)value_data[0] * 10U);  // V251123R5: VIA dropdown 단일 바이트만 수용
      changed = (tapping_term_storage.tapping_term_ms != term_ms);
      tapping_term_storage.tapping_term_ms = term_ms;
      break;
    }

    case id_qmk_tapping_global_term_exact:  // Exact BE16 milliseconds; no legacy-grid conversion.
    {
      if (length < 5U)
      {
        return false;
      }
      term_ms = ((uint16_t)value_data[0] << 8) | (uint16_t)value_data[1];
      if (!era_term_exact_valid(term_ms))
      {
        return false;
      }
      changed = (tapping_term_storage.tapping_term_ms != term_ms);
      tapping_term_storage.tapping_term_ms = term_ms;
      break;
    }

    case id_qmk_tapping_permissive_hold:
      flag = value_data[0] ? 1U : 0U;
      changed = (tapping_term_storage.permissive_hold != flag);
      tapping_term_storage.permissive_hold = flag;
      break;

    case id_qmk_tapping_hold_on_other_key_press:
      flag = value_data[0] ? 1U : 0U;
      changed = (tapping_term_storage.hold_on_other_key_press != flag);
      tapping_term_storage.hold_on_other_key_press = flag;
      break;

    case id_qmk_tapping_retro_tapping:
      flag = value_data[0] ? 1U : 0U;
      changed = (tapping_term_storage.retro_tapping != flag);
      tapping_term_storage.retro_tapping = flag;
      break;

    default:
      return false;
  }

  tapping_term_commit(changed);
  return true;
}

static void tapping_term_get_value(uint8_t id, uint8_t *value_data, uint8_t length)
{
  if (value_data == NULL || length < 4U)
  {
    return;                                                          // V251123R5: VIA 응답 버퍼 최소 길이 확인
  }

  switch (id)
  {
    case id_qmk_tapping_global_term:
    {
      value_data[0] = (uint8_t)tapping_term_legacy_units(tapping_term_state.tapping_term_ms);  // V260821R1: floor-20ms, 저장값 유지
      break;
    }

    case id_qmk_tapping_global_term_exact:  // V260821R1
    {
      uint16_t term_ms = tapping_term_state.tapping_term_ms;
      value_data[0]    = (uint8_t)(term_ms >> 8);
      if (length >= 5U)
      {
        value_data[1] = (uint8_t)(term_ms & 0xFF);
      }
      break;
    }

    case id_qmk_tapping_permissive_hold:
      value_data[0] = tapping_term_storage.permissive_hold;
      break;

    case id_qmk_tapping_hold_on_other_key_press:
      value_data[0] = tapping_term_storage.hold_on_other_key_press;
      break;

    case id_qmk_tapping_retro_tapping:
      value_data[0] = tapping_term_storage.retro_tapping;
      break;

    default:
      value_data[0] = 0U;
      break;
  }
}

#endif
