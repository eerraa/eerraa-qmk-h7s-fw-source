#include "indicator_port.h"
#include "ver_port.h"
#include "sys_port.h"
#include "bootmode.h"
#include "debounce_profile.h"
#include "tapping_term.h"
#include "tapdance.h"
#include "mousekey_config.h"
#include "rgb_sleep.h"

static void via_handle_usb_polling_channel(uint8_t *data, uint8_t length);  // V260823R2: 사용자 BootMode 분기만 유지


void via_custom_value_command_kb(uint8_t *data, uint8_t length)
{
  if (data == NULL || length == 0U)
  {
    return;
  }

  // data = [ command_id, channel_id, value_id, value_data ]
  uint8_t *command_id = &(data[0]);

  if (length < 2U)
  {
    *command_id = id_unhandled;
    return;  // V250310R5: command/channel 헤더가 없으면 보드별 VIA 채널을 라우팅하지 않는다.
  }

  uint8_t *channel_id = &(data[1]);

  if (*channel_id == id_qmk_usb_polling)
  {
    via_handle_usb_polling_channel(data, length);                   // V251108R8: USB POLLING 채널 분기를 단일 함수로 처리
    return;
  }

  if (*channel_id == id_custom_channel)
  {
    indicator_port_via_command(data, length);  // V251012R2: 커스텀 인디케이터 채널 처리
    return;
  }

  if (*channel_id == id_qmk_key_response)
  {
    if (debounce_profile_handle_via_command(data, length))
    {
      return;
    }
  }

#ifdef G_TERM_ENABLE
  if (*channel_id == id_qmk_tapping)
  {
    if (tapping_term_handle_via_command(data, length))
    {
      return;  // V251123R4: VIA TAPPING 채널 처리
    }
  }
#endif
#ifdef TAPDANCE_ENABLE
  if (*channel_id == id_qmk_tapdance)
  {
    if (tapdance_handle_via_command(data, length))
    {
      return;  // V251124R8: VIA TAPDANCE 채널 처리
    }
  }
#endif
#ifdef MOUSEKEY_ENABLE
  if (*channel_id == id_qmk_mousekey)
  {
    if (mousekey_config_handle_via_command(data, length))
    {
      return;  // V260823R1: VIA MOUSE 채널 처리
    }
  }
#endif
#ifdef RGBLIGHT_ENABLE
  if (*channel_id == id_qmk_rgb_sleep)
  {
    if (rgb_sleep_handle_via_command(data, length))
    {
      return;  // V260901R1: VIA RGB SLEEP 채널 처리
    }
  }
#endif

  if (*channel_id == id_qmk_version)
  {
    via_qmk_version(data, length);
    return;
  }

  if (*channel_id == id_qmk_system)
  {
    via_qmk_system(data, length);
    return;
  }

#ifdef KILL_SWITCH_ENABLE
  if (*channel_id == id_qmk_kill_switch_lr)
  {
    via_qmk_kill_swtich_command(0, data, length);
    return;
  }
  if (*channel_id == id_qmk_kill_switch_ud)
  {
    via_qmk_kill_swtich_command(1, data, length);
    return;
  }
#endif
#ifdef KKUK_ENABLE
  if (*channel_id == id_qmk_kkuk)
  {
    via_qmk_kkuk_command(data, length);
    return;
  }
#endif

  // Return the unhandled state
  *command_id = id_unhandled;
}

static void via_handle_usb_polling_channel(uint8_t *data, uint8_t length)
{
  if (data == NULL || length == 0U)
  {
    return;
  }

  uint8_t *command_id = &(data[0]);

  if (length < 2U)
  {
    *command_id = id_unhandled;
    return;  // V250310R5: USB polling 채널 헤더가 없으면 즉시 중단한다.
  }

#ifdef BOOTMODE_ENABLE
  if (*command_id == id_custom_save)
  {
    via_qmk_usb_bootmode_command(data, length);  // V250310R5: save 명령은 payload 없이도 BootMode 경로로 전달한다.
    return;
  }
#endif

  if (length < 4U)
  {
    *command_id = id_unhandled;
    return;
  }

  uint8_t value_id = data[2];

#ifdef BOOTMODE_ENABLE
  if (value_id == id_qmk_usb_bootmode_select ||
      value_id == id_qmk_usb_bootmode_apply ||
      value_id == id_qmk_usb_polling_current)
  {
    via_qmk_usb_bootmode_command(data, length);
    return;
  }
#endif


  *command_id = id_unhandled;
}
