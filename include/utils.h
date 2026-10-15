#ifndef UTILS_H
#define UTILS_H

#include "supersearch.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// ANSI color escape sequences
#define ANSI_RESET        "\x1b[0m"
#define ANSI_BOLD         "\x1b[1m"
#define ANSI_DIM          "\x1b[2m"
#define ANSI_ITALIC       "\x1b[3m"
#define ANSI_UNDERLINE    "\x1b[4m"

#define ANSI_BLACK        "\x1b[30m"
#define ANSI_RED          "\x1b[31m"
#define ANSI_GREEN        "\x1b[32m"
#define ANSI_YELLOW       "\x1b[33m"
#define ANSI_BLUE         "\x1b[34m"
#define ANSI_MAGENTA      "\x1b[35m"
#define ANSI_CYAN         "\x1b[36m"
#define ANSI_WHITE        "\x1b[37m"
#define ANSI_GRAY         "\x1b[90m"

#define ANSI_BOLD_RED     "\x1b[1;31m"
#define ANSI_BOLD_GREEN   "\x1b[1;32m"
#define ANSI_BOLD_YELLOW  "\x1b[1;33m"
#define ANSI_BOLD_BLUE    "\x1b[1;34m"
#define ANSI_BOLD_MAGENTA "\x1b[1;35m"
#define ANSI_BOLD_CYAN    "\x1b[1;36m"
#define ANSI_BOLD_WHITE   "\x1b[1;37m"

#define ANSI_BG_RED       "\x1b[41m"
#define ANSI_BG_GREEN     "\x1b[42m"
#define ANSI_BG_YELLOW    "\x1b[43m"
#define ANSI_BG_BLUE      "\x1b[44m"
#define ANSI_BG_MAGENTA   "\x1b[45m"
#define ANSI_BG_CYAN      "\x1b[46m"
#define ANSI_BG_DARK_GRAY "\x1b[100m"

// Terminal & High-res Timer & Hardware Detection
void utils_init_terminal(bool* no_color);
double timer_now_ms(void);
int get_logical_processor_count(void);
bool utils_cpu_supports_avx2(void);
bool utils_cpu_supports_neon(void);
bool utils_path_exists(const char* path);

// String & Unicode Helpers
bool utf8_to_utf16(const char* utf8, wchar_t* utf16, int max_chars);
bool utf16_to_utf8(const wchar_t* utf16, char* utf8, int max_chars);
char* str_casestr(const char* haystack, const char* needle);
bool file_matches_extension(const char* filename, const char* ext_list);
bool dir_matches_exclude(const char* dirname, const char* exclude_list);
char* sanitize_snippet(const char* raw, size_t len, int* out_len);
char* sanitize_snippet_ex(const char* raw, size_t len, bool multiline, int* out_len);
char* utils_unescape_query(const char* input);
bool utils_resolve_type_extensions(const char* type_str, char* out_exts, size_t max_len);
void utils_print_type_list(void);

// Formatting Helpers
void format_bytes(uint64_t bytes, char* buffer, size_t buf_size);
void format_number(uint64_t num, char* buffer, size_t buf_size);
void format_duration(double ms, char* buffer, size_t buf_size);

// Memory & Safety
void* safe_malloc(size_t size);
void* safe_calloc(size_t num, size_t size);
void* safe_realloc(void* ptr, size_t size);
char* safe_strdup(const char* str);
wchar_t* safe_wcsdup(const wchar_t* str);

#endif // UTILS_H
