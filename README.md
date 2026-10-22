# Medical Inventory Management System

Academic project — VIT, SY Semester 3, Object Oriented Programming (C++).

A medicine inventory system with **two front ends that share the same data
files**: a C++ console application and a Flask web application.

| Component | File | Role |
|---|---|---|
| C++ console app | `prog2.cpp` → `prog2.exe` | Current version: login, role-based access, audit trail, billing |
| C++ console app | `prog1.cpp` → `prog1.exe` | Base version: single user. Demonstrates the abstract base class and runtime polymorphism |
| Flask web app | `app.py` | Browser UI, REST endpoints, CSV backup/restore, user management |
| Test suite | `tests/test_app.py` | 37 assertions over the Python side |

---

## Quick start

### Flask web application

```bash
pip install -r requirements.txt
python app.py
# then open http://localhost:5000
```

### C++ console application

```bash
g++ -std=c++11 -Wall -Wextra prog2.cpp -o prog2.exe
prog2.exe

# base version
g++ -std=c++11 -Wall -Wextra prog1.cpp -o prog1.exe
prog1.exe
```

Both compile warning-free with `-Wall -Wextra` on g++ 6.3.0 (MinGW).
C++11 is required (`<tuple>`, `nullptr`).

### Tests

```bash
python tests/test_app.py
```

Runs against a temporary copy of the data files, so it never touches
`inventory.txt`, `users.txt` or `history.txt`. Exits non-zero on failure.

---

## Test accounts

These are development credentials for a local academic project. They used to be
printed on the login page itself; they live here instead.

| User ID | Password | Role |
|---|---|---|
| `d001` | `doc123` | Doctor |
| `d002` | `doc456` | Doctor |
| `p001` | `pharm123` | Pharmacist |
| `p002` | `pharm456` | Pharmacist |
| `n001` | `nurse123` | Nurse |
| `n002` | `nurse456` | Nurse |

If `users.txt` is missing, both applications recreate it with these six
accounts.

---

## Permissions

| Capability | Doctor | Pharmacist | Nurse |
|---|:--:|:--:|:--:|
| Add medicine | ✓ | ✓ | |
| Update stock | ✓ | ✓ | |
| Restock medicine | ✓ | ✓ | |
| Sell medicine | | ✓ | ✓ |
| Clear expired stock | | ✓ | |
| Manage users | | ✓ | |
| Restore from backup | | ✓ | |
| View reports | ✓ | ✓ | ✓ |

The Flask app re-reads the role from `users.txt` on every request, so editing a
session cookie cannot escalate privileges.

---

## Data file formats

All three programs read and write these files in the project root. **The formats
are a contract between them — changing one side breaks the others.**

```
inventory.txt   name,batch,expiry,quantity,price,originalQuantity
                Paracetamol,1004,2027-11-30,9000,3.00,9000

users.txt       userId,name,role,password
                p001,Ramesh Pharmacist,Pharmacist,pharm123

history.txt     [YYYY-MM-DD HH:MM:SS] [userId - Name] message
                [2025-11-14 11:30:32] [p001 - Ramesh Pharmacist] Sold 100 of Dolo (1002)

user_history.txt  same shape as history.txt; login/logout events only
```

Notes:

- **Fields are comma-separated with no quoting.** Both programs therefore
  *reject* commas in medicine names, batch numbers and user names rather than
  quoting them — a quoted field would corrupt the C++ reader, which parses with
  `getline(ss, field, ',')`.
- `expiry` is `YYYY-MM-DD`. CSV *import* also accepts `DD-MM-YYYY` and
  normalises it.
- Older `history.txt` lines predate user attribution and have no
  `[userId - Name]` block. The parser treats that block as optional.
- Prices are written with two decimals by all three programs.
- An item expiring **today is not expired** — it keeps its final day of shelf
  life. The C++ and Python implementations agree on this rule.
- Clearing expired stock is a **soft delete**: the quantity is set to 0 and the
  record is kept, so batch numbers and history stay meaningful.

### Concurrency caveat

Both applications load the inventory, modify it in memory, then rewrite the
whole file. There is **no cross-process locking**. If the console app and the
web app are open at the same time, the last one to save wins and the other's
changes are lost. Run one at a time.

(The Flask app writes atomically — to a temp file, then `os.replace` — so a
crash mid-write cannot leave a truncated or empty data file. That protects
against corruption, not against a lost update.)

---

## Configuration

The Flask app reads two environment variables:

| Variable | Default | Purpose |
|---|---|---|
| `FLASK_SECRET_KEY` | random per run | Session signing key. Set it to keep sessions valid across restarts. |
| `FLASK_DEBUG` | off | Set to `1` to enable the Werkzeug debugger **in development only**. |
| `PORT` | `5000` | Port to listen on. |

```bash
# Windows PowerShell
$env:FLASK_SECRET_KEY = "some-long-random-value"
python app.py
```

With no `FLASK_SECRET_KEY` the app generates a random key and prints a warning;
everyone is logged out when it restarts.

---

## Security notes

This is a local academic project, not a deployment. What is in place:

- Session-based authentication with role checks re-read from disk
- CSRF tokens on every state-changing POST (implemented in `app.py`; Flask-WTF
  is not a dependency)
- `HttpOnly` and `SameSite=Lax` session cookies; the session is cleared and
  reissued on login
- All inventory-mutating actions are POST-only — clearing expired stock used to
  be a `GET` link, which a link prefetcher could trigger
- Authenticated API endpoints
- Server-side validation of every form field, so bad input cannot be written to
  the shared data files
- The Werkzeug debugger is off unless explicitly enabled

### Known limitation: plain-text passwords

`users.txt` stores passwords in plain text. This is deliberate and documented
rather than an oversight: the file is shared with `prog2.cpp`, which has no
hashing support, so hashing on the Python side alone would lock the C++ program
out of every migrated account.

Fixing it properly means implementing the same hash on both sides (for example
SHA-256 in C++ and `hashlib` in Python) and migrating the file once. Until then,
treat `users.txt` as a secret and do not reuse these passwords anywhere.

Passwords are compared with `hmac.compare_digest`, so comparison timing does not
leak them.

---

## Project layout

```
CP/
├── app.py                  Flask application
├── prog1.cpp / prog1.exe   C++ console app (base version)
├── prog2.cpp / prog2.exe   C++ console app (current version)
├── requirements.txt
├── README.md
│
├── inventory.txt           Live data (shared by all three programs)
├── users.txt
├── history.txt
├── user_history.txt
│
├── Templates/              17 Jinja2 templates, all extending base.html
├── static/
│   ├── css/style.css       Token-based design system, light + dark
│   └── js/app.js           Theme toggle, medicine picker, table filters
├── tests/test_app.py       Test suite
├── backups/                CSV/TXT snapshots written before a restore
└── Info/                   Course documentation and OOP concept notes
```

---

## OOP concepts demonstrated

**`prog1.cpp`** — abstract base class (`Record`) with pure virtual functions and
a virtual destructor, inheritance, **runtime polymorphism** (dispatch through a
`const Record&`), encapsulation, constructor overloading, static factory
methods, exception handling, operator-free value semantics (no `new`/`delete`,
so no leaks).

**`prog2.cpp`** — encapsulation and data hiding, composition
(`InventoryManager` has-a `vector<Medicine>`, `UserManager` has-a
`map<string, User>`), constructor overloading, static factory methods, const
correctness, exception handling around deserialization, role-based access
control, file I/O with objects.

**`app.py`** — the `User` class with permission methods, decorators for
authentication and authorization (`@login_required`,
`@permission_required(...)`), a DAO-style data-access layer, and the MVC split
between routes, data functions and templates.
