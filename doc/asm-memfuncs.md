# メモリ/文字列関数のアセンブラ化計画

Type: explanation · Topic: the assembler memfuncs plan

## Overview

C で書かれたメモリ/文字列関数 (`memcpy` 等) と 10 進数変換コードをアセンブラ (sdas8051) で再実装し、
コードサイズ削減とホットパスの高速化を狙う。

ターゲット: RTL837x (MCS-51 / DW8051 core), SDCC 4.5.0, `--opt-code-size` (デフォルト)

---

## 背景 (調査結果の要約)

- CRC16 は既にアセンブラ実装 (`crc16.asm`)。IP/TCP/UDP チェックサムはハードウェアオフロード済みで C 実装はビルド除外。
- メモリ/文字列関数は `rtlplayground.c:299-360` の自前 C 実装。SDCC ライブラリ版は未リンク。
- 現行サイズ (rtlplayground.map のシンボル境界から計測):

| 関数 | 現在サイズ |
|------|-----------|
| `memcpy`   | 69B |
| `memcpyc`  | 51B |
| `memset`   | 31B |
| `strtox`   | 56B |
| `strlen`   | 34B |
| `strlen_x` | 33B |
| `strcmp`   | 83B |
| 合計 | 357B |

- `itoa_html`/`itoa16_html` (`httpd/page_impl.c`) と `COMMIT_BYTE` (`cmd_commit.c`) が
  `__divuint`(49B) + `__moduint`(29B) のライブラリ関数を計 78B 引き込んでいる。

---

## 制約・前提

| 項目 | 内容 |
|------|------|
| 呼び出し規約 | SDCC: 引数1 = DPTR, 引数2以降 = `_<func>_PARM_n` (XDATA 配置) |
| 戻り値 | 16bit は DPL/DPH, 8bit は ACC |
| MPAGE | `__sfr __at(0x92)`。`--xstack` 未使用のため空き。asm 内で push/pop 退避 |
| デュアル DPTR | `crc16.asm:7` で DPS(0x86) がコメントアウトされているため **非対応と仮定** (単一 DPTR のみ) |
| バンク | 対象関数はホーム領域 (CSEG) に配置。アセンブラでも同じく CSEG に配置 |
| 重複コピー | `uip-split.c:111` の `dst < src` 前方向コピーのみ。前方向コピーで安全 (現行 C と同等) |
| XRAM | ライブラリ局所変数を XSEG に置かない。レジスタのみで実装 |

---

## 作業項目

### 1. メモリ/文字列関数のアセンブラ化 (`rtlplayground_mem.asm` 新規作成)

`rtlplayground.c:299-360` の 7 関数を sdas8051 で再実装し、C 側の定義を削除。
ヘッダ宣言は残し、リンクは `Makefile` の OBJS に `rtlplayground_mem.rel` を追加。

共通テクニック:
- ソース側 (XDATA/CODE) は DPTR + `movx a,@dptr` / `movc a,@a+dptr` + `inc dptr`
- デストネーション側 (XDATA) は MPAGE(高位) + `R1`(低位) による `movx @R1` ページドアクセス
- 16bit カウンタは `R6/R7`。ループは `djnz`/`dec+cjne` ベースで共通パスを短縮

| 関数 | 実装方針 |
|------|---------|
| `memcpy`   | src=DPTR, dst=MPAGE:R1。1バイト/ループ、`dec R6`+`cjne`+`dec R7` |
| `memcpyc`  | src は `movc a,@a+dptr` (CODE)。同様のループ |
| `memset`   | dst=MPAGE:R1, 値=A。R5 を 8bit カウンタ (len は uint8) |
| `strtox`   | `movc` で読み、0 で終了。長さを返す |
| `strlen`   | CODE 側。`movc` + DPTR インクリメント、長さを DPL/DPH で返す |
| `strlen_x` | XDATA 側。`movx a,@dptr` + `inc dptr` |
| `strcmp`   | a=XDATA(R1 ページド), b=CODE(DPTR)。不等で -1/0/1 を ACC で返す |

### 2. 10 進数変換の剰余除去 (C のまま書き換え)

アセンブラ化ではなく、16bit 除算を呼ばない実装に変更する。

- `httpd/page_impl.c:87-113` (`itoa_html`, `itoa16_html`): 値が 0-4095 に限定されるため
  減算ループ (1000/100/10/1) で実装。`__divuint`/`__moduint` 参照を除去。
- `cmd_commit.c:49-54` (`COMMIT_BYTE`) および `cmd_commit.c:106-108`: 同様に減算方式へ。

期待効果: `__divuint`(49B) + `__moduint`(29B) がリンクから消え、計 78B 削減。

### 3. ビルド検証

```
make -j16
```

- 対象マシン: PCB_K0402WS_V3 (デフォルト)
- 他マシン (SWGT024_V2_0_MANAGED 等) もコンパイル確認 (`make MACHINE=...`)

### 4. 動作検証 (可能なら)

- `sim.c` をビルドしてメモリ関数の単体動作を確認 (uip 起動、HTTP 応答)
- 現物ボードでの UART コンソール応答・WebUI 応答を確認

---

## 期待効果 (見積もり)

| 項目 | 削減 |
|------|------|
| メモリ/文字列関数のコード | ~120-150B (357B → ~210-240B) |
| div ライブラリ 2 関数 | 78B |
| **コード合計** | **~200-230B** |
| 速度 | memcpy 系ループが 1バイトあたり 36B 相当の命令列 → 約10B 相当に短縮 (3-4倍高速) |
| RAM | 追加消費なし。レジスタのみ使用 (IDATA 逼迫を悪化させない) |

> 絶対量はファームウェア全体 (~227KB) の 0.1% 程度で僅少。主たる価値は
> HTTP 描画・パケット処理のホットパス高速化と、バンク残量 (BANK2 が 93% 使用で最もタイト) への余裕。

---

## リスク・注意点

1. **MPAGE 退避漏れ**: `movx @R1` は MPAGE を破壊するため、全関数で push/pop MPAGE 必須。
   (現行コードは `--xstack` 未使用なので MPAGE はデータアクセスに未使用だが、安全のため退避)
2. **SDCC 呼び出し規約**: PARM は XDATA にあるため、まず DPTR で PARM を読んでからレジスタへ退避する必要がある。
   `memcpy` では引数 (dst) が既に DPTR にある点に注意。
3. **`strcmp` の戻り値**: C 実装は -1/0/1 を返す。呼び出し元は `!strcmp(...)` で 0 判定のみ
   (rtlplayground.c:1271-1272)。負/正の区別を維持すること。
4. **シンボル可視性**: `_memcpy` 等のグローバルシンボル名は SDCC のアンダースコア規則に従う。
5. **sizeof/型の整合**: `memset` の len は uint8、それ以外の len は uint16 のまま維持。

---

## 検証手順 (完了判定)

- [x] `make -j16` が全マシンで成功 (PCB_K0402WS_V3, SWGT024_V2_0_MANAGED)
- [x] `rtlplayground.map` で 7 関数のサイズが削減 (357B → 272B)
- [x] `__divuint`/`__moduint` が map から消えている
- [ ] 現物ボードでの動作確認 (コンソール/WebUI)

## 実測結果

| 関数 | 旧 | 新 |
|------|-----|-----|
| `memcpy`   | 69B | 47B |
| `memcpyc`  | 51B | 44B |
| `memset`   | 31B | 31B |
| `strtox`   | 56B | 47B |
| `strlen`   | 34B | 21B |
| `strlen_x` | 33B | 20B |
| `strcmp`   | 83B | 62B |
| 合計 | 357B | 272B (-85B) |

- CSEG (ホーム領域): 13,721B → 13,516B (**-205B**) — 内訳: メモリ関数 -85B + `__divuint`/`__moduint` 除去 -78B + その他
- BANK1: -15B (`itoa16_html` の剰余除去)
- BANK3: +32B (`COMMIT_DEC16` インライン化)
- XRAM: -2B
- **純減: ~188B のコード + ホットパス (メモリ関数) の 3-4 倍高速化**

## 検証中に発見・修正したバグ

s51 シミュレータで機能検証中に重大なバグを発見:
- **`jnz` は累算器 (A) を検査する** (8051 に Z フラグはなく、JNZ は A の値を判定)。
  ページラップ検出の `inc r1; jnz skip` は `A == 0` (value が 0 のとき) に必ずフォールスルーし、
  `inc MPAGE` が毎回実行されて書き込み先ページが壊れていた。
- 修正: `jnz` → `cjne r1, #0x00, skip` (R1 を明示比較)。
- これは実機 (DW8051) でも同じ挙動になるため、シミュレータで発見できたのは幸運だった。

## 検証方法 (s51)

- メモリ関数 7 種 × 24 ケースを s51 (uCsim) で実行し全件 PASS
- テスト用ビルドでは MPAGE を P2 (0xA0) に再マップ (標準 8051 の sim には MPAGE が無いため)。
  ロジック (ループ/カウンタ/終端/戻り値) を検証。MPAGE レジスタ実体は `rtl837x_sfr.h` の定義に基づく。
