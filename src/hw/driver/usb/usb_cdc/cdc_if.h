#pragma once
// CDC interface API behind src/hw/driver/cdc.c, independent of the USB stack.
#include <stdbool.h>
#include <stdint.h>

bool     cdcIfInit(void);
uint32_t cdcIfAvailable(void);
uint8_t  cdcIfRead(void);
uint32_t cdcIfGetBaud(void);
uint32_t cdcIfWrite(uint8_t *p_data, uint32_t length);
bool     cdcIfIsConnected(void);
uint8_t  cdcIfGetType(void);
