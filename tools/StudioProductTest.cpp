#include <winsock2.h>
#include <ws2tcpip.h>
#include "../SFML/Headers/Game.h"
#include <SFML/Window/Clipboard.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <stdexcept>
#include <functional>

static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void WaitUntil(const std::function<bool()>& predicate, const char* message) {
    const auto deadline = GetTickCount64() + 2500;
    while (!predicate() && GetTickCount64() < deadline) Sleep(5);
    Check(predicate(), message);
}
static SOCKET Connect(unsigned short port) {
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Check(client != INVALID_SOCKET, "Test socket allocation");
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(client, (sockaddr*)&address, sizeof(address)) != 0) { closesocket(client); throw std::runtime_error("Test connection failed"); }
    DWORD timeout = 2000; setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    return client;
}
static void SendAll(SOCKET socket, const string& text) {
    size_t offset = 0;
    while (offset < text.size()) {
        const int sent = send(socket, text.data() + offset, int(text.size() - offset), 0);
        Check(sent > 0, "Test send failed"); offset += size_t(sent);
    }
}
static void Post(unsigned short port, const string& body) {
    const SOCKET client = Connect(port);
    SendAll(client, "POST /market HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
        to_string(body.size()) + "\r\n\r\n" + body);
    char response[512]; const int received = recv(client, response, sizeof(response), 0);
    closesocket(client);
    Check(received > 0 && string(response, received).find("200 OK") != string::npos, "Feed HTTP response");
}
static string Order(long long id) {
    return nlohmann::json({{"Id",id},{"ItemTypeId","T4_MAIN_SWORD"},{"LocationId","1002"},
        {"QualityLevel",1},{"EnchantmentLevel",0},{"Amount",10},{"UnitPriceSilver",1000000},
        {"AuctionType","offer"}}).dump();
}

struct ToolRegressionAccess {
    static bool Active(MarketFeed& feed) { std::lock_guard<std::mutex> guard(feed.clientLock); return feed.activeClient != 0; }
    static void Click(Game& game, float x, float y) {
        game.sfEvent->mouseButton = {Mouse::Left, int(x), int(y)}; game.ShopController();
    }
    static void Screenshot(Game& game, const string& path, bool home = false) {
        RenderTexture target;
        Check(target.create(WIDTH, HEIGHT), "Render target allocation");
        target.clear(Theme::Background);
        if (home) game.DrawMenuScreen(&target); else game.DrawShopScreen(&target);
        target.display(); Check(target.getTexture().copyToImage().saveToFile(path), "Product screenshot");
    }
    static void Shop() {
        {
            Game game(true); game.window->setVisible(false);
            vector<FlipLot> lots(3);
            const char* names[] = {"Boltcasters T4.2", "Broadsword T6.0", "Leather Hood T5.1"};
            for (size_t i = 0; i < lots.size(); ++i) {
                auto& lot = lots[i]; lot.itemTypeId = "PRODUCT" + to_string(i);
                lot.displayName = names[i]; lot.searchText = names[i]; lot.quantity = 12;
                lot.tier = 4; lot.enchant = 2; lot.quality = 3; lot.bmQuality = 2;
                lot.marginCeiling = 18000; lot.profit = 120000 - static_cast<long long>(i) * 30000;
            }
            game.ShowShopList(lots, "PLAN");
            game.CopyShop(0); game.CopyShop(0); game.CopyShop(1);
            Check(game.shopEstimate.Total() == 330000, "Repeated copy estimate");
            game.UndoShop(); Check(game.shopEstimate.Total() == 240000 && !game.shopDone.count(1), "Undo latest copy");
            game.UndoShop(); Check(game.shopEstimate.Total() == 120000 && game.shopDone.count(0), "Undo repeat must retain previous copy mark");
            game.CopyShop(1);
            Check(game.shopEstimate.Total() == 210000, "Estimate before restart");
            // Exercise native event dispatch, including the right click on
            // Clear Everything that must never wipe a session.
            game.current_wnd = "Menu";
            for (const auto& block : MenuBlocks(game.shopBlocks.size())) for (size_t row = 0; row < block.rows.size(); ++row) {
                if (block.rows[row].command != "Clear") continue;
                const auto& box = block.geom.rows[row];
                PostMessageW(game.window->getSystemHandle(), WM_RBUTTONDOWN, MK_RBUTTON,
                    MAKELPARAM(int(box.left + 20), int(box.top + 20)));
            }
            game.updateSFMLEvents();
            Check(game.shopBlocks.size() == 3 && game.shopEstimate.Total() == 210000, "Right click must not dispatch destructive commands");
            for (const auto& block : MenuBlocks(game.shopBlocks.size())) for (size_t row = 0; row < block.rows.size(); ++row) {
                if (block.rows[row].command != "Shop") continue;
                const auto& box = block.geom.rows[row];
                PostMessageW(game.window->getSystemHandle(), WM_LBUTTONDOWN, MK_LBUTTON,
                    MAKELPARAM(int(box.left + 20), int(box.top + 20)));
            }
            game.updateSFMLEvents();
            Check(game.current_wnd == "Shop", "Native menu click must route to Manual shopping");

            const float buttonY = Ui::GridBottom(game.catTable.All().size(), 60.f) + 50.f;
            Check(game.HitScan(780.f, buttonY) == "SELECT ALL", "Bulk selection hit test");
            game.ScanController(game.HitScan(780.f, buttonY));
            Check(game.selectedScan.size() == game.catTable.All().size(), "All categories must be selected");
            RenderTexture scan;
            Check(scan.create(WIDTH, HEIGHT), "Scan preview allocation");
            scan.clear(Theme::Background); game.DrawScanScreen(&scan); scan.display();
            Check(scan.getTexture().copyToImage().saveToFile("studio-scan-all.png"), "Bulk scan preview");
            game.ScanController("CLEAR");
            Check(game.selectedScan.empty(), "Clear selections must leave no category selected");
            Screenshot(game, "studio-shopping.png");
            Screenshot(game, "studio-home-resume.png", true);
            Click(game, 650, 220); // Pending segment.
            Check(game.shopPendingOnly && game.shopVisible.size() == 1 && game.shopVisible[0] == 2, "Pending filter");
            game.UndoShop(); Check(game.shopVisible.size() == 2, "Undo restores pending item");
            Click(game, 500, 220); // All items.
            game.shopSearchFocused = true;
            for (char c : string("broad")) game.ShopText(unsigned(c));
            Check(game.shopVisible.size() == 1 && game.shopVisible[0] == 1, "Case insensitive search");
            game.shopSearchFocused = false;
            game.sfEvent->key.code = Keyboard::Enter; game.sfEvent->key.control = false;
            Check(game.ShopKey(), "Enter copy shortcut");
            Check(Clipboard::getString() == "Broadsword T6.0", "Filtered visible-row clipboard");
            Check(game.shopEstimate.Total() == 210000, "Filtered visible-row profit");
            Click(game, 800, 223);
            ifstream csv("shopping_list.csv"); string data((istreambuf_iterator<char>(csv)), {});
            Check(data.find("Broadsword") != string::npos && data.find("Boltcasters") == string::npos, "Export current view only");
            game.window->close();
        }
        {
            Game restored(true); restored.window->setVisible(false);
            Check(restored.shopBlocks.size() == 3 && restored.shopEstimate.Total() == 210000 &&
                restored.shopEstimate.Count() == 2 && restored.shopDone.size() == 2, "Restart must restore full manual session");
            Check(restored.NextAction() == "Shop", "Home resume action");
            Check(restored.HitMenu(750, 150) == "Shop", "Home call to action hit test");
            restored.shopSession.savedAt = static_cast<long long>(std::time(nullptr)) - 7200;
            Screenshot(restored, "studio-stale-plan.png");
            restored.ResetShop(); Check(restored.shopEstimate.Total() == 0, "Reset after restart");
            restored.MenuConroller("Clear");
            PostMessageW(restored.window->getSystemHandle(), WM_CLOSE, 0, 0);
            restored.scanServiceTick = 0;
            Check(!restored.WaitWithUi(150), "Startup waits must process window close before starting a scan");
        }
        {
            Game cleared(true); cleared.window->setVisible(false);
            Check(cleared.shopBlocks.empty() && cleared.shopEstimate.Count() == 0, "Clear must survive restart");
            cleared.current_wnd = "Shop"; Screenshot(cleared, "studio-empty.png");
            Screenshot(cleared, "studio-home-empty.png", true);
            cleared.window->close();
        }
    }
};

static void Recovery() {
    const wstring instanceName = L"Local\\MarketStudioTest-" + to_wstring(GetCurrentProcessId());
    {
        Studio::SingleInstance first(instanceName.c_str());
        Studio::SingleInstance second(instanceName.c_str());
        Check(!first.AlreadyRunning() && second.AlreadyRunning(), "Duplicate instance detection");
    }
    {
        Studio::SingleInstance released(instanceName.c_str());
        Check(!released.AlreadyRunning(), "Instance reservation must release after exit");
    }
    Studio::Session session;
    ShopBlock block; block.searchText = "=test,\"quoted\""; block.displayName = "Quoted, item";
    block.tier = 4; block.quantity = 3; block.minQuality = 1; block.profit = 100000; block.bands = 1;
    session.blocks.push_back(block); session.title = "PLAN"; session.savedAt = std::time(nullptr); session.generation = 23;
    session.estimate.Copy(100000, 0); session.estimate.Copy(100000, 0);
    string error;
    Check(Studio::SavePlan("test-plan.json", session, error), "Save plan");
    Check(Studio::SaveProgress("test-progress.json", session, error), "Save progress");
    Studio::Session recovered;
    Check(Studio::Load("test-plan.json", "test-progress.json", recovered, error) &&
        recovered.estimate.Total() == 200000 && recovered.estimate.Count() == 2, "Durable repeat history");
    { ofstream out("test-progress.json"); out << "{partial"; }
    Check(Studio::Load("test-plan.json", "test-progress.json", recovered, error) &&
        recovered.blocks.size() == 1 && recovered.estimate.Total() == 0 && !error.empty(), "Recover plan from damaged progress");
    session.generation = 999;
    Check(Studio::SaveProgress("test-progress.json", session, error), "Different generation progress");
    Check(Studio::Load("test-plan.json", "test-progress.json", recovered, error) && recovered.estimate.Total() == 0,
        "Never apply old history to a new plan");
    { ofstream out("test-plan.json"); out << "{partial"; }
    Check(!Studio::Load("test-plan.json", "test-progress.json", recovered, error) && recovered.blocks.size() == 1,
        "Damaged plan must not partially overwrite current state");
    Check(!Studio::SavePlan("missing-directory/file.json", session, error) && !error.empty(), "Report save failures");
    Check(Studio::ExportCsv("test-export.csv", session.blocks, {0}, {0}, error), "CSV export");
    { ifstream input("test-export.csv"); string csv((istreambuf_iterator<char>(input)), {});
        Check(csv.find("\"'=test,\"\"quoted\"\"\"") != string::npos, "CSV quote and formula handling"); }
    Check(Studio::Number((std::numeric_limits<long long>::min)()) == "-9,223,372,036,854,775,808", "Minimum integer formatting");
    const auto firstGeneration = Studio::NextGeneration(0);
    Check(Studio::NextGeneration(firstGeneration) > firstGeneration, "Two plans in the same clock tick need different generations");
    session.blocks[0].profit = (std::numeric_limits<long long>::max)();
    session.blocks.push_back(block);
    Check(Studio::SavePlan("test-plan.json", session, error), "Save oversized total fixture");
    Check(!Studio::Load("test-plan.json", "test-progress.json", recovered, error), "Reject corrupt overflowing plan totals");
    cout << "PASS: session recovery, atomic save failures, generation mismatch, CSV escaping\n";
}
static void Receiver() {
    struct WinsockGuard {
        WinsockGuard() { WSADATA data{}; Check(WSAStartup(MAKEWORD(2,2), &data) == 0, "Test Winsock startup"); }
        ~WinsockGuard() { WSACleanup(); }
    } winsock;
    MarketFeed feed; Check(feed.Start(0) && feed.Port() > 0, "Start isolated receiver");
    Post(feed.Port(), "{bad json");
    WaitUntil([&] { return feed.RejectedMessages() > 0; }, "Malformed JSON was not rejected");
    Post(feed.Port(), "{\"Orders\":[{\"Id\":\"bad\"}," + Order(41) + "]}");
    WaitUntil([&] { return feed.OrdersSeen() == 1; }, "Malformed row killed valid sibling");
    Post(feed.Port(), "{\"Purchase\":{\"OrderId\":7,\"Amount\":\"bad\"}}");
    Post(feed.Port(), "{\"Orders\":[" + Order(42) + "]}");
    WaitUntil([&] { return feed.OrdersSeen() == 2; }, "Receiver stopped after malformed purchase");
    const auto marketMessages = feed.MessagesSeen();
    Post(feed.Port(), "{\"Purchase\":{\"OrderId\":42,\"Amount\":10,\"Confirmed\":true}}");
    WaitUntil([&] { return feed.PurchasesSeen() == 1; }, "Valid purchase rejected");
    Post(feed.Port(), "{\"OtherTopic\":true}");
    Check(feed.MessagesSeen() == marketMessages, "Purchases/other topics must not finish market waits");
    vector<FeedOrder> orders; feed.Drain(orders);
    Check(orders.size() == 2 && orders[0].price == 100 && feed.LastMarketTick() != 0, "Feed data precision or health timestamp");
    const SOCKET excessive = Connect(feed.Port());
    SendAll(excessive, "POST / HTTP/1.1\r\nContent-Length: 999999999\r\n\r\n");
    WaitUntil([&] { return feed.RejectedMessages() >= 4; }, "Oversized request not rejected");
    closesocket(excessive);
    WaitUntil([&] { return !ToolRegressionAccess::Active(feed); }, "Oversized socket was not closed");
    const SOCKET stalled = Connect(feed.Port()); SendAll(stalled, "POST / HTTP/1.1\r\n");
    WaitUntil([&] { return ToolRegressionAccess::Active(feed); }, "Stalled connection was never accepted");
    const auto before = GetTickCount64(); feed.Stop();
    cout << "Half-open receiver shutdown: " << GetTickCount64() - before << " ms\n";
    Check(GetTickCount64() - before < 700, "Stopping a half-open receiver blocked the app");
    closesocket(stalled);
    Check(feed.Start(0), "Receiver cannot restart after stopped connection");
    Post(feed.Port(), "{\"Orders\":[" + Order(43) + "]}");
    WaitUntil([&] { return feed.OrdersSeen() == 3; }, "Restarted receiver failed to ingest");
    feed.Stop();
    cout << "PASS: real HTTP capture, malformed JSON/rows, request bounds, half-open shutdown, restart\n";
}
int main() {
    try { Recovery(); Receiver(); ToolRegressionAccess::Shop(); cout << "STUDIO PRODUCT CHECKS PASSED\n"; return 0; }
    catch (const std::exception& e) { cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
