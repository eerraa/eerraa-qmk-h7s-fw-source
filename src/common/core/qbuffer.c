#include "qbuffer.h"
#include <stddef.h>
#include <string.h>

// V260909R1: 고정 저장소 SPSC 큐. create/flush는 producer와 consumer가 정지한 때만 호출한다.
// 각 인덱스의 유일 writer와 acquire/release로 payload 수명을 정의한다. 다중 생산자 큐가 아니다.
_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0), "qbuffer needs lock-free 32-bit indices");
static bool qbufferValid(const qbuffer_t *q)
{
  return q != NULL && q->len >= 2U && q->size != 0U && q->len <= UINT32_MAX / q->size;
}
void qbufferInit(void) {}

bool qbufferCreate(qbuffer_t *q, uint8_t *buffer, uint32_t length)
{
  return qbufferCreateBySize(q, buffer, 1U, length);
}
bool qbufferCreateBySize(qbuffer_t *q, uint8_t *buffer, uint32_t size, uint32_t length)
{
  if (q == NULL) return false;
  memset(q, 0, sizeof(*q));
  if (length < 2U || size == 0U || length > UINT32_MAX / size) return false;
  q->len = length;
  q->size = size;
  q->p_buf = buffer;  // NULL 저장소는 기존 index-only 사용 규약을 유지한다.
  return true;
}

bool qbufferWrite(qbuffer_t *q, uint8_t *data, uint32_t length)
{
  if (!qbufferValid(q)) return false;
  for (uint32_t i = 0; i < length; i++) {
    uint32_t in = __atomic_load_n(&q->in, __ATOMIC_RELAXED);
    uint32_t out = __atomic_load_n(&q->out, __ATOMIC_ACQUIRE);
    if (in >= q->len || out >= q->len) return false;
    uint32_t next = in + 1U == q->len ? 0U : in + 1U;
    if (next == out) return false;
    if (q->p_buf != NULL && data != NULL) { memcpy(&q->p_buf[in * q->size], data, q->size); data += q->size; }
    __atomic_store_n(&q->in, next, __ATOMIC_RELEASE);
  }
  return true;
}
bool qbufferRead(qbuffer_t *q, uint8_t *data, uint32_t length)
{
  if (!qbufferValid(q)) return false;
  for (uint32_t i = 0; i < length; i++) {
    uint32_t out = __atomic_load_n(&q->out, __ATOMIC_RELAXED);
    uint32_t in = __atomic_load_n(&q->in, __ATOMIC_ACQUIRE);
    if (out >= q->len || in >= q->len || out == in) return false;
    // 빈 큐는 출력에 쓰지 않는다. 부분 요청의 성공한 prefix만 기존 규약대로 소비한다.
    if (q->p_buf != NULL && data != NULL) { memcpy(data, &q->p_buf[out * q->size], q->size); data += q->size; }
    __atomic_store_n(&q->out, out + 1U == q->len ? 0U : out + 1U, __ATOMIC_RELEASE);
  }
  return true;
}
uint8_t *qbufferPeekWrite(qbuffer_t *q)
{
  if (!qbufferValid(q) || q->p_buf == NULL) return NULL;
  uint32_t in = __atomic_load_n(&q->in, __ATOMIC_RELAXED);
  uint32_t out = __atomic_load_n(&q->out, __ATOMIC_ACQUIRE);
  if (in >= q->len || out >= q->len || (in + 1U == q->len ? 0U : in + 1U) == out) return NULL;
  return &q->p_buf[in * q->size];
}
uint8_t *qbufferPeekRead(qbuffer_t *q)
{
  if (!qbufferValid(q) || q->p_buf == NULL) return NULL;
  uint32_t out = __atomic_load_n(&q->out, __ATOMIC_RELAXED);
  uint32_t in = __atomic_load_n(&q->in, __ATOMIC_ACQUIRE);
  if (out >= q->len || in >= q->len || out == in) return NULL;
  return &q->p_buf[out * q->size];
}
uint32_t qbufferAvailable(qbuffer_t *q)
{
  if (!qbufferValid(q)) return 0U;
  uint32_t out = __atomic_load_n(&q->out, __ATOMIC_ACQUIRE);
  uint32_t in = __atomic_load_n(&q->in, __ATOMIC_ACQUIRE);
  if (out >= q->len || in >= q->len) return 0U;
  return in >= out ? in - out : q->len - out + in;
}
void qbufferFlush(qbuffer_t *q)
{
  if (q == NULL) return;
  __atomic_store_n(&q->in, 0U, __ATOMIC_RELAXED);
  __atomic_store_n(&q->out, 0U, __ATOMIC_RELAXED);
}
