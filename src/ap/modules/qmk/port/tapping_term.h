#pragma once


#include QMK_KEYMAP_CONFIG_H


#ifdef G_TERM_ENABLE

#include <stdbool.h>
#include <stdint.h>


void tapping_term_init(void);
bool tapping_term_handle_via_command(uint8_t *data, uint8_t length);
void tapping_term_storage_apply_defaults(void);
void tapping_term_storage_stage_defaults(void);
void tapping_term_storage_flush(bool force);

#endif
