#ifndef HW_CAPS_KEYS_H_
#define HW_CAPS_KEYS_H_


// ---------------------------------------------------------------------------
// [Caps Dependencies] V251114R3
//   - 사용처: src/hw/driver/keys.c, src/ap/modules/qmk/port/protocol/report.h
//   - 비고  : USB HID 리포트 크기(usbd_hid.c)와 직결되므로 값 변경 시 USB 디스크립터 검토 필요
// ---------------------------------------------------------------------------
#ifndef _USE_HW_KEYS
#define _USE_HW_KEYS
#endif

#ifndef HW_KEYS_PRESS_MAX
#define HW_KEYS_PRESS_MAX           20
#endif


#endif
