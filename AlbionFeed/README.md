# AlbionFeed

A private market feed for the Albion bot. It watches the game's own network
traffic, decodes the market packets, and hands them to the bot on
`127.0.0.1:5000`.

**Nothing is uploaded anywhere.** Not to the Albion Data Project, not to
albionfreemarket.com, not anywhere. That is not a setting — there is no
uploader, no account, no proof-of-work solver and no database in this program
at all. `BotSink.cs` is the only code that sends anything, and its address is
hard-wired to loopback.

## What it is built from

The hard part — decoding Photon/Protocol16 packets — is not reimplemented here.
This project references four projects from a clone of
[JPCodeCraft/AlbionDataAvalonia](https://github.com/JPCodeCraft/AlbionDataAvalonia)
at `..\AlbionDataAvalonia` and **modifies none of them**:

- `Protocol16`
- `PhotonPackageParser`
- `Albion.Network`
- `AlbionDataAvalonia.Shared` (the operation/event code enums)

The Avalonia app in that repo is never built. That app is the part with the
uploader in it.

Keeping the clone pristine means `git pull` in `..\AlbionDataAvalonia` is how
this stays working when the game changes its protocol.

## What it listens for

Five packets. Everything else the game sends — combat, movement, gathering,
loot, mail — is decoded and dropped.

| Packet | Why |
|---|---|
| `JoinResponse` | which map the player is on; `3003` is the Black Market |
| `AuctionGetOffersResponse` | sell offers in a city market |
| `AuctionGetRequestsResponse` | standing buy orders, i.e. Black Market bids |
| `AuctionBuyOfferRequest` | the order id and unit count of a buy just sent |
| `AuctionBuyOfferResponse` | the server's answer to that buy |

The last two are why this project exists rather than a config flag on somebody
else's client. The bot used to work out whether a Buy click had landed by
watching the offer list get shorter, which fails whenever the game does not
re-send the list — see §8.9 of the bot's `HANDOVER.md`.

## Wire format

```
POST /market     {"Orders":[{"Id":…,"ItemTypeId":…,"UnitPriceSilver":…, …}]}
POST /purchase   {"Purchase":{"OrderId":…,"Amount":…,"Confirmed":true}}
```

Field names are the game's own, which is why they are capitalised, and they are
also what `MarketFeed.cpp` parses. Renaming one means editing the bot too.
`UnitPriceSilver` is silver × 10000.

Two purchase messages arrive per buy: `Confirmed: false` when the client sent
the request, then `Confirmed: true` when the server answered.

## Building and running

Needs the .NET SDK 10 and Npcap (already installed for the old client).

```
dotnet publish -c Release -o D:\AlbionTool\bin\AlbionFeed
```

```
AlbionFeed.exe [--port 5000] [--filter "udp port 5056"] [--quiet]
```

Npcap will not hand capture devices to a normal user, so the exe asks for
Administrator. Normally the bot starts it: `D:\AlbionTool\bin\run-feed.bat`,
launched elevated, which kills any other capture client first.

Output goes to the console and to `AlbionFeed.log` next to the exe, truncated on
each start. That log is the place to look when the bot reports receiving
nothing: it names the adapter that was chosen and counts the packets seen.

## Things worth knowing

- **One capture client at a time.** Two of them fighting over the same Npcap
  handle is the best explanation there has ever been for a feed that dies
  mid-session. AlbionFeed warns on startup if it sees another one running, and
  `run-feed.bat` kills them.
- **All adapters are opened, then all but one closed.** There is no reliable
  way to know in advance which adapter the game uses — VPNs, virtual switches
  and several NICs all look plausible. The first one to deliver an Albion
  packet wins. Feeding two copies of the same stream into the parser would
  corrupt its fragment reassembly.
- **Encrypted packets.** If the game starts encrypting, the parser reports it
  and the log says so once. There is nothing to be done about it here.
