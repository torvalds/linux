#define _GNU_SOURCE
#include <stdio.h>
#undef _GNU_SOURCE
/*
 * slang versions <= 2.0.6 have a "#if HAVE_LONG_LONG" that breaks
 * the build if it isn't defined. Use the equivalent one that glibc
 * has on features.h.
 */
#include <features.h>
#ifndef HAVE_LONG_LONG
#define HAVE_LONG_LONG __GLIBC_HAVE_LONG_LONG
#endif
#include <slang.h>
#include <stdlib.h>
#include <newt.h>
#include <sys/ttydefaults.h>

#include "cache.h"
#include "hist.h"
#include "pstack.h"
#include "session.h"
#include "sort.h"
#include "symbol.h"

#if SLANG_VERSION < 20104
#define slsmg_printf(msg, args...) SLsmg_printf((char *)msg, ##args)
#define slsmg_write_nstring(msg, len) SLsmg_write_nstring((char *)msg, len)
#define sltt_set_color(obj, name, fg, bg) SLtt_set_color(obj,(char *)name,\
							 (char *)fg, (char *)bg)
#else
#define slsmg_printf SLsmg_printf
#define slsmg_write_nstring SLsmg_write_nstring
#define sltt_set_color SLtt_set_color
#endif

struct ui_progress {
	newtComponent form, scale;
};

struct ui_progress *ui_progress__new(const char *title, u64 total)
{
	struct ui_progress *self = malloc(sizeof(*self));

	if (self != NULL) {
		int cols;

		if (use_browser <= 0)	
			return self;
		newtGetScreenSize(&cols, NULL);
		cols -= 4;
		newtCenteredWindow(cols, 1, title);
		self->form  = newtForm(NULL, NULL, 0);
		if (self->form == NULL)
			goto out_free_self;
		self->scale = newtScale(0, 0, cols, total);
		if (self->scale == NULL)
			goto out_free_form;
		newtFormAddComponent(self->form, self->scale);
		newtRefresh();
	}

	return self;

out_free_form:
	newtFormDestroy(self->form);
out_free_self:
	free(self);
	return NULL;
}

void ui_progress__update(struct ui_progress *self, u64 curr)
{
	/*
	 * FIXME: We should have a per UI backend way of showing progress,
	 * stdio will just show a percentage as NN%, etc.
	 */
	if (use_browser <= 0)
		return;
	newtScaleSet(self->scale, curr);
	newtRefresh();
}

void ui_progress__delete(struct ui_progress *self)
{
	if (use_browser > 0) {
		newtFormDestroy(self->form);
		newtPopWindow();
	}
	free(self);
}

static void ui_helpline__pop(void)
{
	newtPopHelpLine();
}

static void ui_helpline__push(const char *msg)
{
	newtPushHelpLine(msg);
}

static void ui_helpline__vpush(const char *fmt, va_list ap)
{
	char *s;

	if (vasprintf(&s, fmt, ap) < 0)
		vfprintf(stderr, fmt, ap);
	else {
		ui_helpline__push(s);
		free(s);
	}
}

static void ui_helpline__fpush(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ui_helpline__vpush(fmt, ap);
	va_end(ap);
}

static void ui_helpline__puts(const char *msg)
{
	ui_helpline__pop();
	ui_helpline__push(msg);
}

static char browser__last_msg[1024];

int browser__show_help(const char *format, va_list ap)
{
	int ret;
	static int backlog;

        ret = vsnprintf(browser__last_msg + backlog,
			sizeof(browser__last_msg) - backlog, format, ap);
	backlog += ret;

	if (browser__last_msg[backlog - 1] == '\n') {
		ui_helpline__puts(browser__last_msg);
		newtRefresh();
		backlog = 0;
	}

	return ret;
}

static void newt_form__set_exit_keys(newtComponent self)
{
	newtFormAddHotKey(self, NEWT_KEY_LEFT);
	newtFormAddHotKey(self, NEWT_KEY_ESCAPE);
	newtFormAddHotKey(self, 'Q');
	newtFormAddHotKey(self, 'q');
	newtFormAddHotKey(self, CTRL('c'));
}

static newtComponent newt_form__new(void)
{
	newtComponent self = newtForm(NULL, NULL, 0);
	if (self)
		newt_form__set_exit_keys(self);
	return self;
}

static int popup_menu(int argc, char * const argv[])
{
	struct newtExitStruct es;
	int i, rc = -1, max_len = 5;
	newtComponent listbox, form = newt_form__new();

	if (form == NULL)
		return -1;

	listbox = newtListbox(0, 0, argc, NEWT_FLAG_RETURNEXIT);
	if (listbox == NULL)
		goto out_destroy_form;

	newtFormAddComponent(form, listbox);

	for (i = 0; i < argc; ++i) {
		int len = strlen(argv[i]);
		if (len > max_len)
			max_len = len;
		if (newtListboxAddEntry(listbox, argv[i], (void *)(long)i))
			goto out_destroy_form;
	}

	newtCenteredWindow(max_len, argc, NULL);
	newtFormRun(form, &es);
	rc = newtListboxGetCurrent(listbox) - NULL;
	if (es.reason == NEWT_EXIT_HOTKEY)
		rc = -1;
	newtPopWindow();
out_destroy_form:
	newtFormDestroy(form);
	return rc;
}

static int ui__help_window(const char *text)
{
	struct newtExitStruct es;
	newtComponent tb, form = newt_form__new();
	int rc = -1;
	int max_len = 0, nr_lines = 0;
	const char *t;

	if (form == NULL)
		return -1;

	t = text;
	while (1) {
		const char *sep = strchr(t, '\n');
		int len;

		if (sep == NULL)
			sep = strchr(t, '\0');
		len = sep - t;
		if (max_len < len)
			max_len = len;
		++nr_lines;
		if (*sep == '\0')
			break;
		t = sep + 1;
	}

	tb = newtTextbox(0, 0, max_len, nr_lines, 0);
	if (tb == NULL)
		goto out_destroy_form;

	newtTextboxSetText(tb, text);
	newtFormAddComponent(form, tb);
	newtCenteredWindow(max_len, nr_lines, NULL);
	newtFormRun(form, &es);
	newtPopWindow();
	rc = 0;
out_destroy_form:
	newtFormDestroy(form);
	return rc;
}

static bool dialog_yesno(const char *msg)
{
	/* newtWinChoice should really be accepting const char pointers... */
	char yes[] = "Yes", no[] = "No";
	return newtWinChoice(NULL, yes, no, (char *)msg) == 1;
}

static void ui__error_window(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	newtWinMessagev((char *)"Error", (char *)"Ok", (char *)fmt, ap);
	va_end(ap);
}

#define HE_COLORSET_TOP		50
#define HE_COLORSET_MEDIUM	51
#define HE_COLORSET_NORMAL	52
#define HE_COLORSET_SELECTED	53
#define HE_COLORSET_CODE	54

static int ui_browser__percent_color(double percent, bool current)
{
	if (current)
		return HE_COLORSET_SELECTED;
	if (percent >= MIN_RED)
		return HE_COLORSET_TOP;
	if (percent >= MIN_GREEN)
		return HE_COLORSET_MEDIUM;
	return HE_COLORSET_NORMAL;
}

struct ui_browser {
	newtComponent	form, sb;
	u64		index, first_visible_entry_idx;
	void		*first_visible_entry, *entries;
	u16		top, left, width, height;
	void		*priv;
	u32		nr_entries;
};

static void ui_browser__refresh_dimensions(struct ui_browser *self)
{
	int cols, rows;
	newtGetScreenSize(&cols, &rows);

	if (self->width > cols - 4)
		self->width = cols - 4;
	self->height = rows - 5;
	if (self->height > self->nr_entries)
		self->height = self->nr_entries;
	self->top  = (rows - self->height) / 2;
	self->left = (cols - self->width) / 2;
}

static void ui_browser__reset_index(struct ui_browser *self)
{
        self->index = self->first_visible_entry_idx = 0;
        self->first_visible_entry = NULL;
}

static int objdump_line__show(struct objdump_line *self, struct list_head *head,
			      int width, struct hist_entry *he, int len,
			      bool current_entry)
{
	if (self->offset != -1) {
		struct symbol *sym = he->ms.sym;
		unsigned int hits = 0;
		double percent = 0.0;
		int color;
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
	if (!*self->line)
		slsmg_write_nstring(" ", width - 18);
	else
		slsmg_write_nstring(self->line, width - 18);

	return 0;
}

static int ui_browser__refresh_entries(struct ui_browser *self)
{
	struct objdump_line *pos;
	struct list_head *head = self->entries;
	struct hist_entry *he = self->priv;
	int row = 0;
	int len = he->ms.sym->end - he->ms.sym->start;

	if (self->first_visible_entry == NULL || self->first_visible_entry == self->entries)
                self->first_visible_entry = head->next;

	pos = list_entry(self->first_visible_entry, struct objdump_line, node);

	list_for_each_entry_from(pos, head, node) {
		bool current_entry = (self->first_visible_entry_idx + row) == self->index;
		SLsmg_gotorc(self->top + row, self->left);
		objdump_line__show(pos, head, self->width,
				   he, len, current_entry);
		if (++row == self->height)
			break;
	}

	SLsmg_set_color(HE_COLORSET_NORMAL);
	SLsmg_fill_region(self->top + row, self->left,
			  self->height - row, self->width, ' ');

	return 0;
}

static int ui_browser__run(struct ui_browser *self, const char *title,
			   struct newtExitStruct *es)
{
	if (self->form) {
		newtFormDestroy(self->form);
		newtPopWindow();
	}

	ui_browser__refresh_dimensions(self);
	newtCenteredWindow(self->width + 2, self->height, title);
	self->form = newt_form__new();
	if (self->form == NULL)
		return -1;

	self->sb = newtVerticalScrollbar(self->width + 1, 0, self->height,
					 HE_COLORSET_NORMAL,
					 HE_COLORSET_SELECTED);
	if (self->sb == NULL)
		return -1;

	newtFormAddHotKey(self->form, NEWT_KEY_UP);
	newtFormAddHotKey(self->form, NEWT_KEY_DOWN);
	newtFormAddHotKey(self->form, NEWT_KEY_PGUP);
	newtFormAddHotKey(self->form, NEWT_KEY_PGDN);
	newtFormAddHotKey(self->form, ' ');
	newtFormAddHotKey(self->form, NEWT_KEY_HOME);
	newtFormAddHotKey(self->form, NEWT_KEY_END);
	newtFormAddHotKey(self->form, NEWT_KEY_TAB);
	newtFormAddHotKey(self->form, NEWT_KEY_RIGHT);

	if (ui_browser__refresh_entries(self) < 0)
		return -1;
	newtFormAddComponent(self->form, self->sb);

	while (1) {
		unsigned int offset;

		newtFormRun(self->form, es);

		if (es->reason != NEWT_EXIT_HOTKEY)
			break;
		if (is_exit_key(es->u.key))
			return es->u.key;
		switch (es->u.key) {
		case NEWT_KEY_DOWN:
			if (self->index == self->nr_entries - 1)
				break;
			++self->index;
			if (self->index == self->first_visible_entry_idx + self->height) {
				struct list_head *pos = self->first_visible_entry;
				++self->first_visible_entry_idx;
				self->first_visible_entry = pos->next;
			}
			break;
		case NEWT_KEY_UP:
			if (self->index == 0)
				break;
			--self->index;
			if (self->index < self->first_visible_entry_idx) {
				struct list_head *pos = self->first_visible_entry;
				--self->first_visible_entry_idx;
				self->first_visible_entry = pos->prev;
			}
			break;
		case NEWT_KEY_PGDN:
		case ' ':
			if (self->first_visible_entry_idx + self->height > self->nr_entries - 1)
				break;

			offset = self->height;
			if (self->index + offset > self->nr_entries - 1)
				offset = self->nr_entries - 1 - self->index;
			self->index += offset;
			self->first_visible_entry_idx += offset;

			while (offset--) {
				struct list_head *pos = self->first_visible_entry;
				self->first_visible_entry = pos->next;
			}

			break;
		case NEWT_KEY_PGUP:
			if (self->first_visible_entry_idx == 0)
				break;

			if (self->first_visible_entry_idx < self->height)
				offset = self->first_visible_entry_idx;
			else
				offset = self->height;

			self->index -= offset;
			self->first_visible_entry_idx -= offset;

			while (offset--) {
				struct list_head *pos = self->first_visible_entry;
				self->first_visible_entry = pos->prev;
			}
			break;
		case NEWT_KEY_HOME:
			ui_browser__reset_index(self);
			break;
		case NEWT_KEY_END: {
			struct list_head *head = self->entries;
			offset = self->height - 1;

			if (offset > self->nr_entries)
				offset = self->nr_entries;

			self->index = self->first_visible_entry_idx = self->nr_entries - 1 - offset;
			self->first_visible_entry = head->prev;
			while (offset-- != 0) {
				struct list_head *pos = self->first_visible_entry;
				self->first_visible_entry = pos->prev;
			}
		}
			break;
		case NEWT_KEY_RIGHT:
		case NEWT_KEY_LEFT:
		case NEWT_KEY_TAB:
			return es->u.key;
		default:
			continue;
		}
		if (ui_browser__refresh_entries(self) < 0)
			return -1;
	}
	return 0;
}

/*
 * When debugging newt problems it was useful to be able to "unroll"
 * the calls to newtCheckBoxTreeAdd{Array,Item}, so that we can generate
 * a source file with the sequence of calls to these methods, to then
 * tweak the arrays to get the intended results, so I'm keeping this code
 * here, may be useful again in the future.
 */
#undef NEWT_DEBUG

static void newt_checkbox_tree__add(newtComponent tree, const char *str,
				    void *priv, int *indexes)
{
#ifdef NEWT_DEBUG
	/* Print the newtCheckboxTreeAddArray to tinker with its index arrays */
	int i = 0, len = 40 - strlen(str);

	fprintf(stderr,
		"\tnewtCheckboxTreeAddItem(tree, %*.*s\"%s\", (void *)%p, 0, ",
		len, len, " ", str, priv);
	while (indexes[i] != NEWT_ARG_LAST) {
		if (indexes[i] != NEWT_ARG_APPEND)
			fprintf(stderr, " %d,", indexes[i]);
		else
			fprintf(stderr, " %s,", "NEWT_ARG_APPEND");
		++i;
	}
	fprintf(stderr, " %s", " NEWT_ARG_LAST);\n");
	fflush(stderr);
#endif
	newtCheckboxTreeAddArray(tree, str, priv, 0, indexes);
}

static char *callchain_list__sym_name(struct callchain_list *self,
				      char *bf, size_t bfsize)
{
	if (self->ms.sym)
		return self->ms.sym->name;

	snprintf(bf, bfsize, "%#Lx", self->ip);
	return bf;
}

static void __callchain__append_graph_browser(struct callchain_node *self,
					      newtComponent tree, u64 total,
					      int *indexes, int depth)
{
	struct rb_node *node;
	u64 new_total, remaining;
	int idx = 0;

	if (callchain_param.mode == CHAIN_GRAPH_REL)
		new_total = self->children_hit;
	else
		new_total = total;

	remaining = new_total;
	node = rb_first(&self->rb_root);
	while (node) {
		struct callchain_node *child = rb_entry(node, struct callchain_node, rb_node);
		struct rb_node *next = rb_next(node);
		u64 cumul = cumul_hits(child);
		struct callchain_list *chain;
		int first = true, printed = 0;
		int chain_idx = -1;
		remaining -= cumul;

		indexes[depth] = NEWT_ARG_APPEND;
		indexes[depth + 1] = NEWT_ARG_LAST;

		list_for_each_entry(chain, &child->val, list) {
			char ipstr[BITS_PER_LONG / 4 + 1],
			     *alloc_str = NULL;
			const char *str = callchain_list__sym_name(chain, ipstr, sizeof(ipstr));

			if (first) {
				double percent = cumul * 100.0 / new_total;

				first = false;
				if (asprintf(&alloc_str, "%2.2f%% %s", percent, str) < 0)
					str = "Not enough memory!";
				else
					str = alloc_str;
			} else {
				indexes[depth] = idx;
				indexes[depth + 1] = NEWT_ARG_APPEND;
				indexes[depth + 2] = NEWT_ARG_LAST;
				++chain_idx;
			}
			newt_checkbox_tree__add(tree, str, &chain->ms, indexes);
			free(alloc_str);
			++printed;
		}

		indexes[depth] = idx;
		if (chain_idx != -1)
			indexes[depth + 1] = chain_idx;
		if (printed != 0)
			++idx;
		__callchain__append_graph_browser(child, tree, new_total, indexes,
						  depth + (chain_idx != -1 ? 2 : 1));
		node = next;
	}
}

static void callchain__append_graph_browser(struct callchain_node *self,
					    newtComponent tree, u64 total,
					    int *indexes, int parent_idx)
{
	struct callchain_list *chain;
	int i = 0;

	indexes[1] = NEWT_ARG_APPEND;
	indexes[2] = NEWT_ARG_LAST;

	list_for_each_entry(chain, &self->val, list) {
		char ipstr[BITS_PER_LONG / 4 + 1], *str;

		if (chain->ip >= PERF_CONTEXT_MAX)
			continue;

		if (!i++ && sort__first_dimension == SORT_SYM)
			continue;

		str = callchain_list__sym_name(chain, ipstr, sizeof(ipstr));
		newt_checkbox_tree__add(tree, str, &chain->ms, indexes);
	}

	indexes[1] = parent_idx;
	indexes[2] = NEWT_ARG_APPEND;
	indexes[3] = NEWT_ARG_LAST;
	__callchain__append_graph_browser(self, tree, total, indexes, 2);
}

static void hist_entry__append_callchain_browser(struct hist_entry *self,
						 newtComponent tree, u64 total, int parent_idx)
{
	struct rb_node *rb_node;
	int indexes[1024] = { [0] = parent_idx, };
	int idx = 0;
	struct callchain_node *chain;

	rb_node = rb_first(&self->sorted_chain);
	while (rb_node) {
		chain = rb_entry(rb_node, struct callchain_node, rb_node);
		switch (callchain_param.mode) {
		case CHAIN_FLAT:
			break;
		case CHAIN_GRAPH_ABS: /* falldown */
		case CHAIN_GRAPH_REL:
			callchain__append_graph_browser(chain, tree, total, indexes, idx++);
			break;
		case CHAIN_NONE:
		default:
			break;
		}
		rb_node = rb_next(rb_node);
	}
}

static size_t hist_entry__append_browser(struct hist_entry *self,
					 newtComponent tree, u64 total)
{
	char s[256];
	size_t ret;

	if (symbol_conf.exclude_other && !self->parent)
		return 0;

	ret = hist_entry__snprintf(self, s, sizeof(s), NULL,
				   false, 0, false, total);
	if (symbol_conf.use_callchain) {
		int indexes[2];

		indexes[0] = NEWT_ARG_APPEND;
		indexes[1] = NEWT_ARG_LAST;
		newt_checkbox_tree__add(tree, s, &self->ms, indexes);
	} else
		newtListboxAppendEntry(tree, s, &self->ms);

	return ret;
}

int hist_entry__tui_annotate(struct hist_entry *self)
{
	struct ui_browser browser;
	struct newtExitStruct es;
	struct objdump_line *pos, *n;
	LIST_HEAD(head);
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

	memset(&browser, 0, sizeof(browser));
	browser.entries = &head;
	browser.priv = self;
	list_for_each_entry(pos, &head, node) {
		size_t line_len = strlen(pos->line);
		if (browser.width < line_len)
			browser.width = line_len;
		++browser.nr_entries;
	}

	browser.width += 18; /* Percentage */
	ret = ui_browser__run(&browser, self->ms.sym->name, &es);
	newtFormDestroy(browser.form);
	newtPopWindow();
	list_for_each_entry_safe(pos, n, &head, node) {
		list_del(&pos->node);
		objdump_line__free(pos);
	}
	ui_helpline__pop();
	return ret;
}

static const void *newt__symbol_tree_get_current(newtComponent self)
{
	if (symbol_conf.use_callchain)
		return newtCheckboxTreeGetCurrent(self);
	return newtListboxGetCurrent(self);
}

static void hist_browser__selection(newtComponent self, void *data)
{
	const struct map_symbol **symbol_ptr = data;
	*symbol_ptr = newt__symbol_tree_get_current(self);
}

struct hist_browser {
	newtComponent		form, tree;
	const struct map_symbol *selection;
};

static struct hist_browser *hist_browser__new(void)
{
	struct hist_browser *self = malloc(sizeof(*self));

	if (self != NULL)
		self->form = NULL;

	return self;
}

static void hist_browser__delete(struct hist_browser *self)
{
	newtFormDestroy(self->form);
	newtPopWindow();
	free(self);
}

static int hist_browser__populate(struct hist_browser *self, struct hists *hists,
				  const char *title)
{
	int max_len = 0, idx, cols, rows;
	struct ui_progress *progress;
	struct rb_node *nd;
	u64 curr_hist = 0;
	char seq[] = ".", unit;
	char str[256];
	unsigned long nr_events = hists->stats.nr_events[PERF_RECORD_SAMPLE];

	if (self->form) {
		newtFormDestroy(self->form);
		newtPopWindow();
	}

	nr_events = convert_unit(nr_events, &unit);
	snprintf(str, sizeof(str), "Events: %lu%c                            ",
		 nr_events, unit);
	newtDrawRootText(0, 0, str);

	newtGetScreenSize(NULL, &rows);

	if (symbol_conf.use_callchain)
		self->tree = newtCheckboxTreeMulti(0, 0, rows - 5, seq,
						   NEWT_FLAG_SCROLL);
	else
		self->tree = newtListbox(0, 0, rows - 5,
					(NEWT_FLAG_SCROLL |
					 NEWT_FLAG_RETURNEXIT));

	newtComponentAddCallback(self->tree, hist_browser__selection,
				 &self->selection);

	progress = ui_progress__new("Adding entries to the browser...",
				    hists->nr_entries);
	if (progress == NULL)
		return -1;

	idx = 0;
	for (nd = rb_first(&hists->entries); nd; nd = rb_next(nd)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		int len;

		if (h->filtered)
			continue;

		len = hist_entry__append_browser(h, self->tree, hists->stats.total_period);
		if (len > max_len)
			max_len = len;
		if (symbol_conf.use_callchain)
			hist_entry__append_callchain_browser(h, self->tree,
							     hists->stats.total_period, idx++);
		++curr_hist;
		if (curr_hist % 5)
			ui_progress__update(progress, curr_hist);
	}

	ui_progress__delete(progress);

	newtGetScreenSize(&cols, &rows);

	if (max_len > cols)
		max_len = cols - 3;

	if (!symbol_conf.use_callchain)
		newtListboxSetWidth(self->tree, max_len);

	newtCenteredWindow(max_len + (symbol_conf.use_callchain ? 5 : 0),
			   rows - 5, title);
	self->form = newt_form__new();
	if (self->form == NULL)
		return -1;

	newtFormAddHotKey(self->form, 'A');
	newtFormAddHotKey(self->form, 'a');
	newtFormAddHotKey(self->form, 'D');
	newtFormAddHotKey(self->form, 'd');
	newtFormAddHotKey(self->form, 'T');
	newtFormAddHotKey(self->form, 't');
	newtFormAddHotKey(self->form, '?');
	newtFormAddHotKey(self->form, 'H');
	newtFormAddHotKey(self->form, 'h');
	newtFormAddHotKey(self->form, NEWT_KEY_F1);
	newtFormAddHotKey(self->form, NEWT_KEY_RIGHT);
	newtFormAddHotKey(self->form, NEWT_KEY_TAB);
	newtFormAddHotKey(self->form, NEWT_KEY_UNTAB);
	newtFormAddComponents(self->form, self->tree, NULL);
	self->selection = newt__symbol_tree_get_current(self->tree);

	return 0;
}

static struct hist_entry *hist_browser__selected_entry(struct hist_browser *self)
{
	int *indexes;

	if (!symbol_conf.use_callchain)
		goto out;

	indexes = newtCheckboxTreeFindItem(self->tree, (void *)self->selection);
	if (indexes) {
		bool is_hist_entry = indexes[1] == NEWT_ARG_LAST;
		free(indexes);
		if (is_hist_entry)
			goto out;
	}
	return NULL;
out:
	return container_of(self->selection, struct hist_entry, ms);
}

static struct thread *hist_browser__selected_thread(struct hist_browser *self)
{
	struct hist_entry *he = hist_browser__selected_entry(self);
	return he ? he->thread : NULL;
}

static int hist_browser__title(char *bf, size_t size, const char *ev_name,
			       const struct dso *dso, const struct thread *thread)
{
	int printed = 0;

	if (thread)
		printed += snprintf(bf + printed, size - printed,
				    "Thread: %s(%d)",
				    (thread->comm_set ?  thread->comm : ""),
				    thread->pid);
	if (dso)
		printed += snprintf(bf + printed, size - printed,
				    "%sDSO: %s", thread ? " " : "",
				    dso->short_name);
	return printed ?: snprintf(bf, size, "Event: %s", ev_name);
}

int hists__browse(struct hists *self, const char *helpline, const char *ev_name)
{
	struct hist_browser *browser = hist_browser__new();
	struct pstack *fstack;
	const struct thread *thread_filter = NULL;
	const struct dso *dso_filter = NULL;
	struct newtExitStruct es;
	char msg[160];
	int key = -1;

	if (browser == NULL)
		return -1;

	fstack = pstack__new(2);
	if (fstack == NULL)
		goto out;

	ui_helpline__push(helpline);

	hist_browser__title(msg, sizeof(msg), ev_name,
			    dso_filter, thread_filter);
	if (hist_browser__populate(browser, self, msg) < 0)
		goto out_free_stack;

	while (1) {
		const struct thread *thread;
		const struct dso *dso;
		char *options[16];
		int nr_options = 0, choice = 0, i,
		    annotate = -2, zoom_dso = -2, zoom_thread = -2;

		newtFormRun(browser->form, &es);

		thread = hist_browser__selected_thread(browser);
		dso = browser->selection->map ? browser->selection->map->dso : NULL;

		if (es.reason == NEWT_EXIT_HOTKEY) {
			key = es.u.key;

			switch (key) {
			case NEWT_KEY_F1:
				goto do_help;
			case NEWT_KEY_TAB:
			case NEWT_KEY_UNTAB:
				/*
				 * Exit the browser, let hists__browser_tree
				 * go to the next or previous
				 */
				goto out_free_stack;
			default:;
			}

			key = toupper(key);
			switch (key) {
			case 'A':
				if (browser->selection->map == NULL &&
				    browser->selection->map->dso->annotate_warned)
					continue;
				goto do_annotate;
			case 'D':
				goto zoom_dso;
			case 'T':
				goto zoom_thread;
			case 'H':
			case '?':
do_help:
				ui__help_window("->        Zoom into DSO/Threads & Annotate current symbol\n"
						"<-        Zoom out\n"
						"a         Annotate current symbol\n"
						"h/?/F1    Show this window\n"
						"d         Zoom into current DSO\n"
						"t         Zoom into current Thread\n"
						"q/CTRL+C  Exit browser");
				continue;
			default:;
			}
			if (is_exit_key(key)) {
				if (key == NEWT_KEY_ESCAPE) {
					if (dialog_yesno("Do you really want to exit?"))
						break;
					else
						continue;
				} else
					break;
			}

			if (es.u.key == NEWT_KEY_LEFT) {
				const void *top;

				if (pstack__empty(fstack))
					continue;
				top = pstack__pop(fstack);
				if (top == &dso_filter)
					goto zoom_out_dso;
				if (top == &thread_filter)
					goto zoom_out_thread;
				continue;
			}
		}

		if (browser->selection->sym != NULL &&
		    !browser->selection->map->dso->annotate_warned &&
		    asprintf(&options[nr_options], "Annotate %s",
			     browser->selection->sym->name) > 0)
			annotate = nr_options++;

		if (thread != NULL &&
		    asprintf(&options[nr_options], "Zoom %s %s(%d) thread",
			     (thread_filter ? "out of" : "into"),
			     (thread->comm_set ? thread->comm : ""),
			     thread->pid) > 0)
			zoom_thread = nr_options++;

		if (dso != NULL &&
		    asprintf(&options[nr_options], "Zoom %s %s DSO",
			     (dso_filter ? "out of" : "into"),
			     (dso->kernel ? "the Kernel" : dso->short_name)) > 0)
			zoom_dso = nr_options++;

		options[nr_options++] = (char *)"Exit";

		choice = popup_menu(nr_options, options);

		for (i = 0; i < nr_options - 1; ++i)
			free(options[i]);

		if (choice == nr_options - 1)
			break;

		if (choice == -1)
			continue;

		if (choice == annotate) {
			struct hist_entry *he;
do_annotate:
			if (browser->selection->map->dso->origin == DSO__ORIG_KERNEL) {
				browser->selection->map->dso->annotate_warned = 1;
				ui_helpline__puts("No vmlinux file found, can't "
						 "annotate with just a "
						 "kallsyms file");
				continue;
			}

			he = hist_browser__selected_entry(browser);
			if (he == NULL)
				continue;

			hist_entry__tui_annotate(he);
		} else if (choice == zoom_dso) {
zoom_dso:
			if (dso_filter) {
				pstack__remove(fstack, &dso_filter);
zoom_out_dso:
				ui_helpline__pop();
				dso_filter = NULL;
			} else {
				if (dso == NULL)
					continue;
				ui_helpline__fpush("To zoom out press <- or -> + \"Zoom out of %s DSO\"",
						   dso->kernel ? "the Kernel" : dso->short_name);
				dso_filter = dso;
				pstack__push(fstack, &dso_filter);
			}
			hists__filter_by_dso(self, dso_filter);
			hist_browser__title(msg, sizeof(msg), ev_name,
					    dso_filter, thread_filter);
			if (hist_browser__populate(browser, self, msg) < 0)
				goto out;
		} else if (choice == zoom_thread) {
zoom_thread:
			if (thread_filter) {
				pstack__remove(fstack, &thread_filter);
zoom_out_thread:
				ui_helpline__pop();
				thread_filter = NULL;
			} else {
				ui_helpline__fpush("To zoom out press <- or -> + \"Zoom out of %s(%d) thread\"",
						   thread->comm_set ? thread->comm : "",
						   thread->pid);
				thread_filter = thread;
				pstack__push(fstack, &thread_filter);
			}
			hists__filter_by_thread(self, thread_filter);
			hist_browser__title(msg, sizeof(msg), ev_name,
					    dso_filter, thread_filter);
			if (hist_browser__populate(browser, self, msg) < 0)
				goto out;
		}
	}
out_free_stack:
	pstack__delete(fstack);
out:
	hist_browser__delete(browser);
	return key;
}

int hists__tui_browse_tree(struct rb_root *self, const char *help)
{
	struct rb_node *first = rb_first(self), *nd = first, *next;
	int key = 0;

	while (nd) {
		struct hists *hists = rb_entry(nd, struct hists, rb_node);
		const char *ev_name = __event_name(hists->type, hists->config);

		key = hists__browse(hists, help, ev_name);

		if (is_exit_key(key))
			break;

		switch (key) {
		case NEWT_KEY_TAB:
			next = rb_next(nd);
			if (next)
				nd = next;
			break;
		case NEWT_KEY_UNTAB:
			if (nd == first)
				continue;
			nd = rb_prev(nd);
		default:
			break;
		}
	}

	return key;
}

static struct newtPercentTreeColors {
	const char *topColorFg, *topColorBg;
	const char *mediumColorFg, *mediumColorBg;
	const char *normalColorFg, *normalColorBg;
	const char *selColorFg, *selColorBg;
	const char *codeColorFg, *codeColorBg;
} defaultPercentTreeColors = {
	"red",       "lightgray",
	"green",     "lightgray",
	"black",     "lightgray",
	"lightgray", "magenta",
	"blue",	     "lightgray",
};

void setup_browser(void)
{
	struct newtPercentTreeColors *c = &defaultPercentTreeColors;

	if (!isatty(1) || !use_browser || dump_trace) {
		use_browser = 0;
		setup_pager();
		return;
	}

	use_browser = 1;
	newtInit();
	newtCls();
	ui_helpline__puts(" ");
	sltt_set_color(HE_COLORSET_TOP, NULL, c->topColorFg, c->topColorBg);
	sltt_set_color(HE_COLORSET_MEDIUM, NULL, c->mediumColorFg, c->mediumColorBg);
	sltt_set_color(HE_COLORSET_NORMAL, NULL, c->normalColorFg, c->normalColorBg);
	sltt_set_color(HE_COLORSET_SELECTED, NULL, c->selColorFg, c->selColorBg);
	sltt_set_color(HE_COLORSET_CODE, NULL, c->codeColorFg, c->codeColorBg);
}

void exit_browser(bool wait_for_ok)
{
	if (use_browser > 0) {
		if (wait_for_ok) {
			char title[] = "Fatal Error", ok[] = "Ok";
			newtWinMessage(title, ok, browser__last_msg);
		}
		newtFinished();
	}
}
