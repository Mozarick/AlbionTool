#include "../SFML/Headers/Game.h"
#include "../SFML/Headers/AbortSwitch.h"
#include <SFML/Window/Clipboard.hpp>
#include <chrono>
#include <stdexcept>

static void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

struct ToolRegressionAccess {
    static void Click(Game& game, float x, float y, sf::Mouse::Button button = sf::Mouse::Left) {
        game.sfEvent->mouseButton = {button, int(x), int(y)};
        game.ShopController();
    }
    static void Run() {
        Game game(true);
        game.window->setVisible(false);
        Check(game.current_wnd == "Menu", "Startup must open Menu");
        for (const auto& section : MenuSections()) for (const auto& entry : section.entries)
            Check(string(entry.command) != "Craft", "Craft still in menu");

        vector<FlipLot> lots(3);
        for (int i = 0; i < 3; ++i) {
            auto& lot = lots[i];
            lot.itemTypeId = "ITEM" + to_string(i);
            lot.displayName = "Item " + to_string(i);
            lot.searchText = "Item " + to_string(i) + " T4.0";
            lot.tier = 4; lot.quantity = 10; lot.bmQuality = 1;
            lot.marginCeiling = 20000; lot.profit = i == 0 ? 100000 : (i == 1 ? 70000 : 30000);
        }
        game.ShowShopList(lots, "PLAN");
        game.mPosX = 0; game.mPosY = 0;
        const auto first = game.ShopBlockRect(0);
        const auto second = game.ShopBlockRect(1);
        Click(game, first.left + 20, first.top + 20);
        Check(game.shopEstimate.Total() == 100000, "First copy total");
        Check(sf::Clipboard::getString() == "Item 0 T4.0", "Clipboard search text");
        Click(game, second.left + 20, second.top + 20);
        Check(game.shopEstimate.Total() == 170000, "Second copy total");
        Click(game, first.left + 20, first.top + 20);
        Check(game.shopEstimate.Total() == 270000 && game.shopEstimate.Count() == 3,
            "Every copy including repeated items must add profit");
        Click(game, first.left + 20, first.top + 20, sf::Mouse::Right);
        Check(game.shopEstimate.Total() == 270000, "Right click must not copy/count");
        Click(game, 50, 115);
        Check(game.shopEstimate.Total() == 270000, "Header must not count a hidden row");

        sf::RenderTexture screenshot;
        Check(screenshot.create(WIDTH, HEIGHT), "Screenshot allocation");
        screenshot.clear(Theme::Background);
        game.DrawShopScreen(&screenshot);
        screenshot.display();
        auto pixels = screenshot.getTexture().copyToImage();
        bool painted = false;
        for (unsigned y = 100; y < 125; ++y) for (unsigned x = 20; x < 600; ++x)
            if (pixels.getPixel(x, y) != Theme::Background) painted = true;
        Check(painted, "Estimated profit header did not render");
        Check(pixels.saveToFile("shop-profit.png"), "Shop screenshot");
        game.MenuConroller("Shop");
        Check(game.shopEstimate.Total() == 270000, "Reopening Shop must preserve total");
        Click(game, float(WIDTH) - Ui::Inset - 50, 112);
        Check(game.shopEstimate.Total() == 0 && game.shopDone.empty(), "Reset must clear total and marks");
        Click(game, first.left + 20, first.top + 20);
        game.ShowShopList(lots, "DEALS");
        Check(game.shopEstimate.Total() == 0, "New plan must reset estimate");
        game.shopScroll = 55.f;
        screenshot.clear(Theme::Background);
        game.DrawShopScreen(&screenshot);
        screenshot.display();
        Check(screenshot.getTexture().copyToImage().saveToFile("shop-scrolled.png"), "Clipped shop screenshot");

        // 10,000 rows: click a scrolled row and verify the visible item wins.
        lots.resize(10000, lots[0]);
        for (size_t i = 0; i < lots.size(); ++i) {
            lots[i].itemTypeId = "ROW" + to_string(i);
            lots[i].searchText = "Row " + to_string(i);
            lots[i].profit = 100000 - static_cast<long long>(i);
        }
        game.ShowShopList(lots, "LARGE LIST");
        game.shopScroll = 9000 * 94.f;
        const auto visible = game.ShopBlockRect(9000);
        Click(game, visible.left + 20, visible.top + 20);
        Check(game.shopEstimate.Total() == 91000, "Scrolled row profit");
        Check(sf::Clipboard::getString() == "Row 9000", "Scrolled row clipboard");
        auto start = chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) game.render();
        cout << "Large Shop: " << chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - start).count() << " ms / 20 frames\n";

        game.MenuConroller("Clear");
        Check(game.shopBlocks.empty() && game.shopEstimate.Total() == 0,
            "Clear Everything must clear the shopping total and list");

        // The same service callback is used inside scanner waits. It must
        // pump a real close event and request a safe stop during a run.
        Abort::Guard stopping;
        game.scanServiceTick = 0;
        MarketScanner scanner(game.feed);
        int serviced = 0;
        scanner.SetService([&] {
            if (++serviced == 3) PostMessageW(game.window->getSystemHandle(), WM_CLOSE, 0, 0);
            game.ServiceScan(false);
        });
        scanner.Wait(250);
        Check(serviced >= 3, "Scanner wait did not service the window");
        Check(!game.window->isOpen() && Abort::Latched(), "Scan close must be processed and latched");
    }
};

static void LargeMarket() {
    MarketStore store;
    vector<FeedOrder> batch;
    batch.reserve(200000);
    for (int i = 0; i < 200000; ++i) {
        FeedOrder order;
        order.id = i + 1; order.groupTypeId = "T4_ITEM" + to_string(i / 20);
        order.itemTypeId = order.groupTypeId; order.locationId = "1002";
        order.price = 100; order.amount = 1; order.tier = 4; order.quality = 1;
        batch.push_back(std::move(order));
    }
    auto start = chrono::steady_clock::now();
    store.Absorb(std::move(batch));
    const auto elapsed = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count();
    Check(store.CityCount() == 200000 && store.BlackMarketCount() == 0, "Large scan lost offers");
    cout << "200,000 offers absorbed: " << elapsed << " ms\n";
    auto offers = store.CityOffers();
    vector<FeedOrder> bids;
    for (int i = 0; i < 10000; ++i) {
        FeedOrder bid;
        bid.id = 300000 + i; bid.groupTypeId = "T4_ITEM" + to_string(i);
        bid.itemTypeId = bid.groupTypeId; bid.locationId = "3003";
        bid.isBuyOrder = true; bid.price = 200; bid.amount = 20; bid.quality = 1; bid.tier = 4;
        bids.push_back(std::move(bid));
    }
    FeedOrder refresh = offers.front();
    refresh.price = 110;
    store.Absorb(vector<FeedOrder>{refresh});
    Check(store.CityCount() == 200000, "Rescan duplicated an order");
    store.Absorb(bids);
    Check(store.BlackMarketCount() == 10000, "BM routing");
    MarketFeed feed;
    ItemTable items;
    CityBuyer buyer(feed, items);
    BuyPlan settings;
    settings.minProfitRate = 0;
    settings.minProfitSilver = 0;
    settings.budget = 0;
    start = chrono::steady_clock::now();
    auto plan = buyer.Plan(bids, offers, settings);
    Check(plan.size() == 10000, "Large market plan lost items");
    long long units = 0, profit = 0;
    for (const auto& lot : plan) { units += lot.quantity; profit += lot.profit; }
    Check(units == 200000 && profit == 18400000, "Large plan quantity/profit mismatch");
    cout << "Plan 200,000 offers / 10,000 items: " << chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now() - start).count() << " ms\n";
}

int main() {
    try {
        LargeMarket();
        ToolRegressionAccess::Run();
        cout << "ALL PASS: large scan, clipboard, estimated profit, scrolling, safe close\n";
        return 0;
    } catch (const std::exception& e) {
        cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
