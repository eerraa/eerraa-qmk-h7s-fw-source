#include "rgb_sleep.h"

#ifdef RGBLIGHT_ENABLE

#include <string.h>
#include "quantum.h"
#include "port.h"
#include "era_state_sync.h"
#include "usb.h"
#include "rgblight.h"


#define RGB_SLEEP_SIGNATURE  (0x53U)  // 'S'
#define RGB_SLEEP_VERSION    (1U)
#define RGB_SLEEP_VERSION_MASK      (0x7FU)
#define RGB_SLEEP_DISABLED_FLAG     (0x80U)  // V260901R1: 기존 version byte의 상위 비트를 inverted runtime enable로 사용


typedef struct
{
  uint8_t  signature;
  uint8_t  version;
  uint16_t timeout_seconds;
} rgb_sleep_storage_t;

_Static_assert(sizeof(rgb_sleep_storage_t) == 4, "EECONFIG out of spec.");  // V260901R1: 슬롯 크기 고정


static bool via_qmk_rgb_sleep_get_value(uint8_t *data, uint8_t length);
static bool via_qmk_rgb_sleep_set_value(uint8_t *data, uint8_t length);
static void rgb_sleep_apply_rgb(bool want_dark);
static uint16_t rgb_sleep_normalized_seconds(uint16_t seconds);


static rgb_sleep_storage_t rgb_sleep_store;
static bool                rgb_sleep_dark;
static bool                rgb_sleep_sof_seen;
static uint32_t            rgb_sleep_last_sof_count;
static uint32_t            rgb_sleep_last_sof_ms;

EECONFIG_DEBOUNCE_HELPER_CHECKED(rgb_sleep_cfg, EECONFIG_USER_RGB_SLEEP, rgb_sleep_store);


static uint16_t rgb_sleep_normalized_seconds(uint16_t seconds)
{
  return (seconds == 0U) ? RGB_SLEEP_TIMEOUT_DEFAULT_SEC : seconds;
}

bool rgb_sleep_enabled(void)
{
  return (rgb_sleep_store.version & RGB_SLEEP_DISABLED_FLAG) == 0U;
}

uint16_t rgb_sleep_timeout_seconds(void)
{
  return rgb_sleep_normalized_seconds(rgb_sleep_store.timeout_seconds);
}

uint8_t rgb_sleep_timeout_min(void)
{
  return rgb_sleep_policy_preset_minutes(rgb_sleep_timeout_seconds());
}

bool rgb_sleep_is_dark(void)
{
  return rgb_sleep_dark;
}

bool eeconfig_check_valid_rgb_sleep_cfg(void)
{
  rgb_sleep_storage_t probe;

  eeprom_read_block(&probe, EECONFIG_USER_RGB_SLEEP, sizeof(probe));
  return probe.signature == RGB_SLEEP_SIGNATURE &&
         (probe.version & RGB_SLEEP_VERSION_MASK) == RGB_SLEEP_VERSION &&
         probe.timeout_seconds != 0U;
}

void eeconfig_post_flush_rgb_sleep_cfg(void)
{
}

void rgb_sleep_storage_publish(void)
{
  // Publish verified settings without forgetting a currently suspended physical output.
  eeconfig_init_rgb_sleep_cfg();
  rgb_sleep_store.timeout_seconds = rgb_sleep_normalized_seconds(rgb_sleep_store.timeout_seconds);
}

void rgb_sleep_storage_stage_defaults(void)
{
  rgb_sleep_storage_t previous = rgb_sleep_store;
  uint8_t previous_dirty = dirty_rgb_sleep_cfg;
  rgb_sleep_storage_apply_defaults();
  eeconfig_flush_rgb_sleep_cfg(true);
  rgb_sleep_store = previous;
  dirty_rgb_sleep_cfg = previous_dirty;
}

void rgb_sleep_storage_apply_defaults(void)
{
  rgb_sleep_store.signature        = RGB_SLEEP_SIGNATURE;
  rgb_sleep_store.version          = RGB_SLEEP_VERSION;
  rgb_sleep_store.timeout_seconds  = RGB_SLEEP_TIMEOUT_DEFAULT_SEC;
  eeconfig_flag_rgb_sleep_cfg(true);
}

void rgb_sleep_storage_flush(bool force)
{
  eeconfig_flush_rgb_sleep_cfg(force);
}

void rgb_sleep_init(void)
{
  eeconfig_init_rgb_sleep_cfg();
  if (rgb_sleep_store.signature != RGB_SLEEP_SIGNATURE ||
      (rgb_sleep_store.version & RGB_SLEEP_VERSION_MASK) != RGB_SLEEP_VERSION ||
      rgb_sleep_store.timeout_seconds == 0U)
  {
    rgb_sleep_storage_apply_defaults();
    eeconfig_flush_rgb_sleep_cfg(true);  // V260901R1: 유효하지 않은 슬롯은 init에서 기본 600초를 바로 기록한다
  }
  rgb_sleep_store.timeout_seconds = rgb_sleep_normalized_seconds(rgb_sleep_store.timeout_seconds);
  rgb_sleep_dark             = false;
  rgb_sleep_sof_seen         = false;
  rgb_sleep_last_sof_count   = usbSofCount();
  rgb_sleep_last_sof_ms      = timer_read32();
}

static void rgb_sleep_apply_rgb(bool want_dark)
{
#if defined(RGBLIGHT_SLEEP)
  if (want_dark != rgb_sleep_dark)
  {
    // Sleep is a physical output gate. Keep rgblight_config untouched so VIA and SAVE
    // continue to describe the user's selected effect while the LEDs are dark.
    rgblight_set_output_suspend_state(want_dark);
    rgb_sleep_dark = want_dark;
  }
#else
  (void)want_dark;
#endif
}

void rgb_sleep_task(void)
{
  uint32_t sof_count;
  bool     host_gone;
  bool     want_dark;

  sof_count = usbSofCount();
  if (sof_count != rgb_sleep_last_sof_count)
  {
    rgb_sleep_last_sof_count = sof_count;
    rgb_sleep_last_sof_ms    = timer_read32();
    rgb_sleep_sof_seen       = true;
  }

  host_gone = false;
  if (usbHostSeen() && rgb_sleep_sof_seen)
  {
    if (timer_elapsed32(rgb_sleep_last_sof_ms) >= RGB_SLEEP_SOF_STALE_MS)
    {
      host_gone = true;  // V260901R1: Suspend 콜백이 없어도 SOF가 끊기면 호스트가 사라진 것으로 본다
    }
  }

  want_dark = rgb_sleep_policy_local_requested(usbIsSuspended(),
                                               host_gone,
                                               rgb_sleep_enabled(),
                                               rgb_sleep_timeout_seconds(),
                                               last_matrix_activity_elapsed());
  rgb_sleep_apply_rgb(want_dark);
}

bool rgb_sleep_handle_via_command(uint8_t *data, uint8_t length)
{
  uint8_t *command_id;
  uint8_t *value_id_and_data;
  rgb_sleep_storage_t before;
  rgb_sleep_storage_t after;
  bool     handled;

  if (data == NULL || length < 2U)
  {
    return false;
  }

  command_id        = &(data[0]);
  value_id_and_data = &(data[2]);

  switch (*command_id)
  {
    case id_custom_set_value:
      {
        if (length < 4U)
        {
          *command_id = id_unhandled;
          return true;
        }
        before  = rgb_sleep_store;
        handled = via_qmk_rgb_sleep_set_value(value_id_and_data, length);
        if (!handled)
        {
          *command_id = id_unhandled;
          return true;
        }
        (void)via_qmk_rgb_sleep_get_value(value_id_and_data, length);  // V260901R1: SET 응답은 펌웨어가 실제로 보관한 값을 에코한다
        after = rgb_sleep_store;
        if (memcmp(&before, &after, sizeof(before)) != 0)
        {
          era_state_sync_bump_config();
        }
        break;
      }
    case id_custom_get_value:
      {
        if (length < 4U)
        {
          *command_id = id_unhandled;
          return true;
        }
        if (!via_qmk_rgb_sleep_get_value(value_id_and_data, length))
        {
          *command_id = id_unhandled;
        }
        break;
      }
    case id_custom_save:
      {
        rgb_sleep_storage_flush(true);
        break;
      }
    default:
      {
        *command_id = id_unhandled;
        break;
      }
  }
  return true;
}

static bool via_qmk_rgb_sleep_get_value(uint8_t *data, uint8_t length)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);
  uint16_t seconds;

  switch (*value_id)
  {
    case id_qmk_rgb_sleep_timeout:
      {
        if (length < 4U)
        {
          return false;
        }
        value_data[0] = rgb_sleep_policy_preset_minutes(rgb_sleep_timeout_seconds());
        return true;
      }
    case id_qmk_rgb_sleep_timeout_exact:
      {
        if (length < 5U)
        {
          return false;
        }
        seconds       = rgb_sleep_timeout_seconds();
        value_data[0] = (uint8_t)(seconds >> 8);
        value_data[1] = (uint8_t)(seconds & 0xFFU);
        return true;
      }
    case id_qmk_rgb_sleep_enable:
      {
        if (length < 4U)
        {
          return false;
        }
        value_data[0] = rgb_sleep_enabled() ? 1U : 0U;
        return true;
      }
    default:
      return true;  // 기존 미등록 value id는 inert handled 상태를 유지한다
  }
}

static bool via_qmk_rgb_sleep_set_value(uint8_t *data, uint8_t length)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);
  uint16_t requested;

  switch (*value_id)
  {
    case id_qmk_rgb_sleep_timeout:
      {
        if (length < 4U)
        {
          return false;
        }
        if (!rgb_sleep_timeout_is_preset(value_data[0]))
        {
          return true;  // V260901R1: 기존 value 1의 프리셋 밖 값은 inert handled로 두고 저장소를 바꾸지 않는다
        }
        requested = (uint16_t)value_data[0] * 60U;
        break;
      }
    case id_qmk_rgb_sleep_timeout_exact:
      {
        if (length < 5U)
        {
          return false;
        }
        requested = ((uint16_t)value_data[0] << 8) | (uint16_t)value_data[1];
        if (requested == 0U)
        {
          return false;  // V260901R1: exact seconds는 uint16 1..65535만 허용한다
        }
        break;
      }
    case id_qmk_rgb_sleep_enable:
      {
        uint8_t version = rgb_sleep_store.version & RGB_SLEEP_VERSION_MASK;
        bool enabled = value_data[0] != 0U;
        uint8_t requested_version = version | (enabled ? 0U : RGB_SLEEP_DISABLED_FLAG);
        if (rgb_sleep_store.version != requested_version)
        {
          rgb_sleep_store.version = requested_version;
          eeconfig_flag_rgb_sleep_cfg(true);
        }
        return true;
      }
    default:
      return true;  // 기존 미등록 value id는 inert handled 상태를 유지한다
  }

  if (rgb_sleep_store.timeout_seconds != requested)
  {
    rgb_sleep_store.timeout_seconds = requested;
    eeconfig_flag_rgb_sleep_cfg(true);
  }
  return true;
}

#endif
