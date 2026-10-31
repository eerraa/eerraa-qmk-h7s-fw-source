#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "micros.h"

#if _DEF_ENABLE_MATRIX_TIMING_PROBE
extern uint32_t g_matrixInstrumentationScanTime;  // V251009R9: 매트릭스 계측 결과 저장소
#endif

static inline uint32_t matrixInstrumentationCaptureStart(void)
{
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
  return micros();  // V260823R2: 매트릭스 probe만 이 타임스탬프를 사용
#else
  return 0U;
#endif
}

static inline void matrixInstrumentationLogScan(uint32_t pre_time_us, bool info_enabled)
{
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
  if (info_enabled)
  {
    g_matrixInstrumentationScanTime = micros() - pre_time_us;
  }
#else
  (void)pre_time_us;
  (void)info_enabled;
#endif
}

static inline void matrixInstrumentationReset(void)
{
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
  g_matrixInstrumentationScanTime = 0U;
#endif
}

static inline uint32_t matrixInstrumentationGetScanTime(void)
{
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
  return g_matrixInstrumentationScanTime;
#else
  return 0U;
#endif
}

static inline bool matrixInstrumentationIsCompileEnabled(void)
{
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
  return true;
#else
  return false;
#endif
}
