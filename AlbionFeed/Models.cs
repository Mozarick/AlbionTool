using System.Text.Json.Serialization;

namespace AlbionFeed;

[JsonConverter(typeof(JsonStringEnumConverter))]
public enum AuctionType
{
    unknown,
    offer,
    request
}

/// <summary>
/// One market order exactly as the game sends it.
///
/// The property names are the game's own and are kept verbatim, because they
/// are also the wire format the C++ bot parses. Renaming anything here means
/// editing MarketFeed.cpp to match.
/// </summary>
public class MarketOrder
{
    public ulong Id { get; set; }
    public string ItemTypeId { get; set; } = string.Empty;
    public string ItemGroupTypeId { get; set; } = string.Empty;
    public string LocationId { get; set; } = string.Empty;
    public byte QualityLevel { get; set; }
    public byte EnchantmentLevel { get; set; }

    /// Silver multiplied by 10000. The bot divides; forgetting to is a 10000x error.
    public ulong UnitPriceSilver { get; set; }

    public uint Amount { get; set; }
    public AuctionType AuctionType { get; set; }
    public string Expires { get; set; } = string.Empty;
}

/// The body POSTed to the bot for market data.
public class MarketUpload
{
    public List<MarketOrder> Orders { get; set; } = new();
}

/// <summary>
/// A purchase the game itself reported, not one the bot inferred.
///
/// This is the whole reason for building our own client. The old setup could
/// only guess whether a click had gone through by checking if the offer list
/// got shorter, which fails whenever the game does not re-send the list - and
/// that guess is what produced "purchase did not register" on purchases that
/// had actually succeeded. AuctionBuyOffer carries the order id and the number
/// of units, so the bot now gets told.
/// </summary>
public class PurchaseEvent
{
    public ulong OrderId { get; set; }
    public int Amount { get; set; }

    /// False = the request went out, true = the server answered it.
    public bool Confirmed { get; set; }
}

public class PurchaseUpload
{
    public PurchaseEvent Purchase { get; set; } = new();
}
