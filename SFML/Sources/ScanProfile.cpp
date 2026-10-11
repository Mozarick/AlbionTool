#include "../Headers/ScanProfile.h"
#include "../Headers/MarketScanner.h"

#include <fstream>
#include <iostream>
#include <sstream>

using namespace std;

const vector<int> ScanProfile::kEmpty = {};

string ScanProfile::LocationKey(bool blackMarket) {
	return blackMarket ? "bm" : "mk";
}

void ScanProfile::EnsureDefaults(const CategoryTable& categories) {
	for (const auto& row : categories.All()) {
		auto bmKey = make_pair(LocationKey(true), row.key);
		if (!tiers.count(bmKey)) {
			// black market: one pass, no tier filter
			tiers[bmKey] = {};
		}
		auto mkKey = make_pair(LocationKey(false), row.key);
		if (!tiers.count(mkKey)) {
			// royal city: tier 6/7/8 only, skip the low-value 4/5 flips
			tiers[mkKey] = { 6, 7, 8 };
		}
	}

	// Enchantments are deliberately left unset, which reads as empty: turning
	// them on multiplies the length of every scan by the number of levels
	// picked, so it is opt-in per category from the settings screen rather
	// than something a first run discovers the hard way.
}

bool ScanProfile::Load(const string& path) {
	ifstream fin(path);
	if (!fin) {
		return false;
	}

	tiers.clear();
	enchants.clear();
	string line;
	while (getline(fin, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
			line.pop_back();
		}

		size_t firstTab = line.find('\t');
		if (firstTab == string::npos) {
			continue;
		}
		size_t secondTab = line.find('\t', firstTab + 1);
		if (secondTab == string::npos) {
			continue;
		}

		string location = line.substr(0, firstTab);
		string key = line.substr(firstTab + 1, secondTab - firstTab - 1);
		if (location.empty() || key.empty()) {
			continue;
		}

		// The enchant column was added after this file format shipped, so a
		// three-field row is not malformed - it is a profile saved before the
		// enchantment filter existed, and it means "no enchantment filter".
		string tiersField;
		string enchantsField;
		size_t thirdTab = line.find('\t', secondTab + 1);
		if (thirdTab == string::npos) {
			tiersField = line.substr(secondTab + 1);
		}
		else {
			tiersField = line.substr(secondTab + 1, thirdTab - secondTab - 1);
			enchantsField = line.substr(thirdTab + 1);
		}

		auto parseList = [](const string& field) {
			vector<int> parsed;
			stringstream ss(field);
			string tok;
			while (getline(ss, tok, ',')) {
				if (!tok.empty()) {
					try {
						parsed.push_back(stoi(tok));
					}
					catch (...) {
						// malformed entry, skip just this token
					}
				}
			}
			return parsed;
		};

		auto entryKey = make_pair(location, key);
		tiers[entryKey] = parseList(tiersField);
		enchants[entryKey] = parseList(enchantsField);
	}

	cout << "ScanProfile: loaded " << tiers.size() << " entries\n";
	return true;
}

void ScanProfile::Save(const string& path) const {
	ofstream fout(path, ios::trunc);
	if (!fout) {
		cout << "ScanProfile: could not write " << path << '\n';
		return;
	}

	fout << "# location\tcategory_key\ttiers (blank = one pass, no tier filter)"
		"\tenchants (blank = no enchantment filter)\n";

	auto writeList = [&fout](const vector<int>& values) {
		for (size_t i = 0; i < values.size(); ++i) {
			if (i) {
				fout << ',';
			}
			fout << values[i];
		}
	};

	for (const auto& entry : tiers) {
		fout << entry.first.first << '\t' << entry.first.second << '\t';
		writeList(entry.second);
		fout << '\t';

		auto it = enchants.find(entry.first);
		if (it != enchants.end()) {
			writeList(it->second);
		}
		fout << '\n';
	}
}

const vector<int>& ScanProfile::TiersFor(bool blackMarket, const string& categoryKey) const {
	auto it = tiers.find(make_pair(LocationKey(blackMarket), categoryKey));
	if (it == tiers.end()) {
		return kEmpty;
	}
	return it->second;
}

void ScanProfile::SetTiers(bool blackMarket, const string& categoryKey, vector<int> newTiers) {
	tiers[make_pair(LocationKey(blackMarket), categoryKey)] = std::move(newTiers);
}

const vector<int>& ScanProfile::EnchantsFor(bool blackMarket, const string& categoryKey) const {
	auto it = enchants.find(make_pair(LocationKey(blackMarket), categoryKey));
	if (it == enchants.end()) {
		return kEmpty;
	}
	return it->second;
}

void ScanProfile::SetEnchants(bool blackMarket, const string& categoryKey, vector<int> newEnchants) {
	enchants[make_pair(LocationKey(blackMarket), categoryKey)] = std::move(newEnchants);
}
