// SPDX-FileCopyrightText: 2026 雙語拼音輸入法 contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bilingual_mt.h"

#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "slimt/Io.hh"
#include "slimt/slimt.hh"

namespace {

struct Engine {
  std::shared_ptr<slimt::Model> model;
  std::unique_ptr<slimt::Blocking> service;
};

// slimt does not read the model's architecture, so take the layer counts
// from the parameter names (encoder_l1_..., decoder_l4_...).
void detect_layers(const std::string& path, size_t& encoder, size_t& decoder) {
  slimt::io::MmapFile file(path);
  for (const auto& item : slimt::io::load_items(file.data())) {
    auto check = [&](const char* prefix, size_t& out) {
      size_t n = std::strlen(prefix);
      if (item.name.compare(0, n, prefix) != 0)
        return;
      size_t index = 0;
      size_t pos = n;
      while (pos < item.name.size() && item.name[pos] >= '0' &&
             item.name[pos] <= '9') {
        index = index * 10 + (item.name[pos] - '0');
        ++pos;
      }
      if (pos > n && index > out)
        out = index;
    };
    check("encoder_l", encoder);
    check("decoder_l", decoder);
  }
}

}  // namespace

extern "C" {

void* bmt_load(const char* model,
               const char* source_vocab,
               const char* target_vocab,
               const char* shortlist) {
  try {
    slimt::Package<std::string> package{
        .model = model,
        .vocabulary = source_vocab,
        .target_vocabulary = target_vocab,
        .shortlist = shortlist,
        .ssplit = "",
    };
    slimt::Model::Config config = slimt::preset::tiny();
    size_t encoder = 0;
    size_t decoder = 0;
    detect_layers(package.model, encoder, decoder);
    if (encoder > 0)
      config.encoder_layers = encoder;
    if (decoder > 0)
      config.decoder_layers = decoder;

    auto engine = std::make_unique<Engine>();
    engine->model = std::make_shared<slimt::Model>(config, package);
    slimt::Config service_config;
    service_config.cache_size = 256;
    engine->service = std::make_unique<slimt::Blocking>(service_config);
    return engine.release();
  } catch (const std::exception&) {
    return nullptr;
  }
}

int bmt_translate(void* handle, const char* text, char* out, int out_size) {
  auto* engine = static_cast<Engine*>(handle);
  if (engine == nullptr || text == nullptr)
    return -1;
  try {
    std::vector<std::string> sources{text};
    auto responses = engine->service->translate(
        engine->model, std::move(sources), slimt::Options{});
    if (responses.empty())
      return -1;
    const std::string& target = responses.front().target.text;
    if (out != nullptr && out_size > 0) {
      size_t n = target.size() < size_t(out_size - 1) ? target.size()
                                                      : size_t(out_size - 1);
      std::memcpy(out, target.data(), n);
      out[n] = '\0';
    }
    return static_cast<int>(target.size());
  } catch (const std::exception&) {
    return -1;
  }
}

void bmt_free(void* handle) {
  delete static_cast<Engine*>(handle);
}

}  // extern "C"
