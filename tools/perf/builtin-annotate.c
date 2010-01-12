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
#include "util/string.h"

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

static int		force;

static int		full_paths;

static int		print_line;

struct sym_hist {
	u64		sum;
	u64		ip[0];
};

struct sym_ext {
	struct rb_node	node;
	double		percent;
	char		*path;
};

struct sym_priv {
	struct sym_hist	*hist;
	struct sym_ext	*ext;
};

static const char *sym_hist_filter;

static int symbol_filter(struct map *map __used, struct symbol *sym)
{
	if (sym_hist_filter == NULL ||
	    strcmp(sym->name, sym_hist_filter) == 0) {
		struct sym_priv *priv = symbol__priv(sym);
		const int size = (sizeof(*priv->hist) +
				  (sym->end - sym->start) * sizeof(u64));

		priv->hist = malloc(size);
		if (priv->hist)
			memset(priv->hist, 0, size);
		return 0;
	}
	/*
	 * FIXME: We should really filter it out, as we don't want to go thru symbols
	 * we're not interested, and if a DSO ends up with no symbols, delete it too,
	 * but right now the kernel loading routines in symbol.c bail out if no symbols
	 * are found, fix it later.
	 */
	return 0;
}

/*
 * collect histogram counts
 */
static void hist_hit(struct hist_entry *he, u64 ip)
{
	unsigned int sym_size, offset;
	struct symbol *sym = he->sym;
	struct sym_priv *priv;
	struct sym_hist *h;

	he->count++;

	if (!sym || !he->map)
		return;

	priv = symbol__priv(sym);
	if (!priv->hist)
		return;

	sym_size = sym->end - sym->start;
	offset = ip - sym->start;

	if (verbose)
		fprintf(stderr, "%s: ip=%Lx\n", __func__,
			he->map->unmap_ip(he->map, ip));

	if (offset >= sym_size)
		return;

	h = priv->hist;
	h->sum++;
	h->ip[offset]++;

	if (verbose >= 3)
		printf("%p %s: count++ [ip: %p, %08Lx] => %Ld\n",
			(void *)(unsigned long)he->sym->start,
			he->sym->name,
			(void *)(unsigned long)ip, ip - he->sym->start,
			h->ip[offset]);
}

static int perf_session__add_hist_entry(struct perf_session *self,
					struct addr_location *al, u64 count)
{
	bool hit;
	struct hist_entry *he = __perf_session__add_hist_entry(self, al, NULL,
							       count, &hit);
	if (he == NULL)
		return -ENOMEM;
	hist_hit(he, al->addr);
	return 0;
}

static int process_sample_event(event_t *event, struct perf_session *session)
{
	struct addr_location al;

	dump_printf("(IP, %d): %d: %p\n", event->header.misc,
		    event->ip.pid, (void *)(long)event->ip.ip);

	if (event__preprocess_sample(event, session, &al, symbol_filter) < 0) {
		fprintf(stderr, "problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	if (!al.filtered && perf_session__add_hist_entry(session, &al, 1)) {
		fprintf(stderr, "problem incrementing symbol count, "
				"skipping event\n");
		return -1;
	}

	return 0;
}

static int parse_line(FILE *file, struct hist_entry *he, u64 len)
{
	struct symbol *sym = he->sym;
	char *line = NULL, *tmp, *tmp2;
	static const char *prev_line;
	static const char *prev_color;
	unsigned int offset;
	size_t line_len;
	u64 start;
	s64 line_ip;
	int ret;
	char *c;

	if (getline(&line, &line_len, file) < 0)
		return -1;
	if (!line)
		return -1;

	c = strchr(line, '\n');
	if (c)
		*c = 0;

	line_ip = -1;
	offset = 0;
	ret = -2;

	/*
	 * Strip leading spaces:
	 */
	tmp = line;
	while (*tmp) {
		if (*tmp != ' ')
			break;
		tmp++;
	}

	if (*tmp) {
		/*
		 * Parse hexa addresses followed by ':'
		 */
		line_ip = strtoull(tmp, &tmp2, 16);
		if (*tmp2 != ':')
			line_ip = -1;
	}

	start = he->map->unmap_ip(he->map, sym->start);

	if (line_ip != -1) {
		const char *path = NULL;
		unsigned int hits = 0;
		double percent = 0.0;
		const char *color;
		struct sym_priv *priv = symbol__priv(sym);
		struct sym_ext *sym_ext = priv->ext;
		struct sym_hist *h = priv->hist;

		offset = line_ip - start;
		if (offset < len)
			hits = h->ip[offset];

		if (offset < len && sym_ext) {
			path = sym_ext[offset].path;
			percent = sym_ext[offset].percent;
		} else if (h->sum)
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
		color_fprintf(stdout, PERF_COLOR_BLUE, "%s\n", line);
	} else {
		if (!*line)
			printf("         :\n");
		else
			printf("         :	%s\n", line);
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
	struct sym_priv *priv = symbol__priv(he->sym);
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
	struct symbol *sym = he->sym;
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

	start = he->map->unmap_ip(he->map, sym->start);

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

static void annotate_sym(struct hist_entry *he)
{
	struct map *map = he->map;
	struct dso *dso = map->dso;
	struct symbol *sym = he->sym;
	const char *filename = dso->long_name, *d_filename;
	u64 len;
	char command[PATH_MAX*2];
	FILE *file;

	if (!filename)
		return;

	if (verbose)
		fprintf(stderr, "%s: filename=%s, sym=%s, start=%Lx, end=%Lx\n",
			__func__, filename, sym->name,
			map->unmap_ip(map, sym->start),
			map->unmap_ip(map, sym->end));

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

	if (verbose >= 2)
		printf("annotating [%p] %30s : [%p] %30s\n",
		       dso, dso->long_name, sym, sym->name);

	sprintf(command, "objdump --start-address=0x%016Lx --stop-address=0x%016Lx -dS %s|grep -v %s",
		map->unmap_ip(map, sym->start), map->unmap_ip(map, sym->end),
		filename, filename);

	if (verbose >= 3)
		printf("doing: %s\n", command);

	file = popen(command, "r");
	if (!file)
		return;

	while (!feof(file)) {
		if (parse_line(file, he, len) < 0)
			break;
	}

	pclose(file);
	if (print_line)
		free_source_line(he, len);
}

static void perf_session__find_annotations(struct perf_session *self)
{
	struct rb_node *nd;

	for (nd = rb_first(&self->hists); nd; nd = rb_next(nd)) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		struct sym_priv *priv;

		if (he->sym == NULL)
			continue;

		priv = symbol__priv(he->sym);
		if (priv->hist == NULL)
			continue;

		annotate_sym(he);
		/*
		 * Since we have a hist_entry per IP for the same symbol, free
		 * he->sym->hist to signal we already processed this symbol.
		 */
		free(priv->hist);
		priv->hist = NULL;
	}
}

static struct perf_event_ops event_ops = {
	.process_sample_event	= process_sample_event,
	.process_mmap_event	= event__process_mmap,
	.process_comm_event	= event__process_comm,
	.process_fork_event	= event__process_task,
};

static int __cmd_annotate(void)
{
	int ret;
	struct perf_session *session;

	session = perf_session__new(input_name, O_RDONLY, force);
	if (session == NULL)
		return -ENOMEM;

	ret = perf_session__process_events(session, &event_ops);
	if (ret)
		goto out_delete;

	if (dump_trace) {
		event__print_totals();
		goto out_delete;
	}

	if (verbose > 3)
		perf_session__fprintf(session, stdout);

	if (verbose > 2)
		dsos__fprintf(stdout);

	perf_session__collapse_resort(session);
	perf_session__output_resort(session, session->event_total[0]);
	perf_session__find_annotations(session);
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
	OPT_STRING('s', "symbol", &sym_hist_filter, "symbol",
		    "symbol to annotate"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_BOOLEAN('v', "verbose", &verbose,
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

	setup_pager();

	if (field_sep && *field_sep == '.') {
		fputs("'.' is the only non valid --field-separator argument\n",
				stderr);
		exit(129);
	}

	return __cmd_annotate();
}
