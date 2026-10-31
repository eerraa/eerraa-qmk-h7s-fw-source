#include "debounce_profile.h"


#include <string.h>
#include "eeprom.h"
#include "eeconfig.h"
#include "log.h"
#include "qmk/port/port.h"
#include "via.h"
#include "era_state_sync.h"  // V260823R1: 디바운스 값 변경 시 CONFIG revision


#define DEBOUNCE_PROFILE_SIGNATURE     (0x434E4244UL)     // "DBNC"
#define DEBOUNCE_PROFILE_VERSION       (1U)
#define DEBOUNCE_PROFILE_MIN_DELAY_MS  (1U)
#define DEBOUNCE_PROFILE_MAX_DELAY_MS  (30U)


typedef struct __attribute__((packed))
{
  uint8_t  type;
  uint8_t  pre_ms;
  uint8_t  post_ms;
  uint8_t  version;
  uint32_t signature;
} debounce_profile_storage_t;

static debounce_profile_storage_t debounce_profile_storage = {0};


EECONFIG_DEBOUNCE_HELPER(debounce_profile, EECONFIG_USER_DEBOUNCE, debounce_profile_storage);


typedef struct
{
  debounce_profile_values_t values;
  bool                      initialized;
} debounce_profile_state_t;  // V260901R1: 소비자 없는 VIA STATUS 전용 복사 상태 제거

static debounce_profile_state_t debounce_profile_state = {0};


static void     debounce_profile_apply_defaults_locked(void);
static void     debounce_profile_sync_from_storage(void);
static bool     debounce_profile_is_storage_valid(const debounce_profile_storage_t *storage);
static uint8_t  debounce_profile_clamp_delay(uint8_t delay);
static void     debounce_profile_store_current(void);
static bool     debounce_profile_set_value_internal(uint8_t id, uint8_t value);
static bool     debounce_profile_get_value_internal(uint8_t id, uint8_t *value_data);
static void     debounce_profile_log_change(const char *source);
static bool     debounce_profile_values_changed(const debounce_profile_values_t *before,
                                                const debounce_profile_values_t *after);
static const debounce_runtime_config_t *debounce_profile_default_config(void);


void debounce_profile_init(void)
{
  if (debounce_profile_state.initialized)
  {
    return;
  }

  eeconfig_init_debounce_profile();

  if (!debounce_profile_is_storage_valid(&debounce_profile_storage))
  {
    debounce_profile_apply_defaults_locked();                     // V251115R1: 손상 데이터 복구 시 기본값을 기록
    eeconfig_flush_debounce_profile(true);
  }

  debounce_profile_sync_from_storage();
  debounce_profile_state.initialized = true;
}

void debounce_profile_apply_current(void)
{
  if (!debounce_profile_state.initialized)
  {
    debounce_profile_init();
  }

  debounce_runtime_config_t config =
  {
    .type    = debounce_profile_state.values.type,
    .pre_ms  = debounce_profile_state.values.pre_ms,
    .post_ms = debounce_profile_state.values.post_ms,
  };

  if (!debounce_runtime_apply_config(&config))
  {
    logPrintf("[!] Debounce profile apply failed (type %d)\n", config.type);  // V251115R1: 런타임 재초기화 실패 로그
    return;
  }
}

const debounce_profile_values_t *debounce_profile_current(void)
{
  return &debounce_profile_state.values;
}

bool debounce_profile_set_mode(uint8_t mode)
{
  if (mode >= DEBOUNCE_RUNTIME_TYPE_COUNT)
  {
    return false;
  }

  debounce_profile_values_t previous = debounce_profile_state.values;
  debounce_profile_state.values.type = (debounce_runtime_type_t)mode;
  if (mode == DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK)
  {
    uint8_t common_delay = debounce_profile_clamp_delay(debounce_profile_state.values.pre_ms);
    debounce_profile_state.values.pre_ms  = common_delay;
    debounce_profile_state.values.post_ms = common_delay;
  }
  else if (mode == DEBOUNCE_RUNTIME_TYPE_SYM_EAGER_PK)
  {
    debounce_profile_state.values.post_ms = debounce_profile_clamp_delay(debounce_profile_state.values.post_ms);
  }
  else
  {
    debounce_profile_state.values.pre_ms  = debounce_profile_clamp_delay(debounce_profile_state.values.pre_ms);
    debounce_profile_state.values.post_ms = debounce_profile_clamp_delay(debounce_profile_state.values.post_ms);
  }

  bool changed = debounce_profile_values_changed(&previous, &debounce_profile_state.values);
  debounce_profile_store_current();
  debounce_profile_apply_current();
  if (changed)
  {
    debounce_profile_log_change("mode");                    // V251115R2: VIA 디바운스 타입 변경 로그
  }
  return true;
}

bool debounce_profile_set_single_delay(uint8_t delay_ms)
{
  if (debounce_profile_state.values.type != DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK)
  {
    return false;
  }

  debounce_profile_values_t previous = debounce_profile_state.values;
  uint8_t clamped = debounce_profile_clamp_delay(delay_ms);
  debounce_profile_state.values.pre_ms  = clamped;
  debounce_profile_state.values.post_ms = clamped;
  bool changed = debounce_profile_values_changed(&previous, &debounce_profile_state.values);
  debounce_profile_store_current();
  debounce_profile_apply_current();
  if (changed)
  {
    debounce_profile_log_change("single delay");            // V251115R2: VIA 디바운스 지연 변경 로그
  }
  return true;
}

bool debounce_profile_set_press_delay(uint8_t delay_ms)
{
  if (debounce_profile_state.values.type != DEBOUNCE_RUNTIME_TYPE_ASYM_EAGER_DEFER_PK)
  {
    return false;
  }

  debounce_profile_values_t previous = debounce_profile_state.values;
  debounce_profile_state.values.pre_ms = debounce_profile_clamp_delay(delay_ms);
  bool changed = debounce_profile_values_changed(&previous, &debounce_profile_state.values);
  debounce_profile_store_current();
  debounce_profile_apply_current();
  if (changed)
  {
    debounce_profile_log_change("press delay");             // V251115R2: VIA 디바운스 지연 변경 로그
  }
  return true;
}

bool debounce_profile_set_release_delay(uint8_t delay_ms)
{
  if (debounce_profile_state.values.type == DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK)
  {
    return false;
  }

  debounce_profile_values_t previous = debounce_profile_state.values;
  debounce_profile_state.values.post_ms = debounce_profile_clamp_delay(delay_ms);
  bool changed = debounce_profile_values_changed(&previous, &debounce_profile_state.values);
  debounce_profile_store_current();
  debounce_profile_apply_current();
  if (changed)
  {
    debounce_profile_log_change("release delay");           // V251115R2: VIA 디바운스 지연 변경 로그
  }
  return true;
}

void debounce_profile_save(bool force)
{
  eeconfig_flush_debounce_profile(force);
}

void debounce_profile_storage_stage_defaults(void)
{
  debounce_profile_storage_t previous = debounce_profile_storage;
  uint8_t previous_dirty = dirty_debounce_profile;
  debounce_profile_apply_defaults_locked();
  eeconfig_flush_debounce_profile(true);
  debounce_profile_storage = previous;
  dirty_debounce_profile = previous_dirty;
}

void debounce_profile_storage_publish(void)
{
  eeconfig_init_debounce_profile();
  debounce_profile_sync_from_storage();
  if (debounce_profile_state.initialized) debounce_profile_apply_current();
}

void debounce_profile_storage_apply_defaults(void)
{
  debounce_profile_apply_defaults_locked();                      // V251115R1: EEPROM 공장 초기화 경로에서 기본값만 기록
}

bool debounce_profile_handle_via_command(uint8_t *data, uint8_t length)
{
  if (data == NULL || length < 4U)
  {
    return false;
  }

  uint8_t *command_id = &(data[0]);
  uint8_t *value_id   = &(data[2]);
  uint8_t *value_data = &(data[3]);
  bool     handled    = false;

  switch (*command_id)
  {
    case id_custom_set_value:
    {
      // V260823R1: 한 value id의 SET이 다른 id가 돌려주는 값까지 바꾼다. Balanced 시간 하나가
      //            pre/post를 동시에 옮기는 것이 그 예다. 그래서 스냅샷은 네 값 전체를 본다.
      static const uint8_t watched[] =
      {
        id_qmk_debounce_mode,
        id_qmk_debounce_time_single,
        id_qmk_debounce_time_pre,
        id_qmk_debounce_time_post,
      };
      uint8_t before[sizeof(watched)];
      uint8_t after[sizeof(watched)];
      uint8_t k;

      for (k = 0; k < sizeof(watched); k++)
      {
        (void)debounce_profile_get_value_internal(watched[k], &before[k]);
      }

      handled = debounce_profile_set_value_internal(*value_id, value_data[0]);
      if (handled)
      {
        (void)debounce_profile_get_value_internal(*value_id, value_data);

        for (k = 0; k < sizeof(watched); k++)
        {
          (void)debounce_profile_get_value_internal(watched[k], &after[k]);
        }
        if (memcmp(before, after, sizeof(before)) != 0)
        {
          era_state_sync_bump_config();                             // V260823R1: 실제로 값이 바뀐 SET에서만 revision을 올린다
        }
      }
      break;
    }

    case id_custom_get_value:
      handled = debounce_profile_get_value_internal(*value_id, value_data);  // V260901R1: 알 수 없는 value는 id_unhandled
      break;

    case id_custom_save:
      debounce_profile_save(true);
      handled = true;
      break;

    default:
      handled = false;
      break;
  }

  if (!handled)
  {
    *command_id = id_unhandled;
  }
  return handled;
}

static void debounce_profile_apply_defaults_locked(void)
{
  const debounce_runtime_config_t *default_config = debounce_profile_default_config();
  debounce_profile_storage.type      = (uint8_t)default_config->type;
  debounce_profile_storage.pre_ms    = debounce_profile_clamp_delay(default_config->pre_ms);
  debounce_profile_storage.post_ms   = debounce_profile_clamp_delay(default_config->post_ms);
  debounce_profile_storage.version   = DEBOUNCE_PROFILE_VERSION;
  debounce_profile_storage.signature = DEBOUNCE_PROFILE_SIGNATURE;
  eeconfig_flag_debounce_profile(true);
}

static void debounce_profile_sync_from_storage(void)
{
  debounce_profile_values_t values;
  values.type    = (debounce_runtime_type_t)debounce_profile_storage.type;
  values.pre_ms  = debounce_profile_clamp_delay(debounce_profile_storage.pre_ms);
  values.post_ms = debounce_profile_clamp_delay(debounce_profile_storage.post_ms);

  if (values.type >= DEBOUNCE_RUNTIME_TYPE_COUNT)
  {
    values.type = debounce_profile_default_config()->type;
  }
  if (values.type == DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK)
  {
    values.post_ms = values.pre_ms;
  }

  debounce_profile_state.values = values;
}

static bool debounce_profile_is_storage_valid(const debounce_profile_storage_t *storage)
{
  if (storage->signature != DEBOUNCE_PROFILE_SIGNATURE)
  {
    return false;
  }
  if (storage->version != DEBOUNCE_PROFILE_VERSION)
  {
    return false;
  }
  if (storage->type >= DEBOUNCE_RUNTIME_TYPE_COUNT)
  {
    return false;
  }
  return true;
}

static const debounce_runtime_config_t *debounce_profile_default_config(void)
{
  return debounce_runtime_get_default_config();             // V251115R3: 보드 기본 디바운스 설정을 프로필 기본값으로 사용
}

static uint8_t debounce_profile_clamp_delay(uint8_t delay)
{
  if (delay < DEBOUNCE_PROFILE_MIN_DELAY_MS)
  {
    delay = DEBOUNCE_PROFILE_MIN_DELAY_MS;
  }
  if (delay > DEBOUNCE_PROFILE_MAX_DELAY_MS)
  {
    delay = DEBOUNCE_PROFILE_MAX_DELAY_MS;
  }
  return delay;
}

static void debounce_profile_store_current(void)
{
  debounce_profile_storage.type      = (uint8_t)debounce_profile_state.values.type;
  debounce_profile_storage.pre_ms    = debounce_profile_state.values.pre_ms;
  debounce_profile_storage.post_ms   = debounce_profile_state.values.post_ms;
  debounce_profile_storage.version   = DEBOUNCE_PROFILE_VERSION;
  debounce_profile_storage.signature = DEBOUNCE_PROFILE_SIGNATURE;
  eeconfig_flag_debounce_profile(true);
}

static bool debounce_profile_set_value_internal(uint8_t id, uint8_t value)
{
  switch (id)
  {
    case id_qmk_debounce_mode:
      return debounce_profile_set_mode(value);

    case id_qmk_debounce_time_single:
      return debounce_profile_set_single_delay(value);

    case id_qmk_debounce_time_pre:
      return debounce_profile_set_press_delay(value);

    case id_qmk_debounce_time_post:
      return debounce_profile_set_release_delay(value);

    default:
      break;
  }

  return false;
}

static bool debounce_profile_get_value_internal(uint8_t id, uint8_t *value_data)
{
  switch (id)
  {
    case id_qmk_debounce_mode:
      value_data[0] = (uint8_t)debounce_profile_state.values.type;
      return true;

    case id_qmk_debounce_time_single:
      value_data[0] = debounce_profile_state.values.pre_ms;
      return true;

    case id_qmk_debounce_time_pre:
      value_data[0] = debounce_profile_state.values.pre_ms;
      return true;

    case id_qmk_debounce_time_post:
      value_data[0] = debounce_profile_state.values.post_ms;
      return true;

    default:
      break;
  }

  return false;
}

static bool debounce_profile_values_changed(const debounce_profile_values_t *before,
                                            const debounce_profile_values_t *after)
{
  if (before == NULL || after == NULL)
  {
    return false;
  }

  if (before->type != after->type)
  {
    return true;
  }
  if (before->pre_ms != after->pre_ms)
  {
    return true;
  }
  if (before->post_ms != after->post_ms)
  {
    return true;
  }
  return false;
}

static void debounce_profile_log_change(const char *source)
{
  const char *log_source = (source != NULL) ? source : "unknown";
  logPrintf("[  ] DEBOUNCE change (%s): type %d, pre %d ms, post %d ms\n",
            log_source,
            debounce_profile_state.values.type,
            debounce_profile_state.values.pre_ms,
            debounce_profile_state.values.post_ms);         // V251115R2: VIA 디바운스 런타임 설정 변경 로그
}
