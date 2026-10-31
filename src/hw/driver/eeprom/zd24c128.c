#include "eeprom.h"
#if defined(QMK_KEYMAP_CONFIG_H)
#include "qmk/port/platforms/eeprom.h"                 // V251112R2: QMK EEPROM 큐 상태 출력
#endif


#if defined(_USE_HW_EEPROM) && defined(EEPROM_CHIP_ZD24C128)
#include "i2c.h"
#include "cli.h"



#if CLI_USE(HW_EEPROM)
void cliEeprom(cli_args_t *args);
#endif


#define EEPROM_MAX_SIZE                (16*1024)
#define EEPROM_PAGE_SIZE               32                          // V251112R5: ZD24C128 페이지 크기
#define EEPROM_WRITE_I2C_TIMEOUT_MS    10                          // V251112R5: 페이지 쓰기 I2C 타임아웃
#define EEPROM_WRITE_READY_TIMEOUT_MS  100                         // V251112R5: 페이지 쓰기 완료 확인 제한 시간


static bool is_init = false;
static uint8_t i2c_ch = _DEF_I2C1;
static uint8_t i2c_addr = 0x50;
static uint8_t page_write_buf[EEPROM_PAGE_SIZE];                   // V251112R5: I2C 페이지 버퍼

static bool eepromWaitReady(uint32_t timeout_ms);
// V260909R1: page_write_buf의 소유권은 WRITE -> READY ACK가 끝날 때까지 유지한다.
typedef enum { PAGE_IDLE, PAGE_WRITE, PAGE_WAIT_READY, PAGE_PROBE, PAGE_VERIFY } page_state_t;
static uint8_t page_verify_buf[EEPROM_PAGE_SIZE];
static uint16_t page_address, page_length;
static page_state_t page_state;
static uint32_t page_ready_begin_ms, page_next_probe_ms;

bool eepromWritePageStart(uint32_t addr, const uint8_t *data, uint32_t length)
{
  if (!is_init || page_state != PAGE_IDLE || data == NULL || length == 0U ||
      addr >= EEPROM_MAX_SIZE || length > EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE) ||
      length > EEPROM_MAX_SIZE - addr) return false;
  memcpy(page_write_buf, data, length);
  if (!i2cWriteA16BytesAsync(i2c_ch, i2c_addr, (uint16_t)addr, page_write_buf, (uint16_t)length)) return false;
  page_address = (uint16_t)addr;
  page_length = (uint16_t)length;
  page_state = PAGE_WRITE;
  return true;
}

eeprom_async_result_t eepromWritePagePoll(void)
{
  if (page_state == PAGE_IDLE) return EEPROM_ASYNC_IDLE;
  uint32_t now = millis();
  if (page_state == PAGE_WRITE || page_state == PAGE_PROBE || page_state == PAGE_VERIFY) {
    i2c_async_result_t result = i2cAsyncPoll(i2c_ch, NULL);
    if (result == I2C_ASYNC_BUSY) return EEPROM_ASYNC_BUSY;
    if (page_state == PAGE_VERIFY) {
      page_state = PAGE_IDLE;
      return result == I2C_ASYNC_DONE && memcmp(page_verify_buf, page_write_buf, page_length) == 0
          ? EEPROM_ASYNC_DONE : EEPROM_ASYNC_ERROR;
    }
    if (page_state == PAGE_PROBE && result == I2C_ASYNC_DONE) {
      if (!i2cReadA16BytesAsync(i2c_ch, i2c_addr, page_address, page_verify_buf, page_length)) {
        page_state = PAGE_IDLE;
        return EEPROM_ASYNC_ERROR;
      }
      page_state = PAGE_VERIFY;
      return EEPROM_ASYNC_BUSY;
    }
    if (page_state == PAGE_WRITE && result == I2C_ASYNC_DONE) {
      page_ready_begin_ms = now;
      page_next_probe_ms = now;
      page_state = PAGE_WAIT_READY;
    } else if (page_state == PAGE_PROBE && result == I2C_ASYNC_NACK) {
      page_next_probe_ms = now + 1U;
      page_state = PAGE_WAIT_READY;
    } else {
      page_state = PAGE_IDLE;
      return EEPROM_ASYNC_ERROR;
    }
  }
  if ((uint32_t)(now - page_ready_begin_ms) >= EEPROM_WRITE_READY_TIMEOUT_MS) {
    page_state = PAGE_IDLE;
    return EEPROM_ASYNC_ERROR;
  }
  if ((int32_t)(now - page_next_probe_ms) >= 0 && i2cProbeAsync(i2c_ch, i2c_addr)) page_state = PAGE_PROBE;
  return EEPROM_ASYNC_BUSY;
}




bool eepromInit()
{
  bool ret;


  ret = i2cBegin(i2c_ch, 1000);                                    // V251112R5: FastMode Plus 1 MHz


  if (ret == true)
  {
    ret = eepromValid(0x00);
  }

  logPrintf("[%s] eepromInit()\n", ret ? "OK":"NG");
  if (ret == true)
  {
    logPrintf("     chip  : ZD24C128\n");
    logPrintf("     found : 0x%02X\n", i2c_addr);
    logPrintf("     size  : %dKB\n", eepromGetLength()/1024);
  }
  else
  {
    logPrintf("     empty\n");
  }

#if CLI_USE(HW_EEPROM)
  cliAdd("eeprom", cliEeprom);
#endif

  is_init = ret;

  return ret;
}

bool eepromIsInit(void)
{
  return is_init;
}

bool eepromValid(uint32_t addr)
{
  if (page_state != PAGE_IDLE) return false;  // V260909R1: async 저장 소유권 보호

  uint8_t data;
  bool ret;

  if (addr >= EEPROM_MAX_SIZE)
  {
    return false;
  }

  ret = i2cReadA16Bytes(i2c_ch, i2c_addr, addr, &data, 1, 100);

  return ret;
}

bool eepromReadByte(uint32_t addr, uint8_t *p_data)
{
  if (page_state != PAGE_IDLE) return false;  // V260909R1: async 저장 소유권 보호

  bool ret;

  if (p_data == NULL || addr >= EEPROM_MAX_SIZE)
  {
    return false;
  }

  ret = i2cReadA16Bytes(i2c_ch, i2c_addr, addr, p_data, 1, 100);

  return ret;
}

bool eepromWritePage(uint32_t addr, uint8_t const *p_data, uint32_t length)
{
  if (page_state != PAGE_IDLE) return false;  // V260909R1: async 저장 소유권 보호

  // V251112R5: 큐에서 전달된 연속 구간을 32바이트 페이지로 전송
  bool ret = true;
  uint32_t page_offset;

  if (length == 0)
  {
    return true;
  }
  if (p_data == NULL || addr >= EEPROM_MAX_SIZE || length > EEPROM_MAX_SIZE - addr)
  {
    return false;
  }

  page_offset = addr % EEPROM_PAGE_SIZE;
  if ((page_offset + length) > EEPROM_PAGE_SIZE)
  {
    return false;
  }

  for (uint32_t i = 0; i < length; i++)
  {
    page_write_buf[i] = p_data[i];
  }

  ret = i2cWriteA16Bytes(i2c_ch, i2c_addr, addr, page_write_buf, length, EEPROM_WRITE_I2C_TIMEOUT_MS);
  if (ret != true)
  {
    return false;
  }

  if (!eepromWaitReady(EEPROM_WRITE_READY_TIMEOUT_MS)) return false;
  uint8_t verify[EEPROM_PAGE_SIZE];
  return i2cReadA16Bytes(i2c_ch, i2c_addr, addr, verify, length, EEPROM_WRITE_I2C_TIMEOUT_MS)
      && memcmp(verify, page_write_buf, length) == 0;
}

bool eepromWriteByte(uint32_t addr, uint8_t data_in)
{
  return eepromWritePage(addr, &data_in, 1);
}

bool eepromRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (length == 0U) return true;
  if (page_state != PAGE_IDLE || p_data == NULL || addr >= EEPROM_MAX_SIZE || length > EEPROM_MAX_SIZE - addr) return false;
  return i2cReadA16Bytes(i2c_ch, i2c_addr, (uint16_t)addr, p_data, length, 100U);
}

bool eepromWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (p_data == NULL || addr >= EEPROM_MAX_SIZE || length > EEPROM_MAX_SIZE - addr) return false;
  if (page_state != PAGE_IDLE) return false;  // V260909R1: async 저장 소유권 보호

  bool ret = false;

  while (length > 0)
  {
    uint32_t page_space = EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE);
    uint32_t chunk      = length < page_space ? length : page_space;

    // V251112R5: 페이지 단위로 잘라내어 쓰기 지연 최소화
    ret = eepromWritePage(addr, p_data, chunk);
    if (ret == false)
    {
      break;
    }

    addr   += chunk;
    p_data += chunk;
    length -= chunk;
  }

  return ret;
}

uint32_t eepromGetLength(void)
{
  return EEPROM_MAX_SIZE;
}

bool eepromIsErasing(void)
{
  return false;                                                     // V251112R8: 외부 EEPROM은 클린업 개념 없음
}

bool eepromFormat(void)
{
  return true;
}

static bool eepromWaitReady(uint32_t timeout_ms)
{
  // V251112R5: FastMode Plus에서 완료 여부를 짧게 폴링
  uint32_t pre_time = millis();

  while (millis()-pre_time < timeout_ms)
  {
    if (i2cIsDeviceReady(i2c_ch, i2c_addr) == true)
    {
      return true;
    }
    delay(1);
  }

  return false;
}




#if CLI_USE(HW_EEPROM)
void cliEeprom(cli_args_t *args)
{
  bool ret = true;
  uint32_t i;
  uint32_t addr;
  uint32_t length;
  uint8_t  data;
  uint32_t pre_time;
  bool eep_ret;


  if (args->argc == 1)
  {
    if(args->isStr(0, "info") == true)
    {
      cliPrintf("eeprom init   : %s\n", eepromIsInit() ? "True":"False");
      cliPrintf("eeprom length : %d bytes\n", eepromGetLength());
#if defined(QMK_KEYMAP_CONFIG_H)
      cliPrintf("eeprom dirty cur : %lu bytes\n", (unsigned long)eeprom_get_write_pending_count());   // V260909R1: 아직 ACK되지 않은 byte 수
      cliPrintf("eeprom dirty max : %lu bytes\n", (unsigned long)eeprom_get_write_pending_max());     // V251112R2: 최고 사용량
      cliPrintf("eeprom queue ofl : %lu events\n", (unsigned long)eeprom_get_write_overflow_count());  // V260909R1: 호환 조회이며 dirty bitmap에는 queue-full 없음
#endif
#if defined(QMK_KEYMAP_CONFIG_H)
      cliPrintf("eeprom failures : %lu\n", (unsigned long)eeprom_get_write_failure_count());
      cliPrintf("eeprom image ready : %d\n", eeprom_is_ready());
#endif
      i2c_ready_wait_stats_t ready_stats;
      i2cGetReadyWaitStats(i2c_ch, &ready_stats);                           // V251112R9: Ready wait 계측 노출
      cliPrintf("ready wait count : %lu\n", (unsigned long)ready_stats.wait_count);
      cliPrintf("ready wait max   : %lums\n", (unsigned long)ready_stats.wait_max_ms);
      cliPrintf("ready wait last  : %lums (addr=0x%02X)\n",
                (unsigned long)ready_stats.wait_last_ms,
                ready_stats.wait_last_addr);
      cliPrintf("emul cleanup busy : %d\n", eepromIsErasing());                                        // V251112R8: 외부 EEPROM에서도 계측 필드 제공
      cliPrintf("emul cleanup last : 0ms\n");                                                          // V251112R8: 클린업 미지원 보드 → 고정 0
      cliPrintf("emul cleanup wait : 0 entries\n");                                                    // V251112R8: 외부 EEPROM은 큐 대기로 전환 없음
      cliPrintf("emul cleanup cnt  : 0\n");                                                            // V251112R8: 클린업 횟수 개념 없음
    }
    else if(args->isStr(0, "format") == true)
    {
      if (eepromFormat() == true)
      {
        cliPrintf("format OK\n");
      }
      else
      {
        cliPrintf("format Fail\n");
      }
    }
    else
    {
      ret = false;
    }
  }
  else if (args->argc == 3)
  {
    if(args->isStr(0, "read") == true)
    {
      addr   = (uint32_t)args->getData(1);
      length = (uint32_t)args->getData(2);

      if (length > eepromGetLength())
      {
        cliPrintf( "length error\n");
      }
      for (i=0; i<length; i++)
      {
        if (eepromReadByte(addr+i, &data) == true)
        {
          cliPrintf( "addr : %d\t 0x%02X\n", addr+i, data);          
        }
        else
        {
          cliPrintf("eepromReadByte() Error\n");
          break;
        }
      }
    }
    else if(args->isStr(0, "write") == true)
    {
      addr = (uint32_t)args->getData(1);
      data = (uint8_t )args->getData(2);

      pre_time = millis();
#if defined(QMK_KEYMAP_CONFIG_H)
      // V260909R1: CLI도 QMK 이미지 범위 안에서는 단일 writer를 거친다. 유지보수 명령만 동기 대기한다.
      if (addr < TOTAL_EEPROM_BYTE_COUNT) {
        eeprom_write_byte((uint8_t *)(uintptr_t)addr, data);
        eep_ret = eeprom_flush_pending();
      } else {
        eep_ret = eeprom_flush_pending() && eepromWriteByte(addr, data);
      }
#else
      eep_ret = eepromWriteByte(addr, data);
#endif

      cliPrintf( "addr : %d\t 0x%02X %dms\n", addr, data, millis()-pre_time);
      if (eep_ret)
      {
        cliPrintf("OK\n");
      }
      else
      {
        cliPrintf("FAIL\n");
      }
    }
    else
    {
      ret = false;
    }
  }
  else
  {
    ret = false;
  }


  if (ret == false)
  {
    cliPrintf( "eeprom info\n");
    cliPrintf( "eeprom format\n");
    cliPrintf( "eeprom read  [addr] [length]\n");
    cliPrintf( "eeprom write [addr] [data]\n");
  }

}
#endif 


#endif 
