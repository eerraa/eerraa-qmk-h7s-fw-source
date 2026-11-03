// SPDX-License-Identifier: GPL-3.0-or-later
#include "peripheral_irq.h"

/* One private list supplies both the assertions and the stored IRQ values. */
#define I2C3_LINES(X) X(I2C3_EV_IRQn) X(I2C3_ER_IRQn)
#define LED_LINES(X) X(GPDMA1_Channel4_IRQn)
#define CHECK_LINE(irq) \
  _Static_assert((irq) >= 0 && (irq) != OTG_HS_IRQn && (irq) != OTG_FS_IRQn, "peripheral IRQ set excludes USB");
I2C3_LINES(CHECK_LINE)
LED_LINES(CHECK_LINE)

#define LINE_VALUE(irq) irq,
static const IRQn_Type i2c3_lines[] = {I2C3_LINES(LINE_VALUE)};
static const IRQn_Type led_lines[] = {LED_LINES(LINE_VALUE)};

typedef struct
{
  const IRQn_Type *lines;
  uint32_t count;
} peripheral_irq_set_t;

static const peripheral_irq_set_t sets[] =
{
  [PERIPHERAL_IRQ_I2C3] = {i2c3_lines, sizeof(i2c3_lines) / sizeof(i2c3_lines[0])},
  [PERIPHERAL_IRQ_LED] = {led_lines, sizeof(led_lines) / sizeof(led_lines[0])},
};

static const peripheral_irq_set_t *getSet(peripheral_irq_id_t id)
{
  if (__get_IPSR() != 0U || (uint32_t)id >= sizeof(sets) / sizeof(sets[0])) return NULL;
  const peripheral_irq_set_t *set = &sets[id];
  for (uint32_t i = 0; i < set->count; ++i)
    if ((int32_t)set->lines[i] < 0 || set->lines[i] == OTG_HS_IRQn || set->lines[i] == OTG_FS_IRQn) return NULL;
  return set;
}

bool peripheralIrqSave(peripheral_irq_id_t id, uint32_t *enabled)
{
  const peripheral_irq_set_t *set = getSet(id);
  if (set == NULL || enabled == NULL) return false;
  *enabled = 0U;
  for (uint32_t i = 0; i < set->count; ++i)
  {
    if (NVIC_GetEnableIRQ(set->lines[i])) *enabled |= 1UL << i;
    /* CMSIS DisableIRQ supplies DSB/ISB before the protected region. */
    NVIC_DisableIRQ(set->lines[i]);
  }
  return true;
}

void peripheralIrqRestore(peripheral_irq_id_t id, uint32_t enabled)
{
  const peripheral_irq_set_t *set = getSet(id);
  if (set == NULL) return;
  for (uint32_t i = 0; i < set->count; ++i)
    if (enabled & (1UL << i)) NVIC_EnableIRQ(set->lines[i]);
}

void peripheralIrqEnable(peripheral_irq_id_t id)
{
  peripheralIrqRestore(id, UINT32_MAX);
}

void peripheralIrqDisable(peripheral_irq_id_t id)
{
  uint32_t enabled;
  (void)peripheralIrqSave(id, &enabled);
}
