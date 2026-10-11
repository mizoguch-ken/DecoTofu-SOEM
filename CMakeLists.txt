cmake_minimum_required(VERSION 3.28)
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
elseif(UNIX AND NOT APPLE)
  set(OS "linux")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
  set(OS_LIBS soem pthread rt)
elseif(APPLE)
  set(OS "macosx")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
  set(OS_LIBS soem pthread pcap)
  # --version-script is a GNU ld / ELF linker option. macOS links with Mach-O
  # ld, which rejects it, so the generated ELF export map (dekotofusoem.map,
  # built from dekotofu/dekotofusoem.exports) cannot be used here. jnr-ffi
  # dlopens this library and resolves names by lookup, so exporting every
  # defined symbol is sufficient - no hiding is applied.
  # To hide symbols on macOS instead, keep a Mach-O list file (one plain
  # symbol per line, C names prefixed with '_') and use
  #   set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-exported_symbols_list,${LIST}")
endif()

message("OS is ${OS}")

# dekotofu/dekotofusoem.exports is the single list of names this library hands
# out: the six functions of the shim plus the SOEM functions that are linked
# statically from soem.lib and re-exported. jnr-ffi resolves them by name on
# the JVM side, so the list is an ABI. The platform specific export maps are
# generated from it here, which is what keeps the Windows module definition
# file and the GNU ld version script from ever disagreeing, and what makes
# "export one more SOEM call" a one line change in that list.
file(STRINGS dekotofu/dekotofusoem.exports DEKOTOFU_EXPORTS
     REGEX "^[A-Za-z_][A-Za-z0-9_]*$")
list(LENGTH DEKOTOFU_EXPORTS DEKOTOFU_EXPORT_COUNT)
message(STATUS "dekotofusoem exports ${DEKOTOFU_EXPORT_COUNT} symbols")
# the list is read at configure time, so changing it has to re-run CMake
set_property(DIRECTORY APPEND PROPERTY
             CMAKE_CONFIGURE_DEPENDS dekotofu/dekotofusoem.exports)

set(DEKOTOFU_DEF "")
if(WIN32)
  set(DEKOTOFU_DEF_CONTENT "LIBRARY\tDEKOTOFUSOEM\nEXPORTS\n")
  foreach(symbol IN LISTS DEKOTOFU_EXPORTS)
    string(APPEND DEKOTOFU_DEF_CONTENT "\t${symbol}\n")
  endforeach()
  set(DEKOTOFU_DEF "${CMAKE_BINARY_DIR}/dekotofusoem.def")
  file(CONFIGURE OUTPUT "${DEKOTOFU_DEF}" CONTENT "${DEKOTOFU_DEF_CONTENT}" @ONLY)
elseif(UNIX AND NOT APPLE)
  set(DEKOTOFU_DEF_CONTENT "{\n  global:\n")
  foreach(symbol IN LISTS DEKOTOFU_EXPORTS)
    string(APPEND DEKOTOFU_DEF_CONTENT "    ${symbol};\n")
  endforeach()
  string(APPEND DEKOTOFU_DEF_CONTENT "  local:\n    *;\n};\n")
  set(DEKOTOFU_DEF "${CMAKE_BINARY_DIR}/dekotofusoem.map")
  file(CONFIGURE OUTPUT "${DEKOTOFU_DEF}" CONTENT "${DEKOTOFU_DEF_CONTENT}" @ONLY)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,--version-script=${DEKOTOFU_DEF}")
endif()

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
