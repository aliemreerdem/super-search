#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "supersearch.h"
#include "engine.h"
#include <stdbool.h>
#include <stddef.h>

// Check if a file is a supported compressed archive (.zip, .gz, .tar.gz, etc.)
bool archive_is_supported_file(const char* file_path, const unsigned char* buffer, size_t length);

// In-memory streaming search inside an archive buffer
bool archive_search_buffer(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* archive_path,
    const unsigned char* buffer,
    size_t length,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
);

#endif // ARCHIVE_H
