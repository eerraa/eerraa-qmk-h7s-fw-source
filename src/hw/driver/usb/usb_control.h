#pragma once
#include "usb_class.h"

void usbControlReset(void);
// The shared engine released the early-status EP0 IN owner.
void usbControlRetired(void);
void usbControlSetup(const uint8_t bytes[8]);
void usbControlInComplete(void);
// A completed IN reconciled before a SETUP replaces the request: only the
// status stage's side effects (address, test mode) are still delivered.
void usbControlStatusComplete(void);
void usbControlOutComplete(uint16_t length);
bool usbControlReply(const uint8_t *data, uint16_t length);
bool usbControlReceive(uint16_t length);
bool usbControlStatus(void);
void usbControlStall(void);
