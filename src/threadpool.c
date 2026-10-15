#include "threadpool.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct {
    int thread_id;
    const SearchOptions* options;
    const ExecutionPlan* plan;
    const SearchEngineContext* ctx;
    FileList* file_list;
    volatile LONG* next_file_idx;
    size_t total_files;
    size_t chunk_size;
    ThreadResult* result;
    volatile LONG* global_match_count;
    volatile LONG* stop_flag;
} WorkerArgs;

static void worker_run_loop(WorkerArgs* args) {
    double start_time = timer_now_ms();

    while (true) {
        if (args->stop_flag && *args->stop_flag) {
            break;
        }

        // Atomic lock-free chunk reservation
        LONG start = InterlockedExchangeAdd(args->next_file_idx, (LONG)args->chunk_size);
        if (start >= (LONG)args->total_files) {
            break;
        }

        LONG end = start + (LONG)args->chunk_size;
        if (end > (LONG)args->total_files) {
            end = (LONG)args->total_files;
        }

        for (LONG i = start; i < end; i++) {
            if (args->stop_flag && *args->stop_flag) {
                break;
            }
            FileEntry* entry = &args->file_list->items[i];
            engine_search_file(args->ctx, args->options, entry, args->result, args->global_match_count, args->stop_flag);
        }
    }

    args->result->active_time_ms = timer_now_ms() - start_time;
}

#ifdef _WIN32
static DWORD WINAPI worker_thread_proc(LPVOID lpParam) {
    worker_run_loop((WorkerArgs*)lpParam);
    return 0;
}
#else
static void* worker_thread_proc_posix(void* lpParam) {
    worker_run_loop((WorkerArgs*)lpParam);
    return NULL;
}
#endif

bool threadpool_execute_search(
    const SearchOptions* options,
    const ExecutionPlan* plan,
    FileList* list,
    SearchReport* report
) {
    if (!options || !plan || !list || !report) return false;

    // Initialize search context
    SearchEngineContext ctx;
    engine_init_context(&ctx, options);

    int num_threads = plan->worker_threads;
    if (num_threads < 1) num_threads = 1;
    if (num_threads > 64) num_threads = 64;

    // Allocate thread local results and arguments
    ThreadResult* results = (ThreadResult*)safe_calloc(num_threads, sizeof(ThreadResult));
    WorkerArgs* args = (WorkerArgs*)safe_calloc(num_threads, sizeof(WorkerArgs));

    volatile LONG next_file_idx = 0;
    volatile LONG global_match_count = 0;
    volatile LONG stop_flag = 0;

    size_t chunk_size = plan->dynamic_chunk_size;
    if (chunk_size < 1) chunk_size = 1;

    double search_start = timer_now_ms();

#ifdef _WIN32
    HANDLE* thread_handles = (HANDLE*)safe_calloc(num_threads, sizeof(HANDLE));

    for (int t = 0; t < num_threads; t++) {
        args[t].thread_id = t;
        args[t].options = options;
        args[t].plan = plan;
        args[t].ctx = &ctx;
        args[t].file_list = list;
        args[t].next_file_idx = &next_file_idx;
        args[t].total_files = list->count;
        args[t].chunk_size = chunk_size;
        args[t].result = &results[t];
        args[t].global_match_count = &global_match_count;
        args[t].stop_flag = &stop_flag;

        thread_handles[t] = CreateThread(
            NULL,
            0,
            worker_thread_proc,
            &args[t],
            0,
            NULL
        );

        if (!thread_handles[t]) {
            fprintf(stderr, "Warning: Failed to spawn worker thread %d\n", t);
        }
    }

    WaitForMultipleObjects(num_threads, thread_handles, TRUE, INFINITE);

    double search_elapsed = timer_now_ms() - search_start;
    report->search_time_ms = search_elapsed;

    for (int t = 0; t < num_threads; t++) {
        if (thread_handles[t]) {
            CloseHandle(thread_handles[t]);
        }
    }
    free(thread_handles);
#else
    pthread_t* threads = (pthread_t*)safe_calloc(num_threads, sizeof(pthread_t));
    bool* thread_started = (bool*)safe_calloc(num_threads, sizeof(bool));

    for (int t = 0; t < num_threads; t++) {
        args[t].thread_id = t;
        args[t].options = options;
        args[t].plan = plan;
        args[t].ctx = &ctx;
        args[t].file_list = list;
        args[t].next_file_idx = &next_file_idx;
        args[t].total_files = list->count;
        args[t].chunk_size = chunk_size;
        args[t].result = &results[t];
        args[t].global_match_count = &global_match_count;
        args[t].stop_flag = &stop_flag;

        if (pthread_create(&threads[t], NULL, worker_thread_proc_posix, &args[t]) == 0) {
            thread_started[t] = true;
        } else {
            fprintf(stderr, "Warning: Failed to spawn worker thread %d\n", t);
        }
    }

    for (int t = 0; t < num_threads; t++) {
        if (thread_started[t]) {
            pthread_join(threads[t], NULL);
        }
    }

    double search_elapsed = timer_now_ms() - search_start;
    report->search_time_ms = search_elapsed;

    free(threads);
    free(thread_started);
#endif
    free(args);

    // Aggregate thread metrics and results
    report->files_scanned = 0;
    report->bytes_scanned = 0;
    report->binary_skipped = 0;
    report->error_count = 0;
    report->content_matches = 0;
    report->truncated = (stop_flag != 0);

    size_t total_match_count = 0;
    report->total_replacements = 0;
    report->files_modified = 0;
    report->backups_created = 0;
    for (int t = 0; t < num_threads; t++) {
        report->files_scanned += results[t].files_scanned;
        report->bytes_scanned += results[t].bytes_scanned;
        report->binary_skipped += results[t].binary_skipped;
        report->error_count += results[t].error_count;
        report->content_matches += results[t].match_count;
        report->total_replacements += results[t].replacement_count;
        report->files_modified += results[t].files_modified;
        report->backups_created += results[t].backups_created;
        total_match_count += results[t].match_count;
    }

    // Also include filename matches
    report->filename_matches = list->filename_matches;
    total_match_count += list->filename_matches;

    report->total_matches = total_match_count;

    // Cap output at max_results if specified
    size_t alloc_matches = total_match_count;
    if (options->max_results > 0 && alloc_matches > options->max_results) {
        alloc_matches = options->max_results;
        report->truncated = true;
    }

    // Allocate unified master matches list
    if (alloc_matches > 0) {
        report->all_matches = (MatchRecord*)safe_calloc(alloc_matches, sizeof(MatchRecord));
        report->all_matches_capacity = alloc_matches;
        report->all_matches_count = 0;

        // 1. Add filename matches first
        for (size_t i = 0; i < list->count && report->all_matches_count < alloc_matches; i++) {
            if (list->items[i].name_matched) {
                MatchRecord* m = &report->all_matches[report->all_matches_count++];
                m->file_path = safe_strdup(list->items[i].path_utf8);
                m->line_number = 0;
                m->col_number = 0;
                m->line_text = safe_strdup("[MATCH IN FILE/DIRECTORY NAME]");
                m->match_len = options->query_len;
                m->is_filename_match = true;
                m->lines_before = NULL;
                m->lines_before_count = 0;
                m->lines_after = NULL;
                m->lines_after_count = 0;
            }
        }

        // 2. Add content matches from each thread
        for (int t = 0; t < num_threads; t++) {
            for (size_t m = 0; m < results[t].match_count; m++) {
                MatchRecord* src = &results[t].matches[m];

                if (report->all_matches_count < alloc_matches) {
                    MatchRecord* dst = &report->all_matches[report->all_matches_count++];
                    dst->file_path = src->file_path; // transfer ownership
                    dst->line_number = src->line_number;
                    dst->col_number = src->col_number;
                    dst->line_text = src->line_text; // transfer ownership
                    dst->match_len = src->match_len;
                    dst->is_filename_match = false;
                    dst->lines_before = src->lines_before;
                    dst->lines_before_count = src->lines_before_count;
                    dst->lines_after = src->lines_after;
                    dst->lines_after_count = src->lines_after_count;
                    dst->replaced_text = src->replaced_text; // transfer ownership
                    dst->enclosing_func = src->enclosing_func; // transfer ownership
                    dst->enclosing_line = src->enclosing_line;
                } else {
                    // Match omitted due to max_results truncation -> free memory
                    report->truncated = true;
                    free(src->file_path);
                    free(src->line_text);
                    if (src->replaced_text) free(src->replaced_text);
                    if (src->enclosing_func) free(src->enclosing_func);
                    if (src->lines_before) {
                        for (int k = 0; k < src->lines_before_count; k++) free(src->lines_before[k]);
                        free(src->lines_before);
                    }
                    if (src->lines_after) {
                        for (int k = 0; k < src->lines_after_count; k++) free(src->lines_after[k]);
                        free(src->lines_after);
                    }
                }
            }
            free(results[t].matches);
        }
    } else {
        // Free matches if alloc_matches was 0
        for (int t = 0; t < num_threads; t++) {
            for (size_t m = 0; m < results[t].match_count; m++) {
                MatchRecord* src = &results[t].matches[m];
                free(src->file_path);
                free(src->line_text);
                if (src->replaced_text) free(src->replaced_text);
                if (src->enclosing_func) free(src->enclosing_func);
                if (src->lines_before) {
                    for (int k = 0; k < src->lines_before_count; k++) free(src->lines_before[k]);
                    free(src->lines_before);
                }
                if (src->lines_after) {
                    for (int k = 0; k < src->lines_after_count; k++) free(src->lines_after[k]);
                    free(src->lines_after);
                }
            }
            free(results[t].matches);
        }
    }

    // Calculate files with matches
    size_t files_with_matches = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].match_count > 0 || list->items[i].name_matched) {
            files_with_matches++;
        }
    }
    report->files_with_matches = files_with_matches;

    // Calculate throughput
    double search_sec = search_elapsed / 1000.0;
    if (search_sec > 0.000001) {
        report->throughput_mb_s = ((double)report->bytes_scanned / (1024.0 * 1024.0)) / search_sec;
        report->files_per_sec = (double)report->files_scanned / search_sec;
    } else {
        report->throughput_mb_s = 0.0;
        report->files_per_sec = 0.0;
    }

    free(results);
    engine_free_context(&ctx);
    return true;
}
