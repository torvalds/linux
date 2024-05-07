// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <string.h>
#include <linux/zalloc.h>
#include <sys/ttydefaults.h>

#include "ui/browser.h"
#include "ui/helpline.h"
#include "ui/keysyms.h"
#include "ui/ui.h"
#include "util/annotate.h"
#include "util/annotate-data.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/sort.h"

struct annotated_data_browser {
	struct ui_browser b;
	struct list_head entries;
	int nr_events;
};

struct browser_entry {
	struct list_head node;
	struct annotated_member *data;
	struct type_hist_entry *hists;
	int indent;
};

static struct annotated_data_browser *get_browser(struct ui_browser *uib)
{
	return container_of(uib, struct annotated_data_browser, b);
}

static void update_hist_entry(struct type_hist_entry *dst,
			      struct type_hist_entry *src)
{
	dst->nr_samples += src->nr_samples;
	dst->period += src->period;
}

static int get_member_overhead(struct annotated_data_type *adt,
			       struct browser_entry *entry,
			       struct evsel *leader)
{
	struct annotated_member *member = entry->data;
	int i, k;

	for (i = 0; i < member->size; i++) {
		struct type_hist *h;
		struct evsel *evsel;
		int offset = member->offset + i;

		for_each_group_evsel(evsel, leader) {
			h = adt->histograms[evsel->core.idx];
			k = evsel__group_idx(evsel);
			update_hist_entry(&entry->hists[k], &h->addr[offset]);
		}
	}
	return 0;
}

static int add_child_entries(struct annotated_data_browser *browser,
			     struct annotated_data_type *adt,
			     struct annotated_member *member,
			     struct evsel *evsel, int indent)
{
	struct annotated_member *pos;
	struct browser_entry *entry;
	int nr_entries = 0;

	entry = zalloc(sizeof(*entry));
	if (entry == NULL)
		return -1;

	entry->hists = calloc(browser->nr_events, sizeof(*entry->hists));
	if (entry->hists == NULL) {
		free(entry);
		return -1;
	}

	entry->data = member;
	entry->indent = indent;
	if (get_member_overhead(adt, entry, evsel) < 0) {
		free(entry);
		return -1;
	}

	list_add_tail(&entry->node, &browser->entries);
	nr_entries++;

	list_for_each_entry(pos, &member->children, node) {
		int nr = add_child_entries(browser, adt, pos, evsel, indent + 1);

		if (nr < 0)
			return nr;

		nr_entries += nr;
	}

	/* add an entry for the closing bracket ("}") */
	if (!list_empty(&member->children)) {
		entry = zalloc(sizeof(*entry));
		if (entry == NULL)
			return -1;

		entry->indent = indent;
		list_add_tail(&entry->node, &browser->entries);
		nr_entries++;
	}

	return nr_entries;
}

static int annotated_data_browser__collect_entries(struct annotated_data_browser *browser)
{
	struct hist_entry *he = browser->b.priv;
	struct annotated_data_type *adt = he->mem_type;
	struct evsel *evsel = hists_to_evsel(he->hists);

	INIT_LIST_HEAD(&browser->entries);
	browser->b.entries = &browser->entries;
	browser->b.nr_entries = add_child_entries(browser, adt, &adt->self,
						  evsel, /*indent=*/0);
	return 0;
}

static void annotated_data_browser__delete_entries(struct annotated_data_browser *browser)
{
	struct browser_entry *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, &browser->entries, node) {
		list_del_init(&pos->node);
		zfree(&pos->hists);
		free(pos);
	}
}

static unsigned int browser__refresh(struct ui_browser *uib)
{
	return ui_browser__list_head_refresh(uib);
}

static int browser__show(struct ui_browser *uib)
{
	struct hist_entry *he = uib->priv;
	struct annotated_data_type *adt = he->mem_type;
	struct annotated_data_browser *browser = get_browser(uib);
	const char *help = "Press 'h' for help on key bindings";
	char title[256];

	snprintf(title, sizeof(title), "Annotate type: '%s' (%d samples)",
		 adt->self.type_name, he->stat.nr_events);

	if (ui_browser__show(uib, title, help) < 0)
		return -1;

	/* second line header */
	ui_browser__gotorc_title(uib, 0, 0);
	ui_browser__set_color(uib, HE_COLORSET_ROOT);

	if (symbol_conf.show_total_period)
		strcpy(title, "Period");
	else if (symbol_conf.show_nr_samples)
		strcpy(title, "Samples");
	else
		strcpy(title, "Percent");

	ui_browser__printf(uib, "%*s %10s %10s %10s  %s",
			   11 * (browser->nr_events - 1), "",
			   title, "Offset", "Size", "Field");
	ui_browser__write_nstring(uib, "", uib->width);
	return 0;
}

static void browser__write_overhead(struct ui_browser *uib,
				    struct type_hist *total,
				    struct type_hist_entry *hist, int row)
{
	u64 period = hist->period;
	double percent = total->period ? (100.0 * period / total->period) : 0;
	bool current = ui_browser__is_current_entry(uib, row);
	int nr_samples = 0;

	ui_browser__set_percent_color(uib, percent, current);

	if (symbol_conf.show_total_period)
		ui_browser__printf(uib, " %10" PRIu64, period);
	else if (symbol_conf.show_nr_samples)
		ui_browser__printf(uib, " %10d", nr_samples);
	else
		ui_browser__printf(uib, " %10.2f", percent);

	ui_browser__set_percent_color(uib, 0, current);
}

static void browser__write(struct ui_browser *uib, void *entry, int row)
{
	struct annotated_data_browser *browser = get_browser(uib);
	struct browser_entry *be = entry;
	struct annotated_member *member = be->data;
	struct hist_entry *he = uib->priv;
	struct annotated_data_type *adt = he->mem_type;
	struct evsel *leader = hists_to_evsel(he->hists);
	struct evsel *evsel;

	if (member == NULL) {
		bool current = ui_browser__is_current_entry(uib, row);

		/* print the closing bracket */
		ui_browser__set_percent_color(uib, 0, current);
		ui_browser__write_nstring(uib, "", 11 * browser->nr_events);
		ui_browser__printf(uib, " %10s %10s  %*s};",
				   "", "", be->indent * 4, "");
		ui_browser__write_nstring(uib, "", uib->width);
		return;
	}

	/* print the number */
	for_each_group_evsel(evsel, leader) {
		struct type_hist *h = adt->histograms[evsel->core.idx];
		int idx = evsel__group_idx(evsel);

		browser__write_overhead(uib, h, &be->hists[idx], row);
	}

	/* print type info */
	if (be->indent == 0 && !member->var_name) {
		ui_browser__printf(uib, " %10d %10d  %s%s",
				   member->offset, member->size,
				   member->type_name,
				   list_empty(&member->children) ? ";" : " {");
	} else {
		ui_browser__printf(uib, " %10d %10d  %*s%s\t%s%s",
				   member->offset, member->size,
				   be->indent * 4, "", member->type_name,
				   member->var_name ?: "",
				   list_empty(&member->children) ? ";" : " {");
	}
	/* fill the rest */
	ui_browser__write_nstring(uib, "", uib->width);
}

static int annotated_data_browser__run(struct annotated_data_browser *browser,
				       struct evsel *evsel __maybe_unused,
				       struct hist_browser_timer *hbt)
{
	int delay_secs = hbt ? hbt->refresh : 0;
	int key;

	if (browser__show(&browser->b) < 0)
		return -1;

	while (1) {
		key = ui_browser__run(&browser->b, delay_secs);

		switch (key) {
		case K_TIMER:
			if (hbt)
				hbt->timer(hbt->arg);
			continue;
		case K_F1:
		case 'h':
			ui_browser__help_window(&browser->b,
		"UP/DOWN/PGUP\n"
		"PGDN/SPACE    Navigate\n"
		"</>           Move to prev/next symbol\n"
		"q/ESC/CTRL+C  Exit\n\n");
			continue;
		case K_LEFT:
		case '<':
		case '>':
		case K_ESC:
		case 'q':
		case CTRL('c'):
			goto out;
		default:
			continue;
		}
	}
out:
	ui_browser__hide(&browser->b);
	return key;
}

int hist_entry__annotate_data_tui(struct hist_entry *he, struct evsel *evsel,
				  struct hist_browser_timer *hbt)
{
	struct annotated_data_browser browser = {
		.b = {
			.refresh = browser__refresh,
			.seek	 = ui_browser__list_head_seek,
			.write	 = browser__write,
			.priv	 = he,
			.extra_title_lines = 1,
		},
		.nr_events = 1,
	};
	int ret;

	ui_helpline__push("Press ESC to exit");

	if (evsel__is_group_event(evsel))
		browser.nr_events = evsel->core.nr_members;

	ret = annotated_data_browser__collect_entries(&browser);
	if (ret == 0)
		ret = annotated_data_browser__run(&browser, evsel, hbt);

	annotated_data_browser__delete_entries(&browser);

	return ret;
}
