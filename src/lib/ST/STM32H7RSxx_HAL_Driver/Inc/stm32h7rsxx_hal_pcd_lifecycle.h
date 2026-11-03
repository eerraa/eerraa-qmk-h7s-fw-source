/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 era (@eerraa).
 * Project device-lifetime extension; include after stm32h7rsxx_hal.h.
 */
#ifndef STM32H7RSxx_HAL_PCD_LIFECYCLE_H
#define STM32H7RSxx_HAL_PCD_LIFECYCLE_H

#include <stdint.h>

#define PCD_LIFE_ENDPOINTS 9U
#define PCD_LIFE_HANDSHAKE_US 10000U
#define PCD_LIFE_FIFO_US 10000U
#define PCD_LIFE_RX_STATUSES 16U
#define PCD_LIFE_RX_BYTES 512U
#define PCD_LIFE_RESET_US 40000U
/* fifo_owner value while no GRSTCTL/FIFO-exclusion ticket is held. */
#define PCD_LIFE_FIFO_NONE 0xffU

/* Raw-reset join: inherited GRSTCTL command, reset register sequence (DAD=0,
 * SNAK, masks), retirement of endpoints still enabled, all-TX flush, RX-empty
 * barrier, descriptor release, then the same-reset ENUMDNE. No TX flush
 * precedes the NAK/disable of every IN endpoint it covers (design 5.1). A
 * poisoned FIFO is recovered by the controller-wide RX flush whenever the join
 * observes poison before its barrier: in RETIRE while no OUT disable depends on
 * the global OUT NAK, otherwise at BARRIER. No join passes its barrier
 * poisoned; poison raised later (the ENUMDNE wait included) stays masked until
 * the next raw reset's join. The flush also ends any SETUP capture. Every
 * FIFO-ticket holder and every poison keep RXFLVL masked, so the
 * level-triggered OTG line never holds an entry the parser will not pop. Each
 * reset FIFO-ticket acquisition has its own PCD_LIFE_FIFO_US allowance inside
 * the PCD_LIFE_RESET_US wall. An authorized shutdown runs the same join after
 * a soft detach and ends in interrupt isolation instead of EP0 restart and
 * ENUMDNE. */
typedef enum {
  PCD_RESET_IDLE, PCD_RESET_INHERIT, PCD_RESET_REGS, PCD_RESET_RETIRE,
  PCD_RESET_TX, PCD_RESET_TX_FLUSH, PCD_RESET_RX, PCD_RESET_RX_FLUSH,
  PCD_RESET_BARRIER, PCD_RESET_ENUM, PCD_RESET_FAILED
} PCD_ResetPhase;
/* ResetStep wait bits: a no-IRQ GRSTCTL observation, an OTG barrier check
 * the caller must software-pend, or READY when the call advanced and the next
 * step needs no new fact. None is set while an IRQ fact is awaited. */
#define PCD_RESET_WAIT_POLL 1U
#define PCD_RESET_WAIT_BARRIER 2U
#define PCD_RESET_WAIT_READY 4U

typedef enum {
  PCD_QUIESCE_IDLE, PCD_QUIESCE_IN_NAK, PCD_QUIESCE_IN_DISABLE,
  PCD_QUIESCE_OUT_NAK, PCD_QUIESCE_OUT_RX, PCD_QUIESCE_DISABLED,
  PCD_QUIESCE_FIFO, PCD_QUIESCE_FLUSH, PCD_QUIESCE_RELEASE,
  PCD_QUIESCE_DONE, PCD_QUIESCE_FAILED
} PCD_QuiescePhase;

/* CLOSE: deactivate and clear transfer/allocation/arm state.
 * ABORT: retain the quiesced descriptor and restore its saved IRQ route on OpenGate.
 * DEFAULT_RESTART: deactivate but retain immutable transfer/allocation/arm state;
 * the policy must reopen/default the endpoint before OpenGate.
 * EP0_RELEASE: IN0 only; clear its transfer/arm, retain the control endpoint.
 * CONTROLLER_RETIRE: raw-reset join only (QuiesceBegin rejects it): deactivate/
 * clear, keep gates and acquired OUT NAK owned by controller cleanup. Its new
 * identity already discards old payload, so old RX debt/poison end at the
 * join's flush and RX-empty barrier. EP0 OUT cannot use endpoint disable; the
 * join's controller-wide RX flush/barrier owns its old entries.
 */
typedef enum {
  PCD_QUIESCE_CLOSE, PCD_QUIESCE_ABORT, PCD_QUIESCE_DEFAULT_RESTART,
  PCD_QUIESCE_EP0_RELEASE, PCD_QUIESCE_CONTROLLER_RETIRE
} PCD_QuiesceDisposition;

typedef struct {
  uint64_t operation, reset, arm;
  uint32_t events, remaining_us, observed_us, daint_mask;
  uint8_t phase, failed_phase, disposition, disable_owned, isolated, failure;
} PCD_QuiesceEndpoint;

typedef struct {
  uint8_t *base;
  uint32_t capacity, offset;
  uint64_t reset, arm;
} PCD_RxAllocation;

typedef struct {
  uint16_t statuses, bytes;
} PCD_RxBudget;

typedef enum {
  PCD_RX_DATA, PCD_RX_SETUP, PCD_RX_STATUS, PCD_RX_REJECTED, PCD_RX_POISONED
} PCD_RxKind;

typedef struct {
  uint32_t header;
  uint16_t length;
  uint8_t endpoint, kind;
} PCD_RxEvent;

typedef struct {
  PCD_HandleTypeDef *handle;
  uint64_t reset;
  PCD_QuiesceEndpoint endpoint[2U * PCD_LIFE_ENDPOINTS];
  PCD_RxAllocation receive[PCD_LIFE_ENDPOINTS];
  uint64_t arm[2U * PCD_LIFE_ENDPOINTS];
  uint64_t nak_operation, nak_reset;
  uint32_t gated, rx_debt;
  uint32_t nak_mask, fifo_rx_mask, fifo_empty_mask, fifo_started_us;
  uint16_t nak_users;
  /* nak_clearing: a CGONAK the core may not have taken yet, so GONAKEFF can
   * still read effective; the next acquisition waits for it to clear. */
  uint8_t nak_acquired, nak_effective, nak_clearing, fifo_owner, fifo_issued;
  uint8_t rx_empty, rx_poisoned, suspended;
  uint64_t enum_reset;
  uint32_t reset_started_us, reset_fifo_us;
  uint8_t reset_phase, enum_speed, rx_barrier, reset_nak, reset_fifo;
  /* One-way: set by HAL_PCD_LifecycleShutdownBegin until MCU reset. */
  uint8_t shutdown;
  /* An eight-byte SETUP captured in Setup and not yet dispatched at STUP. */
  uint8_t setup_ready;
  /* Set by IRQ-side captures for the project's executor wake. */
  uint8_t facts;
} PCD_LifecycleTypeDef;

/* A zero-initialized context may be initialized once. Init never erases owners.
 * The handle's pData links the context for HAL_PCD_IRQHandler. */
HAL_StatusTypeDef HAL_PCD_LifecycleInit(PCD_LifecycleTypeDef *life,
                                      PCD_HandleTypeDef *handle, uint64_t reset);
/* Bind storage/identity before publishing a hardware arm, under the caller's
 * transport lock. Capacity is allocated storage, not requested transfer length. */
HAL_StatusTypeDef HAL_PCD_LifecycleBindRx(PCD_LifecycleTypeDef *life, uint8_t ep,
                                        uint8_t *base, uint32_t capacity, uint64_t arm);
HAL_StatusTypeDef HAL_PCD_LifecycleBindIn(PCD_LifecycleTypeDef *life, uint8_t ep,
                                        uint64_t arm);
HAL_StatusTypeDef HAL_PCD_QuiesceBegin(PCD_LifecycleTypeDef *life, uint8_t address,
                                     uint64_t operation, uint64_t arm,
                                     PCD_QuiesceDisposition disposition, uint32_t now_us);
/* One transaction, never a hardware-ready loop. HAL_BUSY means pending;
 * HAL_OK is physical release; HAL_ERROR/HAL_TIMEOUT retain failed ownership. */
HAL_StatusTypeDef HAL_PCD_QuiesceStep(PCD_LifecycleTypeDef *life, uint8_t address,
                                    uint64_t operation, uint32_t now_us);
HAL_StatusTypeDef HAL_PCD_QuiesceRetry(PCD_LifecycleTypeDef *life, uint8_t address,
                                     uint64_t operation, uint32_t now_us);
/* Reopen only a released owner. The caller's transport transaction must cover
 * this gate, binding the successor storage and the actual hardware arm. */
HAL_StatusTypeDef HAL_PCD_LifecycleOpenGate(PCD_LifecycleTypeDef *life, uint8_t address,
                                          uint64_t operation);
/* Same-operation rollback of a released owner whose gate was never reopened
 * (OPEN may have reactivated it; nothing can have been armed): deactivate and
 * clear it as CLOSE without another NAK/disable/flush cycle. Anything still
 * active, completing or owing RX debt fails closed (HAL_ERROR). */
HAL_StatusTypeDef HAL_PCD_QuiesceClose(PCD_LifecycleTypeDef *life, uint8_t address,
                                     uint64_t operation);
/* While suspended, owners pause their handshakes and FIFO-ticket acquisition,
 * so no GRSTCTL command starts under a gated PHY clock; a held ticket's flush
 * completes. The caller gates the PHY only while no FIFO owner exists. */
void HAL_PCD_LifecycleSuspend(PCD_LifecycleTypeDef *life, uint8_t suspended,
                              uint32_t now_us);
/* Capture precedes W1C and any isolation. Returns retired flags, including a
 * non-owned INEPNE notification; other unrelated flags stay with the normal
 * handler. The caller supplies its observed identity. INEPNE that reasserts
 * after its W1C is contained on that endpoint alone, owned or not: DAINTMSK
 * isolation and a retained failure record (a non-owned endpoint is gated). */
uint32_t HAL_PCD_QuiesceObserve(PCD_LifecycleTypeDef *life, uint8_t address,
                              uint64_t reset, uint64_t arm, uint32_t flags);
/* IRQ-side effective global OUT NAK fact, from GINTSTS or a consumed RX status.
 * Ticket identity remains the original acquisition across an endpoint retry. */
uint32_t HAL_PCD_LifecycleObserveOutNak(PCD_LifecycleTypeDef *life, uint64_t reset,
                                       uint64_t ticket, uint32_t flags);
/* IRQ-only parser transaction. One budget spans both IRQ passes.
 * pass: 0 non-CDC, 1 CDC OUT3, 2 HID-only/no split. A popped transfer-complete
 * pattern retires an ungated endpoint's payload debt: slave-mode XFRC follows
 * it, so the arm's payload was consumed in order. Gated owners keep their debt
 * for HAL_PCD_LifecycleRxDebtDone. A popped setup-complete pattern returns
 * HAL_BUSY, ending the drain before any later SETUP is parsed. */
HAL_StatusTypeDef HAL_PCD_LifecycleRxStep(PCD_LifecycleTypeDef *life, uint8_t pass,
                                        PCD_RxBudget *budget, PCD_RxEvent *event);
/* IRQ-only, at STUP: 1 consumes the SETUP captured since the last dispatch;
 * 0 when the capture was rejected, reset, flushed or already dispatched. */
uint8_t HAL_PCD_LifecycleTakeSetup(PCD_LifecycleTypeDef *life);
/* Request an RX-empty observation made after this call. QuiesceStep and
 * ResetStep request it themselves; the caller software-pends the OTG IRQ while
 * rx_barrier is set. */
void HAL_PCD_LifecycleRequestRxBarrier(PCD_LifecycleTypeDef *life);
/* OTG IRQ entry, before its no-interrupt return: completes a requested barrier
 * when no entry is queued. Returns 1 when it completed. */
uint8_t HAL_PCD_LifecycleRxBarrier(PCD_LifecycleTypeDef *life);
/* OTG raw-reset capture: adopt the new identity, making every old owner, arm
 * and allocation stale. Arms and TX refill stay excluded until
 * HAL_PCD_LifecycleResetStep proves controller cleanup. Never flushes.
 * During a shutdown it restarts that join under the original deadline; after
 * the shutdown's terminal result it is ignored. */
void HAL_PCD_LifecycleResetBegin(PCD_LifecycleTypeDef *life, uint64_t reset, uint32_t now_us);
/* Authorized shutdown admission: soft-detach, then adopt the new identity as
 * a raw reset does. anchor_us is the authorization's wall-time anchor; no
 * later raw reset renews it. Once only; never flushes or waits. */
void HAL_PCD_LifecycleShutdownBegin(PCD_LifecycleTypeDef *life, uint64_t reset,
                                    uint32_t anchor_us);
/* One bounded reset-join phase per call, never a hardware-ready loop. HAL_BUSY
 * is pending with PCD_RESET_WAIT_* bits in *wait. HAL_OK: physical cleanup and
 * the same-reset ENUMDNE join are complete. HAL_TIMEOUT/HAL_ERROR: retained
 * until a new raw reset. Absent ENUMDNE after cleanup is a non-spinning wait.
 * A shutdown join ends at HAL_OK or failure; both isolate the controller. */
HAL_StatusTypeDef HAL_PCD_LifecycleResetStep(PCD_LifecycleTypeDef *life, uint32_t now_us,
                                           uint8_t *wait);
/* A pending shutdown's escape at its wall deadline: the independent safe
 * detach and interrupt isolation, then a failed terminal result. Register
 * writes only, valid while a FIFO command stays busy; owners stay retained. */
void HAL_PCD_LifecycleShutdownIsolate(PCD_LifecycleTypeDef *life);
/* ENUMDNE IRQ: latch enumerated speed and reset identity only. */
void HAL_PCD_LifecycleEnumDone(PCD_LifecycleTypeDef *life);
/* After an empty IRQ drain, the policy owner consumes/discards old OUT data
 * under its generation rules. Neither an empty FIFO nor timeout clears debt. */
HAL_StatusTypeDef HAL_PCD_LifecycleRxDebtDone(PCD_LifecycleTypeDef *life, uint8_t ep,
                                            uint64_t reset, uint64_t arm);

#endif
