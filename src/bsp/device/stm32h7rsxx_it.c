/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h7rsxx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "bsp.h"
#include "stm32h7rsxx_it.h"



/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  while (1)
  {
  }
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  uint32_t cfsr  = SCB->CFSR;                                 // V251123R7: Fault 원인 로깅
  uint32_t hfsr  = SCB->HFSR;
  uint32_t mmfar = SCB->MMFAR;
  uint32_t bfar  = SCB->BFAR;

  logPrintf("[F] HardFault cfsr=0x%08lX hfsr=0x%08lX mmfar=0x%08lX bfar=0x%08lX\n",
            cfsr, hfsr, mmfar, bfar);
  NVIC_SystemReset();
  while (1)
  {
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  uint32_t cfsr  = SCB->CFSR;
  uint32_t mmfar = SCB->MMFAR;

  logPrintf("[F] MemFault cfsr=0x%08lX mmfar=0x%08lX\n", cfsr, mmfar);
  NVIC_SystemReset();
  while (1)
  {
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  uint32_t cfsr  = SCB->CFSR;
  uint32_t bfar  = SCB->BFAR;

  logPrintf("[F] BusFault cfsr=0x%08lX bfar=0x%08lX\n", cfsr, bfar);  // V251123R7: BusFault 즉시 리셋
  NVIC_SystemReset();
  while (1)
  {
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  uint32_t cfsr = SCB->CFSR;

  logPrintf("[F] UsageFault cfsr=0x%08lX\n", cfsr);
  NVIC_SystemReset();
  while (1)
  {
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
  /* V251124R2: V251123R8 메인 루프 헬스체크 계측 제거 */
}
