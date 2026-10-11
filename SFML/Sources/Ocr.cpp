#include "../Headers/Ocr.h"
#include "../Headers/Config.h"

#include <opencv2/imgproc.hpp>


// Market text is small (price labels are ~18px tall, menu rows a bit more),
// well under the ~30px of glyph height tesseract wants, so both engines
// upscale before recognising. Menu text is larger to begin with and there is
// much more of it, so it needs less magnification.
static const double DIGIT_UPSCALE = 3.0;
static const double MENU_UPSCALE = 2.0;


OcrEngine& OcrEngine::Digits() {
	static OcrEngine instance(tesseract::PSM_SINGLE_LINE, DIGIT_UPSCALE, true);
	return instance;
}

OcrEngine& OcrEngine::Menu() {
	static OcrEngine instance(tesseract::PSM_SPARSE_TEXT, MENU_UPSCALE, true);
	return instance;
}

OcrEngine::OcrEngine(int pageSegMode, double upscaleFactor, bool invertText)
	: upscale(upscaleFactor)
	, invert(invertText)
{
	// an empty path makes tesseract fall back to TESSDATA_PREFIX
	const char* tessdata = Config.TessdataPath.empty()
		? NULL
		: Config.TessdataPath.c_str();

	if (api.Init(tessdata, "eng", tesseract::OEM_LSTM_ONLY) != 0) {
		cout << "Tesseract error: cannot load 'eng' from "
			<< (tessdata != NULL ? tessdata : "TESSDATA_PREFIX") << '\n'
			<< "Set tessdata_path in Config.txt\n";
		return;
	}

	api.SetPageSegMode((tesseract::PageSegMode)pageSegMode);
	ready = true;
}

OcrEngine::~OcrEngine() {
	if (ready) {
		api.End();
	}
}

bool OcrEngine::IsReady() const {
	return ready;
}

bool OcrEngine::Prepare(const cv::Mat& bgra) {
	if (!ready || bgra.empty()) {
		return false;
	}

	if (bgra.channels() == 4) {
		cv::cvtColor(bgra, gray, cv::COLOR_BGRA2GRAY);
	}
	else if (bgra.channels() == 3) {
		cv::cvtColor(bgra, gray, cv::COLOR_BGR2GRAY);
	}
	else {
		gray = bgra;
	}

	cv::resize(gray, scaled, cv::Size(), upscale, upscale, cv::INTER_CUBIC);

	// the UI draws light text on a dark panel, tesseract expects the opposite
	int mode = invert ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY;
	cv::threshold(scaled, binary, 0, 255, mode | cv::THRESH_OTSU);

	api.SetImage(binary.data, binary.cols, binary.rows, 1,
		static_cast<int>(binary.step));
	return true;
}

string OcrEngine::Read(const cv::Mat& bgra) {
	if (!Prepare(bgra)) {
		return "";
	}

	char* text = api.GetUTF8Text();
	if (text == NULL) {
		return "";
	}
	string result(text);
	delete[] text;
	return result;
}

vector<OcrWord> OcrEngine::ReadWords(const cv::Mat& bgra) {
	vector<OcrWord> words;
	if (!Prepare(bgra)) {
		return words;
	}

	if (api.Recognize(NULL) != 0) {
		return words;
	}

	tesseract::ResultIterator* it = api.GetIterator();
	if (it == NULL) {
		return words;
	}

	const tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
	do {
		char* text = it->GetUTF8Text(level);
		if (text == NULL) {
			continue;
		}

		string word(text);
		delete[] text;
		if (word.empty()) {
			continue;
		}

		int left = 0;
		int top = 0;
		int right = 0;
		int bottom = 0;
		it->BoundingBox(level, &left, &top, &right, &bottom);

		OcrWord entry;
		entry.text = std::move(word);
		// back out of the upscale so the caller gets region pixels
		entry.x = int(left / upscale);
		entry.y = int(top / upscale);
		entry.w = int((right - left) / upscale);
		entry.h = int((bottom - top) / upscale);
		entry.confidence = int(it->Confidence(level));

		words.push_back(std::move(entry));
	} while (it->Next(level));

	delete it;
	return words;
}
