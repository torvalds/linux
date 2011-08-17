/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-{top,stat,record}.c, see those files for further
 * copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */
#include "../browser.h"
#include "../../annotate.h"
#include "../helpline.h"
#include "../libslang.h"
#include "../util.h"
#include "../../evlist.h"
#include "../../hist.h"
#include "../../sort.h"
#include "../../symbol.h"
#include "../../top.h"

struct perf_top_browser {
	struct ui_browser b;
	struct rb_root	  root;
	struct sym_entry  *selection;
	float		  sum_ksamples;
	int		  dso_width;
	int		  dso_short_width;
	int		  sym_width;
};

static void perf_top_browser__write(struct ui_browser *browser, void *entry, int row)
{
	struct perf_top_browser *top_browser = container_of(browser, struct perf_top_browser, b);
	struct sym_entry *syme = rb_entry(entry, struct sym_entry, rb_node);
	bool current_entry = ui_browser__is_current_entry(browser, row);
	struct symbol *symbol = sym_entry__symbol(syme);
	struct perf_top *top = browser->priv;
	int width = browser->width;
	double pcnt;

	pcnt = 100.0 - (100.0 * ((top_browser->sum_ksamples - syme->snap_count) /
				 top_browser->sum_ksamples));
	ui_browser__set_percent_color(browser, pcnt, current_entry);

	if (top->evlist->nr_entries == 1 || !top->display_weighted) {
		slsmg_printf("%20.2f ", syme->weight);
		width -= 24;
	} else {
		slsmg_printf("%9.1f %10ld ", syme->weight, syme->snap_count);
		width -= 23;
	}

	slsmg_printf("%4.1f%%", pcnt);
	width -= 7;

	if (verbose) {
		slsmg_printf(" %016" PRIx64, symbol->start);
		width -= 17;
	}

	slsmg_printf(" %-*.*s ", top_browser->sym_width, top_browser->sym_width,
		     symbol->name);
	width -= top_browser->sym_width;
	slsmg_write_nstring(width >= syme->map->dso->long_name_len ?
				syme->map->dso->long_name :
				syme->map->dso->short_name, width);

	if (current_entry)
		top_browser->selection = syme;
}

static void perf_top_browser__update_rb_tree(struct perf_top_browser *browser)
{
	struct perf_top *top = browser->b.priv;
	u64 top_idx = browser->b.top_idx;

	browser->root = RB_ROOT;
	browser->b.top = NULL;
	browser->sum_ksamples = perf_top__decay_samples(top, &browser->root);
	/*
 	 * No active symbols
 	 */
	if (top->rb_entries == 0)
		return;

	perf_top__find_widths(top, &browser->root, &browser->dso_width,
			      &browser->dso_short_width,
                              &browser->sym_width);
	if (browser->sym_width + browser->dso_width > browser->b.width - 29) {
		browser->dso_width = browser->dso_short_width;
		if (browser->sym_width + browser->dso_width > browser->b.width - 29)
			browser->sym_width = browser->b.width - browser->dso_width - 29;
	}

	/*
	 * Adjust the ui_browser indexes since the entries in the browser->root
	 * rb_tree may have changed, then seek it from start, so that we get a
	 * possible new top of the screen.
 	 */
	browser->b.nr_entries = top->rb_entries;

	if (top_idx >= browser->b.nr_entries) {
		if (browser->b.height >= browser->b.nr_entries)
			top_idx = browser->b.nr_entries - browser->b.height;
		else
			top_idx = 0;
	}

	if (browser->b.index >= top_idx + browser->b.height)
		browser->b.index = top_idx + browser->b.index - browser->b.top_idx;

	if (browser->b.index >= browser->b.nr_entries)
		browser->b.index = browser->b.nr_entries - 1;

	browser->b.top_idx = top_idx;
	browser->b.seek(&browser->b, top_idx, SEEK_SET);
}

static void perf_top_browser__annotate(struct perf_top_browser *browser)
{
	struct sym_entry *syme = browser->selection;
	struct symbol *sym = sym_entry__symbol(syme);
	struct annotation *notes = symbol__annotation(sym);
	struct perf_top *top = browser->b.priv;

	if (notes->src != NULL)
		goto do_annotation;

	pthread_mutex_lock(&notes->lock);

	top->sym_filter_entry = NULL;

	if (symbol__alloc_hist(sym, top->evlist->nr_entries) < 0) {
		pr_err("Not enough memory for annotating '%s' symbol!\n",
		       sym->name);
		pthread_mutex_unlock(&notes->lock);
		return;
	}

	top->sym_filter_entry = syme;

	pthread_mutex_unlock(&notes->lock);
do_annotation:
	symbol__tui_annotate(sym, syme->map, 0, top->delay_secs * 1000);
}

static int perf_top_browser__run(struct perf_top_browser *browser)
{
	int key;
	char title[160];
	struct perf_top *top = browser->b.priv;
	int delay_msecs = top->delay_secs * 1000;
	int exit_keys[] = { 'a', NEWT_KEY_ENTER, NEWT_KEY_RIGHT, 0, };

	perf_top_browser__update_rb_tree(browser);
        perf_top__header_snprintf(top, title, sizeof(title));
        perf_top__reset_sample_counters(top);

	if (ui_browser__show(&browser->b, title,
			     "ESC: exit, ENTER|->|a: Live Annotate") < 0)
		return -1;

	newtFormSetTimer(browser->b.form, delay_msecs);
	ui_browser__add_exit_keys(&browser->b, exit_keys);

	while (1) {
		key = ui_browser__run(&browser->b);

		switch (key) {
		case -1:
			/* FIXME we need to check if it was es.reason == NEWT_EXIT_TIMER */
			perf_top_browser__update_rb_tree(browser);
			perf_top__header_snprintf(top, title, sizeof(title));
			perf_top__reset_sample_counters(top);
			ui_browser__set_color(&browser->b, NEWT_COLORSET_ROOT);
			SLsmg_gotorc(0, 0);
			slsmg_write_nstring(title, browser->b.width);
			break;
		case 'a':
		case NEWT_KEY_RIGHT:
		case NEWT_KEY_ENTER:
			if (browser->selection)
				perf_top_browser__annotate(browser);
			break;
		case NEWT_KEY_LEFT:
			continue;
		case NEWT_KEY_ESCAPE:
			if (!ui__dialog_yesno("Do you really want to exit?"))
				continue;
			/* Fall thru */
		default:
			goto out;
		}
	}
out:
	ui_browser__hide(&browser->b);
	return key;
}

int perf_top__tui_browser(struct perf_top *top)
{
	struct perf_top_browser browser = {
		.b = {
			.entries = &browser.root,
			.refresh = ui_browser__rb_tree_refresh,
			.seek	 = ui_browser__rb_tree_seek,
			.write	 = perf_top_browser__write,
			.priv	 = top,
		},
	};

	ui_helpline__push("Press <- or ESC to exit");
	return perf_top_browser__run(&browser);
}
