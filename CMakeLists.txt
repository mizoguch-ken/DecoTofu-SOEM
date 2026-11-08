cmake_minimum_required(VERSION 3.28)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")
project(DECOTOFUSOEM C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

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
elseif(UNIX AND NOT APPLE)
  set(OS "linux")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
  set(OS_LIBS soem pthread rt)
elseif(APPLE)
  set(OS "macosx")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
  set(OS_LIBS soem pthread pcap)
  # --version-script is a GNU ld / ELF linker option. macOS links with Mach-O
  # ld, which rejects it, so the generated ELF export map (decotofusoem.map,
  # built from decotofu/decotofusoem.exports) cannot be used here. jnr-ffi
  # dlopens this library and resolves names by lookup, so exporting every
  # defined symbol is sufficient - no hiding is applied.
  # To hide symbols on macOS instead, keep a Mach-O list file (one plain
  # symbol per line, C names prefixed with '_') and use
  #   set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-exported_symbols_list,${LIST}")
endif()

message("OS is ${OS}")

# decotofu/decotofusoem.exports is the single list of names this library hands
# out: the nine functions of the shim plus the SOEM functions that are linked
# statically from soem.lib and re-exported. jnr-ffi resolves them by name on
# the JVM side, so the list is an ABI. The platform specific export maps are
# generated from it here, which is what keeps the Windows module definition
# file and the GNU ld version script from ever disagreeing, and what makes
# "export one more SOEM call" a one line change in that list.
file(STRINGS decotofu/decotofusoem.exports DECOTOFU_EXPORTS
     REGEX "^[A-Za-z_][A-Za-z0-9_]*$")
list(LENGTH DECOTOFU_EXPORTS DECOTOFU_EXPORT_COUNT)
message(STATUS "decotofusoem exports ${DECOTOFU_EXPORT_COUNT} symbols")
# the list is read at configure time, so changing it has to re-run CMake
set_property(DIRECTORY APPEND PROPERTY
             CMAKE_CONFIGURE_DEPENDS decotofu/decotofusoem.exports)

set(DECOTOFU_DEF "")
if(WIN32)
  set(DECOTOFU_DEF_CONTENT "LIBRARY\tDECOTOFUSOEM\nEXPORTS\n")
  foreach(symbol IN LISTS DECOTOFU_EXPORTS)
    string(APPEND DECOTOFU_DEF_CONTENT "\t${symbol}\n")
  endforeach()
  set(DECOTOFU_DEF "${CMAKE_BINARY_DIR}/decotofusoem.def")
  file(CONFIGURE OUTPUT "${DECOTOFU_DEF}" CONTENT "${DECOTOFU_DEF_CONTENT}" @ONLY)
elseif(UNIX AND NOT APPLE)
  set(DECOTOFU_DEF_CONTENT "{\n  global:\n")
  foreach(symbol IN LISTS DECOTOFU_EXPORTS)
    string(APPEND DECOTOFU_DEF_CONTENT "    ${symbol};\n")
  endforeach()
  string(APPEND DECOTOFU_DEF_CONTENT "  local:\n    *;\n};\n")
  set(DECOTOFU_DEF "${CMAKE_BINARY_DIR}/decotofusoem.map")
  file(CONFIGURE OUTPUT "${DECOTOFU_DEF}" CONTENT "${DECOTOFU_DEF_CONTENT}" @ONLY)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,--version-script=${DECOTOFU_DEF}")
endif()

link_directories(${CMAKE_SOURCE_DIR}/SOEM/build)
file(GLOB DECOTOFU_SOURCES decotofu/*.c)

add_library(decotofusoem SHARED
  ${DECOTOFU_SOURCES}
  ${DECOTOFU_DEF})
target_link_libraries(decotofusoem ${OS_LIBS})

target_include_directories(decotofusoem PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/SOEM/soem>
  $<INSTALL_INTERFACE:include/decotofusoem>)

target_include_directories(decotofusoem PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/SOEM/osal>
  $<INSTALL_INTERFACE:include/decotofusoem>)

target_include_directories(decotofusoem PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/SOEM/osal/${OS}>
  $<INSTALL_INTERFACE:include/decotofusoem>)

target_include_directories(decotofusoem PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/SOEM/oshw/${OS}>
  $<INSTALL_INTERFACE:include/decotofusoem>)
