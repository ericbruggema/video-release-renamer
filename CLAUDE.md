# CLAUDE.md — Video File Renamer Script

## What this is
One tool, three implementations, all functionally identical (tested with the same set of files, byte-for-byte identical output). Renames video files in a directory to:

```
Title (Year) [Format] <NL-SUBS>.ext
```

with matching subtitle files moved along automatically.

| Implementation | File | Typical build size | Dependencies to run |
|---|---|---|---|
| Python | [`rename_movies.py`](rename_movies.py) | n/a (interpreter) | Python 3, no pip packages |
| C# (.NET, Native AOT) | [`dotnet/RenameMovies/`](dotnet/RenameMovies/) | ~1.8 MB | none (natively compiled) |
| C (MSVC) | [`c/rename_movies.c`](c/rename_movies.c) | ~168 KB | none (just Win32 + libc) |

The Python version is the original / reference implementation; the C# and C versions are later standalone ports (requested: "can this also be a single standalone .exe, as small as possible"). All three share the rules below — if you change one, change the other two deliberately too, or document why they diverge.

## Shared behavior (all three versions)
- **Dry run by default**: shows only a preview; only with `--apply` are files actually moved/renamed.
- Recognizes year (`19xx`/`20xx`), resolution (1080p/720p/...), source (BluRay/WEB-DL/...), and NL (Dutch) subtitle markers from the filename.
- **Only the resolution (or, if missing, the source) appears in `[Format]`** — codec/audio info is deliberately left out (explicit request: "I only want to see [1080p], not the rest of the noise").
- **No year?** Then the first quality marker (resolution/source/codec) is used as an anchor to split the title from the release tags, and the name is built **without** `(year)` — with a `[NOTE]` warning. Only when there's **also** no quality marker is the file skipped (never renamed on a guess).
- **Extension is never changed** — forcing a `.mkv` to `.mp4` would make the file unusable (wrong container); all three always keep the original extension.
- **Collisions are handled smartly** (`resolve_target`): if the desired name already exists with **identical content** (compared byte-for-byte) → the source file is left alone (no duplicate copy); if it exists with **different content** → `-1`, `-2`, ... appended until a free name is found.
- Subtitle files (`.srt/.sub/.idx/.ssa/.ass/.vtt`, plus `.smi` in the C#/C versions) whose filename starts with the same name as the video are moved along automatically, preserving their own suffix (e.g. `.nl.srt`). **`.srt` is included in all three versions** — this was explicitly re-verified after the C port (see test note below).
- No automated test suite; verification is done by reading the dry-run output before using `--apply`.

## `rename_movies.py`
```bash
python rename_movies.py "D:/Movies"          # dry run
python rename_movies.py "D:/Movies" --apply  # actually renames
```
Pattern recognition via `re` regexes. Reference implementation — if behavior ever needs to change, start here.

## `dotnet/RenameMovies/` (C#, Native AOT)
```bash
cd dotnet/RenameMovies
dotnet publish -c Release -r win-x64 -o publish
```
Produces `publish/RenameMovies.exe` (~1.8 MB), standalone, no .NET runtime needed on the target machine.
- Regexes are **source-generated** (`[GeneratedRegex]`), not `RegexOptions.Compiled` — the latter uses Reflection.Emit, which Native AOT doesn't support.
- The csproj sets `PublishAot=true` + `IlcOptimizationPreference=Size`.
- Extras over Python: `-r`/`--recursive` (process subdirectories separately), `--no-color`, per-file error handling that doesn't crash the whole batch, summary at the end.
- Publishing requires the MSVC linker (Visual Studio Build Tools, C++ workload) — if `vswhere.exe`/`link.exe` aren't on PATH, add `C:\Program Files (x86)\Microsoft Visual Studio\Installer` to PATH before publishing.

## `c/rename_movies.c` (C, MSVC)
```bash
c/build.bat
```
(Or manually: load `vcvars64.bat`, then `cl.exe /O1 /MT rename_movies.c`.) Produces `c/rename_movies.exe` (~168 KB) — the smallest achievable standalone build.
- No regex library: all pattern recognition (year/resolution/source/codec/NL-subs) is hand-written string scanning (`ci_find`, `find_earliest_idx`, `find_year`, `has_nl_subs`).
- Pure Win32 API (`FindFirstFileA`/`MoveFileA`) + C standard library, no other dependencies.
- **Deliberately ANSI/system-codepage, not wide-char Unicode** — keeps the code simple; filenames with exotic Unicode characters outside the system codepage could theoretically break. All tested release names are ASCII.
- Deliberately no short/ambiguous source codes (CAM/TS/TC/R5) in `SOURCES[]` — without a guaranteed word boundary (underscores don't count as one), those could accidentally match in the middle of a title word, especially in the no-year fallback split.
- Requires the same MSVC Build Tools as the C# publish above.

## Known differences between versions
- C# has `--recursive` and `--no-color`; Python and C do not.
- C# recognizes a few more source tags (DVDR/HDRip/REMUX) and codecs (AV1/VP9/DivX) as anchor points than Python (which wasn't updated to match); C follows the C# list.
- None of the three guarantee support for non-ASCII Unicode filenames (Python could in theory handle this via UTF-8, but it hasn't been explicitly tested).

## Test coverage note
All three versions were manually verified against the same fixture set (11 realistic release filenames covering: year present/absent, resolution-only vs. source-only format fallback, NL-subs detection, a `.nl.srt` subtitle file, and a 3-way duplicate-content collision producing skip / `-1` / rename). Output was confirmed byte-for-byte identical across Python, C#, and C, including that the `.srt` file was correctly matched and renamed alongside its video in all three.
