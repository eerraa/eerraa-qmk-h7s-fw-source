#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "usb_class.h"

// Max packet sizes shared by the descriptors, endpoint opens and the TX FIFO
// plan. The CDC FIFOs are reserved in every build, so these always exist.
#define USB_EP0_PACKET           64U
#define USB_CDC_NOTIFY_PACKET    8U
#define USB_CDC_BULK_FS_PACKET   64U
#define USB_CDC_BULK_HS_PACKET   512U

typedef struct {
  const uint8_t *bytes;
  uint16_t length;
} usb_descriptor_t;

typedef struct {
  uint8_t address;
  uint8_t interface;
  uint16_t packet_size;
} usb_endpoint_plan_t;

extern const usb_endpoint_plan_t usb_hid_endpoints[4];
extern const uint8_t usb_interface_count;
extern const usb_class_t *const usb_interface_owners[];
extern void (*const usb_in_handlers[16])(uint8_t endpoint);
extern bool (*const usb_out_handlers[16])(uint8_t endpoint, uint32_t length);
void usbDescriptorsInit(const uint32_t uid[3]);
bool usbDescriptorLookup(uint8_t type, uint8_t index, uint16_t language,
                         bool high_speed, bool dual_speed, uint8_t interval,
                         usb_descriptor_t *result);
bool usbHidDescriptorLookup(uint8_t type, uint8_t interface, usb_descriptor_t *result);
