#include "keys.h"
#include "log.h"                                                               // V251124R4: 키 초기화 실패 로깅


#ifdef _USE_HW_KEYS
#include "button.h"



static bool keysInitTimer(void);
static bool keysInitDma(void);
static bool keysInitGpio(void);



const static uint8_t row_wr_buf[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20};
_Static_assert(MATRIX_ROWS > 0 && MATRIX_ROWS <= sizeof(row_wr_buf), "matrix row drive table overflow");
_Static_assert(MATRIX_COLS > 0 && MATRIX_COLS <= 16, "matrix column sample width overflow");

__attribute__((section(".non_cache")))
static volatile uint16_t col_rd_buf[MATRIX_ROWS] = {0x00,};  // V250924R5: DMA 직접 참조 재검토로 volatile 지정해 최신 값 보장



static TIM_HandleTypeDef htim16;
// V260909R1: DMA가 재읽는 linked-list descriptor도 payload와 같은 non-cache 영역에 둔다.
__attribute__((section(".non_cache"), aligned(32)))
static DMA_NodeTypeDef   Node_GPDMA1_Channel1;
static DMA_QListTypeDef  List_GPDMA1_Channel1;
static DMA_HandleTypeDef handle_GPDMA1_Channel1;
__attribute__((section(".non_cache"), aligned(32)))
static DMA_NodeTypeDef   Node_GPDMA1_Channel2;
static DMA_QListTypeDef  List_GPDMA1_Channel2;
static DMA_HandleTypeDef handle_GPDMA1_Channel2;



bool keysInit(void)
{
  if (keysInitGpio() != true)
  {
    logPrintf("[!] keysInitGpio 실패\n");                                      // V251124R4: GPIO 초기화 실패 감지
    return false;
  }

  if (keysInitDma() != true)
  {
    logPrintf("[!] keysInitDma 실패\n");                                       // V251124R4: DMA 초기화 실패 감지
    return false;
  }

  if (keysInitTimer() != true)
  {
    logPrintf("[!] keysInitTimer 실패\n");                                     // V251124R4: 타이머 초기화 실패 감지
    return false;
  }



  // V260909R1: OC start가 channel과 counter를 함께 시작한다. 실패를 정상 scan으로 취급하지 않는다.
  if (HAL_TIM_OC_Start(&htim16, TIM_CHANNEL_1) != HAL_OK) return false;


  // V251009R2: DMA 기반 자동 스캔이 상시 갱신되므로 CLI 키 매트릭스 뷰어를 제거함
  return true;
}

bool keysInitGpio(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};


  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // ROWS
  //
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Pin   = GPIO_PIN_0 |
                        GPIO_PIN_1 |
                        GPIO_PIN_2 |
                        GPIO_PIN_3 |
                        GPIO_PIN_4 |
                        GPIO_PIN_5 |
                        GPIO_PIN_6 |
                        GPIO_PIN_7;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct); 


  // COLS
  //
  GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Pin   = GPIO_PIN_0 |
                        GPIO_PIN_1 |
                        GPIO_PIN_2 |
                        GPIO_PIN_3 |
                        GPIO_PIN_4 |
                        GPIO_PIN_5 |
                        GPIO_PIN_6 |
                        GPIO_PIN_7 |
                        GPIO_PIN_8 |
                        GPIO_PIN_9 |
                        GPIO_PIN_10 |
                        GPIO_PIN_11 |
                        GPIO_PIN_12 |
                        GPIO_PIN_13 |
                        GPIO_PIN_14 |
                        GPIO_PIN_15;
  GPIO_InitStruct.Pull  = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);  
  return true;
}

bool keysInitTimer(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};


  /* TIM16 clock enable */
  __HAL_RCC_TIM16_CLK_ENABLE();

  htim16.Instance               = TIM16;
  htim16.Init.Prescaler         = 29;
  htim16.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim16.Init.Period            = 9;
  htim16.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim16.Init.RepetitionCounter = 0;
  htim16.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  if (HAL_TIM_Base_Init(&htim16) != HAL_OK)
  {
    return false;
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim16, &sClockSourceConfig) != HAL_OK)
  {
    return false;
  }
  if (HAL_TIM_OC_Init(&htim16) != HAL_OK)
  {
    return false;
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim16, &sMasterConfig) != HAL_OK)
  {
    return false;
  }
  sConfigOC.OCMode       = TIM_OCMODE_TIMING;
  sConfigOC.Pulse        = 1;
  sConfigOC.OCPolarity   = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode   = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState  = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_OC_ConfigChannel(&htim16, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    return false;
  }
  __HAL_TIM_ENABLE_OCxPRELOAD(&htim16, TIM_CHANNEL_1);


  __HAL_TIM_ENABLE_DMA(&htim16, TIM_DMA_CC1);
  __HAL_TIM_ENABLE_DMA(&htim16, TIM_DMA_UPDATE);

  return true;
}

bool keysInitDma(void)
{
  DMA_NodeConfTypeDef NodeConfig= {0};


  __HAL_RCC_GPDMA1_CLK_ENABLE();


  // CC1 Event
  //
  /* GPDMA1_REQUEST_TIM16_CH1 Init */
  NodeConfig.NodeType                         = DMA_GPDMA_LINEAR_NODE;
  NodeConfig.Init.Request                     = GPDMA1_REQUEST_TIM16_CH1;
  NodeConfig.Init.BlkHWRequest                = DMA_BREQ_SINGLE_BURST;
  NodeConfig.Init.Direction                   = DMA_MEMORY_TO_PERIPH;
  NodeConfig.Init.SrcInc                      = DMA_SINC_INCREMENTED;
  NodeConfig.Init.DestInc                     = DMA_DINC_FIXED;
  NodeConfig.Init.SrcDataWidth                = DMA_SRC_DATAWIDTH_BYTE;
  NodeConfig.Init.DestDataWidth               = DMA_DEST_DATAWIDTH_BYTE;
  NodeConfig.Init.SrcBurstLength              = 1;
  NodeConfig.Init.DestBurstLength             = 1;
  NodeConfig.Init.TransferAllocatedPort       = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
  NodeConfig.Init.TransferEventMode           = DMA_TCEM_BLOCK_TRANSFER;
  NodeConfig.Init.Mode                        = DMA_NORMAL;
  NodeConfig.TriggerConfig.TriggerPolarity    = DMA_TRIG_POLARITY_MASKED;
  NodeConfig.DataHandlingConfig.DataExchange  = DMA_EXCHANGE_NONE;
  NodeConfig.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
  NodeConfig.SrcAddress                       = (uint32_t)&row_wr_buf[0];
  NodeConfig.DstAddress                       = (uint32_t)&GPIOA->ODR;
  NodeConfig.DataSize                         = MATRIX_ROWS;
  if (HAL_DMAEx_List_BuildNode(&NodeConfig, &Node_GPDMA1_Channel1) != HAL_OK)
  {
    return false;
  }

  if (HAL_DMAEx_List_InsertNode(&List_GPDMA1_Channel1, NULL, &Node_GPDMA1_Channel1) != HAL_OK)
  {
    return false;
  }

  if (HAL_DMAEx_List_SetCircularMode(&List_GPDMA1_Channel1) != HAL_OK)
  {
    return false;
  }

  handle_GPDMA1_Channel1.Instance                         = GPDMA1_Channel1;
  handle_GPDMA1_Channel1.InitLinkedList.Priority          = DMA_LOW_PRIORITY_MID_WEIGHT;
  handle_GPDMA1_Channel1.InitLinkedList.LinkStepMode      = DMA_LSM_FULL_EXECUTION;
  handle_GPDMA1_Channel1.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
  handle_GPDMA1_Channel1.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
  handle_GPDMA1_Channel1.InitLinkedList.LinkedListMode    = DMA_LINKEDLIST_CIRCULAR;
  if (HAL_DMAEx_List_Init(&handle_GPDMA1_Channel1) != HAL_OK)
  {
    return false;
  }

  if (HAL_DMAEx_List_LinkQ(&handle_GPDMA1_Channel1, &List_GPDMA1_Channel1) != HAL_OK)
  {
    return false;
  }

  if (HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel1, DMA_CHANNEL_NPRIV) != HAL_OK)
  {
    return false;
  }

  __DSB();  // V260909R1: descriptor 게시 후 DMA에 소유권 전달
  if (HAL_DMAEx_List_Start(&handle_GPDMA1_Channel1) != HAL_OK) return false;


  // Update Event
  //
  NodeConfig.NodeType                         = DMA_GPDMA_LINEAR_NODE;
  NodeConfig.Init.Request                     = GPDMA1_REQUEST_TIM16_UP;
  NodeConfig.Init.BlkHWRequest                = DMA_BREQ_SINGLE_BURST;
  NodeConfig.Init.Direction                   = DMA_PERIPH_TO_MEMORY;
  NodeConfig.Init.SrcInc                      = DMA_SINC_FIXED;
  NodeConfig.Init.DestInc                     = DMA_DINC_INCREMENTED;
  NodeConfig.Init.SrcDataWidth                = DMA_SRC_DATAWIDTH_HALFWORD;
  NodeConfig.Init.DestDataWidth               = DMA_DEST_DATAWIDTH_HALFWORD;
  NodeConfig.Init.SrcBurstLength              = 1;
  NodeConfig.Init.DestBurstLength             = 1;
  NodeConfig.Init.TransferAllocatedPort       = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
  NodeConfig.Init.TransferEventMode           = DMA_TCEM_BLOCK_TRANSFER;
  NodeConfig.Init.Mode                        = DMA_NORMAL;
  NodeConfig.TriggerConfig.TriggerPolarity    = DMA_TRIG_POLARITY_MASKED;
  NodeConfig.DataHandlingConfig.DataExchange  = DMA_EXCHANGE_NONE;
  NodeConfig.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
  NodeConfig.SrcAddress                       = (uint32_t)&GPIOB->IDR;
  NodeConfig.DstAddress                       = (uint32_t)&col_rd_buf[0];
  NodeConfig.DataSize                         = sizeof(col_rd_buf);
  if (HAL_DMAEx_List_BuildNode(&NodeConfig, &Node_GPDMA1_Channel2) != HAL_OK)
  {
    return false;
  }

  if (HAL_DMAEx_List_InsertNode(&List_GPDMA1_Channel2, NULL, &Node_GPDMA1_Channel2) != HAL_OK)
  {
    return false;
  }

  if (HAL_DMAEx_List_SetCircularMode(&List_GPDMA1_Channel2) != HAL_OK)
  {
    return false;
  }

  handle_GPDMA1_Channel2.Instance                         = GPDMA1_Channel2;
  handle_GPDMA1_Channel2.InitLinkedList.Priority          = DMA_LOW_PRIORITY_MID_WEIGHT;
  handle_GPDMA1_Channel2.InitLinkedList.LinkStepMode      = DMA_LSM_FULL_EXECUTION;
  handle_GPDMA1_Channel2.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT1;
  handle_GPDMA1_Channel2.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
  handle_GPDMA1_Channel2.InitLinkedList.LinkedListMode    = DMA_LINKEDLIST_CIRCULAR;
  if (HAL_DMAEx_List_Init(&handle_GPDMA1_Channel2) != HAL_OK)
  {
    return false;
  }

  if (HAL_DMAEx_List_LinkQ(&handle_GPDMA1_Channel2, &List_GPDMA1_Channel2) != HAL_OK)
  {
    return false;
  }

  if (HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel2, DMA_CHANNEL_NPRIV) != HAL_OK)
  {
    return false;
  }

  __DSB();
  if (HAL_DMAEx_List_Start(&handle_GPDMA1_Channel2) != HAL_OK) return false;

  return true;
}

bool keysIsBusy(void)
{
  return false;
}

bool keysReadBuf(uint8_t *p_data, uint32_t length)
{
  return true;
}

bool keysReadColsBuf(uint16_t *p_data, uint32_t rows_cnt)
{
  if (p_data == NULL || rows_cnt > MATRIX_ROWS) return false;
  for (uint32_t row = 0; row < rows_cnt; row++) p_data[row] = col_rd_buf[row];  // V260909R1: volatile halfword 관측 유지
  return true;
}

const volatile uint16_t *keysPeekColsBuf(void)
{
  return col_rd_buf;  // V250924R5: DMA 수집 버퍼를 직접 노출하여 추가 복사 없이 스캔 상태를 참조 (재검토: volatile 접근으로 안정성 확보)
}

bool keysGetPressed(uint16_t row, uint16_t col)
{
  bool     ret = false;
  uint16_t col_bit;

  if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return false;  // V260909R1: public accessor 범위 검사
  col_bit = col_rd_buf[row];

  if (col_bit & (1<<col))
  {
    ret = true;
  }

  return ret;
}

#endif
