#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 雙語拼音輸入法 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Puts the 雙語拼音 files where the installer (output/install.nsi) takes them.

    python bilingual/install-data.py --models <fetch-models.sh output> --dll <bilingual_mt.dll>

Run after `build.bat data`, which fills output/data with the stock schemas.
"""
import argparse
import glob
import os
import re
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.join(ROOT, "bilingual")
OUT = os.path.join(ROOT, "output")


def copy(src, dst_dir):
    os.makedirs(dst_dir, exist_ok=True)
    shutil.copy2(src, dst_dir)
    print(f"{os.path.relpath(src, ROOT)} -> {os.path.relpath(dst_dir, ROOT)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", required=True)
    ap.add_argument("--dll", required=True)
    args = ap.parse_args()

    data = os.path.join(OUT, "data")
    for f in glob.glob(os.path.join(HERE, "rime", "*.*")):
        copy(f, data)
    for f in glob.glob(os.path.join(HERE, "rime", "lua", "*.lua")):
        copy(f, os.path.join(data, "lua"))
    copy(os.path.join(args.models, "zh-hans-t-essay-bgw.gram"), data)
    # The word lists bilingual_pinyin.dict.yaml imports (cn_dicts/, cn_dicts_cell/).
    words = os.path.join(args.models, "rime")
    for f in glob.glob(os.path.join(words, "*", "*.dict.yaml")):
        copy(f, os.path.join(data, os.path.relpath(os.path.dirname(f), words)))

    target = os.path.join(OUT, "bilingual")
    for f in glob.glob(os.path.join(HERE, "data", "*.tsv")):
        copy(f, target)
    copy(args.dll, target)
    for f in glob.glob(os.path.join(args.models, "mt", "*")):
        copy(f, os.path.join(target, "mt"))

    # 雙語拼音 first in the schema list, so it is the default.
    path = os.path.join(data, "default.yaml")
    text = open(path, encoding="utf-8").read()
    if "schema: bilingual_pinyin" not in text:
        text, n = re.subn(r"^schema_list:\n", "schema_list:\n  - schema: bilingual_pinyin\n",
                          text, count=1, flags=re.M)
        if n != 1:
            raise SystemExit("schema_list not found in default.yaml")
        open(path, "w", encoding="utf-8", newline="\n").write(text)
        print("default.yaml: bilingual_pinyin added to schema_list")


if __name__ == "__main__":
    main()
