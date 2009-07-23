/*
 * builtin-report.c
 *
 * Builtin report command: Analyze the perf.data input file,
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
#include "util/callchain.h"
#include "util/strlist.h"

#include "perf.h"
#include "util/header.h"

#include "util/parse-options.h"
#include "util/parse-events.h"

#define SHOW_KERNEL	1
#define SHOW_USER	2
#define SHOW_HV		4

static char		const *input_name = "perf.data";
static char		*vmlinux = NULL;

static char		default_sort_order[] = "comm,dso";
static char		*sort_order = default_sort_order;
static char		*dso_list_str, *comm_list_str, *sym_list_str,
			*col_width_list_str;
static struct strlist	*dso_list, *comm_list, *sym_list;
static char		*field_sep;

static int		input;
static int		show_mask = SHOW_KERNEL | SHOW_USER | SHOW_HV;

static int		dump_trace = 0;
#define dprintf(x...)	do { if (dump_trace) printf(x); } while (0)
#define cdprintf(x...)	do { if (dump_trace) color_fprintf(stdout, color, x); } while (0)

static int		verbose;
#define eprintf(x...)	do { if (verbose) fprintf(stderr, x); } while (0)

static int		modules;

static int		full_paths;
static int		show_nr_samples;

static unsigned long	page_size;
static unsigned long	mmap_window = 32;

static char		default_parent_pattern[] = "^sys_|^do_page_fault";
static char		*parent_pattern = default_parent_pattern;
static regex_t		parent_regex;

static int		exclude_other = 1;

static char		callchain_default_opt[] = "fractal,0.5";

static int		callchain;

static
struct callchain_param	callchain_param = {
	.mode	= CHAIN_GRAPH_ABS,
	.min_percent = 0.5
};

static u64		sample_type;

struct ip_event {
	struct perf_event_header header;
	u64 ip;
	u32 pid, tid;
	unsigned char __more_data[];
};

struct mmap_event {
	struct perf_event_header header;
	u32 pid, tid;
	u64 start;
	u64 len;
	u64 pgoff;
	char filename[PATH_MAX];
};

struct comm_event {
	struct perf_event_header header;
	u32 pid, tid;
	char comm[16];
};

struct fork_event {
	struct perf_event_header header;
	u32 pid, ppid;
};

struct lost_event {
	struct perf_event_header header;
	u64 id;
	u64 lost;
};

struct read_event {
	struct perf_event_header header;
	u32 pid,tid;
	u64 value;
	u64 format[3];
};

typedef union event_union {
	struct perf_event_header	header;
	struct ip_event			ip;
	struct mmap_event		mmap;
	struct comm_event		comm;
	struct fork_event		fork;
	struct lost_event		lost;
	struct read_event		read;
} event_t;

static int repsep_fprintf(FILE *fp, const char *fmt, ...)
{
	int n;
	va_list ap;

	va_start(ap, fmt);
	if (!field_sep)
		n = vfprintf(fp, fmt, ap);
	else {
		char *bf = NULL;
		n = vasprintf(&bf, fmt, ap);
		if (n > 0) {
			char *sep = bf;
			while (1) {
				sep = strchr(sep, *field_sep);
				if (sep == NULL)
					break;
				*sep = '.';
			}
		}
		fputs(bf, fp);
		free(bf);
	}
	va_end(ap);
	return n;
}

static LIST_HEAD(dsos);
static struct dso *kernel_dso;
static struct dso *vdso;
static struct dso *hypervisor_dso;

static void dsos__add(struct dso *dso)
{
	list_add_tail(&dso->node, &dsos);
}

static struct dso *dsos__find(const char *name)
{
	struct dso *pos;

	list_for_each_entry(pos, &dsos, node)
		if (strcmp(pos->name, name) == 0)
			return pos;
	return NULL;
}

static struct dso *dsos__findnew(const char *name)
{
	struct dso *dso = dsos__find(name);
	int nr;

	if (dso)
		return dso;

	dso = dso__new(name, 0);
	if (!dso)
		goto out_delete_dso;

	nr = dso__load(dso, NULL, verbose);
	if (nr < 0) {
		eprintf("Failed to open: %s\n", name);
		goto out_delete_dso;
	}
	if (!nr)
		eprintf("No symbols found in: %s, maybe install a debug package?\n", name);

	dsos__add(dso);

	return dso;

out_delete_dso:
	dso__delete(dso);
	return NULL;
}

static void dsos__fprintf(FILE *fp)
{
	struct dso *pos;

	list_for_each_entry(pos, &dsos, node)
		dso__fprintf(pos, fp);
}

static struct symbol *vdso__find_symbol(struct dso *dso, u64 ip)
{
	return dso__find_symbol(dso, ip);
}

static int load_kernel(void)
{
	int err;

	kernel_dso = dso__new("[kernel]", 0);
	if (!kernel_dso)
		return -1;

	err = dso__load_kernel(kernel_dso, vmlinux, NULL, verbose, modules);
	if (err <= 0) {
		dso__delete(kernel_dso);
		kernel_dso = NULL;
	} else
		dsos__add(kernel_dso);

	vdso = dso__new("[vdso]", 0);
	if (!vdso)
		return -1;

	vdso->find_symbol = vdso__find_symbol;

	dsos__add(vdso);

	hypervisor_dso = dso__new("[hypervisor]", 0);
	if (!hypervisor_dso)
		return -1;
	dsos__add(hypervisor_dso);

	return err;
}

static char __cwd[PATH_MAX];
static char *cwd = __cwd;
static int cwdlen;

static int strcommon(const char *pathname)
{
	int n = 0;

	while (pathname[n] == cwd[n] && n < cwdlen)
		++n;

	return n;
}

struct map {
	struct list_head node;
	u64	 start;
	u64	 end;
	u64	 pgoff;
	u64	 (*map_ip)(struct map *, u64);
	struct dso	 *dso;
};

static u64 map__map_ip(struct map *map, u64 ip)
{
	return ip - map->start + map->pgoff;
}

static u64 vdso__map_ip(struct map *map __used, u64 ip)
{
	return ip;
}

static inline int is_anon_memory(const char *filename)
{
	return strcmp(filename, "//anon") == 0;
}

static struct map *map__new(struct mmap_event *event)
{
	struct map *self = malloc(sizeof(*self));

	if (self != NULL) {
		const char *filename = event->filename;
		char newfilename[PATH_MAX];
		int anon;

		if (cwd) {
			int n = strcommon(filename);

			if (n == cwdlen) {
				snprintf(newfilename, sizeof(newfilename),
					 ".%s", filename + n);
				filename = newfilename;
			}
		}

		anon = is_anon_memory(filename);

		if (anon) {
			snprintf(newfilename, sizeof(newfilename), "/tmp/perf-%d.map", event->pid);
			filename = newfilename;
		}

		self->start = event->start;
		self->end   = event->start + event->len;
		self->pgoff = event->pgoff;

		self->dso = dsos__findnew(filename);
		if (self->dso == NULL)
			goto out_delete;

		if (self->dso == vdso || anon)
			self->map_ip = vdso__map_ip;
		else
			self->map_ip = map__map_ip;
	}
	return self;
out_delete:
	free(self);
	return NULL;
}

static struct map *map__clone(struct map *self)
{
	struct map *map = malloc(sizeof(*self));

	if (!map)
		return NULL;

	memcpy(map, self, sizeof(*self));

	return map;
}

static int map__overlap(struct map *l, struct map *r)
{
	if (l->start > r->start) {
		struct map *t = l;
		l = r;
		r = t;
	}

	if (l->end > r->start)
		return 1;

	return 0;
}

static size_t map__fprintf(struct map *self, FILE *fp)
{
	return fprintf(fp, " %Lx-%Lx %Lx %s\n",
		       self->start, self->end, self->pgoff, self->dso->name);
}


struct thread {
	struct rb_node	 rb_node;
	struct list_head maps;
	pid_t		 pid;
	char		 *comm;
};

static struct thread *thread__new(pid_t pid)
{
	struct thread *self = malloc(sizeof(*self));

	if (self != NULL) {
		self->pid = pid;
		self->comm = malloc(32);
		if (self->comm)
			snprintf(self->comm, 32, ":%d", self->pid);
		INIT_LIST_HEAD(&self->maps);
	}

	return self;
}

static unsigned int dsos__col_width,
		    comms__col_width,
		    threads__col_width;

static int thread__set_comm(struct thread *self, const char *comm)
{
	if (self->comm)
		free(self->comm);
	self->comm = strdup(comm);
	if (!self->comm)
		return -ENOMEM;

	if (!col_width_list_str && !field_sep &&
	    (!comm_list || strlist__has_entry(comm_list, comm))) {
		unsigned int slen = strlen(comm);
		if (slen > comms__col_width) {
			comms__col_width = slen;
			threads__col_width = slen + 6;
		}
	}

	return 0;
}

static size_t thread__fprintf(struct thread *self, FILE *fp)
{
	struct map *pos;
	size_t ret = fprintf(fp, "Thread %d %s\n", self->pid, self->comm);

	list_for_each_entry(pos, &self->maps, node)
		ret += map__fprintf(pos, fp);

	return ret;
}


static struct rb_root threads;
static struct thread *last_match;

static struct thread *threads__findnew(pid_t pid)
{
	struct rb_node **p = &threads.rb_node;
	struct rb_node *parent = NULL;
	struct thread *th;

	/*
	 * Font-end cache - PID lookups come in blocks,
	 * so most of the time we dont have to look up
	 * the full rbtree:
	 */
	if (last_match && last_match->pid == pid)
		return last_match;

	while (*p != NULL) {
		parent = *p;
		th = rb_entry(parent, struct thread, rb_node);

		if (th->pid == pid) {
			last_match = th;
			return th;
		}

		if (pid < th->pid)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	th = thread__new(pid);
	if (th != NULL) {
		rb_link_node(&th->rb_node, parent, p);
		rb_insert_color(&th->rb_node, &threads);
		last_match = th;
	}

	return th;
}

static void thread__insert_map(struct thread *self, struct map *map)
{
	struct map *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, &self->maps, node) {
		if (map__overlap(pos, map)) {
			if (verbose >= 2) {
				printf("overlapping maps:\n");
				map__fprintf(map, stdout);
				map__fprintf(pos, stdout);
			}

			if (map->start <= pos->start && map->end > pos->start)
				pos->start = map->end;

			if (map->end >= pos->end && map->start < pos->end)
				pos->end = map->start;

			if (verbose >= 2) {
				printf("after collision:\n");
				map__fprintf(pos, stdout);
			}

			if (pos->start >= pos->end) {
				list_del_init(&pos->node);
				free(pos);
			}
		}
	}

	list_add_tail(&map->node, &self->maps);
}

static int thread__fork(struct thread *self, struct thread *parent)
{
	struct map *map;

	if (self->comm)
		free(self->comm);
	self->comm = strdup(parent->comm);
	if (!self->comm)
		return -ENOMEM;

	list_for_each_entry(map, &parent->maps, node) {
		struct map *new = map__clone(map);
		if (!new)
			return -ENOMEM;
		thread__insert_map(self, new);
	}

	return 0;
}

static struct map *thread__find_map(struct thread *self, u64 ip)
{
	struct map *pos;

	if (self == NULL)
		return NULL;

	list_for_each_entry(pos, &self->maps, node)
		if (ip >= pos->start && ip <= pos->end)
			return pos;

	return NULL;
}

static size_t threads__fprintf(FILE *fp)
{
	size_t ret = 0;
	struct rb_node *nd;

	for (nd = rb_first(&threads); nd; nd = rb_next(nd)) {
		struct thread *pos = rb_entry(nd, struct thread, rb_node);

		ret += thread__fprintf(pos, fp);
	}

	return ret;
}

/*
 * histogram, sorted on item, collects counts
 */

static struct rb_root hist;

struct hist_entry {
	struct rb_node		rb_node;

	struct thread		*thread;
	struct map		*map;
	struct dso		*dso;
	struct symbol		*sym;
	struct symbol		*parent;
	u64			ip;
	char			level;
	struct callchain_node	callchain;
	struct rb_root		sorted_chain;

	u64			count;
};

/*
 * configurable sorting bits
 */

struct sort_entry {
	struct list_head list;

	char *header;

	int64_t (*cmp)(struct hist_entry *, struct hist_entry *);
	int64_t (*collapse)(struct hist_entry *, struct hist_entry *);
	size_t	(*print)(FILE *fp, struct hist_entry *, unsigned int width);
	unsigned int *width;
	bool	elide;
};

static int64_t cmp_null(void *l, void *r)
{
	if (!l && !r)
		return 0;
	else if (!l)
		return -1;
	else
		return 1;
}

/* --sort pid */

static int64_t
sort__thread_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return right->thread->pid - left->thread->pid;
}

static size_t
sort__thread_print(FILE *fp, struct hist_entry *self, unsigned int width)
{
	return repsep_fprintf(fp, "%*s:%5d", width - 6,
			      self->thread->comm ?: "", self->thread->pid);
}

static struct sort_entry sort_thread = {
	.header = "Command:  Pid",
	.cmp	= sort__thread_cmp,
	.print	= sort__thread_print,
	.width	= &threads__col_width,
};

/* --sort comm */

static int64_t
sort__comm_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return right->thread->pid - left->thread->pid;
}

static int64_t
sort__comm_collapse(struct hist_entry *left, struct hist_entry *right)
{
	char *comm_l = left->thread->comm;
	char *comm_r = right->thread->comm;

	if (!comm_l || !comm_r)
		return cmp_null(comm_l, comm_r);

	return strcmp(comm_l, comm_r);
}

static size_t
sort__comm_print(FILE *fp, struct hist_entry *self, unsigned int width)
{
	return repsep_fprintf(fp, "%*s", width, self->thread->comm);
}

static struct sort_entry sort_comm = {
	.header		= "Command",
	.cmp		= sort__comm_cmp,
	.collapse	= sort__comm_collapse,
	.print		= sort__comm_print,
	.width		= &comms__col_width,
};

/* --sort dso */

static int64_t
sort__dso_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct dso *dso_l = left->dso;
	struct dso *dso_r = right->dso;

	if (!dso_l || !dso_r)
		return cmp_null(dso_l, dso_r);

	return strcmp(dso_l->name, dso_r->name);
}

static size_t
sort__dso_print(FILE *fp, struct hist_entry *self, unsigned int width)
{
	if (self->dso)
		return repsep_fprintf(fp, "%-*s", width, self->dso->name);

	return repsep_fprintf(fp, "%*llx", width, (u64)self->ip);
}

static struct sort_entry sort_dso = {
	.header = "Shared Object",
	.cmp	= sort__dso_cmp,
	.print	= sort__dso_print,
	.width	= &dsos__col_width,
};

/* --sort symbol */

static int64_t
sort__sym_cmp(struct hist_entry *left, struct hist_entry *right)
{
	u64 ip_l, ip_r;

	if (left->sym == right->sym)
		return 0;

	ip_l = left->sym ? left->sym->start : left->ip;
	ip_r = right->sym ? right->sym->start : right->ip;

	return (int64_t)(ip_r - ip_l);
}

static size_t
sort__sym_print(FILE *fp, struct hist_entry *self, unsigned int width __used)
{
	size_t ret = 0;

	if (verbose)
		ret += repsep_fprintf(fp, "%#018llx  ", (u64)self->ip);

	ret += repsep_fprintf(fp, "[%c] ", self->level);
	if (self->sym) {
		ret += repsep_fprintf(fp, "%s", self->sym->name);

		if (self->sym->module)
			ret += repsep_fprintf(fp, "\t[%s]",
					     self->sym->module->name);
	} else {
		ret += repsep_fprintf(fp, "%#016llx", (u64)self->ip);
	}

	return ret;
}

static struct sort_entry sort_sym = {
	.header = "Symbol",
	.cmp	= sort__sym_cmp,
	.print	= sort__sym_print,
};

/* --sort parent */

static int64_t
sort__parent_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct symbol *sym_l = left->parent;
	struct symbol *sym_r = right->parent;

	if (!sym_l || !sym_r)
		return cmp_null(sym_l, sym_r);

	return strcmp(sym_l->name, sym_r->name);
}

static size_t
sort__parent_print(FILE *fp, struct hist_entry *self, unsigned int width)
{
	return repsep_fprintf(fp, "%-*s", width,
			      self->parent ? self->parent->name : "[other]");
}

static unsigned int parent_symbol__col_width;

static struct sort_entry sort_parent = {
	.header = "Parent symbol",
	.cmp	= sort__parent_cmp,
	.print	= sort__parent_print,
	.width	= &parent_symbol__col_width,
};

static int sort__need_collapse = 0;
static int sort__has_parent = 0;

struct sort_dimension {
	char			*name;
	struct sort_entry	*entry;
	int			taken;
};

static struct sort_dimension sort_dimensions[] = {
	{ .name = "pid",	.entry = &sort_thread,	},
	{ .name = "comm",	.entry = &sort_comm,	},
	{ .name = "dso",	.entry = &sort_dso,	},
	{ .name = "symbol",	.entry = &sort_sym,	},
	{ .name = "parent",	.entry = &sort_parent,	},
};

static LIST_HEAD(hist_entry__sort_list);

static int sort_dimension__add(char *tok)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sort_dimensions); i++) {
		struct sort_dimension *sd = &sort_dimensions[i];

		if (sd->taken)
			continue;

		if (strncasecmp(tok, sd->name, strlen(tok)))
			continue;

		if (sd->entry->collapse)
			sort__need_collapse = 1;

		if (sd->entry == &sort_parent) {
			int ret = regcomp(&parent_regex, parent_pattern, REG_EXTENDED);
			if (ret) {
				char err[BUFSIZ];

				regerror(ret, &parent_regex, err, sizeof(err));
				fprintf(stderr, "Invalid regex: %s\n%s",
					parent_pattern, err);
				exit(-1);
			}
			sort__has_parent = 1;
		}

		list_add_tail(&sd->entry->list, &hist_entry__sort_list);
		sd->taken = 1;

		return 0;
	}

	return -ESRCH;
}

static int64_t
hist_entry__cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct sort_entry *se;
	int64_t cmp = 0;

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		cmp = se->cmp(left, right);
		if (cmp)
			break;
	}

	return cmp;
}

static int64_t
hist_entry__collapse(struct hist_entry *left, struct hist_entry *right)
{
	struct sort_entry *se;
	int64_t cmp = 0;

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		int64_t (*f)(struct hist_entry *, struct hist_entry *);

		f = se->collapse ?: se->cmp;

		cmp = f(left, right);
		if (cmp)
			break;
	}

	return cmp;
}

static size_t ipchain__fprintf_graph_line(FILE *fp, int depth, int depth_mask)
{
	int i;
	size_t ret = 0;

	ret += fprintf(fp, "%s", "                ");

	for (i = 0; i < depth; i++)
		if (depth_mask & (1 << i))
			ret += fprintf(fp, "|          ");
		else
			ret += fprintf(fp, "           ");

	ret += fprintf(fp, "\n");

	return ret;
}
static size_t
ipchain__fprintf_graph(FILE *fp, struct callchain_list *chain, int depth,
		       int depth_mask, int count, u64 total_samples,
		       int hits)
{
	int i;
	size_t ret = 0;

	ret += fprintf(fp, "%s", "                ");
	for (i = 0; i < depth; i++) {
		if (depth_mask & (1 << i))
			ret += fprintf(fp, "|");
		else
			ret += fprintf(fp, " ");
		if (!count && i == depth - 1) {
			double percent;

			percent = hits * 100.0 / total_samples;
			ret += percent_color_fprintf(fp, "--%2.2f%%-- ", percent);
		} else
			ret += fprintf(fp, "%s", "          ");
	}
	if (chain->sym)
		ret += fprintf(fp, "%s\n", chain->sym->name);
	else
		ret += fprintf(fp, "%p\n", (void *)(long)chain->ip);

	return ret;
}

static size_t
callchain__fprintf_graph(FILE *fp, struct callchain_node *self,
			u64 total_samples, int depth, int depth_mask)
{
	struct rb_node *node, *next;
	struct callchain_node *child;
	struct callchain_list *chain;
	int new_depth_mask = depth_mask;
	u64 new_total;
	size_t ret = 0;
	int i;

	if (callchain_param.mode == CHAIN_GRAPH_REL)
		new_total = self->cumul_hit;
	else
		new_total = total_samples;

	node = rb_first(&self->rb_root);
	while (node) {
		child = rb_entry(node, struct callchain_node, rb_node);

		/*
		 * The depth mask manages the output of pipes that show
		 * the depth. We don't want to keep the pipes of the current
		 * level for the last child of this depth
		 */
		next = rb_next(node);
		if (!next)
			new_depth_mask &= ~(1 << (depth - 1));

		/*
		 * But we keep the older depth mask for the line seperator
		 * to keep the level link until we reach the last child
		 */
		ret += ipchain__fprintf_graph_line(fp, depth, depth_mask);
		i = 0;
		list_for_each_entry(chain, &child->val, list) {
			if (chain->ip >= PERF_CONTEXT_MAX)
				continue;
			ret += ipchain__fprintf_graph(fp, chain, depth,
						      new_depth_mask, i++,
						      new_total,
						      child->cumul_hit);
		}
		ret += callchain__fprintf_graph(fp, child, new_total,
						depth + 1,
						new_depth_mask | (1 << depth));
		node = next;
	}

	return ret;
}

static size_t
callchain__fprintf_flat(FILE *fp, struct callchain_node *self,
			u64 total_samples)
{
	struct callchain_list *chain;
	size_t ret = 0;

	if (!self)
		return 0;

	ret += callchain__fprintf_flat(fp, self->parent, total_samples);


	list_for_each_entry(chain, &self->val, list) {
		if (chain->ip >= PERF_CONTEXT_MAX)
			continue;
		if (chain->sym)
			ret += fprintf(fp, "                %s\n", chain->sym->name);
		else
			ret += fprintf(fp, "                %p\n",
					(void *)(long)chain->ip);
	}

	return ret;
}

static size_t
hist_entry_callchain__fprintf(FILE *fp, struct hist_entry *self,
			      u64 total_samples)
{
	struct rb_node *rb_node;
	struct callchain_node *chain;
	size_t ret = 0;

	rb_node = rb_first(&self->sorted_chain);
	while (rb_node) {
		double percent;

		chain = rb_entry(rb_node, struct callchain_node, rb_node);
		percent = chain->hit * 100.0 / total_samples;
		switch (callchain_param.mode) {
		case CHAIN_FLAT:
			ret += percent_color_fprintf(fp, "           %6.2f%%\n",
						     percent);
			ret += callchain__fprintf_flat(fp, chain, total_samples);
			break;
		case CHAIN_GRAPH_ABS: /* Falldown */
		case CHAIN_GRAPH_REL:
			ret += callchain__fprintf_graph(fp, chain,
							total_samples, 1, 1);
		default:
			break;
		}
		ret += fprintf(fp, "\n");
		rb_node = rb_next(rb_node);
	}

	return ret;
}


static size_t
hist_entry__fprintf(FILE *fp, struct hist_entry *self, u64 total_samples)
{
	struct sort_entry *se;
	size_t ret;

	if (exclude_other && !self->parent)
		return 0;

	if (total_samples)
		ret = percent_color_fprintf(fp,
					    field_sep ? "%.2f" : "   %6.2f%%",
					(self->count * 100.0) / total_samples);
	else
		ret = fprintf(fp, field_sep ? "%lld" : "%12lld ", self->count);

	if (show_nr_samples) {
		if (field_sep)
			fprintf(fp, "%c%lld", *field_sep, self->count);
		else
			fprintf(fp, "%11lld", self->count);
	}

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		if (se->elide)
			continue;

		fprintf(fp, "%s", field_sep ?: "  ");
		ret += se->print(fp, self, se->width ? *se->width : 0);
	}

	ret += fprintf(fp, "\n");

	if (callchain)
		hist_entry_callchain__fprintf(fp, self, total_samples);

	return ret;
}

/*
 *
 */

static void dso__calc_col_width(struct dso *self)
{
	if (!col_width_list_str && !field_sep &&
	    (!dso_list || strlist__has_entry(dso_list, self->name))) {
		unsigned int slen = strlen(self->name);
		if (slen > dsos__col_width)
			dsos__col_width = slen;
	}

	self->slen_calculated = 1;
}

static struct symbol *
resolve_symbol(struct thread *thread, struct map **mapp,
	       struct dso **dsop, u64 *ipp)
{
	struct dso *dso = dsop ? *dsop : NULL;
	struct map *map = mapp ? *mapp : NULL;
	u64 ip = *ipp;

	if (!thread)
		return NULL;

	if (dso)
		goto got_dso;

	if (map)
		goto got_map;

	map = thread__find_map(thread, ip);
	if (map != NULL) {
		/*
		 * We have to do this here as we may have a dso
		 * with no symbol hit that has a name longer than
		 * the ones with symbols sampled.
		 */
		if (!sort_dso.elide && !map->dso->slen_calculated)
			dso__calc_col_width(map->dso);

		if (mapp)
			*mapp = map;
got_map:
		ip = map->map_ip(map, ip);

		dso = map->dso;
	} else {
		/*
		 * If this is outside of all known maps,
		 * and is a negative address, try to look it
		 * up in the kernel dso, as it might be a
		 * vsyscall (which executes in user-mode):
		 */
		if ((long long)ip < 0)
		dso = kernel_dso;
	}
	dprintf(" ...... dso: %s\n", dso ? dso->name : "<not found>");
	dprintf(" ...... map: %Lx -> %Lx\n", *ipp, ip);
	*ipp  = ip;

	if (dsop)
		*dsop = dso;

	if (!dso)
		return NULL;
got_dso:
	return dso->find_symbol(dso, ip);
}

static int call__match(struct symbol *sym)
{
	if (sym->name && !regexec(&parent_regex, sym->name, 0, NULL, 0))
		return 1;

	return 0;
}

static struct symbol **
resolve_callchain(struct thread *thread, struct map *map __used,
		    struct ip_callchain *chain, struct hist_entry *entry)
{
	u64 context = PERF_CONTEXT_MAX;
	struct symbol **syms = NULL;
	unsigned int i;

	if (callchain) {
		syms = calloc(chain->nr, sizeof(*syms));
		if (!syms) {
			fprintf(stderr, "Can't allocate memory for symbols\n");
			exit(-1);
		}
	}

	for (i = 0; i < chain->nr; i++) {
		u64 ip = chain->ips[i];
		struct dso *dso = NULL;
		struct symbol *sym;

		if (ip >= PERF_CONTEXT_MAX) {
			context = ip;
			continue;
		}

		switch (context) {
		case PERF_CONTEXT_HV:
			dso = hypervisor_dso;
			break;
		case PERF_CONTEXT_KERNEL:
			dso = kernel_dso;
			break;
		default:
			break;
		}

		sym = resolve_symbol(thread, NULL, &dso, &ip);

		if (sym) {
			if (sort__has_parent && call__match(sym) &&
			    !entry->parent)
				entry->parent = sym;
			if (!callchain)
				break;
			syms[i] = sym;
		}
	}

	return syms;
}

/*
 * collect histogram counts
 */

static int
hist_entry__add(struct thread *thread, struct map *map, struct dso *dso,
		struct symbol *sym, u64 ip, struct ip_callchain *chain,
		char level, u64 count)
{
	struct rb_node **p = &hist.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *he;
	struct symbol **syms = NULL;
	struct hist_entry entry = {
		.thread	= thread,
		.map	= map,
		.dso	= dso,
		.sym	= sym,
		.ip	= ip,
		.level	= level,
		.count	= count,
		.parent = NULL,
		.sorted_chain = RB_ROOT
	};
	int cmp;

	if ((sort__has_parent || callchain) && chain)
		syms = resolve_callchain(thread, map, chain, &entry);

	while (*p != NULL) {
		parent = *p;
		he = rb_entry(parent, struct hist_entry, rb_node);

		cmp = hist_entry__cmp(&entry, he);

		if (!cmp) {
			he->count += count;
			if (callchain) {
				append_chain(&he->callchain, chain, syms);
				free(syms);
			}
			return 0;
		}

		if (cmp < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	he = malloc(sizeof(*he));
	if (!he)
		return -ENOMEM;
	*he = entry;
	if (callchain) {
		callchain_init(&he->callchain);
		append_chain(&he->callchain, chain, syms);
		free(syms);
	}
	rb_link_node(&he->rb_node, parent, p);
	rb_insert_color(&he->rb_node, &hist);

	return 0;
}

static void hist_entry__free(struct hist_entry *he)
{
	free(he);
}

/*
 * collapse the histogram
 */

static struct rb_root collapse_hists;

static void collapse__insert_entry(struct hist_entry *he)
{
	struct rb_node **p = &collapse_hists.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *iter;
	int64_t cmp;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct hist_entry, rb_node);

		cmp = hist_entry__collapse(iter, he);

		if (!cmp) {
			iter->count += he->count;
			hist_entry__free(he);
			return;
		}

		if (cmp < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&he->rb_node, parent, p);
	rb_insert_color(&he->rb_node, &collapse_hists);
}

static void collapse__resort(void)
{
	struct rb_node *next;
	struct hist_entry *n;

	if (!sort__need_collapse)
		return;

	next = rb_first(&hist);
	while (next) {
		n = rb_entry(next, struct hist_entry, rb_node);
		next = rb_next(&n->rb_node);

		rb_erase(&n->rb_node, &hist);
		collapse__insert_entry(n);
	}
}

/*
 * reverse the map, sort on count.
 */

static struct rb_root output_hists;

static void output__insert_entry(struct hist_entry *he, u64 min_callchain_hits)
{
	struct rb_node **p = &output_hists.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *iter;

	if (callchain)
		callchain_param.sort(&he->sorted_chain, &he->callchain,
				      min_callchain_hits, &callchain_param);

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct hist_entry, rb_node);

		if (he->count > iter->count)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&he->rb_node, parent, p);
	rb_insert_color(&he->rb_node, &output_hists);
}

static void output__resort(u64 total_samples)
{
	struct rb_node *next;
	struct hist_entry *n;
	struct rb_root *tree = &hist;
	u64 min_callchain_hits;

	min_callchain_hits = total_samples * (callchain_param.min_percent / 100);

	if (sort__need_collapse)
		tree = &collapse_hists;

	next = rb_first(tree);

	while (next) {
		n = rb_entry(next, struct hist_entry, rb_node);
		next = rb_next(&n->rb_node);

		rb_erase(&n->rb_node, tree);
		output__insert_entry(n, min_callchain_hits);
	}
}

static size_t output__fprintf(FILE *fp, u64 total_samples)
{
	struct hist_entry *pos;
	struct sort_entry *se;
	struct rb_node *nd;
	size_t ret = 0;
	unsigned int width;
	char *col_width = col_width_list_str;

	fprintf(fp, "# Samples: %Ld\n", (u64)total_samples);
	fprintf(fp, "#\n");

	fprintf(fp, "# Overhead");
	if (show_nr_samples) {
		if (field_sep)
			fprintf(fp, "%cSamples", *field_sep);
		else
			fputs("  Samples  ", fp);
	}
	list_for_each_entry(se, &hist_entry__sort_list, list) {
		if (se->elide)
			continue;
		if (field_sep) {
			fprintf(fp, "%c%s", *field_sep, se->header);
			continue;
		}
		width = strlen(se->header);
		if (se->width) {
			if (col_width_list_str) {
				if (col_width) {
					*se->width = atoi(col_width);
					col_width = strchr(col_width, ',');
					if (col_width)
						++col_width;
				}
			}
			width = *se->width = max(*se->width, width);
		}
		fprintf(fp, "  %*s", width, se->header);
	}
	fprintf(fp, "\n");

	if (field_sep)
		goto print_entries;

	fprintf(fp, "# ........");
	if (show_nr_samples)
		fprintf(fp, " ..........");
	list_for_each_entry(se, &hist_entry__sort_list, list) {
		unsigned int i;

		if (se->elide)
			continue;

		fprintf(fp, "  ");
		if (se->width)
			width = *se->width;
		else
			width = strlen(se->header);
		for (i = 0; i < width; i++)
			fprintf(fp, ".");
	}
	fprintf(fp, "\n");

	fprintf(fp, "#\n");

print_entries:
	for (nd = rb_first(&output_hists); nd; nd = rb_next(nd)) {
		pos = rb_entry(nd, struct hist_entry, rb_node);
		ret += hist_entry__fprintf(fp, pos, total_samples);
	}

	if (sort_order == default_sort_order &&
			parent_pattern == default_parent_pattern) {
		fprintf(fp, "#\n");
		fprintf(fp, "# (For more details, try: perf report --sort comm,dso,symbol)\n");
		fprintf(fp, "#\n");
	}
	fprintf(fp, "\n");

	return ret;
}

static void register_idle_thread(void)
{
	struct thread *thread = threads__findnew(0);

	if (thread == NULL ||
			thread__set_comm(thread, "[idle]")) {
		fprintf(stderr, "problem inserting idle task.\n");
		exit(-1);
	}
}

static unsigned long total = 0,
		     total_mmap = 0,
		     total_comm = 0,
		     total_fork = 0,
		     total_unknown = 0,
		     total_lost = 0;

static int validate_chain(struct ip_callchain *chain, event_t *event)
{
	unsigned int chain_size;

	chain_size = event->header.size;
	chain_size -= (unsigned long)&event->ip.__more_data - (unsigned long)event;

	if (chain->nr*sizeof(u64) > chain_size)
		return -1;

	return 0;
}

static int
process_sample_event(event_t *event, unsigned long offset, unsigned long head)
{
	char level;
	int show = 0;
	struct dso *dso = NULL;
	struct thread *thread = threads__findnew(event->ip.pid);
	u64 ip = event->ip.ip;
	u64 period = 1;
	struct map *map = NULL;
	void *more_data = event->ip.__more_data;
	struct ip_callchain *chain = NULL;
	int cpumode;

	if (sample_type & PERF_SAMPLE_PERIOD) {
		period = *(u64 *)more_data;
		more_data += sizeof(u64);
	}

	dprintf("%p [%p]: PERF_EVENT_SAMPLE (IP, %d): %d: %p period: %Ld\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->header.misc,
		event->ip.pid,
		(void *)(long)ip,
		(long long)period);

	if (sample_type & PERF_SAMPLE_CALLCHAIN) {
		unsigned int i;

		chain = (void *)more_data;

		dprintf("... chain: nr:%Lu\n", chain->nr);

		if (validate_chain(chain, event) < 0) {
			eprintf("call-chain problem with event, skipping it.\n");
			return 0;
		}

		if (dump_trace) {
			for (i = 0; i < chain->nr; i++)
				dprintf("..... %2d: %016Lx\n", i, chain->ips[i]);
		}
	}

	dprintf(" ... thread: %s:%d\n", thread->comm, thread->pid);

	if (thread == NULL) {
		eprintf("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	if (comm_list && !strlist__has_entry(comm_list, thread->comm))
		return 0;

	cpumode = event->header.misc & PERF_EVENT_MISC_CPUMODE_MASK;

	if (cpumode == PERF_EVENT_MISC_KERNEL) {
		show = SHOW_KERNEL;
		level = 'k';

		dso = kernel_dso;

		dprintf(" ...... dso: %s\n", dso->name);

	} else if (cpumode == PERF_EVENT_MISC_USER) {

		show = SHOW_USER;
		level = '.';

	} else {
		show = SHOW_HV;
		level = 'H';

		dso = hypervisor_dso;

		dprintf(" ...... dso: [hypervisor]\n");
	}

	if (show & show_mask) {
		struct symbol *sym = resolve_symbol(thread, &map, &dso, &ip);

		if (dso_list && dso && dso->name && !strlist__has_entry(dso_list, dso->name))
			return 0;

		if (sym_list && sym && !strlist__has_entry(sym_list, sym->name))
			return 0;

		if (hist_entry__add(thread, map, dso, sym, ip, chain, level, period)) {
			eprintf("problem incrementing symbol count, skipping event\n");
			return -1;
		}
	}
	total += period;

	return 0;
}

static int
process_mmap_event(event_t *event, unsigned long offset, unsigned long head)
{
	struct thread *thread = threads__findnew(event->mmap.pid);
	struct map *map = map__new(&event->mmap);

	dprintf("%p [%p]: PERF_EVENT_MMAP %d: [%p(%p) @ %p]: %s\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->mmap.pid,
		(void *)(long)event->mmap.start,
		(void *)(long)event->mmap.len,
		(void *)(long)event->mmap.pgoff,
		event->mmap.filename);

	if (thread == NULL || map == NULL) {
		dprintf("problem processing PERF_EVENT_MMAP, skipping event.\n");
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

	dprintf("%p [%p]: PERF_EVENT_COMM: %s:%d\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->comm.comm, event->comm.pid);

	if (thread == NULL ||
	    thread__set_comm(thread, event->comm.comm)) {
		dprintf("problem processing PERF_EVENT_COMM, skipping event.\n");
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

	dprintf("%p [%p]: PERF_EVENT_FORK: %d:%d\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->fork.pid, event->fork.ppid);

	if (!thread || !parent || thread__fork(thread, parent)) {
		dprintf("problem processing PERF_EVENT_FORK, skipping event.\n");
		return -1;
	}
	total_fork++;

	return 0;
}

static int
process_lost_event(event_t *event, unsigned long offset, unsigned long head)
{
	dprintf("%p [%p]: PERF_EVENT_LOST: id:%Ld: lost:%Ld\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->lost.id,
		event->lost.lost);

	total_lost += event->lost.lost;

	return 0;
}

static void trace_event(event_t *event)
{
	unsigned char *raw_event = (void *)event;
	char *color = PERF_COLOR_BLUE;
	int i, j;

	if (!dump_trace)
		return;

	dprintf(".");
	cdprintf("\n. ... raw event: size %d bytes\n", event->header.size);

	for (i = 0; i < event->header.size; i++) {
		if ((i & 15) == 0) {
			dprintf(".");
			cdprintf("  %04x: ", i);
		}

		cdprintf(" %02x", raw_event[i]);

		if (((i & 15) == 15) || i == event->header.size-1) {
			cdprintf("  ");
			for (j = 0; j < 15-(i & 15); j++)
				cdprintf("   ");
			for (j = 0; j < (i & 15); j++) {
				if (isprint(raw_event[i-15+j]))
					cdprintf("%c", raw_event[i-15+j]);
				else
					cdprintf(".");
			}
			cdprintf("\n");
		}
	}
	dprintf(".\n");
}

static int
process_read_event(event_t *event, unsigned long offset, unsigned long head)
{
	dprintf("%p [%p]: PERF_EVENT_READ: %d %d %Lu\n",
			(void *)(offset + head),
			(void *)(long)(event->header.size),
			event->read.pid,
			event->read.tid,
			event->read.value);

	return 0;
}

static int
process_event(event_t *event, unsigned long offset, unsigned long head)
{
	trace_event(event);

	switch (event->header.type) {
	case PERF_EVENT_SAMPLE:
		return process_sample_event(event, offset, head);

	case PERF_EVENT_MMAP:
		return process_mmap_event(event, offset, head);

	case PERF_EVENT_COMM:
		return process_comm_event(event, offset, head);

	case PERF_EVENT_FORK:
		return process_fork_event(event, offset, head);

	case PERF_EVENT_LOST:
		return process_lost_event(event, offset, head);

	case PERF_EVENT_READ:
		return process_read_event(event, offset, head);

	/*
	 * We dont process them right now but they are fine:
	 */

	case PERF_EVENT_THROTTLE:
	case PERF_EVENT_UNTHROTTLE:
		return 0;

	default:
		return -1;
	}

	return 0;
}

static struct perf_header	*header;

static u64 perf_header__sample_type(void)
{
	u64 sample_type = 0;
	int i;

	for (i = 0; i < header->attrs; i++) {
		struct perf_header_attr *attr = header->attr[i];

		if (!sample_type)
			sample_type = attr->attr.sample_type;
		else if (sample_type != attr->attr.sample_type)
			die("non matching sample_type");
	}

	return sample_type;
}

static int __cmd_report(void)
{
	int ret, rc = EXIT_FAILURE;
	unsigned long offset = 0;
	unsigned long head, shift;
	struct stat stat;
	event_t *event;
	uint32_t size;
	char *buf;

	register_idle_thread();

	input = open(input_name, O_RDONLY);
	if (input < 0) {
		fprintf(stderr, " failed to open file: %s", input_name);
		if (!strcmp(input_name, "perf.data"))
			fprintf(stderr, "  (try 'perf record' first)");
		fprintf(stderr, "\n");
		exit(-1);
	}

	ret = fstat(input, &stat);
	if (ret < 0) {
		perror("failed to stat file");
		exit(-1);
	}

	if (!stat.st_size) {
		fprintf(stderr, "zero-sized file, nothing to do!\n");
		exit(0);
	}

	header = perf_header__read(input);
	head = header->data_offset;

	sample_type = perf_header__sample_type();

	if (!(sample_type & PERF_SAMPLE_CALLCHAIN)) {
		if (sort__has_parent) {
			fprintf(stderr, "selected --sort parent, but no"
					" callchain data. Did you call"
					" perf record without -g?\n");
			exit(-1);
		}
		if (callchain) {
			fprintf(stderr, "selected -c but no callchain data."
					" Did you call perf record without"
					" -g?\n");
			exit(-1);
		}
	}

	if (load_kernel() < 0) {
		perror("failed to load kernel symbols");
		return EXIT_FAILURE;
	}

	if (!full_paths) {
		if (getcwd(__cwd, sizeof(__cwd)) == NULL) {
			perror("failed to get the current directory");
			return EXIT_FAILURE;
		}
		cwdlen = strlen(cwd);
	} else {
		cwd = NULL;
		cwdlen = 0;
	}

	shift = page_size * (head / page_size);
	offset += shift;
	head -= shift;

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
		int ret;

		shift = page_size * (head / page_size);

		ret = munmap(buf, page_size * mmap_window);
		assert(ret == 0);

		offset += shift;
		head -= shift;
		goto remap;
	}

	size = event->header.size;

	dprintf("\n%p [%p]: event: %d\n",
			(void *)(offset + head),
			(void *)(long)event->header.size,
			event->header.type);

	if (!size || process_event(event, offset, head) < 0) {

		dprintf("%p [%p]: skipping unknown header type: %d\n",
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

	if (offset + head >= header->data_offset + header->data_size)
		goto done;

	if (offset + head < (unsigned long)stat.st_size)
		goto more;

done:
	rc = EXIT_SUCCESS;
	close(input);

	dprintf("      IP events: %10ld\n", total);
	dprintf("    mmap events: %10ld\n", total_mmap);
	dprintf("    comm events: %10ld\n", total_comm);
	dprintf("    fork events: %10ld\n", total_fork);
	dprintf("    lost events: %10ld\n", total_lost);
	dprintf(" unknown events: %10ld\n", total_unknown);

	if (dump_trace)
		return 0;

	if (verbose >= 3)
		threads__fprintf(stdout);

	if (verbose >= 2)
		dsos__fprintf(stdout);

	collapse__resort();
	output__resort(total);
	output__fprintf(stdout, total);

	return rc;
}

static int
parse_callchain_opt(const struct option *opt __used, const char *arg,
		    int unset __used)
{
	char *tok;
	char *endptr;

	callchain = 1;

	if (!arg)
		return 0;

	tok = strtok((char *)arg, ",");
	if (!tok)
		return -1;

	/* get the output mode */
	if (!strncmp(tok, "graph", strlen(arg)))
		callchain_param.mode = CHAIN_GRAPH_ABS;

	else if (!strncmp(tok, "flat", strlen(arg)))
		callchain_param.mode = CHAIN_FLAT;

	else if (!strncmp(tok, "fractal", strlen(arg)))
		callchain_param.mode = CHAIN_GRAPH_REL;

	else
		return -1;

	/* get the min percentage */
	tok = strtok(NULL, ",");
	if (!tok)
		goto setup;

	callchain_param.min_percent = strtod(tok, &endptr);
	if (tok == endptr)
		return -1;

setup:
	if (register_callchain_param(&callchain_param) < 0) {
		fprintf(stderr, "Can't register callchain params\n");
		return -1;
	}
	return 0;
}

static const char * const report_usage[] = {
	"perf report [<options>] <command>",
	NULL
};

static const struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_STRING('k', "vmlinux", &vmlinux, "file", "vmlinux pathname"),
	OPT_BOOLEAN('m', "modules", &modules,
		    "load module symbols - WARNING: use only with -k and LIVE kernel"),
	OPT_BOOLEAN('n', "show-nr-samples", &show_nr_samples,
		    "Show a column with the number of samples"),
	OPT_STRING('s', "sort", &sort_order, "key[,key2...]",
		   "sort by key(s): pid, comm, dso, symbol, parent"),
	OPT_BOOLEAN('P', "full-paths", &full_paths,
		    "Don't shorten the pathnames taking into account the cwd"),
	OPT_STRING('p', "parent", &parent_pattern, "regex",
		   "regex filter to identify parent, see: '--sort parent'"),
	OPT_BOOLEAN('x', "exclude-other", &exclude_other,
		    "Only display entries with parent-match"),
	OPT_CALLBACK_DEFAULT('g', "call-graph", NULL, "output_type,min_percent",
		     "Display callchains using output_type and min percent threshold. "
		     "Default: fractal,0.5", &parse_callchain_opt, callchain_default_opt),
	OPT_STRING('d', "dsos", &dso_list_str, "dso[,dso...]",
		   "only consider symbols in these dsos"),
	OPT_STRING('C', "comms", &comm_list_str, "comm[,comm...]",
		   "only consider symbols in these comms"),
	OPT_STRING('S', "symbols", &sym_list_str, "symbol[,symbol...]",
		   "only consider these symbols"),
	OPT_STRING('w', "column-widths", &col_width_list_str,
		   "width[,width...]",
		   "don't try to adjust column width, use these fixed values"),
	OPT_STRING('t', "field-separator", &field_sep, "separator",
		   "separator for columns, no spaces will be added between "
		   "columns '.' is reserved."),
	OPT_END()
};

static void setup_sorting(void)
{
	char *tmp, *tok, *str = strdup(sort_order);

	for (tok = strtok_r(str, ", ", &tmp);
			tok; tok = strtok_r(NULL, ", ", &tmp)) {
		if (sort_dimension__add(tok) < 0) {
			error("Unknown --sort key: `%s'", tok);
			usage_with_options(report_usage, options);
		}
	}

	free(str);
}

static void setup_list(struct strlist **list, const char *list_str,
		       struct sort_entry *se, const char *list_name,
		       FILE *fp)
{
	if (list_str) {
		*list = strlist__new(true, list_str);
		if (!*list) {
			fprintf(stderr, "problems parsing %s list\n",
				list_name);
			exit(129);
		}
		if (strlist__nr_entries(*list) == 1) {
			fprintf(fp, "# %s: %s\n", list_name,
				strlist__entry(*list, 0)->s);
			se->elide = true;
		}
	}
}

int cmd_report(int argc, const char **argv, const char *prefix __used)
{
	symbol__init();

	page_size = getpagesize();

	argc = parse_options(argc, argv, options, report_usage, 0);

	setup_sorting();

	if (parent_pattern != default_parent_pattern) {
		sort_dimension__add("parent");
		sort_parent.elide = 1;
	} else
		exclude_other = 0;

	/*
	 * Any (unrecognized) arguments left?
	 */
	if (argc)
		usage_with_options(report_usage, options);

	setup_pager();

	setup_list(&dso_list, dso_list_str, &sort_dso, "dso", stdout);
	setup_list(&comm_list, comm_list_str, &sort_comm, "comm", stdout);
	setup_list(&sym_list, sym_list_str, &sort_sym, "symbol", stdout);

	if (field_sep && *field_sep == '.') {
		fputs("'.' is the only non valid --field-separator argument\n",
		      stderr);
		exit(129);
	}

	return __cmd_report();
}
