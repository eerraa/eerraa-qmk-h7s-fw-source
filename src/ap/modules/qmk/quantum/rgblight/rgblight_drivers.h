// Copyright 2023 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "color.h"

typedef struct {
    void (*setleds)(rgb_led_t *ledarray, uint16_t number_of_leds);
    /* Optional asynchronous transport fence. All three are provided together. */
    uint32_t (*get_generation)(void);
    bool (*is_complete)(uint32_t generation);
    uint32_t (*time_us)(void);
} rgblight_driver_t;

extern const rgblight_driver_t rgblight_driver;
