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

#include "util/parse-options.h"
#include "util/parse-events.h"
#include "util/thread.h"
#include "util/sort.h"
#include "util/hist.h"

static char		const *input_name = "perf.data";

static int		force;
static int		input;

static int		full_paths;

static int		print_line;

static unsigned long	page_size;
static unsigned long	mmap_window = 32;

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

static int symbol_filter(struct map *map, struct symbol *sym)
{
	if (sym_hist_filter == NULL ||
	    strcmp(sym->name, sym_hist_filter) == 0) {
		struct sym_priv *priv = dso__sym_priv(map->dso, sym);
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

	priv = dso__sym_priv(he->map->dso, sym);
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

static int hist_entry__add(struct thread *thread, struct map *map,
			   struct symbol *sym, u64 ip, u64 count, char level)
{
	bool hit;
	struct hist_entry *he = __hist_entry__add(thread, map, sym, NULL, ip,
						  count, level, &hit);
	if (he == NULL)
		return -ENOMEM;
	hist_hit(he, ip);
	return 0;
}

static int
process_sample_event(event_t *event, unsigned long offset, unsigned long head)
{
	char level;
	u64 ip = event->ip.ip;
	struct map *map = NULL;
	struct symbol *sym = NULL;
	struct thread *thread = threads__findnew(event->ip.pid);

	dump_printf("%p [%p]: PERF_EVENT (IP, %d): %d: %p\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->header.misc,
		event->ip.pid,
		(void *)(long)ip);

	if (thread == NULL) {
		fprintf(stderr, "problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	dump_printf(" ... thread: %s:%d\n", thread->comm, thread->pid);

	if (event->header.misc & PERF_RECORD_MISC_KERNEL) {
		level = 'k';
		sym = kernel_maps__find_symbol(ip, &map);
		dump_printf(" ...... dso: %s\n",
			    map ? map->dso->long_name : "<not found>");
	} else if (event->header.misc & PERF_RECORD_MISC_USER) {
		level = '.';
		map = thread__find_map(thread, ip);
		if (map != NULL) {
got_map:
			ip = map->map_ip(map, ip);
			sym = map__find_symbol(map, ip, symbol_filter);
		} else {
			/*
			 * If this is outside of all known maps,
			 * and is a negative address, try to look it
			 * up in the kernel dso, as it might be a
			 * vsyscall or vdso (which executes in user-mode).
			 *
			 * XXX This is nasty, we should have a symbol list in
			 * the "[vdso]" dso, but for now lets use the old
			 * trick of looking in the whole kernel symbol list.
			 */
			if ((long long)ip < 0) {
				map = kernel_map;
				goto got_map;
			}
		}
		dump_printf(" ...... dso: %s\n",
			    map ? map->dso->long_name : "<not found>");
	} else {
		level = 'H';
		dump_printf(" ...... dso: [hypervisor]\n");
	}

	if (hist_entry__add(thread, map, sym, ip, 1, level)) {
		fprintf(stderr, "problem incrementing symbol count, "
				"skipping event\n");
		return -1;
	}
	total++;

	return 0;
}

static int
process_mmap_event(event_t *event, unsigned long offset, unsigned long head)
{
	struct map *map = map__new(&event->mmap, NULL, 0,
				   sizeof(struct sym_priv));
	struct thread *thread = threads__findnew(event->mmap.pid);

	dump_printf("%p [%p]: PERF_RECORD_MMAP %d: [%p(%p) @ %p]: %s\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->mmap.pid,
		(void *)(long)event->mmap.start,
		(void *)(long)event->mmap.len,
		(void *)(long)event->mmap.pgoff,
		event->mmap.filename);

	if (thread == NULL || map == NULL) {
		dump_printf("problem processing PERF_RECORD_MMAP, skipping event.\n");
		return 0;
	}

	thread__insert_map(thread, map);
	total_mmap++;

	return 0;
}

static int
process_comm_event(event_t *event, unsigned long offset, unsigned long head)
{
	struct thread *thread = threads__findnew(event->comm.pid);

	dump_printf("%p [%p]: PERF_RECORD_COMM: %s:%d\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->comm.comm, event->comm.pid);

	if (thread == NULL ||
	    thread__set_comm(thread, event->comm.comm)) {
		dump_printf("problem processing PERF_RECORD_COMM, skipping event.\n");
		return -1;
	}
	total_comm++;

	return 0;
}

static int
process_fork_event(event_t *event, unsigned long offset, unsigned long head)
{
	struct thread *thread = threads__findnew(event->fork.pid);
	struct thread *parent = threads__findnew(event->fork.ppid);

	dump_printf("%p [%p]: PERF_RECORD_FORK: %d:%d\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->fork.pid, event->fork.ppid);

	/*
	 * A thread clone will have the same PID for both
	 * parent and child.
	 */
	if (thread == parent)
		return 0;

	if (!thread || !parent || thread__fork(thread, parent)) {
		dump_printf("problem processing PERF_RECORD_FORK, skipping event.\n");
		return -1;
	}
	total_fork++;

	return 0;
}

static int
process_event(event_t *event, unsigned long offset, unsigned long head)
{
	switch (event->header.type) {
	case PERF_RECORD_SAMPLE:
		return process_sample_event(event, offset, head);

	case PERF_RECORD_MMAP:
		return process_mmap_event(event, offset, head);

	case PERF_RECORD_COMM:
		return process_comm_event(event, offset, head);

	case PERF_RECORD_FORK:
		return process_fork_event(event, offset, head);
	/*
	 * We dont process them right now but they are fine:
	 */

	case PERF_RECORD_THROTTLE:
	case PERF_RECORD_UNTHROTTLE:
		return 0;

	default:
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
		struct sym_priv *priv = dso__sym_priv(he->map->dso, sym);
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
	struct sym_priv *priv = dso__sym_priv(he->map->dso, he->sym);
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
	struct sym_priv *priv = dso__sym_priv(he->map->dso, sym);
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

static void find_annotations(void)
{
	struct rb_node *nd;

	for (nd = rb_first(&output_hists); nd; nd = rb_next(nd)) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		struct sym_priv *priv;

		if (he->sym == NULL)
			continue;

		priv = dso__sym_priv(he->map->dso, he->sym);
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

static int __cmd_annotate(void)
{
	int ret, rc = EXIT_FAILURE;
	unsigned long offset = 0;
	unsigned long head = 0;
	struct stat input_stat;
	event_t *event;
	uint32_t size;
	char *buf;

	register_idle_thread();

	input = open(input_name, O_RDONLY);
	if (input < 0) {
		perror("failed to open file");
		exit(-1);
	}

	ret = fstat(input, &input_stat);
	if (ret < 0) {
		perror("failed to stat file");
		exit(-1);
	}

	if (!force && input_stat.st_uid && (input_stat.st_uid != geteuid())) {
		fprintf(stderr, "file: %s not owned by current user or root\n", input_name);
		exit(-1);
	}

	if (!input_stat.st_size) {
		fprintf(stderr, "zero-sized file, nothing to do!\n");
		exit(0);
	}

	if (load_kernel(sizeof(struct sym_priv), symbol_filter) < 0) {
		perror("failed to load kernel symbols");
		return EXIT_FAILURE;
	}

remap:
	buf = (char *)mmap(NULL, page_size * mmap_window, PROT_READ,
			   MAP_SHARED, input, offset);
	if (buf == MAP_FAILED) {
		perror("failed to mmap file");
		exit(-1);
	}

more:
	event = (event_t *)(buf + head);

	size = event->header.size;
	if (!size)
		size = 8;

	if (head + event->header.size >= page_size * mmap_window) {
		unsigned long shift = page_size * (head / page_size);
		int munmap_ret;

		munmap_ret = munmap(buf, page_size * mmap_window);
		assert(munmap_ret == 0);

		offset += shift;
		head -= shift;
		goto remap;
	}

	size = event->header.size;

	dump_printf("%p [%p]: event: %d\n",
			(void *)(offset + head),
			(void *)(long)event->header.size,
			event->header.type);

	if (!size || process_event(event, offset, head) < 0) {

		dump_printf("%p [%p]: skipping unknown header type: %d\n",
			(void *)(offset + head),
			(void *)(long)(event->header.size),
			event->header.type);

		total_unknown++;

		/*
		 * assume we lost track of the stream, check alignment, and
		 * increment a single u64 in the hope to catch on again 'soon'.
		 */

		if (unlikely(head & 7))
			head &= ~7ULL;

		size = 8;
	}

	head += size;

	if (offset + head < (unsigned long)input_stat.st_size)
		goto more;

	rc = EXIT_SUCCESS;
	close(input);

	dump_printf("      IP events: %10ld\n", total);
	dump_printf("    mmap events: %10ld\n", total_mmap);
	dump_printf("    comm events: %10ld\n", total_comm);
	dump_printf("    fork events: %10ld\n", total_fork);
	dump_printf(" unknown events: %10ld\n", total_unknown);

	if (dump_trace)
		return 0;

	if (verbose > 3)
		threads__fprintf(stdout);

	if (verbose > 2)
		dsos__fprintf(stdout);

	collapse__resort();
	output__resort(total);

	find_annotations();

	return rc;
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
	OPT_STRING('k', "vmlinux", &vmlinux_name, "file", "vmlinux pathname"),
	OPT_BOOLEAN('m', "modules", &modules,
		    "load module symbols - WARNING: use only with -k and LIVE kernel"),
	OPT_BOOLEAN('l', "print-line", &print_line,
		    "print matching source lines (may be slow)"),
	OPT_BOOLEAN('P', "full-paths", &full_paths,
		    "Don't shorten the displayed pathnames"),
	OPT_END()
};

static void setup_sorting(void)
{
	char *tmp, *tok, *str = strdup(sort_order);

	for (tok = strtok_r(str, ", ", &tmp);
			tok; tok = strtok_r(NULL, ", ", &tmp)) {
		if (sort_dimension__add(tok) < 0) {
			error("Unknown --sort key: `%s'", tok);
			usage_with_options(annotate_usage, options);
		}
	}

	free(str);
}

int cmd_annotate(int argc, const char **argv, const char *prefix __used)
{
	symbol__init();

	page_size = getpagesize();

	argc = parse_options(argc, argv, options, annotate_usage, 0);

	setup_sorting();

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
