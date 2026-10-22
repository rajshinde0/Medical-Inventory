"""
================================================================================
    MEDICAL INVENTORY MANAGEMENT SYSTEM v2.1 - FLASK WEB APPLICATION
================================================================================
    Documentation: see README.md in this folder.

    Quick start:
        pip install -r requirements.txt
        python app.py
        http://localhost:5000

    This web app shares its data files (inventory.txt, users.txt, history.txt)
    with the C++ console application (prog2.cpp). The on-disk formats are a
    contract between the two programs -- see README.md before changing them.
================================================================================
"""

from flask import (Flask, render_template, request, redirect, url_for, flash,
                   jsonify, send_file, make_response, session, g)
from functools import wraps
from datetime import datetime, date
import os
import re
import csv
import io
import hmac
import secrets
import tempfile
from werkzeug.utils import secure_filename

# ----------------------------------------------------------------------------
# Paths. Every data file is resolved against the directory holding this file,
# so the app no longer depends on the process working directory (the old
# os.chdir() at import time mutated global state for anything importing us).
# ----------------------------------------------------------------------------
APP_DIR = os.path.dirname(os.path.abspath(__file__))
BACKUP_DIR = os.path.join(APP_DIR, "backups")

INVENTORY_FILE = os.path.join(APP_DIR, "inventory.txt")
USERS_FILE = os.path.join(APP_DIR, "users.txt")
HISTORY_FILE = os.path.join(APP_DIR, "history.txt")
USER_HISTORY_FILE = os.path.join(APP_DIR, "user_history.txt")

RUPEE = "₹"     # U+20B9; this file is UTF-8, as Python 3 assumes
ALLOWED_EXTENSIONS = {"csv"}
LOW_STOCK_THRESHOLD = 10
VALID_ROLES = ("Doctor", "Pharmacist", "Nurse")

# ----------------------------------------------------------------------------
# OOP CONCEPT: OBJECT INSTANTIATION -- the Flask app is an object.
# template_folder is explicit because the real directory is "Templates" with a
# capital T; relying on Flask's default "templates" only works on a
# case-insensitive filesystem (Windows) and breaks on Linux.
# ----------------------------------------------------------------------------
app = Flask(__name__, template_folder="Templates")

app.secret_key = os.environ.get("FLASK_SECRET_KEY") or secrets.token_hex(32)
if not os.environ.get("FLASK_SECRET_KEY"):
    print("[WARN] FLASK_SECRET_KEY is not set; using a random key. "
          "Sessions will not survive a restart.")

app.config.update(
    MAX_CONTENT_LENGTH=16 * 1024 * 1024,      # 16 MB upload cap
    SESSION_COOKIE_HTTPONLY=True,
    SESSION_COOKIE_SAMESITE="Lax",
)


# ============================================================================
# USER CLASS - AUTHENTICATION & AUTHORIZATION
# ============================================================================
class User:
    """
    OOP CONCEPT: ENCAPSULATION
    Bundles a user's identity with the permission rules that depend on it.

    NOTE ON PASSWORDS: stored in plain text because users.txt is shared with
    the C++ console app, which has no hashing support. This is a documented
    limitation of the academic project, not an oversight -- see README.md.
    """

    def __init__(self, user_id, name, role, password):
        self.user_id = user_id
        self.name = name
        self.role = role
        self.password = password

    def verify_password(self, password):
        """Constant-time comparison, so timing does not leak the password."""
        return hmac.compare_digest(self.password, password or "")

    # --- permission predicates (role-based access control) ------------------
    def can_add_medicine(self):
        return self.role in ("Doctor", "Pharmacist")

    def can_buy_medicine(self):
        return self.role in ("Pharmacist", "Nurse")

    def can_update_medicine(self):
        return self.role in ("Doctor", "Pharmacist")

    def can_remove_expired(self):
        return self.role == "Pharmacist"

    def can_manage_users(self):
        return self.role == "Pharmacist"

    def to_dict(self):
        """Minimal payload for the session cookie (never the password)."""
        return {"user_id": self.user_id, "name": self.name, "role": self.role}


# ============================================================================
# LOW-LEVEL FILE HELPERS
# ============================================================================

def _atomic_write(path, text):
    """
    Write via a temp file in the same directory, then os.replace().

    The previous code opened the real file with mode "w", which truncates
    first -- so any crash mid-write left a truncated or empty data file.
    os.replace() is atomic on both Windows and POSIX.
    """
    directory = os.path.dirname(path) or "."
    fd, tmp = tempfile.mkstemp(dir=directory, prefix=".tmp_", suffix=".txt")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(text)
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise


def _append_line(path, line):
    with open(path, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def clean_field(value, label, maxlen=60):
    """
    Validate a single free-text field destined for a comma-delimited file.

    Commas are rejected rather than quoted on purpose: the C++ reader parses
    with getline(ss, field, ',') and has no quoting support, so a quoted field
    would corrupt the shared format. Returns (value, error_message).
    """
    value = (value or "").strip()
    if not value:
        return None, "%s is required." % label
    if "," in value:
        return None, "%s may not contain a comma." % label
    if len(value) > maxlen:
        return None, "%s must be %d characters or fewer." % (label, maxlen)
    if "\n" in value or "\r" in value:
        return None, "%s may not contain line breaks." % label
    return value, None


# ============================================================================
# DATE HELPERS -- one implementation, used everywhere
# ============================================================================

def parse_date(text):
    """Parse YYYY-MM-DD (or DD-MM-YYYY) into a date, or None if invalid."""
    text = (text or "").strip()
    for fmt in ("%Y-%m-%d", "%d-%m-%Y"):
        try:
            return datetime.strptime(text, fmt).date()
        except ValueError:
            continue
    return None


def is_expired(expiry_text):
    """
    True only if the expiry date is strictly before today.

    Compares date to date. The old version compared a midnight-truncated
    datetime against datetime.today() (a wall-clock time), which made an item
    expiring TODAY report as already expired, losing its final day of shelf
    life. An unparseable date is treated as not-expired here and surfaced via
    the 'expiry_valid' flag instead of raising -- a single bad value used to
    take down every page in the app.
    """
    expiry = parse_date(expiry_text)
    if expiry is None:
        return False
    return expiry < date.today()


def today_str():
    return date.today().strftime("%Y-%m-%d")


def now_stamp():
    return datetime.now().strftime("[%Y-%m-%d %H:%M:%S]")


# ============================================================================
# INVENTORY DATA ACCESS (DAO pattern) -- ONE loader and ONE writer
#
# The previous version had two parsers (read_inventory -> "qty",
# load_inventory -> "quantity") and two writers. write_inventory filtered on
# the key "qty", so passing it a load_inventory() list silently produced an
# empty list and truncated inventory.txt to zero bytes while flashing success.
# Both shapes are now collapsed into the "quantity" schema the templates use.
#
# On-disk format (shared with prog2.cpp, do not change casually):
#     name,batch,expiry,quantity,price,orig
# ============================================================================

INVENTORY_FIELDS = ("name", "batch", "expiry", "quantity", "price", "orig")


def load_inventory():
    """
    Read inventory.txt into a list of medicine dicts.

    Malformed lines are skipped and counted rather than raising: the old
    tuple-unpack (a,b,c,d,e,f = line.split(',')) raised ValueError on any
    line that did not have exactly 6 fields.
    """
    medicines = []
    skipped = 0
    if not os.path.exists(INVENTORY_FILE):
        return medicines

    with open(INVENTORY_FILE, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) != 6:
                skipped += 1
                continue
            name, batch, expiry, qty, price, orig = (p.strip() for p in parts)
            try:
                quantity = int(qty)
                unit_price = float(price)
                original = int(orig)
            except ValueError:
                skipped += 1
                continue

            expiry_date = parse_date(expiry)
            medicines.append({
                "name": name,
                "batch": batch,
                "expiry": expiry,
                "expiry_date": expiry_date,
                "expiry_valid": expiry_date is not None,
                "quantity": quantity,
                "price": unit_price,
                "orig": original,
                "expired": expiry_date is not None and expiry_date < date.today(),
                "low_stock": 0 < quantity <= LOW_STOCK_THRESHOLD,
            })

    if skipped:
        try:
            g.inventory_skipped = skipped
        except RuntimeError:
            pass    # called outside a request context (tests)
    return medicines


def write_inventory(medicines):
    """Serialize the medicine list back to inventory.txt atomically."""
    rows = []
    for m in medicines:
        missing = [k for k in INVENTORY_FIELDS if k not in m]
        if missing:
            raise ValueError(
                "write_inventory() received a record missing required keys: %r"
                % (missing,)
            )
        rows.append("%s,%s,%s,%d,%s,%d\n" % (
            m["name"], m["batch"], m["expiry"],
            int(m["quantity"]), m["price"], int(m["orig"]),
        ))
    _atomic_write(INVENTORY_FILE, "".join(rows))


def find_medicine(medicines, batch):
    for m in medicines:
        if m["batch"] == batch:
            return m
    return None


# ============================================================================
# USER DATA ACCESS
# ============================================================================

def create_default_users():
    defaults = [
        "d001,Dr.Patil,Doctor,doc123",
        "d002,Dr.Chabra,Doctor,doc456",
        "p001,Ramesh Pharmacist,Pharmacist,pharm123",
        "p002,Suresh Pharmacist,Pharmacist,pharm456",
        "n001,Mary Nurse,Nurse,nurse123",
        "n002,Anuradha Nurse,Nurse,nurse456",
    ]
    _atomic_write(USERS_FILE, "\n".join(defaults) + "\n")


def load_users():
    """Load users.txt into {user_id: User}. Seeds defaults if absent."""
    if not os.path.exists(USERS_FILE):
        create_default_users()

    users = {}
    with open(USERS_FILE, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) != 4:
                continue
            user_id, name, role, password = (p.strip() for p in parts)
            if user_id:
                users[user_id] = User(user_id, name, role, password)
    return users


def save_users(users):
    rows = "".join(
        "%s,%s,%s,%s\n" % (u.user_id, u.name, u.role, u.password)
        for u in users.values()
    )
    _atomic_write(USERS_FILE, rows)


def generate_user_id(role):
    """
    Next free id for a role, e.g. p003.

    Ids that do not match <letter><digits> are ignored instead of crashing;
    the old int(uid[1:]) raised on any hand-edited id and broke /add_user.
    """
    users = load_users()
    prefix = role[0].lower()
    numbers = []
    pattern = re.compile(re.escape(prefix) + r"(\d+)$")
    for uid in users:
        match = pattern.match(uid)
        if match:
            numbers.append(int(match.group(1)))
    nxt = (max(numbers) + 1) if numbers else 1
    return "%s%03d" % (prefix, nxt)


def count_role(users, role):
    return sum(1 for u in users if u.role == role)


# ============================================================================
# AUDIT LOGGING -- one function, two destinations
# (log and log_user_activity used to be byte-identical clones.)
# ============================================================================

def _write_log(path, msg):
    user = get_current_user()
    who = "[%s - %s] " % (user.user_id, user.name) if user else "[SYSTEM] "
    _append_line(path, "%s %s%s" % (now_stamp(), who, msg))


def log(msg):
    """Transaction audit trail -> history.txt (shared with the C++ app)."""
    _write_log(HISTORY_FILE, msg)


def log_user_activity(msg):
    """Authentication audit trail -> user_history.txt."""
    _write_log(USER_HISTORY_FILE, msg)


HISTORY_RE = re.compile(
    r"^\[(?P<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\]\s*"
    r"(?:\[(?P<uid>[^\]\-]+?)\s*-\s*(?P<uname>[^\]]*)\]\s*)?"
    r"(?P<msg>.*)$"
)


def load_history(path=None, newest_first=True):
    """
    Parse an audit log into structured records.

    Replaces fixed-offset slicing (parts[1:20] / parts[21:]), which was wrong
    in three ways: the message body starts at index 22 not 21 (so every row
    rendered with a leading space); a line not starting with '[' still had its
    first 21 characters sliced off; and blank lines consumed index numbers.
    The [uid - name] block is optional because the older history lines predate
    user attribution.
    """
    path = path or HISTORY_FILE
    if not os.path.exists(path):
        return []

    records = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            match = HISTORY_RE.match(line)
            if match:
                records.append({
                    "timestamp": match.group("ts"),
                    "user_id": (match.group("uid") or "").strip(),
                    "user_name": (match.group("uname") or "").strip(),
                    "message": match.group("msg").strip(),
                    "full_text": line,
                })
            else:
                records.append({
                    "timestamp": "",
                    "user_id": "",
                    "user_name": "",
                    "message": line,
                    "full_text": line,
                })

    if newest_first:
        records.reverse()
    for i, rec in enumerate(records, 1):
        rec["index"] = i
    return records


# ============================================================================
# SESSION / AUTH PLUMBING
# ============================================================================

def get_current_user():
    """
    Resolve the logged-in User from users.txt (never from the cookie).

    Roles are re-read from disk so a tampered session cookie cannot escalate
    privileges. Cached on flask.g so a single request reads the file once
    instead of once per permission check and per log line.
    """
    if "user" not in session:
        return None
    if "current_user" not in g:
        g.current_user = load_users().get(session["user"]["user_id"])
    return g.current_user


def login_required(f):
    @wraps(f)
    def wrapper(*args, **kwargs):
        if "user" not in session:
            flash("Please login to access this page.", "warning")
            return redirect(url_for("login"))
        if get_current_user() is None:
            # The account was deleted while the cookie was still valid. The
            # old check only tested cookie presence, so routes rendered with
            # user=None and the nav silently collapsed.
            session.clear()
            flash("Your account no longer exists. Please login again.", "warning")
            return redirect(url_for("login"))
        return f(*args, **kwargs)
    return wrapper


def permission_required(permission_check):
    def decorator(f):
        @wraps(f)
        def wrapper(*args, **kwargs):
            user = get_current_user()
            if not user:
                flash("Please login to access this page.", "warning")
                return redirect(url_for("login"))
            if not permission_check(user):
                flash("Access denied: you do not have permission for that action.",
                      "danger")
                return redirect(url_for("index"))
            return f(*args, **kwargs)
        return wrapper
    return decorator


# --- CSRF protection (hand-rolled; Flask-WTF is not a dependency) -----------

def csrf_token():
    if "_csrf_token" not in session:
        session["_csrf_token"] = secrets.token_urlsafe(32)
    return session["_csrf_token"]


@app.context_processor
def inject_globals():
    return {"csrf_token": csrf_token, "current_year": date.today().year}


@app.before_request
def csrf_protect():
    if request.method not in ("POST", "PUT", "PATCH", "DELETE"):
        return None
    expected = session.get("_csrf_token")
    supplied = request.form.get("_csrf_token", "")
    if not expected or not supplied or not hmac.compare_digest(expected, supplied):
        flash("Your session expired or the form was invalid. Please try again.",
              "danger")
        return redirect(request.referrer or url_for("index"))
    return None


def allowed_file(filename):
    return "." in filename and \
        filename.rsplit(".", 1)[1].lower() in ALLOWED_EXTENSIONS


# ============================================================================
# FORM FIELD PARSERS
# Return (value, error). Routes flash the error and redirect, instead of the
# old bare int()/float() on raw form data which raised and produced a 500.
# ============================================================================

def form_int(field, label, minimum=None, maximum=None):
    raw = (request.form.get(field) or "").strip()
    if not raw:
        return None, "%s is required." % label
    try:
        value = int(raw)
    except ValueError:
        return None, "%s must be a whole number." % label
    if minimum is not None and value < minimum:
        return None, "%s must be at least %d." % (label, minimum)
    if maximum is not None and value > maximum:
        return None, "%s may not exceed %d." % (label, maximum)
    return value, None


def form_float(field, label, minimum=None):
    raw = (request.form.get(field) or "").strip()
    if not raw:
        return None, "%s is required." % label
    try:
        value = float(raw)
    except ValueError:
        return None, "%s must be a number." % label
    if value != value or value in (float("inf"), float("-inf")):
        return None, "%s must be a finite number." % label
    if minimum is not None and value < minimum:
        return None, "%s must be at least %s." % (label, minimum)
    return value, None


def form_date(field, label, required=True):
    raw = (request.form.get(field) or "").strip()
    if not raw:
        if required:
            return None, "%s is required." % label
        return None, None
    parsed = parse_date(raw)
    if parsed is None:
        return None, "%s must be a real calendar date (YYYY-MM-DD)." % label
    return parsed.strftime("%Y-%m-%d"), None


def inventory_stats(medicines):
    """
    Mutually exclusive dashboard buckets.

    The template used to compute these inline with overlapping tests:
    "Low Stock" ignored `expired`, so expired low-stock items were counted
    twice, and a non-expired item with quantity 0 fell into no bucket at all.
    """
    expired = [m for m in medicines if m["expired"]]
    fresh = [m for m in medicines if not m["expired"]]
    return {
        "total": len(medicines),
        "expired_count": len(expired),
        "out_of_stock": sum(1 for m in fresh if m["quantity"] == 0),
        "low_stock": sum(1 for m in fresh
                         if 0 < m["quantity"] <= LOW_STOCK_THRESHOLD),
        "in_stock": sum(1 for m in fresh
                        if m["quantity"] > LOW_STOCK_THRESHOLD),
        "invalid_dates": sum(1 for m in medicines if not m["expiry_valid"]),
    }


# ============================================================================
# AUTHENTICATION ROUTES
# ============================================================================

@app.route("/login", methods=["GET", "POST"])
def login():
    if "user" in session and get_current_user():
        return redirect(url_for("index"))

    if request.method == "POST":
        user_id = (request.form.get("user_id") or "").strip()
        password = request.form.get("password") or ""
        users = load_users()
        account = users.get(user_id)

        if account and account.verify_password(password):
            # Drop any pre-login session state (session fixation), then
            # establish the authenticated session.
            session.clear()
            session["user"] = account.to_dict()
            g.current_user = account
            flash("Welcome, %s!" % account.name, "success")
            log_user_activity("User logged in")
            return redirect(url_for("index"))
        flash("Invalid User ID or Password.", "danger")

    return render_template("login.html")


@app.route("/logout")
def logout():
    if "user" in session:
        log_user_activity("User logged out")
        # session.clear() also drops 'last_bill'. The old code popped only
        # 'user', so the next person to log in could open /view_bill and see
        # the previous user's bill.
        session.clear()
        flash("Logged out successfully.", "success")
    return redirect(url_for("login"))


# ============================================================================
# DASHBOARD & INVENTORY VIEWS
# ============================================================================

@app.route("/")
@login_required
def index():
    """OOP CONCEPT: CONTROLLER METHOD (MVC) -- loads model, returns view."""
    user = get_current_user()
    medicines = load_inventory()
    stats = inventory_stats(medicines)
    return render_template("index.html", medicines=medicines, user=user,
                           stats=stats, expired_count=stats["expired_count"],
                           threshold=LOW_STOCK_THRESHOLD)


@app.route("/add", methods=["GET", "POST"])
@login_required
@permission_required(lambda user: user.can_add_medicine())
def add():
    """OOP CONCEPT: CRUD - CREATE, gated by role permissions."""
    user = get_current_user()
    medicines = load_inventory()

    if request.method == "POST":
        name, err_name = clean_field(request.form.get("name"), "Medicine name")
        batch, err_batch = clean_field(request.form.get("batch"), "Batch number", 30)
        expiry, err_expiry = form_date("expiry", "Expiry date")
        qty, err_qty = form_int("qty", "Quantity", minimum=0)
        price, err_price = form_float("price", "Price", minimum=0)

        error = err_name or err_batch or err_expiry or err_qty or err_price
        if not error and find_medicine(medicines, batch):
            error = ("Batch %s already exists. Batch numbers must be unique -- "
                     "use Restock to add stock to it." % batch)

        if error:
            flash(error, "danger")
            return redirect(url_for("add"))

        medicines.append({
            "name": name, "batch": batch, "expiry": expiry,
            "quantity": qty, "price": price, "orig": qty,
        })
        write_inventory(medicines)
        log("Added medicine: %s (%s), qty=%d, price=%.2f" % (name, batch, qty, price))
        flash("Medicine added successfully.", "success")
        return redirect(url_for("index"))

    # medicines is passed so the "Quick Search existing medicines" widget on
    # the form actually has data; the old route omitted it, leaving the
    # dropdown permanently empty behind an {% if medicines is defined %} guard.
    return render_template("add.html", user=user, medicines=medicines,
                           today=today_str())


@app.route("/update", methods=["GET", "POST"])
@login_required
@permission_required(lambda user: user.can_update_medicine())
def update():
    """OOP CONCEPT: CRUD - UPDATE (replaces the stock count outright)."""
    user = get_current_user()
    medicines = load_inventory()

    if request.method == "POST":
        batch = (request.form.get("batch") or "").strip()
        new_qty, err_qty = form_int("qty", "Quantity", minimum=0)
        new_expiry, err_expiry = form_date("expiry", "Expiry date")

        error = err_qty or err_expiry
        if error:
            flash(error, "danger")
            return redirect(url_for("update"))

        target = find_medicine(medicines, batch)
        # The old loop set found=True but never read it, so a nonexistent
        # batch still flashed "Stock updated successfully!". It also had no
        # break, so it mutated every matching batch.
        if target is None:
            flash("Medicine not found.", "danger")
            return redirect(url_for("update"))

        target["quantity"] = new_qty
        target["expiry"] = new_expiry
        if new_qty > target["orig"]:
            target["orig"] = new_qty
        write_inventory(medicines)
        log("Updated medicine: %s (%s), new qty=%d" % (target["name"], batch, new_qty))
        flash("Stock updated successfully.", "success")
        return redirect(url_for("index"))

    return render_template("update.html", medicines=medicines, user=user,
                           today=today_str())


@app.route("/restock", methods=["GET", "POST"])
@login_required
@permission_required(lambda user: user.can_update_medicine())
def restock():
    """OOP CONCEPT: STATE MODIFICATION -- adds to the existing quantity."""
    user = get_current_user()
    medicines = load_inventory()

    if request.method == "POST":
        batch = (request.form.get("batch") or "").strip()
        add_qty, err_qty = form_int("qty", "Quantity to add", minimum=1)
        new_expiry, err_expiry = form_date("expiry", "New expiry date",
                                           required=False)

        error = err_qty or err_expiry
        if error:
            flash(error, "danger")
            return redirect(url_for("restock"))

        target = find_medicine(medicines, batch)
        if target is None:
            # Return before writing. The old code rewrote the whole file even
            # on a not-found no-op.
            flash("Medicine not found.", "danger")
            return redirect(url_for("restock"))

        was_expired = target["expired"]
        if was_expired and not new_expiry:
            flash("%s is expired. Provide a new expiry date to restock it."
                  % target["name"], "danger")
            return redirect(url_for("restock"))

        target["quantity"] += add_qty
        # Keep orig >= quantity. Not updating it is why live data showed a
        # quantity of 1,100,000 against an "original" of 500.
        if target["quantity"] > target["orig"]:
            target["orig"] = target["quantity"]
        if new_expiry:
            target["expiry"] = new_expiry

        write_inventory(medicines)
        log("Restocked medicine: %s (%s), added qty=%d, new total=%d, new expiry=%s"
            % (target["name"], batch, add_qty, target["quantity"],
               new_expiry if new_expiry else "unchanged"))
        flash("Restocked %s: added %d units." % (target["name"], add_qty), "success")
        return redirect(url_for("index"))

    return render_template("restock.html", medicines=medicines, user=user,
                           today=today_str())


@app.route("/remove_expired", methods=["POST"])
@login_required
@permission_required(lambda user: user.can_remove_expired())
def remove_expired():
    """
    OOP CONCEPT: SOFT DELETE -- sets quantity to 0, keeps the record.

    POST-only now. This used to be a GET reachable from a plain <a href>,
    so any page embedding the URL (or a link prefetcher) could zero live
    stock with no confirmation.
    """
    medicines = load_inventory()
    removed = 0
    # Uses the same is_expired rule as every other view. The old version did a
    # lexicographic string compare here, which disagreed with the dashboard --
    # /expired would count N items and this route would then clear none.
    for m in medicines:
        if m["expired"] and m["quantity"] > 0:
            log("Marked as expired (qty set to 0): %s (%s), previous qty=%d"
                % (m["name"], m["batch"], m["quantity"]))
            m["quantity"] = 0
            removed += 1

    if removed:
        write_inventory(medicines)
        flash("Marked %d expired medicine(s) as out of stock." % removed, "success")
    else:
        flash("No expired medicines with stock found.", "info")
    return redirect(url_for("index"))


@app.route("/lowstock")
@login_required
def lowstock():
    """OOP CONCEPT: FILTERING -- excludes expired items, which belong on /expired."""
    user = get_current_user()
    medicines = load_inventory()
    low_stock = [m for m in medicines
                 if not m["expired"] and m["quantity"] <= LOW_STOCK_THRESHOLD]
    return render_template("lowstock.html", low_stock=low_stock, user=user,
                           threshold=LOW_STOCK_THRESHOLD)


@app.route("/expired")
@login_required
def expired():
    user = get_current_user()
    medicines = load_inventory()
    expired_items = [m for m in medicines if m["expired"]]
    return render_template("expired.html", expired=expired_items, user=user)


# ============================================================================
# SALES / BILLING
# ============================================================================

@app.route("/buy", methods=["GET", "POST"])
@login_required
@permission_required(lambda user: user.can_buy_medicine())
def buy():
    """OOP CONCEPT: TRANSACTION METHOD -- validates, mutates state, logs."""
    user = get_current_user()
    medicines = load_inventory()

    if request.method == "POST":
        batch = (request.form.get("batch") or "").strip()
        # minimum=1 closes a real hole: a negative quantity passed the
        # "enough stock" test, then `quantity -= qty` ADDED stock and issued a
        # bill with a negative total.
        qty, err_qty = form_int("qty", "Quantity", minimum=1)
        if err_qty:
            flash(err_qty, "danger")
            return redirect(url_for("buy"))

        target = find_medicine(medicines, batch)
        if target is None:
            flash("Medicine not found.", "danger")
            return redirect(url_for("buy"))
        if target["expired"]:
            flash("%s has expired and cannot be sold." % target["name"], "danger")
            return redirect(url_for("buy"))
        if target["quantity"] < qty:
            flash("Insufficient stock. Available: %d units." % target["quantity"],
                  "danger")
            return redirect(url_for("buy"))

        details = {
            "name": target["name"], "batch": target["batch"],
            "expiry": target["expiry"], "price": target["price"],
            "qty_sold": qty, "old_qty": target["quantity"],
        }
        target["quantity"] -= qty
        total = qty * target["price"]
        write_inventory(medicines)

        log("Sold %d of %s (%s), total=%s%.2f"
            % (qty, target["name"], batch, RUPEE, total))

        session["last_bill"] = {
            "medicine": details,
            "total": total,
            "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "served_by": user.name,
            "user_id": user.user_id,
        }
        flash("Sale complete. Total: %s%.2f" % (RUPEE, total), "success")
        return redirect(url_for("view_bill"))

    sellable = [m for m in medicines if m["quantity"] > 0 and not m["expired"]]
    return render_template("buy.html", medicines=medicines, sellable=sellable,
                           user=user)


@app.route("/view_bill")
@login_required
def view_bill():
    user = get_current_user()
    if "last_bill" not in session:
        flash("No recent bill found. Please make a sale first.", "warning")
        return redirect(url_for("buy"))
    return render_template("bill.html", bill=session["last_bill"], user=user)


@app.route("/download_bill")
@login_required
def download_bill():
    if "last_bill" not in session:
        flash("No recent bill found.", "warning")
        return redirect(url_for("buy"))

    bill = session["last_bill"]
    med = bill["medicine"]
    rule = "=" * 80
    thin = "-" * 80
    bill_text = "\n".join([
        rule,
        "                    MEDICAL INVENTORY MANAGEMENT SYSTEM",
        "                            PURCHASE BILL",
        rule,
        "",
        "Bill Date/Time: %s" % bill["timestamp"],
        "Served By: %s (ID: %s)" % (bill["served_by"], bill["user_id"]),
        "",
        thin,
        "                            MEDICINE DETAILS",
        thin,
        "",
        "Medicine Name:      %s" % med["name"],
        "Batch Number:       %s" % med["batch"],
        "Expiry Date:        %s" % med["expiry"],
        "Unit Price:         %s%.2f" % (RUPEE, med["price"]),
        "Quantity Sold:      %d units" % med["qty_sold"],
        "",
        thin,
        "                            PAYMENT SUMMARY",
        thin,
        "",
        "Subtotal:           %s%.2f" % (RUPEE, bill["total"]),
        "Tax (0%%):           %s0.00" % RUPEE,
        "                    --------------------------------",
        "Total Amount:       %s%.2f" % (RUPEE, bill["total"]),
        "",
        thin,
        "",
        "Stock After Sale:   %d units" % (med["old_qty"] - med["qty_sold"]),
        "",
        "                    Thank you for your business!",
        "",
        rule,
        "",
    ])

    output = make_response(bill_text)
    filename = "bill_%s.txt" % bill["timestamp"].replace(":", "-").replace(" ", "_")
    output.headers["Content-Disposition"] = 'attachment; filename="%s"' % filename
    # charset matters: the body contains the rupee sign, and overwriting
    # Content-Type without it left browsers guessing (mojibake).
    output.headers["Content-Type"] = "text/plain; charset=utf-8"
    log("Bill downloaded - %s (Batch: %s)" % (med["name"], med["batch"]))
    return output


# ============================================================================
# HISTORY
# ============================================================================

@app.route("/history")
@login_required
def history():
    """Transaction log plus the authentication log, which previously had no UI."""
    user = get_current_user()
    return render_template("history.html", user=user,
                           history=load_history(HISTORY_FILE),
                           activity=load_history(USER_HISTORY_FILE))


@app.route("/backup_history")
@login_required
def backup_history():
    if not os.path.exists(HISTORY_FILE):
        flash("No history file found.", "warning")
        return redirect(url_for("history"))
    log("Transaction history backup downloaded")
    return send_file(
        HISTORY_FILE, as_attachment=True,
        download_name="history_backup_%s.txt" % datetime.now().strftime("%Y%m%d_%H%M%S"),
        mimetype="text/plain",
    )


# ============================================================================
# API ENDPOINTS (RESTful interface)
# Both now require a session. They used to be completely unauthenticated,
# exposing the full inventory to anyone who knew the URL.
# ============================================================================

@app.route("/api/medicines")
@login_required
def api_medicines():
    return jsonify([_api_view(m) for m in load_inventory()])


@app.route("/api/medicine/<batch>")
@login_required
def api_medicine(batch):
    target = find_medicine(load_inventory(), batch)
    if target is None:
        return jsonify({"error": "Medicine not found"}), 404
    return jsonify(_api_view(target))


def _api_view(med):
    """Serializable projection (drops the internal date object)."""
    return {k: v for k, v in med.items() if k != "expiry_date"}


# ============================================================================
# BACKUP AND RESTORE
# ============================================================================

CSV_HEADER = ["Medicine Name", "Batch Number", "Expiry Date",
              "Quantity", "Price", "Original Quantity"]


@app.route("/backup")
@login_required
def backup():
    user = get_current_user()
    return render_template("backup.html", medicines=load_inventory(), user=user)


@app.route("/download_backup")
@login_required
def download_backup():
    medicines = load_inventory()
    si = io.StringIO()
    writer = csv.writer(si)
    writer.writerow(CSV_HEADER)
    for med in medicines:
        writer.writerow([med["name"], med["batch"], med["expiry"],
                         med["quantity"], med["price"], med["orig"]])

    output = make_response(si.getvalue())
    filename = "inventory_backup_%s.csv" % datetime.now().strftime("%Y%m%d_%H%M%S")
    output.headers["Content-Disposition"] = 'attachment; filename="%s"' % filename
    output.headers["Content-Type"] = "text/csv; charset=utf-8"
    log("Inventory backup downloaded - %d medicines exported" % len(medicines))
    # No flash() here: the response is a file download, so the message would
    # have surfaced out of context on whatever page the user visited next.
    return output


@app.route("/restore", methods=["GET", "POST"])
@login_required
@permission_required(lambda user: user.can_manage_users())
def restore():
    """
    OOP CONCEPT: DESERIALIZATION with validation.
    Restricted to Pharmacists; replaces the whole inventory, so it takes a
    safety backup first.
    """
    user = get_current_user()
    if request.method != "POST":
        return render_template("restore.html", user=user)

    if "file" not in request.files or request.files["file"].filename == "":
        flash("No file selected.", "danger")
        return redirect(url_for("restore"))

    upload = request.files["file"]
    filename = secure_filename(upload.filename)
    if not allowed_file(filename):
        flash("Invalid file type. Please upload a CSV file.", "danger")
        return redirect(url_for("restore"))
    if not request.form.get("confirm"):
        flash("Please tick the confirmation box before restoring.", "warning")
        return redirect(url_for("restore"))

    # Narrow decode/parse handling. The old blanket `except Exception` around
    # this whole block reported UnicodeDecodeError, PermissionError, csv.Error
    # and genuine bugs alike as "Error processing file".
    try:
        text = upload.stream.read().decode("utf-8-sig")
    except UnicodeDecodeError:
        flash("File is not valid UTF-8 text.", "danger")
        return redirect(url_for("restore"))

    try:
        rows = list(csv.reader(io.StringIO(text, newline=None)))
    except csv.Error as exc:
        flash("Could not parse CSV: %s" % exc, "danger")
        return redirect(url_for("restore"))

    if not rows:
        flash("CSV file is empty.", "warning")
        return redirect(url_for("restore"))
    if [h.strip() for h in rows[0]] != CSV_HEADER:
        flash("Invalid CSV format. Header must be: %s" % ", ".join(CSV_HEADER),
              "danger")
        return redirect(url_for("restore"))

    imported = []
    seen_batches = set()
    for line_no, row in enumerate(rows[1:], start=2):
        if not any(cell.strip() for cell in row):
            continue
        if len(row) != 6:
            flash("Row %d: expected 6 columns, got %d." % (line_no, len(row)),
                  "danger")
            return redirect(url_for("restore"))

        name, err = clean_field(row[0], "Row %d medicine name" % line_no)
        if err:
            flash(err, "danger")
            return redirect(url_for("restore"))
        batch, err = clean_field(row[1], "Row %d batch number" % line_no, 30)
        if err:
            flash(err, "danger")
            return redirect(url_for("restore"))
        if batch in seen_batches:
            flash("Row %d: duplicate batch number %s." % (line_no, batch), "danger")
            return redirect(url_for("restore"))
        seen_batches.add(batch)

        expiry = parse_date(row[2])
        if expiry is None:
            flash("Row %d: expiry '%s' must be YYYY-MM-DD or DD-MM-YYYY."
                  % (line_no, row[2].strip()), "danger")
            return redirect(url_for("restore"))

        try:
            quantity = int(row[3])
            price = float(row[4])
            orig = int(row[5])
        except ValueError:
            flash("Row %d: quantity, price and original quantity must be numeric."
                  % line_no, "danger")
            return redirect(url_for("restore"))

        if quantity < 0 or price < 0 or orig < 0:
            flash("Row %d: negative values are not allowed." % line_no, "danger")
            return redirect(url_for("restore"))

        imported.append({
            "name": name, "batch": batch,
            "expiry": expiry.strftime("%Y-%m-%d"),
            "quantity": quantity, "price": price,
            "orig": max(orig, quantity),
        })

    if not imported:
        flash("CSV file contains no data rows.", "warning")
        return redirect(url_for("restore"))

    try:
        backup_path = _safety_backup()
        write_inventory(imported)
    except OSError as exc:
        flash("Could not write inventory: %s" % exc, "danger")
        return redirect(url_for("restore"))

    backup_name = os.path.basename(backup_path) if backup_path else "none"
    log("Inventory restored from CSV - %d medicines imported (backup: %s)"
        % (len(imported), backup_name))
    flash("Inventory restored: %d medicines imported. Previous inventory saved "
          "to backups/%s." % (len(imported), backup_name), "success")
    return redirect(url_for("index"))


def _safety_backup():
    """
    Copy the current inventory into backups/ before it is overwritten.

    Microsecond precision because the old second-resolution name meant two
    restores in the same second silently overwrote the same backup. These also
    used to land in the app root, mixed in with the live data files.
    """
    if not os.path.exists(INVENTORY_FILE):
        return None
    os.makedirs(BACKUP_DIR, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    path = os.path.join(BACKUP_DIR, "inventory_backup_%s.txt" % stamp)
    with open(INVENTORY_FILE, "r", encoding="utf-8") as src:
        content = src.read()
    # encoding is explicit here; this was the one write in the file that
    # omitted it, so a non-ASCII medicine name raised UnicodeEncodeError on
    # Windows -- swallowed by the blanket handler AFTER the user was told a
    # backup had been taken.
    with open(path, "w", encoding="utf-8") as dst:
        dst.write(content)
    return path


# ============================================================================
# USER MANAGEMENT (Pharmacist only)
# ============================================================================

@app.route("/manage_users")
@login_required
@permission_required(lambda user: user.can_manage_users())
def manage_users():
    user = get_current_user()
    users_list = sorted(load_users().values(), key=lambda u: (u.role, u.user_id))
    return render_template("manage_users.html", users=users_list, user=user,
                           doctors_count=count_role(users_list, "Doctor"),
                           pharmacists_count=count_role(users_list, "Pharmacist"),
                           nurses_count=count_role(users_list, "Nurse"))


@app.route("/add_user", methods=["GET", "POST"])
@login_required
@permission_required(lambda user: user.can_manage_users())
def add_user():
    current = get_current_user()
    if request.method == "POST":
        name, err = clean_field(request.form.get("name"), "Name", 40)
        role = (request.form.get("role") or "").strip()
        password = request.form.get("password") or ""

        if err:
            flash(err, "danger")
        elif role not in VALID_ROLES:
            flash("Invalid role selected.", "danger")
        elif len(password) < 6:
            flash("Password must be at least 6 characters.", "danger")
        elif "," in password:
            flash("Password may not contain a comma.", "danger")
        else:
            users = load_users()
            user_id = generate_user_id(role)
            users[user_id] = User(user_id, name, role, password)
            save_users(users)
            log("New user added: %s - %s (%s)" % (user_id, name, role))
            flash("User added successfully. User ID: %s" % user_id, "success")
            return redirect(url_for("manage_users"))
        return redirect(url_for("add_user"))

    return render_template("add_user.html", user=current)


@app.route("/edit_user/<user_id>", methods=["GET", "POST"])
@login_required
@permission_required(lambda user: user.can_manage_users())
def edit_user(user_id):
    current = get_current_user()
    users = load_users()
    if user_id not in users:
        flash("User not found.", "danger")
        return redirect(url_for("manage_users"))

    target = users[user_id]
    if request.method == "POST":
        name, err = clean_field(request.form.get("name"), "Name", 40)
        role = (request.form.get("role") or "").strip()
        password = request.form.get("password") or ""

        if err:
            flash(err, "danger")
            return redirect(url_for("edit_user", user_id=user_id))
        if role not in VALID_ROLES:
            flash("Invalid role selected.", "danger")
            return redirect(url_for("edit_user", user_id=user_id))

        # Guard the last Pharmacist. Without this a Pharmacist could demote
        # themselves (or the only other one) and permanently lock everyone out
        # of user management -- delete_user already guarded self-deletion.
        if target.role == "Pharmacist" and role != "Pharmacist":
            remaining = [u for u in users.values()
                         if u.role == "Pharmacist" and u.user_id != user_id]
            if not remaining:
                flash("Cannot change the role of the last Pharmacist -- no one "
                      "would be able to manage users.", "danger")
                return redirect(url_for("edit_user", user_id=user_id))

        if password:
            if len(password) < 6:
                flash("Password must be at least 6 characters.", "danger")
                return redirect(url_for("edit_user", user_id=user_id))
            if "," in password:
                flash("Password may not contain a comma.", "danger")
                return redirect(url_for("edit_user", user_id=user_id))
            target.password = password

        target.name = name
        target.role = role
        save_users(users)
        log("User updated: %s - %s (%s)" % (user_id, name, role))

        if user_id == current.user_id:
            # Refresh the cached session copy so the nav reflects the new role.
            session["user"] = target.to_dict()
        flash("User %s updated successfully." % user_id, "success")
        return redirect(url_for("manage_users"))

    return render_template("edit_user.html", user=current, target_user=target)


@app.route("/delete_user/<user_id>", methods=["POST"])
@login_required
@permission_required(lambda user: user.can_manage_users())
def delete_user(user_id):
    current = get_current_user()
    if user_id == current.user_id:
        flash("You cannot delete your own account.", "danger")
        return redirect(url_for("manage_users"))

    users = load_users()
    if user_id not in users:
        flash("User not found.", "danger")
        return redirect(url_for("manage_users"))

    if users[user_id].role == "Pharmacist":
        remaining = [u for u in users.values()
                     if u.role == "Pharmacist" and u.user_id != user_id]
        if not remaining:
            flash("Cannot delete the last Pharmacist.", "danger")
            return redirect(url_for("manage_users"))

    removed = users.pop(user_id)
    save_users(users)
    log("User deleted: %s - %s (%s)" % (user_id, removed.name, removed.role))
    flash("User %s (%s) deleted." % (user_id, removed.name), "success")
    return redirect(url_for("manage_users"))


# ============================================================================
# ERROR HANDLERS
# ============================================================================

@app.errorhandler(404)
def handle_404(error):
    return render_template("error.html", code=404,
                           title="Page not found",
                           message="That page does not exist.",
                           user=get_current_user()), 404


@app.errorhandler(413)
def handle_413(error):
    return render_template("error.html", code=413,
                           title="File too large",
                           message="Uploads are limited to 16 MB.",
                           user=get_current_user()), 413


@app.errorhandler(500)
def handle_500(error):
    app.logger.exception("Unhandled server error")
    return render_template("error.html", code=500,
                           title="Something went wrong",
                           message="An unexpected error occurred. The details "
                                   "were written to the server log.",
                           user=get_current_user()), 500


# ============================================================================
# APPLICATION ENTRY POINT
# ============================================================================
if __name__ == "__main__":
    # debug defaults OFF. The Werkzeug debugger is a remote-code-execution
    # surface; opt in explicitly with FLASK_DEBUG=1 while developing.
    debug_mode = os.environ.get("FLASK_DEBUG", "").lower() in ("1", "true", "yes")
    app.run(debug=debug_mode, port=int(os.environ.get("PORT", 5000)))
