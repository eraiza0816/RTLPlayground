#!/usr/bin/env bash
#
# sfp-fc2eth.sh - FC SFP モジュールを Ethernet 認識に書き換える
#
# 使い方:
#   ./sfp-fc2eth.sh [1|2|all]     (省略時は all)
#
# 動作:
#   1. モジュール有無と現在のEEPROMを確認 (/sfp_eeprom.json)
#   2. 未書き換えなら telnet で特権モードに入り
#      sfp <slot> patch  : compliance + rate byte を Ethernet 用に書き換え
#      sfp <slot> auto   : 挿入検出を再トリガ(SerDes自動設定)
#   3. EEPROMを読み戻してバイト単位でベリファイ
#   4. rtlpctl status でリンク状態を表示
#
# 注意:
#   - ファームが Full CLI の場合、HTTP /cmd は debug コマンド(sfp 等)を
#     拒否するため、書き込みは telnet(23/tcp)経由で行う
#   - 一度書き換えたモジュールに対しては patch を再実行しない(検証のみ)
#
# 設定(環境変数):
#   RTLP_HOST      スイッチのIPアドレス (既定: 192.168.10.247)
#   RTLP_PASSWORD  管理パスワード     (既定: 1234)
#
set -euo pipefail

HOST="${RTLP_HOST:-192.168.10.247}"
PASSWORD="${RTLP_PASSWORD:-1234}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RTLPCTL="$SCRIPT_DIR/rtlpctl/rtlpctl"
COOKIE="$(mktemp)"
TMPDIR_SFP="$(mktemp -d)"
trap 'rm -rf "$COOKIE" "$TMPDIR_SFP"' EXIT

TARGETS=()
for a in "$@"; do
	case "$a" in
		1|2|all) TARGETS+=("$a") ;;
		*) echo "使い方: $0 [1|2|all]" >&2; exit 2 ;;
	esac
done
[ ${#TARGETS[@]} -eq 0 ] && TARGETS=(all)

# スロット番号(コンソール表記 1|2)へ展開
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

# ---- HTTP ログイン -------------------------------------------------------
login() {
	curl -s -m 5 -c "$COOKIE" -d "pwd=$PASSWORD" "http://$HOST/login" -o /dev/null
	curl -s -m 5 -b "$COOKIE" "http://$HOST/information.json" | grep -q sw_ver
}

# ---- EEPROM 取得: slot(1|2) を 256byte の hex へ --------------------------
fetch_eeprom() { # $1=console slot
	local json_slot=$(( $1 - 1 ))
	login || return 1
	curl -s -m 5 -b "$COOKIE" "http://$HOST/sfp_eeprom.json?slot=$json_slot" \
		| python3 -c '
import sys, json
try:
    d = json.load(sys.stdin)["data"]
except Exception:
    sys.exit(1)
open(sys.argv[1], "w").write(d)
' "$TMPDIR_SFP/slot$1.hex"
}

# ---- EEPROM 内容サマリ ----------------------------------------------------
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

# ---- ベリファイ -----------------------------------------------------------
verify_eeprom() { # $1=file -> 戻り値 0=OK
	python3 - "$1" <<'PYEOF'
import sys
b = bytes.fromhex(open(sys.argv[1]).read().strip())
want = {3: 0x20, 6: 0x02, 7: 0x00, 9: 0x00, 12: 0x67}
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

already_patched() { # $1=file -> 戻り値 0=書き換え済み
	python3 - "$1" <<'PYEOF'
import sys
b = bytes.fromhex(open(sys.argv[1]).read().strip())
sys.exit(0 if (b[0] == 0x03 and b[3] == 0x20 and b[6] == 0x02
               and b[7] == 0x00 and b[9] == 0x00 and b[12] == 0x67) else 1)
PYEOF
}

# ---- メイン ----------------------------------------------------------------
say "接続確認: http://$HOST"
login || { bad "ログイン失敗 (HOST/PASSWORD を確認)"; exit 1; }
ok "ログイン成功"

# 各スロットの事前確認 + 必要なコマンド収集
TELNET_CMDS=()
NEED_WORK=()
for s in "${SLOTS[@]}"; do
	say "Slot $s 事前確認"
	if ! fetch_eeprom "$s"; then
		bad "Slot $s: EEPROM読み取り失敗"; continue
	fi
	if ! show_eeprom "$TMPDIR_SFP/slot$s.hex"; then
		bad "Slot $s: モジュールが見つかりません"; continue
	fi
	if already_patched "$TMPDIR_SFP/slot$s.hex"; then
		ok "Slot $s: 既に書き換え済み (patchスキップ、検証のみ)"
	else
		info "Slot $s: 書き換えが必要 -> patch/auto を実行"
		TELNET_CMDS+=("sfp $s patch" "sfp $s auto")
		NEED_WORK+=("$s")
	fi
done

# telnet 特権セッションで書き込み
if [ ${#TELNET_CMDS[@]} -gt 0 ]; then
	say "telnet で書き込み実行 (${TELNET_CMDS[*]})"
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
	say "書き込み不要 (全スロット書き換え済みまたは対象なし)"
fi

# ベリファイ
RESULT=0
for s in "${SLOTS[@]}"; do
	say "Slot $s ベリファイ"
	if ! fetch_eeprom "$s"; then
		bad "Slot $s: EEPROM読み取り失敗"; RESULT=1; continue
	fi
	show_eeprom "$TMPDIR_SFP/slot$s.hex" || true
	if verify_eeprom "$TMPDIR_SFP/slot$s.hex"; then
		ok "Slot $s: ベリファイ成功"
	else
		bad "Slot $s: ベリファイ失敗"
		RESULT=1
	fi
done

# リンク状態表示
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
