# Arista EOS CLI 互換性

> 最終更新: 2026-07-20
> 対象: RTLPlayground v0.2.19

## 総合評価

| カテゴリ | 件数 | 割合 |
|----------|------|------|
| 完全互換（構文・意味一致） | 7 | 7% |
| 類似（概念は同じ、構文が違う） | 33 | 33% |
| 独自（EOSにないコマンド） | 19 | 19% |
| 未実装（EOSにあってないもの） | 45 | 45% |
| **合計** | **104** | **100%** |

**総合互換率: ~40%**（完全互換 + 類似）

本デバイスは L2 スイッチ専用の 8bit MCU (8051) であり、メモリ制約 (256B RAM + ~8KB XRAM) やハードウェア制限により Arista EOS との完全互換は不可能。CLI は **"EOS にインスパイアされた組み込み向け最適化インターフェース"** と位置づける。

---

## モード体系

| モード | うちの実装 | EOS | 互換 |
|--------|-----------|-----|------|
| EXEC (`>`) | `MODE_EXEC` | EXEC | ○ |
| PRIVILEGED (`#`) | `MODE_PRIVILEGED` | PRIVILEGED EXEC | ○ |
| CONFIG (`(config)#`) | `MODE_CONFIG` | GLOBAL CONFIGURATION | ○ |
| CONFIG-IF (`(config-if)#`) | `MODE_CONFIG_IF` | INTERFACE CONFIGURATION | ○ |
| CONFIG-VLAN (`(config-vlan)#`) | `MODE_CONFIG_VLAN` | VLAN CONFIGURATION | ○ |

### モード遷移

| 操作 | 遷移先 | EOS同 |
|------|--------|-------|
| `enable` | EXEC → PRIVILEGED | ○ |
| `disable` | PRIVILEGED → EXEC | ○ |
| `configure terminal` | PRIVILEGED → CONFIG | ○ |
| `exit` | 一つ上のモードへ | ○ |
| `end` | どのモードからも PRIVILEGED へ | ○ |

### 実装上の制限

- **Enable パスワードなし**: `enable` でパスワード認証不要（常に昇格可能）
- **権限レベルなし**: コマンドごとの権限制御は mode mask のみ（AAA 相当なし）
- **プロンプト表示**: EOS 形式 (`hostname>`, `hostname#`, `hostname(config)#`) にはなっていない
- **`do` コマンドなし**: Config モードから EXEC コマンドを実行する `do show run` 相当は未実装

---

## コマンド互換性リスト

### 完全互換 (7)

| コマンド | 引数 | モード | 備考 |
|----------|------|--------|------|
| `enable` | - | EXEC | |
| `disable` | - | PRIVILEGED | |
| `configure terminal` | - | PRIVILEGED | |
| `exit` | - | 全モード | |
| `end` | - | CONFIG/CONFIG-IF/CONFIG-VLAN | |
| `hostname` | `<name>` | CONFIG | 英数字 + `-`/`_` |
| `?` / `help` | - | 全モード | コンテキスト依存ヘルプ |

### 類似（概念は同じ、構文が違う）(33)

| # | コマンド | うちの構文 | EOS 構文 | 差分 |
|---|---------|-----------|---------|------|
| 1 | IP設定 | `ip <addr>` (別途 `netmask <mask>`) | `ip address <addr> <mask>` | EOS は IP とマスクを一行で指定 |
| 2 | DHCP | `ip dhcp` | `ip address dhcp` | キーワード数違い |
| 3 | GW | `gw <addr>` | `ip default-gateway <addr>` | コマンド名違い |
| 4 | バージョン表示 | `version` | `show version` | EOS は `show` 必須 |
| 5 | 履歴 | `history` | `show history` | 同上 |
| 6 | 時刻 | `time` (tick counter) | `show clock` (RTC) | 内容も異なる |
| 7 | 統計 | `stat` | `show interfaces counters` | コマンド名違い |
| 8 | パスワード設定 | `passwd <pw>` | `enable secret <pw>` / `username ...` | EOS はユーザー/パスワードモデル |
| 9 | ポート状態表示 | `port <n> show` | `show interface ethernet <n>` | EOS は `show interface` |
| 10 | ポート有効/無効 | `port <n> on/off` | `no shutdown` / `shutdown` (in config-if) | キーワード違い |
| 11 | ポート速度 | `port <n> 1g/10g/2g5/5g/100m` | `speed 1000/10000/2500/5000/100` (in config-if) | 単位表現違い (`1g` vs `1000`) |
| 12 | ポート Duplex | `port <n> duplex half/full` | `duplex half/full` (in config-if) | 構文違い |
| 13 | ポート名 | `port <n> name <text>` | `description <text>` (in config-if) | EOS は `description` |
| 14 | MTU | `mtu <port> <size>` | `mtu <size>` (in config-if) | EOS はインターフェースサブモード |
| 15 | VLAN 設定 | `vlan <id> <ports...>` (1行) | `vlan <id>` → `tagged/untagged` (サブモード) | 設計思想の違い |
| 16 | VLAN 表示 | `vlan show` (in CONFIG-VLAN) | `show vlan` (in PRIVILEGED/EXEC) | モードと構文違い |
| 17 | VLAN 削除 | `vlan <id> d` | `no vlan <id>` | サフィックス vs `no` |
| 18 | Management VLAN | `vlan <id> mgmt` | `interface vlan <id>` で管理 | 別コンセプト |
| 19 | PVID | `pvid <port> <vlan>` | `switchport access vlan <id>` (in config-if) | コマンド名違い |
| 20 | ミラーリング | `mirror <dp> <sources>` | `monitor session <n> source/destination` | EOS は複数コマンド |
| 21 | ミラー状態/解除 | `mirror status/off` | `show monitor session` / `no monitor session` | キーワード違い |
| 22 | LAG 設定 | `lag <group> <ports>` | `interface port-channel <n>` + `channel-group` | EOS は複数コマンド |
| 23 | LAG ハッシュ | `laghash <group> <params>` | `port-channel load-balance ethernet` | コマンド名と引数順序 |
| 24 | STP | `stp on/off` | `spanning-tree mode <mode>` / `no spanning-tree` | EOS はモード選択可 |
| 25 | IGMP | `igmp on/off/show` | `ip igmp snooping` / `no` / `show` | キーワード数違い |
| 26 | EEE | `eee on/off/status [port] [speed]` | `power efficient-ethernet` (in config-if) | 構文違い |
| 27 | Telnet 制御 | `telnet on/off` | `management telnet` / `transport input telnet` | 構文違い |
| 28 | Web 制御 | `web on/off` | `management http/https` | 構文違い |
| 29 | MAC テーブル表示 | `l2` | `show mac address-table` | コマンド名違い |
| 30 | MAC テーブル削除 | `l2 del <idx>` | `no mac address-table ...` | 構文違い |
| 31 | MAC テーブルクリア | `l2 forget` | `clear mac address-table` | コマンド名違い |
| 32 | 設定保存 | `commit` | `copy running-config startup-config` / `write memory` | EOS は `copy` / `write` |
| 33 | IP 表示 | `ip` (引数なし) | `show ip interface brief` | 構文違い |

### 独自コマンド (19)

EOS に存在せず、本デバイスに固有のコマンド。

| コマンド | 機能 | 理由 |
|----------|------|------|
| `reset` | 即時チップリセット | EOS は `reload` だが確認あり |
| `sfp <slot> {describe\|dump\|save\|restore\|fix\|patch\|clone\|checksum\|write\|bulk}` | SFP EEPROM 操作 | EOS は読み取り専用 (`show interface transceiver`) |
| `flash {s\|j\|u}` | フラッシュ操作（JEDEC ID/UID/セキュリティ） | HW デバッグ用 |
| `sds` | SerDes モード表示 | HW デバッグ用 |
| `gpio` | GPIO 入力状態表示 | HW デバッグ用 |
| `regget <hex>` | スイッチレジスタ読み取り | HW デバッグ用 |
| `regset <hex> <hex>` | スイッチレジスタ書き込み | HW デバッグ用 |
| `sdsget <id> <page> <reg>` | SerDes レジスタ読み取り | HW デバッグ用 |
| `sdsset <id> <page> <reg> <val>` | SerDes レジスタ書き込み | HW デバッグ用 |
| `phyget <id> <dev> <reg>` | PHY (Clause 45) 読み取り | HW デバッグ用 |
| `physet <id> <dev> <reg> <val>` | PHY (Clause 45) 書き込み | HW デバッグ用 |
| `rnd` | ハードウェア乱数生成 | テスト用 |
| `xmodem` | XMODEM ファームウェア更新 | シリアル回復用 |
| `ingress {u\|t\|a}` | Ingress VLAN フィルタ設定 | HW 固有機能 |
| `isolate <port> <ports...>` | ポート分離 (PVLAN-like) | HW 固有機能 |
| `bw {in\|out} <port> <hex>` | 帯域制御 | HW 固有機能 (raw hex 指定) |
| `show` (引数なし) | 簡易システム情報 | `show running-config` の代用ではない |
| `netmask <mask>` | ネットマスク設定 | EOS は `ip address` に内包 |

### 未実装 (45)

EOS にあって本デバイスにないコマンド。重要度順。

#### Critical (基本運用に必須)

| コマンド | EOS 構文 | 備考 |
|----------|---------|------|
| ping | `ping <host>` | 基本診断 |
| show running-config | `show running-config` | 動作中コンフィグ表示 |
| show startup-config | `show startup-config` | 保存コンフィグ表示 |
| show interfaces | `show interfaces [status\|counters]` | ポート状態/カウンタ |

#### High (実装優先度高)

| コマンド | EOS 構文 | 備考 |
|----------|---------|------|
| `no` 否定接頭辞 | `no shutdown`, `no hostname` ... | 全コマンドに適用 |
| interface サブモード | `interface ethernet <n>` | `port <n>` で代用中 |
| show ip interface | `show ip interface brief` | `ip` で代用中 |
| show vlan | `show vlan` | `vlan show` で代用 (Config内のみ) |
| show mac address-table | `show mac address-table` | `l2` で代用 |
| copy running-config startup-config | `copy running-config startup-config` | `commit` で代用 |
| username | `username <name> secret <pw>` | ユーザー管理なし |
| enable secret | `enable secret <pw>` | 特権モード認証なし |
| aaa | `aaa authentication login default local` | 認証フレームワークなし |
| logging | `logging host <ip>` / `logging buffered <size>` | syslog なし |
| ntp / clock set | `ntp server <ip>` / `clock set <time>` | 時刻同期なし |
| ip routing | `ip routing` | L3 非対応 |
| router ospf / router bgp | 動的ルーティング | L3 非対応 |
| interface vlan <id> | SVI (L3 VLAN インターフェース) | L3 非対応 |
| access-list | `ip access-list extended`, `ip access-group` | フィルタリングなし |
| qos / policy-map / class-map | QoS 設定 | 優先制御なし |
| spanning-tree (詳細) | `spanning-tree mode rapid-pvst` | 今は on/off のみ |
| lldp | `lldp enable` / `show lldp neighbors` | 近隣探索なし |
| ssh | `ssh <host>` / `management ssh` | Telnet のみ |
| igmp querier | `ip igmp snooping querier` | IGMP は on/off のみ |

#### Medium

| コマンド | 備考 |
|----------|------|
| `banner motd` | ログインバナー |
| `snmp-server` | SNMP |
| `show clock` | `time` で代用中（中身別物） |
| `show arp` | ARP テーブル |
| `show port-channel` | LAG 状態 |
| `lacp` | 今は Static LAG のみ |
| `storm-control` | ストーム制御 |
| `sflow` | フローサンプリング |
| `ip domain-name`, `ip name-server` | DNS |
| `show system` | `show` で代用中 |
| `show inventory` | `version` で一部代用 |
| `show environment` | 環境監視 |
| `reload` | `reset` で代用（確認なし即時） |
| `service password-encryption` | パスワード暗号化 |
| `terminal monitor / length` | 端末制御 |
| `do` | Config から EXEC コマンド実行 |

#### Low

| コマンド | 備考 |
|----------|------|
| `errdisable recovery` | エラー回復 |
| `show log` | ログ表示 |
| `debug` | デバッグ（regget/regset で代用） |
| `interface management 1` | 管理ポート |

---

## 設計上の制約と原則

### 実装できないもの（ハードウェア制約）

- **L3 ルーティング** (OSPF/BGP/static routes/ACL/QoS): スイッチチップが L2 のみ対応
- **SSH**: MCU に暗号処理リソース不足
- **NTP 同期**: RTC ハードウェアなし
- **SNMP**: エージェント実装のリソース不足
- **LLDP**: 現状未実装だがリソース次第で可能か
- **AAA/TACACS+**: 外部認証プロトコルは不可

### 今後実装可能なもの（優先度順）

1. **`ping`**: ICMP echo は MCU でも実装可能（uIP スタック利用）
2. **`show running-config`**: 全コンフィグをダンプする関数の追加
3. **`no` 否定接頭辞**: パーサーの拡張（ただし parser state が複雑化）
4. **LLDP**: パケット生成/解析の追加実装
5. **`show vlan` を EXEC/PRIVILEGED からも実行可能に**: モード権限制御の変更

### 設計指針

- EOS 完全互換は目標としない。リソース制約とのトレードオフ。
- EOS ユーザーが迷わない程度の親和性を保つ（モード遷移、Tab 補完、`?` ヘルプ）。
- デバッグ用コマンド (`regget/regset` 等) は削除しない。実機開発には必須。
- 独自コマンド (`isolate`, `bw`, `ingress`) はこのチップの固有機能であり、抽象化しない。
- SFP EEPROM 操作コマンドは本デバイスの差別化機能（現場でのモジュール修理に対応）。

---

## 参考: EOS 互換性実装の進め方

新規コマンド追加時の判断基準:

1. **EOS に存在するか？** → Yes: EOS 構文を優先。No: 独自構文でよい。
2. **リソース (コードサイズ/RAM) は足りるか？** → No: 実装しない。
3. **HW がサポートする機能か？** → No: 実装しない。
4. **デバッグ/運用に必須か？** → Yes: EOS 互換性より機能性を優先（独自構文でよい）。
