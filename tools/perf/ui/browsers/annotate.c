#include "../../util/util.h"
#include "../browser.h"
#include "../helpline.h"
#include "../libslang.h"
#include "../ui.h"
#include "../util.h"
#include "../../util/annotate.h"
#include "../../util/hist.h"
#include "../../util/sort.h"
#include "../../util/symbol.h"
#include "../../util/evsel.h"
#include <pthread.h>

struct browser_disasm_line {
	struct rb_node	rb_node;
	u32		idx;
	int		idx_asm;
	int		jump_sources;
	/*
	 * actual length of this array is saved on the nr_events field
	 * of the struct annotate_browser
	 */
	double		percent[1];
};

static struct annotate_browser_opt {
	bool hide_src_code,
	     use_offset,
	     jump_arrows,
	     show_linenr,
	     show_nr_jumps;
} annotate_browser__opts = {
	.use_offset	= true,
	.jump_arrows	= true,
};

struct annotate_browser {
	struct ui_browser b;
	struct rb_root	  entries;
	struct rb_node	  *curr_hot;
	struct disasm_line  *selection;
	struct disasm_line  **offsets;
	int		    nr_events;
	u64		    start;
	int		    nr_asm_entries;
	int		    nr_entries;
	int		    max_jump_sources;
	int		    nr_jumps;
	bool		    searching_backwards;
	u8		    addr_width;
	u8		    jumps_width;
	u8		    target_width;
	u8		    min_addr_width;
	u8		    max_addr_width;
	char		    search_bf[128];
};

static inline struct browser_disasm_line *disasm_line__browser(struct disasm_line *dl)
{
	return (struct browser_disasm_line *)(dl + 1);
}

static bool disasm_line__filter(struct ui_browser *browser __maybe_unused,
				void *entry)
{
	if (annotate_browser__opts.hide_src_code) {
		struct disasm_line *dl = list_entry(entry, struct disasm_line, node);
		return dl->offset == -1;
	}

	return false;
}

static int annotate_browser__jumps_percent_color(struct annotate_browser *browser,
						 int nr, bool current)
{
	if (current && (!browser->b.use_navkeypressed || browser->b.navkeypressed))
		return HE_COLORSET_SELECTED;
	if (nr == browser->max_jump_sources)
		return HE_COLORSET_TOP;
	if (nr > 1)
		return HE_COLORSET_MEDIUM;
	return HE_COLORSET_NORMAL;
}

static int annotate_browser__set_jumps_percent_color(struct annotate_browser *browser,
						     int nr, bool current)
{
	 int color = annotate_browser__jumps_percent_color(browser, nr, current);
	 return ui_browser__set_color(&browser->b, color);
}

static void annotate_browser__write(struct ui_browser *browser, void *entry, int row)
{
	struct annotate_browser *ab = container_of(browser, struct annotate_browser, b);
	struct disasm_line *dl = list_entry(entry, struct disasm_line, node);
	struct browser_disasm_line *bdl = disasm_line__browser(dl);
	bool current_entry = ui_browser__is_current_entry(browser, row);
	bool change_color = (!annotate_browser__opts.hide_src_code &&
			     (!current_entry || (browser->use_navkeypressed &&
					         !browser->navkeypressed)));
	int width = browser->width, printed;
	int i, pcnt_width = 7 * ab->nr_events;
	double percent_max = 0.0;
	char bf[256];

	for (i = 0; i < ab->nr_events; i++) {
		if (bdl->percent[i] > percent_max)
			percent_max = bdl->percent[i];
	}

	if (dl->offset != -1 && percent_max != 0.0) {
		for (i = 0; i < ab->nr_events; i++) {
			ui_browser__set_percent_color(browser, bdl->percent[i],
						      current_entry);
			slsmg_printf("%6.2f ", bdl->percent[i]);
		}
	} else {
		ui_browser__set_percent_color(browser, 0, current_entry);
		slsmg_write_nstring(" ", pcnt_width);
	}

	SLsmg_write_char(' ');

	/* The scroll bar isn't being used */
	if (!browser->navkeypressed)
		width += 1;

	if (!*dl->line)
		slsmg_write_nstring(" ", width - pcnt_width);
	else if (dl->offset == -1) {
		if (dl->line_nr && annotate_browser__opts.show_linenr)
			printed = scnprintf(bf, sizeof(bf), "%-*d ",
					ab->addr_width + 1, dl->line_nr);
		else
			printed = scnprintf(bf, sizeof(bf), "%*s  ",
				    ab->addr_width, " ");
		slsmg_write_nstring(bf, printed);
		slsmg_write_nstring(dl->line, width - printed - pcnt_width + 1);
	} else {
		u64 addr = dl->offset;
		int color = -1;

		if (!annotate_browser__opts.use_offset)
			addr += ab->start;

		if (!annotate_browser__opts.use_offset) {
			printed = scnprintf(bf, sizeof(bf), "%" PRIx64 ": ", addr);
		} else {
			if (bdl->jump_sources) {
				if (annotate_browser__opts.show_nr_jumps) {
					int prev;
					printed = scnprintf(bf, sizeof(bf), "%*d ",
							    ab->jumps_width,
							    bdl->jump_sources);
					prev = annotate_browser__set_jumps_percent_color(ab, bdl->jump_sources,
											 current_entry);
					slsmg_write_nstring(bf, printed);
					ui_browser__set_color(browser, prev);
				}

				printed = scnprintf(bf, sizeof(bf), "%*" PRIx64 ": ",
						    ab->target_width, addr);
			} else {
				printed = scnprintf(bf, sizeof(bf), "%*s  ",
						    ab->addr_width, " ");
			}
		}

		if (change_color)
			color = ui_browser__set_color(browser, HE_COLORSET_ADDR);
		slsmg_write_nstring(bf, printed);
		if (change_color)
			ui_browser__set_color(browser, color);
		if (dl->ins && dl->ins->ops->scnprintf) {
			if (ins__is_jump(dl->ins)) {
				bool fwd = dl->ops.target.offset > (u64)dl->offset;

				ui_browser__write_graph(browser, fwd ? SLSMG_DARROW_CHAR :
								    SLSMG_UARROW_CHAR);
				SLsmg_write_char(' ');
			} else if (ins__is_call(dl->ins)) {
				ui_browser__write_graph(browser, SLSMG_RARROW_CHAR);
				SLsmg_write_char(' ');
			} else {
				slsmg_write_nstring(" ", 2);
			}
		} else {
			if (strcmp(dl->name, "retq")) {
				slsmg_write_nstring(" ", 2);
			} else {
				ui_browser__write_graph(browser, SLSMG_LARROW_CHAR);
				SLsmg_write_char(' ');
			}
		}

		disasm_line__scnprintf(dl, bf, sizeof(bf), !annotate_browser__opts.use_offset);
		slsmg_write_nstring(bf, width - pcnt_width - 3 - printed);
	}

	if (current_entry)
		ab->selection = dl;
}

static bool disasm_line__is_valid_jump(struct disasm_line *dl, struct symbol *sym)
{
	if (!dl || !dl->ins || !ins__is_jump(dl->ins)
	    || !disasm_line__has_offset(dl)
	    || dl->ops.target.offset >= symbol__size(sym))
		return false;

	return true;
}

static void annotate_browser__draw_current_jump(struct ui_browser *browser)
{
	struct annotate_browser *ab = container_of(browser, struct annotate_browser, b);
	struct disasm_line *cursor = ab->selection, *target;
	struct browser_disasm_line *btarget, *bcursor;
	unsigned int from, to;
	struct map_symbol *ms = ab->b.priv;
	struct symbol *sym = ms->sym;
	u8 pcnt_width = 7;

	/* PLT symbols contain external offsets */
	if (strstr(sym->name, "@plt"))
		return;

	if (!disasm_line__is_valid_jump(cursor, sym))
		return;

	target = ab->offsets[cursor->ops.target.offset];
	if (!target)
		return;

	bcursor = disasm_line__browser(cursor);
	btarget = disasm_line__browser(target);

	if (annotate_browser__opts.hide_src_code) {
		from = bcursor->idx_asm;
		to = btarget->idx_asm;
	} else {
		from = (u64)bcursor->idx;
		to = (u64)btarget->idx;
	}

	pcnt_width *= ab->nr_events;

	ui_browser__set_color(browser, HE_COLORSET_CODE);
	__ui_browser__line_arrow(browser, pcnt_width + 2 + ab->addr_width,
				 from, to);
}

static unsigned int annotate_browser__refresh(struct ui_browser *browser)
{
	struct annotate_browser *ab = container_of(browser, struct annotate_browser, b);
	int ret = ui_browser__list_head_refresh(browser);
	int pcnt_width;

	pcnt_width = 7 * ab->nr_events;

	if (annotate_browser__opts.jump_arrows)
		annotate_browser__draw_current_jump(browser);

	ui_browser__set_color(browser, HE_COLORSET_NORMAL);
	__ui_browser__vline(browser, pcnt_width, 0, browser->height - 1);
	return ret;
}

static int disasm__cmp(struct browser_disasm_line *a,
		       struct browser_disasm_line *b, int nr_pcnt)
{
	int i;

	for (i = 0; i < nr_pcnt; i++) {
		if (a->percent[i] == b->percent[i])
			continue;
		return a->percent[i] < b->percent[i];
	}
	return 0;
}

static void disasm_rb_tree__insert(struct rb_root *root, struct browser_disasm_line *bdl,
				   int nr_events)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct browser_disasm_line *l;

	while (*p != NULL) {
		parent = *p;
		l = rb_entry(parent, struct browser_disasm_line, rb_node);

		if (disasm__cmp(bdl, l, nr_events))
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&bdl->rb_node, parent, p);
	rb_insert_color(&bdl->rb_node, root);
}

static void annotate_browser__set_top(struct annotate_browser *browser,
				      struct disasm_line *pos, u32 idx)
{
	unsigned back;

	ui_browser__refresh_dimensions(&browser->b);
	back = browser->b.height / 2;
	browser->b.top_idx = browser->b.index = idx;

	while (browser->b.top_idx != 0 && back != 0) {
		pos = list_entry(pos->node.prev, struct disasm_line, node);

		if (disasm_line__filter(&browser->b, &pos->node))
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
	struct browser_disasm_line *bpos;
	struct disasm_line *pos;
	u32 idx;

	bpos = rb_entry(nd, struct browser_disasm_line, rb_node);
	pos = ((struct disasm_line *)bpos) - 1;
	idx = bpos->idx;
	if (annotate_browser__opts.hide_src_code)
		idx = bpos->idx_asm;
	annotate_browser__set_top(browser, pos, idx);
	browser->curr_hot = nd;
}

static void annotate_browser__calc_percent(struct annotate_browser *browser,
					   struct perf_evsel *evsel)
{
	struct map_symbol *ms = browser->b.priv;
	struct symbol *sym = ms->sym;
	struct annotation *notes = symbol__annotation(sym);
	struct disasm_line *pos, *next;
	s64 len = symbol__size(sym);

	browser->entries = RB_ROOT;

	pthread_mutex_lock(&notes->lock);

	list_for_each_entry(pos, &notes->src->source, node) {
		struct browser_disasm_line *bpos = disasm_line__browser(pos);
		const char *path = NULL;
		double max_percent = 0.0;
		int i;

		if (pos->offset == -1) {
			RB_CLEAR_NODE(&bpos->rb_node);
			continue;
		}

		next = disasm__get_next_ip_line(&notes->src->source, pos);

		for (i = 0; i < browser->nr_events; i++) {
			bpos->percent[i] = disasm__calc_percent(notes,
						evsel->idx + i,
						pos->offset,
						next ? next->offset : len,
					        &path);

			if (max_percent < bpos->percent[i])
				max_percent = bpos->percent[i];
		}

		if (max_percent < 0.01) {
			RB_CLEAR_NODE(&bpos->rb_node);
			continue;
		}
		disasm_rb_tree__insert(&browser->entries, bpos,
				       browser->nr_events);
	}
	pthread_mutex_unlock(&notes->lock);

	browser->curr_hot = rb_last(&browser->entries);
}

static bool annotate_browser__toggle_source(struct annotate_browser *browser)
{
	struct disasm_line *dl;
	struct browser_disasm_line *bdl;
	off_t offset = browser->b.index - browser->b.top_idx;

	browser->b.seek(&browser->b, offset, SEEK_CUR);
	dl = list_entry(browser->b.top, struct disasm_line, node);
	bdl = disasm_line__browser(dl);

	if (annotate_browser__opts.hide_src_code) {
		if (bdl->idx_asm < offset)
			offset = bdl->idx;

		browser->b.nr_entries = browser->nr_entries;
		annotate_browser__opts.hide_src_code = false;
		browser->b.seek(&browser->b, -offset, SEEK_CUR);
		browser->b.top_idx = bdl->idx - offset;
		browser->b.index = bdl->idx;
	} else {
		if (bdl->idx_asm < 0) {
			ui_helpline__puts("Only available for assembly lines.");
			browser->b.seek(&browser->b, -offset, SEEK_CUR);
			return false;
		}

		if (bdl->idx_asm < offset)
			offset = bdl->idx_asm;

		browser->b.nr_entries = browser->nr_asm_entries;
		annotate_browser__opts.hide_src_code = true;
		browser->b.seek(&browser->b, -offset, SEEK_CUR);
		browser->b.top_idx = bdl->idx_asm - offset;
		browser->b.index = bdl->idx_asm;
	}

	return true;
}

static void annotate_browser__init_asm_mode(struct annotate_browser *browser)
{
	ui_browser__reset_index(&browser->b);
	browser->b.nr_entries = browser->nr_asm_entries;
}

#define SYM_TITLE_MAX_SIZE (PATH_MAX + 64)

static int sym_title(struct symbol *sym, struct map *map, char *title,
		     size_t sz)
{
	return snprintf(title, sz, "%s  %s", sym->name, map->dso->long_name);
}

static bool annotate_browser__callq(struct annotate_browser *browser,
				    struct perf_evsel *evsel,
				    struct hist_browser_timer *hbt)
{
	struct map_symbol *ms = browser->b.priv;
	struct disasm_line *dl = browser->selection;
	struct annotation *notes;
	struct addr_map_symbol target = {
		.map = ms->map,
		.addr = map__objdump_2mem(ms->map, dl->ops.target.addr),
	};
	char title[SYM_TITLE_MAX_SIZE];

	if (!ins__is_call(dl->ins))
		return false;

	if (map_groups__find_ams(&target, NULL) ||
	    map__rip_2objdump(target.map, target.map->map_ip(target.map,
							     target.addr)) !=
	    dl->ops.target.addr) {
		ui_helpline__puts("The called function was not found.");
		return true;
	}

	notes = symbol__annotation(target.sym);
	pthread_mutex_lock(&notes->lock);

	if (notes->src == NULL && symbol__alloc_hist(target.sym) < 0) {
		pthread_mutex_unlock(&notes->lock);
		ui__warning("Not enough memory for annotating '%s' symbol!\n",
			    target.sym->name);
		return true;
	}

	pthread_mutex_unlock(&notes->lock);
	symbol__tui_annotate(target.sym, target.map, evsel, hbt);
	sym_title(ms->sym, ms->map, title, sizeof(title));
	ui_browser__show_title(&browser->b, title);
	return true;
}

static
struct disasm_line *annotate_browser__find_offset(struct annotate_browser *browser,
					  s64 offset, s64 *idx)
{
	struct map_symbol *ms = browser->b.priv;
	struct symbol *sym = ms->sym;
	struct annotation *notes = symbol__annotation(sym);
	struct disasm_line *pos;

	*idx = 0;
	list_for_each_entry(pos, &notes->src->source, node) {
		if (pos->offset == offset)
			return pos;
		if (!disasm_line__filter(&browser->b, &pos->node))
			++*idx;
	}

	return NULL;
}

static bool annotate_browser__jump(struct annotate_browser *browser)
{
	struct disasm_line *dl = browser->selection;
	s64 idx;

	if (!ins__is_jump(dl->ins))
		return false;

	dl = annotate_browser__find_offset(browser, dl->ops.target.offset, &idx);
	if (dl == NULL) {
		ui_helpline__puts("Invalid jump offset");
		return true;
	}

	annotate_browser__set_top(browser, dl, idx);

	return true;
}

static
struct disasm_line *annotate_browser__find_string(struct annotate_browser *browser,
					  char *s, s64 *idx)
{
	struct map_symbol *ms = browser->b.priv;
	struct symbol *sym = ms->sym;
	struct annotation *notes = symbol__annotation(sym);
	struct disasm_line *pos = browser->selection;

	*idx = browser->b.index;
	list_for_each_entry_continue(pos, &notes->src->source, node) {
		if (disasm_line__filter(&browser->b, &pos->node))
			continue;

		++*idx;

		if (pos->line && strstr(pos->line, s) != NULL)
			return pos;
	}

	return NULL;
}

static bool __annotate_browser__search(struct annotate_browser *browser)
{
	struct disasm_line *dl;
	s64 idx;

	dl = annotate_browser__find_string(browser, browser->search_bf, &idx);
	if (dl == NULL) {
		ui_helpline__puts("String not found!");
		return false;
	}

	annotate_browser__set_top(browser, dl, idx);
	browser->searching_backwards = false;
	return true;
}

static
struct disasm_line *annotate_browser__find_string_reverse(struct annotate_browser *browser,
						  char *s, s64 *idx)
{
	struct map_symbol *ms = browser->b.priv;
	struct symbol *sym = ms->sym;
	struct annotation *notes = symbol__annotation(sym);
	struct disasm_line *pos = browser->selection;

	*idx = browser->b.index;
	list_for_each_entry_continue_reverse(pos, &notes->src->source, node) {
		if (disasm_line__filter(&browser->b, &pos->node))
			continue;

		--*idx;

		if (pos->line && strstr(pos->line, s) != NULL)
			return pos;
	}

	return NULL;
}

static bool __annotate_browser__search_reverse(struct annotate_browser *browser)
{
	struct disasm_line *dl;
	s64 idx;

	dl = annotate_browser__find_string_reverse(browser, browser->search_bf, &idx);
	if (dl == NULL) {
		ui_helpline__puts("String not found!");
		return false;
	}

	annotate_browser__set_top(browser, dl, idx);
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

static void annotate_browser__update_addr_width(struct annotate_browser *browser)
{
	if (annotate_browser__opts.use_offset)
		browser->target_width = browser->min_addr_width;
	else
		browser->target_width = browser->max_addr_width;

	browser->addr_width = browser->target_width;

	if (annotate_browser__opts.show_nr_jumps)
		browser->addr_width += browser->jumps_width + 1;
}

static int annotate_browser__run(struct annotate_browser *browser,
				 struct perf_evsel *evsel,
				 struct hist_browser_timer *hbt)
{
	struct rb_node *nd = NULL;
	struct map_symbol *ms = browser->b.priv;
	struct symbol *sym = ms->sym;
	const char *help = "Press 'h' for help on key bindings";
	int delay_secs = hbt ? hbt->refresh : 0;
	int key;
	char title[SYM_TITLE_MAX_SIZE];

	sym_title(sym, ms->map, title, sizeof(title));
	if (ui_browser__show(&browser->b, title, help) < 0)
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

			if (delay_secs != 0)
				symbol__annotate_decay_histogram(sym, evsel->idx);
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
			if (nd != NULL)
				nd = rb_next(nd);
				if (nd == NULL)
					nd = rb_first(&browser->entries);
			else
				nd = browser->curr_hot;
			break;
		case K_F1:
		case 'h':
			ui_browser__help_window(&browser->b,
		"UP/DOWN/PGUP\n"
		"PGDN/SPACE    Navigate\n"
		"q/ESC/CTRL+C  Exit\n\n"
		"->            Go to target\n"
		"<-            Exit\n"
		"H             Cycle thru hottest instructions\n"
		"j             Toggle showing jump to target arrows\n"
		"J             Toggle showing number of jump sources on targets\n"
		"n             Search next string\n"
		"o             Toggle disassembler output/simplified view\n"
		"s             Toggle source code view\n"
		"/             Search string\n"
		"k             Toggle line numbers\n"
		"r             Run available scripts\n"
		"?             Search string backwards\n");
			continue;
		case 'r':
			{
				script_browse(NULL);
				continue;
			}
		case 'k':
			annotate_browser__opts.show_linenr =
				!annotate_browser__opts.show_linenr;
			break;
		case 'H':
			nd = browser->curr_hot;
			break;
		case 's':
			if (annotate_browser__toggle_source(browser))
				ui_helpline__puts(help);
			continue;
		case 'o':
			annotate_browser__opts.use_offset = !annotate_browser__opts.use_offset;
			annotate_browser__update_addr_width(browser);
			continue;
		case 'j':
			annotate_browser__opts.jump_arrows = !annotate_browser__opts.jump_arrows;
			continue;
		case 'J':
			annotate_browser__opts.show_nr_jumps = !annotate_browser__opts.show_nr_jumps;
			annotate_browser__update_addr_width(browser);
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
					   browser->nr_asm_entries);
		}
			continue;
		case K_ENTER:
		case K_RIGHT:
			if (browser->selection == NULL)
				ui_helpline__puts("Huh? No selection. Report to linux-kernel@vger.kernel.org");
			else if (browser->selection->offset == -1)
				ui_helpline__puts("Actions are only available for assembly lines.");
			else if (!browser->selection->ins) {
				if (strcmp(browser->selection->name, "retq"))
					goto show_sup_ins;
				goto out;
			} else if (!(annotate_browser__jump(browser) ||
				     annotate_browser__callq(browser, evsel, hbt))) {
show_sup_ins:
				ui_helpline__puts("Actions are only available for 'callq', 'retq' & jump instructions.");
			}
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
			     struct hist_browser_timer *hbt)
{
	return symbol__tui_annotate(ms->sym, ms->map, evsel, hbt);
}

int hist_entry__tui_annotate(struct hist_entry *he, struct perf_evsel *evsel,
			     struct hist_browser_timer *hbt)
{
	return map_symbol__tui_annotate(&he->ms, evsel, hbt);
}

static void annotate_browser__mark_jump_targets(struct annotate_browser *browser,
						size_t size)
{
	u64 offset;
	struct map_symbol *ms = browser->b.priv;
	struct symbol *sym = ms->sym;

	/* PLT symbols contain external offsets */
	if (strstr(sym->name, "@plt"))
		return;

	for (offset = 0; offset < size; ++offset) {
		struct disasm_line *dl = browser->offsets[offset], *dlt;
		struct browser_disasm_line *bdlt;

		if (!disasm_line__is_valid_jump(dl, sym))
			continue;

		dlt = browser->offsets[dl->ops.target.offset];
		/*
 		 * FIXME: Oops, no jump target? Buggy disassembler? Or do we
 		 * have to adjust to the previous offset?
 		 */
		if (dlt == NULL)
			continue;

		bdlt = disasm_line__browser(dlt);
		if (++bdlt->jump_sources > browser->max_jump_sources)
			browser->max_jump_sources = bdlt->jump_sources;

		++browser->nr_jumps;
	}
}

static inline int width_jumps(int n)
{
	if (n >= 100)
		return 5;
	if (n / 10)
		return 2;
	return 1;
}

int symbol__tui_annotate(struct symbol *sym, struct map *map,
			 struct perf_evsel *evsel,
			 struct hist_browser_timer *hbt)
{
	struct disasm_line *pos, *n;
	struct annotation *notes;
	size_t size;
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
			.priv	 = &ms,
			.use_navkeypressed = true,
		},
	};
	int ret = -1;
	int nr_pcnt = 1;
	size_t sizeof_bdl = sizeof(struct browser_disasm_line);

	if (sym == NULL)
		return -1;

	size = symbol__size(sym);

	if (map->dso->annotate_warned)
		return -1;

	browser.offsets = zalloc(size * sizeof(struct disasm_line *));
	if (browser.offsets == NULL) {
		ui__error("Not enough memory!");
		return -1;
	}

	if (perf_evsel__is_group_event(evsel)) {
		nr_pcnt = evsel->nr_members;
		sizeof_bdl += sizeof(double) * (nr_pcnt - 1);
	}

	if (symbol__annotate(sym, map, sizeof_bdl) < 0) {
		ui__error("%s", ui_helpline__last_msg);
		goto out_free_offsets;
	}

	ui_helpline__push("Press <- or ESC to exit");

	notes = symbol__annotation(sym);
	browser.start = map__rip_2objdump(map, sym->start);

	list_for_each_entry(pos, &notes->src->source, node) {
		struct browser_disasm_line *bpos;
		size_t line_len = strlen(pos->line);

		if (browser.b.width < line_len)
			browser.b.width = line_len;
		bpos = disasm_line__browser(pos);
		bpos->idx = browser.nr_entries++;
		if (pos->offset != -1) {
			bpos->idx_asm = browser.nr_asm_entries++;
			/*
			 * FIXME: short term bandaid to cope with assembly
			 * routines that comes with labels in the same column
			 * as the address in objdump, sigh.
			 *
			 * E.g. copy_user_generic_unrolled
 			 */
			if (pos->offset < (s64)size)
				browser.offsets[pos->offset] = pos;
		} else
			bpos->idx_asm = -1;
	}

	annotate_browser__mark_jump_targets(&browser, size);

	browser.addr_width = browser.target_width = browser.min_addr_width = hex_width(size);
	browser.max_addr_width = hex_width(sym->end);
	browser.jumps_width = width_jumps(browser.max_jump_sources);
	browser.nr_events = nr_pcnt;
	browser.b.nr_entries = browser.nr_entries;
	browser.b.entries = &notes->src->source,
	browser.b.width += 18; /* Percentage */

	if (annotate_browser__opts.hide_src_code)
		annotate_browser__init_asm_mode(&browser);

	annotate_browser__update_addr_width(&browser);

	ret = annotate_browser__run(&browser, evsel, hbt);
	list_for_each_entry_safe(pos, n, &notes->src->source, node) {
		list_del(&pos->node);
		disasm_line__free(pos);
	}

out_free_offsets:
	free(browser.offsets);
	return ret;
}

#define ANNOTATE_CFG(n) \
	{ .name = #n, .value = &annotate_browser__opts.n, }

/*
 * Keep the entries sorted, they are bsearch'ed
 */
static struct annotate_config {
	const char *name;
	bool *value;
} annotate__configs[] = {
	ANNOTATE_CFG(hide_src_code),
	ANNOTATE_CFG(jump_arrows),
	ANNOTATE_CFG(show_linenr),
	ANNOTATE_CFG(show_nr_jumps),
	ANNOTATE_CFG(use_offset),
};

#undef ANNOTATE_CFG

static int annotate_config__cmp(const void *name, const void *cfgp)
{
	const struct annotate_config *cfg = cfgp;

	return strcmp(name, cfg->name);
}

static int annotate__config(const char *var, const char *value,
			    void *data __maybe_unused)
{
	struct annotate_config *cfg;
	const char *name;

	if (prefixcmp(var, "annotate.") != 0)
		return 0;

	name = var + 9;
	cfg = bsearch(name, annotate__configs, ARRAY_SIZE(annotate__configs),
		      sizeof(struct annotate_config), annotate_config__cmp);

	if (cfg == NULL)
		return -1;

	*cfg->value = perf_config_bool(name, value);
	return 0;
}

void annotate_browser__init(void)
{
	perf_config(annotate__config, NULL);
}
