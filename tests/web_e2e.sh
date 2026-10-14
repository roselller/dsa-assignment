#!/usr/bin/env bash
# End-to-end test of the web server: starts build/forum-web on a free port with a throwaway
# data directory and drives the JSON API with curl.
#   usage: BUILD=build bash tests/web_e2e.sh
set -u
cd "$(dirname "$0")/.."

BIN="${BUILD:-build}/forum-web"
[ -x "$BIN" ] || { echo "missing $BIN (run make first)"; exit 2; }

TMP=$(mktemp -d)
SERVER_PID=""
BASE=""

stop_server() {
  if [ -n "$SERVER_PID" ]; then
    kill -TERM "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null
    SERVER_EXIT=$?
    SERVER_PID=""
  fi
}
trap 'stop_server; rm -rf "$TMP"' EXIT

start_server() {
  : > "$TMP/server.out"
  "$BIN" --port 0 --data "$TMP/data" --web web > "$TMP/server.out" 2> "$TMP/server.err" &
  SERVER_PID=$!
  local port="" tries=0
  while [ $tries -lt 100 ]; do
    port=$(sed -n 's/^Listening on http:\/\/[^:]*:\([0-9][0-9]*\)$/\1/p' "$TMP/server.out")
    if [ -n "$port" ] && curl -s -o /dev/null "http://127.0.0.1:$port/api/me"; then
      BASE="http://127.0.0.1:$port"
      return 0
    fi
    sleep 0.1
    tries=$((tries + 1))
  done
  echo "server did not become ready"; cat "$TMP/server.err"
  exit 2
}

PASS=0
FAILS=0
ok()  { PASS=$((PASS + 1)); }
bad() { FAILS=$((FAILS + 1)); echo "FAIL: $1"; [ -n "${2:-}" ] && echo "      $2"; }
eq()   { if [ "$2" = "$3" ]; then ok; else bad "$1" "expected [$2], got [$3]"; fi; }
has()  { case "$3" in *"$2"*) ok ;; *) bad "$1" "expected [$2] in: $3" ;; esac; }
hasnt() { case "$3" in *"$2"*) bad "$1" "did not expect [$2] in: $3" ;; *) ok ;; esac; }

# call JAR METHOD PATH [curl args...]  ->  sets CODE and BODY. Always sends the X-Forum header.
call() {
  local jar=$1 method=$2 path=$3
  shift 3
  CODE=$(curl -s -o "$TMP/body" -w '%{http_code}' -b "$jar" -c "$jar" -X "$method" -H 'X-Forum: 1' "$@" "$BASE$path")
  BODY=$(cat "$TMP/body")
}
id_of() { echo "$1" | sed -n 's/.*"id":\([0-9][0-9]*\).*/\1/p'; }
titles() { echo "$1" | grep -o '"title":"[^"]*"' | sed 's/"title"://; s/"//g' | paste -sd, -; }

A="$TMP/alice.jar"
B="$TMP/bob.jar"

start_server

# ---- anonymous access and CSRF header ----
call "$TMP/anon.jar" GET /api/me
eq "anonymous /api/me" "200" "$CODE"
has "anonymous user is null" '"user":null' "$BODY"

CODE=$(curl -s -o /dev/null -w '%{http_code}' -d 'username=mallory&password=secret1' "$BASE/api/register")
eq "POST without X-Forum is refused" "403" "$CODE"

call "$TMP/anon.jar" GET /api/topics
eq "topics need a session" "401" "$CODE"

# ---- accounts ----
call "$A" POST /api/register -d 'username=alice&password=secret1'
eq "register alice" "200" "$CODE"
has "register returns the name" '"user":"alice"' "$BODY"
grep -q '#HttpOnly_' "$A" && ok || bad "session cookie is HttpOnly" "$(cat "$A")"

call "$B" POST /api/register -d 'username=bob&password=secret2'
eq "register bob (collided with alice in the old hash)" "200" "$CODE"

call "$TMP/dup.jar" POST /api/register -d 'username=ALICE&password=whatever1'
eq "duplicate username, other case" "409" "$CODE"
call "$TMP/dup.jar" POST /api/register -d 'username=carol&password=abc'
eq "password too short" "400" "$CODE"

call "$TMP/login.jar" POST /api/login -d 'username=alice&password=wrongpw'
eq "wrong password" "401" "$CODE"
call "$TMP/login.jar" POST /api/login -d 'username=alice&password=secret1'
eq "right password" "200" "$CODE"

call "$A" GET /api/me
has "session identifies alice" '"user":"alice"' "$BODY"

# ---- topics and sorting ----
call "$A" POST /api/topics --data-urlencode 'title=Topics' --data-urlencode 'description=named like a data file'
eq "create topic called Topics" "200" "$CODE"
T1=$(id_of "$BODY")
call "$B" POST /api/topics --data-urlencode 'title=apple' --data-urlencode 'description='
eq "create second topic" "200" "$CODE"
T2=$(id_of "$BODY")
call "$B" POST /api/topics --data-urlencode 'title=TOPICS' --data-urlencode 'description='
eq "duplicate title, other case" "409" "$CODE"
call "$B" POST /api/topics --data-urlencode 'title=' --data-urlencode 'description=x'
eq "empty title" "400" "$CODE"

call "$A" GET "/api/topics?sort=none"
eq "creation order" "Topics,apple" "$(titles "$BODY")"
call "$A" GET "/api/topics?sort=alpha"
eq "A-Z order" "apple,Topics" "$(titles "$BODY")"
call "$A" GET "/api/topics?sort=time"
eq "newest first" "apple,Topics" "$(titles "$BODY")"
call "$A" GET "/api/topics?sort=none"
eq "sorting is per request and does not reorder storage" "Topics,apple" "$(titles "$BODY")"

# ---- posts, with text that used to break the file format ----
call "$A" POST "/api/topics/$T1/posts" --data-urlencode 'text=x; y/z\w <script>alert(1)</script>'
eq "post with hostile text" "200" "$CODE"
P1=$(id_of "$BODY")
call "$B" POST "/api/topics/$T1/posts" --data-urlencode 'text=middle post'
P2=$(id_of "$BODY")
call "$A" POST "/api/topics/$T1/posts" --data-urlencode 'text=last post'
P3=$(id_of "$BODY")
call "$A" POST "/api/topics/$T1/posts" --data-urlencode 'text=last post'
eq "duplicate post text" "409" "$CODE"
call "$A" POST "/api/topics/$T1/posts" --data-urlencode 'text=   '
eq "blank post" "400" "$CODE"
call "$A" POST "/api/topics/99999/posts" --data-urlencode 'text=orphan'
eq "post to a missing topic" "404" "$CODE"

call "$A" GET "/api/topics/$T1"
has "topic view lists the post" 'x; y/z\\w' "$BODY"
BS='\'   # a single backslash, so the expected JSON escapes below can be spelled out
has "markup is escaped in JSON" "${BS}u003cscript${BS}u003ealert(1)${BS}u003c/script${BS}u003e" "$BODY"
hasnt "no raw <script> in the response" '<script>' "$BODY"

# ---- replies and likes ----
call "$B" POST "/api/topics/$T1/posts/$P2/replies" --data-urlencode 'text=first reply'
eq "bob replies" "200" "$CODE"
R1=$(id_of "$BODY")
call "$A" POST "/api/topics/$T1/posts/$P2/replies" --data-urlencode 'text=second reply'
eq "alice replies" "200" "$CODE"

call "$A" POST "/api/topics/$T1/posts/$P2/like"
has "alice likes" '"liked":true,"likes":1' "$BODY"
call "$B" POST "/api/topics/$T1/posts/$P2/like"
has "bob likes too" '"liked":true,"likes":2' "$BODY"
call "$A" POST "/api/topics/$T1/posts/$P2/like"
has "alice's second click removes her like (net zero)" '"liked":false,"likes":1' "$BODY"
call "$A" POST "/api/topics/$T1/like"
has "like a topic" '"liked":true,"likes":1' "$BODY"
call "$A" POST "/api/topics/$T1/posts/$P2/replies/$R1/like"
has "like a reply" '"liked":true,"likes":1' "$BODY"

# ---- ownership: edit and delete ----
call "$A" POST "/api/topics/$T1/posts/$P2/edit" --data-urlencode 'text=hijacked'
eq "alice cannot edit bob's post" "403" "$CODE"
call "$B" POST "/api/topics/$T1/posts/$P2/edit" --data-urlencode 'text=middle post, edited'
eq "bob edits his post" "200" "$CODE"
call "$B" GET "/api/topics/$T1/posts/$P2"
has "edit keeps the text" 'middle post, edited' "$BODY"
has "edit marks the post edited" '"edited":true' "$BODY"
has "edit keeps the replies" '"replyCount":2' "$BODY"
has "edit keeps the likes" '"likes":1' "$BODY"

call "$A" POST "/api/topics/$T1/posts/$P2/delete"
eq "alice cannot delete bob's post" "403" "$CODE"
call "$B" POST "/api/topics/$T1/posts/$P2/delete"
eq "bob deletes his post" "200" "$CODE"
call "$A" GET "/api/topics/$T1"
hasnt "deleted post is gone" 'middle post' "$BODY"
has "neighbour before is intact" 'last post' "$BODY"
has "neighbour after is intact" 'x; y/z' "$BODY"
call "$A" GET "/api/topics/$T1/posts/$P2"
eq "deleted post is 404" "404" "$CODE"

# ---- search ----
call "$A" GET "/api/search?q=LAST"
has "search finds the post, ignoring case" "\"postId\":$P3" "$BODY"
call "$A" GET "/api/search?q=named+like"
has "search finds a topic by description" "\"topicId\":$T1,\"postId\":0" "$BODY"
call "$A" GET "/api/search?q=zzzzzz"
has "search with no match" '"hits":[]' "$BODY"

# ---- bad paths ----
call "$A" GET /api/topics/99999
eq "missing topic" "404" "$CODE"
call "$A" GET /api/topics/abc
eq "non-numeric id" "404" "$CODE"

# ---- static files and headers ----
CODE=$(curl -s -o "$TMP/index.html" -D "$TMP/headers" -w '%{http_code}' "$BASE/")
eq "index page" "200" "$CODE"
has "index is the app" 'app.js' "$(cat "$TMP/index.html")"
has "CSP header is sent" 'Content-Security-Policy' "$(cat "$TMP/headers")"
has "nosniff header is sent" 'X-Content-Type-Options: nosniff' "$(cat "$TMP/headers")"
CODE=$(curl -s -o "$TMP/app.js" -w '%{http_code}' "$BASE/app.js")
eq "app.js is served" "200" "$CODE"
CODE=$(curl -s --path-as-is -o "$TMP/trav" -w '%{http_code}' "$BASE/../../../../etc/passwd")
hasnt "path traversal does not leak files" 'root:' "$(cat "$TMP/trav")"
CODE=$(curl -s -o /dev/null -w '%{http_code}' -H 'Host: evil.example' "$BASE/api/me")
eq "foreign Host header is refused (DNS rebinding)" "403" "$CODE"

# ---- concurrent requests: the lock must keep the forum consistent ----
export BASE B
seq 1 40 | xargs -P 8 -I{} sh -c 'curl -s -o /dev/null -b "$B" -H "X-Forum: 1" --data-urlencode "text=concurrent post {}" "$BASE/api/topics/'"$T2"'/posts"' &
WRITERS=$!
seq 1 40 | xargs -P 8 -I{} sh -c 'curl -s -o /dev/null -b "$B" "$BASE/api/topics?sort=alpha"; curl -s -o /dev/null -b "$B" "$BASE/api/search?q=concurrent"' &
READERS=$!
wait $WRITERS $READERS
call "$B" GET "/api/topics/$T2"
eq "40 concurrent posts were all stored" "40" "$(echo "$BODY" | grep -o '"text":"concurrent post' | wc -l | tr -d ' ')"
eq "and every id is unique" "0" "$(echo "$BODY" | grep -o '"id":[0-9]*' | sort | uniq -d | wc -l | tr -d ' ')"

# ---- sign out ----
call "$A" POST /api/logout
eq "logout" "200" "$CODE"
call "$A" GET /api/topics
eq "session is gone after logout" "401" "$CODE"

# ---- brute-force throttle ----
for i in 1 2 3 4 5; do call "$TMP/bf.jar" POST /api/login -d 'username=carol&password=guess'; done
call "$TMP/bf.jar" POST /api/login -d 'username=carol&password=guess'
eq "login is throttled after repeated failures" "429" "$CODE"

# ---- everything survives a restart; sessions do not ----
stop_server
eq "server exits cleanly on SIGTERM" "0" "$SERVER_EXIT"
start_server

call "$B" GET /api/topics
eq "old session does not survive a restart" "401" "$CODE"

call "$TMP/again.jar" POST /api/login -d 'username=alice&password=secret1'
eq "alice can still log in" "200" "$CODE"
call "$TMP/again.jar" GET "/api/topics?sort=none"
eq "topics persisted" "Topics,apple" "$(titles "$BODY")"
call "$TMP/again.jar" GET "/api/topics/$T1"
has "post with hostile text persisted" 'x; y/z\\w' "$BODY"
has "second post persisted" 'last post' "$BODY"
hasnt "deleted post stayed deleted" 'middle post' "$BODY"
call "$TMP/again.jar" GET "/api/topics/$T2"
eq "concurrent posts persisted" "40" "$(echo "$BODY" | grep -o '"text":"concurrent post' | wc -l | tr -d ' ')"
call "$TMP/again.jar" GET "/api/topics/$T1"
has "topic like persisted" '"likes":1' "$BODY"
call "$TMP/again.jar" POST "/api/topics/$T1/posts" --data-urlencode 'text=after restart'
NEW=$(id_of "$BODY")
[ "$NEW" -gt "$P3" ] 2>/dev/null && ok || bad "ids keep increasing after restart" "new=$NEW last=$P3"

# ---- a second process may not share the data directory ----
"$BIN" --port 0 --data "$TMP/data" --web web > "$TMP/second.out" 2> "$TMP/second.err"
eq "second server on the same data dir fails" "1" "$?"
has "and says why" 'in use' "$(cat "$TMP/second.err")"

stop_server
eq "server exits cleanly again" "0" "$SERVER_EXIT"
eq "server printed no errors" "" "$(cat "$TMP/server.err")"

echo "web_e2e: $PASS passed, $FAILS failed"
[ "$FAILS" -eq 0 ]
