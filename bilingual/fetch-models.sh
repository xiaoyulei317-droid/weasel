#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 雙語拼音輸入法 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Downloads the two models that are too big for git, into $1 (default: models):
#  - Firefox Translations zh-Hant -> en (translations-models-v2, 3.0), MPL-2.0 -> mt/
#  - rime-octagram zh-hant language model (lotem/rime-octagram-data@hant), LGPL-3.0
set -euo pipefail
out=${1:-models}
mkdir -p "$out/mt"
base=https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/translations-models-v2
while read -r id name sha; do
  if [ -f "$out/mt/$name" ] && echo "$sha  $out/mt/$name" | sha256sum -c --quiet 2>/dev/null; then continue; fi
  curl -fsSL "$base/$id.zst" | zstd -dq -o "$out/mt/$name" -f
  echo "$sha  $out/mt/$name" | sha256sum -c --quiet
done <<'LIST'
931f8f78-1b3e-4c16-8514-2b17df2cf9cf lex.50.50.zhen.s2t.bin aa7daf6cfc85c0cd2c10e2944d66f6da55497c9c6408789f3adfded4074c2fb1
32debe3a-b7f3-4e08-a771-e0cfa573d1fa model.zhen.intgemm.alphas.bin 0aee91790894458f5d367551f6edcd4c9cb97852c34f221bcbf9f4701ebcf0cd
981d1f3a-8b0f-4773-bde8-1fb82af4e654 srcvocab.zhen.spm 5cc6a76611dbf86219f109141533606b15ecb34eee83673bb86b2c16b14734db
113f0d05-ad64-4311-b74f-9366f2a8c628 trgvocab.zhen.spm 7bf002db37c10d3b114cc5588d7fdcb16c57d0fd1e2c34354c22cc9f0b6c3c29
LIST

gram=$out/zh-hant-t-essay-bgw.gram
sha=0488ebd6688f900a39200f2b794f2f99bcbf1e8fc27280ae4a2324b08b1559c1
if ! { [ -f "$gram" ] && echo "$sha  $gram" | sha256sum -c --quiet 2>/dev/null; }; then
  curl -fsSL -o "$gram" https://raw.githubusercontent.com/lotem/rime-octagram-data/hant/zh-hant-t-essay-bgw.gram
  echo "$sha  $gram" | sha256sum -c --quiet
fi
