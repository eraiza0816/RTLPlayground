# WebUI TypeScript Conversion Feasibility

## Overview

`html/main.js` (~57KB) と `html/i18n.js` (~14KB) は型情報を持たないため挙動が不安定になることがある。これを解決するため、TypeScript（TS）で書き直し、TS を中間コードとしてビルド時に JS を生成する構成を検討する。

**結論: 可能。** コンパイルはビルドホスト（PC）で完結し、ターゲットの 8051 MCU には負担をかけない。ただし、Flash 容量・配信方式・HTML インラインハンドラに関する制約を考慮する必要がある。

---

## 現状のアーキテクチャ

### WebUI ファイル（`html/`）

| File | Size | Role |
|------|------|------|
| `main.js` | 56.8KB | 全ページのロジック（単一ファイル・Vanilla JS） |
| `i18n.js` | 13.7KB | 多言語辞書 + `t()` |
| `index.html` | 26.9KB | メインページ |
| `login.html` | 4.0KB | ログインページ |
| `favicon.ico` | 1.9KB | アイコン |

合計 約101KB

### ビルドフロー

```
html/*.js, *.html, *.svg
        │  tools/output/fileadder
        ▼
ファームウェアイメージ (512KB)
  - HTML 領域: 0x40000 から埋め込み
  - html_data.c/h: ファイル名→(アドレス, サイズ, MIME) の index を自動生成
```

- Makefile:62-67 で `fileadder` が `html/` 内の `*.js|*.html|*.svg` をそのままイメージに埋め込む
- httpd は `f_data[]` の index を引いて flash から読み出して配信
- `tools/httpd_sim.c`（Linux シミュレータ）はカレントディレクトリから `fopen` で配信（`html/` で起動する想定）

---

## ハードウェアリソース制約

### 1. Flash 容量

- イメージ全体: 512KB（`IMAGESIZE = 524288`）
- HTML 領域開始: `0x40000`（262144）
- 後方に config データ（`0x6F000`, `0x70000`）→ 重複不可

**実効上限: 0x40000〜0x6F000 = 約188KB**
- 現在使用: 約101KB
- 余裕: **約87KB**

### 2. 配信方式（非圧縮・逐次送信）

- ファイルは圧縮されず flash から配信される（`TCP_OUTBUF_SIZE` 2500B + MSS 1460B 単位で ACK ごとに逐次送信、httpd.c:749-763）
- **JS サイズ = 転送量 = TCP RTT 数に直結**
- ボトルネックは帯域ではなく RTT / 8051 CPU（`doc/webui-performance.md` 参照）
- **uIP は HTTP keep-alive 非対応** → `<script src>` 1つ = TCP 接続1回分のコスト

### 3. `#{func}` 置換機構

- fileadder.c:178-214 が全ファイルの `#{...}` をファームウェア関数呼び出し `{NNN}` に置換
- 現在 JS には未使用
- **`#{` は JS/TS 構文として不正** → TS パイプラインとは両立しない（将来使うなら別記法が必要）

### 4. ターゲット側リソース

- 埋め込みデータは flash から 2500B バッファ経由で読むだけ
- コンパイルはホスト側 → **8051 の RAM/CPU に影響なし**

---

## TypeScript 導入の可否

### 構成（推奨）

```
html/src/main.ts   html/src/i18n.ts   ← 型付きソース（真実のソース）
        │  tsc --noEmit (型チェック) + esbuild (emit, minify, target ES2017)
        ▼
html/dist/main.js  html/dist/i18n.js  ← fileadder / httpd_sim の参照先をこちらへ
```

- tsc 素の出力は手書き JS と同サイズ。esbuild の minify で 25〜50% 削減も可能 → 87KB の余裕は十分
- `target ES2017` 指定で async/await 用ヘルパーの混入を回避
- 移行は `checkJs` + `allowJs` で既存 JS を型チェックしつつ徐々に `.ts` 化するのが安全

### 必要な修正箇所

1. Makefile: `webui` ターゲット追加、`html_data.c` 生成の前提に組み込み
2. `HTML :=` と fileadder の参照先を `html/dist` へ
3. `httpd_sim.c:883` の `fopen` 先を `html/dist` へ
4. ビルドホストに node が必要（README の依存一覧に追加）

### 新規ビルド依存

- ビルド毎に JS を生成するなら node が必須化
- 生成 JS をコミットすれば現行 `make` のまま動作（設計判断）

---

## 責務ごとの TS 分割とビルド時結合

**可能。しかも keep-alive 非対応のため、むしろ結合が有利。**

### 方式A: モジュール分割 + esbuild バンドル（推奨）

```
html/src/api.ts   nav.ts   vlan.ts   eee.ts ...   main.ts(entry)
        └─ esbuild (minify, format:iife, target:ES2017) ─> html/dist/main.js
```

- import/export で責務単位に分割、型安全 + tree shaking

### 方式B: グローバルスコープ結合

- 現行 main.js は全てグローバル関数
- `.ts` に分割して import/export を使わず tsc `--outFile`（`module:none`）+ 結合
- モジュール境界は無いが、既存コードへの変更が最小

---

## 重要な制約（検証で判明）

### 1. HTML インラインハンドラがグローバル関数を参照

index.html:199-451 に **60個以上のインラインハンドラ**が main.js のグローバル関数を直接呼ぶ。

```html
<li onclick="nav('dash')">
<select id="mtu5" onchange="applyMTU(5)">
<button onclick="startFlash()">
```

- 方式A（IIFE）では関数がモジュール内に閉じ込められる
- **HTML から使う関数を `window.nav = nav` 等で明示的に公開**する仕組みが必要
- 方式B なら自動的にグローバルのまま

### 2. i18n.js は分離維持が安全

- login.html:99 は **i18n.js のみ**を読み込み（main.js なし）で `t()` を使用
- i18n.js を main.js に結合すると login ページに main.js の init 処理（main.js:1345）まで引き込まれる
- **i18n.ts は単独ファイルとして `i18n.js` を配信**するのが無難

→ 最終構成: **`html/dist/main.js`（多数の TS を結合）+ `html/dist/i18n.js`（単独）** の2ファイル
→ 配信面の負担は現状と変わらない

---

## 推奨実装手順

1. `html/src/` に現在の `main.js` を分割配置、`checkJs` で型チェックを開始
2. esbuild + tsc のビルドスクリプトを整備（`html/dist/` へ出力）
3. Makefile / httpd_sim の参照先を `html/dist` に変更
4. `window` への明示公開（方式Aの場合）を実装
5. 徐々に各ファイルへ型注釈を追加、`noImplicitAny` へ移行

---

## リスクまとめ

| Risk | Impact | Mitigation |
|------|--------|------------|
| 新ビルド依存（node） | ビルド環境の変化 | 生成 JS をコミット or 依存追加 |
| JS サイズ増 | ロード時間の増加 | esbuild minify + tree shaking |
| インラインハンドラとの非互換 | 全ページの操作不能 | `window` への明示公開 |
| `#{func}` 置換との非互換 | 将来のファームウェア連携 | 別記法の導入 |
