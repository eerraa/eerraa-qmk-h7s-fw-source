# Third-party notices — ERA-QMK-H7S-FW

ERA-QMK-H7S-FW is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. The license text is in `LICENSE`. Some components below are licensed GPL-3.0-or-later or Apache-2.0, which are compatible with version 3 of the GPL but not with version 2, so the GPL-2.0-or-later components are used here under version 3.

The firmware contains the components below. Each keeps its own copyright, and the license named in its row applies to it. Where a file header states a license, that header governs the file. License texts that are not shipped elsewhere in this tree are reproduced at the end of this file.

| Component | Location | Copyright | License |
| --- | --- | --- | --- |
| ERA-QMK-H7S-FW | everything not listed below | era (@eerraa) | GPL-3.0-or-later |
| baram-qmk-h7s, the original project this firmware was ported from | the ported base, including the files marked `Author: baram` | Hancheol Cho | MIT |
| QMK Firmware and TMK | QMK-derived files under `src/ap/modules/qmk/` | QMK contributors, Jun Wako and the other authors named in the file headers | GPL-2.0-or-later; a few files GPL-3.0-or-later or Apache-2.0, as their headers state |
| STM32H7RSxx HAL Driver, with project modifications | `src/lib/ST/STM32H7RSxx_HAL_Driver/` | STMicroelectronics; project modifications by era (@eerraa) | BSD-3-Clause |
| CMSIS | `src/lib/ST/CMSIS/` | Arm Limited, STMicroelectronics | Apache-2.0 |
| STM32 device, startup and linker files | `src/bsp/device/`, `src/bsp/startup/`, `src/bsp/ldscript/` | STMicroelectronics | as stated in each file header |
| W25Q128FV definitions | `src/common/hw/include/qspi/w25q128fv.h` | STMicroelectronics | BSD-3-Clause |
| EEPROM emulation | ST-marked files in `src/hw/driver/eeprom/` | STMicroelectronics | BSD-3-Clause |
| lib8tion | `src/lib/lib8tion/` | FastLED | MIT |
| UF2 conversion tool | `tools/uf2/` | Microsoft Corporation | MIT |
| newlib / newlib-nano runtime and vendored speed-optimized memory primitives | `src/lib/newlib/`; linked newlib-nano C/math runtime from GNU Arm Embedded Toolchain 10.3-2021.10 | Red Hat, Inc.; Arm Ltd; the Regents of the University of California; David M. Gay / AT&T / Lucent; Sun Microsystems; other contributors identified in the upstream notices | newlib's per-file permissive licenses; Arm BSD-3-Clause for the vendored ARM assembly/header; full notices in `src/lib/newlib/COPYING.NEWLIB` and retained file headers |
| GCC target runtime (libgcc; libstdc++ / libsupc++ where actually linked) | compiler-provided target runtime from GNU Arm Embedded Toolchain 10.3-2021.10; not vendored | Free Software Foundation, Inc. and contributors | GPL-3.0-or-later WITH GCC-exception-3.1, as stated in the applicable runtime files |

## MIT License

Applies to the components marked MIT above.

```text
Copyright (c) 2024 Hancheol Cho
Copyright (c) 2013 FastLED
Copyright (c) Microsoft Corporation. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## BSD-3-Clause License

Applies to the components marked BSD-3-Clause above.

```text
Copyright (c) 2015-2024 STMicroelectronics. All rights reserved.
Copyright (c) 2026 era (@eerraa)

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
  3. Neither the name of STMicroelectronics nor the names of its contributors
     may be used to endorse or promote products derived from this software
     without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Apache License 2.0

Applies to CMSIS and to the QMK files whose headers state Apache-2.0. The license text is in `src/lib/ST/CMSIS/LICENSE.txt`.

## newlib and toolchain runtime

GNU Arm Embedded Toolchain **10.3-2021.10** identifies both newlib and
newlib-nano as newlib-cygwin commit
[`2a3a03972b35377aef8d3d52d873ac3b8fcc512c`](https://github.com/mirror/newlib-cygwin/tree/2a3a03972b35377aef8d3d52d873ac3b8fcc512c)
(a snapshot after newlib 4.1.0, not the unqualified 4.1.0 release tag).
The toolchain's `share/doc/gcc-arm-none-eabi/release.txt` and
[Arm release notes](https://documentation-service.arm.com/static/6686858969e89f01e39c4158)
identify this revision. The firmware continues to link the nano runtime,
including its printf and allocator, using `-specs=nano.specs`.

The project's unmodified copies of `libc/machine/arm/memcpy.S`,
`memcpy-armv7m.S`, `acle-compat.h`, and `libc/string/memset.c`, `memmove.c`,
and `memcmp.c` come from that same revision. These implement only the four
ordinary C memory functions, selected as in the toolchain's full
`thumb/v7e-m+dp/hard/libc.a`; they do not replace the rest of newlib-nano.
Project-authored `glue/local.h` supplies the internal loop-to-libcall
inhibition macro without importing unrelated newlib locale machinery.
The upstream implementation files have not been edited.

The original Arm notices remain in the three ARM files. The generic C files
have no individual copyright/license notice; the default and applicable
per-component notices in upstream
[`COPYING.NEWLIB`](https://github.com/mirror/newlib-cygwin/blob/2a3a03972b35377aef8d3d52d873ac3b8fcc512c/COPYING.NEWLIB)
remain available verbatim at `src/lib/newlib/COPYING.NEWLIB`.
That complete collection also records the copyrights, permission conditions
and disclaimers for the toolchain-provided newlib-nano runtime, including
stdio, allocation and number conversion. It is a collection of licenses,
not a claim that every listed component or host build tool is linked into
the firmware. The applicable per-file terms govern.

The GCC Runtime Library Exception is **not** newlib's license.
For covered libgcc/libstdc++/libsupc++ files, its
[version 3.1, section 1](https://www.gnu.org/licenses/gcc-exception-3.1.html)
permits distribution of the combined target code on the independent
modules' terms following an Eligible Compilation Process. The
[FSF FAQ](https://www.gnu.org/licenses/gcc-exception-3.1-faq.html)
explicitly distinguishes that combined executable from separately
distributing the libraries or their source. Thus it does not require an
additional attribution entry solely for the GCC runtime code linked into
this firmware; the table entry above is informational. It does not remove
the obligations of this GPL-3.0-or-later project or of other components.
Redistributing a runtime library itself, or vendoring its source, would
require preserving its applicable GPL/exception notices and meeting those
terms separately. The exception text is supplied with the pinned toolchain
in `share/doc/gcc-arm-none-eabi/license.txt`; the GPL text is in `LICENSE`.

These notices and license texts are maintained in the development and
public source repositories. This change does not change the release ZIP,
VIA application, or their user interfaces.
