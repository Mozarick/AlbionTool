#pragma once
#include <deque>
#include <fstream>
#include <mutex>
#include <streambuf>
#include <string>
#include <vector>

using namespace std;

// Everything that used to go to the console window now goes here instead:
// cout is redirected onto this streambuf at the very start of main(), before
// anything else can write to it. The GUI draws the last few lines as a log
// panel; every line is also mirrored to bot.log so nothing is lost when
// there is no console left to scroll back through.
//
// Deliberately unbuffered (no put-area is ever set), so every character
// written goes through overflow(). That is the simplest correct
// implementation and the bot only prints a status line here and there, not
// a hot loop, so the per-character call overhead does not matter.
class LogSink : public streambuf {
public:
	LogSink();

	// Newest last. maxLines caps how many are copied out for drawing.
	vector<string> Lines(size_t maxLines) const;

	// Empties the on-screen panel only. bot.log keeps everything, which is
	// the point: the panel is wiped between screens so a fresh plan is
	// readable, and losing the file with it would throw away the record of
	// what just happened.
	void Clear();

	// Empties the panel AND truncates bot.log, so the next line written is
	// the first line in the file. For the Clear button, which is meant to
	// leave nothing behind from previous runs - a log that quietly spans
	// days of testing is exactly what "clear the history" is asking to be
	// rid of.
	void Wipe();

protected:
	int overflow(int ch) override;

private:
	void PushLine();

	// Opens bot.log on the first line actually written, not in the
	// constructor. GlobalLog is a global object, so its constructor runs at
	// static-init time - before main() gets a chance to call
	// SetWorkingDirectoryToBotFolder(). Opening "bot.log" that early opens it
	// relative to whatever directory the process happened to start in, which
	// is only ever the SFML/ folder when launched through START.bat's /D
	// switch. Launched any other way (a shortcut, the debugger, by hand from
	// a different folder) it silently created a second, empty-looking
	// bot.log next to wherever the exe's caller stood, while every actual
	// line went to the first one - "session start" appearing to just vanish.
	// Deferring the open until the first real write means it always happens
	// after main() has already fixed the working directory.
	void EnsureOpen();

	mutable mutex mtx;
	string current;
	deque<string> lines;
	ofstream file;
	bool opened = false;
	static const size_t kCapacity = 500;
};

// Installed onto cout by main() before the Game object (and therefore
// anything it constructs) can print. Declared here, not owned by Game,
// because cout.rdbuf() has to be set up before Game's constructor runs.
extern LogSink GlobalLog;
