
#pragma once


// VIA는 이 날짜로 EEPROM magic을 만든다. 바꾸면 모든 장치의 키맵·매크로가 지워지므로
// 빌드 시각이 아니라 고정값이다. EERRAA도 같은 값을 쓴다 (docs/contract_eeprom.md §2).
#define QMK_BUILDDATE   "2024-04-23-11:29:54"