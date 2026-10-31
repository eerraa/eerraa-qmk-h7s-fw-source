#pragma once

#include QMK_KEYMAP_CONFIG_H

#ifdef RGBLIGHT_ENABLE

#include <stdbool.h>
#include <stdint.h>

// V260901R1: VIA RGB SLEEP. 유휴 타임아웃·USB Suspend·호스트 소실을 한 owner가 담당한다.
//            판정은 qmk_firmware_eerraa work/era-nvm 의 사용자-가시 동작과 같다.
//            스플릿 와이어 owner·DUAL-HOST·SYSTEM ch9 는 가져오지 않는다.


#define RGB_SLEEP_TIMEOUT_DEFAULT_SEC  600U
#define RGB_SLEEP_SOF_STALE_MS         300U


static inline bool rgb_sleep_policy_local_requested(bool usb_suspended, bool frames_lost,
                                                    bool rgb_sleep_enabled, uint16_t timeout_seconds,
                                                    uint32_t matrix_idle_ms)
{
  bool idle_timeout = (timeout_seconds != 0U) &&
                      (matrix_idle_ms >= ((uint32_t)timeout_seconds * 1000U));
  return rgb_sleep_enabled && (usb_suspended || frames_lost || idle_timeout);
}

static inline bool rgb_sleep_timeout_is_preset(uint8_t minutes)
{
  return minutes == 1U || minutes == 3U || minutes == 5U ||
         minutes == 10U || minutes == 30U || minutes == 60U;
}

// 공식 VIA GET 은 투영만 한다. 저장값을 바꾸지 않는다.
static inline uint8_t rgb_sleep_policy_preset_minutes(uint16_t seconds)
{
  static const uint8_t presets[] = {1, 3, 5, 10, 30, 60};
  uint8_t result = presets[0];
  uint8_t i;

  for (i = 0; i < (uint8_t)(sizeof(presets) / sizeof(presets[0])); i++)
  {
    if (seconds < ((uint16_t)presets[i] * 60U))
    {
      break;
    }
    result = presets[i];
  }
  return result;
}


void     rgb_sleep_init(void);
void     rgb_sleep_task(void);
void     rgb_sleep_storage_apply_defaults(void);
void     rgb_sleep_storage_stage_defaults(void);
void     rgb_sleep_storage_publish(void);
void     rgb_sleep_storage_flush(bool force);
bool     rgb_sleep_handle_via_command(uint8_t *data, uint8_t length);
bool     rgb_sleep_enabled(void);
uint16_t rgb_sleep_timeout_seconds(void);
uint8_t  rgb_sleep_timeout_min(void);
bool     rgb_sleep_is_dark(void);

#endif
