# Albion market bot — machine handover

**Audience: an AI agent picking this project up cold.** Optimised for lookup and
for not re-breaking things, not for reading start to finish. Prose survives only
where the *reason* is the thing that stops a regression.

Repo base: `github.com/Merab16/OpenCV_and_tesseract`. C++17 / MSVC / SFML GUI.
Working tree only — **nothing is committed, there is no undo.** Read before you
delete or overwrite.

Paths are relative to `D:\AlbionTool\OpenCV_and_tesseract\`.

---

## 0. Hard invariants

Break one and the failure is silent or misattributed. Each is here because it has
already happened; §9 has the incident.

| # | Invariant | What breaks |
|---|---|---|
| I1 | **No `printf`/`puts`/`fprintf`.** Only `std::cout` is visible. | GUI binary has no console; C stdout is discarded. Output vanishes with no error. §9.1 |
| I2 | **No `system()`.** | Spawns a `cmd.exe` flash per call. §9.2 |
| I3 | **The bot's window must never overlap the game's click/read coordinates.** Use the `WindowHidden` guard around every automated run. | Clicks land on the bot; region hashes read the bot's own pixels. Reports success, does nothing. §9.3 |
| I4 | **No threads in the input path.** Scan/buy/sell run synchronously on the event thread. | A 60fps render loop next to `Sleep()`-paced `SendInput` perturbs timing. §9.4 |
| I5 | **No OCR anywhere.** Every menu row is a measured coordinate. `Ocr.h/.cpp` are dead code. | OCR read "Tier 5" as "Tier S"; whole tiers were skipped silently. §9.10 |
| I6 | **Grep for callers per *symbol* before deleting.** | `ClearItemName()` was misclassified as dead; it is on the live buying path. §9.6 |
| I7 | **`ClickInside` must not add `MoveTargetSlopPx` on top of its random point.** | The tier button is 7px tall; +3px slop misses it and the dropdown never opens. |
| I8 | **`menu_sub` in `categories.tsv` must be globally unique.** | `Game.cpp` matches button clicks by that string. Duplicates make two buttons toggle each other. Guarded by `tools/CategoryTest`. |
| I9 | **All coordinates are 1920×1080 base, scaled at runtime by `Config.ScreenScale`.** Measure only with a DPI-aware tool. | 125% DPI makes an unaware process see 1536×864; everything is 25% off. |
| I11 | **The emergency stop must never break between a Buy click and its confirmation.** Poll anywhere; act only at the boundaries listed in §7b. | The game may already have acted on the click. Abandoning the answer loses a real purchase out of the ledger — §9.9 again, self-inflicted. |
| I10 | **A failure message that is a *deduction*, not an observation, is the first suspect.** | "purchase did not register" and "gone from the market" were both wrong deductions that cost whole lots. §9.9, §9.11, §9.12 |

---

## 1. System map

```
Albion Online client
     │ UDP :5056  (Photon)
     ▼
AlbionFeed                    C#, D:\AlbionTool\AlbionFeed
                              needs Npcap + Administrator
     │ HTTP POST 127.0.0.1:5000/market    {"Orders":[...]}
     │ HTTP POST 127.0.0.1:5000/purchase  {"Purchase":{...}}
     ▼
MarketFeed                    own thread, Winsock, nlohmann/json
     │                        two independent queues + atomic counters
     ├─ orders ────► MarketStore        split by data, keyed by order id
     │                   ├──► CityBuyer::Plan  → vector<FlipLot>
     │                   │      └► CityBuyer::Run → Holdings
     │                   └──► BlackMarketSeller::Run ← Holdings
     └─ purchases ─► CityBuyer::WaitForPurchase
```

`MarketFeed`'s socket accept loop is the only load-bearing extra thread.
Everything else is on the SFML event thread (I4).

The routing is decided by the **body shape**, not the URL path, so a new topic
can be added without touching routing. Field names are the game's own, which is
why they are capitalised.

### AlbionFeed contract

References four projects from an **unmodified** clone of
`JPCodeCraft/AlbionDataAvalonia` at `D:\AlbionTool\AlbionDataAvalonia`
(`Protocol16`, `PhotonPackageParser`, `Albion.Network`,
`AlbionDataAvalonia.Shared`). Never edit that clone — `git pull` is how the
protocol stays current when the game changes. The Avalonia app itself is never
built; that is the part containing the uploader.

Five handlers, everything else discarded:

| Packet | Yields |
|---|---|
| `JoinResponse` | current map id (3003 = Black Market) |
| `AuctionGetOffersResponse` | city sell offers |
| `AuctionGetRequestsResponse` | black market buy orders |
| `AuctionBuyOfferRequest` | order id + units of a buy the client just sent |
| `AuctionBuyOfferResponse` | the server's answer to that buy |

Two purchase messages arrive per buy: `confirmed == false` when the client sent
the request, then `confirmed == true` when the server answered.

`UnitPriceSilver` on the wire is **silver × 10000**. AlbionFeed divides. A missed
division is a 10,000× error.

**Why not the third-party `albiondata-client`:** (a) it exists to upload to the
public Albion Data Project, and staying quiet depended on remembering `-i ""`
every launch — AlbionFeed has no uploader at all, which is a guarantee rather
than a setting; (b) `AuctionBuyOffer` forwarding is what made purchase
confirmation an observation instead of a guess (§9.9).

Historical, recorded so it is not rediscovered: that Go client's NATS uploader is
broken (fresh connection per message, `Publish()` only buffers, connection
abandoned before the flusher runs → zero PUBs, verified with `nats-server -DV`),
and its documented `-i noop` **panics** it (`len(target) < 4` admits `"noop"`,
then `target[0:8]` slices out of range).

---

## 2. Files

### Core modules

| File | Role |
|---|---|
| `MarketFeed.h/.cpp` | HTTP server on 127.0.0.1:5000. JSON → `FeedOrder` / `FeedPurchase` in two separate queues. Owns `ItemTable` (item id → display name). |
| `MarketStore.h/.cpp` | Files orders into black-market bids vs city offers **by what the data says**, keyed by game order id. |
| `MarketScanner.h/.cpp` | Drives the market menus so the *game* requests listings. Holds `CategoryLayout` (46 entries). |
| `CityBuyer.h/.cpp` | Pairs bids with offers, plans lots, executes purchases. The core arithmetic. |
| `BlackMarket.h/.cpp` | Sells the ledger into black market bids. |
| `Holdings.h/.cpp` | Ledger of bought-not-yet-sold goods → `holdings.tsv`. |
| `MarketUi.h` | Tier / quality / enchantment dropdowns. Header only, shared by scan+buy+sell. |
| `MoveToPosition.h/.cpp` | Human-like cursor movement, clicking, typing. |
| `ScreenCapture.h/.cpp` | BitBlt into a reused DIB as `cv::Mat`. Used only for region hashing. |
| `Ocr.h/.cpp` | **Dead.** Compiles, nothing calls it. I5. |
| `Game.h/.cpp` | Window, state machine, all button handling, all `Run*` entry points. |
| `LogSink.h/.cpp` | Redirects `std::cout` into the in-window panel and `bot.log`. |
| `ScanProfile.h/.cpp` | Per (location, category) tier + enchant selection → `scan_profile.tsv`. |
| `ScanProgress.h/.cpp` | Per-step scan status for the Scanning screen. |
| `ShopList.h/.cpp` | Groups planned lots into one entry per market search, for the hand-shopping screen. Pure arithmetic. |
| `AbortSwitch.h/.cpp` | The emergency stop. Polls the hotkey with `GetAsyncKeyState` and latches. |
| `UiKit.h/.cpp` | The whole drawing vocabulary: rounded rects, the iOS type ramp in Inter, list groups, buttons, chips, segmented control, nav bar. Depends on SFML and `Theme.h` and nothing else. |
| `Theme.h` | One symbol per role, named to match the Figma file's variables exactly. |
| `Shapes.h/.cpp`, `MyImg.h/.cpp` | **Dead.** `TextBox` and the background image were what the old UI was built from; nothing draws them now. Unlike `Ocr` these are **removed from the vcxproj** — `Shapes.cpp` was the last warning in the build, and dead code that still shouts is worse than either keeping or deleting it. The files stay on disk because this tree has no commits to recover them from. |
| `ProcessLauncher.h/.cpp` | Starts the game launcher and `run-feed.bat`. |

### Data files (in `SFML/`, which is the working directory)

| File | Written by | Format |
|---|---|---|
| `Config.txt` | human | `key: value`, `#` comments. §7 |
| `categories.tsv` | human | `key \t menu_top \t menu_sub \t slug_top \t slug_sub`. 46 rows. `slug_*` are parsed and never used. |
| `items.tsv` | generated from ao-bin-dumps | `unique_name \t display_name`. 5165 rows. |
| `scan_profile.tsv` | app | `location \t category_key \t tiers(csv) \t enchants(csv)`. Blank tiers = one pass. Auto-created; `EnsureDefaults` fills new categories. |
| `holdings.tsv` | app | `itemTypeId \t tier \t enchant \t quality \t quantity \t unitCost \t searchText` |
| `bot.log` | app | mirror of the log panel |

`scan_profile.tsv` and `holdings.tsv` may legitimately not exist yet.

### Test tools (`tools/`, each with its own `build_*.bat`)

| Tool | Needs game? | Covers |
|---|---|---|
| `PlanTest` | no | `CityBuyer::Plan` arithmetic. 26 checks. |
| `CategoryTest` | no | `categories.tsv` ↔ `CategoryLayout` consistency. 10 checks. Run from `SFML/`: it reads `categories.tsv` out of the working directory and fails anywhere else. |
| `ShopListTest` | no | `BuildShopList` grouping/ranking and `CityBuyer::Shortfall` re-costing. 26 checks. |
| `UiPreview` | no | Not a test - renders the real screens to PNG through a `RenderTexture`, no window and no game. `build_uipreview.bat` builds it, runs it from `SFML/` and cleans up after itself. The fastest way to see a layout change. |
| `WipeTest` | no | `LogSink::Wipe()` |
| `FeedTest` | **yes** | live packet reception; waits 90s for traffic |
| `OcrWordTest` | no | orphaned with the OCR path |

---

## 3. UI state machine

`Game::current_wnd` is a string.

| State | Rendered by | Click routed to |
|---|---|---|
| `Menu` | `render()` + `vTB_menu` | `MenuConroller` |
| `Scan` | `render()` + `vTB_scan` | `ScanController` |
| `Scanning` | `DrawScanningScreen()` | `ScanningController` |
| `Settings` | `DrawSettingsScreen()` | `SettingsController` |
| `Shop` | `DrawShopScreen()` | `ShopController` |

**No screen owns `TextBox`es any more.** Every screen draws through `UiKit` and
hit-tests its own geometry with a `Hit*` function that calls the SAME layout
helper the drawing calls - `MenuBlocks`, `Ui::LayoutGroup`, `Ui::ChipRect`,
`ShopBlockRect`. A click can only land in a box that function produced.

That is also what took the click path off `menu_sub` uniqueness. Rows carry a
`command` id separate from their display label, and categories route by KEY.
I8 still holds for the scanner's own button matching in `Game.cpp`, but a
duplicate `menu_sub` is now a cosmetic problem in the UI rather than two
buttons toggling each other.
| `EnterWndBM` | `drawEnterWnd()` | keyboard only |
| `EnterWndBuyProfit` | `drawEnterWnd()` | keyboard only |
| `EnterWndBuyMinProfit` | `drawEnterWnd()` | keyboard only |
| `EnterWndBuyCombine` | `DrawBuyCombineScreen()` | `BuyCombineController` |
| `EnterWndBuy` | `drawEnterWnd()` | keyboard only |

Number-entry states accept `Num0`–`Num9`, `Backspace`, `Enter`, `Escape`.
`MAX_ENTERED_DIGITS = 12`, parsed with `stoll` into `long long`.

**Adding a state requires three edits**, and missing any one is silent:
`render()` routing, the click/keyboard routing in `updateSFMLEvents`, and the
`Escape` list. `EnterWndBuy` shipped without a `render()` branch for exactly this
reason (§9.8).

`Shop` is the only state that suppresses the log panel, and the only one that
handles `MouseWheelScrolled`. Its click targets are not `TextBox`es: every block
is drawn from `shopBlocks` and hit-tested against `Game::ShopBlockRect`, which is
the same function the drawing uses. Two copies of that arithmetic would be two
chances to copy the wrong item's name to the clipboard.

---

## 4. Buttons — exact behaviour

Main menu order from `vMenu` in `Menus.h`:
`SellBM, Scan, Settings, Plan, Deals, BuyCity, Capture, Clear, Exit`.

`BACK` is orange, top-right on every screen (`BackButtonPos()` = `WIDTH-132, 8`).

`MenuConroller` returns early on an empty command — a click on empty space must
not wipe a printed plan (§9.8).

### SellBM
→ `EnterWndBM`. One number: required profit % over recorded cost.
On Enter: clear log, "switch to the game window", `Sleep(2000)`, `WindowHidden`,
`BlackMarketSeller::Run`, `holdings.Save`.
Reads `holdings.tsv`, **not** the order book. §5.3.

### Scan
→ `Scan`. Category boxes toggle green (`selectedScan`, a `set<string>` of keys).
Buttons: `SCAN BM`, `SCAN MARKET`, `CLEAR` (clears the selection only), `BACK`.
Category buttons are labelled and matched by `menuSub` (I8).

### Settings
→ `Settings`. Edits `scan_profile.tsv`, **saved on every click**, no apply step.

| Control | Effect |
|---|---|
| `BLACK MARKET` / `CITY MARKET` | which profile is being edited (`settingsBlackMarket`) |
| category box | selects which category the pickers act on |
| `T1`..`T8` | toggle tier in the selected category |
| `E0`..`E4` | toggle enchantment. **City only** — BM buttons are drawn inert and ignored, because the scanner drops the setting anyway |
| `ONE PASS (NO TIER)` | clears the tier list |
| `APPLY TO ALL` | copies current category's tiers **and** enchants to every category **in the current tab only** |

**Non-obvious:** an empty tier list does not mean "skip". It means *scan in one
pass without opening the tier dropdown* — the black market default. To exclude a
category from a run, untick it on the **Scan** screen.

**Consequence reaching the buy side:** `Game::AllowedCityTiers()` unions the CITY
profile's tiers across all categories into `BuyPlan::allowedTiers`, a hard gate.
**If any one category is ONE PASS the union is empty, meaning no restriction at
all** — a one-pass scan brings back every tier, so claiming otherwise would be a
lie about the data.

### Plan
Dry run of BuyCity. Sets `pendingBuyDryRun = true`, walks the **same four
screens**, and at step 4 calls `RunPlan` instead of `RunBuy`. Prints the table
plus a totals line; buys nothing. Budget `0` = no limit. Ends on the `Shop`
screen with the table still in the log behind it.

### Deals
No prompt. `BuildPlan(0, Config.MinProfitRate, Config.MinProfitSilver,
Config.RequireBothProfits)` — the whole gate from `Config.txt`, no budget.
Prints the ranked list, spends nothing, and ends on the `Shop` screen like Plan
does. It is the "what is out there right now" button, so it is the one most
likely to be pressed by someone deciding what to buy by hand; leaving the Shop
screen showing a previous run's list would have been worse than showing
nothing, because a stale list still looks current.

`Plan` with budget 0 == `Deals` with your own thresholds typed in.

### BuyCity
Sets `pendingBuyDryRun = false`, then:

| Step | State | Meaning |
|---|---|---|
| 1/4 | `EnterWndBuyProfit` | min margin %, → `pendingBuyProfitRate = v/100` |
| 2/4 | `EnterWndBuyMinProfit` | min **total** net profit per lot, silver. `0` skips step 3 |
| 3/4 | `EnterWndBuyCombine` | `AND` / `OR` buttons → `pendingBuyRequireBoth` |
| 4/4 | `EnterWndBuy` | silver budget. `0` = no limit |

Every label is prefixed `PLAN:` or `BUY:` — four identical prompts where one
spends real silver is a thing to get wrong once.

Then: print plan, "switch to the game window", `Sleep(2000)`, `WindowHidden` +
`Abort::Guard`, `CityBuyer::Run`, `holdings.Save`, **`store.ClearCities()`**,
then `CityBuyer::Shortfall` and the `Shop` screen — or back to `Menu` when the
run bought everything it planned.

**`store.ClearCities()` means the city half must be re-scanned before another
BuyCity.** The black market half survives, because SellBM needs it.

### Shop
The hand-shopping list. One entry per **market search** — item + tier +
enchantment, which is exactly what `searchText` spells — rather than one per lot,
ranked by the profit of the whole block. Clicking a block puts its search text on
the clipboard (`sf::Clipboard`) and dims it, so a list can be worked top to
bottom across alt-tabs. Mouse wheel scrolls; `Escape` and `BACK` both return to
`Menu` rather than closing the program.

**Nothing is filtered off the list.** A block of one unit is still a thing to
go and buy, and `ShopListTest` holds that: a one-unit lot, sixty items in and
sixty blocks out, and a one-unit shortfall all have to survive to the screen.
The log panel underneath is no longer capped at twenty lines either - it shows
as many as fit, because twenty quietly cut the bottom off any plan with more
than about seventeen lots.

The button itself **reopens the last list, it never rebuilds one.** Rebuilding
would answer a different question: `store.ClearCities()` has already run by then,
so a fresh plan comes back empty and would read as "the shortfall is bought".

Blocks that merge several quality bands show the **lowest** accepted quality and
the **lowest** price ceiling, and both have to be the lowest one. A block holding
an order for Good (ceiling 12,450) and one for Excellent (ceiling 20,100) that
advertised Good next to 20,100 would invite buying a Good at 18,000 — which
cannot fill the Excellent order, and sells into the Good one at a loss. Covered
by `ShopListTest`.

The price shown is `FlipLot::marginCeiling`, not `maxBuyPrice`: the question a
person is answering while shopping by hand is "may I pay this?", and
`maxBuyPrice` answers "did the plan pay this?", which is a narrower and
different thing. See §6 stage 2b.

### Capture
`RestartFeedClient()` — kills every capture client, starts `run-feed.bat`
(elevated; Npcap needs Administrator). Sets `lastCaptureRestartTick`.
The single most common cure for "scan returns 0 orders".

### Clear
Total wipe, in this order: snapshot counts → `store.Clear()` →
`scanProgress.Clear()` → `holdings.Clear()` + `holdings.Save()` →
`feed.Drain()` (else the next `Absorb` refills the store with what was just
cleared) → `remove("_save_data.txt")`, `remove("ocr_miss.png")` →
`GlobalLog.Wipe()` → print summary.

Prints a loud warning when the ledger was non-empty: **the goods do not leave the
game bag when the ledger does.** SellBM will no longer touch them.

### Exit
`window->close()`.

### Scanning screen
Category boxes recoloured: grey = not in this run, amber = running, green = ok,
red = failed. Red means the category menu never opened.
`RETRY FAILED` re-runs exactly those steps.

Progress % is deliberately approximate — each category is an equal slice
regardless of tier count. **The bar is not animated**: the window is minimised
(I3) and the scan blocks the event thread (I4), so it shows only the final state.

---

## 5. Click sequences

All coordinates 1920×1080 base. `MoveAndClickRect` picks a **random point inside**
the rectangle (`ClickInside`), travels a curved eased path (`HumanMove`), clicks.
`MoveToRect(rect, holdRowY = -1)` does the same without clicking and returns the y
it used; passing `holdRowY` varies x only.

### 5.1 Scan — `Game::RunScan(blackMarket)` → `LaunchScan` → `MarketScanner::ScanCategory`

```
per category:
  1  ClickAndSettle(resetTabs 1334,271-1336,273, watch typeList)
        clears whatever tabs the previous category left open.
        Done up front, not as cleanup, so a category that fails partway
        still leaves the next one a known state.
  2  ChooseCategory:
       a  ClickAndSettle(typeButton 759,268-875,278, watch typeList)
       b  rowY = MoveToRect(entry.parent)          hover, does NOT click
       c  hash subList  (baseline BEFORE the hover that opens it)
       d  MoveToRect(entry.sub, rowY)              slide right, same row
       e  SettleRegion(subList)
       f  MoveAndClickRect(entry.sub)
     failure here = every planned step for the category reported failed
  3  if tiers empty  (black market default):
       PageThroughList(40)
     else per tier:
       ChooseTier(tier)                            measured coordinate, no OCR
       if enchants set (city only):
         per level: ChooseTier(tier) AGAIN, ChooseEnchantment(level),
                    PageThroughList(40)
       else:
         PageThroughList(40)
after all categories:
  store.Absorb(feed)
```

Submenus open **on hover, not click** — clicking the parent collapses the menu.
That is why 2b/2d exist and why 2d must hold `rowY`: leaving the row closes it.

The tier dropdown is **reopened for every enchantment level**, not once per tier.
It costs one menu click and stops a tier filter the game quietly dropped
mid-walk from poisoning every level after it.

Enchantments are split out because one tier of a popular item overruns the game's
~20-tab cap when every level is listed together, and the overflow is dropped
silently. `feed.LastOrderEnchant()` is the cheap check that the filter took.

`SettleRegion` has a **floor** as well as a timeout: the cursor travelling over
the panel highlights a row, which is a real repaint, so two frames either side of
that highlight can be legitimately identical while the dropdown has not begun to
draw. `ChooseCategory` is called once per category with nothing to retry, so
returning early costs the whole category while waiting costs milliseconds.

#### `PageThroughList` — the pager loop

```
loop up to maxClicks (40):
  before        = feed.MessagesSeen()
  ordersBefore  = feed.OrdersSeen()
  MoveAndClickRect(nextPage 986,882-997,892)
  clickedAt = now                    ← timed AFTER the click lands, not before;
                                       HumanMove spends up to move_max_ms first,
                                       and a deadline started ahead of it can be
                                       most of a second stale
  sleep(page_click_delay_ms)
  budget = PageBudgetMs(...)         ← from observed latency, capped at
                                       page_timeout_ms
  wait until MessagesSeen() > before, or budget expires

  if got data:
     observedPageMs = (observedPageMs*3 + elapsed)/4    ← measured BEFORE
                                       settling, so it keeps meaning "time to
                                       first packet"; timing it after would
                                       inflate every future budget by a page
     ++pages; quiet = 0
     if page_size > 0:
        settle: extend while packets keep arriving, stop at one quiet interval
        onPage = OrdersSeen() - ordersBefore
        if 0 < onPage < page_size:  → listing ended, break
  else:
     if ++quiet < page_quiet_clicks: retry the click
     else: break, printing why
```

Two independent stop conditions:

- **Short page** (`page_size`, default 50). A tab holding fewer than 50 rows is
  the last one. Positive evidence, so it needs no confirming.
  **The settle wait is mandatory:** the wait above returns on the *first* packet
  and a page arrives in several, so counting there would see a fraction of 50,
  which looks exactly like a short page and would end the listing early and
  silently. `onPage == 0` is explicitly **not** treated as short — that is
  something else, and the quiet-click test is the honest way to handle it.
- **Quiet clicks** (`page_quiet_clicks`, default 2). One is not enough: a slow
  round trip is indistinguishable from the end of a listing.

### 5.2 Buy — `CityBuyer::Run(lots, settings, holdings)`

The lot is filled from **one quality band at a time**, working down
`lot.qualityOrder` — see §6 stage 2b for why that list exists and §9.21 for what
it fixes. `remaining`, `blindBuys` and `shiftedRows` live across the bands
because they describe the lot; `live`, `mark`, `rounds` and `retries` are
per band.

```
per lot, in plan order (biggest total profit first):
  skip if searchText empty            → "no name for X, skipping"
  skip if doneKeys has (id,ench,qual) → already bought this run
  skip if lot.spend > left            → budget

  for each band in lot.qualityOrder, while remaining > 0:
    ceiling = band 0 ? lot.maxBuyPrice
                     : max(lot.maxBuyPrice, lot.marginCeiling)
    SelectQuality(band)               quality button + row, both measured.
                                      Deliberately NOT skipped when the quality
                                      matches last time: nothing verifies that
                                      the filter survives a search + clear.
  SearchFor(lot.searchText, &mark):
      ClearItemName()   click 722,270
      sleep(menu_open_delay_ms)
      feed.Drain(stale)               ← bin what the clear provoked
      mark = feed.MessagesSeen()      ← AFTER the drain. This ordering IS the
                                        fix for §9.12
      MoveAndClickRect(search 586,264-697,277)
      TypeString(text)                one SendInput per char (below)
      sleep(menu_open_delay_ms)

    read-back, up to search_retries (3):
        WaitForRefresh(mark, wait_timeout_ms)
        mark = MessagesSeen()
        pullLive()                            → matched: proceed
    nothing matched → try the NEXT BAND, not the next lot

  after every band: bandsUsed == 0 → "gone from the market in every
                                     quality that fits", vanished++

  buy loop, up to lot.quantity*2 + 8 rounds:
      top = live.front()                      cheapest live offer
      if top.price > ceiling          break   ← not fatal: the next band
                                                may still be cheaper
      if top.price > left             break
      take = min(top.amount, remaining, left/top.price, buy_units_per_click)
      feed.DrainPurchases(stale)              ← so an old confirm is not read
                                                as this one's
      mark = MessagesSeen()
      BuyTopRow(take):
          MoveAndClickRect(buyFirstRow 1232,424-1314,440)
          if take > 1: amountField 567,570-583,578, Ctrl+A+Del, type
          MoveAndClickRect(confirmBuy 844,722-922,739)
          MoveAndClickRect(levelWarning 819,545-899,555)  ← harmless when absent,
                                                            essential when not
      WaitForPurchase(top.id, purchase_confirm_ms)
```

`WaitForPurchase` shortens its deadline once the *request* packet is seen: the
server answer is one round trip behind, so holding the full budget is time spent
on a deadline that no longer applies. This path runs once per unit — at four
seconds, a lost confirmation used to stall a twenty-unit lot for over a minute.

| Outcome | Handling |
|---|---|
| `Confirmed` / `Sent` | count, `holdings.Add` **immediately** (an interrupted run still has the goods in the bag), decrement the local row, refresh if the game re-sent |
| `None` | click never reached the game. Re-click up to `buy_retries`, then leave the lot |
| `Elsewhere` | bought a different order id — below |

**`Elsewhere` is the normal weather of a busy market, not a fault.** The aimed row
was taken, the one below slid up, the click bought that. Handling:

1. look `boughtOrderId` up in `live` for its price
2. record to `holdings` regardless — the goods are in the bag either way. Unknown
   cost is handled downstream: `BlackMarketSeller` sells those to any positive bid
3. remove those units from the local copy, or the next round aims at a row that is
   already gone and shifts again for the same reason
4. `paid == 0` (not in the listing → margin never checked): log, count, abandon
   the lot after `stray_buy_limit` (3)
5. `paid > lot.maxBuyPrice`: real margin breach, stop the lot
6. otherwise: count in `shiftedRows`, **carry on** after re-reading

End of lot prints one line only when there is something to say:
`12/12 units, 11 rows shifted under the click`.

Only the **top row** is ever clicked. The list is price-ascending and buying it
out slides the next row into the same coordinates, so a sweep is one button
pressed repeatedly — no per-row coordinates, no scrolling.

`buy_units_per_click` stays at 1: the buy dialog only grows a quantity field when
the row itself holds more than one unit, and typing into a dialog that has no such
field derails everything after it (§9.9).

`TypeString` sends **one `SendInput` per character**. A whole-string call looks
like a paste at OS level; the text lands but the search box's autocomplete never
opens, because nothing fired the per-keystroke event. `type_char_delay_ms` must
stay above 0.

### 5.3 Sell — `BlackMarketSeller::Run(plan, holdings)`

Walks the **ledger**, not the order book. Walking the order book meant searching
for items the character does not own and clicking sell on whatever row was there.

```
Absorb()                                  drop stale feed data
per stack in holdings:
  minBid = ceil(unitCost * (1 + minProfitRate) / 0.96)
  typed  = SellSearchText(stack.searchText)  ← " T6.0" → " t6.0", lower-case t.
                                               ONLY that letter; a blanket
                                               tolower() would flatten the item
                                               name, which is what the game
                                               matches on
  up to 12 rounds while units remain:
      before = MessagesSeen()
      ClearItemName(); click search 586,264-697,277; TypeString(typed)
      sleep(menu_open_delay_ms)           ← the feed can have the prices before
                                            the client has repainted the rows;
                                            clicking into a list that is still
                                            the previous item's is how a sell
                                            lands on the wrong row
      WaitForRefresh(before)  → none: give up on this stack
      SelectQuality(stack.quality)
      WaitForRefresh(...)                 best effort; the game does not always
                                          re-request after a filter change
      feed.Drain(fresh)
      pick best bid where: isBuyOrder, location 3003, groupTypeId+enchant match,
                           stack.quality >= order.quality
      no bid           → vanished++,  next stack
      bid < minBid     → tooCheap++,  next stack
      same order id AND amount not smaller than last time
                       → "sale did not register", leave the stack
      qty = min(remaining, order.amount)
      MoveAndClickRect(sell 1238,427-1323,442);      sleep
      MoveAndClickRect(confirmSell 848,725-919,735); sleep
      holdings.Remove(...); report.silver += price*qty*0.96
```

One sell click moves the **whole stack**, capped by what the order still wants.
`maxRounds = 12`, not `quantity + 8` — that older bound came from wrongly
believing a click sold one unit.

Clearing the search box first matters as much as on the buy side: without it the
box still holds the previous stack's name and the new text is appended, so the
first stack searches fine and every one after it searches for nonsense — which
reads as "no bid left for it" rather than as a typing bug.

---

## 6. The arithmetic — `CityBuyer::Plan`

Market rules encoded, all confirmed and load-bearing:

- **Quality:** the black market accepts **equal or higher** than asked.
  `QualityFills(offered, wanted) { return offered >= wanted; }`
- **Sale tax 4%:** `NetAfterTax(p) = (long long)(p * 0.96)`
- **No setup fee** — the bot takes existing offers rather than posting an order.
  Posting costs 2.5% of order value *even if it never fills*, which is why "post
  an aggressive order to cross the spread" was rejected.
- Fills happen at the **seller's** price.

### Stage 1 — hard gates (reject outright)

| Gate | Line | Rejects |
|---|---|---|
| junk | `CityBuyer.cpp:159` | `price <= 0`, `amount <= 0`, wrong side |
| `maxUnitPrice` | `:162` | any offer over the per-unit ceiling |
| `allowedTiers` | `:170` | tier outside the CITY scan profile — §4 Settings |
| quality | `:27` | decides which orders an offer may fill |

Tier is rejected **at the point offers enter the plan**, not filtered out of the
results afterwards: a lot that was never built cannot be bought by mistake, and it
keeps the "nothing worth buying" count honest.

City offers are grouped by `(groupTypeId, enchant, quality)`.

> **Use `groupTypeId`, never `itemTypeId`.** The feed sends both: `itemTypeId`
> carries the enchant suffix (`T6_MAIN_SWORD@2`), `groupTypeId` does not. Enchant
> is stored separately, and every later comparison — `CityBuyer::Run` matching
> live offers, `BlackMarketSeller` matching bids against the ledger — tests
> `groupTypeId`. Storing the suffixed form made every enchanted lot report "gone
> from the market" and every enchanted stack unsellable. Plain items have no
> suffix, which is what hid it.

### Stage 2 — demand is a finite pool

Black market orders are indexed by `(groupTypeId, enchant)`, **all of them**,
sorted price-descending, each carrying `left = amount`.

Quality bands compete for that pool. Planning order, `CityBuyer.cpp:220`: sort by
`(itemTypeId, enchant, cheapest offer price, quality)` — **the band that can fill
an order most cheaply claims it first**, which is also the band that earns most on
those units.

Greedy, not optimal: a cheap high-quality band can take an order a lower band
could also have filled, leaving that band's own order unmatched. A min-cost flow
would pair them better. The error is always in the safe direction — it plans
slightly fewer units than the market could absorb, never more.

### Stage 2b — the fallback list

A lot also records **every** band that could fill its orders, cheapest planned
supply first, in `FlipLot::qualityOrder` (planned band always first). "Could
fill" is decided by the ORDER's quality, never the band's: an order for Good
accepts Good and everything above it, so a band below `bmQuality` is never
listed — buying one would leave a stack with no buyer.

`FlipLot::marginCeiling` goes with it: `NetAfterTax(cheapest bid in the lot) /
(1 + minProfitRate)`. Cheapest rather than average, so it holds for every unit
individually whichever order that unit ends up against.

`maxBuyPrice` cannot do that job. It is the dearest offer of the band that
happened to be planned, and a fallback band is normally *dearer* — the cheap one
is the one that got planned — so `maxBuyPrice` would refuse every fallback
before it was looked at. See §9.21.

### Stage 3 — pairing sweep (tentative, claims nothing)

Offers ascend, orders descend, units pair off. Stops when demand is exhausted
**or** `NetAfterTax(bidPrice) <= offerPrice`. `break`, not `continue`: because the
two queues move in opposite directions, once a unit loses money every later one
does too.

### Stage 4 — choosing the depth

Walk every prefix `k` of the sweep:

```
avgBuy = runSpend / runQty
avgNet = runNet   / runQty          break if avgNet <= avgBuy
rate   = (avgNet - avgBuy) / avgBuy
profit =  runNet - runSpend

byRate   = rate   >= minProfitRate
bySilver = profit >= minProfitSilver
accept   = minProfitSilver <= 0 ? byRate
         : requireBoth        ? (byRate && bySilver)
         :                      (byRate || bySilver)
```

Keep the accepted prefix with the greatest profit; **strictly** greater, so a tie
keeps the shallower one (same profit, less silver, less clicking).

> Taking more units raises total profit and lowers the margin. There is no
> objectively best depth — it depends which threshold is being optimised. So the
> planner walks the whole sweep and picks, rather than stopping where the rate
> crosses. Under OR the answer can be much deeper than the rate alone allows;
> under AND, shallower.

Only then is the depth committed: `pool[pick].left -= units`, ladders built.

`FlipLot` carries two ladders — `ladder` (offer price, units) cheapest-first and
`revenueLadder` (bid price, units) dearest-first. `bmPrice` is the weighted
average across orders filled; `PrintPlan` says so when any lot spans more than one.

### Stage 5 — ranking

`profit` descending, ties by `spend` ascending. **This is the order `Run` buys
in**, unchanged by anything downstream.

Not per-silver, deliberately: that reasoning holds only when the budget is the
only thing that runs out. In practice the offers get taken by other players while
the bot works through a list of tiny high-percentage flips, and the deal worth
200k is exactly the one that disappears.

### Stage 6 — budget trim

Walk in rank order. Whole lot if it fits; otherwise walk `ladder` cheapest-first
taking what the remaining silver covers. A trimmed lot keeps the **cheapest**
units, which are paired with the **dearest** orders, so it is re-costed from
`revenueLadder` — not from the whole lot's average, which understated both how
many units fit and what they earn. The gate is re-applied to the trimmed lot;
failing it **skips that lot and continues**, so the silver goes to the next one
that fits.

### No margin ceiling

There is no `absurdProfitRate`. A wide gap is what a real bargain looks like; what
makes a pairing trustworthy is the arithmetic around it (item + enchant matched,
quality filtered, ladder swept cheapest-first), not the size of the number.

---

## 7. Configuration (`SFML/Config.txt`)

Every key falls back to its default if missing or unparseable — a broken config
never stops startup. `ToInt` is hand-rolled (≤9 digits, digits only) because
`stoi` used to throw before `main()`.

| Key | Default | Meaning |
|---|---|---|
| `screen_resolution` | 1920, 1080 | drives `ScreenScale` |
| `tessdata_path` | — | vestigial; OCR is dead (I5) |
| `click_hold_ms` | 25 | mouse button held down |
| `click_delay_ms` | 50 | pause after each click |
| `type_char_delay_ms` | 10 | between keystrokes; **must be > 0** |
| `poll_interval_ms` | 15 | wait-loop granularity; clamped ≥ 1 |
| `wait_timeout_ms` | 1200 | ceiling on a redraw wait |
| `menu_open_delay_ms` | 350 | **ceiling** on a dropdown wait, not a sleep |
| `page_timeout_ms` | 1200 | **ceiling** on the per-page wait, not a sleep |
| `page_click_delay_ms` | 40 | extra pause after a tab click. Tuned ~60ms ping; ~100 for ~270ms |
| `page_quiet_clicks` | 2 | silent clicks meaning "listing ended"; clamped ≥ 1 |
| `page_size` | 50 | rows in a full tab; a shorter tab ends the walk. 0 = off. **Too low is the dangerous direction** — a full page would read as short and the rest be dropped |
| `move_ms_per_100px` | 45 | cursor travel |
| `move_min_ms` / `move_max_ms` | 150 / 700 | travel clamps |
| `move_jitter_px` | 3 | path bow/wobble; 0 = straight line |
| `move_target_slop_px` | 3 | aim error **on bare points only** (I7) |
| `buy_units_per_click` | 1 | leave at 1; clamped ≥ 1 |
| `purchase_confirm_ms` | 1500 | wait for the game's own buy packet |
| `buy_retries` | 2 | re-clicks on an unacknowledged row |
| `search_retries` | 3 | re-reads of a searched listing before "gone"; clamped ≥ 1 |
| `stray_buy_limit` | 3 | unpriced purchases absorbed per lot |
| `budget` | 0 | Deals/Plan only; the buttons ask and that wins |
| `min_profit_percent` | 20 | Deals only |
| `min_profit_silver` | 0 | Deals/Plan floor on total profit per lot |
| `profit_combine` | and | `and`/`or`; anything else keeps the current value |
| `max_unit_price` | 0 | 0 = no ceiling |
| `abort_hotkey` | ctrl+shift+q | emergency stop; unparseable values keep the default |

`page_timeout_ms` and `menu_open_delay_ms` are **ceilings on a poll**, not sleeps:
menus are hashed until they stop repainting, and the page wait is derived from
latency measured during the run.

---

## 7b. The emergency stop

`AbortSwitch.h/.cpp`. `Ctrl+Shift+Q` by default, set with `abort_hotkey`.

**It cannot be an SFML key event.** A run blocks the event thread from start to
finish (I4) and the window minimises itself (I3), so nothing is pumping the
message queue and the keystrokes are going to the game anyway. It is read with
`GetAsyncKeyState` from inside the loops. The consequence: the game receives the
keystroke too, which is why the default is a chord and not `F12`.

**Noticing and acting are separate, and that separation is the design.**
`Abort::Requested()` latches — the first poll that sees the chord down sets a
flag and every later call returns true without polling. So the poll can live in
places where stopping would be unsafe, while the `break` happens only where
nothing is half-finished:

| Polled (latch only) | Acted on (breaks) |
|---|---|
| `CityBuyer::WaitForPurchase` | between lots, and after a lot completes |
| `CityBuyer::WaitForRefresh` | between quality bands |
| `PageThroughList` budget + settle waits | **before** each Buy click, never after |
| | top of each pager click |
| | between categories, tiers and enchant levels |
| | between stacks in `BlackMarketSeller::Run` |

**Never break between a Buy click and its confirmation.** The click may already
have been acted on by the game, and walking away from the answer is how the
ledger loses track of goods that are really in the bag — the same class of
mistake as §9.9. `WaitForPurchase` therefore latches and returns normally; the
run leaves at the end of that lot.

Two waits latch rather than break for a second reason: returning early would
hand the caller an observation that never happened. `WaitForRefresh` would
report "no data arrived", which the caller prints as "gone from the market"
(§9.12), and `PageThroughList`'s settle wait would make a full page look short,
which silently drops the rest of a listing (§5.1). An invented observation is
worse than a second of waiting.

`Abort::Guard` is RAII, modelled on `WindowHidden`, and wraps every run in
`Game.cpp`. `Arm()` will not watch until the chord has been released once, so
stopping one run cannot instantly stop the next one started while the keys are
still down.

Stopping is not pausing. Everything bought stays bought, `holdings.Save` runs
exactly as it would after a finished run, and BuyCity still hands the unbought
remainder to the `Shop` screen — titled `STOPPED - NOT BOUGHT` so it cannot be
mistaken for a completed run's leftovers.

---

## 7c. The UI kit

`UiKit.h/.cpp`. Everything visual goes through it; `Theme.h` is the palette.

Design file: https://www.figma.com/design/MKFHfQnaJVt3b2eplAkl4s - foundations,
components and three assembled screens. The `Theme` variable collection there is
named after the C++ symbols and its Dev Mode "iOS" code syntax prints
`Theme::Ok`, so a colour change has exactly one place to land on each side.

**Metrics are iOS HIG values, not measured from Apple's file.** The file has the
official iOS UI kit subscribed, but a Starter plan refuses
`importComponentByKeyAsync` (`Not permitted to upsert from library`), so nothing
could be measured off it. Row 44px, side inset 20, row inset 16, nav 44, chip
206x32 on a 42px pitch, group radius 12.

**Two things that will bite anyone editing the Figma file.** A paint returned by
`setBoundVariableForPaint` is frozen, so assigning `.opacity` to it does
nothing; put the opacity in the paint you hand IN. And an instance does not
preserve a paint's opacity at all. Both produced solid blocks of colour with
invisible text, twice, and only a screenshot caught either. Every tint is a
precomputed SOLID variable now - which is also what SFML wants, since it has no
compositing model to lose one in.

**Fonts.** Inter, four static weights, shipped under SIL OFL. SFML loads one
face per weight; there is no variable-font axis to reach for, which is why
Windows' own `SegUIVar.ttf` was not an option. `Ui::LoadFonts` falls back to the
mono face for any weight that fails rather than drawing nothing.

---

## 8. Coordinates (1920×1080, DPI-aware)

Format `x1,y1-x2,y2`. Clicks land on a **random point inside**.

```
Scanner                 MarketScanner.h
  typeButton     759,268-875,278    opens the item-type dropdown
  typeList       759,268-876,502    hashed while the top level opens
  subList        889,323-1007,890   hashed while the submenu opens
  resetTabs     1334,271-1336,273   clears open tabs before a category
  nextPage       986,882-997,892    pages the result list

Shared dropdowns        MarketUi.h
  tier button    903,268-1015,275   only 7px tall — see I7
  tier rows      904,401 .. 901,521 T4..T8 ONLY. T1-T3 are not measured and
                                    Row() returns NULL rather than extrapolate
  enchant button 1050,268-1152,276
  enchant rows   1052,320 .. 1050,442  E0..E4; "All" deliberately absent —
                                    picking it produces the over-long listing
                                    this filter exists to avoid
  quality button 1192,266-1300,278
  quality rows   1192,319 .. 1197,438  Normal..Masterpiece

City buying             CityBuyer.h
  search         586,264-697,277    type "Boltcasters T4.2"
  buyFirstRow   1232,424-1314,440   the only row ever clicked. Bottom edge
                                 raised from 442; the old box overhung the
                                 button by 2px and clicks landing there were
                                 dropped - see the note in CityBuyer.h
  amountField    567,570-583,578    only touched when take > 1
  confirmBuy     844,722-922,739
  levelWarning   819,545-899,555    "may not be able to use"; empty when absent
  clear search   722,270            ClearItemName()

Black market selling    BlackMarket.h
  search         586,264-697,277
  sell          1238,427-1323,442
  confirmSell    848,725-919,735    always appears, always clicked
```

**`levelWarning` — do not extend downward past y=558.** The 8px band at
`830,566–866,585` was flagged by hand as unsafe to click. As measured (558 vs 566)
nothing needs changing; this note exists so a future "widen the target a bit" edit
does not walk into it blind.

### Category menu — `CategoryLayout`, 46 entries

Both columns draw on a fixed **27px pitch** regardless of content. That is why the
whole table is one grid rather than 46 independent measurements, and why Capes
could be derived and then checked against a screenshot.

```
parent rows:  Weapons 324 · Chest 351 · Head 378 · Foot 405
              Off-Hands 432 · Capes 459 · Bags 486
submenu:      each submenu's "All" lines up with its parent;
              real rows start one pitch below
boxes:        x 790-845 (left), 920-995 (right); y = centre ±2
```

**Boxes are wide in x and tight in y, and the asymmetry is the point.** The table
was `2×2` px, which `ClickInside` reduces to nine pixels — a fixed aim point in
all but name. The stated reason was the 27px row pitch, but that constrains **y**
alone: the columns are ~130px wide and nothing sits beside a row to hit by mistake.

Widening the boxes alone changed nothing, because `ChooseCategory` hovered the
**exact centre** of the parent rect via `MoveToScreenPoint` with coordinates the
caller had already averaged to a point. `MoveToRect` exists for that (§9.19).

Deepest row is `capes_smuggler` at y=864, which is why `subList` reaches 890.
`capes_demon` (837) and `capes_smuggler` (864) are **derived beyond the deepest
row ever measured** (783, `weapons_shapeshifterstaff`). Screenshot-checked, not
`FindCoords.ps1`-checked.

---

## 9. Bug history

Each entry: what it looked like, what it actually was, the fix, the invariant.

### 9.1 GUI binary silently discarded all output
`SubSystem` Console→Windows. `CityBuyer::PrintPlan` used `printf`; only
`std::cout` is redirected to the panel. The whole plan table was formatted and
thrown away, and three `printf("SendInput failed…")` calls were swallowing exactly
the message that says a click never reached the game.
**Fix:** `snprintf` into a buffer, `cout << buffer` (keeps column alignment). → I1

### 9.2 `system("cls")` ×13
Caught before shipping. Each spawns a `cmd.exe` in a GUI binary — a console flash
per button press, the exact thing the change removed.
**Fix:** `GlobalLog.Clear()`. → I2

### 9.3 The window ate its own screen reads
Window grew 500×500 → 900×680. The read regions are ~x 759–1020, y 268–521 in
*screen* coordinates; a 900×680 window centred on 1920×1080 spans x 510–1410,
y 200–880 and covers them completely. Scans reported success and returned 0 orders,
because the bot was reading its own category list.
**Fix:** `WindowHidden` RAII guard, scoped so an early `return` still restores.
Applied to scanning, buying **and** selling. → I3

### 9.4 Threading the scan
Scan moved to a `std::thread` so the progress bar could animate. Suspected timing
irregularities in mouse movement; reverted. A 60fps render loop competing with
`Sleep()`-paced `SendInput` is not worth it.
**Accepted cost:** the window freezes during a scan and the bar shows only the
final state — invisible anyway, since the window is minimised. → I4

### 9.5 "albiondata-client is running" proved nothing
Scans returned 0 orders. `EnsureAlbionDataClientRunning()` only checked whether a
process by that name existed. A stale instance — running **without**
`-p http://127.0.0.1:5000` — satisfied that perfectly while posting elsewhere.

Proven, not guessed: (1) `ocr_miss.png` timestamp stale → the reader never missed;
(2) no "could not find" lines in `bot.log`; (3) `netstat` showed the app LISTENING
on 5000; (4) a hand-crafted POST of a fake order returned HTTP 200 → the whole
receive path works; (5) therefore only the sender was left. `adc.log` confirmed the
app-launched instance had exited.

**Root cause:** the machine's normally-installed Albion Data Client had registered
a **Windows Scheduled Task** (`Albion Data Client`, args `-minimize`, no `-p`),
firing at logon from `C:\Program Files\…` — a different path from the bot's copy.
`LastRunTime` matched the rogue process exactly.

**Fix, four layers:** kill in the elevated `run-feed.bat` (an unelevated process
cannot kill an elevated one); always take ownership rather than skipping; a
**Capture** button plus `LaunchScan` self-heal when `MessagesSeen() == 0` and
capture was not restarted in the last 60s; the scheduled task disabled.

**Lesson:** when an external dependency can be started by someone else, do not
detect it — **own it**. Check the OS's autostart surfaces (Run keys, Startup,
Services, **Scheduled Tasks**) before calling a process unexplainable. And verify
the receiver end to end first: it is cheap and decisive.

### 9.6 Bulk-delete near-miss
Deleting the old OCR price-database generation (`DataBase`, `Item`, `Items`,
`MarketAction`). An automated dependency audit classified `ClearItemName()` as
old-only; it is called by `CityBuyer::SearchFor`, the live buying path.
Also: `LOG_DURATION` had been reaching `Game.cpp` *transitively* through the
deleted `MarketAction.h`; adding `#include "profile.h"` fixed the build but created
an ambiguity between `sf::milliseconds` and `std::chrono::milliseconds`
(`profile.h` does `using namespace std::chrono`, `MyImg.h` does
`using namespace sf`). Resolved by including `profile.h` **first**. → I6

### 9.7 The log panel ate the buttons
Enlarging the panel to 300px covered `BACK` and the progress bar.
**Fix:** `BACK` pinned top-right on every screen, window to 820px, panel pinned
below the last row of controls.

### 9.8 Small ones
- **`EnterWndBuy` was never drawn** — no branch in `render()`. Pre-existing,
  invisible until BuyCity was used. → §3's three-edit rule.
- **Any stray click wiped the log** — `MenuConroller` called `GlobalLog.Clear()`
  before checking whether a button was hit.
- **Running the exe directly failed** — everything loads by relative path.
  `main()` now derives the folder from the executable path.
- **DLLs were never copied** — the exe only ran via `START.bat` or the IDE.
  Post-build step copies SFML/OpenCV/vcpkg DLLs next to it. **The Windows loader
  resolves imports before `main()`, so no in-program `PATH` fix can work.**

### 9.9 "purchase did not register" — five things that were five rows
Every BuyCity run: `Buying 5x Satchel of Insight` → `purchase did not register` →
`0 units in 0/2 lots, spent 0 silver`.

What was actually on screen: five **separate listings of one unit each**, from five
sellers. That is the normal shape of a market list.

Two faults behind one message:
1. **The quantity box.** A buy dialog only grows one when the row holds more than
   one unit. `BuyTopRow` typed the amount whenever `amount > 1`; for a row of one
   the keystrokes went elsewhere and derailed the following clicks.
2. **The success test was a guess.** "Did the list get shorter?" needs the game to
   re-request after every purchase. When it does not, a working purchase reads as
   failure — and the code abandoned the whole lot on that reading. Worse,
   `refreshLive()` cleared the list first and refilled from whatever the next drain
   held, so an unrelated batch could empty it and report "gone from the market"
   about goods still visible.

**Fix:** `buy_units_per_click = 1`, so the dialog's shape stops mattering entirely;
and the game is *asked* instead of guessed at — `AuctionBuyOffer` carries the order
id and unit count, which is partly why AlbionFeed exists. `pullLive()` only
replaces the working list when the batch really contains offers for the item. → I10

### 9.10 OCR removed from tier/quality, then from everything
Tesseract read "Tier 5" back as "Tier S" often enough that whole tiers were skipped
silently, and the retry loop existed purely to paper over misreads. The rows never
move, so there was nothing to discover. Category menus followed for the same reason
— both columns are on a fixed 27px grid.
**Accepted cost:** a tier click can no longer be verified. If the dropdown failed
to open, the pass runs against whatever tier was selected; the per-tier order counts
printed by `ScanCategory` are what show that up. → I5

### 9.11 `Elsewhere` abandoned whole lots
Log evidence: plan 21,999,567 silver / 33 lots → **actually spent 2,390,569
(10.9%)**. Ten lots bought exactly **one** unit and stopped, on
`bought order X rather than Y - the list shifted under the click, stopping this item`.

**Cause:** another player taking the aimed row is the ordinary weather of a busy
market. The stated reason for stopping — "its price was never checked against the
margin" — is true only when the bot *cannot price it*, and usually it can: the row
is in the listing it already holds.

**Fix:** price it. Within `maxBuyPrice` → record, re-read, carry on. Over it → real
breach, stop. Not priceable → count, cap at `stray_buy_limit`.

**Result, next run:** 47 units in **13/13** lots, 17,825,223 of 17,979,908 planned
= **99.1%**. 34 units came through the `Elsewhere` path — every one of them would
have been discarded by the old code.

### 9.12 "gone from the market" about goods on screen
**19 of 33 lots** in one run, plus 1 `no fresh data after the search`.

**Cause:** `SearchFor` calls `ClearItemName()`, and that click makes the game
re-send the whole **unfiltered** listing (so does the quality filter set just
before it). The caller's `mark` was taken *before* those clicks, so
`WaitForRefresh` was satisfied instantly by the wrong packets and the drain that
followed handed back the unfiltered listing. Nothing in it matched the item, and a
single non-retrying drain called that "gone".

**Fix:** `SearchFor` bins the stale orders and reports the feed position *after*
them (`mark` became an out-parameter, which is why the signature changed);
read-back retries `search_retries` times; the log distinguishes "no data at all"
(search never reached the game) from "data, but not this item".

**Result:** 19 → **0**. → I10

### 9.13 Deals inflated profit by counting one order per quality band
Group key is `(item, enchant, quality)`, but each group took the full `amount` of
the same black market order. The city normally lists an item at three or four
qualities and all of them fill a low-quality order.

Reproduced in `PlanTest`: one 10-unit bid vs three quality bands of supply →
planned **30 units / 1,350,000 profit**, where the real ceiling is
**10 units / 460,000**. **2.9× overstated.** Real logs showed
`Cleric Sandals q1/q2/q3` each planned at 11 units against one 11-unit order.

**Fix:** demand is a finite pool with `left` per order; bands compete for it,
cheapest supply claiming first (§6 stage 2).

### 9.14 Only the dearest bid was used
`BestBuyerFor` returned the single highest-priced order and discarded the rest, so
five standing orders of five units read as demand for five.
**Fix:** all orders enter the pool, price-descending. `PlanTest`: two bids × 5 units
now plans 10, not 5.

### 9.15 Budget trim mis-priced the remainder
A trimmed lot keeps the cheapest units, which pair with the dearest orders, but
profit was recomputed from the whole lot's average `bmPrice`.
**Fix:** re-cost from `revenueLadder`; also re-apply the profit gate to the trimmed
lot and skip it (not stop) if it no longer qualifies.

### 9.16 Entry capped at ~1 billion
`MAX_ENTERED_DIGITS = 9` with `stoi`. A rich character could hit it.
**Fix:** 12 digits, `stoll`, `long long` throughout.

### 9.17 `Plan` was a duplicate of `Deals`
`Plan` asked only for a percentage and took budget, silver floor and AND/OR from
`Config.txt`. With the shipped `budget: 0` it computed **exactly** the same list as
`Deals`, and there was no way to preview a specific gate without editing
`Config.txt` or spending real silver to find out.
**Fix:** `Plan` walks the same four screens as `BuyCity`, differing by the single
flag `pendingBuyDryRun`. A preview that asks fewer questions than the run it
previews is previewing a different list.

### 9.18 Scanner confirmed silence it already had the answer to
Scanning a high tier holding one item still paid `page_quiet_clicks` ×
`page_timeout_ms` to confirm the listing had ended.
**Fix:** `page_size` (§5.1). The settle-before-counting requirement is the whole
risk in that change and is documented there.

### 9.19 Category hover was the one fixed-pixel movement
Boxes were 2×2, and the hover bypassed `ClickInside` entirely by averaging the rect
to its centre — so widening the boxes alone would have changed nothing.
**Fix:** `PointInside` factored out of `ClickInside`; new
`MoveToRect(rect, holdRowY = -1)`; boxes widened in x across all 46 entries.

### 9.21 The buy run refused a substitution the market itself allows
The black market takes anything **at or above** the quality it asked for, and
`Plan` already exploits that: bands compete for demand and the cheapest one
wins, so a lot for a Good order is usually planned against a cheaper
Outstanding.

`Run` then threw that away. It locked to the single planned band
(`SelectQuality(lot.quality)`, and `pullLive` dropped every other quality), with
the planned band's own `maxBuyPrice` as the ceiling. So a lot planned against a
cheap Outstanding that somebody else bought out was abandoned outright — with
the Good it had been undercutting sitting on the same screen, at a price the
margin still cleared. The whole lot was lost to a substitution the game
considers valid.

**Fix, three parts:**
1. `FlipLot::qualityOrder` — every band that can fill the lot's orders, cheapest
   planned supply first (§6 stage 2b).
2. `FlipLot::marginCeiling` — a ceiling that survives switching to a dearer
   band, since `maxBuyPrice` by construction cannot.
3. `Run` walks the bands instead of one, and **re-sweeps** them while a pass is
   still buying (`MAX_BAND_PASSES`, plus a no-progress break). A band abandoned
   at its ceiling can have something cheap listed in it while a later band is
   worked, and that row is the one that should be taken next.

The ledger records the band **actually** bought from, not the planned one —
`holdings.tsv` feeding `BlackMarketSeller` a wrong quality would make it search
for a stack that does not exist.

Within a single band this already worked and still does: `pullLive()` re-reads
and re-sorts after every purchase, so a newly-listed bargain becomes
`live.front()` and is taken on the next round.

`PlanTest` covers the plan side: a q2 order against q2\@60000 / q3\@50000 /
q4\@55000 plans the q3 and carries `q3 q4 q2` with an 80000 ceiling; a q4 order
never lists the cheap q1/q2 that cannot fill it.

**Live run, and it corrected two things.** The band walk fired as designed
(`q1 -> q2 -> q3 -> q4` on a Bridgewatch Cape lot, and the second pass correctly
did not run because the first bought nothing). But:

- **Every band failure was silent.** Four bands tried, then `0/29 units`, with
  no way to tell whether the market had none of it or whether everything on
  offer was too dear. Making a band failure non-fatal had also made it
  unlogged - the I10 mistake with the message deleted rather than merely wrong.
  Each band now reports `nothing listed`, `cheapest X is over the Y ceiling`, or
  `bought out while working it`.
- **The ceiling was on the wrong band.** The planned band kept `maxBuyPrice`
  (142988) while its fallbacks got `marginCeiling` (146509), so the bot would
  refuse a Good at 145000 and accept an Outstanding at the same price - when
  Good is the quality the order actually asked for. Now
  `max(maxBuyPrice, marginCeiling)` everywhere: both are justified prices, and
  the limit is the same in every band.

Note the headroom is only as wide as the margin allows. At a 10% requirement
against a 167875 bid, `marginCeiling` is 146509 against a planned 142988 - 2.5%
of room. The fallback rescues a lot only when the spread leaves it something to
work with.

### 9.22 Every category click stopped registering, silently

The UI rebuild moved click routing off display labels: hit tests started
returning `cat:<key>` and rows started carrying a `command` id. The controllers
were not changed with them - `ScanController` and `SettingsController` kept
comparing `row.menuSub == command`, which a prefixed key never equals.

Nothing errored. An unmatched command is an empty-ish no-op, so clicking a
category did nothing at all, and with nothing selectable `SCAN BM` could only
answer "Nothing to scan." The screen simply refused to respond, and the build
was clean.

Two things made it worse than an ordinary miss. It is the exact failure the key
prefix exists to prevent, arrived at by changing one end and not the other. And
it is invisible to `UiPreview`, which draws through `UiKit` and never routes an
event - the screenshots were all correct.

The fix is not just the two comparisons. Both ends now go through
`Ui::CategoryCommand` / `Ui::CategoryKey`, so the prefix is written once.
Two functions cannot drift the way two string literals can.

**The general rule:** when a hit test and its controller are edited separately,
nothing in the compiler or the tests connects them. If the vocabulary between
them is a string, put it behind a function that both call.

### 9.20 OPEN — the buy loop chases its own tail
**Not a fix. A known, measured, accepted behaviour.** In the 47-unit run, the order
id aimed at in round *N+1* was **always exactly the id bought in round N**, 34 out
of 34 times.

`pullLive()` does `live.swap(fresh)`, which **undoes** the local
`live.erase(live.begin())` from the purchase just made — the game's re-sent listing
still contains the bought row. So the bot aims at a row that is gone and the click
lands on the next one down.

Measured cost: **zero units** (every lot hit its planned quantity) and **0.10%**
silver against plan. Residual risk: if a partial refresh ever leaves the bought id
absent from `live`, `paid == 0`, and three of those abort a lot — **0 occurrences
in 34 samples**, with a structural reason it is rare (the stale listing contains
*extra* rows, not missing ones).

Deliberately not fixed: the correct fix needs per-id consumption tracking that must
also detect when the server *has* accounted for a purchase, and it lives in
`CityBuyer::Run` — the one loop with no test coverage. Trading a small measured
risk for an unmeasurable one is a loss. **Fix it when
`at a price not in the listing` actually appears in a log.**

Logging changed instead: the ordinary case is counted, not announced, and reported
once per lot. 34 warning-shaped lines for normal operation train the reader to skim
past the two that matter.

---

## 10. Inherited traps

**DPI.** Machine runs 1920×1080 at 125%. A DPI-*unaware* process sees a fake
1536×864 desktop; `SetCursorPos` gets scaled and `BitBlt` returns a downscaled
blurry image. `main()` calls `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`
before any window exists. `FindCoords.ps1` prints `Physical screen: 1920 x 1080`
as its own check — if it says 1536×864, everything measured with it is 25% off.

**`tessedit_char_whitelist` silently breaks OCR.** Tesseract 4/5's LSTM returns an
**empty string** rather than a best guess when a whitelist is set. Filter after
recognition instead. `SquashNumeric()` folds S→5, O→0, B→8 and is applied **only**
to numeric text — globally it would turn "Swords" into "5word5". (Vestigial: OCR
is dead, I5.)

**Source files were CP1251** (Russian comments from the original author). All
converted to **UTF-8 with BOM**. Scripted edits must preserve that.

**`sf::Text` renders `std::string` as Latin-1, not UTF-8.** All UI labels are
English for this reason; Vietnamese labels need explicit UTF-8 → UTF-32.

**Submenus open on hover, not click.** Clicking a parent collapses the menu.

---

## 11. State

### Verified without the game
- `MSBuild SFML.sln /p:Configuration=Release /p:Platform=x64` — clean rebuild, only
  pre-existing warnings (two C4244 in `Game.cpp`, one C4267 in `Shapes.cpp`)
- `dotnet build -c Release` for AlbionFeed
- `PlanTest` **38/38** — tier gate, profit-descending ranking (T8 69600 before
  T6 38000, i.e. *not* per-silver), quality double-count, multi-order demand,
  budget trim re-costing, AND/OR semantics, silver floor after trim, and the
  quality fallback list (cheaper high quality preferred, bands that cannot fill
  the order excluded, margin ceiling above maxBuyPrice)
- `CategoryTest` **10/10** — coordinates ↔ tsv both directions, table length, no two
  categories on the same menu row, `menu_sub` uniqueness, 27px grid, box shape,
  `subList` depth. **Mutation-tested**: a planted row with no coordinates and a
  duplicated label trips three checks and exits 1
- `WipeTest` — 8515 bytes of `bot.log` → 0, next line written is line one
- The whole UI, rendered to PNG by `UiPreview` and looked at: all six screens,
  46 real categories in four columns, the five scan states distinguishable at a
  glance, the plan table still aligned in the mono log panel
- `SFML.exe` builds with **zero warnings** - the two pre-existing C4244s in
  `updateMousePos` are gone too
- `ShopListTest` **26/26** — lots sharing a search merge into one block and their
  quantities/profits add up; a merged block advertises the lowest quality and the
  lowest ceiling; ranking is by block total, so two middling lots of one item
  outrank a single fatter lot of another; `Shortfall` re-costs from the dearest
  end of the offer ladder and the cheapest end of the order ladder, and carries
  `marginCeiling` over unchanged
- `abort_hotkey` parsing, against the real `Config.txt`: `ctrl+shift+q` → vk
  0x51, `pause` → 0x13, `f9` → 0x78, `alt+end` → 0x23; `ctrl+shift`, `banana`,
  `f13` and `ctrl+q+w` are all refused and keep the default. Confirmed positively
  by editing the shipped file to `alt+f8` and watching the parsed value follow
- `MarketFeed` parses both message shapes (hand-crafted POSTs)
- UI layout arithmetic for 46 categories (§12)

### Verified in a live run
- **The rebuilt UI, running.** Menu draws correctly at a real 900x820 client
  rect, hover highlights the row under the cursor, and the log panel carries the
  startup lines in the mono face. Clicking `Shop` and `Plan` produced their own
  correct messages in `bot.log`, which is what proves the new routing works -
  rows carry a command id and the label is never compared.
- One thing only a real launch could catch: `InitVersion` used to be reached as
  the last line of `InitTextVar`, so removing the FPS text object took the
  `version = new Text` with it and the first frame dereferenced a garbage
  pointer. The window appeared and the process died. `UiPreview` renders through
  `UiKit` and never touches `Game`, so it could not have found this.
- Buy path end to end: **47 units, 13/13 lots, 17,825,223 silver, 99.1% of plan,
  0 `gone from the market`** — the evidence for §9.11 and §9.12
- Capes, scanned and bought: `Avalonian Cape T6.0` 45/45 and `Demon Cape T6.1`
  30/30. Their submenu rows (810 and 837) were **derived from a screenshot and
  sit past the deepest row ever measured** (783), so this validates the 27px
  grid extrapolation as well as the categories themselves
- `page_size` short-page detection: **40 firings** in one scan, with the
  quiet-click test still handling the other 35 (a tab holding exactly 50, or an
  empty one) — the two paths are complements, not rivals
- The randomised category hover (§9.19) — every cape category selected correctly
- The quality-band fallback firing (§9.21)

### Not verified — needs the game
- **The emergency stop has never been pressed during a real run.** The parsing
  and the latch are covered; what is not is whether `Ctrl+Shift+Q` is genuinely
  inert inside Albion.
- **The `Shop` screen has never been rendered.** It builds and its arithmetic is
  covered, but nothing has looked at it: block geometry, text fitting inside
  900px, scrolling, the clipboard copy and the COPIED flash are all unobserved
- **The emergency stop has never been pressed during a real run.** The parsing
  and the latch are covered; what is not is whether `Ctrl+Shift+Q` is genuinely
  inert inside Albion, and how long a run takes to reach its next checkpoint
- The **re-sweep** half of §9.21 (a second pass has not yet had cause to run)
- `capes_smuggler` (864), the only derived coordinate still unproven
- A full SellBM run
- The randomised category hover (§9.19) against the real menu
- A full SellBM run
- Whether the menu timings hold on a laggy day

### Dead code, left deliberately
- `Ocr.h/.cpp` — nothing calls them. `BUILD.md`'s whole tesseract/vcpkg section
  exists only for these. **Removing them would drop tesseract from the build
  entirely** — a real simplification, not yet taken.
- `OcrWordTest` — orphaned with the above
- `MarketScanner::menuAttempts` — never referenced. Category menus are attempted
  **once**; `RETRY FAILED` is the retry. Do not assume this field does anything.
- `ScanProgress`'s mutex — correct, no longer load-bearing (single-threaded)
- `MarketStore::ClearBlackMarket()` — unused. `ClearCities()` is **not** dead
  (BuyCity calls it); `Clear()` and `Holdings::Clear()` are used by the Clear button.
- `Albion.vcxproj` — orphan pointing at paths that no longer exist. `SFML.vcxproj`
  is the real project.

---

## 12. Layout headroom

Window 900×820. Category grid: 3 columns, 28px step, 26px boxes.
Log panel starts at **y=540**.

| Screen | 46 categories | Margin |
|---|---|---|
| Scan | grid ends 476; action row (`SCAN BM`/`SCAN MARKET`/`CLEAR`) 484–510 | **30px** |
| Settings | grid ends 492 | 48px |
| Scanning | grid ends 476; `RETRY FAILED` at 500 | 24px |
| Shop | no grid; list runs 48 → 798, 72px blocks on a 78px pitch | scrolls |

`Shop` is the exception to the log panel: `render()` skips `DrawLog()` for it, so
the list gets the whole 750px and about nine entries are visible at once. It is
read while alt-tabbing to the game and back, and the log behind it holds the plan
table the screen was built from — the one thing there is no reason to consult
while working through it.

**The next batch of categories needs the grid reflowed** — four columns or shorter
boxes — not more rows.

Scan cost scales as `categories × tiers × enchants`. Capes added 15 × 5 × 5 = 375
steps on top of ~775. `page_size` earns much of that back, because cape listings
are short.

---

## 13. Running it

1. Desktop shortcut **Albion Bot** → `x64\Release\SFML.exe` directly. Approve the
   one UAC prompt (packet capture needs Administrator). `START.bat` is no longer
   needed.
2. Stand at the **Black Market** (Caerleon) → `Scan` → pick categories → `SCAN BM`
3. Travel to a **royal city** → `Scan` → `SCAN MARKET`
4. `Plan` — enter the four figures, read the table, **spends nothing**
5. `BuyCity` — same four figures, buys
6. Travel back to the Black Market → `SellBM` → profit %

A flip needs the **same item on both sides**, so scan the same categories in both
places or nothing pairs.

After BuyCity the city half of the store is cleared — **re-scan the city before
buying again.**

**Build the bot:** `MSBuild SFML.sln /p:Configuration=Release /p:Platform=x64` with
`VCPKG_ROOT` and `OPENCV_DIR` set. See `BUILD.md`.

**Build the feed:** .NET SDK 10.
```
cd D:\AlbionTool\AlbionFeed
dotnet publish -c Release -o D:\AlbionTool\bin\AlbionFeed
```

`D:\AlbionTool\bin\run-feed.bat` is what the bot launches: kills every capture
client (including any stray `albiondata-client`), then starts
`AlbionFeed.exe --port 5000` minimised. **The kill must live in that script** — it
runs elevated, and an unelevated process cannot terminate an elevated one.

Installed outside the repo: `D:\VSBuildTools`, `D:\OpenCV`, `D:\vcpkg` (tesseract,
nlohmann-json), .NET SDK, Npcap in system32.

Npcap is a **passive** capture driver — it copies packets and does not modify,
block or reroute. The feed port and everything AlbionFeed sends bind
**127.0.0.1 only**, and AlbionFeed contains no code that could upload elsewhere.

If a stray `albiondata-client.exe` survives from the old setup it is still uploading
to the public Albion Data Project. It runs elevated: kill it from an elevated prompt
or press **Capture**. Also check Task Scheduler for `Albion Data Client` (§9.5).

---

## 14. Risk

Automating input violates Albion Online's terms of service and can get the account
banned. The original author's README says the same. **Nothing here hides the bot
from anti-cheat.** The human-like mouse movement exists because the user asked for
it, not because it defeats detection.
