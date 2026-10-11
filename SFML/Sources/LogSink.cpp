#include "../Headers/LogSink.h"

LogSink GlobalLog;

LogSink::LogSink() {
	// Deliberately empty. See EnsureOpen(): opening the file here would run at
	// static-init time, before main() has fixed the working directory.
}

void LogSink::EnsureOpen() {
	if (opened) {
		return;
	}
	opened = true;

	// Start a fresh file once the old one gets big. Every line the bot prints
	// is mirrored here, and a scan prints per category and per tier, so an
	// append-only file grows without bound over months. Truncating at startup
	// (rather than mid-run) keeps the whole of the current session intact,
	// which is what anyone diagnosing a problem actually needs.
	const long long kMaxBytes = 5LL * 1024 * 1024;

	bool truncate = false;
	{
		ifstream existing("bot.log", ios::binary | ios::ate);
		if (existing && existing.tellg() > kMaxBytes) {
			truncate = true;
		}
	}

	file.open("bot.log", truncate ? ios::trunc : ios::app);
	if (file) {
		if (truncate) {
			file << "--- previous log discarded, it had passed 5 MB ---\n";
		}
		file << "\n--- session start ---\n";
		file.flush();
	}
}

int LogSink::overflow(int ch) {
	if (ch == char_traits<char>::eof()) {
		return ch;
	}

	lock_guard<mutex> lock(mtx);
	if (ch == '\n') {
		PushLine();
	}
	else {
		current += char(ch);
	}
	return ch;
}

// caller holds mtx
void LogSink::PushLine() {
	EnsureOpen();

	lines.push_back(current);
	if (file) {
		file << current << '\n';
		file.flush();
	}
	current.clear();

	while (lines.size() > kCapacity) {
		lines.pop_front();
	}
}

vector<string> LogSink::Lines(size_t maxLines) const {
	lock_guard<mutex> lock(mtx);
	vector<string> out;
	size_t start = lines.size() > maxLines ? lines.size() - maxLines : 0;
	for (size_t i = start; i < lines.size(); ++i) {
		out.push_back(lines[i]);
	}
	return out;
}

void LogSink::Clear() {
	lock_guard<mutex> lock(mtx);
	lines.clear();
	current.clear();
}

void LogSink::Wipe() {
	lock_guard<mutex> lock(mtx);
	lines.clear();
	current.clear();

	// Reopened with trunc rather than deleted: the stream stays valid, so a
	// line written a microsecond later still has somewhere to go. Deleting the
	// file out from under an open ofstream would leave writes going into a
	// handle with no name, and the log would silently stop working.
	if (file.is_open()) {
		file.close();
	}
	file.open("bot.log", ios::trunc);
	opened = true;
}
