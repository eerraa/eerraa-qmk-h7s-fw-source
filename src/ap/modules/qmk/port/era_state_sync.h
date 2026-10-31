#pragma once


#include <stdbool.h>
#include <stdint.h>


// V260821R1: GET_KEYBOARD_VALUE(0x02) selector. G1 승인 0x06.
#define ERA_STATE_SYNC_KEYBOARD_VALUE          0x06U
#define ERA_STATE_SYNC_ENVELOPE_VERSION        0x01U
#define ERA_STATE_SYNC_STATUS_OK               0x00U
#define ERA_STATE_SYNC_STATUS_UNSUPPORTED_VERSION 0x01U
#define ERA_STATE_SYNC_STATUS_INVALID          0x02U

#define ERA_STATE_SYNC_DOMAIN_KEYMAP           0x01U
#define ERA_STATE_SYNC_DOMAIN_MACRO            0x02U
#define ERA_STATE_SYNC_DOMAIN_CONFIG           0x04U
#define ERA_STATE_SYNC_DOMAIN_MASK_INITIAL     0x07U


void     era_state_sync_bump_keymap(void);
void     era_state_sync_bump_macro(void);
void     era_state_sync_bump_config(void);
bool     era_state_sync_via_command(uint8_t *data, uint8_t length);
uint32_t era_state_sync_keymap_revision(void);
uint32_t era_state_sync_macro_revision(void);
uint32_t era_state_sync_config_revision(void);
