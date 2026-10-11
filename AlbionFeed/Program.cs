using Albion.Network;
using System.Diagnostics;

namespace AlbionFeed;

/// <summary>
/// A private Albion market feed.
///
/// It watches the game's own traffic, decodes the market packets with
/// AlbionDataAvalonia's parser, and hands them to the bot on 127.0.0.1. That is
/// the entire program. Nothing is uploaded to the Albion Data Project or to any
/// other server, and not because a switch is turned off - there is no code here
/// that could.
///
///   AlbionFeed.exe [--port 5000] [--filter "udp port 5056"] [--quiet]
/// </summary>
public static class Program
{
    public static async Task<int> Main(string[] args)
    {
        var port = 5000;
        var filter = "udp port 5056";
        var quiet = false;

        for (var i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--port" when i + 1 < args.Length && int.TryParse(args[i + 1], out var parsed):
                    port = parsed;
                    i++;
                    break;

                case "--filter" when i + 1 < args.Length:
                    filter = args[i + 1];
                    i++;
                    break;

                case "--quiet":
                    quiet = true;
                    break;

                case "--help":
                case "-h":
                    Console.WriteLine("AlbionFeed [--port 5000] [--filter \"udp port 5056\"] [--quiet]");
                    return 0;
            }
        }

        var here = AppContext.BaseDirectory;
        Log.OpenFile(Path.Combine(here, "AlbionFeed.log"));

        Log.Info($"AlbionFeed starting - market data goes to 127.0.0.1:{port} and nowhere else");

        // One client at a time. Two of them fighting over the same Npcap
        // capture is the best explanation there has ever been for a feed that
        // dies mid-session, and the machine has previously had a stray
        // albiondata-client started by a scheduled task at logon.
        WarnAboutOtherClients();

        await using var sink = new BotSink(port);
        var state = new FeedState();

        var receiver = ReceiverBuilder.Create()
            .AddResponseHandler(new LocationHandler(state))
            .AddResponseHandler(new OffersHandler(state, sink))
            .AddResponseHandler(new RequestsHandler(state, sink))
            .AddRequestHandler(new BuyRequestHandler(state, sink))
            .AddResponseHandler(new BuyResponseHandler(state, sink))
            .Build();

        using var capture = new Capture(receiver, filter);
        if (capture.Start() == 0)
        {
            Log.Error("nothing to listen on, giving up");
            return 1;
        }

        using var stopping = new CancellationTokenSource();
        Console.CancelKeyPress += (_, e) =>
        {
            e.Cancel = true;
            stopping.Cancel();
        };

        // A heartbeat, so a silent log distinguishes "not capturing" from
        // "capturing, but you have not opened a market yet".
        var lastPackets = -1L;
        try
        {
            while (!stopping.IsCancellationRequested)
            {
                await Task.Delay(TimeSpan.FromSeconds(30), stopping.Token);

                var seen = capture.Packets;
                if (!quiet && seen != lastPackets)
                {
                    Log.Info($"{seen} game packets seen, {sink.Delivered} messages delivered to the bot"
                        + (sink.Dropped > 0 ? $", {sink.Dropped} dropped" : string.Empty));
                    lastPackets = seen;
                }
                else if (seen == 0)
                {
                    Log.Warn("no game packets at all yet - is Albion Online running, and is the filter right?");
                }
            }
        }
        catch (OperationCanceledException)
        {
        }

        Log.Info("stopping");
        return 0;
    }

    private static void WarnAboutOtherClients()
    {
        foreach (var name in new[] { "albiondata-client", "AFMDataClient", "AlbionDataAvalonia" })
        {
            try
            {
                if (Process.GetProcessesByName(name).Length > 0)
                {
                    Log.Warn($"{name}.exe is running. It captures the same packets and uploads them "
                        + "to the public servers; close it, or the capture may be unreliable.");
                }
            }
            catch (InvalidOperationException)
            {
            }
        }
    }
}
