// SPDX-FileCopyrightText: 2026 雙語拼音輸入法 contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Test tool: bmt_cli <model dir> < sentences.txt

#include <cstdio>
#include <iostream>
#include <string>

#include "bilingual_mt.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: bmt_cli <model dir>\n");
    return 2;
  }
  std::string dir = argv[1];
  void* engine = bmt_load((dir + "/model.zhen.intgemm.alphas.bin").c_str(),
                          (dir + "/srcvocab.zhen.spm").c_str(),
                          (dir + "/trgvocab.zhen.spm").c_str(),
                          (dir + "/lex.50.50.zhen.s2t.bin").c_str());
  if (!engine) {
    std::fprintf(stderr, "load failed\n");
    return 1;
  }
  std::string line;
  char out[4096];
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    int n = bmt_translate(engine, line.c_str(), out, sizeof out);
    std::printf("%s\t%s\n", line.c_str(), n < 0 ? "(failed)" : out);
    std::fflush(stdout);
  }
  bmt_free(engine);
  return 0;
}
