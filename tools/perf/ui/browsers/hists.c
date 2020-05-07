// SPDX-License-Identifier: GPL-2.0
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/rbtree.h>
#include <linux/string.h>
#include <sys/ttydefaults.h>
#include <linux/time64.h>
#include <linux/zalloc.h>

#include "../../util/debug.h"
#include "../../util/dso.h"
#include "../../util/callchain.h"
#include "../../util/evsel.h"
#include "../../util/evlist.h"
#include "../../util/header.h"
#include "../../util/hist.h"
#include "../../util/machine.h"
#include "../../util/map.h"
#include "../../util/maps.h"
#include "../../util/symbol.h"
#include "../../util/map_symbol.h"
#include "../../util/branch.h"
#include "../../util/pstack.h"
#include "../../util/sort.h"
#include "../../util/top.h"
#include "../../util/thread.h"
#include "../../util/block-info.h"
#include "../../arch/common.h"
#include "../../perf.h"

#include "../browsers/hists.h"
#include "../helpline.h"
#include "../util.h"
#include "../ui.h"
#include "map.h"
#include "annotate.h"
#include "srcline.h"
#include "string2.h"
#include "units.h"
#include "time-utils.h"

#include <linux/ctype.h>

extern void hist_browser__init_hpp(void);

static int hists_browser__scnprintf_title(struct hist_browser *browser, char *bf, size_t size);
static void hist_browser__update_nr_entries(struct hist_browser *hb);

static struct rb_node *hists__filter_entries(struct rb_node *nd,
					     float min_pcnt);

static bool hist_browser__has_filter(struct hist_browser *hb)
{
	return hists__has_filter(hb->hists) || hb->min_pcnt || symbol_conf.has_filter || hb->c2c_filter;
}

static int hist_browser__get_folding(struct hist_browser *browser)
{
	struct rb_node *nd;
	struct hists *hists = browser->hists;
	int unfolded_rows = 0;

	for (nd = rb_first_cached(&hists->entries);
	     (nd = hists__filter_entries(nd, browser->min_pcnt)) != NULL;
	     nd = rb_hierarchy_next(nd)) {
		struct hist_entry *he =
			rb_entry(nd, struct hist_entry, rb_node);

		if (he->leaf && he->unfolded)
			unfolded_rows += he->nr_rows;
	}
	return unfolded_rows;
}

static void hist_browser__set_title_space(struct hist_browser *hb)
{
	struct ui_browser *browser = &hb->b;
	struct hists *hists = hb->hists;
	struct perf_hpp_list *hpp_list = hists->hpp_list;

	browser->extra_title_lines = hb->show_headers ? hpp_list->nr_header_lines : 0;
}

static u32 hist_browser__nr_entries(struct hist_browser *hb)
{
	u32 nr_entries;

	if (symbol_conf.report_hierarchy)
		nr_entries = hb->nr_hierarchy_entries;
	else if (hist_browser__has_filter(hb))
		nr_entries = hb->nr_non_filtered_entries;
	else
		nr_entries = hb->hists->nr_entries;

	hb->nr_callchain_rows = hist_browser__get_folding(hb);
	return nr_entries + hb->nr_callchain_rows;
}

static void hist_browser__update_rows(struct hist_browser *hb)
{
	struct ui_browser *browser = &hb->b;
	struct hists *hists = hb->hists;
	struct perf_hpp_list *hpp_list = hists->hpp_list;
	u16 index_row;

	if (!hb->show_headers) {
		browser->rows += browser->extra_title_lines;
		browser->extra_title_lines = 0;
		return;
	}

	browser->extra_title_lines = hpp_list->nr_header_lines;
	browser->rows -= browser->extra_title_lines;
	/*
	 * Verify if we were at the last line and that line isn't
	 * visibe because we now show the header line(s).
	 */
	index_row = browser->index - browser->top_idx;
	if (index_row >= browser->rows)
		browser->index -= index_row - browser->rows + 1;
}

static void hist_browser__refresh_dimensions(struct ui_browser *browser)
{
	struct hist_browser *hb = container_of(browser, struct hist_browser, b);

	/* 3 == +/- toggle symbol before actual hist_entry rendering */
	browser->width = 3 + (hists__sort_list_width(hb->hists) + sizeof("[k]"));
	/*
 	 * FIXME: Just keeping existing behaviour, but this really should be
 	 *	  before updating browser->width, as it will invalidate the
 	 *	  calculation above. Fix this and the fallout in another
 	 *	  changeset.
 	 */
	ui_browser__refresh_dimensions(browser);
}

static void hist_browser__reset(struct hist_browser *browser)
{
	/*
	 * The hists__remove_entry_filter() already folds non-filtered
	 * entries so we can assume it has 0 callchain rows.
	 */
	browser->nr_callchain_rows = 0;

	hist_browser__update_nr_entries(browser);
	browser->b.nr_entries = hist_browser__nr_entries(browser);
	hist_browser__refresh_dimensions(&browser->b);
	ui_browser__reset_index(&browser->b);
}

static char tree__folded_sign(bool unfolded)
{
	return unfolded ? '-' : '+';
}

static char hist_entry__folded(const struct hist_entry *he)
{
	return he->has_children ? tree__folded_sign(he->unfolded) : ' ';
}

static char callchain_list__folded(const struct callchain_list *cl)
{
	return cl->has_children ? tree__folded_sign(cl->unfolded) : ' ';
}

static void callchain_list__set_folding(struct callchain_list *cl, bool unfold)
{
	cl->unfolded = unfold ? cl->has_children : false;
}

static int callchain_node__count_rows_rb_tree(struct callchain_node *node)
{
	int n = 0;
	struct rb_node *nd;

	for (nd = rb_first(&node->rb_root); nd; nd = rb_next(nd)) {
		struct callchain_node *child = rb_entry(nd, struct callchain_node, rb_node);
		struct callchain_list *chain;
		char folded_sign = ' '; /* No children */

		list_for_each_entry(chain, &child->val, list) {
			++n;

			/* We need this because we may not have children */
			folded_sign = callchain_list__folded(chain);
			if (folded_sign == '+')
				break;
		}

		if (folded_sign == '-') /* Have children and they're unfolded */
			n += callchain_node__count_rows_rb_tree(child);
	}

	return n;
}

static int callchain_node__count_flat_rows(struct callchain_node *node)
{
	struct callchain_list *chain;
	char folded_sign = 0;
	int n = 0;

	list_for_each_entry(chain, &node->parent_val, list) {
		if (!folded_sign) {
			/* only check first chain list entry */
			folded_sign = callchain_list__folded(chain);
			if (folded_sign == '+')
				return 1;
		}
		n++;
	}

	list_for_each_entry(chain, &node->val, list) {
		if (!folded_sign) {
			/* node->parent_val list might be empty */
			folded_sign = callchain_list__folded(chain);
			if (folded_sign == '+')
				return 1;
		}
		n++;
	}

	return n;
}

static int callchain_node__count_folded_rows(struct callchain_node *node __maybe_unused)
{
	return 1;
}

static int callchain_node__count_rows(struct callchain_node *node)
{
	struct callchain_list *chain;
	bool unfolded = false;
	int n = 0;

	if (callchain_param.mode == CHAIN_FLAT)
		return callchain_node__count_flat_rows(node);
	else if (callchain_param.mode == CHAIN_FOLDED)
		return callchain_node__count_folded_rows(node);

	list_for_each_entry(chain, &node->val, list) {
		++n;

		unfolded = chain->unfolded;
	}

	if (unfolded)
		n += callchain_node__count_rows_rb_tree(node);

	return n;
}

static int callchain__count_rows(struct rb_root *chain)
{
	struct rb_node *nd;
	int n = 0;

	for (nd = rb_first(chain); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);
		n += callchain_node__count_rows(node);
	}

	return n;
}

static int hierarchy_count_rows(struct hist_browser *hb, struct hist_entry *he,
				bool include_children)
{
	int count = 0;
	struct rb_node *node;
	struct hist_entry *child;

	if (he->leaf)
		return callchain__count_rows(&he->sorted_chain);

	if (he->has_no_entry)
		return 1;

	node = rb_first_cached(&he->hroot_out);
	while (node) {
		float percent;

		child = rb_entry(node, struct hist_entry, rb_node);
		percent = hist_entry__get_percent_limit(child);

		if (!child->filtered && percent >= hb->min_pcnt) {
			count++;

			if (include_children && child->unfolded)
				count += hierarchy_count_rows(hb, child, true);
		}

		node = rb_next(node);
	}
	return count;
}

static bool hist_entry__toggle_fold(struct hist_entry *he)
{
	if (!he)
		return false;

	if (!he->has_children)
		return false;

	he->unfolded = !he->unfolded;
	return true;
}

static bool callchain_list__toggle_fold(struct callchain_list *cl)
{
	if (!cl)
		return false;

	if (!cl->has_children)
		return false;

	cl->unfolded = !cl->unfolded;
	return true;
}

static void callchain_node__init_have_children_rb_tree(struct callchain_node *node)
{
	struct rb_node *nd = rb_first(&node->rb_root);

	for (nd = rb_first(&node->rb_root); nd; nd = rb_next(nd)) {
		struct callchain_node *child = rb_entry(nd, struct callchain_node, rb_node);
		struct callchain_list *chain;
		bool first = true;

		list_for_each_entry(chain, &child->val, list) {
			if (first) {
				first = false;
				chain->has_children = chain->list.next != &child->val ||
							 !RB_EMPTY_ROOT(&child->rb_root);
			} else
				chain->has_children = chain->list.next == &child->val &&
							 !RB_EMPTY_ROOT(&child->rb_root);
		}

		callchain_node__init_have_children_rb_tree(child);
	}
}

static void callchain_node__init_have_children(struct callchain_node *node,
					       bool has_sibling)
{
	struct callchain_list *chain;

	chain = list_entry(node->val.next, struct callchain_list, list);
	chain->has_children = has_sibling;

	if (!list_empty(&node->val)) {
		chain = list_entry(node->val.prev, struct callchain_list, list);
		chain->has_children = !RB_EMPTY_ROOT(&node->rb_root);
	}

	callchain_node__init_have_children_rb_tree(node);
}

static void callchain__init_have_children(struct rb_root *root)
{
	struct rb_node *nd = rb_first(root);
	bool has_sibling = nd && rb_next(nd);

	for (nd = rb_first(root); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);
		callchain_node__init_have_children(node, has_sibling);
		if (callchain_param.mode == CHAIN_FLAT ||
		    callchain_param.mode == CHAIN_FOLDED)
			callchain_node__make_parent_list(node);
	}
}

static void hist_entry__init_have_children(struct hist_entry *he)
{
	if (he->init_have_children)
		return;

	if (he->leaf) {
		he->has_children = !RB_EMPTY_ROOT(&he->sorted_chain);
		callchain__init_have_children(&he->sorted_chain);
	} else {
		he->has_children = !RB_EMPTY_ROOT(&he->hroot_out.rb_root);
	}

	he->init_have_children = true;
}

static bool hist_browser__selection_has_children(struct hist_browser *browser)
{
	struct hist_entry *he = browser->he_selection;
	struct map_symbol *ms = browser->selection;

	if (!he || !ms)
		return false;

	if (ms == &he->ms)
	       return he->has_children;

	return container_of(ms, struct callchain_list, ms)->has_children;
}

static bool hist_browser__he_selection_unfolded(struct hist_browser *browser)
{
	return browser->he_selection ? browser->he_selection->unfolded : false;
}

static bool hist_browser__selection_unfolded(struct hist_browser *browser)
{
	struct hist_entry *he = browser->he_selection;
	struct map_symbol *ms = browser->selection;

	if (!he || !ms)
		return false;

	if (ms == &he->ms)
	       return he->unfolded;

	return container_of(ms, struct callchain_list, ms)->unfolded;
}

static char *hist_browser__selection_sym_name(struct hist_browser *browser, char *bf, size_t size)
{
	struct hist_entry *he = browser->he_selection;
	struct map_symbol *ms = browser->selection;
	struct callchain_list *callchain_entry;

	if (!he || !ms)
		return NULL;

	if (ms == &he->ms) {
	       hist_entry__sym_snprintf(he, bf, size, 0);
	       return bf + 4; // skip the level, e.g. '[k] '
	}

	callchain_entry = container_of(ms, struct callchain_list, ms);
	return callchain_list__sym_name(callchain_entry, bf, size, browser->show_dso);
}

static bool hist_browser__toggle_fold(struct hist_browser *browser)
{
	struct hist_entry *he = browser->he_selection;
	struct map_symbol *ms = browser->selection;
	struct callchain_list *cl = container_of(ms, struct callchain_list, ms);
	bool has_children;

	if (!he || !ms)
		return false;

	if (ms == &he->ms)
		has_children = hist_entry__toggle_fold(he);
	else
		has_children = callchain_list__toggle_fold(cl);

	if (has_children) {
		int child_rows = 0;

		hist_entry__init_have_children(he);
		browser->b.nr_entries -= he->nr_rows;

		if (he->leaf)
			browser->nr_callchain_rows -= he->nr_rows;
		else
			browser->nr_hierarchy_entries -= he->nr_rows;

		if (symbol_conf.report_hierarchy)
			child_rows = hierarchy_count_rows(browser, he, true);

		if (he->unfolded) {
			if (he->leaf)
				he->nr_rows = callchain__count_rows(
						&he->sorted_chain);
			else
				he->nr_rows = hierarchy_count_rows(browser, he, false);

			/* account grand children */
			if (symbol_conf.report_hierarchy)
				browser->b.nr_entries += child_rows - he->nr_rows;

			if (!he->leaf && he->nr_rows == 0) {
				he->has_no_entry = true;
				he->nr_rows = 1;
			}
		} else {
			if (symbol_conf.report_hierarchy)
				browser->b.nr_entries -= child_rows - he->nr_rows;

			if (he->has_no_entry)
				he->has_no_entry = false;

			he->nr_rows = 0;
		}

		browser->b.nr_entries += he->nr_rows;

		if (he->leaf)
			browser->nr_callchain_rows += he->nr_rows;
		else
			browser->nr_hierarchy_entries += he->nr_rows;

		return true;
	}

	/* If it doesn't have children, no toggling performed */
	return false;
}

static int callchain_node__set_folding_rb_tree(struct callchain_node *node, bool unfold)
{
	int n = 0;
	struct rb_node *nd;

	for (nd = rb_first(&node->rb_root); nd; nd = rb_next(nd)) {
		struct callchain_node *child = rb_entry(nd, struct callchain_node, rb_node);
		struct callchain_list *chain;
		bool has_children = false;

		list_for_each_entry(chain, &child->val, list) {
			++n;
			callchain_list__set_folding(chain, unfold);
			has_children = chain->has_children;
		}

		if (has_children)
			n += callchain_node__set_folding_rb_tree(child, unfold);
	}

	return n;
}

static int callchain_node__set_folding(struct callchain_node *node, bool unfold)
{
	struct callchain_list *chain;
	bool has_children = false;
	int n = 0;

	list_for_each_entry(chain, &node->val, list) {
		++n;
		callchain_list__set_folding(chain, unfold);
		has_children = chain->has_children;
	}

	if (has_children)
		n += callchain_node__set_folding_rb_tree(node, unfold);

	return n;
}

static int callchain__set_folding(struct rb_root *chain, bool unfold)
{
	struct rb_node *nd;
	int n = 0;

	for (nd = rb_first(chain); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);
		n += callchain_node__set_folding(node, unfold);
	}

	return n;
}

static int hierarchy_set_folding(struct hist_browser *hb, struct hist_entry *he,
				 bool unfold __maybe_unused)
{
	float percent;
	struct rb_node *nd;
	struct hist_entry *child;
	int n = 0;

	for (nd = rb_first_cached(&he->hroot_out); nd; nd = rb_next(nd)) {
		child = rb_entry(nd, struct hist_entry, rb_node);
		percent = hist_entry__get_percent_limit(child);
		if (!child->filtered && percent >= hb->min_pcnt)
			n++;
	}

	return n;
}

static void __hist_entry__set_folding(struct hist_entry *he,
				      struct hist_browser *hb, bool unfold)
{
	hist_entry__init_have_children(he);
	he->unfolded = unfold ? he->has_children : false;

	if (he->has_children) {
		int n;

		if (he->leaf)
			n = callchain__set_folding(&he->sorted_chain, unfold);
		else
			n = hierarchy_set_folding(hb, he, unfold);

		he->nr_rows = unfold ? n : 0;
	} else
		he->nr_rows = 0;
}

static void hist_entry__set_folding(struct hist_entry *he,
				    struct hist_browser *browser, bool unfold)
{
	double percent;

	percent = hist_entry__get_percent_limit(he);
	if (he->filtered || percent < browser->min_pcnt)
		return;

	__hist_entry__set_folding(he, browser, unfold);

	if (!he->depth || unfold)
		browser->nr_hierarchy_entries++;
	if (he->leaf)
		browser->nr_callchain_rows += he->nr_rows;
	else if (unfold && !hist_entry__has_hierarchy_children(he, browser->min_pcnt)) {
		browser->nr_hierarchy_entries++;
		he->has_no_entry = true;
		he->nr_rows = 1;
	} else
		he->has_no_entry = false;
}

static void
__hist_browser__set_folding(struct hist_browser *browser, bool unfold)
{
	struct rb_node *nd;
	struct hist_entry *he;

	nd = rb_first_cached(&browser->hists->entries);
	while (nd) {
		he = rb_entry(nd, struct hist_entry, rb_node);

		/* set folding state even if it's currently folded */
		nd = __rb_hierarchy_next(nd, HMD_FORCE_CHILD);

		hist_entry__set_folding(he, browser, unfold);
	}
}

static void hist_browser__set_folding(struct hist_browser *browser, bool unfold)
{
	browser->nr_hierarchy_entries = 0;
	browser->nr_callchain_rows = 0;
	__hist_browser__set_folding(browser, unfold);

	browser->b.nr_entries = hist_browser__nr_entries(browser);
	/* Go to the start, we may be way after valid entries after a collapse */
	ui_browser__reset_index(&browser->b);
}

static void hist_browser__set_folding_selected(struct hist_browser *browser, bool unfold)
{
	if (!browser->he_selection)
		return;

	hist_entry__set_folding(browser->he_selection, browser, unfold);
	browser->b.nr_entries = hist_browser__nr_entries(browser);
}

static void ui_browser__warn_lost_events(struct ui_browser *browser)
{
	ui_browser__warning(browser, 4,
		"Events are being lost, check IO/CPU overload!\n\n"
		"You may want to run 'perf' using a RT scheduler policy:\n\n"
		" perf top -r 80\n\n"
		"Or reduce the sampling frequency.");
}

static int hist_browser__title(struct hist_browser *browser, char *bf, size_t size)
{
	return browser->title ? browser->title(browser, bf, size) : 0;
}

static int hist_browser__handle_hotkey(struct hist_browser *browser, bool warn_lost_event, char *title, size_t size, int key)
{
	switch (key) {
	case K_TIMER: {
		struct hist_browser_timer *hbt = browser->hbt;
		u64 nr_entries;

		WARN_ON_ONCE(!hbt);

		if (hbt)
			hbt->timer(hbt->arg);

		if (hist_browser__has_filter(browser) || symbol_conf.report_hierarchy)
			hist_browser__update_nr_entries(browser);

		nr_entries = hist_browser__nr_entries(browser);
		ui_browser__update_nr_entries(&browser->b, nr_entries);

		if (warn_lost_event &&
		    (browser->hists->stats.nr_lost_warned !=
		    browser->hists->stats.nr_events[PERF_RECORD_LOST])) {
			browser->hists->stats.nr_lost_warned =
				browser->hists->stats.nr_events[PERF_RECORD_LOST];
			ui_browser__warn_lost_events(&browser->b);
		}

		hist_browser__title(browser, title, size);
		ui_browser__show_title(&browser->b, title);
		break;
	}
	case 'D': { /* Debug */
		struct hist_entry *h = rb_entry(browser->b.top, struct hist_entry, rb_node);
		static int seq;

		ui_helpline__pop();
		ui_helpline__fpush("%d: nr_ent=(%d,%d), etl: %d, rows=%d, idx=%d, fve: idx=%d, row_off=%d, nrows=%d",
				   seq++, browser->b.nr_entries, browser->hists->nr_entries,
				   browser->b.extra_title_lines, browser->b.rows,
				   browser->b.index, browser->b.top_idx, h->row_offset, h->nr_rows);
	}
		break;
	case 'C':
		/* Collapse the whole world. */
		hist_browser__set_folding(browser, false);
		break;
	case 'c':
		/* Collapse the selected entry. */
		hist_browser__set_folding_selected(browser, false);
		break;
	case 'E':
		/* Expand the whole world. */
		hist_browser__set_folding(browser, true);
		break;
	case 'e':
		/* Expand the selected entry. */
		hist_browser__set_folding_selected(browser, !hist_browser__he_selection_unfolded(browser));
		break;
	case 'H':
		browser->show_headers = !browser->show_headers;
		hist_browser__update_rows(browser);
		break;
	case '+':
		if (hist_browser__toggle_fold(browser))
			break;
		/* fall thru */
	default:
		return -1;
	}

	return 0;
}

int hist_browser__run(struct hist_browser *browser, const char *help,
		      bool warn_lost_event, int key)
{
	char title[160];
	struct hist_browser_timer *hbt = browser->hbt;
	int delay_secs = hbt ? hbt->refresh : 0;

	browser->b.entries = &browser->hists->entries;
	browser->b.nr_entries = hist_browser__nr_entries(browser);

	hist_browser__title(browser, title, sizeof(title));

	if (ui_browser__show(&browser->b, title, "%s", help) < 0)
		return -1;

	if (key && hist_browser__handle_hotkey(browser, warn_lost_event, title, sizeof(title), key))
		goto out;

	while (1) {
		key = ui_browser__run(&browser->b, delay_secs);

		if (hist_browser__handle_hotkey(browser, warn_lost_event, title, sizeof(title), key))
			break;
	}
out:
	ui_browser__hide(&browser->b);
	return key;
}

struct callchain_print_arg {
	/* for hists browser */
	off_t	row_offset;
	bool	is_current_entry;

	/* for file dump */
	FILE	*fp;
	int	printed;
};

typedef void (*print_callchain_entry_fn)(struct hist_browser *browser,
					 struct callchain_list *chain,
					 const char *str, int offset,
					 unsigned short row,
					 struct callchain_print_arg *arg);

static void hist_browser__show_callchain_entry(struct hist_browser *browser,
					       struct callchain_list *chain,
					       const char *str, int offset,
					       unsigned short row,
					       struct callchain_print_arg *arg)
{
	int color, width;
	char folded_sign = callchain_list__folded(chain);
	bool show_annotated = browser->show_dso && chain->ms.sym && symbol__annotation(chain->ms.sym)->src;

	color = HE_COLORSET_NORMAL;
	width = browser->b.width - (offset + 2);
	if (ui_browser__is_current_entry(&browser->b, row)) {
		browser->selection = &chain->ms;
		color = HE_COLORSET_SELECTED;
		arg->is_current_entry = true;
	}

	ui_browser__set_color(&browser->b, color);
	ui_browser__gotorc(&browser->b, row, 0);
	ui_browser__write_nstring(&browser->b, " ", offset);
	ui_browser__printf(&browser->b, "%c", folded_sign);
	ui_browser__write_graph(&browser->b, show_annotated ? SLSMG_RARROW_CHAR : ' ');
	ui_browser__write_nstring(&browser->b, str, width);
}

static void hist_browser__fprintf_callchain_entry(struct hist_browser *b __maybe_unused,
						  struct callchain_list *chain,
						  const char *str, int offset,
						  unsigned short row __maybe_unused,
						  struct callchain_print_arg *arg)
{
	char folded_sign = callchain_list__folded(chain);

	arg->printed += fprintf(arg->fp, "%*s%c %s\n", offset, " ",
				folded_sign, str);
}

typedef bool (*check_output_full_fn)(struct hist_browser *browser,
				     unsigned short row);

static bool hist_browser__check_output_full(struct hist_browser *browser,
					    unsigned short row)
{
	return browser->b.rows == row;
}

static bool hist_browser__check_dump_full(struct hist_browser *browser __maybe_unused,
					  unsigned short row __maybe_unused)
{
	return false;
}

#define LEVEL_OFFSET_STEP 3

static int hist_browser__show_callchain_list(struct hist_browser *browser,
					     struct callchain_node *node,
					     struct callchain_list *chain,
					     unsigned short row, u64 total,
					     bool need_percent, int offset,
					     print_callchain_entry_fn print,
					     struct callchain_print_arg *arg)
{
	char bf[1024], *alloc_str;
	char buf[64], *alloc_str2;
	const char *str;
	int ret = 1;

	if (arg->row_offset != 0) {
		arg->row_offset--;
		return 0;
	}

	alloc_str = NULL;
	alloc_str2 = NULL;

	str = callchain_list__sym_name(chain, bf, sizeof(bf),
				       browser->show_dso);

	if (symbol_conf.show_branchflag_count) {
		callchain_list_counts__printf_value(chain, NULL,
						    buf, sizeof(buf));

		if (asprintf(&alloc_str2, "%s%s", str, buf) < 0)
			str = "Not enough memory!";
		else
			str = alloc_str2;
	}

	if (need_percent) {
		callchain_node__scnprintf_value(node, buf, sizeof(buf),
						total);

		if (asprintf(&alloc_str, "%s %s", buf, str) < 0)
			str = "Not enough memory!";
		else
			str = alloc_str;
	}

	print(browser, chain, str, offset, row, arg);
	free(alloc_str);
	free(alloc_str2);

	return ret;
}

static bool check_percent_display(struct rb_node *node, u64 parent_total)
{
	struct callchain_node *child;

	if (node == NULL)
		return false;

	if (rb_next(node))
		return true;

	child = rb_entry(node, struct callchain_node, rb_node);
	return callchain_cumul_hits(child) != parent_total;
}

static int hist_browser__show_callchain_flat(struct hist_browser *browser,
					     struct rb_root *root,
					     unsigned short row, u64 total,
					     u64 parent_total,
					     print_callchain_entry_fn print,
					     struct callchain_print_arg *arg,
					     check_output_full_fn is_output_full)
{
	struct rb_node *node;
	int first_row = row, offset = LEVEL_OFFSET_STEP;
	bool need_percent;

	node = rb_first(root);
	need_percent = check_percent_display(node, parent_total);

	while (node) {
		struct callchain_node *child = rb_entry(node, struct callchain_node, rb_node);
		struct rb_node *next = rb_next(node);
		struct callchain_list *chain;
		char folded_sign = ' ';
		int first = true;
		int extra_offset = 0;

		list_for_each_entry(chain, &child->parent_val, list) {
			bool was_first = first;

			if (first)
				first = false;
			else if (need_percent)
				extra_offset = LEVEL_OFFSET_STEP;

			folded_sign = callchain_list__folded(chain);

			row += hist_browser__show_callchain_list(browser, child,
							chain, row, total,
							was_first && need_percent,
							offset + extra_offset,
							print, arg);

			if (is_output_full(browser, row))
				goto out;

			if (folded_sign == '+')
				goto next;
		}

		list_for_each_entry(chain, &child->val, list) {
			bool was_first = first;

			if (first)
				first = false;
			else if (need_percent)
				extra_offset = LEVEL_OFFSET_STEP;

			folded_sign = callchain_list__folded(chain);

			row += hist_browser__show_callchain_list(browser, child,
							chain, row, total,
							was_first && need_percent,
							offset + extra_offset,
							print, arg);

			if (is_output_full(browser, row))
				goto out;

			if (folded_sign == '+')
				break;
		}

next:
		if (is_output_full(browser, row))
			break;
		node = next;
	}
out:
	return row - first_row;
}

static char *hist_browser__folded_callchain_str(struct hist_browser *browser,
						struct callchain_list *chain,
						char *value_str, char *old_str)
{
	char bf[1024];
	const char *str;
	char *new;

	str = callchain_list__sym_name(chain, bf, sizeof(bf),
				       browser->show_dso);
	if (old_str) {
		if (asprintf(&new, "%s%s%s", old_str,
			     symbol_conf.field_sep ?: ";", str) < 0)
			new = NULL;
	} else {
		if (value_str) {
			if (asprintf(&new, "%s %s", value_str, str) < 0)
				new = NULL;
		} else {
			if (asprintf(&new, "%s", str) < 0)
				new = NULL;
		}
	}
	return new;
}

static int hist_browser__show_callchain_folded(struct hist_browser *browser,
					       struct rb_root *root,
					       unsigned short row, u64 total,
					       u64 parent_total,
					       print_callchain_entry_fn print,
					       struct callchain_print_arg *arg,
					       check_output_full_fn is_output_full)
{
	struct rb_node *node;
	int first_row = row, offset = LEVEL_OFFSET_STEP;
	bool need_percent;

	node = rb_first(root);
	need_percent = check_percent_display(node, parent_total);

	while (node) {
		struct callchain_node *child = rb_entry(node, struct callchain_node, rb_node);
		struct rb_node *next = rb_next(node);
		struct callchain_list *chain, *first_chain = NULL;
		int first = true;
		char *value_str = NULL, *value_str_alloc = NULL;
		char *chain_str = NULL, *chain_str_alloc = NULL;

		if (arg->row_offset != 0) {
			arg->row_offset--;
			goto next;
		}

		if (need_percent) {
			char buf[64];

			callchain_node__scnprintf_value(child, buf, sizeof(buf), total);
			if (asprintf(&value_str, "%s", buf) < 0) {
				value_str = (char *)"<...>";
				goto do_print;
			}
			value_str_alloc = value_str;
		}

		list_for_each_entry(chain, &child->parent_val, list) {
			chain_str = hist_browser__folded_callchain_str(browser,
						chain, value_str, chain_str);
			if (first) {
				first = false;
				first_chain = chain;
			}

			if (chain_str == NULL) {
				chain_str = (char *)"Not enough memory!";
				goto do_print;
			}

			chain_str_alloc = chain_str;
		}

		list_for_each_entry(chain, &child->val, list) {
			chain_str = hist_browser__folded_callchain_str(browser,
						chain, value_str, chain_str);
			if (first) {
				first = false;
				first_chain = chain;
			}

			if (chain_str == NULL) {
				chain_str = (char *)"Not enough memory!";
				goto do_print;
			}

			chain_str_alloc = chain_str;
		}

do_print:
		print(browser, first_chain, chain_str, offset, row++, arg);
		free(value_str_alloc);
		free(chain_str_alloc);

next:
		if (is_output_full(browser, row))
			break;
		node = next;
	}

	return row - first_row;
}

static int hist_browser__show_callchain_graph(struct hist_browser *browser,
					struct rb_root *root, int level,
					unsigned short row, u64 total,
					u64 parent_total,
					print_callchain_entry_fn print,
					struct callchain_print_arg *arg,
					check_output_full_fn is_output_full)
{
	struct rb_node *node;
	int first_row = row, offset = level * LEVEL_OFFSET_STEP;
	bool need_percent;
	u64 percent_total = total;

	if (callchain_param.mode == CHAIN_GRAPH_REL)
		percent_total = parent_total;

	node = rb_first(root);
	need_percent = check_percent_display(node, parent_total);

	while (node) {
		struct callchain_node *child = rb_entry(node, struct callchain_node, rb_node);
		struct rb_node *next = rb_next(node);
		struct callchain_list *chain;
		char folded_sign = ' ';
		int first = true;
		int extra_offset = 0;

		list_for_each_entry(chain, &child->val, list) {
			bool was_first = first;

			if (first)
				first = false;
			else if (need_percent)
				extra_offset = LEVEL_OFFSET_STEP;

			folded_sign = callchain_list__folded(chain);

			row += hist_browser__show_callchain_list(browser, child,
							chain, row, percent_total,
							was_first && need_percent,
							offset + extra_offset,
							print, arg);

			if (is_output_full(browser, row))
				goto out;

			if (folded_sign == '+')
				break;
		}

		if (folded_sign == '-') {
			const int new_level = level + (extra_offset ? 2 : 1);

			row += hist_browser__show_callchain_graph(browser, &child->rb_root,
							    new_level, row, total,
							    child->children_hit,
							    print, arg, is_output_full);
		}
		if (is_output_full(browser, row))
			break;
		node = next;
	}
out:
	return row - first_row;
}

static int hist_browser__show_callchain(struct hist_browser *browser,
					struct hist_entry *entry, int level,
					unsigned short row,
					print_callchain_entry_fn print,
					struct callchain_print_arg *arg,
					check_output_full_fn is_output_full)
{
	u64 total = hists__total_period(entry->hists);
	u64 parent_total;
	int printed;

	if (symbol_conf.cumulate_callchain)
		parent_total = entry->stat_acc->period;
	else
		parent_total = entry->stat.period;

	if (callchain_param.mode == CHAIN_FLAT) {
		printed = hist_browser__show_callchain_flat(browser,
						&entry->sorted_chain, row,
						total, parent_total, print, arg,
						is_output_full);
	} else if (callchain_param.mode == CHAIN_FOLDED) {
		printed = hist_browser__show_callchain_folded(browser,
						&entry->sorted_chain, row,
						total, parent_total, print, arg,
						is_output_full);
	} else {
		printed = hist_browser__show_callchain_graph(browser,
						&entry->sorted_chain, level, row,
						total, parent_total, print, arg,
						is_output_full);
	}

	if (arg->is_current_entry)
		browser->he_selection = entry;

	return printed;
}

struct hpp_arg {
	struct ui_browser *b;
	char folded_sign;
	bool current_entry;
};

int __hpp__slsmg_color_printf(struct perf_hpp *hpp, const char *fmt, ...)
{
	struct hpp_arg *arg = hpp->ptr;
	int ret, len;
	va_list args;
	double percent;

	va_start(args, fmt);
	len = va_arg(args, int);
	percent = va_arg(args, double);
	va_end(args);

	ui_browser__set_percent_color(arg->b, percent, arg->current_entry);

	ret = scnprintf(hpp->buf, hpp->size, fmt, len, percent);
	ui_browser__printf(arg->b, "%s", hpp->buf);

	return ret;
}

#define __HPP_COLOR_PERCENT_FN(_type, _field)				\
static u64 __hpp_get_##_field(struct hist_entry *he)			\
{									\
	return he->stat._field;						\
}									\
									\
static int								\
hist_browser__hpp_color_##_type(struct perf_hpp_fmt *fmt,		\
				struct perf_hpp *hpp,			\
				struct hist_entry *he)			\
{									\
	return hpp__fmt(fmt, hpp, he, __hpp_get_##_field, " %*.2f%%",	\
			__hpp__slsmg_color_printf, true);		\
}

#define __HPP_COLOR_ACC_PERCENT_FN(_type, _field)			\
static u64 __hpp_get_acc_##_field(struct hist_entry *he)		\
{									\
	return he->stat_acc->_field;					\
}									\
									\
static int								\
hist_browser__hpp_color_##_type(struct perf_hpp_fmt *fmt,		\
				struct perf_hpp *hpp,			\
				struct hist_entry *he)			\
{									\
	if (!symbol_conf.cumulate_callchain) {				\
		struct hpp_arg *arg = hpp->ptr;				\
		int len = fmt->user_len ?: fmt->len;			\
		int ret = scnprintf(hpp->buf, hpp->size,		\
				    "%*s", len, "N/A");			\
		ui_browser__printf(arg->b, "%s", hpp->buf);		\
									\
		return ret;						\
	}								\
	return hpp__fmt(fmt, hpp, he, __hpp_get_acc_##_field,		\
			" %*.2f%%", __hpp__slsmg_color_printf, true);	\
}

__HPP_COLOR_PERCENT_FN(overhead, period)
__HPP_COLOR_PERCENT_FN(overhead_sys, period_sys)
__HPP_COLOR_PERCENT_FN(overhead_us, period_us)
__HPP_COLOR_PERCENT_FN(overhead_guest_sys, period_guest_sys)
__HPP_COLOR_PERCENT_FN(overhead_guest_us, period_guest_us)
__HPP_COLOR_ACC_PERCENT_FN(overhead_acc, period)

#undef __HPP_COLOR_PERCENT_FN
#undef __HPP_COLOR_ACC_PERCENT_FN

void hist_browser__init_hpp(void)
{
	perf_hpp__format[PERF_HPP__OVERHEAD].color =
				hist_browser__hpp_color_overhead;
	perf_hpp__format[PERF_HPP__OVERHEAD_SYS].color =
				hist_browser__hpp_color_overhead_sys;
	perf_hpp__format[PERF_HPP__OVERHEAD_US].color =
				hist_browser__hpp_color_overhead_us;
	perf_hpp__format[PERF_HPP__OVERHEAD_GUEST_SYS].color =
				hist_browser__hpp_color_overhead_guest_sys;
	perf_hpp__format[PERF_HPP__OVERHEAD_GUEST_US].color =
				hist_browser__hpp_color_overhead_guest_us;
	perf_hpp__format[PERF_HPP__OVERHEAD_ACC].color =
				hist_browser__hpp_color_overhead_acc;

	res_sample_init();
}

static int hist_browser__show_entry(struct hist_browser *browser,
				    struct hist_entry *entry,
				    unsigned short row)
{
	int printed = 0;
	int width = browser->b.width;
	char folded_sign = ' ';
	bool current_entry = ui_browser__is_current_entry(&browser->b, row);
	bool use_callchain = hist_entry__has_callchains(entry) && symbol_conf.use_callchain;
	off_t row_offset = entry->row_offset;
	bool first = true;
	struct perf_hpp_fmt *fmt;

	if (current_entry) {
		browser->he_selection = entry;
		browser->selection = &entry->ms;
	}

	if (use_callchain) {
		hist_entry__init_have_children(entry);
		folded_sign = hist_entry__folded(entry);
	}

	if (row_offset == 0) {
		struct hpp_arg arg = {
			.b		= &browser->b,
			.folded_sign	= folded_sign,
			.current_entry	= current_entry,
		};
		int column = 0;

		ui_browser__gotorc(&browser->b, row, 0);

		hists__for_each_format(browser->hists, fmt) {
			char s[2048];
			struct perf_hpp hpp = {
				.buf	= s,
				.size	= sizeof(s),
				.ptr	= &arg,
			};

			if (perf_hpp__should_skip(fmt, entry->hists) ||
			    column++ < browser->b.horiz_scroll)
				continue;

			if (current_entry && browser->b.navkeypressed) {
				ui_browser__set_color(&browser->b,
						      HE_COLORSET_SELECTED);
			} else {
				ui_browser__set_color(&browser->b,
						      HE_COLORSET_NORMAL);
			}

			if (first) {
				if (use_callchain) {
					ui_browser__printf(&browser->b, "%c ", folded_sign);
					width -= 2;
				}
				first = false;
			} else {
				ui_browser__printf(&browser->b, "  ");
				width -= 2;
			}

			if (fmt->color) {
				int ret = fmt->color(fmt, &hpp, entry);
				hist_entry__snprintf_alignment(entry, &hpp, fmt, ret);
				/*
				 * fmt->color() already used ui_browser to
				 * print the non alignment bits, skip it (+ret):
				 */
				ui_browser__printf(&browser->b, "%s", s + ret);
			} else {
				hist_entry__snprintf_alignment(entry, &hpp, fmt, fmt->entry(fmt, &hpp, entry));
				ui_browser__printf(&browser->b, "%s", s);
			}
			width -= hpp.buf - s;
		}

		/* The scroll bar isn't being used */
		if (!browser->b.navkeypressed)
			width += 1;

		ui_browser__write_nstring(&browser->b, "", width);

		++row;
		++printed;
	} else
		--row_offset;

	if (folded_sign == '-' && row != browser->b.rows) {
		struct callchain_print_arg arg = {
			.row_offset = row_offset,
			.is_current_entry = current_entry,
		};

		printed += hist_browser__show_callchain(browser,
				entry, 1, row,
				hist_browser__show_callchain_entry,
				&arg,
				hist_browser__check_output_full);
	}

	return printed;
}

static int hist_browser__show_hierarchy_entry(struct hist_browser *browser,
					      struct hist_entry *entry,
					      unsigned short row,
					      int level)
{
	int printed = 0;
	int width = browser->b.width;
	char folded_sign = ' ';
	bool current_entry = ui_browser__is_current_entry(&browser->b, row);
	off_t row_offset = entry->row_offset;
	bool first = true;
	struct perf_hpp_fmt *fmt;
	struct perf_hpp_list_node *fmt_node;
	struct hpp_arg arg = {
		.b		= &browser->b,
		.current_entry	= current_entry,
	};
	int column = 0;
	int hierarchy_indent = (entry->hists->nr_hpp_node - 2) * HIERARCHY_INDENT;

	if (current_entry) {
		browser->he_selection = entry;
		browser->selection = &entry->ms;
	}

	hist_entry__init_have_children(entry);
	folded_sign = hist_entry__folded(entry);
	arg.folded_sign = folded_sign;

	if (entry->leaf && row_offset) {
		row_offset--;
		goto show_callchain;
	}

	ui_browser__gotorc(&browser->b, row, 0);

	if (current_entry && browser->b.navkeypressed)
		ui_browser__set_color(&browser->b, HE_COLORSET_SELECTED);
	else
		ui_browser__set_color(&browser->b, HE_COLORSET_NORMAL);

	ui_browser__write_nstring(&browser->b, "", level * HIERARCHY_INDENT);
	width -= level * HIERARCHY_INDENT;

	/* the first hpp_list_node is for overhead columns */
	fmt_node = list_first_entry(&entry->hists->hpp_formats,
				    struct perf_hpp_list_node, list);
	perf_hpp_list__for_each_format(&fmt_node->hpp, fmt) {
		char s[2048];
		struct perf_hpp hpp = {
			.buf		= s,
			.size		= sizeof(s),
			.ptr		= &arg,
		};

		if (perf_hpp__should_skip(fmt, entry->hists) ||
		    column++ < browser->b.horiz_scroll)
			continue;

		if (current_entry && browser->b.navkeypressed) {
			ui_browser__set_color(&browser->b,
					      HE_COLORSET_SELECTED);
		} else {
			ui_browser__set_color(&browser->b,
					      HE_COLORSET_NORMAL);
		}

		if (first) {
			ui_browser__printf(&browser->b, "%c ", folded_sign);
			width -= 2;
			first = false;
		} else {
			ui_browser__printf(&browser->b, "  ");
			width -= 2;
		}

		if (fmt->color) {
			int ret = fmt->color(fmt, &hpp, entry);
			hist_entry__snprintf_alignment(entry, &hpp, fmt, ret);
			/*
			 * fmt->color() already used ui_browser to
			 * print the non alignment bits, skip it (+ret):
			 */
			ui_browser__printf(&browser->b, "%s", s + ret);
		} else {
			int ret = fmt->entry(fmt, &hpp, entry);
			hist_entry__snprintf_alignment(entry, &hpp, fmt, ret);
			ui_browser__printf(&browser->b, "%s", s);
		}
		width -= hpp.buf - s;
	}

	if (!first) {
		ui_browser__write_nstring(&browser->b, "", hierarchy_indent);
		width -= hierarchy_indent;
	}

	if (column >= browser->b.horiz_scroll) {
		char s[2048];
		struct perf_hpp hpp = {
			.buf		= s,
			.size		= sizeof(s),
			.ptr		= &arg,
		};

		if (current_entry && browser->b.navkeypressed) {
			ui_browser__set_color(&browser->b,
					      HE_COLORSET_SELECTED);
		} else {
			ui_browser__set_color(&browser->b,
					      HE_COLORSET_NORMAL);
		}

		perf_hpp_list__for_each_format(entry->hpp_list, fmt) {
			if (first) {
				ui_browser__printf(&browser->b, "%c ", folded_sign);
				first = false;
			} else {
				ui_browser__write_nstring(&browser->b, "", 2);
			}

			width -= 2;

			/*
			 * No need to call hist_entry__snprintf_alignment()
			 * since this fmt is always the last column in the
			 * hierarchy mode.
			 */
			if (fmt->color) {
				width -= fmt->color(fmt, &hpp, entry);
			} else {
				int i = 0;

				width -= fmt->entry(fmt, &hpp, entry);
				ui_browser__printf(&browser->b, "%s", skip_spaces(s));

				while (isspace(s[i++]))
					width++;
			}
		}
	}

	/* The scroll bar isn't being used */
	if (!browser->b.navkeypressed)
		width += 1;

	ui_browser__write_nstring(&browser->b, "", width);

	++row;
	++printed;

show_callchain:
	if (entry->leaf && folded_sign == '-' && row != browser->b.rows) {
		struct callchain_print_arg carg = {
			.row_offset = row_offset,
		};

		printed += hist_browser__show_callchain(browser, entry,
					level + 1, row,
					hist_browser__show_callchain_entry, &carg,
					hist_browser__check_output_full);
	}

	return printed;
}

static int hist_browser__show_no_entry(struct hist_browser *browser,
				       unsigned short row, int level)
{
	int width = browser->b.width;
	bool current_entry = ui_browser__is_current_entry(&browser->b, row);
	bool first = true;
	int column = 0;
	int ret;
	struct perf_hpp_fmt *fmt;
	struct perf_hpp_list_node *fmt_node;
	int indent = browser->hists->nr_hpp_node - 2;

	if (current_entry) {
		browser->he_selection = NULL;
		browser->selection = NULL;
	}

	ui_browser__gotorc(&browser->b, row, 0);

	if (current_entry && browser->b.navkeypressed)
		ui_browser__set_color(&browser->b, HE_COLORSET_SELECTED);
	else
		ui_browser__set_color(&browser->b, HE_COLORSET_NORMAL);

	ui_browser__write_nstring(&browser->b, "", level * HIERARCHY_INDENT);
	width -= level * HIERARCHY_INDENT;

	/* the first hpp_list_node is for overhead columns */
	fmt_node = list_first_entry(&browser->hists->hpp_formats,
				    struct perf_hpp_list_node, list);
	perf_hpp_list__for_each_format(&fmt_node->hpp, fmt) {
		if (perf_hpp__should_skip(fmt, browser->hists) ||
		    column++ < browser->b.horiz_scroll)
			continue;

		ret = fmt->width(fmt, NULL, browser->hists);

		if (first) {
			/* for folded sign */
			first = false;
			ret++;
		} else {
			/* space between columns */
			ret += 2;
		}

		ui_browser__write_nstring(&browser->b, "", ret);
		width -= ret;
	}

	ui_browser__write_nstring(&browser->b, "", indent * HIERARCHY_INDENT);
	width -= indent * HIERARCHY_INDENT;

	if (column >= browser->b.horiz_scroll) {
		char buf[32];

		ret = snprintf(buf, sizeof(buf), "no entry >= %.2f%%", browser->min_pcnt);
		ui_browser__printf(&browser->b, "  %s", buf);
		width -= ret + 2;
	}

	/* The scroll bar isn't being used */
	if (!browser->b.navkeypressed)
		width += 1;

	ui_browser__write_nstring(&browser->b, "", width);
	return 1;
}

static int advance_hpp_check(struct perf_hpp *hpp, int inc)
{
	advance_hpp(hpp, inc);
	return hpp->size <= 0;
}

static int
hists_browser__scnprintf_headers(struct hist_browser *browser, char *buf,
				 size_t size, int line)
{
	struct hists *hists = browser->hists;
	struct perf_hpp dummy_hpp = {
		.buf    = buf,
		.size   = size,
	};
	struct perf_hpp_fmt *fmt;
	size_t ret = 0;
	int column = 0;
	int span = 0;

	if (hists__has_callchains(hists) && symbol_conf.use_callchain) {
		ret = scnprintf(buf, size, "  ");
		if (advance_hpp_check(&dummy_hpp, ret))
			return ret;
	}

	hists__for_each_format(browser->hists, fmt) {
		if (perf_hpp__should_skip(fmt, hists)  || column++ < browser->b.horiz_scroll)
			continue;

		ret = fmt->header(fmt, &dummy_hpp, hists, line, &span);
		if (advance_hpp_check(&dummy_hpp, ret))
			break;

		if (span)
			continue;

		ret = scnprintf(dummy_hpp.buf, dummy_hpp.size, "  ");
		if (advance_hpp_check(&dummy_hpp, ret))
			break;
	}

	return ret;
}

static int hists_browser__scnprintf_hierarchy_headers(struct hist_browser *browser, char *buf, size_t size)
{
	struct hists *hists = browser->hists;
	struct perf_hpp dummy_hpp = {
		.buf    = buf,
		.size   = size,
	};
	struct perf_hpp_fmt *fmt;
	struct perf_hpp_list_node *fmt_node;
	size_t ret = 0;
	int column = 0;
	int indent = hists->nr_hpp_node - 2;
	bool first_node, first_col;

	ret = scnprintf(buf, size, "  ");
	if (advance_hpp_check(&dummy_hpp, ret))
		return ret;

	first_node = true;
	/* the first hpp_list_node is for overhead columns */
	fmt_node = list_first_entry(&hists->hpp_formats,
				    struct perf_hpp_list_node, list);
	perf_hpp_list__for_each_format(&fmt_node->hpp, fmt) {
		if (column++ < browser->b.horiz_scroll)
			continue;

		ret = fmt->header(fmt, &dummy_hpp, hists, 0, NULL);
		if (advance_hpp_check(&dummy_hpp, ret))
			break;

		ret = scnprintf(dummy_hpp.buf, dummy_hpp.size, "  ");
		if (advance_hpp_check(&dummy_hpp, ret))
			break;

		first_node = false;
	}

	if (!first_node) {
		ret = scnprintf(dummy_hpp.buf, dummy_hpp.size, "%*s",
				indent * HIERARCHY_INDENT, "");
		if (advance_hpp_check(&dummy_hpp, ret))
			return ret;
	}

	first_node = true;
	list_for_each_entry_continue(fmt_node, &hists->hpp_formats, list) {
		if (!first_node) {
			ret = scnprintf(dummy_hpp.buf, dummy_hpp.size, " / ");
			if (advance_hpp_check(&dummy_hpp, ret))
				break;
		}
		first_node = false;

		first_col = true;
		perf_hpp_list__for_each_format(&fmt_node->hpp, fmt) {
			char *start;

			if (perf_hpp__should_skip(fmt, hists))
				continue;

			if (!first_col) {
				ret = scnprintf(dummy_hpp.buf, dummy_hpp.size, "+");
				if (advance_hpp_check(&dummy_hpp, ret))
					break;
			}
			first_col = false;

			ret = fmt->header(fmt, &dummy_hpp, hists, 0, NULL);
			dummy_hpp.buf[ret] = '\0';

			start = strim(dummy_hpp.buf);
			ret = strlen(start);

			if (start != dummy_hpp.buf)
				memmove(dummy_hpp.buf, start, ret + 1);

			if (advance_hpp_check(&dummy_hpp, ret))
				break;
		}
	}

	return ret;
}

static void hists_browser__hierarchy_headers(struct hist_browser *browser)
{
	char headers[1024];

	hists_browser__scnprintf_hierarchy_headers(browser, headers,
						   sizeof(headers));

	ui_browser__gotorc(&browser->b, 0, 0);
	ui_browser__set_color(&browser->b, HE_COLORSET_ROOT);
	ui_browser__write_nstring(&browser->b, headers, browser->b.width + 1);
}

static void hists_browser__headers(struct hist_browser *browser)
{
	struct hists *hists = browser->hists;
	struct perf_hpp_list *hpp_list = hists->hpp_list;

	int line;

	for (line = 0; line < hpp_list->nr_header_lines; line++) {
		char headers[1024];

		hists_browser__scnprintf_headers(browser, headers,
						 sizeof(headers), line);

		ui_browser__gotorc_title(&browser->b, line, 0);
		ui_browser__set_color(&browser->b, HE_COLORSET_ROOT);
		ui_browser__write_nstring(&browser->b, headers, browser->b.width + 1);
	}
}

static void hist_browser__show_headers(struct hist_browser *browser)
{
	if (symbol_conf.report_hierarchy)
		hists_browser__hierarchy_headers(browser);
	else
		hists_browser__headers(browser);
}

static void ui_browser__hists_init_top(struct ui_browser *browser)
{
	if (browser->top == NULL) {
		struct hist_browser *hb;

		hb = container_of(browser, struct hist_browser, b);
		browser->top = rb_first_cached(&hb->hists->entries);
	}
}

static unsigned int hist_browser__refresh(struct ui_browser *browser)
{
	unsigned row = 0;
	struct rb_node *nd;
	struct hist_browser *hb = container_of(browser, struct hist_browser, b);

	if (hb->show_headers)
		hist_browser__show_headers(hb);

	ui_browser__hists_init_top(browser);
	hb->he_selection = NULL;
	hb->selection = NULL;

	for (nd = browser->top; nd; nd = rb_hierarchy_next(nd)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		float percent;

		if (h->filtered) {
			/* let it move to sibling */
			h->unfolded = false;
			continue;
		}

		if (symbol_conf.report_individual_block)
			percent = block_info__total_cycles_percent(h);
		else
			percent = hist_entry__get_percent_limit(h);

		if (percent < hb->min_pcnt)
			continue;

		if (symbol_conf.report_hierarchy) {
			row += hist_browser__show_hierarchy_entry(hb, h, row,
								  h->depth);
			if (row == browser->rows)
				break;

			if (h->has_no_entry) {
				hist_browser__show_no_entry(hb, row, h->depth + 1);
				row++;
			}
		} else {
			row += hist_browser__show_entry(hb, h, row);
		}

		if (row == browser->rows)
			break;
	}

	return row;
}

static struct rb_node *hists__filter_entries(struct rb_node *nd,
					     float min_pcnt)
{
	while (nd != NULL) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		float percent = hist_entry__get_percent_limit(h);

		if (!h->filtered && percent >= min_pcnt)
			return nd;

		/*
		 * If it's filtered, its all children also were filtered.
		 * So move to sibling node.
		 */
		if (rb_next(nd))
			nd = rb_next(nd);
		else
			nd = rb_hierarchy_next(nd);
	}

	return NULL;
}

static struct rb_node *hists__filter_prev_entries(struct rb_node *nd,
						  float min_pcnt)
{
	while (nd != NULL) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		float percent = hist_entry__get_percent_limit(h);

		if (!h->filtered && percent >= min_pcnt)
			return nd;

		nd = rb_hierarchy_prev(nd);
	}

	return NULL;
}

static void ui_browser__hists_seek(struct ui_browser *browser,
				   off_t offset, int whence)
{
	struct hist_entry *h;
	struct rb_node *nd;
	bool first = true;
	struct hist_browser *hb;

	hb = container_of(browser, struct hist_browser, b);

	if (browser->nr_entries == 0)
		return;

	ui_browser__hists_init_top(browser);

	switch (whence) {
	case SEEK_SET:
		nd = hists__filter_entries(rb_first(browser->entries),
					   hb->min_pcnt);
		break;
	case SEEK_CUR:
		nd = browser->top;
		goto do_offset;
	case SEEK_END:
		nd = rb_hierarchy_last(rb_last(browser->entries));
		nd = hists__filter_prev_entries(nd, hb->min_pcnt);
		first = false;
		break;
	default:
		return;
	}

	/*
	 * Moves not relative to the first visible entry invalidates its
	 * row_offset:
	 */
	h = rb_entry(browser->top, struct hist_entry, rb_node);
	h->row_offset = 0;

	/*
	 * Here we have to check if nd is expanded (+), if it is we can't go
	 * the next top level hist_entry, instead we must compute an offset of
	 * what _not_ to show and not change the first visible entry.
	 *
	 * This offset increments when we are going from top to bottom and
	 * decreases when we're going from bottom to top.
	 *
	 * As we don't have backpointers to the top level in the callchains
	 * structure, we need to always print the whole hist_entry callchain,
	 * skipping the first ones that are before the first visible entry
	 * and stop when we printed enough lines to fill the screen.
	 */
do_offset:
	if (!nd)
		return;

	if (offset > 0) {
		do {
			h = rb_entry(nd, struct hist_entry, rb_node);
			if (h->unfolded && h->leaf) {
				u16 remaining = h->nr_rows - h->row_offset;
				if (offset > remaining) {
					offset -= remaining;
					h->row_offset = 0;
				} else {
					h->row_offset += offset;
					offset = 0;
					browser->top = nd;
					break;
				}
			}
			nd = hists__filter_entries(rb_hierarchy_next(nd),
						   hb->min_pcnt);
			if (nd == NULL)
				break;
			--offset;
			browser->top = nd;
		} while (offset != 0);
	} else if (offset < 0) {
		while (1) {
			h = rb_entry(nd, struct hist_entry, rb_node);
			if (h->unfolded && h->leaf) {
				if (first) {
					if (-offset > h->row_offset) {
						offset += h->row_offset;
						h->row_offset = 0;
					} else {
						h->row_offset += offset;
						offset = 0;
						browser->top = nd;
						break;
					}
				} else {
					if (-offset > h->nr_rows) {
						offset += h->nr_rows;
						h->row_offset = 0;
					} else {
						h->row_offset = h->nr_rows + offset;
						offset = 0;
						browser->top = nd;
						break;
					}
				}
			}

			nd = hists__filter_prev_entries(rb_hierarchy_prev(nd),
							hb->min_pcnt);
			if (nd == NULL)
				break;
			++offset;
			browser->top = nd;
			if (offset == 0) {
				/*
				 * Last unfiltered hist_entry, check if it is
				 * unfolded, if it is then we should have
				 * row_offset at its last entry.
				 */
				h = rb_entry(nd, struct hist_entry, rb_node);
				if (h->unfolded && h->leaf)
					h->row_offset = h->nr_rows;
				break;
			}
			first = false;
		}
	} else {
		browser->top = nd;
		h = rb_entry(nd, struct hist_entry, rb_node);
		h->row_offset = 0;
	}
}

static int hist_browser__fprintf_callchain(struct hist_browser *browser,
					   struct hist_entry *he, FILE *fp,
					   int level)
{
	struct callchain_print_arg arg  = {
		.fp = fp,
	};

	hist_browser__show_callchain(browser, he, level, 0,
				     hist_browser__fprintf_callchain_entry, &arg,
				     hist_browser__check_dump_full);
	return arg.printed;
}

static int hist_browser__fprintf_entry(struct hist_browser *browser,
				       struct hist_entry *he, FILE *fp)
{
	char s[8192];
	int printed = 0;
	char folded_sign = ' ';
	struct perf_hpp hpp = {
		.buf = s,
		.size = sizeof(s),
	};
	struct perf_hpp_fmt *fmt;
	bool first = true;
	int ret;

	if (hist_entry__has_callchains(he) && symbol_conf.use_callchain) {
		folded_sign = hist_entry__folded(he);
		printed += fprintf(fp, "%c ", folded_sign);
	}

	hists__for_each_format(browser->hists, fmt) {
		if (perf_hpp__should_skip(fmt, he->hists))
			continue;

		if (!first) {
			ret = scnprintf(hpp.buf, hpp.size, "  ");
			advance_hpp(&hpp, ret);
		} else
			first = false;

		ret = fmt->entry(fmt, &hpp, he);
		ret = hist_entry__snprintf_alignment(he, &hpp, fmt, ret);
		advance_hpp(&hpp, ret);
	}
	printed += fprintf(fp, "%s\n", s);

	if (folded_sign == '-')
		printed += hist_browser__fprintf_callchain(browser, he, fp, 1);

	return printed;
}


static int hist_browser__fprintf_hierarchy_entry(struct hist_browser *browser,
						 struct hist_entry *he,
						 FILE *fp, int level)
{
	char s[8192];
	int printed = 0;
	char folded_sign = ' ';
	struct perf_hpp hpp = {
		.buf = s,
		.size = sizeof(s),
	};
	struct perf_hpp_fmt *fmt;
	struct perf_hpp_list_node *fmt_node;
	bool first = true;
	int ret;
	int hierarchy_indent = (he->hists->nr_hpp_node - 2) * HIERARCHY_INDENT;

	printed = fprintf(fp, "%*s", level * HIERARCHY_INDENT, "");

	folded_sign = hist_entry__folded(he);
	printed += fprintf(fp, "%c", folded_sign);

	/* the first hpp_list_node is for overhead columns */
	fmt_node = list_first_entry(&he->hists->hpp_formats,
				    struct perf_hpp_list_node, list);
	perf_hpp_list__for_each_format(&fmt_node->hpp, fmt) {
		if (!first) {
			ret = scnprintf(hpp.buf, hpp.size, "  ");
			advance_hpp(&hpp, ret);
		} else
			first = false;

		ret = fmt->entry(fmt, &hpp, he);
		advance_hpp(&hpp, ret);
	}

	ret = scnprintf(hpp.buf, hpp.size, "%*s", hierarchy_indent, "");
	advance_hpp(&hpp, ret);

	perf_hpp_list__for_each_format(he->hpp_list, fmt) {
		ret = scnprintf(hpp.buf, hpp.size, "  ");
		advance_hpp(&hpp, ret);

		ret = fmt->entry(fmt, &hpp, he);
		advance_hpp(&hpp, ret);
	}

	strim(s);
	printed += fprintf(fp, "%s\n", s);

	if (he->leaf && folded_sign == '-') {
		printed += hist_browser__fprintf_callchain(browser, he, fp,
							   he->depth + 1);
	}

	return printed;
}

static int hist_browser__fprintf(struct hist_browser *browser, FILE *fp)
{
	struct rb_node *nd = hists__filter_entries(rb_first(browser->b.entries),
						   browser->min_pcnt);
	int printed = 0;

	while (nd) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);

		if (symbol_conf.report_hierarchy) {
			printed += hist_browser__fprintf_hierarchy_entry(browser,
									 h, fp,
									 h->depth);
		} else {
			printed += hist_browser__fprintf_entry(browser, h, fp);
		}

		nd = hists__filter_entries(rb_hierarchy_next(nd),
					   browser->min_pcnt);
	}

	return printed;
}

static int hist_browser__dump(struct hist_browser *browser)
{
	char filename[64];
	FILE *fp;

	while (1) {
		scnprintf(filename, sizeof(filename), "perf.hist.%d", browser->print_seq);
		if (access(filename, F_OK))
			break;
		/*
 		 * XXX: Just an arbitrary lazy upper limit
 		 */
		if (++browser->print_seq == 8192) {
			ui_helpline__fpush("Too many perf.hist.N files, nothing written!");
			return -1;
		}
	}

	fp = fopen(filename, "w");
	if (fp == NULL) {
		char bf[64];
		const char *err = str_error_r(errno, bf, sizeof(bf));
		ui_helpline__fpush("Couldn't write to %s: %s", filename, err);
		return -1;
	}

	++browser->print_seq;
	hist_browser__fprintf(browser, fp);
	fclose(fp);
	ui_helpline__fpush("%s written!", filename);

	return 0;
}

void hist_browser__init(struct hist_browser *browser,
			struct hists *hists)
{
	struct perf_hpp_fmt *fmt;

	browser->hists			= hists;
	browser->b.refresh		= hist_browser__refresh;
	browser->b.refresh_dimensions	= hist_browser__refresh_dimensions;
	browser->b.seek			= ui_browser__hists_seek;
	browser->b.use_navkeypressed	= true;
	browser->show_headers		= symbol_conf.show_hist_headers;
	hist_browser__set_title_space(browser);

	if (symbol_conf.report_hierarchy) {
		struct perf_hpp_list_node *fmt_node;

		/* count overhead columns (in the first node) */
		fmt_node = list_first_entry(&hists->hpp_formats,
					    struct perf_hpp_list_node, list);
		perf_hpp_list__for_each_format(&fmt_node->hpp, fmt)
			++browser->b.columns;

		/* add a single column for whole hierarchy sort keys*/
		++browser->b.columns;
	} else {
		hists__for_each_format(hists, fmt)
			++browser->b.columns;
	}

	hists__reset_column_width(hists);
}

struct hist_browser *hist_browser__new(struct hists *hists)
{
	struct hist_browser *browser = zalloc(sizeof(*browser));

	if (browser)
		hist_browser__init(browser, hists);

	return browser;
}

static struct hist_browser *
perf_evsel_browser__new(struct evsel *evsel,
			struct hist_browser_timer *hbt,
			struct perf_env *env,
			struct annotation_options *annotation_opts)
{
	struct hist_browser *browser = hist_browser__new(evsel__hists(evsel));

	if (browser) {
		browser->hbt   = hbt;
		browser->env   = env;
		browser->title = hists_browser__scnprintf_title;
		browser->annotation_opts = annotation_opts;
	}
	return browser;
}

void hist_browser__delete(struct hist_browser *browser)
{
	free(browser);
}

static struct hist_entry *hist_browser__selected_entry(struct hist_browser *browser)
{
	return browser->he_selection;
}

static struct thread *hist_browser__selected_thread(struct hist_browser *browser)
{
	return browser->he_selection->thread;
}

/* Check whether the browser is for 'top' or 'report' */
static inline bool is_report_browser(void *timer)
{
	return timer == NULL;
}

static int hists_browser__scnprintf_title(struct hist_browser *browser, char *bf, size_t size)
{
	struct hist_browser_timer *hbt = browser->hbt;
	int printed = __hists__scnprintf_title(browser->hists, bf, size, !is_report_browser(hbt));

	if (!is_report_browser(hbt)) {
		struct perf_top *top = hbt->arg;

		printed += scnprintf(bf + printed, size - printed,
				     " lost: %" PRIu64 "/%" PRIu64,
				     top->lost, top->lost_total);

		printed += scnprintf(bf + printed, size - printed,
				     " drop: %" PRIu64 "/%" PRIu64,
				     top->drop, top->drop_total);

		if (top->zero)
			printed += scnprintf(bf + printed, size - printed, " [z]");

		perf_top__reset_sample_counters(top);
	}


	return printed;
}

static inline void free_popup_options(char **options, int n)
{
	int i;

	for (i = 0; i < n; ++i)
		zfree(&options[i]);
}

/*
 * Only runtime switching of perf data file will make "input_name" point
 * to a malloced buffer. So add "is_input_name_malloced" flag to decide
 * whether we need to call free() for current "input_name" during the switch.
 */
static bool is_input_name_malloced = false;

static int switch_data_file(void)
{
	char *pwd, *options[32], *abs_path[32], *tmp;
	DIR *pwd_dir;
	int nr_options = 0, choice = -1, ret = -1;
	struct dirent *dent;

	pwd = getenv("PWD");
	if (!pwd)
		return ret;

	pwd_dir = opendir(pwd);
	if (!pwd_dir)
		return ret;

	memset(options, 0, sizeof(options));
	memset(abs_path, 0, sizeof(abs_path));

	while ((dent = readdir(pwd_dir))) {
		char path[PATH_MAX];
		u64 magic;
		char *name = dent->d_name;
		FILE *file;

		if (!(dent->d_type == DT_REG))
			continue;

		snprintf(path, sizeof(path), "%s/%s", pwd, name);

		file = fopen(path, "r");
		if (!file)
			continue;

		if (fread(&magic, 1, 8, file) < 8)
			goto close_file_and_continue;

		if (is_perf_magic(magic)) {
			options[nr_options] = strdup(name);
			if (!options[nr_options])
				goto close_file_and_continue;

			abs_path[nr_options] = strdup(path);
			if (!abs_path[nr_options]) {
				zfree(&options[nr_options]);
				ui__warning("Can't search all data files due to memory shortage.\n");
				fclose(file);
				break;
			}

			nr_options++;
		}

close_file_and_continue:
		fclose(file);
		if (nr_options >= 32) {
			ui__warning("Too many perf data files in PWD!\n"
				    "Only the first 32 files will be listed.\n");
			break;
		}
	}
	closedir(pwd_dir);

	if (nr_options) {
		choice = ui__popup_menu(nr_options, options, NULL);
		if (choice < nr_options && choice >= 0) {
			tmp = strdup(abs_path[choice]);
			if (tmp) {
				if (is_input_name_malloced)
					free((void *)input_name);
				input_name = tmp;
				is_input_name_malloced = true;
				ret = 0;
			} else
				ui__warning("Data switch failed due to memory shortage!\n");
		}
	}

	free_popup_options(options, nr_options);
	free_popup_options(abs_path, nr_options);
	return ret;
}

struct popup_action {
	unsigned long		time;
	struct thread 		*thread;
	struct map_symbol 	ms;
	int			socket;
	struct evsel	*evsel;
	enum rstype		rstype;

	int (*fn)(struct hist_browser *browser, struct popup_action *act);
};

static int
do_annotate(struct hist_browser *browser, struct popup_action *act)
{
	struct evsel *evsel;
	struct annotation *notes;
	struct hist_entry *he;
	int err;

	if (!browser->annotation_opts->objdump_path &&
	    perf_env__lookup_objdump(browser->env, &browser->annotation_opts->objdump_path))
		return 0;

	notes = symbol__annotation(act->ms.sym);
	if (!notes->src)
		return 0;

	if (browser->block_evsel)
		evsel = browser->block_evsel;
	else
		evsel = hists_to_evsel(browser->hists);

	err = map_symbol__tui_annotate(&act->ms, evsel, browser->hbt,
				       browser->annotation_opts);
	he = hist_browser__selected_entry(browser);
	/*
	 * offer option to annotate the other branch source or target
	 * (if they exists) when returning from annotate
	 */
	if ((err == 'q' || err == CTRL('c')) && he->branch_info)
		return 1;

	ui_browser__update_nr_entries(&browser->b, browser->hists->nr_entries);
	if (err)
		ui_browser__handle_resize(&browser->b);
	return 0;
}

static struct symbol *symbol__new_unresolved(u64 addr, struct map *map)
{
	struct annotated_source *src;
	struct symbol *sym;
	char name[64];

	snprintf(name, sizeof(name), "%.*" PRIx64, BITS_PER_LONG / 4, addr);

	sym = symbol__new(addr, ANNOTATION_DUMMY_LEN, 0, 0, name);
	if (sym) {
		src = symbol__hists(sym, 1);
		if (!src) {
			symbol__delete(sym);
			return NULL;
		}

		dso__insert_symbol(map->dso, sym);
	}

	return sym;
}

static int
add_annotate_opt(struct hist_browser *browser __maybe_unused,
		 struct popup_action *act, char **optstr,
		 struct map_symbol *ms,
		 u64 addr)
{
	if (!ms->map || !ms->map->dso || ms->map->dso->annotate_warned)
		return 0;

	if (!ms->sym)
		ms->sym = symbol__new_unresolved(addr, ms->map);

	if (ms->sym == NULL || symbol__annotation(ms->sym)->src == NULL)
		return 0;

	if (asprintf(optstr, "Annotate %s", ms->sym->name) < 0)
		return 0;

	act->ms = *ms;
	act->fn = do_annotate;
	return 1;
}

static int
do_zoom_thread(struct hist_browser *browser, struct popup_action *act)
{
	struct thread *thread = act->thread;

	if ((!hists__has(browser->hists, thread) &&
	     !hists__has(browser->hists, comm)) || thread == NULL)
		return 0;

	if (browser->hists->thread_filter) {
		pstack__remove(browser->pstack, &browser->hists->thread_filter);
		perf_hpp__set_elide(HISTC_THREAD, false);
		thread__zput(browser->hists->thread_filter);
		ui_helpline__pop();
	} else {
		if (hists__has(browser->hists, thread)) {
			ui_helpline__fpush("To zoom out press ESC or ENTER + \"Zoom out of %s(%d) thread\"",
					   thread->comm_set ? thread__comm_str(thread) : "",
					   thread->tid);
		} else {
			ui_helpline__fpush("To zoom out press ESC or ENTER + \"Zoom out of %s thread\"",
					   thread->comm_set ? thread__comm_str(thread) : "");
		}

		browser->hists->thread_filter = thread__get(thread);
		perf_hpp__set_elide(HISTC_THREAD, false);
		pstack__push(browser->pstack, &browser->hists->thread_filter);
	}

	hists__filter_by_thread(browser->hists);
	hist_browser__reset(browser);
	return 0;
}

static int
add_thread_opt(struct hist_browser *browser, struct popup_action *act,
	       char **optstr, struct thread *thread)
{
	int ret;

	if ((!hists__has(browser->hists, thread) &&
	     !hists__has(browser->hists, comm)) || thread == NULL)
		return 0;

	if (hists__has(browser->hists, thread)) {
		ret = asprintf(optstr, "Zoom %s %s(%d) thread",
			       browser->hists->thread_filter ? "out of" : "into",
			       thread->comm_set ? thread__comm_str(thread) : "",
			       thread->tid);
	} else {
		ret = asprintf(optstr, "Zoom %s %s thread",
			       browser->hists->thread_filter ? "out of" : "into",
			       thread->comm_set ? thread__comm_str(thread) : "");
	}
	if (ret < 0)
		return 0;

	act->thread = thread;
	act->fn = do_zoom_thread;
	return 1;
}

static int hists_browser__zoom_map(struct hist_browser *browser, struct map *map)
{
	if (!hists__has(browser->hists, dso) || map == NULL)
		return 0;

	if (browser->hists->dso_filter) {
		pstack__remove(browser->pstack, &browser->hists->dso_filter);
		perf_hpp__set_elide(HISTC_DSO, false);
		browser->hists->dso_filter = NULL;
		ui_helpline__pop();
	} else {
		ui_helpline__fpush("To zoom out press ESC or ENTER + \"Zoom out of %s DSO\"",
				   __map__is_kernel(map) ? "the Kernel" : map->dso->short_name);
		browser->hists->dso_filter = map->dso;
		perf_hpp__set_elide(HISTC_DSO, true);
		pstack__push(browser->pstack, &browser->hists->dso_filter);
	}

	hists__filter_by_dso(browser->hists);
	hist_browser__reset(browser);
	return 0;
}

static int
do_zoom_dso(struct hist_browser *browser, struct popup_action *act)
{
	return hists_browser__zoom_map(browser, act->ms.map);
}

static int
add_dso_opt(struct hist_browser *browser, struct popup_action *act,
	    char **optstr, struct map *map)
{
	if (!hists__has(browser->hists, dso) || map == NULL)
		return 0;

	if (asprintf(optstr, "Zoom %s %s DSO (use the 'k' hotkey to zoom directly into the kernel)",
		     browser->hists->dso_filter ? "out of" : "into",
		     __map__is_kernel(map) ? "the Kernel" : map->dso->short_name) < 0)
		return 0;

	act->ms.map = map;
	act->fn = do_zoom_dso;
	return 1;
}

static int do_toggle_callchain(struct hist_browser *browser, struct popup_action *act __maybe_unused)
{
	hist_browser__toggle_fold(browser);
	return 0;
}

static int add_callchain_toggle_opt(struct hist_browser *browser, struct popup_action *act, char **optstr)
{
	char sym_name[512];

        if (!hist_browser__selection_has_children(browser))
                return 0;

	if (asprintf(optstr, "%s [%s] callchain (one level, same as '+' hotkey, use 'e'/'c' for the whole main level entry)",
		     hist_browser__selection_unfolded(browser) ? "Collapse" : "Expand",
		     hist_browser__selection_sym_name(browser, sym_name, sizeof(sym_name))) < 0)
		return 0;

	act->fn = do_toggle_callchain;
	return 1;
}

static int
do_browse_map(struct hist_browser *browser __maybe_unused,
	      struct popup_action *act)
{
	map__browse(act->ms.map);
	return 0;
}

static int
add_map_opt(struct hist_browser *browser,
	    struct popup_action *act, char **optstr, struct map *map)
{
	if (!hists__has(browser->hists, dso) || map == NULL)
		return 0;

	if (asprintf(optstr, "Browse map details") < 0)
		return 0;

	act->ms.map = map;
	act->fn = do_browse_map;
	return 1;
}

static int
do_run_script(struct hist_browser *browser __maybe_unused,
	      struct popup_action *act)
{
	char *script_opt;
	int len;
	int n = 0;

	len = 100;
	if (act->thread)
		len += strlen(thread__comm_str(act->thread));
	else if (act->ms.sym)
		len += strlen(act->ms.sym->name);
	script_opt = malloc(len);
	if (!script_opt)
		return -1;

	script_opt[0] = 0;
	if (act->thread) {
		n = scnprintf(script_opt, len, " -c %s ",
			  thread__comm_str(act->thread));
	} else if (act->ms.sym) {
		n = scnprintf(script_opt, len, " -S %s ",
			  act->ms.sym->name);
	}

	if (act->time) {
		char start[32], end[32];
		unsigned long starttime = act->time;
		unsigned long endtime = act->time + symbol_conf.time_quantum;

		if (starttime == endtime) { /* Display 1ms as fallback */
			starttime -= 1*NSEC_PER_MSEC;
			endtime += 1*NSEC_PER_MSEC;
		}
		timestamp__scnprintf_usec(starttime, start, sizeof start);
		timestamp__scnprintf_usec(endtime, end, sizeof end);
		n += snprintf(script_opt + n, len - n, " --time %s,%s", start, end);
	}

	script_browse(script_opt, act->evsel);
	free(script_opt);
	return 0;
}

static int
do_res_sample_script(struct hist_browser *browser __maybe_unused,
		     struct popup_action *act)
{
	struct hist_entry *he;

	he = hist_browser__selected_entry(browser);
	res_sample_browse(he->res_samples, he->num_res, act->evsel, act->rstype);
	return 0;
}

static int
add_script_opt_2(struct hist_browser *browser __maybe_unused,
	       struct popup_action *act, char **optstr,
	       struct thread *thread, struct symbol *sym,
	       struct evsel *evsel, const char *tstr)
{

	if (thread) {
		if (asprintf(optstr, "Run scripts for samples of thread [%s]%s",
			     thread__comm_str(thread), tstr) < 0)
			return 0;
	} else if (sym) {
		if (asprintf(optstr, "Run scripts for samples of symbol [%s]%s",
			     sym->name, tstr) < 0)
			return 0;
	} else {
		if (asprintf(optstr, "Run scripts for all samples%s", tstr) < 0)
			return 0;
	}

	act->thread = thread;
	act->ms.sym = sym;
	act->evsel = evsel;
	act->fn = do_run_script;
	return 1;
}

static int
add_script_opt(struct hist_browser *browser,
	       struct popup_action *act, char **optstr,
	       struct thread *thread, struct symbol *sym,
	       struct evsel *evsel)
{
	int n, j;
	struct hist_entry *he;

	n = add_script_opt_2(browser, act, optstr, thread, sym, evsel, "");

	he = hist_browser__selected_entry(browser);
	if (sort_order && strstr(sort_order, "time")) {
		char tstr[128];

		optstr++;
		act++;
		j = sprintf(tstr, " in ");
		j += timestamp__scnprintf_usec(he->time, tstr + j,
					       sizeof tstr - j);
		j += sprintf(tstr + j, "-");
		timestamp__scnprintf_usec(he->time + symbol_conf.time_quantum,
				          tstr + j, sizeof tstr - j);
		n += add_script_opt_2(browser, act, optstr, thread, sym,
					  evsel, tstr);
		act->time = he->time;
	}
	return n;
}

static int
add_res_sample_opt(struct hist_browser *browser __maybe_unused,
		   struct popup_action *act, char **optstr,
		   struct res_sample *res_sample,
		   struct evsel *evsel,
		   enum rstype type)
{
	if (!res_sample)
		return 0;

	if (asprintf(optstr, "Show context for individual samples %s",
		type == A_ASM ? "with assembler" :
		type == A_SOURCE ? "with source" : "") < 0)
		return 0;

	act->fn = do_res_sample_script;
	act->evsel = evsel;
	act->rstype = type;
	return 1;
}

static int
do_switch_data(struct hist_browser *browser __maybe_unused,
	       struct popup_action *act __maybe_unused)
{
	if (switch_data_file()) {
		ui__warning("Won't switch the data files due to\n"
			    "no valid data file get selected!\n");
		return 0;
	}

	return K_SWITCH_INPUT_DATA;
}

static int
add_switch_opt(struct hist_browser *browser,
	       struct popup_action *act, char **optstr)
{
	if (!is_report_browser(browser->hbt))
		return 0;

	if (asprintf(optstr, "Switch to another data file in PWD") < 0)
		return 0;

	act->fn = do_switch_data;
	return 1;
}

static int
do_exit_browser(struct hist_browser *browser __maybe_unused,
		struct popup_action *act __maybe_unused)
{
	return 0;
}

static int
add_exit_opt(struct hist_browser *browser __maybe_unused,
	     struct popup_action *act, char **optstr)
{
	if (asprintf(optstr, "Exit") < 0)
		return 0;

	act->fn = do_exit_browser;
	return 1;
}

static int
do_zoom_socket(struct hist_browser *browser, struct popup_action *act)
{
	if (!hists__has(browser->hists, socket) || act->socket < 0)
		return 0;

	if (browser->hists->socket_filter > -1) {
		pstack__remove(browser->pstack, &browser->hists->socket_filter);
		browser->hists->socket_filter = -1;
		perf_hpp__set_elide(HISTC_SOCKET, false);
	} else {
		browser->hists->socket_filter = act->socket;
		perf_hpp__set_elide(HISTC_SOCKET, true);
		pstack__push(browser->pstack, &browser->hists->socket_filter);
	}

	hists__filter_by_socket(browser->hists);
	hist_browser__reset(browser);
	return 0;
}

static int
add_socket_opt(struct hist_browser *browser, struct popup_action *act,
	       char **optstr, int socket_id)
{
	if (!hists__has(browser->hists, socket) || socket_id < 0)
		return 0;

	if (asprintf(optstr, "Zoom %s Processor Socket %d",
		     (browser->hists->socket_filter > -1) ? "out of" : "into",
		     socket_id) < 0)
		return 0;

	act->socket = socket_id;
	act->fn = do_zoom_socket;
	return 1;
}

static void hist_browser__update_nr_entries(struct hist_browser *hb)
{
	u64 nr_entries = 0;
	struct rb_node *nd = rb_first_cached(&hb->hists->entries);

	if (hb->min_pcnt == 0 && !symbol_conf.report_hierarchy) {
		hb->nr_non_filtered_entries = hb->hists->nr_non_filtered_entries;
		return;
	}

	while ((nd = hists__filter_entries(nd, hb->min_pcnt)) != NULL) {
		nr_entries++;
		nd = rb_hierarchy_next(nd);
	}

	hb->nr_non_filtered_entries = nr_entries;
	hb->nr_hierarchy_entries = nr_entries;
}

static void hist_browser__update_percent_limit(struct hist_browser *hb,
					       double percent)
{
	struct hist_entry *he;
	struct rb_node *nd = rb_first_cached(&hb->hists->entries);
	u64 total = hists__total_period(hb->hists);
	u64 min_callchain_hits = total * (percent / 100);

	hb->min_pcnt = callchain_param.min_percent = percent;

	while ((nd = hists__filter_entries(nd, hb->min_pcnt)) != NULL) {
		he = rb_entry(nd, struct hist_entry, rb_node);

		if (he->has_no_entry) {
			he->has_no_entry = false;
			he->nr_rows = 0;
		}

		if (!he->leaf || !hist_entry__has_callchains(he) || !symbol_conf.use_callchain)
			goto next;

		if (callchain_param.mode == CHAIN_GRAPH_REL) {
			total = he->stat.period;

			if (symbol_conf.cumulate_callchain)
				total = he->stat_acc->period;

			min_callchain_hits = total * (percent / 100);
		}

		callchain_param.sort(&he->sorted_chain, he->callchain,
				     min_callchain_hits, &callchain_param);

next:
		nd = __rb_hierarchy_next(nd, HMD_FORCE_CHILD);

		/* force to re-evaluate folding state of callchains */
		he->init_have_children = false;
		hist_entry__set_folding(he, hb, false);
	}
}

static int perf_evsel__hists_browse(struct evsel *evsel, int nr_events,
				    const char *helpline,
				    bool left_exits,
				    struct hist_browser_timer *hbt,
				    float min_pcnt,
				    struct perf_env *env,
				    bool warn_lost_event,
				    struct annotation_options *annotation_opts)
{
	struct hists *hists = evsel__hists(evsel);
	struct hist_browser *browser = perf_evsel_browser__new(evsel, hbt, env, annotation_opts);
	struct branch_info *bi = NULL;
#define MAX_OPTIONS  16
	char *options[MAX_OPTIONS];
	struct popup_action actions[MAX_OPTIONS];
	int nr_options = 0;
	int key = -1;
	char buf[64];
	int delay_secs = hbt ? hbt->refresh : 0;

#define HIST_BROWSER_HELP_COMMON					\
	"h/?/F1        Show this window\n"				\
	"UP/DOWN/PGUP\n"						\
	"PGDN/SPACE    Navigate\n"					\
	"q/ESC/CTRL+C  Exit browser or go back to previous screen\n\n"	\
	"For multiple event sessions:\n\n"				\
	"TAB/UNTAB     Switch events\n\n"				\
	"For symbolic views (--sort has sym):\n\n"			\
	"ENTER         Zoom into DSO/Threads & Annotate current symbol\n" \
	"ESC           Zoom out\n"					\
	"+             Expand/Collapse one callchain level\n"		\
	"a             Annotate current symbol\n"			\
	"C             Collapse all callchains\n"			\
	"d             Zoom into current DSO\n"				\
	"e             Expand/Collapse main entry callchains\n"	\
	"E             Expand all callchains\n"				\
	"F             Toggle percentage of filtered entries\n"		\
	"H             Display column headers\n"			\
	"k             Zoom into the kernel map\n"			\
	"L             Change percent limit\n"				\
	"m             Display context menu\n"				\
	"S             Zoom into current Processor Socket\n"		\

	/* help messages are sorted by lexical order of the hotkey */
	static const char report_help[] = HIST_BROWSER_HELP_COMMON
	"i             Show header information\n"
	"P             Print histograms to perf.hist.N\n"
	"r             Run available scripts\n"
	"s             Switch to another data file in PWD\n"
	"t             Zoom into current Thread\n"
	"V             Verbose (DSO names in callchains, etc)\n"
	"/             Filter symbol by name\n"
	"0-9           Sort by event n in group";
	static const char top_help[] = HIST_BROWSER_HELP_COMMON
	"P             Print histograms to perf.hist.N\n"
	"t             Zoom into current Thread\n"
	"V             Verbose (DSO names in callchains, etc)\n"
	"z             Toggle zeroing of samples\n"
	"f             Enable/Disable events\n"
	"/             Filter symbol by name";

	if (browser == NULL)
		return -1;

	/* reset abort key so that it can get Ctrl-C as a key */
	SLang_reset_tty();
	SLang_init_tty(0, 0, 0);

	if (min_pcnt)
		browser->min_pcnt = min_pcnt;
	hist_browser__update_nr_entries(browser);

	browser->pstack = pstack__new(3);
	if (browser->pstack == NULL)
		goto out;

	ui_helpline__push(helpline);

	memset(options, 0, sizeof(options));
	memset(actions, 0, sizeof(actions));

	if (symbol_conf.col_width_list_str)
		perf_hpp__set_user_width(symbol_conf.col_width_list_str);

	if (!is_report_browser(hbt))
		browser->b.no_samples_msg = "Collecting samples...";

	while (1) {
		struct thread *thread = NULL;
		struct map *map = NULL;
		int choice;
		int socked_id = -1;

		key = 0; // reset key
do_hotkey:		 // key came straight from options ui__popup_menu()
		choice = nr_options = 0;
		key = hist_browser__run(browser, helpline, warn_lost_event, key);

		if (browser->he_selection != NULL) {
			thread = hist_browser__selected_thread(browser);
			map = browser->selection->map;
			socked_id = browser->he_selection->socket;
		}
		switch (key) {
		case K_TAB:
		case K_UNTAB:
			if (nr_events == 1)
				continue;
			/*
			 * Exit the browser, let hists__browser_tree
			 * go to the next or previous
			 */
			goto out_free_stack;
		case '0' ... '9':
			if (!symbol_conf.event_group ||
			    evsel->core.nr_members < 2) {
				snprintf(buf, sizeof(buf),
					 "Sort by index only available with group events!");
				helpline = buf;
				continue;
			}

			if (key - '0' == symbol_conf.group_sort_idx)
				continue;

			symbol_conf.group_sort_idx = key - '0';

			if (symbol_conf.group_sort_idx >= evsel->core.nr_members) {
				snprintf(buf, sizeof(buf),
					 "Max event group index to sort is %d (index from 0 to %d)",
					 evsel->core.nr_members - 1,
					 evsel->core.nr_members - 1);
				helpline = buf;
				continue;
			}

			key = K_RELOAD;
			goto out_free_stack;
		case 'a':
			if (!hists__has(hists, sym)) {
				ui_browser__warning(&browser->b, delay_secs * 2,
			"Annotation is only available for symbolic views, "
			"include \"sym*\" in --sort to use it.");
				continue;
			}

			if (!browser->selection ||
			    !browser->selection->map ||
			    !browser->selection->map->dso ||
			    browser->selection->map->dso->annotate_warned) {
				continue;
			}

			if (!browser->selection->sym) {
				if (!browser->he_selection)
					continue;

				if (sort__mode == SORT_MODE__BRANCH) {
					bi = browser->he_selection->branch_info;
					if (!bi || !bi->to.ms.map)
						continue;

					actions->ms.sym = symbol__new_unresolved(bi->to.al_addr, bi->to.ms.map);
					actions->ms.map = bi->to.ms.map;
				} else {
					actions->ms.sym = symbol__new_unresolved(browser->he_selection->ip,
										 browser->selection->map);
					actions->ms.map = browser->selection->map;
				}

				if (!actions->ms.sym)
					continue;
			} else {
				if (symbol__annotation(browser->selection->sym)->src == NULL) {
					ui_browser__warning(&browser->b, delay_secs * 2,
						"No samples for the \"%s\" symbol.\n\n"
						"Probably appeared just in a callchain",
						browser->selection->sym->name);
					continue;
				}

				actions->ms.map = browser->selection->map;
				actions->ms.sym = browser->selection->sym;
			}

			do_annotate(browser, actions);
			continue;
		case 'P':
			hist_browser__dump(browser);
			continue;
		case 'd':
			actions->ms.map = map;
			do_zoom_dso(browser, actions);
			continue;
		case 'k':
			if (browser->selection != NULL)
				hists_browser__zoom_map(browser, browser->selection->maps->machine->vmlinux_map);
			continue;
		case 'V':
			verbose = (verbose + 1) % 4;
			browser->show_dso = verbose > 0;
			ui_helpline__fpush("Verbosity level set to %d\n",
					   verbose);
			continue;
		case 't':
			actions->thread = thread;
			do_zoom_thread(browser, actions);
			continue;
		case 'S':
			actions->socket = socked_id;
			do_zoom_socket(browser, actions);
			continue;
		case '/':
			if (ui_browser__input_window("Symbol to show",
					"Please enter the name of symbol you want to see.\n"
					"To remove the filter later, press / + ENTER.",
					buf, "ENTER: OK, ESC: Cancel",
					delay_secs * 2) == K_ENTER) {
				hists->symbol_filter_str = *buf ? buf : NULL;
				hists__filter_by_symbol(hists);
				hist_browser__reset(browser);
			}
			continue;
		case 'r':
			if (is_report_browser(hbt)) {
				actions->thread = NULL;
				actions->ms.sym = NULL;
				do_run_script(browser, actions);
			}
			continue;
		case 's':
			if (is_report_browser(hbt)) {
				key = do_switch_data(browser, actions);
				if (key == K_SWITCH_INPUT_DATA)
					goto out_free_stack;
			}
			continue;
		case 'i':
			/* env->arch is NULL for live-mode (i.e. perf top) */
			if (env->arch)
				tui__header_window(env);
			continue;
		case 'F':
			symbol_conf.filter_relative ^= 1;
			continue;
		case 'z':
			if (!is_report_browser(hbt)) {
				struct perf_top *top = hbt->arg;

				top->zero = !top->zero;
			}
			continue;
		case 'L':
			if (ui_browser__input_window("Percent Limit",
					"Please enter the value you want to hide entries under that percent.",
					buf, "ENTER: OK, ESC: Cancel",
					delay_secs * 2) == K_ENTER) {
				char *end;
				double new_percent = strtod(buf, &end);

				if (new_percent < 0 || new_percent > 100) {
					ui_browser__warning(&browser->b, delay_secs * 2,
						"Invalid percent: %.2f", new_percent);
					continue;
				}

				hist_browser__update_percent_limit(browser, new_percent);
				hist_browser__reset(browser);
			}
			continue;
		case K_F1:
		case 'h':
		case '?':
			ui_browser__help_window(&browser->b,
				is_report_browser(hbt) ? report_help : top_help);
			continue;
		case K_ENTER:
		case K_RIGHT:
		case 'm':
			/* menu */
			break;
		case K_ESC:
		case K_LEFT: {
			const void *top;

			if (pstack__empty(browser->pstack)) {
				/*
				 * Go back to the perf_evsel_menu__run or other user
				 */
				if (left_exits)
					goto out_free_stack;

				if (key == K_ESC &&
				    ui_browser__dialog_yesno(&browser->b,
							     "Do you really want to exit?"))
					goto out_free_stack;

				continue;
			}
			actions->ms.map = map;
			top = pstack__peek(browser->pstack);
			if (top == &browser->hists->dso_filter) {
				/*
				 * No need to set actions->dso here since
				 * it's just to remove the current filter.
				 * Ditto for thread below.
				 */
				do_zoom_dso(browser, actions);
			} else if (top == &browser->hists->thread_filter) {
				do_zoom_thread(browser, actions);
			} else if (top == &browser->hists->socket_filter) {
				do_zoom_socket(browser, actions);
			}
			continue;
		}
		case 'q':
		case CTRL('c'):
			goto out_free_stack;
		case 'f':
			if (!is_report_browser(hbt)) {
				struct perf_top *top = hbt->arg;

				perf_evlist__toggle_enable(top->evlist);
				/*
				 * No need to refresh, resort/decay histogram
				 * entries if we are not collecting samples:
				 */
				if (top->evlist->enabled) {
					helpline = "Press 'f' to disable the events or 'h' to see other hotkeys";
					hbt->refresh = delay_secs;
				} else {
					helpline = "Press 'f' again to re-enable the events";
					hbt->refresh = 0;
				}
				continue;
			}
			/* Fall thru */
		default:
			helpline = "Press '?' for help on key bindings";
			continue;
		}

		if (!hists__has(hists, sym) || browser->selection == NULL)
			goto skip_annotation;

		if (sort__mode == SORT_MODE__BRANCH) {

			if (browser->he_selection)
				bi = browser->he_selection->branch_info;

			if (bi == NULL)
				goto skip_annotation;

			nr_options += add_annotate_opt(browser,
						       &actions[nr_options],
						       &options[nr_options],
						       &bi->from.ms,
						       bi->from.al_addr);
			if (bi->to.ms.sym != bi->from.ms.sym)
				nr_options += add_annotate_opt(browser,
							&actions[nr_options],
							&options[nr_options],
							&bi->to.ms,
							bi->to.al_addr);
		} else {
			nr_options += add_annotate_opt(browser,
						       &actions[nr_options],
						       &options[nr_options],
						       browser->selection,
						       browser->he_selection->ip);
		}
skip_annotation:
		nr_options += add_thread_opt(browser, &actions[nr_options],
					     &options[nr_options], thread);
		nr_options += add_dso_opt(browser, &actions[nr_options],
					  &options[nr_options], map);
		nr_options += add_callchain_toggle_opt(browser, &actions[nr_options], &options[nr_options]);
		nr_options += add_map_opt(browser, &actions[nr_options],
					  &options[nr_options],
					  browser->selection ?
						browser->selection->map : NULL);
		nr_options += add_socket_opt(browser, &actions[nr_options],
					     &options[nr_options],
					     socked_id);
		/* perf script support */
		if (!is_report_browser(hbt))
			goto skip_scripting;

		if (browser->he_selection) {
			if (hists__has(hists, thread) && thread) {
				nr_options += add_script_opt(browser,
							     &actions[nr_options],
							     &options[nr_options],
							     thread, NULL, evsel);
			}
			/*
			 * Note that browser->selection != NULL
			 * when browser->he_selection is not NULL,
			 * so we don't need to check browser->selection
			 * before fetching browser->selection->sym like what
			 * we do before fetching browser->selection->map.
			 *
			 * See hist_browser__show_entry.
			 */
			if (hists__has(hists, sym) && browser->selection->sym) {
				nr_options += add_script_opt(browser,
							     &actions[nr_options],
							     &options[nr_options],
							     NULL, browser->selection->sym,
							     evsel);
			}
		}
		nr_options += add_script_opt(browser, &actions[nr_options],
					     &options[nr_options], NULL, NULL, evsel);
		nr_options += add_res_sample_opt(browser, &actions[nr_options],
						 &options[nr_options],
				 hist_browser__selected_entry(browser)->res_samples,
				 evsel, A_NORMAL);
		nr_options += add_res_sample_opt(browser, &actions[nr_options],
						 &options[nr_options],
				 hist_browser__selected_entry(browser)->res_samples,
				 evsel, A_ASM);
		nr_options += add_res_sample_opt(browser, &actions[nr_options],
						 &options[nr_options],
				 hist_browser__selected_entry(browser)->res_samples,
				 evsel, A_SOURCE);
		nr_options += add_switch_opt(browser, &actions[nr_options],
					     &options[nr_options]);
skip_scripting:
		nr_options += add_exit_opt(browser, &actions[nr_options],
					   &options[nr_options]);

		do {
			struct popup_action *act;

			choice = ui__popup_menu(nr_options, options, &key);
			if (choice == -1)
				break;

			if (choice == nr_options)
				goto do_hotkey;

			act = &actions[choice];
			key = act->fn(browser, act);
		} while (key == 1);

		if (key == K_SWITCH_INPUT_DATA)
			break;
	}
out_free_stack:
	pstack__delete(browser->pstack);
out:
	hist_browser__delete(browser);
	free_popup_options(options, MAX_OPTIONS);
	return key;
}

struct evsel_menu {
	struct ui_browser b;
	struct evsel *selection;
	struct annotation_options *annotation_opts;
	bool lost_events, lost_events_warned;
	float min_pcnt;
	struct perf_env *env;
};

static void perf_evsel_menu__write(struct ui_browser *browser,
				   void *entry, int row)
{
	struct evsel_menu *menu = container_of(browser,
						    struct evsel_menu, b);
	struct evsel *evsel = list_entry(entry, struct evsel, core.node);
	struct hists *hists = evsel__hists(evsel);
	bool current_entry = ui_browser__is_current_entry(browser, row);
	unsigned long nr_events = hists->stats.nr_events[PERF_RECORD_SAMPLE];
	const char *ev_name = perf_evsel__name(evsel);
	char bf[256], unit;
	const char *warn = " ";
	size_t printed;

	ui_browser__set_color(browser, current_entry ? HE_COLORSET_SELECTED :
						       HE_COLORSET_NORMAL);

	if (perf_evsel__is_group_event(evsel)) {
		struct evsel *pos;

		ev_name = perf_evsel__group_name(evsel);

		for_each_group_member(pos, evsel) {
			struct hists *pos_hists = evsel__hists(pos);
			nr_events += pos_hists->stats.nr_events[PERF_RECORD_SAMPLE];
		}
	}

	nr_events = convert_unit(nr_events, &unit);
	printed = scnprintf(bf, sizeof(bf), "%lu%c%s%s", nr_events,
			   unit, unit == ' ' ? "" : " ", ev_name);
	ui_browser__printf(browser, "%s", bf);

	nr_events = hists->stats.nr_events[PERF_RECORD_LOST];
	if (nr_events != 0) {
		menu->lost_events = true;
		if (!current_entry)
			ui_browser__set_color(browser, HE_COLORSET_TOP);
		nr_events = convert_unit(nr_events, &unit);
		printed += scnprintf(bf, sizeof(bf), ": %ld%c%schunks LOST!",
				     nr_events, unit, unit == ' ' ? "" : " ");
		warn = bf;
	}

	ui_browser__write_nstring(browser, warn, browser->width - printed);

	if (current_entry)
		menu->selection = evsel;
}

static int perf_evsel_menu__run(struct evsel_menu *menu,
				int nr_events, const char *help,
				struct hist_browser_timer *hbt,
				bool warn_lost_event)
{
	struct evlist *evlist = menu->b.priv;
	struct evsel *pos;
	const char *title = "Available samples";
	int delay_secs = hbt ? hbt->refresh : 0;
	int key;

	if (ui_browser__show(&menu->b, title,
			     "ESC: exit, ENTER|->: Browse histograms") < 0)
		return -1;

	while (1) {
		key = ui_browser__run(&menu->b, delay_secs);

		switch (key) {
		case K_TIMER:
			if (hbt)
				hbt->timer(hbt->arg);

			if (!menu->lost_events_warned &&
			    menu->lost_events &&
			    warn_lost_event) {
				ui_browser__warn_lost_events(&menu->b);
				menu->lost_events_warned = true;
			}
			continue;
		case K_RIGHT:
		case K_ENTER:
			if (!menu->selection)
				continue;
			pos = menu->selection;
browse_hists:
			perf_evlist__set_selected(evlist, pos);
			/*
			 * Give the calling tool a chance to populate the non
			 * default evsel resorted hists tree.
			 */
			if (hbt)
				hbt->timer(hbt->arg);
			key = perf_evsel__hists_browse(pos, nr_events, help,
						       true, hbt,
						       menu->min_pcnt,
						       menu->env,
						       warn_lost_event,
						       menu->annotation_opts);
			ui_browser__show_title(&menu->b, title);
			switch (key) {
			case K_TAB:
				if (pos->core.node.next == &evlist->core.entries)
					pos = evlist__first(evlist);
				else
					pos = perf_evsel__next(pos);
				goto browse_hists;
			case K_UNTAB:
				if (pos->core.node.prev == &evlist->core.entries)
					pos = evlist__last(evlist);
				else
					pos = perf_evsel__prev(pos);
				goto browse_hists;
			case K_SWITCH_INPUT_DATA:
			case K_RELOAD:
			case 'q':
			case CTRL('c'):
				goto out;
			case K_ESC:
			default:
				continue;
			}
		case K_LEFT:
			continue;
		case K_ESC:
			if (!ui_browser__dialog_yesno(&menu->b,
					       "Do you really want to exit?"))
				continue;
			/* Fall thru */
		case 'q':
		case CTRL('c'):
			goto out;
		default:
			continue;
		}
	}

out:
	ui_browser__hide(&menu->b);
	return key;
}

static bool filter_group_entries(struct ui_browser *browser __maybe_unused,
				 void *entry)
{
	struct evsel *evsel = list_entry(entry, struct evsel, core.node);

	if (symbol_conf.event_group && !perf_evsel__is_group_leader(evsel))
		return true;

	return false;
}

static int __perf_evlist__tui_browse_hists(struct evlist *evlist,
					   int nr_entries, const char *help,
					   struct hist_browser_timer *hbt,
					   float min_pcnt,
					   struct perf_env *env,
					   bool warn_lost_event,
					   struct annotation_options *annotation_opts)
{
	struct evsel *pos;
	struct evsel_menu menu = {
		.b = {
			.entries    = &evlist->core.entries,
			.refresh    = ui_browser__list_head_refresh,
			.seek	    = ui_browser__list_head_seek,
			.write	    = perf_evsel_menu__write,
			.filter	    = filter_group_entries,
			.nr_entries = nr_entries,
			.priv	    = evlist,
		},
		.min_pcnt = min_pcnt,
		.env = env,
		.annotation_opts = annotation_opts,
	};

	ui_helpline__push("Press ESC to exit");

	evlist__for_each_entry(evlist, pos) {
		const char *ev_name = perf_evsel__name(pos);
		size_t line_len = strlen(ev_name) + 7;

		if (menu.b.width < line_len)
			menu.b.width = line_len;
	}

	return perf_evsel_menu__run(&menu, nr_entries, help,
				    hbt, warn_lost_event);
}

int perf_evlist__tui_browse_hists(struct evlist *evlist, const char *help,
				  struct hist_browser_timer *hbt,
				  float min_pcnt,
				  struct perf_env *env,
				  bool warn_lost_event,
				  struct annotation_options *annotation_opts)
{
	int nr_entries = evlist->core.nr_entries;

single_entry:
	if (nr_entries == 1) {
		struct evsel *first = evlist__first(evlist);

		return perf_evsel__hists_browse(first, nr_entries, help,
						false, hbt, min_pcnt,
						env, warn_lost_event,
						annotation_opts);
	}

	if (symbol_conf.event_group) {
		struct evsel *pos;

		nr_entries = 0;
		evlist__for_each_entry(evlist, pos) {
			if (perf_evsel__is_group_leader(pos))
				nr_entries++;
		}

		if (nr_entries == 1)
			goto single_entry;
	}

	return __perf_evlist__tui_browse_hists(evlist, nr_entries, help,
					       hbt, min_pcnt, env,
					       warn_lost_event,
					       annotation_opts);
}

static int block_hists_browser__title(struct hist_browser *browser, char *bf,
				      size_t size)
{
	struct hists *hists = evsel__hists(browser->block_evsel);
	const char *evname = perf_evsel__name(browser->block_evsel);
	unsigned long nr_samples = hists->stats.nr_events[PERF_RECORD_SAMPLE];
	int ret;

	ret = scnprintf(bf, size, "# Samples: %lu", nr_samples);
	if (evname)
		scnprintf(bf + ret, size -  ret, " of event '%s'", evname);

	return 0;
}

int block_hists_tui_browse(struct block_hist *bh, struct evsel *evsel,
			   float min_percent, struct perf_env *env,
			   struct annotation_options *annotation_opts)
{
	struct hists *hists = &bh->block_hists;
	struct hist_browser *browser;
	int key = -1;
	struct popup_action action;
	static const char help[] =
	" q             Quit \n";

	browser = hist_browser__new(hists);
	if (!browser)
		return -1;

	browser->block_evsel = evsel;
	browser->title = block_hists_browser__title;
	browser->min_pcnt = min_percent;
	browser->env = env;
	browser->annotation_opts = annotation_opts;

	/* reset abort key so that it can get Ctrl-C as a key */
	SLang_reset_tty();
	SLang_init_tty(0, 0, 0);

	memset(&action, 0, sizeof(action));

	while (1) {
		key = hist_browser__run(browser, "? - help", true, 0);

		switch (key) {
		case 'q':
			goto out;
		case '?':
			ui_browser__help_window(&browser->b, help);
			break;
		case 'a':
		case K_ENTER:
			if (!browser->selection ||
			    !browser->selection->sym) {
				continue;
			}

			action.ms.map = browser->selection->map;
			action.ms.sym = browser->selection->sym;
			do_annotate(browser, &action);
			continue;
		default:
			break;
		}
	}

out:
	hist_browser__delete(browser);
	return 0;
}
