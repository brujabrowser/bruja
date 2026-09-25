# OpenSSL for WASMLime is go++ wasigocvm (`toolchain/openssl-wasm`
# libssl.a / libcrypto.a, memory-BIO TlsTransport). Never vcpkg mingw
# DLLs, never Schannel, never Bytecode Alliance wasi:crypto.
#
# Call before any subdirectory that FetchContents libdatachannel.

if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
  return()
endif()

set(_WLM_GOXX "")
foreach(_cand
    "${CMAKE_CURRENT_SOURCE_DIR}/../go++"
    "$ENV{USERPROFILE}/go++"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../go++")
  if(EXISTS "${_cand}/toolchain/openssl-wasm/lib/libssl.a"
     AND EXISTS "${_cand}/toolchain/openssl-wasm/include/openssl/ssl.h")
    set(_WLM_GOXX "${_cand}")
    break()
  endif()
endforeach()

if(_WLM_GOXX STREQUAL "")
  message(STATUS
    "WASMLime: go++ toolchain/openssl-wasm not found -- TLS stays off "
    "(wasigocvm compile.bat / wasigocvm.bat links it with WASIGO_HAS_OPENSSL)")
  return()
endif()

set(_WLM_SSL_ROOT "${_WLM_GOXX}/toolchain/openssl-wasm")
set(_WLM_SSL_INC "${_WLM_SSL_ROOT}/include")
set(_WLM_SSL_LIB "${_WLM_SSL_ROOT}/lib")

set(OPENSSL_ROOT_DIR "${_WLM_SSL_ROOT}" CACHE PATH "wasigocvm OpenSSL" FORCE)
set(OPENSSL_INCLUDE_DIR "${_WLM_SSL_INC}" CACHE PATH "" FORCE)
set(OPENSSL_CRYPTO_LIBRARY "${_WLM_SSL_LIB}/libcrypto.a" CACHE FILEPATH "" FORCE)
set(OPENSSL_SSL_LIBRARY "${_WLM_SSL_LIB}/libssl.a" CACHE FILEPATH "" FORCE)
set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(OPENSSL_FOUND TRUE CACHE BOOL "" FORCE)
set(OpenSSL_FOUND TRUE)

if(NOT TARGET OpenSSL::Crypto)
  add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
  set_target_properties(OpenSSL::Crypto PROPERTIES
    IMPORTED_LOCATION "${_WLM_SSL_LIB}/libcrypto.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_WLM_SSL_INC}"
  )
endif()
if(NOT TARGET OpenSSL::SSL)
  add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
  set_target_properties(OpenSSL::SSL PROPERTIES
    IMPORTED_LOCATION "${_WLM_SSL_LIB}/libssl.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_WLM_SSL_INC}"
    INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
  )
endif()

set(WLM_WASIGO_OPENSSL ON)
message(STATUS
  "WASMLime: wasigocvm OpenSSL at ${_WLM_SSL_ROOT} (TlsTransport / memory BIO)")
