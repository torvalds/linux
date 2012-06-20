#ifndef _PERF_GTK_H_
#define _PERF_GTK_H_ 1

#include <stdbool.h>

#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <gtk/gtk.h>
#pragma GCC diagnostic error "-Wstrict-prototypes"


struct perf_gtk_context {
	GtkWidget *main_window;

#ifdef HAVE_GTK_INFO_BAR
	GtkWidget *info_bar;
	GtkWidget *message_label;
#endif
	GtkWidget *statbar;
	guint statbar_ctx_id;
};

extern struct perf_gtk_context *pgctx;

static inline bool perf_gtk__is_active_context(struct perf_gtk_context *ctx)
{
	return ctx && ctx->main_window;
}

struct perf_gtk_context *perf_gtk__activate_context(GtkWidget *window);
int perf_gtk__deactivate_context(struct perf_gtk_context **ctx);

#ifndef HAVE_GTK_INFO_BAR
static inline GtkWidget *perf_gtk__setup_info_bar(void)
{
	return NULL;
}
#endif

#endif /* _PERF_GTK_H_ */
