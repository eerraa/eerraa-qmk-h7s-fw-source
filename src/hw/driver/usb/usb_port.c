// SPDX-License-Identifier: GPL-3.0-or-later
// Controller ownership: RM0477 USB OTG device mode; lifecycle policy: contract_usb section 6.
#include "hw_def.h"
#include "stm32h7rsxx_hal_pcd_lifecycle.h"
#include "usb_port.h"
#include "usb_device.h"
#include "usb_control.h"
#include "usb_descriptors.h"
#include "usb_hid/hid_port.h"
#include "micros.h"
#if HW_USB_CDC
#include "usb_cdc/cdc_acm.h"
#endif

// EP0 IN retirement owners use their own identity space beside the policy's.
#define PORT_RETIRE_OPERATION (1ULL << 62)

static PCD_HandleTypeDef controller;
static PCD_LifecycleTypeDef pcd_life;
static uint64_t reset_counter, arm_counter, retire_counter, control_retire;
static const usb_lifecycle_action_t *owner_action;
static uint32_t owner_touched, owner_now;
static bool resume_pending, wake_pending;
static bool bus_suspended;
static bool resume_skip_sof;
static uint32_t suspended_at;
static bool started;
// Written by the executor or the foreground escape, read by the waiting thread.
static volatile usb_shutdown_t shutdown;
// SOF demand owner (contract S1): one bit per duty, and the shadow of
// GINTMSK.SOFM, which only this file writes. Updated with PRIMASK held.
static uint8_t sof_demand;
static bool sof_unmasked;
// Bumped by every settle; read lock-free by the idle getters' snapshot bracket.
uint32_t usb_settle_seq;

static uint32_t pcdBit(uint8_t address)
{
  return 1UL << ((address & 15U) + ((address & 0x80U) ? 0U : 16U));
}

static int32_t pcdIndex(uint8_t address)
{
  uint8_t ep = address & 15U;
  if ((address & 0x70U) || ep >= PCD_LIFE_ENDPOINTS) return -1;
  return (int32_t)(ep + ((address & 0x80U) ? 0U : PCD_LIFE_ENDPOINTS));
}

static USB_OTG_DeviceTypeDef *registers(void)
{
  return (USB_OTG_DeviceTypeDef *)((uintptr_t)controller.Instance + USB_OTG_DEVICE_BASE);
}

static bool checked(HAL_StatusTypeDef status, usb_fault_operation_t operation, uint8_t endpoint)
{
  if (status == HAL_OK) return true;
  usbDeviceFault(operation, endpoint, (uint8_t)status);
  return false;
}

static bool prepareController(void)
{
  RCC_PeriphCLKInitTypeDef clocks = {0};
  clocks.PeriphClockSelection = RCC_PERIPHCLK_USBPHYC;
  clocks.UsbPhycClockSelection = RCC_USBPHYCCLKSOURCE_HSE;
  if (!checked(HAL_RCCEx_PeriphCLKConfig(&clocks), USB_FAULT_INIT, 0)) return false;
  HAL_PWREx_EnableUSBVoltageDetector();
  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
  __HAL_RCC_USBPHYC_CLK_ENABLE();
  // A legacy jump can retain TinyUSB's global IRQ ownership before HAL can initialize.
  if (USB_OTG_HS->GAHBCFG & USB_OTG_GAHBCFG_GINT) {
    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
    __HAL_RCC_USB_OTG_HS_FORCE_RESET();
    __HAL_RCC_USBPHYC_FORCE_RESET();
    delay(1);
    __HAL_RCC_USBPHYC_RELEASE_RESET();
    __HAL_RCC_USB_OTG_HS_RELEASE_RESET();
    NVIC_ClearPendingIRQ(OTG_HS_IRQn);
    delay(100);
  }
  return true;
}

void HAL_PCD_MspInit(PCD_HandleTypeDef *handle) { (void)handle; }

// The RX-empty barrier is observed by the OTG IRQ itself, even with no entry queued.
static void pendOtg(void)
{
  NVIC_SetPendingIRQ(OTG_HS_IRQn);
}

// The shutdown's terminal: no OTG interrupt reaches the executor again.
static void isolateOtg(void)
{
  HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
  NVIC_ClearPendingIRQ(OTG_HS_IRQn);
}

// PHY clock rule (design T7): gated iff the bus is suspended and no FIFO
// command can be in flight. Suspended owners never acquire the ticket, so the
// release of one already held is the only later gating point.
static void regate(void)
{
  if (bus_suspended && pcd_life.fifo_owner == PCD_LIFE_FIFO_NONE)
    __HAL_PCD_GATE_PHYCLOCK(&controller);
}

// One engine command's budget (design 8.1): re-observing an unchanged FIFO or
// flush wait is a poll read, and every command reserves a transition that is
// refunded when it changed nothing. False leaves the work READY or parked.
static bool sliceStep(usb_lifecycle_slice_t *slice, bool poll)
{
  uint32_t now = micros();
  if (poll && !usbLifecycleSlicePoll(slice, now)) return false;
  return usbLifecycleSliceTransition(slice, now);
}

// The ticket holder's (or next holder's) FIFO-idle and flush waits are
// controller-local readiness, polled. A queued owner waits for the holder's
// release, an event, and an unheld ticket while suspended is paused
// (stm32h7rsxx_hal_pcd_lifecycle.h) until the resume event.
static bool fifoWait(int32_t index, uint8_t phase)
{
  if (phase == PCD_QUIESCE_FLUSH || pcd_life.fifo_owner == (uint8_t)index) return true;
  return phase == PCD_QUIESCE_FIFO && pcd_life.fifo_owner == PCD_LIFE_FIFO_NONE &&
         !pcd_life.suspended;
}

bool usbPortStart(bool dual_speed)
{
  // A pending shutdown keeps `started` until its terminal.
  if (started || !prepareController()) return false;
  shutdown = USB_SHUTDOWN_NONE;
  controller = (PCD_HandleTypeDef){0};
  pcd_life = (PCD_LifecycleTypeDef){0};
  control_retire = 0U;
  owner_action = NULL;
  sof_demand = 0U;
  sof_unmasked = false;
  controller.Instance = USB_OTG_HS;
  controller.Init.dev_endpoints = 9;
  controller.Init.speed = dual_speed ? PCD_SPEED_HIGH : PCD_SPEED_HIGH_IN_FULL;
  controller.Init.phy_itface = USB_OTG_HS_EMBEDDED_PHY;
  // SOFM starts clear; only the demand owner unmasks it.
  controller.Init.Sof_enable = DISABLE;
  if (!checked(HAL_PCD_Init(&controller), USB_FAULT_INIT, 0)) return false;
  // TXFE means "completely empty", after the last core soft reset: with every
  // IN FIFO at least one max packet deep, each TXFE entry can write a packet
  // (Linux dwc2 sets the same level for dedicated FIFOs).
  controller.Instance->GAHBCFG |= USB_OTG_GAHBCFG_TXFELVL;
  // One reset identity names both the physical owners and the policy record.
  ++reset_counter;
  if (!checked(HAL_PCD_LifecycleInit(&pcd_life, &controller, reset_counter), USB_FAULT_INIT, 0) ||
      !usbLifecycleInit(&usb_lifecycle, reset_counter)) return false;
  enum { RX = 512, CONTROL = 32, KEYBOARD = 32, NOTIFY = 16, BULK = 128, VIA = 16, EXTRA = 16 };
  _Static_assert(RX + CONTROL + KEYBOARD + NOTIFY + BULK + VIA + EXTRA <= 1024, "USB FIFO capacity");
  _Static_assert(CONTROL * 4 >= USB_EP0_PACKET && KEYBOARD * 4 >= HID_EPIN_SIZE &&
                 NOTIFY * 4 >= USB_CDC_NOTIFY_PACKET && BULK * 4 >= USB_CDC_BULK_HS_PACKET &&
                 VIA * 4 >= HID_VIA_EP_SIZE && EXTRA * 4 >= HID_EXK_EP_SIZE, "USB packet capacity");
  static const uint16_t words[] = {CONTROL, KEYBOARD, NOTIFY, BULK, VIA, EXTRA};
  if (!checked(HAL_PCDEx_SetRxFiFo(&controller, RX), USB_FAULT_INIT, 0)) return false;
  for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
    if (!checked(HAL_PCDEx_SetTxFiFo(&controller, i, words[i]), USB_FAULT_INIT, i | 0x80))
      return false;
  usbDescriptorsInit((const uint32_t *)UID_BASE);
  resume_pending = wake_pending = false;
  bus_suspended = resume_skip_sof = false;
  // The executor's lowest priority precedes every fact source (design 8.1).
  HAL_NVIC_SetPriority(PendSV_IRQn, 15, 0);
  usbLifecycleSchedulerInstall();
  HAL_NVIC_SetPriority(OTG_HS_IRQn, 2, 0);
  NVIC_ClearPendingIRQ(OTG_HS_IRQn);
  started = checked(HAL_PCD_Start(&controller), USB_FAULT_INIT, 0);
  if (started) HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
  return started;
}

// Only the executor's owner window may answer, open or default inside an
// operation's scope; every other caller needs an ungated, unowned endpoint
// and, to arm, no FIFO-I/O exclusion (a refusal the producer simply retries).
static bool permitted(uint8_t address, bool arm)
{
  if (owner_action != NULL)
    return arm ? usbLifecycleMayArm(&usb_lifecycle, address, owner_action, owner_now) :
                 usbLifecycleMayDefault(&usb_lifecycle, address, owner_action, owner_now);
  return pcd_life.reset_phase == PCD_RESET_IDLE && !(pcd_life.gated & pcdBit(address)) &&
         (arm ? pcd_life.fifo_owner == PCD_LIFE_FIFO_NONE &&
                usbLifecycleMayArm(&usb_lifecycle, address, NULL, 0U) :
                usbLifecycleMayDefault(&usb_lifecycle, address, NULL, 0U));
}

static bool openEndpoint(uint8_t address, uint16_t size, uint8_t type)
{
  uint32_t mask = usbHidLock();
  bool ok = permitted(address, false);
  if (ok) {
    // A failed open may already have touched the descriptor.
    if (owner_action != NULL) owner_touched |= usbEndpointMask(address);
    ok = checked(HAL_PCD_EP_Open(&controller, address, size, type), USB_FAULT_OPEN, address);
  }
  usbHidUnlock(mask);
  return ok;
}

bool usbPortOpen(uint8_t address, uint16_t size)
{
  return openEndpoint(address, size, (address & 15) ? EP_TYPE_INTR : EP_TYPE_CTRL);
}

bool usbPortOpenBulk(uint8_t address, uint16_t size)
{
  return openEndpoint(address, size, EP_TYPE_BULK);
}

// Every arm binds a fresh identity first: events latch against the arm that owns them.
static bool transmit(uint8_t address, const uint8_t *data, uint16_t length, bool ready)
{
  uint32_t mask = usbHidLock();
  bool ok = permitted(address, true);
  if (ok) {
    HAL_StatusTypeDef status = HAL_PCD_LifecycleBindIn(&pcd_life, address & 15U, ++arm_counter);
    if (status == HAL_OK)
      status = ready ? HAL_PCD_EP_TransmitReady(&controller, address, (uint8_t *)data, length) :
                       HAL_PCD_EP_Transmit(&controller, address, (uint8_t *)data, length);
    ok = checked(status, USB_FAULT_TRANSMIT, address);
  }
  usbHidUnlock(mask);
  return ok;
}

bool usbPortTransmitBuffered(uint8_t address, const uint8_t *data, uint16_t length)
{
  return transmit(address, data, length, false);
}

bool usbPortTransmit(uint8_t address, const uint8_t *data, uint16_t length)
{
  return transmit(address, data, length, (address & 15) != 0);
}

bool usbPortReceive(uint8_t address, uint8_t *data, uint16_t length, uint16_t capacity)
{
  uint32_t mask = usbHidLock();
  bool ok = permitted(address, true);
  if (ok) {
    HAL_StatusTypeDef status = HAL_PCD_LifecycleBindRx(&pcd_life, address & 15U, data,
                                                       capacity, ++arm_counter);
    if (status == HAL_OK) status = HAL_PCD_EP_Receive(&controller, address, data, length);
    ok = checked(status, USB_FAULT_RECEIVE, address);
  }
  usbHidUnlock(mask);
  return ok;
}

bool usbPortHalt(uint8_t address, bool halt)
{
  uint32_t mask = usbHidLock();
  bool ok = permitted(address, false) &&
            checked(halt ? HAL_PCD_EP_SetStall(&controller, address) :
                           HAL_PCD_EP_ClrStall(&controller, address), USB_FAULT_CONTROL, address);
  usbHidUnlock(mask);
  return ok;
}

bool usbPortRetireControlIn(void)
{
  uint32_t mask = usbHidLock();
  HAL_StatusTypeDef status = HAL_OK;
  // The raw-reset join itself retires EP0 IN and drops every pre-join SETUP.
  if (!control_retire && pcd_life.reset_phase == PCD_RESET_IDLE) {
    uint64_t operation = PORT_RETIRE_OPERATION | ++retire_counter;
    status = HAL_PCD_QuiesceBegin(&pcd_life, 0x80U, operation, pcd_life.arm[0],
                                  PCD_QUIESCE_EP0_RELEASE, micros());
    if (status == HAL_OK) control_retire = operation;
    // Old storage stays owned: without an owner EP0 parks until a raw reset.
    usbLifecycleHoldControl(&usb_lifecycle, true);
    usbLifecyclePublish(USB_LIFECYCLE_FACT_EVENT);
  }
  usbHidUnlock(mask);
  return checked(status, USB_FAULT_CONTROL, 0x80);
}

void usbPortOwner(const usb_lifecycle_action_t *owner, uint32_t now_us)
{
  owner_action = owner;
  owner_now = now_us;
  owner_touched = 0U;
}

uint32_t usbPortTouched(void)
{
  return owner_touched;
}

// Same-generation defaults keep accepted data: a completion latched by the
// owner is delivered once, never replayed by RESTART or silently dropped.
static bool deliverCaptured(uint8_t address, PCD_QuiesceEndpoint *owner, bool defaults)
{
  uint8_t ep = address & 15U;
  if (!(owner->events & USB_OTG_DIEPINT_XFRC)) return false;
  if (address & 0x80U) {
    if (owner->phase != PCD_QUIESCE_DONE) return false;
    owner->events &= ~USB_OTG_DIEPINT_XFRC;
    if (defaults) usbDeviceInComplete(ep);
    return true;
  }
  if (!(pcd_life.rx_debt & (1UL << ep)) || !pcd_life.rx_empty) return false;
  uint32_t length = controller.OUT_ep[ep].xfer_count;
  if (HAL_PCD_LifecycleRxDebtDone(&pcd_life, ep, pcd_life.reset,
                                  pcd_life.arm[PCD_LIFE_ENDPOINTS + ep]) != HAL_OK) return false;
  owner->events &= ~USB_OTG_DOEPINT_XFRC;
  if (defaults) usbDeviceOutComplete(ep, length);
  return true;
}

// Release deactivates and unroutes a closed owner; activation or a route
// since then is OPEN's.
static bool reopened(uint8_t address)
{
  uintptr_t USBx_BASE = (uintptr_t)controller.Instance;
  uint8_t ep = address & 15U;
  uint32_t control = (address & 0x80U) ? USBx_INEP(ep)->DIEPCTL : USBx_OUTEP(ep)->DOEPCTL;
  return (control & USB_OTG_DIEPCTL_USBAEP) || (USBx_DEVICE->DAINTMSK & pcdBit(address));
}

usb_lifecycle_result_t usbPortQuiesce(uint8_t address, uint64_t operation, bool defaults,
                                      usb_lifecycle_slice_t *slice, usb_lifecycle_wait_t *wait)
{
  int32_t index = pcdIndex(address);
  *wait = USB_LIFECYCLE_WAIT_EVENT;
  if (index < 0) return USB_LIFECYCLE_ERROR;
  uint32_t mask = usbHidLock();
  PCD_QuiesceEndpoint *owner = &pcd_life.endpoint[index];
  uint8_t holder = pcd_life.fifo_owner;
  HAL_StatusTypeDef status = HAL_OK;
  // A new owner, or one whose gate RESTART reopened (it may be armed again),
  // runs a full NAK/disable/flush cycle; a released, still-gated owner that
  // kept its descriptor (DEFAULT_RESTART) or that OPEN reactivated (it cannot
  // arm one) is closed in place, once.
  bool begin = owner->operation != operation || owner->reset != pcd_life.reset ||
               owner->phase == PCD_QUIESCE_IDLE;
  bool close = !begin && !defaults && owner->phase == PCD_QUIESCE_DONE &&
               (pcd_life.gated & pcdBit(address)) &&
               (owner->disposition != PCD_QUIESCE_CLOSE || reopened(address));
  // A finished owner with nothing left to deliver, and a queued FIFO owner
  // (it waits for the holder's release, published as an event; the holder's
  // steps enforce the shared ticket deadline), take no step: re-observing them
  // would spend the slice ahead of the owners that still have work.
  if (!begin && !close &&
      ((owner->phase == PCD_QUIESCE_DONE && !(owner->events & USB_OTG_DIEPINT_XFRC)) ||
       (owner->phase == PCD_QUIESCE_FIFO && pcd_life.fifo_owner != PCD_LIFE_FIFO_NONE &&
        pcd_life.fifo_owner != (uint8_t)index))) {
    usb_lifecycle_result_t result = owner->phase == PCD_QUIESCE_DONE ? USB_LIFECYCLE_OK
                                                                      : USB_LIFECYCLE_PENDING;
    usbHidUnlock(mask);
    return result;
  }
  if (begin || close) {
    if (!sliceStep(slice, false)) {
      *wait = USB_LIFECYCLE_READY;
      status = HAL_BUSY;
    } else if (!begin) {
      status = HAL_PCD_QuiesceClose(&pcd_life, address, operation);
    } else if (HAL_PCD_QuiesceBegin(&pcd_life, address, operation, pcd_life.arm[index],
                                    defaults ? PCD_QUIESCE_DEFAULT_RESTART : PCD_QUIESCE_CLOSE,
                                    micros()) != HAL_OK) {
      // An endpoint this operation cannot own now fails closed, never waits.
      status = HAL_ERROR;
    }
  }
  usbHidUnlock(mask);
  // Each step is its own short transaction, so a FIFO or flush poll burst
  // never runs under PRIMASK; every HAL step rechecks the owner's operation
  // and reset identity.
  while (status == HAL_OK) {
    bool poll = fifoWait(index, owner->phase);
    if (!sliceStep(slice, poll)) {
      *wait = slice->parked ? USB_LIFECYCLE_WAIT_POLL : USB_LIFECYCLE_READY;
      status = HAL_BUSY;
      break;
    }
    mask = usbHidLock();
    uint8_t before = owner->phase;
    HAL_StatusTypeDef step = HAL_PCD_QuiesceStep(&pcd_life, address, operation, micros());
    bool delivered = deliverCaptured(address, owner, defaults);
    bool progress = owner->phase != before || delivered;
    usbHidUnlock(mask);
    if (!progress) {
      usbLifecycleSliceRefund(slice);
      if (poll) usbLifecycleSlicePolled(slice);
    }
    if (step != HAL_BUSY) {
      status = step;
      break;
    }
    // Without progress only a FIFO/flush wait is re-observed (polled); any
    // other wait ends on an interrupt-capable fact.
    if (!progress && !poll) status = HAL_BUSY;
  }
  mask = usbHidLock();
  // A released ticket is the queued owners' event.
  bool released = holder == (uint8_t)index && pcd_life.fifo_owner == PCD_LIFE_FIFO_NONE;
  regate();
  usbHidUnlock(mask);
  if (released) usbLifecyclePublish(USB_LIFECYCLE_FACT_EVENT);
  if (pcd_life.rx_barrier) pendOtg();
  switch (status) {
    case HAL_OK: return USB_LIFECYCLE_OK;
    case HAL_BUSY: return USB_LIFECYCLE_PENDING;
    case HAL_TIMEOUT: return USB_LIFECYCLE_TIMEOUT;
    default: return USB_LIFECYCLE_ERROR;
  }
}

bool usbPortOpenGate(uint8_t address, uint64_t operation)
{
  int32_t index = pcdIndex(address);
  if (index < 0) return false;
  uint32_t mask = usbHidLock();
  const PCD_QuiesceEndpoint *owner = &pcd_life.endpoint[index];
  bool ok = owner->operation != operation || owner->reset != pcd_life.reset ||
            !(pcd_life.gated & pcdBit(address)) ||
            HAL_PCD_LifecycleOpenGate(&pcd_life, address, operation) == HAL_OK;
  usbHidUnlock(mask);
  return ok;
}

bool usbPortReleaseGates(uint64_t operation)
{
  bool ok = true;
  for (uint8_t ep = 0U; ep < PCD_LIFE_ENDPOINTS; ++ep) {
    if (!usbPortOpenGate((uint8_t)(0x80U | ep), operation)) ok = false;
    if (!usbPortOpenGate(ep, operation)) ok = false;
  }
  return ok;
}

bool usbPortHalted(uint8_t address)
{
  return address & 0x80 ? controller.IN_ep[address & 15].is_stall != 0 :
                         controller.OUT_ep[address & 15].is_stall != 0;
}

bool usbPortAddress(uint8_t address)
{
  return checked(HAL_PCD_SetAddress(&controller, address), USB_FAULT_CONTROL, 0);
}

bool usbPortTest(uint8_t selector)
{
  return checked(HAL_PCD_SetTestMode(&controller, selector), USB_FAULT_CONTROL, 0);
}

static bool hardwareSuspended(void)
{
  return (registers()->DSTS & USB_OTG_DSTS_SUSPSTS) != 0;
}

bool usbPortSuspended(void) { return bus_suspended; }

uint16_t usbPortFrameNumber(void)
{
  if (!started) return 0;
  return (uint16_t)((registers()->DSTS & USB_OTG_DSTS_FNSOF) >> USB_OTG_DSTS_FNSOF_Pos);
}

static void reconcileActivity(bool endpoint_event)
{
  (void)HAL_PCD_ObserveReset(&controller);
  if (!usb_device.suspended || usb_device.reset_pending) return;
  __HAL_PCD_UNGATE_PHYCLOCK(&controller);
  if (hardwareSuspended()) return;
  bus_suspended = false;
  HAL_PCD_LifecycleSuspend(&pcd_life, 0U, micros());
  if (endpoint_event || resume_pending || wake_pending) {
    resume_pending = wake_pending = false;
    usbDeviceResume();
  }
}

// Raw reset, disconnect or authorized shutdown: one new identity retires
// admission, control and every owner; the PendSV join alone proves physical
// cleanup. Never flushes. The PHY clock is ungated before any handshake.
static void capture(bool shutdown_admit, uint32_t anchor_us)
{
  __HAL_PCD_UNGATE_PHYCLOCK(&controller);
  uint32_t mask = usbHidLock();
  ++reset_counter;
  if (shutdown_admit) HAL_PCD_LifecycleShutdownBegin(&pcd_life, reset_counter, anchor_us);
  else HAL_PCD_LifecycleResetBegin(&pcd_life, reset_counter, micros());
  usbLifecycleResetBegin(&usb_lifecycle, reset_counter);
  control_retire = 0U;
  bus_suspended = false;
  resume_pending = wake_pending = false;
  resume_skip_sof = false;
  usbDeviceResetBegin();
  usbHidUnlock(mask);
}

void HAL_PCD_ResetBeginCallback(PCD_HandleTypeDef *handle)
{
  (void)handle;
  capture(false, 0U);
}

void usbPortShutdown(uint32_t anchor_us)
{
  uint32_t mask = usbHidLock();
  if (shutdown == USB_SHUTDOWN_NONE) {
    // A controller never started was never exposed to a host.
    shutdown = started ? USB_SHUTDOWN_PENDING : USB_SHUTDOWN_OK;
    if (started) capture(true, anchor_us);
  }
  usbHidUnlock(mask);
  usbLifecyclePublish(USB_LIFECYCLE_FACT_EVENT);
}

usb_shutdown_t usbPortShutdownResult(void)
{
  return shutdown;
}

// Caller holds the transport lock. Success and failure both end isolated;
// failed owners stay retained until the MCU reset.
static void finishShutdown(HAL_StatusTypeDef status)
{
  isolateOtg();
  started = false;
  if (status != HAL_OK) usbDeviceFault(USB_FAULT_STOP, 0xff, (uint8_t)status);
  shutdown = status == HAL_OK ? USB_SHUTDOWN_OK : USB_SHUTDOWN_FAILED;
}

usb_shutdown_t usbPortShutdownEscape(void)
{
  uint32_t mask = usbHidLock();
  if (shutdown == USB_SHUTDOWN_PENDING) {
    HAL_PCD_LifecycleShutdownIsolate(&pcd_life);
    finishShutdown(HAL_TIMEOUT);
  }
  usb_shutdown_t result = shutdown;
  usbHidUnlock(mask);
  return result;
}

// Design 6: a co-pending status completion belongs to the request it ends,
// e.g. SET_ADDRESS before the next SETUP replaces the control identity. The
// data-stage completion of a superseded request is acknowledged only: it arms
// neither the next packet nor a terminating ZLP.
static void reconcileControlIn(void)
{
  USB_OTG_GlobalTypeDef *USBx = controller.Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  if (!(pcd_life.gated & pcdBit(0x80U)) && (USBx_DEVICE->DIEPMSK & USB_OTG_DIEPMSK_XFRCM) &&
      (USBx_INEP(0U)->DIEPINT & USB_OTG_DIEPINT_XFRC)) {
    USBx_DEVICE->DIEPEMPMSK &= ~1U;
    CLEAR_IN_EP_INTR(0U, USB_OTG_DIEPINT_XFRC);
    if (!usb_device.reset_pending) usbControlStatusComplete();
  }
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *handle)
{
  uint32_t mask = usbHidLock();
  reconcileActivity(true);
  reconcileControlIn();
  // Replacement: old IN data still owned by EP0 is retired before the new
  // request may arm it; the capture below is held until then.
  uintptr_t USBx_BASE = (uintptr_t)controller.Instance;
  if (USBx_INEP(0U)->DIEPCTL & (USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_EPDIS))
    (void)usbPortRetireControlIn();
  usbDeviceSetup((const uint8_t *)handle->Setup);
  usbHidUnlock(mask);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *handle, uint8_t endpoint)
{
  (void)handle;
  reconcileActivity(true);
  usbDeviceInComplete(endpoint);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *handle, uint8_t endpoint)
{
  reconcileActivity(true);
  usbDeviceOutComplete(endpoint, HAL_PCD_EP_GetRxCount(handle, endpoint));
}

// PRIMASK held. Raise only; an unmasking rise discards the latched flag first,
// so the first SOF served afterwards is one received after the raise.
static void sofRaiseLocked(uint8_t sources)
{
  sof_demand |= sources;
  if (sof_demand && !sof_unmasked && started && shutdown == USB_SHUTDOWN_NONE) {
    __HAL_PCD_CLEAR_FLAG(&controller, USB_OTG_GINTSTS_SOF);
    controller.Instance->GINTMSK |= USB_OTG_GINTMSK_SOFM;
    sof_unmasked = true;
  }
}

// The settle (contract S1): raises every duty whose predicate holds. Called
// with PRIMASK clear by usbHidUnlock after its restore and directly at the end
// of each OTG dispatch, in the SOF callback and at the end of each PendSV
// slice. The predicates only read; a stale read costs at most one SOF.
void usbHidPortSettle(void)
{
  uint8_t wanted = usb_device.suspended ? USB_SOF_SUSPEND : 0U;
  if (usbHidTransportFrameWanted()) wanted |= USB_SOF_HID;
#if HW_USB_CDC
  if (usbCdcFrameWanted()) wanted |= USB_SOF_CDC;
#endif
  uint32_t mask = usbHidLock();
  sofRaiseLocked(wanted);
  usb_settle_seq++;
  usbHidRestore(mask);
}

// Callers are the OTG ISR, PendSV or a section nested in another lock, so the
// release only restores.
void usbPortSofDemand(uint8_t sources, bool wanted)
{
  uint32_t mask = usbHidLock();
  if (wanted) sofRaiseLocked(sources);
  else sof_demand &= (uint8_t)~sources;
  usbHidUnlock(mask);
}

// No return: every path must reach the tail, which clears the served SOF
// flag, or the OTG interrupt would re-enter on it at once.
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *handle)
{
  (void)handle;
  if (resume_skip_sof) {
    resume_skip_sof = false;
    (void)HAL_PCD_ObserveReset(&controller);
  } else reconcileActivity(false);
  usbPortSofDemand(USB_SOF_SUSPEND, usb_device.suspended);
  // A Resume completed above is served in this SOF.
  usbHidPortSettle();
  usbDeviceSof(sof_demand);
  uint32_t mask = usbHidLock();
  // Mask, then clear the served flag, in one section: the line falls without
  // an edge, and no handler can unmask between the two.
  if (!sof_demand) {
    controller.Instance->GINTMSK &= ~USB_OTG_GINTMSK_SOFM;
    sof_unmasked = false;
  }
  __HAL_PCD_CLEAR_FLAG(&controller, USB_OTG_GINTSTS_SOF);
  usbHidUnlock(mask);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *handle)
{
  (void)HAL_PCD_ObserveReset(handle);
  if (usb_device.reset_pending) return;
  if (wake_pending)
    checked(HAL_PCD_DeActivateRemoteWakeup(handle), USB_FAULT_CONTROL, 0);
  resume_pending = wake_pending = false;
  resume_skip_sof = false;
  bus_suspended = true;
  if (!usb_device.suspended) suspended_at = millis();
  uint32_t mask = usbHidLock();
  __HAL_PCD_CLEAR_FLAG(handle, USB_OTG_GINTSTS_SOF);
  usbHidUnlock(mask);
  usbDeviceSuspend();
  // Owners pause; a FIFO command in flight stays observable until released.
  HAL_PCD_LifecycleSuspend(&pcd_life, 1U, micros());
  regate();
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *handle)
{
  __HAL_PCD_UNGATE_PHYCLOCK(handle);
  if (usb_device.reset_pending) return;
  resume_pending = true;
  resume_skip_sof = hardwareSuspended() && (handle->Instance->GINTSTS & USB_OTG_GINTSTS_SOF);
  reconcileActivity(false);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *handle) { (void)handle; }
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *handle)
{
  (void)handle;
  capture(false, 0U);
  usbDeviceDisconnect();
}

void usbPortIrq(void)
{
  HAL_PCD_IRQHandler(&controller);
  // Once per dispatch, after every endpoint pass and successor arm.
  usbHidPortSettle();
  if (pcd_life.facts) {
    pcd_life.facts = 0U;
    usbLifecyclePublish(USB_LIFECYCLE_FACT_EVENT);
  }
}

static void serviceControlRetire(usb_lifecycle_slice_t *slice)
{
  // An unchanged FIFO/flush wait is re-observed as a poll read, also at the
  // start of a slice; any other unchanged wait ends on an interrupt fact.
  while (control_retire && sliceStep(slice, fifoWait(0, pcd_life.endpoint[0].phase))) {
    uint32_t mask = usbHidLock();
    uint8_t before = pcd_life.endpoint[0].phase;
    HAL_StatusTypeDef status = HAL_PCD_QuiesceStep(&pcd_life, 0x80U, control_retire, micros());
    uint8_t phase = pcd_life.endpoint[0].phase;
    if (status == HAL_OK) status = HAL_PCD_LifecycleOpenGate(&pcd_life, 0x80U, control_retire);
    if (status == HAL_OK) {
      control_retire = 0U;
      usbLifecycleHoldControl(&usb_lifecycle, false);
      usbControlRetired();
    } else if (status != HAL_BUSY) {
      // Retained: the failed owner keeps EP0 held until a raw reset.
      control_retire = 0U;
      usbDeviceFault(USB_FAULT_CONTROL, 0x80, (uint8_t)status);
    }
    regate();
    usbHidUnlock(mask);
    if (status == HAL_BUSY && phase == before) {
      usbLifecycleSliceRefund(slice);
      if (!fifoWait(0, phase)) return;
      usbLifecycleSlicePolled(slice);
    }
  }
}

// A join step that may observe a FIFO or flush wait (no interrupt ends one) is
// a poll read: the FIFO-command phases, and RETIRE while one of its owners
// waits in FIFO or FLUSH.
static bool resetPoll(void)
{
  uint8_t phase = pcd_life.reset_phase;
  if (phase == PCD_RESET_INHERIT || phase == PCD_RESET_TX || phase == PCD_RESET_TX_FLUSH ||
      phase == PCD_RESET_RX || phase == PCD_RESET_RX_FLUSH) return true;
  if (phase != PCD_RESET_RETIRE) return false;
  for (unsigned i = 0U; i < 2U * PCD_LIFE_ENDPOINTS; ++i) {
    const PCD_QuiesceEndpoint *owner = &pcd_life.endpoint[i];
    if (owner->reset == pcd_life.reset &&
        (owner->phase == PCD_QUIESCE_FIFO || owner->phase == PCD_QUIESCE_FLUSH)) return true;
  }
  return false;
}

// True while the raw-reset or shutdown join owns the controller; nothing else
// runs then. A shutdown never ends the reset hold: no SETUP dispatches again.
static bool serviceReset(usb_lifecycle_slice_t *slice)
{
  while (usb_lifecycle.resetting) {
    // Each join step is at most one transition (one phase, Begin or owner
    // phase change), refunded when it changed nothing.
    if (!sliceStep(slice, resetPoll())) return true;
    uint8_t wait = 0U;
    HAL_StatusTypeDef status = HAL_PCD_LifecycleResetStep(&pcd_life, micros(), &wait);
    if (wait & PCD_RESET_WAIT_BARRIER) pendOtg();
    if (status == HAL_BUSY) {
      if (!(wait & PCD_RESET_WAIT_READY)) {
        usbLifecycleSliceRefund(slice);
        if (!(wait & PCD_RESET_WAIT_POLL)) return true;
        usbLifecycleSlicePolled(slice);
      }
      continue;
    }
    uint32_t mask = usbHidLock();
    if (shutdown == USB_SHUTDOWN_PENDING) {
      finishShutdown(status);
      usbHidUnlock(mask);
      return true;
    }
    if (usbLifecycleResetEnd(&usb_lifecycle, pcd_life.reset, status == HAL_OK)) {
      if (status == HAL_OK) {
        usbDeviceResetComplete(controller.Init.speed == PCD_SPEED_HIGH);
      } else {
        usbDeviceFault(USB_FAULT_FLUSH, 0xff, (uint8_t)status);
        usb_device.retained = true;
      }
    }
    usbHidUnlock(mask);
  }
  return false;
}

void usbPortService(void)
{
  uint32_t facts = usbLifecycleSliceBegin();
  if (!facts) {
    usbLifecycleSliceEnd(0U);
    return;
  }
  usb_lifecycle_slice_t slice;
  usbLifecycleSliceStart(&slice, micros());
  if (started && !serviceReset(&slice)) {
    serviceControlRetire(&slice);
    usbDeviceService(&slice);
  }
  // Once per slice, after every executor step and SETUP dispatch.
  usbHidPortSettle();
  if (slice.parked) usbLifecycleParkPoll();
  usbLifecycleSliceEnd(slice.more ? USB_LIFECYCLE_FACT_EVENT : 0U);
}

void usbPortTick(void)
{
  // ENUMDNE after a completed cleanup is an event wait, not a tick-paced one.
  uint8_t phase = pcd_life.reset_phase;
  usbLifecycleTick(started && (usb_lifecycle.active || control_retire != 0U ||
                               (phase != PCD_RESET_IDLE && phase != PCD_RESET_ENUM &&
                                phase != PCD_RESET_FAILED)));
}

bool usbPortWake(void)
{
  if (__get_IPSR() || __get_PRIMASK()) return false;
  uint32_t mask = usbHidLock();
  if (!usb_device.configured || !usb_device.suspended || !usb_device.wake_enabled ||
      usb_device.reset_pending || (uint32_t)(millis() - suspended_at) < 5 || !hardwareSuspended()) {
    usbHidUnlock(mask);
    return false;
  }
  uint32_t epoch = usb_device.epoch;
  uint32_t interrupt = controller.Instance->GINTMSK & USB_OTG_GINTMSK_WUIM;
  controller.Instance->GINTMSK &= ~USB_OTG_GINTMSK_WUIM;
  __HAL_PCD_UNGATE_PHYCLOCK(&controller);
  __HAL_PCD_CLEAR_FLAG(&controller, USB_OTG_GINTSTS_SOF);
  wake_pending = true;
  bool signalled = checked(HAL_PCD_ActivateRemoteWakeup(&controller), USB_FAULT_CONTROL, 0) &&
                   (registers()->DCTL & USB_OTG_DCTL_RWUSIG) != 0;
  usbHidUnlock(mask);
  if (signalled) delay(10);
  mask = usbHidLock();
  bool same_session = epoch == usb_device.epoch && !usb_device.reset_pending;
  bool ended = true;
  if (same_session) {
    ended = checked(HAL_PCD_DeActivateRemoteWakeup(&controller), USB_FAULT_CONTROL, 0);
    if (hardwareSuspended()) __HAL_PCD_CLEAR_FLAG(&controller, USB_OTG_GINTSTS_WKUINT);
  }
  controller.Instance->GINTMSK |= interrupt;
  if (!signalled || !same_session) wake_pending = false;
  usbHidUnlock(mask);
  return signalled && ended && same_session;
}
