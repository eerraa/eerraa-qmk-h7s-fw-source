#pragma once
#include "usb_class.h"
#include "cdc_if.h"

extern const usb_class_t usb_cdc_class;
void usbCdcInComplete(uint8_t endpoint);
bool usbCdcOutComplete(uint8_t endpoint, uint32_t length);
// The CDC SOF-demand predicate (contract S3); any context, reads only.
bool usbCdcFrameWanted(void);
