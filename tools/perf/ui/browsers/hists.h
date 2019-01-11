/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_UI_BROWSER_HISTS_H_
#define _PERF_UI_BROWSER_HISTS_H_ 1

#include "ui/browser.h"

struct annotation_options;

struct hist_browser {
	struct ui_browser   b;
	struct hists	    *hists;
	struct hist_entry   *he_selection;
	struct map_symbol   *selection;
	struct hist_browser_timer *hbt;
	struct pstack	    *pstack;
	struct perf_env	    *env;
	struct annotation_options *annotation_opts;
	int		     print_seq;
	bool		     show_dso;
	bool		     show_headers;
	float		     min_pcnt;
	u64		     nr_non_filtered_entries;
	u64		     nr_hierarchy_entries;
	u64		     nr_callchain_rows;
	bool		     c2c_filter;

	/* Get title string. */
	int                  (*title)(struct hist_browser *browser,
			     char *bf, size_t size);
};

struct hist_browser *hist_browser__new(struct hists *hists);
void hist_browser__delete(struct hist_browser *browser);
int hist_browser__run(struct hist_browser *browser, const char *help,
		      bool warn_lost_event);
void hist_browser__init(struct hist_browser *browser,
			struct hists *hists);
#endif /* _PERF_UI_BROWSER_HISTS_H_ */
