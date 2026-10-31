#include "indicator_port.h"

#include "color.h"
#include "eeconfig.h"
#include "host.h"
#include "led.h"
#include "port.h"
#include "rgblight.h"
#include "era_state_sync.h"  // V260823R1: 인디케이터 값 변경 시 CONFIG revision
#include <string.h>

// V251012R3: rgblight 내부 정의를 그대로 사용해 구성 슬롯 중복 정의를 제거한다.
static rgblight_indicator_config_t indicator_config = {.raw = 0};

enum
{
  INTIGRITY80_INDICATOR_START_INDEX = 0,
  INTIGRITY80_INDICATOR_END_INDEX   = 27,  // V251127R1: 실제 RGB 수량(0~27, 28개)로 보정
  INTIGRITY80_INDICATOR_LED_COUNT   = (INTIGRITY80_INDICATOR_END_INDEX - INTIGRITY80_INDICATOR_START_INDEX + 1),
};  // V251127R1: INTIGRITY80 인디케이터는 RGB 0~27번(28개)을 고정 사용

static const rgblight_indicator_range_t intigrity80_indicator_ranges[] = {
  [RGBLIGHT_INDICATOR_TARGET_OFF] = {.start = 0, .count = 0},
  [RGBLIGHT_INDICATOR_TARGET_CAPS] = {
    .start = INTIGRITY80_INDICATOR_START_INDEX,
    .count = INTIGRITY80_INDICATOR_LED_COUNT,
  },
  [RGBLIGHT_INDICATOR_TARGET_SCROLL] = {
    .start = INTIGRITY80_INDICATOR_START_INDEX,
    .count = INTIGRITY80_INDICATOR_LED_COUNT,
  },
  [RGBLIGHT_INDICATOR_TARGET_NUM] = {
    .start = INTIGRITY80_INDICATOR_START_INDEX,
    .count = INTIGRITY80_INDICATOR_LED_COUNT,
  },
};  // V251127R1: Caps/Scroll/Num 인디케이터 모두 0~27번 RGB를 공유

_Static_assert(sizeof(rgblight_indicator_config_t) == sizeof(uint32_t),
               "EECONFIG out of spec.");  // V251012R3: 슬롯 크기 검증 유지

static void indicator_via_get_value(uint8_t *data);
static void indicator_via_set_value(uint8_t *data);
static void indicator_via_save(void);
static void indicator_apply_led_ranges(void);  // V251016R8: 인디케이터 대상별 LED 범위 등록
static bool indicator_target_from_host(uint8_t target, led_t host_state);  // V251125R3: INTIGRITY80 전용 타깃 판별기
static bool indicator_via_color_value(uint8_t value_id);  // V250310R5: 색상 명령의 2바이트 payload 요구 여부 판별

EECONFIG_DEBOUNCE_HELPER(indicator, EECONFIG_USER_INDICATOR, indicator_config);

static void indicator_apply_defaults(void)
{
  indicator_config.target = RGBLIGHT_INDICATOR_TARGET_CAPS;
  HSV default_hsv         = {HSV_GREEN};
  indicator_config.val    = default_hsv.v;
  indicator_config.hue    = default_hsv.h;  // V251012R2: HSV_GREEN 기본값 적용
  indicator_config.sat    = default_hsv.s;
}

// V251012R2: USB HID 호스트 LED 이벤트를 QMK LED 파이프라인으로 전달한다.
void usbHidSetStatusLed(uint8_t led_bits)
{
  host_keyboard_leds_update(led_bits);  // V251124R7: 호스트 LED 상태를 캐시에 반영해 Layer-Tap led_set 재동기화
  led_t host_state = {.raw = led_bits};
  rgblight_indicator_post_host_event(host_state);  // V251018R1: USB 인터럽트에서 rgblight_set 호출을 피하기 위해 큐잉
}

void led_init_ports(void)
{
  rgblight_indicator_set_target_callback(indicator_target_from_host);  // V251125R3: INTIGRITY80 호스트 LED 매핑 등록

  indicator_apply_led_ranges();

  eeconfig_init_indicator();

  if (indicator_config.raw == 0 || indicator_config.target > RGBLIGHT_INDICATOR_TARGET_NUM)
  {
    indicator_apply_defaults();  // V251012R2: 초기값 또는 손상된 데이터 복원
    eeconfig_flush_indicator(true);
  }

  rgblight_indicator_update_config(indicator_config);  // V251012R4: 기본 구성 및 저장된 구성을 즉시 반영
  // V251012R7: rgblight 초기화 직후 재평가가 진행되므로 이 시점에서 동기화 호출은 생략
}

void led_update_ports(led_t led_state)
{
  (void)led_state;
  rgblight_indicator_request_host_refresh();
}

static bool indicator_target_from_host(uint8_t target, led_t host_state)
{
  switch (target)
  {
    case RGBLIGHT_INDICATOR_TARGET_CAPS:
    {
      return host_state.caps_lock;
    }
    case RGBLIGHT_INDICATOR_TARGET_SCROLL:
    {
      return host_state.scroll_lock;
    }
    case RGBLIGHT_INDICATOR_TARGET_NUM:
    {
      return host_state.num_lock;
    }
    default:
    {
      return false;
    }
  }
}

static void indicator_apply_led_ranges(void)
{
  uint8_t range_count = (uint8_t)(sizeof(intigrity80_indicator_ranges) / sizeof(intigrity80_indicator_ranges[0]));

  rgblight_indicator_set_ranges(intigrity80_indicator_ranges, range_count);  // V251125R3: INTIGRITY80 범위를 rgblight에 전달
}

static bool indicator_via_color_value(uint8_t value_id)
{
  switch (value_id)
  {
    case id_qmk_custom_ind_color:
    {
      return true;
    }
    default:
    {
      return false;
    }
  }
}

void indicator_port_via_command(uint8_t *data, uint8_t length)
{
  if (data == NULL || length == 0U)
  {
    return;
  }

  uint8_t *command_id = &(data[0]);

  if (length < 2U)
  {
    *command_id = id_unhandled;
    return;  // V250310R5: command/channel 헤더가 없으면 커스텀 인디케이터 채널을 해석하지 않는다.
  }

  switch (*command_id)
  {
    case id_custom_set_value:
    case id_custom_get_value:
    {
      if (length < 4U)
      {
        *command_id = id_unhandled;
        return;  // V250310R5: value_id와 첫 value_data 접근 전 최소 길이를 확인한다.
      }

      if (indicator_via_color_value(data[2]) && length < 5U)
      {
        *command_id = id_unhandled;
        return;  // V250310R5: 색상 명령은 hue/sat 2바이트가 모두 필요하다.
      }

      if (*command_id == id_custom_set_value)
      {
        uint8_t before[4] = {data[2], 0, 0, 0};
        uint8_t after[4]  = {data[2], 0, 0, 0};

        indicator_via_get_value(before);                 // V260823R1: 변경 여부 판정을 위한 사전 스냅샷
        indicator_via_set_value(&(data[2]));
        indicator_via_get_value(after);
        if (memcmp(before, after, sizeof(before)) != 0)
        {
          era_state_sync_bump_config();                  // V260823R1: 실제로 값이 바뀐 SET에서만 revision을 올린다
        }
      }
      else
      {
        indicator_via_get_value(&(data[2]));
      }
      break;
    }
    case id_custom_save:
    {
      indicator_via_save();
      break;
    }
    default:
    {
      *command_id = id_unhandled;
      break;
    }
  }
}

static void indicator_via_get_value(uint8_t *data)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);

  switch (*value_id)
  {
    case id_qmk_custom_ind_selec:
    {
      value_data[0] = indicator_config.target;
      break;
    }
    case id_qmk_custom_ind_brightness:
    {
      value_data[0] = indicator_config.val;
      break;
    }
    case id_qmk_custom_ind_color:
    {
      value_data[0] = indicator_config.hue;
      value_data[1] = indicator_config.sat;
      break;
    }
  }
}

static void indicator_via_set_value(uint8_t *data)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);
  uint32_t prev_raw   = indicator_config.raw;  // V251013R5: 중복 갱신 여부 확인

  switch (*value_id)
  {
    case id_qmk_custom_ind_selec:
    {
      indicator_config.target = value_data[0];
      if (indicator_config.target > RGBLIGHT_INDICATOR_TARGET_NUM)
      {
        indicator_config.target = RGBLIGHT_INDICATOR_TARGET_OFF;
      }
      break;
    }
    case id_qmk_custom_ind_brightness:
    {
      indicator_config.val = value_data[0];
      break;
    }
    case id_qmk_custom_ind_color:
    {
      indicator_config.hue = value_data[0];
      indicator_config.sat = value_data[1];
      break;
    }
    default:
    {
      return;
    }
  }

  if (indicator_config.raw == prev_raw)
  {
    return;  // V251013R5: 동일 값 반복 전달 시 rgblight 경로 호출 생략
  }

  rgblight_indicator_update_config(indicator_config);  // V251012R4: VIA 변경 사항을 즉시 rgblight에 전달
}

static void indicator_via_save(void)
{
  eeconfig_flush_indicator(true);
}
