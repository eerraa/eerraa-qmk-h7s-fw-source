#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "usb_lifecycle.h"

typedef enum {
  USB_SHUTDOWN_NONE, USB_SHUTDOWN_PENDING, USB_SHUTDOWN_OK, USB_SHUTDOWN_FAILED
} usb_shutdown_t;

// Refused while started, including a pending shutdown.
bool usbPortStart(bool dual_speed);
// Authorized shutdown admission (contract_usb section 6), inside the caller's
// short commit: like a raw reset it retires every producer, control and owner
// at once, soft-detaches, and the PendSV join proves cleanup or fails. Never
// waits; repeated calls cannot renew it.
void usbPortShutdown(uint32_t anchor_us);
usb_shutdown_t usbPortShutdownResult(void);
// The original wall deadline's escape: a pending shutdown fails with the
// independent safe detach and interrupt isolation. Returns the terminal.
usb_shutdown_t usbPortShutdownEscape(void);
// Endpoint commands are refused while a lifecycle gate, reset join or FIFO
// exclusion owns the endpoint; only the executor's owner window may OPEN,
// default, restart or answer an admitted request (contract_usb section 6).
bool usbPortOpen(uint8_t address, uint16_t size);
bool usbPortOpenBulk(uint8_t address, uint16_t size);
bool usbPortTransmit(uint8_t address, const uint8_t *data, uint16_t length);
bool usbPortTransmitBuffered(uint8_t address, const uint8_t *data, uint16_t length);
// Capacity is the allocated buffer, the parser's memory bound; length is the transfer.
bool usbPortReceive(uint8_t address, uint8_t *data, uint16_t length, uint16_t capacity);
bool usbPortHalt(uint8_t address, bool halt);
bool usbPortHalted(uint8_t address);
bool usbPortAddress(uint8_t address);
bool usbPortTest(uint8_t selector);
bool usbPortWake(void);
bool usbPortSuspended(void);
// DSTS.FNSOF, the (micro)frame number of the last received SOF, or 0 before
// start. It advances whether or not the SOF interrupt is unmasked; one
// register read, no lock.
uint16_t usbPortFrameNumber(void);

// SOF demand (contract S1): one bit per frame-paced duty. The SOF interrupt is
// unmasked while any bit is set and masked only by the SOF interrupt itself.
enum { USB_SOF_SUSPEND = 1U << 0, USB_SOF_HID = 1U << 1, USB_SOF_CDC = 1U << 2 };
// A duty's SOF-time settle, retirement or the non-serviceable drop; raising
// clears a stale SOF flag before it unmasks. A release writes no register.
void usbPortSofDemand(uint8_t sources, bool wanted);
void usbPortIrq(void);
// Early status: retire EP0 IN through the shared engine; SETUP dispatch stays
// held until its storage is released.
bool usbPortRetireControlIn(void);

// Executor seams (PendSV, inside one transport-locked command transaction).
// The owner window's capabilities are checked at the action's own timestamp.
void usbPortOwner(const usb_lifecycle_action_t *owner, uint32_t now_us);
uint32_t usbPortTouched(void);
// Steps one endpoint owner as far as the slice allows: each Begin, in-place
// close or phase change is one transition, each unchanged FIFO/flush
// observation one poll read. PENDING leaves wait READY (budget spent), POLL
// (parked until the next tick) or EVENT (an interrupt-capable fact).
// A new owner, or one whose gate RESTART reopened, begins a full cycle.
usb_lifecycle_result_t usbPortQuiesce(uint8_t address, uint64_t operation, bool defaults,
                                      usb_lifecycle_slice_t *slice, usb_lifecycle_wait_t *wait);
bool usbPortOpenGate(uint8_t address, uint64_t operation);
// Clears the physical gate of every released owner of the operation.
bool usbPortReleaseGates(uint64_t operation);
// PendSV_Handler and SysTick_Handler entries.
void usbPortService(void);
void usbPortTick(void);
