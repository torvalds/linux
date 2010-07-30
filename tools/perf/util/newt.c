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
#include <signal.h>
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
	unsigned int	(*refresh_entries)(struct ui_browser *self);
	void		(*seek)(struct ui_browser *self,
				off_t offset, int whence);
	u32		nr_entries;
};

static void ui_browser__list_head_seek(struct ui_browser *self,
				       off_t offset, int whence)
{
	struct list_head *head = self->entries;
	struct list_head *pos;

	switch (whence) {
	case SEEK_SET:
		pos = head->next;
		break;
	case SEEK_CUR:
		pos = self->first_visible_entry;
		break;
	case SEEK_END:
		pos = head->prev;
		break;
	default:
		return;
	}

	if (offset > 0) {
		while (offset-- != 0)
			pos = pos->next;
	} else {
		while (offset++ != 0)
			pos = pos->prev;
	}

	self->first_visible_entry = pos;
}

static bool ui_browser__is_current_entry(struct ui_browser *self, unsigned row)
{
	return (self->first_visible_entry_idx + row) == self->index;
}

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
	self->seek(self, 0, SEEK_SET);
}

static int ui_browser__show(struct ui_browser *self, const char *title)
{
	if (self->form != NULL) {
		newtFormDestroy(self->form);
		newtPopWindow();
	}
	ui_browser__refresh_dimensions(self);
	newtCenteredWindow(self->width, self->height, title);
	self->form = newt_form__new();
	if (self->form == NULL)
		return -1;

	self->sb = newtVerticalScrollbar(self->width, 0, self->height,
					 HE_COLORSET_NORMAL,
					 HE_COLORSET_SELECTED);
	if (self->sb == NULL)
		return -1;

	newtFormAddHotKey(self->form, NEWT_KEY_UP);
	newtFormAddHotKey(self->form, NEWT_KEY_DOWN);
	newtFormAddHotKey(self->form, NEWT_KEY_PGUP);
	newtFormAddHotKey(self->form, NEWT_KEY_PGDN);
	newtFormAddHotKey(self->form, NEWT_KEY_HOME);
	newtFormAddHotKey(self->form, NEWT_KEY_END);
	newtFormAddComponent(self->form, self->sb);
	return 0;
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
	int row;

	newtScrollbarSet(self->sb, self->index, self->nr_entries - 1);
	row = self->refresh_entries(self);
	SLsmg_set_color(HE_COLORSET_NORMAL);
	SLsmg_fill_region(self->top + row, self->left,
			  self->height - row, self->width, ' ');

	return 0;
}

static int ui_browser__run(struct ui_browser *self, struct newtExitStruct *es)
{
	if (ui_browser__refresh_entries(self) < 0)
		return -1;

	while (1) {
		off_t offset;

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
				++self->first_visible_entry_idx;
				self->seek(self, +1, SEEK_CUR);
			}
			break;
		case NEWT_KEY_UP:
			if (self->index == 0)
				break;
			--self->index;
			if (self->index < self->first_visible_entry_idx) {
				--self->first_visible_entry_idx;
				self->seek(self, -1, SEEK_CUR);
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
			self->seek(self, +offset, SEEK_CUR);
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
			self->seek(self, -offset, SEEK_CUR);
			break;
		case NEWT_KEY_HOME:
			ui_browser__reset_index(self);
			break;
		case NEWT_KEY_END:
			offset = self->height - 1;
			if (offset >= self->nr_entries)
				offset = self->nr_entries - 1;

			self->index = self->nr_entries - 1;
			self->first_visible_entry_idx = self->index - offset;
			self->seek(self, -offset, SEEK_END);
			break;
		default:
			return es->u.key;
		}
		if (ui_browser__refresh_entries(self) < 0)
			return -1;
	}
	return 0;
}

static char *callchain_list__sym_name(struct callchain_list *self,
				      char *bf, size_t bfsize)
{
	if (self->ms.sym)
		return self->ms.sym->name;

	snprintf(bf, bfsize, "%#Lx", self->ip);
	return bf;
}

static unsigned int hist_entry__annotate_browser_refresh(struct ui_browser *self)
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
		bool current_entry = ui_browser__is_current_entry(self, row);
		SLsmg_gotorc(self->top + row, self->left);
		objdump_line__show(pos, head, self->width,
				   he, len, current_entry);
		if (++row == self->height)
			break;
	}

	return row;
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
	browser.entries		= &head;
	browser.refresh_entries = hist_entry__annotate_browser_refresh;
	browser.seek		= ui_browser__list_head_seek;
	browser.priv = self;
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

struct hist_browser {
	struct ui_browser   b;
	struct hists	    *hists;
	struct hist_entry   *he_selection;
	struct map_symbol   *selection;
};

static void hist_browser__reset(struct hist_browser *self);
static int hist_browser__run(struct hist_browser *self, const char *title,
			     struct newtExitStruct *es);
static unsigned int hist_browser__refresh_entries(struct ui_browser *self);
static void ui_browser__hists_seek(struct ui_browser *self,
				   off_t offset, int whence);

static struct hist_browser *hist_browser__new(struct hists *hists)
{
	struct hist_browser *self = zalloc(sizeof(*self));

	if (self) {
		self->hists = hists;
		self->b.refresh_entries = hist_browser__refresh_entries;
		self->b.seek = ui_browser__hists_seek;
	}

	return self;
}

static void hist_browser__delete(struct hist_browser *self)
{
	newtFormDestroy(self->b.form);
	newtPopWindow();
	free(self);
}

static struct hist_entry *hist_browser__selected_entry(struct hist_browser *self)
{
	return self->he_selection;
}

static struct thread *hist_browser__selected_thread(struct hist_browser *self)
{
	return self->he_selection->thread;
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
	struct hist_browser *browser = hist_browser__new(self);
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

	while (1) {
		const struct thread *thread;
		const struct dso *dso;
		char *options[16];
		int nr_options = 0, choice = 0, i,
		    annotate = -2, zoom_dso = -2, zoom_thread = -2;

		if (hist_browser__run(browser, msg, &es))
			break;

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
			hist_browser__reset(browser);
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
			hist_browser__reset(browser);
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

static void newt_suspend(void *d __used)
{
	newtSuspend();
	raise(SIGTSTP);
	newtResume();
}

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
	newtSetSuspendCallback(newt_suspend, NULL);
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

static void hist_browser__refresh_dimensions(struct hist_browser *self)
{
	/* 3 == +/- toggle symbol before actual hist_entry rendering */
	self->b.width = 3 + (hists__sort_list_width(self->hists) +
			     sizeof("[k]"));
}

static void hist_browser__reset(struct hist_browser *self)
{
	self->b.nr_entries = self->hists->nr_entries;
	hist_browser__refresh_dimensions(self);
	ui_browser__reset_index(&self->b);
}

static char tree__folded_sign(bool unfolded)
{
	return unfolded ? '-' : '+';
}

static char map_symbol__folded(const struct map_symbol *self)
{
	return self->has_children ? tree__folded_sign(self->unfolded) : ' ';
}

static char hist_entry__folded(const struct hist_entry *self)
{
	return map_symbol__folded(&self->ms);
}

static char callchain_list__folded(const struct callchain_list *self)
{
	return map_symbol__folded(&self->ms);
}

static bool map_symbol__toggle_fold(struct map_symbol *self)
{
	if (!self->has_children)
		return false;

	self->unfolded = !self->unfolded;
	return true;
}

#define LEVEL_OFFSET_STEP 3

static int hist_browser__show_callchain_node_rb_tree(struct hist_browser *self,
						     struct callchain_node *chain_node,
						     u64 total, int level,
						     unsigned short row,
						     off_t *row_offset,
						     bool *is_current_entry)
{
	struct rb_node *node;
	int first_row = row, width, offset = level * LEVEL_OFFSET_STEP;
	u64 new_total, remaining;

	if (callchain_param.mode == CHAIN_GRAPH_REL)
		new_total = chain_node->children_hit;
	else
		new_total = total;

	remaining = new_total;
	node = rb_first(&chain_node->rb_root);
	while (node) {
		struct callchain_node *child = rb_entry(node, struct callchain_node, rb_node);
		struct rb_node *next = rb_next(node);
		u64 cumul = cumul_hits(child);
		struct callchain_list *chain;
		char folded_sign = ' ';
		int first = true;
		int extra_offset = 0;

		remaining -= cumul;

		list_for_each_entry(chain, &child->val, list) {
			char ipstr[BITS_PER_LONG / 4 + 1], *alloc_str;
			const char *str;
			int color;
			bool was_first = first;

			if (first) {
				first = false;
				chain->ms.has_children = chain->list.next != &child->val ||
							 rb_first(&child->rb_root) != NULL;
			} else {
				extra_offset = LEVEL_OFFSET_STEP;
				chain->ms.has_children = chain->list.next == &child->val &&
							 rb_first(&child->rb_root) != NULL;
			}

			folded_sign = callchain_list__folded(chain);
			if (*row_offset != 0) {
				--*row_offset;
				goto do_next;
			}

			alloc_str = NULL;
			str = callchain_list__sym_name(chain, ipstr, sizeof(ipstr));
			if (was_first) {
				double percent = cumul * 100.0 / new_total;

				if (asprintf(&alloc_str, "%2.2f%% %s", percent, str) < 0)
					str = "Not enough memory!";
				else
					str = alloc_str;
			}

			color = HE_COLORSET_NORMAL;
			width = self->b.width - (offset + extra_offset + 2);
			if (ui_browser__is_current_entry(&self->b, row)) {
				self->selection = &chain->ms;
				color = HE_COLORSET_SELECTED;
				*is_current_entry = true;
			}

			SLsmg_set_color(color);
			SLsmg_gotorc(self->b.top + row, self->b.left);
			slsmg_write_nstring(" ", offset + extra_offset);
			slsmg_printf("%c ", folded_sign);
			slsmg_write_nstring(str, width);
			free(alloc_str);

			if (++row == self->b.height)
				goto out;
do_next:
			if (folded_sign == '+')
				break;
		}

		if (folded_sign == '-') {
			const int new_level = level + (extra_offset ? 2 : 1);
			row += hist_browser__show_callchain_node_rb_tree(self, child, new_total,
									 new_level, row, row_offset,
									 is_current_entry);
		}
		if (row == self->b.height)
			goto out;
		node = next;
	}
out:
	return row - first_row;
}

static int hist_browser__show_callchain_node(struct hist_browser *self,
					     struct callchain_node *node,
					     int level, unsigned short row,
					     off_t *row_offset,
					     bool *is_current_entry)
{
	struct callchain_list *chain;
	int first_row = row,
	     offset = level * LEVEL_OFFSET_STEP,
	     width = self->b.width - offset;
	char folded_sign = ' ';

	list_for_each_entry(chain, &node->val, list) {
		char ipstr[BITS_PER_LONG / 4 + 1], *s;
		int color;
		/*
		 * FIXME: This should be moved to somewhere else,
		 * probably when the callchain is created, so as not to
		 * traverse it all over again
		 */
		chain->ms.has_children = rb_first(&node->rb_root) != NULL;
		folded_sign = callchain_list__folded(chain);

		if (*row_offset != 0) {
			--*row_offset;
			continue;
		}

		color = HE_COLORSET_NORMAL;
		if (ui_browser__is_current_entry(&self->b, row)) {
			self->selection = &chain->ms;
			color = HE_COLORSET_SELECTED;
			*is_current_entry = true;
		}

		s = callchain_list__sym_name(chain, ipstr, sizeof(ipstr));
		SLsmg_gotorc(self->b.top + row, self->b.left);
		SLsmg_set_color(color);
		slsmg_write_nstring(" ", offset);
		slsmg_printf("%c ", folded_sign);
		slsmg_write_nstring(s, width - 2);

		if (++row == self->b.height)
			goto out;
	}

	if (folded_sign == '-')
		row += hist_browser__show_callchain_node_rb_tree(self, node,
								 self->hists->stats.total_period,
								 level + 1, row,
								 row_offset,
								 is_current_entry);
out:
	return row - first_row;
}

static int hist_browser__show_callchain(struct hist_browser *self,
					struct rb_root *chain,
					int level, unsigned short row,
					off_t *row_offset,
					bool *is_current_entry)
{
	struct rb_node *nd;
	int first_row = row;

	for (nd = rb_first(chain); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);

		row += hist_browser__show_callchain_node(self, node, level,
							 row, row_offset,
							 is_current_entry);
		if (row == self->b.height)
			break;
	}

	return row - first_row;
}

static int hist_browser__show_entry(struct hist_browser *self,
				    struct hist_entry *entry,
				    unsigned short row)
{
	char s[256];
	double percent;
	int printed = 0;
	int color, width = self->b.width;
	char folded_sign = ' ';
	bool current_entry = ui_browser__is_current_entry(&self->b, row);
	off_t row_offset = entry->row_offset;

	if (current_entry) {
		self->he_selection = entry;
		self->selection = &entry->ms;
	}

	if (symbol_conf.use_callchain) {
		entry->ms.has_children = !RB_EMPTY_ROOT(&entry->sorted_chain);
		folded_sign = hist_entry__folded(entry);
	}

	if (row_offset == 0) {
		hist_entry__snprintf(entry, s, sizeof(s), self->hists, NULL, false,
				     0, false, self->hists->stats.total_period);
		percent = (entry->period * 100.0) / self->hists->stats.total_period;

		color = HE_COLORSET_SELECTED;
		if (!current_entry) {
			if (percent >= MIN_RED)
				color = HE_COLORSET_TOP;
			else if (percent >= MIN_GREEN)
				color = HE_COLORSET_MEDIUM;
			else
				color = HE_COLORSET_NORMAL;
		}

		SLsmg_set_color(color);
		SLsmg_gotorc(self->b.top + row, self->b.left);
		if (symbol_conf.use_callchain) {
			slsmg_printf("%c ", folded_sign);
			width -= 2;
		}
		slsmg_write_nstring(s, width);
		++row;
		++printed;
	} else
		--row_offset;

	if (folded_sign == '-' && row != self->b.height) {
		printed += hist_browser__show_callchain(self, &entry->sorted_chain,
							1, row, &row_offset,
							&current_entry);
		if (current_entry)
			self->he_selection = entry;
	}

	return printed;
}

static unsigned int hist_browser__refresh_entries(struct ui_browser *self)
{
	unsigned row = 0;
	struct rb_node *nd;
	struct hist_browser *hb = container_of(self, struct hist_browser, b);

	if (self->first_visible_entry == NULL)
		self->first_visible_entry = rb_first(&hb->hists->entries);

	for (nd = self->first_visible_entry; nd; nd = rb_next(nd)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);

		if (h->filtered)
			continue;

		row += hist_browser__show_entry(hb, h, row);
		if (row == self->height)
			break;
	}

	return row;
}

static void callchain_node__init_have_children_rb_tree(struct callchain_node *self)
{
	struct rb_node *nd = rb_first(&self->rb_root);

	for (nd = rb_first(&self->rb_root); nd; nd = rb_next(nd)) {
		struct callchain_node *child = rb_entry(nd, struct callchain_node, rb_node);
		struct callchain_list *chain;
		int first = true;

		list_for_each_entry(chain, &child->val, list) {
			if (first) {
				first = false;
				chain->ms.has_children = chain->list.next != &child->val ||
							 rb_first(&child->rb_root) != NULL;
			} else
				chain->ms.has_children = chain->list.next == &child->val &&
							 rb_first(&child->rb_root) != NULL;
		}

		callchain_node__init_have_children_rb_tree(child);
	}
}

static void callchain_node__init_have_children(struct callchain_node *self)
{
	struct callchain_list *chain;

	list_for_each_entry(chain, &self->val, list)
		chain->ms.has_children = rb_first(&self->rb_root) != NULL;

	callchain_node__init_have_children_rb_tree(self);
}

static void callchain__init_have_children(struct rb_root *self)
{
	struct rb_node *nd;

	for (nd = rb_first(self); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);
		callchain_node__init_have_children(node);
	}
}

static void hist_entry__init_have_children(struct hist_entry *self)
{
	if (!self->init_have_children) {
		callchain__init_have_children(&self->sorted_chain);
		self->init_have_children = true;
	}
}

static struct rb_node *hists__filter_entries(struct rb_node *nd)
{
	while (nd != NULL) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		if (!h->filtered)
			return nd;

		nd = rb_next(nd);
	}

	return NULL;
}

static struct rb_node *hists__filter_prev_entries(struct rb_node *nd)
{
	while (nd != NULL) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		if (!h->filtered)
			return nd;

		nd = rb_prev(nd);
	}

	return NULL;
}

static void ui_browser__hists_seek(struct ui_browser *self,
				   off_t offset, int whence)
{
	struct hist_entry *h;
	struct rb_node *nd;
	bool first = true;

	switch (whence) {
	case SEEK_SET:
		nd = hists__filter_entries(rb_first(self->entries));
		break;
	case SEEK_CUR:
		nd = self->first_visible_entry;
		goto do_offset;
	case SEEK_END:
		nd = hists__filter_prev_entries(rb_last(self->entries));
		first = false;
		break;
	default:
		return;
	}

	/*
	 * Moves not relative to the first visible entry invalidates its
	 * row_offset:
	 */
	h = rb_entry(self->first_visible_entry, struct hist_entry, rb_node);
	h->row_offset = 0;

	/*
	 * Here we have to check if nd is expanded (+), if it is we can't go
	 * the next top level hist_entry, instead we must compute an offset of
	 * what _not_ to show and not change the first visible entry.
	 *
	 * This offset increments when we are going from top to bottom and
	 * decreases when we're going from bottom to top.
	 *
	 * As we don't have backpointers to the top level in the callchains
	 * structure, we need to always print the whole hist_entry callchain,
	 * skipping the first ones that are before the first visible entry
	 * and stop when we printed enough lines to fill the screen.
	 */
do_offset:
	if (offset > 0) {
		do {
			h = rb_entry(nd, struct hist_entry, rb_node);
			if (h->ms.unfolded) {
				u16 remaining = h->nr_rows - h->row_offset;
				if (offset > remaining) {
					offset -= remaining;
					h->row_offset = 0;
				} else {
					h->row_offset += offset;
					offset = 0;
					self->first_visible_entry = nd;
					break;
				}
			}
			nd = hists__filter_entries(rb_next(nd));
			if (nd == NULL)
				break;
			--offset;
			self->first_visible_entry = nd;
		} while (offset != 0);
	} else if (offset < 0) {
		while (1) {
			h = rb_entry(nd, struct hist_entry, rb_node);
			if (h->ms.unfolded) {
				if (first) {
					if (-offset > h->row_offset) {
						offset += h->row_offset;
						h->row_offset = 0;
					} else {
						h->row_offset += offset;
						offset = 0;
						self->first_visible_entry = nd;
						break;
					}
				} else {
					if (-offset > h->nr_rows) {
						offset += h->nr_rows;
						h->row_offset = 0;
					} else {
						h->row_offset = h->nr_rows + offset;
						offset = 0;
						self->first_visible_entry = nd;
						break;
					}
				}
			}

			nd = hists__filter_prev_entries(rb_prev(nd));
			if (nd == NULL)
				break;
			++offset;
			self->first_visible_entry = nd;
			if (offset == 0) {
				/*
				 * Last unfiltered hist_entry, check if it is
				 * unfolded, if it is then we should have
				 * row_offset at its last entry.
				 */
				h = rb_entry(nd, struct hist_entry, rb_node);
				if (h->ms.unfolded)
					h->row_offset = h->nr_rows;
				break;
			}
			first = false;
		}
	} else {
		self->first_visible_entry = nd;
		h = rb_entry(nd, struct hist_entry, rb_node);
		h->row_offset = 0;
	}
}

static int callchain_node__count_rows_rb_tree(struct callchain_node *self)
{
	int n = 0;
	struct rb_node *nd;

	for (nd = rb_first(&self->rb_root); nd; nd = rb_next(nd)) {
		struct callchain_node *child = rb_entry(nd, struct callchain_node, rb_node);
		struct callchain_list *chain;
		char folded_sign = ' '; /* No children */

		list_for_each_entry(chain, &child->val, list) {
			++n;
			/* We need this because we may not have children */
			folded_sign = callchain_list__folded(chain);
			if (folded_sign == '+')
				break;
		}

		if (folded_sign == '-') /* Have children and they're unfolded */
			n += callchain_node__count_rows_rb_tree(child);
	}

	return n;
}

static int callchain_node__count_rows(struct callchain_node *node)
{
	struct callchain_list *chain;
	bool unfolded = false;
	int n = 0;

	list_for_each_entry(chain, &node->val, list) {
		++n;
		unfolded = chain->ms.unfolded;
	}

	if (unfolded)
		n += callchain_node__count_rows_rb_tree(node);

	return n;
}

static int callchain__count_rows(struct rb_root *chain)
{
	struct rb_node *nd;
	int n = 0;

	for (nd = rb_first(chain); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);
		n += callchain_node__count_rows(node);
	}

	return n;
}

static bool hist_browser__toggle_fold(struct hist_browser *self)
{
	if (map_symbol__toggle_fold(self->selection)) {
		struct hist_entry *he = self->he_selection;

		hist_entry__init_have_children(he);
		self->hists->nr_entries -= he->nr_rows;

		if (he->ms.unfolded)
			he->nr_rows = callchain__count_rows(&he->sorted_chain);
		else
			he->nr_rows = 0;
		self->hists->nr_entries += he->nr_rows;
		self->b.nr_entries = self->hists->nr_entries;

		return true;
	}

	/* If it doesn't have children, no toggling performed */
	return false;
}

static int hist_browser__run(struct hist_browser *self, const char *title,
			     struct newtExitStruct *es)
{
	char str[256], unit;
	unsigned long nr_events = self->hists->stats.nr_events[PERF_RECORD_SAMPLE];

	self->b.entries = &self->hists->entries;
	self->b.nr_entries = self->hists->nr_entries;

	hist_browser__refresh_dimensions(self);

	nr_events = convert_unit(nr_events, &unit);
	snprintf(str, sizeof(str), "Events: %lu%c                            ",
		 nr_events, unit);
	newtDrawRootText(0, 0, str);

	if (ui_browser__show(&self->b, title) < 0)
		return -1;

	newtFormAddHotKey(self->b.form, 'A');
	newtFormAddHotKey(self->b.form, 'a');
	newtFormAddHotKey(self->b.form, '?');
	newtFormAddHotKey(self->b.form, 'h');
	newtFormAddHotKey(self->b.form, 'H');
	newtFormAddHotKey(self->b.form, 'd');

	newtFormAddHotKey(self->b.form, NEWT_KEY_LEFT);
	newtFormAddHotKey(self->b.form, NEWT_KEY_RIGHT);
	newtFormAddHotKey(self->b.form, NEWT_KEY_ENTER);

	while (1) {
		ui_browser__run(&self->b, es);

		if (es->reason != NEWT_EXIT_HOTKEY)
			break;
		switch (es->u.key) {
		case 'd': { /* Debug */
			static int seq;
			struct hist_entry *h = rb_entry(self->b.first_visible_entry,
							struct hist_entry, rb_node);
			ui_helpline__pop();
			ui_helpline__fpush("%d: nr_ent=(%d,%d), height=%d, idx=%d, fve: idx=%d, row_off=%d, nrows=%d",
					   seq++, self->b.nr_entries,
					   self->hists->nr_entries,
					   self->b.height,
					   self->b.index,
					   self->b.first_visible_entry_idx,
					   h->row_offset, h->nr_rows);
		}
			continue;
		case NEWT_KEY_ENTER:
			if (hist_browser__toggle_fold(self))
				break;
			/* fall thru */
		default:
			return 0;
		}
	}
	return 0;
}
