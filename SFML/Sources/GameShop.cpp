#include "../Headers/Game.h"
#include <SFML/Window/Clipboard.hpp>
#include <cctype>
#include <filesystem>

namespace {
    constexpr float ListTop = 288.f;
    constexpr float ListBottom = float(HEIGHT) - 56.f;
    constexpr float RowHeight = 86.f;
    constexpr float RowPitch = 94.f;
    const FloatRect SearchBox(20.f, 202.f, 438.f, 42.f);
    const FloatRect Filters(472.f, 202.f, 244.f, 42.f);
    const FloatRect ExportButton(732.f, 202.f, 148.f, 42.f);
    const FloatRect UndoButton(644.f, 102.f, 104.f, 32.f);
    const FloatRect ResetButton(760.f, 102.f, 100.f, 32.f);
    string Lower(string text) {
        for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }
    string Fit(string text, Ui::Text style, float width) {
        if (Ui::Width(text, style) <= width) return text;
        while (!text.empty() && Ui::Width(text + "...", style) > width) text.pop_back();
        return text + "...";
    }
    string Signed(long long value) { return (value >= 0 ? "+" : "") + Studio::Number(value); }
}

void Game::Notice(const string& message, bool error) {
    studioNotice = message; studioNoticeError = error; studioNoticeTick = GetTickCount64();
}
void Game::RebuildShopVisible() {
    shopVisible.clear();
    const string query = Lower(shopQuery);
    for (size_t i = 0; i < shopBlocks.size(); ++i) {
        if (shopPendingOnly && shopDone.count(i)) continue;
        if (!query.empty() && Lower(shopBlocks[i].searchText + " " + shopBlocks[i].displayName).find(query) == string::npos) continue;
        shopVisible.push_back(i);
    }
    shopScroll = min(shopScroll, ShopMaxScroll());
}
void Game::SaveShopProgress() {
    string error;
    if (!Studio::SaveProgress("shopping_progress.json", shopSession, error))
        Notice("Could not save progress: " + error, true);
}
void Game::ShowShopList(const vector<FlipLot>& lots, const string& title) {
    shopBlocks = BuildShopList(lots);
    shopTitle = title;
    shopTotalProfit = shopTotalUnits = 0;
    for (const auto& block : shopBlocks) { shopTotalProfit += block.profit; shopTotalUnits += block.quantity; }
    shopScroll = 0.f; shopDone.clear(); shopEstimate.Reset();
    shopCopied = size_t(-1); shopCopiedTick = 0;
    shopQuery.clear(); shopSearchFocused = false; shopPendingOnly = false;
    shopSession.savedAt = static_cast<long long>(std::time(nullptr));
    shopSession.generation = Studio::NextGeneration(shopSession.generation);
    RebuildShopVisible();
    string error;
    if (!Studio::SavePlan("shopping_session.json", shopSession, error)) Notice("Could not save list: " + error, true);
    else { SaveShopProgress(); Notice("Shopping list saved on this computer."); }
    current_wnd = "Shop";
}
float Game::ShopMaxScroll() const {
    return max(0.f, float(shopVisible.size()) * RowPitch - (ListBottom - ListTop));
}
FloatRect Game::ShopBlockRect(size_t index) const {
    // Index is the visible position; shopVisible maps it back to the plan.
    return FloatRect(20.f, ListTop + float(index) * RowPitch - shopScroll, 846.f, RowHeight);
}
void Game::CopyShop(size_t index) {
    if (index >= shopBlocks.size()) return;
    const auto& block = shopBlocks[index];
    const string& text = block.searchText.empty() ? block.displayName : block.searchText;
    if (text.empty()) { Notice("This item has no search name.", true); return; }
    const auto clipboardText = String::fromUtf8(text.begin(), text.end());
    Clipboard::setString(clipboardText);
    if (Clipboard::getString() != clipboardText) { Notice("Clipboard is busy. Click again to copy.", true); return; }
    if (!shopEstimate.Copy(block.profit, index)) { Notice("Estimated total is too large. Reset to start a new total.", true); return; }
    shopCopied = index; shopCopiedTick = GetTickCount64(); shopDone.insert(index);
    Notice("Copied " + text + "   /   " + Signed(block.profit) + " added");
    if (shopPendingOnly) RebuildShopVisible();
    SaveShopProgress();
}
void Game::UndoShop() {
    size_t index = 0;
    if (!shopEstimate.Undo(index)) { Notice("Nothing to undo yet."); return; }
    bool stillCopied = false;
    for (const auto& record : shopEstimate.History()) if (record.first == index) { stillCopied = true; break; }
    if (!stillCopied) shopDone.erase(index);
    shopCopied = size_t(-1);
    RebuildShopVisible(); Notice("Last copy removed from the estimate."); SaveShopProgress();
}
void Game::ResetShop() {
    shopEstimate.Reset(); shopDone.clear(); shopCopied = size_t(-1);
    RebuildShopVisible(); Notice("Estimate reset. Your shopping list is still here."); SaveShopProgress();
}
bool Game::ShopKey() {
    const auto& key = sfEvent->key;
    if (key.control && key.code == Keyboard::F) { shopSearchFocused = true; return true; }
    if (key.control && key.code == Keyboard::Z && !shopSearchFocused) { UndoShop(); return true; }
    if (key.code == Keyboard::Escape && shopSearchFocused) { shopSearchFocused = false; return true; }
    if (shopSearchFocused) {
        if (key.code == Keyboard::Enter) shopSearchFocused = false;
        return true;
    }
    if (key.code == Keyboard::Enter) {
        const size_t first = size_t(shopScroll / RowPitch);
        if (first < shopVisible.size()) CopyShop(shopVisible[first]);
        return true;
    }
    if (key.code == Keyboard::PageDown || key.code == Keyboard::PageUp) {
        shopScroll = max(0.f, min(ShopMaxScroll(), shopScroll +
            (key.code == Keyboard::PageDown ? 1.f : -1.f) * (ListBottom - ListTop)));
        return true;
    }
    return false;
}
void Game::ShopText(unsigned unicode) {
    if (!shopSearchFocused) return;
    if (unicode == 8) { if (!shopQuery.empty()) shopQuery.pop_back(); }
    else if (unicode >= 32 && unicode <= 126 && shopQuery.size() < 80) shopQuery += char(unicode);
    else return;
    shopScroll = 0.f; RebuildShopVisible();
}
void Game::ShopController() {
    if (sfEvent->mouseButton.button != Mouse::Left) return;
    const float x = float(sfEvent->mouseButton.x), y = float(sfEvent->mouseButton.y);
    if (Ui::NavBackRect().contains(x, y)) { shopSearchFocused = false; current_wnd = "Menu"; return; }
    if (ResetButton.contains(x, y)) { ResetShop(); return; }
    if (UndoButton.contains(x, y)) { UndoShop(); return; }
    if (ExportButton.contains(x, y)) {
        if (shopVisible.empty()) { Notice("There are no items to export."); return; }
        string error;
        if (Studio::ExportCsv("shopping_list.csv", shopBlocks, shopVisible, shopDone, error))
            Notice("Exported " + to_string(shopVisible.size()) + " items to shopping_list.csv");
        else Notice("Export failed: " + error, true);
        return;
    }
    const int segment = Ui::HitSegmented(Filters, 2, x, y);
    if (segment >= 0) { shopPendingOnly = segment == 1; shopScroll = 0.f; RebuildShopVisible(); return; }
    shopSearchFocused = SearchBox.contains(x, y);
    if (shopSearchFocused || y < ListTop || y >= ListBottom) return;
    const size_t first = size_t(shopScroll / RowPitch);
    const size_t end = min(shopVisible.size(), first + size_t((ListBottom - ListTop) / RowPitch) + 2);
    for (size_t row = first; row < end; ++row) if (ShopBlockRect(row).contains(x, y)) {
        CopyShop(shopVisible[row]); return;
    }
}
void Game::DrawShopScreen(RenderTarget* target) {
    RenderTarget& wnd = target ? *target : *wnd_stack.top();
    const float mx = float(mPosX), my = float(mPosY);
    Ui::DrawNavBar(wnd, float(WIDTH), "Manual shopping", "Home", Ui::NavBackRect().contains(mx, my));
    const bool stale = shopSession.savedAt && std::time(nullptr) - shopSession.savedAt > 900;
    Ui::Draw(wnd, shopTitle + "   /   " + to_string(shopBlocks.size()) + " searches   /   " +
        to_string(shopTotalUnits) + " units", Ui::Text::Footnote, Theme::TextSecondary, 20.f, 57.f);
    Ui::DrawRight(wnd, "Plan from " + Studio::ShortAge(shopSession.savedAt) + (stale ? " - rescan for fresh prices" : ""),
        Ui::Text::Caption, stale ? Theme::Running : Theme::TextMuted, 880.f, 58.f);

    Ui::FillRounded(wnd, FloatRect(20.f, 82.f, 860.f, 104.f), 14.f, Theme::Panel);
    Ui::Draw(wnd, "PROFIT ESTIMATED", Ui::Text::FootnoteStrong, Theme::Ok, 40.f, 96.f);
    Ui::Draw(wnd, Signed(shopEstimate.Total()), Ui::Text::Title2, Theme::TextPrimary, 40.f, 117.f);
    Ui::Draw(wnd, to_string(shopEstimate.Count()) + " copies   /   " + to_string(shopDone.size()) +
        " of " + to_string(shopBlocks.size()) + " searches copied", Ui::Text::Caption, Theme::TextSecondary, 40.f, 154.f);
    Ui::Draw(wnd, "PLAN POTENTIAL", Ui::Text::Caption, Theme::TextMuted, 378.f, 97.f);
    Ui::Draw(wnd, Signed(shopTotalProfit), Ui::Text::Headline, Theme::Accent, 378.f, 119.f);
    Ui::Draw(wnd, "Each copy adds profit. Repeated copies count again.", Ui::Text::Caption, Theme::TextMuted, 378.f, 154.f);
    Ui::DrawButton(wnd, UndoButton, "Undo", Ui::ButtonStyle::Tinted, UndoButton.contains(mx, my));
    Ui::DrawButton(wnd, ResetButton, "Reset", Ui::ButtonStyle::Tinted, ResetButton.contains(mx, my));

    Ui::FillRounded(wnd, SearchBox, 10.f, Theme::Panel);
    Ui::StrokeRounded(wnd, SearchBox, 10.f, shopSearchFocused ? Theme::Accent : Theme::Border);
    const string search = shopQuery.empty() ? (shopSearchFocused ? "Type an item name..." : "Search items   /   Ctrl+F") : shopQuery;
    Ui::Draw(wnd, Fit(search + (shopSearchFocused ? "|" : ""), Ui::Text::Subhead, 408.f),
        Ui::Text::Subhead, shopQuery.empty() ? Theme::TextMuted : Theme::TextPrimary, 36.f, 215.f);
    Ui::DrawSegmented(wnd, Filters, {"All items", "Pending"}, shopPendingOnly ? 1 : 0);
    Ui::DrawButton(wnd, ExportButton, "Export CSV", Ui::ButtonStyle::Tinted, ExportButton.contains(mx, my));
    Ui::Draw(wnd, to_string(shopVisible.size()) + " shown   /   Click to copy search text", Ui::Text::Caption,
        Theme::TextMuted, 20.f, 258.f);
    Ui::DrawRight(wnd, "Enter: copy   /   Ctrl+Z: undo   /   PgUp/PgDn", Ui::Text::Caption, Theme::TextMuted, 880.f, 258.f);

    if (shopVisible.empty()) {
        Ui::FillRounded(wnd, FloatRect(20.f, ListTop, 860.f, 146.f), 14.f, Theme::Panel);
        Ui::Draw(wnd, shopBlocks.empty() ? "Your shopping list starts here." : "No items match this view.",
            Ui::Text::Title2, Theme::TextPrimary, 44.f, ListTop + 30.f);
        Ui::Draw(wnd, shopBlocks.empty() ? "Scan both markets, then use Plan or Find deals on Home." :
            (shopPendingOnly ? "All matching searches are copied. Switch to All items or undo a copy." :
                "Try a shorter item name, or clear the search field."),
            Ui::Text::Footnote, Theme::TextSecondary, 44.f, ListTop + 74.f);
    } else {
        const View previous = wnd.getView();
        View listView(FloatRect(0.f, ListTop, float(WIDTH), ListBottom - ListTop));
        listView.setViewport(FloatRect(0.f, ListTop / float(HEIGHT), 1.f, (ListBottom - ListTop) / float(HEIGHT)));
        wnd.setView(listView);
        const size_t first = size_t(shopScroll / RowPitch);
        const size_t end = min(shopVisible.size(), first + size_t((ListBottom - ListTop) / RowPitch) + 2);
        for (size_t row = first; row < end; ++row) {
            const size_t index = shopVisible[row];
            const auto& block = shopBlocks[index];
            const auto box = ShopBlockRect(row);
            const bool copied = shopDone.count(index) != 0;
            const bool hover = box.contains(mx, my) && my >= ListTop && my < ListBottom;
            Ui::FillRounded(wnd, box, 12.f, hover ? Theme::PanelRaised : Theme::Panel);
            Ui::StrokeRounded(wnd, box, 12.f, hover ? Theme::Accent : Theme::Border);
            if (copied) Ui::FillRounded(wnd, FloatRect(box.left, box.top + 15.f, 3.f, 56.f), 1.f, Theme::Ok);
            const string name = block.searchText.empty() ? block.displayName : block.searchText;
            Ui::Draw(wnd, Fit(name, Ui::Text::Headline, 615.f), Ui::Text::Headline,
                copied ? Theme::TextSecondary : Theme::TextPrimary, box.left + 16.f, box.top + 10.f);
            Ui::DrawRight(wnd, Signed(block.profit), Ui::Text::Headline, Theme::Ok, box.left + box.width - 16.f, box.top + 10.f);
            Ui::Draw(wnd, "Up to " + to_string(block.quantity) + " units   /   " + QualityName(block.minQuality) +
                " or better" + (block.bands > 1 ? "   /   " + to_string(block.bands) + " quality bands" : ""),
                Ui::Text::Footnote, Theme::TextMuted, box.left + 16.f, box.top + 36.f);
            Ui::Draw(wnd, "BUY UNDER  " + Studio::Number(block.ceiling) + " each", Ui::Text::CalloutStrong,
                Theme::Accent, box.left + 16.f, box.top + 57.f);
            if (copied) Ui::DrawRight(wnd, shopCopied == index && GetTickCount64() - shopCopiedTick < 1400 ? "COPIED" : "In estimate",
                Ui::Text::Caption, Theme::Ok, box.left + box.width - 16.f, box.top + 59.f);
        }
        const float maxScroll = ShopMaxScroll();
        if (maxScroll > 0) {
            const float height = ListBottom - ListTop;
            const float thumb = max(30.f, height * height / (float(shopVisible.size()) * RowPitch));
            Ui::FillRounded(wnd, FloatRect(874.f, ListTop, 6.f, height), 3.f, Theme::Panel);
            Ui::FillRounded(wnd, FloatRect(874.f, ListTop + (height - thumb) * shopScroll / maxScroll, 6.f, thumb), 3.f, Theme::BorderStrong);
        }
        wnd.setView(previous);
    }
    Ui::DrawSeparator(wnd, 20.f, 778.f, 860.f);
    const bool notice = !studioNotice.empty() && GetTickCount64() - studioNoticeTick < 8000;
    Ui::Draw(wnd, Fit(notice ? studioNotice : "Saved locally. Profit is estimated from your plan's captured prices.",
        Ui::Text::Footnote, 840.f), Ui::Text::Footnote,
        notice ? (studioNoticeError ? Theme::Failed : Theme::Ok) : Theme::TextMuted, 20.f, 789.f);
}
