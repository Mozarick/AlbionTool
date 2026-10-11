using Albion.Network;
using System.Text.Json;

namespace AlbionFeed;

// The five packets this client cares about. Everything else the game sends -
// combat, movement, gathering, loot, mail - is decoded by the parser and then
// dropped on the floor, because nothing downstream wants it and anything kept
// is something that could later leak.

/// <summary>Sell offers in a city market (and in the Black Market's sell tab).</summary>
public class AuctionGetOffersResponse : BaseOperation
{
    public readonly List<MarketOrder> Orders = new();

    public AuctionGetOffersResponse(Dictionary<byte, object> parameters) : base(parameters)
    {
        MarketOrderParser.Fill(parameters, Orders);
    }
}

/// <summary>Standing buy orders - the Black Market's bids are these.</summary>
public class AuctionGetRequestsResponse : BaseOperation
{
    public readonly List<MarketOrder> Orders = new();

    public AuctionGetRequestsResponse(Dictionary<byte, object> parameters) : base(parameters)
    {
        MarketOrderParser.Fill(parameters, Orders);
    }
}

internal static class MarketOrderParser
{
    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNameCaseInsensitive = true,
    };

    /// <summary>
    /// Parameter 0 is a list of JSON strings, one per order. A single bad entry
    /// must not cost the whole packet: a market list is 20-50 orders and losing
    /// all of them because one had a field the parser did not expect would look
    /// exactly like "the scan found nothing".
    /// </summary>
    public static void Fill(Dictionary<byte, object> parameters, List<MarketOrder> into)
    {
        if (!parameters.TryGetValue(0, out var raw) || raw is not IEnumerable<string> encoded)
        {
            return;
        }

        foreach (var text in encoded)
        {
            if (string.IsNullOrEmpty(text))
            {
                continue;
            }

            try
            {
                var order = JsonSerializer.Deserialize<MarketOrder>(text, Options);
                if (order != null)
                {
                    into.Add(order);
                }
            }
            catch (JsonException)
            {
                // one malformed order, not a broken listing
            }
        }
    }
}

/// <summary>
/// The client asking to buy. Carries the order id and the number of units, so
/// this is what tells the bot which listing its click actually hit.
/// </summary>
public class AuctionBuyOfferRequest : BaseOperation
{
    public readonly int Amount;
    public readonly ulong OrderId;

    public AuctionBuyOfferRequest(Dictionary<byte, object> parameters) : base(parameters)
    {
        if (parameters.TryGetValue(1, out var amount) && amount != null)
        {
            Amount = Convert.ToInt32(amount);
        }

        if (parameters.TryGetValue(2, out var orderId) && orderId != null)
        {
            OrderId = Convert.ToUInt64(orderId);
        }
    }
}

/// <summary>
/// The server's answer to a buy.
///
/// It no longer carries a success flag - upstream checked for one, found the
/// key gone and now assumes success, same as the sell response. So this being
/// received at all is the confirmation: the request reached the server and the
/// server replied to it.
/// </summary>
public class AuctionBuyOfferResponse : BaseOperation
{
    public AuctionBuyOfferResponse(Dictionary<byte, object> parameters) : base(parameters)
    {
    }
}

/// <summary>Sent on every map change; parameter 8 is where the player now is.</summary>
public class JoinResponse : BaseOperation
{
    public readonly string LocationId = string.Empty;
    public readonly string PlayerName = string.Empty;

    public JoinResponse(Dictionary<byte, object> parameters) : base(parameters)
    {
        if (parameters.TryGetValue(2, out var name) && name is string playerName)
        {
            PlayerName = playerName;
        }

        if (parameters.TryGetValue(8, out var location) && location is string raw)
        {
            LocationId = Locations.Normalize(raw);
        }
    }
}
