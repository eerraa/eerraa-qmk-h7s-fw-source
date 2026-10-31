#include "hid_tx_queue.h"
#include <stddef.h>
#include <string.h>

// V260909R1: 고정 메모리 FIFO. 실패한 arm은 head를 소비하지 않는다.
void hidTxInit(hid_tx_queue_t *q, hid_tx_packet_t *slots, uint16_t capacity)
{
  memset(q, 0, sizeof(*q));
  q->slots = slots;
  q->capacity = capacity;
}

bool hidTxPush(hid_tx_queue_t *q, const hid_tx_packet_t *packet)
{
  if (q == NULL || packet == NULL || q->slots == NULL || q->capacity == 0U ||
      packet->length == 0U || packet->length > HID_TX_PACKET_BYTES || q->count == q->capacity)
    return false;
  q->slots[(q->head + q->count) % q->capacity] = *packet;
  q->count++;
  return true;
}

bool hidTxKick(hid_tx_queue_t *q, hid_tx_arm_t arm, void *context)
{
  if (q == NULL || arm == NULL || q->busy || q->count == 0U) return false;
  q->active = q->slots[q->head];
  q->busy = true;
  if (!arm(context, &q->active)) {
    q->busy = false;
    return false;
  }
  q->head = (q->head + 1U) % q->capacity;
  q->count--;
  return true;
}

bool hidTxComplete(hid_tx_queue_t *q)
{
  if (q == NULL || !q->busy) return false;
  q->busy = false;
  return true;
}

void hidTxDiscardPending(hid_tx_queue_t *q)
{
  // 활성 전송은 하드웨어가 여전히 소유할 수 있으므로 변경하지 않는다.
  q->head = 0U;
  q->count = 0U;
}
