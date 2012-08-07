#include "builtin.h"
#include "perf.h"

#include "util/evsel.h"
#include "util/util.h"
#include "util/cache.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/header.h"
#include "util/session.h"
#include "util/tool.h"

#include "util/parse-options.h"
#include "util/trace-event.h"

#include "util/debug.h"

#include <linux/rbtree.h>

struct alloc_stat;
typedef int (*sort_fn_t)(struct alloc_stat *, struct alloc_stat *);

static const char		*input_name;

static int			alloc_flag;
static int			caller_flag;

static int			alloc_lines = -1;
static int			caller_lines = -1;

static bool			raw_ip;

static char			default_sort_order[] = "frag,hit,bytes";

static int			*cpunode_map;
static int			max_cpu_num;

struct alloc_stat {
	u64	call_site;
	u64	ptr;
	u64	bytes_req;
	u64	bytes_alloc;
	u32	hit;
	u32	pingpong;

	short	alloc_cpu;

	struct rb_node node;
};

static struct rb_root root_alloc_stat;
static struct rb_root root_alloc_sorted;
static struct rb_root root_caller_stat;
static struct rb_root root_caller_sorted;

static unsigned long total_requested, total_allocated;
static unsigned long nr_allocs, nr_cross_allocs;

#define PATH_SYS_NODE	"/sys/devices/system/node"

static void init_cpunode_map(void)
{
	FILE *fp;
	int i;

	fp = fopen("/sys/devices/system/cpu/kernel_max", "r");
	if (!fp) {
		max_cpu_num = 4096;
		return;
	}

	if (fscanf(fp, "%d", &max_cpu_num) < 1)
		die("Failed to read 'kernel_max' from sysfs");
	max_cpu_num++;

	cpunode_map = calloc(max_cpu_num, sizeof(int));
	if (!cpunode_map)
		die("calloc");
	for (i = 0; i < max_cpu_num; i++)
		cpunode_map[i] = -1;
	fclose(fp);
}

static void setup_cpunode_map(void)
{
	struct dirent *dent1, *dent2;
	DIR *dir1, *dir2;
	unsigned int cpu, mem;
	char buf[PATH_MAX];

	init_cpunode_map();

	dir1 = opendir(PATH_SYS_NODE);
	if (!dir1)
		return;

	while ((dent1 = readdir(dir1)) != NULL) {
		if (dent1->d_type != DT_DIR ||
		    sscanf(dent1->d_name, "node%u", &mem) < 1)
			continue;

		snprintf(buf, PATH_MAX, "%s/%s", PATH_SYS_NODE, dent1->d_name);
		dir2 = opendir(buf);
		if (!dir2)
			continue;
		while ((dent2 = readdir(dir2)) != NULL) {
			if (dent2->d_type != DT_LNK ||
			    sscanf(dent2->d_name, "cpu%u", &cpu) < 1)
				continue;
			cpunode_map[cpu] = mem;
		}
		closedir(dir2);
	}
	closedir(dir1);
}

static void insert_alloc_stat(unsigned long call_site, unsigned long ptr,
			      int bytes_req, int bytes_alloc, int cpu)
{
	struct rb_node **node = &root_alloc_stat.rb_node;
	struct rb_node *parent = NULL;
	struct alloc_stat *data = NULL;

	while (*node) {
		parent = *node;
		data = rb_entry(*node, struct alloc_stat, node);

		if (ptr > data->ptr)
			node = &(*node)->rb_right;
		else if (ptr < data->ptr)
			node = &(*node)->rb_left;
		else
			break;
	}

	if (data && data->ptr == ptr) {
		data->hit++;
		data->bytes_req += bytes_req;
		data->bytes_alloc += bytes_alloc;
	} else {
		data = malloc(sizeof(*data));
		if (!data)
			die("malloc");
		data->ptr = ptr;
		data->pingpong = 0;
		data->hit = 1;
		data->bytes_req = bytes_req;
		data->bytes_alloc = bytes_alloc;

		rb_link_node(&data->node, parent, node);
		rb_insert_color(&data->node, &root_alloc_stat);
	}
	data->call_site = call_site;
	data->alloc_cpu = cpu;
}

static void insert_caller_stat(unsigned long call_site,
			      int bytes_req, int bytes_alloc)
{
	struct rb_node **node = &root_caller_stat.rb_node;
	struct rb_node *parent = NULL;
	struct alloc_stat *data = NULL;

	while (*node) {
		parent = *node;
		data = rb_entry(*node, struct alloc_stat, node);

		if (call_site > data->call_site)
			node = &(*node)->rb_right;
		else if (call_site < data->call_site)
			node = &(*node)->rb_left;
		else
			break;
	}

	if (data && data->call_site == call_site) {
		data->hit++;
		data->bytes_req += bytes_req;
		data->bytes_alloc += bytes_alloc;
	} else {
		data = malloc(sizeof(*data));
		if (!data)
			die("malloc");
		data->call_site = call_site;
		data->pingpong = 0;
		data->hit = 1;
		data->bytes_req = bytes_req;
		data->bytes_alloc = bytes_alloc;

		rb_link_node(&data->node, parent, node);
		rb_insert_color(&data->node, &root_caller_stat);
	}
}

static void process_alloc_event(void *data,
				struct event_format *event,
				int cpu,
				u64 timestamp __used,
				struct thread *thread __used,
				int node)
{
	unsigned long call_site;
	unsigned long ptr;
	int bytes_req;
	int bytes_alloc;
	int node1, node2;

	ptr = raw_field_value(event, "ptr", data);
	call_site = raw_field_value(event, "call_site", data);
	bytes_req = raw_field_value(event, "bytes_req", data);
	bytes_alloc = raw_field_value(event, "bytes_alloc", data);

	insert_alloc_stat(call_site, ptr, bytes_req, bytes_alloc, cpu);
	insert_caller_stat(call_site, bytes_req, bytes_alloc);

	total_requested += bytes_req;
	total_allocated += bytes_alloc;

	if (node) {
		node1 = cpunode_map[cpu];
		node2 = raw_field_value(event, "node", data);
		if (node1 != node2)
			nr_cross_allocs++;
	}
	nr_allocs++;
}

static int ptr_cmp(struct alloc_stat *, struct alloc_stat *);
static int callsite_cmp(struct alloc_stat *, struct alloc_stat *);

static struct alloc_stat *search_alloc_stat(unsigned long ptr,
					    unsigned long call_site,
					    struct rb_root *root,
					    sort_fn_t sort_fn)
{
	struct rb_node *node = root->rb_node;
	struct alloc_stat key = { .ptr = ptr, .call_site = call_site };

	while (node) {
		struct alloc_stat *data;
		int cmp;

		data = rb_entry(node, struct alloc_stat, node);

		cmp = sort_fn(&key, data);
		if (cmp < 0)
			node = node->rb_left;
		else if (cmp > 0)
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}

static void process_free_event(void *data,
			       struct event_format *event,
			       int cpu,
			       u64 timestamp __used,
			       struct thread *thread __used)
{
	unsigned long ptr;
	struct alloc_stat *s_alloc, *s_caller;

	ptr = raw_field_value(event, "ptr", data);

	s_alloc = search_alloc_stat(ptr, 0, &root_alloc_stat, ptr_cmp);
	if (!s_alloc)
		return;

	if (cpu != s_alloc->alloc_cpu) {
		s_alloc->pingpong++;

		s_caller = search_alloc_stat(0, s_alloc->call_site,
					     &root_caller_stat, callsite_cmp);
		assert(s_caller);
		s_caller->pingpong++;
	}
	s_alloc->alloc_cpu = -1;
}

static void process_raw_event(struct perf_evsel *evsel, void *data,
			      int cpu, u64 timestamp, struct thread *thread)
{
	struct event_format *event = evsel->tp_format;

	if (!strcmp(event->name, "kmalloc") ||
	    !strcmp(event->name, "kmem_cache_alloc")) {
		process_alloc_event(data, event, cpu, timestamp, thread, 0);
		return;
	}

	if (!strcmp(event->name, "kmalloc_node") ||
	    !strcmp(event->name, "kmem_cache_alloc_node")) {
		process_alloc_event(data, event, cpu, timestamp, thread, 1);
		return;
	}

	if (!strcmp(event->name, "kfree") ||
	    !strcmp(event->name, "kmem_cache_free")) {
		process_free_event(data, event, cpu, timestamp, thread);
		return;
	}
}

static int process_sample_event(struct perf_tool *tool __used,
				union perf_event *event,
				struct perf_sample *sample,
				struct perf_evsel *evsel,
				struct machine *machine)
{
	struct thread *thread = machine__findnew_thread(machine, event->ip.pid);

	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			 event->header.type);
		return -1;
	}

	dump_printf(" ... thread: %s:%d\n", thread->comm, thread->pid);

	process_raw_event(evsel, sample->raw_data, sample->cpu,
			  sample->time, thread);

	return 0;
}

static struct perf_tool perf_kmem = {
	.sample		 = process_sample_event,
	.comm		 = perf_event__process_comm,
	.ordered_samples = true,
};

static double fragmentation(unsigned long n_req, unsigned long n_alloc)
{
	if (n_alloc == 0)
		return 0.0;
	else
		return 100.0 - (100.0 * n_req / n_alloc);
}

static void __print_result(struct rb_root *root, struct perf_session *session,
			   int n_lines, int is_caller)
{
	struct rb_node *next;
	struct machine *machine;

	printf("%.102s\n", graph_dotted_line);
	printf(" %-34s |",  is_caller ? "Callsite": "Alloc Ptr");
	printf(" Total_alloc/Per | Total_req/Per   | Hit      | Ping-pong | Frag\n");
	printf("%.102s\n", graph_dotted_line);

	next = rb_first(root);

	machine = perf_session__find_host_machine(session);
	if (!machine) {
		pr_err("__print_result: couldn't find kernel information\n");
		return;
	}
	while (next && n_lines--) {
		struct alloc_stat *data = rb_entry(next, struct alloc_stat,
						   node);
		struct symbol *sym = NULL;
		struct map *map;
		char buf[BUFSIZ];
		u64 addr;

		if (is_caller) {
			addr = data->call_site;
			if (!raw_ip)
				sym = machine__find_kernel_function(machine, addr, &map, NULL);
		} else
			addr = data->ptr;

		if (sym != NULL)
			snprintf(buf, sizeof(buf), "%s+%" PRIx64 "", sym->name,
				 addr - map->unmap_ip(map, sym->start));
		else
			snprintf(buf, sizeof(buf), "%#" PRIx64 "", addr);
		printf(" %-34s |", buf);

		printf(" %9llu/%-5lu | %9llu/%-5lu | %8lu | %8lu | %6.3f%%\n",
		       (unsigned long long)data->bytes_alloc,
		       (unsigned long)data->bytes_alloc / data->hit,
		       (unsigned long long)data->bytes_req,
		       (unsigned long)data->bytes_req / data->hit,
		       (unsigned long)data->hit,
		       (unsigned long)data->pingpong,
		       fragmentation(data->bytes_req, data->bytes_alloc));

		next = rb_next(next);
	}

	if (n_lines == -1)
		printf(" ...                                | ...             | ...             | ...    | ...      | ...   \n");

	printf("%.102s\n", graph_dotted_line);
}

static void print_summary(void)
{
	printf("\nSUMMARY\n=======\n");
	printf("Total bytes requested: %lu\n", total_requested);
	printf("Total bytes allocated: %lu\n", total_allocated);
	printf("Total bytes wasted on internal fragmentation: %lu\n",
	       total_allocated - total_requested);
	printf("Internal fragmentation: %f%%\n",
	       fragmentation(total_requested, total_allocated));
	printf("Cross CPU allocations: %lu/%lu\n", nr_cross_allocs, nr_allocs);
}

static void print_result(struct perf_session *session)
{
	if (caller_flag)
		__print_result(&root_caller_sorted, session, caller_lines, 1);
	if (alloc_flag)
		__print_result(&root_alloc_sorted, session, alloc_lines, 0);
	print_summary();
}

struct sort_dimension {
	const char		name[20];
	sort_fn_t		cmp;
	struct list_head	list;
};

static LIST_HEAD(caller_sort);
static LIST_HEAD(alloc_sort);

static void sort_insert(struct rb_root *root, struct alloc_stat *data,
			struct list_head *sort_list)
{
	struct rb_node **new = &(root->rb_node);
	struct rb_node *parent = NULL;
	struct sort_dimension *sort;

	while (*new) {
		struct alloc_stat *this;
		int cmp = 0;

		this = rb_entry(*new, struct alloc_stat, node);
		parent = *new;

		list_for_each_entry(sort, sort_list, list) {
			cmp = sort->cmp(data, this);
			if (cmp)
				break;
		}

		if (cmp > 0)
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

static void __sort_result(struct rb_root *root, struct rb_root *root_sorted,
			  struct list_head *sort_list)
{
	struct rb_node *node;
	struct alloc_stat *data;

	for (;;) {
		node = rb_first(root);
		if (!node)
			break;

		rb_erase(node, root);
		data = rb_entry(node, struct alloc_stat, node);
		sort_insert(root_sorted, data, sort_list);
	}
}

static void sort_result(void)
{
	__sort_result(&root_alloc_stat, &root_alloc_sorted, &alloc_sort);
	__sort_result(&root_caller_stat, &root_caller_sorted, &caller_sort);
}

static int __cmd_kmem(void)
{
	int err = -EINVAL;
	struct perf_session *session;

	session = perf_session__new(input_name, O_RDONLY, 0, false, &perf_kmem);
	if (session == NULL)
		return -ENOMEM;

	if (perf_session__create_kernel_maps(session) < 0)
		goto out_delete;

	if (!perf_session__has_traces(session, "kmem record"))
		goto out_delete;

	setup_pager();
	err = perf_session__process_events(session, &perf_kmem);
	if (err != 0)
		goto out_delete;
	sort_result();
	print_result(session);
out_delete:
	perf_session__delete(session);
	return err;
}

static const char * const kmem_usage[] = {
	"perf kmem [<options>] {record|stat}",
	NULL
};

static int ptr_cmp(struct alloc_stat *l, struct alloc_stat *r)
{
	if (l->ptr < r->ptr)
		return -1;
	else if (l->ptr > r->ptr)
		return 1;
	return 0;
}

static struct sort_dimension ptr_sort_dimension = {
	.name	= "ptr",
	.cmp	= ptr_cmp,
};

static int callsite_cmp(struct alloc_stat *l, struct alloc_stat *r)
{
	if (l->call_site < r->call_site)
		return -1;
	else if (l->call_site > r->call_site)
		return 1;
	return 0;
}

static struct sort_dimension callsite_sort_dimension = {
	.name	= "callsite",
	.cmp	= callsite_cmp,
};

static int hit_cmp(struct alloc_stat *l, struct alloc_stat *r)
{
	if (l->hit < r->hit)
		return -1;
	else if (l->hit > r->hit)
		return 1;
	return 0;
}

static struct sort_dimension hit_sort_dimension = {
	.name	= "hit",
	.cmp	= hit_cmp,
};

static int bytes_cmp(struct alloc_stat *l, struct alloc_stat *r)
{
	if (l->bytes_alloc < r->bytes_alloc)
		return -1;
	else if (l->bytes_alloc > r->bytes_alloc)
		return 1;
	return 0;
}

static struct sort_dimension bytes_sort_dimension = {
	.name	= "bytes",
	.cmp	= bytes_cmp,
};

static int frag_cmp(struct alloc_stat *l, struct alloc_stat *r)
{
	double x, y;

	x = fragmentation(l->bytes_req, l->bytes_alloc);
	y = fragmentation(r->bytes_req, r->bytes_alloc);

	if (x < y)
		return -1;
	else if (x > y)
		return 1;
	return 0;
}

static struct sort_dimension frag_sort_dimension = {
	.name	= "frag",
	.cmp	= frag_cmp,
};

static int pingpong_cmp(struct alloc_stat *l, struct alloc_stat *r)
{
	if (l->pingpong < r->pingpong)
		return -1;
	else if (l->pingpong > r->pingpong)
		return 1;
	return 0;
}

static struct sort_dimension pingpong_sort_dimension = {
	.name	= "pingpong",
	.cmp	= pingpong_cmp,
};

static struct sort_dimension *avail_sorts[] = {
	&ptr_sort_dimension,
	&callsite_sort_dimension,
	&hit_sort_dimension,
	&bytes_sort_dimension,
	&frag_sort_dimension,
	&pingpong_sort_dimension,
};

#define NUM_AVAIL_SORTS	\
	(int)(sizeof(avail_sorts) / sizeof(struct sort_dimension *))

static int sort_dimension__add(const char *tok, struct list_head *list)
{
	struct sort_dimension *sort;
	int i;

	for (i = 0; i < NUM_AVAIL_SORTS; i++) {
		if (!strcmp(avail_sorts[i]->name, tok)) {
			sort = malloc(sizeof(*sort));
			if (!sort)
				die("malloc");
			memcpy(sort, avail_sorts[i], sizeof(*sort));
			list_add_tail(&sort->list, list);
			return 0;
		}
	}

	return -1;
}

static int setup_sorting(struct list_head *sort_list, const char *arg)
{
	char *tok;
	char *str = strdup(arg);

	if (!str)
		die("strdup");

	while (true) {
		tok = strsep(&str, ",");
		if (!tok)
			break;
		if (sort_dimension__add(tok, sort_list) < 0) {
			error("Unknown --sort key: '%s'", tok);
			free(str);
			return -1;
		}
	}

	free(str);
	return 0;
}

static int parse_sort_opt(const struct option *opt __used,
			  const char *arg, int unset __used)
{
	if (!arg)
		return -1;

	if (caller_flag > alloc_flag)
		return setup_sorting(&caller_sort, arg);
	else
		return setup_sorting(&alloc_sort, arg);

	return 0;
}

static int parse_caller_opt(const struct option *opt __used,
			  const char *arg __used, int unset __used)
{
	caller_flag = (alloc_flag + 1);
	return 0;
}

static int parse_alloc_opt(const struct option *opt __used,
			  const char *arg __used, int unset __used)
{
	alloc_flag = (caller_flag + 1);
	return 0;
}

static int parse_line_opt(const struct option *opt __used,
			  const char *arg, int unset __used)
{
	int lines;

	if (!arg)
		return -1;

	lines = strtoul(arg, NULL, 10);

	if (caller_flag > alloc_flag)
		caller_lines = lines;
	else
		alloc_lines = lines;

	return 0;
}

static const struct option kmem_options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		   "input file name"),
	OPT_CALLBACK_NOOPT(0, "caller", NULL, NULL,
			   "show per-callsite statistics",
			   parse_caller_opt),
	OPT_CALLBACK_NOOPT(0, "alloc", NULL, NULL,
			   "show per-allocation statistics",
			   parse_alloc_opt),
	OPT_CALLBACK('s', "sort", NULL, "key[,key2...]",
		     "sort by keys: ptr, call_site, bytes, hit, pingpong, frag",
		     parse_sort_opt),
	OPT_CALLBACK('l', "line", NULL, "num",
		     "show n lines",
		     parse_line_opt),
	OPT_BOOLEAN(0, "raw-ip", &raw_ip, "show raw ip instead of symbol"),
	OPT_END()
};

static const char *record_args[] = {
	"record",
	"-a",
	"-R",
	"-f",
	"-c", "1",
	"-e", "kmem:kmalloc",
	"-e", "kmem:kmalloc_node",
	"-e", "kmem:kfree",
	"-e", "kmem:kmem_cache_alloc",
	"-e", "kmem:kmem_cache_alloc_node",
	"-e", "kmem:kmem_cache_free",
};

static int __cmd_record(int argc, const char **argv)
{
	unsigned int rec_argc, i, j;
	const char **rec_argv;

	rec_argc = ARRAY_SIZE(record_args) + argc - 1;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));

	if (rec_argv == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[i] = strdup(record_args[i]);

	for (j = 1; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	return cmd_record(i, rec_argv, NULL);
}

int cmd_kmem(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, kmem_options, kmem_usage, 0);

	if (!argc)
		usage_with_options(kmem_usage, kmem_options);

	symbol__init();

	if (!strncmp(argv[0], "rec", 3)) {
		return __cmd_record(argc, argv);
	} else if (!strcmp(argv[0], "stat")) {
		setup_cpunode_map();

		if (list_empty(&caller_sort))
			setup_sorting(&caller_sort, default_sort_order);
		if (list_empty(&alloc_sort))
			setup_sorting(&alloc_sort, default_sort_order);

		return __cmd_kmem();
	} else
		usage_with_options(kmem_usage, kmem_options);

	return 0;
}

