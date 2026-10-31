#ifndef KEYS_H_
#define KEYS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"




bool keysInit(void);
bool keysIsBusy(void);
// V251009R1: DMA 전용 스캔으로 keysUpdate() API 제거
bool keysGetPressed(uint16_t row, uint16_t col);
bool keysReadBuf(uint8_t *p_data, uint32_t length);
bool keysReadColsBuf(uint16_t *p_data, uint32_t rows_cnt);
const volatile uint16_t *keysPeekColsBuf(void);  // V250924R5: DMA 버퍼 스냅샷 포인터 제공 (재검토: volatile 포인터 반환)

#ifdef __cplusplus
}
#endif

#endif