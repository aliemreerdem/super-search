#ifndef SCANNER_H
#define SCANNER_H

#include "supersearch.h"

// Initialize and free file list
void file_list_init(FileList* list);
void file_list_free(FileList* list);
void file_list_add(FileList* list, const wchar_t* path_w, const char* path_utf8, uint64_t size, bool name_match);

// Directory inspection and file discovery
bool scanner_inspect_directory(const SearchOptions* options, FileList* list, double* elapsed_ms);

// Plan generation based on inspection metrics
ExecutionPlan scanner_create_plan(const SearchOptions* options, FileList* list);

// Sort file list for optimal parallel scheduling (e.g., LPT - largest files first)
void scanner_sort_files_for_execution(FileList* list);

#endif // SCANNER_H
