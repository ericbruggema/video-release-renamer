[🇬🇧 English](README.md) · 🇳🇱 Nederlands

# Video Release Renamer

Hernoemt rommelige scene-release videobestandsnamen naar een nette, consistente indeling — met bijbehorende ondertitelbestanden die automatisch meeverhuizen. Drie functioneel identieke implementaties (Python, C#, C), zodat je kunt kiezen wat het beste bij je past: van "gewoon het script draaien" tot "één piepklein standalone `.exe`-bestand".

```
Citizen.Vigilante.2026.1080p.BluRay.X265.DD.5.1-Chivaman.mkv
```
wordt
```
Citizen Vigilante (2026) [1080p].mkv
```

## Inhoud
- [Features](#features)
- [Voor / na](#voor--na)
- [Voorbeeld-run](#voorbeeld-run)
- [Welke versie kiezen](#welke-versie-kiezen)
- [Installatie & gebruik](#installatie--gebruik)
- [Command-line opties](#command-line-opties)
- [Hoe het werkt](#hoe-het-werkt)
- [Zelf bouwen vanaf source](#zelf-bouwen-vanaf-source)

## Features
- Hernoemt naar `Titel (Jaar) [Formaat] <NL-SUBS>.ext`
- Verhuist bijbehorende ondertitelbestanden automatisch mee (`.srt`/`.sub`/`.idx`/`.ssa`/`.ass`/`.vtt`, plus `.smi` in de C#/C-versies)
- **Standaard dry run** — er verandert niets totdat je `--apply` meegeeft
- **Wijzigt nooit de bestandsextensie**
- Slimme botsingsafhandeling: bestaat een identiek bestand al → overgeslagen; andere inhoud → `-1`, `-2`, ...
- Geen jaartal in de bestandsnaam? Valt terug op de kwaliteitsmarker als ankerpunt en hernoemt alsnog (met een waarschuwing) in plaats van het simpelweg op te geven
- Drie implementaties: Python (referentie), C# Native AOT (~1,8 MB standalone exe), C/MSVC (~168 KB standalone exe)

## Voor / na

| Originele bestandsnaam | Hernoemd naar |
|---|---|
| `Citizen.Vigilante.2026.1080p.BluRay.X265.DD.5.1-Chivaman.mkv` | `Citizen Vigilante (2026) [1080p].mkv` |
| `In.The.Grey.2026.BRRip Xvid Nl SubS Retail.mkv` | `In the Grey (2026) [BRRip] NL-SUBS.mkv` |
| `Rage.of.Stars.2026.1080p.WEB-DL.H264-GP-M-NLsubs.mkv` | `Rage of Stars (2026) [1080p] NL-SUBS.mkv` |
| `Run_Hide_Fight_Infidels_1080p_WEB-DL_AAC_2_0_H264-GP-M-NLsubs.mp4` | `Run Hide Fight Infidels [1080p] NL-SUBS.mp4` *(geen jaartal gevonden → gebouwd zonder, met waarschuwing)* |
| `The Fix (2026) Screener-1080p-H265 AAC-2.0 Eng-NL-Subs.mkv` | `The Fix (2026) [1080p] NL-SUBS.mkv` |
| `young.washington.2026.1080p.bluray.x264-knives.mkv` | `Young Washington (2026) [1080p].mkv` |

Bijbehorende ondertitels worden op dezelfde manier hernoemd, bv. `Citizen.Vigilante...Chivaman.nl.srt` → `Citizen Vigilante (2026) [1080p].nl.srt`.

## Voorbeeld-run

Echte dry-run-output (C#-build), ongewijzigd:

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

(De tool-output zelf is in het Engels, ongeacht welke taal je hier leest — zie [waarom](#hoe-het-werkt).) Er verandert niets op disk in deze modus. Ziet de preview er goed uit? Draai dan opnieuw met `--apply`.

## Welke versie kiezen

| | Python | C# (Native AOT) | C (MSVC) |
|---|---|---|---|
| Draait als | `python rename_movies.py` | standalone `.exe` | standalone `.exe` |
| Grootte | n.v.t. (Python nodig) | ~1,8 MB | ~168 KB |
| Dependencies om te draaien | Python 3 | geen | geen |
| Extra opties | — | `--recursive`, `--no-color` | — |
| Handigst voor | logica aanpassen / referentie | de meeste Windows-gebruikers | zo klein mogelijk |

Alle drie leveren byte-voor-byte identieke hernoemingen op dezelfde input — kies wat het handigst uitkomt.

## Installatie & gebruik

### Python — [`rename_movies.py`](rename_movies.py)
Vereist Python 3.9+, geen extra packages.
```bash
python rename_movies.py "D:/Movies"          # dry run / preview
python rename_movies.py "D:/Movies" --apply  # echt hernoemen
```

### C# (.NET, Native AOT) — [`dotnet/RenameMovies/`](dotnet/RenameMovies/)
Zelf bouwen (zie [Zelf bouwen vanaf source](#zelf-bouwen-vanaf-source)).
```bash
RenameMovies.exe "D:\Movies"                       # dry run / preview
RenameMovies.exe "D:\Movies" --apply               # echt hernoemen
RenameMovies.exe "D:\Movies" --apply --recursive   # inclusief submappen
```

### C (MSVC) — [`c/rename_movies.c`](c/rename_movies.c)
Zelf bouwen (zie [Zelf bouwen vanaf source](#zelf-bouwen-vanaf-source)).
```bash
rename_movies.exe "D:\Movies"                # dry run / preview
rename_movies.exe "D:\Movies" --apply        # echt hernoemen
```

## Command-line opties

| Optie | Python | C# | C | Beschrijving |
|---|:---:|:---:|:---:|---|
| `map` (positioneel) | ✅ | ✅ | ✅ | Map om te scannen (default: huidige map) |
| `--apply` | ✅ | ✅ | ✅ | Voer de hernoemingen echt uit (default: dry run) |
| `-r`, `--recursive` | — | ✅ | — | Doorzoek ook submappen, elk apart verwerkt |
| `--no-color` | — | ✅ | — | Geen kleur in de console-output |
| `-h`, `--help` | ✅ | ✅ | ✅ | Toon hulptekst |

## Hoe het werkt
- Herkent **jaartal** (`19xx`/`20xx`), **resolutie** (1080p/720p/...), **bron** (BluRay/WEB-DL/BRRip/...) en **NL-ondertitel-markers** uit de bestandsnaam.
- `[Formaat]` toont **alleen de resolutie** (of de bron, als er geen resolutie gevonden is) — codec- en audio-info worden bewust weggelaten.
- **Geen jaartal in de naam?** De eerst gevonden kwaliteitsmarker wordt als ankerpunt gebruikt om titel en releasetags te splitsen, en het bestand wordt alsnog hernoemd — alleen zonder `(jaar)` — met een `[NOTE]`-waarschuwing zodat je het kunt controleren.
- **Geen jaartal ÉN geen kwaliteitsmarker?** Het bestand wordt overgeslagen in plaats van gegokt hernoemd.
- **Extensie wordt nooit gewijzigd** — een `.mkv` hernoemd naar `.mp4` zou gewoon een kapot bestand zijn (verkeerde container).
- **Botsingen**: bestaat de doelnaam al met **identieke inhoud**, dan blijft het bronbestand onaangeroerd (geen dubbele kopie). Bestaat hij met **andere inhoud**, dan wordt `-1`, `-2`, ... toegevoegd tot er een vrije naam is.
- **Ondertitels** (`.srt`, `.sub`, `.idx`, `.ssa`, `.ass`, `.vtt`, plus `.smi` in de C#/C-versies) die met dezelfde naam beginnen als de video worden automatisch hernoemd en meeverhuisd, met behoud van hun eigen suffix (bv. `.nl.srt`).
- Er wordt nooit iets aangeraakt zonder `--apply` — bekijk altijd eerst de dry-run-output.

## Zelf bouwen vanaf source

### C# (Native AOT)
Vereist de [.NET SDK](https://dotnet.microsoft.com/download) en de MSVC-linker (Visual Studio Build Tools, workload "Desktop development with C++").
```bash
cd dotnet/RenameMovies
dotnet publish -c Release -r win-x64 -o publish
```
Levert `publish/RenameMovies.exe` op (~1,8 MB), volledig standalone — geen .NET-runtime nodig op de doelmachine.

### C (MSVC)
Vereist de Visual Studio Build Tools (C++ workload).
```bash
c/build.bat
```
Levert `c/rename_movies.exe` op (~168 KB), volledig standalone.

---

Meer implementatiedetails en ontwerpkeuzes staan in [CLAUDE.md](CLAUDE.md) (Engelstalig).
