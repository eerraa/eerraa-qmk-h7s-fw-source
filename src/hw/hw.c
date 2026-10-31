#include "hw.h"
#include "qmk/port/platforms/eeprom.h"  // V250923R1 Preload QMK EEPROM services



extern uint32_t _fw_flash_begin;

volatile const firm_ver_t firm_ver __attribute__((section(".version"))) = 
{
  .magic_number = VERSION_MAGIC_NUMBER,
  .version_str  = _DEF_FIRMWARE_VERSION,
  .name_str     = _DEF_BOARD_NAME,
  .firm_addr    = (uint32_t)&_fw_flash_begin
};

// reset guard 실패 알림용 LED 점멸 시퀀스
static void hwBlinkResetGuardFailure(void)
{
  const uint32_t blink_count  = 3;
  const uint32_t blink_on_ms  = 120U;
  const uint32_t blink_off_ms = 120U;

  for (uint32_t i = 0; i < blink_count; i++)
  {
    ledOn(_DEF_LED1);
    delay(blink_on_ms);
    ledOff(_DEF_LED1);
    delay(blink_off_ms);
  }
}

// reset guard 재시도 및 실패 기록 헬퍼
static bool hwRunResetGuardWithRetry(void)
{
  const uint32_t retry_limit = 3;

  for (uint32_t attempt = 0; attempt < retry_limit; attempt++)
  {
    if (eepromResetGuardCheck() == true)
    {
      return true;
    }

    logPrintf("[!] EEPROM reset guard : retry %lu/%lu\n",
              (unsigned long)(attempt + 1U),
              (unsigned long)retry_limit);                          // V251124R6: 실패 시 재시도 및 원인 표시
    hwBlinkResetGuardFailure();
    eeprom_init();                                                 // V251124R6: 재시도 전에 QMK EEPROM 미러 재동기화
  }

  logPrintf("[!] EEPROM reset guard : failed after %lu attempts\n",
            (unsigned long)retry_limit);                            // V251124R6: 반복 실패 시 치명적 오류로 처리
  return false;
}



bool hwInit(void)
{
  bool hw_ok = true;                                                       // V251124R4: 하위 모듈 초기화 상태 집계
  #ifdef _USE_HW_CACHE
  SCB_EnableICache();
  SCB_EnableDCache();
  #endif  

  cliInit();
  logInit();  
  ledInit();
  microsInit();

  uartInit();
  for (int i=0; i<HW_UART_MAX_CH; i++)
  {
    uartOpen(i, 115200);
  }

  if (HW_LOG_ENABLE_DEFAULT)                                     // V251113R1: 개발 빌드만 기본적으로 UART 로그를 연다
  {
    logOpen(HW_LOG_CH, 115200);
  }
  else
  {
    logDisable();                                                // V251113R1: 릴리스 빌드는 UART 출력 비활성 상태로 시작
  }
  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..KBD  \t\t: %s\r\n", KBD_NAME);  
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWARE_VERSION);  
  logPrintf("Booting..Clock\t\t: %d Mhz\r\n", (int)HAL_RCC_GetSysClockFreq()/1000000);
  logPrintf("Booting..Date \t\t: %s\r\n", __DATE__); 
  logPrintf("Booting..Time \t\t: %s\r\n", __TIME__); 
  logPrintf("Booting..Addr \t\t: 0x%X\r\n", (uint32_t)&_fw_flash_begin); 

  logPrintf("\n");
  logPrintf("[  ] ICache  %s\n", (SCB->CCR & SCB_CCR_IC_Msk) ? "ON":"OFF");
  logPrintf("[  ] DCache  %s\n", (SCB->CCR & SCB_CCR_DC_Msk) ? "ON":"OFF");
  
  rtcInit();
  resetInit();    
  i2cInit();
  if (!eepromInit()) return false;  // V260909R1: 저장장치 오류에서 기본값 부팅을 진행하지 않는다.
  eeprom_init();                                              // V250923R1 Sync QMK EEPROM image before USB init
  if (!eeprom_is_ready()) return false;  // V260909R1: 부분 읽기를 정상 mirror로 사용하지 않는다.
#ifdef BOOTMODE_ENABLE
  bootmode_init();                                            // V251112R6: BootMode 기본값 초기화
#endif
  bool reset_guard_ok = hwRunResetGuardWithRetry();            // 실패 시 LED 표시 후 재시도
#ifdef BOOTMODE_ENABLE
  if (reset_guard_ok && usbBootModeLoad() != true)           // V250923R1 Apply stored USB boot mode preference
  {
    logPrintf("[!] usbBootModeLoad Fail\n");
  }
#endif
  if (reset_guard_ok != true)
  {
    hw_ok = false;                                             // reset guard 실패는 치명적 상태
  }
  #ifdef _USE_HW_QSPI
  qspiInit();
  #endif
  flashInit();
  if (keysInit() != true)
  {
    logPrintf("[!] keysInit Fail\n");                                     // V251124R4: 키 스캔 초기화 실패 표시
    hw_ok = false;
  }
  #ifdef _USE_HW_WS2812
  if (ws2812Init() != true)
  {
    logPrintf("[!] ws2812Init Fail\n");                                    // V251124R6: WS2812 초기화 실패 시 부팅 상태에 반영
    hw_ok = false;
  }
  #endif
  
  cdcInit();
  usbInit();
#if HW_USB_CMP
  usbBegin(USB_CMP_MODE);
#else
  usbBegin(USB_HID_MODE);
#endif

#if defined(_USE_HW_I2C)
  {
    i2c_ready_wait_stats_t ready_stats;
    i2cGetReadyWaitStats(_DEF_I2C1, &ready_stats);
    logPrintf("[I2C] ready wait summary max=%lums count=%lu last=0x%02X\n",
              (unsigned long)ready_stats.wait_max_ms,
              (unsigned long)ready_stats.wait_count,
              ready_stats.wait_last_addr);                       // V251112R9: 부팅 완료 후 한 줄 요약
  }
#endif

  return hw_ok;
}
