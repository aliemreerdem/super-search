#ifndef CACHE_H
#define CACHE_H

#include "supersearch.h"

// Check if a valid cache exists for target_dir and options, and load it into list
// Returns true if cache was successfully loaded and is valid
bool cache_load_file_list(const SearchOptions* options, FileList* list);

// Save the given FileList to persistent disk cache for target_dir and options
bool cache_save_file_list(const SearchOptions* options, const FileList* list);

// Invalidate/delete cache for target_dir
bool cache_clear(const char* target_dir);

// Get cache file path for target_dir
bool cache_get_path(const char* target_dir, char* out_cache_path, size_t max_len);

// Start background directory watcher thread using ReadDirectoryChangesW
// Returns watcher pointer or NULL on failure
void* cache_watcher_start(const char* target_dir, FileList* list);

// Stop background directory watcher
void cache_watcher_stop(void* watcher_handle);

#endif // CACHE_H
