# 雙語拼音輸入法：Windows 版

以小狼毫（[rime/weasel](https://github.com/rime/weasel)，GPL-3.0）為基礎，加上：

- **簡體**：候選和輸出是簡體字（方案選單的「简体／繁體」可以暫時切到繁體，用臺灣字形），整句那一行和單字詳情也跟著用簡體。
- **候選旁的英文**：每個中文候選旁顯示英文；第一個候選後面列出它的英文（標示〔英〕），按數字鍵就輸出英文。
- **整句翻譯**：打一句話時，候選窗上方顯示整句的英文（離線翻譯，不用網路）。按 **Ctrl+Enter** 或點一下那一行，就輸出英文。
- **成語、俗語和口語**：翻譯模型會把成語和俗語逐字翻（指点江山 → "pointing to the mountains"，万事总有开头 → "Everything always starts."），所以只打一個詞或一句俗語時，整句那一行和 Ctrl+Enter 都用英文表裡的英文，和候選旁的英文一樣（指点江山 → talk idly about important matters；俗語用英文的說法：万事总有开头 → There's a first time for everything）。後面帶句號、問號，或是「嘛、啊」，也一樣認得出來。一句俗語的拼音打全，整句就出來（wan shi zong you kai tou → 万事总有开头，以前是「玩失踪有开头」）。句子裡有常用成語或口語時，先換成白話再翻（跟他讲道理就是对牛弹琴 → "To reason with him is a waste of words."；對照表是 `bilingual/data/idioms.tsv`，215 條成語、40 多條口語，每條都拿例句比過換和不換的翻譯）。口語裡省掉的東西會補上（他吃我的住我的还整天骂我 → "He ate my food, lived in my house, and scolded me all day."），沒打逗號的長句會在「但是、所以、然后」這類連接詞，和「…了为什么」前面斷開。候選旁的英文，成語只列意思、不列字面義。
- **逐詞選好再決定**：整句的第一個候選不對時，可以用數字鍵一個詞一個詞選。選完最後一個詞，整句會先留在候選窗，不會自動送出；這時按**空白鍵**或 **Enter** 打中文，按 **Ctrl+Enter** 或點「整句」那一行打英文。按 Backspace 會退回最後一個詞重選。一次選整句的話照舊直接打出。
- **單字詳情**：在候選詞上**按滑鼠右鍵**，看拼音、中文釋義、英文詞性、釋義、動詞變形、近義詞和例句。按「朗讀」或連按兩下任一行，用 Windows 的語音朗讀。
- **記住習慣**：選過的英文下次排前面（存在使用者資料夾的 `bilingual_usage.txt`）；中文詞頻由 Rime 自己學。
- **整句選字**：詞庫用[白霜拼音](https://github.com/gaboolic/rime-frost)的簡體詞庫，加上 Rime 的簡體語言模型（octagram）。拿 4,468 句沒調過參數的句子測，整句一次打對的比例從原本明月拼音的 59.7% 升到 65.5%（日常句），48.0% 升到 58.4%（網路文章）。
- 小狼毫原本的功能都保留。

## 檔案

| 位置 | 內容 |
| --- | --- |
| `RimeWithWeasel/Bilingual.*` | 整句翻譯的背景執行緒、單字詳情視窗、朗讀 |
| `RimeWithWeasel/RimeWithWeasel.cpp` | 把翻譯放進候選窗、Ctrl+Enter、右鍵指令 |
| `WeaselUI/WeaselPanel.cpp`、`WeaselTSF/CandidateList.cpp` | 候選窗的右鍵和點擊翻譯 |
| `bilingual/mt/` | `bilingual_mt.dll`：用 [slimt](https://github.com/DavidVentura/slimt) 執行 Firefox Translations 的簡中→英模型（桌面版 Firefox 用的 base 模型），以 MinGW-w64 編譯（繁體字先用 `t2s.tsv` 轉成簡體再翻） |
| `bilingual/rime/` | 雙語拼音的 Rime 方案、詞典（匯入白霜拼音的詞庫，加上它沒有的 137 條俗語）、以簡體詞為鍵的英文表（含 CC-CEDICT 兩段式的俗語，和本專案補寫的常用俗語、網路用語）、Lua（候選旁的英文、逐詞選好的整句先留著） |
| `bilingual/data/` | 單字詳情的資料（CC-CEDICT、WordNet、LemmInflect、Tatoeba 整理而成；CC-CEDICT 的長釋義已用 2026-11-03 版補全）；`t2s.tsv` 是繁轉簡字表（取自 OpenCC 的 TSCharacters）；`idioms.tsv` 是翻譯前把成語和口語換成白話的對照表 |
| `bilingual/fetch-models.sh` | 下載翻譯模型、語言模型和白霜拼音的詞庫（太大，沒放進 git；固定版本並核對 SHA-256） |
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

整體以 GPL-3.0 釋出。另含：slimt（GPL-2.0 以上）、Firefox Translations 模型（MPL-2.0）、OpenCC 繁簡字表（Apache-2.0）、rime-octagram 簡體語言模型（LGPL-3.0）、CC-CEDICT（CC BY-SA 4.0）、WordNet、Tatoeba（CC BY 2.0 FR）、LemmInflect（MIT）、白霜拼音詞庫（GPL-3.0）。
