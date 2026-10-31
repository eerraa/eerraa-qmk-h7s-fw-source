#pragma once

#include "quantum.h"

void led_init_ports(void);
void led_update_ports(led_t led_state);
void indicator_port_via_command(uint8_t *data, uint8_t length);  // V251012R2: VIA 커스텀 채널 라우팅
