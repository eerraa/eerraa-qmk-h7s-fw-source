#pragma once
// HID report transport API for the firmware outside the USB driver. It does not
// depend on a USB stack; hid_port.h binds the transport to the one in use.
#include <stdbool.h>
#include <stdint.h>

// One VIA raw HID request or reply, which is also the VIA endpoint packet size.
#define HID_VIA_EP_SIZE 32U

enum
{
  USB_HID_LED_NUM_LOCK    = (1 << 0),
  USB_HID_LED_CAPS_LOCK   = (1 << 1),
  USB_HID_LED_SCROLL_LOCK = (1 << 2),
  USB_HID_LED_COMPOSE     = (1 << 3),
  USB_HID_LED_KANA        = (1 << 4)
};

typedef struct {
  uint32_t generation;
  bool valid;
  bool suspended;
} usb_hid_session_t;
// One coherent snapshot of the admitted session; Suspend preserves its generation.
usb_hid_session_t usbHidGetSession(void);
const char *usbHidGetPollingLabel(void);

// 버스 세대가 다른 명령/응답을 섞지 않는다. 숫자는 boot 이후 누적 관측값이다.
typedef struct {
  uint32_t arm_failures;
  uint32_t keyboard_coalesced;
  uint32_t extra_coalesced;
  uint32_t invalid_rx;
  uint32_t invalid_control;
  uint32_t session_discards;
} usb_hid_transport_stats_t;
// Dequeue reserves one response credit until enqueue or retirement of that generation.
bool usbHidReadViaRequest(uint8_t *data, uint32_t *generation);
bool usbHidEnqueueViaResponse(const uint8_t *data, uint8_t length, uint32_t generation);
bool usbHidViaResponsesPending(void);
bool usbHidHostSleeping(void);
bool usbHidRequestRemoteWakeFromInput(void);
void usbHidGetTransportStats(usb_hid_transport_stats_t *stats);
bool usbHidSendReport(uint8_t *p_data, uint16_t length);
// Opt-in logical updates from a single physical scan; ordinary reports stay immutable.
bool usbHidSubmitKeyUpdate(uint8_t *data, uint16_t length, uint32_t scan_token, uint8_t usage, bool pressed);
// token 0 is a barrier for any unfinished physical scan.
void usbHidEndKeyScan(uint32_t scan_token);
// 마지막 keyboard 스냅샷 뒤에 최소 간격을 설정한다. 호출자는 기다리지 않는다.
void usbHidDelayKeyboardReport(uint16_t delay_ms);
void usbHidDelayKeyboardReportForGeneration(uint16_t delay_ms, uint32_t generation);
bool usbHidSendReportEXK(uint8_t *p_data, uint16_t length);
// Reject retired synthetic contributions before they can update queue or reconciliation state.
bool usbHidSendReportForGeneration(uint8_t *data, uint16_t length, uint32_t generation);
bool usbHidSendReportEXKForGeneration(uint8_t *data, uint16_t length, uint32_t generation);
// Each board applies the host's keyboard LED Output report.
void usbHidSetStatusLed(uint8_t led_bits);
