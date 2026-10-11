#pragma once
#include <Windows.h>
#include <string>

#include <opencv2/core.hpp>

#include "Config.h"

#pragma warning(disable: 4996)

using namespace std;

// A rectangle on screen. The constructor takes coordinates as measured on a
// 1920x1080 screen and scales them to the resolution from Config.txt, so
// every call site can keep using the reference coordinates.
struct ScreenShot {
    int x;
    int y;
    int width;
    int height;

    // base_* are coordinates on a 1920x1080 screen
    ScreenShot(int base_x, int base_y, int base_w, int base_h);
};


// Grabs screen regions straight into memory. The device contexts and the
// bitmap are created once and reused, and the returned Mat is a view on the
// bitmap's own pixels, so a grab costs one BitBlt and no allocation.
class ScreenCapturer {
public:
    ScreenCapturer();
    ~ScreenCapturer();

    ScreenCapturer(const ScreenCapturer&) = delete;
    ScreenCapturer& operator= (const ScreenCapturer&) = delete;

    // Returns a BGRA view of the region, or an empty Mat on failure. The
    // pixels are overwritten by the next Grab, so clone what you keep.
    const cv::Mat& Grab(const ScreenShot& region);

private:
    bool Resize(int w, int h);
    void ReleaseBitmap();

    HDC screen_dc = NULL;
    HDC mem_dc = NULL;
    HBITMAP dib = NULL;
    HGDIOBJ old_bitmap = NULL;
    void* pixels = NULL;
    int width = 0;
    int height = 0;

    // wraps `pixels`, does not own them
    cv::Mat frame;
};
