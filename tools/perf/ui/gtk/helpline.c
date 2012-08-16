#include "gtk.h"
#include "../helpline.h"

static void gtk_helpline_pop(void)
{
	if (!perf_gtk__is_active_context(pgctx))
		return;

	gtk_statusbar_pop(GTK_STATUSBAR(pgctx->statbar),
			  pgctx->statbar_ctx_id);
}

static void gtk_helpline_push(const char *msg)
{
	if (!perf_gtk__is_active_context(pgctx))
		return;

	gtk_statusbar_push(GTK_STATUSBAR(pgctx->statbar),
			   pgctx->statbar_ctx_id, msg);
}

static struct ui_helpline gtk_helpline_fns = {
	.pop	= gtk_helpline_pop,
	.push	= gtk_helpline_push,
};

void perf_gtk__init_helpline(void)
{
	helpline_fns = &gtk_helpline_fns;
}
