// SPDX-FileCopyrightText: 2026 雙語拼音輸入法 contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Offline Chinese -> English sentence translation (slimt + Firefox
// Translations models), as a plain C DLL so any compiler can load it.

#pragma once

#ifdef _WIN32
#define BMT_API __declspec(dllexport)
#else
#define BMT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Loads a model. Paths are UTF-8. Returns NULL on failure.
BMT_API void* bmt_load(const char* model,
                       const char* source_vocab,
                       const char* target_vocab,
                       const char* shortlist);

// Translates UTF-8 `text` into `out` (NUL-terminated, at most `out_size`
// bytes). Returns the length of the full translation, which may be larger
// than `out_size - 1`, or -1 on failure.
BMT_API int bmt_translate(void* engine,
                          const char* text,
                          char* out,
                          int out_size);

BMT_API void bmt_free(void* engine);

#ifdef __cplusplus
}
#endif
