#include "../cache.h"
#include "progress.h"

static void null_progress__update(u64 curr __maybe_unused,
				  u64 total __maybe_unused,
				  const char *title __maybe_unused)
{
}

static struct ui_progress_ops null_progress__ops =
{
	.update = null_progress__update,
};

struct ui_progress_ops *ui_progress__ops = &null_progress__ops;

void ui_progress__update(u64 curr, u64 total, const char *title)
{
	return ui_progress__ops->update(curr, total, title);
}

void ui_progress__finish(void)
{
	if (ui_progress__ops->finish)
		ui_progress__ops->finish();
}
