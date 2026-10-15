#include "supersearch.h"
#include "utils.h"
#include "scanner.h"
#include "engine.h"
#include "threadpool.h"
#include "report.h"
#include "mcp_server.h"
#include "interactive.h"
#include "semantic.h"
#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_banner(void) {
    printf("%s   _____                               _____                                %s\n", ANSI_BOLD_CYAN, ANSI_RESET);
    printf("%s  / ____|                             / ____|                               %s\n", ANSI_BOLD_CYAN, ANSI_RESET);
    printf("%s | (___  _   _ _ __   ___ _ __       | (___   ___  __ _ _ __ ___| |__       %s\n", ANSI_BOLD_CYAN, ANSI_RESET);
    printf("%s  \\___ \\| | | | '_ \\ / _ \\ '__|  ___  \\___ \\ / _ \\/ _` | '__/ __| '_ \\      %s\n", ANSI_BOLD_CYAN, ANSI_RESET);
    printf("%s  ____) | |_| | |_) |  __/ |    |___| ____) |  __/ (_| | | | (__| | | |     %s\n", ANSI_BOLD_CYAN, ANSI_RESET);
    printf("%s |_____/ \\__,_| .__/ \\___|_|         |_____/ \\___|\\__,_|_|  \\___|_| |_|     %s\n", ANSI_BOLD_CYAN, ANSI_RESET);
    printf("%s              | |                                                           %s\n", ANSI_BOLD_CYAN, ANSI_RESET);
    printf("%s              |_|    v%s - High-Performance Parallel Search Engine       %s\n\n", ANSI_BOLD_CYAN, APP_VERSION, ANSI_RESET);
}

static void print_usage(const char* prog_name) {
    print_banner();
    printf("USAGE:\n");
    printf("  %s <directory> <query> [options]\n", prog_name);
    printf("  %s <query> [directory] [options]   (ripgrep / grep compatible order)\n", prog_name);
    printf("  %s -d <directory> -q <query> [options]\n\n", prog_name);
    printf("OPTIONS:\n");
    printf("  -d, --dir <path>         Target directory or file to search\n");
    printf("  -q, --query <phrase>     Search keyword, phrase, or regular expression\n");
    printf("  -i, --ignore-case        Case-insensitive search\n");
    printf("  -S, --smart-case         Case-insensitive unless query contains uppercase letters\n");
    printf("  -w, --word-regexp        Match whole words only (\\b...\\b)\n");
    printf("  -v, --invert-match       Invert match: select non-matching lines\n");
    printf("  -l, --files-with-matches Print only paths of files with matches\n");
    printf("  --files                  Print all candidate files without searching contents (fd / rg --files)\n");
    printf("  -c, --count              Print match count per file (<file>:<count>)\n");
    printf("  -g, --glob <pattern>     Include/exclude files matching glob (e.g., -g \"*.c\" -g \"!*test*\")\n");
    printf("  -t, --type <type>        Predefined language / file type filter (e.g., c, cs, py, js, rust, web)\n");
    printf("  --type-list              List all supported file types and extensions\n");
    printf("  -e, --regexp <pattern>   Search pattern (can be specified multiple times: -e p1 -e p2)\n");
    printf("  -f, --file <file>        Read newline-separated patterns from file\n");
    printf("  -R, --regex              Enable regular expression search engine\n");
    printf("  -F, --fixed-strings      Treat patterns as literal fixed strings\n");
    printf("  --ext <.c,.h,...>        Comma-separated file extensions to include\n");
    printf("  --threads <num>          Number of parallel worker threads (Default: Auto-detect)\n");
    printf("  -x, --exclude <dirs>     Comma-separated directories to skip (e.g., .git,node_modules,bin,obj)\n");
    printf("  -b, --binary             Search binary files as well (Default: Text files only)\n");
    printf("  -m, --max-size <MB>      Maximum file size to scan in MB\n");
    printf("  -o, --output <file>      Save search report to plain text file\n");
    printf("  -j, --json <file>        Export search report to JSON file\n");
    printf("  --no-color               Disable colored terminal output\n");
    printf("  --no-ignore              Do not respect .gitignore and .superignore rules\n");
    printf("  --hidden                 Search hidden files and directories\n");
    printf("  -U, --multiline          Enable multiline matching across newline boundaries\n");
    printf("  -z, --search-zip         Search inside compressed archives (.zip, .gz, .tar.gz)\n");
    printf("  -h, --help               Display this help message\n\n");
    printf("REFACTORING & SEARCH-REPLACE OPTIONS:\n");
    printf("  -r, --replace <text>     Replacement string for matched patterns\n");
    printf("  --in-place               Apply modifications directly to files on disk\n");
    printf("  --backup                 Create .bak backup copies of modified files\n");
    printf("  --diff                   Print changes in Git unified diff patch format\n\n");
    printf("AI AGENT & TOKEN ECONOMY OPTIONS:\n");
    printf("  --json-stdout            Output pure JSON report directly to stdout\n");
    printf("  --porcelain, --quiet     Suppress headers and banners, output raw machine-readable lines\n");
    printf("  --plan-only, --dry-run   Return directory profile and execution plan without scanning contents\n");
    printf("  -W, --enclosing-function Attach the enclosing function/class signature and line number to matches\n");
    printf("  --outline, --symbols     Extract all function, struct, and class definitions without a query\n");
    printf("  --max-tokens <num>       Cap output at approximately N LLM tokens (~N*4 bytes)\n");
    printf("  --mcp-schema             Output Model Context Protocol (MCP) tool schema JSON\n");
    printf("  --mcp-serve              Start persistent MCP stdio JSON-RPC 2.0 server\n");
    printf("  --mcp-config             Print ready-to-use mcpServers JSON config for Claude/Cursor/Windsurf\n");
    printf("  --max-results <num>      Maximum total matches to return (prevents context overflow)\n");
    printf("  --head-per-file <num>    Maximum matches allowed per individual file\n");
    printf("  -C, --context <N>        Include N lines of code context before and after each match\n");
    printf("  -B, --before <N>         Include N lines of code context before each match\n");
    printf("  -A, --after <N>          Include N lines of code context after each match\n");
    printf("  --max-line-len <num>     Maximum characters per snippet line (Default: 250)\n");
    printf("  -I, --interactive        Launch live interactive terminal UI (TUI / FZF mode)\n");
    printf("  --kind <kind>            Filter by semantic code structure (comment, string, code, function)\n");
    printf("  --cache                  Use persistent binary disk cache for sub-millisecond discovery\n");
    printf("  --cache-build            Force rebuild persistent directory index cache\n");
    printf("  --cache-clear            Clear stored directory index cache for target directory\n");
    printf("  --watch                  Run background directory watcher daemon via ReadDirectoryChangesW\n\n");
    printf("EXAMPLES:\n");
    printf("  %s c:\\Code\\MyProject \"database connection\"\n", prog_name);
    printf("  %s \"TODO\" src -S -W --json-stdout\n", prog_name);
    printf("  %s --outline src --json-stdout\n", prog_name);
    printf("  %s --files -g \"*.c\" -g \"!*test*\"\n\n", prog_name);
}

static void append_glob_pattern(SearchOptions* options, const char* gpat) {
    if (!gpat || !*gpat) return;
    if (gpat[0] == '!') {
        gpat++;
        if (*gpat) {
            if (options->exclude_globs[0]) {
                strncat(options->exclude_globs, ",", sizeof(options->exclude_globs) - strlen(options->exclude_globs) - 1);
            }
            strncat(options->exclude_globs, gpat, sizeof(options->exclude_globs) - strlen(options->exclude_globs) - 1);
        }
    } else {
        if (options->include_globs[0]) {
            strncat(options->include_globs, ",", sizeof(options->include_globs) - strlen(options->include_globs) - 1);
        }
        strncat(options->include_globs, gpat, sizeof(options->include_globs) - strlen(options->include_globs) - 1);
    }
}

static bool parse_arguments(int argc, char* argv[], SearchOptions* options) {
    memset(options, 0, sizeof(SearchOptions));
    options->max_matches_per_file = 500;
    strncpy(options->excludes, ".git,.vs,node_modules,bin,obj", sizeof(options->excludes) - 1);

    const char* pos_args[32];
    int pos_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("SuperSearch v%s\n", APP_VERSION);
            exit(0);
        } else if (strcmp(argv[i], "--type-list") == 0) {
            utils_print_type_list();
            exit(0);
        } else if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
            utils_resolve_type_extensions(argv[++i], options->extensions, sizeof(options->extensions));
        } else if (strcmp(argv[i], "--mcp-schema") == 0 || strcmp(argv[i], "--schema") == 0) {
            report_print_mcp_schema();
            exit(0);
        } else if (strcmp(argv[i], "--mcp-config") == 0) {
            report_print_mcp_config(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--mcp-serve") == 0 || strcmp(argv[i], "--mcp") == 0) {
            exit(mcp_server_run());
        } else if (strcmp(argv[i], "--plan-only") == 0 || strcmp(argv[i], "--dry-run") == 0) {
            options->plan_only = true;
        } else if (strcmp(argv[i], "--json-stdout") == 0 || strcmp(argv[i], "--json-stream") == 0) {
            options->json_stdout = true;
            options->no_color = true;
        } else if (strcmp(argv[i], "--porcelain") == 0 || strcmp(argv[i], "--quiet") == 0) {
            options->porcelain = true;
            options->no_color = true;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--ignore-case") == 0) {
            options->ignore_case = true;
        } else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--smart-case") == 0) {
            options->smart_case = true;
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--word-regexp") == 0) {
            options->word_regexp = true;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--files-with-matches") == 0) {
            options->files_with_matches = true;
        } else if (strcmp(argv[i], "--files") == 0) {
            options->list_files_only = true;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--count") == 0) {
            options->count_only = true;
        } else if (strcmp(argv[i], "-W") == 0 || strcmp(argv[i], "--enclosing-function") == 0) {
            options->enclosing_function = true;
        } else if (strcmp(argv[i], "--outline") == 0 || strcmp(argv[i], "--symbols") == 0) {
            options->outline_mode = true;
        } else if (strcmp(argv[i], "--max-tokens") == 0 && i + 1 < argc) {
            options->max_tokens = (size_t)atoll(argv[++i]);
        } else if ((strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--glob") == 0) && i + 1 < argc) {
            append_glob_pattern(options, argv[++i]);
        } else if (strncmp(argv[i], "--glob=", 7) == 0) {
            append_glob_pattern(options, argv[i] + 7);
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--binary") == 0) {
            options->search_binary = true;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            options->no_color = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--invert-match") == 0) {
            options->invert_match = true;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            options->verbose = true;
        } else if (strcmp(argv[i], "--no-ignore") == 0) {
            options->no_ignore = true;
        } else if (strcmp(argv[i], "--hidden") == 0) {
            options->hidden = true;
        } else if (strcmp(argv[i], "-U") == 0 || strcmp(argv[i], "--multiline") == 0) {
            options->multiline = true;
        } else if (strcmp(argv[i], "-R") == 0 || strcmp(argv[i], "--regex") == 0) {
            options->is_regex = true;
        } else if (strcmp(argv[i], "-F") == 0 || strcmp(argv[i], "--fixed-strings") == 0) {
            options->fixed_strings = true;
        } else if (strcmp(argv[i], "-z") == 0 || strcmp(argv[i], "--search-zip") == 0) {
            options->search_zip = true;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--line-number") == 0 ||
                   strcmp(argv[i], "--column") == 0 || strcmp(argv[i], "--no-heading") == 0 ||
                   strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--with-filename") == 0 ||
                   strcmp(argv[i], "--vimgrep") == 0) {
            // Accepted for ripgrep / grep CLI drop-in compatibility
        } else if (strncmp(argv[i], "--color=", 8) == 0) {
            if (strcmp(argv[i] + 8, "never") == 0) options->no_color = true;
        } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
            if (strcmp(argv[++i], "never") == 0) options->no_color = true;
        } else if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--replace") == 0) && i + 1 < argc) {
            options->do_replace = true;
            strncpy(options->replace_text, argv[++i], sizeof(options->replace_text) - 1);
            options->replace_len = (int)strlen(options->replace_text);
        } else if (strcmp(argv[i], "--in-place") == 0) {
            options->in_place = true;
        } else if (strcmp(argv[i], "--backup") == 0) {
            options->backup = true;
        } else if (strcmp(argv[i], "--diff") == 0) {
            options->diff_output = true;
        } else if (strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "--interactive") == 0) {
            options->interactive = true;
        } else if (strcmp(argv[i], "--kind") == 0 && i + 1 < argc) {
            options->kind_filter = (int)semantic_parse_kind(argv[++i]);
            if (options->kind_filter == 0) {
                fprintf(stderr, "Error: Invalid --kind value '%s'. Supported: comment, string, code, function\n", argv[i]);
                return false;
            }
        } else if (strcmp(argv[i], "--cache") == 0) {
            options->use_cache = true;
        } else if (strcmp(argv[i], "--cache-build") == 0) {
            options->build_cache = true;
            options->use_cache = true;
        } else if (strcmp(argv[i], "--cache-clear") == 0) {
            options->clear_cache = true;
        } else if (strcmp(argv[i], "--watch") == 0) {
            options->watch_mode = true;
            options->use_cache = true;
        } else if ((strcmp(argv[i], "-C") == 0 || strcmp(argv[i], "--context") == 0) && i + 1 < argc) {
            int ctx = atoi(argv[++i]);
            options->context_before = ctx;
            options->context_after = ctx;
        } else if ((strcmp(argv[i], "-A") == 0 || strcmp(argv[i], "--after") == 0) && i + 1 < argc) {
            options->context_after = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-B") == 0 || strcmp(argv[i], "--before") == 0) && i + 1 < argc) {
            options->context_before = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-results") == 0 && i + 1 < argc) {
            options->max_results = (size_t)atoll(argv[++i]);
        } else if (strcmp(argv[i], "--head-per-file") == 0 && i + 1 < argc) {
            options->max_matches_per_file = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-line-len") == 0 && i + 1 < argc) {
            options->max_line_len = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0) && i + 1 < argc) {
            strncpy(options->target_dir, argv[++i], sizeof(options->target_dir) - 1);
        } else if ((strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--query") == 0) && i + 1 < argc) {
            strncpy(options->query, argv[++i], sizeof(options->query) - 1);
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            options->threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            const char* arg = argv[++i];
            bool is_numeric = true;
            for (const char* p = arg; *p; p++) {
                if (!isdigit((unsigned char)*p)) {
                    is_numeric = false;
                    break;
                }
            }
            if (is_numeric && atoi(arg) > 0) {
                options->threads = atoi(arg);
            } else {
                utils_resolve_type_extensions(arg, options->extensions, sizeof(options->extensions));
            }
        } else if (strcmp(argv[i], "--ext") == 0 && i + 1 < argc) {
            strncpy(options->extensions, argv[++i], sizeof(options->extensions) - 1);
        } else if ((strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--regexp") == 0) && i + 1 < argc) {
            const char* val = argv[++i];
            if (strcmp(argv[i - 1], "-e") == 0 && val[0] == '.' && strchr(val, ',') != NULL) {
                // Backward compatibility for old -e .c,.h
                strncpy(options->extensions, val, sizeof(options->extensions) - 1);
            } else {
                options->patterns = (char**)safe_realloc(options->patterns, (options->pattern_count + 1) * sizeof(char*));
                options->patterns[options->pattern_count++] = safe_strdup(val);
            }
        } else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) && i + 1 < argc) {
            const char* pat_file = argv[++i];
            strncpy(options->pattern_file, pat_file, sizeof(options->pattern_file) - 1);
            FILE* fp = fopen(pat_file, "r");
            if (!fp) {
                fprintf(stderr, "%sERROR: Failed to open pattern file: %s%s\n", ANSI_BOLD_RED, pat_file, ANSI_RESET);
                exit(2);
            }
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                size_t l = strlen(line);
                while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n' || isspace((unsigned char)line[l - 1]))) {
                    line[--l] = '\0';
                }
                char* start = line;
                while (*start && isspace((unsigned char)*start)) start++;
                if (*start) {
                    options->patterns = (char**)safe_realloc(options->patterns, (options->pattern_count + 1) * sizeof(char*));
                    options->patterns[options->pattern_count++] = safe_strdup(start);
                }
            }
            fclose(fp);
        } else if ((strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--exclude") == 0) && i + 1 < argc) {
            strncpy(options->excludes, argv[++i], sizeof(options->excludes) - 1);
        } else if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--max-size") == 0) && i + 1 < argc) {
            uint64_t mb = (uint64_t)atoll(argv[++i]);
            options->max_file_size = mb * 1024ULL * 1024ULL;
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            strncpy(options->output_file, argv[++i], sizeof(options->output_file) - 1);
        } else if ((strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--json") == 0) && i + 1 < argc) {
            strncpy(options->json_file, argv[++i], sizeof(options->json_file) - 1);
        } else if (argv[i][0] != '-') {
            if (pos_count < 32) {
                pos_args[pos_count++] = argv[i];
            }
        }
    }

    // Smart positional argument resolution (supports both <dir> <query>, <query> <dir>, and <query>)
    bool no_query_mode = options->list_files_only || options->outline_mode ||
                         options->interactive || options->clear_cache ||
                         options->watch_mode || options->build_cache;

    if (options->target_dir[0] != '\0' && options->query[0] == '\0' && !no_query_mode) {
        // -d was explicitly given; remaining positional args form the query
        for (int p = 0; p < pos_count; p++) {
            if (p > 0) strncat(options->query, " ", sizeof(options->query) - strlen(options->query) - 1);
            strncat(options->query, pos_args[p], sizeof(options->query) - strlen(options->query) - 1);
        }
    } else if ((options->query[0] != '\0' || options->pattern_count > 0 || no_query_mode) && options->target_dir[0] == '\0') {
        // -q/-e/-f or a query-less mode was given; positional arg is target_dir
        if (pos_count >= 1) {
            strncpy(options->target_dir, pos_args[0], sizeof(options->target_dir) - 1);
        } else {
            strcpy(options->target_dir, ".");
        }
    } else if (options->target_dir[0] == '\0' && options->query[0] == '\0') {
        if (pos_count == 1) {
            if (options->plan_only && utils_path_exists(pos_args[0])) {
                strncpy(options->target_dir, pos_args[0], sizeof(options->target_dir) - 1);
            } else {
                // Single positional argument -> rg-style `supersearch <query>` in current directory "."
                strcpy(options->target_dir, ".");
                strncpy(options->query, pos_args[0], sizeof(options->query) - 1);
            }
        } else if (pos_count >= 2) {
            bool first_is_path = utils_path_exists(pos_args[0]);
            bool last_is_path = utils_path_exists(pos_args[pos_count - 1]);

            if (!first_is_path && last_is_path) {
                // rg-style order: `supersearch <query> <dir>`
                strncpy(options->target_dir, pos_args[pos_count - 1], sizeof(options->target_dir) - 1);
                for (int p = 0; p < pos_count - 1; p++) {
                    if (p > 0) strncat(options->query, " ", sizeof(options->query) - strlen(options->query) - 1);
                    strncat(options->query, pos_args[p], sizeof(options->query) - strlen(options->query) - 1);
                }
            } else {
                // Traditional order: `supersearch <dir> <query>`
                strncpy(options->target_dir, pos_args[0], sizeof(options->target_dir) - 1);
                for (int p = 1; p < pos_count; p++) {
                    if (p > 1) strncat(options->query, " ", sizeof(options->query) - strlen(options->query) - 1);
                    strncat(options->query, pos_args[p], sizeof(options->query) - strlen(options->query) - 1);
                }
            }
        }
    }

    if (options->outline_mode && options->query[0] == '\0') {
        strcpy(options->query, "[OUTLINE]");
    }

    if (options->pattern_count == 0 && options->query[0] != '\0' && !options->outline_mode) {
        options->patterns = (char**)safe_malloc(sizeof(char*));
        options->patterns[0] = safe_strdup(options->query);
        options->pattern_count = 1;
    } else if (options->pattern_count > 0 && options->query[0] == '\0') {
        if (options->pattern_count == 1) {
            strncpy(options->query, options->patterns[0], sizeof(options->query) - 1);
        } else {
            snprintf(options->query, sizeof(options->query), "%s (+%zu patterns)",
                     options->patterns[0], options->pattern_count - 1);
        }
    }

    if (no_query_mode || options->plan_only) {
        if (options->target_dir[0] == '\0') {
            strcpy(options->target_dir, ".");
        }
    } else if (options->target_dir[0] == '\0' || (options->query[0] == '\0' && options->pattern_count == 0)) {
        print_usage(argv[0]);
        return false;
    }

    // Apply -S / --smart-case: case-insensitive unless pattern has an uppercase ASCII character
    if (options->smart_case && !options->ignore_case) {
        bool has_upper = false;
        for (const char* p = options->query; *p; p++) {
            if (isupper((unsigned char)*p)) {
                has_upper = true;
                break;
            }
        }
        for (size_t k = 0; !has_upper && k < options->pattern_count; k++) {
            for (const char* p = options->patterns[k]; *p; p++) {
                if (isupper((unsigned char)*p)) {
                    has_upper = true;
                    break;
                }
            }
        }
        if (!has_upper) {
            options->ignore_case = true;
        }
    }

    // Apply -w / --word-regexp: wrap each pattern with \b...\b and enable regex engine
    if (options->word_regexp && !options->outline_mode) {
        options->is_regex = true;
        options->fixed_strings = false;
        for (size_t k = 0; k < options->pattern_count; k++) {
            size_t plen = strlen(options->patterns[k]);
            char* wrapped = (char*)safe_malloc(plen + 8);
            snprintf(wrapped, plen + 8, "\\b%s\\b", options->patterns[k]);
            free(options->patterns[k]);
            options->patterns[k] = wrapped;
        }
        if (options->pattern_count == 1) {
            strncpy(options->query, options->patterns[0], sizeof(options->query) - 1);
        }
    }

    // Optimization for -l / --files-with-matches: stop after 1st match per file
    if (options->files_with_matches) {
        options->max_matches_per_file = 1;
    }

    if (options->multiline) {
        if (options->query[0] != '\0') {
            char* unesc = utils_unescape_query(options->query);
            if (unesc) {
                strncpy(options->query, unesc, sizeof(options->query) - 1);
                options->query[sizeof(options->query) - 1] = '\0';
                free(unesc);
            }
        }
        for (size_t p = 0; p < options->pattern_count; p++) {
            char* unesc = utils_unescape_query(options->patterns[p]);
            if (unesc) {
                free(options->patterns[p]);
                options->patterns[p] = unesc;
            }
        }
        if (options->do_replace && options->replace_text[0] != '\0') {
            char* unesc_repl = utils_unescape_query(options->replace_text);
            if (unesc_repl) {
                strncpy(options->replace_text, unesc_repl, sizeof(options->replace_text) - 1);
                options->replace_text[sizeof(options->replace_text) - 1] = '\0';
                options->replace_len = (int)strlen(options->replace_text);
                free(unesc_repl);
            }
        }
    }

    options->query_len = (int)strlen(options->query);

    // Convert target directory to UTF-16
    if (!utf8_to_utf16(options->target_dir, options->target_dir_w, MAX_PATH)) {
        fprintf(stderr, "Error: Invalid target directory path: %s\n", options->target_dir);
        return false;
    }

    return true;
}

static void free_options(SearchOptions* options) {
    if (!options) return;
    if (options->patterns) {
        for (size_t i = 0; i < options->pattern_count; i++) {
            free(options->patterns[i]);
        }
        free(options->patterns);
        options->patterns = NULL;
    }
}

int main(int argc, char* argv[]) {
    // 1. Initialize UTF-8 console and virtual terminal processing immediately
    bool initial_no_color = false;
    utils_init_terminal(&initial_no_color);

    SearchOptions options;
    if (!parse_arguments(argc, argv, &options)) {
        return 2; // Error exit code for agents
    }

    if (options.interactive) {
        int rc = interactive_run(&options);
        free_options(&options);
        return rc;
    }

    if (options.clear_cache) {
        if (cache_clear(options.target_dir)) {
            printf("[INFO] Instant cache cleared for '%s'.\n", options.target_dir);
        } else {
            printf("[INFO] No cache found to clear for '%s'.\n", options.target_dir);
        }
        free_options(&options);
        return 0;
    }

    if (options.watch_mode) {
        printf("%s[DAEMON] Watching directory '%s' in background via ReadDirectoryChangesW...%s\n",
               options.no_color ? "" : ANSI_BOLD_CYAN, options.target_dir, options.no_color ? "" : ANSI_RESET);
        printf("%sPress Ctrl+C to exit.%s\n\n",
               options.no_color ? "" : ANSI_GRAY, options.no_color ? "" : ANSI_RESET);

        FileList init_list;
        file_list_init(&init_list);
        double init_ms = 0.0;
        options.build_cache = true;
        if (scanner_inspect_directory(&options, &init_list, &init_ms)) {
            cache_save_file_list(&options, &init_list);
        }
        file_list_free(&init_list);

        void* watcher = cache_watcher_start(options.target_dir, NULL);
        if (!watcher) {
            fprintf(stderr, "Error: Failed to start directory watcher.\n");
            free_options(&options);
            return 1;
        }
        while (1) {
            Sleep(1000);
        }
        cache_watcher_stop(watcher);
        free_options(&options);
        return 0;
    }

    double total_start_time = timer_now_ms();

    // ─────────────────────────────────────────────────────────────────────────────
    // STEP 1: PRE-SEARCH INSPECTION & DISCOVERY
    // ─────────────────────────────────────────────────────────────────────────────
    FileList file_list;
    file_list_init(&file_list);

    double inspection_ms = 0.0;
    if (!scanner_inspect_directory(&options, &file_list, &inspection_ms)) {
        fprintf(stderr, "Error: Failed to inspect target directory '%s'\n", options.target_dir);
        file_list_free(&file_list);
        free_options(&options);
        return 2;
    }

    if (options.list_files_only) {
        report_print_file_list(&options, &file_list, inspection_ms);
        int rc = (file_list.count > 0) ? 0 : 1;
        file_list_free(&file_list);
        free_options(&options);
        return rc;
    }

    if (options.build_cache && options.query[0] == '\0' && options.pattern_count == 0 && !options.outline_mode) {
        printf("[INFO] Instant cache built for '%s' (%zu files, %.2f ms).\n",
               options.target_dir, file_list.count, inspection_ms);
        file_list_free(&file_list);
        free_options(&options);
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // STEP 2: EXECUTION PLAN GENERATION
    // ─────────────────────────────────────────────────────────────────────────────
    ExecutionPlan plan = scanner_create_plan(&options, &file_list);
    plan.inspection_time_ms = inspection_ms;

    // Sort files by size (LPT) for load balancing
    scanner_sort_files_for_execution(&file_list);

    // If agent requested plan_only (Dry-run / Inspection)
    if (options.plan_only) {
        if (options.json_stdout) {
            report_print_json_plan(&options, &file_list, &plan, inspection_ms);
        } else {
            report_print_execution_plan(&options, &file_list, &plan, inspection_ms);
        }
        file_list_free(&file_list);
        free_options(&options);
        return 0;
    }

    // Print execution plan to console in human mode
    report_print_execution_plan(&options, &file_list, &plan, inspection_ms);

    if (file_list.count == 0) {
        if (!options.json_stdout && !options.porcelain && !options.files_with_matches && !options.count_only) {
            printf("%sNo matching candidate files found in target directory.%s\n\n",
                options.no_color ? "" : ANSI_YELLOW, options.no_color ? "" : ANSI_RESET);
        } else if (options.json_stdout) {
            SearchReport empty_report;
            memset(&empty_report, 0, sizeof(SearchReport));
            empty_report.options = options;
            empty_report.plan = plan;
            report_print_json_stdout(&empty_report);
        }
        file_list_free(&file_list);
        free_options(&options);
        return 1; // 1 = No matches found
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // STEP 3: PARALLEL SEARCH EXECUTION
    // ─────────────────────────────────────────────────────────────────────────────
    SearchReport report;
    memset(&report, 0, sizeof(SearchReport));
    report.options = options;
    report.plan = plan;
    report.inspection_time_ms = inspection_ms;
    report.total_files_discovered = file_list.count;
    report.total_dirs_discovered = file_list.total_dirs;
    report.total_bytes_discovered = file_list.total_bytes;

    threadpool_execute_search(&options, &plan, &file_list, &report);

    report.total_time_ms = timer_now_ms() - total_start_time;

    // ─────────────────────────────────────────────────────────────────────────────
    // STEP 4: REPORTING & EXPORT
    // ─────────────────────────────────────────────────────────────────────────────
    if (options.json_stdout) {
        report_print_json_stdout(&report);
    } else {
        report_print_matches(&options, &report);
        report_print_summary(&report);
    }

    // File exports if requested
    if (options.output_file[0] != '\0') {
        report_export_text(&report, options.output_file);
    }

    if (options.json_file[0] != '\0') {
        report_export_json(&report, options.json_file);
    }

    int exit_code = (report.total_matches > 0) ? 0 : 1;

    // Clean up
    report_free(&report);
    file_list_free(&file_list);
    free_options(&options);

    return exit_code;
}
