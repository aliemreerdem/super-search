#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static LARGE_INTEGER g_timer_freq;
static bool g_timer_initialized = false;
#else
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <wchar.h>
#endif

void utils_init_terminal(bool* no_color) {
#ifdef _WIN32
    // Set console codepage to UTF-8 for proper Unicode character output
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (!SetConsoleMode(hOut, dwMode)) {
                // If VT processing cannot be enabled, disable colors unless forced
                if (no_color) *no_color = true;
            }
        }
    }
#else
    if (getenv("NO_COLOR") != NULL) {
        if (no_color) *no_color = true;
    } else {
        const char* term = getenv("TERM");
        if (term && strcmp(term, "dumb") == 0) {
            if (no_color) *no_color = true;
        }
    }
#endif
}

double timer_now_ms(void) {
#ifdef _WIN32
    if (!g_timer_initialized) {
        QueryPerformanceFrequency(&g_timer_freq);
        g_timer_initialized = true;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return ((double)counter.QuadPart * 1000.0) / (double)g_timer_freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1000000.0);
#endif
}

int get_logical_processor_count(void) {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors > 0 ? (int)sysinfo.dwNumberOfProcessors : 1;
#else
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    return (nprocs > 0) ? (int)nprocs : 4;
#endif
}

bool utils_cpu_supports_avx2(void) {
    static int cached_avx2 = -1;
    if (cached_avx2 != -1) {
        return (cached_avx2 == 1);
    }

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_cpu_supports("avx2")) {
        cached_avx2 = 1;
        return true;
    }
#elif defined(_MSC_VER)
    int info[4];
    __cpuidex(info, 0, 0);
    if (info[0] >= 7) {
        __cpuidex(info, 7, 0);
        if ((info[1] & (1 << 5)) != 0) { // EBX bit 5 is AVX2
            cached_avx2 = 1;
            return true;
        }
    }
#endif

#ifdef _WIN32
    if (IsProcessorFeaturePresent(40)) { // 40 = PF_AVX2_INSTRUCTIONS_AVAILABLE
        cached_avx2 = 1;
        return true;
    }
#endif
#endif

    cached_avx2 = 0;
    return false;
}

bool utils_cpu_supports_neon(void) {
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
    return true;
#else
    return false;
#endif
}

bool utils_path_exists(const char* path) {
    if (!path || !*path) return false;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES);
#else
    struct stat st;
    return (stat(path, &st) == 0);
#endif
}

bool utf8_to_utf16(const char* utf8, wchar_t* utf16, int max_chars) {
    if (!utf8 || !utf16 || max_chars <= 0) return false;
#ifdef _WIN32
    int res = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, utf16, max_chars);
    return res > 0;
#else
    size_t res = mbstowcs(utf16, utf8, (size_t)max_chars - 1);
    if (res == (size_t)-1) {
        int i = 0;
        while (utf8[i] && i < max_chars - 1) {
            utf16[i] = (wchar_t)(unsigned char)utf8[i];
            i++;
        }
        utf16[i] = L'\0';
        return true;
    }
    utf16[res] = L'\0';
    return true;
#endif
}

bool utf16_to_utf8(const wchar_t* utf16, char* utf8, int max_chars) {
    if (!utf16 || !utf8 || max_chars <= 0) return false;
#ifdef _WIN32
    int res = WideCharToMultiByte(CP_UTF8, 0, utf16, -1, utf8, max_chars, NULL, NULL);
    return res > 0;
#else
    size_t res = wcstombs(utf8, utf16, (size_t)max_chars - 1);
    if (res == (size_t)-1) {
        int i = 0;
        while (utf16[i] && i < max_chars - 1) {
            utf8[i] = (char)(utf16[i] & 0xFF);
            i++;
        }
        utf8[i] = '\0';
        return true;
    }
    utf8[res] = '\0';
    return true;
#endif
}

char* str_casestr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char*)haystack;

    size_t needle_len = strlen(needle);
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            size_t i = 1;
            for (; i < needle_len; i++) {
                if (!haystack[i]) return NULL;
                if (tolower((unsigned char)haystack[i]) != tolower((unsigned char)needle[i])) {
                    break;
                }
            }
            if (i == needle_len) return (char*)haystack;
        }
    }
    return NULL;
}

bool file_matches_extension(const char* filename, const char* ext_list) {
    if (!ext_list || !*ext_list) return true; // No filter specified -> accept all

    const char* dot = strrchr(filename, '.');

    char ext_copy[512];
    strncpy(ext_copy, ext_list, sizeof(ext_copy) - 1);
    ext_copy[sizeof(ext_copy) - 1] = '\0';

    char* saveptr = NULL;
    char* token = strtok_s(ext_copy, ",; ", &saveptr);
    while (token) {
        if (!dot) {
            // Match whole filename for non-extension files like Makefile, Dockerfile
            if (_stricmp(filename, token) == 0) return true;
        } else {
            const char* target_ext = token;
            if (*token != '.') {
                if (_stricmp(dot + 1, target_ext) == 0) return true;
            } else {
                if (_stricmp(dot, target_ext) == 0) return true;
            }
            if (_stricmp(filename, token) == 0) return true;
        }
        token = strtok_s(NULL, ",; ", &saveptr);
    }
    return false;
}

typedef struct {
    const char* name;
    const char* aliases;
    const char* exts;
    const char* description;
} FileTypePreset;

static const FileTypePreset G_FILE_TYPES[] = {
    {"c",          "h",            ".c,.h",                               "C source and header files"},
    {"cpp",        "c++,cc,cxx",   ".cpp,.cc,.cxx,.hpp,.hxx,.h",         "C++ source and header files"},
    {"cs",         "csharp",       ".cs,.csx",                            "C# source files"},
    {"rust",       "rs",           ".rs",                                 "Rust source files"},
    {"python",     "py",           ".py,.pyw,.pyi",                       "Python source files"},
    {"javascript", "js",           ".js,.jsx,.mjs,.cjs",                  "JavaScript source files"},
    {"typescript", "ts",           ".ts,.tsx,.mts,.cts",                  "TypeScript source files"},
    {"web",        "html,css",     ".html,.htm,.css,.scss,.sass,.less,.js,.jsx,.ts,.tsx,.json,.svg", "Web frontend files"},
    {"go",         "golang",       ".go",                                 "Go source files"},
    {"java",       NULL,           ".java,.jar",                          "Java source files"},
    {"kotlin",     "kt",           ".kt,.kts",                            "Kotlin source files"},
    {"swift",      NULL,           ".swift",                              "Swift source files"},
    {"php",        NULL,           ".php,.phtml",                         "PHP source files"},
    {"ruby",       "rb",           ".rb,.rake",                           "Ruby source files"},
    {"sql",        NULL,           ".sql",                                "SQL database scripts"},
    {"json",       NULL,           ".json",                               "JSON data files"},
    {"yaml",       "yml",          ".yaml,.yml",                          "YAML configuration files"},
    {"xml",        NULL,           ".xml,.xaml,.config,.csproj,.props,.targets,.resx", "XML files"},
    {"markdown",   "md",           ".md,.markdown",                       "Markdown documentation"},
    {"shell",      "sh,bash",      ".sh,.bash,.zsh",                      "Shell scripts"},
    {"powershell", "ps,ps1",       ".ps1,.psm1,.psd1",                    "PowerShell scripts"},
    {"batch",      "bat,cmd",      ".bat,.cmd",                           "Windows Batch scripts"},
    {"cmake",      NULL,           ".cmake,CMakeLists.txt",               "CMake build files"},
    {"make",       NULL,           "Makefile,makefile,.mk",               "Makefiles"},
    {"docker",     NULL,           "Dockerfile,.dockerignore",            "Docker container files"},
    {"toml",       NULL,           ".toml",                               "TOML configuration files"},
    {NULL, NULL, NULL, NULL}
};

static bool matches_preset_alias(const FileTypePreset* preset, const char* name) {
    if (_stricmp(preset->name, name) == 0) return true;
    if (!preset->aliases) return false;

    char buf[128];
    strncpy(buf, preset->aliases, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* saveptr = NULL;
    char* token = strtok_s(buf, ",", &saveptr);
    while (token) {
        while (*token == ' ') token++;
        size_t l = strlen(token);
        while (l > 0 && token[l - 1] == ' ') token[--l] = '\0';

        if (_stricmp(token, name) == 0) return true;
        token = strtok_s(NULL, ",", &saveptr);
    }
    return false;
}

bool utils_resolve_type_extensions(const char* type_str, char* out_exts, size_t max_len) {
    if (!type_str || !out_exts || max_len == 0) return false;

    char input_copy[512];
    strncpy(input_copy, type_str, sizeof(input_copy) - 1);
    input_copy[sizeof(input_copy) - 1] = '\0';

    char* saveptr = NULL;
    char* token = strtok_s(input_copy, ",", &saveptr);
    bool any_matched = false;

    while (token) {
        while (*token == ' ') token++;
        size_t l = strlen(token);
        while (l > 0 && token[l - 1] == ' ') token[--l] = '\0';

        if (l > 0) {
            bool found = false;
            for (int i = 0; G_FILE_TYPES[i].name != NULL; i++) {
                if (matches_preset_alias(&G_FILE_TYPES[i], token)) {
                    found = true;
                    any_matched = true;

                    if (out_exts[0] != '\0') {
                        strncat(out_exts, ",", max_len - strlen(out_exts) - 1);
                    }
                    strncat(out_exts, G_FILE_TYPES[i].exts, max_len - strlen(out_exts) - 1);
                    break;
                }
            }

            if (!found) {
                fprintf(stderr, "Warning: Unknown file type '%s'. Use --type-list to see available types.\n", token);
            }
        }
        token = strtok_s(NULL, ",", &saveptr);
    }

    return any_matched;
}

void utils_print_type_list(void) {
    printf("\n%s┌── SUPERSEARCH : SUPPORTED FILE TYPE PRESETS (--type / -t) ─────────────%s\n", ANSI_BOLD_CYAN, ANSI_RESET);
    printf("%s│%s  %-16s %-28s %s\n", ANSI_BOLD_CYAN, ANSI_RESET, "TYPE (ALIASES)", "EXTENSIONS", "DESCRIPTION");
    printf("%s├───────────────────────────────────────────────────────────────────────────────%s\n", ANSI_BOLD_CYAN, ANSI_RESET);

    for (int i = 0; G_FILE_TYPES[i].name != NULL; i++) {
        char type_label[32];
        if (G_FILE_TYPES[i].aliases) {
            snprintf(type_label, sizeof(type_label), "%s (%s)", G_FILE_TYPES[i].name, G_FILE_TYPES[i].aliases);
        } else {
            snprintf(type_label, sizeof(type_label), "%s", G_FILE_TYPES[i].name);
        }

        printf("%s│%s  %-16s %-28.28s %s\n",
            ANSI_BOLD_CYAN, ANSI_RESET,
            type_label,
            G_FILE_TYPES[i].exts,
            G_FILE_TYPES[i].description);
    }

    printf("%s└───────────────────────────────────────────────────────────────────────────────%s\n\n", ANSI_BOLD_CYAN, ANSI_RESET);
}

bool dir_matches_exclude(const char* dirname, const char* exclude_list) {
    if (!exclude_list || !*exclude_list) return false;

    char excl_copy[512];
    strncpy(excl_copy, exclude_list, sizeof(excl_copy) - 1);
    excl_copy[sizeof(excl_copy) - 1] = '\0';

    char* saveptr = NULL;
    char* token = strtok_s(excl_copy, ",; ", &saveptr);
    while (token) {
        if (_stricmp(dirname, token) == 0) return true;
        token = strtok_s(NULL, ",; ", &saveptr);
    }
    return false;
}

char* sanitize_snippet_ex(const char* raw, size_t len, bool multiline, int* out_len) {
    if (!raw || len == 0) {
        if (out_len) *out_len = 0;
        return safe_strdup("");
    }
    
    // Allocate space for cleaned line
    char* buf = (char*)safe_malloc(len + 1);
    size_t write_idx = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)raw[i];
        if (!multiline) {
            if (c == '\r' || c == '\n') break;
        } else {
            if (c == '\r') {
                if (i + 1 < len && raw[i + 1] == '\n') {
                    i++; // skip \r of \r\n
                }
                buf[write_idx++] = '\n';
                continue;
            }
            if (c == '\n') {
                buf[write_idx++] = '\n';
                continue;
            }
        }

        if (c == '\t') {
            buf[write_idx++] = ' ';
        } else if (c >= 32 || c >= 128) {
            buf[write_idx++] = c;
        } else {
            buf[write_idx++] = ' ';
        }
    }
    buf[write_idx] = '\0';
    if (out_len) *out_len = (int)write_idx;
    return buf;
}

char* sanitize_snippet(const char* raw, size_t len, int* out_len) {
    return sanitize_snippet_ex(raw, len, false, out_len);
}

char* utils_unescape_query(const char* input) {
    if (!input) return NULL;
    size_t len = strlen(input);
    char* buf = (char*)safe_malloc(len + 1);
    size_t write_idx = 0;

    for (size_t i = 0; i < len; i++) {
        if (input[i] == '\\' && i + 1 < len) {
            char next = input[i + 1];
            if (next == 'n') {
                buf[write_idx++] = '\n';
                i++;
            } else if (next == 'r') {
                buf[write_idx++] = '\r';
                i++;
            } else if (next == 't') {
                buf[write_idx++] = '\t';
                i++;
            } else if (next == '\\') {
                buf[write_idx++] = '\\';
                i++;
            } else {
                buf[write_idx++] = input[i];
            }
        } else {
            buf[write_idx++] = input[i];
        }
    }
    buf[write_idx] = '\0';
    return buf;
}

void format_bytes(uint64_t bytes, char* buffer, size_t buf_size) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int unit_idx = 0;
    double dbytes = (double)bytes;

    while (dbytes >= 1024.0 && unit_idx < 4) {
        dbytes /= 1024.0;
        unit_idx++;
    }

    if (unit_idx == 0) {
        snprintf(buffer, buf_size, "%llu %s", (unsigned long long)bytes, units[unit_idx]);
    } else {
        snprintf(buffer, buf_size, "%.2f %s", dbytes, units[unit_idx]);
    }
}

void format_number(uint64_t num, char* buffer, size_t buf_size) {
    char temp[64];
    snprintf(temp, sizeof(temp), "%llu", (unsigned long long)num);
    int len = (int)strlen(temp);
    
    int comma_count = (len - 1) / 3;
    int new_len = len + comma_count;
    if ((size_t)new_len >= buf_size) {
        snprintf(buffer, buf_size, "%s", temp);
        return;
    }

    buffer[new_len] = '\0';
    int src = len - 1;
    int dst = new_len - 1;
    int count = 0;

    while (src >= 0) {
        buffer[dst--] = temp[src--];
        count++;
        if (count == 3 && src >= 0) {
            buffer[dst--] = ',';
            count = 0;
        }
    }
}

void format_duration(double ms, char* buffer, size_t buf_size) {
    if (ms < 1.0) {
        snprintf(buffer, buf_size, "%.2f µs", ms * 1000.0);
    } else if (ms < 1000.0) {
        snprintf(buffer, buf_size, "%.2f ms", ms);
    } else if (ms < 60000.0) {
        snprintf(buffer, buf_size, "%.2f s", ms / 1000.0);
    } else {
        int minutes = (int)(ms / 60000.0);
        double seconds = (ms - (minutes * 60000.0)) / 1000.0;
        snprintf(buffer, buf_size, "%dm %.1fs", minutes, seconds);
    }
}

void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "FATAL: Out of memory attempting to allocate %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void* safe_calloc(size_t num, size_t size) {
    void* ptr = calloc(num, size);
    if (!ptr && num > 0 && size > 0) {
        fprintf(stderr, "FATAL: Out of memory in safe_calloc\n");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void* safe_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "FATAL: Out of memory in safe_realloc (%zu bytes)\n", size);
        exit(EXIT_FAILURE);
    }
    return new_ptr;
}

char* safe_strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* copy = (char*)safe_malloc(len);
    memcpy(copy, str, len);
    return copy;
}

wchar_t* safe_wcsdup(const wchar_t* str) {
    if (!str) return NULL;
    size_t len = wcslen(str) + 1;
    wchar_t* copy = (wchar_t*)safe_malloc(len * sizeof(wchar_t));
    memcpy(copy, str, len * sizeof(wchar_t));
    return copy;
}
