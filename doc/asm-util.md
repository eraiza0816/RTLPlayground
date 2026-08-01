# 数値パース・出力フォーマットのアセンブラ化計画

## Overview

既存の `rtlplayground_mem.asm` (メモリ/文字列関数) に続き、数値パース系と出力フォーマット系の関数を
sdas8051 で再実装し、コードサイズ削減と WebUI/CLI の高速化を狙う。

ターゲット: RTL837x (MCS-51 / DW8051 core), SDCC 4.5.0, `--opt-code-size` (デフォルト)

---

## 移植対象

### 候補1: 数値パース

| 関数 | ファイル | 現在 | 備考 |
|------|---------|------|------|
| `atoi_byte`   | cmd_parser.c | 229B | 8bit 10進+範囲チェック (`num>25`) |
| `atoi_short`  | cmd_parser.c | 75B  | 16bit 10進。`*10` が `__mulint` を呼ぶ |
| `parse_short` | httpd.c      | 95B  | Web クエリ数値。`__mulint` 使用 |

### 候補2: 出力フォーマット

| 関数 | ファイル | 現在 | 備考 |
|------|---------|------|------|
| `string_to_html` | page_impl.c    | 173B | outbuf 追記をインライン化 (1文字ごとの `char_to_html` lcall を排除) |
| `itoa`           | rtlplayground.c | 73B  | 10進表示 (`div ab`×2 + `_write_char`) |
| `print_byte`     | rtlplayground.c | 65B  | 16進ニブル→文字 (`swap a` 利用) |

### 保留 (リスク高・効果低)
- `atoi_hex` (117B): `hexvalue[]` へのニブル詰め込みとエンディアン補正が複雑なため C のまま維持

---

## 実装方針

- 新規 `rtlplayground_util.asm` を作成 (`rtlplayground_mem.asm` と分離)
- 全関数を **CSEG (ホーム領域)** に配置 → 全バンクから `lcall` で呼び出し可能
- SDCC 呼び出し規約:
  - 第1引数 = DPTR (uint8 の単一引数は DPL)
  - 残り引数 = `_<func>_PARM_n` (OSEG/DATA or XSEG)
  - 戻り値: uint8 → DPL
- PARM 定義: `_atoi_byte_PARM_2`, `_atoi_short_PARM_2` (実測 OSEG 0x73 / 0x1C, DATA)
- グローバル参照 (外部シンボル):
  - `_cmd_buffer` (XDATA) — atoi_byte/atoi_short
  - `_short_parsed` (XDATA, uint16) — parse_short
  - `_outbuf` (XDATA), `_slen` (XDATA, uint16) — string_to_html
- 16bit `*10` は `mul ab`×2 + carry 伝播で実装 (lib 呼び出し不要)
- `_write_char` を lcall (itoa / print_byte)
- C 側定義を削除し、Makefile のリンク行に `.rel` を追加

---

## 各関数の移植仕様

### atoi_byte(`__xdata uint8_t *out`, `uint8_t idx`) → uint8
- `cmd_buffer[idx]` が数字 (0x30-0x39) の間ループ
- `num = num*10 + digit` (8bit, `mul ab`)
- 範囲: `num > 25 || (num == 25 && digit > 5)` → return 1 (オーバーフロー)
- 正常終了: `*out = num; return 0` / 数字ゼロ個: `*out = 0; return 1`

### atoi_short(`__xdata uint16_t *vlan`, `uint8_t idx`) → uint8
- 同様に 16bit 累積 (`mul ab`×2)
- 範囲: `*vlan > 6553 || (*vlan == 6553 && digit > 5)` → return 1
- `*vlan` は引数 DPTR 経由で書込

### parse_short(`__xdata uint8_t *p`) → uint8
- `*p++ - '0'` が 0-9 の間、グローバル `_short_parsed` (uint16) に 16bit 累積
- 非数字で break → return 0/1

### string_to_html(`__code char *s`) → void
- `while (*s) outbuf[slen++] = *s++;` (char_to_html インライン化)
- `_slen` を XDATA から読んで `_outbuf[_slen]` に書込み後インクリメント

### itoa(`uint8_t v`) → void
- `v/100`, `(v/10)%10`, `v%10` を `div ab`×2 で算出し `_write_char` で出力
- 先行ゼロ抑制 (`print_zeros`)

### print_byte(`uint8_t a`) → void
- 上位/下位ニブルを `swap a` + `add a,#'0'` + (必要なら +7) で hex 文字化し `_write_char`

---

## 検証手順

- [x] s51 テストハーネスで境界値検証 (36ケース全件 PASS: 0/255/65535/オーバーフロー/空/非数値/先頭0/部分文字列)
- [x] `make -j16` で PCB_K0402WS_V3 / SWGT024_V2_0_MANAGED 両マシンビルド
- [x] マップで配置・サイズ確認
- [ ] 実機へアップロードし WebUI/CLI 動作確認

## 実測結果

| 関数 | 旧(C) | 新(asm) | 配置 |
|------|-------|---------|------|
| `atoi_byte`   | 75B | 69B | BANK2 |
| `atoi_short`  | 75B | 84B | BANK2 |
| `parse_short` | 95B | 73B | BANK1 |
| `string_to_html` | 173B | 73B | BANK1 |
| `itoa`        | 73B | 64B | HOME (CSEG) |
| `print_byte`  | 65B | 50B | HOME (CSEG) |

- 合計コード: 116,323B → 116,270B (**-53B**)。ホーム領域 (CSEG) はほぼ不変 (-8B)、
  解放分は BANK2 (-41B, 最もタイトなバンク) と BANK1 (-4B) に寄与。
- コード削減は小さいが、主目的は**速度**:
  - `string_to_html` が outbuf 追記をインライン化 → ページ描画の 1文字ごとの
    `char_to_html` lcall を排除 (WebUI 応答が高速化)
  - `atoi_short`/`parse_short` が `__mulint` 呼び出しを排除 (CLI/Web の数値パース高速化)
- **`__mulint` は uIP スタック (uip-fw.c, uip-neighbor.c) と cmd_commit が
  構造体インデックス用に使用するため、リンクからは除去できない** (27B 維持)。

## 検証中に発見したバグ

- 16bit `*10` を `mul ab` 2回で実装する際、高位バイトの `mul ab` は `carry_hi`
  (hi*10 の 9bit目) を捨ててしまう。atoi_short ではオーバーフロー検査を**検査→乗算**の順に
  行い acc <= 6553 (hi*10 <= 250) を保証して回避。parse_short は uint16 の mod 65536
  ラップ (C と同義) を意図し、carry_hi は破棄する仕様として実装。

## 期待効果

- コード **~53B 削減** (ホーム領域はほぼ不変、BANK2 が -41B と最もタイトなバンクを解放)
- `string_to_html` の lcall 排除により **WebUI 描画が高速化**
- atoi 系の `__mulint` 呼び出し排除により **CLI/Web の数値パースが高速化**
