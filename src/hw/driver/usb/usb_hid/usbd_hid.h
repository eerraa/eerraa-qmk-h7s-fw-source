/**
  ******************************************************************************
  * @file    usbd_hid.h
  * @author  MCD Application Team
  * @brief   Header file for the usbd_hid_core.c file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USB_HID_H
#define __USB_HID_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include  "usbd_ioreq.h"

/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */

/** @defgroup USBD_HID
  * @brief This file is the Header file for usbd_hid.c
  * @{
  */


/** @defgroup USBD_HID_Exported_Defines
  * @{
  */
#define HID_EP_SIZE                                     64U

#define HID_EPIN_ADDR                                   0x81U
#define HID_EPIN_SIZE                                   64U

#define HID_VIA_EP_IN                                   0x84U
#define HID_VIA_EP_OUT                                  0x04U
#define HID_VIA_EP_SIZE                                 32U

#define HID_EXK_EP_IN                                   0x85U
#define HID_EXK_EP_SIZE                                 8U

#define HID_KEYBOARD
#define USB_HID_CONFIG_DESC_SIZ                         91U  
#define USB_HID_DESC_SIZ                                9U

#define HID_KEYBOARD_REPORT_DESC_SIZE                   64U
#define HID_KEYBOARD_VIA_REPORT_DESC_SIZE               34U
#define HID_EXK_REPORT_DESC_SIZE                        123U  // SYSTEM/CONSUMER(50) + MOUSE(73)

#define HID_DESCRIPTOR_TYPE                             0x21U
#define HID_REPORT_DESC                                 0x22U

#define HID_HS_BINTERVAL                                0x01U
#define HID_FS_BINTERVAL                                0x01U

#define USBD_HID_REQ_SET_PROTOCOL                       0x0BU
#define USBD_HID_REQ_GET_PROTOCOL                       0x03U

#define USBD_HID_REQ_SET_IDLE                           0x0AU
#define USBD_HID_REQ_GET_IDLE                           0x02U

#define USBD_HID_REQ_SET_REPORT                         0x09U
#define USBD_HID_REQ_GET_REPORT                         0x01U


/**
  * @}
  */


/** @defgroup USBD_CORE_Exported_TypesDefinitions
  * @{
  */
typedef enum
{
  USBD_HID_IDLE = 0,
  USBD_HID_BUSY,
} USBD_HID_StateTypeDef;

// V260824R1: IN 엔드포인트는 각자 FIFO와 전송 상태를 가진 독립 자원이므로 busy 상태를
//            엔드포인트 번호별로 분리한다. 단일 state를 keyboard/VIA/EXK가 공유하면
//            무관한 엔드포인트의 완료가 남의 busy를 풀어 활성 EP를 재무장할 수 있었다.
#define USBD_HID_IN_EP_MAX  16U

typedef struct
{
  uint32_t Protocol;
  uint32_t IdleState;
  uint32_t AltSetting;
} USBD_HID_HandleTypeDef;

/*
 * HID Class specification version 1.1
 * 6.2.1 HID Descriptor
 */

typedef struct
{
  uint8_t           bLength;
  uint8_t           bDescriptorType;
  uint16_t          bcdHID;
  uint8_t           bCountryCode;
  uint8_t           bNumDescriptors;
  uint8_t           bHIDDescriptorType;
  uint16_t          wItemLength;
} __PACKED USBD_HIDDescTypeDef;

/**
  * @}
  */



/** @defgroup USBD_CORE_Exported_Macros
  * @{
  */

/**
  * @}
  */

/** @defgroup USBD_CORE_Exported_Variables
  * @{
  */

extern USBD_ClassTypeDef USBD_HID;
#define USBD_HID_CLASS &USBD_HID
/**
  * @}
  */

/** @defgroup USB_CORE_Exported_Functions
  * @{
  */
const char *usbHidGetPollingLabel(void);

typedef struct {
  uint32_t generation;
  bool valid;
  bool suspended;
} usb_hid_session_t;
// One coherent snapshot of the admitted session; Suspend preserves its generation.
usb_hid_session_t usbHidGetSession(void);


enum 
{
  USB_HID_LED_NUM_LOCK    = (1 << 0),
  USB_HID_LED_CAPS_LOCK   = (1 << 1),
  USB_HID_LED_SCROLL_LOCK = (1 << 2),
  USB_HID_LED_COMPOSE     = (1 << 3),
  USB_HID_LED_KANA        = (1 << 4)
};

// V260909R1: 버스 세대가 다른 명령/응답을 섞지 않는다. 숫자는 boot 이후 누적 관측값이다.
typedef struct {
  uint32_t arm_failures;
  uint32_t keyboard_coalesced;
  uint32_t extra_coalesced;
  uint32_t invalid_rx;
  uint32_t invalid_control;
  uint32_t session_discards;
} usb_hid_transport_stats_t;
bool usbHidReadViaRequest(uint8_t *data, uint32_t *generation);
bool usbHidEnqueueViaResponse(const uint8_t *data, uint8_t length, uint32_t generation);
bool usbHidViaResponsesPending(void);
void usbHidOnBusResetBegin(void);
void usbHidOnSuspend(void);
bool usbHidHostSleeping(void);
bool usbHidRequestRemoteWakeFromInput(void);
void usbHidOnResume(void);
bool usbHidConsumeWakeSof(void);
void usbHidGetTransportStats(usb_hid_transport_stats_t *stats);
bool usbHidSendReport(uint8_t *p_data, uint16_t length);
// Opt-in logical updates from a single physical scan; ordinary reports stay immutable.
bool usbHidSubmitKeyUpdate(uint8_t *data, uint16_t length, uint32_t scan_token, uint8_t usage, bool pressed);
// token 0 is a barrier for any unfinished physical scan.
void usbHidEndKeyScan(uint32_t scan_token);
// V260911R3: 마지막 keyboard 스냅샷 뒤에 최소 간격을 설정한다. 호출자는 기다리지 않는다.
void usbHidDelayKeyboardReport(uint16_t delay_ms);
void usbHidDelayKeyboardReportForGeneration(uint16_t delay_ms, uint32_t generation);
bool usbHidSendReportEXK(uint8_t *p_data, uint16_t length);
// Reject retired synthetic contributions before they can update queue or reconciliation state.
bool usbHidSendReportForGeneration(uint8_t *data, uint16_t length, uint32_t generation);
bool usbHidSendReportEXKForGeneration(uint8_t *data, uint16_t length, uint32_t generation);
void usbHidSetStatusLed(uint8_t led_bits);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif  /* __USB_HID_H */
/**
  * @}
  */

/**
  * @}
  */

