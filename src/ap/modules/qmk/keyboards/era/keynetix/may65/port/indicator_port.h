#pragma once

#include "quantum.h"

void led_init_ports(void);
void led_update_ports(led_t led_state);
void indicator_port_via_command(uint8_t *data, uint8_t length);
