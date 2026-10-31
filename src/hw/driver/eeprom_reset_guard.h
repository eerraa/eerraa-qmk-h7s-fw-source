#pragma once

#include <stdbool.h>


// 부팅 때 USER 슬롯의 guard(VCLR 표식 + ERA_EEPROM_RESET_KEY)를 확인하고, 다르면 전체 초기화한다.
bool eepromResetGuardCheck(void);
// guard를 지워 다음 부팅이 전체 초기화하게 한다 (VIA EEPROM CLEAN).
bool eepromResetGuardInvalidate(void);
// Stage only; callers retain reset intent until the shared writer verifies both guard words.
void eepromResetGuardStageInvalidation(void);
