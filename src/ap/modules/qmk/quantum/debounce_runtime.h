#pragma once


#include <stdbool.h>
#include <stdint.h>


typedef enum
{
  DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK = 0,
  DEBOUNCE_RUNTIME_TYPE_SYM_EAGER_PK = 1,
  DEBOUNCE_RUNTIME_TYPE_ASYM_EAGER_DEFER_PK = 2,
  DEBOUNCE_RUNTIME_TYPE_COUNT
} debounce_runtime_type_t;  // V251115R1: VIA 런타임 전용 디바운스 알고리즘 구분값

typedef struct
{
  debounce_runtime_type_t type;
  uint8_t                 pre_ms;
  uint8_t                 post_ms;
} debounce_runtime_config_t;  // V251115R1: 런타임 디바운스 구성 (press/pre, release/post)

// V260901R1: VIA STATUS 전용 오류 조회 API를 제거하고 준비/오류 상태를 런타임 내부로 한정
bool                         debounce_runtime_apply_config(const debounce_runtime_config_t *config);
const debounce_runtime_config_t *
                              debounce_runtime_get_config(void);
const debounce_runtime_config_t *
                              debounce_runtime_get_default_config(void);  // V251115R3: 보드 기본 디바운스 설정 조회

uint8_t debounce_runtime_press_delay(void);
uint8_t debounce_runtime_release_delay(void);
