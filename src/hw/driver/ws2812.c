#include "ws2812.h"



#ifdef _USE_HW_WS2812
#include "cli.h"
#include "micros.h"

/* TIM15: 300 MHz / 3 = 100 MHz. WS2812B-2020 requires RESET > 280 us.
 * Two extra zero symbols cover CCR preload and the last DMA write. A leading
 * reset also resynchronizes the receiver after an aborted/failed transaction. */
#define BIT_PERIOD 130U
#define BIT_HIGH   70U
#define BIT_LOW    35U
#define WS2812_COUNTER_MHZ 100U
#define WS2812_RESET_US 320U
#define WS2812_PIPELINE_SLOTS 2U
#define BIT_ZERO ((WS2812_RESET_US * WS2812_COUNTER_MHZ + BIT_PERIOD - 1U) / BIT_PERIOD + WS2812_PIPELINE_SLOTS)
#define WS2812_BIT_BUF_LEN (BIT_ZERO + 24U * HW_WS2812_MAX_CH + BIT_ZERO)
#define WS2812_FRAME_US ((WS2812_BIT_BUF_LEN * BIT_PERIOD + WS2812_COUNTER_MHZ - 1U) / WS2812_COUNTER_MHZ)
#define WS2812_SERVICE_TIMEOUT_US (WS2812_FRAME_US + 2000U)
_Static_assert((BIT_ZERO - WS2812_PIPELINE_SLOTS) * BIT_PERIOD > 280U * WS2812_COUNTER_MHZ, "WS2812 reset too short");
_Static_assert(WS2812_BIT_BUF_LEN <= UINT16_MAX, "WS2812 DMA length exceeds HAL limit");
_Static_assert(WS2812_FRAME_US < 5000U, "WS2812 frame cannot support 5 ms pulses");

bool is_init = false;


typedef struct
{
  TIM_HandleTypeDef *h_timer;  
  uint32_t channel;
  uint16_t led_cnt;
} ws2812_t;

__attribute__((section(".non_cache"), aligned(4)))
static uint8_t bit_buf_dma[WS2812_BIT_BUF_LEN];            // V251116R1: DMA 활성 버퍼
__attribute__((section(".non_cache"), aligned(4)))
static uint8_t bit_buf_cpu[WS2812_BIT_BUF_LEN];            // V251116R1: CPU 작업 버퍼
static uint8_t *ws2812_dma_buf = bit_buf_dma;              // V251116R1: DMA와 CPU 포인터 분리
static uint8_t *ws2812_work_buf = bit_buf_cpu;
typedef enum { WS2812_IDLE, WS2812_ACTIVE, WS2812_QUIESCING } ws2812_phase_t;
static ws2812_phase_t ws2812_phase;
static bool ws2812_refresh_pending;
static uint32_t ws2812_requested_generation;
static uint32_t ws2812_inflight_generation;
static uint32_t ws2812_completed_generation;
static uint32_t ws2812_started_us;



ws2812_t ws2812;
static TIM_HandleTypeDef htim15;
static DMA_HandleTypeDef handle_GPDMA1_Channel4;


#if CLI_USE(HW_WS2812)
static void cliCmd(cli_args_t *args);
#endif
static bool ws2812InitHw(void);
static bool ws2812StartTransfer(void);
static void ws2812Service(void);
static void ws2812PinIdle(void);





bool ws2812Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};


  memset(bit_buf_dma, 0, sizeof(bit_buf_dma));
  memset(bit_buf_cpu, 0, sizeof(bit_buf_cpu));
  ws2812_dma_buf        = bit_buf_dma;
  ws2812_work_buf       = bit_buf_cpu;
  ws2812_phase = WS2812_IDLE;
  ws2812_refresh_pending = false;
  ws2812_requested_generation = 0;
  ws2812_inflight_generation = 0;
  ws2812_completed_generation = 0;
  ws2812_started_us = 0;
  
  ws2812.h_timer = &htim15;
  ws2812.channel = TIM_CHANNEL_1;

  // Timer 
  //
  __HAL_RCC_GPDMA1_CLK_ENABLE();
  __HAL_RCC_TIM15_CLK_ENABLE();

  htim15.Instance               = TIM15;
  htim15.Init.Prescaler         = 2; // BSP supplies TIM15 with 300 MHz; counter is 100 MHz.
  htim15.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim15.Init.Period            = BIT_PERIOD-1;
  htim15.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim15.Init.RepetitionCounter = 0;
  htim15.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim15) != HAL_OK)
  {
    return false;                                                // V251124R6: WS2812 타이머 초기화 실패 시 오류 전파
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim15, &sClockSourceConfig) != HAL_OK)
  {
    return false;                                                // V251124R6: WS2812 클럭 소스 설정 실패 시 오류 전파
  }
  if (HAL_TIM_PWM_Init(&htim15) != HAL_OK)
  {
    return false;                                                // V251124R6: PWM 초기화 실패 시 상위로 실패 전달
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim15, &sMasterConfig) != HAL_OK)
  {
    return false;                                                // V251124R6: PWM 마스터 설정 실패 시 부팅 중단
  }
  sConfigOC.OCMode       = TIM_OCMODE_PWM1;
  sConfigOC.Pulse        = 0;
  sConfigOC.OCPolarity   = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode   = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState  = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim15, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    return false;                                                // V251124R6: PWM 채널 설정 실패 시 부팅 진행 차단
  }
  sBreakDeadTimeConfig.OffStateRunMode  = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel        = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime         = 0;
  sBreakDeadTimeConfig.BreakState       = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity    = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter      = 0;
  sBreakDeadTimeConfig.AutomaticOutput  = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim15, &sBreakDeadTimeConfig) != HAL_OK)
  {
    return false;                                                // V251124R6: 브레이크/데드타임 설정 실패 시 상위로 실패 전파
  }

  /* One symbol per UPDATE, independent of the previous symbol's duty cycle. */
  __HAL_TIM_SELECT_CCDMAREQUEST(&htim15, TIM_CCDMAREQUEST_UPDATE);

  if (ws2812InitHw() != true)
  {
    return false;                                                 // V251124R6: WS2812 하드웨어 초기화 실패 시 상위로 전달
  }


  ws2812.led_cnt = WS2812_MAX_CH;
  is_init = false;

  for (int i=0; i<WS2812_MAX_CH; i++)
  {
    ws2812SetColor(i, WS2812_COLOR_OFF);
  }
  if (ws2812Refresh() != true)
  {
    return false;                                                 // V251124R6: 초기 WS2812 전송 실패 시 부팅 진행 차단
  }

  is_init = true;

#if CLI_USE(HW_WS2812)
  cliAdd("ws2812", cliCmd);
#endif
  return true;
}

bool ws2812InitHw(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};


  __HAL_RCC_GPIOC_CLK_ENABLE();
  /**TIM15 GPIO Configuration
  PC12     ------> TIM15_CH1
  */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF2_TIM15;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  ws2812PinIdle();


  /* TIM15 DMA Init */
  /* GPDMA1_REQUEST_TIM15_CH1 Init */
  handle_GPDMA1_Channel4.Instance                   = GPDMA1_Channel4;
  handle_GPDMA1_Channel4.Init.Request               = GPDMA1_REQUEST_TIM15_CH1;
  handle_GPDMA1_Channel4.Init.BlkHWRequest          = DMA_BREQ_SINGLE_BURST;
  handle_GPDMA1_Channel4.Init.Direction             = DMA_MEMORY_TO_PERIPH;  // V251124R6: WS2812 PWM 전송 방향을 메모리→타이머로 교정
  handle_GPDMA1_Channel4.Init.SrcInc                = DMA_SINC_INCREMENTED;
  handle_GPDMA1_Channel4.Init.DestInc               = DMA_DINC_FIXED;
  handle_GPDMA1_Channel4.Init.SrcDataWidth          = DMA_SRC_DATAWIDTH_BYTE;
  handle_GPDMA1_Channel4.Init.DestDataWidth         = DMA_DEST_DATAWIDTH_BYTE;
  handle_GPDMA1_Channel4.Init.Priority              = DMA_LOW_PRIORITY_LOW_WEIGHT;
  handle_GPDMA1_Channel4.Init.SrcBurstLength        = 1;
  handle_GPDMA1_Channel4.Init.DestBurstLength       = 1;
  handle_GPDMA1_Channel4.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
  handle_GPDMA1_Channel4.Init.TransferEventMode     = DMA_TCEM_BLOCK_TRANSFER;
  handle_GPDMA1_Channel4.Init.Mode                  = DMA_NORMAL;
  if (HAL_DMA_Init(&handle_GPDMA1_Channel4) != HAL_OK)
  {
    return false;                                                // V251124R6: DMA 초기화 실패 시 즉시 실패 반환
  }

  __HAL_LINKDMA(&htim15, hdma[TIM_DMA_ID_CC1], handle_GPDMA1_Channel4);

  if (HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel4, DMA_CHANNEL_NPRIV) != HAL_OK)
  {
    return false;                                                // V251124R6: 채널 속성 설정 실패 시 상위에서 복구 처리
  }

  HAL_NVIC_SetPriority(GPDMA1_Channel4_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(GPDMA1_Channel4_IRQn);

  return true;
}

/* The pin is actively LOW while stopped, including error recovery. Changing
 * only MOE/CC1E can leave an AF pin undriven; ODR is preloaded before MODER. */
static void ws2812PinMode(uint32_t mode)
{
  /* MODER is shared with other GPIOC pins; keep the read/modify/write atomic. */
  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  MODIFY_REG(GPIOC->MODER, 3UL << 24, mode << 24);
  __set_PRIMASK(irq_state);
}

static void ws2812PinIdle(void)
{
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_RESET);
  ws2812PinMode(1U);
}

static void ws2812StopOutput(void)
{
  ws2812PinIdle(); // Take over LOW before disabling the peripheral output.
  (void)HAL_TIM_PWM_Stop_DMA(ws2812.h_timer, ws2812.channel);
}

/* Recover a delayed/missing IRQ through the same HAL handler, never by
 * claiming completion or reusing a still-owned DMA buffer. */
static void ws2812PollDma(void)
{
  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel4);
  __set_PRIMASK(irq_state);
}

static bool ws2812StartTransfer(void)
{
  /* Flush the zero compare into the preload and start at a complete period. */
  __HAL_TIM_DISABLE_DMA(ws2812.h_timer, TIM_DMA_CC1);
  __HAL_TIM_SET_COMPARE(ws2812.h_timer, ws2812.channel, 0U);
  ws2812.h_timer->Instance->EGR = TIM_EGR_UG;
  __HAL_TIM_SET_COUNTER(ws2812.h_timer, 0U);
  __HAL_TIM_CLEAR_FLAG(ws2812.h_timer, TIM_FLAG_UPDATE | TIM_FLAG_CC1);

  /* Drive zero before connecting AF, then start DMA. Connecting after Start
   * would lose data if an IRQ preempted the CPU longer than the leading reset. */
  TIM_CCxChannelCmd(ws2812.h_timer->Instance, ws2812.channel, TIM_CCx_ENABLE);
  __HAL_TIM_MOE_ENABLE(ws2812.h_timer);
  ws2812PinMode(2U);
  __DMB(); // Publish the non-cacheable work frame before handing it to DMA.
  HAL_StatusTypeDef status = HAL_TIM_PWM_Start_DMA(ws2812.h_timer, ws2812.channel,
                                                  (const uint32_t *)ws2812_work_buf, WS2812_BIT_BUF_LEN);
  if (status != HAL_OK)
  {
    ws2812StopOutput();
    ws2812_phase = WS2812_QUIESCING;
    return false;
  }

  uint8_t *previous_dma = ws2812_dma_buf;
  ws2812_dma_buf = ws2812_work_buf;
  ws2812_work_buf = previous_dma;
  memcpy(ws2812_work_buf, ws2812_dma_buf, WS2812_BIT_BUF_LEN);
  ws2812_inflight_generation = ws2812_requested_generation;
  ws2812_started_us = micros();
  ws2812_phase = WS2812_ACTIVE;
  return true;
}

static void ws2812Service(void)
{
  if (ws2812_phase == WS2812_ACTIVE)
  {
    if (HAL_DMA_GetState(&handle_GPDMA1_Channel4) == HAL_DMA_STATE_BUSY &&
        (uint32_t)(micros() - ws2812_started_us) >= WS2812_SERVICE_TIMEOUT_US)
    {
      ws2812PollDma();
      if (HAL_DMA_GetState(&handle_GPDMA1_Channel4) != HAL_DMA_STATE_READY)
      {
        ws2812StopOutput();
        ws2812_refresh_pending = true;
        ws2812_phase = WS2812_QUIESCING;
        return;
      }
    }
    if (HAL_DMA_GetState(&handle_GPDMA1_Channel4) != HAL_DMA_STATE_READY)
    {
      return;
    }

    bool success = HAL_DMA_GetError(&handle_GPDMA1_Channel4) == HAL_DMA_ERROR_NONE;
    ws2812StopOutput();
    ws2812_phase = WS2812_IDLE;
    if (success)
    {
      /* The DMA suffix has already emitted RESET plus preload/drain slack. */
      ws2812_completed_generation = ws2812_inflight_generation;
    }
    else
    {
      ws2812_refresh_pending = true;
    }
  }

  if (ws2812_phase == WS2812_QUIESCING)
  {
    ws2812PollDma();
    if (HAL_DMA_GetState(&handle_GPDMA1_Channel4) != HAL_DMA_STATE_READY)
    {
      return; // Abort is asynchronous. Keep both buffer ownership and LOW.
    }
    ws2812_phase = WS2812_IDLE;
  }

  if (ws2812_refresh_pending && ws2812StartTransfer())
  {
    ws2812_refresh_pending = false;
  }
}

void ws2812Task(void)
{
  ws2812Service();
}

bool ws2812Refresh(void)
{
  ++ws2812_requested_generation;
  if (ws2812_requested_generation == 0U) ++ws2812_requested_generation;
  ws2812_refresh_pending = true;
  ws2812Service();
  return true; // Accepted, not necessarily transmitted. Failed starts remain queued.
}

uint32_t ws2812GetRequestedGeneration(void)
{
  return ws2812_requested_generation;
}

bool ws2812IsFrameComplete(uint32_t generation)
{
  return generation != 0U && ws2812_completed_generation != 0U &&
         (int32_t)(ws2812_completed_generation - generation) >= 0;
}

uint32_t ws2812TimeUs(void)
{
  return micros();
}

void ws2812SetColor(uint32_t ch, uint32_t color)
{
  uint8_t r_bit[8];
  uint8_t g_bit[8];
  uint8_t b_bit[8];
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint32_t offset;

  if (ch >= WS2812_MAX_CH)
    return;

  red   = (color >> 16) & 0xFF;
  green = (color >> 8) & 0xFF;
  blue  = (color >> 0) & 0xFF;


  for (int i=0; i<8; i++)
  {
    if (red & (1<<7))
    {
      r_bit[i] = BIT_HIGH;
    }
    else
    {
      r_bit[i] = BIT_LOW;
    }
    red <<= 1;

    if (green & (1<<7))
    {
      g_bit[i] = BIT_HIGH;
    }
    else
    {
      g_bit[i] = BIT_LOW;
    }
    green <<= 1;

    if (blue & (1<<7))
    {
      b_bit[i] = BIT_HIGH;
    }
    else
    {
      b_bit[i] = BIT_LOW;
    }
    blue <<= 1;
  }

  offset = BIT_ZERO;

  // V251116R1: CPU 전용 버퍼에 RGB 비트를 준비해 DMA 버퍼를 보호
  memcpy(&ws2812_work_buf[offset + ch*24 + 8*0], g_bit, 8*1);
  memcpy(&ws2812_work_buf[offset + ch*24 + 8*1], r_bit, 8*1);
  memcpy(&ws2812_work_buf[offset + ch*24 + 8*2], b_bit, 8*1);
}

void GPDMA1_Channel4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel4);
}

#if CLI_USE(HW_WS2812)
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("ws2812 led cnt : %d\n", WS2812_MAX_CH);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "test"))
  {
    uint32_t color[6] = {WS2812_COLOR_RED,
                         WS2812_COLOR_OFF,
                         WS2812_COLOR_GREEN,
                         WS2812_COLOR_OFF,
                         WS2812_COLOR_BLUE,
                         WS2812_COLOR_OFF};

    uint8_t color_idx = 0;
    uint32_t pre_time;


    pre_time = millis();
    while(cliKeepLoop())
    {
      if (millis()-pre_time >= 500)
      {
        pre_time = millis();
        
        for (int i=0; i<WS2812_MAX_CH; i++)
        {      
          ws2812SetColor(i, color[color_idx]);
        }
        ws2812Refresh();
        color_idx = (color_idx + 1) % 6;
      }
      
      cliLoopIdle();
    }

    for (int i=0; i<WS2812_MAX_CH; i++)
    {
      ws2812SetColor(i, WS2812_COLOR_OFF);
    }
    ws2812Refresh();

    ret = true;
  }


  if (args->argc == 5 && args->isStr(0, "color"))
  {
    uint8_t  ch;
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    ch    = (uint8_t)args->getData(1);
    red   = (uint8_t)args->getData(2);
    green = (uint8_t)args->getData(3);
    blue  = (uint8_t)args->getData(4);

    ws2812SetColor(ch, WS2812_COLOR(red, green, blue));
    ws2812Refresh();

    while(cliKeepLoop())
    {
      cliLoopIdle();
    }
    ws2812SetColor(0, 0);
    ws2812Refresh();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("ws2812 info\n");
    cliPrintf("ws2812 test\n");
    cliPrintf("ws2812 color ch r g b\n");
  }
}
#endif

#endif
