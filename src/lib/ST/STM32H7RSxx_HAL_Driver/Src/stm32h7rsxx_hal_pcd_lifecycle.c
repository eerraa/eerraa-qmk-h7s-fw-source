/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 era (@eerraa).
 * Project PCD extension: RM0477 device endpoint retirement and slave RX framing.
 * Runtime routing is adopted with the single project lifecycle cutover.
 */
#include "stm32h7rsxx_hal.h"

#if defined(HAL_PCD_MODULE_ENABLED) && (defined(USB_OTG_FS) || defined(USB_OTG_HS))
#include "stm32h7rsxx_hal_pcd_lifecycle.h"

#define PCD_FIFO_NONE PCD_LIFE_FIFO_NONE
#define PCD_FIFO_RESET 0xfeU
#define PCD_RESET_OPERATION (1ULL << 63)

enum {
  LIFE_VALID = 1U, LIFE_DEACTIVATE = 2U, LIFE_CLEAR = 4U,
  LIFE_RESTORE_IRQ = 8U, LIFE_KEEP_GATE = 16U, LIFE_KEEP_NAK = 32U
};

static uint32_t lifeReleasePolicy(uint8_t address, PCD_QuiesceDisposition disposition)
{
  if (address == 0U) return 0U;
  switch (disposition) {
    case PCD_QUIESCE_CLOSE:
      return (address & 15U) ? LIFE_VALID | LIFE_DEACTIVATE | LIFE_CLEAR : 0U;
    case PCD_QUIESCE_ABORT:
      return LIFE_VALID | LIFE_RESTORE_IRQ;
    case PCD_QUIESCE_DEFAULT_RESTART:
      return (address & 15U) ? LIFE_VALID | LIFE_DEACTIVATE : 0U;
    case PCD_QUIESCE_EP0_RELEASE:
      return address == 0x80U ? LIFE_VALID | LIFE_CLEAR | LIFE_RESTORE_IRQ : 0U;
    case PCD_QUIESCE_CONTROLLER_RETIRE:
      return LIFE_VALID | LIFE_DEACTIVATE | LIFE_CLEAR | LIFE_KEEP_GATE | LIFE_KEEP_NAK;
    default:
      return 0U;
  }
}

static int32_t lifeIndex(const PCD_LifecycleTypeDef *life, uint8_t address)
{
  uint8_t ep = address & 15U;
  if (life == NULL || life->handle == NULL || (address & 0x70U) != 0U ||
      ep >= PCD_LIFE_ENDPOINTS || ep >= life->handle->Init.dev_endpoints)
    return -1;
  return (int32_t)(ep + ((address & 0x80U) ? 0U : PCD_LIFE_ENDPOINTS));
}

static uint32_t lifeBit(uint8_t address)
{
  return 1UL << ((address & 15U) + ((address & 0x80U) ? 0U : 16U));
}

static uint8_t lifeFifoIdle(const USB_OTG_GlobalTypeDef *USBx)
{
  return (USBx->GRSTCTL & (USB_OTG_GRSTCTL_AHBIDL | USB_OTG_GRSTCTL_TXFFLSH |
                           USB_OTG_GRSTCTL_RXFFLSH | USB_OTG_GRSTCTL_CSRST)) ==
         USB_OTG_GRSTCTL_AHBIDL;
}

static HAL_StatusTypeDef lifeLock(PCD_LifecycleTypeDef *life, uint32_t *mask)
{
  *mask = __get_PRIMASK();
  __disable_irq();
  if (life->handle->Lock == HAL_LOCKED) {
    __set_PRIMASK(*mask);
    return HAL_BUSY;
  }
  life->handle->Lock = HAL_LOCKED;
  return HAL_OK;
}

static void lifeUnlock(PCD_LifecycleTypeDef *life, uint32_t mask)
{
  life->handle->Lock = HAL_UNLOCKED;
  __set_PRIMASK(mask);
}

static HAL_StatusTypeDef lifeFail(PCD_QuiesceEndpoint *owner, HAL_StatusTypeDef result)
{
  if (owner->phase != PCD_QUIESCE_FAILED) {
    owner->failed_phase = owner->phase;
    owner->failure = (uint8_t)result;
    owner->phase = PCD_QUIESCE_FAILED;
  }
  return (HAL_StatusTypeDef)owner->failure;
}

static uint8_t lifeHandshake(uint8_t phase, uint8_t in)
{
  return phase == PCD_QUIESCE_IN_NAK || phase == PCD_QUIESCE_IN_DISABLE ||
         phase == PCD_QUIESCE_OUT_NAK || phase == PCD_QUIESCE_OUT_RX ||
         phase == PCD_QUIESCE_DISABLED || (!in && phase == PCD_QUIESCE_RELEASE);
}

static HAL_StatusTypeDef lifeCharge(PCD_QuiesceEndpoint *owner, uint32_t now, uint8_t in)
{
  uint32_t elapsed = now - owner->observed_us;
  owner->observed_us = now;
  if (!lifeHandshake(owner->phase, in)) return HAL_OK;
  if (elapsed >= owner->remaining_us) return lifeFail(owner, HAL_TIMEOUT);
  owner->remaining_us -= elapsed;
  return HAL_OK;
}

/* Use the IRQ's capture/acknowledgement authority inside command transactions,
 * too: an interrupt mask cannot stop hardware completing between phases. */
static uint32_t lifeCaptureRaw(PCD_LifecycleTypeDef *life, uint8_t address,
                               PCD_QuiesceEndpoint *owner)
{
  USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  uint8_t ep = address & 15U;
  uint32_t flags = (address & 0x80U) ? USBx_INEP(ep)->DIEPINT : USBx_OUTEP(ep)->DOEPINT;
  return HAL_PCD_QuiesceObserve(life, address, owner->reset, owner->arm,
                                flags & (USB_OTG_DIEPINT_XFRC | USB_OTG_DIEPINT_EPDISD));
}

/* INEPNE is a one-shot event (BRICK60 HIL): one W1C and re-read per
 * observation. A source that reasserts like a level is contained
 * endpoint-locally, owned or not: DAINTMSK isolation of that endpoint only and
 * a retained failure record that a raw reset alone replaces. A non-owned
 * endpoint is also gated, so nothing arms it again. */
static HAL_StatusTypeDef lifeRetireInNak(PCD_LifecycleTypeDef *life, uint8_t address,
                                        PCD_QuiesceEndpoint *owner, uint8_t owned)
{
  USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  uint8_t ep = address & 15U;
  USBx_INEP(ep)->DIEPINT = USB_OTG_DIEPINT_INEPNE;
  if (USBx_INEP(ep)->DIEPINT & USB_OTG_DIEPINT_INEPNE) {
    if (owned) {
      lifeCaptureRaw(life, address, owner);
    } else {
      *owner = (PCD_QuiesceEndpoint){0};
      owner->reset = life->reset;
      owner->arm = life->arm[ep];
      owner->phase = PCD_QUIESCE_FAILED;
      owner->failed_phase = PCD_QUIESCE_FAILED;
      owner->failure = (uint8_t)HAL_ERROR;
      life->gated |= lifeBit(address);
    }
    USBx_DEVICE->DAINTMSK &= ~(1UL << ep);
    owner->isolated = 1U;
    return lifeFail(owner, HAL_ERROR);
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_PCD_LifecycleInit(PCD_LifecycleTypeDef *life,
                                      PCD_HandleTypeDef *handle, uint64_t reset)
{
  if (life == NULL || handle == NULL || handle->Instance == NULL || reset == 0U ||
      handle->Init.dev_endpoints == 0U || handle->Init.dev_endpoints > PCD_LIFE_ENDPOINTS ||
      handle->Init.dma_enable != 0U) return HAL_ERROR;
  if (life->handle != NULL) return HAL_BUSY;
  *life = (PCD_LifecycleTypeDef){0};
  life->handle = handle;
  life->reset = reset;
  life->fifo_owner = PCD_FIFO_NONE;
  handle->pData = life;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_PCD_LifecycleBindRx(PCD_LifecycleTypeDef *life, uint8_t ep,
                                        uint8_t *base, uint32_t capacity, uint64_t arm)
{
  int32_t index = lifeIndex(life, ep);
  if (index < 0 || ep >= PCD_LIFE_ENDPOINTS || base == NULL || arm == 0U ||
      capacity == 0U || capacity > PCD_LIFE_RX_BYTES) return HAL_ERROR;
  uint32_t mask;
  if (lifeLock(life, &mask) != HAL_OK) return HAL_BUSY;
  HAL_StatusTypeDef result = HAL_BUSY;
  USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  if (!(life->gated & lifeBit(ep)) && !(life->rx_debt & (1UL << ep)) &&
      !life->rx_poisoned && life->fifo_owner == PCD_FIFO_NONE &&
      life->reset_phase == PCD_RESET_IDLE &&
      !(USBx_OUTEP(ep)->DOEPCTL & (USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_EPDIS)) &&
      !(USBx_OUTEP(ep)->DOEPINT & (USB_OTG_DOEPINT_XFRC | USB_OTG_DOEPINT_EPDISD)) &&
      arm != life->arm[index]) {
    life->receive[ep] = (PCD_RxAllocation){base, capacity, 0U, life->reset, arm};
    life->arm[index] = arm;
    life->handle->OUT_ep[ep].xfer_buff = base;
    life->handle->OUT_ep[ep].xfer_count = 0U;
    result = HAL_OK;
  }
  lifeUnlock(life, mask);
  return result;
}

HAL_StatusTypeDef HAL_PCD_LifecycleBindIn(PCD_LifecycleTypeDef *life, uint8_t ep,
                                        uint64_t arm)
{
  if (ep >= PCD_LIFE_ENDPOINTS || arm == 0U) return HAL_ERROR;
  int32_t index = lifeIndex(life, ep | 0x80U);
  if (index < 0) return HAL_ERROR;
  uint32_t mask;
  if (lifeLock(life, &mask) != HAL_OK) return HAL_BUSY;
  HAL_StatusTypeDef result = HAL_BUSY;
  USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  if (!(life->gated & lifeBit(ep | 0x80U)) && life->fifo_owner == PCD_FIFO_NONE &&
      life->reset_phase == PCD_RESET_IDLE &&
      !(USBx_INEP(ep)->DIEPCTL & (USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_EPDIS)) &&
      !(USBx_INEP(ep)->DIEPINT & (USB_OTG_DIEPINT_XFRC | USB_OTG_DIEPINT_EPDISD)) &&
      arm != life->arm[index]) {
    life->arm[index] = arm;
    result = HAL_OK;
  }
  lifeUnlock(life, mask);
  return result;
}

/* The first OUT user's global OUT NAK. A NAK already in effect is adopted, but
 * GONAKEFF can still read effective after this engine's own CGONAK until the
 * core takes it: acquisition then waits (OUT_NAK, re-checked every step) for
 * the clear, so a stale GONAKEFF is never adopted and SGONAK never races the
 * clear. */
static void lifeAcquireOutNak(PCD_LifecycleTypeDef *life)
{
  USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  uint8_t effective = (USBx->GINTSTS & USB_OTG_GINTSTS_BOUTNAKEFF) != 0U;
  if (life->nak_clearing && effective) return;
  life->nak_clearing = 0U;
  life->nak_effective = effective;
  life->nak_acquired = !effective;
  if (life->nak_acquired) {
    USBx->GINTMSK |= USB_OTG_GINTMSK_GONAKEFFM;
    USBx_DEVICE->DCTL |= USB_OTG_DCTL_SGONAK;
  } else {
    USBx->GINTMSK &= ~USB_OTG_GINTMSK_GONAKEFFM;
  }
}

/* Caller holds the transaction lock and has validated address/disposition. */
static HAL_StatusTypeDef lifeBeginLocked(PCD_LifecycleTypeDef *life, uint8_t address,
                                       int32_t index, uint64_t operation, uint64_t arm,
                                       PCD_QuiesceDisposition disposition, uint32_t now_us)
{
  PCD_QuiesceEndpoint *owner = &life->endpoint[index];
  HAL_StatusTypeDef result = HAL_BUSY;
  if ((owner->phase == PCD_QUIESCE_IDLE || owner->phase == PCD_QUIESCE_DONE) &&
      arm == life->arm[index] && !(life->gated & lifeBit(address)) &&
      ((address & 0x80U) || !life->nak_users ||
       (life->nak_operation == operation && life->nak_reset == life->reset))) {
    USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
    uintptr_t USBx_BASE = (uintptr_t)USBx;
    uint8_t ep = address & 15U;
    *owner = (PCD_QuiesceEndpoint){0};
    owner->operation = operation;
    owner->reset = life->reset;
    owner->arm = arm;
    owner->disposition = (uint8_t)disposition;
    owner->remaining_us = PCD_LIFE_HANDSHAKE_US;
    owner->observed_us = now_us;
    owner->daint_mask = USBx_DEVICE->DAINTMSK & lifeBit(address);
    life->gated |= lifeBit(address);
    if (address & 0x80U) {
      owner->phase = PCD_QUIESCE_IN_NAK;
      lifeCaptureRaw(life, address, owner);
      uint32_t ctl = USBx_INEP(ep)->DIEPCTL;
      USBx_DEVICE->DIEPEMPMSK &= ~(1UL << ep);
      USBx_DEVICE->DIEPMSK |= USB_OTG_DIEPMSK_EPDM;
      owner->disable_owned = (ctl & USB_OTG_DIEPCTL_EPDIS) != 0U;
      if (owner->disable_owned) owner->phase = PCD_QUIESCE_DISABLED;
      else if (!(ctl & USB_OTG_DIEPCTL_EPENA)) owner->phase = PCD_QUIESCE_FIFO;
      else if (ctl & USB_OTG_DIEPCTL_NAKSTS) {
        /* NAK already in effect (e.g. core-set on EP0 SETUP) raises no fresh
         * INEPNE: disable directly, as Zephyr udc_dwc2 (d590d27) does. */
        owner->events &= ~USB_OTG_DIEPINT_EPDISD;
        USBx_INEP(ep)->DIEPCTL = (ctl & ~USB_OTG_DIEPCTL_CNAK) | USB_OTG_DIEPCTL_SNAK;
        owner->phase = PCD_QUIESCE_IN_DISABLE;
      } else {
        owner->events &= ~USB_OTG_DIEPINT_EPDISD;
        USBx_INEP(ep)->DIEPINT = USB_OTG_DIEPINT_INEPNE;
        USBx_INEP(ep)->DIEPCTL = (ctl & ~USB_OTG_DIEPCTL_CNAK) | USB_OTG_DIEPCTL_SNAK;
        owner->phase = PCD_QUIESCE_IN_NAK;
      }
    } else {
      owner->phase = PCD_QUIESCE_OUT_NAK;
      lifeCaptureRaw(life, address, owner);
      USBx_OUTEP(ep)->DOEPCTL |= USB_OTG_DOEPCTL_SNAK;
      if (life->nak_users == 0U) {
        life->nak_operation = operation;
        life->nak_reset = life->reset;
        life->nak_mask = USBx->GINTMSK & USB_OTG_GINTMSK_GONAKEFFM;
        lifeAcquireOutNak(life);
      }
      life->nak_users |= (uint16_t)(1U << ep);
      life->rx_empty = 0U;
      owner->disable_owned = (USBx_OUTEP(ep)->DOEPCTL & USB_OTG_DOEPCTL_EPDIS) != 0U;
      owner->phase = PCD_QUIESCE_OUT_NAK;
    }
    result = HAL_OK;
  }
  return result;
}

HAL_StatusTypeDef HAL_PCD_QuiesceBegin(PCD_LifecycleTypeDef *life, uint8_t address,
                                     uint64_t operation, uint64_t arm,
                                     PCD_QuiesceDisposition disposition, uint32_t now_us)
{
  int32_t index = lifeIndex(life, address);
  /* Controller retirement belongs to the raw-reset join's new identity. */
  if (index < 0 || operation == 0U || disposition == PCD_QUIESCE_CONTROLLER_RETIRE ||
      !(lifeReleasePolicy(address, disposition) & LIFE_VALID)) return HAL_ERROR;
  PCD_EPTypeDef *descriptor = (address & 0x80U) ?
    &life->handle->IN_ep[address & 15U] : &life->handle->OUT_ep[address & 15U];
  if (descriptor->type == EP_TYPE_ISOC) return HAL_ERROR;
  uint32_t mask;
  if (lifeLock(life, &mask) != HAL_OK) return HAL_BUSY;
  /* The raw-reset join owns every endpoint until its cleanup completes. */
  HAL_StatusTypeDef result = life->reset_phase != PCD_RESET_IDLE ? HAL_BUSY :
    lifeBeginLocked(life, address, index, operation, arm, disposition, now_us);
  lifeUnlock(life, mask);
  return result;
}

uint32_t HAL_PCD_QuiesceObserve(PCD_LifecycleTypeDef *life, uint8_t address,
                              uint64_t reset, uint64_t arm, uint32_t flags)
{
  int32_t index = lifeIndex(life, address);
  if (index < 0) return 0U;
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  PCD_QuiesceEndpoint *owner = &life->endpoint[index];
  uint32_t captured = 0U;
  if (reset == life->reset && arm == life->arm[index]) {
    USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
    uintptr_t USBx_BASE = (uintptr_t)USBx;
    uint8_t ep = address & 15U;
    uint8_t owned = owner->reset == reset && owner->arm == arm &&
                    owner->phase != PCD_QUIESCE_IDLE &&
                    (owner->phase != PCD_QUIESCE_DONE || (life->gated & lifeBit(address)));
    if (owned) {
      captured = flags & (USB_OTG_DIEPINT_XFRC | USB_OTG_DIEPINT_EPDISD);
      owner->events |= captured;
      if (!(address & 0x80U) && (captured & USB_OTG_DOEPINT_XFRC)) {
        life->rx_debt |= 1UL << ep;
        life->rx_empty = 0U;
      }
      if (address & 0x80U) USBx_INEP(ep)->DIEPINT = captured;
      else USBx_OUTEP(ep)->DOEPINT = captured;
    }
    if (address & 0x80U) {
      if (flags & USB_OTG_DIEPINT_INEPNE) {
        uint8_t requested = owned && owner->phase == PCD_QUIESCE_IN_NAK &&
                            (USBx_INEP(ep)->DIEPCTL & USB_OTG_DIEPCTL_NAKSTS);
        if (lifeRetireInNak(life, address, owner, owned) == HAL_OK && requested)
          owner->events |= USB_OTG_DIEPINT_INEPNE;
        captured |= USB_OTG_DIEPINT_INEPNE;
      }
    }
    if (owned && captured) life->facts = 1U;
  }
  __set_PRIMASK(mask);
  return captured;
}

uint32_t HAL_PCD_LifecycleObserveOutNak(PCD_LifecycleTypeDef *life, uint64_t reset,
                                       uint64_t ticket, uint32_t flags)
{
  if (life == NULL || life->handle == NULL || __get_IPSR() == 0U) return 0U;
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  uint32_t captured = 0U;
  if (reset == life->reset && reset == life->nak_reset &&
      ticket == life->nak_operation && life->nak_users != 0U &&
      (flags & USB_OTG_GINTSTS_BOUTNAKEFF)) {
    life->nak_effective = 1U;
    life->handle->Instance->GINTMSK &= ~USB_OTG_GINTMSK_GONAKEFFM;
    captured = USB_OTG_GINTSTS_BOUTNAKEFF;
    life->facts = 1U;
  }
  __set_PRIMASK(mask);
  return captured;
}

static HAL_StatusTypeDef lifeStepLocked(PCD_LifecycleTypeDef *life, uint8_t address,
                                      int32_t index, uint64_t operation, uint32_t now_us)
{
  PCD_QuiesceEndpoint *owner = &life->endpoint[index];
  USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  uint8_t ep = address & 15U;
  uint8_t in = (address & 0x80U) != 0U;
  PCD_EPTypeDef *descriptor = in ? &life->handle->IN_ep[ep] : &life->handle->OUT_ep[ep];
  /* Controller retirement follows a new reset identity, so the parser already
   * discards every old allocation's payload. Old debt and RX poison end at the
   * join's own flush and RX-empty barrier, never by delivery or this owner. */
  uint8_t drained = owner->disposition == PCD_QUIESCE_CONTROLLER_RETIRE;
  HAL_StatusTypeDef result = HAL_BUSY;
  if (owner->operation != operation || owner->reset != life->reset ||
      (owner->phase != PCD_QUIESCE_DONE && owner->arm != life->arm[index])) {
    result = HAL_ERROR;
    goto done;
  }
  if (owner->phase == PCD_QUIESCE_DONE) { result = HAL_OK; goto done; }
  if (owner->phase == PCD_QUIESCE_FAILED) {
    result = (HAL_StatusTypeDef)owner->failure;
    goto done;
  }
  if (owner->phase == PCD_QUIESCE_IDLE) {
    result = HAL_ERROR;
    goto done;
  }
  /* A queued FIFO phase shares the held ticket's deadline, not a new allowance. */
  if (life->fifo_owner != PCD_FIFO_NONE &&
      (life->fifo_owner == (uint8_t)index || owner->phase == PCD_QUIESCE_FIFO) &&
      (uint32_t)(now_us - life->fifo_started_us) >= PCD_LIFE_FIFO_US) {
    result = lifeFail(owner, HAL_TIMEOUT);
    goto done;
  }
  /* Suspend pauses the handshakes and FIFO-ticket acquisition: no GRSTCTL
   * command starts while the PHY clock may be gated (design T7). A ticket
   * held at suspend kept the clock running, so its holder completes. */
  if (life->suspended && owner->phase != PCD_QUIESCE_RELEASE &&
      (lifeHandshake(owner->phase, in) ||
       (owner->phase == PCD_QUIESCE_FIFO && life->fifo_owner != (uint8_t)index))) goto done;
  if (!life->suspended && lifeCharge(owner, now_us, in) != HAL_OK) {
    result = HAL_TIMEOUT;
    goto done;
  }
  switch (owner->phase) {
    case PCD_QUIESCE_IN_NAK: {
      lifeCaptureRaw(life, address, owner);
      uint32_t ctl = USBx_INEP(ep)->DIEPCTL;
      if (!(ctl & (USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_EPDIS)))
        owner->phase = PCD_QUIESCE_FIFO;
      else if ((ctl & USB_OTG_DIEPCTL_NAKSTS) &&
               ((owner->events | USBx_INEP(ep)->DIEPINT) & USB_OTG_DIEPINT_INEPNE))
        owner->phase = PCD_QUIESCE_IN_DISABLE;
      break;
    }
    case PCD_QUIESCE_IN_DISABLE: {
      lifeCaptureRaw(life, address, owner);
      uint32_t ctl = USBx_INEP(ep)->DIEPCTL;
      if (!(ctl & (USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_EPDIS))) {
        owner->phase = PCD_QUIESCE_FIFO;
        break;
      }
      /* The observed NAK event is retired before the RM's SNAK|EPDIS command:
       * an INEPNE that command raises is then an ordinary one-shot event,
       * never a false reassertion. */
      result = lifeRetireInNak(life, address, owner, 1U);
      if (result != HAL_OK) break;
      if (!(ctl & USB_OTG_DIEPCTL_EPDIS)) {
        owner->events &= ~USB_OTG_DIEPINT_EPDISD;
        USBx_INEP(ep)->DIEPCTL = (ctl & ~USB_OTG_DIEPCTL_CNAK) |
                                 USB_OTG_DIEPCTL_SNAK | USB_OTG_DIEPCTL_EPDIS;
      }
      owner->disable_owned = 1U;
      owner->phase = PCD_QUIESCE_DISABLED;
      USBx_DEVICE->DAINTMSK |= owner->daint_mask;
      owner->isolated = 0U;
      result = HAL_BUSY;
      break;
    }
    case PCD_QUIESCE_OUT_NAK:
      if (!life->nak_acquired && !life->nak_effective) lifeAcquireOutNak(life);
      if (life->nak_effective) {
        owner->phase = PCD_QUIESCE_OUT_RX;
      }
      break;
    case PCD_QUIESCE_OUT_RX:
      lifeCaptureRaw(life, address, owner);
      if (!drained) {
        if (life->rx_poisoned) { result = lifeFail(owner, HAL_ERROR); break; }
        /* A preexisting NAK queues no status: only a requested OTG check can
         * observe the post-NAK empty FIFO without new traffic. */
        if (!life->rx_empty) life->rx_barrier = 1U;
        if (!life->rx_empty || (life->rx_debt & (1UL << ep))) break;
      }
      if (!(USBx_OUTEP(ep)->DOEPCTL & (USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_EPDIS)) &&
          !owner->disable_owned) {
        owner->phase = PCD_QUIESCE_RELEASE;
      } else {
        if (!(USBx_OUTEP(ep)->DOEPCTL & USB_OTG_DOEPCTL_EPDIS) && !owner->disable_owned) {
          owner->events &= ~USB_OTG_DOEPINT_EPDISD;
          USBx_OUTEP(ep)->DOEPCTL |= USB_OTG_DOEPCTL_SNAK | USB_OTG_DOEPCTL_EPDIS;
        }
        owner->disable_owned = 1U;
        owner->phase = PCD_QUIESCE_DISABLED;
      }
      break;
    case PCD_QUIESCE_DISABLED: {
      lifeCaptureRaw(life, address, owner);
      if (owner->events & USB_OTG_DIEPINT_EPDISD) {
        owner->phase = in ? PCD_QUIESCE_FIFO : PCD_QUIESCE_RELEASE;
      }
      break;
    }
    case PCD_QUIESCE_FIFO:
      if (life->fifo_owner != PCD_FIFO_NONE && life->fifo_owner != (uint8_t)index) break;
      if (life->fifo_owner == PCD_FIFO_NONE) {
        life->fifo_owner = (uint8_t)index;
        life->fifo_started_us = now_us;
        life->fifo_rx_mask = USBx->GINTMSK & USB_OTG_GINTMSK_RXFLVLM;
        life->fifo_empty_mask = USBx_DEVICE->DIEPEMPMSK;
        USBx->GINTMSK &= ~USB_OTG_GINTMSK_RXFLVLM;
        USBx_DEVICE->DIEPEMPMSK = 0U;
      }
      if ((uint32_t)(now_us - life->fifo_started_us) >= PCD_LIFE_FIFO_US) {
        result = lifeFail(owner, HAL_TIMEOUT);
        break;
      }
      if (lifeFifoIdle(USBx)) {
        USBx->GRSTCTL = USB_OTG_GRSTCTL_TXFFLSH | ((uint32_t)ep << 6U);
        life->fifo_issued = 1U;
        owner->phase = PCD_QUIESCE_FLUSH;
      }
      break;
    case PCD_QUIESCE_FLUSH:
      if ((uint32_t)(now_us - life->fifo_started_us) >= PCD_LIFE_FIFO_US) {
        result = lifeFail(owner, HAL_TIMEOUT);
        break;
      }
      if (!(USBx->GRSTCTL & USB_OTG_GRSTCTL_TXFFLSH)) {
        life->fifo_issued = 0U;
        life->fifo_owner = PCD_FIFO_NONE;
        if (!life->rx_poisoned) USBx->GINTMSK |= life->fifo_rx_mask;
        for (uint32_t n = 0U; n < life->handle->Init.dev_endpoints; ++n) {
          const PCD_EPTypeDef *other = &life->handle->IN_ep[n];
          if ((life->fifo_empty_mask & ~life->gated & (1UL << n)) &&
              other->xfer_count < other->xfer_len &&
              (USBx_INEP(n)->DIEPCTL & USB_OTG_DIEPCTL_EPENA))
            USBx_DEVICE->DIEPEMPMSK |= 1UL << n;
        }
        owner->phase = PCD_QUIESCE_RELEASE;
      }
      break;
    case PCD_QUIESCE_RELEASE: {
      lifeCaptureRaw(life, address, owner);
      if (!in && !drained) {
        if (life->rx_poisoned) { result = lifeFail(owner, HAL_ERROR); break; }
        if (!life->rx_empty) life->rx_barrier = 1U;
        if (!life->rx_empty || (USBx->GINTSTS & USB_OTG_GINTSTS_RXFLVL) ||
            (life->rx_debt & (1UL << ep))) break;
      }
      uint32_t policy = lifeReleasePolicy(address, (PCD_QuiesceDisposition)owner->disposition);
      if (!(policy & LIFE_VALID)) { result = lifeFail(owner, HAL_ERROR); break; }
      if (policy & LIFE_DEACTIVATE) {
        if (USB_DeactivateEndpoint(USBx, descriptor) != HAL_OK) {
          result = lifeFail(owner, HAL_ERROR);
          break;
        }
      }
      if (policy & LIFE_CLEAR) {
        descriptor->xfer_buff = NULL;
        descriptor->xfer_len = descriptor->xfer_size = descriptor->xfer_count = 0U;
        if (!in) life->receive[ep] = (PCD_RxAllocation){0};
        life->arm[index] = 0U;
      }
      USBx_DEVICE->DAINTMSK &= ~lifeBit(address);
      if (!in && !(policy & LIFE_KEEP_NAK)) {
        life->nak_users &= (uint16_t)~(1U << ep);
        if (life->nak_users == 0U) {
          if (life->nak_acquired) {
            USBx_DEVICE->DCTL |= USB_OTG_DCTL_CGONAK;
            life->nak_clearing = 1U;
          }
          USBx->GINTMSK = (USBx->GINTMSK & ~USB_OTG_GINTMSK_GONAKEFFM) | life->nak_mask;
          life->nak_acquired = life->nak_effective = 0U;
          life->nak_operation = life->nak_reset = 0U;
        }
      }
      owner->phase = PCD_QUIESCE_DONE;
      result = HAL_OK;
      break;
    }
    default:
      result = lifeFail(owner, HAL_ERROR);
      break;
  }
done:
  return result;
}

HAL_StatusTypeDef HAL_PCD_QuiesceStep(PCD_LifecycleTypeDef *life, uint8_t address,
                                    uint64_t operation, uint32_t now_us)
{
  int32_t index = lifeIndex(life, address);
  if (index < 0) return HAL_ERROR;
  uint32_t mask;
  if (lifeLock(life, &mask) != HAL_OK) return HAL_BUSY;
  HAL_StatusTypeDef result = lifeStepLocked(life, address, index, operation, now_us);
  lifeUnlock(life, mask);
  return result;
}

HAL_StatusTypeDef HAL_PCD_QuiesceRetry(PCD_LifecycleTypeDef *life, uint8_t address,
                                     uint64_t operation, uint32_t now_us)
{
  int32_t index = lifeIndex(life, address);
  if (index < 0 || operation == 0U) return HAL_ERROR;
  uint32_t mask;
  if (lifeLock(life, &mask) != HAL_OK) return HAL_BUSY;
  PCD_QuiesceEndpoint *owner = &life->endpoint[index];
  HAL_StatusTypeDef result = HAL_ERROR;
  if (owner->phase == PCD_QUIESCE_FAILED && owner->reset == life->reset &&
      operation != owner->operation) {
    owner->operation = operation;
    owner->phase = owner->failed_phase;
    owner->remaining_us = PCD_LIFE_HANDSHAKE_US;
    owner->observed_us = now_us;
    if (life->fifo_owner == (uint8_t)index) life->fifo_started_us = now_us;
    result = HAL_OK;
  }
  lifeUnlock(life, mask);
  return result;
}

HAL_StatusTypeDef HAL_PCD_LifecycleOpenGate(PCD_LifecycleTypeDef *life, uint8_t address,
                                          uint64_t operation)
{
  int32_t index = lifeIndex(life, address);
  if (index < 0) return HAL_ERROR;
  uint32_t mask;
  if (lifeLock(life, &mask) != HAL_OK) return HAL_BUSY;
  PCD_QuiesceEndpoint *owner = &life->endpoint[index];
  HAL_StatusTypeDef result = HAL_BUSY;
  if (owner->phase == PCD_QUIESCE_FAILED && owner->operation == operation &&
      owner->reset == life->reset) {
    result = (HAL_StatusTypeDef)owner->failure;
    lifeUnlock(life, mask);
    return result;
  }
  if (owner->phase == PCD_QUIESCE_DONE && owner->operation == operation &&
      owner->reset == life->reset && life->fifo_owner == PCD_FIFO_NONE &&
      !life->rx_poisoned) {
    USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
    uintptr_t USBx_BASE = (uintptr_t)USBx;
    uint32_t policy = lifeReleasePolicy(address, (PCD_QuiesceDisposition)owner->disposition);
    if (!(policy & LIFE_VALID) || (policy & LIFE_KEEP_GATE)) {
      lifeUnlock(life, mask);
      return HAL_ERROR;
    }
    if (owner->disposition == PCD_QUIESCE_DEFAULT_RESTART) {
      uint32_t ctl = (address & 0x80U) ? USBx_INEP(address & 15U)->DIEPCTL :
                                       USBx_OUTEP(address & 15U)->DOEPCTL;
      if (!(ctl & USB_OTG_DIEPCTL_USBAEP)) {
        lifeUnlock(life, mask);
        return HAL_BUSY;
      }
    }
    if ((address & 0x80U) && (USBx_INEP(address & 15U)->DIEPINT & USB_OTG_DIEPINT_INEPNE)) {
      lifeCaptureRaw(life, address, owner);
      result = lifeRetireInNak(life, address, owner, 1U);
      if (result != HAL_OK) {
        lifeUnlock(life, mask);
        return result;
      }
    }
    if (policy & LIFE_RESTORE_IRQ)
      USBx_DEVICE->DAINTMSK |= owner->daint_mask;
    life->gated &= ~lifeBit(address);
    owner->phase = PCD_QUIESCE_IDLE;
    result = HAL_OK;
  }
  lifeUnlock(life, mask);
  return result;
}

HAL_StatusTypeDef HAL_PCD_QuiesceClose(PCD_LifecycleTypeDef *life, uint8_t address,
                                     uint64_t operation)
{
  int32_t index = lifeIndex(life, address);
  if (index < 0 || operation == 0U || (address & 15U) == 0U) return HAL_ERROR;
  uint32_t mask;
  if (lifeLock(life, &mask) != HAL_OK) return HAL_BUSY;
  PCD_QuiesceEndpoint *owner = &life->endpoint[index];
  HAL_StatusTypeDef result = HAL_ERROR;
  if (owner->phase == PCD_QUIESCE_DONE && owner->operation == operation &&
      owner->reset == life->reset && (life->gated & lifeBit(address))) {
    USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
    uintptr_t USBx_BASE = (uintptr_t)USBx;
    uint8_t ep = address & 15U;
    uint8_t in = (address & 0x80U) != 0U;
    PCD_EPTypeDef *descriptor = in ? &life->handle->IN_ep[ep] : &life->handle->OUT_ep[ep];
    uint32_t events = in ? USBx_INEP(ep)->DIEPINT : USBx_OUTEP(ep)->DOEPINT;
    /* The gate kept every arm out: completion evidence or RX debt is an owner
     * fault, and deactivation itself refuses an enabled or disabling endpoint. */
    if (!(events & (USB_OTG_DIEPINT_XFRC | USB_OTG_DIEPINT_EPDISD)) &&
        (in || !(life->rx_debt & (1UL << ep))) &&
        USB_DeactivateEndpoint(USBx, descriptor) == HAL_OK) {
      descriptor->xfer_buff = NULL;
      descriptor->xfer_len = descriptor->xfer_size = descriptor->xfer_count = 0U;
      if (!in) life->receive[ep] = (PCD_RxAllocation){0};
      life->arm[index] = 0U;
      USBx_DEVICE->DAINTMSK &= ~lifeBit(address);
      owner->disposition = (uint8_t)PCD_QUIESCE_CLOSE;
      result = HAL_OK;
    }
  }
  lifeUnlock(life, mask);
  return result;
}

void HAL_PCD_LifecycleSuspend(PCD_LifecycleTypeDef *life, uint8_t suspended,
                              uint32_t now_us)
{
  if (life == NULL || life->handle == NULL) return;
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  if (!!suspended != !!life->suspended) {
    for (uint32_t i = 0U; i < 2U * PCD_LIFE_ENDPOINTS; ++i) {
      PCD_QuiesceEndpoint *owner = &life->endpoint[i];
      if (suspended && lifeHandshake(owner->phase, i < PCD_LIFE_ENDPOINTS))
        (void)lifeCharge(owner, now_us, i < PCD_LIFE_ENDPOINTS);
      owner->observed_us = now_us;
    }
    life->suspended = !!suspended;
    /* Resume is the event that ends a paused FIFO-ticket acquisition. */
    if (!suspended) life->facts = 1U;
  }
  __set_PRIMASK(mask);
}

HAL_StatusTypeDef HAL_PCD_LifecycleRxStep(PCD_LifecycleTypeDef *life, uint8_t pass,
                                        PCD_RxBudget *budget, PCD_RxEvent *event)
{
  if (life == NULL || life->handle == NULL || budget == NULL || event == NULL ||
      pass > 2U || budget->statuses > PCD_LIFE_RX_STATUSES ||
      budget->bytes > PCD_LIFE_RX_BYTES || __get_IPSR() == 0U) return HAL_ERROR;
  if (life->rx_poisoned) return HAL_ERROR;
  if (life->fifo_owner != PCD_FIFO_NONE) return HAL_BUSY;
  USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  if (!(USBx->GINTSTS & USB_OTG_GINTSTS_RXFLVL)) {
    life->rx_empty = 1U;
    if (life->rx_barrier) {
      life->rx_barrier = 0U;
      life->facts = 1U;
    }
    return HAL_BUSY;
  }
  life->rx_empty = 0U;
  uint32_t header = USBx->GRXSTSR;
  uint32_t ep = header & USB_OTG_GRXSTSP_EPNUM;
  uint32_t count = (header & USB_OTG_GRXSTSP_BCNT) >> 4U;
  if (pass < 2U && ((ep == 3U) != (pass == 1U))) return HAL_BUSY;
  if (budget->statuses == 0U || (count <= PCD_LIFE_RX_BYTES && count > budget->bytes))
    return HAL_BUSY;
  uint32_t peeked = header;
  header = USBx->GRXSTSP;
  ep = header & USB_OTG_GRXSTSP_EPNUM;
  count = (header & USB_OTG_GRXSTSP_BCNT) >> 4U;
  *event = (PCD_RxEvent){header, (uint16_t)count, (uint8_t)ep, PCD_RX_REJECTED};
  --budget->statuses;
  if (header != peeked || count > PCD_LIFE_RX_BYTES) {
    life->rx_poisoned = 1U;
    /* Every post-init GINTMSK RMW holds PRIMASK (SOF demand owner). */
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    USBx->GINTMSK &= ~USB_OTG_GINTMSK_RXFLVLM;
    __set_PRIMASK(irq);
    event->kind = PCD_RX_POISONED;
    return HAL_ERROR;
  }
  budget->bytes -= (uint16_t)count;
  uint32_t status = (header & USB_OTG_GRXSTSP_PKTSTS) >> 17U;
  uint8_t *destination = NULL;
  PCD_RxAllocation *allocation = NULL;
  if (status == STS_SETUP_UPDT && ep == 0U) {
    /* Any EP0 SETUP entry ends the previous capture; only eight bytes make a
     * new one, so a rejected SETUP can never replay older bytes at STUP. */
    life->setup_ready = 0U;
    if (count == 8U) {
      destination = (uint8_t *)life->handle->Setup;
      event->kind = PCD_RX_SETUP;
    }
  } else if (status == STS_DATA_UPDT && ep < life->handle->Init.dev_endpoints &&
             ep < PCD_LIFE_ENDPOINTS) {
    allocation = &life->receive[ep];
    if (allocation->base != NULL && allocation->reset == life->reset &&
        allocation->arm == life->arm[PCD_LIFE_ENDPOINTS + ep] &&
        allocation->offset <= allocation->capacity &&
        count <= allocation->capacity - allocation->offset) {
      destination = allocation->base + allocation->offset;
      event->kind = PCD_RX_DATA;
    }
  } else if (count == 0U && ep < life->handle->Init.dev_endpoints &&
             (status == STS_GOUT_NAK || status == STS_XFER_COMP || status == STS_SETUP_COMP)) {
    event->kind = PCD_RX_STATUS;
  }
  for (uint32_t offset = 0U; offset < count; offset += 4U) {
    uint32_t word = USBx_DFIFO(0U);
    if (destination != NULL)
      for (uint32_t byte = 0U; byte < 4U && offset + byte < count; ++byte)
        destination[offset + byte] = (uint8_t)(word >> (8U * byte));
  }
  if (event->kind == PCD_RX_DATA) {
    allocation->offset += count;
    life->handle->OUT_ep[ep].xfer_buff = allocation->base + allocation->offset;
    life->handle->OUT_ep[ep].xfer_count = allocation->offset;
    life->rx_debt |= 1UL << ep;
  } else if (event->kind == PCD_RX_SETUP) {
    life->setup_ready = 1U;
  } else if (event->kind == PCD_RX_STATUS && status == STS_SETUP_COMP) {
    /* Popping setup-complete raises STUP: this drain ends so that STUP
     * dispatches its own SETUP before a later SETUP can replace the capture,
     * as stock HAL's one entry per IRQ did (dwc2 acts at SETUP_COMP). */
    return HAL_BUSY;
  } else if (event->kind == PCD_RX_STATUS && status == STS_GOUT_NAK) {
    HAL_PCD_LifecycleObserveOutNak(life, life->reset, life->nak_operation,
                                   USB_OTG_GINTSTS_BOUTNAKEFF);
  } else if (event->kind == PCD_RX_STATUS && status == STS_XFER_COMP &&
             !(life->gated & lifeBit((uint8_t)ep))) {
    /* Slave-mode XFRC follows this status: the arm's payload was consumed in
     * order, so its ordinary completion may rebind. A gated owner keeps it. */
    life->rx_debt &= ~(1UL << ep);
  }
  return event->kind == PCD_RX_REJECTED ? HAL_ERROR : HAL_OK;
}

void HAL_PCD_LifecycleRequestRxBarrier(PCD_LifecycleTypeDef *life)
{
  if (life == NULL || life->handle == NULL) return;
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  life->rx_empty = 0U;
  life->rx_barrier = 1U;
  __set_PRIMASK(mask);
}

uint8_t HAL_PCD_LifecycleTakeSetup(PCD_LifecycleTypeDef *life)
{
  if (life == NULL || life->handle == NULL || !life->setup_ready || __get_IPSR() == 0U)
    return 0U;
  life->setup_ready = 0U;
  return 1U;
}

uint8_t HAL_PCD_LifecycleRxBarrier(PCD_LifecycleTypeDef *life)
{
  /* With RXFLVL set the IRQ drain itself observes emptiness after the queue. */
  if (life == NULL || life->handle == NULL || !life->rx_barrier || __get_IPSR() == 0U ||
      life->rx_poisoned || life->fifo_owner != PCD_FIFO_NONE ||
      (life->handle->Instance->GINTSTS & USB_OTG_GINTSTS_RXFLVL)) return 0U;
  life->rx_barrier = 0U;
  life->rx_empty = 1U;
  life->facts = 1U;
  return 1U;
}

/* Independent safe detach and interrupt isolation: register writes only. */
static void lifeIsolate(PCD_LifecycleTypeDef *life)
{
  USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  USBx_DEVICE->DCTL |= USB_OTG_DCTL_SDIS;
  USBx->GAHBCFG &= ~USB_OTG_GAHBCFG_GINT;
  USBx->GINTMSK = 0U;
  USBx_DEVICE->DAINTMSK = 0U;
  USBx_DEVICE->DIEPEMPMSK = 0U;
}

static uint8_t lifeShutdownEnded(const PCD_LifecycleTypeDef *life)
{
  return life->shutdown && (life->reset_phase == PCD_RESET_IDLE ||
                            life->reset_phase == PCD_RESET_FAILED);
}

/* Caller masks interrupts. */
static void lifeAdopt(PCD_LifecycleTypeDef *life, uint64_t reset, uint32_t now_us)
{
  USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  /* An acquired global OUT NAK passes to controller cleanup, never to an owner. */
  if (life->nak_users != 0U && life->nak_acquired) life->reset_nak = 1U;
  life->nak_users = 0U;
  life->nak_acquired = life->nak_effective = 0U;
  life->nak_operation = life->nak_reset = 0U;
  /* Old owners cannot prove release across the boundary; the join re-owns
   * every endpoint hardware still has enabled. */
  for (uint32_t i = 0U; i < 2U * PCD_LIFE_ENDPOINTS; ++i)
    life->endpoint[i] = (PCD_QuiesceEndpoint){0};
  life->gated = 0U;
  life->reset = reset;
  life->reset_started_us = now_us;
  life->reset_phase = PCD_RESET_INHERIT;
  life->reset_fifo = 0U;
  life->rx_empty = life->rx_barrier = 0U;
  life->setup_ready = 0U;
  life->suspended = 0U;
  USBx_DEVICE->DIEPEMPMSK = 0U;
  life->facts = 1U;
}

void HAL_PCD_LifecycleResetBegin(PCD_LifecycleTypeDef *life, uint64_t reset, uint32_t now_us)
{
  if (life == NULL || life->handle == NULL || reset == 0U) return;
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  /* A raw reset restarts a shutdown's join but never renews its deadline. */
  if (!lifeShutdownEnded(life))
    lifeAdopt(life, reset, life->shutdown ? life->reset_started_us : now_us);
  __set_PRIMASK(mask);
}

void HAL_PCD_LifecycleShutdownBegin(PCD_LifecycleTypeDef *life, uint64_t reset,
                                    uint32_t anchor_us)
{
  if (life == NULL || life->handle == NULL || reset == 0U) return;
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  if (!life->shutdown) {
    uintptr_t USBx_BASE = (uintptr_t)life->handle->Instance;
    /* Detach first: no new host traffic reaches the endpoints being retired. */
    USBx_DEVICE->DCTL |= USB_OTG_DCTL_SDIS;
    life->shutdown = 1U;
    lifeAdopt(life, reset, anchor_us);
  }
  __set_PRIMASK(mask);
}

void HAL_PCD_LifecycleShutdownIsolate(PCD_LifecycleTypeDef *life)
{
  if (life == NULL || life->handle == NULL) return;
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  if (life->shutdown && !lifeShutdownEnded(life)) {
    lifeIsolate(life);
    life->reset_phase = PCD_RESET_FAILED;
  }
  __set_PRIMASK(mask);
}

void HAL_PCD_LifecycleEnumDone(PCD_LifecycleTypeDef *life)
{
  if (life == NULL || life->handle == NULL) return;
  life->enum_speed = USB_GetDevSpeed(life->handle->Instance);
  life->enum_reset = life->reset;
  life->facts = 1U;
}

static uint8_t lifeResetFifoPhase(uint8_t phase)
{
  return phase == PCD_RESET_INHERIT || phase == PCD_RESET_TX || phase == PCD_RESET_TX_FLUSH ||
         phase == PCD_RESET_RX || phase == PCD_RESET_RX_FLUSH;
}

/* The join's FIFO-command ticket excludes FIFO I/O. RXFLVL stays masked while
 * it is held: the parser does not pop then, and the level-triggered OTG line
 * would otherwise re-enter forever, above PendSV and the thread. */
static void lifeResetTakeFifo(PCD_LifecycleTypeDef *life, uint32_t now_us)
{
  USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  if (!life->reset_fifo) {
    life->reset_fifo = 1U;
    life->reset_fifo_us = now_us;
  }
  life->fifo_owner = PCD_FIFO_RESET;
  USBx->GINTMSK &= ~USB_OTG_GINTMSK_RXFLVLM;
  USBx_DEVICE->DIEPEMPMSK = 0U;
}

/* An OUT endpoint disable still awaiting its EPDISD: the core clears EPDIS as
 * it raises EPDISD (RM0477 "Disabling an OUT endpoint"). */
static uint8_t lifeOutDisabling(const PCD_LifecycleTypeDef *life)
{
  uintptr_t USBx_BASE = (uintptr_t)life->handle->Instance;
  for (uint32_t ep = 1U; ep < life->handle->Init.dev_endpoints; ++ep)
    if (USBx_OUTEP(ep)->DOEPCTL & USB_OTG_DOEPCTL_EPDIS) return 1U;
  return 0U;
}

/* Poison ends only at the controller-wide RX flush (RX, RX_FLUSH), so no join
 * passes its barrier poisoned; poison raised after it stays masked until the
 * next raw reset's join. In slave mode a global OUT NAK takes effect when its
 * status entry is popped (RM0477), and the flush may discard that entry: the
 * NAK is released first and reacquired by RETIRE afterwards. RM0477 clears the
 * NAK only after the EPDISD of every OUT disable under it, so callers
 * guarantee that none depends on it: RETIRE before the NAK is effective and
 * with no disable pending, BARRIER once every owner is retired. */
static void lifeResetRecoverRx(PCD_LifecycleTypeDef *life)
{
  USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  if (life->reset_nak || life->nak_acquired || (USBx_DEVICE->DCTL & USB_OTG_DCTL_GONSTS) ||
      (USBx->GINTSTS & USB_OTG_GINTSTS_BOUTNAKEFF)) {
    USBx_DEVICE->DCTL |= USB_OTG_DCTL_CGONAK;
    life->nak_clearing = 1U;
  }
  life->reset_nak = life->nak_acquired = life->nak_effective = 0U;
  life->reset_phase = PCD_RESET_RX;
}

HAL_StatusTypeDef HAL_PCD_LifecycleResetStep(PCD_LifecycleTypeDef *life, uint32_t now_us,
                                           uint8_t *wait)
{
  if (life == NULL || life->handle == NULL || wait == NULL) return HAL_ERROR;
  *wait = 0U;
  uint32_t mask;
  if (lifeLock(life, &mask) != HAL_OK) return HAL_BUSY;
  USB_OTG_GlobalTypeDef *USBx = life->handle->Instance;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  uint32_t endpoints = life->handle->Init.dev_endpoints;
  uint64_t operation = PCD_RESET_OPERATION | life->reset;
  HAL_StatusTypeDef result = HAL_BUSY;
  uint8_t phase = life->reset_phase;
  if (phase == PCD_RESET_IDLE) { result = HAL_OK; goto done; }
  if (phase == PCD_RESET_FAILED) { result = HAL_ERROR; goto done; }
  /* Physical cleanup has one wall allowance; the ENUMDNE wait has none. */
  if (phase != PCD_RESET_ENUM &&
      (uint32_t)(now_us - life->reset_started_us) >= PCD_LIFE_RESET_US) goto timeout;
  if (life->reset_fifo && lifeResetFifoPhase(phase) &&
      (uint32_t)(now_us - life->reset_fifo_us) >= PCD_LIFE_FIFO_US) goto timeout;
  switch (phase) {
    case PCD_RESET_INHERIT:
      if (life->fifo_owner != PCD_FIFO_NONE || !lifeFifoIdle(USBx)) {
        /* Adopt a busy command; its completion is not old-owner success. */
        lifeResetTakeFifo(life, now_us);
        if (!lifeFifoIdle(USBx)) { *wait |= PCD_RESET_WAIT_POLL; goto done; }
      }
      /* Each acquisition of the released reset FIFO ticket has its own
       * allowance; the reset wall deadline bounds the whole join. */
      life->fifo_owner = PCD_FIFO_NONE;
      life->fifo_issued = life->reset_fifo = 0U;
      life->reset_phase = PCD_RESET_REGS;
      break;
    case PCD_RESET_REGS:
      USBx_DEVICE->DCFG &= ~USB_OTG_DCFG_DAD;
      for (uint32_t i = 0U; i < endpoints; ++i) {
        USBx_INEP(i)->DIEPINT = 0xFB7FU;
        USBx_INEP(i)->DIEPCTL &= ~USB_OTG_DIEPCTL_STALL;
        USBx_OUTEP(i)->DOEPINT = 0xFB7FU;
        USBx_OUTEP(i)->DOEPCTL &= ~USB_OTG_DOEPCTL_STALL;
        USBx_OUTEP(i)->DOEPCTL |= USB_OTG_DOEPCTL_SNAK;
        /* Retirement handshakes below stay interrupt-driven. */
        if (USBx_INEP(i)->DIEPCTL & (USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_EPDIS))
          USBx_DEVICE->DAINTMSK |= 1UL << i;
        if (i && (USBx_OUTEP(i)->DOEPCTL & (USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_EPDIS)))
          USBx_DEVICE->DAINTMSK |= 1UL << (16U + i);
      }
      USBx_DEVICE->DAINTMSK |= 0x10001U;
      USBx_DEVICE->DOEPMSK |= USB_OTG_DOEPMSK_STUPM | USB_OTG_DOEPMSK_XFRCM |
                              USB_OTG_DOEPMSK_EPDM | USB_OTG_DOEPMSK_OTEPSPRM |
                              USB_OTG_DOEPMSK_NAKM;
      /* Shared INEPNE notification belongs to the device/reset configuration. */
      USBx_DEVICE->DIEPMSK |= USB_OTG_DIEPMSK_TOM | USB_OTG_DIEPMSK_XFRCM |
                              USB_OTG_DIEPMSK_EPDM | USB_OTG_DIEPMSK_INEPNEM;
      USBx->GINTMSK &= ~USB_OTG_GINTMSK_GONAKEFFM;
      /* A poisoned FIFO keeps RXFLVL masked; RETIRE recovers it first. */
      if (!life->rx_poisoned) USBx->GINTMSK |= USB_OTG_GINTMSK_RXFLVLM;
      life->reset_phase = PCD_RESET_RETIRE;
      break;
    case PCD_RESET_RETIRE: {
      /* Every OUT endpoint is SNAKed. Poison from before REGS, or raised since
       * by an entry parsed late, is recovered while no OUT disable depends on
       * the global OUT NAK: that NAK takes effect only when its status entry
       * is popped (slave mode; dwc2 fecb3a171db4), which poison prevents.
       * Once one does (the NAK is effective, or a disable issued before this
       * reset still awaits EPDISD), retirement no longer reads the FIFO and
       * BARRIER recovers. */
      if (life->rx_poisoned && !life->nak_effective && !lifeOutDisabling(life)) {
        lifeResetRecoverRx(life);
        break;
      }
      /* At most one Begin or owner phase change per call, so the caller's
       * slice charges each as one transition; other owners are observed. */
      uint8_t pending = 0U;
      for (uint32_t i = 0U; i < 2U * endpoints; ++i) {
        uint8_t address = i < endpoints ? (uint8_t)(0x80U | i) : (uint8_t)(i - endpoints);
        int32_t index = lifeIndex(life, address);
        /* EP0 OUT cannot be disabled; the RX barrier owns its old entries. */
        if (address == 0U || index < 0) continue;
        PCD_QuiesceEndpoint *owner = &life->endpoint[index];
        if (owner->operation != operation) {
          uint32_t ctl = (address & 0x80U) ? USBx_INEP(i)->DIEPCTL :
                                           USBx_OUTEP(address)->DOEPCTL;
          if (!(ctl & (USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_EPDIS))) continue;
          if (lifeBeginLocked(life, address, index, operation, life->arm[index],
                              address == 0x80U ? PCD_QUIESCE_EP0_RELEASE :
                                                 PCD_QUIESCE_CONTROLLER_RETIRE,
                              now_us) != HAL_OK) goto fail;
          *wait |= PCD_RESET_WAIT_READY;
          goto done;
        }
        uint8_t before = owner->phase;
        HAL_StatusTypeDef step = lifeStepLocked(life, address, index, operation, now_us);
        if (step != HAL_OK && step != HAL_BUSY) {
          result = step;
          goto failed;
        }
        if (owner->phase != before) {
          *wait |= PCD_RESET_WAIT_READY;
          goto done;
        }
        if (step == HAL_BUSY) {
          pending = 1U;
          if (owner->phase == PCD_QUIESCE_FIFO || owner->phase == PCD_QUIESCE_FLUSH)
            *wait |= PCD_RESET_WAIT_POLL;
        }
      }
      if (pending) {
        if (life->rx_barrier) *wait |= PCD_RESET_WAIT_BARRIER;
        goto done;
      }
      life->reset_phase = PCD_RESET_TX;
      break;
    }
    case PCD_RESET_TX:
      /* FIFO-I/O exclusion: no RX parse or CPU TX write overlaps the flush. */
      lifeResetTakeFifo(life, now_us);
      if (!lifeFifoIdle(USBx)) { *wait |= PCD_RESET_WAIT_POLL; goto done; }
      USBx->GRSTCTL = USB_OTG_GRSTCTL_TXFFLSH | (0x10U << 6U);
      life->fifo_issued = 1U;
      life->reset_phase = PCD_RESET_TX_FLUSH;
      break;
    case PCD_RESET_TX_FLUSH:
      if (USBx->GRSTCTL & USB_OTG_GRSTCTL_TXFFLSH) { *wait |= PCD_RESET_WAIT_POLL; goto done; }
      life->fifo_issued = life->reset_fifo = 0U;
      life->fifo_owner = PCD_FIFO_NONE;
      if (!life->rx_poisoned) USBx->GINTMSK |= USB_OTG_GINTMSK_RXFLVLM;
      life->rx_empty = 0U;
      life->rx_barrier = 1U;
      life->reset_phase = PCD_RESET_BARRIER;
      break;
    case PCD_RESET_RX:
      lifeResetTakeFifo(life, now_us);
      if (!lifeFifoIdle(USBx) || (USBx_DEVICE->DCTL & USB_OTG_DCTL_GONSTS) ||
          (USBx->GINTSTS & USB_OTG_GINTSTS_BOUTNAKEFF)) {
        *wait |= PCD_RESET_WAIT_POLL;
        goto done;
      }
      USBx->GRSTCTL = USB_OTG_GRSTCTL_RXFFLSH;
      life->fifo_issued = 1U;
      life->reset_phase = PCD_RESET_RX_FLUSH;
      break;
    case PCD_RESET_RX_FLUSH:
      if (USBx->GRSTCTL & USB_OTG_GRSTCTL_RXFFLSH) { *wait |= PCD_RESET_WAIT_POLL; goto done; }
      /* Only this completed controller-wide RX flush clears a framing poison;
       * the parser then pops RETIRE's global OUT NAK status. */
      life->fifo_issued = life->reset_fifo = 0U;
      life->rx_poisoned = 0U;
      /* Every RX flush also ends any SETUP capture: a setup-done popped later
       * cannot be matched to the SETUP it completes, so it dispatches nothing. */
      life->setup_ready = 0U;
      life->fifo_owner = PCD_FIFO_NONE;
      USBx->GINTMSK |= USB_OTG_GINTMSK_RXFLVLM;
      life->reset_phase = PCD_RESET_RETIRE;
      break;
    case PCD_RESET_BARRIER:
      /* Poison raised after RETIRE's recovery point: every owner is retired,
       * so the same recovery runs here; no join passes its barrier poisoned. */
      if (life->rx_poisoned) {
        lifeResetRecoverRx(life);
        break;
      }
      if (!life->rx_empty) {
        if (life->rx_barrier) *wait |= PCD_RESET_WAIT_BARRIER;
        goto done;
      }
      /* All-TX, SNAK/masks and the post-SNAK RX-empty barrier are complete:
       * old descriptors, arms, allocations and completion debt end here. */
      for (uint32_t i = 0U; i < endpoints; ++i) {
        PCD_EPTypeDef *in = &life->handle->IN_ep[i], *out = &life->handle->OUT_ep[i];
        in->xfer_buff = out->xfer_buff = NULL;
        in->xfer_len = in->xfer_size = in->xfer_count = 0U;
        out->xfer_len = out->xfer_size = out->xfer_count = 0U;
        in->is_stall = out->is_stall = 0U;
        life->receive[i] = (PCD_RxAllocation){0};
        life->arm[i] = life->arm[PCD_LIFE_ENDPOINTS + i] = 0U;
      }
      for (uint32_t i = 0U; i < 2U * PCD_LIFE_ENDPOINTS; ++i)
        life->endpoint[i] = (PCD_QuiesceEndpoint){0};
      life->gated = life->rx_debt = 0U;
      USBx_DEVICE->DAINTMSK &= 0x10001U;
      if (life->reset_nak || life->nak_acquired) {
        USBx_DEVICE->DCTL |= USB_OTG_DCTL_CGONAK;
        life->nak_clearing = 1U;
      }
      life->reset_nak = life->nak_acquired = life->nak_effective = 0U;
      life->nak_users = 0U;
      life->nak_operation = life->nak_reset = 0U;
      USBx->GINTMSK &= ~USB_OTG_GINTMSK_GONAKEFFM;
      if (life->shutdown) {
        /* Proven cleanup ends in isolation; EP0 never restarts before MCU reset. */
        lifeIsolate(life);
        life->reset_phase = PCD_RESET_IDLE;
        result = HAL_OK;
        goto done;
      }
      (void)USB_EP0_OutStart(USBx, 0U, (uint8_t *)life->handle->Setup);
      life->reset_phase = PCD_RESET_ENUM;
      break;
    case PCD_RESET_ENUM:
      /* Early ENUMDNE is only a latch; without it the device stays parked. */
      if (life->enum_reset != life->reset) goto done;
      (void)USB_ActivateSetup(USBx);
      life->handle->Init.speed = life->enum_speed;
      (void)USB_SetTurnaroundTime(USBx, HAL_RCC_GetHCLKFreq(), life->enum_speed);
      life->reset_phase = PCD_RESET_IDLE;
      result = HAL_OK;
      goto done;
    default:
      goto fail;
  }
  /* One phase per call keeps each masked transaction short; the caller's
   * slice budget decides whether the next phase runs now. */
  *wait |= PCD_RESET_WAIT_READY;
  goto done;
timeout:
  result = HAL_TIMEOUT;
  goto failed;
fail:
  result = HAL_ERROR;
failed:
  life->reset_phase = PCD_RESET_FAILED;
  /* A failed shutdown still detaches and isolates; its owners stay retained. */
  if (life->shutdown) lifeIsolate(life);
done:
  lifeUnlock(life, mask);
  return result;
}

HAL_StatusTypeDef HAL_PCD_LifecycleRxDebtDone(PCD_LifecycleTypeDef *life, uint8_t ep,
                                            uint64_t reset, uint64_t arm)
{
  if (lifeIndex(life, ep) < 0 || ep >= PCD_LIFE_ENDPOINTS) return HAL_ERROR;
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  HAL_StatusTypeDef result = HAL_BUSY;
  if (life->rx_empty && !life->rx_poisoned && reset == life->reset &&
      arm == life->arm[PCD_LIFE_ENDPOINTS + ep]) {
    life->rx_debt &= ~(1UL << ep);
    result = HAL_OK;
  }
  __set_PRIMASK(mask);
  return result;
}
#endif
