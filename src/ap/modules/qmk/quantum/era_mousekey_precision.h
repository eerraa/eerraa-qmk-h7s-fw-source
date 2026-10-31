// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
static inline uint8_t era_mousekey_unit(uint8_t start, uint8_t target, uint16_t ramp_ms, uint32_t elapsed, uint8_t accel, uint8_t maximum) {
    uint32_t top = ramp_ms ? target : start;
    int32_t value;
    if (accel & 1) value = top / 4;
    else if (accel & 2) value = top / 2;
    else if (accel & 4) value = top;
    else if (!ramp_ms) value = start;
    else if (elapsed >= ramp_ms) value = target;
    else value = start + ((int32_t)target - start) * (int32_t)elapsed / ramp_ms;
    return value < 1 ? 1 : value > maximum ? maximum : value;
}
