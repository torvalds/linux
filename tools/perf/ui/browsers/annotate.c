// SPDX-License-Identifier: GPL-2.0
#include "../../util/util.h"
#include "../browser.h"
#include "../helpline.h"
#include "../ui.h"
#include "../util.h"
#include "../../util/annotate.h"
#include "../../util/hist.h"
#include "../../util/sort.h"
#include "../../util/symbol.h"
#include "../../util/evsel.h"
#include "../../util/evlist.h"
#include <inttypes.h>
#include <pthread.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <sys/ttydefaults.h>
#include <asm/bug.h>

struct disasm_line_samples {
	double		      percent;
	struct sym_hist_entry he;
};

struct arch;

struct annotate_browser {
	struct ui_browser	    b;
	struct rb_root		    entries;
	struct rb_node		   *curr_hot;
	struct annotation_line	   *selection;
	struct arch		   *arch;
	struct annotation_options  *opts;
	bool			    searching_backwards;
	char			    search_bf[128];
};

static inline struct annotation *browser__annotation(struct ui_browser *browser)
{
	struct map_symbol *ms = browser->priv;
	return symbol__annotation(ms->sym);
}

static bool disasm_line__filter(struct ui_browser *browser, void *entry)
{
	struct annotation *notes = browser__annotation(browser);
	struct annotation_line *al = list_entry(entry, struct annotation_line, node);
	return annotation_line__filter(al, notes);
}

static int ui_browser__jumps_percent_color(struct ui_browser *browser, int nr, bool current)
{
	struct annotation *notes = browser__annotation(browser);

	if (current && (!browser->use_navkeypressed || browser->navkeypressed))
		return HE_COLORSET_SELECTED;
	if (nr == notes->max_jump_sources)
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

static int annotate_browser__set_color(void *browser, int color)
{
	return ui_browser__set_color(browser, color);
}

static void annotate_browser__write_graph(void *browser, int graph)
{
	ui_browser__write_graph(browser, graph);
}

static void annotate_browser__set_percent_color(void *browser, double percent, bool current)
{
	ui_browser__set_percent_color(browser, percent, current);
}

static void annotate_browser__printf(void *browser, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	ui_browser__vprintf(browser, fmt, args);
	va_end(args);
}

static void annotate_browser__write(struct ui_browser *browser, void *entry, int row)
{
	struct annotate_browser *ab = container_of(browser, struct annotate_browser, b);
	struct annotation *notes = browser__annotation(browser);
	struct annotation_line *al = list_entry(entry, struct annotation_line, node);
	struct annotation_write_ops ops = {
		.first_line		 = row == 0,
		.current_entry		 = ui_browser__is_current_entry(browser, row),
		.change_color		 = (!notes->options->hide_src_code &&
					    (!ops.current_entry ||
					     (browser->use_navkeypressed &&
					      !browser->navkeypressed))),
		.width			 = browser->width,
		.obj			 = browser,
		.set_color		 = annotate_browser__set_color,
		.set_percent_color	 = annotate_browser__set_percent_color,
		.set_jumps_percent_color = ui_browser__set_jumps_percent_color,
		.printf			 = annotate_browser__printf,
		.write_graph		 = annotate_browser__write_graph,
	};

	/* The scroll bar isn't being used */
	if (!browser->navkeypressed)
		ops.width += 1;

	annotation_line__write(al, notes, &ops, ab->opts);

	if (ops.current_entry)
		ab->selection = al;
}

static bool is_fused(struct annotate_browser *ab, struct disasm_line *cursor)
{
	struct disasm_line *pos = list_prev_entry(cursor, al.node);
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

static void annotate_browser__draw_current_jump(struct ui_browser *browser)
{
	struct annotate_browser *ab = container_of(browser, struct annotate_browser, b);
	struct disasm_line *cursor = disasm_line(ab->selection);
	struct annotation_line *target;
	unsigned int from, to;
	struct map_symbol *ms = ab->b.priv;
	struct symbol *sym = ms->sym;
	struct annotation *notes = symbol__annotation(sym);
	u8 pcnt_width = annotation__pcnt_width(notes);
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
	 *  I.e. jumps to another function, outside _cpp_lex_token, which
	 *  are not being correctly handled generating as a side effect references
	 *  to ab->offset[] entries that are set to NULL, so to make this code
	 *  more robust, check that here.
	 *
	 *  A proper fix for will be put in place, looking at the function
	 *  name right after the '<' token and probably treating this like a
	 *  'call' instruction.
	 */
	target = notes->offsets[cursor->ops.target.offset];
	if (target == NULL) {
		ui_helpline__printf("WARN: jump target inconsistency, press 'o', notes->offsets[%#x] = NULL\n",
				    cursor->ops.target.offset);
		return;
	}

	if (notes->options->hide_src_code) {
		from = cursor->al.idx_asm;
		to = target->idx_asm;
	} else {
		from = (u64)cursor->al.idx;
		to = (u64)target->idx;
	}

	width = annotation__cycles_width(notes);

	ui_browser__set_color(browser, HE_COLORSET_JUMP_ARROWS);
	__ui_browser__line_arrow(browser,
				 pcnt_width + 2 + notes->widths.addr + width,
				 from, to);

	if (is_fused(ab, cursor)) {
		ui_browser__mark_fused(browser,
				       pcnt_width + 3 + notes->widths.addr + width,
				       from - 1,
				       to > from ? true : false);
	}
}

static unsigned int annotate_browser__refresh(struct ui_browser *browser)
{
	struct annotation *notes = browser__annotation(browser);
	int ret = ui_browser__list_head_refresh(browser);
	int pcnt_width = annotation__pcnt_width(notes);

	if (notes->options->jump_arrows)
		annotate_browser__draw_current_jump(browser);

	ui_browser__set_color(browser, HE_COLORSET_NORMAL);
	__ui_browser__vline(browser, pcnt_width, 0, browser->rows - 1);
	return ret;
}

static int disasm__cmp(struct annotation_line *a, struct annotation_line *b)
{
	int i;

	for (i = 0; i < a->data_nr; i++) {
		if (a->data[i].percent == b->data[i].percent)
			continue;
		return a->data[i].percent < b->data[i].percent;
	}
	return 0;
}

static void disasm_rb_tree__insert(struct rb_root *root, struct annotation_line *al)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct annotation_line *l;

	while (*p != NULL) {
		parent = *p;
		l = rb_entry(parent, struct annotation_line, rb_node);

		if (disasm__cmp(al, l))
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&al->rb_node, parent, p);
	rb_insert_color(&al->rb_node, root);
}

static void annotate_browser__set_top(struct annotate_browser *browser,
				      struct annotation_line *pos, u32 idx)
{
	struct annotation *notes = browser__annotation(&browser->b);
	unsigned back;

	ui_browser__refresh_dimensions(&browser->b);
	back = browser->b.height / 2;
	browser->b.top_idx = browser->b.index = idx;

	while (browser->b.top_idx != 0 && back != 0) {
		pos = list_entry(pos->node.prev, struct annotation_line, node);

		if (annotation_line__filter(pos, notes))
			continue;

		--browser->b.top_idx;
		--back;
	}

	browser->b.top = pos;
	browser->b.navkeypressed = true;
}

static void annotate_browser__set_rb_top(struct annotate_browser *browser,
					 struct rb_node *nd)
{
	struct annotation *notes = browser__annotation(&browser->b);
	struct annotation_line * pos = rb_entry(nd, struct annotation_line, rb_node);
	u32 idx = pos->idx;

	if (notes->options->hide_src_code)
		idx = pos->idx_asm;
	annotate_browser__set_top(browser, pos, idx);
	browser->curr_hot = nd;
}

static void annotate_browser__calc_percent(struct annotate_browser *browser,
					   struct perf_evsel *evsel)
{
	struct map_symbol *ms = browser->b.priv;
	struct symbol *sym = ms->sym;
	struct annotation *notes = symbol__annotation(sym);
	struct disasm_line *pos;

	browser->entries = RB_ROOT;

	pthread_mutex_lock(&notes->lock);

	symbol__calc_percent(sym, evsel);

	list_for_each_entry(pos, &notes->src->source, al.node) {
		double max_percent = 0.0;
		int i;

		if (pos->al.offset == -1) {
			RB_CLEAR_NODE(&pos->al.rb_node);
			continue;
		}

		for (i = 0; i < pos->al.data_nr; i++) {
			double percent;

			percent = annotation_data__percent(&pos->al.data[i],
							   browser->opts->percent_type);

			if (max_percent < percent)
				max_percent = percent;
		}

		if (max_percent < 0.01 && pos->al.ipc == 0) {
			RB_CLEAR_NODE(&pos->al.rb_node);
			continue;
		}
		disasm_rb_tree__insert(&browser->entries, &pos->al);
	}
	pthread_mutex_unlock(&notes->lock);

	browser->curr_hot = rb_last(&browser->entries);
}

static bool annotate_browser__toggle_source(struct annotate_browser *browser)
{
	struct annotation *notes = browser__annotation(&browser->b);
	struct annotation_line *al;
	off_t offset = browser->b.index - browser->b.top_idx;

	browser->b.seek(&browser->b, offset, SEEK_CUR);
	al = list_entry(browser->b.top, struct annotation_line, node);

	if (notes->options->hide_src_code) {
		if (al->idx_asm < offset)
			offset = al->idx;

		browser->b.nr_entries = notes->nr_entries;
		notes->options->hide_src_code = false;
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

		browser->b.nr_entries = notes->nr_asm_entries;
		notes->options->hide_src_code = true;
		browser->b.seek(&browser->b, -offset, SEEK_CUR);
		browser->b.top_idx = al->idx_asm - offset;
		browser->b.index = al->idx_asm;
	}

	return true;
}

static void ui_browser__init_asm_mode(struct ui_browser *browser)
{
	struct annotation *notes = browser__annotation(browser);
	ui_browser__reset_index(browser);
	browser->nr_entries = notes->nr_asm_entries;
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
 * to another, like from the kernel's entry_SYSCALL_64 function to the
 * swapgs_restore_regs_and_return_to_usermode() function.
 *
 * So all we check here is that dl->ops.target.sym is set, if it is, just
 * go to that function and when exiting from its disassembly, come back
 * to the calling function.
 */
static bool annotate_browser__callq(struct annotate_browser *browser,
				    struct perf_evsel *evsel,
				    struct hist_browser_timer *hbt)
{
	struct map_symbol *ms = browser->b.priv;
	struct disasm_line *dl = disasm_line(browser->selection);
	struct annotation *notes;
	char title[SYM_TITLE_MAX_SIZE];

	if (!dl->ops.target.sym) {
		ui_helpline__puts("The called function was not found.");
		return true;
	}

	notes = symbol__annotation(dl->ops.target.sym);
	pthread_mutex_lock(&notes->lock);

	if (!symbol__hists(dl->ops.target.sym, evsel->evlist->nr_entries)) {
		pthread_mutex_unlock(&notes->lock);
		ui__warning("Not enough memory for annotating '%s' symbol!\n",
			    dl->ops.target.sym->name);
		return true;
	}

	pthread_mutex_unlock(&notes->lock);
	symbol__tui_annotate(dl->ops.target.sym, ms->map, evsel, hbt, browser->opts);
	sym_title(ms->sym, ms->map, title, sizeof(title), browser->opts->percent_type);
	ui_browser__show_title(&browser->b, title);
	return true;
}

static
struct disasm_line *annotate_browser__find_offset(struct annotate_browser *browser,
					  s64 offset, s64 *idx)
{
	struct annotation *notes = browser__annotation(&browser->b);
	struct disasm_line *pos;

	*idx = 0;
	list_for_each_entry(pos, &notes->src->source, al.node) {
		if (pos->al.offset == offset)
			return pos;
		if (!annotation_line__filter(&pos->al, notes))
			++*idx;
	}

	return NULL;
}

static bool annotate_browser__jump(struct annotate_browser *browser,
				   struct perf_evsel *evsel,
				   struct hist_browser_timer *hbt)
{
	struct disasm_line *dl = disasm_line(browser->selection);
	u64 offset;
	s64 idx;

	if (!ins__is_jump(&dl->ins))
		return false;

	if (dl->ops.target.outside) {
		annotate_browser__callq(browser, evsel, hbt);
		return true;
	}

	offset = dl->ops.target.offset;
	dl = annotate_browser__find_offset(browser, offset, &idx);
	if (dl == NULL) {
		ui_helpline__printf("Invalid jump offset: %" PRIx64, offset);
		return true;
	}

	annotate_browser__set_top(browser, &dl->al, idx);

	return true;
}

static
struct annotation_line *annotate_browser__find_string(struct annotate_browser *browser,
					  char *s, s64 *idx)
{
	struct annotation *notes = browser__annotation(&browser->b);
	struct annotation_line *al = browser->selection;

	*idx = browser->b.index;
	list_for_each_entry_continue(al, &notes->src->source, node) {
		if (annotation_line__filter(al, notes))
			continue;

		++*idx;

		if (al->line && strstr(al->line, s) != NULL)
			return al;
	}

	return NULL;
}

static bool __annotate_browser__search(struct annotate_browser *browser)
{
	struct annotation_line *al;
	s64 idx;

	al = annotate_browser__find_string(browser, browser->search_bf, &idx);
	if (al == NULL) {
		ui_helpline__puts("String not found!");
		return false;
	}

	annotate_browser__set_top(browser, al, idx);
	browser->searching_backwards = false;
	return true;
}

static
struct annotation_line *annotate_browser__find_string_reverse(struct annotate_browser *browser,
						  char *s, s64 *idx)
{
	struct annotation *notes = browser__annotation(&browser->b);
	struct annotation_line *al = browser->selection;

	*idx = browser->b.index;
	list_for_each_entry_continue_reverse(al, &notes->src->source, node) {
		if (annotation_line__filter(al, notes))
			continue;

		--*idx;

		if (al->line && strstr(al->line, s) != NULL)
			return al;
	}

	return NULL;
}

static bool __annotate_browser__search_reverse(struct annotate_browser *browser)
{
	struct annotation_line *al;
	s64 idx;

	al = annotate_browser__find_string_reverse(browser, browser->search_bf, &idx);
	if (al == NULL) {
		ui_helpline__puts("String not found!");
		return false;
	}

	annotate_browser__set_top(browser, al, idx);
	browser->searching_backwards = true;
	return true;
}

static bool annotate_browser__search_window(struct annotate_browser *browser,
					    int delay_secs)
{
	if (ui_browser__input_window("Search", "String: ", browser->search_bf,
				     "ENTER: OK, ESC: Cancel",
				     delay_secs * 2) != K_ENTER ||
	    !*browser->search_bf)
		return false;

	return true;
}

static bool annotate_browser__search(struct annotate_browser *browser, int delay_secs)
{
	if (annotate_browser__search_window(browser, delay_secs))
		return __annotate_browser__search(browser);

	return false;
}

static bool annotate_browser__continue_search(struct annotate_browser *browser,
					      int delay_secs)
{
	if (!*browser->search_bf)
		return annotate_browser__search(browser, delay_secs);

	return __annotate_browser__search(browser);
}

static bool annotate_browser__search_reverse(struct annotate_browser *browser,
					   int delay_secs)
{
	if (annotate_browser__search_window(browser, delay_secs))
		return __annotate_browser__search_reverse(browser);

	return false;
}

static
bool annotate_browser__continue_search_reverse(struct annotate_browser *browser,
					       int delay_secs)
{
	if (!*browser->search_bf)
		return annotate_browser__search_reverse(browser, delay_secs);

	return __annotate_browser__search_reverse(browser);
}

static int annotate_browser__show(struct ui_browser *browser, char *title, const char *help)
{
	struct annotate_browser *ab = container_of(browser, struct annotate_browser, b);
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
switch_percent_type(struct annotation_options *opts, bool base)
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

static int annotate_browser__run(struct annotate_browser *browser,
				 struct perf_evsel *evsel,
				 struct hist_browser_timer *hbt)
{
	struct rb_node *nd = NULL;
	struct hists *hists = evsel__hists(evsel);
	struct map_symbol *ms = browser->b.priv;
	struct symbol *sym = ms->sym;
	struct annotation *notes = symbol__annotation(ms->sym);
	const char *help = "Press 'h' for help on key bindings";
	int delay_secs = hbt ? hbt->refresh : 0;
	char title[256];
	int key;

	hists__scnprintf_title(hists, title, sizeof(title));
	if (annotate_browser__show(&browser->b, title, help) < 0)
		return -1;

	annotate_browser__calc_percent(browser, evsel);

	if (browser->curr_hot) {
		annotate_browser__set_rb_top(browser, browser->curr_hot);
		browser->b.navkeypressed = false;
	}

	nd = browser->curr_hot;

	while (1) {
		key = ui_browser__run(&browser->b, delay_secs);

		if (delay_secs != 0) {
			annotate_browser__calc_percent(browser, evsel);
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
				symbol__annotate_decay_histogram(sym, evsel->idx);
				hists__scnprintf_title(hists, title, sizeof(title));
				annotate_browser__show(&browser->b, title, help);
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
		"P             Print to [symbol_name].annotation file.\n"
		"r             Run available scripts\n"
		"p             Toggle percent type [local/global]\n"
		"b             Toggle percent base [period/hits]\n"
		"?             Search string backwards\n");
			continue;
		case 'r':
			{
				script_browse(NULL);
				continue;
			}
		case 'k':
			notes->options->show_linenr = !notes->options->show_linenr;
			break;
		case 'H':
			nd = browser->curr_hot;
			break;
		case 's':
			if (annotate_browser__toggle_source(browser))
				ui_helpline__puts(help);
			continue;
		case 'o':
			notes->options->use_offset = !notes->options->use_offset;
			annotation__update_column_widths(notes);
			continue;
		case 'O':
			if (++notes->options->offset_level > ANNOTATION__MAX_OFFSET_LEVEL)
				notes->options->offset_level = ANNOTATION__MIN_OFFSET_LEVEL;
			continue;
		case 'j':
			notes->options->jump_arrows = !notes->options->jump_arrows;
			continue;
		case 'J':
			notes->options->show_nr_jumps = !notes->options->show_nr_jumps;
			annotation__update_column_widths(notes);
			continue;
		case '/':
			if (annotate_browser__search(browser, delay_secs)) {
show_help:
				ui_helpline__puts(help);
			}
			continue;
		case 'n':
			if (browser->searching_backwards ?
			    annotate_browser__continue_search_reverse(browser, delay_secs) :
			    annotate_browser__continue_search(browser, delay_secs))
				goto show_help;
			continue;
		case '?':
			if (annotate_browser__search_reverse(browser, delay_secs))
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
					   notes->nr_asm_entries);
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
			else if (!(annotate_browser__jump(browser, evsel, hbt) ||
				     annotate_browser__callq(browser, evsel, hbt))) {
show_sup_ins:
				ui_helpline__puts("Actions are only available for function call/return & jump/branch instructions.");
			}
			continue;
		}
		case 'P':
			map_symbol__annotation_dump(ms, evsel, browser->opts);
			continue;
		case 't':
			if (notes->options->show_total_period) {
				notes->options->show_total_period = false;
				notes->options->show_nr_samples = true;
			} else if (notes->options->show_nr_samples)
				notes->options->show_nr_samples = false;
			else
				notes->options->show_total_period = true;
			annotation__update_column_widths(notes);
			continue;
		case 'c':
			if (notes->options->show_minmax_cycle)
				notes->options->show_minmax_cycle = false;
			else
				notes->options->show_minmax_cycle = true;
			annotation__update_column_widths(notes);
			continue;
		case 'p':
		case 'b':
			switch_percent_type(browser->opts, key == 'b');
			hists__scnprintf_title(hists, title, sizeof(title));
			annotate_browser__show(&browser->b, title, help);
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
			annotate_browser__set_rb_top(browser, nd);
	}
out:
	ui_browser__hide(&browser->b);
	return key;
}

int map_symbol__tui_annotate(struct map_symbol *ms, struct perf_evsel *evsel,
			     struct hist_browser_timer *hbt,
			     struct annotation_options *opts)
{
	return symbol__tui_annotate(ms->sym, ms->map, evsel, hbt, opts);
}

int hist_entry__tui_annotate(struct hist_entry *he, struct perf_evsel *evsel,
			     struct hist_browser_timer *hbt,
			     struct annotation_options *opts)
{
	/* reset abort key so that it can get Ctrl-C as a key */
	SLang_reset_tty();
	SLang_init_tty(0, 0, 0);

	return map_symbol__tui_annotate(&he->ms, evsel, hbt, opts);
}

int symbol__tui_annotate(struct symbol *sym, struct map *map,
			 struct perf_evsel *evsel,
			 struct hist_browser_timer *hbt,
			 struct annotation_options *opts)
{
	struct annotation *notes = symbol__annotation(sym);
	struct map_symbol ms = {
		.map = map,
		.sym = sym,
	};
	struct annotate_browser browser = {
		.b = {
			.refresh = annotate_browser__refresh,
			.seek	 = ui_browser__list_head_seek,
			.write	 = annotate_browser__write,
			.filter  = disasm_line__filter,
			.extra_title_lines = 1, /* for hists__scnprintf_title() */
			.priv	 = &ms,
			.use_navkeypressed = true,
		},
		.opts = opts,
	};
	int ret = -1, err;

	if (sym == NULL)
		return -1;

	if (map->dso->annotate_warned)
		return -1;

	err = symbol__annotate2(sym, map, evsel, opts, &browser.arch);
	if (err) {
		char msg[BUFSIZ];
		symbol__strerror_disassemble(sym, map, err, msg, sizeof(msg));
		ui__error("Couldn't annotate %s:\n%s", sym->name, msg);
		goto out_free_offsets;
	}

	ui_helpline__push("Press ESC to exit");

	browser.b.width = notes->max_line_len;
	browser.b.nr_entries = notes->nr_entries;
	browser.b.entries = &notes->src->source,
	browser.b.width += 18; /* Percentage */

	if (notes->options->hide_src_code)
		ui_browser__init_asm_mode(&browser.b);

	ret = annotate_browser__run(&browser, evsel, hbt);

	annotated_source__purge(notes->src);

out_free_offsets:
	zfree(&notes->offsets);
	return ret;
}
