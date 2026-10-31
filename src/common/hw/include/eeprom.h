#ifndef EEPROM_H_
#define EEPROM_H_

#ifdef __cplusplus
 extern "C" {
#endif



#include "hw_def.h"

#ifdef _USE_HW_EEPROM


// V260909R1: start는 복사/무장만, poll은 한 단계만 진행한다. DONE은 write-cycle ACK 이후다.
typedef enum { EEPROM_ASYNC_IDLE, EEPROM_ASYNC_BUSY, EEPROM_ASYNC_DONE, EEPROM_ASYNC_ERROR } eeprom_async_result_t;
bool eepromWritePageStart(uint32_t addr, const uint8_t *data, uint32_t length);
eeprom_async_result_t eepromWritePagePoll(void);

bool     eepromInit();
bool     eepromIsInit(void);
bool     eepromValid(uint32_t addr);
bool     eepromReadByte(uint32_t addr, uint8_t *p_data);
bool     eepromWriteByte(uint32_t addr, uint8_t data_in);
bool     eepromWritePage(uint32_t addr, uint8_t const *p_data, uint32_t length);  // V251112R5: 32바이트 페이지 쓰기 API
bool     eepromRead(uint32_t addr, uint8_t *p_data, uint32_t length);
bool     eepromWrite(uint32_t addr, uint8_t *p_data, uint32_t length);
uint32_t eepromGetLength(void);
bool     eepromIsErasing(void);                               // V251112R8: 플래시 에뮬 클린업 진행 여부
bool     eepromFormat(void);


#endif


#ifdef __cplusplus
}
#endif

#endif 
