// HID report transport shared by every USB stack binding: ordered IN queues per
// endpoint, VIA request/reply flow with OUT backpressure, and the admitted session
// generation. The binding behind hid_port.h owns endpoints, control requests,
// descriptors and Remote Wake.
#include <string.h>
#include "hid_port.h"
#include "hid_tx_queue.h"
#include "micros.h"
#include "usb.h"


// TIM2/일반 PWM callback 없이 EP별 FIFO와 완료 기반 pump로 보낸다.
#define HID_TX_DEPTH 128U
#define HID_VIA_RX_DEPTH 16U
static hid_tx_packet_t keyboard_slots[HID_TX_DEPTH];
static hid_tx_packet_t extra_slots[HID_TX_DEPTH];
static hid_tx_packet_t via_slots[HID_TX_DEPTH];
static hid_tx_queue_t keyboard_tx, extra_tx, via_tx;
static hid_tx_packet_t keyboard_latest = { .length = HID_KEYBOARD_REPORT_SIZE };
static hid_tx_packet_t extra_latest[3] = {
  { .length = 6U, .data = {2U} },
  { .length = 3U, .data = {3U} },
  { .length = 3U, .data = {4U} },
};
static bool keyboard_reconcile;
static bool keyboard_latest_session_bound;
static uint8_t extra_latest_session_bound;
/* Logical physical-key updates may share this candidate while the endpoint is
 * busy. It is separate from the immutable transport queue; an idle endpoint
 * freezes it immediately, even halfway through the producing matrix scan. */
static struct {
  hid_tx_packet_t packet;
  uint32_t scan_token;
  uint8_t touched[32];
  bool pressed;
  bool valid;
} keyboard_candidate;
// FIFO 스냅샷의 완료 시점과 유지 시간만 소유한다. QMK 키 상태는 만지지 않는다.
static uint32_t keyboard_completed_us;
static uint16_t keyboard_delay_ms;
static uint16_t keyboard_active_delay_ms;
static bool keyboard_completed;
static uint8_t extra_reconcile;
static bool admitted;
static uint32_t transport_generation;
static usb_hid_transport_stats_t transport_stats;
static uint8_t via_rx[HID_VIA_RX_DEPTH][HID_VIA_EP_SIZE];
static uint8_t via_rx_head, via_rx_count;
static bool via_rx_armed;
static bool via_reserved;
static uint8_t via_rx_report[HID_VIA_EP_SIZE] __attribute__((aligned(4)));
// Boot protocol에서 무장한 keyboard 보고. 다음 무장은 완료 뒤에만 일어나므로 전송 중에는 불변이다.
static uint8_t keyboard_boot_report[HID_BOOT_KEYBOARD_REPORT_SIZE] __attribute__((aligned(4)));

static bool usbHidFreezeKeyCandidateLocked(void);
static void usbHidPumpLocked(void);
static void usbHidRearmViaLocked(void);

bool usbHidAdmissionOpen(void)
{
  return admitted && !usbIsResetPending();
}

uint32_t usbHidTransportGeneration(void)
{
  return transport_generation;
}

static bool usbHidLinkValid(usb_hid_link_t link)
{
  return link == USB_HID_LINK_CONFIGURED || link == USB_HID_LINK_SLEEPING;
}

static bool usbHidLinkSuspended(usb_hid_link_t link)
{
  return link == USB_HID_LINK_SUSPENDED || link == USB_HID_LINK_SLEEPING;
}

static bool usbHidSessionValid(void)
{
  if (!usbHidAdmissionOpen()) return false;
  return usbHidLinkValid(usbHidPortLink());
}

// 구성마다 새 세대로 완전히 초기화한다.
void usbHidTransportOpen(void)
{
  admitted = true;
  usbHidTransportReset();
}

void usbHidTransportRetireLocked(void)
{
  // Retiring software admission cannot release storage still owned by the controller.
  via_reserved = false;
  if (admitted) {
    admitted = false;
    transport_generation++;
  }
  usbHidPortFrameDemand(false);
}

void usbHidTransportReset(void)
{
  transport_stats.session_discards += keyboard_tx.count + extra_tx.count + via_tx.count + via_rx_count +
                                     (keyboard_candidate.valid ? 1U : 0U);
  memset(&keyboard_candidate, 0, sizeof(keyboard_candidate));
  transport_generation++;
  via_reserved = false;
  // Synthetic contributions belong to their admitted session. The main loop
  // republishes the surviving physical owners after cancelling the old macro.
  if (keyboard_latest_session_bound) memset(keyboard_latest.data, 0, sizeof(keyboard_latest.data));
  for (uint8_t i = 0U; i < 3U; i++)
    if (extra_latest_session_bound & (1U << i)) memset(&extra_latest[i].data[1], 0, sizeof(extra_latest[i].data) - 1U);
  keyboard_latest_session_bound = false;
  extra_latest_session_bound = 0U;
  hidTxInit(&keyboard_tx, keyboard_slots, HID_TX_DEPTH);
  hidTxInit(&extra_tx, extra_slots, HID_TX_DEPTH);
  hidTxInit(&via_tx, via_slots, HID_TX_DEPTH);
  keyboard_reconcile = true;
  extra_reconcile = 7U;
  keyboard_latest.delay_after_ms = 0U;
  keyboard_delay_ms = keyboard_active_delay_ms = 0U;
  keyboard_completed = false;
  via_rx_count = via_rx_head = 0U;
  via_rx_armed = false;
}

bool usbHidTransportArmVia(void)
{
  usbHidRearmViaLocked();
  return via_rx_armed;
}

void usbHidTransportInComplete(uint8_t ep_num)
{
  uint32_t irq = usbHidLock();
  if (usbHidAdmissionOpen()) {
    if (ep_num == (HID_EPIN_ADDR & 0xFU)) {
      if (hidTxComplete(&keyboard_tx))
      {
        keyboard_completed_us = micros();
        keyboard_completed = true;
        keyboard_delay_ms = keyboard_active_delay_ms;
        keyboard_active_delay_ms = 0U;
      }
    } else if (ep_num == (HID_EXK_EP_IN & 0xFU)) {
      hidTxComplete(&extra_tx);
    } else if (ep_num == (HID_VIA_EP_IN & 0xFU)) {
      hidTxComplete(&via_tx);
    }
    // 별도 타이머 위상/다음 SOF를 기다리지 않고 다음 head를 즉시 무장한다.
    usbHidPumpLocked();
  }
  usbHidUnlock(irq);
}

bool usbHidTransportOutComplete(uint8_t ep_num, uint32_t length)
{
  uint32_t irq = usbHidLock();
  if (!usbHidAdmissionOpen() || ep_num != (HID_VIA_EP_OUT & 0xFU) || !via_rx_armed) {
    usbHidUnlock(irq);
    return false;
  }
  via_rx_armed = false;
  if (length == HID_VIA_EP_SIZE && via_rx_count < HID_VIA_RX_DEPTH) {
    memcpy(via_rx[(via_rx_head + via_rx_count) % HID_VIA_RX_DEPTH], via_rx_report, HID_VIA_EP_SIZE);
    via_rx_count++;
  } else {
    transport_stats.invalid_rx++;
  }
  // 여유가 있으면 즉시 재무장, 꽉 차면 ACK 후 폐기 대신 OUT NAK로 backpressure.
  usbHidRearmViaLocked();
  usbHidUnlock(irq);
  return true;
}

void usbHidTransportSof(void)
{
  uint32_t irq = usbHidLock();
  usbHidPumpLocked();  // arm 실패/재구성 후의 bounded 재시도만 담당한다.
  usbHidRearmViaLocked();
  // Settled after the pump, so the arming frame releases its own demand.
  usbHidPortFrameDemand(usbHidTransportFrameWanted());
  usbHidUnlock(irq);
}

// Mirrors the pump: only work a later frame could arm holds demand. In-flight
// packets and a full request queue are completion-driven and hold none.
bool usbHidTransportFrameWanted(void)
{
  if (!usbHidAdmissionOpen() || usbHidPortLink() != USB_HID_LINK_CONFIGURED) return false;
  return (!keyboard_tx.busy && (keyboard_tx.count || keyboard_candidate.valid || keyboard_reconcile)) ||
         (!extra_tx.busy && (extra_tx.count || extra_reconcile)) ||
         (!via_tx.busy && via_tx.count) ||
         (!via_rx_armed && via_rx_count < HID_VIA_RX_DEPTH);
}

void usbHidTransportCountInvalidControl(void)
{
  transport_stats.invalid_control++;
}

// TX FIFO, 엔드포인트 무장, lifecycle은 동일 PRIMASK 규약 아래에서만 접근한다.
static bool usbHidArm(void *context, const hid_tx_packet_t *packet)
{
  uint8_t ep = (uint8_t)(uintptr_t)context;
  uint8_t *data = (uint8_t *)packet->data;
  uint32_t length = packet->length;
  if (ep == HID_EPIN_ADDR && usbHidPortKeyboardProtocol() == 0U) {
    // Boot 호스트는 report descriptor 없이 8바이트만 읽는다. 앞에서부터 비어 있지 않은 슬롯 6개를 싣는다.
    memset(keyboard_boot_report, 0, sizeof(keyboard_boot_report));
    keyboard_boot_report[0] = packet->data[0];
    for (uint32_t i = 2U, n = 2U; i < HID_KEYBOARD_REPORT_SIZE && n < sizeof(keyboard_boot_report); i++)
      if (packet->data[i] != 0U) keyboard_boot_report[n++] = packet->data[i];
    data = keyboard_boot_report;
    length = sizeof(keyboard_boot_report);
  }
  bool ok = usbHidPortTransmit(ep, data, length);
  if (!ok) transport_stats.arm_failures++;
  if (ok && ep == HID_EPIN_ADDR) keyboard_active_delay_ms = packet->delay_after_ms;
  return ok;
}

bool usbHidTransportRestartEndpointLocked(uint8_t address)
{
  if (address == HID_VIA_EP_OUT) {
    via_rx_armed = false;
    // A full queue deliberately remains NAKed, and a logically suspended link
    // refuses the arm until resume: both rearm at SOF, keeping the session.
    // Available capacity on a running link must rearm now.
    return via_rx_count == HID_VIA_RX_DEPTH || usbHidLinkSuspended(usbHidPortLink()) ||
           usbHidTransportArmVia();
  }
  hid_tx_queue_t *queue;
  switch (address) {
    case HID_EPIN_ADDR: queue = &keyboard_tx; break;
    case HID_EXK_EP_IN: queue = &extra_tx; break;
    case HID_VIA_EP_IN: queue = &via_tx; break;
    default: return false;
  }
  // The active slot is outside the pending FIFO, which may already be completely full.
  // An interval may have been extended after the packet's original arm.
  if (queue->busy && address == HID_EPIN_ADDR)
    queue->active.delay_after_ms = keyboard_active_delay_ms;
  return !queue->busy || usbHidArm((void *)(uintptr_t)address, &queue->active);
}

static usb_hid_session_t usbHidSessionRead(void)
{
  usb_hid_link_t link = usbHidPortLink();
  return (usb_hid_session_t){
    .generation = transport_generation,
    .valid = usbHidAdmissionOpen() && usbHidLinkValid(link),
    .suspended = usbHidLinkSuspended(link),
  };
}

// Per main-loop pass: one lock-free attempt inside the settle bracket, then at
// most one locked read, so a host flood cannot hold the caller here.
usb_hid_session_t usbHidGetSession(void)
{
  uint32_t seq = usbHidSnapshotBegin();
  usb_hid_session_t session = usbHidSessionRead();
  if (usbHidSnapshotValid(seq)) return session;
  uint32_t irq = usbHidLock();
  session = usbHidSessionRead();
  usbHidUnlock(irq);
  return session;
}

// Endpoint metadata belongs to the admitted HID session, independently of pending BootMode.
const char *usbHidGetPollingLabel(void)
{
  bool high_speed = false;
  uint8_t interval = 0U;
  uint32_t irq = usbHidLock();
  bool valid = usbHidSessionValid() && usbHidPortKeyboardPolling(&high_speed, &interval);
  usbHidUnlock(irq);

  if (valid) {
    if (!high_speed && interval == 1U) return "1000 Hz (FS)";
    if (high_speed) {
      switch (interval) {
        case 1U: return "8000 Hz (HS)";
        case 2U: return "4000 Hz (HS)";
        case 3U: return "2000 Hz (HS)";
      }
    }
  }
  return "Unavailable";
}

static bool usbHidFreezeKeyCandidateLocked(void)
{
  if (!keyboard_candidate.valid) return true;
  if (!hidTxPush(&keyboard_tx, &keyboard_candidate.packet)) return false;
  keyboard_candidate.valid = false;
  return true;
}

// The physical gate precedes the kick: hidTxKick replaces `active` before its
// arm callback, while a gated endpoint may still own the retained descriptor.
static void usbHidKickLocked(hid_tx_queue_t *queue, uint8_t address)
{
  if (!queue->busy && queue->count != 0U && usbHidPortArmable(address))
    hidTxKick(queue, usbHidArm, (void *)(uintptr_t)address);
}

static void usbHidPumpLocked(void)
{
  if (!usbHidAdmissionOpen()) return;
  usb_hid_link_t link = usbHidPortLink();
  if (!usbHidLinkValid(link)) return;
  // No end-of-scan, key-count or SOF gate: completion can consume even one update.
  if (!keyboard_tx.busy && !usbHidFreezeKeyCandidateLocked()) return;
  // 정상 경로는 모든 전이를 FIFO에 유지한다. 유한 큐 overflow 뒤에만 최종 상태를 복구한다.
  if (keyboard_reconcile && keyboard_tx.count == 0U) {
    hidTxPush(&keyboard_tx, &keyboard_latest);
    keyboard_reconcile = false;
  }
  if (extra_reconcile && extra_tx.count == 0U) {
    for (uint8_t i = 0; i < 3U; i++)
      if (extra_reconcile & (1U << i)) hidTxPush(&extra_tx, &extra_latest[i]);
    extra_reconcile = 0U;
  }
  // 같은 세션의 suspend는 전이 큐를 유지하되 물리 IN 무장은 resume 이후만 한다.
  if (link != USB_HID_LINK_CONFIGURED) return;
  // 기본 경로는 시계를 읽지 않는다. 유지 중에도 다른 IN 엔드포인트는 계속 서비스한다.
  if (keyboard_delay_ms == 0U || (uint32_t)(micros() - keyboard_completed_us) >= (uint32_t)keyboard_delay_ms * 1000U)
  {
    keyboard_delay_ms = 0U;
    usbHidKickLocked(&keyboard_tx, HID_EPIN_ADDR);
  }
  usbHidKickLocked(&extra_tx, HID_EXK_EP_IN);
  usbHidKickLocked(&via_tx, HID_VIA_EP_IN);
}

// 형식은 무장할 때 정하므로 대기 중인 보고도 새 형식으로 나간다. 호스트는 protocol을 바꾸면 이전 형식의 키
// 상태를 버리므로 현재 상태를 새 형식으로 한 번 더 보낸다.
void usbHidTransportProtocolChangedLocked(void)
{
  usbHidFreezeKeyCandidateLocked();
  hid_tx_packet_t resync = keyboard_latest;
  resync.delay_after_ms = 0U;
  if (keyboard_reconcile || !usbHidSessionValid() || !hidTxPush(&keyboard_tx, &resync)) keyboard_reconcile = true;
  usbHidPumpLocked();
}

static void usbHidRearmViaLocked(void)
{
  if (usbHidAdmissionOpen() && !via_rx_armed && via_rx_count < HID_VIA_RX_DEPTH) {
    via_rx_armed = usbHidPortReceive(HID_VIA_EP_OUT, via_rx_report, sizeof(via_rx_report));
  }
}

bool usbHidReadViaRequest(uint8_t *data, uint32_t *generation)
{
  if (data == NULL || generation == NULL) return false;
  // Only this consumer decrements the count, so a stale zero only defers a
  // request to the next pass; a nonzero count is re-tested under the lock.
  if (__atomic_load_n(&via_rx_count, __ATOMIC_RELAXED) == 0U) return false;
  uint32_t irq = usbHidLock();
  // 응답 슬롯을 확보할 수 있을 때만 부작용을 가진 다음 명령을 꺼낸다. TX 생산자는 하나다.
  bool ready = usbHidAdmissionOpen() && usbHidPortLink() == USB_HID_LINK_CONFIGURED &&
               !via_reserved && via_rx_count != 0U && via_tx.count < via_tx.capacity;
  if (ready) {
    memcpy(data, via_rx[via_rx_head], HID_VIA_EP_SIZE);
    *generation = transport_generation;
    via_reserved = true;
    via_rx_head = (via_rx_head + 1U) % HID_VIA_RX_DEPTH;
    via_rx_count--;
    usbHidRearmViaLocked();
  }
  usbHidUnlock(irq);
  return ready;
}

bool usbHidEnqueueViaResponse(const uint8_t *data, uint8_t length, uint32_t generation)
{
  if (data == NULL || length != HID_VIA_EP_SIZE) return false;
  hid_tx_packet_t packet = { .length = HID_VIA_EP_SIZE };
  memcpy(packet.data, data, length);
  uint32_t irq = usbHidLock();
  bool ok = via_reserved && usbHidAdmissionOpen() && generation == transport_generation &&
            hidTxPush(&via_tx, &packet);
  if (ok) via_reserved = false;
  usbHidPumpLocked();
  usbHidUnlock(irq);
  return ok;
}

// Suspend entered by a host that had configured this device. Bus idle before enumeration also
// raises Suspend, but no host is asleep then and a key held at power-on must still reach it.
bool usbHidHostSleeping(void)
{
  return usbHidAdmissionOpen() && usbHidPortLink() == USB_HID_LINK_SLEEPING;
}

// 마지막으로 수락한 리포트 뒤의 간격을 연장한다. active payload는 불변이다.
static void usbHidDelayKeyboard(uint16_t delay_ms, bool session_bound, uint32_t generation)
{
  if (delay_ms == 0U)
  {
    return;
  }
  uint32_t irq = usbHidLock();
  if ((!session_bound || generation == transport_generation) && usbHidSessionValid())
  {
    uint16_t *interval = NULL;
    usbHidFreezeKeyCandidateLocked();
    if (keyboard_candidate.valid)
    {
      interval = &keyboard_candidate.packet.delay_after_ms;
    }
    else if (keyboard_tx.count != 0U)
    {
      uint16_t tail = (keyboard_tx.head + keyboard_tx.count - 1U) % keyboard_tx.capacity;
      interval = &keyboard_tx.slots[tail].delay_after_ms;
    }
    else if (keyboard_tx.busy)
    {
      interval = &keyboard_active_delay_ms;
    }
    else if (keyboard_completed)
    {
      interval = &keyboard_delay_ms;
    }
    if (interval != NULL && *interval < delay_ms)
    {
      *interval = delay_ms;
    }
  }
  usbHidUnlock(irq);
}

void usbHidDelayKeyboardReport(uint16_t delay_ms)
{
  usbHidDelayKeyboard(delay_ms, false, 0U);
}

void usbHidDelayKeyboardReportForGeneration(uint16_t delay_ms, uint32_t generation)
{
  usbHidDelayKeyboard(delay_ms, true, generation);
}

/* Validate the producer's declared single-key update against the preceding
 * snapshot. Slot reordering, a filter changing other usages, duplicates, and
 * modifiers all fall back to ordinary immutable reports. */
static bool usbHidIsSingleKeyUpdate(const hid_tx_packet_t *packet, uint8_t usage, bool pressed)
{
  if (usage < 4U || usage >= 0xE0U || packet->data[0] != 0U || packet->data[1] != 0U ||
      keyboard_latest.data[0] != 0U || keyboard_latest.data[1] != 0U) return false;
  bool changed = false;
  for (uint32_t i = 2U; i < HID_KEYBOARD_REPORT_SIZE; i++) {
    uint8_t before = keyboard_latest.data[i], after = packet->data[i];
    if (before == after) {
      if (before == usage) return false;
      continue;
    }
    if (changed || (pressed ? (before != 0U || after != usage) : (before != usage || after != 0U)))
      return false;
    changed = true;
  }
  return changed;
}

static bool usbHidSubmitKeyboard(uint8_t *data, uint16_t length, uint32_t scan_token, uint8_t usage, bool pressed, bool session_bound, uint32_t generation)
{
  if (data == NULL || length != HID_KEYBOARD_REPORT_SIZE) return false;
  hid_tx_packet_t packet = { .length = HID_KEYBOARD_REPORT_SIZE };
  memcpy(packet.data, data, length);
  uint32_t irq = usbHidLock();
  if (session_bound && (generation != transport_generation || !usbHidSessionValid())) {
    usbHidUnlock(irq);
    return false;
  }
  usbHidPumpLocked();
  bool configured = usbHidSessionValid();
  bool eligible = scan_token != 0U && configured && usbHidPortLink() == USB_HID_LINK_CONFIGURED &&
                  usbHidPortKeyboardProtocol() == 1U && !keyboard_reconcile && keyboard_tx.busy &&
                  keyboard_tx.count == 0U && keyboard_active_delay_ms == 0U && keyboard_delay_ms == 0U &&
                  usbHidIsSingleKeyUpdate(&packet, usage, pressed);
  bool merge = eligible && keyboard_candidate.valid && keyboard_candidate.scan_token == scan_token &&
               keyboard_candidate.pressed == pressed &&
               (keyboard_candidate.touched[usage / 8U] & (1U << (usage % 8U))) == 0U;
  bool ok = false;
  if (merge) {
    memcpy(keyboard_candidate.packet.data, packet.data, packet.length);
    keyboard_candidate.touched[usage / 8U] |= 1U << (usage % 8U);
    ok = true;
  } else if (usbHidFreezeKeyCandidateLocked()) {
    if (eligible && keyboard_tx.count == 0U) {
      keyboard_candidate.packet = packet;
      keyboard_candidate.scan_token = scan_token;
      memset(keyboard_candidate.touched, 0, sizeof(keyboard_candidate.touched));
      keyboard_candidate.touched[usage / 8U] = 1U << (usage % 8U);
      keyboard_candidate.pressed = pressed;
      keyboard_candidate.valid = true;
      ok = true;
    } else {
      ok = configured && !keyboard_reconcile && hidTxPush(&keyboard_tx, &packet);
    }
  }
  keyboard_latest = packet;
  keyboard_latest_session_bound = session_bound;
  if (!ok) {
    if (configured) {
      transport_stats.keyboard_coalesced++;
    }
    keyboard_reconcile = true;
  }
  usbHidPumpLocked();
  usbHidUnlock(irq);
  return ok;
}

bool usbHidSendReport(uint8_t *data, uint16_t length)
{
  return usbHidSubmitKeyboard(data, length, 0U, 0U, false, false, 0U);
}

bool usbHidSendReportForGeneration(uint8_t *data, uint16_t length, uint32_t generation)
{
  return usbHidSubmitKeyboard(data, length, 0U, 0U, false, true, generation);
}

bool usbHidSubmitKeyUpdate(uint8_t *data, uint16_t length, uint32_t scan_token, uint8_t usage, bool pressed)
{
  return usbHidSubmitKeyboard(data, length, scan_token, usage, pressed, false, 0U);
}

void usbHidEndKeyScan(uint32_t scan_token)
{
  // Only this thread sets the candidate; other contexts only freeze or discard
  // it, so a false read is exactly what the locked body would decide.
  if (!__atomic_load_n(&keyboard_candidate.valid, __ATOMIC_RELAXED)) return;
  uint32_t irq = usbHidLock();
  if (keyboard_candidate.valid && (scan_token == 0U || keyboard_candidate.scan_token == scan_token)) {
    usbHidFreezeKeyCandidateLocked();
    usbHidPumpLocked();
  }
  usbHidUnlock(irq);
}

static bool usbHidSubmitExtra(uint8_t *data, uint16_t length, bool session_bound, uint32_t generation)
{
  if (data == NULL || length == 0U || data[0] < 2U || data[0] > 4U ||
      length != (data[0] == 2U ? 6U : 3U)) return false;
  // Indexed immutable layouts let GCC 10.3 copy words instead of byte-loop clearing.
  static const hid_tx_packet_t layouts[2] = { { .length = 3U }, { .length = 6U } };
  hid_tx_packet_t packet = layouts[length == 6U];
  if (length == 6U) memcpy(packet.data, data, 6U);
  else memcpy(packet.data, data, 3U);
  uint32_t irq = usbHidLock();
  if (session_bound && (generation != transport_generation || !usbHidSessionValid())) {
    usbHidUnlock(irq);
    return false;
  }
  usbHidFreezeKeyCandidateLocked();
  usbHidPumpLocked();
  uint8_t index = data[0] - 2U;
  extra_latest[index] = packet;
  if (session_bound) extra_latest_session_bound |= 1U << index;
  else extra_latest_session_bound &= ~(1U << index);
  // 상대 이동/휠은 재연결 및 overflow 복구에서 반복하지 않는다. 버튼만 현재 상태다.
  if (index == 0U) memset(&extra_latest[0].data[2], 0, 4U);
  bool configured = usbHidSessionValid();
  bool ok = configured && !extra_reconcile && hidTxPush(&extra_tx, &packet);
  if (!ok) {
    if (configured) {
      transport_stats.extra_coalesced++;
    }
    extra_reconcile |= 1U << index;
  }
  usbHidPumpLocked();
  usbHidUnlock(irq);
  return ok;
}

bool usbHidSendReportEXK(uint8_t *data, uint16_t length)
{
  return usbHidSubmitExtra(data, length, false, 0U);
}

bool usbHidSendReportEXKForGeneration(uint8_t *data, uint16_t length, uint32_t generation)
{
  return usbHidSubmitExtra(data, length, true, generation);
}

void usbHidGetTransportStats(usb_hid_transport_stats_t *stats)
{
  if (stats == NULL) return;
  uint32_t irq = usbHidLock();
  *stats = transport_stats;
  usbHidUnlock(irq);
}

// 사용자 Apply/reset은 큐에 넣기만 한 응답을 전송 완료로 오인하지 않는다.
bool usbHidViaResponsesPending(void)
{
  uint32_t irq = usbHidLock();
  // Retained failed-teardown payloads belong to a retired session, not a response drain.
  bool pending = usbHidAdmissionOpen() && (via_reserved || via_tx.busy || via_tx.count != 0U);
  usbHidUnlock(irq);
  return pending;
}
