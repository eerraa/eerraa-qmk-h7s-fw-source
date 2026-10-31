#include "i2c.h"

#ifdef _USE_HW_I2C
// V260909R1: polling HAL wait 대신 실제 IT 전송/주소 ACK probe. IRQ는 완료 상태만 발행한다.
#define I2C_ASYNC_TIMEOUT_MS 10U
typedef struct {
  volatile i2c_async_result_t result;
  volatile uint32_t error;
  uint32_t started_ms;
  bool probe;
  bool read;
} i2c_async_t;
static i2c_async_t transfers[I2C_MAX_CH];

bool i2cAsyncOwned(uint8_t ch)
{
  return ch < I2C_MAX_CH && transfers[ch].result != I2C_ASYNC_IDLE;
}

static bool i2cAsyncStart(uint8_t ch, uint8_t address, uint16_t offset,
                          uint8_t *data, uint16_t length, bool probe, bool read)
{
  if (ch >= I2C_MAX_CH || address > 0x7FU || !i2cIsBegin(ch) ||
      (!probe && (data == NULL || length == 0U))) return false;
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  i2c_async_t *t = &transfers[ch];
  I2C_HandleTypeDef *h = i2cGetHandle(ch);
  if (h == NULL || t->result != I2C_ASYNC_IDLE) { __set_PRIMASK(irq); return false; }
  t->error = HAL_I2C_ERROR_NONE;
  t->probe = probe;
  t->read = read;
  t->started_ms = millis();
  t->result = I2C_ASYNC_BUSY;
  // NBYTES=0 + AUTOEND는 주소 ACK만 검사한다. 어떤 EEPROM data byte도 쓰지 않는다.
  HAL_StatusTypeDef status = probe
      ? HAL_I2C_Master_Transmit_IT(h, (uint16_t)address << 1, NULL, 0U)
      : read ? HAL_I2C_Mem_Read_IT(h, (uint16_t)address << 1, offset, I2C_MEMADD_SIZE_16BIT, data, length)
             : HAL_I2C_Mem_Write_IT(h, (uint16_t)address << 1, offset, I2C_MEMADD_SIZE_16BIT, data, length);
  if (status != HAL_OK) t->result = I2C_ASYNC_IDLE;
  __set_PRIMASK(irq);
  return status == HAL_OK;
}

bool i2cWriteA16BytesAsync(uint8_t ch, uint8_t address, uint16_t offset, uint8_t *data, uint16_t length)
{
  return i2cAsyncStart(ch, address, offset, data, length, false, false);
}

bool i2cReadA16BytesAsync(uint8_t ch, uint8_t address, uint16_t offset, uint8_t *data, uint16_t length)
{
  return i2cAsyncStart(ch, address, offset, data, length, false, true);
}

bool i2cProbeAsync(uint8_t ch, uint8_t address)
{
  return i2cAsyncStart(ch, address, 0U, NULL, 0U, true, false);
}

i2c_async_result_t i2cAsyncPoll(uint8_t ch, uint32_t *error)
{
  if (ch >= I2C_MAX_CH) { if (error) *error = HAL_I2C_ERROR_INVALID_PARAM; return I2C_ASYNC_ERROR; }
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  i2c_async_t *t = &transfers[ch];
  if (t->result == I2C_ASYNC_BUSY && (uint32_t)(millis() - t->started_ms) >= I2C_ASYNC_TIMEOUT_MS) {
    I2C_HandleTypeDef *h = i2cGetHandle(ch);
    // IT 전송을 먼저 정지시킨 뒤 버퍼 소유권을 반환한다. GPIO bus-recovery 대기는 하지 않는다.
    __HAL_I2C_DISABLE_IT(h, I2C_IT_TXI | I2C_IT_RXI | I2C_IT_ADDRI | I2C_IT_NACKI |
                           I2C_IT_STOPI | I2C_IT_TCI | I2C_IT_ERRI);
    __HAL_I2C_DISABLE(h);
    h->Instance->CR2 = 0U;
    __HAL_I2C_CLEAR_FLAG(h, I2C_FLAG_AF | I2C_FLAG_STOPF | I2C_FLAG_BERR | I2C_FLAG_ARLO |
                          I2C_FLAG_OVR | I2C_FLAG_TIMEOUT | I2C_FLAG_PECERR);
    h->XferISR = NULL;
    h->XferCount = h->XferSize = 0U;
    h->State = HAL_I2C_STATE_READY;
    h->Mode = HAL_I2C_MODE_NONE;
    h->Lock = HAL_UNLOCKED;
    h->ErrorCode = HAL_I2C_ERROR_TIMEOUT;
    __HAL_I2C_ENABLE(h);
    t->error = HAL_I2C_ERROR_TIMEOUT;
    t->result = I2C_ASYNC_ERROR;
  }
  i2c_async_result_t result = t->result;
  if (error != NULL) *error = t->error;
  if (result != I2C_ASYNC_BUSY) t->result = I2C_ASYNC_IDLE;
  __set_PRIMASK(irq);
  return result;
}

static i2c_async_t *i2cAsyncFind(I2C_HandleTypeDef *h)
{
  for (uint8_t ch = 0; ch < I2C_MAX_CH; ch++)
    if (i2cGetHandle(ch) == h && transfers[ch].result == I2C_ASYNC_BUSY) return &transfers[ch];
  return NULL;
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *h)
{
  i2c_async_t *t = i2cAsyncFind(h);
  if (t != NULL && !t->probe && !t->read) t->result = I2C_ASYNC_DONE;
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h)
{
  i2c_async_t *t = i2cAsyncFind(h);
  if (t != NULL && t->read) t->result = I2C_ASYNC_DONE;
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *h)
{
  i2c_async_t *t = i2cAsyncFind(h);
  if (t != NULL && t->probe) t->result = I2C_ASYNC_DONE;
}

bool i2cAsyncOnError(I2C_HandleTypeDef *h)
{
  i2c_async_t *t = i2cAsyncFind(h);
  if (t == NULL) return false;
  t->error = HAL_I2C_GetError(h);
  t->result = t->error == HAL_I2C_ERROR_AF ? I2C_ASYNC_NACK : I2C_ASYNC_ERROR;
  return t->probe && t->result == I2C_ASYNC_NACK;  // 정상적인 EEPROM write-cycle 대기는 오류 로그가 아니다.
}
#endif
