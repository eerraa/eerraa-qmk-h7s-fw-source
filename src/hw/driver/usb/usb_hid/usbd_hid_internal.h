#pragma once

#include "hw_def.h"

#define HID_KEYBOARD_REPORT_SIZE (HW_KEYS_PRESS_MAX + 2U)  // V260823R2: 개발용 계측 상수와 분리
#define HID_BOOT_KEYBOARD_REPORT_SIZE 8U  // HID 1.11 부록 B.1: 수식키, 예약, 키 6개
