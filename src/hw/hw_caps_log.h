#ifndef HW_CAPS_LOG_H_
#define HW_CAPS_LOG_H_


// ---------------------------------------------------------------------------
// [Caps Dependencies] V251114R3
//   - 사용처: src/hw/driver/log.c, src/ap/ap.c CLI 전환 로그, usb_hid.c 로그 토글
//   - 비고  : HW_LOG_CH는 HW_UART_CH_*와 연결되므로 uart 캡 변경 시 동기화 필요
// ---------------------------------------------------------------------------
#ifndef _USE_HW_LOG
#define _USE_HW_LOG
#endif

#ifndef HW_LOG_CH
#define HW_LOG_CH                   HW_UART_CH_SWD
#endif

#ifndef HW_LOG_BOOT_BUF_MAX
#define HW_LOG_BOOT_BUF_MAX         2048
#endif

#ifndef HW_LOG_LIST_BUF_MAX
#define HW_LOG_LIST_BUF_MAX         4096
#endif


#endif
