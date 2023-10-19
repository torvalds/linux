// SPDX-License-Identifier: GPL-2.0
#include "../evsel.h"
#include "../sort.h"
#include "../hist.h"
#include "../helpline.h"
#include "gtk.h"

#include <signal.h>

void perf_gtk__signal(int sig)
{
	perf_gtk__exit(false);
	psignal(sig, "perf");
}

void perf_gtk__resize_window(GtkWidget *window)
{
	GdkRectangle rect;
	GdkScreen *screen;
	int monitor;
	int height;
	int width;

	screen = gtk_widget_get_screen(window);

	monitor = gdk_screen_get_monitor_at_window(screen, window->window);

	gdk_screen_get_monitor_geometry(screen, monitor, &rect);

	width	= rect.width * 3 / 4;
	height	= rect.height * 3 / 4;

	gtk_window_resize(GTK_WINDOW(window), width, height);
}

const char *perf_gtk__get_percent_color(double percent)
{
	if (percent >= MIN_RED)
		return "<span fgcolor='red'>";
	if (percent >= MIN_GREEN)
		return "<span fgcolor='dark green'>";
	return NULL;
}

#ifdef HAVE_GTK_INFO_BAR_SUPPORT
GtkWidget *perf_gtk__setup_info_bar(void)
{
	GtkWidget *info_bar;
	GtkWidget *label;
	GtkWidget *content_area;

	info_bar = gtk_info_bar_new();
	gtk_widget_set_no_show_all(info_bar, TRUE);

	label = gtk_label_new("");
	gtk_widget_show(label);

	content_area = gtk_info_bar_get_content_area(GTK_INFO_BAR(info_bar));
	gtk_container_add(GTK_CONTAINER(content_area), label);

	gtk_info_bar_add_button(GTK_INFO_BAR(info_bar), GTK_STOCK_OK,
				GTK_RESPONSE_OK);
	g_signal_connect(info_bar, "response",
			 G_CALLBACK(gtk_widget_hide), NULL);

	pgctx->info_bar = info_bar;
	pgctx->message_label = label;

	return info_bar;
}
#endif

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
