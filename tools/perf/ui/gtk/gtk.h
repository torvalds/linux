#ifndef _PERF_GTK_H_
#define _PERF_GTK_H_ 1

#include <stdbool.h>

#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <gtk/gtk.h>
#pragma GCC diagnostic error "-Wstrict-prototypes"


struct perf_gtk_context {
	GtkWidget *main_window;
	GtkWidget *notebook;

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

void perf_gtk__init_helpline(void);
void perf_gtk__init_progress(void);
void perf_gtk__init_hpp(void);

void perf_gtk__signal(int sig);
void perf_gtk__resize_window(GtkWidget *window);
const char *perf_gtk__get_percent_color(double percent);
GtkWidget *perf_gtk__setup_statusbar(void);

#ifdef HAVE_GTK_INFO_BAR
GtkWidget *perf_gtk__setup_info_bar(void);
#else
static inline GtkWidget *perf_gtk__setup_info_bar(void)
{
	return NULL;
}
#endif

#endif /* _PERF_GTK_H_ */
