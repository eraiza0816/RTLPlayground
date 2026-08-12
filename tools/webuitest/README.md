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
| 全ページ遷移 | dash / stat / vlan / eee / port / bw / qos / storm / acl / mirror / lag / l2 / sys / sfp / update で例外なし |
| パネル描画 | 各パネルの要素数・制御 (stat 行数、eee/bw/port テーブル、qos の PCP/DSCP マップ、storm の 4 行、ACL 追加フォーム、sys タブ、SFP コントロール) |
| コンソールタブ | ARP テーブル、running-config 表示、ping 結果 (実行時) |
| 適用フロー | シミュレータのみ: QoS/storm/ACL/IGMP/mirror/bw/lag/eee/vlan の適用操作で `/cmd` が期待通りに送信され 200 が返ること |
| エラー検出 | console error / pageerror / requestfailed が 0 であること |

## 動作モード

- ホストが既定のシミュレータ (`http://127.0.0.1:18080`) の場合、
  適用フロー (設定変更コマンドの送信) を含む全チェックを実行する。
- それ以外 (`http://192.168.10.247:80` など) に対しては読み取り専用
  (ログインと描画・遷移チェックのみ。`/cmd` は送信しない)。
- 環境変数 `WUI_INTERACTIVE=1` / `WUI_INTERACTIVE=0` で明示的に上書きできる。
  localhost をプロキシとして実機に転送する構成で誤って設定変更コマンドを
  送信しないよう、`localhost` マッチによる暗黙の書き込みモードは廃止した。

## シミュレータの API 対応

`httpd_sim` は静的ファイルに加えてファームウェアと同じ JSON API を
模擬する: `/status.json`, `/eee.json`, `/bandwidth.json`, `/mirror.json`,
`/lag.json`, `/vlanlist`, `/config`, `/running-config`, `/ping.json`,
`/arp.json`, `/lldp.json`, `/igmp.json`, `/storm-control.json`,
`/qos.json`, `/acl.json`, `/cmd`, `/login`, `/upload` 等。

## 注意

- 実機に対して実行する場合、テストはログインと読み取り専用のページ遷移のみを行う
  (設定変更コマンドは実行しない)。
- 実機はポート 80 で WebUI を配信する。`node test.js http://192.168.10.247:80 <password>`
  のように指定する。

## 実機用テスト (監査 D 群の回帰テスト)

シミュレータはファームウェアの JSON エスケープ・PVID マスク・大ファイル転送を
再現しないため、実機向けのスモークテストを別に用意している:

```sh
# 実機 HTTP API テスト: D1 (JSON エスケープ) / D5 (PVID マスク) / D8 (大ファイル転送)
python3 device_api_test.py --host 192.168.10.247 --password 1234

# オプション: GET /reset の 200 応答も確認 (スイッチが再起動する)
python3 device_api_test.py --host 192.168.10.247 --reset-test

# 実機 WebUI テスト (playwright): D4 / D5 / D9 / D10 と B5 (ログアウト)
node device_ui_check.js http://192.168.10.247 1234
```

`device_api_test.py` はポート名の引用符エスケープ (`/status.json` が壊れないこと)、
`/vlanlist` の PVID マスクが `pvid` コマンドの変更を追跡すること、`/main.js`
(gzip) がほぼ完全に届いて展開後に実行可能なスクリプトになることを検証する。
テストで変更したポート名・PVID は最後に元へ戻す (`--port` でテスト対象ポートを
指定可能。既定 5 = 管理ポートを避ける)。

監査 C 群 (機能修正) の回帰テストも含む:

- C1: `vlan` / `pvid` 設定後の `/running-config` が vlan/pvid/mtu 行と
  ベース行 (ip/gw/netmask/telnet/web) を含むこと
- C1b: telnet 経由の `commit` が flash の config に vlan/pvid 行を書くこと
- C2/C7: `storm-control on multicast 1000p` が running-config に `p` サフィックスで
  シリアライズされ、`/storm-control.json` の `pps` フィールドが 1 になること
- C5: `qos dscp 46 3` が 2 桁の dscp を正しくマップし、範囲外のキューは
  マップを変更しないこと
- C6: `lag 4 1` (グループ範囲外) が `/lag.json` を変更しないこと
- C12: 奇数桁・20bit 超の `bw` レートが無視され、有効な偶数桁レートは
  受け入れられること

注意: `stp on` は全ユーザーポートを blocking にする (upstream の STP 実装に
ポート状態遷移がない) ため、管理ポートも切り離され、電源リサイクルが必要に
なる。このため C3 (STP) は HTTP テストで実行しない。

## 既知のデバイス挙動 (テストで許容)

実機の httpd は `/` を認証なしで `index.html` として配信するため
(`html_data.c` の `{"/", ... index.html ...}` エントリ)、main.js の init が
ログイン前に一度だけ `/status.json` を fetch し、セッション未設定のため
401 を受けて `/login.html` へリダイレクトする。これは既存仕様であり、
テストはログイン前の 401 コンソールエラーを許容する。
(ログイン後の 401 は失敗として検出される)
