// SPDX-FileCopyrightText: 2026 雙語拼音輸入法 contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

// 雙語拼音: English for the Chinese being typed.
//
// - Sentence translation with an offline model (bilingual_mt.dll), shown as
//   the candidate window's auxiliary line.
// - A details window for a word: pinyin, Chinese senses, English parts of
//   speech, definitions, forms, synonyms and example sentences, read aloud
//   with Windows text-to-speech.
#pragma once

#include <windows.h>
#include <memory>
#include <string>

namespace bilingual {

class Service {
 public:
  // Create on the server's UI thread. `install_dir` holds the bilingual
  // folder; `data_dir` is Rime's shared data folder.
  Service(const std::wstring& install_dir, const std::wstring& data_dir);
  ~Service();

  // The English translation of `text` (UTF-8) if it is ready. Otherwise the
  // text is queued and an empty string is returned; once the translation is
  // ready, the input client is asked to refresh, which brings it back here.
  std::wstring Translate(const std::string& text);
  // The same, but waits up to `timeout_ms` for the translation, for typing it
  // right away. Empty if it is not ready by then.
  std::wstring TranslateWait(const std::string& text, unsigned timeout_ms);
  // Nothing is being typed; drop any queued translation.
  void Idle();
  // False once the translation engine failed to load (or on 32-bit Windows).
  bool Available();

  // Opens the details window. Either word may be empty; both are UTF-8.
  // Safe to call from any thread.
  void ShowDetails(const std::string& chinese, const std::string& english);

  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace bilingual
