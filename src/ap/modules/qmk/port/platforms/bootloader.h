/*
Copyright 2011 Jun Wako <wakojun@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "ap_def.h"

/* give code for your bootloader to come up if needed */
void bootloader_jump(void);
bool bootloader_jump_deferred(void);  // V250310R6: VIA 응답 송신 후 부트모드 진입 예약
void mcu_reset(void);
bool mcu_reset_deferred(void);        // V250310R6: VIA 응답 송신 후 일반 리셋 예약

/* USB lifecycle shutdown authorization (design §9) and the only way to seal. Thread mode only
   (handler mode returns false); RAM only: no blocking, RTC write or I2C, so it is callable inside
   the caller's short PRIMASK commit. False leaves everything unsealed. */
bool bootloader_shutdown_seal(void);
bool bootloader_shutdown_sealed(void);  // true only when persistence and boot intent are both sealed
