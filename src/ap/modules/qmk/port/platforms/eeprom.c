#include "quantum.h"
#include "usb.h"                                   // V251112R5: VIA EEPROM 클리어 시 BootMode 기본값 적용
#include "bootloader.h"                            // V250310R6: VIA CLEAN 이후 응답 송신 보장 리셋 래퍼
#include "eeprom_reset_guard.h"
#include "qmk/quantum/eeconfig.h"
#include "qmk/port/port.h"


// V260909R1: RAM 최신 이미지 + byte dirty bitmap. pending 데이터는 ACK 이전/실패 시 절대 제거하지 않는다.
#define EEPROM_WRITE_PAGE_SIZE 32U
#define EEPROM_PAGE_COUNT ((TOTAL_EEPROM_BYTE_COUNT + EEPROM_WRITE_PAGE_SIZE - 1U) / EEPROM_WRITE_PAGE_SIZE)
#define EEPROM_SCAN_PAGES_PER_CALL 8U
#define EEPROM_FLUSH_STALL_TIMEOUT_MS 200U
#define EEPROM_FAILURE_BACKOFF_MS 10U
static uint8_t eeprom_buf[TOTAL_EEPROM_BYTE_COUNT];
static uint32_t dirty_pages[EEPROM_PAGE_COUNT];
static uint8_t page_snapshot[EEPROM_WRITE_PAGE_SIZE];
static uint32_t pending_bytes, pending_max, failure_count, invalid_accesses, completed_pages;
static uint32_t page_cursor, active_page, active_length, retry_after_ms;
static bool image_ready, write_active, retry_pending;
static bool commit_pending, commit_started, commit_rejected;
static uintptr_t commit_address;
static uint8_t commit_value, commit_previous;

__attribute__((weak)) void eeprom_note_change(uint32_t address, uint32_t length) { (void)address; (void)length; }
bool eeprom_commit_is_pending(void) { return commit_pending; }
bool eeprom_commit_failed(void) { return commit_rejected; }
bool eeprom_prepare_commit(uint8_t *addr, uint8_t invalid)
{
  if (!image_ready || (uintptr_t)addr >= TOTAL_EEPROM_BYTE_COUNT) { commit_rejected = true; return false; }
  commit_rejected = false;
  eeprom_update_byte(addr, invalid);
  bool ok = !eeprom_byte_is_pending((uintptr_t)addr);
  if (!ok && __get_IPSR() == 0U && __get_PRIMASK() == 0U) {
    uint32_t begin_ms = millis();
    while (eeprom_byte_is_pending((uintptr_t)addr)) {
      if ((uint32_t)(millis() - begin_ms) >= EEPROM_FLUSH_STALL_TIMEOUT_MS) {
        failure_count++;
        break;
      }
      // 이미 제출한 snapshot은 마친 뒤 marker만 우선 검증한다. 다른 SAVE를 여기서 비우지 않는다.
      page_cursor = (uintptr_t)addr / EEPROM_WRITE_PAGE_SIZE;
      eeprom_update();
    }
    ok = !eeprom_byte_is_pending((uintptr_t)addr);
  }
  commit_rejected = !ok;
  return ok;
}
bool eeprom_byte_is_pending(uintptr_t address)
{
  return address < TOTAL_EEPROM_BYTE_COUNT && (dirty_pages[address / EEPROM_WRITE_PAGE_SIZE] & (1UL << (address % EEPROM_WRITE_PAGE_SIZE))) != 0U;
}
bool eeprom_commit_byte(uint8_t *addr, uint8_t value)
{
  if (commit_rejected || !image_ready || (uintptr_t)addr >= TOTAL_EEPROM_BYTE_COUNT) return false;
  if (commit_pending) return commit_address == (uintptr_t)addr && commit_value == value;
  if (pending_bytes == 0U && eeprom_buf[(uintptr_t)addr] == value) return true;
  commit_address = (uintptr_t)addr;
  commit_previous = eeprom_buf[commit_address];
  commit_value = value;
  commit_pending = true;
  commit_started = false;
  return true;
}
_Static_assert(TOTAL_EEPROM_BYTE_COUNT <= 16384U, "EEPROM image exceeds chip");

static bool eeprom_address_valid(uintptr_t addr, size_t length)
{
  return addr <= TOTAL_EEPROM_BYTE_COUNT && length <= TOTAL_EEPROM_BYTE_COUNT - addr;
}

bool eeprom_is_ready(void) { return image_ready; }

static void eeprom_write_reset_guard(void)
{
#ifdef VIA_ENABLE
  eeprom_write_dword((uint32_t *)EECONFIG_USER_RESET_GUARD_MAGIC, ERA_EEPROM_RESET_GUARD_MAGIC);
  eeprom_write_dword((uint32_t *)EECONFIG_USER_RESET_GUARD_KEY, ERA_EEPROM_RESET_KEY);
#endif
}


bool eeprom_init(void)
{
  // Reload must preserve pending intent and compare the values exposed by GET.
  if (eeprom_is_pending() && !eeprom_flush_pending()) return false;
  bool was_ready = image_ready;
  memset(dirty_pages, 0, sizeof(dirty_pages));
  bool loaded = true;
  for (uint32_t page = 0; page < EEPROM_PAGE_COUNT; page++) {
    uint32_t address = page * EEPROM_WRITE_PAGE_SIZE;
    uint32_t length = TOTAL_EEPROM_BYTE_COUNT - address;
    if (length > EEPROM_WRITE_PAGE_SIZE) length = EEPROM_WRITE_PAGE_SIZE;
    if (!eepromRead(address, page_snapshot, length)) { loaded = false; break; }
    for (uint32_t i = 0; i < length; i++) {
      uint8_t before = was_ready ? eeprom_buf[address + i] : 0xFFU;
      if (before != page_snapshot[i]) dirty_pages[page] |= 1UL << i;
    }
    memcpy(&eeprom_buf[address], page_snapshot, length);
    if (was_ready) {
      for (uint32_t i = 0; i < length; i++)
        if (dirty_pages[page] & (1UL << i)) eeprom_note_change(address + i, 1U);
    }
  }
  image_ready = loaded;
  if (!was_ready && loaded) {
    // An initial image stays hidden until the complete read succeeds.
    for (uint32_t address = 0; address < TOTAL_EEPROM_BYTE_COUNT; address++)
      if (dirty_pages[address / EEPROM_WRITE_PAGE_SIZE] & (1UL << (address % EEPROM_WRITE_PAGE_SIZE)))
        eeprom_note_change(address, 1U);
  } else if (was_ready && !loaded) {
    // Read failures expose FF, including pages which were not read this time.
    for (uint32_t address = 0; address < TOTAL_EEPROM_BYTE_COUNT; address++)
      if (eeprom_buf[address] != 0xFFU) eeprom_note_change(address, 1U);
  }
  memset(dirty_pages, 0, sizeof(dirty_pages));
  if (!loaded) { failure_count++; return false; }
  pending_bytes = 0U;
  page_cursor = 0U;
  write_active = retry_pending = false;
  commit_rejected = false;
  return true;
}

static void eeprom_stage_byte(uintptr_t offset, uint8_t value)
{
  if (eeprom_buf[offset] == value) return;
  eeprom_buf[offset] = value;
  uint32_t page = offset / EEPROM_WRITE_PAGE_SIZE;
  uint32_t bit = 1UL << (offset % EEPROM_WRITE_PAGE_SIZE);
  if (!(dirty_pages[page] & bit)) {
    dirty_pages[page] |= bit;
    pending_bytes++;
    if (pending_bytes > pending_max) pending_max = pending_bytes;
  }
}

static void eeprom_complete_page(void)
{
  uint32_t addr = active_page * EEPROM_WRITE_PAGE_SIZE;
  // 전송 중 바뀐 byte는 dirty로 남긴다. 예전 완료가 새로운 저장 의도를 지울 수 없다.
  for (uint32_t i = 0; i < active_length; i++) {
    uint32_t bit = 1UL << i;
    if ((dirty_pages[active_page] & bit) && eeprom_buf[addr + i] == page_snapshot[i]) {
      dirty_pages[active_page] &= ~bit;
      pending_bytes--;
    }
  }
  completed_pages++;
}

void eeprom_update(void)
{
  if (!image_ready || eepromIsErasing()) return;
  if (pending_bytes == 0U) {
    if (!commit_pending) return;
    if (commit_started) {
      commit_pending = false;
      if (commit_previous != eeprom_buf[commit_address]) eeprom_note_change(commit_address, 1U);
      return;
    }
    uintptr_t address = commit_address;
    uint8_t value = commit_value;
    // The completion byte stays hidden until its verified receipt.
    eeprom_stage_byte(address, value);
    commit_started = true;
    return;
  }
  if (write_active) {
#if defined(EEPROM_CHIP_ZD24C128)
    eeprom_async_result_t result = eepromWritePagePoll();
    if (result == EEPROM_ASYNC_BUSY) return;
    write_active = false;
    if (result == EEPROM_ASYNC_DONE) {
      eeprom_complete_page();
    } else {
      failure_count++;
      retry_after_ms = millis() + EEPROM_FAILURE_BACKOFF_MS;
      retry_pending = true;
    }
#endif
    return;  // 한 호출에 완료 처리 또는 시작 중 한 단계만 수행한다.
  }
  if (retry_pending) {
    if ((int32_t)(millis() - retry_after_ms) < 0) return;
    retry_pending = false;
  }
  // 전체 이미지를 매 loop 순회하지 않는다. 최대8 page 확인 후 다음 입력 처리로 돌아간다.
  for (uint32_t scanned = 0; scanned < EEPROM_SCAN_PAGES_PER_CALL; scanned++) {
    uint32_t page = page_cursor;
    page_cursor = (page_cursor + 1U) % EEPROM_PAGE_COUNT;
    if (dirty_pages[page] == 0U) continue;
    uint32_t addr = page * EEPROM_WRITE_PAGE_SIZE;
    active_page = page;
    active_length = TOTAL_EEPROM_BYTE_COUNT - addr;
    if (active_length > EEPROM_WRITE_PAGE_SIZE) active_length = EEPROM_WRITE_PAGE_SIZE;
    memcpy(page_snapshot, &eeprom_buf[addr], active_length);
#if defined(EEPROM_CHIP_ZD24C128)
    write_active = eepromWritePageStart(addr, page_snapshot, active_length);
    if (!write_active) {
      failure_count++;
      retry_after_ms = millis() + EEPROM_FAILURE_BACKOFF_MS;
      retry_pending = true;
    }
#else
    // 미검증 flash-emulation 빌드는 기존 동기 backend를 유지한다. 외부 EEPROM과 같은 시간 보장은 없다.
    uint8_t verify[EEPROM_WRITE_PAGE_SIZE];
    if (eepromWritePage(addr, page_snapshot, active_length) && eepromRead(addr, verify, active_length)
        && memcmp(verify, page_snapshot, active_length) == 0) eeprom_complete_page();
    else { failure_count++; retry_after_ms = millis() + EEPROM_FAILURE_BACKOFF_MS; retry_pending = true; }
#endif
    return;
  }
}

bool eeprom_is_pending(void) { return pending_bytes != 0U || commit_pending; }

bool eeprom_flush_pending(void)
{
  // V260909R1: 부팅/명시적 reset의 durability barrier만 대기한다. 일반 SAVE는 비동기다.
  if (!image_ready || __get_IPSR() != 0U || __get_PRIMASK() != 0U) return false;
  uint32_t last_progress_ms = millis();
  uint32_t last_completed = completed_pages;
  while (eeprom_is_pending()) {
    eeprom_update();
    if (completed_pages != last_completed) {
      last_completed = completed_pages;
      last_progress_ms = millis();
    } else if ((uint32_t)(millis() - last_progress_ms) >= EEPROM_FLUSH_STALL_TIMEOUT_MS) {
      failure_count++;
      return false;  // dirty 및 in-flight 소유권을 유지해 다음 service에서 계속 처리한다.
    }
  }
  return true;
}

bool eeprom_apply_factory_defaults(bool write_reset_guard)
{
  if (eeprom_flush_pending() != true)                                      // V251112R3: 초기화 전 대기열 제거
  {
    return false;
  }

  /* Retire the previous validity before any default can reach hardware. */
#ifdef VIA_ENABLE
  if (!eepromResetGuardInvalidate()) return false;
#endif
  if (!eeconfig_init_quantum_checked()) return false;
  if (eeprom_flush_pending() != true)
  {
    return false;
  }

#if (EECONFIG_USER_DATA_SIZE) == 0
#ifdef BOOTMODE_ENABLE
  usbBootModeApplyDefaults();                               // V251114R4: USER 블록이 없을 때만 기본값 백업 적용
#endif
#endif
  if (eeprom_flush_pending() != true)
  {
    return false;
  }

  if (write_reset_guard)
  {
    eeprom_write_reset_guard();                                // 기본값이 모두 기록된 뒤 guard를 마지막에 쓴다
    if (eeprom_flush_pending() != true)
    {
      return false;
    }
  }

  eeconfig_publish_quantum_defaults();
  return true;
}

#ifdef VIA_ENABLE
static enum {
  EEPROM_CLEAN_IDLE,
  EEPROM_CLEAN_DRAINING,
  EEPROM_CLEAN_INVALIDATING,
  EEPROM_CLEAN_RESET_QUEUED,
} clean_state;

static bool eeprom_clean_service(void)
{
  if (clean_state == EEPROM_CLEAN_DRAINING && !eeprom_is_pending())
  {
    eepromResetGuardStageInvalidation();
    clean_state = EEPROM_CLEAN_INVALIDATING;
  }
  if (clean_state == EEPROM_CLEAN_INVALIDATING && !eeprom_is_pending())
  {
    // 실패 뒤 늦게 무효화가 완료되어도 reset 의도와 응답 송신 유예를 보존한다.
    if (!mcu_reset_deferred()) return false;
    clean_state = EEPROM_CLEAN_RESET_QUEUED;
  }
  return clean_state == EEPROM_CLEAN_RESET_QUEUED;
}
#endif

void eeprom_task(void)
{
  eeprom_update();
#ifdef VIA_ENABLE
  (void)eeprom_clean_service();
#endif
}

bool eeprom_req_clean(void)
{
#ifdef VIA_ENABLE
  if (!image_ready || __get_IPSR() != 0U || __get_PRIMASK() != 0U) return false;
  if (clean_state == EEPROM_CLEAN_RESET_QUEUED) return true;
  if (clean_state == EEPROM_CLEAN_IDLE) clean_state = EEPROM_CLEAN_DRAINING;
  // 명시적 요청만 barrier를 기다린다. 실패 의도는 eeprom_task가 비차단으로 계속 처리한다.
  if (!eeprom_flush_pending()) return false;
  if (eeprom_clean_service()) return true;
  if (!eeprom_flush_pending()) return false;
  return eeprom_clean_service();
#else
  logPrintf("[!] VIA EEPROM clear : reset guard needs VIA_ENABLE\n");
  return false;
#endif
}

uint8_t eeprom_read_byte(const uint8_t *addr)
{
  uintptr_t offset = (uintptr_t)addr;
  if (!image_ready || !eeprom_address_valid(offset, 1U)) { invalid_accesses++; return 0xFFU; }
  if (commit_pending && offset == commit_address) return commit_previous;
  return eeprom_buf[offset];
}

uint16_t eeprom_read_word(const uint16_t *addr)
{
  uintptr_t offset = (uintptr_t)addr;
  if (!eeprom_address_valid(offset, 2U)) { invalid_accesses++; return UINT16_MAX; }
  return (uint16_t)eeprom_read_byte((const uint8_t *)offset) |
         ((uint16_t)eeprom_read_byte((const uint8_t *)(offset + 1U)) << 8);
}

uint32_t eeprom_read_dword(const uint32_t *addr)
{
  uintptr_t offset = (uintptr_t)addr;
  if (!eeprom_address_valid(offset, 4U)) { invalid_accesses++; return UINT32_MAX; }
  uint32_t result = 0U;
  for (uint32_t i = 0; i < 4U; i++) result |= (uint32_t)eeprom_read_byte((const uint8_t *)(offset + i)) << (8U * i);
  return result;
}

void eeprom_read_block(void *buf, const void *addr, uint32_t len)
{
  if (buf == NULL) { invalid_accesses++; return; }
  if (!image_ready || !eeprom_address_valid((uintptr_t)addr, len)) {
    invalid_accesses++;
    memset(buf, 0xFF, len);
    return;
  }
  memcpy(buf, &eeprom_buf[(uintptr_t)addr], len);
  if (commit_pending && commit_address >= (uintptr_t)addr && commit_address - (uintptr_t)addr < len)
    ((uint8_t *)buf)[commit_address - (uintptr_t)addr] = commit_previous;
}

void eeprom_write_byte(uint8_t *addr, uint8_t value)
{
  uintptr_t offset = (uintptr_t)addr;
  if (!image_ready || !eeprom_address_valid(offset, 1U)) { invalid_accesses++; return; }
  uint8_t before = eeprom_read_byte(addr);
  if (commit_pending && offset == commit_address) commit_pending = commit_started = false;
  eeprom_stage_byte(offset, value);
  if (before != eeprom_read_byte(addr)) eeprom_note_change(offset, 1U);
}

void eeprom_write_word(uint16_t *addr, uint16_t value)
{
  if (!eeprom_address_valid((uintptr_t)addr, 2U)) { invalid_accesses++; return; }
  for (uint32_t i = 0; i < 2U; i++) eeprom_write_byte((uint8_t *)((uintptr_t)addr + i), value >> (8U * i));
}

void eeprom_write_dword(uint32_t *addr, uint32_t value)
{
  if (!eeprom_address_valid((uintptr_t)addr, 4U)) { invalid_accesses++; return; }
  for (uint32_t i = 0; i < 4U; i++) eeprom_write_byte((uint8_t *)((uintptr_t)addr + i), value >> (8U * i));
}

void eeprom_write_block(const void *buf, void *addr, size_t len)
{
  if (buf == NULL || !eeprom_address_valid((uintptr_t)addr, len)) { invalid_accesses++; return; }
  const uint8_t *src = buf;
  for (size_t i = 0; i < len; i++) eeprom_write_byte((uint8_t *)((uintptr_t)addr + i), src[i]);
}

void eeprom_update_byte(uint8_t *addr, uint8_t value) { eeprom_write_byte(addr, value); }
void eeprom_update_word(uint16_t *addr, uint16_t value) { eeprom_write_word(addr, value); }
void eeprom_update_dword(uint32_t *addr, uint32_t value) { eeprom_write_dword(addr, value); }
void eeprom_update_block(const void *buf, void *addr, size_t len) { eeprom_write_block(buf, addr, len); }
uint32_t eeprom_get_write_pending_count(void) { return pending_bytes; }
uint32_t eeprom_get_write_pending_max(void) { return pending_max; }
uint32_t eeprom_get_write_overflow_count(void) { return 0U; }  // 호환 조회: dirty bitmap에는 queue-full이 없다.
uint32_t eeprom_get_write_failure_count(void) { return failure_count; }
uint32_t eeprom_get_invalid_access_count(void) { return invalid_accesses; }
