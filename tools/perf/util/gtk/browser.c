#include "../evlist.h"
#include "../cache.h"
#include "../evsel.h"
#include "../sort.h"
#include "../hist.h"
#include "gtk.h"

#include <signal.h>

#define MAX_COLUMNS			32

void perf_gtk_setup_browser(int argc, const char *argv[],
			    bool fallback_to_pager __used)
{
	gtk_init(&argc, (char ***)&argv);
}

void perf_gtk_exit_browser(bool wait_for_ok __used)
{
	gtk_main_quit();
}

static void perf_gtk_signal(int sig)
{
	psignal(sig, "perf");
	gtk_main_quit();
}

static void perf_gtk_resize_window(GtkWidget *window)
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

static void perf_gtk_show_hists(GtkWidget *window, struct hists *hists)
{
	GType col_types[MAX_COLUMNS];
	GtkCellRenderer *renderer;
	struct sort_entry *se;
	GtkListStore *store;
	struct rb_node *nd;
	u64 total_period;
	GtkWidget *view;
	int col_idx;
	int nr_cols;

	nr_cols = 0;

	/* The percentage column */
	col_types[nr_cols++] = G_TYPE_STRING;

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		if (se->elide)
			continue;

		col_types[nr_cols++] = G_TYPE_STRING;
	}

	store = gtk_list_store_newv(nr_cols, col_types);

	view = gtk_tree_view_new();

	renderer = gtk_cell_renderer_text_new();

	col_idx = 0;

	/* The percentage column */
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
						    -1, "Overhead (%)",
						    renderer, "text",
						    col_idx++, NULL);

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		if (se->elide)
			continue;

		gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
							    -1, se->se_header,
							    renderer, "text",
							    col_idx++, NULL);
	}

	gtk_tree_view_set_model(GTK_TREE_VIEW(view), GTK_TREE_MODEL(store));

	g_object_unref(GTK_TREE_MODEL(store));

	total_period = hists->stats.total_period;

	for (nd = rb_first(&hists->entries); nd; nd = rb_next(nd)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		GtkTreeIter iter;
		double percent;
		char s[512];

		if (h->filtered)
			continue;

		gtk_list_store_append(store, &iter);

		col_idx = 0;

		percent = (h->period * 100.0) / total_period;

		snprintf(s, ARRAY_SIZE(s), "%.2f", percent);

		gtk_list_store_set(store, &iter, col_idx++, s, -1);

		list_for_each_entry(se, &hist_entry__sort_list, list) {
			if (se->elide)
				continue;

			se->se_snprintf(h, s, ARRAY_SIZE(s),
					hists__col_len(hists, se->se_width_idx));

			gtk_list_store_set(store, &iter, col_idx++, s, -1);
		}
	}

	gtk_container_add(GTK_CONTAINER(window), view);
}

int perf_evlist__gtk_browse_hists(struct perf_evlist *evlist,
				  const char *help __used,
				  void (*timer) (void *arg)__used,
				  void *arg __used, int delay_secs __used)
{
	struct perf_evsel *pos;
	GtkWidget *notebook;
	GtkWidget *window;

	signal(SIGSEGV, perf_gtk_signal);
	signal(SIGFPE,  perf_gtk_signal);
	signal(SIGINT,  perf_gtk_signal);
	signal(SIGQUIT, perf_gtk_signal);
	signal(SIGTERM, perf_gtk_signal);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gtk_window_set_title(GTK_WINDOW(window), "perf report");

	g_signal_connect(window, "delete_event", gtk_main_quit, NULL);

	notebook = gtk_notebook_new();

	list_for_each_entry(pos, &evlist->entries, node) {
		struct hists *hists = &pos->hists;
		const char *evname = event_name(pos);
		GtkWidget *scrolled_window;
		GtkWidget *tab_label;

		scrolled_window = gtk_scrolled_window_new(NULL, NULL);

		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
							GTK_POLICY_AUTOMATIC,
							GTK_POLICY_AUTOMATIC);

		perf_gtk_show_hists(scrolled_window, hists);

		tab_label = gtk_label_new(evname);

		gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled_window, tab_label);
	}

	gtk_container_add(GTK_CONTAINER(window), notebook);

	gtk_widget_show_all(window);

	perf_gtk_resize_window(window);

	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

	gtk_main();

	return 0;
}
