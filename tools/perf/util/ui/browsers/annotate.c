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
		int color = ui_browser__percent_color(olrb->percent, current_entry);
		SLsmg_set_color(color);
		slsmg_printf(" %7.2f ", olrb->percent);
		if (!current_entry)
			SLsmg_set_color(HE_COLORSET_CODE);
	} else {
		int color = ui_browser__percent_color(0, current_entry);
		SLsmg_set_color(color);
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

int hist_entry__tui_annotate(struct hist_entry *self)
{
	struct newtExitStruct es;
	struct objdump_line *pos, *n;
	struct objdump_line_rb_node *rbpos;
	struct rb_node *nd;
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
	nd = rb_last(&browser.entries);
	if (nd != NULL) {
		unsigned back;

		ui_browser__refresh_dimensions(&browser.b);
		back = browser.b.height / 2;
		rbpos = rb_entry(nd, struct objdump_line_rb_node, rb_node);
		pos = ((struct objdump_line *)rbpos) - 1;
		browser.b.top_idx = browser.b.index = rbpos->idx;

		while (browser.b.top_idx != 0 && back != 0) {
			pos = list_entry(pos->node.prev, struct objdump_line, node);

			--browser.b.top_idx;
			--back;
		}

		browser.b.top = pos;
	}

	browser.b.width += 18; /* Percentage */
	ui_browser__show(&browser.b, self->ms.sym->name);
	ret = ui_browser__run(&browser.b, &es);
	newtFormDestroy(browser.b.form);
	newtPopWindow();
	list_for_each_entry_safe(pos, n, &head, node) {
		list_del(&pos->node);
		objdump_line__free(pos);
	}
	ui_helpline__pop();
	return ret;
}
