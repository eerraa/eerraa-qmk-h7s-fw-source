#ifndef HW_CAPS_UART_H_
#define HW_CAPS_UART_H_


// ---------------------------------------------------------------------------
// [Caps Dependencies] V251114R3
//   - 사용처: src/hw/driver/uart.c, src/ap/ap.c CLI 채널 스위칭, src/bsp/device/syscalls.c
//   - 비고  : HW_UART_CH_USB/CLI는 로거(log.c)와 USB CDC 경로 결정에 영향
// ---------------------------------------------------------------------------
#ifndef _USE_HW_UART
#define _USE_HW_UART
#endif

#ifndef HW_UART_MAX_CH
#define HW_UART_MAX_CH              2
#endif

#ifndef HW_UART_CH_SWD
#define HW_UART_CH_SWD              _DEF_UART1
#endif

#ifndef HW_UART_CH_USB
#define HW_UART_CH_USB              _DEF_UART2
#endif

#ifndef HW_UART_CH_CLI
#define HW_UART_CH_CLI              HW_UART_CH_SWD
#endif


#endif
