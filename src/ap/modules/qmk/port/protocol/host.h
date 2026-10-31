#pragma once

#include "hw_def.h"
#include "cli.h"

#include QMK_KEYMAP_CONFIG_H

#include "host_driver.h"
#include "qmk/quantum/led.h"


/* host driver */
void           host_set_driver(host_driver_t *driver);
host_driver_t *host_get_driver(void);


/* host driver interface */
uint8_t host_keyboard_leds(void);
led_t   host_keyboard_led_state(void);
void    host_keyboard_leds_update(uint8_t led_state);  // V251124R7: USB HID LED 상태를 캐시해 탭/홀드 분기 시 재사용
void    host_keyboard_send(report_keyboard_t *report);
void    host_keyboard_begin_key_update(uint32_t scan_token, uint8_t usage, bool pressed);
void    host_keyboard_end_key_update(void);
void    host_keyboard_end_key_scan(uint32_t scan_token);
uint32_t host_keyboard_key_update_count(void);
void    host_keyboard_delay(uint16_t delay_ms); // V260911R3: 키 상태 대신 USB 스냅샷의 유지 시간을 예약
void    host_nkro_send(report_nkro_t *report);
void    host_mouse_send(report_mouse_t *report);
void    host_system_send(uint16_t usage);
void    host_consumer_send(uint16_t usage);
void    host_programmable_button_send(uint32_t data);

uint16_t host_last_system_usage(void);
uint16_t host_last_consumer_usage(void);
