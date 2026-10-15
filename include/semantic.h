#ifndef SEMANTIC_H
#define SEMANTIC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    KIND_ALL = 0,       // No semantic filtering (default)
    KIND_COMMENT,       // Only matches within comments (//, /* */, #)
    KIND_STRING,        // Only matches within string literals ("...", '...', `...`)
    KIND_CODE,          // Only matches within actual code (not comment, not string)
    KIND_FUNCTION       // Only matches within function definitions/signatures
} SemanticKind;

// Parse CLI string (e.g. "comment", "string", "code", "function", "func", "fn")
SemanticKind semantic_parse_kind(const char* str);
const char* semantic_kind_name(SemanticKind kind);

// Check if a match at col_no in line_text satisfies the requested SemanticKind
// col_no is 1-based index
bool semantic_match_satisfies_kind(
    SemanticKind kind,
    const char* file_path,
    const char* line_text,
    int64_t col_no,
    int match_len
);

// Check if a single line is a function, method, class, struct, or interface definition
bool semantic_is_symbol_definition(const char* line, const char* file_path);

// Scan backward from match_offset (up to 200 lines) to find the enclosing function/class signature
char* semantic_find_enclosing_function(
    const char* data,
    size_t data_len,
    size_t match_offset,
    int64_t match_line_no,
    const char* file_path,
    int64_t* out_func_line
);

#endif // SEMANTIC_H
