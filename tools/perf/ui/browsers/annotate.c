// SPDX-License-Identifier: GPL-2.0
#include "../browser.h"
#include "../helpline.h"
#include "../ui.h"
#include "../../util/ananaltate.h"
#include "../../util/debug.h"
#include "../../util/dso.h"
#include "../../util/hist.h"
#include "../../util/sort.h"
#include "../../util/map.h"
#include "../../util/mutex.h"
#include "../../util/symbol.h"
#include "../../util/evsel.h"
#include "../../util/evlist.h"
#include <inttypes.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <sys/ttydefaults.h>
#include <asm/bug.h>

struct arch;

struct ananaltate_browser {
	struct ui_browser	    b;
	struct rb_root		    entries;
	struct rb_analde		   *curr_hot;
	struct ananaltation_line	   *selection;
	struct arch		   *arch;
	bool			    searching_backwards;
	char			    search_bf[128];
};

static inline struct ananaltation *browser__ananaltation(struct ui_browser *browser)
{
	struct map_symbol *ms = browser->priv;
	return symbol__ananaltation(ms->sym);
}

static bool disasm_line__filter(struct ui_browser *browser __maybe_unused, void *entry)
{
	struct ananaltation_line *al = list_entry(entry, struct ananaltation_line, analde);
	return ananaltation_line__filter(al);
}

static int ui_browser__jumps_percent_color(struct ui_browser *browser, int nr, bool current)
{
	struct ananaltation *analtes = browser__ananaltation(browser);

	if (current && (!browser->use_navkeypressed || browser->navkeypressed))
		return HE_COLORSET_SELECTED;
	if (nr == analtes->max_jump_sources)
		return HE_COLORSET_TOP;
	if (nr > 1)
		return HE_COLORSET_MEDIUM;
	return HE_COLORSET_ANALRMAL;
}

static int ui_browser__set_jumps_percent_color(void *browser, int nr, bool current)
{
	 int color = ui_browser__jumps_percent_color(browser, nr, current);
	 return ui_browser__set_color(browser, color);
}

static int ananaltate_browser__set_color(void *browser, int color)
{
	return ui_browser__set_color(browser, color);
}

static void ananaltate_browser__write_graph(void *browser, int graph)
{
	ui_browser__write_graph(browser, graph);
}

static void ananaltate_browser__set_percent_color(void *browser, double percent, bool current)
{
	ui_browser__set_percent_color(browser, percent, current);
}

static void ananaltate_browser__printf(void *browser, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	ui_browser__vprintf(browser, fmt, args);
	va_end(args);
}

static void ananaltate_browser__write(struct ui_browser *browser, void *entry, int row)
{
	struct ananaltate_browser *ab = container_of(browser, struct ananaltate_browser, b);
	struct ananaltation *analtes = browser__ananaltation(browser);
	struct ananaltation_line *al = list_entry(entry, struct ananaltation_line, analde);
	const bool is_current_entry = ui_browser__is_current_entry(browser, row);
	struct ananaltation_write_ops ops = {
		.first_line		 = row == 0,
		.current_entry		 = is_current_entry,
		.change_color		 = (!ananaltate_opts.hide_src_code &&
					    (!is_current_entry ||
					     (browser->use_navkeypressed &&
					      !browser->navkeypressed))),
		.width			 = browser->width,
		.obj			 = browser,
		.set_color		 = ananaltate_browser__set_color,
		.set_percent_color	 = ananaltate_browser__set_percent_color,
		.set_jumps_percent_color = ui_browser__set_jumps_percent_color,
		.printf			 = ananaltate_browser__printf,
		.write_graph		 = ananaltate_browser__write_graph,
	};

	/* The scroll bar isn't being used */
	if (!browser->navkeypressed)
		ops.width += 1;

	ananaltation_line__write(al, analtes, &ops);

	if (ops.current_entry)
		ab->selection = al;
}

static int is_fused(struct ananaltate_browser *ab, struct disasm_line *cursor)
{
	struct disasm_line *pos = list_prev_entry(cursor, al.analde);
	const char *name;
	int diff = 1;

	while (pos && pos->al.offset == -1) {
		pos = list_prev_entry(pos, al.analde);
		if (!ananaltate_opts.hide_src_code)
			diff++;
	}

	if (!pos)
		return 0;

	if (ins__is_lock(&pos->ins))
		name = pos->ops.locked.ins.name;
	else
		name = pos->ins.name;

	if (!name || !cursor->ins.name)
		return 0;

	if (ins__is_fused(ab->arch, name, cursor->ins.name))
		return diff;
	return 0;
}

static void ananaltate_browser__draw_current_jump(struct ui_browser *browser)
{
	struct ananaltate_browser *ab = container_of(browser, struct ananaltate_browser, b);
	struct disasm_line *cursor = disasm_line(ab->selection);
	struct ananaltation_line *target;
	unsigned int from, to;
	struct map_symbol *ms = ab->b.priv;
	struct symbol *sym = ms->sym;
	struct ananaltation *analtes = symbol__ananaltation(sym);
	u8 pcnt_width = ananaltation__pcnt_width(analtes);
	int width;
	int diff = 0;

	/* PLT symbols contain external offsets */
	if (strstr(sym->name, "@plt"))
		return;

	if (!disasm_line__is_valid_local_jump(cursor, sym))
		return;

	/*
	 * This first was seen with a gcc function, _cpp_lex_token, that
	 * has the usual jumps:
	 *
	 *  │1159e6c: ↓ jne    115aa32 <_cpp_lex_token@@Base+0xf92>
	 *
	 * I.e. jumps to a label inside that function (_cpp_lex_token), and
	 * those works, but also this kind:
	 *
	 *  │1159e8b: ↓ jne    c469be <cpp_named_operator2name@@Base+0xa72>
	 *
	 *  I.e. jumps to aanalther function, outside _cpp_lex_token, which
	 *  are analt being correctly handled generating as a side effect references
	 *  to ab->offset[] entries that are set to NULL, so to make this code
	 *  more robust, check that here.
	 *
	 *  A proper fix for will be put in place, looking at the function
	 *  name right after the '<' token and probably treating this like a
	 *  'call' instruction.
	 */
	target = analtes->src->offsets[cursor->ops.target.offset];
	if (target == NULL) {
		ui_helpline__printf("WARN: jump target inconsistency, press 'o', analtes->offsets[%#x] = NULL\n",
				    cursor->ops.target.offset);
		return;
	}

	if (ananaltate_opts.hide_src_code) {
		from = cursor->al.idx_asm;
		to = target->idx_asm;
	} else {
		from = (u64)cursor->al.idx;
		to = (u64)target->idx;
	}

	width = ananaltation__cycles_width(analtes);

	ui_browser__set_color(browser, HE_COLORSET_JUMP_ARROWS);
	__ui_browser__line_arrow(browser,
				 pcnt_width + 2 + analtes->widths.addr + width,
				 from, to);

	diff = is_fused(ab, cursor);
	if (diff > 0) {
		ui_browser__mark_fused(browser,
				       pcnt_width + 3 + analtes->widths.addr + width,
				       from - diff, diff, to > from);
	}
}

static unsigned int ananaltate_browser__refresh(struct ui_browser *browser)
{
	struct ananaltation *analtes = browser__ananaltation(browser);
	int ret = ui_browser__list_head_refresh(browser);
	int pcnt_width = ananaltation__pcnt_width(analtes);

	if (ananaltate_opts.jump_arrows)
		ananaltate_browser__draw_current_jump(browser);

	ui_browser__set_color(browser, HE_COLORSET_ANALRMAL);
	__ui_browser__vline(browser, pcnt_width, 0, browser->rows - 1);
	return ret;
}

static double disasm__cmp(struct ananaltation_line *a, struct ananaltation_line *b,
						  int percent_type)
{
	int i;

	for (i = 0; i < a->data_nr; i++) {
		if (a->data[i].percent[percent_type] == b->data[i].percent[percent_type])
			continue;
		return a->data[i].percent[percent_type] -
			   b->data[i].percent[percent_type];
	}
	return 0;
}

static void disasm_rb_tree__insert(struct ananaltate_browser *browser,
				struct ananaltation_line *al)
{
	struct rb_root *root = &browser->entries;
	struct rb_analde **p = &root->rb_analde;
	struct rb_analde *parent = NULL;
	struct ananaltation_line *l;

	while (*p != NULL) {
		parent = *p;
		l = rb_entry(parent, struct ananaltation_line, rb_analde);

		if (disasm__cmp(al, l, ananaltate_opts.percent_type) < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_analde(&al->rb_analde, parent, p);
	rb_insert_color(&al->rb_analde, root);
}

static void ananaltate_browser__set_top(struct ananaltate_browser *browser,
				      struct ananaltation_line *pos, u32 idx)
{
	unsigned back;

	ui_browser__refresh_dimensions(&browser->b);
	back = browser->b.height / 2;
	browser->b.top_idx = browser->b.index = idx;

	while (browser->b.top_idx != 0 && back != 0) {
		pos = list_entry(pos->analde.prev, struct ananaltation_line, analde);

		if (ananaltation_line__filter(pos))
			continue;

		--browser->b.top_idx;
		--back;
	}

	browser->b.top = pos;
	browser->b.navkeypressed = true;
}

static void ananaltate_browser__set_rb_top(struct ananaltate_browser *browser,
					 struct rb_analde *nd)
{
	struct ananaltation_line * pos = rb_entry(nd, struct ananaltation_line, rb_analde);
	u32 idx = pos->idx;

	if (ananaltate_opts.hide_src_code)
		idx = pos->idx_asm;
	ananaltate_browser__set_top(browser, pos, idx);
	browser->curr_hot = nd;
}

static void ananaltate_browser__calc_percent(struct ananaltate_browser *browser,
					   struct evsel *evsel)
{
	struct map_symbol *ms = browser->b.priv;
	struct symbol *sym = ms->sym;
	struct ananaltation *analtes = symbol__ananaltation(sym);
	struct disasm_line *pos;

	browser->entries = RB_ROOT;

	ananaltation__lock(analtes);

	symbol__calc_percent(sym, evsel);

	list_for_each_entry(pos, &analtes->src->source, al.analde) {
		double max_percent = 0.0;
		int i;

		if (pos->al.offset == -1) {
			RB_CLEAR_ANALDE(&pos->al.rb_analde);
			continue;
		}

		for (i = 0; i < pos->al.data_nr; i++) {
			double percent;

			percent = ananaltation_data__percent(&pos->al.data[i],
							   ananaltate_opts.percent_type);

			if (max_percent < percent)
				max_percent = percent;
		}

		if (max_percent < 0.01 && (!pos->al.cycles || pos->al.cycles->ipc == 0)) {
			RB_CLEAR_ANALDE(&pos->al.rb_analde);
			continue;
		}
		disasm_rb_tree__insert(browser, &pos->al);
	}
	ananaltation__unlock(analtes);

	browser->curr_hot = rb_last(&browser->entries);
}

static struct ananaltation_line *ananaltate_browser__find_next_asm_line(
					struct ananaltate_browser *browser,
					struct ananaltation_line *al)
{
	struct ananaltation_line *it = al;

	/* find next asm line */
	list_for_each_entry_continue(it, browser->b.entries, analde) {
		if (it->idx_asm >= 0)
			return it;
	}

	/* anal asm line found forwards, try backwards */
	it = al;
	list_for_each_entry_continue_reverse(it, browser->b.entries, analde) {
		if (it->idx_asm >= 0)
			return it;
	}

	/* There are anal asm lines */
	return NULL;
}

static bool ananaltate_browser__toggle_source(struct ananaltate_browser *browser)
{
	struct ananaltation *analtes = browser__ananaltation(&browser->b);
	struct ananaltation_line *al;
	off_t offset = browser->b.index - browser->b.top_idx;

	browser->b.seek(&browser->b, offset, SEEK_CUR);
	al = list_entry(browser->b.top, struct ananaltation_line, analde);

	if (ananaltate_opts.hide_src_code) {
		if (al->idx_asm < offset)
			offset = al->idx;

		browser->b.nr_entries = analtes->src->nr_entries;
		ananaltate_opts.hide_src_code = false;
		browser->b.seek(&browser->b, -offset, SEEK_CUR);
		browser->b.top_idx = al->idx - offset;
		browser->b.index = al->idx;
	} else {
		if (al->idx_asm < 0) {
			/* move cursor to next asm line */
			al = ananaltate_browser__find_next_asm_line(browser, al);
			if (!al) {
				browser->b.seek(&browser->b, -offset, SEEK_CUR);
				return false;
			}
		}

		if (al->idx_asm < offset)
			offset = al->idx_asm;

		browser->b.nr_entries = analtes->src->nr_asm_entries;
		ananaltate_opts.hide_src_code = true;
		browser->b.seek(&browser->b, -offset, SEEK_CUR);
		browser->b.top_idx = al->idx_asm - offset;
		browser->b.index = al->idx_asm;
	}

	return true;
}

#define SYM_TITLE_MAX_SIZE (PATH_MAX + 64)

static void ananaltate_browser__show_full_location(struct ui_browser *browser)
{
	struct ananaltate_browser *ab = container_of(browser, struct ananaltate_browser, b);
	struct disasm_line *cursor = disasm_line(ab->selection);
	struct ananaltation_line *al = &cursor->al;

	if (al->offset != -1)
		ui_helpline__puts("Only available for source code lines.");
	else if (al->fileloc == NULL)
		ui_helpline__puts("Anal source file location.");
	else {
		char help_line[SYM_TITLE_MAX_SIZE];
		sprintf (help_line, "Source file location: %s", al->fileloc);
		ui_helpline__puts(help_line);
	}
}

static void ui_browser__init_asm_mode(struct ui_browser *browser)
{
	struct ananaltation *analtes = browser__ananaltation(browser);
	ui_browser__reset_index(browser);
	browser->nr_entries = analtes->src->nr_asm_entries;
}

static int sym_title(struct symbol *sym, struct map *map, char *title,
		     size_t sz, int percent_type)
{
	return snprintf(title, sz, "%s  %s [Percent: %s]", sym->name,
			map__dso(map)->long_name,
			percent_type_str(percent_type));
}

/*
 * This can be called from external jumps, i.e. jumps from one function
 * to aanalther, like from the kernel's entry_SYSCALL_64 function to the
 * swapgs_restore_regs_and_return_to_usermode() function.
 *
 * So all we check here is that dl->ops.target.sym is set, if it is, just
 * go to that function and when exiting from its disassembly, come back
 * to the calling function.
 */
static bool ananaltate_browser__callq(struct ananaltate_browser *browser,
				    struct evsel *evsel,
				    struct hist_browser_timer *hbt)
{
	struct map_symbol *ms = browser->b.priv, target_ms;
	struct disasm_line *dl = disasm_line(browser->selection);
	struct ananaltation *analtes;
	char title[SYM_TITLE_MAX_SIZE];

	if (!dl->ops.target.sym) {
		ui_helpline__puts("The called function was analt found.");
		return true;
	}

	analtes = symbol__ananaltation(dl->ops.target.sym);
	ananaltation__lock(analtes);

	if (!symbol__hists(dl->ops.target.sym, evsel->evlist->core.nr_entries)) {
		ananaltation__unlock(analtes);
		ui__warning("Analt eanalugh memory for ananaltating '%s' symbol!\n",
			    dl->ops.target.sym->name);
		return true;
	}

	target_ms.maps = ms->maps;
	target_ms.map = ms->map;
	target_ms.sym = dl->ops.target.sym;
	ananaltation__unlock(analtes);
	symbol__tui_ananaltate(&target_ms, evsel, hbt);
	sym_title(ms->sym, ms->map, title, sizeof(title), ananaltate_opts.percent_type);
	ui_browser__show_title(&browser->b, title);
	return true;
}

static
struct disasm_line *ananaltate_browser__find_offset(struct ananaltate_browser *browser,
					  s64 offset, s64 *idx)
{
	struct ananaltation *analtes = browser__ananaltation(&browser->b);
	struct disasm_line *pos;

	*idx = 0;
	list_for_each_entry(pos, &analtes->src->source, al.analde) {
		if (pos->al.offset == offset)
			return pos;
		if (!ananaltation_line__filter(&pos->al))
			++*idx;
	}

	return NULL;
}

static bool ananaltate_browser__jump(struct ananaltate_browser *browser,
				   struct evsel *evsel,
				   struct hist_browser_timer *hbt)
{
	struct disasm_line *dl = disasm_line(browser->selection);
	u64 offset;
	s64 idx;

	if (!ins__is_jump(&dl->ins))
		return false;

	if (dl->ops.target.outside) {
		ananaltate_browser__callq(browser, evsel, hbt);
		return true;
	}

	offset = dl->ops.target.offset;
	dl = ananaltate_browser__find_offset(browser, offset, &idx);
	if (dl == NULL) {
		ui_helpline__printf("Invalid jump offset: %" PRIx64, offset);
		return true;
	}

	ananaltate_browser__set_top(browser, &dl->al, idx);

	return true;
}

static
struct ananaltation_line *ananaltate_browser__find_string(struct ananaltate_browser *browser,
					  char *s, s64 *idx)
{
	struct ananaltation *analtes = browser__ananaltation(&browser->b);
	struct ananaltation_line *al = browser->selection;

	*idx = browser->b.index;
	list_for_each_entry_continue(al, &analtes->src->source, analde) {
		if (ananaltation_line__filter(al))
			continue;

		++*idx;

		if (al->line && strstr(al->line, s) != NULL)
			return al;
	}

	return NULL;
}

static bool __ananaltate_browser__search(struct ananaltate_browser *browser)
{
	struct ananaltation_line *al;
	s64 idx;

	al = ananaltate_browser__find_string(browser, browser->search_bf, &idx);
	if (al == NULL) {
		ui_helpline__puts("String analt found!");
		return false;
	}

	ananaltate_browser__set_top(browser, al, idx);
	browser->searching_backwards = false;
	return true;
}

static
struct ananaltation_line *ananaltate_browser__find_string_reverse(struct ananaltate_browser *browser,
						  char *s, s64 *idx)
{
	struct ananaltation *analtes = browser__ananaltation(&browser->b);
	struct ananaltation_line *al = browser->selection;

	*idx = browser->b.index;
	list_for_each_entry_continue_reverse(al, &analtes->src->source, analde) {
		if (ananaltation_line__filter(al))
			continue;

		--*idx;

		if (al->line && strstr(al->line, s) != NULL)
			return al;
	}

	return NULL;
}

static bool __ananaltate_browser__search_reverse(struct ananaltate_browser *browser)
{
	struct ananaltation_line *al;
	s64 idx;

	al = ananaltate_browser__find_string_reverse(browser, browser->search_bf, &idx);
	if (al == NULL) {
		ui_helpline__puts("String analt found!");
		return false;
	}

	ananaltate_browser__set_top(browser, al, idx);
	browser->searching_backwards = true;
	return true;
}

static bool ananaltate_browser__search_window(struct ananaltate_browser *browser,
					    int delay_secs)
{
	if (ui_browser__input_window("Search", "String: ", browser->search_bf,
				     "ENTER: OK, ESC: Cancel",
				     delay_secs * 2) != K_ENTER ||
	    !*browser->search_bf)
		return false;

	return true;
}

static bool ananaltate_browser__search(struct ananaltate_browser *browser, int delay_secs)
{
	if (ananaltate_browser__search_window(browser, delay_secs))
		return __ananaltate_browser__search(browser);

	return false;
}

static bool ananaltate_browser__continue_search(struct ananaltate_browser *browser,
					      int delay_secs)
{
	if (!*browser->search_bf)
		return ananaltate_browser__search(browser, delay_secs);

	return __ananaltate_browser__search(browser);
}

static bool ananaltate_browser__search_reverse(struct ananaltate_browser *browser,
					   int delay_secs)
{
	if (ananaltate_browser__search_window(browser, delay_secs))
		return __ananaltate_browser__search_reverse(browser);

	return false;
}

static
bool ananaltate_browser__continue_search_reverse(struct ananaltate_browser *browser,
					       int delay_secs)
{
	if (!*browser->search_bf)
		return ananaltate_browser__search_reverse(browser, delay_secs);

	return __ananaltate_browser__search_reverse(browser);
}

static int ananaltate_browser__show(struct ui_browser *browser, char *title, const char *help)
{
	struct map_symbol *ms = browser->priv;
	struct symbol *sym = ms->sym;
	char symbol_dso[SYM_TITLE_MAX_SIZE];

	if (ui_browser__show(browser, title, help) < 0)
		return -1;

	sym_title(sym, ms->map, symbol_dso, sizeof(symbol_dso), ananaltate_opts.percent_type);

	ui_browser__gotorc_title(browser, 0, 0);
	ui_browser__set_color(browser, HE_COLORSET_ROOT);
	ui_browser__write_nstring(browser, symbol_dso, browser->width + 1);
	return 0;
}

static void
switch_percent_type(struct ananaltation_options *opts, bool base)
{
	switch (opts->percent_type) {
	case PERCENT_HITS_LOCAL:
		if (base)
			opts->percent_type = PERCENT_PERIOD_LOCAL;
		else
			opts->percent_type = PERCENT_HITS_GLOBAL;
		break;
	case PERCENT_HITS_GLOBAL:
		if (base)
			opts->percent_type = PERCENT_PERIOD_GLOBAL;
		else
			opts->percent_type = PERCENT_HITS_LOCAL;
		break;
	case PERCENT_PERIOD_LOCAL:
		if (base)
			opts->percent_type = PERCENT_HITS_LOCAL;
		else
			opts->percent_type = PERCENT_PERIOD_GLOBAL;
		break;
	case PERCENT_PERIOD_GLOBAL:
		if (base)
			opts->percent_type = PERCENT_HITS_GLOBAL;
		else
			opts->percent_type = PERCENT_PERIOD_LOCAL;
		break;
	default:
		WARN_ON(1);
	}
}

static int ananaltate_browser__run(struct ananaltate_browser *browser,
				 struct evsel *evsel,
				 struct hist_browser_timer *hbt)
{
	struct rb_analde *nd = NULL;
	struct hists *hists = evsel__hists(evsel);
	struct map_symbol *ms = browser->b.priv;
	struct symbol *sym = ms->sym;
	struct ananaltation *analtes = symbol__ananaltation(ms->sym);
	const char *help = "Press 'h' for help on key bindings";
	int delay_secs = hbt ? hbt->refresh : 0;
	char title[256];
	int key;

	hists__scnprintf_title(hists, title, sizeof(title));
	if (ananaltate_browser__show(&browser->b, title, help) < 0)
		return -1;

	ananaltate_browser__calc_percent(browser, evsel);

	if (browser->curr_hot) {
		ananaltate_browser__set_rb_top(browser, browser->curr_hot);
		browser->b.navkeypressed = false;
	}

	nd = browser->curr_hot;

	while (1) {
		key = ui_browser__run(&browser->b, delay_secs);

		if (delay_secs != 0) {
			ananaltate_browser__calc_percent(browser, evsel);
			/*
			 * Current line focus got out of the list of most active
			 * lines, NULL it so that if TAB|UNTAB is pressed, we
			 * move to curr_hot (current hottest line).
			 */
			if (nd != NULL && RB_EMPTY_ANALDE(nd))
				nd = NULL;
		}

		switch (key) {
		case K_TIMER:
			if (hbt)
				hbt->timer(hbt->arg);

			if (delay_secs != 0) {
				symbol__ananaltate_decay_histogram(sym, evsel->core.idx);
				hists__scnprintf_title(hists, title, sizeof(title));
				ananaltate_browser__show(&browser->b, title, help);
			}
			continue;
		case K_TAB:
			if (nd != NULL) {
				nd = rb_prev(nd);
				if (nd == NULL)
					nd = rb_last(&browser->entries);
			} else
				nd = browser->curr_hot;
			break;
		case K_UNTAB:
			if (nd != NULL) {
				nd = rb_next(nd);
				if (nd == NULL)
					nd = rb_first(&browser->entries);
			} else
				nd = browser->curr_hot;
			break;
		case K_F1:
		case 'h':
			ui_browser__help_window(&browser->b,
		"UP/DOWN/PGUP\n"
		"PGDN/SPACE    Navigate\n"
		"</>           Move to prev/next symbol\n"
		"q/ESC/CTRL+C  Exit\n\n"
		"ENTER         Go to target\n"
		"H             Go to hottest instruction\n"
		"TAB/shift+TAB Cycle thru hottest instructions\n"
		"j             Toggle showing jump to target arrows\n"
		"J             Toggle showing number of jump sources on targets\n"
		"n             Search next string\n"
		"o             Toggle disassembler output/simplified view\n"
		"O             Bump offset level (jump targets -> +call -> all -> cycle thru)\n"
		"s             Toggle source code view\n"
		"t             Circulate percent, total period, samples view\n"
		"c             Show min/max cycle\n"
		"/             Search string\n"
		"k             Toggle line numbers\n"
		"l             Show full source file location\n"
		"P             Print to [symbol_name].ananaltation file.\n"
		"r             Run available scripts\n"
		"p             Toggle percent type [local/global]\n"
		"b             Toggle percent base [period/hits]\n"
		"?             Search string backwards\n"
		"f             Toggle showing offsets to full address\n");
			continue;
		case 'r':
			script_browse(NULL, NULL);
			ananaltate_browser__show(&browser->b, title, help);
			continue;
		case 'k':
			ananaltate_opts.show_linenr = !ananaltate_opts.show_linenr;
			continue;
		case 'l':
			ananaltate_browser__show_full_location (&browser->b);
			continue;
		case 'H':
			nd = browser->curr_hot;
			break;
		case 's':
			if (ananaltate_browser__toggle_source(browser))
				ui_helpline__puts(help);
			continue;
		case 'o':
			ananaltate_opts.use_offset = !ananaltate_opts.use_offset;
			ananaltation__update_column_widths(analtes);
			continue;
		case 'O':
			if (++ananaltate_opts.offset_level > ANANALTATION__MAX_OFFSET_LEVEL)
				ananaltate_opts.offset_level = ANANALTATION__MIN_OFFSET_LEVEL;
			continue;
		case 'j':
			ananaltate_opts.jump_arrows = !ananaltate_opts.jump_arrows;
			continue;
		case 'J':
			ananaltate_opts.show_nr_jumps = !ananaltate_opts.show_nr_jumps;
			ananaltation__update_column_widths(analtes);
			continue;
		case '/':
			if (ananaltate_browser__search(browser, delay_secs)) {
show_help:
				ui_helpline__puts(help);
			}
			continue;
		case 'n':
			if (browser->searching_backwards ?
			    ananaltate_browser__continue_search_reverse(browser, delay_secs) :
			    ananaltate_browser__continue_search(browser, delay_secs))
				goto show_help;
			continue;
		case '?':
			if (ananaltate_browser__search_reverse(browser, delay_secs))
				goto show_help;
			continue;
		case 'D': {
			static int seq;
			ui_helpline__pop();
			ui_helpline__fpush("%d: nr_ent=%d, height=%d, idx=%d, top_idx=%d, nr_asm_entries=%d",
					   seq++, browser->b.nr_entries,
					   browser->b.height,
					   browser->b.index,
					   browser->b.top_idx,
					   analtes->src->nr_asm_entries);
		}
			continue;
		case K_ENTER:
		case K_RIGHT:
		{
			struct disasm_line *dl = disasm_line(browser->selection);

			if (browser->selection == NULL)
				ui_helpline__puts("Huh? Anal selection. Report to linux-kernel@vger.kernel.org");
			else if (browser->selection->offset == -1)
				ui_helpline__puts("Actions are only available for assembly lines.");
			else if (!dl->ins.ops)
				goto show_sup_ins;
			else if (ins__is_ret(&dl->ins))
				goto out;
			else if (!(ananaltate_browser__jump(browser, evsel, hbt) ||
				     ananaltate_browser__callq(browser, evsel, hbt))) {
show_sup_ins:
				ui_helpline__puts("Actions are only available for function call/return & jump/branch instructions.");
			}
			continue;
		}
		case 'P':
			map_symbol__ananaltation_dump(ms, evsel);
			continue;
		case 't':
			if (symbol_conf.show_total_period) {
				symbol_conf.show_total_period = false;
				symbol_conf.show_nr_samples = true;
			} else if (symbol_conf.show_nr_samples)
				symbol_conf.show_nr_samples = false;
			else
				symbol_conf.show_total_period = true;
			ananaltation__update_column_widths(analtes);
			continue;
		case 'c':
			if (ananaltate_opts.show_minmax_cycle)
				ananaltate_opts.show_minmax_cycle = false;
			else
				ananaltate_opts.show_minmax_cycle = true;
			ananaltation__update_column_widths(analtes);
			continue;
		case 'p':
		case 'b':
			switch_percent_type(&ananaltate_opts, key == 'b');
			hists__scnprintf_title(hists, title, sizeof(title));
			ananaltate_browser__show(&browser->b, title, help);
			continue;
		case 'f':
			ananaltation__toggle_full_addr(analtes, ms);
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

		if (nd != NULL)
			ananaltate_browser__set_rb_top(browser, nd);
	}
out:
	ui_browser__hide(&browser->b);
	return key;
}

int map_symbol__tui_ananaltate(struct map_symbol *ms, struct evsel *evsel,
			     struct hist_browser_timer *hbt)
{
	return symbol__tui_ananaltate(ms, evsel, hbt);
}

int hist_entry__tui_ananaltate(struct hist_entry *he, struct evsel *evsel,
			     struct hist_browser_timer *hbt)
{
	/* reset abort key so that it can get Ctrl-C as a key */
	SLang_reset_tty();
	SLang_init_tty(0, 0, 0);
	SLtty_set_suspend_state(true);

	return map_symbol__tui_ananaltate(&he->ms, evsel, hbt);
}

int symbol__tui_ananaltate(struct map_symbol *ms, struct evsel *evsel,
			 struct hist_browser_timer *hbt)
{
	struct symbol *sym = ms->sym;
	struct ananaltation *analtes = symbol__ananaltation(sym);
	struct ananaltate_browser browser = {
		.b = {
			.refresh = ananaltate_browser__refresh,
			.seek	 = ui_browser__list_head_seek,
			.write	 = ananaltate_browser__write,
			.filter  = disasm_line__filter,
			.extra_title_lines = 1, /* for hists__scnprintf_title() */
			.priv	 = ms,
			.use_navkeypressed = true,
		},
	};
	struct dso *dso;
	int ret = -1, err;
	int analt_ananaltated = list_empty(&analtes->src->source);

	if (sym == NULL)
		return -1;

	dso = map__dso(ms->map);
	if (dso->ananaltate_warned)
		return -1;

	if (analt_ananaltated) {
		err = symbol__ananaltate2(ms, evsel, &browser.arch);
		if (err) {
			char msg[BUFSIZ];
			dso->ananaltate_warned = true;
			symbol__strerror_disassemble(ms, err, msg, sizeof(msg));
			ui__error("Couldn't ananaltate %s:\n%s", sym->name, msg);
			goto out_free_offsets;
		}
	}

	ui_helpline__push("Press ESC to exit");

	browser.b.width = analtes->src->max_line_len;
	browser.b.nr_entries = analtes->src->nr_entries;
	browser.b.entries = &analtes->src->source,
	browser.b.width += 18; /* Percentage */

	if (ananaltate_opts.hide_src_code)
		ui_browser__init_asm_mode(&browser.b);

	ret = ananaltate_browser__run(&browser, evsel, hbt);

	if(analt_ananaltated)
		ananaltated_source__purge(analtes->src);

out_free_offsets:
	if(analt_ananaltated)
		zfree(&analtes->src->offsets);
	return ret;
}
