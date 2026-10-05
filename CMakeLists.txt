cmake_minimum_required(VERSION 3.20)
project(AHLC VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ahmc
    src/main.cpp
    src/analyzer/analyzer.cpp
    src/bitio/bit_writer.cpp
    src/bitio/bit_reader.cpp
    src/rle/rle_encoder.cpp
    src/rle/rle_decoder.cpp
    src/image/image.cpp
    src/image/image_predictor.cpp
    src/audio/audio.cpp
    src/audio/audio_predictor.cpp
    src/entropy/huffman.cpp
)

target_include_directories(ahmc PRIVATE src)

if(MSVC)
    target_compile_options(ahmc PRIVATE /W4)
else()
    target_compile_options(ahmc PRIVATE -Wall -Wextra)
endif()
