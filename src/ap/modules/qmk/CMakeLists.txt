cmake_minimum_required(VERSION 3.13)
# H7S custom UI support revision: keyboard IN polling configuration label.
add_compile_definitions(VIA_FIRMWARE_VERSION=0x00000001)
set(QMK_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/ap/modules/qmk")

set (QMK_KEYBOARD_PATH ${QMK_ROOT_PATH}/${KEYBOARD_PATH})

if (DEFINED RGBLIGHT_ENABLE)
  set(_QMK_RGBLIGHT_CAN_AUTO FALSE)
else()
  set(_QMK_RGBLIGHT_CAN_AUTO TRUE)
  set(RGBLIGHT_ENABLE OFF)
endif()
set(_QMK_RGBLIGHT_DEFINED_BY_CONFIG FALSE)

set(_QMK_CONFIG_HEADER "${QMK_KEYBOARD_PATH}/config.h")
if (EXISTS "${_QMK_CONFIG_HEADER}")
  file(STRINGS "${_QMK_CONFIG_HEADER}" _QMK_DEBOUNCE_DEFINE REGEX "^[ \\t]*#define[ \\t]+DEBOUNCE[ \\t]+[0-9]+")
  file(STRINGS "${_QMK_CONFIG_HEADER}" _QMK_DEBOUNCE_TYPE_LINE REGEX "^[ \\t]*#define[ \\t]+DEBOUNCE_TYPE[ \\t]+[A-Za-z0-9_]+")

  if (NOT _QMK_DEBOUNCE_DEFINE OR NOT _QMK_DEBOUNCE_TYPE_LINE)
    message(FATAL_ERROR "V251115R4: ${_QMK_CONFIG_HEADER}에서 DEBOUNCE/DEBOUNCE_TYPE을 찾을 수 없습니다. 보드 기본 설정을 확인하세요.")
  endif()

  if (_QMK_RGBLIGHT_CAN_AUTO)
    file(STRINGS "${_QMK_CONFIG_HEADER}" _QMK_RGBLIGHT_DEFINE REGEX "^[ \\t]*#define[ \\t]+RGBLIGHT_ENABLE")
    if (_QMK_RGBLIGHT_DEFINE)
      set(RGBLIGHT_ENABLE ON)
      set(_QMK_RGBLIGHT_DEFINED_BY_CONFIG TRUE)
    endif()
  endif()

  if (_QMK_DEBOUNCE_DEFINE)
    string(REGEX REPLACE ".*#define[ \\t]+DEBOUNCE[ \\t]+([0-9]+).*" "\\1" _QMK_DEBOUNCE_VALUE "${_QMK_DEBOUNCE_DEFINE}")
    add_compile_definitions(QMK_DEFAULT_DEBOUNCE_DELAY=${_QMK_DEBOUNCE_VALUE})  # V251115R3: 보드 config.h DEBOUNCE 값 기본 적용
  endif()
  if (_QMK_DEBOUNCE_TYPE_LINE)
    string(REGEX REPLACE ".*#define[ \\t]+DEBOUNCE_TYPE[ \\t]+([A-Za-z0-9_]+).*" "\\1" _QMK_DEBOUNCE_TYPE "${_QMK_DEBOUNCE_TYPE_LINE}")
    add_compile_definitions(QMK_DEFAULT_DEBOUNCE_TYPE=${_QMK_DEBOUNCE_TYPE})    # V251115R3: 보드 config.h DEBOUNCE_TYPE 토큰 기본 적용
  endif()

  file(STRINGS "${_QMK_CONFIG_HEADER}" _QMK_TAPDANCE_DEFINE REGEX "^[ \\t]*#define[ \\t]+TAPDANCE_ENABLE")
  if (_QMK_TAPDANCE_DEFINE)
    list(APPEND QMK_ADD_FILES "${QMK_ROOT_PATH}/quantum/process_keycode/process_tap_dance.c")  # V251124R8: Tap Dance 처리 소스 포함
  endif()
endif()

if (RGBLIGHT_ENABLE)
  list(APPEND QMK_ADD_FILES "${QMK_ROOT_PATH}/quantum/rgblight/rgblight.c")
  list(APPEND QMK_ADD_FILES "${QMK_ROOT_PATH}/quantum/process_keycode/process_rgb.c")
  list(APPEND QMK_ADD_FILES "${QMK_KEYBOARD_PATH}/port/driver/rgblight_drivers.c")
endif()  


# 지정한 폴더에 있는 파일만 포함한다.
#
file(GLOB QMK_SRC_FILES CONFIGURE_DEPENDS
  ${QMK_ROOT_PATH}/*.c
  ${QMK_ROOT_PATH}/port/*.c
  ${QMK_ROOT_PATH}/port/platforms/*.c
  ${QMK_ROOT_PATH}/port/protocol/*.c

  ${QMK_ROOT_PATH}/quantum/quantum.c
  ${QMK_ROOT_PATH}/quantum/via.c
  ${QMK_ROOT_PATH}/quantum/keyboard.c
  ${QMK_ROOT_PATH}/quantum/action.c
  ${QMK_ROOT_PATH}/quantum/action_tapping.c
  ${QMK_ROOT_PATH}/quantum/action_util.c
  ${QMK_ROOT_PATH}/quantum/action_layer.c
  ${QMK_ROOT_PATH}/quantum/keycode_config.c
  ${QMK_ROOT_PATH}/quantum/led.c
  ${QMK_ROOT_PATH}/quantum/keymap_common.c
  ${QMK_ROOT_PATH}/quantum/eepconfig.c
  ${QMK_ROOT_PATH}/quantum/dynamic_keymap.c
  ${QMK_ROOT_PATH}/quantum/eeconfig.c
  ${QMK_ROOT_PATH}/quantum/keymap_introspection.c
  ${QMK_ROOT_PATH}/quantum/color.c


  ${QMK_ROOT_PATH}/quantum/logging/*.c
  ${QMK_ROOT_PATH}/quantum/debounce_runtime.c
  ${QMK_ROOT_PATH}/quantum/debounce/asym_eager_defer_pk.c
  ${QMK_ROOT_PATH}/quantum/debounce/sym_defer_pk.c
  ${QMK_ROOT_PATH}/quantum/debounce/sym_eager_pk.c
  

  ${QMK_ROOT_PATH}/quantum/send_string/*.c
  ${QMK_ROOT_PATH}/quantum/process_keycode/process_grave_esc.c
  ${QMK_ROOT_PATH}/quantum/process_keycode/process_magic.c
  ${QMK_ROOT_PATH}/quantum/process_keycode/process_space_cadet.c
  ${QMK_ROOT_PATH}/quantum/mousekey.c                        # V260823R1: 마우스 키 엔진


  # ${QMK_KEYBOARD_PATH}/*.c
  ${QMK_KEYBOARD_PATH}/port/*.c
  
  ${QMK_ADD_FILES}
)

# 하위폴더에 있는 파일까지 포함한다.
#
# file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
# )


set (QMK_INC_DIR 
  ${QMK_ROOT_PATH}/port
  ${QMK_ROOT_PATH}/port/platforms 
  ${QMK_ROOT_PATH}/port/protocol
  ${QMK_ROOT_PATH}/quantum
  ${QMK_ROOT_PATH}/quantum/logging
  ${QMK_ROOT_PATH}/quantum/keymap_extras
  ${QMK_ROOT_PATH}/quantum/sequencer
  ${QMK_ROOT_PATH}/quantum/send_string
  ${QMK_ROOT_PATH}/quantum/process_keycode
  ${QMK_ROOT_PATH}/quantum/rgblight

  ${QMK_KEYBOARD_PATH}
)

add_compile_definitions(DYNAMIC_KEYMAP_MACRO_DELAY=10)
add_compile_definitions(ERA_MACRO_ENABLE)

add_compile_definitions(VIA_ENABLE)
add_compile_definitions(RAW_ENABLE)
add_compile_definitions(DYNAMIC_KEYMAP_ENABLE)
# add_compile_definitions(HOLD_ON_OTHER_KEY_PRESS) // QMK와 디폴트와 매치하지 않음
add_compile_definitions(EXTRAKEY_ENABLE)
# QMK 기본 기능. EERRAA와 같이 모든 보드에서 Magic(GUI·Alt 스왑 등)과 Space Cadet 키코드를 처리한다.
add_compile_definitions(MAGIC_ENABLE)
add_compile_definitions(SPACE_CADET_ENABLE)

# V260823R1: 마우스 리포트는 EXK(shared) 인터페이스의 리포트 ID 2로 내보낸다.
#            인터페이스 0(키보드)은 손대지 않는다. NKRO는 도입하지 않았다 — 근거는 docs/contract_usb.md.
add_compile_definitions(MOUSEKEY_ENABLE)
add_compile_definitions(MOUSE_ENABLE)        # V260823R1: report.c의 마우스 리포트 헬퍼를 켠다
add_compile_definitions(MOUSE_SHARED_EP)
add_compile_definitions(ERA_MOUSEKEY_RUNTIME_DELTA)


add_compile_definitions(QMK_KEYBOARD_H="quantum.h")
add_compile_definitions(QMK_KEYMAP_CONFIG_H="${QMK_KEYBOARD_PATH}/config.h")
add_compile_definitions(KEYMAP_C="${QMK_KEYBOARD_PATH}/keymap.c")



if (RGBLIGHT_ENABLE AND NOT _QMK_RGBLIGHT_DEFINED_BY_CONFIG)
  add_compile_definitions(RGBLIGHT_ENABLE)
endif()

# V260901R1: RGB 절전 상태의 단일 owner는 port/rgb_sleep.c 이다.
#            quantum suspend 경로의 rgblight_suspend/wakeup과 겹치지 않게 한다.
if (RGBLIGHT_ENABLE)
  add_compile_definitions(ERA_RGB_SLEEP_OWNER)
endif()
