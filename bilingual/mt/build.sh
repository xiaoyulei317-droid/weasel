#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 雙語拼音輸入法 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Cross-compiles bilingual_mt.dll (64-bit Windows) with MinGW-w64.
# On Ubuntu: apt install mingw-w64 cmake ninja-build
set -euo pipefail
cd "$(dirname "$0")"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=mingw-w64-x86_64.cmake
ninja -C build bilingual_mt bmt_cli
