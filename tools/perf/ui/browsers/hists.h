#ifndef _PERF_UI_BROWSER_HISTS_H_
#define _PERF_UI_BROWSER_HISTS_H_ 1

#include "ui/browser.h"

struct hist_browser {
	struct ui_browser   b;
	struct hists	    *hists;
	struct hist_entry   *he_selection;
	struct map_symbol   *selection;
	struct hist_browser_timer *hbt;
	struct pstack	    *pstack;
	struct perf_env	    *env;
	int		     print_seq;
	bool		     show_dso;
	bool		     show_headers;
	float		     min_pcnt;
	u64		     nr_non_filtered_entries;
	u64		     nr_hierarchy_entries;
	u64		     nr_callchain_rows;
};

#endif /* _PERF_UI_BROWSER_HISTS_H_ */
