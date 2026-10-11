#include "../Headers/ShopList.h"

#include <algorithm>
#include <map>


const char* QualityName(int quality) {
	switch (quality) {
	case QUALITY_NORMAL: return "Normal";
	case QUALITY_GOOD: return "Good";
	case QUALITY_OUTSTANDING: return "Outstanding";
	case QUALITY_EXCELLENT: return "Excellent";
	case QUALITY_MASTERPIECE: return "Masterpiece";
	default: return "?";
	}
}


vector<ShopBlock> BuildShopList(const vector<FlipLot>& lots) {
	// Keyed by the three things the search box is built from, not by
	// searchText itself: searchText is derived from exactly these, and keying
	// on the id keeps two different items that happen to share a display name
	// from being merged into one block.
	typedef pair<string, pair<int, int> > Key;

	map<Key, size_t> index;
	vector<ShopBlock> blocks;

	for (const FlipLot& lot : lots) {
		if (lot.quantity <= 0) {
			continue;
		}

		const Key key(lot.itemTypeId, make_pair(lot.tier, lot.enchant));
		map<Key, size_t>::iterator found = index.find(key);

		if (found == index.end()) {
			ShopBlock block;
			block.searchText = lot.searchText;
			block.displayName = lot.displayName;
			block.tier = lot.tier;
			block.enchant = lot.enchant;
			block.quantity = lot.quantity;
			block.minQuality = lot.bmQuality;
			block.ceiling = lot.marginCeiling;
			block.profit = lot.profit;
			block.spend = lot.spend;
			block.bands = 1;

			index[key] = blocks.size();
			blocks.push_back(block);
			continue;
		}

		ShopBlock& block = blocks[found->second];
		block.quantity += lot.quantity;
		block.profit += lot.profit;
		block.spend += lot.spend;
		++block.bands;

		// The two numbers that must come out conservative when bands merge.
		// Everything above adds up; these two do not.
		if (lot.bmQuality < block.minQuality) {
			block.minQuality = lot.bmQuality;
		}
		if (lot.marginCeiling < block.ceiling) {
			block.ceiling = lot.marginCeiling;
		}

		// A lot without a name cannot be searched for by hand. If any lot in
		// the block has one, keep it - the block is still worth showing.
		if (block.searchText.empty() && !lot.searchText.empty()) {
			block.searchText = lot.searchText;
		}
	}

	// Same ranking rule as the plan itself: most profit first, and on a tie the
	// one that ties up less silver. Applied to block totals rather than lot
	// totals, because a block is the unit of work being ordered.
	stable_sort(blocks.begin(), blocks.end(),
		[](const ShopBlock& a, const ShopBlock& b) {
			if (a.profit != b.profit) {
				return a.profit > b.profit;
			}
			return a.spend < b.spend;
		});

	return blocks;
}
