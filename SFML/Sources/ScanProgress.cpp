#include "../Headers/ScanProgress.h"

void ScanProgress::Begin(vector<ScanStep> plan) {
	lock_guard<mutex> lock(mtx);
	steps = std::move(plan);
	cursor = 0;
	running = true;
}

void ScanProgress::Finish() {
	lock_guard<mutex> lock(mtx);
	running = false;
}

void ScanProgress::Clear() {
	lock_guard<mutex> lock(mtx);
	steps.clear();
	cursor = 0;
	running = false;
}

string ScanProgress::MarkNextRunning() {
	lock_guard<mutex> lock(mtx);
	if (cursor >= steps.size()) {
		return "";
	}
	steps[cursor].state = StepState::Running;
	return steps[cursor].categoryLabel;
}

void ScanProgress::CompleteCurrent(bool ok) {
	lock_guard<mutex> lock(mtx);
	if (cursor >= steps.size()) {
		return;
	}
	steps[cursor].state = ok ? StepState::Ok : StepState::Failed;
	++cursor;
}

bool ScanProgress::IsRunning() const {
	lock_guard<mutex> lock(mtx);
	return running;
}

bool ScanProgress::HasPlan() const {
	lock_guard<mutex> lock(mtx);
	return !steps.empty();
}

float ScanProgress::Percent() const {
	lock_guard<mutex> lock(mtx);
	if (steps.empty()) {
		return 0.f;
	}

	// group by category, preserving first-seen order, without pulling in
	// <map> just for this. The inner scan is over the distinct categories, of
	// which there are ~30, so this stays cheap even for the biggest plan the
	// settings screen can build (every category, every tier, every
	// enchantment level - a few hundred steps). Nothing renders while a scan
	// is running anyway: the window is hidden and the scan owns the thread.
	vector<string> order;
	vector<int> total;
	vector<int> done;

	for (const auto& step : steps) {
		int idx = -1;
		for (size_t i = 0; i < order.size(); ++i) {
			if (order[i] == step.categoryKey) {
				idx = int(i);
				break;
			}
		}
		if (idx < 0) {
			order.push_back(step.categoryKey);
			total.push_back(0);
			done.push_back(0);
			idx = int(order.size()) - 1;
		}
		total[idx] += 1;
		if (step.state == StepState::Ok || step.state == StepState::Failed) {
			done[idx] += 1;
		}
	}

	if (order.empty()) {
		return 0.f;
	}

	float sum = 0.f;
	for (size_t i = 0; i < order.size(); ++i) {
		if (total[i] > 0) {
			sum += float(done[i]) / float(total[i]);
		}
	}
	return sum / float(order.size()) * 100.f;
}

string ScanProgress::CurrentLabel() const {
	lock_guard<mutex> lock(mtx);
	if (cursor < steps.size()) {
		return steps[cursor].categoryLabel;
	}
	return running ? "" : "Done";
}

void ScanProgress::Counts(int& ok, int& failed, int& pending) const {
	lock_guard<mutex> lock(mtx);
	ok = 0;
	failed = 0;
	pending = 0;
	for (const auto& step : steps) {
		switch (step.state) {
		case StepState::Ok: ++ok; break;
		case StepState::Failed: ++failed; break;
		default: ++pending; break;
		}
	}
}

vector<ScanStep> ScanProgress::Snapshot() const {
	lock_guard<mutex> lock(mtx);
	return steps;
}

vector<ScanStep> ScanProgress::FailedSteps() const {
	lock_guard<mutex> lock(mtx);
	vector<ScanStep> out;
	for (const auto& step : steps) {
		if (step.state == StepState::Failed) {
			out.push_back(step);
		}
	}
	return out;
}
