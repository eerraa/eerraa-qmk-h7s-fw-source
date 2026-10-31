#ifndef HW_CAPS_I2C_H_
#define HW_CAPS_I2C_H_


// ---------------------------------------------------------------------------
// [Caps Dependencies] V251114R3
//   - 사용처: src/hw/driver/i2c.c, src/hw/hw.c 센서/EEPROM 브릿지 초기화
//   - 비고  : CLI I2C 명령(_USE_CLI_HW_I2C)과 연계되어 진단 채널 노출
// ---------------------------------------------------------------------------
#ifndef _USE_HW_I2C
#define _USE_HW_I2C
#endif

#ifndef HW_I2C_MAX_CH
#define HW_I2C_MAX_CH               1
#endif


#endif
