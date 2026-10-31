#include "bootloader.h"
#include "eeprom.h"
#include "usb.h"        // V251109R4: VIA 리셋 유예 요청

static bool bootloader_schedule_deferred_reset(uint32_t boot_mode);  // V250310R6: VIA 응답 이후 부트/리셋 공용 예약

static bool bootloader_schedule_deferred_reset(uint32_t boot_mode)
{
#ifdef _USE_HW_USB
  resetSetBootMode(boot_mode);

  if (usbScheduleGraceReset(0U) == true)
  {
    return true;  // V250310R6: VIA 응답 패킷 전송 이후에만 리셋되도록 예약
  }
#else
  resetSetBootMode(boot_mode);
#endif

  return false;
}

void bootloader_jump(void)
{
  resetToBoot();
}

bool bootloader_jump_deferred(void)
{
  if (bootloader_schedule_deferred_reset(1U << MODE_BIT_BOOT) == true)
  {
    return true;
  }

  resetToBoot();  // V250310R6: USB 유예 예약 실패 시 기존 즉시 부트 진입으로 폴백
  return false;
}

void mcu_reset(void)
{
  for (int i=0; i<32; i++)
  {
    eeprom_update();
  }

  if (mcu_reset_deferred() == true)
  {
    return;             // V251109R4: VIA 응답 송신까지 리셋을 보류
  }

  resetToReset();
}

bool mcu_reset_deferred(void)
{
  if (bootloader_schedule_deferred_reset(0U) == true)
  {
    return true;
  }

  return false;
}
