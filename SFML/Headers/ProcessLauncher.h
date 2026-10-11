#pragma once
#include <string>

using namespace std;

// Starts AlbionFeed if it is not already running, so the app itself is the one
// thing to launch - no separate launcher window needed for it.
//
// AlbionFeed is this project's own capture client, built from
// AlbionDataAvalonia's packet parser (D:\AlbionTool\AlbionFeed). It replaced
// the third-party albiondata-client for two reasons:
//
//   1. Privacy. The old client's whole purpose is to upload what it sees to the
//      public Albion Data Project servers, and keeping that off depended on
//      remembering to pass -i "". AlbionFeed contains no uploader at all, so
//      there is nothing to leave switched on by mistake.
//   2. Purchase confirmations. AlbionFeed forwards the game's own
//      AuctionBuyOffer packets, which name the order id and the number of units
//      bought. The bot used to have to guess whether a click worked by watching
//      the offer list shrink.
//
// Still triggers exactly one UAC prompt (Npcap packet capture needs
// Administrator, same as before), and the feed's window starts minimised.
//
// Returns true if the feed is already running or was launched; false only if
// launching failed outright (missing bat file, UAC declined by policy - a plain
// decline still returns true, since ShellExecute has already done its part by
// the time the user sees the prompt).
bool EnsureFeedClientRunning();

// Kills every capture client and starts a fresh AlbionFeed. Unconditional,
// unlike EnsureFeedClientRunning: use this when a client is running but
// demonstrably not feeding us.
//
// The kill happens inside the elevated script, not here, because an unelevated
// process cannot terminate an elevated one - and the client has to be elevated
// for Npcap. Costs one UAC prompt.
bool RestartFeedClient();

// True if an Albion Online client or its launcher is already running.
bool IsAlbionOnlineRunning();

// Starts the Albion launcher if neither it nor the game is already running.
// Not elevated, not hidden - it is a normal GUI app, no console to hide.
void EnsureAlbionLauncherRunning();
