#include "builtin.h"
#include "perf.h"

#include "util/evlist.h"
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
#include "util/data.h"
#include "util/cpumap.h"

#include "util/debug.h"

#include <linux/rbtree.h>
#include <linux/string.h>
#include <locale.h>

static int	kmem_slab;
static int	kmem_page;

static long	kmem_page_size;

struct alloc_stat;
typedef int (*sort_fn_t)(struct alloc_stat *, struct alloc_stat *);

static int			alloc_flag;
static int			caller_flag;

static int			alloc_lines = -1;
static int			caller_lines = -1;

static bool			raw_ip;

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

static int insert_alloc_stat(unsigned long call_site, unsigned long ptr,
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
		if (!data) {
			pr_err("%s: malloc failed\n", __func__);
			return -1;
		}
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
	return 0;
}

static int insert_caller_stat(unsigned long call_site,
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
		if (!data) {
			pr_err("%s: malloc failed\n", __func__);
			return -1;
		}
		data->call_site = call_site;
		data->pingpong = 0;
		data->hit = 1;
		data->bytes_req = bytes_req;
		data->bytes_alloc = bytes_alloc;

		rb_link_node(&data->node, parent, node);
		rb_insert_color(&data->node, &root_caller_stat);
	}

	return 0;
}

static int perf_evsel__process_alloc_event(struct perf_evsel *evsel,
					   struct perf_sample *sample)
{
	unsigned long ptr = perf_evsel__intval(evsel, sample, "ptr"),
		      call_site = perf_evsel__intval(evsel, sample, "call_site");
	int bytes_req = perf_evsel__intval(evsel, sample, "bytes_req"),
	    bytes_alloc = perf_evsel__intval(evsel, sample, "bytes_alloc");

	if (insert_alloc_stat(call_site, ptr, bytes_req, bytes_alloc, sample->cpu) ||
	    insert_caller_stat(call_site, bytes_req, bytes_alloc))
		return -1;

	total_requested += bytes_req;
	total_allocated += bytes_alloc;

	nr_allocs++;
	return 0;
}

static int perf_evsel__process_alloc_node_event(struct perf_evsel *evsel,
						struct perf_sample *sample)
{
	int ret = perf_evsel__process_alloc_event(evsel, sample);

	if (!ret) {
		int node1 = cpu__get_node(sample->cpu),
		    node2 = perf_evsel__intval(evsel, sample, "node");

		if (node1 != node2)
			nr_cross_allocs++;
	}

	return ret;
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

static int perf_evsel__process_free_event(struct perf_evsel *evsel,
					  struct perf_sample *sample)
{
	unsigned long ptr = perf_evsel__intval(evsel, sample, "ptr");
	struct alloc_stat *s_alloc, *s_caller;

	s_alloc = search_alloc_stat(ptr, 0, &root_alloc_stat, ptr_cmp);
	if (!s_alloc)
		return 0;

	if ((short)sample->cpu != s_alloc->alloc_cpu) {
		s_alloc->pingpong++;

		s_caller = search_alloc_stat(0, s_alloc->call_site,
					     &root_caller_stat, callsite_cmp);
		if (!s_caller)
			return -1;
		s_caller->pingpong++;
	}
	s_alloc->alloc_cpu = -1;

	return 0;
}

static u64 total_page_alloc_bytes;
static u64 total_page_free_bytes;
static u64 total_page_nomatch_bytes;
static u64 total_page_fail_bytes;
static unsigned long nr_page_allocs;
static unsigned long nr_page_frees;
static unsigned long nr_page_fails;
static unsigned long nr_page_nomatch;

static bool use_pfn;

#define MAX_MIGRATE_TYPES  6
#define MAX_PAGE_ORDER     11

static int order_stats[MAX_PAGE_ORDER][MAX_MIGRATE_TYPES];

struct page_stat {
	struct rb_node 	node;
	u64 		page;
	int 		order;
	unsigned 	gfp_flags;
	unsigned 	migrate_type;
	u64		alloc_bytes;
	u64 		free_bytes;
	int 		nr_alloc;
	int 		nr_free;
};

static struct rb_root page_tree;
static struct rb_root page_alloc_tree;
static struct rb_root page_alloc_sorted;

static struct page_stat *search_page(unsigned long page, bool create)
{
	struct rb_node **node = &page_tree.rb_node;
	struct rb_node *parent = NULL;
	struct page_stat *data;

	while (*node) {
		s64 cmp;

		parent = *node;
		data = rb_entry(*node, struct page_stat, node);

		cmp = data->page - page;
		if (cmp < 0)
			node = &parent->rb_left;
		else if (cmp > 0)
			node = &parent->rb_right;
		else
			return data;
	}

	if (!create)
		return NULL;

	data = zalloc(sizeof(*data));
	if (data != NULL) {
		data->page = page;

		rb_link_node(&data->node, parent, node);
		rb_insert_color(&data->node, &page_tree);
	}

	return data;
}

static int page_stat_cmp(struct page_stat *a, struct page_stat *b)
{
	if (a->page > b->page)
		return -1;
	if (a->page < b->page)
		return 1;
	if (a->order > b->order)
		return -1;
	if (a->order < b->order)
		return 1;
	if (a->migrate_type > b->migrate_type)
		return -1;
	if (a->migrate_type < b->migrate_type)
		return 1;
	if (a->gfp_flags > b->gfp_flags)
		return -1;
	if (a->gfp_flags < b->gfp_flags)
		return 1;
	return 0;
}

static struct page_stat *search_page_alloc_stat(struct page_stat *pstat, bool create)
{
	struct rb_node **node = &page_alloc_tree.rb_node;
	struct rb_node *parent = NULL;
	struct page_stat *data;

	while (*node) {
		s64 cmp;

		parent = *node;
		data = rb_entry(*node, struct page_stat, node);

		cmp = page_stat_cmp(data, pstat);
		if (cmp < 0)
			node = &parent->rb_left;
		else if (cmp > 0)
			node = &parent->rb_right;
		else
			return data;
	}

	if (!create)
		return NULL;

	data = zalloc(sizeof(*data));
	if (data != NULL) {
		data->page = pstat->page;
		data->order = pstat->order;
		data->gfp_flags = pstat->gfp_flags;
		data->migrate_type = pstat->migrate_type;

		rb_link_node(&data->node, parent, node);
		rb_insert_color(&data->node, &page_alloc_tree);
	}

	return data;
}

static bool valid_page(u64 pfn_or_page)
{
	if (use_pfn && pfn_or_page == -1UL)
		return false;
	if (!use_pfn && pfn_or_page == 0)
		return false;
	return true;
}

static int perf_evsel__process_page_alloc_event(struct perf_evsel *evsel,
						struct perf_sample *sample)
{
	u64 page;
	unsigned int order = perf_evsel__intval(evsel, sample, "order");
	unsigned int gfp_flags = perf_evsel__intval(evsel, sample, "gfp_flags");
	unsigned int migrate_type = perf_evsel__intval(evsel, sample,
						       "migratetype");
	u64 bytes = kmem_page_size << order;
	struct page_stat *pstat;
	struct page_stat this = {
		.order = order,
		.gfp_flags = gfp_flags,
		.migrate_type = migrate_type,
	};

	if (use_pfn)
		page = perf_evsel__intval(evsel, sample, "pfn");
	else
		page = perf_evsel__intval(evsel, sample, "page");

	nr_page_allocs++;
	total_page_alloc_bytes += bytes;

	if (!valid_page(page)) {
		nr_page_fails++;
		total_page_fail_bytes += bytes;

		return 0;
	}

	/*
	 * This is to find the current page (with correct gfp flags and
	 * migrate type) at free event.
	 */
	pstat = search_page(page, true);
	if (pstat == NULL)
		return -ENOMEM;

	pstat->order = order;
	pstat->gfp_flags = gfp_flags;
	pstat->migrate_type = migrate_type;

	this.page = page;
	pstat = search_page_alloc_stat(&this, true);
	if (pstat == NULL)
		return -ENOMEM;

	pstat->nr_alloc++;
	pstat->alloc_bytes += bytes;

	order_stats[order][migrate_type]++;

	return 0;
}

static int perf_evsel__process_page_free_event(struct perf_evsel *evsel,
						struct perf_sample *sample)
{
	u64 page;
	unsigned int order = perf_evsel__intval(evsel, sample, "order");
	u64 bytes = kmem_page_size << order;
	struct page_stat *pstat;
	struct page_stat this = {
		.order = order,
	};

	if (use_pfn)
		page = perf_evsel__intval(evsel, sample, "pfn");
	else
		page = perf_evsel__intval(evsel, sample, "page");

	nr_page_frees++;
	total_page_free_bytes += bytes;

	pstat = search_page(page, false);
	if (pstat == NULL) {
		pr_debug2("missing free at page %"PRIx64" (order: %d)\n",
			  page, order);

		nr_page_nomatch++;
		total_page_nomatch_bytes += bytes;

		return 0;
	}

	this.page = page;
	this.gfp_flags = pstat->gfp_flags;
	this.migrate_type = pstat->migrate_type;

	rb_erase(&pstat->node, &page_tree);
	free(pstat);

	pstat = search_page_alloc_stat(&this, false);
	if (pstat == NULL)
		return -ENOENT;

	pstat->nr_free++;
	pstat->free_bytes += bytes;

	return 0;
}

typedef int (*tracepoint_handler)(struct perf_evsel *evsel,
				  struct perf_sample *sample);

static int process_sample_event(struct perf_tool *tool __maybe_unused,
				union perf_event *event,
				struct perf_sample *sample,
				struct perf_evsel *evsel,
				struct machine *machine)
{
	struct thread *thread = machine__findnew_thread(machine, sample->pid,
							sample->tid);

	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			 event->header.type);
		return -1;
	}

	dump_printf(" ... thread: %s:%d\n", thread__comm_str(thread), thread->tid);

	if (evsel->handler != NULL) {
		tracepoint_handler f = evsel->handler;
		return f(evsel, sample);
	}

	return 0;
}

static struct perf_tool perf_kmem = {
	.sample		 = process_sample_event,
	.comm		 = perf_event__process_comm,
	.mmap		 = perf_event__process_mmap,
	.mmap2		 = perf_event__process_mmap2,
	.ordered_events	 = true,
};

static double fragmentation(unsigned long n_req, unsigned long n_alloc)
{
	if (n_alloc == 0)
		return 0.0;
	else
		return 100.0 - (100.0 * n_req / n_alloc);
}

static void __print_slab_result(struct rb_root *root,
				struct perf_session *session,
				int n_lines, int is_caller)
{
	struct rb_node *next;
	struct machine *machine = &session->machines.host;

	printf("%.105s\n", graph_dotted_line);
	printf(" %-34s |",  is_caller ? "Callsite": "Alloc Ptr");
	printf(" Total_alloc/Per | Total_req/Per   | Hit      | Ping-pong | Frag\n");
	printf("%.105s\n", graph_dotted_line);

	next = rb_first(root);

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

		printf(" %9llu/%-5lu | %9llu/%-5lu | %8lu | %9lu | %6.3f%%\n",
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
		printf(" ...                                | ...             | ...             | ...      | ...       | ...   \n");

	printf("%.105s\n", graph_dotted_line);
}

static const char * const migrate_type_str[] = {
	"UNMOVABL",
	"RECLAIM",
	"MOVABLE",
	"RESERVED",
	"CMA/ISLT",
	"UNKNOWN",
};

static void __print_page_result(struct rb_root *root,
				struct perf_session *session __maybe_unused,
				int n_lines)
{
	struct rb_node *next = rb_first(root);
	const char *format;

	printf("\n%.80s\n", graph_dotted_line);
	printf(" %-16s | Total alloc (KB) | Hits      | Order | Mig.type | GFP flags\n",
	       use_pfn ? "PFN" : "Page");
	printf("%.80s\n", graph_dotted_line);

	if (use_pfn)
		format = " %16llu | %'16llu | %'9d | %5d | %8s |  %08lx\n";
	else
		format = " %016llx | %'16llu | %'9d | %5d | %8s |  %08lx\n";

	while (next && n_lines--) {
		struct page_stat *data;

		data = rb_entry(next, struct page_stat, node);

		printf(format, (unsigned long long)data->page,
		       (unsigned long long)data->alloc_bytes / 1024,
		       data->nr_alloc, data->order,
		       migrate_type_str[data->migrate_type],
		       (unsigned long)data->gfp_flags);

		next = rb_next(next);
	}

	if (n_lines == -1)
		printf(" ...              | ...              | ...       | ...   | ...      | ...     \n");

	printf("%.80s\n", graph_dotted_line);
}

static void print_slab_summary(void)
{
	printf("\nSUMMARY (SLAB allocator)");
	printf("\n========================\n");
	printf("Total bytes requested: %'lu\n", total_requested);
	printf("Total bytes allocated: %'lu\n", total_allocated);
	printf("Total bytes wasted on internal fragmentation: %'lu\n",
	       total_allocated - total_requested);
	printf("Internal fragmentation: %f%%\n",
	       fragmentation(total_requested, total_allocated));
	printf("Cross CPU allocations: %'lu/%'lu\n", nr_cross_allocs, nr_allocs);
}

static void print_page_summary(void)
{
	int o, m;
	u64 nr_alloc_freed = nr_page_frees - nr_page_nomatch;
	u64 total_alloc_freed_bytes = total_page_free_bytes - total_page_nomatch_bytes;

	printf("\nSUMMARY (page allocator)");
	printf("\n========================\n");
	printf("%-30s: %'16lu   [ %'16"PRIu64" KB ]\n", "Total allocation requests",
	       nr_page_allocs, total_page_alloc_bytes / 1024);
	printf("%-30s: %'16lu   [ %'16"PRIu64" KB ]\n", "Total free requests",
	       nr_page_frees, total_page_free_bytes / 1024);
	printf("\n");

	printf("%-30s: %'16"PRIu64"   [ %'16"PRIu64" KB ]\n", "Total alloc+freed requests",
	       nr_alloc_freed, (total_alloc_freed_bytes) / 1024);
	printf("%-30s: %'16"PRIu64"   [ %'16"PRIu64" KB ]\n", "Total alloc-only requests",
	       nr_page_allocs - nr_alloc_freed,
	       (total_page_alloc_bytes - total_alloc_freed_bytes) / 1024);
	printf("%-30s: %'16lu   [ %'16"PRIu64" KB ]\n", "Total free-only requests",
	       nr_page_nomatch, total_page_nomatch_bytes / 1024);
	printf("\n");

	printf("%-30s: %'16lu   [ %'16"PRIu64" KB ]\n", "Total allocation failures",
	       nr_page_fails, total_page_fail_bytes / 1024);
	printf("\n");

	printf("%5s  %12s  %12s  %12s  %12s  %12s\n", "Order",  "Unmovable",
	       "Reclaimable", "Movable", "Reserved", "CMA/Isolated");
	printf("%.5s  %.12s  %.12s  %.12s  %.12s  %.12s\n", graph_dotted_line,
	       graph_dotted_line, graph_dotted_line, graph_dotted_line,
	       graph_dotted_line, graph_dotted_line);

	for (o = 0; o < MAX_PAGE_ORDER; o++) {
		printf("%5d", o);
		for (m = 0; m < MAX_MIGRATE_TYPES - 1; m++) {
			if (order_stats[o][m])
				printf("  %'12d", order_stats[o][m]);
			else
				printf("  %12c", '.');
		}
		printf("\n");
	}
}

static void print_slab_result(struct perf_session *session)
{
	if (caller_flag)
		__print_slab_result(&root_caller_sorted, session, caller_lines, 1);
	if (alloc_flag)
		__print_slab_result(&root_alloc_sorted, session, alloc_lines, 0);
	print_slab_summary();
}

static void print_page_result(struct perf_session *session)
{
	if (alloc_flag)
		__print_page_result(&page_alloc_sorted, session, alloc_lines);
	print_page_summary();
}

static void print_result(struct perf_session *session)
{
	if (kmem_slab)
		print_slab_result(session);
	if (kmem_page)
		print_page_result(session);
}

struct sort_dimension {
	const char		name[20];
	sort_fn_t		cmp;
	struct list_head	list;
};

static LIST_HEAD(caller_sort);
static LIST_HEAD(alloc_sort);

static void sort_slab_insert(struct rb_root *root, struct alloc_stat *data,
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

static void __sort_slab_result(struct rb_root *root, struct rb_root *root_sorted,
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
		sort_slab_insert(root_sorted, data, sort_list);
	}
}

static void sort_page_insert(struct rb_root *root, struct page_stat *data)
{
	struct rb_node **new = &root->rb_node;
	struct rb_node *parent = NULL;

	while (*new) {
		struct page_stat *this;
		int cmp = 0;

		this = rb_entry(*new, struct page_stat, node);
		parent = *new;

		/* TODO: support more sort key */
		cmp = data->alloc_bytes - this->alloc_bytes;

		if (cmp > 0)
			new = &parent->rb_left;
		else
			new = &parent->rb_right;
	}

	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

static void __sort_page_result(struct rb_root *root, struct rb_root *root_sorted)
{
	struct rb_node *node;
	struct page_stat *data;

	for (;;) {
		node = rb_first(root);
		if (!node)
			break;

		rb_erase(node, root);
		data = rb_entry(node, struct page_stat, node);
		sort_page_insert(root_sorted, data);
	}
}

static void sort_result(void)
{
	if (kmem_slab) {
		__sort_slab_result(&root_alloc_stat, &root_alloc_sorted,
				   &alloc_sort);
		__sort_slab_result(&root_caller_stat, &root_caller_sorted,
				   &caller_sort);
	}
	if (kmem_page) {
		__sort_page_result(&page_alloc_tree, &page_alloc_sorted);
	}
}

static int __cmd_kmem(struct perf_session *session)
{
	int err = -EINVAL;
	struct perf_evsel *evsel;
	const struct perf_evsel_str_handler kmem_tracepoints[] = {
		/* slab allocator */
		{ "kmem:kmalloc",		perf_evsel__process_alloc_event, },
    		{ "kmem:kmem_cache_alloc",	perf_evsel__process_alloc_event, },
		{ "kmem:kmalloc_node",		perf_evsel__process_alloc_node_event, },
    		{ "kmem:kmem_cache_alloc_node", perf_evsel__process_alloc_node_event, },
		{ "kmem:kfree",			perf_evsel__process_free_event, },
    		{ "kmem:kmem_cache_free",	perf_evsel__process_free_event, },
		/* page allocator */
		{ "kmem:mm_page_alloc",		perf_evsel__process_page_alloc_event, },
		{ "kmem:mm_page_free",		perf_evsel__process_page_free_event, },
	};

	if (!perf_session__has_traces(session, "kmem record"))
		goto out;

	if (perf_session__set_tracepoints_handlers(session, kmem_tracepoints)) {
		pr_err("Initializing perf session tracepoint handlers failed\n");
		goto out;
	}

	evlist__for_each(session->evlist, evsel) {
		if (!strcmp(perf_evsel__name(evsel), "kmem:mm_page_alloc") &&
		    perf_evsel__field(evsel, "pfn")) {
			use_pfn = true;
			break;
		}
	}

	setup_pager();
	err = perf_session__process_events(session);
	if (err != 0) {
		pr_err("error during process events: %d\n", err);
		goto out;
	}
	sort_result();
	print_result(session);
out:
	return err;
}

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

#define NUM_AVAIL_SORTS	((int)ARRAY_SIZE(avail_sorts))

static int sort_dimension__add(const char *tok, struct list_head *list)
{
	struct sort_dimension *sort;
	int i;

	for (i = 0; i < NUM_AVAIL_SORTS; i++) {
		if (!strcmp(avail_sorts[i]->name, tok)) {
			sort = memdup(avail_sorts[i], sizeof(*avail_sorts[i]));
			if (!sort) {
				pr_err("%s: memdup failed\n", __func__);
				return -1;
			}
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
	char *pos = str;

	if (!str) {
		pr_err("%s: strdup failed\n", __func__);
		return -1;
	}

	while (true) {
		tok = strsep(&pos, ",");
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

static int parse_sort_opt(const struct option *opt __maybe_unused,
			  const char *arg, int unset __maybe_unused)
{
	if (!arg)
		return -1;

	if (caller_flag > alloc_flag)
		return setup_sorting(&caller_sort, arg);
	else
		return setup_sorting(&alloc_sort, arg);

	return 0;
}

static int parse_caller_opt(const struct option *opt __maybe_unused,
			    const char *arg __maybe_unused,
			    int unset __maybe_unused)
{
	caller_flag = (alloc_flag + 1);
	return 0;
}

static int parse_alloc_opt(const struct option *opt __maybe_unused,
			   const char *arg __maybe_unused,
			   int unset __maybe_unused)
{
	alloc_flag = (caller_flag + 1);
	return 0;
}

static int parse_slab_opt(const struct option *opt __maybe_unused,
			  const char *arg __maybe_unused,
			  int unset __maybe_unused)
{
	kmem_slab = (kmem_page + 1);
	return 0;
}

static int parse_page_opt(const struct option *opt __maybe_unused,
			  const char *arg __maybe_unused,
			  int unset __maybe_unused)
{
	kmem_page = (kmem_slab + 1);
	return 0;
}

static int parse_line_opt(const struct option *opt __maybe_unused,
			  const char *arg, int unset __maybe_unused)
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

static int __cmd_record(int argc, const char **argv)
{
	const char * const record_args[] = {
	"record", "-a", "-R", "-c", "1",
	};
	const char * const slab_events[] = {
	"-e", "kmem:kmalloc",
	"-e", "kmem:kmalloc_node",
	"-e", "kmem:kfree",
	"-e", "kmem:kmem_cache_alloc",
	"-e", "kmem:kmem_cache_alloc_node",
	"-e", "kmem:kmem_cache_free",
	};
	const char * const page_events[] = {
	"-e", "kmem:mm_page_alloc",
	"-e", "kmem:mm_page_free",
	};
	unsigned int rec_argc, i, j;
	const char **rec_argv;

	rec_argc = ARRAY_SIZE(record_args) + argc - 1;
	if (kmem_slab)
		rec_argc += ARRAY_SIZE(slab_events);
	if (kmem_page)
		rec_argc += ARRAY_SIZE(page_events);

	rec_argv = calloc(rec_argc + 1, sizeof(char *));

	if (rec_argv == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[i] = strdup(record_args[i]);

	if (kmem_slab) {
		for (j = 0; j < ARRAY_SIZE(slab_events); j++, i++)
			rec_argv[i] = strdup(slab_events[j]);
	}
	if (kmem_page) {
		for (j = 0; j < ARRAY_SIZE(page_events); j++, i++)
			rec_argv[i] = strdup(page_events[j]);
	}

	for (j = 1; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	return cmd_record(i, rec_argv, NULL);
}

int cmd_kmem(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char * const default_sort_order = "frag,hit,bytes";
	struct perf_data_file file = {
		.mode = PERF_DATA_MODE_READ,
	};
	const struct option kmem_options[] = {
	OPT_STRING('i', "input", &input_name, "file", "input file name"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_CALLBACK_NOOPT(0, "caller", NULL, NULL,
			   "show per-callsite statistics", parse_caller_opt),
	OPT_CALLBACK_NOOPT(0, "alloc", NULL, NULL,
			   "show per-allocation statistics", parse_alloc_opt),
	OPT_CALLBACK('s', "sort", NULL, "key[,key2...]",
		     "sort by keys: ptr, call_site, bytes, hit, pingpong, frag",
		     parse_sort_opt),
	OPT_CALLBACK('l', "line", NULL, "num", "show n lines", parse_line_opt),
	OPT_BOOLEAN(0, "raw-ip", &raw_ip, "show raw ip instead of symbol"),
	OPT_BOOLEAN('f', "force", &file.force, "don't complain, do it"),
	OPT_CALLBACK_NOOPT(0, "slab", NULL, NULL, "Analyze slab allocator",
			   parse_slab_opt),
	OPT_CALLBACK_NOOPT(0, "page", NULL, NULL, "Analyze page allocator",
			   parse_page_opt),
	OPT_END()
	};
	const char *const kmem_subcommands[] = { "record", "stat", NULL };
	const char *kmem_usage[] = {
		NULL,
		NULL
	};
	struct perf_session *session;
	int ret = -1;

	argc = parse_options_subcommand(argc, argv, kmem_options,
					kmem_subcommands, kmem_usage, 0);

	if (!argc)
		usage_with_options(kmem_usage, kmem_options);

	if (kmem_slab == 0 && kmem_page == 0)
		kmem_slab = 1;  /* for backward compatibility */

	if (!strncmp(argv[0], "rec", 3)) {
		symbol__init(NULL);
		return __cmd_record(argc, argv);
	}

	file.path = input_name;

	session = perf_session__new(&file, false, &perf_kmem);
	if (session == NULL)
		return -1;

	if (kmem_page) {
		struct perf_evsel *evsel = perf_evlist__first(session->evlist);

		if (evsel == NULL || evsel->tp_format == NULL) {
			pr_err("invalid event found.. aborting\n");
			return -1;
		}

		kmem_page_size = pevent_get_page_size(evsel->tp_format->pevent);
	}

	symbol__init(&session->header.env);

	if (!strcmp(argv[0], "stat")) {
		setlocale(LC_ALL, "");

		if (cpu__setup_cpunode_map())
			goto out_delete;

		if (list_empty(&caller_sort))
			setup_sorting(&caller_sort, default_sort_order);
		if (list_empty(&alloc_sort))
			setup_sorting(&alloc_sort, default_sort_order);

		ret = __cmd_kmem(session);
	} else
		usage_with_options(kmem_usage, kmem_options);

out_delete:
	perf_session__delete(session);

	return ret;
}

