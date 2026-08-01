# webuitest — WebUI end-to-end test

`html/main.js` / `html/i18n.js` の変更をブラウザ実動で検証するための
Playwright テスト。`tools/httpd_sim` シミュレータ (または実機) に対して
ログイン → ダッシュボード → 全ページ遷移を行い、例外・コンソールエラー・
失敗リクエストがないことを確認する。

## 必要なもの

- Node.js (npm で playwright をインストール)
- Chromium (playwright が自動ダウンロードするか、`chromium-browser` を利用)
- `tools/output/httpd_sim` (本ツールの Makefile でビルドされる)

## 使い方

### 1. シミュレータを起動 (html/ ディレクトリから)

httpd_sim はカレントディレクトリのファイルを配信するため、`html/` から起動する:

```sh
cd html
../tools/output/httpd_sim 18080
```

### 2. テストを実行

```sh
cd tools/webuitest
npm install          # 初回のみ (playwright)
node test.js         # デフォルト: http://127.0.0.1:18080, パスワード 1234
```

URL とパスワードは引数または環境変数で変更できる:

```sh
node test.js http://192.168.10.247:80 1234     # 実機に対して直接テスト
WUI_URL=http://127.0.0.1:18080 WUI_PASSWORD=1234 node test.js
```

### 3. 終了コード

- `0` = PASS (全チェック成功)
- `1` = FAIL (ログイン失敗 / ポート順異常 / ページ例外 / コンソールエラー等)
- `2` = テストクラッシュ (シミュレータ未起動等)

## 検証内容

| 項目 | 内容 |
|------|------|
| ログイン遷移 | `/` → `login.html` へのリダイレクト |
| 認証 | `pwd` フォーム送信 → ダッシュボード表示 |
| ポート順 | `#port-grid` が物理順 (1..N) で描画されること |
| 全ページ遷移 | dash / stat / vlan / eee / port / bw / mirror / lag / l2 / sys / sfp / update で例外なし |
| エラー検出 | console error / pageerror / requestfailed が 0 であること |

## 注意

- 実機に対して実行する場合、テストはログインと読み取り専用のページ遷移のみを行う
  (設定変更コマンドは実行しない)。
- 実機はポート 80 で WebUI を配信する。`node test.js http://192.168.10.247:80 <password>`
  のように指定する。

## 既知のデバイス挙動 (テストで許容)

実機の httpd は `/` を認証なしで `index.html` として配信するため
(`html_data.c` の `{"/", ... index.html ...}` エントリ)、main.js の init が
ログイン前に一度だけ `/status.json` を fetch し、セッション未設定のため
401 を受けて `/login.html` へリダイレクトする。これは既存仕様であり、
テストはログイン前の 401 コンソールエラーを許容する。
(ログイン後の 401 は失敗として検出される)
