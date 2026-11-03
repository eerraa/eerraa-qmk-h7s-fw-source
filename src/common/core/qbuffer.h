#ifndef QBUFFER_H_
#define QBUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "def.h"
#include <stddef.h>
#include <string.h>



// V260909R1: 한 producer/한 consumer. length 항목 요청은 prefix 부분 성공이 가능하다.
// create/flush는 외부에서 양쪽을 정지시킨 후 호출. peek 후 NULL-data write/read로 commit 가능.
// Read/write copy at most two item-sized spans from one peer-index snapshot,
// then publish the accepted prefix once. False may still transfer that prefix.
typedef struct
{
  uint32_t in;
  uint32_t out;
  uint32_t len;
  uint32_t size;

  uint8_t *p_buf;
} qbuffer_t;


void     qbufferInit(void);
bool     qbufferCreate(qbuffer_t *p_node, uint8_t *p_buf, uint32_t length);
bool     qbufferCreateBySize(qbuffer_t *p_node, uint8_t *p_buf, uint32_t size, uint32_t length);
inline bool qbufferValid(const qbuffer_t *q)
{
  return q != NULL && q->len >= 2U && q->size != 0U && q->len <= UINT32_MAX / q->size;
}

// Inline the shared mechanism so fixed single-item callers do not grow their
// existing IRQ lock. qbuffer.c also emits the ordinary external entry points.
inline bool qbufferWrite(qbuffer_t *q, uint8_t *data, uint32_t length)
{
  if (!qbufferValid(q)) return false;
  if (!length) return true;
  uint32_t in = __atomic_load_n(&q->in, __ATOMIC_RELAXED);
  uint32_t out = __atomic_load_n(&q->out, __ATOMIC_ACQUIRE);
  if (in >= q->len || out >= q->len) return false;
  uint32_t count = out > in ? out - in - 1U : q->len - in + out - 1U;
  if (count > length) count = length;
  if (!count) return false;
  uint32_t first = q->len - in;
  if (first > count) first = count;
  if (q->p_buf != NULL && data != NULL) {
    memcpy(q->p_buf + in * q->size, data, first * q->size);
    if (count > first) memcpy(q->p_buf, data + first * q->size, (count - first) * q->size);
  }
  uint32_t next = count < q->len - in ? in + count : count - (q->len - in);
  __atomic_store_n(&q->in, next, __ATOMIC_RELEASE);
  return count == length;
}
inline bool qbufferRead(qbuffer_t *q, uint8_t *data, uint32_t length)
{
  if (!qbufferValid(q)) return false;
  if (!length) return true;
  uint32_t out = __atomic_load_n(&q->out, __ATOMIC_RELAXED);
  uint32_t in = __atomic_load_n(&q->in, __ATOMIC_ACQUIRE);
  if (out >= q->len || in >= q->len || out == in) return false;
  uint32_t count = in >= out ? in - out : q->len - out + in;
  if (count > length) count = length;
  uint32_t first = q->len - out;
  if (first > count) first = count;
  if (q->p_buf != NULL && data != NULL) {
    memcpy(data, q->p_buf + out * q->size, first * q->size);
    if (count > first) memcpy(data + first * q->size, q->p_buf, (count - first) * q->size);
  }
  uint32_t next = count < q->len - out ? out + count : count - (q->len - out);
  __atomic_store_n(&q->out, next, __ATOMIC_RELEASE);
  return count == length;
}
uint8_t *qbufferPeekWrite(qbuffer_t *p_node);
uint8_t *qbufferPeekRead(qbuffer_t *p_node);
uint32_t qbufferAvailable(qbuffer_t *p_node);
void     qbufferFlush(qbuffer_t *p_node);



#ifdef __cplusplus
}
#endif

#endif