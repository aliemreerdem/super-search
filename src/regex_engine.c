#include "regex_engine.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_REGEX_INSTS 4096
#define MAX_BACKTRACK_STACK 2048

static inline bool is_word_char(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static inline void class_set_bit(RegexClass* cls, unsigned char c) {
    cls->bits[c >> 5] |= (1U << (c & 31));
}

static inline bool class_has(const RegexClass* cls, unsigned char c) {
    return (cls->bits[c >> 5] & (1U << (c & 31))) != 0;
}

static inline void class_set_range(RegexClass* cls, unsigned char start, unsigned char end, bool ignore_case) {
    for (int c = start; c <= end; c++) {
        class_set_bit(cls, (unsigned char)c);
        if (ignore_case) {
            class_set_bit(cls, (unsigned char)tolower(c));
            class_set_bit(cls, (unsigned char)toupper(c));
        }
    }
}

// Check if a character or pattern string contains regex metacharacters
bool regex_has_metacharacters(const char* pattern) {
    if (!pattern) return false;
    const char* meta = "*+?^$[](){}|\\.";
    for (const char* p = pattern; *p; p++) {
        if (strchr(meta, *p)) return true;
    }
    return false;
}

// Compiler internal context
typedef struct {
    const char* src;
    size_t pos;
    size_t len;
    bool ignore_case;
    RegexCompiled* re;
    char* err_buf;
    size_t err_buf_size;
} RegexParser;

static void set_error(RegexParser* parser, const char* msg) {
    if (parser->err_buf && parser->err_buf_size > 0) {
        snprintf(parser->err_buf, parser->err_buf_size, "Regex error at pos %zu: %s", parser->pos, msg);
    }
}

static int emit_inst(RegexParser* parser, RegexInst inst) {
    RegexCompiled* re = parser->re;
    if (re->inst_count >= re->inst_capacity) {
        int new_cap = (re->inst_capacity == 0) ? 64 : re->inst_capacity * 2;
        if (new_cap > MAX_REGEX_INSTS) {
            set_error(parser, "Regex compiled instruction limit exceeded");
            return -1;
        }
        re->insts = (RegexInst*)safe_realloc(re->insts, new_cap * sizeof(RegexInst));
        re->inst_capacity = new_cap;
    }
    int idx = re->inst_count++;
    re->insts[idx] = inst;
    return idx;
}

// Forward declarations of parser functions
static int parse_expr(RegexParser* parser);
static int parse_branch(RegexParser* parser);
static int parse_piece(RegexParser* parser);
static int parse_atom(RegexParser* parser);

// Parse an expression: branch ('|' branch)*
static int parse_expr(RegexParser* parser) {
    int start_node = parse_branch(parser);
    if (start_node < 0) return -1;

    while (parser->pos < parser->len && parser->src[parser->pos] == '|') {
        parser->pos++; // consume '|'

        // Create split: branch 1 or branch 2
        // We insert a split before start_node or restructure
        // Pattern: split(branch1, branch2)
        // Branch 1 jumps to end
        int branch1_start = start_node;
        int branch1_end_jump = -1;

        // Emit jump at end of branch 1 to jump past branch 2
        RegexInst jmp_inst;
        memset(&jmp_inst, 0, sizeof(jmp_inst));
        jmp_inst.op = RE_OP_JUMP;
        jmp_inst.u.jump_target = -1;
        branch1_end_jump = emit_inst(parser, jmp_inst);
        if (branch1_end_jump < 0) return -1;

        int branch2_start = parse_branch(parser);
        if (branch2_start < 0) return -1;

        int end_node = parser->re->inst_count;
        parser->re->insts[branch1_end_jump].u.jump_target = end_node;

        // Shift instructions to insert split at branch1_start
        // To keep compiler simple and linear without large memmoves:
        // We emit: split_inst that branches to branch1_start and branch2_start!
        // We can do this with an entry jump or relocation.
        // Even simpler: allocate split at end and jump, but standard NFA uses:
        // split L1, L2.
        // Let's implement safe instruction insertion at branch1_start:
        if (parser->re->inst_count >= parser->re->inst_capacity) {
            int new_cap = parser->re->inst_capacity * 2;
            if (new_cap < 64) new_cap = 64;
            parser->re->insts = (RegexInst*)safe_realloc(parser->re->insts, new_cap * sizeof(RegexInst));
            parser->re->inst_capacity = new_cap;
        }

        memmove(&parser->re->insts[branch1_start + 1],
                &parser->re->insts[branch1_start],
                (parser->re->inst_count - branch1_start) * sizeof(RegexInst));
        parser->re->inst_count++;

        // Adjust all jump and split targets that were >= branch1_start
        for (int i = 0; i < parser->re->inst_count; i++) {
            if (i == branch1_start) continue;
            RegexInst* in = &parser->re->insts[i];
            if (in->op == RE_OP_JUMP) {
                if (in->u.jump_target >= branch1_start) in->u.jump_target++;
            } else if (in->op == RE_OP_SPLIT || in->op == RE_OP_SPLIT_LAZY) {
                if (in->u.split.target1 >= branch1_start) in->u.split.target1++;
                if (in->u.split.target2 >= branch1_start) in->u.split.target2++;
            }
        }

        RegexInst split_inst;
        memset(&split_inst, 0, sizeof(split_inst));
        split_inst.op = RE_OP_SPLIT;
        split_inst.u.split.target1 = branch1_start + 1;
        split_inst.u.split.target2 = branch2_start + 1;
        parser->re->insts[branch1_start] = split_inst;

        start_node = branch1_start;
    }

    return start_node;
}

// Parse branch: sequence of pieces
static int parse_branch(RegexParser* parser) {
    int first_node = -1;

    while (parser->pos < parser->len) {
        char c = parser->src[parser->pos];
        if (c == '|' || c == ')') break;

        int piece_start = parse_piece(parser);
        if (piece_start < 0) return -1;

        if (first_node < 0) {
            first_node = piece_start;
        }
    }

    if (first_node < 0) {
        // Empty branch matches epsilon
        RegexInst nop;
        memset(&nop, 0, sizeof(nop));
        nop.op = RE_OP_JUMP;
        nop.u.jump_target = parser->re->inst_count + 1;
        first_node = emit_inst(parser, nop);
    }

    return first_node;
}

// Helper to emit a copy of an instruction range [orig_start .. orig_start + count)
// while relocating internal jump and split targets to the new destination offset.
static int emit_atom_relocated(RegexParser* parser, const RegexInst* atom_copy, int atom_count, int orig_start) {
    int new_base = parser->re->inst_count;
    int delta = new_base - orig_start;
    for (int a = 0; a < atom_count; a++) {
        RegexInst inst = atom_copy[a];
        if (inst.op == RE_OP_JUMP) {
            inst.u.jump_target += delta;
        } else if (inst.op == RE_OP_SPLIT || inst.op == RE_OP_SPLIT_LAZY) {
            inst.u.split.target1 += delta;
            inst.u.split.target2 += delta;
        }
        if (emit_inst(parser, inst) < 0) return -1;
    }
    return new_base;
}

// Parse piece: atom followed by quantifier (*, +, ?, {m,n})
static int parse_piece(RegexParser* parser) {
    int atom_start = parser->re->inst_count;
    int res = parse_atom(parser);
    if (res < 0) return -1;
    int atom_end = parser->re->inst_count;

    if (parser->pos >= parser->len) {
        return atom_start;
    }

    char q = parser->src[parser->pos];
    if (q == '*' || q == '+' || q == '?') {
        parser->pos++;
        bool lazy = false;
        if (parser->pos < parser->len && parser->src[parser->pos] == '?') {
            lazy = true;
            parser->pos++;
        }

        if (q == '?') {
            // atom? -> split(atom, after)
            int atom_count = atom_end - atom_start;
            if (parser->re->inst_count >= parser->re->inst_capacity) {
                int new_cap = parser->re->inst_capacity * 2;
                if (new_cap < 64) new_cap = 64;
                parser->re->insts = (RegexInst*)safe_realloc(parser->re->insts, new_cap * sizeof(RegexInst));
                parser->re->inst_capacity = new_cap;
            }
            memmove(&parser->re->insts[atom_start + 1],
                    &parser->re->insts[atom_start],
                    atom_count * sizeof(RegexInst));
            parser->re->inst_count++;

            for (int i = 0; i < parser->re->inst_count; i++) {
                if (i == atom_start) continue;
                RegexInst* in = &parser->re->insts[i];
                if (in->op == RE_OP_JUMP) {
                    if (in->u.jump_target >= atom_start) in->u.jump_target++;
                } else if (in->op == RE_OP_SPLIT || in->op == RE_OP_SPLIT_LAZY) {
                    if (in->u.split.target1 >= atom_start) in->u.split.target1++;
                    if (in->u.split.target2 >= atom_start) in->u.split.target2++;
                }
            }

            RegexInst split_inst;
            memset(&split_inst, 0, sizeof(split_inst));
            split_inst.op = lazy ? RE_OP_SPLIT_LAZY : RE_OP_SPLIT;
            split_inst.u.split.target1 = atom_start + 1;
            split_inst.u.split.target2 = atom_start + 1 + atom_count;
            parser->re->insts[atom_start] = split_inst;
            return atom_start;
        } else if (q == '+') {
            // atom+ -> atom, split(atom_start, after)
            RegexInst split_inst;
            memset(&split_inst, 0, sizeof(split_inst));
            split_inst.op = lazy ? RE_OP_SPLIT_LAZY : RE_OP_SPLIT;
            split_inst.u.split.target1 = atom_start;
            split_inst.u.split.target2 = parser->re->inst_count + 1;
            emit_inst(parser, split_inst);
            return atom_start;
        } else if (q == '*') {
            // atom* -> split(atom, after); atom jumps back to split
            int atom_count = atom_end - atom_start;
            if (parser->re->inst_count >= parser->re->inst_capacity) {
                int new_cap = parser->re->inst_capacity * 2;
                if (new_cap < 64) new_cap = 64;
                parser->re->insts = (RegexInst*)safe_realloc(parser->re->insts, new_cap * sizeof(RegexInst));
                parser->re->inst_capacity = new_cap;
            }
            memmove(&parser->re->insts[atom_start + 1],
                    &parser->re->insts[atom_start],
                    atom_count * sizeof(RegexInst));
            parser->re->inst_count++;

            for (int i = 0; i < parser->re->inst_count; i++) {
                if (i == atom_start) continue;
                RegexInst* in = &parser->re->insts[i];
                if (in->op == RE_OP_JUMP) {
                    if (in->u.jump_target >= atom_start) in->u.jump_target++;
                } else if (in->op == RE_OP_SPLIT || in->op == RE_OP_SPLIT_LAZY) {
                    if (in->u.split.target1 >= atom_start) in->u.split.target1++;
                    if (in->u.split.target2 >= atom_start) in->u.split.target2++;
                }
            }

            // Jump back to split
            RegexInst jmp_inst;
            memset(&jmp_inst, 0, sizeof(jmp_inst));
            jmp_inst.op = RE_OP_JUMP;
            jmp_inst.u.jump_target = atom_start;
            emit_inst(parser, jmp_inst);

            RegexInst split_inst;
            memset(&split_inst, 0, sizeof(split_inst));
            split_inst.op = lazy ? RE_OP_SPLIT_LAZY : RE_OP_SPLIT;
            split_inst.u.split.target1 = atom_start + 1;
            split_inst.u.split.target2 = parser->re->inst_count;
            parser->re->insts[atom_start] = split_inst;
            return atom_start;
        }
    } else if (q == '{') {
        // Counted repetition {min,max} or {min} or {min,}
        size_t save_pos = parser->pos++;
        int min_rep = 0;
        int max_rep = -1;
        bool has_digits = false;

        while (parser->pos < parser->len && isdigit((unsigned char)parser->src[parser->pos])) {
            min_rep = min_rep * 10 + (parser->src[parser->pos] - '0');
            parser->pos++;
            has_digits = true;
        }

        if (has_digits && parser->pos < parser->len && parser->src[parser->pos] == ',') {
            parser->pos++;
            if (parser->pos < parser->len && isdigit((unsigned char)parser->src[parser->pos])) {
                max_rep = 0;
                while (parser->pos < parser->len && isdigit((unsigned char)parser->src[parser->pos])) {
                    max_rep = max_rep * 10 + (parser->src[parser->pos] - '0');
                    parser->pos++;
                }
            } else {
                max_rep = 255; // {min,} unbounded
            }
        } else {
            max_rep = min_rep; // {min}
        }

        if (has_digits && parser->pos < parser->len && parser->src[parser->pos] == '}') {
            parser->pos++; // consume '}'
            bool lazy = false;
            if (parser->pos < parser->len && parser->src[parser->pos] == '?') {
                lazy = true;
                parser->pos++;
            }

            int atom_count = atom_end - atom_start;
            if (atom_count <= 0 || min_rep > 100 || (max_rep > 100 && max_rep != 255) || (max_rep != 255 && max_rep < min_rep)) {
                set_error(parser, "Invalid or too large repetition range");
                return -1;
            }

            // Save original atom instructions and rewind inst_count to atom_start
            RegexInst* atom_copy = (RegexInst*)safe_malloc(atom_count * sizeof(RegexInst));
            memcpy(atom_copy, &parser->re->insts[atom_start], atom_count * sizeof(RegexInst));
            parser->re->inst_count = atom_start;

            if (min_rep == 0 && max_rep == 0) {
                RegexInst nop;
                memset(&nop, 0, sizeof(nop));
                nop.op = RE_OP_JUMP;
                nop.u.jump_target = parser->re->inst_count + 1;
                emit_inst(parser, nop);
                free(atom_copy);
                return atom_start;
            }

            // 1. Emit required repetitions (min_rep times) with proper target relocation
            for (int k = 0; k < min_rep; k++) {
                if (emit_atom_relocated(parser, atom_copy, atom_count, atom_start) < 0) {
                    free(atom_copy);
                    return -1;
                }
            }

            // 2. Emit optional repetitions
            if (max_rep == 255) {
                // {min,} -> after min_rep required copies, emit a Kleene star loop:
                // loop_split: SPLIT(loop_body, after_loop)
                // loop_body:  atom_copy
                //             JUMP(loop_split)
                int loop_split = parser->re->inst_count;
                RegexInst split_inst;
                memset(&split_inst, 0, sizeof(split_inst));
                split_inst.op = lazy ? RE_OP_SPLIT_LAZY : RE_OP_SPLIT;
                split_inst.u.split.target1 = loop_split + 1;
                split_inst.u.split.target2 = -1; // patched below
                if (emit_inst(parser, split_inst) < 0) {
                    free(atom_copy);
                    return -1;
                }

                if (emit_atom_relocated(parser, atom_copy, atom_count, atom_start) < 0) {
                    free(atom_copy);
                    return -1;
                }

                RegexInst jmp_inst;
                memset(&jmp_inst, 0, sizeof(jmp_inst));
                jmp_inst.op = RE_OP_JUMP;
                jmp_inst.u.jump_target = loop_split;
                if (emit_inst(parser, jmp_inst) < 0) {
                    free(atom_copy);
                    return -1;
                }

                parser->re->insts[loop_split].u.split.target2 = parser->re->inst_count;
            } else if (max_rep > min_rep) {
                for (int k = min_rep; k < max_rep; k++) {
                    int opt_start = parser->re->inst_count;
                    RegexInst split_inst;
                    memset(&split_inst, 0, sizeof(split_inst));
                    split_inst.op = lazy ? RE_OP_SPLIT_LAZY : RE_OP_SPLIT;
                    split_inst.u.split.target1 = opt_start + 1;
                    split_inst.u.split.target2 = -1; // patched below
                    if (emit_inst(parser, split_inst) < 0) {
                        free(atom_copy);
                        return -1;
                    }

                    if (emit_atom_relocated(parser, atom_copy, atom_count, atom_start) < 0) {
                        free(atom_copy);
                        return -1;
                    }
                    parser->re->insts[opt_start].u.split.target2 = parser->re->inst_count;
                }
            }

            free(atom_copy);
            return atom_start;
        } else {
            // Not a valid {m,n}, rewind and treat '{' literally
            parser->pos = save_pos;
        }
    }

    return atom_start;
}

// Parse atom: character, class, anchor, group, or escape
static int parse_atom(RegexParser* parser) {
    if (parser->pos >= parser->len) {
        set_error(parser, "Unexpected end of pattern");
        return -1;
    }

    char c = parser->src[parser->pos++];

    if (c == '^') {
        RegexInst inst;
        memset(&inst, 0, sizeof(inst));
        inst.op = RE_OP_BOL;
        return emit_inst(parser, inst);
    }

    if (c == '$') {
        RegexInst inst;
        memset(&inst, 0, sizeof(inst));
        inst.op = RE_OP_EOL;
        return emit_inst(parser, inst);
    }

    if (c == '.') {
        RegexInst inst;
        memset(&inst, 0, sizeof(inst));
        inst.op = RE_OP_ANY;
        return emit_inst(parser, inst);
    }

    if (c == '(') {
        // Grouping: check for (?i) case flag
        if (parser->pos + 2 <= parser->len && parser->src[parser->pos] == '?' && parser->src[parser->pos + 1] == 'i') {
            parser->pos += 2;
            if (parser->pos < parser->len && parser->src[parser->pos] == ')') {
                parser->pos++;
                parser->ignore_case = true;
                parser->re->ignore_case = true;
                // Epsilon transition
                RegexInst nop;
                memset(&nop, 0, sizeof(nop));
                nop.op = RE_OP_JUMP;
                nop.u.jump_target = parser->re->inst_count + 1;
                return emit_inst(parser, nop);
            }
        }

        int group_start = parse_expr(parser);
        if (group_start < 0) return -1;
        if (parser->pos >= parser->len || parser->src[parser->pos] != ')') {
            set_error(parser, "Unmatched open parenthesis '('");
            return -1;
        }
        parser->pos++; // consume ')'
        return group_start;
    }

    if (c == '[') {
        // Character class
        bool inverted = false;
        if (parser->pos < parser->len && parser->src[parser->pos] == '^') {
            inverted = true;
            parser->pos++;
        }

        RegexClass cls;
        memset(&cls, 0, sizeof(cls));

        // If ']' is the very first character inside class, treat as literal ']'
        if (parser->pos < parser->len && parser->src[parser->pos] == ']') {
            class_set_bit(&cls, ']');
            parser->pos++;
        }

        while (parser->pos < parser->len && parser->src[parser->pos] != ']') {
            char ch = parser->src[parser->pos++];
            if (ch == '\\' && parser->pos < parser->len) {
                char esc = parser->src[parser->pos++];
                if (esc == 'd') class_set_range(&cls, '0', '9', parser->ignore_case);
                else if (esc == 'w') {
                    class_set_range(&cls, 'a', 'z', parser->ignore_case);
                    class_set_range(&cls, 'A', 'Z', parser->ignore_case);
                    class_set_range(&cls, '0', '9', parser->ignore_case);
                    class_set_bit(&cls, '_');
                } else if (esc == 's') {
                    class_set_bit(&cls, ' '); class_set_bit(&cls, '\t');
                    class_set_bit(&cls, '\r'); class_set_bit(&cls, '\n');
                    class_set_bit(&cls, '\v'); class_set_bit(&cls, '\f');
                } else if (esc == 'n') class_set_bit(&cls, '\n');
                else if (esc == 'r') class_set_bit(&cls, '\r');
                else if (esc == 't') class_set_bit(&cls, '\t');
                else {
                    class_set_bit(&cls, (unsigned char)esc);
                    if (parser->ignore_case) {
                        class_set_bit(&cls, (unsigned char)tolower((unsigned char)esc));
                        class_set_bit(&cls, (unsigned char)toupper((unsigned char)esc));
                    }
                }
                continue;
            }

            // Check for range 'ch - next'
            if (parser->pos + 1 < parser->len && parser->src[parser->pos] == '-' && parser->src[parser->pos + 1] != ']') {
                parser->pos++; // consume '-'
                char ch_end = parser->src[parser->pos++];
                if ((unsigned char)ch <= (unsigned char)ch_end) {
                    class_set_range(&cls, (unsigned char)ch, (unsigned char)ch_end, parser->ignore_case);
                } else {
                    class_set_range(&cls, (unsigned char)ch_end, (unsigned char)ch, parser->ignore_case);
                }
            } else {
                class_set_bit(&cls, (unsigned char)ch);
                if (parser->ignore_case) {
                    class_set_bit(&cls, (unsigned char)tolower((unsigned char)ch));
                    class_set_bit(&cls, (unsigned char)toupper((unsigned char)ch));
                }
            }
        }

        if (parser->pos >= parser->len || parser->src[parser->pos] != ']') {
            set_error(parser, "Unmatched open bracket '['");
            return -1;
        }
        parser->pos++; // consume ']'

        RegexInst inst;
        memset(&inst, 0, sizeof(inst));
        inst.op = inverted ? RE_OP_NCLASS : RE_OP_CLASS;
        inst.u.cls = cls;
        return emit_inst(parser, inst);
    }

    if (c == '\\') {
        if (parser->pos >= parser->len) {
            set_error(parser, "Trailing backslash");
            return -1;
        }
        char esc = parser->src[parser->pos++];

        if (esc == 'b') {
            RegexInst inst;
            memset(&inst, 0, sizeof(inst));
            inst.op = RE_OP_WORD_BOUNDARY;
            return emit_inst(parser, inst);
        }
        if (esc == 'B') {
            RegexInst inst;
            memset(&inst, 0, sizeof(inst));
            inst.op = RE_OP_NOT_WORD_BOUNDARY;
            return emit_inst(parser, inst);
        }
        if (esc == 'd' || esc == 'D' || esc == 'w' || esc == 'W' || esc == 's' || esc == 'S') {
            RegexClass cls;
            memset(&cls, 0, sizeof(cls));
            if (esc == 'd' || esc == 'D') {
                class_set_range(&cls, '0', '9', false);
            } else if (esc == 'w' || esc == 'W') {
                class_set_range(&cls, 'a', 'z', true);
                class_set_range(&cls, '0', '9', false);
                class_set_bit(&cls, '_');
            } else if (esc == 's' || esc == 'S') {
                class_set_bit(&cls, ' '); class_set_bit(&cls, '\t');
                class_set_bit(&cls, '\r'); class_set_bit(&cls, '\n');
                class_set_bit(&cls, '\v'); class_set_bit(&cls, '\f');
            }
            RegexInst inst;
            memset(&inst, 0, sizeof(inst));
            inst.op = (esc == 'D' || esc == 'W' || esc == 'S') ? RE_OP_NCLASS : RE_OP_CLASS;
            inst.u.cls = cls;
            return emit_inst(parser, inst);
        }

        // Standard escapes
        unsigned char target_c = (unsigned char)esc;
        if (esc == 'n') target_c = '\n';
        else if (esc == 'r') target_c = '\r';
        else if (esc == 't') target_c = '\t';

        RegexInst inst;
        memset(&inst, 0, sizeof(inst));
        inst.op = RE_OP_CHAR;
        inst.u.ch.c = target_c;
        inst.u.ch.c_lower = (unsigned char)tolower(target_c);
        inst.u.ch.c_upper = (unsigned char)toupper(target_c);
        return emit_inst(parser, inst);
    }

    // Normal literal character
    RegexInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.op = RE_OP_CHAR;
    inst.u.ch.c = (unsigned char)c;
    inst.u.ch.c_lower = (unsigned char)tolower((unsigned char)c);
    inst.u.ch.c_upper = (unsigned char)toupper((unsigned char)c);
    return emit_inst(parser, inst);
}

// Compile pattern into bytecode and extract acceleration properties
RegexCompiled* regex_compile(const char* pattern, bool ignore_case, char* err_buf, size_t err_buf_size) {
    if (!pattern) {
        if (err_buf && err_buf_size > 0) snprintf(err_buf, err_buf_size, "Pattern is NULL");
        return NULL;
    }

    RegexCompiled* re = (RegexCompiled*)safe_malloc(sizeof(RegexCompiled));
    memset(re, 0, sizeof(RegexCompiled));
    re->ignore_case = ignore_case;
    strncpy(re->original_pattern, pattern, sizeof(re->original_pattern) - 1);

    RegexParser parser;
    memset(&parser, 0, sizeof(parser));
    parser.src = pattern;
    parser.pos = 0;
    parser.len = strlen(pattern);
    parser.ignore_case = ignore_case;
    parser.re = re;
    parser.err_buf = err_buf;
    parser.err_buf_size = err_buf_size;

    int root = parse_expr(&parser);
    if (root < 0) {
        regex_free(re);
        return NULL;
    }

    // Append match completion opcode
    RegexInst end_inst;
    memset(&end_inst, 0, sizeof(end_inst));
    end_inst.op = RE_OP_END;
    emit_inst(&parser, end_inst);

    // Extract optimization properties: prefix, anchors, word boundaries
    int pc = 0;
    if (re->inst_count > pc && re->insts[pc].op == RE_OP_BOL) {
        re->has_bol_anchor = true;
        pc++;
    }
    if (re->inst_count > pc && re->insts[pc].op == RE_OP_WORD_BOUNDARY) {
        re->has_word_boundary_prefix = true;
        pc++;
    }

    int prefix_idx = 0;
    while (pc < re->inst_count && re->insts[pc].op == RE_OP_CHAR && prefix_idx < 63) {
        unsigned char ch = re->insts[pc].u.ch.c;
        re->prefix[prefix_idx] = (char)ch;
        re->prefix_lower[prefix_idx] = (char)re->insts[pc].u.ch.c_lower;
        re->prefix_upper[prefix_idx] = (char)re->insts[pc].u.ch.c_upper;
        prefix_idx++;
        pc++;
    }
    re->prefix[prefix_idx] = '\0';
    re->prefix_lower[prefix_idx] = '\0';
    re->prefix_upper[prefix_idx] = '\0';
    re->prefix_len = prefix_idx;

    if (re->prefix_len >= 2) {
        re->has_prefix = true;
        int m = re->prefix_len;
        for (int i = 0; i < 256; i++) re->prefix_skip[i] = m;
        for (int i = 0; i < m - 1; i++) {
            if (re->ignore_case) {
                re->prefix_skip[(unsigned char)re->prefix_lower[i]] = m - 1 - i;
                re->prefix_skip[(unsigned char)re->prefix_upper[i]] = m - 1 - i;
            } else {
                re->prefix_skip[(unsigned char)re->prefix[i]] = m - 1 - i;
            }
        }
    }

    return re;
}

void regex_free(RegexCompiled* re) {
    if (!re) return;
    if (re->insts) {
        free(re->insts);
        re->insts = NULL;
    }
    free(re);
}

// Backtracking frame structure
typedef struct {
    int pc;
    size_t pos;
} BacktrackFrame;

// Match regex at exact position
bool regex_match_at(
    const RegexCompiled* re,
    const char* text,
    size_t text_len,
    size_t pos,
    bool multiline,
    size_t* out_matched_len
) {
    if (!re || !text || pos > text_len) return false;

    BacktrackFrame stack[MAX_BACKTRACK_STACK];
    int sp = 0;

    int pc = 0;
    size_t cur = pos;
    size_t max_matched_len = 0;
    bool found_match = false;

    // Iterative bytecode execution loop
    while (true) {
        if (pc >= re->inst_count) goto backtrack;

        const RegexInst* in = &re->insts[pc];
        switch (in->op) {
            case RE_OP_END: {
                // Match succeeded!
                size_t mlen = cur - pos;
                if (!found_match || mlen >= max_matched_len) {
                    max_matched_len = mlen;
                    found_match = true;
                }
                // Greedy: check if another longer match can be found on backtrack stack
                goto backtrack;
            }

            case RE_OP_CHAR: {
                if (cur >= text_len) goto backtrack;
                unsigned char tc = (unsigned char)text[cur];
                if (re->ignore_case) {
                    if (tc != in->u.ch.c_lower && tc != in->u.ch.c_upper) goto backtrack;
                } else {
                    if (tc != in->u.ch.c) goto backtrack;
                }
                cur++;
                pc++;
                break;
            }

            case RE_OP_ANY: {
                if (cur >= text_len) goto backtrack;
                char c = text[cur];
                if (!multiline && (c == '\n' || c == '\r')) goto backtrack;
                cur++;
                pc++;
                break;
            }

            case RE_OP_ANY_ALL: {
                if (cur >= text_len) goto backtrack;
                cur++;
                pc++;
                break;
            }

            case RE_OP_CLASS: {
                if (cur >= text_len) goto backtrack;
                unsigned char c = (unsigned char)text[cur];
                if (!class_has(&in->u.cls, c)) goto backtrack;
                cur++;
                pc++;
                break;
            }

            case RE_OP_NCLASS: {
                if (cur >= text_len) goto backtrack;
                unsigned char c = (unsigned char)text[cur];
                if (c == '\n' || c == '\r' || class_has(&in->u.cls, c)) goto backtrack;
                cur++;
                pc++;
                break;
            }

            case RE_OP_BOL: {
                // ^ matches beginning of text or immediately after newline
                if (cur == 0 || text[cur - 1] == '\n' || (cur >= 1 && text[cur - 1] == '\r')) {
                    pc++;
                    break;
                }
                goto backtrack;
            }

            case RE_OP_EOL: {
                // $ matches end of text or before \r / \n
                if (cur == text_len || text[cur] == '\r' || text[cur] == '\n') {
                    pc++;
                    break;
                }
                goto backtrack;
            }

            case RE_OP_WORD_BOUNDARY: {
                bool prev_w = (cur > 0) && is_word_char((unsigned char)text[cur - 1]);
                bool cur_w = (cur < text_len) && is_word_char((unsigned char)text[cur]);
                if (prev_w != cur_w) {
                    pc++;
                    break;
                }
                goto backtrack;
            }

            case RE_OP_NOT_WORD_BOUNDARY: {
                bool prev_w = (cur > 0) && is_word_char((unsigned char)text[cur - 1]);
                bool cur_w = (cur < text_len) && is_word_char((unsigned char)text[cur]);
                if (prev_w == cur_w) {
                    pc++;
                    break;
                }
                goto backtrack;
            }

            case RE_OP_SPLIT: {
                // Greedy: try target1, save target2
                if (sp < MAX_BACKTRACK_STACK) {
                    stack[sp].pc = in->u.split.target2;
                    stack[sp].pos = cur;
                    sp++;
                }
                pc = in->u.split.target1;
                break;
            }

            case RE_OP_SPLIT_LAZY: {
                // Lazy: try target2, save target1
                if (sp < MAX_BACKTRACK_STACK) {
                    stack[sp].pc = in->u.split.target1;
                    stack[sp].pos = cur;
                    sp++;
                }
                pc = in->u.split.target2;
                break;
            }

            case RE_OP_JUMP: {
                pc = in->u.jump_target;
                break;
            }

            default:
                goto backtrack;
        }
        continue;

backtrack:
        if (sp > 0) {
            sp--;
            pc = stack[sp].pc;
            cur = stack[sp].pos;
        } else {
            break;
        }
    }

    if (found_match) {
        if (out_matched_len) *out_matched_len = max_matched_len;
        return true;
    }
    return false;
}

// Search for the first match in buffer starting from start_pos
bool regex_search_in_buffer(
    const RegexCompiled* re,
    const char* text,
    size_t text_len,
    size_t start_pos,
    bool multiline,
    size_t* out_match_pos,
    size_t* out_match_len
) {
    if (!re || !text || start_pos > text_len) return false;

    // Fast-path 1: Anchored to beginning of line (^)
    if (re->has_bol_anchor) {
        size_t cur = start_pos;
        while (cur <= text_len) {
            size_t mlen = 0;
            if (regex_match_at(re, text, text_len, cur, multiline, &mlen)) {
                if (out_match_pos) *out_match_pos = cur;
                if (out_match_len) *out_match_len = mlen;
                return true;
            }

            if (cur >= text_len) break;
            const char* nl = (const char*)memchr(&text[cur], '\n', text_len - cur);
            if (!nl) break;
            cur = (size_t)(nl - text) + 1;
        }
        return false;
    }

    // Fast-path 2: Literal prefix exists (Boyer-Moore-Horspool / AVX2 candidate jump)
    if (re->has_prefix && re->prefix_len >= 2) {
        int m = re->prefix_len;
        size_t cur = start_pos;
        size_t max_pos = (text_len >= (size_t)m) ? (text_len - m) : 0;

        while (cur <= max_pos) {
            bool prefix_matched = true;
            if (re->ignore_case) {
                for (int j = m - 1; j >= 0; j--) {
                    unsigned char c = (unsigned char)text[cur + j];
                    if (c != (unsigned char)re->prefix_lower[j] && c != (unsigned char)re->prefix_upper[j]) {
                        prefix_matched = false;
                        break;
                    }
                }
            } else {
                for (int j = m - 1; j >= 0; j--) {
                    if (text[cur + j] != re->prefix[j]) {
                        prefix_matched = false;
                        break;
                    }
                }
            }

            if (prefix_matched) {
                // If regex has \b before prefix, verify word boundary
                bool boundary_ok = true;
                if (re->has_word_boundary_prefix) {
                    bool prev_w = (cur > 0) && is_word_char((unsigned char)text[cur - 1]);
                    bool cur_w = is_word_char((unsigned char)text[cur]);
                    boundary_ok = (prev_w != cur_w);
                }

                if (boundary_ok) {
                    size_t mlen = 0;
                    if (regex_match_at(re, text, text_len, cur, multiline, &mlen)) {
                        if (out_match_pos) *out_match_pos = cur;
                        if (out_match_len) *out_match_len = mlen;
                        return true;
                    }
                }
                cur++;
            } else {
                unsigned char last_c = (unsigned char)text[cur + m - 1];
                cur += re->prefix_skip[last_c];
            }
        }
        return false;
    }

    // General regex search without prefix: scan positions
    size_t cur = start_pos;
    while (cur <= text_len) {
        size_t mlen = 0;
        if (regex_match_at(re, text, text_len, cur, multiline, &mlen)) {
            if (out_match_pos) *out_match_pos = cur;
            if (out_match_len) *out_match_len = mlen;
            return true;
        }

        if (cur >= text_len) break;
        cur++;
    }

    return false;
}
