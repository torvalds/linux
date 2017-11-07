// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include "../cache.h"
#include "../progress.h"
#include "../libslang.h"
#include "../ui.h"
#include "tui.h"
#include "units.h"
#include "../browser.h"

static void __tui_progress__init(struct ui_progress *p)
{
	p->next = p->step = p->total / (SLtt_Screen_Cols - 2) ?: 1;
}

static int get_title(struct ui_progress *p, char *buf, size_t size)
{
	char buf_cur[20];
	char buf_tot[20];
	int ret;

	ret  = unit_number__scnprintf(buf_cur, sizeof(buf_cur), p->curr);
	ret += unit_number__scnprintf(buf_tot, sizeof(buf_tot), p->total);

	return ret + scnprintf(buf, size, "%s [%s/%s]",
			       p->title, buf_cur, buf_tot);
}

static void tui_progress__update(struct ui_progress *p)
{
	char buf[100], *title = (char *) p->title;
	int bar, y;
	/*
	 * FIXME: We should have a per UI backend way of showing progress,
	 * stdio will just show a percentage as NN%, etc.
	 */
	if (use_browser <= 0)
		return;

	if (p->total == 0)
		return;

	if (p->size) {
		get_title(p, buf, sizeof(buf));
		title = buf;
	}

	ui__refresh_dimensions(false);
	pthread_mutex_lock(&ui__lock);
	y = SLtt_Screen_Rows / 2 - 2;
	SLsmg_set_color(0);
	SLsmg_draw_box(y, 0, 3, SLtt_Screen_Cols);
	SLsmg_gotorc(y++, 1);
	SLsmg_write_string(title);
	SLsmg_fill_region(y, 1, 1, SLtt_Screen_Cols - 2, ' ');
	SLsmg_set_color(HE_COLORSET_SELECTED);
	bar = ((SLtt_Screen_Cols - 2) * p->curr) / p->total;
	SLsmg_fill_region(y, 1, 1, bar, ' ');
	SLsmg_refresh();
	pthread_mutex_unlock(&ui__lock);
}

static void tui_progress__finish(void)
{
	int y;

	if (use_browser <= 0)
		return;

	ui__refresh_dimensions(false);
	pthread_mutex_lock(&ui__lock);
	y = SLtt_Screen_Rows / 2 - 2;
	SLsmg_set_color(0);
	SLsmg_fill_region(y, 0, 3, SLtt_Screen_Cols, ' ');
	SLsmg_refresh();
	pthread_mutex_unlock(&ui__lock);
}

static struct ui_progress_ops tui_progress__ops = {
	.init   = __tui_progress__init,
	.update = tui_progress__update,
	.finish = tui_progress__finish,
};

void tui_progress__init(void)
{
	ui_progress__ops = &tui_progress__ops;
}
