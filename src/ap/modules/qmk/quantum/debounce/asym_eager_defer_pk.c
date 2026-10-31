/*
 * Copyright 2017 Alex Ong <the.onga@gmail.com>
 * Copyright 2020 Andrei Purdea <andrei@purdea.ro>
 * Copyright 2021 Simon Arlott
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
Asymetric per-key algorithm. After pressing a key, it immediately changes state,
with no further inputs accepted until DEBOUNCE milliseconds have occurred. After
releasing a key, that state is pushed after no changes occur for DEBOUNCE milliseconds.
*/

#include "debounce.h"
#include "debounce_runtime.h"
#include "timer.h"
#include <string.h>


#define ROW_SHIFTER ((matrix_row_t)1)

typedef struct {
    bool    pressed : 1;
    uint8_t time : 7;
} debounce_counter_t;

// V260909R1: 보드 최대 행 수에 고정 할당하여 설정 전환 시 힙 수명을 없앤다.
static debounce_counter_t debounce_counters[MATRIX_ROWS * MATRIX_COLS];
static fast_timer_t        last_time;
static bool                counters_need_update;
static bool                matrix_need_update;
static bool                cooked_changed;

#    define DEBOUNCE_ELAPSED 0

static void update_debounce_counters_and_transfer_if_expired(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, uint8_t elapsed_time);
static void transfer_matrix_values(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows);

bool debounce_asym_eager_defer_pk_init(uint8_t num_rows)
{
    if (num_rows == 0U || num_rows > MATRIX_ROWS) return false;
    memset(debounce_counters, 0, (size_t)num_rows * MATRIX_COLS * sizeof(debounce_counter_t));
    counters_need_update = false;
    matrix_need_update   = false;
    cooked_changed       = false;
    last_time            = timer_read_fast();
    return true;
}

void debounce_asym_eager_defer_pk_free(void)
{
    counters_need_update = false;
    matrix_need_update   = false;
}

bool debounce_asym_eager_defer_pk_run(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed)
{
    bool updated_last = false;
    cooked_changed    = false;

    if (counters_need_update) {
        fast_timer_t now          = timer_read_fast();
        fast_timer_t elapsed_time = TIMER_DIFF_FAST(now, last_time);

        last_time    = now;
        updated_last = true;
        if (elapsed_time > UINT8_MAX) {
            elapsed_time = UINT8_MAX;
        }

        if (elapsed_time > 0) {
            update_debounce_counters_and_transfer_if_expired(raw, cooked, num_rows, elapsed_time);
        }
    }

    if (changed || matrix_need_update) {
        if (!updated_last) {
            last_time = timer_read_fast();
        }

        transfer_matrix_values(raw, cooked, num_rows);
    }

    return cooked_changed;
}

static void update_debounce_counters_and_transfer_if_expired(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, uint8_t elapsed_time) {
    debounce_counter_t *debounce_pointer = debounce_counters;

    counters_need_update = false;
    matrix_need_update   = false;

    for (uint8_t row = 0; row < num_rows; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            matrix_row_t col_mask = (ROW_SHIFTER << col);

            if (debounce_pointer->time != DEBOUNCE_ELAPSED) {
                if (debounce_pointer->time <= elapsed_time) {
                    debounce_pointer->time = DEBOUNCE_ELAPSED;

                    if (debounce_pointer->pressed) {
                        // key-down: eager
                        matrix_need_update = true;
                    } else {
                        // key-up: defer
                        matrix_row_t cooked_next = (cooked[row] & ~col_mask) | (raw[row] & col_mask);
                        cooked_changed |= cooked_next ^ cooked[row];
                        cooked[row] = cooked_next;
                    }
                } else {
                    debounce_pointer->time -= elapsed_time;
                    counters_need_update = true;
                }
            }
            debounce_pointer++;
        }
    }
}

static void transfer_matrix_values(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows) {
    debounce_counter_t *debounce_pointer = debounce_counters;
    const uint8_t       press_delay      = debounce_runtime_press_delay();
    const uint8_t       release_delay    = debounce_runtime_release_delay();
    matrix_need_update = false;

    for (uint8_t row = 0; row < num_rows; row++) {
        matrix_row_t delta = raw[row] ^ cooked[row];
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            matrix_row_t col_mask = (ROW_SHIFTER << col);

            if (delta & col_mask) {
                if (debounce_pointer->time == DEBOUNCE_ELAPSED) {
                    debounce_pointer->pressed = (raw[row] & col_mask);
                    debounce_pointer->time    = debounce_pointer->pressed ? press_delay : release_delay;
                    counters_need_update      = true;

                    if (debounce_pointer->pressed) {
                        // key-down: eager
                        cooked[row] ^= col_mask;
                        cooked_changed = true;
                    }
                }
            } else if (debounce_pointer->time != DEBOUNCE_ELAPSED) {
                if (!debounce_pointer->pressed) {
                    // key-up: defer
                    debounce_pointer->time = DEBOUNCE_ELAPSED;
                }
            }
            debounce_pointer++;
        }
    }
}

