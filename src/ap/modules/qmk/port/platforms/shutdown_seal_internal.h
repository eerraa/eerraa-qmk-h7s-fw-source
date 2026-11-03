#pragma once

#include <stdbool.h>

// Raw halves of bootloader_shutdown_seal(). Only bootloader.c calls them; eeprom.c and reset.c
// include this header solely to define them. Sealing one half alone would let the other
// replace the authorized intent, so no public header declares them.
bool eeprom_admission_seal(void);   // false and no change while persistence is pending
void resetSealBootIntent(void);     // freezes the RTC boot intent; RAM only
