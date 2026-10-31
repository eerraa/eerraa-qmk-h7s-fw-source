#ifndef HW_CAPS_RESET_H_
#define HW_CAPS_RESET_H_


// ---------------------------------------------------------------------------
// [Caps Dependencies] V251114R3
//   - 사용처: src/hw/driver/reset.c, CLI reset 명령(_USE_CLI_HW_RESET)
//   - 비고  : HW_RESET_BOOT가 0이면 부트핀 기반 점프가 비활성화
// ---------------------------------------------------------------------------
#ifndef _USE_HW_RESET
#define _USE_HW_RESET
#endif

#ifndef HW_RESET_BOOT
#define HW_RESET_BOOT               1
#endif


#endif
