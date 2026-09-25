cmake_minimum_required(VERSION 2.8.12)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")
project(DEKOTOFUSOEM C)

if(WIN32)
  set(OS "win32")
  include_directories(SOEM/oshw/win32/wpcap/Include)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    link_directories(${CMAKE_SOURCE_DIR}/SOEM/oshw/win32/wpcap/Lib/x64)
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    link_directories(${CMAKE_SOURCE_DIR}/SOEM/oshw/win32/wpcap/Lib)
  endif()
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /D _CRT_SECURE_NO_WARNINGS")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}  /WX")
  set(OS_LIBS soem.lib wpcap.lib Packet.lib Ws2_32.lib Winmm.lib)
  file(GLOB DEKOTOFU_DEF dekotofu/dekotofusoem.def)
elseif(UNIX AND NOT APPLE)
  set(OS "linux")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
  set(OS_LIBS soem pthread rt)
  file(GLOB DEKOTOFU_DEF dekotofu/dekotofusoem.map)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,--version-script=${DEKOTOFU_DEF}")
elseif(APPLE)
  set(OS "macosx")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
  set(OS_LIBS soem pthread pcap)
  file(GLOB DEKOTOFU_DEF dekotofu/dekotofusoem.map)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,--version-script=${DEKOTOFU_DEF}")
endif()

message("OS is ${OS}")

link_directories(${CMAKE_SOURCE_DIR}/SOEM/build)
file(GLOB DEKOTOFU_SOURCES dekotofu/*.c)

add_library(dekotofusoem SHARED
  ${DEKOTOFU_SOURCES}
  ${DEKOTOFU_DEF})
target_link_libraries(dekotofusoem ${OS_LIBS})

target_include_directories(dekotofusoem PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/SOEM/soem>
  $<INSTALL_INTERFACE:include/dekotofusoem>)

target_include_directories(dekotofusoem PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/SOEM/osal>
  $<INSTALL_INTERFACE:include/dekotofusoem>)

target_include_directories(dekotofusoem PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/SOEM/osal/${OS}>
  $<INSTALL_INTERFACE:include/dekotofusoem>)

target_include_directories(dekotofusoem PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/SOEM/oshw/${OS}>
  $<INSTALL_INTERFACE:include/dekotofusoem>)
