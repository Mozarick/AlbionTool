#include "../Headers/StudioSession.h"
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <sstream>
#include <algorithm>

namespace Studio {
using nlohmann::json;
SingleInstance::SingleInstance(const wchar_t* name) {
    if (!name) return;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, name);
    if (!mutex) throw std::runtime_error("Cannot reserve the application instance");
    alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
    handle = reinterpret_cast<uintptr_t>(mutex);
}
SingleInstance::~SingleInstance() { if (handle) CloseHandle(reinterpret_cast<HANDLE>(handle)); }
namespace {
    bool AtomicWrite(const string& path, const string& data, string& error) {
        try {
            const auto file = std::filesystem::u8path(path);
            auto temp = file; temp += L".tmp";
            {
                std::ofstream output(temp, std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error("Cannot open the save file");
                output.write(data.data(), std::streamsize(data.size()));
                output.flush();
                if (!output) throw std::runtime_error("Cannot write the save file");
            }
            if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot replace the save file");
            error.clear(); return true;
        } catch (const std::exception& e) { error = e.what(); return false; }
    }
    json Read(const string& path) {
        const auto file = std::filesystem::u8path(path);
        if (std::filesystem::file_size(file) > 32u * 1024u * 1024u)
            throw std::runtime_error("Session file is too large");
        std::ifstream input(file, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read the session");
        json doc; input >> doc; return doc;
    }
    string Cell(string text) {
        if (!text.empty() && (text[0] == '=' || text[0] == '+' || text[0] == '-' || text[0] == '@'))
            text.insert(text.begin(), '\'');
        string result = "\"";
        for (char c : text) { if (c == '"') result += '"'; result += c; }
        return result + '"';
    }
}

bool SavePlan(const string& path, const Session& session, string& error) {
    try {
        json rows = json::array();
        for (const auto& b : session.blocks)
            rows.push_back({b.searchText, b.displayName, b.tier, b.enchant, b.quantity,
                b.minQuality, b.ceiling, b.profit, b.spend, b.bands});
        return AtomicWrite(path, json({{"schema", 1}, {"generation", session.generation},
            {"saved_at", session.savedAt}, {"title", session.title}, {"rows", rows}}).dump(), error);
    } catch (const std::exception& e) { error = e.what(); return false; }
}
bool SaveProgress(const string& path, const Session& session, string& error) {
    try {
        json copies = json::array();
        for (const auto& record : session.estimate.History()) copies.push_back(record.first);
        return AtomicWrite(path, json({{"schema", 1}, {"generation", session.generation},
            {"copies", copies}}).dump(), error);
    } catch (const std::exception& e) { error = e.what(); return false; }
}
bool Load(const string& planPath, const string& progressPath, Session& out, string& error) {
    try {
        if (!std::filesystem::exists(std::filesystem::u8path(planPath))) { error.clear(); return true; }
        const json doc = Read(planPath);
        if (doc.at("schema") != 1) throw std::runtime_error("Unsupported session version");
        Session parsed;
        parsed.generation = doc.at("generation").get<unsigned long long>();
        parsed.savedAt = doc.at("saved_at").get<long long>();
        parsed.title = doc.at("title").get<string>();
        const auto& rows = doc.at("rows");
        if (!rows.is_array() || rows.size() > 50000 || parsed.title.size() > 100 ||
            parsed.generation >= (std::numeric_limits<unsigned long long>::max)() ||
            parsed.savedAt < 0 || parsed.savedAt > static_cast<long long>(std::time(nullptr)) + 60)
            throw std::runtime_error("Invalid session data");
        long long totalProfit = 0;
        for (const auto& row : rows) {
            if (!row.is_array() || row.size() != 10) throw std::runtime_error("Invalid session row");
            ShopBlock b;
            b.searchText = row[0].get<string>(); b.displayName = row[1].get<string>();
            b.tier = row[2].get<int>(); b.enchant = row[3].get<int>(); b.quantity = row[4].get<int>();
            b.minQuality = row[5].get<int>(); b.ceiling = row[6].get<long long>();
            b.profit = row[7].get<long long>(); b.spend = row[8].get<long long>(); b.bands = row[9].get<int>();
            if (b.searchText.size() > 512 || b.displayName.size() > 512 || b.quantity <= 0 ||
                b.tier < 0 || b.tier > 8 || b.enchant < 0 || b.enchant > 4 ||
                b.minQuality < 1 || b.minQuality > 5 || b.ceiling < 0 || b.spend < 0 || b.bands < 1 ||
                b.profit < 0 || totalProfit > (std::numeric_limits<long long>::max)() - b.profit)
                throw std::runtime_error("Invalid session row values");
            totalProfit += b.profit;
            parsed.blocks.push_back(std::move(b));
        }
        // A valid plan is still useful when progress is damaged or belongs
        // to the previous plan. Never apply old copies to a new generation.
        string progressError;
        if (std::filesystem::exists(std::filesystem::u8path(progressPath))) {
            try {
                const json progress = Read(progressPath);
                if (progress.at("schema") != 1) throw std::runtime_error("Unsupported progress version");
                if (progress.at("generation").get<unsigned long long>() == parsed.generation) {
                    const auto& copies = progress.at("copies");
                    if (!copies.is_array() || copies.size() > 100000) throw std::runtime_error("Invalid progress");
                    for (const auto& copy : copies) {
                        size_t index = copy.get<size_t>();
                        if (index >= parsed.blocks.size() || !parsed.estimate.Copy(parsed.blocks[index].profit, index))
                            throw std::runtime_error("Invalid copy history");
                    }
                }
            } catch (const std::exception&) { parsed.estimate.Reset(); progressError = "Saved list recovered; copy history was damaged."; }
        }
        out = std::move(parsed); error = progressError; return true;
    } catch (const std::exception&) { error = "Saved session could not be read. Scan and build a new plan."; return false; }
}
bool ExportCsv(const string& path, const vector<ShopBlock>& blocks,
    const vector<size_t>& visible, const set<size_t>& copied, string& error) {
    std::ostringstream csv;
    csv << "Search,Item,Tier,Enchantment,Max quantity,Minimum quality,Buy under,Estimated profit,Spend,Copied\r\n";
    for (size_t index : visible) {
        if (index >= blocks.size()) { error = "Invalid export selection"; return false; }
        const auto& b = blocks[index];
        csv << Cell(b.searchText) << ',' << Cell(b.displayName) << ',' << b.tier << ',' << b.enchant << ','
            << b.quantity << ',' << b.minQuality << ',' << b.ceiling << ',' << b.profit << ',' << b.spend << ','
            << (copied.count(index) ? "yes" : "no") << "\r\n";
    }
    return AtomicWrite(path, csv.str(), error);
}
string Number(long long value) {
    string result = std::to_string(value);
    for (size_t pos = result.size(); pos > 3;) {
        pos -= 3;
        if (pos > (value < 0 ? 1u : 0u)) result.insert(pos, ",");
    }
    return result;
}
string ShortAge(long long utcSeconds) {
    if (!utcSeconds) return "not saved";
    const long long age = (std::max)(0LL, static_cast<long long>(std::time(nullptr)) - utcSeconds);
    if (age < 60) return "just now";
    if (age < 3600) return std::to_string(age / 60) + "m ago";
    if (age < 86400) return std::to_string(age / 3600) + "h ago";
    return std::to_string(age / 86400) + "d ago";
}
unsigned long long NextGeneration(unsigned long long current) {
    const auto clock = static_cast<unsigned long long>(std::time(nullptr)) * 1000000 + GetTickCount64() % 1000000;
    return (std::max)(current + 1, clock);
}
}
