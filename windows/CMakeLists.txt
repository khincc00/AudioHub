# Build backend Windows AudioHub: engine (dari engine/ bersama macOS) dan backend WASAPI.
# Dari folder windows/:
#   cmake -S . -B build -A x64
#   cmake --build build --config Release
# Hasil: build/io/Release/audiohub_io.dll
cmake_minimum_required(VERSION 3.20)
project(AudioHubWindows LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
endif()

set(AH_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Engine routing: kode yang sama dengan macOS, tanpa perubahan.
add_library(ah_engine STATIC
    ${AH_ROOT}/engine/api/audiohub_engine.cpp
    ${AH_ROOT}/engine/dsp/dsp.cpp
    ${AH_ROOT}/engine/graph/graph.cpp)
target_include_directories(ah_engine PUBLIC
    ${AH_ROOT}/engine/api
    ${AH_ROOT}/engine/dsp
    ${AH_ROOT}/engine/graph)

# DLL yang dipakai aplikasi WinUI lewat P/Invoke.
add_library(audiohub_io SHARED
    io/src/ah_io.cpp
    io/src/devices.cpp
    io/src/devices.h
    io/src/ring.h
    io/include/audiohub_io.h
    io/AudioHubIO.def)
target_include_directories(audiohub_io PRIVATE io/src io/include)
target_link_libraries(audiohub_io PRIVATE ah_engine ole32 mmdevapi propsys uuid)
set_target_properties(audiohub_io PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/io/$<CONFIG>
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/io/$<CONFIG>)

# Uji unit engine dijalankan dari tests/unit/run.sh (macOS). Tes engine di Windows belum disiapkan.
