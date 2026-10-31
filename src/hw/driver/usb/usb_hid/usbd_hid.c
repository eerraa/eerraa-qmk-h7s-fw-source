/**
  ******************************************************************************
  * @file    usbd_hid.c
  * @author  MCD Application Team
  * @brief   This file provides the HID core functions.
  *
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
  * @verbatim
  *
  *          ===================================================================
  *                                HID Class  Description
  *          ===================================================================
  *           This module manages the HID class V1.11 following the "Device Class Definition
  *           for Human Interface Devices (HID) Version 1.11 Jun 27, 2001".
  *           This driver implements the following aspects of the specification:
  *             - The Boot Interface Subclass
  *             - The Mouse protocol
  *             - Usage Page : Generic Desktop
  *             - Usage : Joystick
  *             - Collection : Application
  *
  * @note     In HS mode and when the DMA is used, all variables and data structures
  *           dealing with the DMA during the transaction process should be 32-bit aligned.
  *
  *
  *  @endverbatim
  *
  ******************************************************************************
  */


#include "usbd_hid.h"
#include "usbd_ctlreq.h"
#include "usbd_desc.h"
#include "usb.h"                                                // V250923R1 Boot mode aware intervals
#include <string.h>                                             // V251108R8: VIA 큐 헬퍼에서 memset 사용

#include "log.h"
#include "hid_tx_queue.h"
#include "report.h"
#include "micros.h"
#include "usbd_hid_internal.h"


#if HW_USB_LOG == 1
#define logDebug(...)                              \
  {                                                \
    if (HW_LOG_CH == HW_UART_CH_USB) logDisable(); \
    logPrintf(__VA_ARGS__);                        \
    if (HW_LOG_CH == HW_UART_CH_USB) logEnable();  \
  }
#else
#define logDebug(...) 
#endif

static uint8_t USBD_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_HID_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_HID_SOF(USBD_HandleTypeDef *pdev);

#ifndef USE_USBD_COMPOSITE
static uint8_t *USBD_HID_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_GetDeviceQualifierDesc(uint16_t *length);
#endif /* USE_USBD_COMPOSITE  */

#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
static uint8_t *USBD_HID_GetUsrStrDescriptor(struct _USBD_HandleTypeDef *pdev, uint8_t index,  uint16_t *length);
#endif


// V260909R1: TIM2/일반 PWM callback을 제거하고 EP별 FIFO와 완료 기반 pump로 통합한다.
#define HID_TX_DEPTH 128U
#define HID_VIA_RX_DEPTH 16U
static hid_tx_packet_t keyboard_slots[HID_TX_DEPTH];
static hid_tx_packet_t extra_slots[HID_TX_DEPTH];
static hid_tx_packet_t via_slots[HID_TX_DEPTH];
static hid_tx_queue_t keyboard_tx, extra_tx, via_tx;
static hid_tx_packet_t keyboard_latest = { .length = HID_KEYBOARD_REPORT_SIZE };
static hid_tx_packet_t extra_latest[3] = {
  { .length = 6U, .data = {2U} },
  { .length = 3U, .data = {3U} },
  { .length = 3U, .data = {4U} },
};
static bool keyboard_reconcile;
/* Logical physical-key updates may share this candidate while the endpoint is
 * busy. It is separate from the immutable transport queue; an idle endpoint
 * freezes it immediately, even halfway through the producing matrix scan. */
static struct {
  hid_tx_packet_t packet;
  uint32_t scan_token;
  uint8_t touched[32];
  bool pressed;
  bool valid;
} keyboard_candidate;
// V260911R3: FIFO 스냅샷의 완료 시점과 유지 시간만 소유한다. QMK 키 상태는 만지지 않는다.
static uint32_t keyboard_completed_us;
static uint16_t keyboard_delay_ms;
static uint16_t keyboard_active_delay_ms;
static bool keyboard_completed;
static uint8_t extra_reconcile;
static uint32_t transport_generation;
static usb_hid_transport_stats_t transport_stats;
static uint8_t via_rx[HID_VIA_RX_DEPTH][HID_VIA_EP_SIZE];
static uint8_t via_rx_head, via_rx_count;
static bool via_rx_armed;
__ALIGN_BEGIN static uint8_t via_hid_usb_rx_report[HID_VIA_EP_SIZE] __ALIGN_END;
// V260909R1: HAL은 요청1바이트도 EP0 maxpacket 수신을 무장한다. 실제 packet을 수용한 후 길이를 거부한다.
__ALIGN_BEGIN static uint8_t ep0_req_buf[USB_MAX_EP0_SIZE] __ALIGN_END;
// Boot protocol에서 무장한 keyboard 보고. 다음 무장은 DataIn 완료 뒤에만 일어나므로 전송 중에는 불변이다.
__ALIGN_BEGIN static uint8_t keyboard_boot_report[HID_BOOT_KEYBOARD_REPORT_SIZE] __ALIGN_END;
static uint8_t hid_report_protocol = 1U;  // keyboard 외 인터페이스의 GET_PROTOCOL 응답
_Static_assert(sizeof(ep0_req_buf) >= 64U, "EP0 receive buffer must cover a full control packet");
static bool ep0_led_pending;
typedef enum {
  USB_HID_WAKE_IDLE = 0,
  USB_HID_WAKE_SIGNALING,
  USB_HID_WAKE_WAIT_RESUME,
} usb_hid_wake_state_t;
static volatile usb_hid_wake_state_t wake_state;
static volatile bool wake_skip_stale_sof;
static volatile uint32_t wake_suspend_epoch;
static volatile uint32_t suspend_ms;

static bool usbHidFreezeKeyCandidateLocked(void);
static void usbHidPumpLocked(USBD_HandleTypeDef *pdev);
static void usbHidRearmViaLocked(USBD_HandleTypeDef *pdev);
static void usbHidResetTransport(void);
static void usbHidSetKeyboardProtocolLocked(USBD_HandleTypeDef *pdev, USBD_HID_HandleTypeDef *hhid, uint8_t protocol);
static bool usbHidRemoteWakeSuspended(void) __attribute__((noinline));
static uint32_t usbHidLock(void) { uint32_t p = __get_PRIMASK(); __disable_irq(); return p; }
static void usbHidUnlock(uint32_t p) { __set_PRIMASK(p); }

static USB_OTG_DeviceTypeDef *usbHidDeviceRegisters(PCD_HandleTypeDef *pcd)
{
  return (USB_OTG_DeviceTypeDef *)((uintptr_t)pcd->Instance + USB_OTG_DEVICE_BASE);
}

USBD_ClassTypeDef USBD_HID =
{
  USBD_HID_Init,
  USBD_HID_DeInit,
  USBD_HID_Setup,
  NULL,                 /* EP0_TxSent */
  USBD_HID_EP0_RxReady, /* EP0_RxReady */
  USBD_HID_DataIn,      /* DataIn */
  USBD_HID_DataOut,     /* DataOut */
  USBD_HID_SOF,         /* SOF */
  NULL,
  NULL,
#ifdef USE_USBD_COMPOSITE
  NULL,
  NULL,
  NULL,
  NULL,
#else
  USBD_HID_GetHSCfgDesc,
  USBD_HID_GetFSCfgDesc,
  USBD_HID_GetOtherSpeedCfgDesc,
  USBD_HID_GetDeviceQualifierDesc,
#endif /* USE_USBD_COMPOSITE  */

#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
  USBD_HID_GetUsrStrDescriptor,
#endif 
};

#ifndef USE_USBD_COMPOSITE
/* USB HID device FS Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_CfgDesc[USB_HID_CONFIG_DESC_SIZ] __ALIGN_END =
{
  0x09,                                               /* bLength: Configuration Descriptor size */
  USB_DESC_TYPE_CONFIGURATION,                        /* bDescriptorType: Configuration */
  USB_HID_CONFIG_DESC_SIZ,                            /* wTotalLength: Bytes returned */
  0x00,
  0x03,                                               /* bNumInterfaces: 3 interface */
  0x01,                                               /* bConfigurationValue: Configuration value */
  0x00,                                               /* iConfiguration: Index of string descriptor
                                                         describing the configuration */
#if (USBD_SELF_POWERED == 1U)
  0xE0,                                               /* bmAttributes: Bus Powered according to user configuration */
#else
  0xA0,                                               /* bmAttributes: Bus Powered according to user configuration */
#endif /* USBD_SELF_POWERED */
  USBD_MAX_POWER,                                     /* MaxPower (mA) */

  /************** Descriptor of Keyboard interface ****************/
  /* 09 */
  0x09,                                               /* bLength: Interface Descriptor size */
  USB_DESC_TYPE_INTERFACE,                            /* bDescriptorType: Interface descriptor type */
  0x00,                                               /* bInterfaceNumber: Number of Interface */
  0x00,                                               /* bAlternateSetting: Alternate setting */
  0x01,                                               /* bNumEndpoints */
  0x03,                                               /* bInterfaceClass: HID */
  0x01,                                               /* bInterfaceSubClass : 1=BOOT, 0=no boot */
  0x01,                                               /* nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
  0,                                                  /* iInterface: Index of string descriptor */
  /******************** Descriptor of Keyboard HID ********************/
  /* 18 */
  0x09,                                               /* bLength: HID Descriptor size */
  HID_DESCRIPTOR_TYPE,                                /* bDescriptorType: HID */
  0x11,                                               /* bcdHID: HID Class Spec release number */
  0x01,
  0x00,                                               /* bCountryCode: Hardware target country */
  0x01,                                               /* bNumDescriptors: Number of HID class descriptors to follow */
  0x22,                                               /* bDescriptorType */
  HID_KEYBOARD_REPORT_DESC_SIZE,                      /* wItemLength: Total length of Report descriptor */
  0x00,
  /******************** Descriptor of Keyboard endpoint ********************/
  /* 27 */
  0x07,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,                             /* bDescriptorType:*/

  HID_EPIN_ADDR,                                      /* bEndpointAddress: Endpoint Address (IN) */
  0x03,                                               /* bmAttributes: Interrupt endpoint */
  HID_EPIN_SIZE,                                      /* wMaxPacketSize: */
  0x00,
  HID_HS_BINTERVAL,                                   /* bInterval: Polling Interval */
  /* 34 */


  /*---------------------------------------------------------------------------*/
  /* VIA interface descriptor */
  0x09,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_INTERFACE,                            /* bDescriptorType: */
  0x01,                                               /* bInterfaceNumber: Number of Interface */
  0x00,                                               /* bAlternateSetting: Alternate setting */
  0x02,                                               /* bNumEndpoints: Two endpoints used */
  0x03,                                               /* bInterfaceClass: HID */
  0x00,                                               /* bInterfaceSubClass : 1=BOOT, 0=no boot */
  0x00,                                               /* nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
  0x00,                                               /* iInterface */

  /******************** Descriptor of VIA ********************/
  /* 43 */
  0x09,                                               /* bLength: HID Descriptor size */
  HID_DESCRIPTOR_TYPE,                                /* bDescriptorType: HID */
  0x11,                                               /* bcdHID: HID Class Spec release number */
  0x01,
  0x00,                                               /* bCountryCode: Hardware target country */
  0x01,                                               /* bNumDescriptors: Number of HID class descriptors to follow */
  0x22,                                               /* bDescriptorType */
  HID_KEYBOARD_VIA_REPORT_DESC_SIZE,                  /* wItemLength: Total length of Report descriptor */
  0x00,

  /******************** Descriptor of VIA endpoint ********************/
  /* 52 */
  0x07,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,                             /* bDescriptorType:*/
  HID_VIA_EP_IN,                                      /* bEndpointAddress: Endpoint Address (IN) */
  USBD_EP_TYPE_INTR,                                  /* bmAttributes: Interrupt endpoint */
  HID_VIA_EP_SIZE,                                    /* wMaxPacketSize: */
  0x00,
  HID_FS_BINTERVAL,                                   /* V260901R1: FS VIA IN도 1ms polling */

  /* 59 */
  0x07,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,                             /* bDescriptorType:*/
  HID_VIA_EP_OUT,                                     /* bEndpointAddress: Endpoint Address (OUT) */
  USBD_EP_TYPE_INTR,                                  /* bmAttributes: Interrupt endpoint */
  HID_VIA_EP_SIZE,                                    /* wMaxPacketSize: */
  0x00,
  HID_FS_BINTERVAL,                                   /* V260901R1: FS VIA OUT도 1ms polling */
  /* 66 */


  /*---------------------------------------------------------------------------*/
  /* EXK interface descriptor */
  0x09,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_INTERFACE,                            /* bDescriptorType: */
  0x02,                                               /* bInterfaceNumber: Number of Interface */
  0x00,                                               /* bAlternateSetting: Alternate setting */
  0x01,                                               /* bNumEndpoints: One endpoint used */
  0x03,                                               /* bInterfaceClass: HID */
  0x01,                                               /* bInterfaceSubClass : 1=BOOT, 0=no boot */
  0x00,                                               /* nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
  0x00,                                               /* iInterface */

  /******************** Descriptor of EXK ********************/
  /* 75 */
  0x09,                                               /* bLength: HID Descriptor size */
  HID_DESCRIPTOR_TYPE,                                /* bDescriptorType: HID */
  0x11,                                               /* bcdHID: HID Class Spec release number */
  0x01,
  0x00,                                               /* bCountryCode: Hardware target country */
  0x01,                                               /* bNumDescriptors: Number of HID class descriptors to follow */
  0x22,                                               /* bDescriptorType */
  HID_EXK_REPORT_DESC_SIZE,                           /* wItemLength: Total length of Report descriptor */
  0x00,

  /******************** Descriptor of EXK endpoint ********************/
  /* 84 */
  0x07,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,                             /* bDescriptorType:*/
  HID_EXK_EP_IN,                                      /* bEndpointAddress: Endpoint Address (IN) */
  USBD_EP_TYPE_INTR,                                  /* bmAttributes: Interrupt endpoint */
  HID_EXK_EP_SIZE,                                    /* wMaxPacketSize: */
  0x00,
  HID_HS_BINTERVAL,                                   /* bInterval: Polling Interval */
  /* 91 */

};
#endif /* USE_USBD_COMPOSITE  */

/* USB HID device Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_Desc[USB_HID_DESC_SIZ] __ALIGN_END =
{
  /* 18 */
  0x09,                                               /* bLength: HID Descriptor size */
  HID_DESCRIPTOR_TYPE,                                /* bDescriptorType: HID */
  0x11,                                               /* bcdHID: HID Class Spec release number */
  0x01,
  0x00,                                               /* bCountryCode: Hardware target country */
  0x01,                                               /* bNumDescriptors: Number of HID class descriptors to follow */
  0x22,                                               /* bDescriptorType */
  HID_KEYBOARD_REPORT_DESC_SIZE,                      /* wItemLength: Total length of Report descriptor */
  0x00,
};

#ifndef USE_USBD_COMPOSITE
/* USB Standard Device Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};
#endif /* USE_USBD_COMPOSITE  */

__ALIGN_BEGIN static uint8_t HID_KEYBOARD_ReportDesc[HID_KEYBOARD_REPORT_DESC_SIZE] __ALIGN_END =
{
  0x05, 0x01,                         // USAGE_PAGE (Generic Desktop)
  0x09, 0x06,                         // USAGE (Keyboard)
  0xa1, 0x01,                         // COLLECTION (Application)
  0x05, 0x07,                         //   USAGE_PAGE (Keyboard)
  0x19, 0xe0,                         //   USAGE_MINIMUM (Keyboard LeftControl)
  0x29, 0xe7,                         //   USAGE_MAXIMUM (Keyboard Right GUI)
  0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
  0x25, 0x01,                         //   LOGICAL_MAXIMUM (1)
  0x75, 0x01,                         //   REPORT_SIZE (1)
  0x95, 0x08,                         //   REPORT_COUNT (8)
  0x81, 0x02,                         //   INPUT (Data,Var,Abs)
  0x95, 0x01,                         //   REPORT_COUNT (1)
  0x75, 0x08,                         //   REPORT_SIZE (8)
  0x81, 0x03,                         //   INPUT (Cnst,Var,Abs)
  0x95, 0x05,                         //   REPORT_COUNT (5)
  0x75, 0x01,                         //   REPORT_SIZE (1)
  0x05, 0x08,                         //   USAGE_PAGE (LEDs)
  0x19, 0x01,                         //   USAGE_MINIMUM (Num Lock)
  0x29, 0x05,                         //   USAGE_MAXIMUM (Kana)
  0x91, 0x02,                         //   OUTPUT (Data,Var,Abs)
  0x95, 0x01,                         //   REPORT_COUNT (1)
  0x75, 0x03,                         //   REPORT_SIZE (3)
  0x91, 0x03,                         //   OUTPUT (Cnst,Var,Abs)
  0x95, HW_KEYS_PRESS_MAX,            //   REPORT_COUNT (6)
  0x75, 0x08,                         //   REPORT_SIZE (8)
  0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
  0x26, 0xFF, 0x00,                   //   LOGICAL_MAXIMUM (255)
  0x05, 0x07,                         //   USAGE_PAGE (Keyboard)
  0x19, 0x00,                         //   USAGE_MINIMUM (Reserved (no event indicated))
  0x29, 0xFF,                         //   USAGE_MAXIMUM (Keyboard Application)
  0x81, 0x00,                         //   INPUT (Data,Ary,Abs)
  0xc0                                // END_COLLECTION
};

__ALIGN_BEGIN static uint8_t HID_VIA_ReportDesc[HID_KEYBOARD_VIA_REPORT_DESC_SIZE] __ALIGN_END = 
{
  //
  0x06, 0x60, 0xFF, // Usage Page (Vendor Defined)
  0x09, 0x61,       // Usage (Vendor Defined)
  0xA1, 0x01,       // Collection (Application)
  // Data to host
  0x09, 0x62,       //   Usage (Vendor Defined)
  0x15, 0x00,       //   Logical Minimum (0)
  0x26, 0xFF, 0x00, //   Logical Maximum (255)
  0x95, 32,         //   Report Count
  0x75, 0x08,       //   Report Size (8)
  0x81, 0x02,       //   Input (Data, Variable, Absolute)
  // Data from host
  0x09, 0x63,       //   Usage (Vendor Defined)
  0x15, 0x00,       //   Logical Minimum (0)
  0x26, 0xFF, 0x00, //   Logical Maximum (255)
  0x95, 32,         //   Report Count
  0x75, 0x08,       //   Report Size (8)
  0x91, 0x02,       //   Output (Data, Variable, Absolute)
  0xC0              // End Collection
};

__ALIGN_BEGIN static uint8_t HID_EXK_ReportDesc[] __ALIGN_END =
{
  //
  0x05, 0x01,               // Usage Page (Generic Desktop)
  0x09, 0x80,               // Usage (System Control)
  0xA1, 0x01,               // Collection (Application)
  0x85, REPORT_ID_SYSTEM,   //   Report ID
  0x19, 0x01,               //   Usage Minimum (Pointer)
  0x2A, 0xB7, 0x00,         //   Usage Maximum (System Display LCD Autoscale)
  0x15, 0x01,               //   Logical Minimum
  0x26, 0xB7, 0x00,         //   Logical Maximum
  0x95, 0x01,               //   Report Count (1)
  0x75, 0x10,               //   Report Size (16)
  0x81, 0x00,               //   Input (Data, Array, Absolute)
  0xC0,                     // End Collection

  0x05, 0x0C,               // Usage Page (Consumer)
  0x09, 0x01,               // Usage (Consumer Control)
  0xA1, 0x01,               // Collection (Application)
  0x85, REPORT_ID_CONSUMER, //   Report ID
  0x19, 0x01,               //   Usage Minimum (Consumer Control)
  0x2A, 0xA0, 0x02,         //   Usage Maximum (AC Desktop Show All Applications)
  0x15, 0x01,               //   Logical Minimum
  0x26, 0xA0, 0x02,         //   Logical Maximum
  0x95, 0x01,               //   Report Count (1)
  0x75, 0x10,               //   Report Size (16)
  0x81, 0x00,               //   Input (Data, Array, Absolute)
  0xC0,                     // End Collection

  // V260823R1: MOUSE 리포트 (report_mouse_t = report_id + buttons + x + y + v + h = 6B)
  0x05, 0x01,               // Usage Page (Generic Desktop)
  0x09, 0x02,               // Usage (Mouse)
  0xA1, 0x01,               // Collection (Application)
  0x85, REPORT_ID_MOUSE,    //   Report ID
  0x09, 0x01,               //   Usage (Pointer)
  0xA1, 0x00,               //   Collection (Physical)
  0x05, 0x09,               //     Usage Page (Button)
  0x19, 0x01,               //     Usage Minimum (Button 1)
  0x29, 0x08,               //     Usage Maximum (Button 8)
  0x15, 0x00,               //     Logical Minimum (0)
  0x25, 0x01,               //     Logical Maximum (1)
  0x95, 0x08,               //     Report Count (8)
  0x75, 0x01,               //     Report Size (1)
  0x81, 0x02,               //     Input (Data, Variable, Absolute)
  0x05, 0x01,               //     Usage Page (Generic Desktop)
  0x09, 0x30,               //     Usage (X)
  0x09, 0x31,               //     Usage (Y)
  0x15, 0x81,               //     Logical Minimum (-127)
  0x25, 0x7F,               //     Logical Maximum (127)
  0x95, 0x02,               //     Report Count (2)
  0x75, 0x08,               //     Report Size (8)
  0x81, 0x06,               //     Input (Data, Variable, Relative)
  0x09, 0x38,               //     Usage (Wheel)
  0x15, 0x81,               //     Logical Minimum (-127)
  0x25, 0x7F,               //     Logical Maximum (127)
  0x95, 0x01,               //     Report Count (1)
  0x75, 0x08,               //     Report Size (8)
  0x81, 0x06,               //     Input (Data, Variable, Relative)
  0x05, 0x0C,               //     Usage Page (Consumer)
  0x0A, 0x38, 0x02,         //     Usage (AC Pan)
  0x15, 0x81,               //     Logical Minimum (-127)
  0x25, 0x7F,               //     Logical Maximum (127)
  0x95, 0x01,               //     Report Count (1)
  0x75, 0x08,               //     Report Size (8)
  0x81, 0x06,               //     Input (Data, Variable, Relative)
  0xC0,                     //   End Collection
  0xC0                      // End Collection
};

// V260823R1: 리포트 디스크립터가 선언한 크기와 QMK 구조체 크기는 반드시 같아야 한다.
//            어긋나면 호스트가 리포트를 잘못 해석하므로 링크가 아니라 컴파일에서 막는다.
// 배열 크기를 상수로 고정하면 짧은 초기화가 0으로 채워져 호스트에 그대로 보고된다.
_Static_assert(sizeof(HID_EXK_ReportDesc) == HID_EXK_REPORT_DESC_SIZE, "EXK 리포트 디스크립터 길이와 선언 크기가 다르다.");
#ifdef MOUSE_SHARED_EP
_Static_assert(sizeof(report_mouse_t) == 6U, "MOUSE 리포트 디스크립터(6B)와 report_mouse_t 크기가 다르다.");
_Static_assert(sizeof(report_mouse_t) <= HID_EXK_EP_SIZE, "MOUSE 리포트가 EXK 엔드포인트 크기를 넘는다.");
#endif
_Static_assert(sizeof(report_extra_t) == 3U, "SYSTEM/CONSUMER 리포트 디스크립터(3B)와 report_extra_t 크기가 다르다.");

static USBD_HID_HandleTypeDef *p_hhid = NULL;

static bool usbHidAdmissionOpen(void)
{
  return p_hhid != NULL && !usbIsResetPending();
}

static void usbHidRetireSessionLocked(void)
{
  // Retiring software admission cannot release storage still owned by the controller.
  if (p_hhid != NULL) {
    p_hhid = NULL;
    transport_generation++;
  }
  ep0_led_pending = false;
  wake_state = USB_HID_WAKE_IDLE;
  wake_skip_stale_sof = false;
}
static uint8_t HIDInEpAdd = HID_EPIN_ADDR;
extern USBD_HandleTypeDef USBD_Device;

/**
  * @brief  USBD_HID_Init
  *         Initialize the HID interface
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);
  // V260909R1: 구성마다 새 세대, 완전 초기화. 정상적인 DeInit 없는 재진입도 누적 할당하지 않는다.
  // A failed teardown keeps its handle and payloads until a later lifecycle teardown succeeds.
  if (p_hhid == NULL && pdev->pClassDataCmsit[pdev->classId] != NULL) return (uint8_t)USBD_FAIL;
  if (p_hhid != NULL && USBD_HID_DeInit(pdev, cfgidx) != USBD_OK) return (uint8_t)USBD_FAIL;
  USBD_HID_HandleTypeDef *hhid = USBD_malloc(sizeof(*hhid));
  if (hhid == NULL) return (uint8_t)USBD_EMEM;
  memset(hhid, 0, sizeof(*hhid));
  hhid->Protocol = 1U;
  pdev->pClassDataCmsit[pdev->classId] = hhid;
  pdev->pClassData = hhid;
  p_hhid = hhid;
  usbHidResetTransport();
#ifdef USE_USBD_COMPOSITE
  HIDInEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_INTR, (uint8_t)pdev->classId);
#endif
  const uint8_t endpoints[] = {HIDInEpAdd, HID_VIA_EP_IN, HID_EXK_EP_IN};
  const uint16_t sizes[] = {HID_EPIN_SIZE, HID_VIA_EP_SIZE, HID_EXK_EP_SIZE};
  uint8_t interval = pdev->dev_speed == USBD_SPEED_HIGH ? usbBootModeGetHsInterval() : HID_FS_BINTERVAL;
  for (uint32_t i = 0; i < 3U; i++) {
    if (USBD_LL_OpenEP(pdev, endpoints[i], USBD_EP_TYPE_INTR, sizes[i]) != USBD_OK) goto fail;
    pdev->ep_in[endpoints[i] & 0xFU].is_used = 1U;
    pdev->ep_in[endpoints[i] & 0xFU].bInterval = interval;
  }
  if (USBD_LL_OpenEP(pdev, HID_VIA_EP_OUT, USBD_EP_TYPE_INTR, HID_VIA_EP_SIZE) != USBD_OK) goto fail;
  pdev->ep_out[HID_VIA_EP_OUT & 0xFU].is_used = 1U;
  pdev->ep_out[HID_VIA_EP_OUT & 0xFU].bInterval = interval;
  usbHidRearmViaLocked(pdev);
  if (!via_rx_armed) goto fail;
  return (uint8_t)USBD_OK;
fail:
  USBD_HID_DeInit(pdev, cfgidx);
  return (uint8_t)USBD_FAIL;
}

/**
  * @brief  USBD_HID_DeInit
  *         DeInitialize the HID layer
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);
  const uint8_t endpoints[] = {HIDInEpAdd, HID_VIA_EP_IN, HID_EXK_EP_IN};
  uint8_t ret = (uint8_t)USBD_OK;
  // Stop class admission before closing hardware; failed closes still own their storage.
  uint32_t irq = usbHidLock();
  usbHidRetireSessionLocked();
  usbHidUnlock(irq);
  for (uint32_t i = 0; i < 3U; i++) {
    if (pdev->ep_in[endpoints[i] & 0xFU].is_used) {
      if (USBD_LL_CloseEP(pdev, endpoints[i]) != USBD_OK) {
        ret = (uint8_t)USBD_FAIL;
        continue;
      }
      pdev->ep_in[endpoints[i] & 0xFU].is_used = 0U;
      pdev->ep_in[endpoints[i] & 0xFU].bInterval = 0U;
    }
  }
  if (pdev->ep_out[HID_VIA_EP_OUT & 0xFU].is_used) {
    if (USBD_LL_CloseEP(pdev, HID_VIA_EP_OUT) != USBD_OK) {
      ret = (uint8_t)USBD_FAIL;
    } else {
      pdev->ep_out[HID_VIA_EP_OUT & 0xFU].is_used = 0U;
      pdev->ep_out[HID_VIA_EP_OUT & 0xFU].bInterval = 0U;
    }
  }
  // hidTxInit also clears active.data. A close/flush failure cannot authorize that reuse.
  if (ret != USBD_OK) return ret;
  void *handle = pdev->pClassDataCmsit[pdev->classId];
  if (pdev->pClassData == handle) pdev->pClassData = NULL;
  pdev->pClassDataCmsit[pdev->classId] = NULL;
  USBD_free(handle);
  usbHidResetTransport();
  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_Setup
  *         Handle the HID specific requests
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t USBD_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  USBD_StatusTypeDef ret = USBD_OK;
  uint16_t len;
  uint8_t *pbuf;
  // EP0 keeps this buffer after Setup returns; the FIFO reads a full word.
  __ALIGN_BEGIN static const uint32_t status_info __ALIGN_END = 0U;

  if (hhid == NULL || hhid != p_hhid || !usbHidAdmissionOpen())
  {
    return (uint8_t)USBD_FAIL;
  }

  ep0_led_pending = false;  // V260909R1: 새 SETUP은 이전 control OUT 소유권을 취소한다.
  logDebug("HID_SETUP %d\n", pdev->classId);
  logDebug("  req->bmRequest : 0x%X\n", req->bmRequest);
  logDebug("  req->bRequest  : 0x%X\n", req->bRequest);
  logDebug("       wIndex    : 0x%X\n", req->wIndex);
  logDebug("       wLength   : 0x%X %d\n", req->wLength, req->wLength);

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS :
      switch (req->bRequest)
      {
        case USBD_HID_REQ_SET_PROTOCOL:
          logDebug("  USBD_HID_REQ_SET_PROTOCOL  : 0x%X, 0x%d\n", req->wValue, req->wLength);      
          if (req->bmRequest != 0x21U || req->wValue > 1U || req->wLength != 0U) {
            transport_stats.invalid_control++;
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;
          }
          // Boot 형식은 keyboard 인터페이스(0)에만 있다. 다른 인터페이스는 요청을 받아도 Report protocol이다.
          if (req->wIndex == 0U) {
            uint32_t irq = usbHidLock();
            usbHidSetKeyboardProtocolLocked(pdev, hhid, (uint8_t)req->wValue);
            usbHidUnlock(irq);
          }
          break;

        case USBD_HID_REQ_GET_PROTOCOL:
          logDebug("  USBD_HID_REQ_GET_PROTOCOL  : 0x%X, 0x%d\n", req->wValue, req->wLength);      
          (void)USBD_CtlSendData(pdev, req->wIndex == 0U ? (uint8_t *)&hhid->Protocol : (uint8_t *)&hid_report_protocol, 1U);
          break;

        case USBD_HID_REQ_SET_IDLE:
          logDebug("  USBD_HID_REQ_SET_IDLE  : 0x%X, 0x%d\n", req->wValue, req->wLength);      
          hhid->IdleState = (uint8_t)(req->wValue >> 8);
          break;

        case USBD_HID_REQ_GET_IDLE:
          logDebug("  USBD_HID_REQ_GET_IDLE  : 0x%X, 0x%d\n", req->wValue, req->wLength);          
          (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->IdleState, 1U);
          break;

        case USBD_HID_REQ_SET_REPORT:  
          logDebug("  USBD_HID_REQ_SET_REPORT  : 0x%X, 0x%d\n", req->wValue, req->wLength);     
          // V260909R1: keyboard/output/report-id 0, 정확히 1바이트만 허용한다.
          if (req->bmRequest != 0x21U || req->wIndex != 0U || req->wValue != 0x0200U || req->wLength != 1U) {
            transport_stats.invalid_control++;
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;
          }
          ep0_req_buf[0] = 0U;
          ret = USBD_CtlPrepareRx(pdev, ep0_req_buf, 1U);
          ep0_led_pending = ret == USBD_OK;
          break;

        default:
          logDebug("  ERROR  : 0x%X\n", req->wValue); 
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;
    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_STATUS:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_GET_DESCRIPTOR:
          logDebug("  USB_REQ_GET_DESCRIPTOR  : 0x%X\n", req->wValue); 
          if ((req->wValue >> 8) == HID_REPORT_DESC)
          {
            switch(req->wIndex)
            {
              case 1:
                len = MIN(HID_KEYBOARD_VIA_REPORT_DESC_SIZE, req->wLength);
                pbuf = HID_VIA_ReportDesc;
                break;

              case 2:
                len = MIN(HID_EXK_REPORT_DESC_SIZE, req->wLength);
                pbuf = HID_EXK_ReportDesc;
                break;

              default:
                len = MIN(HID_KEYBOARD_REPORT_DESC_SIZE, req->wLength);
                pbuf = HID_KEYBOARD_ReportDesc;
              break;
            }
          }
          else if ((req->wValue >> 8) == HID_DESCRIPTOR_TYPE)
          {
            pbuf = USBD_HID_Desc;
            len = MIN(USB_HID_DESC_SIZ, req->wLength);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;
          }
          (void)USBD_CtlSendData(pdev, pbuf, len);
          break;

        case USB_REQ_GET_INTERFACE :
          logDebug("  USB_REQ_GET_INTERFACE  : 0x%X\n", req->wValue); 
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->AltSetting, 1U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_SET_INTERFACE:
          logDebug("  USB_REQ_SET_INTERFACE  : 0x%X\n", req->wValue); 
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            hhid->AltSetting = (uint8_t)(req->wValue);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_CLEAR_FEATURE:
          logDebug("  USB_REQ_CLEAR_FEATURE  : 0x%X\n", req->wValue); 
          break;

        default:
          logDebug("  ERROR  : 0x%X\n", req->wValue); 
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }

  return (uint8_t)ret;
}

/**
  * @brief  USBD_HID_EP0_RxReady
  *         handle EP0 Rx Ready event
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t USBD_HID_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  if (usbHidAdmissionOpen() && ep0_led_pending && USBD_LL_GetRxDataSize(pdev, 0U) == 1U)
    usbHidSetStatusLed(ep0_req_buf[0]);
  ep0_led_pending = false;
  return (uint8_t)USBD_OK;
}




#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
uint8_t *USBD_HID_GetUsrStrDescriptor(struct _USBD_HandleTypeDef *pdev, uint8_t index,  uint16_t *length)
{
  logPrintf("USBD_HID_GetUsrStrDescriptor() %d\n", index);
  return USBD_HID_ProductStrDescriptor(pdev->dev_speed, length);
}
#endif

#ifndef USE_USBD_COMPOSITE
/**
  * @brief  USBD_HID_GetCfgFSDesc
  *         return FS configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetFSCfgDesc(uint16_t *length)
{
  // V260909R1: HS descriptor 조회가 공유 배열을 변경하므로 FS 전환 시 모든 EP를 복원한다.
  const uint8_t endpoints[] = {HID_EPIN_ADDR, HID_VIA_EP_IN, HID_VIA_EP_OUT, HID_EXK_EP_IN};
  for (uint32_t i = 0; i < sizeof(endpoints); i++) {
    USBD_EpDescTypeDef *pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, endpoints[i]);
    if (pEpDesc != NULL) pEpDesc->bInterval = HID_FS_BINTERVAL;
  }

  *length = (uint16_t)sizeof(USBD_HID_CfgDesc);
  return USBD_HID_CfgDesc;
}

/**
  * @brief  USBD_HID_GetCfgHSDesc
  *         return HS configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetHSCfgDesc(uint16_t *length)
{
  uint8_t             hs_interval = usbBootModeGetHsInterval();
  USBD_EpDescTypeDef *pEpDesc    = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_EPIN_ADDR);

  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = hs_interval;                                 // V250923R1 Keyboard HS polling interval
  }

  pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_VIA_EP_IN);
  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = hs_interval;                                 // V250923R1 VIA IN polling interval
  }

  pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_VIA_EP_OUT);
  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = hs_interval;                                 // V250923R1 VIA OUT polling interval
  }

  pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_EXK_EP_IN);
  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = hs_interval;                                 // V250923R1 EXK polling interval
  }

  *length = (uint16_t)sizeof(USBD_HID_CfgDesc);
  return USBD_HID_CfgDesc;
}

/**
  * @brief  USBD_HID_GetOtherSpeedCfgDesc
  *         return other speed configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetOtherSpeedCfgDesc(uint16_t *length)
{
  // V260909R1: HS descriptor 조회가 공유 배열을 변경하므로 FS 전환 시 모든 EP를 복원한다.
  const uint8_t endpoints[] = {HID_EPIN_ADDR, HID_VIA_EP_IN, HID_VIA_EP_OUT, HID_EXK_EP_IN};
  for (uint32_t i = 0; i < sizeof(endpoints); i++) {
    USBD_EpDescTypeDef *pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, endpoints[i]);
    if (pEpDesc != NULL) pEpDesc->bInterval = HID_FS_BINTERVAL;
  }

  *length = (uint16_t)sizeof(USBD_HID_CfgDesc);
  return USBD_HID_CfgDesc;
}
#endif /* USE_USBD_COMPOSITE  */


/**
  * @brief  USBD_HID_DataIn
  *         handle data IN Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  uint32_t irq = usbHidLock();
  if (usbHidAdmissionOpen()) {
    if (epnum == (HIDInEpAdd & 0xFU)) {
      if (hidTxComplete(&keyboard_tx))
      {
        keyboard_completed_us = micros();
        keyboard_completed = true;
        keyboard_delay_ms = keyboard_active_delay_ms;
        keyboard_active_delay_ms = 0U;
      }
    } else if (epnum == (HID_EXK_EP_IN & 0xFU)) {
      hidTxComplete(&extra_tx);
    } else if (epnum == (HID_VIA_EP_IN & 0xFU)) {
      hidTxComplete(&via_tx);
    }
    // V260909R1: 별도 타이머 위상/다음 SOF를 기다리지 않고 다음 head를 즉시 무장한다.
    usbHidPumpLocked(pdev);
  }
  usbHidUnlock(irq);
  return (uint8_t)USBD_OK;
}

static uint8_t USBD_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  uint32_t irq = usbHidLock();
  if (!usbHidAdmissionOpen() || epnum != (HID_VIA_EP_OUT & 0xFU) || !via_rx_armed) {
    usbHidUnlock(irq);
    return (uint8_t)USBD_FAIL;
  }
  via_rx_armed = false;
  uint32_t rx_size = USBD_LL_GetRxDataSize(pdev, epnum);
  if (rx_size == HID_VIA_EP_SIZE && via_rx_count < HID_VIA_RX_DEPTH) {
    memcpy(via_rx[(via_rx_head + via_rx_count) % HID_VIA_RX_DEPTH], via_hid_usb_rx_report, HID_VIA_EP_SIZE);
    via_rx_count++;
  } else {
    transport_stats.invalid_rx++;
  }
  // V260909R1: 여유가 있으면 즉시 재무장, 꽉 차면 ACK 후 폐기 대신 OUT NAK로 backpressure.
  usbHidRearmViaLocked(pdev);
  usbHidUnlock(irq);
  return (uint8_t)USBD_OK;
}

static uint8_t USBD_HID_SOF(USBD_HandleTypeDef *pdev)
{
  uint32_t irq = usbHidLock();
  usbHidPumpLocked(pdev);  // V260909R1: arm 실패/재구성 후의 bounded 재시도만 담당한다.
  usbHidRearmViaLocked(pdev);
  usbHidUnlock(irq);
  return (uint8_t)USBD_OK;
}

#ifndef USE_USBD_COMPOSITE
/**
  * @brief  DeviceQualifierDescriptor
  *         return Device Qualifier descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_HID_DeviceQualifierDesc);

  return USBD_HID_DeviceQualifierDesc;
}
#endif /* USE_USBD_COMPOSITE  */

// V260909R1: TX FIFO, PCD register RMW, lifecycle은 동일 PRIMASK 규약 아래에서만 접근한다.
static bool usbHidArm(void *context, const hid_tx_packet_t *packet)
{
  uint8_t ep = (uint8_t)(uintptr_t)context;
  uint8_t *data = (uint8_t *)packet->data;
  uint32_t length = packet->length;
  if (ep == HIDInEpAdd && p_hhid != NULL && p_hhid->Protocol == 0U) {
    // Boot 호스트는 report descriptor 없이 8바이트만 읽는다. 앞에서부터 비어 있지 않은 슬롯 6개를 싣는다.
    memset(keyboard_boot_report, 0, sizeof(keyboard_boot_report));
    keyboard_boot_report[0] = packet->data[0];
    for (uint32_t i = 2U, n = 2U; i < HID_KEYBOARD_REPORT_SIZE && n < sizeof(keyboard_boot_report); i++)
      if (packet->data[i] != 0U) keyboard_boot_report[n++] = packet->data[i];
    data = keyboard_boot_report;
    length = sizeof(keyboard_boot_report);
  }
  bool ok = USBD_LL_Transmit(&USBD_Device, ep, data, length) == USBD_OK;
  if (!ok) transport_stats.arm_failures++;
  if (ok && ep == HIDInEpAdd) keyboard_active_delay_ms = packet->delay_after_ms;
  return ok;
}

static bool usbHidSessionValid(USBD_HandleTypeDef *pdev)
{
  return usbHidAdmissionOpen() && (pdev->dev_state == USBD_STATE_CONFIGURED ||
      (pdev->dev_state == USBD_STATE_SUSPENDED && pdev->dev_old_state == USBD_STATE_CONFIGURED));
}

// Endpoint metadata belongs to the admitted HID session, independently of pending BootMode.
const char *usbHidGetPollingLabel(void)
{
  uint32_t irq = usbHidLock();
  uint8_t ep = HIDInEpAdd & 0x0FU;
  bool valid = usbHidSessionValid(&USBD_Device) && USBD_Device.ep_in[ep].is_used;
  uint8_t speed = USBD_Device.dev_speed;
  uint8_t interval = USBD_Device.ep_in[ep].bInterval;
  usbHidUnlock(irq);

  if (valid) {
    if (speed == USBD_SPEED_FULL && interval == 1U) return "1000 Hz (FS)";
    if (speed == USBD_SPEED_HIGH) {
      switch (interval) {
        case 1U: return "8000 Hz (HS)";
        case 2U: return "4000 Hz (HS)";
        case 3U: return "2000 Hz (HS)";
      }
    }
  }
  return "Unavailable";
}

static bool usbHidFreezeKeyCandidateLocked(void)
{
  if (!keyboard_candidate.valid) return true;
  if (!hidTxPush(&keyboard_tx, &keyboard_candidate.packet)) return false;
  keyboard_candidate.valid = false;
  return true;
}

static void usbHidPumpLocked(USBD_HandleTypeDef *pdev)
{
  if (!usbHidSessionValid(pdev)) return;
  // No end-of-scan, key-count or SOF gate: completion can consume even one update.
  if (!keyboard_tx.busy && !usbHidFreezeKeyCandidateLocked()) return;
  // 정상 경로는 모든 전이를 FIFO에 유지한다. 유한 큐 overflow 뒤에만 최종 상태를 복구한다.
  if (keyboard_reconcile && keyboard_tx.count == 0U) {
    hidTxPush(&keyboard_tx, &keyboard_latest);
    keyboard_reconcile = false;
  }
  if (extra_reconcile && extra_tx.count == 0U) {
    for (uint8_t i = 0; i < 3U; i++)
      if (extra_reconcile & (1U << i)) hidTxPush(&extra_tx, &extra_latest[i]);
    extra_reconcile = 0U;
  }
  // V260909R1: 같은 세션의 suspend는 전이 큐를 유지하되 물리 IN 무장은 resume 이후만 한다.
  if (pdev->dev_state != USBD_STATE_CONFIGURED) return;
  // V260911R3: 기본 경로는 시계를 읽지 않는다. 유지 중에도 다른 IN 엔드포인트는 계속 서비스한다.
  if (keyboard_delay_ms == 0U || (uint32_t)(micros() - keyboard_completed_us) >= (uint32_t)keyboard_delay_ms * 1000U)
  {
    keyboard_delay_ms = 0U;
    hidTxKick(&keyboard_tx, usbHidArm, (void *)(uintptr_t)HIDInEpAdd);
  }
  hidTxKick(&extra_tx, usbHidArm, (void *)(uintptr_t)HID_EXK_EP_IN);
  hidTxKick(&via_tx, usbHidArm, (void *)(uintptr_t)HID_VIA_EP_IN);
}

// 형식은 무장할 때 정하므로 대기 중인 보고도 새 형식으로 나간다. 호스트는 protocol을 바꾸면 이전 형식의 키
// 상태를 버리므로 현재 상태를 새 형식으로 한 번 더 보낸다.
static void usbHidSetKeyboardProtocolLocked(USBD_HandleTypeDef *pdev, USBD_HID_HandleTypeDef *hhid, uint8_t protocol)
{
  if (hhid->Protocol == protocol) return;
  usbHidFreezeKeyCandidateLocked();
  hhid->Protocol = protocol;
  hid_tx_packet_t resync = keyboard_latest;
  resync.delay_after_ms = 0U;
  if (keyboard_reconcile || !usbHidSessionValid(pdev) || !hidTxPush(&keyboard_tx, &resync)) keyboard_reconcile = true;
  usbHidPumpLocked(pdev);
}

static void usbHidRearmViaLocked(USBD_HandleTypeDef *pdev)
{
  if (usbHidAdmissionOpen() && !via_rx_armed && via_rx_count < HID_VIA_RX_DEPTH &&
      pdev->ep_out[HID_VIA_EP_OUT & 0xFU].is_used) {
    via_rx_armed = USBD_LL_PrepareReceive(pdev, HID_VIA_EP_OUT, via_hid_usb_rx_report,
                                        sizeof(via_hid_usb_rx_report)) == USBD_OK;
  }
}

static void usbHidResetTransport(void)
{
  transport_stats.session_discards += keyboard_tx.count + extra_tx.count + via_tx.count + via_rx_count +
                                     (keyboard_candidate.valid ? 1U : 0U);
  memset(&keyboard_candidate, 0, sizeof(keyboard_candidate));
  transport_generation++;
  hidTxInit(&keyboard_tx, keyboard_slots, HID_TX_DEPTH);
  hidTxInit(&extra_tx, extra_slots, HID_TX_DEPTH);
  hidTxInit(&via_tx, via_slots, HID_TX_DEPTH);
  keyboard_reconcile = true;
  extra_reconcile = 7U;
  keyboard_latest.delay_after_ms = 0U;
  keyboard_delay_ms = keyboard_active_delay_ms = 0U;
  keyboard_completed = false;
  via_rx_count = via_rx_head = 0U;
  via_rx_armed = false;
  ep0_led_pending = false;
  wake_state = USB_HID_WAKE_IDLE;
  wake_skip_stale_sof = false;
}

bool usbHidReadViaRequest(uint8_t *data, uint32_t *generation)
{
  if (data == NULL || generation == NULL) return false;
  uint32_t irq = usbHidLock();
  // 응답 슬롯을 확보할 수 있을 때만 부작용을 가진 다음 명령을 꺼낸다. TX 생산자는 하나다.
  bool ready = usbHidAdmissionOpen() && USBD_Device.dev_state == USBD_STATE_CONFIGURED &&
               via_rx_count != 0U && via_tx.count < via_tx.capacity;
  if (ready) {
    memcpy(data, via_rx[via_rx_head], HID_VIA_EP_SIZE);
    *generation = transport_generation;
    via_rx_head = (via_rx_head + 1U) % HID_VIA_RX_DEPTH;
    via_rx_count--;
    usbHidRearmViaLocked(&USBD_Device);
  }
  usbHidUnlock(irq);
  return ready;
}

bool usbHidEnqueueViaResponse(const uint8_t *data, uint8_t length, uint32_t generation)
{
  if (data == NULL || length != HID_VIA_EP_SIZE) return false;
  hid_tx_packet_t packet = { .length = HID_VIA_EP_SIZE };
  memcpy(packet.data, data, length);
  uint32_t irq = usbHidLock();
  bool ok = usbHidAdmissionOpen() && generation == transport_generation && hidTxPush(&via_tx, &packet);
  usbHidPumpLocked(&USBD_Device);
  usbHidUnlock(irq);
  return ok;
}

static bool usbHidRemoteWakeSuspended(void)
{
  uint32_t irq = usbHidLock();
  PCD_HandleTypeDef *pcd = (PCD_HandleTypeDef *)USBD_Device.pData;
  bool allowed = usbHidAdmissionOpen() && USBD_Device.dev_state == USBD_STATE_SUSPENDED && USBD_Device.dev_remote_wakeup &&
                 pcd != NULL && pcd->Instance != NULL;
  uint32_t suspend_stamp = suspend_ms;
  uint32_t suspend_epoch = wake_suspend_epoch;
  uint32_t generation = transport_generation;
  usbHidUnlock(irq);
  if (!allowed) return false;

  // USB 2.0 requires at least 5 ms of suspend before device-driven resume signaling.
  uint32_t suspended_for = (uint32_t)(millis() - suspend_stamp);
  if (suspended_for < 5U) delay(5U - suspended_for);

  irq = usbHidLock();
  if (!usbHidAdmissionOpen() || USBD_Device.dev_state != USBD_STATE_SUSPENDED || !USBD_Device.dev_remote_wakeup ||
      USBD_Device.pData != pcd || pcd->Instance == NULL ||
      suspend_epoch != wake_suspend_epoch || generation != transport_generation) {
    usbHidUnlock(irq);
    return false;
  }

  USB_OTG_DeviceTypeDef *device = usbHidDeviceRegisters(pcd);
  if ((device->DSTS & USB_OTG_DSTS_SUSPSTS) == 0U) {
    usbHidUnlock(irq);
    return false;
  }

  // H7RS can raise WKUINT immediately when RWUSIG is asserted. Mask only WUIM for
  // the commanded pulse so the vendor IRQ handler cannot clear RWUSIG prematurely.
  uint32_t saved_wuim = pcd->Instance->GINTMSK & USB_OTG_GINTMSK_WUIM;
  if (saved_wuim != 0U) {
    pcd->Instance->GINTMSK &= ~USB_OTG_GINTMSK_WUIM;
    __DSB();
  }

  wake_skip_stale_sof = (pcd->Instance->GINTSTS & USB_OTG_GINTSTS_SOF) != 0U;
  wake_state = USB_HID_WAKE_SIGNALING;

  // Suspend gates STOPCLK in usbd_conf.c. Use only the matching ST HAL ungate;
  // do not manipulate GATECLK or add PHY recovery sequences.
  __HAL_PCD_UNGATE_PHYCLOCK(pcd);
  (void)HAL_PCD_ActivateRemoteWakeup(pcd);
  bool asserted = (device->DCTL & USB_OTG_DCTL_RWUSIG) != 0U;
  if (!asserted) {
    wake_state = USB_HID_WAKE_IDLE;
    wake_skip_stale_sof = false;
    if (saved_wuim != 0U) {
      pcd->Instance->GINTMSK |= saved_wuim;
      __DSB();
    }
    usbHidUnlock(irq);
    return false;
  }
  usbHidUnlock(irq);

  // Keep RWUSIG asserted for the intended 10 ms window, independent of early WKUINT.
  delay(10U);

  irq = usbHidLock();
  if (USBD_Device.pData == pcd && pcd->Instance != NULL) {
    // A reset/reconfiguration owns the new USB generation; the old wake attempt
    // must not deassert or classify signals in that new session.
    if (generation == transport_generation) {
      (void)HAL_PCD_DeActivateRemoteWakeup(pcd);

      if (wake_state == USB_HID_WAKE_SIGNALING) {
        // Discard only the device-generated early WKUINT. If hardware has actually
        // resumed, preserve WKUINT so the normal HAL callback can complete USBD resume.
        if ((pcd->Instance->GINTSTS & USB_OTG_GINTSTS_WKUINT) != 0U &&
            (device->DSTS & USB_OTG_DSTS_SUSPSTS) != 0U) {
          __HAL_PCD_CLEAR_FLAG(pcd, USB_OTG_GINTSTS_WKUINT);
          __DSB();
        }
        wake_state = (suspend_epoch == wake_suspend_epoch &&
                      USBD_Device.dev_state == USBD_STATE_SUSPENDED) ?
                     USB_HID_WAKE_WAIT_RESUME : USB_HID_WAKE_IDLE;
      }
    }
    // Restore only the interrupt bit this wake attempt changed.
    if (saved_wuim != 0U) {
      pcd->Instance->GINTMSK |= saved_wuim;
      __DSB();
    }
  }
  usbHidUnlock(irq);
  return true;
}

// Suspend entered by a host that had configured this device. Bus idle before enumeration also
// raises Suspend, but no host is asleep then and a key held at power-on must still reach it.
bool usbHidHostSleeping(void)
{
  return usbHidAdmissionOpen() && USBD_Device.dev_state == USBD_STATE_SUSPENDED && USBD_Device.dev_old_state == USBD_STATE_CONFIGURED;
}

bool usbHidRequestRemoteWakeFromInput(void)
{
  // Keep the configured physical-key path separate from all slow wake machinery.
  if (USBD_Device.dev_state != USBD_STATE_SUSPENDED) return false;
  return usbHidRemoteWakeSuspended();
}

// V260911R3: 마지막으로 수락한 리포트 뒤의 간격을 연장한다. active payload는 불변이다.
void usbHidDelayKeyboardReport(uint16_t delay_ms)
{
  if (delay_ms == 0U)
  {
    return;
  }
  uint32_t irq = usbHidLock();
  if (usbHidSessionValid(&USBD_Device))
  {
    uint16_t *interval = NULL;
    usbHidFreezeKeyCandidateLocked();
    if (keyboard_candidate.valid)
    {
      interval = &keyboard_candidate.packet.delay_after_ms;
    }
    else if (keyboard_tx.count != 0U)
    {
      uint16_t tail = (keyboard_tx.head + keyboard_tx.count - 1U) % keyboard_tx.capacity;
      interval = &keyboard_tx.slots[tail].delay_after_ms;
    }
    else if (keyboard_tx.busy)
    {
      interval = &keyboard_active_delay_ms;
    }
    else if (keyboard_completed)
    {
      interval = &keyboard_delay_ms;
    }
    if (interval != NULL && *interval < delay_ms)
    {
      *interval = delay_ms;
    }
  }
  usbHidUnlock(irq);
}

/* Validate the producer's declared single-key update against the preceding
 * snapshot. Slot reordering, a filter changing other usages, duplicates, and
 * modifiers all fall back to ordinary immutable reports. */
static bool usbHidIsSingleKeyUpdate(const hid_tx_packet_t *packet, uint8_t usage, bool pressed)
{
  if (usage < 4U || usage >= 0xE0U || packet->data[0] != 0U || packet->data[1] != 0U ||
      keyboard_latest.data[0] != 0U || keyboard_latest.data[1] != 0U) return false;
  bool changed = false;
  for (uint32_t i = 2U; i < HID_KEYBOARD_REPORT_SIZE; i++) {
    uint8_t before = keyboard_latest.data[i], after = packet->data[i];
    if (before == after) {
      if (before == usage) return false;
      continue;
    }
    if (changed || (pressed ? (before != 0U || after != usage) : (before != usage || after != 0U)))
      return false;
    changed = true;
  }
  return changed;
}

static bool usbHidSubmitKeyboard(uint8_t *data, uint16_t length, uint32_t scan_token, uint8_t usage, bool pressed)
{
  if (data == NULL || length != HID_KEYBOARD_REPORT_SIZE) return false;
  hid_tx_packet_t packet = { .length = HID_KEYBOARD_REPORT_SIZE };
  memcpy(packet.data, data, length);
  uint32_t irq = usbHidLock();
  usbHidPumpLocked(&USBD_Device);
  bool configured = usbHidSessionValid(&USBD_Device);
  bool eligible = scan_token != 0U && configured && USBD_Device.dev_state == USBD_STATE_CONFIGURED &&
                  p_hhid->Protocol == 1U && !keyboard_reconcile && keyboard_tx.busy &&
                  keyboard_tx.count == 0U && keyboard_active_delay_ms == 0U && keyboard_delay_ms == 0U &&
                  usbHidIsSingleKeyUpdate(&packet, usage, pressed);
  bool merge = eligible && keyboard_candidate.valid && keyboard_candidate.scan_token == scan_token &&
               keyboard_candidate.pressed == pressed &&
               (keyboard_candidate.touched[usage / 8U] & (1U << (usage % 8U))) == 0U;
  bool ok = false;
  if (merge) {
    memcpy(keyboard_candidate.packet.data, packet.data, packet.length);
    keyboard_candidate.touched[usage / 8U] |= 1U << (usage % 8U);
    ok = true;
  } else if (usbHidFreezeKeyCandidateLocked()) {
    if (eligible && keyboard_tx.count == 0U) {
      keyboard_candidate.packet = packet;
      keyboard_candidate.scan_token = scan_token;
      memset(keyboard_candidate.touched, 0, sizeof(keyboard_candidate.touched));
      keyboard_candidate.touched[usage / 8U] = 1U << (usage % 8U);
      keyboard_candidate.pressed = pressed;
      keyboard_candidate.valid = true;
      ok = true;
    } else {
      ok = configured && !keyboard_reconcile && hidTxPush(&keyboard_tx, &packet);
    }
  }
  keyboard_latest = packet;
  if (!ok) {
    if (configured) {
      transport_stats.keyboard_coalesced++;
    }
    keyboard_reconcile = true;
  }
  usbHidPumpLocked(&USBD_Device);
  usbHidUnlock(irq);
  return ok;
}

bool usbHidSendReport(uint8_t *data, uint16_t length)
{
  return usbHidSubmitKeyboard(data, length, 0U, 0U, false);
}

bool usbHidSubmitKeyUpdate(uint8_t *data, uint16_t length, uint32_t scan_token, uint8_t usage, bool pressed)
{
  return usbHidSubmitKeyboard(data, length, scan_token, usage, pressed);
}

void usbHidEndKeyScan(uint32_t scan_token)
{
  uint32_t irq = usbHidLock();
  if (keyboard_candidate.valid && (scan_token == 0U || keyboard_candidate.scan_token == scan_token)) {
    usbHidFreezeKeyCandidateLocked();
    usbHidPumpLocked(&USBD_Device);
  }
  usbHidUnlock(irq);
}

bool usbHidSendReportEXK(uint8_t *data, uint16_t length)
{
  if (data == NULL || length == 0U || data[0] < 2U || data[0] > 4U ||
      length != (data[0] == 2U ? 6U : 3U)) return false;
  hid_tx_packet_t packet = { .length = (uint8_t)length };
  memcpy(packet.data, data, length);
  uint32_t irq = usbHidLock();
  usbHidFreezeKeyCandidateLocked();
  usbHidPumpLocked(&USBD_Device);
  uint8_t index = data[0] - 2U;
  extra_latest[index] = packet;
  // 상대 이동/휠은 재연결 및 overflow 복구에서 반복하지 않는다. 버튼만 현재 상태다.
  if (index == 0U) memset(&extra_latest[0].data[2], 0, 4U);
  bool configured = usbHidSessionValid(&USBD_Device);
  bool ok = configured && !extra_reconcile && hidTxPush(&extra_tx, &packet);
  if (!ok) {
    if (configured) {
      transport_stats.extra_coalesced++;
    }
    extra_reconcile |= 1U << index;
  }
  usbHidPumpLocked(&USBD_Device);
  usbHidUnlock(irq);
  return ok;
}

void usbHidOnBusResetBegin(void)
{
  uint32_t irq = usbHidLock();
  usbHidRetireSessionLocked();
  usbHidUnlock(irq);
}

void usbHidOnSuspend(void)
{
  uint32_t irq = usbHidLock();
  ++wake_suspend_epoch;
  suspend_ms = millis();
  wake_state = USB_HID_WAKE_IDLE;
  wake_skip_stale_sof = false;
  // V260909R1: suspend는 reset이 아니다. 기존 backlog와 복귀 중의 짧은 press/release를 순서대로 보존한다.
  usbHidUnlock(irq);
}

void usbHidOnResume(void)
{
  // The PCD -> USBD bridge owns physical bus state and logical Resume delivery.
  // HID owns only the device-driven wake attempt and its SOF freshness bookkeeping.
  wake_state = USB_HID_WAKE_IDLE;
  wake_skip_stale_sof = false;
}

bool usbHidConsumeWakeSof(void)
{
  if (wake_state == USB_HID_WAKE_IDLE) return false;
  if (wake_skip_stale_sof) {
    // GINTSTS.SOF was already pending before RWUSIG; wait for the next callback.
    wake_skip_stale_sof = false;
    return false;
  }
  wake_state = USB_HID_WAKE_IDLE;
  return true;
}

void usbHidGetTransportStats(usb_hid_transport_stats_t *stats)
{
  if (stats == NULL) return;
  uint32_t irq = usbHidLock();
  *stats = transport_stats;
  usbHidUnlock(irq);
}

// V260909R1: 사용자 Apply/reset은 큐에 넣기만 한 응답을 전송 완료로 오인하지 않는다.
bool usbHidViaResponsesPending(void)
{
  uint32_t irq = usbHidLock();
  // Retained failed-teardown payloads belong to a retired session, not a response drain.
  bool pending = usbHidAdmissionOpen() && (via_tx.busy || via_tx.count != 0U);
  usbHidUnlock(irq);
  return pending;
}
