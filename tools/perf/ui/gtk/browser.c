// SPDX-License-Identifier: GPL-2.0
#include "gtk.h"
#include "../evsel.h"
#include "../sort.h"
#include "../hist.h"
#include "../helpline.h"

#include <glib-unix.h>
#include <signal.h>

/*
 * SIGINT/SIGQUIT/SIGTERM are asynchronous: deferring the actual exit to a
 * GLib source dispatched from the main loop means it always runs on the
 * main thread, serialized with everything else the main loop does
 * (including perf_gtk__error()'s updates to perf_gtk__error_loops), instead
 * of racing them from arbitrary signal-handler context.
 */
static gboolean perf_gtk__quit_signal(gpointer data __maybe_unused)
{
	perf_gtk__exit(false);
	return G_SOURCE_REMOVE;
}

void perf_gtk__install_quit_signals(void)
{
	g_unix_signal_add(SIGINT,  perf_gtk__quit_signal, NULL);
	g_unix_signal_add(SIGQUIT, perf_gtk__quit_signal, NULL);
	g_unix_signal_add(SIGTERM, perf_gtk__quit_signal, NULL);
}

/*
 * SIGSEGV/SIGFPE are synchronous faults: there's no "later" to defer to,
 * and no safe way to run GTK/GLib code (or anything else non-async-signal-
 * safe) from the faulting context. Report and let the default disposition
 * (core dump) happen instead of trying to tear down GTK state here.
 */
void perf_gtk__fatal_signal(int sig)
{
	psignal(sig, "perf");
	signal(sig, SIG_DFL);
	raise(sig);
}

void perf_gtk__resize_window(GtkWidget *window)
{
	GdkRectangle rect;
	GdkMonitor *monitor;
	GdkDisplay *display;
	GListModel *monitors;
	int height;
	int width;

	display = gtk_widget_get_display(window);
	monitors = gdk_display_get_monitors(display);
	monitor = g_list_model_get_item(monitors, 0);
	if (!monitor) {
		gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
		return;
	}

	gdk_monitor_get_geometry(monitor, &rect);
	g_object_unref(monitor);

	width	= rect.width * 3 / 4;
	height	= rect.height * 3 / 4;

	gtk_window_set_default_size(GTK_WINDOW(window), width, height);
}

static GMainLoop *perf_gtk__main_loop;

void perf_gtk__quit_main_loop(void)
{
	if (perf_gtk__main_loop)
		g_main_loop_quit(perf_gtk__main_loop);
}

static gboolean perf_gtk__close_request(GtkWidget *widget __maybe_unused,
					gpointer data __maybe_unused)
{
	perf_gtk__quit_main_loop();

	return FALSE;
}

void perf_gtk__run_main_loop(GtkWidget *window)
{
	g_signal_connect(window, "close-request",
			 G_CALLBACK(perf_gtk__close_request), NULL);

	perf_gtk__main_loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(perf_gtk__main_loop);
	g_clear_pointer(&perf_gtk__main_loop, g_main_loop_unref);
}

const char *perf_gtk__get_percent_color(double percent)
{
	if (percent >= MIN_RED)
		return "<span fgcolor='red'>";
	if (percent >= MIN_GREEN)
		return "<span fgcolor='dark green'>";
	return NULL;
}

static void perf_gtk__hide_widget(GtkWidget *widget, gint response_id __maybe_unused,
				   gpointer data __maybe_unused)
{
	gtk_widget_set_visible(widget, FALSE);
}

GtkWidget *perf_gtk__setup_info_bar(void)
{
	GtkWidget *info_bar;
	GtkWidget *label;

	info_bar = gtk_info_bar_new();
	gtk_widget_set_visible(info_bar, FALSE);

	label = gtk_label_new("");
	gtk_widget_set_visible(label, TRUE);

	gtk_info_bar_add_child(GTK_INFO_BAR(info_bar), label);

	gtk_info_bar_add_button(GTK_INFO_BAR(info_bar), "_OK",
				GTK_RESPONSE_OK);
	g_signal_connect(info_bar, "response",
			 G_CALLBACK(perf_gtk__hide_widget), NULL);

	pgctx->info_bar = info_bar;
	pgctx->message_label = label;

	return info_bar;
}

GtkWidget *perf_gtk__setup_statusbar(void)
{
	GtkWidget *stbar;
	unsigned ctxid;

	stbar = gtk_statusbar_new();

	ctxid = gtk_statusbar_get_context_id(GTK_STATUSBAR(stbar),
					     "perf report");
	pgctx->statbar = stbar;
	pgctx->statbar_ctx_id = ctxid;

	return stbar;
}
