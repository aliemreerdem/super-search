#include "scanner.h"
#include "utils.h"
#include "ignore.h"
#include "archive.h"
#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#endif

void file_list_init(FileList* list) {
    if (!list) return;
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    list->total_bytes = 0;
    list->total_dirs = 0;
    list->filename_matches = 0;
    memset(list->cat_counts, 0, sizeof(list->cat_counts));
    memset(list->cat_bytes, 0, sizeof(list->cat_bytes));
}

void file_list_free(FileList* list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].path_utf8);
        free(list->items[i].path_utf16);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static FileCategory categorize_file(uint64_t size) {
    if (size < 4096) return CAT_TINY;
    if (size < 65536) return CAT_SMALL;
    if (size < 16ULL * 1024 * 1024) return CAT_MEDIUM;
    if (size < 128ULL * 1024 * 1024) return CAT_LARGE;
    return CAT_HUGE;
}

void file_list_add(FileList* list, const wchar_t* path_w, const char* path_utf8, uint64_t size, bool name_match) {
    if (list->count >= list->capacity) {
        size_t new_cap = (list->capacity == 0) ? 1024 : list->capacity * 2;
        list->items = (FileEntry*)safe_realloc(list->items, new_cap * sizeof(FileEntry));
        list->capacity = new_cap;
    }

    FileCategory cat = categorize_file(size);
    FileEntry* entry = &list->items[list->count++];
    entry->path_utf16 = path_w ? safe_wcsdup(path_w) : NULL;
    entry->path_utf8 = safe_strdup(path_utf8);
    entry->file_size = size;
    entry->category = cat;
    entry->name_matched = name_match;
    entry->is_binary = false;
    entry->match_count = 0;

    list->total_bytes += size;
    list->cat_counts[cat]++;
    list->cat_bytes[cat] += size;
    if (name_match) {
        list->filename_matches++;
    }
}

// Check if filename contains search query
static bool check_filename_match(const char* filename, const SearchOptions* options) {
    if (!filename || !options || options->invert_match) return false;

    if (options->pattern_count > 0) {
        for (size_t i = 0; i < options->pattern_count; i++) {
            const char* p = options->patterns[i];
            if (!p || !*p) continue;
            if (options->ignore_case) {
                if (str_casestr(filename, p) != NULL) return true;
            } else {
                if (strstr(filename, p) != NULL) return true;
            }
        }
        return false;
    }

    if (options->query_len == 0) return false;

    if (options->ignore_case) {
        return (str_casestr(filename, options->query) != NULL);
    } else {
        return (strstr(filename, options->query) != NULL);
    }
}

// Fast wildcard glob matcher supporting '*', '**', and '?' (case-insensitive on Windows)
static bool glob_match_pattern(const char* pat, const char* str) {
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (*pat == '\0') return true;
            while (*str) {
                if (glob_match_pattern(pat, str)) return true;
                str++;
            }
            return false;
        } else if (*pat == '?') {
            if (*str == '\0') return false;
            pat++;
            str++;
        } else {
            char c1 = (char)tolower((unsigned char)*pat);
            char c2 = (char)tolower((unsigned char)*str);
            if (c1 == '\\') c1 = '/';
            if (c2 == '\\') c2 = '/';
            if (c1 != c2) return false;
            pat++;
            str++;
        }
    }
    return (*str == '\0');
}

static bool path_matches_glob_list(const char* name_utf8, const char* full_path_utf8, const char* glob_list) {
    if (!glob_list || glob_list[0] == '\0') return false;
    char buf[512];
    strncpy(buf, glob_list, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* saveptr = NULL;
    char* tok = strtok_s(buf, ",", &saveptr);
    while (tok) {
        while (*tok == ' ') tok++;
        // Strip leading "**/" if matching against filename
        const char* clean_tok = tok;
        if (strncmp(clean_tok, "**/", 3) == 0 || strncmp(clean_tok, "**\\", 3) == 0) {
            clean_tok += 3;
        }
        // Strip trailing "/**" for directory matches
        char dir_tok[256];
        strncpy(dir_tok, clean_tok, sizeof(dir_tok) - 1);
        dir_tok[sizeof(dir_tok) - 1] = '\0';
        size_t dlen = strlen(dir_tok);
        if (dlen > 3 && (strcmp(&dir_tok[dlen - 3], "/**") == 0 || strcmp(&dir_tok[dlen - 3], "\\**") == 0)) {
            dir_tok[dlen - 3] = '\0';
        }

        if (glob_match_pattern(dir_tok, name_utf8)) return true;
        if (strchr(dir_tok, '/') || strchr(dir_tok, '\\')) {
            char wrapped[280];
            snprintf(wrapped, sizeof(wrapped), "*%s*", dir_tok);
            if (glob_match_pattern(wrapped, full_path_utf8)) return true;
        }
        tok = strtok_s(NULL, ",", &saveptr);
    }
    return false;
}

static void free_scope_chain(IgnoreScope* scope) {
    while (scope) {
        IgnoreScope* parent = scope->parent;
        ignore_scope_free(scope);
        scope = parent;
    }
}

#ifdef _WIN32
// Ascend directory hierarchy from target_dir to find any parent/root .gitignore or .superignore
static IgnoreScope* init_root_ignore_scope(const wchar_t* target_dir_w, const SearchOptions* options) {
    if (options->no_ignore) return NULL;

    wchar_t abs_path_w[MAX_PATH];
    if (GetFullPathNameW(target_dir_w, MAX_PATH, abs_path_w, NULL) == 0) {
        wcscpy(abs_path_w, target_dir_w);
    }

    wchar_t dirs[16][MAX_PATH];
    int depth = 0;

    wchar_t current[MAX_PATH];
    wcscpy(current, abs_path_w);

    while (depth < 16) {
        size_t len = wcslen(current);
        if (len > 3 && (current[len - 1] == L'\\' || current[len - 1] == L'/')) {
            current[len - 1] = L'\0';
        }

        wchar_t test_path[MAX_PATH + 16];
        swprintf(test_path, MAX_PATH + 16, L"%s\\.gitignore", current);
        DWORD a1 = GetFileAttributesW(test_path);
        bool has_git = (a1 != INVALID_FILE_ATTRIBUTES && !(a1 & FILE_ATTRIBUTE_DIRECTORY));

        swprintf(test_path, MAX_PATH + 16, L"%s\\.superignore", current);
        DWORD a2 = GetFileAttributesW(test_path);
        bool has_super = (a2 != INVALID_FILE_ATTRIBUTES && !(a2 & FILE_ATTRIBUTE_DIRECTORY));

        swprintf(test_path, MAX_PATH + 16, L"%s\\.git", current);
        DWORD a_git = GetFileAttributesW(test_path);
        bool is_git_root = (a_git != INVALID_FILE_ATTRIBUTES);

        if (has_git || has_super) {
            wcscpy(dirs[depth++], current);
        }

        if (is_git_root) {
            break; // Stop at git repository root boundary
        }

        wchar_t* last_slash = wcsrchr(current, L'\\');
        if (!last_slash) last_slash = wcsrchr(current, L'/');
        if (!last_slash || last_slash == current || (last_slash - current <= 2 && current[1] == L':')) {
            break; // Reached drive root
        }
        *last_slash = L'\0';
    }

    IgnoreScope* root_scope = NULL;
    for (int i = depth - 1; i >= 0; i--) {
        char dir_utf8[MAX_PATH * 3];
        utf16_to_utf8(dirs[i], dir_utf8, sizeof(dir_utf8));
        IgnoreScope* scope = ignore_scope_create(root_scope, dir_utf8);

        wchar_t file_path[MAX_PATH + 16];
        swprintf(file_path, MAX_PATH + 16, L"%s\\.gitignore", dirs[i]);
        ignore_scope_load_file(scope, file_path);

        swprintf(file_path, MAX_PATH + 16, L"%s\\.superignore", dirs[i]);
        ignore_scope_load_file(scope, file_path);

        root_scope = scope;
    }

    return root_scope;
}

// Recursive directory crawler using Win32 FindFirstFileExW
static void crawl_directory_recursive(
    const wchar_t* dir_w,
    const SearchOptions* options,
    FileList* list,
    IgnoreScope* current_scope
) {
    wchar_t search_pattern[MAX_PATH + 4];
    size_t dir_len = wcslen(dir_w);
    if (dir_len + 3 >= MAX_PATH) return;

    wcscpy(search_pattern, dir_w);
    if (dir_w[dir_len - 1] != L'\\' && dir_w[dir_len - 1] != L'/') {
        wcscat(search_pattern, L"\\*");
    } else {
        wcscat(search_pattern, L"*");
    }

    // Check if this directory has its own .gitignore or .superignore (if not already loaded)
    IgnoreScope* local_scope = current_scope;
    bool created_local_scope = false;

    char dir_utf8[MAX_PATH * 3];
    utf16_to_utf8(dir_w, dir_utf8, sizeof(dir_utf8));

    if (!options->no_ignore) {
        bool already_loaded = false;
        if (current_scope && current_scope->base_dir_utf8) {
            char norm_current[MAX_PATH * 3];
            strncpy(norm_current, dir_utf8, sizeof(norm_current) - 1);
            norm_current[sizeof(norm_current) - 1] = '\0';
            for (char* p = norm_current; *p; p++) if (*p == '\\') *p = '/';
            size_t nlen = strlen(norm_current);
            if (nlen > 0 && norm_current[nlen - 1] != '/') {
                norm_current[nlen] = '/';
                norm_current[nlen + 1] = '\0';
            }
            if (_stricmp(norm_current, current_scope->base_dir_utf8) == 0) {
                already_loaded = true;
            }
        }

        if (!already_loaded) {
            wchar_t test_path[MAX_PATH + 16];
            swprintf(test_path, MAX_PATH + 16, L"%s\\.gitignore", dir_w);
            DWORD a1 = GetFileAttributesW(test_path);
            bool has_git = (a1 != INVALID_FILE_ATTRIBUTES && !(a1 & FILE_ATTRIBUTE_DIRECTORY));

            swprintf(test_path, MAX_PATH + 16, L"%s\\.superignore", dir_w);
            DWORD a2 = GetFileAttributesW(test_path);
            bool has_super = (a2 != INVALID_FILE_ATTRIBUTES && !(a2 & FILE_ATTRIBUTE_DIRECTORY));

            if (has_git || has_super) {
                local_scope = ignore_scope_create(current_scope, dir_utf8);
                created_local_scope = true;

                if (has_git) {
                    swprintf(test_path, MAX_PATH + 16, L"%s\\.gitignore", dir_w);
                    ignore_scope_load_file(local_scope, test_path);
                }
                if (has_super) {
                    swprintf(test_path, MAX_PATH + 16, L"%s\\.superignore", dir_w);
                    ignore_scope_load_file(local_scope, test_path);
                }
            }
        }
    }

    WIN32_FIND_DATAW find_data;
    // FindExInfoBasic skips querying the 8.3 short name, significantly speeding up directory queries
    HANDLE hFind = FindFirstFileExW(
        search_pattern,
        FindExInfoBasic,
        &find_data,
        FindExSearchNameMatch,
        NULL,
        FIND_FIRST_EX_LARGE_FETCH
    );

    if (hFind == INVALID_HANDLE_VALUE) {
        if (created_local_scope) ignore_scope_free(local_scope);
        return;
    }

    do {
        // Skip '.' and '..'
        if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
            continue;
        }

        // Hidden files/directories filter (unless --hidden)
        if (!options->hidden && find_data.cFileName[0] == '.') {
            continue;
        }

        // Convert current name to UTF-8
        char name_utf8[MAX_PATH * 3];
        utf16_to_utf8(find_data.cFileName, name_utf8, sizeof(name_utf8));

        // Construct full path
        wchar_t full_path_w[MAX_PATH + 4];
        wcscpy(full_path_w, dir_w);
        if (dir_w[dir_len - 1] != L'\\' && dir_w[dir_len - 1] != L'/') {
            wcscat(full_path_w, L"\\");
        }
        wcscat(full_path_w, find_data.cFileName);

        char full_path_utf8[MAX_PATH * 3];
        utf16_to_utf8(full_path_w, full_path_utf8, sizeof(full_path_utf8));

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Check exclusion list
            if (dir_matches_exclude(name_utf8, options->excludes)) {
                continue;
            }
            if (options->exclude_globs[0] != '\0' && path_matches_glob_list(name_utf8, full_path_utf8, options->exclude_globs)) {
                continue;
            }

            // Check hierarchical ignore scope
            if (!options->no_ignore && ignore_scope_is_ignored(local_scope, full_path_utf8, true)) {
                continue;
            }

            list->total_dirs++;

            // Check if directory name itself matches query
            if (check_filename_match(name_utf8, options)) {
                file_list_add(list, full_path_w, full_path_utf8, 0, true);
            }

            // Recurse into subdirectory
            crawl_directory_recursive(full_path_w, options, list, local_scope);
        } else {
            // Check hierarchical ignore scope for files
            if (!options->no_ignore && ignore_scope_is_ignored(local_scope, full_path_utf8, false)) {
                continue;
            }
            if (options->exclude_globs[0] != '\0' && path_matches_glob_list(name_utf8, full_path_utf8, options->exclude_globs)) {
                continue;
            }
            if (options->include_globs[0] != '\0' && !path_matches_glob_list(name_utf8, full_path_utf8, options->include_globs)) {
                continue;
            }

            // Normal file
            uint64_t file_size = ((uint64_t)find_data.nFileSizeHigh << 32) | find_data.nFileSizeLow;

            // Filter max size if configured
            if (options->max_file_size > 0 && file_size > options->max_file_size) {
                continue;
            }

            // Filter extension
            if (!file_matches_extension(name_utf8, options->extensions)) {
                if (!options->search_zip || !archive_is_supported_file(name_utf8, NULL, 0)) {
                    continue;
                }
            }

            bool name_matched = check_filename_match(name_utf8, options);
            file_list_add(list, full_path_w, full_path_utf8, file_size, name_matched);
        }
    } while (FindNextFileW(hFind, &find_data));

    FindClose(hFind);

    if (created_local_scope) {
        ignore_scope_free(local_scope);
    }
}

bool scanner_inspect_directory(const SearchOptions* options, FileList* list, double* elapsed_ms) {
    if (!options || !list) return false;

    DWORD attrs = GetFileAttributesW(options->target_dir_w);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    double start_time = timer_now_ms();

    // Check if cache should be loaded (Phase 4.4 Instant Cache)
    if (options->use_cache && !options->build_cache) {
        if (cache_load_file_list(options, list)) {
            if (elapsed_ms) *elapsed_ms = timer_now_ms() - start_time;
            return true;
        }
    }

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        IgnoreScope* root_scope = init_root_ignore_scope(options->target_dir_w, options);
        crawl_directory_recursive(options->target_dir_w, options, list, root_scope);
        free_scope_chain(root_scope);
    } else {
        // Single file target support
        HANDLE hFile = CreateFileW(options->target_dir_w, GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER sz;
            if (GetFileSizeEx(hFile, &sz)) {
                bool name_match = check_filename_match(options->target_dir, options);
                file_list_add(list, options->target_dir_w, options->target_dir, (uint64_t)sz.QuadPart, name_match);
            }
            CloseHandle(hFile);
        }
    }

    // Save cache if requested
    if (options->use_cache || options->build_cache) {
        cache_save_file_list(options, list);
    }

    double duration = timer_now_ms() - start_time;

    if (elapsed_ms) *elapsed_ms = duration;
    return true;
}

#else // POSIX (Linux / macOS)

static bool posix_is_regular_file(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static bool posix_path_exists(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

static IgnoreScope* init_root_ignore_scope_posix(const char* target_dir_utf8, const SearchOptions* options) {
    if (options->no_ignore) return NULL;

    char abs_path[MAX_PATH];
    if (!realpath(target_dir_utf8, abs_path)) {
        strncpy(abs_path, target_dir_utf8, sizeof(abs_path) - 1);
        abs_path[sizeof(abs_path) - 1] = '\0';
    }

    char dirs[16][MAX_PATH];
    int depth = 0;

    char current[MAX_PATH];
    strncpy(current, abs_path, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';

    while (depth < 16) {
        size_t len = strlen(current);
        if (len > 1 && current[len - 1] == '/') {
            current[len - 1] = '\0';
        }

        char test_path[MAX_PATH + 32];
        snprintf(test_path, sizeof(test_path), "%s/.gitignore", current);
        bool has_git = posix_is_regular_file(test_path);

        snprintf(test_path, sizeof(test_path), "%s/.superignore", current);
        bool has_super = posix_is_regular_file(test_path);

        snprintf(test_path, sizeof(test_path), "%s/.git", current);
        bool is_git_root = posix_path_exists(test_path);

        if (has_git || has_super) {
            strncpy(dirs[depth], current, MAX_PATH - 1);
            dirs[depth][MAX_PATH - 1] = '\0';
            depth++;
        }

        if (is_git_root) {
            break;
        }

        char* last_slash = strrchr(current, '/');
        if (!last_slash || last_slash == current) {
            break;
        }
        *last_slash = '\0';
    }

    IgnoreScope* root_scope = NULL;
    for (int i = depth - 1; i >= 0; i--) {
        IgnoreScope* scope = ignore_scope_create(root_scope, dirs[i]);

        char file_path[MAX_PATH + 32];
        snprintf(file_path, sizeof(file_path), "%s/.gitignore", dirs[i]);
        ignore_scope_load_file_utf8(scope, file_path);

        snprintf(file_path, sizeof(file_path), "%s/.superignore", dirs[i]);
        ignore_scope_load_file_utf8(scope, file_path);

        root_scope = scope;
    }

    return root_scope;
}

static void crawl_directory_recursive_posix(
    const char* dir_utf8,
    const SearchOptions* options,
    FileList* list,
    IgnoreScope* current_scope
) {
    size_t dir_len = strlen(dir_utf8);
    if (dir_len + 2 >= MAX_PATH) return;

    IgnoreScope* local_scope = current_scope;
    bool created_local_scope = false;

    if (!options->no_ignore) {
        bool already_loaded = false;
        if (current_scope && current_scope->base_dir_utf8) {
            char norm_current[MAX_PATH];
            strncpy(norm_current, dir_utf8, sizeof(norm_current) - 1);
            norm_current[sizeof(norm_current) - 1] = '\0';
            size_t nlen = strlen(norm_current);
            if (nlen > 0 && norm_current[nlen - 1] != '/') {
                norm_current[nlen] = '/';
                norm_current[nlen + 1] = '\0';
            }
            if (strcmp(norm_current, current_scope->base_dir_utf8) == 0) {
                already_loaded = true;
            }
        }

        if (!already_loaded) {
            char test_path[MAX_PATH + 32];
            snprintf(test_path, sizeof(test_path), "%s/.gitignore", dir_utf8);
            bool has_git = posix_is_regular_file(test_path);

            snprintf(test_path, sizeof(test_path), "%s/.superignore", dir_utf8);
            bool has_super = posix_is_regular_file(test_path);

            if (has_git || has_super) {
                local_scope = ignore_scope_create(current_scope, dir_utf8);
                created_local_scope = true;

                if (has_git) {
                    snprintf(test_path, sizeof(test_path), "%s/.gitignore", dir_utf8);
                    ignore_scope_load_file_utf8(local_scope, test_path);
                }
                if (has_super) {
                    snprintf(test_path, sizeof(test_path), "%s/.superignore", dir_utf8);
                    ignore_scope_load_file_utf8(local_scope, test_path);
                }
            }
        }
    }

    DIR* dir = opendir(dir_utf8);
    if (!dir) {
        if (created_local_scope) ignore_scope_free(local_scope);
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* name_utf8 = entry->d_name;
        if (strcmp(name_utf8, ".") == 0 || strcmp(name_utf8, "..") == 0) {
            continue;
        }

        if (!options->hidden && name_utf8[0] == '.') {
            continue;
        }

        char full_path_utf8[MAX_PATH * 2];
        if (dir_len > 0 && (dir_utf8[dir_len - 1] == '/' || dir_utf8[dir_len - 1] == '\\')) {
            snprintf(full_path_utf8, sizeof(full_path_utf8), "%s%s", dir_utf8, name_utf8);
        } else {
            snprintf(full_path_utf8, sizeof(full_path_utf8), "%s/%s", dir_utf8, name_utf8);
        }

        struct stat st;
        if (lstat(full_path_utf8, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (dir_matches_exclude(name_utf8, options->excludes)) {
                continue;
            }
            if (options->exclude_globs[0] != '\0' && path_matches_glob_list(name_utf8, full_path_utf8, options->exclude_globs)) {
                continue;
            }
            if (!options->no_ignore && ignore_scope_is_ignored(local_scope, full_path_utf8, true)) {
                continue;
            }

            list->total_dirs++;

            if (check_filename_match(name_utf8, options)) {
                file_list_add(list, NULL, full_path_utf8, 0, true);
            }

            crawl_directory_recursive_posix(full_path_utf8, options, list, local_scope);
        } else if (S_ISREG(st.st_mode)) {
            if (!options->no_ignore && ignore_scope_is_ignored(local_scope, full_path_utf8, false)) {
                continue;
            }
            if (options->exclude_globs[0] != '\0' && path_matches_glob_list(name_utf8, full_path_utf8, options->exclude_globs)) {
                continue;
            }
            if (options->include_globs[0] != '\0' && !path_matches_glob_list(name_utf8, full_path_utf8, options->include_globs)) {
                continue;
            }

            uint64_t file_size = (uint64_t)st.st_size;
            if (options->max_file_size > 0 && file_size > options->max_file_size) {
                continue;
            }

            if (!file_matches_extension(name_utf8, options->extensions)) {
                if (!options->search_zip || !archive_is_supported_file(name_utf8, NULL, 0)) {
                    continue;
                }
            }

            bool name_matched = check_filename_match(name_utf8, options);
            file_list_add(list, NULL, full_path_utf8, file_size, name_matched);
        }
    }

    closedir(dir);

    if (created_local_scope) {
        ignore_scope_free(local_scope);
    }
}

bool scanner_inspect_directory(const SearchOptions* options, FileList* list, double* elapsed_ms) {
    if (!options || !list) return false;

    struct stat st;
    if (stat(options->target_dir, &st) != 0) {
        return false;
    }

    double start_time = timer_now_ms();

    if (options->use_cache && !options->build_cache) {
        if (cache_load_file_list(options, list)) {
            if (elapsed_ms) *elapsed_ms = timer_now_ms() - start_time;
            return true;
        }
    }

    if (S_ISDIR(st.st_mode)) {
        IgnoreScope* root_scope = init_root_ignore_scope_posix(options->target_dir, options);
        crawl_directory_recursive_posix(options->target_dir, options, list, root_scope);
        free_scope_chain(root_scope);
    } else if (S_ISREG(st.st_mode)) {
        bool name_match = check_filename_match(options->target_dir, options);
        file_list_add(list, NULL, options->target_dir, (uint64_t)st.st_size, name_match);
    }

    if (options->use_cache || options->build_cache) {
        cache_save_file_list(options, list);
    }

    double duration = timer_now_ms() - start_time;
    if (elapsed_ms) *elapsed_ms = duration;
    return true;
}
#endif

ExecutionPlan scanner_create_plan(const SearchOptions* options, FileList* list) {
    ExecutionPlan plan;
    memset(&plan, 0, sizeof(plan));

    plan.logical_cores = get_logical_processor_count();

    // Determine optimal thread count
    if (options->threads > 0) {
        plan.worker_threads = options->threads;
    } else {
        if (list->count == 0) {
            plan.worker_threads = 1;
        } else if (list->count <= 4) {
            plan.worker_threads = (int)list->count;
        } else if (list->count < 32 && list->total_bytes < 512 * 1024) {
            plan.worker_threads = (plan.logical_cores > 4) ? 4 : plan.logical_cores;
        } else {
            plan.worker_threads = plan.logical_cores;
        }
        if (list->count > 0 && plan.worker_threads > (int)list->count) {
            plan.worker_threads = (int)list->count;
        }
    }

    if (plan.worker_threads < 1) plan.worker_threads = 1;
    if (plan.worker_threads > 64) plan.worker_threads = 64;

    // Determine dynamic dispatch chunk size
    if (list->count > 5000) {
        plan.dynamic_chunk_size = 32;
    } else if (list->count > 1000) {
        plan.dynamic_chunk_size = 16;
    } else if (list->count > 100) {
        plan.dynamic_chunk_size = 4;
    } else {
        plan.dynamic_chunk_size = 1;
    }

    // Longest Processing Time First (LPT) scheduling
    plan.sort_strategy = "LPT (Longest Processing Time First)";

    // Determine I/O Strategy description
    if (list->cat_counts[CAT_LARGE] > 0 || list->cat_counts[CAT_HUGE] > 0) {
        plan.io_strategy = "Hybrid (Direct Read <64KB, Memory-Mapped OS Cache >=64KB)";
    } else if (list->cat_counts[CAT_MEDIUM] > 0) {
        plan.io_strategy = "Hybrid (Direct Read <64KB, Memory-Mapped OS Cache >=64KB)";
    } else {
        plan.io_strategy = "High-Speed Stack & Heap Buffered Direct Read";
    }

    // Dynamic hardware SIMD detection via CPUID / ARM64 NEON
    plan.has_avx2 = utils_cpu_supports_avx2();
    if (plan.has_avx2) {
        plan.simd_acceleration = "AVX2 256-bit Dual-Pivot SIMD + Horspool (Hardware Accelerated)";
    } else if (utils_cpu_supports_neon()) {
        plan.simd_acceleration = "ARM NEON 128-bit Dual-Pivot SIMD + Horspool (Hardware Accelerated)";
    } else {
        plan.simd_acceleration = "Boyer-Moore-Horspool Optimized Scalar (Universal Fallback)";
    }

    // Estimated memory footprint: thread stacks (1MB each) + file table + buffer cache
    plan.estimated_memory_mb = (uint64_t)(plan.worker_threads * 2 + (list->count * sizeof(FileEntry)) / (1024 * 1024) + 16);

    return plan;
}

// Comparator for sorting files by size descending (LPT)
static int compare_files_by_size_desc(const void* a, const void* b) {
    const FileEntry* fa = (const FileEntry*)a;
    const FileEntry* fb = (const FileEntry*)b;

    if (fa->file_size > fb->file_size) return -1;
    if (fa->file_size < fb->file_size) return 1;
    return 0;
}

void scanner_sort_files_for_execution(FileList* list) {
    if (!list || list->count <= 1) return;
    qsort(list->items, list->count, sizeof(FileEntry), compare_files_by_size_desc);
}
