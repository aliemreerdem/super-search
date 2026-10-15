#include "report.h"
#include "replace.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void report_print_execution_plan(
    const SearchOptions* options,
    const FileList* list,
    const ExecutionPlan* plan,
    double inspection_ms
) {
    if (options->porcelain || options->json_stdout || options->files_with_matches ||
        options->count_only || options->list_files_only) return;

    bool c = !options->no_color;
    char size_str[32], count_str[32], dirs_str[32], time_str[32];
    char cat_str[128];

    format_bytes(list->total_bytes, size_str, sizeof(size_str));
    format_number(list->count, count_str, sizeof(count_str));
    format_number(list->total_dirs, dirs_str, sizeof(dirs_str));
    format_duration(inspection_ms, time_str, sizeof(time_str));

    snprintf(cat_str, sizeof(cat_str), "%zu Tiny, %zu Small, %zu Med, %zu Large",
        list->cat_counts[CAT_TINY], list->cat_counts[CAT_SMALL],
        list->cat_counts[CAT_MEDIUM], list->cat_counts[CAT_LARGE] + list->cat_counts[CAT_HUGE]);

    printf("\n%s┌── SUPERSEARCH : EXECUTION PLAN & INSPECTION ──────────────────────────────────%s\n", c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "");

    // Search Parameters
    printf("%s│%s  Target Directory : %s%s%s\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
        c ? ANSI_YELLOW : "", options->target_dir, c ? ANSI_RESET : "");

    if (options->pattern_count > 1) {
        printf("%s│%s  Search Patterns  : %s%zu patterns active%s (%s, %s%s%s)\n",
            c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
            c ? ANSI_BOLD_YELLOW : "", options->pattern_count, c ? ANSI_RESET : "",
            options->ignore_case ? "Case-Insensitive" : "Case-Sensitive",
            options->search_binary ? "Binary Included" : "Text Files Only",
            options->invert_match ? ", Invert Match" : "",
            options->multiline ? ", Multiline (-U)" : "");
        for (size_t p = 0; p < options->pattern_count && p < 5; p++) {
            printf("%s│%s    [%zu] \"%s\"\n",
                c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
                p + 1, options->patterns[p]);
        }
        if (options->pattern_count > 5) {
            printf("%s│%s    ... and %zu more patterns\n",
                c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
                options->pattern_count - 5);
        }
    } else {
        printf("%s│%s  Search Query     : %s\"%s\"%s (%s, %s%s%s%s%s)\n",
            c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
            c ? ANSI_BOLD_YELLOW : "", options->query, c ? ANSI_RESET : "",
            options->ignore_case ? "Case-Insensitive" : "Case-Sensitive",
            options->search_binary ? "Binary Included" : "Text Files Only",
            options->invert_match ? ", Invert Match" : "",
            options->multiline ? ", Multiline (-U)" : "",
            options->is_regex ? ", Regex Engine" : "",
            options->search_zip ? ", Search Zip/Gz" : "");
    }

    if (options->do_replace) {
        printf("%s│%s  Replacement Text : %s\"%s\"%s\n",
            c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
            c ? ANSI_BOLD_GREEN : "", options->replace_text, c ? ANSI_RESET : "");
        printf("%s│%s  Replace Mode     : %s%s%s (Backup: %s)\n",
            c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
            options->in_place ? (c ? ANSI_BOLD_RED : "") : (c ? ANSI_BOLD_YELLOW : ""),
            options->in_place ? "In-Place Modification (Active)" : "Dry-Run Preview (No files modified)",
            c ? ANSI_RESET : "",
            options->backup ? "Enabled (.bak)" : "Disabled");
    }

    if (*options->extensions) {
        printf("%s│%s  Extension Filter : %s%s%s\n",
            c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
            c ? ANSI_CYAN : "", options->extensions, c ? ANSI_RESET : "");
    }
    if (*options->excludes) {
        printf("%s│%s  Excluded Dirs    : %s%s%s\n",
            c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
            c ? ANSI_GRAY : "", options->excludes, c ? ANSI_RESET : "");
    }

    printf("%s│%s  Ignore Rules     : %s%s%s\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
        options->no_ignore ? (c ? ANSI_YELLOW : "") : (c ? ANSI_GREEN : ""),
        options->no_ignore ? "Disabled (--no-ignore)" : "Active (.gitignore & .superignore)",
        c ? ANSI_RESET : "");

    if (options->max_results > 0) {
        printf("%s│%s  Max Results Cap  : %s%zu matches%s\n",
            c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
            c ? ANSI_YELLOW : "", options->max_results, c ? ANSI_RESET : "");
    }

    printf("%s│%s\n", c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "");
    printf("%s│%s  %s[INSPECTION PROFILE]%s\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
        c ? ANSI_BOLD_MAGENTA : "", c ? ANSI_RESET : "");

    printf("%s│%s  • Discovered Files    : %s%-16s%s  • Directories Scanned : %s%s%s\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
        c ? ANSI_BOLD_GREEN : "", count_str, c ? ANSI_RESET : "",
        c ? ANSI_BOLD_GREEN : "", dirs_str, c ? ANSI_RESET : "");

    printf("%s│%s  • Total Data Volume   : %s%-16s%s  • Inspection Time     : %s%s%s\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
        c ? ANSI_BOLD_GREEN : "", size_str, c ? ANSI_RESET : "",
        c ? ANSI_BOLD_GREEN : "", time_str, c ? ANSI_RESET : "");

    printf("%s│%s  • Filename Matches    : %s%-16zu%s  • Size Categories     : %s%s%s\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
        c ? ANSI_BOLD_YELLOW : "", list->filename_matches, c ? ANSI_RESET : "",
        c ? ANSI_WHITE : "", cat_str, c ? ANSI_RESET : "");

    printf("%s│%s\n", c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "");
    printf("%s│%s  %s[PARALLEL EXECUTION STRATEGY]%s\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
        c ? ANSI_BOLD_MAGENTA : "", c ? ANSI_RESET : "");

    printf("%s│%s  • Active Workers      : %s%d Threads%s (%d Logical Cores Available)\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
        c ? ANSI_BOLD_GREEN : "", plan->worker_threads, c ? ANSI_RESET : "",
        plan->logical_cores);

    printf("%s│%s  • Dispatch Strategy   : %sLock-Free Dynamic Queue%s (Chunk: %zu files)\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
        c ? ANSI_CYAN : "", c ? ANSI_RESET : "",
        plan->dynamic_chunk_size);

    printf("%s│%s  • Load Balancing      : %s%s%s\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
        c ? ANSI_WHITE : "", plan->sort_strategy, c ? ANSI_RESET : "");

    printf("%s│%s  • Memory & I/O Model  : %s%s%s\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
        c ? ANSI_WHITE : "", plan->io_strategy, c ? ANSI_RESET : "");

    printf("%s│%s  • SIMD Engine         : %s%s%s\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
        c ? ANSI_BOLD_GREEN : "", plan->simd_acceleration, c ? ANSI_RESET : "");

    printf("%s└───────────────────────────────────────────────────────────────────────────────%s\n\n",
        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "");
    fflush(stdout);
}

// Print a line snippet with matched keyword highlighted
static void print_highlighted_snippet(const char* snippet, const char* query, bool ignore_case, bool use_color) {
    if (!snippet) return;
    if (!use_color || !query || !*query) {
        printf("    %s\n", snippet);
        return;
    }

    size_t qlen = strlen(query);
    const char* cursor = snippet;

    printf("    ");
    while (*cursor) {
        const char* match = ignore_case ? str_casestr(cursor, query) : strstr(cursor, query);
        if (!match) {
            printf("%s", cursor);
            break;
        }

        if (match > cursor) {
            fwrite(cursor, 1, match - cursor, stdout);
        }

        printf("%s%s", ANSI_BOLD_WHITE, ANSI_BG_RED);
        fwrite(match, 1, qlen, stdout);
        printf("%s", ANSI_RESET);

        cursor = match + qlen;
    }
    printf("\n");
}

void report_print_matches(const SearchOptions* options, const SearchReport* report) {
    if (options->json_stdout) return; // Suppressed in JSON mode

    bool c = !options->no_color;

    if (report->total_matches == 0) {
        if (!options->porcelain && !options->files_with_matches && !options->count_only) {
            printf("%s[No matches found for \"%s\"]%s\n\n", c ? ANSI_YELLOW : "", options->query, c ? ANSI_RESET : "");
        }
        return;
    }

    if (options->files_with_matches) {
        const char* cur = NULL;
        for (size_t i = 0; i < report->all_matches_count; i++) {
            const MatchRecord* m = &report->all_matches[i];
            if (!cur || strcmp(cur, m->file_path) != 0) {
                cur = m->file_path;
                printf("%s\n", m->file_path);
            }
        }
        return;
    }

    if (options->count_only) {
        const char* cur = NULL;
        size_t count = 0;
        for (size_t i = 0; i < report->all_matches_count; i++) {
            const MatchRecord* m = &report->all_matches[i];
            if (m->is_filename_match) continue;
            if (!cur) {
                cur = m->file_path;
                count = 1;
            } else if (strcmp(cur, m->file_path) == 0) {
                count++;
            } else {
                printf("%s:%zu\n", cur, count);
                cur = m->file_path;
                count = 1;
            }
        }
        if (cur && count > 0) {
            printf("%s:%zu\n", cur, count);
        }
        return;
    }

    if (options->diff_output) {
        const char* cur_file = NULL;
        for (size_t i = 0; i < report->all_matches_count; i++) {
            const MatchRecord* m = &report->all_matches[i];
            if (m->is_filename_match) continue;
            if (!cur_file || strcmp(cur_file, m->file_path) != 0) {
                cur_file = m->file_path;
                printf("%s--- a/%s%s\n", c ? ANSI_BOLD_MAGENTA : "", m->file_path, c ? ANSI_RESET : "");
                printf("%s+++ b/%s%s\n", c ? ANSI_BOLD_CYAN : "", m->file_path, c ? ANSI_RESET : "");
            }
            replace_print_unified_diff(stdout, m, options, c);
        }
        return;
    }

    if (!options->porcelain) {
        printf("%s─── SEARCH RESULTS ─────────────────────────────────────────────────────────────%s\n\n",
            c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "");
    }

    const char* current_file = NULL;
    size_t budget_bytes = options->max_tokens > 0 ? options->max_tokens * 4 : 0;
    size_t used_bytes = 0;

    for (size_t i = 0; i < report->all_matches_count; i++) {
        const MatchRecord* m = &report->all_matches[i];

        if (budget_bytes > 0) {
            size_t m_bytes = (m->file_path ? strlen(m->file_path) : 0) +
                             (m->line_text ? strlen(m->line_text) : 0) +
                             (m->enclosing_func ? strlen(m->enclosing_func) : 0) + 32;
            if (used_bytes + m_bytes > budget_bytes && i > 0) {
                printf("\n[TOKEN LIMIT REACHED: %zu matches omitted (max_tokens=%zu)]\n",
                    report->all_matches_count - i, options->max_tokens);
                break;
            }
            used_bytes += m_bytes;
        }

        // Group by file
        if (!current_file || strcmp(current_file, m->file_path) != 0) {
            current_file = m->file_path;
            if (options->porcelain) {
                // In porcelain mode: FILE: <path>
                printf("\n%s\n", m->file_path);
            } else {
                if (m->is_filename_match) {
                    printf("%s[FILE MATCH]%s %s%s%s\n",
                        c ? ANSI_BOLD_MAGENTA : "", c ? ANSI_RESET : "",
                        c ? ANSI_BOLD_WHITE : "", m->file_path, c ? ANSI_RESET : "");
                } else if (options->do_replace) {
                    if (options->in_place) {
                        printf("%s[REPLACED]%s %s%s%s\n",
                            c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
                            c ? ANSI_BOLD_WHITE : "", m->file_path, c ? ANSI_RESET : "");
                    } else {
                        printf("%s[REPLACE PREVIEW]%s %s%s%s\n",
                            c ? ANSI_BOLD_YELLOW : "", c ? ANSI_RESET : "",
                            c ? ANSI_BOLD_WHITE : "", m->file_path, c ? ANSI_RESET : "");
                    }
                } else {
                    printf("%s[MATCH]%s %s%s%s\n",
                        c ? ANSI_BOLD_CYAN : "", c ? ANSI_RESET : "",
                        c ? ANSI_BOLD_WHITE : "", m->file_path, c ? ANSI_RESET : "");
                }
            }
        }

        if (!m->is_filename_match) {
            if (m->enclosing_func) {
                if (options->porcelain) {
                    printf("@@ %lld: %s @@\n", (long long)m->enclosing_line, m->enclosing_func);
                } else {
                    printf("  %s@@ Line %-4lld: %s @@%s\n",
                        c ? ANSI_BOLD_MAGENTA : "", (long long)m->enclosing_line, m->enclosing_func, c ? ANSI_RESET : "");
                }
            }

            // Print preceding context lines if any
            if (m->lines_before_count > 0 && !options->porcelain) {
                for (int b = 0; b < m->lines_before_count; b++) {
                    int64_t b_line = m->line_number - m->lines_before_count + b;
                    printf("  %sLine %-5lld%s %s│%s    %s\n",
                        c ? ANSI_DIM : "", (long long)b_line, c ? ANSI_RESET : "",
                        c ? ANSI_DIM : "", c ? ANSI_RESET : "", m->lines_before[b]);
                }
            }

            // Print matching line(s)
            if (options->multiline && strchr(m->line_text, '\n')) {
                const char* cur = m->line_text;
                int64_t cur_line = m->line_number;
                int64_t cur_col = m->col_number;

                while (*cur) {
                    const char* next_nl = strchr(cur, '\n');
                    size_t sub_len = next_nl ? (size_t)(next_nl - cur) : strlen(cur);

                    char sub_buf[1024];
                    if (sub_len >= sizeof(sub_buf)) sub_len = sizeof(sub_buf) - 1;
                    memcpy(sub_buf, cur, sub_len);
                    sub_buf[sub_len] = '\0';

                    if (options->porcelain) {
                        printf("%lld:%lld:%s\n", (long long)cur_line, (long long)cur_col, sub_buf);
                    } else {
                        printf("  %sLine %-5lld%s %sCol %-3lld%s  ",
                            c ? ANSI_BOLD_GREEN : "", (long long)cur_line, c ? ANSI_RESET : "",
                            c ? ANSI_DIM : "", (long long)cur_col, c ? ANSI_RESET : "");
                        print_highlighted_snippet(sub_buf, options->query, options->ignore_case, c);
                    }

                    if (!next_nl) break;
                    cur = next_nl + 1;
                    cur_line++;
                    cur_col = 1;
                }
            } else if (options->do_replace) {
                if (options->porcelain) {
                    printf("%lld:%lld:-:%s\n", (long long)m->line_number, (long long)m->col_number, m->line_text);
                    printf("%lld:%lld:+:%s\n", (long long)m->line_number, (long long)m->col_number, m->replaced_text ? m->replaced_text : options->replace_text);
                } else {
                    printf("  %sLine %-5lld%s %sCol %-3lld%s  %s-%s  %s\n",
                        c ? ANSI_BOLD_YELLOW : "", (long long)m->line_number, c ? ANSI_RESET : "",
                        c ? ANSI_DIM : "", (long long)m->col_number, c ? ANSI_RESET : "",
                        c ? ANSI_BOLD_RED : "", c ? ANSI_RESET : "", m->line_text);
                    printf("                 %s+%s  %s\n",
                        c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "", m->replaced_text ? m->replaced_text : options->replace_text);
                }
            } else {
                if (options->porcelain) {
                    printf("%lld:%lld:%s\n", (long long)m->line_number, (long long)m->col_number, m->line_text);
                } else {
                    printf("  %sLine %-5lld%s %sCol %-3lld%s  ",
                        c ? ANSI_BOLD_GREEN : "", (long long)m->line_number, c ? ANSI_RESET : "",
                        c ? ANSI_DIM : "", (long long)m->col_number, c ? ANSI_RESET : "");
                    print_highlighted_snippet(m->line_text, options->query, options->ignore_case, c);
                }
            }

            // Print succeeding context lines if any
            if (m->lines_after_count > 0 && !options->porcelain) {
                int64_t after_base = m->line_number + 1;
                if (options->multiline && strchr(m->line_text, '\n')) {
                    const char* p = m->line_text;
                    while (*p) {
                        if (*p == '\n') after_base++;
                        p++;
                    }
                }
                for (int a = 0; a < m->lines_after_count; a++) {
                    int64_t a_line = after_base + a;
                    printf("  %sLine %-5lld%s %s│%s    %s\n",
                        c ? ANSI_DIM : "", (long long)a_line, c ? ANSI_RESET : "",
                        c ? ANSI_DIM : "", c ? ANSI_RESET : "", m->lines_after[a]);
                }
            }
        }
    }
    printf("\n");
}

void report_print_summary(const SearchReport* report) {
    if (report->options.json_stdout || report->options.porcelain ||
        report->options.files_with_matches || report->options.count_only ||
        report->options.list_files_only) return;

    bool c = !report->options.no_color;
    char total_size_str[32], throughput_str[32], total_time_str[32];
    char search_time_str[32], plan_time_str[32], files_sec_str[32];

    format_bytes(report->bytes_scanned, total_size_str, sizeof(total_size_str));
    format_duration(report->total_time_ms, total_time_str, sizeof(total_time_str));
    format_duration(report->search_time_ms, search_time_str, sizeof(search_time_str));
    format_duration(report->inspection_time_ms, plan_time_str, sizeof(plan_time_str));

    if (report->throughput_mb_s >= 1024.0) {
        snprintf(throughput_str, sizeof(throughput_str), "%.2f GB/s", report->throughput_mb_s / 1024.0);
    } else {
        snprintf(throughput_str, sizeof(throughput_str), "%.2f MB/s", report->throughput_mb_s);
    }

    snprintf(files_sec_str, sizeof(files_sec_str), "%.0f files/s", report->files_per_sec);

    printf("%s┌── SUPERSEARCH : PERFORMANCE & SUMMARY REPORT ─────────────────────────────────%s\n", c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "");

    printf("%s│%s  Total Matches Found   : %s%zu%s (Filename: %zu, Content: %zu)\n",
        c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
        c ? ANSI_BOLD_YELLOW : "", report->total_matches, c ? ANSI_RESET : "",
        report->filename_matches, report->content_matches);

    if (report->truncated) {
        printf("%s│%s  %s[WARNING]%s Match limit reached (capped at %zu matches for context limits)\n",
            c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
            c ? ANSI_BOLD_RED : "", c ? ANSI_RESET : "",
            report->options.max_results);
    }

    printf("%s│%s  Files With Matches    : %s%zu%s  (Scanned Files: %zu)\n",
        c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
        c ? ANSI_BOLD_YELLOW : "", report->files_with_matches, c ? ANSI_RESET : "",
        report->files_scanned);

    printf("%s│%s  Total Scanned Volume  : %s%s%s  (Skipped Binaries: %zu)\n",
        c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
        c ? ANSI_WHITE : "", total_size_str, c ? ANSI_RESET : "",
        report->binary_skipped);

    printf("%s│%s\n", c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "");
    printf("%s│%s  Execution Speed       : %s%s%s  (Scan Rate: %s)\n",
        c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
        c ? ANSI_BOLD_CYAN : "", throughput_str, c ? ANSI_RESET : "",
        files_sec_str);

    printf("%s│%s  Plan & Inspection Time: %s%s%s\n",
        c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
        c ? ANSI_WHITE : "", plan_time_str, c ? ANSI_RESET : "");

    printf("%s│%s  Parallel Search Time  : %s%s%s\n",
        c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
        c ? ANSI_WHITE : "", search_time_str, c ? ANSI_RESET : "");

    printf("%s│%s  Total Elapsed Time    : %s%s%s\n",
        c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
        c ? ANSI_BOLD_YELLOW : "", total_time_str, c ? ANSI_RESET : "");

    if (report->options.do_replace) {
        printf("%s│%s\n", c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "");
        if (report->options.in_place) {
            printf("%s│%s  Replacements Applied  : %s%zu%s across %zu files %s[MODIFIED IN-PLACE]%s\n",
                c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
                c ? ANSI_BOLD_CYAN : "", report->total_replacements, c ? ANSI_RESET : "",
                report->files_modified,
                c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "");
            if (report->options.backup) {
                printf("%s│%s  Backup Files Created  : %s%zu%s (.bak files saved)\n",
                    c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
                    c ? ANSI_BOLD_YELLOW : "", report->backups_created, c ? ANSI_RESET : "");
            }
        } else {
            printf("%s│%s  Replacements Previewed: %s%zu%s across %zu files %s[DRY RUN - No files modified]%s\n",
                c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
                c ? ANSI_BOLD_CYAN : "", report->total_replacements, c ? ANSI_RESET : "",
                report->files_with_matches,
                c ? ANSI_BOLD_YELLOW : "", c ? ANSI_RESET : "");
            printf("%s│%s  %s(Tip: Run with --in-place to apply replacements to disk)%s\n",
                c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "",
                c ? ANSI_DIM : "", c ? ANSI_RESET : "");
        }
    }

    printf("%s└───────────────────────────────────────────────────────────────────────────────%s\n\n",
        c ? ANSI_BOLD_GREEN : "", c ? ANSI_RESET : "");
}

bool report_export_text(const SearchReport* report, const char* filepath) {
    if (!report || !filepath) return false;

    FILE* fp = fopen(filepath, "w");
    if (!fp) return false;

    char size_str[32], total_time_str[32], search_time_str[32];
    format_bytes(report->bytes_scanned, size_str, sizeof(size_str));
    format_duration(report->total_time_ms, total_time_str, sizeof(total_time_str));
    format_duration(report->search_time_ms, search_time_str, sizeof(search_time_str));

    fprintf(fp, "================================================================================\n");
    fprintf(fp, " SUPERSEARCH EXECUTION REPORT\n");
    fprintf(fp, "================================================================================\n");
    fprintf(fp, "Target Directory : %s\n", report->options.target_dir);
    fprintf(fp, "Search Query     : \"%s\" (Case-Sensitive: %s)\n",
        report->options.query, report->options.ignore_case ? "No" : "Yes");
    fprintf(fp, "Total Files      : %zu\n", report->files_scanned);
    fprintf(fp, "Total Bytes      : %s\n", size_str);
    fprintf(fp, "Search Time      : %s\n", search_time_str);
    fprintf(fp, "Total Time       : %s\n", total_time_str);
    fprintf(fp, "Throughput       : %.2f MB/s\n", report->throughput_mb_s);
    fprintf(fp, "Total Matches    : %zu (Filename: %zu, Content: %zu)\n",
        report->total_matches, report->filename_matches, report->content_matches);
    if (report->truncated) {
        fprintf(fp, "Truncated        : Yes (Capped at %zu matches)\n", report->options.max_results);
    }
    fprintf(fp, "================================================================================\n\n");

    const char* cur_file = NULL;
    for (size_t i = 0; i < report->all_matches_count; i++) {
        const MatchRecord* m = &report->all_matches[i];
        if (!cur_file || strcmp(cur_file, m->file_path) != 0) {
            cur_file = m->file_path;
            if (m->is_filename_match) {
                fprintf(fp, "\n[FILE MATCH] %s\n", m->file_path);
            } else {
                fprintf(fp, "\nFile: %s\n", m->file_path);
            }
        }

        if (!m->is_filename_match) {
            for (int b = 0; b < m->lines_before_count; b++) {
                int64_t b_line = m->line_number - m->lines_before_count + b;
                fprintf(fp, "  %lld | %s\n", (long long)b_line, m->lines_before[b]);
            }
            fprintf(fp, "> Line %lld, Col %lld: %s\n",
                (long long)m->line_number, (long long)m->col_number, m->line_text);
            for (int a = 0; a < m->lines_after_count; a++) {
                int64_t a_line = m->line_number + 1 + a;
                fprintf(fp, "  %lld | %s\n", (long long)a_line, m->lines_after[a]);
            }
        }
    }

    fclose(fp);
    return true;
}

static void json_write_escaped_string(FILE* fp, const char* str) {
    if (!str) {
        fputs("\"\"", fp);
        return;
    }
    fputc('"', fp);
    for (; *str; str++) {
        switch (*str) {
            case '"':  fputs("\\\"", fp); break;
            case '\\': fputs("\\\\", fp); break;
            case '\b': fputs("\\b", fp); break;
            case '\f': fputs("\\f", fp); break;
            case '\n': fputs("\\n", fp); break;
            case '\r': fputs("\\r", fp); break;
            case '\t': fputs("\\t", fp); break;
            default:
                if ((unsigned char)*str < 32) {
                    fprintf(fp, "\\u%04x", (unsigned char)*str);
                } else {
                    fputc(*str, fp);
                }
                break;
        }
    }
    fputc('"', fp);
}

void report_serialize_json_stream(FILE* fp, const SearchReport* report) {
    size_t emit_count = report->all_matches_count;
    bool token_limit_reached = report->token_limit_reached;
    size_t omitted_matches = report->omitted_matches;

    if (report->options.max_tokens > 0) {
        size_t budget_bytes = report->options.max_tokens * 4;
        size_t used_bytes = 360;
        emit_count = 0;
        for (size_t i = 0; i < report->all_matches_count; i++) {
            const MatchRecord* m = &report->all_matches[i];
            size_t m_bytes = 96;
            if (m->file_path) m_bytes += strlen(m->file_path);
            if (m->line_text) m_bytes += strlen(m->line_text);
            if (m->enclosing_func) m_bytes += strlen(m->enclosing_func) + 40;
            for (int b = 0; b < m->lines_before_count; b++) {
                if (m->lines_before[b]) m_bytes += strlen(m->lines_before[b]) + 4;
            }
            for (int a = 0; a < m->lines_after_count; a++) {
                if (m->lines_after[a]) m_bytes += strlen(m->lines_after[a]) + 4;
            }
            if (used_bytes + m_bytes > budget_bytes && emit_count > 0) {
                token_limit_reached = true;
                omitted_matches = report->all_matches_count - emit_count;
                break;
            }
            used_bytes += m_bytes;
            emit_count++;
        }
        if (emit_count == 0 && report->all_matches_count > 0 && budget_bytes < used_bytes) {
            token_limit_reached = true;
            omitted_matches = report->all_matches_count;
        }
    }

    fprintf(fp, "{\n");
    if (report->options.pattern_count > 1) {
        fprintf(fp, "  \"patterns\": [");
        for (size_t p = 0; p < report->options.pattern_count; p++) {
            json_write_escaped_string(fp, report->options.patterns[p]);
            if (p + 1 < report->options.pattern_count) fprintf(fp, ", ");
        }
        fprintf(fp, "],\n");
    }
    fprintf(fp, "  \"query\": ");
    json_write_escaped_string(fp, report->options.query);
    fprintf(fp, ",\n  \"directory\": ");
    json_write_escaped_string(fp, report->options.target_dir);
    fprintf(fp, ",\n  \"ignore_case\": %s,\n", report->options.ignore_case ? "true" : "false");
    fprintf(fp, "  \"invert_match\": %s,\n", report->options.invert_match ? "true" : "false");
    fprintf(fp, "  \"multiline\": %s,\n", report->options.multiline ? "true" : "false");
    fprintf(fp, "  \"regex\": %s,\n", report->options.is_regex ? "true" : "false");
    fprintf(fp, "  \"search_zip\": %s,\n", report->options.search_zip ? "true" : "false");
    if (report->options.outline_mode) {
        fprintf(fp, "  \"outline_mode\": true,\n");
    }
    fprintf(fp, "  \"replace_mode\": %s,\n", report->options.do_replace ? "true" : "false");
    if (report->options.do_replace) {
        fprintf(fp, "  \"replace_text\": ");
        json_write_escaped_string(fp, report->options.replace_text);
        fprintf(fp, ",\n  \"in_place\": %s,\n", report->options.in_place ? "true" : "false");
        fprintf(fp, "  \"total_replacements\": %zu,\n", report->total_replacements);
        fprintf(fp, "  \"files_modified\": %zu,\n", report->files_modified);
        fprintf(fp, "  \"backups_created\": %zu,\n", report->backups_created);
    }
    fprintf(fp, "  \"total_matches\": %zu,\n", report->total_matches);
    fprintf(fp, "  \"filename_matches\": %zu,\n", report->filename_matches);
    fprintf(fp, "  \"content_matches\": %zu,\n", report->content_matches);
    fprintf(fp, "  \"files_with_matches\": %zu,\n", report->files_with_matches);
    fprintf(fp, "  \"truncated\": %s,\n", (report->truncated || token_limit_reached) ? "true" : "false");
    fprintf(fp, "  \"token_limit_reached\": %s,\n", token_limit_reached ? "true" : "false");
    fprintf(fp, "  \"omitted_matches\": %zu,\n", omitted_matches);
    fprintf(fp, "  \"files_scanned\": %zu,\n", report->files_scanned);
    fprintf(fp, "  \"bytes_scanned\": %llu,\n", (unsigned long long)report->bytes_scanned);
    fprintf(fp, "  \"inspection_time_ms\": %.2f,\n", report->inspection_time_ms);
    fprintf(fp, "  \"search_time_ms\": %.2f,\n", report->search_time_ms);
    fprintf(fp, "  \"total_time_ms\": %.2f,\n", report->total_time_ms);
    fprintf(fp, "  \"throughput_mb_s\": %.2f,\n", report->throughput_mb_s);
    fprintf(fp, "  \"workers\": %d,\n", report->plan.worker_threads);
    fprintf(fp, "  \"matches\": [\n");

    for (size_t i = 0; i < emit_count; i++) {
        const MatchRecord* m = &report->all_matches[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"file\": ");
        json_write_escaped_string(fp, m->file_path);
        fprintf(fp, ",\n      \"is_filename\": %s,\n", m->is_filename_match ? "true" : "false");
        fprintf(fp, "      \"line\": %lld,\n", (long long)m->line_number);
        fprintf(fp, "      \"column\": %lld,\n", (long long)m->col_number);
        fprintf(fp, "      \"snippet\": ");
        json_write_escaped_string(fp, m->line_text);
        if (m->replaced_text) {
            fprintf(fp, ",\n      \"replaced_snippet\": ");
            json_write_escaped_string(fp, m->replaced_text);
        }
        if (m->enclosing_func) {
            fprintf(fp, ",\n      \"enclosing_function\": ");
            json_write_escaped_string(fp, m->enclosing_func);
            fprintf(fp, ",\n      \"enclosing_line\": %lld", (long long)m->enclosing_line);
        }

        // Surrounding lines before
        fprintf(fp, ",\n      \"lines_before\": [");
        for (int b = 0; b < m->lines_before_count; b++) {
            json_write_escaped_string(fp, m->lines_before[b]);
            if (b + 1 < m->lines_before_count) fprintf(fp, ", ");
        }
        fprintf(fp, "]");

        // Surrounding lines after
        fprintf(fp, ",\n      \"lines_after\": [");
        for (int a = 0; a < m->lines_after_count; a++) {
            json_write_escaped_string(fp, m->lines_after[a]);
            if (a + 1 < m->lines_after_count) fprintf(fp, ", ");
        }
        fprintf(fp, "]");

        fprintf(fp, "\n    }%s\n", (i + 1 < emit_count) ? "," : "");
    }

    fprintf(fp, "  ]\n}\n");
}

bool report_export_json(const SearchReport* report, const char* filepath) {
    if (!report || !filepath) return false;

    FILE* fp = fopen(filepath, "w");
    if (!fp) return false;

    report_serialize_json_stream(fp, report);
    fclose(fp);
    return true;
}

void report_print_json_stdout(const SearchReport* report) {
    if (!report) return;
    report_serialize_json_stream(stdout, report);
}

void report_print_json_plan(
    const SearchOptions* options,
    const FileList* list,
    const ExecutionPlan* plan,
    double inspection_ms
) {
    printf("{\n");
    printf("  \"plan_only\": true,\n");
    printf("  \"directory\": ");
    json_write_escaped_string(stdout, options->target_dir);
    printf(",\n  \"query\": ");
    json_write_escaped_string(stdout, options->query);
    printf(",\n  \"files_discovered\": %zu,\n", list->count);
    printf("  \"directories_scanned\": %zu,\n", list->total_dirs);
    printf("  \"total_bytes\": %llu,\n", (unsigned long long)list->total_bytes);
    printf("  \"filename_matches\": %zu,\n", list->filename_matches);
    printf("  \"categories\": {\n");
    printf("    \"tiny\": %zu,\n", list->cat_counts[CAT_TINY]);
    printf("    \"small\": %zu,\n", list->cat_counts[CAT_SMALL]);
    printf("    \"medium\": %zu,\n", list->cat_counts[CAT_MEDIUM]);
    printf("    \"large\": %zu\n", list->cat_counts[CAT_LARGE] + list->cat_counts[CAT_HUGE]);
    printf("  },\n");
    printf("  \"inspection_time_ms\": %.2f,\n", inspection_ms);
    printf("  \"recommended_workers\": %d,\n", plan->worker_threads);
    printf("  \"dynamic_chunk_size\": %zu,\n", plan->dynamic_chunk_size);
    printf("  \"io_strategy\": ");
    json_write_escaped_string(stdout, plan->io_strategy);
    printf(",\n  \"simd_acceleration\": ");
    json_write_escaped_string(stdout, plan->simd_acceleration);
    printf("\n}\n");
}

void report_print_file_list(
    const SearchOptions* options,
    const FileList* list,
    double inspection_ms
) {
    size_t limit = list->count;
    if (options->max_results > 0 && options->max_results < limit) {
        limit = options->max_results;
    }

    if (options->json_stdout) {
        printf("{\n");
        printf("  \"files_only\": true,\n");
        printf("  \"directory\": ");
        json_write_escaped_string(stdout, options->target_dir);
        printf(",\n  \"total_files\": %zu,\n", list->count);
        printf("  \"returned_files\": %zu,\n", limit);
        printf("  \"truncated\": %s,\n", (limit < list->count) ? "true" : "false");
        printf("  \"total_bytes\": %llu,\n", (unsigned long long)list->total_bytes);
        printf("  \"inspection_time_ms\": %.2f,\n", inspection_ms);
        printf("  \"files\": [\n");
        for (size_t i = 0; i < limit; i++) {
            printf("    ");
            json_write_escaped_string(stdout, list->items[i].path_utf8);
            printf("%s\n", (i + 1 < limit) ? "," : "");
        }
        printf("  ]\n}\n");
    } else {
        for (size_t i = 0; i < limit; i++) {
            printf("%s\n", list->items[i].path_utf8);
        }
    }
}

void report_print_mcp_config(const char* prog_path) {
    char exe_path[MAX_PATH] = {0};
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, exe_path, MAX_PATH) == 0 && prog_path) {
        strncpy(exe_path, prog_path, sizeof(exe_path) - 1);
    }
#else
    ssize_t rl = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (rl > 0) {
        exe_path[rl] = '\0';
    } else if (prog_path && realpath(prog_path, exe_path) != NULL) {
        // realpath populated exe_path
    } else if (prog_path) {
        strncpy(exe_path, prog_path, sizeof(exe_path) - 1);
    } else {
        strcpy(exe_path, "supersearch");
    }
#endif
    printf("{\n");
    printf("  \"mcpServers\": {\n");
    printf("    \"supersearch\": {\n");
    printf("      \"command\": ");
    json_write_escaped_string(stdout, exe_path);
    printf(",\n");
    printf("      \"args\": [\"--mcp-serve\"]\n");
    printf("    }\n");
    printf("  }\n");
    printf("}\n");
}

void report_print_mcp_schema(void) {
    const char* schema =
        "{\n"
        "  \"name\": \"supersearch\",\n"
        "  \"description\": \"Ultra-fast parallel file and code search engine with pre-search planning and SIMD AVX2 acceleration.\",\n"
        "  \"parameters\": {\n"
        "    \"type\": \"object\",\n"
        "    \"properties\": {\n"
        "      \"directory\": {\n"
        "        \"type\": \"string\",\n"
        "        \"description\": \"Target root directory to search recursively\"\n"
        "      },\n"
        "      \"query\": {\n"
        "        \"type\": \"string\",\n"
        "        \"description\": \"Keyword, phrase, or token to search in filenames and contents\"\n"
        "      },\n"
        "      \"ignore_case\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Case-insensitive search (default: false)\"\n"
        "      },\n"
        "      \"smart_case\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Case-insensitive unless query contains uppercase characters (-S/--smart-case)\"\n"
        "      },\n"
        "      \"word_regexp\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Match whole words only (-w/--word-regexp)\"\n"
        "      },\n"
        "      \"invert_match\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Invert match: select lines that do not contain the query (default: false)\"\n"
        "      },\n"
        "      \"regex\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Enable regular expression search mode (default: auto/false)\"\n"
        "      },\n"
        "      \"fixed_strings\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Treat search pattern strictly as a fixed literal string (default: false)\"\n"
        "      },\n"
        "      \"search_zip\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Search inside compressed archive files (.zip, .gz, .tar.gz) (default: false)\"\n"
        "      },\n"
        "      \"extensions\": {\n"
        "        \"type\": \"string\",\n"
        "        \"description\": \"Comma-separated file extensions to include (e.g., '.c,.h,.ts,.py')\"\n"
        "      },\n"
        "      \"type\": {\n"
        "        \"type\": \"string\",\n"
        "        \"description\": \"Predefined language/file type filter (e.g., 'c', 'cs', 'rust', 'py', 'js', 'ts', 'web', 'sql')\"\n"
        "      },\n"
        "      \"glob\": {\n"
        "        \"type\": \"string\",\n"
        "        \"description\": \"Comma-separated glob patterns (prefix with '!' to exclude, e.g. 'src/*.c,!*test*')\"\n"
        "      },\n"
        "      \"exclude\": {\n"
        "        \"type\": \"string\",\n"
        "        \"description\": \"Comma-separated directory names to exclude (e.g., '.git,node_modules,bin')\"\n"
        "      },\n"
        "      \"kind\": {\n"
        "        \"type\": \"string\",\n"
        "        \"description\": \"Semantic token filter: 'comment', 'string', 'code', or 'function'\"\n"
        "      },\n"
        "      \"enclosing_function\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Include the enclosing function/class/struct signature for each match (-W/--enclosing-function)\"\n"
        "      },\n"
        "      \"files_with_matches\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Only return one match per file (-l/--files-with-matches)\"\n"
        "      },\n"
        "      \"count_only\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Count matches per file (-c/--count)\"\n"
        "      },\n"
        "      \"max_results\": {\n"
        "        \"type\": \"integer\",\n"
        "        \"description\": \"Maximum total matches to return before truncation (context limit safety)\"\n"
        "      },\n"
        "      \"max_tokens\": {\n"
        "        \"type\": \"integer\",\n"
        "        \"description\": \"Approximate LLM token budget cap (--max-tokens)\"\n"
        "      },\n"
        "      \"context\": {\n"
        "        \"type\": \"integer\",\n"
        "        \"description\": \"Number of lines of code context to return before and after match\"\n"
        "      },\n"
        "      \"plan_only\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"If true, only returns directory profile, file counts, and plan without searching contents\"\n"
        "      },\n"
        "      \"no_ignore\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Disable .gitignore and .superignore parsing (default: false)\"\n"
        "      },\n"
        "      \"hidden\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Search hidden files and directories (default: false)\"\n"
        "      },\n"
        "      \"multiline\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Match across newline boundaries (-U/--multiline) (default: false)\"\n"
        "      },\n"
        "      \"patterns\": {\n"
        "        \"type\": \"array\",\n"
        "        \"items\": { \"type\": \"string\" },\n"
        "        \"description\": \"Multiple search patterns (-e/--regexp p1 -e p2)\"\n"
        "      },\n"
        "      \"pattern_file\": {\n"
        "        \"type\": \"string\",\n"
        "        \"description\": \"Path to file containing newline-separated search patterns (-f/--file)\"\n"
        "      },\n"
        "      \"replace\": {\n"
        "        \"type\": \"string\",\n"
        "        \"description\": \"Replacement string to substitute matching query (-r/--replace)\"\n"
        "      },\n"
        "      \"in_place\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Modify files on disk (default: false, preview/dry-run mode)\"\n"
        "      },\n"
        "      \"backup\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Create .bak backup copy before modifying file (default: false)\"\n"
        "      },\n"
        "      \"diff\": {\n"
        "        \"type\": \"boolean\",\n"
        "        \"description\": \"Emit modifications in standard unified diff format (default: false)\"\n"
        "      }\n"
        "    },\n"
        "    \"required\": [\"directory\", \"query\"]\n"
        "  }\n"
        "}\n";
    fputs(schema, stdout);
}

void report_free(SearchReport* report) {
    if (!report) return;
    if (report->all_matches) {
        for (size_t i = 0; i < report->all_matches_count; i++) {
            free(report->all_matches[i].file_path);
            free(report->all_matches[i].line_text);
            if (report->all_matches[i].replaced_text) {
                free(report->all_matches[i].replaced_text);
            }
            if (report->all_matches[i].enclosing_func) {
                free(report->all_matches[i].enclosing_func);
            }
            if (report->all_matches[i].lines_before) {
                for (int b = 0; b < report->all_matches[i].lines_before_count; b++) {
                    free(report->all_matches[i].lines_before[b]);
                }
                free(report->all_matches[i].lines_before);
            }
            if (report->all_matches[i].lines_after) {
                for (int a = 0; a < report->all_matches[i].lines_after_count; a++) {
                    free(report->all_matches[i].lines_after[a]);
                }
                free(report->all_matches[i].lines_after);
            }
        }
        free(report->all_matches);
        report->all_matches = NULL;
    }
}
