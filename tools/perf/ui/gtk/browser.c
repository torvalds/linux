#include "../evlist.h"
#include "../cache.h"
#include "../evsel.h"
#include "../sort.h"
#include "../hist.h"
#include "../helpline.h"
#include "gtk.h"

#include <signal.h>

#define MAX_COLUMNS			32

static void perf_gtk__signal(int sig)
{
	perf_gtk__exit(false);
	psignal(sig, "perf");
}

static void perf_gtk__resize_window(GtkWidget *window)
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

static const char *perf_gtk__get_percent_color(double percent)
{
	if (percent >= MIN_RED)
		return "<span fgcolor='red'>";
	if (percent >= MIN_GREEN)
		return "<span fgcolor='dark green'>";
	return NULL;
}

#define HPP__COLOR_FN(_name, _field)						\
static int perf_gtk__hpp_color_ ## _name(struct perf_hpp *hpp,			\
					 struct hist_entry *he)			\
{										\
	double percent = 100.0 * he->_field / hpp->total_period;		\
	const char *markup;							\
	int ret = 0;								\
										\
	markup = perf_gtk__get_percent_color(percent);				\
	if (markup)								\
		ret += scnprintf(hpp->buf, hpp->size, "%s", markup);		\
	ret += scnprintf(hpp->buf + ret, hpp->size - ret, "%6.2f%%", percent); 	\
	if (markup)								\
		ret += scnprintf(hpp->buf + ret, hpp->size - ret, "</span>"); 	\
										\
	return ret;								\
}

HPP__COLOR_FN(overhead, period)
HPP__COLOR_FN(overhead_sys, period_sys)
HPP__COLOR_FN(overhead_us, period_us)
HPP__COLOR_FN(overhead_guest_sys, period_guest_sys)
HPP__COLOR_FN(overhead_guest_us, period_guest_us)

#undef HPP__COLOR_FN

void perf_gtk__init_hpp(void)
{
	perf_hpp__init(false, false);

	perf_hpp__format[PERF_HPP__OVERHEAD].color =
				perf_gtk__hpp_color_overhead;
	perf_hpp__format[PERF_HPP__OVERHEAD_SYS].color =
				perf_gtk__hpp_color_overhead_sys;
	perf_hpp__format[PERF_HPP__OVERHEAD_US].color =
				perf_gtk__hpp_color_overhead_us;
	perf_hpp__format[PERF_HPP__OVERHEAD_GUEST_SYS].color =
				perf_gtk__hpp_color_overhead_guest_sys;
	perf_hpp__format[PERF_HPP__OVERHEAD_GUEST_US].color =
				perf_gtk__hpp_color_overhead_guest_us;
}

static void perf_gtk__show_hists(GtkWidget *window, struct hists *hists)
{
	GType col_types[MAX_COLUMNS];
	GtkCellRenderer *renderer;
	struct sort_entry *se;
	GtkListStore *store;
	struct rb_node *nd;
	GtkWidget *view;
	int i, col_idx;
	int nr_cols;
	char s[512];

	struct perf_hpp hpp = {
		.buf		= s,
		.size		= sizeof(s),
		.total_period	= hists->stats.total_period,
	};

	nr_cols = 0;

	for (i = 0; i < PERF_HPP__MAX_INDEX; i++) {
		if (!perf_hpp__format[i].cond)
			continue;

		col_types[nr_cols++] = G_TYPE_STRING;
	}

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		if (se->elide)
			continue;

		col_types[nr_cols++] = G_TYPE_STRING;
	}

	store = gtk_list_store_newv(nr_cols, col_types);

	view = gtk_tree_view_new();

	renderer = gtk_cell_renderer_text_new();

	col_idx = 0;

	for (i = 0; i < PERF_HPP__MAX_INDEX; i++) {
		if (!perf_hpp__format[i].cond)
			continue;

		perf_hpp__format[i].header(&hpp);

		gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
							    -1, s,
							    renderer, "markup",
							    col_idx++, NULL);
	}

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

	for (nd = rb_first(&hists->entries); nd; nd = rb_next(nd)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		GtkTreeIter iter;

		if (h->filtered)
			continue;

		gtk_list_store_append(store, &iter);

		col_idx = 0;

		for (i = 0; i < PERF_HPP__MAX_INDEX; i++) {
			if (!perf_hpp__format[i].cond)
				continue;

			if (perf_hpp__format[i].color)
				perf_hpp__format[i].color(&hpp, h);
			else
				perf_hpp__format[i].entry(&hpp, h);

			gtk_list_store_set(store, &iter, col_idx++, s, -1);
		}

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

#ifdef HAVE_GTK_INFO_BAR
static GtkWidget *perf_gtk__setup_info_bar(void)
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

static GtkWidget *perf_gtk__setup_statusbar(void)
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

int perf_evlist__gtk_browse_hists(struct perf_evlist *evlist,
				  const char *help,
				  void (*timer) (void *arg)__maybe_unused,
				  void *arg __maybe_unused,
				  int delay_secs __maybe_unused)
{
	struct perf_evsel *pos;
	GtkWidget *vbox;
	GtkWidget *notebook;
	GtkWidget *info_bar;
	GtkWidget *statbar;
	GtkWidget *window;

	signal(SIGSEGV, perf_gtk__signal);
	signal(SIGFPE,  perf_gtk__signal);
	signal(SIGINT,  perf_gtk__signal);
	signal(SIGQUIT, perf_gtk__signal);
	signal(SIGTERM, perf_gtk__signal);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gtk_window_set_title(GTK_WINDOW(window), "perf report");

	g_signal_connect(window, "delete_event", gtk_main_quit, NULL);

	pgctx = perf_gtk__activate_context(window);
	if (!pgctx)
		return -1;

	vbox = gtk_vbox_new(FALSE, 0);

	notebook = gtk_notebook_new();

	list_for_each_entry(pos, &evlist->entries, node) {
		struct hists *hists = &pos->hists;
		const char *evname = perf_evsel__name(pos);
		GtkWidget *scrolled_window;
		GtkWidget *tab_label;

		scrolled_window = gtk_scrolled_window_new(NULL, NULL);

		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
							GTK_POLICY_AUTOMATIC,
							GTK_POLICY_AUTOMATIC);

		perf_gtk__show_hists(scrolled_window, hists);

		tab_label = gtk_label_new(evname);

		gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled_window, tab_label);
	}

	gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

	info_bar = perf_gtk__setup_info_bar();
	if (info_bar)
		gtk_box_pack_start(GTK_BOX(vbox), info_bar, FALSE, FALSE, 0);

	statbar = perf_gtk__setup_statusbar();
	gtk_box_pack_start(GTK_BOX(vbox), statbar, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(window), vbox);

	gtk_widget_show_all(window);

	perf_gtk__resize_window(window);

	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

	ui_helpline__push(help);

	gtk_main();

	perf_gtk__deactivate_context(&pgctx);

	return 0;
}
