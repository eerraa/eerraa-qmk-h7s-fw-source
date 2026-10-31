#ifndef HW_CAPS_RTC_H_
#define HW_CAPS_RTC_H_


// ---------------------------------------------------------------------------
// [Caps Dependencies] V251114R3
//   - 사용처: src/hw/driver/rtc.c, src/hw/driver/reset.c 부트모드 플래그 저장
//   - 비고  : HW_RTC_BOOT_MODE/RESET_BITS는 bootmode/cli 명령과 연계
// ---------------------------------------------------------------------------
#ifndef _USE_HW_RTC
#define _USE_HW_RTC
#endif

#ifndef HW_RTC_BOOT_MODE
#define HW_RTC_BOOT_MODE            RTC_BKP_DR3
#endif

#ifndef HW_RTC_RESET_BITS
#define HW_RTC_RESET_BITS           RTC_BKP_DR4
#endif


#endif
