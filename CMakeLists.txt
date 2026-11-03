cmake_minimum_required(VERSION 3.13)
find_package(Python3 REQUIRED)
set(PYTHON_EXECUTABLE ${Python3_EXECUTABLE})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools")
include(arm-none-eabi-gcc)


# 프로젝트 이름은 최종 출력 파일 이름으로 사용된다.
#
set(PRJ_NAME eerraa-qmk-h7s)


project(${PRJ_NAME}
  LANGUAGES ASM C CXX
)


set(EXECUTABLE ${PRJ_NAME}.elf)

# 배포 구성은 HID 전용이다. ON이면 같은 HID 3개 인터페이스에 CDC를 더한 VCOM 복합 장치를 빌드한다.
option(ERA_USB_VCOM "Build the HID + CDC composite (VCOM) USB configuration" OFF)


include(src/ap/modules/qmk/CMakeLists.txt)

# 지정한 폴더에 있는 파일만 포함한다.
#
file(GLOB SRC_FILES CONFIGURE_DEPENDS
  src/*.cpp
  src/*.c
  src/ap/*.cpp
  src/ap/*.c
  src/bsp/*.c
  src/bsp/device/*.c

)

# 하위폴더에 있는 파일까지 포함한다.
#
file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
  src/bsp/*.s
  src/common/*.c
  src/hw/*.c
  src/lib/ST/STM32H7RSxx_HAL_Driver/Src/*.c
)

# 특정 폴더를 빌드에서 제외한다.
#
set(EXCLUDE_PATHS 
)

foreach(TMP_PATH ${SRC_FILES_RECURSE}) 
  foreach(EXCLUDE_PATH ${EXCLUDE_PATHS}) 
    string(FIND ${TMP_PATH} ${EXCLUDE_PATH} RESULT) 
    if(NOT ${RESULT} EQUAL -1) 
      list(REMOVE_ITEM SRC_FILES_RECURSE ${TMP_PATH}) 
    endif() 
  endforeach(EXCLUDE_PATH) 
endforeach(TMP_PATH)

if(ERA_USB_VCOM)
  list(FILTER SRC_FILES_RECURSE EXCLUDE REGEX "/usb/usb_cdc/cdc_disabled\\.c$")
else()
  list(FILTER SRC_FILES_RECURSE EXCLUDE REGEX "/usb/usb_cdc/cdc_acm\\.c$")
endif()

add_executable(${EXECUTABLE} 
  ${SRC_FILES}
  ${SRC_FILES_RECURSE}
  ${QMK_SRC_FILES}
)



# Match the pinned full-newlib multilib speed build, independently of the
# firmware target's optimization and Cortex-M7 scheduling flags.
set(ERA_NEWLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/newlib)
add_library(era_newlib_memory OBJECT
  ${ERA_NEWLIB_DIR}/machine/arm/memcpy.S
  ${ERA_NEWLIB_DIR}/string/memset.c
  ${ERA_NEWLIB_DIR}/string/memmove.c
  ${ERA_NEWLIB_DIR}/string/memcmp.c
)
target_include_directories(era_newlib_memory PRIVATE ${ERA_NEWLIB_DIR}/glue)
target_compile_options(era_newlib_memory PRIVATE
  -march=armv7e-m
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard
  -O2
  -fno-builtin
  -fno-tree-loop-distribute-patterns
  -fdata-sections
  -ffunction-sections
  -fstack-usage
  -Wmissing-field-initializers
  -Wuninitialized
  -Wall
  -g3
)
target_sources(${EXECUTABLE} PRIVATE $<TARGET_OBJECTS:era_newlib_memory>)
target_link_options(${EXECUTABLE} PRIVATE
  -Wl,--undefined=memcpy,--undefined=memset,--undefined=memmove,--undefined=memcmp
)
# Libraries must follow the project objects: an earlier -lc can extract
# nano's memset to satisfy crt0 before seeing our strong definition.
target_link_libraries(${EXECUTABLE} PRIVATE c m stdc++ supc++)


target_include_directories(${EXECUTABLE} PRIVATE 
  src  
  src/ap
  src/ap/modules
  src/bsp
  src/bsp/device
  src/common 
  src/common/core
  src/common/hw/include 
  src/hw
  src/hw/driver
  src/lib

  src/hw/driver/usb
  src/hw/driver/usb/usb_cdc
  src/hw/driver/usb/usb_hid
  src/hw/driver/eeprom

  src/lib/ST/CMSIS/Include
  src/lib/ST/CMSIS/Device/ST/STM32H7RSxx/Include
  src/lib/ST/STM32H7RSxx_HAL_Driver/Inc
  ${QMK_INC_DIR}
)

target_compile_definitions(${EXECUTABLE} PRIVATE
  -DSTM32H7S3xx
  -DUSE_HAL_DRIVER
  )

if (ERA_USB_VCOM)
  target_compile_definitions(${EXECUTABLE} PRIVATE _USE_HW_VCOM)
endif()

target_compile_options(${EXECUTABLE} PRIVATE
  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard

  -fdata-sections
  -ffunction-sections
  
  -fstack-usage 
  -Wmissing-field-initializers
  -Wuninitialized 
  # -Wenum-conversion

  -Wall
  -g3
  -O2
  )

target_link_options(${EXECUTABLE} PRIVATE
  -T../src/bsp/ldscript/STM32H7S3V8TX_FLASH.ld

  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard
  -specs=nano.specs
  -fno-rtti
  # -lnosys
  -Wl,-Map=${PRJ_NAME}.map,--cref
  -Wl,--gc-sections
  -Xlinker -print-memory-usage -Xlinker
  )

set (CMAKE_C_FLAGS "-Wenum-conversion")
set (CMAKE_CXX_FLAGS "-fno-rtti")

message(${CMAKE_CXX_COMPILER_VERSION})
if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 11)
  string(APPEND CMAKE_EXE_LINKER_FLAGS "-Wl,--no-warn-rwx-segments")
endif()

add_custom_command(TARGET ${EXECUTABLE} 
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ARGS -O binary ${EXECUTABLE} ${PROJECT_NAME}.bin
  COMMENT "Invoking: Make Bin -> ${KEYBOARD_PATH}"
  )  

  
  file(READ "${QMK_KEYBOARD_PATH}/config.h" config_in)
  string(REPLACE "\"" "" config_out ${config_in})
  string(REGEX MATCH "KBD_NAME * ([-A-Za-z0-9.]*)" _ ${config_out})
  set(KBD_NAME ${CMAKE_MATCH_1})
  
  file(READ "src/hw/hw_def.h" version_in)
  string(REPLACE "\"" "" version_out ${version_in})
  string(REGEX MATCH "_DEF_FIRMWARE_VERSION * ([-A-Za-z0-9]*)" _ ${version_out})
  set(FIRM_VER ${CMAKE_MATCH_1})
  
  message("${KBD_NAME}")
  message("${FIRM_VER}")


add_custom_command(TARGET ${EXECUTABLE} 
  POST_BUILD
  COMMAND ${PYTHON_EXECUTABLE} ARGS ${CMAKE_CURRENT_SOURCE_DIR}/tools/uf2/uf2conv.py ARGS ${PROJECT_NAME}.bin --family 0xFFFF0002 --convert --output ${KBD_NAME}-${FIRM_VER}.uf2
  COMMENT "Invoking: Make UF2"
  )    
