cmake_minimum_required(VERSION 3.28)
project(DECOTOFUSOEM VERSION 2.0.0 LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

set(SOEM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SOEM" CACHE PATH "Path to SOEM source tree")
set(SOEM_BUILD_DIR "${SOEM_DIR}/build" CACHE PATH "Path to SOEM build directory")
option(DECOTOFUSOEM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(DECOTOFU_INCLUDE_DIRS
  "${CMAKE_CURRENT_SOURCE_DIR}/decotofu"
  "${SOEM_DIR}/include"
  "${SOEM_BUILD_DIR}/include"
  "${SOEM_DIR}/osal"
)
set(DECOTOFU_LINK_DIRS
  "${SOEM_BUILD_DIR}"
)

if(WIN32)
  set(OS "win32")
  list(APPEND DECOTOFU_INCLUDE_DIRS
    "${SOEM_DIR}/oshw/win32/wpcap/Include"
  )
  list(APPEND DECOTOFU_LINK_DIRS
    "${SOEM_BUILD_DIR}/$<CONFIG>"
  )
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND DECOTOFU_LINK_DIRS "${SOEM_DIR}/oshw/win32/wpcap/Lib/x64")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    list(APPEND DECOTOFU_LINK_DIRS "${SOEM_DIR}/oshw/win32/wpcap/Lib")
  endif()
  set(OS_COMMON_LIBS wpcap Packet Ws2_32 Winmm)
elseif(UNIX AND NOT APPLE)
  set(OS "linux")
  set(OS_COMMON_LIBS pthread rt)
elseif(APPLE)
  set(OS "macosx")
  set(OS_COMMON_LIBS pthread pcap)
  # --version-script is a GNU ld / ELF linker option. macOS links with Mach-O
  # ld, which rejects it, so the generated ELF export map (decotofusoem.map,
  # built from decotofu/decotofusoem.exports) cannot be used here. jnr-ffi
  # dlopens this library and resolves names by lookup, so exporting every
  # defined symbol is sufficient - no hiding is applied.
  # To hide symbols on macOS instead, keep a Mach-O list file (one plain
  # symbol per line, C names prefixed with '_') and pass
  #   -Wl,-exported_symbols_list,${LIST}
  # via target_link_options().
endif()

list(APPEND DECOTOFU_INCLUDE_DIRS
  "${SOEM_DIR}/osal/${OS}"
  "${SOEM_DIR}/oshw/${OS}"
)

message(STATUS "OS is ${OS}")

# decotofu/decotofusoem.exports is the single list of names this library hands
# out: the shim functions in decotofu/decotofusoem.c plus the SOEM functions
# linked statically from soem.lib / libsoem.a and re-exported. jnr-ffi resolves
# them by name on the JVM side, so the list is an ABI. The platform specific
# export maps are generated from it here, keeping Windows and ELF in sync.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/decotofu/decotofusoem.exports" DECOTOFU_EXPORTS
     REGEX "^[A-Za-z_][A-Za-z0-9_]*$")
list(LENGTH DECOTOFU_EXPORTS DECOTOFU_EXPORT_COUNT)
if(DECOTOFU_EXPORT_COUNT EQUAL 0)
  message(FATAL_ERROR "decotofu/decotofusoem.exports contains no valid symbol names")
endif()
message(STATUS "decotofusoem exports ${DECOTOFU_EXPORT_COUNT} symbols")
# The list is read at configure time, so changing it has to re-run CMake.
set_property(DIRECTORY APPEND PROPERTY
             CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/decotofu/decotofusoem.exports")

set(DECOTOFU_DEF "")
set(DECOTOFU_MAP "")
if(WIN32)
  set(DECOTOFU_DEF_CONTENT "LIBRARY\tDECOTOFUSOEM\nEXPORTS\n")
  foreach(symbol IN LISTS DECOTOFU_EXPORTS)
    string(APPEND DECOTOFU_DEF_CONTENT "\t${symbol}\n")
  endforeach()
  set(DECOTOFU_DEF "${CMAKE_CURRENT_BINARY_DIR}/decotofusoem.def")
  file(CONFIGURE OUTPUT "${DECOTOFU_DEF}" CONTENT "${DECOTOFU_DEF_CONTENT}" @ONLY)
elseif(UNIX AND NOT APPLE)
  set(DECOTOFU_MAP_CONTENT "{\n  global:\n")
  foreach(symbol IN LISTS DECOTOFU_EXPORTS)
    string(APPEND DECOTOFU_MAP_CONTENT "    ${symbol};\n")
  endforeach()
  string(APPEND DECOTOFU_MAP_CONTENT "  local:\n    *;\n};\n")
  set(DECOTOFU_MAP "${CMAKE_CURRENT_BINARY_DIR}/decotofusoem.map")
  file(CONFIGURE OUTPUT "${DECOTOFU_MAP}" CONTENT "${DECOTOFU_MAP_CONTENT}" @ONLY)
endif()

set(DECOTOFU_SOURCES
  decotofu/decotofusoem.c
)

add_library(decotofusoem SHARED
  ${DECOTOFU_SOURCES}
  ${DECOTOFU_DEF}
)

if(UNIX)
  target_link_libraries(decotofusoem PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,soem>" ${OS_COMMON_LIBS})
else()
  target_link_libraries(decotofusoem PRIVATE soem ${OS_COMMON_LIBS})
endif()

target_link_directories(decotofusoem PRIVATE ${DECOTOFU_LINK_DIRS})

target_compile_definitions(decotofusoem PRIVATE
  $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
  DECOTOFUSOEM_VERSION_STRING="${PROJECT_VERSION}"
)
target_compile_options(decotofusoem PRIVATE
  $<$<C_COMPILER_ID:MSVC>:/W3 $<$<BOOL:${DECOTOFUSOEM_WARNINGS_AS_ERRORS}>:/WX>>
  $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>:-Wall -Wextra $<$<BOOL:${DECOTOFUSOEM_WARNINGS_AS_ERRORS}>:-Werror>>
)

if(UNIX AND NOT APPLE)
  target_link_options(decotofusoem PRIVATE "-Wl,--version-script=${DECOTOFU_MAP}")
  set_target_properties(decotofusoem PROPERTIES LINK_DEPENDS "${DECOTOFU_MAP}")
endif()

target_include_directories(decotofusoem PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/decotofu>
  $<BUILD_INTERFACE:${SOEM_DIR}/include>
  $<BUILD_INTERFACE:${SOEM_BUILD_DIR}/include>
  $<BUILD_INTERFACE:${SOEM_DIR}/osal>
  $<BUILD_INTERFACE:${SOEM_DIR}/osal/${OS}>
  $<BUILD_INTERFACE:${SOEM_DIR}/oshw/${OS}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/decotofusoem>
)
if(WIN32)
  target_include_directories(decotofusoem PUBLIC
    $<BUILD_INTERFACE:${SOEM_DIR}/oshw/win32/wpcap/Include>
  )
endif()

install(TARGETS decotofusoem
  EXPORT decotofusoemTargets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/decotofu/decotofusoem.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/decotofusoem"
)
install(EXPORT decotofusoemTargets
  FILE decotofusoemTargets.cmake
  NAMESPACE decotofusoem::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/decotofusoem"
)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/decotofusoemConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/decotofusoemConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/decotofusoem"
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/decotofusoemConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/decotofusoemConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/decotofusoemConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/decotofusoem"
)

if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
  if(BUILD_TESTING)
    add_executable(test_decotofusoem_unit
      tests/test_decotofusoem_unit.c
    )
    target_include_directories(test_decotofusoem_unit PRIVATE ${DECOTOFU_INCLUDE_DIRS})
    target_link_directories(test_decotofusoem_unit PRIVATE ${DECOTOFU_LINK_DIRS})
    target_link_libraries(test_decotofusoem_unit PRIVATE soem ${OS_COMMON_LIBS})
    target_compile_definitions(test_decotofusoem_unit PRIVATE
      $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
      DECOTOFUSOEM_VERSION_STRING="${PROJECT_VERSION}"
    )
    target_compile_options(test_decotofusoem_unit PRIVATE
      $<$<C_COMPILER_ID:MSVC>:/W3 $<$<BOOL:${DECOTOFUSOEM_WARNINGS_AS_ERRORS}>:/WX>>
      $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>:-Wall -Wextra $<$<BOOL:${DECOTOFUSOEM_WARNINGS_AS_ERRORS}>:-Werror>>
    )
    add_test(NAME decotofusoem_unit COMMAND test_decotofusoem_unit)

    add_executable(test_decotofusoem_abi
      tests/test_decotofusoem_abi.c
    )
    target_link_libraries(test_decotofusoem_abi PRIVATE decotofusoem)
    if(UNIX)
      target_link_libraries(test_decotofusoem_abi PRIVATE ${CMAKE_DL_LIBS})
    endif()
    target_compile_definitions(test_decotofusoem_abi PRIVATE
      $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
      DECOTOFUSOEM_EXPORTS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/decotofu/decotofusoem.exports"
      DECOTOFUSOEM_LIBRARY_NAME="$<TARGET_FILE_NAME:decotofusoem>"
      DECOTOFUSOEM_LIBRARY_PATH="$<TARGET_FILE:decotofusoem>"
    )
    target_compile_options(test_decotofusoem_abi PRIVATE
      $<$<C_COMPILER_ID:MSVC>:/W3 $<$<BOOL:${DECOTOFUSOEM_WARNINGS_AS_ERRORS}>:/WX>>
      $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>:-Wall -Wextra $<$<BOOL:${DECOTOFUSOEM_WARNINGS_AS_ERRORS}>:-Werror>>
    )
    add_test(NAME decotofusoem_abi COMMAND test_decotofusoem_abi)
  endif()
endif()
