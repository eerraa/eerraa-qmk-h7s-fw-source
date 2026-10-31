#include "via_hid.h"
#include "raw_hid.h"
#include "hw/driver/usb/usb_hid/usbd_hid.h"

void via_hid_init(void)
{
  // V260909R1: USB class가 RX 큐와 bus generation을 함께 소유한다.
}

void raw_hid_send(uint8_t *data, uint8_t length)
{
  (void)data;
  (void)length;
  // V260821R1: VIA TX 생산자는 via_hid_task의 enqueue 하나다. 이 stub를 채우지 않는다.
}

void via_hid_task(void)
{
  uint8_t packet[HID_VIA_EP_SIZE];
  uint32_t generation;
  // V260909R1: 응답 credit을 확보한 한 명령만 처리한다. reset 중인 이전 세대 응답은 폐기한다.
  if (usbHidReadViaRequest(packet, &generation)) {
    raw_hid_receive(packet, sizeof(packet));
    (void)usbHidEnqueueViaResponse(packet, sizeof(packet), generation);
  }
}
