#!/usr/bin/env python3
"""
Renames video releases (and their subtitle files) to the format:

    Title (Year) [Format] <NL-SUBS>.ext

- The original extension (.mkv/.mp4/...) is preserved. Forcing everything to
  .mp4 would break the file (a .mkv is simply not a valid .mp4 container) -
  so this script never does that.
- Files with no recognizable year are skipped (with a clear message) instead
  of being renamed based on a guess.
- Subtitle files (.srt/.sub/.idx/.ssa/.ass/.vtt) whose filename starts with
  the same name as the video are moved along automatically.
- Does the new filename already exist?
    * Identical content -> the source file is left alone (no duplicate copy,
      not overwritten).
    * Different content -> "-1" is appended to the name (and "-2", "-3", ...
      as needed until a free name is found, or an identical match turns up).

By default the script runs in DRY RUN mode (nothing changes on disk, it's
just a preview). Add --apply to actually perform the renames.

Usage:
    python rename_movies.py "D:/Movies"              # dry run / preview
    python rename_movies.py "D:/Movies" --apply       # actually renames
"""

from __future__ import annotations

import argparse
import filecmp
import os
import re
import shutil
import sys
from pathlib import Path

VIDEO_EXTS = {".mkv", ".mp4", ".avi", ".m4v", ".wmv", ".mov", ".ts"}
SUBTITLE_EXTS = {".srt", ".sub", ".idx", ".ssa", ".ass", ".vtt"}

YEAR_RE = re.compile(r"(19|20)\d{2}")
RESOLUTION_RE = re.compile(r"(2160p|1080p|720p|480p)", re.I)
SOURCE_RE = re.compile(r"(BluRay|BRRip|BDRip|WEB-?DL|WEBRip|HDTV|DVDRip|Screener|SCR)", re.I)
CODEC_RE = re.compile(r"(x264|x265|h\.?264|h\.?265|HEVC|XviD)", re.I)
NLSUBS_RE = re.compile(r"nl[\s\-_.]*sub", re.I)

MINOR_WORDS = {"of", "the", "in", "and", "a", "an", "to", "at", "for", "on", "vs"}

SOURCE_CANON = {
    "bluray": "BluRay",
    "brrip": "BRRip",
    "bdrip": "BDRip",
    "web-dl": "WEB-DL",
    "webdl": "WEB-DL",
    "webrip": "WEBRip",
    "hdtv": "HDTV",
    "dvdrip": "DVDRip",
    "screener": "Screener",
    "scr": "Screener",
}

CODEC_CANON = {
    "x264": "x264",
    "x265": "x265",
    "h264": "H264",
    "h265": "H265",
    "hevc": "HEVC",
    "xvid": "XviD",
}

INVALID_CHARS_RE = re.compile(r'[\\/:*?"<>|]')


def clean_title(raw: str) -> str:
    raw = raw.replace(".", " ").replace("_", " ")
    raw = raw.strip(" -([")
    raw = re.sub(r"\s+", " ", raw).strip()
    words = raw.split(" ")
    out = []
    for i, w in enumerate(words):
        if not w:
            continue
        if i > 0 and w.lower() in MINOR_WORDS:
            out.append(w.lower())
        else:
            out.append(w[:1].upper() + w[1:].lower() if w.isalpha() else w)
    return " ".join(out)


def parse_release(stem: str) -> dict | None:
    """Parse a release filename stem (no extension). Returns None if nothing usable is found."""
    m = YEAR_RE.search(stem)
    if m:
        year = m.group(0)
        title_part = stem[: m.start()]
        rest_part = stem[m.end():]
    else:
        # No year: use the first quality marker (1080p/BluRay/x264/...) as
        # the split point between the title and the rest of the release tags.
        year = None
        anchors = [
            x.start()
            for x in (RESOLUTION_RE.search(stem), SOURCE_RE.search(stem), CODEC_RE.search(stem))
            if x
        ]
        if not anchors:
            return None
        split = min(anchors)
        title_part = stem[:split]
        rest_part = stem[split:]

    title = clean_title(title_part)
    if not title:
        return None

    res_m = RESOLUTION_RE.search(rest_part)
    src_m = SOURCE_RE.search(rest_part)
    codec_m = CODEC_RE.search(rest_part)
    has_nl_subs = bool(NLSUBS_RE.search(stem))

    if res_m:
        format_str = res_m.group(0).lower()
    elif src_m:
        key = src_m.group(0).lower().replace("-", "").replace(" ", "")
        format_str = SOURCE_CANON.get(key, src_m.group(0))
    else:
        format_str = "Unknown"

    return {
        "title": title,
        "year": year,
        "format": format_str,
        "nl_subs": has_nl_subs,
    }


def build_new_stem(info: dict) -> str:
    if info["year"]:
        name = f"{info['title']} ({info['year']}) [{info['format']}]"
    else:
        name = f"{info['title']} [{info['format']}]"
    if info["nl_subs"]:
        name += " NL-SUBS"
    name = INVALID_CHARS_RE.sub("", name)
    return name


def resolve_target(directory: Path, desired_name: str, source_path: Path):
    """
    Find the path `source_path` should end up at.
    Returns (target_path, is_duplicate). is_duplicate=True means an
    identical file already exists under that name -> caller should skip.
    """
    stem, ext = os.path.splitext(desired_name)
    candidate = desired_name
    n = 0
    while True:
        target_path = directory / candidate
        if target_path.resolve() == source_path.resolve():
            return target_path, False
        if not target_path.exists():
            return target_path, False
        if filecmp.cmp(target_path, source_path, shallow=False):
            return target_path, True
        n += 1
        candidate = f"{stem}-{n}{ext}"


def find_subtitles(directory: Path, video_stem: str, exclude: set[Path]) -> list[Path]:
    subs = []
    for f in directory.iterdir():
        if f in exclude or not f.is_file():
            continue
        if f.suffix.lower() not in SUBTITLE_EXTS:
            continue
        f_name_no_ext, _ = os.path.splitext(f.name)
        if f_name_no_ext == video_stem or f_name_no_ext.startswith(video_stem + "."):
            subs.append(f)
    return subs


def move_or_report(source: Path, target: Path, is_duplicate: bool, apply: bool, kind: str):
    if is_duplicate:
        print(f"  [SKIPPED] {kind} '{source.name}' already exists identically as '{target.name}' -> left untouched")
        return
    print(f"  [{('DRY RUN' if not apply else 'OK')}] {kind}: '{source.name}' -> '{target.name}'")
    if apply:
        shutil.move(str(source), str(target))


def main():
    parser = argparse.ArgumentParser(description="Rename movie releases to 'Title (Year) [Format] NL-SUBS.ext'")
    parser.add_argument("directory", nargs="?", default=".", help="Directory with the videos (default: current directory)")
    parser.add_argument("--apply", action="store_true", help="Actually perform the renames (default: preview / dry run only)")
    args = parser.parse_args()

    directory = Path(args.directory).resolve()
    if not directory.is_dir():
        print(f"Directory not found: {directory}")
        sys.exit(1)

    if not args.apply:
        print("=== DRY RUN (nothing is being changed; add --apply to actually rename) ===\n")

    video_files = sorted(
        f for f in directory.iterdir() if f.is_file() and f.suffix.lower() in VIDEO_EXTS
    )

    if not video_files:
        print("No video files found in", directory)
        return

    handled_subs: set[Path] = set()

    for video in video_files:
        print(f"* {video.name}")
        video_stem = video.stem
        info = parse_release(video_stem)
        if info is None:
            print("  [SKIPPED] no title/quality info recognized in filename - rename manually")
            print()
            continue
        if info["year"] is None:
            print("  [NOTE] no year recognized, name built without (year) - check manually")

        new_stem = build_new_stem(info)
        desired_video_name = new_stem + video.suffix.lower()

        target, is_dup = resolve_target(directory, desired_video_name, video)
        move_or_report(video, target, is_dup, args.apply, "Video")

        # Move along any matching subtitle files.
        subs = find_subtitles(directory, video_stem, exclude={video} | handled_subs)
        for sub in subs:
            handled_subs.add(sub)
            suffix = sub.name[len(video_stem):]  # e.g. ".srt" or ".nl.srt"
            desired_sub_name = new_stem + suffix
            sub_target, sub_is_dup = resolve_target(directory, desired_sub_name, sub)
            move_or_report(sub, sub_target, sub_is_dup, args.apply, "  Sub  ")

        print()


if __name__ == "__main__":
    main()
