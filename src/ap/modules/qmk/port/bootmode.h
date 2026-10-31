#pragma once

#include <stdint.h>

#include QMK_KEYMAP_CONFIG_H

// V251108R1: BootMode VIA 채널 핸들러 선언
#ifdef BOOTMODE_ENABLE
void via_qmk_usb_bootmode_command(uint8_t *data, uint8_t length);
void bootmode_publish_defaults(void);
#else
static inline void via_qmk_usb_bootmode_command(uint8_t *data, uint8_t length)
{
  (void)data;
  (void)length;
}
#endif
