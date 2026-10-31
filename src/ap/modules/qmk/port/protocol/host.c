/*
Copyright 2011,2012 Jun Wako <wakojun@gmail.com>

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
#include "keyboard.h"
#include "keycode.h"
#include "host.h"
#include "util.h"
#include "debug.h"
#include "usb.h"
#ifdef ERA_MACRO_ENABLE
#    include "qmk/port/era_macro.h"
#endif
#include "action_util.h"


#ifdef DIGITIZER_ENABLE
#    include "digitizer.h"
#endif

#ifdef JOYSTICK_ENABLE
#    include "joystick.h"
#endif

#ifdef BLUETOOTH_ENABLE
#    include "bluetooth.h"
#    include "outputselect.h"
#endif

#ifdef NKRO_ENABLE
#    include "keycode_config.h"
extern keymap_config_t keymap_config;
#endif

static host_driver_t *driver;
static uint16_t       last_system_usage   = 0;
static uint16_t       last_consumer_usage = 0;
static volatile uint8_t host_led_state    = 0;  // V251124R7: USB HID SET_REPORT로 수신한 LED 상태 캐시

static uint32_t key_update_scan_token;
static uint8_t  key_update_usage;
static bool     key_update_pressed;
static uint32_t key_update_count;

#ifdef ERA_MACRO_ENABLE
/* Payload equality does not imply equal lifetime: an unchanged union can become
 * generation-bound when a macro acquires it. These cache the last submit attempt,
 * not USB acceptance (overflow may still update the transport's latest state). */
typedef struct {
    uint32_t generation;
    bool bound;
} host_report_context_t;
static host_report_context_t keyboard_report_context;
static host_report_context_t system_report_context;
static host_report_context_t consumer_report_context;

static host_report_context_t host_report_context_current(void) {
    host_report_context_t context = {0};
    context.bound = era_macro_report_generation(&context.generation);
    return context;
}

static bool host_report_context_changed(const host_report_context_t *previous) {
    const host_report_context_t current = host_report_context_current();
    return current.bound != previous->bound ||
           (current.bound && current.generation != previous->generation);
}
#endif

bool host_keyboard_report_needs_send(void) {
#ifdef ERA_MACRO_ENABLE
    return host_report_context_changed(&keyboard_report_context);
#else
    return false;
#endif
}

void host_set_driver(host_driver_t *d) {
    driver = d;
}

host_driver_t *host_get_driver(void) {
    return driver;
}

#ifdef SPLIT_KEYBOARD
uint8_t split_led_state = 0;
void    set_split_host_keyboard_leds(uint8_t led_state) {
    split_led_state = led_state;
}
#endif

void host_keyboard_leds_update(uint8_t led_state)
{
  host_led_state = led_state;  // V251124R7: IRQ 컨텍스트에서 전달된 LED 비트를 저장해 탭/홀드 후 재사용
}

__WEAK uint8_t host_keyboard_leds(void)
{
#ifdef SPLIT_KEYBOARD
  if (!is_keyboard_master())
  {
    return split_led_state;
  }
#endif
  if (driver && driver->keyboard_leds != NULL)
  {
    uint8_t leds = (*driver->keyboard_leds)();

    host_led_state = leds;  // V251124R7: 드라이버 경로로 읽은 LED 상태를 캐시에 동기화
    return leds;
  }

  return host_led_state;  // V251124R7: 드라이버가 없을 때는 최근 HID SET_REPORT 값으로 응답
}

led_t host_keyboard_led_state(void) {
    return (led_t)host_keyboard_leds();
}

// V260911R3: QMK는 논리 해제를 즉시 완료하고, 호스트에 보이는 간격은 전송 계층이 소유한다.
void host_keyboard_delay(uint16_t delay_ms)
{
#ifdef ERA_MACRO_ENABLE
  uint32_t generation;
  if (era_macro_report_generation(&generation)) {
    usbHidDelayKeyboardReportForGeneration(delay_ms, generation);
    return;
  }
#endif
  usbHidDelayKeyboardReport(delay_ms);
}

/* send report */
void host_keyboard_begin_key_update(uint32_t scan_token, uint8_t usage, bool pressed)
{
  key_update_scan_token = scan_token;
  key_update_usage = usage;
  key_update_pressed = pressed;
}

void host_keyboard_end_key_update(void)
{
  key_update_scan_token = 0U;
}

void host_keyboard_end_key_scan(uint32_t scan_token)
{
  host_keyboard_end_key_update();
  usbHidEndKeyScan(scan_token);
}

uint32_t host_keyboard_key_update_count(void)
{
  return key_update_count;
}

void host_keyboard_send(report_keyboard_t *report)
{
#ifdef BLUETOOTH_ENABLE
  if (where_to_send() == OUTPUT_BLUETOOTH)
  {
    bluetooth_send_keyboard(report);
    return;
  }
#endif

  uint32_t scan_token = key_update_scan_token;
  key_update_scan_token = 0U;  // A native key action may opt in only its first report.
#ifdef ERA_MACRO_ENABLE
  keyboard_report_context = host_report_context_current();
  if (keyboard_report_context.bound) {
    usbHidSendReportForGeneration((uint8_t *)report, sizeof(report_keyboard_t), keyboard_report_context.generation);
  } else
#endif
  if (scan_token != 0U) {
    key_update_count++;
    usbHidSubmitKeyUpdate((uint8_t *)report, sizeof(report_keyboard_t), scan_token, key_update_usage, key_update_pressed);
  } else
    usbHidSendReport((uint8_t *)report, sizeof(report_keyboard_t));

  #ifdef DEBUG_KEY_SEND
  static uint32_t pre_time = 0;
  static uint32_t exe_time = 0;

  exe_time = micros() - pre_time;
  pre_time = micros();

  cliPrintf("report : 0x%02X, %02X %02X %02X %02X %02X %02X %03d.%03d ms\n",
            report->mods,
            report->keys[0],
            report->keys[1],
            report->keys[2],
            report->keys[3],
            report->keys[4],
            report->keys[5],
            exe_time/1000,
            exe_time%1000);
  #endif

  if (!driver) return;
#ifdef KEYBOARD_SHARED_EP
  report->report_id = REPORT_ID_KEYBOARD;
#endif
  (*driver->send_keyboard)(report);

  if (debug_keyboard)
  {
    dprintf("keyboard_report: %02X | ", report->mods);
    for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS; i++)
    {
      dprintf("%02X ", report->keys[i]);
    }
    dprint("\n");
  }
}

void host_nkro_send(report_nkro_t *report) {
    if (!driver) return;
    report->report_id = REPORT_ID_NKRO;
    (*driver->send_nkro)(report);

    if (debug_keyboard) {
        dprintf("nkro_report: %02X | ", report->mods);
        for (uint8_t i = 0; i < NKRO_REPORT_BITS; i++) {
            dprintf("%02X ", report->bits[i]);
        }
        dprint("\n");
    }
}

static void host_extra_snapshot(uint8_t *data, uint16_t length) {
#ifdef ERA_MACRO_ENABLE
    const host_report_context_t context = host_report_context_current();
    if (data[0] == REPORT_ID_SYSTEM) system_report_context = context;
    if (data[0] == REPORT_ID_CONSUMER) consumer_report_context = context;
    if (context.bound) {
        usbHidSendReportEXKForGeneration(data, length, context.generation);
        return;
    }
#endif
    usbHidSendReportEXK(data, length);
}

void host_mouse_send(report_mouse_t *report) {
#ifdef BLUETOOTH_ENABLE
    if (where_to_send() == OUTPUT_BLUETOOTH) {
        bluetooth_send_mouse(report);
        return;
    }
#endif

#ifdef MOUSE_SHARED_EP
    report->report_id = REPORT_ID_MOUSE;
#endif
#ifdef MOUSE_EXTENDED_REPORT
    // clip and copy to Boot protocol XY
    report->boot_x = (report->x > 127) ? 127 : ((report->x < -127) ? -127 : report->x);
    report->boot_y = (report->y > 127) ? 127 : ((report->y < -127) ? -127 : report->y);
#endif
    host_extra_snapshot((uint8_t *)report, sizeof(report_mouse_t));  // V260823R1: shared 엔드포인트로 마우스 리포트 전송
}

void host_system_send(uint16_t usage) {
  if (usage == last_system_usage
#ifdef ERA_MACRO_ENABLE
      && !host_report_context_changed(&system_report_context)
#endif
  ) return;
  last_system_usage = usage;

  report_extra_t report = {
    .report_id = REPORT_ID_SYSTEM,
    .usage     = usage,
  };

  host_extra_snapshot((uint8_t *)&report, sizeof(report_extra_t));

#ifdef DEBUG_KEY_SEND
  static uint32_t pre_time = 0;
  static uint32_t exe_time = 0;

  exe_time = micros() - pre_time;
  pre_time = micros();

  cliPrintf("report exk(sys) : %04X %03d.%03d ms\n",
            usage,
            exe_time / 1000,
            exe_time % 1000);
#endif
}

void host_consumer_send(uint16_t usage) {
  if (usage == last_consumer_usage
#ifdef ERA_MACRO_ENABLE
      && !host_report_context_changed(&consumer_report_context)
#endif
  ) return;
  last_consumer_usage = usage;

#ifdef BLUETOOTH_ENABLE
  if (where_to_send() == OUTPUT_BLUETOOTH)
  {
    bluetooth_send_consumer(usage);
    return;
  }
#endif

  report_extra_t report = {
    .report_id = REPORT_ID_CONSUMER,
    .usage     = usage,
  };

  host_extra_snapshot((uint8_t *)&report, sizeof(report_extra_t));

#ifdef DEBUG_KEY_SEND
  static uint32_t pre_time = 0;
  static uint32_t exe_time = 0;

  exe_time = micros() - pre_time;
  pre_time = micros();

  cliPrintf("report exk(csr) : %04X %03d.%03d ms\n",
            usage,
            exe_time / 1000,
            exe_time % 1000);
#endif  
}


#ifdef ACTION_OWNERSHIP_ENABLE
void host_extra_reconcile(void) {
    last_system_usage = action_owned_usage(ACTION_USAGE_PAGE_SYSTEM, last_system_usage);
    last_consumer_usage = action_owned_usage(ACTION_USAGE_PAGE_CONSUMER, last_consumer_usage);
    report_extra_t system = {.report_id = REPORT_ID_SYSTEM, .usage = last_system_usage};
    report_extra_t consumer = {.report_id = REPORT_ID_CONSUMER, .usage = last_consumer_usage};
    host_extra_snapshot((uint8_t *)&system, sizeof(system));
    host_extra_snapshot((uint8_t *)&consumer, sizeof(consumer));
}
#endif

#ifdef JOYSTICK_ENABLE
void host_joystick_send(joystick_t *joystick) {
    if (!driver) return;

    report_joystick_t report = {
#    ifdef JOYSTICK_SHARED_EP
        .report_id = REPORT_ID_JOYSTICK,
#    endif
#    if JOYSTICK_AXIS_COUNT > 0
        .axes =
            {
                joystick->axes[0],

#        if JOYSTICK_AXIS_COUNT >= 2
                joystick->axes[1],
#        endif
#        if JOYSTICK_AXIS_COUNT >= 3
                joystick->axes[2],
#        endif
#        if JOYSTICK_AXIS_COUNT >= 4
                joystick->axes[3],
#        endif
#        if JOYSTICK_AXIS_COUNT >= 5
                joystick->axes[4],
#        endif
#        if JOYSTICK_AXIS_COUNT >= 6
                joystick->axes[5],
#        endif
            },
#    endif

#    if JOYSTICK_BUTTON_COUNT > 0
        .buttons =
            {
                joystick->buttons[0],

#        if JOYSTICK_BUTTON_COUNT > 8
                joystick->buttons[1],
#        endif
#        if JOYSTICK_BUTTON_COUNT > 16
                joystick->buttons[2],
#        endif
#        if JOYSTICK_BUTTON_COUNT > 24
                joystick->buttons[3],
#        endif
            },
#    endif
    };

    send_joystick(&report);
}
#endif

__attribute__((weak)) void send_joystick(report_joystick_t *report) {}

#ifdef DIGITIZER_ENABLE
void host_digitizer_send(digitizer_t *digitizer) {
    report_digitizer_t report = {
#    ifdef DIGITIZER_SHARED_EP
        .report_id = REPORT_ID_DIGITIZER,
#    endif
        .in_range = digitizer->in_range,
        .tip      = digitizer->tip,
        .barrel   = digitizer->barrel,
        .x        = (uint16_t)(digitizer->x * 0x7FFF),
        .y        = (uint16_t)(digitizer->y * 0x7FFF),
    };

    send_digitizer(&report);
}
#endif

__attribute__((weak)) void send_digitizer(report_digitizer_t *report) {}

#ifdef PROGRAMMABLE_BUTTON_ENABLE
void host_programmable_button_send(uint32_t data) {
    report_programmable_button_t report = {
        .report_id = REPORT_ID_PROGRAMMABLE_BUTTON,
        .usage     = data,
    };

    send_programmable_button(&report);
}
#endif

__attribute__((weak)) void send_programmable_button(report_programmable_button_t *report) {}

uint16_t host_last_system_usage(void) {
    return last_system_usage;
}

uint16_t host_last_consumer_usage(void) {
    return last_consumer_usage;
}
