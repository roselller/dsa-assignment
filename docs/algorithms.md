# Data structures and algorithms

The hand-written pieces the project is about, and how they work. Part of the [README](../README.md).

Contents: [Overview](#overview) · [HashMap](#hashmap) · [Vector](#vector) · [Why accounts used to overwrite each other](#why-accounts-used-to-overwrite-each-other) · [Merge sort and topic ordering](#merge-sort-and-topic-ordering) · [Substring search (KMP)](#substring-search-kmp) · [Password hashing and SHA-256](#password-hashing-and-sha-256) · [What the main operations cost](#what-the-main-operations-cost)

## Overview

These are hand-written and are the point of the project. Nothing in `src/` uses `std::vector`,
`std::unordered_map` or `std::sort` for forum data.

| What | Where | How it works | Cost |
|---|---|---|---|
| `Vector<T>` | `src/Vector.h` | Dynamic array; capacity doubles when full | push amortised O(1), `removeAt` O(n), index O(1) |
| `HashMap<K,V>` | `src/HashMap.h` | Separate chaining, 64-bit FNV-1a over every byte, doubles at load factor 0.75, rehash relinks nodes so pointers stay valid | find/insert/erase O(1) average |
| `mergeSort` | `src/Sort.h` | Stable top-down merge sort that moves elements instead of copying | O(n log n), O(n) scratch |
| `containsIgnoreCase` | `src/StringSearch.cpp` | Knuth-Morris-Pratt substring search | O(n + m) |
| SHA-256 | `src/Sha256.cpp` | FIPS 180-4, checked against the NIST vectors | |

Where they are used: accounts and web sessions live in a `HashMap`; topic ordering is a
`mergeSort` over topic indices (so each viewer can pick their own order without changing stored
data); search uses KMP; every list in the program is a `Vector`.

## HashMap

`src/HashMap.h`. A key is hashed, reduced to a bucket, and the bucket holds a chain of nodes. New
nodes go on the front of the chain (so `user1`, added after `alice`, comes first). Accounts and web
sessions are stored this way. The array starts
at 16 buckets and doubles when `size / buckets` would pass 0.75.

```mermaid
flowchart LR
    key["key, e.g. 'user1'"] --> hash["FNV-1a<br/>64-bit hash"]
    hash --> index["hash mod<br/>bucketCount"]
    index --> arr

    subgraph arr["bucket array: each bucket holds a chain of nodes"]
        direction TB
        b0["bucket 0: (empty)"] ~~~ b1["bucket 1: user1 → alice"] ~~~ b2["bucket 2: bob"] ~~~ b3["bucket 3: (empty)"]
    end
```

`insertOrAssign` in full. On a resize the existing nodes are *relinked* into the new array, not
copied, so a pointer returned earlier by `find` stays valid until that entry is erased.

```mermaid
flowchart TD
    start["insertOrAssign(key, value)"] --> found{"find(key):<br/>already there?"}
    found -- yes --> assign["replace the stored value"] --> ret
    found -- no --> grow{"no buckets yet, or<br/>(size + 1) * 4 > buckets * 3?"}
    grow -- yes --> rehash["allocate 16 buckets, or double the array<br/>and relink every node into its new bucket"] --> add
    grow -- no --> add["index = hash(key) mod buckets<br/>new node at the head of that chain<br/>size = size + 1"]
    add --> ret(["return the stored value"])
```

## Vector

`src/Vector.h`. Capacity doubles, so `push` is amortised O(1). The old version grew by 4 each
time, which is O(n) per push in the long run.

```mermaid
flowchart TD
    push["push(item)"] --> full{"size == capacity?"}
    full -- no --> store
    full -- yes --> grow["new array: 4 slots the first time, otherwise 2 x capacity<br/>move every element across, free the old array"] --> store["move the item into the next free slot<br/>size = size + 1"]

    remove["removeAt(i)"] --> shift["move elements i+1 ... end one place to the left"]
    shift --> reset["reset the vacated last slot (frees what it held)<br/>size = size - 1"]
```

## Why accounts used to overwrite each other

The original `Map` had two bugs that combined. Both are covered by the unit tests now: one forces
every key into the same bucket and removes from the head, middle and tail of the chain.

```mermaid
flowchart TD
    subgraph old["Original Map"]
        a["charvalue(c) returned -1 for every character that is not a letter"] --> b["'user1' and 'user2' hash to the same bucket"]
        b --> c["push() walked to the end of the chain, appended a new node,<br/>but returned the previous last node"]
        c --> d["the adapter then assigned the new Account into that node:<br/>the earlier user's account was overwritten"]
    end
    subgraph new["HashMap now"]
        e["FNV-1a over all bytes, 64-bit"] --> f["collisions are rare, and chains of any length work"]
        f --> g["insertOrAssign() returns the node it really inserted"]
    end
```

## Merge sort and topic ordering

`src/Sort.h`. Top-down merge sort, O(n log n) time and O(n) scratch space. It *moves* elements
instead of copying them (the old selection sort copied whole topics, with all their posts, on every
swap). It takes from the right run only when strictly smaller, so equal items keep their original
order. The example below has two equal 2s (`2a` came first) and shows `2a` staying ahead of `2b`.

```mermaid
flowchart TD
    s0["5, 2a, 4, 7, 1, 3, 2b, 6"]
    s0 -- "split" --> l["5, 2a, 4, 7"]
    s0 -- "split" --> r["1, 3, 2b, 6"]
    l -- "split" --> ll["5, 2a"]
    l -- "split" --> lr["4, 7"]
    r -- "split" --> rl["1, 3"]
    r -- "split" --> rr["2b, 6"]
    ll -- "merge 5 and 2a" --> m1["2a, 5"]
    lr -- "already in order" --> m2["4, 7"]
    rl -- "already in order" --> m3["1, 3"]
    rr -- "already in order" --> m4["2b, 6"]
    m1 -- "merge" --> ml["2a, 4, 5, 7"]
    m2 -- "merge" --> ml
    m3 -- "merge" --> mr["1, 2b, 3, 6"]
    m4 -- "merge" --> mr
    ml -- "merge: 2a goes first because 2b is not strictly smaller" --> out["1, 2a, 2b, 3, 4, 5, 6, 7"]
    mr -- "merge" --> out
```

Topics are never reordered in memory. `topicOrder` builds a list of *indices* and sorts that, so
each web visitor can pick their own order, a sort needs no write to disk, and the stored `topics_`
vector always stays in order of creation.

```mermaid
flowchart TB
    call["topicOrder(mode)"] --> idx["indices = 0, 1, ... n-1"]
    idx --> mode{"mode"}
    mode -- "None" --> keep["leave as is:<br/>oldest first"]
    mode -- "Time" --> t["mergeSort by created, newest first<br/>(same second: higher id first)"]
    mode -- "Alpha" --> a["mergeSort by<br/>lower-cased title"]
    keep --> ret["return the index list"]
    t --> ret
    a --> ret
```

## Substring search (KMP)

`src/StringSearch.cpp`. Knuth-Morris-Pratt, O(n + m), case-insensitive for ASCII. The prefix table
holds, for each position `i`, the length of the longest proper prefix of `pattern[0..i]` that is also
a suffix of it. When a character does not match, the table says how far to fall back inside the
pattern, so the text is read exactly once and never backed over.

```mermaid
flowchart TD
    start["containsIgnoreCase(text, pattern)"] --> empty{"pattern empty?"}
    empty -- yes --> t1(["true"])
    empty -- no --> table["build the prefix table, O(m)"]
    table --> next["next text character c"]
    next --> mism{"k > 0 and<br/>c != pattern[k]?"}
    mism -- yes --> fall["k = table[k - 1]"] --> mism
    mism -- no --> eq{"c == pattern[k]?"}
    eq -- yes --> inc["k = k + 1"] --> done{"k == length?"}
    done -- yes --> t2(["true"])
    done -- no --> more
    eq -- no --> more{"more text?"}
    more -- yes --> next
    more -- no --> f(["false"])
```

Worked example, pattern `ababc` in text `abababc`. The prefix table is `0 0 1 2 0`.

| Text character | Matched before (k) | Step | k after |
|---|---|---|---|
| a | 0 | matches `a` | 1 |
| b | 1 | matches `b` | 2 |
| a | 2 | matches `a` | 3 |
| b | 3 | matches `b` | 4 |
| a | 4 | `c` expected, no match: fall back to `table[3]` = 2, then `a` matches pattern[2] | 3 |
| b | 3 | matches `b` | 4 |
| c | 4 | matches `c`, k equals the pattern length | 5, found |

## Password hashing and SHA-256

`src/Sha256.cpp`, `src/Account.cpp`. SHA-256 is written by hand and checked against the NIST test
vectors. A password is never stored: only a random salt and the result of 10,000 rounds. For a
username that does not exist, login still does the same amount of hashing, so how long it takes
does not reveal which names are registered.

```mermaid
flowchart TB
    subgraph reg["Register or change password"]
        p1["password"] --> s1["salt = 16 random bytes<br/>(std::random_device), as hex"]
        s1 --> h0["h = SHA-256(salt + password)"]
        h0 --> loop["10,000 times:<br/>h = SHA-256(h + salt + password)"]
        loop --> store["store the salt and hex(h)"]
    end
    subgraph chk["Sign in"]
        p2["password typed"] --> rec["take the account's salt"] --> same["the same 10,000 rounds"]
        same --> cmp{"constantTimeEquals<br/>(computed, stored)"}
        cmp -- equal --> ok(["signed in"])
        cmp -- different --> no(["Unauthorized"])
    end
```

```mermaid
flowchart TD
    m["message bytes"] --> pad["pad: one 0x80 byte, zeros,<br/>then the length in bits as 64-bit big-endian<br/>(total is a multiple of 64 bytes)"]
    pad --> blk["for each 64-byte block"]
    blk --> w["message schedule w[0..63]:<br/>16 words from the block, 48 more from rotations and shifts"]
    w --> rounds["64 rounds over the working variables a to h<br/>using the constants K[0..63]"]
    rounds --> add["add the result into the running state h[0..7]"]
    add --> more{"another block?"}
    more -- yes --> blk
    more -- no --> dig["digest = h[0..7] as 32 bytes"]
```

## What the main operations cost

*T* topics, *P* posts in a topic, *A* accounts, *L* likes on one item, *N* the total size of the data.

| Operation | Cost | Why |
|---|---|---|
| Find an account | O(1) average | `HashMap` |
| Check a password | constant, about 10,000 SHA-256 rounds | deliberately slow |
| Find a topic by id | O(T) | linear scan of `topics_` |
| Find a post by id | O(T + P) | scan for the topic, then its posts |
| Order the topics | O(T log T) | `mergeSort` on indices |
| Add a post | O(T + P) plus a save | find the topic, compare text with its posts |
| Like or unlike | O(L) plus a save | `likedBy` is a `Vector`, searched linearly |
| Search | O(total text) | one KMP pass per title, description and post |
| Save | O(N + A log A) | the whole file is rewritten, accounts sorted by name |

The linear scans and the full rewrite on every change are the main trade-offs: fine for a forum of
this size, and the first things to change (an id index, an append-only log or a database) if it grew.
