# WebUI 型安全性導入の方針 (TypeScript / JSDoc 検討)

Type: explanation · Topic: the WebUI TypeScript conversion

## 概要

`html/main.js` (~55KB) と `html/i18n.js` (~13KB) は型情報を持たず、保守性の不安がある。
本ドキュメントは TypeScript (TS) 移行の可否と、より低リスクな代替手段を整理し、
導入方針を決めるための資料である。

**結論: フル TS 移行は過剰。まず JSDoc + `// @ts-check` で型安全性を得る (段階0)。
それでも不足する場合のみ、グローバルスコープ結合の TS (方式B) を段階1 として採用する。
モジュール分割 + esbuild バンドル (方式A) は見送る。**

> **性能への影響はない。** `doc/webui-performance.md` が特定したボトルネック
> (直列リクエストキュー・レジスタ読み出し・TCP RTT) はフロントエンドの実行時間ではない。
> TS 化・minify は WebUI の体感速度を変えない。本提案の目的は**保守性・型安全性**であり、
> 性能改善ではないことに注意。

---

## 現状アーキテクチャ

### WebUI ファイル (`html/`)

| File | Size | Role |
|------|------|------|
| `main.js` | 55KB | 全ページのロジック (単一ファイル・Vanilla JS) |
| `i18n.js` | 13KB | 多言語辞書 + `t()` |
| `index.html` | 27KB | メインページ (`DOMContentLoaded` 後に i18n.js→main.js を直列ロード) |
| `login.html` | 4KB | ログインページ (i18n.js のみをロード) |
| `favicon.ico` | 2KB | アイコン (HTML は `data:,` で参照し実 fetch を抑制) |

合計 約100KB

### ビルドフロー

```
html/*.js, *.html, *.svg
        │  tools/output/fileadder (Makefile:62-67)
        ▼
ファームウェアイメージ (512KB)
  - HTML 領域: 0x40000 から埋め込み
  - html_data.c/h: ファイル名→(アドレス, サイズ, MIME) の index を自動生成
```

- `tools/httpd_sim.c` (Linux シミュレータ) はカレントディレクトリから `fopen` で配信
  (httpd_sim.c:883、`html/` で起動する想定)

---

## 制約 (検証済みの事実)

### 1. Flash 容量

- イメージ全体: 512KB (`IMAGESIZE = 524288`)
- HTML 領域開始: `0x40000` (262144)、後方に config データ (`0x6F000`, `0x70000`)
- **実効上限: 0x40000〜0x6F000 = 約188KB**、現在使用 約101KB、余裕 約87KB
- → **サイズは制約にならない**。esbuild minify (25-50% 削減) はボーナスに過ぎない

### 2. 配信方式 (非圧縮・逐次送信)

- ファイルは圧縮されず flash から配信される (`TCP_OUTBUF_SIZE` 2500B + MSS 1460B 単位で ACK ごとに逐次送信)
- **JS サイズ = 転送量 = TCP RTT 数に直結**
- **uIP は HTTP keep-alive 非対応** → `<script src>` 1つ = TCP 接続1回分のコスト
- → ファイル数を増やさないこと (結合維持) が重要

### 3. インラインハンドラ

- `index.html` に **44個のインラインハンドラ**が main.js のグローバル関数を直接呼ぶ
  (`onclick="nav('dash')"` 等)
- → モジュール化 (IIFE) すると関数がスコープ内に閉じ込められるため、
  `window.nav = nav` 等の明示公開が 44 箇所必要になる

### 4. 直列ローダーの維持

- 並列取得によるデバイスのファイル破損対策として、`index.html` / `login.html` は
  `DOMContentLoaded` 後に `document.createElement('script')` で直列ロードする
  動的ローダーを使用中 (index.html:464)
- TS 化後も生成 JS をこのローダーで読み込む構成を維持すること

### 5. i18n.js の分離

- `login.html` は **i18n.js のみ**を読み込み (main.js なし) で `t()` を使用
- i18n.js を main.js に結合すると login ページに main.js の init 処理まで引き込まれる
- → **i18n は単独ファイルとして配信を維持**

### 6. `#{func}` 置換機構

- `fileadder` が全ファイルの `#{...}` をファームウェア関数呼び出し `{NNN}` に置換
- 現在 JS には未使用。`#{` は JS/TS 構文として不正
- → 将来使う場合は別記法が必要 (TS 化とは独立した制約)

---

## 選択肢の評価

| 選択肢 | 型安全性 | ビルドステップ | インラインハンドラ対応 | デバッグ | 推奨 |
|--------|---------|----------------|----------------------|---------|------|
| **0. JSDoc + `@ts-check`** | あり (JSDoc 注釈ベース) | **なし** (tsc --checkJs のみ検証に使用可) | 影響なし | そのまま | **まずこれ** |
| **1. 方式B**: TS + グローバルスコープ (`tsc --outFile`) | あり | あり (node + tsc) | 自然に対応 (グローバルのまま) | 生成 JS | 必要なら |
| **2. 方式A**: TS モジュール + esbuild バンドル | あり | あり (node + tsc + esbuild) | **44ハンドラの `window` 公開が必要** | 生成 JS | 見送り |

### 段階0: JSDoc + `// @ts-check` (推奨)

- 既存 JS の先頭に `// @ts-check` を追加し、`tsc --noEmit` で型チェックを開始
- 型注釈は JSDoc コメント (`/** @param {number} x */` 等) で段階的に追加
- **ビルドステップが増えない** (make フロー・配信ファイル・実機デバッグが現状のまま)
- 埋め込みファームウェアの開発ループ (編集→再アップロード→実機確認) に一切影響しない
- 不十分になった時点で初めて段階1 を検討する

### 段階1: 方式B (条件付き採用)

- `.ts` にリネームし、import/export を使わず tsc `--outFile` (module:none) で
  単一の `main.js` を生成
- インラインハンドラはグローバル関数のまま → 44箇所の変更不要
- **生成 JS はコミットする** (ビルドホストの node 依存を回避し、現行 `make` のまま動作)
  - 注意: 生成 JS がコミットされるため、`.ts` と `.js` の乖離リスクがある。
    `make webui` で再生成し、diff を確認する運用を徹底する
- i18n.ts は単独で `i18n.js` を生成 (login.html 用に分離維持)
- 配信は `html/dist/` を fileadder / httpd_sim が参照する形に変更

### 見送り: 方式A (モジュール分割 + esbuild)

- import/export による責務分割と tree shaking は魅力的だが、
  44 個のインラインハンドラを `window` に公開する儀式が発生
- 単一ページアプリ (SPA) であり、モジュール分割の利益が小さい
- esbuild 追加でビルド依存が増え、`dist` の扱いも複雑になる

---

## 性能への影響 (明確化)

`doc/webui-performance.md` の調査結果 (2026-08):

- 主要ボトルネックは ①直列リクエストキュー (フロントエンド設計 + バックエンドの
  グローバル状態 + uIP keep-alive なし) ②レジスタ読み出し (SFR busy-wait)
  ③TCP RTT。**フロントエンドの JS 実行時間はボトルネックではない**
- アセンブラ移植 (`perf(mem)`, `perf(util)`) で整形処理は高速化されたが、
  TTFB への寄与は数 ms 未満
- → **TS 化・minify で体感速度は変わらない。** 性能が目的ならバックエンドの
  レジスタ読み出し削減やリクエスト削減を優先すること

---

## 推奨実装手順

1. `html/main.js` と `html/i18n.js` に `// @ts-check` を追加し、`tsc --noEmit` で
   既存の型エラーを洗い出す (JSDoc 注釈を最小限追加)
2. 型エラーが許容範囲に収まったら、重要関数 (fetchAPI, nav, 各 send_* ハンドラ) に
   JSDoc 型注釈を追加
3. 段階0 で不十分と判断した場合のみ段階1 (方式B) に進む:
   - `.ts` 化 → `tsc --outFile` で `html/dist/main.js` / `html/dist/i18n.js` を生成
   - Makefile の `HTML :=` と fileadder、httpd_sim の参照先を `html/dist` へ変更
   - 生成 JS をコミットし、`make webui` ターゲットで再生成可能にする
4. 方式A (esbuild バンドル) は採用しない

---

## リスクまとめ

| Risk | Impact | Mitigation |
|------|--------|------------|
| 段階1 のビルド依存 (node) | ビルド環境の変化 | 生成 JS をコミットし、node は任意依存に |
| 生成 JS と .ts の乖離 | コミット済み JS が古くなる | `make webui` + diff 確認の運用を徹底 |
| インラインハンドラとの非互換 | 全ページの操作不能 | 方式B 採用で回避 (グローバルのまま) |
| 直列ローダー / i18n 分離の崩壊 | ファイル破損・login ページ破綻 | 配信ファイル構成を現状のまま維持 |
| `#{func}` 置換との非互換 | 将来のファームウェア連携 | 別記法の導入 (TS 化とは独立) |

---

## 経緯

- 本ドキュメントは旧版 (TypeScript Conversion Feasibility) を大幅に書き直したもの。
  旧版の制約分析 (Flash 容量・配信方式・直列ローダー・i18n 分離・`#{func}`) は
  正確であり、本版に引き継いでいる。
- 変更の背景: フル TS 移行の価値提案が弱い (性能に寄与しない、44 個のインラインハンドラ、
  埋め込み開発ループへのビルドステップ追加) ことが調査で判明したため、
  JSDoc 先行・方式B 限定の段階的方針に改めた。
