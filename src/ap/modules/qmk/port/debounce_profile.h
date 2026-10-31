#pragma once


#include <stdbool.h>
#include <stdint.h>
#include "qmk/quantum/debounce_runtime.h"


typedef struct
{
  debounce_runtime_type_t type;
  uint8_t                 pre_ms;
  uint8_t                 post_ms;
} debounce_profile_values_t;  // V251115R1: VIA 런타임 디바운스 값 캐시

// V260901R1: 소비자 없고 런타임 재시도 결과와 어긋날 수 있는 VIA STATUS API 제거
void                        debounce_profile_init(void);
void                        debounce_profile_apply_current(void);
const debounce_profile_values_t *
                             debounce_profile_current(void);
bool                        debounce_profile_set_mode(uint8_t mode);
bool                        debounce_profile_set_single_delay(uint8_t delay_ms);
bool                        debounce_profile_set_press_delay(uint8_t delay_ms);
bool                        debounce_profile_set_release_delay(uint8_t delay_ms);
void                        debounce_profile_save(bool force);
void                        debounce_profile_storage_apply_defaults(void);
void                        debounce_profile_storage_stage_defaults(void);
void                        debounce_profile_storage_publish(void);
bool                        debounce_profile_handle_via_command(uint8_t *data, uint8_t length);
