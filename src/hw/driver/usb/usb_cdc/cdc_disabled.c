// SPDX-License-Identifier: GPL-3.0-or-later
#include "cdc_if.h"

bool cdcIfInit(void) { return true; }
uint32_t cdcIfAvailable(void) { return 0; }
uint8_t cdcIfRead(void) { return 0; }
uint32_t cdcIfGetBaud(void) { return 115200; }
uint32_t cdcIfWrite(uint8_t *data, uint32_t length) { (void)data; (void)length; return 0; }
bool cdcIfIsConnected(void) { return false; }
uint8_t cdcIfGetType(void) { return 0; }
