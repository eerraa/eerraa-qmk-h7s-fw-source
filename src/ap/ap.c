#include "ap.h"
#include "qmk/qmk.h"
#include "usb.h"


void cliUpdate(void);




void apInit(void)
{  
  cliOpen(HW_UART_CH_CLI, 115200);  
  if (!qmkInit()) {
    logPrintf("[!] QMK storage initialization failed\n");
    while (1) { ledToggle(_DEF_LED1); delay(250); }
  }

  logBoot(false);
}

void apMain(void)
{
  uint32_t pre_time   = millis();
  bool     is_led_on  = true;

  ledOn(_DEF_LED1);
  while(1)
  {
    if (is_led_on && millis()-pre_time >= 500U)                  // V251124R2: 부팅 후 0.5s 경과 시 LED 1회 소등
    {
      is_led_on = false;
      ledOff(_DEF_LED1);
    }

    cliUpdate();
    usbProcess();                                               // V260823R2: 사용자 요청 BootMode/reset 큐만 처리
    qmkUpdate();
  }
}

void cliUpdate(void)
{
  static uint8_t cli_ch = HW_UART_CH_CLI; 

  if (usbIsOpen() && usbGetType() == USB_CON_CLI)
  {
    cli_ch = HW_UART_CH_USB;
  }
  else
  {
    cli_ch = HW_UART_CH_CLI;
  }
  if (cli_ch != cliGetPort())
  {
    if (cli_ch == HW_UART_CH_USB)
      logPrintf("\nCLI To USB\n");
    else
      logPrintf("\nCLI To UART\n");
    cliOpen(cli_ch, 0);
  }

  cliMain();
}

void cliLoopIdle(void)
{
  qmkUpdate();
}
