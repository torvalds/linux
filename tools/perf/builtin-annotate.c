/*
 * builtin-annotate.c
 *
 * Builtin annotate command: Analyze the perf.data input file,
 * look up and read DSOs and symbol information and display
 * a histogram of results, along various sorting keys.
 */
#include "builtin.h"

#include "util/util.h"

#include "util/color.h"
#include <linux/list.h>
#include "util/cache.h"
#include <linux/rbtree.h>
#include "util/symbol.h"

#include "perf.h"
#include "util/debug.h"

#include "util/event.h"
#include "util/parse-options.h"
#include "util/parse-events.h"
#include "util/thread.h"
#include "util/sort.h"
#include "util/hist.h"
#include "util/session.h"

static char		const *input_name = "perf.data";

static bool		force;

static bool		full_paths;

static bool		print_line;

static const char *sym_hist_filter;

static int hists__add_entry(struct hists *self, struct addr_location *al)
{
	struct hist_entry *he;

	if (sym_hist_filter != NULL &&
	    (al->sym == NULL || strcmp(sym_hist_filter, al->sym->name) != 0)) {
		/* We're only interested in a symbol named sym_hist_filter */
		if (al->sym != NULL) {
			rb_erase(&al->sym->rb_node,
				 &al->map->dso->symbols[al->map->type]);
			symbol__delete(al->sym);
		}
		return 0;
	}

	he = __hists__add_entry(self, al, NULL, 1);
	if (he == NULL)
		return -ENOMEM;

	return hist_entry__inc_addr_samples(he, al->addr);
}

static int process_sample_event(event_t *event, struct perf_session *session)
{
	struct addr_location al;
	struct sample_data data;

	if (event__preprocess_sample(event, session, &al, &data, NULL) < 0) {
		pr_warning("problem processing %d event, skipping it.\n",
			   event->header.type);
		return -1;
	}

	if (!al.filtered && hists__add_entry(&session->hists, &al)) {
		pr_warning("problem incrementing symbol count, "
			   "skipping event\n");
		return -1;
	}

	return 0;
}

static int objdump_line__print(struct objdump_line *self,
			       struct list_head *head,
			       struct hist_entry *he, u64 len)
{
	struct symbol *sym = he->ms.sym;
	static const char *prev_line;
	static const char *prev_color;

	if (self->offset != -1) {
		const char *path = NULL;
		unsigned int hits = 0;
		double percent = 0.0;
		const char *color;
		struct sym_priv *priv = symbol__priv(sym);
		struct sym_ext *sym_ext = priv->ext;
		struct sym_hist *h = priv->hist;
		s64 offset = self->offset;
		struct objdump_line *next = objdump__get_next_ip_line(head, self);

		while (offset < (s64)len &&
		       (next == NULL || offset < next->offset)) {
			if (sym_ext) {
				if (path == NULL)
					path = sym_ext[offset].path;
				percent += sym_ext[offset].percent;
			} else
				hits += h->ip[offset];

			++offset;
		}

		if (sym_ext == NULL && h->sum)
			percent = 100.0 * hits / h->sum;

		color = get_percent_color(percent);

		/*
		 * Also color the filename and line if needed, with
		 * the same color than the percentage. Don't print it
		 * twice for close colored ip with the same filename:line
		 */
		if (path) {
			if (!prev_line || strcmp(prev_line, path)
				       || color != prev_color) {
				color_fprintf(stdout, color, " %s", path);
				prev_line = path;
				prev_color = color;
			}
		}

		color_fprintf(stdout, color, " %7.2f", percent);
		printf(" :	");
		color_fprintf(stdout, PERF_COLOR_BLUE, "%s\n", self->line);
	} else {
		if (!*self->line)
			printf("         :\n");
		else
			printf("         :	%s\n", self->line);
	}

	return 0;
}

static struct rb_root root_sym_ext;

static void insert_source_line(struct sym_ext *sym_ext)
{
	struct sym_ext *iter;
	struct rb_node **p = &root_sym_ext.rb_node;
	struct rb_node *parent = NULL;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct sym_ext, node);

		if (sym_ext->percent > iter->percent)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&sym_ext->node, parent, p);
	rb_insert_color(&sym_ext->node, &root_sym_ext);
}

static void free_source_line(struct hist_entry *he, int len)
{
	struct sym_priv *priv = symbol__priv(he->ms.sym);
	struct sym_ext *sym_ext = priv->ext;
	int i;

	if (!sym_ext)
		return;

	for (i = 0; i < len; i++)
		free(sym_ext[i].path);
	free(sym_ext);

	priv->ext = NULL;
	root_sym_ext = RB_ROOT;
}

/* Get the filename:line for the colored entries */
static void
get_source_line(struct hist_entry *he, int len, const char *filename)
{
	struct symbol *sym = he->ms.sym;
	u64 start;
	int i;
	char cmd[PATH_MAX * 2];
	struct sym_ext *sym_ext;
	struct sym_priv *priv = symbol__priv(sym);
	struct sym_hist *h = priv->hist;

	if (!h->sum)
		return;

	sym_ext = priv->ext = calloc(len, sizeof(struct sym_ext));
	if (!priv->ext)
		return;

	start = he->ms.map->unmap_ip(he->ms.map, sym->start);

	for (i = 0; i < len; i++) {
		char *path = NULL;
		size_t line_len;
		u64 offset;
		FILE *fp;

		sym_ext[i].percent = 100.0 * h->ip[i] / h->sum;
		if (sym_ext[i].percent <= 0.5)
			continue;

		offset = start + i;
		sprintf(cmd, "addr2line -e %s %016llx", filename, offset);
		fp = popen(cmd, "r");
		if (!fp)
			continue;

		if (getline(&path, &line_len, fp) < 0 || !line_len)
			goto next;

		sym_ext[i].path = malloc(sizeof(char) * line_len + 1);
		if (!sym_ext[i].path)
			goto next;

		strcpy(sym_ext[i].path, path);
		insert_source_line(&sym_ext[i]);

	next:
		pclose(fp);
	}
}

static void print_summary(const char *filename)
{
	struct sym_ext *sym_ext;
	struct rb_node *node;

	printf("\nSorted summary for file %s\n", filename);
	printf("----------------------------------------------\n\n");

	if (RB_EMPTY_ROOT(&root_sym_ext)) {
		printf(" Nothing higher than %1.1f%%\n", MIN_GREEN);
		return;
	}

	node = rb_first(&root_sym_ext);
	while (node) {
		double percent;
		const char *color;
		char *path;

		sym_ext = rb_entry(node, struct sym_ext, node);
		percent = sym_ext->percent;
		color = get_percent_color(percent);
		path = sym_ext->path;

		color_fprintf(stdout, color, " %7.2f %s", percent, path);
		node = rb_next(node);
	}
}

static void hist_entry__print_hits(struct hist_entry *self)
{
	struct symbol *sym = self->ms.sym;
	struct sym_priv *priv = symbol__priv(sym);
	struct sym_hist *h = priv->hist;
	u64 len = sym->end - sym->start, offset;

	for (offset = 0; offset < len; ++offset)
		if (h->ip[offset] != 0)
			printf("%*Lx: %Lu\n", BITS_PER_LONG / 2,
			       sym->start + offset, h->ip[offset]);
	printf("%*s: %Lu\n", BITS_PER_LONG / 2, "h->sum", h->sum);
}

static int hist_entry__tty_annotate(struct hist_entry *he)
{
	struct map *map = he->ms.map;
	struct dso *dso = map->dso;
	struct symbol *sym = he->ms.sym;
	const char *filename = dso->long_name, *d_filename;
	u64 len;
	LIST_HEAD(head);
	struct objdump_line *pos, *n;

	if (hist_entry__annotate(he, &head, 0) < 0)
		return -1;

	if (full_paths)
		d_filename = filename;
	else
		d_filename = basename(filename);

	len = sym->end - sym->start;

	if (print_line) {
		get_source_line(he, len, filename);
		print_summary(filename);
	}

	printf("\n\n------------------------------------------------\n");
	printf(" Percent |	Source code & Disassembly of %s\n", d_filename);
	printf("------------------------------------------------\n");

	if (verbose)
		hist_entry__print_hits(he);

	list_for_each_entry_safe(pos, n, &head, node) {
		objdump_line__print(pos, &head, he, len);
		list_del(&pos->node);
		objdump_line__free(pos);
	}

	if (print_line)
		free_source_line(he, len);

	return 0;
}

static void hists__find_annotations(struct hists *self)
{
	struct rb_node *first = rb_first(&self->entries), *nd = first;
	int key = KEY_RIGHT;

	while (nd) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		struct sym_priv *priv;

		if (he->ms.sym == NULL || he->ms.map->dso->annotate_warned)
			goto find_next;

		priv = symbol__priv(he->ms.sym);
		if (priv->hist == NULL) {
find_next:
			if (key == KEY_LEFT)
				nd = rb_prev(nd);
			else
				nd = rb_next(nd);
			continue;
		}

		if (use_browser > 0) {
			key = hist_entry__tui_annotate(he);
			if (is_exit_key(key))
				break;
			switch (key) {
			case KEY_RIGHT:
			case '\t':
				nd = rb_next(nd);
				break;
			case KEY_LEFT:
				if (nd == first)
					continue;
				nd = rb_prev(nd);
			default:
				break;
			}
		} else {
			hist_entry__tty_annotate(he);
			nd = rb_next(nd);
			/*
			 * Since we have a hist_entry per IP for the same
			 * symbol, free he->ms.sym->hist to signal we already
			 * processed this symbol.
			 */
			free(priv->hist);
			priv->hist = NULL;
		}
	}
}

static struct perf_event_ops event_ops = {
	.sample	= process_sample_event,
	.mmap	= event__process_mmap,
	.comm	= event__process_comm,
	.fork	= event__process_task,
};

static int __cmd_annotate(void)
{
	int ret;
	struct perf_session *session;

	session = perf_session__new(input_name, O_RDONLY, force, false);
	if (session == NULL)
		return -ENOMEM;

	ret = perf_session__process_events(session, &event_ops);
	if (ret)
		goto out_delete;

	if (dump_trace) {
		perf_session__fprintf_nr_events(session, stdout);
		goto out_delete;
	}

	if (verbose > 3)
		perf_session__fprintf(session, stdout);

	if (verbose > 2)
		perf_session__fprintf_dsos(session, stdout);

	hists__collapse_resort(&session->hists);
	hists__output_resort(&session->hists);
	hists__find_annotations(&session->hists);
out_delete:
	perf_session__delete(session);

	return ret;
}

static const char * const annotate_usage[] = {
	"perf annotate [<options>] <command>",
	NULL
};

static const struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_STRING('d', "dsos", &symbol_conf.dso_list_str, "dso[,dso...]",
		   "only consider symbols in these dsos"),
	OPT_STRING('s', "symbol", &sym_hist_filter, "symbol",
		    "symbol to annotate"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_BOOLEAN('m', "modules", &symbol_conf.use_modules,
		    "load module symbols - WARNING: use only with -k and LIVE kernel"),
	OPT_BOOLEAN('l', "print-line", &print_line,
		    "print matching source lines (may be slow)"),
	OPT_BOOLEAN('P', "full-paths", &full_paths,
		    "Don't shorten the displayed pathnames"),
	OPT_END()
};

int cmd_annotate(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, options, annotate_usage, 0);

	setup_browser();

	symbol_conf.priv_size = sizeof(struct sym_priv);
	symbol_conf.try_vmlinux_path = true;

	if (symbol__init() < 0)
		return -1;

	setup_sorting(annotate_usage, options);

	if (argc) {
		/*
		 * Special case: if there's an argument left then assume tha
		 * it's a symbol filter:
		 */
		if (argc > 1)
			usage_with_options(annotate_usage, options);

		sym_hist_filter = argv[0];
	}

	if (field_sep && *field_sep == '.') {
		pr_err("'.' is the only non valid --field-separator argument\n");
		return -1;
	}

	return __cmd_annotate();
}
