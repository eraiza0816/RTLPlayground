# rtlplayground_exporter

A [Prometheus](https://prometheus.io/) exporter for RTLPlayground-managed network switches. It collects metrics from the switch's existing HTTP API (`/status.json`, `/information.json`, `/counters.json`, etc.) and serves them in Prometheus text format at `/metrics`.

No firmware modifications are required — the exporter speaks the same JSON API that the Web UI uses.

## Usage

```bash
# Build
cd tools/rtlplayground_exporter
go build -o ../output/rtlplayground_exporter .

# Run
./rtlplayground_exporter \
  --target http://192.168.10.247 \
  --password your_password \
  --listen :9101
```

### Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--target` | `http://localhost:8080` | Switch URL |
| `--password` | `1234` | Login password |
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
- `rtl_port_enabled{port, name, logical_port}` — admin state
- `rtl_port_tx_good_packets_total{port, name, logical_port}`
- `rtl_port_tx_bad_packets_total{port, name, logical_port}`
- `rtl_port_rx_good_packets_total{port, name, logical_port}`
- `rtl_port_rx_bad_packets_total{port, name, logical_port}`

### SFP diagnostics (per SFP port)
- `rtl_sfp_temperature_celsius{port, vendor, model}`
- `rtl_sfp_voltage_volts{port}`
- `rtl_sfp_tx_bias_amperes{port}`
- `rtl_sfp_tx_power_dbm{port}`
- `rtl_sfp_rx_power_dbm{port}`

### MIB counters (55 per port)
- `rtl_port_mib_counter{port, counter="In Octets"}` — individual MIB counters indexed by name

### System
- `rtl_switch_info{ip_address, mac_address, sw_ver, hw_ver}` — constant 1 with label metadata
- `rtl_vlan_count` — number of configured VLANs
- `rtl_l2_table_entries` — number of entries in the MAC table

### Feature config
- `rtl_mirror_enabled`, `rtl_mirror_monitor_port` — port mirroring
- `rtl_lag_members{lag_num}` — link aggregation groups
- `rtl_eee_active{port}` — Energy Efficient Ethernet
- `rtl_port_bandwidth_ingress|egress_limit_bytes{port}` — rate limits
- `rtl_port_mtu_bytes{port}` — max frame length

### Collector health
- `rtl_scrape_duration_seconds` — scrape duration
- `rtl_scrape_success` — 1 if the last scrape succeeded

## Architecture

```
 +-----------+     HTTP/JSON      +---------------------+     Prometheus      +-----------+
 |  Switch   | <----------------> | rtlplayground_expoer | <-----------------> | Prometheus |
 |  :80      |   (existing API)   |  :9101/metrics      |    (pull)           |           |
 +-----------+                    +---------------------+                     +-----------+
```

The exporter authenticates once at startup, and uses the session cookie for all subsequent API calls. If a 401 is received, it re-authenticates automatically.

---

# rtlplayground_exporter — 日本語

RTLPlayground 管理下のネットワークスイッチ向け Prometheus エクスポーターです。スイッチの既存 HTTP API（`/status.json`、`/information.json`、`/counters.json` 等）からメトリクスを取得し、Prometheus text format で `/metrics` に公開します。

ファームウェアの改造は一切不要です。エクスポーターは Web UI と同じ JSON API を使用します。

## 使い方

```bash
# ビルド
cd tools/rtlplayground_exporter
go build -o ../output/rtlplayground_exporter .

# 実行
./rtlplayground_exporter \
  --target http://192.168.10.247 \
  --password your_password \
  --listen :9101
```

### フラグ

| フラグ | デフォルト | 説明 |
|--------|-----------|------|
| `--target` | `http://localhost:8080` | スイッチのURL |
| `--password` | `1234` | ログインパスワード |
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
- `rtl_port_enabled{port, name, logical_port}` — 管理状態
- `rtl_port_tx_good_packets_total` / `rtl_port_tx_bad_packets_total`
- `rtl_port_rx_good_packets_total` / `rtl_port_rx_bad_packets_total`

### SFP 診断（SFP ポートのみ）
- `rtl_sfp_temperature_celsius{port, vendor, model}`
- `rtl_sfp_voltage_volts{port}`
- `rtl_sfp_tx_bias_amperes{port}`
- `rtl_sfp_tx_power_dbm{port}`
- `rtl_sfp_rx_power_dbm{port}`

### MIB カウンタ（ポートあたり55種）
- `rtl_port_mib_counter{port, counter="In Octets"}` — 個別 MIB カウンタ

### システム
- `rtl_switch_info{ip_address, mac_address, sw_ver, hw_ver}` — 固定値1にラベルで情報を付加
- `rtl_vlan_count` — 設定済み VLAN 数
- `rtl_l2_table_entries` — MAC アドレステーブルエントリ数

### 機能設定
- `rtl_mirror_enabled`, `rtl_mirror_monitor_port` — ポートミラーリング
- `rtl_lag_members{lag_num}` — リンクアグリゲーション
- `rtl_eee_active{port}` — Energy Efficient Ethernet
- `rtl_port_bandwidth_ingress|egress_limit_bytes{port}` — 帯域制限
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

エクスポーターは起動時に一度認証し、セッションクッキーを以降の API 呼び出しに使用します。401 を受け取った場合は自動的に再認証します。
