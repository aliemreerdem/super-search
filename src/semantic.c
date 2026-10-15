#include "semantic.h"
#include "utils.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

SemanticKind semantic_parse_kind(const char* str) {
    if (!str) return KIND_ALL;
    if (strcasecmp(str, "comment") == 0 || strcasecmp(str, "comments") == 0 || strcasecmp(str, "com") == 0) {
        return KIND_COMMENT;
    }
    if (strcasecmp(str, "string") == 0 || strcasecmp(str, "strings") == 0 || strcasecmp(str, "str") == 0) {
        return KIND_STRING;
    }
    if (strcasecmp(str, "code") == 0) {
        return KIND_CODE;
    }
    if (strcasecmp(str, "function") == 0 || strcasecmp(str, "func") == 0 ||
        strcasecmp(str, "fn") == 0 || strcasecmp(str, "def") == 0 || strcasecmp(str, "method") == 0) {
        return KIND_FUNCTION;
    }
    return KIND_ALL;
}

const char* semantic_kind_name(SemanticKind kind) {
    switch (kind) {
        case KIND_COMMENT: return "comment";
        case KIND_STRING: return "string";
        case KIND_CODE: return "code";
        case KIND_FUNCTION: return "function";
        default: return "all";
    }
}

static const char* get_file_extension(const char* path) {
    if (!path) return "";
    const char* dot = strrchr(path, '.');
    return dot ? dot : "";
}

static bool is_hash_comment_lang(const char* ext) {
    if (!ext || ext[0] == '\0') return false;
    return (strcasecmp(ext, ".py") == 0 || strcasecmp(ext, ".sh") == 0 ||
            strcasecmp(ext, ".bash") == 0 || strcasecmp(ext, ".rb") == 0 ||
            strcasecmp(ext, ".yaml") == 0 || strcasecmp(ext, ".yml") == 0 ||
            strcasecmp(ext, ".toml") == 0 || strcasecmp(ext, ".ps1") == 0 ||
            strcasecmp(ext, ".pl") == 0 || strcasecmp(ext, ".r") == 0 ||
            strcasecmp(ext, ".env") == 0 || strcasecmp(ext, ".conf") == 0);
}

static bool is_function_definition_line(const char* line, const char* ext) {
    if (!line) return false;

    // Skip leading whitespace
    const char* p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') return false;

    // Reject comment lines immediately
    if ((p[0] == '/' && (p[1] == '/' || p[1] == '*')) || p[0] == '*' || p[0] == '#') {
        return false;
    }

    // Python: def func(...) or async def func(...)
    if (strncmp(p, "def ", 4) == 0 || strncmp(p, "async def ", 10) == 0) {
        return (strchr(p, '(') != NULL);
    }

    // Rust: fn func(...) or pub fn func(...) or pub(crate) fn func(...) or async fn func(...)
    if (strstr(p, "fn ") != NULL) {
        const char* fn_pos = strstr(p, "fn ");
        if (fn_pos == p || isspace((unsigned char)*(fn_pos - 1))) {
            return (strchr(fn_pos, '(') != NULL);
        }
    }

    // Go: func (r *Recv) Method(...) or func Function(...)
    if (strncmp(p, "func ", 5) == 0 || strncmp(p, "func(", 5) == 0) {
        return (strchr(p, '(') != NULL);
    }

    // JS/TS/PHP/PS1: function func(...) or async function func(...) or export function ...
    if (strncmp(p, "function ", 9) == 0 || strncmp(p, "async function ", 15) == 0 ||
        strncmp(p, "export function ", 16) == 0 || strncmp(p, "export async function ", 22) == 0 ||
        strncmp(p, "export default function", 23) == 0) {
        return (strchr(p, '(') != NULL);
    }
    if ((strncmp(p, "const ", 6) == 0 || strncmp(p, "let ", 4) == 0 || strncmp(p, "export const ", 13) == 0) &&
        strstr(p, "=>") != NULL && strchr(p, '(') != NULL) {
        return true;
    }

    // C/C++/C#/Java:
    // Must not be an assignment statement before '(' (e.g. int x = foo(y);)
    const char* open_paren = strchr(p, '(');
    if (!open_paren) return false;

    for (const char* q = p; q < open_paren; q++) {
        if (*q == '=' || *q == ';' || *q == '"' || *q == '\'') return false;
    }

    // Exclude control flow statements
    if (strncmp(p, "if ", 3) == 0 || strncmp(p, "if(", 3) == 0 ||
        strncmp(p, "else ", 5) == 0 || strncmp(p, "else{", 5) == 0 ||
        strncmp(p, "while ", 6) == 0 || strncmp(p, "while(", 6) == 0 ||
        strncmp(p, "for ", 4) == 0 || strncmp(p, "for(", 4) == 0 ||
        strncmp(p, "switch ", 7) == 0 || strncmp(p, "switch(", 7) == 0 ||
        strncmp(p, "catch ", 6) == 0 || strncmp(p, "catch(", 6) == 0 ||
        strncmp(p, "return ", 7) == 0 || strncmp(p, "sizeof(", 7) == 0 ||
        strncmp(p, "#include", 8) == 0 || strncmp(p, "#define", 7) == 0) {
        return false;
    }

    // Check if before open_paren is an identifier preceded by a return type/modifier
    const char* id_end = open_paren;
    while (id_end > p && isspace((unsigned char)*(id_end - 1))) id_end--;
    const char* id_start = id_end;
    while (id_start > p && (isalnum((unsigned char)*(id_start - 1)) || *(id_start - 1) == '_' || *(id_start - 1) == ':')) id_start--;

    if (id_start == id_end) return false; // No identifier before '('
    if (id_start == p) return false;      // Bare function call like foo(x), no return type before identifier

    size_t id_len = (size_t)(id_end - id_start);
    if ((id_len == 2 && strncmp(id_start, "if", 2) == 0) ||
        (id_len == 3 && strncmp(id_start, "for", 3) == 0) ||
        (id_len == 5 && strncmp(id_start, "while", 5) == 0) ||
        (id_len == 6 && strncmp(id_start, "switch", 6) == 0) ||
        (id_len == 5 && strncmp(id_start, "catch", 5) == 0) ||
        (id_len == 6 && strncmp(id_start, "return", 6) == 0) ||
        (id_len == 6 && strncmp(id_start, "sizeof", 6) == 0)) {
        return false;
    }

    // Check closing paren or multiline signature
    const char* close_paren = strchr(open_paren, ')');
    const char* end_ptr = line + strlen(line);
    while (end_ptr > line && isspace((unsigned char)*(end_ptr - 1))) end_ptr--;
    if (end_ptr > line) {
        char last_char = *(end_ptr - 1);
        if (last_char == ';') {
            // Semicolon allowed only for header declarations
            if (close_paren && (strcasecmp(ext, ".h") == 0 || strcasecmp(ext, ".hpp") == 0)) {
                return true;
            }
            return false;
        }
        if (last_char == '{' || last_char == ')' || last_char == ':' || last_char == '(' || (last_char == ',' && !close_paren)) {
            return true;
        }
    }

    return close_paren != NULL;
}

bool semantic_match_satisfies_kind(
    SemanticKind kind,
    const char* file_path,
    const char* line_text,
    int64_t col_no,
    int match_len
) {
    if (kind == KIND_ALL) return true;
    if (!line_text) return false;

    int target_idx = (int)(col_no - 1);
    if (target_idx < 0) target_idx = 0;

    const char* ext = get_file_extension(file_path);
    bool hash_comments = is_hash_comment_lang(ext);

    bool in_double_quote = false;
    bool in_single_quote = false;
    bool in_backtick = false;
    bool in_line_comment = false;
    bool in_block_comment = false;

    const char* trimmed = line_text;
    while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;
    if (*trimmed == '*' && (trimmed[1] == ' ' || trimmed[1] == '\t' || trimmed[1] == '/' || trimmed[1] == '*' || trimmed[1] == '\0')) {
        in_line_comment = true;
    }

    SemanticKind token_kind = KIND_CODE;
    size_t line_len = strlen(line_text);

    for (size_t i = 0; i < line_len; i++) {
        char c = line_text[i];
        char next = (i + 1 < line_len) ? line_text[i + 1] : '\0';

        // Check if we reached the match starting column
        if ((int)i == target_idx) {
            if (in_line_comment || in_block_comment) {
                token_kind = KIND_COMMENT;
            } else if (in_double_quote || in_single_quote || in_backtick) {
                token_kind = KIND_STRING;
            } else {
                token_kind = KIND_CODE;
            }
            break;
        }

        // Inside line comment: everything until end of line is comment
        if (in_line_comment) {
            continue;
        }

        // Inside block comment: check for */
        if (in_block_comment) {
            if (c == '*' && next == '/') {
                in_block_comment = false;
                i++; // Skip '/'
            }
            continue;
        }

        // Inside double quotes
        if (in_double_quote) {
            if (c == '\\' && next != '\0') {
                i++; // Skip escaped char
            } else if (c == '"') {
                in_double_quote = false;
            }
            continue;
        }

        // Inside single quotes
        if (in_single_quote) {
            if (c == '\\' && next != '\0') {
                i++; // Skip escaped char
            } else if (c == '\'') {
                in_single_quote = false;
            }
            continue;
        }

        // Inside backticks
        if (in_backtick) {
            if (c == '\\' && next != '\0') {
                i++; // Skip escaped char
            } else if (c == '`') {
                in_backtick = false;
            }
            continue;
        }

        // Check for comment starts
        if (c == '/' && next == '/') {
            in_line_comment = true;
            i++;
            continue;
        }
        if (c == '/' && next == '*') {
            in_block_comment = true;
            i++;
            continue;
        }
        if (hash_comments && c == '#') {
            in_line_comment = true;
            continue;
        }

        // Check for string starts
        if (c == '"') {
            in_double_quote = true;
            continue;
        }
        if (c == '\'') {
            in_single_quote = true;
            continue;
        }
        if (c == '`') {
            in_backtick = true;
            continue;
        }
    }

    // In case target_idx is beyond parsed length
    if ((int)line_len <= target_idx) {
        if (in_line_comment || in_block_comment) token_kind = KIND_COMMENT;
        else if (in_double_quote || in_single_quote || in_backtick) token_kind = KIND_STRING;
        else token_kind = KIND_CODE;
    }

    // Verify against requested kind
    if (kind == KIND_COMMENT) {
        return (token_kind == KIND_COMMENT);
    }
    if (kind == KIND_STRING) {
        return (token_kind == KIND_STRING);
    }
    if (kind == KIND_CODE) {
        return (token_kind == KIND_CODE);
    }
    if (kind == KIND_FUNCTION) {
        if (token_kind != KIND_CODE) return false;
        return is_function_definition_line(line_text, ext);
    }

    return true;
}

bool semantic_is_symbol_definition(const char* line, const char* file_path) {
    if (!line) return false;
    const char* p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') return false;

    // Ignore comments
    if ((p[0] == '/' && (p[1] == '/' || p[1] == '*')) || p[0] == '*' || p[0] == '#') {
        return false;
    }

    const char* ext = get_file_extension(file_path);

    // Check function/method definitions first
    if (is_function_definition_line(line, ext)) {
        return true;
    }

    // Check class / struct / interface / enum / trait / impl / typedef struct definitions
    if (strncmp(p, "class ", 6) == 0 || strncmp(p, "struct ", 7) == 0 ||
        strncmp(p, "interface ", 10) == 0 || strncmp(p, "enum ", 5) == 0 ||
        strncmp(p, "trait ", 6) == 0 || strncmp(p, "impl ", 5) == 0 ||
        strncmp(p, "typedef struct", 14) == 0 || strncmp(p, "typedef enum", 12) == 0 ||
        strncmp(p, "export class ", 13) == 0 || strncmp(p, "export interface ", 17) == 0 ||
        strncmp(p, "export type ", 12) == 0 || strncmp(p, "public class ", 13) == 0 ||
        strncmp(p, "public interface ", 17) == 0 || strncmp(p, "public struct ", 14) == 0 ||
        strncmp(p, "public enum ", 12) == 0 || strncmp(p, "private class ", 14) == 0 ||
        strncmp(p, "internal class ", 15) == 0 || strncmp(p, "abstract class ", 15) == 0) {
        const char* end_ptr = line + strlen(line);
        while (end_ptr > line && isspace((unsigned char)*(end_ptr - 1))) end_ptr--;
        if (end_ptr > line && *(end_ptr - 1) == ';' && strncmp(p, "export type ", 12) != 0) {
            return false; // Forward declaration or struct variable declaration
        }
        return true;
    }

    return false;
}

char* semantic_find_enclosing_function(
    const char* data,
    size_t data_len,
    size_t match_offset,
    int64_t match_line_no,
    const char* file_path,
    int64_t* out_func_line
) {
    if (out_func_line) *out_func_line = 0;
    if (!data || data_len == 0 || match_offset > data_len) return NULL;

    // Locate start of the match's line
    size_t line_start = match_offset;
    while (line_start > 0 && data[line_start - 1] != '\n') {
        line_start--;
    }

    int64_t cur_line_no = match_line_no;
    size_t cur_start = line_start;
    int lines_checked = 0;

    while (lines_checked < 500) {
        size_t cur_end = cur_start;
        while (cur_end < data_len && data[cur_end] != '\r' && data[cur_end] != '\n') {
            cur_end++;
        }

        size_t len = cur_end - cur_start;
        if (len > 0 && len < 512) {
            char buf[512];
            memcpy(buf, &data[cur_start], len);
            buf[len] = '\0';

            if (semantic_is_symbol_definition(buf, file_path)) {
                // Trim leading & trailing whitespace for clean signature
                char* s = buf;
                while (*s && isspace((unsigned char)*s)) s++;
                size_t slen = strlen(s);
                while (slen > 0 && isspace((unsigned char)s[slen - 1])) {
                    s[--slen] = '\0';
                }
                if (out_func_line) *out_func_line = cur_line_no;
                return safe_strdup(s);
            }
        }

        if (cur_start == 0 || cur_line_no <= 1) break;

        // Move to previous line
        size_t prev_end = cur_start - 1;
        cur_start = prev_end;
        while (cur_start > 0 && data[cur_start - 1] != '\n') {
            cur_start--;
        }
        cur_line_no--;
        lines_checked++;
    }

    return NULL;
}

