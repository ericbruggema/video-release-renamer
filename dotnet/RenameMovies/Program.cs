// RenameMovies - renames video releases (and their subtitle files) to
// "Title (Year) [Format] <NL-SUBS>.ext". A 1:1 port of rename_movies.py,
// with a bit of extra robustness the Python version didn't have (see the
// comment on each method below). No dependencies beyond the .NET BCL - can
// be published self-contained/single-file, so no .NET runtime is needed on
// the target machine.

using System.Text;
using System.Text.RegularExpressions;

namespace RenameMovies;

internal sealed record ParsedInfo(string Title, string? Year, string Format, bool HasNlSubs);

internal sealed class Stats
{
    public int VideosRenamed;
    public int VideosDuplicateSkipped;
    public int VideosUnparsed;
    public int SubsRenamed;
    public int SubsDuplicateSkipped;
    public int Errors;
    public int DirectoriesScanned;
}

internal static partial class Program
{
    private static readonly string[] VideoExtensions =
        { ".mkv", ".mp4", ".avi", ".m4v", ".wmv", ".mov", ".ts", ".webm" };

    private static readonly string[] SubtitleExtensions =
        { ".srt", ".sub", ".idx", ".ssa", ".ass", ".vtt", ".smi" };

    private static readonly HashSet<string> MinorWords = new(StringComparer.OrdinalIgnoreCase)
        { "of", "the", "in", "and", "a", "an", "to", "at", "for", "on", "vs" };

    // Year: 19xx/20xx.
    [GeneratedRegex(@"(19|20)\d{2}")]
    private static partial Regex YearRegex();

    // Resolution - the only thing that appears (together with a source fallback) in the [Format] tag.
    [GeneratedRegex(@"(2160p|4K|1080p|720p|480p|360p)", RegexOptions.IgnoreCase)]
    private static partial Regex ResolutionRegex();

    // Source tags. Deliberately no short/ambiguous codes (CAM, TS, TC, R5) -
    // without a guaranteed word boundary (underscores don't count as one)
    // those could accidentally match in the middle of a title word,
    // especially in the no-year fallback split below.
    [GeneratedRegex(@"(BluRay|BRRip|BDRip|WEB-?DL|WEBRip|HDTV|DVDRip|DVDR|HDRip|REMUX|Screener|SCR)", RegexOptions.IgnoreCase)]
    private static partial Regex SourceRegex();

    // Codec is no longer shown in the filename (per explicit request), but is
    // still used as an anchor point to split title/quality section when there's no year.
    [GeneratedRegex(@"(x264|x265|h\.?264|h\.?265|HEVC|AV1|VP9|XviD|DivX)", RegexOptions.IgnoreCase)]
    private static partial Regex CodecRegex();

    [GeneratedRegex(@"nl[\s\-_.]*sub", RegexOptions.IgnoreCase)]
    private static partial Regex NlSubsRegex();

    [GeneratedRegex(@"\s+")]
    private static partial Regex WhitespaceRegex();

    private static readonly Dictionary<string, string> SourceCanon = new(StringComparer.OrdinalIgnoreCase)
    {
        ["bluray"] = "BluRay",
        ["brrip"] = "BRRip",
        ["bdrip"] = "BDRip",
        ["webdl"] = "WEB-DL",
        ["webrip"] = "WEBRip",
        ["hdtv"] = "HDTV",
        ["dvdrip"] = "DVDRip",
        ["dvdr"] = "DVDR",
        ["hdrip"] = "HDRip",
        ["remux"] = "REMUX",
        ["screener"] = "Screener",
        ["scr"] = "Screener",
    };

    private static readonly char[] InvalidFileNameChars = Path.GetInvalidFileNameChars();

    private static bool _useColor = true;

    private static int Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;

        string directory = ".";
        bool apply = false;
        bool recursive = false;
        bool noColor = false;
        bool showHelp = false;
        bool sawDirectory = false;

        foreach (var raw in args)
        {
            switch (raw)
            {
                case "--apply":
                    apply = true;
                    break;
                case "--recursive":
                case "-r":
                    recursive = true;
                    break;
                case "--no-color":
                    noColor = true;
                    break;
                case "--help":
                case "-h":
                case "-?":
                case "/?":
                    showHelp = true;
                    break;
                default:
                    if (raw.StartsWith('-'))
                    {
                        Console.Error.WriteLine($"Unknown option: {raw}");
                        PrintUsage();
                        return 2;
                    }
                    if (sawDirectory)
                    {
                        Console.Error.WriteLine($"Unexpected extra argument: {raw}");
                        PrintUsage();
                        return 2;
                    }
                    directory = raw;
                    sawDirectory = true;
                    break;
            }
        }

        if (showHelp)
        {
            PrintUsage();
            return 0;
        }

        _useColor = !noColor && !Console.IsOutputRedirected;

        var root = new DirectoryInfo(Path.GetFullPath(directory));
        if (!root.Exists)
        {
            Console.Error.WriteLine($"Directory not found: {root.FullName}");
            return 1;
        }

        if (!apply)
        {
            WriteLine("=== DRY RUN (nothing is being changed; add --apply to actually rename) ===", ConsoleColor.Cyan);
            Console.WriteLine();
        }

        var stats = new Stats();

        IEnumerable<DirectoryInfo> directoriesToScan = recursive
            ? new[] { root }.Concat(root.EnumerateDirectories("*", SearchOption.AllDirectories))
            : new[] { root };

        foreach (var dir in directoriesToScan)
        {
            ProcessDirectory(dir, apply, stats);
        }

        PrintSummary(stats, apply);

        return stats.Errors > 0 ? 1 : 0;
    }

    private static void ProcessDirectory(DirectoryInfo dir, bool apply, Stats stats)
    {
        List<FileInfo> videoFiles;
        try
        {
            videoFiles = dir.EnumerateFiles()
                .Where(f => VideoExtensions.Contains(f.Extension.ToLowerInvariant()))
                .OrderBy(f => f.Name, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }
        catch (UnauthorizedAccessException)
        {
            WriteLine($"[ERROR] No access to directory: {dir.FullName}", ConsoleColor.Red);
            stats.Errors++;
            return;
        }

        if (videoFiles.Count == 0)
        {
            return;
        }

        stats.DirectoriesScanned++;
        var handledSubs = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (var video in videoFiles)
        {
            Console.WriteLine($"* {video.Name}");
            string videoStem = Path.GetFileNameWithoutExtension(video.Name);

            ParsedInfo? info;
            try
            {
                info = ParseRelease(videoStem);
            }
            catch (Exception ex)
            {
                WriteLine($"  [ERROR] could not parse '{video.Name}': {ex.Message}", ConsoleColor.Red);
                stats.Errors++;
                Console.WriteLine();
                continue;
            }

            if (info is null)
            {
                WriteLine("  [SKIPPED] no title/quality info recognized in filename - rename manually", ConsoleColor.Yellow);
                stats.VideosUnparsed++;
                Console.WriteLine();
                continue;
            }

            if (info.Year is null)
            {
                WriteLine("  [NOTE] no year recognized, name built without (year) - check manually", ConsoleColor.Yellow);
            }

            string newStem = BuildNewStem(info);
            string desiredVideoName = newStem + video.Extension.ToLowerInvariant();

            try
            {
                var (target, isDup) = ResolveTarget(dir.FullName, desiredVideoName, video.FullName);
                MoveOrReport(video.FullName, target, isDup, apply, "Video", stats, isVideo: true);
            }
            catch (Exception ex)
            {
                WriteLine($"  [ERROR] renaming '{video.Name}' failed: {ex.Message}", ConsoleColor.Red);
                stats.Errors++;
            }

            // Move along any matching subtitle files.
            List<FileInfo> subs;
            try
            {
                subs = FindSubtitles(dir, videoStem, video.FullName, handledSubs);
            }
            catch (UnauthorizedAccessException)
            {
                subs = new List<FileInfo>();
            }

            foreach (var sub in subs)
            {
                handledSubs.Add(sub.FullName);
                string suffix = sub.Name[videoStem.Length..]; // e.g. ".srt" or ".nl.srt"
                string desiredSubName = newStem + suffix;

                try
                {
                    var (subTarget, subIsDup) = ResolveTarget(dir.FullName, desiredSubName, sub.FullName);
                    MoveOrReport(sub.FullName, subTarget, subIsDup, apply, "  Sub  ", stats, isVideo: false);
                }
                catch (Exception ex)
                {
                    WriteLine($"  [ERROR] renaming '{sub.Name}' failed: {ex.Message}", ConsoleColor.Red);
                    stats.Errors++;
                }
            }

            Console.WriteLine();
        }
    }

    /// <summary>
    /// Parses the filename stem (no extension). Returns null if there is
    /// nothing usable at all (no year AND no quality marker).
    /// </summary>
    private static ParsedInfo? ParseRelease(string stem)
    {
        string titlePart;
        string restPart;
        string? year;

        var yearMatch = YearRegex().Match(stem);
        if (yearMatch.Success)
        {
            year = yearMatch.Value;
            titlePart = stem[..yearMatch.Index];
            restPart = stem[(yearMatch.Index + yearMatch.Length)..];
        }
        else
        {
            // No year: use the first quality marker (1080p/BluRay/x264/...)
            // as the split point between the title and the rest of the release tags.
            year = null;
            var candidates = new[] { ResolutionRegex().Match(stem), SourceRegex().Match(stem), CodecRegex().Match(stem) }
                .Where(m => m.Success)
                .Select(m => m.Index)
                .ToList();

            if (candidates.Count == 0)
            {
                return null;
            }

            int split = candidates.Min();
            titlePart = stem[..split];
            restPart = stem[split..];
        }

        string title = CleanTitle(titlePart);
        if (title.Length == 0)
        {
            return null;
        }

        var resMatch = ResolutionRegex().Match(restPart);
        var srcMatch = SourceRegex().Match(restPart);
        bool hasNlSubs = NlSubsRegex().IsMatch(stem);

        string format;
        if (resMatch.Success)
        {
            format = string.Equals(resMatch.Value, "4k", StringComparison.OrdinalIgnoreCase)
                ? "4K"
                : resMatch.Value.ToLowerInvariant();
        }
        else if (srcMatch.Success)
        {
            format = CanonicalSource(srcMatch.Value);
        }
        else
        {
            format = "Unknown";
        }

        return new ParsedInfo(title, year, format, hasNlSubs);
    }

    private static string CanonicalSource(string matched)
    {
        string key = matched.ToLowerInvariant().Replace("-", "").Replace(" ", "");
        return SourceCanon.TryGetValue(key, out var canon) ? canon : matched;
    }

    private static string CleanTitle(string raw)
    {
        raw = raw.Replace('.', ' ').Replace('_', ' ');
        raw = raw.Trim(' ', '-', '(', '[');
        raw = WhitespaceRegex().Replace(raw, " ").Trim();

        if (raw.Length == 0)
        {
            return string.Empty;
        }

        var words = raw.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        var outWords = new List<string>(words.Length);

        for (int i = 0; i < words.Length; i++)
        {
            string w = words[i];
            if (i > 0 && MinorWords.Contains(w))
            {
                outWords.Add(w.ToLowerInvariant());
            }
            else if (w.All(char.IsLetter))
            {
                outWords.Add(char.ToUpperInvariant(w[0]) + w[1..].ToLowerInvariant());
            }
            else
            {
                outWords.Add(w);
            }
        }

        return string.Join(' ', outWords);
    }

    private static string BuildNewStem(ParsedInfo info)
    {
        string name = info.Year is not null
            ? $"{info.Title} ({info.Year}) [{info.Format}]"
            : $"{info.Title} [{info.Format}]";

        if (info.HasNlSubs)
        {
            name += " NL-SUBS";
        }

        return SanitizeFileName(name);
    }

    private static string SanitizeFileName(string name)
    {
        var sb = new StringBuilder(name.Length);
        foreach (char c in name)
        {
            if (Array.IndexOf(InvalidFileNameChars, c) < 0)
            {
                sb.Append(c);
            }
        }
        return sb.ToString();
    }

    /// <summary>
    /// Determines where <paramref name="sourcePath"/> should end up.
    /// If the desired name already exists with identical content -> (path, true) = duplicate, leave it alone.
    /// If it exists with different content -> "-1", "-2", ... until a free name or an identical match is found.
    /// </summary>
    private static (string target, bool isDuplicate) ResolveTarget(string directory, string desiredName, string sourcePath)
    {
        string stem = Path.GetFileNameWithoutExtension(desiredName);
        string ext = Path.GetExtension(desiredName);
        string candidate = desiredName;
        int n = 0;
        string sourceFull = Path.GetFullPath(sourcePath);

        while (true)
        {
            string targetPath = Path.Combine(directory, candidate);
            string targetFull = Path.GetFullPath(targetPath);

            if (string.Equals(targetFull, sourceFull, StringComparison.OrdinalIgnoreCase))
            {
                return (targetPath, false);
            }

            if (!File.Exists(targetPath))
            {
                return (targetPath, false);
            }

            if (FilesAreIdentical(targetPath, sourcePath))
            {
                return (targetPath, true);
            }

            n++;
            candidate = $"{stem}-{n}{ext}";
        }
    }

    /// <summary>
    /// Exact byte-for-byte comparison, streamed in 1 MB chunks so large video
    /// files (several GB) don't have to be fully loaded into memory.
    /// Equivalent of Python's filecmp.cmp(shallow=False).
    /// </summary>
    private static bool FilesAreIdentical(string pathA, string pathB)
    {
        var infoA = new FileInfo(pathA);
        var infoB = new FileInfo(pathB);
        if (infoA.Length != infoB.Length)
        {
            return false;
        }

        const int bufferSize = 1024 * 1024;
        Span<byte> bufA = new byte[bufferSize];
        Span<byte> bufB = new byte[bufferSize];

        using var streamA = File.OpenRead(pathA);
        using var streamB = File.OpenRead(pathB);

        while (true)
        {
            int readA = streamA.Read(bufA);
            int readB = streamB.Read(bufB);

            if (readA != readB)
            {
                return false;
            }

            if (readA == 0)
            {
                return true;
            }

            if (!bufA[..readA].SequenceEqual(bufB[..readB]))
            {
                return false;
            }
        }
    }

    private static List<FileInfo> FindSubtitles(DirectoryInfo dir, string videoStem, string videoFullPath, HashSet<string> handled)
    {
        var result = new List<FileInfo>();
        foreach (var f in dir.EnumerateFiles())
        {
            if (string.Equals(f.FullName, videoFullPath, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            if (handled.Contains(f.FullName))
            {
                continue;
            }
            if (!SubtitleExtensions.Contains(f.Extension.ToLowerInvariant()))
            {
                continue;
            }

            string nameNoExt = Path.GetFileNameWithoutExtension(f.Name);
            if (nameNoExt.Equals(videoStem, StringComparison.Ordinal) ||
                nameNoExt.StartsWith(videoStem + ".", StringComparison.Ordinal))
            {
                result.Add(f);
            }
        }
        return result;
    }

    private static void MoveOrReport(string source, string target, bool isDuplicate, bool apply, string kind, Stats stats, bool isVideo)
    {
        string sourceName = Path.GetFileName(source);
        string targetName = Path.GetFileName(target);

        if (isDuplicate)
        {
            WriteLine($"  [SKIPPED] {kind} '{sourceName}' already exists identically as '{targetName}' -> left untouched", ConsoleColor.Yellow);
            if (isVideo) stats.VideosDuplicateSkipped++; else stats.SubsDuplicateSkipped++;
            return;
        }

        string tag = apply ? "OK" : "DRY RUN";
        WriteLine($"  [{tag}] {kind}: '{sourceName}' -> '{targetName}'", apply ? ConsoleColor.Green : ConsoleColor.Gray);

        if (apply)
        {
            File.Move(source, target);
        }

        if (isVideo) stats.VideosRenamed++; else stats.SubsRenamed++;
    }

    private static void PrintSummary(Stats stats, bool apply)
    {
        Console.WriteLine(new string('-', 60));
        string verb = apply ? "renamed" : "would rename";
        Console.WriteLine($"Directories scanned:       {stats.DirectoriesScanned}");
        Console.WriteLine($"Videos {verb}:        {stats.VideosRenamed}");
        Console.WriteLine($"Subs {verb}:          {stats.SubsRenamed}");
        Console.WriteLine($"Video duplicates skipped:  {stats.VideosDuplicateSkipped}");
        Console.WriteLine($"Sub duplicates skipped:    {stats.SubsDuplicateSkipped}");
        Console.WriteLine($"Unparseable (manual):      {stats.VideosUnparsed}");
        if (stats.Errors > 0)
        {
            WriteLine($"Errors:                    {stats.Errors}", ConsoleColor.Red);
        }
    }

    private static void WriteLine(string text, ConsoleColor color)
    {
        if (!_useColor)
        {
            Console.WriteLine(text);
            return;
        }
        var prev = Console.ForegroundColor;
        Console.ForegroundColor = color;
        Console.WriteLine(text);
        Console.ForegroundColor = prev;
    }

    private static void PrintUsage()
    {
        Console.WriteLine("""
            RenameMovies - renames video releases to 'Title (Year) [Format] NL-SUBS.ext'

            Usage:
              RenameMovies.exe [directory] [options]

            Arguments:
              directory        Directory with the videos (default: current directory)

            Options:
              --apply          Actually perform the renames (default: dry run / preview)
              -r, --recursive  Also scan subdirectories (each directory is processed separately)
              --no-color       No color in console output
              -h, --help       Show this help text

            Behavior:
              - Recognizes year, resolution, source, and NL subtitle markers from the filename.
              - No year? The quality marker is used as an anchor and the name is built
                without (year) - with a warning.
              - No year AND no quality marker? File is skipped.
              - Extension is never changed.
              - Desired name already exists with identical content -> left untouched (no duplicate copy).
              - Exists with different content -> "-1", "-2", ... until a free name is found.
              - Subtitles (.srt/.sub/.idx/.ssa/.ass/.vtt/.smi) whose filename starts with the
                same name as the video are moved along automatically.
            """);
    }
}
