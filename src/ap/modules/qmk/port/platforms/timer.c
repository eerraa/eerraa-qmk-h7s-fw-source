#include "timer.h"



// V260901R1: 호출자가 없는 timer_clear 스텁을 제거해 미구현 호출을 링크 오류로 드러낸다.

void timer_init(void)
{

}

uint16_t timer_read(void)
{
  return millis();
}

uint32_t timer_read32(void)
{
  return millis();
}

uint16_t timer_elapsed(uint16_t last)
{
    uint32_t t;

    t = millis();

    return TIMER_DIFF_16((t & 0xFFFF), last);  
}

uint32_t timer_elapsed32(uint32_t last)
{
  return millis()-last;
}
