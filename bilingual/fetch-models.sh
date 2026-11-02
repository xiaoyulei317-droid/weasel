#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 雙語拼音輸入法 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Downloads what is too big for git, into $1 (default: models):
#  - Firefox Translations zh-Hans -> en (translations-models-v2, 3.0 base, the
#    model desktop Firefox uses), MPL-2.0 -> mt/. It reads simplified Chinese;
#    traditional is converted first.
#  - rime-octagram zh-hans language model (lotem/rime-octagram-data@hans),
#    LGPL-3.0 -> zh-hans-t-essay-bgw.gram
#  - the word lists of 白霜拼音 (gaboolic/rime-frost, GPL-3.0) -> rime/cn_dicts/,
#    rime/cn_dicts_cell/; bilingual/rime/bilingual_pinyin.dict.yaml imports them.
set -euo pipefail
out=${1:-models}

# fetch <file> <sha256> <url> [zstd]
fetch() {
  if [ -f "$1" ] && echo "$2  $1" | sha256sum -c --quiet 2>/dev/null; then return; fi
  mkdir -p "$(dirname "$1")"
  if [ "${4:-}" = zstd ]; then
    curl -fsSL --retry 3 "$3" | zstd -dq -o "$1" -f
  else
    curl -fsSL --retry 3 -o "$1" "$3"
  fi
  echo "$2  $1" | sha256sum -c --quiet
}

mt=https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/translations-models-v2
while read -r id name sha; do
  fetch "$out/mt/$name" "$sha" "$mt/$id.zst" zstd
done <<'LIST'
570592b5-224e-4ad9-96cd-7a4905f0f044 lex.50.50.zhen.s2t.bin cdcad3592dc2bc4676c34c4d37203f7649ee989195cf083cbb60f1ea011f976b
ef9e8f1a-b8c5-4626-a134-ff74bd8628ae model.zhen.intgemm.alphas.bin 3535442962ec8f4a553cc19b206befcac689ee9cddaea44fa91e21527fc30ac2
4c5d89d3-9412-4a61-a843-dfc1ed70565d vocab.zhen.spm dff594318ab7d8b7b60b844ab98ebe6b932ae8045fab15235404c787715965b3
LIST

fetch "$out/zh-hans-t-essay-bgw.gram" \
  d3cb2438c1fdcd6a855dd6ca8f5c1060a29273c6b64c2c2c69af67cd71b6aa7e \
  https://raw.githubusercontent.com/lotem/rime-octagram-data/hans/zh-hans-t-essay-bgw.gram

frost=https://raw.githubusercontent.com/gaboolic/rime-frost/3a242664138b930ef76f92504afdc819679b6a1a
while read -r sha table; do
  fetch "$out/rime/$table.dict.yaml" "$sha" "$frost/$table.dict.yaml"
done <<'LIST'
5a6bb545d07140406208728aeed70706e84279daeee132f10b069cd6387a042a cn_dicts/8105
b2115eae3aea58190424c22825f6aea12f781ae23ea82b62cd951c41f6d576d5 cn_dicts/41448
cb1b9cb35eeb06dd04264b87d1312be7141209b118ba811e5298a9fd70e77acd cn_dicts/GB18030-2022
db5825b653107eb349627d39ae74833208e06d52eb0231ce10c3bb59b94e4f86 cn_dicts/base
44b78e4feb8a3b302298844061626aa4e2194901b8ed4ee6acec84261b6aa90d cn_dicts/ext
7a5f34ce7f1206152e41b8661af77f0055a807c60801302742afb99e1ac9474d cn_dicts/others
18161a6286a8751da2cae0777eafba3b8b4957dbacbe31ce2b3bfac90478cc2a cn_dicts/corrections
f58f41c6167aac2c34fe73f6eeacc00693d27d5adb16b8d977acfd33fa76172f cn_dicts_cell/medication
83bac41f9c637d5653772a4a20665cd10f73ac1762cc3a979b236a65a6c8d8e0 cn_dicts_cell/industry_product
d5f8bda70bb621f82ed85e4e8dbe8386c81effb856ff82ebb5c708d18ae993c1 cn_dicts_cell/exthot
2233505702dd3223fa7c73ead8f15da3541cd9047516fa834e1e5d7c022b6568 cn_dicts_cell/chess
94c37cc0ace9c67caed6f30866eb06f765aa5a6df31bff8d4f1599ce5b33073f cn_dicts_cell/chess2
c57169a197638b25dc550496418ba3e1732ea3fdba981eb566f4b6b98a306479 cn_dicts_cell/animal
fecdb2cb6a8d0ed4e814afca227615d9e0664dd75b8cc485bf2a130a4b730a43 cn_dicts_cell/game
341d777d4fd077ddb534e47f8ba27a8dd80d085fd0b847548a97a6666d644113 cn_dicts_cell/idiom
4e48defd9ab1cd99e1a439af7a8e75b95a05a148411f5b8dd2120eaddfed19e4 cn_dicts_cell/sport
028f3513a33290f1d8b97b215775238294a0086af0e7d675a449ba220609f220 cn_dicts_cell/media
87fd06c33065443f68a16ab2faa3f12ec0c42b4cd582a1b18001bc5b599d008e cn_dicts_cell/shulihua
1423b4aa5a421145454dff855c0c3a4b2a8540ec3e45dea95ace3870f2996cd7 cn_dicts_cell/food
20008c6997112c269061d45572d353df5b9bd83c47bbd1c5bc35ccf64c7855f6 cn_dicts_cell/inputmethod
ef2a7e08e9d2729d36d1e24cbd3f3444cca1b0eb4fae11cc0e0f925cf1525305 cn_dicts_cell/history
38dd1c72c4119f47a607fc542c528b54ead9c819cfbd5f7f508f8b4be1aae88b cn_dicts_cell/place
f78cec3f88f6bbcb8970c6174d68819df16d0f1a7731fa8ecf30f7e15602b956 cn_dicts_cell/geography
556cf9fa06032a6b41fb0727eed1e93dd739a6fde6c48a4d90b00a9a9272e5b4 cn_dicts_cell/name2
80d0c4fe33384c2b42f131a6ef464cb3ef7e2523b7e141412bc7afa5ca997e75 cn_dicts_cell/literature
0b5822b6ca5ce738b50bdb524cebc86b74a733cc0afe47426c24ec97b6d65c33 cn_dicts_cell/music
a92fe61d48b53d1d20f1e66be4ca83ac2e8be0caa1fa3f383c5c15de6cb7d5ea cn_dicts_cell/computer
a5f872f95f07028b5085667dc6ededfd02215e5431d6dfec0efa197e6f6445a3 cn_dicts_cell/composite
23a6706dfae9cc6cd850735ef5fbc36a6c3fb20221f47384ca889c5e17a28744 cn_dicts_cell/luna
a02ee62dd39cfdb5d2bda137c0df8586eefced6e1016a773a2eea8e28be1cf5f cn_dicts_cell/name
LIST
