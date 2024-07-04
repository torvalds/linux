// SPDX-License-Identifier: GPL-2.0
/*
 * This is rewrite of original c2c tool introduced in here:
 *   http://lwn.net/Articles/588866/
 *
 * The original tool was changed to fit in current perf state.
 *
 * Original authors:
 *   Don Zickus <dzickus@redhat.com>
 *   Dick Fowles <fowles@inreach.com>
 *   Joe Mario <jmario@redhat.com>
 */
#include <errno.h>
#include <inttypes.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/stringify.h>
#include <linux/zalloc.h>
#include <asm/bug.h>
#include <sys/param.h>
#include "debug.h"
#include "builtin.h"
#include <perf/cpumap.h>
#include <subcmd/pager.h>
#include <subcmd/parse-options.h>
#include "map_symbol.h"
#include "mem-events.h"
#include "session.h"
#include "hist.h"
#include "sort.h"
#include "tool.h"
#include "cacheline.h"
#include "data.h"
#include "event.h"
#include "evlist.h"
#include "evsel.h"
#include "ui/browsers/hists.h"
#include "thread.h"
#include "mem2node.h"
#include "mem-info.h"
#include "symbol.h"
#include "ui/ui.h"
#include "ui/progress.h"
#include "pmus.h"
#include "string2.h"
#include "util/util.h"

struct c2c_hists {
	struct hists		hists;
	struct perf_hpp_list	list;
	struct c2c_stats	stats;
};

struct compute_stats {
	struct stats		 lcl_hitm;
	struct stats		 rmt_hitm;
	struct stats		 lcl_peer;
	struct stats		 rmt_peer;
	struct stats		 load;
};

struct c2c_hist_entry {
	struct c2c_hists	*hists;
	struct c2c_stats	 stats;
	unsigned long		*cpuset;
	unsigned long		*nodeset;
	struct c2c_stats	*node_stats;
	unsigned int		 cacheline_idx;

	struct compute_stats	 cstats;

	unsigned long		 paddr;
	unsigned long		 paddr_cnt;
	bool			 paddr_zero;
	char			*nodestr;

	/*
	 * must be at the end,
	 * because of its callchain dynamic entry
	 */
	struct hist_entry	he;
};

static char const *coalesce_default = "iaddr";

struct perf_c2c {
	struct perf_tool	tool;
	struct c2c_hists	hists;
	struct mem2node		mem2node;

	unsigned long		**nodes;
	int			 nodes_cnt;
	int			 cpus_cnt;
	int			*cpu2node;
	int			 node_info;

	bool			 show_src;
	bool			 show_all;
	bool			 use_stdio;
	bool			 stats_only;
	bool			 symbol_full;
	bool			 stitch_lbr;

	/* Shared cache line stats */
	struct c2c_stats	shared_clines_stats;
	int			shared_clines;

	int			 display;

	const char		*coalesce;
	char			*cl_sort;
	char			*cl_resort;
	char			*cl_output;
};

enum {
	DISPLAY_LCL_HITM,
	DISPLAY_RMT_HITM,
	DISPLAY_TOT_HITM,
	DISPLAY_SNP_PEER,
	DISPLAY_MAX,
};

static const char *display_str[DISPLAY_MAX] = {
	[DISPLAY_LCL_HITM] = "Local HITMs",
	[DISPLAY_RMT_HITM] = "Remote HITMs",
	[DISPLAY_TOT_HITM] = "Total HITMs",
	[DISPLAY_SNP_PEER] = "Peer Snoop",
};

static const struct option c2c_options[] = {
	OPT_INCR('v', "verbose", &verbose, "be more verbose (show counter open errors, etc)"),
	OPT_END()
};

static struct perf_c2c c2c;

static void *c2c_he_zalloc(size_t size)
{
	struct c2c_hist_entry *c2c_he;

	c2c_he = zalloc(size + sizeof(*c2c_he));
	if (!c2c_he)
		return NULL;

	c2c_he->cpuset = bitmap_zalloc(c2c.cpus_cnt);
	if (!c2c_he->cpuset)
		goto out_free;

	c2c_he->nodeset = bitmap_zalloc(c2c.nodes_cnt);
	if (!c2c_he->nodeset)
		goto out_free;

	c2c_he->node_stats = zalloc(c2c.nodes_cnt * sizeof(*c2c_he->node_stats));
	if (!c2c_he->node_stats)
		goto out_free;

	init_stats(&c2c_he->cstats.lcl_hitm);
	init_stats(&c2c_he->cstats.rmt_hitm);
	init_stats(&c2c_he->cstats.lcl_peer);
	init_stats(&c2c_he->cstats.rmt_peer);
	init_stats(&c2c_he->cstats.load);

	return &c2c_he->he;

out_free:
	zfree(&c2c_he->nodeset);
	zfree(&c2c_he->cpuset);
	free(c2c_he);
	return NULL;
}

static void c2c_he_free(void *he)
{
	struct c2c_hist_entry *c2c_he;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	if (c2c_he->hists) {
		hists__delete_entries(&c2c_he->hists->hists);
		zfree(&c2c_he->hists);
	}

	zfree(&c2c_he->cpuset);
	zfree(&c2c_he->nodeset);
	zfree(&c2c_he->nodestr);
	zfree(&c2c_he->node_stats);
	free(c2c_he);
}

static struct hist_entry_ops c2c_entry_ops = {
	.new	= c2c_he_zalloc,
	.free	= c2c_he_free,
};

static int c2c_hists__init(struct c2c_hists *hists,
			   const char *sort,
			   int nr_header_lines);

static struct c2c_hists*
he__get_c2c_hists(struct hist_entry *he,
		  const char *sort,
		  int nr_header_lines)
{
	struct c2c_hist_entry *c2c_he;
	struct c2c_hists *hists;
	int ret;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	if (c2c_he->hists)
		return c2c_he->hists;

	hists = c2c_he->hists = zalloc(sizeof(*hists));
	if (!hists)
		return NULL;

	ret = c2c_hists__init(hists, sort, nr_header_lines);
	if (ret) {
		free(hists);
		return NULL;
	}

	return hists;
}

static void c2c_he__set_cpu(struct c2c_hist_entry *c2c_he,
			    struct perf_sample *sample)
{
	if (WARN_ONCE(sample->cpu == (unsigned int) -1,
		      "WARNING: no sample cpu value"))
		return;

	__set_bit(sample->cpu, c2c_he->cpuset);
}

static void c2c_he__set_node(struct c2c_hist_entry *c2c_he,
			     struct perf_sample *sample)
{
	int node;

	if (!sample->phys_addr) {
		c2c_he->paddr_zero = true;
		return;
	}

	node = mem2node__node(&c2c.mem2node, sample->phys_addr);
	if (WARN_ONCE(node < 0, "WARNING: failed to find node\n"))
		return;

	__set_bit(node, c2c_he->nodeset);

	if (c2c_he->paddr != sample->phys_addr) {
		c2c_he->paddr_cnt++;
		c2c_he->paddr = sample->phys_addr;
	}
}

static void compute_stats(struct c2c_hist_entry *c2c_he,
			  struct c2c_stats *stats,
			  u64 weight)
{
	struct compute_stats *cstats = &c2c_he->cstats;

	if (stats->rmt_hitm)
		update_stats(&cstats->rmt_hitm, weight);
	else if (stats->lcl_hitm)
		update_stats(&cstats->lcl_hitm, weight);
	else if (stats->rmt_peer)
		update_stats(&cstats->rmt_peer, weight);
	else if (stats->lcl_peer)
		update_stats(&cstats->lcl_peer, weight);
	else if (stats->load)
		update_stats(&cstats->load, weight);
}

static int process_sample_event(struct perf_tool *tool __maybe_unused,
				union perf_event *event,
				struct perf_sample *sample,
				struct evsel *evsel,
				struct machine *machine)
{
	struct c2c_hists *c2c_hists = &c2c.hists;
	struct c2c_hist_entry *c2c_he;
	struct c2c_stats stats = { .nr_entries = 0, };
	struct hist_entry *he;
	struct addr_location al;
	struct mem_info *mi, *mi_dup;
	struct callchain_cursor *cursor;
	int ret;

	addr_location__init(&al);
	if (machine__resolve(machine, &al, sample) < 0) {
		pr_debug("problem processing %d event, skipping it.\n",
			 event->header.type);
		ret = -1;
		goto out;
	}

	if (c2c.stitch_lbr)
		thread__set_lbr_stitch_enable(al.thread, true);

	cursor = get_tls_callchain_cursor();
	ret = sample__resolve_callchain(sample, cursor, NULL,
					evsel, &al, sysctl_perf_event_max_stack);
	if (ret)
		goto out;

	mi = sample__resolve_mem(sample, &al);
	if (mi == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * The mi object is released in hists__add_entry_ops,
	 * if it gets sorted out into existing data, so we need
	 * to take the copy now.
	 */
	mi_dup = mem_info__get(mi);

	c2c_decode_stats(&stats, mi);

	he = hists__add_entry_ops(&c2c_hists->hists, &c2c_entry_ops,
				  &al, NULL, NULL, mi, NULL,
				  sample, true);
	if (he == NULL)
		goto free_mi;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	c2c_add_stats(&c2c_he->stats, &stats);
	c2c_add_stats(&c2c_hists->stats, &stats);

	c2c_he__set_cpu(c2c_he, sample);
	c2c_he__set_node(c2c_he, sample);

	hists__inc_nr_samples(&c2c_hists->hists, he->filtered);
	ret = hist_entry__append_callchain(he, sample);

	if (!ret) {
		/*
		 * There's already been warning about missing
		 * sample's cpu value. Let's account all to
		 * node 0 in this case, without any further
		 * warning.
		 *
		 * Doing node stats only for single callchain data.
		 */
		int cpu = sample->cpu == (unsigned int) -1 ? 0 : sample->cpu;
		int node = c2c.cpu2node[cpu];

		mi = mi_dup;

		c2c_hists = he__get_c2c_hists(he, c2c.cl_sort, 2);
		if (!c2c_hists)
			goto free_mi;

		he = hists__add_entry_ops(&c2c_hists->hists, &c2c_entry_ops,
					  &al, NULL, NULL, mi, NULL,
					  sample, true);
		if (he == NULL)
			goto free_mi;

		c2c_he = container_of(he, struct c2c_hist_entry, he);
		c2c_add_stats(&c2c_he->stats, &stats);
		c2c_add_stats(&c2c_hists->stats, &stats);
		c2c_add_stats(&c2c_he->node_stats[node], &stats);

		compute_stats(c2c_he, &stats, sample->weight);

		c2c_he__set_cpu(c2c_he, sample);
		c2c_he__set_node(c2c_he, sample);

		hists__inc_nr_samples(&c2c_hists->hists, he->filtered);
		ret = hist_entry__append_callchain(he, sample);
	}

out:
	addr_location__exit(&al);
	return ret;

free_mi:
	mem_info__put(mi_dup);
	mem_info__put(mi);
	ret = -ENOMEM;
	goto out;
}

static struct perf_c2c c2c = {
	.tool = {
		.sample		= process_sample_event,
		.mmap		= perf_event__process_mmap,
		.mmap2		= perf_event__process_mmap2,
		.comm		= perf_event__process_comm,
		.exit		= perf_event__process_exit,
		.fork		= perf_event__process_fork,
		.lost		= perf_event__process_lost,
		.attr		= perf_event__process_attr,
		.auxtrace_info  = perf_event__process_auxtrace_info,
		.auxtrace       = perf_event__process_auxtrace,
		.auxtrace_error = perf_event__process_auxtrace_error,
		.ordered_events	= true,
		.ordering_requires_timestamps = true,
	},
};

static const char * const c2c_usage[] = {
	"perf c2c {record|report}",
	NULL
};

static const char * const __usage_report[] = {
	"perf c2c report",
	NULL
};

static const char * const *report_c2c_usage = __usage_report;

#define C2C_HEADER_MAX 2

struct c2c_header {
	struct {
		const char *text;
		int	    span;
	} line[C2C_HEADER_MAX];
};

struct c2c_dimension {
	struct c2c_header	 header;
	const char		*name;
	int			 width;
	struct sort_entry	*se;

	int64_t (*cmp)(struct perf_hpp_fmt *fmt,
		       struct hist_entry *, struct hist_entry *);
	int   (*entry)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he);
	int   (*color)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he);
};

struct c2c_fmt {
	struct perf_hpp_fmt	 fmt;
	struct c2c_dimension	*dim;
};

#define SYMBOL_WIDTH 30

static struct c2c_dimension dim_symbol;
static struct c2c_dimension dim_srcline;

static int symbol_width(struct hists *hists, struct sort_entry *se)
{
	int width = hists__col_len(hists, se->se_width_idx);

	if (!c2c.symbol_full)
		width = MIN(width, SYMBOL_WIDTH);

	return width;
}

static int c2c_width(struct perf_hpp_fmt *fmt,
		     struct perf_hpp *hpp __maybe_unused,
		     struct hists *hists)
{
	struct c2c_fmt *c2c_fmt;
	struct c2c_dimension *dim;

	c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	dim = c2c_fmt->dim;

	if (dim == &dim_symbol || dim == &dim_srcline)
		return symbol_width(hists, dim->se);

	return dim->se ? hists__col_len(hists, dim->se->se_width_idx) :
			 c2c_fmt->dim->width;
}

static int c2c_header(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		      struct hists *hists, int line, int *span)
{
	struct perf_hpp_list *hpp_list = hists->hpp_list;
	struct c2c_fmt *c2c_fmt;
	struct c2c_dimension *dim;
	const char *text = NULL;
	int width = c2c_width(fmt, hpp, hists);

	c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	dim = c2c_fmt->dim;

	if (dim->se) {
		text = dim->header.line[line].text;
		/* Use the last line from sort_entry if not defined. */
		if (!text && (line == hpp_list->nr_header_lines - 1))
			text = dim->se->se_header;
	} else {
		text = dim->header.line[line].text;

		if (*span) {
			(*span)--;
			return 0;
		} else {
			*span = dim->header.line[line].span;
		}
	}

	if (text == NULL)
		text = "";

	return scnprintf(hpp->buf, hpp->size, "%*s", width, text);
}

#define HEX_STR(__s, __v)				\
({							\
	scnprintf(__s, sizeof(__s), "0x%" PRIx64, __v);	\
	__s;						\
})

static int64_t
dcacheline_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	       struct hist_entry *left, struct hist_entry *right)
{
	return sort__dcacheline_cmp(left, right);
}

static int dcacheline_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			    struct hist_entry *he)
{
	uint64_t addr = 0;
	int width = c2c_width(fmt, hpp, he->hists);
	char buf[20];

	if (he->mem_info)
		addr = cl_address(mem_info__daddr(he->mem_info)->addr, chk_double_cl);

	return scnprintf(hpp->buf, hpp->size, "%*s", width, HEX_STR(buf, addr));
}

static int
dcacheline_node_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		      struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	if (WARN_ON_ONCE(!c2c_he->nodestr))
		return 0;

	return scnprintf(hpp->buf, hpp->size, "%*s", width, c2c_he->nodestr);
}

static int
dcacheline_node_count(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		      struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	return scnprintf(hpp->buf, hpp->size, "%*lu", width, c2c_he->paddr_cnt);
}

static int offset_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			struct hist_entry *he)
{
	uint64_t addr = 0;
	int width = c2c_width(fmt, hpp, he->hists);
	char buf[20];

	if (he->mem_info)
		addr = cl_offset(mem_info__daddr(he->mem_info)->al_addr, chk_double_cl);

	return scnprintf(hpp->buf, hpp->size, "%*s", width, HEX_STR(buf, addr));
}

static int64_t
offset_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	   struct hist_entry *left, struct hist_entry *right)
{
	uint64_t l = 0, r = 0;

	if (left->mem_info)
		l = cl_offset(mem_info__daddr(left->mem_info)->addr, chk_double_cl);

	if (right->mem_info)
		r = cl_offset(mem_info__daddr(right->mem_info)->addr, chk_double_cl);

	return (int64_t)(r - l);
}

static int
iaddr_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	    struct hist_entry *he)
{
	uint64_t addr = 0;
	int width = c2c_width(fmt, hpp, he->hists);
	char buf[20];

	if (he->mem_info)
		addr = mem_info__iaddr(he->mem_info)->addr;

	return scnprintf(hpp->buf, hpp->size, "%*s", width, HEX_STR(buf, addr));
}

static int64_t
iaddr_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	  struct hist_entry *left, struct hist_entry *right)
{
	return sort__iaddr_cmp(left, right);
}

static int
tot_hitm_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	unsigned int tot_hitm;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	tot_hitm = c2c_he->stats.lcl_hitm + c2c_he->stats.rmt_hitm;

	return scnprintf(hpp->buf, hpp->size, "%*u", width, tot_hitm);
}

static int64_t
tot_hitm_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;
	uint64_t tot_hitm_left;
	uint64_t tot_hitm_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	tot_hitm_left  = c2c_left->stats.lcl_hitm + c2c_left->stats.rmt_hitm;
	tot_hitm_right = c2c_right->stats.lcl_hitm + c2c_right->stats.rmt_hitm;

	return tot_hitm_left - tot_hitm_right;
}

#define STAT_FN_ENTRY(__f)					\
static int							\
__f ## _entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,	\
	      struct hist_entry *he)				\
{								\
	struct c2c_hist_entry *c2c_he;				\
	int width = c2c_width(fmt, hpp, he->hists);		\
								\
	c2c_he = container_of(he, struct c2c_hist_entry, he);	\
	return scnprintf(hpp->buf, hpp->size, "%*u", width,	\
			 c2c_he->stats.__f);			\
}

#define STAT_FN_CMP(__f)						\
static int64_t								\
__f ## _cmp(struct perf_hpp_fmt *fmt __maybe_unused,			\
	    struct hist_entry *left, struct hist_entry *right)		\
{									\
	struct c2c_hist_entry *c2c_left, *c2c_right;			\
									\
	c2c_left  = container_of(left, struct c2c_hist_entry, he);	\
	c2c_right = container_of(right, struct c2c_hist_entry, he);	\
	return (uint64_t) c2c_left->stats.__f -				\
	       (uint64_t) c2c_right->stats.__f;				\
}

#define STAT_FN(__f)		\
	STAT_FN_ENTRY(__f)	\
	STAT_FN_CMP(__f)

STAT_FN(rmt_hitm)
STAT_FN(lcl_hitm)
STAT_FN(rmt_peer)
STAT_FN(lcl_peer)
STAT_FN(tot_peer)
STAT_FN(store)
STAT_FN(st_l1hit)
STAT_FN(st_l1miss)
STAT_FN(st_na)
STAT_FN(ld_fbhit)
STAT_FN(ld_l1hit)
STAT_FN(ld_l2hit)
STAT_FN(ld_llchit)
STAT_FN(rmt_hit)

static uint64_t get_load_llc_misses(struct c2c_stats *stats)
{
	return stats->lcl_dram +
	       stats->rmt_dram +
	       stats->rmt_hitm +
	       stats->rmt_hit;
}

static uint64_t get_load_cache_hits(struct c2c_stats *stats)
{
	return stats->ld_fbhit +
	       stats->ld_l1hit +
	       stats->ld_l2hit +
	       stats->ld_llchit +
	       stats->lcl_hitm;
}

static uint64_t get_stores(struct c2c_stats *stats)
{
	return stats->st_l1hit +
	       stats->st_l1miss +
	       stats->st_na;
}

static uint64_t total_records(struct c2c_stats *stats)
{
	return get_load_llc_misses(stats) +
	       get_load_cache_hits(stats) +
	       get_stores(stats);
}

static int
tot_recs_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	uint64_t tot_recs;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	tot_recs = total_records(&c2c_he->stats);

	return scnprintf(hpp->buf, hpp->size, "%*" PRIu64, width, tot_recs);
}

static int64_t
tot_recs_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;
	uint64_t tot_recs_left;
	uint64_t tot_recs_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	tot_recs_left  = total_records(&c2c_left->stats);
	tot_recs_right = total_records(&c2c_right->stats);

	return tot_recs_left - tot_recs_right;
}

static uint64_t total_loads(struct c2c_stats *stats)
{
	return get_load_llc_misses(stats) +
	       get_load_cache_hits(stats);
}

static int
tot_loads_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	uint64_t tot_recs;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	tot_recs = total_loads(&c2c_he->stats);

	return scnprintf(hpp->buf, hpp->size, "%*" PRIu64, width, tot_recs);
}

static int64_t
tot_loads_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	      struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;
	uint64_t tot_recs_left;
	uint64_t tot_recs_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	tot_recs_left  = total_loads(&c2c_left->stats);
	tot_recs_right = total_loads(&c2c_right->stats);

	return tot_recs_left - tot_recs_right;
}

typedef double (get_percent_cb)(struct c2c_hist_entry *);

static int
percent_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	      struct hist_entry *he, get_percent_cb get_percent)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	double per;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	per = get_percent(c2c_he);

#ifdef HAVE_SLANG_SUPPORT
	if (use_browser)
		return __hpp__slsmg_color_printf(hpp, "%*.2f%%", width - 1, per);
#endif
	return hpp_color_scnprintf(hpp, "%*.2f%%", width - 1, per);
}

static double percent_costly_snoop(struct c2c_hist_entry *c2c_he)
{
	struct c2c_hists *hists;
	struct c2c_stats *stats;
	struct c2c_stats *total;
	int tot = 0, st = 0;
	double p;

	hists = container_of(c2c_he->he.hists, struct c2c_hists, hists);
	stats = &c2c_he->stats;
	total = &hists->stats;

	switch (c2c.display) {
	case DISPLAY_RMT_HITM:
		st  = stats->rmt_hitm;
		tot = total->rmt_hitm;
		break;
	case DISPLAY_LCL_HITM:
		st  = stats->lcl_hitm;
		tot = total->lcl_hitm;
		break;
	case DISPLAY_TOT_HITM:
		st  = stats->tot_hitm;
		tot = total->tot_hitm;
		break;
	case DISPLAY_SNP_PEER:
		st  = stats->tot_peer;
		tot = total->tot_peer;
		break;
	default:
		break;
	}

	p = tot ? (double) st / tot : 0;

	return 100 * p;
}

#define PERC_STR(__s, __v)				\
({							\
	scnprintf(__s, sizeof(__s), "%.2F%%", __v);	\
	__s;						\
})

static int
percent_costly_snoop_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			   struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	char buf[10];
	double per;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	per = percent_costly_snoop(c2c_he);
	return scnprintf(hpp->buf, hpp->size, "%*s", width, PERC_STR(buf, per));
}

static int
percent_costly_snoop_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			   struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_costly_snoop);
}

static int64_t
percent_costly_snoop_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
			 struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;
	double per_left;
	double per_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	per_left  = percent_costly_snoop(c2c_left);
	per_right = percent_costly_snoop(c2c_right);

	return per_left - per_right;
}

static struct c2c_stats *he_stats(struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	return &c2c_he->stats;
}

static struct c2c_stats *total_stats(struct hist_entry *he)
{
	struct c2c_hists *hists;

	hists = container_of(he->hists, struct c2c_hists, hists);
	return &hists->stats;
}

static double percent(u32 st, u32 tot)
{
	return tot ? 100. * (double) st / (double) tot : 0;
}

#define PERCENT(__h, __f) percent(he_stats(__h)->__f, total_stats(__h)->__f)

#define PERCENT_FN(__f)								\
static double percent_ ## __f(struct c2c_hist_entry *c2c_he)			\
{										\
	struct c2c_hists *hists;						\
										\
	hists = container_of(c2c_he->he.hists, struct c2c_hists, hists);	\
	return percent(c2c_he->stats.__f, hists->stats.__f);			\
}

PERCENT_FN(rmt_hitm)
PERCENT_FN(lcl_hitm)
PERCENT_FN(rmt_peer)
PERCENT_FN(lcl_peer)
PERCENT_FN(st_l1hit)
PERCENT_FN(st_l1miss)
PERCENT_FN(st_na)

static int
percent_rmt_hitm_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	double per = PERCENT(he, rmt_hitm);
	char buf[10];

	return scnprintf(hpp->buf, hpp->size, "%*s", width, PERC_STR(buf, per));
}

static int
percent_rmt_hitm_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_rmt_hitm);
}

static int64_t
percent_rmt_hitm_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		     struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = PERCENT(left, rmt_hitm);
	per_right = PERCENT(right, rmt_hitm);

	return per_left - per_right;
}

static int
percent_lcl_hitm_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	double per = PERCENT(he, lcl_hitm);
	char buf[10];

	return scnprintf(hpp->buf, hpp->size, "%*s", width, PERC_STR(buf, per));
}

static int
percent_lcl_hitm_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_lcl_hitm);
}

static int64_t
percent_lcl_hitm_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		     struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = PERCENT(left, lcl_hitm);
	per_right = PERCENT(right, lcl_hitm);

	return per_left - per_right;
}

static int
percent_lcl_peer_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	double per = PERCENT(he, lcl_peer);
	char buf[10];

	return scnprintf(hpp->buf, hpp->size, "%*s", width, PERC_STR(buf, per));
}

static int
percent_lcl_peer_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_lcl_peer);
}

static int64_t
percent_lcl_peer_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		     struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = PERCENT(left, lcl_peer);
	per_right = PERCENT(right, lcl_peer);

	return per_left - per_right;
}

static int
percent_rmt_peer_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	double per = PERCENT(he, rmt_peer);
	char buf[10];

	return scnprintf(hpp->buf, hpp->size, "%*s", width, PERC_STR(buf, per));
}

static int
percent_rmt_peer_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_rmt_peer);
}

static int64_t
percent_rmt_peer_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		     struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = PERCENT(left, rmt_peer);
	per_right = PERCENT(right, rmt_peer);

	return per_left - per_right;
}

static int
percent_stores_l1hit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			   struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	double per = PERCENT(he, st_l1hit);
	char buf[10];

	return scnprintf(hpp->buf, hpp->size, "%*s", width, PERC_STR(buf, per));
}

static int
percent_stores_l1hit_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			   struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_st_l1hit);
}

static int64_t
percent_stores_l1hit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
			struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = PERCENT(left, st_l1hit);
	per_right = PERCENT(right, st_l1hit);

	return per_left - per_right;
}

static int
percent_stores_l1miss_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			   struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	double per = PERCENT(he, st_l1miss);
	char buf[10];

	return scnprintf(hpp->buf, hpp->size, "%*s", width, PERC_STR(buf, per));
}

static int
percent_stores_l1miss_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			    struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_st_l1miss);
}

static int64_t
percent_stores_l1miss_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
			  struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = PERCENT(left, st_l1miss);
	per_right = PERCENT(right, st_l1miss);

	return per_left - per_right;
}

static int
percent_stores_na_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	double per = PERCENT(he, st_na);
	char buf[10];

	return scnprintf(hpp->buf, hpp->size, "%*s", width, PERC_STR(buf, per));
}

static int
percent_stores_na_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_st_na);
}

static int64_t
percent_stores_na_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		      struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = PERCENT(left, st_na);
	per_right = PERCENT(right, st_na);

	return per_left - per_right;
}

STAT_FN(lcl_dram)
STAT_FN(rmt_dram)

static int
pid_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	  struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);

	return scnprintf(hpp->buf, hpp->size, "%*d", width, thread__pid(he->thread));
}

static int64_t
pid_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	struct hist_entry *left, struct hist_entry *right)
{
	return thread__pid(left->thread) - thread__pid(right->thread);
}

static int64_t
empty_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	  struct hist_entry *left __maybe_unused,
	  struct hist_entry *right __maybe_unused)
{
	return 0;
}

static int display_metrics(struct perf_hpp *hpp, u32 val, u32 sum)
{
	int ret;

	if (sum != 0)
		ret = scnprintf(hpp->buf, hpp->size, "%5.1f%% ",
				percent(val, sum));
	else
		ret = scnprintf(hpp->buf, hpp->size, "%6s ", "n/a");

	return ret;
}

static int
node_entry(struct perf_hpp_fmt *fmt __maybe_unused, struct perf_hpp *hpp,
	   struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	bool first = true;
	int node;
	int ret = 0;

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	for (node = 0; node < c2c.nodes_cnt; node++) {
		DECLARE_BITMAP(set, c2c.cpus_cnt);

		bitmap_zero(set, c2c.cpus_cnt);
		bitmap_and(set, c2c_he->cpuset, c2c.nodes[node], c2c.cpus_cnt);

		if (bitmap_empty(set, c2c.cpus_cnt)) {
			if (c2c.node_info == 1) {
				ret = scnprintf(hpp->buf, hpp->size, "%21s", " ");
				advance_hpp(hpp, ret);
			}
			continue;
		}

		if (!first) {
			ret = scnprintf(hpp->buf, hpp->size, " ");
			advance_hpp(hpp, ret);
		}

		switch (c2c.node_info) {
		case 0:
			ret = scnprintf(hpp->buf, hpp->size, "%2d", node);
			advance_hpp(hpp, ret);
			break;
		case 1:
		{
			int num = bitmap_weight(set, c2c.cpus_cnt);
			struct c2c_stats *stats = &c2c_he->node_stats[node];

			ret = scnprintf(hpp->buf, hpp->size, "%2d{%2d ", node, num);
			advance_hpp(hpp, ret);

			switch (c2c.display) {
			case DISPLAY_RMT_HITM:
				ret = display_metrics(hpp, stats->rmt_hitm,
						      c2c_he->stats.rmt_hitm);
				break;
			case DISPLAY_LCL_HITM:
				ret = display_metrics(hpp, stats->lcl_hitm,
						      c2c_he->stats.lcl_hitm);
				break;
			case DISPLAY_TOT_HITM:
				ret = display_metrics(hpp, stats->tot_hitm,
						      c2c_he->stats.tot_hitm);
				break;
			case DISPLAY_SNP_PEER:
				ret = display_metrics(hpp, stats->tot_peer,
						      c2c_he->stats.tot_peer);
				break;
			default:
				break;
			}

			advance_hpp(hpp, ret);

			if (c2c_he->stats.store > 0) {
				ret = scnprintf(hpp->buf, hpp->size, "%5.1f%%}",
						percent(stats->store, c2c_he->stats.store));
			} else {
				ret = scnprintf(hpp->buf, hpp->size, "%6s}", "n/a");
			}

			advance_hpp(hpp, ret);
			break;
		}
		case 2:
			ret = scnprintf(hpp->buf, hpp->size, "%2d{", node);
			advance_hpp(hpp, ret);

			ret = bitmap_scnprintf(set, c2c.cpus_cnt, hpp->buf, hpp->size);
			advance_hpp(hpp, ret);

			ret = scnprintf(hpp->buf, hpp->size, "}");
			advance_hpp(hpp, ret);
			break;
		default:
			break;
		}

		first = false;
	}

	return 0;
}

static int
mean_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	   struct hist_entry *he, double mean)
{
	int width = c2c_width(fmt, hpp, he->hists);
	char buf[10];

	scnprintf(buf, 10, "%6.0f", mean);
	return scnprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

#define MEAN_ENTRY(__func, __val)						\
static int									\
__func(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp, struct hist_entry *he)	\
{										\
	struct c2c_hist_entry *c2c_he;						\
	c2c_he = container_of(he, struct c2c_hist_entry, he);			\
	return mean_entry(fmt, hpp, he, avg_stats(&c2c_he->cstats.__val));	\
}

MEAN_ENTRY(mean_rmt_entry,  rmt_hitm);
MEAN_ENTRY(mean_lcl_entry,  lcl_hitm);
MEAN_ENTRY(mean_load_entry, load);
MEAN_ENTRY(mean_rmt_peer_entry, rmt_peer);
MEAN_ENTRY(mean_lcl_peer_entry, lcl_peer);

static int
cpucnt_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	     struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	char buf[10];

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	scnprintf(buf, 10, "%d", bitmap_weight(c2c_he->cpuset, c2c.cpus_cnt));
	return scnprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int
cl_idx_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	     struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	char buf[10];

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	scnprintf(buf, 10, "%u", c2c_he->cacheline_idx);
	return scnprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int
cl_idx_empty_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		   struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);

	return scnprintf(hpp->buf, hpp->size, "%*s", width, "");
}

#define HEADER_LOW(__h)			\
	{				\
		.line[1] = {		\
			.text = __h,	\
		},			\
	}

#define HEADER_BOTH(__h0, __h1)		\
	{				\
		.line[0] = {		\
			.text = __h0,	\
		},			\
		.line[1] = {		\
			.text = __h1,	\
		},			\
	}

#define HEADER_SPAN(__h0, __h1, __s)	\
	{				\
		.line[0] = {		\
			.text = __h0,	\
			.span = __s,	\
		},			\
		.line[1] = {		\
			.text = __h1,	\
		},			\
	}

#define HEADER_SPAN_LOW(__h)		\
	{				\
		.line[1] = {		\
			.text = __h,	\
		},			\
	}

static struct c2c_dimension dim_dcacheline = {
	.header		= HEADER_SPAN("--- Cacheline ----", "Address", 2),
	.name		= "dcacheline",
	.cmp		= dcacheline_cmp,
	.entry		= dcacheline_entry,
	.width		= 18,
};

static struct c2c_dimension dim_dcacheline_node = {
	.header		= HEADER_LOW("Node"),
	.name		= "dcacheline_node",
	.cmp		= empty_cmp,
	.entry		= dcacheline_node_entry,
	.width		= 4,
};

static struct c2c_dimension dim_dcacheline_count = {
	.header		= HEADER_LOW("PA cnt"),
	.name		= "dcacheline_count",
	.cmp		= empty_cmp,
	.entry		= dcacheline_node_count,
	.width		= 6,
};

static struct c2c_header header_offset_tui = HEADER_SPAN("-----", "Off", 2);

static struct c2c_dimension dim_offset = {
	.header		= HEADER_SPAN("--- Data address -", "Offset", 2),
	.name		= "offset",
	.cmp		= offset_cmp,
	.entry		= offset_entry,
	.width		= 18,
};

static struct c2c_dimension dim_offset_node = {
	.header		= HEADER_LOW("Node"),
	.name		= "offset_node",
	.cmp		= empty_cmp,
	.entry		= dcacheline_node_entry,
	.width		= 4,
};

static struct c2c_dimension dim_iaddr = {
	.header		= HEADER_LOW("Code address"),
	.name		= "iaddr",
	.cmp		= iaddr_cmp,
	.entry		= iaddr_entry,
	.width		= 18,
};

static struct c2c_dimension dim_tot_hitm = {
	.header		= HEADER_SPAN("------- Load Hitm -------", "Total", 2),
	.name		= "tot_hitm",
	.cmp		= tot_hitm_cmp,
	.entry		= tot_hitm_entry,
	.width		= 7,
};

static struct c2c_dimension dim_lcl_hitm = {
	.header		= HEADER_SPAN_LOW("LclHitm"),
	.name		= "lcl_hitm",
	.cmp		= lcl_hitm_cmp,
	.entry		= lcl_hitm_entry,
	.width		= 7,
};

static struct c2c_dimension dim_rmt_hitm = {
	.header		= HEADER_SPAN_LOW("RmtHitm"),
	.name		= "rmt_hitm",
	.cmp		= rmt_hitm_cmp,
	.entry		= rmt_hitm_entry,
	.width		= 7,
};

static struct c2c_dimension dim_tot_peer = {
	.header		= HEADER_SPAN("------- Load Peer -------", "Total", 2),
	.name		= "tot_peer",
	.cmp		= tot_peer_cmp,
	.entry		= tot_peer_entry,
	.width		= 7,
};

static struct c2c_dimension dim_lcl_peer = {
	.header		= HEADER_SPAN_LOW("Local"),
	.name		= "lcl_peer",
	.cmp		= lcl_peer_cmp,
	.entry		= lcl_peer_entry,
	.width		= 7,
};

static struct c2c_dimension dim_rmt_peer = {
	.header		= HEADER_SPAN_LOW("Remote"),
	.name		= "rmt_peer",
	.cmp		= rmt_peer_cmp,
	.entry		= rmt_peer_entry,
	.width		= 7,
};

static struct c2c_dimension dim_cl_rmt_hitm = {
	.header		= HEADER_SPAN("----- HITM -----", "Rmt", 1),
	.name		= "cl_rmt_hitm",
	.cmp		= rmt_hitm_cmp,
	.entry		= rmt_hitm_entry,
	.width		= 7,
};

static struct c2c_dimension dim_cl_lcl_hitm = {
	.header		= HEADER_SPAN_LOW("Lcl"),
	.name		= "cl_lcl_hitm",
	.cmp		= lcl_hitm_cmp,
	.entry		= lcl_hitm_entry,
	.width		= 7,
};

static struct c2c_dimension dim_cl_rmt_peer = {
	.header		= HEADER_SPAN("----- Peer -----", "Rmt", 1),
	.name		= "cl_rmt_peer",
	.cmp		= rmt_peer_cmp,
	.entry		= rmt_peer_entry,
	.width		= 7,
};

static struct c2c_dimension dim_cl_lcl_peer = {
	.header		= HEADER_SPAN_LOW("Lcl"),
	.name		= "cl_lcl_peer",
	.cmp		= lcl_peer_cmp,
	.entry		= lcl_peer_entry,
	.width		= 7,
};

static struct c2c_dimension dim_tot_stores = {
	.header		= HEADER_BOTH("Total", "Stores"),
	.name		= "tot_stores",
	.cmp		= store_cmp,
	.entry		= store_entry,
	.width		= 7,
};

static struct c2c_dimension dim_stores_l1hit = {
	.header		= HEADER_SPAN("--------- Stores --------", "L1Hit", 2),
	.name		= "stores_l1hit",
	.cmp		= st_l1hit_cmp,
	.entry		= st_l1hit_entry,
	.width		= 7,
};

static struct c2c_dimension dim_stores_l1miss = {
	.header		= HEADER_SPAN_LOW("L1Miss"),
	.name		= "stores_l1miss",
	.cmp		= st_l1miss_cmp,
	.entry		= st_l1miss_entry,
	.width		= 7,
};

static struct c2c_dimension dim_stores_na = {
	.header		= HEADER_SPAN_LOW("N/A"),
	.name		= "stores_na",
	.cmp		= st_na_cmp,
	.entry		= st_na_entry,
	.width		= 7,
};

static struct c2c_dimension dim_cl_stores_l1hit = {
	.header		= HEADER_SPAN("------- Store Refs ------", "L1 Hit", 2),
	.name		= "cl_stores_l1hit",
	.cmp		= st_l1hit_cmp,
	.entry		= st_l1hit_entry,
	.width		= 7,
};

static struct c2c_dimension dim_cl_stores_l1miss = {
	.header		= HEADER_SPAN_LOW("L1 Miss"),
	.name		= "cl_stores_l1miss",
	.cmp		= st_l1miss_cmp,
	.entry		= st_l1miss_entry,
	.width		= 7,
};

static struct c2c_dimension dim_cl_stores_na = {
	.header		= HEADER_SPAN_LOW("N/A"),
	.name		= "cl_stores_na",
	.cmp		= st_na_cmp,
	.entry		= st_na_entry,
	.width		= 7,
};

static struct c2c_dimension dim_ld_fbhit = {
	.header		= HEADER_SPAN("----- Core Load Hit -----", "FB", 2),
	.name		= "ld_fbhit",
	.cmp		= ld_fbhit_cmp,
	.entry		= ld_fbhit_entry,
	.width		= 7,
};

static struct c2c_dimension dim_ld_l1hit = {
	.header		= HEADER_SPAN_LOW("L1"),
	.name		= "ld_l1hit",
	.cmp		= ld_l1hit_cmp,
	.entry		= ld_l1hit_entry,
	.width		= 7,
};

static struct c2c_dimension dim_ld_l2hit = {
	.header		= HEADER_SPAN_LOW("L2"),
	.name		= "ld_l2hit",
	.cmp		= ld_l2hit_cmp,
	.entry		= ld_l2hit_entry,
	.width		= 7,
};

static struct c2c_dimension dim_ld_llchit = {
	.header		= HEADER_SPAN("- LLC Load Hit --", "LclHit", 1),
	.name		= "ld_lclhit",
	.cmp		= ld_llchit_cmp,
	.entry		= ld_llchit_entry,
	.width		= 8,
};

static struct c2c_dimension dim_ld_rmthit = {
	.header		= HEADER_SPAN("- RMT Load Hit --", "RmtHit", 1),
	.name		= "ld_rmthit",
	.cmp		= rmt_hit_cmp,
	.entry		= rmt_hit_entry,
	.width		= 8,
};

static struct c2c_dimension dim_tot_recs = {
	.header		= HEADER_BOTH("Total", "records"),
	.name		= "tot_recs",
	.cmp		= tot_recs_cmp,
	.entry		= tot_recs_entry,
	.width		= 7,
};

static struct c2c_dimension dim_tot_loads = {
	.header		= HEADER_BOTH("Total", "Loads"),
	.name		= "tot_loads",
	.cmp		= tot_loads_cmp,
	.entry		= tot_loads_entry,
	.width		= 7,
};

static struct c2c_header percent_costly_snoop_header[] = {
	[DISPLAY_LCL_HITM] = HEADER_BOTH("Lcl", "Hitm"),
	[DISPLAY_RMT_HITM] = HEADER_BOTH("Rmt", "Hitm"),
	[DISPLAY_TOT_HITM] = HEADER_BOTH("Tot", "Hitm"),
	[DISPLAY_SNP_PEER] = HEADER_BOTH("Peer", "Snoop"),
};

static struct c2c_dimension dim_percent_costly_snoop = {
	.name		= "percent_costly_snoop",
	.cmp		= percent_costly_snoop_cmp,
	.entry		= percent_costly_snoop_entry,
	.color		= percent_costly_snoop_color,
	.width		= 7,
};

static struct c2c_dimension dim_percent_rmt_hitm = {
	.header		= HEADER_SPAN("----- HITM -----", "RmtHitm", 1),
	.name		= "percent_rmt_hitm",
	.cmp		= percent_rmt_hitm_cmp,
	.entry		= percent_rmt_hitm_entry,
	.color		= percent_rmt_hitm_color,
	.width		= 7,
};

static struct c2c_dimension dim_percent_lcl_hitm = {
	.header		= HEADER_SPAN_LOW("LclHitm"),
	.name		= "percent_lcl_hitm",
	.cmp		= percent_lcl_hitm_cmp,
	.entry		= percent_lcl_hitm_entry,
	.color		= percent_lcl_hitm_color,
	.width		= 7,
};

static struct c2c_dimension dim_percent_rmt_peer = {
	.header		= HEADER_SPAN("-- Peer Snoop --", "Rmt", 1),
	.name		= "percent_rmt_peer",
	.cmp		= percent_rmt_peer_cmp,
	.entry		= percent_rmt_peer_entry,
	.color		= percent_rmt_peer_color,
	.width		= 7,
};

static struct c2c_dimension dim_percent_lcl_peer = {
	.header		= HEADER_SPAN_LOW("Lcl"),
	.name		= "percent_lcl_peer",
	.cmp		= percent_lcl_peer_cmp,
	.entry		= percent_lcl_peer_entry,
	.color		= percent_lcl_peer_color,
	.width		= 7,
};

static struct c2c_dimension dim_percent_stores_l1hit = {
	.header		= HEADER_SPAN("------- Store Refs ------", "L1 Hit", 2),
	.name		= "percent_stores_l1hit",
	.cmp		= percent_stores_l1hit_cmp,
	.entry		= percent_stores_l1hit_entry,
	.color		= percent_stores_l1hit_color,
	.width		= 7,
};

static struct c2c_dimension dim_percent_stores_l1miss = {
	.header		= HEADER_SPAN_LOW("L1 Miss"),
	.name		= "percent_stores_l1miss",
	.cmp		= percent_stores_l1miss_cmp,
	.entry		= percent_stores_l1miss_entry,
	.color		= percent_stores_l1miss_color,
	.width		= 7,
};

static struct c2c_dimension dim_percent_stores_na = {
	.header		= HEADER_SPAN_LOW("N/A"),
	.name		= "percent_stores_na",
	.cmp		= percent_stores_na_cmp,
	.entry		= percent_stores_na_entry,
	.color		= percent_stores_na_color,
	.width		= 7,
};

static struct c2c_dimension dim_dram_lcl = {
	.header		= HEADER_SPAN("--- Load Dram ----", "Lcl", 1),
	.name		= "dram_lcl",
	.cmp		= lcl_dram_cmp,
	.entry		= lcl_dram_entry,
	.width		= 8,
};

static struct c2c_dimension dim_dram_rmt = {
	.header		= HEADER_SPAN_LOW("Rmt"),
	.name		= "dram_rmt",
	.cmp		= rmt_dram_cmp,
	.entry		= rmt_dram_entry,
	.width		= 8,
};

static struct c2c_dimension dim_pid = {
	.header		= HEADER_LOW("Pid"),
	.name		= "pid",
	.cmp		= pid_cmp,
	.entry		= pid_entry,
	.width		= 7,
};

static struct c2c_dimension dim_tid = {
	.header		= HEADER_LOW("Tid"),
	.name		= "tid",
	.se		= &sort_thread,
};

static struct c2c_dimension dim_symbol = {
	.name		= "symbol",
	.se		= &sort_sym,
};

static struct c2c_dimension dim_dso = {
	.header		= HEADER_BOTH("Shared", "Object"),
	.name		= "dso",
	.se		= &sort_dso,
};

static struct c2c_dimension dim_node = {
	.name		= "node",
	.cmp		= empty_cmp,
	.entry		= node_entry,
	.width		= 4,
};

static struct c2c_dimension dim_mean_rmt = {
	.header		= HEADER_SPAN("---------- cycles ----------", "rmt hitm", 2),
	.name		= "mean_rmt",
	.cmp		= empty_cmp,
	.entry		= mean_rmt_entry,
	.width		= 8,
};

static struct c2c_dimension dim_mean_lcl = {
	.header		= HEADER_SPAN_LOW("lcl hitm"),
	.name		= "mean_lcl",
	.cmp		= empty_cmp,
	.entry		= mean_lcl_entry,
	.width		= 8,
};

static struct c2c_dimension dim_mean_load = {
	.header		= HEADER_SPAN_LOW("load"),
	.name		= "mean_load",
	.cmp		= empty_cmp,
	.entry		= mean_load_entry,
	.width		= 8,
};

static struct c2c_dimension dim_mean_rmt_peer = {
	.header		= HEADER_SPAN("---------- cycles ----------", "rmt peer", 2),
	.name		= "mean_rmt_peer",
	.cmp		= empty_cmp,
	.entry		= mean_rmt_peer_entry,
	.width		= 8,
};

static struct c2c_dimension dim_mean_lcl_peer = {
	.header		= HEADER_SPAN_LOW("lcl peer"),
	.name		= "mean_lcl_peer",
	.cmp		= empty_cmp,
	.entry		= mean_lcl_peer_entry,
	.width		= 8,
};

static struct c2c_dimension dim_cpucnt = {
	.header		= HEADER_BOTH("cpu", "cnt"),
	.name		= "cpucnt",
	.cmp		= empty_cmp,
	.entry		= cpucnt_entry,
	.width		= 8,
};

static struct c2c_dimension dim_srcline = {
	.name		= "cl_srcline",
	.se		= &sort_srcline,
};

static struct c2c_dimension dim_dcacheline_idx = {
	.header		= HEADER_LOW("Index"),
	.name		= "cl_idx",
	.cmp		= empty_cmp,
	.entry		= cl_idx_entry,
	.width		= 5,
};

static struct c2c_dimension dim_dcacheline_num = {
	.header		= HEADER_LOW("Num"),
	.name		= "cl_num",
	.cmp		= empty_cmp,
	.entry		= cl_idx_entry,
	.width		= 5,
};

static struct c2c_dimension dim_dcacheline_num_empty = {
	.header		= HEADER_LOW("Num"),
	.name		= "cl_num_empty",
	.cmp		= empty_cmp,
	.entry		= cl_idx_empty_entry,
	.width		= 5,
};

static struct c2c_dimension *dimensions[] = {
	&dim_dcacheline,
	&dim_dcacheline_node,
	&dim_dcacheline_count,
	&dim_offset,
	&dim_offset_node,
	&dim_iaddr,
	&dim_tot_hitm,
	&dim_lcl_hitm,
	&dim_rmt_hitm,
	&dim_tot_peer,
	&dim_lcl_peer,
	&dim_rmt_peer,
	&dim_cl_lcl_hitm,
	&dim_cl_rmt_hitm,
	&dim_cl_lcl_peer,
	&dim_cl_rmt_peer,
	&dim_tot_stores,
	&dim_stores_l1hit,
	&dim_stores_l1miss,
	&dim_stores_na,
	&dim_cl_stores_l1hit,
	&dim_cl_stores_l1miss,
	&dim_cl_stores_na,
	&dim_ld_fbhit,
	&dim_ld_l1hit,
	&dim_ld_l2hit,
	&dim_ld_llchit,
	&dim_ld_rmthit,
	&dim_tot_recs,
	&dim_tot_loads,
	&dim_percent_costly_snoop,
	&dim_percent_rmt_hitm,
	&dim_percent_lcl_hitm,
	&dim_percent_rmt_peer,
	&dim_percent_lcl_peer,
	&dim_percent_stores_l1hit,
	&dim_percent_stores_l1miss,
	&dim_percent_stores_na,
	&dim_dram_lcl,
	&dim_dram_rmt,
	&dim_pid,
	&dim_tid,
	&dim_symbol,
	&dim_dso,
	&dim_node,
	&dim_mean_rmt,
	&dim_mean_lcl,
	&dim_mean_rmt_peer,
	&dim_mean_lcl_peer,
	&dim_mean_load,
	&dim_cpucnt,
	&dim_srcline,
	&dim_dcacheline_idx,
	&dim_dcacheline_num,
	&dim_dcacheline_num_empty,
	NULL,
};

static void fmt_free(struct perf_hpp_fmt *fmt)
{
	struct c2c_fmt *c2c_fmt;

	c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	free(c2c_fmt);
}

static bool fmt_equal(struct perf_hpp_fmt *a, struct perf_hpp_fmt *b)
{
	struct c2c_fmt *c2c_a = container_of(a, struct c2c_fmt, fmt);
	struct c2c_fmt *c2c_b = container_of(b, struct c2c_fmt, fmt);

	return c2c_a->dim == c2c_b->dim;
}

static struct c2c_dimension *get_dimension(const char *name)
{
	unsigned int i;

	for (i = 0; dimensions[i]; i++) {
		struct c2c_dimension *dim = dimensions[i];

		if (!strcmp(dim->name, name))
			return dim;
	}

	return NULL;
}

static int c2c_se_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			struct hist_entry *he)
{
	struct c2c_fmt *c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	struct c2c_dimension *dim = c2c_fmt->dim;
	size_t len = fmt->user_len;

	if (!len) {
		len = hists__col_len(he->hists, dim->se->se_width_idx);

		if (dim == &dim_symbol || dim == &dim_srcline)
			len = symbol_width(he->hists, dim->se);
	}

	return dim->se->se_snprintf(he, hpp->buf, hpp->size, len);
}

static int64_t c2c_se_cmp(struct perf_hpp_fmt *fmt,
			  struct hist_entry *a, struct hist_entry *b)
{
	struct c2c_fmt *c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	struct c2c_dimension *dim = c2c_fmt->dim;

	return dim->se->se_cmp(a, b);
}

static int64_t c2c_se_collapse(struct perf_hpp_fmt *fmt,
			       struct hist_entry *a, struct hist_entry *b)
{
	struct c2c_fmt *c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	struct c2c_dimension *dim = c2c_fmt->dim;
	int64_t (*collapse_fn)(struct hist_entry *, struct hist_entry *);

	collapse_fn = dim->se->se_collapse ?: dim->se->se_cmp;
	return collapse_fn(a, b);
}

static struct c2c_fmt *get_format(const char *name)
{
	struct c2c_dimension *dim = get_dimension(name);
	struct c2c_fmt *c2c_fmt;
	struct perf_hpp_fmt *fmt;

	if (!dim)
		return NULL;

	c2c_fmt = zalloc(sizeof(*c2c_fmt));
	if (!c2c_fmt)
		return NULL;

	c2c_fmt->dim = dim;

	fmt = &c2c_fmt->fmt;
	INIT_LIST_HEAD(&fmt->list);
	INIT_LIST_HEAD(&fmt->sort_list);

	fmt->cmp	= dim->se ? c2c_se_cmp   : dim->cmp;
	fmt->sort	= dim->se ? c2c_se_cmp   : dim->cmp;
	fmt->color	= dim->se ? NULL	 : dim->color;
	fmt->entry	= dim->se ? c2c_se_entry : dim->entry;
	fmt->header	= c2c_header;
	fmt->width	= c2c_width;
	fmt->collapse	= dim->se ? c2c_se_collapse : dim->cmp;
	fmt->equal	= fmt_equal;
	fmt->free	= fmt_free;

	return c2c_fmt;
}

static int c2c_hists__init_output(struct perf_hpp_list *hpp_list, char *name)
{
	struct c2c_fmt *c2c_fmt = get_format(name);

	if (!c2c_fmt) {
		reset_dimensions();
		return output_field_add(hpp_list, name);
	}

	perf_hpp_list__column_register(hpp_list, &c2c_fmt->fmt);
	return 0;
}

static int c2c_hists__init_sort(struct perf_hpp_list *hpp_list, char *name)
{
	struct c2c_fmt *c2c_fmt = get_format(name);
	struct c2c_dimension *dim;

	if (!c2c_fmt) {
		reset_dimensions();
		return sort_dimension__add(hpp_list, name, NULL, 0);
	}

	dim = c2c_fmt->dim;
	if (dim == &dim_dso)
		hpp_list->dso = 1;

	perf_hpp_list__register_sort_field(hpp_list, &c2c_fmt->fmt);
	return 0;
}

#define PARSE_LIST(_list, _fn)							\
	do {									\
		char *tmp, *tok;						\
		ret = 0;							\
										\
		if (!_list)							\
			break;							\
										\
		for (tok = strtok_r((char *)_list, ", ", &tmp);			\
				tok; tok = strtok_r(NULL, ", ", &tmp)) {	\
			ret = _fn(hpp_list, tok);				\
			if (ret == -EINVAL) {					\
				pr_err("Invalid --fields key: `%s'", tok);	\
				break;						\
			} else if (ret == -ESRCH) {				\
				pr_err("Unknown --fields key: `%s'", tok);	\
				break;						\
			}							\
		}								\
	} while (0)

static int hpp_list__parse(struct perf_hpp_list *hpp_list,
			   const char *output_,
			   const char *sort_)
{
	char *output = output_ ? strdup(output_) : NULL;
	char *sort   = sort_   ? strdup(sort_) : NULL;
	int ret;

	PARSE_LIST(output, c2c_hists__init_output);
	PARSE_LIST(sort,   c2c_hists__init_sort);

	/* copy sort keys to output fields */
	perf_hpp__setup_output_field(hpp_list);

	/*
	 * We don't need other sorting keys other than those
	 * we already specified. It also really slows down
	 * the processing a lot with big number of output
	 * fields, so switching this off for c2c.
	 */

#if 0
	/* and then copy output fields to sort keys */
	perf_hpp__append_sort_keys(&hists->list);
#endif

	free(output);
	free(sort);
	return ret;
}

static int c2c_hists__init(struct c2c_hists *hists,
			   const char *sort,
			   int nr_header_lines)
{
	__hists__init(&hists->hists, &hists->list);

	/*
	 * Initialize only with sort fields, we need to resort
	 * later anyway, and that's where we add output fields
	 * as well.
	 */
	perf_hpp_list__init(&hists->list);

	/* Overload number of header lines.*/
	hists->list.nr_header_lines = nr_header_lines;

	return hpp_list__parse(&hists->list, NULL, sort);
}

static int c2c_hists__reinit(struct c2c_hists *c2c_hists,
			     const char *output,
			     const char *sort)
{
	perf_hpp__reset_output_field(&c2c_hists->list);
	return hpp_list__parse(&c2c_hists->list, output, sort);
}

#define DISPLAY_LINE_LIMIT  0.001

static u8 filter_display(u32 val, u32 sum)
{
	if (sum == 0 || ((double)val / sum) < DISPLAY_LINE_LIMIT)
		return HIST_FILTER__C2C;

	return 0;
}

static bool he__display(struct hist_entry *he, struct c2c_stats *stats)
{
	struct c2c_hist_entry *c2c_he;

	if (c2c.show_all)
		return true;

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	switch (c2c.display) {
	case DISPLAY_LCL_HITM:
		he->filtered = filter_display(c2c_he->stats.lcl_hitm,
					      stats->lcl_hitm);
		break;
	case DISPLAY_RMT_HITM:
		he->filtered = filter_display(c2c_he->stats.rmt_hitm,
					      stats->rmt_hitm);
		break;
	case DISPLAY_TOT_HITM:
		he->filtered = filter_display(c2c_he->stats.tot_hitm,
					      stats->tot_hitm);
		break;
	case DISPLAY_SNP_PEER:
		he->filtered = filter_display(c2c_he->stats.tot_peer,
					      stats->tot_peer);
		break;
	default:
		break;
	}

	return he->filtered == 0;
}

static inline bool is_valid_hist_entry(struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	bool has_record = false;

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	/* It's a valid entry if contains stores */
	if (c2c_he->stats.store)
		return true;

	switch (c2c.display) {
	case DISPLAY_LCL_HITM:
		has_record = !!c2c_he->stats.lcl_hitm;
		break;
	case DISPLAY_RMT_HITM:
		has_record = !!c2c_he->stats.rmt_hitm;
		break;
	case DISPLAY_TOT_HITM:
		has_record = !!c2c_he->stats.tot_hitm;
		break;
	case DISPLAY_SNP_PEER:
		has_record = !!c2c_he->stats.tot_peer;
	default:
		break;
	}

	return has_record;
}

static void set_node_width(struct c2c_hist_entry *c2c_he, int len)
{
	struct c2c_dimension *dim;

	dim = &c2c.hists == c2c_he->hists ?
	      &dim_dcacheline_node : &dim_offset_node;

	if (len > dim->width)
		dim->width = len;
}

static int set_nodestr(struct c2c_hist_entry *c2c_he)
{
	char buf[30];
	int len;

	if (c2c_he->nodestr)
		return 0;

	if (!bitmap_empty(c2c_he->nodeset, c2c.nodes_cnt)) {
		len = bitmap_scnprintf(c2c_he->nodeset, c2c.nodes_cnt,
				      buf, sizeof(buf));
	} else {
		len = scnprintf(buf, sizeof(buf), "N/A");
	}

	set_node_width(c2c_he, len);
	c2c_he->nodestr = strdup(buf);
	return c2c_he->nodestr ? 0 : -ENOMEM;
}

static void calc_width(struct c2c_hist_entry *c2c_he)
{
	struct c2c_hists *c2c_hists;

	c2c_hists = container_of(c2c_he->he.hists, struct c2c_hists, hists);
	hists__calc_col_len(&c2c_hists->hists, &c2c_he->he);
	set_nodestr(c2c_he);
}

static int filter_cb(struct hist_entry *he, void *arg __maybe_unused)
{
	struct c2c_hist_entry *c2c_he;

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	if (c2c.show_src && !he->srcline)
		he->srcline = hist_entry__srcline(he);

	calc_width(c2c_he);

	if (!is_valid_hist_entry(he))
		he->filtered = HIST_FILTER__C2C;

	return 0;
}

static int resort_cl_cb(struct hist_entry *he, void *arg __maybe_unused)
{
	struct c2c_hist_entry *c2c_he;
	struct c2c_hists *c2c_hists;
	bool display = he__display(he, &c2c.shared_clines_stats);

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	c2c_hists = c2c_he->hists;

	if (display && c2c_hists) {
		static unsigned int idx;

		c2c_he->cacheline_idx = idx++;
		calc_width(c2c_he);

		c2c_hists__reinit(c2c_hists, c2c.cl_output, c2c.cl_resort);

		hists__collapse_resort(&c2c_hists->hists, NULL);
		hists__output_resort_cb(&c2c_hists->hists, NULL, filter_cb);
	}

	return 0;
}

static struct c2c_header header_node_0 = HEADER_LOW("Node");
static struct c2c_header header_node_1_hitms_stores =
		HEADER_LOW("Node{cpus %hitms %stores}");
static struct c2c_header header_node_1_peers_stores =
		HEADER_LOW("Node{cpus %peers %stores}");
static struct c2c_header header_node_2 = HEADER_LOW("Node{cpu list}");

static void setup_nodes_header(void)
{
	switch (c2c.node_info) {
	case 0:
		dim_node.header = header_node_0;
		break;
	case 1:
		if (c2c.display == DISPLAY_SNP_PEER)
			dim_node.header = header_node_1_peers_stores;
		else
			dim_node.header = header_node_1_hitms_stores;
		break;
	case 2:
		dim_node.header = header_node_2;
		break;
	default:
		break;
	}

	return;
}

static int setup_nodes(struct perf_session *session)
{
	struct numa_node *n;
	unsigned long **nodes;
	int node, idx;
	struct perf_cpu cpu;
	int *cpu2node;

	if (c2c.node_info > 2)
		c2c.node_info = 2;

	c2c.nodes_cnt = session->header.env.nr_numa_nodes;
	c2c.cpus_cnt  = session->header.env.nr_cpus_avail;

	n = session->header.env.numa_nodes;
	if (!n)
		return -EINVAL;

	nodes = zalloc(sizeof(unsigned long *) * c2c.nodes_cnt);
	if (!nodes)
		return -ENOMEM;

	c2c.nodes = nodes;

	cpu2node = zalloc(sizeof(int) * c2c.cpus_cnt);
	if (!cpu2node)
		return -ENOMEM;

	for (idx = 0; idx < c2c.cpus_cnt; idx++)
		cpu2node[idx] = -1;

	c2c.cpu2node = cpu2node;

	for (node = 0; node < c2c.nodes_cnt; node++) {
		struct perf_cpu_map *map = n[node].map;
		unsigned long *set;

		set = bitmap_zalloc(c2c.cpus_cnt);
		if (!set)
			return -ENOMEM;

		nodes[node] = set;

		perf_cpu_map__for_each_cpu_skip_any(cpu, idx, map) {
			__set_bit(cpu.cpu, set);

			if (WARN_ONCE(cpu2node[cpu.cpu] != -1, "node/cpu topology bug"))
				return -EINVAL;

			cpu2node[cpu.cpu] = node;
		}
	}

	setup_nodes_header();
	return 0;
}

#define HAS_HITMS(__h) ((__h)->stats.lcl_hitm || (__h)->stats.rmt_hitm)
#define HAS_PEER(__h) ((__h)->stats.lcl_peer || (__h)->stats.rmt_peer)

static int resort_shared_cl_cb(struct hist_entry *he, void *arg __maybe_unused)
{
	struct c2c_hist_entry *c2c_he;
	c2c_he = container_of(he, struct c2c_hist_entry, he);

	if (HAS_HITMS(c2c_he) || HAS_PEER(c2c_he)) {
		c2c.shared_clines++;
		c2c_add_stats(&c2c.shared_clines_stats, &c2c_he->stats);
	}

	return 0;
}

static int hists__iterate_cb(struct hists *hists, hists__resort_cb_t cb)
{
	struct rb_node *next = rb_first_cached(&hists->entries);
	int ret = 0;

	while (next) {
		struct hist_entry *he;

		he = rb_entry(next, struct hist_entry, rb_node);
		ret = cb(he, NULL);
		if (ret)
			break;
		next = rb_next(&he->rb_node);
	}

	return ret;
}

static void print_c2c__display_stats(FILE *out)
{
	int llc_misses;
	struct c2c_stats *stats = &c2c.hists.stats;

	llc_misses = get_load_llc_misses(stats);

	fprintf(out, "=================================================\n");
	fprintf(out, "            Trace Event Information              \n");
	fprintf(out, "=================================================\n");
	fprintf(out, "  Total records                     : %10d\n", stats->nr_entries);
	fprintf(out, "  Locked Load/Store Operations      : %10d\n", stats->locks);
	fprintf(out, "  Load Operations                   : %10d\n", stats->load);
	fprintf(out, "  Loads - uncacheable               : %10d\n", stats->ld_uncache);
	fprintf(out, "  Loads - IO                        : %10d\n", stats->ld_io);
	fprintf(out, "  Loads - Miss                      : %10d\n", stats->ld_miss);
	fprintf(out, "  Loads - no mapping                : %10d\n", stats->ld_noadrs);
	fprintf(out, "  Load Fill Buffer Hit              : %10d\n", stats->ld_fbhit);
	fprintf(out, "  Load L1D hit                      : %10d\n", stats->ld_l1hit);
	fprintf(out, "  Load L2D hit                      : %10d\n", stats->ld_l2hit);
	fprintf(out, "  Load LLC hit                      : %10d\n", stats->ld_llchit + stats->lcl_hitm);
	fprintf(out, "  Load Local HITM                   : %10d\n", stats->lcl_hitm);
	fprintf(out, "  Load Remote HITM                  : %10d\n", stats->rmt_hitm);
	fprintf(out, "  Load Remote HIT                   : %10d\n", stats->rmt_hit);
	fprintf(out, "  Load Local DRAM                   : %10d\n", stats->lcl_dram);
	fprintf(out, "  Load Remote DRAM                  : %10d\n", stats->rmt_dram);
	fprintf(out, "  Load MESI State Exclusive         : %10d\n", stats->ld_excl);
	fprintf(out, "  Load MESI State Shared            : %10d\n", stats->ld_shared);
	fprintf(out, "  Load LLC Misses                   : %10d\n", llc_misses);
	fprintf(out, "  Load access blocked by data       : %10d\n", stats->blk_data);
	fprintf(out, "  Load access blocked by address    : %10d\n", stats->blk_addr);
	fprintf(out, "  Load HIT Local Peer               : %10d\n", stats->lcl_peer);
	fprintf(out, "  Load HIT Remote Peer              : %10d\n", stats->rmt_peer);
	fprintf(out, "  LLC Misses to Local DRAM          : %10.1f%%\n", ((double)stats->lcl_dram/(double)llc_misses) * 100.);
	fprintf(out, "  LLC Misses to Remote DRAM         : %10.1f%%\n", ((double)stats->rmt_dram/(double)llc_misses) * 100.);
	fprintf(out, "  LLC Misses to Remote cache (HIT)  : %10.1f%%\n", ((double)stats->rmt_hit /(double)llc_misses) * 100.);
	fprintf(out, "  LLC Misses to Remote cache (HITM) : %10.1f%%\n", ((double)stats->rmt_hitm/(double)llc_misses) * 100.);
	fprintf(out, "  Store Operations                  : %10d\n", stats->store);
	fprintf(out, "  Store - uncacheable               : %10d\n", stats->st_uncache);
	fprintf(out, "  Store - no mapping                : %10d\n", stats->st_noadrs);
	fprintf(out, "  Store L1D Hit                     : %10d\n", stats->st_l1hit);
	fprintf(out, "  Store L1D Miss                    : %10d\n", stats->st_l1miss);
	fprintf(out, "  Store No available memory level   : %10d\n", stats->st_na);
	fprintf(out, "  No Page Map Rejects               : %10d\n", stats->nomap);
	fprintf(out, "  Unable to parse data source       : %10d\n", stats->noparse);
}

static void print_shared_cacheline_info(FILE *out)
{
	struct c2c_stats *stats = &c2c.shared_clines_stats;
	int hitm_cnt = stats->lcl_hitm + stats->rmt_hitm;

	fprintf(out, "=================================================\n");
	fprintf(out, "    Global Shared Cache Line Event Information   \n");
	fprintf(out, "=================================================\n");
	fprintf(out, "  Total Shared Cache Lines          : %10d\n", c2c.shared_clines);
	fprintf(out, "  Load HITs on shared lines         : %10d\n", stats->load);
	fprintf(out, "  Fill Buffer Hits on shared lines  : %10d\n", stats->ld_fbhit);
	fprintf(out, "  L1D hits on shared lines          : %10d\n", stats->ld_l1hit);
	fprintf(out, "  L2D hits on shared lines          : %10d\n", stats->ld_l2hit);
	fprintf(out, "  LLC hits on shared lines          : %10d\n", stats->ld_llchit + stats->lcl_hitm);
	fprintf(out, "  Load hits on peer cache or nodes  : %10d\n", stats->lcl_peer + stats->rmt_peer);
	fprintf(out, "  Locked Access on shared lines     : %10d\n", stats->locks);
	fprintf(out, "  Blocked Access on shared lines    : %10d\n", stats->blk_data + stats->blk_addr);
	fprintf(out, "  Store HITs on shared lines        : %10d\n", stats->store);
	fprintf(out, "  Store L1D hits on shared lines    : %10d\n", stats->st_l1hit);
	fprintf(out, "  Store No available memory level   : %10d\n", stats->st_na);
	fprintf(out, "  Total Merged records              : %10d\n", hitm_cnt + stats->store);
}

static void print_cacheline(struct c2c_hists *c2c_hists,
			    struct hist_entry *he_cl,
			    struct perf_hpp_list *hpp_list,
			    FILE *out)
{
	char bf[1000];
	struct perf_hpp hpp = {
		.buf            = bf,
		.size           = 1000,
	};
	static bool once;

	if (!once) {
		hists__fprintf_headers(&c2c_hists->hists, out);
		once = true;
	} else {
		fprintf(out, "\n");
	}

	fprintf(out, "  ----------------------------------------------------------------------\n");
	__hist_entry__snprintf(he_cl, &hpp, hpp_list);
	fprintf(out, "%s\n", bf);
	fprintf(out, "  ----------------------------------------------------------------------\n");

	hists__fprintf(&c2c_hists->hists, false, 0, 0, 0, out, false);
}

static void print_pareto(FILE *out)
{
	struct perf_hpp_list hpp_list;
	struct rb_node *nd;
	int ret;
	const char *cl_output;

	if (c2c.display != DISPLAY_SNP_PEER)
		cl_output = "cl_num,"
			    "cl_rmt_hitm,"
			    "cl_lcl_hitm,"
			    "cl_stores_l1hit,"
			    "cl_stores_l1miss,"
			    "cl_stores_na,"
			    "dcacheline";
	else
		cl_output = "cl_num,"
			    "cl_rmt_peer,"
			    "cl_lcl_peer,"
			    "cl_stores_l1hit,"
			    "cl_stores_l1miss,"
			    "cl_stores_na,"
			    "dcacheline";

	perf_hpp_list__init(&hpp_list);
	ret = hpp_list__parse(&hpp_list, cl_output, NULL);

	if (WARN_ONCE(ret, "failed to setup sort entries\n"))
		return;

	nd = rb_first_cached(&c2c.hists.hists.entries);

	for (; nd; nd = rb_next(nd)) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		struct c2c_hist_entry *c2c_he;

		if (he->filtered)
			continue;

		c2c_he = container_of(he, struct c2c_hist_entry, he);
		print_cacheline(c2c_he->hists, he, &hpp_list, out);
	}
}

static void print_c2c_info(FILE *out, struct perf_session *session)
{
	struct evlist *evlist = session->evlist;
	struct evsel *evsel;
	bool first = true;

	fprintf(out, "=================================================\n");
	fprintf(out, "                 c2c details                     \n");
	fprintf(out, "=================================================\n");

	evlist__for_each_entry(evlist, evsel) {
		fprintf(out, "%-36s: %s\n", first ? "  Events" : "", evsel__name(evsel));
		first = false;
	}
	fprintf(out, "  Cachelines sort on                : %s\n",
		display_str[c2c.display]);
	fprintf(out, "  Cacheline data grouping           : %s\n", c2c.cl_sort);
}

static void perf_c2c__hists_fprintf(FILE *out, struct perf_session *session)
{
	setup_pager();

	print_c2c__display_stats(out);
	fprintf(out, "\n");
	print_shared_cacheline_info(out);
	fprintf(out, "\n");
	print_c2c_info(out, session);

	if (c2c.stats_only)
		return;

	fprintf(out, "\n");
	fprintf(out, "=================================================\n");
	fprintf(out, "           Shared Data Cache Line Table          \n");
	fprintf(out, "=================================================\n");
	fprintf(out, "#\n");

	hists__fprintf(&c2c.hists.hists, true, 0, 0, 0, stdout, true);

	fprintf(out, "\n");
	fprintf(out, "=================================================\n");
	fprintf(out, "      Shared Cache Line Distribution Pareto      \n");
	fprintf(out, "=================================================\n");
	fprintf(out, "#\n");

	print_pareto(out);
}

#ifdef HAVE_SLANG_SUPPORT
static void c2c_browser__update_nr_entries(struct hist_browser *hb)
{
	u64 nr_entries = 0;
	struct rb_node *nd = rb_first_cached(&hb->hists->entries);

	while (nd) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);

		if (!he->filtered)
			nr_entries++;

		nd = rb_next(nd);
	}

	hb->nr_non_filtered_entries = nr_entries;
}

struct c2c_cacheline_browser {
	struct hist_browser	 hb;
	struct hist_entry	*he;
};

static int
perf_c2c_cacheline_browser__title(struct hist_browser *browser,
				  char *bf, size_t size)
{
	struct c2c_cacheline_browser *cl_browser;
	struct hist_entry *he;
	uint64_t addr = 0;

	cl_browser = container_of(browser, struct c2c_cacheline_browser, hb);
	he = cl_browser->he;

	if (he->mem_info)
		addr = cl_address(mem_info__daddr(he->mem_info)->addr, chk_double_cl);

	scnprintf(bf, size, "Cacheline 0x%lx", addr);
	return 0;
}

static struct c2c_cacheline_browser*
c2c_cacheline_browser__new(struct hists *hists, struct hist_entry *he)
{
	struct c2c_cacheline_browser *browser;

	browser = zalloc(sizeof(*browser));
	if (browser) {
		hist_browser__init(&browser->hb, hists);
		browser->hb.c2c_filter	= true;
		browser->hb.title	= perf_c2c_cacheline_browser__title;
		browser->he		= he;
	}

	return browser;
}

static int perf_c2c__browse_cacheline(struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	struct c2c_hists *c2c_hists;
	struct c2c_cacheline_browser *cl_browser;
	struct hist_browser *browser;
	int key = -1;
	static const char help[] =
	" ENTER         Toggle callchains (if present) \n"
	" n             Toggle Node details info \n"
	" s             Toggle full length of symbol and source line columns \n"
	" q             Return back to cacheline list \n";

	if (!he)
		return 0;

	/* Display compact version first. */
	c2c.symbol_full = false;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	c2c_hists = c2c_he->hists;

	cl_browser = c2c_cacheline_browser__new(&c2c_hists->hists, he);
	if (cl_browser == NULL)
		return -1;

	browser = &cl_browser->hb;

	/* reset abort key so that it can get Ctrl-C as a key */
	SLang_reset_tty();
	SLang_init_tty(0, 0, 0);

	c2c_browser__update_nr_entries(browser);

	while (1) {
		key = hist_browser__run(browser, "? - help", true, 0);

		switch (key) {
		case 's':
			c2c.symbol_full = !c2c.symbol_full;
			break;
		case 'n':
			c2c.node_info = (c2c.node_info + 1) % 3;
			setup_nodes_header();
			break;
		case 'q':
			goto out;
		case '?':
			ui_browser__help_window(&browser->b, help);
			break;
		default:
			break;
		}
	}

out:
	free(cl_browser);
	return 0;
}

static int perf_c2c_browser__title(struct hist_browser *browser,
				   char *bf, size_t size)
{
	scnprintf(bf, size,
		  "Shared Data Cache Line Table     "
		  "(%lu entries, sorted on %s)",
		  browser->nr_non_filtered_entries,
		  display_str[c2c.display]);
	return 0;
}

static struct hist_browser*
perf_c2c_browser__new(struct hists *hists)
{
	struct hist_browser *browser = hist_browser__new(hists);

	if (browser) {
		browser->title = perf_c2c_browser__title;
		browser->c2c_filter = true;
	}

	return browser;
}

static int perf_c2c__hists_browse(struct hists *hists)
{
	struct hist_browser *browser;
	int key = -1;
	static const char help[] =
	" d             Display cacheline details \n"
	" ENTER         Toggle callchains (if present) \n"
	" q             Quit \n";

	browser = perf_c2c_browser__new(hists);
	if (browser == NULL)
		return -1;

	/* reset abort key so that it can get Ctrl-C as a key */
	SLang_reset_tty();
	SLang_init_tty(0, 0, 0);

	c2c_browser__update_nr_entries(browser);

	while (1) {
		key = hist_browser__run(browser, "? - help", true, 0);

		switch (key) {
		case 'q':
			goto out;
		case 'd':
			perf_c2c__browse_cacheline(browser->he_selection);
			break;
		case '?':
			ui_browser__help_window(&browser->b, help);
			break;
		default:
			break;
		}
	}

out:
	hist_browser__delete(browser);
	return 0;
}

static void perf_c2c_display(struct perf_session *session)
{
	if (use_browser == 0)
		perf_c2c__hists_fprintf(stdout, session);
	else
		perf_c2c__hists_browse(&c2c.hists.hists);
}
#else
static void perf_c2c_display(struct perf_session *session)
{
	use_browser = 0;
	perf_c2c__hists_fprintf(stdout, session);
}
#endif /* HAVE_SLANG_SUPPORT */

static char *fill_line(const char *orig, int len)
{
	int i, j, olen = strlen(orig);
	char *buf;

	buf = zalloc(len + 1);
	if (!buf)
		return NULL;

	j = len / 2 - olen / 2;

	for (i = 0; i < j - 1; i++)
		buf[i] = '-';

	buf[i++] = ' ';

	strcpy(buf + i, orig);

	i += olen;

	buf[i++] = ' ';

	for (; i < len; i++)
		buf[i] = '-';

	return buf;
}

static int ui_quirks(void)
{
	const char *nodestr = "Data address";
	char *buf;

	if (!c2c.use_stdio) {
		dim_offset.width  = 5;
		dim_offset.header = header_offset_tui;
		nodestr = chk_double_cl ? "Double-CL" : "CL";
	}

	dim_percent_costly_snoop.header = percent_costly_snoop_header[c2c.display];

	/* Fix the zero line for dcacheline column. */
	buf = fill_line(chk_double_cl ? "Double-Cacheline" : "Cacheline",
				dim_dcacheline.width +
				dim_dcacheline_node.width +
				dim_dcacheline_count.width + 4);
	if (!buf)
		return -ENOMEM;

	dim_dcacheline.header.line[0].text = buf;

	/* Fix the zero line for offset column. */
	buf = fill_line(nodestr, dim_offset.width +
			         dim_offset_node.width +
				 dim_dcacheline_count.width + 4);
	if (!buf)
		return -ENOMEM;

	dim_offset.header.line[0].text = buf;

	return 0;
}

#define CALLCHAIN_DEFAULT_OPT  "graph,0.5,caller,function,percent"

const char callchain_help[] = "Display call graph (stack chain/backtrace):\n\n"
				CALLCHAIN_REPORT_HELP
				"\n\t\t\t\tDefault: " CALLCHAIN_DEFAULT_OPT;

static int
parse_callchain_opt(const struct option *opt, const char *arg, int unset)
{
	struct callchain_param *callchain = opt->value;

	callchain->enabled = !unset;
	/*
	 * --no-call-graph
	 */
	if (unset) {
		symbol_conf.use_callchain = false;
		callchain->mode = CHAIN_NONE;
		return 0;
	}

	return parse_callchain_report_opt(arg);
}

static int setup_callchain(struct evlist *evlist)
{
	u64 sample_type = evlist__combined_sample_type(evlist);
	enum perf_call_graph_mode mode = CALLCHAIN_NONE;

	if ((sample_type & PERF_SAMPLE_REGS_USER) &&
	    (sample_type & PERF_SAMPLE_STACK_USER)) {
		mode = CALLCHAIN_DWARF;
		dwarf_callchain_users = true;
	} else if (sample_type & PERF_SAMPLE_BRANCH_STACK)
		mode = CALLCHAIN_LBR;
	else if (sample_type & PERF_SAMPLE_CALLCHAIN)
		mode = CALLCHAIN_FP;

	if (!callchain_param.enabled &&
	    callchain_param.mode != CHAIN_NONE &&
	    mode != CALLCHAIN_NONE) {
		symbol_conf.use_callchain = true;
		if (callchain_register_param(&callchain_param) < 0) {
			ui__error("Can't register callchain params.\n");
			return -EINVAL;
		}
	}

	if (c2c.stitch_lbr && (mode != CALLCHAIN_LBR)) {
		ui__warning("Can't find LBR callchain. Switch off --stitch-lbr.\n"
			    "Please apply --call-graph lbr when recording.\n");
		c2c.stitch_lbr = false;
	}

	callchain_param.record_mode = mode;
	callchain_param.min_percent = 0;
	return 0;
}

static int setup_display(const char *str)
{
	const char *display = str;

	if (!strcmp(display, "tot"))
		c2c.display = DISPLAY_TOT_HITM;
	else if (!strcmp(display, "rmt"))
		c2c.display = DISPLAY_RMT_HITM;
	else if (!strcmp(display, "lcl"))
		c2c.display = DISPLAY_LCL_HITM;
	else if (!strcmp(display, "peer"))
		c2c.display = DISPLAY_SNP_PEER;
	else {
		pr_err("failed: unknown display type: %s\n", str);
		return -1;
	}

	return 0;
}

#define for_each_token(__tok, __buf, __sep, __tmp)		\
	for (__tok = strtok_r(__buf, __sep, &__tmp); __tok;	\
	     __tok = strtok_r(NULL,  __sep, &__tmp))

static int build_cl_output(char *cl_sort, bool no_source)
{
	char *tok, *tmp, *buf = strdup(cl_sort);
	bool add_pid   = false;
	bool add_tid   = false;
	bool add_iaddr = false;
	bool add_sym   = false;
	bool add_dso   = false;
	bool add_src   = false;
	int ret = 0;

	if (!buf)
		return -ENOMEM;

	for_each_token(tok, buf, ",", tmp) {
		if (!strcmp(tok, "tid")) {
			add_tid = true;
		} else if (!strcmp(tok, "pid")) {
			add_pid = true;
		} else if (!strcmp(tok, "iaddr")) {
			add_iaddr = true;
			add_sym   = true;
			add_dso   = true;
			add_src   = no_source ? false : true;
		} else if (!strcmp(tok, "dso")) {
			add_dso = true;
		} else if (strcmp(tok, "offset")) {
			pr_err("unrecognized sort token: %s\n", tok);
			ret = -EINVAL;
			goto err;
		}
	}

	if (asprintf(&c2c.cl_output,
		"%s%s%s%s%s%s%s%s%s%s%s%s",
		c2c.use_stdio ? "cl_num_empty," : "",
		c2c.display == DISPLAY_SNP_PEER ? "percent_rmt_peer,"
						  "percent_lcl_peer," :
						  "percent_rmt_hitm,"
						  "percent_lcl_hitm,",
		"percent_stores_l1hit,"
		"percent_stores_l1miss,"
		"percent_stores_na,"
		"offset,offset_node,dcacheline_count,",
		add_pid   ? "pid," : "",
		add_tid   ? "tid," : "",
		add_iaddr ? "iaddr," : "",
		c2c.display == DISPLAY_SNP_PEER ? "mean_rmt_peer,"
						  "mean_lcl_peer," :
						  "mean_rmt,"
						  "mean_lcl,",
		"mean_load,"
		"tot_recs,"
		"cpucnt,",
		add_sym ? "symbol," : "",
		add_dso ? "dso," : "",
		add_src ? "cl_srcline," : "",
		"node") < 0) {
		ret = -ENOMEM;
		goto err;
	}

	c2c.show_src = add_src;
err:
	free(buf);
	return ret;
}

static int setup_coalesce(const char *coalesce, bool no_source)
{
	const char *c = coalesce ?: coalesce_default;
	const char *sort_str = NULL;

	if (asprintf(&c2c.cl_sort, "offset,%s", c) < 0)
		return -ENOMEM;

	if (build_cl_output(c2c.cl_sort, no_source))
		return -1;

	if (c2c.display == DISPLAY_TOT_HITM)
		sort_str = "tot_hitm";
	else if (c2c.display == DISPLAY_RMT_HITM)
		sort_str = "rmt_hitm,lcl_hitm";
	else if (c2c.display == DISPLAY_LCL_HITM)
		sort_str = "lcl_hitm,rmt_hitm";
	else if (c2c.display == DISPLAY_SNP_PEER)
		sort_str = "tot_peer";

	if (asprintf(&c2c.cl_resort, "offset,%s", sort_str) < 0)
		return -ENOMEM;

	pr_debug("coalesce sort   fields: %s\n", c2c.cl_sort);
	pr_debug("coalesce resort fields: %s\n", c2c.cl_resort);
	pr_debug("coalesce output fields: %s\n", c2c.cl_output);
	return 0;
}

static int perf_c2c__report(int argc, const char **argv)
{
	struct itrace_synth_opts itrace_synth_opts = {
		.set = true,
		.mem = true,	/* Only enable memory event */
		.default_no_sample = true,
	};

	struct perf_session *session;
	struct ui_progress prog;
	struct perf_data data = {
		.mode = PERF_DATA_MODE_READ,
	};
	char callchain_default_opt[] = CALLCHAIN_DEFAULT_OPT;
	const char *display = NULL;
	const char *coalesce = NULL;
	bool no_source = false;
	const struct option options[] = {
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_STRING('i', "input", &input_name, "file",
		   "the input file to process"),
	OPT_INCR('N', "node-info", &c2c.node_info,
		 "show extra node info in report (repeat for more info)"),
	OPT_BOOLEAN(0, "stdio", &c2c.use_stdio, "Use the stdio interface"),
	OPT_BOOLEAN(0, "stats", &c2c.stats_only,
		    "Display only statistic tables (implies --stdio)"),
	OPT_BOOLEAN(0, "full-symbols", &c2c.symbol_full,
		    "Display full length of symbols"),
	OPT_BOOLEAN(0, "no-source", &no_source,
		    "Do not display Source Line column"),
	OPT_BOOLEAN(0, "show-all", &c2c.show_all,
		    "Show all captured HITM lines."),
	OPT_CALLBACK_DEFAULT('g', "call-graph", &callchain_param,
			     "print_type,threshold[,print_limit],order,sort_key[,branch],value",
			     callchain_help, &parse_callchain_opt,
			     callchain_default_opt),
	OPT_STRING('d', "display", &display, "Switch HITM output type", "tot,lcl,rmt,peer"),
	OPT_STRING('c', "coalesce", &coalesce, "coalesce fields",
		   "coalesce fields: pid,tid,iaddr,dso"),
	OPT_BOOLEAN('f', "force", &symbol_conf.force, "don't complain, do it"),
	OPT_BOOLEAN(0, "stitch-lbr", &c2c.stitch_lbr,
		    "Enable LBR callgraph stitching approach"),
	OPT_BOOLEAN(0, "double-cl", &chk_double_cl, "Detect adjacent cacheline false sharing"),
	OPT_PARENT(c2c_options),
	OPT_END()
	};
	int err = 0;
	const char *output_str, *sort_str = NULL;

	argc = parse_options(argc, argv, options, report_c2c_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc)
		usage_with_options(report_c2c_usage, options);

#ifndef HAVE_SLANG_SUPPORT
	c2c.use_stdio = true;
#endif

	if (c2c.stats_only)
		c2c.use_stdio = true;

	err = symbol__validate_sym_arguments();
	if (err)
		goto out;

	if (!input_name || !strlen(input_name))
		input_name = "perf.data";

	data.path  = input_name;
	data.force = symbol_conf.force;

	session = perf_session__new(&data, &c2c.tool);
	if (IS_ERR(session)) {
		err = PTR_ERR(session);
		pr_debug("Error creating perf session\n");
		goto out;
	}

	/*
	 * Use the 'tot' as default display type if user doesn't specify it;
	 * since Arm64 platform doesn't support HITMs flag, use 'peer' as the
	 * default display type.
	 */
	if (!display) {
		if (!strcmp(perf_env__arch(&session->header.env), "arm64"))
			display = "peer";
		else
			display = "tot";
	}

	err = setup_display(display);
	if (err)
		goto out_session;

	err = setup_coalesce(coalesce, no_source);
	if (err) {
		pr_debug("Failed to initialize hists\n");
		goto out_session;
	}

	err = c2c_hists__init(&c2c.hists, "dcacheline", 2);
	if (err) {
		pr_debug("Failed to initialize hists\n");
		goto out_session;
	}

	session->itrace_synth_opts = &itrace_synth_opts;

	err = setup_nodes(session);
	if (err) {
		pr_err("Failed setup nodes\n");
		goto out_session;
	}

	err = mem2node__init(&c2c.mem2node, &session->header.env);
	if (err)
		goto out_session;

	err = setup_callchain(session->evlist);
	if (err)
		goto out_mem2node;

	if (symbol__init(&session->header.env) < 0)
		goto out_mem2node;

	/* No pipe support at the moment. */
	if (perf_data__is_pipe(session->data)) {
		pr_debug("No pipe support at the moment.\n");
		goto out_mem2node;
	}

	if (c2c.use_stdio)
		use_browser = 0;
	else
		use_browser = 1;

	setup_browser(false);

	err = perf_session__process_events(session);
	if (err) {
		pr_err("failed to process sample\n");
		goto out_mem2node;
	}

	if (c2c.display != DISPLAY_SNP_PEER)
		output_str = "cl_idx,"
			     "dcacheline,"
			     "dcacheline_node,"
			     "dcacheline_count,"
			     "percent_costly_snoop,"
			     "tot_hitm,lcl_hitm,rmt_hitm,"
			     "tot_recs,"
			     "tot_loads,"
			     "tot_stores,"
			     "stores_l1hit,stores_l1miss,stores_na,"
			     "ld_fbhit,ld_l1hit,ld_l2hit,"
			     "ld_lclhit,lcl_hitm,"
			     "ld_rmthit,rmt_hitm,"
			     "dram_lcl,dram_rmt";
	else
		output_str = "cl_idx,"
			     "dcacheline,"
			     "dcacheline_node,"
			     "dcacheline_count,"
			     "percent_costly_snoop,"
			     "tot_peer,lcl_peer,rmt_peer,"
			     "tot_recs,"
			     "tot_loads,"
			     "tot_stores,"
			     "stores_l1hit,stores_l1miss,stores_na,"
			     "ld_fbhit,ld_l1hit,ld_l2hit,"
			     "ld_lclhit,lcl_hitm,"
			     "ld_rmthit,rmt_hitm,"
			     "dram_lcl,dram_rmt";

	if (c2c.display == DISPLAY_TOT_HITM)
		sort_str = "tot_hitm";
	else if (c2c.display == DISPLAY_RMT_HITM)
		sort_str = "rmt_hitm";
	else if (c2c.display == DISPLAY_LCL_HITM)
		sort_str = "lcl_hitm";
	else if (c2c.display == DISPLAY_SNP_PEER)
		sort_str = "tot_peer";

	c2c_hists__reinit(&c2c.hists, output_str, sort_str);

	ui_progress__init(&prog, c2c.hists.hists.nr_entries, "Sorting...");

	hists__collapse_resort(&c2c.hists.hists, NULL);
	hists__output_resort_cb(&c2c.hists.hists, &prog, resort_shared_cl_cb);
	hists__iterate_cb(&c2c.hists.hists, resort_cl_cb);

	ui_progress__finish();

	if (ui_quirks()) {
		pr_err("failed to setup UI\n");
		goto out_mem2node;
	}

	perf_c2c_display(session);

out_mem2node:
	mem2node__exit(&c2c.mem2node);
out_session:
	perf_session__delete(session);
out:
	return err;
}

static int parse_record_events(const struct option *opt,
			       const char *str, int unset __maybe_unused)
{
	bool *event_set = (bool *) opt->value;
	struct perf_pmu *pmu;

	pmu = perf_mem_events_find_pmu();
	if (!pmu) {
		pr_err("failed: there is no PMU that supports perf c2c\n");
		exit(-1);
	}

	if (!strcmp(str, "list")) {
		perf_pmu__mem_events_list(pmu);
		exit(0);
	}
	if (perf_pmu__mem_events_parse(pmu, str))
		exit(-1);

	*event_set = true;
	return 0;
}


static const char * const __usage_record[] = {
	"perf c2c record [<options>] [<command>]",
	"perf c2c record [<options>] -- <command> [<options>]",
	NULL
};

static const char * const *record_mem_usage = __usage_record;

static int perf_c2c__record(int argc, const char **argv)
{
	int rec_argc, i = 0, j;
	const char **rec_argv;
	int ret;
	bool all_user = false, all_kernel = false;
	bool event_set = false;
	struct perf_mem_event *e;
	struct perf_pmu *pmu;
	struct option options[] = {
	OPT_CALLBACK('e', "event", &event_set, "event",
		     "event selector. Use 'perf c2c record -e list' to list available events",
		     parse_record_events),
	OPT_BOOLEAN('u', "all-user", &all_user, "collect only user level data"),
	OPT_BOOLEAN('k', "all-kernel", &all_kernel, "collect only kernel level data"),
	OPT_UINTEGER('l', "ldlat", &perf_mem_events__loads_ldlat, "setup mem-loads latency"),
	OPT_PARENT(c2c_options),
	OPT_END()
	};

	pmu = perf_mem_events_find_pmu();
	if (!pmu) {
		pr_err("failed: no PMU supports the memory events\n");
		return -1;
	}

	if (perf_pmu__mem_events_init(pmu)) {
		pr_err("failed: memory events not supported\n");
		return -1;
	}

	argc = parse_options(argc, argv, options, record_mem_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	/* Max number of arguments multiplied by number of PMUs that can support them. */
	rec_argc = argc + 11 * (perf_pmu__mem_events_num_mem_pmus(pmu) + 1);

	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	if (!rec_argv)
		return -1;

	rec_argv[i++] = "record";

	if (!event_set) {
		e = perf_pmu__mem_events_ptr(pmu, PERF_MEM_EVENTS__LOAD_STORE);
		/*
		 * The load and store operations are required, use the event
		 * PERF_MEM_EVENTS__LOAD_STORE if it is supported.
		 */
		if (e->tag) {
			e->record = true;
			rec_argv[i++] = "-W";
		} else {
			e = perf_pmu__mem_events_ptr(pmu, PERF_MEM_EVENTS__LOAD);
			e->record = true;

			e = perf_pmu__mem_events_ptr(pmu, PERF_MEM_EVENTS__STORE);
			e->record = true;
		}
	}

	e = perf_pmu__mem_events_ptr(pmu, PERF_MEM_EVENTS__LOAD);
	if (e->record)
		rec_argv[i++] = "-W";

	rec_argv[i++] = "-d";
	rec_argv[i++] = "--phys-data";
	rec_argv[i++] = "--sample-cpu";

	ret = perf_mem_events__record_args(rec_argv, &i);
	if (ret)
		goto out;

	if (all_user)
		rec_argv[i++] = "--all-user";

	if (all_kernel)
		rec_argv[i++] = "--all-kernel";

	for (j = 0; j < argc; j++, i++)
		rec_argv[i] = argv[j];

	if (verbose > 0) {
		pr_debug("calling: ");

		j = 0;

		while (rec_argv[j]) {
			pr_debug("%s ", rec_argv[j]);
			j++;
		}
		pr_debug("\n");
	}

	ret = cmd_record(i, rec_argv);
out:
	free(rec_argv);
	return ret;
}

int cmd_c2c(int argc, const char **argv)
{
	argc = parse_options(argc, argv, c2c_options, c2c_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (!argc)
		usage_with_options(c2c_usage, c2c_options);

	if (strlen(argv[0]) > 2 && strstarts("record", argv[0])) {
		return perf_c2c__record(argc, argv);
	} else if (strlen(argv[0]) > 2 && strstarts("report", argv[0])) {
		return perf_c2c__report(argc, argv);
	} else {
		usage_with_options(c2c_usage, c2c_options);
	}

	return 0;
}
