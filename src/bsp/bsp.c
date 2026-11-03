#include "bsp.h"
#include "hw_def.h"
#include "usb_lifecycle.h"


static void SystemClock_Config(void);
static void bspMpuInit(void);
extern void bspResetMpu(void);

static void bspStopInheritedDma(uint32_t first, uint32_t last, uint32_t stride)
{
  for (uint32_t address = first; address <= last; address += stride)
  {
    DMA_Channel_TypeDef *channel = (DMA_Channel_TypeDef *)address;
    if ((channel->CCR & DMA_CCR_EN) != 0U)
    {
      /* RM0477: SUSP is the request; SUSPF is a separate sticky event.
         Resume even an inherited suspended channel before establishing a new
         request/acknowledgement. Its old buffers still belong to the loader. */
      channel->CCR &= ~DMA_CCR_SUSP;
      channel->CFCR = DMA_CFCR_SUSPF;
      __DSB();
      channel->CCR |= DMA_CCR_SUSP;
      /* No HAL tick exists yet. Fail closed if the bus master cannot drain. */
      while ((channel->CCR & DMA_CCR_EN) != 0U && (channel->CSR & DMA_CSR_SUSPF) == 0U)
      {
      }
    }
    channel->CCR |= DMA_CCR_RESET;
    __DSB();
    while ((channel->CCR & DMA_CCR_EN) != 0U)
    {
    }
  }
}

/* Runs on the DTCM stack, before SystemInit and all C data initialization.
 * Inspected loaders copy the image through Normal Shareable SRAM (uncached,
 * or write-through with SIWT), even in cache-enabled builds. Old dirty lines
 * are not authoritative. Cleaning them could overwrite the newly copied image. */
void bspPrepareEntry(void)
{
  uint32_t banks = (SCnSCB->ICTR & SCnSCB_ICTR_INTLINESNUM_Msk) + 1U;
  for (uint32_t bank = 0; bank < banks; bank++)
  {
    NVIC->ICER[bank] = 0xFFFFFFFFU;
    NVIC->ICPR[bank] = 0xFFFFFFFFU;
  }
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
  __set_BASEPRI(0);
  __set_FAULTMASK(0);
  __DSB();
  __ISB();
  __HAL_RCC_GPDMA1_CLK_ENABLE();
  __HAL_RCC_HPDMA1_CLK_ENABLE();
  bspStopInheritedDma(GPDMA1_Channel0_BASE, GPDMA1_Channel15_BASE,
                      GPDMA1_Channel1_BASE - GPDMA1_Channel0_BASE);
  bspStopInheritedDma(HPDMA1_Channel0_BASE, HPDMA1_Channel15_BASE,
                      HPDMA1_Channel1_BASE - HPDMA1_Channel0_BASE);
  __HAL_RCC_GPDMA1_FORCE_RESET();
  __HAL_RCC_HPDMA1_FORCE_RESET();
  __DSB();
  __HAL_RCC_GPDMA1_RELEASE_RESET();
  __HAL_RCC_HPDMA1_RELEASE_RESET();
  __HAL_RCC_GPDMA1_CLK_DISABLE();
  __HAL_RCC_HPDMA1_CLK_DISABLE();
  __DSB();
  /* Reset_Handler has already cleared CCR.DC without writeback. */
  SCB_InvalidateDCache();
}

bool bspInit(void)
{
  bool ret = true;

  bspMpuInit();
  __enable_irq();
  HAL_Init();


  SystemClock_Config();

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();


  return ret;
}

void delay(uint32_t ms)
{
#ifdef _USE_HW_RTOS
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    osDelay(ms);
  }
  else
  {
    HAL_Delay(ms);
  }
#else
  // HAL_Delay's minimum-wait arithmetic, with a publish-only USB lifecycle turn at each poll.
  uint32_t start = HAL_GetTick();
  uint32_t wait  = ms;

  if (wait < HAL_MAX_DELAY)
  {
    wait += (uint32_t)HAL_GetTickFreq();
  }
  while ((HAL_GetTick() - start) < wait)
  {
    usbLifecycleThreadCheckpoint();
  }
#endif
}

uint32_t millis(void)
{
  return HAL_GetTick();
}

void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;  
  RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL1.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL1.PLLM = 2;
  RCC_OscInitStruct.PLL1.PLLN = 50;
  RCC_OscInitStruct.PLL1.PLLP = 1;
  RCC_OscInitStruct.PLL1.PLLQ = 2;
  RCC_OscInitStruct.PLL1.PLLR = 2;
  RCC_OscInitStruct.PLL1.PLLS = 2;
  RCC_OscInitStruct.PLL1.PLLT = 2;
  RCC_OscInitStruct.PLL1.PLLFractional = 0;
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK4|RCC_CLOCKTYPE_PCLK5;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
  RCC_ClkInitStruct.APB5CLKDivider = RCC_APB5_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_6) != HAL_OK)
  {
    Error_Handler();
  }
}

void bspMpuInit(void)
{

  /*  T C B
   *  0 0 0 Strongly Ordered  , Stringly Ordered
   *  0 0 1 Device            , Shared Device
   *  0 1 0 Normal            , Write through, no write allocate (메모리 동기화됨)
   *  0 1 1 Normal            , Write-back, no write allocate (실제 메모리와 다를 수 있음)
   *  1 0 0 Normal            , Non-cacheable
   *  1 0 1 Reserved
   *  1 1 0 Undefined
   *  1 1 1 Normal            , Write-back, write and read allocate
   *  2 0 0 Device            , Non-shareable device
   *  2 0 1 Reserved
   */

  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Entry discarded obsolete cache state before C initialization. Caches
     remain disabled until hwInit, so no new dirty lines cross this change. */
  bspResetMpu();


  // MPU 설정 메모리 크기에 정렬된 주소가 입력되어야 함.

  /* Configure the MPU as Strongly ordered for not defined regions */
  MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress      = 0x0;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);


  /* Write-back, write and read allocate */
  MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress      = 0x24000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);


  /* None Cache */
  MPU_InitStruct.Number           = MPU_REGION_NUMBER2;
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress      = 0x24000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_16KB;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);


  /* Write-back, write and read allocate */
  MPU_InitStruct.Number           = MPU_REGION_NUMBER3;
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress      = 0x08000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_64KB;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);


  /* Write-back, write and read allocate */
  MPU_InitStruct.Number           = MPU_REGION_NUMBER4;
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress      = 0x90000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_2MB;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);


  // V260909R1: ES0596 Rev10 2.2.17. H7S3에는 없는 GFXMMU 영역도 speculative access를 차단한다.
  MPU_InitStruct.Number           = MPU_REGION_NUMBER5;
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress      = 0x25000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_16MB;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;  // TEX=0,C=0,B=1: Device
  MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Enable the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
  __DSB();
  __ISB();
}
