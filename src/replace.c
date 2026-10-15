#include "replace.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
    size_t start;
    size_t len;
} MatchSpan;

// Generate replaced string from a snippet line
char* replace_generate_snippet(
    const char* snippet,
    const SearchOptions* options,
    const char* pattern,
    int pattern_len,
    const char* replacement
) {
    if (!snippet) return NULL;
    if (!replacement) replacement = "";

    const char* pat = pattern ? pattern : options->query;
    size_t pat_len = pattern_len > 0 ? (size_t)pattern_len : strlen(pat);
    if (pat_len == 0) return safe_strdup(snippet);

    size_t repl_len = strlen(replacement);
    size_t snip_len = strlen(snippet);

    bool is_re = options->is_regex || (!options->fixed_strings && regex_has_metacharacters(pat));
    if (is_re) {
        char err[128] = {0};
        RegexCompiled* re = regex_compile(pat, options->ignore_case, err, sizeof(err));
        if (re) {
            size_t cap = 16, count = 0;
            MatchSpan* spans = (MatchSpan*)safe_malloc(cap * sizeof(MatchSpan));
            size_t cur = 0;
            size_t mpos = 0, mlen = 0;
            while (cur < snip_len && regex_search_in_buffer(re, snippet, snip_len, cur, options->multiline, &mpos, &mlen)) {
                if (count >= cap) {
                    cap *= 2;
                    spans = (MatchSpan*)safe_realloc(spans, cap * sizeof(MatchSpan));
                }
                spans[count].start = mpos;
                spans[count].len = mlen;
                count++;
                cur = mpos + (mlen > 0 ? mlen : 1);
            }
            regex_free(re);

            if (count == 0) {
                free(spans);
                return safe_strdup(snippet);
            }

            size_t matched_total = 0;
            for (size_t i = 0; i < count; i++) matched_total += spans[i].len;
            size_t new_len = snip_len - matched_total + (count * repl_len);
            char* result = (char*)safe_malloc(new_len + 1);
            size_t r = 0, w = 0;
            for (size_t i = 0; i < count; i++) {
                size_t prefix_len = spans[i].start - r;
                if (prefix_len > 0) {
                    memcpy(&result[w], &snippet[r], prefix_len);
                    w += prefix_len;
                }
                if (repl_len > 0) {
                    memcpy(&result[w], replacement, repl_len);
                    w += repl_len;
                }
                r = spans[i].start + spans[i].len;
            }
            if (r < snip_len) {
                memcpy(&result[w], &snippet[r], snip_len - r);
                w += (snip_len - r);
            }
            result[w] = '\0';
            free(spans);
            return result;
        }
    }

    // Count occurrences in snippet
    size_t count = 0;
    size_t cur = 0;
    while (cur + pat_len <= snip_len) {
        bool match = false;
        if (options->ignore_case) {
            match = true;
            for (size_t k = 0; k < pat_len; k++) {
                if (tolower((unsigned char)snippet[cur + k]) != tolower((unsigned char)pat[k])) {
                    match = false;
                    break;
                }
            }
        } else {
            match = (memcmp(&snippet[cur], pat, pat_len) == 0);
        }

        if (match) {
            count++;
            cur += pat_len;
        } else {
            cur++;
        }
    }

    if (count == 0) return safe_strdup(snippet);

    size_t new_size = snip_len - (count * pat_len) + (count * repl_len);
    char* result = (char*)safe_malloc(new_size + 1);
    size_t w = 0;
    cur = 0;

    while (cur < snip_len) {
        bool match = false;
        if (cur + pat_len <= snip_len) {
            if (options->ignore_case) {
                match = true;
                for (size_t k = 0; k < pat_len; k++) {
                    if (tolower((unsigned char)snippet[cur + k]) != tolower((unsigned char)pat[k])) {
                        match = false;
                        break;
                    }
                }
            } else {
                match = (memcmp(&snippet[cur], pat, pat_len) == 0);
            }
        }

        if (match) {
            if (repl_len > 0) {
                memcpy(&result[w], replacement, repl_len);
                w += repl_len;
            }
            cur += pat_len;
        } else {
            result[w++] = snippet[cur++];
        }
    }

    result[w] = '\0';
    return result;
}

// Find all replacement spans in buffer
static MatchSpan* collect_spans(
    const char* buffer,
    size_t buffer_len,
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    size_t* out_count
) {
    size_t capacity = 64;
    size_t count = 0;
    MatchSpan* spans = (MatchSpan*)safe_malloc(capacity * sizeof(MatchSpan));

    size_t num_patterns = (ctx->pattern_count > 0) ? ctx->pattern_count : 1;

    // Scan buffer for matches
    size_t cur = 0;
    while (cur < buffer_len) {
        bool found = false;
        size_t match_len = 0;

        for (size_t p = 0; p < num_patterns; p++) {
            const RegexCompiled* re = (ctx->pattern_count > 0 && ctx->patterns) ? ctx->patterns[p].regex : ctx->regex;
            bool is_re = (ctx->pattern_count > 0 && ctx->patterns) ? ctx->patterns[p].is_regex : ctx->is_regex;

            if (is_re && re) {
                size_t matched_bytes = 0;
                if (regex_match_at(re, buffer, buffer_len, cur, options->multiline, &matched_bytes)) {
                    found = true;
                    match_len = (matched_bytes > 0 ? matched_bytes : 1);
                    break;
                }
                continue;
            }

            const char* pat = (ctx->pattern_count > 0 && ctx->patterns) ? ctx->patterns[p].pattern : ctx->pattern;
            size_t pat_len = (size_t)((ctx->pattern_count > 0 && ctx->patterns) ? ctx->patterns[p].pattern_len : ctx->pattern_len);
            if (pat_len == 0 || cur + pat_len > buffer_len) continue;

            if (options->multiline) {
                // Check flexible newline match
                size_t matched_bytes = 0;
                // Forward declaration of flexible matcher logic or inline byte test
                size_t t = 0, pt = 0;
                bool m = true;
                while (pt < pat_len && cur + t < buffer_len) {
                    if (pat[pt] == '\n') {
                        if (buffer[cur + t] == '\r' && cur + t + 1 < buffer_len && buffer[cur + t + 1] == '\n') {
                            t += 2;
                            pt++;
                            if (pt < pat_len && pat[pt] == '\r') pt++;
                        } else if (buffer[cur + t] == '\n') {
                            t++;
                            pt++;
                        } else {
                            m = false;
                            break;
                        }
                    } else if (pat[pt] == '\r' && pt + 1 < pat_len && pat[pt + 1] == '\n') {
                        if (buffer[cur + t] == '\r' && cur + t + 1 < buffer_len && buffer[cur + t + 1] == '\n') {
                            t += 2;
                            pt += 2;
                        } else if (buffer[cur + t] == '\n') {
                            t++;
                            pt += 2;
                        } else {
                            m = false;
                            break;
                        }
                    } else {
                        unsigned char c1 = (unsigned char)buffer[cur + t];
                        unsigned char c2 = (unsigned char)pat[pt];
                        if (ctx->ignore_case) {
                            if (tolower(c1) != tolower(c2)) { m = false; break; }
                        } else {
                            if (c1 != c2) { m = false; break; }
                        }
                        t++;
                        pt++;
                    }
                }
                if (m && pt == pat_len) {
                    matched_bytes = t;
                    found = true;
                    match_len = matched_bytes;
                    break;
                }
            } else {
                if (ctx->ignore_case) {
                    bool m = true;
                    for (size_t k = 0; k < pat_len; k++) {
                        if (tolower((unsigned char)buffer[cur + k]) != tolower((unsigned char)pat[k])) {
                            m = false;
                            break;
                        }
                    }
                    if (m) {
                        found = true;
                        match_len = pat_len;
                        break;
                    }
                } else {
                    if (memcmp(&buffer[cur], pat, pat_len) == 0) {
                        found = true;
                        match_len = pat_len;
                        break;
                    }
                }
            }
        }

        if (found && match_len > 0) {
            if (count >= capacity) {
                capacity *= 2;
                spans = (MatchSpan*)safe_realloc(spans, capacity * sizeof(MatchSpan));
            }
            spans[count].start = cur;
            spans[count].len = match_len;
            count++;
            cur += match_len;
        } else {
            cur++;
        }
    }

    *out_count = count;
    return spans;
}

// Apply replacement to a file buffer and atomically write changes to disk if in_place is true
bool replace_process_file_buffer(
    const SearchOptions* options,
    const FileEntry* entry,
    const char* buffer,
    size_t buffer_len,
    const SearchEngineContext* ctx,
    size_t* out_replacements,
    bool* out_file_modified,
    bool* out_backup_created
) {
    if (out_replacements) *out_replacements = 0;
    if (out_file_modified) *out_file_modified = false;
    if (out_backup_created) *out_backup_created = false;

    if (!options || !entry || !buffer || buffer_len == 0 || !ctx) return false;

    size_t span_count = 0;
    MatchSpan* spans = collect_spans(buffer, buffer_len, ctx, options, &span_count);

    if (span_count == 0) {
        free(spans);
        return true;
    }

    if (out_replacements) *out_replacements = span_count;

    // If not in-place (Dry Run), we do not touch the disk
    if (!options->in_place) {
        free(spans);
        return true;
    }

    // Calculate new buffer size
    size_t matched_bytes_total = 0;
    for (size_t i = 0; i < span_count; i++) {
        matched_bytes_total += spans[i].len;
    }

    size_t repl_len = (size_t)options->replace_len;
    size_t new_size = buffer_len - matched_bytes_total + (span_count * repl_len);
    char* new_data = (char*)safe_malloc(new_size + 1);

    size_t w = 0;
    size_t r = 0;

    for (size_t i = 0; i < span_count; i++) {
        size_t unch_len = spans[i].start - r;
        if (unch_len > 0) {
            memcpy(&new_data[w], &buffer[r], unch_len);
            w += unch_len;
        }
        if (repl_len > 0) {
            memcpy(&new_data[w], options->replace_text, repl_len);
            w += repl_len;
        }
        r = spans[i].start + spans[i].len;
    }

    size_t tail_len = buffer_len - r;
    if (tail_len > 0) {
        memcpy(&new_data[w], &buffer[r], tail_len);
        w += tail_len;
    }
    new_data[w] = '\0';
    free(spans);

#ifdef _WIN32
    // Create backup file if requested (--backup)
    if (options->backup) {
        wchar_t bak_path[MAX_PATH];
        swprintf(bak_path, MAX_PATH, L"%s.bak", entry->path_utf16);
        if (CopyFileW(entry->path_utf16, bak_path, FALSE)) {
            if (out_backup_created) *out_backup_created = true;
        }
    }

    // Atomic file replacement using temporary file and MoveFileExW
    wchar_t tmp_path[MAX_PATH];
    swprintf(tmp_path, MAX_PATH, L"%s.supersearch_tmp_%lu", entry->path_utf16, (unsigned long)GetCurrentThreadId());

    HANDLE hTmp = CreateFileW(
        tmp_path,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hTmp == INVALID_HANDLE_VALUE) {
        free(new_data);
        return false;
    }

    DWORD written = 0;
    BOOL write_ok = WriteFile(hTmp, new_data, (DWORD)w, &written, NULL);
    FlushFileBuffers(hTmp);
    CloseHandle(hTmp);
    free(new_data);

    if (!write_ok || written != (DWORD)w) {
        DeleteFileW(tmp_path);
        return false;
    }

    BOOL moved = MoveFileExW(tmp_path, entry->path_utf16, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!moved) {
        DeleteFileW(tmp_path);
        return false;
    }
#else
    struct stat orig_st;
    bool has_orig_st = (stat(entry->path_utf8, &orig_st) == 0);

    // Create backup file if requested (--backup)
    if (options->backup) {
        char bak_path[MAX_PATH + 16];
        snprintf(bak_path, sizeof(bak_path), "%s.bak", entry->path_utf8);
        FILE* fbak = fopen(bak_path, "wb");
        if (fbak) {
            if (fwrite(buffer, 1, buffer_len, fbak) == buffer_len) {
                if (out_backup_created) *out_backup_created = true;
            }
            fclose(fbak);
            if (has_orig_st) chmod(bak_path, orig_st.st_mode);
        }
    }

    // Atomic file replacement using temporary file and POSIX rename()
    char tmp_path[MAX_PATH + 64];
    snprintf(tmp_path, sizeof(tmp_path), "%s.supersearch_tmp_%lu", entry->path_utf8, (unsigned long)(uintptr_t)&w);

    FILE* ftmp = fopen(tmp_path, "wb");
    if (!ftmp) {
        free(new_data);
        return false;
    }

    size_t written = fwrite(new_data, 1, w, ftmp);
    fflush(ftmp);
    fclose(ftmp);
    free(new_data);

    if (written != w) {
        unlink(tmp_path);
        return false;
    }

    if (has_orig_st) {
        chmod(tmp_path, orig_st.st_mode);
    }

    if (rename(tmp_path, entry->path_utf8) != 0) {
        unlink(tmp_path);
        return false;
    }
#endif

    if (out_file_modified) *out_file_modified = true;
    return true;
}

// Print unified diff hunk for a modified/previewed match
void replace_print_unified_diff(
    FILE* fp,
    const MatchRecord* match,
    const SearchOptions* options,
    bool use_color
) {
    if (!fp || !match) return;

    int64_t line = match->line_number;
    int before_count = match->lines_before_count;
    int after_count = match->lines_after_count;

    int total_lines_old = before_count + 1 + after_count;
    int total_lines_new = before_count + 1 + after_count;
    int64_t start_line = (line > before_count) ? (line - before_count) : 1;

    // Diff hunk header @@ -start,count +start,count @@
    if (use_color) fputs(ANSI_CYAN, fp);
    fprintf(fp, "@@ -%lld,%d +%lld,%d @@\n",
            (long long)start_line, total_lines_old,
            (long long)start_line, total_lines_new);
    if (use_color) fputs(ANSI_RESET, fp);

    // Preceding context lines
    for (int b = 0; b < before_count; b++) {
        fprintf(fp, " %s\n", match->lines_before[b]);
    }

    // Removed line (-)
    if (use_color) fputs(ANSI_RED, fp);
    fprintf(fp, "-%s\n", match->line_text ? match->line_text : "");
    if (use_color) fputs(ANSI_RESET, fp);

    // Added replacement line (+)
    if (use_color) fputs(ANSI_GREEN, fp);
    fprintf(fp, "+%s\n", match->replaced_text ? match->replaced_text : (options->replace_text[0] != '\0' ? options->replace_text : ""));
    if (use_color) fputs(ANSI_RESET, fp);

    // Succeeding context lines
    for (int a = 0; a < after_count; a++) {
        fprintf(fp, " %s\n", match->lines_after[a]);
    }
}
