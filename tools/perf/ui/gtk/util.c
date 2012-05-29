#include "../util.h"
#include "../../util/debug.h"
#include "gtk.h"


struct perf_gtk_context *pgctx;

struct perf_gtk_context *perf_gtk__activate_context(GtkWidget *window)
{
	struct perf_gtk_context *ctx;

	ctx = malloc(sizeof(*pgctx));
	if (ctx)
		ctx->main_window = window;

	return ctx;
}

int perf_gtk__deactivate_context(struct perf_gtk_context **ctx)
{
	if (!perf_gtk__is_active_context(*ctx))
		return -1;

	free(*ctx);
	*ctx = NULL;
	return 0;
}

/*
 * FIXME: Functions below should be implemented properly.
 *        For now, just add stubs for NO_NEWT=1 build.
 */
#ifdef NO_NEWT_SUPPORT
int ui_helpline__show_help(const char *format __used, va_list ap __used)
{
	return 0;
}

void ui_progress__update(u64 curr __used, u64 total __used,
			 const char *title __used)
{
}
#endif
