# 計画: PSK / API キーの自動生成

作成: 2026-08-02
ステータス: 計画 (実装未着手)

## 背景と目的

現在の PSK は 64 hex 文字 (32 バイト) をユーザーが手入力する。
長いランダムキーの手入力はコピーミス・タイプミスが起きやすく、
また「良いキーを自分で考えられない」という問題がある。

本計画では:
- WebUI から**ワンクリックで安全な長いキーを自動生成**できるようにする
- 生成したキーをそのまま PSK として設定 (commit で永続化) できるようにする
- rtlpctl 等の API アクセス用キー (API キー) としても利用できるようにする

## 現状の整理

| 項目 | 現状 |
|---|---|
| PSK の設定方法 | System > Advanced に手入力 (64 hex) → `preshared_key` + `commit` |
| ログイン | login.html で PSK 入力、またはパスワード (PSK 未設定時) |
| 永続化 | `commit` でフラッシュ保存 (実装済み) |
| rtlpctl | `RTLP_PSK=<64hex>` で利用 |
| 乱数源 | ブラウザ: `crypto.getRandomValues()` (secure context 不要) |
| | 実機: `gen_random_bytes()` (既存, SFP/セッションで使用) |

## 計画 1: WebUI での PSK 自動生成 (推奨)

### 1.1 ブラウザ側生成 (第一候補)

**UI (System > Advanced タブ):**
```
Pre-Shared Key (encrypted access):
[入力欄: 64 hex]  [Generate]  [Set PSK]
```

**動作:**
1. 「Generate」クリック → `crypto.getRandomValues(new Uint8Array(32))` で
   32 バイト生成 → hex 文字列を入力欄に表示
2. 「Set PSK」クリック → 既存の `setPSK()` で `preshared_key` + `commit`
3. 成功後 localStorage に保存 → 暗号化モードに切替 (既存動作)

**実装:**
- `html/main.js` に `generatePSK()` を追加:
  ```js
  function generatePSK() {
    var b = new Uint8Array(32);
    if (window.crypto && crypto.getRandomValues) crypto.getRandomValues(b);
    else for (var i = 0; i < 32; i++) b[i] = Math.floor(Math.random() * 256);
    var h = RTLAEAD.toHex(b);
    var inp = document.getElementById('psk-input');
    if (inp) inp.value = h;
    notify('PSK generated. Review and click Set PSK.', 'info');
  }
  ```
- `html/index.html` の Advanced タブに「Generate」ボタン追加
- フォールバック: `crypto.getRandomValues` が無い環境は
  `Math.random` ベース (品質は低いが動作はする)

### 1.2 (任意) 実機側生成

**/enc に `genkey` コマンドを追加:**
- リクエスト: `genkey` (暗号化)
- レスポンス: 32 バイトの乱数 (hex 64 文字) を暗号化して返す
- 実機の `gen_random_bytes()` を使用 (SFP EEPROM 乱数と同一ソース)

**利点:** 実機の乱数品質、ブラウザ環境に依存しない
**欠点:** 実装量が増える (handle_enc に分岐追加、レスポンス形式の定義)
**推奨:** まず 1.1 で実装し、必要になれば追加

## 計画 2: API キーの自動生成

### 2.1 API キーの位置づけ

「API キー」は PSK と同一のキーを指す (rtlpctl の `RTLP_PSK` が
API アクセスキーとして機能している)。別キー方式も選択肢として検討する。

### 2.2 案 A: PSK と同一キーを API キーとして利用 (推奨)

- 生成した PSK をそのまま `RTLP_PSK` に設定
- 「Set PSK」成功時に**キーを表示・コピー**できる UI を追加:
  - 成功通知に「キーをコピー」ボタン
  - または成功時にキーを表示するモーダル
- ユーザーはそれを rtlpctl (`export RTLP_PSK=...`) やスクリプトに設定

### 2.3 案 B: PSK と独立した API キー (将来検討)

- PSK (WebUI ログイン用) と API キー (CLI/スクリプト用) を分離
- 実機に `apikey` コマンドを追加: 設定・表示・無効化
- WebUI の API モード (/enc api) は PSK 認証、CLI は API キー認証
- **注意:** 認証経路が複数になり攻撃面が増える。実装は慎重に
- 現時点では非推奨 (PSK が両方の役割を果たせるため)

## 計画 3: キー生成・設定の UX

### 3.1 生成フロー (まとめ)

1. System > Advanced → 「Generate」→ ランダム 64 hex が入力欄に表示
2. 「Set PSK」→ `preshared_key` + `commit` → localStorage 保存 → 暗号化切替
3. 成功通知に「キーをコピー」ボタン → クリップボードにコピー
4. ユーザーは rtlpctl 等に設定 (`RTLP_PSK`)

### 3.2 表示・セキュリティ上の注意

- キーは**設定時にのみ表示** (commit 後は表示しない)
- ページの閲覧者 (肩越し等) に注意する旨の警告文
- キー変更後は既存クライアント (ブラウザ localStorage / rtlpctl) が
  新しいキーを必要とする旨の通知

## 実装ステップ (承認後)

1. `html/main.js`: `generatePSK()` 追加 + コピー機能
2. `html/index.html`: Advanced タブに「Generate」ボタン追加
3. (任意) `/enc` に `genkey` コマンド (実機側生成)
4. Playwright テスト:
   - Generate → 64 hex が入力欄に入る (正規表現チェック)
   - Set PSK → 暗号化モードに切替
   - rtlpctl (新キー) で API アクセス可能
   - 再起動後もキーが維持される (commit)
5. ドキュメント更新 (doc/pskauth.md)

## 検証方法

| テスト | 期待結果 |
|---|---|
| Generate クリック | 入力欄に 64 hex が入る |
| 生成キーで Set PSK | 設定成功、暗号化モードに切替 |
| rtlpctl enc-cmd (生成キー) | `{"result":"ok"}` |
| 再起動後 | キー維持 (commit 永続化) |
| パスワードログイン | 従来通り動作 (リカバリ経路) |
