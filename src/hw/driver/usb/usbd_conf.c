/**
  ******************************************************************************
  * @file    USB_Device/CDC_Standalone/Src/usbd_conf.c
  * @author  MCD Application Team
  * @brief   This file implements the USB Device library callbacks and MSP
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2017 STMicroelectronics International N.V. 
  * All rights reserved.</center></h2>
  *
  * Redistribution and use in source and binary forms, with or without 
  * modification, are permitted, provided that the following conditions are met:
  *
  * 1. Redistribution of source code must retain the above copyright notice, 
  *    this list of conditions and the following disclaimer.
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  *    this list of conditions and the following disclaimer in the documentation
  *    and/or other materials provided with the distribution.
  * 3. Neither the name of STMicroelectronics nor the names of other 
  *    contributors to this software may be used to endorse or promote products 
  *    derived from this software without specific written permission.
  * 4. This software, including modifications and/or derivative works of this 
  *    software, must execute solely and exclusively on microcontroller or
  *    microprocessor devices manufactured by or for STMicroelectronics.
  * 5. Redistribution and use of this software other than as permitted under 
  *    this license is void and will automatically terminate your rights under 
  *    this license. 
  *
  * THIS SOFTWARE IS PROVIDED BY STMICROELECTRONICS AND CONTRIBUTORS "AS IS" 
  * AND ANY EXPRESS, IMPLIED OR STATUTORY WARRANTIES, INCLUDING, BUT NOT 
  * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A 
  * PARTICULAR PURPOSE AND NON-INFRINGEMENT OF THIRD PARTY INTELLECTUAL PROPERTY
  * RIGHTS ARE DISCLAIMED TO THE FULLEST EXTENT PERMITTED BY LAW. IN NO EVENT 
  * SHALL STMICROELECTRONICS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
  * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
  * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
  * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "hw.h"
#include "usbd_def.h"
#include "usbd_core.h"
#include "usbd_cdc.h"
#include "usbd_hid.h"
#include "usbd_hid_internal.h"

#define USBD_HANDOFF_RESET_HOLD_MS  (100U)   // V260912R1: 물려받은 USB 블록을 리셋한 뒤 호스트가 분리를 확정할 시간

PCD_HandleTypeDef hpcd_USB_OTG_HS;
void Error_Handler(void);
static bool is_connected = false;
static volatile bool bus_suspended = false;
static volatile bool pcd_reset_pending = false;
static bool pcd_resume_pending = false;
static bool pcd_resume_skip_stale_sof = false;
static volatile uint32_t sof_count = 0;  // V260901R1: SOF 생존 카운터. 점수는 계산하지 않는다.
static bool host_seen = false;           // V260901R1: 한 번이라도 주소를 받은 뒤에만 호스트 소실로 본다


/* External functions --------------------------------------------------------*/

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/
USBD_StatusTypeDef USBD_Get_USB_Status(HAL_StatusTypeDef hal_status);

/* USER CODE END PFP */

/* Private functions ---------------------------------------------------------*/

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */

bool USBD_is_connected(void)
{
  return is_connected;
}

bool USBD_is_suspended(void)
{
  return bus_suspended;
}

bool USBD_host_seen(void)
{
  return host_seen;  // V260901R1
}

uint32_t USBD_sof_count(void)
{
  return sof_count;  // V260901R1
}

bool USBD_is_reset_pending(void)
{
  return pcd_reset_pending;
}

static void usbPcdBeginReset(void)
{
  if (pcd_reset_pending) return;

  // Raw USBRST precedes ENUMDNE's stack Reset. Retire software admission now,
  // while leaving payload and controller ownership to the normal teardown.
  pcd_reset_pending = true;
  pcd_resume_pending = false;
  pcd_resume_skip_stale_sof = false;
  usbHidOnBusResetBegin();
}

void usbPcdOnIrqEntry(PCD_HandleTypeDef *hpcd)
{
  if (pcd_reset_pending || hpcd == NULL || hpcd->Instance == NULL) return;
  uint32_t status = hpcd->Instance->GINTSTS;
  if ((status & USB_OTG_GINTSTS_CMOD) != 0U ||
      (status & hpcd->Instance->GINTMSK & USB_OTG_GINTSTS_USBRST) == 0U) return;

  __HAL_PCD_UNGATE_PHYCLOCK(hpcd);
  usbPcdBeginReset();
}

void HAL_PCD_ResetBeginCallback(PCD_HandleTypeDef *hpcd)
{
  if (pcd_reset_pending) return;
  __HAL_PCD_UNGATE_PHYCLOCK(hpcd);
  usbPcdBeginReset();
}

static bool usbPcdHardwareActive(PCD_HandleTypeDef *hpcd)
{
  if (hpcd == NULL || hpcd->Instance == NULL) return false;
  USB_OTG_DeviceTypeDef *device =
      (USB_OTG_DeviceTypeDef *)((uintptr_t)hpcd->Instance + USB_OTG_DEVICE_BASE);
  return (device->DSTS & USB_OTG_DSTS_SUSPSTS) == 0U;
}

static void usbPcdResumeIfActive(PCD_HandleTypeDef *hpcd)
{
  USBD_HandleTypeDef *pdev = (USBD_HandleTypeDef *)hpcd->pData;
  if (pcd_reset_pending || pdev == NULL || pdev->dev_state != USBD_STATE_SUSPENDED || !usbPcdHardwareActive(hpcd)) return;

  __HAL_PCD_UNGATE_PHYCLOCK(hpcd);
  bus_suspended = false;
  pcd_resume_pending = false;
  pcd_resume_skip_stale_sof = false;
  usbHidOnResume();
  (void)USBD_LL_Resume(pdev);
}

/*******************************************************************************
                       LL Driver Callbacks (PCD -> USB Device Library)
*******************************************************************************/
/* MSP Init */

void HAL_PCD_MspInit(PCD_HandleTypeDef* pcdHandle)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if (pcdHandle->Instance == USB_OTG_HS)
  {
    /** Initializes the peripherals clock
     */
    PeriphClkInit.PeriphClockSelection  = RCC_PERIPHCLK_USBPHYC;
    PeriphClkInit.UsbPhycClockSelection = RCC_USBPHYCCLKSOURCE_HSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /** Enable USB Voltage detector
     */
    HAL_PWREx_EnableUSBVoltageDetector();

    /* USB_OTG_HS clock enable */
    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
    __HAL_RCC_USBPHYC_CLK_ENABLE();

    // V260912R1: 구 부트로더는 UF2 업로드 뒤 USB 를 켠 채 클럭만 끊고 펌웨어로 점프한다.
    //            그렇게 물려받은 OTG 코어와 PHY 는 HAL_PCD_Init() 의 코어 소프트리셋에 응답하지
    //            않아 HAL_USB_TIMEOUT 뒤(약 10 s) 실패하고 Error_Handler 에서 멈춘다.
    //            부트로더 TinyUSB 가 세워 둔 GINT 가 켜져 있으면 그 코어를 물려받은 것이므로
    //            두 블록을 RCC 로 리셋해 전원 인가 상태로 되돌리고, 부트로더 세션의 pending
    //            인터럽트를 버린 뒤, 풀업이 떨어진 채로 유예를 두고 평소 초기화로 진행한다.
    //            콜드부트·VIA 리셋·리셋 방식 부트로더는 GINT 가 0 이라 이 분기를 타지 않는다.
    //            docs/contract_usb.md §6
    if ((USB_OTG_HS->GAHBCFG & USB_OTG_GAHBCFG_GINT) != 0U)
    {
      __HAL_RCC_USB_OTG_HS_FORCE_RESET();
      __HAL_RCC_USBPHYC_FORCE_RESET();
      delay(1);
      __HAL_RCC_USBPHYC_RELEASE_RESET();
      __HAL_RCC_USB_OTG_HS_RELEASE_RESET();
      NVIC_ClearPendingIRQ(OTG_HS_IRQn);
      delay(USBD_HANDOFF_RESET_HOLD_MS);
    }

    /* Peripheral interrupt init */
    HAL_NVIC_SetPriority(OTG_HS_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
  }
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef* pcdHandle)
{
  if(pcdHandle->Instance==USB_OTG_HS)
  {
    /* Peripheral clock disable */
    __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
    __HAL_RCC_USBPHYC_CLK_DISABLE();

    /* Peripheral interrupt Deinit*/
    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
  }
}

/**
  * @brief  Setup stage callback
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  usbPcdOnIrqEntry(hpcd);
  if (pcd_reset_pending) return;
  // HAL dispatches endpoint events before WKUINT. Restore the active bus state
  // before the core validates SETUP or consumes a transfer completion.
  usbPcdResumeIfActive(hpcd);
  USBD_LL_SetupStage((USBD_HandleTypeDef*)hpcd->pData, (uint8_t *)hpcd->Setup);
}

/**
  * @brief  Data Out stage callback.
  * @param  hpcd: PCD handle
  * @param  epnum: Endpoint number
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  usbPcdOnIrqEntry(hpcd);
  if (pcd_reset_pending) return;
  usbPcdResumeIfActive(hpcd);
  USBD_LL_DataOutStage((USBD_HandleTypeDef*)hpcd->pData, epnum, hpcd->OUT_ep[epnum].xfer_buff);
}

/**
  * @brief  Data In stage callback.
  * @param  hpcd: PCD handle
  * @param  epnum: Endpoint number
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  usbPcdOnIrqEntry(hpcd);
  if (pcd_reset_pending) return;
  usbPcdResumeIfActive(hpcd);
  USBD_LL_DataInStage((USBD_HandleTypeDef*)hpcd->pData, epnum, hpcd->IN_ep[epnum].xfer_buff);
}

/**
  * @brief  SOF callback.
  * @param  hpcd: PCD handle
  * @retval None
  */
static void usbHidLogicalSuspendedSof(PCD_HandleTypeDef *hpcd) __attribute__((noinline));
static void usbHidLogicalSuspendedSof(PCD_HandleTypeDef *hpcd)
{
  USBD_HandleTypeDef *pdev = (USBD_HandleTypeDef *)hpcd->pData;
  if (pcd_reset_pending) return;
  bool stale_resume_sof = pcd_resume_skip_stale_sof;
  pcd_resume_skip_stale_sof = false;
  if (pdev != NULL && usbPcdHardwareActive(hpcd)) {
    bool wake_sof = usbHidConsumeWakeSof();
    if ((pcd_resume_pending && !stale_resume_sof) || wake_sof) {
      usbPcdResumeIfActive(hpcd);
    }
  }
  (void)USBD_LL_SOF(pdev);
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  USBD_HandleTypeDef *pdev = (USBD_HandleTypeDef *)hpcd->pData;
  sof_count++;  // V260901R1: RGB 판정은 메인 루프.
  usbPcdOnIrqEntry(hpcd);
  if (pcd_reset_pending) return;
  if (bus_suspended && usbPcdHardwareActive(hpcd)) {
    bus_suspended = false;
  }
  if (pdev != NULL && pdev->dev_state == USBD_STATE_SUSPENDED) {
    usbHidLogicalSuspendedSof(hpcd);
    return;
  }
  (void)USBD_LL_SOF(pdev);
}

/**
  * @brief  Reset callback.
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  __HAL_PCD_UNGATE_PHYCLOCK(hpcd);
  usbPcdBeginReset();
  USBD_SpeedTypeDef speed = USBD_SPEED_FULL;

  if ( hpcd->Init.speed == PCD_SPEED_HIGH)
  {
    speed = USBD_SPEED_HIGH;
  }
  else if ( hpcd->Init.speed == PCD_SPEED_HIGH_IN_FULL)
  {
    speed = USBD_SPEED_FULL;                                      // V250923R1 HS core running in FS mode
  }
  else if ( hpcd->Init.speed == PCD_SPEED_FULL)
  {
    speed = USBD_SPEED_FULL;
  }
  else
  {
    Error_Handler();
  }
  // USB Reset is bus activity and starts a new USB session. It authoritatively
  // ends any cached physical-suspend state before the device stack is reset.
  bus_suspended = false;
  pcd_resume_pending = false;
  pcd_resume_skip_stale_sof = false;
    /* Set Speed. */
  USBD_LL_SetSpeed((USBD_HandleTypeDef*)hpcd->pData, speed);

  /* Reset Device. */
  USBD_LL_Reset((USBD_HandleTypeDef*)hpcd->pData);
  pcd_reset_pending = false;
}

/**
  * @brief  Suspend callback.
  * When Low power mode is enabled the debug cannot be used (IAR, Keil doesn't support it)
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  usbPcdOnIrqEntry(hpcd);
  if (pcd_reset_pending) return;
  /* Inform USB library that core enters in suspend Mode. */
  USBD_LL_Suspend((USBD_HandleTypeDef*)hpcd->pData);
  pcd_resume_pending = false;
  pcd_resume_skip_stale_sof = false;
  usbHidOnSuspend();  // V260909R1: 비차단 wake와 현재 키 상태 보존
  __HAL_PCD_GATE_PHYCLOCK(hpcd);
  /* Enter in STOP mode. */
  /* USER CODE BEGIN 2 */
  if (hpcd->Init.low_power_enable)
  {
    /* Set SLEEPDEEP bit and SleepOnExit of Cortex System Control Register. */
    SCB->SCR |= (uint32_t)((uint32_t)(SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk));
  }

  is_connected = false;
  bus_suspended = true;
  /* USER CODE END 2 */
}

/**
  * @brief  Resume callback.
  * When Low power mode is enabled the debug cannot be used (IAR, Keil doesn't support it)
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  /* USER CODE BEGIN 3 */

  usbPcdOnIrqEntry(hpcd);
  if (pcd_reset_pending) return;
  // Host-driven wake must undo Suspend's STOPCLK too, before sampling DSTS.
  __HAL_PCD_UNGATE_PHYCLOCK(hpcd);
  USBD_HandleTypeDef *pdev = (USBD_HandleTypeDef *)hpcd->pData;
  bool hardware_resumed = usbPcdHardwareActive(hpcd);
  if (hardware_resumed) {
    bus_suspended = false;
  }
  if (pdev != NULL && pdev->dev_state == USBD_STATE_SUSPENDED) {
    // If WKUINT precedes the hardware-active observation, retain the indication
    // until fresh bus activity without treating a stale SOF as Resume.
    pcd_resume_pending = true;
    pcd_resume_skip_stale_sof = (hpcd->Instance->GINTSTS & USB_OTG_GINTSTS_SOF) != 0U;
    usbPcdResumeIfActive(hpcd);
  }
  /* USER CODE END 3 */
}

/**
  * @brief  ISOOUTIncomplete callback.
  * @param  hpcd: PCD handle
  * @param  epnum: Endpoint number
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  usbPcdOnIrqEntry(hpcd);
  if (pcd_reset_pending) return;
  USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef*)hpcd->pData, epnum);
}

/**
  * @brief  ISOINIncomplete callback.
  * @param  hpcd: PCD handle
  * @param  epnum: Endpoint number
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  usbPcdOnIrqEntry(hpcd);
  if (pcd_reset_pending) return;
  USBD_LL_IsoINIncomplete((USBD_HandleTypeDef*)hpcd->pData, epnum);
}

/**
  * @brief  Connect callback.
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  usbPcdOnIrqEntry(hpcd);
  if (pcd_reset_pending) return;
  USBD_LL_DevConnected((USBD_HandleTypeDef*)hpcd->pData);
}

/**
  * @brief  Disconnect callback.
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  pcd_resume_pending = false;
  pcd_resume_skip_stale_sof = false;
  USBD_LL_DevDisconnected((USBD_HandleTypeDef*)hpcd->pData);
}

/*******************************************************************************
                       LL Driver Interface (USB Device Library --> PCD)
*******************************************************************************/

/**
  * @brief  Initializes the low level portion of the device driver.
  * @param  pdev: Device handle
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
  /* Init USB Ip. */
  if (pdev->id == DEVICE_HS) {
  /* Link the driver to the stack. */
  hpcd_USB_OTG_HS.pData = pdev;
  pdev->pData = &hpcd_USB_OTG_HS;

  hpcd_USB_OTG_HS.Instance = USB_OTG_HS;
  hpcd_USB_OTG_HS.Init.dev_endpoints = 9;
  if (usbBootModeIsFullSpeed() == true)
  {
    hpcd_USB_OTG_HS.Init.speed = PCD_SPEED_HIGH_IN_FULL;          // V250923R1 HS PHY operating at FS 1 kHz
  }
  else
  {
    hpcd_USB_OTG_HS.Init.speed = PCD_SPEED_HIGH;
  }
  hpcd_USB_OTG_HS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.phy_itface = USB_OTG_HS_EMBEDDED_PHY;
  hpcd_USB_OTG_HS.Init.Sof_enable = ENABLE;
  hpcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_HS) != HAL_OK)
  {
    Error_Handler( );
  }

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
  /* Register USB PCD CallBacks */
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_SOF_CB_ID, PCD_SOFCallback);
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_SETUPSTAGE_CB_ID, PCD_SetupStageCallback);
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_RESET_CB_ID, PCD_ResetCallback);
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_SUSPEND_CB_ID, PCD_SuspendCallback);
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_RESUME_CB_ID, PCD_ResumeCallback);
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_CONNECT_CB_ID, PCD_ConnectCallback);
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_DISCONNECT_CB_ID, PCD_DisconnectCallback);

  HAL_PCD_RegisterDataOutStageCallback(&hpcd_USB_OTG_HS, PCD_DataOutStageCallback);
  HAL_PCD_RegisterDataInStageCallback(&hpcd_USB_OTG_HS, PCD_DataInStageCallback);
  HAL_PCD_RegisterIsoOutIncpltCallback(&hpcd_USB_OTG_HS, PCD_ISOOUTIncompleteCallback);
  HAL_PCD_RegisterIsoInIncpltCallback(&hpcd_USB_OTG_HS, PCD_ISOINIncompleteCallback);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
  
  // V260909R1: OTG HS 1024-word 상한 내 752 words. 미사용 낮은 FIFO도 최소16 words.
  enum { RX_WORDS = 512, TX0 = 32, TX1 = 32, TX2 = 128, TX3 = 16, TX4 = 16, TX5 = 16 };
  _Static_assert(RX_WORDS + TX0 + TX1 + TX2 + TX3 + TX4 + TX5 <= 1024, "USB FIFO overflow");
  HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_HS, RX_WORDS);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 0, TX0);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 1, TX1);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 2, TX2);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 3, TX3);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 4, TX4);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 5, TX5);
  }
  return USBD_OK;
}

/**
  * @brief  De-Initializes the low level portion of the device driver.
  * @param  pdev: Device handle
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_DeInit(pdev->pData);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Starts the low level portion of the device driver.
  * @param  pdev: Device handle
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  // V260912R1: V260824R2가 여기 두었던 부팅 시 100 ms 디태치 유지를 제거했다.
  //            구 부트로더 보드의 실패는 HAL_PCD_Init() 안에 있어 이 유지는 실행된 적이
  //            없었다. 실제 조치는 HAL_PCD_MspInit() 의 물려받은 블록 리셋이다.
  //            docs/contract_usb.md §6
  hal_status = HAL_PCD_Start(pdev->pData);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Stops the low level portion of the device driver.
  * @param  pdev: Device handle
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_Stop(pdev->pData);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Opens an endpoint of the low level driver.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @param  ep_type: Endpoint type
  * @param  ep_mps: Endpoint max packet size
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t ep_type, uint16_t ep_mps)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  PCD_HandleTypeDef *hpcd = pdev->pData;
  uint32_t ep = ep_addr & 0x7FU;
  if (hpcd == NULL || ep >= hpcd->Init.dev_endpoints) return USBD_FAIL;
  uint32_t USBx_BASE = (uint32_t)hpcd->Instance;
  if (ep == 0U) return USBD_Get_USB_Status(HAL_PCD_EP_Open(hpcd, ep_addr, ep_mps, ep_type));
  if (ep_addr & 0x80U) {
    if (USBx_INEP(ep)->DIEPCTL & USB_OTG_DIEPCTL_EPENA) return USBD_FAIL;
    USBx_INEP(ep)->DIEPINT = USBx_INEP(ep)->DIEPINT;
  } else {
    if (USBx_OUTEP(ep)->DOEPCTL & USB_OTG_DOEPCTL_EPENA) return USBD_FAIL;
    USBx_OUTEP(ep)->DOEPINT = USBx_OUTEP(ep)->DOEPINT;
  }
  hal_status = HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Closes an endpoint of the low level driver.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return USBD_Get_USB_Status(HAL_PCD_EP_Close(pdev->pData, ep_addr));
}

/**
  * @brief  Flushes an endpoint of the Low Level Driver.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_Flush(pdev->pData, ep_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Sets a Stall condition on an endpoint of the Low Level Driver.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_SetStall(pdev->pData, ep_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Clears a Stall condition on an endpoint of the Low Level Driver.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_ClrStall(pdev->pData, ep_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Returns Stall condition.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @retval Stall (1: Yes, 0: No)
  */
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef*) pdev->pData;

  if((ep_addr & 0x80) == 0x80)
  {
    return hpcd->IN_ep[ep_addr & 0x7F].is_stall;
  }
  else
  {
    return hpcd->OUT_ep[ep_addr & 0x7F].is_stall;
  }
}

/**
  * @brief  Assigns a USB address to the device.
  * @param  pdev: Device handle
  * @param  dev_addr: Device address
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_SetAddress(pdev->pData, dev_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  is_connected = true;
  host_seen = true;  // V260901R1: 주소 할당 = 호스트가 한 번 나타났다. 이후에도 지우지 않는다.

  return usb_status;
}

/**
  * @brief  Transmits data over an endpoint.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @param  pbuf: Pointer to data to be sent
  * @param  size: Data size
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint32_t size)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  PCD_HandleTypeDef *hpcd = pdev->pData;
  uint32_t keyboard_ep = HID_EPIN_ADDR & EP_ADDR_MSK;
  if (ep_addr == HID_EPIN_ADDR &&
      (size == HID_KEYBOARD_REPORT_SIZE || size == HID_BOOT_KEYBOARD_REPORT_SIZE) &&
      hpcd != NULL && hpcd->Init.dma_enable == 0U && keyboard_ep < hpcd->Init.dev_endpoints &&
      hpcd->IN_ep[keyboard_ep].type == EP_TYPE_INTR && size <= hpcd->IN_ep[keyboard_ep].maxpacket)
  {
    hal_status = HAL_PCD_EP_TransmitReady(hpcd, ep_addr, pbuf, size);
  }
  else
  {
    hal_status = HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size);
  }

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Prepares an endpoint for reception.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @param  pbuf: Pointer to data to be received
  * @param  size: Data size
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint32_t size)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Returns the last transferred packet size.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @retval Received Data Size
  */
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef*) pdev->pData, ep_addr);
}

#ifdef USBD_HS_TESTMODE_ENABLE
/**
  * @brief  Set High speed Test mode.
  * @param  pdev: Device handle
  * @param  testmode: test mode
  * @retval USBD Status
  */
USBD_StatusTypeDef USBD_LL_SetTestMode(USBD_HandleTypeDef *pdev, uint8_t testmode)
{
  UNUSED(pdev);
  UNUSED(testmode);

  return USBD_OK;
}
#endif /* USBD_HS_TESTMODE_ENABLE */
/**
  * @brief  Delays routine for the USB device library.
  * @param  Delay: Delay in ms
  * @retval None
  */
void USBD_LL_Delay(uint32_t Delay)
{
  HAL_Delay(Delay);
}

/**
  * @brief  Returns the USB status depending on the HAL status:
  * @param  hal_status: HAL status
  * @retval USB status
  */
USBD_StatusTypeDef USBD_Get_USB_Status(HAL_StatusTypeDef hal_status)
{
  USBD_StatusTypeDef usb_status = USBD_OK;

  switch (hal_status)
  {
    case HAL_OK :
      usb_status = USBD_OK;
    break;
    case HAL_ERROR :
      usb_status = USBD_FAIL;
    break;
    case HAL_BUSY :
      usb_status = USBD_BUSY;
    break;
    case HAL_TIMEOUT :
      usb_status = USBD_FAIL;
    break;
    default :
      usb_status = USBD_FAIL;
    break;
  }
  return usb_status;
}
