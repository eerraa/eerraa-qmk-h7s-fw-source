#pragma once

#include <stdbool.h>
#include <stdint.h>

/* One exact value is shared by the runtime and both VIA presentations.
 * Legacy conversion is only a SET normalizer / read-only GET projection. */
#define ERA_TERM_DEFAULT_MS      200U
#define ERA_TERM_LEGACY_MIN_MS   100U
#define ERA_TERM_LEGACY_MAX_MS   500U
#define ERA_TERM_LEGACY_STEP_MS  20U
#define ERA_TERM_LEGACY_UNIT_MS  10U

static inline bool era_term_exact_valid(uint16_t term_ms)
{
  return term_ms != 0U;
}

static inline uint16_t era_term_legacy_normalize(uint16_t term_ms)
{
  if (term_ms < ERA_TERM_LEGACY_MIN_MS) term_ms = ERA_TERM_LEGACY_MIN_MS;
  if (term_ms > ERA_TERM_LEGACY_MAX_MS) term_ms = ERA_TERM_LEGACY_MAX_MS;
  return ERA_TERM_LEGACY_MIN_MS +
         ((term_ms - ERA_TERM_LEGACY_MIN_MS) / ERA_TERM_LEGACY_STEP_MS) * ERA_TERM_LEGACY_STEP_MS;
}

static inline uint8_t era_term_legacy_units(uint16_t term_ms)
{
  return (uint8_t)(era_term_legacy_normalize(term_ms) / ERA_TERM_LEGACY_UNIT_MS);
}
