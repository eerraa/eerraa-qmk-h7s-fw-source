/*
Copyright 2011, 2012, 2013 Jun Wako <wakojun@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>
// V251010R3: _DEF_ENABLE_MATRIX_TIMING_PROBE 매크로를 참조하기 위해 하드웨어 설정 헤더를 포함합니다.
#include "hw_def.h"
#include "keyboard.h"
#include "keycode_config.h"
#include "matrix.h"
#include "keymap_introspection.h"
#include "host.h"
#include "led.h"
#include "keycode.h"
#include "timer.h"
#include "sync_timer.h"
#include "print.h"
#include "debug.h"
#include "command.h"
#include "util.h"
#include "sendchar.h"
#include "eeconfig.h"
#include "action_layer.h"
#include "suspend.h"
#ifdef BOOTMAGIC_ENABLE
#    include "bootmagic.h"
#endif
#ifdef AUDIO_ENABLE
#    include "audio.h"
#endif
#if defined(AUDIO_ENABLE) || (defined(MIDI_ENABLE) && defined(MIDI_BASIC))
#    include "process_music.h"
#endif
#ifdef BACKLIGHT_ENABLE
#    include "backlight.h"
#endif
#ifdef MOUSEKEY_ENABLE
#    include "mousekey.h"
#endif
#ifdef PS2_MOUSE_ENABLE
#    include "ps2_mouse.h"
#endif
#ifdef RGBLIGHT_ENABLE
#    include "rgblight.h"
#endif
#ifdef LED_MATRIX_ENABLE
#    include "led_matrix.h"
#endif
#ifdef RGB_MATRIX_ENABLE
#    include "rgb_matrix.h"
#endif
#ifdef ENCODER_ENABLE
#    include "encoder.h"
#endif
#ifdef HAPTIC_ENABLE
#    include "haptic.h"
#endif
#ifdef AUTO_SHIFT_ENABLE
#    include "process_auto_shift.h"
#endif
#ifdef COMBO_ENABLE
#    include "process_combo.h"
#endif
#ifdef TAP_DANCE_ENABLE
#    include "process_tap_dance.h"
#endif
#ifdef STENO_ENABLE
#    include "process_steno.h"
#endif
#ifdef KEY_OVERRIDE_ENABLE
#    include "process_key_override.h"
#endif
#ifdef SECURE_ENABLE
#    include "secure.h"
#endif
#ifdef POINTING_DEVICE_ENABLE
#    include "pointing_device.h"
#endif
#ifdef MIDI_ENABLE
#    include "process_midi.h"
#endif
#ifdef JOYSTICK_ENABLE
#    include "joystick.h"
#endif
#ifdef HD44780_ENABLE
#    include "hd44780.h"
#endif
#ifdef OLED_ENABLE
#    include "oled_driver.h"
#endif
#ifdef ST7565_ENABLE
#    include "st7565.h"
#endif
#ifdef VIA_ENABLE
#    include "via.h"
#endif
#ifdef DIP_SWITCH_ENABLE
#    include "dip_switch.h"
#endif
#ifdef EEPROM_DRIVER
#    include "eeprom_driver.h"
#endif
#if defined(CRC_ENABLE)
#    include "crc.h"
#endif
#ifdef VIRTSER_ENABLE
#    include "virtser.h"
#endif
#ifdef SLEEP_LED_ENABLE
#    include "sleep_led.h"
#endif
#ifdef SPLIT_KEYBOARD
#    include "split_util.h"
#endif
#ifdef BLUETOOTH_ENABLE
#    include "bluetooth.h"
#endif
#ifdef CAPS_WORD_ENABLE
#    include "caps_word.h"
#endif
#ifdef LEADER_ENABLE
#    include "leader.h"
#endif
#ifdef UNICODE_COMMON_ENABLE
#    include "unicode.h"
#endif
#ifdef WPM_ENABLE
#    include "wpm.h"
#endif
#ifdef OS_DETECTION_ENABLE
#    include "os_detection.h"
#endif

static uint32_t last_input_modification_time = 0;
uint32_t        last_input_activity_time(void) {
    return last_input_modification_time;
}
uint32_t last_input_activity_elapsed(void) {
    return sync_timer_elapsed32(last_input_modification_time);
}

static uint32_t last_matrix_modification_time = 0;
static uint32_t pending_matrix_activity_time  = 0;  // V251001R3: matrix_task에서 공유한 타임스탬프로 sync_timer_read32() 중복 호출 제거
uint32_t        last_matrix_activity_time(void) {
    return last_matrix_modification_time;
}
uint32_t last_matrix_activity_elapsed(void) {
    return sync_timer_elapsed32(last_matrix_modification_time);
}
void last_matrix_activity_trigger(void)
{
  uint32_t activity_time = pending_matrix_activity_time;  // V251001R3: matrix_task에서 기록한 32비트 타임스탬프를 우선 사용
  pending_matrix_activity_time = 0;

  if (!activity_time)
  {
    activity_time = sync_timer_read32();  // V251001R3: 타임스탬프가 공유되지 않은 예외 상황에서는 기존 경로를 사용
  }

  last_matrix_modification_time = last_input_modification_time = activity_time;
}

static uint32_t last_encoder_modification_time = 0;
uint32_t        last_encoder_activity_time(void) {
    return last_encoder_modification_time;
}
uint32_t last_encoder_activity_elapsed(void) {
    return sync_timer_elapsed32(last_encoder_modification_time);
}
void last_encoder_activity_trigger(void) {
    last_encoder_modification_time = last_input_modification_time = sync_timer_read32();
}

static uint32_t last_pointing_device_modification_time = 0;
uint32_t        last_pointing_device_activity_time(void) {
    return last_pointing_device_modification_time;
}
uint32_t last_pointing_device_activity_elapsed(void) {
    return sync_timer_elapsed32(last_pointing_device_modification_time);
}
void last_pointing_device_activity_trigger(void) {
    last_pointing_device_modification_time = last_input_modification_time = sync_timer_read32();
}

void set_activity_timestamps(uint32_t matrix_timestamp, uint32_t encoder_timestamp, uint32_t pointing_device_timestamp) {
    last_matrix_modification_time          = matrix_timestamp;
    last_encoder_modification_time         = encoder_timestamp;
    last_pointing_device_modification_time = pointing_device_timestamp;
    last_input_modification_time           = MAX(matrix_timestamp, MAX(encoder_timestamp, pointing_device_timestamp));
}

// Only enable this if console is enabled to print to
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
// V251010R4: DEBUG_MATRIX_SCAN_RATE 대신 _DEF_ENABLE_MATRIX_TIMING_PROBE 단일 플래그로 스캔 계측을 제어
static uint32_t matrix_timer           = 0;
static uint32_t matrix_scan_count      = 0;
static uint32_t last_matrix_scan_count = 0;

void matrix_scan_perf_task(void) {
    matrix_scan_count++;

    uint32_t timer_now = timer_read32();
    if (TIMER_DIFF_32(timer_now, matrix_timer) >= 1000) {
#    if defined(CONSOLE_ENABLE)
        dprintf("matrix scan frequency: %lu\n", matrix_scan_count);
#    endif
        last_matrix_scan_count = matrix_scan_count;
        matrix_timer           = timer_now;
        matrix_scan_count      = 0;
    }
}

uint32_t get_matrix_scan_rate(void) {
    return last_matrix_scan_count;
}
#else
void matrix_scan_perf_task(void)
{
    // V251010R4: 빌드 가드 비활성화 시에도 호출 지점을 유지하기 위해 빈 함수로 남깁니다.
}

uint32_t get_matrix_scan_rate(void)
{
    return 0U;  // V251010R4: 계측 비활성화 상태에서는 0을 반환해 CLI에서 안내 메시지를 출력합니다.
}
#endif

#ifdef MATRIX_HAS_GHOST
static matrix_row_t real_key_mask[MATRIX_ROWS];
static bool         real_key_mask_dirty[MATRIX_ROWS];
static bool         real_key_mask_ready;

static void mark_all_real_key_masks_dirty(void) {
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        real_key_mask_dirty[row] = true;  // V250928R3: 초기화 및 전체 무효화 시 지연 계산 대비
    }
    real_key_mask_ready = true;  // V250928R3: 최초 접근 시점에 한 번만 초기화하도록 표시
}

static void refresh_real_key_mask(uint8_t row) {
    matrix_row_t mask = 0;

    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        if (keycode_at_keymap_location(0, row, col)) {
            mask |= ((matrix_row_t)1) << col;
        }
    }

    real_key_mask[row]       = mask;      // V250928R3: 열 비트를 캐싱해 반복 키맵 조회 제거
    real_key_mask_dirty[row] = false;
}

void keyboard_keymap_real_keys_invalidate(uint8_t row) {
    if (row < MATRIX_ROWS) {
        if (!real_key_mask_ready) {
            mark_all_real_key_masks_dirty();  // V250928R3: 초기 접근 전에 무효화 요청이 들어오면 전체 초기화
        }
        real_key_mask_dirty[row] = true;      // V250928R3: 동적 키맵 변경 시 해당 행만 다시 계산
    }
}

void keyboard_keymap_real_keys_invalidate_all(void) {
    mark_all_real_key_masks_dirty();  // V250928R3: 대량 업데이트 시 전체 행을 다시 계산하도록 플래그 설정
}

static matrix_row_t get_real_keys(uint8_t row, matrix_row_t rowdata) {
    if (!real_key_mask_ready) {
        mark_all_real_key_masks_dirty();  // V250928R3: 최초 호출 시 모든 행을 지연 초기화로 준비
    }
    if (real_key_mask_dirty[row]) {
        refresh_real_key_mask(row);  // V250928R3: 필요한 행만 즉시 재계산
    }

    return rowdata & real_key_mask[row];
}

static matrix_row_t real_rowdata_cache[MATRIX_ROWS];          // V251001R4: 고스트 판정 시 필터링된 행 상태를 캐시해 중복 계산 제거
static uint32_t     real_rowdata_cache_epoch[MATRIX_ROWS];    // V251001R4: 행별 캐시가 유효한 스캔 번호를 추적
static uint32_t     real_rowdata_epoch = 1;                   // V251001R4: matrix_task() 호출 간 캐시 세대를 구분

static inline matrix_row_t get_cached_real_keys(uint8_t row, matrix_row_t rowdata)
{
  if (real_rowdata_cache_epoch[row] != real_rowdata_epoch)
  {
    real_rowdata_cache[row]       = get_real_keys(row, rowdata);  // V251001R4: 동일 스캔에서 반복 호출을 방지
    real_rowdata_cache_epoch[row] = real_rowdata_epoch;
  }

  return real_rowdata_cache[row];
}

static inline bool popcount_more_than_one(matrix_row_t rowdata) {
    rowdata &= rowdata - 1; // if there are less than two bits (keys) set, rowdata will become zero
    return rowdata;
}

static inline bool has_ghost_in_row(uint8_t row, matrix_row_t rowdata) {
    if ((rowdata & (rowdata - 1)) == 0) {
        return false;  // V250924R8: 물리적으로 0/1키만 눌린 경우 바로 종료해 키맵 필터 비용 절감
    }
    /* No ghost exists when less than 2 keys are down on the row.
    If there are "active" blanks in the matrix, the key can't be pressed by the user,
    there is no doubt as to which keys are really being pressed.
    The ghosts will be ignored, they are KC_NO.   */
    rowdata = get_cached_real_keys(row, rowdata);  // V251001R4: 필터링된 행 상태를 재사용해 키맵 마스크 연산 절감
    if ((popcount_more_than_one(rowdata)) == 0) {
        return false;
    }
    /* Ghost occurs when the row shares a column line with other row,
    and two columns are read on each row. Blanks in the matrix don't matter,
    so they are filtered out.
    If there are two or more real keys pressed and they match columns with
    at least two of another row's real keys, the row will be ignored. Keep in mind,
    we are checking one row at a time, not all of them at once.
    */
    for (uint8_t i = 0; i < MATRIX_ROWS; i++) {
        if (i == row) {
            continue;
        }

        const matrix_row_t other_row_state = matrix_get_row(i);
        const matrix_row_t overlap         = other_row_state & rowdata;

        if ((overlap & (overlap - 1)) == 0) {
            continue;  // V250928R2: 물리 중복 열이 0/1개면 고스트가 생길 수 없어 키맵 필터링을 생략
        }

        if (popcount_more_than_one(get_cached_real_keys(i, other_row_state) & rowdata)) {
            return true;
        }
    }
    return false;
}

#else

static inline bool has_ghost_in_row(uint8_t row, matrix_row_t rowdata) {
    return false;
}

#endif

/** \brief matrix_setup
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void matrix_setup(void) {}

/** \brief keyboard_pre_init_user
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void keyboard_pre_init_user(void) {}

/** \brief keyboard_pre_init_kb
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void keyboard_pre_init_kb(void) {
    keyboard_pre_init_user();
}

/** \brief keyboard_post_init_user
 *
 * FIXME: needs doc
 */

__attribute__((weak)) void keyboard_post_init_user(void) {}

/** \brief keyboard_post_init_kb
 *
 * FIXME: needs doc
 */

__attribute__((weak)) void keyboard_post_init_kb(void) {
    keyboard_post_init_user();
}

/** \brief matrix_can_read
 *
 * Allows overriding when matrix scanning operations should be executed.
 */
__attribute__((weak)) bool matrix_can_read(void) {
    return true;
}

/** \brief keyboard_setup
 *
 * FIXME: needs doc
 */
void keyboard_setup(void) {
    print_set_sendchar(sendchar);
#ifdef EEPROM_DRIVER
    eeprom_driver_init();
#endif
    matrix_setup();
    keyboard_pre_init_kb();
}

#ifndef SPLIT_KEYBOARD

/** \brief is_keyboard_master
 *
 * FIXME: needs doc
 */
__attribute__((weak)) bool is_keyboard_master(void) {
    return true;
}

/** \brief is_keyboard_left
 *
 * FIXME: needs doc
 */
__attribute__((weak)) bool is_keyboard_left(void) {
    return true;
}

#endif

/** \brief should_process_keypress
 *
 * Override this function if you have a condition where keypresses processing should change:
 *   - splits where the slave side needs to process for rgb/oled functionality
 */
__attribute__((weak)) bool should_process_keypress(void) {
    return is_keyboard_master();
}

/** \brief housekeeping_task_kb
 *
 * Override this function if you have a need to execute code for every keyboard main loop iteration.
 * This is specific to keyboard-level functionality.
 */
__attribute__((weak)) void housekeeping_task_kb(void) {}

/** \brief housekeeping_task_user
 *
 * Override this function if you have a need to execute code for every keyboard main loop iteration.
 * This is specific to user/keymap-level functionality.
 */
__attribute__((weak)) void housekeeping_task_user(void) {}

/** \brief housekeeping_task
 *
 * Invokes hooks for executing code after QMK is done after each loop iteration.
 */
void housekeeping_task(void) {
    housekeeping_task_kb();
    housekeeping_task_user();
}

/** \brief quantum_init
 *
 * Init global state
 */
void quantum_init(void) {
    /* check signature */
    if (!eeconfig_is_enabled()) {
        eeconfig_init();
    }

    /* init globals */
    debug_config.raw  = eeconfig_read_debug();
    keymap_config.raw = eeconfig_read_keymap();

#ifdef BOOTMAGIC_ENABLE
    bootmagic();
#endif

    /* read here just incase bootmagic process changed its value */
    layer_state_t default_layer = (layer_state_t)eeconfig_read_default_layer();
    default_layer_set(default_layer);

    /* Also initialize layer state to trigger callback functions for layer_state */
    layer_state_set_kb((layer_state_t)layer_state);
}

/** \brief keyboard_init
 *
 * FIXME: needs doc
 */
void keyboard_init(void) {
    timer_init();
    sync_timer_init();
#ifdef VIA_ENABLE
    via_init();
#endif
#ifdef SPLIT_KEYBOARD
    split_pre_init();
#endif
#ifdef ENCODER_ENABLE
    encoder_init();
#endif
    matrix_init();
    quantum_init();
    led_init_ports();
#ifdef BACKLIGHT_ENABLE
    backlight_init_ports();
#endif
#ifdef AUDIO_ENABLE
    audio_init();
#endif
#ifdef LED_MATRIX_ENABLE
    led_matrix_init();
#endif
#ifdef RGB_MATRIX_ENABLE
    rgb_matrix_init();
#endif
#if defined(UNICODE_COMMON_ENABLE)
    unicode_input_mode_init();
#endif
#if defined(CRC_ENABLE)
    crc_init();
#endif
#ifdef OLED_ENABLE
    oled_init(OLED_ROTATION_0);
#endif
#ifdef ST7565_ENABLE
    st7565_init(DISPLAY_ROTATION_0);
#endif
#ifdef PS2_MOUSE_ENABLE
    ps2_mouse_init();
#endif
#ifdef BACKLIGHT_ENABLE
    backlight_init();
#endif
#ifdef RGBLIGHT_ENABLE
    rgblight_init();
#endif
#ifdef STENO_ENABLE_ALL
    steno_init();
#endif
#if defined(NKRO_ENABLE) && defined(FORCE_NKRO)
    keymap_config.nkro = 1;
    eeconfig_update_keymap(keymap_config.raw);
#endif
#ifdef DIP_SWITCH_ENABLE
    dip_switch_init();
#endif
#ifdef JOYSTICK_ENABLE
    joystick_init();
#endif
#ifdef SLEEP_LED_ENABLE
    sleep_led_init();
#endif
#ifdef VIRTSER_ENABLE
    virtser_init();
#endif
#ifdef SPLIT_KEYBOARD
    split_post_init();
#endif
#ifdef POINTING_DEVICE_ENABLE
    // init after split init
    pointing_device_init();
#endif
#ifdef BLUETOOTH_ENABLE
    bluetooth_init();
#endif
#ifdef HAPTIC_ENABLE
    haptic_init();
#endif

#if _DEF_ENABLE_MATRIX_TIMING_PROBE && defined(CONSOLE_ENABLE)
    // V251010R4: 콘솔 자동 활성화 조건을 새로운 빌드 가드에 맞춰 조정
    debug_enable = true;
#endif

    keyboard_post_init_kb(); /* Always keep this last */
}

/** \brief key_event_task
 *
 * This function is responsible for calling into other systems when they need to respond to electrical switch press events.
 * This is differnet than keycode events as no layer processing, or filtering occurs.
 */
void switch_events(uint8_t row, uint8_t col, bool pressed) {
#if defined(LED_MATRIX_ENABLE)
    process_led_matrix(row, col, pressed);
#endif
#if defined(RGB_MATRIX_ENABLE)
    process_rgb_matrix(row, col, pressed);
#endif
}

/**
 * @brief Generates a tick event at a maximum rate of 1KHz that drives the
 * internal QMK state machine.
 */
static inline void generate_tick_event(void)
{
  static uint32_t last_tick = 0;
  const uint32_t  now       = timer_read32();

  if (TIMER_DIFF_32(now, last_tick) != 0)
  {
    const keyevent_t tick_event = {
      .key = {.row = 0, .col = 0},
      .time = now,            // V251001R1: tick 이벤트도 스캔 시각을 재사용해 timer_read() 중복 호출 제거
      .type = TICK_EVENT,
      .pressed = false,
    };

    action_exec(tick_event);
    last_tick = now;
  }
}

/**
 * @brief This task scans the keyboards matrix and processes any key presses
 * that occur.
 *
 * @return true Matrix did change
 * @return false Matrix didn't change
 */
static bool matrix_task(void) {
    if (!matrix_can_read()) {
        generate_tick_event();
        return false;
    }

    static matrix_row_t matrix_previous[MATRIX_ROWS];
    static uint32_t report_scan_token;
    static bool         ghost_pending = false;  // V250924R6: 고스트 감지 시 후속 스캔에서도 행 비교 유지

    const bool scan_changed   = matrix_scan();
    bool       matrix_changed = scan_changed || ghost_pending;

    matrix_scan_perf_task();

    // 고스트가 없고 스캔 결과가 동일하면 행 순회를 생략해 낭비를 줄인다. (V250924R6)
    if (!matrix_changed) {
        generate_tick_event();
        return false;
    }
    /* Millisecond event timestamps cannot distinguish consecutive matrix scans. */
    if (++report_scan_token == 0U) ++report_scan_token;

#ifdef MATRIX_HAS_GHOST
    real_rowdata_epoch++;  // V251001R4: 스캔마다 캐시 세대를 갱신해 행별 필터링 결과를 분리
    if (real_rowdata_epoch == 0)
    {
      real_rowdata_epoch = 1;  // V251001R4: 32비트 오버플로우 시 세대 값을 재설정
      for (uint8_t i = 0; i < MATRIX_ROWS; i++)
      {
        real_rowdata_cache_epoch[i] = 0;  // V251001R4: 오버플로우 후 이전 캐시가 재사용되지 않도록 무효화
      }
    }
#endif

    if (debug_config.matrix) {
        matrix_print();
    }

    bool       process_keypress    = false;
    bool       event_initialized   = false;
    bool       new_ghost_pending   = false;
    uint32_t   event_time_32       = 0;    // V251001R3: 키 이벤트와 활동 타임스탬프를 공유해 타이머 접근을 1회로 축소
    keyevent_t event;

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        const matrix_row_t current_row = matrix_get_row(row);
        const matrix_row_t row_changes = current_row ^ matrix_previous[row];

        if (!row_changes) {
            continue;
        }

        if (!event_time_32)
        {
          event_time_32 = sync_timer_read32();        // V251001R3: 첫 변화 시점의 32비트 타임스탬프를 확보해 이후 재사용
        }

        if (has_ghost_in_row(row, current_row)) {
            new_ghost_pending = true;
            continue;
        }

        if (!event_initialized) {
            // V251001R2: 실제 키 이벤트가 확정된 이후에만 should_process_keypress()와 timer_read()를 호출해 고스트 반복 검사 시 낭비 제거
            process_keypress  = should_process_keypress();
            event_initialized = true;

            if (process_keypress) {
                event = (keyevent_t){
                    .key = {.row = 0, .col = 0},
                    .time = event_time_32, // Keep the captured scan time through tapping-buffer replay.
                    .type = KEY_EVENT,
                    .pressed = false,
                };
            }
        }

        matrix_row_t pending_changes = row_changes;
        if (process_keypress) {
            event.key.row = row;
            event.key.col = 0;
        }

        while (pending_changes) {
            const uint8_t      col      = __builtin_ctz((unsigned long)pending_changes);  // V250924R7: 변경 비트만 스캔해 열 루프 비용 최소화
            const matrix_row_t col_mask = ((matrix_row_t)1) << col;

            pending_changes &= pending_changes - 1;  // V250924R7: 처리한 비트를 제거해 반복 횟수를 줄임

            const bool key_pressed = current_row & col_mask;

            // Remote wake belongs to the physical switch event, not to whether QMK later
            // emits a keyboard HID report. This also wakes on layer-only/consumed keys.
            suspend_wakeup_key_event(row, col, key_pressed);

#ifdef RGBLIGHT_ENABLE
            // V260911R1: RGB 입력은 판정/재생 전 물리 전이가 소유하며 스캔 시각을 공유한다.
            rgblight_handle_physical_key(key_pressed, row, col, event_time_32);
#endif

            if (process_keypress && !keypress_is_wakeup_key(row, col)) {
                event.key.col = col;
                event.pressed = key_pressed;
                action_exec_physical(event, report_scan_token);
            }

            switch_events(row, col, key_pressed);
            wakeup_matrix_handle_key_event(row, col, key_pressed);
        }

        matrix_previous[row] = current_row;
    }

    if (event_time_32)
    {
      pending_matrix_activity_time = event_time_32;  // V251001R3: matrix_task와 last_matrix_activity_trigger() 간 타임스탬프 공유
    }

    host_keyboard_end_key_scan(report_scan_token);
    ghost_pending = new_ghost_pending;

    return true;
}

/** \brief Tasks previously located in matrix_scan_quantum
 *
 * TODO: rationalise against keyboard_task and current split role
 */
void quantum_task(void) {
#ifdef SPLIT_KEYBOARD
    // some tasks should only run on master
    if (!is_keyboard_master()) return;
#endif

#if defined(AUDIO_ENABLE) && defined(AUDIO_INIT_DELAY)
    // There are some tasks that need to be run a little bit
    // after keyboard startup, or else they will not work correctly
    // because of interaction with the USB device state, which
    // may still be in flux...
    //
    // At the moment the only feature that needs this is the
    // startup song.
    static bool     delayed_tasks_run  = false;
    static uint16_t delayed_task_timer = 0;
    if (!delayed_tasks_run) {
        if (!delayed_task_timer) {
            delayed_task_timer = timer_read();
        } else if (timer_elapsed(delayed_task_timer) > 300) {
            audio_startup();
            delayed_tasks_run = true;
        }
    }
#endif

#if defined(AUDIO_ENABLE) && !defined(NO_MUSIC_MODE)
    music_task();
#endif

#ifdef KEY_OVERRIDE_ENABLE
    key_override_task();
#endif

#ifdef SEQUENCER_ENABLE
    sequencer_task();
#endif

#ifdef TAP_DANCE_ENABLE
    tap_dance_task();
#endif

#ifdef COMBO_ENABLE
    combo_task();
#endif

#ifdef LEADER_ENABLE
    leader_task();
#endif

#ifdef WPM_ENABLE
    decay_wpm();
#endif

#ifdef DIP_SWITCH_ENABLE
    dip_switch_task();
#endif

#ifdef AUTO_SHIFT_ENABLE
    autoshift_matrix_scan();
#endif

#ifdef CAPS_WORD_ENABLE
    caps_word_task();
#endif

#ifdef SECURE_ENABLE
    secure_task();
#endif
}

/** \brief Main task that is repeatedly called as fast as possible. */
void keyboard_task(void) {
    __attribute__((unused)) bool activity_has_occurred = false;
    if (matrix_task()) {
        last_matrix_activity_trigger();
        activity_has_occurred = true;
    }

    quantum_task();

#if defined(SPLIT_WATCHDOG_ENABLE)
    split_watchdog_task();
#endif

#if defined(RGBLIGHT_ENABLE)
    rgblight_task();
#endif

#ifdef LED_MATRIX_ENABLE
    led_matrix_task();
#endif
#ifdef RGB_MATRIX_ENABLE
    rgb_matrix_task();
#endif

#if defined(BACKLIGHT_ENABLE)
#    if defined(BACKLIGHT_PIN) || defined(BACKLIGHT_PINS)
    backlight_task();
#    endif
#endif

#ifdef ENCODER_ENABLE
    if (encoder_task()) {
        last_encoder_activity_trigger();
        activity_has_occurred = true;
    }
#endif

#ifdef POINTING_DEVICE_ENABLE
    if (pointing_device_task()) {
        last_pointing_device_activity_trigger();
        activity_has_occurred = true;
    }
#endif

#ifdef OLED_ENABLE
    oled_task();
#    if OLED_TIMEOUT > 0
    // Wake up oled if user is using those fabulous keys or spinning those encoders!
    if (activity_has_occurred) oled_on();
#    endif
#endif

#ifdef ST7565_ENABLE
    st7565_task();
#    if ST7565_TIMEOUT > 0
    // Wake up display if user is using those fabulous keys or spinning those encoders!
    if (activity_has_occurred) st7565_on();
#    endif
#endif

#ifdef MOUSEKEY_ENABLE
    // mousekey repeat & acceleration
    mousekey_task();
#endif

#ifdef PS2_MOUSE_ENABLE
    ps2_mouse_task();
#endif

#ifdef MIDI_ENABLE
    midi_task();
#endif

#ifdef JOYSTICK_ENABLE
    joystick_task();
#endif

#ifdef BLUETOOTH_ENABLE
    bluetooth_task();
#endif

#ifdef HAPTIC_ENABLE
    haptic_task();
#endif

#ifdef OS_DETECTION_ENABLE
    os_detection_task();
#endif
}
