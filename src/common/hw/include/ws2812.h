#ifndef WS2812_H_
#define WS2812_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_WS2812

#define WS2812_MAX_CH  HW_WS2812_MAX_CH


#define WS2812_COLOR(r, g, b)   (((r)<<16) | ((g)<<8) | ((b)<<0))

#define WS2812_COLOR_RED        WS2812_COLOR(255,   0,   0)
#define WS2812_COLOR_GREEN      WS2812_COLOR(  0, 255,   0)
#define WS2812_COLOR_BLUE       WS2812_COLOR(  0,   0, 255)
#define WS2812_COLOR_OFF        WS2812_COLOR(  0,   0,   0)


bool ws2812Init(void);
void ws2812SetColor(uint32_t ch, uint32_t color);
/* Main-loop API: refresh accepts the latest complete work frame; DMA is immutable.
 * A generation is complete only after its frame (or a newer one) and RESET.
 * Outstanding generations must span less than 2^31 submissions. */
bool ws2812Refresh(void);
uint32_t ws2812GetRequestedGeneration(void);
bool ws2812IsFrameComplete(uint32_t generation);
uint32_t ws2812TimeUs(void);
void ws2812Task(void);                                          // V260910R6: 완료 DMA 정리 및 병합된 최신 프레임 전송


#endif

#ifdef __cplusplus
}
#endif

#endif 
