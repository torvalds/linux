#ifndef _PERF_UI_PROGRESS_H_
#define _PERF_UI_PROGRESS_H_ 1

#include <../types.h>

void ui_progress__finish(void);
 
struct ui_progress {
	const char *title;
	u64 curr, next, step, total;
};
 
void ui_progress__init(struct ui_progress *p, u64 total, const char *title);
void ui_progress__update(struct ui_progress *p, u64 adv);

struct ui_progress_ops {
	void (*update)(struct ui_progress *p);
	void (*finish)(void);
};

extern struct ui_progress_ops *ui_progress__ops;

#endif
