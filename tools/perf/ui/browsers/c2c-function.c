// SPDX-License-Identifier: GPL-2.0
/*
 * C2C function browser - TUI front end for function-level sharing analysis
 */

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/ttydefaults.h>
#include <linux/rbtree.h>
#include <linux/zalloc.h>

#include "../browser.h"
#include "../keysyms.h"
#include "../libslang.h"
#include "../ui.h"
#include "../../util/c2c.h"
#include "../../util/debug.h"
#include "../../util/hist.h"
#include "../../util/symbol.h"
#include "hists.h"

struct c2c_function_browser {
	struct hist_browser	hb;
	unsigned int		(*orig_refresh)(struct ui_browser *browser);
	int			(*browse_cacheline)(struct hist_entry *he);
};

/*
 * Count visible entries in @root, descending only through visible, unfolded
 * parents. Match hists__filter_entries(), which drives generic browser
 * navigation, so the count cannot include rows the browser skips.
 */
static u64
c2c_function__nr_visible_rows(struct rb_root_cached *root, float min_pcnt)
{
	struct rb_node *nd;
	u64 rows = 0;

	for (nd = rb_first_cached(root); nd; nd = rb_next(nd)) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);

		/*
		 * The generic refresh folds filtered parents and therefore hides
		 * their subtree. A percentage-rejected parent is merely skipped;
		 * if it is unfolded, qualifying descendants are still rendered.
		 */
		if (he->filtered)
			continue;

		if (hist_entry__get_percent_limit(he) >= min_pcnt)
			rows++;
		if (he->has_children && he->unfolded)
			rows += c2c_function__nr_visible_rows(&he->hroot_out,
							     min_pcnt);
	}
	return rows;
}

static void
c2c_function_browser__update_nr_entries(struct c2c_function_browser *browser)
{
	u64 nr_entries;

	nr_entries = c2c_function__nr_visible_rows(&browser->hb.hists->entries,
						   browser->hb.min_pcnt);
	browser->hb.nr_non_filtered_entries = nr_entries;
	browser->hb.b.nr_entries = nr_entries;
}

static unsigned int c2c_function_browser__refresh(struct ui_browser *ui_browser)
{
	struct hist_browser *hist_browser = container_of(ui_browser, struct hist_browser, b);
	struct c2c_function_browser *browser;

	browser = container_of(hist_browser, struct c2c_function_browser, hb);
	c2c_function_browser__update_nr_entries(browser);
	return browser->orig_refresh(ui_browser);
}

static int c2c_function_browser__title(struct hist_browser *browser,
				       char *bf, size_t size)
{
	scnprintf(bf, size,
		  "Shared Data Functions Table     (%" PRIu64 " entries, sorted on Cycles %%)",
		  browser->hists->nr_non_filtered_entries);
	return 0;
}

static struct c2c_function_browser *
c2c_function_browser__new(struct hists *hists,
			  int (*browse_cacheline)(struct hist_entry *he))
{
	struct c2c_function_browser *browser;

	if (!hists)
		return NULL;

	browser = zalloc(sizeof(*browser));
	if (!browser)
		return NULL;

	hist_browser__init(&browser->hb, hists);
	browser->orig_refresh = browser->hb.b.refresh;
	browser->hb.b.refresh = c2c_function_browser__refresh;
	browser->browse_cacheline = browse_cacheline;

	browser->hb.title = c2c_function_browser__title;
	browser->hb.c2c_filter = true;
	browser->hb.show_headers = true;
	/* Keep title line count consistent with forcing headers on. */
	browser->hb.b.extra_title_lines = hists->hpp_list->nr_header_lines;
	browser->hb.min_pcnt = 0.0;

	return browser;
}

static void c2c_function_browser__delete(struct c2c_function_browser *browser)
{
	free(browser);
}

static int
c2c_browser__browse_cacheline(struct c2c_function_browser *browser,
			      struct hist_entry *he_selection)
{
	struct hist_entry *he = c2c_function__find_cacheline(he_selection);

	return he ? browser->browse_cacheline(he) : -1;
}

int perf_c2c__browse_function_view(struct c2c_function_view_args *args)
{
	struct c2c_function_browser *browser;
	struct hists *hists;
	bool saved_use_callchain = symbol_conf.use_callchain;
	int key, ret;
	static const char help[] =
	" d             Display details for the selected level-3 cacheline\n"
	" e/+           Expand/collapse the selected entry\n"
	" TAB/ESC/q/^C  Return to the cacheline view\n";

	if (!args || !args->cl_hists || !args->browse_cacheline)
		return -EINVAL;

	/*
	 * Function view does not display callchains; cacheline detail temporarily
	 * restores them.
	 */
	symbol_conf.use_callchain = false;

	ret = c2c_function__build(args->cl_hists, args->cl_sort,
				  args->symbol_full, &hists);
	if (ret) {
		if (ret == -EOPNOTSUPP)
			ui__warning("The function view requires iaddr in --coalesce.\n");
		else
			ui__error("Failed to build function view hierarchy (ret=%d)\n", ret);
		goto out;
	}

	browser = c2c_function_browser__new(hists, args->browse_cacheline);
	if (!browser) {
		ret = -ENOMEM;
		goto out_reset;
	}

	/* Reset abort key so we can receive Ctrl-C as a key. */
	SLang_reset_tty();
	SLang_init_tty(0, 0, 0);
	SLtty_set_suspend_state(true);

	while (1) {
		c2c_function_browser__update_nr_entries(browser);
		key = hist_browser__run(&browser->hb, "? - help", true, 0);

		switch (key) {
		case 'q':
		case K_TAB:
		case K_ESC:
		case CTRL('c'):
			goto browser_done;
		case 'd':
			/* Cacheline detail honors the user's callchain setting. */
			symbol_conf.use_callchain = saved_use_callchain;
			c2c_browser__browse_cacheline(browser, browser->hb.he_selection);
			/*
			 * Preserve any toggle made in the detail view, then
			 * re-disable callchain for the function view.
			 */
			saved_use_callchain = symbol_conf.use_callchain;
			symbol_conf.use_callchain = false;
			break;
		case '?':
			ui_browser__help_window(&browser->hb.b, help);
			break;
		default:
			break;
		}
	}

browser_done:
	c2c_function_browser__delete(browser);
out_reset:
	c2c_function__reset();
out:
	symbol_conf.use_callchain = saved_use_callchain;
	return ret;
}
