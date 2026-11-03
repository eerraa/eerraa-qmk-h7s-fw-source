// SPDX-License-Identifier: GPL-3.0-or-later
// USB 2.0 chapter 9 and HID 1.11; wire data is pinned by the host golden fixtures.
#include "hw_def.h"
#include "usb_descriptors.h"
#include "usb_hid/hid_port.h"
#if HW_USB_CDC
#include "usb_cdc/cdc_acm.h"
#endif
#include <string.h>

#define WORD(v) ((v) & 255), (((v) >> 8) & 255)
#define ALIGNED __attribute__((aligned(4)))
#define HID_DESCRIPTOR(n) 9, 0x21, 0x11, 1, 0, 1, 0x22, WORD(n)
#define INTERFACE(n, eps, sub, proto) 9, 4, n, 0, eps, 3, sub, proto, 0
#define ENDPOINT(a, size, period) 7, 5, a, 3, WORD(size), period
#if HW_USB_CDC
#define CONFIGURATION_BYTES 157
#define INTERFACE_COUNT 5
#define DEVICE_CLASS 0xef, 2, 1
#define CONFIGURATION_STRING 4
#define CDC_DESCRIPTORS(bulk) \
  ,8, 0x0b, 3, 2, 2, 2, 1, 0, \
  9, 4, 3, 0, 1, 2, 2, 1, 0, \
  5, 0x24, 0, 0x10, 1, 5, 0x24, 1, 0, 4, \
  4, 0x24, 2, 2, 5, 0x24, 6, 3, 4, ENDPOINT(0x82, USB_CDC_NOTIFY_PACKET, 16), \
  9, 4, 4, 0, 2, 0x0a, 0, 0, 0, \
  7, 5, 0x83, 2, WORD(bulk), 0, 7, 5, 0x03, 2, WORD(bulk), 0
#else
#define CONFIGURATION_BYTES 91
#define INTERFACE_COUNT 3
#define DEVICE_CLASS 0, 0, 0
#define CONFIGURATION_STRING 0
#define CDC_DESCRIPTORS(bulk)
#endif
#define CONFIGURATION(kind, period, bulk) \
  9, kind, WORD(CONFIGURATION_BYTES), INTERFACE_COUNT, 1, CONFIGURATION_STRING, 0xe0, 50, \
  INTERFACE(0, 1, 1, 1), HID_DESCRIPTOR(64), ENDPOINT(0x81, HID_EPIN_SIZE, period), \
  INTERFACE(1, 2, 0, 0), HID_DESCRIPTOR(34), \
  ENDPOINT(0x84, HID_VIA_EP_SIZE, period), ENDPOINT(0x04, HID_VIA_EP_SIZE, period), \
  INTERFACE(2, 1, 1, 0), HID_DESCRIPTOR(123), ENDPOINT(0x85, HID_EXK_EP_SIZE, period) \
  CDC_DESCRIPTORS(bulk)

static const uint8_t device[20] ALIGNED = {
  18, 1, 0, 2, DEVICE_CLASS, USB_EP0_PACKET, WORD(USB_VID), WORD(USB_PID), 0, 2, 1, 2, 3, 1
};
static const uint8_t qualifier[12] ALIGNED = {10, 6, 0, 2, DEVICE_CLASS, USB_EP0_PACKET, 1, 0};
static const uint8_t full_configurations[2][(CONFIGURATION_BYTES + 3) & ~3] ALIGNED = {
  {CONFIGURATION(2, 1, USB_CDC_BULK_FS_PACKET)}, {CONFIGURATION(7, 1, USB_CDC_BULK_FS_PACKET)}
};
static const uint8_t high_configurations[2][3][(CONFIGURATION_BYTES + 3) & ~3] ALIGNED = {
  {{CONFIGURATION(2, 1, USB_CDC_BULK_HS_PACKET)}, {CONFIGURATION(2, 2, USB_CDC_BULK_HS_PACKET)},
   {CONFIGURATION(2, 3, USB_CDC_BULK_HS_PACKET)}},
  {{CONFIGURATION(7, 1, USB_CDC_BULK_HS_PACKET)}, {CONFIGURATION(7, 2, USB_CDC_BULK_HS_PACKET)},
   {CONFIGURATION(7, 3, USB_CDC_BULK_HS_PACKET)}}
};
static const uint8_t hid_descriptors[3][12] ALIGNED = {
  {HID_DESCRIPTOR(64)}, {HID_DESCRIPTOR(34)}, {HID_DESCRIPTOR(123)}
};
static const uint8_t keyboard_report[64] ALIGNED = {
  0x05,0x01,0x09,0x06,0xa1,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,
  0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x01,0x75,0x08,0x81,0x03,0x95,0x05,0x75,0x01,
  0x05,0x08,0x19,0x01,0x29,0x05,0x91,0x02,0x95,0x01,0x75,0x03,0x91,0x03,0x95,HW_KEYS_PRESS_MAX,
  0x75,0x08,0x15,0x00,0x26,0xff,0x00,0x05,0x07,0x19,0x00,0x29,0xff,0x81,0x00,0xc0
};
static const uint8_t via_report[36] ALIGNED = {
  0x06,0x60,0xff,0x09,0x61,0xa1,0x01,0x09,0x62,0x15,0x00,0x26,0xff,0x00,0x95,0x20,
  0x75,0x08,0x81,0x02,0x09,0x63,0x15,0x00,0x26,0xff,0x00,0x95,0x20,0x75,0x08,0x91,0x02,0xc0
};
static const uint8_t extra_report[124] ALIGNED = {
  0x05,0x01,0x09,0x80,0xa1,0x01,0x85,0x03,0x19,0x01,0x2a,0xb7,0x00,0x15,0x01,0x26,
  0xb7,0x00,0x95,0x01,0x75,0x10,0x81,0x00,0xc0,0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,
  0x04,0x19,0x01,0x2a,0xa0,0x02,0x15,0x01,0x26,0xa0,0x02,0x95,0x01,0x75,0x10,0x81,
  0x00,0xc0,0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x02,0x09,0x01,0xa1,0x00,0x05,0x09,
  0x19,0x01,0x29,0x08,0x15,0x00,0x25,0x01,0x95,0x08,0x75,0x01,0x81,0x02,0x05,0x01,
  0x09,0x30,0x09,0x31,0x15,0x81,0x25,0x7f,0x95,0x02,0x75,0x08,0x81,0x06,0x09,0x38,
  0x15,0x81,0x25,0x7f,0x95,0x01,0x75,0x08,0x81,0x06,0x05,0x0c,0x0a,0x38,0x02,0x15,
  0x81,0x25,0x7f,0x95,0x01,0x75,0x08,0x81,0x06,0xc0,0xc0
};
const usb_endpoint_plan_t usb_hid_endpoints[4] = {
  {0x81, 0, HID_EPIN_SIZE}, {0x84, 1, HID_VIA_EP_SIZE}, {0x04, 1, HID_VIA_EP_SIZE},
  {0x85, 2, HID_EXK_EP_SIZE}
};
extern const usb_class_t usb_hid_class;
const uint8_t usb_interface_count = INTERFACE_COUNT;
const usb_class_t *const usb_interface_owners[] = {
  &usb_hid_class, &usb_hid_class, &usb_hid_class,
#if HW_USB_CDC
  &usb_cdc_class, &usb_cdc_class,
#endif
};
void (*const usb_in_handlers[16])(uint8_t) = {
  [1] = usbHidTransportInComplete, [4] = usbHidTransportInComplete, [5] = usbHidTransportInComplete,
#if HW_USB_CDC
  [2] = usbCdcInComplete, [3] = usbCdcInComplete,
#endif
};
bool (*const usb_out_handlers[16])(uint8_t, uint32_t) = {
  [4] = usbHidTransportOutComplete,
#if HW_USB_CDC
  [3] = usbCdcOutComplete,
#endif
};
#define STRING_BYTES(text) ((2U * sizeof(text) + 3U) & ~3U)
static struct {
  uint8_t language[4];
  uint8_t manufacturer[STRING_BYTES(KBD_MANUFACTURER)];
  uint8_t product[STRING_BYTES(KBD_NAME)];
  uint8_t serial[STRING_BYTES("H7_000000000")];
  uint8_t configuration[STRING_BYTES("HID-K Config")];
  uint8_t interface[STRING_BYTES("HID-K Interface")];
} string_storage ALIGNED;
static uint8_t *const strings[] = {
  string_storage.language, string_storage.manufacturer, string_storage.product,
  string_storage.serial, string_storage.configuration, string_storage.interface
};

_Static_assert(HW_KEYS_PRESS_MAX == 20, "keyboard wire contract");
_Static_assert(sizeof(KBD_MANUFACTURER) <= 63 && sizeof(KBD_NAME) <= 63, "USB string capacity");

static void encodeString(unsigned index, const char *text)
{
  size_t count = strlen(text);
  strings[index][0] = (uint8_t)(2 + 2 * count);
  strings[index][1] = 3;
  for (size_t i = 0; i < count; ++i) strings[index][2 + 2 * i] = (uint8_t)text[i];
}

void usbDescriptorsInit(const uint32_t uid[3])
{
  static const char digits[] = "0123456789ABCDEF";
  char serial[13] = "H7_000000000";
  uint32_t low = (uid[0] + uid[2]) & 0xfffffU;
  for (unsigned i = 0; i < 5; ++i) serial[7 - i] = digits[(low >> (i * 4)) & 15];
  for (unsigned i = 0; i < 4; ++i) serial[11 - i] = digits[(uid[1] >> (16 + i * 4)) & 15];
  memset(&string_storage, 0, sizeof(string_storage));
  strings[0][0] = 4; strings[0][1] = 3; strings[0][2] = 9; strings[0][3] = 4;
  encodeString(1, KBD_MANUFACTURER);
  encodeString(2, KBD_NAME);
  encodeString(3, serial);
  encodeString(4, "HID-K Config");
  encodeString(5, "HID-K Interface");
}

bool usbDescriptorLookup(uint8_t type, uint8_t index, uint16_t language,
                         bool high_speed, bool dual_speed, uint8_t interval,
                         usb_descriptor_t *result)
{
  if (type == 3) {
    if (index >= 6 || (index != 0 && language != 0x409)) return false;
    *result = (usb_descriptor_t){strings[index], strings[index][0]};
    return true;
  }
  if (index || language) return false;
  if (type == 1) *result = (usb_descriptor_t){device, 18};
  else if (type == 6 && dual_speed) *result = (usb_descriptor_t){qualifier, 10};
  else if (type == 2 || (type == 7 && dual_speed)) {
    bool selected_high = type == 7 ? !high_speed : high_speed;
    unsigned slot = selected_high && interval >= 1 && interval <= 3 ? interval - 1 : 0;
    *result = (usb_descriptor_t){selected_high ? high_configurations[type == 7][slot] :
                                               full_configurations[type == 7], CONFIGURATION_BYTES};
  } else return false;
  return true;
}

bool usbHidDescriptorLookup(uint8_t type, uint8_t interface, usb_descriptor_t *result)
{
  if (interface >= 3) return false;
  if (type == 0x21) *result = (usb_descriptor_t){hid_descriptors[interface], 9};
  else if (type == 0x22) {
    static const usb_descriptor_t reports[3] = {
      {keyboard_report, 64}, {via_report, 34}, {extra_report, 123}
    };
    *result = reports[interface];
  } else return false;
  return true;
}
