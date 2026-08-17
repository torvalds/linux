// SPDX-License-Identifier: GPL-2.0
/*
 * C2C function model - function-level cacheline sharing analysis
 *
 * Displays a 3-level hierarchy showing which functions share cachelines:
 *   Level 1: Read-side functions sorted by Cycles % (estimated load cycles)
 *   Level 2: Functions sampled writing the shared lines read by level 1
 *   Level 3: The specific cachelines where the two functions contend
 *
 * Builds the hierarchy from the existing cacheline histograms
 * (c2c_hist_entry->hists), reusing the shared c2c data structures.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <tools/libc_compat.h> /* reallocarray */
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/zalloc.h>

#include "addr_location.h"
#include "c2c.h"
#include "cacheline.h"
#include "hist.h"
#include "map.h"
#include "mem-events.h"
#include "mem-info.h"
#include "sort.h"
#include "symbol.h"
#include "thread.h"

struct c2c_function_model {
	struct c2c_hists	function_hists;
	/* Total estimated cycles across all level-1 entries. */
	u64			total_cycles;
	/* Source cacheline histograms; not owned here. */
	struct c2c_hists	*cl_hists;
	/* --coalesce field list, used to require iaddr. */
	const char		*cl_sort;
	/* Do not cap long symbol names. */
	bool			 symbol_full;
};

static struct c2c_function_model c2c_ext __maybe_unused;

static inline __maybe_unused u64 c2c_hitm_count(const struct c2c_stats *stats)
{
	return stats->tot_hitm;
}

static inline __maybe_unused bool symbol_name_equal(struct symbol *a, struct symbol *b)
{
	/* Two unknown symbols compare equal, matching cmp_null() in util/sort.c. */
	if (!a || !b)
		return a == b;
	return arch__compare_symbol_names(a->name, b->name) == 0;
}

static inline __maybe_unused u64 hist_entry__iaddr(struct hist_entry *he)
{
	if (he->mem_info)
		return mem_info__iaddr(he->mem_info)->addr;
	return he->ip;
}

/*
 * Hierarchy levels (by depth): L1 = read-side function, L2 = the writing
 * function it contends with, L3 = the specific shared cacheline.
 */
static inline bool hist_entry__is_cacheline(struct hist_entry *he)
{
	return he->parent_he && he->parent_he->parent_he;	/* level 3: cacheline */
}

/* Spaces of indent per hierarchy level, like the normal report view. */
#define C2C_FUNC_INDENT 2

/* Width of the folded-sign prefix ("%c ") each identity cell emits. */
#define C2C_FUNC_FOLD_WIDTH 2

/*
 * Write he->depth levels of leading indentation into @buf, so lower-level
 * entries are visually nested under their parent. Returns bytes written.
 */
static int hist_entry__indent(struct hist_entry *he, char *buf, size_t size)
{
	int indent = he->depth * C2C_FUNC_INDENT;

	if (indent <= 0 || (size_t)indent >= size)
		return 0;

	return scnprintf(buf, size, "%*s", indent, "");
}

static int symbol_width(struct hists *hists, struct sort_entry *se)
{
	int width = hists__col_len(hists, se->se_width_idx);

	/*
	 * Cap long symbol names as the cacheline view does. The stored column
	 * length is grown up front to fit the deepest, longest identity cell
	 * (including a level-3 cacheline address), so this cap never shrinks the
	 * column below what the cacheline address needs.
	 */
	if (!c2c_ext.symbol_full && width > SYMBOL_WIDTH)
		width = SYMBOL_WIDTH;

	return width;
}

static struct c2c_dimension dim_symbol_view;

/*
 * c2c_width - Calculate width for a C2C column in function view
 */
static int c2c_width(struct perf_hpp_fmt *fmt,
		     struct perf_hpp *hpp __maybe_unused,
		     struct hists *hists)
{
	struct c2c_fmt *c2c_fmt;
	struct c2c_dimension *dim;

	c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	dim = c2c_fmt->dim;

	if (dim == &dim_symbol_view)
		return symbol_width(hists, dim->se);

	return dim->se ? hists__col_len(hists, dim->se->se_width_idx) :
			 dim->width;
}

static int __maybe_unused c2c_header(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
				     struct hists *hists, int line, int *span)
{
	struct c2c_fmt *c2c_fmt;
	struct c2c_dimension *dim;
	const char *text = NULL;
	int width = c2c_width(fmt, hpp, hists);

	c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	dim = c2c_fmt->dim;

	if (dim->se) {
		text = dim->header.line[line].text;
		/* Use the last line from sort_entry if not defined. */
		if (!text && line == hists->hpp_list->nr_header_lines - 1)
			text = dim->se->se_header;
	} else {
		text = dim->header.line[line].text;

		if (span) {
			if (*span) {
				(*span)--;
				return 0;
			}

			*span = dim->header.line[line].span;
		}
	}

	if (!text)
		text = "";

	return scnprintf(hpp->buf, hpp->size, "%*s", width, text);
}

/*
 * Return the estimated total cycles for a c2c_hist_entry
 * (rmt_hitm + lcl_hitm + rmt_peer + lcl_peer + other loads).
 */
static u64 c2c_hist_entry__cycles(struct c2c_hist_entry *c2c_he)
{
	struct compute_stats *cs = &c2c_he->cstats;
	double cycles = 0;

	/*
	 * compute_stats() in builtin-c2c.c routes each load sample into exactly
	 * one cstats bucket (rmt_hitm, lcl_hitm, rmt_peer, lcl_peer or plain
	 * load), so each bucket's cycle total is its mean times its own sample
	 * count. Summing the per-bucket totals avoids both dropping peer-snoop
	 * cycles and double counting a sample that carries several data-source
	 * flags (e.g. Arm SPE sets HITM and PEER on the same load), which would
	 * happen if the mean were multiplied by the non-exclusive stats counts.
	 */
	cycles += avg_stats(&cs->rmt_hitm) * cs->rmt_hitm.n;
	cycles += avg_stats(&cs->lcl_hitm) * cs->lcl_hitm.n;
	cycles += avg_stats(&cs->rmt_peer) * cs->rmt_peer.n;
	cycles += avg_stats(&cs->lcl_peer) * cs->lcl_peer.n;
	cycles += avg_stats(&cs->load)     * cs->load.n;

	return (u64)cycles;
}

/* Sum c2c_hist_entry__cycles() across all level-1 entries. */
static u64 __maybe_unused c2c_ext__total_cycles(void)
{
	struct rb_node *nd;
	u64 total = 0;

	for (nd = rb_first_cached(&c2c_ext.function_hists.hists.entries); nd;
	     nd = rb_next(nd)) {
		struct c2c_hist_entry *c2c_he =
			rb_entry(nd, struct c2c_hist_entry, he.rb_node);

		total += c2c_hist_entry__cycles(c2c_he);
	}
	return total;
}

/*
 * Store count shown in the column: a level-3 cacheline leaf shows its parent
 * level-2 writer's stores on that line, not all stores on the line. A level-2
 * writer shows the sum across its level-3 cachelines. A level-1 reader shows
 * the sum across all included writers on the cachelines it reads; this is not
 * the reader function's own store count and is not additive across readers.
 */
static u64 hist_entry__displayed_stores(struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he = container_of(he, struct c2c_hist_entry, he);
	struct rb_node *nd;
	u64 stores = 0;

	/* Level-2/3 entries already aggregate the stores they represent. */
	if (he->parent_he)
		return c2c_he->stats.store;

	for (nd = rb_first_cached(&he->hroot_out); nd; nd = rb_next(nd)) {
		struct c2c_hist_entry *child_c2c =
			rb_entry(nd, struct c2c_hist_entry, he.rb_node);
		stores += child_c2c->stats.store;
	}
	return stores;
}

static int
total_stores_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		   struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	u64 total = hist_entry__displayed_stores(he);

	return scnprintf(hpp->buf, hpp->size, "%*" PRIu64, width, total);
}

/*
 * symbol_view_entry - Render the unified, indented identity column.
 *
 * All three levels share this single column so the hierarchy reads top-down
 * with progressive indentation, like the normal report hierarchy view. It is
 * a function-centric view with no dedicated code-address column. Verbose
 * function rows can still include a representative address:
 *   L1 read-side function: "- [k] cpupri_set"
 *   L2 writing function:   "  - [k] pull_rt_task"
 *   L3 shared cacheline:   "      0xff2d0082809da080"
 */
static int
symbol_view_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		  struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	int text_width;
	int ret;
	char folded_sign;

	ret = hist_entry__indent(he, hpp->buf, hpp->size);

	folded_sign = he->has_children ? (he->unfolded ? '-' : '+') : ' ';
	ret += scnprintf(hpp->buf + ret, hpp->size - ret, "%c ", folded_sign);

	text_width = width - ret;
	if (text_width <= 0)
		return ret;

	if (hist_entry__is_cacheline(he)) {
		/* Level 3: the shared cacheline address. */
		u64 addr = he->mem_info ?
			cl_address(mem_info__daddr(he->mem_info)->addr, chk_double_cl) : 0;
		char symbuf[32];

		scnprintf(symbuf, sizeof(symbuf), "0x%" PRIx64, addr);
		ret += scnprintf(hpp->buf + ret, hpp->size - ret, "%-*.*s",
				 text_width, text_width, symbuf);
	} else {
		/* Level 1 and level 2 are both functions. */
		size_t cell_size;
		int len;

		if ((size_t)ret >= hpp->size)
			return ret;

		cell_size = min_t(size_t, hpp->size - ret,
				  (size_t)text_width + 1);
		len = sort_sym.se_snprintf(he, hpp->buf + ret, cell_size,
					   text_width);
		/*
		 * se_snprintf() accumulates repsep_snprintf() calls, which cap
		 * their return at the remaining size - 1 rather than reporting
		 * what the format would have needed, so len stays below
		 * cell_size. Clamp anyway so ret cannot leave hpp->buf.
		 */
		if (len < 0)
			len = 0;
		else
			len = min_t(size_t, len, cell_size - 1);

		ret += len;
		if (len < text_width)
			ret += scnprintf(hpp->buf + ret, hpp->size - ret, "%*s",
					 text_width - len, "");
	}

	return ret;
}

/*
 * cycles_percent_entry - Render cycles percentage column
 */
static int
cycles_percent_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		     struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	u64 fn_cycles, total_cycles;
	char folded_sign;
	double pct;
	int ret, pct_width;

	/* Hide Cycles Percent for child functions and cachelines. */
	if (he->parent_he)
		return scnprintf(hpp->buf, hpp->size, "%*s", width, "");

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	fn_cycles = c2c_hist_entry__cycles(c2c_he);
	/* Populated by build_function_view_hierarchy() once the L1 tree is built. */
	total_cycles = c2c_ext.total_cycles;
	pct = total_cycles > 0 ? (double)fn_cycles / total_cycles * 100.0 : 0.0;

	/* Add folded sign only for level-1 entries */
	folded_sign = he->has_children ? (he->unfolded ? '-' : '+') : ' ';
	ret = scnprintf(hpp->buf, hpp->size, "%c ", folded_sign);

	pct_width = width - ret;
	if (pct_width <= 0)
		return ret;
	ret += scnprintf(hpp->buf + ret, hpp->size - ret, "%*.2f%%", pct_width - 1, pct);
	return ret;
}

/*
 * cycles_percent_cmp - Comparison function for cycles percentage sorting
 */
static int64_t
cycles_percent_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		   struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left = container_of(left, struct c2c_hist_entry, he);
	struct c2c_hist_entry *c2c_right = container_of(right, struct c2c_hist_entry, he);
	u64 cycles_left, cycles_right;

	/* Cycles Percent is only shown for level-1 entries; others compare equal. */
	if (left->parent_he || right->parent_he)
		return 0;

	cycles_left = c2c_hist_entry__cycles(c2c_left);
	cycles_right = c2c_hist_entry__cycles(c2c_right);

	return (cycles_left > cycles_right) - (cycles_left < cycles_right);
}

/*
 * total_stores_cmp - Comparison function for total stores sorting
 */
static int64_t
total_stores_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		 struct hist_entry *left, struct hist_entry *right)
{
	u64 left_store = hist_entry__displayed_stores(left);
	u64 right_store = hist_entry__displayed_stores(right);

	return (left_store > right_store) - (left_store < right_store);
}

/*
 * Function view dimensions
 */
static struct c2c_dimension dim_cycles_percent = {
	.header		= HEADER_BOTH("Cycles", "%"),
	.name		= "cycles_percent",
	.cmp		= cycles_percent_cmp,
	.entry		= cycles_percent_entry,
	.width		= 9,
};

static struct c2c_dimension dim_total_stores = {
	.header		= HEADER_BOTH("Store", "count"),
	.name		= "total_stores",
	.cmp		= total_stores_cmp,
	.entry		= total_stores_entry,
	.width		= 7,
};

static struct c2c_dimension dim_symbol_view = {
	.header		= HEADER_LOW("Function / Contending function / Cacheline"),
	.name		= "symbol_view",
	.se		= &sort_sym,
	.entry		= symbol_view_entry,
	.width		= SYMBOL_WIDTH,
};

static struct c2c_dimension *function_view_dimensions[] __maybe_unused = {
	&dim_cycles_percent,
	&dim_total_stores,
	&dim_symbol_view,
	NULL,
};
