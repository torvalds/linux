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

static void annotate_browser__write(struct ui_browser *self, void *entry, int row)
{
	struct objdump_line *ol = rb_entry(entry, struct objdump_line, node);
	bool current_entry = ui_browser__is_current_entry(self, row);
	int width = self->width;

	if (ol->offset != -1) {
		struct hist_entry *he = self->priv;
		struct symbol *sym = he->ms.sym;
		int len = he->ms.sym->end - he->ms.sym->start;
		unsigned int hits = 0;
		double percent = 0.0;
		int color;
		struct sym_priv *priv = symbol__priv(sym);
		struct sym_ext *sym_ext = priv->ext;
		struct sym_hist *h = priv->hist;
		s64 offset = ol->offset;
		struct objdump_line *next = objdump__get_next_ip_line(self->entries, ol);

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

		color = ui_browser__percent_color(percent, current_entry);
		SLsmg_set_color(color);
		slsmg_printf(" %7.2f ", percent);
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

int hist_entry__tui_annotate(struct hist_entry *self)
{
	struct newtExitStruct es;
	struct objdump_line *pos, *n;
	LIST_HEAD(head);
	struct ui_browser browser = {
		.entries = &head,
		.refresh = ui_browser__list_head_refresh,
		.seek	 = ui_browser__list_head_seek,
		.write	 = annotate_browser__write,
		.priv	 = self,
	};
	int ret;

	if (self->ms.sym == NULL)
		return -1;

	if (self->ms.map->dso->annotate_warned)
		return -1;

	if (hist_entry__annotate(self, &head) < 0) {
		ui__error_window(browser__last_msg);
		return -1;
	}

	ui_helpline__push("Press <- or ESC to exit");

	list_for_each_entry(pos, &head, node) {
		size_t line_len = strlen(pos->line);
		if (browser.width < line_len)
			browser.width = line_len;
		++browser.nr_entries;
	}

	browser.width += 18; /* Percentage */
	ui_browser__show(&browser, self->ms.sym->name);
	newtFormAddHotKey(browser.form, ' ');
	ret = ui_browser__run(&browser, &es);
	newtFormDestroy(browser.form);
	newtPopWindow();
	list_for_each_entry_safe(pos, n, &head, node) {
		list_del(&pos->node);
		objdump_line__free(pos);
	}
	ui_helpline__pop();
	return ret;
}
