#include "cache.h"
#include "utils.h"
#include "scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#endif

#pragma pack(push, 1)
typedef struct {
    char magic[4];              // 'S', 'S', 'C', 'H'
    uint32_t version;           // 2
    uint64_t filter_hash;       // Hash of traversal filters (no_ignore, hidden, excludes, extensions, search_zip)
    uint64_t file_count;
    uint64_t total_bytes;
    uint64_t total_dirs;
    uint64_t cached_timestamp;
    char root_dir[MAX_PATH];
} CacheHeader;

typedef struct {
    char path[MAX_PATH];
    uint64_t file_size;
    uint64_t last_write_time;
    uint32_t attributes;
} CacheFileRecord;
#pragma pack(pop)

// 64-bit FNV-1a hash of normalized path
static uint64_t hash_path(const char* path) {
    uint64_t h = 14695981039346656037ULL;
    if (!path) return h;
    for (const char* p = path; *p; p++) {
        char c = (char)tolower((unsigned char)*p);
        if (c == '/') c = '\\';
        h ^= (unsigned char)c;
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t compute_filter_hash(const SearchOptions* options) {
    uint64_t h = 14695981039346656037ULL;
    if (!options) return h;
    h ^= (uint64_t)(options->no_ignore ? 1 : 0); h *= 1099511628211ULL;
    h ^= (uint64_t)(options->hidden ? 1 : 0);    h *= 1099511628211ULL;
    h ^= (uint64_t)(options->search_zip ? 1 : 0); h *= 1099511628211ULL;
    for (const char* p = options->excludes; *p; p++) {
        h ^= (unsigned char)tolower((unsigned char)*p);
        h *= 1099511628211ULL;
    }
    h ^= 0xFF; h *= 1099511628211ULL;
    for (const char* p = options->extensions; *p; p++) {
        h ^= (unsigned char)tolower((unsigned char)*p);
        h *= 1099511628211ULL;
    }
    h ^= 0xFE; h *= 1099511628211ULL;
    for (const char* p = options->include_globs; *p; p++) {
        h ^= (unsigned char)tolower((unsigned char)*p);
        h *= 1099511628211ULL;
    }
    h ^= 0xFD; h *= 1099511628211ULL;
    for (const char* p = options->exclude_globs; *p; p++) {
        h ^= (unsigned char)tolower((unsigned char)*p);
        h *= 1099511628211ULL;
    }
    return h;
}

static bool check_cached_filename_match(const char* filepath, const SearchOptions* options) {
    if (!filepath || !options || options->invert_match) return false;
    const char* s1 = strrchr(filepath, '\\');
    const char* s2 = strrchr(filepath, '/');
    const char* base = s1 > s2 ? s1 : s2;
    const char* filename = base ? base + 1 : filepath;

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
    return options->ignore_case ? (str_casestr(filename, options->query) != NULL)
                                : (strstr(filename, options->query) != NULL);
}

static void ensure_cache_directory(char* out_dir, size_t max_len) {
#ifdef _WIN32
    char local_app_data[MAX_PATH] = {0};
    if (GetEnvironmentVariableA("LOCALAPPDATA", local_app_data, MAX_PATH) > 0) {
        snprintf(out_dir, max_len, "%s\\supersearch\\cache", local_app_data);
    } else {
        snprintf(out_dir, max_len, ".supersearch_cache");
    }

    // Recursively create directory
    char tmp[MAX_PATH];
    strncpy(tmp, out_dir, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char* p = tmp + 3; *p; p++) {
        if (*p == '\\' || *p == '/') {
            char old = *p;
            *p = '\0';
            CreateDirectoryA(tmp, NULL);
            *p = old;
        }
    }
    CreateDirectoryA(tmp, NULL);
#else
    const char* xdg = getenv("XDG_CACHE_HOME");
    const char* home = getenv("HOME");
    if (xdg && *xdg) {
        snprintf(out_dir, max_len, "%s/supersearch/cache", xdg);
    } else if (home && *home) {
#ifdef __APPLE__
        snprintf(out_dir, max_len, "%s/Library/Caches/supersearch", home);
#else
        snprintf(out_dir, max_len, "%s/.cache/supersearch", home);
#endif
    } else {
        snprintf(out_dir, max_len, "/tmp/supersearch_cache");
    }

    char tmp[MAX_PATH];
    strncpy(tmp, out_dir, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
#endif
}

static void resolve_canonical_dir(const char* target_dir, char* out_canonical, size_t max_len) {
    if (!target_dir || !out_canonical || max_len == 0) return;
#ifdef _WIN32
    wchar_t wtarget[MAX_PATH] = {0};
    wchar_t wfull[MAX_PATH] = {0};
    if (utf8_to_utf16(target_dir, wtarget, MAX_PATH)) {
        DWORD len = GetFullPathNameW(wtarget, MAX_PATH, wfull, NULL);
        if (len > 0 && len < MAX_PATH) {
            if (utf16_to_utf8(wfull, out_canonical, (int)max_len)) {
                return;
            }
        }
    }
#else
    char resolved[MAX_PATH];
    if (realpath(target_dir, resolved) != NULL) {
        strncpy(out_canonical, resolved, max_len - 1);
        out_canonical[max_len - 1] = '\0';
        return;
    }
#endif
    strncpy(out_canonical, target_dir, max_len - 1);
    out_canonical[max_len - 1] = '\0';
}

bool cache_get_path(const char* target_dir, char* out_cache_path, size_t max_len) {
    if (!target_dir || !out_cache_path) return false;
    char cache_dir[MAX_PATH];
    ensure_cache_directory(cache_dir, sizeof(cache_dir));

    char canonical_dir[MAX_PATH] = {0};
    resolve_canonical_dir(target_dir, canonical_dir, sizeof(canonical_dir));

    uint64_t h = hash_path(canonical_dir);
    snprintf(out_cache_path, max_len, "%s%s%016llx.ssch", cache_dir, SS_PATH_SEP_STR, (unsigned long long)h);
    return true;
}

bool cache_load_file_list(const SearchOptions* options, FileList* list) {
    if (!options || !list) return false;
    const char* target_dir = options->target_dir;
    char canonical_dir[MAX_PATH] = {0};
    resolve_canonical_dir(target_dir, canonical_dir, sizeof(canonical_dir));

    char cache_path[MAX_PATH];
    if (!cache_get_path(canonical_dir, cache_path, sizeof(cache_path))) return false;

    FILE* fp = fopen(cache_path, "rb");
    if (!fp) return false;

    CacheHeader header;
    if (fread(&header, sizeof(CacheHeader), 1, fp) != 1) {
        fclose(fp);
        return false;
    }

    if (memcmp(header.magic, "SSCH", 4) != 0 || header.version != 2) {
        fclose(fp);
        return false;
    }

    // Verify root_dir and traversal filter compatibility
    if (_stricmp(header.root_dir, canonical_dir) != 0 || header.filter_hash != compute_filter_hash(options)) {
        fclose(fp);
        return false;
    }

    // Allocate and load FileList items
    size_t count = (size_t)header.file_count;
    file_list_init(list);

    CacheFileRecord rec;
    for (size_t i = 0; i < count; i++) {
        if (fread(&rec, sizeof(CacheFileRecord), 1, fp) != 1) {
            file_list_free(list);
            fclose(fp);
            return false;
        }
        if (options->max_file_size > 0 && rec.file_size > options->max_file_size) {
            continue;
        }
        wchar_t wpath[MAX_PATH];
        utf8_to_utf16(rec.path, wpath, MAX_PATH);
        bool name_matched = check_cached_filename_match(rec.path, options);
        file_list_add(list, wpath, rec.path, rec.file_size, name_matched);
    }

    list->total_dirs = (size_t)header.total_dirs;

    fclose(fp);
    return true;
}

bool cache_save_file_list(const SearchOptions* options, const FileList* list) {
    if (!options || !list) return false;
    const char* target_dir = options->target_dir;
    char canonical_dir[MAX_PATH] = {0};
    resolve_canonical_dir(target_dir, canonical_dir, sizeof(canonical_dir));

    char cache_path[MAX_PATH];
    if (!cache_get_path(canonical_dir, cache_path, sizeof(cache_path))) return false;

    FILE* fp = fopen(cache_path, "wb");
    if (!fp) return false;

    CacheHeader header;
    memset(&header, 0, sizeof(CacheHeader));
    memcpy(header.magic, "SSCH", 4);
    header.version = 2;
    header.filter_hash = compute_filter_hash(options);
    header.file_count = (uint64_t)list->count;
    header.total_bytes = list->total_bytes;
    header.total_dirs = (uint64_t)list->total_dirs;
    header.cached_timestamp = (uint64_t)timer_now_ms();
    strncpy(header.root_dir, canonical_dir, sizeof(header.root_dir) - 1);

    if (fwrite(&header, sizeof(CacheHeader), 1, fp) != 1) {
        fclose(fp);
        return false;
    }

    for (size_t i = 0; i < list->count; i++) {
        const FileEntry* e = &list->items[i];
        CacheFileRecord rec;
        memset(&rec, 0, sizeof(CacheFileRecord));
        if (e->path_utf8) {
            strncpy(rec.path, e->path_utf8, sizeof(rec.path) - 1);
        }
        rec.file_size = e->file_size;
        rec.last_write_time = 0;
        rec.attributes = 0;

        if (fwrite(&rec, sizeof(CacheFileRecord), 1, fp) != 1) {
            fclose(fp);
            return false;
        }
    }

    fclose(fp);
    return true;
}

bool cache_clear(const char* target_dir) {
    char cache_path[MAX_PATH];
    if (!cache_get_path(target_dir, cache_path, sizeof(cache_path))) return false;
    return (remove(cache_path) == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Windows ReadDirectoryChangesW Watcher Thread
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
typedef struct {
    HANDLE hDir;
    HANDLE hThread;
    HANDLE hStopEvent;
    char target_dir[MAX_PATH];
    FileList* list;
    volatile bool running;
} WatcherContext;

static DWORD WINAPI watcher_thread_func(LPVOID param) {
    WatcherContext* ctx = (WatcherContext*)param;
    BYTE buffer[16 * 1024];
    DWORD bytes_returned = 0;

    OVERLAPPED ov;
    memset(&ov, 0, sizeof(OVERLAPPED));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    while (ctx->running) {
        ResetEvent(ov.hEvent);
        BOOL success = ReadDirectoryChangesW(
            ctx->hDir,
            buffer,
            sizeof(buffer),
            TRUE, // Recursive watch
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
            &bytes_returned,
            &ov,
            NULL
        );

        if (!success) break;

        HANDLE handles[2] = { ctx->hStopEvent, ov.hEvent };
        DWORD wait_res = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (wait_res == WAIT_OBJECT_0) {
            // Stop requested
            CancelIo(ctx->hDir);
            break;
        } else if (wait_res == WAIT_OBJECT_0 + 1) {
            // Directory changed! Refresh in-memory list and update cache
            GetOverlappedResult(ctx->hDir, &ov, &bytes_returned, FALSE);

            // Re-inspect directory in background to keep list fresh
            SearchOptions opts;
            memset(&opts, 0, sizeof(SearchOptions));
            strncpy(opts.target_dir, ctx->target_dir, sizeof(opts.target_dir) - 1);
            strncpy(opts.excludes, ".git,.vs,node_modules,bin,obj", sizeof(opts.excludes) - 1);
            utf8_to_utf16(opts.target_dir, opts.target_dir_w, MAX_PATH);
            opts.no_color = true;

            FileList new_list;
            file_list_init(&new_list);
            double dummy_ms = 0;
            if (scanner_inspect_directory(&opts, &new_list, &dummy_ms)) {
                cache_save_file_list(&opts, &new_list);
                file_list_free(&new_list);
            }
        }
    }

    CloseHandle(ov.hEvent);
    return 0;
}

void* cache_watcher_start(const char* target_dir, FileList* list) {
    wchar_t wdir[MAX_PATH];
    utf8_to_utf16(target_dir, wdir, MAX_PATH);

    HANDLE hDir = CreateFileW(
        wdir,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (hDir == INVALID_HANDLE_VALUE) return NULL;

    WatcherContext* ctx = (WatcherContext*)safe_malloc(sizeof(WatcherContext));
    ctx->hDir = hDir;
    ctx->hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    strncpy(ctx->target_dir, target_dir, sizeof(ctx->target_dir) - 1);
    ctx->list = list;
    ctx->running = true;

    ctx->hThread = CreateThread(NULL, 0, watcher_thread_func, ctx, 0, NULL);
    if (!ctx->hThread) {
        CloseHandle(ctx->hStopEvent);
        CloseHandle(ctx->hDir);
        free(ctx);
        return NULL;
    }

    return (void*)ctx;
}

void cache_watcher_stop(void* watcher_handle) {
    if (!watcher_handle) return;
    WatcherContext* ctx = (WatcherContext*)watcher_handle;
    ctx->running = false;
    SetEvent(ctx->hStopEvent);

    if (ctx->hThread) {
        WaitForSingleObject(ctx->hThread, 2000);
        CloseHandle(ctx->hThread);
    }
    if (ctx->hDir != INVALID_HANDLE_VALUE) {
        CloseHandle(ctx->hDir);
    }
    if (ctx->hStopEvent) {
        CloseHandle(ctx->hStopEvent);
    }
    free(ctx);
}
#else
typedef struct {
    pthread_t thread;
    char target_dir[MAX_PATH];
    volatile bool running;
} PosixWatcherContext;

static void* posix_watcher_thread_func(void* param) {
    PosixWatcherContext* ctx = (PosixWatcherContext*)param;
    while (ctx->running) {
        usleep(2000 * 1000); // Poll every 2 seconds on POSIX
        if (!ctx->running) break;

        SearchOptions opts;
        memset(&opts, 0, sizeof(SearchOptions));
        strncpy(opts.target_dir, ctx->target_dir, sizeof(opts.target_dir) - 1);
        strncpy(opts.excludes, ".git,.vs,node_modules,bin,obj", sizeof(opts.excludes) - 1);
        opts.no_color = true;
        opts.build_cache = true;

        FileList new_list;
        file_list_init(&new_list);
        double dummy_ms = 0;
        if (scanner_inspect_directory(&opts, &new_list, &dummy_ms)) {
            cache_save_file_list(&opts, &new_list);
            file_list_free(&new_list);
        }
    }
    return NULL;
}

void* cache_watcher_start(const char* target_dir, FileList* list) {
    (void)list;
    if (!target_dir || !utils_path_exists(target_dir)) return NULL;
    PosixWatcherContext* ctx = (PosixWatcherContext*)safe_malloc(sizeof(PosixWatcherContext));
    strncpy(ctx->target_dir, target_dir, sizeof(ctx->target_dir) - 1);
    ctx->target_dir[sizeof(ctx->target_dir) - 1] = '\0';
    ctx->running = true;
    if (pthread_create(&ctx->thread, NULL, posix_watcher_thread_func, ctx) != 0) {
        free(ctx);
        return NULL;
    }
    return (void*)ctx;
}

void cache_watcher_stop(void* watcher_handle) {
    if (!watcher_handle) return;
    PosixWatcherContext* ctx = (PosixWatcherContext*)watcher_handle;
    ctx->running = false;
    pthread_join(ctx->thread, NULL);
    free(ctx);
}
#endif
