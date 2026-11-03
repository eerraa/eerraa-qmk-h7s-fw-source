#pragma once
#include "usb_class.h"
#include "usb_lifecycle.h"

typedef enum {
  USB_FAULT_NONE, USB_FAULT_INIT, USB_FAULT_OPEN, USB_FAULT_CLOSE,
  USB_FAULT_TRANSMIT, USB_FAULT_RECEIVE, USB_FAULT_STOP,
  USB_FAULT_CONTROL, USB_FAULT_FLUSH
} usb_fault_operation_t;

typedef struct {
  uint32_t count;
  usb_fault_operation_t operation;
  uint8_t endpoint;
  uint8_t cause;
} usb_fault_t;

typedef struct {
  bool configured, suspended, reset_pending, retained, high_speed, dual_speed;
  bool wake_enabled, host_seen;
  uint8_t address, interval;
  uint32_t epoch;
} usb_device_state_t;

extern usb_device_state_t usb_device;
// The controller's one lifecycle record; every access holds the transport lock.
extern usb_lifecycle_t usb_lifecycle;
extern const usb_class_t usb_hid_class;
bool usbDeviceStart(bool dual_speed, uint8_t interval);
// True admits the dispatched request into the lifecycle owner: its status or
// protocol STALL is then published only by that operation's COMMIT.
bool usbDeviceConfigure(uint8_t configuration);
bool usbDeviceSetInterface(uint8_t interface);
// OTG SETUP capture: dispatch now, or hold the newest request for the executor.
void usbDeviceSetup(const uint8_t bytes[8]);
// PendSV executor: advance the control lifecycle, then dispatch a held SETUP.
void usbDeviceService(usb_lifecycle_slice_t *slice);
// Ordinary producers' physical gate, checked before any copy or arm.
bool usbDeviceArmable(uint8_t address);
void usbDeviceResetBegin(void);
void usbDeviceResetComplete(bool high_speed);
void usbDeviceDisconnect(void);
void usbDeviceSuspend(void);
void usbDeviceResume(void);
// Serves exactly the raised class duties (USB_SOF_HID, USB_SOF_CDC); drops
// them when the session is not serviceable.
void usbDeviceSof(uint8_t demand);
void usbDeviceInComplete(uint8_t endpoint);
void usbDeviceOutComplete(uint8_t endpoint, uint32_t length);
void usbDeviceFault(usb_fault_operation_t operation, uint8_t endpoint, uint8_t cause);
usb_fault_t usbDeviceLastFault(void);
bool usbDeviceEndpoint(uint16_t address);
