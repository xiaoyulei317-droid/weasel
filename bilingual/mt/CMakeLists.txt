# SPDX-FileCopyrightText: 2026 雙語拼音輸入法 contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later

# bilingual_mt.dll: offline sentence translation for WeaselServer.
# Built with MinGW-w64 (see build.sh), loaded at run time with LoadLibrary.

cmake_minimum_required(VERSION 3.16)
project(bilingual_mt CXX C)

set(CMAKE_CXX_STANDARD 20)
set(WITH_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(BUILD_JNI OFF CACHE BOOL "" FORCE)
set(USE_BUILTIN_SENTENCEPIECE ON CACHE BOOL "" FORCE)
set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)

# Keep IEEE math like marian does; fused multiply-add garbles quantized output.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffp-contract=off")

set(SLIMT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/slimt")
include_directories("${SLIMT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/slimt")
add_subdirectory(${SLIMT_SOURCE_DIR} slimt EXCLUDE_FROM_ALL)
# slimt builds with -Werror; MinGW headers trip harmless warnings.
target_compile_options(slimt-static PRIVATE -Wno-error)

add_library(bilingual_mt SHARED bilingual_mt.cc)
set_target_properties(bilingual_mt PROPERTIES PREFIX "")
target_link_libraries(bilingual_mt PRIVATE slimt-static)
target_link_options(bilingual_mt PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(bmt_cli bmt_cli.cc)
target_link_libraries(bmt_cli PRIVATE bilingual_mt)
target_link_options(bmt_cli PRIVATE -static -static-libgcc -static-libstdc++)
