// Proves LogSink::Wipe actually truncates bot.log, rather than only clearing
// the on-screen panel. Run from a scratch directory: it writes bot.log there.
#include "../SFML/Headers/LogSink.h"
#include <iostream>
using namespace std;

static long long Size(const char* path) {
    ifstream f(path, ios::binary | ios::ate);
    return f ? (long long)f.tellg() : -1;
}

int main() {
    cout.rdbuf(&GlobalLog);

    for (int i = 0; i < 200; ++i) {
        cout << "old scan line " << i << " that should not survive\n";
    }
    long long before = Size("bot.log");

    GlobalLog.Wipe();
    long long afterWipe = Size("bot.log");

    cout << "first line after the wipe\n";
    long long afterWrite = Size("bot.log");

    cout.rdbuf(NULL);
    fprintf(stderr, "before=%lld afterWipe=%lld afterWrite=%lld panelLines=%zu\n",
        before, afterWipe, afterWrite, GlobalLog.Lines(500).size());
    return (before > 1000 && afterWipe == 0 && afterWrite > 0) ? 0 : 1;
}
