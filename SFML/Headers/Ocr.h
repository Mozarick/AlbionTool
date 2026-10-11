#pragma once
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <tesseract/baseapi.h>

using namespace std;


// A word tesseract found, with where it sits in the image it was read from
// (region-local pixels, already scaled back down from the upscaled bitmap).
struct OcrWord {
	string text;
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
	int confidence = 0;
};


// Reads text off the screen.
//
// Two tuned instances rather than one: prices are a single line of digits,
// menu labels are several lines of words, and the page-segmentation mode that
// suits one is wrong for the other. Both are created once and reused - the bot
// used to shell out to tesseract.exe per lookup, which reloaded the language
// model every single time.
//
// Note on whitelists: tesseract 4/5's LSTM engine handles
// tessedit_char_whitelist badly and returns an EMPTY string rather than a best
// guess. An earlier version of this file whitelisted digits and silently read
// nothing at all. Filtering now happens after recognition instead.
class OcrEngine {
public:
	// single line, for prices and counters
	static OcrEngine& Digits();
	// block of menu entries, for dropdown navigation
	static OcrEngine& Menu();

	OcrEngine(const OcrEngine&) = delete;
	OcrEngine& operator= (const OcrEngine&) = delete;

	bool IsReady() const;

	// Whole recognised text of a BGRA image.
	string Read(const cv::Mat& bgra);

	// Individual words with their boxes, in the coordinates of `bgra`.
	vector<OcrWord> ReadWords(const cv::Mat& bgra);

private:
	explicit OcrEngine(int pageSegMode, double upscale, bool invert);
	~OcrEngine();

	bool Prepare(const cv::Mat& bgra);

	tesseract::TessBaseAPI api;
	bool ready = false;
	double upscale;
	bool invert;

	// scratch buffers, reused across calls
	cv::Mat gray;
	cv::Mat scaled;
	cv::Mat binary;
};
