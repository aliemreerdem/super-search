#ifndef SUPERSEARCH_H
#define SUPERSEARCH_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SS_PATH_SEP '\\'
#define SS_PATH_SEP_STR "\\"
#else
#include <wchar.h>
#include <strings.h>
#include <unistd.h>
#include <limits.h>
#ifndef MAX_PATH
#ifdef PATH_MAX
#define MAX_PATH PATH_MAX
#else
#define MAX_PATH 4096
#endif
#endif
typedef long LONG;
typedef unsigned long DWORD;
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define strtok_s strtok_r
#define InterlockedIncrement(ptr) (__atomic_add_fetch((ptr), 1, __ATOMIC_SEQ_CST))
#define InterlockedDecrement(ptr) (__atomic_sub_fetch((ptr), 1, __ATOMIC_SEQ_CST))
#define InterlockedExchangeAdd(ptr, val) (__atomic_fetch_add((ptr), (val), __ATOMIC_SEQ_CST))
#define Sleep(ms) usleep((useconds_t)(ms) * 1000)
#define SS_PATH_SEP '/'
#define SS_PATH_SEP_STR "/"
#endif

#define APP_NAME "SuperSearch"
#define APP_VERSION "2.6.0"

// File categories based on size for optimal I/O strategy
typedef enum {
    CAT_TINY = 0,    // < 4 KB: Buffered stack/heap read
    CAT_SMALL,       // 4 KB - 64 KB: Buffered direct read
    CAT_MEDIUM,      // 64 KB - 16 MB: Standard Memory-Mapped I/O
    CAT_LARGE,       // 16 MB - 128 MB: Chunked/Windowed Memory-Mapped I/O
    CAT_HUGE,        // > 128 MB: Streaming windowed scan
    CAT_COUNT
} FileCategory;

// CLI search configuration options
typedef struct {
    char target_dir[MAX_PATH];
    wchar_t target_dir_w[MAX_PATH];
    char query[512];
    int query_len;
    char** patterns;           // List of multiple patterns from -e and -f
    size_t pattern_count;      // Total number of patterns
    char pattern_file[MAX_PATH]; // Path to file containing patterns (-f, --file)
    bool ignore_case;
    int threads;               // 0 = auto-detect
    bool search_binary;
    uint64_t max_file_size;    // 0 = unlimited
    char extensions[256];      // Comma-separated list (e.g., ".c,.h,.txt")
    char excludes[512];        // Comma-separated list of directories to skip
    char include_globs[512];   // Comma-separated positive filename globs from -g / --glob
    char exclude_globs[512];   // Comma-separated negative filename globs from -g !pattern
    char output_file[MAX_PATH];
    char json_file[MAX_PATH];
    bool no_color;
    bool verbose;
    bool invert_match;         // -v, --invert-match: select non-matching lines
    bool no_ignore;            // --no-ignore: do not respect .gitignore or .superignore
    bool hidden;               // --hidden: search hidden files and directories
    bool multiline;            // -U, --multiline: match across newline boundaries
    bool is_regex;             // -R, --regex: enable regular expression engine
    bool fixed_strings;        // -F, --fixed-strings: treat patterns as literal strings
    bool search_zip;           // -z, --search-zip: search compressed archives (.zip, .gz, .tar.gz)
    int max_matches_per_file;  // 0 = unlimited

    // ripgrep (rg) & Coding Agent CLI Compatibility Options
    bool files_with_matches;   // -l, --files-with-matches: print only matching file paths
    bool list_files_only;      // --files: print all candidate file paths without searching content
    bool count_only;           // -c, --count: print match count per file
    bool word_regexp;          // -w, --word-regexp: match whole words only (\b...\b)
    bool smart_case;           // -S, --smart-case: case-insensitive unless query has uppercase

    // Search & Replace Options (Phase 2)
    char replace_text[512];    // Replacement text from --replace
    int replace_len;           // Length of replacement text
    bool do_replace;           // Set to true if --replace is passed
    bool in_place;             // --in-place: apply changes to disk
    bool backup;               // --backup: create .bak copies before writing
    bool diff_output;          // --diff: emit unified diff format

    // AI Agent & Token Economy Options
    size_t max_results;        // 0 = unlimited, hard cap on total matches to prevent context overflow
    size_t max_tokens;         // --max-tokens <N>: approximate token budget cap (1 token ~ 4 bytes)
    int max_line_len;          // 0 = default (250 chars), max length of snippet line
    int context_before;        // -B N: lines of code before match
    int context_after;         // -A N: lines of code after match
    bool enclosing_function;   // -W, --enclosing-function: attach enclosing function/symbol signature
    bool outline_mode;         // --outline / --symbols: extract all function/class definitions in target
    bool plan_only;            // --plan-only / --dry-run: output plan & stats only
    bool json_stdout;          // --json-stdout: output pure JSON/NDJSON to stdout
    bool porcelain;            // --porcelain: machine-readable clean mode
    bool show_schema;          // --mcp-schema / --schema: output MCP tool definition
    bool interactive;          // -I, --interactive: full-screen live interactive TUI mode

    // Semantic & AST Code Search (Phase 4.3)
    int kind_filter;           // --kind <comment|string|code|function> (SemanticKind)

    // Instant Cache & Directory Watcher (Phase 4.4)
    bool use_cache;            // --cache: use persistent instant disk cache
    bool build_cache;          // --cache-build: force rebuild cache
    bool clear_cache;          // --cache-clear: delete cache for target dir
    bool watch_mode;           // --watch: run directory watcher daemon
} SearchOptions;

// Individual file metadata
typedef struct {
    char* path_utf8;
    wchar_t* path_utf16;
    uint64_t file_size;
    FileCategory category;
    bool name_matched;
    bool is_binary;
    int match_count;
} FileEntry;

// Dynamic file list catalogued during inspection phase
typedef struct {
    FileEntry* items;
    size_t count;
    size_t capacity;
    uint64_t total_bytes;
    size_t total_dirs;
    size_t cat_counts[CAT_COUNT];
    uint64_t cat_bytes[CAT_COUNT];
    size_t filename_matches;
} FileList;

// Generated execution plan before parallel search
typedef struct {
    int worker_threads;
    int logical_cores;
    size_t dynamic_chunk_size;
    uint64_t estimated_memory_mb;
    const char* io_strategy;
    const char* simd_acceleration;
    const char* sort_strategy;
    double inspection_time_ms;
    bool has_avx2;
} ExecutionPlan;

// Search match record with surrounding context
typedef struct {
    char* file_path;
    int64_t line_number;
    int64_t col_number;
    char* line_text;
    int match_len;
    bool is_filename_match;

    // Surrounding context lines for AI Agents
    char** lines_before;
    int lines_before_count;
    char** lines_after;
    int lines_after_count;
    char* replaced_text;       // Replaced line/snippet if do_replace is true
    char* enclosing_func;      // Enclosing function/method/class signature if enclosing_function is true
    int64_t enclosing_line;    // Line number of enclosing function/symbol signature
} MatchRecord;

// Thread-local search metrics and results
typedef struct {
    MatchRecord* matches;
    size_t match_count;
    size_t match_capacity;
    size_t files_scanned;
    uint64_t bytes_scanned;
    size_t binary_skipped;
    size_t error_count;
    size_t replacement_count;  // Replacements performed/previewed in this thread
    size_t files_modified;     // Number of files modified on disk
    size_t backups_created;    // Number of .bak backup files created in this thread
    double active_time_ms;
} ThreadResult;

// Overall search report
typedef struct {
    SearchOptions options;
    ExecutionPlan plan;
    double inspection_time_ms;
    double search_time_ms;
    double report_gen_time_ms;
    double total_time_ms;
    size_t total_files_discovered;
    size_t total_dirs_discovered;
    uint64_t total_bytes_discovered;
    size_t files_scanned;
    uint64_t bytes_scanned;
    size_t binary_skipped;
    size_t error_count;
    size_t total_matches;
    size_t filename_matches;
    size_t content_matches;
    size_t files_with_matches;
    size_t total_replacements; // Total replacements performed or previewed
    size_t files_modified;     // Total files modified on disk
    size_t backups_created;    // Number of .bak backup files created
    double throughput_mb_s;
    double files_per_sec;
    bool truncated;            // true if total matches exceeded max_results
    bool token_limit_reached;  // true if output was capped by max_tokens
    size_t omitted_matches;    // Number of matches omitted due to max_tokens

    // Aggregated matches
    MatchRecord* all_matches;
    size_t all_matches_count;
    size_t all_matches_capacity;
} SearchReport;

#endif // SUPERSEARCH_H
