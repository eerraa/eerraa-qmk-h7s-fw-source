#include "flash.h"



#ifdef _USE_HW_FLASH
#include "qspi.h"
#include "spi_flash.h"
#include "cli.h"


#define FLASH_ADDR(bank)          (FLASH_BASE + (bank*FLASH_BANK_SIZE))
#define FLASH_WRITE_SIZE          16
// #define FLASH_SECTOR_SIZE         FLASH_PAGE_SIZE



#if CLI_USE(HW_FLASH)
static void cliFlash(cli_args_t *args);
#endif
_Static_assert(FLASH_BASE % FLASH_WRITE_SIZE == 0 && FLASH_SIZE % FLASH_WRITE_SIZE == 0,
               "internal flash must contain complete program blocks");

static bool flashSpanValid(uint32_t addr, uint32_t length)
{
  return addr >= FLASH_BASE && addr - FLASH_BASE < FLASH_SIZE
      && length <= FLASH_SIZE - (addr - FLASH_BASE);
}

static HAL_StatusTypeDef flashProgramBlock(uint32_t addr, const uint8_t *data, uint32_t length)
{
  uint32_t buf_[FLASH_WRITE_SIZE / sizeof(uint32_t)];
  uint8_t *buf = (uint8_t *)buf_;
  uint32_t offset = addr % FLASH_WRITE_SIZE;
  uint32_t base = addr - offset;

  /* Both partial paths must preserve the entire block, including its tail. */
  if (offset != 0 || length != FLASH_WRITE_SIZE)
  {
    memcpy(buf, (const void *)base, FLASH_WRITE_SIZE);
  }
  memcpy(buf + offset, data, length);
  return HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, base, (uint32_t)buf);
}





bool flashInit(void)
{

  logPrintf("[OK] flashInit()\n");

#if CLI_USE(HW_FLASH)
  cliAdd("flash", cliFlash);
#endif
  return true;
}

bool flashErase(uint32_t addr, uint32_t length)
{
  if (length == 0) return true;

#ifdef _USE_HW_QSPI
  if (addr >= qspiGetAddr() && addr < (qspiGetAddr() + qspiGetLength()))
  {
    return qspiErase(addr - qspiGetAddr(), length);
  }
#endif
#ifdef _USE_HW_SPI_FLASH
  if (addr >= spiFlashGetAddr() && addr < (spiFlashGetAddr() + spiFlashGetLength()))
  {
    return spiFlashErase(addr - spiFlashGetAddr(), length);
  }
#endif

  if (!flashSpanValid(addr, length)) return false;
  uint32_t offset = addr - FLASH_BASE;
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t sector_error;
  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.Sector = offset / FLASH_SECTOR_SIZE;
  erase.NbSectors = (offset + length - 1U) / FLASH_SECTOR_SIZE - erase.Sector + 1U;
  HAL_StatusTypeDef status = HAL_FLASH_Unlock();
  if (status == HAL_OK)
  {
    status = HAL_FLASHEx_Erase(&erase, &sector_error);
  }
  HAL_FLASH_Lock();
  return status == HAL_OK;
}

bool flashWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (length == 0) return true;

#ifdef _USE_HW_QSPI
  if (addr >= qspiGetAddr() && addr < (qspiGetAddr() + qspiGetLength()))
  {
    return qspiWrite(addr - qspiGetAddr(), p_data, length);
  }
#endif
#ifdef _USE_HW_SPI_FLASH
  if (addr >= spiFlashGetAddr() && addr < (spiFlashGetAddr() + spiFlashGetLength()))
  {
    return spiFlashWrite(addr - spiFlashGetAddr(), p_data, length);
  }
#endif

  if (!flashSpanValid(addr, length) || p_data == NULL) return false;
  HAL_StatusTypeDef status = HAL_FLASH_Unlock();
  uint32_t index = 0;
  while (status == HAL_OK && index < length)
  {
    uint32_t count = FLASH_WRITE_SIZE - (addr + index) % FLASH_WRITE_SIZE;
    if (count > length - index) count = length - index;
    status = flashProgramBlock(addr + index, p_data + index, count);
    index += count;
  }
  HAL_FLASH_Lock();
  return status == HAL_OK;
}

bool flashRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (length == 0) return true;

#ifdef _USE_HW_QSPI
  if (addr >= qspiGetAddr() && addr < (qspiGetAddr() + qspiGetLength()))
  {
    return qspiRead(addr - qspiGetAddr(), p_data, length);
  }
#endif
#ifdef _USE_HW_SPI_FLASH
  if (addr >= spiFlashGetAddr() && addr < (spiFlashGetAddr() + spiFlashGetLength()))
  {
    return spiFlashRead(addr - spiFlashGetAddr(), p_data, length);
  }
#endif

  if (!flashSpanValid(addr, length) || p_data == NULL) return false;
  const uint8_t *p_byte = (const uint8_t *)addr;
  for (uint32_t i=0; i<length; i++)
  {
    p_data[i] = p_byte[i];
  }

  return true;
}




#if CLI_USE(HW_FLASH)
void cliFlash(cli_args_t *args)
{
  bool ret = false;
  uint32_t i;
  uint32_t addr;
  uint32_t length;
  uint32_t pre_time;
  bool flash_ret;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("flash addr b1: 0x%X\n", FLASH_ADDR(0));
    cliPrintf("flash addr b2: 0x%X\n", FLASH_ADDR(1));    
    ret = true;
  }

  if(args->argc == 3 && args->isStr(0, "read"))
  {
    uint8_t data;

    addr   = (uint32_t)args->getData(1);
    length = (uint32_t)args->getData(2);

    for (i=0; i<length; i++)
    {
      flash_ret = flashRead(addr+i, &data, 1);

      if (flash_ret == true)
      {
        cliPrintf( "addr : 0x%X\t 0x%02X\n", addr+i, data);
      }
      else
      {
        cliPrintf( "addr : 0x%X\t Fail\n", addr+i);
      }
    }

    ret = true;
  }
    
  if(args->argc == 3 && args->isStr(0, "erase"))
  {
    addr   = (uint32_t)args->getData(1);
    length = (uint32_t)args->getData(2);

    pre_time = millis();
    flash_ret = flashErase(addr, length);

    cliPrintf( "addr : 0x%X\t len : %d %d ms\n", addr, length, (millis()-pre_time));
    if (flash_ret)
    {
      cliPrintf("OK\n");
    }
    else
    {
      cliPrintf("FAIL\n");
    }

    ret = true;
  }
    
  if(args->argc == 3 && args->isStr(0, "write"))
  {
    uint32_t data;

    addr = (uint32_t)args->getData(1);
    data = (uint32_t)args->getData(2);

    pre_time = millis();
    flash_ret = flashWrite(addr, (uint8_t *)&data, 4);

    cliPrintf( "addr : 0x%X\t 0x%X %dms\n", addr, data, millis()-pre_time);
    if (flash_ret)
    {
      cliPrintf("OK\n");
    }
    else
    {
      cliPrintf("FAIL\n");
    }

    ret = true;
  }

  if(args->argc == 3 && args->isStr(0, "check"))
  {
    uint32_t data = 0;
    uint32_t block = 4;


    addr    = (uint32_t)args->getData(1);
    length  = (uint32_t)args->getData(2);
    length -= (length % block);

    do
    {
      cliPrintf("flashErase()..");
      if (flashErase(addr, length) == false)
      {
        cliPrintf("Fail\n");
        break;
      }
      cliPrintf("OK\n");

      cliPrintf("flashWrite()..");
      for (uint32_t i=0; i<length; i+=block)
      {
        data = i;
        if (flashWrite(addr + i, (uint8_t *)&data, block) == false)
        {
          cliPrintf("Fail %d\n", i);
          break;
        }
      }
      cliPrintf("OK\n");

      cliPrintf("flashRead() ..");
      for (uint32_t i=0; i<length; i+=block)
      {
        data = 0;
        if (flashRead(addr + i, (uint8_t *)&data, block) == false)
        {
          cliPrintf("Fail %d\n", i);
          break;
        }
        if (data != i)
        {
          cliPrintf("Check Fail %d\n", i);
          break;
        }
      }  
      cliPrintf("OK\n");


      cliPrintf("flashErase()..");
      if (flashErase(addr, length) == false)
      {
        cliPrintf("Fail\n");
        break;
      }
      cliPrintf("OK\n");  
    } while (0);
    
    ret = true;
  }


  if (ret == false)
  {
    cliPrintf( "flash info\n");
    cliPrintf( "flash read  [addr] [length]\n");
    cliPrintf( "flash erase [addr] [length]\n");
    cliPrintf( "flash write [addr] [data]\n");
    cliPrintf( "flash check [addr] [length]\n");
  }
}
#endif

#endif