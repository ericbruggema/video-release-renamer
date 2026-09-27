/*
 * rename_movies.c - renames video releases (and their subtitle files) to
 * "Title (Year) [Format] <NL-SUBS>.ext". A functional 1:1 port of
 * rename_movies.py / the C# RenameMovies project, but without a regex
 * library and without a runtime: just the Win32 API + the C standard
 * library. Compiles to a few hundred KB, no dependencies to run.
 *
 * Deliberate choice: filenames are treated as "ANSI"/system-codepage bytes
 * (FindFirstFileA/MoveFileA), not as wide-char Unicode. All example
 * filenames are ASCII; this keeps the code a lot simpler. Names with exotic
 * Unicode characters outside the system codepage could theoretically break -
 * see CLAUDE.md for the trade-off.
 */

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define MAX_NAME  512
#define MAX_FULL  1024
#define MAX_FILES 4096

/* ---------- candidate lists (replace regex alternations) ---------- */

static const char* RESOLUTIONS[] = { "2160p", "4k", "1080p", "720p", "480p", "360p" };
#define RES_COUNT (int)(sizeof(RESOLUTIONS)/sizeof(RESOLUTIONS[0]))

/* Deliberately no short/ambiguous codes (CAM, TS, TC, R5): without a
 * guaranteed word boundary those could accidentally match in the middle of
 * a title word, especially in the no-year fallback split below. */
static const char* SOURCES[]        = { "BluRay","BRRip","BDRip","WEB-DL","WEBDL","WEBRip","HDTV","DVDRip","DVDR","HDRip","REMUX","Screener","SCR" };
static const char* SOURCE_DISPLAY[] = { "BluRay","BRRip","BDRip","WEB-DL","WEB-DL","WEBRip","HDTV","DVDRip","DVDR","HDRip","REMUX","Screener","Screener" };
#define SRC_COUNT (int)(sizeof(SOURCES)/sizeof(SOURCES[0]))

/* Codec is not shown in the filename (per request), but remains an anchor
 * point to split the title/quality section when there's no year. */
static const char* CODECS[] = { "x264","x265","h264","h.264","h265","h.265","HEVC","AV1","VP9","XviD","DivX" };
#define CODEC_COUNT (int)(sizeof(CODECS)/sizeof(CODECS[0]))

static const char* VIDEO_EXTS[] = { ".mkv",".mp4",".avi",".m4v",".wmv",".mov",".ts",".webm" };
#define VIDEO_EXT_COUNT (int)(sizeof(VIDEO_EXTS)/sizeof(VIDEO_EXTS[0]))

static const char* SUB_EXTS[] = { ".srt",".sub",".idx",".ssa",".ass",".vtt",".smi" };
#define SUB_EXT_COUNT (int)(sizeof(SUB_EXTS)/sizeof(SUB_EXTS[0]))

static const char* MINOR_WORDS[] = { "of","the","in","and","a","an","to","at","for","on","vs", NULL };

typedef struct {
    char title[MAX_NAME];
    char year[8];
    int  hasYear;
    char format[64];
    int  hasNlSubs;
} ParsedInfo;

typedef struct {
    char name[MAX_NAME];
    int  handled; /* already moved along as a subtitle of an earlier video */
} FileEntry;

typedef struct {
    int videosRenamed, videosDupSkipped, videosUnparsed;
    int subsRenamed, subsDupSkipped;
    int errors;
} Stats;

/* ---------------------------- console color ----------------------------- */

static HANDLE g_hConsole;
static WORD   g_defaultAttrs = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

static void init_console(void) {
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(g_hConsole, &info)) {
        g_defaultAttrs = info.wAttributes;
    }
}

static void printc(WORD color, const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    SetConsoleTextAttribute(g_hConsole, color);
    fputs(buf, stdout);
    SetConsoleTextAttribute(g_hConsole, g_defaultAttrs);
}

/* ------------------------- string helpers --------------------------- */

/* Case-insensitive substring search (MSVC has no strcasestr). */
static const char* ci_find(const char* haystack, const char* needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;
    for (const char* p = haystack; *p; p++) {
        if (_strnicmp(p, needle, nlen) == 0) return p;
    }
    return NULL;
}

/* Finds, across a list of candidate literals, the one that occurs earliest
 * in s (case-insensitive). On a tied start position, the first candidate in
 * the list wins - just like regex alternation at the same position. */
static int find_earliest_idx(const char* s, const char** candidates, int count, int* out_pos) {
    int best_pos = -1, best_i = -1;
    for (int i = 0; i < count; i++) {
        const char* found = ci_find(s, candidates[i]);
        if (found) {
            int pos = (int)(found - s);
            if (best_pos == -1 || pos < best_pos) { best_pos = pos; best_i = i; }
        }
    }
    if (out_pos) *out_pos = best_pos;
    return best_i;
}

static int find_year(const char* s, int* out_index, char year_out[5]) {
    size_t len = strlen(s);
    for (size_t i = 0; i + 4 <= len; i++) {
        if (isdigit((unsigned char)s[i+2]) && isdigit((unsigned char)s[i+3]) &&
            ((s[i]=='1' && s[i+1]=='9') || (s[i]=='2' && s[i+1]=='0'))) {
            *out_index = (int)i;
            memcpy(year_out, s + i, 4);
            year_out[4] = '\0';
            return 1;
        }
    }
    return 0;
}

/* nl[\s\-_.]*sub, case-insensitive. */
static int has_nl_subs(const char* s) {
    size_t len = strlen(s);
    for (size_t i = 0; i + 2 <= len; i++) {
        if ((s[i]=='n'||s[i]=='N') && (s[i+1]=='l'||s[i+1]=='L')) {
            size_t j = i + 2;
            while (j < len && strchr(" -_.", s[j])) j++;
            if (j + 3 <= len && _strnicmp(s + j, "sub", 3) == 0) return 1;
        }
    }
    return 0;
}

static void split_stem_ext(const char* name, char* stem, size_t stem_size, char* ext, size_t ext_size) {
    const char* dot = strrchr(name, '.');
    if (!dot) {
        strncpy(stem, name, stem_size - 1); stem[stem_size - 1] = 0;
        ext[0] = 0;
        return;
    }
    size_t stem_len = (size_t)(dot - name);
    if (stem_len >= stem_size) stem_len = stem_size - 1;
    memcpy(stem, name, stem_len); stem[stem_len] = 0;
    strncpy(ext, dot, ext_size - 1); ext[ext_size - 1] = 0;
}

static int ends_with_ext(const char* name, const char** exts, int count) {
    const char* dot = strrchr(name, '.');
    if (!dot) return -1;
    for (int i = 0; i < count; i++) {
        if (_stricmp(dot, exts[i]) == 0) return i;
    }
    return -1;
}

static void sanitize_filename(char* s) {
    char* w = s;
    for (char* r = s; *r; r++) {
        unsigned char c = (unsigned char)*r;
        if (c < 0x20) continue;
        if (strchr("\\/:*?\"<>|", *r)) continue;
        *w++ = *r;
    }
    *w = 0;
}

/* ".", "_" -> space; trim junk from the edges; title-case words, minor
 * connector words (except as the first word) stay lowercase; words
 * containing non-letters (digits etc.) are left unchanged - same rules as
 * the Python/C# versions. */
static void clean_title(const char* raw, char* out, size_t out_size) {
    char buf[MAX_NAME];
    size_t bi = 0;
    for (const char* p = raw; *p && bi < sizeof(buf) - 1; p++) {
        char c = *p;
        if (c == '.' || c == '_') c = ' ';
        buf[bi++] = c;
    }
    buf[bi] = 0;

    char* start = buf;
    while (*start && strchr(" -([", *start)) start++;
    char* end = start + strlen(start);
    while (end > start && strchr(" -([", *(end - 1))) end--;
    *end = 0;

    char collapsed[MAX_NAME];
    size_t ci = 0;
    int prev_space = 0;
    for (char* p = start; *p; p++) {
        if (*p == ' ') {
            if (!prev_space) collapsed[ci++] = ' ';
            prev_space = 1;
        } else {
            collapsed[ci++] = *p;
            prev_space = 0;
        }
    }
    while (ci > 0 && collapsed[ci - 1] == ' ') ci--;
    collapsed[ci] = 0;

    out[0] = 0;
    size_t oi = 0;
    const char* p = collapsed;
    int word_idx = 0;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        size_t wlen = 0;
        while (p[wlen] && p[wlen] != ' ') wlen++;

        char word[256];
        size_t copy_len = wlen < sizeof(word) - 1 ? wlen : sizeof(word) - 1;
        memcpy(word, p, copy_len);
        word[copy_len] = 0;

        int is_minor = 0;
        if (word_idx > 0) {
            for (int m = 0; MINOR_WORDS[m]; m++) {
                if (_stricmp(word, MINOR_WORDS[m]) == 0) { is_minor = 1; break; }
            }
        }

        char transformed[256];
        if (is_minor) {
            size_t k = 0;
            for (; word[k]; k++) transformed[k] = (char)tolower((unsigned char)word[k]);
            transformed[k] = 0;
        } else {
            int all_alpha = word[0] != 0;
            for (size_t k = 0; word[k]; k++) {
                if (!isalpha((unsigned char)word[k])) { all_alpha = 0; break; }
            }
            if (all_alpha) {
                transformed[0] = (char)toupper((unsigned char)word[0]);
                size_t k = 1;
                for (; word[k]; k++) transformed[k] = (char)tolower((unsigned char)word[k]);
                transformed[k] = 0;
            } else {
                strcpy(transformed, word);
            }
        }

        if (oi > 0 && oi < out_size - 1) out[oi++] = ' ';
        size_t tlen = strlen(transformed);
        if (oi + tlen < out_size - 1) { memcpy(out + oi, transformed, tlen); oi += tlen; }
        out[oi] = 0;

        p += wlen;
        word_idx++;
    }
}

/* Returns 0 if nothing usable (no year AND no quality marker) was found. */
static int parse_release(const char* stem, ParsedInfo* info) {
    int year_idx = -1;
    char year[5];
    int has_year = find_year(stem, &year_idx, year);

    size_t title_len;
    const char* rest;

    if (has_year) {
        title_len = (size_t)year_idx;
        rest = stem + year_idx + 4;
    } else {
        int res_pos = -1, src_pos = -1, codec_pos = -1;
        int res_i    = find_earliest_idx(stem, RESOLUTIONS, RES_COUNT, &res_pos);
        int src_i    = find_earliest_idx(stem, SOURCES, SRC_COUNT, &src_pos);
        int codec_i  = find_earliest_idx(stem, CODECS, CODEC_COUNT, &codec_pos);
        int best = -1;
        if (res_i   != -1) best = res_pos;
        if (src_i   != -1 && (best == -1 || src_pos   < best)) best = src_pos;
        if (codec_i != -1 && (best == -1 || codec_pos < best)) best = codec_pos;
        if (best == -1) return 0;
        title_len = (size_t)best;
        rest = stem + best;
    }

    char title_part[MAX_NAME];
    size_t copy_len = title_len < sizeof(title_part) - 1 ? title_len : sizeof(title_part) - 1;
    memcpy(title_part, stem, copy_len);
    title_part[copy_len] = 0;

    clean_title(title_part, info->title, sizeof(info->title));
    if (info->title[0] == 0) return 0;

    int res_pos = 0, src_pos = 0;
    int res_i = find_earliest_idx(rest, RESOLUTIONS, RES_COUNT, &res_pos);
    int src_i = find_earliest_idx(rest, SOURCES, SRC_COUNT, &src_pos);

    if (res_i != -1) {
        if (_stricmp(RESOLUTIONS[res_i], "4k") == 0) strcpy(info->format, "4K");
        else { strncpy(info->format, RESOLUTIONS[res_i], sizeof(info->format) - 1); info->format[sizeof(info->format)-1]=0; }
    } else if (src_i != -1) {
        strncpy(info->format, SOURCE_DISPLAY[src_i], sizeof(info->format) - 1);
        info->format[sizeof(info->format) - 1] = 0;
    } else {
        strcpy(info->format, "Unknown");
    }

    info->hasYear = has_year;
    if (has_year) strcpy(info->year, year); else info->year[0] = 0;
    info->hasNlSubs = has_nl_subs(stem);
    return 1;
}

static void build_new_stem(const ParsedInfo* info, char* out, size_t out_size) {
    char tmp[MAX_NAME];
    if (info->hasYear) snprintf(tmp, sizeof(tmp), "%s (%s) [%s]", info->title, info->year, info->format);
    else                snprintf(tmp, sizeof(tmp), "%s [%s]", info->title, info->format);
    if (info->hasNlSubs) strncat(tmp, " NL-SUBS", sizeof(tmp) - strlen(tmp) - 1);
    sanitize_filename(tmp);
    strncpy(out, tmp, out_size - 1);
    out[out_size - 1] = 0;
}

/* Exact byte-for-byte comparison, streamed in 1 MB chunks - no full video
 * files loaded into memory. Equivalent of filecmp.cmp(shallow=False). */
static int files_identical(const char* pathA, const char* pathB) {
    FILE* fa = fopen(pathA, "rb");
    FILE* fb = fopen(pathB, "rb");
    if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return 0; }

    static char bufA[1 << 20];
    static char bufB[1 << 20];
    int identical = 1;
    for (;;) {
        size_t ra = fread(bufA, 1, sizeof(bufA), fa);
        size_t rb = fread(bufB, 1, sizeof(bufB), fb);
        if (ra != rb || memcmp(bufA, bufB, ra) != 0) { identical = 0; break; }
        if (ra == 0) break;
    }
    fclose(fa); fclose(fb);
    return identical;
}

/* Determines where source_path should end up. If the desired name already
 * exists with identical content -> is_dup=1 (don't touch it). Different
 * content -> "-1", "-2", ... until a free name or an identical match is found. */
static void resolve_target(const char* dir, const char* desired_name, const char* source_path,
                            char* target_out, size_t target_size, int* is_dup) {
    char stem[MAX_NAME], ext[32];
    split_stem_ext(desired_name, stem, sizeof(stem), ext, sizeof(ext));

    char candidate[MAX_NAME];
    strncpy(candidate, desired_name, sizeof(candidate) - 1); candidate[sizeof(candidate) - 1] = 0;

    int n = 0;
    for (;;) {
        char target_path[MAX_FULL];
        snprintf(target_path, sizeof(target_path), "%s\\%s", dir, candidate);

        if (_stricmp(target_path, source_path) == 0) {
            strncpy(target_out, target_path, target_size - 1); target_out[target_size - 1] = 0;
            *is_dup = 0;
            return;
        }

        DWORD attrs = GetFileAttributesA(target_path);
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            strncpy(target_out, target_path, target_size - 1); target_out[target_size - 1] = 0;
            *is_dup = 0;
            return;
        }

        if (files_identical(target_path, source_path)) {
            strncpy(target_out, target_path, target_size - 1); target_out[target_size - 1] = 0;
            *is_dup = 1;
            return;
        }

        n++;
        snprintf(candidate, sizeof(candidate), "%s-%d%s", stem, n, ext);
    }
}

static void move_or_report(const char* source_path, const char* source_name, const char* target,
                            int is_dup, int apply, const char* kind, Stats* stats, int isVideo) {
    const char* target_name = strrchr(target, '\\');
    target_name = target_name ? target_name + 1 : target;

    if (is_dup) {
        printc(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
               "  [SKIPPED] %s '%s' already exists identically as '%s' -> left untouched\n", kind, source_name, target_name);
        if (isVideo) stats->videosDupSkipped++; else stats->subsDupSkipped++;
        return;
    }

    const char* tag = apply ? "OK" : "DRY RUN";
    WORD color = apply ? (FOREGROUND_GREEN | FOREGROUND_INTENSITY) : (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    printc(color, "  [%s] %s: '%s' -> '%s'\n", tag, kind, source_name, target_name);

    if (apply) {
        if (!MoveFileA(source_path, target)) {
            printc(FOREGROUND_RED | FOREGROUND_INTENSITY, "  [ERROR] rename failed (error code %lu)\n", GetLastError());
            stats->errors++;
            return;
        }
    }
    if (isVideo) stats->videosRenamed++; else stats->subsRenamed++;
}

static int is_subtitle_match(const char* filename, const char* video_stem) {
    char stem[MAX_NAME], ext[32];
    split_stem_ext(filename, stem, sizeof(stem), ext, sizeof(ext));
    if (ext[0] == 0) return 0;

    int ext_ok = 0;
    for (int i = 0; i < SUB_EXT_COUNT; i++) {
        if (_stricmp(ext, SUB_EXTS[i]) == 0) { ext_ok = 1; break; }
    }
    if (!ext_ok) return 0;

    size_t vlen = strlen(video_stem);
    if (strcmp(stem, video_stem) == 0) return 1;
    if (strncmp(stem, video_stem, vlen) == 0 && stem[vlen] == '.') return 1;
    return 0;
}

static int compare_names(const void* a, const void* b) {
    return _stricmp((const char*)a, (const char*)b);
}

static void print_usage(void) {
    printf(
        "RenameMovies - renames video releases to 'Title (Year) [Format] NL-SUBS.ext'\n\n"
        "Usage:\n"
        "  rename_movies.exe [directory] [options]\n\n"
        "Arguments:\n"
        "  directory        Directory with the videos (default: current directory)\n\n"
        "Options:\n"
        "  --apply          Actually perform the renames (default: dry run / preview)\n"
        "  -h, --help       Show this help text\n\n"
        "Behavior:\n"
        "  - Recognizes year, resolution, source, and NL subtitle markers from the filename.\n"
        "  - No year? The quality marker is used as an anchor and the name is built\n"
        "    without (year) - with a warning.\n"
        "  - No year AND no quality marker? File is skipped.\n"
        "  - Extension is never changed.\n"
        "  - Desired name already exists identically -> left untouched. Different content -> \"-1\", \"-2\", ...\n"
        "  - Subtitles (.srt/.sub/.idx/.ssa/.ass/.vtt/.smi) are moved along automatically.\n"
    );
}

int main(int argc, char** argv) {
    const char* directory = ".";
    int apply = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--apply") == 0) {
            apply = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage();
            return 2;
        } else {
            directory = argv[i];
        }
    }

    init_console();

    char full_dir[MAX_FULL];
    DWORD len = GetFullPathNameA(directory, sizeof(full_dir), full_dir, NULL);
    if (len == 0 || len >= sizeof(full_dir)) {
        fprintf(stderr, "Invalid path: %s\n", directory);
        return 1;
    }

    DWORD attrs = GetFileAttributesA(full_dir);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        fprintf(stderr, "Directory not found: %s\n", full_dir);
        return 1;
    }

    if (!apply) {
        printc(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
               "=== DRY RUN (nothing is being changed; add --apply to actually rename) ===\n\n");
    }

    static FileEntry all_files[MAX_FILES];
    int all_count = 0;

    char search_pattern[MAX_FULL];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*", full_dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (all_count < MAX_FILES) {
                strncpy(all_files[all_count].name, fd.cFileName, MAX_NAME - 1);
                all_files[all_count].name[MAX_NAME - 1] = 0;
                all_files[all_count].handled = 0;
                all_count++;
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    /* collect video files and sort alphabetically for deterministic order */
    static char video_names[MAX_FILES][MAX_NAME];
    int video_count = 0;
    for (int i = 0; i < all_count; i++) {
        if (ends_with_ext(all_files[i].name, VIDEO_EXTS, VIDEO_EXT_COUNT) >= 0) {
            strncpy(video_names[video_count], all_files[i].name, MAX_NAME - 1);
            video_names[video_count][MAX_NAME - 1] = 0;
            video_count++;
        }
    }
    qsort(video_names, video_count, MAX_NAME, compare_names);

    if (video_count == 0) {
        printf("No video files found in %s\n", full_dir);
        return 0;
    }

    Stats stats = {0};

    for (int vi = 0; vi < video_count; vi++) {
        const char* video_name = video_names[vi];
        printf("* %s\n", video_name);

        char video_stem[MAX_NAME], video_ext[32];
        split_stem_ext(video_name, video_stem, sizeof(video_stem), video_ext, sizeof(video_ext));
        for (char* p = video_ext; *p; p++) *p = (char)tolower((unsigned char)*p);

        ParsedInfo info;
        memset(&info, 0, sizeof(info));

        if (!parse_release(video_stem, &info)) {
            printc(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
                   "  [SKIPPED] no title/quality info recognized in filename - rename manually\n");
            stats.videosUnparsed++;
            printf("\n");
            continue;
        }
        if (!info.hasYear) {
            printc(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
                   "  [NOTE] no year recognized, name built without (year) - check manually\n");
        }

        char new_stem[MAX_NAME];
        build_new_stem(&info, new_stem, sizeof(new_stem));

        char desired_video_name[MAX_NAME];
        snprintf(desired_video_name, sizeof(desired_video_name), "%s%s", new_stem, video_ext);

        char video_full_path[MAX_FULL];
        snprintf(video_full_path, sizeof(video_full_path), "%s\\%s", full_dir, video_name);

        char target[MAX_FULL];
        int is_dup;
        resolve_target(full_dir, desired_video_name, video_full_path, target, sizeof(target), &is_dup);
        move_or_report(video_full_path, video_name, target, is_dup, apply, "Video", &stats, 1);

        /* move along matching subtitle files */
        for (int fi = 0; fi < all_count; fi++) {
            if (all_files[fi].handled) continue;
            if (_stricmp(all_files[fi].name, video_name) == 0) continue;
            if (!is_subtitle_match(all_files[fi].name, video_stem)) continue;

            all_files[fi].handled = 1;
            const char* suffix = all_files[fi].name + strlen(video_stem); /* e.g. ".srt" or ".nl.srt" */

            char desired_sub_name[MAX_NAME];
            snprintf(desired_sub_name, sizeof(desired_sub_name), "%s%s", new_stem, suffix);

            char sub_full_path[MAX_FULL];
            snprintf(sub_full_path, sizeof(sub_full_path), "%s\\%s", full_dir, all_files[fi].name);

            char sub_target[MAX_FULL];
            int sub_is_dup;
            resolve_target(full_dir, desired_sub_name, sub_full_path, sub_target, sizeof(sub_target), &sub_is_dup);
            move_or_report(sub_full_path, all_files[fi].name, sub_target, sub_is_dup, apply, "  Sub  ", &stats, 0);
        }

        printf("\n");
    }

    printf("------------------------------------------------------------\n");
    printf("Videos %s:      %d\n", apply ? "renamed" : "would rename", stats.videosRenamed);
    printf("Subs %s:         %d\n", apply ? "renamed" : "would rename", stats.subsRenamed);
    printf("Video duplicates skipped: %d\n", stats.videosDupSkipped);
    printf("Sub duplicates skipped:   %d\n", stats.subsDupSkipped);
    printf("Unparseable (manual):     %d\n", stats.videosUnparsed);
    if (stats.errors > 0) {
        printc(FOREGROUND_RED | FOREGROUND_INTENSITY, "Errors:                   %d\n", stats.errors);
    }

    return stats.errors > 0 ? 1 : 0;
}
