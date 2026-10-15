#ifndef REPORT_H
#define REPORT_H

#include "supersearch.h"

// Print the pre-search inspection plan to the console
void report_print_execution_plan(
    const SearchOptions* options,
    const FileList* list,
    const ExecutionPlan* plan,
    double inspection_ms
);

// Print all search matches (filenames and content snippets with highlighted terms)
void report_print_matches(const SearchOptions* options, const SearchReport* report);

// Print post-search performance and statistical summary
void report_print_summary(const SearchReport* report);

// Export report to plain text file
bool report_export_text(const SearchReport* report, const char* filepath);

// Export report to structured JSON file
bool report_export_json(const SearchReport* report, const char* filepath);

// AI Agent & Machine-readable outputs
void report_serialize_json_stream(FILE* fp, const SearchReport* report);
void report_print_json_stdout(const SearchReport* report);
void report_print_json_plan(const SearchOptions* options, const FileList* list, const ExecutionPlan* plan, double inspection_ms);
void report_print_file_list(const SearchOptions* options, const FileList* list, double inspection_ms);
void report_print_mcp_schema(void);
void report_print_mcp_config(const char* prog_path);

// Free all resources in search report
void report_free(SearchReport* report);

#endif // REPORT_H
