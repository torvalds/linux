// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>

#include "gtk.h"
#include "../progress.h"
#include "util.h"

static GtkWidget *dialog;
static GtkWidget *progress;

static void gtk_ui_progress__update(struct ui_progress *p)
{
	double fraction = p->total ? 1.0 * p->curr / p->total : 0.0;
	char buf[1024];

	if (dialog == NULL) {
		GtkWidget *vbox = gtk_vbox_new(TRUE, 5);
		GtkWidget *label = gtk_label_new(p->title);

		dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		progress = gtk_progress_bar_new();

		gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, FALSE, 3);
		gtk_box_pack_start(GTK_BOX(vbox), progress, TRUE, TRUE, 3);

		gtk_container_add(GTK_CONTAINER(dialog), vbox);

		gtk_window_set_title(GTK_WINDOW(dialog), "perf");
		gtk_window_resize(GTK_WINDOW(dialog), 300, 80);
		gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

		gtk_widget_show_all(dialog);
	}

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), fraction);
	snprintf(buf, sizeof(buf), "%"PRIu64" / %"PRIu64, p->curr, p->total);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress), buf);

	/* we didn't call gtk_main yet, so do it manually */
	while (gtk_events_pending())
		gtk_main_iteration();
}

static void gtk_ui_progress__finish(void)
{
	/* this will also destroy all of its children */
	gtk_widget_destroy(dialog);

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
