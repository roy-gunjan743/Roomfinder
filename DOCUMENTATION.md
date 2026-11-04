# RoomFinder: Complete Function and Logic Guide

Nov 4, 2026 - @gunjan roy

## Overview

RoomFinder solves one problem: a student arrives on campus and finds that a lecture has been moved to another room. The student types a subject, a teacher or a class name and immediately sees the assigned room, the building and simple directions. The system also keeps a history of room changes and refuses any change that would put two classes in the same room at the same time.

The project has a **backend** written in C and a **frontend** written in HTML, CSS and JavaScript. The two talk through a small set of web addresses. All data and every rule live in the C program, while the page only sends requests and draws the answers.

### How the brief maps to the code

| Requirement in the project brief | Where it is implemented |
| --- | --- |
| Search by subject, faculty or class | `apiClasses` using `contains` |
| Show the assigned room with simple directions | The directions text built inside `apiClasses` |
| Room-change history | `Change` struct, `apiChange` writes it and `apiHistory` reads it |
| Flag conflicting room allocations | `findConflict`, used by `apiAdd`, `apiChange` and the conflict list |

### Files in the repository

| File | Role |
| --- | --- |
| `server.c` | The C backend as a web server. The main program, documented in detail below |
| `index.html` | The frontend: page layout, styling and the JavaScript that calls the server |
| `roomfinder.c` | A console (text menu) version of the same backend, kept as an alternative |
| `.gitignore` | Stops the compiled programs from being uploaded |

### Running it

1. Compile: `gcc server.c -o server`
2. Start from the folder that holds `index.html`: `./server`
3. Open `http://localhost:8080` in a browser.

### How the parts fit together

```
+------------------------------+
| BROWSER  (index.html)        |
|  Page:   HTML + CSS layout   |
|  Script: render(), changeRoom(), add form
+--------------+---------------+
               |  requests: GET /, GET /api/classes, GET /api/history,
               |            POST /api/add, POST /api/change
               v
+------------------------------+
| SERVER  (server.c, port 8080)|
|  main + handle: open port, read request, pick a route
|              |
|              v
|  API handlers: apiClasses, apiHistory, apiAdd, apiChange, serveIndex
|              |
|      +-------+--------+
|      v                v
|  Core rules        Memory
|  contains          classes[100]
|  findConflict      history[100]
+--------------+---------------+
               |  replies: JSON data, or the page itself
               v
            BROWSER
```

The browser sends one of five kinds of request. The server's `handle` function routes it to a handler, the handler uses the two core rules and the in-memory arrays, and the answer travels back as JSON data or as the page itself.

### How to read the rest of this document

The sections follow the program from the bottom up: the data first, then the rules, the helper functions, the request handlers, the server start-up, the page, a set of complete walkthroughs, the console version, and finally the limitations.

## Data structures and global state

All data lives in two fixed-size global arrays inside `server.c`, with one counter for each. There is no database, no file and no `malloc` for the records, so the data disappears when the server stops. This choice keeps the program short and removes a whole class of memory bugs, at the price of a hard limit of 100 classes.

### The `Class` struct

One `Class` is one timetable entry: a subject taught by a faculty member to a class group, in a room, at a time. All six fields are text (`char` arrays).

| Field | Array size | Longest text it holds | Example | Meaning |
| --- | --- | --- | --- | --- |
| `subject` | 40 | 39 characters | `Physics` | Name of the subject |
| `faculty` | 40 | 39 characters | `Dr. Das` | Teacher's name |
| `cls` | 20 | 19 characters | `CSE-3A` | Student group or section |
| `room` | 10 | 9 characters | `204` | Room number or label |
| `building` | 30 | 29 characters | `Science Block` | Building name |
| `slot` | 20 | 19 characters | `Mon-11AM` | Day and time of the lecture |

Three design decisions are worth knowing:

- **Every array is one byte larger than the text it holds.** C strings end with a hidden `'\0'` byte, so an array of 40 stores at most 39 visible characters. The input functions always cut text at `size - 1` to protect this byte.
- **The field is called `cls`, not `class`.** `class` is a reserved word in C++, so `cls` keeps the file compilable by both compilers and avoids confusion.
- **`room` and `slot` are text, not numbers.** Real campuses have rooms like `A-12` or `LAB2`, and slots like `Mon-10AM` cannot be a single number. The cost is that two slots count as equal only when their text matches (ignoring upper and lower case), so `Mon-10AM` and `Mon 10AM` would be treated as different slots.

### The `Change` struct

One `Change` is one entry in the room-change history: it records that a class moved from `oldRoom` to `newRoom` at a given time.

| Field | Array size | Example | Meaning |
| --- | --- | --- | --- |
| `subject` | 40 | `Programming` | Subject that moved |
| `cls` | 20 | `CSE-3B` | Class group that moved |
| `oldRoom` | 10 | `305` | Room before the move |
| `newRoom` | 10 | `306` | Room after the move |
| `when` | 32 | `04 Nov 2026, 08:46` | Local date and time of the move |

The history stores **copies of the text**, not a pointer or an index into `classes`. That keeps each history line correct even if the class is moved again later, because the old line still says what was true at that moment.

### Global variables

| Name | Type | Starts as | Role |
| --- | --- | --- | --- |
| `MAX` | constant `100` | n/a | Capacity of both arrays |
| `PORT` | constant `8080` | n/a | TCP port the server listens on |
| `classes[MAX]` | array of `Class` | all zeros | The timetable |
| `history[MAX]` | array of `Change` | all zeros | The room-change log |
| `nClasses` | `int` | 0 (then 3 after `seed`) | How many `classes` entries are in use |
| `nHistory` | `int` | 0 | How many `history` entries are in use |
| `out[65536]` | `char` array | zeros | Output buffer where each JSON reply is built |
| `olen` | `int` | 0 | How many bytes of `out` are filled |

Global variables in C are set to zero before `main` runs, which is why the arrays and counters need no explicit setup.

**Counters double as insertion points.** `nClasses` is both the number of classes and the index of the next free slot, so adding a class is just `classes[nClasses++] = c;`. The same holds for `nHistory`.

**A class's id is its array index.** The API sends `"id":1` for the second class, and the browser sends that number back when changing a room. This is safe only because classes are never deleted. If a delete feature is added later, the indexes would shift and the ids would need to become stable numbers instead.

**What happens when an array is full.** Adding a class at 100 entries is refused with the message `Database full.` Room changes keep working at 100 history entries, but the move is no longer recorded: the code only writes a history line when `nHistory < MAX`, and changes the room either way.

**Size of the output buffer.** One class takes roughly 250 to 300 bytes of JSON, so 100 classes need about 30 KB. The 65,536-byte buffer leaves room to spare, and `put` clamps the length so an overflow cannot write past the end.

## Core logic functions

Three functions hold the actual rules of RoomFinder: `contains` (search matching), `findConflict` (double-booking detection) and `seed` (starting data). Everything else in the program is plumbing that moves data to and from the browser.

### `int contains(const char *text, const char *key)`

**Job:** answer "does `text` contain `key` somewhere inside it, ignoring upper and lower case?" It returns 1 for yes and 0 for no. Search uses it on the subject, faculty and class fields.

```c
int contains(const char *text, const char *key) {
    char a[60], b[60];
    int i;
    for (i = 0; text[i] && i < 59; i++) a[i] = tolower((unsigned char)text[i]);
    a[i] = '\0';
    for (i = 0; key[i] && i < 59; i++) b[i] = tolower((unsigned char)key[i]);
    b[i] = '\0';
    return strstr(a, b) != NULL;
}
```

**Step by step:**

1. Two local arrays `a` and `b` (60 bytes each) hold lowercase copies of the text and the key. The originals are never changed, because both parameters are `const`.
2. The first loop copies `text` into `a` one character at a time, converting each to lowercase with `tolower`. It stops at the end of the string (`text[i]` is `'\0'`) or after 59 characters, whichever comes first.
3. `a[i] = '\0'` closes the copy as a proper C string. The same two steps are repeated for `key` into `b`.
4. `strstr(a, b)` looks for `b` as a substring of `a`. It returns a pointer to the first match, or `NULL` when there is none. Comparing it with `NULL` turns that into 1 or 0.

**Why it is written this way:**

- Lowercasing both sides first is the simplest way to ignore case in standard C, since `strstr` itself is case-sensitive.
- The cast `(unsigned char)` before `tolower` is required by the C standard. A plain `char` can be negative for accented characters, and passing a negative value to `tolower` is undefined behaviour.
- 60 bytes is safe because the longest stored field is 39 characters and the search box is limited to 39 characters. The `i < 59` check is a second safety net.

**Worked examples:**

| Call | Result | Why |
| --- | --- | --- |
| `contains("Dr. Roy", "roy")` | 1 | Case is ignored |
| `contains("Physics", "sic")` | 1 | Matches anywhere in the text, not only at the start |
| `contains("Physics", "chem")` | 0 | `chem` does not appear |
| `contains("CSE-3A", "")` | 1 | An empty key is found in every string, so an empty search box lists every class |

**Limits:** only plain English letters are case-folded, so accented or non-Latin letters must match exactly. It matches the characters typed, not words, so searching `a` matches nearly everything.

### `int findConflict(const char *room, const char *slot, int skip)`

**Job:** find another class that already uses the same room at the same time slot. It returns the **index** of the first clashing class, or **-1** when there is no clash. This single function is the whole "flag conflicting room allocations" feature of the project brief.

```c
int findConflict(const char *room, const char *slot, int skip) {
    for (int i = 0; i < nClasses; i++)
        if (i != skip && strcasecmp(classes[i].room, room) == 0 &&
            strcasecmp(classes[i].slot, slot) == 0)
            return i;
    return -1;
}
```

**Step by step:**

1. A loop visits every stored class from index 0 to `nClasses - 1`. This is a linear search, which is plenty fast for at most 100 entries.
2. For each class, three conditions must all be true (joined by `&&`): it is not the class being checked (`i != skip`), its room equals `room`, and its slot equals `slot`.
3. `strcasecmp` compares two strings ignoring case and returns 0 when they are equal. That is why the tests read `== 0`. So `204` and `204` match, and `lab2` matches `LAB2`.
4. On the first class that satisfies all three, the function returns its index immediately. If the loop ends with no match, it returns -1.

**The `skip` parameter** exists because there are two different situations:

- **Adding a new class:** the new class is not in the array yet, so nothing needs skipping. The caller passes **-1**, which never equals a real index.
- **Moving or checking an existing class:** the class is already in the array and would clash with itself (same room, same slot). The caller passes that class's own index so it is ignored.

**Why it returns an index and not just yes or no:** the caller uses the index to read the clashing class and build a helpful message, for example "room 101 at Mon-10AM is used by Mathematics (CSE-3A)".

**Worked example with the starting data:**

| Question | Call | Result | Outcome |
| --- | --- | --- | --- |
| Move Programming (index 2) into room 101 at Mon-10AM | `findConflict("101", "Mon-10AM", 2)` | 0 | Index 0 is Mathematics, which holds 101 at Mon-10AM, so the move is refused |
| Move Programming into room 102 | `findConflict("102", "Mon-10AM", 2)` | -1 | Nobody uses 102 then, so the move is allowed |
| Add a class in room 101 at Mon-11AM | `findConflict("101", "Mon-11AM", -1)` | -1 | Same room, different time, so no clash |

**Limits to be aware of:**

- It checks only **room double-booking**. It does not notice a teacher (or a student group) scheduled in two different rooms at the same time.
- It returns only the **first** clash, even if several exist.
- Slots must be typed identically. `Mon-10AM` and `Mon 10AM` are different text, so they never clash.
- Because both adding and changing check this function first, the API can never create a clash. The red conflict banner in the page therefore stays empty in normal use. It is still kept as a safety net for future features such as bulk import, where bad data could slip in.

### `void seed(void)`

**Job:** fill the timetable with three sample classes when the server starts, so the page has something to show and test.

```c
void seed(void) {
    Class s[] = {
        {"Mathematics", "Dr. Roy", "CSE-3A", "101", "Main Block",    "Mon-10AM"},
        {"Physics",     "Dr. Das", "CSE-3A", "204", "Science Block", "Mon-11AM"},
        {"Programming", "Ms. Sen", "CSE-3B", "305", "Main Block",    "Mon-10AM"},
    };
    for (int i = 0; i < 3; i++) classes[nClasses++] = s[i];
}
```

**How it works:**

1. The local array `s` is written with positional initializers. Each `{...}` fills the six `Class` fields in the order they are declared: subject, faculty, class, room, building, slot.
2. The loop copies each entry into the global `classes` array. Assigning one struct to another copies all of its bytes, so no `strcpy` calls are needed.
3. `nClasses++` both picks the next free index and increases the count, so after the loop `nClasses` is 3.

**The sample data:**

| Index | Subject | Faculty | Class | Room | Building | Slot |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Mathematics | Dr. Roy | CSE-3A | 101 | Main Block | Mon-10AM |
| 1 | Physics | Dr. Das | CSE-3A | 204 | Science Block | Mon-11AM |
| 2 | Programming | Ms. Sen | CSE-3B | 305 | Main Block | Mon-10AM |

The data is chosen on purpose: Mathematics and Programming share the slot Mon-10AM but sit in different rooms. That makes it easy to demonstrate the conflict rule by trying to move one of them into the other's room.

`seed` is called once, at the start of `main`, before the server accepts any request.

## Helper functions

These six small functions do the "web" work: building JSON text, reading values out of a request, and sending bytes back to the browser. They are all marked `static`, which in C means the function can only be used inside `server.c`.

A typical reply is built in three moves: set `olen = 0`, call `put` and `putStr` many times to write JSON into the global buffer `out`, then call `sendResp(fd, "application/json", out, olen)` to send it.

### `static void put(const char *fmt, ...)`

**Job:** work like `printf`, but append the text to the global output buffer `out` instead of printing to the screen.

```c
static void put(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    olen += vsnprintf(out + olen, sizeof(out) - olen, fmt, ap);
    va_end(ap);
    if (olen >= (int)sizeof(out)) olen = sizeof(out) - 1;
}
```

**Step by step:**

1. The `...` in the parameter list means the function accepts any number of extra arguments, like `printf`. `va_list`, `va_start` and `va_end` (from `<stdarg.h>`) are the standard tools for reading them.
2. `out + olen` is the address of the first unused byte in the buffer, and `sizeof(out) - olen` is how many bytes are still free.
3. `vsnprintf` writes the formatted text there, never more than the free space. It returns how many characters the full text **would** need, which is added to `olen`.
4. If the text did not fit, `olen` could now point past the end of the buffer. The last line pulls it back to the last valid position, so later calls cannot write outside the array.

**Why it exists:** it lets the handlers write JSON in small readable pieces, such as `put(",\"conflict\":%s}", flag)`, without tracking positions by hand.

### `static void putStr(const char *s)`

**Job:** write `s` into the buffer as a valid JSON string, wrapped in double quotes, with special characters made safe.

```c
static void putStr(const char *s) {
    put("\"");
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') put("\\%c", *s);
        else if ((unsigned char)*s < 32) put(" ");
        else put("%c", *s);
    }
    put("\"");
}
```

**Step by step:**

1. Write the opening quote.
2. Visit each character until the end of the string.
3. A double quote or a backslash would break the JSON, so each is written with a backslash in front (`"` becomes `\"`).
4. Control characters (anything below ASCII 32, such as a new line or a tab) are not allowed raw inside JSON strings, so they are replaced with a space.
5. Every other character is copied unchanged. Write the closing quote at the end.

**Why it matters:** the text comes from users. If someone types `Dr. "Bob" Roy` as a faculty name and it were written as-is, the JSON would be broken and the page would show nothing. It also blocks text from escaping the string and injecting extra JSON.

**Detail worth knowing:** the cast `(unsigned char)` is essential. Letters outside English, such as `é` or Bengali characters, are stored as bytes above 127. With a plain signed `char` those bytes look negative, would count as "below 32", and would all be replaced by spaces. With the cast they stay intact.

| Input text | Written to the buffer |
| --- | --- |
| `Physics` | `"Physics"` |
| `Dr. "Bob"` | `"Dr. \"Bob\""` |
| `C:\lab` | `"C:\\lab"` |

### `static void getParam(const char *s, const char *name, char *o, int size)`

**Job:** find the value of one named parameter inside a URL-encoded string and copy it, decoded, into the buffer `o`. The string looks like `id=2&room=306` (a POST body) or `q=phys` (the part of a URL after the `?`).

```c
static void getParam(const char *s, const char *name, char *o, int size) {
    int nl = strlen(name);
    o[0] = 0;
    while (*s) {
        if (strncmp(s, name, nl) == 0 && s[nl] == '=') {
            s += nl + 1;
            int j = 0;
            while (*s && *s != '&' && j < size - 1) {
                if (*s == '+') { o[j++] = ' '; s++; }
                else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
                    char h[3] = { s[1], s[2], 0 };
                    o[j++] = (char)strtol(h, NULL, 16);
                    s += 3;
                } else o[j++] = *s++;
            }
            o[j] = 0;
            return;
        }
        s = strchr(s, '&');
        if (!s) return;
        s++;
    }
}
```

**Step by step:**

1. `nl` is the length of the wanted name, and `o[0] = 0` sets the result to an empty string first. If the parameter is missing, the caller simply receives an empty string, which is how the handlers detect "not provided".
2. The outer loop looks at the start of each `key=value` pair. A pair matches when it begins with the name **and** the next character is `=`. The `=` check stops `room` from matching a longer name such as `roomy`.
3. After a match, `s` jumps past `name=` to the first character of the value. The inner loop then copies characters until the next `&`, the end of the string, or the buffer being full (`j < size - 1`, leaving space for the closing zero).
4. Three cases decode the value. A `+` becomes a space. A `%` followed by two hex digits (such as `%2F`) becomes the single byte with that hex value, computed by `strtol(h, NULL, 16)`. Anything else is copied as is.
5. If the pair did not match, `strchr(s, '&')` moves to the next `&`. If there is none, the parameter is absent and the function returns. Otherwise `s++` skips the `&` and the loop tries the next pair.

**Why decoding is needed:** the browser sends form data URL-encoded, so `Science Block` arrives as `Science+Block` and `1/2` arrives as `1%2F2`. Without decoding, the stored text would be wrong.

**The `size` argument also enforces the field limits.** Callers pass `sizeof` of each struct field, so an over-long value is cut to fit the field, never overflowing it.

| Call | Result in `o` |
| --- | --- |
| `getParam("id=2&room=306", "room", ...)` | `306` |
| `getParam("subject=Data+Structures", "subject", ...)` | `Data Structures` |
| `getParam("room=1%2F2", "room", ...)` | `1/2` |
| `getParam("id=2", "room", ...)` | empty string |

### `static void writeAll(int fd, const char *buf, int len)`

**Job:** send exactly `len` bytes of `buf` through the network connection `fd`, even if the operating system accepts them in several pieces.

```c
static void writeAll(int fd, const char *buf, int len) {
    while (len > 0) {
        int w = write(fd, buf, len);
        if (w <= 0) return;
        buf += w; len -= w;
    }
}
```

`write` on a socket is allowed to send fewer bytes than asked, which happens easily with a large page such as `index.html`. The loop handles that: after each call it moves the pointer forward by `w` bytes and reduces the remaining length by `w`, until nothing is left. If `write` returns 0 or a negative number (the browser closed the connection), the function gives up quietly, since there is nobody left to answer.

### `static void sendResp(int fd, const char *type, const char *body, int len)`

**Job:** send one complete HTTP response: a header, then the body.

```c
static void sendResp(int fd, const char *type, const char *body, int len) {
    char head[300];
    int h = snprintf(head, sizeof head,
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: *\r\n"
        "Connection: close\r\n\r\n", type, len);
    writeAll(fd, head, h);
    writeAll(fd, body, len);
}
```

**What each header line does:**

| Header | Purpose |
| --- | --- |
| `HTTP/1.1 200 OK` | Status line: the request succeeded |
| `Content-Type` | Tells the browser how to read the body: `application/json` for data, `text/html; charset=utf-8` for the page |
| `Content-Length` | Number of body bytes, so the browser knows where the reply ends |
| `Access-Control-Allow-Origin: *` | Lets a page opened straight from disk (`file://`) call this server. Without it the browser blocks the reply |
| `Access-Control-Allow-Headers: *` | Allows the extra headers a browser may announce in its pre-check request |
| `Connection: close` | The server closes the connection after each reply. This avoids keeping connections open, which keeps the server simple |

The header ends with a blank line (`\r\n\r\n`), which is how HTTP marks the start of the body. `snprintf` returns the header length, so exactly those bytes are sent, followed by the body.

**Simplification:** the status is always `200 OK`, even for "Not found" and for failed actions. Success or failure of an action is reported inside the JSON (`"ok":true` or `"ok":false`).

### `static void replyJson(int fd, int ok, const char *err)`

**Job:** send a short success or error answer for actions that return no data (adding a class, changing a room).

```c
static void replyJson(int fd, int ok, const char *err) {
    olen = 0;
    if (ok) put("{\"ok\":true}");
    else { put("{\"ok\":false,\"error\":"); putStr(err); put("}"); }
    sendResp(fd, "application/json", out, olen);
}
```

It resets the buffer, writes either `{"ok":true}` or `{"ok":false,"error":"..."}`, and sends it. The error message goes through `putStr` so quotes inside it cannot break the JSON. The page reads `ok` to decide whether to refresh the list or show the message in red.

## API handler functions

Each handler answers one kind of request from the browser. They combine the core logic with the helper functions: read the input, apply the rules, write the reply. The routing table in `handle` (next section) decides which handler runs.

| Route | Method | Handler | Input | Output |
| --- | --- | --- | --- | --- |
| `/` | GET | `serveIndex` | none | The `index.html` page |
| `/api/classes` | GET | `apiClasses` | `q` (search text, optional) | Matching classes and the list of conflicts |
| `/api/history` | GET | `apiHistory` | none | All room changes, oldest first |
| `/api/add` | POST | `apiAdd` | `subject`, `faculty`, `cls`, `building`, `room`, `slot` | `{"ok":true}` or an error |
| `/api/change` | POST | `apiChange` | `id`, `room` | `{"ok":true}` or an error |

### `static void apiClasses(int fd, const char *query)`

**Job:** return the classes that match the search text, each with its directions and conflict flag, plus a separate list of every conflict in the timetable.

**Step by step:**

1. `getParam(query, "q", key, sizeof key)` reads the search text from the URL (`/api/classes?q=phys`). With no `q`, `key` is empty and `contains` matches everything.
2. Reset the buffer (`olen = 0`) and write the opening `{"classes":[`.
3. Loop over every class. A class is skipped (`continue`) unless the key appears in its subject **or** its faculty **or** its class name. So typing `CSE-3A` finds both classes of that group, and typing `Roy` finds Dr. Roy's lessons.
4. For each kept class, build the directions text with `snprintf`: `Go to <building>, take the stairs/lift to floor <first character of room>, find room <room>.` The floor is simply the first character of the room number, so room `204` gives floor `2`. An empty room gives `?` instead of reading outside the string.
5. Write the class as one JSON object: its `id` (the array index), the six text fields (each through `putStr`), the `directions`, and `"conflict":true` or `false`, decided by `findConflict(c->room, c->slot, i)` where `i` is passed as `skip` so a class never clashes with itself.
6. **Commas between objects:** the variable `first` starts as 1. The code writes `""` before the first object and `","` before each later one (`first ? "" : ","`), then sets `first = 0`. This is the standard trick for building a JSON list without a trailing comma.
7. After the list, write `],"conflicts":[` and loop over **all** classes (ignoring the search text). Every class that clashes with another adds a line such as `Physics (CSE-3A, room 204, Mon-11AM)`, again using the `first` trick for commas. Because this list ignores the search, the red banner stays accurate however narrow the search is.
8. Close with `]}` and send the buffer.

**Example reply** for `/api/classes?q=phys`:

```json
{"classes":[{"id":1,"subject":"Physics","faculty":"Dr. Das","cls":"CSE-3A","room":"204","building":"Science Block","slot":"Mon-11AM","directions":"Go to Science Block, take the stairs/lift to floor 2, find room 204.","conflict":false}],"conflicts":[]}
```

**Limit:** the floor rule assumes rooms start with a digit. A room named `LAB2` would produce "floor L".

### `static void apiHistory(int fd)`

**Job:** return every recorded room change.

It takes no input. It writes `{"history":[`, loops from index 0 to `nHistory - 1`, and writes each change as an object with `subject`, `cls`, `oldRoom`, `newRoom` and `when`, using `i ? "," : ""` for the commas (a comma before every object except the first). The list is oldest first, exactly as the changes happened. The page reverses it so the newest change is shown at the top.

### `static void apiAdd(int fd, const char *body)`

**Job:** add a new class to the timetable, after checking it is complete and does not double-book a room.

**Step by step:**

1. Create a local `Class c` and fill each field with `getParam`, passing `sizeof` of the field so text is cut to fit. `getParam` always sets its output to an empty string first, so even though `c` starts with random contents, every field ends up as a valid string.
2. **Check 1, completeness:** if any of the six fields is empty, reply `All fields are required.`
3. **Check 2, capacity:** if `nClasses >= MAX`, reply `Database full.`
4. **Check 3, conflict:** call `findConflict(c.room, c.slot, -1)`. If it returns a number `k`, build the message with `snprintf` using `classes[k]` (for example "Conflict: room 101 at Mon-10AM is already used by Mathematics (CSE-3A).") and reply with the error.
5. **Commit:** only when all three checks pass does `classes[nClasses++] = c;` store the new class. It then replies `{"ok":true}`.

**Why the order matters:** all checks come before the one line that changes data, so a refused request leaves the timetable untouched. The cheap checks (empty fields, capacity) also come before the search through the array.

**Limit:** the server does not remove spaces around text. A field containing only a space counts as "not empty". The web page trims every field before sending, so this only matters for someone calling the API directly.

### `static void apiChange(int fd, const char *body)`

**Job:** move an existing class to a different room, refusing the move if it would double-book, and write the change to the history.

**Step by step:**

1. Read `id` and `room` with `getParam`. Convert the id text to a number with `atoi`.
2. **Check 1, valid class:** the id must be between 0 and `nClasses - 1`, and the id text must not be empty. (`atoi` turns an empty string into 0, which is a valid index, so the empty test is separate.) Otherwise reply `Class not found.`
3. **Check 2, room given:** an empty `room` gets `Enter a new room.`
4. Take a pointer `Class *c = &classes[id];` so the rest of the code can use `c->` and change the real entry, not a copy.
5. **Check 3, conflict:** `findConflict(room, c->slot, id)`. The class keeps its slot and only the room changes, so the question is "is the new room free at this class's time?" Passing `id` as `skip` stops the class clashing with itself. A clash gives a refusal message naming the other class.
6. **Record history:** if there is space (`nHistory < MAX`), take the next `Change` entry, copy in the subject and class, copy the **old** room, then the new room, and stamp the time with `strftime` using the format `%d %b %Y, %H:%M` (for example `04 Nov 2026, 08:46`). The time is the server computer's local time.
7. **Apply the change:** `strcpy(c->room, room)` overwrites the room. This comes **after** the history copy, which is the only reason the old room could still be recorded. Then reply `{"ok":true}`.

**Why `strcpy` is safe here:** every destination array is the same size as, or larger than, its source (`room[10]` into `oldRoom[10]`, `newRoom[10]` and `c->room[10]`), and `getParam` already cut the text to fit.

**Behaviours to know:**

- Moving a class to the room it is already in is allowed (it does not clash with itself) and creates a history line such as "101 to 101".
- If the history is full, the room still changes but no line is recorded.
- A non-numeric id such as `abc` becomes 0 through `atoi`, so a hand-written request could change class 0. The web page always sends a real number.

### `static void serveIndex(int fd)`

**Job:** send the web page file `index.html` to the browser.

**Step by step:**

1. `fopen("index.html", "rb")` opens the file in binary mode. The name has no folder part, so it is looked up in the **current directory**. This is why the server must be started from the folder that contains `index.html`.
2. If the file is missing, send the plain-text reply `index.html not found` (using `strlen` for its exact length) and stop.
3. Find the file size: `fseek(f, 0, SEEK_END)` jumps to the end, `ftell` reports the position (the size in bytes), and `rewind` returns to the start.
4. `malloc(size + 1)` reserves memory for the whole file. `fread` loads it, and the function checks that the number of bytes read equals the size.
5. Send the content as `text/html; charset=utf-8`, so the browser shows it as a page and reads accented characters correctly.
6. `free(buf)` and `fclose(f)` release the memory and the file. They run on every path, so nothing leaks.

The page is read from disk on each request, so editing `index.html` and refreshing the browser shows the change without restarting the server.

## Request handling: `handle`

`static void handle(int fd)` is the single entry point for every browser request. It reads the raw request from the network connection, splits it into method, path, query and body, and passes control to the right handler.

### What an HTTP request looks like

A request is plain text. The first line names the method and the address, then come header lines, then one blank line, then an optional body.

```
POST /api/change HTTP/1.1\r\n
Host: localhost:8080\r\n
Content-Type: application/x-www-form-urlencoded\r\n
Content-Length: 13\r\n
\r\n
id=2&room=306
```

A search request has no body, and its data sits in the address instead: `GET /api/classes?q=phys HTTP/1.1`.

### Part 1: reading the whole request

```c
char req[8192];
int n = 0, bodyStart = -1;

while (n < (int)sizeof(req) - 1) {
    int r = recv(fd, req + n, sizeof(req) - 1 - n, 0);
    if (r <= 0) break;
    n += r;
    req[n] = 0;
    char *h = strstr(req, "\r\n\r\n");
    if (h) {
        bodyStart = (int)(h + 4 - req);
        char *p = strcasestr(req, "Content-Length:");
        int cl = p ? atoi(p + 15) : 0;
        if (n - bodyStart >= cl) break;
    }
}
req[n] = 0;
if (bodyStart < 0) return;
const char *body = req + bodyStart;
```

**Why a loop:** a network connection delivers a stream of bytes, not neat messages. One request can arrive in several pieces, so the code keeps reading until it has everything.

**Step by step:**

1. `req` is an 8,192-byte buffer, and `n` counts the bytes received so far. The loop stops before the buffer is full (`n < sizeof(req) - 1`), always leaving one byte for the closing zero.
2. `recv` reads whatever has arrived into the free part of the buffer. A return value of 0 or less means the browser closed the connection or an error happened, so the loop ends.
3. After each read, `req[n] = 0` closes the text as a C string. This keeps `strstr` and `atoi` safe, because they need a string that ends.
4. `strstr(req, "\r\n\r\n")` looks for the blank line that ends the headers. Until it appears, the loop keeps reading.
5. Once found, `bodyStart` is the index where the body begins: the position of the blank line plus 4 characters.
6. `strcasestr` searches for `Content-Length:` ignoring case, because header names are case-insensitive in HTTP. (It needs the `_GNU_SOURCE` line at the top of the file.) `atoi(p + 15)` reads the number after it, since `Content-Length:` is exactly 15 characters long. No such header, as in a GET request, means a length of 0.
7. The loop ends when the number of body bytes received (`n - bodyStart`) is at least the promised length. A GET request therefore ends right after its headers.
8. If the blank line never arrived (`bodyStart < 0`), the request is incomplete or broken, and the function returns without answering.
9. `body` points into the same buffer at the first body character.

**Limit:** requests larger than about 8 KB are cut off. That is far more than any form in this project sends.

### Part 2: splitting method, path and query

```c
char method[8], target[512];
if (sscanf(req, "%7s %511s", method, target) != 2) return;
char *query = strchr(target, '?');
if (query) *query++ = 0; else query = "";
```

1. `sscanf` reads the first two words of the request, the method (`GET`, `POST`) and the target (`/api/classes?q=phys`). The numbers in `%7s` and `%511s` limit how many characters are stored, which prevents overflowing `method` and `target`. If it cannot find two words, the request is malformed and ignored.
2. `strchr(target, '?')` finds the question mark that separates the path from the query text.
3. If there is one, the code overwrites it with a zero byte, which ends `target` there so it contains only the path (`/api/classes`), and moves `query` to the character after it (`q=phys`). The `*query++ = 0` writes the zero and advances the pointer in one step.
4. With no question mark, `query` is set to an empty string.

After this, GET handlers use `query` and POST handlers use `body`. Both are URL-encoded text, so `getParam` reads either one the same way.

### Part 3: routing

```c
if (strcmp(method, "OPTIONS") == 0)                      sendResp(fd, "text/plain", "", 0);
else if (!strcmp(target, "/") || !strcmp(target, "/index.html")) serveIndex(fd);
else if (!strcmp(target, "/api/classes"))                apiClasses(fd, query);
else if (!strcmp(target, "/api/history"))                apiHistory(fd);
else if (!strcmp(target, "/api/add"))                    apiAdd(fd, body);
else if (!strcmp(target, "/api/change"))                 apiChange(fd, body);
else                                                     sendResp(fd, "text/plain", "Not found", 9);
```

This chain of `if` / `else if` is the "router". `strcmp` returns 0 when two strings are equal, so `!strcmp(...)` reads as "is equal". The first match wins.

| Request | What happens |
| --- | --- |
| `OPTIONS` (any address) | An empty `200` reply carrying the permission headers. Browsers sometimes send this "pre-check" before a request from another origin |
| `GET /` or `/index.html` | `serveIndex` sends the page |
| `/api/classes`, `/api/history`, `/api/add`, `/api/change` | The matching handler runs |
| anything else | The text `Not found` |

**Behaviours to know:**

- The API routes do not check the method. A `GET` to `/api/add` runs `apiAdd` with an empty body and simply gets the error `All fields are required.`
- The function ends after one reply. The caller (`main`) closes the connection, so each request uses its own short-lived connection.
- Requests are processed one at a time. While one is being handled, the next waits. For a classroom tool used by a few people this is fast enough, since each reply takes a fraction of a millisecond.

### Tracing one request

A browser sends `POST /api/change` with the body `id=2&room=306`:

1. `recv` delivers the text. The blank line is found, `Content-Length` is 13, and the 13 body bytes are already there, so the loop ends.
2. `sscanf` gives `method = "POST"` and `target = "/api/change"`. There is no `?`, so `query` is empty.
3. `body` points to `id=2&room=306`.
4. The router matches `/api/change` and calls `apiChange(fd, body)`, which replies with JSON.

## The `main` function: starting the server

`main` does three jobs in order: load the sample data, open a network port, and then serve browsers forever.

```c
int main(void) {
    signal(SIGPIPE, SIG_IGN);
    seed();

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT);

    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(srv, 10) < 0) {
        perror("Cannot start server");
        return 1;
    }
    printf("RoomFinder running at http://localhost:%d  (Ctrl+C to stop)\n", PORT);

    for (;;) {
        int fd = accept(srv, NULL, NULL);
        if (fd < 0) continue;
        handle(fd);
        close(fd);
    }
}
```

### Line by line

| Line | What it does | Why |
| --- | --- | --- |
| `signal(SIGPIPE, SIG_IGN)` | Tells the operating system to ignore the "broken pipe" signal | Writing to a connection that the browser already closed would otherwise kill the whole server. Now `write` just returns an error, which `writeAll` handles |
| `seed()` | Loads the three sample classes | The timetable must be filled before the first request arrives |
| `socket(AF_INET, SOCK_STREAM, 0)` | Creates a network endpoint and returns its number (a file descriptor) | `AF_INET` means IPv4 addresses and `SOCK_STREAM` means TCP, the reliable byte stream that HTTP uses |
| `setsockopt(... SO_REUSEADDR ...)` | Allows the port to be reused immediately | Without it, restarting the server within a minute of stopping it can fail with "Address already in use" |
| `struct sockaddr_in addr = {0}` | Creates the address record, all zeros | Unset fields must not contain random leftovers |
| `addr.sin_family = AF_INET` | Marks it as an IPv4 address | Must match the socket type |
| `htonl(INADDR_LOOPBACK)` | Sets the address to `127.0.0.1` | Only programs on the **same computer** can connect. Other machines on the network cannot reach the server, which is the safe choice for a tool with no password |
| `htons(PORT)` | Sets the port to 8080 | `htonl` and `htons` convert numbers into the byte order used on the network |
| `bind(...)` | Attaches the socket to that address and port | Fails if another program (or a second copy of this server) already holds port 8080 |
| `listen(srv, 10)` | Starts waiting for connections, with room for 10 waiting visitors | Visitors that arrive while one request is being served queue up instead of being refused |
| `perror(...)` and `return 1` | Prints the system's reason for failure and exits with an error code | The user sees why the server could not start, for example "Address already in use" |
| `printf("RoomFinder running ...")` | Prints the address to open | Tells the user the server is ready |

### The serving loop

`for (;;)` is an endless loop. Each round does four things:

1. `accept(srv, NULL, NULL)` **waits** until a browser connects, then returns a new connection number `fd`. The original `srv` keeps listening for the next visitor. The two `NULL` arguments mean the program does not need to know who connected.
2. `if (fd < 0) continue;` skips a failed accept and goes back to waiting.
3. `handle(fd)` reads the request and sends the reply (see the previous section).
4. `close(fd)` ends that one connection. This matches the `Connection: close` header sent with every reply.

Because the loop handles one connection fully before accepting the next, the server is **single-tasking**. It never runs two requests at the same time, which is why the global arrays and the global output buffer need no locks. The loop has no exit, so the server stops only when you press **Ctrl+C**.

### Changing the port

Edit `#define PORT 8080` at the top of `server.c`. If you open the page directly from a file instead of through the server, also change the address in the `API` constant inside `index.html`, because that is the one place the port is written on the browser side.

## Frontend functions in `index.html`

The page contains no timetable rules. Its JavaScript only does three things: send requests to the C server, draw the answers, and show the server's error messages. Every decision (what matches a search, what counts as a conflict) is made in C.

### Which user action calls which code

| User action | Frontend function | Request sent | C function that answers |
| --- | --- | --- | --- |
| Opens the page, or types in the search box | `render` | `GET /api/classes?q=...` and `GET /api/history` | `apiClasses`, `apiHistory` |
| Presses **Change room** on a card | `changeRoom` | `POST /api/change` | `apiChange` |
| Submits the **Add a class** form | the form's submit handler | `POST /api/add` | `apiAdd` |

### The `API` constant and the `$` shortcut

```js
const API = location.protocol === "file:" ? "http://localhost:8080" : "";
const $ = id => document.getElementById(id);
```

- `API` is the start of every request address. When the page is opened through the server (`http://localhost:8080`), it is an empty string, so requests like `/api/classes` go to the same server the page came from. When the page is opened straight from a file (`file:` address), the browser has no server, so the full `http://localhost:8080` address is used. That second case is the reason the server sends the `Access-Control-Allow-Origin` header.
- `$` is a short name for `document.getElementById`, used to find page elements by their id.

### `esc(s)`

**Job:** make any text safe to place inside HTML.

```js
const esc = s => String(s).replace(/[&<>"']/g, c =>
  ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
```

It converts the five characters that have special meaning in HTML (`&`, `<`, `>`, `"`, `'`) into their harmless entity forms. The page builds its cards by joining strings and inserting them with `innerHTML`, so without this function a subject named `<script>...</script>` would run as code. Every piece of server data goes through `esc` before it is inserted. `String(s)` first makes sure that numbers also work.

### `post(path, data)`

**Job:** send a form-style POST request to the C server and return its JSON answer.

```js
async function post(path, data) {
  const r = await fetch(API + path, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(data)
  });
  return r.json();
}
```

1. `fetch` sends the request. `await` pauses this function until the answer arrives, without freezing the page.
2. `new URLSearchParams(data)` turns an object such as `{ id: 2, room: "306" }` into the text `id=2&room=306`, encoding spaces as `+` and special characters as `%XX`. This is exactly the format `getParam` in C knows how to decode.
3. `r.json()` reads the reply body and converts it from JSON text into a JavaScript object, such as `{ ok: false, error: "..." }`.

**Why this format and not JSON:** reading URL-encoded text in C takes about twenty lines (`getParam`), while a full JSON parser would be much longer. It also counts as a "simple" request for browsers, which avoids an extra permission check when the page is opened from a file.

### `render()`

**Job:** fetch the current data from the server and redraw the whole page: the conflict banner, the class cards and the history list.

**Step by step:**

1. Read the search box and trim spaces: `key = $("q").value.trim()`.
2. Send two requests **at the same time** with `Promise.all`: the class list (`/api/classes?q=` plus the text through `encodeURIComponent`, which makes it safe to put in an address) and the history. Running them together is faster than one after the other.
3. **Conflict banner:** if the server's `conflicts` list has entries, draw a red box listing them, separated by semicolons. Otherwise clear the banner.
4. **Class cards:** for each class in the reply, build a card with the subject, faculty, class, slot, room, building and directions. If `c.conflict` is true, a red "CONFLICT" tag is added next to the subject. Each card has a text box with the id `nr` plus the class id, and a button that calls `changeRoom` with that id. An empty list shows "No match found."
5. **History:** copy the list, reverse it so the newest change is first (`slice().reverse()` copies first so the original is untouched), and draw one line per change in the form "Subject (class): Room A to Room B, time". An empty list shows "No room changes yet."
6. **Failure case:** the whole block sits in `try ... catch`. If the server is not running, the requests fail, and the page shows a red message telling the user to start `./server` and open `http://localhost:8080`.

The ids on each card (`nr` + id for the text box, `m` + id for the message line) tie each button to its own card, which matters because many cards exist at once.

### `changeRoom(id)`

**Job:** ask the server to move one class to the room typed in its card.

```js
async function changeRoom(id) {
  const msg = $("m" + id);
  try {
    const res = await post("/api/change", { id, room: $("nr" + id).value.trim() });
    if (res.ok) render();
    else { msg.className = "msg bad"; msg.textContent = res.error; }
  } catch (e) { msg.className = "msg bad"; msg.textContent = "Backend not reachable."; }
}
```

1. Find the message line of this card and send the id together with the trimmed text of the new-room box.
2. **Success** (`res.ok` is true): call `render()` again, which shows the new room, clears any old messages and adds the new history line.
3. **Refusal:** show the server's own wording (`res.error`, for example "Conflict: room 101 at Mon-10AM is used by Mathematics (CSE-3A). Change refused.") in red on that card. The page does not redraw, so the message stays visible.
4. **Server unreachable:** the `catch` shows "Backend not reachable."

Note that the message is placed with `textContent`, not `innerHTML`, so even a message containing HTML characters appears as plain text.

### The add-class form handler

```js
$("addForm").addEventListener("submit", async e => {
  e.preventDefault();
  const msg = $("addMsg");
  const f = Object.fromEntries(new FormData(e.target));
  Object.keys(f).forEach(k => f[k] = f[k].trim());
  try {
    const res = await post("/api/add", f);
    if (res.ok) { e.target.reset(); msg.className = "msg ok"; msg.textContent = "Class added."; render(); }
    else { msg.className = "msg bad"; msg.textContent = res.error; }
  } catch (err) { msg.className = "msg bad"; msg.textContent = "Backend not reachable."; }
});
```

1. `e.preventDefault()` stops the browser's default action for a form, which would reload the page.
2. `new FormData(e.target)` gathers every input of the form by its `name` attribute (`subject`, `faculty`, `cls`, `building`, `room`, `slot`). `Object.fromEntries` turns that into a plain object whose keys match the names `apiAdd` reads.
3. Each value is trimmed, so stray spaces are never stored. This is the protection that the server itself lacks.
4. The object is sent to `/api/add`. On success the form is emptied, a green "Class added." appears and `render()` refreshes the list. On refusal (a conflict or a full database) the server's message is shown in red and the typed values are kept, so the user can correct one field.

The form's inputs also have the HTML attribute `required`, so the browser itself stops an empty submission before any request is made.

### Live search and start-up

```js
$("q").addEventListener("input", render);
render();
```

The `input` event fires on every keystroke, so each typed letter calls `render` and the results narrow immediately. The final `render()` call runs once when the page loads and shows the first list.

**Things to be aware of:**

- Each keystroke sends two requests. That is fine on one computer, but a slow network would feel laggy.
- `render` redraws all cards, so text already typed into other cards' "New room" boxes is cleared.
- If replies arrive out of order, an older search result could briefly replace a newer one. On a local server this is very unlikely.

## End-to-end walkthroughs

These five scenarios follow real actions from the browser, through the C functions, and back. All of them start from the three sample classes.

### 1. Starting the server and opening the page

1. You run `./server`. `main` ignores the broken-pipe signal, calls `seed()` (three classes are now stored, `nClasses` is 3), opens and binds the socket on `127.0.0.1:8080`, prints the address and waits in `accept`.
2. You open `http://localhost:8080`. The browser sends `GET / HTTP/1.1`.
3. `accept` returns, `handle` reads the request and `sscanf` finds the method `GET` and the target `/`. The router matches `/` and calls `serveIndex`, which reads `index.html` from disk and sends it. `main` then closes the connection.
4. The browser shows the page. Its script ends with `render()`, which sends `GET /api/classes?q=` and `GET /api/history` as two new connections.
5. `apiClasses` returns all three classes (an empty key matches everything) and an empty `conflicts` list. `apiHistory` returns an empty list. The page draws three cards and the text "No room changes yet."

### 2. Searching as you type

Suppose you type the word `roy` one letter at a time. Each keystroke triggers `render`, and the server runs `contains` on the subject, faculty and class of every stored class.

| You have typed | Classes that match | Why |
| --- | --- | --- |
| `r` | Mathematics, Physics, Programming | The letter appears in `Dr. Roy`, `Dr. Das` and `Programming` |
| `ro` | Mathematics, Programming | `ro` is in `Dr. Roy` and in `Programming`; Physics has none |
| `roy` | Mathematics | Only `Dr. Roy` contains `roy` |

The request for the last step is `GET /api/classes?q=roy`. `getParam` extracts `roy`, the loop keeps only index 0, and the reply contains one class with the directions "Go to Main Block, take the stairs/lift to floor 1, find room 101." Deleting the letters widens the list again.

### 3. Adding a class that is accepted

You fill the form with Chemistry, Dr. Paul, CSE-3C, Science Block, room 210, slot Tue-9AM.

1. The browser checks the `required` attributes, then the submit handler trims each value and sends `POST /api/add` with the body `subject=Chemistry&faculty=Dr.+Paul&cls=CSE-3C&building=Science+Block&room=210&slot=Tue-9AM`.
2. `handle` reads the request. Its `Content-Length` matches the body, so it routes to `apiAdd`.
3. `getParam` decodes the six values (the `+` signs become spaces).
4. No field is empty, `nClasses` (3) is below 100, and `findConflict("210", "Tue-9AM", -1)` returns -1 because nobody uses room 210.
5. `classes[3] = c` stores the class and `nClasses` becomes 4. The reply is `{"ok":true}`.
6. The page empties the form, shows a green "Class added." and calls `render()`. The new card appears with the directions "Go to Science Block, take the stairs/lift to floor 2, find room 210."

### 4. Adding a class that is refused

This time you enter room 101 and slot Mon-10AM.

1. The same request is sent, and `apiAdd` reaches the conflict check.
2. `findConflict("101", "Mon-10AM", -1)` finds Mathematics at index 0 and returns 0.
3. `snprintf` builds the message "Conflict: room 101 at Mon-10AM is already used by Mathematics (CSE-3A)." `replyJson` sends it with `"ok":false`. Nothing is stored and `nClasses` stays the same.
4. The page shows the message in red under the form. The typed values stay in the boxes so you can change just the room.

### 5. Moving a class: success, then refusal

**Success.** On the Programming card (id 2, now in room 305) you type 306 and press **Change room**.

1. `changeRoom(2)` sends `POST /api/change` with `id=2&room=306`.
2. `apiChange` reads id 2 and room `306`, and passes the range check. It sets `c` to point at `classes[2]`.
3. `findConflict("306", "Mon-10AM", 2)` finds nothing, so the move is allowed.
4. The history entry is filled **first**: Programming, CSE-3B, old room `305`, new room `306`, and the current time. `nHistory` becomes 1.
5. Only then does `strcpy(c->room, room)` overwrite the room. The reply is `{"ok":true}`.
6. `render()` redraws. The card shows room 306 with floor 3, and the history list shows a line like "Programming (CSE-3B): Room 305 to Room 306, 04 Nov 2026, 08:46".

**Refusal.** Now you try to move Mathematics (id 0) into room 306 at its slot, or Programming back into room 101.

1. For Programming into 101, `findConflict("101", "Mon-10AM", 2)` skips index 2 and finds Mathematics at index 0, which holds room 101 at Mon-10AM.
2. `apiChange` replies with "Conflict: room 101 at Mon-10AM is used by Mathematics (CSE-3A). Change refused."
3. The room is not changed, no history line is written, and the page shows the message in red on the Programming card only.

These two outcomes were both checked against the running server during development: the refused move returned the error above, and the accepted move produced the history line.

## The console version: `roomfinder.c`

The first file written for the project, `roomfinder.c`, is a text-menu program that runs in a terminal. The project brief allows "C programming for a console-based version", so it is kept as a fully working alternative. It shares its core logic with `server.c` but has no network code. It has its own copy of the data, so changes made in one program are never seen by the other.

Build and run it with `gcc roomfinder.c -o roomfinder` and then `./roomfinder`.

### How each function maps to the server

| Console function | What it does | Server counterpart |
| --- | --- | --- |
| `contains` | Case-insensitive "text contains key" | Same function, same code |
| `findConflict` | Finds another class using the same room and slot | Same idea, but see the difference below |
| `printDirections` | Prints the directions line | Built with `snprintf` inside `apiClasses` |
| `addClass` | Asks for six fields, checks the conflict, stores the class | `apiAdd` |
| `search` | Asks for text, prints every match with directions | `apiClasses` |
| `changeRoom` | Picks a class, asks for a room, checks the conflict, logs history | `apiChange` |
| `showHistory` | Prints the numbered list of changes | `apiHistory` |
| `listAll` | Prints every class on one line each | `apiClasses` with an empty search |
| `seed` | Loads the three sample classes | Same function, same data |
| `main` | Menu loop | `main` (socket loop) plus `handle` (routing) |

### Differences in behaviour

- **Case sensitivity of conflicts.** The console `findConflict` uses `strcmp`, which treats `lab2` and `LAB2` as different rooms. The server uses `strcasecmp`, which ignores case. The server's behaviour is the stricter and safer one.
- **No timestamp.** The console `Change` record has no `when` field, so its history lists the move but not the time.
- **Input and output.** The console version prints with `printf` and reads with `scanf`. The server never touches the keyboard or screen. It reads a request and writes a reply.

### Reading input: `scanf(" %39[^\n]", text)`

The console program reads every text field with this pattern, which has three parts:

1. The **space** at the start tells `scanf` to skip any whitespace first. This matters because after the menu number is read with `scanf("%d", ...)`, the Enter key's newline is still waiting in the input. Without the space, the next read would see that newline and return an empty value.
2. `[^\n]` means "read every character except a newline". Unlike plain `%s`, this accepts spaces, so `Science Block` is read as one value.
3. The number `39` limits how many characters are stored, one fewer than the array size, which prevents overflowing the field.

### The menu loop in `main`

```c
do {
    printf("... menu ...");
    if (scanf("%d", &choice) != 1) break;
    switch (choice) {
        case 1: search(); break;
        case 2: addClass(); break;
        case 3: changeRoom(); break;
        case 4: showHistory(); break;
        case 5: listAll(); break;
    }
} while (choice != 0);
```

The loop prints the menu, reads a number and runs the matching function through `switch`. Choosing `0` makes the condition `choice != 0` false, so the loop ends. The check `scanf(...) != 1` handles input that is not a number (a letter, or the end of input): `scanf` then returns something other than 1 and the loop is left, which prevents an endless loop of failed reads.

### How `changeRoom` picks the class

Because several classes can share a subject name (two sections of Mathematics, for example), the console version first lists every class whose subject contains the typed text, numbering them from 1 and remembering their real positions in the array `idx[]`. If more than one matches it asks which number to move. `idx[ch - 1]` then converts the chosen number back into the array index. The rest (conflict check, history, update) matches `apiChange`.

The web version avoids this extra step: each card already knows its own `id`, so the user simply presses the button on the exact class they want.

## Limitations, edge cases and improvements

The program is deliberately simple, which means some things are left out. This section lists them honestly so you can explain them in a viva or report, and shows how each could be fixed.

### What was and was not tested

The server was compiled with warnings on and exercised with `curl`: loading the page, searching, an accepted and a refused room change, an accepted and a refused add, and reading the history. All gave the expected replies. The page itself has **not** been opened in a real browser during development, so click through it once before a demonstration.

### Known limitations

| Limitation | Effect | Possible fix |
| --- | --- | --- |
| Data is kept in memory only | Everything resets to the three sample classes when the server stops | Save the arrays to a file with `fopen`, `fwrite` and `fread` when data changes, and load them in `main` |
| Fixed capacity of 100 classes and 100 history lines | Adding is refused at 100; history stops recording at 100 | Allocate arrays with `malloc` and grow them with `realloc` |
| Only room double-booking is detected | The same teacher or student group can be placed in two rooms at the same time | Add a second check in `findConflict` comparing `faculty` and `cls` with the slot |
| Slots are compared as plain text | `Mon-10AM` and `Mon 10AM` count as different times | Use fixed dropdown values for the slot in the form |
| Floor is the first character of the room | A room named `LAB2` is reported as floor `L` | Store the floor and building as separate fields |
| Directions are generic text | No real route inside the building | Add a path or landmark field per room |
| Single-tasking server | One request at a time; an idle connection can delay the next one | Use `fork` or threads, or `poll`, for several connections |
| No password or login | Anyone who can reach the port can change the timetable. The loopback address limits this to the same computer | Add a login check in `handle` and listen on a network address only behind a trusted network |
| Status code is always 200 | Errors are reported only in the JSON | Send `400` or `404` status lines for failures |
| No way to delete or edit a class | Mistakes can only be fixed by changing the room | Add a delete route, and use stable id numbers instead of array positions |
| Server does not trim spaces | A field containing only a space counts as filled | Trim in `getParam` or in the handlers |
| `atoi` accepts any text | A hand-written id such as `abc` becomes 0 | Use `strtol` and check that digits were read |

### Edge cases and how the code behaves

| Situation | Behaviour |
| --- | --- |
| Search box empty | Every class is listed, because an empty key is found in every string |
| Search text longer than 39 characters | `getParam` cuts it to 39 characters |
| Text containing a quote, backslash or new line | `putStr` escapes or replaces it, so the JSON stays valid |
| Accented or non-English letters | Kept intact when sent back, but not case-folded by `contains` |
| Moving a class to the room it already has | Allowed, and a history line "101 to 101" is written |
| Browser closes the connection mid-reply | `SIGPIPE` is ignored and `writeAll` stops quietly |
| Port 8080 already in use | `bind` fails, and `perror` prints the reason before the server exits |
| `index.html` missing from the current folder | The server answers with the text `index.html not found` |
| Server not running when the page opens | The page shows a red message telling the user to start `./server` |

### Ideas for extending the project

The original project brief lists a lost-and-found style matching feature next to RoomFinder. If the group wants it, the same structure applies: a new struct for items, a `contains`-style keyword match between a lost report and found reports, two new API routes and a second panel on the page.

Two smaller upgrades give the best value for little work: saving data to a file, and adding the teacher and student-group conflict check.
