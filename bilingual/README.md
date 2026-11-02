# 雙語拼音輸入法：Windows 版

以小狼毫（[rime/weasel](https://github.com/rime/weasel)，GPL-3.0）為基礎，加上：

- **候選旁的英文**：每個中文候選旁顯示英文；第一個候選後面列出它的英文（標示〔英〕），按數字鍵就輸出英文。
- **整句翻譯**：打一句話時，候選窗上方顯示整句的英文（離線翻譯，不用網路）。按 **Ctrl+Enter** 或點一下那一行，就輸出英文。
- **逐詞選好再決定**：整句的第一個候選不對時，可以用數字鍵一個詞一個詞選。選完最後一個詞，整句會先留在候選窗，不會自動送出；這時按**空白鍵**或 **Enter** 打中文，按 **Ctrl+Enter** 或點「整句」那一行打英文。按 Backspace 會退回最後一個詞重選。一次選整句的話照舊直接打出。
- **單字詳情**：在候選詞上**按滑鼠右鍵**，看拼音、中文釋義、英文詞性、釋義、動詞變形、近義詞和例句。按「朗讀」或連按兩下任一行，用 Windows 的語音朗讀。
- **記住習慣**：選過的英文下次排前面（存在使用者資料夾的 `bilingual_usage.txt`）；中文詞頻由 Rime 自己學。
- **整句選字**：附 Rime 的語言模型（octagram），整句拼音選字比較準。
- 小狼毫原本的功能都保留。

## 檔案

| 位置 | 內容 |
| --- | --- |
| `RimeWithWeasel/Bilingual.*` | 整句翻譯的背景執行緒、單字詳情視窗、朗讀 |
| `RimeWithWeasel/RimeWithWeasel.cpp` | 把翻譯放進候選窗、Ctrl+Enter、右鍵指令 |
| `WeaselUI/WeaselPanel.cpp`、`WeaselTSF/CandidateList.cpp` | 候選窗的右鍵和點擊翻譯 |
| `bilingual/mt/` | `bilingual_mt.dll`：用 [slimt](https://github.com/DavidVentura/slimt) 執行 Firefox Translations 的繁中→英模型，以 MinGW-w64 編譯 |
| `bilingual/rime/` | 雙語拼音的 Rime 方案、英文表、Lua（候選旁的英文、逐詞選好的整句先留著） |
| `bilingual/data/` | 單字詳情的資料（CC-CEDICT、WordNet、LemmInflect、Tatoeba 整理而成） |
| `bilingual/fetch-models.sh` | 下載翻譯模型和語言模型（太大，沒放進 git） |
| `bilingual/install-data.py` | 把上面的檔案放進安裝檔 |

## 建置

GitHub Actions（`.github/workflows/ci.yml`）會自動建置：先在 Linux 上編譯 `bilingual_mt.dll` 並下載模型，再在 Windows 上編譯小狼毫和安裝檔。`bilingual` 分支每次更新，都會把安裝檔放到 Releases 的「bilingual-latest」。

自己在 Windows 上建置：照小狼毫的 `INSTALL.md` 準備好環境，在 `build.bat data` 之後執行

```sh
bash bilingual/mt/build.sh            # 需要 mingw-w64（或在 Linux 上編好複製過來）
bash bilingual/fetch-models.sh models
python bilingual/install-data.py --models models --dll bilingual/mt/build/bilingual_mt.dll
build.bat installer
```

整句翻譯只有 64 位元 Windows 有（32 位元 Windows 上其他功能照常）。

## 授權

整體以 GPL-3.0 釋出。另含：slimt（GPL-2.0 以上）、Firefox Translations 模型（MPL-2.0）、rime-octagram 語言模型（LGPL-3.0）、CC-CEDICT（CC BY-SA 4.0）、WordNet、Tatoeba（CC BY 2.0 FR）、LemmInflect（MIT）、Rime 明月拼音（LGPL-3.0）。
