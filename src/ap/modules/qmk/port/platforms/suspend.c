// Copyright 2022 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#include "suspend.h"
#include "matrix.h"
#include "usbd_hid.h"

// TODO: Move to more correct location
__attribute__((weak)) void matrix_power_up(void) {}
__attribute__((weak)) void matrix_power_down(void) {}

/** \brief Run user level Power down
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void suspend_power_down_user(void) {}

/** \brief Run keyboard level Power down
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void suspend_power_down_kb(void) {
    suspend_power_down_user();
}

/** \brief run user level code immediately after wakeup
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void suspend_wakeup_init_user(void) {}

/** \brief run keyboard level code immediately after wakeup
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void suspend_wakeup_init_kb(void) {
    suspend_wakeup_init_user();
}

/** \brief suspend wakeup condition
 *
 * FIXME: needs doc
 */
bool suspend_wakeup_condition(void) {
    matrix_power_up();
    matrix_scan();
    matrix_power_down();
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        if (matrix_get_row(r)) return true;
    }
    return false;
}

// A key pressed while the host sleeps wakes it but is never typed: its press and its release
// are both dropped, and it types again only on a new press (QMK's wakeup-key rule).
static matrix_row_t wakeup_matrix[MATRIX_ROWS];

void suspend_wakeup_key_event(uint8_t row, uint8_t col, bool pressed)
{
    if (pressed) {
        const bool asleep = usbHidHostSleeping();
        const bool woke   = usbHidRequestRemoteWakeFromInput();
        if (asleep || woke) {
            wakeup_matrix[row] |= (matrix_row_t)1 << col;
        }
    }
}

bool keypress_is_wakeup_key(uint8_t row, uint8_t col)
{
    return (wakeup_matrix[row] & ((matrix_row_t)1 << col)) != 0;
}

void wakeup_matrix_handle_key_event(uint8_t row, uint8_t col, bool pressed)
{
    if (!pressed) {
        wakeup_matrix[row] &= ~((matrix_row_t)1 << col);
    }
}

void suspend_power_down(void)
{
  suspend_power_down_quantum();
}

void suspend_wakeup_init(void)
{
  suspend_wakeup_init_quantum();
}
