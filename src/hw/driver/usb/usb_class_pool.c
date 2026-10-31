#include "usbd_hid.h"
#include "usbd_cdc.h"
#include "hw_def.h"
#include <string.h>

// V260909R1: 한 device의 지원 class 수만큼만 고정 할당한다. free는 정확한 slot만 해제한다.
typedef union { USBD_CDC_HandleTypeDef cdc; USBD_HID_HandleTypeDef hid; uint32_t alignment; } usb_class_slot_t;
static usb_class_slot_t usb_class_slots[USBD_MAX_SUPPORTED_CLASS];
static bool usb_class_slot_used[USBD_MAX_SUPPORTED_CLASS];

void *USBD_static_malloc(uint32_t size)
{
  if (size == 0U || size > sizeof(usb_class_slot_t)) return NULL;
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  void *result = NULL;
  for (uint32_t i = 0; i < USBD_MAX_SUPPORTED_CLASS; i++) {
    if (!usb_class_slot_used[i]) {
      usb_class_slot_used[i] = true;
      memset(&usb_class_slots[i], 0, sizeof(usb_class_slots[i]));
      result = &usb_class_slots[i];
      break;
    }
  }
  __set_PRIMASK(irq);
  return result;
}

/**
  * @brief  Release an exact owned slot
  * @param  p: Pointer to allocated  memory address
  * @retval None
  */
void USBD_static_free(void *p)
{
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  for (uint32_t i = 0; i < USBD_MAX_SUPPORTED_CLASS; i++)
    if (p == &usb_class_slots[i]) usb_class_slot_used[i] = false;
  __set_PRIMASK(irq);
}

