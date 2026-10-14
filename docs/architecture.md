# Architecture

How the forum is put together and who uses it. Part of the [README](../README.md).

Contents: [Components](#components) · [Project layout](#project-layout) · [Who uses it](#who-uses-it) · [Structure](#structure) · [A web request, and concurrency](#a-web-request-and-concurrency)

## Components

```mermaid
flowchart TB
    browser["Browser<br/>web/: index.html, app.js, app.css"]
    you["You at a keyboard"]

    subgraph webproc["forum-web  (web_main.cpp, WebServer.cpp)"]
        http["cpp-httplib<br/>thread pool"]
        ws["WebServer<br/>routes, sessions, CSRF check,<br/>login throttle"]
    end
    subgraph cliproc["forum  (main.cpp, Application.cpp, Terminal.cpp)"]
        tty["Terminal<br/>input, output, colour"]
        app["Application<br/>menus"]
    end
    subgraph core["Shared core (src/)"]
        forum["Forum<br/>all the rules"]
        model["Model<br/>Comment, Post, Topic, Account"]
        ds["Vector, HashMap, mergeSort,<br/>KMP search, SHA-256"]
        storage["Storage<br/>forum.db and the directory lock"]
    end
    file[("data/forum.db")]

    browser -- "JSON over HTTP<br/>cookie + X-Forum header" --> http --> ws --> forum
    you --> tty --> app --> forum
    forum --> model
    forum --> ds
    forum --> storage --> file
```

`Forum` knows nothing about HTTP or terminals, so the rules (who may edit what, one like per
user, unique names) live in one place and are unit-tested directly.

## Project layout

```
src/        the C++ code
  Vector.h HashMap.h Sort.h StringSearch.* Sha256.*     data structures and algorithms
  Comment.* Post.h Topic.h Account.*                    the model
  Storage.*   forum.db reader/writer and the directory lock
  Forum.*     the forum's rules (shared by both front-ends)
  WebServer.* Json.h web_main.cpp                       web front-end (forum-web)
  Terminal.* Application.* main.cpp                     terminal front-end (forum)
web/        the browser app: index.html, app.js, app.css
tests/      unit tests, end-to-end scripts, and the browser check
third_party/httplib.h    cpp-httplib v0.58.0 (MIT), the only third-party code
```

## Who uses it

### Use cases

There are no administrators or moderators: nobody can edit or delete another person's content, and
there is no way to delete a topic.

```mermaid
flowchart TB
    visitor(["Visitor<br/>(not signed in)"])
    user(["Signed-in user"])
    author(["Author of a post"])
    author -. "is also a" .-> user

    subgraph before["Before signing in"]
        reg(["Register"]) ~~~ login(["Sign in"])
    end
    subgraph signed["Once signed in"]
        logout(["Sign out"]) ~~~ pw(["Change password"]) ~~~ browse(["Browse topics<br/>newest, A-Z or oldest"]) ~~~ find(["Search topics and posts"]) ~~~ newtopic(["Start a topic"]) ~~~ newpost(["Write a post"]) ~~~ reply(["Reply to a post"]) ~~~ like(["Like or unlike a<br/>topic, post or reply"])
    end
    subgraph own["On their own posts only"]
        edit(["Edit a post"]) ~~~ del(["Delete a post"])
    end

    visitor --> before
    user --> signed
    author --> own
```

### User flow in the web app

Each box is a screen of the single-page app (`web/app.js`); the labels are its URL hashes.

```mermaid
flowchart TB
    start(["Open the site"]) --> authed{"Signed in?"}
    authed -- no --> login["#/login<br/>Sign in or Create account"]
    authed -- yes --> topics
    login -- "success" --> topics["#/<br/>Topics: sort, search,<br/>start a topic"]
    topics -- "click a topic" --> topic["#/topic/ID<br/>Topic and its posts:<br/>write a post, like"]
    topic -- "open a post" --> post["#/topic/ID/post/ID<br/>Replies: write a reply, like<br/>Edit and Delete on your own post"]
    post -- "delete the post" --> topic
    topics -- "search box (in the header of every page)" --> search["#/search/QUERY<br/>Matching topics and posts"]
    search -- "topic hit" --> topic
    search -- "post hit" --> post
    topics -. "Password link" .-> pw["#/password<br/>Change password"]
    pw -- "saved" --> topics
    topics -- "Sign out" --> login
    post -- "session expired (401)" --> login
```

### Screens

| Sign in | A post with replies | Phone |
|---|---|---|
| ![Sign in](login.png) | ![A post with replies](post.png) | ![On a phone](mobile.png) |

## Structure

### Classes

The model is three small classes and an account. `Post` and `Topic` extend `Comment` so that
likes, ids, authors and timestamps are written once.

```mermaid
classDiagram
    direction TB
    class Comment {
        +int id
        +string author
        +string text
        +time_t created
        +Vector~string~ likedBy
        +likes() int
        +isLikedBy(user) bool
        +toggleLike(user) bool
        +createdString() string
    }
    class Post {
        +Vector~Comment~ replies
        +bool edited
    }
    class Topic {
        +string description
        +Vector~Post~ posts
        +title() string
    }
    class Account {
        +string username
        +string salt
        +string hash
        +create(username, password) Account
        +checkPassword(password) bool
        +setPassword(password)
    }
    class Forum {
        -HashMap accounts_
        -Vector topics_
        -int nextId_
        +open(dataDir) Forum
        +registerUser() Status
        +login() Status
        +createTopic() Status
        +topicOrder(mode) Vector
        +addPost() Status
        +editPost() Status
        +deletePost() Status
        +addReply() Status
        +likePost() Status
        +search(query) Vector
    }
    Comment <|-- Post : is a
    Comment <|-- Topic : is a
    Topic "1" *-- "0..*" Post : posts
    Post "1" *-- "0..*" Comment : replies
    Forum "1" *-- "0..*" Topic : topics_
    Forum "1" *-- "0..*" Account : accounts_
```

### Where each data structure is used

```mermaid
flowchart TB
    forum["Forum"]
    forum -- "accounts_" --> accounts["HashMap of Account<br/>key = lower-cased username"]
    forum -- "topics_" --> topics["Vector of Topic<br/>in order of creation"]
    topics --> topic["Topic"]
    topic -- "posts" --> posts["Vector of Post"]
    posts --> post["Post"]
    post -- "replies" --> replies["Vector of Comment"]
    topic -- "likedBy" --> likers["Vector of string<br/>usernames, no duplicates"]
    post -- "likedBy" --> likers
    replies -- "each reply's likedBy" --> likers

```

Short-lived structures and the web layer's own tables:

```mermaid

flowchart LR
    f1["Forum::topicOrder()"] --> o["Vector of int<br/>topic indices, sorted by mergeSort"]
    f2["Forum::persist()"] --> a["Vector of Account<br/>copied, merge-sorted by name"]
    w1["WebServer"] -- "sessions" --> s["HashMap of Session<br/>key = random 256-bit token"]
    w1 -- "throttles" --> t["HashMap of Throttle<br/>key = address and username"]
```

### Data model on disk

The same shape as `forum.db` (see [Data file](reference.md#data-file)). Likes are not a separate table: each
topic, post and reply carries the list of usernames who like it. A post belongs to the topic whose
`TOPIC` line is above it in the file, and a reply to the `POST` line above it.

```mermaid
erDiagram
    ACCOUNT ||--o{ TOPIC : "starts"
    ACCOUNT ||--o{ POST : "writes"
    ACCOUNT ||--o{ REPLY : "writes"
    TOPIC ||--o{ POST : "contains"
    POST ||--o{ REPLY : "has"
    ACCOUNT }o--o{ TOPIC : "likes"
    ACCOUNT }o--o{ POST : "likes"
    ACCOUNT }o--o{ REPLY : "likes"

    ACCOUNT {
        string username PK "unique, ignoring case"
        string salt "16 random bytes as hex"
        string hash "10000 rounds of SHA-256"
    }
    TOPIC {
        int id PK
        int created "epoch seconds"
        string author FK
        string title "unique, ignoring case"
        string description
    }
    POST {
        int id PK
        int created
        string author FK
        bool edited
        string text
    }
    REPLY {
        int id PK
        int created
        string author FK
        string text
    }
```

## A web request, and concurrency

The pipeline every request goes through, in order. "Host OK?" means the `Host` header names a loopback
address (`localhost`, `127.0.0.1` or `[::1]`) when the server is listening on loopback; "valid sid
cookie?" means a known token that has not been idle for over 24 hours; the mutex is released when
the response has been built. Every response, including errors and static
files, also carries `Content-Security-Policy`, `X-Content-Type-Options: nosniff`,
`X-Frame-Options: DENY`, `Referrer-Policy: no-referrer` and `Cache-Control: no-store`.

```mermaid
flowchart TD
    req["HTTP request"] --> host{"Host OK?"}
    host -- "no: not a loopback name" --> r403a["403, blocks DNS rebinding"]
    host -- yes --> static{"file in web/?"}
    static -- yes --> file["serve the file"]
    static -- no --> route{"/api route?"}
    route -- no --> r404["404"]
    route -- "yes: take the global mutex" --> csrf{"POST without<br/>X-Forum: 1?"}
    csrf -- yes --> r403b["403, blocks cross-site forgery"]
    csrf -- no --> needs{"needs a session?"}
    needs -- yes --> sess{"valid sid cookie?"}
    sess -- no --> r401["401 Sign in required"]
    sess -- yes --> handler
    needs -- no --> handler["run the handler (call Forum, build the JSON),<br/>release the mutex, send the response"]
```

There are two levels of locking. Threads inside the web server share one mutex. Separate
programs are kept apart by a file lock, because each one holds all the data in memory and rewrites
the file: two of them at once would silently overwrite each other's changes.

```mermaid
flowchart LR
    subgraph proc["forum-web process"]
        t1["worker thread 1"] --> mu(("mutex"))
        t2["worker thread 2"] --> mu
        t3["worker thread N"] --> mu
        mu --> state["Forum, sessions,<br/>throttles (memory)"]
    end
    state --> db[("data/forum.db")]
    state --> lockf[/"data/forum.lock<br/>(flock, held while running)"/]
    cli["terminal client or<br/>a second forum-web"] -. "refused" .-> lockf
```
