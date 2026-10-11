#include "../Headers/Holdings.h"

#include <fstream>
#include <iostream>

using namespace std;


void Holdings::Add(const string& itemTypeId, const string& searchText,
	int tier, int enchant, int quality, int quantity, long long unitCost) {

	if (quantity <= 0) {
		return;
	}

	for (Holding& stack : stacks) {
		if (stack.itemTypeId != itemTypeId ||
			stack.enchant != enchant ||
			stack.quality != quality) {
			continue;
		}

		// weighted average, so the ledger still shows what the goods cost
		// after buying the same thing twice at different prices. Rounded
		// rather than truncated: plain integer division loses a fraction on
		// every merge, always downwards, which quietly understates the cost
		// the sell side has to clear.
		long long totalCost = stack.unitCost * stack.quantity
			+ unitCost * quantity;
		stack.quantity += quantity;
		stack.unitCost = (totalCost + stack.quantity / 2) / stack.quantity;
		return;
	}

	Holding stack;
	stack.itemTypeId = itemTypeId;
	stack.searchText = searchText;
	stack.tier = tier;
	stack.enchant = enchant;
	stack.quality = quality;
	stack.quantity = quantity;
	stack.unitCost = unitCost;
	stacks.push_back(std::move(stack));
}

int Holdings::Remove(const string& itemTypeId, int enchant, int quality,
	int quantity) {
	if (quantity <= 0) {
		return 0;
	}

	for (size_t i = 0; i < stacks.size(); ++i) {
		Holding& stack = stacks[i];
		if (stack.itemTypeId != itemTypeId ||
			stack.enchant != enchant ||
			stack.quality != quality) {
			continue;
		}

		int taken = stack.quantity < quantity ? stack.quantity : quantity;
		stack.quantity -= taken;
		if (stack.quantity <= 0) {
			stacks.erase(stacks.begin() + i);
		}
		return taken;
	}
	return 0;
}

const vector<Holding>& Holdings::All() const {
	return stacks;
}

int Holdings::TotalUnits() const {
	int total = 0;
	for (const Holding& stack : stacks) {
		total += stack.quantity;
	}
	return total;
}

bool Holdings::Empty() const {
	return stacks.empty();
}

void Holdings::Clear() {
	stacks.clear();
}

bool Holdings::Save(const string& path) const {
	ofstream fout(path);
	if (!fout) {
		return false;
	}

	// itemTypeId \t tier \t enchant \t quality \t quantity \t unitCost \t name
	for (const Holding& stack : stacks) {
		fout << stack.itemTypeId << '\t' << stack.tier << '\t'
			<< stack.enchant << '\t' << stack.quality << '\t'
			<< stack.quantity << '\t' << stack.unitCost << '\t'
			<< stack.searchText << '\n';
	}
	return true;
}

bool Holdings::Load(const string& path) {
	ifstream fin(path);
	if (!fin) {
		return false;
	}

	stacks.clear();

	string line;
	while (getline(fin, line)) {
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
			line.pop_back();
		}
		if (line.empty()) {
			continue;
		}

		string parts[7];
		size_t at = 0;
		int found = 0;
		for (; found < 7; ++found) {
			size_t tab = line.find('\t', at);
			if (tab == string::npos) {
				parts[found] = line.substr(at);
				++found;
				break;
			}
			parts[found] = line.substr(at, tab - at);
			at = tab + 1;
		}
		if (found < 6 || parts[0].empty()) {
			continue;
		}

		Holding stack;
		stack.itemTypeId = parts[0];
		stack.tier = atoi(parts[1].c_str());
		stack.enchant = atoi(parts[2].c_str());
		stack.quality = atoi(parts[3].c_str());
		stack.quantity = atoi(parts[4].c_str());
		stack.unitCost = atoll(parts[5].c_str());
		stack.searchText = parts[6];

		if (stack.quantity > 0) {
			stacks.push_back(std::move(stack));
		}
	}

	if (!stacks.empty()) {
		cout << "Holdings: carrying " << TotalUnits() << " units in "
			<< stacks.size() << " stacks\n";
	}
	return true;
}
