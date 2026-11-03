#pragma once
// Seam between the stack-neutral HID transport (hid_transport.c) and the USB stack
// binding that owns endpoints, control requests and descriptors. The transport
// entry points may run in any interrupt or thread context: shared state is
// serialized only by usbHidLock(), which masks every interrupt.
#include "hw_def.h"
#include "hid_transport.h"

// Endpoint plan of the three HID interfaces; addresses and sizes are host-visible.
#define HID_EPIN_ADDR                                   0x81U
#define HID_EPIN_SIZE                                   64U
#define HID_VIA_EP_IN                                   0x84U
#define HID_VIA_EP_OUT                                  0x04U
#define HID_EXK_EP_IN                                   0x85U
#define HID_EXK_EP_SIZE                                 8U
#define HID_KEYBOARD_REPORT_SIZE (HW_KEYS_PRESS_MAX + 2U)
#define HID_BOOT_KEYBOARD_REPORT_SIZE 8U

// Provided by the stack binding: the SOF-demand settle (contract S1). Called
// with PRIMASK clear; it masks only for the owner update.
void usbHidPortSettle(void);

static inline uint32_t usbHidLock(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

// Restores PRIMASK and never settles. Only usbHidUnlock and the settle's own
// window use it, so no other section can skip its settle and none recurses.
static inline void usbHidRestore(uint32_t primask)
{
  __set_PRIMASK(primask);
}

static inline void usbHidUnlock(uint32_t primask)
{
  usbHidRestore(primask);
  // The outermost release outside the OTG ISR and PendSV settles after the
  // restore, so the section's masked span does not grow; those two contexts
  // settle once at their own end, after every arm (Linux local_bh_enable).
  if (primask == 0U) {
    uint32_t context = __get_IPSR();
    if (context != (uint32_t)(OTG_HS_IRQn + 16) && context != (uint32_t)(PendSV_IRQn + 16))
      usbHidPortSettle();
  }
}

// Thread-mode snapshot bracket: every settle bumps usb_settle_seq, and every
// interrupt-context store to a bracketed field is followed by a settle before
// that context returns, so equal values bracket one coherent state.
extern uint32_t usb_settle_seq;
static inline uint32_t usbHidSnapshotBegin(void)
{
  uint32_t seq = __atomic_load_n(&usb_settle_seq, __ATOMIC_RELAXED);
  __atomic_signal_fence(__ATOMIC_SEQ_CST);
  return seq;
}

static inline bool usbHidSnapshotValid(uint32_t seq)
{
  __atomic_signal_fence(__ATOMIC_SEQ_CST);
  return __atomic_load_n(&usb_settle_seq, __ATOMIC_RELAXED) == seq;
}

typedef enum {
  USB_HID_LINK_IDLE = 0,    // neither configured nor suspended
  USB_HID_LINK_CONFIGURED,  // configured and running
  USB_HID_LINK_SUSPENDED,   // bus suspended before any configuration
  USB_HID_LINK_SLEEPING,    // suspended by a host that had configured the device
} usb_hid_link_t;

// Provided by the stack binding.
usb_hid_link_t usbHidPortLink(void);
// False while a lifecycle gate owns the endpoint: checked before any kick
// can replace the queue's active packet.
bool usbHidPortArmable(uint8_t ep_addr);
bool usbHidPortTransmit(uint8_t ep_addr, uint8_t *data, uint32_t length);
// Arms only an open OUT endpoint; false leaves the host NAKed.
bool usbHidPortReceive(uint8_t ep_addr, uint8_t *data, uint32_t length);
// HID SOF demand: raised or lowered by the transport's own SOF-time service and retirement.
void usbHidPortFrameDemand(bool wanted);
// 0 is Boot protocol and 1 Report protocol, as the keyboard interface's SET_PROTOCOL left it.
uint8_t usbHidPortKeyboardProtocol(void);
// False unless the keyboard IN endpoint is open at Full or High Speed.
bool usbHidPortKeyboardPolling(bool *high_speed, uint8_t *interval);

// Provided by the transport for the stack binding.
bool usbHidAdmissionOpen(void);
uint32_t usbHidTransportGeneration(void);
void usbHidTransportOpen(void);
void usbHidTransportRetireLocked(void);
// Only after every endpoint has released its buffers: this also clears the armed payloads.
void usbHidTransportReset(void);
// After a successful endpoint close/reopen: resend active IN without consuming the FIFO.
bool usbHidTransportRestartEndpointLocked(uint8_t address);
bool usbHidTransportArmVia(void);
void usbHidTransportInComplete(uint8_t ep_num);
bool usbHidTransportOutComplete(uint8_t ep_num, uint32_t length);
void usbHidTransportSof(void);
// True while a later frame could arm work: any context, PRIMASK clear, reads only.
bool usbHidTransportFrameWanted(void);
// Call after the keyboard interface's protocol changed.
void usbHidTransportProtocolChangedLocked(void);
void usbHidTransportCountInvalidControl(void);
