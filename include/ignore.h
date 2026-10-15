#ifndef IGNORE_H
#define IGNORE_H

#include "supersearch.h"
#include <stdbool.h>
#include <stddef.h>

// Individual ignore pattern rule
typedef struct {
    char* pattern;          // Cleaned pattern string (normalized with forward slashes)
    bool is_negation;       // True if rule begins with '!'
    bool dir_only;          // True if rule ended with '/'
    bool match_basename;    // True if pattern does not contain '/'
    bool anchored;          // True if pattern started with '/'
} IgnoreRule;

// Scope representing rules loaded from a directory's .gitignore / .superignore
typedef struct IgnoreScope {
    struct IgnoreScope* parent;
    char* base_dir_utf8;    // Normalized base directory with trailing slash (e.g. "c:/code/project/")
    size_t base_dir_len;
    IgnoreRule* rules;
    size_t count;
    size_t capacity;
} IgnoreScope;

// Allocate a new ignore scope for a directory with an optional parent scope
IgnoreScope* ignore_scope_create(IgnoreScope* parent, const char* dir_utf8);

// Free an ignore scope (does not free parent)
void ignore_scope_free(IgnoreScope* scope);

// Add a single ignore rule string into the scope
void ignore_scope_add_rule(IgnoreScope* scope, const char* raw_line);

// Load rules from a specific ignore file (e.g., .gitignore or .superignore)
bool ignore_scope_load_file(IgnoreScope* scope, const wchar_t* file_path_w);
bool ignore_scope_load_file_utf8(IgnoreScope* scope, const char* file_path_utf8);

// Check if a path (file or directory) is ignored according to Git ignore precedence
// Returns true if ignored (skip), false if kept
bool ignore_scope_is_ignored(const IgnoreScope* scope, const char* path_utf8, bool is_dir);

#endif // IGNORE_H
