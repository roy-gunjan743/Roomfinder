# RoomFinder - The Last-Minute Classroom Locator

A student arrives on campus and finds that a lecture has been moved to another room. RoomFinder lets them search by **subject, faculty or class**, shows the **room, building and simple directions**, keeps a **room-change history**, and **refuses room clashes** (two classes in the same room at the same time).

- **Backend:** C (a small web server with no extra libraries)
- **Frontend:** HTML, CSS and JavaScript (`index.html`)

## Files

| File | What it is |
| --- | --- |
| `server.c` | The C backend as a web server (use this with the web page) |
| `index.html` | The web page (frontend) |
| `roomfinder.c` | Console (text menu) version of the backend, no browser needed |
| `DOCUMENTATION.md` | Detailed explanation of every function and the logic behind it |

## Requirements

- A C compiler (`gcc`)
- A web browser
- Linux or macOS (or Windows with WSL)

Check that `gcc` is installed:

```bash
gcc --version
```

If it is missing:

```bash
# Ubuntu / Debian / WSL
sudo apt install gcc

# macOS
xcode-select --install
```

## Get the code

```bash
git clone https://github.com/roy-gunjan743/Roomfinder.git
cd Roomfinder
```

## Option 1: Web version (backend + frontend)

1. Compile the server:

   ```bash
   gcc server.c -o server
   ```

2. Start it **from the folder that contains `index.html`**:

   ```bash
   ./server
   ```

   You should see:

   ```
   RoomFinder running at http://localhost:8080  (Ctrl+C to stop)
   ```

3. Open **http://localhost:8080** in your browser.

4. Keep the terminal open while you use the page. Press `Ctrl+C` to stop the server.

### What you can do on the page

| Action | How |
| --- | --- |
| Search | Type a subject, faculty or class name in the search box (results update as you type) |
| See directions | Each class card shows its room, building and directions |
| Change a room | Type the new room in a card and press **Change room** |
| Add a class | Fill in the **Add a class** form and press **Add class** |
| View history | Scroll to **Room-change history** |

If a new class or a room change would put two classes in the same room at the same time, it is refused with a message.

### Sample data

The server starts with three classes:

| Subject | Faculty | Class | Room | Building | Slot |
| --- | --- | --- | --- | --- | --- |
| Mathematics | Dr. Roy | CSE-3A | 101 | Main Block | Mon-10AM |
| Physics | Dr. Das | CSE-3A | 204 | Science Block | Mon-11AM |
| Programming | Ms. Sen | CSE-3B | 305 | Main Block | Mon-10AM |

To try the conflict check, move **Programming** into room **101**: Mathematics already uses room 101 at Mon-10AM, so the change is refused.

> Data is kept in memory only. It resets to the sample data every time the server is restarted.

## Option 2: Console version (no browser)

```bash
gcc roomfinder.c -o roomfinder
./roomfinder
```

Use the menu:

```
1. Search room
2. Add class
3. Change room
4. Room-change history
5. List all classes
0. Exit
```

## Testing the API with curl (optional)

With the server running, in another terminal:

```bash
# Search (empty q lists everything)
curl "localhost:8080/api/classes?q=phys"

# Room-change history
curl localhost:8080/api/history

# Move class 2 (Programming) to room 306
curl -d "id=2&room=306" localhost:8080/api/change

# Add a class
curl -d "subject=Chemistry&faculty=Dr.+Paul&cls=CSE-3C&building=Science+Block&room=210&slot=Tue-9AM" localhost:8080/api/add
```

### API summary

| Route | Method | Input | Returns |
| --- | --- | --- | --- |
| `/` | GET | none | The web page |
| `/api/classes` | GET | `q` (optional search text) | Matching classes and any conflicts |
| `/api/history` | GET | none | All room changes |
| `/api/add` | POST | `subject`, `faculty`, `cls`, `building`, `room`, `slot` | `{"ok":true}` or an error |
| `/api/change` | POST | `id`, `room` | `{"ok":true}` or an error |

## Troubleshooting

| Problem | Fix |
| --- | --- |
| `Cannot start server: Address already in use` | Another program (or an old copy of the server) is using port 8080. Stop it, or change `#define PORT 8080` in `server.c` and recompile |
| Page says "Cannot reach the C backend" | The server is not running. Start `./server` and open `http://localhost:8080` |
| Browser shows `index.html not found` | You started `./server` from the wrong folder. `cd` into the folder that contains `index.html` and run it again |
| `gcc: command not found` | Install a C compiler (see Requirements) |
| Changes to `server.c` have no effect | Recompile with `gcc server.c -o server`, then restart the server |
| Changes to `index.html` have no effect | Just refresh the browser page (no restart needed) |

## Team

Group 3: Barna Saha, Bibek Kundu, Biraj Biswas
