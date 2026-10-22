# MEDICAL INVENTORY MANAGEMENT SYSTEM - FLOWCHART SPECIFICATION

## COMPLETE SYSTEM FLOWCHART

Copy this to ChatGPT or any flowchart tool with the prompt: "Create a detailed flowchart diagram from this specification"

---

## MAIN SYSTEM FLOW

```
START
  ↓
[Load System Data]
  ├─ Load inventory.txt
  ├─ Load users.txt
  └─ Load history.txt
  ↓
[Display Login Page]
  ↓
<User Login?>
  ├─ YES → [Validate Credentials]
  │         ↓
  │       <Valid?>
  │         ├─ YES → [Load Dashboard]
  │         └─ NO → [Show Error] → [Display Login Page]
  │
  └─ NO → [Exit System]
```

---

## DASHBOARD MENU FLOW

```
[Dashboard]
  ↓
[Display Menu Options]
  1. View Inventory
  2. Add Medicine
  3. Update Medicine
  4. Delete Medicine
  5. Low Stock Alert
  6. Expired Medicines
  7. Restock Medicine
  8. Sell Medicine (Generate Bill)
  9. Purchase History
  10. Backup & Restore
  11. User Management (Admin Only)
  12. Logout
  ↓
<User Selection?>
```

---

## 1. VIEW INVENTORY FLOW

```
[View Inventory]
  ↓
[Read inventory.txt]
  ↓
<File Exists?>
  ├─ YES → [Parse Data]
  │         ↓
  │       [Display Table]
  │         • Medicine Name
  │         • Quantity
  │         • Price
  │         • Manufacturing Date
  │         • Expiry Date
  │         • Company
  │         ↓
  │       [Return to Dashboard]
  │
  └─ NO → [Show Error: No Data]
           ↓
          [Return to Dashboard]
```

---

## 2. ADD MEDICINE FLOW

```
[Add Medicine]
  ↓
[Display Add Form]
  ↓
[Input Medicine Details]
  • Name
  • Quantity
  • Price
  • Mfg Date
  • Expiry Date
  • Company
  ↓
<Validate Input?>
  ├─ NO → [Show Error] → [Display Add Form]
  │
  └─ YES → [Check if Medicine Exists]
            ↓
          <Already Exists?>
            ├─ YES → [Show Error: Duplicate]
            │         ↓
            │        [Return to Dashboard]
            │
            └─ NO → [Append to inventory.txt]
                     ↓
                    [Log to history.txt]
                     • Action: Added
                     • User
                     • Timestamp
                     • Details
                     ↓
                    [Show Success Message]
                     ↓
                    [Return to Dashboard]
```

---

## 3. UPDATE MEDICINE FLOW

```
[Update Medicine]
  ↓
[Display Current Inventory]
  ↓
[Input Medicine Name to Update]
  ↓
<Medicine Exists?>
  ├─ NO → [Show Error: Not Found]
  │        ↓
  │       [Return to Dashboard]
  │
  └─ YES → [Display Current Details]
            ↓
           [Input New Details]
            • Quantity (optional)
            • Price (optional)
            • Mfg Date (optional)
            • Expiry Date (optional)
            • Company (optional)
            ↓
           <Validate Input?>
             ├─ NO → [Show Error] → [Input New Details]
             │
             └─ YES → [Update inventory.txt]
                       ↓
                      [Log to history.txt]
                       • Action: Updated
                       • User
                       • Timestamp
                       • Old vs New Values
                       ↓
                      [Show Success Message]
                       ↓
                      [Return to Dashboard]
```

---

## 4. DELETE MEDICINE FLOW

```
[Delete Medicine]
  ↓
[Display Current Inventory]
  ↓
[Input Medicine Name to Delete]
  ↓
<Medicine Exists?>
  ├─ NO → [Show Error: Not Found]
  │        ↓
  │       [Return to Dashboard]
  │
  └─ YES → [Display Medicine Details]
            ↓
           [Confirm Deletion]
            ↓
           <Confirm?>
             ├─ NO → [Cancel] → [Return to Dashboard]
             │
             └─ YES → [Remove from inventory.txt]
                       ↓
                      [Log to history.txt]
                       • Action: Deleted
                       • User
                       • Timestamp
                       • Details
                       ↓
                      [Show Success Message]
                       ↓
                      [Return to Dashboard]
```

---

## 5. LOW STOCK ALERT FLOW

```
[Low Stock Alert]
  ↓
[Read inventory.txt]
  ↓
[Set Threshold = 10]
  ↓
[Filter Medicines]
  • WHERE Quantity < Threshold
  ↓
<Low Stock Items Found?>
  ├─ NO → [Show Message: All Stock OK]
  │        ↓
  │       [Return to Dashboard]
  │
  └─ YES → [Display Low Stock Table]
            • Medicine Name
            • Current Quantity
            • Recommended Restock
            ↓
           [Highlight Critical Items]
            • Quantity < 5 (RED)
            • Quantity < 10 (YELLOW)
            ↓
           [Return to Dashboard]
```

---

## 6. EXPIRED MEDICINES FLOW

```
[Expired Medicines]
  ↓
[Read inventory.txt]
  ↓
[Get Current Date]
  ↓
[Filter Medicines]
  • WHERE Expiry Date < Current Date
  ↓
<Expired Items Found?>
  ├─ NO → [Show Message: No Expired Items]
  │        ↓
  │       [Return to Dashboard]
  │
  └─ YES → [Display Expired Table]
            • Medicine Name
            • Expiry Date
            • Days Expired
            • Quantity
            • Company
            ↓
           [Option: Remove Expired]
            ↓
           <Remove All?>
             ├─ NO → [Return to Dashboard]
             │
             └─ YES → [Delete Expired Items]
                       ↓
                      [Update inventory.txt]
                       ↓
                      [Log to history.txt]
                       ↓
                      [Show Success Message]
                       ↓
                      [Return to Dashboard]
```

---

## 7. RESTOCK MEDICINE FLOW

```
[Restock Medicine]
  ↓
[Display Current Inventory]
  ↓
[Input Medicine Name]
  ↓
<Medicine Exists?>
  ├─ NO → [Show Error: Not Found]
  │        ↓
  │       [Return to Dashboard]
  │
  └─ YES → [Display Current Stock]
            ↓
           [Input Quantity to Add]
            ↓
           <Valid Quantity?>
             ├─ NO → [Show Error] → [Input Quantity to Add]
             │
             └─ YES → [Calculate New Quantity]
                       • New = Current + Added
                       ↓
                      [Update inventory.txt]
                       ↓
                      [Log to history.txt]
                       • Action: Restocked
                       • User
                       • Timestamp
                       • Quantity Added
                       ↓
                      [Show Success Message]
                       ↓
                      [Return to Dashboard]
```

---

## 8. SELL MEDICINE (GENERATE BILL) FLOW

```
[Sell Medicine]
  ↓
[Initialize Bill]
  • Bill Number = Auto Generated
  • Date = Current Date
  • Items List = Empty
  ↓
[Add Item Loop]
  ↓
[Input Medicine Name]
  ↓
<Medicine Exists?>
  ├─ NO → [Show Error: Not Found]
  │        ↓
  │       [Continue Adding?]
  │
  └─ YES → [Display Available Quantity]
            ↓
           [Input Quantity to Sell]
            ↓
           <Valid Quantity?>
             ├─ NO → [Show Error] → [Input Quantity to Sell]
             │
             └─ YES → <Sufficient Stock?>
                       ├─ NO → [Show Error: Insufficient Stock]
                       │        ↓
                       │       [Continue Adding?]
                       │
                       └─ YES → [Calculate Item Total]
                                 • Total = Quantity × Price
                                 ↓
                                [Add to Bill]
                                 ↓
                                [Update inventory.txt]
                                 • Reduce Quantity
                                 ↓
                                [Continue Adding?]
                                 ↓
                               <Add More Items?>
                                 ├─ YES → [Add Item Loop]
                                 │
                                 └─ NO → [Calculate Bill Total]
                                          ↓
                                         [Display Bill]
                                          • Bill Number
                                          • Date
                                          • Items List
                                          • Subtotal
                                          • Tax (if applicable)
                                          • Grand Total
                                          ↓
                                         [Log to history.txt]
                                          • Action: Sale
                                          • User
                                          • Timestamp
                                          • Bill Details
                                          ↓
                                         [Print/Save Bill]
                                          ↓
                                         [Return to Dashboard]
```

---

## 9. PURCHASE HISTORY FLOW

```
[Purchase History]
  ↓
[Read history.txt]
  ↓
<File Exists?>
  ├─ NO → [Show Message: No History]
  │        ↓
  │       [Return to Dashboard]
  │
  └─ YES → [Display Filter Options]
            • All Transactions
            • By Date Range
            • By Action Type
            • By User
            ↓
           [Apply Filter]
            ↓
           [Display History Table]
            • Date & Time
            • User
            • Action Type
            • Medicine Name
            • Quantity
            • Details
            ↓
           [Export Option]
            ↓
           <Export?>
             ├─ YES → [Generate CSV/PDF]
             │         ↓
             │        [Download File]
             │
             └─ NO → [Return to Dashboard]
```

---

## 10. BACKUP & RESTORE FLOW

### Backup Flow
```
[Backup]
  ↓
[Generate Timestamp]
  ↓
[Create Backup Files]
  ├─ inventory_backup_YYYYMMDD_HHMMSS.csv
  ├─ users_backup_YYYYMMDD_HHMMSS.txt
  └─ history_backup_YYYYMMDD_HHMMSS.txt
  ↓
[Save to Backup Directory]
  ↓
[Log to history.txt]
  • Action: Backup Created
  • User
  • Timestamp
  ↓
[Show Success Message]
  ↓
[Return to Dashboard]
```

### Restore Flow
```
[Restore]
  ↓
[List Available Backups]
  ↓
<Backups Found?>
  ├─ NO → [Show Error: No Backups]
  │        ↓
  │       [Return to Dashboard]
  │
  └─ YES → [Display Backup List]
            • Date Created
            • File Names
            ↓
           [Select Backup to Restore]
            ↓
           [Confirm Restore]
            ↓
           <Confirm?>
             ├─ NO → [Cancel] → [Return to Dashboard]
             │
             └─ YES → [Backup Current Data]
                       ↓
                      [Restore Selected Files]
                       ↓
                      [Log to history.txt]
                       • Action: Restore
                       • User
                       • Timestamp
                       • Backup Date
                       ↓
                      [Show Success Message]
                       ↓
                      [Reload System]
                       ↓
                      [Return to Dashboard]
```

---

## 11. USER MANAGEMENT FLOW (ADMIN ONLY)

### View Users
```
[User Management]
  ↓
<User is Admin?>
  ├─ NO → [Show Error: Access Denied]
  │        ↓
  │       [Return to Dashboard]
  │
  └─ YES → [Display User Management Menu]
            1. View All Users
            2. Add User
            3. Edit User
            4. Delete User
            5. Back
            ↓
           <Selection?>
```

### Add User Flow
```
[Add User]
  ↓
[Display Add User Form]
  ↓
[Input User Details]
  • Username
  • Password
  • Role (Admin/User)
  ↓
<Validate Input?>
  ├─ NO → [Show Error] → [Display Add User Form]
  │
  └─ YES → [Check if Username Exists]
            ↓
          <Already Exists?>
            ├─ YES → [Show Error: Username Taken]
            │         ↓
            │        [Display Add User Form]
            │
            └─ NO → [Hash Password]
                     ↓
                    [Append to users.txt]
                     ↓
                    [Log to history.txt]
                     • Action: User Added
                     • Admin User
                     • Timestamp
                     ↓
                    [Show Success Message]
                     ↓
                    [Return to User Management]
```

### Edit User Flow
```
[Edit User]
  ↓
[Display User List]
  ↓
[Select User to Edit]
  ↓
[Display Current Details]
  ↓
[Input New Details]
  • Password (optional)
  • Role (optional)
  ↓
<Validate Input?>
  ├─ NO → [Show Error] → [Input New Details]
  │
  └─ YES → [Update users.txt]
            ↓
           [Log to history.txt]
            ↓
           [Show Success Message]
            ↓
           [Return to User Management]
```

### Delete User Flow
```
[Delete User]
  ↓
[Display User List]
  ↓
[Select User to Delete]
  ↓
<Cannot Delete Self?>
  ├─ YES → [Show Error: Cannot Delete Own Account]
  │         ↓
  │        [Return to User Management]
  │
  └─ NO → [Confirm Deletion]
           ↓
          <Confirm?>
            ├─ NO → [Cancel] → [Return to User Management]
            │
            └─ YES → [Remove from users.txt]
                      ↓
                     [Log to history.txt]
                      ↓
                     [Show Success Message]
                      ↓
                     [Return to User Management]
```

---

## 12. LOGOUT FLOW

```
[Logout]
  ↓
[Confirm Logout]
  ↓
<Confirm?>
  ├─ NO → [Return to Dashboard]
  │
  └─ YES → [Clear Session]
            ↓
           [Save All Data]
            ↓
           [Log to history.txt]
            • Action: Logout
            • User
            • Timestamp
            ↓
           [Redirect to Login Page]
            ↓
           [Display Login Page]
```

---

## ERROR HANDLING FLOW

```
[Any Operation]
  ↓
<Error Occurs?>
  ├─ NO → [Continue Normal Flow]
  │
  └─ YES → [Identify Error Type]
            ├─ File Error → [Log Error]
            │               ↓
            │              [Show User-Friendly Message]
            │               ↓
            │              [Suggest Solution]
            │
            ├─ Validation Error → [Show Error Message]
            │                     ↓
            │                    [Return to Input Form]
            │
            ├─ Permission Error → [Show Access Denied]
            │                     ↓
            │                    [Return to Dashboard]
            │
            └─ System Error → [Log Critical Error]
                              ↓
                             [Show Error Message]
                              ↓
                             [Attempt Recovery]
                              ↓
                             <Recovery Success?>
                               ├─ YES → [Continue]
                               └─ NO → [Safe Shutdown]
```

---

## DATA PERSISTENCE FLOW

```
[Any Data Modification Operation]
  ↓
[Modify In-Memory Data]
  ↓
[Validate Data Integrity]
  ↓
<Valid?>
  ├─ NO → [Rollback Changes]
  │        ↓
  │       [Show Error]
  │
  └─ YES → [Write to File]
            ├─ inventory.txt
            ├─ users.txt
            └─ history.txt
            ↓
           <Write Success?>
             ├─ NO → [Retry Write]
             │        ↓
             │       <Retry Success?>
             │         ├─ NO → [Log Error]
             │         │        ↓
             │         │       [Notify User]
             │         └─ YES → [Continue]
             │
             └─ YES → [Update Timestamp]
                       ↓
                      [Log Transaction]
                       ↓
                      [Continue]
```

---

## SYSTEM INITIALIZATION FLOW

```
START APPLICATION
  ↓
[Check Required Files]
  ├─ inventory.txt
  ├─ users.txt
  └─ history.txt
  ↓
<All Files Exist?>
  ├─ NO → [Create Missing Files]
  │        ↓
  │       [Initialize Default Data]
  │        • Default Admin User
  │        • Sample Inventory (optional)
  │        ↓
  │       [Log Initialization]
  │
  └─ YES → [Load Data Files]
            ↓
           [Validate Data Format]
            ↓
           <Valid Format?>
             ├─ NO → [Show Error]
             │        ↓
             │       [Attempt Data Recovery]
             │        ↓
             │       <Recovery Success?>
             │         ├─ NO → [Exit with Error]
             │         └─ YES → [Continue]
             │
             └─ YES → [Initialize Flask App]
                       ↓
                      [Start Web Server]
                       ↓
                      [Display Login Page]
```

---

## NOTES FOR FLOWCHART CREATION

**Symbols to Use:**
- **Oval** = Start/End
- **Rectangle** = Process/Action
- **Diamond** = Decision
- **Parallelogram** = Input/Output
- **Document** = File Operation
- **Cylinder** = Database/Storage

**Color Coding:**
- Green = Success Path
- Red = Error Path
- Blue = Admin Functions
- Yellow = Warning/Alert Functions

**Key Points:**
- All operations log to history.txt
- All data modifications update files immediately
- User authentication checked at each admin operation
- Input validation occurs before any data modification
- Backups created before restore operations

