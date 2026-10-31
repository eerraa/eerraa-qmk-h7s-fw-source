#include "main.h"




int main(void)
{
  bspInit();

  if (hwInit() != true)
  {
    while (1)
    {
      ledToggle(_DEF_LED1);                                      // V251124R6: 치명적 초기화 실패 시 LED 점멸 후 정지
      delay(250);
    }
  }
  apInit();
  apMain();

  return 0;
}

