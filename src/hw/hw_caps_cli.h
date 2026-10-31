#ifndef HW_CAPS_CLI_H_
#define HW_CAPS_CLI_H_


// ---------------------------------------------------------------------------
// [Caps Dependencies] V251114R3
//   - 사용처: src/hw/driver/cli.c, src/hw/driver/cli_gui.c, 각 _USE_CLI_HW_* 모듈
//   - 비고  : CLI 버퍼 크기는 USB CLI 전환(ap.c) 안정성과 직결되므로 조정 시 주의
// ---------------------------------------------------------------------------
#ifndef _USE_HW_CLI
#define _USE_HW_CLI
#endif

#ifndef HW_CLI_CMD_LIST_MAX
#define HW_CLI_CMD_LIST_MAX         32
#endif

#ifndef HW_CLI_CMD_NAME_MAX
#define HW_CLI_CMD_NAME_MAX         16
#endif

#ifndef HW_CLI_LINE_HIS_MAX
#define HW_CLI_LINE_HIS_MAX         8
#endif

#ifndef HW_CLI_LINE_BUF_MAX
#define HW_CLI_LINE_BUF_MAX         64
#endif

#ifndef _USE_HW_CLI_GUI
#define _USE_HW_CLI_GUI
#endif

#ifndef HW_CLI_GUI_WIDTH
#define HW_CLI_GUI_WIDTH            80
#endif

#ifndef HW_CLI_GUI_HEIGHT
#define HW_CLI_GUI_HEIGHT           24
#endif

#ifndef _USE_CLI_HW_EEPROM
#define _USE_CLI_HW_EEPROM          1
#endif

#ifndef _USE_CLI_HW_I2C
#define _USE_CLI_HW_I2C             1
#endif

#ifndef _USE_CLI_HW_QSPI
#define _USE_CLI_HW_QSPI            1
#endif

#ifndef _USE_CLI_HW_FLASH
#define _USE_CLI_HW_FLASH           1
#endif

#ifndef _USE_CLI_HW_RTC
#define _USE_CLI_HW_RTC             1
#endif

#ifndef _USE_CLI_HW_RESET
#define _USE_CLI_HW_RESET           1
#endif

#ifndef _USE_CLI_HW_KEYS
#define _USE_CLI_HW_KEYS            1
#endif

#ifndef _USE_CLI_HW_WS2812
#define _USE_CLI_HW_WS2812          1
#endif


#endif
