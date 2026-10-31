#ifndef QBUFFER_H_
#define QBUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "def.h"



// V260909R1: 한 producer/한 consumer. length 항목 요청은 prefix 부분 성공이 가능하다.
// create/flush는 외부에서 양쪽을 정지시킨 후 호출. peek 후 NULL-data write/read로 commit 가능.
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
bool     qbufferWrite(qbuffer_t *p_node, uint8_t *p_data, uint32_t length);
bool     qbufferRead(qbuffer_t *p_node, uint8_t *p_data, uint32_t length);
uint8_t *qbufferPeekWrite(qbuffer_t *p_node);
uint8_t *qbufferPeekRead(qbuffer_t *p_node);
uint32_t qbufferAvailable(qbuffer_t *p_node);
void     qbufferFlush(qbuffer_t *p_node);



#ifdef __cplusplus
}
#endif

#endif