// SPDX-License-Identifier: GPL-3.0-or-later
// USB 2.0 sections 8.5.3 and 9.4: packet progress and deferred status actions.
#include "usb_control.h"
#include "usb_device.h"
#include "usb_descriptors.h"
#include "usb_port.h"
#include "usb_hid/hid_port.h"
#include <string.h>

// OPERATION: a lifecycle operation admitted the request; only its COMMIT
// publishes the status or protocol STALL.
typedef enum { CONTROL_IDLE, CONTROL_IN, CONTROL_OUT, CONTROL_ACK_IN,
               CONTROL_ACK_OUT, CONTROL_RETIRING, CONTROL_OPERATION } control_phase_t;
static struct {
  usb_setup_t request;
  control_phase_t phase;
  const uint8_t *source;
  uint16_t remaining, packet, expected;
  bool zlp;
  uint8_t pending_address, pending_test;
  bool set_address;
} control;
// EP0 always owns this whole allocation: a bounded status OUT payload lands
// inside it and becomes a protocol STALL, never a parser fault.
static uint8_t receive_packet[64] __attribute__((aligned(4)));
static uint8_t small_reply[4] __attribute__((aligned(4)));

void usbControlReset(void)
{
  control.phase = CONTROL_IDLE;
  control.set_address = false;
  control.pending_test = 0;
}

void usbControlStall(void)
{
  usbControlReset();
  bool out = usbPortHalt(0, true);
  bool in = usbPortHalt(0x80, true);
  if (!out || !in) usbDeviceFault(USB_FAULT_CONTROL, 0, 1);
}

static bool sendPacket(void)
{
  control.packet = control.remaining < USB_EP0_PACKET ? control.remaining : USB_EP0_PACKET;
  if (usbPortTransmit(0x80, control.source, control.packet)) return true;
  usbControlStall();
  return false;
}

bool usbControlReply(const uint8_t *data, uint16_t length)
{
  uint16_t requested = control.request.length;
  control.remaining = length < requested ? length : requested;
  control.source = data;
  control.zlp = control.remaining < requested && (control.remaining % USB_EP0_PACKET) == 0;
  control.phase = CONTROL_IN;
  if (!requested) return usbControlStatus();
  if (!control.remaining) control.zlp = false;
  if (!usbPortReceive(0, receive_packet, 0, sizeof(receive_packet))) return false;
  return sendPacket();
}

bool usbControlReceive(uint16_t length)
{
  if (length > sizeof(receive_packet) || length != control.request.length) return false;
  control.expected = length;
  control.phase = CONTROL_OUT;
  return usbPortReceive(0, receive_packet, length, sizeof(receive_packet));
}

bool usbControlStatus(void)
{
  control.phase = CONTROL_ACK_IN;
  return usbPortTransmit(0x80, small_reply, 0);
}

static bool replyByte(uint8_t value)
{
  small_reply[0] = value;
  return usbControlReply(small_reply, 1);
}

static bool standardRequest(void)
{
  const usb_setup_t *r = &control.request;
  uint8_t recipient = r->type & 31U;
  bool addressed = usb_device.address != 0;
  bool configured = usb_device.configured;
  switch (r->request) {
    case 6: {
      usb_descriptor_t descriptor;
      bool found = false;
      if (r->type == 0x80)
        found = usbDescriptorLookup(r->value >> 8, r->value & 255, r->index,
                                    usb_device.high_speed, usb_device.dual_speed,
                                    usb_device.interval, &descriptor);
      else if (r->type == 0x81 && r->index < 3 && !(r->value & 255))
        found = usbHidDescriptorLookup(r->value >> 8, r->index, &descriptor);
      return found && usbControlReply(descriptor.bytes, descriptor.length);
    }
    case 5:
      if (r->type || r->index || r->length || r->value > 127 || configured) return false;
      // DWC2 requires DAD programming before the status IN is armed.
      if (!usbPortAddress((uint8_t)r->value)) return false;
      control.pending_address = (uint8_t)r->value;
      control.set_address = true;
      return usbControlStatus();
    case 8:
      return r->type == 0x80 && !r->value && !r->index && r->length == 1 &&
             addressed && replyByte(configured ? 1 : 0);
    case 9:
      if (r->type || r->index || r->length || r->value > 1 || !addressed ||
          !usbDeviceConfigure((uint8_t)r->value)) return false;
      control.phase = CONTROL_OPERATION;
      return true;
    case 0: {
      if (r->type != (0x80U | recipient) || r->value || r->length != 2 || !addressed) return false;
      small_reply[0] = small_reply[1] = 0;
      if (!recipient) {
        if (r->index) return false;
        small_reply[0] = 1U | (usb_device.wake_enabled ? 2U : 0U);
      } else if (recipient == 1) {
        if (!configured || r->index >= usb_interface_count) return false;
      } else if (recipient == 2) {
        if (!usbDeviceEndpoint(r->index)) return false;
        // EP0 protocol stalls expire at SETUP even when HAL's cache still says stalled.
        small_reply[0] = (r->index & 15) && usbPortHalted((uint8_t)r->index) ? 1 : 0;
      } else return false;
      return usbControlReply(small_reply, 2);
    }
    case 1:
    case 3: {
      bool set = r->request == 3;
      if (r->type != recipient || r->length) return false;
      if (recipient == 0 && r->value == 1 && !r->index && addressed) {
        usb_device.wake_enabled = set;
        return usbControlStatus();
      }
      if (recipient == 0 && r->value == 2 && set && usb_device.high_speed &&
          !(r->index & 255) && (r->index >> 8) >= 1 && (r->index >> 8) <= 4) {
        control.pending_test = (uint8_t)(r->index >> 8);
        return usbControlStatus();
      }
      if (recipient == 2 && !r->value && addressed && usbDeviceEndpoint(r->index)) {
        // EP0's protocol stall is transient; its optional halt feature is not latched.
        if (!(r->index & 15)) return usbControlStatus();
        return usbPortHalt((uint8_t)r->index, set) && usbControlStatus();
      }
      return false;
    }
    case 10:
      return r->type == 0x81 && !r->value && r->index < usb_interface_count && r->length == 1 &&
             configured && replyByte(0);
    case 11:
      if (r->type != 1 || r->value || r->index >= usb_interface_count || r->length ||
          !configured || !usbDeviceSetInterface((uint8_t)r->index)) return false;
      control.phase = CONTROL_OPERATION;
      return true;
    default:
      // SET_DESCRIPTOR is optional; SYNCH_FRAME has no isochronous owner here.
      return false;
  }
}

// Called only by lifecycle dispatch, after every EP0 owner released storage.
void usbControlSetup(const uint8_t bytes[8])
{
  usbControlReset();
  if (usb_device.reset_pending) return;
  control.request = (usb_setup_t){
    bytes[0], bytes[1], (uint16_t)(bytes[2] | bytes[3] << 8),
    (uint16_t)(bytes[4] | bytes[5] << 8), (uint16_t)(bytes[6] | bytes[7] << 8)
  };
  bool accepted = false;
  if (!(control.request.type & 0x60)) accepted = standardRequest();
  else if ((control.request.type & 0x7f) == 0x21 && usb_device.configured &&
           control.request.index < usb_interface_count)
    accepted = usb_interface_owners[control.request.index]->control(&control.request);
  if (!accepted) usbControlStall();
}

void usbControlRetired(void)
{
  if (control.phase == CONTROL_RETIRING) usbControlReset();
}

void usbControlInComplete(void)
{
  if (control.phase == CONTROL_ACK_IN) {
    bool address = control.set_address;
    uint8_t value = control.pending_address, test = control.pending_test;
    usbControlReset();
    if (address) {
      usb_device.address = value;
      usb_device.host_seen |= value != 0;
    }
    if (test) usbPortTest(test);
  } else if (control.phase == CONTROL_IN) {
    control.source += control.packet;
    control.remaining -= control.packet;
    if (control.remaining) sendPacket();
    else if (control.zlp) { control.zlp = false; sendPacket(); }
    else control.phase = CONTROL_ACK_OUT;
  }
}

void usbControlStatusComplete(void)
{
  if (control.phase == CONTROL_ACK_IN) usbControlInComplete();
}

void usbControlOutComplete(uint16_t length)
{
  if (control.phase == CONTROL_IN || control.phase == CONTROL_ACK_OUT) {
    if (length) { usbControlStall(); return; }
    bool pending_in = control.phase == CONTROL_IN;
    usbControlReset();
    if (pending_in) {
      // Early status: the shared engine retires the old IN storage; SETUP
      // dispatch stays held until it is released (or, failed, until reset).
      control.phase = CONTROL_RETIRING;
      (void)usbPortRetireControlIn();
    }
  } else if (control.phase == CONTROL_OUT) {
    if (length == control.expected &&
        usb_interface_owners[control.request.index]->received(&control.request, receive_packet, length)) {
      if (!usbControlStatus()) usbControlStall();
    } else usbControlStall();
  }
}
