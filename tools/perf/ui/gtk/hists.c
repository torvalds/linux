#include "../evlist.h"
#include "../cache.h"
#include "../evsel.h"
#include "../sort.h"
#include "../hist.h"
#include "../helpline.h"
#include "gtk.h"

#define MAX_COLUMNS			32

static int __percent_color_snprintf(char *buf, size_t size, double percent)
{
	int ret = 0;
	const char *markup;

	markup = perf_gtk__get_percent_color(percent);
	if (markup)
		ret += scnprintf(buf, size, markup);

	ret += scnprintf(buf + ret, size - ret, " %6.2f%%", percent);

	if (markup)
		ret += scnprintf(buf + ret, size - ret, "</span>");

	return ret;
}


static int __hpp__color_fmt(struct perf_hpp *hpp, struct hist_entry *he,
			    u64 (*get_field)(struct hist_entry *))
{
	int ret;
	double percent = 0.0;
	struct hists *hists = he->hists;

	if (hists->stats.total_period)
		percent = 100.0 * get_field(he) / hists->stats.total_period;

	ret = __percent_color_snprintf(hpp->buf, hpp->size, percent);

	if (symbol_conf.event_group) {
		int prev_idx, idx_delta;
		struct perf_evsel *evsel = hists_to_evsel(hists);
		struct hist_entry *pair;
		int nr_members = evsel->nr_members;

		if (nr_members <= 1)
			return ret;

		prev_idx = perf_evsel__group_idx(evsel);

		list_for_each_entry(pair, &he->pairs.head, pairs.node) {
			u64 period = get_field(pair);
			u64 total = pair->hists->stats.total_period;

			evsel = hists_to_evsel(pair->hists);
			idx_delta = perf_evsel__group_idx(evsel) - prev_idx - 1;

			while (idx_delta--) {
				/*
				 * zero-fill group members in the middle which
				 * have no sample
				 */
				ret += __percent_color_snprintf(hpp->buf + ret,
								hpp->size - ret,
								0.0);
			}

			percent = 100.0 * period / total;
			ret += __percent_color_snprintf(hpp->buf + ret,
							hpp->size - ret,
							percent);

			prev_idx = perf_evsel__group_idx(evsel);
		}

		idx_delta = nr_members - prev_idx - 1;

		while (idx_delta--) {
			/*
			 * zero-fill group members at last which have no sample
			 */
			ret += __percent_color_snprintf(hpp->buf + ret,
							hpp->size - ret,
							0.0);
		}
	}
	return ret;
}

#define __HPP_COLOR_PERCENT_FN(_type, _field)					\
static u64 he_get_##_field(struct hist_entry *he)				\
{										\
	return he->stat._field;							\
}										\
										\
static int perf_gtk__hpp_color_##_type(struct perf_hpp *hpp,			\
				       struct hist_entry *he)			\
{										\
	return __hpp__color_fmt(hpp, he, he_get_##_field);			\
}

__HPP_COLOR_PERCENT_FN(overhead, period)
__HPP_COLOR_PERCENT_FN(overhead_sys, period_sys)
__HPP_COLOR_PERCENT_FN(overhead_us, period_us)
__HPP_COLOR_PERCENT_FN(overhead_guest_sys, period_guest_sys)
__HPP_COLOR_PERCENT_FN(overhead_guest_us, period_guest_us)

#undef __HPP_COLOR_PERCENT_FN


void perf_gtk__init_hpp(void)
{
	perf_hpp__column_enable(PERF_HPP__OVERHEAD);

	perf_hpp__init();

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
	struct perf_hpp_fmt *fmt;
	GType col_types[MAX_COLUMNS];
	GtkCellRenderer *renderer;
	struct sort_entry *se;
	GtkListStore *store;
	struct rb_node *nd;
	GtkWidget *view;
	int col_idx;
	int nr_cols;
	char s[512];

	struct perf_hpp hpp = {
		.buf		= s,
		.size		= sizeof(s),
		.ptr		= hists_to_evsel(hists),
	};

	nr_cols = 0;

	perf_hpp__for_each_format(fmt)
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

	perf_hpp__for_each_format(fmt) {
		fmt->header(&hpp);

		gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
							    -1, ltrim(s),
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

		perf_hpp__for_each_format(fmt) {
			if (fmt->color)
				fmt->color(&hpp, h);
			else
				fmt->entry(&hpp, h);

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

int perf_evlist__gtk_browse_hists(struct perf_evlist *evlist,
				  const char *help,
				  struct hist_browser_timer *hbt __maybe_unused)
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

	gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

	info_bar = perf_gtk__setup_info_bar();
	if (info_bar)
		gtk_box_pack_start(GTK_BOX(vbox), info_bar, FALSE, FALSE, 0);

	statbar = perf_gtk__setup_statusbar();
	gtk_box_pack_start(GTK_BOX(vbox), statbar, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(window), vbox);

	list_for_each_entry(pos, &evlist->entries, node) {
		struct hists *hists = &pos->hists;
		const char *evname = perf_evsel__name(pos);
		GtkWidget *scrolled_window;
		GtkWidget *tab_label;
		char buf[512];
		size_t size = sizeof(buf);

		if (symbol_conf.event_group) {
			if (!perf_evsel__is_group_leader(pos))
				continue;

			if (pos->nr_members > 1) {
				perf_evsel__group_desc(pos, buf, size);
				evname = buf;
			}
		}

		scrolled_window = gtk_scrolled_window_new(NULL, NULL);

		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
							GTK_POLICY_AUTOMATIC,
							GTK_POLICY_AUTOMATIC);

		perf_gtk__show_hists(scrolled_window, hists);

		tab_label = gtk_label_new(evname);

		gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled_window, tab_label);
	}

	gtk_widget_show_all(window);

	perf_gtk__resize_window(window);

	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

	ui_helpline__push(help);

	gtk_main();

	perf_gtk__deactivate_context(&pgctx);

	return 0;
}
