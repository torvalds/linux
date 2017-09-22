#include <linux/kernel.h>
#include "../cache.h"
#include "progress.h"

static void null_progress__update(struct ui_progress *p __maybe_unused)
{
}

static struct ui_progress_ops null_progress__ops =
{
	.update = null_progress__update,
};

struct ui_progress_ops *ui_progress__ops = &null_progress__ops;

void ui_progress__update(struct ui_progress *p, u64 adv)
{
	u64 last = p->curr;

	p->curr += adv;

	if (p->curr >= p->next) {
		u64 nr = DIV_ROUND_UP(p->curr - last, p->step);

		p->next += nr * p->step;
		ui_progress__ops->update(p);
	}
}

void __ui_progress__init(struct ui_progress *p, u64 total,
			 const char *title, bool size)
{
	p->curr = 0;
	p->next = p->step = total / 16 ?: 1;
	p->total = total;
	p->title = title;
	p->size  = size;

	if (ui_progress__ops->init)
		ui_progress__ops->init(p);
}

void ui_progress__finish(void)
{
	if (ui_progress__ops->finish)
		ui_progress__ops->finish();
}
