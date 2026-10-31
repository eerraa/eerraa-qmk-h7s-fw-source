#pragma once


#include <stdbool.h>
#include <stdint.h>


#include "era_wire.h"

// GET_KEYBOARD_VALUE vendor selector; allocations belong to the registry.
#define ERA_STATE_SYNC_KEYBOARD_VALUE          ERA_WIRE_STATE_SYNC_SELECTORS_CURRENT
#define ERA_STATE_SYNC_ENVELOPE_VERSION        ERA_WIRE_STATE_SYNC_ENVELOPE_VERSION
#define ERA_STATE_SYNC_STATUS_OK               ERA_WIRE_STATE_SYNC_STATUS_OK
#define ERA_STATE_SYNC_STATUS_UNSUPPORTED_VERSION ERA_WIRE_STATE_SYNC_STATUS_UNSUPPORTED_VERSION
#define ERA_STATE_SYNC_STATUS_INVALID          ERA_WIRE_STATE_SYNC_STATUS_INVALID

#define ERA_STATE_SYNC_DOMAIN_KEYMAP           ERA_WIRE_STATE_SYNC_DOMAINS_KEYMAP
#define ERA_STATE_SYNC_DOMAIN_MACRO            ERA_WIRE_STATE_SYNC_DOMAINS_MACRO
#define ERA_STATE_SYNC_DOMAIN_CONFIG           ERA_WIRE_STATE_SYNC_DOMAINS_CONFIG
#define ERA_STATE_SYNC_DOMAIN_MASK_INITIAL     ERA_WIRE_STATE_SYNC_DOMAINS_MASK_INITIAL


void     era_state_sync_bump_keymap(void);
void     era_state_sync_bump_macro(void);
void     era_state_sync_bump_config(void);
bool     era_state_sync_via_command(uint8_t *data, uint8_t length);
uint32_t era_state_sync_keymap_revision(void);
uint32_t era_state_sync_macro_revision(void);
uint32_t era_state_sync_config_revision(void);
