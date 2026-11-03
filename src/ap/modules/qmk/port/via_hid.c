#include "via_hid.h"
#include "raw_hid.h"
#include "via.h"
#include "dynamic_keymap.h"
#include "hw/driver/usb/usb_hid/hid_transport.h"

static struct {
  uint8_t packet[HID_VIA_EP_SIZE];
  uint32_t generation;
  enum { VIA_IDLE, VIA_PENDING, VIA_REPLY } phase;
  dynamic_macro_command_t macro;
  bool generation_known, upload_open;
} command;

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

dynamic_macro_result_t via_hid_macro_set(uint16_t offset, uint16_t size, const uint8_t *data)
{
  dynamic_macro_result_t result = dynamic_keymap_macro_set_begin(&command.macro, offset, size, data);
  if (result == DYNAMIC_MACRO_PENDING) {
    command.phase = VIA_PENDING;
    command.upload_open = true;
  }
  return result;
}

dynamic_macro_result_t via_hid_macro_reset(void)
{
  dynamic_macro_result_t result = dynamic_keymap_macro_reset_begin(&command.macro);
  command.phase = VIA_PENDING;
  command.upload_open = true;
  return result;
}

void via_hid_task(void)
{
  usb_hid_session_t session = usbHidGetSession();
  if (command.generation_known && (!session.valid || session.generation != command.generation)) {
    if (command.upload_open) eeprom_prepare_commit_cancel();
    command.upload_open = command.generation_known = false;
    command.phase = VIA_IDLE;
  }
  if (!session.valid) return;
  if (command.phase == VIA_IDLE) {
    uint32_t generation;
    if (!usbHidReadViaRequest(command.packet, &generation)) return;
    // An IRQ may retire/reconfigure after the session snapshot but before dequeue.
    if (command.generation_known && generation != command.generation && command.upload_open) {
      eeprom_prepare_commit_cancel();
      command.upload_open = false;
    }
    command.generation = generation;
    command.generation_known = true;
    command.phase = VIA_REPLY;
    raw_hid_receive(command.packet, sizeof(command.packet));
  }
  if (command.phase == VIA_PENDING) {
    dynamic_macro_result_t result = dynamic_keymap_macro_step(&command.macro, &command.packet[4]);
    if (result == DYNAMIC_MACRO_PENDING) return;
    if (result == DYNAMIC_MACRO_FAILED) command.packet[0] = id_unhandled;
    else if (command.macro.closes) command.upload_open = false;
    command.phase = VIA_REPLY;
  }
  if (usbHidEnqueueViaResponse(command.packet, sizeof(command.packet), command.generation))
    command.phase = VIA_IDLE;
}
