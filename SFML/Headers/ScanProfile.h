#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace std;

class CategoryTable;

// Which tiers to walk for one category, per location. An empty tier list
// means "one pass, never open the tier dropdown" - the black market default,
// since the account doesn't filter black market listings by tier at all.
// A non-empty list means "step through exactly these tiers, in order" - the
// royal city default is {6,7,8}, skipping the low-value 4/5 flips.
//
// Enchantments work the same way but default to empty, which keeps the old
// behaviour exactly: the enchantment dropdown is never opened and the listing
// arrives with every level mixed together. Setting levels here splits each
// tier into one pass per level, which is the only way past the game's
// twenty-tab cap on a busy tier - see EnchantmentLayout in MarketUi.h. The
// black market ignores them.
//
// Persisted to scan_profile.tsv so a chosen setup survives a restart:
//   location \t category_key \t tiers \t enchants
// Tiers blank = one pass. The enchant column was added later, so a row with
// only three fields still loads - it just means "no enchantment filter".
class ScanProfile {
public:
	// Fills in any category missing from the file with the location's
	// default, so the profile always has an answer for every category in
	// the table, even ones added to categories.tsv after this file was last
	// saved.
	void EnsureDefaults(const CategoryTable& categories);

	bool Load(const string& path);
	void Save(const string& path) const;

	const vector<int>& TiersFor(bool blackMarket, const string& categoryKey) const;
	void SetTiers(bool blackMarket, const string& categoryKey, vector<int> tiers);

	const vector<int>& EnchantsFor(bool blackMarket, const string& categoryKey) const;
	void SetEnchants(bool blackMarket, const string& categoryKey, vector<int> enchants);

private:
	static string LocationKey(bool blackMarket);

	// (location, category key) -> tiers
	map<pair<string, string>, vector<int>> tiers;
	// same key, enchantment levels to split each tier by
	map<pair<string, string>, vector<int>> enchants;
	static const vector<int> kEmpty;
};
