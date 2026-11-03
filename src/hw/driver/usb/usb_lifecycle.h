// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "usb_class.h"

#define USB_LIFECYCLE_CLASSES 2U
#define USB_LIFECYCLE_REQUEST_US 40000U

typedef enum {
  USB_LIFECYCLE_CONFIGURE,
  USB_LIFECYCLE_DEFAULTS,
  USB_LIFECYCLE_DECONFIGURE
} usb_lifecycle_kind_t;

typedef enum {
  USB_LIFECYCLE_OK, USB_LIFECYCLE_PENDING, USB_LIFECYCLE_CANCELLED,
  USB_LIFECYCLE_ERROR, USB_LIFECYCLE_TIMEOUT, USB_LIFECYCLE_STALE
} usb_lifecycle_result_t;

typedef enum {
  USB_LIFECYCLE_FREEZE, USB_LIFECYCLE_QUIESCE_CLOSE,
  USB_LIFECYCLE_QUIESCE_DEFAULT, USB_LIFECYCLE_RELEASE,
  USB_LIFECYCLE_OPEN, USB_LIFECYCLE_RESTART, USB_LIFECYCLE_COMMIT
} usb_lifecycle_command_t;

typedef struct {
  uint64_t reset, control;
  uint32_t received_us;
  uint8_t bytes[8];
} usb_lifecycle_setup_t;

typedef struct {
  uint32_t scope[USB_LIFECYCLE_CLASSES];
  uint32_t opened[USB_LIFECYCLE_CLASSES];
  uint8_t interface;  // class argument chosen by the admitting SETUP
} usb_lifecycle_plan_t;

typedef struct {
  uint64_t reset, operation, control, ticket;
  uint32_t scope;
  usb_lifecycle_command_t command;
  usb_lifecycle_kind_t kind;
  usb_class_transition_t transition;
  uint8_t class_index, interface;
  bool rollback, status_allowed, stall_allowed, configured;
} usb_lifecycle_action_t;

typedef struct {
  uint64_t reset, operation, control;
  usb_lifecycle_result_t result;
  usb_lifecycle_result_t cleanup_failure;
  bool retained;
} usb_lifecycle_receipt_t;

// One fixed record per controller. All calls require the transport lock.
// Only the continuation executor issues/completes actions. IRQ-side SETUP
// capture may cancel between transactions, never during a class commit.
typedef struct {
  uint64_t reset, operation_counter, control_counter, ticket_counter;
  usb_lifecycle_setup_t latest, dispatched;
  usb_lifecycle_plan_t plan;
  usb_lifecycle_action_t action;
  usb_lifecycle_receipt_t receipt;
  uint32_t gates, received_us, opened[USB_LIFECYCLE_CLASSES];
  usb_lifecycle_kind_t kind;
  usb_lifecycle_result_t failure;
  uint8_t phase, cursor;
  uint64_t published;  // ticket of the COMMIT whose status arm was granted
  bool latest_valid, active, outstanding, cancelled, rollback, retained;
  bool control_hold, resetting, control_owned;
} usb_lifecycle_t;

// Initialize zeroed storage once; Init cannot erase a retained owner.
bool usbLifecycleInit(usb_lifecycle_t *life, uint64_t reset);
// Raw reset/disconnect identity. An active operation ends CANCELLED; every
// non-control direction stays gated and dispatch stays held until End. A
// failed controller cleanup leaves the record retained until the next reset.
void usbLifecycleResetBegin(usb_lifecycle_t *life, uint64_t reset);
bool usbLifecycleResetEnd(usb_lifecycle_t *life, uint64_t reset, bool released);
// EP0 IN retirement debt: holds SETUP dispatch and EP0 arms without an owner.
void usbLifecycleHoldControl(usb_lifecycle_t *life, bool hold);
bool usbLifecyclePlan(usb_lifecycle_plan_t *plan, const usb_class_t *const classes[],
                      uint8_t count, uint8_t interface);
uint64_t usbLifecycleCaptureSetup(usb_lifecycle_t *life, const uint8_t bytes[8], uint32_t now_us);
// OK/ TIMEOUT both return the original receipt; TIMEOUT must not produce success.
// PENDING means ownership blocks dispatch; STALE means the inbox is empty.
usb_lifecycle_result_t usbLifecycleTakeSetup(usb_lifecycle_t *life, uint32_t now_us,
                                            usb_lifecycle_setup_t *setup);
usb_lifecycle_result_t usbLifecycleBegin(usb_lifecycle_t *life, usb_lifecycle_kind_t kind,
                                        uint64_t control, const usb_lifecycle_plan_t *plan,
                                        uint32_t now_us);
bool usbLifecycleNext(usb_lifecycle_t *life, uint32_t now_us, usb_lifecycle_action_t *action);
// Nonwaiting commands return one receipt. PENDING is valid only for quiescence.
// OPEN reports every touched endpoint, including a failed open's descriptor.
// QUIESCE OK requires the complete matching physical receipt from the PCD owner.
usb_lifecycle_result_t usbLifecycleComplete(usb_lifecycle_t *life,
                                           const usb_lifecycle_action_t *action,
                                           usb_lifecycle_result_t result,
                                           uint32_t touched, uint32_t now_us);
// Owner capabilities are checked against the original SETUP wall deadline at
// the caller's timestamp: at expiry they are revoked and TIMEOUT is retained,
// so one timestamp spans validation, publication and the receipt. A granted
// COMMIT status arm is final, so its receipt can no longer expire. Without an
// owner, EP0 never answers a request that an operation admitted.
bool usbLifecycleActionCurrent(usb_lifecycle_t *life, const usb_lifecycle_action_t *action,
                               uint32_t now_us);
bool usbLifecycleMayArm(usb_lifecycle_t *life, uint8_t address,
                        const usb_lifecycle_action_t *owner, uint32_t now_us);
bool usbLifecycleMayDefault(usb_lifecycle_t *life, uint8_t address,
                            const usb_lifecycle_action_t *owner, uint32_t now_us);
bool usbLifecycleReceipt(const usb_lifecycle_t *life, uint64_t operation,
                         usb_lifecycle_receipt_t *receipt);

// Executor scheduling (design 8.1). These calls take the transport lock
// themselves. Facts stay sticky until an executor slice consumes them; only a
// thread-mode checkpoint opens the permit for the next working slice.
enum {
  USB_LIFECYCLE_FACT_EVENT = 1U << 0,  // captured hardware fact or request
  USB_LIFECYCLE_FACT_TICK = 1U << 1,   // deadline or missed-event reconciliation
  USB_LIFECYCLE_FACT_POLL = 1U << 2    // parked controller-local poll retry
};
// Called once the executor's PendSV priority is installed; until then every
// publication is dropped, so no early thread checkpoint can pend PendSV.
void usbLifecycleSchedulerInstall(void);
// Any context, including before USB initialization; never runs the executor.
void usbLifecyclePublish(uint32_t facts);
// Thread mode only. Publish-only proof of a thread turn: opens one permit and
// pends the executor when facts are sticky; never steps, polls or waits, and is
// safe before USB initialization. An exception-handler call is ignored. It
// never masks interrupts; while the permit is open it is one volatile load.
void usbLifecycleThreadCheckpoint(void);
// Any context: true while sticky facts wait for the next thread turn, so a
// waiting foreground loop must take another checkpoint instead of sleeping.
bool usbLifecycleFactsPending(void);
// PendSV only. Begin consumes the permit only when it returns sticky facts.
// End republishes unserviced facts and retires a stale pend while closed;
// it never republishes POLL.
uint32_t usbLifecycleSliceBegin(void);
void usbLifecycleSliceEnd(uint32_t unserviced);
// PendSV only: a poll burst reached its ceiling; only the next tick retries.
void usbLifecycleParkPoll(void);
// SysTick: TICK while lifecycle work is owned, plus one parked POLL retry.
void usbLifecycleTick(bool owned);

// One executor invocation (design 8.1): at most eight transitions in a 10-us
// exclusive budget and one no-IRQ poll burst of 2 us or 64 observations.
#define USB_LIFECYCLE_SLICE_US 10U
#define USB_LIFECYCLE_SLICE_TRANSITIONS 8U
#define USB_LIFECYCLE_POLL_US 2U
#define USB_LIFECYCLE_POLL_READS 64U
typedef enum {
  USB_LIFECYCLE_READY,       // another transition may run now
  USB_LIFECYCLE_WAIT_EVENT,  // an interrupt-capable fact will publish
  USB_LIFECYCLE_WAIT_POLL    // controller-local readiness without an IRQ
} usb_lifecycle_wait_t;
typedef struct {
  uint32_t start_us, poll_us;
  uint8_t transitions, polls;
  bool parked, more;
} usb_lifecycle_slice_t;
// A slice started while a poll is parked cannot poll before the next tick.
void usbLifecycleSliceStart(usb_lifecycle_slice_t *slice, uint32_t now_us);
// Charges one transition; false (and `more`) once the slice budget is spent.
bool usbLifecycleSliceTransition(usb_lifecycle_slice_t *slice, uint32_t now_us);
// Returns a transition charged for a command that changed nothing (an
// observation of an interrupt-capable wait, or a poll read charged as such).
void usbLifecycleSliceRefund(usb_lifecycle_slice_t *slice);
// Admits one no-IRQ observation of a controller-local wait. False parks it
// until the next tick once this invocation saw the wait unchanged and its
// burst (2 us or 64 such observations) or the slice is spent; a slice spent
// before any unchanged observation leaves it ready work (`more`).
bool usbLifecycleSlicePoll(usb_lifecycle_slice_t *slice, uint32_t now_us);
// Charges an admitted observation that found the wait unchanged: one poll
// read, never a transition.
void usbLifecycleSlicePolled(usb_lifecycle_slice_t *slice);
