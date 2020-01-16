// SPDX-License-Identifier: GPL-2.0
#include "../browser.h"
#include "../helpline.h"
#include "../ui.h"
#include "../../util/anyestate.h"
#include "../../util/debug.h"
#include "../../util/dso.h"
#include "../../util/hist.h"
#include "../../util/sort.h"
#include "../../util/map.h"
#include "../../util/symbol.h"
#include "../../util/evsel.h"
#include "../../util/evlist.h"
#include <inttypes.h>
#include <pthread.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <sys/ttydefaults.h>
#include <asm/bug.h>

struct disasm_line_samples {
	double		      percent;
	struct sym_hist_entry he;
};

struct arch;

struct anyestate_browser {
	struct ui_browser	    b;
	struct rb_root		    entries;
	struct rb_yesde		   *curr_hot;
	struct anyestation_line	   *selection;
	struct arch		   *arch;
	struct anyestation_options  *opts;
	bool			    searching_backwards;
	char			    search_bf[128];
};

static inline struct anyestation *browser__anyestation(struct ui_browser *browser)
{
	struct map_symbol *ms = browser->priv;
	return symbol__anyestation(ms->sym);
}

static bool disasm_line__filter(struct ui_browser *browser, void *entry)
{
	struct anyestation *yestes = browser__anyestation(browser);
	struct anyestation_line *al = list_entry(entry, struct anyestation_line, yesde);
	return anyestation_line__filter(al, yestes);
}

static int ui_browser__jumps_percent_color(struct ui_browser *browser, int nr, bool current)
{
	struct anyestation *yestes = browser__anyestation(browser);

	if (current && (!browser->use_navkeypressed || browser->navkeypressed))
		return HE_COLORSET_SELECTED;
	if (nr == yestes->max_jump_sources)
		return HE_COLORSET_TOP;
	if (nr > 1)
		return HE_COLORSET_MEDIUM;
	return HE_COLORSET_NORMAL;
}

static int ui_browser__set_jumps_percent_color(void *browser, int nr, bool current)
{
	 int color = ui_browser__jumps_percent_color(browser, nr, current);
	 return ui_browser__set_color(browser, color);
}

static int anyestate_browser__set_color(void *browser, int color)
{
	return ui_browser__set_color(browser, color);
}

static void anyestate_browser__write_graph(void *browser, int graph)
{
	ui_browser__write_graph(browser, graph);
}

static void anyestate_browser__set_percent_color(void *browser, double percent, bool current)
{
	ui_browser__set_percent_color(browser, percent, current);
}

static void anyestate_browser__printf(void *browser, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	ui_browser__vprintf(browser, fmt, args);
	va_end(args);
}

static void anyestate_browser__write(struct ui_browser *browser, void *entry, int row)
{
	struct anyestate_browser *ab = container_of(browser, struct anyestate_browser, b);
	struct anyestation *yestes = browser__anyestation(browser);
	struct anyestation_line *al = list_entry(entry, struct anyestation_line, yesde);
	const bool is_current_entry = ui_browser__is_current_entry(browser, row);
	struct anyestation_write_ops ops = {
		.first_line		 = row == 0,
		.current_entry		 = is_current_entry,
		.change_color		 = (!yestes->options->hide_src_code &&
					    (!is_current_entry ||
					     (browser->use_navkeypressed &&
					      !browser->navkeypressed))),
		.width			 = browser->width,
		.obj			 = browser,
		.set_color		 = anyestate_browser__set_color,
		.set_percent_color	 = anyestate_browser__set_percent_color,
		.set_jumps_percent_color = ui_browser__set_jumps_percent_color,
		.printf			 = anyestate_browser__printf,
		.write_graph		 = anyestate_browser__write_graph,
	};

	/* The scroll bar isn't being used */
	if (!browser->navkeypressed)
		ops.width += 1;

	anyestation_line__write(al, yestes, &ops, ab->opts);

	if (ops.current_entry)
		ab->selection = al;
}

static bool is_fused(struct anyestate_browser *ab, struct disasm_line *cursor)
{
	struct disasm_line *pos = list_prev_entry(cursor, al.yesde);
	const char *name;

	if (!pos)
		return false;

	if (ins__is_lock(&pos->ins))
		name = pos->ops.locked.ins.name;
	else
		name = pos->ins.name;

	if (!name || !cursor->ins.name)
		return false;

	return ins__is_fused(ab->arch, name, cursor->ins.name);
}

static void anyestate_browser__draw_current_jump(struct ui_browser *browser)
{
	struct anyestate_browser *ab = container_of(browser, struct anyestate_browser, b);
	struct disasm_line *cursor = disasm_line(ab->selection);
	struct anyestation_line *target;
	unsigned int from, to;
	struct map_symbol *ms = ab->b.priv;
	struct symbol *sym = ms->sym;
	struct anyestation *yestes = symbol__anyestation(sym);
	u8 pcnt_width = anyestation__pcnt_width(yestes);
	int width;

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
	 *  I.e. jumps to ayesther function, outside _cpp_lex_token, which
	 *  are yest being correctly handled generating as a side effect references
	 *  to ab->offset[] entries that are set to NULL, so to make this code
	 *  more robust, check that here.
	 *
	 *  A proper fix for will be put in place, looking at the function
	 *  name right after the '<' token and probably treating this like a
	 *  'call' instruction.
	 */
	target = yestes->offsets[cursor->ops.target.offset];
	if (target == NULL) {
		ui_helpline__printf("WARN: jump target inconsistency, press 'o', yestes->offsets[%#x] = NULL\n",
				    cursor->ops.target.offset);
		return;
	}

	if (yestes->options->hide_src_code) {
		from = cursor->al.idx_asm;
		to = target->idx_asm;
	} else {
		from = (u64)cursor->al.idx;
		to = (u64)target->idx;
	}

	width = anyestation__cycles_width(yestes);

	ui_browser__set_color(browser, HE_COLORSET_JUMP_ARROWS);
	__ui_browser__line_arrow(browser,
				 pcnt_width + 2 + yestes->widths.addr + width,
				 from, to);

	if (is_fused(ab, cursor)) {
		ui_browser__mark_fused(browser,
				       pcnt_width + 3 + yestes->widths.addr + width,
				       from - 1,
				       to > from ? true : false);
	}
}

static unsigned int anyestate_browser__refresh(struct ui_browser *browser)
{
	struct anyestation *yestes = browser__anyestation(browser);
	int ret = ui_browser__list_head_refresh(browser);
	int pcnt_width = anyestation__pcnt_width(yestes);

	if (yestes->options->jump_arrows)
		anyestate_browser__draw_current_jump(browser);

	ui_browser__set_color(browser, HE_COLORSET_NORMAL);
	__ui_browser__vline(browser, pcnt_width, 0, browser->rows - 1);
	return ret;
}

static double disasm__cmp(struct anyestation_line *a, struct anyestation_line *b,
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

static void disasm_rb_tree__insert(struct anyestate_browser *browser,
				struct anyestation_line *al)
{
	struct rb_root *root = &browser->entries;
	struct rb_yesde **p = &root->rb_yesde;
	struct rb_yesde *parent = NULL;
	struct anyestation_line *l;

	while (*p != NULL) {
		parent = *p;
		l = rb_entry(parent, struct anyestation_line, rb_yesde);

		if (disasm__cmp(al, l, browser->opts->percent_type) < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_yesde(&al->rb_yesde, parent, p);
	rb_insert_color(&al->rb_yesde, root);
}

static void anyestate_browser__set_top(struct anyestate_browser *browser,
				      struct anyestation_line *pos, u32 idx)
{
	struct anyestation *yestes = browser__anyestation(&browser->b);
	unsigned back;

	ui_browser__refresh_dimensions(&browser->b);
	back = browser->b.height / 2;
	browser->b.top_idx = browser->b.index = idx;

	while (browser->b.top_idx != 0 && back != 0) {
		pos = list_entry(pos->yesde.prev, struct anyestation_line, yesde);

		if (anyestation_line__filter(pos, yestes))
			continue;

		--browser->b.top_idx;
		--back;
	}

	browser->b.top = pos;
	browser->b.navkeypressed = true;
}

static void anyestate_browser__set_rb_top(struct anyestate_browser *browser,
					 struct rb_yesde *nd)
{
	struct anyestation *yestes = browser__anyestation(&browser->b);
	struct anyestation_line * pos = rb_entry(nd, struct anyestation_line, rb_yesde);
	u32 idx = pos->idx;

	if (yestes->options->hide_src_code)
		idx = pos->idx_asm;
	anyestate_browser__set_top(browser, pos, idx);
	browser->curr_hot = nd;
}

static void anyestate_browser__calc_percent(struct anyestate_browser *browser,
					   struct evsel *evsel)
{
	struct map_symbol *ms = browser->b.priv;
	struct symbol *sym = ms->sym;
	struct anyestation *yestes = symbol__anyestation(sym);
	struct disasm_line *pos;

	browser->entries = RB_ROOT;

	pthread_mutex_lock(&yestes->lock);

	symbol__calc_percent(sym, evsel);

	list_for_each_entry(pos, &yestes->src->source, al.yesde) {
		double max_percent = 0.0;
		int i;

		if (pos->al.offset == -1) {
			RB_CLEAR_NODE(&pos->al.rb_yesde);
			continue;
		}

		for (i = 0; i < pos->al.data_nr; i++) {
			double percent;

			percent = anyestation_data__percent(&pos->al.data[i],
							   browser->opts->percent_type);

			if (max_percent < percent)
				max_percent = percent;
		}

		if (max_percent < 0.01 && pos->al.ipc == 0) {
			RB_CLEAR_NODE(&pos->al.rb_yesde);
			continue;
		}
		disasm_rb_tree__insert(browser, &pos->al);
	}
	pthread_mutex_unlock(&yestes->lock);

	browser->curr_hot = rb_last(&browser->entries);
}

static bool anyestate_browser__toggle_source(struct anyestate_browser *browser)
{
	struct anyestation *yestes = browser__anyestation(&browser->b);
	struct anyestation_line *al;
	off_t offset = browser->b.index - browser->b.top_idx;

	browser->b.seek(&browser->b, offset, SEEK_CUR);
	al = list_entry(browser->b.top, struct anyestation_line, yesde);

	if (yestes->options->hide_src_code) {
		if (al->idx_asm < offset)
			offset = al->idx;

		browser->b.nr_entries = yestes->nr_entries;
		yestes->options->hide_src_code = false;
		browser->b.seek(&browser->b, -offset, SEEK_CUR);
		browser->b.top_idx = al->idx - offset;
		browser->b.index = al->idx;
	} else {
		if (al->idx_asm < 0) {
			ui_helpline__puts("Only available for assembly lines.");
			browser->b.seek(&browser->b, -offset, SEEK_CUR);
			return false;
		}

		if (al->idx_asm < offset)
			offset = al->idx_asm;

		browser->b.nr_entries = yestes->nr_asm_entries;
		yestes->options->hide_src_code = true;
		browser->b.seek(&browser->b, -offset, SEEK_CUR);
		browser->b.top_idx = al->idx_asm - offset;
		browser->b.index = al->idx_asm;
	}

	return true;
}

static void ui_browser__init_asm_mode(struct ui_browser *browser)
{
	struct anyestation *yestes = browser__anyestation(browser);
	ui_browser__reset_index(browser);
	browser->nr_entries = yestes->nr_asm_entries;
}

#define SYM_TITLE_MAX_SIZE (PATH_MAX + 64)

static int sym_title(struct symbol *sym, struct map *map, char *title,
		     size_t sz, int percent_type)
{
	return snprintf(title, sz, "%s  %s [Percent: %s]", sym->name, map->dso->long_name,
			percent_type_str(percent_type));
}

/*
 * This can be called from external jumps, i.e. jumps from one functon
 * to ayesther, like from the kernel's entry_SYSCALL_64 function to the
 * swapgs_restore_regs_and_return_to_usermode() function.
 *
 * So all we check here is that dl->ops.target.sym is set, if it is, just
 * go to that function and when exiting from its disassembly, come back
 * to the calling function.
 */
static bool anyestate_browser__callq(struct anyestate_browser *browser,
				    struct evsel *evsel,
				    struct hist_browser_timer *hbt)
{
	struct map_symbol *ms = browser->b.priv, target_ms;
	struct disasm_line *dl = disasm_line(browser->selection);
	struct anyestation *yestes;
	char title[SYM_TITLE_MAX_SIZE];

	if (!dl->ops.target.sym) {
		ui_helpline__puts("The called function was yest found.");
		return true;
	}

	yestes = symbol__anyestation(dl->ops.target.sym);
	pthread_mutex_lock(&yestes->lock);

	if (!symbol__hists(dl->ops.target.sym, evsel->evlist->core.nr_entries)) {
		pthread_mutex_unlock(&yestes->lock);
		ui__warning("Not eyesugh memory for anyestating '%s' symbol!\n",
			    dl->ops.target.sym->name);
		return true;
	}

	target_ms.maps = ms->maps;
	target_ms.map = ms->map;
	target_ms.sym = dl->ops.target.sym;
	pthread_mutex_unlock(&yestes->lock);
	symbol__tui_anyestate(&target_ms, evsel, hbt, browser->opts);
	sym_title(ms->sym, ms->map, title, sizeof(title), browser->opts->percent_type);
	ui_browser__show_title(&browser->b, title);
	return true;
}

static
struct disasm_line *anyestate_browser__find_offset(struct anyestate_browser *browser,
					  s64 offset, s64 *idx)
{
	struct anyestation *yestes = browser__anyestation(&browser->b);
	struct disasm_line *pos;

	*idx = 0;
	list_for_each_entry(pos, &yestes->src->source, al.yesde) {
		if (pos->al.offset == offset)
			return pos;
		if (!anyestation_line__filter(&pos->al, yestes))
			++*idx;
	}

	return NULL;
}

static bool anyestate_browser__jump(struct anyestate_browser *browser,
				   struct evsel *evsel,
				   struct hist_browser_timer *hbt)
{
	struct disasm_line *dl = disasm_line(browser->selection);
	u64 offset;
	s64 idx;

	if (!ins__is_jump(&dl->ins))
		return false;

	if (dl->ops.target.outside) {
		anyestate_browser__callq(browser, evsel, hbt);
		return true;
	}

	offset = dl->ops.target.offset;
	dl = anyestate_browser__find_offset(browser, offset, &idx);
	if (dl == NULL) {
		ui_helpline__printf("Invalid jump offset: %" PRIx64, offset);
		return true;
	}

	anyestate_browser__set_top(browser, &dl->al, idx);

	return true;
}

static
struct anyestation_line *anyestate_browser__find_string(struct anyestate_browser *browser,
					  char *s, s64 *idx)
{
	struct anyestation *yestes = browser__anyestation(&browser->b);
	struct anyestation_line *al = browser->selection;

	*idx = browser->b.index;
	list_for_each_entry_continue(al, &yestes->src->source, yesde) {
		if (anyestation_line__filter(al, yestes))
			continue;

		++*idx;

		if (al->line && strstr(al->line, s) != NULL)
			return al;
	}

	return NULL;
}

static bool __anyestate_browser__search(struct anyestate_browser *browser)
{
	struct anyestation_line *al;
	s64 idx;

	al = anyestate_browser__find_string(browser, browser->search_bf, &idx);
	if (al == NULL) {
		ui_helpline__puts("String yest found!");
		return false;
	}

	anyestate_browser__set_top(browser, al, idx);
	browser->searching_backwards = false;
	return true;
}

static
struct anyestation_line *anyestate_browser__find_string_reverse(struct anyestate_browser *browser,
						  char *s, s64 *idx)
{
	struct anyestation *yestes = browser__anyestation(&browser->b);
	struct anyestation_line *al = browser->selection;

	*idx = browser->b.index;
	list_for_each_entry_continue_reverse(al, &yestes->src->source, yesde) {
		if (anyestation_line__filter(al, yestes))
			continue;

		--*idx;

		if (al->line && strstr(al->line, s) != NULL)
			return al;
	}

	return NULL;
}

static bool __anyestate_browser__search_reverse(struct anyestate_browser *browser)
{
	struct anyestation_line *al;
	s64 idx;

	al = anyestate_browser__find_string_reverse(browser, browser->search_bf, &idx);
	if (al == NULL) {
		ui_helpline__puts("String yest found!");
		return false;
	}

	anyestate_browser__set_top(browser, al, idx);
	browser->searching_backwards = true;
	return true;
}

static bool anyestate_browser__search_window(struct anyestate_browser *browser,
					    int delay_secs)
{
	if (ui_browser__input_window("Search", "String: ", browser->search_bf,
				     "ENTER: OK, ESC: Cancel",
				     delay_secs * 2) != K_ENTER ||
	    !*browser->search_bf)
		return false;

	return true;
}

static bool anyestate_browser__search(struct anyestate_browser *browser, int delay_secs)
{
	if (anyestate_browser__search_window(browser, delay_secs))
		return __anyestate_browser__search(browser);

	return false;
}

static bool anyestate_browser__continue_search(struct anyestate_browser *browser,
					      int delay_secs)
{
	if (!*browser->search_bf)
		return anyestate_browser__search(browser, delay_secs);

	return __anyestate_browser__search(browser);
}

static bool anyestate_browser__search_reverse(struct anyestate_browser *browser,
					   int delay_secs)
{
	if (anyestate_browser__search_window(browser, delay_secs))
		return __anyestate_browser__search_reverse(browser);

	return false;
}

static
bool anyestate_browser__continue_search_reverse(struct anyestate_browser *browser,
					       int delay_secs)
{
	if (!*browser->search_bf)
		return anyestate_browser__search_reverse(browser, delay_secs);

	return __anyestate_browser__search_reverse(browser);
}

static int anyestate_browser__show(struct ui_browser *browser, char *title, const char *help)
{
	struct anyestate_browser *ab = container_of(browser, struct anyestate_browser, b);
	struct map_symbol *ms = browser->priv;
	struct symbol *sym = ms->sym;
	char symbol_dso[SYM_TITLE_MAX_SIZE];

	if (ui_browser__show(browser, title, help) < 0)
		return -1;

	sym_title(sym, ms->map, symbol_dso, sizeof(symbol_dso), ab->opts->percent_type);

	ui_browser__gotorc_title(browser, 0, 0);
	ui_browser__set_color(browser, HE_COLORSET_ROOT);
	ui_browser__write_nstring(browser, symbol_dso, browser->width + 1);
	return 0;
}

static void
switch_percent_type(struct anyestation_options *opts, bool base)
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

static int anyestate_browser__run(struct anyestate_browser *browser,
				 struct evsel *evsel,
				 struct hist_browser_timer *hbt)
{
	struct rb_yesde *nd = NULL;
	struct hists *hists = evsel__hists(evsel);
	struct map_symbol *ms = browser->b.priv;
	struct symbol *sym = ms->sym;
	struct anyestation *yestes = symbol__anyestation(ms->sym);
	const char *help = "Press 'h' for help on key bindings";
	int delay_secs = hbt ? hbt->refresh : 0;
	char title[256];
	int key;

	hists__scnprintf_title(hists, title, sizeof(title));
	if (anyestate_browser__show(&browser->b, title, help) < 0)
		return -1;

	anyestate_browser__calc_percent(browser, evsel);

	if (browser->curr_hot) {
		anyestate_browser__set_rb_top(browser, browser->curr_hot);
		browser->b.navkeypressed = false;
	}

	nd = browser->curr_hot;

	while (1) {
		key = ui_browser__run(&browser->b, delay_secs);

		if (delay_secs != 0) {
			anyestate_browser__calc_percent(browser, evsel);
			/*
			 * Current line focus got out of the list of most active
			 * lines, NULL it so that if TAB|UNTAB is pressed, we
			 * move to curr_hot (current hottest line).
			 */
			if (nd != NULL && RB_EMPTY_NODE(nd))
				nd = NULL;
		}

		switch (key) {
		case K_TIMER:
			if (hbt)
				hbt->timer(hbt->arg);

			if (delay_secs != 0) {
				symbol__anyestate_decay_histogram(sym, evsel->idx);
				hists__scnprintf_title(hists, title, sizeof(title));
				anyestate_browser__show(&browser->b, title, help);
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
		"q/ESC/CTRL+C  Exit\n\n"
		"ENTER         Go to target\n"
		"ESC           Exit\n"
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
		"P             Print to [symbol_name].anyestation file.\n"
		"r             Run available scripts\n"
		"p             Toggle percent type [local/global]\n"
		"b             Toggle percent base [period/hits]\n"
		"?             Search string backwards\n");
			continue;
		case 'r':
			{
				script_browse(NULL, NULL);
				continue;
			}
		case 'k':
			yestes->options->show_linenr = !yestes->options->show_linenr;
			break;
		case 'H':
			nd = browser->curr_hot;
			break;
		case 's':
			if (anyestate_browser__toggle_source(browser))
				ui_helpline__puts(help);
			continue;
		case 'o':
			yestes->options->use_offset = !yestes->options->use_offset;
			anyestation__update_column_widths(yestes);
			continue;
		case 'O':
			if (++yestes->options->offset_level > ANNOTATION__MAX_OFFSET_LEVEL)
				yestes->options->offset_level = ANNOTATION__MIN_OFFSET_LEVEL;
			continue;
		case 'j':
			yestes->options->jump_arrows = !yestes->options->jump_arrows;
			continue;
		case 'J':
			yestes->options->show_nr_jumps = !yestes->options->show_nr_jumps;
			anyestation__update_column_widths(yestes);
			continue;
		case '/':
			if (anyestate_browser__search(browser, delay_secs)) {
show_help:
				ui_helpline__puts(help);
			}
			continue;
		case 'n':
			if (browser->searching_backwards ?
			    anyestate_browser__continue_search_reverse(browser, delay_secs) :
			    anyestate_browser__continue_search(browser, delay_secs))
				goto show_help;
			continue;
		case '?':
			if (anyestate_browser__search_reverse(browser, delay_secs))
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
					   yestes->nr_asm_entries);
		}
			continue;
		case K_ENTER:
		case K_RIGHT:
		{
			struct disasm_line *dl = disasm_line(browser->selection);

			if (browser->selection == NULL)
				ui_helpline__puts("Huh? No selection. Report to linux-kernel@vger.kernel.org");
			else if (browser->selection->offset == -1)
				ui_helpline__puts("Actions are only available for assembly lines.");
			else if (!dl->ins.ops)
				goto show_sup_ins;
			else if (ins__is_ret(&dl->ins))
				goto out;
			else if (!(anyestate_browser__jump(browser, evsel, hbt) ||
				     anyestate_browser__callq(browser, evsel, hbt))) {
show_sup_ins:
				ui_helpline__puts("Actions are only available for function call/return & jump/branch instructions.");
			}
			continue;
		}
		case 'P':
			map_symbol__anyestation_dump(ms, evsel, browser->opts);
			continue;
		case 't':
			if (yestes->options->show_total_period) {
				yestes->options->show_total_period = false;
				yestes->options->show_nr_samples = true;
			} else if (yestes->options->show_nr_samples)
				yestes->options->show_nr_samples = false;
			else
				yestes->options->show_total_period = true;
			anyestation__update_column_widths(yestes);
			continue;
		case 'c':
			if (yestes->options->show_minmax_cycle)
				yestes->options->show_minmax_cycle = false;
			else
				yestes->options->show_minmax_cycle = true;
			anyestation__update_column_widths(yestes);
			continue;
		case 'p':
		case 'b':
			switch_percent_type(browser->opts, key == 'b');
			hists__scnprintf_title(hists, title, sizeof(title));
			anyestate_browser__show(&browser->b, title, help);
			continue;
		case K_LEFT:
		case K_ESC:
		case 'q':
		case CTRL('c'):
			goto out;
		default:
			continue;
		}

		if (nd != NULL)
			anyestate_browser__set_rb_top(browser, nd);
	}
out:
	ui_browser__hide(&browser->b);
	return key;
}

int map_symbol__tui_anyestate(struct map_symbol *ms, struct evsel *evsel,
			     struct hist_browser_timer *hbt,
			     struct anyestation_options *opts)
{
	return symbol__tui_anyestate(ms, evsel, hbt, opts);
}

int hist_entry__tui_anyestate(struct hist_entry *he, struct evsel *evsel,
			     struct hist_browser_timer *hbt,
			     struct anyestation_options *opts)
{
	/* reset abort key so that it can get Ctrl-C as a key */
	SLang_reset_tty();
	SLang_init_tty(0, 0, 0);

	return map_symbol__tui_anyestate(&he->ms, evsel, hbt, opts);
}

int symbol__tui_anyestate(struct map_symbol *ms, struct evsel *evsel,
			 struct hist_browser_timer *hbt,
			 struct anyestation_options *opts)
{
	struct symbol *sym = ms->sym;
	struct anyestation *yestes = symbol__anyestation(sym);
	struct anyestate_browser browser = {
		.b = {
			.refresh = anyestate_browser__refresh,
			.seek	 = ui_browser__list_head_seek,
			.write	 = anyestate_browser__write,
			.filter  = disasm_line__filter,
			.extra_title_lines = 1, /* for hists__scnprintf_title() */
			.priv	 = ms,
			.use_navkeypressed = true,
		},
		.opts = opts,
	};
	int ret = -1, err;

	if (sym == NULL)
		return -1;

	if (ms->map->dso->anyestate_warned)
		return -1;

	err = symbol__anyestate2(ms, evsel, opts, &browser.arch);
	if (err) {
		char msg[BUFSIZ];
		symbol__strerror_disassemble(ms, err, msg, sizeof(msg));
		ui__error("Couldn't anyestate %s:\n%s", sym->name, msg);
		goto out_free_offsets;
	}

	ui_helpline__push("Press ESC to exit");

	browser.b.width = yestes->max_line_len;
	browser.b.nr_entries = yestes->nr_entries;
	browser.b.entries = &yestes->src->source,
	browser.b.width += 18; /* Percentage */

	if (yestes->options->hide_src_code)
		ui_browser__init_asm_mode(&browser.b);

	ret = anyestate_browser__run(&browser, evsel, hbt);

	anyestated_source__purge(yestes->src);

out_free_offsets:
	zfree(&yestes->offsets);
	return ret;
}
