#ifndef _PERF_UI_PROGRESS_H_
#define _PERF_UI_PROGRESS_H_ 1

#include <../types.h>

void ui_progress__update(u64 curr, u64 total, const char *title);

#endif
