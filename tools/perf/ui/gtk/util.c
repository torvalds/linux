#include "../util.h"
#include "../../util/debug.h"
#include "gtk.h"

#include <string.h>


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

static int perf_gtk__error(const char *format, va_list args)
{
	char *msg;
	GtkWidget *dialog;

	if (!perf_gtk__is_active_context(pgctx) ||
	    vasprintf(&msg, format, args) < 0) {
		fprintf(stderr, "Error:\n");
		vfprintf(stderr, format, args);
		fprintf(stderr, "\n");
		return -1;
	}

	dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(pgctx->main_window),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					"<b>Error</b>\n\n%s", msg);
	gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_widget_destroy(dialog);
	free(msg);
	return 0;
}

#ifdef HAVE_GTK_INFO_BAR_SUPPORT
static int perf_gtk__warning_info_bar(const char *format, va_list args)
{
	char *msg;

	if (!perf_gtk__is_active_context(pgctx) ||
	    vasprintf(&msg, format, args) < 0) {
		fprintf(stderr, "Warning:\n");
		vfprintf(stderr, format, args);
		fprintf(stderr, "\n");
		return -1;
	}

	gtk_label_set_text(GTK_LABEL(pgctx->message_label), msg);
	gtk_info_bar_set_message_type(GTK_INFO_BAR(pgctx->info_bar),
				      GTK_MESSAGE_WARNING);
	gtk_widget_show(pgctx->info_bar);

	free(msg);
	return 0;
}
#else
static int perf_gtk__warning_statusbar(const char *format, va_list args)
{
	char *msg, *p;

	if (!perf_gtk__is_active_context(pgctx) ||
	    vasprintf(&msg, format, args) < 0) {
		fprintf(stderr, "Warning:\n");
		vfprintf(stderr, format, args);
		fprintf(stderr, "\n");
		return -1;
	}

	gtk_statusbar_pop(GTK_STATUSBAR(pgctx->statbar),
			  pgctx->statbar_ctx_id);

	/* Only first line can be displayed */
	p = strchr(msg, '\n');
	if (p)
		*p = '\0';

	gtk_statusbar_push(GTK_STATUSBAR(pgctx->statbar),
			   pgctx->statbar_ctx_id, msg);

	free(msg);
	return 0;
}
#endif

struct perf_error_ops perf_gtk_eops = {
	.error		= perf_gtk__error,
#ifdef HAVE_GTK_INFO_BAR_SUPPORT
	.warning	= perf_gtk__warning_info_bar,
#else
	.warning	= perf_gtk__warning_statusbar,
#endif
};
