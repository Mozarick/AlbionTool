#pragma once
#include <string>
#include <vector>

using namespace std;


// One stack the bot bought and has not sold on yet.
struct Holding {
	string itemTypeId;    // T4_2H_DUALCROSSBOW_HELL@2
	string searchText;    // "Boltcasters T4.2"
	int tier = 0;
	int enchant = 0;
	int quality = 0;
	int quantity = 0;
	long long unitCost = 0;   // average silver paid per unit
};


// What the character is carrying, as far as the bot knows.
//
// This exists because the sell run used to walk every buy order the black
// market happened to have. That meant searching for items the character does
// not own and clicking sell on whatever row was underneath, which looks
// exactly like the bot clicking at random. Selling now walks this list, so it
// only ever tries to sell things it actually bought.
//
// Saved to disk because buying and selling happen in different cities with a
// journey in between, and losing the ledger would leave the goods unsellable.
class Holdings {
public:
	// Merges into an existing stack of the same item, enchant AND quality,
	// keeping a weighted average of what was paid.
	//
	// Enchant is part of the identity, not decoration: itemTypeId is the base
	// id with no "@n" suffix, so T6_MAIN_SWORD@1 and T6_MAIN_SWORD@2 arrive
	// here with the same itemTypeId and would otherwise merge into one stack
	// - blending two different price points and leaving the sell run looking
	// for the wrong enchant's buy orders.
	void Add(const string& itemTypeId, const string& searchText,
		int tier, int enchant, int quality,
		int quantity, long long unitCost);

	// Returns how many were actually removed. Matches on the same three
	// fields Add merges on.
	int Remove(const string& itemTypeId, int enchant, int quality, int quantity);

	const vector<Holding>& All() const;
	int TotalUnits() const;
	bool Empty() const;
	void Clear();

	bool Save(const string& path) const;
	bool Load(const string& path);

private:
	vector<Holding> stacks;
};
