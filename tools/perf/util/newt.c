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

struct ui_progress {
	newtComponent form, scale;
};

struct ui_progress *ui_progress__new(const char *title, u64 total)
{
	struct ui_progress *self = malloc(sizeof(*self));

	if (self != NULL) {
		int cols;
		newtGetScreenSize(&cols, NULL);
		cols -= 4;
		newtCenteredWindow(cols, 1, title);
		self->form  = newtForm(NULL, NULL, 0);
		if (self->form == NULL)
			goto out_free_self;
		self->scale = newtScale(0, 0, cols, total);
		if (self->scale == NULL)
			goto out_free_form;
		newtFormAddComponents(self->form, self->scale, NULL);
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
	newtScaleSet(self->scale, curr);
	newtRefresh();
}

void ui_progress__delete(struct ui_progress *self)
{
	newtFormDestroy(self->form);
	newtPopWindow();
	free(self);
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
		newtPopHelpLine();
		newtPushHelpLine(browser__last_msg);
		newtRefresh();
		backlog = 0;
	}

	return ret;
}

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

static int popup_menu(int argc, const char *argv[])
{
	struct newtExitStruct es;
	int i, rc = -1, max_len = 5;
	newtComponent listbox, form = newt_form__new();

	if (form == NULL)
		return -1;

	listbox = newtListbox(0, 0, argc, NEWT_FLAG_RETURNEXIT);
	if (listbox == NULL)
		goto out_destroy_form;

	newtFormAddComponents(form, listbox, NULL);

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

static bool dialog_yesno(const char *msg)
{
	/* newtWinChoice should really be accepting const char pointers... */
	char yes[] = "Yes", no[] = "No";
	return newtWinChoice(NULL, no, yes, (char *)msg) == 2;
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

static void map_symbol__annotate_browser(const struct map_symbol *self)
{
	FILE *fp;
	int cols, rows;
	newtComponent form, tree;
	struct newtExitStruct es;
	char *str;
	size_t line_len, max_line_len = 0;
	size_t max_usable_width;
	char *line = NULL;

	if (self->sym == NULL)
		return;

	if (asprintf(&str, "perf annotate -d \"%s\" %s 2>&1 | expand",
		     self->map->dso->name, self->sym->name) < 0)
		return;

	fp = popen(str, "r");
	if (fp == NULL)
		goto out_free_str;

	newtPushHelpLine("Press ESC to exit");
	newtGetScreenSize(&cols, &rows);
	tree = newtListbox(0, 0, rows - 5, NEWT_FLAG_SCROLL);

	while (!feof(fp)) {
		if (getline(&line, &line_len, fp) < 0 || !line_len)
			break;
		while (line_len != 0 && isspace(line[line_len - 1]))
			line[--line_len] = '\0';

		if (line_len > max_line_len)
			max_line_len = line_len;
		newtListboxAppendEntry(tree, line, NULL);
	}
	fclose(fp);
	free(line);

	max_usable_width = cols - 22;
	if (max_line_len > max_usable_width)
		max_line_len = max_usable_width;

	newtListboxSetWidth(tree, max_line_len);

	newtCenteredWindow(max_line_len + 2, rows - 5, self->sym->name);
	form = newt_form__new();
	newtFormAddComponents(form, tree, NULL);

	newtFormRun(form, &es);
	newtFormDestroy(form);
	newtPopWindow();
	newtPopHelpLine();
out_free_str:
	free(str);
}

static const void *newt__symbol_tree_get_current(newtComponent self)
{
	if (symbol_conf.use_callchain)
		return newtCheckboxTreeGetCurrent(self);
	return newtListboxGetCurrent(self);
}

static void perf_session__selection(newtComponent self, void *data)
{
	const struct map_symbol **symbol_ptr = data;
	*symbol_ptr = newt__symbol_tree_get_current(self);
}

int perf_session__browse_hists(struct rb_root *hists, u64 nr_hists,
			       u64 session_total, const char *helpline)
{
	struct sort_entry *se;
	struct rb_node *nd;
	char seq[] = ".";
	unsigned int width;
	char *col_width = symbol_conf.col_width_list_str;
	int rows, cols, idx;
	int max_len = 0;
	char str[1024];
	newtComponent form, tree;
	struct newtExitStruct es;
	const struct map_symbol *selection;
	u64 curr_hist = 0;
	struct ui_progress *progress;

	progress = ui_progress__new("Adding entries to the browser...", nr_hists);
	if (progress == NULL)
		return -1;

	snprintf(str, sizeof(str), "Samples: %Ld", session_total);
	newtDrawRootText(0, 0, str);
	newtPushHelpLine(helpline);

	newtGetScreenSize(&cols, &rows);

	if (symbol_conf.use_callchain)
		tree = newtCheckboxTreeMulti(0, 0, rows - 5, seq,
						NEWT_FLAG_SCROLL);
	else
		tree = newtListbox(0, 0, rows - 5, (NEWT_FLAG_SCROLL |
						       NEWT_FLAG_RETURNEXIT));

	newtComponentAddCallback(tree, perf_session__selection, &selection);

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

	idx = 0;
	for (nd = rb_first(hists); nd; nd = rb_next(nd)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		int len = hist_entry__append_browser(h, tree, session_total);
		if (len > max_len)
			max_len = len;
		if (symbol_conf.use_callchain)
			hist_entry__append_callchain_browser(h, tree, session_total, idx++);
		++curr_hist;
		if (curr_hist % 5)
			ui_progress__update(progress, curr_hist);
	}

	ui_progress__delete(progress);

	if (max_len > cols)
		max_len = cols - 3;

	if (!symbol_conf.use_callchain)
		newtListboxSetWidth(tree, max_len);

	newtCenteredWindow(max_len + (symbol_conf.use_callchain ? 5 : 0),
			   rows - 5, "Report");
	form = newt_form__new();
	newtFormAddHotKey(form, 'A');
	newtFormAddHotKey(form, 'a');
	newtFormAddHotKey(form, NEWT_KEY_RIGHT);
	newtFormAddComponents(form, tree, NULL);
	selection = newt__symbol_tree_get_current(tree);

	while (1) {
		char annotate[512];
		const char *options[2];
		int nr_options = 0, choice = 0;

		newtFormRun(form, &es);
		if (es.reason == NEWT_EXIT_HOTKEY) {
			if (toupper(es.u.key) == 'A')
				goto do_annotate;
			if (es.u.key == NEWT_KEY_ESCAPE ||
			    toupper(es.u.key) == 'Q' ||
			    es.u.key == CTRL('c')) {
				if (dialog_yesno("Do you really want to exit?"))
					break;
				else
					continue;
			}
		}

		if (selection->sym != NULL) {
			snprintf(annotate, sizeof(annotate),
				 "Annotate %s", selection->sym->name);
			options[nr_options++] = annotate;
		}

		options[nr_options++] = "Exit";
		choice = popup_menu(nr_options, options);
		if (choice == nr_options - 1)
			break;
do_annotate:
		if (selection->sym != NULL && choice >= 0) {
			if (selection->map->dso->origin == DSO__ORIG_KERNEL) {
				newtPopHelpLine();
				newtPushHelpLine("No vmlinux file found, can't "
						 "annotate with just a "
						 "kallsyms file");
				continue;
			}
			map_symbol__annotate_browser(selection);
		}
	}

	newtFormDestroy(form);
	newtPopWindow();
	return 0;
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

void exit_browser(bool wait_for_ok)
{
	if (use_browser) {
		if (wait_for_ok) {
			char title[] = "Fatal Error", ok[] = "Ok";
			newtWinMessage(title, ok, browser__last_msg);
		}
		newtFinished();
	}
}
