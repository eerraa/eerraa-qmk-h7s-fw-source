#include "eeconfig.h"
#include "eeprom.h"
#include "qmk/port/port.h"
#include "hw_def.h"
#include "usb.h"
#include "qmk/port/debounce_profile.h"
#include "qmk/port/bootmode.h"


#if (EECONFIG_USER_DATA_SIZE) > 0
void eeconfig_init_user_datablock(void)
{
  uint8_t dummy_user[(EECONFIG_USER_DATA_SIZE)] = {0};
  eeconfig_update_user_datablock(dummy_user);
#ifdef BOOTMODE_ENABLE
  eeprom_update_dword((uint32_t *)EECONFIG_USER_BOOTMODE, USB_BOOT_MODE_DEFAULT_VALUE);
#endif
  // V260823R2: +32 레거시 monitor 슬롯은 예약만 하며 초기화하지 않는다.
  debounce_profile_storage_stage_defaults();
#ifdef G_TERM_ENABLE
  tapping_term_storage_stage_defaults();
#endif
#ifdef TAPDANCE_ENABLE
  tapdance_storage_stage_defaults();
#endif
#ifdef MOUSEKEY_ENABLE
  mousekey_config_storage_stage_defaults();
#endif
#ifdef RGBLIGHT_ENABLE
  rgb_sleep_storage_stage_defaults();
#endif
}

void eeconfig_publish_user_datablock(void)
{
#ifdef BOOTMODE_ENABLE
  bootmode_publish_defaults();
#endif
  debounce_profile_storage_publish();
#ifdef G_TERM_ENABLE
  tapping_term_init();
#endif
#ifdef TAPDANCE_ENABLE
  tapdance_init();
#endif
#ifdef MOUSEKEY_ENABLE
  mousekey_config_init();
#endif
#ifdef RGBLIGHT_ENABLE
  rgb_sleep_storage_publish();
#endif
}
#endif
