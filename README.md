# AlbionTool — Market Studio 0.6.0

Tool Windows cho Albion Online: scan Black Market và market thành phố, lập danh sách deal theo ngân sách/lợi nhuận, hỗ trợ mua tay bằng cách copy tên món, và lưu phiên mua để tiếp tục sau khi khởi động lại.

- **Select all** để chọn toàn bộ category trước khi scan.
- **Manual shopping** cộng profit ước tính của mỗi lần copy, kể cả copy trùng; có Undo, Reset, tìm kiếm và Export CSV.
- **Ctrl+Shift+Q** dừng lượt scan tại điểm an toàn.
- Dữ liệu market từ client **AlbionFeed** được gửi tới bộ nhận tại `127.0.0.1:5000`.

Xem [hướng dẫn sử dụng](STUDIO-GUIDE.vi.md), [hướng dẫn build C++](BUILD.md), và [tài liệu kỹ thuật](HANDOVER.md).

## Lấy mã nguồn

```powershell
git clone --recurse-submodules https://github.com/Mozarick/AlbionTool.git
cd AlbionTool
```

Repo công khai: ai có link đều có thể xem và clone. Nếu đã clone mà thiếu dependency:

```powershell
git submodule update --init --recursive
```

## Build

C++ app cần Visual Studio C++ Build Tools, OpenCV 4.5.5, Tesseract development libraries qua vcpkg và SFML 2.5.1 đi kèm trong `lib/`. Đặt `OPENCV_DIR` và `VCPKG_ROOT` theo [BUILD.md](BUILD.md), rồi build `SFML.sln` với **Release / x64**. File chạy là `x64/Release/SFML.exe`, working directory là `SFML/`. OCR cần `eng.traineddata` theo `SFML/Config.txt`.

Client capture cần .NET SDK 10 và Npcap:

```powershell
dotnet publish AlbionFeed/AlbionFeed.csproj -c Release -o bin/AlbionFeed
```

`AlbionDataAvalonia` là dependency submodule được ghim phiên bản; AlbionFeed chỉ build các project giải mã packet, không build ứng dụng Avalonia.

Các script `tools/Build-Tool.ps1`, `tools/Package-Studio.ps1`, `START.bat` và launcher capture hiện dùng cấu hình máy phát triển `D:/AlbionTool`, `.build-deps` và `D:/vcpkg`. Trên máy khác cần điều chỉnh các đường dẫn trước khi dùng script. Khi chạy từ checkout, capture launcher mặc định tìm `D:/AlbionTool/bin/run-feed.bat`; bản đóng gói tìm `capture/run-feed.bat` tương đối với thư mục chạy.

## Kiểm tra và đóng gói trên máy phát triển

```powershell
pwsh -NoProfile -File tools/Build-Tool.ps1
pwsh -NoProfile -File tools/Package-Studio.ps1
pwsh -NoProfile -File tools/Smoke-Studio.ps1
```

Script build chạy planner, shopping, tải lớn và kiểm tra hành vi sản phẩm. Script đóng gói tạo `dist/MarketStudio-0.6.0`; smoke test kiểm tra app đóng gói, HTTP loopback, giao diện và manifest SHA-256. Chưa kèm file phát hành tải sẵn trên GitHub.

## Dữ liệu cá nhân

Log, giá market đã capture, holdings, lịch sử copy, phiên mua, cấu hình scan cá nhân, build output và snapshot rollback được loại khỏi commit. Tool tự tạo dữ liệu runtime khi sử dụng. Profit hiển thị là ước tính theo dữ liệu plan, không xác nhận giao dịch trong game.

## Nguồn gốc

Mã C++ phát triển từ [Merab16/OpenCV_and_tesseract](https://github.com/Merab16/OpenCV_and_tesseract); lịch sử Git được giữ lại. [README gốc](docs/UPSTREAM-README.md) được lưu để tham khảo. Packet decoding dùng [JPCodeCraft/AlbionDataAvalonia](https://github.com/JPCodeCraft/AlbionDataAvalonia). Các thư viện/font giữ giấy phép đi kèm.
