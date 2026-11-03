// SPDX-License-Identifier: GPL-3.0-or-later
#include "usb_device.h"
#include "usb_control.h"
#include "usb_port.h"
#include "usb_descriptors.h"
#include "usb_hid/hid_port.h"
#include "micros.h"
#if HW_USB_CDC
#include "usb_cdc/cdc_acm.h"
#endif

usb_device_state_t usb_device;
usb_lifecycle_t usb_lifecycle;
static usb_fault_t last_fault;
static uint64_t dispatching;
static const usb_class_t *const classes[] = {
  &usb_hid_class,
#if HW_USB_CDC
  &usb_cdc_class,
#endif
};
enum { CLASS_COUNT = sizeof(classes) / sizeof(classes[0]) };

void usbDeviceFault(usb_fault_operation_t operation, uint8_t endpoint, uint8_t cause)
{
  ++last_fault.count;
  last_fault.operation = operation;
  last_fault.endpoint = endpoint;
  last_fault.cause = cause;
}

usb_fault_t usbDeviceLastFault(void)
{
  uint32_t mask = usbHidLock();
  usb_fault_t result = last_fault;
  usbHidUnlock(mask);
  return result;
}

bool usbDeviceStart(bool dual_speed, uint8_t interval)
{
  if (usb_device.retained || usb_lifecycle.retained || usb_lifecycle.active) return false;
  // Unlocked: power-on, before the OTG IRQ is enabled, and `configured` false
  // makes every SOF-demand predicate false (contract S1).
  usb_device = (usb_device_state_t){.dual_speed = dual_speed, .interval = interval};
  usb_lifecycle = (usb_lifecycle_t){0};
  return usbPortStart(dual_speed);
}

static void retireSession(void)
{
  usb_device.configured = false;
  usb_device.wake_enabled = false;
  ++usb_device.epoch;
  for (unsigned i = 0; i < CLASS_COUNT; ++i) classes[i]->retire();
}

// Logical release only: the reset join owns the physical proof for every
// endpoint. A shutdown never calls it: class storage stays owned until reset.
static bool releaseClasses(void)
{
  retireSession();
  usbControlReset();
  bool released = true;
  for (unsigned i = 0; i < CLASS_COUNT; ++i)
    if (!classes[i]->release(USB_CLASS_ALL_INTERFACES, USB_CLASS_FRESH)) released = false;
  usb_device.retained = !released;
  return released;
}

// Admission of the dispatched request: on success its status belongs to the
// operation's COMMIT, executed by the PendSV lifecycle executor.
static bool beginOperation(usb_lifecycle_kind_t kind, uint8_t interface)
{
  usb_lifecycle_plan_t plan;
  if (!usbLifecyclePlan(&plan, classes, CLASS_COUNT, interface) ||
      usbLifecycleBegin(&usb_lifecycle, kind, dispatching, &plan, micros()) != USB_LIFECYCLE_OK)
    return false;
  usbLifecyclePublish(USB_LIFECYCLE_FACT_EVENT);
  return true;
}

bool usbDeviceConfigure(uint8_t configuration)
{
  if (configuration > 1 || usb_device.retained || usb_device.reset_pending) return false;
  return beginOperation(!configuration ? USB_LIFECYCLE_DECONFIGURE :
                        usb_device.configured ? USB_LIFECYCLE_DEFAULTS : USB_LIFECYCLE_CONFIGURE,
                        USB_CLASS_ALL_INTERFACES);
}

bool usbDeviceSetInterface(uint8_t interface)
{
  if (interface >= usb_interface_count || !usb_device.configured || usb_device.retained || usb_device.reset_pending)
    return false;
  return beginOperation(USB_LIFECYCLE_DEFAULTS, interface);
}

// False while ownership holds the latest SETUP in the policy inbox.
static bool dispatch(void)
{
  usb_lifecycle_setup_t setup;
  switch (usbLifecycleTakeSetup(&usb_lifecycle, micros(), &setup)) {
    case USB_LIFECYCLE_PENDING:
      return false;
    case USB_LIFECYCLE_OK:
      dispatching = setup.control;
      usbControlSetup(setup.bytes);
      dispatching = 0U;
      break;
    case USB_LIFECYCLE_TIMEOUT:
      // Expired before dispatch: the request gets no late answer.
      usbControlReset();
      break;
    default:
      break;
  }
  return true;
}

void usbDeviceSetup(const uint8_t bytes[8])
{
  uint32_t mask = usbHidLock();
  if (usbLifecycleCaptureSetup(&usb_lifecycle, bytes, micros()) && !dispatch())
    usbLifecyclePublish(USB_LIFECYCLE_FACT_EVENT);
  usbHidUnlock(mask);
}

bool usbDeviceArmable(uint8_t address)
{
  return usbLifecycleMayArm(&usb_lifecycle, address, NULL, 0U);
}

static usb_lifecycle_result_t quiesce(const usb_lifecycle_action_t *action,
                                      usb_lifecycle_slice_t *slice, usb_lifecycle_wait_t *wait)
{
  usb_lifecycle_result_t result = USB_LIFECYCLE_OK;
  *wait = USB_LIFECYCLE_WAIT_EVENT;
  for (uint8_t bit = 0U; bit < 32U; ++bit) {
    if (!(action->scope & (1UL << bit))) continue;
    uint8_t address = bit < 16U ? (uint8_t)(0x80U | bit) : (uint8_t)(bit - 16U);
    usb_lifecycle_wait_t endpoint;
    usb_lifecycle_result_t step = usbPortQuiesce(address, action->operation,
      action->command == USB_LIFECYCLE_QUIESCE_DEFAULT, slice, &endpoint);
    if (step == USB_LIFECYCLE_PENDING) {
      result = USB_LIFECYCLE_PENDING;
      if (endpoint == USB_LIFECYCLE_READY ||
          (endpoint == USB_LIFECYCLE_WAIT_POLL && *wait == USB_LIFECYCLE_WAIT_EVENT))
        *wait = endpoint;
    } else if (step != USB_LIFECYCLE_OK) {
      return step;
    }
  }
  return result;
}

static bool openGates(const usb_lifecycle_action_t *action)
{
  bool ok = true;
  for (uint8_t bit = 0U; bit < 32U; ++bit)
    if ((action->scope & (1UL << bit)) &&
        !usbPortOpenGate(bit < 16U ? (uint8_t)(0x80U | bit) : (uint8_t)(bit - 16U),
                         action->operation)) ok = false;
  return ok;
}

// Caller holds the transport lock and has validated the action at now_us, the
// one timestamp of its owner window, publication and receipt.
static usb_lifecycle_result_t run(const usb_lifecycle_action_t *action, uint32_t *touched,
                                  uint32_t now_us)
{
  const usb_class_t *owner = action->class_index < CLASS_COUNT ? classes[action->class_index] : NULL;
  bool ok = false;
  switch (action->command) {
    case USB_LIFECYCLE_FREEZE:
      if (action->rollback || action->kind != USB_LIFECYCLE_DEFAULTS) {
        retireSession();
      } else if (action->interface == USB_CLASS_ALL_INTERFACES) {
        usb_device.wake_enabled = false;
        ++usb_device.epoch;
      }
      ok = true;
      break;
    case USB_LIFECYCLE_RELEASE:
      ok = owner != NULL && owner->release(action->interface, action->transition);
      break;
    case USB_LIFECYCLE_OPEN:
      usbPortOwner(action, now_us);
      ok = owner != NULL && owner->open(action->interface, action->transition);
      *touched = usbPortTouched();
      usbPortOwner(NULL, 0U);
      break;
    case USB_LIFECYCLE_RESTART:
      // Reopen exactly the released scope inside the arm transaction.
      ok = owner != NULL && openGates(action);
      if (ok) {
        usbPortOwner(action, now_us);
        ok = owner->restart(action->interface, action->transition);
        usbPortOwner(NULL, 0U);
      }
      break;
    case USB_LIFECYCLE_COMMIT:
      // Configuration, status and STALL are published only once every gate
      // is released; a failed release or status arm fails COMMIT, retained.
      ok = usbPortReleaseGates(action->operation);
      if (!ok) break;
      usbPortOwner(action, now_us);
      if (action->status_allowed && !(ok = usbControlStatus()))
        usbDeviceFault(USB_FAULT_CONTROL, 0x80, 2);
      if (action->stall_allowed) usbControlStall();
      usbPortOwner(NULL, 0U);
      if (ok && action->configured) usb_device.configured = true;
      break;
    default:
      break;
  }
  return ok ? USB_LIFECYCLE_OK : USB_LIFECYCLE_ERROR;
}

static usb_lifecycle_wait_t execute(const usb_lifecycle_action_t *action,
                                    usb_lifecycle_slice_t *slice)
{
  usb_lifecycle_wait_t wait = USB_LIFECYCLE_READY;
  usb_lifecycle_result_t result = USB_LIFECYCLE_ERROR;
  uint32_t touched = 0U;
  bool physical = action->command == USB_LIFECYCLE_QUIESCE_CLOSE ||
                  action->command == USB_LIFECYCLE_QUIESCE_DEFAULT;
  // Quiescence is the only pending command and steps in its own short
  // transactions, each endpoint command charged to the slice; every other
  // command validates, runs and completes in one.
  if (physical) result = quiesce(action, slice, &wait);
  uint32_t mask = usbHidLock();
  uint32_t now = micros();
  if (usbLifecycleActionCurrent(&usb_lifecycle, action, now)) {
    if (!physical) result = run(action, &touched, now);
    usb_lifecycle_result_t completed = usbLifecycleComplete(&usb_lifecycle, action, result,
                                                            touched, now);
    // Gates are clear at terminal: a keyboard report queued meanwhile arms
    // now; CDC demand is settled at the end of this slice and refills at SOF.
    if (action->command == USB_LIFECYCLE_COMMIT && completed == USB_LIFECYCLE_OK &&
        usb_device.configured && !usb_lifecycle.active) usb_hid_class.sof();
  }
  usbHidUnlock(mask);
  return physical && result == USB_LIFECYCLE_PENDING ? wait : USB_LIFECYCLE_READY;
}

void usbDeviceService(usb_lifecycle_slice_t *slice)
{
  // Each issued action is one transition; quiescence additionally charges
  // every endpoint command and poll read inside usbPortQuiesce.
  while (usb_lifecycle.active && usbLifecycleSliceTransition(slice, micros())) {
    usb_lifecycle_action_t action;
    uint32_t mask = usbHidLock();
    bool issued = usbLifecycleNext(&usb_lifecycle, micros(), &action);
    usbHidUnlock(mask);
    if (!issued) {
      usbLifecycleSliceRefund(slice);
      break;
    }
    // An interrupt-capable wait, a parked poll or a spent budget ends here.
    if (execute(&action, slice) != USB_LIFECYCLE_READY || slice->more) return;
  }
  uint32_t mask = usbHidLock();
  // A retained owner (failed step or expiry) also closes session admission.
  if (usb_lifecycle.retained && usb_device.configured) retireSession();
  // The newest held request is dispatched only once no owner holds EP0.
  if (!usb_lifecycle.active) (void)dispatch();
  usbHidUnlock(mask);
}

void usbDeviceResetBegin(void)
{
  if (usb_device.reset_pending) return;
  usb_device.reset_pending = true;
  usb_device.suspended = false;
  retireSession();
  usbControlReset();
}

void usbDeviceResetComplete(bool high_speed)
{
  bool released = releaseClasses();
  usb_device.address = 0;
  usb_device.high_speed = high_speed;
  usb_device.suspended = false;
  bool out = usbPortOpen(0, USB_EP0_PACKET);
  bool in = usbPortOpen(0x80, USB_EP0_PACKET);
  usb_device.reset_pending = !(released && out && in);
}

void usbDeviceDisconnect(void)
{
  // The disconnect identity's join and the next raw reset own the cleanup.
  usb_device.address = 0;
}

void usbDeviceSuspend(void)
{
  if (!usb_device.reset_pending) {
    usb_device.suspended = true;
    ++usb_device.epoch;
  }
}

void usbDeviceResume(void)
{
  if (!usb_device.reset_pending && usb_device.suspended) {
    usb_device.suspended = false;
    // Resume can be reconciled inside a HID completion; CDC demand is settled
    // before this SOF routes or at the end of this dispatch.
    if (usb_device.configured) usb_hid_class.sof();
  }
}

void usbDeviceSof(uint8_t demand)
{
  // Class predicates imply a serviceable session, so no settle re-raises
  // what this drops; SUSPEND belongs to the SOF callback.
  if (!usb_device.configured || usb_device.suspended || usb_device.reset_pending) {
    usbPortSofDemand(USB_SOF_HID | USB_SOF_CDC, false);
    return;
  }
  if (demand & USB_SOF_HID) usb_hid_class.sof();
#if HW_USB_CDC
  // CDC ring refill is SOF-owned: never inside a lifecycle commit (design 10).
  if (demand & USB_SOF_CDC) usb_cdc_class.sof();
#endif
}

bool usbDeviceEndpoint(uint16_t address)
{
  if (address & 0xff70U) return false;
  if (!(address & 15U)) return true;
  if (!usb_device.configured) return false;
  return (address & 0x80U) ? usb_in_handlers[address & 15U] != 0 :
                           usb_out_handlers[address & 15U] != 0;
}

void usbDeviceInComplete(uint8_t endpoint)
{
  if (usb_device.reset_pending) return;
  if (!endpoint) { usbControlInComplete(); return; }
  if (endpoint < 16 && usb_device.configured && !usb_device.suspended) {
    void (*handler)(uint8_t) = usb_in_handlers[endpoint];
    if (handler) handler(endpoint);
  }
}

void usbDeviceOutComplete(uint8_t endpoint, uint32_t length)
{
  if (usb_device.reset_pending) return;
  if (!endpoint) { usbControlOutComplete((uint16_t)length); return; }
  if (endpoint < 16 && usb_device.configured && !usb_device.suspended) {
    bool (*handler)(uint8_t, uint32_t) = usb_out_handlers[endpoint];
    if (handler) handler(endpoint, length);
  }
}
