/* RoomFinder backend as a tiny HTTP server (plain C, no libraries).
 * Compile: gcc server.c -o server
 * Run:     ./server        then open http://localhost:8080
 *
 * API
 *   GET  /                    -> index.html
 *   GET  /api/classes?q=key   -> classes matching key + list of conflicts
 *   GET  /api/history         -> room-change history
 *   POST /api/add             -> subject,faculty,cls,building,room,slot
 *   POST /api/change          -> id,room
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX  100
#define PORT 8080

typedef struct {
    char subject[40], faculty[40], cls[20], room[10], building[30], slot[20];
} Class;

typedef struct {
    char subject[40], cls[20], oldRoom[10], newRoom[10], when[32];
} Change;

Class  classes[MAX];
Change history[MAX];
int nClasses = 0, nHistory = 0;

/* ---------------- core logic (same as console version) ---------------- */

int contains(const char *text, const char *key) {
    char a[60], b[60];
    int i;
    for (i = 0; text[i] && i < 59; i++) a[i] = tolower((unsigned char)text[i]);
    a[i] = '\0';
    for (i = 0; key[i] && i < 59; i++) b[i] = tolower((unsigned char)key[i]);
    b[i] = '\0';
    return strstr(a, b) != NULL;
}

int findConflict(const char *room, const char *slot, int skip) {
    for (int i = 0; i < nClasses; i++)
        if (i != skip && strcasecmp(classes[i].room, room) == 0 &&
            strcasecmp(classes[i].slot, slot) == 0)
            return i;
    return -1;
}

void seed(void) {
    Class s[] = {
        {"Mathematics", "Dr. Roy", "CSE-3A", "101", "Main Block",    "Mon-10AM"},
        {"Physics",     "Dr. Das", "CSE-3A", "204", "Science Block", "Mon-11AM"},
        {"Programming", "Ms. Sen", "CSE-3B", "305", "Main Block",    "Mon-10AM"},
    };
    for (int i = 0; i < 3; i++) classes[nClasses++] = s[i];
}

/* ---------------- tiny helpers: JSON output, URL params ---------------- */

static char out[65536];
static int  olen;

static void put(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    olen += vsnprintf(out + olen, sizeof(out) - olen, fmt, ap);
    va_end(ap);
    if (olen >= (int)sizeof(out)) olen = sizeof(out) - 1;
}

static void putStr(const char *s) {           /* JSON string with escaping */
    put("\"");
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') put("\\%c", *s);
        else if ((unsigned char)*s < 32) put(" ");
        else put("%c", *s);
    }
    put("\"");
}

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

static void writeAll(int fd, const char *buf, int len) {
    while (len > 0) {
        int w = write(fd, buf, len);
        if (w <= 0) return;
        buf += w; len -= w;
    }
}

static void sendResp(int fd, const char *type, const char *body, int len) {
    char head[300];
    int h = snprintf(head, sizeof head,
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: *\r\n"
        "Connection: close\r\n\r\n", type, len);
    writeAll(fd, head, h);
    writeAll(fd, body, len);
}

static void replyJson(int fd, int ok, const char *err) {
    olen = 0;
    if (ok) put("{\"ok\":true}");
    else { put("{\"ok\":false,\"error\":"); putStr(err); put("}"); }
    sendResp(fd, "application/json", out, olen);
}

/* ---------------- API handlers ---------------- */

static void apiClasses(int fd, const char *query) {
    char key[40], dir[160], tmp[200];
    getParam(query, "q", key, sizeof key);
    olen = 0;
    put("{\"classes\":[");
    int first = 1;
    for (int i = 0; i < nClasses; i++) {
        Class *c = &classes[i];
        if (!(contains(c->subject, key) || contains(c->faculty, key) || contains(c->cls, key)))
            continue;
        snprintf(dir, sizeof dir, "Go to %s, take the stairs/lift to floor %c, find room %s.",
                 c->building, c->room[0] ? c->room[0] : '?', c->room);
        put("%s{\"id\":%d,\"subject\":", first ? "" : ",", i); putStr(c->subject);
        put(",\"faculty\":");   putStr(c->faculty);
        put(",\"cls\":");       putStr(c->cls);
        put(",\"room\":");      putStr(c->room);
        put(",\"building\":");  putStr(c->building);
        put(",\"slot\":");      putStr(c->slot);
        put(",\"directions\":"); putStr(dir);
        put(",\"conflict\":%s}", findConflict(c->room, c->slot, i) >= 0 ? "true" : "false");
        first = 0;
    }
    put("],\"conflicts\":[");
    first = 1;
    for (int i = 0; i < nClasses; i++) {
        Class *c = &classes[i];
        if (findConflict(c->room, c->slot, i) < 0) continue;
        snprintf(tmp, sizeof tmp, "%s (%s, room %s, %s)", c->subject, c->cls, c->room, c->slot);
        put("%s", first ? "" : ",");
        putStr(tmp);
        first = 0;
    }
    put("]}");
    sendResp(fd, "application/json", out, olen);
}

static void apiHistory(int fd) {
    olen = 0;
    put("{\"history\":[");
    for (int i = 0; i < nHistory; i++) {
        Change *h = &history[i];
        put("%s{\"subject\":", i ? "," : ""); putStr(h->subject);
        put(",\"cls\":");     putStr(h->cls);
        put(",\"oldRoom\":"); putStr(h->oldRoom);
        put(",\"newRoom\":"); putStr(h->newRoom);
        put(",\"when\":");    putStr(h->when);
        put("}");
    }
    put("]}");
    sendResp(fd, "application/json", out, olen);
}

static void apiAdd(int fd, const char *body) {
    Class c;
    getParam(body, "subject",  c.subject,  sizeof c.subject);
    getParam(body, "faculty",  c.faculty,  sizeof c.faculty);
    getParam(body, "cls",      c.cls,      sizeof c.cls);
    getParam(body, "building", c.building, sizeof c.building);
    getParam(body, "room",     c.room,     sizeof c.room);
    getParam(body, "slot",     c.slot,     sizeof c.slot);

    if (!c.subject[0] || !c.faculty[0] || !c.cls[0] || !c.building[0] || !c.room[0] || !c.slot[0])
        { replyJson(fd, 0, "All fields are required."); return; }
    if (nClasses >= MAX) { replyJson(fd, 0, "Database full."); return; }

    int k = findConflict(c.room, c.slot, -1);
    if (k >= 0) {
        char msg[200];
        snprintf(msg, sizeof msg, "Conflict: room %s at %s is already used by %s (%s).",
                 c.room, c.slot, classes[k].subject, classes[k].cls);
        replyJson(fd, 0, msg);
        return;
    }
    classes[nClasses++] = c;
    replyJson(fd, 1, NULL);
}

static void apiChange(int fd, const char *body) {
    char idStr[12], room[10];
    getParam(body, "id", idStr, sizeof idStr);
    getParam(body, "room", room, sizeof room);
    int id = atoi(idStr);

    if (id < 0 || id >= nClasses || !idStr[0]) { replyJson(fd, 0, "Class not found."); return; }
    if (!room[0]) { replyJson(fd, 0, "Enter a new room."); return; }

    Class *c = &classes[id];
    int k = findConflict(room, c->slot, id);
    if (k >= 0) {
        char msg[200];
        snprintf(msg, sizeof msg, "Conflict: room %s at %s is used by %s (%s). Change refused.",
                 room, c->slot, classes[k].subject, classes[k].cls);
        replyJson(fd, 0, msg);
        return;
    }
    if (nHistory < MAX) {
        Change *h = &history[nHistory++];
        time_t t = time(NULL);
        strcpy(h->subject, c->subject);
        strcpy(h->cls, c->cls);
        strcpy(h->oldRoom, c->room);
        strcpy(h->newRoom, room);
        strftime(h->when, sizeof h->when, "%d %b %Y, %H:%M", localtime(&t));
    }
    strcpy(c->room, room);
    replyJson(fd, 1, NULL);
}

static void serveIndex(int fd) {
    FILE *f = fopen("index.html", "rb");
    if (!f) { sendResp(fd, "text/plain", "index.html not found", (int)strlen("index.html not found")); return; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    if (buf && fread(buf, 1, size, f) == (size_t)size)
        sendResp(fd, "text/html; charset=utf-8", buf, (int)size);
    free(buf);
    fclose(f);
}

/* ---------------- request handling ---------------- */

static void handle(int fd) {
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

    char method[8], target[512];
    if (sscanf(req, "%7s %511s", method, target) != 2) return;
    char *query = strchr(target, '?');
    if (query) *query++ = 0; else query = "";

    if (strcmp(method, "OPTIONS") == 0)                      sendResp(fd, "text/plain", "", 0);
    else if (!strcmp(target, "/") || !strcmp(target, "/index.html")) serveIndex(fd);
    else if (!strcmp(target, "/api/classes"))                apiClasses(fd, query);
    else if (!strcmp(target, "/api/history"))                apiHistory(fd);
    else if (!strcmp(target, "/api/add"))                    apiAdd(fd, body);
    else if (!strcmp(target, "/api/change"))                 apiChange(fd, body);
    else                                                     sendResp(fd, "text/plain", "Not found", 9);
}

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
