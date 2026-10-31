#pragma once


#include <stdbool.h>
#include <stdint.h>


// MOUSE v2 stores integer report counts and real millisecond durations.
// Legacy reads project presets; exact reads preserve the authoritative values.

void mousekey_config_init(void);
void mousekey_config_storage_apply_defaults(void);
void mousekey_config_storage_stage_defaults(void);
void mousekey_config_storage_flush(bool force);
bool mousekey_config_handle_via_command(uint8_t *data, uint8_t length);
