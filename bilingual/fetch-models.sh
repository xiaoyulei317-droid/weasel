#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 雙語拼音輸入法 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Downloads the two models that are too big for git, into $1 (default: models):
#  - Firefox Translations zh-Hans -> en (translations-models-v2, 3.1 base-memory),
#    MPL-2.0 -> mt/. It reads simplified Chinese; traditional is converted first.
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
a2261cbe-7fe7-498d-a128-c08d918b42fc lex.50.50.zhen.s2t.bin 8524dd4c93ffd7f0ae7be32f77a7c14fea7cdbbebe34ea6ad0ec368d5ddd8b13
8bb74789-fad9-40e0-8f49-d3e017106138 model.zhen.intgemm.alphas.bin 5cd149601802fc8a18124a1c1306144dbbedc058630c4ddb2d53aa76fa9c7c06
369efa59-d98e-4aa9-8a91-363ba16c3ba7 vocab.zhen.spm dff594318ab7d8b7b60b844ab98ebe6b932ae8045fab15235404c787715965b3
LIST

gram=$out/zh-hant-t-essay-bgw.gram
sha=0488ebd6688f900a39200f2b794f2f99bcbf1e8fc27280ae4a2324b08b1559c1
if ! { [ -f "$gram" ] && echo "$sha  $gram" | sha256sum -c --quiet 2>/dev/null; }; then
  curl -fsSL -o "$gram" https://raw.githubusercontent.com/lotem/rime-octagram-data/hant/zh-hant-t-essay-bgw.gram
  echo "$sha  $gram" | sha256sum -c --quiet
fi
