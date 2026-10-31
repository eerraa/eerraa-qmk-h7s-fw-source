/*
 * usb.h
 *
 *  Created on: 2018. 3. 16.
 *      Author: HanCheol Cho
 */

#ifndef SRC_HW_USB_CDC_USB_H_
#define SRC_HW_USB_CDC_USB_H_




#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"

#ifdef _USE_HW_USB



#include "usbd_core.h"
#include "usbd_desc.h"

#if HW_USB_CDC == 1
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#endif

#if HW_USB_MSC == 1
#include "usbd_msc.h"
#endif

#if HW_USB_HID == 1
#include "usbd_hid.h"
// #include "usbd_hid_if.h"
#endif


typedef enum UsbMode
{
  USB_NON_MODE,
  USB_CDC_MODE,
  USB_MSC_MODE,
  USB_HID_MODE,
  USB_CMP_MODE,
} UsbMode_t;

typedef enum UsbType
{
  USB_CON_CDC = 0,
  USB_CON_CLI = 1,
  USB_CON_CAN = 2,
  USB_CON_ESP = 3,
  USB_CON_HID = 4,
} UsbType_t;

typedef enum UsbBootMode                               // V250923R1 Persisted USB polling profile
{
  USB_BOOT_MODE_FS_1K = 0,
  USB_BOOT_MODE_HS_2K,
  USB_BOOT_MODE_HS_4K,
  USB_BOOT_MODE_HS_8K,
  USB_BOOT_MODE_MAX,
} UsbBootMode_t;

#ifndef USB_BOOT_MODE_DEFAULT_VALUE
#define USB_BOOT_MODE_DEFAULT_VALUE USB_BOOT_MODE_FS_1K           // V251112R6: 기본 BootMode, 보드에서 재정의 가능
#endif

#ifdef BOOTMODE_ENABLE
void          bootmode_init(void);                         // V251112R6: BootMode 기본값 초기화 진입점
bool          usbBootModeLoad(void);                       // V250923R1: 저장된 폴링 모드 로드
UsbBootMode_t usbBootModeGet(void);                        // V250923R1: 현재 폴링 모드
bool          usbBootModeIsFullSpeed(void);
uint8_t       usbBootModeGetHsInterval(void);
bool          usbBootModeStore(UsbBootMode_t mode);
void          usbBootModeApplyDefaults(void);
bool          usbBootModeSaveAndReset(UsbBootMode_t mode);
bool          usbBootModeScheduleApply(UsbBootMode_t mode);  // V260823R2: 사용자 명시 적용 경로만 유지
#else
static inline void bootmode_init(void)
{
}

static inline bool usbBootModeLoad(void)
{
  return true;
}

static inline UsbBootMode_t usbBootModeGet(void)
{
  return USB_BOOT_MODE_HS_8K;
}

static inline bool usbBootModeIsFullSpeed(void)
{
  return false;
}

static inline uint8_t usbBootModeGetHsInterval(void)
{
  return 0x01;
}

static inline bool usbBootModeStore(UsbBootMode_t mode)
{
  (void)mode;
  return false;
}

static inline void usbBootModeApplyDefaults(void)
{
}

static inline bool usbBootModeSaveAndReset(UsbBootMode_t mode)
{
  (void)mode;
  return false;
}

static inline bool usbBootModeScheduleApply(UsbBootMode_t mode)
{
  (void)mode;
  return false;
}
#endif

void usbProcess(void);                                  // V260823R2 사용자 BootMode/reset 요청 서비스
bool usbScheduleGraceReset(uint32_t delay_ms);          // V251109R4 VIA 응답 송신 보장용 리셋 요청

bool usbInit(void);
bool usbBegin(UsbMode_t usb_mode);
void usbDeInit(void);
bool usbIsOpen(void);
bool usbIsConnect(void);
bool usbIsSuspended(void);
bool usbIsResetPending(void);
bool usbHostSeen(void);       // V260901R1: 부팅 이후 호스트가 한 번이라도 주소를 줬는가
uint32_t usbSofCount(void);   // V260901R1: SOF 누적. 자동 다운그레이드에 쓰지 않는다

UsbMode_t usbGetMode(void);
UsbType_t usbGetType(void);


#endif


#ifdef __cplusplus
}
#endif




#endif /* SRC_HW_USB_CDC_USB_H_ */
