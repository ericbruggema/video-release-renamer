🇬🇧 English · [🇳🇱 Nederlands](README.nl.md)

# Video Release Renamer

Renames messy scene-release video filenames into a clean, consistent format — with matching subtitle files moved along automatically. Three functionally identical implementations (Python, C#, C) so you can pick whichever fits your setup, from "just run the script" to "one tiny standalone `.exe`".

```
Citizen.Vigilante.2026.1080p.BluRay.X265.DD.5.1-Chivaman.mkv
```
becomes
```
Citizen Vigilante (2026) [1080p].mkv
```

## Contents
- [Features](#features)
- [Before / after](#before--after)
- [Example run](#example-run)
- [Choosing a version](#choosing-a-version)
- [Installation & usage](#installation--usage)
- [Command-line reference](#command-line-reference)
- [How it works](#how-it-works)
- [Building from source](#building-from-source)

## Features
- Renames to `Title (Year) [Format] <NL-SUBS>.ext`
- Moves matching subtitle files along (`.srt`/`.sub`/`.idx`/`.ssa`/`.ass`/`.vtt`, plus `.smi` in the C#/C versions)
- **Dry run by default** — nothing changes until you pass `--apply`
- **Never changes the file extension**
- Smart collision handling: identical file already exists → skipped; different content → `-1`, `-2`, ...
- No year in the filename? Falls back to the quality marker as an anchor and still renames (with a warning) instead of giving up
- Three implementations: Python (reference), C# Native AOT (~1.8 MB standalone exe), C/MSVC (~168 KB standalone exe)

## Before / after

| Original filename | Renamed to |
|---|---|
| `Citizen.Vigilante.2026.1080p.BluRay.X265.DD.5.1-Chivaman.mkv` | `Citizen Vigilante (2026) [1080p].mkv` |
| `In.The.Grey.2026.BRRip Xvid Nl SubS Retail.mkv` | `In the Grey (2026) [BRRip] NL-SUBS.mkv` |
| `Rage.of.Stars.2026.1080p.WEB-DL.H264-GP-M-NLsubs.mkv` | `Rage of Stars (2026) [1080p] NL-SUBS.mkv` |
| `Run_Hide_Fight_Infidels_1080p_WEB-DL_AAC_2_0_H264-GP-M-NLsubs.mp4` | `Run Hide Fight Infidels [1080p] NL-SUBS.mp4` *(no year found → built without one, with a warning)* |
| `The Fix (2026) Screener-1080p-H265 AAC-2.0 Eng-NL-Subs.mkv` | `The Fix (2026) [1080p] NL-SUBS.mkv` |
| `young.washington.2026.1080p.bluray.x264-knives.mkv` | `Young Washington (2026) [1080p].mkv` |

Matching subtitles are renamed the same way, e.g. `Citizen.Vigilante...Chivaman.nl.srt` → `Citizen Vigilante (2026) [1080p].nl.srt`.

## Example run

Real dry-run output (C# build), unedited:

```
$ RenameMovies.exe "D:\Movies"
=== DRY RUN (nothing is being changed; add --apply to actually rename) ===

* Citizen.Vigilante.2026.1080p.BluRay.X265.DD.5.1-Chivaman.mkv
  [DRY RUN] Video: 'Citizen.Vigilante.2026.1080p.BluRay.X265.DD.5.1-Chivaman.mkv' -> 'Citizen Vigilante (2026) [1080p].mkv'

* In.The.Grey.2026.BRRip Xvid Nl SubS Retail.mkv
  [DRY RUN] Video: 'In.The.Grey.2026.BRRip Xvid Nl SubS Retail.mkv' -> 'In the Grey (2026) [BRRip] NL-SUBS.mkv'

* Rage.of.Stars.2026.1080p.WEB-DL.H264-GP-M-NLsubs.mkv
  [DRY RUN] Video: 'Rage.of.Stars.2026.1080p.WEB-DL.H264-GP-M-NLsubs.mkv' -> 'Rage of Stars (2026) [1080p] NL-SUBS.mkv'

* Run_Hide_Fight_Infidels_1080p_WEB-DL_AAC_2_0_H264-GP-M-NLsubs.mp4
  [NOTE] no year recognized, name built without (year) - check manually
  [DRY RUN] Video: 'Run_Hide_Fight_Infidels_1080p_WEB-DL_AAC_2_0_H264-GP-M-NLsubs.mp4' -> 'Run Hide Fight Infidels [1080p] NL-SUBS.mp4'

* The Fix (2026) Screener-1080p-H265 AAC-2.0 Eng-NL-Subs.mkv
  [DRY RUN] Video: 'The Fix (2026) Screener-1080p-H265 AAC-2.0 Eng-NL-Subs.mkv' -> 'The Fix (2026) [1080p] NL-SUBS.mkv'

* young.washington.2026.1080p.bluray.x264-knives.mkv
  [DRY RUN] Video: 'young.washington.2026.1080p.bluray.x264-knives.mkv' -> 'Young Washington (2026) [1080p].mkv'

------------------------------------------------------------
Directories scanned:       1
Videos would rename:        6
Subs would rename:          0
Video duplicates skipped:  0
Sub duplicates skipped:    0
Unparseable (manual):      0
```

Nothing on disk changes in this mode. Once the preview looks right, re-run with `--apply`.

## Choosing a version

| | Python | C# (Native AOT) | C (MSVC) |
|---|---|---|---|
| Run as | `python rename_movies.py` | standalone `.exe` | standalone `.exe` |
| Size | n/a (needs Python) | ~1.8 MB | ~168 KB |
| Dependencies to run | Python 3 | none | none |
| Extra options | — | `--recursive`, `--no-color` | — |
| Best for | editing the logic / reference | most Windows users | smallest possible footprint |

All three produce byte-for-byte identical renames on the same input — pick whichever is most convenient.

## Installation & usage

### Python — [`rename_movies.py`](rename_movies.py)
Requires Python 3.9+, no extra packages.
```bash
python rename_movies.py "D:/Movies"          # dry run / preview
python rename_movies.py "D:/Movies" --apply  # actually renames
```

### C# (.NET, Native AOT) — [`dotnet/RenameMovies/`](dotnet/RenameMovies/)
Build it yourself (see [Building from source](#building-from-source)).
```bash
RenameMovies.exe "D:\Movies"                       # dry run / preview
RenameMovies.exe "D:\Movies" --apply               # actually renames
RenameMovies.exe "D:\Movies" --apply --recursive   # include subfolders
```

### C (MSVC) — [`c/rename_movies.c`](c/rename_movies.c)
Build it yourself (see [Building from source](#building-from-source)).
```bash
rename_movies.exe "D:\Movies"                # dry run / preview
rename_movies.exe "D:\Movies" --apply        # actually renames
```

## Command-line reference

| Option | Python | C# | C | Description |
|---|:---:|:---:|:---:|---|
| `directory` (positional) | ✅ | ✅ | ✅ | Directory to scan (default: current directory) |
| `--apply` | ✅ | ✅ | ✅ | Actually perform the renames (default: dry run) |
| `-r`, `--recursive` | — | ✅ | — | Also scan subdirectories, each processed separately |
| `--no-color` | — | ✅ | — | Disable colored console output |
| `-h`, `--help` | ✅ | ✅ | ✅ | Show usage |

## How it works
- Detects **year** (`19xx`/`20xx`), **resolution** (1080p/720p/...), **source** (BluRay/WEB-DL/BRRip/...), and **NL subtitle markers** from the filename.
- `[Format]` shows **only the resolution** (or the source, if no resolution is found) — codec and audio info are intentionally left out.
- **No year in the name?** The first quality marker found is used as an anchor to split the title from the release tags, and the file is still renamed — just without `(year)` — with a `[NOTE]` warning so you can double-check it.
- **No year AND no quality marker at all?** The file is skipped rather than renamed on a guess.
- **Extension is never changed** — a `.mkv` renamed to `.mp4` would just be a broken file (wrong container).
- **Collisions**: if the target name already exists with **identical content**, the source file is left untouched (no duplicate). If it exists with **different content**, `-1`, `-2`, ... is appended until a free name is found.
- **Subtitles** (`.srt`, `.sub`, `.idx`, `.ssa`, `.ass`, `.vtt`, plus `.smi` in the C#/C versions) whose filename starts with the same name as the video are renamed and moved along automatically, keeping their own suffix (e.g. `.nl.srt`).
- Nothing is ever touched without `--apply` — always review the dry-run output first.

## Building from source

### C# (Native AOT)
Requires the [.NET SDK](https://dotnet.microsoft.com/download) and the MSVC linker (Visual Studio Build Tools, "Desktop development with C++" workload).
```bash
cd dotnet/RenameMovies
dotnet publish -c Release -r win-x64 -o publish
```
Produces `publish/RenameMovies.exe` (~1.8 MB), fully standalone — no .NET runtime needed on the target machine.

### C (MSVC)
Requires the Visual Studio Build Tools (C++ workload).
```bash
c/build.bat
```
Produces `c/rename_movies.exe` (~168 KB), fully standalone.

---

More implementation details and design decisions are documented in [CLAUDE.md](CLAUDE.md).
