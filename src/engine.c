#include "engine.h"
#include "replace.h"
#include "archive.h"
#include "utils.h"
#include "semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define SMALL_FILE_THRESHOLD (64 * 1024)
#define CHUNK_SIZE_LARGE (32 * 1024 * 1024)

void engine_init_context(SearchEngineContext* ctx, const SearchOptions* options) {
    memset(ctx, 0, sizeof(SearchEngineContext));
    ctx->ignore_case = options->ignore_case;
    ctx->pattern_count = options->pattern_count;

    if (ctx->pattern_count == 0) {
        const char* query = options->query;
        ctx->pattern_len = (int)strlen(query);
        if (ctx->pattern_len >= (int)sizeof(ctx->pattern)) {
            ctx->pattern_len = sizeof(ctx->pattern) - 1;
        }
        strncpy(ctx->pattern, query, ctx->pattern_len);
        ctx->pattern[ctx->pattern_len] = '\0';

        for (int i = 0; i < ctx->pattern_len; i++) {
            ctx->pattern_lower[i] = (char)tolower((unsigned char)ctx->pattern[i]);
            ctx->pattern_upper[i] = (char)toupper((unsigned char)ctx->pattern[i]);
        }
        ctx->pattern_lower[ctx->pattern_len] = '\0';
        ctx->pattern_upper[ctx->pattern_len] = '\0';

        int m = ctx->pattern_len;
        for (int i = 0; i < 256; i++) ctx->skip_table[i] = m;
        for (int i = 0; i < m - 1; i++) {
            if (ctx->ignore_case) {
                ctx->skip_table[(unsigned char)ctx->pattern_lower[i]] = m - 1 - i;
                ctx->skip_table[(unsigned char)ctx->pattern_upper[i]] = m - 1 - i;
            } else {
                ctx->skip_table[(unsigned char)ctx->pattern[i]] = m - 1 - i;
            }
        }
        ctx->min_pattern_len = ctx->pattern_len;

        bool should_regex = !options->fixed_strings && (options->is_regex || regex_has_metacharacters(ctx->pattern));
        if (should_regex) {
            char err[256] = {0};
            ctx->regex = regex_compile(ctx->pattern, ctx->ignore_case, err, sizeof(err));
            if (ctx->regex) {
                ctx->is_regex = true;
            }
        }
        return;
    }

    // Initialize first pattern directly into ctx for single-pattern compatibility
    const char* first_query = options->patterns[0];
    ctx->pattern_len = (int)strlen(first_query);
    if (ctx->pattern_len >= (int)sizeof(ctx->pattern)) {
        ctx->pattern_len = sizeof(ctx->pattern) - 1;
    }
    strncpy(ctx->pattern, first_query, ctx->pattern_len);
    ctx->pattern[ctx->pattern_len] = '\0';

    for (int i = 0; i < ctx->pattern_len; i++) {
        ctx->pattern_lower[i] = (char)tolower((unsigned char)ctx->pattern[i]);
        ctx->pattern_upper[i] = (char)toupper((unsigned char)ctx->pattern[i]);
    }
    ctx->pattern_lower[ctx->pattern_len] = '\0';
    ctx->pattern_upper[ctx->pattern_len] = '\0';

    int m = ctx->pattern_len;
    for (int i = 0; i < 256; i++) ctx->skip_table[i] = m;
    for (int i = 0; i < m - 1; i++) {
        if (ctx->ignore_case) {
            ctx->skip_table[(unsigned char)ctx->pattern_lower[i]] = m - 1 - i;
            ctx->skip_table[(unsigned char)ctx->pattern_upper[i]] = m - 1 - i;
        } else {
            ctx->skip_table[(unsigned char)ctx->pattern[i]] = m - 1 - i;
        }
    }
    ctx->min_pattern_len = ctx->pattern_len;

    bool first_should_regex = !options->fixed_strings && (options->is_regex || regex_has_metacharacters(ctx->pattern));
    if (first_should_regex) {
        char err[256] = {0};
        ctx->regex = regex_compile(ctx->pattern, ctx->ignore_case, err, sizeof(err));
        if (ctx->regex) {
            ctx->is_regex = true;
        }
    }

    // Allocate and populate all patterns for multi-pattern search
    ctx->patterns = (SearchPatternDesc*)safe_malloc(ctx->pattern_count * sizeof(SearchPatternDesc));
    for (size_t p = 0; p < ctx->pattern_count; p++) {
        const char* pat_str = options->patterns[p];
        SearchPatternDesc* desc = &ctx->patterns[p];
        memset(desc, 0, sizeof(SearchPatternDesc));
        desc->pattern_len = (int)strlen(pat_str);
        if (desc->pattern_len >= (int)sizeof(desc->pattern)) {
            desc->pattern_len = sizeof(desc->pattern) - 1;
        }
        strncpy(desc->pattern, pat_str, desc->pattern_len);
        desc->pattern[desc->pattern_len] = '\0';

        for (int i = 0; i < desc->pattern_len; i++) {
            desc->pattern_lower[i] = (char)tolower((unsigned char)desc->pattern[i]);
            desc->pattern_upper[i] = (char)toupper((unsigned char)desc->pattern[i]);
        }
        desc->pattern_lower[desc->pattern_len] = '\0';
        desc->pattern_upper[desc->pattern_len] = '\0';

        if (desc->pattern_len < ctx->min_pattern_len) {
            ctx->min_pattern_len = desc->pattern_len;
        }

        int pm = desc->pattern_len;
        for (int i = 0; i < 256; i++) desc->skip_table[i] = pm;
        for (int i = 0; i < pm - 1; i++) {
            if (ctx->ignore_case) {
                desc->skip_table[(unsigned char)desc->pattern_lower[i]] = pm - 1 - i;
                desc->skip_table[(unsigned char)desc->pattern_upper[i]] = pm - 1 - i;
            } else {
                desc->skip_table[(unsigned char)desc->pattern[i]] = pm - 1 - i;
            }
        }

        bool pat_should_regex = !options->fixed_strings && (options->is_regex || regex_has_metacharacters(desc->pattern));
        if (pat_should_regex) {
            char err[256] = {0};
            desc->regex = regex_compile(desc->pattern, ctx->ignore_case, err, sizeof(err));
            if (desc->regex) {
                desc->is_regex = true;
                ctx->is_regex = true;
            }
        }
    }
}

void engine_free_context(SearchEngineContext* ctx) {
    if (!ctx) return;
    if (ctx->regex) {
        regex_free(ctx->regex);
        ctx->regex = NULL;
    }
    if (ctx->patterns) {
        for (size_t p = 0; p < ctx->pattern_count; p++) {
            if (ctx->patterns[p].regex) {
                regex_free(ctx->patterns[p].regex);
                ctx->patterns[p].regex = NULL;
            }
        }
        free(ctx->patterns);
        ctx->patterns = NULL;
    }
}

bool is_binary_buffer(const unsigned char* buffer, size_t length) {
    size_t sample_size = (length < 1024) ? length : 1024;
    for (size_t i = 0; i < sample_size; i++) {
        if (buffer[i] == 0) return true;
    }
    return false;
}

typedef struct {
    const char* data_ptr;
    size_t data_len;
    size_t last_offset;
    int64_t last_line_no;
    size_t last_line_start;
    int64_t base_line_offset;
} ThreadLineCache;

#if defined(_MSC_VER)
static __declspec(thread) ThreadLineCache g_tls_line_cache = { NULL, 0, 0, 1, 0, 0 };
#else
static __thread ThreadLineCache g_tls_line_cache = { NULL, 0, 0, 1, 0, 0 };
#endif

// Add a match to thread-local results
static bool add_thread_match(
    ThreadResult* result,
    const SearchOptions* options,
    const char* file_path,
    int64_t line_no,
    int64_t col_no,
    const char* line_text,
    int match_len,
    char** lines_before,
    int count_before,
    char** lines_after,
    int count_after
) {
    if (options && options->kind_filter != 0) {
        if (!semantic_match_satisfies_kind((SemanticKind)options->kind_filter, file_path, line_text, col_no, match_len)) {
            // Free allocated context lines since match is rejected by semantic filter
            if (lines_before) {
                for (int b = 0; b < count_before; b++) free(lines_before[b]);
                free(lines_before);
            }
            if (lines_after) {
                for (int a = 0; a < count_after; a++) free(lines_after[a]);
                free(lines_after);
            }
            return false;
        }
    }

    if (result->match_count >= result->match_capacity) {
        size_t new_cap = (result->match_capacity == 0) ? 64 : result->match_capacity * 2;
        result->matches = (MatchRecord*)safe_realloc(result->matches, new_cap * sizeof(MatchRecord));
        result->match_capacity = new_cap;
    }

    MatchRecord* match = &result->matches[result->match_count++];
    match->file_path = safe_strdup(file_path);
    match->line_number = line_no;
    match->col_number = col_no;
    match->line_text = safe_strdup(line_text);
    match->match_len = match_len;
    match->is_filename_match = false;
    match->lines_before = lines_before;
    match->lines_before_count = count_before;
    match->lines_after = lines_after;
    match->lines_after_count = count_after;
    match->enclosing_func = NULL;
    match->enclosing_line = 0;

    if (options && options->enclosing_function && g_tls_line_cache.data_ptr != NULL) {
        int64_t enc_line = 0;
        match->enclosing_func = semantic_find_enclosing_function(
            g_tls_line_cache.data_ptr,
            g_tls_line_cache.data_len,
            g_tls_line_cache.last_offset,
            line_no - g_tls_line_cache.base_line_offset,
            file_path,
            &enc_line
        );
        if (match->enclosing_func) {
            match->enclosing_line = enc_line + g_tls_line_cache.base_line_offset;
        }
    }

    if (options && options->do_replace) {
        match->replaced_text = replace_generate_snippet(line_text, options, NULL, 0, options->replace_text);
    } else {
        match->replaced_text = NULL;
    }
    return true;
}

static inline void handle_match_record_result(
    bool added,
    const SearchOptions* options,
    volatile LONG* global_match_count,
    int* file_match_count,
    int max_per_file,
    bool* out_stop_file
) {
    if (added) {
        if (file_match_count) {
            (*file_match_count)++;
            if (max_per_file > 0 && *file_match_count >= max_per_file) {
                if (out_stop_file) *out_stop_file = true;
            }
        }
    } else {
        if (options && options->max_results > 0 && global_match_count) {
            InterlockedDecrement(global_match_count);
        }
    }
}

// Flexible CRLF/LF pattern matcher across lines
static bool match_text_flexible_newline(
    const char* text,
    size_t text_rem,
    const char* pat,
    size_t pat_len,
    bool ignore_case,
    size_t* out_matched_bytes
) {
    size_t t = 0;
    size_t p = 0;

    while (p < pat_len && t < text_rem) {
        if (pat[p] == '\n') {
            if (text[t] == '\r' && t + 1 < text_rem && text[t + 1] == '\n') {
                t += 2;
                p++;
                if (p < pat_len && pat[p] == '\r') p++;
            } else if (text[t] == '\n') {
                t++;
                p++;
            } else {
                return false;
            }
        } else if (pat[p] == '\r' && p + 1 < pat_len && pat[p + 1] == '\n') {
            if (text[t] == '\r' && t + 1 < text_rem && text[t + 1] == '\n') {
                t += 2;
                p += 2;
            } else if (text[t] == '\n') {
                t++;
                p += 2;
            } else {
                return false;
            }
        } else {
            unsigned char c1 = (unsigned char)text[t];
            unsigned char c2 = (unsigned char)pat[p];
            if (ignore_case) {
                if (tolower(c1) != tolower(c2)) return false;
            } else {
                if (c1 != c2) return false;
            }
            t++;
            p++;
        }
    }

    if (p == pat_len) {
        if (out_matched_bytes) *out_matched_bytes = t;
        return true;
    }
    return false;
}

// Helper to calculate line number, column number, snippet, and surrounding context lines
static void extract_match_context(
    const SearchOptions* options,
    const char* data,
    size_t data_len,
    size_t match_offset,
    int match_len,
    int64_t* out_line_no,
    int64_t* out_col_no,
    char** out_snippet,
    char*** out_lines_before,
    int* out_count_before,
    char*** out_lines_after,
    int* out_count_after
) {
    // 1. Incremental O(N) line number counting up to match_offset
    int64_t line_no = 1 + g_tls_line_cache.base_line_offset;
    size_t line_start = 0;
    size_t scan_start = 0;

    if (g_tls_line_cache.data_ptr == data &&
        g_tls_line_cache.data_len == data_len &&
        match_offset >= g_tls_line_cache.last_offset) {
        scan_start = g_tls_line_cache.last_offset;
        line_no = g_tls_line_cache.last_line_no;
        line_start = g_tls_line_cache.last_line_start;
    }

    for (size_t i = scan_start; i < match_offset; i++) {
        if (data[i] == '\n') {
            line_no++;
            line_start = i + 1;
        }
    }

    g_tls_line_cache.data_ptr = data;
    g_tls_line_cache.data_len = data_len;
    g_tls_line_cache.last_offset = match_offset;
    g_tls_line_cache.last_line_no = line_no;
    g_tls_line_cache.last_line_start = line_start;

    *out_line_no = line_no;
    *out_col_no = (int64_t)(match_offset - line_start) + 1;

    // 2. Find end of line
    size_t line_end = match_offset + match_len;
    while (line_end < data_len && data[line_end] != '\r' && data[line_end] != '\n') {
        line_end++;
    }

    // Limit snippet length using max_line_len (default 250)
    size_t max_len = (options->max_line_len > 0) ? (size_t)options->max_line_len : 250;
    if (options->multiline && (size_t)match_len + 120 > max_len) {
        max_len = (size_t)match_len + 120;
    }
    size_t raw_line_len = line_end - line_start;
    size_t snippet_start = line_start;
    size_t snippet_len = raw_line_len;

    if (raw_line_len > max_len) {
        if (!options->multiline && match_offset > line_start + 40) {
            snippet_start = match_offset - 40;
        }
        if (snippet_start + max_len < line_end) {
            snippet_len = max_len;
        } else {
            snippet_len = line_end - snippet_start;
        }
    }

    int clean_len = 0;
    *out_snippet = sanitize_snippet_ex(&data[snippet_start], snippet_len, options->multiline, &clean_len);

    // 3. Extract surrounding lines before (context_before)
    *out_lines_before = NULL;
    *out_count_before = 0;
    int num_before = options->context_before;

    if (num_before > 0 && line_start > 0) {
        size_t cur = line_start - 1;
        size_t starts[64], ends[64];
        int found = 0;

        while (cur > 0 && found < num_before && found < 64) {
            while (cur > 0 && (data[cur] == '\r' || data[cur] == '\n')) cur--;
            if (cur == 0 && (data[0] == '\r' || data[0] == '\n')) break;
            size_t lend = cur + 1;
            while (cur > 0 && data[cur - 1] != '\n') cur--;
            size_t lstart = cur;
            starts[found] = lstart;
            ends[found] = lend;
            found++;
            if (cur == 0) break;
            cur--;
        }

        if (found > 0) {
            char** b_lines = (char**)safe_malloc(found * sizeof(char*));
            for (int k = 0; k < found; k++) {
                int idx = found - 1 - k; // Chronological order
                size_t l_len = ends[idx] - starts[idx];
                if (l_len > max_len) l_len = max_len;
                int cl = 0;
                b_lines[k] = sanitize_snippet(&data[starts[idx]], l_len, &cl);
            }
            *out_lines_before = b_lines;
            *out_count_before = found;
        }
    }

    // 4. Extract surrounding lines after (context_after)
    *out_lines_after = NULL;
    *out_count_after = 0;
    int num_after = options->context_after;

    if (num_after > 0 && line_end < data_len) {
        size_t cur = line_end;
        char** a_lines = (char**)safe_malloc(num_after * sizeof(char*));
        int found = 0;

        while (cur < data_len && found < num_after) {
            while (cur < data_len && (data[cur] == '\r' || data[cur] == '\n')) cur++;
            if (cur >= data_len) break;
            size_t lstart = cur;
            while (cur < data_len && data[cur] != '\r' && data[cur] != '\n') cur++;
            size_t lend = cur;
            size_t l_len = lend - lstart;
            if (l_len > max_len) l_len = max_len;
            int cl = 0;
            a_lines[found++] = sanitize_snippet(&data[lstart], l_len, &cl);
        }

        if (found > 0) {
            *out_lines_after = a_lines;
            *out_count_after = found;
        } else {
            free(a_lines);
        }
    }
}

// Boyer-Moore-Horspool search on arbitrary byte buffer with start offset
static void search_buffer_horspool(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* data,
    size_t data_len,
    size_t start_offset,
    const char* file_path,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    int m = ctx->pattern_len;
    if (data_len < (size_t)m || start_offset > data_len - m) return;

    size_t i = start_offset;
    size_t max_i = data_len - m;
    int max_per_file = options->max_matches_per_file;

    if (ctx->ignore_case) {
        while (i <= max_i) {
            if (stop_flag && *stop_flag) return;

            int j = m - 1;
            while (j >= 0 && tolower((unsigned char)data[i + j]) == (unsigned char)ctx->pattern_lower[j]) {
                j--;
            }

            if (j < 0) {
                // Match found! Check global limit
                if (options->max_results > 0 && global_match_count) {
                    LONG cur = InterlockedIncrement(global_match_count);
                    if (cur > (LONG)options->max_results) {
                        if (stop_flag) *stop_flag = 1;
                        return;
                    }
                }

                int64_t line_no = 0, col_no = 0;
                char* snippet = NULL;
                char** b_lines = NULL; int b_count = 0;
                char** a_lines = NULL; int a_count = 0;

                extract_match_context(options, data, data_len, i, m, &line_no, &col_no, &snippet,
                                      &b_lines, &b_count, &a_lines, &a_count);

                bool added = add_thread_match(result, options, file_path, line_no, col_no, snippet, m,
                                              b_lines, b_count, a_lines, a_count);
                free(snippet);
                bool stop_file = false;
                handle_match_record_result(added, options, global_match_count, file_match_count, max_per_file, &stop_file);
                if (stop_file) return;
                i += m; // advance past match
            } else {
                unsigned char c = (unsigned char)data[i + m - 1];
                i += ctx->skip_table[c];
            }
        }
    } else {
        while (i <= max_i) {
            if (stop_flag && *stop_flag) return;

            int j = m - 1;
            while (j >= 0 && data[i + j] == ctx->pattern[j]) {
                j--;
            }

            if (j < 0) {
                // Match found! Check global limit
                if (options->max_results > 0 && global_match_count) {
                    LONG cur = InterlockedIncrement(global_match_count);
                    if (cur > (LONG)options->max_results) {
                        if (stop_flag) *stop_flag = 1;
                        return;
                    }
                }

                int64_t line_no = 0, col_no = 0;
                char* snippet = NULL;
                char** b_lines = NULL; int b_count = 0;
                char** a_lines = NULL; int a_count = 0;

                extract_match_context(options, data, data_len, i, m, &line_no, &col_no, &snippet,
                                      &b_lines, &b_count, &a_lines, &a_count);

                bool added = add_thread_match(result, options, file_path, line_no, col_no, snippet, m,
                                              b_lines, b_count, a_lines, a_count);
                free(snippet);
                bool stop_file = false;
                handle_match_record_result(added, options, global_match_count, file_match_count, max_per_file, &stop_file);
                if (stop_file) return;
                i += m;
            } else {
                unsigned char c = (unsigned char)data[i + m - 1];
                i += ctx->skip_table[c];
            }
        }
    }
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
// AVX2 accelerated search function (compiled with avx2 target attribute for runtime dispatch)
#if defined(__clang__) || defined(__GNUC__)
__attribute__((target("avx2")))
#endif
static void search_buffer_avx2(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* data,
    size_t data_len,
    const char* file_path,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    int m = ctx->pattern_len;
    if (data_len < 32 || data_len < (size_t)m + 32) {
        search_buffer_horspool(ctx, options, data, data_len, 0, file_path, result, file_match_count, global_match_count, stop_flag);
        return;
    }

    size_t i = 0;
    size_t simd_limit = data_len - m - 32;
    int max_per_file = options->max_matches_per_file;

    if (!ctx->ignore_case) {
        __m256i first_char = _mm256_set1_epi8(ctx->pattern[0]);
        __m256i last_char  = _mm256_set1_epi8(ctx->pattern[m - 1]);

        while (i <= simd_limit) {
            if (stop_flag && *stop_flag) return;

            __m256i block_first = _mm256_loadu_si256((const __m256i*)&data[i]);
            __m256i block_last  = _mm256_loadu_si256((const __m256i*)&data[i + m - 1]);

            __m256i eq_first = _mm256_cmpeq_epi8(block_first, first_char);
            __m256i eq_last  = _mm256_cmpeq_epi8(block_last, last_char);

            uint32_t mask = (uint32_t)_mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));

            while (mask != 0) {
                int bit = __builtin_ctz(mask);
                size_t candidate = i + bit;

                bool matches = true;
                if (m > 2) {
                    matches = (memcmp(&data[candidate + 1], &ctx->pattern[1], m - 2) == 0);
                }

                if (matches) {
                    if (options->max_results > 0 && global_match_count) {
                        LONG cur = InterlockedIncrement(global_match_count);
                        if (cur > (LONG)options->max_results) {
                            if (stop_flag) *stop_flag = 1;
                            return;
                        }
                    }

                    int64_t line_no = 0, col_no = 0;
                    char* snippet = NULL;
                    char** b_lines = NULL; int b_count = 0;
                    char** a_lines = NULL; int a_count = 0;

                    extract_match_context(options, data, data_len, candidate, m, &line_no, &col_no, &snippet,
                                          &b_lines, &b_count, &a_lines, &a_count);

                    bool added = add_thread_match(result, options, file_path, line_no, col_no, snippet, m,
                                                  b_lines, b_count, a_lines, a_count);
                    free(snippet);
                    bool stop_file = false;
                    handle_match_record_result(added, options, global_match_count, file_match_count, max_per_file, &stop_file);
                    if (stop_file) return;
                }
                mask &= mask - 1;
            }
            i += 32;
        }
    } else {
        __m256i first_l = _mm256_set1_epi8(ctx->pattern_lower[0]);
        __m256i first_u = _mm256_set1_epi8(ctx->pattern_upper[0]);
        __m256i last_l  = _mm256_set1_epi8(ctx->pattern_lower[m - 1]);
        __m256i last_u  = _mm256_set1_epi8(ctx->pattern_upper[m - 1]);

        while (i <= simd_limit) {
            if (stop_flag && *stop_flag) return;

            __m256i block_first = _mm256_loadu_si256((const __m256i*)&data[i]);
            __m256i block_last  = _mm256_loadu_si256((const __m256i*)&data[i + m - 1]);

            __m256i eq_first = _mm256_or_si256(
                _mm256_cmpeq_epi8(block_first, first_l),
                _mm256_cmpeq_epi8(block_first, first_u)
            );
            __m256i eq_last = _mm256_or_si256(
                _mm256_cmpeq_epi8(block_last, last_l),
                _mm256_cmpeq_epi8(block_last, last_u)
            );

            uint32_t mask = (uint32_t)_mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));

            while (mask != 0) {
                int bit = __builtin_ctz(mask);
                size_t candidate = i + bit;

                bool matches = true;
                for (int k = 1; k < m - 1; k++) {
                    if (tolower((unsigned char)data[candidate + k]) != (unsigned char)ctx->pattern_lower[k]) {
                        matches = false;
                        break;
                    }
                }

                if (matches) {
                    if (options->max_results > 0 && global_match_count) {
                        LONG cur = InterlockedIncrement(global_match_count);
                        if (cur > (LONG)options->max_results) {
                            if (stop_flag) *stop_flag = 1;
                            return;
                        }
                    }

                    int64_t line_no = 0, col_no = 0;
                    char* snippet = NULL;
                    char** b_lines = NULL; int b_count = 0;
                    char** a_lines = NULL; int a_count = 0;

                    extract_match_context(options, data, data_len, candidate, m, &line_no, &col_no, &snippet,
                                          &b_lines, &b_count, &a_lines, &a_count);

                    bool added = add_thread_match(result, options, file_path, line_no, col_no, snippet, m,
                                                  b_lines, b_count, a_lines, a_count);
                    free(snippet);
                    bool stop_file = false;
                    handle_match_record_result(added, options, global_match_count, file_match_count, max_per_file, &stop_file);
                    if (stop_file) return;
                }
                mask &= mask - 1;
            }
            i += 32;
        }
    }

    // Process remaining tail bytes with Horspool
    if (i < data_len) {
        search_buffer_horspool(ctx, options, data, data_len, i, file_path, result, file_match_count, global_match_count, stop_flag);
    }
}
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
// ARM NEON 128-bit Dual-Pivot SIMD search function for Apple Silicon (M1/M2/M3/M4) and Linux ARM64
static void search_buffer_neon(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* data,
    size_t data_len,
    const char* file_path,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    int m = ctx->pattern_len;
    if (data_len < 16 || data_len < (size_t)m + 16) {
        search_buffer_horspool(ctx, options, data, data_len, 0, file_path, result, file_match_count, global_match_count, stop_flag);
        return;
    }

    size_t i = 0;
    size_t simd_limit = data_len - m - 16;
    int max_per_file = options->max_matches_per_file;

    if (!ctx->ignore_case) {
        uint8x16_t first_char = vdupq_n_u8((uint8_t)ctx->pattern[0]);
        uint8x16_t last_char  = vdupq_n_u8((uint8_t)ctx->pattern[m - 1]);

        while (i <= simd_limit) {
            if (stop_flag && *stop_flag) return;

            uint8x16_t block_first = vld1q_u8((const uint8_t*)&data[i]);
            uint8x16_t block_last  = vld1q_u8((const uint8_t*)&data[i + m - 1]);

            uint8x16_t hit_vec = vandq_u8(vceqq_u8(block_first, first_char), vceqq_u8(block_last, last_char));

            if (vmaxvq_u8(hit_vec) != 0) {
                uint8_t lanes[16];
                vst1q_u8(lanes, hit_vec);
                for (int bit = 0; bit < 16; bit++) {
                    if (!lanes[bit]) continue;
                    size_t candidate = i + (size_t)bit;

                    bool matches = true;
                    if (m > 2) {
                        matches = (memcmp(&data[candidate + 1], &ctx->pattern[1], m - 2) == 0);
                    }

                    if (matches) {
                        if (options->max_results > 0 && global_match_count) {
                            LONG cur = InterlockedIncrement(global_match_count);
                            if (cur > (LONG)options->max_results) {
                                if (stop_flag) *stop_flag = 1;
                                return;
                            }
                        }

                        int64_t line_no = 0, col_no = 0;
                        char* snippet = NULL;
                        char** b_lines = NULL; int b_count = 0;
                        char** a_lines = NULL; int a_count = 0;

                        extract_match_context(options, data, data_len, candidate, m, &line_no, &col_no, &snippet,
                                              &b_lines, &b_count, &a_lines, &a_count);

                        bool added = add_thread_match(result, options, file_path, line_no, col_no, snippet, m,
                                                      b_lines, b_count, a_lines, a_count);
                        free(snippet);
                        bool stop_file = false;
                        handle_match_record_result(added, options, global_match_count, file_match_count, max_per_file, &stop_file);
                        if (stop_file) return;
                    }
                }
            }
            i += 16;
        }
    } else {
        uint8x16_t first_l = vdupq_n_u8((uint8_t)ctx->pattern_lower[0]);
        uint8x16_t first_u = vdupq_n_u8((uint8_t)ctx->pattern_upper[0]);
        uint8x16_t last_l  = vdupq_n_u8((uint8_t)ctx->pattern_lower[m - 1]);
        uint8x16_t last_u  = vdupq_n_u8((uint8_t)ctx->pattern_upper[m - 1]);

        while (i <= simd_limit) {
            if (stop_flag && *stop_flag) return;

            uint8x16_t block_first = vld1q_u8((const uint8_t*)&data[i]);
            uint8x16_t block_last  = vld1q_u8((const uint8_t*)&data[i + m - 1]);

            uint8x16_t eq_first = vorrq_u8(vceqq_u8(block_first, first_l), vceqq_u8(block_first, first_u));
            uint8x16_t eq_last  = vorrq_u8(vceqq_u8(block_last, last_l), vceqq_u8(block_last, last_u));
            uint8x16_t hit_vec  = vandq_u8(eq_first, eq_last);

            if (vmaxvq_u8(hit_vec) != 0) {
                uint8_t lanes[16];
                vst1q_u8(lanes, hit_vec);
                for (int bit = 0; bit < 16; bit++) {
                    if (!lanes[bit]) continue;
                    size_t candidate = i + (size_t)bit;

                    bool matches = true;
                    for (int k = 1; k < m - 1; k++) {
                        if (tolower((unsigned char)data[candidate + k]) != (unsigned char)ctx->pattern_lower[k]) {
                            matches = false;
                            break;
                        }
                    }

                    if (matches) {
                        if (options->max_results > 0 && global_match_count) {
                            LONG cur = InterlockedIncrement(global_match_count);
                            if (cur > (LONG)options->max_results) {
                                if (stop_flag) *stop_flag = 1;
                                return;
                            }
                        }

                        int64_t line_no = 0, col_no = 0;
                        char* snippet = NULL;
                        char** b_lines = NULL; int b_count = 0;
                        char** a_lines = NULL; int a_count = 0;

                        extract_match_context(options, data, data_len, candidate, m, &line_no, &col_no, &snippet,
                                              &b_lines, &b_count, &a_lines, &a_count);

                        bool added = add_thread_match(result, options, file_path, line_no, col_no, snippet, m,
                                                      b_lines, b_count, a_lines, a_count);
                        free(snippet);
                        bool stop_file = false;
                        handle_match_record_result(added, options, global_match_count, file_match_count, max_per_file, &stop_file);
                        if (stop_file) return;
                    }
                }
            }
            i += 16;
        }
    }

    if (i < data_len) {
        search_buffer_horspool(ctx, options, data, data_len, i, file_path, result, file_match_count, global_match_count, stop_flag);
    }
}
#endif

// Inverted search function: matches lines that DO NOT contain the pattern
static void search_buffer_invert(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* data,
    size_t data_len,
    const char* file_path,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    size_t line_start = 0;
    int64_t line_no = 1 + g_tls_line_cache.base_line_offset;
    int max_per_file = options->max_matches_per_file;
    int pat_len = ctx->pattern_len;

    for (size_t i = 0; i <= data_len; i++) {
        if (stop_flag && *stop_flag) return;

        if (i == data_len || data[i] == '\n') {
            size_t line_end = (i > line_start && data[i - 1] == '\r') ? i - 1 : i;
            size_t line_len = line_end - line_start;

            if (line_len > 0) {
                bool found = false;
                if (line_len >= (size_t)pat_len) {
                    if (ctx->ignore_case) {
                        for (size_t k = 0; k <= line_len - pat_len; k++) {
                            int j = 0;
                            while (j < pat_len && tolower((unsigned char)data[line_start + k + j]) == (unsigned char)ctx->pattern_lower[j]) {
                                j++;
                            }
                            if (j == pat_len) {
                                found = true;
                                break;
                            }
                        }
                    } else {
                        const char* target = ctx->pattern;
                        char first = target[0];
                        for (size_t k = 0; k <= line_len - pat_len; k++) {
                            if (data[line_start + k] == first && memcmp(&data[line_start + k], target, pat_len) == 0) {
                                found = true;
                                break;
                            }
                        }
                    }
                }

                if (!found) {
                    if (options->max_results > 0 && global_match_count) {
                        LONG cur = InterlockedIncrement(global_match_count);
                        if (cur > (LONG)options->max_results) {
                            if (stop_flag) *stop_flag = 1;
                            return;
                        }
                    }

                    int clean_len = 0;
                    size_t max_len = (options->max_line_len > 0) ? (size_t)options->max_line_len : 250;
                    size_t snippet_len = line_len > max_len ? max_len : line_len;
                    char* snippet = sanitize_snippet(&data[line_start], snippet_len, &clean_len);

                    bool added = add_thread_match(result, options, file_path, line_no, 1, snippet, 0, NULL, 0, NULL, 0);
                    free(snippet);
                    bool stop_file = false;
                    handle_match_record_result(added, options, global_match_count, file_match_count, max_per_file, &stop_file);
                    if (stop_file) return;
                }
            }

            line_start = i + 1;
            line_no++;
        }
    }
}

// Multi-pattern search function supporting logical OR across multiple -e or -f patterns
static void search_buffer_multi(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* data,
    size_t data_len,
    const char* file_path,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    size_t line_start = 0;
    int max_per_file = options->max_matches_per_file;
    size_t num_patterns = ctx->pattern_count;
    int min_len = ctx->min_pattern_len;
    bool invert = options->invert_match;

    for (size_t i = 0; i <= data_len; i++) {
        if (stop_flag && *stop_flag) return;

        if (i == data_len || data[i] == '\n') {
            size_t line_end = (i > line_start && data[i - 1] == '\r') ? i - 1 : i;
            size_t line_len = line_end - line_start;

            if (line_len > 0) {
                bool line_matched = false;
                int matched_pat_idx = -1;
                size_t matched_col = 0;

                if (line_len >= (size_t)min_len) {
                    const char* line_ptr = &data[line_start];

                    for (size_t p = 0; p < num_patterns; p++) {
                        const SearchPatternDesc* pat = &ctx->patterns[p];
                        int pat_len = pat->pattern_len;
                        if (line_len < (size_t)pat_len) continue;

                        if (ctx->ignore_case) {
                            unsigned char first_l = (unsigned char)pat->pattern_lower[0];
                            unsigned char first_u = (unsigned char)pat->pattern_upper[0];

                            for (size_t k = 0; k <= line_len - pat_len; k++) {
                                unsigned char c = (unsigned char)line_ptr[k];
                                if (c == first_l || c == first_u) {
                                    int j = 1;
                                    while (j < pat_len &&
                                           tolower((unsigned char)line_ptr[k + j]) == (unsigned char)pat->pattern_lower[j]) {
                                        j++;
                                    }
                                    if (j == pat_len) {
                                        line_matched = true;
                                        matched_pat_idx = (int)p;
                                        matched_col = k + 1;
                                        break;
                                    }
                                }
                            }
                        } else {
                            char first_c = pat->pattern[0];
                            const char* search_start = line_ptr;
                            size_t remaining = line_len;

                            while (remaining >= (size_t)pat_len) {
                                const char* match_cand = (const char*)memchr(search_start, first_c, remaining - pat_len + 1);
                                if (!match_cand) break;

                                if (memcmp(match_cand, pat->pattern, pat_len) == 0) {
                                    line_matched = true;
                                    matched_pat_idx = (int)p;
                                    matched_col = (size_t)(match_cand - line_ptr) + 1;
                                    break;
                                }

                                size_t offset = (size_t)(match_cand - search_start) + 1;
                                search_start += offset;
                                remaining -= offset;
                            }
                        }

                        if (line_matched) {
                            break;
                        }
                    }
                }

                bool emit = invert ? !line_matched : line_matched;

                if (emit) {
                    if (options->max_results > 0 && global_match_count) {
                        LONG cur = InterlockedIncrement(global_match_count);
                        if (cur > (LONG)options->max_results) {
                            if (stop_flag) *stop_flag = 1;
                            return;
                        }
                    }

                    int match_len = (matched_pat_idx >= 0) ? ctx->patterns[matched_pat_idx].pattern_len : 0;
                    size_t match_offset = (matched_col > 0) ? (line_start + matched_col - 1) : line_start;

                    int64_t l_no = 0, col_no = 0;
                    char* snippet = NULL;
                    char** b_lines = NULL; int b_count = 0;
                    char** a_lines = NULL; int a_count = 0;

                    extract_match_context(options, data, data_len, match_offset, match_len,
                                          &l_no, &col_no, &snippet,
                                          &b_lines, &b_count, &a_lines, &a_count);

                    bool added = add_thread_match(result, options, file_path, l_no, col_no, snippet, match_len,
                                                  b_lines, b_count, a_lines, a_count);
                    free(snippet);
                    bool stop_file = false;
                    handle_match_record_result(added, options, global_match_count, file_match_count, max_per_file, &stop_file);
                    if (stop_file) return;
                }
            }

            line_start = i + 1;
        }
    }
}

// Multiline search implementation with prefix jump acceleration and flexible CRLF/LF verification
static void search_buffer_multiline(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* data,
    size_t data_len,
    const char* file_path,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    if (data_len == 0) return;

    size_t num_patterns = (ctx->pattern_count > 0) ? ctx->pattern_count : 1;

    for (size_t p = 0; p < num_patterns; p++) {
        if (stop_flag && *stop_flag) return;

        const char* pat = (ctx->pattern_count > 0 && ctx->patterns) ? ctx->patterns[p].pattern : ctx->pattern;
        int pat_len = (ctx->pattern_count > 0 && ctx->patterns) ? ctx->patterns[p].pattern_len : ctx->pattern_len;
        if (pat_len <= 0 || data_len < (size_t)pat_len) continue;

        // Find prefix before first newline (\n or \r)
        size_t prefix_len = 0;
        while (prefix_len < (size_t)pat_len && pat[prefix_len] != '\n' && pat[prefix_len] != '\r') {
            prefix_len++;
        }

        if (prefix_len > 0) {
            // Build temporary skip table for prefix
            int skip[256];
            for (int k = 0; k < 256; k++) skip[k] = (int)prefix_len;
            for (size_t k = 0; k < prefix_len - 1; k++) {
                if (ctx->ignore_case) {
                    skip[(unsigned char)tolower((unsigned char)pat[k])] = (int)(prefix_len - 1 - k);
                    skip[(unsigned char)toupper((unsigned char)pat[k])] = (int)(prefix_len - 1 - k);
                } else {
                    skip[(unsigned char)pat[k]] = (int)(prefix_len - 1 - k);
                }
            }

            size_t i = 0;
            size_t max_i = (data_len >= prefix_len) ? (data_len - prefix_len) : 0;

            while (i <= max_i) {
                if (stop_flag && *stop_flag) return;

                bool prefix_match = true;
                if (ctx->ignore_case) {
                    int j = (int)prefix_len - 1;
                    while (j >= 0) {
                        if (tolower((unsigned char)data[i + j]) != tolower((unsigned char)pat[j])) {
                            prefix_match = false;
                            break;
                        }
                        j--;
                    }
                } else {
                    int j = (int)prefix_len - 1;
                    while (j >= 0) {
                        if (data[i + j] != pat[j]) {
                            prefix_match = false;
                            break;
                        }
                        j--;
                    }
                }

                if (prefix_match) {
                    size_t matched_bytes = 0;
                    if (match_text_flexible_newline(&data[i], data_len - i, pat, pat_len, ctx->ignore_case, &matched_bytes)) {
                        if (options->max_results > 0 && global_match_count) {
                            LONG cur = InterlockedIncrement(global_match_count);
                            if (cur > (LONG)options->max_results) {
                                if (stop_flag) *stop_flag = 1;
                                return;
                            }
                        }

                        int64_t line_no = 0, col_no = 0;
                        char* snippet = NULL;
                        char** b_lines = NULL; int b_count = 0;
                        char** a_lines = NULL; int a_count = 0;

                        extract_match_context(options, data, data_len, i, (int)matched_bytes,
                                              &line_no, &col_no, &snippet,
                                              &b_lines, &b_count, &a_lines, &a_count);

                        bool added = add_thread_match(result, options, file_path, line_no, col_no, snippet, (int)matched_bytes,
                                                      b_lines, b_count, a_lines, a_count);
                        free(snippet);
                        bool stop_file = false;
                        handle_match_record_result(added, options, global_match_count, file_match_count, options->max_matches_per_file, &stop_file);
                        if (stop_file) return;

                        i += (matched_bytes > 0 ? matched_bytes : 1);
                        continue;
                    }
                    i++;
                } else {
                    unsigned char c = (unsigned char)data[i + prefix_len - 1];
                    i += skip[c];
                }
            }
        } else {
            // Pattern starts with \n or \r
            size_t cur = 0;
            while (cur < data_len) {
                if (stop_flag && *stop_flag) return;

                const char* nl = (const char*)memchr(&data[cur], '\n', data_len - cur);
                if (!nl) break;
                size_t nl_offset = (size_t)(nl - data);

                size_t test_offset = (nl_offset > 0 && data[nl_offset - 1] == '\r') ? nl_offset - 1 : nl_offset;
                size_t matched_bytes = 0;
                bool found = false;
                size_t found_offset = 0;

                if (match_text_flexible_newline(&data[test_offset], data_len - test_offset, pat, pat_len, ctx->ignore_case, &matched_bytes)) {
                    found = true;
                    found_offset = test_offset;
                } else if (test_offset != nl_offset && match_text_flexible_newline(&data[nl_offset], data_len - nl_offset, pat, pat_len, ctx->ignore_case, &matched_bytes)) {
                    found = true;
                    found_offset = nl_offset;
                }

                if (found) {
                    if (options->max_results > 0 && global_match_count) {
                        LONG cur = InterlockedIncrement(global_match_count);
                        if (cur > (LONG)options->max_results) {
                            if (stop_flag) *stop_flag = 1;
                            return;
                        }
                    }

                    int64_t line_no = 0, col_no = 0;
                    char* snippet = NULL;
                    char** b_lines = NULL; int b_count = 0;
                    char** a_lines = NULL; int a_count = 0;

                    extract_match_context(options, data, data_len, found_offset, (int)matched_bytes,
                                          &line_no, &col_no, &snippet,
                                          &b_lines, &b_count, &a_lines, &a_count);

                    bool added = add_thread_match(result, options, file_path, line_no, col_no, snippet, (int)matched_bytes,
                                                  b_lines, b_count, a_lines, a_count);
                    free(snippet);
                    bool stop_file = false;
                    handle_match_record_result(added, options, global_match_count, file_match_count, options->max_matches_per_file, &stop_file);
                    if (stop_file) return;

                    cur = found_offset + (matched_bytes > 0 ? matched_bytes : 1);
                } else {
                    cur = nl_offset + 1;
                }
            }
        }
    }
}

// Regular expression buffer search implementation supporting single/multiple regexes, anchors, classes and invert match
static void search_buffer_regex(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* data,
    size_t data_len,
    const char* file_path,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    int max_per_file = options->max_matches_per_file;

    // Inverted regex match mode: match lines that do NOT contain regex
    if (options->invert_match) {
        size_t line_start = 0;
        int64_t line_no = 1 + g_tls_line_cache.base_line_offset;

        for (size_t i = 0; i <= data_len; i++) {
            if (stop_flag && *stop_flag) return;

            if (i == data_len || data[i] == '\n') {
                size_t line_end = (i > line_start && data[i - 1] == '\r') ? i - 1 : i;
                size_t line_len = line_end - line_start;

                if (line_len > 0) {
                    bool matched = false;
                    if (ctx->pattern_count > 1) {
                        for (size_t p = 0; p < ctx->pattern_count; p++) {
                            const SearchPatternDesc* pat = &ctx->patterns[p];
                            if (pat->is_regex && pat->regex) {
                                size_t mp = 0, ml = 0;
                                if (regex_search_in_buffer(pat->regex, &data[line_start], line_len, 0, false, &mp, &ml)) {
                                    matched = true;
                                    break;
                                }
                            } else if (pat->pattern_len > 0 && line_len >= (size_t)pat->pattern_len) {
                                const char* lp = &data[line_start];
                                if (ctx->ignore_case) {
                                    for (size_t k = 0; k <= line_len - pat->pattern_len; k++) {
                                        if (_strnicmp(&lp[k], pat->pattern, pat->pattern_len) == 0) {
                                            matched = true;
                                            break;
                                        }
                                    }
                                } else {
                                    for (size_t k = 0; k <= line_len - pat->pattern_len; k++) {
                                        if (memcmp(&lp[k], pat->pattern, pat->pattern_len) == 0) {
                                            matched = true;
                                            break;
                                        }
                                    }
                                }
                                if (matched) break;
                            }
                        }
                    } else if (ctx->regex) {
                        size_t mp = 0, ml = 0;
                        if (regex_search_in_buffer(ctx->regex, &data[line_start], line_len, 0, false, &mp, &ml)) {
                            matched = true;
                        }
                    }

                    if (!matched) {
                        if (options->max_results > 0 && global_match_count) {
                            LONG cur = InterlockedIncrement(global_match_count);
                            if (cur > (LONG)options->max_results) {
                                if (stop_flag) *stop_flag = 1;
                                return;
                            }
                        }

                        int clean_len = 0;
                        size_t max_len = (options->max_line_len > 0) ? (size_t)options->max_line_len : 250;
                        size_t snippet_len = line_len > max_len ? max_len : line_len;
                        char* snippet = sanitize_snippet(&data[line_start], snippet_len, &clean_len);

                        bool added = add_thread_match(result, options, file_path, line_no, 1, snippet, 0, NULL, 0, NULL, 0);
                        free(snippet);
                        bool stop_file = false;
                        handle_match_record_result(added, options, global_match_count, file_match_count, max_per_file, &stop_file);
                        if (stop_file) return;
                    }
                }

                line_start = i + 1;
                line_no++;
            }
        }
        return;
    }

    // Single pattern streaming regex search
    if (ctx->pattern_count <= 1 && ctx->regex) {
        size_t cur = 0;
        while (cur < data_len) {
            if (stop_flag && *stop_flag) return;

            size_t match_pos = 0, match_len = 0;
            if (!regex_search_in_buffer(ctx->regex, data, data_len, cur, options->multiline, &match_pos, &match_len)) {
                break;
            }

            if (options->max_results > 0 && global_match_count) {
                LONG cnt = InterlockedIncrement(global_match_count);
                if (cnt > (LONG)options->max_results) {
                    if (stop_flag) *stop_flag = 1;
                    return;
                }
            }

            int64_t line_no = 0, col_no = 0;
            char* snippet = NULL;
            char** b_lines = NULL; int b_count = 0;
            char** a_lines = NULL; int a_count = 0;

            extract_match_context(options, data, data_len, match_pos, (int)match_len,
                                  &line_no, &col_no, &snippet,
                                  &b_lines, &b_count, &a_lines, &a_count);

            bool added = add_thread_match(result, options, file_path, line_no, col_no, snippet, (int)match_len,
                                          b_lines, b_count, a_lines, a_count);
            free(snippet);
            bool stop_file = false;
            handle_match_record_result(added, options, global_match_count, file_match_count, max_per_file, &stop_file);
            if (stop_file) return;

            cur = match_pos + (match_len > 0 ? match_len : 1);
        }
        return;
    }

    // Multi-pattern regex search: line-by-line deduplication
    size_t line_start = 0;
    for (size_t i = 0; i <= data_len; i++) {
        if (stop_flag && *stop_flag) return;

        if (i == data_len || data[i] == '\n') {
            size_t line_end = (i > line_start && data[i - 1] == '\r') ? i - 1 : i;
            size_t line_len = line_end - line_start;

            if (line_len > 0) {
                for (size_t p = 0; p < ctx->pattern_count; p++) {
                    const SearchPatternDesc* pat = &ctx->patterns[p];
                    size_t mpos = 0, mlen = 0;
                    bool matched = false;

                    if (pat->is_regex && pat->regex) {
                        matched = regex_search_in_buffer(pat->regex, &data[line_start], line_len, 0, false, &mpos, &mlen);
                    } else if (pat->pattern_len > 0 && line_len >= (size_t)pat->pattern_len) {
                        const char* lp = &data[line_start];
                        if (ctx->ignore_case) {
                            for (size_t k = 0; k <= line_len - pat->pattern_len; k++) {
                                if (_strnicmp(&lp[k], pat->pattern, pat->pattern_len) == 0) {
                                    matched = true; mpos = k; mlen = pat->pattern_len; break;
                                }
                            }
                        } else {
                            for (size_t k = 0; k <= line_len - pat->pattern_len; k++) {
                                if (strncmp(&lp[k], pat->pattern, pat->pattern_len) == 0) {
                                    matched = true; mpos = k; mlen = pat->pattern_len; break;
                                }
                            }
                        }
                    }

                    if (matched) {
                        if (options->max_results > 0 && global_match_count) {
                            LONG cnt = InterlockedIncrement(global_match_count);
                            if (cnt > (LONG)options->max_results) {
                                if (stop_flag) *stop_flag = 1;
                                return;
                            }
                        }

                        size_t match_offset = line_start + mpos;
                        int64_t l_no = 0, col_no = 0;
                        char* snippet = NULL;
                        char** b_lines = NULL; int b_count = 0;
                        char** a_lines = NULL; int a_count = 0;

                        extract_match_context(options, data, data_len, match_offset, (int)mlen,
                                              &l_no, &col_no, &snippet,
                                              &b_lines, &b_count, &a_lines, &a_count);

                        bool added = add_thread_match(result, options, file_path, l_no, col_no, snippet, (int)mlen,
                                                      b_lines, b_count, a_lines, a_count);
                        free(snippet);
                        bool stop_file = false;
                        handle_match_record_result(added, options, global_match_count, file_match_count, max_per_file, &stop_file);
                        if (stop_file) return;
                        break; // Deduplicate: match at most one pattern per line
                    }
                }
            }

            line_start = i + 1;
        }
    }
}

// Outline / Symbol extraction scanner: finds all function, method, class, struct, interface definitions
static void search_buffer_outline(
    const SearchOptions* options,
    const char* data,
    size_t data_len,
    const char* file_path,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    size_t line_start = 0;
    int64_t line_no = 1 + g_tls_line_cache.base_line_offset;
    int max_per_file = options->max_matches_per_file;

    for (size_t i = 0; i <= data_len; i++) {
        if (stop_flag && *stop_flag) return;

        if (i == data_len || data[i] == '\n') {
            size_t line_end = (i > line_start && data[i - 1] == '\r') ? i - 1 : i;
            size_t line_len = line_end - line_start;

            if (line_len > 0 && line_len < 512) {
                char buf[512];
                memcpy(buf, &data[line_start], line_len);
                buf[line_len] = '\0';

                if (semantic_is_symbol_definition(buf, file_path)) {
                    // Optional query filter if user passed both --outline and a query
                    bool query_ok = true;
                    if (options->query_len > 0 && strcmp(options->query, "[OUTLINE]") != 0) {
                        query_ok = options->ignore_case
                            ? (str_casestr(buf, options->query) != NULL)
                            : (strstr(buf, options->query) != NULL);
                    }

                    if (query_ok) {
                        if (options->max_results > 0 && global_match_count) {
                            LONG cnt = InterlockedIncrement(global_match_count);
                            if (cnt > (LONG)options->max_results) {
                                if (stop_flag) *stop_flag = 1;
                                return;
                            }
                        }

                        g_tls_line_cache.last_offset = line_start;
                        int clean_len = 0;
                        size_t max_len = (options->max_line_len > 0) ? (size_t)options->max_line_len : 250;
                        size_t snippet_len = line_len > max_len ? max_len : line_len;
                        char* snippet = sanitize_snippet(&data[line_start], snippet_len, &clean_len);

                        bool added = add_thread_match(result, options, file_path, line_no, 1, snippet, (int)snippet_len, NULL, 0, NULL, 0);
                        free(snippet);
                        bool stop_file = false;
                        handle_match_record_result(added, options, global_match_count, file_match_count, max_per_file, &stop_file);
                        if (stop_file) return;
                    }
                }
            }

            line_start = i + 1;
            line_no++;
        }
    }
}

// Master buffer search dispatcher with runtime CPUID dispatch
static void search_buffer(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* data,
    size_t data_len,
    const char* file_path,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    // Reset incremental line counter for the new buffer while keeping base_line_offset
    g_tls_line_cache.data_ptr = data;
    g_tls_line_cache.data_len = data_len;
    g_tls_line_cache.last_offset = 0;
    g_tls_line_cache.last_line_no = 1 + g_tls_line_cache.base_line_offset;
    g_tls_line_cache.last_line_start = 0;

    if (options->outline_mode) {
        search_buffer_outline(options, data, data_len, file_path, result, file_match_count, global_match_count, stop_flag);
        return;
    }

    if (ctx->is_regex) {
        search_buffer_regex(ctx, options, data, data_len, file_path, result, file_match_count, global_match_count, stop_flag);
        return;
    }

    if (options->multiline) {
        search_buffer_multiline(ctx, options, data, data_len, file_path, result, file_match_count, global_match_count, stop_flag);
        return;
    }

    if (ctx->pattern_count > 1) {
        search_buffer_multi(ctx, options, data, data_len, file_path, result, file_match_count, global_match_count, stop_flag);
        return;
    }

    if (options->invert_match) {
        search_buffer_invert(ctx, options, data, data_len, file_path, result, file_match_count, global_match_count, stop_flag);
        return;
    }

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    if (utils_cpu_supports_avx2()) {
        search_buffer_avx2(ctx, options, data, data_len, file_path, result, file_match_count, global_match_count, stop_flag);
    } else {
        search_buffer_horspool(ctx, options, data, data_len, 0, file_path, result, file_match_count, global_match_count, stop_flag);
    }
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
    search_buffer_neon(ctx, options, data, data_len, file_path, result, file_match_count, global_match_count, stop_flag);
#else
    search_buffer_horspool(ctx, options, data, data_len, 0, file_path, result, file_match_count, global_match_count, stop_flag);
#endif
}

void engine_search_decompressed_buffer(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* data,
    size_t data_len,
    const char* file_path,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    search_buffer(ctx, options, data, data_len, file_path, result, file_match_count, global_match_count, stop_flag);
}

bool engine_search_file(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    FileEntry* entry,
    ThreadResult* result,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    if (!ctx || !options || !entry || !result) return false;
    if (entry->file_size == 0) return true; // Empty file -> nothing to scan
    if (stop_flag && *stop_flag) return true;

    g_tls_line_cache.base_line_offset = 0;

#ifdef _WIN32
    HANDLE hFile = CreateFileW(
        entry->path_utf16,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        result->error_count++;
        return false;
    }

    uint64_t file_size = entry->file_size;
    int file_matches = 0;

    // Small File Optimization (< 64 KB): Direct Stack/Heap Buffer Read
    if (file_size <= SMALL_FILE_THRESHOLD) {
        char small_buf[SMALL_FILE_THRESHOLD];
        DWORD bytes_read = 0;
        BOOL read_ok = ReadFile(hFile, small_buf, (DWORD)file_size, &bytes_read, NULL);
        CloseHandle(hFile);

        if (!read_ok || bytes_read == 0) {
            result->error_count++;
            return false;
        }

        // Archive check (.zip, .gz, .tar.gz)
        if (options->search_zip && archive_is_supported_file(entry->path_utf8, (const unsigned char*)small_buf, bytes_read)) {
            archive_search_buffer(ctx, options, entry->path_utf8, (const unsigned char*)small_buf, bytes_read,
                                  result, &file_matches, global_match_count, stop_flag);
            entry->match_count = file_matches;
            result->files_scanned++;
            result->bytes_scanned += bytes_read;
            return true;
        }

        // Binary check
        if (!options->search_binary && is_binary_buffer((const unsigned char*)small_buf, bytes_read)) {
            entry->is_binary = true;
            result->binary_skipped++;
            return true;
        }

        search_buffer(ctx, options, small_buf, bytes_read, entry->path_utf8, result, &file_matches, global_match_count, stop_flag);

        if (file_matches > 0 && options->do_replace) {
            size_t repls = 0;
            bool modified = false;
            bool backed_up = false;
            replace_process_file_buffer(options, entry, small_buf, bytes_read, ctx, &repls, &modified, &backed_up);
            result->replacement_count += repls;
            if (modified) result->files_modified++;
            if (backed_up) result->backups_created++;
        }

        entry->match_count = file_matches;
        result->files_scanned++;
        result->bytes_scanned += bytes_read;
        return true;
    }

    // Medium & Large Files: Memory-Mapped I/O
    if (file_size <= CHUNK_SIZE_LARGE) {
        HANDLE hMapping = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMapping) {
            CloseHandle(hFile);
            result->error_count++;
            return false;
        }

        const char* mapped_data = (const char*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
        if (!mapped_data) {
            CloseHandle(hMapping);
            CloseHandle(hFile);
            result->error_count++;
            return false;
        }

        // Archive check (.zip, .gz, .tar.gz)
        if (options->search_zip && archive_is_supported_file(entry->path_utf8, (const unsigned char*)mapped_data, (size_t)file_size)) {
            archive_search_buffer(ctx, options, entry->path_utf8, (const unsigned char*)mapped_data, (size_t)file_size,
                                  result, &file_matches, global_match_count, stop_flag);
            entry->match_count = file_matches;
            result->files_scanned++;
            result->bytes_scanned += file_size;
            UnmapViewOfFile(mapped_data);
            CloseHandle(hMapping);
            CloseHandle(hFile);
            return true;
        }

        // Check if binary
        if (!options->search_binary && is_binary_buffer((const unsigned char*)mapped_data, file_size)) {
            entry->is_binary = true;
            result->binary_skipped++;
            UnmapViewOfFile(mapped_data);
            CloseHandle(hMapping);
            CloseHandle(hFile);
            return true;
        }

        search_buffer(ctx, options, mapped_data, (size_t)file_size, entry->path_utf8, result, &file_matches, global_match_count, stop_flag);

        entry->match_count = file_matches;
        result->files_scanned++;
        result->bytes_scanned += file_size;

        if (file_matches > 0 && options->do_replace) {
            size_t repls = 0;
            bool modified = false;
            bool backed_up = false;
            char* copy_data = (char*)safe_malloc(file_size);
            memcpy(copy_data, mapped_data, file_size);

            UnmapViewOfFile(mapped_data);
            CloseHandle(hMapping);
            CloseHandle(hFile);

            replace_process_file_buffer(options, entry, copy_data, file_size, ctx, &repls, &modified, &backed_up);
            free(copy_data);

            result->replacement_count += repls;
            if (modified) result->files_modified++;
            if (backed_up) result->backups_created++;
            return true;
        }

        UnmapViewOfFile(mapped_data);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return true;
    }

    // Huge Files (> 32 MB): Windowed Memory-Mapped I/O
    {
        HANDLE hMapping = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMapping) {
            CloseHandle(hFile);
            result->error_count++;
            return false;
        }

        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        DWORD gran = sys_info.dwAllocationGranularity;

        uint64_t offset = 0;
        size_t overlap = (size_t)(ctx->pattern_len > 0 ? ctx->pattern_len : 64);
        bool checked_binary = false;
        int64_t cumulative_lines = 0;

        while (offset < file_size) {
            if (stop_flag && *stop_flag) break;

            uint64_t chunk_len = CHUNK_SIZE_LARGE;
            if (offset + chunk_len > file_size) {
                chunk_len = file_size - offset;
            }

            // Align offset to allocation granularity
            uint64_t aligned_offset = (offset / gran) * gran;
            size_t internal_shift = (size_t)(offset - aligned_offset);
            size_t map_size = (size_t)(chunk_len + internal_shift);

            DWORD offset_high = (DWORD)(aligned_offset >> 32);
            DWORD offset_low = (DWORD)(aligned_offset & 0xFFFFFFFF);

            const char* base_map = (const char*)MapViewOfFile(hMapping, FILE_MAP_READ, offset_high, offset_low, map_size);
            if (!base_map) {
                break;
            }

            const char* chunk_ptr = base_map + internal_shift;

            if (!checked_binary) {
                if (options->search_zip && archive_is_supported_file(entry->path_utf8, (const unsigned char*)chunk_ptr, (size_t)chunk_len)) {
                    UnmapViewOfFile(base_map);
                    const char* full_map = (const char*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
                    if (full_map) {
                        archive_search_buffer(ctx, options, entry->path_utf8, (const unsigned char*)full_map, (size_t)file_size,
                                              result, &file_matches, global_match_count, stop_flag);
                        UnmapViewOfFile(full_map);
                    }
                    entry->match_count = file_matches;
                    result->files_scanned++;
                    result->bytes_scanned += file_size;
                    CloseHandle(hMapping);
                    CloseHandle(hFile);
                    return true;
                }

                if (!options->search_binary && is_binary_buffer((const unsigned char*)chunk_ptr, chunk_len)) {
                    entry->is_binary = true;
                    result->binary_skipped++;
                    UnmapViewOfFile(base_map);
                    CloseHandle(hMapping);
                    CloseHandle(hFile);
                    return true;
                }
                checked_binary = true;
            }

            // If not at EOF and not multiline, align window end to last newline to avoid boundary duplicates
            size_t effective_len = (size_t)chunk_len;
            size_t step_len = effective_len;
            if (offset + chunk_len < file_size) {
                if (!options->multiline) {
                    size_t back_limit = effective_len > 65536 ? (effective_len - 65536) : 0;
                    size_t nl_pos = effective_len;
                    while (nl_pos > back_limit) {
                        if (chunk_ptr[nl_pos - 1] == '\n') {
                            break;
                        }
                        nl_pos--;
                    }
                    if (nl_pos > back_limit) {
                        effective_len = nl_pos;
                        step_len = nl_pos;
                    } else {
                        step_len = (effective_len > overlap) ? (effective_len - overlap) : effective_len;
                    }
                } else {
                    step_len = (effective_len > overlap) ? (effective_len - overlap) : effective_len;
                }
            }

            g_tls_line_cache.base_line_offset = cumulative_lines;
            search_buffer(ctx, options, chunk_ptr, effective_len, entry->path_utf8, result, &file_matches, global_match_count, stop_flag);

            if (offset + chunk_len < file_size) {
                for (size_t k = 0; k < step_len; k++) {
                    if (chunk_ptr[k] == '\n') cumulative_lines++;
                }
            }

            UnmapViewOfFile(base_map);

            if (offset + chunk_len >= file_size) {
                break;
            }

            offset += step_len;
        }

        g_tls_line_cache.base_line_offset = 0;
        entry->match_count = file_matches;
        result->files_scanned++;
        result->bytes_scanned += file_size;

        if (file_matches > 0 && options->do_replace && file_size <= 512ULL * 1024 * 1024) {
            const char* full_map = (const char*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
            if (full_map) {
                size_t repls = 0;
                bool modified = false;
                bool backed_up = false;
                char* copy_data = (char*)safe_malloc((size_t)file_size);
                memcpy(copy_data, full_map, (size_t)file_size);

                UnmapViewOfFile(full_map);
                CloseHandle(hMapping);
                CloseHandle(hFile);

                replace_process_file_buffer(options, entry, copy_data, (size_t)file_size, ctx, &repls, &modified, &backed_up);
                free(copy_data);

                result->replacement_count += repls;
                if (modified) result->files_modified++;
                if (backed_up) result->backups_created++;
                return true;
            }
        }

        CloseHandle(hMapping);
        CloseHandle(hFile);
        return true;
    }
#else // POSIX (Linux / macOS)
    int fd = open(entry->path_utf8, O_RDONLY);
    if (fd < 0) {
        result->error_count++;
        return false;
    }

    uint64_t file_size = entry->file_size;
    int file_matches = 0;

    // Small File Optimization (< 64 KB): Direct Stack Buffer Read
    if (file_size <= SMALL_FILE_THRESHOLD) {
        char small_buf[SMALL_FILE_THRESHOLD];
        ssize_t bytes_read = read(fd, small_buf, (size_t)file_size);
        close(fd);

        if (bytes_read <= 0) {
            result->error_count++;
            return false;
        }

        if (options->search_zip && archive_is_supported_file(entry->path_utf8, (const unsigned char*)small_buf, (size_t)bytes_read)) {
            archive_search_buffer(ctx, options, entry->path_utf8, (const unsigned char*)small_buf, (size_t)bytes_read,
                                  result, &file_matches, global_match_count, stop_flag);
            entry->match_count = file_matches;
            result->files_scanned++;
            result->bytes_scanned += (uint64_t)bytes_read;
            return true;
        }

        if (!options->search_binary && is_binary_buffer((const unsigned char*)small_buf, (size_t)bytes_read)) {
            entry->is_binary = true;
            result->binary_skipped++;
            return true;
        }

        search_buffer(ctx, options, small_buf, (size_t)bytes_read, entry->path_utf8, result, &file_matches, global_match_count, stop_flag);

        if (file_matches > 0 && options->do_replace) {
            size_t repls = 0;
            bool modified = false;
            bool backed_up = false;
            replace_process_file_buffer(options, entry, small_buf, (size_t)bytes_read, ctx, &repls, &modified, &backed_up);
            result->replacement_count += repls;
            if (modified) result->files_modified++;
            if (backed_up) result->backups_created++;
        }

        entry->match_count = file_matches;
        result->files_scanned++;
        result->bytes_scanned += (uint64_t)bytes_read;
        return true;
    }

    // Medium, Large & Huge Files: POSIX mmap()
    {
        void* mapped_ptr = mmap(NULL, (size_t)file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped_ptr == MAP_FAILED) {
            close(fd);
            result->error_count++;
            return false;
        }

#ifdef MADV_SEQUENTIAL
        madvise(mapped_ptr, (size_t)file_size, MADV_SEQUENTIAL);
#endif
        const char* mapped_data = (const char*)mapped_ptr;

        if (options->search_zip && archive_is_supported_file(entry->path_utf8, (const unsigned char*)mapped_data, (size_t)file_size)) {
            archive_search_buffer(ctx, options, entry->path_utf8, (const unsigned char*)mapped_data, (size_t)file_size,
                                  result, &file_matches, global_match_count, stop_flag);
            entry->match_count = file_matches;
            result->files_scanned++;
            result->bytes_scanned += file_size;
            munmap(mapped_ptr, (size_t)file_size);
            close(fd);
            return true;
        }

        if (!options->search_binary && is_binary_buffer((const unsigned char*)mapped_data, (size_t)file_size)) {
            entry->is_binary = true;
            result->binary_skipped++;
            munmap(mapped_ptr, (size_t)file_size);
            close(fd);
            return true;
        }

        search_buffer(ctx, options, mapped_data, (size_t)file_size, entry->path_utf8, result, &file_matches, global_match_count, stop_flag);

        entry->match_count = file_matches;
        result->files_scanned++;
        result->bytes_scanned += file_size;

        if (file_matches > 0 && options->do_replace) {
            size_t repls = 0;
            bool modified = false;
            bool backed_up = false;
            char* copy_data = (char*)safe_malloc((size_t)file_size);
            memcpy(copy_data, mapped_data, (size_t)file_size);

            munmap(mapped_ptr, (size_t)file_size);
            close(fd);

            replace_process_file_buffer(options, entry, copy_data, (size_t)file_size, ctx, &repls, &modified, &backed_up);
            free(copy_data);

            result->replacement_count += repls;
            if (modified) result->files_modified++;
            if (backed_up) result->backups_created++;
            return true;
        }

        munmap(mapped_ptr, (size_t)file_size);
        close(fd);
        return true;
    }
#endif
}
