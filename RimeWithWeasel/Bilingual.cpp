// SPDX-FileCopyrightText: 2026 雙語拼音輸入法 contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stdafx.h"
#include "Bilingual.h"

#include <sapi.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "sapi.lib")

namespace bilingual {

namespace {

const UINT WM_TRANSLATED = WM_APP + 1;
const UINT WM_SHOW_DETAILS = WM_APP + 2;
const wchar_t kMessageWindowClass[] = L"WeaselBilingualMessage";
const wchar_t kDetailsWindowClass[] = L"WeaselBilingualDetails";
const size_t kCacheSize = 512;
const size_t kMaxSentence = 200;

std::wstring u8tow(std::string_view s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
  return out;
}

bool IsHan(wchar_t c) {
  return (c >= 0x3400 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
         (c >= 0xD800 && c <= 0xDBFF);
}

bool HasHan(const std::wstring& s) {
  return std::any_of(s.begin(), s.end(), IsHan);
}

std::vector<std::string_view> Split(std::string_view s, char sep) {
  std::vector<std::string_view> out;
  size_t start = 0;
  for (;;) {
    size_t end = s.find(sep, start);
    out.push_back(s.substr(start, end == std::string_view::npos
                                      ? std::string_view::npos
                                      : end - start));
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  return out;
}

std::wstring Join(const std::vector<std::wstring>& items,
                  const wchar_t* sep) {
  std::wstring out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i)
      out += sep;
    out += items[i];
  }
  return out;
}

std::string ToLower(std::string_view s) {
  std::string out(s);
  for (auto& c : out)
    if (c >= 'A' && c <= 'Z')
      c = c - 'A' + 'a';
  return out;
}

// A path the narrow (ANSI) file APIs inside the model loader can open.
std::string NarrowPath(const std::wstring& path) {
  std::wstring p = path;
  if (std::any_of(p.begin(), p.end(), [](wchar_t c) { return c > 0x7F; })) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetShortPathNameW(path.c_str(), buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
      p = buf;
  }
  int n = WideCharToMultiByte(CP_ACP, 0, p.c_str(), (int)p.size(), nullptr, 0,
                              nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_ACP, 0, p.c_str(), (int)p.size(), &out[0], n,
                      nullptr, nullptr);
  return out;
}

bool ReadAll(const std::wstring& path, std::string& out) {
  std::ifstream in(std::filesystem::path(path), std::ios::binary);
  if (!in)
    return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

std::string wtou8(const std::wstring& s) {
  if (s.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0,
                              nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr,
                      nullptr);
  return out;
}

// ---------------------------------------------------------------------------
// Traditional to simplified Chinese, character by character, with OpenCC's
// table (t2s.tsv). The translation model reads simplified Chinese, and the
// details window follows the schema's 简体/繁體 (traditionalization) switch.

class Simplifier {
 public:
  void Load(const std::wstring& path) {
    if (!ReadAll(path, file_))
      return;
    std::string_view all(file_);
    size_t start = 0;
    while (start < all.size()) {
      size_t end = all.find('\n', start);
      if (end == std::string_view::npos)
        end = all.size();
      auto line = all.substr(start, end - start);
      if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
      auto tab = line.find('\t');
      if (tab != std::string_view::npos && tab > 0 && tab + 1 < line.size())
        map_[line.substr(0, tab)] = line.substr(tab + 1);
      start = end + 1;
    }
  }

  std::string Convert(std::string_view text) const {
    if (map_.empty())
      return std::string(text);
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
      unsigned char lead = text[i];
      size_t n = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
      std::string_view ch = text.substr(i, n);
      auto it = map_.find(ch);
      out += it == map_.end() ? ch : it->second;
      i += n;
    }
    return out;
  }

  std::wstring Convert(const std::wstring& text) const {
    if (map_.empty() || !std::any_of(text.begin(), text.end(), [](wchar_t c) {
          return c >= 0x3400;
        }))
      return text;
    return u8tow(Convert(wtou8(text)));
  }

 private:
  std::string file_;
  std::unordered_map<std::string_view, std::string_view> map_;
};

// ---------------------------------------------------------------------------
// Dictionaries for the details window. Each file is kept in memory as read;
// the maps point into it.

class Dictionary {
 public:
  struct ZhEntry {
    std::string_view simp, pinyin, senses;
  };

  bool Load(const std::wstring& dir, const std::wstring& data_dir) {
    if (loaded_)
      return true;
    loaded_ = true;
    bool ok = true;
    if (ReadAll(data_dir + L"\\bilingual_english.txt", english_file_)) {
      ForEachLine(english_file_, [this](std::string_view line) {
        auto tab = line.find('\t');
        if (tab != std::string_view::npos)
          english_[line.substr(0, tab)] = line.substr(tab + 1);
      });
    } else {
      ok = false;
    }
    if (ReadAll(dir + L"\\zh.tsv", zh_file_)) {
      ForEachLine(zh_file_, [this](std::string_view line) {
        auto f = Split(line, '\t');
        if (f.size() < 4)
          return;
        zh_[f[0]] = ZhEntry{f[1], f[2], f[3]};
        if (!f[1].empty() && f[1] != f[0] && !simp_to_trad_.count(f[1]))
          simp_to_trad_[f[1]] = f[0];
      });
    } else {
      ok = false;
    }
    if (ReadAll(dir + L"\\en.tsv", en_file_)) {
      ForEachLine(en_file_, [this](std::string_view line) {
        auto tab = line.find('\t');
        if (tab != std::string_view::npos)
          en_[line.substr(0, tab)] = line.substr(tab + 1);
      });
    } else {
      ok = false;
    }
    if (ReadAll(dir + L"\\examples.tsv", examples_file_)) {
      ForEachLine(examples_file_, [this](std::string_view line) {
        auto tab = line.find('\t');
        if (tab != std::string_view::npos)
          examples_[line.substr(0, tab)] = line.substr(tab + 1);
      });
    } else {
      ok = false;
    }
    return ok;
  }

  // Tables are keyed by traditional (Taiwan) forms.
  std::string_view Trad(std::string_view word) const {
    if (zh_.count(word) || english_.count(word))
      return word;
    auto it = simp_to_trad_.find(word);
    return it == simp_to_trad_.end() ? word : it->second;
  }

  const ZhEntry* Zh(std::string_view word) const {
    auto it = zh_.find(Trad(word));
    return it == zh_.end() ? nullptr : &it->second;
  }

  std::vector<std::string_view> EnglishOf(std::string_view word) const {
    auto it = english_.find(Trad(word));
    if (it == english_.end())
      return {};
    return Split(it->second, '|');
  }

  // word \t parts of speech \t forms \t definitions \t synonyms
  std::vector<std::string_view> En(const std::string& word) const {
    auto it = en_.find(ToLower(word));
    if (it == en_.end())
      return {};
    auto f = Split(it->second, '\t');
    f.resize(4);
    return f;
  }

  std::vector<std::pair<std::string_view, std::string_view>> Examples(
      std::string_view word) const {
    std::vector<std::pair<std::string_view, std::string_view>> out;
    auto it = examples_.find(Trad(word));
    if (it == examples_.end())
      return out;
    for (auto pair : Split(it->second, '\x1e')) {
      auto p = Split(pair, '\x1f');
      if (p.size() == 2)
        out.emplace_back(p[0], p[1]);
    }
    return out;
  }

 private:
  template <typename F>
  static void ForEachLine(const std::string& text, F f) {
    std::string_view all(text);
    size_t start = 0;
    while (start < all.size()) {
      size_t end = all.find('\n', start);
      if (end == std::string_view::npos)
        end = all.size();
      auto line = all.substr(start, end - start);
      if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
      if (!line.empty())
        f(line);
      start = end + 1;
    }
  }

  bool loaded_ = false;
  std::string english_file_, zh_file_, en_file_, examples_file_;
  std::unordered_map<std::string_view, std::string_view> english_;
  std::unordered_map<std::string_view, ZhEntry> zh_;
  std::unordered_map<std::string_view, std::string_view> simp_to_trad_;
  std::unordered_map<std::string_view, std::string_view> en_;
  std::unordered_map<std::string_view, std::string_view> examples_;
};

// "key\x1fitem\x1fitem\x1ekey\x1fitem" -> [(key, [item, item]), ...]
std::vector<std::pair<std::string_view, std::vector<std::string_view>>>
Groups(std::string_view field) {
  std::vector<std::pair<std::string_view, std::vector<std::string_view>>> out;
  if (field.empty())
    return out;
  for (auto group : Split(field, '\x1e')) {
    auto items = Split(group, '\x1f');
    if (items.empty())
      continue;
    std::string_view key = items.front();
    items.erase(items.begin());
    out.emplace_back(key, std::move(items));
  }
  return out;
}

// ---------------------------------------------------------------------------
// What the details window shows: lines of text, each with what to read aloud
// when it is double-clicked.

struct Line {
  std::wstring text;
  std::wstring speech;
};

struct Content {
  std::wstring title;
  std::vector<Line> lines;
  std::wstring head_speech;
  std::vector<std::wstring> example_speech;
};

// The Chinese text of the window: all of it in simplified characters, the
// labels in mainland wording too.
void Simplify(const Simplifier& s, Content& c) {
  c.title = s.Convert(c.title);
  for (auto& line : c.lines) {
    line.text = s.Convert(line.text);
    line.speech = s.Convert(line.speech);
  }
  c.head_speech = s.Convert(c.head_speech);
}

// Traditional and simplified (mainland) labels.
const wchar_t* Label(bool simplified, const wchar_t* t, const wchar_t* s) {
  return simplified ? s : t;
}

struct FormLabel {
  const char* key;
  const wchar_t* traditional;
  const wchar_t* simplified;
};

const FormLabel kFormLabels[] = {
    {"3sg", L"第三人稱", L"第三人称"},   {"past", L"過去式", L"过去式"},
    {"pp", L"過去分詞", L"过去分词"},    {"ing", L"進行式", L"现在分词"},
    {"pl", L"複數", L"复数"},            {"comp", L"比較級", L"比较级"},
    {"sup", L"最高級", L"最高级"},
};

void AddEnglishCard(const Dictionary& dict,
                    const std::string& word,
                    bool prefer_verb,
                    bool simplified,
                    Content& c) {
  auto e = dict.En(word);
  std::wstring w = u8tow(word);
  if (e.empty()) {
    c.lines.push_back({w, w});
    return;
  }
  std::vector<std::string_view> pos = Split(e[0], '\x1f');
  if (prefer_verb) {
    std::stable_partition(pos.begin(), pos.end(),
                          [](std::string_view p) { return p == "v."; });
  }
  std::vector<std::wstring> pos_w;
  for (auto p : pos)
    if (!p.empty())
      pos_w.push_back(u8tow(p));
  c.lines.push_back({w + L"    " + Join(pos_w, L" "), w});

  auto forms = Groups(e[1]);
  if (!forms.empty()) {
    std::vector<std::wstring> parts;
    std::vector<std::wstring> words;
    for (auto& form : kFormLabels) {
      for (auto& [key, items] : forms) {
        if (key == form.key && !items.empty()) {
          parts.push_back(
              std::wstring(Label(simplified, form.traditional, form.simplified)) +
              L" " + u8tow(items[0]));
          words.push_back(u8tow(items[0]));
        }
      }
    }
    if (!parts.empty())
      c.lines.push_back({L"    " + Join(parts, L"　"), Join(words, L", ")});
  }

  auto defs = Groups(e[2]);
  int shown = 0;
  for (auto p : pos) {
    for (auto& [key, items] : defs) {
      if (key != p)
        continue;
      for (auto d : items) {
        if (shown >= 3)
          break;
        std::wstring dw = u8tow(d);
        c.lines.push_back({L"    " + u8tow(p) + L" " + dw, dw});
        ++shown;
      }
    }
  }

  auto syns = Groups(e[3]);
  for (auto p : pos) {
    bool done = false;
    for (auto& [key, items] : syns) {
      if (key != p || items.empty())
        continue;
      std::vector<std::wstring> sw;
      for (auto s : items)
        sw.push_back(u8tow(s));
      std::wstring list = Join(sw, L", ");
      c.lines.push_back(
          {L"    " + std::wstring(Label(simplified, L"近義詞", L"近义词")) +
               L"　" + list,
           list});
      done = true;
      break;
    }
    if (done)
      break;
  }
}

Content BuildContent(const Dictionary& dict,
                     const std::string& chinese,
                     const std::string& english,
                     bool simplified) {
  Content c;
  std::wstring zh = u8tow(chinese);
  c.title = std::wstring(Label(simplified, L"單字詳情", L"单词详情")) + L"　" +
            (zh.empty() ? u8tow(english) : zh);

  std::vector<std::string> options;
  if (!english.empty())
    options.push_back(english);
  bool prefer_verb = false;

  if (!chinese.empty()) {
    const auto* entry = dict.Zh(chinese);
    std::wstring head = zh;
    if (entry) {
      std::wstring simp = u8tow(entry->simp);
      if (!simp.empty() && simp != zh)
        head += L"（" + simp + L"）";
      head += L"    " + u8tow(entry->pinyin);
    }
    c.lines.push_back({head, zh});
    c.head_speech = zh;
    if (entry && !entry->senses.empty()) {
      c.lines.push_back({L"", L""});
      c.lines.push_back({Label(simplified, L"【釋義】", L"【释义】"), L""});
      int i = 0;
      for (auto s : Split(entry->senses, '/')) {
        std::wstring sw = u8tow(s);
        c.lines.push_back({std::to_wstring(++i) + L". " + sw, sw});
        // "to run" among the senses means the English is a verb here.
        if (s.substr(0, 3) == "to ")
          prefer_verb = true;
      }
    }
    for (auto o : dict.EnglishOf(chinese)) {
      std::string os(o);
      if (std::find(options.begin(), options.end(), os) == options.end())
        options.push_back(os);
    }
  }

  if (!options.empty()) {
    c.lines.push_back({L"", L""});
    c.lines.push_back({L"【英文】", L""});
    size_t cards = 0;
    for (auto& o : options) {
      if (cards >= 4)
        break;
      // Options without WordNet data are listed only if picked directly.
      if (dict.En(o).empty() && o != english)
        continue;
      if (cards)
        c.lines.push_back({L"", L""});
      AddEnglishCard(dict, o, prefer_verb, simplified, c);
      ++cards;
    }
    if (!cards)
      AddEnglishCard(dict, options.front(), prefer_verb, simplified, c);
    std::wstring first = u8tow(options.front());
    c.head_speech = c.head_speech.empty() ? first : c.head_speech + L"，" + first;
  }

  auto examples = dict.Examples(chinese);
  if (!examples.empty()) {
    c.lines.push_back({L"", L""});
    c.lines.push_back({L"【例句】", L""});
    for (auto& [zh_ex, en_ex] : examples) {
      std::wstring z = u8tow(zh_ex), e = u8tow(en_ex);
      c.lines.push_back({z, z});
      c.lines.push_back({L"    " + e, e});
      c.example_speech.push_back(e);
    }
  }
  if (c.lines.size() <= 1) {
    c.lines.push_back({L"", L""});
    c.lines.push_back({Label(simplified, L"字典裡沒有這個詞的詳細資料。",
                             L"词典里没有这个词的详细资料。"),
                       L""});
  }
  c.lines.push_back({L"", L""});
  c.lines.push_back({Label(simplified, L"（連按兩下任一行就會朗讀那一行）",
                           L"（双击任意一行即可朗读该行）"),
                     L""});
  return c;
}

// ---------------------------------------------------------------------------
// Text-to-speech with SAPI. English text gets an English voice and Chinese
// text a Chinese one, whichever the system has.

class Speaker {
 public:
  ~Speaker() {
    if (voice_)
      voice_->Release();
    for (auto* t : {english_, chinese_})
      if (t)
        t->Release();
  }

  void Speak(const std::wstring& text) {
    if (text.empty())
      return;
    if (!Init())
      return;
    ISpObjectToken* token = HasHan(text) ? chinese_ : english_;
    if (token)
      voice_->SetVoice(token);
    voice_->Speak(text.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML,
                  nullptr);
  }

  void Stop() {
    if (voice_)
      voice_->Speak(L"", SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
  }

 private:
  bool Init() {
    if (voice_)
      return true;
    if (tried_)
      return false;
    tried_ = true;
    if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                                IID_ISpVoice, (void**)&voice_))) {
      voice_ = nullptr;
      return false;
    }
    english_ = FindVoice({L"Language=409", L"Language=809", L"Language=c09"});
    chinese_ = FindVoice({L"Language=404", L"Language=804", L"Language=c04"});
    voice_->SetRate(-1);
    return true;
  }

  static ISpObjectToken* FindVoice(std::initializer_list<const wchar_t*> langs) {
    // Desktop voices first, then the newer OneCore voices, which SAPI can
    // also use.
    const wchar_t* categories[] = {
        SPCAT_VOICES,
        L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech_OneCore\\Voices"};
    for (const wchar_t* lang : langs) {
      for (const wchar_t* category : categories) {
        ISpObjectTokenCategory* cat = nullptr;
        if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr,
                                    CLSCTX_ALL, IID_ISpObjectTokenCategory,
                                    (void**)&cat)))
          continue;
        ISpObjectToken* token = nullptr;
        if (SUCCEEDED(cat->SetId(category, FALSE))) {
          IEnumSpObjectTokens* tokens = nullptr;
          if (SUCCEEDED(cat->EnumTokens(lang, nullptr, &tokens)) && tokens) {
            ULONG fetched = 0;
            if (FAILED(tokens->Next(1, &token, &fetched)) || fetched == 0)
              token = nullptr;
            tokens->Release();
          }
        }
        cat->Release();
        if (token)
          return token;
      }
    }
    return nullptr;
  }

  ISpVoice* voice_ = nullptr;
  ISpObjectToken* english_ = nullptr;
  ISpObjectToken* chinese_ = nullptr;
  bool tried_ = false;
};

// ---------------------------------------------------------------------------
// The details window. It never takes the focus, so the text being typed in
// the application stays where it is.

class DetailsWindow {
 public:
  ~DetailsWindow() {
    if (hwnd_)
      DestroyWindow(hwnd_);
    if (font_)
      DeleteObject(font_);
  }

  void Show(Content content, bool simplified) {
    content_ = std::move(content);
    if (!hwnd_ && !Create())
      return;
    SetScript(simplified);
    std::wstring text;
    starts_.clear();
    for (auto& line : content_.lines) {
      starts_.push_back(text.size());
      text += line.text;
      text += L"\r\n";
    }
    SetWindowTextW(hwnd_, content_.title.c_str());
    SetWindowTextW(edit_, text.c_str());
    EnableWindow(examples_button_, !content_.example_speech.empty());
    if (!IsWindowVisible(hwnd_))
      Place();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }

 private:
  enum { ID_EDIT = 100, ID_SPEAK, ID_EXAMPLES, ID_CLOSE };

  bool Create() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
      WNDCLASSEXW wc = {sizeof(wc)};
      wc.lpfnWndProc = WndProc;
      wc.hInstance = inst;
      wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
      wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
      wc.lpszClassName = kDetailsWindowClass;
      RegisterClassExW(&wc);
      registered = true;
    }
    HDC dc = GetDC(nullptr);
    dpi_ = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kDetailsWindowClass, L"",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, CW_USEDEFAULT,
        CW_USEDEFAULT, Scale(520), Scale(560), nullptr, nullptr, inst, this);
    if (!hwnd_)
      return false;
    edit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL,
        0, 0, 0, 0, hwnd_, (HMENU)(INT_PTR)ID_EDIT, inst, nullptr);
    SetWindowLongPtrW(edit_, GWLP_USERDATA, (LONG_PTR)this);
    edit_proc_ = (WNDPROC)SetWindowLongPtrW(edit_, GWLP_WNDPROC,
                                            (LONG_PTR)EditProc);
    speak_button_ = Button(L"", ID_SPEAK);
    examples_button_ = Button(L"", ID_EXAMPLES);
    close_button_ = Button(L"", ID_CLOSE);
    Layout();
    return true;
  }

  // Button labels and font for simplified or traditional Chinese.
  void SetScript(bool simplified) {
    int script = simplified ? 1 : 2;
    if (script == script_)
      return;
    script_ = script;
    SetWindowTextW(speak_button_, Label(simplified, L"朗讀", L"朗读"));
    SetWindowTextW(examples_button_,
                   Label(simplified, L"朗讀例句", L"朗读例句"));
    SetWindowTextW(close_button_, Label(simplified, L"關閉", L"关闭"));
    HFONT old = font_;
    font_ = CreateFontW(
        -MulDiv(11, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        Label(simplified, L"Microsoft JhengHei UI", L"Microsoft YaHei UI"));
    for (HWND w : {edit_, speak_button_, examples_button_, close_button_})
      SendMessageW(w, WM_SETFONT, (WPARAM)font_, TRUE);
    if (old)
      DeleteObject(old);
  }

  HWND Button(const wchar_t* text, int id) {
    return CreateWindowExW(0, L"BUTTON", text,
                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
                           hwnd_, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(nullptr), nullptr);
  }

  int Scale(int v) const { return MulDiv(v, dpi_, 96); }

  void Layout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int pad = Scale(8), bh = Scale(32), bw = Scale(120);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    MoveWindow(edit_, pad, pad, w - 2 * pad, h - 3 * pad - bh, TRUE);
    int y = h - pad - bh;
    MoveWindow(speak_button_, pad, y, bw, bh, TRUE);
    MoveWindow(examples_button_, 2 * pad + bw, y, bw, bh, TRUE);
    MoveWindow(close_button_, w - pad - Scale(90), y, Scale(90), bh, TRUE);
  }

  // Next to the mouse (which just right-clicked a candidate), on screen.
  void Place() {
    POINT pt;
    GetCursorPos(&pt);
    RECT wr;
    GetWindowRect(hwnd_, &wr);
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    RECT work = mi.rcWork;
    int x = pt.x + Scale(24), y = pt.y - h / 3;
    if (x + w > work.right)
      x = pt.x - Scale(24) - w;
    x = std::max<int>(work.left, std::min<int>(x, work.right - w));
    y = std::max<int>(work.top, std::min<int>(y, work.bottom - h));
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
  }

  std::wstring Selection() {
    DWORD start = 0, end = 0;
    SendMessageW(edit_, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    if (end <= start)
      return {};
    int n = GetWindowTextLengthW(edit_);
    std::wstring text(n + 1, L'\0');
    GetWindowTextW(edit_, &text[0], n + 1);
    if (end > (DWORD)n)
      end = n;
    return text.substr(start, end - start);
  }

  void OnCommand(int id) {
    switch (id) {
      case ID_SPEAK: {
        std::wstring s = Selection();
        speaker_.Speak(s.empty() ? content_.head_speech : s);
        break;
      }
      case ID_EXAMPLES:
        speaker_.Speak(Join(content_.example_speech, L"\n"));
        break;
      case ID_CLOSE:
        speaker_.Stop();
        ShowWindow(hwnd_, SW_HIDE);
        break;
    }
  }

  void OnDoubleClick(LPARAM lParam) {
    LRESULT r = SendMessageW(edit_, EM_CHARFROMPOS, 0, lParam);
    size_t pos = LOWORD(r);
    auto it = std::upper_bound(starts_.begin(), starts_.end(), pos);
    if (it == starts_.begin())
      return;
    size_t i = (it - starts_.begin()) - 1;
    if (i < content_.lines.size())
      speaker_.Speak(content_.lines[i].speech);
  }

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self =
        reinterpret_cast<DetailsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
      switch (msg) {
        case WM_MOUSEACTIVATE:
          return MA_NOACTIVATE;
        case WM_SIZE:
          self->Layout();
          return 0;
        case WM_GETMINMAXINFO: {
          auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
          mmi->ptMinTrackSize = {self->Scale(380), self->Scale(240)};
          return 0;
        }
        case WM_COMMAND:
          self->OnCommand(LOWORD(wp));
          return 0;
        case WM_CTLCOLORSTATIC:
          // A read-only edit draws like a static; keep it white like a page.
          if ((HWND)lp == self->edit_) {
            SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
            SetTextColor((HDC)wp, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
          }
          break;
        case WM_CLOSE:
          self->speaker_.Stop();
          ShowWindow(hwnd, SW_HIDE);
          return 0;
        case WM_DESTROY:
          self->hwnd_ = nullptr;
          break;
      }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }

  static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self =
        reinterpret_cast<DetailsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_LBUTTONDBLCLK) {
      LRESULT r = CallWindowProcW(self->edit_proc_, hwnd, msg, wp, lp);
      self->OnDoubleClick(lp);
      return r;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
      self->OnCommand(ID_CLOSE);
      return 0;
    }
    return CallWindowProcW(self->edit_proc_, hwnd, msg, wp, lp);
  }

  HWND hwnd_ = nullptr;
  HWND edit_ = nullptr;
  HWND speak_button_ = nullptr;
  HWND examples_button_ = nullptr;
  HWND close_button_ = nullptr;
  WNDPROC edit_proc_ = nullptr;
  HFONT font_ = nullptr;
  int dpi_ = 96;
  int script_ = 0;  // 1 simplified, 2 traditional
  Content content_;
  std::vector<size_t> starts_;
  Speaker speaker_;
};

// ---------------------------------------------------------------------------
// bilingual_mt.dll, see bilingual\mt.

typedef void* (*LoadFn)(const char*, const char*, const char*, const char*);
typedef int (*TranslateFn)(void*, const char*, char*, int);
typedef void (*FreeFn)(void*);

struct DetailsRequest {
  std::string chinese;
  std::string english;
  bool simplified;
};

}  // namespace

class Service::Impl {
 public:
  Impl(const std::wstring& install_dir, const std::wstring& data_dir)
      : dir_(install_dir + L"\\bilingual"), data_dir_(data_dir) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = MessageProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kMessageWindowClass;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(0, kMessageWindowClass, L"", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, wc.hInstance, this);
    simplifier_.Load(dir_ + L"\\t2s.tsv");
    worker_ = std::thread([this] { Work(); });
  }

  ~Impl() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable())
      worker_.join();
    if (engine_ && free_)
      free_(engine_);
    if (dll_)
      FreeLibrary(dll_);
    details_.reset();
    if (hwnd_)
      DestroyWindow(hwnd_);
  }

  std::wstring Translate(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    wanted_ = text;
    if (unavailable_ || text.size() > kMaxSentence * 3)
      return {};
    auto it = cache_.find(text);
    if (it != cache_.end())
      return it->second;
    pending_ = text;
    cv_.notify_one();
    return {};
  }

  std::wstring TranslateWait(const std::string& text, unsigned timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    wanted_ = text;
    if (unavailable_ || text.size() > kMaxSentence * 3)
      return {};
    // The caller uses the translation itself: no refresh of the client.
    nudged_ = text;
    auto ready = [&] { return unavailable_ || cache_.count(text) != 0; };
    if (!ready()) {
      pending_ = text;
      cv_.notify_one();
      done_.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready);
    }
    auto it = cache_.find(text);
    if (it == cache_.end()) {
      // Too slow: refresh the client once it is ready after all.
      nudged_.clear();
      return {};
    }
    return it->second;
  }

  void Idle() {
    std::lock_guard<std::mutex> lock(mutex_);
    wanted_.clear();
    pending_.clear();
  }

  bool Available() {
    std::lock_guard<std::mutex> lock(mutex_);
    return !unavailable_;
  }

  void ShowDetails(const std::string& chinese,
                   const std::string& english,
                   bool simplified) {
    auto* request = new DetailsRequest{chinese, english, simplified};
    if (!hwnd_ || !PostMessageW(hwnd_, WM_SHOW_DETAILS, 0, (LPARAM)request))
      delete request;
  }

 private:
  void Work() {
    // Load the model right away (about a second), so that the first sentence
    // is translated as quickly as the rest.
    if (!LoadEngine()) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        unavailable_ = true;
        pending_.clear();
      }
      done_.notify_all();
      // Have the client drop the "translating" line.
      PostMessageW(hwnd_, WM_TRANSLATED, 0, 0);
    }
    for (;;) {
      std::string text;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return stop_ || !pending_.empty(); });
        if (stop_)
          return;
        text.swap(pending_);
      }
      if (!engine_)
        continue;
      // The model reads simplified Chinese.
      std::string source = simplifier_.Convert(text);
      std::string out(1024, '\0');
      int n = translate_(engine_, source.c_str(), &out[0], (int)out.size());
      if (n >= (int)out.size()) {
        out.assign(n + 1, '\0');
        n = translate_(engine_, source.c_str(), &out[0], (int)out.size());
      }
      std::wstring result = n > 0 ? u8tow(std::string_view(out.data(), n))
                                  : std::wstring();
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cache_.size() >= kCacheSize) {
          cache_.erase(order_.front());
          order_.pop_front();
        }
        if (cache_.emplace(text, result).second)
          order_.push_back(text);
      }
      done_.notify_all();
      PostMessageW(hwnd_, WM_TRANSLATED, 0, 0);
    }
  }

  bool LoadEngine() {
#ifdef _WIN64
    dll_ = LoadLibraryExW((dir_ + L"\\bilingual_mt.dll").c_str(), nullptr,
                          LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll_)
      return false;
    auto load = (LoadFn)GetProcAddress(dll_, "bmt_load");
    translate_ = (TranslateFn)GetProcAddress(dll_, "bmt_translate");
    free_ = (FreeFn)GetProcAddress(dll_, "bmt_free");
    if (!load || !translate_ || !free_)
      return false;
    // Firefox Translations, simplified Chinese to English; one vocabulary
    // for both languages.
    std::wstring mt = dir_ + L"\\mt\\";
    std::string vocab = NarrowPath(mt + L"vocab.zhen.spm");
    engine_ = load(NarrowPath(mt + L"model.zhen.intgemm.alphas.bin").c_str(),
                   vocab.c_str(), vocab.c_str(),
                   NarrowPath(mt + L"lex.50.50.zhen.s2t.bin").c_str());
    return engine_ != nullptr;
#else
    // The translation engine is built for 64-bit Windows only.
    return false;
#endif
  }

  // A translation is ready. If it is for the text being typed, have the input
  // client ask again, the same way Weasel refreshes after a mouse click.
  void OnTranslated() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (wanted_.empty() || wanted_ == nudged_ ||
          !(unavailable_ || cache_.count(wanted_)))
        return;
      nudged_ = wanted_;
    }
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_SELECT;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_SELECT;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
  }

  void OnShowDetails(DetailsRequest* request) {
    std::unique_ptr<DetailsRequest> owned(request);
    dict_.Load(dir_, data_dir_);
    if (!details_)
      details_ = std::make_unique<DetailsWindow>();
    Content content = BuildContent(dict_, request->chinese, request->english,
                                   request->simplified);
    if (request->simplified)
      Simplify(simplifier_, content);
    details_->Show(std::move(content), request->simplified);
  }

  static LRESULT CALLBACK MessageProc(HWND hwnd,
                                      UINT msg,
                                      WPARAM wp,
                                      LPARAM lp) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && msg == WM_TRANSLATED) {
      self->OnTranslated();
      return 0;
    }
    if (self && msg == WM_SHOW_DETAILS) {
      self->OnShowDetails(reinterpret_cast<DetailsRequest*>(lp));
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }

  std::wstring dir_;
  std::wstring data_dir_;
  HWND hwnd_ = nullptr;

  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;    // a text to translate, or stop
  std::condition_variable done_;  // a translation is ready
  bool stop_ = false;
  bool unavailable_ = false;
  std::string pending_;
  std::string wanted_;
  std::string nudged_;
  std::unordered_map<std::string, std::wstring> cache_;
  std::deque<std::string> order_;

  HMODULE dll_ = nullptr;
  void* engine_ = nullptr;
  TranslateFn translate_ = nullptr;
  FreeFn free_ = nullptr;

  Simplifier simplifier_;  // read-only once loaded, by both threads
  Dictionary dict_;
  std::unique_ptr<DetailsWindow> details_;
};

Service::Service(const std::wstring& install_dir, const std::wstring& data_dir)
    : impl_(std::make_unique<Impl>(install_dir, data_dir)) {}

Service::~Service() = default;

std::wstring Service::Translate(const std::string& text) {
  return impl_->Translate(text);
}

std::wstring Service::TranslateWait(const std::string& text,
                                    unsigned timeout_ms) {
  return impl_->TranslateWait(text, timeout_ms);
}

void Service::Idle() {
  impl_->Idle();
}

bool Service::Available() {
  return impl_->Available();
}

void Service::ShowDetails(const std::string& chinese,
                          const std::string& english,
                          bool simplified) {
  impl_->ShowDetails(chinese, english, simplified);
}

}  // namespace bilingual
