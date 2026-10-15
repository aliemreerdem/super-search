#include "mcp_server.h"
#include "scanner.h"
#include "threadpool.h"
#include "report.h"
#include "semantic.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define MAX_JSON_LINE (1024 * 1024)

// Simple JSON string value finder
static bool json_get_string(const char* json, const char* key, char* out_val, size_t max_len) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return false;

    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    if (*p != '"') return false;
    p++; // Skip opening quote

    size_t w = 0;
    while (*p && *p != '"' && w + 1 < max_len) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            if (*p == 'n') out_val[w++] = '\n';
            else if (*p == 'r') out_val[w++] = '\r';
            else if (*p == 't') out_val[w++] = '\t';
            else if (*p == '\\') out_val[w++] = '\\';
            else if (*p == '"') out_val[w++] = '"';
            else out_val[w++] = *p;
            p++;
        } else {
            out_val[w++] = *p++;
        }
    }
    out_val[w] = '\0';
    return true;
}

// Simple JSON boolean value finder
static bool json_get_bool(const char* json, const char* key, bool default_val) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return default_val;

    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    if (strncmp(p, "true", 4) == 0) return true;
    if (strncmp(p, "false", 5) == 0) return false;
    return default_val;
}

// Simple JSON integer value finder
static int json_get_int(const char* json, const char* key, int default_val) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return default_val;

    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    if (*p == '-' || isdigit((unsigned char)*p)) {
        return atoi(p);
    }
    return default_val;
}

// Extract JSON-RPC ID (can be number or string or null)
static void json_get_id(const char* json, char* out_id, size_t max_len) {
    const char* p = strstr(json, "\"id\"");
    if (!p) {
        strncpy(out_id, "null", max_len - 1);
        out_id[max_len - 1] = '\0';
        return;
    }
    p += 4;
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;

    if (*p == '"') {
        p++;
        size_t w = 0;
        out_id[w++] = '"';
        while (*p && *p != '"' && w + 2 < max_len) {
            out_id[w++] = *p++;
        }
        out_id[w++] = '"';
        out_id[w] = '\0';
    } else {
        size_t w = 0;
        while (*p && (isdigit((unsigned char)*p) || *p == '-') && w + 1 < max_len) {
            out_id[w++] = *p++;
        }
        out_id[w] = '\0';
        if (w == 0) strncpy(out_id, "null", max_len - 1);
    }
}

static void send_initialize_response(const char* id_str) {
    printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{"
           "\"protocolVersion\":\"2024-11-05\","
           "\"capabilities\":{\"tools\":{}},"
           "\"serverInfo\":{\"name\":\"supersearch\",\"version\":\"%s\"}"
           "}}\n", id_str, APP_VERSION);
    fflush(stdout);
}

static void send_tools_list_response(const char* id_str) {
    printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"tools\":[", id_str);

    // 1. supersearch (Primary search tool)
    printf("{"
           "\"name\":\"supersearch\","
           "\"description\":\"Ultra-fast parallel file, code, and archive search engine with AVX2 SIMD acceleration, regex, semantic filtering, and enclosing function attribution.\","
           "\"inputSchema\":{"
           "\"type\":\"object\","
           "\"properties\":{"
           "\"directory\":{\"type\":\"string\",\"description\":\"Root directory or file path to search (default: '.')\"},"
           "\"query\":{\"type\":\"string\",\"description\":\"Search keyword, phrase, or regular expression\"},"
           "\"ignore_case\":{\"type\":\"boolean\",\"description\":\"Case-insensitive search (default: false)\"},"
           "\"smart_case\":{\"type\":\"boolean\",\"description\":\"Case-insensitive unless query contains uppercase letters (default: false)\"},"
           "\"word_regexp\":{\"type\":\"boolean\",\"description\":\"Match whole words only (default: false)\"},"
           "\"invert_match\":{\"type\":\"boolean\",\"description\":\"Select non-matching lines (default: false)\"},"
           "\"regex\":{\"type\":\"boolean\",\"description\":\"Enable regular expression mode (default: false)\"},"
           "\"fixed_strings\":{\"type\":\"boolean\",\"description\":\"Treat query as fixed literal string (default: false)\"},"
           "\"multiline\":{\"type\":\"boolean\",\"description\":\"Match across newline boundaries (default: false)\"},"
           "\"search_zip\":{\"type\":\"boolean\",\"description\":\"Search inside .zip, .gz, .tar.gz archives in-memory (default: false)\"},"
           "\"kind\":{\"type\":\"string\",\"description\":\"Semantic filter: 'comment', 'string', 'code', or 'function'\"},"
           "\"enclosing_function\":{\"type\":\"boolean\",\"description\":\"Attach the enclosing function/class/struct signature and line number to each match (default: false)\"},"
           "\"files_with_matches\":{\"type\":\"boolean\",\"description\":\"Return at most one match per file to quickly identify matching files (default: false)\"},"
           "\"count_only\":{\"type\":\"boolean\",\"description\":\"Count matches without needing full context (default: false)\"},"
           "\"glob\":{\"type\":\"string\",\"description\":\"Comma-separated glob filters (prefix with '!' to exclude, e.g. 'src/*.c,!*test*')\"},"
           "\"extensions\":{\"type\":\"string\",\"description\":\"Comma-separated extension filter (e.g. '.c,.h,.ts')\"},"
           "\"type\":{\"type\":\"string\",\"description\":\"Language preset filter (e.g. 'c', 'rust', 'py', 'ts', 'web')\"},"
           "\"exclude\":{\"type\":\"string\",\"description\":\"Comma-separated directories to skip\"},"
           "\"max_results\":{\"type\":\"integer\",\"description\":\"Maximum total matches to return before truncation\"},"
           "\"max_tokens\":{\"type\":\"integer\",\"description\":\"Approximate LLM token budget cap (e.g. 2000)\"},"
           "\"context\":{\"type\":\"integer\",\"description\":\"Number of lines of code context before and after match\"},"
           "\"use_cache\":{\"type\":\"boolean\",\"description\":\"Use persistent directory index cache for sub-millisecond discovery (default: false)\"}"
           "},"
           "\"required\":[\"query\"]"
           "}"
           "},");

    // 2. supersearch_files (Fast file tree discovery)
    printf("{"
           "\"name\":\"supersearch_files\","
           "\"description\":\"Ultra-fast parallel file discovery (like fd or rg --files) respecting .gitignore, globs, and language types without reading file contents.\","
           "\"inputSchema\":{"
           "\"type\":\"object\","
           "\"properties\":{"
           "\"directory\":{\"type\":\"string\",\"description\":\"Target directory to list files from (default: '.')\"},"
           "\"glob\":{\"type\":\"string\",\"description\":\"Comma-separated glob filters (prefix with '!' to exclude, e.g. '*.c,!*test*')\"},"
           "\"extensions\":{\"type\":\"string\",\"description\":\"Comma-separated extension filter (e.g. '.c,.h')\"},"
           "\"type\":{\"type\":\"string\",\"description\":\"Language preset filter (e.g. 'c', 'py', 'ts')\"},"
           "\"exclude\":{\"type\":\"string\",\"description\":\"Comma-separated directory names to exclude\"},"
           "\"hidden\":{\"type\":\"boolean\",\"description\":\"Include hidden files and directories (default: false)\"},"
           "\"no_ignore\":{\"type\":\"boolean\",\"description\":\"Do not respect .gitignore/.superignore (default: false)\"},"
           "\"max_results\":{\"type\":\"integer\",\"description\":\"Maximum number of file paths to return\"}"
           "}"
           "}"
           "},");

    // 3. supersearch_outline (Structural code symbols & outline)
    printf("{"
           "\"name\":\"supersearch_outline\","
           "\"description\":\"Extract all function, method, class, struct, and interface definitions from a file or directory to understand code structure with minimal tokens.\","
           "\"inputSchema\":{"
           "\"type\":\"object\","
           "\"properties\":{"
           "\"directory\":{\"type\":\"string\",\"description\":\"Target file or directory path (default: '.')\"},"
           "\"glob\":{\"type\":\"string\",\"description\":\"Comma-separated glob filters (e.g. 'src/*.c')\"},"
           "\"extensions\":{\"type\":\"string\",\"description\":\"Comma-separated extension filter\"},"
           "\"type\":{\"type\":\"string\",\"description\":\"Language preset filter (e.g. 'c', 'py', 'ts', 'rust')\"},"
           "\"max_results\":{\"type\":\"integer\",\"description\":\"Maximum number of symbol definitions to return\"},"
           "\"max_tokens\":{\"type\":\"integer\",\"description\":\"Approximate LLM token budget cap\"}"
           "},"
           "\"required\":[\"directory\"]"
           "}"
           "},");

    // 4. supersearch_replace (Search & Replace / Refactoring)
    printf("{"
           "\"name\":\"supersearch_replace\","
           "\"description\":\"High-speed parallel Search & Replace with dry-run preview, Git unified diff generation, or atomic in-place modification.\","
           "\"inputSchema\":{"
           "\"type\":\"object\","
           "\"properties\":{"
           "\"directory\":{\"type\":\"string\",\"description\":\"Target directory or file path\"},"
           "\"query\":{\"type\":\"string\",\"description\":\"Pattern or literal string to find\"},"
           "\"replace\":{\"type\":\"string\",\"description\":\"Replacement text (supports $1..$9 capture groups in regex mode)\"},"
           "\"in_place\":{\"type\":\"boolean\",\"description\":\"Write changes to disk (default: false for safe dry-run preview)\"},"
           "\"backup\":{\"type\":\"boolean\",\"description\":\"Create .bak backup files before modifying (default: false)\"},"
           "\"regex\":{\"type\":\"boolean\",\"description\":\"Enable regex matching and capture group substitution (default: false)\"},"
           "\"ignore_case\":{\"type\":\"boolean\",\"description\":\"Case-insensitive search (default: false)\"},"
           "\"word_regexp\":{\"type\":\"boolean\",\"description\":\"Match whole words only (default: false)\"},"
           "\"glob\":{\"type\":\"string\",\"description\":\"Comma-separated glob filters\"},"
           "\"extensions\":{\"type\":\"string\",\"description\":\"Comma-separated extension filter\"},"
           "\"type\":{\"type\":\"string\",\"description\":\"Language preset filter\"}"
           "},"
           "\"required\":[\"directory\",\"query\",\"replace\"]"
           "}"
           "},");

    // 5. supersearch_plan (Pre-search repository inspection & execution plan)
    printf("{"
           "\"name\":\"supersearch_plan\","
           "\"description\":\"Inspect a repository or directory and return file counts, data volume, size categories, and optimal parallel execution plan without scanning file contents.\","
           "\"inputSchema\":{"
           "\"type\":\"object\","
           "\"properties\":{"
           "\"directory\":{\"type\":\"string\",\"description\":\"Target directory to inspect (default: '.')\"},"
           "\"query\":{\"type\":\"string\",\"description\":\"Optional query to check filename matches\"},"
           "\"glob\":{\"type\":\"string\",\"description\":\"Comma-separated glob filters\"},"
           "\"extensions\":{\"type\":\"string\",\"description\":\"Comma-separated extension filter\"},"
           "\"type\":{\"type\":\"string\",\"description\":\"Language preset filter\"}"
           "}"
           "}"
           "}");

    printf("]}}\n");
    fflush(stdout);
}

static void send_ping_response(const char* id_str) {
    printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{}}\n", id_str);
    fflush(stdout);
}

static void parse_mcp_globs(SearchOptions* options, const char* glob_str) {
    if (!glob_str || !*glob_str) return;
    char buf[1024];
    strncpy(buf, glob_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* ctx = NULL;
    char* tok = strtok_s(buf, ",", &ctx);
    while (tok) {
        while (*tok == ' ' || *tok == '\t') tok++;
        if (*tok == '!') {
            tok++;
            if (*tok) {
                if (options->exclude_globs[0]) strncat(options->exclude_globs, ",", sizeof(options->exclude_globs) - strlen(options->exclude_globs) - 1);
                strncat(options->exclude_globs, tok, sizeof(options->exclude_globs) - strlen(options->exclude_globs) - 1);
            }
        } else if (*tok) {
            if (options->include_globs[0]) strncat(options->include_globs, ",", sizeof(options->include_globs) - strlen(options->include_globs) - 1);
            strncat(options->include_globs, tok, sizeof(options->include_globs) - strlen(options->include_globs) - 1);
        }
        tok = strtok_s(NULL, ",", &ctx);
    }
}

static FILE* open_ram_temp_stream(void) {
#if defined(_WIN32)
    char temp_path[MAX_PATH];
    char temp_file[MAX_PATH];
    if (GetTempPathA(MAX_PATH, temp_path) > 0 &&
        GetTempFileNameA(temp_path, "ss_mcp_", 0, temp_file) != 0) {
        HANDLE hFile = CreateFileA(
            temp_file,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
            NULL
        );
        if (hFile != INVALID_HANDLE_VALUE) {
            int fd = _open_osfhandle((intptr_t)hFile, _O_RDWR | _O_TEXT);
            if (fd != -1) {
                FILE* fp = _fdopen(fd, "w+");
                if (fp) return fp;
                _close(fd);
            } else {
                CloseHandle(hFile);
            }
        }
    }
    return tmpfile();
#else
    return tmpfile();
#endif
}

static void emit_mcp_stream_result(const char* id_str, FILE* mem_fp) {
    printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"content\":[{\"type\":\"text\",\"text\":", id_str);
    if (mem_fp) {
        fflush(mem_fp);
        rewind(mem_fp);
        fputc('"', stdout);
        int ch;
        while ((ch = fgetc(mem_fp)) != EOF) {
            if (ch == '"') fputs("\\\"", stdout);
            else if (ch == '\\') fputs("\\\\", stdout);
            else if (ch == '\n') fputs("\\n", stdout);
            else if (ch == '\r') fputs("\\r", stdout);
            else if (ch == '\t') fputs("\\t", stdout);
            else if ((unsigned char)ch < 32) fprintf(stdout, "\\u%04x", (unsigned char)ch);
            else fputc(ch, stdout);
        }
        fputc('"', stdout);
    } else {
        fputs("\"{}\"", stdout);
    }
    printf("}],\"isError\":false}}\n");
    fflush(stdout);
}

// Execute tool call and return JSON response
static void handle_tools_call(const char* id_str, const char* json_line) {
    SearchOptions options;
    memset(&options, 0, sizeof(SearchOptions));
    options.no_color = true;
    options.json_stdout = true;
    options.max_matches_per_file = 500;

    char tool_name[64] = "supersearch";
    const char* params_pos = strstr(json_line, "\"params\"");
    if (params_pos) {
        json_get_string(params_pos, "name", tool_name, sizeof(tool_name));
    }

    const char* args_json = strstr(json_line, "\"arguments\"");
    if (!args_json) args_json = json_line;

    if (strcmp(tool_name, "supersearch_files") == 0) {
        options.list_files_only = true;
    } else if (strcmp(tool_name, "supersearch_outline") == 0) {
        options.outline_mode = true;
    } else if (strcmp(tool_name, "supersearch_plan") == 0) {
        options.plan_only = true;
    }

    if (!json_get_string(args_json, "directory", options.target_dir, sizeof(options.target_dir)) ||
        options.target_dir[0] == '\0') {
        strcpy(options.target_dir, ".");
    }

    json_get_string(args_json, "query", options.query, sizeof(options.query));
    if (options.outline_mode && options.query[0] == '\0') {
        strcpy(options.query, "[OUTLINE]");
    }

    bool requires_query = (!options.list_files_only && !options.outline_mode && !options.plan_only);
    if (requires_query && options.query[0] == '\0') {
        printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":{\"code\":-32602,\"message\":\"Missing required parameter 'query'\"}}\n", id_str);
        fflush(stdout);
        return;
    }

    // Parse optional arguments
    options.ignore_case = json_get_bool(args_json, "ignore_case", false);
    options.smart_case = json_get_bool(args_json, "smart_case", false);
    options.word_regexp = json_get_bool(args_json, "word_regexp", false);
    options.invert_match = json_get_bool(args_json, "invert_match", false);
    options.is_regex = json_get_bool(args_json, "regex", false);
    options.fixed_strings = json_get_bool(args_json, "fixed_strings", false);
    options.multiline = json_get_bool(args_json, "multiline", false);
    options.search_zip = json_get_bool(args_json, "search_zip", false);
    options.diff_output = json_get_bool(args_json, "diff", false);
    options.in_place = json_get_bool(args_json, "in_place", false);
    options.backup = json_get_bool(args_json, "backup", false);
    options.enclosing_function = json_get_bool(args_json, "enclosing_function", false);
    options.files_with_matches = json_get_bool(args_json, "files_with_matches", false);
    options.count_only = json_get_bool(args_json, "count_only", false);
    options.hidden = json_get_bool(args_json, "hidden", false);
    options.no_ignore = json_get_bool(args_json, "no_ignore", false);
    options.use_cache = json_get_bool(args_json, "use_cache", false);
    if (json_get_bool(args_json, "plan_only", false)) options.plan_only = true;

    char kind_str[32] = {0};
    if (json_get_string(args_json, "kind", kind_str, sizeof(kind_str)) && kind_str[0]) {
        options.kind_filter = (int)semantic_parse_kind(kind_str);
    }

    char glob_str[512] = {0};
    if (json_get_string(args_json, "glob", glob_str, sizeof(glob_str)) && glob_str[0]) {
        parse_mcp_globs(&options, glob_str);
    }

    char repl[512] = {0};
    if (json_get_string(args_json, "replace", repl, sizeof(repl)) || strcmp(tool_name, "supersearch_replace") == 0) {
        options.do_replace = true;
        strncpy(options.replace_text, repl, sizeof(options.replace_text) - 1);
        options.replace_len = (int)strlen(options.replace_text);
    }

    char type_str[64] = {0};
    if (json_get_string(args_json, "type", type_str, sizeof(type_str))) {
        utils_resolve_type_extensions(type_str, options.extensions, sizeof(options.extensions));
    }

    char exts[256] = {0};
    if (json_get_string(args_json, "extensions", exts, sizeof(exts))) {
        if (options.extensions[0]) strncat(options.extensions, ",", sizeof(options.extensions) - strlen(options.extensions) - 1);
        strncat(options.extensions, exts, sizeof(options.extensions) - strlen(options.extensions) - 1);
    }

    char excl[512] = {0};
    if (json_get_string(args_json, "exclude", excl, sizeof(excl))) {
        strncpy(options.excludes, excl, sizeof(options.excludes) - 1);
    } else {
        strcpy(options.excludes, ".git,.vs,node_modules,bin,obj");
    }

    int ctx = json_get_int(args_json, "context", 0);
    if (ctx > 0) {
        options.context_before = ctx;
        options.context_after = ctx;
    }
    int max_res = json_get_int(args_json, "max_results", 0);
    if (max_res > 0) {
        options.max_results = (size_t)max_res;
    }
    int max_tok = json_get_int(args_json, "max_tokens", 0);
    if (max_tok > 0) {
        options.max_tokens = (size_t)max_tok;
    }

    if (options.smart_case && !options.ignore_case) {
        bool has_upper = false;
        for (const char* p = options.query; *p; p++) {
            if (isupper((unsigned char)*p)) {
                has_upper = true;
                break;
            }
        }
        if (!has_upper) options.ignore_case = true;
    }

    if (options.word_regexp && !options.outline_mode && options.query[0] != '\0') {
        options.is_regex = true;
        options.fixed_strings = false;
        char wrapped[512];
        snprintf(wrapped, sizeof(wrapped), "\\b%s\\b", options.query);
        strncpy(options.query, wrapped, sizeof(options.query) - 1);
    }

    if (options.files_with_matches) {
        options.max_matches_per_file = 1;
    }

    options.query_len = (int)strlen(options.query);

    if (options.query[0] != '\0' && !options.outline_mode) {
        options.patterns = (char**)safe_malloc(sizeof(char*));
        options.patterns[0] = safe_strdup(options.query);
        options.pattern_count = 1;
    }

    // Convert target dir to UTF-16
    utf8_to_utf16(options.target_dir, options.target_dir_w, MAX_PATH);

    // Execute discovery in-process
    FileList list;
    file_list_init(&list);
    double inspect_ms = 0;
    bool inspect_ok = scanner_inspect_directory(&options, &list, &inspect_ms);

    if (!inspect_ok) {
        file_list_free(&list);
        if (options.patterns) {
            for (size_t i = 0; i < options.pattern_count; i++) free(options.patterns[i]);
            free(options.patterns);
        }
        printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":{\"code\":-32603,\"message\":\"Target directory inaccessible or invalid\"}}\n", id_str);
        fflush(stdout);
        return;
    }

    FILE* mem_fp = open_ram_temp_stream();

    if (options.list_files_only) {
        if (mem_fp) {
            size_t limit = list.count;
            if (options.max_results > 0 && options.max_results < limit) limit = options.max_results;
            fprintf(mem_fp, "{\n  \"files_only\": true,\n  \"directory\": \"");
            for (const char* d = options.target_dir; *d; d++) {
                if (*d == '\\') fputs("\\\\", mem_fp);
                else if (*d == '"') fputs("\\\"", mem_fp);
                else fputc(*d, mem_fp);
            }
            fprintf(mem_fp, "\",\n  \"total_files\": %zu,\n  \"returned_files\": %zu,\n  \"inspection_time_ms\": %.2f,\n  \"files\": [\n",
                    list.count, limit, inspect_ms);
            for (size_t i = 0; i < limit; i++) {
                fprintf(mem_fp, "    \"");
                for (const char* p = list.items[i].path_utf8; *p; p++) {
                    if (*p == '\\') fputs("\\\\", mem_fp);
                    else if (*p == '"') fputs("\\\"", mem_fp);
                    else fputc(*p, mem_fp);
                }
                fprintf(mem_fp, "\"%s\n", (i + 1 < limit) ? "," : "");
            }
            fprintf(mem_fp, "  ]\n}\n");
        }
        emit_mcp_stream_result(id_str, mem_fp);
        if (mem_fp) fclose(mem_fp);
        file_list_free(&list);
        if (options.patterns) {
            for (size_t i = 0; i < options.pattern_count; i++) free(options.patterns[i]);
            free(options.patterns);
        }
        return;
    }

    scanner_sort_files_for_execution(&list);
    ExecutionPlan plan = scanner_create_plan(&options, &list);
    plan.inspection_time_ms = inspect_ms;

    if (options.plan_only) {
        if (mem_fp) {
            fprintf(mem_fp, "{\n  \"plan_only\": true,\n  \"files_discovered\": %zu,\n  \"directories_scanned\": %zu,\n  \"total_bytes\": %llu,\n  \"filename_matches\": %zu,\n  \"inspection_time_ms\": %.2f,\n  \"recommended_workers\": %d,\n  \"dynamic_chunk_size\": %zu\n}\n",
                    list.count, list.total_dirs, (unsigned long long)list.total_bytes, list.filename_matches, inspect_ms, plan.worker_threads, plan.dynamic_chunk_size);
        }
        emit_mcp_stream_result(id_str, mem_fp);
        if (mem_fp) fclose(mem_fp);
        file_list_free(&list);
        if (options.patterns) {
            for (size_t i = 0; i < options.pattern_count; i++) free(options.patterns[i]);
            free(options.patterns);
        }
        return;
    }

    SearchReport report;
    memset(&report, 0, sizeof(SearchReport));
    report.options = options;
    report.plan = plan;
    report.inspection_time_ms = inspect_ms;
    report.total_files_discovered = list.count;
    report.total_dirs_discovered = list.total_dirs;
    report.total_bytes_discovered = list.total_bytes;

    double total_start_time = timer_now_ms();
    threadpool_execute_search(&options, &plan, &list, &report);
    report.total_time_ms = timer_now_ms() - total_start_time;

    if (mem_fp) {
        report_serialize_json_stream(mem_fp, &report);
    }

    emit_mcp_stream_result(id_str, mem_fp);
    if (mem_fp) fclose(mem_fp);

    report_free(&report);
    file_list_free(&list);
    if (options.patterns) {
        for (size_t i = 0; i < options.pattern_count; i++) free(options.patterns[i]);
        free(options.patterns);
    }
}

int mcp_server_run(void) {
    #ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    #endif

    char* line = (char*)safe_malloc(MAX_JSON_LINE);

    while (fgets(line, MAX_JSON_LINE, stdin)) {
        // Strip trailing newlines
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || isspace((unsigned char)line[len - 1]))) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        char id_str[64] = "null";
        json_get_id(line, id_str, sizeof(id_str));

        char method[128] = {0};
        if (!json_get_string(line, "method", method, sizeof(method))) {
            continue;
        }

        if (strcmp(method, "initialize") == 0) {
            send_initialize_response(id_str);
        } else if (strcmp(method, "notifications/initialized") == 0) {
            // Notification: no response needed
        } else if (strcmp(method, "ping") == 0) {
            send_ping_response(id_str);
        } else if (strcmp(method, "tools/list") == 0) {
            send_tools_list_response(id_str);
        } else if (strcmp(method, "tools/call") == 0) {
            handle_tools_call(id_str, line);
        } else {
            // Unknown method
            printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}\n", id_str);
            fflush(stdout);
        }
    }

    free(line);
    return 0;
}
