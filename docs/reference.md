# Reference

The web API, the data file format and the security notes. Part of the [README](../README.md).

Contents: [Web API](#web-api) · [Data file](#data-file) · [Security notes](#security-notes)

## Web API

All responses are JSON. Errors look like `{"error": "..."}` with a 400, 401, 403, 404, 409 or 429
status. Every `POST` must send the header `X-Forum: 1` and form-encoded parameters.
Everything except `register`, `login` and `me` needs a session.

| Method and path | Parameters | |
|---|---|---|
| `GET /api/me` | | Current user (or `null`) and input limits |
| `POST /api/register` | `username`, `password` | Creates the account and signs in |
| `POST /api/login` | `username`, `password` | |
| `POST /api/logout` | | |
| `POST /api/password` | `old`, `new` | |
| `GET /api/topics` | `sort` = `time`, `alpha` or `none` | Sorting is per request, not stored |
| `POST /api/topics` | `title`, `description` | |
| `GET /api/topics/:t` | | Topic and its posts |
| `POST /api/topics/:t/like` | | Toggles; returns `{liked, likes}` |
| `POST /api/topics/:t/posts` | `text` | |
| `GET /api/topics/:t/posts/:p` | | Post and its replies |
| `POST /api/topics/:t/posts/:p/like` | | |
| `POST /api/topics/:t/posts/:p/edit` | `text` | Author only (403 otherwise) |
| `POST /api/topics/:t/posts/:p/delete` | | Author only |
| `POST /api/topics/:t/posts/:p/replies` | `text` | |
| `POST /api/topics/:t/posts/:p/replies/:r/like` | | |
| `GET /api/search` | `q` | Topic and post hits |

## Data file

One file, `forum.db`: text, one record per line, tab-separated, with tabs, newlines and backslashes
inside fields escaped, so nothing a user types can break the format. Nothing a user types is ever
used as a file name. It is written to a temporary file and renamed into place, so a crash cannot
leave half a file. If a line cannot be read it is skipped (and the original is kept as
`forum.db.bak`). A file that is not a forum file at all is never overwritten.

```
FORUM<TAB>2
META<TAB>nextId<TAB>terminalSortMode
ACCOUNT<TAB>username<TAB>salt<TAB>hash
TOPIC<TAB>id<TAB>created<TAB>author<TAB>title<TAB>description[<TAB>liker...]
POST<TAB>id<TAB>created<TAB>author<TAB>edited<TAB>text[<TAB>liker...]     (belongs to the TOPIC above)
REPLY<TAB>id<TAB>created<TAB>author<TAB>text[<TAB>liker...]               (belongs to the POST above)
```

## Security notes

This is a learning project, not something to put on the internet as is.

- **No HTTPS.** The web server listens on `127.0.0.1` only by default and refuses requests whose
  `Host` is not a loopback name (protection against DNS rebinding). If you pass `--host` with
  another address it warns you: passwords and session cookies would travel in clear text.
- **Passwords** are hashed with 10,000 rounds of salted SHA-256 and compared in constant time. That
  is far better than plain text but weaker than a purpose-built scheme: use argon2, scrypt or bcrypt
  for anything real. Repeated failed logins are throttled.
- **Sessions** are random 256-bit tokens in `HttpOnly`, `SameSite=Strict` cookies, kept in memory
  (so a server restart signs everyone out) and expiring after a day idle.
- **Cross-site requests** are blocked by requiring the `X-Forum` header on every `POST` and never
  enabling CORS. **Cross-site scripting** is prevented by rendering all user text with
  `textContent`, escaping `<`, `>` and `&` in JSON, and a Content-Security-Policy.
- Control characters (including terminal escape sequences) are rejected on input and replaced when
  shown, so stored text cannot drive someone's terminal.
