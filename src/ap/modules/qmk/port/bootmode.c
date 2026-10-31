#include "bootmode.h"

#ifdef BOOTMODE_ENABLE

#include "usb.h"
#include "usbd_hid.h"
#include <string.h>
#include "via.h"
#include "era_state_sync.h"  // V260823R1: BootMode 선택값 변경 시 CONFIG revision

// V251108R3: VIA 선택값을 리셋 전까지 보류하고 Apply 요청은 메인 루프에서 처리
static UsbBootMode_t pending_boot_mode = USB_BOOT_MODE_FS_1K;
static bool          pending_boot_mode_init = false;
static uint8_t       bootmode_encode_via_value(UsbBootMode_t mode);          // V251113R1: VIA JSON과 열거형 간 값 변환
static UsbBootMode_t bootmode_decode_via_value(uint8_t via_value);

void bootmode_publish_defaults(void)
{
  pending_boot_mode = USB_BOOT_MODE_DEFAULT_VALUE;
  pending_boot_mode_init = true;
}

static void bootmode_sync_pending(void)
{
  if (pending_boot_mode_init == false)
  {
    pending_boot_mode      = usbBootModeGet();
    pending_boot_mode_init = true;
  }
}

// Selection and Apply retain their existing behavior; value 4 is an observation only.
void via_qmk_usb_bootmode_command(uint8_t *data, uint8_t length)
{
  if (data == NULL || length == 0U) return;
  uint8_t *command_id = &data[0];
  if (*command_id == id_custom_save) return;
  if (length < 4U) { *command_id = id_unhandled; return; }
  uint8_t *value_id = &data[2];
  uint8_t *value_data = &data[3];

  if (*value_id == id_qmk_usb_polling_current) {
    if (*command_id == id_custom_get_value) {
      if (length < 32U) { *command_id = id_unhandled; return; }
      const char *label = usbHidGetPollingLabel();
      memset(value_data, 0, 29U);
      memcpy(value_data, label, strlen(label));
    } else if (*command_id != id_custom_set_value) {
      *command_id = id_unhandled;
    }
    return;
  }
  if (*value_id != id_qmk_usb_bootmode_select && *value_id != id_qmk_usb_bootmode_apply) {
    *command_id = id_unhandled;
    return;
  }
  bootmode_sync_pending();

  switch (*command_id)
  {
    case id_custom_set_value:
    {
      if (*value_id == id_qmk_usb_bootmode_select)
      {
        UsbBootMode_t req_mode = bootmode_decode_via_value(value_data[0]);      // V251113R1: VIA 옵션 순서와 열거형 순서를 분리
        UsbBootMode_t prev_mode = pending_boot_mode;
        if (req_mode < USB_BOOT_MODE_MAX)
        {
          pending_boot_mode = req_mode;  // V251108R1: 값만 보류, 실제 적용은 Apply 토글 시점
        }
        value_data[0] = bootmode_encode_via_value(pending_boot_mode);
        if (pending_boot_mode != prev_mode)
        {
          era_state_sync_bump_config();  // V260823R1: GET이 새 값을 돌려주게 된 경우에만 revision을 올린다
        }
      }
      else if (*value_id == id_qmk_usb_bootmode_apply)
      {
        uint8_t request = value_data[0];                                  // V251109R5: VIA echo 유지
        if (request != 0U)
        {
          usbBootModeScheduleApply(pending_boot_mode);  // V251108R6: 동일 값이라도 Apply 요청 시 재부팅
        }
        value_data[0] = request;
      }
      break;
    }

    case id_custom_get_value:
    {
      if (*value_id == id_qmk_usb_bootmode_select)
      {
        value_data[0] = bootmode_encode_via_value(pending_boot_mode);  // V251113R1
      }
      else if (*value_id == id_qmk_usb_bootmode_apply)
      {
        value_data[0] = 0U;
      }
      break;
    }

    default:
      *command_id = id_unhandled;
      break;
  }
}

static uint8_t bootmode_encode_via_value(UsbBootMode_t mode)
{
  switch (mode)
  {
    case USB_BOOT_MODE_HS_8K:
      return 0U;
    case USB_BOOT_MODE_HS_4K:
      return 1U;
    case USB_BOOT_MODE_HS_2K:
      return 2U;
    case USB_BOOT_MODE_FS_1K:
    default:
      return 3U;
  }
}

static UsbBootMode_t bootmode_decode_via_value(uint8_t via_value)
{
  switch (via_value)
  {
    case 0U:
      return USB_BOOT_MODE_HS_8K;
    case 1U:
      return USB_BOOT_MODE_HS_4K;
    case 2U:
      return USB_BOOT_MODE_HS_2K;
    case 3U:
      return USB_BOOT_MODE_FS_1K;
    default:
      return USB_BOOT_MODE_MAX;
  }
}

#endif
