namespace AlbionFeed;

/// <summary>
/// Turns whatever string the game calls the current map into the numeric market
/// id the bot works with ("3003" = Black Market, "3005" = Caerleon, and so on).
///
/// Ported from AlbionDataAvalonia's AlbionLocations, minus the part that
/// downloads world.json from a CDN - the bot only ever compares ids, it never
/// shows a city name, so the network round trip buys nothing.
/// </summary>
public static class Locations
{
    private static readonly Dictionary<string, int> HellDenToMarket = new(StringComparer.OrdinalIgnoreCase)
    {
        { "0000-HellDen", 7 },
        { "1000-HellDen", 1002 },
        { "2000-HellDen", 2004 },
        { "3004-HellDen", 3008 },
        { "3003-HellDen", 3005 },
        { "4000-HellDen", 4002 },
        { "5000-HellDen", 5003 },
    };

    private static readonly Dictionary<int, int> PortalToCity = new()
    {
        { 301, 7 },
        { 1301, 1002 },
        { 2301, 2004 },
        { 3301, 3008 },
        { 4301, 4002 },
        { 3013, 3005 },
    };

    /// <summary>
    /// Empty when the map is not a market at all (open world, a dungeon, the
    /// Mists). Callers treat that as "no location known" rather than guessing.
    /// </summary>
    public static string Normalize(string? raw)
    {
        if (string.IsNullOrWhiteSpace(raw))
        {
            return string.Empty;
        }

        var id = raw.Trim().Trim('"', '\'').TrimStart('@');

        if (id.Equals("BLACK_MARKET", StringComparison.OrdinalIgnoreCase)
            || id.Equals("BLACKMARKET", StringComparison.OrdinalIgnoreCase))
        {
            return "3003";
        }

        // The same map is announced under several decorated names depending on
        // which building you are standing in, and they can stack.
        var changed = true;
        while (changed)
        {
            changed = false;

            if (id.EndsWith("-Auction2", StringComparison.OrdinalIgnoreCase))
            {
                id = id[..^"-Auction2".Length];
                changed = true;
            }

            if (id.StartsWith("BLACKBANK-", StringComparison.OrdinalIgnoreCase))
            {
                id = id["BLACKBANK-".Length..];
                changed = true;
            }
        }

        if (id.EndsWith("-HellDen", StringComparison.OrdinalIgnoreCase))
        {
            return HellDenToMarket.TryGetValue(id, out var hellDenMarket)
                ? hellDenMarket.ToString()
                : string.Empty;
        }

        if (int.TryParse(id, out var numeric))
        {
            return PortalToCity.TryGetValue(numeric, out var city)
                ? city.ToString()
                : numeric.ToString();
        }

        return string.Empty;
    }
}
