#!/usr/bin/env bash
# End-to-end test of the terminal client: scripted stdin sessions against build/forum.
#   usage: BUILD=build bash tests/cli_e2e.sh
set -u
cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD:-build}"
BIN="$BUILD_DIR/forum"
WEB="$BUILD_DIR/forum-web"
[ -x "$BIN" ] || { echo "missing $BIN (run make first)"; exit 2; }

TMP=$(mktemp -d)
DATA="$TMP/data"
WEB_PID=""
trap '[ -n "$WEB_PID" ] && kill -TERM "$WEB_PID" 2>/dev/null; rm -rf "$TMP"' EXIT

PASS=0
FAILS=0
ok()  { PASS=$((PASS + 1)); }
bad() { FAILS=$((FAILS + 1)); echo "FAIL: $1"; [ -n "${2:-}" ] && echo "      $2"; }
eq()   { if [ "$2" = "$3" ]; then ok; else bad "$1" "expected [$2], got [$3]"; fi; }
has()  { case "$3" in *"$2"*) ok ;; *) bad "$1" "expected [$2] in output" ;; esac; }
hasnt() { case "$3" in *"$2"*) bad "$1" "did not expect [$2]" ;; *) ok ;; esac; }

# run_cli "<one input per line>" [extra args...]  ->  sets OUT, ERR and CODE.
# A run that is still going after 15 seconds is killed and reported as "timeout".
run_cli() {
  local input=$1
  shift
  printf '%s\n' "$input" | "$BIN" --data "$DATA" "$@" > "$TMP/out" 2> "$TMP/err" &
  local pid=$! tries=0
  while kill -0 "$pid" 2>/dev/null && [ $tries -lt 150 ]; do
    sleep 0.1
    tries=$((tries + 1))
  done
  if kill -0 "$pid" 2>/dev/null; then
    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    CODE="timeout"
  else
    wait "$pid"
    CODE=$?
  fi
  OUT=$(cat "$TMP/out")
  ERR=$(cat "$TMP/err")
}

lines() { printf '%s\n' "$@"; }

# ---- session 1: register, topic, posts, replies, likes, edit, delete, sort ----
run_cli "$(lines \
  2 user1 secret1 \
  2 \
  C Topics "named like a data file" \
  1 \
  R 'Hello; with / slash \ and <b>markup</b>' \
  R 'middle post' \
  R 'last post' \
  2 \
  R 'first reply' \
  L L L \
  1 \
  P 'middle post, edited' \
  E \
  2 \
  D y \
  E \
  T \
  E \
  1 1)"
eq "session 1 exits cleanly" "0" "$CODE"
has "registered" "Account registered successfully!" "$OUT"
has "topic created (a topic called Topics is harmless now)" "Topic created!" "$OUT"
has "posts created" "Posted!" "$OUT"
has "reply added" "Reply added!" "$OUT"
has "like" "You liked this post." "$OUT"
has "unlike" "You removed your like." "$OUT"
has "reply like" "You liked this reply." "$OUT"
has "edit" "Post updated!" "$OUT"
has "delete" "Post deleted." "$OUT"
has "hostile text is shown literally" 'Hello; with / slash \ and <b>markup</b>' "$OUT"
AFTER_DELETE=$(printf '%s\n' "$OUT" | sed -n '/Post deleted\./,$p')
hasnt "deleted post is gone from the topic" "middle post" "$AFTER_DELETE"
has "its neighbours are intact" "last post" "$AFTER_DELETE"
has "and the first post too" "Hello; with" "$AFTER_DELETE"
eq "no errors on stderr" "" "$ERR"
eq "sort choice is stored (Time = 1)" "1" "$(grep '^META' "$DATA/forum.db" | cut -f3)"
[ -f "$DATA/Topics.txt" ] && bad "topic title must not become a file name" || ok

# ---- session 2: everything survived a restart ----
run_cli "$(lines \
  3 user1 secret1 \
  2 \
  1 \
  E \
  S LAST E \
  E \
  1 1)"
eq "session 2 exits cleanly" "0" "$CODE"
has "login works after restart" "Logged in successfully!" "$OUT"
has "topic persisted with its 2 remaining posts" "Topics  by user1, 2 posts" "$OUT"
has "post persisted" "Hello; with / slash" "$OUT"
has "search finds the post" "Results for \"LAST\" (1 match)" "$OUT"
has "search result names the post" "last post" "$OUT"

# ---- wrong password ----
run_cli "$(lines 3 user1 nope 1)"
has "wrong password is refused" "Wrong credentials." "$OUT"

# ---- session 3: a second user cannot edit or delete, and likes are per user ----
run_cli "$(lines \
  2 user2 secret2 \
  2 1 1 \
  P D L \
  E E E \
  1 1)"
eq "session 3 exits cleanly" "0" "$CODE"
hasnt "no edit/delete offered on someone else's post" "[P] Edit Post" "$OUT"
hasnt "nor delete" "[D] Delete Post" "$OUT"
has "typing P anyway is rejected" "Invalid input" "$OUT"
has "user2 can like it" "You liked this post." "$OUT"
LIKERS=$(grep '^POST' "$DATA/forum.db" | grep 'Hello;' | cut -f7-)
has "stored likers include user2" "user2" "$LIKERS"

# ---- input that ends mid-menu must exit, not spin ----
run_cli "$(lines 2 user3)"
eq "EOF in the middle of registration exits" "0" "$CODE"
has "and says so" "End of input" "$OUT"
printf '' | "$BIN" --data "$DATA" > /dev/null 2>&1 &
EMPTY_PID=$!
for _ in $(seq 1 50); do kill -0 "$EMPTY_PID" 2>/dev/null || break; sleep 0.1; done
if kill -0 "$EMPTY_PID" 2>/dev/null; then kill -9 "$EMPTY_PID"; bad "empty stdin must exit, not loop"; else wait "$EMPTY_PID"; eq "empty stdin exits cleanly" "0" "$?"; fi

# ---- invalid menu input is handled ----
run_cli "$(lines banana 99 1)"
has "invalid menu choice" "Invalid option!" "$OUT"
eq "and the session still ends cleanly" "0" "$CODE"

# ---- command line ----
"$BIN" --help > "$TMP/help" 2>&1
eq "--help exits 0" "0" "$?"
has "--help describes --data" "--data" "$(cat "$TMP/help")"
"$BIN" --bogus > /dev/null 2>&1
eq "unknown option exits 2" "2" "$?"

# ---- the data directory belongs to one process ----
if [ -x "$WEB" ]; then
  "$WEB" --port 0 --data "$DATA" --web web > "$TMP/web.out" 2> "$TMP/web.err" &
  WEB_PID=$!
  for _ in $(seq 1 50); do grep -q Listening "$TMP/web.out" && break; sleep 0.1; done
  run_cli "$(lines 1)"
  eq "CLI is refused while the web server holds the directory" "1" "$CODE"
  has "and says why" "in use by another forum process" "$ERR"
  kill -TERM "$WEB_PID"; wait "$WEB_PID" 2>/dev/null; WEB_PID=""
  run_cli "$(lines 1)"
  eq "CLI works again once the server has stopped" "0" "$CODE"
fi

# ---- a file this version cannot read is never overwritten ----
DATA="$TMP/other"
mkdir -p "$DATA"
printf 'precious data from some other program\n' > "$DATA/forum.db"
run_cli "$(lines 1)"
eq "refuses an unknown data file" "1" "$CODE"
has "and says why" "refusing to overwrite" "$ERR"
eq "and leaves it untouched" "precious data from some other program" "$(cat "$DATA/forum.db")"

echo "cli_e2e: $PASS passed, $FAILS failed"
[ "$FAILS" -eq 0 ]
