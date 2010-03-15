#define _GNU_SOURCE
#include <stdio.h>
#undef _GNU_SOURCE

#include <stdlib.h>
#include <newt.h>
#include <sys/ttydefaults.h>

#include "cache.h"
#include "hist.h"
#include "session.h"
#include "sort.h"
#include "symbol.h"

static void newt_form__set_exit_keys(newtComponent self)
{
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

static size_t hist_entry__append_browser(struct hist_entry *self,
					 newtComponent listbox, u64 total)
{
	char bf[1024];
	size_t len;
	FILE *fp;

	if (symbol_conf.exclude_other && !self->parent)
		return 0;

	fp = fmemopen(bf, sizeof(bf), "w");
	if (fp == NULL)
		return 0;

	len = hist_entry__fprintf(self, NULL, false, 0, fp, total);

	fclose(fp);
	newtListboxAppendEntry(listbox, bf, self);
	return len;
}

static void hist_entry__annotate_browser(struct hist_entry *self)
{
	FILE *fp;
	int cols, rows;
	newtComponent form, listbox;
	struct newtExitStruct es;
	char *str;
	size_t line_len, max_line_len = 0;
	size_t max_usable_width;
	char *line = NULL;

	if (self->sym == NULL)
		return;

	if (asprintf(&str, "perf annotate %s 2>&1 | expand", self->sym->name) < 0)
		return;

	fp = popen(str, "r");
	if (fp == NULL)
		goto out_free_str;

	newtPushHelpLine("Press ESC to exit");
	newtGetScreenSize(&cols, &rows);
	listbox = newtListbox(0, 0, rows - 5, NEWT_FLAG_SCROLL);

	while (!feof(fp)) {
		if (getline(&line, &line_len, fp) < 0 || !line_len)
			break;
		while (line_len != 0 && isspace(line[line_len - 1]))
			line[--line_len] = '\0';

		if (line_len > max_line_len)
			max_line_len = line_len;
		newtListboxAppendEntry(listbox, line, NULL);
	}
	fclose(fp);
	free(line);

	max_usable_width = cols - 22;
	if (max_line_len > max_usable_width)
		max_line_len = max_usable_width;

	newtListboxSetWidth(listbox, max_line_len);

	newtCenteredWindow(max_line_len + 2, rows - 5, self->sym->name);
	form = newt_form__new();
	newtFormAddComponents(form, listbox, NULL);

	newtFormRun(form, &es);
	newtFormDestroy(form);
	newtPopWindow();
	newtPopHelpLine();
out_free_str:
	free(str);
}

void perf_session__browse_hists(struct rb_root *hists, u64 session_total,
				const char *helpline)
{
	struct sort_entry *se;
	struct rb_node *nd;
	unsigned int width;
	char *col_width = symbol_conf.col_width_list_str;
	int rows;
	size_t max_len = 0;
	char str[1024];
	newtComponent form, listbox;
	struct newtExitStruct es;

	snprintf(str, sizeof(str), "Samples: %Ld", session_total);
	newtDrawRootText(0, 0, str);
	newtPushHelpLine(helpline);

	newtGetScreenSize(NULL, &rows);

	form = newt_form__new();

	listbox = newtListbox(1, 1, rows - 2, (NEWT_FLAG_SCROLL |
					       NEWT_FLAG_BORDER |
					       NEWT_FLAG_RETURNEXIT));

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		if (se->elide)
			continue;
		width = strlen(se->header);
		if (se->width) {
			if (symbol_conf.col_width_list_str) {
				if (col_width) {
					*se->width = atoi(col_width);
					col_width = strchr(col_width, ',');
					if (col_width)
						++col_width;
				}
			}
			*se->width = max(*se->width, width);
		}
	}

	for (nd = rb_first(hists); nd; nd = rb_next(nd)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		size_t len = hist_entry__append_browser(h, listbox, session_total);
		if (len > max_len)
			max_len = len;
	}

	newtListboxSetWidth(listbox, max_len);
	newtFormAddComponents(form, listbox, NULL);

	while (1) {
		struct hist_entry *selection;

		newtFormRun(form, &es);
		if (es.reason == NEWT_EXIT_HOTKEY)
			break;
		selection = newtListboxGetCurrent(listbox);
		hist_entry__annotate_browser(selection);
	}

	newtFormDestroy(form);
}

int browser__show_help(const char *format, va_list ap)
{
	int ret;
	static int backlog;
	static char msg[1024];

        ret = vsnprintf(msg + backlog, sizeof(msg) - backlog, format, ap);
	backlog += ret;

	if (msg[backlog - 1] == '\n') {
		newtPopHelpLine();
		newtPushHelpLine(msg);
		newtRefresh();
		backlog = 0;
	}

	return ret;
}

void setup_browser(void)
{
	if (!isatty(1))
		return;

	use_browser = true;
	newtInit();
	newtCls();
	newtPushHelpLine(" ");
}

void exit_browser(void)
{
	if (use_browser)
		newtFinished();
}
