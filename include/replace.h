#ifndef REPLACE_H
#define REPLACE_H

#include "supersearch.h"
#include "engine.h"
#include <stdio.h>

// Generate replaced string from a snippet line by substituting the matched pattern
char* replace_generate_snippet(
    const char* snippet,
    const SearchOptions* options,
    const char* pattern,
    int pattern_len,
    const char* replacement
);

// Apply replacement to a file buffer, handle backup if enabled, and write atomically to disk if in_place is true
bool replace_process_file_buffer(
    const SearchOptions* options,
    const FileEntry* entry,
    const char* buffer,
    size_t buffer_len,
    const SearchEngineContext* ctx,
    size_t* out_replacements,
    bool* out_file_modified,
    bool* out_backup_created
);

// Print unified diff hunk for a modified/previewed match
void replace_print_unified_diff(
    FILE* fp,
    const MatchRecord* match,
    const SearchOptions* options,
    bool use_color
);

#endif // REPLACE_H
