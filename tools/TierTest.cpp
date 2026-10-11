// Diagnostic for reading an Albion item icon.
//
// Two independent signals sit on an icon, and this tool reports both so the
// real classifier can be built from measured numbers instead of guesses:
//
//   1. the frame colour behind the item art, which encodes the tier
//      (T4 blue, T6 orange, T8 white, ...)
//   2. the roman numeral in the top-left badge (IV, VI, VII, VIII)
//
// Colour is the sturdier of the two: the badge is only a few pixels tall and
// tesseract happily confuses VII with VIII, Vll and MI. The numeral is still
// read as a cross-check.
//
// Icons arrive padded (transparent, or flattened to white/black), so the
// padding is trimmed first - otherwise every measurement just samples the
// padding instead of the frame.
//
// Usage: TierTest.exe <image> [<image> ...]

#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace std;
using namespace cv;


static string Squeeze(const string& s) {
	string out;
	for (unsigned char c : s) {
		if (!isspace(c)) {
			out += char(c);
		}
	}
	return out;
}

// Trims the uniform border around the icon. Uses alpha when the PNG has it,
// otherwise treats whatever colour fills the corners as padding.
static Mat TrimPadding(const Mat& src, string& how) {
	Mat mask;

	if (src.channels() == 4) {
		vector<Mat> ch;
		split(src, ch);
		mask = ch[3] > 128;
		how = "alpha";
	}
	else {
		Mat bgr;
		if (src.channels() == 3) {
			bgr = src;
		}
		else {
			cvtColor(src, bgr, COLOR_GRAY2BGR);
		}

		// the four corners agree on the padding colour
		Vec3b pad = bgr.at<Vec3b>(2, 2);
		Mat diff;
		absdiff(bgr, Scalar(pad[0], pad[1], pad[2]), diff);

		vector<Mat> d;
		split(diff, d);
		Mat total = d[0] + d[1] + d[2];
		mask = total > 28;

		char buf[64];
		snprintf(buf, sizeof(buf), "corner colour (%d,%d,%d)",
			pad[0], pad[1], pad[2]);
		how = buf;
	}

	vector<vector<Point>> contours;
	Mat work = mask.clone();
	findContours(work, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

	Rect best(0, 0, src.cols, src.rows);
	double bestArea = 0;
	for (const auto& c : contours) {
		Rect r = boundingRect(c);
		double a = double(r.width) * r.height;
		if (a > bestArea) {
			bestArea = a;
			best = r;
		}
	}

	Mat bgrSrc;
	if (src.channels() == 4) {
		cvtColor(src, bgrSrc, COLOR_BGRA2BGR);
	}
	else if (src.channels() == 1) {
		cvtColor(src, bgrSrc, COLOR_GRAY2BGR);
	}
	else {
		bgrSrc = src;
	}

	return bgrSrc(best).clone();
}

// Everything inside the icon's grey outer border. Sampling only a thin edge
// ring does not work: the frame colour lives *inside* that border, so a ring
// hugging the edge just measures the border itself. The item art is mostly
// desaturated metal, so taking the whole interior and looking at the
// saturated pixels still lets the frame colour dominate.
//
// Cut out the top-left tier badge, the bottom-left enchant pips and the
// bottom-right quality dot, none of which are frame.
static Mat FrameMask(const Size& size) {
	Mat mask = Mat::zeros(size, CV_8UC1);

	int mx = max(1, size.width * 14 / 100);
	int my = max(1, size.height * 14 / 100);
	rectangle(mask, Rect(mx, my, size.width - 2 * mx, size.height - 2 * my),
		Scalar(255), FILLED);

	rectangle(mask, Rect(0, 0, size.width * 34 / 100, size.height * 28 / 100),
		Scalar(0), FILLED);
	rectangle(mask, Rect(0, size.height * 74 / 100,
		size.width * 42 / 100, size.height * 26 / 100), Scalar(0), FILLED);
	rectangle(mask, Rect(size.width * 62 / 100, size.height * 62 / 100,
		size.width * 38 / 100, size.height * 38 / 100), Scalar(0), FILLED);

	return mask;
}

// Modal hue among the reasonably saturated ring pixels, plus how much of the
// ring is washed out. A white T8 frame has almost no saturated pixels at all,
// which is itself the signal.
static void ReportFrame(const Mat& bgr) {
	Mat mask = FrameMask(bgr.size());
	Mat hsv;
	cvtColor(bgr, hsv, COLOR_BGR2HSV);

	int hist[180] = { 0 };
	long total = 0;
	long saturated = 0;
	long bright = 0;
	double sumS = 0;
	double sumV = 0;

	for (int y = 0; y < bgr.rows; ++y) {
		for (int x = 0; x < bgr.cols; ++x) {
			if (mask.at<uchar>(y, x) == 0) {
				continue;
			}
			Vec3b p = hsv.at<Vec3b>(y, x);
			++total;
			sumS += p[1];
			sumV += p[2];
			if (p[2] > 60) {
				++bright;
			}
			if (p[1] > 60 && p[2] > 60) {
				++saturated;
				hist[p[0]]++;
			}
		}
	}

	if (total == 0) {
		cout << "    frame: no pixels sampled\n";
		return;
	}

	int bestHue = -1;
	int bestCount = 0;
	for (int h = 0; h < 180; ++h) {
		// smooth over a small window, hues are noisy
		int c = 0;
		for (int d = -3; d <= 3; ++d) {
			int idx = (h + d + 180) % 180;
			c += hist[idx];
		}
		if (c > bestCount) {
			bestCount = c;
			bestHue = h;
		}
	}

	// spelled out rather than using max(): with "using namespace cv" in scope
	// the cv::max overloads hijack plain integer comparisons
	double satPct = 100.0 * double(saturated) / double(total);

	printf("    ring pixels %ld   saturated %.0f%%   mean S %.0f   mean V %.0f\n",
		total, satPct, sumS / double(total), sumV / double(total));

	if (satPct < 12.0) {
		printf("    dominant hue  -- (washed out, looks like a white/grey frame)\n");
	}
	else {
		double share = 100.0 * double(bestCount) / double(saturated);
		printf("    dominant hue  %d  (%.0f%% of saturated pixels within +-3)\n",
			bestHue, share);
	}

	// Small patches hugging the inside of the border, where neither the item
	// art nor an enchantment glow usually reaches. On an enchanted item the
	// glow swamps the whole-interior average, so these corners are the more
	// honest read of the frame itself.
	struct Spot { const char* name; int xp; int yp; };
	const Spot spots[] = {
		{ "top-right  ", 82, 12 },
		{ "right-mid  ", 88, 45 },
		{ "bottom-mid ", 50, 88 },
		{ "left-mid   ", 12, 50 },
	};

	for (const auto& s : spots) {
		int half = max(2, min(bgr.cols, bgr.rows) * 5 / 100);
		int cx = bgr.cols * s.xp / 100;
		int cy = bgr.rows * s.yp / 100;

		Rect r(cx - half, cy - half, half * 2, half * 2);
		r &= Rect(0, 0, bgr.cols, bgr.rows);
		if (r.width <= 0 || r.height <= 0) {
			continue;
		}

		Mat patch = hsv(r);
		Scalar mean = cv::mean(patch);
		printf("      %s  H=%3.0f S=%3.0f V=%3.0f\n",
			s.name, mean[0], mean[1], mean[2]);
	}
}

static void ReadBadge(tesseract::TessBaseAPI& api, const Mat& bgr,
	const string& path) {
	// The badge sits a little in from the icon corner. Too tight a crop clips
	// the V, too loose drags in frame colour and ruins the Otsu split.
	Rect roi(bgr.cols * 4 / 100, bgr.rows * 4 / 100,
		max(1, bgr.cols * 28 / 100), max(1, bgr.rows * 28 / 100));
	roi &= Rect(0, 0, bgr.cols, bgr.rows);
	Mat badge = bgr(roi);

	Mat gray;
	Mat big;
	cvtColor(badge, gray, COLOR_BGR2GRAY);
	resize(gray, big, Size(), 8.0, 8.0, INTER_CUBIC);

	const int modes[2] = { THRESH_BINARY, THRESH_BINARY_INV };
	const char* polarity[2] = { "dark-on-light", "light-on-dark" };

	// Tesseract's layout analysis wants quiet space around the glyphs and a
	// hint about what shape of text to expect. Empty results from a bitmap
	// where the numeral is plainly legible almost always mean one of these
	// two is missing, so try several page-segmentation modes.
	struct Mode { tesseract::PageSegMode psm; const char* name; };
	const Mode psms[] = {
		{ tesseract::PSM_SINGLE_LINE,  "line" },
		{ tesseract::PSM_SINGLE_WORD,  "word" },
		{ tesseract::PSM_RAW_LINE,     "raw"  },
		{ tesseract::PSM_SINGLE_BLOCK, "block"},
	};

	for (int i = 0; i < 2; ++i) {
		Mat bin;
		threshold(big, bin, 0, 255, modes[i] | THRESH_OTSU);

		// white quiet zone; tesseract discards glyphs touching the edge
		Mat padded;
		copyMakeBorder(bin, padded, 40, 40, 40, 40, BORDER_CONSTANT,
			Scalar(255));

		printf("    badge %-14s", polarity[i]);
		for (const auto& m : psms) {
			api.SetPageSegMode(m.psm);
			api.SetImage(padded.data, padded.cols, padded.rows, 1,
				int(padded.step));

			char* text = api.GetUTF8Text();
			string got = text != NULL ? Squeeze(text) : "";
			delete[] text;

			printf("  %s=\"%s\"", m.name, got.c_str());
		}
		printf("\n");

		// leave the exact bitmap tesseract saw on disk, so a bad read can be
		// blamed on the crop rather than guessed at
		imwrite(path + ".badge" + to_string(i) + ".png", padded);
	}
}


int main(int argc, char** argv) {
	if (argc < 2) {
		cout << "Usage: TierTest.exe <image> [<image> ...]\n";
		return 1;
	}

	tesseract::TessBaseAPI api;
	const char* tessdata = "C:\\Program Files\\Tesseract-OCR\\tessdata";

	if (api.Init(tessdata, "eng", tesseract::OEM_LSTM_ONLY) != 0) {
		cout << "Tesseract error: cannot load 'eng' from " << tessdata << '\n';
		return 1;
	}
	api.SetPageSegMode(tesseract::PSM_SINGLE_LINE);

	// Whitelisting is deliberately off by default. The LSTM engine in
	// tesseract 4/5 handles tessedit_char_whitelist poorly and can return
	// nothing at all rather than a best guess, which looks identical to a
	// broken setup. Pass --whitelist to see the difference for yourself.
	bool useWhitelist = false;
	for (int i = 1; i < argc; ++i) {
		if (string(argv[i]) == "--whitelist") {
			useWhitelist = true;
		}
	}
	if (useWhitelist) {
		api.SetVariable("tessedit_char_whitelist", "IVX");
		cout << "(char whitelist IVX enabled)\n";
	}

	// Sanity check that tesseract is wired up at all: if this cannot read
	// text OpenCV itself drew, nothing downstream is worth debugging.
	{
		Mat probe(160, 520, CV_8UC1, Scalar(255));
		putText(probe, "VIII IV VI", Point(20, 110),
			FONT_HERSHEY_SIMPLEX, 3.0, Scalar(0), 8);

		api.SetPageSegMode(tesseract::PSM_SINGLE_LINE);
		api.SetImage(probe.data, probe.cols, probe.rows, 1, int(probe.step));
		char* text = api.GetUTF8Text();
		string got = text != NULL ? string(text) : "";
		delete[] text;

		while (!got.empty() && isspace((unsigned char)got.back())) {
			got.pop_back();
		}
		cout << "selftest (expects \"VIII IV VI\"): \"" << got << "\"\n";
	}

	for (int i = 1; i < argc; ++i) {
		if (string(argv[i]) == "--whitelist") {
			continue;
		}
		string path = argv[i];
		Mat raw = imread(path, IMREAD_UNCHANGED);

		cout << "\n=== " << path << " ===\n";
		if (raw.empty()) {
			cout << "    could not read this file\n";
			continue;
		}

		string how;
		Mat icon = TrimPadding(raw, how);

		printf("    raw %dx%d ch=%d   trimmed to %dx%d via %s\n",
			raw.cols, raw.rows, raw.channels(),
			icon.cols, icon.rows, how.c_str());

		ReportFrame(icon);
		ReadBadge(api, icon, path);

		// leave the trimmed icon on disk so the crop can be eyeballed
		imwrite(path + ".trim.png", icon);
	}

	api.End();
	return 0;
}
