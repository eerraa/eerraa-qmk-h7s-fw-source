#include "qmk.h"
#include "qmk/port/port.h"
#include "qmk/port/platforms/eeprom.h"            // V251112R5: EEPROM 버스트 모드 제어
#include "qmk/port/debounce_profile.h"
#ifdef ERA_MACRO_ENABLE
#    include "qmk/port/era_macro.h"
#endif


static void cliQmk(cli_args_t *args);
static void idle_task(void);

static bool is_suspended = false;




bool qmkInit(void)
{
  if (!eeprom_is_ready()) return false;
  if (!eeconfig_is_enabled() && !eeprom_apply_factory_defaults(true)) return false;
#ifdef ERA_MACRO_ENABLE
  era_macro_cancel();
#endif
  via_hid_init();
  debounce_profile_init();                         // V251115R1: VIA 디바운스 프로필 초기 로드
#ifdef G_TERM_ENABLE
  tapping_term_init();                             // V251123R4: VIA TAPPING 설정 초기 로드
#endif
#ifdef TAPDANCE_ENABLE
  tapdance_init();                                 // V251124R8: VIA TAPDANCE 설정 초기 로드
#endif
#ifdef MOUSEKEY_ENABLE
  mousekey_config_init();                          // V260823R1: VIA MOUSE 설정 초기 로드 (keyboard_init 전에 mk_* 반영)
#endif
#ifdef RGBLIGHT_ENABLE
  rgb_sleep_init();                                // V260901R1: VIA RGB SLEEP 타임아웃 로드
#endif

  keyboard_setup();
  keyboard_init();

  
  is_suspended = usbIsSuspended();

  logPrintf("[  ] qmkInit()\n");
  logPrintf("     MATRIX_ROWS : %d\n", MATRIX_ROWS);
  logPrintf("     MATRIX_COLS : %d\n", MATRIX_COLS);
  const debounce_profile_values_t *profile = debounce_profile_current();
  logPrintf("     DEBOUNCE    : mode %d, pre %d ms, post %d ms\n",
            profile->type,
            profile->pre_ms,
            profile->post_ms);                    // V251115R1: VIA 런타임 디바운스 상태 로그

  cliAdd("qmk", cliQmk);
  return true;
}

void qmkUpdate(void)
{
#ifdef ERA_MACRO_ENABLE
  usb_hid_session_t macro_session = usbHidGetSession();
  era_macro_session(macro_session.generation, macro_session.valid, macro_session.suspended);
#endif
  keyboard_task();
#ifdef ERA_MACRO_ENABLE
  macro_session = usbHidGetSession();
  era_macro_session(macro_session.generation, macro_session.valid, macro_session.suspended);
  era_macro_task();
#endif
#ifdef _USE_HW_WS2812
  ws2812Task();                                      // V260910R6: RGB DMA 완료 후 대기 중 최신 프레임을 비차단 전송
#endif
  via_hid_task();  // V260909R1: 입력 처리 후 한 명령만 실행
  eeprom_task();
  idle_task();
}

void keyboard_post_init_user(void)
{
#ifdef KILL_SWITCH_ENABLE
  kill_switch_init();
#endif
#ifdef KKUK_ENABLE
  kkuk_init();
#endif
}

bool process_record_user(uint16_t keycode, keyrecord_t *record)
{
  // Kill switch has no record step: it resolves on the outgoing report (port/kill_switch.c).
#ifdef KKUK_ENABLE
  kkuk_process(keycode, record);
#endif
  return true;
}

void idle_task(void)
{
  bool is_suspended_cur;

  is_suspended_cur = usbIsSuspended();
  if (is_suspended_cur != is_suspended)
  {
    if (is_suspended_cur)
    {
      suspend_power_down();
    }
    else
    {
      suspend_wakeup_init();
    }

    is_suspended = is_suspended_cur;
  }

#ifdef KKUK_ENABLE
  kkuk_idle();
#endif
#ifdef RGBLIGHT_ENABLE
  rgb_sleep_task();                                // V260901R1: 유휴·Suspend·호스트 소실을 한 RGB owner가 적용
#endif
}

void cliQmk(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 2 && args->isStr(0, "clear") && args->isStr(1, "eeprom"))
  {
    bool cleared = eeprom_apply_factory_defaults(true);
    cliPrintf(cleared ? "Clearing EEPROM\n" : "EEPROM clear failed\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("qmk info\n");
    cliPrintf("qmk clear eeprom\n");
  }
}
