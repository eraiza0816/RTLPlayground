# rtlplayground_exporter

A [Prometheus](https://prometheus.io/) exporter for RTLPlayground-managed network switches. It collects metrics from the switch's existing HTTP API (`/status.json`, `/information.json`, `/counters.json`, etc.) and serves them in Prometheus text format at `/metrics`.

No firmware modifications are required — the exporter speaks the same JSON API that the Web UI uses.

With a pre-shared key (`--psk`, 64 hex chars) the exporter accesses the switch through the **encrypted `/enc` API** (ChaCha20-Poly1305 AEAD, RFC 8439) introduced in firmware **v0.2.23**. Every request and response is encrypted and no password login is needed: the exporter authenticates with the encrypted login challenge, so it also works when the firmware is in **PSK mode** (where password logins are rejected, see [doc/authentication.md](../../doc/authentication.md)). Without `--psk` it falls back to the plaintext password + session cookie authentication.

## Supported versions

- **RTLPlayground firmware: v0.2.23 or later** — the encrypted `/enc` API (`api <path>` mode) is required for `--psk` access.
- **Go toolchain: 1.26.5** — the version pinned in `go.mod` and used when building via `make` (`tools/Makefile`). With `GOTOOLCHAIN=auto` (default) a newer Go downloads the required toolchain automatically.

## Usage

```bash
# Build
cd tools/rtlplayground_exporter
go build -o ../output/rtlplayground_exporter .

# Run with password login (firmware without PSK, or no PSK configured)
./rtlplayground_exporter \
  --target http://192.168.10.247 \
  --password your_password \
  --listen :9101

# Run with PSK (encrypted /enc API, firmware v0.2.23+)
./rtlplayground_exporter \
  --target http://192.168.10.247 \
  --psk 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f \
  --listen :9101
```

### Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--target` | `http://localhost:8080` | Switch URL |
| `--password` | `1234` | Login password (used when `--psk` is not set) |
| `--psk` | (empty) | Pre-shared key, 64 hex chars (32 bytes); enables encrypted `/enc` API access. Also read from the `RTLP_PSK` environment variable |
| `--listen` | `:9101` | Exporter listen address |

### Prometheus scrape config

Add to your `prometheus.yml`:

```yaml
scrape_configs:
  - job_name: 'rtlplayground'
    static_configs:
      - targets: ['your-exporter-host:9101']
```

## Metrics

### Port status
- `rtl_port_up{port, name, logical_port}` — link state (1=up)
- `rtl_port_speed_bps{port, name, logical_port}` — negotiated speed
  (firmware link codes: 1=10M, 2=100M, 3=1G, 5=10G, 6=2.5G, 7=5G; 0=down)
- `rtl_port_enabled{port, name, logical_port}` — admin state
- `rtl_port_tx_good_packets_total{port, name, logical_port}`
- `rtl_port_tx_bad_packets_total{port, name, logical_port}`
- `rtl_port_rx_good_packets_total{port, name, logical_port}`
- `rtl_port_rx_bad_packets_total{port, name, logical_port}`

### SFP diagnostics (per SFP port)
- `rtl_sfp_temperature_celsius{port, vendor, model}` — SFF-8472 signed/256 °C
- `rtl_sfp_voltage_volts{port}`
- `rtl_sfp_tx_bias_amperes{port}`
- `rtl_sfp_tx_power_dbm{port}` — NaN when the module reports 0 mW
- `rtl_sfp_rx_power_dbm{port}` — NaN when no light is received (LOS)

### MIB counters (55 per port)
- `rtl_port_mib_counter{port, counter="In Octets"}` — individual MIB counters indexed by name

### System
- `rtl_switch_info{ip_address, mac_address, sw_ver, hw_ver}` — constant 1 with label metadata
- `rtl_vlan_count` — number of configured VLANs
- `rtl_l2_table_entries` — number of entries in the MAC table

### Feature config
- `rtl_mirror_enabled`, `rtl_mirror_monitor_port` — port mirroring
- `rtl_lag_members{lag_num}` — link aggregation groups (1 if the group has members)
- `rtl_eee_active{port}` — Energy Efficient Ethernet
- `rtl_port_bandwidth_ingress|egress_limit_bytes{port}` — rate limits (0 = unlimited)
- `rtl_port_mtu_bytes{port}` — max frame length

### Collector health
- `rtl_scrape_duration_seconds` — scrape duration
- `rtl_scrape_success` — 1 if the last scrape succeeded

## Architecture

```
 +-----------+     HTTP/JSON      +---------------------+     Prometheus      +-----------+
 |  Switch   | <----------------> | rtlplayground_exporter | <-----------------> | Prometheus |
 |  :80      |   (existing API)   |  :9101/metrics      |    (pull)           |           |
 +-----------+                    +---------------------+                     +-----------+
```

When `--psk` is given, every request goes through the firmware's encrypted `/enc` endpoint (`api <path>`, response also encrypted) — no session or login is used. Without `--psk`, the exporter authenticates once at startup with the password and uses the session cookie for all subsequent API calls, re-authenticating automatically on 401.

---

# rtlplayground_exporter — 日本語

RTLPlayground 管理下のネットワークスイッチ向け Prometheus エクスポーターです。スイッチの既存 HTTP API（`/status.json`、`/information.json`、`/counters.json` 等）からメトリクスを取得し、Prometheus text format で `/metrics` に公開します。

ファームウェアの改造は一切不要です。エクスポーターは Web UI と同じ JSON API を使用します。

プリシェアードキー（`--psk`、64 hex 文字）を指定すると、ファームウェア **v0.2.23** で導入された **暗号化 `/enc` API**（ChaCha20-Poly1305 AEAD、RFC 8439）経由でアクセスします。リクエスト・レスポンスは全て暗号化され、パスワードログインは不要です。`--psk` 未指定時は従来どおり平文のパスワード + セッションクッキー認証を使用します。

## 対応バージョン

- **RTLPlayground ファームウェア: v0.2.23 以降** — `--psk` でのアクセスには暗号化 `/enc` API（`api <path>` モード）が必要です。
- **Go ツールチェーン: 1.26.5** — `go.mod` で固定され、`make`（`tools/Makefile`）でのビルド時に使用するバージョンです。`GOTOOLCHAIN=auto`（デフォルト）では新しい Go が自動的に必要なツールチェーンを取得します。

## 使い方

```bash
# ビルド
cd tools/rtlplayground_exporter
go build -o ../output/rtlplayground_exporter .

# パスワードログインで実行（PSK 未設定のファームウェア向け）
./rtlplayground_exporter \
  --target http://192.168.10.247 \
  --password your_password \
  --listen :9101

# PSK で実行（暗号化 /enc API、ファームウェア v0.2.23 以降）
./rtlplayground_exporter \
  --target http://192.168.10.247 \
  --psk 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f \
  --listen :9101
```

### フラグ

| フラグ | デフォルト | 説明 |
|--------|-----------|------|
| `--target` | `http://localhost:8080` | スイッチのURL |
| `--password` | `1234` | ログインパスワード（`--psk` 未指定時のみ使用） |
| `--psk` | （なし） | プリシェアードキー（64 hex 文字 = 32 バイト）。指定すると暗号化 `/enc` API でアクセス。`RTLP_PSK` 環境変数からも読み込み |
| `--listen` | `:9101` | エクスポーターの待受アドレス |

### Prometheus スクレイプ設定

`prometheus.yml` に以下を追加:

```yaml
scrape_configs:
  - job_name: 'rtlplayground'
    static_configs:
      - targets: ['your-exporter-host:9101']
```

## メトリクス一覧

### ポートステータス
- `rtl_port_up{port, name, logical_port}` — リンク状態（1=up）
- `rtl_port_speed_bps{port, name, logical_port}` — ネゴシエーション速度
  （ファームの link コード: 1=10M, 2=100M, 3=1G, 5=10G, 6=2.5G, 7=5G; 0=down）
- `rtl_port_enabled{port, name, logical_port}` — 管理状態
- `rtl_port_tx_good_packets_total` / `rtl_port_tx_bad_packets_total`
- `rtl_port_rx_good_packets_total` / `rtl_port_rx_bad_packets_total`

### SFP 診断（SFP ポートのみ）
- `rtl_sfp_temperature_celsius{port, vendor, model}` — SFF-8472 符号付き /256 °C
- `rtl_sfp_voltage_volts{port}`
- `rtl_sfp_tx_bias_amperes{port}`
- `rtl_sfp_tx_power_dbm{port}` — 0 mW 報告時は NaN
- `rtl_sfp_rx_power_dbm{port}` — 受信光なし (LOS) 時は NaN

### MIB カウンタ（ポートあたり55種）
- `rtl_port_mib_counter{port, counter="In Octets"}` — 個別 MIB カウンタ

### システム
- `rtl_switch_info{ip_address, mac_address, sw_ver, hw_ver}` — 固定値1にラベルで情報を付加
- `rtl_vlan_count` — 設定済み VLAN 数
- `rtl_l2_table_entries` — MAC アドレステーブルエントリ数

### 機能設定
- `rtl_mirror_enabled`, `rtl_mirror_monitor_port` — ポートミラーリング
- `rtl_lag_members{lag_num}` — リンクアグリゲーション（メンバーあり=1）
- `rtl_eee_active{port}` — Energy Efficient Ethernet
- `rtl_port_bandwidth_ingress|egress_limit_bytes{port}` — 帯域制限（0=無制限）
- `rtl_port_mtu_bytes{port}` — 最大フレーム長

### コレクター健全性
- `rtl_scrape_duration_seconds` — スクレイプ所要時間
- `rtl_scrape_success` — 最後のスクレイプが成功したか（1=成功）

## アーキテクチャ

```
 +-----------+     HTTP/JSON      +---------------------+     Prometheus      +-----------+
 |  スイッチ  | <----------------> | rtlplayground_exporter | <-----------------> | Prometheus |
 |  :80      |   (既存API)        |  :9101/metrics      |    (pull)           |           |
 +-----------+                    +---------------------+                     +-----------+
```

`--psk` 指定時は、すべてのリクエストがファームウェアの暗号化 `/enc` エンドポイント（`api <path>`、応答も暗号化）経由で送信されます。セッション・ログインは不要です。`--psk` 未指定時は、エクスポーターは起動時に一度パスワードで認証し、セッションクッキーを以降の API 呼び出しに使用します。401 を受け取った場合は自動的に再認証します。
