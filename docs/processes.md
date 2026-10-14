# Processes and state machines

What happens, step by step, when the program starts and when someone uses it. Part of the [README](../README.md).

Contents: [Sequence diagrams](#sequence-diagrams) · [State machines](#state-machines)

## Sequence diagrams

### Start-up

```mermaid
sequenceDiagram
    autonumber
    actor Dev as You
    participant Main as web_main
    participant F as Forum
    participant S as Storage
    participant W as WebServer

    Dev->>Main: build/forum-web
    Main->>F: Forum::open(dir)
    F->>F: DataLock: exclusive flock<br/>on forum.lock
    break another process holds the lock
        F-->>Main: error: in use
        Main-->>Dev: exit 1
    end
    F->>S: load(forum.db)
    break header is not FORUM 2
        S-->>F: Corrupt
        F-->>Main: error: refusing<br/>to overwrite
        Main-->>Dev: exit 1
    end
    alt no file yet
        S-->>F: Missing: start empty
    else readable
        S-->>F: Snapshot, skipped lines
        opt some lines skipped
            F->>F: keep forum.db.bak,<br/>warn on stderr
        end
    end
    F->>F: build the accounts HashMap<br/>(key = lower-cased name)
    F-->>Main: Forum
    Main->>W: WebServer(forum, web)
    Main->>W: bind(host, port)
    break web folder missing, or cannot listen
        Main-->>Dev: error, exit 1
    end
    W-->>Main: port
    Main-->>Dev: Listening on<br/>http://127.0.0.1:8080
    Main->>W: run() blocks (a watcher thread<br/>waits for SIGINT or SIGTERM)
```

### Signing in

The throttle, the session table and the password check are separate pieces; the web layer owns the
first two and `Forum` owns the last.

```mermaid
sequenceDiagram
    autonumber
    participant B as Browser
    participant W as WebServer
    participant F as Forum
    participant A as Account

    B->>W: POST /api/login<br/>X-Forum: 1, username, password
    W->>W: take the mutex, check X-Forum,<br/>check the throttle
    alt locked out
        W-->>B: 429 Too many attempts
    else
        W->>F: login(username, password)
        F->>F: accounts_.find(lower-cased name)
        alt no such user
            F->>A: create("", password)<br/>same hashing cost, so<br/>timing reveals nothing
            F-->>W: Unauthorized
        else found
            F->>A: checkPassword(password)
            A->>A: 10,000 SHA-256 rounds,<br/>constant-time compare
            A-->>F: true or false
        end
        alt Unauthorized
            W->>W: count the failure<br/>(5 in 60 s locks for 30 s)
            W-->>B: 401 Wrong username<br/>or password
        else Ok
            W->>W: new random token,<br/>old one erased
            W-->>B: 200 user<br/>Set-Cookie sid, HttpOnly,<br/>SameSite=Strict
        end
    end
```

### Writing a post

Every change follows the same path: authenticate, validate, change memory, save, answer.

```mermaid
sequenceDiagram
    autonumber
    participant B as Browser
    participant W as WebServer
    participant F as Forum
    participant S as Storage

    B->>W: POST /api/topics/7/posts<br/>cookie sid, X-Forum: 1, text
    W->>W: take the mutex, check X-Forum,<br/>find the session, refresh lastSeen
    W->>F: addPost(user, 7, text)
    F->>F: find topic 7 (scan by id)
    F->>F: trim, 1 to 2000 bytes,<br/>no control characters
    F->>F: reject a post whose text<br/>is already in this topic
    F->>F: id = nextId++<br/>topic.posts.push(post)
    F->>S: save(everything)
    S->>S: write forum.db.tmp, then rename<br/>it over forum.db (atomic)
    S-->>F: ok
    F-->>W: Ok, new id
    W->>W: build the JSON while<br/>still holding the mutex
    W-->>B: 200 id
    B->>W: GET /api/topics/7 to redraw
```

### Liking

```mermaid
sequenceDiagram
    autonumber
    participant B as Browser
    participant W as WebServer
    participant F as Forum
    participant P as Post

    B->>W: POST .../posts/9/like
    W->>F: likePost(user, 7, 9)
    F->>P: toggleLike(user)
    alt already in likedBy
        P->>P: likedBy.remove(user)<br/>returns false
    else
        P->>P: likedBy.push(user)<br/>returns true
    end
    P-->>F: liked
    F->>F: persist()
    F-->>W: Ok
    W-->>B: liked, new like count
```

### Editing and ownership

The check is against the signed-in user. (The original compared against the topic's starter.)
Editing changes the text in place, so replies, likes, id and creation time all stay.

```mermaid
sequenceDiagram
    autonumber
    participant B as Browser
    participant W as WebServer
    participant F as Forum

    B->>W: POST /api/topics/7/posts/9/edit<br/>text
    W->>F: editPost(user, 7, 9, text)
    alt topic or post not found
        F-->>W: NotFound
        W-->>B: 404
    else post.author is not the signed-in user
        F-->>W: Forbidden
        W-->>B: 403 You can only edit your own posts
    else text empty, too long or has control characters
        F-->>W: Invalid
        W-->>B: 400
    else text identical to what it already is
        F-->>W: Ok, nothing changes
        W-->>B: 200
    else another post in the topic has this text
        F-->>W: Conflict
        W-->>B: 409
    else
        F->>F: post.text = text, post.edited = true
        F->>F: persist()
        F-->>W: Ok
        W-->>B: 200
    end
```

### Search

```mermaid
sequenceDiagram
    autonumber
    participant B as Browser
    participant W as WebServer
    participant F as Forum
    participant K as containsIgnoreCase

    B->>W: GET /api/search?q=deep copy
    W->>F: search(q)
    F->>F: trim q (empty: no hits)
    loop every topic
        F->>K: title, then description
        K-->>F: match or not
        loop every post in it
            F->>K: post text
            K-->>F: match or not
        end
    end
    F-->>W: hits: topic id,<br/>post id (0 = the topic), text
    W->>W: cut each text to 160 bytes<br/>(never inside a UTF-8 character)
    W-->>B: 200 hits
```

### A terminal session

The terminal client calls the same `Forum` methods. At the end of input (Ctrl-D, or a closed pipe)
it throws a private exception that unwinds out of whichever menu is open, so it cannot loop forever.

```mermaid
sequenceDiagram
    autonumber
    actor U as You
    participant T as Terminal
    participant A as Application
    participant F as Forum

    loop until Exit or end of input
        A->>T: clear the screen<br/>(real terminal only)
        A->>U: print the menu
        A->>T: ask("Your choice? ")
        T->>U: show the prompt
        alt a line arrives
            U-->>T: text
            T-->>A: line
            Note over A,F: every pass re-finds its topic or post by id,<br/>and returns if it was deleted
            A->>F: e.g. addPost(user, topicId, text)
            F-->>A: Status
            A->>U: green or red message<br/>(0.8 s pause on a terminal)
        else end of input
            T-->>A: false
            A->>A: throw EndOfInput
            A-->>U: "End of input. Goodbye!"<br/>exit code 0
        end
    end
```

### Saving to disk

The file is replaced as a whole, atomically, so a crash or a full disk can never leave half a file.

```mermaid
flowchart TD
    chg["any change: addPost, likePost, ..."] --> mem["update the in-memory structures"]
    mem --> per["persist(): sort the accounts by name (mergeSort),<br/>write every record to forum.db.tmp"]
    per --> good{"written and flushed OK?"}
    good -- no --> del["delete the temporary file"]
    good -- yes --> mv["rename forum.db.tmp over forum.db"]
    mv --> okmv{"rename OK?"}
    okmv -- no --> del
    okmv -- yes --> done(["Status::Ok"])
    del --> fail(["Status::Internal: the change stays in memory,<br/>the old file is intact"])
```

## State machines

### A web session

```mermaid
stateDiagram-v2
    [*] --> Active: sign in or register<br/>(a brand-new random token)
    Active --> Active: any request with the cookie<br/>(lastSeen is refreshed)
    Active --> [*]: sign out
    Active --> [*]: signing in again<br/>(the old token is erased)
    Active --> Expired: idle for more than 24 hours
    Expired --> [*]: removed on its next use or by a sweep
    Active --> [*]: the server restarts<br/>(sessions live in memory only)
```

### The login throttle

One entry per remote address and username. A failure that arrives after the 60 s window has passed
starts the count again at 1.

```mermaid
stateDiagram-v2
    [*] --> Open
    Open --> Counting: a failed login
    Counting --> Counting: another failed login<br/>(fewer than 5 inside the 60 s window)
    Counting --> Locked: the 5th failure inside the window
    Counting --> Open: a successful login
    Locked --> Locked: an attempt (answered 429, password not checked)
    Locked --> Open: 30 seconds pass
```

### Reading the data file

Lines that cannot be parsed are skipped and counted rather than aborting the load. A `POST` needs a
good `TOPIC` before it, and a `REPLY` a good `POST`, so one damaged line cannot attach later lines
to the wrong parent.

```mermaid
stateDiagram-v2
    [*] --> NoTopic
    NoTopic --> TopicOnly: TOPIC parsed
    NoTopic --> NoTopic: TOPIC damaged, or a POST or REPLY with no parent (skipped)
    TopicOnly --> TopicOnly: TOPIC parsed, or a REPLY with no post (skipped), or a damaged POST (skipped)
    TopicOnly --> NoTopic: TOPIC damaged
    TopicOnly --> TopicAndPost: POST parsed
    TopicAndPost --> TopicAndPost: POST parsed, or REPLY parsed, or a damaged REPLY (skipped)
    TopicAndPost --> TopicOnly: TOPIC parsed, or a damaged POST (skipped)
    TopicAndPost --> NoTopic: TOPIC damaged
```
