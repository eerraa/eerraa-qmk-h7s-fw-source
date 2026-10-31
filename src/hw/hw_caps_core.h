#ifndef HW_CAPS_CORE_H_
#define HW_CAPS_CORE_H_


// ---------------------------------------------------------------------------
// [Caps Dependencies] V251114R3
//   - 사용처: src/hw/hw.c 초기화, src/hw/driver/flash.c, src/hw/driver/micros.c
//   - 비고  : _USE_HW_VCOM 토글 시 USB 로그 경로(hcaps_usb.h)와 연동
// ---------------------------------------------------------------------------
#ifndef _USE_HW_CACHE
#define _USE_HW_CACHE
#endif

#ifndef _USE_HW_FLASH
#define _USE_HW_FLASH
#endif

#ifndef _USE_HW_MICROS
#define _USE_HW_MICROS
#endif

// #define _USE_HW_QSPI
// #define _USE_HW_VCOM


#endif
