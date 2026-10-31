// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <stdio.h>

#include "gtk.h"
#include "../progress.h"
#include <linux/compiler.h>

static GtkWidget *dialog;
static GtkWidget *progress;

static void gtk_ui_progress__destroyed(GtkWidget *widget __maybe_unused,
					gpointer data __maybe_unused)
{
	dialog = NULL;
	progress = NULL;
}

static void gtk_ui_progress__update(struct ui_progress *p)
{
	double fraction = p->total ? 1.0 * p->curr / p->total : 0.0;
	char buf[1024];

	if (dialog == NULL) {
		GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
		GtkWidget *label = gtk_label_new(p->title);

		dialog = gtk_window_new();
		progress = gtk_progress_bar_new();

		gtk_widget_set_vexpand(label, TRUE);
		gtk_box_append(GTK_BOX(vbox), label);
		gtk_widget_set_vexpand(progress, TRUE);
		gtk_box_append(GTK_BOX(vbox), progress);

		gtk_window_set_child(GTK_WINDOW(dialog), vbox);

		g_signal_connect(dialog, "destroy",
				 G_CALLBACK(gtk_ui_progress__destroyed), NULL);

		gtk_window_set_title(GTK_WINDOW(dialog), "perf");
		gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 80);

		gtk_widget_set_visible(dialog, TRUE);
	}

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), fraction);
	snprintf(buf, sizeof(buf), "%"PRIu64" / %"PRIu64, p->curr, p->total);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress), buf);

	/* we didn't start a main loop yet, so pump events manually */
	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, FALSE);
}

static void gtk_ui_progress__finish(void)
{
	if (dialog == NULL)
		return;

	/* this will also destroy all of its children */
	gtk_window_destroy(GTK_WINDOW(dialog));

	dialog = NULL;
}

static struct ui_progress_ops gtk_ui_progress__ops = {
	.update		= gtk_ui_progress__update,
	.finish		= gtk_ui_progress__finish,
};

void gtk_ui_progress__init(void)
{
	ui_progress__ops = &gtk_ui_progress__ops;
}
