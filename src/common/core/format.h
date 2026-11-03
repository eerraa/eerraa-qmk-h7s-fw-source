#ifndef FORMAT_H_
#define FORMAT_H_

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/* Return stored bytes, not libc's required size; errors have no payload. */
static inline size_t formatVscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
  int length = vsnprintf(buf, size, fmt, args);

  if (length < 0 || size == 0) return 0;
  if ((size_t)length >= size) return size - 1;
  return (size_t)length;
}

#endif
