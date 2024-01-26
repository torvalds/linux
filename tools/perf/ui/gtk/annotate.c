// SPDX-License-Identifier: GPL-2.0
#include "gtk.h"
#include "util/sort.h"
#include "util/debug.h"
#include "util/annotate.h"
#include "util/evsel.h"
#include "util/map.h"
#include "util/dso.h"
#include "util/symbol.h"
#include "ui/helpline.h"
#include <inttypes.h>
#include <signal.h>

enum {
	ANN_COL__PERCENT,
	ANN_COL__OFFSET,
	ANN_COL__LINE,

	MAX_ANN_COLS
};

static const char *const col_names[] = {
	"Overhead",
	"Offset",
	"Line"
};

static int perf_gtk__get_percent(char *buf, size_t size, struct symbol *sym,
				 struct disasm_line *dl, int evidx)
{
	struct sym_hist *symhist;
	double percent = 0.0;
	const char *markup;
	int ret = 0;

	strcpy(buf, "");

	if (dl->al.offset == (s64) -1)
		return 0;

	symhist = annotation__histogram(symbol__annotation(sym), evidx);
	if (!symbol_conf.event_group && !symhist->addr[dl->al.offset].nr_samples)
		return 0;

	percent = 100.0 * symhist->addr[dl->al.offset].nr_samples / symhist->nr_samples;

	markup = perf_gtk__get_percent_color(percent);
	if (markup)
		ret += scnprintf(buf, size, "%s", markup);
	ret += scnprintf(buf + ret, size - ret, "%6.2f%%", percent);
	if (markup)
		ret += scnprintf(buf + ret, size - ret, "</span>");

	return ret;
}

static int perf_gtk__get_offset(char *buf, size_t size, struct map_symbol *ms,
				struct disasm_line *dl)
{
	u64 start = map__rip_2objdump(ms->map, ms->sym->start);

	strcpy(buf, "");

	if (dl->al.offset == (s64) -1)
		return 0;

	return scnprintf(buf, size, "%"PRIx64, start + dl->al.offset);
}

static int perf_gtk__get_line(char *buf, size_t size, struct disasm_line *dl)
{
	int ret = 0;
	char *line = g_markup_escape_text(dl->al.line, -1);
	const char *markup = "<span fgcolor='gray'>";

	strcpy(buf, "");

	if (!line)
		return 0;

	if (dl->al.offset != (s64) -1)
		markup = NULL;

	if (markup)
		ret += scnprintf(buf, size, "%s", markup);
	ret += scnprintf(buf + ret, size - ret, "%s", line);
	if (markup)
		ret += scnprintf(buf + ret, size - ret, "</span>");

	g_free(line);
	return ret;
}

static int perf_gtk__annotate_symbol(GtkWidget *window, struct map_symbol *ms,
				struct evsel *evsel,
				struct hist_browser_timer *hbt __maybe_unused)
{
	struct symbol *sym = ms->sym;
	struct disasm_line *pos, *n;
	struct annotation *notes;
	GType col_types[MAX_ANN_COLS];
	GtkCellRenderer *renderer;
	GtkListStore *store;
	GtkWidget *view;
	int i;
	char s[512];

	notes = symbol__annotation(sym);

	for (i = 0; i < MAX_ANN_COLS; i++) {
		col_types[i] = G_TYPE_STRING;
	}
	store = gtk_list_store_newv(MAX_ANN_COLS, col_types);

	view = gtk_tree_view_new();
	renderer = gtk_cell_renderer_text_new();

	for (i = 0; i < MAX_ANN_COLS; i++) {
		gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
					-1, col_names[i], renderer, "markup",
					i, NULL);
	}

	gtk_tree_view_set_model(GTK_TREE_VIEW(view), GTK_TREE_MODEL(store));
	g_object_unref(GTK_TREE_MODEL(store));

	list_for_each_entry(pos, &notes->src->source, al.node) {
		GtkTreeIter iter;
		int ret = 0;

		gtk_list_store_append(store, &iter);

		if (evsel__is_group_event(evsel)) {
			for (i = 0; i < evsel->core.nr_members; i++) {
				ret += perf_gtk__get_percent(s + ret,
							     sizeof(s) - ret,
							     sym, pos,
							     evsel->core.idx + i);
				ret += scnprintf(s + ret, sizeof(s) - ret, " ");
			}
		} else {
			ret = perf_gtk__get_percent(s, sizeof(s), sym, pos,
						    evsel->core.idx);
		}

		if (ret)
			gtk_list_store_set(store, &iter, ANN_COL__PERCENT, s, -1);
		if (perf_gtk__get_offset(s, sizeof(s), ms, pos))
			gtk_list_store_set(store, &iter, ANN_COL__OFFSET, s, -1);
		if (perf_gtk__get_line(s, sizeof(s), pos))
			gtk_list_store_set(store, &iter, ANN_COL__LINE, s, -1);
	}

	gtk_container_add(GTK_CONTAINER(window), view);

	list_for_each_entry_safe(pos, n, &notes->src->source, al.node) {
		list_del_init(&pos->al.node);
		disasm_line__free(pos);
	}

	return 0;
}

static int symbol__gtk_annotate(struct map_symbol *ms, struct evsel *evsel,
				struct hist_browser_timer *hbt)
{
	struct dso *dso = map__dso(ms->map);
	struct symbol *sym = ms->sym;
	GtkWidget *window;
	GtkWidget *notebook;
	GtkWidget *scrolled_window;
	GtkWidget *tab_label;
	int err;

	if (dso->annotate_warned)
		return -1;

	err = symbol__annotate(ms, evsel, NULL);
	if (err) {
		char msg[BUFSIZ];
		dso->annotate_warned = true;
		symbol__strerror_disassemble(ms, err, msg, sizeof(msg));
		ui__error("Couldn't annotate %s: %s\n", sym->name, msg);
		return -1;
	}

	symbol__calc_percent(sym, evsel);

	if (perf_gtk__is_active_context(pgctx)) {
		window = pgctx->main_window;
		notebook = pgctx->notebook;
	} else {
		GtkWidget *vbox;
		GtkWidget *infobar;
		GtkWidget *statbar;

		signal(SIGSEGV, perf_gtk__signal);
		signal(SIGFPE,  perf_gtk__signal);
		signal(SIGINT,  perf_gtk__signal);
		signal(SIGQUIT, perf_gtk__signal);
		signal(SIGTERM, perf_gtk__signal);

		window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_title(GTK_WINDOW(window), "perf annotate");

		g_signal_connect(window, "delete_event", gtk_main_quit, NULL);

		pgctx = perf_gtk__activate_context(window);
		if (!pgctx)
			return -1;

		vbox = gtk_vbox_new(FALSE, 0);
		notebook = gtk_notebook_new();
		pgctx->notebook = notebook;

		gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

		infobar = perf_gtk__setup_info_bar();
		if (infobar) {
			gtk_box_pack_start(GTK_BOX(vbox), infobar,
					   FALSE, FALSE, 0);
		}

		statbar = perf_gtk__setup_statusbar();
		gtk_box_pack_start(GTK_BOX(vbox), statbar, FALSE, FALSE, 0);

		gtk_container_add(GTK_CONTAINER(window), vbox);
	}

	scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	tab_label = gtk_label_new(sym->name);

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled_window,
				 tab_label);

	perf_gtk__annotate_symbol(scrolled_window, ms, evsel, hbt);
	return 0;
}

int hist_entry__gtk_annotate(struct hist_entry *he,
			     struct evsel *evsel,
			     struct hist_browser_timer *hbt)
{
	return symbol__gtk_annotate(&he->ms, evsel, hbt);
}

void perf_gtk__show_annotations(void)
{
	GtkWidget *window;

	if (!perf_gtk__is_active_context(pgctx))
		return;

	window = pgctx->main_window;
	gtk_widget_show_all(window);

	perf_gtk__resize_window(window);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

	gtk_main();

	perf_gtk__deactivate_context(&pgctx);
}
