cmake_minimum_required(VERSION 3.16)
project(WASMChromeErrStates LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WHP_ROOT "$ENV{USERPROFILE}/WASMHolePunch")
if(NOT EXISTS "${WHP_ROOT}/include/whp/control.h")
  set(WHP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../WASMVoodooCompile/WASMHolePunch")
endif()
file(TO_CMAKE_PATH "${WHP_ROOT}" WHP_ROOT)

add_library(ces
  src/catalog/catalog.cc
  src/loader/loader.cc
  src/deputy/deputy.cc
  src/harness/harness.cc
  src/c/system.cc
)

target_include_directories(ces
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
  target_compile_options(ces PRIVATE /W4)
else()
  target_compile_options(ces PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  target_compile_options(ces PRIVATE -fno-exceptions -fno-rtti)
endif()

# WASI reactor named ces.wasm. Native tests/CLI stay off this toolchain.
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
  add_executable(ces_wasm "${CMAKE_CURRENT_SOURCE_DIR}/src/c/wasi_start.cc")
  target_compile_options(ces_wasm PRIVATE -fno-exceptions -fno-rtti)
  target_link_libraries(ces_wasm PRIVATE "-Wl,--whole-archive" ces "-Wl,--no-whole-archive")
  target_link_options(ces_wasm PRIVATE "LINKER:--export-all" "LINKER:--no-entry" "LINKER:--allow-undefined")
  set_target_properties(ces_wasm PROPERTIES OUTPUT_NAME "ces")
else()
  enable_testing()
  add_executable(ces_tests
    tests/test_main.cc
    tests/catalog/lookup.cc
    tests/loader/dino.cc
    tests/harness/fire.cc
    tests/machine/classify.cc
    tests/c/api.cc
    tests/deputy/access.cc
    tests/cdp/ordinal_fire.cc
    src/cdp/fire.cc
  )
  target_include_directories(ces_tests PRIVATE tests "${WHP_ROOT}/include")
  target_link_libraries(ces_tests PRIVATE ces)
  if(EXISTS "${WHP_ROOT}/build/libwhp.a")
    target_link_libraries(ces_tests PRIVATE "${WHP_ROOT}/build/libwhp.a")
  elseif(EXISTS "${WHP_ROOT}/build-chrome/libwhp.a")
    target_link_libraries(ces_tests PRIVATE "${WHP_ROOT}/build-chrome/libwhp.a")
  endif()
  if(WIN32)
    target_link_libraries(ces_tests PRIVATE ws2_32)
    target_compile_definitions(ces_tests PRIVATE _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN)
  endif()
  add_test(NAME ces_tests COMMAND ces_tests)

  add_executable(ces_fire examples/fire/main.cc)
  target_link_libraries(ces_fire PRIVATE ces)
endif()
