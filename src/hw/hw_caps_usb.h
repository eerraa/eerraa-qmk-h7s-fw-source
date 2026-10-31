#ifndef HW_CAPS_USB_H_
#define HW_CAPS_USB_H_


// ---------------------------------------------------------------------------
// [Caps Dependencies] V251114R3
//   - 사용처: src/hw/driver/usb/*.c, src/hw/driver/usb/usb_hid/*.c, cli USB 경로
//   - 비고  : _USE_HW_VCOM 토글 시 HW_USB_* 매크로를 통해 CDC/LOG 구성이 자동 변경
// ---------------------------------------------------------------------------
#ifndef _USE_HW_USB
#define _USE_HW_USB
#endif

#ifndef _USE_HW_CDC
#define _USE_HW_CDC
#endif

#ifdef _USE_HW_VCOM
#ifndef HW_USB_LOG
#define HW_USB_LOG                  0
#endif
#ifndef HW_USB_CMP
#define HW_USB_CMP                  1
#endif
#ifndef HW_USB_CDC
#define HW_USB_CDC                  1
#endif
#ifndef HW_USB_MSC
#define HW_USB_MSC                  0
#endif
#ifndef HW_USB_HID
#define HW_USB_HID                  1
#endif
#else
#ifndef HW_USB_LOG
#define HW_USB_LOG                  0
#endif
#ifndef HW_USB_CMP
#define HW_USB_CMP                  0
#endif
#ifndef HW_USB_CDC
#define HW_USB_CDC                  0
#endif
#ifndef HW_USB_MSC
#define HW_USB_MSC                  0
#endif
#ifndef HW_USB_HID
#define HW_USB_HID                  1
#endif
#endif


#endif
