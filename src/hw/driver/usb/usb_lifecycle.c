// SPDX-License-Identifier: GPL-3.0-or-later
#include "usb_lifecycle.h"
#include "usb_hid/hid_port.h"
#include <string.h>

enum {
  LIFE_IDLE, LIFE_FREEZE, LIFE_QUIESCE, LIFE_RELEASE, LIFE_OPEN,
  LIFE_RESTART, LIFE_COMMIT, LIFE_ROLLBACK_FREEZE, LIFE_ROLLBACK_QUIESCE,
  LIFE_ROLLBACK_RELEASE, LIFE_ROLLBACK_COMMIT
};

#define LIFE_NONCONTROL_MASK 0x01fe01feUL
#define LIFE_CONTROLLER_CLASS 0xffU
#define LIFE_PENDSV_EXCEPTION 14U

// Volatile keeps the lock-free checkpoint's permit store ahead of its fact load.
static volatile uint32_t schedule_facts;
static volatile bool schedule_permit;
static volatile bool schedule_installed, schedule_parked;

static uint32_t allScopes(const uint32_t scopes[USB_LIFECYCLE_CLASSES])
{
  return scopes[0] | scopes[1];
}

// Every owner a failed operation took: a DEFAULT_RESTART receipt that OPEN
// never reached is still owned and must be closed by the rollback.
static uint32_t rollbackScope(const usb_lifecycle_t *life)
{
  uint32_t scope = allScopes(life->opened);
  if (life->kind == USB_LIFECYCLE_DEFAULTS) scope |= allScopes(life->plan.opened);
  return scope;
}

static bool validPlan(const usb_lifecycle_plan_t *plan)
{
  return plan != NULL && allScopes(plan->scope) != 0U &&
         !(allScopes(plan->scope) & ~LIFE_NONCONTROL_MASK) &&
         !(plan->scope[0] & plan->scope[1]) &&
         !(plan->opened[0] & ~plan->scope[0]) &&
         !(plan->opened[1] & ~plan->scope[1]);
}

static void terminal(usb_lifecycle_t *life, usb_lifecycle_result_t result, bool retained)
{
  life->receipt.result = result;
  life->receipt.retained = retained;
  life->active = life->outstanding = false;
  life->retained = retained;
  life->phase = LIFE_IDLE;
  if (!retained) life->gates = 0U;
}

static void retainFailure(usb_lifecycle_t *life, usb_lifecycle_result_t result)
{
  if (life->failure == USB_LIFECYCLE_OK) life->failure = result;
  else life->receipt.cleanup_failure = result;
  terminal(life, life->failure, true);
}

static bool expired(usb_lifecycle_t *life, uint32_t now_us)
{
  if (!life->active ||
      (uint32_t)(now_us - life->received_us) < USB_LIFECYCLE_REQUEST_US) return false;
  retainFailure(life, USB_LIFECYCLE_TIMEOUT);
  return true;
}

static void rollback(usb_lifecycle_t *life, usb_lifecycle_result_t result)
{
  if (life->failure == USB_LIFECYCLE_OK) life->failure = result;
  life->rollback = true;
  life->outstanding = false;
  life->phase = LIFE_ROLLBACK_FREEZE;
  life->cursor = 0U;
}

bool usbLifecycleInit(usb_lifecycle_t *life, uint64_t reset)
{
  if (life == NULL || reset == 0U || life->reset != 0U) return false;
  *life = (usb_lifecycle_t){.reset = reset};
  return true;
}

void usbLifecycleResetBegin(usb_lifecycle_t *life, uint64_t reset)
{
  if (life == NULL || life->reset == 0U || reset == 0U) return;
  life->reset = reset;
  // Pre-boundary SETUPs are discarded, never replayed into the new session.
  life->latest_valid = false;
  life->dispatched.control = 0U;
  life->control_hold = life->control_owned = false;
  if (life->active) {
    // Physical owners pass to the controller cleanup of this reset.
    life->receipt.result = USB_LIFECYCLE_CANCELLED;
    life->active = false;
    life->phase = LIFE_IDLE;
  }
  // Every issued capability dies with the old identity; retention does not.
  life->outstanding = false;
  life->published = 0U;
  life->resetting = true;
  life->gates = LIFE_NONCONTROL_MASK;
}

bool usbLifecycleResetEnd(usb_lifecycle_t *life, uint64_t reset, bool released)
{
  if (life == NULL || !life->resetting || reset == 0U || reset != life->reset) return false;
  life->resetting = false;
  // Only this reset's controller cleanup proves every endpoint owner released.
  if (released) {
    life->gates = 0U;
    life->retained = false;
  } else {
    life->retained = true;
  }
  return true;
}

void usbLifecycleHoldControl(usb_lifecycle_t *life, bool hold)
{
  if (life != NULL) life->control_hold = hold;
}

bool usbLifecyclePlan(usb_lifecycle_plan_t *plan, const usb_class_t *const classes[],
                      uint8_t count, uint8_t interface)
{
  if (plan == NULL || classes == NULL || count == 0U || count > USB_LIFECYCLE_CLASSES)
    return false;
  usb_lifecycle_plan_t next = {.interface = interface};
  for (uint8_t i = 0U; i < count; ++i) {
    if (classes[i] == NULL || classes[i]->scope == NULL) return false;
    next.scope[i] = classes[i]->scope(interface, false);
    next.opened[i] = classes[i]->scope(interface, true);
  }
  if (!validPlan(&next)) return false;
  *plan = next;
  return true;
}

uint64_t usbLifecycleCaptureSetup(usb_lifecycle_t *life, const uint8_t bytes[8], uint32_t now_us)
{
  if (life == NULL || life->reset == 0U || bytes == NULL || life->resetting) return 0U;
  if (life->control_counter == UINT64_MAX) {
    terminal(life, USB_LIFECYCLE_ERROR, true);
    return 0U;
  }
  life->latest.reset = life->reset;
  life->latest.control = ++life->control_counter;
  life->latest.received_us = now_us;
  memcpy(life->latest.bytes, bytes, sizeof(life->latest.bytes));
  life->latest_valid = true;
  if (life->active) {
    life->cancelled = true;
    // An unexecuted publication capability cannot survive a newer SETUP, nor
    // can a cancelled fresh configuration still OPEN or arm its classes.
    // Executed commands and their receipts are one locked transaction.
    if (life->phase == LIFE_COMMIT || life->phase == LIFE_ROLLBACK_COMMIT ||
        (life->kind == USB_LIFECYCLE_CONFIGURE && !life->rollback &&
         (life->phase == LIFE_OPEN || life->phase == LIFE_RESTART)))
      life->outstanding = false;
  }
  return life->latest.control;
}

usb_lifecycle_result_t usbLifecycleTakeSetup(usb_lifecycle_t *life, uint32_t now_us,
                                            usb_lifecycle_setup_t *setup)
{
  if (life == NULL || setup == NULL || life->reset == 0U) return USB_LIFECYCLE_ERROR;
  // A held SETUP ages while it waits: every observation (TICKs while EP0
  // retirement holds it) judges expiry, so release after a 32-bit wrap of the
  // clock can never make it fresh.
  bool expired = life->latest_valid &&
                 (uint32_t)(now_us - life->latest.received_us) >= USB_LIFECYCLE_REQUEST_US;
  if (!expired && (life->active || life->retained || life->control_hold || life->resetting))
    return USB_LIFECYCLE_PENDING;
  if (!life->latest_valid) return USB_LIFECYCLE_STALE;
  life->latest_valid = false;
  if (life->latest.reset != life->reset) return USB_LIFECYCLE_STALE;
  life->dispatched = life->latest;
  // A newer dispatched request ends the previous operation's EP0 ownership.
  life->control_owned = false;
  *setup = life->dispatched;
  if (expired) {
    // Expired is final: a wrapped clock must never admit it later.
    life->dispatched.control = 0U;
    return USB_LIFECYCLE_TIMEOUT;
  }
  return USB_LIFECYCLE_OK;
}

usb_lifecycle_result_t usbLifecycleBegin(usb_lifecycle_t *life, usb_lifecycle_kind_t kind,
                                        uint64_t control, const usb_lifecycle_plan_t *plan,
                                        uint32_t now_us)
{
  if (life == NULL || life->reset == 0U || !validPlan(plan) ||
      (kind != USB_LIFECYCLE_CONFIGURE && kind != USB_LIFECYCLE_DEFAULTS &&
       kind != USB_LIFECYCLE_DECONFIGURE)) return USB_LIFECYCLE_ERROR;
  if (life->active || life->retained || life->control_hold || life->resetting)
    return USB_LIFECYCLE_PENDING;
  if (control == 0U || control != life->dispatched.control ||
      control != life->control_counter || life->dispatched.reset != life->reset)
    return USB_LIFECYCLE_STALE;
  if ((uint32_t)(now_us - life->dispatched.received_us) >= USB_LIFECYCLE_REQUEST_US) {
    life->dispatched.control = 0U;
    return USB_LIFECYCLE_TIMEOUT;
  }
  if (life->operation_counter == UINT64_MAX) {
    terminal(life, USB_LIFECYCLE_ERROR, true);
    return USB_LIFECYCLE_ERROR;
  }
  life->plan = *plan;
  life->kind = kind;
  life->received_us = life->dispatched.received_us;
  life->gates = allScopes(plan->scope);
  life->receipt = (usb_lifecycle_receipt_t){
    .reset = life->reset, .operation = ++life->operation_counter, .control = control
  };
  life->dispatched.control = 0U;
  memset(life->opened, 0, sizeof(life->opened));
  life->failure = USB_LIFECYCLE_OK;
  life->phase = LIFE_FREEZE;
  life->cursor = 0U;
  life->active = true;
  // Only this operation's COMMIT owner may answer the admitting request.
  life->control_owned = true;
  life->outstanding = life->cancelled = life->rollback = false;
  life->published = 0U;
  return USB_LIFECYCLE_OK;
}

bool usbLifecycleNext(usb_lifecycle_t *life, uint32_t now_us, usb_lifecycle_action_t *action)
{
  if (life == NULL || action == NULL || expired(life, now_us) ||
      !life->active || life->outstanding) return false;
  if (life->cancelled && life->kind == USB_LIFECYCLE_CONFIGURE && !life->rollback &&
      (life->phase == LIFE_OPEN || life->phase == LIFE_RESTART || life->phase == LIFE_COMMIT)) {
    if (allScopes(life->opened)) rollback(life, USB_LIFECYCLE_CANCELLED);
    else life->phase = LIFE_COMMIT;
  }
  usb_lifecycle_action_t next = {
    .reset = life->reset, .operation = life->receipt.operation,
    .control = life->receipt.control, .kind = life->kind,
    .transition = life->kind == USB_LIFECYCLE_DEFAULTS && !life->rollback ?
                  USB_CLASS_DEFAULTS : USB_CLASS_FRESH,
    .rollback = life->rollback, .class_index = LIFE_CONTROLLER_CLASS,
    .interface = life->plan.interface
  };
  // Only empty class slots/phases are skipped here; no hardware is observed.
  for (unsigned skip = 0U; skip < 8U; ++skip) {
    switch (life->phase) {
      case LIFE_FREEZE:
      case LIFE_ROLLBACK_FREEZE:
        next.command = USB_LIFECYCLE_FREEZE;
        next.scope = life->gates;
        goto issue;
      case LIFE_QUIESCE:
      case LIFE_ROLLBACK_QUIESCE:
        next.command = life->kind == USB_LIFECYCLE_DEFAULTS && !life->rollback ?
                       USB_LIFECYCLE_QUIESCE_DEFAULT : USB_LIFECYCLE_QUIESCE_CLOSE;
        next.scope = life->rollback ? rollbackScope(life) : allScopes(life->plan.opened);
        if (next.scope) goto issue;
        life->phase = life->rollback ? LIFE_ROLLBACK_RELEASE : LIFE_RELEASE;
        life->cursor = 0U;
        break;
      case LIFE_RELEASE:
      case LIFE_ROLLBACK_RELEASE:
      case LIFE_OPEN:
      case LIFE_RESTART:
        while (life->cursor < USB_LIFECYCLE_CLASSES && !life->plan.scope[life->cursor])
          ++life->cursor;
        if (life->cursor == USB_LIFECYCLE_CLASSES) {
          life->cursor = 0U;
          if (life->phase == LIFE_ROLLBACK_RELEASE) life->phase = LIFE_ROLLBACK_COMMIT;
          else if (life->phase == LIFE_RELEASE)
            life->phase = life->kind == USB_LIFECYCLE_DECONFIGURE ||
                          (life->cancelled && life->kind == USB_LIFECYCLE_CONFIGURE) ?
                          LIFE_COMMIT : LIFE_OPEN;
          else if (life->phase == LIFE_OPEN) life->phase = LIFE_RESTART;
          else life->phase = LIFE_COMMIT;
          break;
        }
        next.class_index = life->cursor;
        next.scope = life->plan.scope[life->cursor];
        next.command = life->phase == LIFE_OPEN ? USB_LIFECYCLE_OPEN :
                       life->phase == LIFE_RESTART ? USB_LIFECYCLE_RESTART : USB_LIFECYCLE_RELEASE;
        goto issue;
      case LIFE_COMMIT:
      case LIFE_ROLLBACK_COMMIT:
        next.command = USB_LIFECYCLE_COMMIT;
        next.scope = life->gates;
        next.status_allowed = !life->cancelled && !life->rollback;
        // A failed, still-current request is answered with a protocol STALL.
        next.stall_allowed = life->rollback && !life->cancelled;
        next.configured = !life->rollback && life->kind != USB_LIFECYCLE_DECONFIGURE &&
                          !(life->cancelled && life->kind == USB_LIFECYCLE_CONFIGURE);
        goto issue;
      default:
        retainFailure(life, USB_LIFECYCLE_ERROR);
        return false;
    }
  }
  retainFailure(life, USB_LIFECYCLE_ERROR);
  return false;
issue:
  if (life->ticket_counter == UINT64_MAX) {
    retainFailure(life, USB_LIFECYCLE_ERROR);
    return false;
  }
  next.ticket = ++life->ticket_counter;
  life->action = next;
  life->outstanding = true;
  *action = next;
  return true;
}

static bool actionMatches(const usb_lifecycle_t *life, const usb_lifecycle_action_t *action)
{
  if (life == NULL || action == NULL || !life->active || !life->outstanding) return false;
  const usb_lifecycle_action_t *owned = &life->action;
  return action->reset == life->reset && action->reset == owned->reset &&
         action->operation == owned->operation &&
         action->control == owned->control && action->ticket == owned->ticket &&
         action->scope == owned->scope && action->command == owned->command &&
         action->kind == owned->kind && action->transition == owned->transition &&
         action->class_index == owned->class_index && action->interface == owned->interface &&
         action->rollback == owned->rollback &&
         action->status_allowed == owned->status_allowed &&
         action->stall_allowed == owned->stall_allowed && action->configured == owned->configured;
}

// A published status answer is final: its own receipt cannot expire afterwards.
static bool current(usb_lifecycle_t *life, const usb_lifecycle_action_t *action, uint32_t now_us)
{
  if (!actionMatches(life, action)) return false;
  return (action->command == USB_LIFECYCLE_COMMIT && life->published == action->ticket) ||
         !expired(life, now_us);
}

bool usbLifecycleActionCurrent(usb_lifecycle_t *life, const usb_lifecycle_action_t *action,
                               uint32_t now_us)
{
  return current(life, action, now_us);
}

usb_lifecycle_result_t usbLifecycleComplete(usb_lifecycle_t *life,
                                           const usb_lifecycle_action_t *action,
                                           usb_lifecycle_result_t result,
                                           uint32_t touched, uint32_t now_us)
{
  if (!actionMatches(life, action)) return USB_LIFECYCLE_STALE;
  if (!current(life, action, now_us)) return USB_LIFECYCLE_TIMEOUT;
  if (result != USB_LIFECYCLE_OK && result != USB_LIFECYCLE_PENDING &&
      result != USB_LIFECYCLE_ERROR && result != USB_LIFECYCLE_TIMEOUT) {
    retainFailure(life, USB_LIFECYCLE_ERROR);
    return USB_LIFECYCLE_ERROR;
  }
  if (action->command == USB_LIFECYCLE_OPEN) {
    if ((touched & ~action->scope) || (result == USB_LIFECYCLE_OK && touched != action->scope)) {
      life->gates = LIFE_NONCONTROL_MASK;
      retainFailure(life, USB_LIFECYCLE_ERROR);
      return USB_LIFECYCLE_ERROR;
    }
    life->opened[action->class_index] |= touched;
  } else if (touched) {
    retainFailure(life, USB_LIFECYCLE_ERROR);
    return USB_LIFECYCLE_ERROR;
  }
  life->outstanding = false;
  if (result == USB_LIFECYCLE_PENDING) {
    if (action->command == USB_LIFECYCLE_QUIESCE_CLOSE ||
        action->command == USB_LIFECYCLE_QUIESCE_DEFAULT) return result;
    result = USB_LIFECYCLE_ERROR;
  }
  if (result != USB_LIFECYCLE_OK) {
    if (life->rollback) {
      retainFailure(life, result);
    } else if (action->command == USB_LIFECYCLE_OPEN || action->command == USB_LIFECYCLE_RESTART) {
      rollback(life, result);
    } else {
      retainFailure(life, result);
    }
    return result;
  }
  switch (life->phase) {
    case LIFE_FREEZE: life->phase = LIFE_QUIESCE; break;
    case LIFE_ROLLBACK_FREEZE: life->phase = LIFE_ROLLBACK_QUIESCE; break;
    case LIFE_QUIESCE: life->phase = LIFE_RELEASE; life->cursor = 0U; break;
    case LIFE_ROLLBACK_QUIESCE: life->phase = LIFE_ROLLBACK_RELEASE; life->cursor = 0U; break;
    case LIFE_RELEASE:
    case LIFE_ROLLBACK_RELEASE:
    case LIFE_OPEN:
    case LIFE_RESTART:
      ++life->cursor;
      break;
    case LIFE_COMMIT:
    case LIFE_ROLLBACK_COMMIT:
      terminal(life, life->failure != USB_LIFECYCLE_OK ? life->failure :
               life->cancelled ? USB_LIFECYCLE_CANCELLED : USB_LIFECYCLE_OK, false);
      break;
    default:
      retainFailure(life, USB_LIFECYCLE_ERROR);
      return USB_LIFECYCLE_ERROR;
  }
  return USB_LIFECYCLE_OK;
}

// EP0 without an owner belongs to ordinary control traffic only: never while
// an operation, EP0 retirement or reset cleanup owns it, and never for a
// request an operation admitted (only that operation's COMMIT answers it).
static bool controlFree(const usb_lifecycle_t *life)
{
  return !life->active && !life->control_hold && !life->resetting && !life->control_owned;
}

bool usbLifecycleMayArm(usb_lifecycle_t *life, uint8_t address,
                        const usb_lifecycle_action_t *owner, uint32_t now_us)
{
  uint32_t bit = usbEndpointMask(address);
  if (life == NULL || !life->reset || !bit || life->retained) return false;
  if (owner == NULL) return !(life->gates & bit) && ((address & 15U) || controlFree(life));
  if (owner->command == USB_LIFECYCLE_COMMIT) {
    if (address != 0x80U || !owner->status_allowed || !current(life, owner, now_us))
      return false;
    life->published = owner->ticket;
    return true;
  }
  return owner->command == USB_LIFECYCLE_RESTART && (owner->scope & bit) != 0U &&
         current(life, owner, now_us);
}

bool usbLifecycleMayDefault(usb_lifecycle_t *life, uint8_t address,
                            const usb_lifecycle_action_t *owner, uint32_t now_us)
{
  uint32_t bit = usbEndpointMask(address);
  if (life == NULL || !life->reset || !bit || life->retained) return false;
  if (owner == NULL)
    return !life->active && !life->resetting && !(life->gates & bit) &&
           ((address & 15U) || controlFree(life));
  if (owner->command == USB_LIFECYCLE_COMMIT)
    return !(address & 15U) && owner->stall_allowed && current(life, owner, now_us);
  return owner->command == USB_LIFECYCLE_OPEN && (owner->scope & bit) != 0U &&
         current(life, owner, now_us);
}

bool usbLifecycleReceipt(const usb_lifecycle_t *life, uint64_t operation,
                         usb_lifecycle_receipt_t *receipt)
{
  if (life == NULL || receipt == NULL || life->active || operation == 0U ||
      operation != life->receipt.operation) return false;
  *receipt = life->receipt;
  return true;
}

void usbLifecycleSchedulerInstall(void)
{
  schedule_installed = true;
}

void usbLifecyclePublish(uint32_t facts)
{
  // Before the executor priority is installed no fact may exist, so neither a
  // publication nor an early thread checkpoint can pend PendSV.
  if (!facts || !schedule_installed) return;
  uint32_t mask = usbHidLock();
  schedule_facts |= facts;
  if (schedule_permit) SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
  usbHidUnlock(mask);
}

void usbLifecycleThreadCheckpoint(void)
{
  // Common case: the permit is still open, and Publish pends under its lock
  // for every fact that sees it open, so there is nothing to add. A slice
  // closing it after this read is reopened by the next checkpoint.
  if (schedule_permit) return;
  // Exception code cannot acknowledge the thread turn it may be starving.
  if (__get_IPSR() != 0U) return;
  // Single core: an exception between these two volatile accesses sees the
  // permit and pends for its own fact; one before them leaves the fact here.
  schedule_permit = true;
  if (schedule_facts) SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
}

bool usbLifecycleFactsPending(void)
{
  return schedule_facts != 0U;
}

uint32_t usbLifecycleSliceBegin(void)
{
  if (__get_IPSR() != LIFE_PENDSV_EXCEPTION) return 0U;
  uint32_t mask = usbHidLock();
  uint32_t facts = schedule_permit ? schedule_facts : 0U;
  if (facts) {
    schedule_permit = false;
    schedule_facts = 0U;
  }
  usbHidUnlock(mask);
  return facts;
}

void usbLifecycleSliceEnd(uint32_t unserviced)
{
  if (__get_IPSR() != LIFE_PENDSV_EXCEPTION) return;
  uint32_t mask = usbHidLock();
  // A poll retry is never handed back here: only the next tick re-arms it.
  schedule_facts |= unserviced & ~USB_LIFECYCLE_FACT_POLL;
  // With the permit closed, a pend raised meanwhile cannot start a slice.
  if (!schedule_permit) SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk;
  usbHidUnlock(mask);
}

void usbLifecycleParkPoll(void)
{
  if (__get_IPSR() != LIFE_PENDSV_EXCEPTION) return;
  uint32_t mask = usbHidLock();
  schedule_parked = true;
  usbHidUnlock(mask);
}

void usbLifecycleSliceStart(usb_lifecycle_slice_t *slice, uint32_t now_us)
{
  // A parked poll waits for its tick even when another fact starts a slice.
  *slice = (usb_lifecycle_slice_t){.start_us = now_us, .parked = schedule_parked};
}

bool usbLifecycleSliceTransition(usb_lifecycle_slice_t *slice, uint32_t now_us)
{
  if (slice->transitions >= USB_LIFECYCLE_SLICE_TRANSITIONS ||
      (uint32_t)(now_us - slice->start_us) >= USB_LIFECYCLE_SLICE_US) {
    // Ready work stays sticky for the next thread turn, never a self-pend.
    slice->more = true;
    return false;
  }
  ++slice->transitions;
  return true;
}

void usbLifecycleSliceRefund(usb_lifecycle_slice_t *slice)
{
  if (slice->transitions) --slice->transitions;
}

bool usbLifecycleSlicePoll(usb_lifecycle_slice_t *slice, uint32_t now_us)
{
  if (slice->parked) return false;
  bool spent = (uint32_t)(now_us - slice->start_us) >= USB_LIFECYCLE_SLICE_US;
  // Only a controller this invocation saw not ready waits for the tick; an
  // observation the slice could not reach is ready work (a flush that has
  // completed meanwhile must not cost a tick period).
  if (slice->polls && (spent || slice->polls >= USB_LIFECYCLE_POLL_READS ||
                       (uint32_t)(now_us - slice->poll_us) >= USB_LIFECYCLE_POLL_US)) {
    slice->parked = true;
    return false;
  }
  if (spent) {
    slice->more = true;
    return false;
  }
  if (!slice->polls) slice->poll_us = now_us;
  return true;
}

void usbLifecycleSlicePolled(usb_lifecycle_slice_t *slice)
{
  ++slice->polls;
}

void usbLifecycleTick(bool owned)
{
  if (!owned && !schedule_parked) return;
  uint32_t mask = usbHidLock();
  uint32_t facts = owned ? USB_LIFECYCLE_FACT_TICK : 0U;
  if (schedule_parked) {
    schedule_parked = false;
    facts |= USB_LIFECYCLE_FACT_POLL;
  }
  usbHidUnlock(mask);
  usbLifecyclePublish(facts);
}
