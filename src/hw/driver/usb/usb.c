/*
 * usb.c
 *
 *  Created on: 2018. 3. 16.
 *      Author: HanCheol Cho
 */


#include "usb.h"
#include "cdc.h"
#include "cli.h"
#include "reset.h"
#include "eeprom.h"
#include "qmk/port/port.h"
#include "qmk/port/platforms/eeprom.h"
#include "qmk/port/platforms/bootloader.h"

#define USB_RESET_RESPONSE_GRACE_MS   (40U)                           // V251109R4: VIA 응답 송신 보장을 위한 최소 유예
#define USB_BOOTMODE_APPLY_GRACE_MS   (500U)                          // Allow host follow-up requests while the main loop keeps servicing USB.
#define USB_RESET_DETACH_DELAY_MS     (100U)                          // V251109R7: 호스트가 디태치를 감지할 최소 시간
#define USB_SHUTDOWN_DEADLINE_US      (40000U)                        // Authorized shutdown wall time, from the seal


#ifdef _USE_HW_USB
#include "usb_device.h"
#include "usb_port.h"
#include "usb_descriptors.h"
#include "usb_hid/hid_port.h"
#include "micros.h"


static bool      is_init = false;
static uint32_t teardown_failures;
// Set once by the authorization commit; the sealed shutdown owns the only reset.
static bool      shutdown_authorized;
static UsbMode_t is_usb_mode = USB_NON_MODE;
#ifdef BOOTMODE_ENABLE
static UsbBootMode_t usb_boot_mode = USB_BOOT_MODE_DEFAULT_VALUE;                    // V251112R6: 보드별 기본값을 매크로로 분리
#endif

static const char *const usb_boot_mode_name[USB_BOOT_MODE_MAX] = {          // V250923R1 Mode labels for logging/CLI
  "FS 1K",
  "HS 2K",
  "HS 4K",
  "HS 8K",
};

static const char *usbBootModeLabel(UsbBootMode_t mode);                     // V250923R1 helpers
bool usbScheduleGraceReset(uint32_t delay_ms);                               // V251109R4: VIA 응답 송신 보장용 리셋 요청
#ifdef BOOTMODE_ENABLE
static bool usbBootModeWriteRaw(UsbBootMode_t mode);                         // V251112R5: EEPROM 직접 갱신 헬퍼
bool usbBootModeStore(UsbBootMode_t mode);
#endif
#ifdef BOOTMODE_ENABLE
static volatile struct
{
  bool          pending;                                                   // V251108R3: VIA에서 요청된 BootMode 적용 큐
  UsbBootMode_t mode;
} boot_mode_apply_request = {false, USB_BOOT_MODE_DEFAULT_VALUE};          // V251112R6: 보드별 기본값 사용
#endif

#ifdef BOOTMODE_ENABLE
static void bootmode_ensure_default_persisted(void)
{
  uint32_t raw_mode = eeprom_read_dword((const uint32_t *)EECONFIG_USER_BOOTMODE);

  if (raw_mode >= USB_BOOT_MODE_MAX)
  {
    usbBootModeApplyDefaults();                                             // V251112R6: EEPROM이 비어 있으면 즉시 기본값 기록
  }
}
#endif

static volatile struct
{
  bool     pending;                                                        // V251109R4: VIA 응답 송신을 보장하기 위한 리셋 큐
  uint32_t ready_ms;
} usb_reset_request = {false, 0U};


static const char *usbBootModeLabel(UsbBootMode_t mode)
{
  if (mode < USB_BOOT_MODE_MAX)
  {
    return usb_boot_mode_name[mode];
  }
  return "UNKNOWN";
}

#ifdef BOOTMODE_ENABLE
void bootmode_init(void)
{
  bootmode_ensure_default_persisted();                                      // V251112R6: EEPROM 기본값 동기화
  usb_boot_mode = USB_BOOT_MODE_DEFAULT_VALUE;                              // V251112R6: BootMode 기본값 초기화 진입점
}

bool usbBootModeLoad(void)
{
  uint32_t raw_mode = eeprom_read_dword((const uint32_t *)EECONFIG_USER_BOOTMODE);

  if (raw_mode >= USB_BOOT_MODE_MAX)
  {
    usbBootModeApplyDefaults();                                   // V251112R5: 슬롯이 손상되면 즉시 기본값을 기록
    raw_mode = USB_BOOT_MODE_DEFAULT_VALUE;                        // V251112R6: 보드별 기본값 적용
  }

  usb_boot_mode = (UsbBootMode_t)raw_mode;
  logPrintf("[  ] USB BootMode : %s\n", usbBootModeLabel(usb_boot_mode));  // V250923R1 Log persisted boot mode

  return true;
}

UsbBootMode_t usbBootModeGet(void)
{
  return usb_boot_mode;
}

bool usbBootModeIsFullSpeed(void)
{
  return usb_boot_mode == USB_BOOT_MODE_FS_1K;
}

uint8_t usbBootModeGetHsInterval(void)
{
  switch (usb_boot_mode)
  {
    case USB_BOOT_MODE_HS_4K:
      return 0x02;
    case USB_BOOT_MODE_HS_2K:
      return 0x03;
    case USB_BOOT_MODE_FS_1K:
      return 0x01;
    case USB_BOOT_MODE_HS_8K:
    default:
      return 0x01;
  }
}

bool usbBootModeStore(UsbBootMode_t mode)
{
  if (mode >= USB_BOOT_MODE_MAX)
  {
    return false;
  }

  if (usb_boot_mode == mode)
  {
    return true;
  }

  return usbBootModeWriteRaw(mode);
}

bool usbBootModeSaveAndReset(UsbBootMode_t mode)
{
  if (usbBootModeStore(mode) != true)
  {
    return false;
  }

  return usbScheduleGraceReset(USB_BOOTMODE_APPLY_GRACE_MS);              // V251109R4: VIA 응답 송신 후 리셋
}
#endif

#ifdef BOOTMODE_ENABLE
bool usbBootModeScheduleApply(UsbBootMode_t mode)
{
  if (mode >= USB_BOOT_MODE_MAX)
  {
    return false;
  }

  if (boot_mode_apply_request.pending == true && boot_mode_apply_request.mode == mode)
  {
    return true;  // V251108R8: 동일 모드 중복 요청은 메인 루프 부하 없이 무시
  }

  boot_mode_apply_request.mode    = mode;
  boot_mode_apply_request.pending = true;
  return true;
}
#endif

#ifdef BOOTMODE_ENABLE
static bool usbBootModeWriteRaw(UsbBootMode_t mode)
{
  uint32_t raw_mode = (uint32_t)mode;
  uint32_t addr     = (uint32_t)EECONFIG_USER_BOOTMODE;

  if (mode >= USB_BOOT_MODE_MAX)
  {
    return false;
  }

  // A sealed write would be dropped: report rejection and keep the RAM mode.
  if (eeprom_admission_sealed())
  {
    return false;
  }

  eeprom_update_dword((uint32_t *)addr, raw_mode);                         // V251112R4: QMK EEPROM 큐 경로로 BootMode 저장
  usb_boot_mode = mode;
  return true;
}

void usbBootModeApplyDefaults(void)
{
  (void)usbBootModeWriteRaw(USB_BOOT_MODE_DEFAULT_VALUE);                   // V251112R6: 보드 기본값으로 EEPROM 초기화
}
#endif

bool usbScheduleGraceReset(uint32_t delay_ms)                              // V251109R4: VIA 응답을 보장하기 위한 리셋 유예 API
{
  // An authorized shutdown is never renewed or replaced.
  if (shutdown_authorized)
  {
    return false;
  }

  if (delay_ms == 0U)
  {
    delay_ms = USB_RESET_RESPONSE_GRACE_MS;
  }

  uint32_t ready_ms = millis() + delay_ms;

  if (usb_reset_request.pending == true)
  {
    if ((int32_t)(usb_reset_request.ready_ms - ready_ms) > 0)
    {
      usb_reset_request.ready_ms = ready_ms;
    }
    return true;
  }

  usb_reset_request.pending = true;
  usb_reset_request.ready_ms = ready_ms;
  return true;
}

#ifdef BOOTMODE_ENABLE
static void usbProcessBootModeApply(void)
{
  if (boot_mode_apply_request.pending == false)
  {
    return;
  }

  UsbBootMode_t req_mode = boot_mode_apply_request.mode;
  boot_mode_apply_request.pending = false;

  if (req_mode >= USB_BOOT_MODE_MAX)
  {
    return;
  }

  if (usbBootModeSaveAndReset(req_mode) != true)
  {
    logPrintf("[!] USB BootMode apply 실패\n");
  }
}
#endif

// Final recheck, both seals and shutdown admission in one short masked section,
// with no foreground return or dispatch after the preliminary checks; IRQ-side
// changes since then are covered by the recheck. False changed nothing.
static bool usbAuthorizeShutdown(uint32_t *anchor_us)
{
  uint32_t mask = usbHidLock();
  bool authorized = !usbHidViaResponsesPending() && bootloader_shutdown_seal();
  if (authorized)
  {
    shutdown_authorized = true;
    usb_reset_request.pending = false;
    *anchor_us = micros();
    // Retiring every producer closes VIA receive, dispatch and rearm before the mask drops.
    usbPortShutdown(*anchor_us);
  }
  usbHidUnlock(mask);
  return authorized;
}

// Nonreturning: interrupts stay enabled and the caller never resumes QMK, VIA,
// CLI or persistence. It only publishes thread turns, reads the terminal and
// the original wall deadline (which escapes independently of USB) and sleeps
// while no work is sticky; OTG, SysTick and every published fact wake it.
static void usbCompleteShutdown(uint32_t anchor_us)
{
  usb_shutdown_t result;
  for (;;)
  {
    usbLifecycleThreadCheckpoint();
    result = usbPortShutdownResult();
    if (result != USB_SHUTDOWN_PENDING)
    {
      break;
    }
    if ((uint32_t)(micros() - anchor_us) >= USB_SHUTDOWN_DEADLINE_US)
    {
      result = usbPortShutdownEscape();
      break;
    }
    // Checked and entered masked: an interrupt after the check stays pending,
    // still wakes WFI and runs at the unmask, so no fact is slept through.
    uint32_t mask = usbHidLock();
    if (!usbLifecycleFactsPending())
    {
      __WFI();
    }
    usbHidUnlock(mask);
  }
  // USB failure must never prevent the requested MCU/boot reset.
  if (result != USB_SHUTDOWN_OK) ++teardown_failures;
  is_init     = false;                                                  // V251109R6: 재부팅 이후 usbBegin()을 강제하도록 초기화 상태 리셋
  is_usb_mode = USB_NON_MODE;

  delay(USB_RESET_DETACH_DELAY_MS);                                      // V251109R7: 호스트가 디태치를 감지할 시간을 제공

  resetToReset();                                                        // V251109R4: VIA 응답 송신 이후에만 리셋 실행
}

static void usbProcessDeferredReset(void)
{
  if (usb_reset_request.pending == false)
  {
    return;
  }

  if ((int32_t)(millis() - usb_reset_request.ready_ms) < 0)
  {
    return;
  }

  // Pending prerequisites return to normal keyboard and application service.
  // Response drain includes the live deferred command's reserved reply, even before enqueue.
  // Persistence covers dirty bytes, a page in flight or awaiting retry, a pending
  // prepare receipt, backend erase and an unfinished VIA CLEAN.
  if (eeprom_persistence_pending() || usbHidViaResponsesPending()) return;

  uint32_t anchor_us;
  if (!usbAuthorizeShutdown(&anchor_us)) return;
  usbCompleteShutdown(anchor_us);
}

static bool usbHasPendingService(bool has_apply_request, bool has_reset_request)
{
  return has_apply_request || has_reset_request;
}

void usbProcess(void)
{
#ifdef BOOTMODE_ENABLE
  bool has_apply_request = boot_mode_apply_request.pending;
#else
  const bool has_apply_request = false;
#endif
  bool has_reset_request = usb_reset_request.pending;

  if (!usbHasPendingService(has_apply_request, has_reset_request))
  {
    return;  // V260823R2: 자동 복구 큐 없이 사용자 apply/reset 요청만 서비스한다.
  }

#ifdef BOOTMODE_ENABLE
  if (has_apply_request)
  {
    usbProcessBootModeApply();
  }
#endif
  if (has_reset_request)
  {
    usbProcessDeferredReset();
  }
}

#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
static void cliBoot(cli_args_t *args);                                       // V250923R1 Boot mode CLI handler
#endif




bool usbInit(void)
{
#ifdef _USE_HW_CLI
  cliAdd("usb", cliCmd);
  cliAdd("boot", cliBoot);                                    // V250923R1 Expose boot mode control
#endif
  return true;
}

bool usbBegin(UsbMode_t usb_mode)
{
#if HW_USB_CDC
  if (usb_mode != USB_CMP_MODE) return false;
#else
  if (usb_mode != USB_HID_MODE) return false;
#endif
  is_init = usbDeviceStart(!usbBootModeIsFullSpeed(), usbBootModeGetHsInterval());
  is_usb_mode = is_init ? usb_mode : USB_NON_MODE;
  return is_init;
}

uint32_t usbTeardownFailureCount(void) { return teardown_failures; }

bool usbIsOpen(void)
{
  #if HW_USB_CDC == 1
  return cdcIsConnect();
  #else
  return false;
  #endif
}

bool usbIsConnect(void)
{
  return usb_device.configured && !usb_device.suspended && !usb_device.reset_pending;
}

bool usbIsSuspended(void)
{
  return usbPortSuspended();
}

bool usbIsResetPending(void)
{
  return usb_device.reset_pending;
}

bool usbHostSeen(void)
{
  return usb_device.host_seen;
}

uint16_t usbFrameNumber(void)
{
  return usbPortFrameNumber();
}

UsbMode_t usbGetMode(void)
{
  return is_usb_mode;
}

UsbType_t usbGetType(void)
{
#if HW_USB_CDC == 1  
  return (UsbType_t)cdcGetType();
#elif HW_USE_KBD == 1
  return USB_CON_KBD;
#else
  return USB_CON_CDC;
#endif
}

void OTG_HS_IRQHandler(void)
{
  usbPortIrq();
}


#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    uint16_t vid = 0;
    uint16_t pid = 0;
    vid = USB_VID;
    pid = USB_PID;

    cliPrintf("USB VID     : 0x%04X\n", vid);
    cliPrintf("USB PID     : 0x%04X\n", pid);

    while(cliKeepLoop())
    {
      cliPrintf("USB Mode    : %d\n", usbGetMode());
      cliPrintf("USB Type    : %d\n", usbGetType());
      cliPrintf("USB Connect : %d\n", usbIsConnect());
      cliPrintf("USB Open    : %d\n", usbIsOpen());
      cliPrintf("\x1B[%dA", 4);
      delay(100);
    }
    cliPrintf("\x1B[%dB", 4);

    ret = true;
  }
#if HW_USB_CDC == 1
  if (args->argc == 1 && args->isStr(0, "tx") == true)
  {
    uint32_t pre_time;
    uint32_t tx_cnt = 0;
    uint32_t sent_len = 0;

    pre_time = millis();
    while(cliKeepLoop())
    {
      if (millis()-pre_time >= 1000)
      {
        pre_time = millis();
        logPrintf("tx : %d KB/s\n", tx_cnt/1024);
        tx_cnt = 0;
      }
      sent_len = cdcWrite((uint8_t *)"123456789012345678901234567890\n", 31);
      tx_cnt += sent_len;
    }
    cliPrintf("\x1B[%dB", 2);

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "rx") == true)
  {
    uint32_t pre_time;
    uint32_t rx_cnt = 0;
    uint32_t rx_len;

    pre_time = millis();
    while(cliKeepLoop())
    {
      if (millis()-pre_time >= 1000)
      {
        pre_time = millis();
        logPrintf("rx : %d KB/s\n", rx_cnt/1024);
        rx_cnt = 0;
      }

      rx_len = cdcAvailable();

      for (int i=0; i<rx_len; i++)
      {
        cdcRead();
      }

      rx_cnt += rx_len;
    }
    cliPrintf("\x1B[%dB", 2);

    ret = true;
  }
#endif

  if (ret == false)
  {
    cliPrintf("usb info\n");
    #if HW_USB_CDC == 1
    cliPrintf("usb tx\n");
    cliPrintf("usb rx\n");
    #endif
  }
}

void cliBoot(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("Boot Mode   : %s\n", usbBootModeLabel(usbBootModeGet()));       // V250923R1 Display stored mode
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "set") == true)
  {
    UsbBootMode_t req_mode = USB_BOOT_MODE_MAX;

    if (args->isStr(1, "8k") == true)
    {
      req_mode = USB_BOOT_MODE_HS_8K;
    }
    else if (args->isStr(1, "4k") == true)
    {
      req_mode = USB_BOOT_MODE_HS_4K;
    }
    else if (args->isStr(1, "2k") == true)
    {
      req_mode = USB_BOOT_MODE_HS_2K;
    }
    else if (args->isStr(1, "1k") == true)
    {
      req_mode = USB_BOOT_MODE_FS_1K;
    }

    if (req_mode < USB_BOOT_MODE_MAX)
    {
      cliPrintf("Boot Mode   : %s -> %s\n", usbBootModeLabel(usbBootModeGet()), usbBootModeLabel(req_mode));
      if (usbBootModeSaveAndReset(req_mode) != true)
      {
        cliPrintf("Boot mode save failed\n");
      }
      ret = true;
    }
  }

  if (ret == false)
  {
    cliPrintf("boot info\n");
    cliPrintf("boot set 8k\n");
    cliPrintf("boot set 4k\n");
    cliPrintf("boot set 2k\n");
    cliPrintf("boot set 1k\n");
  }
}
#endif

#endif
