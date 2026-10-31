/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @version        : v1.0_Cube
  * @brief          : Usb device for Virtual Com Port.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc_if.h"
#include "qbuffer.h"



const char *JUMP_BOOT_STR = "BOOT 5555AAAA";


USBD_CDC_LineCodingTypeDef LineCoding =
    {
        115200,
        0x00,
        0x00,
        0x08
    };


uint8_t CDC_Reset_Status = 0;
uint8_t UserRxBufferFS[APP_RX_DATA_SIZE + 1];
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE + 1];



static qbuffer_t q_rx;
static qbuffer_t q_tx;

static uint8_t q_rx_buf[2048];
static uint8_t q_tx_buf[2048];

static volatile bool is_opened = false;
static bool is_rx_full = false;
static uint32_t tx_pending_length;
static volatile uint32_t session_generation;
static uint8_t cdc_class_id;
static uint8_t cdc_type = 0;

extern USBD_HandleTypeDef USBD_Device;

static int8_t CDC_Init_FS(USBD_HandleTypeDef *pdev);
static int8_t CDC_DeInit_FS(USBD_HandleTypeDef *pdev);
static int8_t CDC_Control_FS(USBD_HandleTypeDef *pdev, uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(USBD_HandleTypeDef *pdev, uint8_t* pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(USBD_HandleTypeDef *pdev, uint8_t *pbuf, uint32_t *Len, uint8_t epnum);




USBD_CDC_ItfTypeDef USBD_CDC_fops =
{
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS,
  CDC_TransmitCplt_FS
};




static void cdcResetSession(void)
{
  is_opened = false;
  is_rx_full = false;
  tx_pending_length = 0U;
  session_generation++;
  qbufferFlush(&q_rx);
  qbufferFlush(&q_tx);
  LineCoding = (USBD_CDC_LineCodingTypeDef){115200U, 0U, 0U, 8U};
  CDC_Reset_Status = 0U;
  cdc_type = 0U;
}

bool cdcIfInit(void)
{
  qbufferCreate(&q_rx, q_rx_buf, sizeof(q_rx_buf));
  qbufferCreate(&q_tx, q_tx_buf, sizeof(q_tx_buf));
  cdcResetSession();
  return true;
}

uint32_t cdcIfAvailable(void)
{
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  uint32_t available = qbufferAvailable(&q_rx);
  __set_PRIMASK(irq);
  return available;
}

uint8_t cdcIfRead(void)
{
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  uint8_t ret = 0U;
  qbufferRead(&q_rx, &ret, 1U);
  __set_PRIMASK(irq);
  return ret;
}

uint32_t cdcIfWrite(uint8_t *p_data, uint32_t length)
{
  if (p_data == NULL) return 0U;
  uint32_t generation = session_generation;
  uint32_t sent_len = 0U;
  uint32_t pre_time = millis();
  while (sent_len < length)
  {
    // Reset may flush both indices in IRQ context. Publish only a bounded
    // chunk while it is excluded, and never resume this write in a new session.
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    if (generation != session_generation || !cdcIfIsConnected())
    {
      __set_PRIMASK(irq);
      break;
    }
    uint32_t buf_len = q_tx.len - qbufferAvailable(&q_tx) - 1U;
    uint32_t tx_len = length - sent_len;
    if (tx_len > buf_len) tx_len = buf_len;
    if (tx_len > 64U) tx_len = 64U;
    if (tx_len > 0U && qbufferWrite(&q_tx, p_data + sent_len, tx_len)) sent_len += tx_len;
    __set_PRIMASK(irq);
    if (millis() - pre_time >= 100U) break;
    if (tx_len == 0U)
    {
      if (irq != 0U || __get_IPSR() != 0U) break;
      delay(1);
    }
  }
  return sent_len;
}

uint32_t cdcIfGetBaud(void)
{
  return LineCoding.bitrate;
}

bool cdcIfIsConnected(void)
{
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  USBD_CDC_HandleTypeDef *hcdc = USBD_Device.pClassDataCmsit[cdc_class_id];
  bool connected = hcdc != NULL && hcdc->RxState != UINT32_MAX && is_opened &&
                   USBD_Device.dev_state == USBD_STATE_CONFIGURED && USBD_Device.dev_config != 0U;
  __set_PRIMASK(irq);
  return connected;
}

uint8_t cdcIfGetType(void)
{
  return cdc_type;
}

uint8_t CDC_SoF_ISR(struct _USBD_HandleTypeDef *pdev)
{
  USBD_CDC_HandleTypeDef *hcdc = pdev->pClassDataCmsit[pdev->classId];
  if (hcdc == NULL || hcdc->RxState == UINT32_MAX) return USBD_FAIL;
  if (is_rx_full)
  {
    uint32_t free_bytes = q_rx.len - qbufferAvailable(&q_rx) - 1U;
    if (free_bytes >= CDC_DATA_HS_MAX_PACKET_SIZE &&
        USBD_CDC_SetRxBuffer(pdev, UserRxBufferFS) == USBD_OK &&
        USBD_CDC_ReceivePacket(pdev) == USBD_OK) is_rx_full = false;
  }
  if (hcdc->TxState == 0U)
  {
    // Once dequeued, this buffer owns the bytes until the class accepts them.
    // A rejected arm must not let the next SOF replace the pending payload.
    if (tx_pending_length == 0U)
    {
      uint32_t tx_len = qbufferAvailable(&q_tx);
      if (tx_len > APP_TX_DATA_SIZE) tx_len = APP_TX_DATA_SIZE;
      if (tx_len > 0U && tx_len % CDC_DATA_HS_MAX_PACKET_SIZE == 0U) tx_len--;
      if (tx_len > 0U && qbufferRead(&q_tx, UserTxBufferFS, tx_len)) tx_pending_length = tx_len;
    }
    if (tx_pending_length > 0U)
    {
#ifdef USE_USBD_COMPOSITE
      if (USBD_CDC_SetTxBuffer(pdev, UserTxBufferFS, tx_pending_length, pdev->classId) == USBD_OK &&
          USBD_CDC_TransmitPacket(pdev, pdev->classId) == USBD_OK) tx_pending_length = 0U;
#else
      if (USBD_CDC_SetTxBuffer(pdev, UserTxBufferFS, tx_pending_length) == USBD_OK &&
          USBD_CDC_TransmitPacket(pdev) == USBD_OK) tx_pending_length = 0U;
#endif
    }
  }
  return USBD_OK;
}

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Initializes the CDC media low layer over the FS USB IP
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Init_FS(USBD_HandleTypeDef *pdev)
{
  cdc_class_id = pdev->classId;
  cdcResetSession();
  /* Set Application Buffers */
  #ifdef USE_USBD_COMPOSITE
  USBD_CDC_SetTxBuffer(pdev, UserTxBufferFS, 0, pdev->classId);
  USBD_CDC_SetRxBuffer(pdev, UserRxBufferFS);  
  #else
  USBD_CDC_SetTxBuffer(&USBD_Device, UserTxBufferFS, 0);
  USBD_CDC_SetRxBuffer(&USBD_Device, UserRxBufferFS);
  #endif
  is_opened = false;

  return (USBD_OK);
}

/**
  * @brief  DeInitializes the CDC media low layer
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_DeInit_FS(USBD_HandleTypeDef *pdev)
{
  // The class calls this only after every endpoint has released its buffers.
  cdcResetSession();

  return (USBD_OK);
}

/**
  * @brief  Manage the CDC class requests
  * @param  cmd: Command code
  * @param  pbuf: Buffer containing command data (request parameters)
  * @param  length: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_FS(USBD_HandleTypeDef *pdev, uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  USBD_SetupReqTypedef *req = (USBD_SetupReqTypedef *)pbuf;
  uint32_t bitrate;

  switch(cmd)
  {
    case CDC_SEND_ENCAPSULATED_COMMAND:

    break;

    case CDC_GET_ENCAPSULATED_RESPONSE:

    break;

    case CDC_SET_COMM_FEATURE:

    break;

    case CDC_GET_COMM_FEATURE:

    break;

    case CDC_CLEAR_COMM_FEATURE:

    break;

  /*******************************************************************************/
  /* Line Coding Structure                                                       */
  /*-----------------------------------------------------------------------------*/
  /* Offset | Field       | Size | Value  | Description                          */
  /* 0      | dwDTERate   |   4  | Number |Data terminal rate, in bits per second*/
  /* 4      | bCharFormat |   1  | Number | Stop bits                            */
  /*                                        0 - 1 Stop bit                       */
  /*                                        1 - 1.5 Stop bits                    */
  /*                                        2 - 2 Stop bits                      */
  /* 5      | bParityType |  1   | Number | Parity                               */
  /*                                        0 - None                             */
  /*                                        1 - Odd                              */
  /*                                        2 - Even                             */
  /*                                        3 - Mark                             */
  /*                                        4 - Space                            */
  /* 6      | bDataBits  |   1   | Number Data bits (5, 6, 7, 8 or 16).          */
  /*******************************************************************************/
    case CDC_SET_LINE_CODING:
      bitrate   = (uint32_t)(pbuf[0]);
      bitrate  |= (uint32_t)(pbuf[1]<<8);
      bitrate  |= (uint32_t)(pbuf[2]<<16);
      bitrate  |= (uint32_t)(pbuf[3]<<24);
      LineCoding.format    = pbuf[4];
      LineCoding.paritytype= pbuf[5];
      LineCoding.datatype  = pbuf[6];
      LineCoding.bitrate   = bitrate - (bitrate%10);

      if( LineCoding.bitrate == 1200 )
      {
        CDC_Reset_Status = 1;
      }
      if (LineCoding.bitrate == 115200)
        cdc_type = 1;
      else
        cdc_type = 0;
    break;

    case CDC_GET_LINE_CODING:
      bitrate = LineCoding.bitrate | cdc_type;

      pbuf[0] = (uint8_t)(bitrate);
      pbuf[1] = (uint8_t)(bitrate>>8);
      pbuf[2] = (uint8_t)(bitrate>>16);
      pbuf[3] = (uint8_t)(bitrate>>24);
      pbuf[4] = LineCoding.format;
      pbuf[5] = LineCoding.paritytype;
      pbuf[6] = LineCoding.datatype;
    break;

    case CDC_SET_CONTROL_LINE_STATE:
      // TODO : 나중에 다른 터미널에서 문제 없는지 확인 필요
      //is_opened = req->wValue&0x01;  // 0 bit:DTR, 1 bit:RTS
      if (req->wValue & 0x01)
        is_opened = true;
      else
        is_opened = false;
        
      //logPrintf("CDC_SET_CONTROL_LINE_STATE %X\n", req->wValue);
      // if (cdc_type == 0 && LineCoding.bitrate > 57600)
      // {
      //   esp32RequestBoot(req->wValue);
      // }
    break;

    case CDC_SEND_BREAK:

    break;

  default:
    break;
  }

  return (USBD_OK);
}

/**
  * @brief  Data received over USB OUT endpoint are sent over CDC interface
  *         through this function.
  *
  *         @note
  *         This function will issue a NAK packet on any OUT packet received on
  *         USB endpoint until exiting this function. If you exit this function
  *         before transfer is complete on CDC interface (ie. using DMA controller)
  *         it will result in receiving more data while previous ones are still
  *         not sent.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Receive_FS(USBD_HandleTypeDef *pdev, uint8_t* Buf, uint32_t *Len)
{
  uint32_t i;


  qbufferWrite(&q_rx, Buf, *Len);

  if( CDC_Reset_Status == 1 )
  {
    CDC_Reset_Status = 0;

    if( *Len >= 13 )
    {
      for(i=0; i<13; i++ )
      {
        if( JUMP_BOOT_STR[i] != Buf[i] ) break;
      }

      // if( i == 13 )
      // {
      //   resetToBoot(0);
      // }
    }
  }

  uint32_t buf_len;

  buf_len = (q_rx.len - qbufferAvailable(&q_rx)) - 1;

  if (buf_len >= CDC_DATA_HS_MAX_PACKET_SIZE)
  {
    is_rx_full = USBD_CDC_SetRxBuffer(pdev, Buf) != USBD_OK ||
                 USBD_CDC_ReceivePacket(pdev) != USBD_OK;
  }
  else
  {
    is_rx_full = true;
  }

  return (USBD_OK);
}

/**
  * @brief  CDC_Transmit_FS
  *         Data to send over USB IN endpoint are sent over CDC interface
  *         through this function.
  *         @note
  *
  *
  * @param  Buf: Buffer of data to be sent
  * @param  Len: Number of data to be sent (in bytes)
  * @retval USBD_OK if all operations are OK else USBD_FAIL or USBD_BUSY
  */
uint8_t CDC_Transmit_FS(USBD_HandleTypeDef *pdev, uint8_t* Buf, uint16_t Len)
{
  uint8_t result = USBD_OK;
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)pdev->pClassDataCmsit[pdev->classId];
  if (hcdc == NULL || hcdc->TxState != 0 || tx_pending_length != 0U){
    return USBD_BUSY;
  }
  #ifdef USE_USBD_COMPOSITE
  USBD_CDC_SetTxBuffer(pdev, Buf, Len, pdev->classId);
  result = USBD_CDC_TransmitPacket(pdev, pdev->classId);  
  #else
  USBD_CDC_SetTxBuffer(&USBD_Device, Buf, Len);
  result = USBD_CDC_TransmitPacket(&USBD_Device);
  #endif

  return result;
}

/**
  * @brief  CDC_TransmitCplt_FS
  *         Data transmitted callback
  *
  *         @note
  *         This function is IN transfer complete callback used to inform user that
  *         the submitted Data is successfully sent over USB.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_TransmitCplt_FS(USBD_HandleTypeDef *pdev, uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 13 */
  UNUSED(Buf);
  UNUSED(Len);
  UNUSED(epnum);
  /* USER CODE END 13 */
  return result;
}


