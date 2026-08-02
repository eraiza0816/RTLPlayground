# 共通鍵認証 (PSK + ChaCha20-Poly1305 AEAD) 実装計画

## 1. 目的

[issue #250](https://github.com/logicog/RTLPlayground/issues/250) の議論 (eraiza0816 提案) にある
「Switch firmware — implement an encrypted API endpoint layer (PSK + ChaCha20 from PR #207)
on top of the current JSON API」を実現する。

現行の WebUI 認証は平文 HTTP の POST `pwd=` 比較 + セッション Cookie のみ (httpd/httpd.c:478) であるため、
- 共通鍵 (PSK) の所持証明による認証 (タグ検証)
- API ペイロードの機密性 (ChaCha20 暗号化)
- 改ざん検知 (Poly1305 タグ)

を追加し、外部管理アプリケーションが安全に JSON API を利用できる暗号化 API 層を提供する。

## 2. 背景・既存アセット

### 2.1 既存ブランチの状況

| ブランチ | 内容 | 扱い |
|---|---|---|
| `main` | 現在のベース | ここからブランチ `feat/chacha20-poly1305-aead` を作成済み |
| PR #207 (`chacha20`) | 8051 アセンブラ版 ChaCha20 (固定グローバル構造体 @0x7000) | **未マージ・未採用**。アセンブラ版は将来の最適化候補 |
| `ssh-ecdsa-p256` | 改良版 ChaCha20 (C・reentrant) + **Poly1305 (実装済み)** + sha256/curve25519/p256/sshd | **移植元**。ssh 用部品は移植しない |

### 2.2 移植するファイル (ssh-ecdsa-p256 から)

| ファイル | 行数 | 内容 |
|---|---|---|
| `crypto/chacha20.c` | 87 | C 版 ChaCha20、`chacha20_init/encrypt/set_counter`、`__reentrant __banked`、`#pragma codeseg BANK3` |
| `crypto/chacha.h` | — | `struct chacha20_t` 定義 |
| `crypto/poly1305.c` | 207 | 26bit リム表現の標準 Poly1305、`poly1305_init/update/finish` |
| `crypto/poly1305.h` | — | `struct poly1305_t` 定義 |

- p256.c / sha256.c / curve25519.c / sshd/ は**移植しない** (PSK 方式には不要)

### 2.3 main ブランチの前提確認済み事項

- BANK3 は既にリンク済み (`Makefile` の `-Wl-bBANK3=0x34000`)、`cmd_commit.c` が
  `__reentrant __banked` の実績あり → crypto も同じパターンで載せられる
- `gen_random_bytes()` が httpd/httpd.c:291 に存在 (nonce 生成に使用可)
- `passwd` は `__xdata char passwd[21]`、コンフィグ (`CONFIG_START` 領域) から読み込み、
  `passwd` コマンド (cmd_parser.c:1327) で変更可能 → PSK も同じフローで管理する
- `--stack-auto` は使用しない (SDCC シミュレーテッドスタックで reentrant 動作)

## 3. 実装内容

### 3.1 Makefile 統合

- `mkdir -p $(BUILDDIR)/crypto`
- `SRCS += crypto/chacha20.c crypto/poly1305.c crypto/aead.c`
- `$(BUILDDIR)/crypto/%.rel: crypto/%.c` ルール追加 (ssh-ecdsa-p256 の Makefile から移植)

### 3.2 新規 `crypto/aead.c` — RFC8439 接着層 (BANK3)

公開 API:

```c
// 0 = OK, 1 = タグ不一致 (復号時)
uint8_t aead_encrypt(__xdata uint8_t *key,    // 32B PSK
                     __xdata uint8_t *nonce,  // 12B
                     __xdata uint8_t *aad, uint16_t aad_len,
                     __xdata uint8_t *plaintext, uint16_t len,
                     __xdata uint8_t *ciphertext,
                     __xdata uint8_t *tag);   // 16B
uint8_t aead_decrypt(__xdata uint8_t *key,
                     __xdata uint8_t *nonce,
                     __xdata uint8_t *aad, uint16_t aad_len,
                     __xdata uint8_t *ciphertext, uint16_t len,
                     __xdata uint8_t *plaintext,
                     __xdata uint8_t *tag);   // 16B
```

(実装は `crypto/aead.c`。aad は `__xdata` ポインタ。`aead_test()` は RFC8439 §2.8.2 の
暗号化・復号・改ざん検出を検証するファームウェア用テスト関数も同梱)

構成手順 (RFC8439 §2.8):
1. **1回限り Poly1305 キー**: 64B のゼロ平文を `chacha20_set_counter(ctx, 0)` で暗号化し、
   キーストリーム先頭 32B を poly1305 キーとして使用 (内部ブロック関数は公開しない)
2. **暗号化**: `chacha20_set_counter(ctx, 1)` で本暗号化
3. **タグ**: `poly1305_init/update/finish` で
   `AAD | pad16 | ciphertext | pad16 | len(AAD) LE 8B | len(CT) LE 8B` を MAC → 16B タグ
4. **復号**: タグを再計算し全バイト比較 (失敗時は平文を出力しない)。成功時のみ復号

データ領域 (すべて `__xdata`):
- `chacha20_t` (約 140B) + `poly1305_t` (約 70B) + スクラッチ 64B×2
- メッセージバッファは httpd 側の既存 XDATA バッファを再利用する方針
- 実装時に .map で XDATA 空きを確認 (PR #207 が 0x7000 固定領域を使っていた経緯あり)

### 3.3 httpd 統合 — 暗号化 API エンドポイント POST `/enc`

httpd/httpd.c の POST ルーティング (`/cmd` 処理の隣、BANK1) に `handle_enc()` として追加。

リクエスト形式 (POST body、1 パケットに収まるサイズ):
```
12B nonce | AEAD ciphertext | 16B tag
```
平文は `/cmd` と同じコマンドテキスト (最大 `CMD_BUF_SIZE - 1` = 127B)。

処理フロー:
1. POST `/enc` を検出 (パスは `/cmd` と同じくスラッシュ付きで照合)
2. PSK 未設定 (`preshared_key[0] == 0`) なら 401
3. `aead_decrypt(psk, nonce, aad=NULL, ...)` で復号 + タグ検証
4. **タグ検証 = PSK 所持の証明 (共通鍵認証)**。失敗時は `401 Unauthorized`
5. 成功時: 平文コマンドを `execute_commands()` に渡す
6. 応答: `HTTP 200` + `Content-Type: application/octet-stream`、
   body = `12B nonce (gen_random_bytes) | ct | tag`、平文は `{"result":"ok"}`
   (コマンド実行エラー時は平文の 400)

既存エンドポイント (`/status.json`, `/cmd`, login 等) は**無変更で維持**:
- 緊急時 WebUI / 既存ユーザーに影響なし
- 暗号化 API は追加レイヤとして共存

※ POST パス解析は実動作検証の結果、`request_path = p + 5` (スラッシュ付き) が正しい規約と判明し、
`/cmd` `/login` `/upload` `/config` の照合をすべてスラッシュ付きに統一した (8.3 参照)。

### 3.4 PSK 管理

- `cmd_parser.c` に `preshared_key` コンフィグコマンドを追加 (パターンは `passwd` に準拠)
  - 32B キーを**64 桁の十六進文字列**で設定 (大文字小文字どちらも可)
  - 引数なしで実行するとキーをクリア (未設定に戻す)
  - `execute_config()` 起動時に `memset` でゼロクリア → config.txt 内の
    `preshared_key <hex64>` 行で上書き (passwd と同じフロー)
  - `preshared_key` 未設定 (`preshared_key[0] == 0`) の場合は `/enc` は常に 401
- `__xdata uint8_t preshared_key[AEAD_KEY_LEN]` を httpd.c に保持 (cmd_parser.c から extern)

## 4. テスト計画

1. **RFC テストベクタ** (`aead_test()` として実装):
   - ChaCha20 単体: RFC7539 §2.4.2
   - Poly1305 単体: RFC7539 §2.5.2
   - AEAD 全体: RFC8439 §2.8.2 (encrypt/decrypt 両方向)
2. **回帰テスト**: 既存 WebUI ログイン / JSON API / シミュレータ動作が無傷であること
3. **コードサイズ**: crypto 一式 (約 400 行) が BANK3 (0x34000〜) に収まることを .ihx/.map で確認
4. **実機確認**: KP-9000-6XHML_X2 (シミュレータ) で `/enc` エンドポイントの疎通確認

## 5. リスク・注意点

- **PR #207 との競合**: 未マージ。マージ時は crypto ディレクトリが衝突するため、
  当 PR には「実装は本ブランチの C 版に置換される」旨をコメントで連絡する
- **nonce 管理**: 同一キーでの nonce 再利用は暗号が破綻。クライアント側は毎リクエスト
  ランダム nonce を生成する仕様とする (リプレイ防止カウンタはフェーズ2で検討)
- **速度**: C 版 ChaCha20 は 64B/ブロック。JSON ペイロード (数百B) なら実用範囲。
  アセンブラ最適化 (PR #207) は将来の選択肢
- **PSK の保管**: flash 内の PSK は平文保管 (現行 passwd と同等)。セキュリティ強化は将来課題

## 6. 実装手順

1. crypto/ ディレクトリ作成 + chacha20/poly1305 を ssh-ecdsa-p256 から移植 (`git show` 経由)
2. Makefile 統合 → コンパイル確認 (build-dev スキル)
3. `crypto/aead.c` 実装 + RFC テストベクタ検証
4. httpd に POST `/enc` 追加 (タグ検証・暗号化応答)
5. `preshared_key` コンフィグコマンド実装
6. 回帰テスト + コードサイズ/メモリ確認
7. コミット

## 7. 参照

- [issue #250](https://github.com/logicog/RTLPlayground/issues/250) — WebUI クライアント・サーバー化の議論
- [PR #207](https://github.com/logicog/RTLPlayground/pull/207) — ChaCha20 実装 (アセンブラ版)
- ブランチ `ssh-ecdsa-p256` — 移植元 (chacha20 C 版 + poly1305)
- RFC7539 (ChaCha20 / Poly1305)、RFC8439 (AEAD 構成)

## 8. 進捗状況 (2026-08-02)

### 8.1 完了

| # | 項目 | 状態 |
|---|---|---|
| 1 | ブランチ `feat/chacha20-poly1305-aead` をローカル main から作成、最新 origin/main (v0.2.22) をマージ | 完了 |
| 2 | `crypto/chacha20.c/h` + `crypto/poly1305.c/h` を ssh-ecdsa-p256 から移植 | 完了 |
| 3 | `crypto/aead.c/h` (RFC8439 AEAD 接着層) 実装 | 完了 |
| 4 | Makefile 統合 (BANK3) + SDCC ビルド成功 (BANK3 28KB/64KB) | 完了 |
| 5 | ホスト検証 (gcc + shim): RFC7539 §2.4.2 / §2.5.2 / RFC8439 §2.8.2 / ラウンドトリップ / 改ざん検出 / 2000 ケース差動テスト | **全 PASS** |
| 6 | httpd に POST `/enc` 追加 (`handle_enc()`、タグ検証 = PSK 認証、暗号化応答) | 実装済み (実機検証中) |
| 7 | `preshared_key` コンフィグコマンド (64 hex chars / 引数なしで解除) | 実装済み (実機確認済み) |
| 8 | POST パス解析のリグレッション修正 (`request_path = p + 6` → `p + 5`、全照合を `/cmd` 形式に統一) | 完了 (実機確認済み) |
| 9 | /upload 認証必須化 + /cmd・/enc の MODE_CONFIG 実行修正 | 完了 (実機確認済み) |
| 10 | rtlpctl に enc-cmd (/enc クライアント) 追加 + RFC ベクタ・相互運用テスト | 完了 |
| 11 | 実機 /enc 疎通 (正しい PSK) | **要調査 (タグ不一致)** |

### 8.2 移植コードから発見・修正したバグ (ssh-ecdsa-p256 由来、未検証コードのため)

`poly1305.c` はオリジナルが RFC ベクタを満たさず、**全面書き直し**:

1. `struct poly1305_t` の `r[4]` が 4 要素なのに `r[4]` にアクセス (donna 参照実装は `r[5]`)
2. 32bit 演算での 52bit 積の切捨て (`v >> 26` は上位 20bit 消失) → **13bit 分割で正確な
   32bit 専用乗算に置換** (`__mullonglong` は mcs51 ライブラリに無いため 64bit 不使用)
3. final ブロックの `hibit` (2^128 bit) 処理欠落 → `poly1305_blocks()` に `final` 引数追加
4. `poly1305_update()` の `want` が `uint8_t` のため **256〜271 バイト入力で無限ループ** → `uint16_t` に修正
5. finish の pad 加算を 32bit carry チェーンで正しく実装 (donna の select 処理も再実装)

`chacha20.c` は API を RAM キー対応に変更 (`__code` → `__xdata`)。RFC7539 §2.4.2 はそのまま PASS。

### 8.3 POST まわりの実挙動検証と発見事項

- 当初から **POST /cmd はファームウェアで壊れていた** (b548eeb で `is_word(request_path, "/cmd")` に
  変更されたが、`request_path = p + 6` は `"POST /"` の後を指すためスラッシュ無しの `"cmd..."` が入り、
  login/upload/config (スラッシュ無し照合) とは規約が不一致)
- `tools/httpd_sim` (シミュレータ) は `&buffer[5]` でスラッシュ付き照合しており、**実動作確認の結果
  シミュレータが正しい規約** (POST /login → 302、POST /cmd → 401/200 を確認)
- 修正: `request_path = p + 5` に変更し、`/cmd` `/login` `/upload` `/config` の全照合を
  スラッシュ付きに統一 → シミュレータの規約と一致

### 8.4 現在のブロッカー: OSEG リンクエラー (解決済み)

```
?ASlink-Error-Could not get 11 consecutive bytes in internal RAM for area OSEG.
```

- 内部 RAM (DSEG) は 128B 中 126B 使用済みでほぼ満杯。OSEG (オーバーレイ領域) が
  4B → 11B に増加したため配置不能
- **原因**: `/enc` 分岐の `handle_enc(p + 4, uip_len - ((p + 4) - uip_appdata))` で
  重複式 `p + 4` により handle_post (non-reentrant) に sloc 2 つ (DSEG 4B) が生成され、
  DSEG 合計が 128B を超過
- **修正**: 呼び出しを `handle_enc(p + 4)` に簡略化し、body_len 計算を __reentrant な
  handle_enc 内部 (再入スタック) に移動 → DSEG 4B 削減
- 結果: DSEG 126/128、OSEG 11B、SSEG 129B、XDATA 13.5KB/64KB、BANK1 42KB/64KB — 全て収まる

### 8.5 エンドツーエンド検証 (httpd_sim + 実 crypto コード)

- `tools/httpd_sim` に `/enc` ハンドラを追加し、**実際の crypto コード** (aead.c /
  chacha20.c / poly1305.c) をホストビルドで組み込み (`tools/rtl837x_common.h` shim、
  gcc の `-include` + `-Wno-unknown-pragmas`)
- テストクライアント (同じ crypto コードで暗号化/復号) による結果:

| テスト | 内容 | 結果 |
|---|---|---|
| T1 | 正しい PSK でコマンド暗号化 POST → 200 + 暗号化 `{"result":"ok"}` 応答 | **PASS** |
| T2 | 誤ったキー → 401 | **PASS** |
| T3 | タグ改ざん → 401 | **PASS** |
| T4 | 本文短すぎ → 400 | **PASS** |
| 回帰 | login 302 / cmd 200 / status.json 200 / information.json 200 | **PASS** |

- シミュレータ側も body を Content-Length 分読み切るループを追加 (POST 本文が
  別セグメントで届くケースに対応)

### 8.6 WebUI の設計意図と /cmd 実行モード修正

**設計意図**: WebUI の操作 (POST /cmd) による設定変更は RAM 上のみの**一時設定**であり、
操作中に失敗した場合は**再起動すれば保存済みコンフィグに戻せる**ことを意図している。
flash への永続化は `/config` アップロード (認証必須・マルチパート) のみが行う。

この意図に沿う形で、実機テスト中に発見・修正した事項:

1. **/cmd・/enc は MODE_CONFIG でコマンド実行するよう修正**
   - 従来: `cli_mode = MODE_EXEC` のまま `execute_commands()` を実行しており、
     WebUI が送る設定コマンド (`port`/`vlan`/`bw`/`pvid` 等、全て CONFIG モード限定) が
     「Command not available in this mode」で**黙って拒否**され、設定が一切反映されない
     状態だった (HTTP は 200 を返すため UI 上は成功に見える)
   - 修正: `/cmd`・`handle_enc()` 内で `cli_mode = MODE_CONFIG` に切り替えてから
     `execute_commands()` を実行し、終了後に `MODE_EXEC` へ戻す
   - RAM のみの一時設定であり永続化はしない → 上記の設計意図と整合
2. **ファームウェアアップロードの認証必須化**
   - `/upload` のセットアップ処理が認証チェックより先に走る構造だったため、
     ハンドラ冒頭に `authenticated` チェックを追加 (実際のデータ書込みは従来から
     認証必須のマルチパートパスにあるため、防御の明示化)
3. **実機で POST /cmd が 404 になることを確認** (旧ファームウェア) — §8.3 の
   リグレッションが実機でも再現することを実証

### 8.7 実機テスト状況 (PCB_K0402WS_V3 @ 192.168.10.247)

| 項目 | 結果 |
|---|---|
| 旧ファームでの POST /cmd / /enc | 404 (リグレッション + 未実装を実証) |
| 新ファームフラッシュ (WebUI /upload、認証 Cookie 付き) | 成功 (HTTP 200 → リセット) |
| 新ファームでの POST /enc (PSK 未設定) | 401 (正常) |
| 新ファームでの POST /cmd (ログイン後) | **200 (リグレッション修正を実機確認)** |
| `preshared_key` 設定 via /cmd | 成功 (MODE_CONFIG 修正後。hostname 変更も /cmd 経由で実機確認) |
| /enc 疎通 (正しい PSK) | **成功!** `rtlpctl enc-cmd` → `{"result":"ok"}`、コマンド実行も確認 (hostname 変更反映) |

実機テスト中に発見・対処した事項:

1. **テストクライアントの TCP 送信が 2 セグメントに分割される問題**
   - ヘッダと本文を別 `write()` で送ると TCP 上で分割され、1 パケット前提の
     ファームウェアが本文なし (body_len=0) で 400 を返す → クライアントを
     1 回の `write()` に変更
2. **実機は HTTP 応答後にコネクションを閉じない** (keep-alive)
   → クライアントの `read()` が EOF 待ちでブロック → ソケットに受信タイムアウトを設定
3. **`port 1 off` を /enc 経由で実行すると管理ポートが落ちて接続不能になる**
   → テストコマンドをリンクに影響しない `hostname` に変更
4. **`preshared_key[0]` の未設定チェックが、先頭バイトが 0x00 のキーを「未設定」と
   誤判定** → 全 32 バイトの OR 判定に修正
5. **暗号経路で応答なし (ハング) → 再入スタック (SSEG) 深さが疑われた**
   - 対策: `poly1305_mul()` のホットローカル (uint32 多数) を static __xdata 化
     (単一スレッドのため安全) → ハングは解消し 401 応答が返るようになった
   - 残課題: デバイス側のタグ計算がホスト (gcc) と一致しない (SDCC 特有の
     演算差の可能性、調査中)
6. **タグ不一致の原因 = SDCC のレジスタ liveness バグ** (サブエージェント調査)
   - `poly1305_mul()` の内側ループカウンタ `j` (static __xdata) がレジスタ (r3) に
     キャッシュされ、`phi` 計算のコード生成が r3 を破壊 → `lo[i+j]` の蓄積インデックス
     が壊れる。`i/j` を `volatile` 化することで解決
   - 別 AI の「SDCC の 32bit シフトコード生成バグ」説は**実測で反証**:
     `>> 13` / `>> 26` / `>> 20` / `>> 25` は ucsim で全て正しい値を返し、
     chacha20 (ROTL32 で同種シフトを使用) も SDCC ビルドで正しく動作
   - 修正後: ucsim で RFC7539 §2.5.2 / RFC8439 §2.8.2 がバイト完全一致、
     ホスト (gcc) テストも全 PASS
7. **再入スタック (SSEG) 深さの問題** — 実機の再入スタックは IDATA 上限 129B だが、
   crypto チェーンは SDCC 計算で ~243B 必要 → 実機でスタックオーバーフロー
   - **対処**: `poly1305_finish()` の uint32 ローカル (h0-h4/g0-g4/h0p-h3p/mask 等) を
     static __xdata 化 → フレーム 93B → **44B** に削減
   - ucsim で全ベクタ PASS + 実機フローチェーン (aead_encrypt → インプレース復号 →
      aead_encrypt) が完了することを確認 (マーカー検証)
   - **最終解決**: `handle_enc()` のローカルを static 化、aead パスで chacha20_init を
     インライン化、`aead_poly_key`/`aead_tag`/`aead_decrypt` のローカルを static 化
     → ピークスタック実測 122B → 112B
8. **クラッシュの根本原因 = __banked 内部呼び出しのスタックオーバーヘッド**
   - 症状: 単発の /enc は成功するが、連続リクエスト (テストスイート) で実機がクラッシュ
   - 原因: `chacha20_*` / `poly1305_*` に `__banked` が付いており、**同バンク (BANK3)
     内の呼び出しでも** `__sdcc_banked_call` が PSBANK + r0-r2 をハードウェアスタックに
     退避 (**+4B/呼び出し**)。最深チェーンで ~26B の追加 → 112B + 26B = 138B > 129B
   - 修正: 内部関数 (chacha20_*/poly1305_*) は aead.c (BANK3) からのみ呼ばれるため
     `__banked` を除去 → 直接呼び出しに (banked 参照 26 → **2**、公開 API の
     aead_encrypt/decrypt のみ維持)
   - 結果: **実機で enc_client スイート (T1-T4) 全 PASS + rtlpctl enc-cmd 成功 +
     hostname 反映 + デバイス安定動作を確認**
   - 教訓: 8051 では同バンク呼び出しに `__banked` を付けるとスタックを無駄に消費する
   - 代替案 (必要になった場合): `--xstack` (XDATA スタック、pdata 256B 制限あり)、
     16bit poly1305 化 (chacha20 は本質的に 32bit のため部分効果)

テストクライアント:
- `/tmp/opencode/aeadtest/enc_client <IP> <port>` (実 crypto コードをリンク)
- **`tools/rtlpctl` に `/enc` サポートを追加** (推奨): `enc-cmd <text>` コマンド、
  PSK は `RTLP_PSK` 環境変数または `--psk <hex64>` で指定

### 8.7.5 メモ: POST 系バグ修正の main 取り込み方針

**もし PSK (共通鍵認証) の実装が完成しなかったとしても、本ブランチで修正した POST 系の
バグは価値が独立している。** これに関しては main に取り込めるように、修正ブランチを
作成して修正する。

対象の修正 (PSK 非依存・単独で main に取り込めるもの):

1. **POST /cmd のリグレッション修正** — `request_path = p + 6` → `p + 5` に変更し、
   `/cmd` `/login` `/upload` `/config` のパス照合をスラッシュ付きに統一
   (b548eeb で壊れた /cmd が実機で 404 になることを確認・修正)
2. **/cmd・/enc の MODE_CONFIG 実行修正** — `cli_mode = MODE_EXEC` のままでは WebUI の
   設定コマンド (port/vlan/bw 等) が黙って拒否されていた → CONFIG モードで実行するよう修正
3. **/upload の認証必須化** — ファームウェアアップロードのセットアップ処理が認証チェック
   より先に走る構造の防御明示化

上記は RAM 上の一時設定のみ (失敗時は再起動で復旧) という WebUI の設計意図と整合し、
既存機能の回帰リスクも低い。PSK 部分 (crypto/、/enc、rtlpctl enc-cmd) は別途継続。

### 8.8 rtlpctl への /enc サポート追加

- `tools/rtlpctl/aead.go`: **Go 標準ライブラリのみ**で ChaCha20-Poly1305 AEAD を実装
  (RFC 7539 §2.4.2 / §2.5.2、RFC 8439 §2.8.2 のベクタで検証済み)
- `tools/rtlpctl/aead_test.go`: RFC ベクタ + ラウンドトリップ + 改ざん検出テスト
- `enc-cmd <text>` コマンド (main.go / commands.go): コマンドを PSK で暗号化して
  POST /enc、応答を復号して表示。401 はエラーとして報告
- 相互運用性テスト: Go 実装のパケットを C 実装 (aead.c) で復号できることを確認
  - **バグ発見**: Go 側のタグ計算が `len(aad)` の 8 バイトを書き忘れており、
    C 実装と非互換だった → 修正後、双方向で相互運用を確認
- シミュレータ E2E: `enc-cmd` (正しい PSK) → `{"result":"ok"}`、
  誤った PSK → 401 を確認

### 8.9 残作業

1. ~~OSEG エラー解決~~ → 完了
2. ~~httpd_sim でエンドツーエンド検証~~ → 完了 (全 PASS)
3. ~~rtlpctl enc-cmd 追加 + シミュレータ E2E~~ → 完了 (相互運用確認済み)
4. ~~タグ不一致の原因究明~~ → 完了 (poly1305 の SDCC レジスタ liveness バグ)
5. ~~再入スタック深さの問題~~ → 完了 (フレーム削減 + __banked 内部呼び出し除去)
6. ~~実機での /enc 疎通確認~~ → **完了** (T1-T4 全 PASS、コマンド実行・hostname 反映確認)
7. 残る検証: シミュレータ回帰 + ホスト全テスト + 他機種 (KP_9000_6XHML_X2 等) ビルド確認
8. **POST 系バグ修正の main 取り込み** (§8.7.5 参照) — 修正ブランチ作成・マージ
9. コミット (ユーザー指示があるまで実施しない)

### 8.10 PSK (共通鍵) の使い方

#### 設定 (WebUI 経由)

ログインして `/cmd` に `preshared_key <64桁の16進>` を POST する:

```sh
# ログイン (Cookie 保存)
curl -c cookies.txt -d "pwd=1234" http://<host>/login

# PSK 設定 (例: 00..1f の 32 バイト)
curl -b cookies.txt -d "preshared_key 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" http://<host>/cmd

# PSK クリア (引数なし)
curl -b cookies.txt -d "preshared_key" http://<host>/cmd
```

- PSK は **RAM 上の一時設定** (WebUI の `/cmd` と同じ扱い)。再起動で消える。
- 設定確認: `/enc` に短い本文 (28 バイト未満) を POST → **400** なら PSK 設定済み、**401** なら未設定:

```sh
printf 'garbage' | curl --data-binary @- -o /dev/null -w "%{http_code}\n" http://<host>/enc
```

#### 暗号化コマンド実行 (CLI)

`rtlpctl` に `/enc` サポート (`enc-cmd`):

```sh
RTLP_HOST=<host> RTLP_PSK=<64hex> rtlpctl enc-cmd "hostname MY-SWITCH"
# または
rtlpctl --host <host> --psk <64hex> enc-cmd "hostname MY-SWITCH"
```

- `/enc` のパケット形式: `nonce[12] || 暗号文 || tag[16]`、AAD なし (RFC8439 AEAD)
- 応答も同じ形式で暗号化される
- 正しい PSK → 200 + `{"result":"ok"}`、誤った PSK/改ざん → 401

#### テストスイート (enc_client)

```sh
/tmp/opencode/aeadtest/enc_client <host> <port>
```

- T1: 正しい PSK → 200 + ok
- T2: 誤った鍵 → 401
- T3: タグ改ざん → 401
- T4: 短い本文 → 400

#### 実機での検証結果 (PCB_K0402WS_V3)

- `/login` → `/cmd preshared_key ...` → `/upload` フラッシュのサイクルで毎回動作確認
- T1-T4 全 PASS、`rtlpctl enc-cmd` で hostname 変更が反映されることを確認
- 注: PSK 未設定時に `/enc` へフックした診断コード (aead_test 実行) を入れると、
  その後の初回 `/enc` が 401 になる (static 変数の後遺症)。診断コードは本番から除去済み。

#### 8.10.1 PSK 方式でアクセスできる範囲 (2026-08-02 実機確認)

- **PSK 方式 (`POST /enc`) は CLI コマンド実行専用**。レスポンスは
  暗号化 JSON (`{"result":"ok"}`) のみで、HTML ページは返さない。
- **セッション (ログイン) は不要** — PSK を知っていれば config モードの
  全 CLI コマンド (hostname 変更, config save, reboot, status, vlan 等) を
  実行できる = 実質的に管理者権限相当。
- **認証が必要な WebUI ページ (network.html 等)** は PSK 方式では
  HTML を取得できない。ページパスを /enc に送ると「不明コマンド」として
  無視され 200 + ok が返る (ページ内容は返らない)。
  ページ閲覧は従来通りセッション (pwd=1234 で login) が必要。
- **telnet** は PSK 方式に対応していない — 平文パスワード認証
  (Password: 1234) のみ。暗号化は不可。さらに exec モード限定で
  hostname 等の設定変更は "Command not available in this mode"。
- **まとめ**: PSK 方式は「操作 (設定変更・情報取得)」をセキュアに行う
  手段。WebUI ページの閲覧や telnet は対象外 (平文 or セッション認証)。
