// SPDX-License-Identifier: GPL-2.0
#include "../util.h"
#include "gtk.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/compiler.h>
#include <linux/zalloc.h>

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

	zfree(ctx);
	return 0;
}

/*
 * perf_gtk__error() can be called re-entrantly, since the dialog isn't
 * modal and its nested loop still pumps events for the main window.
 * Track every currently running loop instead of a single pointer, so a
 * nested call can't clobber an outer call's loop and leak it.
 */
static GSList *perf_gtk__error_loops;

static void perf_gtk__quit_loop(gpointer data, gpointer user_data __maybe_unused)
{
	g_main_loop_quit(data);
}

void perf_gtk__quit_error_dialog(void)
{
	g_slist_foreach(perf_gtk__error_loops, perf_gtk__quit_loop, NULL);
}

static void perf_gtk__dialog_response(GtkDialog *dialog,
				      gint response_id __maybe_unused,
				      gpointer data __maybe_unused)
{
	gtk_window_destroy(GTK_WINDOW(dialog));
}

static int perf_gtk__error(const char *format, va_list args)
{
	char *msg;
	GtkWidget *dialog;
	GMainLoop *loop;
	va_list args_copy;

	va_copy(args_copy, args);
	if (!perf_gtk__is_active_context(pgctx) ||
	    vasprintf(&msg, format, args_copy) < 0) {
		va_end(args_copy);
		fprintf(stderr, "Error:\n");
		vfprintf(stderr, format, args);
		fprintf(stderr, "\n");
		return -1;
	}
	va_end(args_copy);

	dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(pgctx->main_window),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					"<b>Error</b>\n\n%s", msg);

	/*
	 * "response" only fires when a button is clicked; DESTROY_WITH_PARENT
	 * destroys the dialog directly without it. Quit from "destroy"
	 * instead, which fires either way, so the nested loop below can't
	 * outlive the dialog and hang.
	 */
	loop = g_main_loop_new(NULL, FALSE);
	perf_gtk__error_loops = g_slist_prepend(perf_gtk__error_loops, loop);
	g_signal_connect(dialog, "response",
			 G_CALLBACK(perf_gtk__dialog_response), NULL);
	g_signal_connect_swapped(dialog, "destroy",
				 G_CALLBACK(g_main_loop_quit), loop);

	gtk_widget_set_visible(dialog, TRUE);
	g_main_loop_run(loop);
	perf_gtk__error_loops = g_slist_remove(perf_gtk__error_loops, loop);
	g_main_loop_unref(loop);

	free(msg);
	return 0;
}

static int perf_gtk__warning_info_bar(const char *format, va_list args)
{
	char *msg;
	va_list args_copy;

	va_copy(args_copy, args);
	if (!perf_gtk__is_active_context(pgctx) ||
	    vasprintf(&msg, format, args_copy) < 0) {
		va_end(args_copy);
		fprintf(stderr, "Warning:\n");
		vfprintf(stderr, format, args);
		fprintf(stderr, "\n");
		return -1;
	}
	va_end(args_copy);

	gtk_label_set_text(GTK_LABEL(pgctx->message_label), msg);
	gtk_info_bar_set_message_type(GTK_INFO_BAR(pgctx->info_bar),
				      GTK_MESSAGE_WARNING);
	gtk_widget_set_visible(pgctx->info_bar, TRUE);

	free(msg);
	return 0;
}

struct perf_error_ops perf_gtk_eops = {
	.error		= perf_gtk__error,
	.warning	= perf_gtk__warning_info_bar,
};
