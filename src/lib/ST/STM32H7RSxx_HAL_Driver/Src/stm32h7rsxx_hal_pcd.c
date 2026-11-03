/**
  ******************************************************************************
  * @file    stm32h7rsxx_hal_pcd.c
  * @author  MCD Application Team
  * @brief   PCD HAL module driver.
  *          This file provides firmware functions to manage the following
  *          functionalities of the USB Peripheral Controller:
  *           + Initialization and de-initialization functions
  *           + IO operation functions
  *           + Peripheral Control functions
  *           + Peripheral State functions
  *
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  @verbatim
  ==============================================================================
                    ##### How to use this driver #####
  ==============================================================================
    [..]
      The PCD HAL driver can be used as follows:

     (#) Declare a PCD_HandleTypeDef handle structure, for example:
         PCD_HandleTypeDef  hpcd;

     (#) Fill parameters of Init structure in HCD handle

     (#) Call HAL_PCD_Init() API to initialize the PCD peripheral (Core, Device core, ...)

     (#) Initialize the PCD low level resources through the HAL_PCD_MspInit() API:
         (##) Enable the PCD/USB Low Level interface clock using
              (+++) __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
              (+++) __HAL_RCC_USB_OTG_HS_CLK_ENABLE(); (For High Speed Mode)

         (##) Initialize the related GPIO clocks
         (##) Configure PCD pin-out
         (##) Configure PCD NVIC interrupt

     (#)Associate the Upper USB device stack to the HAL PCD Driver:
         (##) hpcd.pData = pdev;

     (#)Enable PCD transmission and reception:
         (##) HAL_PCD_Start();

  @endverbatim
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32h7rsxx_hal.h"

/** @addtogroup STM32H7RSxx_HAL_Driver
  * @{
  */

/** @defgroup PCD PCD
  * @brief PCD HAL module driver
  * @{
  */

#ifdef HAL_PCD_MODULE_ENABLED

#if defined (USB_OTG_FS) || defined (USB_OTG_HS)
#include "stm32h7rsxx_hal_pcd_lifecycle.h"

/* Private types -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private constants ---------------------------------------------------------*/
/* Private macros ------------------------------------------------------------*/
/** @defgroup PCD_Private_Macros PCD Private Macros
  * @{
  */
#define PCD_MIN(a, b)  (((a) < (b)) ? (a) : (b))
#define PCD_MAX(a, b)  (((a) > (b)) ? (a) : (b))
/**
  * @}
  */

/* Private functions prototypes ----------------------------------------------*/
/** @defgroup PCD_Private_Functions PCD Private Functions
  * @{
  */
#if defined (USB_OTG_FS) || defined (USB_OTG_HS)
static HAL_StatusTypeDef PCD_WriteEmptyTxFifo(PCD_HandleTypeDef *hpcd, uint32_t epnum);
static HAL_StatusTypeDef PCD_EP_OutXfrComplete_int(PCD_HandleTypeDef *hpcd, uint32_t epnum);
static HAL_StatusTypeDef PCD_EP_OutSetupPacket_int(PCD_HandleTypeDef *hpcd, uint32_t epnum);
#endif /* defined (USB_OTG_FS) || defined (USB_OTG_HS) */
/**
  * @}
  */

/* Exported functions --------------------------------------------------------*/
/** @defgroup PCD_Exported_Functions PCD Exported Functions
  * @{
  */

/** @defgroup PCD_Exported_Functions_Group1 Initialization and de-initialization functions
  *  @brief    Initialization and Configuration functions
  *
@verbatim
 ===============================================================================
            ##### Initialization and de-initialization functions #####
 ===============================================================================
    [..]  This section provides functions allowing to:

@endverbatim
  * @{
  */

/**
  * @brief  Initializes the PCD according to the specified
  *         parameters in the PCD_InitTypeDef and initialize the associated handle.
  * @param  hpcd PCD handle
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_Init(PCD_HandleTypeDef *hpcd)
{
#if defined (USB_OTG_FS)
  const USB_OTG_GlobalTypeDef *USBx;
#endif /* defined (USB_OTG_FS) */
  uint8_t i;

  /* Check the PCD handle allocation */
  if (hpcd == NULL)
  {
    return HAL_ERROR;
  }

  /* Check the parameters */
  assert_param(IS_PCD_ALL_INSTANCE(hpcd->Instance));

#if defined (USB_OTG_FS)
  USBx = hpcd->Instance;
#endif /* defined (USB_OTG_FS) */

  if (hpcd->State == HAL_PCD_STATE_RESET)
  {
    /* Allocate lock resource and initialize it */
    hpcd->Lock = HAL_UNLOCKED;

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
    hpcd->SOFCallback = HAL_PCD_SOFCallback;
    hpcd->SetupStageCallback = HAL_PCD_SetupStageCallback;
    hpcd->ResetCallback = HAL_PCD_ResetCallback;
    hpcd->SuspendCallback = HAL_PCD_SuspendCallback;
    hpcd->ResumeCallback = HAL_PCD_ResumeCallback;
    hpcd->ConnectCallback = HAL_PCD_ConnectCallback;
    hpcd->DisconnectCallback = HAL_PCD_DisconnectCallback;
    hpcd->DataOutStageCallback = HAL_PCD_DataOutStageCallback;
    hpcd->DataInStageCallback = HAL_PCD_DataInStageCallback;
    hpcd->ISOOUTIncompleteCallback = HAL_PCD_ISOOUTIncompleteCallback;
    hpcd->ISOINIncompleteCallback = HAL_PCD_ISOINIncompleteCallback;
    hpcd->LPMCallback = HAL_PCDEx_LPM_Callback;
    hpcd->BCDCallback = HAL_PCDEx_BCD_Callback;

    if (hpcd->MspInitCallback == NULL)
    {
      hpcd->MspInitCallback = HAL_PCD_MspInit;
    }

    /* Init the low level hardware */
    hpcd->MspInitCallback(hpcd);
#else
    /* Init the low level hardware : GPIO, CLOCK, NVIC... */
    HAL_PCD_MspInit(hpcd);
#endif /* (USE_HAL_PCD_REGISTER_CALLBACKS) */
  }

  hpcd->State = HAL_PCD_STATE_BUSY;

#if defined (USB_OTG_FS)
  /* Disable DMA mode for FS instance */
  if (USBx == USB_OTG_FS)
  {
    hpcd->Init.dma_enable = 0U;
  }
#endif /* defined (USB_OTG_FS) */

  /* Disable the Interrupts */
  __HAL_PCD_DISABLE(hpcd);

  /*Init the Core (common init.) */
  if (USB_CoreInit(hpcd->Instance, hpcd->Init) != HAL_OK)
  {
    hpcd->State = HAL_PCD_STATE_ERROR;
    return HAL_ERROR;
  }

  /* Force Device Mode */
  if (USB_SetCurrentMode(hpcd->Instance, USB_DEVICE_MODE) != HAL_OK)
  {
    hpcd->State = HAL_PCD_STATE_ERROR;
    return HAL_ERROR;
  }

  /* Init endpoints structures */
  for (i = 0U; i < hpcd->Init.dev_endpoints; i++)
  {
    /* Init ep structure */
    hpcd->IN_ep[i].is_in = 1U;
    hpcd->IN_ep[i].num = i;
    hpcd->IN_ep[i].tx_fifo_num = i;
    /* Control until ep is activated */
    hpcd->IN_ep[i].type = EP_TYPE_CTRL;
    hpcd->IN_ep[i].maxpacket = 0U;
    hpcd->IN_ep[i].xfer_buff = 0U;
    hpcd->IN_ep[i].xfer_len = 0U;
  }

  for (i = 0U; i < hpcd->Init.dev_endpoints; i++)
  {
    hpcd->OUT_ep[i].is_in = 0U;
    hpcd->OUT_ep[i].num = i;
    /* Control until ep is activated */
    hpcd->OUT_ep[i].type = EP_TYPE_CTRL;
    hpcd->OUT_ep[i].maxpacket = 0U;
    hpcd->OUT_ep[i].xfer_buff = 0U;
    hpcd->OUT_ep[i].xfer_len = 0U;
  }

  /* Init Device */
  if (USB_DevInit(hpcd->Instance, hpcd->Init) != HAL_OK)
  {
    hpcd->State = HAL_PCD_STATE_ERROR;
    return HAL_ERROR;
  }

  hpcd->USB_Address = 0U;
  hpcd->State = HAL_PCD_STATE_READY;

  /* Activate LPM */
  if (hpcd->Init.lpm_enable == 1U)
  {
    (void)HAL_PCDEx_ActivateLPM(hpcd);
  }

  (void)USB_DevDisconnect(hpcd->Instance);

  return HAL_OK;
}

/**
  * @brief  Initializes the PCD MSP.
  * @param  hpcd PCD handle
  * @retval None
  */
__weak void HAL_PCD_MspInit(PCD_HandleTypeDef *hpcd)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hpcd);

  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_PCD_MspInit could be implemented in the user file
   */
}

/**
  * @brief  DeInitializes PCD MSP.
  * @param  hpcd PCD handle
  * @retval None
  */
__weak void HAL_PCD_MspDeInit(PCD_HandleTypeDef *hpcd)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hpcd);

  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_PCD_MspDeInit could be implemented in the user file
   */
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
/**
  * @brief  Register a User USB PCD Callback
  *         To be used instead of the weak predefined callback
  * @param  hpcd USB PCD handle
  * @param  CallbackID ID of the callback to be registered
  *         This parameter can be one of the following values:
  *          @arg @ref HAL_PCD_SOF_CB_ID USB PCD SOF callback ID
  *          @arg @ref HAL_PCD_SETUPSTAGE_CB_ID USB PCD Setup callback ID
  *          @arg @ref HAL_PCD_RESET_CB_ID USB PCD Reset callback ID
  *          @arg @ref HAL_PCD_SUSPEND_CB_ID USB PCD Suspend callback ID
  *          @arg @ref HAL_PCD_RESUME_CB_ID USB PCD Resume callback ID
  *          @arg @ref HAL_PCD_CONNECT_CB_ID USB PCD Connect callback ID
  *          @arg @ref HAL_PCD_DISCONNECT_CB_ID USB PCD Disconnect callback ID
  *          @arg @ref HAL_PCD_MSPINIT_CB_ID MspDeInit callback ID
  *          @arg @ref HAL_PCD_MSPDEINIT_CB_ID MspDeInit callback ID
  * @param  pCallback pointer to the Callback function
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_RegisterCallback(PCD_HandleTypeDef *hpcd,
                                           HAL_PCD_CallbackIDTypeDef CallbackID,
                                           pPCD_CallbackTypeDef pCallback)
{
  HAL_StatusTypeDef status = HAL_OK;

  if (pCallback == NULL)
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;
    return HAL_ERROR;
  }
  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    switch (CallbackID)
    {
      case HAL_PCD_SOF_CB_ID :
        hpcd->SOFCallback = pCallback;
        break;

      case HAL_PCD_SETUPSTAGE_CB_ID :
        hpcd->SetupStageCallback = pCallback;
        break;

      case HAL_PCD_RESET_CB_ID :
        hpcd->ResetCallback = pCallback;
        break;

      case HAL_PCD_SUSPEND_CB_ID :
        hpcd->SuspendCallback = pCallback;
        break;

      case HAL_PCD_RESUME_CB_ID :
        hpcd->ResumeCallback = pCallback;
        break;

      case HAL_PCD_CONNECT_CB_ID :
        hpcd->ConnectCallback = pCallback;
        break;

      case HAL_PCD_DISCONNECT_CB_ID :
        hpcd->DisconnectCallback = pCallback;
        break;

      case HAL_PCD_MSPINIT_CB_ID :
        hpcd->MspInitCallback = pCallback;
        break;

      case HAL_PCD_MSPDEINIT_CB_ID :
        hpcd->MspDeInitCallback = pCallback;
        break;

      default :
        /* Update the error code */
        hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;
        /* Return error status */
        status =  HAL_ERROR;
        break;
    }
  }
  else if (hpcd->State == HAL_PCD_STATE_RESET)
  {
    switch (CallbackID)
    {
      case HAL_PCD_MSPINIT_CB_ID :
        hpcd->MspInitCallback = pCallback;
        break;

      case HAL_PCD_MSPDEINIT_CB_ID :
        hpcd->MspDeInitCallback = pCallback;
        break;

      default :
        /* Update the error code */
        hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;
        /* Return error status */
        status =  HAL_ERROR;
        break;
    }
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;
    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);
  return status;
}

/**
  * @brief  Unregister an USB PCD Callback
  *         USB PCD callback is redirected to the weak predefined callback
  * @param  hpcd USB PCD handle
  * @param  CallbackID ID of the callback to be unregistered
  *         This parameter can be one of the following values:
  *          @arg @ref HAL_PCD_SOF_CB_ID USB PCD SOF callback ID
  *          @arg @ref HAL_PCD_SETUPSTAGE_CB_ID USB PCD Setup callback ID
  *          @arg @ref HAL_PCD_RESET_CB_ID USB PCD Reset callback ID
  *          @arg @ref HAL_PCD_SUSPEND_CB_ID USB PCD Suspend callback ID
  *          @arg @ref HAL_PCD_RESUME_CB_ID USB PCD Resume callback ID
  *          @arg @ref HAL_PCD_CONNECT_CB_ID USB PCD Connect callback ID
  *          @arg @ref HAL_PCD_DISCONNECT_CB_ID USB PCD Disconnect callback ID
  *          @arg @ref HAL_PCD_MSPINIT_CB_ID MspDeInit callback ID
  *          @arg @ref HAL_PCD_MSPDEINIT_CB_ID MspDeInit callback ID
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_UnRegisterCallback(PCD_HandleTypeDef *hpcd, HAL_PCD_CallbackIDTypeDef CallbackID)
{
  HAL_StatusTypeDef status = HAL_OK;

  /* Process locked */
  __HAL_LOCK(hpcd);

  /* Setup Legacy weak Callbacks  */
  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    switch (CallbackID)
    {
      case HAL_PCD_SOF_CB_ID :
        hpcd->SOFCallback = HAL_PCD_SOFCallback;
        break;

      case HAL_PCD_SETUPSTAGE_CB_ID :
        hpcd->SetupStageCallback = HAL_PCD_SetupStageCallback;
        break;

      case HAL_PCD_RESET_CB_ID :
        hpcd->ResetCallback = HAL_PCD_ResetCallback;
        break;

      case HAL_PCD_SUSPEND_CB_ID :
        hpcd->SuspendCallback = HAL_PCD_SuspendCallback;
        break;

      case HAL_PCD_RESUME_CB_ID :
        hpcd->ResumeCallback = HAL_PCD_ResumeCallback;
        break;

      case HAL_PCD_CONNECT_CB_ID :
        hpcd->ConnectCallback = HAL_PCD_ConnectCallback;
        break;

      case HAL_PCD_DISCONNECT_CB_ID :
        hpcd->DisconnectCallback = HAL_PCD_DisconnectCallback;
        break;

      case HAL_PCD_MSPINIT_CB_ID :
        hpcd->MspInitCallback = HAL_PCD_MspInit;
        break;

      case HAL_PCD_MSPDEINIT_CB_ID :
        hpcd->MspDeInitCallback = HAL_PCD_MspDeInit;
        break;

      default :
        /* Update the error code */
        hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

        /* Return error status */
        status =  HAL_ERROR;
        break;
    }
  }
  else if (hpcd->State == HAL_PCD_STATE_RESET)
  {
    switch (CallbackID)
    {
      case HAL_PCD_MSPINIT_CB_ID :
        hpcd->MspInitCallback = HAL_PCD_MspInit;
        break;

      case HAL_PCD_MSPDEINIT_CB_ID :
        hpcd->MspDeInitCallback = HAL_PCD_MspDeInit;
        break;

      default :
        /* Update the error code */
        hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

        /* Return error status */
        status =  HAL_ERROR;
        break;
    }
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);
  return status;
}

/**
  * @brief  Register USB PCD Data OUT Stage Callback
  *         To be used instead of the weak HAL_PCD_DataOutStageCallback() predefined callback
  * @param  hpcd PCD handle
  * @param  pCallback pointer to the USB PCD Data OUT Stage Callback function
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_RegisterDataOutStageCallback(PCD_HandleTypeDef *hpcd,
                                                       pPCD_DataOutStageCallbackTypeDef pCallback)
{
  HAL_StatusTypeDef status = HAL_OK;

  if (pCallback == NULL)
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    return HAL_ERROR;
  }

  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    hpcd->DataOutStageCallback = pCallback;
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);

  return status;
}

/**
  * @brief  Unregister the USB PCD Data OUT Stage Callback
  *         USB PCD Data OUT Stage Callback is redirected to the weak HAL_PCD_DataOutStageCallback() predefined callback
  * @param  hpcd PCD handle
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_UnRegisterDataOutStageCallback(PCD_HandleTypeDef *hpcd)
{
  HAL_StatusTypeDef status = HAL_OK;

  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    hpcd->DataOutStageCallback = HAL_PCD_DataOutStageCallback; /* Legacy weak DataOutStageCallback  */
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);

  return status;
}

/**
  * @brief  Register USB PCD Data IN Stage Callback
  *         To be used instead of the weak HAL_PCD_DataInStageCallback() predefined callback
  * @param  hpcd PCD handle
  * @param  pCallback pointer to the USB PCD Data IN Stage Callback function
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_RegisterDataInStageCallback(PCD_HandleTypeDef *hpcd,
                                                      pPCD_DataInStageCallbackTypeDef pCallback)
{
  HAL_StatusTypeDef status = HAL_OK;

  if (pCallback == NULL)
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    return HAL_ERROR;
  }

  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    hpcd->DataInStageCallback = pCallback;
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);

  return status;
}

/**
  * @brief  Unregister the USB PCD Data IN Stage Callback
  *         USB PCD Data OUT Stage Callback is redirected to the weak HAL_PCD_DataInStageCallback() predefined callback
  * @param  hpcd PCD handle
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_UnRegisterDataInStageCallback(PCD_HandleTypeDef *hpcd)
{
  HAL_StatusTypeDef status = HAL_OK;

  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    hpcd->DataInStageCallback = HAL_PCD_DataInStageCallback; /* Legacy weak DataInStageCallback  */
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);

  return status;
}

/**
  * @brief  Register USB PCD Iso OUT incomplete Callback
  *         To be used instead of the weak HAL_PCD_ISOOUTIncompleteCallback() predefined callback
  * @param  hpcd PCD handle
  * @param  pCallback pointer to the USB PCD Iso OUT incomplete Callback function
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_RegisterIsoOutIncpltCallback(PCD_HandleTypeDef *hpcd,
                                                       pPCD_IsoOutIncpltCallbackTypeDef pCallback)
{
  HAL_StatusTypeDef status = HAL_OK;

  if (pCallback == NULL)
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    return HAL_ERROR;
  }

  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    hpcd->ISOOUTIncompleteCallback = pCallback;
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);

  return status;
}

/**
  * @brief  Unregister the USB PCD Iso OUT incomplete Callback
  *         USB PCD Iso OUT incomplete Callback is redirected
  *         to the weak HAL_PCD_ISOOUTIncompleteCallback() predefined callback
  * @param  hpcd PCD handle
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_UnRegisterIsoOutIncpltCallback(PCD_HandleTypeDef *hpcd)
{
  HAL_StatusTypeDef status = HAL_OK;

  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    hpcd->ISOOUTIncompleteCallback = HAL_PCD_ISOOUTIncompleteCallback; /* Legacy weak ISOOUTIncompleteCallback  */
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);

  return status;
}

/**
  * @brief  Register USB PCD Iso IN incomplete Callback
  *         To be used instead of the weak HAL_PCD_ISOINIncompleteCallback() predefined callback
  * @param  hpcd PCD handle
  * @param  pCallback pointer to the USB PCD Iso IN incomplete Callback function
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_RegisterIsoInIncpltCallback(PCD_HandleTypeDef *hpcd,
                                                      pPCD_IsoInIncpltCallbackTypeDef pCallback)
{
  HAL_StatusTypeDef status = HAL_OK;

  if (pCallback == NULL)
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    return HAL_ERROR;
  }

  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    hpcd->ISOINIncompleteCallback = pCallback;
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);

  return status;
}

/**
  * @brief  Unregister the USB PCD Iso IN incomplete Callback
  *         USB PCD Iso IN incomplete Callback is redirected
  *         to the weak HAL_PCD_ISOINIncompleteCallback() predefined callback
  * @param  hpcd PCD handle
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_UnRegisterIsoInIncpltCallback(PCD_HandleTypeDef *hpcd)
{
  HAL_StatusTypeDef status = HAL_OK;

  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    hpcd->ISOINIncompleteCallback = HAL_PCD_ISOINIncompleteCallback; /* Legacy weak ISOINIncompleteCallback  */
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);

  return status;
}

/**
  * @brief  Register USB PCD BCD Callback
  *         To be used instead of the weak HAL_PCDEx_BCD_Callback() predefined callback
  * @param  hpcd PCD handle
  * @param  pCallback pointer to the USB PCD BCD Callback function
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_RegisterBcdCallback(PCD_HandleTypeDef *hpcd, pPCD_BcdCallbackTypeDef pCallback)
{
  HAL_StatusTypeDef status = HAL_OK;

  if (pCallback == NULL)
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    return HAL_ERROR;
  }

  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    hpcd->BCDCallback = pCallback;
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);

  return status;
}

/**
  * @brief  Unregister the USB PCD BCD Callback
  *         USB BCD Callback is redirected to the weak HAL_PCDEx_BCD_Callback() predefined callback
  * @param  hpcd PCD handle
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_UnRegisterBcdCallback(PCD_HandleTypeDef *hpcd)
{
  HAL_StatusTypeDef status = HAL_OK;

  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    hpcd->BCDCallback = HAL_PCDEx_BCD_Callback; /* Legacy weak HAL_PCDEx_BCD_Callback  */
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);

  return status;
}

/**
  * @brief  Register USB PCD LPM Callback
  *         To be used instead of the weak HAL_PCDEx_LPM_Callback() predefined callback
  * @param  hpcd PCD handle
  * @param  pCallback pointer to the USB PCD LPM Callback function
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_RegisterLpmCallback(PCD_HandleTypeDef *hpcd, pPCD_LpmCallbackTypeDef pCallback)
{
  HAL_StatusTypeDef status = HAL_OK;

  if (pCallback == NULL)
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    return HAL_ERROR;
  }

  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    hpcd->LPMCallback = pCallback;
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);

  return status;
}

/**
  * @brief  Unregister the USB PCD LPM Callback
  *         USB LPM Callback is redirected to the weak HAL_PCDEx_LPM_Callback() predefined callback
  * @param  hpcd PCD handle
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_UnRegisterLpmCallback(PCD_HandleTypeDef *hpcd)
{
  HAL_StatusTypeDef status = HAL_OK;

  /* Process locked */
  __HAL_LOCK(hpcd);

  if (hpcd->State == HAL_PCD_STATE_READY)
  {
    hpcd->LPMCallback = HAL_PCDEx_LPM_Callback; /* Legacy weak HAL_PCDEx_LPM_Callback  */
  }
  else
  {
    /* Update the error code */
    hpcd->ErrorCode |= HAL_PCD_ERROR_INVALID_CALLBACK;

    /* Return error status */
    status =  HAL_ERROR;
  }

  /* Release Lock */
  __HAL_UNLOCK(hpcd);

  return status;
}
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */

/**
  * @}
  */

/** @defgroup PCD_Exported_Functions_Group2 Input and Output operation functions
  *  @brief   Data transfers functions
  *
@verbatim
 ===============================================================================
                      ##### IO operation functions #####
 ===============================================================================
    [..]
    This subsection provides a set of functions allowing to manage the PCD data
    transfers.

@endverbatim
  * @{
  */

/**
  * @brief  Start the USB device
  * @param  hpcd PCD handle
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_Start(PCD_HandleTypeDef *hpcd)
{
  USB_OTG_GlobalTypeDef *USBx = hpcd->Instance;

  __HAL_LOCK(hpcd);

  if (((USBx->GUSBCFG & USB_OTG_GUSBCFG_PHYSEL) != 0U) &&
      (hpcd->Init.battery_charging_enable == 1U))
  {
    /* Enable USB Transceiver */
    USBx->GCCFG |= USB_OTG_GCCFG_PWRDWN;
  }

  __HAL_PCD_ENABLE(hpcd);
  (void)USB_DevConnect(hpcd->Instance);
  __HAL_UNLOCK(hpcd);

  return HAL_OK;
}

#if defined (USB_OTG_FS) || defined (USB_OTG_HS)
/**
  * @brief  Handles PCD interrupt request.
  * @param  hpcd PCD handle
  * @retval HAL status
  */
/* Project: the identity owning a latched event; 0 outside the fixed records. */
static uint64_t PCD_LifeArm(const PCD_LifecycleTypeDef *life, uint32_t address)
{
  uint32_t epnum = address & EP_ADDR_MSK;
  if (epnum >= PCD_LIFE_ENDPOINTS) return 0U;
  return life->arm[epnum + (((address & 0x80U) != 0U) ? 0U : PCD_LIFE_ENDPOINTS)];
}

uint8_t HAL_PCD_ObserveReset(PCD_HandleTypeDef *hpcd)
{
  USB_OTG_GlobalTypeDef *USBx = hpcd->Instance;
  uint32_t USBx_BASE = (uint32_t)USBx;
  if (((USBx->GINTSTS & USB_OTG_GINTSTS_CMOD) != 0U) ||
      !__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_USBRST)) return 0U;
  USBx_DEVICE->DCTL &= ~USB_OTG_DCTL_RWUSIG;
  HAL_PCD_ResetBeginCallback(hpcd);
  __HAL_PCD_CLEAR_FLAG(hpcd, USB_OTG_GINTSTS_USBRST);
  return 1U;
}

/* Project: OTG captures facts, parses RX and completes eligible transfers.
 * Lifecycle cleanup (flush, disable, reset join) runs only in PendSV. */
void HAL_PCD_IRQHandler(PCD_HandleTypeDef *hpcd)
{
  USB_OTG_GlobalTypeDef *USBx = hpcd->Instance;
  uint32_t USBx_BASE = (uint32_t)USBx;
  PCD_LifecycleTypeDef *life = (PCD_LifecycleTypeDef *)hpcd->pData;
  uint32_t ep_intr;
  uint32_t epint;
  uint32_t epnum;
  uint32_t fifoemptymsk;
  uint32_t RegVal;

  /* ensure that we are in device mode */
  if ((USB_GetMode(hpcd->Instance) == USB_OTG_MODE_DEVICE) && (life != NULL))
  {
    /* A requested RX-empty barrier needs no pending source. */
    (void)HAL_PCD_LifecycleRxBarrier(life);

    /* avoid spurious interrupt */
    if (__HAL_PCD_IS_INVALID_INTERRUPT(hpcd))
    {
      return;
    }

    /* store current frame number */
    hpcd->FrameNumber = (USBx_DEVICE->DSTS & USB_OTG_DSTS_FNSOF_Msk) >> USB_OTG_DSTS_FNSOF_Pos;

    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_MMIS))
    {
      /* incorrect mode, acknowledge the interrupt */
      __HAL_PCD_CLEAR_FLAG(hpcd, USB_OTG_GINTSTS_MMIS);
    }

    /* Raw reset precedes endpoint dispatch: old facts never reach new arms. */
    (void)HAL_PCD_ObserveReset(hpcd);

    /* One finite parse budget per invocation, shared by both passes. */
    PCD_RxBudget budget = {PCD_LIFE_RX_STATUSES, PCD_LIFE_RX_BYTES};
#if defined(_USE_HW_VCOM)
    /* The static composite owns OUT3 and IN2/3. Defer only those events,
     * including RX FIFO movement, until EP0 and all HID endpoints have run.
     * The parser peeks before popping: a head of the other pass waits, so EP0
     * RX/SETUP/IN keep their original relative order. Queued CDC entries are
     * consumed in the second pass even while the class is gated.
     */
    const uint32_t passes = 2U;
#else
    const uint32_t passes = 1U;
#endif
    for (uint32_t cdc_pass = 0U; cdc_pass < passes; ++cdc_pass)
    {
#if defined(_USE_HW_VCOM)
      uint8_t rx_pass = (uint8_t)cdc_pass;
#else
      uint8_t rx_pass = 2U;
#endif
    /* Handle RxQLevel Interrupt: stop at any non-OK transaction. */
    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_RXFLVL))
    {
      PCD_RxEvent event;
      while (HAL_PCD_LifecycleRxStep(life, rx_pass, &budget, &event) == HAL_OK)
      {
      }
    }

    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_OEPINT))
    {
      epnum = 0U;

      /* Read in the device interrupt bits */
      ep_intr = USB_ReadDevAllOutEpInterrupt(hpcd->Instance);
#if defined(_USE_HW_VCOM)
      ep_intr &= cdc_pass ? 0x08U : ~0x08U;
#endif

      while (ep_intr != 0U)
      {
        if ((ep_intr & 0x1U) != 0U)
        {
          epint = USB_ReadDevOutEPInterrupt(hpcd->Instance, (uint8_t)epnum);
          /* An owner's completion/disable evidence is latched before W1C. */
          epint &= ~HAL_PCD_QuiesceObserve(life, (uint8_t)epnum, life->reset,
                                           PCD_LifeArm(life, epnum), epint);

          if ((epint & USB_OTG_DOEPINT_XFRC) == USB_OTG_DOEPINT_XFRC)
          {
            CLEAR_OUT_EP_INTR(epnum, USB_OTG_DOEPINT_XFRC);
            (void)PCD_EP_OutXfrComplete_int(hpcd, epnum);
          }

          if ((epint & USB_OTG_DOEPINT_STUP) == USB_OTG_DOEPINT_STUP)
          {
            CLEAR_OUT_EP_INTR(epnum, USB_OTG_DOEPINT_STUP);
            /* Class B setup phase done for previous decoded setup */
            (void)PCD_EP_OutSetupPacket_int(hpcd, epnum);
          }

          if ((epint & USB_OTG_DOEPINT_OTEPDIS) == USB_OTG_DOEPINT_OTEPDIS)
          {
            CLEAR_OUT_EP_INTR(epnum, USB_OTG_DOEPINT_OTEPDIS);
          }

          /* Unowned disable completion: acknowledge only, no CGONAK. */
          if ((epint & USB_OTG_DOEPINT_EPDISD) == USB_OTG_DOEPINT_EPDISD)
          {
            CLEAR_OUT_EP_INTR(epnum, USB_OTG_DOEPINT_EPDISD);
          }

          /* Clear Status Phase Received interrupt */
          if ((epint & USB_OTG_DOEPINT_OTEPSPR) == USB_OTG_DOEPINT_OTEPSPR)
          {
            CLEAR_OUT_EP_INTR(epnum, USB_OTG_DOEPINT_OTEPSPR);
          }

          /* Clear OUT NAK interrupt */
          if ((epint & USB_OTG_DOEPINT_NAK) == USB_OTG_DOEPINT_NAK)
          {
            CLEAR_OUT_EP_INTR(epnum, USB_OTG_DOEPINT_NAK);
          }
        }
        epnum++;
        ep_intr >>= 1U;
      }
    }

    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_IEPINT))
    {
      /* Read in the device interrupt bits */
      ep_intr = USB_ReadDevAllInEpInterrupt(hpcd->Instance);
#if defined(_USE_HW_VCOM)
      ep_intr &= cdc_pass ? 0x0cU : ~0x0cU;
#endif

      epnum = 0U;

      while (ep_intr != 0U)
      {
        if ((ep_intr & 0x1U) != 0U) /* In ITR */
        {
          epint = USB_ReadDevInEPInterrupt(hpcd->Instance, (uint8_t)epnum);
          /* Owned XFRC/EPDISD are latched before W1C or any completion
           * callback; INEPNE is always retired here, owned or not. */
          epint &= ~HAL_PCD_QuiesceObserve(life, (uint8_t)(epnum | 0x80U), life->reset,
                                           PCD_LifeArm(life, epnum | 0x80U), epint);

          if ((epint & USB_OTG_DIEPINT_XFRC) == USB_OTG_DIEPINT_XFRC)
          {
            fifoemptymsk = (uint32_t)(0x1UL << (epnum & EP_ADDR_MSK));
            USBx_DEVICE->DIEPEMPMSK &= ~fifoemptymsk;

            CLEAR_IN_EP_INTR(epnum, USB_OTG_DIEPINT_XFRC);

            if (hpcd->Init.dma_enable == 1U)
            {
              hpcd->IN_ep[epnum].xfer_buff += hpcd->IN_ep[epnum].maxpacket;

              /* this is ZLP, so prepare EP0 for next setup */
              if ((epnum == 0U) && (hpcd->IN_ep[epnum].xfer_len == 0U))
              {
                /* prepare to rx more setup packets */
                (void)USB_EP0_OutStart(hpcd->Instance, 1U, (uint8_t *)hpcd->Setup);
              }
            }

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
            hpcd->DataInStageCallback(hpcd, (uint8_t)epnum);
#else
            HAL_PCD_DataInStageCallback(hpcd, (uint8_t)epnum);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
          }
          if ((epint & USB_OTG_DIEPINT_TOC) == USB_OTG_DIEPINT_TOC)
          {
            CLEAR_IN_EP_INTR(epnum, USB_OTG_DIEPINT_TOC);
          }
          if ((epint & USB_OTG_DIEPINT_ITTXFE) == USB_OTG_DIEPINT_ITTXFE)
          {
            CLEAR_IN_EP_INTR(epnum, USB_OTG_DIEPINT_ITTXFE);
          }
          /* Unowned disable completion: acknowledge only, never flush here. */
          if ((epint & USB_OTG_DIEPINT_EPDISD) == USB_OTG_DIEPINT_EPDISD)
          {
            CLEAR_IN_EP_INTR(epnum, USB_OTG_DIEPINT_EPDISD);
          }
          if ((epint & USB_OTG_DIEPINT_TXFE) == USB_OTG_DIEPINT_TXFE)
          {
            (void)PCD_WriteEmptyTxFifo(hpcd, epnum);
          }
        }
        epnum++;
        ep_intr >>= 1U;
      }
    }

    }

    /* Handle Resume Interrupt */
    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_WKUINT))
    {
      /* Clear the Remote Wake-up Signaling */
      USBx_DEVICE->DCTL &= ~USB_OTG_DCTL_RWUSIG;

      if (hpcd->LPM_State == LPM_L1)
      {
        hpcd->LPM_State = LPM_L0;

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
        hpcd->LPMCallback(hpcd, PCD_LPM_L0_ACTIVE);
#else
        HAL_PCDEx_LPM_Callback(hpcd, PCD_LPM_L0_ACTIVE);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
      }
      else
      {
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
        hpcd->ResumeCallback(hpcd);
#else
        HAL_PCD_ResumeCallback(hpcd);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
      }

      __HAL_PCD_CLEAR_FLAG(hpcd, USB_OTG_GINTSTS_WKUINT);
    }

    /* Handle Suspend Interrupt */
    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_USBSUSP))
    {
      if ((USBx_DEVICE->DSTS & USB_OTG_DSTS_SUSPSTS) == USB_OTG_DSTS_SUSPSTS)
      {
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
        hpcd->SuspendCallback(hpcd);
#else
        HAL_PCD_SuspendCallback(hpcd);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
      }
      __HAL_PCD_CLEAR_FLAG(hpcd, USB_OTG_GINTSTS_USBSUSP);
    }

    /* Handle LPM Interrupt */
    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_LPMINT))
    {
      __HAL_PCD_CLEAR_FLAG(hpcd, USB_OTG_GINTSTS_LPMINT);

      if (hpcd->LPM_State == LPM_L0)
      {
        hpcd->LPM_State = LPM_L1;
        hpcd->BESL = (hpcd->Instance->GLPMCFG & USB_OTG_GLPMCFG_BESL) >> 2U;

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
        hpcd->LPMCallback(hpcd, PCD_LPM_L1_ACTIVE);
#else
        HAL_PCDEx_LPM_Callback(hpcd, PCD_LPM_L1_ACTIVE);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
      }
      else
      {
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
        hpcd->SuspendCallback(hpcd);
#else
        HAL_PCD_SuspendCallback(hpcd);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
      }
    }

    /* A raw reset raised during this invocation is consumed with fresh flags.
     * Its flush/register/EP0 sequence is the PendSV reset join. */
    (void)HAL_PCD_ObserveReset(hpcd);

    /* ENUMDNE only latches speed and reset identity; the join applies them. */
    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_ENUMDNE))
    {
      HAL_PCD_LifecycleEnumDone(life);
      __HAL_PCD_CLEAR_FLAG(hpcd, USB_OTG_GINTSTS_ENUMDNE);
    }

    /* Handle SOF Interrupt. The SOF callback, direct or registered, owns the
     * flag clear: every SOF callback, including one registered later, must
     * clear it with PRIMASK held (the port callback also masks SOF in that
     * section). */
    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_SOF))
    {
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
      hpcd->SOFCallback(hpcd);
#else
      HAL_PCD_SOFCallback(hpcd);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
    }

    /* Global OUT NAK effective: latch it for its ticket, then mask the
     * notification. Only the acquiring owner releases NAK with CGONAK. */
    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_BOUTNAKEFF))
    {
      (void)HAL_PCD_LifecycleObserveOutNak(life, life->reset, life->nak_operation,
                                           USBx->GINTSTS);
      /* Every post-init GINTMSK RMW holds PRIMASK (SOF demand owner). */
      uint32_t irq = __get_PRIMASK();
      __disable_irq();
      USBx->GINTMSK &= ~USB_OTG_GINTMSK_GONAKEFFM;
      __set_PRIMASK(irq);
    }

    /* No isochronous endpoint exists; incomplete-ISO events fail closed. */
    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_IISOIXFR))
    {
      __HAL_PCD_CLEAR_FLAG(hpcd, USB_OTG_GINTSTS_IISOIXFR);
    }
    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_PXFR_INCOMPISOOUT))
    {
      __HAL_PCD_CLEAR_FLAG(hpcd, USB_OTG_GINTSTS_PXFR_INCOMPISOOUT);
    }

    /* Handle Connection event Interrupt */
    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_SRQINT))
    {
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
      hpcd->ConnectCallback(hpcd);
#else
      HAL_PCD_ConnectCallback(hpcd);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */

      __HAL_PCD_CLEAR_FLAG(hpcd, USB_OTG_GINTSTS_SRQINT);
    }

    /* Handle Disconnection event Interrupt */
    if (__HAL_PCD_GET_FLAG(hpcd, USB_OTG_GINTSTS_OTGINT))
    {
      RegVal = hpcd->Instance->GOTGINT;

      if ((RegVal & USB_OTG_GOTGINT_SEDET) == USB_OTG_GOTGINT_SEDET)
      {
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
        hpcd->DisconnectCallback(hpcd);
#else
        HAL_PCD_DisconnectCallback(hpcd);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
      }
      hpcd->Instance->GOTGINT |= RegVal;
    }
  }
}
#endif /* defined (USB_OTG_FS) || defined (USB_OTG_HS) */


/**
  * @brief  Data OUT stage callback.
  * @param  hpcd PCD handle
  * @param  epnum endpoint number
  * @retval None
  */
__weak void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hpcd);
  UNUSED(epnum);

  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_PCD_DataOutStageCallback could be implemented in the user file
   */
}

/**
  * @brief  Data IN stage callback
  * @param  hpcd PCD handle
  * @param  epnum endpoint number
  * @retval None
  */
__weak void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hpcd);
  UNUSED(epnum);

  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_PCD_DataInStageCallback could be implemented in the user file
   */
}
/**
  * @brief  Setup stage callback
  * @param  hpcd PCD handle
  * @retval None
  */
__weak void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hpcd);

  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_PCD_SetupStageCallback could be implemented in the user file
   */
}

/**
  * @brief  USB Start Of Frame callback.
  * @param  hpcd PCD handle
  * @retval None
  */
__weak void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
{
  /* NOTE : Unlike ST's template, this default clears the SOF flag:
            HAL_PCD_IRQHandler's SOF branch leaves the clear to the SOF
            callback, so a callback implemented in the user file must clear
            it too, with PRIMASK held.
   */
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  __HAL_PCD_CLEAR_FLAG(hpcd, USB_OTG_GINTSTS_SOF);
  __set_PRIMASK(irq);
}

/**
  * @brief  USB bus reset beginning callback, before enumeration completion.
  * @param  hpcd PCD handle
  * @retval None
  */
__weak void HAL_PCD_ResetBeginCallback(PCD_HandleTypeDef *hpcd)
{
  UNUSED(hpcd);
}

/**
  * @brief  USB Reset callback.
  * @param  hpcd PCD handle
  * @retval None
  */
__weak void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hpcd);

  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_PCD_ResetCallback could be implemented in the user file
   */
}

/**
  * @brief  Suspend event callback.
  * @param  hpcd PCD handle
  * @retval None
  */
__weak void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hpcd);

  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_PCD_SuspendCallback could be implemented in the user file
   */
}

/**
  * @brief  Resume event callback.
  * @param  hpcd PCD handle
  * @retval None
  */
__weak void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hpcd);

  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_PCD_ResumeCallback could be implemented in the user file
   */
}

/**
  * @brief  Incomplete ISO OUT callback.
  * @param  hpcd PCD handle
  * @param  epnum endpoint number
  * @retval None
  */
__weak void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hpcd);
  UNUSED(epnum);

  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_PCD_ISOOUTIncompleteCallback could be implemented in the user file
   */
}

/**
  * @brief  Incomplete ISO IN callback.
  * @param  hpcd PCD handle
  * @param  epnum endpoint number
  * @retval None
  */
__weak void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hpcd);
  UNUSED(epnum);

  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_PCD_ISOINIncompleteCallback could be implemented in the user file
   */
}

/**
  * @brief  Connection event callback.
  * @param  hpcd PCD handle
  * @retval None
  */
__weak void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hpcd);

  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_PCD_ConnectCallback could be implemented in the user file
   */
}

/**
  * @brief  Disconnection event callback.
  * @param  hpcd PCD handle
  * @retval None
  */
__weak void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hpcd);

  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_PCD_DisconnectCallback could be implemented in the user file
   */
}

/**
  * @}
  */

/** @defgroup PCD_Exported_Functions_Group3 Peripheral Control functions
  *  @brief   management functions
  *
@verbatim
 ===============================================================================
                      ##### Peripheral Control functions #####
 ===============================================================================
    [..]
    This subsection provides a set of functions allowing to control the PCD data
    transfers.

@endverbatim
  * @{
  */

/**
  * @brief  Connect the USB device
  * @param  hpcd PCD handle
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_DevConnect(PCD_HandleTypeDef *hpcd)
{
  USB_OTG_GlobalTypeDef *USBx = hpcd->Instance;

  __HAL_LOCK(hpcd);

  if (((USBx->GUSBCFG & USB_OTG_GUSBCFG_PHYSEL) != 0U) &&
      (hpcd->Init.battery_charging_enable == 1U))
  {
    /* Enable USB Transceiver */
    USBx->GCCFG |= USB_OTG_GCCFG_PWRDWN;
  }
  (void)USB_DevConnect(hpcd->Instance);
  __HAL_UNLOCK(hpcd);

  return HAL_OK;
}

/**
  * @brief  Disconnect the USB device.
  * @param  hpcd PCD handle
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_DevDisconnect(PCD_HandleTypeDef *hpcd)
{
  USB_OTG_GlobalTypeDef *USBx = hpcd->Instance;

  __HAL_LOCK(hpcd);
  (void)USB_DevDisconnect(hpcd->Instance);

  if (((USBx->GUSBCFG & USB_OTG_GUSBCFG_PHYSEL) != 0U) &&
      (hpcd->Init.battery_charging_enable == 1U))
  {
    /* Disable USB Transceiver */
    USBx->GCCFG &= ~(USB_OTG_GCCFG_PWRDWN);
  }

  __HAL_UNLOCK(hpcd);

  return HAL_OK;
}

/**
  * @brief  Set the USB Device address.
  * @param  hpcd PCD handle
  * @param  address new device address
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_SetAddress(PCD_HandleTypeDef *hpcd, uint8_t address)
{
  __HAL_LOCK(hpcd);
  hpcd->USB_Address = address;
  (void)USB_SetDevAddress(hpcd->Instance, address);
  __HAL_UNLOCK(hpcd);

  return HAL_OK;
}
/**
  * @brief  Open and configure an endpoint.
  * @param  hpcd PCD handle
  * @param  ep_addr endpoint address
  * @param  ep_mps endpoint max packet size
  * @param  ep_type endpoint type
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_EP_Open(PCD_HandleTypeDef *hpcd, uint8_t ep_addr,
                                  uint16_t ep_mps, uint8_t ep_type)
{
  HAL_StatusTypeDef  ret = HAL_OK;
  PCD_EPTypeDef *ep;

  if ((ep_addr & 0x80U) == 0x80U)
  {
    ep = &hpcd->IN_ep[ep_addr & EP_ADDR_MSK];
    ep->is_in = 1U;
  }
  else
  {
    ep = &hpcd->OUT_ep[ep_addr & EP_ADDR_MSK];
    ep->is_in = 0U;
  }

  ep->num = ep_addr & EP_ADDR_MSK;
  ep->maxpacket = ep_mps;
  ep->type = ep_type;

  if (ep->is_in != 0U)
  {
    /* Assign a Tx FIFO */
    ep->tx_fifo_num = ep->num;
  }

  /* Set initial data PID. */
  if (ep_type == EP_TYPE_BULK)
  {
    ep->data_pid_start = 0U;
  }

  __HAL_LOCK(hpcd);
  (void)USB_ActivateEndpoint(hpcd->Instance, ep);
  __HAL_UNLOCK(hpcd);

  return ret;
}

/**
  * @brief  Receive an amount of data.
  * @param  hpcd PCD handle
  * @param  ep_addr endpoint address
  * @param  pBuf pointer to the reception buffer
  * @param  len amount of data to be received
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_EP_Receive(PCD_HandleTypeDef *hpcd, uint8_t ep_addr, uint8_t *pBuf, uint32_t len)
{
  PCD_EPTypeDef *ep;

  ep = &hpcd->OUT_ep[ep_addr & EP_ADDR_MSK];

  /*setup and start the Xfer */
  ep->xfer_buff = pBuf;
  ep->xfer_len = len;
  ep->xfer_count = 0U;
  ep->is_in = 0U;
  ep->num = ep_addr & EP_ADDR_MSK;

  if (hpcd->Init.dma_enable == 1U)
  {
    ep->dma_addr = (uint32_t)pBuf;
  }

  (void)USB_EPStartXfer(hpcd->Instance, ep, (uint8_t)hpcd->Init.dma_enable);

  return HAL_OK;
}

/**
  * @brief  Get Received Data Size
  * @param  hpcd PCD handle
  * @param  ep_addr endpoint address
  * @retval Data Size
  */
uint32_t HAL_PCD_EP_GetRxCount(PCD_HandleTypeDef const *hpcd, uint8_t ep_addr)
{
  return hpcd->OUT_ep[ep_addr & EP_ADDR_MSK].xfer_count;
}
/* Set up and enable an IN transfer. The LL start never unmasks TXFE: each
 * caller owns that decision (deferred fill or fill-then-unmask). */
static void PCD_EP_StartIn(PCD_HandleTypeDef *hpcd, uint8_t ep_addr, uint8_t *pBuf, uint32_t len)
{
  PCD_EPTypeDef *ep;

  ep = &hpcd->IN_ep[ep_addr & EP_ADDR_MSK];

  /*setup and start the Xfer */
  ep->xfer_buff = pBuf;
  ep->xfer_len = len;
  ep->xfer_count = 0U;
  ep->is_in = 1U;
  ep->num = ep_addr & EP_ADDR_MSK;

  if (hpcd->Init.dma_enable == 1U)
  {
    ep->dma_addr = (uint32_t)pBuf;
  }

  (void)USB_EPStartXfer(hpcd->Instance, ep, (uint8_t)hpcd->Init.dma_enable);
}

/**
  * @brief  Send an amount of data
  * @note   Deferred fill: the TXFE interrupt writes every packet.
  * @param  hpcd PCD handle
  * @param  ep_addr endpoint address
  * @param  pBuf pointer to the transmission buffer
  * @param  len amount of data to be sent
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_EP_Transmit(PCD_HandleTypeDef *hpcd, uint8_t ep_addr, uint8_t *pBuf, uint32_t len)
{
  uint32_t USBx_BASE = (uint32_t)hpcd->Instance;
  PCD_EPTypeDef *ep = &hpcd->IN_ep[ep_addr & EP_ADDR_MSK];

  PCD_EP_StartIn(hpcd, ep_addr, pBuf, len);

  /* Enable the Tx FIFO Empty Interrupt for this EP */
  if ((hpcd->Init.dma_enable == 0U) && (ep->type != EP_TYPE_ISOC) && (ep->xfer_len > 0U))
  {
    USBx_DEVICE->DIEPEMPMSK |= 1UL << (ep->num & EP_ADDR_MSK);
  }

  return HAL_OK;
}

/**
  * @brief  Arm an interrupt IN packet and fill available FIFO space immediately.
  * @note   Opt-in non-DMA, non-EP0, single-packet path. FIFO writes do not
  *         release the caller's payload ownership before DataIn completion.
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_EP_TransmitReady(PCD_HandleTypeDef *hpcd, uint8_t ep_addr,
                                          uint8_t *pBuf, uint32_t len)
{
  uint32_t epnum = (uint32_t)ep_addr & EP_ADDR_MSK;
  if (hpcd == NULL || hpcd->Instance == NULL || (ep_addr & 0xF0U) != 0x80U ||
      epnum == 0U || epnum >= hpcd->Init.dev_endpoints || pBuf == NULL || len == 0U)
  {
    return HAL_ERROR;
  }

  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  PCD_EPTypeDef *ep = &hpcd->IN_ep[epnum];
  USB_OTG_GlobalTypeDef *USBx = hpcd->Instance;
  uint32_t USBx_BASE = (uint32_t)USBx;
  uint32_t control = USBx_INEP(epnum)->DIEPCTL;
  HAL_StatusTypeDef status = HAL_ERROR;

  if (hpcd->Init.dma_enable == 0U && ep->is_in == 1U && ep->num == epnum &&
      ep->type == EP_TYPE_INTR && len <= ep->maxpacket &&
      (control & USB_OTG_DIEPCTL_USBAEP) != 0U &&
      (control & USB_OTG_DIEPCTL_EPTYP) == ((uint32_t)EP_TYPE_INTR << 18))
  {
    // EPENA or an unconsumed completion/disable still owns the old packet.
    if (hpcd->Lock == HAL_LOCKED || (control & USB_OTG_DIEPCTL_EPENA) != 0U ||
        (USBx_INEP(epnum)->DIEPINT & (USB_OTG_DIEPINT_XFRC | USB_OTG_DIEPINT_EPDISD)) != 0U)
    {
      status = HAL_BUSY;
    }
    else
    {
      PCD_EP_StartIn(hpcd, ep_addr, pBuf, len);
      // With one packet this helper writes at most once. Unmasking TXFE only
      // for bytes it could not place keeps every TXFE entry productive.
      (void)PCD_WriteEmptyTxFifo(hpcd, epnum);
      if (ep->xfer_count < ep->xfer_len)
      {
        USBx_DEVICE->DIEPEMPMSK |= 1UL << epnum;
      }
      status = HAL_OK;
    }
  }

  __set_PRIMASK(irq);
  return status;
}

/**
  * @brief  Set a STALL condition over an endpoint
  * @param  hpcd PCD handle
  * @param  ep_addr endpoint address
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_EP_SetStall(PCD_HandleTypeDef *hpcd, uint8_t ep_addr)
{
  PCD_EPTypeDef *ep;

  if (((uint32_t)ep_addr & EP_ADDR_MSK) > hpcd->Init.dev_endpoints)
  {
    return HAL_ERROR;
  }

  if ((0x80U & ep_addr) == 0x80U)
  {
    ep = &hpcd->IN_ep[ep_addr & EP_ADDR_MSK];
    ep->is_in = 1U;
  }
  else
  {
    ep = &hpcd->OUT_ep[ep_addr];
    ep->is_in = 0U;
  }

  ep->is_stall = 1U;
  ep->num = ep_addr & EP_ADDR_MSK;

  __HAL_LOCK(hpcd);

  (void)USB_EPSetStall(hpcd->Instance, ep);

  if ((ep_addr & EP_ADDR_MSK) == 0U)
  {
    (void)USB_EP0_OutStart(hpcd->Instance, (uint8_t)hpcd->Init.dma_enable, (uint8_t *)hpcd->Setup);
  }

  __HAL_UNLOCK(hpcd);

  return HAL_OK;
}

/**
  * @brief  Clear a STALL condition over in an endpoint
  * @param  hpcd PCD handle
  * @param  ep_addr endpoint address
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_EP_ClrStall(PCD_HandleTypeDef *hpcd, uint8_t ep_addr)
{
  PCD_EPTypeDef *ep;

  if (((uint32_t)ep_addr & 0x0FU) > hpcd->Init.dev_endpoints)
  {
    return HAL_ERROR;
  }

  if ((0x80U & ep_addr) == 0x80U)
  {
    ep = &hpcd->IN_ep[ep_addr & EP_ADDR_MSK];
    ep->is_in = 1U;
  }
  else
  {
    ep = &hpcd->OUT_ep[ep_addr & EP_ADDR_MSK];
    ep->is_in = 0U;
  }

  ep->is_stall = 0U;
  ep->num = ep_addr & EP_ADDR_MSK;

  __HAL_LOCK(hpcd);
  (void)USB_EPClearStall(hpcd->Instance, ep);
  __HAL_UNLOCK(hpcd);

  return HAL_OK;
}

/**
  * @brief  Activate remote wakeup signalling
  * @param  hpcd PCD handle
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_ActivateRemoteWakeup(PCD_HandleTypeDef *hpcd)
{
  return (USB_ActivateRemoteWakeup(hpcd->Instance));
}

/**
  * @brief  De-activate remote wakeup signalling.
  * @param  hpcd PCD handle
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_DeActivateRemoteWakeup(PCD_HandleTypeDef *hpcd)
{
  return (USB_DeActivateRemoteWakeup(hpcd->Instance));
}

/**
  * @}
  */

/** @defgroup PCD_Exported_Functions_Group4 Peripheral State functions
  *  @brief   Peripheral State functions
  *
@verbatim
 ===============================================================================
                      ##### Peripheral State functions #####
 ===============================================================================
    [..]
    This subsection permits to get in run-time the status of the peripheral
    and the data flow.

@endverbatim
  * @{
  */

/**
  * @brief  Return the PCD handle state.
  * @param  hpcd PCD handle
  * @retval HAL state
  */
PCD_StateTypeDef HAL_PCD_GetState(PCD_HandleTypeDef const *hpcd)
{
  return hpcd->State;
}

#if defined (USB_OTG_FS) || defined (USB_OTG_HS)
/**
  * @brief  Set the USB Device high speed test mode.
  * @param  hpcd PCD handle
  * @param  testmode USB Device high speed test mode
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_PCD_SetTestMode(const PCD_HandleTypeDef *hpcd, uint8_t testmode)
{
  const USB_OTG_GlobalTypeDef *USBx = hpcd->Instance;
  uint32_t USBx_BASE = (uint32_t)USBx;

  switch (testmode)
  {
    case TEST_J:
    case TEST_K:
    case TEST_SE0_NAK:
    case TEST_PACKET:
    case TEST_FORCE_EN:
      USBx_DEVICE->DCTL |= (uint32_t)testmode << 4;
      break;

    default:
      break;
  }

  return HAL_OK;
}
#endif /* defined (USB_OTG_FS) || defined (USB_OTG_HS) */
/**
  * @}
  */

/**
  * @}
  */

/* Private functions ---------------------------------------------------------*/
/** @addtogroup PCD_Private_Functions
  * @{
  */
#if defined (USB_OTG_FS) || defined (USB_OTG_HS)
/**
  * @brief  Check FIFO for the next packet to be loaded.
  * @param  hpcd PCD handle
  * @param  epnum endpoint number
  * @retval HAL status
  */
static HAL_StatusTypeDef PCD_WriteEmptyTxFifo(PCD_HandleTypeDef *hpcd, uint32_t epnum)
{
  USB_OTG_GlobalTypeDef *USBx = hpcd->Instance;
  uint32_t USBx_BASE = (uint32_t)USBx;
  USB_OTG_EPTypeDef *ep;
  uint32_t len;
  uint32_t len32b;
  uint32_t fifoemptymsk;

  ep = &hpcd->IN_ep[epnum];

  if (ep->xfer_count > ep->xfer_len)
  {
    return HAL_ERROR;
  }

  len = ep->xfer_len - ep->xfer_count;

  if (len > ep->maxpacket)
  {
    len = ep->maxpacket;
  }

  len32b = (len + 3U) / 4U;

  while (((USBx_INEP(epnum)->DTXFSTS & USB_OTG_DTXFSTS_INEPTFSAV) >= len32b) &&
         (ep->xfer_count < ep->xfer_len) && (ep->xfer_len != 0U))
  {
    /* Write the FIFO */
    len = ep->xfer_len - ep->xfer_count;

    if (len > ep->maxpacket)
    {
      len = ep->maxpacket;
    }
    len32b = (len + 3U) / 4U;

    (void)USB_WritePacket(USBx, ep->xfer_buff, (uint8_t)epnum, (uint16_t)len,
                          (uint8_t)hpcd->Init.dma_enable);

    ep->xfer_buff  += len;
    ep->xfer_count += len;
  }

  if (ep->xfer_len <= ep->xfer_count)
  {
    fifoemptymsk = (uint32_t)(0x1UL << (epnum & EP_ADDR_MSK));
    USBx_DEVICE->DIEPEMPMSK &= ~fifoemptymsk;
  }

  return HAL_OK;
}


/**
  * @brief  process EP OUT transfer complete interrupt.
  * @param  hpcd PCD handle
  * @param  epnum endpoint number
  * @retval HAL status
  */
static HAL_StatusTypeDef PCD_EP_OutXfrComplete_int(PCD_HandleTypeDef *hpcd, uint32_t epnum)
{
  USB_OTG_EPTypeDef *ep;
  const USB_OTG_GlobalTypeDef *USBx = hpcd->Instance;
  uint32_t USBx_BASE = (uint32_t)USBx;
  uint32_t gSNPSiD = *(__IO const uint32_t *)(&USBx->CID + 0x1U);
  uint32_t DoepintReg = USBx_OUTEP(epnum)->DOEPINT;

  if (hpcd->Init.dma_enable == 1U)
  {
    if ((DoepintReg & USB_OTG_DOEPINT_STUP) == USB_OTG_DOEPINT_STUP) /* Class C */
    {
      /* StupPktRcvd = 1 this is a setup packet */
      if ((gSNPSiD > USB_OTG_CORE_ID_300A) &&
          ((DoepintReg & USB_OTG_DOEPINT_STPKTRX) == USB_OTG_DOEPINT_STPKTRX))
      {
        CLEAR_OUT_EP_INTR(epnum, USB_OTG_DOEPINT_STPKTRX);
      }
    }
    else if ((DoepintReg & USB_OTG_DOEPINT_OTEPSPR) == USB_OTG_DOEPINT_OTEPSPR) /* Class E */
    {
      CLEAR_OUT_EP_INTR(epnum, USB_OTG_DOEPINT_OTEPSPR);
    }
    else if ((DoepintReg & (USB_OTG_DOEPINT_STUP | USB_OTG_DOEPINT_OTEPSPR)) == 0U)
    {
      /* StupPktRcvd = 1 this is a setup packet */
      if ((gSNPSiD > USB_OTG_CORE_ID_300A) &&
          ((DoepintReg & USB_OTG_DOEPINT_STPKTRX) == USB_OTG_DOEPINT_STPKTRX))
      {
        CLEAR_OUT_EP_INTR(epnum, USB_OTG_DOEPINT_STPKTRX);
      }
      else
      {
        ep = &hpcd->OUT_ep[epnum];

        /* out data packet received over EP */
        ep->xfer_count = ep->xfer_size - (USBx_OUTEP(epnum)->DOEPTSIZ & USB_OTG_DOEPTSIZ_XFRSIZ);

        if (epnum == 0U)
        {
          if (ep->xfer_len == 0U)
          {
            /* this is ZLP, so prepare EP0 for next setup */
            (void)USB_EP0_OutStart(hpcd->Instance, 1U, (uint8_t *)hpcd->Setup);
          }
          else
          {
            ep->xfer_buff += ep->xfer_count;
          }
        }

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
        hpcd->DataOutStageCallback(hpcd, (uint8_t)epnum);
#else
        HAL_PCD_DataOutStageCallback(hpcd, (uint8_t)epnum);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
      }
    }
    else
    {
      /* ... */
    }
  }
  else
  {
    if (gSNPSiD == USB_OTG_CORE_ID_310A)
    {
      /* StupPktRcvd = 1 this is a setup packet */
      if ((DoepintReg & USB_OTG_DOEPINT_STPKTRX) == USB_OTG_DOEPINT_STPKTRX)
      {
        CLEAR_OUT_EP_INTR(epnum, USB_OTG_DOEPINT_STPKTRX);
      }
      else
      {
        if ((DoepintReg & USB_OTG_DOEPINT_OTEPSPR) == USB_OTG_DOEPINT_OTEPSPR)
        {
          CLEAR_OUT_EP_INTR(epnum, USB_OTG_DOEPINT_OTEPSPR);
        }

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
        hpcd->DataOutStageCallback(hpcd, (uint8_t)epnum);
#else
        HAL_PCD_DataOutStageCallback(hpcd, (uint8_t)epnum);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
      }
    }
    else
    {
      if ((epnum == 0U) && (hpcd->OUT_ep[epnum].xfer_len == 0U))
      {
        /* this is ZLP, so prepare EP0 for next setup */
        (void)USB_EP0_OutStart(hpcd->Instance, 0U, (uint8_t *)hpcd->Setup);
      }

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
      hpcd->DataOutStageCallback(hpcd, (uint8_t)epnum);
#else
      HAL_PCD_DataOutStageCallback(hpcd, (uint8_t)epnum);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
    }
  }

  return HAL_OK;
}


/**
  * @brief  process EP OUT setup packet received interrupt.
  * @param  hpcd PCD handle
  * @param  epnum endpoint number
  * @retval HAL status
  */
static HAL_StatusTypeDef PCD_EP_OutSetupPacket_int(PCD_HandleTypeDef *hpcd, uint32_t epnum)
{
  const USB_OTG_GlobalTypeDef *USBx = hpcd->Instance;
  uint32_t USBx_BASE = (uint32_t)USBx;
  uint32_t gSNPSiD = *(__IO const uint32_t *)(&USBx->CID + 0x1U);
  uint32_t DoepintReg = USBx_OUTEP(epnum)->DOEPINT;

  if ((gSNPSiD > USB_OTG_CORE_ID_300A) &&
      ((DoepintReg & USB_OTG_DOEPINT_STPKTRX) == USB_OTG_DOEPINT_STPKTRX))
  {
    CLEAR_OUT_EP_INTR(epnum, USB_OTG_DOEPINT_STPKTRX);
  }

  /* Project: dispatch only the SETUP this STUP completes, once. */
  if (HAL_PCD_LifecycleTakeSetup((PCD_LifecycleTypeDef *)hpcd->pData) == 0U)
  {
    return HAL_OK;
  }

  /* Inform the upper layer that a setup packet is available */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
  hpcd->SetupStageCallback(hpcd);
#else
  HAL_PCD_SetupStageCallback(hpcd);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */

  if ((gSNPSiD > USB_OTG_CORE_ID_300A) && (hpcd->Init.dma_enable == 1U))
  {
    (void)USB_EP0_OutStart(hpcd->Instance, 1U, (uint8_t *)hpcd->Setup);
  }

  return HAL_OK;
}
#endif /* defined (USB_OTG_FS) || defined (USB_OTG_HS) */


/**
  * @}
  */
#endif /* defined (USB_OTG_FS) || defined (USB_OTG_HS) */
#endif /* HAL_PCD_MODULE_ENABLED */

/**
  * @}
  */

/**
  * @}
  */
