# Configures and builds both targets: the native unit-test binary (run
# immediately, failing the script on any test failure) and the wasm32-wasi
# reactor guest module. Mirrors ../guikit/cpp/build.ps1.
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not $env:WASI_SDK_PATH) {
    $candidate = Join-Path $HOME "wasi-sdk"
    if (Test-Path $candidate) {
        $env:WASI_SDK_PATH = $candidate
    } else {
        throw "WASI_SDK_PATH is not set and $candidate does not exist -- set WASI_SDK_PATH to a wasi-sdk checkout."
    }
}

Write-Host ">> native: configure + build"
cmake -S . -B build-native -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug | Out-Null
cmake --build build-native -j4

Write-Host ">> native: unit tests"
& (Join-Path $PSScriptRoot "build-native/wkv_tests.exe")
if ($LASTEXITCODE -ne 0) { throw "native unit tests failed" }

Write-Host ">> wasm: configure + build"
$toolchain = Join-Path $env:WASI_SDK_PATH "share/cmake/wasi-sdk.cmake"
cmake -S . -B build-wasm -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release | Out-Null
cmake --build build-wasm -j4

Write-Host "built build-wasm/wkv.wasm"
