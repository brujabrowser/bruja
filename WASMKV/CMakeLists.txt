cmake_minimum_required(VERSION 3.20)
project(WASMKV CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# wkv_core: the portable KV/B+tree/search engine, a port of ultimate_db.go.
# Built identically whether the active toolchain is native or the wasi-sdk
# cross-toolchain (CMAKE_TOOLCHAIN_FILE=.../wasi-sdk.cmake) -- no
# platform-specific code belongs in this target (real file I/O is POSIX
# pread/pwrite/fsync, which wasi-libc backs with fd_pread/fd_pwrite/fd_sync).
add_library(wkv_core STATIC
    src/crc32.cc
    src/disk_manager.cc
    src/buffer_pool.cc
    src/wal.cc
    src/db.cc
    src/btree.cc
    src/index.cc
    src/query.cc
    src/console.cc
)
target_include_directories(wkv_core PUBLIC include)
target_compile_options(wkv_core PRIVATE -Wall -Wextra)

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    # Cross build: produce the WASI reactor guest module. See
    # ../../guikit/cpp/README.md for why reactor mode is assembled by hand
    # (skip the default CRT startup object, link crt1-reactor.o directly)
    # instead of via -mexec-model=reactor on this wasi-sdk install.
    add_executable(wkv
        wasi/abi.cc
        wasi/exports.cc
    )
    target_link_libraries(wkv PRIVATE wkv_core)

    # No exceptions in the wasm guest -- a WASI reactor shouldn't depend on
    # the C++ unwinder. Nothing in this codebase throws in its normal paths
    # (errors are reported via bool + std::string* out-param, not
    # exceptions), so this also turns any future accidental throw into a
    # hard compile error instead of a silent link failure.
    target_compile_options(wkv_core PRIVATE -fno-exceptions)
    target_compile_options(wkv PRIVATE -fno-exceptions)

    get_filename_component(_wasi_sysroot_lib "${WASI_SDK_PREFIX}/share/wasi-sysroot/lib/wasm32-wasi" ABSOLUTE)
    target_link_options(wkv PRIVATE
        -nostartfiles
        "${_wasi_sysroot_lib}/crt1-reactor.o"
        -Wl,--no-entry
    )
    set_target_properties(wkv PROPERTIES SUFFIX ".wasm" OUTPUT_NAME "wkv")
else()
    # Native build: fast unit tests, no wasm runtime needed.
    enable_testing()
    add_executable(wkv_tests
        tests/test_main.cc
        tests/test_buffer_pool.cc
        tests/test_wal_recovery.cc
        tests/test_db.cc
        tests/test_btree.cc
        tests/test_index.cc
        tests/test_query.cc
        tests/test_console.cc
    )
    target_link_libraries(wkv_tests PRIVATE wkv_core)
    add_test(NAME wkv_tests COMMAND wkv_tests)
endif()
