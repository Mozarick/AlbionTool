// Checks that OcrEngine::Menu() finds menu labels and reports where they are,
// which is what MarketScanner's navigation depends on. Draws a fake dropdown
// (light text on a dark panel, like the game) and asks for each label back.

#include "../SFML/Headers/Ocr.h"

#include <opencv2/imgproc.hpp>
#include <cctype>
#include <iostream>
#include <vector>

using namespace std;
using namespace cv;


static string Squash(const string& s) {
	string out;
	for (unsigned char c : s) {
		if (isalnum(c)) {
			out += (char)tolower(c);
		}
	}
	return out;
}

int main() {
	const vector<string> labels = {
		"Weapons", "Bow", "Chest Armor", "Leather Jackets",
		"Satchels of Insight", "Tier 4", "Tier 8", "Nature Staff"
	};

	// dark panel, light text: the polarity the engine expects to invert
	Mat panel(40 + int(labels.size()) * 34, 340, CV_8UC3, Scalar(30, 28, 26));
	vector<Point> where;

	for (size_t i = 0; i < labels.size(); ++i) {
		Point origin(18, 34 + int(i) * 34);
		putText(panel, labels[i], origin, FONT_HERSHEY_SIMPLEX, 0.6,
			Scalar(226, 226, 226), 1, LINE_AA);
		where.push_back(origin);
	}

	Mat bgra;
	cvtColor(panel, bgra, COLOR_BGR2BGRA);

	vector<OcrWord> words = OcrEngine::Menu().ReadWords(bgra);
	printf("read %zu words\n\n", words.size());

	int passed = 0;
	for (size_t i = 0; i < labels.size(); ++i) {
		const string target = Squash(labels[i]);

		bool found = false;
		int bx = 0;
		int by = 0;

		// same consecutive-word join MarketScanner uses
		for (size_t s = 0; s < words.size() && !found; ++s) {
			string joined;
			int left = words[s].x;
			int top = words[s].y;

			for (size_t e = s; e < words.size() && e < s + 5; ++e) {
				if (e > s) {
					int dy = words[e].y - words[s].y;
					if (dy < -words[s].h || dy > words[s].h) {
						break;
					}
				}
				joined += Squash(words[e].text);
				if (joined == target) {
					found = true;
					bx = left;
					by = top;
					break;
				}
				if (joined.size() > target.size()) {
					break;
				}
			}
		}

		if (found) {
			// the box should sit near where the text was drawn
			int dy = by - (where[i].y - 16);
			printf("  OK    %-22s at y=%-4d (drawn y=%-4d, off by %d)\n",
				labels[i].c_str(), by, where[i].y - 16, dy);
			++passed;
		}
		else {
			printf("  MISS  %s\n", labels[i].c_str());
		}
	}

	printf("\n%d/%zu labels located\n", passed, labels.size());
	return passed == (int)labels.size() ? 0 : 1;
}
