#include <stdio.h>
#include <string.h>

#include "gtk.h"
#include "../ui.h"
#include "../helpline.h"
#include "../../util/debug.h"

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

int perf_gtk__show_helpline(const char *fmt, va_list ap)
{
	int ret;
	char *ptr;
	static int backlog;

	ret = vscnprintf(ui_helpline__current + backlog,
			 sizeof(ui_helpline__current) - backlog, fmt, ap);
	backlog += ret;

	/* only first line can be displayed */
	ptr = strchr(ui_helpline__current, '\n');
	if (ptr && (ptr - ui_helpline__current) <= backlog) {
		*ptr = '\0';
		ui_helpline__puts(ui_helpline__current);
		backlog = 0;
	}

	return ret;
}
