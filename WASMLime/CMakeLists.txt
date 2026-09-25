# CMake 4 rejects CMAKE_POLICY_VERSION_MINIMUM=3 (a stale cache value in
# this tree). Force 3.5 before cmake_minimum_required reads it.
if(DEFINED CMAKE_POLICY_VERSION_MINIMUM AND
   (CMAKE_POLICY_VERSION_MINIMUM STREQUAL "3" OR
    CMAKE_POLICY_VERSION_MINIMUM VERSION_LESS "3.5"))
  set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)
endif()
cmake_minimum_required(VERSION 3.16)
project(WASMLime LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# TLS is go++ wasigocvm OpenSSL (toolchain/openssl-wasm), same memory-BIO
# TlsTransport shape as wasigocvm_tls.hpp. Native vcpkg OpenSSL DLLs are
# the wrong ABI. Do this before WASMv16: wasmtty would otherwise
# FetchContent libdatachannel and find_package(OpenSSL) against the host.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(wasigocvm-openssl)

# WebRTC/DTLS is not the wasigocvm TLS path. Keep wasmtty_core; skip
# libdatachannel so configure does not need host OpenSSL.
set(WASMTTY_WITH_NATIVE OFF CACHE BOOL
  "libdatachannel WebRTC peer (off: wasigocvm in-module OpenSSL)" FORCE)
if(NOT TARGET datachannel AND NOT TARGET LibDataChannel::LibDataChannel)
  add_library(datachannel INTERFACE)
endif()

set(WLM_WASIGO_RUNTIME OFF)
if(WASIGO_GOCVM OR CMAKE_SYSTEM_NAME STREQUAL "WASI" OR DEFINED WLM_WASIGO_OPENSSL)
  set(WLM_WASIGO_RUNTIME ON)
endif()

# CreateView2 is Win32. Native host is WIN32; wasm occupancy is WASMWin32
# on wasigocvm (guest IS Windows). Not a host-vs-browser split.
if(NOT WIN32 AND NOT WLM_WASIGO_RUNTIME)
  message(FATAL_ERROR
    "WASMLime's CreateView2 backing (Win32 window + WIC PNG decode, see "
    "lime::FrameWindow) is Windows or wasigocvm. WASM is go++ wasigocvm, "
    "not Bytecode Alliance WASI.")
endif()

# wasm occupancy of k32 for FrameWindow when not linking host user32.
set(WLM_WIN32_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMWin32")
if(WLM_WASIGO_RUNTIME AND NOT WIN32
   AND EXISTS "${WLM_WIN32_DIR}/CMakeLists.txt" AND NOT TARGET wasmwin32)
  add_subdirectory("${WLM_WIN32_DIR}" "${CMAKE_BINARY_DIR}/wasmwin32" EXCLUDE_FROM_ALL)
endif()

# ---- WASMVoodooCompile (voodoomc + its own wcb/wck/wst/whp siblings) --
# used both for this repo's own .voodoom interfaces and transitively
# required by WASMBlinker/WASMSkia below.
set(WLM_WVC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMVoodooCompile")
if(EXISTS "${WLM_WVC_DIR}/CMakeLists.txt" AND NOT TARGET voodoomc)
  add_subdirectory("${WLM_WVC_DIR}" "${CMAKE_BINARY_DIR}/wvc" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET voodoomc OR NOT TARGET wcb OR NOT TARGET whp)
  message(FATAL_ERROR
    "WASMLime requires the sibling WASMVoodooCompile checkout at "
    "${WLM_WVC_DIR} (voodoomc, and transitively wcb/wck/wst/whp).")
endif()

# voodoomc (the codegen tool `wlm_generate` shells out to below) is a
# host tool, like protoc -- when cross-compiling wcb/wck/wst/whp to WASI
# above, the `voodoomc` target is a .wasm module the host CMake/ninja
# process cannot execute. Same WASI/host split WASMBlinker's CMakeLists
# already uses for the same reason.
set(WLM_VOODOOMC voodoomc)
set(WLM_VOODOOMC_DEP voodoomc)
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  if(NOT VOODOOMC_HOST)
    find_program(VOODOOMC_HOST NAMES voodoomc voodoomc.exe
      HINTS
        "${WLM_WVC_DIR}/build"
        "${WLM_WVC_DIR}/build_clean"
        "${WLM_WVC_DIR}/build_clean2"
        "${WLM_WVC_DIR}/build_clean3"
        "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv16/voodoomc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv16/build/voodoomc"
    )
  endif()
  if(NOT VOODOOMC_HOST)
    message(FATAL_ERROR
      "WASMLime WASI build needs a native voodoomc. Build "
      "${WLM_WVC_DIR} natively, then reconfigure with "
      "-DVOODOOMC_HOST=/path/to/voodoomc.exe")
  endif()
  set(WLM_VOODOOMC "${VOODOOMC_HOST}")
  set(WLM_VOODOOMC_DEP "${VOODOOMC_HOST}")
  message(STATUS "WASMLime WASI: host voodoomc = ${VOODOOMC_HOST}")
endif()

# ---- WASMSkia (wasmskia + wasmskia_skia_voodoo + wasmskia_font) -- added
# here, before WASMv16/WASMBlinker below, so wasmskia_font/rasta exist as
# targets early enough for WASMBlinker's own optional `if(TARGET
# wasmskia_font)` guard to pick them up for a real CaptureSnapshot (same
# early-declaration reasoning as the WASMv16 block below). Pulls in
# WASMRasta transitively (stb_truetype for wasmskia_font, PNG encode for
# the paint pipeline's own output).
set(WLM_SKIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMSkia")
if(EXISTS "${WLM_SKIA_DIR}/CMakeLists.txt" AND NOT TARGET wasmskia)
  add_subdirectory("${WLM_SKIA_DIR}" "${CMAKE_BINARY_DIR}/wasmskia" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET wasmskia_skia_voodoo)
  message(FATAL_ERROR
    "WASMLime requires the sibling WASMSkia checkout at ${WLM_SKIA_DIR}, "
    "built with wasmskia_skia_voodoo (Canvas <-> skia::voodoo::BitmapN32 -- "
    "present once WASMVoodooCompile/wcb are, which they are above).")
endif()
if(NOT TARGET wasmskia_font)
  message(STATUS
    "WASMLime: wasmskia_font not available (needs the sibling WASMRasta "
    "checkout too, for stb_truetype) -- Frame's CreateView2/CaptureSnapshot "
    "path will fall back to its honest-stub paint implementation.")
endif()

# ---- WASMBruja (brujac, bruja_dom_v8) -- added here, before WASMv16/
# WASMBlinker below, so bruja_dom_v8 exists as a target early enough for
# WASMBlinker's own optional `if(TARGET bruja_dom_v8)` guard to pick it up
# for a real DOM (document/window/Storage) installed into
# LocalFrame::Eval's engine (same early-declaration reasoning as WASMSkia
# above -- WASMv16's own CMakeLists also add_subdirectory()s WASMBruja,
# for brujac + its console/navigator/document/processes bindings, so its
# own `NOT TARGET brujac` guard just skips re-adding it once this runs
# first). DOM bindings go through WASMv8bindings (bruja_dom_v8), never
# the legacy quickjs-backend bruja_dom target.
set(WLM_BRUJA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMBruja")
if(EXISTS "${WLM_BRUJA_DIR}/CMakeLists.txt" AND NOT TARGET brujac)
  add_subdirectory("${WLM_BRUJA_DIR}" "${CMAKE_BINARY_DIR}/bruja" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET bruja_dom_v8)
  message(STATUS
    "WASMLime: sibling WASMBruja checkout missing bruja_dom_v8 at "
    "${WLM_BRUJA_DIR} -- Frame::ExecuteJavaScript will have no "
    "document/window/Storage DOM installed (WASMv8bindings facade engine "
    "only, see WASMBlinker's local_frame_impl.cc).")
endif()

# ---- WASMv16 (wasmv16_engine over WASMv8bindings/wasmv8_facade) -- added
# here, before WASMRenderer/WASMBlinker below, purely so wasmv16_engine
# exists as a target early enough for WASMBlinker's own optional
# `if(TARGET wasmv16_engine)` guard to pick it up (see WASMBlinker's
# CMakeLists for why it can't add_subdirectory(WASMv16) itself -- that
# would be a mutual add_subdirectory cycle, since WASMv16 already adds
# WASMBlinker). This is what gives lime::Frame::ExecuteJavaScript a real
# JS engine (quickjs-ng only via wasmv8_facade, never a second FetchContent)
# instead of the honest-stub fallback.
set(WLM_WV16_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv16")
if(EXISTS "${WLM_WV16_DIR}/CMakeLists.txt" AND NOT TARGET wasmv16_engine)
  add_subdirectory("${WLM_WV16_DIR}" "${CMAKE_BINARY_DIR}/wasmv16" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET wasmv16_engine)
  message(STATUS
    "WASMLime: sibling WASMv16 checkout not found at ${WLM_WV16_DIR} -- "
    "Frame::ExecuteJavaScript will fall back to its honest-stub "
    "implementation (see WASMBlinker's local_frame_impl.cc).")
endif()

# ---- WASMRenderer (renderer, content::Renderer/content::LocalFrame) --
# the real content.mojom-shaped frame-bootstrap layer lime::ContextImpl
# drives (real Renderer.CreateFrame() calls, not a bare C++ constructor).
# Pulls in WASMBlinker (blink::LocalFrameImpl, the actual render/JS/HTTP
# backing) transitively.
set(WLM_RENDERER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMRenderer")
if(EXISTS "${WLM_RENDERER_DIR}/CMakeLists.txt" AND NOT TARGET renderer)
  add_subdirectory("${WLM_RENDERER_DIR}" "${CMAKE_BINARY_DIR}/renderer" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET renderer)
  message(FATAL_ERROR
    "WASMLime requires the sibling WASMRenderer checkout at "
    "${WLM_RENDERER_DIR} (content::Renderer/content::LocalFrame).")
endif()

# ---- WASMProcess (wpr::TaskManager) -- real process/tab-table bookkeeping
# for ContextProviderImpl/ContextImpl (one Context = one spawned Process,
# one Frame = one attached tab -- see context_provider_impl.h).
set(WLM_WPR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMProcess")
if(EXISTS "${WLM_WPR_DIR}/CMakeLists.txt" AND NOT TARGET wpr)
  add_subdirectory("${WLM_WPR_DIR}" "${CMAKE_BINARY_DIR}/wpr" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET wpr)
  message(FATAL_ERROR
    "WASMLime requires the sibling WASMProcess checkout at ${WLM_WPR_DIR} "
    "(wpr::TaskManager).")
endif()

# ---- .voodoom -> *_gen.h, same add_custom_command pattern WASMBlinker/
# WASMSkia already use. All four share one generated dir so
# `#include "navigation_gen.h"` etc. resolve across files.
set(WLM_GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${WLM_GEN_DIR}")
set(WLM_IF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/interfaces")

function(wlm_generate name)
  set(out "${WLM_GEN_DIR}/${name}_gen.h")
  add_custom_command(
    OUTPUT "${out}"
    COMMAND ${WLM_VOODOOMC} "${WLM_IF_DIR}/${name}.voodoom" -o "${out}" --out-dir=${WLM_GEN_DIR}
    DEPENDS ${WLM_VOODOOMC_DEP} "${WLM_IF_DIR}/${name}.voodoom" ${ARGN}
    COMMENT "voodoomc: generating ${name}_gen.h"
    VERBATIM
  )
endfunction()

wlm_generate(navigation)
wlm_generate(frame "${WLM_IF_DIR}/navigation.voodoom" "${WLM_GEN_DIR}/navigation_gen.h")
wlm_generate(context "${WLM_IF_DIR}/frame.voodoom" "${WLM_GEN_DIR}/frame_gen.h")
wlm_generate(context_provider "${WLM_IF_DIR}/context.voodoom" "${WLM_GEN_DIR}/context_gen.h")

add_custom_target(wlm_generate_interfaces DEPENDS
  "${WLM_GEN_DIR}/navigation_gen.h"
  "${WLM_GEN_DIR}/frame_gen.h"
  "${WLM_GEN_DIR}/context_gen.h"
  "${WLM_GEN_DIR}/context_provider_gen.h"
)

add_library(wlm
  src/command_line/command_line.cc
  src/main/main.cc
  src/context_provider_main/context_provider_main.cc
  src/navigation_controller_impl/navigation_controller_impl.cc
  src/frame_window/frame_window.cc
  src/frame_impl/frame_impl.cc
  src/context_impl/context_impl.cc
  src/context_provider_impl/context_provider_impl.cc
)
add_dependencies(wlm wlm_generate_interfaces)

target_include_directories(wlm
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "${WLM_GEN_DIR}"
)
target_link_libraries(wlm PUBLIC whp renderer wasmskia_skia_voodoo wpr)
if(WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
  target_link_libraries(wlm PRIVATE ole32 windowscodecs gdi32 user32)
elseif(TARGET wasmwin32)
  # PUBLIC: consumers of wlm (wlm_example, bruja_browser) also #include
  # "win32/wasigocvm_windows_shim.hpp" directly for their own window/message
  # loop code, same as frame_window.cc does inside wlm itself -- they need
  # wasmwin32's include dir, not just its linked symbols.
  target_link_libraries(wlm PUBLIC wasmwin32)
endif()
if(TARGET OpenSSL::SSL)
  target_link_libraries(wlm PUBLIC OpenSSL::SSL)
  target_compile_definitions(wlm PUBLIC WASIGO_HAS_OPENSSL=1)
endif()
if(WLM_WASIGO_RUNTIME)
  target_compile_definitions(wlm PUBLIC
    WASIGO_GOCVM=1
    _WASI_EMULATED_MMAN
    _WASI_EMULATED_GETPID)
endif()

if(MSVC)
  target_compile_options(wlm PRIVATE /W4)
else()
  target_compile_options(wlm PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(wlm_tests
  tests/test_main.cc
  tests/command_line/parsing.cc
  tests/main/lifecycle.cc
  tests/web/navigation_roundtrip.cc
  tests/web/type_intern.cc
)
target_include_directories(wlm_tests PRIVATE tests)
target_link_libraries(wlm_tests PRIVATE wlm ws2_32)
find_package(Threads REQUIRED)
target_link_libraries(wlm_tests PRIVATE Threads::Threads)
add_test(NAME wlm_tests COMMAND wlm_tests)
file(WRITE "${CMAKE_BINARY_DIR}/CTestCustom.cmake"
     "set(CTEST_CUSTOM_TESTS_IGNORE whp_tests;wst_tests;wck_tests;wcb_tests;wvc_tests;wvc_golden_tests;blinker_tests;renderer_tests;wasmskia_tests;wasmskia_bitmap_tests;wpr_tests)\n")

add_executable(wlm_example examples/lime/main.cc)
target_link_libraries(wlm_example PRIVATE wlm)

# ~/Bruja occupancy (mojom -> voodoomc source, RendererImpl + LocalFrameImpl +
# FrameWindow). This target only links `wlm`; it does not build WASMv16
# bruja_browser.
set(BRUJA_BROWSER_CC "${CMAKE_CURRENT_SOURCE_DIR}/../Bruja/src/browser.cc")
if(EXISTS "${BRUJA_BROWSER_CC}")
  add_executable(bruja "${BRUJA_BROWSER_CC}")
  target_link_libraries(bruja PRIVATE wlm)
  if(WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    target_link_libraries(bruja PRIVATE user32 gdi32)
  endif()
endif()

# ---- Bruja Browser (lives in ../WASMv16/src, built from here) -----------
# One real lime::Frame per tab via CreateView2 -- see
# ../WASMv16/src/browser_main.cc. Defined here because `wlm` does not
# exist yet when WASMv16 is add_subdirectory()'d above.
if(TARGET webview::core)
  add_executable(bruja_browser
    "${WLM_WV16_DIR}/src/browser_main.cc"
    "${WLM_WV16_DIR}/src/local_frame_bindings.cc"
    "${WLM_WV16_DIR}/src/document_bindings.cc"
    "${WLM_WV16_DIR}/src/processes_bindings.cc"
  )
  add_dependencies(bruja_browser wv16_generate_echo wv16_generate_console
                    wv16_generate_navigator wv16_generate_document wv16_generate_processes)
  target_include_directories(bruja_browser PRIVATE
    "${WLM_WV16_DIR}/include"
    "${WLM_GEN_DIR}"
  )
  target_link_libraries(bruja_browser PRIVATE wasmv16_js wcb wasmv16_engine blinker webview::core wpr wlm)
  get_filename_component(WLM_GUIKIT_BRIDGE_EXE
    "${WLM_WV16_DIR}/../WASMUniLoader/dist/guikit_bridge.exe" ABSOLUTE)
  target_compile_definitions(bruja_browser PRIVATE
    "BRUJA_GUIKIT_BRIDGE_EXE=\"${WLM_GUIKIT_BRIDGE_EXE}\""
  )
  if(MSVC)
    target_compile_options(bruja_browser PRIVATE /W4)
  else()
    target_compile_options(bruja_browser PRIVATE -Wall -Wextra -Wpedantic)
  endif()
else()
  message(STATUS
    "WASMLime: webview::core not available (needs the sibling "
    "WASMWebviewKernel checkout) -- skipping bruja_browser.")
endif()

# ---- UniLoader sandbox display -------------------------------------------
# WASMLime FrameWindow (CreateView2) paints GML. Source lives in the sandbox;
# links `wlm` the same way wlm_example does.
set(WLM_UNIL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMUniLoader")
set(WLM_UNIL_DISPLAY "${WLM_UNIL_DIR}/cpp/display/main.cc")
if(EXISTS "${WLM_UNIL_DISPLAY}")
  add_subdirectory("${WLM_UNIL_DIR}/cpp/display" "${CMAKE_BINARY_DIR}/after_lime" EXCLUDE_FROM_ALL)
  # main.cc drives a wasmv16::Engine the same way bruja_browser's
  # browser_main.cc does (InstallPrinter/InstallMojoBindings/
  # InstallLocalFrameBindings/InstallConsoleBindings/InstallNavigatorBindings/
  # InstallDocumentBindings/InstallProcessesBindings, DeferredJsRoots) --
  # those bindings are plain sources compiled directly into whichever
  # executable uses them (see bruja_browser above), not a linkable library,
  # so unil_display needs the same source list and wv16_generate_* deps
  # bruja_browser has, or its link fails with undefined wasmv16:: symbols.
  add_executable(unil_display
    "${WLM_UNIL_DISPLAY}"
    "${WLM_WV16_DIR}/src/local_frame_bindings.cc"
    "${WLM_WV16_DIR}/src/document_bindings.cc"
    "${WLM_WV16_DIR}/src/processes_bindings.cc"
  )
  add_dependencies(unil_display wv16_generate_echo wv16_generate_console
                    wv16_generate_navigator wv16_generate_document wv16_generate_processes)
  target_link_libraries(unil_display PRIVATE wlm after_lime wasmv16_js wcb wasmv16_engine blinker wpr)
  target_include_directories(unil_display PRIVATE
    "${WLM_UNIL_DIR}/cpp/display"
    "${WLM_WV16_DIR}/include"
    "${WLM_GEN_DIR}"
  )
  get_filename_component(WLM_UNIL_BRIDGE_EXE
    "${WLM_UNIL_DIR}/dist/guikit_bridge.exe" ABSOLUTE)
  target_compile_definitions(unil_display PRIVATE
    "BRUJA_GUIKIT_BRIDGE_EXE=\"${WLM_UNIL_BRIDGE_EXE}\""
    WIN32_LEAN_AND_MEAN
    WPR_HAS_WHP
    _WIN32_WINNT=0x0601
  )
  if(TARGET webview::core)
    target_link_libraries(unil_display PRIVATE webview::core)
  endif()
  if(NOT MSVC)
    target_compile_options(unil_display PRIVATE
      -Wno-unused-function -Wno-unused-const-variable -Wno-unused-variable
      -Wno-unused-parameter)
  endif()

  get_filename_component(WLM_SHIM_SANDBOX
    "${WLM_UNIL_DIR}/../shim_sandbox" ABSOLUTE)
  set(WLM_RENDER_WORKER_SRC "${WLM_UNIL_DIR}/cpp/display/render_worker.cc")
  if(EXISTS "${WLM_RENDER_WORKER_SRC}" AND EXISTS "${WLM_SHIM_SANDBOX}/build/libw2g.a")
    add_executable(render_worker "${WLM_RENDER_WORKER_SRC}")
    target_include_directories(render_worker PRIVATE
      "${WLM_SHIM_SANDBOX}/include"
      "${CMAKE_BINARY_DIR}/blinker/CMakeFiles/blinker.dir"
    )
    # wasmv8_facade (not raw qjs): the single quickjs-ng package for this
    # stack -- WASMv16's engine already links it; keep the same target here
    # so UniLoader workers never FetchContent/link quickjs on their own.
    target_link_libraries(render_worker PRIVATE
      blinker wasmv16_engine wasmskia_font wasmskia wasmskia_image renderer rasta
      wasmv8_facade
      "${WLM_SHIM_SANDBOX}/build/libw2g.a"
    )
    if(MSVC)
      target_compile_definitions(render_worker PRIVATE _WIN32_WINNT=0x0601)
    else()
      target_compile_definitions(render_worker PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
      target_compile_options(render_worker PRIVATE -Wno-unused-parameter)
    endif()
    set_target_properties(render_worker PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${WLM_UNIL_DIR}/dist"
      RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WLM_UNIL_DIR}/dist"
      RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WLM_UNIL_DIR}/dist"
      OUTPUT_NAME "render_worker"
    )
  endif()
  set(WLM_PAGE_JS_SRC "${WLM_UNIL_DIR}/cpp/display/page_js_main.cc")
  if(EXISTS "${WLM_PAGE_JS_SRC}")
    add_executable(page_js
      "${WLM_PAGE_JS_SRC}"
    )
    add_dependencies(page_js wv16_generate_console wv16_generate_navigator)
    target_include_directories(page_js PRIVATE
      "${CMAKE_BINARY_DIR}/blinker/CMakeFiles/blinker.dir"
      "${WLM_WV16_DIR}/include"
      "${WLM_GEN_DIR}"
    )
    target_link_libraries(page_js PRIVATE
      blinker wasmv16_js wasmv16_engine wasmskia_font wasmskia wasmskia_image renderer rasta
      wasmv8_facade
    )
    target_compile_definitions(page_js PRIVATE
      BLINK_HAS_DOM_BINDINGS BLINK_HAS_IMAGE_DECODE BLINK_HAS_PAINT_PIPELINE
      BLINK_HAS_WASMV16_ENGINE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601
    )
    if(NOT MSVC)
      target_compile_options(page_js PRIVATE -Wno-unused-parameter)
    endif()
    set_target_properties(page_js PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${WLM_UNIL_DIR}/dist"
      RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WLM_UNIL_DIR}/dist"
      RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WLM_UNIL_DIR}/dist"
      OUTPUT_NAME "page_js"
    )
  endif()
  set_target_properties(unil_display PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WLM_UNIL_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WLM_UNIL_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WLM_UNIL_DIR}/dist"
    OUTPUT_NAME "unil_display"
  )
  # TLS is wasigocvm static libssl.a (toolchain/openssl-wasm). No vcpkg
  # OpenSSL DLLs to copy next to unil_display.
  add_custom_command(TARGET unil_display POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${WLM_UNIL_DIR}/cpp/third_party/wgpu-native/lib/wgpu_native.dll"
      "$<TARGET_FILE_DIR:unil_display>/wgpu_native.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:unil_display>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${WLM_UNIL_DIR}/cpp/third_party/RmlUi/Samples/assets/LatoLatin-Regular.ttf"
      "$<TARGET_FILE_DIR:unil_display>/fonts/LatoLatin-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${WLM_UNIL_DIR}/cpp/third_party/RmlUi/Samples/assets/LatoLatin-Bold.ttf"
      "$<TARGET_FILE_DIR:unil_display>/fonts/LatoLatin-Bold.ttf"
  )
  if(MSVC)
    target_compile_options(unil_display PRIVATE /W4)
  else()
    target_compile_options(unil_display PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()
