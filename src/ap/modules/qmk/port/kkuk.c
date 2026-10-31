#include "quantum.h"

#ifdef KKUK_ENABLE

#include <stdint.h>
#include <string.h>
#include "era_state_sync.h"  // V260823R1: KKUK 값 변경 시 CONFIG revision

#define KKUK_TIME_UNIT         10
#define KKUK_DELAY_TICKS_MIN   5    // 50ms
#define KKUK_DELAY_TICKS_MAX   30   // 300ms
#define KKUK_REPEAT_TICKS_MIN  5    // 50ms
#define KKUK_REPEAT_TICKS_MAX  20   // 200ms


enum via_qmk_kill_switch_value {
    id_qmk_kkuk_enable      = 1,
    id_qmk_kkuk_delay_time  = 2,
    id_qmk_kkuk_repeat_time = 3,
};


typedef union
{
  uint32_t raw;

  struct PACKED
  {
    uint8_t  enable : 2;
    uint8_t  mode   : 6;
    uint8_t  repeat_time;
    uint8_t  delay_time;
  };

} kkuk_config_t;

_Static_assert(sizeof(kkuk_config_t) == sizeof(uint32_t), "EECONFIG out of spec.");


static void via_qmk_kkuk_get_value(uint8_t *data);
static void via_qmk_kkuk_set_value(uint8_t *data);
static void via_qmk_kkuk_save(void);
static bool kkuk_normalize_config(void);
static void kkuk_reset_runtime(void);
static uint8_t kkuk_clamp_ticks(uint8_t value, uint8_t min_value, uint8_t max_value);


static kkuk_config_t kkuk_config;

EECONFIG_DEBOUNCE_HELPER(kkuk, EECONFIG_USER_KKUK, kkuk_config);


static bool is_kkuk_mode = false;
static uint32_t pre_time;
static uint32_t pre_time_delay;
static uint8_t key_cnt = 0;
static uint8_t pre_cnt = 0;
// 셈에 넣은 매트릭스 위치. 누르는 동안 SOCD나 키맵이 바뀌어도 release는 자기 press가 한 일만 되돌린다.
static uint8_t kkuk_counted[(MATRIX_ROWS * MATRIX_COLS + 7U) / 8U];
static report_keyboard_t last_report;


static uint8_t kkuk_clamp_ticks(uint8_t value, uint8_t min_value, uint8_t max_value)
{
  if (value < min_value)
  {
    return min_value;
  }
  if (value > max_value)
  {
    return max_value;
  }
  return value;
}

static void kkuk_reset_runtime(void)
{
  uint32_t now = millis();

  is_kkuk_mode  = false;
  pre_time      = now;
  pre_time_delay = now;
  key_cnt       = 0U;
  pre_cnt       = 0U;
  memset(kkuk_counted, 0, sizeof(kkuk_counted));
}


void kkuk_init(void)
{
  eeconfig_init_kkuk();
  if (kkuk_config.mode != 1U)
  {
    kkuk_config.raw         = 0U;
    kkuk_config.mode        = 1U;
    kkuk_config.enable      = false;
    kkuk_config.delay_time  = 20U;   // 200ms
    kkuk_config.repeat_time = 8U;    // 80ms
    eeconfig_flush_kkuk(true);
  }

  if (kkuk_normalize_config())
  {
    eeconfig_flush_kkuk(true);                                  // V251125R3: KKUK 정규화 경로 정리
  }

  kkuk_reset_runtime();                                         // V260909R1: 설정/부팅 경계에서 이전 입력 epoch 제거
  logPrintf("[ON] KKUK\n");
}

void kkuk_idle(void)
{
  if (!kkuk_config.enable)
  {
    return;
  }

  uint16_t delay_time  = (uint16_t)kkuk_config.delay_time * KKUK_TIME_UNIT;
  uint16_t repeat_time = (uint16_t)kkuk_config.repeat_time * KKUK_TIME_UNIT;
  uint32_t now         = millis();

  if (!is_kkuk_mode)
  {
    if (key_cnt >= 2U && (uint32_t)(now - pre_time_delay) >= delay_time)
    {
      is_kkuk_mode = true;
      pre_time     = now;                                       // V260909R1: 미래 timestamp를 저장하지 않는다.
      pre_cnt      = key_cnt;
    }
  }
  else if (key_cnt == 0U)
  {
    is_kkuk_mode = false;
    pre_cnt      = 0U;
    pre_time     = now;
    return;
  }

  if (!is_kkuk_mode || (uint32_t)(now - pre_time) < repeat_time)
  {
    return;
  }

  bool repeat_requested = key_cnt >= 2U || (key_cnt == 1U && pre_cnt == 2U);
  pre_cnt  = key_cnt;
  pre_time = now;

  if (!repeat_requested)
  {
    return;
  }

  memcpy(&last_report, keyboard_report, sizeof(report_keyboard_t));
  clear_keys();
#ifdef KILL_SWITCH_ENABLE
  // SOCD가 맡은 usage는 반복하지 않는다. winner 판정 전의 눌림 상태를 유지한다.
  for (uint8_t i = 0U; i < sizeof(last_report.keys); i++)
  {
    if (kill_switch_is_use(last_report.keys[i]))
    {
      keyboard_report->keys[i] = last_report.keys[i];
    }
  }
#endif
  send_keyboard_report();
  memcpy(keyboard_report, &last_report, sizeof(report_keyboard_t));
  send_keyboard_report();
}

bool kkuk_process(uint16_t keycode, keyrecord_t *record)
{
  if (record == NULL || record->event.key.row >= MATRIX_ROWS || record->event.key.col >= MATRIX_COLS)
  {
    return true;
  }

  uint16_t index = (uint16_t)((uint16_t)record->event.key.row * MATRIX_COLS + record->event.key.col);
  uint8_t *slot  = &kkuk_counted[index >> 3];
  uint8_t  bit   = (uint8_t)(1U << (index & 7U));

  if (record->event.pressed)
  {
    bool counted = kkuk_config.enable && IS_BASIC_KEYCODE(keycode) && (*slot & bit) == 0U;
#ifdef KILL_SWITCH_ENABLE
    counted = counted && !kill_switch_is_use(keycode);
#endif
    if (!counted)
    {
      return true;
    }
    *slot |= bit;
    if (key_cnt < UINT8_MAX)
    {
      key_cnt++;
    }
  }
  else
  {
    if ((*slot & bit) == 0U)
    {
      return true;
    }
    *slot &= (uint8_t)~bit;
    if (key_cnt > 0U)
    {
      key_cnt--;
    }
  }
  pre_time_delay = millis();
  return true;
}


void via_qmk_kkuk_command(uint8_t *data, uint8_t length)
{
  if (data == NULL || length < 4U)
  {
    if (data != NULL && length > 0U)
    {
      data[0] = id_unhandled;
    }
    return;
  }

  // data = [ command_id, channel_id, value_id, value_data ]
  uint8_t *command_id        = &(data[0]);
  uint8_t *value_id_and_data = &(data[2]);

  switch (*command_id)
  {
    case id_custom_set_value:
      {
        uint8_t before[3] = {value_id_and_data[0], 0, 0};
        uint8_t after[3]  = {value_id_and_data[0], 0, 0};

        via_qmk_kkuk_get_value(before);                 // V260823R1: 실제로 값이 바뀐 SET에서만 revision을 올린다
        via_qmk_kkuk_set_value(value_id_and_data);
        via_qmk_kkuk_get_value(after);
        if (memcmp(before, after, sizeof(before)) != 0)
        {
          era_state_sync_bump_config();
        }
        break;
      }
    case id_custom_get_value:
      {
        via_qmk_kkuk_get_value(value_id_and_data);
        break;
      }
    case id_custom_save:
      {
        via_qmk_kkuk_save();
        break;
      }
    default:
      {
        *command_id = id_unhandled;
        break;
      }
  }
}

static void via_qmk_kkuk_get_value(uint8_t *data)
{
  // data = [ value_id, value_data ]
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);

  switch (*value_id)
  {
    case id_qmk_kkuk_enable:
      value_data[0] = kkuk_config.enable;
      break;
    case id_qmk_kkuk_delay_time:
      value_data[0] = kkuk_config.delay_time;
      break;
    case id_qmk_kkuk_repeat_time:
      value_data[0] = kkuk_config.repeat_time;
      break;
    default:
      break;
  }
}

static void via_qmk_kkuk_set_value(uint8_t *data)
{
  // data = [ value_id, value_data ]
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);
  bool runtime_changed = false;

  switch (*value_id)
  {
    case id_qmk_kkuk_enable:
      {
        uint8_t next = value_data[0] != 0U ? 1U : 0U;
        if (kkuk_config.enable != next)
        {
          kkuk_config.enable = next;
          runtime_changed = true;
        }
        break;
      }
    case id_qmk_kkuk_delay_time:
      {
        uint8_t next = kkuk_clamp_ticks(value_data[0], KKUK_DELAY_TICKS_MIN, KKUK_DELAY_TICKS_MAX);
        if (kkuk_config.delay_time != next)
        {
          kkuk_config.delay_time = next;
          runtime_changed = true;
        }
        break;
      }
    case id_qmk_kkuk_repeat_time:
      {
        uint8_t next = kkuk_clamp_ticks(value_data[0], KKUK_REPEAT_TICKS_MIN, KKUK_REPEAT_TICKS_MAX);
        if (kkuk_config.repeat_time != next)
        {
          kkuk_config.repeat_time = next;
          runtime_changed = true;
        }
        break;
      }
    default:
      break;
  }

  if (runtime_changed)
  {
    // V260909R1: live 설정 변경은 새 입력 epoch다. 이미 눌려 있던 키는 다음 press부터 다시 추적한다.
    kkuk_reset_runtime();
  }
}

static void via_qmk_kkuk_save(void)
{
  eeconfig_flush_kkuk(true);
}

static bool kkuk_normalize_config(void)
{
  bool dirty = false;

  if (kkuk_config.enable > 1U)
  {
    kkuk_config.enable = 1U;
    dirty = true;
  }

  uint8_t delay_time = kkuk_clamp_ticks(kkuk_config.delay_time, KKUK_DELAY_TICKS_MIN, KKUK_DELAY_TICKS_MAX);
  if (delay_time != kkuk_config.delay_time)
  {
    kkuk_config.delay_time = delay_time;
    dirty = true;
  }

  uint8_t repeat_time = kkuk_clamp_ticks(kkuk_config.repeat_time, KKUK_REPEAT_TICKS_MIN, KKUK_REPEAT_TICKS_MAX);
  if (repeat_time != kkuk_config.repeat_time)
  {
    kkuk_config.repeat_time = repeat_time;
    dirty = true;
  }

  return dirty;
}

#endif
