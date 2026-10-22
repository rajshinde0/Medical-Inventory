/*
================================================================================
    MEDICAL INVENTORY MANAGEMENT SYSTEM v1.1 - C++ CONSOLE (BASE VERSION)
================================================================================
    Compile: g++ -std=c++11 -Wall -Wextra prog1.cpp -o prog1.exe
    Run:     prog1.exe

    This is the single-user base version of the system. prog2.cpp extends it
    with authentication, role-based access control and an audit trail.

    It is kept as a separate, self-contained program on purpose: it is the file
    that demonstrates the project's inheritance and runtime polymorphism (the
    abstract Record base class below), which prog2 does not use.

    It reads and writes the SAME inventory.txt as prog2.cpp and app.py:

        inventory.txt : name,batch,expiry,quantity,price,originalQuantity

    Fields are comma-separated with no quoting, so commas are rejected in any
    text field.

    OOP Concepts: Abstract Base Class, Pure Virtual Functions, Runtime
                  Polymorphism, Virtual Destructor, Inheritance, Encapsulation,
                  Constructor Overloading, Static Factory, Exception Handling
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
#include <cstdio>
#include <limits>
#include <stdexcept>

// ============================================================================
// SAFE INPUT
// The original version had NO std::cin.fail() handling anywhere: typing a
// letter at the menu left failbit set, so every later read failed instantly
// and the menu redrew forever at full speed. Piped input hit the same spin at
// end-of-file.
// ============================================================================

static void discardLine()
{
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

static int readInt(const std::string &prompt, bool *ok = nullptr)
{
    while (true)
    {
        std::cout << prompt;
        int value;
        if (std::cin >> value)
        {
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            if (ok) *ok = true;
            return value;
        }
        if (std::cin.eof())
        {
            if (ok) *ok = false;
            return 0;
        }
        discardLine();
        std::cout << "  Please enter a whole number.\n";
    }
}

static double readDouble(const std::string &prompt, bool *ok = nullptr)
{
    while (true)
    {
        std::cout << prompt;
        double value;
        if (std::cin >> value)
        {
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            if (ok) *ok = true;
            return value;
        }
        if (std::cin.eof())
        {
            if (ok) *ok = false;
            return 0.0;
        }
        discardLine();
        std::cout << "  Please enter a number.\n";
    }
}

static std::string readWord(const std::string &prompt)
{
    std::cout << prompt;
    std::string word;
    if (!(std::cin >> word))
        return std::string();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return word;
}

// Safe because every numeric/word reader above consumes its trailing newline,
// so this never picks up a leftover one. The original called an unconditional
// cin.ignore() at the top of addMedicine() instead, which swallowed a whole
// line of real input whenever the buffer was already clean.
static std::string readLine(const std::string &prompt)
{
    std::cout << prompt;
    std::string line;
    if (!getline(std::cin, line))
        return std::string();
    return line;
}

static std::string money(double value)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

// Reject text that would corrupt the comma-separated data file.
static bool validateText(const std::string &value, const std::string &label,
                         std::size_t maxLen = 60)
{
    if (value.empty())
    {
        std::cout << "  " << label << " cannot be empty.\n";
        return false;
    }
    if (value.find(',') != std::string::npos)
    {
        std::cout << "  " << label << " may not contain a comma "
                  << "(the data file is comma-separated).\n";
        return false;
    }
    if (value.size() > maxLen)
    {
        std::cout << "  " << label << " must be " << maxLen
                  << " characters or fewer.\n";
        return false;
    }
    return true;
}

// ============================================================================
// DATE HANDLING
// A real calendar date with validation. The original fed sscanf output
// straight into mktime(), which silently normalises nonsense (2025-13-45
// became 2026-01-14), and treated an unparseable date as "never expires".
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

static bool parseDate(const std::string &text, Date &out)
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
    std::time_t now = std::time(nullptr);
    std::tm *local = std::localtime(&now);
    if (local == nullptr)
        return Date(1970, 1, 1);
    return Date(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday);
}

static bool dateBefore(const Date &a, const Date &b)
{
    if (a.year != b.year)   return a.year < b.year;
    if (a.month != b.month) return a.month < b.month;
    return a.day < b.day;
}

static std::string currentTimestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm *local = std::localtime(&now);
    if (local == nullptr)
        return "[0000-00-00 00:00:00]";
    std::ostringstream oss;
    oss << "[" << std::put_time(local, "%Y-%m-%d %H:%M:%S") << "]";
    return oss.str();
}

/*=============================
 OOP CONCEPT: ABSTRACT BASE CLASS
 - Pure virtual functions make this an interface that cannot be instantiated
 - The virtual destructor makes destruction through a base handle well defined
=============================*/
class Record
{
public:
    virtual void display() const = 0;
    virtual void saveToFile(std::ofstream &out) const = 0;
    virtual std::string summary() const = 0;
    virtual ~Record() {}
};

// ============================================================================
// Medicine Class
// ============================================================================
/*
OOP CONCEPT: INHERITANCE
- Medicine "is-a" Record and must implement the whole interface
*/
class Medicine : public Record
{
private:
    std::string name;
    std::string batchNumber;
    std::string expiryDate;
    int quantity;
    double price;            // double, not float: see sell-total note below
    int originalQuantity;

public:
    // OOP CONCEPT: CONSTRUCTOR OVERLOADING
    Medicine() : quantity(0), price(0.0), originalQuantity(0) {}

    Medicine(const std::string &n, const std::string &b, const std::string &e,
             int q, double p)
        : name(n), batchNumber(b), expiryDate(e),
          quantity(q), price(p), originalQuantity(q) {}

    // OOP CONCEPT: ACCESSORS
    std::string getName() const { return name; }
    std::string getBatchNumber() const { return batchNumber; }
    std::string getExpiryDate() const { return expiryDate; }
    int getQuantity() const { return quantity; }
    double getPrice() const { return price; }
    int getOriginalQuantity() const { return originalQuantity; }

    // OOP CONCEPT: MUTATORS with invariant maintenance
    void setQuantity(int q)
    {
        quantity = q < 0 ? 0 : q;
        // Keep originalQuantity >= quantity; the original never updated it, so
        // the "original" column drifted into meaninglessness.
        if (quantity > originalQuantity)
            originalQuantity = quantity;
    }

    void setExpiryDate(const std::string &e) { expiryDate = e; }

    bool hasValidExpiry() const
    {
        Date parsed;
        return parseDate(expiryDate, parsed);
    }

    // An item expiring TODAY is NOT expired. The original compared mktime() of
    // midnight against the current wall-clock time, so it lost its final day.
    bool isExpired() const
    {
        Date expiry;
        if (!parseDate(expiryDate, expiry))
            return false;
        return dateBefore(expiry, todayDate());
    }

    // OOP CONCEPT: OVERRIDING A PURE VIRTUAL FUNCTION
    void display() const override
    {
        std::cout << "  " << std::left
                  << std::setw(22) << name.substr(0, 21)
                  << std::setw(12) << batchNumber.substr(0, 11)
                  << std::setw(14) << (expiryDate + (hasValidExpiry() ? "" : " (?)"))
                  << std::right
                  << std::setw(9) << quantity << "  "
                  << std::setw(10) << money(price) << "  "
                  << std::setw(9) << originalQuantity
                  << std::left << "\n";
    }

    static void displayHeader()
    {
        std::cout << "  " << std::left
                  << std::setw(22) << "Name"
                  << std::setw(12) << "Batch"
                  << std::setw(14) << "Expiry"
                  << std::right
                  << std::setw(9) << "QtyLeft" << "  "
                  << std::setw(10) << "Price" << "  "
                  << std::setw(9) << "OrigQty"
                  << std::left << "\n";
        // 22 + 12 + 14 + 9 + 2 + 10 + 2 + 9 = 80. The original underlined six
        // columns totalling 68 with a 64-dash rule.
        std::cout << "  " << std::string(80, '-') << "\n";
    }

    std::string summary() const override
    {
        return name + " (" + batchNumber + ")";
    }

    void saveToFile(std::ofstream &out) const override
    {
        // Fixed 2 decimals. Default ostream formatting uses 6 significant
        // digits, so a large price could be written as 1.23457e+06 into a file
        // that prog2.cpp and app.py also parse.
        out << name << "," << batchNumber << "," << expiryDate << ","
            << quantity << "," << money(price) << ","
            << originalQuantity << "\n";
    }

    // OOP CONCEPT: STATIC FACTORY METHOD + EXCEPTION HANDLING
    static bool loadFromFile(const std::string &line, Medicine &out)
    {
        std::stringstream ss(line);
        std::string n, b, e, qStr, pStr, oqStr;
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
            // All three conversions are inside the try. The original left the
            // third std::stoi OUTSIDE its own try/catch, so a malformed sixth
            // field still terminated the program.
            q = qStr.empty() ? 0 : std::stoi(qStr);
            p = pStr.empty() ? 0.0 : std::stod(pStr);
            oq = oqStr.empty() ? q : std::stoi(oqStr);
        }
        catch (const std::invalid_argument &)
        {
            return false;
        }
        catch (const std::out_of_range &)
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

    // OOP CONCEPT: TRANSACTION METHOD encapsulating the business rule
    bool sell(int qty)
    {
        if (qty <= 0 || qty > quantity || isExpired())
            return false;
        quantity -= qty;
        return true;
    }
};

/*
    OOP CONCEPT: RUNTIME POLYMORPHISM (actually exercised)

    This free function knows nothing about Medicine -- it takes a reference to
    the abstract base and the call is dispatched through the vtable. In the
    original program the Record base existed but no code ever used a Record
    handle, so the inheritance was dead weight.
*/
static void printRecord(const Record &record)
{
    record.display();
}

// ============================================================================
// InventoryManager
// ============================================================================
/*
OOP CONCEPT: COMPOSITION -- "has-a" collection of Medicine objects
*/
class InventoryManager
{
private:
    std::vector<Medicine> inventory;
    static const int LOW_STOCK_THRESHOLD = 10;
    std::string dataFile;
    bool dirty;              // tracks unsaved in-memory changes

    Medicine *findByBatch(const std::string &batch)
    {
        for (std::size_t i = 0; i < inventory.size(); ++i)
            if (inventory[i].getBatchNumber() == batch)
                return &inventory[i];
        return nullptr;
    }

    // prog1 never wrote to history.txt, which left unexplained gaps in the
    // audit trail shared with prog2.cpp and app.py. Entries are tagged
    // [SYSTEM] because this version has no concept of users.
    void writeHistory(const std::string &message)
    {
        std::ofstream histOut("history.txt", std::ios::app);
        if (!histOut)
            return;
        histOut << currentTimestamp() << " [SYSTEM] " << message << "\n";
    }

public:
    InventoryManager() : dataFile("inventory.txt"), dirty(false) {}

    bool hasUnsavedChanges() const { return dirty; }

    void loadFromFile(const std::string &filename)
    {
        dataFile = filename;
        inventory.clear();
        std::ifstream in(filename.c_str());
        if (!in)
        {
            std::cout << "  Note: " << filename
                      << " not found - starting with an empty inventory.\n";
            return;
        }

        std::string line;
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
            std::cout << "  Note: skipped " << skipped
                      << " malformed line(s) in " << filename << ".\n";
        dirty = false;
    }

    // Returns false on failure. The original printed "Inventory saved."
    // unconditionally, even if the file could not be opened at all.
    bool saveToFile(const std::string &filename)
    {
        std::ofstream out(filename.c_str());
        if (!out)
        {
            std::cout << "  ERROR: cannot write " << filename
                      << " (is it open in another program?)\n";
            return false;
        }
        for (std::size_t i = 0; i < inventory.size(); ++i)
            inventory[i].saveToFile(out);
        if (!out.good())
        {
            std::cout << "  ERROR: writing " << filename << " failed.\n";
            return false;
        }
        dirty = false;
        return true;
    }

    bool save() { return saveToFile(dataFile); }

    void addMedicine()
    {
        std::cout << "\n--- Add Medicine ---\n";

        std::string name = readLine("  Medicine name: ");
        if (!validateText(name, "Medicine name"))
            return;

        std::string batch = readWord("  Batch number: ");
        if (!validateText(batch, "Batch number", 30))
            return;

        // Batch is the de-facto primary key. A duplicate used to be accepted
        // and then be permanently unreachable, since every lookup stops at the
        // first match.
        if (findByBatch(batch) != nullptr)
        {
            std::cout << "  Batch " << batch << " already exists. "
                      << "Use option 2 (Update) to change it.\n";
            return;
        }

        std::string expiry = readWord("  Expiry date (YYYY-MM-DD): ");
        Date parsed;
        if (!parseDate(expiry, parsed))
        {
            std::cout << "  '" << expiry << "' is not a real calendar date.\n";
            return;
        }

        bool ok = false;
        int quantity = readInt("  Quantity: ", &ok);
        if (!ok) return;
        if (quantity < 0)
        {
            std::cout << "  Quantity cannot be negative.\n";
            return;
        }

        double price = readDouble("  Price per unit: ", &ok);
        if (!ok) return;
        if (price < 0.0)
        {
            std::cout << "  Price cannot be negative.\n";
            return;
        }

        inventory.push_back(Medicine(name, batch, expiry, quantity, price));
        dirty = true;
        writeHistory("Added medicine: " + name + " (" + batch + "), qty=" +
                     std::to_string(quantity) + ", price=" + money(price));
        std::cout << "  Added " << name << " (" << batch
                  << "). Use option 8 to save.\n";
    }

    void updateMedicine()
    {
        std::cout << "\n--- Update Medicine ---\n";
        std::string batch = readWord("  Batch number to update: ");

        Medicine *med = findByBatch(batch);
        if (med == nullptr)
        {
            std::cout << "  No medicine with batch number '" << batch << "'.\n";
            return;
        }

        std::cout << "  Found: " << med->getName()
                  << " | current qty " << med->getQuantity()
                  << " | expiry " << med->getExpiryDate() << "\n";

        bool ok = false;
        int newQty = readInt("  New quantity: ", &ok);
        if (!ok) return;
        if (newQty < 0)
        {
            std::cout << "  Quantity cannot be negative.\n";
            return;
        }

        std::string newExp = readWord("  New expiry date (YYYY-MM-DD): ");
        Date parsed;
        if (!parseDate(newExp, parsed))
        {
            std::cout << "  '" << newExp << "' is not a real calendar date.\n";
            return;
        }

        med->setQuantity(newQty);
        med->setExpiryDate(newExp);
        dirty = true;
        writeHistory("Updated medicine: " + med->summary() +
                     ", new qty=" + std::to_string(newQty));
        std::cout << "  Updated. Use option 8 to save.\n";
    }

    /*
        Soft delete: sets the quantity to 0 and KEEPS the record.

        The original erased the rows. Because inventory.txt is shared with
        prog2.cpp and app.py -- both of which treat "expired" as quantity 0
        with the record intact -- erasing here silently deleted batches the
        other two programs still referenced.
    */
    void clearExpiredStock()
    {
        int cleared = 0;
        for (std::size_t i = 0; i < inventory.size(); ++i)
        {
            if (inventory[i].isExpired() && inventory[i].getQuantity() > 0)
            {
                std::cout << "  Clearing stock: " << inventory[i].summary()
                          << " (was " << inventory[i].getQuantity() << ")\n";
                writeHistory("Marked as expired (qty set to 0): " +
                             inventory[i].summary() + ", previous qty=" +
                             std::to_string(inventory[i].getQuantity()));
                inventory[i].setQuantity(0);
                ++cleared;
            }
        }
        // The original announced "Expired medicines removed." even when it had
        // removed nothing.
        if (cleared == 0)
            std::cout << "  No expired medicines with stock found.\n";
        else
        {
            dirty = true;
            std::cout << "  Cleared stock for " << cleared
                      << " expired medicine(s). Records kept. "
                      << "Use option 8 to save.\n";
        }
    }

    void generateLowStockReport() const
    {
        std::cout << "\n--- Low Stock Report (at or below "
                  << LOW_STOCK_THRESHOLD << " units) ---\n";
        int found = 0;
        for (std::size_t i = 0; i < inventory.size(); ++i)
        {
            // <= , not < : an item at exactly the threshold was never listed.
            if (!inventory[i].isExpired() &&
                inventory[i].getQuantity() <= LOW_STOCK_THRESHOLD)
            {
                if (found == 0) Medicine::displayHeader();
                printRecord(inventory[i]);     // polymorphic dispatch
                ++found;
            }
        }
        if (found == 0)
            std::cout << "  Stock levels are healthy.\n";
        else
            std::cout << "\n  " << found << " item(s) need restocking.\n";
    }

    void generateExpiredReport() const
    {
        std::cout << "\n--- Expired Medicines Report ---\n";
        int found = 0, badDates = 0;
        for (std::size_t i = 0; i < inventory.size(); ++i)
        {
            if (inventory[i].isExpired())
            {
                if (found == 0) Medicine::displayHeader();
                printRecord(inventory[i]);
                ++found;
            }
            if (!inventory[i].hasValidExpiry())
                ++badDates;
        }
        if (found == 0)
            std::cout << "  No expired medicines.\n";
        else
            std::cout << "\n  " << found << " expired item(s).\n";
        if (badDates > 0)
            std::cout << "  " << badDates << " item(s) have an unreadable "
                      << "expiry date, shown as (?) in listings.\n";
    }

    void displayInventory() const
    {
        std::cout << "\n--- Inventory (" << inventory.size() << " items) ---\n";
        if (inventory.empty())
        {
            std::cout << "  The inventory is empty.\n";
            return;
        }
        Medicine::displayHeader();
        for (std::size_t i = 0; i < inventory.size(); ++i)
            printRecord(inventory[i]);

        long long units = 0;
        double value = 0.0;
        for (std::size_t i = 0; i < inventory.size(); ++i)
        {
            units += inventory[i].getQuantity();
            value += inventory[i].getQuantity() * inventory[i].getPrice();
        }
        std::cout << "\n  Total units: " << units
                  << " | Stock value: " << money(value) << "\n";
    }

    // Multi-item billing
    void buyMedicines()
    {
        // double, not float: float carries ~24 bits of mantissa, and the real
        // data (1.5M units at 8.50) exceeds 2^24, so running totals came out
        // wrong by whole rupees.
        std::vector<std::tuple<std::string, std::string, int, double, double> > items;
        double total = 0.0;

        while (true)
        {
            std::cout << "\n--- Buy Medicine ---\n";
            std::string batch = readWord("  Batch number (or '-' to finish): ");
            if (batch.empty() && std::cin.eof())
                break;
            if (batch == "-")
                break;

            Medicine *med = findByBatch(batch);
            if (med == nullptr)
            {
                std::cout << "  No medicine with batch number '" << batch << "'.\n";
            }
            else if (med->isExpired())
            {
                std::cout << "  " << med->getName()
                          << " has expired and cannot be sold.\n";
            }
            else if (med->getQuantity() == 0)
            {
                std::cout << "  " << med->getName() << " is out of stock.\n";
            }
            else
            {
                std::cout << "  " << med->getName() << " | available "
                          << med->getQuantity() << " | unit price "
                          << money(med->getPrice()) << "\n";

                bool ok = false;
                int qty = readInt("  Quantity to buy: ", &ok);
                if (!ok)
                    break;

                if (qty <= 0)
                {
                    std::cout << "  Quantity must be at least 1.\n";
                }
                else if (qty > med->getQuantity())
                {
                    // Separates "too much" from "unreadable input"; both used
                    // to report "Not enough stock available."
                    std::cout << "  Only " << med->getQuantity()
                              << " units are in stock.\n";
                }
                else if (med->sell(qty))
                {
                    double unit = med->getPrice();
                    double cost = qty * unit;
                    total += cost;
                    items.push_back(std::make_tuple(med->getName(),
                                                    med->getBatchNumber(),
                                                    qty, unit, cost));
                    dirty = true;
                    writeHistory("Sold " + std::to_string(qty) + " of " +
                                 med->summary() + ", total=" + money(cost));
                    std::cout << "  Added to bill: " << med->getName()
                              << " x" << qty << " = " << money(cost) << "\n";
                }
            }

            std::string again = readWord("  Buy another medicine? (y/n): ");
            if (again.empty() && std::cin.eof())
                break;
            if (again != "y" && again != "Y")
                break;
        }

        if (items.empty())
        {
            std::cout << "\n  Nothing purchased. No bill generated.\n";
            return;
        }

        // Column widths and rule lengths derive from the same numbers, so the
        // bill can no longer be framed by rules of three different lengths.
        const std::string rule(72, '-');
        std::cout << "\n  " << std::string(72, '=') << "\n";
        std::cout << "                              FINAL BILL\n";
        std::cout << "  " << rule << "\n";
        std::cout << "  " << std::left
                  << std::setw(24) << "Medicine"
                  << std::setw(12) << "Batch"
                  << std::right
                  << std::setw(7) << "Qty" << "  "
                  << std::setw(12) << "Unit Price" << "  "
                  << std::setw(13) << "Amount"
                  << std::left << "\n";
        std::cout << "  " << rule << "\n";

        for (std::size_t i = 0; i < items.size(); ++i)
        {
            std::cout << "  " << std::left
                      << std::setw(24) << std::get<0>(items[i]).substr(0, 23)
                      << std::setw(12) << std::get<1>(items[i]).substr(0, 11)
                      << std::right
                      << std::setw(7) << std::get<2>(items[i]) << "  "
                      << std::setw(12) << money(std::get<3>(items[i])) << "  "
                      << std::setw(13) << money(std::get<4>(items[i]))
                      << std::left << "\n";
        }

        std::cout << "  " << rule << "\n";
        std::cout << "  " << std::left << std::setw(45) << "TOTAL:"
                  << std::right << std::setw(25) << money(total)
                  << std::left << "\n";
        std::cout << "  " << std::string(72, '=') << "\n";
        std::cout << "\n  Remember to save (option 8) to persist the stock change.\n";
    }
};

const int InventoryManager::LOW_STOCK_THRESHOLD;

// ============================================================================
// Main
// ============================================================================
int main()
{
    InventoryManager manager;
    manager.loadFromFile("inventory.txt");

    bool running = true;
    while (running)
    {
        std::cout << "\n===== MEDICAL INVENTORY SYSTEM (base version) =====\n";
        std::cout << "  1. Add New Medicine\n";
        std::cout << "  2. Update Medicine\n";
        std::cout << "  3. Clear Expired Stock\n";
        std::cout << "  4. Low Stock Report\n";
        std::cout << "  5. Expired Report\n";
        std::cout << "  6. Show Inventory\n";
        std::cout << "  7. Buy Medicines (Generate Bill)\n";
        std::cout << "  8. Save\n";
        std::cout << "  9. Save and Exit\n";
        std::cout << "  0. Exit without saving\n";
        if (manager.hasUnsavedChanges())
            std::cout << "  * You have unsaved changes.\n";

        bool ok = false;
        int choice = readInt("  Enter choice: ", &ok);
        if (!ok)
        {
            // End of input. The original spun the menu forever here.
            std::cout << "\nInput closed. Saving and exiting.\n";
            manager.save();
            return 0;
        }

        switch (choice)
        {
        case 1: manager.addMedicine();            break;
        case 2: manager.updateMedicine();         break;
        case 3: manager.clearExpiredStock();      break;
        case 4: manager.generateLowStockReport(); break;
        case 5: manager.generateExpiredReport();  break;
        case 6: manager.displayInventory();       break;
        case 7: manager.buyMedicines();           break;
        case 8:
            if (manager.save())
                std::cout << "  Inventory saved.\n";
            break;
        case 9:
            if (manager.save())
            {
                std::cout << "  Inventory saved. Exiting.\n";
                running = false;
            }
            else
                std::cout << "  Not exiting, so you can retry the save.\n";
            break;
        case 0:
            if (manager.hasUnsavedChanges())
            {
                std::string confirm =
                    readWord("  Discard unsaved changes? (y/n): ");
                if (confirm != "y" && confirm != "Y")
                    break;
            }
            std::cout << "  Exiting without saving.\n";
            running = false;
            break;
        default:
            std::cout << "  Invalid choice. Please enter a number from 0 to 9.\n";
            break;
        }
    }

    return 0;
}
