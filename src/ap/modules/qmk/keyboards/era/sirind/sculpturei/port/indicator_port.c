#include "indicator_port.h"

#include "color.h"
#include "eeconfig.h"
#include "host.h"
#include "led.h"
#include "port.h"
#include "rgblight.h"
#include "era_state_sync.h"  // V260823R1: 인디케이터 값 변경 시 CONFIG revision
#include <string.h>
#include "ws2812.h"

enum
{
  SCULPTUREI_INDICATOR_SLOT_1 = 0,
  SCULPTUREI_INDICATOR_SLOT_2 = 1,
  SCULPTUREI_INDICATOR_SLOT_COUNT_LOCAL = 2,
};

// V260701R1: SCULPTUREI 인디케이터는 물리 WS2812 0~2에서만 출력하고 underglow 버퍼에는 섞지 않는다.
static const rgblight_indicator_range_t sculpturei_indicator_ranges[] = {
  [RGBLIGHT_INDICATOR_TARGET_OFF] = {.start = 0, .count = 0},
  [RGBLIGHT_INDICATOR_TARGET_CAPS] = {.start = 0, .count = 0},
  [RGBLIGHT_INDICATOR_TARGET_SCROLL] = {.start = 0, .count = 0},
  [RGBLIGHT_INDICATOR_TARGET_NUM] = {.start = 0, .count = 0},
};

static rgblight_indicator_config_t indicator_config[SCULPTUREI_INDICATOR_SLOT_COUNT_LOCAL] = {
  {.raw = 0},
  {.raw = 0},
};

_Static_assert(sizeof(rgblight_indicator_config_t) == sizeof(uint32_t),
               "EECONFIG out of spec.");
_Static_assert(SCULPTUREI_INDICATOR_SLOT_COUNT_LOCAL == RGBLIGHT_INDICATOR_SLOT_COUNT,
               "SCULPTUREI indicator slot count mismatch.");

static void indicator_via_get_value(uint8_t *data);
static void indicator_via_set_value(uint8_t *data);
static void indicator_via_save(void);
static void indicator_apply_defaults(uint8_t index);
static void indicator_render(uint8_t slot, bool active, rgb_led_t color);
static void indicator_render_range(uint32_t start, uint8_t count, bool active, rgb_led_t color);
static bool indicator_via_color_value(uint8_t value_id);  // V250310R5: 색상 명령의 2바이트 payload 요구 여부 판별

EECONFIG_DEBOUNCE_HELPER(indicator_0, EECONFIG_USER_INDICATOR, indicator_config[SCULPTUREI_INDICATOR_SLOT_1]);
EECONFIG_DEBOUNCE_HELPER(indicator_1, (void *)((uint32_t)EECONFIG_USER_INDICATOR + 4), indicator_config[SCULPTUREI_INDICATOR_SLOT_2]);

static void indicator_apply_defaults(uint8_t index)
{
  indicator_config[index].target = (index == SCULPTUREI_INDICATOR_SLOT_1) ? RGBLIGHT_INDICATOR_TARGET_CAPS : RGBLIGHT_INDICATOR_TARGET_SCROLL;

  HSV default_hsv          = {HSV_RED};
  indicator_config[index].val = RGBLIGHT_LIMIT_VAL;  // V260701R2: 락 인디케이터 기본 밝기는 제한된 최대 밝기 100%로 유지
  indicator_config[index].hue = default_hsv.h;  // V260701R1: SCULPTUREI indicator 기본색을 RED로 유지
  indicator_config[index].sat = default_hsv.s;
}

static void indicator_render(uint8_t slot, bool active, rgb_led_t color)
{
  switch (slot)
  {
    case SCULPTUREI_INDICATOR_SLOT_1:
    {
      indicator_render_range(HW_WS2812_CAPS, HW_WS2812_CAPS_CNT, active, color);  // V260701R1: IND1은 RGB1~2를 같은 상태로 구동
      return;
    }
    case SCULPTUREI_INDICATOR_SLOT_2:
    {
      indicator_render_range(HW_WS2812_SCROLL, 1, active, color);  // V260701R1: IND2는 RGB3 단일 LED를 구동
      return;
    }
    default:
    {
      return;
    }
  }
}

static void indicator_render_range(uint32_t start, uint8_t count, bool active, rgb_led_t color)
{
  if (!active)
  {
    for (uint8_t i = 0; i < count; i++)
    {
      ws2812SetColor(start + i, WS2812_COLOR_OFF);
    }
    return;
  }

  for (uint8_t i = 0; i < count; i++)
  {
    ws2812SetColor(start + i, WS2812_COLOR(color.r, color.g, color.b));  // V260701R1: 공용 rgblight 프레임에 SCULPTUREI 물리 인디케이터를 합성
  }
}

static bool indicator_via_color_value(uint8_t value_id)
{
  switch (value_id)
  {
    case id_qmk_custom_ind_color:
    case id_qmk_custom_ind_2_color:
    {
      return true;
    }
    default:
    {
      return false;
    }
  }
}

void usbHidSetStatusLed(uint8_t led_bits)
{
  host_keyboard_leds_update(led_bits);

  led_t host_state = {.raw = led_bits};
  rgblight_indicator_post_host_event(host_state);  // V260701R1: SCULPTUREI에서 host LED 이벤트만 rgblight로 전달
}

void led_init_ports(void)
{
  uint8_t range_count = (uint8_t)(sizeof(sculpturei_indicator_ranges) / sizeof(sculpturei_indicator_ranges[0]));

  rgblight_indicator_set_ranges_at(SCULPTUREI_INDICATOR_SLOT_1, sculpturei_indicator_ranges, range_count);
  rgblight_indicator_set_ranges_at(SCULPTUREI_INDICATOR_SLOT_2, sculpturei_indicator_ranges, range_count);
  rgblight_indicator_set_render_callback(indicator_render);  // V260701R1: SCULPTUREI 물리 0~2 출력은 공용 rgblight 렌더 단계에서 처리

  eeconfig_init_indicator_0();
  eeconfig_init_indicator_1();

  for (uint8_t i = 0; i < SCULPTUREI_INDICATOR_SLOT_COUNT_LOCAL; i++)
  {
    if (indicator_config[i].raw == 0 || indicator_config[i].target > RGBLIGHT_INDICATOR_TARGET_NUM)
    {
      indicator_apply_defaults(i);

      if (i == SCULPTUREI_INDICATOR_SLOT_1)
      {
        eeconfig_flush_indicator_0(true);
      }
      else
      {
        eeconfig_flush_indicator_1(true);
      }
    }

    rgblight_indicator_update_config_at(i, indicator_config[i]);  // V260701R1: 저장된 슬롯 구성을 공용 rgblight 상태로 즉시 반영
  }
}

void led_update_ports(led_t led_state)
{
  (void)led_state;
  rgblight_indicator_request_host_refresh();
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
      value_data[0] = indicator_config[SCULPTUREI_INDICATOR_SLOT_1].target;
      break;
    }
    case id_qmk_custom_ind_brightness:
    {
      value_data[0] = indicator_config[SCULPTUREI_INDICATOR_SLOT_1].val;
      break;
    }
    case id_qmk_custom_ind_color:
    {
      value_data[0] = indicator_config[SCULPTUREI_INDICATOR_SLOT_1].hue;
      value_data[1] = indicator_config[SCULPTUREI_INDICATOR_SLOT_1].sat;
      break;
    }
    case id_qmk_custom_ind_2_selec:
    {
      value_data[0] = indicator_config[SCULPTUREI_INDICATOR_SLOT_2].target;
      break;
    }
    case id_qmk_custom_ind_2_brightness:
    {
      value_data[0] = indicator_config[SCULPTUREI_INDICATOR_SLOT_2].val;
      break;
    }
    case id_qmk_custom_ind_2_color:
    {
      value_data[0] = indicator_config[SCULPTUREI_INDICATOR_SLOT_2].hue;
      value_data[1] = indicator_config[SCULPTUREI_INDICATOR_SLOT_2].sat;
      break;
    }
  }
}

static void indicator_via_set_value(uint8_t *data)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);
  uint8_t  indicator_index = SCULPTUREI_INDICATOR_SLOT_1;
  uint32_t prev_raw;

  switch (*value_id)
  {
    case id_qmk_custom_ind_2_selec:
    case id_qmk_custom_ind_2_brightness:
    case id_qmk_custom_ind_2_color:
    {
      indicator_index = SCULPTUREI_INDICATOR_SLOT_2;
      break;
    }
    default:
    {
      indicator_index = SCULPTUREI_INDICATOR_SLOT_1;
      break;
    }
  }

  prev_raw = indicator_config[indicator_index].raw;

  switch (*value_id)
  {
    case id_qmk_custom_ind_selec:
    case id_qmk_custom_ind_2_selec:
    {
      indicator_config[indicator_index].target = value_data[0];
      if (indicator_config[indicator_index].target > RGBLIGHT_INDICATOR_TARGET_NUM)
      {
        indicator_config[indicator_index].target = RGBLIGHT_INDICATOR_TARGET_OFF;
      }
      break;
    }
    case id_qmk_custom_ind_brightness:
    case id_qmk_custom_ind_2_brightness:
    {
      indicator_config[indicator_index].val = value_data[0];
      break;
    }
    case id_qmk_custom_ind_color:
    case id_qmk_custom_ind_2_color:
    {
      indicator_config[indicator_index].hue = value_data[0];
      indicator_config[indicator_index].sat = value_data[1];
      break;
    }
    default:
    {
      return;
    }
  }

  if (indicator_config[indicator_index].raw == prev_raw)
  {
    return;
  }

  rgblight_indicator_update_config_at(indicator_index, indicator_config[indicator_index]);  // V260701R1: VIA 변경은 공용 rgblight 슬롯 구성만 갱신
}

static void indicator_via_save(void)
{
  eeconfig_flush_indicator_0(true);
  eeconfig_flush_indicator_1(true);  // V260701R1: SCULPTUREI는 단일 custom channel save에 두 indicator 슬롯을 함께 저장
}
