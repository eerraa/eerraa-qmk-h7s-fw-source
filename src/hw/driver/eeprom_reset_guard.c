#include "eeprom_reset_guard.h"

#include <stdint.h>

#include "hw_def.h"

#ifdef VIA_ENABLE
#include "log.h"
#include "eeprom.h"
#include "qmk/quantum/eeconfig.h"
#include "qmk/port/port.h"
#include "qmk/port/platforms/eeprom.h"
#endif

#ifdef VIA_ENABLE
static bool eepromReadU32(uint32_t addr, uint32_t *value)
{
  return eepromRead(addr, (uint8_t *)value, sizeof(uint32_t));
}

static bool eepromWriteU32(uint32_t addr, uint32_t value)
{
  return eepromWrite(addr, (uint8_t *)&value, sizeof(uint32_t));
}

static bool eepromWriteMagic(uint32_t addr, uint32_t value)
{
  if (eepromWriteU32(addr, value) != true)
  {
    logPrintf("[!] EEPROM reset guard : magic write 0x%08X fail\n", value);
    return false;
  }
  return true;
}
#endif  // VIA_ENABLE


void eepromResetGuardStageInvalidation(void)
{
#ifdef VIA_ENABLE
  eeprom_write_dword((uint32_t *)(uintptr_t)EECONFIG_USER_RESET_GUARD_MAGIC, ERA_EEPROM_RESET_GUARD_CLEAR);
  eeprom_write_dword((uint32_t *)(uintptr_t)EECONFIG_USER_RESET_GUARD_KEY, ERA_EEPROM_RESET_GUARD_CLEAR);
#endif
}

bool eepromResetGuardInvalidate(void)
{
#ifdef VIA_ENABLE
  // guard도 RAM 이미지와 같은 writer를 거친다. 기존 page 완료가 reset 의도를 덮지 못한다.
  if (!eeprom_flush_pending()) return false;
  eepromResetGuardStageInvalidation();
  return eeprom_flush_pending();
#else
  return false;
#endif
}


bool eepromResetGuardCheck(void)
{
#ifdef VIA_ENABLE
  const uint32_t magic_addr = (uint32_t)((uintptr_t)EECONFIG_USER_RESET_GUARD_MAGIC);
  const uint32_t key_addr   = (uint32_t)((uintptr_t)EECONFIG_USER_RESET_GUARD_KEY);
  uint32_t magic_value      = 0;
  uint32_t key_value        = 0;

  if (eepromReadU32(magic_addr, &magic_value) != true || eepromReadU32(key_addr, &key_value) != true)
  {
    logPrintf("[!] EEPROM reset guard : read fail\n");
    return false;
  }

  const bool has_magic   = (magic_value == ERA_EEPROM_RESET_GUARD_MAGIC);
  const bool has_key_match = (key_value == ERA_EEPROM_RESET_KEY);

  if (has_magic && has_key_match)
  {
    return true;
  }

  if (has_magic && has_key_match == false)
  {
    if (eepromWriteMagic(magic_addr, ERA_EEPROM_RESET_GUARD_CLEAR) != true)
    {
      return false;
    }
  }

  logPrintf("[  ] EEPROM reset guard : reset (magic=0x%08X, key=0x%08X, target=0x%08X)\n",
            magic_value,
            key_value,
            ERA_EEPROM_RESET_KEY);

  if (eepromFormat() != true)
  {
    logPrintf("[!] EEPROM reset guard : format fail\n");
    return false;
  }

  if (!eeprom_init()) return false;  // 포맷한 칩과 RAM 이미지를 다시 맞춘다

  if (eeprom_apply_factory_defaults(true) != true)              // VIA CLEAN과 같은 초기화 경로
  {
    logPrintf("[!] EEPROM reset guard : factory defaults fail\n");
    return false;
  }

  logPrintf("[  ] EEPROM reset guard : reset done (key=0x%08X)\n", ERA_EEPROM_RESET_KEY);  // 추가 재부팅 없이 부팅을 이어간다
  return true;
#else
  return true;
#endif
}
