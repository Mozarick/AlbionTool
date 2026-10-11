#include "../Headers/ScreenCapture.h"


ScreenShot::ScreenShot(int base_x, int base_y, int base_w, int base_h)
	: x(int(Config.ScreenScale.first * base_x))
	, y(int(Config.ScreenScale.second * base_y))
	, width(int(Config.ScreenScale.first * base_w))
	, height(int(Config.ScreenScale.second * base_h))
{

}


ScreenCapturer::ScreenCapturer() {
	screen_dc = GetDC(NULL);
	if (screen_dc != NULL) {
		mem_dc = CreateCompatibleDC(screen_dc);
	}

	if (screen_dc == NULL || mem_dc == NULL) {
		cout << "ScreenCapturer: cannot create a device context\n";
	}
}

ScreenCapturer::~ScreenCapturer() {
	ReleaseBitmap();

	if (mem_dc != NULL) {
		DeleteDC(mem_dc);
		mem_dc = NULL;
	}
	if (screen_dc != NULL) {
		ReleaseDC(NULL, screen_dc);
		screen_dc = NULL;
	}
}

void ScreenCapturer::ReleaseBitmap() {
	// drop the Mat first, it points into the bitmap we are about to free
	frame = cv::Mat();

	if (mem_dc != NULL && old_bitmap != NULL) {
		SelectObject(mem_dc, old_bitmap);
		old_bitmap = NULL;
	}
	if (dib != NULL) {
		DeleteObject(dib);
		dib = NULL;
	}

	pixels = NULL;
	width = 0;
	height = 0;
}

bool ScreenCapturer::Resize(int w, int h) {
	ReleaseBitmap();

	BITMAPINFO bmi;
	ZeroMemory(&bmi, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	// negative height asks for top-down rows, the order cv::Mat expects
	bmi.bmiHeader.biHeight = -h;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	dib = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &pixels, NULL, 0);
	if (dib == NULL || pixels == NULL) {
		ReleaseBitmap();
		return false;
	}

	old_bitmap = SelectObject(mem_dc, dib);
	width = w;
	height = h;

	// 32 bpp BI_RGB is BGRA in memory; the Mat is only a view on the bitmap
	frame = cv::Mat(h, w, CV_8UC4, pixels);
	return true;
}

const cv::Mat& ScreenCapturer::Grab(const ScreenShot& region) {
	static const cv::Mat empty;

	if (screen_dc == NULL || mem_dc == NULL ||
		region.width <= 0 || region.height <= 0) {
		return empty;
	}

	if (region.width != width || region.height != height) {
		if (!Resize(region.width, region.height)) {
			return empty;
		}
	}

	if (!BitBlt(mem_dc, 0, 0, width, height,
		screen_dc, region.x, region.y, SRCCOPY)) {
		return empty;
	}

	// GDI batches drawing calls; without this the bitmap may still be stale
	// when we read the pixels straight out of it
	GdiFlush();

	return frame;
}
