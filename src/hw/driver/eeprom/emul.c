#include "eeprom.h"
#include "cli.h"
#if defined(QMK_KEYMAP_CONFIG_H)
#include "qmk/port/platforms/eeprom.h"                 // V251112R2: QMK EEPROM 큐 상태 출력
#endif


#if defined(_USE_HW_EEPROM) && defined(EEPROM_CHIP_EMUL)
#include "eeprom_emul.h"

#if PAGES_NUMBER > HW_EEPROM_MAX_PAGES
#error "EEPROM PAGES_NUMBER OVER"
#endif


#if CLI_USE(HW_EEPROM)
void cliEeprom(cli_args_t *args);
#endif
static void eepromInitMPU(void);



#define EEPROM_MAX_SIZE   NB_OF_VARIABLES


static bool is_init = false;
static __IO bool is_erasing = false;

typedef struct
{
  uint32_t last_duration_ms;
  uint32_t wait_entry_snapshot;
  uint32_t total_count;
  uint32_t in_progress_begin_ms;
} eeprom_emul_cleanup_stats_t;                         // V251112R8: 비동기 클린업 계측

static eeprom_emul_cleanup_stats_t cleanup_stats = {0};

static bool eepromScheduleCleanup(void);


bool eepromInit()
{
  EE_Status ee_ret = EE_OK;


  eepromInitMPU();
  
  /* Enable ICACHE after testing SR BUSYF and BSYENDF */
  while((ICACHE->SR & 0x1) != 0x0) {;}
  while((ICACHE->SR & 0x2) == 0x0) {;}
  ICACHE->CR |= 0x1; 



  HAL_NVIC_SetPriority(FLASH_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(FLASH_IRQn);

  HAL_FLASH_Unlock();
  ee_ret = EE_Init(EE_FORCED_ERASE);
  HAL_FLASH_Lock();
  logPrintf("[%s] eepromInit()\n", ee_ret == EE_OK ? "OK":"E_");
  logPrintf("     chip  : emul\n");
  
  is_init = ee_ret == EE_OK ? true:false;

#if CLI_USE(HW_EEPROM)
  cliAdd("eeprom", cliEeprom);
#endif

  return is_init;
}

bool eepromIsInit(void)
{
  return is_init;
}

bool eepromValid(uint32_t addr)
{
  if (addr >= EEPROM_MAX_SIZE)
  {
    return false;
  }

  return is_init;
}

bool eepromReadByte(uint32_t addr, uint8_t *p_data)
{
  bool ret = true;
  EE_Status ee_ret = EE_OK;
  uint16_t ee_addr;

  if (addr >= EEPROM_MAX_SIZE)
    return false;
  if (is_init != true)
    return false;
  ee_addr = addr + 1;

  HAL_FLASH_Unlock();
  ee_ret = EE_ReadVariable8bits(ee_addr, p_data);                        // V251112R8: 8비트 API 복구로 낭비 제거
  HAL_FLASH_Lock();
  if (ee_ret != EE_OK)
  {
    if (ee_ret == EE_NO_DATA)
      *p_data = 0;
    else
      ret = false;
  }
  return ret;
}

bool eepromWriteByte(uint32_t addr, uint8_t data_in)
{
  bool ret = true;
  EE_Status ee_ret = EE_OK;
  uint16_t ee_addr;

  if (addr >= EEPROM_MAX_SIZE)
    return false;
  if (is_init != true)
    return false;

  ee_addr = addr + 1;

  HAL_FLASH_Unlock();
  ee_ret = EE_WriteVariable8bits(ee_addr, data_in);                     // V251112R8: 8비트 쓰기로 락/언락 부담 축소
  if (ee_ret != EE_OK)
  {
    if ((ee_ret & EE_STATUSMASK_CLEANUP) == EE_STATUSMASK_CLEANUP)
    {
      if (eepromScheduleCleanup() != true)
      {
        ret = false;
      }
      else
      {
        ret = false;                                                    // V251112R8: 비동기 클린업 중이므로 상위 계층 재시도
      }
    }
    else
    {
      ret = false;
    }
  }
  HAL_FLASH_Lock();

  return ret;
}

bool eepromWritePage(uint32_t addr, uint8_t const *p_data, uint32_t length)
{
  // V251112R5: 실 칩 드라이버와 동일한 API 형식을 유지하기 위한 래퍼
  bool ret = true;

  while (length-- > 0)
  {
    if (eepromWriteByte(addr++, *p_data++) != true)
    {
      ret = false;
      break;
    }
  }

  return ret;
}

bool eepromRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  bool ret = true;
  uint32_t i;


  for (i=0; i<length; i++)
  {
    if (eepromReadByte(addr + i, &p_data[i]) != true)
    {
      ret = false;
      break;
    }
  }

  return ret;
}

bool eepromWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  return eepromWritePage(addr, p_data, length);  // V251112R5: 페이지 API를 그대로 사용
}

static bool eepromScheduleCleanup(void)
{
  if (is_erasing == true)
  {
    return true;
  }

  cleanup_stats.in_progress_begin_ms = millis();
#if defined(QMK_KEYMAP_CONFIG_H)
  cleanup_stats.wait_entry_snapshot = eeprom_get_write_pending_count();
#else
  cleanup_stats.wait_entry_snapshot = 0;
#endif

  is_erasing = true;

  EE_Status clean_ret = EE_CleanUp_IT();
  if (clean_ret != EE_OK)
  {
    is_erasing = false;
    cleanup_stats.in_progress_begin_ms = 0;
    logPrintf("[!] EEPROM emul cleanup schedule fail : %d\n", clean_ret);  // V251112R8: 실패 로그
    return false;
  }

  return true;
}

uint32_t eepromGetLength(void)
{
  return EEPROM_MAX_SIZE;
}

bool eepromIsErasing(void)
{
  return is_erasing;                                                    // V251112R8: 비동기 클린업 진행 여부 노출
}

bool eepromFormat(void)
{
  bool ret = true;
  EE_Status ee_ret = EE_OK;

  HAL_FLASH_Unlock();
  ee_ret = EE_Format(EE_FORCED_ERASE);
  HAL_FLASH_Lock();
  if (ee_ret != EE_OK)
  {
    ret = false;
  }  
  return ret;
}

void FLASH_IRQHandler(void)
{
  HAL_FLASH_IRQHandler();
}

void HAL_FLASH_EndOfOperationCallback(uint32_t ReturnValue)
{
  /* Call CleanUp callback when all requested pages have been erased */
  if (ReturnValue == 0xFFFFFFFF)
  {
    EE_EndOfCleanup_UserCallback();
  }
}

void EE_EndOfCleanup_UserCallback(void)
{
  if (cleanup_stats.in_progress_begin_ms != 0)
  {
    cleanup_stats.last_duration_ms = millis() - cleanup_stats.in_progress_begin_ms;  // V251112R8: 마지막 소요 시간 기록
    cleanup_stats.in_progress_begin_ms = 0;
  }
  cleanup_stats.total_count++;
  is_erasing = false;
}

void eepromInitMPU(void)
{
  /* MPU registers address definition */
  volatile uint32_t *mpu_type  = (void *)0xE000ED90;
  volatile uint32_t *mpu_ctrl  = (void *)0xE000ED94;
  volatile uint32_t *mpu_rnr   = (void *)0xE000ED98;
  volatile uint32_t *mpu_rbar  = (void *)0xE000ED9C;
  volatile uint32_t *mpu_rlar  = (void *)0xE000EDA0;
  volatile uint32_t *mpu_mair0 = (void *)0xE000EDC0;

  /* Check that MPU is implemented and recover the number of regions available */
  uint32_t mpu_regions_nb = ((*mpu_type) >> 8) & 0xff;

  /* If the MPU is implemented */
  if (mpu_regions_nb != 0)
  {
    /* Set RNR to configure the region with the highest number which also has the highest priority */
    *mpu_rnr = (mpu_regions_nb - 1) & 0x000000FF;

    /* Set RBAR to get the region configured starting at FLASH_USER_START_ADDR, being non-shareable, r/w by any privilege level, and non executable */
    *mpu_rbar &= 0x00000000;
    *mpu_rbar  = (START_PAGE_ADDRESS | (0x0 << 3) | (0x1 << 1) | 0x1);

    /* Set RLAR to get the region configured ending at FLASH_USER_END_ADDR, being associated to the Attribute Index 0 and enabled */
    *mpu_rlar &= 0x00000000;
    *mpu_rlar  = (END_EEPROM_ADDRESS | (0x0 << 1) | 0x1);

    /* Set MAIR0 so that the region configured is inner and outer non-cacheable */
    *mpu_mair0 &= 0xFFFFFF00;
    *mpu_mair0 |= ((0x4 << 4) | 0x4);

    /* Enable MPU + PRIVDEFENA=1 for the MPU rules to be effective + HFNMIENA=0 to ease debug */
    *mpu_ctrl &= 0xFFFFFFF8;
    *mpu_ctrl |= 0x5;
  }

  return;
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
      cliPrintf("eeprom init   : %d\n", eepromIsInit());
      cliPrintf("eeprom length : %d bytes\n", eepromGetLength());
#if defined(QMK_KEYMAP_CONFIG_H)
      cliPrintf("eeprom queue cur : %lu entries\n", (unsigned long)eeprom_get_write_pending_count());   // V251112R2: QMK 큐 계측
      cliPrintf("eeprom queue max : %lu entries\n", (unsigned long)eeprom_get_write_pending_max());     // V251112R2: 최고 사용량
      cliPrintf("eeprom queue ofl : %lu events\n", (unsigned long)eeprom_get_write_overflow_count());  // V251112R2: 직접 쓰기 횟수
#endif
      cliPrintf("emul cleanup busy : %d\n", eepromIsErasing());                                        // V251112R8: 클린업 진행 여부
      cliPrintf("emul cleanup last : %lums\n", (unsigned long)cleanup_stats.last_duration_ms);         // V251112R8: 최근 클린업 시간
      cliPrintf("emul cleanup wait : %lu entries\n", (unsigned long)cleanup_stats.wait_entry_snapshot);// V251112R8: 트리거 당시 대기 엔트리
      cliPrintf("emul cleanup cnt  : %lu\n", (unsigned long)cleanup_stats.total_count);                // V251112R8: 누적 실행 횟수
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
      bool ee_ret;
      addr   = (uint32_t)args->getData(1);
      length = (uint32_t)args->getData(2);

      if (length > eepromGetLength())
      {
        cliPrintf( "length error\n");
      }
      for (i=0; i<length; i++)
      {
        ee_ret = eepromReadByte(addr+i, &data);
        cliPrintf( "addr : %d\t 0x%02X, ret %d\n", addr+i, data, ee_ret);
      }
    }
    else if(args->isStr(0, "write") == true)
    {
      addr = (uint32_t)args->getData(1);
      data = (uint8_t )args->getData(2);

      pre_time = millis();
      eep_ret = eepromWriteByte(addr, data);

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
#endif /* _USE_HW_CMDIF_EEPROM */


#endif /* _USE_HW_EEPROM */
