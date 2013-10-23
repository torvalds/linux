#ifndef _PERF_UI_PROGRESS_H_
#define _PERF_UI_PROGRESS_H_ 1

#include <../types.h>

struct ui_progress_ops {
	void (*update)(u64, u64, const char *);
	void (*finish)(void);
};

extern struct ui_progress_ops *ui_progress__ops;

void ui_progress__update(u64 curr, u64 total, const char *title);
void ui_progress__finish(void);

#endif
