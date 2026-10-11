#pragma once
#include "ShopList.h"

namespace Studio {
    class SingleInstance {
    public:
        explicit SingleInstance(const wchar_t* name);
        ~SingleInstance();
        SingleInstance(const SingleInstance&) = delete;
        SingleInstance& operator=(const SingleInstance&) = delete;
        bool AlreadyRunning() const { return alreadyRunning; }
    private:
        uintptr_t handle = 0;
        bool alreadyRunning = false;
    };
    struct Session {
        vector<ShopBlock> blocks;
        string title;
        long long savedAt = 0;
        unsigned long long generation = 0;
        ShopEstimate estimate;
    };
    bool SavePlan(const string& path, const Session& session, string& error);
    bool SaveProgress(const string& path, const Session& session, string& error);
    bool Load(const string& planPath, const string& progressPath, Session& out, string& error);
    bool ExportCsv(const string& path, const vector<ShopBlock>& blocks,
        const vector<size_t>& visible, const set<size_t>& copied, string& error);
    string Number(long long value);
    string ShortAge(long long utcSeconds);
    unsigned long long NextGeneration(unsigned long long current);
}
