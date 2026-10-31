#pragma once
#include <stdbool.h>
#include <stdint.h>

// V260909R1: 호출자가 IRQ 직렬화를 소유한다. active.data는 완료 전까지 불변이다.
#define HID_TX_PACKET_BYTES 32U
typedef struct {
  // USB transfer buffers and every queue slot must remain word-aligned.
  uint8_t data[HID_TX_PACKET_BYTES] __attribute__((aligned(4)));
  uint8_t length;
  uint16_t delay_after_ms; // V260911R3: 전송 완료 뒤 다음 keyboard 리포트까지의 최소 간격, wire 밖의 메타데이터
} hid_tx_packet_t;

typedef struct {
  hid_tx_packet_t *slots;
  uint16_t capacity;
  uint16_t head;
  uint16_t count;
  bool busy;
  hid_tx_packet_t active;
} hid_tx_queue_t;

typedef bool (*hid_tx_arm_t)(void *context, const hid_tx_packet_t *packet);
void hidTxInit(hid_tx_queue_t *q, hid_tx_packet_t *slots, uint16_t capacity);
bool hidTxPush(hid_tx_queue_t *q, const hid_tx_packet_t *packet);
bool hidTxKick(hid_tx_queue_t *q, hid_tx_arm_t arm, void *context);
bool hidTxComplete(hid_tx_queue_t *q);
void hidTxDiscardPending(hid_tx_queue_t *q);
