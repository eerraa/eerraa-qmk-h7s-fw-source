#include "bootloader.h"
#include "eeprom.h"
#include "qmk/port/platforms/shutdown_seal_internal.h"
#include "usb.h"        // V251109R4: VIA 리셋 유예 요청

static bool bootloader_schedule_deferred_reset(uint32_t boot_mode);  // V250310R6: VIA 응답 이후 부트/리셋 공용 예약

static bool bootloader_schedule_deferred_reset(uint32_t boot_mode)
{
  // The seal is checked before any RTC write; a sealed shutdown is never rescheduled or renewed.
  reset_mode_result_t admitted = resetRequestBootMode(boot_mode);

  if (admitted != RESET_MODE_ACCEPTED)
  {
    return admitted == RESET_MODE_SEALED_SAME;
  }
#ifdef _USE_HW_USB
  if (usbScheduleGraceReset(0U) == true)
  {
    return true;  // V250310R6: VIA 응답 패킷 전송 이후에만 리셋되도록 예약
  }
#endif

  return false;
}

bool bootloader_shutdown_seal(void)
{
  // Persistence and boot intent seal together, or not at all. Thread mode only: a handler-mode
  // seal could land inside resetRequestBootMode between its seal check and its RTC write.
  if (__get_IPSR() != 0U || eeprom_admission_seal() != true)
  {
    return false;
  }
  resetSealBootIntent();
  return true;
}

bool bootloader_shutdown_sealed(void)
{
  return eeprom_admission_sealed() && resetBootIntentIsSealed();
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
  if (bootloader_shutdown_sealed() == true)
  {
    return;             // 승인된 shutdown이 유일한 reset을 소유한다
  }

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
