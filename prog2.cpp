/*
================================================================================
    MEDICAL INVENTORY MANAGEMENT SYSTEM v2.1 - C++ CONSOLE APPLICATION
================================================================================
    Documentation: see README.md in this folder.

    Compile: g++ -std=c++11 -Wall -Wextra prog2.cpp -o prog2.exe
    Run:     prog2.exe

    This program shares its data files (inventory.txt, users.txt, history.txt)
    with the Flask web application (app.py). The on-disk formats are a contract
    between the two programs:

        inventory.txt : name,batch,expiry,quantity,price,originalQuantity
        users.txt     : userId,name,role,password
        history.txt   : [YYYY-MM-DD HH:MM:SS] [userId - Name] message

    Fields are comma-separated with no quoting, so commas are rejected in any
    text field -- see validateText().

    OOP Concepts: Encapsulation, Data Hiding, Abstraction, Composition,
                  Constructor Overloading, Static Factory Methods,
                  Exception Handling, RBAC
================================================================================
*/

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <tuple>
#include <limits>
#include <map>
#include <algorithm>
#include <stdexcept>
#include <cstdio>     // sscanf -- prog1 included this; prog2 had dropped it and
                      // only compiled because libstdc++ pulls it in indirectly.

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

// ============================================================================
// CONSOLE / TEXT UTILITIES
// ============================================================================

// U+20B9 INDIAN RUPEE SIGN as explicit UTF-8 bytes, so this source file does
// not depend on the compiler's input-charset assumptions.
static const char *RUPEE = "\xE2\x82\xB9";

/*
    Enable UTF-8 output on Windows.

    This program draws its entire UI with box-drawing characters and uses the
    rupee sign throughout. The file is UTF-8, but a default cmd.exe/PowerShell
    console runs in code page 437/1252, so every frame character rendered as
    two or three pieces of mojibake. Setting the output code page fixes the
    whole UI at once.
*/
static void initConsole()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

/*
    Display width of a UTF-8 string, in characters rather than bytes.

    std::setw() counts bytes, so a 3-byte glyph such as the rupee sign made
    every money column two cells too wide and pushed the table borders out of
    alignment. All table and box formatting below pads through these helpers
    instead of setw().
*/
static size_t displayWidth(const string &s)
{
    size_t width = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) != 0x80)   // skip UTF-8 continuation bytes
            ++width;
    }
    return width;
}

// Repeat a (possibly multi-byte) unit n times.
static string repeat(const string &unit, size_t n)
{
    string out;
    out.reserve(unit.size() * n);
    for (size_t i = 0; i < n; ++i)
        out += unit;
    return out;
}

// Cut a string to `width` characters, appending an ellipsis when it overflows.
// setw() pads but never truncates, so a long user name used to push the menu
// border off the frame.
static string clip(const string &s, size_t width)
{
    if (displayWidth(s) <= width)
        return s;
    string out;
    size_t seen = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) != 0x80)
        {
            if (seen == width - 1)
                break;
            ++seen;
        }
        out += s[i];
    }
    return out + "~";
}

static string padRight(const string &s, size_t width)
{
    string t = clip(s, width);
    size_t w = displayWidth(t);
    return w >= width ? t : t + string(width - w, ' ');
}

static string padLeft(const string &s, size_t width)
{
    string t = clip(s, width);
    size_t w = displayWidth(t);
    return w >= width ? t : string(width - w, ' ') + t;
}

// Money formatting, always two decimals with the currency sign attached.
static string money(double value)
{
    ostringstream oss;
    oss << fixed << setprecision(2) << value;
    return string(RUPEE) + oss.str();
}

// Plain two-decimal number, for writing to the shared data/log files.
static string decimals2(double value)
{
    ostringstream oss;
    oss << fixed << setprecision(2) << value;
    return oss.str();
}

// ============================================================================
// BOX DRAWING
// Every frame is built from these so the borders always line up. The old menu
// mixed 55- and 56-character rows inside a 56-character frame, and the table
// rules were 5 characters shorter than the columns they underlined.
// ============================================================================

static const size_t BOX_W = 62;   // inner width of every framed box

static void boxTop()    { cout << "╔" << repeat("═", BOX_W) << "╗\n"; }
static void boxSep()    { cout << "╠" << repeat("═", BOX_W) << "╣\n"; }
static void boxBottom() { cout << "╚" << repeat("═", BOX_W) << "╝\n"; }

static void boxLine(const string &text)
{
    cout << "║" << padRight("  " + text, BOX_W) << "║\n";
}

static void banner(const string &title, const string &subtitle = "")
{
    boxTop();
    boxLine(title);
    if (!subtitle.empty())
        boxLine(subtitle);
    boxBottom();
}

// ============================================================================
// SAFE INPUT
// The old code read with `cin >> x` and never recovered from a failed
// extraction. prog1 had no cin.fail() handling at all, so typing a letter at
// the menu spun it forever; prog2 recovered from a typo but not from EOF,
// because clear()+ignore() immediately re-set eofbit.
// ============================================================================

static void discardLine()
{
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

// True when the stream is finished (piped input exhausted, or Ctrl+Z/Ctrl+D).
static bool inputClosed()
{
    return cin.eof();
}

static int readInt(const string &prompt, bool *ok = nullptr)
{
    while (true)
    {
        cout << prompt;
        int value;
        if (cin >> value)
        {
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            if (ok) *ok = true;
            return value;
        }
        if (cin.eof())
        {
            if (ok) *ok = false;
            return 0;
        }
        discardLine();
        cout << "  Please enter a whole number.\n";
    }
}

static double readDouble(const string &prompt, bool *ok = nullptr)
{
    while (true)
    {
        cout << prompt;
        double value;
        if (cin >> value)
        {
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            if (ok) *ok = true;
            return value;
        }
        if (cin.eof())
        {
            if (ok) *ok = false;
            return 0.0;
        }
        discardLine();
        cout << "  Please enter a number.\n";
    }
}

// Reads a whole line. Callers never mix this with `cin >>` without the
// intervening ignore() that readInt/readDouble/readWord already perform, so
// the classic "getline reads the leftover newline" bug cannot occur.
static string readLine(const string &prompt)
{
    cout << prompt;
    string line;
    if (!getline(cin, line))
        return string();
    return line;
}

static string readWord(const string &prompt)
{
    cout << prompt;
    string word;
    if (!(cin >> word))
        return string();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    return word;
}

static void pause(const string &prompt = "\nPress Enter to continue...")
{
    cout << prompt;
    if (!cin.eof())
    {
        string ignored;
        getline(cin, ignored);
    }
    cout << "\n";
}

// ============================================================================
// DATE HANDLING
// A plain calendar date with real validation. The previous implementation fed
// sscanf output straight to mktime(), which silently NORMALISES nonsense:
// 2025-13-45 became 2026-01-14 and 2025-02-30 became March 2. Worse, an
// unparseable date produced (time_t)-1 and was then treated as "never
// expires", hiding the item from the expired report forever.
// ============================================================================

struct Date
{
    int year;
    int month;
    int day;

    Date() : year(0), month(0), day(0) {}
    Date(int y, int m, int d) : year(y), month(m), day(d) {}
};

static bool isLeapYear(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int daysInMonth(int year, int month)
{
    static const int lengths[12] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && isLeapYear(year))
        return 29;
    return lengths[month - 1];
}

// Strict YYYY-MM-DD parse with full calendar validation (leap years included).
static bool parseDate(const string &text, Date &out)
{
    int y = 0, m = 0, d = 0;
    char extra = '\0';
    if (sscanf(text.c_str(), "%d-%d-%d%c", &y, &m, &d, &extra) != 3)
        return false;
    if (y < 1900 || y > 9999 || m < 1 || m > 12)
        return false;
    if (d < 1 || d > daysInMonth(y, m))
        return false;
    out = Date(y, m, d);
    return true;
}

static Date todayDate()
{
    time_t now = time(nullptr);
    tm *local = localtime(&now);
    if (local == nullptr)        // localtime can fail; it was never checked
        return Date(1970, 1, 1);
    return Date(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday);
}

static bool dateBefore(const Date &a, const Date &b)
{
    if (a.year != b.year)   return a.year < b.year;
    if (a.month != b.month) return a.month < b.month;
    return a.day < b.day;
}

static string currentTimestamp()
{
    time_t now = time(nullptr);
    tm *local = localtime(&now);
    if (local == nullptr)
        return "[0000-00-00 00:00:00]";
    ostringstream oss;
    oss << "[" << put_time(local, "%Y-%m-%d %H:%M:%S") << "]";
    return oss.str();
}

static string timestampPlain()
{
    time_t now = time(nullptr);
    tm *local = localtime(&now);
    if (local == nullptr)
        return "0000-00-00 00:00:00";
    ostringstream oss;
    oss << put_time(local, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

/*
    Reject text that would corrupt the comma-separated data files.

    The Flask app applies the same rule (clean_field in app.py). Without it, a
    medicine named "Vitamin C, 500mg" wrote a 7-field line that shifted every
    subsequent column on read -- silent data corruption with no error.
*/
static bool validateText(const string &value, const string &label,
                         size_t maxLen = 60)
{
    if (value.empty())
    {
        cout << "  " << label << " cannot be empty.\n";
        return false;
    }
    if (value.find(',') != string::npos)
    {
        cout << "  " << label << " may not contain a comma "
             << "(the data file is comma-separated).\n";
        return false;
    }
    if (displayWidth(value) > maxLen)
    {
        cout << "  " << label << " must be " << maxLen
             << " characters or fewer.\n";
        return false;
    }
    return true;
}

// ============================================================================
// User Class
// ============================================================================
/*
OOP CONCEPT: ENCAPSULATION & DATA HIDING
- Private data members, access only through the public interface
- Bundles identity with the permission rules that depend on it

NOTE ON PASSWORDS: stored in plain text because users.txt is shared with the
Flask app and this program has no hashing support. A documented limitation of
the academic project -- see README.md.
*/
class User
{
private:
    string userId;      // d001 (Doctor), p001 (Pharmacist), n001 (Nurse)
    string name;
    string role;        // Doctor | Pharmacist | Nurse
    string password;

public:
    // OOP CONCEPT: CONSTRUCTOR OVERLOADING
    User() {}

    User(const string &id, const string &n, const string &r, const string &pass)
        : userId(id), name(n), role(r), password(pass) {}

    // OOP CONCEPT: GETTERS (const accessors)
    string getUserId() const { return userId; }
    string getName() const { return name; }
    string getRole() const { return role; }
    string getPassword() const { return password; }

    void setName(const string &n) { name = n; }
    void setRole(const string &r) { role = r; }
    void setPassword(const string &p) { password = p; }

    bool verifyPassword(const string &pass) const { return password == pass; }

    // --- role-based permissions ---
    bool canAddMedicine() const    { return role == "Doctor" || role == "Pharmacist"; }
    bool canBuyMedicine() const    { return role == "Pharmacist" || role == "Nurse"; }
    bool canUpdateMedicine() const { return role == "Doctor" || role == "Pharmacist"; }
    bool canRemoveExpired() const  { return role == "Pharmacist"; }
    bool canManageUsers() const    { return role == "Pharmacist"; }
    bool canViewReports() const    { return true; }

    bool isValidRole() const
    {
        return role == "Doctor" || role == "Pharmacist" || role == "Nurse";
    }

    void display() const
    {
        cout << "  " << padRight(userId, 8)
             << padRight(name, 26)
             << padRight(role, 14) << "\n";
    }

    // OOP CONCEPT: OBJECT SERIALIZATION
    void saveToFile(ofstream &out) const
    {
        out << userId << "," << name << "," << role << "," << password << "\n";
    }

    // OOP CONCEPT: STATIC FACTORY METHOD
    // Returns false for a malformed line instead of fabricating a user with an
    // empty id (map::operator[] used to default-insert a users[""] account).
    static bool loadFromFile(const string &line, User &out)
    {
        stringstream ss(line);
        string id, n, r, pass;
        if (!getline(ss, id, ',')) return false;
        if (!getline(ss, n, ',')) return false;
        if (!getline(ss, r, ',')) return false;
        if (!getline(ss, pass)) return false;
        if (id.empty()) return false;
        out = User(id, n, r, pass);
        return true;
    }
};

// ============================================================================
// UserManager Class
// ============================================================================
/*
OOP CONCEPT: COMPOSITION & AUTHENTICATION
- Owns a map of User objects and the authentication state
*/
class UserManager
{
private:
    map<string, User> users;
    string currentUserId;     // id rather than a raw pointer, so clearing or
                              // reloading the map can never leave a dangler
    bool fileReadable;

public:
    UserManager() : fileReadable(false)
    {
        loadUsers();
        /*
            Seed defaults ONLY when the file genuinely does not exist.

            The old condition was `if (users.empty())`, which also fired when
            the file existed but could not be read (locked by the Flask app,
            or permission-denied) -- and then overwrote it with the six
            built-in accounts, destroying any user created on the web side.
        */
        if (users.empty() && !fileReadable)
            createDefaultUsers();
    }

    void loadUsers()
    {
        users.clear();
        currentUserId.clear();
        ifstream in("users.txt");
        if (!in)
        {
            fileReadable = false;
            return;
        }
        fileReadable = true;

        string line;
        int lineNo = 0, skipped = 0;
        while (getline(in, line))
        {
            ++lineNo;
            if (line.empty())
                continue;
            User user;
            if (loadUser(line, user))
                users[user.getUserId()] = user;
            else
                ++skipped;
        }
        if (skipped > 0)
            cout << "  Note: skipped " << skipped
                 << " malformed line(s) in users.txt.\n";
    }

    static bool loadUser(const string &line, User &out)
    {
        return User::loadFromFile(line, out);
    }

    bool saveUsers()
    {
        ofstream out("users.txt");
        if (!out)
        {
            // Write failures used to be invisible: the program reported
            // success regardless of whether anything reached disk.
            cout << "  ERROR: cannot write users.txt (is it open elsewhere?)\n";
            return false;
        }
        for (map<string, User>::const_iterator it = users.begin();
             it != users.end(); ++it)
            it->second.saveToFile(out);
        return out.good();
    }

    void createDefaultUsers()
    {
        users["d001"] = User("d001", "Dr.Patil", "Doctor", "doc123");
        users["d002"] = User("d002", "Dr.Chabra", "Doctor", "doc456");
        users["p001"] = User("p001", "Ramesh Pharmacist", "Pharmacist", "pharm123");
        users["p002"] = User("p002", "Suresh Pharmacist", "Pharmacist", "pharm456");
        users["n001"] = User("n001", "Mary Nurse", "Nurse", "nurse123");
        users["n002"] = User("n002", "Anuradha Nurse", "Nurse", "nurse456");
        saveUsers();
        cout << "\n  No users.txt found - created one with the default accounts.\n"
             << "  Default login: p001 / pharm123\n\n";
    }

    // Returns false only when input is exhausted, so main can exit cleanly
    // instead of looping. The old login() ran once and killed the process on
    // a single typo.
    bool login()
    {
        const int maxAttempts = 3;
        for (int attempt = 1; attempt <= maxAttempts; ++attempt)
        {
            banner("LOGIN", "Attempt " + to_string(attempt) + " of " +
                            to_string(maxAttempts));
            string id = readWord("  User ID (e.g. d001, p001, n001): ");
            if (id.empty() && inputClosed())
                return false;
            string pass = readWord("  Password: ");
            if (pass.empty() && inputClosed())
                return false;

            map<string, User>::iterator it = users.find(id);
            if (it != users.end() && it->second.verifyPassword(pass))
            {
                currentUserId = id;
                cout << "\n  Login successful. Welcome, "
                     << it->second.getName() << ".\n";
                return true;
            }
            cout << "\n  Invalid User ID or password.\n";
        }
        cout << "\n  Too many failed attempts.\n";
        return false;
    }

    void logout()
    {
        currentUserId.clear();
        cout << "  Logged out.\n";
    }

    User *getCurrentUser()
    {
        if (currentUserId.empty())
            return nullptr;
        map<string, User>::iterator it = users.find(currentUserId);
        return it == users.end() ? nullptr : &it->second;
    }

    bool isLoggedIn() const { return !currentUserId.empty(); }

    int countRole(const string &role) const
    {
        int n = 0;
        for (map<string, User>::const_iterator it = users.begin();
             it != users.end(); ++it)
            if (it->second.getRole() == role)
                ++n;
        return n;
    }

    // Generate the next free id for a role, e.g. p003.
    string nextUserId(const string &role) const
    {
        char prefix = static_cast<char>(tolower(role[0]));
        int highest = 0;
        for (map<string, User>::const_iterator it = users.begin();
             it != users.end(); ++it)
        {
            const string &id = it->first;
            if (id.size() < 2 || id[0] != prefix)
                continue;
            bool allDigits = true;
            for (size_t i = 1; i < id.size(); ++i)
                if (!isdigit(static_cast<unsigned char>(id[i])))
                    allDigits = false;
            if (!allDigits)
                continue;
            int n = atoi(id.substr(1).c_str());
            if (n > highest)
                highest = n;
        }
        ostringstream oss;
        oss << prefix << setw(3) << setfill('0') << (highest + 1);
        return oss.str();
    }

    /*
        Add a user. This method was fully implemented in the previous version
        but had NO menu entry, so the feature was unreachable.
    */
    void addUser()
    {
        User *me = getCurrentUser();
        if (me == nullptr || !me->canManageUsers())
        {
            cout << "  Access denied. Required role: Pharmacist\n";
            return;
        }

        cout << "\n--- Add User ---\n";
        string name = readLine("  Full name: ");
        if (!validateText(name, "Name", 40))
            return;

        string role = readWord("  Role (Doctor/Pharmacist/Nurse): ");
        if (role != "Doctor" && role != "Pharmacist" && role != "Nurse")
        {
            cout << "  Invalid role. Must be Doctor, Pharmacist or Nurse.\n";
            return;
        }

        string pass = readWord("  Password (min 6 chars): ");
        if (pass.size() < 6)
        {
            cout << "  Password must be at least 6 characters.\n";
            return;
        }
        if (!validateText(pass, "Password", 40))
            return;

        string id = nextUserId(role);
        users[id] = User(id, name, role, pass);
        if (saveUsers())
            cout << "  User created. User ID: " << id << "\n";
    }

    void listUsers() const
    {
        cout << "\n--- Registered Users (" << users.size() << ") ---\n";
        cout << "  " << padRight("ID", 8) << padRight("Name", 26)
             << padRight("Role", 14) << "\n";
        cout << "  " << string(48, '-') << "\n";
        for (map<string, User>::const_iterator it = users.begin();
             it != users.end(); ++it)
            it->second.display();
        cout << "\n  Doctors: " << countRole("Doctor")
             << " | Pharmacists: " << countRole("Pharmacist")
             << " | Nurses: " << countRole("Nurse") << "\n";
    }
};

// ============================================================================
// Medicine Class
// ============================================================================
/*
OOP CONCEPT: CLASS DEFINITION & DATA HIDING
- Private members protect the invariants; access is through getters/setters
*/
class Medicine
{
private:
    string name;
    string batchNumber;
    string expiryDate;        // YYYY-MM-DD
    int quantity;
    double price;             // double, not float: see the note on sell totals
    int originalQuantity;

public:
    // OOP CONCEPT: CONSTRUCTOR OVERLOADING
    Medicine() : quantity(0), price(0.0), originalQuantity(0) {}

    Medicine(const string &n, const string &b, const string &e,
             int q, double p)
        : name(n), batchNumber(b), expiryDate(e),
          quantity(q), price(p), originalQuantity(q) {}

    // OOP CONCEPT: ACCESSORS
    string getName() const { return name; }
    string getBatchNumber() const { return batchNumber; }
    string getExpiryDate() const { return expiryDate; }
    int getQuantity() const { return quantity; }
    double getPrice() const { return price; }
    int getOriginalQuantity() const { return originalQuantity; }

    // OOP CONCEPT: MUTATORS with validation
    void setQuantity(int q)
    {
        quantity = q < 0 ? 0 : q;
        // Keep originalQuantity >= quantity. Not maintaining this is why the
        // live data showed a quantity of 1,100,000 against an "original" of
        // 500 -- a meaningless 220,000% stock level.
        if (quantity > originalQuantity)
            originalQuantity = quantity;
    }

    void setExpiryDate(const string &e) { expiryDate = e; }
    void setPrice(double p) { if (p >= 0.0) price = p; }

    void addStock(int extra)
    {
        if (extra <= 0)
            return;
        quantity += extra;
        if (quantity > originalQuantity)
            originalQuantity = quantity;
    }

    bool hasValidExpiry() const
    {
        Date parsed;
        return parseDate(expiryDate, parsed);
    }

    /*
        OOP CONCEPT: BUSINESS LOGIC METHOD

        An item expiring TODAY is NOT expired -- it keeps its final day of
        shelf life. The old version compared mktime() of midnight against
        time(nullptr), so anything expiring today read as expired from
        00:00:01. The Flask app uses the same rule (is_expired in app.py).
    */
    bool isExpired() const
    {
        Date expiry;
        if (!parseDate(expiryDate, expiry))
            return false;        // reported separately via hasValidExpiry()
        return dateBefore(expiry, todayDate());
    }

    void display() const
    {
        cout << "  " << padRight(name, 22)
             << padRight(batchNumber, 12)
             << padRight(expiryDate + (hasValidExpiry() ? "" : " (?)"), 14)
             << padLeft(to_string(quantity), 9) << "  "
             << padLeft(money(price), 11) << "  "
             << padLeft(to_string(originalQuantity), 9) << "\n";
    }

    static void displayHeader()
    {
        cout << "  " << padRight("Name", 22)
             << padRight("Batch", 12)
             << padRight("Expiry", 14)
             << padLeft("QtyLeft", 9) << "  "
             << padLeft("Price", 11) << "  "
             << padLeft("OrigQty", 9) << "\n";
        // 2 + 22 + 12 + 14 + 9 + 2 + 11 + 2 + 9 = 83
        cout << "  " << string(81, '-') << "\n";
    }

    // OOP CONCEPT: OBJECT SERIALIZATION
    void saveToFile(ofstream &out) const
    {
        // Price written with fixed 2 decimals. Default ostream formatting gave
        // 6 significant digits, so large values could be written in scientific
        // notation (1.23457e+06) into a file the Flask app also parses.
        out << name << "," << batchNumber << "," << expiryDate << ","
            << quantity << "," << decimals2(price) << ","
            << originalQuantity << "\n";
    }

    /*
        OOP CONCEPT: STATIC FACTORY METHOD + EXCEPTION HANDLING

        stoi/stof throw on non-numeric input. This runs over every line of
        inventory.txt at startup, before login, so one corrupt field used to
        terminate the program outright. prog1 guarded this; prog2 had dropped
        the try/catch.
    */
    static bool loadFromFile(const string &line, Medicine &out)
    {
        stringstream ss(line);
        string n, b, e, qStr, pStr, oqStr;
        if (!getline(ss, n, ',')) return false;
        if (!getline(ss, b, ',')) return false;
        if (!getline(ss, e, ',')) return false;
        if (!getline(ss, qStr, ',')) return false;
        if (!getline(ss, pStr, ',')) return false;
        if (!getline(ss, oqStr)) return false;

        int q = 0, oq = 0;
        double p = 0.0;
        try
        {
            q = qStr.empty() ? 0 : stoi(qStr);
            p = pStr.empty() ? 0.0 : stod(pStr);
            oq = oqStr.empty() ? q : stoi(oqStr);
        }
        catch (const invalid_argument &)
        {
            return false;
        }
        catch (const out_of_range &)
        {
            return false;
        }

        if (q < 0) q = 0;
        if (p < 0.0) p = 0.0;

        Medicine med(n, b, e, q, p);
        med.originalQuantity = oq < q ? q : oq;
        out = med;
        return true;
    }

    /*
        OOP CONCEPT: TRANSACTION METHOD
        Encapsulates the business rule: cannot sell a non-positive amount,
        more than is in stock, or an expired item.
    */
    bool sell(int qty)
    {
        if (qty <= 0 || qty > quantity || isExpired())
            return false;
        quantity -= qty;
        return true;
    }
};

// ============================================================================
// InventoryManager Class
// ============================================================================
/*
OOP CONCEPT: COMPOSITION
- "has-a" collection of Medicine objects, plus the operations over them
*/
class InventoryManager
{
private:
    vector<Medicine> inventory;
    static const int LOW_STOCK_THRESHOLD = 10;
    User *currentUser;
    string dataFile;

    /*
        OOP CONCEPT: PRIVATE HELPER (audit logging)

        The stream is named histOut, not `log`: with `using namespace std;`
        in scope, a local called `log` hides std::log, so any later numeric
        use of log() inside this function would fail to compile.
    */
    void writeHistory(const string &message)
    {
        ofstream histOut("history.txt", ios::app);
        if (!histOut)
        {
            cout << "  WARNING: could not append to history.txt\n";
            return;
        }
        string who = currentUser
            ? "[" + currentUser->getUserId() + " - " + currentUser->getName() + "] "
            : "[SYSTEM] ";
        histOut << currentTimestamp() << " " << who << message << "\n";
    }

    Medicine *findByBatch(const string &batch)
    {
        for (size_t i = 0; i < inventory.size(); ++i)
            if (inventory[i].getBatchNumber() == batch)
                return &inventory[i];
        return nullptr;
    }

    bool requirePermission(bool allowed, const string &action,
                           const string &roles)
    {
        if (currentUser == nullptr)
        {
            cout << "  Access denied: not logged in.\n";
            return false;
        }
        if (!allowed)
        {
            cout << "  Access denied: you cannot " << action << ".\n"
                 << "  Required role: " << roles << "\n";
            return false;
        }
        return true;
    }

public:
    InventoryManager() : currentUser(nullptr), dataFile("inventory.txt") {}

    void setCurrentUser(User *user) { currentUser = user; }

    // OOP CONCEPT: FILE I/O WITH OBJECTS (deserialization)
    void loadFromFile(const string &filename)
    {
        dataFile = filename;
        inventory.clear();
        ifstream in(filename.c_str());
        if (!in)
        {
            cout << "  Note: " << filename
                 << " not found - starting with an empty inventory.\n";
            return;
        }

        string line;
        int skipped = 0;
        while (getline(in, line))
        {
            if (line.empty())
                continue;
            Medicine med;
            if (Medicine::loadFromFile(line, med))
                inventory.push_back(med);
            else
                ++skipped;
        }
        if (skipped > 0)
            cout << "  Note: skipped " << skipped
                 << " malformed line(s) in " << filename << ".\n";
    }

    // OOP CONCEPT: OBJECT PERSISTENCE (serialization)
    // Returns false on failure so callers stop claiming success.
    bool saveToFile(const string &filename)
    {
        ofstream out(filename.c_str());
        if (!out)
        {
            cout << "  ERROR: cannot write " << filename
                 << " (is it open in another program?)\n";
            return false;
        }
        for (size_t i = 0; i < inventory.size(); ++i)
            inventory[i].saveToFile(out);
        if (!out.good())
        {
            cout << "  ERROR: writing " << filename << " failed.\n";
            return false;
        }
        return true;
    }

    bool save() { return saveToFile(dataFile); }

    size_t size() const { return inventory.size(); }

    // OOP CONCEPT: CRUD - CREATE
    void addMedicine()
    {
        if (!requirePermission(currentUser && currentUser->canAddMedicine(),
                               "add medicines", "Doctor or Pharmacist"))
            return;

        cout << "\n--- Add Medicine ---\n";

        string name = readLine("  Medicine name: ");
        if (!validateText(name, "Medicine name"))
            return;

        string batch = readWord("  Batch number: ");
        if (!validateText(batch, "Batch number", 30))
            return;

        // Batch is the de-facto primary key, shared with the Flask app. A
        // duplicate used to be accepted silently and then be permanently
        // unreachable, because every lookup breaks on the first match.
        if (findByBatch(batch) != nullptr)
        {
            cout << "  Batch " << batch << " already exists. "
                 << "Use option 3 (Restock) to add stock to it.\n";
            return;
        }

        string expiry = readWord("  Expiry date (YYYY-MM-DD): ");
        Date parsed;
        if (!parseDate(expiry, parsed))
        {
            cout << "  '" << expiry << "' is not a real calendar date.\n";
            return;
        }

        bool ok = false;
        int quantity = readInt("  Quantity: ", &ok);
        if (!ok) return;
        if (quantity < 0)
        {
            cout << "  Quantity cannot be negative.\n";
            return;
        }

        double price = readDouble("  Price per unit: ", &ok);
        if (!ok) return;
        if (price < 0.0)
        {
            cout << "  Price cannot be negative.\n";
            return;
        }

        inventory.push_back(Medicine(name, batch, expiry, quantity, price));
        if (save())
        {
            writeHistory("Added medicine: " + name + " (" + batch + "), qty=" +
                         to_string(quantity) + ", price=" + decimals2(price));
            cout << "  Added " << name << " (" << batch << ").\n";
        }
    }

    // OOP CONCEPT: CRUD - UPDATE (replaces the stored quantity)
    void updateMedicine()
    {
        if (!requirePermission(currentUser && currentUser->canUpdateMedicine(),
                               "update medicines", "Doctor or Pharmacist"))
            return;

        cout << "\n--- Update Medicine ---\n";
        string batch = readWord("  Batch number to update: ");
        Medicine *med = findByBatch(batch);
        if (med == nullptr)
        {
            cout << "  No medicine with batch number '" << batch << "'.\n";
            return;
        }

        cout << "  Found: " << med->getName()
             << " | current qty " << med->getQuantity()
             << " | expiry " << med->getExpiryDate() << "\n";

        bool ok = false;
        int newQty = readInt("  New quantity: ", &ok);
        if (!ok) return;
        if (newQty < 0)
        {
            cout << "  Quantity cannot be negative.\n";
            return;
        }

        string newExp = readWord("  New expiry date (YYYY-MM-DD): ");
        Date parsed;
        if (!parseDate(newExp, parsed))
        {
            cout << "  '" << newExp << "' is not a real calendar date.\n";
            return;
        }

        med->setQuantity(newQty);
        med->setExpiryDate(newExp);
        if (save())
        {
            writeHistory("Updated medicine: " + med->getName() + " (" +
                         med->getBatchNumber() + "), new qty=" + to_string(newQty));
            cout << "  Updated.\n";
        }
    }

    // OOP CONCEPT: STATE MODIFICATION (adds to the existing quantity)
    void restockMedicine()
    {
        if (!requirePermission(currentUser && currentUser->canUpdateMedicine(),
                               "restock medicines", "Doctor or Pharmacist"))
            return;

        cout << "\n--- Restock Medicine ---\n";
        string batch = readWord("  Batch number to restock: ");
        Medicine *med = findByBatch(batch);
        if (med == nullptr)
        {
            cout << "  No medicine with batch number '" << batch << "'.\n";
            return;
        }

        bool wasExpired = med->isExpired();
        cout << "  Found: " << med->getName()
             << " | current qty " << med->getQuantity()
             << " | expiry " << med->getExpiryDate()
             << (wasExpired ? "  [EXPIRED]" : "") << "\n";

        bool ok = false;
        int extra = readInt("  Quantity to add: ", &ok);
        if (!ok) return;
        if (extra <= 0)
        {
            cout << "  Quantity to add must be at least 1.\n";
            return;
        }

        string newExp;
        if (wasExpired)
        {
            cout << "  This medicine is expired, so a new expiry date is required.\n";
            newExp = readWord("  New expiry date (YYYY-MM-DD): ");
            Date parsed;
            if (!parseDate(newExp, parsed))
            {
                cout << "  '" << newExp << "' is not a real calendar date.\n";
                return;
            }
        }
        else
        {
            newExp = readWord("  New expiry date (blank/'-' to keep current): ");
            if (newExp == "-" || newExp.empty())
            {
                newExp.clear();
            }
            else
            {
                Date parsed;
                if (!parseDate(newExp, parsed))
                {
                    cout << "  '" << newExp << "' is not a real calendar date.\n";
                    return;
                }
            }
        }

        med->addStock(extra);
        if (!newExp.empty())
            med->setExpiryDate(newExp);

        if (save())
        {
            writeHistory("Restocked medicine: " + med->getName() + " (" +
                         med->getBatchNumber() + "), added qty=" + to_string(extra) +
                         ", new total=" + to_string(med->getQuantity()) +
                         ", new expiry=" + (newExp.empty() ? "unchanged" : newExp));
            cout << "  Added " << extra << " units. New total: "
                 << med->getQuantity() << ".\n";
        }
    }

    /*
        OOP CONCEPT: SOFT DELETE

        Sets the quantity to 0 and KEEPS the record. The previous version
        erased the rows outright, which contradicted the rest of the system:
        history.txt records "Marked as expired (qty set to 0)" and the Flask
        app expects those batches to still exist. Running the old option 3
        permanently deleted records the web UI still referenced.
    */
    void clearExpiredStock()
    {
        if (!requirePermission(currentUser && currentUser->canRemoveExpired(),
                               "clear expired stock", "Pharmacist"))
            return;

        int cleared = 0;
        for (size_t i = 0; i < inventory.size(); ++i)
        {
            if (inventory[i].isExpired() && inventory[i].getQuantity() > 0)
            {
                writeHistory("Marked as expired (qty set to 0): " +
                             inventory[i].getName() + " (" +
                             inventory[i].getBatchNumber() + "), previous qty=" +
                             to_string(inventory[i].getQuantity()));
                inventory[i].setQuantity(0);
                ++cleared;
            }
        }

        if (cleared == 0)
        {
            // Don't rewrite the file for a no-op.
            cout << "  No expired medicines with stock found.\n";
            return;
        }
        if (save())
            cout << "  Cleared stock for " << cleared
                 << " expired medicine(s). Records kept.\n";
    }

    // OOP CONCEPT: REPORTING
    void generateLowStockReport() const
    {
        cout << "\n--- Low Stock Report (at or below "
             << LOW_STOCK_THRESHOLD << " units) ---\n";
        int found = 0;
        for (size_t i = 0; i < inventory.size(); ++i)
        {
            // <= , not < : an item at exactly the threshold was never
            // reported, though both the docs and the Flask app use <=.
            if (!inventory[i].isExpired() &&
                inventory[i].getQuantity() <= LOW_STOCK_THRESHOLD)
            {
                if (found == 0) Medicine::displayHeader();
                inventory[i].display();
                ++found;
            }
        }
        if (found == 0)
            cout << "  Stock levels are healthy.\n";
        else
            cout << "\n  " << found << " item(s) need restocking.\n";
    }

    void generateExpiredReport() const
    {
        cout << "\n--- Expired Medicines Report ---\n";
        int found = 0, badDates = 0;
        for (size_t i = 0; i < inventory.size(); ++i)
        {
            if (inventory[i].isExpired())
            {
                if (found == 0) Medicine::displayHeader();
                inventory[i].display();
                ++found;
            }
            if (!inventory[i].hasValidExpiry())
                ++badDates;
        }
        if (found == 0)
            cout << "  No expired medicines.\n";
        else
            cout << "\n  " << found << " expired item(s).\n";
        if (badDates > 0)
            cout << "  " << badDates << " item(s) have an unreadable expiry "
                 << "date, shown as (?) in listings.\n";
    }

    void displayInventory() const
    {
        cout << "\n--- Inventory (" << inventory.size() << " items) ---\n";
        if (inventory.empty())
        {
            cout << "  The inventory is empty.\n";
            return;
        }
        Medicine::displayHeader();
        for (size_t i = 0; i < inventory.size(); ++i)
            inventory[i].display();

        long long units = 0;
        double value = 0.0;
        for (size_t i = 0; i < inventory.size(); ++i)
        {
            units += inventory[i].getQuantity();
            value += inventory[i].getQuantity() * inventory[i].getPrice();
        }
        cout << "\n  Total units: " << units
             << " | Stock value: " << money(value) << "\n";
    }

    /*
        OOP CONCEPT: FILE GENERATION
        Writes the bill to its own timestamped file.
    */
    string generateBillFile(
        const vector<tuple<string, string, int, double, double> > &items,
        double total, const string &timestamp)
    {
        string filename = "bill_" + timestamp;
        for (size_t i = 0; i < filename.size(); ++i)
            if (filename[i] == ':' || filename[i] == ' ')
                filename[i] = '_';
        filename += ".txt";

        ofstream bill(filename.c_str());
        if (!bill)
        {
            cout << "  WARNING: could not write " << filename << "\n";
            return string();
        }

        const string rule(80, '=');
        const string thin(80, '-');

        bill << rule << "\n";
        bill << "                    MEDICAL INVENTORY MANAGEMENT SYSTEM\n";
        bill << "                            PURCHASE BILL\n";
        bill << rule << "\n\n";
        bill << "Bill Date/Time: " << timestamp << "\n";
        if (currentUser)
        {
            bill << "Served By: " << currentUser->getName()
                 << " (ID: " << currentUser->getUserId() << ")\n";
            bill << "Role: " << currentUser->getRole() << "\n";
        }
        bill << "\n" << thin << "\n";
        bill << "                            MEDICINE DETAILS\n";
        bill << thin << "\n\n";

        bill << padRight("Medicine Name", 24) << padRight("Batch", 12)
             << padLeft("Qty", 8) << "  " << padLeft("Unit Price", 14) << "  "
             << padLeft("Amount", 14) << "\n";
        bill << thin << "\n";

        for (size_t i = 0; i < items.size(); ++i)
        {
            bill << padRight(get<0>(items[i]), 24)
                 << padRight(get<1>(items[i]), 12)
                 << padLeft(to_string(get<2>(items[i])), 8) << "  "
                 << padLeft(money(get<3>(items[i])), 14) << "  "
                 << padLeft(money(get<4>(items[i])), 14) << "\n";
        }

        bill << "\n" << thin << "\n";
        bill << "                            PAYMENT SUMMARY\n";
        bill << thin << "\n\n";
        bill << padRight("Subtotal:", 40) << money(total) << "\n";
        bill << padRight("Tax (0%):", 40) << money(0.0) << "\n";
        bill << "                    --------------------------------\n";
        bill << padRight("Total Amount:", 40) << money(total) << "\n\n";
        bill << thin << "\n\n";
        bill << "                    Thank you for your business!\n\n";
        bill << rule << "\n";

        return filename;
    }

    // OOP CONCEPT: COMPLEX TRANSACTION over multiple objects
    void sellMedicines()
    {
        if (!requirePermission(currentUser && currentUser->canBuyMedicine(),
                               "sell medicines", "Pharmacist or Nurse"))
            return;

        // double, not float. float has a 24-bit mantissa (~16.7M), and the
        // real data (1.5M units at 8.50) exceeds 2^24, so running totals were
        // wrong by whole rupees.
        vector<tuple<string, string, int, double, double> > items;
        double total = 0.0;

        while (true)
        {
            cout << "\n--- Sell Medicine ---\n";
            string batch = readWord("  Batch number (or '-' to finish): ");
            if (batch.empty() && inputClosed())
                break;
            if (batch == "-")
                break;

            Medicine *med = findByBatch(batch);
            if (med == nullptr)
            {
                cout << "  No medicine with batch number '" << batch << "'.\n";
            }
            else if (med->isExpired())
            {
                cout << "  " << med->getName()
                     << " has expired and cannot be sold.\n";
            }
            else if (med->getQuantity() == 0)
            {
                cout << "  " << med->getName() << " is out of stock.\n";
            }
            else
            {
                cout << "  " << med->getName() << " | available "
                     << med->getQuantity() << " | unit price "
                     << money(med->getPrice()) << "\n";

                bool ok = false;
                int qty = readInt("  Quantity to sell: ", &ok);
                if (!ok)
                    break;

                if (qty <= 0)
                {
                    cout << "  Quantity must be at least 1.\n";
                }
                else if (qty > med->getQuantity())
                {
                    // Distinguishes "too much" from "unreadable input", which
                    // both used to produce "Not enough stock available".
                    cout << "  Only " << med->getQuantity()
                         << " units are in stock.\n";
                }
                else if (med->sell(qty))
                {
                    double unit = med->getPrice();
                    double cost = qty * unit;
                    total += cost;
                    items.push_back(make_tuple(med->getName(),
                                               med->getBatchNumber(),
                                               qty, unit, cost));
                    writeHistory("Sold " + to_string(qty) + " of " +
                                 med->getName() + " (" + med->getBatchNumber() +
                                 "), total=" + string(RUPEE) + decimals2(cost));
                    cout << "  Added to bill: " << med->getName()
                         << " x" << qty << " = " << money(cost) << "\n";
                }
            }

            string again = readWord("  Sell another medicine? (y/n): ");
            if (again.empty() && inputClosed())
                break;
            if (again != "y" && again != "Y")
                break;
        }

        if (items.empty())
        {
            cout << "\n  Nothing sold. No bill generated.\n";
            return;
        }

        string timestamp = timestampPlain();

        banner("PURCHASE BILL");
        cout << "\n  Date/Time: " << timestamp << "\n";
        if (currentUser)
            cout << "  Served by: " << currentUser->getName()
                 << " (" << currentUser->getUserId() << ")\n"
                 << "  Role:      " << currentUser->getRole() << "\n";

        // Column widths and the rules below are both derived from the same
        // numbers, so the bill box can no longer be a different width from
        // the lines around it.
        const string thin(81, '-');
        cout << "\n  " << thin << "\n";
        cout << "  " << padRight("Medicine", 24) << padRight("Batch", 12)
             << padLeft("Qty", 7) << "  " << padLeft("Unit Price", 16) << "  "
             << padLeft("Amount", 16) << "\n";
        cout << "  " << thin << "\n";

        for (size_t i = 0; i < items.size(); ++i)
        {
            cout << "  " << padRight(get<0>(items[i]), 24)
                 << padRight(get<1>(items[i]), 12)
                 << padLeft(to_string(get<2>(items[i])), 7) << "  "
                 << padLeft(money(get<3>(items[i])), 16) << "  "
                 << padLeft(money(get<4>(items[i])), 16) << "\n";
        }

        cout << "  " << thin << "\n";
        cout << "  " << padRight("Subtotal:", 45) << padLeft(money(total), 16) << "\n";
        cout << "  " << padRight("Tax (0%):", 45) << padLeft(money(0.0), 16) << "\n";
        cout << "  " << padRight("TOTAL:", 45) << padLeft(money(total), 16) << "\n";
        cout << "  " << string(81, '=') << "\n";

        string billFile = generateBillFile(items, total, timestamp);
        if (!billFile.empty())
            cout << "\n  Bill saved to: " << billFile << "\n";

        save();
    }

    /*
        OOP CONCEPT: AUDIT VIEW

        Shows the most recent entries rather than dumping the whole log. main()
        used to duplicate this logic inline to show the last 5 entries while
        this method printed every line unpaged.
    */
    void showHistory(int lastN = 0) const
    {
        ifstream in("history.txt");
        if (!in)
        {
            cout << "  No history file found.\n";
            return;
        }

        vector<string> lines;
        string line;
        while (getline(in, line))
            if (!line.empty())
                lines.push_back(line);

        if (lines.empty())
        {
            cout << "  No transactions recorded yet.\n";
            return;
        }

        // size_t throughout: the old loop compared an int against
        // vector::size() and produced the program's only compiler warning.
        size_t start = 0;
        if (lastN > 0 && lines.size() > static_cast<size_t>(lastN))
            start = lines.size() - static_cast<size_t>(lastN);

        if (lastN > 0)
            cout << "\n--- Recent Transactions (last "
                 << (lines.size() - start) << " of " << lines.size() << ") ---\n";
        else
            cout << "\n--- Transaction History (" << lines.size()
                 << " entries) ---\n";

        for (size_t i = start; i < lines.size(); ++i)
            cout << "  " << lines[i] << "\n";
    }
};

// Out-of-class definition for the static const member (needed when it is
// used in contexts that require an address, and keeps C++11 happy).
const int InventoryManager::LOW_STOCK_THRESHOLD;

// ============================================================================
// MENU
// ============================================================================

static void printMenu(const User *user)
{
    boxTop();
    boxLine("MEDICAL INVENTORY SYSTEM - MENU");
    boxSep();
    if (user)
    {
        // clip() guarantees these never overflow the frame.
        boxLine("User: " + clip(user->getName() + " (" + user->getUserId() + ")", 50));
        boxLine("Role: " + user->getRole());
        boxSep();
    }
    boxLine("1.  Add New Medicine       [Doctor, Pharmacist]");
    boxLine("2.  Update Medicine        [Doctor, Pharmacist]");
    boxLine("3.  Restock Medicine       [Doctor, Pharmacist]");
    boxLine("4.  Clear Expired Stock    [Pharmacist]");
    boxLine("5.  Low Stock Report       [All]");
    boxLine("6.  Expired Report         [All]");
    boxLine("7.  Show Inventory         [All]");
    boxLine("8.  Sell Medicines (Bill)  [Pharmacist, Nurse]");
    boxLine("9.  Transaction History    [All]");
    boxLine("10. List All Users         [All]");
    boxLine("11. Add New User           [Pharmacist]");
    boxLine("12. Logout");
    boxLine("13. Save and Exit");
    boxBottom();
}

/*
OOP CONCEPT: OBJECT LIFECYCLE & INTERACTION
- Creates the manager objects and drives them through a menu
*/
int main()
{
    initConsole();

    banner("MEDICAL INVENTORY MANAGEMENT SYSTEM v2.1",
           "With User Authentication & Role-Based Access");

    // The banner is printed BEFORE constructing UserManager: its constructor
    // can print "created default users", which used to appear above the
    // welcome banner.
    InventoryManager manager;
    UserManager userManager;

    manager.loadFromFile("inventory.txt");

    bool running = true;
    while (running)
    {
        if (!userManager.login())
        {
            cout << "\nExiting.\n";
            return 1;
        }

        User *me = userManager.getCurrentUser();
        manager.setCurrentUser(me);

        banner("CURRENT INVENTORY STATUS");
        manager.displayInventory();
        manager.showHistory(5);
        pause("\nPress Enter to continue to the menu...");

        bool loggedIn = true;
        while (loggedIn)
        {
            printMenu(userManager.getCurrentUser());

            bool ok = false;
            int choice = readInt("  Enter your choice: ", &ok);
            if (!ok)
            {
                // Input exhausted (EOF). The old loop spun here forever,
                // reprinting the menu at full speed.
                cout << "\nInput closed. Saving and exiting.\n";
                manager.save();
                return 0;
            }

            switch (choice)
            {
            case 1:  manager.addMedicine();            break;
            case 2:  manager.updateMedicine();         break;
            case 3:  manager.restockMedicine();        break;
            case 4:  manager.clearExpiredStock();      break;
            case 5:  manager.generateLowStockReport(); break;
            case 6:  manager.generateExpiredReport();  break;
            case 7:  manager.displayInventory();       break;
            case 8:  manager.sellMedicines();          break;
            case 9:  manager.showHistory();            break;
            case 10: userManager.listUsers();          break;
            case 11: userManager.addUser();            break;
            case 12:
                // Logout now returns to the login prompt instead of killing
                // the process, and clears BOTH holders of the user pointer.
                userManager.logout();
                manager.setCurrentUser(nullptr);
                loggedIn = false;
                break;
            case 13:
                cout << "  Saving...\n";
                manager.save();
                cout << "  Goodbye.\n";
                loggedIn = false;
                running = false;
                break;
            default:
                cout << "  Invalid choice. Please enter a number from 1 to 13.\n";
                break;
            }

            if (loggedIn)
                pause();
        }
    }

    return 0;
}
