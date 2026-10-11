#pragma once
#include <mutex>
#include <string>
#include <vector>

using namespace std;

enum class StepState {
	Pending,
	Running,
	Ok,
	Failed,
};

struct ScanStep {
	string categoryKey;
	string categoryLabel; // menuSub, for display
	int tier = 0;          // 0 = one pass, no tier filter
	int enchant = -1;      // -1 = no enchantment filter, else 0..4
	StepState state = StepState::Pending;
};

// Shared between the background scan thread and the render thread. The scan
// thread only ever advances a cursor through the plan it was given, in
// order, so no per-step lookup is needed - "the next pending step" is always
// unambiguous as long as the caller's loop order matches the plan's order.
//
// Percent is deliberately approximate, not a count of pages or orders: each
// category is an equal 1/N slice regardless of how many steps it has, and
// each of its steps is an equal slice of that. Five categories, one scanned
// at three steps, gives that category's steps 1/5 / 3 = ~6.7% each - close
// enough to glance at, not meant to be exact. A step is one (tier,
// enchantment) pair, so splitting a category by enchantment makes its own
// steps smaller without changing the category's share of the whole.
class ScanProgress {
public:
	void Begin(vector<ScanStep> plan);
	void Finish();

	// Throws the plan away entirely, so HasPlan() goes false and the Scan
	// screen's category boxes go back to grey. Finish() only stops the run -
	// it deliberately leaves the results behind for the retry-failed button to
	// read, which is why forgetting the last scan needs its own call.
	void Clear();

	// Advances the cursor: marks the next pending step Running (and returns
	// its label), or completes the current Running step with a result.
	string MarkNextRunning();
	void CompleteCurrent(bool ok);

	bool IsRunning() const;
	bool HasPlan() const;
	float Percent() const;
	string CurrentLabel() const;

	// counts for the summary line
	void Counts(int& ok, int& failed, int& pending) const;

	vector<ScanStep> Snapshot() const;
	// Failed steps, one entry per (category, tier), in plan order - what a
	// "retry failed" run should attempt again.
	vector<ScanStep> FailedSteps() const;

private:
	mutable mutex mtx;
	vector<ScanStep> steps;
	size_t cursor = 0;
	bool running = false;
};
