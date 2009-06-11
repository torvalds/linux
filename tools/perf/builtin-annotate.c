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
#include "util/list.h"
#include "util/cache.h"
#include "util/rbtree.h"
#include "util/symbol.h"
#include "util/string.h"

#include "perf.h"

#include "util/parse-options.h"
#include "util/parse-events.h"

#define SHOW_KERNEL	1
#define SHOW_USER	2
#define SHOW_HV		4

static char		const *input_name = "perf.data";
static char		*vmlinux = "vmlinux";

static char		default_sort_order[] = "comm,symbol";
static char		*sort_order = default_sort_order;

static int		input;
static int		show_mask = SHOW_KERNEL | SHOW_USER | SHOW_HV;

static int		dump_trace = 0;
#define dprintf(x...)	do { if (dump_trace) printf(x); } while (0)

static int		verbose;

static unsigned long	page_size;
static unsigned long	mmap_window = 32;

struct ip_event {
	struct perf_event_header header;
	__u64 ip;
	__u32 pid, tid;
};

struct mmap_event {
	struct perf_event_header header;
	__u32 pid, tid;
	__u64 start;
	__u64 len;
	__u64 pgoff;
	char filename[PATH_MAX];
};

struct comm_event {
	struct perf_event_header header;
	__u32 pid, tid;
	char comm[16];
};

struct fork_event {
	struct perf_event_header header;
	__u32 pid, ppid;
};

struct period_event {
	struct perf_event_header header;
	__u64 time;
	__u64 id;
	__u64 sample_period;
};

typedef union event_union {
	struct perf_event_header	header;
	struct ip_event			ip;
	struct mmap_event		mmap;
	struct comm_event		comm;
	struct fork_event		fork;
	struct period_event		period;
} event_t;

static LIST_HEAD(dsos);
static struct dso *kernel_dso;
static struct dso *vdso;


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
		if (verbose)
			fprintf(stderr, "Failed to open: %s\n", name);
		goto out_delete_dso;
	}
	if (!nr && verbose) {
		fprintf(stderr,
		"No symbols found in: %s, maybe install a debug package?\n",
				name);
	}

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

static struct symbol *vdso__find_symbol(struct dso *dso, __u64 ip)
{
	return dso__find_symbol(kernel_dso, ip);
}

static int load_kernel(void)
{
	int err;

	kernel_dso = dso__new("[kernel]", 0);
	if (!kernel_dso)
		return -1;

	err = dso__load_kernel(kernel_dso, vmlinux, NULL, verbose);
	if (err) {
		dso__delete(kernel_dso);
		kernel_dso = NULL;
	} else
		dsos__add(kernel_dso);

	vdso = dso__new("[vdso]", 0);
	if (!vdso)
		return -1;

	vdso->find_symbol = vdso__find_symbol;

	dsos__add(vdso);

	return err;
}

struct map {
	struct list_head node;
	__u64	 start;
	__u64	 end;
	__u64	 pgoff;
	__u64	 (*map_ip)(struct map *, __u64);
	struct dso	 *dso;
};

static __u64 map__map_ip(struct map *map, __u64 ip)
{
	return ip - map->start + map->pgoff;
}

static __u64 vdso__map_ip(struct map *map, __u64 ip)
{
	return ip;
}

static struct map *map__new(struct mmap_event *event)
{
	struct map *self = malloc(sizeof(*self));

	if (self != NULL) {
		const char *filename = event->filename;

		self->start = event->start;
		self->end   = event->start + event->len;
		self->pgoff = event->pgoff;

		self->dso = dsos__findnew(filename);
		if (self->dso == NULL)
			goto out_delete;

		if (self->dso == vdso)
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

static int thread__set_comm(struct thread *self, const char *comm)
{
	if (self->comm)
		free(self->comm);
	self->comm = strdup(comm);
	return self->comm ? 0 : -ENOMEM;
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
			list_del_init(&pos->node);
			/* XXX leaks dsos */
			free(pos);
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

static struct map *thread__find_map(struct thread *self, __u64 ip)
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
	struct rb_node	 rb_node;

	struct thread	 *thread;
	struct map	 *map;
	struct dso	 *dso;
	struct symbol	 *sym;
	__u64	 ip;
	char		 level;

	uint32_t	 count;
};

/*
 * configurable sorting bits
 */

struct sort_entry {
	struct list_head list;

	char *header;

	int64_t (*cmp)(struct hist_entry *, struct hist_entry *);
	int64_t (*collapse)(struct hist_entry *, struct hist_entry *);
	size_t	(*print)(FILE *fp, struct hist_entry *);
};

/* --sort pid */

static int64_t
sort__thread_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return right->thread->pid - left->thread->pid;
}

static size_t
sort__thread_print(FILE *fp, struct hist_entry *self)
{
	return fprintf(fp, "%16s:%5d", self->thread->comm ?: "", self->thread->pid);
}

static struct sort_entry sort_thread = {
	.header = "         Command:  Pid",
	.cmp	= sort__thread_cmp,
	.print	= sort__thread_print,
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

	if (!comm_l || !comm_r) {
		if (!comm_l && !comm_r)
			return 0;
		else if (!comm_l)
			return -1;
		else
			return 1;
	}

	return strcmp(comm_l, comm_r);
}

static size_t
sort__comm_print(FILE *fp, struct hist_entry *self)
{
	return fprintf(fp, "%16s", self->thread->comm);
}

static struct sort_entry sort_comm = {
	.header		= "         Command",
	.cmp		= sort__comm_cmp,
	.collapse	= sort__comm_collapse,
	.print		= sort__comm_print,
};

/* --sort dso */

static int64_t
sort__dso_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct dso *dso_l = left->dso;
	struct dso *dso_r = right->dso;

	if (!dso_l || !dso_r) {
		if (!dso_l && !dso_r)
			return 0;
		else if (!dso_l)
			return -1;
		else
			return 1;
	}

	return strcmp(dso_l->name, dso_r->name);
}

static size_t
sort__dso_print(FILE *fp, struct hist_entry *self)
{
	if (self->dso)
		return fprintf(fp, "%-25s", self->dso->name);

	return fprintf(fp, "%016llx         ", (__u64)self->ip);
}

static struct sort_entry sort_dso = {
	.header = "Shared Object            ",
	.cmp	= sort__dso_cmp,
	.print	= sort__dso_print,
};

/* --sort symbol */

static int64_t
sort__sym_cmp(struct hist_entry *left, struct hist_entry *right)
{
	__u64 ip_l, ip_r;

	if (left->sym == right->sym)
		return 0;

	ip_l = left->sym ? left->sym->start : left->ip;
	ip_r = right->sym ? right->sym->start : right->ip;

	return (int64_t)(ip_r - ip_l);
}

static size_t
sort__sym_print(FILE *fp, struct hist_entry *self)
{
	size_t ret = 0;

	if (verbose)
		ret += fprintf(fp, "%#018llx  ", (__u64)self->ip);

	if (self->sym) {
		ret += fprintf(fp, "[%c] %s",
			self->dso == kernel_dso ? 'k' : '.', self->sym->name);
	} else {
		ret += fprintf(fp, "%#016llx", (__u64)self->ip);
	}

	return ret;
}

static struct sort_entry sort_sym = {
	.header = "Symbol",
	.cmp	= sort__sym_cmp,
	.print	= sort__sym_print,
};

static int sort__need_collapse = 0;

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
};

static LIST_HEAD(hist_entry__sort_list);

static int sort_dimension__add(char *tok)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sort_dimensions); i++) {
		struct sort_dimension *sd = &sort_dimensions[i];

		if (sd->taken)
			continue;

		if (strncasecmp(tok, sd->name, strlen(tok)))
			continue;

		if (sd->entry->collapse)
			sort__need_collapse = 1;

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

/*
 * collect histogram counts
 */
static void hist_hit(struct hist_entry *he, __u64 ip)
{
	unsigned int sym_size, offset;
	struct symbol *sym = he->sym;

	he->count++;

	if (!sym || !sym->hist)
		return;

	sym_size = sym->end - sym->start;
	offset = ip - sym->start;

	if (offset >= sym_size)
		return;

	sym->hist_sum++;
	sym->hist[offset]++;

	if (verbose >= 3)
		printf("%p %s: count++ [ip: %p, %08Lx] => %Ld\n",
			(void *)(unsigned long)he->sym->start,
			he->sym->name,
			(void *)(unsigned long)ip, ip - he->sym->start,
			sym->hist[offset]);
}

static int
hist_entry__add(struct thread *thread, struct map *map, struct dso *dso,
		struct symbol *sym, __u64 ip, char level)
{
	struct rb_node **p = &hist.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *he;
	struct hist_entry entry = {
		.thread	= thread,
		.map	= map,
		.dso	= dso,
		.sym	= sym,
		.ip	= ip,
		.level	= level,
		.count	= 1,
	};
	int cmp;

	while (*p != NULL) {
		parent = *p;
		he = rb_entry(parent, struct hist_entry, rb_node);

		cmp = hist_entry__cmp(&entry, he);

		if (!cmp) {
			hist_hit(he, ip);

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

static void output__insert_entry(struct hist_entry *he)
{
	struct rb_node **p = &output_hists.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *iter;

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

static void output__resort(void)
{
	struct rb_node *next;
	struct hist_entry *n;
	struct rb_root *tree = &hist;

	if (sort__need_collapse)
		tree = &collapse_hists;

	next = rb_first(tree);

	while (next) {
		n = rb_entry(next, struct hist_entry, rb_node);
		next = rb_next(&n->rb_node);

		rb_erase(&n->rb_node, tree);
		output__insert_entry(n);
	}
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
		     total_unknown = 0;

static int
process_overflow_event(event_t *event, unsigned long offset, unsigned long head)
{
	char level;
	int show = 0;
	struct dso *dso = NULL;
	struct thread *thread = threads__findnew(event->ip.pid);
	__u64 ip = event->ip.ip;
	struct map *map = NULL;

	dprintf("%p [%p]: PERF_EVENT (IP, %d): %d: %p\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->header.misc,
		event->ip.pid,
		(void *)(long)ip);

	dprintf(" ... thread: %s:%d\n", thread->comm, thread->pid);

	if (thread == NULL) {
		fprintf(stderr, "problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	if (event->header.misc & PERF_EVENT_MISC_KERNEL) {
		show = SHOW_KERNEL;
		level = 'k';

		dso = kernel_dso;

		dprintf(" ...... dso: %s\n", dso->name);

	} else if (event->header.misc & PERF_EVENT_MISC_USER) {

		show = SHOW_USER;
		level = '.';

		map = thread__find_map(thread, ip);
		if (map != NULL) {
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

	} else {
		show = SHOW_HV;
		level = 'H';
		dprintf(" ...... dso: [hypervisor]\n");
	}

	if (show & show_mask) {
		struct symbol *sym = NULL;

		if (dso)
			sym = dso->find_symbol(dso, ip);

		if (hist_entry__add(thread, map, dso, sym, ip, level)) {
			fprintf(stderr,
		"problem incrementing symbol count, skipping event\n");
			return -1;
		}
	}
	total++;

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
process_period_event(event_t *event, unsigned long offset, unsigned long head)
{
	dprintf("%p [%p]: PERF_EVENT_PERIOD: time:%Ld, id:%Ld: period:%Ld\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->period.time,
		event->period.id,
		event->period.sample_period);

	return 0;
}

static int
process_event(event_t *event, unsigned long offset, unsigned long head)
{
	if (event->header.misc & PERF_EVENT_MISC_OVERFLOW)
		return process_overflow_event(event, offset, head);

	switch (event->header.type) {
	case PERF_EVENT_MMAP:
		return process_mmap_event(event, offset, head);

	case PERF_EVENT_COMM:
		return process_comm_event(event, offset, head);

	case PERF_EVENT_FORK:
		return process_fork_event(event, offset, head);

	case PERF_EVENT_PERIOD:
		return process_period_event(event, offset, head);
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

static int
parse_line(FILE *file, struct symbol *sym, __u64 start, __u64 len)
{
	char *line = NULL, *tmp, *tmp2;
	unsigned int offset;
	size_t line_len;
	__u64 line_ip;
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

	if (line_ip != -1) {
		unsigned int hits = 0;
		double percent = 0.0;
		char *color = PERF_COLOR_NORMAL;

		offset = line_ip - start;
		if (offset < len)
			hits = sym->hist[offset];

		if (sym->hist_sum)
			percent = 100.0 * hits / sym->hist_sum;

		/*
		 * We color high-overhead entries in red, mid-overhead
		 * entries in green - and keep the low overhead places
		 * normal:
		 */
		if (percent >= 5.0)
			color = PERF_COLOR_RED;
		else {
			if (percent > 0.5)
				color = PERF_COLOR_GREEN;
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

static void annotate_sym(struct dso *dso, struct symbol *sym)
{
	char *filename = dso->name;
	__u64 start, end, len;
	char command[PATH_MAX*2];
	FILE *file;

	if (!filename)
		return;
	if (dso == kernel_dso)
		filename = vmlinux;

	printf("\n------------------------------------------------\n");
	printf(" Percent |	Source code & Disassembly of %s\n", filename);
	printf("------------------------------------------------\n");

	if (verbose >= 2)
		printf("annotating [%p] %30s : [%p] %30s\n", dso, dso->name, sym, sym->name);

	start = sym->obj_start;
	if (!start)
		start = sym->start;

	end = start + sym->end - sym->start + 1;
	len = sym->end - sym->start;

	sprintf(command, "objdump --start-address=0x%016Lx --stop-address=0x%016Lx -dS %s", (__u64)start, (__u64)end, filename);

	if (verbose >= 3)
		printf("doing: %s\n", command);

	file = popen(command, "r");
	if (!file)
		return;

	while (!feof(file)) {
		if (parse_line(file, sym, start, len) < 0)
			break;
	}

	pclose(file);
}

static void find_annotations(void)
{
	struct rb_node *nd;
	struct dso *dso;
	int count = 0;

	list_for_each_entry(dso, &dsos, node) {

		for (nd = rb_first(&dso->syms); nd; nd = rb_next(nd)) {
			struct symbol *sym = rb_entry(nd, struct symbol, rb_node);

			if (sym->hist) {
				annotate_sym(dso, sym);
				count++;
			}
		}
	}

	if (!count)
		printf(" Error: symbol '%s' not present amongst the samples.\n", sym_hist_filter);
}

static int __cmd_annotate(void)
{
	int ret, rc = EXIT_FAILURE;
	unsigned long offset = 0;
	unsigned long head = 0;
	struct stat stat;
	event_t *event;
	uint32_t size;
	char *buf;

	register_idle_thread();

	input = open(input_name, O_RDONLY);
	if (input < 0) {
		perror("failed to open file");
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

	if (load_kernel() < 0) {
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
		int ret;

		ret = munmap(buf, page_size * mmap_window);
		assert(ret == 0);

		offset += shift;
		head -= shift;
		goto remap;
	}

	size = event->header.size;

	dprintf("%p [%p]: event: %d\n",
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

	if (offset + head < stat.st_size)
		goto more;

	rc = EXIT_SUCCESS;
	close(input);

	dprintf("      IP events: %10ld\n", total);
	dprintf("    mmap events: %10ld\n", total_mmap);
	dprintf("    comm events: %10ld\n", total_comm);
	dprintf("    fork events: %10ld\n", total_fork);
	dprintf(" unknown events: %10ld\n", total_unknown);

	if (dump_trace)
		return 0;

	if (verbose >= 3)
		threads__fprintf(stdout);

	if (verbose >= 2)
		dsos__fprintf(stdout);

	collapse__resort();
	output__resort();

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
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_STRING('k', "vmlinux", &vmlinux, "file", "vmlinux pathname"),
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

int cmd_annotate(int argc, const char **argv, const char *prefix)
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

	if (!sym_hist_filter)
		usage_with_options(annotate_usage, options);

	setup_pager();

	return __cmd_annotate();
}
