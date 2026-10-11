# Bot thị trường Albion — tài liệu bàn giao (bản tiếng Việt)

**Đối tượng đọc: một AI agent tiếp nhận project này từ đầu, chưa biết gì.** Tài
liệu được tối ưu để tra cứu và để không làm hỏng lại những thứ đã sửa, không phải
để đọc từ đầu đến cuối. Phần văn xuôi chỉ được giữ lại ở nơi mà *lý do* chính là
thứ ngăn một lỗi cũ lặp lại.

Repo gốc: `github.com/Merab16/OpenCV_and_tesseract`. C++17 / MSVC / SFML GUI.
Chỉ có working tree — **không có gì được commit, không có undo.** Đọc kỹ trước
khi xoá hoặc ghi đè.

Đường dẫn trong tài liệu này là tương đối so với `D:\AlbionTool\OpenCV_and_tesseract\`.

> Đây là bản dịch tiếng Việt của `HANDOVER.md`. Tên hàm, tên biến, tên file, đoạn
> code, tên cột trong bảng cấu hình được **giữ nguyên tiếng Anh** vì đó là các
> định danh thật trong mã nguồn — dịch chúng sẽ làm tài liệu sai lệch so với code.

---

## 0. Những nguyên tắc bất biến (không được phá vỡ)

Phá vỡ một trong số này thì lỗi sẽ **im lặng** hoặc **bị đổ oan cho nguyên nhân
khác**. Mỗi dòng ở đây tồn tại vì nó **đã từng xảy ra thật**; xem chi tiết sự cố ở
mục §9.

| # | Nguyên tắc | Hậu quả nếu phá vỡ |
|---|---|---|
| I1 | **Không dùng `printf`/`puts`/`fprintf`.** Chỉ `std::cout` mới hiển thị được. | Binary GUI không có console; stdout kiểu C bị bỏ qua hoàn toàn. Output biến mất mà không báo lỗi. §9.1 |
| I2 | **Không dùng `system()`.** | Mỗi lần gọi sẽ mở nhấp nháy một `cmd.exe`. §9.2 |
| I3 | **Cửa sổ của bot không bao giờ được đè lên vùng toạ độ click/đọc của game.** Dùng khoá `WindowHidden` bao quanh mọi lượt chạy tự động. | Click rơi vào chính bot; hash vùng ảnh đọc lại pixel của bot. Báo "thành công" nhưng thực chất không làm gì. §9.3 |
| I4 | **Không dùng thread trong đường xử lý input.** Scan/mua/bán chạy đồng bộ trên event thread. | Một render loop 60fps chạy cạnh `SendInput` theo nhịp `Sleep()` sẽ làm lệch thời gian. §9.4 |
| I5 | **Không dùng OCR ở bất kỳ đâu.** Mọi dòng menu là một toạ độ đã đo sẵn. `Ocr.h/.cpp` là code chết. | OCR từng đọc "Tier 5" thành "Tier S"; cả tier bị bỏ qua trong im lặng. §9.10 |
| I6 | **Grep tìm nơi gọi theo *symbol* trước khi xoá bất cứ gì.** | `ClearItemName()` từng bị xếp nhầm là code chết; thực ra nó nằm trên đường mua hàng đang hoạt động. §9.6 |
| I7 | **`ClickInside` không được cộng thêm `MoveTargetSlopPx` lên trên điểm ngẫu nhiên đã có.** | Nút tier chỉ cao 7px; cộng thêm 3px sai số sẽ trượt ra ngoài và dropdown không bao giờ mở. |
| I8 | **`menu_sub` trong `categories.tsv` phải là duy nhất trên toàn cục.** | `Game.cpp` so khớp click nút theo chuỗi đó. Trùng lặp làm hai nút tự bật/tắt nhau. Được kiểm tra bởi `tools/CategoryTest`. |
| I9 | **Mọi toạ độ đều tính theo nền 1920×1080, được scale lúc runtime bởi `Config.ScreenScale`.** Chỉ đo toạ độ bằng công cụ có nhận biết DPI. | DPI 125% làm một tiến trình không nhận biết DPI thấy màn hình 1536×864; mọi toạ độ lệch 25%. |
| I11 | **Phím dừng khẩn cấp không bao giờ được cắt giữa một click Buy và lúc nhận xác nhận.** Poll ở đâu cũng được; chỉ *hành động* tại các ranh giới liệt kê ở §7b. | Game có thể đã xử lý click đó rồi. Bỏ đi mà không chờ câu trả lời sẽ làm ledger mất dấu một lô hàng có thật — đúng lỗi §9.9, lần này là tự gây ra. |
| I10 | **Một thông báo lỗi mang tính *suy diễn*, không phải *quan sát trực tiếp*, luôn là nghi phạm số một.** | "purchase did not register" và "gone from the market" đều là những suy diễn sai, gây mất cả lô hàng. §9.9, §9.11, §9.12 |

---

## 1. Sơ đồ hệ thống

```
Client game Albion Online
     │ UDP :5056  (Photon)
     ▼
AlbionFeed                    C#, D:\AlbionTool\AlbionFeed
                              cần Npcap + quyền Administrator
     │ HTTP POST 127.0.0.1:5000/market    {"Orders":[...]}
     │ HTTP POST 127.0.0.1:5000/purchase  {"Purchase":{...}}
     ▼
MarketFeed                    thread riêng, Winsock, nlohmann/json
     │                        hai hàng đợi độc lập + bộ đếm atomic
     ├─ orders ────► MarketStore        chia theo dữ liệu, khoá theo order id
     │                   ├──► CityBuyer::Plan  → vector<FlipLot>
     │                   │      └► CityBuyer::Run → Holdings
     │                   └──► BlackMarketSeller::Run ← Holdings
     └─ purchases ─► CityBuyer::WaitForPurchase
```

Loop accept socket của `MarketFeed` là **thread phụ duy nhất mang tính sống còn**
của hệ thống. Mọi thứ khác chạy trên event thread của SFML (I4).

Việc định tuyến dữ liệu được quyết định bởi **hình dạng của body** (JSON), không
phải theo đường dẫn URL — nên có thể thêm một loại dữ liệu mới mà không cần đụng
vào logic định tuyến. Tên field giữ nguyên như game đặt, nên viết hoa chữ đầu.

### Hợp đồng dữ liệu với AlbionFeed

Tham chiếu bốn project từ một bản clone **không sửa đổi** của
`JPCodeCraft/AlbionDataAvalonia` tại `D:\AlbionTool\AlbionDataAvalonia`
(`Protocol16`, `PhotonPackageParser`, `Albion.Network`,
`AlbionDataAvalonia.Shared`). Không bao giờ chỉnh sửa bản clone đó — `git pull`
là cách để giao thức luôn cập nhật khi game thay đổi. Ứng dụng Avalonia gốc
không bao giờ được build; phần đó chứa uploader mà ta không cần.

Năm handler, mọi thứ khác bị bỏ qua:

| Packet | Trả về |
|---|---|
| `JoinResponse` | id bản đồ hiện tại (3003 = Black Market) |
| `AuctionGetOffersResponse` | các offer bán ở City |
| `AuctionGetRequestsResponse` | các order mua ở Black Market |
| `AuctionBuyOfferRequest` | order id + số lượng của một lượt mua client vừa gửi |
| `AuctionBuyOfferResponse` | câu trả lời của server cho lượt mua đó |

Có hai message mua hàng gửi về cho mỗi lượt mua: `confirmed == false` khi client
gửi request, sau đó `confirmed == true` khi server trả lời.

`UnitPriceSilver` trên đường truyền là **bạc × 10000**. AlbionFeed chia lại. Quên
chia sẽ gây sai số 10.000 lần.

**Vì sao không dùng `albiondata-client` của bên thứ ba:** (a) nó tồn tại để upload
lên Albion Data Project công khai, và để giữ im lặng thì phải nhớ thêm `-i ""`
mỗi lần chạy — AlbionFeed hoàn toàn không có uploader, đây là một **sự bảo đảm**
chứ không phải một cấu hình có thể quên; (b) việc forward `AuctionBuyOffer` là
điều khiến xác nhận mua hàng trở thành một **quan sát thực tế** thay vì một phép
đoán (§9.9).

Ghi lại để không bị phát hiện lại lần nữa: uploader NATS của client Go đó bị hỏng
(mỗi message mở connection mới, `Publish()` chỉ đưa vào buffer, connection bị bỏ
trước khi flusher chạy → không có PUB nào cả, đã kiểm chứng bằng
`nats-server -DV`), và tham số `-i noop` được ghi trong docs của nó thực ra làm
nó **panic** (`len(target) < 4` chấp nhận `"noop"`, rồi `target[0:8]` cắt chuỗi
vượt giới hạn).

---

## 2. Các file

### Module cốt lõi

| File | Vai trò |
|---|---|
| `MarketFeed.h/.cpp` | HTTP server tại 127.0.0.1:5000. JSON → `FeedOrder` / `FeedPurchase` vào hai hàng đợi riêng. Giữ `ItemTable` (id item → tên hiển thị). |
| `MarketStore.h/.cpp` | Phân loại order thành bid Black Market hay offer City **theo đúng dữ liệu**, khoá theo order id của game. |
| `MarketScanner.h/.cpp` | Điều khiển menu thị trường để chính *game* gửi yêu cầu danh sách. Giữ `CategoryLayout` (46 mục). |
| `CityBuyer.h/.cpp` | Ghép bid với offer, lập kế hoạch lô hàng, thực thi mua. Đây là phần tính toán cốt lõi. |
| `BlackMarket.h/.cpp` | Bán ledger vào các bid của Black Market. |
| `Holdings.h/.cpp` | Sổ ghi hàng đã mua chưa bán → `holdings.tsv`. |
| `MarketUi.h` | Dropdown tier / quality / enchantment. Chỉ có header, dùng chung cho scan+buy+sell. |
| `MoveToPosition.h/.cpp` | Di chuyển chuột giống người, click, gõ chữ. |
| `ScreenCapture.h/.cpp` | BitBlt vào một DIB tái sử dụng dưới dạng `cv::Mat`. Chỉ dùng để hash vùng ảnh. |
| `Ocr.h/.cpp` | **Code chết.** Vẫn compile được nhưng không ai gọi. I5. |
| `Game.h/.cpp` | Cửa sổ, state machine, toàn bộ xử lý nút bấm, mọi điểm vào `Run*`. |
| `LogSink.h/.cpp` | Chuyển hướng `std::cout` vào panel trong cửa sổ và `bot.log`. |
| `ScanProfile.h/.cpp` | Lựa chọn tier + enchant theo mỗi (địa điểm, category) → `scan_profile.tsv`. |
| `ScanProgress.h/.cpp` | Trạng thái quét theo từng bước cho màn hình Scanning. |
| `ShopList.h/.cpp` | Gộp các lot đã lập kế hoạch thành một mục cho mỗi lần tìm kiếm trên chợ, phục vụ màn hình mua tay. Toán thuần. |
| `AbortSwitch.h/.cpp` | Phím dừng khẩn cấp. Poll hotkey bằng `GetAsyncKeyState` và chốt cứng (latch). |
| `UiKit.h/.cpp` | Toàn bộ từ vựng vẽ: rounded rect, thang chữ iOS bằng Inter, list group, nút, chip, segmented control, nav bar. Chỉ phụ thuộc SFML và `Theme.h`. |
| `Theme.h` | Mỗi vai trò một symbol, đặt tên trùng khít variable trong file Figma. |
| `Shapes.h/.cpp`, `MyImg.h/.cpp` | **Code chết.** `TextBox` và ảnh nền là thứ UI cũ được dựng từ đó; giờ không ai vẽ chúng nữa. Khác `Ocr`, chúng đã bị **gỡ khỏi vcxproj** — `Shapes.cpp` là cảnh báo cuối cùng còn lại trong build, và code chết mà còn la làng thì tệ hơn cả giữ lẫn xoá. File vẫn nằm trên đĩa vì cây này không có commit nào để khôi phục. |
| `ProcessLauncher.h/.cpp` | Khởi động launcher của game và `run-feed.bat`. |

### File dữ liệu (trong `SFML/`, đây là working directory)

| File | Ai ghi | Định dạng |
|---|---|---|
| `Config.txt` | người dùng | `key: value`, dòng `#` là comment. Xem §7. |
| `categories.tsv` | người dùng | `key \t menu_top \t menu_sub \t slug_top \t slug_sub`. 46 dòng. `slug_*` được đọc nhưng không bao giờ dùng đến. |
| `items.tsv` | sinh ra từ ao-bin-dumps | `unique_name \t display_name`. 5165 dòng. |
| `scan_profile.tsv` | app tự ghi | `location \t category_key \t tiers(csv) \t enchants(csv)`. Tier rỗng = quét một lượt (one pass). Tự tạo; `EnsureDefaults` điền category mới. |
| `holdings.tsv` | app tự ghi | `itemTypeId \t tier \t enchant \t quality \t quantity \t unitCost \t searchText` |
| `bot.log` | app tự ghi | bản sao của log panel |

`scan_profile.tsv` và `holdings.tsv` hợp lệ nếu chưa tồn tại.

### Công cụ kiểm thử (`tools/`, mỗi công cụ có `build_*.bat` riêng)

| Tool | Cần game không? | Kiểm tra gì |
|---|---|---|
| `PlanTest` | không | Thuật toán `CityBuyer::Plan`. 26 test case. |
| `CategoryTest` | không | Tính nhất quán giữa `categories.tsv` ↔ `CategoryLayout`. 10 test case. Phải chạy từ `SFML/`: nó đọc `categories.tsv` theo working directory, chạy ở chỗ khác là fail. |
| `ShopListTest` | không | Cách gộp/xếp hạng của `BuildShopList` và cách tính lại giá của `CityBuyer::Shortfall`. 26 test case. |
| `UiPreview` | không | Không phải test — render các màn hình thật ra PNG qua `RenderTexture`, không cần cửa sổ, không cần game. `build_uipreview.bat` build, chạy từ `SFML/` rồi tự dọn. Cách nhanh nhất để xem một thay đổi bố cục. |
| `WipeTest` | không | `LogSink::Wipe()` |
| `FeedTest` | **có** | nhận packet thật; chờ 90 giây có traffic |
| `OcrWordTest` | không | mồ côi cùng với nhánh OCR |

---

## 3. State machine của UI

`Game::current_wnd` là một chuỗi (string).

| Trạng thái | Được vẽ bởi | Click được định tuyến tới |
|---|---|---|
| `Menu` | `render()` + `vTB_menu` | `MenuConroller` |
| `Scan` | `render()` + `vTB_scan` | `ScanController` |
| `Scanning` | `DrawScanningScreen()` | `ScanningController` |
| `Settings` | `DrawSettingsScreen()` | `SettingsController` |
| `Shop` | `DrawShopScreen()` | `ShopController` |

**Không màn nào còn sở hữu `TextBox` nữa.** Mọi màn vẽ qua `UiKit` và tự test
va chạm bằng một hàm `Hit*` gọi ĐÚNG cái helper bố cục mà phần vẽ gọi —
`MenuBlocks`, `Ui::LayoutGroup`, `Ui::ChipRect`, `ShopBlockRect`. Một cú click
chỉ có thể rơi vào ô do chính hàm đó sinh ra.

Đó cũng là thứ gỡ đường click ra khỏi tính duy nhất của `menu_sub`. Mỗi hàng mang
một `command` id tách rời nhãn hiển thị, và category được định tuyến theo KEY.
I8 vẫn còn hiệu lực cho phần scanner khớp nút trong `Game.cpp`, nhưng một
`menu_sub` trùng giờ chỉ là vấn đề thẩm mỹ chứ không còn làm hai nút tự bật/tắt nhau.
| `EnterWndBM` | `drawEnterWnd()` | chỉ nhận bàn phím |
| `EnterWndBuyProfit` | `drawEnterWnd()` | chỉ nhận bàn phím |
| `EnterWndBuyMinProfit` | `drawEnterWnd()` | chỉ nhận bàn phím |
| `EnterWndBuyCombine` | `DrawBuyCombineScreen()` | `BuyCombineController` |
| `EnterWndBuy` | `drawEnterWnd()` | chỉ nhận bàn phím |

Các state nhập số nhận `Num0`–`Num9`, `Backspace`, `Enter`, `Escape`.
`MAX_ENTERED_DIGITS = 12`, được parse bằng `stoll` thành `long long`.

**Thêm một state mới cần sửa ba nơi**, thiếu bất kỳ nơi nào cũng sẽ lỗi trong im
lặng: định tuyến trong `render()`, định tuyến click/bàn phím trong
`updateSFMLEvents`, và danh sách xử lý `Escape`. `EnterWndBuy` từng bị thiếu
nhánh trong `render()` chính vì lý do này (§9.8).

---

## 4. Các nút bấm — hành vi chi tiết

Thứ tự menu chính lấy từ `vMenu` trong `Menus.h`:
`SellBM, Scan, Settings, Plan, Deals, BuyCity, Capture, Clear, Exit`.

`BACK` màu cam, luôn ở góc trên-phải mọi màn hình (`BackButtonPos()` =
`WIDTH-132, 8`).

`MenuConroller` sẽ return sớm nếu lệnh click rỗng — một click vào khoảng trống
không được phép xoá bảng kế hoạch đang hiển thị (§9.8).

### SellBM
→ `EnterWndBM`. Nhập một số: % lợi nhuận yêu cầu trên chi phí đã ghi nhận.
Khi Enter: xoá log, "chuyển sang cửa sổ game", `Sleep(2000)`, `WindowHidden`,
`BlackMarketSeller::Run`, `holdings.Save`.
Đọc từ `holdings.tsv`, **không phải** từ order book. Xem §5.3.

### Scan
→ `Scan`. Các ô category chuyển màu xanh khi được chọn (`selectedScan`, một
`set<string>` các key). Các nút: `SCAN BM`, `SCAN MARKET`, `CLEAR` (chỉ xoá lựa
chọn), `BACK`. Các nút category được gán nhãn và so khớp theo `menuSub` (I8).

### Settings
→ `Settings`. Sửa `scan_profile.tsv`, **lưu ngay ở mỗi lần click**, không có
bước "Apply".

| Điều khiển | Hiệu ứng |
|---|---|
| `BLACK MARKET` / `CITY MARKET` | đang sửa profile nào (`settingsBlackMarket`) |
| ô category | chọn category mà các nút bấm dưới đây sẽ tác động |
| `T1`..`T8` | bật/tắt tier cho category đang chọn |
| `E0`..`E4` | bật/tắt enchantment. **Chỉ áp dụng cho City** — nút của BM vẫn hiển thị nhưng vô hiệu, vì scanner bỏ qua thiết lập này |
| `ONE PASS (NO TIER)` | xoá toàn bộ danh sách tier |
| `APPLY TO ALL` | copy tier **và** enchant của category hiện tại sang mọi category khác **chỉ trong tab đang mở** |

**Điểm không hiển nhiên:** danh sách tier trống **không có nghĩa là "bỏ qua"**.
Nó có nghĩa là *quét một lượt duy nhất mà không mở dropdown tier* — đây là mặc
định của Black Market. Muốn loại một category khỏi lượt quét, phải bỏ tick nó ở
màn hình **Scan**.

**Hệ quả ảnh hưởng tới việc mua:** `Game::AllowedCityTiers()` gộp (union) tier
của mọi category trong profile CITY thành `BuyPlan::allowedTiers`, đây là một
gate cứng. **Nếu chỉ một category ở chế độ ONE PASS thì phần union sẽ rỗng,
nghĩa là không có giới hạn gì cả** — vì quét một lượt sẽ mang về mọi tier, nên
nói ngược lại sẽ là nói sai về dữ liệu thật.

### Plan
Chạy thử (dry run) của BuyCity. Đặt `pendingBuyDryRun = true`, đi qua **cùng bốn
màn hình**, và ở bước 4 gọi `RunPlan` thay vì `RunBuy`. In ra bảng cùng dòng
tổng; **không mua gì cả**. Ngân sách `0` = không giới hạn. Kết thúc ở màn hình
`Shop`, bảng vẫn còn nguyên trong log phía sau.

### Deals
Không hỏi gì. `BuildPlan(0, Config.MinProfitRate, Config.MinProfitSilver,
Config.RequireBothProfits)` — toàn bộ ngưỡng lấy từ `Config.txt`, không có ngân
sách. In danh sách đã xếp hạng, không tốn tiền, và kết thúc ở màn `Shop` giống Plan.
Đây là nút "ngoài kia đang có gì", nên nó là nút dễ được bấm nhất bởi người
đang quyết định mua gì bằng tay; để màn Shop hiện danh sách của lượt trước còn
tệ hơn là không hiện gì, vì danh sách cũ vẫn trông như đang mới.

`Plan` với ngân sách 0 == `Deals` với ngưỡng do bạn tự nhập.

### BuyCity
Đặt `pendingBuyDryRun = false`, sau đó:

| Bước | State | Ý nghĩa |
|---|---|---|
| 1/4 | `EnterWndBuyProfit` | % lợi nhuận tối thiểu, → `pendingBuyProfitRate = v/100` |
| 2/4 | `EnterWndBuyMinProfit` | lợi nhuận net tối thiểu **tính theo tổng**, đơn vị bạc. `0` bỏ qua bước 3 |
| 3/4 | `EnterWndBuyCombine` | nút `AND` / `OR` → `pendingBuyRequireBoth` |
| 4/4 | `EnterWndBuy` | ngân sách bạc. `0` = không giới hạn |

Mỗi nhãn đều có tiền tố `PLAN:` hoặc `BUY:` — bốn màn hình hỏi giống nhau tuyệt
đối trong đó chỉ một cái thật sự tốn bạc, đây là thứ dễ nhầm nếu không cẩn thận.

Sau đó: in kế hoạch, "chuyển sang cửa sổ game", `Sleep(2000)`, `WindowHidden` +
`Abort::Guard`, `CityBuyer::Run`, `holdings.Save`, **`store.ClearCities()`**, rồi
`CityBuyer::Shortfall` và màn hình `Shop` — hoặc quay về `Menu` nếu lượt chạy đã
mua đủ mọi thứ trong kế hoạch.

**`store.ClearCities()` nghĩa là phải quét lại City trước khi chạy BuyCity lần
tiếp theo.** Nửa dữ liệu Black Market vẫn còn, vì SellBM cần dùng đến nó.

### Shop
Danh sách mua tay. Mỗi mục là **một lần tìm kiếm trên chợ** — item + tier +
enchant, tức đúng chuỗi mà `searchText` sinh ra — chứ không phải mỗi lot một
dòng, và xếp hạng theo lợi nhuận của cả block. Click một block sẽ copy chuỗi tìm
kiếm của nó vào clipboard (`sf::Clipboard`) và làm mờ block đó, để có thể làm từ
trên xuống qua nhiều lần alt-tab. Lăn chuột để cuộn; `Escape` và `BACK` đều quay
về `Menu` chứ không đóng chương trình.

**Không có gì bị lọc khỏi danh sách.** Một block chỉ có một đơn vị vẫn là một
thứ phải đi mua, và `ShopListTest` giữ điều đó: một lot 1 đơn vị, sáu mươi món
vào thì sáu mươi block ra, và một phần thiếu 1 đơn vị — tất cả đều phải sống
sót tới màn hình. Log panel bên dưới cũng không còn bị chặn ở 20 dòng: nó hiện
đúng số dòng vừa khung, vì 20 dòng đã âm thầm cắt đuôi mọi plan trên ~17 lot.

Bản thân nút này **chỉ mở lại danh sách gần nhất, không bao giờ dựng lại.** Dựng
lại sẽ trả lời một câu hỏi khác: lúc đó `store.ClearCities()` đã chạy rồi, nên
một plan mới sẽ rỗng và bị đọc thành "phần còn thiếu đã mua xong".

Block gộp nhiều band quality sẽ hiện quality **thấp nhất** và giá trần **thấp
nhất**, và cả hai bắt buộc phải là con thấp nhất. Một block chứa order đòi Good
(trần 12,450) và order đòi Excellent (trần 20,100) mà ghi Good cạnh 20,100 sẽ mời
người dùng mua một món Good giá 18,000 — món đó không khớp được order Excellent,
và bán vào order Good thì lỗ. Được `ShopListTest` kiểm tra.

Giá hiển thị là `FlipLot::marginCeiling`, không phải `maxBuyPrice`: câu hỏi mà
người mua tay đang trả lời là "tao được phép trả tới đâu?", còn `maxBuyPrice` trả
lời "plan đã trả tới đâu?" — hẹp hơn và là chuyện khác. Xem §6 stage 2b.

### Capture
`RestartFeedClient()` — kill mọi client capture, khởi động `run-feed.bat`
(chạy với quyền elevated; Npcap cần Administrator). Đặt
`lastCaptureRestartTick`. Đây là cách chữa phổ biến nhất cho lỗi "scan trả về 0
order".

### Clear
Xoá toàn bộ, theo đúng thứ tự: chụp lại số liệu hiện tại → `store.Clear()` →
`scanProgress.Clear()` → `holdings.Clear()` + `holdings.Save()` →
`feed.Drain()` (nếu không thì lần `Absorb` kế tiếp sẽ nạp lại đúng những gì vừa
xoá) → `remove("_save_data.txt")`, `remove("ocr_miss.png")` →
`GlobalLog.Wipe()` → in tổng kết.

In cảnh báo lớn khi ledger không rỗng: **hàng hoá không biến mất khỏi túi đồ
trong game khi ledger bị xoá.** SellBM sẽ không còn động tới chúng nữa.

### Exit
`window->close()`.

### Màn hình Scanning
Các ô category đổi màu: xám = không nằm trong lượt quét này, vàng cam = đang
chạy, xanh = ổn, đỏ = lỗi. Đỏ nghĩa là menu category chưa từng mở được.
`RETRY FAILED` sẽ chạy lại đúng những bước đó.

Phần trăm tiến trình cố tình chỉ là gần đúng — mỗi category được coi là một
phần bằng nhau bất kể có bao nhiêu tier. **Thanh tiến trình không có animation**:
cửa sổ bị minimize (I3) và quá trình scan chặn event thread (I4), nên nó chỉ
hiện trạng thái cuối cùng.

---

## 5. Chuỗi thao tác click

Mọi toạ độ tính theo nền 1920×1080. `MoveAndClickRect` chọn một **điểm ngẫu
nhiên bên trong** hình chữ nhật (`ClickInside`), di chuyển theo đường cong có
easing (`HumanMove`), rồi click. `MoveToRect(rect, holdRowY = -1)` làm giống vậy
nhưng không click và trả về y đã dùng; nếu truyền `holdRowY` thì chỉ thay đổi x.

### 5.1 Scan — `Game::RunScan(blackMarket)` → `LaunchScan` → `MarketScanner::ScanCategory`

```
với mỗi category:
  1  ClickAndSettle(resetTabs 1334,271-1336,273, watch typeList)
        dọn sạch mọi tab mà category trước để lại đang mở.
        Làm việc này trước, không phải như một bước dọn dẹp sau cùng, để
        một category lỗi giữa chừng vẫn để lại trạng thái đã biết cho
        category tiếp theo.
  2  ChooseCategory:
       a  ClickAndSettle(typeButton 759,268-875,278, watch typeList)
       b  rowY = MoveToRect(entry.parent)          chỉ hover, KHÔNG click
       c  hash subList  (đo baseline TRƯỚC lần hover mở nó)
       d  MoveToRect(entry.sub, rowY)              trượt sang phải, giữ đúng dòng
       e  SettleRegion(subList)
       f  MoveAndClickRect(entry.sub)
     lỗi ở đây = mọi bước dự kiến của category này đều báo lỗi
  3  nếu danh sách tier rỗng  (mặc định của black market):
       PageThroughList(40)
     ngược lại, với mỗi tier:
       ChooseTier(tier)                            toạ độ đã đo, không OCR
       nếu có set enchant (chỉ City):
         với mỗi level: ChooseTier(tier) LẠI, ChooseEnchantment(level),
                    PageThroughList(40)
       ngược lại:
         PageThroughList(40)
sau khi hết mọi category:
  store.Absorb(feed)
```

Submenu mở **khi hover, không phải khi click** — click vào mục cha sẽ làm menu
đóng lại. Đó là lý do bước 2b/2d tồn tại, và vì sao 2d phải giữ nguyên `rowY`:
rời khỏi dòng đó sẽ làm submenu đóng.

Dropdown tier được **mở lại cho mỗi mức enchantment**, không phải một lần cho
mỗi tier. Việc này tốn thêm một click menu nhưng ngăn được trường hợp bộ lọc
tier bị game âm thầm bỏ giữa lượt quét, làm hỏng mọi level phía sau nó.

Enchantment được tách riêng vì một tier của một món đồ phổ biến sẽ vượt quá giới
hạn ~20 tab của game nếu liệt kê tất cả level cùng lúc, và phần vượt quá bị bỏ
trong im lặng. `feed.LastOrderEnchant()` là cách kiểm tra rẻ nhất để biết bộ lọc
có thực sự được áp dụng.

`SettleRegion` có cả một **ngưỡng sàn (floor)** bên cạnh timeout: con trỏ chuột
di chuyển qua panel sẽ highlight một dòng, đây là một lần vẽ lại (repaint) thật,
nên hai khung hình trước/sau lần highlight đó có thể giống nhau một cách hợp lệ
trong khi dropdown còn chưa bắt đầu vẽ. `ChooseCategory` chỉ được gọi một lần
cho mỗi category, không có gì để thử lại, nên return sớm sẽ làm mất cả category
đó, còn việc chờ thêm chỉ tốn vài milli giây.

#### `PageThroughList` — vòng lặp lật trang

```
lặp tối đa maxClicks (40) lần:
  before        = feed.MessagesSeen()
  ordersBefore  = feed.OrdersSeen()
  MoveAndClickRect(nextPage 986,882-997,892)
  clickedAt = now                    ← đo thời gian SAU KHI click chạm tới,
                                       không phải trước; HumanMove tốn tới
                                       move_max_ms trước đó, và một deadline
                                       bắt đầu trước lúc đó có thể lệch tới
                                       gần một giây
  sleep(page_click_delay_ms)
  budget = PageBudgetMs(...)         ← tính từ độ trễ đã quan sát, giới hạn
                                       trên bởi page_timeout_ms
  chờ tới khi MessagesSeen() > before, hoặc hết budget

  nếu có dữ liệu:
     observedPageMs = (observedPageMs*3 + elapsed)/4    ← đo TRƯỚC khi settle,
                                       để nó vẫn giữ đúng nghĩa "thời gian tới
                                       packet đầu tiên"; đo sau khi settle sẽ
                                       làm phình to mọi budget về sau vì một
                                       trang
     ++pages; quiet = 0
     nếu page_size > 0:
        settle: kéo dài chờ khi packet còn tiếp tục đến, dừng khi có một
                khoảng lặng
        onPage = OrdersSeen() - ordersBefore
        nếu 0 < onPage < page_size:  → hết danh sách, break
  ngược lại:
     nếu ++quiet < page_quiet_clicks: click lại
     ngược lại: break, kèm log lý do
```

Có hai điều kiện dừng độc lập:

- **Trang ngắn** (`page_size`, mặc định 50). Một tab có ít hơn 50 dòng chính là
  tab cuối cùng. Đây là bằng chứng dương tính (positive evidence), nên không cần
  xác nhận thêm.
  **Bước chờ settle là bắt buộc:** vòng chờ ở trên trả về ngay khi có packet
  *đầu tiên*, còn một trang thực ra đến trong nhiều packet, nên nếu đếm ngay ở
  đó sẽ chỉ thấy một phần của 50, trông y hệt một trang ngắn và sẽ kết thúc
  danh sách sớm trong im lặng. `onPage == 0` **không** được coi là trang ngắn —
  đó là một trường hợp khác, và kiểm tra "click im lặng" (quiet-click) là cách
  xử lý trung thực cho nó.
- **Click im lặng** (`page_quiet_clicks`, mặc định 2). Một lần là không đủ: một
  round trip chậm không thể phân biệt được với việc danh sách đã hết.

### 5.2 Mua — `CityBuyer::Run(lots, settings, holdings)`

Lô hàng được lấp đầy **từng band chất lượng một lúc**, đi lần theo
`lot.qualityOrder` — xem §6 stage 2b để biết vì sao danh sách đó tồn tại, và
§9.21 để biết nó khắc phục lỗi gì. `remaining`, `blindBuys` và `shiftedRows` sống
xuyên suốt các band vì chúng thuộc về cả lô hàng; `live`, `mark`, `rounds` và
`retries` chỉ thuộc riêng từng band.

```
với mỗi lô, theo đúng thứ tự kế hoạch (lợi nhuận tổng lớn nhất trước):
  bỏ qua nếu searchText rỗng            → "no name for X, skipping"
  bỏ qua nếu doneKeys đã có (id,ench,qual) → đã mua rồi trong lượt này
  bỏ qua nếu lot.spend > left            → hết ngân sách

  với mỗi band trong lot.qualityOrder, khi remaining > 0:
    ceiling = band 0 ? lot.maxBuyPrice
                     : max(lot.maxBuyPrice, lot.marginCeiling)
    SelectQuality(band)               nút quality + dòng, cả hai đều đo toạ độ.
                                      Cố tình KHÔNG bỏ qua dù quality trùng lần
                                      trước: không có gì đảm bảo bộ lọc còn giữ
                                      sau một lần search + clear.
  SearchFor(lot.searchText, &mark):
      ClearItemName()   click 722,270
      sleep(menu_open_delay_ms)
      feed.Drain(stale)               ← bỏ đi những gì lệnh clear vừa gây ra
      mark = feed.MessagesSeen()      ← LẤY SAU khi drain. Thứ tự này CHÍNH
                                        LÀ cách sửa lỗi §9.12
      MoveAndClickRect(search 586,264-697,277)
      TypeString(text)                mỗi ký tự một lệnh SendInput (xem dưới)
      sleep(menu_open_delay_ms)

    đọc lại kết quả, tối đa search_retries (3) lần:
        WaitForRefresh(mark, wait_timeout_ms)
        mark = MessagesSeen()
        pullLive()                            → khớp: tiếp tục
    không khớp gì cả → thử BAND TIẾP THEO, không phải lô tiếp theo

  sau mỗi band: bandsUsed == 0 → "gone from the market in every
                                     quality that fits", vanished++

  vòng lặp mua, tối đa lot.quantity*2 + 8 lượt:
      top = live.front()                      offer còn sống rẻ nhất
      nếu top.price > ceiling          break   ← không phải lỗi chết: band
                                                tiếp theo có thể vẫn rẻ hơn
      nếu top.price > left             break
      take = min(top.amount, remaining, left/top.price, buy_units_per_click)
      feed.DrainPurchases(stale)              ← để một xác nhận cũ không bị
                                                đọc nhầm thành xác nhận này
      mark = MessagesSeen()
      BuyTopRow(take):
          MoveAndClickRect(buyFirstRow 1232,424-1314,440)
          nếu take > 1: amountField 567,570-583,578, Ctrl+A+Del, gõ số
          MoveAndClickRect(confirmBuy 844,722-922,739)
          MoveAndClickRect(levelWarning 819,545-899,555)  ← vô hại khi không có,
                                                            cần thiết khi có
      WaitForPurchase(top.id, purchase_confirm_ms)
```

`WaitForPurchase` tự rút ngắn deadline ngay khi thấy packet *request* được gửi:
câu trả lời của server luôn đi sau đúng một round trip, nên việc giữ nguyên cả
budget thời gian là lãng phí thời gian cho một deadline không còn ý nghĩa. Đường
này chạy một lần cho mỗi đơn vị hàng — ở mức bốn giây, một lần mất xác nhận từng
làm treo một lô 20 đơn vị hơn cả một phút.

| Kết quả | Cách xử lý |
|---|---|
| `Confirmed` / `Sent` | tính vào, `holdings.Add` **ngay lập tức** (một lượt chạy bị gián đoạn vẫn có hàng thật trong túi), giảm dòng cục bộ tương ứng, refresh nếu game gửi lại |
| `None` | click chưa từng chạm tới game. Click lại tối đa `buy_retries` lần, sau đó bỏ lô này |
| `Elsewhere` | mua vào một order id khác — xem dưới |

**`Elsewhere` là "thời tiết" bình thường của một thị trường sôi động, không phải
lỗi.** Dòng đang nhắm tới đã bị người khác lấy, dòng bên dưới trượt lên, click đó
mua trúng dòng đó. Cách xử lý:

1. tìm `boughtOrderId` trong `live` để lấy giá của nó
2. dù thế nào cũng ghi vào `holdings` — hàng đã vào túi rồi. Chi phí không rõ sẽ
   được xử lý ở bước sau: `BlackMarketSeller` sẽ bán chúng vào bất kỳ bid dương nào
3. loại các đơn vị đó khỏi bản copy cục bộ, nếu không lượt sau sẽ nhắm vào một
   dòng đã biến mất và lại bị trượt tiếp vì đúng lý do cũ
4. `paid == 0` (không có trong listing → margin chưa từng được kiểm tra): ghi
   log, tính đếm, bỏ lô này sau khi vượt `stray_buy_limit` (3)
5. `paid > lot.maxBuyPrice`: đây là vi phạm margin thật, dừng lô
6. còn lại: tính vào `shiftedRows`, **tiếp tục** sau khi đọc lại

Cuối mỗi lô chỉ in một dòng khi có gì cần nói:
`12/12 units, 11 rows shifted under the click`.

Chỉ **dòng trên cùng (top row)** được click. Danh sách xếp giá tăng dần và mua
hết nó sẽ làm dòng kế tiếp trượt vào đúng vị trí đó, nên một lượt gom hàng chỉ
là một nút bấm lặp lại — không có toạ độ riêng cho từng dòng, không cần cuộn.

`buy_units_per_click` luôn giữ ở 1: hộp thoại mua chỉ hiện thêm ô số lượng khi
dòng đó có nhiều hơn một đơn vị, và việc gõ chữ vào một hộp thoại không có ô đó
sẽ làm hỏng mọi thứ diễn ra sau nó (§9.9).

`TypeString` gửi **mỗi ký tự bằng một lệnh `SendInput` riêng**. Gọi cả chuỗi một
lần trông giống một hành động "paste" ở tầng OS; chữ vẫn hiện ra nhưng ô
autocomplete của thanh tìm kiếm không bao giờ mở, vì không có sự kiện gõ từng ký
tự nào được kích hoạt. `type_char_delay_ms` phải luôn lớn hơn 0.

### 5.3 Bán — `BlackMarketSeller::Run(plan, holdings)`

Đi qua **ledger**, không phải order book. Đi qua order book đồng nghĩa với việc
tìm những món hàng mà nhân vật không sở hữu và bấm bán vào bất cứ dòng nào đang
hiện ra.

```
Absorb()                                  bỏ dữ liệu feed cũ
với mỗi stack trong holdings:
  minBid = ceil(unitCost * (1 + minProfitRate) / 0.96)
  typed  = SellSearchText(stack.searchText)  ← " T6.0" → " t6.0", chữ t viết
                                               thường. CHỈ chữ đó; hạ toàn bộ
                                               chữ về lowercase (tolower())
                                               sẽ làm phẳng tên item, đây
                                               chính là thứ game dùng để
                                               so khớp
  tối đa 12 lượt khi còn đơn vị chưa bán:
      before = MessagesSeen()
      ClearItemName(); click search 586,264-697,277; TypeString(typed)
      sleep(menu_open_delay_ms)           ← feed có thể đã có giá trước khi
                                            client kịp vẽ lại các dòng; click
                                            vào một danh sách vẫn còn là của
                                            item trước là cách một lượt bán
                                            rơi vào nhầm dòng
      WaitForRefresh(before)  → không có gì: bỏ qua stack này
      SelectQuality(stack.quality)
      WaitForRefresh(...)                 cố gắng hết sức; game không luôn
                                          tự gửi lại request sau khi đổi filter
      feed.Drain(fresh)
      chọn bid tốt nhất mà: isBuyOrder, location 3003, groupTypeId+enchant
                           khớp, stack.quality >= order.quality
      không có bid           → vanished++,  sang stack tiếp theo
      bid < minBid     → tooCheap++,  sang stack tiếp theo
      cùng order id VÀ amount không nhỏ hơn lần trước
                       → "sale did not register", để lại stack đó
      qty = min(remaining, order.amount)
      MoveAndClickRect(sell 1238,427-1323,442);      sleep
      MoveAndClickRect(confirmSell 848,725-919,735); sleep
      holdings.Remove(...); report.silver += price*qty*0.96
```

Một lần click bán sẽ chuyển động cả **stack**, giới hạn bởi số lượng order còn
muốn mua. `maxRounds = 12`, không phải `quantity + 8` — giới hạn cũ đó xuất phát
từ việc tưởng lầm rằng một click chỉ bán một đơn vị.

Xoá ô tìm kiếm trước quan trọng không kém gì ở phía mua: nếu không, ô đó vẫn giữ
tên của stack trước và chữ mới bị nối thêm vào, nên stack đầu tìm đúng còn mọi
stack sau đó tìm ra một chuỗi vô nghĩa — điều này đọc giống "không còn bid nào
cho nó" hơn là một lỗi gõ chữ.

---

## 6. Phần tính toán — `CityBuyer::Plan`

Các quy tắc thị trường được mã hoá cứng, tất cả đã được xác nhận và là nền tảng
cho mọi tính toán:

- **Chất lượng (Quality):** black market chấp nhận hàng có chất lượng **bằng
  hoặc cao hơn** mức yêu cầu.
  `QualityFills(offered, wanted) { return offered >= wanted; }`
- **Thuế bán 4%:** `NetAfterTax(p) = (long long)(p * 0.96)`
- **Không có phí đặt lệnh** — bot chỉ khớp vào offer có sẵn thay vì đăng lệnh
  mới. Đăng lệnh tốn 2.5% giá trị lệnh *dù nó không bao giờ khớp*, đây là lý do
  ý tưởng "đăng một lệnh mạnh tay để vượt qua spread" bị loại bỏ.
- Khớp lệnh diễn ra theo giá của **người bán**.

### Stage 1 — Lọc cứng (loại thẳng)

| Gate | Dòng code | Loại bỏ những gì |
|---|---|---|
| junk | `CityBuyer.cpp:159` | `price <= 0`, `amount <= 0`, sai phía |
| `maxUnitPrice` | `:162` | mọi offer vượt mức giá trần theo đơn vị |
| `allowedTiers` | `:170` | tier không thuộc profile scan của CITY — xem §4 Settings |
| quality | `:27` | quyết định offer đó có thể khớp vào order nào |

Tier bị loại **ngay tại lúc offer được nạp vào kế hoạch**, không phải lọc ra
khỏi kết quả sau đó: một lô hàng chưa từng được tạo ra thì không thể bị mua nhầm,
và điều này giữ cho con số "không có gì đáng mua" luôn trung thực.

Offer ở City được nhóm theo `(groupTypeId, enchant, quality)`.

> **Dùng `groupTypeId`, không bao giờ dùng `itemTypeId`.** Feed gửi cả hai:
> `itemTypeId` mang theo hậu tố enchant (`T6_MAIN_SWORD@2`), `groupTypeId` thì
> không. Enchant được lưu riêng, và mọi so sánh sau đó — `CityBuyer::Run` so
> khớp offer đang sống, `BlackMarketSeller` so khớp bid với ledger — đều kiểm
> tra `groupTypeId`. Việc lưu dạng có hậu tố khiến mọi lô hàng có enchant báo
> "gone from the market" và mọi stack có enchant không thể bán được. Item
> thường không có hậu tố, đó là lý do lỗi này bị che giấu lâu.

### Stage 2 — Cầu là một bể hữu hạn

Các order ở Black Market được index theo `(groupTypeId, enchant)`, **gộp toàn
bộ**, sắp xếp giá giảm dần, mỗi order mang theo `left = amount` (số lượng còn
lại).

Các band chất lượng cạnh tranh nhau để giành lấy bể đó. Thứ tự lập kế hoạch,
`CityBuyer.cpp:220`: sắp theo `(itemTypeId, enchant, giá offer rẻ nhất,
quality)` — **band nào có thể khớp một order với giá rẻ nhất sẽ giành order đó
trước**, và đây cũng chính là band lãi nhiều nhất trên những đơn vị đó.

Đây là thuật toán tham lam (greedy), không phải tối ưu tuyệt đối: một band chất
lượng cao nhưng rẻ có thể chiếm mất một order mà một band thấp hơn cũng có thể
khớp được, khiến order của band thấp bị bỏ lại không khớp. Một thuật toán
min-cost flow sẽ ghép cặp tốt hơn. Sai số luôn nằm ở hướng an toàn — kế hoạch
luôn tính ra **ít hơn** số đơn vị mà thị trường thực sự có thể hấp thụ, chứ
không bao giờ nhiều hơn.

### Stage 2b — Danh sách dự phòng (fallback)

Mỗi lô hàng cũng ghi lại **mọi** band có thể khớp các order của nó, theo thứ tự
band nào cung cấp rẻ nhất trước, lưu trong `FlipLot::qualityOrder` (band đã
được lập kế hoạch luôn đứng đầu). "Có thể khớp" được quyết định bởi chất lượng
của ORDER, không phải của band: một order yêu cầu Good sẽ chấp nhận Good và mọi
mức cao hơn nó, nên một band thấp hơn `bmQuality` sẽ không bao giờ được liệt kê
— mua nó sẽ để lại một stack không có ai mua.

`FlipLot::marginCeiling` đi kèm với danh sách đó: `NetAfterTax(bid rẻ nhất
trong lô) / (1 + minProfitRate)`. Dùng giá rẻ nhất chứ không phải giá trung
bình, để nó đúng cho từng đơn vị riêng lẻ bất kể đơn vị đó cuối cùng khớp với
order nào.

`maxBuyPrice` không thể làm được việc đó. Nó là offer đắt nhất của band đã được
lập kế hoạch, và một band dự phòng thường *đắt hơn* — vì band rẻ mới là band
được chọn lập kế hoạch — nên `maxBuyPrice` sẽ từ chối mọi band dự phòng trước
khi nó kịp được xem xét. Xem §9.21.

### Stage 3 — Vòng ghép cặp thử nghiệm (chưa chốt gì cả)

Offer xếp tăng dần, order xếp giảm dần, các đơn vị được ghép cặp lần lượt. Dừng
lại khi hết cầu **hoặc** khi `NetAfterTax(bidPrice) <= offerPrice`. Dùng
`break`, không phải `continue`: vì hai hàng đợi di chuyển ngược hướng nhau, một
khi một đơn vị bị lỗ thì mọi đơn vị sau đó cũng sẽ lỗ.

### Stage 4 — Chọn độ sâu (bao nhiêu đơn vị nên mua)

Đi qua mọi tiền tố (prefix) `k` của vòng ghép cặp:

```
avgBuy = runSpend / runQty
avgNet = runNet   / runQty          dừng nếu avgNet <= avgBuy
rate   = (avgNet - avgBuy) / avgBuy
profit =  runNet - runSpend

byRate   = rate   >= minProfitRate
bySilver = profit >= minProfitSilver
accept   = minProfitSilver <= 0 ? byRate
         : requireBoth        ? (byRate && bySilver)
         :                      (byRate || bySilver)
```

Giữ lại prefix được chấp nhận có lợi nhuận lớn nhất; **lớn hơn thực sự (strict)**,
nên khi hoà thì giữ prefix ngắn hơn (cùng lợi nhuận, ít bạc hơn, ít click hơn).

> Lấy nhiều đơn vị hơn làm tăng tổng lợi nhuận nhưng làm giảm tỉ suất %. Không có
> "độ sâu tốt nhất tuyệt đối" — nó tuỳ vào ngưỡng nào đang được tối ưu hoá. Vì vậy
> bộ lập kế hoạch đi qua toàn bộ vòng ghép cặp và chọn, thay vì dừng ngay tại
> điểm tỉ suất % vừa đủ ngưỡng. Ở chế độ OR, câu trả lời có thể sâu hơn nhiều so
> với việc chỉ nhìn tỉ suất %; ở chế độ AND, nó lại nông hơn.

Chỉ sau đó độ sâu mới được chốt: `pool[pick].left -= units`, các ladder được
dựng lên.

`FlipLot` mang theo hai ladder — `ladder` (giá offer, số lượng) xếp rẻ nhất
trước, và `revenueLadder` (giá bid, số lượng) xếp đắt nhất trước. `bmPrice` là
giá trung bình có trọng số trên các order đã khớp; `PrintPlan` sẽ nói rõ điều
này khi một lô trải rộng qua nhiều order.

### Stage 5 — Xếp hạng

Sắp theo `profit` giảm dần, hoà thì theo `spend` tăng dần. **Đây chính xác là
thứ tự mà `Run` sẽ dùng để mua**, không bị thay đổi bởi bất cứ bước nào sau đó.

Cố tình **không** xếp theo lợi-nhuận-trên-mỗi-đơn-vị-bạc: cách suy nghĩ đó chỉ
đúng khi ngân sách là thứ duy nhất có thể hết. Trong thực tế, các offer bị người
chơi khác lấy mất trong lúc bot đang xử lý một danh sách toàn deal nhỏ nhưng % lãi
cao, và deal đáng giá 200 nghìn bạc chính là deal biến mất trước tiên.

### Stage 6 — Cắt theo ngân sách

Đi theo đúng thứ tự đã xếp hạng. Mua nguyên lô nếu vừa ngân sách; nếu không, đi
theo `ladder` (rẻ nhất trước) và lấy phần mà số bạc còn lại có thể chi trả. Một
lô bị cắt sẽ giữ lại các đơn vị **rẻ nhất**, những đơn vị này lại được ghép với
các order **đắt nhất**, nên nó phải được tính giá lại từ `revenueLadder` — không
tính từ giá trung bình của cả lô, vì cách đó sẽ tính sai cả số lượng vừa vặn và
số tiền kiếm được. Gate lợi nhuận được áp lại cho lô đã cắt; nếu không đạt thì
**bỏ qua lô đó và tiếp tục**, để số bạc còn lại chuyển sang lô kế tiếp phù hợp
hơn.

### Không có trần lợi nhuận

Không có `absurdProfitRate` (ngưỡng lãi "vô lý"). Một khoảng cách giá lớn chính
là hình dạng của một món hời thật; điều làm cho một cặp ghép đáng tin cậy nằm ở
phần tính toán xung quanh nó (item + enchant khớp đúng, quality được lọc đúng,
ladder được quét theo thứ tự rẻ nhất trước), không nằm ở độ lớn của con số.

---

## 7. Cấu hình (`SFML/Config.txt`)

Mọi key đều có giá trị mặc định để dùng nếu thiếu hoặc không parse được — một
file config bị hỏng không bao giờ làm chương trình dừng khởi động. `ToInt` được
viết tay (tối đa 9 chữ số, chỉ chấp nhận số) vì `stoi` từng ném exception trước
khi vào `main()`.

| Key | Mặc định | Ý nghĩa |
|---|---|---|
| `screen_resolution` | 1920, 1080 | dùng để tính `ScreenScale` |
| `tessdata_path` | — | còn sót lại, không dùng; OCR đã chết (I5) |
| `click_hold_ms` | 25 | thời gian giữ nút chuột |
| `click_delay_ms` | 50 | thời gian nghỉ sau mỗi lần click |
| `type_char_delay_ms` | 10 | giữa mỗi lần gõ ký tự; **phải > 0** |
| `poll_interval_ms` | 15 | độ chi tiết của vòng chờ; giới hạn dưới ≥ 1 |
| `wait_timeout_ms` | 1200 | trần thời gian chờ một lần vẽ lại (redraw) |
| `menu_open_delay_ms` | 350 | **trần** thời gian chờ dropdown mở, không phải thời gian ngủ cố định |
| `page_timeout_ms` | 1200 | **trần** thời gian chờ mỗi trang, không phải thời gian ngủ cố định |
| `page_click_delay_ms` | 40 | thời gian nghỉ thêm sau khi click tab. Đã tinh chỉnh cho ping ~60ms; ~100 cho ~270ms |
| `page_quiet_clicks` | 2 | số lần click "im lặng" liên tiếp nghĩa là "danh sách đã hết"; giới hạn dưới ≥ 1 |
| `page_size` | 50 | số dòng trong một tab đầy; một tab ngắn hơn kết thúc việc lật trang. 0 = tắt tính năng này. **Đặt quá thấp là hướng nguy hiểm** — một trang đầy sẽ bị đọc nhầm là trang ngắn và phần còn lại bị bỏ mất |
| `move_ms_per_100px` | 45 | tốc độ di chuyển chuột |
| `move_min_ms` / `move_max_ms` | 150 / 700 | giới hạn thời gian di chuyển |
| `move_jitter_px` | 3 | độ cong/lắc của đường di chuyển; 0 = đường thẳng |
| `move_target_slop_px` | 3 | sai số điểm nhắm **chỉ áp dụng cho các điểm trần trụi** (I7) |
| `buy_units_per_click` | 1 | luôn để 1; giới hạn dưới ≥ 1 |
| `purchase_confirm_ms` | 1500 | thời gian chờ packet xác nhận mua từ game |
| `buy_retries` | 2 | số lần click lại khi một dòng không được xác nhận |
| `search_retries` | 3 | số lần đọc lại một danh sách đã tìm trước khi coi là "hết hàng"; giới hạn dưới ≥ 1 |
| `stray_buy_limit` | 3 | số lượt mua không xác định giá được chấp nhận cho mỗi lô |
| `budget` | 0 | chỉ dùng cho Deals/Plan; nếu nút bấm có hỏi số thì số đó được ưu tiên hơn |
| `min_profit_percent` | 20 | chỉ dùng cho Deals |
| `min_profit_silver` | 0 | ngưỡng sàn lợi nhuận tổng theo bạc cho mỗi lô, dùng cho Deals/Plan |
| `profit_combine` | and | `and`/`or`; giá trị khác sẽ giữ nguyên giá trị hiện tại |
| `max_unit_price` | 0 | 0 = không có giá trần |
| `abort_hotkey` | ctrl+shift+q | phím dừng khẩn cấp; giá trị không parse được thì giữ mặc định |

`page_timeout_ms` và `menu_open_delay_ms` là **trần cho một vòng chờ (poll)**,
không phải thời gian ngủ cố định: các menu được hash liên tục cho đến khi hết
vẽ lại, và thời gian chờ trang được suy ra từ độ trễ đo được trong lượt chạy.

---

## 7b. Phím dừng khẩn cấp

`AbortSwitch.h/.cpp`. Mặc định `Ctrl+Shift+Q`, đổi bằng `abort_hotkey`.

**Nó không thể là một key event của SFML.** Một lượt chạy chiếm event thread từ
đầu đến cuối (I4) và cửa sổ tự minimize (I3), nên không có ai bơm message queue,
và phím thì đang đi vào game. Nó được đọc bằng `GetAsyncKeyState` từ bên trong
chính các vòng lặp. Hệ quả: game cũng nhận được phím đó — đấy là lý do mặc định
là một tổ hợp chứ không phải `F12`.

**"Nhận thấy" và "hành động" là hai việc tách rời, và chính sự tách rời đó là
thiết kế.** `Abort::Requested()` chốt cứng — lần poll đầu tiên thấy tổ hợp đang
được giữ sẽ bật cờ, mọi lần gọi sau trả `true` mà không cần poll lại. Nhờ vậy
việc poll có thể nằm ở những chỗ mà dừng lại sẽ nguy hiểm, còn `break` chỉ xảy ra
ở nơi không có gì dở dang:

| Poll (chỉ chốt cờ) | Hành động (break) |
|---|---|
| `CityBuyer::WaitForPurchase` | giữa các lot, và sau khi một lot hoàn tất |
| `CityBuyer::WaitForRefresh` | giữa các band quality |
| Vòng chờ budget + settle của `PageThroughList` | **trước** mỗi click Buy, không bao giờ sau |
| | đầu mỗi lần click lật trang |
| | giữa các category, tier và mức enchant |
| | giữa các stack trong `BlackMarketSeller::Run` |

**Không bao giờ break giữa một click Buy và lúc nhận xác nhận.** Game có thể đã
xử lý click đó rồi, và bỏ đi mà không chờ câu trả lời chính là cách ledger mất
dấu hàng thật đang nằm trong túi đồ — đúng loại lỗi §9.9. Vì thế
`WaitForPurchase` chỉ chốt cờ rồi trả về bình thường; lượt chạy rời đi ở cuối lot
đó.

Hai vòng chờ chốt cờ thay vì break còn vì một lý do thứ hai: trả về sớm sẽ đưa
cho caller một **quan sát chưa từng xảy ra**. `WaitForRefresh` sẽ báo "không có
dữ liệu", và caller in ra thành "gone from the market" (§9.12); còn vòng settle
của `PageThroughList` sẽ làm một trang đầy trông như trang ngắn, và thế là phần
còn lại của listing bị bỏ trong im lặng (§5.1). Một quan sát bịa ra còn tệ hơn
việc chờ thêm một giây.

`Abort::Guard` là RAII, làm theo mẫu `WindowHidden`, và bọc quanh mọi lượt chạy
trong `Game.cpp`. `Arm()` sẽ không bắt đầu theo dõi cho tới khi tổ hợp được nhả
ra một lần, nên việc dừng lượt chạy này không thể làm chết ngay lượt chạy kế tiếp
được khởi động lúc tay còn đang giữ phím.

Dừng không phải là tạm dừng. Mọi thứ đã mua vẫn là đã mua, `holdings.Save` chạy y
hệt như sau một lượt hoàn tất, và BuyCity vẫn đưa phần chưa mua sang màn hình
`Shop` — với tiêu đề `STOPPED - NOT BOUGHT` để không thể nhầm nó với phần thừa
của một lượt chạy trọn vẹn.

---

## 7c. Bộ UI kit

`UiKit.h/.cpp`. Mọi thứ nhìn thấy được đều đi qua nó; `Theme.h` là bảng màu.

File thiết kế: https://www.figma.com/design/MKFHfQnaJVt3b2eplAkl4s — foundations,
component và ba màn đã lắp. Collection `Theme` ở đó đặt tên theo symbol C++ và
code syntax "iOS" trong Dev Mode in ra `Theme::Ok`, nên một lần đổi màu chỉ có
đúng một chỗ để hạ cánh ở mỗi bên.

**Số đo là giá trị HIG của Apple, KHÔNG phải đo từ file Apple.** File có subscribe
UI kit iOS chính thức, nhưng plan Starter từ chối `importComponentByKeyAsync`
(`Not permitted to upsert from library`), nên không đo được gì từ nó. Hàng 44px,
inset ngoài 20, inset trong hàng 16, nav 44, chip 206×32 bước 42px, bo group 12.

**Hai thứ sẽ cắn bất kỳ ai sửa file Figma.** Paint do `setBoundVariableForPaint`
trả về bị đóng băng, nên gán `.opacity` lên nó không có tác dụng; phải đặt opacity
vào paint đưa VÀO. Và instance **không giữ được opacity của paint** chút nào. Cả
hai đã tạo ra khối màu đặc mất chữ, hai lần, và chỉ ảnh chụp mới bắt được. Giờ
mọi tint là một variable ĐẶC tính sẵn — cũng là thứ SFML cần, vì nó không có mô
hình compositing nào để làm mất.

**Font.** Inter, bốn weight tĩnh, ship kèm theo giấy phép SIL OFL. SFML nạp một
face cho mỗi weight; không có trục variable-font nào để với tới, đó là lý do
`SegUIVar.ttf` của Windows bị loại. `Ui::LoadFonts` rơi về font mono cho weight
nào nạp hỏng, thay vì không vẽ gì.

---

## 8. Toạ độ (nền 1920×1080, nhận biết DPI)

Định dạng `x1,y1-x2,y2`. Click rơi vào một **điểm ngẫu nhiên bên trong**.

```
Scanner                 MarketScanner.h
  typeButton     759,268-875,278    mở dropdown loại item
  typeList       759,268-876,502    được hash trong lúc menu cấp trên mở
  subList        889,323-1007,890   được hash trong lúc submenu mở
  resetTabs     1334,271-1336,273   dọn các tab đang mở trước một category
  nextPage       986,882-997,892    lật trang kết quả

Dropdown dùng chung        MarketUi.h
  tier button    903,268-1015,275   chỉ cao 7px — xem I7
  tier rows      904,401 .. 901,521 CHỈ T4..T8. T1-T3 không được đo và
                                    Row() trả về NULL thay vì tự suy ra
  enchant button 1050,268-1152,276
  enchant rows   1052,320 .. 1050,442  E0..E4; "All" cố tình không có —
                                    chọn nó sẽ tạo ra danh sách quá dài mà
                                    bộ lọc này tồn tại để tránh
  quality button 1192,266-1300,278
  quality rows   1192,319 .. 1197,438  Normal..Masterpiece

Mua ở City             CityBuyer.h
  search         586,264-697,277    gõ "Boltcasters T4.2"
  buyFirstRow   1232,424-1314,440   dòng duy nhất được click. Cạnh dưới
                                 được nâng lên từ 442; hộp cũ đè lên nút
                                 2px và các click rơi vào đó bị mất — xem
                                 chú thích trong CityBuyer.h
  amountField    567,570-583,578    chỉ chạm tới khi take > 1
  confirmBuy     844,722-922,739
  levelWarning   819,545-899,555    "may not be able to use"; trống khi không có
  clear search   722,270            ClearItemName()

Bán ở Black Market    BlackMarket.h
  search         586,264-697,277
  sell          1238,427-1323,442
  confirmSell    848,725-919,735    luôn xuất hiện, luôn được click
```

**`levelWarning` — không mở rộng xuống quá y=558.** Dải 8px tại
`830,566–866,585` từng được đánh dấu bằng tay là không an toàn để click. Như đã
đo (558 so với 566), không cần sửa gì; ghi chú này tồn tại để một chỉnh sửa
tương lai kiểu "mở rộng target thêm một chút" không vô tình đi vào vùng đó mà
không biết.

### Menu category — `CategoryLayout`, 46 mục

Cả hai cột đều vẽ theo một **bước cố định 27px** bất kể nội dung. Đó là lý do
toàn bộ bảng là một lưới thống nhất thay vì 46 lần đo độc lập, và vì sao Capes
có thể được suy ra rồi kiểm tra lại bằng screenshot.

```
dòng cha:     Weapons 324 · Chest 351 · Head 378 · Foot 405
              Off-Hands 432 · Capes 459 · Bags 486
submenu:      dòng "All" của mỗi submenu thẳng hàng với dòng cha của nó;
              các dòng thật bắt đầu một bước lưới bên dưới
ô chọn:        x 790-845 (trái), 920-995 (phải); y = tâm ±2
```

**Các ô chọn rộng theo x nhưng hẹp theo y, và sự bất đối xứng đó là có chủ đích.**
Bảng ban đầu là `2×2` px, mà `ClickInside` sẽ thu nhỏ xuống thành chín pixel —
thực chất là một điểm nhắm cố định trên danh nghĩa. Lý do được nêu ra là bước
lưới 27px, nhưng điều đó chỉ giới hạn **y**: các cột rộng ~130px và không có gì
nằm cạnh một dòng để bị click nhầm.

Chỉ mở rộng các ô theo x mà không sửa gì khác thì không thay đổi được gì, vì
`ChooseCategory` từng hover vào **đúng tâm** của hình chữ nhật cha thông qua
`MoveToScreenPoint` với toạ độ mà nơi gọi đã lấy trung bình thành một điểm duy
nhất từ trước. `MoveToRect` được tạo ra để giải quyết đúng việc đó (§9.19).

Dòng sâu nhất là `capes_smuggler` ở y=864, đó là lý do `subList` phải kéo dài
tới 890. `capes_demon` (837) và `capes_smuggler` (864) là **những toạ độ được
suy ra vượt xa dòng sâu nhất từng được đo thực tế** (783,
`weapons_shapeshifterstaff`). Chúng được kiểm tra bằng screenshot, không phải
bằng `FindCoords.ps1`.

---

## 9. Lịch sử các lỗi đã gặp

Mỗi mục: hiện tượng trông như thế nào, thực chất là gì, cách sửa, nguyên tắc
bất biến liên quan.

### 9.1 Binary GUI âm thầm bỏ toàn bộ output
`SubSystem` là Console→Windows. `CityBuyer::PrintPlan` dùng `printf`; chỉ
`std::cout` mới được chuyển hướng vào panel. Toàn bộ bảng kế hoạch được format
xong rồi bị bỏ đi, và ba lệnh `printf("SendInput failed…")` đã nuốt mất chính
thông báo cho biết một click chưa từng tới được game.
**Cách sửa:** dùng `snprintf` vào một buffer, rồi `cout << buffer` (giữ được
căn cột). → I1

### 9.2 `system("cls")` xuất hiện 13 lần
Bị phát hiện trước khi phát hành. Mỗi lần gọi sẽ mở một `cmd.exe` — một cửa sổ
console nhấp nháy mỗi lần bấm nút, đúng thứ mà thay đổi này muốn loại bỏ.
**Cách sửa:** `GlobalLog.Clear()`. → I2

### 9.3 Cửa sổ tự đọc nhầm chính pixel của mình
Cửa sổ lớn lên từ 500×500 thành 900×680. Vùng đọc khoảng x 759–1020, y
268–521 theo toạ độ *màn hình*; một cửa sổ 900×680 nằm giữa màn hình 1920×1080
trải từ x 510–1410, y 200–880 và che phủ hoàn toàn vùng đó. Các lượt scan báo
thành công nhưng trả về 0 order, vì bot đang đọc chính danh sách category của
nó.
**Cách sửa:** khoá RAII `WindowHidden`, có scope sao cho một `return` sớm vẫn
khôi phục lại được. Áp dụng cho cả scan, mua **và** bán. → I3

### 9.4 Đưa scan vào một thread riêng
Scan từng được chuyển vào `std::thread` để thanh tiến trình có thể chạy
animation. Nghi ngờ gây ra bất thường trong thời gian di chuyển chuột; đã revert
lại. Một render loop 60fps chạy cạnh `SendInput` theo nhịp `Sleep()` không đáng
đánh đổi.
**Chi phí đã chấp nhận:** cửa sổ bị đứng hình trong lúc scan và thanh tiến trình
chỉ hiện trạng thái cuối cùng — vốn cũng vô hình vì cửa sổ đang bị minimize. → I4

### 9.5 "albiondata-client is running" không chứng minh được điều gì
Các lượt scan trả về 0 order. `EnsureAlbionDataClientRunning()` chỉ kiểm tra có
tồn tại một process tên như vậy hay không. Một instance cũ — chạy **không có**
`-p http://127.0.0.1:5000` — thoả điều kiện đó hoàn hảo trong khi gửi dữ liệu đi
nơi khác.

Đã được chứng minh, không phải đoán: (1) timestamp của `ocr_miss.png` cũ →
reader chưa từng đọc trượt; (2) không có dòng "could not find" nào trong
`bot.log`; (3) `netstat` cho thấy app đang LISTENING ở cổng 5000; (4) một POST
giả lập một order giả trả về HTTP 200 → toàn bộ đường nhận hoạt động; (5) vậy
chỉ còn phần gửi là có vấn đề. `adc.log` xác nhận instance do app khởi động đã
tắt.

**Nguyên nhân gốc:** Albion Data Client được cài sẵn thông thường trên máy đã
đăng ký một **Scheduled Task của Windows** (`Albion Data Client`, tham số
`-minimize`, không có `-p`), tự chạy lúc đăng nhập từ
`C:\Program Files\…` — khác đường dẫn với bản copy của bot. `LastRunTime` khớp
chính xác với process "lạ" đó.

**Cách sửa, bốn lớp:** kill trong `run-feed.bat` (chạy elevated; một process
không elevated không thể kill một process elevated); luôn giành quyền sở hữu
thay vì bỏ qua; thêm nút **Capture** cùng khả năng tự chữa của `LaunchScan` khi
`MessagesSeen() == 0` và capture chưa được khởi động lại trong 60 giây gần
nhất; tắt scheduled task đó.

**Bài học:** khi một dependency bên ngoài có thể bị người khác/thứ khác khởi
động, không nên chỉ *phát hiện* nó — phải **sở hữu** nó. Kiểm tra các nơi tự
khởi động của OS (Run keys, Startup, Services, **Scheduled Tasks**) trước khi
gọi một process là không thể giải thích được. Và kiểm tra đầu nhận trước tiên,
toàn diện: việc này rẻ và cho kết quả dứt khoát.

### 9.6 Suýt xoá nhầm khi xoá hàng loạt
Đang xoá thế hệ database giá dùng OCR cũ (`DataBase`, `Item`, `Items`,
`MarketAction`). Một công cụ audit dependency tự động xếp nhầm `ClearItemName()`
là chỉ thuộc code cũ; thực tế nó được gọi bởi `CityBuyer::SearchFor`, nằm trên
đường mua hàng đang sống. Ngoài ra: `LOG_DURATION` từng chạm được tới `Game.cpp`
*gián tiếp* qua `MarketAction.h` (đã bị xoá); thêm `#include "profile.h"` sửa
được lỗi build nhưng lại tạo ra một sự mập mờ giữa `sf::milliseconds` và
`std::chrono::milliseconds` (`profile.h` dùng `using namespace std::chrono`,
`MyImg.h` dùng `using namespace sf`). Giải quyết bằng cách include `profile.h`
**trước**. → I6

### 9.7 Log panel che mất các nút bấm
Mở rộng panel lên 300px đã che mất `BACK` và thanh tiến trình.
**Cách sửa:** `BACK` được cố định ở góc trên-phải mọi màn hình, cửa sổ mở rộng
lên 820px, panel được cố định bên dưới dòng điều khiển cuối cùng.

### 9.8 Vài lỗi nhỏ
- **`EnterWndBuy` chưa từng được vẽ** — không có nhánh trong `render()`. Có từ
  trước, không lộ ra cho tới khi BuyCity được dùng thật. → quy tắc "sửa ba nơi"
  ở §3.
- **Bất kỳ click lạc nào cũng xoá log** — `MenuConroller` gọi
  `GlobalLog.Clear()` trước khi kiểm tra có nút nào bị bấm hay không.
- **Chạy trực tiếp file .exe bị lỗi** — mọi thứ load theo đường dẫn tương đối.
  `main()` giờ tự suy ra thư mục từ đường dẫn của file thực thi.
- **DLL không bao giờ được copy** — file .exe chỉ chạy được qua `START.bat`
  hoặc từ IDE. Một bước sau khi build sẽ copy DLL của SFML/OpenCV/vcpkg vào
  cạnh nó. **Windows loader phân giải các import trước khi `main()` chạy, nên
  không có cách sửa `PATH` nào trong chương trình có thể hoạt động.**

### 9.9 "purchase did not register" — năm thứ hoá ra là năm dòng khác nhau
Mỗi lần chạy BuyCity: `Buying 5x Satchel of Insight` → `purchase did not
register` → `0 units in 0/2 lots, spent 0 silver`.

Thực tế trên màn hình là: năm **listing riêng biệt, mỗi listing một đơn vị**, từ
năm người bán khác nhau. Đây là hình dạng bình thường của một danh sách thị
trường.

Có hai lỗi ẩn sau một thông báo:
1. **Ô số lượng.** Hộp thoại mua chỉ mở thêm ô số lượng khi dòng đó có nhiều
   hơn một đơn vị. `BuyTopRow` luôn gõ số lượng khi `amount > 1`; với một dòng
   chỉ có một đơn vị, các ký tự gõ vào rơi vào nơi khác và làm hỏng mọi click
   sau đó.
2. **Cách kiểm tra thành công chỉ là một phép đoán.** Câu hỏi "danh sách có
   ngắn lại không?" cần game tự gửi lại request sau mỗi lần mua. Khi game
   không làm vậy, một lượt mua thành công sẽ bị đọc thành thất bại — và code
   đã bỏ cả lô hàng chỉ vì cách đọc đó. Tệ hơn, `refreshLive()` xoá danh sách
   trước rồi nạp lại từ bất cứ gì lần drain tiếp theo giữ được, nên một batch
   không liên quan có thể làm rỗng nó và báo "gone from the market" về những
   món hàng vẫn đang hiển thị rõ ràng trên màn hình.

**Cách sửa:** `buy_units_per_click = 1`, để hình dạng của hộp thoại không còn
quan trọng nữa; và game được *hỏi thẳng* thay vì bị đoán — `AuctionBuyOffer`
mang theo order id và số lượng, đây một phần là lý do AlbionFeed tồn tại.
`pullLive()` chỉ thay danh sách đang dùng khi batch đó thực sự chứa offer của
đúng item đang cần. → I10

### 9.10 OCR bị loại khỏi tier/quality, rồi loại khỏi mọi thứ
Tesseract đọc "Tier 5" thành "Tier S" thường xuyên đến mức cả tier bị bỏ qua
trong im lặng, và vòng lặp thử lại chỉ tồn tại để che đi những lần đọc sai đó.
Các dòng không bao giờ di chuyển, nên chẳng có gì để "phát hiện" cả. Menu
category cũng bị loại OCR vì lý do tương tự — cả hai cột đều nằm trên một lưới
27px cố định.
**Chi phí đã chấp nhận:** một lần click tier không còn được xác minh lại được.
Nếu dropdown không mở, lượt quét đó sẽ chạy với tier đang được chọn từ trước;
số lượng order in ra theo từng tier bởi `ScanCategory` là thứ giúp lộ ra vấn đề
này. → I5

### 9.11 `Elsewhere` từng làm bỏ cả lô hàng
Bằng chứng từ log: kế hoạch 21.999.567 bạc / 33 lô → **thực tế chỉ chi
2.390.569 (10.9%)**. Mười lô chỉ mua đúng **một** đơn vị rồi dừng, với lý do
`bought order X rather than Y - the list shifted under the click, stopping
this item`.

**Nguyên nhân:** một người chơi khác lấy mất dòng đang nhắm tới là "thời tiết"
bình thường của một thị trường sôi động. Lý do được đưa ra để dừng — "giá của
nó chưa từng được kiểm tra so với margin" — chỉ đúng khi bot *không thể* định
giá nó, và thường thì bot hoàn toàn có thể: dòng đó vẫn nằm trong danh sách mà
bot đang giữ.

**Cách sửa:** định giá nó. Trong `maxBuyPrice` → ghi lại, đọc lại, tiếp tục.
Vượt quá → vi phạm thật, dừng lại. Không định giá được → tính đếm, giới hạn ở
`stray_buy_limit`.

**Kết quả, lần chạy tiếp theo:** 47 đơn vị trong **13/13** lô, 17.825.223 trên
tổng 17.979.908 dự kiến = **99.1%**. 34 đơn vị đi qua nhánh `Elsewhere` — mỗi
đơn vị trong số đó lẽ ra đã bị bỏ đi bởi code cũ.

### 9.12 "gone from the market" trong khi hàng vẫn hiện rõ trên màn hình
**19 trên 33 lô** trong một lượt chạy, cộng thêm 1 lần
`no fresh data after the search`.

**Nguyên nhân:** `SearchFor` gọi `ClearItemName()`, và click đó làm game gửi
lại toàn bộ danh sách **chưa lọc** (bộ lọc quality được đặt ngay trước đó cũng
gây ra điều tương tự). `mark` của nơi gọi được lấy *trước* các click đó, nên
`WaitForRefresh` bị thoả mãn ngay lập tức bởi những packet sai, và lần drain
tiếp theo trả về đúng danh sách chưa lọc đó. Không có gì trong đó khớp với item
cần tìm, và một lần drain không thử lại đã gọi đó là "hết hàng".

**Cách sửa:** `SearchFor` bỏ các order cũ đi và báo lại vị trí của feed *sau*
chúng (`mark` trở thành một out-parameter, đây là lý do signature của hàm thay
đổi); việc đọc lại được thử tối đa `search_retries` lần; log phân biệt rõ
"không có dữ liệu nào cả" (search chưa từng tới được game) với "có dữ liệu,
nhưng không phải item này".

**Kết quả:** 19 → **0**. → I10

### 9.13 Deals từng thổi phồng lợi nhuận vì tính một order cho mỗi band chất lượng
Khoá nhóm là `(item, enchant, quality)`, nhưng mỗi nhóm lại lấy toàn bộ
`amount` của cùng một order Black Market. City thường list một item ở ba hoặc
bốn mức chất lượng và tất cả đều có thể khớp vào một order chất lượng thấp.

Đã tái hiện được trong `PlanTest`: một bid 10 đơn vị đối đầu với ba band cung
cấp khác chất lượng → kế hoạch tính ra **30 đơn vị / lợi nhuận 1.350.000**,
trong khi trần thực tế chỉ là **10 đơn vị / 460.000**. **Sai lệch 2.9 lần.**
Log thật cho thấy `Cleric Sandals q1/q2/q3` mỗi cái đều được lập kế hoạch cho
11 đơn vị nhưng chỉ đối đầu với một order duy nhất 11 đơn vị.

**Cách sửa:** coi cầu là một bể hữu hạn với `left` cho từng order; các band
cạnh tranh nhau để giành lấy nó, band cung cấp rẻ nhất giành trước (§6 stage 2).

### 9.14 Chỉ dùng đúng một bid đắt nhất
`BestBuyerFor` từng chỉ trả về một order giá cao nhất duy nhất và bỏ hết phần
còn lại, nên năm order đứng sẵn mỗi order 5 đơn vị bị đọc thành cầu chỉ cho 5
đơn vị.
**Cách sửa:** tất cả order được đưa vào bể, xếp giá giảm dần. `PlanTest`: hai
bid × 5 đơn vị giờ tính ra kế hoạch 10 đơn vị, không phải 5.

### 9.15 Cắt theo ngân sách tính sai giá phần còn lại
Một lô bị cắt sẽ giữ lại các đơn vị rẻ nhất, những đơn vị này ghép với các order
đắt nhất, nhưng lợi nhuận lại được tính lại từ giá `bmPrice` trung bình của cả
lô.
**Cách sửa:** tính lại giá từ `revenueLadder`; đồng thời áp lại gate lợi nhuận
cho lô đã cắt và bỏ qua nó (không dừng hẳn) nếu nó không còn đạt điều kiện.

### 9.16 Nhập số bị giới hạn ở khoảng 1 tỷ
`MAX_ENTERED_DIGITS = 9` dùng `stoi`. Một nhân vật giàu có thể chạm giới hạn
này.
**Cách sửa:** 12 chữ số, `stoll`, dùng `long long` xuyên suốt.

### 9.17 `Plan` từng chỉ là một bản sao của `Deals`
`Plan` từng chỉ hỏi một con số phần trăm và lấy ngân sách, sàn lợi nhuận bạc,
AND/OR từ `Config.txt`. Với `budget: 0` mặc định, nó tính ra **chính xác** cùng
một danh sách như `Deals`, và không có cách nào để xem thử một ngưỡng cụ thể mà
không phải sửa `Config.txt` hoặc tốn tiền thật để biết kết quả.
**Cách sửa:** `Plan` đi qua đúng bốn màn hình giống `BuyCity`, chỉ khác nhau ở
một cờ duy nhất `pendingBuyDryRun`. Một bản xem thử mà hỏi ít câu hơn lượt chạy
thật mà nó đang xem thử thì đang xem thử một danh sách khác.

### 9.18 Scanner tự xác nhận lại một sự im lặng mà nó đã biết câu trả lời rồi
Quét một tier cao chỉ có đúng một item vẫn tốn `page_quiet_clicks` ×
`page_timeout_ms` chỉ để xác nhận rằng danh sách đã kết thúc.
**Cách sửa:** `page_size` (§5.1). Yêu cầu "settle trước khi đếm" là toàn bộ rủi
ro của thay đổi này và đã được ghi chú ngay tại đó.

### 9.19 Hover category là chuyển động duy nhất dùng toạ độ cố định
Các ô chọn có kích thước 2×2, và bước hover đã bỏ qua hoàn toàn `ClickInside`
bằng cách lấy trung bình hình chữ nhật thành đúng tâm của nó — nên chỉ mở rộng
các ô theo x thì sẽ không thay đổi được gì.
**Cách sửa:** tách `PointInside` ra khỏi `ClickInside`; thêm hàm mới
`MoveToRect(rect, holdRowY = -1)`; mở rộng các ô theo x cho toàn bộ 46 mục.

### 9.21 Lượt mua từng từ chối một sự thay thế mà chính thị trường cho phép
Black market nhận bất cứ thứ gì có chất lượng **bằng hoặc cao hơn** mức nó yêu
cầu, và `Plan` đã khai thác điều đó từ trước: các band cạnh tranh nhau để giành
cầu và band rẻ nhất thắng, nên một lô cho một order Good thường được lập kế
hoạch để đối đầu với một Outstanding rẻ hơn.

`Run` sau đó lại bỏ mất điều đó. Nó khoá cứng vào đúng một band đã lập kế hoạch
(`SelectQuality(lot.quality)`, và `pullLive` loại bỏ mọi chất lượng khác), với
`maxBuyPrice` của riêng band đó làm giá trần. Vậy nên một lô được lập kế hoạch
đối đầu với một Outstanding rẻ mà bị người khác mua hết sẽ bị bỏ hoàn toàn — dù
ngay trên cùng màn hình vẫn còn Good, cái mà nó vốn đang mua rẻ hơn, với mức
giá margin vẫn đủ đạt yêu cầu. Cả lô hàng bị mất chỉ vì một sự thay thế mà chính
game coi là hợp lệ.

**Cách sửa, gồm ba phần:**
1. `FlipLot::qualityOrder` — mọi band có thể khớp các order của lô, band cung
   cấp rẻ nhất đã lập kế hoạch đứng trước (§6 stage 2b).
2. `FlipLot::marginCeiling` — một giá trần vẫn còn hợp lệ khi chuyển sang một
   band đắt hơn, vì `maxBuyPrice` về bản chất không thể làm được điều đó.
3. `Run` đi qua nhiều band thay vì chỉ một, và **quét lại** chúng khi một lượt
   vẫn còn đang mua (`MAX_BAND_PASSES`, cộng thêm điều kiện dừng khi không có
   tiến triển). Một band bị bỏ vì đạt giá trần có thể có gì đó rẻ mới được list
   trong khi một band khác đang được xử lý, và chính dòng đó nên được lấy ở
   lượt tiếp theo.

Ledger ghi lại band **thực sự** đã mua, không phải band đã lập kế hoạch —
`holdings.tsv` đưa sai chất lượng cho `BlackMarketSeller` sẽ làm nó tìm một
stack không tồn tại.

Trong phạm vi một band duy nhất, điều này vốn đã hoạt động đúng và vẫn vậy:
`pullLive()` đọc lại và sắp xếp lại sau mỗi lần mua, nên một món hời mới vừa
được list sẽ trở thành `live.front()` và được lấy ở lượt kế tiếp.

`PlanTest` kiểm tra phần lập kế hoạch: một order q2 đối đầu với
q2\@60000 / q3\@50000 / q4\@55000 sẽ lập kế hoạch cho q3 và mang theo
`q3 q4 q2` với giá trần 80000; một order q4 sẽ không bao giờ liệt kê q1/q2 rẻ
vì chúng không thể khớp được order đó.

**Chạy thật, và nó đã sửa được hai điều.** Vòng quét qua các band chạy đúng như
thiết kế (`q1 -> q2 -> q3 -> q4` trên một lô Bridgewatch Cape, và lượt thứ hai
đúng đắn không chạy vì lượt đầu không mua được gì). Nhưng:

- **Mọi lần một band thất bại đều bị im lặng.** Bốn band đã thử, rồi
  `0/29 units`, không có cách nào biết được liệu thị trường thực sự không có
  hàng hay mọi thứ đang bán đều quá đắt. Việc biến một lần band thất bại thành
  "không chết chương trình" cũng vô tình làm nó "không được ghi log" —
  đúng kiểu lỗi I10, khi thông báo bị xoá hẳn chứ không chỉ là sai. Giờ mỗi
  band sẽ báo `nothing listed`, `cheapest X is over the Y ceiling`, hoặc
  `bought out while working it`.
- **Giá trần từng nằm ở band sai.** Band đã lập kế hoạch giữ `maxBuyPrice`
  (142988) trong khi các band dự phòng lại nhận `marginCeiling` (146509), nên
  bot sẽ từ chối một Good giá 145000 nhưng lại chấp nhận một Outstanding cùng
  giá đó — trong khi Good chính là chất lượng mà order thực sự yêu cầu. Giờ
  dùng `max(maxBuyPrice, marginCeiling)` cho mọi nơi: cả hai đều là mức giá hợp
  lý, và giới hạn giống nhau ở mọi band.

Lưu ý là khoảng "dư địa" chỉ rộng bằng đúng mức margin cho phép. Với yêu cầu
10% trên một bid 167875, `marginCeiling` là 146509 so với mức đã lập kế hoạch
142988 — chỉ 2.5% dư địa. Band dự phòng chỉ cứu được một lô khi spread còn để
lại đủ khoảng để làm việc.

### 9.22 Mọi cú click vào category ngừng ăn, trong im lặng

Đợt làm lại UI đã chuyển định tuyến click ra khỏi nhãn hiển thị: hit test bắt
đầu trả về `cat:<key>` và mỗi hàng bắt đầu mang một `command` id. Nhưng các
controller không được sửa theo — `ScanController` và `SettingsController` vẫn so
`row.menuSub == command`, thứ mà một key có tiền tố không bao giờ bằng.

Không có lỗi nào được báo. Một command không khớp là một no-op, nên click vào
category không làm gì cả, và vì không chọn được gì nên `SCAN BM` chỉ có thể trả
lời "Nothing to scan". Màn hình đơn giản là không phản ứng, và build thì sạch.

Hai thứ làm nó tệ hơn một lỗi thông thường. Nó chính là cái thất bại mà tiền tố
key sinh ra để ngăn, đạt được bằng cách sửa một đầu mà không sửa đầu kia. Và
nó vô hình với `UiPreview`, thứ vẽ qua `UiKit` và không bao giờ định tuyến một
event — mọi ảnh chụp đều đúng.

Cách sửa không chỉ là hai phép so sánh. Cả hai đầu giờ đi qua
`Ui::CategoryCommand` / `Ui::CategoryKey`, nên tiền tố chỉ được viết một lần.
Hai hàm không thể lệch nhau theo kiểu hai chuỗi ký tự có thể.

**Quy tắc chung:** khi một hit test và controller của nó được sửa riêng rẽ,
không có gì trong trình biên dịch hay test nối chúng lại. Nếu từ vựng giữa
chúng là một chuỗi, hãy đặt nó sau một hàm mà cả hai cùng gọi.

### 9.20 CHƯA XONG — vòng lặp mua tự đuổi theo chính cái đuôi của nó
**Không phải một lỗi đã sửa. Một hành vi đã biết, đã đo, và được chấp nhận.**
Trong lượt chạy 47 đơn vị, order id được nhắm tới ở lượt *N+1* **luôn luôn
chính xác** là id đã mua ở lượt N, đúng 34 trên 34 lần.

`pullLive()` thực hiện `live.swap(fresh)`, việc này **hoàn tác** lại
`live.erase(live.begin())` cục bộ từ lần mua vừa thực hiện — danh sách game gửi
lại vẫn còn chứa dòng đã mua đó. Nên bot nhắm vào một dòng đã biến mất và click
rơi vào dòng kế tiếp bên dưới.

Chi phí đã đo được: **không mất đơn vị nào** (mọi lô đều đạt đúng số lượng kế
hoạch) và lệch **0.10%** số bạc so với kế hoạch. Rủi ro còn lại: nếu một lần
refresh không đầy đủ nào đó khiến id đã mua biến mất khỏi `live`, thì
`paid == 0`, và ba lần như vậy sẽ làm dừng cả một lô — **0 lần xảy ra trong 34
mẫu**, có lý do cấu trúc để nó hiếm (danh sách cũ chứa các dòng *thừa*, không
phải các dòng bị thiếu).

Cố tình chưa sửa: cách sửa đúng đắn cần theo dõi mức tiêu thụ theo từng id, và
còn phải phát hiện được khi nào server *đã* ghi nhận một lượt mua, và nó nằm
trong `CityBuyer::Run` — vòng lặp duy nhất chưa có test coverage. Đánh đổi một
rủi ro nhỏ đã đo được để lấy một rủi ro không đo được là một sự thua lỗ.
**Chỉ sửa khi `at a price not in the listing` thực sự xuất hiện trong log.**

Log đã được đổi thay vào đó: trường hợp bình thường được đếm, không được thông
báo, và chỉ báo cáo một lần cho mỗi lô. 34 dòng trông như cảnh báo cho một hoạt
động bình thường sẽ tập cho người đọc quen với việc bỏ qua đúng hai dòng thực
sự quan trọng.

---

## 10. Các cái "bẫy" kế thừa từ trước

**DPI.** Máy chạy 1920×1080 ở 125%. Một process *không nhận biết* DPI sẽ thấy
một desktop giả 1536×864; `SetCursorPos` bị scale sai và `BitBlt` trả về một
ảnh mờ bị thu nhỏ. `main()` gọi
`SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` trước khi bất kỳ cửa sổ
nào được tạo. `FindCoords.ps1` in ra `Physical screen: 1920 x 1080` như một
cách tự kiểm tra — nếu nó in ra 1536×864, mọi thứ đo bằng nó đều lệch 25%.

**`tessedit_char_whitelist` âm thầm làm hỏng OCR.** LSTM của Tesseract 4/5 trả
về một **chuỗi rỗng** thay vì đoán tốt nhất khi có whitelist được đặt. Nên lọc
*sau khi* nhận diện xong. `SquashNumeric()` gộp S→5, O→0, B→8 và **chỉ** áp
dụng cho chữ số — nếu áp dụng toàn cục nó sẽ biến "Swords" thành "5word5".
(Còn sót lại: OCR đã chết, I5.)

**File nguồn từng ở mã CP1251** (comment tiếng Nga của tác giả gốc). Tất cả đã
được chuyển sang **UTF-8 kèm BOM**. Các chỉnh sửa bằng script phải giữ nguyên
điều đó.

**`sf::Text` vẽ `std::string` theo Latin-1, không phải UTF-8.** Mọi nhãn trên
UI đều là tiếng Anh vì lý do này; nhãn tiếng Việt cần chuyển đổi rõ ràng từ
UTF-8 → UTF-32.

**Submenu mở khi hover, không phải khi click.** Click vào mục cha sẽ làm menu
đóng lại.

---

## 11. Trạng thái hiện tại

### Đã kiểm chứng mà không cần mở game
- `MSBuild SFML.sln /p:Configuration=Release /p:Platform=x64` — build lại sạch,
  chỉ còn các warning đã có từ trước (hai C4244 trong `Game.cpp`, một C4267
  trong `Shapes.cpp`)
- `dotnet build -c Release` cho AlbionFeed
- `PlanTest` **38/38** — gate tier, xếp hạng theo lợi nhuận giảm dần (T8 69600
  đứng trước T6 38000, tức là *không* theo mỗi-đơn-vị-bạc), lỗi đếm đôi
  quality, cầu từ nhiều order, tính lại giá khi cắt theo ngân sách, ngữ nghĩa
  AND/OR, sàn lợi nhuận bạc sau khi cắt, và danh sách dự phòng chất lượng
  (ưu tiên chất lượng cao mà rẻ hơn, loại band không thể khớp order, giá trần
  margin cao hơn maxBuyPrice)
- `CategoryTest` **10/10** — toạ độ ↔ tsv theo cả hai hướng, độ dài bảng, không
  có hai category cùng một dòng menu, `menu_sub` là duy nhất, lưới 27px, hình
  dạng ô chọn, độ sâu `subList`. **Đã kiểm thử bằng mutation testing**: một
  dòng được cài cắm không có toạ độ và một nhãn bị trùng lặp sẽ làm ba kiểm
  tra thất bại và exit code 1
- `WipeTest` — 8515 byte của `bot.log` → 0, dòng tiếp theo được ghi là dòng số 1
- Toàn bộ UI, render ra PNG bằng `UiPreview` và đã nhìn tận mắt: cả sáu màn,
  46 category thật trong bốn cột, năm trạng thái scan phân biệt được tức thì,
  bảng plan vẫn thẳng hàng trong log panel monospace
- `SFML.exe` build với **zero warning** — hai cảnh báo C4244 tồn từ trước trong
  `updateMousePos` cũng đã hết
- `ShopListTest` **26/26** — các lot cùng một chuỗi tìm kiếm gộp thành một block
  và số lượng/lợi nhuận cộng đúng; block gộp hiện quality thấp nhất và giá trần
  thấp nhất; xếp hạng theo tổng của block, nên hai lot vừa phải của một món
  đứng trên một lot béo hơn của món khác; `Shortfall` tính lại giá từ đầu ĐẮT
  của ladder offer và đầu RẺ của ladder order, và giữ nguyên `marginCeiling`
- Parse `abort_hotkey`, đối chiếu với `Config.txt` thật: `ctrl+shift+q` → vk
  0x51, `pause` → 0x13, `f9` → 0x78, `alt+end` → 0x23; `ctrl+shift`, `banana`,
  `f13` và `ctrl+q+w` đều bị từ chối và giữ mặc định. Kiểm chứng dương tính
  bằng cách sửa file thật thành `alt+f8` và thấy giá trị parse đổi theo
- `MarketFeed` parse đúng cả hai hình dạng message (POST giả lập bằng tay)
- Tính toán bố cục UI cho 46 category (§12)

### Đã kiểm chứng trong lượt chạy thật với game
- **Giao diện mới, đang chạy.** Menu vẽ đúng với client rect thật 900×820,
  hover sáng đúng hàng dưới con trỏ, log panel mang các dòng khởi động ở font
  mono. Bấm `Shop` và `Plan` cho ra đúng thông báo riêng của chúng trong
  `bot.log` — đó là bằng chứng định tuyến mới hoạt động: mỗi hàng mang một
  command id và nhãn không bao giờ được đem ra so sánh.
- Một thứ chỉ lần chạy thật mới bắt được: `InitVersion` trước đây được gọi
  gián tiếp ở dòng cuối của `InitTextVar`, nên việc bỏ đối tượng chữ FPS đã kéo
  luôn `version = new Text` đi theo, và frame đầu tiên dereference một con trỏ
  rác. Cửa sổ hiện ra rồi tiến trình chết. `UiPreview` vẽ qua `UiKit` và không
  đụng tới `Game`, nên nó không thể tìm ra lỗi này.
- Toàn bộ đường mua hàng từ đầu đến cuối: **47 đơn vị, 13/13 lô, 17.825.223
  bạc, 99.1% so với kế hoạch, 0 lần `gone from the market`** — đây là bằng
  chứng cho §9.11 và §9.12
- Capes, đã scan và mua: `Avalonian Cape T6.0` 45/45 và `Demon Cape T6.1`
  30/30. Các dòng submenu của chúng (810 và 837) được **suy ra từ một
  screenshot và nằm sau dòng sâu nhất từng được đo thực tế** (783), việc này
  xác nhận luôn cả cách suy luận theo lưới 27px, không chỉ riêng các category
  đó
- Phát hiện trang ngắn bằng `page_size`: **40 lần kích hoạt** trong một lượt
  scan, với kiểm tra click-im-lặng vẫn xử lý 35 trường hợp còn lại (một tab
  có đúng 50 dòng, hoặc một tab rỗng) — hai đường này bổ trợ cho nhau, không
  cạnh tranh
- Hover category theo kiểu ngẫu nhiên (§9.19) — mọi category cape đều được
  chọn đúng
- Danh sách dự phòng theo band chất lượng đã kích hoạt đúng (§9.21)

### Chưa được kiểm chứng — cần mở game để kiểm tra
- **Phím dừng khẩn cấp chưa từng được bấm trong một lượt chạy thật.**
- **Màn hình `Shop` chưa từng được vẽ ra lần nào.** Nó build được và phần số
  học đã có test phủ, nhưng chưa ai nhìn thấy nó: hình học của block, chữ có
  lọt trong 900px không, việc cuộn, việc copy clipboard và cái nháy COPIED —
  tất cả đều chưa được quan sát
- **Phím dừng khẩn cấp chưa từng được bấm trong một lượt chạy thật.** Phần
  parse và phần latch đã có kiểm chứng; cái chưa có là `Ctrl+Shift+Q` có thật
  sự vô hại trong Albion không, và một lượt chạy mất bao lâu để tới được
  checkpoint kế tiếp
- Nửa phần **quét lại (re-sweep)** của §9.21 (chưa có trường hợp nào cần lượt
  quét thứ hai chạy)
- `capes_smuggler` (864), toạ độ suy luận duy nhất còn chưa được chứng minh
- Một lượt SellBM chạy đầy đủ
- Hover category ngẫu nhiên (§9.19) đối chiếu với menu thật
- Một lượt SellBM chạy đầy đủ
- Liệu các mốc thời gian của menu có còn đúng vào một ngày mạng lag không

### Code chết, cố tình giữ lại
- `Ocr.h/.cpp` — không ai gọi. Toàn bộ phần tesseract/vcpkg trong `BUILD.md`
  chỉ tồn tại vì hai file này. **Xoá chúng sẽ loại bỏ hoàn toàn tesseract khỏi
  bản build** — một sự đơn giản hoá thật sự, nhưng chưa được thực hiện.
- `OcrWordTest` — mồ côi cùng với hai file trên
- `MarketScanner::menuAttempts` — không được tham chiếu ở đâu cả. Menu
  category chỉ được thử **một lần**; `RETRY FAILED` chính là lượt thử lại.
  Không nên cho rằng field này có tác dụng gì.
- Mutex của `ScanProgress` — đúng đắn, nhưng không còn cần thiết (chương
  trình chạy đơn luồng)
- `MarketStore::ClearBlackMarket()` — không dùng. `ClearCities()` **không**
  phải code chết (BuyCity có gọi nó); `Clear()` và `Holdings::Clear()` được
  dùng bởi nút Clear.
- `Albion.vcxproj` — project mồ côi, trỏ tới các đường dẫn không còn tồn tại.
  `SFML.vcxproj` là project thật.

---

## 12. Dư địa bố cục (layout headroom)

Cửa sổ 900×820. Lưới category: **4 cột**, chip 206px, bước 42px — đúng cái
vẽ lại mà §12 đã nói đợt category tới sẽ cần.

Log panel không còn bắt đầu ở một y cố định. Mỗi màn tự đặt `Game::logTop`
lúc vẽ, vì bốn nhóm của menu và mười hai hàng của lưới scan kết thúc ở những
chỗ khác nhau; `DrawLog` trả về sớm thay vì vẽ một panel cao dưới 40px, đó là
lý do màn Settings không có log.

| Màn hình | 46 category | Dư địa |
|---|---|---|
| Scan | lưới kết thúc ở 476; dòng nút hành động (`SCAN BM`/`SCAN MARKET`/`CLEAR`) 484–510 | **30px** |
| Settings | lưới kết thúc ở 492 | 48px |
| Scanning | lưới kết thúc ở 476; `RETRY FAILED` ở 500 | 24px |
| Shop | không có lưới; danh sách chạy 48 → 798, block cao 72px, bước 78px | cuộn được |

`Shop` là ngoại lệ với log panel: `render()` bỏ qua `DrawLog()` cho nó, nên danh
sách được trọn 750px và nhìn được khoảng chín mục cùng lúc. Đây là màn hình được
đọc trong lúc alt-tab qua lại với game, còn log phía sau chính là bảng plan mà
màn hình này được dựng từ đó — thứ duy nhất không có lý do gì phải xem trong lúc
đang làm theo nó.

**Đợt category tiếp theo sẽ cần vẽ lại lưới** — bốn cột hoặc ô ngắn hơn — không
phải thêm dòng.

Chi phí scan tăng theo `số category × số tier × số enchant`. Capes thêm vào
15 × 5 × 5 = 375 bước trên nền ~775 bước đã có. `page_size` bù lại được phần
lớn chi phí đó, vì listing của cape thường ngắn.

---

## 13. Cách chạy

1. Shortcut trên Desktop **Albion Bot** → chạy trực tiếp
   `x64\Release\SFML.exe`. Đồng ý một lần cảnh báo UAC (bắt gói tin cần quyền
   Administrator). Không còn cần `START.bat` nữa.
2. Đứng ở **Black Market** (Caerleon) → `Scan` → chọn category → `SCAN BM`
3. Di chuyển tới một **thành phố hoàng gia** → `Scan` → `SCAN MARKET`
4. `Plan` — nhập bốn con số, đọc bảng kết quả, **không tốn tiền**
5. `BuyCity` — nhập đúng bốn con số đó, thực hiện mua thật
6. Quay lại Black Market → `SellBM` → nhập % lợi nhuận

Một cú "flip" cần **cùng một item ở cả hai phía**, nên phải scan cùng category
ở cả hai nơi, nếu không sẽ không có gì để ghép cặp.

Sau khi chạy BuyCity, nửa dữ liệu City trong store bị xoá — **phải scan lại
City trước khi mua tiếp lần sau.**

**Build bot:** `MSBuild SFML.sln /p:Configuration=Release /p:Platform=x64` với
`VCPKG_ROOT` và `OPENCV_DIR` đã được thiết lập. Xem `BUILD.md`.

**Build feed:** .NET SDK 10.
```
cd D:\AlbionTool\AlbionFeed
dotnet publish -c Release -o D:\AlbionTool\bin\AlbionFeed
```

`D:\AlbionTool\bin\run-feed.bat` là thứ mà bot khởi động: kill mọi client bắt
gói tin (bao gồm cả `albiondata-client` nếu còn sót lại), sau đó khởi động
`AlbionFeed.exe --port 5000` ở dạng minimize. **Lệnh kill phải nằm trong chính
script đó** — vì nó chạy elevated, và một process không elevated không thể tắt
một process elevated.

Cài đặt bên ngoài repo: `D:\VSBuildTools`, `D:\OpenCV`, `D:\vcpkg` (tesseract,
nlohmann-json), .NET SDK, Npcap trong system32.

Npcap là một driver bắt gói tin **thụ động (passive)** — nó chỉ copy gói tin,
không sửa, không chặn, không định tuyến lại. Cổng của feed và mọi thứ
AlbionFeed gửi đi chỉ bind vào **127.0.0.1**, và AlbionFeed không chứa đoạn
code nào có thể upload đi nơi khác.

Nếu một `albiondata-client.exe` cũ còn sót lại từ cài đặt trước, nó vẫn đang
upload lên Albion Data Project công khai. Nó chạy elevated: kill nó từ một
cửa sổ lệnh elevated hoặc bấm nút **Capture**. Cũng nên kiểm tra Task Scheduler
để tìm `Albion Data Client` (§9.5).

---

## 14. Rủi ro

Tự động hoá input vi phạm điều khoản dịch vụ (ToS) của Albion Online và có thể
khiến tài khoản bị ban. README của tác giả gốc cũng nói y như vậy. **Không có
gì ở đây giúp bot tránh được anti-cheat.** Việc di chuyển chuột giống người
tồn tại vì người dùng yêu cầu như vậy, không phải vì nó giúp qua mặt được hệ
thống phát hiện.
