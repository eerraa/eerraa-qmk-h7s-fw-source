// SPDX-License-Identifier: GPL-3.0-or-later
// CDC 1.2 / PSTN 1.2 ACM over the project's controller port.
#include "cdc_acm.h"
#include "usb_control.h"
#include "usb_device.h"
#include "usb_descriptors.h"
#include "usb_port.h"
#include "usb_hid/hid_port.h"
#include "qbuffer.h"
#include <string.h>

enum { CDC_NOTIFY = 0x82, CDC_TX = 0x83, CDC_RX = 0x03, CDC_RING_BYTES = 2048 };
static uint8_t rx_store[CDC_RING_BYTES], tx_store[CDC_RING_BYTES];
static qbuffer_t receive_queue, transmit_queue;
static uint8_t receive_packet[512] __attribute__((aligned(4)));
static uint8_t transmit_packet[2048] __attribute__((aligned(4)));
static uint8_t line_reply[8] __attribute__((aligned(4)));
static uint8_t notification[12] __attribute__((aligned(4)));
static uint32_t baud = 115200, session;
static uint8_t format, parity, bits = 8, type, opened;
static uint16_t packet_size, transmit_length;
static bool running, dtr, rx_armed, tx_armed, notify_armed, notify_dirty;
static enum { TX_EMPTY, TX_DATA, TX_ZLP } transmit_phase;
static const uint8_t endpoints[] = {CDC_NOTIFY, CDC_TX, CDC_RX};
static bool release(uint8_t interface, usb_class_transition_t transition);

static bool activeSession(void)
{
  return running && usb_device.configured && !usb_device.suspended &&
         !usb_device.reset_pending && !usb_device.retained;
}

static void resetStorage(void)
{
  qbufferCreate(&receive_queue, rx_store, sizeof(rx_store));
  qbufferCreate(&transmit_queue, tx_store, sizeof(tx_store));
  baud = 115200;
  format = parity = type = 0;
  bits = 8;
  dtr = rx_armed = tx_armed = notify_armed = notify_dirty = false;
  transmit_phase = TX_EMPTY;
  transmit_length = 0;
  ++session;
}

bool cdcIfInit(void)
{
  uint32_t mask = usbHidLock();
  bool available = !running && !opened;
  if (available) resetStorage();
  usbHidUnlock(mask);
  return available;
}

// Per CLI pass: one lock-free attempt inside the settle bracket (a ring
// recreated between the index loads fails it), then at most one locked read.
uint32_t cdcIfAvailable(void)
{
  uint32_t seq = usbHidSnapshotBegin();
  uint32_t count = qbufferAvailable(&receive_queue);
  if (usbHidSnapshotValid(seq)) return count;
  uint32_t mask = usbHidLock();
  count = qbufferAvailable(&receive_queue);
  usbHidUnlock(mask);
  return count;
}

uint8_t cdcIfRead(void)
{
  uint8_t value = 0;
  uint32_t mask = usbHidLock();
  qbufferRead(&receive_queue, &value, 1);
  usbHidUnlock(mask);
  return value;
}

uint32_t cdcIfGetBaud(void) { return baud; }
uint8_t cdcIfGetType(void) { return type; }

// Per main-loop pass: same bracket shape as cdcIfAvailable.
bool cdcIfIsConnected(void)
{
  uint32_t seq = usbHidSnapshotBegin();
  bool connected = dtr && activeSession();
  if (usbHidSnapshotValid(seq)) return connected;
  uint32_t mask = usbHidLock();
  connected = dtr && activeSession();
  usbHidUnlock(mask);
  return connected;
}

uint32_t cdcIfWrite(uint8_t *data, uint32_t length)
{
  if (!data) return 0;
  // One word: any value is the session at some instant, and every chunk
  // re-tests it under the lock.
  uint32_t generation = __atomic_load_n(&session, __ATOMIC_RELAXED);
  uint32_t accepted = 0, started = millis();
  while (accepted < length) {
    uint32_t mask = usbHidLock();
    if (generation != session || !dtr || !activeSession()) {
      usbHidUnlock(mask);
      break;
    }
    uint32_t space = CDC_RING_BYTES - 1 - qbufferAvailable(&transmit_queue);
    uint32_t count = length - accepted;
    if (count > space) count = space;
    if (count > 64) count = 64;
    if (count && qbufferWrite(&transmit_queue, data + accepted, count)) accepted += count;
    usbHidUnlock(mask);
    if ((uint32_t)(millis() - started) >= 100) break;
    if (!count) {
      if (mask || __get_IPSR()) break;
      delay(1);
    }
  }
  return accepted;
}

static bool armReceive(void)
{
  if (rx_armed || !(opened & 4) ||
      CDC_RING_BYTES - 1 - qbufferAvailable(&receive_queue) < packet_size) return true;
  rx_armed = usbPortReceive(CDC_RX, receive_packet, packet_size, sizeof(receive_packet));
  return rx_armed;
}

static bool armTransmit(void)
{
  if (tx_armed || transmit_phase == TX_EMPTY) return true;
  tx_armed = usbPortTransmitBuffered(CDC_TX, transmit_packet,
                                    transmit_phase == TX_ZLP ? 0 : transmit_length);
  return tx_armed;
}

// Reads only; PRIMASK clear in the settle. While the session is active: an
// unarmed reception with room for a packet, a pending SERIAL_STATE, or TX
// work with no armed transfer (contract S3).
bool usbCdcFrameWanted(void)
{
  if (!activeSession()) return false;
  return (!rx_armed && (opened & 4) &&
          CDC_RING_BYTES - 1 - qbufferAvailable(&receive_queue) >= packet_size) ||
         (!notify_armed && notify_dirty) ||
         (!tx_armed && (transmit_phase != TX_EMPTY || qbufferAvailable(&transmit_queue)));
}

static void serviceBody(void)
{
  // USB IRQ owns this state; the SPSC rings allow a bulk copy without masking input IRQs.
  // Each physical gate is checked before its copy: a gated owner may still
  // retain the immutable active packet.
  if (activeSession()) {
    if (usbDeviceArmable(CDC_RX)) armReceive();
    if (!notify_armed && notify_dirty && usbDeviceArmable(CDC_NOTIFY)) {
      const uint8_t header[8] = {0xa1, 0x20, 0, 0, 3, 0, 2, 0};
      memcpy(notification, header, sizeof(header));
      notification[8] = dtr ? 3 : 0;
      notification[9] = 0;
      notify_armed = usbPortTransmitBuffered(CDC_NOTIFY, notification, 10);
      if (notify_armed) notify_dirty = false;
    }
    if (!usbDeviceArmable(CDC_TX)) return;
    if (transmit_phase == TX_EMPTY) {
      _Static_assert(sizeof(transmit_packet) >= CDC_RING_BYTES - 1,
                     "CDC transfer buffer must hold the ring's accepted prefix");
      uint32_t count = qbufferAvailable(&transmit_queue);
      if (count && qbufferRead(&transmit_queue, transmit_packet, count)) {
        transmit_length = (uint16_t)count;
        transmit_phase = TX_DATA;
      }
    }
    armTransmit();
  }
}

// Every path of the SOF-time service settles its own demand.
static void service(void)
{
  serviceBody();
  usbPortSofDemand(USB_SOF_CDC, usbCdcFrameWanted());
}

void usbCdcInComplete(uint8_t endpoint)
{
  if (!activeSession()) return;
  if (endpoint == (CDC_NOTIFY & 15)) {
    notify_armed = false;
  } else if (endpoint == (CDC_TX & 15) && tx_armed) {
    tx_armed = false;
    if (transmit_phase == TX_DATA && transmit_length % packet_size == 0) {
      transmit_phase = TX_ZLP;
      armTransmit();
    } else {
      transmit_phase = TX_EMPTY;
      transmit_length = 0;
    }
  }
}

bool usbCdcOutComplete(uint8_t endpoint, uint32_t length)
{
  if (!activeSession() || endpoint != CDC_RX || !rx_armed) return false;
  rx_armed = false;
  if (length > packet_size || !qbufferWrite(&receive_queue, receive_packet, length)) {
    usbDeviceFault(USB_FAULT_RECEIVE, CDC_RX, 1);
    return false;
  }
  armReceive();
  return true;
}

static bool openEndpoints(uint8_t selection)
{
  for (unsigned i = 0; i < 3; ++i) {
    if (!(selection & (1U << i))) continue;
    opened |= 1U << i;
    bool ok = i ? usbPortOpenBulk(endpoints[i], packet_size) : usbPortOpen(CDC_NOTIFY, USB_CDC_NOTIFY_PACKET);
    if (!ok || !usbPortHalt(endpoints[i], false)) return false;
  }
  return true;
}

static void retire(void)
{
  if (running) ++session;
  running = false;
  usbPortSofDemand(USB_SOF_CDC, false);
}

static uint8_t endpointSelection(uint8_t interface)
{
  return interface == USB_CLASS_ALL_INTERFACES ? 7 :
         interface == 3 ? 1 : interface == 4 ? 6 : 0;
}

static uint32_t scope(uint8_t interface, bool opened_only)
{
  uint8_t selection = endpointSelection(interface);
  if (opened_only) selection &= opened;
  uint32_t result = 0U;
  for (unsigned i = 0U; i < 3U; ++i)
    if (selection & (1U << i)) result |= usbEndpointMask(endpoints[i]);
  return result;
}

static bool release(uint8_t interface, usb_class_transition_t transition)
{
  uint8_t selection = endpointSelection(interface);
  if (!selection || (transition != USB_CLASS_FRESH && transition != USB_CLASS_DEFAULTS))
    return false;
  opened &= ~selection;
  if (transition == USB_CLASS_FRESH && !opened) resetStorage();
  return true;
}

static bool open(uint8_t interface, usb_class_transition_t transition)
{
  uint8_t selection = endpointSelection(interface);
  if (!selection || (transition != USB_CLASS_FRESH && transition != USB_CLASS_DEFAULTS))
    return false;
  if (transition == USB_CLASS_FRESH) {
    if (interface != USB_CLASS_ALL_INTERFACES || opened) return false;
    resetStorage();
    packet_size = usb_device.high_speed ? USB_CDC_BULK_HS_PACKET : USB_CDC_BULK_FS_PACKET;
  }
  return openEndpoints(selection);
}

static bool restart(uint8_t interface, usb_class_transition_t transition)
{
  uint8_t selection = endpointSelection(interface);
  if (!selection || (opened & selection) != selection) return false;
  if (transition == USB_CLASS_FRESH) {
    if (interface != USB_CLASS_ALL_INTERFACES || !armReceive()) return false;
    running = true;
    return true;
  }
  if (transition != USB_CLASS_DEFAULTS) return false;
  if (selection & 1) {
    if (notify_armed && !usbPortTransmitBuffered(CDC_NOTIFY, notification, 10)) return false;
  }
  if (selection & 2) {
    tx_armed = false;
    if (!armTransmit()) return false;
  }
  if (selection & 4) {
    rx_armed = false;
    if (!armReceive()) return false;
  }
  return true;
}

static bool control(const usb_setup_t *request)
{
  if (!running || request->index != 3 ||
      (request->value && request->request != 0x22)) return false;
  if (request->type == 0x21 && request->request == 0x20 && request->length == 7)
    return usbControlReceive(7);
  if (request->type == 0xa1 && request->request == 0x21 && request->length == 7) {
    uint32_t reported = baud | type;
    for (unsigned i = 0; i < 4; ++i) line_reply[i] = (uint8_t)(reported >> (8 * i));
    line_reply[4] = format; line_reply[5] = parity; line_reply[6] = bits;
    return usbControlReply(line_reply, 7);
  }
  if (request->type == 0x21 && request->request == 0x22 &&
      !request->length && !(request->value & ~3U)) {
    bool next = (request->value & 1) != 0;
    if (next != dtr) notify_dirty = true;
    dtr = next;
    return usbControlStatus();
  }
  if (request->type == 0x21 && !request->request && request->length <= 64)
    return request->length ? usbControlReceive(request->length) : usbControlStatus();
  if (request->type == 0xa1 && request->request == 1)
    return usbControlReply(line_reply, 0);
  return false;
}

static bool received(const usb_setup_t *request, const uint8_t *data, uint16_t length)
{
  if (!running || request->type != 0x21 || request->index != 3 || request->value) return false;
  if (!request->request) return length == request->length;
  if (request->request != 0x20 || length != 7) return false;
  uint32_t requested = 0;
  for (unsigned i = 0; i < 4; ++i) requested |= (uint32_t)data[i] << (8 * i);
  baud = requested / 10 * 10;
  type = baud == 115200 ? 1 : 0;
  format = data[4]; parity = data[5]; bits = data[6];
  return true;
}

const usb_class_t usb_cdc_class = {
  control, received, service, open, restart, scope, retire, release
};
