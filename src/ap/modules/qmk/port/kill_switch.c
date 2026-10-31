#include "quantum.h"

#ifdef KILL_SWITCH_ENABLE

#include <string.h>
#include "era_state_sync.h"  // SOCD(kill switch) 값 변경 시 CONFIG revision

#define KILL_SWITCH_MAX_CH        2U


enum
{
  KILL_SWITCH_LR = 0,
  KILL_SWITCH_UD,
};

enum via_qmk_kill_switch_value {
    id_qmk_kill_switch_enable      = 1,
    id_qmk_kill_switch_keycode_0   = 2,
    id_qmk_kill_switch_keycode_1   = 3,
    id_qmk_kill_switch_mode        = 4,
};

// 저장되는 mode 값. EERRAA SOCD와 같은 번호를 쓴다.
enum
{
  KILL_SWITCH_MODE_LAST_INPUT = 1,
  KILL_SWITCH_MODE_NEUTRAL,
  KILL_SWITCH_MODE_FIRST_INPUT,
  KILL_SWITCH_MODE_KEY_0_PRIORITY,
  KILL_SWITCH_MODE_KEY_1_PRIORITY,
  KILL_SWITCH_MODE_MAX = KILL_SWITCH_MODE_KEY_1_PRIORITY,
};


typedef union
{
  uint64_t raw;

  struct PACKED
  {
    uint8_t  enable;
    uint8_t  mode;
    uint16_t keycode[2];
  };

} kill_switch_config_t;

_Static_assert(sizeof(kill_switch_config_t) == sizeof(uint64_t), "EECONFIG out of spec.");


static void via_qmk_kill_switch_get_value(uint8_t type, uint8_t *data);
static void via_qmk_kill_switch_set_value(uint8_t type, uint8_t *data);
static void via_qmk_kill_switch_save(uint8_t type);


static kill_switch_config_t kill_switch_config[KILL_SWITCH_MAX_CH];

// Kill switch는 호스트로 나가는 report에서 판정한다. 물리 키든 Tap Dance나 매크로든
// 누가 넣은 usage인지 가리지 않는다. pair마다 직전 report에 있던 키(키별 bit)와
// 가장 최근에 나타난 키(-1은 아직 모름)를 기억한다.
static uint8_t kill_switch_seen[KILL_SWITCH_MAX_CH];
static int8_t  kill_switch_last[KILL_SWITCH_MAX_CH];

EECONFIG_DEBOUNCE_HELPER(kill_switch_lr, EECONFIG_USER_KILL_SWITCH_LR, kill_switch_config[KILL_SWITCH_LR]);
EECONFIG_DEBOUNCE_HELPER(kill_switch_ud, EECONFIG_USER_KILL_SWITCH_UD, kill_switch_config[KILL_SWITCH_UD]);


static bool kill_switch_mode_valid(uint8_t mode)
{
  return mode >= KILL_SWITCH_MODE_LAST_INPUT && mode <= KILL_SWITCH_MODE_MAX;
}

static bool kill_switch_keycode_can_report(uint16_t keycode)
{
  return IS_BASIC_KEYCODE(keycode) || IS_MODIFIER_KEYCODE(keycode);
}

static bool kill_switch_pair_local_valid(uint8_t type)
{
  kill_switch_config_t *cfg = &kill_switch_config[type];
  return cfg->enable &&
         kill_switch_mode_valid(cfg->mode) &&
         kill_switch_keycode_can_report(cfg->keycode[0]) &&
         kill_switch_keycode_can_report(cfg->keycode[1]) &&
         cfg->keycode[0] != cfg->keycode[1];
}

static bool kill_switch_pair_can_run(uint8_t type)
{
  if (type >= KILL_SWITCH_MAX_CH || !kill_switch_pair_local_valid(type))
  {
    return false;
  }

  // 두 활성 pair가 같은 usage를 공유하면 report에서 답이 하나로 정해지지 않으므로 둘 다 멈춘다.
  for (uint8_t other = 0U; other < KILL_SWITCH_MAX_CH; other++)
  {
    if (other == type || !kill_switch_pair_local_valid(other))
    {
      continue;
    }
    for (uint8_t i = 0U; i < 2U; i++)
    {
      for (uint8_t j = 0U; j < 2U; j++)
      {
        if (kill_switch_config[type].keycode[i] == kill_switch_config[other].keycode[j])
        {
          return false;
        }
      }
    }
  }
  return true;
}

static void kill_switch_reset_pair(uint8_t type)
{
  kill_switch_seen[type] = 0U;
  kill_switch_last[type] = -1;
}

static bool kill_switch_report_has(const report_keyboard_t *report, uint16_t keycode)
{
  if (IS_MODIFIER_KEYCODE(keycode))
  {
    return (report->mods & MOD_BIT(keycode)) != 0U;
  }
  for (uint8_t i = 0U; i < sizeof(report->keys); i++)
  {
    if (report->keys[i] == (uint8_t)keycode)
    {
      return true;
    }
  }
  return false;
}

static void kill_switch_report_drop(report_keyboard_t *report, uint16_t keycode)
{
  if (IS_MODIFIER_KEYCODE(keycode))
  {
    report->mods &= (uint8_t)~MOD_BIT(keycode);
    return;
  }
  for (uint8_t i = 0U; i < sizeof(report->keys); i++)
  {
    if (report->keys[i] == (uint8_t)keycode)
    {
      report->keys[i] = KC_NO;
    }
  }
}

// 두 키가 모두 눌렸을 때 보고할 키. -1은 둘 다 보고하지 않는다.
static int8_t kill_switch_winner(uint8_t type)
{
  int8_t last = kill_switch_last[type];

  switch (kill_switch_config[type].mode)
  {
    case KILL_SWITCH_MODE_LAST_INPUT:
      return last;
    case KILL_SWITCH_MODE_FIRST_INPUT:
      return last < 0 ? -1 : (int8_t)(1 - last);
    case KILL_SWITCH_MODE_KEY_0_PRIORITY:
      return 0;
    case KILL_SWITCH_MODE_KEY_1_PRIORITY:
      return 1;
    default:
      return -1;
  }
}

void keyboard_report_filter(report_keyboard_t *report)
{
  for (uint8_t type = 0U; type < KILL_SWITCH_MAX_CH; type++)
  {
    if (!kill_switch_pair_can_run(type))
    {
      kill_switch_reset_pair(type);
      continue;
    }

    kill_switch_config_t *cfg  = &kill_switch_config[type];
    uint8_t               seen = (uint8_t)((kill_switch_report_has(report, cfg->keycode[0]) ? 1U : 0U) |
                                           (kill_switch_report_has(report, cfg->keycode[1]) ? 2U : 0U));
    uint8_t               rose = (uint8_t)(seen & (uint8_t)~kill_switch_seen[type]);

    // 한 report에 두 키가 함께 나타나면(KKUK 복원, 두 키를 싣는 탭) 순서가 아니므로 이전 답을 유지한다.
    if (rose == 1U)
    {
      kill_switch_last[type] = 0;
    }
    else if (rose == 2U)
    {
      kill_switch_last[type] = 1;
    }
    kill_switch_seen[type] = seen;

    if (seen != 3U)
    {
      continue;
    }
    int8_t winner = kill_switch_winner(type);
    for (uint8_t i = 0U; i < 2U; i++)
    {
      if ((int8_t)i != winner)
      {
        kill_switch_report_drop(report, cfg->keycode[i]);
      }
    }
  }
}

// 바뀐 pair는 처음부터 판정하고, 결과를 곧바로 호스트에 보낸다. 이전 설정이 숨긴 키도 추가 입력 없이 돌아온다.
static void kill_switch_pair_changed(uint8_t type)
{
  kill_switch_reset_pair(type);
  send_keyboard_report();
}

static bool kill_switch_normalize(uint8_t type)
{
  kill_switch_config_t *cfg = &kill_switch_config[type];

  if (!kill_switch_mode_valid(cfg->mode))
  {
    cfg->raw  = 0U;
    cfg->mode = KILL_SWITCH_MODE_LAST_INPUT;
    return true;
  }
  if (cfg->enable > 1U)
  {
    cfg->enable = 1U;
    return true;
  }
  return false;
}


void kill_switch_init(void)
{
  eeconfig_init_kill_switch_lr();
  if (kill_switch_normalize(KILL_SWITCH_LR))
  {
    eeconfig_flush_kill_switch_lr(true);
  }
  eeconfig_init_kill_switch_ud();
  if (kill_switch_normalize(KILL_SWITCH_UD))
  {
    eeconfig_flush_kill_switch_ud(true);
  }

  kill_switch_reset_pair(KILL_SWITCH_LR);
  kill_switch_reset_pair(KILL_SWITCH_UD);
  logPrintf("[ON] KILL SWITCH\n");
}

bool kill_switch_is_use(uint16_t keycode)
{
  for (uint8_t type = 0U; type < KILL_SWITCH_MAX_CH; type++)
  {
    if (!kill_switch_pair_can_run(type))
    {
      continue;
    }
    if (keycode == kill_switch_config[type].keycode[0] || keycode == kill_switch_config[type].keycode[1])
    {
      return true;
    }
  }
  return false;
}

void via_qmk_kill_swtich_command(uint8_t type, uint8_t *data, uint8_t length)
{
  if (data == NULL || length < 5U || type >= KILL_SWITCH_MAX_CH)
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

        via_qmk_kill_switch_get_value(type, before);    // 실제로 값이 바뀐 SET에서만 revision을 올린다
        via_qmk_kill_switch_set_value(type, value_id_and_data);
        via_qmk_kill_switch_get_value(type, after);
        if (memcmp(before, after, sizeof(before)) != 0)
        {
          era_state_sync_bump_config();
        }
        via_qmk_kill_switch_get_value(type, value_id_and_data);  // 응답은 받아들인 값을 돌려준다
        break;
      }
    case id_custom_get_value:
      via_qmk_kill_switch_get_value(type, value_id_and_data);
      break;
    case id_custom_save:
      via_qmk_kill_switch_save(type);
      break;
    default:
      *command_id = id_unhandled;
      break;
  }
}

static void via_qmk_kill_switch_get_value(uint8_t type, uint8_t *data)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);

  switch (*value_id)
  {
    case id_qmk_kill_switch_enable:
      value_data[0] = kill_switch_config[type].enable;
      break;
    case id_qmk_kill_switch_keycode_0:
      value_data[0] = (uint8_t)(kill_switch_config[type].keycode[0] >> 8);
      value_data[1] = (uint8_t)(kill_switch_config[type].keycode[0] & 0xFFU);
      break;
    case id_qmk_kill_switch_keycode_1:
      value_data[0] = (uint8_t)(kill_switch_config[type].keycode[1] >> 8);
      value_data[1] = (uint8_t)(kill_switch_config[type].keycode[1] & 0xFFU);
      break;
    case id_qmk_kill_switch_mode:
      value_data[0] = kill_switch_config[type].mode;
      break;
    default:
      break;
  }
}

static void via_qmk_kill_switch_set_value(uint8_t type, uint8_t *data)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);

  switch (*value_id)
  {
    case id_qmk_kill_switch_enable:
      {
        uint8_t next = value_data[0] != 0U ? 1U : 0U;
        if (kill_switch_config[type].enable != next)
        {
          kill_switch_config[type].enable = next;
          kill_switch_pair_changed(type);
        }
        break;
      }
    case id_qmk_kill_switch_keycode_0:
    case id_qmk_kill_switch_keycode_1:
      {
        uint8_t index = (uint8_t)(*value_id - id_qmk_kill_switch_keycode_0);
        uint16_t next = (uint16_t)(((uint16_t)value_data[0] << 8) | value_data[1]);
        if (kill_switch_config[type].keycode[index] != next)
        {
          kill_switch_config[type].keycode[index] = next;
          kill_switch_pair_changed(type);
        }
        break;
      }
    case id_qmk_kill_switch_mode:
      // 모르는 mode는 설정을 그대로 두고, 응답이 현재 값을 보여 준다.
      if (kill_switch_mode_valid(value_data[0]) && kill_switch_config[type].mode != value_data[0])
      {
        kill_switch_config[type].mode = value_data[0];
        kill_switch_pair_changed(type);
      }
      break;
    default:
      break;
  }
}

static void via_qmk_kill_switch_save(uint8_t type)
{
  if (type == KILL_SWITCH_LR)
  {
    eeconfig_flush_kill_switch_lr(true);
  }
  else if (type == KILL_SWITCH_UD)
  {
    eeconfig_flush_kill_switch_ud(true);
  }
}

#endif
