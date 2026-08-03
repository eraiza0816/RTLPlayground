# メモリ使用量の調査と軽量化計画

## 背景

ChaCha20-Poly1305 AEAD (PSK 認証) の実装で、コードと再入スタックが
増加し、容量がギリギリになった。8051 (内部 RAM 128B) の制約が特に厳しい。
2026-08 時点の調査結果と軽量化の計画をまとめる。

## 現状のメモリ使用量 (PCB_K0402WS_V3, v0.2.22-6f6623c)

| 領域 | 使用量 | 上限 | 残り | 備考 |
|---|---|---|---|---|
| DSEG (内部 RAM) | 126B | 128B | **2B** | グローバル + 関数ローカル (sloc) |
| OSEG (オーバーレイ) | 11B | - | **実質 0B** | 11B 連続領域が確保できずリンク失敗することがある |
| SSEG (再入スタック) | 129B | 129B | **0B** | 実行時に再入フレームが積まれる |
| XSEG (XDATA) | 13,696B | 65,536B | 51,840B | 大きなバッファは既に XDATA |
| CSEG (非バンク CODE) | 13,497B | 16,384B | 2,887B | アセンブラ移植の容量制約にも影響 |
| BANK1 / BANK2 / BANK3 | 64% / 70% / 44% | 64KB 各 | 余裕 | |

ボトルネックは **内部 RAM (DSEG + OSEG + SSEG)**。XDATA と BANK は余裕がある。

## 各領域の内訳 (判明していること)

### SSEG (再入スタック) 129B
- crypto チェーン (aead_encrypt/decrypt → chacha20 / poly1305) が約 112B
  - poly1305_mul (C 版) が `__mullong` (32bit 乗算ライブラリ) を
    ブロックごとに 7 回呼び、その再入フレームが積まれる
- 残り ~17B が httpd / telnetd 等の再入フレーム

### DSEG (内部 RAM) 126B
- 非静的グローバル変数と関数の一時領域 (sloc) が IDATA に置かれる
  (SDCC small model のデフォルト)
- .map からは内訳を特定できない (コンパイラのアロケーションに依存)

### OSEG (オーバーレイ) 11B
- 非再入関数のローカル変数 (オーバーレイ共有)
- 断片化により 11B 連続領域が確保できない場合がある

### XDATA 13,696B の主な内訳
- _outbuf (HTTP 応答バッファ) 2,500B
- _uip_buf 2,202B / _sfp_pw_pending 2,316B / _xmodem_active 1,588B
- _cmd_history 1,024B / _vlan_names 1,024B / _uip_udp_conns 578B 等

### CODE サイズ (モジュール別上位)
- cmd_parser 22KB / page_impl 12KB / rtlplayground 11KB / **poly1305 10KB**
- httpd 7.9KB / uip 7.6KB / rtl837x_port 7.6KB / cmd_help 7.4KB

## 軽量化の候補 (優先順)

### 1. poly1305 の 32bit 演算 → 16bit/8bit 最適化 【優先】
- **現状**: poly1305 が 10,409B (init 1,396 行 / finish 2,790 行 / mul 2,221 行 /
  blocks 1,137 行のアセンブラ行)。32bit 演算 (`__mullong`) のコード生成が肥大。
- **根拠**: `U8TO32(key+0) & 0x3ffffff` のように、32bit 読みの結果は
  全て 26bit にマスクされる。上位ビット (p[3] 等) は不要で、
  16bit/8bit 演算に分解できる。`__mullong` を避ければコードと
  再入フレームの両方が減る。
- **対象**:
  - `poly1305_init`: U8TO32 の組み立てと 26bit マスク
  - `poly1305_blocks`: h += msg の 26bit 加算 (U8TO32 + mask)
  - `poly1305_finish`: h の 130bit パッキング (32bit 演算の 16bit 化)
  - `poly1305_mul`: 13bit 分割乗算を 16bit 演算に (既に 26bit リムだが
    32bit テンポラリの生成コードが大きい)
- **期待効果**: CODE 数 KB 削減 + SSEG (__mullong フレーム) 削減
- **検証**: ucsim で RFC7539/8439 ベクタ + ホスト gcc との相互比較、
  実機で enc_client T1-T4

### 2. DSEG (内部 RAM) の軽量化
- `__xdata` 指定で IDATA の変数を XDATA (51.8KB 空き) に移す
- 内訳の特定にはコンパイラのアロケーション情報の詳細調査が必要
- **効果**: DSEG の残り 2B → 数十 B に

### 3. OSEG 不足の解消
- 非再入関数の大きなローカルを `static __xdata` へ
- 11B 連続領域不足の解消

### 4. CSEG 82% の解消
- 大きな関数 (page_impl 12KB 等) を BANK へ移動 (__banked)
- CSEG の残り 2,887B を確保 (将来のアセンブラ移植の余地にも)

## 進め方

1. poly1305 の 16bit 最適化を実装 (候補 1)
2. ホスト (gcc) で RFC ベクタ・相互比較を確認
3. ucsim (SDCC ビルド) でベクタ確認
4. 実機で enc_client T1-T4 + rtlpctl を確認
5. メモリ使用量 (.map) で削減効果を確認
6. 必要に応じて候補 2-4 を実施

## 試した検証 (不採用)

- httpd_appcall を `__reentrant` 化: DSEG は不変、SSEG が 129B → 139B に増加
  → 不採用 (元に戻した)

## 実験: poly1305_mul の 16bit 化 (mul13_32) — 結果: 逆効果

13bit x 13bit 乗算を 8/16bit 演算に分解し、SDCC の `__mullong`
(32bit 乗算ライブラリ) を避ける `mul13_32` ヘルパーを実装・検証した。

### 正しさの検証 (合格)
- ホスト (gcc): 境界値 + ランダム 200 万ケースで `x * y` と完全一致
- ホスト: RFC7539 §2.5.2 ベクタ PASS
- ucsim (SDCC ビルド): RFC7539/8439 全ベクタ PASS

### サイズへの影響 (PCB_K0402WS_V3)
| 領域 | 変更前 | 変更後 | 差分 |
|---|---|---|---|
| BANK3 (poly1305 含む) | 29,178B | 29,548B | **+370B** |
| SSEG | 129B | 132B | **+3B** |
| OSEG | 11B | 4B (リンク失敗) | **7-11B 連続不足でエラー** |

### 結論
- **16bit 化は逆効果**。`__mullong` の共有ライブラリコードより、
  個別に展開する 16bit 演算コードの方が大きい (8051/SDCC の特性)。
- `__mullong` の再入フレームは実行時ピーク (112B) に含まれており、
  SSEG 129B に収まっているため、回避の必要性は低い。
- poly1305 の CODE 10KB 削減は「16bit 化」では達成できない。
- **OSEG は危険水域** (11B しかなく、コード変更で連続領域不足の
  リンクエラーが発生する)。OSEG の余裕を作ることが急務。

## 今後の候補 (優先順)

1. **OSEG の余裕作り**: 非再入関数の大きなローカルを `static __xdata` へ
   (OSEG のオーバーレイ断片化を解消)
2. **DSEG (内部 RAM) の軽量化**: `__xdata` 指定で変数を XDATA へ
   (XDATA は 51.8KB 空き)
3. **CSEG 82%**: 大きな関数 (page_impl 12KB 等) を BANK へ移動

## 実施: OSEG の削減 (PARM を XSEG へ) — 成功

memcpy/memcpyc/memset の長さ引数 (len) の PARM を OSEG から XSEG に移した。

### 変更内容
- `rtl837x_common.h`: memcpy len, memcpyc len, memset len を `__xdata` 引数に
- `rtlplayground_mem.asm`: 該当 PARM を XSEG エリアへ移動し、読みを movx に変更

### サイズへの影響 (PCB_K0402WS_V3)
| 領域 | 変更前 | 変更後 |
|---|---|---|
| OSEG | 11B | **8B (-3B)** |
| SSEG | 129B | 132B (+3B, 実行時ピークは安全) |
| CSEG | 13,497B | 13,528B (+31B) |

### 検証 (実機)
- enc_client T1-T4: 3 連続 PASS
- rtlpctl: status / vlan list / counters / enc-cmd (hostname 変更) / info 全て OK
- WebUI: index/login/information.json 200 (セッション切れで 401 になる現象あり)
- デバイス安定 (ping OK)

### 制約: __code ポインタ引数は XSEG 化不可
- memcpyc の src / strtox の s / strcmp の b (`__code` ポインタ) は
  SDCC 構文 (`__xdata __code uint8_t *` = 二重 storage class エラー、
  `__code uint8_t * __xdata` = PARM は移らない) の制約で OSEG に残る。
- OSEG の下限は 6B (これらの PARM) + オーバーレイ 2B = 8B。
- ただし「9B 以上の OSEG フレーム」を持つ関数を追加するとリンク失敗する。

### 見送り事項
- httpd_appcall の DSEG 12B: コンパイラ生成の sloc のためソース変更では移せない
  (__reentrant 化は SSEG +10B で不採用)。
- DSEG 126B/128: ローカル主体で XSEG 移行の効果が薄い。実害はない。
- CSEG 82%: 残り 2,856B。rtlplayground.c (10.9KB) の BANK 移動は効果大だが
  BANK3 は 44% と余裕があり、必要になった時点で検討。

## 最終状態 (2026-08-02)
| 領域 | 使用 | 備考 |
|---|---|---|
| DSEG | 126/128B | 残 2B |
| OSEG | 8B | 9B 以上のフレームは不可 |
| SSEG | 132/132B (静的) | 実行時ピークは余裕あり |
| CSEG | 13,528/16,384B (82%) | 残 2,856B |
| XSEG | 13,698/65,536B | 51.8KB 空き |
| BANK1/2/3 | 63% / 70% / 44% | BANK3 に余裕 |
