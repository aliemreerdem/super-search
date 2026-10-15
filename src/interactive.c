#include "interactive.h"
#include "scanner.h"
#include "threadpool.h"
#include "report.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#define MAX_PREVIEW_LINES 128
#define MAX_LINE_BUF 512

typedef struct {
    char file_path[MAX_PATH];
    int64_t target_line;
    int start_line;
    int line_count;
    char lines[MAX_PREVIEW_LINES][MAX_LINE_BUF];
} PreviewData;

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} ScreenBuffer;

static void sb_init(ScreenBuffer* sb) {
    sb->cap = 256 * 1024; // 256 KB frame buffer
    sb->data = (char*)safe_malloc(sb->cap);
    sb->len = 0;
    sb->data[0] = '\0';
}

static void sb_free(ScreenBuffer* sb) {
    if (sb->data) {
        free(sb->data);
        sb->data = NULL;
    }
    sb->len = 0;
    sb->cap = 0;
}

static void sb_clear(ScreenBuffer* sb) {
    sb->len = 0;
    if (sb->data) sb->data[0] = '\0';
}

static void sb_append(ScreenBuffer* sb, const char* str, size_t str_len) {
    if (sb->len + str_len + 1 >= sb->cap) {
        sb->cap = (sb->cap + str_len + 65536) * 2;
        sb->data = (char*)safe_realloc(sb->data, sb->cap);
    }
    memcpy(sb->data + sb->len, str, str_len);
    sb->len += str_len;
    sb->data[sb->len] = '\0';
}

static void sb_append_str(ScreenBuffer* sb, const char* str) {
    if (!str) return;
    sb_append(sb, str, strlen(str));
}

static void sb_append_spaces(ScreenBuffer* sb, int count) {
    if (count <= 0) return;
    for (int i = 0; i < count; i++) {
        if (sb->len + 2 >= sb->cap) {
            sb->cap *= 2;
            sb->data = (char*)safe_realloc(sb->data, sb->cap);
        }
        sb->data[sb->len++] = ' ';
    }
    sb->data[sb->len] = '\0';
}

static void pop_utf8_char(char* str) {
    size_t len = strlen(str);
    if (len == 0) return;
    size_t i = len - 1;
    while (i > 0 && ((unsigned char)str[i] & 0xC0) == 0x80) {
        i--;
    }
    str[i] = '\0';
}

static const char* get_path_filename(const char* path) {
    if (!path) return "";
    const char* p1 = strrchr(path, '\\');
    const char* p2 = strrchr(path, '/');
    const char* p = p1 > p2 ? p1 : p2;
    return p ? p + 1 : path;
}

static void load_preview(const char* file_path, int64_t target_line, int body_rows, PreviewData* prev) {
    memset(prev, 0, sizeof(PreviewData));
    if (!file_path || file_path[0] == '\0') return;

    strncpy(prev->file_path, file_path, sizeof(prev->file_path) - 1);
    prev->target_line = target_line;

#ifdef _WIN32
    wchar_t wpath[MAX_PATH];
    utf8_to_utf16(file_path, wpath, MAX_PATH);
    FILE* fp = _wfopen(wpath, L"r");
#else
    FILE* fp = fopen(file_path, "r");
#endif
    if (!fp) {
        prev->line_count = 1;
        snprintf(prev->lines[0], MAX_LINE_BUF, "[File unreadable / Access denied]");
        return;
    }

    int half = body_rows / 2;
    int start = (int)(target_line - half);
    if (start < 1) start = 1;
    int end = start + body_rows - 1;
    prev->start_line = start;

    char line_buf[1024];
    int cur_line = 0;
    while (fgets(line_buf, sizeof(line_buf), fp)) {
        cur_line++;
        if (cur_line >= start && cur_line <= end) {
            if (prev->line_count < MAX_PREVIEW_LINES) {
                // Strip \r\n
                size_t l = strlen(line_buf);
                while (l > 0 && (line_buf[l - 1] == '\r' || line_buf[l - 1] == '\n')) {
                    line_buf[--l] = '\0';
                }
                // Replace tabs with 4 spaces for consistent rendering
                char sanitized[MAX_LINE_BUF] = {0};
                size_t sw = 0;
                for (size_t si = 0; si < l && sw + 4 < MAX_LINE_BUF; si++) {
                    if (line_buf[si] == '\t') {
                        sanitized[sw++] = ' ';
                        sanitized[sw++] = ' ';
                        sanitized[sw++] = ' ';
                        sanitized[sw++] = ' ';
                    } else if ((unsigned char)line_buf[si] < 32 && line_buf[si] != ' ') {
                        sanitized[sw++] = ' ';
                    } else {
                        sanitized[sw++] = line_buf[si];
                    }
                }
                sanitized[sw] = '\0';
                strncpy(prev->lines[prev->line_count++], sanitized, MAX_LINE_BUF - 1);
            }
        }
        if (cur_line > end) break;
    }

    fclose(fp);
}

static void run_search(SearchOptions* base_opts, const char* query, FileList* list, SearchReport* report) {
    memset(report, 0, sizeof(SearchReport));
    SearchOptions opts = *base_opts;
    strncpy(opts.query, query, sizeof(opts.query) - 1);
    opts.query_len = (int)strlen(opts.query);
    opts.max_results = 250; // Responsive UI cap
    opts.no_color = true;

    if (opts.query_len == 0) {
        // Query empty: return 0 matches
        report->options = opts;
        return;
    }

    char* pat = safe_strdup(opts.query);
    opts.patterns = (char**)safe_malloc(sizeof(char*));
    opts.patterns[0] = pat;
    opts.pattern_count = 1;

    ExecutionPlan plan = scanner_create_plan(&opts, list);
    report->options = opts;
    report->plan = plan;

    threadpool_execute_search(&opts, &plan, list, report);

    free(opts.patterns[0]);
    free(opts.patterns);
}

int interactive_run(SearchOptions* options) {
    // 1. Target directory validation
    if (options->target_dir[0] == '\0') {
        strcpy(options->target_dir, ".");
    }
    utf8_to_utf16(options->target_dir, options->target_dir_w, MAX_PATH);

    // 2. Perform one-time inspection of target directory
    FileList file_list;
    file_list_init(&file_list);
    double inspect_ms = 0.0;
    if (!scanner_inspect_directory(options, &file_list, &inspect_ms)) {
        fprintf(stderr, "Error: Target directory '%s' cannot be accessed.\n", options->target_dir);
        file_list_free(&file_list);
        return 2;
    }
    scanner_sort_files_for_execution(&file_list);

#ifdef _WIN32
    // 3. Setup Windows console modes
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    bool created_in = false, created_out = false;

    DWORD orig_in_mode = 0, orig_out_mode = 0;
    if (!GetConsoleMode(hIn, &orig_in_mode)) {
        hIn = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hIn != INVALID_HANDLE_VALUE) {
            GetConsoleMode(hIn, &orig_in_mode);
            created_in = true;
        }
    }
    if (!GetConsoleMode(hOut, &orig_out_mode)) {
        hOut = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hOut != INVALID_HANDLE_VALUE) {
            GetConsoleMode(hOut, &orig_out_mode);
            created_out = true;
        }
    }

    if (hIn == INVALID_HANDLE_VALUE || hOut == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Interactive TUI mode requires an interactive console/terminal.\n");
        file_list_free(&file_list);
        return 2;
    }

    // Enable VT processing
    SetConsoleMode(hOut, orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    // Enable raw key input without line echo
    SetConsoleMode(hIn, (orig_in_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT)) | ENABLE_WINDOW_INPUT);

    // Switch to alternate screen buffer, hide hardware cursor
    DWORD written = 0;
    WriteConsoleA(hOut, "\x1b[?1049h\x1b[?25l", 14, &written, NULL);
#else
    // 3. Setup POSIX terminal raw mode
    int tty_fd = open("/dev/tty", O_RDWR);
    bool opened_tty = true;
    if (tty_fd < 0) {
        opened_tty = false;
        if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
            tty_fd = STDIN_FILENO;
        } else {
            fprintf(stderr, "Error: Interactive TUI mode requires an interactive terminal (/dev/tty).\n");
            file_list_free(&file_list);
            return 2;
        }
    }

    struct termios orig_termios, raw_termios;
    if (tcgetattr(tty_fd, &orig_termios) != 0) {
        if (opened_tty) close(tty_fd);
        fprintf(stderr, "Error: Failed to read terminal attributes.\n");
        file_list_free(&file_list);
        return 2;
    }
    raw_termios = orig_termios;
    raw_termios.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw_termios.c_iflag &= ~(IXON | ICRNL);
    raw_termios.c_cc[VMIN] = 1;
    raw_termios.c_cc[VTIME] = 0;
    tcsetattr(tty_fd, TCSAFLUSH, &raw_termios);

    ssize_t unused_w = write(tty_fd, "\x1b[?1049h\x1b[?25l", 14);
    (void)unused_w;
#endif

    // 4. State
    char query[256] = {0};
    if (options->query[0] != '\0') {
        strncpy(query, options->query, sizeof(query) - 1);
    }

    SearchReport report;
    memset(&report, 0, sizeof(SearchReport));
    run_search(options, query, &file_list, &report);

    int selected_idx = 0;
    int scroll_offset = 0;
    PreviewData preview;
    memset(&preview, 0, sizeof(PreviewData));

    ScreenBuffer sb;
    sb_init(&sb);

    bool running = true;
    bool need_render = true;
    bool need_search = false;
    int exit_code = 1;
    char selected_output[MAX_PATH + 1024] = {0};

    int last_selected_idx = -1;

    while (running) {
        // Re-execute search if query or flags changed
        if (need_search) {
            report_free(&report);
            run_search(options, query, &file_list, &report);
            selected_idx = 0;
            scroll_offset = 0;
            last_selected_idx = -1;
            need_search = false;
            need_render = true;
        }

        // Clamp selection
        if (report.all_matches_count > 0) {
            if (selected_idx >= (int)report.all_matches_count) selected_idx = (int)report.all_matches_count - 1;
            if (selected_idx < 0) selected_idx = 0;
        } else {
            selected_idx = 0;
        }

        // Query terminal dimensions
        int cols = 100;
        int rows = 30;
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
            cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        }
#else
        struct winsize ws;
        if (ioctl(tty_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
            cols = ws.ws_col;
            rows = ws.ws_row;
        }
#endif
        if (cols < 60) cols = 60;
        if (rows < 15) rows = 15;
        int body_rows = rows - 5;
        if (body_rows < 5) body_rows = 5;

        // Keep selected in view
        if (selected_idx < scroll_offset) {
            scroll_offset = selected_idx;
        } else if (selected_idx >= scroll_offset + body_rows) {
            scroll_offset = selected_idx - body_rows + 1;
        }

        if (selected_idx != last_selected_idx) {
            if (report.all_matches_count > 0 && selected_idx < (int)report.all_matches_count) {
                const MatchRecord* mr = &report.all_matches[selected_idx];
                load_preview(mr->file_path, mr->line_number, body_rows, &preview);
            } else {
                memset(&preview, 0, sizeof(PreviewData));
            }
            last_selected_idx = selected_idx;
            need_render = true;
        }

        // Render frame
        if (need_render) {
            sb_clear(&sb);
            sb_append_str(&sb, "\x1b[H"); // Cursor home

            // ── Row 1: Header Bar (Inverted Cyan) ───────────────────────────
            sb_append_str(&sb, "\x1b[7m\x1b[1;36m");
            char hdr[512];
            snprintf(hdr, sizeof(hdr), " SUPERSEARCH v%s • LIVE SEARCH | Target: %-20.20s | Files: %zu (%.2f MB) | Matches: %zu ",
                     APP_VERSION, options->target_dir, file_list.count, (double)file_list.total_bytes / (1024.0 * 1024.0),
                     report.all_matches_count);
            sb_append_str(&sb, hdr);
            int hdr_len = (int)strlen(hdr);
            if (cols > hdr_len) sb_append_spaces(&sb, cols - hdr_len);
            sb_append_str(&sb, "\x1b[0m\r\n");

            // ── Row 2: Prompt Line ──────────────────────────────────────────
            sb_append_str(&sb, "\x1b[1;32m > \x1b[0m\x1b[1m");
            sb_append_str(&sb, query);
            sb_append_str(&sb, "\x1b[7m \x1b[0m"); // Block cursor
            int p_len = 3 + (int)strlen(query) + 1;
            if (cols > p_len) sb_append_spaces(&sb, cols - p_len);
            sb_append_str(&sb, "\r\n");

            // ── Row 3: Status / Filter Badges ──────────────────────────────
            sb_append_str(&sb, "\x1b[90m");
            char badges[256];
            snprintf(badges, sizeof(badges), "─── [Case: %s (F2)] [Regex: %s (Tab)] [Archive: %s (F4)] ─",
                     options->ignore_case ? "Insensitive" : "Sensitive",
                     options->is_regex ? "On" : "Off",
                     options->search_zip ? "On" : "Off");
            sb_append_str(&sb, badges);
            int b_len = (int)strlen(badges);
            while (b_len < cols) {
                sb_append_str(&sb, "─");
                b_len++;
            }
            sb_append_str(&sb, "\x1b[0m\r\n");

            // ── Rows 4 to rows - 2: Body Area (Split View) ─────────────────
            int list_w = (cols * 44) / 100;
            if (list_w < 30) list_w = 30;
            int prev_w = cols - list_w - 1;

            for (int r = 0; r < body_rows; r++) {
                int m_idx = scroll_offset + r;
                int left_used = 0;

                // Left Pane: Match List
                if (m_idx < (int)report.all_matches_count) {
                    const MatchRecord* mr = &report.all_matches[m_idx];
                    bool is_sel = (m_idx == selected_idx);

                    if (is_sel) {
                        sb_append_str(&sb, "\x1b[46;30;1m ▶ ");
                        left_used += 3;
                    } else {
                        sb_append_str(&sb, "   ");
                        left_used += 3;
                    }

                    // Format filename (basename or shortened)
                    const char* fname = get_path_filename(mr->file_path);
                    char item_hdr[128];
                    snprintf(item_hdr, sizeof(item_hdr), "%s:%lld ", fname, (long long)mr->line_number);
                    int ih_len = (int)strlen(item_hdr);

                    if (!is_sel) sb_append_str(&sb, "\x1b[36m");
                    sb_append_str(&sb, item_hdr);
                    if (!is_sel) sb_append_str(&sb, "\x1b[0m");
                    left_used += ih_len;

                    // Snippet text
                    int rem_w = list_w - left_used;
                    if (rem_w > 0 && mr->line_text) {
                        const char* snip = mr->line_text;
                        while (*snip && isspace((unsigned char)*snip)) snip++;
                        char snip_buf[256];
                        snprintf(snip_buf, sizeof(snip_buf), "%-.*s", rem_w, snip);
                        int snip_len = (int)strlen(snip_buf);
                        sb_append_str(&sb, snip_buf);
                        left_used += snip_len;
                    }

                    if (is_sel) sb_append_str(&sb, "\x1b[0m");
                } else if (report.all_matches_count == 0 && r == 0) {
                    if (query[0] == '\0') {
                        sb_append_str(&sb, "   \x1b[90mType a search query...\x1b[0m");
                        left_used = 25;
                    } else {
                        sb_append_str(&sb, "   \x1b[33mNo matches found.\x1b[0m");
                        left_used = 20;
                    }
                }

                if (list_w > left_used) sb_append_spaces(&sb, list_w - left_used);

                // Divider Column
                sb_append_str(&sb, "\x1b[90m│\x1b[0m");

                // Right Pane: Preview Line
                int right_used = 0;
                if (preview.line_count > 0 && r < preview.line_count) {
                    int file_line_num = preview.start_line + r;
                    bool is_match_line = (file_line_num == (int)preview.target_line);

                    char line_num_str[32];
                    snprintf(line_num_str, sizeof(line_num_str), " %4d │ ", file_line_num);
                    int ln_len = (int)strlen(line_num_str);

                    if (is_match_line) {
                        sb_append_str(&sb, "\x1b[48;5;236;1;33m");
                        sb_append_str(&sb, line_num_str);
                        sb_append_str(&sb, "\x1b[1;37m");
                    } else {
                        sb_append_str(&sb, "\x1b[90m");
                        sb_append_str(&sb, line_num_str);
                        sb_append_str(&sb, "\x1b[0m");
                    }
                    right_used += ln_len;

                    int max_content_w = prev_w - right_used;
                    if (max_content_w > 0) {
                        const char* pline = preview.lines[r];
                        char line_snip[512];
                        snprintf(line_snip, sizeof(line_snip), "%-.*s", max_content_w, pline);
                        sb_append_str(&sb, line_snip);
                        right_used += (int)strlen(line_snip);
                    }
                    if (is_match_line) sb_append_str(&sb, "\x1b[0m");
                }

                if (prev_w > right_used) sb_append_spaces(&sb, prev_w - right_used);
                sb_append_str(&sb, "\r\n");
            }

            // ── Row rows - 1: Navigation / Keybinding Helper Bar ───────────
            sb_append_str(&sb, "\x1b[7m");
            char nav[512];
            snprintf(nav, sizeof(nav), " [Up/Down] Navigate | [PgUp/PgDn] Page | [ENTER] Select & Exit | [ESC] Cancel | [TAB] Regex | [F2] Case ");
            sb_append_str(&sb, nav);
            int n_len = (int)strlen(nav);
            if (cols > n_len) sb_append_spaces(&sb, cols - n_len);
            sb_append_str(&sb, "\x1b[0m");

            // Flush entire screen frame at once (zero flicker)
#ifdef _WIN32
            WriteConsoleA(hOut, sb.data, (DWORD)sb.len, &written, NULL);
#else
            ssize_t fw = write(tty_fd, sb.data, sb.len);
            (void)fw;
#endif
            need_render = false;
        }

#ifdef _WIN32
        // Wait for key event
        INPUT_RECORD ir[16];
        DWORD num_events = 0;
        if (!ReadConsoleInputW(hIn, ir, 16, &num_events)) {
            break;
        }

        for (DWORD i = 0; i < num_events; i++) {
            if (ir[i].EventType == KEY_EVENT && ir[i].Event.KeyEvent.bKeyDown) {
                WORD vk = ir[i].Event.KeyEvent.wVirtualKeyCode;
                WCHAR ch = ir[i].Event.KeyEvent.uChar.UnicodeChar;
                DWORD ctrl_state = ir[i].Event.KeyEvent.dwControlKeyState;

                // ESC or Ctrl+C: Cancel
                if (vk == VK_ESCAPE || ((ctrl_state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) && (vk == 'C' || vk == 'c'))) {
                    running = false;
                    exit_code = 1;
                    break;
                }

                // ENTER: Select match
                if (vk == VK_RETURN) {
                    if (report.all_matches_count > 0 && selected_idx < (int)report.all_matches_count) {
                        const MatchRecord* mr = &report.all_matches[selected_idx];
                        snprintf(selected_output, sizeof(selected_output), "%s:%lld:%lld:%s",
                                 mr->file_path, (long long)mr->line_number, (long long)mr->col_number,
                                 mr->line_text ? mr->line_text : "");
                        exit_code = 0;
                    }
                    running = false;
                    break;
                }

                // UP / DOWN / PGUP / PGDN
                if (vk == VK_UP) {
                    if (selected_idx > 0) {
                        selected_idx--;
                        need_render = true;
                    }
                } else if (vk == VK_DOWN) {
                    if (selected_idx + 1 < (int)report.all_matches_count) {
                        selected_idx++;
                        need_render = true;
                    }
                } else if (vk == VK_PRIOR) {
                    if (selected_idx > 0) {
                        selected_idx = (selected_idx >= 10) ? selected_idx - 10 : 0;
                        need_render = true;
                    }
                } else if (vk == VK_NEXT) {
                    if (selected_idx + 1 < (int)report.all_matches_count) {
                        selected_idx = (selected_idx + 10 < (int)report.all_matches_count) ? selected_idx + 10 : (int)report.all_matches_count - 1;
                        need_render = true;
                    }
                } else if (vk == VK_TAB) {
                    options->is_regex = !options->is_regex;
                    need_search = true;
                } else if (vk == VK_F2) {
                    options->ignore_case = !options->ignore_case;
                    need_search = true;
                } else if (vk == VK_F4) {
                    options->search_zip = !options->search_zip;
                    need_search = true;
                } else if (vk == VK_BACK) {
                    if (strlen(query) > 0) {
                        pop_utf8_char(query);
                        need_search = true;
                    }
                } else if (ch >= 32 && ch != 127) {
                    // Convert Unicode character to UTF-8
                    char utf8_bytes[8] = {0};
                    int u8_len = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, utf8_bytes, sizeof(utf8_bytes), NULL, NULL);
                    if (u8_len > 0 && strlen(query) + (size_t)u8_len < sizeof(query) - 1) {
                        strncat(query, utf8_bytes, sizeof(query) - strlen(query) - 1);
                        need_search = true;
                    }
                }
            } else if (ir[i].EventType == WINDOW_BUFFER_SIZE_EVENT) {
                need_render = true;
            }
        }
#else
        unsigned char kbuf[8] = {0};
        ssize_t nread = read(tty_fd, kbuf, sizeof(kbuf) - 1);
        if (nread <= 0) break;

        if (kbuf[0] == 3 || (nread == 1 && kbuf[0] == 27)) { // Ctrl+C or bare ESC
            running = false;
            exit_code = 1;
        } else if (kbuf[0] == '\r' || kbuf[0] == '\n') { // ENTER
            if (report.all_matches_count > 0 && selected_idx < (int)report.all_matches_count) {
                const MatchRecord* mr = &report.all_matches[selected_idx];
                snprintf(selected_output, sizeof(selected_output), "%s:%lld:%lld:%s",
                         mr->file_path, (long long)mr->line_number, (long long)mr->col_number,
                         mr->line_text ? mr->line_text : "");
                exit_code = 0;
            }
            running = false;
        } else if (kbuf[0] == '\t') { // TAB
            options->is_regex = !options->is_regex;
            need_search = true;
        } else if (kbuf[0] == 127 || kbuf[0] == 8) { // Backspace
            if (strlen(query) > 0) {
                pop_utf8_char(query);
                need_search = true;
            }
        } else if (kbuf[0] == 27 && nread >= 3) { // ANSI escape sequence
            if (kbuf[1] == '[') {
                if (kbuf[2] == 'A') { // Up
                    if (selected_idx > 0) { selected_idx--; need_render = true; }
                } else if (kbuf[2] == 'B') { // Down
                    if (selected_idx + 1 < (int)report.all_matches_count) { selected_idx++; need_render = true; }
                } else if (kbuf[2] == '5' && kbuf[3] == '~') { // PgUp
                    if (selected_idx > 0) { selected_idx = (selected_idx >= 10) ? selected_idx - 10 : 0; need_render = true; }
                } else if (kbuf[2] == '6' && kbuf[3] == '~') { // PgDn
                    if (selected_idx + 1 < (int)report.all_matches_count) {
                        selected_idx = (selected_idx + 10 < (int)report.all_matches_count) ? selected_idx + 10 : (int)report.all_matches_count - 1;
                        need_render = true;
                    }
                }
            } else if (kbuf[1] == 'O') {
                if (kbuf[2] == 'Q') { // F2
                    options->ignore_case = !options->ignore_case;
                    need_search = true;
                } else if (kbuf[2] == 'S') { // F4
                    options->search_zip = !options->search_zip;
                    need_search = true;
                }
            }
        } else if (kbuf[0] >= 32) {
            if (strlen(query) + (size_t)nread < sizeof(query) - 1) {
                strncat(query, (const char*)kbuf, (size_t)nread);
                need_search = true;
            }
        }
#endif
    }

    // 5. Restore console / terminal
#ifdef _WIN32
    WriteConsoleA(hOut, "\x1b[?25h\x1b[?1049l", 14, &written, NULL);

    SetConsoleMode(hIn, orig_in_mode);
    SetConsoleMode(hOut, orig_out_mode);

    if (created_in) CloseHandle(hIn);
    if (created_out) CloseHandle(hOut);
#else
    ssize_t rw = write(tty_fd, "\x1b[?25h\x1b[?1049l", 14);
    (void)rw;
    tcsetattr(tty_fd, TCSAFLUSH, &orig_termios);
    if (opened_tty) close(tty_fd);
#endif

    // 6. Cleanup
    sb_free(&sb);
    report_free(&report);
    file_list_free(&file_list);

    // If a match was selected, output it to standard output
    if (exit_code == 0 && selected_output[0] != '\0') {
        printf("%s\n", selected_output);
    }

    return exit_code;
}
