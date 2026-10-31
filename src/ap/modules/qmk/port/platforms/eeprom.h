// Copyright 2018-2022 QMK
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once


#include "hw_def.h"



bool     eeprom_init(void);
void     eeprom_update(void);
bool     eeprom_is_pending(void);
// A completion byte is published only after prior writes and its own readback.
bool     eeprom_commit_byte(uint8_t *addr, uint8_t value);
bool     eeprom_prepare_commit(uint8_t *addr, uint8_t invalid);
bool     eeprom_commit_failed(void);
bool     eeprom_commit_is_pending(void);
bool     eeprom_byte_is_pending(uintptr_t address);
void     eeprom_note_commit(uint32_t address, uint32_t length);
bool     eeprom_flush_pending(void);
void     eeprom_task(void);
bool     eeprom_req_clean(void);
uint8_t  eeprom_read_byte(const uint8_t *addr);
uint16_t eeprom_read_word(const uint16_t *addr);
uint32_t eeprom_read_dword(const uint32_t *addr);
void     eeprom_read_block(void *buf, const void *addr, uint32_t len);
void     eeprom_write_byte(uint8_t *addr, uint8_t value);
void     eeprom_write_word(uint16_t *addr, uint16_t value);
void     eeprom_write_dword(uint32_t *addr, uint32_t value);
void     eeprom_write_block(const void *buf, void *addr, size_t len);
void     eeprom_update_byte(uint8_t *addr, uint8_t value);
void     eeprom_update_word(uint16_t *addr, uint16_t value);
void     eeprom_update_dword(uint32_t *addr, uint32_t value);
void     eeprom_update_block(const void *buf, void *addr, size_t len);
bool     eeprom_apply_factory_defaults(bool write_reset_guard);  // reset guard와 VIA CLEAN의 공용 초기화
uint32_t eeprom_get_write_pending_count(void);                       // V251112R2: EEPROM 큐 현재 사용량 조회
uint32_t eeprom_get_write_pending_max(void);                         // V251112R2: 부트 후 최고 사용량 조회
uint32_t eeprom_get_write_overflow_count(void);                      // V251112R2: 직접 기록된 횟수 조회

// V260909R1: pending count는 큐 엔트리가 아니라 아직 ACK되지 않은 서로 다른 byte 수다.
bool eeprom_is_ready(void);
uint32_t eeprom_get_write_failure_count(void);
uint32_t eeprom_get_invalid_access_count(void);
