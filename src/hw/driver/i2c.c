#include "i2c.h"
#include "log.h"





#ifdef _USE_HW_I2C
#include "cli.h"

#define I2C_TIMING_KERNEL_CLOCK_HZ 150000000U

#ifdef _USE_HW_RTOS
#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);
#else
#define lock()      
#define unLock()    
#endif

static uint32_t i2cGetTimming(uint32_t freq_khz);
static void delayUs(uint32_t us);
static int8_t i2cGetChannelFromHandle(I2C_HandleTypeDef *hi2c);
static void i2cLogTimingOnce(uint8_t ch, I2C_HandleTypeDef *hi2c);
#if CLI_USE(HW_I2C)
static void cliI2C(cli_args_t *args);
#endif



static uint32_t i2c_timeout[I2C_MAX_CH];
static uint32_t i2c_errcount[I2C_MAX_CH];
static uint32_t i2c_freq[I2C_MAX_CH];
static bool     i2c_timing_logged[I2C_MAX_CH];
static bool     i2c_ready_wait_active[I2C_MAX_CH];       // V251112R7: Ready 폴링 상태 추적
static uint32_t i2c_ready_wait_start_ms[I2C_MAX_CH];     // V251112R7: Ready 폴링 시작 시각
static uint8_t  i2c_ready_wait_addr[I2C_MAX_CH];         // V251112R7: Ready 폴링 대상 주소
static i2c_ready_wait_stats_t i2c_ready_wait_stats[I2C_MAX_CH];  // V251112R9: Ready wait 통계 누적

static bool is_init = false;
static bool is_begin[I2C_MAX_CH];
#ifdef _USE_HW_RTOS
static SemaphoreHandle_t mutex_lock;
#endif

I2C_HandleTypeDef hi2c3;


typedef struct
{
  I2C_TypeDef       *p_i2c;
  I2C_HandleTypeDef *p_hi2c;

  GPIO_TypeDef *scl_port;
  int           scl_pin;

  GPIO_TypeDef *sda_port;
  int           sda_pin;
} i2c_tbl_t;

static i2c_tbl_t i2c_tbl[I2C_MAX_CH] =
    {
        { I2C3, &hi2c3, GPIOA, GPIO_PIN_8,  GPIOA, GPIO_PIN_9},
    };


// V260909R1: async 드라이버와 IRQ가 검증된 같은 handle을 사용한다.
I2C_HandleTypeDef *i2cGetHandle(uint8_t ch)
{
  return ch < I2C_MAX_CH ? i2c_tbl[ch].p_hi2c : NULL;
}

void I2C3_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&hi2c3); }
void I2C3_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&hi2c3); }

static int8_t i2cGetChannelFromHandle(I2C_HandleTypeDef *hi2c)
{
  for (int ch = 0; ch < I2C_MAX_CH; ch++)
  {
    if (i2c_tbl[ch].p_hi2c == hi2c)
    {
      return ch;
    }
  }

  return -1;
}

static void i2cLogTimingOnce(uint8_t ch, I2C_HandleTypeDef *hi2c)
{
  if (ch >= I2C_MAX_CH)
  {
    return;
  }

  if (i2c_timing_logged[ch] != true)
  {
    i2c_timing_logged[ch] = true;
    logPrintf("[I2C] ch%d TIMING=0x%08lX freq=%lukHz\n",
              ch + 1,
              (unsigned long)hi2c->Init.Timing,
              (unsigned long)i2c_freq[ch]);
  }
}





bool i2cInit(void)
{
  uint32_t i;

#ifdef _USE_HW_RTOS
  mutex_lock = xSemaphoreCreateMutex();
#endif

  for (i=0; i<I2C_MAX_CH; i++)
  {
    i2c_timeout[i] = 10;
    i2c_errcount[i] = 0;
    is_begin[i] = false;
    i2c_timing_logged[i] = false;
    i2c_ready_wait_active[i] = false;                    // V251112R7: Ready 폴링 로그 초기화
    i2c_ready_wait_start_ms[i] = 0;                      // V251112R7: Ready 폴링 타이머 초기화
    i2c_ready_wait_addr[i] = 0;                          // V251112R7: Ready 폴링 주소 초기화
    i2c_ready_wait_stats[i].wait_count = 0;              // V251112R9: Ready wait 누적 카운터 초기화
    i2c_ready_wait_stats[i].wait_last_ms = 0;            // V251112R9: Ready wait 마지막 지연 초기화
    i2c_ready_wait_stats[i].wait_max_ms = 0;             // V251112R9: Ready wait 최댓값 초기화
    i2c_ready_wait_stats[i].wait_last_addr = 0;          // V251112R9: Ready wait 마지막 주소 초기화
  }

#if CLI_USE(HW_I2C)
  cliAdd("i2c", cliI2C);
#endif

  is_init = true;
  return true;
}

bool i2cIsInit(void)
{
  return is_init;
}

bool i2cBegin(uint8_t ch, uint32_t freq_khz)
{
  if (ch >= I2C_MAX_CH || i2cAsyncOwned(ch)) return false;  // V260909R1: 범위/단일 bus 소유권 검사

  bool ret = false;

  I2C_HandleTypeDef *p_handle = i2c_tbl[ch].p_hi2c;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }


  switch(ch)
  {
    case _DEF_I2C1:
    case _DEF_I2C2:
      i2c_freq[ch] = freq_khz;
      i2c_timing_logged[ch] = false;

      p_handle->Instance             = i2c_tbl[ch].p_i2c;
      p_handle->Init.Timing          = i2cGetTimming(freq_khz);
      p_handle->Init.OwnAddress1     = 0x00;
      p_handle->Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
      p_handle->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
      p_handle->Init.OwnAddress2     = 0x00;
      p_handle->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
      p_handle->Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

      i2cReset(ch);

      HAL_I2C_DeInit(p_handle);
      if(HAL_I2C_Init(p_handle) != HAL_OK)
      {
        HAL_I2C_DeInit(p_handle);
        is_begin[ch] = false;
        return false;
      }
      // MSP가 선택한 실제 커널 클록을 확인한 뒤 timing 표를 사용한다.
      uint32_t kernel_clock_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C23);
      if (kernel_clock_hz != I2C_TIMING_KERNEL_CLOCK_HZ)
      {
        logPrintf("[I2C] unsupported kernel clock %luHz\n", (unsigned long)kernel_clock_hz);
        HAL_I2C_DeInit(p_handle);
        is_begin[ch] = false;
        return false;
      }
      uint32_t fast_mode_plus = freq_khz >= 1000 ? I2C_FASTMODEPLUS_ENABLE : I2C_FASTMODEPLUS_DISABLE;
      // 표의 필터/출력 구동 조건을 적용하지 못하면 전송을 허용하지 않는다.
      if (HAL_I2CEx_ConfigFastModePlus(p_handle, fast_mode_plus) != HAL_OK
          || HAL_I2CEx_ConfigAnalogFilter(p_handle, I2C_ANALOGFILTER_ENABLE) != HAL_OK
          || HAL_I2CEx_ConfigDigitalFilter(p_handle, 0) != HAL_OK)
      {
        HAL_I2C_DeInit(p_handle);
        is_begin[ch] = false;
        return false;
      }
      if (freq_khz >= 1000)
      {
        logPrintf("[I2C] ch%d FastModePlus TIMING=0x%08lX freq=%lukHz\n",
                  ch + 1,
                  (unsigned long)p_handle->Init.Timing,
                  (unsigned long)freq_khz);
      }
      i2c_errcount[ch] = 0;

      ret = true;
      is_begin[ch] = true;
      break;
  }

  return ret;
}

// ST H7S BSP의 TIMINGR 식, DS14359 Table 124의 analog filter 50..165 ns 기준.
// 커널 150 MHz (+/-1% 설계 여유), analog filter ON, DNF=0.
// Sm/Fm은 규격 최대 rise/fall (1000/300, 300/300 ns)를 허용한다.
// Fm+는 ZD24C128A의 rise/fall <=100 ns, tSU.DAT>=100 ns, tHIGH>=400 ns를 반영한다.
// 실제 보드 전이시간 측정값이 아니며, 모든 모드에서 가장 빠른 코너도 요청 속도 이하이다.
uint32_t i2cGetTimming(uint32_t freq_khz)
{
  uint32_t ret;

  switch(freq_khz)
  {
    case 100:
      ret = 0xB0F43B40;
      break;

    case 400:
      ret = 0x40C91A2C;
      break;

    case 1000:
      ret = 0x10F61B26;
      break;

    default:
      ret = 0x40C91A2C;
      break;
  };

  return ret;
}

bool i2cIsBegin(uint8_t ch)
{
  if (ch >= I2C_MAX_CH) return false;  // V260909R1: 범위/단일 bus 소유권 검사

  return is_begin[ch];
}

void i2cReset(uint8_t ch)
{
  if (ch >= I2C_MAX_CH || i2cAsyncOwned(ch)) return;  // V260909R1: 범위/단일 bus 소유권 검사

  GPIO_InitTypeDef  GPIO_InitStruct;
  i2c_tbl_t *p_pin = &i2c_tbl[ch];

  lock();
  GPIO_InitStruct.Pin       = p_pin->scl_pin;
  GPIO_InitStruct.Mode      = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(p_pin->scl_port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin       = p_pin->sda_pin;
  GPIO_InitStruct.Mode      = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  HAL_GPIO_Init(p_pin->sda_port, &GPIO_InitStruct);


  HAL_GPIO_WritePin(p_pin->scl_port, p_pin->scl_pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(p_pin->sda_port, p_pin->sda_pin, GPIO_PIN_SET);
  delayUs(5);

  for (int i = 0; i < 9; i++)
  {

    HAL_GPIO_WritePin(p_pin->scl_port, p_pin->scl_pin, GPIO_PIN_RESET);
    delayUs(5);
    HAL_GPIO_WritePin(p_pin->scl_port, p_pin->scl_pin, GPIO_PIN_SET);
    delayUs(5);
  }

  HAL_GPIO_WritePin(p_pin->scl_port, p_pin->scl_pin, GPIO_PIN_RESET);
  delayUs(5);
  HAL_GPIO_WritePin(p_pin->sda_port, p_pin->sda_pin, GPIO_PIN_RESET);
  delayUs(5);

  HAL_GPIO_WritePin(p_pin->scl_port, p_pin->scl_pin, GPIO_PIN_SET);
  delayUs(5);
  HAL_GPIO_WritePin(p_pin->sda_port, p_pin->sda_pin, GPIO_PIN_SET);
  unLock();
}

bool i2cIsDeviceReady(uint8_t ch, uint8_t dev_addr)
{
  if (ch >= I2C_MAX_CH || i2cAsyncOwned(ch)) return false;  // V260909R1: 범위/단일 bus 소유권 검사

  bool ret = false;
  I2C_HandleTypeDef *p_handle = i2c_tbl[ch].p_hi2c;
  bool was_waiting;

  lock();
  if (HAL_I2C_IsDeviceReady(p_handle, dev_addr << 1, 10, 10) == HAL_OK)
  {
    // V260909R1: 호출자의 interrupt mask를 변경하지 않는다.
    ret = true;
  }
  unLock();

  was_waiting = i2c_ready_wait_active[ch];               // V251112R7: Ready 폴링 상태 추적
  if (ret != true)
  {
    if (i2c_ready_wait_active[ch] != true)
    {
      i2c_ready_wait_active[ch] = true;
      i2c_ready_wait_start_ms[ch] = millis();
      i2c_ready_wait_addr[ch] = dev_addr;
#if LOG_LEVEL_VERBOSE || DEBUG_LOG_EEPROM
      logPrintf("[I2C] ch%d ready wait begin addr=0x%02X\n",
                ch + 1,
                dev_addr);
#endif
    }
  }
  else if (was_waiting == true)
  {
    uint32_t elapsed = millis() - i2c_ready_wait_start_ms[ch];
    if (elapsed > i2c_ready_wait_stats[ch].wait_max_ms)
    {
      i2c_ready_wait_stats[ch].wait_max_ms = elapsed;
    }
    i2c_ready_wait_stats[ch].wait_count++;
    i2c_ready_wait_stats[ch].wait_last_ms = elapsed;
    i2c_ready_wait_stats[ch].wait_last_addr = i2c_ready_wait_addr[ch];
#if LOG_LEVEL_VERBOSE || DEBUG_LOG_EEPROM
    logPrintf("[I2C] ch%d ready wait done addr=0x%02X elapsed=%lu ms\n",
              ch + 1,
              i2c_ready_wait_addr[ch],
              (unsigned long)elapsed);
#else
    // V251112R9: 기본 빌드는 UART 로그 대신 CLI 통계만 사용
#endif
    i2c_ready_wait_active[ch] = false;
  }

  return ret;
}

bool i2cRecovery(uint8_t ch)
{
  if (ch >= I2C_MAX_CH || i2cAsyncOwned(ch)) return false;  // V260909R1: 범위/단일 bus 소유권 검사

  bool ret;

  i2cReset(ch);

  ret = i2cBegin(ch, i2c_freq[ch]);

  return ret;
}

bool i2cReadByte (uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t timeout)
{
  return i2cReadBytes(ch, dev_addr, reg_addr, p_data, 1, timeout);
}

bool i2cReadBytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  if (ch >= I2C_MAX_CH || i2cAsyncOwned(ch)) return false;  // V260909R1: 범위/단일 bus 소유권 검사

  bool ret;
  HAL_StatusTypeDef i2c_ret;
  I2C_HandleTypeDef *p_handle = i2c_tbl[ch].p_hi2c;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  i2cLogTimingOnce(ch, p_handle);                        // V251112R7: Read 경로에서도 I2C 타이밍 로그 출력

  lock();
  i2c_ret = HAL_I2C_Mem_Read(p_handle, (uint16_t)(dev_addr << 1), reg_addr, I2C_MEMADD_SIZE_8BIT, p_data, length, timeout);
  unLock();

  if( i2c_ret == HAL_OK )
  {
    ret = true;
  }
  else
  {
    ret = false;
  }

  return ret;
}

bool i2cReadA16Bytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  if (ch >= I2C_MAX_CH || i2cAsyncOwned(ch)) return false;  // V260909R1: 범위/단일 bus 소유권 검사

  bool ret;
  HAL_StatusTypeDef i2c_ret;
  I2C_HandleTypeDef *p_handle = i2c_tbl[ch].p_hi2c;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  i2cLogTimingOnce(ch, p_handle);                        // V251112R7: 16비트 Read 경로 타이밍 계측

  lock();
  i2c_ret = HAL_I2C_Mem_Read(p_handle, (uint16_t)(dev_addr << 1), reg_addr, I2C_MEMADD_SIZE_16BIT, p_data, length, timeout);
  unLock();

  if( i2c_ret == HAL_OK )
  {
    ret = true;
  }
  else
  {
    ret = false;
  }

  return ret;
}

bool i2cReadData(uint8_t ch, uint16_t dev_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  if (ch >= I2C_MAX_CH || i2cAsyncOwned(ch)) return false;  // V260909R1: 범위/단일 bus 소유권 검사

  bool ret;
  HAL_StatusTypeDef i2c_ret;
  I2C_HandleTypeDef *p_handle = i2c_tbl[ch].p_hi2c;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  lock();
  i2c_ret = HAL_I2C_Master_Receive(p_handle, (uint16_t)(dev_addr << 1), p_data, length, timeout);
  unLock();

  if( i2c_ret == HAL_OK )
  {
    ret = true;
  }
  else
  {
    ret = false;
  }

  return ret;
}

bool i2cWriteByte (uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t data, uint32_t timeout)
{
  return i2cWriteBytes(ch, dev_addr, reg_addr, &data, 1, timeout);
}

bool i2cWriteBytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  if (ch >= I2C_MAX_CH || i2cAsyncOwned(ch)) return false;  // V260909R1: 범위/단일 bus 소유권 검사

  bool ret;
  HAL_StatusTypeDef i2c_ret;
  I2C_HandleTypeDef *p_handle = i2c_tbl[ch].p_hi2c;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  lock();
  i2c_ret = HAL_I2C_Mem_Write(p_handle, (uint16_t)(dev_addr << 1), reg_addr, I2C_MEMADD_SIZE_8BIT, p_data, length, timeout);
  unLock();

  if(i2c_ret == HAL_OK)
  {
    ret = true;
  }
  else
  {
    ret = false;
  }

  return ret;
}

bool i2cWriteA16Bytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  if (ch >= I2C_MAX_CH || i2cAsyncOwned(ch)) return false;  // V260909R1: 범위/단일 bus 소유권 검사

  bool ret;
  HAL_StatusTypeDef i2c_ret;
  I2C_HandleTypeDef *p_handle = i2c_tbl[ch].p_hi2c;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  i2cLogTimingOnce(ch, p_handle);

  lock();
  i2c_ret = HAL_I2C_Mem_Write(p_handle, (uint16_t)(dev_addr << 1), reg_addr, I2C_MEMADD_SIZE_16BIT, p_data, length, timeout);
  unLock();

  if(i2c_ret == HAL_OK)
  {
    ret = true;
  }
  else
  {
    ret = false;
  }

  return ret;
}

bool i2cWriteData(uint8_t ch, uint16_t dev_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  if (ch >= I2C_MAX_CH || i2cAsyncOwned(ch)) return false;  // V260909R1: 범위/단일 bus 소유권 검사

  bool ret;
  HAL_StatusTypeDef i2c_ret;
  I2C_HandleTypeDef *p_handle = i2c_tbl[ch].p_hi2c;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  i2cLogTimingOnce(ch, p_handle);

  lock();
  i2c_ret = HAL_I2C_Master_Transmit(p_handle, (uint16_t)(dev_addr << 1), p_data, length, timeout);
  unLock();
  
  if(i2c_ret == HAL_OK)
  {
    ret = true;
  }
  else
  {
    ret = false;
  }

  return ret;
}

void i2cSetTimeout(uint8_t ch, uint32_t timeout)
{
  if (ch >= I2C_MAX_CH) return;  // V260909R1: 범위/단일 bus 소유권 검사

  i2c_timeout[ch] = timeout;
}

uint32_t i2cGetTimeout(uint8_t ch)
{
  if (ch >= I2C_MAX_CH) return 0U;  // V260909R1: 범위/단일 bus 소유권 검사

  return i2c_timeout[ch];
}

void i2cClearErrCount(uint8_t ch)
{
  if (ch >= I2C_MAX_CH) return;  // V260909R1: 범위/단일 bus 소유권 검사

  i2c_errcount[ch] = 0;
}

uint32_t i2cGetErrCount(uint8_t ch)
{
  if (ch >= I2C_MAX_CH) return 0U;  // V260909R1: 범위/단일 bus 소유권 검사

  return i2c_errcount[ch];
}

void i2cGetReadyWaitStats(uint8_t ch, i2c_ready_wait_stats_t *p_stats)
{
  if (p_stats == NULL)
  {
    return;
  }

  p_stats->wait_count = 0;
  p_stats->wait_last_ms = 0;
  p_stats->wait_max_ms = 0;
  p_stats->wait_last_addr = 0;

  if (ch >= I2C_MAX_CH)
  {
    return;
  }

  *p_stats = i2c_ready_wait_stats[ch];                                // V251112R9: Ready wait 통계 조회
}

void delayUs(uint32_t us)
{
  volatile uint32_t i;

  for (i=0; i<us*1000; i++)
  {

  }
}





void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
  if (i2cAsyncOnError(hi2c)) return;
  int8_t ch = i2cGetChannelFromHandle(hi2c);
  if (ch >= 0) i2c_errcount[ch]++;
  // V260909R1: ISR에서 UART/USB 로그를 보내지 않는다. 오류는 카운터/저장 상태로 조회한다.
}


void HAL_I2C_MspInit(I2C_HandleTypeDef* i2cHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  if(i2cHandle->Instance==I2C3)
  {
  /** Initializes the peripherals clock
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_I2C23;
    PeriphClkInit.I2c23ClockSelection = RCC_I2C23CLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**I2C3 GPIO Configuration
    PA8     ------> I2C3_SCL
    PA9     ------> I2C3_SDA
    */
    GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;  // V251112R5: FastMode Plus에서 스위칭 에지 확보
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C3;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);


    /* I2C3 clock enable */
    __HAL_RCC_I2C3_CLK_ENABLE();

    /* I2C3 interrupt Init */
    HAL_NVIC_SetPriority(I2C3_EV_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(I2C3_EV_IRQn);
    HAL_NVIC_SetPriority(I2C3_ER_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(I2C3_ER_IRQn);
  }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef* i2cHandle)
{

  if(i2cHandle->Instance==I2C3)
  {
    /* Peripheral clock disable */
    __HAL_RCC_I2C3_CLK_DISABLE();

    /**I2C3 GPIO Configuration
    PA8     ------> I2C3_SCL
    PA9     ------> I2C3_SDA
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_8);

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9);

    /* I2C3 interrupt Deinit */
    HAL_NVIC_DisableIRQ(I2C3_EV_IRQn);
    HAL_NVIC_DisableIRQ(I2C3_ER_IRQn);
  }
}


#if CLI_USE(HW_I2C)
void cliI2C(cli_args_t *args)
{
  bool ret = false;
  bool i2c_ret;

  uint8_t print_ch;
  uint8_t ch;
  uint16_t dev_addr;
  uint16_t reg_addr;
  uint16_t length;
  uint32_t pre_time;


if (args->argc == 2 && args->isStr(0, "scan") == true)
  {
    uint32_t dev_cnt = 0;
    print_ch = (uint16_t) args->getData(1);

    print_ch = constrain(print_ch, 1, I2C_MAX_CH);
    print_ch -= 1;

    for (int i=0x00; i<= 0x7F; i++)
    {
      if (i2cIsDeviceReady(print_ch, i) == true)
      {
        cliPrintf("I2C CH%d Addr 0x%02X : OK\n", print_ch+1, i);
        dev_cnt++;
      }
    }
    if (dev_cnt == 0)
    {
      cliPrintf("no found\n");
    }
    ret = true;  
  }

  if (args->argc == 2 && args->isStr(0, "begin") == true)
  {
    print_ch = (uint16_t) args->getData(1);

    print_ch = constrain(print_ch, 1, I2C_MAX_CH);
    print_ch -= 1;

    i2c_ret = i2cBegin(print_ch, 400);
    if (i2c_ret == true)
    {
      cliPrintf("I2C CH%d Begin OK\n", print_ch + 1);
    }
    else
    {
      cliPrintf("I2C CH%d Begin Fail\n", print_ch + 1);
    }
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "read") == true)
  {
    print_ch = (uint16_t) args->getData(1);
    print_ch = constrain(print_ch, 1, I2C_MAX_CH);

    dev_addr = (uint16_t) args->getData(2);
    reg_addr = (uint16_t) args->getData(3);
    length   = (uint16_t) args->getData(4);
    ch       = print_ch - 1;

    for (int i=0; i<length; i++)
    {
      uint8_t i2c_data;
      i2c_ret = i2cReadByte(ch, dev_addr, reg_addr+i, &i2c_data, 100);

      if (i2c_ret == true)
      {
        cliPrintf("%d I2C - 0x%02X : 0x%02X\n", print_ch, reg_addr+i, i2c_data);
      }
      else
      {
        cliPrintf("%d I2C - Fail \n", print_ch);
        break;
      }
    }
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "write") == true)
  {
    print_ch = (uint16_t) args->getData(1);
    print_ch = constrain(print_ch, 1, I2C_MAX_CH);

    dev_addr = (uint16_t) args->getData(2);
    reg_addr = (uint16_t) args->getData(3);
    length   = (uint16_t) args->getData(4);
    ch       = print_ch - 1;

    pre_time = millis();
    i2c_ret = i2cWriteByte(ch, dev_addr, reg_addr, (uint8_t)length, 100);

    if (i2c_ret == true)
    {
      cliPrintf("%d I2C - 0x%02X : 0x%02X, %d ms\n", print_ch, reg_addr, length, millis()-pre_time);
    }
    else
    {
      cliPrintf("%d I2C - Fail \n", print_ch);
    }
    ret = true;
  }


  if (ret == false)
  {
    cliPrintf( "i2c begin ch[1~%d]\n", I2C_MAX_CH);
    cliPrintf( "i2c scan  ch[1~%d]\n", I2C_MAX_CH);
    cliPrintf( "i2c read  ch dev_addr reg_addr length\n");
    cliPrintf( "i2c write ch dev_addr reg_addr data\n");
  }
}

#endif

#endif
