// SPDX-License-Identifier: GPL-3.0-or-later
#include "hid_port.h"
#include "usb_device.h"
#include "usb_control.h"
#include "usb_descriptors.h"
#include "usb_port.h"

static uint8_t opened;
static uint8_t protocol = 1;
static uint8_t idle;
static uint8_t control_byte[4] __attribute__((aligned(4)));

static bool openEndpoints(uint8_t selection)
{
  for (unsigned i = 0; i < 4; ++i) {
    if (!(selection & (1U << i))) continue;
    const usb_endpoint_plan_t *ep = &usb_hid_endpoints[i];
    // A failed controller open may already have touched the endpoint descriptor.
    opened |= 1U << i;
    if (!usbPortOpen(ep->address, ep->packet_size) || !usbPortHalt(ep->address, false))
      return false;
  }
  return true;
}

static uint8_t endpointSelection(uint8_t interface)
{
  uint8_t selection = 0;
  for (unsigned i = 0; i < 4; ++i)
    if (interface == USB_CLASS_ALL_INTERFACES || interface == usb_hid_endpoints[i].interface)
      selection |= 1U << i;
  return selection;
}

static uint32_t hidScope(uint8_t interface, bool opened_only)
{
  uint8_t selection = endpointSelection(interface);
  if (opened_only) selection &= opened;
  uint32_t scope = 0U;
  for (unsigned i = 0U; i < 4U; ++i)
    if (selection & (1U << i)) scope |= usbEndpointMask(usb_hid_endpoints[i].address);
  return scope;
}

static bool hidRelease(uint8_t interface, usb_class_transition_t transition)
{
  uint8_t selection = endpointSelection(interface);
  if (!selection || (transition != USB_CLASS_FRESH && transition != USB_CLASS_DEFAULTS))
    return false;
  opened &= ~selection;
  // A scoped failure must not clear buffers still owned by another interface.
  if (transition == USB_CLASS_FRESH && !opened) usbHidTransportReset();
  return true;
}

static bool hidOpen(uint8_t interface, usb_class_transition_t transition)
{
  uint8_t selection = endpointSelection(interface);
  if (!selection || (transition != USB_CLASS_FRESH && transition != USB_CLASS_DEFAULTS) ||
      (transition == USB_CLASS_FRESH && (interface != USB_CLASS_ALL_INTERFACES || opened)))
    return false;
  if (interface == USB_CLASS_ALL_INTERFACES) {
    protocol = 1;
    idle = 0;
  }
  return openEndpoints(selection);
}

static bool hidRestart(uint8_t interface, usb_class_transition_t transition)
{
  uint8_t selection = endpointSelection(interface);
  if (!selection || (opened & selection) != selection) return false;
  if (transition == USB_CLASS_FRESH) {
    if (interface != USB_CLASS_ALL_INTERFACES) return false;
    usbHidTransportOpen();
    return usbHidTransportArmVia();
  }
  if (transition != USB_CLASS_DEFAULTS) return false;
  for (unsigned i = 0; i < 4; ++i)
    if ((selection & (1U << i)) &&
        !usbHidTransportRestartEndpointLocked(usb_hid_endpoints[i].address)) return false;
  return true;
}

static bool hidControl(const usb_setup_t *r)
{
  unsigned interface = r->index;
  if (interface >= 3) return false;
  if (r->type == 0xa1 && r->request == 3 && !r->value && r->length == 1) {
    control_byte[0] = interface ? 1 : protocol;
    return usbControlReply(control_byte, 1);
  }
  if (r->type == 0x21 && r->request == 11 && r->value <= 1 && !r->length) {
    if (!interface && protocol != r->value) {
      uint32_t mask = usbHidLock();
      protocol = (uint8_t)r->value;
      usbHidTransportProtocolChangedLocked();
      usbHidUnlock(mask);
    }
    return usbControlStatus();
  }
  if (r->type == 0xa1 && r->request == 2 && r->value <= 255 && r->length == 1) {
    control_byte[0] = idle;
    return usbControlReply(control_byte, 1);
  }
  if (r->type == 0x21 && r->request == 10 && !r->length) {
    idle = (uint8_t)(r->value >> 8);
    return usbControlStatus();
  }
  if (r->type == 0x21 && r->request == 9 && !interface &&
      r->value == 0x200 && r->length == 1) return usbControlReceive(1);
  usbHidTransportCountInvalidControl();
  return false;
}

static bool hidReceived(const usb_setup_t *r, const uint8_t *data, uint16_t length)
{
  if (r->type != 0x21 || r->request != 9 || r->index || r->value != 0x200 || length != 1) {
    usbHidTransportCountInvalidControl();
    return false;
  }
  usbHidSetStatusLed(data[0]);
  return true;
}

const usb_class_t usb_hid_class = {
  hidControl, hidReceived, usbHidTransportSof, hidOpen, hidRestart, hidScope,
  usbHidTransportRetireLocked, hidRelease
};

usb_hid_link_t usbHidPortLink(void)
{
  return (usb_hid_link_t)(usb_device.configured + 2U * usb_device.suspended);
}

void usbHidPortFrameDemand(bool wanted)
{
  usbPortSofDemand(USB_SOF_HID, wanted);
}

bool usbHidPortTransmit(uint8_t endpoint, uint8_t *data, uint32_t length)
{
  // The transport has validated the link under PRIMASK immediately before this call.
  return usbPortTransmit(endpoint, data, (uint16_t)length);
}

bool usbHidPortReceive(uint8_t endpoint, uint8_t *data, uint32_t length)
{
  if (!(opened & 4) || endpoint != HID_VIA_EP_OUT || usb_device.reset_pending || usb_device.suspended)
    return false;
  // The transport's report buffer is exactly one packet: its whole allocation.
  return usbPortReceive(endpoint, data, (uint16_t)length, (uint16_t)length);
}

bool usbHidPortArmable(uint8_t endpoint) { return usbDeviceArmable(endpoint); }

uint8_t usbHidPortKeyboardProtocol(void) { return protocol; }

bool usbHidPortKeyboardPolling(bool *high_speed, uint8_t *interval)
{
  if (!(opened & 1) || !usb_device.configured || usb_device.reset_pending) return false;
  *high_speed = usb_device.high_speed;
  *interval = usb_device.high_speed ? usb_device.interval : 1;
  return true;
}

bool usbHidRequestRemoteWakeFromInput(void) { return usbPortWake(); }
