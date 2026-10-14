#!/usr/bin/env bash
# Starts forum-web and headless Chrome, then drives the real web UI (see ui_check.mjs).
# Needs Node 22+ and Google Chrome or Chromium; set CHROME=/path/to/browser if it is not found.
#   usage: BUILD=build bash tests/ui_check.sh [screenshot-dir]
set -u
cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD:-build}"
BIN="$BUILD_DIR/forum-web"
SHOTS="${1:-$BUILD_DIR/ui-shots}"

[ -x "$BIN" ] || { echo "missing $BIN (run make first)"; exit 2; }
command -v node > /dev/null || { echo "node not found (needs Node 22 or newer)"; exit 2; }

CHROME="${CHROME:-}"
if [ -z "$CHROME" ]; then
  for candidate in "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
      google-chrome google-chrome-stable chromium chromium-browser; do
    if [ -x "$candidate" ] || command -v "$candidate" > /dev/null 2>&1; then
      CHROME=$candidate
      break
    fi
  done
fi
[ -n "$CHROME" ] || { echo "no Chrome or Chromium found (set CHROME=...)"; exit 2; }

TMP=$(mktemp -d)
SERVER_PID=""
CHROME_PID=""
cleanup() {
  [ -n "$CHROME_PID" ] && kill "$CHROME_PID" 2> /dev/null
  [ -n "$SERVER_PID" ] && kill -TERM "$SERVER_PID" 2> /dev/null
  wait 2> /dev/null
  rm -rf "$TMP"
}
trap cleanup EXIT

"$BIN" --port 0 --data "$TMP/data" --web web > "$TMP/server.out" 2> "$TMP/server.err" &
SERVER_PID=$!
for _ in $(seq 1 50); do grep -q Listening "$TMP/server.out" && break; sleep 0.1; done
PORT=$(sed -n 's/^Listening on http:\/\/[^:]*:\([0-9][0-9]*\)$/\1/p' "$TMP/server.out")
[ -n "$PORT" ] || { echo "server did not start"; cat "$TMP/server.err"; exit 2; }

DEBUG_PORT=$((20000 + RANDOM % 20000))
"$CHROME" --headless=new --disable-gpu --remote-debugging-port="$DEBUG_PORT" --user-data-dir="$TMP/profile" \
  --no-first-run --no-default-browser-check about:blank > /dev/null 2>&1 &
CHROME_PID=$!
for _ in $(seq 1 100); do
  curl -s "http://127.0.0.1:$DEBUG_PORT/json/version" > /dev/null && break
  sleep 0.2
done

mkdir -p "$SHOTS"
node tests/ui_check.mjs "http://127.0.0.1:$PORT" "$SHOTS" "$DEBUG_PORT"
RESULT=$?
[ -s "$TMP/server.err" ] && { echo "server wrote to stderr:"; cat "$TMP/server.err"; RESULT=1; }
echo "screenshots: $SHOTS"
exit $RESULT
