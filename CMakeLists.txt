cmake_minimum_required(VERSION 3.20)
project(AHLC VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------------------------------------
# FFmpeg — provided by vcpkg via FindFFMPEG.cmake
# vcpkg installs FindFFMPEG.cmake into <triplet>/share/ffmpeg/.
# We append it to CMAKE_MODULE_PATH so find_package resolves it.
# VCPKG_INSTALLED_DIR is set automatically by the vcpkg toolchain.
# ----------------------------------------------------------------
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND CMAKE_MODULE_PATH
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ffmpeg")
else()
    # Fallback: search relative to the toolchain (project-local vcpkg_installed)
    list(APPEND CMAKE_MODULE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/share/ffmpeg")
endif()

find_package(FFMPEG REQUIRED MODULE)

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
    src/media/format_detector.cpp
)

target_include_directories(ahmc PRIVATE
    src
    ${FFMPEG_INCLUDE_DIRS}   # libavutil/, libavcodec/, etc.
)

target_link_libraries(ahmc PRIVATE
    ${FFMPEG_LIBRARIES}      # avutil.lib, avcodec.lib, avformat.lib, swscale.lib, swresample.lib
)

if(MSVC)
    target_compile_options(ahmc PRIVATE /W4)
    # Suppress narrowing-conversion warnings from FFmpeg's own headers
    # (C4244: int->uint8_t etc. in libavutil/common.h — not our code)
    target_compile_options(ahmc PRIVATE /wd4244 /wd4305)
else()
    target_compile_options(ahmc PRIVATE -Wall -Wextra)
endif()

# ----------------------------------------------------------------
# Post-build: copy FFmpeg DLLs next to ahmc.exe
# Required on Windows so the exe can find them at runtime.
# Uses the vcpkg_installed bin directory (no hard-coded absolute paths).
# ----------------------------------------------------------------
if(WIN32)
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(_ffmpeg_bin "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    else()
        set(_ffmpeg_bin "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/bin")
    endif()
    add_custom_command(TARGET ahmc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_ffmpeg_bin}/avutil-61.dll"
            "${_ffmpeg_bin}/avcodec-63.dll"
            "${_ffmpeg_bin}/avformat-63.dll"
            "${_ffmpeg_bin}/avdevice-63.dll"
            "${_ffmpeg_bin}/avfilter-12.dll"
            "${_ffmpeg_bin}/swscale-10.dll"
            "${_ffmpeg_bin}/swresample-7.dll"
            $<TARGET_FILE_DIR:ahmc>
        COMMENT "Copying FFmpeg DLLs to output directory"
    )
endif()
