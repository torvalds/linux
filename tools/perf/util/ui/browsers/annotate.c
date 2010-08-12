#include "../browser.h"
#include "../helpline.h"
#include "../libslang.h"
#include "../../hist.h"
#include "../../sort.h"
#include "../../symbol.h"

static void ui__error_window(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	newtWinMessagev((char *)"Error", (char *)"Ok", (char *)fmt, ap);
	va_end(ap);
}

struct annotate_browser {
	struct ui_browser b;
	struct rb_root	  entries;
	struct rb_node	  *curr_hot;
};

struct objdump_line_rb_node {
	struct rb_node	rb_node;
	double		percent;
	u32		idx;
};

static inline
struct objdump_line_rb_node *objdump_line__rb(struct objdump_line *self)
{
	return (struct objdump_line_rb_node *)(self + 1);
}

static void annotate_browser__write(struct ui_browser *self, void *entry, int row)
{
	struct objdump_line *ol = rb_entry(entry, struct objdump_line, node);
	bool current_entry = ui_browser__is_current_entry(self, row);
	int width = self->width;

	if (ol->offset != -1) {
		struct objdump_line_rb_node *olrb = objdump_line__rb(ol);
		ui_browser__set_percent_color(self, olrb->percent, current_entry);
		slsmg_printf(" %7.2f ", olrb->percent);
		if (!current_entry)
			ui_browser__set_color(self, HE_COLORSET_CODE);
	} else {
		ui_browser__set_percent_color(self, 0, current_entry);
		slsmg_write_nstring(" ", 9);
	}

	SLsmg_write_char(':');
	slsmg_write_nstring(" ", 8);
	if (!*ol->line)
		slsmg_write_nstring(" ", width - 18);
	else
		slsmg_write_nstring(ol->line, width - 18);
}

static double objdump_line__calc_percent(struct objdump_line *self,
					 struct list_head *head,
					 struct symbol *sym)
{
	double percent = 0.0;

	if (self->offset != -1) {
		int len = sym->end - sym->start;
		unsigned int hits = 0;
		struct sym_priv *priv = symbol__priv(sym);
		struct sym_ext *sym_ext = priv->ext;
		struct sym_hist *h = priv->hist;
		s64 offset = self->offset;
		struct objdump_line *next = objdump__get_next_ip_line(head, self);


		while (offset < (s64)len &&
		       (next == NULL || offset < next->offset)) {
			if (sym_ext) {
				percent += sym_ext[offset].percent;
			} else
				hits += h->ip[offset];

			++offset;
		}

		if (sym_ext == NULL && h->sum)
			percent = 100.0 * hits / h->sum;
	}

	return percent;
}

static void objdump__insert_line(struct rb_root *self,
				 struct objdump_line_rb_node *line)
{
	struct rb_node **p = &self->rb_node;
	struct rb_node *parent = NULL;
	struct objdump_line_rb_node *l;

	while (*p != NULL) {
		parent = *p;
		l = rb_entry(parent, struct objdump_line_rb_node, rb_node);
		if (line->percent < l->percent)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&line->rb_node, parent, p);
	rb_insert_color(&line->rb_node, self);
}

static void annotate_browser__set_top(struct annotate_browser *self,
				      struct rb_node *nd)
{
	struct objdump_line_rb_node *rbpos;
	struct objdump_line *pos;
	unsigned back;

	ui_browser__refresh_dimensions(&self->b);
	back = self->b.height / 2;
	rbpos = rb_entry(nd, struct objdump_line_rb_node, rb_node);
	pos = ((struct objdump_line *)rbpos) - 1;
	self->b.top_idx = self->b.index = rbpos->idx;

	while (self->b.top_idx != 0 && back != 0) {
		pos = list_entry(pos->node.prev, struct objdump_line, node);

		--self->b.top_idx;
		--back;
	}

	self->b.top = pos;
	self->curr_hot = nd;
}

static int annotate_browser__run(struct annotate_browser *self)
{
	struct rb_node *nd;
	struct hist_entry *he = self->b.priv;
	int key;

	if (ui_browser__show(&self->b, he->ms.sym->name,
			     "<-, -> or ESC: exit, TAB/shift+TAB: cycle thru samples") < 0)
		return -1;
	/*
	 * To allow builtin-annotate to cycle thru multiple symbols by
	 * examining the exit key for this function.
	 */
	ui_browser__add_exit_key(&self->b, NEWT_KEY_RIGHT);

	nd = self->curr_hot;
	if (nd) {
		int tabs[] = { NEWT_KEY_TAB, NEWT_KEY_UNTAB, 0 };
		ui_browser__add_exit_keys(&self->b, tabs);
	}

	while (1) {
		key = ui_browser__run(&self->b);

		switch (key) {
		case NEWT_KEY_TAB:
			nd = rb_prev(nd);
			if (nd == NULL)
				nd = rb_last(&self->entries);
			annotate_browser__set_top(self, nd);
			break;
		case NEWT_KEY_UNTAB:
			nd = rb_next(nd);
			if (nd == NULL)
				nd = rb_first(&self->entries);
			annotate_browser__set_top(self, nd);
			break;
		default:
			goto out;
		}
	}
out:
	ui_browser__hide(&self->b);
	return key;
}

int hist_entry__tui_annotate(struct hist_entry *self)
{
	struct objdump_line *pos, *n;
	struct objdump_line_rb_node *rbpos;
	LIST_HEAD(head);
	struct annotate_browser browser = {
		.b = {
			.entries = &head,
			.refresh = ui_browser__list_head_refresh,
			.seek	 = ui_browser__list_head_seek,
			.write	 = annotate_browser__write,
			.priv	 = self,
		},
	};
	int ret;

	if (self->ms.sym == NULL)
		return -1;

	if (self->ms.map->dso->annotate_warned)
		return -1;

	if (hist_entry__annotate(self, &head, sizeof(*rbpos)) < 0) {
		ui__error_window(ui_helpline__last_msg);
		return -1;
	}

	ui_helpline__push("Press <- or ESC to exit");

	list_for_each_entry(pos, &head, node) {
		size_t line_len = strlen(pos->line);
		if (browser.b.width < line_len)
			browser.b.width = line_len;
		rbpos = objdump_line__rb(pos);
		rbpos->idx = browser.b.nr_entries++;
		rbpos->percent = objdump_line__calc_percent(pos, &head, self->ms.sym);
		if (rbpos->percent < 0.01)
			continue;
		objdump__insert_line(&browser.entries, rbpos);
	}

	/*
	 * Position the browser at the hottest line.
	 */
	browser.curr_hot = rb_last(&browser.entries);
	if (browser.curr_hot)
		annotate_browser__set_top(&browser, browser.curr_hot);

	browser.b.width += 18; /* Percentage */
	ret = annotate_browser__run(&browser);
	list_for_each_entry_safe(pos, n, &head, node) {
		list_del(&pos->node);
		objdump_line__free(pos);
	}
	return ret;
}
