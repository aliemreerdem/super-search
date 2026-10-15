#include "ignore.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// Fast wildcard matcher supporting '*', '**', and '?' with case-insensitivity on Windows
static bool git_wildcard_match(const char* pat, const char* str) {
    while (*pat && *str) {
        if (pat[0] == '*' && pat[1] == '*') {
            pat += 2;
            if (*pat == '/') pat++;
            if (!*pat) return true; // Trailing '**' matches any remaining path
            while (*str) {
                if (git_wildcard_match(pat, str)) return true;
                str++;
            }
            return git_wildcard_match(pat, str);
        } else if (*pat == '*') {
            pat++;
            if (!*pat) {
                // Single '*' matches up to the next path separator
                while (*str) {
                    if (*str == '/' || *str == '\\') return false;
                    str++;
                }
                return true;
            }
            while (*str && *str != '/' && *str != '\\') {
                if (git_wildcard_match(pat, str)) return true;
                str++;
            }
            return git_wildcard_match(pat, str);
        } else if (*pat == '?') {
            if (*str == '/' || *str == '\\') return false;
            pat++;
            str++;
        } else {
            char c1 = *pat;
            char c2 = *str;
            if (c1 == '\\') c1 = '/';
            if (c2 == '\\') c2 = '/';
            if (tolower((unsigned char)c1) != tolower((unsigned char)c2)) {
                return false;
            }
            pat++;
            str++;
        }
    }

    while (pat[0] == '*' && pat[1] == '*') {
        pat += 2;
        if (*pat == '/') pat++;
    }
    while (*pat == '*') pat++;

    return (*pat == '\0' && *str == '\0');
}

// Normalize path in-place by converting backslashes to forward slashes
static void normalize_slashes(char* path) {
    if (!path) return;
    for (char* p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
}

IgnoreScope* ignore_scope_create(IgnoreScope* parent, const char* dir_utf8) {
    IgnoreScope* scope = (IgnoreScope*)safe_malloc(sizeof(IgnoreScope));
    scope->parent = parent;
    scope->count = 0;
    scope->capacity = 16;
    scope->rules = (IgnoreRule*)safe_malloc(scope->capacity * sizeof(IgnoreRule));

    char norm_dir[MAX_PATH * 3];
    strncpy(norm_dir, dir_utf8 ? dir_utf8 : "", sizeof(norm_dir) - 1);
    norm_dir[sizeof(norm_dir) - 1] = '\0';
    normalize_slashes(norm_dir);

    size_t len = strlen(norm_dir);
    if (len > 0 && norm_dir[len - 1] != '/') {
        if (len + 1 < sizeof(norm_dir)) {
            norm_dir[len] = '/';
            norm_dir[len + 1] = '\0';
            len++;
        }
    }
    scope->base_dir_utf8 = safe_strdup(norm_dir);
    scope->base_dir_len = len;

    return scope;
}

void ignore_scope_free(IgnoreScope* scope) {
    if (!scope) return;
    for (size_t i = 0; i < scope->count; i++) {
        free(scope->rules[i].pattern);
    }
    free(scope->rules);
    free(scope->base_dir_utf8);
    free(scope);
}

void ignore_scope_add_rule(IgnoreScope* scope, const char* raw_line) {
    if (!scope || !raw_line) return;

    // Trim leading whitespace
    const char* p = raw_line;
    while (*p && isspace((unsigned char)*p)) p++;

    // Comments and empty lines
    if (*p == '\0' || *p == '#') return;

    // Trim trailing whitespace
    char buf[512];
    strncpy(buf, p, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    normalize_slashes(buf);

    size_t len = strlen(buf);
    while (len > 0 && isspace((unsigned char)buf[len - 1])) {
        buf[--len] = '\0';
    }
    if (len == 0) return;

    bool is_neg = false;
    char* pat = buf;
    if (*pat == '!') {
        is_neg = true;
        pat++;
        len--;
    }
    if (len == 0) return;

    bool dir_only = false;
    if (buf[len - 1] == '/') {
        dir_only = true;
        buf[--len] = '\0';
    }
    if (len == 0) return;

    bool anchored = false;
    if (*pat == '/') {
        anchored = true;
        pat++;
        len--;
    }
    if (len == 0) return;

    bool match_basename = (strchr(pat, '/') == NULL);

    if (scope->count >= scope->capacity) {
        scope->capacity = (scope->capacity == 0) ? 16 : scope->capacity * 2;
        scope->rules = (IgnoreRule*)safe_realloc(scope->rules, scope->capacity * sizeof(IgnoreRule));
    }

    IgnoreRule* rule = &scope->rules[scope->count++];
    rule->pattern = safe_strdup(pat);
    rule->is_negation = is_neg;
    rule->dir_only = dir_only;
    rule->match_basename = match_basename;
    rule->anchored = anchored;
}

static bool load_ignore_stream(IgnoreScope* scope, FILE* fp) {
    if (!scope || !fp) return false;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0 || sz > 2 * 1024 * 1024) { // Ignore files larger than 2MB
        fclose(fp);
        return false;
    }

    char* data = (char*)safe_malloc(sz + 1);
    size_t bytes_read = fread(data, 1, sz, fp);
    fclose(fp);
    data[bytes_read] = '\0';

    char* line = data;
    for (size_t i = 0; i <= bytes_read; i++) {
        if (data[i] == '\r' || data[i] == '\n' || data[i] == '\0') {
            data[i] = '\0';
            if (*line) {
                ignore_scope_add_rule(scope, line);
            }
            line = &data[i + 1];
        }
    }

    free(data);
    return true;
}

bool ignore_scope_load_file_utf8(IgnoreScope* scope, const char* file_path_utf8) {
    if (!scope || !file_path_utf8) return false;
    FILE* fp = fopen(file_path_utf8, "rb");
    if (!fp) return false;
    return load_ignore_stream(scope, fp);
}

bool ignore_scope_load_file(IgnoreScope* scope, const wchar_t* file_path_w) {
    if (!scope || !file_path_w) return false;
#ifdef _WIN32
    FILE* fp = _wfopen(file_path_w, L"rb");
    if (!fp) return false;
    return load_ignore_stream(scope, fp);
#else
    char path_utf8[MAX_PATH * 3];
    if (!utf16_to_utf8(file_path_w, path_utf8, sizeof(path_utf8))) return false;
    return ignore_scope_load_file_utf8(scope, path_utf8);
#endif
}

bool ignore_scope_is_ignored(const IgnoreScope* scope, const char* path_utf8, bool is_dir) {
    if (!scope || !path_utf8) return false;

    char norm_path[MAX_PATH * 3];
    strncpy(norm_path, path_utf8, sizeof(norm_path) - 1);
    norm_path[sizeof(norm_path) - 1] = '\0';
    normalize_slashes(norm_path);

    // Extract basename (after the last '/')
    const char* basename = strrchr(norm_path, '/');
    basename = basename ? basename + 1 : norm_path;

    for (const IgnoreScope* s = scope; s != NULL; s = s->parent) {
        // Calculate relative path from s->base_dir_utf8
        const char* rel_path = norm_path;
        size_t base_len = s->base_dir_len;

        if (base_len > 0 && strncmp(norm_path, s->base_dir_utf8, base_len) == 0) {
            rel_path = norm_path + base_len;
        }

        // Git precedence: evaluate rules in reverse order (bottom-up within file)
        for (size_t i = s->count; i > 0; i--) {
            const IgnoreRule* r = &s->rules[i - 1];

            // If rule requires directory and current entry is not directory, skip
            if (r->dir_only && !is_dir) {
                continue;
            }

            bool matched = false;
            if (r->match_basename) {
                matched = git_wildcard_match(r->pattern, basename);
            } else if (r->anchored) {
                matched = git_wildcard_match(r->pattern, rel_path);
            } else {
                // Non-anchored pattern containing slashes (e.g. "foo/bar" or "src/*.c")
                if (git_wildcard_match(r->pattern, rel_path)) {
                    matched = true;
                } else {
                    // Check if pattern matches any subdirectory suffix
                    char glob_buf[512];
                    snprintf(glob_buf, sizeof(glob_buf), "**/%s", r->pattern);
                    matched = git_wildcard_match(glob_buf, rel_path);
                }
            }

            if (matched) {
                // If negation, this scope explicitly allows the file
                return !r->is_negation;
            }
        }
    }

    return false;
}
