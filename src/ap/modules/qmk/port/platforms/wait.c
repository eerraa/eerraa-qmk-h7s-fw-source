#include "wait.h"



void wait_ms(uint32_t ms)
{
  if (ms == 0U)
  {
    return;  // V260911R2: HAL_Delay(0)의 최소 1 tick 대기를 QMK의 지연 없는 tap으로 유입시키지 않는다.
  }

  delay(ms);
}