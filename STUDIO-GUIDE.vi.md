# Market Studio 0.6.0

Mở `MarketStudio.exe` trong thư mục bản phát hành, hoặc `x64/Release/SFML.exe`
trong checkout. Home cho biết số lệnh Black Market, offer thành phố và tổng
profit của những lần đã copy. “Receiver ready” chỉ xác nhận bộ nhận đang
chạy; “Waiting for market data” nghĩa là chưa có dữ liệu thị trường tới.

## Một lượt mua tay

1. Chọn **Select all** nếu muốn quét tất cả category, rồi scan Black Market
   và scan market thành phố. Giữ `Ctrl+Shift+Q` để dừng
   lượt scan. Các phần đã scan vẫn được giữ trong phiên chạy.
2. Chọn **Plan** để đặt ngưỡng và ngân sách, hoặc **Find deals** để xem deal
   theo cấu hình hiện tại. Mở **Manual shopping** để mua tay.
3. Click một món để copy tên tìm kiếm rồi paste vào game. Mỗi lần copy cộng
   profit hiển thị của món đó vào **Profit estimated**, kể cả copy trùng món.
4. **Undo** hoặc `Ctrl+Z` bỏ lần copy cuối khỏi tổng. Clipboard giữ nguyên.
   **Reset** xóa tổng và dấu đã copy, giữ lại danh sách.
5. Ô tìm kiếm (`Ctrl+F`) lọc tên món; **Pending** chỉ hiện món chưa được copy.
   `Enter` copy món đầu đang nhìn thấy; `PgUp/PgDn` cuộn một trang.
6. **Export CSV** xuất những món đang hiển thị ra `shopping_list.csv`.

Danh sách và lịch sử copy tự lưu trên máy. Tắt rồi mở lại để **Resume
shopping**. Plan mới thay thế phiên mua cũ. **Clear Everything** xóa cả phiên
mua đã lưu, dữ liệu scan trong bộ nhớ và ledger hàng đã mua.

Profit là ước tính từ dữ liệu tạo plan, không phải xác nhận đã mua/bán trong
game. Tuổi của plan hiện ở góc trên; plan cũ hơn 15 phút nhắc scan lại.
Lưu phiên mua không giữ giá thị trường trong bộ nhớ qua lần khởi động.

## Dữ liệu và kết nối

Trong bản đóng gói, file dữ liệu nằm ở `data/`: `shopping_session.json`,
`shopping_progress.json`, `shopping_list.csv`, `holdings.tsv`, `bot.log` và
`scan_profile.tsv`. Không đóng gói giá đã capture hoặc ledger cá nhân.
Mở tool lần nữa sẽ đưa cửa sổ đang chạy lên trước, thay vì tạo một bản thứ
hai tranh port 5000. Các lượt preview chẩn đoán vẫn cần port trống.

Client capture đi kèm nằm ở `capture/AlbionFeed/`, dùng .NET 10 và Npcap đã
cài trên máy này. Client cần quyền Administrator; tool chỉ yêu cầu quyền đó
khi khởi động hoặc reconnect capture. Chưa có data thì mở market trong game;
nếu vẫn không nhận, chọn **Reconnect capture**. Log capture nằm cạnh client.
Launcher game tiếp tục dùng cài đặt Albion hiện có tại `D:/albion/launcher`.

File phiên mua bị hỏng sẽ báo lỗi rõ ràng. Nếu chỉ lịch sử copy bị hỏng,
tool vẫn khôi phục danh sách và bắt đầu tổng mới. File được thay thế nguyên
tử để không ghi đè phiên cũ bằng một lần ghi dang dở.

## Build và kiểm tra

`pwsh -NoProfile -File tools/Build-Tool.ps1` build Release x64 và chạy bốn bộ
kiểm tra: planner, nhóm danh sách mua, tải lớn và hành vi sản phẩm. Kiểm tra
sản phẩm dùng HTTP loopback thật, clipboard thật và render giao diện thật;
có kiểm tra Undo copy trùng, restart, file lỗi, lọc/export, gói mạng sai,
ngắt kết nối dở dang và restart bộ nhận. Ảnh test dùng dữ liệu giả lập.

`pwsh -NoProfile -File tools/Package-Studio.ps1` tạo bản phát hành kèm DLL,
fonts, client capture và manifest SHA-256. Đóng gói lại giữ nguyên các file
phiên mua của bản phát hành đã có.

`pwsh -NoProfile -File tools/Smoke-Studio.ps1` chạy bản đóng gói từ thư mục
khác, với PATH chỉ có Windows, gửi HTTP thử vào chính process đó, kiểm tra
ảnh Home, mã thoát và SHA-256 của các file trong manifest.
