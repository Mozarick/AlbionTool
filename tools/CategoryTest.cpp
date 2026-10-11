// Checks the category menu table against categories.tsv without touching the
// game: every scannable category must have coordinates, no two may point at
// the same menu row, and the click boxes must actually be boxes.
//
// This exists because CategoryLayout is a hand-maintained parallel array. It
// is keyed by string and sized by a literal, so a category added to
// categories.tsv and forgotten here fails at scan time with one line in the
// log - after the run has already started. Adding Capes took it from 31 rows
// to 46, which is exactly the kind of edit that leaves one behind.

#include "../SFML/Headers/MarketScanner.h"

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <vector>

using namespace std;

static int failures = 0;

static void Check(bool ok, const string& what) {
    cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok) {
        ++failures;
    }
}

struct Row {
    string key;
    string menuTop;
    string menuSub;
};

// Deliberately a second, independent reader rather than CategoryTable::Load.
// Compiling MarketScanner.cpp here would drag in OpenCV and tesseract for a
// test that only reads a text file, and a checker that shares its parser with
// the thing it checks agrees with it by construction.
static vector<Row> LoadRows(const string& path) {
    vector<Row> rows;
    ifstream fin(path);
    if (!fin) {
        cout << "cannot open " << path << "\n";
        ++failures;
        return rows;
    }

    string line;
    while (getline(fin, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }

        vector<string> parts;
        size_t at = 0;
        for (;;) {
            size_t tab = line.find('\t', at);
            if (tab == string::npos) {
                parts.push_back(line.substr(at));
                break;
            }
            parts.push_back(line.substr(at, tab - at));
            at = tab + 1;
        }
        if (parts.size() < 3) {
            continue;
        }

        Row row;
        row.key = parts[0];
        row.menuTop = parts[1];
        row.menuSub = parts[2];
        rows.push_back(row);
    }
    return rows;
}

static int Width(const ClickRect& r) {
    return r.x2 - r.x1;
}

static int Height(const ClickRect& r) {
    return r.y2 - r.y1;
}

static int MidY(const ClickRect& r) {
    return (r.y1 + r.y2) / 2;
}

int main() {
    const vector<Row> rows = LoadRows("../SFML/categories.tsv");
    cout << "categories.tsv: " << rows.size() << " rows\n";
    Check(!rows.empty(), "categories.tsv loaded");

    CategoryLayout layout;
    const size_t entryCount =
        sizeof(layout.entries) / sizeof(layout.entries[0]);
    cout << "CategoryLayout: " << entryCount << " entries\n";

    // ---- 1. every scannable category can actually be clicked ----
    {
        int missing = 0;
        for (const Row& row : rows) {
            if (layout.Find(row.key) == NULL) {
                cout << "    no coordinates for " << row.key << "\n";
                ++missing;
            }
        }
        Check(missing == 0, "every categories.tsv row has coordinates");
    }

    // ---- 2. and nothing here points at a category that no longer exists ----
    {
        set<string> known;
        for (const Row& row : rows) {
            known.insert(row.key);
        }

        int orphans = 0;
        for (size_t i = 0; i < entryCount; ++i) {
            if (known.count(layout.entries[i].key) == 0) {
                cout << "    orphan entry " << layout.entries[i].key << "\n";
                ++orphans;
            }
        }
        Check(orphans == 0, "no coordinates for categories that were removed");
        Check(entryCount == rows.size(),
            "the table is exactly as long as categories.tsv");
    }

    // ---- 3. two categories must never share a menu path ----
    //
    // A copy-paste slip in the y column is invisible by inspection and silent
    // at run time: the scan simply brings back the wrong category's listing,
    // filed under the right name.
    {
        map<pair<int, int>, string> seen;
        int clashes = 0;
        for (size_t i = 0; i < entryCount; ++i) {
            const CategoryLayout::Entry& e = layout.entries[i];
            const pair<int, int> path(MidY(e.parent), MidY(e.sub));

            auto found = seen.find(path);
            if (found != seen.end()) {
                cout << "    " << e.key << " and " << found->second
                     << " both click parent y=" << path.first
                     << " sub y=" << path.second << "\n";
                ++clashes;
                continue;
            }
            seen[path] = e.key;
        }
        Check(clashes == 0, "no two categories click the same menu row");
    }

    // ---- 4. the UI matches categories by menuSub, so it has to be unique ----
    //
    // Game.cpp compares the button's label against row.menuSub to decide what
    // was clicked. Two categories sharing a label would toggle each other.
    {
        map<string, string> byLabel;
        int clashes = 0;
        for (const Row& row : rows) {
            auto found = byLabel.find(row.menuSub);
            if (found != byLabel.end()) {
                cout << "    \"" << row.menuSub << "\" used by both "
                     << row.key << " and " << found->second << "\n";
                ++clashes;
                continue;
            }
            byLabel[row.menuSub] = row.key;
        }
        Check(clashes == 0, "every menu_sub label is unique");
    }

    // ---- 5. the boxes are boxes, not points ----
    //
    // They were 2x2 px, which ClickInside reduces to nine pixels - a fixed
    // aim point in all but name. Wide in x, tight in y: the 27px row pitch is
    // a vertical constraint only.
    {
        int narrow = 0;
        int tall = 0;
        for (size_t i = 0; i < entryCount; ++i) {
            const CategoryLayout::Entry& e = layout.entries[i];

            if (Width(e.parent) < 20 || Width(e.sub) < 20) {
                cout << "    " << e.key << " has a narrow box ("
                     << Width(e.parent) << ", " << Width(e.sub) << ")\n";
                ++narrow;
            }
            // Half the 27px pitch would already touch a neighbour; 8 leaves
            // room for the click to land nowhere near one.
            if (Height(e.parent) > 8 || Height(e.sub) > 8) {
                cout << "    " << e.key << " has a tall box ("
                     << Height(e.parent) << ", " << Height(e.sub) << ")\n";
                ++tall;
            }
        }
        Check(narrow == 0, "every box is wide enough to vary the aim point");
        Check(tall == 0, "and short enough never to reach the next row");
    }

    // ---- 6. every row sits on the 27px grid ----
    {
        int offGrid = 0;
        for (size_t i = 0; i < entryCount; ++i) {
            const CategoryLayout::Entry& e = layout.entries[i];
            if ((MidY(e.parent) - 324) % 27 != 0 ||
                (MidY(e.sub) - 324) % 27 != 0) {
                cout << "    " << e.key << " is off the grid (parent "
                     << MidY(e.parent) << ", sub " << MidY(e.sub) << ")\n";
                ++offGrid;
            }
        }
        Check(offGrid == 0, "every row lands on the 27px menu grid");
    }

    // ---- 7. the submenu watch region has to cover the deepest row ----
    //
    // Capes reaches y=864, well past where the longest weapon submenu ended.
    // SettleRegion only ever hashes this area, so a row below it is a row
    // whose redraw the scanner cannot see.
    {
        ScannerLayout scanner;
        int deepest = 0;
        string deepestKey;
        for (size_t i = 0; i < entryCount; ++i) {
            if (MidY(layout.entries[i].sub) > deepest) {
                deepest = MidY(layout.entries[i].sub);
                deepestKey = layout.entries[i].key;
            }
        }

        cout << "deepest submenu row: " << deepestKey << " at y=" << deepest
             << ", subList reaches " << scanner.subList.y2 << "\n";
        Check(deepest <= scanner.subList.y2,
            "subList covers the deepest submenu row");
    }

    cout << (failures == 0 ? "\nCategoryTest PASSED\n"
                           : "\nCategoryTest FAILED\n");
    return failures == 0 ? 0 : 1;
}
