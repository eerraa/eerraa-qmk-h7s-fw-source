#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"
#include QMK_KEYMAP_CONFIG_H


// ---------------------------------------------------------------------------
// 펌웨어/보드 식별 정보
// ---------------------------------------------------------------------------
#define _DEF_FIRMWARE_VERSION       "V261004R1"
#define _DEF_BOARD_NAME             "ERA-QMK-H7S-FW"  // V251125R3: 사용자 표시용 보드명 ERA로 변경


// ---------------------------------------------------------------------------
// 로그 및 디버그 기본값
// ---------------------------------------------------------------------------
#ifndef HW_LOG_ENABLE_DEFAULT
#define HW_LOG_ENABLE_DEFAULT       0             // V251113R1: 릴리스 빌드는 UART 로그 비활성 상태로 시작
#endif

#ifndef LOG_LEVEL_VERBOSE
#define LOG_LEVEL_VERBOSE           0             // V251112R9: 기본 빌드 로그 레벨을 표준으로 유지
#endif

#ifndef DEBUG_LOG_EEPROM
#define DEBUG_LOG_EEPROM            0             // V251112R9: EEPROM 상세 로그 토글 기본 비활성화
#endif


// ---------------------------------------------------------------------------
// EEPROM reset guard (EERRAA와 같은 이름. 끄는 옵션 없이 항상 동작한다)
// ---------------------------------------------------------------------------
#define ERA_EEPROM_RESET_GUARD_MAGIC  0x56434C52U  // "VCLR": guard가 유효하다는 표식
#define ERA_EEPROM_RESET_GUARD_CLEAR  0x00000000U  // 표식을 지운 값. 다음 부팅이 전체 초기화한다

// 저장 형식이 바뀐 릴리스만 올린다. 펌웨어 버전과 무관하며, 올리면 첫 부팅에서
// 전체 EEPROM을 초기화한다 (docs/contract_eeprom.md §2). "H7S" + 리비전.
#define ERA_EEPROM_RESET_KEY          0x48375303U


// ---------------------------------------------------------------------------
// 계측 기능 (보드/빌드 오버라이드 가능)
// ---------------------------------------------------------------------------
#ifndef _DEF_ENABLE_MATRIX_TIMING_PROBE
#define _DEF_ENABLE_MATRIX_TIMING_PROBE   0       // V251010R4: 기본값은 비활성화, 필요 시 보드/빌드에서 재정의
#endif


// ---------------------------------------------------------------------------
// 하드웨어 사용 선언 (기능별 분리 헤더)
// ---------------------------------------------------------------------------
#include "hw_caps_core.h"
#include "hw_caps_led.h"
#include "hw_caps_uart.h"
#include "hw_caps_i2c.h"
#include "hw_caps_eeprom.h"
#include "hw_caps_rtc.h"
#include "hw_caps_reset.h"
#include "hw_caps_keys.h"
#include "hw_caps_usb.h"
#include "hw_caps_cli.h"
#include "hw_caps_log.h"


#endif
