#ifndef ENGINE_H
#define ENGINE_H

#include "supersearch.h"
#include "regex_engine.h"

// Single pattern definition
typedef struct {
    char pattern[512];
    char pattern_lower[512];
    char pattern_upper[512];
    int pattern_len;
    int skip_table[256];
    RegexCompiled* regex;
    bool is_regex;
} SearchPatternDesc;

// Search engine pattern context (supports single and multiple patterns)
typedef struct {
    // Direct single pattern fields for zero-overhead compatibility
    char pattern[512];
    char pattern_lower[512];
    char pattern_upper[512];
    int pattern_len;
    bool ignore_case;
    int skip_table[256];
    RegexCompiled* regex;
    bool is_regex;

    // Multi-pattern fields
    SearchPatternDesc* patterns;
    size_t pattern_count;
    int min_pattern_len;
} SearchEngineContext;

// Initialize search engine tables
void engine_init_context(SearchEngineContext* ctx, const SearchOptions* options);

// Free dynamically allocated search engine resources
void engine_free_context(SearchEngineContext* ctx);

// Scan a single file using the optimal I/O and SIMD search strategy
bool engine_search_file(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    FileEntry* entry,
    ThreadResult* result,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
);

// Fast binary detection
bool is_binary_buffer(const unsigned char* buffer, size_t length);

#endif // ENGINE_H
