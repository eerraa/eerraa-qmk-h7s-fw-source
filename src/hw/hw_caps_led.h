#ifndef HW_CAPS_LED_H_
#define HW_CAPS_LED_H_


// ---------------------------------------------------------------------------
// [Caps Dependencies] V251114R3
//   - 사용처: src/hw/driver/led.c, src/hw/hw.c 보드 LED init, src/ap/ap.c 부팅 표시
//   - 비고  : _DEF_LED1~4 채널 정의는 src/common/def.h 참고
// ---------------------------------------------------------------------------
#ifndef _USE_HW_LED
#define _USE_HW_LED
#endif

#ifndef HW_LED_MAX_CH
#define HW_LED_MAX_CH               1
#endif

// WS2812 확장은 보드 config에서 `_USE_HW_WS2812`를 선언해 사용
// #define _USE_HW_WS2812
// #define HW_WS2812_MAX_CH           45


#endif
