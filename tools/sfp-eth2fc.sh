#!/usr/bin/env bash
#
# sfp-eth2fc.sh - Ethernet 化した SFP モジュールを元の FC 状態へ復元する
#                 (sfp-fc2eth.sh の逆操作)
#
# 使い方:
#   ./sfp-eth2fc.sh [1|2|all]     (省略時は all)
#   ./sfp-eth2fc.sh -n [1|2|all]  (-n: dry-run、実行内容の表示のみ)
#
# 動作:
#   patch が書き換えるのは byte 3/6/7/9/12 と CC_BASE のみ。
#   工場出荷の Brocade 57-1000117-01 はこの 5 バイトが全個体共通のため、
#   元の値へ書き戻すことで EEPROM を完全に元に戻せる:
#
#      byte 0x03: 0x20 -> 0x00   (10G Ethernet compliance 消去)
#      byte 0x06: 0x02 -> 0x00   (1000Base-LX compliance 消去)
#      byte 0x07: 0x00 -> 0x40   (FC link length 復元)
#      byte 0x09: 0x00 -> 0x0C   (FC speed 復元)
#      byte 0x0C: 0x67 -> 0x55   (8.5GBd FC signaling rate 復元)
#
#   各 write コマンドは CC_BASE を自動再計算するためチェックサムも戻る
#   (未触れなら工場値 0x6A になる)。
#
# 注意:
#   - フラッシュ退避(sfp <slot> save 済みの個体)から正確に戻す場合は
#     telnet で "sfp <slot> restore" を実行する方が確実。本スクリプトは
#     save を行わない運用でも復元できるよう、定義済み工場値を使う
#   - 復元後もリンク動作は変わらない(挿入検出の再トリガはしない)。
#     抜き差しすれば新 state で再検出される
#
# 設定(環境変数):
#   RTLP_HOST      スイッチのIPアドレス (既定: 192.168.10.247)
#   RTLP_PASSWORD  管理パスワード     (既定: 1234)
#
set -euo pipefail

HOST="${RTLP_HOST:-192.168.10.247}"
PASSWORD="${RTLP_PASSWORD:-1234}"
DRYRUN=0
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RTLPCTL="$SCRIPT_DIR/rtlpctl/rtlpctl"
COOKIE="$(mktemp)"
TMPDIR_SFP="$(mktemp -d)"
trap 'rm -rf "$COOKIE" "$TMPDIR_SFP"' EXIT

TARGETS=()
for a in "$@"; do
	case "$a" in
		-n|--dry-run) DRYRUN=1 ;;
		1|2|all) TARGETS+=("$a") ;;
		*) echo "使い方: $0 [-n] [1|2|all]" >&2; exit 2 ;;
	esac
done
[ ${#TARGETS[@]} -eq 0 ] && TARGETS=(all)

SLOTS=()
for t in "${TARGETS[@]}"; do
	if [ "$t" = all ]; then SLOTS+=(1 2); else SLOTS+=("$t"); fi
done

if [ -z "$PASSWORD" ]; then
	read -rsp "スイッチのパスワード: " PASSWORD && echo || true
fi
if [ -z "$PASSWORD" ]; then
	echo "パスワードが未設定です。RTLP_PASSWORD=xxx $0 のように実行してください" >&2
	exit 1
fi

say()  { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }
ok()   { printf '  \033[32mOK\033[0m  %s\n' "$*"; }
bad()  { printf '  \033[31mNG\033[0m  %s\n' "$*" >&2; }
info() { printf '  -- %s\n' "$*"; }

login() {
	curl -s -m 5 -c "$COOKIE" -d "pwd=$PASSWORD" "http://$HOST/login" -o /dev/null
	curl -s -m 5 -b "$COOKIE" "http://$HOST/information.json" | grep -q sw_ver
}

fetch_eeprom() { # $1=console slot
	local json_slot=$(( $1 - 1 ))
	local i
	for i in 1 2 3; do
		if login \
			&& curl -s -m 5 -b "$COOKIE" "http://$HOST/sfp_eeprom.json?slot=$json_slot" \
				| python3 -c '
import sys, json
try:
    d = json.load(sys.stdin)["data"]
except Exception:
    sys.exit(1)
open(sys.argv[1], "w").write(d)
' "$TMPDIR_SFP/slot$1.hex"; then
			return 0
		fi
		sleep 1
	done
	return 1
}

show_eeprom() { # $1=file
	python3 - "$1" <<'PYEOF'
import sys
b = bytes.fromhex(open(sys.argv[1]).read().strip())
if b[0] != 0x03:
    print("    (モジュールなし/応答不正 ident=%02X)" % b[0]); raise SystemExit(1)
vendor = b[20:36].decode("ascii", "replace").strip()
pn     = b[40:56].decode("ascii", "replace").strip()
sn     = b[68:84].decode("ascii", "replace").strip()
print("    %s %s S/N:%s rate=0x%02X cc_base=0x%02X" % (vendor, pn, sn, b[12], b[0x3F]))
PYEOF
}

verify_eeprom() { # $1=file -> 戻り値 0=FC状態
	python3 - "$1" <<'PYEOF'
import sys
b = bytes.fromhex(open(sys.argv[1]).read().strip())
want = {3: 0x00, 6: 0x00, 7: 0x40, 9: 0x0C, 12: 0x55}
ng = 0
for off, w in sorted(want.items()):
    g = b[off]
    stat = "OK" if g == w else "NG"
    print("    byte 0x%02X: 0x%02X (期待 0x%02X) %s" % (off, g, w, stat))
    ng += (g != w)
cc = sum(b[0:63]) & 0xFF
stat = "OK" if b[0x3F] == cc else "NG"
print("    CC_BASE  : 0x%02X (期待 0x%02X) %s" % (b[0x3F], cc, stat))
ng += (b[0x3F] != cc)
sys.exit(1 if ng else 0)
PYEOF
}

is_eth_patched() { # $1=file -> 戻り値 0=ETH化されている(要復元)
	python3 - "$1" <<'PYEOF'
import sys
b = bytes.fromhex(open(sys.argv[1]).read().strip())
sys.exit(0 if (b[0] == 0x03 and b[3] == 0x20 and b[6] == 0x02
               and b[7] == 0x00 and b[9] == 0x00 and b[12] == 0x67) else 1)
PYEOF
}

is_fc_state() { # $1=file -> 戻り値 0=既にFC状態
	verify_eeprom "$1" >/dev/null 2>&1
}

# ---- メイン ----------------------------------------------------------------
say "接続確認: http://$HOST"
login || { bad "ログイン失敗 (HOST/PASSWORD を確認)"; exit 1; }
ok "ログイン成功"

TELNET_CMDS=()
for s in "${SLOTS[@]}"; do
	say "Slot $s 事前確認"
	if ! fetch_eeprom "$s"; then
		bad "Slot $s: EEPROM読み取り失敗"; continue
	fi
	if ! show_eeprom "$TMPDIR_SFP/slot$s.hex"; then
		bad "Slot $s: モジュールが見つかりません"; continue
	fi
	if is_fc_state "$TMPDIR_SFP/slot$s.hex"; then
		ok "Slot $s: 既にFC状態 (復元不要)"
	elif is_eth_patched "$TMPDIR_SFP/slot$s.hex"; then
		suffix=""
		[ $DRYRUN -eq 1 ] && suffix=" (dry-run)"
		info "Slot $s: ETH化を検出 -> FC値へ復元${suffix}"
		for w in "3 00" "6 00" "7 40" "9 0c" "c 55"; do
			set -- $w
			info "  sfp $s write $1 $2"
			[ $DRYRUN -eq 0 ] && TELNET_CMDS+=("sfp $s write $1 $2")
		done
	else
		bad "Slot $s: ETH化パターンと一致しません (スキップ。手動確認推奨)"
	fi
done

if [ ${#TELNET_CMDS[@]} -gt 0 ]; then
	say "telnet で復元実行 (${#TELNET_CMDS[@]} コマンド)"
	PASSWORD="$PASSWORD" HOST="$HOST" python3 - "${TELNET_CMDS[@]}" <<'PYEOF'
import os, socket, sys, time

host, password = os.environ["HOST"], os.environ["PASSWORD"]
cmds = sys.argv[1:]

def strip_iac(b):
	out, i = bytearray(), 0
	while i < len(b):
		if b[i] == 255 and i + 2 < len(b): i += 3; continue
		out.append(b[i]); i += 1
	return bytes(out)

s = socket.create_connection((host, 23), timeout=5)
s.settimeout(0.5)
buf = b""
def pump():
	global buf
	try:
		while True:
			d = s.recv(4096)
			if not d: break
			buf += d
	except socket.timeout:
		pass

# login
deadline = time.time() + 15
sent = False
while time.time() < deadline:
	pump()
	t = strip_iac(buf).decode("utf-8", "replace")
	if "assword" in t and not sent:
		s.sendall(password.encode() + b"\n"); sent = True; buf = b""; time.sleep(0.5); continue
	if sent and ("#" in t or ">" in t):
		break
	time.sleep(0.3)

# enable
buf = b""
s.sendall(b"enable\n"); time.sleep(1); pump()
if "assword" in strip_iac(buf).decode("utf-8", "replace"):
	s.sendall(password.encode() + b"\n"); time.sleep(1)

# commands
for cmd in cmds:
	buf = b""
	s.sendall(cmd.encode() + b"\n")
	deadline = time.time() + 60
	quiet = 0
	while time.time() < deadline:
		n = len(buf); pump()
		if len(buf) == n:
			quiet += 1
			if quiet >= 6: break
		else:
			quiet = 0
		time.sleep(0.25)
	print("--- %s ---" % cmd)
	print(strip_iac(buf).decode("utf-8", "replace"))
s.close()
PYEOF
else
	say "復元実行なし (全スロットFC状態または対象なし${DRYRUN:+ / dry-run})"
fi

# ベリファイ
RESULT=0
for s in "${SLOTS[@]}"; do
	say "Slot $s ベリファイ"
	if ! fetch_eeprom "$s"; then
		bad "Slot $s: EEPROM読み取り失敗"; RESULT=1; continue
	fi
	show_eeprom "$TMPDIR_SFP/slot$s.hex" || true
	if is_eth_patched "$TMPDIR_SFP/slot$s.hex"; then
		if [ $DRYRUN -eq 1 ]; then
			info "Slot $s: dry-run のため未復元 (期待どおり)"
			continue
		fi
	fi
	if verify_eeprom "$TMPDIR_SFP/slot$s.hex"; then
		ok "Slot $s: ベリファイ成功 (FC状態)"
	else
		bad "Slot $s: ベリファイ失敗"
		RESULT=1
	fi
done

if [ -x "$RTLPCTL" ]; then
	say "リンク状態 (rtlpctl status)"
	RTLP_PASSWORD="$PASSWORD" "$RTLPCTL" --host "$HOST" status || true
fi

say "結果"
if [ "$RESULT" -eq 0 ]; then
	ok "全スロット正常"
else
	bad "失敗したスロットがあります"
fi
exit "$RESULT"
