"""
================================================================================
    TEST SUITE - MEDICAL INVENTORY MANAGEMENT SYSTEM
================================================================================
    Run from the project folder:

        python tests/test_app.py

    Exits 0 if everything passes, 1 otherwise, so it can be used in a script.

    This replaces six earlier scripts (diagnose_routes.py, system_check.py,
    test_data_loading.py, test_date_conversion.py, test_website.py and
    verify_data_visibility.py). None of them was a real test:

      * test_website.py's helper returned True on BOTH branches, so a failing
        check still counted as a pass -- its "17/17 passed" meant nothing.
      * test_data_loading.py tested its own copy-pasted copy of load_inventory,
        so it could not detect a regression in app.py at all.
      * verify_data_visibility.py printed a hardcoded "FIXES APPLIED" list and
        shelled out to g++, leaving a stray executable behind.

    Every test here imports the real functions from app.py, asserts, and runs
    against a TEMPORARY COPY of the data files -- it never touches the live
    inventory.txt, users.txt or history.txt.
================================================================================
"""

import os
import re
import shutil
import sys
import tempfile
import traceback
from datetime import date, timedelta

# Import app.py from the parent folder regardless of where this is run from.
HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
sys.path.insert(0, PROJECT)

import app as application  # noqa: E402


# ---------------------------------------------------------------------------
# Tiny test harness
# ---------------------------------------------------------------------------

_results = []


def test(name):
    """Decorator that registers a test function."""
    def wrap(fn):
        _results.append((name, fn))
        return fn
    return wrap


def today(offset_days=0):
    return (date.today() + timedelta(days=offset_days)).strftime("%Y-%m-%d")


class Sandbox:
    """
    Redirects app.py's module-level file paths at a throwaway directory, so
    tests can write freely without touching the real data.
    """

    FIELDS = ("INVENTORY_FILE", "USERS_FILE", "HISTORY_FILE",
              "USER_HISTORY_FILE", "BACKUP_DIR")

    def __init__(self, inventory_rows=None, users_rows=None, history_lines=None):
        self.inventory_rows = inventory_rows
        self.users_rows = users_rows
        self.history_lines = history_lines
        self.dir = None
        self.saved = {}

    def __enter__(self):
        self.dir = tempfile.mkdtemp(prefix="mims_test_")
        for field in self.FIELDS:
            self.saved[field] = getattr(application, field)

        application.INVENTORY_FILE = os.path.join(self.dir, "inventory.txt")
        application.USERS_FILE = os.path.join(self.dir, "users.txt")
        application.HISTORY_FILE = os.path.join(self.dir, "history.txt")
        application.USER_HISTORY_FILE = os.path.join(self.dir, "user_history.txt")
        application.BACKUP_DIR = os.path.join(self.dir, "backups")

        if self.inventory_rows is not None:
            self._write(application.INVENTORY_FILE, self.inventory_rows)
        if self.users_rows is not None:
            self._write(application.USERS_FILE, self.users_rows)
        else:
            application.create_default_users()
        if self.history_lines is not None:
            self._write(application.HISTORY_FILE, self.history_lines)
        return self

    def __exit__(self, *exc):
        for field, value in self.saved.items():
            setattr(application, field, value)
        shutil.rmtree(self.dir, ignore_errors=True)
        return False

    @staticmethod
    def _write(path, lines):
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + "\n")


SAMPLE = [
    "Paracetamol,1004,%s,9000,3.0,9000" % today(400),
    "Cofcil,1007,%s,8,12.0,10000" % today(400),
    "OldStock,1099,%s,50,4.0,50" % today(-30),      # expired
    "ExpiresToday,1098,%s,20,2.5,20" % today(0),    # boundary case
]


# ---------------------------------------------------------------------------
# Dates
# ---------------------------------------------------------------------------

@test("is_expired: an item expiring TODAY is not expired")
def t_expiry_today():
    assert application.is_expired(today(0)) is False


@test("is_expired: yesterday is expired, tomorrow is not")
def t_expiry_bounds():
    assert application.is_expired(today(-1)) is True
    assert application.is_expired(today(1)) is False


@test("is_expired: an unreadable date does not raise")
def t_expiry_garbage():
    # A single bad value used to make every page in the app raise on load.
    for bad in ("notadate", "", "2025-13-45", "2025-02-30", "31/12/2025"):
        assert application.is_expired(bad) is False, bad


@test("parse_date: accepts both formats, rejects impossible dates")
def t_parse_date():
    assert application.parse_date("2027-12-31") == date(2027, 12, 31)
    assert application.parse_date("31-12-2027") == date(2027, 12, 31)
    assert application.parse_date("2028-02-29") == date(2028, 2, 29)  # leap
    assert application.parse_date("2027-02-29") is None               # not leap
    assert application.parse_date("2027-13-01") is None
    assert application.parse_date("2027-00-10") is None
    assert application.parse_date("garbage") is None


# ---------------------------------------------------------------------------
# Inventory round-trip -- the data-loss regression
# ---------------------------------------------------------------------------

@test("inventory round-trip preserves every row")
def t_round_trip():
    with Sandbox(inventory_rows=SAMPLE):
        before = application.load_inventory()
        assert len(before) == 4, len(before)
        application.write_inventory(before)
        after = application.load_inventory()
        assert len(after) == 4, "round-trip lost rows: %d" % len(after)
        assert [m["batch"] for m in before] == [m["batch"] for m in after]
        assert [m["quantity"] for m in before] == [m["quantity"] for m in after]


@test("write_inventory rejects a legacy qty-keyed record instead of wiping the file")
def t_no_silent_wipe():
    """
    The original code had two parsers with different key names and a writer
    that filtered on "qty". Handing it a load_inventory() list (keyed
    "quantity") produced an empty list and truncated inventory.txt to zero
    bytes, while still flashing success. It must now fail loudly.
    """
    with Sandbox(inventory_rows=SAMPLE):
        legacy = [{"name": "X", "batch": "1", "expiry": today(10),
                   "qty": 5, "price": 1.0, "orig": 5}]
        try:
            application.write_inventory(legacy)
        except ValueError as exc:
            assert "quantity" in str(exc), str(exc)
        else:
            raise AssertionError("write_inventory accepted a qty-keyed record")
        # And the file on disk must be untouched.
        assert len(application.load_inventory()) == 4


@test("load_inventory skips malformed lines without raising")
def t_malformed_lines():
    rows = list(SAMPLE) + [
        "TooFewFields,1200,2027-01-01",
        "Bad Qty,1201,2027-01-01,notanumber,1.0,5",
        "Comma,Injected,1202,2027-01-01,5,1.0,5",
        "",
    ]
    with Sandbox(inventory_rows=rows):
        meds = application.load_inventory()
        assert len(meds) == 4, "expected 4 good rows, got %d" % len(meds)


@test("expiry flags: expired, low_stock and expiry_valid are computed")
def t_flags():
    rows = list(SAMPLE) + ["NoDate,1300,not-a-date,10,1.0,10"]
    with Sandbox(inventory_rows=rows):
        by_batch = {m["batch"]: m for m in application.load_inventory()}
        assert by_batch["1099"]["expired"] is True
        assert by_batch["1098"]["expired"] is False      # expires today
        assert by_batch["1007"]["low_stock"] is True     # qty 8
        assert by_batch["1004"]["low_stock"] is False    # qty 9000
        assert by_batch["1300"]["expiry_valid"] is False
        assert by_batch["1300"]["expired"] is False


@test("inventory_stats buckets are mutually exclusive and sum to the total")
def t_stats_partition():
    rows = list(SAMPLE) + ["Empty,1400,%s,0,1.0,10" % today(100)]
    with Sandbox(inventory_rows=rows):
        stats = application.inventory_stats(application.load_inventory())
        parts = (stats["in_stock"] + stats["low_stock"]
                 + stats["out_of_stock"] + stats["expired_count"])
        assert parts == stats["total"], (
            "buckets %d != total %d" % (parts, stats["total"]))


# ---------------------------------------------------------------------------
# Text validation (protects the shared comma-separated format)
# ---------------------------------------------------------------------------

@test("clean_field rejects commas, blanks and over-long text")
def t_clean_field():
    assert application.clean_field("Paracetamol", "Name")[0] == "Paracetamol"
    assert application.clean_field("Vitamin C, 500mg", "Name")[1] is not None
    assert application.clean_field("   ", "Name")[1] is not None
    assert application.clean_field("x" * 99, "Name", 60)[1] is not None
    assert application.clean_field("  Spaced  ", "Name")[0] == "Spaced"


# ---------------------------------------------------------------------------
# History parsing
# ---------------------------------------------------------------------------

@test("load_history parses attributed, legacy and malformed lines")
def t_history_formats():
    lines = [
        "[2025-10-10 08:24:33] Added medicine: Dolo (1001), qty=10000",
        "[2025-11-14 11:30:32] [p001 - Ramesh Pharmacist] Sold 100 of Dolo (1002)",
        "a line with no timestamp at all",
        "",
    ]
    with Sandbox(inventory_rows=SAMPLE, history_lines=lines):
        recs = application.load_history(newest_first=False)
        assert len(recs) == 3, len(recs)

        legacy, attributed, orphan = recs
        assert legacy["timestamp"] == "2025-10-10 08:24:33"
        assert legacy["user_id"] == ""
        assert legacy["message"].startswith("Added medicine: Dolo")

        assert attributed["user_id"] == "p001"
        assert attributed["user_name"] == "Ramesh Pharmacist"
        assert attributed["message"].startswith("Sold 100 of Dolo")

        assert orphan["timestamp"] == ""
        # The old fixed-offset slicing chopped the first 21 characters off any
        # line that did not start with "[".
        assert orphan["message"] == "a line with no timestamp at all"


@test("load_history strips the leading space from every message")
def t_history_no_leading_space():
    lines = ["[2025-11-14 11:30:32] [p001 - Ramesh] Sold 100 of Dolo (1002)"]
    with Sandbox(inventory_rows=SAMPLE, history_lines=lines):
        for rec in application.load_history():
            assert rec["message"] == rec["message"].lstrip()


@test("load_history numbers only non-blank lines and can reverse order")
def t_history_numbering():
    lines = ["[2025-01-0%d 00:00:00] entry %d" % (i, i) for i in range(1, 4)]
    lines.insert(2, "")
    with Sandbox(inventory_rows=SAMPLE, history_lines=lines):
        asc = application.load_history(newest_first=False)
        assert [r["index"] for r in asc] == [1, 2, 3]
        desc = application.load_history(newest_first=True)
        assert desc[0]["message"] == "entry 3"


# ---------------------------------------------------------------------------
# Users and permissions
# ---------------------------------------------------------------------------

@test("users round-trip through save/load")
def t_users_round_trip():
    with Sandbox(inventory_rows=SAMPLE):
        users = application.load_users()
        assert len(users) == 6, len(users)
        application.save_users(users)
        again = application.load_users()
        assert sorted(again) == sorted(users)
        assert again["p001"].verify_password("pharm123")
        assert not again["p001"].verify_password("wrong")


@test("permission matrix matches the documented roles")
def t_permissions():
    doctor = application.User("d001", "D", "Doctor", "x")
    pharmacist = application.User("p001", "P", "Pharmacist", "x")
    nurse = application.User("n001", "N", "Nurse", "x")

    expected = {
        "can_add_medicine":    (True,  True,  False),
        "can_update_medicine": (True,  True,  False),
        "can_buy_medicine":    (False, True,  True),
        "can_remove_expired":  (False, True,  False),
        "can_manage_users":    (False, True,  False),
    }
    for method, (doc, pharm, nur) in expected.items():
        assert getattr(doctor, method)() is doc, method
        assert getattr(pharmacist, method)() is pharm, method
        assert getattr(nurse, method)() is nur, method


@test("generate_user_id ignores ids it cannot parse")
def t_generate_user_id():
    rows = [
        "p001,A,Pharmacist,x",
        "p007,B,Pharmacist,x",
        "pBOGUS,C,Pharmacist,x",   # used to raise ValueError and break /add_user
        "d001,D,Doctor,x",
    ]
    with Sandbox(inventory_rows=SAMPLE, users_rows=rows):
        assert application.generate_user_id("Pharmacist") == "p008"
        assert application.generate_user_id("Doctor") == "d002"
        assert application.generate_user_id("Nurse") == "n001"


# ---------------------------------------------------------------------------
# HTTP layer
# ---------------------------------------------------------------------------

def _client():
    application.app.config.update(TESTING=True)
    return application.app.test_client()


def _csrf(client, path):
    html = client.get(path).get_data(as_text=True)
    match = re.search(r'name="_csrf_token" value="([^"]+)"', html)
    assert match, "no CSRF token found on %s" % path
    return match.group(1)


def _login(client, user_id="p001", password="pharm123"):
    return client.post("/login", data={
        "user_id": user_id, "password": password,
        "_csrf_token": _csrf(client, "/login"),
    })


@test("protected routes redirect to /login when signed out")
def t_auth_required():
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        for path in ("/", "/add", "/buy", "/history", "/manage_users",
                     "/api/medicines", "/api/medicine/1004"):
            response = client.get(path)
            assert response.status_code == 302, "%s -> %s" % (path, response.status_code)
            assert "/login" in response.headers.get("Location", ""), path


@test("every signed-in page renders")
def t_pages_render():
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)
        for path in ("/", "/add", "/update", "/restock", "/buy", "/lowstock",
                     "/expired", "/history", "/backup", "/restore",
                     "/manage_users", "/add_user", "/edit_user/d001",
                     "/api/medicines", "/api/medicine/1004",
                     "/download_backup", "/backup_history"):
            response = client.get(path)
            assert response.status_code == 200, "%s -> %s" % (path, response.status_code)


@test("a POST without a CSRF token is rejected and changes nothing")
def t_csrf_required():
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)
        response = client.post("/add", data={
            "name": "Hacked", "batch": "6660",
            "expiry": today(30), "qty": "1", "price": "1",
        })
        assert response.status_code == 302
        assert application.find_medicine(application.load_inventory(), "6660") is None


@test("invalid form input flashes an error and leaves the app usable")
def t_bad_input_does_not_500():
    """
    These inputs used to raise an uncaught exception. The bad-date case was the
    worst: it was written to disk and then made EVERY page raise on load.
    """
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)
        bad_payloads = [
            {"name": "A", "batch": "7001", "expiry": "notadate", "qty": "5", "price": "2"},
            {"name": "A", "batch": "7002", "expiry": today(30), "qty": "abc", "price": "2"},
            {"name": "A", "batch": "7003", "expiry": today(30), "qty": "5", "price": "xyz"},
            {"name": "Has,Comma", "batch": "7004", "expiry": today(30), "qty": "5", "price": "2"},
            {"name": "A", "batch": "1004", "expiry": today(30), "qty": "5", "price": "2"},
        ]
        for payload in bad_payloads:
            payload["_csrf_token"] = _csrf(client, "/add")
            response = client.post("/add", data=payload, follow_redirects=True)
            assert response.status_code == 200, payload

        # Nothing was written, and every page still works.
        assert len(application.load_inventory()) == 4
        for path in ("/", "/lowstock", "/expired", "/buy", "/api/medicines"):
            assert client.get(path).status_code == 200, path


@test("add, restock, sell and update apply correctly")
def t_happy_path():
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)

        def post(path, **data):
            data["_csrf_token"] = _csrf(client, path)
            return client.post(path, data=data, follow_redirects=True)

        post("/add", name="Zyrtec", batch="9001", expiry=today(365),
             qty="250", price="12.50")
        med = application.find_medicine(application.load_inventory(), "9001")
        assert med is not None and med["quantity"] == 250 and med["orig"] == 250

        post("/restock", batch="9001", qty="100")
        med = application.find_medicine(application.load_inventory(), "9001")
        assert med["quantity"] == 350
        # orig must track quantity; not doing so is why the live data showed a
        # quantity of 1,100,000 against an "original" of 500.
        assert med["orig"] == 350, med["orig"]

        post("/buy", batch="9001", qty="50")
        med = application.find_medicine(application.load_inventory(), "9001")
        assert med["quantity"] == 300

        post("/update", batch="9001", qty="77", expiry=today(400))
        med = application.find_medicine(application.load_inventory(), "9001")
        assert med["quantity"] == 77


@test("selling a negative quantity cannot inflate stock")
def t_negative_sale():
    """qty=-5 used to pass the stock check, then `quantity -= qty` ADDED
       stock and issued a bill with a negative total."""
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)
        before = application.find_medicine(application.load_inventory(), "1004")["quantity"]
        client.post("/buy", data={"batch": "1004", "qty": "-5",
                                  "_csrf_token": _csrf(client, "/buy")},
                    follow_redirects=True)
        after = application.find_medicine(application.load_inventory(), "1004")["quantity"]
        assert after == before, "stock changed from %d to %d" % (before, after)


@test("an expired medicine cannot be sold")
def t_no_selling_expired():
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)
        client.post("/buy", data={"batch": "1099", "qty": "1",
                                  "_csrf_token": _csrf(client, "/buy")},
                    follow_redirects=True)
        med = application.find_medicine(application.load_inventory(), "1099")
        assert med["quantity"] == 50, "expired stock was sold"


@test("updating a nonexistent batch reports failure instead of success")
def t_update_missing_batch():
    """The old route set found=True but never read it, so it flashed
       'Stock updated successfully!' for a batch that did not exist."""
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)
        response = client.post("/update",
                               data={"batch": "DOESNOTEXIST", "qty": "5",
                                     "expiry": today(30),
                                     "_csrf_token": _csrf(client, "/update")},
                               follow_redirects=True)
        body = response.get_data(as_text=True)
        assert "not found" in body.lower(), "no failure message shown"


@test("clearing expired stock is a soft delete that keeps the records")
def t_soft_delete():
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)
        client.post("/remove_expired",
                    data={"_csrf_token": _csrf(client, "/expired")},
                    follow_redirects=True)
        meds = application.load_inventory()
        assert len(meds) == 4, "records were deleted, not zeroed"
        assert application.find_medicine(meds, "1099")["quantity"] == 0


@test("role permissions are enforced on routes")
def t_route_permissions():
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client, "n001", "nurse123")          # Nurse
        # A Nurse may sell but not add, restore or manage users.
        assert client.get("/buy").status_code == 200
        for path in ("/add", "/restore", "/manage_users"):
            response = client.get(path)
            assert response.status_code == 302, "%s -> %s" % (path, response.status_code)


@test("logout clears the session, including the last bill")
def t_logout_clears_bill():
    """logout() used to pop only 'user', so the next person to sign in could
       open /view_bill and see the previous user's bill."""
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)
        client.post("/buy", data={"batch": "1004", "qty": "1",
                                  "_csrf_token": _csrf(client, "/buy")},
                    follow_redirects=True)
        assert client.get("/view_bill").status_code == 200
        client.get("/logout")
        _login(client, "n001", "nurse123")
        response = client.get("/view_bill")
        assert response.status_code == 302, "bill leaked across sessions"


@test("the last Pharmacist cannot be demoted or deleted")
def t_last_pharmacist_guard():
    rows = ["p001,Solo,Pharmacist,pharm123", "d001,Doc,Doctor,doc123"]
    with Sandbox(inventory_rows=SAMPLE, users_rows=rows):
        client = _client()
        _login(client)
        client.post("/edit_user/p001",
                    data={"name": "Solo", "role": "Nurse",
                          "_csrf_token": _csrf(client, "/edit_user/p001")},
                    follow_redirects=True)
        users = application.load_users()
        assert users["p001"].role == "Pharmacist", "demoted the last Pharmacist"


@test("the bill download is UTF-8 and carries the rupee sign")
def t_bill_encoding():
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)
        client.post("/buy", data={"batch": "1004", "qty": "2",
                                  "_csrf_token": _csrf(client, "/buy")},
                    follow_redirects=True)
        response = client.get("/download_bill")
        assert response.status_code == 200
        assert "charset=utf-8" in response.headers.get("Content-Type", "")
        assert "₹".encode("utf-8") in response.data


@test("the CSV backup round-trips through restore")
def t_backup_restore_round_trip():
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)
        csv_bytes = client.get("/download_backup").data
        assert csv_bytes.splitlines()[0].decode("utf-8").split(",")[0] == "Medicine Name"

        import io as _io
        response = client.post("/restore", data={
            "file": (_io.BytesIO(csv_bytes), "backup.csv"),
            "confirm": "yes",
            "_csrf_token": _csrf(client, "/restore"),
        }, content_type="multipart/form-data", follow_redirects=True)
        assert response.status_code == 200
        assert len(application.load_inventory()) == 4, "restore lost rows"


@test("restore accepts DD-MM-YYYY and normalises it")
def t_restore_date_normalisation():
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)
        csv_text = (
            "Medicine Name,Batch Number,Expiry Date,Quantity,Price,Original Quantity\n"
            "Dolo,1002,31-12-2027,20000,5,20000\n"
        )
        import io as _io
        client.post("/restore", data={
            "file": (_io.BytesIO(csv_text.encode("utf-8")), "b.csv"),
            "confirm": "yes",
            "_csrf_token": _csrf(client, "/restore"),
        }, content_type="multipart/form-data", follow_redirects=True)
        meds = application.load_inventory()
        assert len(meds) == 1, meds
        assert meds[0]["expiry"] == "2027-12-31", meds[0]["expiry"]


@test("restore rejects a bad file and leaves the inventory untouched")
def t_restore_rejects_bad_csv():
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        _login(client)
        import io as _io
        for payload in (b"Wrong,Header\n1,2\n",
                        b"Medicine Name,Batch Number,Expiry Date,Quantity,"
                        b"Price,Original Quantity\nA,1,notadate,1,1,1\n"):
            client.post("/restore", data={
                "file": (_io.BytesIO(payload), "b.csv"),
                "confirm": "yes",
                "_csrf_token": _csrf(client, "/restore"),
            }, content_type="multipart/form-data", follow_redirects=True)
            assert len(application.load_inventory()) == 4, "bad CSV was imported"


@test("unknown URLs render the 404 page")
def t_404():
    with Sandbox(inventory_rows=SAMPLE):
        client = _client()
        assert client.get("/no/such/page").status_code == 404


# ---------------------------------------------------------------------------
# Project layout
# ---------------------------------------------------------------------------

@test("every template a route renders actually exists")
def t_templates_present():
    folder = os.path.join(PROJECT, "Templates")
    required = [
        "base.html", "index.html", "login.html", "add.html", "update.html",
        "restock.html", "buy.html", "bill.html", "lowstock.html",
        "expired.html", "history.html", "backup.html", "restore.html",
        "manage_users.html", "add_user.html", "edit_user.html", "error.html",
    ]
    missing = [name for name in required
               if not os.path.exists(os.path.join(folder, name))]
    assert not missing, "missing templates: %s" % missing


@test("static assets exist")
def t_static_present():
    for rel in ("static/css/style.css", "static/js/app.js"):
        assert os.path.exists(os.path.join(PROJECT, rel)), rel


@test("every POST form carries a CSRF token")
def t_forms_have_csrf():
    folder = os.path.join(PROJECT, "Templates")
    offenders = []
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".html"):
            continue
        with open(os.path.join(folder, name), encoding="utf-8") as handle:
            text = handle.read()
        if text.count('method="POST"') > text.count("_csrf_token"):
            offenders.append(name)
    assert not offenders, "POST forms without a CSRF token: %s" % offenders


@test("the stylesheet does not reintroduce the Bootstrap-breaking overrides")
def t_css_no_regressions():
    path = os.path.join(PROJECT, "static", "css", "style.css")
    with open(path, encoding="utf-8") as handle:
        css = handle.read()
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)   # ignore comments

    assert "table-responsive" not in css, \
        ".table-responsive override is back; it makes tables clip, not scroll"
    assert not re.search(r"\.btn\s*\{[^}]*padding\s*:", css), \
        ".btn padding override is back; it disables btn-sm / btn-lg"
    assert not re.search(r"\.card\s*\{[^}]*border\s*:\s*none", css), \
        ".card { border: none } is back; it kills the border-* utilities"
    assert not re.search(r"\.card-header\s*\{[^}]*linear-gradient", css), \
        "global .card-header gradient is back; it paints over bg-* utilities"
    for token in (".stat-card.info", ".icon-box.info"):
        assert token in css, "%s is used by a template but not defined" % token


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def main():
    print("=" * 74)
    print("  MEDICAL INVENTORY MANAGEMENT SYSTEM - TEST SUITE")
    print("=" * 74)

    passed, failed = 0, []
    for name, fn in _results:
        try:
            fn()
        except Exception as exc:                      # noqa: BLE001
            failed.append((name, exc, traceback.format_exc()))
            print("  FAIL  %s" % name)
            print("        %s: %s" % (type(exc).__name__, exc))
        else:
            passed += 1
            print("  ok    %s" % name)

    print("-" * 74)
    print("  %d passed, %d failed, %d total" % (passed, len(failed), len(_results)))
    print("=" * 74)

    if failed:
        print("\nFailure details:\n")
        for name, _exc, tb in failed:
            print("--- %s ---" % name)
            print(tb)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
