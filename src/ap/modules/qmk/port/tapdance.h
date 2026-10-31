#pragma once


#include QMK_KEYMAP_CONFIG_H


#ifdef TAPDANCE_ENABLE

#include <stdbool.h>
#include <stdint.h>


#define TAPDANCE_SLOT_COUNT    8    // V251124R8: VIA Tap Dance 슬롯 수 고정
#define TAPDANCE_ACTION_COUNT  4


void     tapdance_init(void);
bool     tapdance_handle_via_command(uint8_t *data, uint8_t length);
void     tapdance_storage_apply_defaults(void);
void     tapdance_storage_stage_defaults(void);
void     tapdance_storage_flush(bool force);
uint16_t tapdance_get_term_ms(uint16_t keycode);
uint16_t tapdance_decision_term(uint8_t slot, uint8_t runtime_index, bool pressed);
bool tapdance_hold_on_interrupt(uint8_t runtime_index, uint8_t count, bool pressed);
bool     tapdance_should_finish_immediate(uint8_t slot_index, uint8_t tap_count, uint8_t runtime_index);  // V251125R1: Vial 호환 조기 완료 조건 노출

#endif
