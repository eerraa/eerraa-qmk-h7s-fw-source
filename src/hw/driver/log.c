/*
 * log.c
 *
 *  Created on: Nov 12, 2021
 *      Author: baram
 */




#include "log.h"
#include "uart.h"
#include "format.h"
#include <string.h>
#ifdef _USE_HW_CLI
#include "cli.h"
#endif


#ifdef _USE_HW_LOG

#ifdef _USE_HW_RTOS
#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);
#else
#define lock()
#define unLock()
#endif


typedef struct
{
  uint16_t line_index;
  uint16_t buf_length;
  uint16_t buf_length_max;
  uint16_t buf_index;
  uint8_t *buf;
} log_buf_t;


log_buf_t log_buf_boot;
log_buf_t log_buf_list;

static uint8_t buf_boot[LOG_BOOT_BUF_MAX];
static uint8_t buf_list[LOG_LIST_BUF_MAX];

static bool is_init = false;
static bool is_boot_log = true;
static bool is_enable = true;
static bool is_open = false;

static uint8_t  log_ch = LOG_CH;
static uint32_t log_baud = 115200;

static char print_buf[256];

#ifdef _USE_HW_RTOS
static SemaphoreHandle_t mutex_lock;
#endif



#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif





bool logInit(void)
{
#ifdef _USE_HW_RTOS
  mutex_lock = xSemaphoreCreateMutex();
#endif

  log_buf_boot.line_index     = 0;
  log_buf_boot.buf_length     = 0;
  log_buf_boot.buf_length_max = LOG_BOOT_BUF_MAX;
  log_buf_boot.buf_index      = 0;
  log_buf_boot.buf            = buf_boot;


  log_buf_list.line_index     = 0;
  log_buf_list.buf_length     = 0;
  log_buf_list.buf_length_max = LOG_LIST_BUF_MAX;
  log_buf_list.buf_index      = 0;
  log_buf_list.buf            = buf_list;


  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("log", cliCmd);
#endif

  return true;
}

void logEnable(void)
{
  is_enable = true;
}

void logDisable(void)
{
  is_enable = false;
}

void logBoot(uint8_t enable)
{
  is_boot_log = enable;
}

bool logOpen(uint8_t ch, uint32_t baud)
{
  log_ch   = ch;
  log_baud = baud;
  is_open  = true;

  is_open = uartOpen(ch, baud);

  return is_open;
}

static bool logBufWrite(log_buf_t *p_log, const char *p_data, uint32_t length, uint32_t text_length)
{
  uint32_t buf_last;
  uint8_t *p_buf;
  uint32_t buf_len = 5 + text_length;
  static const char hex[] = "0123456789ABCDEF";


  // Keep the historical admission reserve, including bytes after a payload NUL.
  buf_last = p_log->buf_index + length + 8;
  if (buf_last > p_log->buf_length_max)
  {
    p_log->buf_index = 0;
    buf_last = p_log->buf_index + length + 8;

    if (buf_last > p_log->buf_length_max)
    {
      return false;
    }
  }

  p_buf = &p_log->buf[p_log->buf_index];

  for (uint32_t i = 0; i < 4; i++)
  {
    p_buf[i] = hex[(p_log->line_index >> (12 - 4 * i)) & 0xF];
  }
  p_buf[4] = '\t';
  memcpy(&p_buf[5], p_data, text_length);
  p_buf[buf_len] = 0;
  p_log->line_index++;
  p_log->buf_index += buf_len;


  if (buf_len + p_log->buf_length <= p_log->buf_length_max)
  {
    p_log->buf_length += buf_len;
  }

  return true;
}

void logPrintf(const char *fmt, ...)
{

  va_list args;
  size_t len;
  size_t text_length;


  if (is_init != true) return;

  lock();
  va_start(args, fmt);
  len = formatVscnprintf(print_buf, sizeof(print_buf), fmt, args);
  const char *nul = memchr(print_buf, '\0', len);
  text_length = nul ? (size_t)(nul - print_buf) : len;

  if (is_open == true && is_enable == true)
  {
    uartWrite(log_ch, (uint8_t *)print_buf, len);
  }

  if (is_boot_log)
  {
    logBufWrite(&log_buf_boot, print_buf, len, text_length);
  }
  logBufWrite(&log_buf_list, print_buf, len, text_length);

  va_end(args);

  unLock();
}


#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "enable"))
  {
    bool open_ok = is_open;

    if (open_ok != true)
    {
      open_ok = logOpen(log_ch, log_baud);
    }

    if (open_ok == true)
    {
      logEnable();
      cliPrintf("[log] UART output enabled (ch=%d baud=%lu)\n",
                log_ch, (unsigned long)log_baud);                      // V251113R1: CLI에서 UART 로그를 필요 시 활성화
    }
    else
    {
      cliPrintf("[log] UART open failed (ch=%d)\n", log_ch);            // V251113R1: 활성화 실패 경고
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "disable"))
  {
    logDisable();
    cliPrintf("[log] UART output disabled\n");                          // V251113R1: CLI에서 UART 로그 출력 중단
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("boot.line_index %d\n", log_buf_boot.line_index);
    cliPrintf("boot.buf_length %d\n", log_buf_boot.buf_length);
    cliPrintf("\n");
    cliPrintf("list.line_index %d\n", log_buf_list.line_index);
    cliPrintf("list.buf_length %d\n", log_buf_list.buf_length);
    cliPrintf("\n");
    cliPrintf("uart.open %d\n", is_open ? 1 : 0);                       // V251113R1: 현재 UART 로그 상태 표시
    cliPrintf("uart.enable %d\n", is_enable ? 1 : 0);                    // V251113R1: CLI에서 상태 점검
    cliPrintf("uart.ch %d\n", log_ch);
    cliPrintf("uart.baud %lu\n", (unsigned long)log_baud);

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "boot"))
  {
    uint32_t index = 0;

    while(cliKeepLoop())
    {
      uint32_t buf_len;

      buf_len = log_buf_boot.buf_length - index;
      if (buf_len == 0)
      {
        break;
      }
      if (buf_len > 64)
      {
        buf_len = 64;
      }

      lock();
      cliWrite((uint8_t *)&log_buf_boot.buf[index], buf_len);
      index += buf_len;
      unLock();
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "list"))
  {
    uint32_t index = 0;

    while(cliKeepLoop())
    {
      uint32_t buf_len;

      buf_len = log_buf_list.buf_length - index;
      if (buf_len == 0)
      {
        break;
      }
      if (buf_len > 64)
      {
        buf_len = 64;
      }

      lock();
      cliWrite((uint8_t *)&log_buf_list.buf[index], buf_len);
      index += buf_len;
      unLock();
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("log info\n");
    cliPrintf("log boot\n");
    cliPrintf("log list\n");
    cliPrintf("log enable\n");                                          // V251113R1: CLI 도움말에 토글 명령 추가
    cliPrintf("log disable\n");
  }
}
#endif


#endif