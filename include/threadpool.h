#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "supersearch.h"
#include "engine.h"

// Run parallel search across all files in list based on execution plan
bool threadpool_execute_search(
    const SearchOptions* options,
    const ExecutionPlan* plan,
    FileList* list,
    SearchReport* report
);

#endif // THREADPOOL_H
