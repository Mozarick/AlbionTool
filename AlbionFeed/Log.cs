namespace AlbionFeed;

/// <summary>
/// Console plus a file, because the bot starts this hidden and elevated - so
/// when something goes wrong there is no window to read it in. Same reasoning
/// as the bot's own bot.log.
/// </summary>
public static class Log
{
    private static readonly object gate = new();
    private static string? path;

    public static void OpenFile(string file)
    {
        lock (gate)
        {
            path = file;
            try
            {
                // Truncate on start: this is a diagnostic tail, not an archive,
                // and a file that only ever grows gets ignored.
                File.WriteAllText(file, string.Empty);
            }
            catch (IOException)
            {
                path = null;
            }
            catch (UnauthorizedAccessException)
            {
                path = null;
            }
        }
    }

    public static void Info(string message) => Write("     ", message);
    public static void Warn(string message) => Write("WARN ", message);
    public static void Error(string message) => Write("ERROR", message);

    private static void Write(string level, string message)
    {
        var line = $"{DateTime.Now:HH:mm:ss} {level} {message}";

        lock (gate)
        {
            Console.WriteLine(line);

            if (path == null)
            {
                return;
            }

            try
            {
                File.AppendAllText(path, line + Environment.NewLine);
            }
            catch (IOException)
            {
            }
        }
    }
}
