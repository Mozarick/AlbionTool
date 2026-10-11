using Albion.Network;
using AlbionDataAvalonia.Shared;

namespace AlbionFeed;

/// <summary>
/// What the packets are worth remembering between them: which map the player is
/// on, and which order the last buy click went to.
/// </summary>
public sealed class FeedState
{
    private readonly object gate = new();
    private string locationId = string.Empty;
    private PurchaseEvent? pendingPurchase;

    public string LocationId
    {
        get { lock (gate) { return locationId; } }
    }

    public void SetLocation(string id)
    {
        lock (gate)
        {
            if (id.Length == 0 || id == locationId)
            {
                return;
            }
            locationId = id;
        }

        Log.Info($"location is now {id}{(id == "3003" ? " (Black Market)" : string.Empty)}");
    }

    public void SetPendingPurchase(PurchaseEvent purchase)
    {
        lock (gate)
        {
            pendingPurchase = purchase;
        }
    }

    /// <summary>
    /// Hands back the buy that is waiting for its answer, and forgets it. Null
    /// when a response arrives with no request in front of it, which would mean
    /// the request packet was missed - the bot is told nothing rather than
    /// being told about a purchase we cannot name.
    /// </summary>
    public PurchaseEvent? TakePendingPurchase()
    {
        lock (gate)
        {
            var purchase = pendingPurchase;
            pendingPurchase = null;
            return purchase;
        }
    }
}

/// <summary>
/// Stamps orders that arrived without a location with wherever the player is,
/// which is what the game leaves out when it is the current map.
/// </summary>
internal static class OrderShipping
{
    public static void Send(List<MarketOrder> orders, FeedState state, BotSink sink, string what)
    {
        if (orders.Count == 0)
        {
            return;
        }

        var location = state.LocationId;
        foreach (var order in orders)
        {
            if (string.IsNullOrEmpty(order.LocationId))
            {
                order.LocationId = location;
            }
            else
            {
                order.LocationId = Locations.Normalize(order.LocationId);
            }
        }

        sink.PostMarket(orders);
        Log.Info($"{orders.Count} {what} at location {(location.Length > 0 ? location : "?")}");
    }
}

public class OffersHandler : ResponsePacketHandler<AuctionGetOffersResponse>
{
    private readonly FeedState state;
    private readonly BotSink sink;

    public OffersHandler(FeedState state, BotSink sink) : base((int)OperationCodes.AuctionGetOffers)
    {
        this.state = state;
        this.sink = sink;
    }

    protected override Task OnActionAsync(AuctionGetOffersResponse value)
    {
        OrderShipping.Send(value.Orders, state, sink, "sell offers");
        return Task.CompletedTask;
    }
}

public class RequestsHandler : ResponsePacketHandler<AuctionGetRequestsResponse>
{
    private readonly FeedState state;
    private readonly BotSink sink;

    public RequestsHandler(FeedState state, BotSink sink) : base((int)OperationCodes.AuctionGetRequests)
    {
        this.state = state;
        this.sink = sink;
    }

    protected override Task OnActionAsync(AuctionGetRequestsResponse value)
    {
        OrderShipping.Send(value.Orders, state, sink, "buy orders");
        return Task.CompletedTask;
    }
}

/// <summary>
/// The click went out. Reported immediately as unconfirmed so the bot can stop
/// waiting the moment it knows the game accepted the input, and confirmed a
/// moment later when the server answers.
/// </summary>
public class BuyRequestHandler : RequestPacketHandler<AuctionBuyOfferRequest>
{
    private readonly FeedState state;
    private readonly BotSink sink;

    public BuyRequestHandler(FeedState state, BotSink sink) : base((int)OperationCodes.AuctionBuyOffer)
    {
        this.state = state;
        this.sink = sink;
    }

    protected override Task OnActionAsync(AuctionBuyOfferRequest value)
    {
        var purchase = new PurchaseEvent
        {
            OrderId = value.OrderId,
            Amount = value.Amount,
            Confirmed = false,
        };

        state.SetPendingPurchase(purchase);
        sink.PostPurchase(purchase);

        Log.Info($"buy sent: order {value.OrderId} x{value.Amount}");
        return Task.CompletedTask;
    }
}

public class BuyResponseHandler : ResponsePacketHandler<AuctionBuyOfferResponse>
{
    private readonly FeedState state;
    private readonly BotSink sink;

    public BuyResponseHandler(FeedState state, BotSink sink) : base((int)OperationCodes.AuctionBuyOffer)
    {
        this.state = state;
        this.sink = sink;
    }

    protected override Task OnActionAsync(AuctionBuyOfferResponse value)
    {
        var pending = state.TakePendingPurchase();
        if (pending == null)
        {
            Log.Warn("buy confirmed but the matching request was never seen");
            return Task.CompletedTask;
        }

        sink.PostPurchase(new PurchaseEvent
        {
            OrderId = pending.OrderId,
            Amount = pending.Amount,
            Confirmed = true,
        });

        Log.Info($"buy confirmed: order {pending.OrderId} x{pending.Amount}");
        return Task.CompletedTask;
    }
}

public class LocationHandler : ResponsePacketHandler<JoinResponse>
{
    private readonly FeedState state;

    public LocationHandler(FeedState state) : base((int)OperationCodes.Join)
    {
        this.state = state;
    }

    protected override Task OnActionAsync(JoinResponse value)
    {
        state.SetLocation(value.LocationId);
        return Task.CompletedTask;
    }
}
