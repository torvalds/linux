#ifndef _PERF_UI_PROGRESS_H_
#define _PERF_UI_PROGRESS_H_ 1

#include <../types.h>

struct ui_progress {
	void (*update)(u64, u64, const char *);
};

extern struct ui_progress *progress_fns;

void ui_progress__init(void);

void ui_progress__update(u64 curr, u64 total, const char *title);

#endif
