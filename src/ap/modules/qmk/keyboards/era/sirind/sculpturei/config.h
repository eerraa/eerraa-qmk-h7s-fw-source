#pragma once

// ---------------------------------------------------------------------------
// 보드/USB 식별
// ---------------------------------------------------------------------------
#define KBD_NAME                    "SCULPTUREI"  // V260701R1: SCULPTUREI 보드명 추가
#define KBD_MANUFACTURER            "SR Industry"
#define USB_VID                     0x4501
#define USB_PID                     0x000E


// ---------------------------------------------------------------------------
// hw_def.h / hw_caps_* 오버라이드 및 공용 토글
// ---------------------------------------------------------------------------
#define _DEF_ENABLE_MATRIX_TIMING_PROBE   0     // MATRIX 계측을 개발 빌드에서 강제 활성화하려면 정의
#define _USE_HW_WS2812
#define     HW_WS2812_MAX_CH        19    // V260701R1: indicator 3ch + underglow 16ch 총 19채널
#define     HW_WS2812_CAPS          0     // V260701R1: IND1은 물리 RGB1~2를 CAPS LOCK 기본 인디케이터로 사용
#define     HW_WS2812_CAPS_CNT      2
#define     HW_WS2812_SCROLL        2     // V260701R1: IND2는 물리 RGB3을 SCROLL LOCK 기본 인디케이터로 사용
#define     HW_WS2812_RGB           3
#define     HW_WS2812_RGB_CNT       16    // V260701R1: rgblight 드라이버는 물리 RGB4부터 underglow 16개만 제어


// ---------------------------------------------------------------------------
// EEPROM 및 매트릭스 구성
// ---------------------------------------------------------------------------
#define EEPROM_CHIP_ZD24C128
#define EECONFIG_USER_DATA_SIZE     512
#define TOTAL_EEPROM_BYTE_COUNT     4096
#define DYNAMIC_KEYMAP_LAYER_COUNT  8

#define MATRIX_ROWS                 6     // V260701R1: SCULPTUREI ASC/VIA matrix 행 수 반영
#define MATRIX_COLS                 16    // V260701R1: SCULPTUREI ASC/VIA matrix 열 수 반영
#define DEBOUNCE                    5
#define DEBOUNCE_TYPE               sym_defer_pk


// ---------------------------------------------------------------------------
// 입력/유틸리티 기능 토글
// ---------------------------------------------------------------------------
#define GRAVE_ESC_ENABLE
#define KILL_SWITCH_ENABLE
#define KKUK_ENABLE
#define BOOTMODE_ENABLE             1     // V260823R2: 폴링 모드는 사용자 명시 적용만 허용
#define G_TERM_ENABLE
#ifdef G_TERM_ENABLE
#  define TAPPING_TERM_PER_KEY
#  define PERMISSIVE_HOLD_PER_KEY
#  define HOLD_ON_OTHER_KEY_PRESS_PER_KEY
#  define RETRO_TAPPING_PER_KEY
#endif
#define TAPDANCE_ENABLE
#ifdef TAPDANCE_ENABLE
#  define TAP_DANCE_ENABLE
#endif
#define INDICATOR_ENABLE            // V260701R1: SCULPTUREI 2슬롯 물리 인디케이터 경로 사용


// ---------------------------------------------------------------------------
// RGB 라이트 모듈
// ---------------------------------------------------------------------------
#define RGBLIGHT_ENABLE
#define EEPROM_ENABLE
#define RGBLIGHT_SLEEP
#define RGBLIGHT_DEFAULT_ON         true
#define RGBLIGHT_DEFAULT_HUE        0
#define RGBLIGHT_DEFAULT_SAT        0
#define RGBLIGHT_DEFAULT_VAL        64    // V260701R2: 최대 밝기 128 기준 underglow 기본 밝기를 50%로 설정
#define RGBLIGHT_INDICATOR_SLOT_COUNT 2   // V260701R1: IND1/IND2를 VIA에서 독립 슬롯으로 운용
#define RGBLIGHT_LED_COUNT          HW_WS2812_RGB_CNT
#define RGBLIGHT_LIMIT_VAL          128   // V260701R2: SCULPTUREI RGB 전체 최대 밝기를 128로 제한
#define RGBLIGHT_SAT_STEP           8
#define RGBLIGHT_VAL_STEP           8
#define RGBLIGHT_EFFECT_RGB_TEST
#define RGBLIGHT_EFFECT_BREATHING
#define RGBLIGHT_EFFECT_SNAKE
#define RGBLIGHT_EFFECT_STATIC_GRADIENT
#define RGBLIGHT_EFFECT_ALTERNATING
#define RGBLIGHT_EFFECT_CHRISTMAS
#define RGBLIGHT_EFFECT_KNIGHT
#define RGBLIGHT_EFFECT_RAINBOW_MOOD
#define RGBLIGHT_EFFECT_RAINBOW_SWIRL
#define RGBLIGHT_EFFECT_TWINKLE
#define RGBLIGHT_EFFECT_PULSE_ON_PRESS       // V251018R5: Pulse On Press 커스텀 이펙트 활성화
#define RGBLIGHT_EFFECT_PULSE_OFF_PRESS      // V251018R5: Pulse Off Press 커스텀 이펙트 활성화
#define RGBLIGHT_EFFECT_PULSE_ON_PRESS_HOLD  // V251018R5: Pulse On Press (Hold) 파생 이펙트 활성화
#define RGBLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD // V251018R5: Pulse Off Press (Hold) 파생 이펙트 활성화
#define VELOCIKEY_ENABLE
