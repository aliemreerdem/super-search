#ifndef REGEX_ENGINE_H
#define REGEX_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Regex instruction opcodes
typedef enum {
    RE_OP_END = 0,          // Match reached end of pattern (success)
    RE_OP_CHAR,             // Match single character (case-sensitive or insensitive)
    RE_OP_ANY,              // Match any char except \r, \n (or any char in multiline)
    RE_OP_ANY_ALL,          // Match any char including \r, \n
    RE_OP_CLASS,            // Match char in 256-bit set
    RE_OP_NCLASS,           // Match char NOT in 256-bit set
    RE_OP_BOL,              // Beginning of line (^)
    RE_OP_EOL,              // End of line ($)
    RE_OP_WORD_BOUNDARY,    // Word boundary (\b)
    RE_OP_NOT_WORD_BOUNDARY,// Non-word boundary (\B)
    RE_OP_SPLIT,            // Split execution: try x, backtrack to y (greedy)
    RE_OP_SPLIT_LAZY,       // Split execution: try y, backtrack to x (lazy)
    RE_OP_JUMP              // Unconditional jump to target instruction
} RegexOp;

// Character class bitset (256 bits = 32 bytes)
typedef struct {
    uint32_t bits[8];
} RegexClass;

// Single regex instruction in compiled bytecode
typedef struct {
    RegexOp op;
    union {
        struct {
            unsigned char c;
            unsigned char c_lower;
            unsigned char c_upper;
        } ch;
        RegexClass cls;
        struct {
            int target1;
            int target2;
        } split;
        int jump_target;
    } u;
} RegexInst;

// Compiled regex object
typedef struct RegexCompiled {
    RegexInst* insts;
    int inst_count;
    int inst_capacity;
    bool ignore_case;
    char original_pattern[512];

    // SIMD Acceleration and Fast-Path Optimization
    char prefix[64];
    int prefix_len;
    char prefix_lower[64];
    char prefix_upper[64];
    int prefix_skip[256];
    bool has_prefix;
    bool has_bol_anchor;             // Starts with ^
    bool has_word_boundary_prefix;   // Starts with \b
    int min_length;                  // Minimum bytes required to match
} RegexCompiled;

// Compile a regular expression into bytecode
// Returns NULL on error and fills err_buf if provided
RegexCompiled* regex_compile(const char* pattern, bool ignore_case, char* err_buf, size_t err_buf_size);

// Free compiled regex memory
void regex_free(RegexCompiled* re);

// Helper to detect if a pattern string contains regex metacharacters
bool regex_has_metacharacters(const char* pattern);

// Test if regex matches at the exact text position 'pos'
// If matched, sets *out_matched_len and returns true
bool regex_match_at(
    const RegexCompiled* re,
    const char* text,
    size_t text_len,
    size_t pos,
    bool multiline,
    size_t* out_matched_len
);

// Search for the first match in buffer starting at 'start_pos'
// Returns true if found, setting *out_match_pos and *out_match_len
bool regex_search_in_buffer(
    const RegexCompiled* re,
    const char* text,
    size_t text_len,
    size_t start_pos,
    bool multiline,
    size_t* out_match_pos,
    size_t* out_match_len
);

#endif // REGEX_ENGINE_H
