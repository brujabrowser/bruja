cmake_minimum_required(VERSION 3.16)
project(WASMBlinker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# WASMBlinker needs voodoomc (code generator, for interfaces/local_frame.voodoom)
# and wcb/wck (the Mojo runtime the generated code links against).
#
# Native: pulling in the sibling WASMVoodooCompile checkout gets all three --
# its own CMakeLists already add_subdirectory()s ../WASMCadidumBindings
# (which in turn pulls in ../WASMCadidumKernel), so `wcb`/`wck` fall out of
# this one add_subdirectory too.
#
# WASI: voodoomc is a host tool (like protoc) -- do not compile it with the
# wasi-sdk toolchain. Use a native voodoomc (VOODOOMC_HOST) and add
# WASMCadidumBindings directly so wcb is the WASI library.
set(WB_WVC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMVoodooCompile")
set(WB_WCB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMCadidumBindings")
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  if(NOT WASIGO_GOCVM)
    # Stock wasmtime cannot instantiate env::__cxa_throw / env::__cpp_exception
    # imports. Compile the whole WASI tree without exceptions (ChromeErrStates
    # does the same) so those symbols are never referenced.
    # wasigocvm doesn't have this constraint -- it links the real eh libc++/
    # libc++abi/libunwind (see go++/toolchain/share/cmake/wasigocvm.cmake)
    # and runs under wasitime, not stock wasmtime.
    add_compile_options(-fno-exceptions -fno-rtti)
  endif()
  if(NOT TARGET wcb AND EXISTS "${WB_WCB_DIR}/CMakeLists.txt")
    add_subdirectory("${WB_WCB_DIR}" "${CMAKE_BINARY_DIR}/wcb" EXCLUDE_FROM_ALL)
  endif()
  if(NOT VOODOOMC_HOST)
    find_program(VOODOOMC_HOST NAMES voodoomc voodoomc.exe
      HINTS
        "${WB_WVC_DIR}/build"
        "${WB_WVC_DIR}/build_clean"
        "${WB_WVC_DIR}/build_clean2"
        "${WB_WVC_DIR}/build_clean3"
        "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv16/voodoomc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv16/build/voodoomc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../WASMLime/build/wvc"
    )
  endif()
  if(NOT VOODOOMC_HOST)
    message(FATAL_ERROR
      "WASI blinker build needs a native voodoomc. Build "
      "${WB_WVC_DIR} natively, then reconfigure with "
      "-DVOODOOMC_HOST=/path/to/voodoomc.exe")
  endif()
  if(NOT TARGET wcb)
    message(FATAL_ERROR
      "WASMBlinker WASI build requires the sibling WASMCadidumBindings "
      "checkout at ${WB_WCB_DIR} (Mojo runtime).")
  endif()
  set(WB_VOODOOMC "${VOODOOMC_HOST}")
  set(WB_VOODOOMC_DEP "${VOODOOMC_HOST}")
  message(STATUS "WASMBlinker WASI: host voodoomc = ${VOODOOMC_HOST}")
else()
  if(EXISTS "${WB_WVC_DIR}/CMakeLists.txt" AND NOT TARGET voodoomc)
    add_subdirectory("${WB_WVC_DIR}" "${CMAKE_BINARY_DIR}/wvc" EXCLUDE_FROM_ALL)
  endif()
  if(NOT TARGET voodoomc OR NOT TARGET wcb)
    message(FATAL_ERROR
      "WASMBlinker requires the sibling WASMVoodooCompile checkout at "
      "${WB_WVC_DIR} (code generator), itself with WASMCadidumBindings/"
      "WASMCadidumKernel siblings present (Mojo runtime).")
  endif()
  set(WB_VOODOOMC voodoomc)
  set(WB_VOODOOMC_DEP voodoomc)
endif()

# ---- optional: WASMMsgProxy (wmp) -- real MojoInvoke/OrdinalCatalog/
# MojoSink trace machinery for page_eval's --trace/--fire-ordinal, the
# same real ecosystem-consistent trace format project_lovelace's own WMP
# ordinal bus uses (see project_lovelace/docs/ORDINAL-CHAINING.md).
# WASMv16's own CMakeLists already add_subdirectory()s this before it
# add_subdirectory()s this repo (so `wmp` exists here without racing);
# this add_subdirectory only fires when building WASMBlinker standalone
# and the sibling checkout is present. Optional (page_eval's --trace/
# --fire-ordinal flags are simply unavailable without it) so a standalone
# WASMBlinker checkout without ../WASMMsgProxy still builds.
set(WB_WMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMMsgProxy")
if(EXISTS "${WB_WMP_DIR}/CMakeLists.txt" AND NOT TARGET wmp)
  add_subdirectory("${WB_WMP_DIR}" "${CMAKE_BINARY_DIR}/wmp" EXCLUDE_FROM_ALL)
endif()

set(WB_GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${WB_GEN_DIR}")
set(WB_GEN_LOCAL_FRAME_H "${WB_GEN_DIR}/local_frame_interface_gen.h")
add_custom_command(
  OUTPUT "${WB_GEN_LOCAL_FRAME_H}"
  COMMAND ${WB_VOODOOMC}
          "${CMAKE_CURRENT_SOURCE_DIR}/interfaces/local_frame.voodoom"
          -o "${WB_GEN_LOCAL_FRAME_H}"
  DEPENDS ${WB_VOODOOMC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/interfaces/local_frame.voodoom"
  COMMENT "voodoomc: generating local_frame_interface_gen.h"
  VERBATIM
)
add_custom_target(wb_generate_local_frame DEPENDS "${WB_GEN_LOCAL_FRAME_H}")

add_library(blinker
  src/local_frame_impl.cc
  src/web_page.cc
  src/html_raster.cc
  src/http_client.cc
  src/html_parser.cc
  src/gml.cc
  src/dml.cc
  src/url.cc
  src/paint.cc
  src/display_list.cc
  src/layout.cc
  src/inline_layout.cc
  src/block_layout.cc
  src/position_layout.cc
  src/css.cc
  src/wasi_frame.cc
)

# Ultralight WebCore: HTML final paint (headless View → RGBA). GML/RML
# stay on RmlUi + WASMSkia. Set ULTRALIGHT_ROOT to the SDK (include/, lib/).
set(ULTRALIGHT_ROOT "" CACHE PATH "Ultralight SDK root")
if(ULTRALIGHT_ROOT STREQUAL "" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Ultralight/include/Ultralight/Ultralight.h")
  set(ULTRALIGHT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Ultralight")
endif()
if(ULTRALIGHT_ROOT STREQUAL "" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ultralight-sdk/include/Ultralight/Ultralight.h")
  set(ULTRALIGHT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../ultralight-sdk")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI" AND EXISTS "${ULTRALIGHT_ROOT}/include/Ultralight/Ultralight.h")
  target_sources(blinker PRIVATE src/ultralight_raster.cc)
  target_include_directories(blinker PUBLIC "${ULTRALIGHT_ROOT}/include")
  target_compile_definitions(blinker PUBLIC BLINK_HAS_ULTRALIGHT)
  if(EXISTS "${ULTRALIGHT_ROOT}/lib")
    target_link_directories(blinker PUBLIC "${ULTRALIGHT_ROOT}/lib")
  endif()
  foreach(ul_lib UltralightCore Ultralight WebCore AppCore)
    if(EXISTS "${ULTRALIGHT_ROOT}/lib/${ul_lib}.lib" OR EXISTS "${ULTRALIGHT_ROOT}/lib/lib${ul_lib}.a")
      target_link_libraries(blinker PUBLIC ${ul_lib})
    endif()
  endforeach()
  message(STATUS "WASMBlinker: HTML raster = Ultralight WebCore (${ULTRALIGHT_ROOT})")
else()
  message(STATUS "WASMBlinker: HTML raster = Blinker fallback (set ULTRALIGHT_ROOT for WebCore)")
endif()
add_dependencies(blinker wb_generate_local_frame)
target_include_directories(blinker PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  "${WB_GEN_DIR}"
)
target_link_libraries(blinker PUBLIC wcb)
if(WIN32)
  target_link_libraries(blinker PUBLIC ws2_32)
endif()

# ---- optional: a real quickjs-ng-backed LocalFrame::Eval -----------------
# wasmv16_engine (../WASMv16's quickjs-ng wrapper, no blinker/wcb
# dependency of its own) is linked *only if already a target* -- this repo
# deliberately never add_subdirectory()s WASMv16 itself: WASMv16's own
# CMakeLists already add_subdirectory()s this one (for `blinker`), so this
# repo doing the same back would be a mutual add_subdirectory cycle.
# Whichever project sits above both in the tree (e.g. WASMLime) is
# responsible for add_subdirectory(WASMv16) *before* this one, the same
# "declare the shared dependency early, everyone downstream sees the
# target" pattern wcb/wck/wst/whp already rely on. Without it, Eval stays
# the honest stub in local_frame_impl.cc -- same graceful-skip convention
# WASMSkia's wasmskia_skia_voodoo uses for its own optional sibling.
if(TARGET wasmv16_engine)
  target_link_libraries(blinker PUBLIC wasmv16_engine)
  target_compile_definitions(blinker PUBLIC BLINK_HAS_WASMV16_ENGINE)
endif()

# Page-safe WASMv16 JS layer (Mojo echo / console / navigator / CDP). Linked
# only if already a target — WASMv16 declares wasmv16_js before adding this
# subdirectory. Page LocalFrameImpls install it on their private engine so
# they never share chrome's Mojo.createLocalFramePipe / processes globals.
if(TARGET wasmv16_js)
  target_link_libraries(blinker PUBLIC wasmv16_js)
  target_compile_definitions(blinker PUBLIC BLINK_HAS_WASMV16_JS)
endif()

# ---- optional: a real paint pipeline for CaptureSnapshot -----------------
# Opt-in for a standalone checkout. The layout engine and its golden tests
# are gated on BLINK_HAS_PAINT_PIPELINE, so without the siblings a
# standalone `ctest` silently skips every box-model, float, flex, table and
# inline-formatting test. WASMSkia does not add_subdirectory this repo, so
# unlike WASMv16 there is no cycle to avoid here -- only the default-off
# habit of not requiring siblings for the plain build.
option(WB_WITH_PAINT_SIBLINGS
       "add_subdirectory ../WASMRasta and ../WASMSkia so the paint pipeline and its tests build standalone"
       OFF)
if(WB_WITH_PAINT_SIBLINGS AND NOT TARGET wasmskia_font)
  if(NOT TARGET rasta AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../WASMRasta/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../WASMRasta" "${CMAKE_BINARY_DIR}/rasta"
                     EXCLUDE_FROM_ALL)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../WASMSkia/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../WASMSkia" "${CMAKE_BINARY_DIR}/wasmskia"
                     EXCLUDE_FROM_ALL)
  endif()
endif()

# wasmskia_font (../WASMSkia's real stb_truetype text rendering) and rasta
# (../WASMRasta's PNG encode, transitively required by wasmskia_font
# itself) -- same "linked only if already a target, no add_subdirectory
# here" reasoning as wasmv16_engine above (WASMSkia doesn't actually
# add_subdirectory this repo, so there's no cycle risk here the way there
# is with WASMv16 -- this guard exists for symmetry/consistency and so a
# standalone WASMBlinker build doesn't require WASMSkia/WASMRasta at all).
if(TARGET wasmskia_font AND TARGET rasta)
  target_link_libraries(blinker PUBLIC wasmskia_font rasta)
  target_compile_definitions(blinker PUBLIC BLINK_HAS_PAINT_PIPELINE)
  if(TARGET wasmskia_image)
    target_link_libraries(blinker PUBLIC wasmskia_image)
    target_compile_definitions(blinker PUBLIC BLINK_HAS_IMAGE_DECODE)
  endif()
endif()

# ---- optional: a real document/window/Storage DOM installed into Eval's
# engine -- bruja_dom_v8 (../WASMBruja's real, tested Document/Element/Event/
# Window/Storage implementation, generated by brujac's V8 backend -- see its
# include/bruja_dom/dom_v8_impl.h) needs both a JS engine (wasmv16_engine,
# above, now itself unified onto the WASMv8bindings facade) and brujac's
# --backend=v8-generated dom_v8_gen.h. Same "linked only if already a
# target" reasoning as the two guards above; WASMBruja doesn't
# add_subdirectory this repo either, so no cycle risk, same as
# wasmskia_font.
if(TARGET wasmv16_engine AND TARGET bruja_dom_v8)
  target_link_libraries(blinker PUBLIC bruja_dom_v8)
  target_compile_definitions(blinker PUBLIC BLINK_HAS_DOM_BINDINGS)
endif()

# ---- optional: the native WebRTC TTY client (../WASMv16/wasmtty) --
# installs `TTYListener`/`TTYReceiver` globals into Eval's engine, the
# in-process replacement for what used to be a real browser-WASM binary
# fetched over HTTP (see WASMv16/ARCHITECTURE.md's WASMTTY row). Same
# "linked only if already a target" reasoning as the two guards above --
# WASMv16/wasmtty doesn't add_subdirectory this repo either.
if(TARGET wasmv16_engine AND TARGET wasmtty_native)
  target_link_libraries(blinker PUBLIC wasmtty_native)
  target_compile_definitions(blinker PUBLIC BLINK_HAS_WASMTTY_BINDINGS)
endif()

# GuiKit GML is Blinker's authoring front-end for chrome/about documents.
# Linked only if already a target (Bruja add_subdirectorys guikit/cpp
# before Lime/Blinker). Without it, LoadGML is an honest stub.
if(TARGET guikit_core)
  target_link_libraries(blinker PUBLIC guikit_core)
  target_compile_definitions(blinker PUBLIC BLINK_HAS_GUIKIT)
endif()

if(MSVC)
  target_compile_options(blinker PRIVATE /W4)
else()
  target_compile_options(blinker PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
  if(NOT WASIGO_GOCVM)
    # Stock wasmtime: no wasm exception tag (env::__cpp_exception) -- it
    # cannot instantiate a module that imports it. libc++ still works;
    # bad_alloc aborts instead of throwing, same as ChromeErrStates' WASI
    # guests. wasigocvm links the real eh libc++/libc++abi/libunwind and
    # runs under wasitime, not stock wasmtime -- see the guard above.
    target_compile_options(blinker PRIVATE -fno-exceptions -fno-rtti)
  endif()
  add_executable(blinker_wasm "${CMAKE_CURRENT_SOURCE_DIR}/src/wasi_start.cc")
  if(NOT WASIGO_GOCVM)
    target_compile_options(blinker_wasm PRIVATE -fno-exceptions -fno-rtti)
  endif()
  target_link_libraries(blinker_wasm PRIVATE "-Wl,--whole-archive" blinker "-Wl,--no-whole-archive")
  target_link_options(blinker_wasm PRIVATE
    "LINKER:--export-all"
    "LINKER:--no-entry"
    "LINKER:--allow-undefined")
  set_target_properties(blinker_wasm PROPERTIES OUTPUT_NAME "blinker")
else()
  enable_testing()
  add_executable(blinker_tests
    tests/test_main.cc
    tests/local_frame_roundtrip_test.cc
    tests/css_layout_test.cc
    tests/motionkb_smoke_test.cc
    tests/c_frame_abi_test.cc
  )
  target_include_directories(blinker_tests PRIVATE tests "${WB_GEN_DIR}")
  target_link_libraries(blinker_tests PRIVATE blinker)
  if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(blinker_tests PRIVATE Threads::Threads)
  endif()
  add_test(NAME blinker_tests COMMAND blinker_tests)

  add_executable(paint_demo examples/paint_demo/main.cc)
  target_link_libraries(paint_demo PRIVATE blinker)

  # wmp (WASMMsgProxy): real MojoInvoke/OrdinalCatalog/MojoSink trace for
  # --trace/--fire-ordinal (see the optional add_subdirectory near the top
  # of this file / WASMv16's own, whichever provides it).
  if(TARGET wmp)
    add_executable(page_eval examples/page_eval/main.cc)
    target_link_libraries(page_eval PRIVATE blinker wmp)
  endif()
endif()

file(WRITE "${CMAKE_BINARY_DIR}/CTestCustom.cmake"
     "set(CTEST_CUSTOM_TESTS_IGNORE whp_tests;wst_tests;wck_tests;wcb_tests;wvc_tests;wvc_golden_tests)\n")
