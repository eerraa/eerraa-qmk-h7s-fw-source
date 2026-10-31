
#pragma once


#include "via_hid.h"
#include "via.h"
#include "eeconfig.h"
#include "kill_switch.h"
#include "kkuk.h"
#include "tapping_term.h"
#include "tapdance.h"
#include "mousekey_config.h"                                                                          // V260823R1: VIA MOUSE 튜닝 페이지
#include "rgb_sleep.h"                                                                                // V260901R1: VIA RGB SLEEP



#define EECONFIG_USER_INDICATOR           ((void *)((uint32_t)EECONFIG_USER_DATABLOCK +  0))  // 8B  // V251129R1: 통합 인디케이터 슬롯 (기존 CAPS/SCROLL 통합)
#define EECONFIG_USER_KILL_SWITCH_LR      ((void *)((uint32_t)EECONFIG_USER_DATABLOCK +  8))  // 8B
#define EECONFIG_USER_KILL_SWITCH_UD      ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 16))  // 8B
#define EECONFIG_USER_KKUK                ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 24))  // 4B
#define EECONFIG_USER_BOOTMODE            ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 28))  // 4B
#define EECONFIG_USER_RESERVED_32         ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 32))  // 4B  // V260823R2: 레거시 모니터 슬롯은 주소 호환용으로만 예약
#define EECONFIG_USER_RESET_GUARD_MAGIC   ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 36))  // 4B  // reset guard 표식 (VCLR)
#define EECONFIG_USER_RESET_GUARD_KEY     ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 40))  // 4B  // reset guard 키 (ERA_EEPROM_RESET_KEY)
#define EECONFIG_USER_DEBOUNCE            ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 44))  // 8B  // V251115R1: VIA 디바운스 프로필 저장 슬롯
#define EECONFIG_USER_TAPPING_TERM        ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 52))  // 12B  // V251123R4: VIA TAPPING 설정 슬롯
#define EECONFIG_USER_TAPDANCE            ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 64))  // 88B  // V251124R8: VIA TAPDANCE 슬롯
#define EECONFIG_USER_MOUSEKEY            ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 152)) // 16B  // V260823R1: VIA MOUSE 튜닝 슬롯
#define EECONFIG_USER_RGB_SLEEP           ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 168)) // 4B   // V260901R1: VIA RGB SLEEP 초 단위 타임아웃 슬롯

#define EECONFIG_USER_TAPDANCE_TIMING      ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 172)) // 24B
