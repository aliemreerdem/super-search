#ifndef INTERACTIVE_H
#define INTERACTIVE_H

#include "supersearch.h"

// Run interactive full-screen TUI search interface
// Returns exit code: 0 on selected match output, 1 on exit/cancel
int interactive_run(SearchOptions* options);

#endif // INTERACTIVE_H
