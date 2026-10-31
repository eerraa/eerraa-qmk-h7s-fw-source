#ifndef HW_CAPS_EEPROM_H_
#define HW_CAPS_EEPROM_H_


// ---------------------------------------------------------------------------
// [Caps Dependencies] V251114R3
//   - 사용처: src/hw/driver/eeprom/*.c
//   - 비고  : 부팅 reset guard(src/hw/driver/eeprom_reset_guard.c)가 항상 이 저장소를 쓴다
// ---------------------------------------------------------------------------
#ifndef _USE_HW_EEPROM
#define _USE_HW_EEPROM
#endif

#ifndef EEPROM_CHIP_ZD24C128
#define EEPROM_CHIP_ZD24C128
#endif


#endif
