/* RoomFinder - The Last-Minute Classroom Locator (backend, console version)
 * Compile: gcc roomfinder.c -o roomfinder
 * Run:     ./roomfinder
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX 100

typedef struct {
    char subject[40];
    char faculty[40];
    char cls[20];       /* class/section, e.g. CSE-3A */
    char room[10];      /* e.g. 304 (first digit = floor) */
    char building[30];
    char slot[20];      /* e.g. Mon-10AM */
} Class;

typedef struct {
    char subject[40];
    char cls[20];
    char oldRoom[10];
    char newRoom[10];
} Change;

Class classes[MAX];
Change history[MAX];
int nClasses = 0, nHistory = 0;

/* case-insensitive "text contains key" */
int contains(const char *text, const char *key) {
    char a[60], b[60];
    int i;
    for (i = 0; text[i] && i < 59; i++) a[i] = tolower(text[i]);
    a[i] = '\0';
    for (i = 0; key[i] && i < 59; i++) b[i] = tolower(key[i]);
    b[i] = '\0';
    return strstr(a, b) != NULL;
}

/* returns index of another class using same room+slot, or -1 */
int findConflict(const char *room, const char *slot, int skip) {
    for (int i = 0; i < nClasses; i++)
        if (i != skip && strcmp(classes[i].room, room) == 0 &&
            strcmp(classes[i].slot, slot) == 0)
            return i;
    return -1;
}

void printDirections(Class *c) {
    printf("  Directions: Go to %s, take the stairs/lift to floor %c, find room %s.\n",
           c->building, c->room[0], c->room);
}

void addClass(void) {
    if (nClasses >= MAX) { printf("Database full.\n"); return; }
    Class c;
    printf("Subject : "); scanf(" %39[^\n]", c.subject);
    printf("Faculty : "); scanf(" %39[^\n]", c.faculty);
    printf("Class   : "); scanf(" %19[^\n]", c.cls);
    printf("Building: "); scanf(" %29[^\n]", c.building);
    printf("Room    : "); scanf(" %9[^\n]", c.room);
    printf("Slot    : "); scanf(" %19[^\n]", c.slot);

    int k = findConflict(c.room, c.slot, -1);
    if (k >= 0) {
        printf("CONFLICT! Room %s at %s is already used by %s (%s).\n",
               c.room, c.slot, classes[k].subject, classes[k].cls);
        return;
    }
    classes[nClasses++] = c;
    printf("Class added.\n");
}

void search(void) {
    char key[40];
    int found = 0;
    printf("Search (subject / faculty / class): ");
    scanf(" %39[^\n]", key);
    for (int i = 0; i < nClasses; i++) {
        Class *c = &classes[i];
        if (contains(c->subject, key) || contains(c->faculty, key) || contains(c->cls, key)) {
            printf("\n%s | %s | %s | %s\n  Room %s, %s\n",
                   c->subject, c->faculty, c->cls, c->slot, c->room, c->building);
            printDirections(c);
            found++;
        }
    }
    if (!found) printf("No match found.\n");
}

void changeRoom(void) {
    char key[40], newRoom[10];
    int idx[MAX], n = 0;
    printf("Subject to move: "); scanf(" %39[^\n]", key);
    for (int i = 0; i < nClasses; i++)
        if (contains(classes[i].subject, key)) {
            printf("%d) %s | %s | %s | Room %s\n", n + 1, classes[i].subject,
                   classes[i].cls, classes[i].slot, classes[i].room);
            idx[n++] = i;
        }
    if (!n) { printf("Not found.\n"); return; }

    int ch = 1;
    if (n > 1) { printf("Pick number: "); scanf("%d", &ch); }
    if (ch < 1 || ch > n) { printf("Invalid choice.\n"); return; }
    Class *c = &classes[idx[ch - 1]];

    printf("New room: "); scanf(" %9[^\n]", newRoom);
    int k = findConflict(newRoom, c->slot, idx[ch - 1]);
    if (k >= 0) {
        printf("CONFLICT! Room %s at %s is used by %s (%s). Change refused.\n",
               newRoom, c->slot, classes[k].subject, classes[k].cls);
        return;
    }
    if (nHistory < MAX) {
        strcpy(history[nHistory].subject, c->subject);
        strcpy(history[nHistory].cls, c->cls);
        strcpy(history[nHistory].oldRoom, c->room);
        strcpy(history[nHistory].newRoom, newRoom);
        nHistory++;
    }
    strcpy(c->room, newRoom);
    printf("Room updated.\n");
}

void showHistory(void) {
    if (!nHistory) { printf("No room changes yet.\n"); return; }
    for (int i = 0; i < nHistory; i++)
        printf("%d. %s (%s): Room %s -> Room %s\n", i + 1, history[i].subject,
               history[i].cls, history[i].oldRoom, history[i].newRoom);
}

void listAll(void) {
    for (int i = 0; i < nClasses; i++)
        printf("%s | %s | %s | %s | Room %s, %s\n", classes[i].subject,
               classes[i].faculty, classes[i].cls, classes[i].slot,
               classes[i].room, classes[i].building);
}

void seed(void) {
    Class s[] = {
        {"Mathematics", "Dr. Roy",   "CSE-3A", "101", "Main Block",    "Mon-10AM"},
        {"Physics",     "Dr. Das",   "CSE-3A", "204", "Science Block", "Mon-11AM"},
        {"Programming", "Ms. Sen",   "CSE-3B", "305", "Main Block",    "Mon-10AM"},
    };
    for (int i = 0; i < 3; i++) classes[nClasses++] = s[i];
}

int main(void) {
    int choice;
    seed();
    do {
        printf("\n=== RoomFinder ===\n"
               "1. Search room\n2. Add class\n3. Change room\n"
               "4. Room-change history\n5. List all classes\n0. Exit\n> ");
        if (scanf("%d", &choice) != 1) break;
        switch (choice) {
            case 1: search(); break;
            case 2: addClass(); break;
            case 3: changeRoom(); break;
            case 4: showHistory(); break;
            case 5: listAll(); break;
        }
    } while (choice != 0);
    return 0;
}
