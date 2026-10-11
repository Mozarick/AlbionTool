using System.Text.Json;
using System.Threading.Channels;

namespace AlbionFeed;

/// <summary>
/// The one and only place data leaves this program.
///
/// It goes to http://127.0.0.1:&lt;port&gt; and nowhere else. There is no uploader,
/// no proof-of-work solver, no account, no database and no second destination
/// to accidentally turn on - the code for those simply does not exist here,
/// which is a stronger guarantee than a setting that says "do not upload".
/// </summary>
public sealed class BotSink : IAsyncDisposable
{
    private readonly HttpClient http;
    private readonly Channel<(string Path, string Body)> queue;
    private readonly Task sender;
    private readonly string baseAddress;

    private long delivered;
    private long dropped;
    private bool warnedUnreachable;

    private static readonly JsonSerializerOptions Json = new()
    {
        DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.Never,
    };

    public BotSink(int port)
    {
        baseAddress = $"http://127.0.0.1:{port}";

        http = new HttpClient
        {
            BaseAddress = new Uri(baseAddress),
            // The bot answers 200 before it parses, so a slow reply means it is
            // wedged, not busy. Failing fast keeps the queue moving.
            Timeout = TimeSpan.FromSeconds(3),
        };

        // Bounded, and the oldest item is dropped when full. If the bot is not
        // listening, an unbounded queue would grow for as long as the game
        // keeps sending; market data seconds old is worthless anyway.
        queue = Channel.CreateBounded<(string, string)>(new BoundedChannelOptions(256)
        {
            FullMode = BoundedChannelFullMode.DropOldest,
            SingleReader = true,
        });

        sender = Task.Run(SendLoopAsync);
    }

    public long Delivered => Interlocked.Read(ref delivered);
    public long Dropped => Interlocked.Read(ref dropped);

    public void PostMarket(List<MarketOrder> orders)
    {
        if (orders.Count == 0)
        {
            return;
        }

        Enqueue("/market", JsonSerializer.Serialize(new MarketUpload { Orders = orders }, Json));
    }

    public void PostPurchase(PurchaseEvent purchase)
    {
        Enqueue("/purchase", JsonSerializer.Serialize(new PurchaseUpload { Purchase = purchase }, Json));
    }

    private void Enqueue(string path, string body)
    {
        if (!queue.Writer.TryWrite((path, body)))
        {
            Interlocked.Increment(ref dropped);
        }
    }

    private async Task SendLoopAsync()
    {
        await foreach (var (path, body) in queue.Reader.ReadAllAsync())
        {
            try
            {
                using var content = new StringContent(body, System.Text.Encoding.UTF8, "application/json");
                using var response = await http.PostAsync(path, content);

                if (response.IsSuccessStatusCode)
                {
                    Interlocked.Increment(ref delivered);
                    warnedUnreachable = false;
                }
                else
                {
                    Interlocked.Increment(ref dropped);
                    Log.Warn($"bot answered {(int)response.StatusCode} for {path}");
                }
            }
            catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException)
            {
                Interlocked.Increment(ref dropped);

                // Once per outage, not once per packet: the game sends market
                // lists in bursts and a closed bot would fill the log with the
                // same line hundreds of times.
                if (!warnedUnreachable)
                {
                    warnedUnreachable = true;
                    Log.Warn($"cannot reach the bot on {baseAddress} - is it running? (data is being dropped until it is)");
                }
            }
        }
    }

    public async ValueTask DisposeAsync()
    {
        queue.Writer.TryComplete();
        try
        {
            await sender.WaitAsync(TimeSpan.FromSeconds(3));
        }
        catch (TimeoutException)
        {
        }
        http.Dispose();
    }
}
