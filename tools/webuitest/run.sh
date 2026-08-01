#!/bin/sh
# Start the httpd_sim simulator and run the WebUI end-to-end test.
#
# Usage:
#   ./run.sh                     # sim on 18080, test with password 1234
#   WUI_PORT=8080 ./run.sh
#   WUI_PASSWORD=xxxx ./run.sh
#   WUI_URL=http://192.168.10.247:80 WUI_PASSWORD=xxxx ./run.sh   # direct to device
set -e

cd "$(dirname "$0")/../.."   # repo root
SIM_PORT="${WUI_PORT:-18080}"
WUI_URL="${WUI_URL:-http://127.0.0.1:$SIM_PORT}"

# Build the simulator if it does not exist yet
if [ ! -x tools/output/httpd_sim ]; then
  echo "Building httpd_sim..."
  make -C tools httpd_sim
fi

cleanup() {
  pkill -f "output/httpd_sim $SIM_PORT" 2>/dev/null || true
}
trap cleanup EXIT

if [ -z "$WUI_URL_DEVICE" ]; then
  echo "Starting simulator on port $SIM_PORT (serving html/)..."
  ( cd html && setsid ../tools/output/httpd_sim "$SIM_PORT" >/tmp/webuitest_sim.log 2>&1 < /dev/null & )
  sleep 1
fi

cd tools/webuitest
if [ ! -d node_modules ]; then
  echo "Installing playwright (first run)..."
  npm install
fi

exec node test.js "$WUI_URL" "$WUI_PASSWORD"
