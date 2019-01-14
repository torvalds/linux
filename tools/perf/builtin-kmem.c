// SPDX-License-Identifier: GPL-2.0
#include "builtin.h"
#include "perf.h"

#include "util/evlist.h"
#include "util/evsel.h"
#include "util/util.h"
#include "util/config.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/header.h"
#include "util/session.h"
#include "util/tool.h"
#include "util/callchain.h"
#include "util/time-utils.h"

#include <subcmd/parse-options.h>
#include "util/trace-event.h"
#include "util/data.h"
#include "util/cpumap.h"

#include "util/debug.h"

#include <linux/kernel.h>
#include <linux/rbtree.h>
#include <linux/string.h>
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <regex.h>

#include "sane_ctype.h"

static int	kmem_slab;
static int	kmem_page;

static long	kmem_page_size;
static enum {
	KMEM_SLAB,
	KMEM_PAGE,
} kmem_default = KMEM_SLAB;  /* for backward compatibility */

struct alloc_stat;
typedef int (*sort_fn_t)(void *, void *);

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
	u64	last_alloc;
	u32	hit;
	u32	pingpong;

	short	alloc_cpu;

	struct rb_node node;
};

static struct rb_root root_alloc_stat;
static struct rb_root root_alloc_sorted;
static struct rb_root root_caller_stat;
static struct rb_root root_caller_sorted;

static unsigned long total_requested, total_allocated, total_freed;
static unsigned long nr_allocs, nr_cross_allocs;

/* filters for controlling start and stop of time of analysis */
static struct perf_time_interval ptime;
const char *time_str;

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
	data->last_alloc = bytes_alloc;

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

static int ptr_cmp(void *, void *);
static int slab_callsite_cmp(void *, void *);

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

	total_freed += s_alloc->last_alloc;

	if ((short)sample->cpu != s_alloc->alloc_cpu) {
		s_alloc->pingpong++;

		s_caller = search_alloc_stat(0, s_alloc->call_site,
					     &root_caller_stat,
					     slab_callsite_cmp);
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
static bool live_page;
static struct perf_session *kmem_session;

#define MAX_MIGRATE_TYPES  6
#define MAX_PAGE_ORDER     11

static int order_stats[MAX_PAGE_ORDER][MAX_MIGRATE_TYPES];

struct page_stat {
	struct rb_node 	node;
	u64 		page;
	u64 		callsite;
	int 		order;
	unsigned 	gfp_flags;
	unsigned 	migrate_type;
	u64		alloc_bytes;
	u64 		free_bytes;
	int 		nr_alloc;
	int 		nr_free;
};

static struct rb_root page_live_tree;
static struct rb_root page_alloc_tree;
static struct rb_root page_alloc_sorted;
static struct rb_root page_caller_tree;
static struct rb_root page_caller_sorted;

struct alloc_func {
	u64 start;
	u64 end;
	char *name;
};

static int nr_alloc_funcs;
static struct alloc_func *alloc_func_list;

static int funcmp(const void *a, const void *b)
{
	const struct alloc_func *fa = a;
	const struct alloc_func *fb = b;

	if (fa->start > fb->start)
		return 1;
	else
		return -1;
}

static int callcmp(const void *a, const void *b)
{
	const struct alloc_func *fa = a;
	const struct alloc_func *fb = b;

	if (fb->start <= fa->start && fa->end < fb->end)
		return 0;

	if (fa->start > fb->start)
		return 1;
	else
		return -1;
}

static int build_alloc_func_list(void)
{
	int ret;
	struct map *kernel_map;
	struct symbol *sym;
	struct rb_node *node;
	struct alloc_func *func;
	struct machine *machine = &kmem_session->machines.host;
	regex_t alloc_func_regex;
	const char pattern[] = "^_?_?(alloc|get_free|get_zeroed)_pages?";

	ret = regcomp(&alloc_func_regex, pattern, REG_EXTENDED);
	if (ret) {
		char err[BUFSIZ];

		regerror(ret, &alloc_func_regex, err, sizeof(err));
		pr_err("Invalid regex: %s\n%s", pattern, err);
		return -EINVAL;
	}

	kernel_map = machine__kernel_map(machine);
	if (map__load(kernel_map) < 0) {
		pr_err("cannot load kernel map\n");
		return -ENOENT;
	}

	map__for_each_symbol(kernel_map, sym, node) {
		if (regexec(&alloc_func_regex, sym->name, 0, NULL, 0))
			continue;

		func = realloc(alloc_func_list,
			       (nr_alloc_funcs + 1) * sizeof(*func));
		if (func == NULL)
			return -ENOMEM;

		pr_debug("alloc func: %s\n", sym->name);
		func[nr_alloc_funcs].start = sym->start;
		func[nr_alloc_funcs].end   = sym->end;
		func[nr_alloc_funcs].name  = sym->name;

		alloc_func_list = func;
		nr_alloc_funcs++;
	}

	qsort(alloc_func_list, nr_alloc_funcs, sizeof(*func), funcmp);

	regfree(&alloc_func_regex);
	return 0;
}

/*
 * Find first non-memory allocation function from callchain.
 * The allocation functions are in the 'alloc_func_list'.
 */
static u64 find_callsite(struct perf_evsel *evsel, struct perf_sample *sample)
{
	struct addr_location al;
	struct machine *machine = &kmem_session->machines.host;
	struct callchain_cursor_node *node;

	if (alloc_func_list == NULL) {
		if (build_alloc_func_list() < 0)
			goto out;
	}

	al.thread = machine__findnew_thread(machine, sample->pid, sample->tid);
	sample__resolve_callchain(sample, &callchain_cursor, NULL, evsel, &al, 16);

	callchain_cursor_commit(&callchain_cursor);
	while (true) {
		struct alloc_func key, *caller;
		u64 addr;

		node = callchain_cursor_current(&callchain_cursor);
		if (node == NULL)
			break;

		key.start = key.end = node->ip;
		caller = bsearch(&key, alloc_func_list, nr_alloc_funcs,
				 sizeof(key), callcmp);
		if (!caller) {
			/* found */
			if (node->map)
				addr = map__unmap_ip(node->map, node->ip);
			else
				addr = node->ip;

			return addr;
		} else
			pr_debug3("skipping alloc function: %s\n", caller->name);

		callchain_cursor_advance(&callchain_cursor);
	}

out:
	pr_debug2("unknown callsite: %"PRIx64 "\n", sample->ip);
	return sample->ip;
}

struct sort_dimension {
	const char		name[20];
	sort_fn_t		cmp;
	struct list_head	list;
};

static LIST_HEAD(page_alloc_sort_input);
static LIST_HEAD(page_caller_sort_input);

static struct page_stat *
__page_stat__findnew_page(struct page_stat *pstat, bool create)
{
	struct rb_node **node = &page_live_tree.rb_node;
	struct rb_node *parent = NULL;
	struct page_stat *data;

	while (*node) {
		s64 cmp;

		parent = *node;
		data = rb_entry(*node, struct page_stat, node);

		cmp = data->page - pstat->page;
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
		rb_insert_color(&data->node, &page_live_tree);
	}

	return data;
}

static struct page_stat *page_stat__find_page(struct page_stat *pstat)
{
	return __page_stat__findnew_page(pstat, false);
}

static struct page_stat *page_stat__findnew_page(struct page_stat *pstat)
{
	return __page_stat__findnew_page(pstat, true);
}

static struct page_stat *
__page_stat__findnew_alloc(struct page_stat *pstat, bool create)
{
	struct rb_node **node = &page_alloc_tree.rb_node;
	struct rb_node *parent = NULL;
	struct page_stat *data;
	struct sort_dimension *sort;

	while (*node) {
		int cmp = 0;

		parent = *node;
		data = rb_entry(*node, struct page_stat, node);

		list_for_each_entry(sort, &page_alloc_sort_input, list) {
			cmp = sort->cmp(pstat, data);
			if (cmp)
				break;
		}

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

static struct page_stat *page_stat__find_alloc(struct page_stat *pstat)
{
	return __page_stat__findnew_alloc(pstat, false);
}

static struct page_stat *page_stat__findnew_alloc(struct page_stat *pstat)
{
	return __page_stat__findnew_alloc(pstat, true);
}

static struct page_stat *
__page_stat__findnew_caller(struct page_stat *pstat, bool create)
{
	struct rb_node **node = &page_caller_tree.rb_node;
	struct rb_node *parent = NULL;
	struct page_stat *data;
	struct sort_dimension *sort;

	while (*node) {
		int cmp = 0;

		parent = *node;
		data = rb_entry(*node, struct page_stat, node);

		list_for_each_entry(sort, &page_caller_sort_input, list) {
			cmp = sort->cmp(pstat, data);
			if (cmp)
				break;
		}

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
		data->callsite = pstat->callsite;
		data->order = pstat->order;
		data->gfp_flags = pstat->gfp_flags;
		data->migrate_type = pstat->migrate_type;

		rb_link_node(&data->node, parent, node);
		rb_insert_color(&data->node, &page_caller_tree);
	}

	return data;
}

static struct page_stat *page_stat__find_caller(struct page_stat *pstat)
{
	return __page_stat__findnew_caller(pstat, false);
}

static struct page_stat *page_stat__findnew_caller(struct page_stat *pstat)
{
	return __page_stat__findnew_caller(pstat, true);
}

static bool valid_page(u64 pfn_or_page)
{
	if (use_pfn && pfn_or_page == -1UL)
		return false;
	if (!use_pfn && pfn_or_page == 0)
		return false;
	return true;
}

struct gfp_flag {
	unsigned int flags;
	char *compact_str;
	char *human_readable;
};

static struct gfp_flag *gfps;
static int nr_gfps;

static int gfpcmp(const void *a, const void *b)
{
	const struct gfp_flag *fa = a;
	const struct gfp_flag *fb = b;

	return fa->flags - fb->flags;
}

/* see include/trace/events/mmflags.h */
static const struct {
	const char *original;
	const char *compact;
} gfp_compact_table[] = {
	{ "GFP_TRANSHUGE",		"THP" },
	{ "GFP_TRANSHUGE_LIGHT",	"THL" },
	{ "GFP_HIGHUSER_MOVABLE",	"HUM" },
	{ "GFP_HIGHUSER",		"HU" },
	{ "GFP_USER",			"U" },
	{ "GFP_KERNEL_ACCOUNT",		"KAC" },
	{ "GFP_KERNEL",			"K" },
	{ "GFP_NOFS",			"NF" },
	{ "GFP_ATOMIC",			"A" },
	{ "GFP_NOIO",			"NI" },
	{ "GFP_NOWAIT",			"NW" },
	{ "GFP_DMA",			"D" },
	{ "__GFP_HIGHMEM",		"HM" },
	{ "GFP_DMA32",			"D32" },
	{ "__GFP_HIGH",			"H" },
	{ "__GFP_ATOMIC",		"_A" },
	{ "__GFP_IO",			"I" },
	{ "__GFP_FS",			"F" },
	{ "__GFP_NOWARN",		"NWR" },
	{ "__GFP_RETRY_MAYFAIL",	"R" },
	{ "__GFP_NOFAIL",		"NF" },
	{ "__GFP_NORETRY",		"NR" },
	{ "__GFP_COMP",			"C" },
	{ "__GFP_ZERO",			"Z" },
	{ "__GFP_NOMEMALLOC",		"NMA" },
	{ "__GFP_MEMALLOC",		"MA" },
	{ "__GFP_HARDWALL",		"HW" },
	{ "__GFP_THISNODE",		"TN" },
	{ "__GFP_RECLAIMABLE",		"RC" },
	{ "__GFP_MOVABLE",		"M" },
	{ "__GFP_ACCOUNT",		"AC" },
	{ "__GFP_WRITE",		"WR" },
	{ "__GFP_RECLAIM",		"R" },
	{ "__GFP_DIRECT_RECLAIM",	"DR" },
	{ "__GFP_KSWAPD_RECLAIM",	"KR" },
};

static size_t max_gfp_len;

static char *compact_gfp_flags(char *gfp_flags)
{
	char *orig_flags = strdup(gfp_flags);
	char *new_flags = NULL;
	char *str, *pos = NULL;
	size_t len = 0;

	if (orig_flags == NULL)
		return NULL;

	str = strtok_r(orig_flags, "|", &pos);
	while (str) {
		size_t i;
		char *new;
		const char *cpt;

		for (i = 0; i < ARRAY_SIZE(gfp_compact_table); i++) {
			if (strcmp(gfp_compact_table[i].original, str))
				continue;

			cpt = gfp_compact_table[i].compact;
			new = realloc(new_flags, len + strlen(cpt) + 2);
			if (new == NULL) {
				free(new_flags);
				return NULL;
			}

			new_flags = new;

			if (!len) {
				strcpy(new_flags, cpt);
			} else {
				strcat(new_flags, "|");
				strcat(new_flags, cpt);
				len++;
			}

			len += strlen(cpt);
		}

		str = strtok_r(NULL, "|", &pos);
	}

	if (max_gfp_len < len)
		max_gfp_len = len;

	free(orig_flags);
	return new_flags;
}

static char *compact_gfp_string(unsigned long gfp_flags)
{
	struct gfp_flag key = {
		.flags = gfp_flags,
	};
	struct gfp_flag *gfp;

	gfp = bsearch(&key, gfps, nr_gfps, sizeof(*gfps), gfpcmp);
	if (gfp)
		return gfp->compact_str;

	return NULL;
}

static int parse_gfp_flags(struct perf_evsel *evsel, struct perf_sample *sample,
			   unsigned int gfp_flags)
{
	struct tep_record record = {
		.cpu = sample->cpu,
		.data = sample->raw_data,
		.size = sample->raw_size,
	};
	struct trace_seq seq;
	char *str, *pos = NULL;

	if (nr_gfps) {
		struct gfp_flag key = {
			.flags = gfp_flags,
		};

		if (bsearch(&key, gfps, nr_gfps, sizeof(*gfps), gfpcmp))
			return 0;
	}

	trace_seq_init(&seq);
	tep_event_info(&seq, evsel->tp_format, &record);

	str = strtok_r(seq.buffer, " ", &pos);
	while (str) {
		if (!strncmp(str, "gfp_flags=", 10)) {
			struct gfp_flag *new;

			new = realloc(gfps, (nr_gfps + 1) * sizeof(*gfps));
			if (new == NULL)
				return -ENOMEM;

			gfps = new;
			new += nr_gfps++;

			new->flags = gfp_flags;
			new->human_readable = strdup(str + 10);
			new->compact_str = compact_gfp_flags(str + 10);
			if (!new->human_readable || !new->compact_str)
				return -ENOMEM;

			qsort(gfps, nr_gfps, sizeof(*gfps), gfpcmp);
		}

		str = strtok_r(NULL, " ", &pos);
	}

	trace_seq_destroy(&seq);
	return 0;
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
	u64 callsite;
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

	if (parse_gfp_flags(evsel, sample, gfp_flags) < 0)
		return -1;

	callsite = find_callsite(evsel, sample);

	/*
	 * This is to find the current page (with correct gfp flags and
	 * migrate type) at free event.
	 */
	this.page = page;
	pstat = page_stat__findnew_page(&this);
	if (pstat == NULL)
		return -ENOMEM;

	pstat->nr_alloc++;
	pstat->alloc_bytes += bytes;
	pstat->callsite = callsite;

	if (!live_page) {
		pstat = page_stat__findnew_alloc(&this);
		if (pstat == NULL)
			return -ENOMEM;

		pstat->nr_alloc++;
		pstat->alloc_bytes += bytes;
		pstat->callsite = callsite;
	}

	this.callsite = callsite;
	pstat = page_stat__findnew_caller(&this);
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

	this.page = page;
	pstat = page_stat__find_page(&this);
	if (pstat == NULL) {
		pr_debug2("missing free at page %"PRIx64" (order: %d)\n",
			  page, order);

		nr_page_nomatch++;
		total_page_nomatch_bytes += bytes;

		return 0;
	}

	this.gfp_flags = pstat->gfp_flags;
	this.migrate_type = pstat->migrate_type;
	this.callsite = pstat->callsite;

	rb_erase(&pstat->node, &page_live_tree);
	free(pstat);

	if (live_page) {
		order_stats[this.order][this.migrate_type]--;
	} else {
		pstat = page_stat__find_alloc(&this);
		if (pstat == NULL)
			return -ENOMEM;

		pstat->nr_free++;
		pstat->free_bytes += bytes;
	}

	pstat = page_stat__find_caller(&this);
	if (pstat == NULL)
		return -ENOENT;

	pstat->nr_free++;
	pstat->free_bytes += bytes;

	if (live_page) {
		pstat->nr_alloc--;
		pstat->alloc_bytes -= bytes;

		if (pstat->nr_alloc == 0) {
			rb_erase(&pstat->node, &page_caller_tree);
			free(pstat);
		}
	}

	return 0;
}

static bool perf_kmem__skip_sample(struct perf_sample *sample)
{
	/* skip sample based on time? */
	if (perf_time__skip_sample(&ptime, sample->time))
		return true;

	return false;
}

typedef int (*tracepoint_handler)(struct perf_evsel *evsel,
				  struct perf_sample *sample);

static int process_sample_event(struct perf_tool *tool __maybe_unused,
				union perf_event *event,
				struct perf_sample *sample,
				struct perf_evsel *evsel,
				struct machine *machine)
{
	int err = 0;
	struct thread *thread = machine__findnew_thread(machine, sample->pid,
							sample->tid);

	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			 event->header.type);
		return -1;
	}

	if (perf_kmem__skip_sample(sample))
		return 0;

	dump_printf(" ... thread: %s:%d\n", thread__comm_str(thread), thread->tid);

	if (evsel->handler != NULL) {
		tracepoint_handler f = evsel->handler;
		err = f(evsel, sample);
	}

	thread__put(thread);

	return err;
}

static struct perf_tool perf_kmem = {
	.sample		 = process_sample_event,
	.comm		 = perf_event__process_comm,
	.mmap		 = perf_event__process_mmap,
	.mmap2		 = perf_event__process_mmap2,
	.namespaces	 = perf_event__process_namespaces,
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
				sym = machine__find_kernel_symbol(machine, addr, &map);
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

static void __print_page_alloc_result(struct perf_session *session, int n_lines)
{
	struct rb_node *next = rb_first(&page_alloc_sorted);
	struct machine *machine = &session->machines.host;
	const char *format;
	int gfp_len = max(strlen("GFP flags"), max_gfp_len);

	printf("\n%.105s\n", graph_dotted_line);
	printf(" %-16s | %5s alloc (KB) | Hits      | Order | Mig.type | %-*s | Callsite\n",
	       use_pfn ? "PFN" : "Page", live_page ? "Live" : "Total",
	       gfp_len, "GFP flags");
	printf("%.105s\n", graph_dotted_line);

	if (use_pfn)
		format = " %16llu | %'16llu | %'9d | %5d | %8s | %-*s | %s\n";
	else
		format = " %016llx | %'16llu | %'9d | %5d | %8s | %-*s | %s\n";

	while (next && n_lines--) {
		struct page_stat *data;
		struct symbol *sym;
		struct map *map;
		char buf[32];
		char *caller = buf;

		data = rb_entry(next, struct page_stat, node);
		sym = machine__find_kernel_symbol(machine, data->callsite, &map);
		if (sym)
			caller = sym->name;
		else
			scnprintf(buf, sizeof(buf), "%"PRIx64, data->callsite);

		printf(format, (unsigned long long)data->page,
		       (unsigned long long)data->alloc_bytes / 1024,
		       data->nr_alloc, data->order,
		       migrate_type_str[data->migrate_type],
		       gfp_len, compact_gfp_string(data->gfp_flags), caller);

		next = rb_next(next);
	}

	if (n_lines == -1) {
		printf(" ...              | ...              | ...       | ...   | ...      | %-*s | ...\n",
		       gfp_len, "...");
	}

	printf("%.105s\n", graph_dotted_line);
}

static void __print_page_caller_result(struct perf_session *session, int n_lines)
{
	struct rb_node *next = rb_first(&page_caller_sorted);
	struct machine *machine = &session->machines.host;
	int gfp_len = max(strlen("GFP flags"), max_gfp_len);

	printf("\n%.105s\n", graph_dotted_line);
	printf(" %5s alloc (KB) | Hits      | Order | Mig.type | %-*s | Callsite\n",
	       live_page ? "Live" : "Total", gfp_len, "GFP flags");
	printf("%.105s\n", graph_dotted_line);

	while (next && n_lines--) {
		struct page_stat *data;
		struct symbol *sym;
		struct map *map;
		char buf[32];
		char *caller = buf;

		data = rb_entry(next, struct page_stat, node);
		sym = machine__find_kernel_symbol(machine, data->callsite, &map);
		if (sym)
			caller = sym->name;
		else
			scnprintf(buf, sizeof(buf), "%"PRIx64, data->callsite);

		printf(" %'16llu | %'9d | %5d | %8s | %-*s | %s\n",
		       (unsigned long long)data->alloc_bytes / 1024,
		       data->nr_alloc, data->order,
		       migrate_type_str[data->migrate_type],
		       gfp_len, compact_gfp_string(data->gfp_flags), caller);

		next = rb_next(next);
	}

	if (n_lines == -1) {
		printf(" ...              | ...       | ...   | ...      | %-*s | ...\n",
		       gfp_len, "...");
	}

	printf("%.105s\n", graph_dotted_line);
}

static void print_gfp_flags(void)
{
	int i;

	printf("#\n");
	printf("# GFP flags\n");
	printf("# ---------\n");
	for (i = 0; i < nr_gfps; i++) {
		printf("# %08x: %*s: %s\n", gfps[i].flags,
		       (int) max_gfp_len, gfps[i].compact_str,
		       gfps[i].human_readable);
	}
}

static void print_slab_summary(void)
{
	printf("\nSUMMARY (SLAB allocator)");
	printf("\n========================\n");
	printf("Total bytes requested: %'lu\n", total_requested);
	printf("Total bytes allocated: %'lu\n", total_allocated);
	printf("Total bytes freed:     %'lu\n", total_freed);
	if (total_allocated > total_freed) {
		printf("Net total bytes allocated: %'lu\n",
		total_allocated - total_freed);
	}
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
	if (caller_flag || alloc_flag)
		print_gfp_flags();
	if (caller_flag)
		__print_page_caller_result(session, caller_lines);
	if (alloc_flag)
		__print_page_alloc_result(session, alloc_lines);
	print_page_summary();
}

static void print_result(struct perf_session *session)
{
	if (kmem_slab)
		print_slab_result(session);
	if (kmem_page)
		print_page_result(session);
}

static LIST_HEAD(slab_caller_sort);
static LIST_HEAD(slab_alloc_sort);
static LIST_HEAD(page_caller_sort);
static LIST_HEAD(page_alloc_sort);

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

static void sort_page_insert(struct rb_root *root, struct page_stat *data,
			     struct list_head *sort_list)
{
	struct rb_node **new = &root->rb_node;
	struct rb_node *parent = NULL;
	struct sort_dimension *sort;

	while (*new) {
		struct page_stat *this;
		int cmp = 0;

		this = rb_entry(*new, struct page_stat, node);
		parent = *new;

		list_for_each_entry(sort, sort_list, list) {
			cmp = sort->cmp(data, this);
			if (cmp)
				break;
		}

		if (cmp > 0)
			new = &parent->rb_left;
		else
			new = &parent->rb_right;
	}

	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

static void __sort_page_result(struct rb_root *root, struct rb_root *root_sorted,
			       struct list_head *sort_list)
{
	struct rb_node *node;
	struct page_stat *data;

	for (;;) {
		node = rb_first(root);
		if (!node)
			break;

		rb_erase(node, root);
		data = rb_entry(node, struct page_stat, node);
		sort_page_insert(root_sorted, data, sort_list);
	}
}

static void sort_result(void)
{
	if (kmem_slab) {
		__sort_slab_result(&root_alloc_stat, &root_alloc_sorted,
				   &slab_alloc_sort);
		__sort_slab_result(&root_caller_stat, &root_caller_sorted,
				   &slab_caller_sort);
	}
	if (kmem_page) {
		if (live_page)
			__sort_page_result(&page_live_tree, &page_alloc_sorted,
					   &page_alloc_sort);
		else
			__sort_page_result(&page_alloc_tree, &page_alloc_sorted,
					   &page_alloc_sort);

		__sort_page_result(&page_caller_tree, &page_caller_sorted,
				   &page_caller_sort);
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

	evlist__for_each_entry(session->evlist, evsel) {
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

/* slab sort keys */
static int ptr_cmp(void *a, void *b)
{
	struct alloc_stat *l = a;
	struct alloc_stat *r = b;

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

static int slab_callsite_cmp(void *a, void *b)
{
	struct alloc_stat *l = a;
	struct alloc_stat *r = b;

	if (l->call_site < r->call_site)
		return -1;
	else if (l->call_site > r->call_site)
		return 1;
	return 0;
}

static struct sort_dimension callsite_sort_dimension = {
	.name	= "callsite",
	.cmp	= slab_callsite_cmp,
};

static int hit_cmp(void *a, void *b)
{
	struct alloc_stat *l = a;
	struct alloc_stat *r = b;

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

static int bytes_cmp(void *a, void *b)
{
	struct alloc_stat *l = a;
	struct alloc_stat *r = b;

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

static int frag_cmp(void *a, void *b)
{
	double x, y;
	struct alloc_stat *l = a;
	struct alloc_stat *r = b;

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

static int pingpong_cmp(void *a, void *b)
{
	struct alloc_stat *l = a;
	struct alloc_stat *r = b;

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

/* page sort keys */
static int page_cmp(void *a, void *b)
{
	struct page_stat *l = a;
	struct page_stat *r = b;

	if (l->page < r->page)
		return -1;
	else if (l->page > r->page)
		return 1;
	return 0;
}

static struct sort_dimension page_sort_dimension = {
	.name	= "page",
	.cmp	= page_cmp,
};

static int page_callsite_cmp(void *a, void *b)
{
	struct page_stat *l = a;
	struct page_stat *r = b;

	if (l->callsite < r->callsite)
		return -1;
	else if (l->callsite > r->callsite)
		return 1;
	return 0;
}

static struct sort_dimension page_callsite_sort_dimension = {
	.name	= "callsite",
	.cmp	= page_callsite_cmp,
};

static int page_hit_cmp(void *a, void *b)
{
	struct page_stat *l = a;
	struct page_stat *r = b;

	if (l->nr_alloc < r->nr_alloc)
		return -1;
	else if (l->nr_alloc > r->nr_alloc)
		return 1;
	return 0;
}

static struct sort_dimension page_hit_sort_dimension = {
	.name	= "hit",
	.cmp	= page_hit_cmp,
};

static int page_bytes_cmp(void *a, void *b)
{
	struct page_stat *l = a;
	struct page_stat *r = b;

	if (l->alloc_bytes < r->alloc_bytes)
		return -1;
	else if (l->alloc_bytes > r->alloc_bytes)
		return 1;
	return 0;
}

static struct sort_dimension page_bytes_sort_dimension = {
	.name	= "bytes",
	.cmp	= page_bytes_cmp,
};

static int page_order_cmp(void *a, void *b)
{
	struct page_stat *l = a;
	struct page_stat *r = b;

	if (l->order < r->order)
		return -1;
	else if (l->order > r->order)
		return 1;
	return 0;
}

static struct sort_dimension page_order_sort_dimension = {
	.name	= "order",
	.cmp	= page_order_cmp,
};

static int migrate_type_cmp(void *a, void *b)
{
	struct page_stat *l = a;
	struct page_stat *r = b;

	/* for internal use to find free'd page */
	if (l->migrate_type == -1U)
		return 0;

	if (l->migrate_type < r->migrate_type)
		return -1;
	else if (l->migrate_type > r->migrate_type)
		return 1;
	return 0;
}

static struct sort_dimension migrate_type_sort_dimension = {
	.name	= "migtype",
	.cmp	= migrate_type_cmp,
};

static int gfp_flags_cmp(void *a, void *b)
{
	struct page_stat *l = a;
	struct page_stat *r = b;

	/* for internal use to find free'd page */
	if (l->gfp_flags == -1U)
		return 0;

	if (l->gfp_flags < r->gfp_flags)
		return -1;
	else if (l->gfp_flags > r->gfp_flags)
		return 1;
	return 0;
}

static struct sort_dimension gfp_flags_sort_dimension = {
	.name	= "gfp",
	.cmp	= gfp_flags_cmp,
};

static struct sort_dimension *slab_sorts[] = {
	&ptr_sort_dimension,
	&callsite_sort_dimension,
	&hit_sort_dimension,
	&bytes_sort_dimension,
	&frag_sort_dimension,
	&pingpong_sort_dimension,
};

static struct sort_dimension *page_sorts[] = {
	&page_sort_dimension,
	&page_callsite_sort_dimension,
	&page_hit_sort_dimension,
	&page_bytes_sort_dimension,
	&page_order_sort_dimension,
	&migrate_type_sort_dimension,
	&gfp_flags_sort_dimension,
};

static int slab_sort_dimension__add(const char *tok, struct list_head *list)
{
	struct sort_dimension *sort;
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(slab_sorts); i++) {
		if (!strcmp(slab_sorts[i]->name, tok)) {
			sort = memdup(slab_sorts[i], sizeof(*slab_sorts[i]));
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

static int page_sort_dimension__add(const char *tok, struct list_head *list)
{
	struct sort_dimension *sort;
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(page_sorts); i++) {
		if (!strcmp(page_sorts[i]->name, tok)) {
			sort = memdup(page_sorts[i], sizeof(*page_sorts[i]));
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

static int setup_slab_sorting(struct list_head *sort_list, const char *arg)
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
		if (slab_sort_dimension__add(tok, sort_list) < 0) {
			pr_err("Unknown slab --sort key: '%s'", tok);
			free(str);
			return -1;
		}
	}

	free(str);
	return 0;
}

static int setup_page_sorting(struct list_head *sort_list, const char *arg)
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
		if (page_sort_dimension__add(tok, sort_list) < 0) {
			pr_err("Unknown page --sort key: '%s'", tok);
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

	if (kmem_page > kmem_slab ||
	    (kmem_page == 0 && kmem_slab == 0 && kmem_default == KMEM_PAGE)) {
		if (caller_flag > alloc_flag)
			return setup_page_sorting(&page_caller_sort, arg);
		else
			return setup_page_sorting(&page_alloc_sort, arg);
	} else {
		if (caller_flag > alloc_flag)
			return setup_slab_sorting(&slab_caller_sort, arg);
		else
			return setup_slab_sorting(&slab_alloc_sort, arg);
	}

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
		rec_argc += ARRAY_SIZE(page_events) + 1; /* for -g */

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
		rec_argv[i++] = strdup("-g");

		for (j = 0; j < ARRAY_SIZE(page_events); j++, i++)
			rec_argv[i] = strdup(page_events[j]);
	}

	for (j = 1; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	return cmd_record(i, rec_argv);
}

static int kmem_config(const char *var, const char *value, void *cb __maybe_unused)
{
	if (!strcmp(var, "kmem.default")) {
		if (!strcmp(value, "slab"))
			kmem_default = KMEM_SLAB;
		else if (!strcmp(value, "page"))
			kmem_default = KMEM_PAGE;
		else
			pr_err("invalid default value ('slab' or 'page' required): %s\n",
			       value);
		return 0;
	}

	return 0;
}

int cmd_kmem(int argc, const char **argv)
{
	const char * const default_slab_sort = "frag,hit,bytes";
	const char * const default_page_sort = "bytes,hit";
	struct perf_data data = {
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
		     "sort by keys: ptr, callsite, bytes, hit, pingpong, frag, "
		     "page, order, migtype, gfp", parse_sort_opt),
	OPT_CALLBACK('l', "line", NULL, "num", "show n lines", parse_line_opt),
	OPT_BOOLEAN(0, "raw-ip", &raw_ip, "show raw ip instead of symbol"),
	OPT_BOOLEAN('f', "force", &data.force, "don't complain, do it"),
	OPT_CALLBACK_NOOPT(0, "slab", NULL, NULL, "Analyze slab allocator",
			   parse_slab_opt),
	OPT_CALLBACK_NOOPT(0, "page", NULL, NULL, "Analyze page allocator",
			   parse_page_opt),
	OPT_BOOLEAN(0, "live", &live_page, "Show live page stat"),
	OPT_STRING(0, "time", &time_str, "str",
		   "Time span of interest (start,stop)"),
	OPT_END()
	};
	const char *const kmem_subcommands[] = { "record", "stat", NULL };
	const char *kmem_usage[] = {
		NULL,
		NULL
	};
	struct perf_session *session;
	const char errmsg[] = "No %s allocation events found.  Have you run 'perf kmem record --%s'?\n";
	int ret = perf_config(kmem_config, NULL);

	if (ret)
		return ret;

	argc = parse_options_subcommand(argc, argv, kmem_options,
					kmem_subcommands, kmem_usage, 0);

	if (!argc)
		usage_with_options(kmem_usage, kmem_options);

	if (kmem_slab == 0 && kmem_page == 0) {
		if (kmem_default == KMEM_SLAB)
			kmem_slab = 1;
		else
			kmem_page = 1;
	}

	if (!strncmp(argv[0], "rec", 3)) {
		symbol__init(NULL);
		return __cmd_record(argc, argv);
	}

	data.file.path = input_name;

	kmem_session = session = perf_session__new(&data, false, &perf_kmem);
	if (session == NULL)
		return -1;

	ret = -1;

	if (kmem_slab) {
		if (!perf_evlist__find_tracepoint_by_name(session->evlist,
							  "kmem:kmalloc")) {
			pr_err(errmsg, "slab", "slab");
			goto out_delete;
		}
	}

	if (kmem_page) {
		struct perf_evsel *evsel;

		evsel = perf_evlist__find_tracepoint_by_name(session->evlist,
							     "kmem:mm_page_alloc");
		if (evsel == NULL) {
			pr_err(errmsg, "page", "page");
			goto out_delete;
		}

		kmem_page_size = tep_get_page_size(evsel->tp_format->pevent);
		symbol_conf.use_callchain = true;
	}

	symbol__init(&session->header.env);

	if (perf_time__parse_str(&ptime, time_str) != 0) {
		pr_err("Invalid time string\n");
		ret = -EINVAL;
		goto out_delete;
	}

	if (!strcmp(argv[0], "stat")) {
		setlocale(LC_ALL, "");

		if (cpu__setup_cpunode_map())
			goto out_delete;

		if (list_empty(&slab_caller_sort))
			setup_slab_sorting(&slab_caller_sort, default_slab_sort);
		if (list_empty(&slab_alloc_sort))
			setup_slab_sorting(&slab_alloc_sort, default_slab_sort);
		if (list_empty(&page_caller_sort))
			setup_page_sorting(&page_caller_sort, default_page_sort);
		if (list_empty(&page_alloc_sort))
			setup_page_sorting(&page_alloc_sort, default_page_sort);

		if (kmem_page) {
			setup_page_sorting(&page_alloc_sort_input,
					   "page,order,migtype,gfp");
			setup_page_sorting(&page_caller_sort_input,
					   "callsite,order,migtype,gfp");
		}
		ret = __cmd_kmem(session);
	} else
		usage_with_options(kmem_usage, kmem_options);

out_delete:
	perf_session__delete(session);

	return ret;
}

