// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hw_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  PERIPHERAL_IRQ_I2C3,
  PERIPHERAL_IRQ_LED
} peripheral_irq_id_t;

/* Foreground only. Regions contain bounded state/IT setup, never waits or
 * USB calls. The implementation owns the IRQ lists; callers cannot supply one. */
bool peripheralIrqSave(peripheral_irq_id_t id, uint32_t *enabled);
void peripheralIrqRestore(peripheral_irq_id_t id, uint32_t enabled);

/* Initialization/deinitialization of the same closed peripheral owners. */
void peripheralIrqEnable(peripheral_irq_id_t id);
void peripheralIrqDisable(peripheral_irq_id_t id);

#ifdef __cplusplus
}
#endif
