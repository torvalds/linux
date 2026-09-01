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
#include "debug.h"
#include "dso.h"
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

static struct c2c_function_model c2c_ext;

static inline u64 c2c_hitm_count(const struct c2c_stats *stats)
{
	return stats->tot_hitm;
}

static int64_t c2c_function_cmp(const struct map_symbol *left,
				const struct map_symbol *right)
{
	const struct dso *left_dso = left->map ? map__dso(left->map) : NULL;
	const struct dso *right_dso = right->map ? map__dso(right->map) : NULL;
	int ret;

	if (!left_dso || !right_dso) {
		if (left_dso != right_dso)
			return left_dso ? 1 : -1;
	} else {
		/*
		 * Use the same DSO name as _sort__dso_cmp() (short name unless
		 * verbose), so this matches the DSO comparison the level-1
		 * entries are deduplicated by; otherwise same-basename DSOs
		 * could be split or merged inconsistently across levels.
		 */
		const char *left_name = verbose > 0 ?
			dso__long_name(left_dso) : dso__short_name(left_dso);
		const char *right_name = verbose > 0 ?
			dso__long_name(right_dso) : dso__short_name(right_dso);

		ret = strcmp(left_name, right_name);
		if (ret)
			return ret;
	}

	return _sort__sym_cmp(left->sym, right->sym);
}

static inline u64 hist_entry__iaddr(struct hist_entry *he)
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

static int c2c_header(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
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
static u64 c2c_ext__total_cycles(void)
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
	/* Populated by c2c_function__build() once the L1 tree is built. */
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

static struct c2c_dimension *function_view_dimensions[] = {
	&dim_cycles_percent,
	&dim_total_stores,
	&dim_symbol_view,
	NULL,
};

static struct c2c_dimension *get_function_dimension(const char *name)
{
	unsigned int i;

	for (i = 0; function_view_dimensions[i]; i++) {
		struct c2c_dimension *dim = function_view_dimensions[i];

		if (!strcmp(dim->name, name))
			return dim;
	}

	return NULL;
}

/* Wrappers so sort_entry-backed dimensions sort/collapse via their se. */
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
	int64_t (*collapse_fn)(struct hist_entry *a, struct hist_entry *b);

	collapse_fn = dim->se->se_collapse ?: dim->se->se_cmp;
	return collapse_fn(a, b);
}

static int64_t c2c_se_sort(struct perf_hpp_fmt *fmt,
			   struct hist_entry *a, struct hist_entry *b)
{
	struct c2c_fmt *c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	struct c2c_dimension *dim = c2c_fmt->dim;
	int64_t (*sort_fn)(struct hist_entry *a, struct hist_entry *b);

	sort_fn = dim->se->se_sort ?: dim->se->se_cmp;
	return sort_fn(a, b);
}

/*
 * Build the c2c_fmt for @name. Returns:
 *   0        and *fmtp set     on success;
 *   -ENOENT  and *fmtp = NULL   if @name is not a function-view dimension;
 *   -ENOMEM                     if allocation failed (distinct from -ENOENT so
 *                               the caller does not misreport it as an
 *                               "invalid field").
 */
static int get_function_format(const char *name, struct c2c_fmt **fmtp)
{
	struct c2c_dimension *dim = get_function_dimension(name);
	struct c2c_fmt *c2c_fmt;
	struct perf_hpp_fmt *fmt;

	*fmtp = NULL;

	if (!dim)
		return -ENOENT;

	c2c_fmt = zalloc(sizeof(*c2c_fmt));
	if (!c2c_fmt)
		return -ENOMEM;

	fmt = &c2c_fmt->fmt;

	c2c_fmt->dim = dim;
	INIT_LIST_HEAD(&fmt->list);
	INIT_LIST_HEAD(&fmt->sort_list);

	fmt->cmp	= dim->se ? c2c_se_cmp : dim->cmp;
	fmt->sort	= dim->se ? c2c_se_sort : dim->cmp;
	fmt->color	= dim->color;
	fmt->entry	= dim->entry;
	fmt->header	= c2c_header;
	fmt->width	= c2c_width;
	fmt->collapse	= dim->se ? c2c_se_collapse : dim->cmp;
	fmt->equal	= c2c_fmt_equal;
	fmt->free	= c2c_fmt_free;

	*fmtp = c2c_fmt;
	return 0;
}

static int
c2c_function_hists__init_output(struct perf_hpp_list *hpp_list, char *name,
				struct perf_env *env __maybe_unused)
{
	struct c2c_fmt *c2c_fmt;
	int ret;

	ret = get_function_format(name, &c2c_fmt);
	if (ret == -ENOMEM)
		return ret;
	/* The function view only accepts its own dimensions. */
	if (ret == -ENOENT)
		return -EINVAL;

	/*
	 * Mark symbol-backed columns so hists__has(hists, sym) is correct.
	 * Only dim_symbol_view carries a sort_entry (.se); the function
	 * view's field strings are fixed and always include symbol_view, so
	 * this single check is sufficient (unlike the user-configurable
	 * cacheline view, which must also test dim_iaddr).
	 */
	if (c2c_fmt->dim->se == &sort_sym)
		hpp_list->sym = 1;

	perf_hpp_list__column_register(hpp_list, &c2c_fmt->fmt);
	return 0;
}

static int
c2c_function_hists__init_sort(struct perf_hpp_list *hpp_list, char *name,
			      struct perf_env *env __maybe_unused)
{
	struct c2c_fmt *c2c_fmt;
	int ret;

	ret = get_function_format(name, &c2c_fmt);
	if (ret == -ENOMEM)
		return ret;
	/* The function view only accepts its own dimensions. */
	if (ret == -ENOENT)
		return -EINVAL;

	/* Mark symbol-backed sort keys so hists__has(hists, sym) is correct. */
	if (c2c_fmt->dim->se == &sort_sym)
		hpp_list->sym = 1;

	perf_hpp_list__register_sort_field(hpp_list, &c2c_fmt->fmt);
	return 0;
}

typedef int (*hpp_list_add_fn)(struct perf_hpp_list *hpp_list, char *name,
			       struct perf_env *env);

static int function_hpp_list__add_tokens(struct perf_hpp_list *hpp_list, char *list,
					 struct perf_env *env, hpp_list_add_fn add)
{
	char *tok, *tmp;
	int ret;

	if (!list)
		return 0;

	for (tok = strtok_r(list, ", ", &tmp); tok; tok = strtok_r(NULL, ", ", &tmp)) {
		ret = add(hpp_list, tok, env);
		if (ret) {
			if (ret == -EINVAL || ret == -ESRCH)
				pr_err("Invalid c2c function-view field: %s\n", tok);
			return ret;
		}
	}
	return 0;
}

/*
 * Append the function view's sort keys to its own output fields, mirroring
 * perf_hpp__setup_output_field() but on the local @list. The shared helper
 * registers onto the global perf_hpp_list, which would leave this local list
 * without output columns, so the function view keeps its own copy here.
 */
static void c2c_function_hists__setup_output_field(struct perf_hpp_list *list)
{
	struct perf_hpp_fmt *fmt;

	perf_hpp_list__for_each_sort_list(list, fmt) {
		struct perf_hpp_fmt *pos;

		if (!fmt->entry && !fmt->color)
			continue;

		perf_hpp_list__for_each_format(list, pos) {
			if (c2c_fmt_equal(fmt, pos))
				goto next;
		}

		perf_hpp_list__column_register(list, fmt);
next:
		continue;
	}
}

static int
function_hpp_list__parse(struct perf_hpp_list *hpp_list,
			 const char *output_str,
			 const char *sort_str,
			 struct perf_env *env)
{
	char *output = output_str ? strdup(output_str) : NULL;
	char *sort   = sort_str   ? strdup(sort_str)   : NULL;
	int ret = 0;

	if ((output_str && !output) || (sort_str && !sort)) {
		ret = -ENOMEM;
		goto out;
	}

	ret = function_hpp_list__add_tokens(hpp_list, output, env,
					    c2c_function_hists__init_output);
	if (ret)
		goto out;

	ret = function_hpp_list__add_tokens(hpp_list, sort, env,
					    c2c_function_hists__init_sort);
	if (ret)
		goto out;

	c2c_function_hists__setup_output_field(hpp_list);
out:
	if (ret)
		perf_hpp__reset_output_field(hpp_list);
	free(output);
	free(sort);
	return ret;
}

static int
c2c_function_hists__init(struct c2c_hists *hists,
			 const char *sort,
			 int nr_header_lines,
			 struct perf_env *env)
{
	__hists__init(&hists->hists, &hists->list);

	perf_hpp_list__init(&hists->list);

	hists->list.nr_header_lines = nr_header_lines;

	return function_hpp_list__parse(&hists->list, /*output=*/NULL, sort, env);
}

static int
c2c_function_hists__reinit(struct c2c_hists *c2c_hists,
			   const char *output,
			   const char *sort,
			   struct perf_env *env)
{
	int nr_header_lines = c2c_hists->list.nr_header_lines;

	perf_hpp__reset_output_field(&c2c_hists->list);

	/* Clear stale state flags so a different output/sort set starts fresh. */
	c2c_hists->list.need_collapse = 0;
	c2c_hists->list.parent = 0;
	c2c_hists->list.sym = 0;
	c2c_hists->list.dso = 0;
	c2c_hists->list.socket = 0;
	c2c_hists->list.thread = 0;
	c2c_hists->list.comm = 0;
	c2c_hists->list.comm_nodigit = 0;
	c2c_hists->list.nr_header_lines = nr_header_lines;

	return function_hpp_list__parse(&c2c_hists->list, output, sort, env);
}

/* Welford online merge of two "stats" (from util/stat.h) accumulators. */
static void c2c_stats_merge(struct stats *dest, const struct stats *src)
{
	double delta;

	if (src->n == 0)
		return;

	if (dest->n == 0) {
		*dest = *src;
		return;
	}

	delta = src->mean - dest->mean;
	dest->M2 += src->M2 + delta * delta * dest->n * src->n / (dest->n + src->n);
	dest->mean = (dest->mean * dest->n + src->mean * src->n) / (dest->n + src->n);
	dest->n += src->n;

	/* Update min/max */
	if (src->max > dest->max)
		dest->max = src->max;
	if (src->min < dest->min)
		dest->min = src->min;
}

/* Merge compute_stats during function aggregation. */
static void c2c_add_cstats(struct compute_stats *dest,
			   const struct compute_stats *src)
{
	c2c_stats_merge(&dest->rmt_hitm, &src->rmt_hitm);
	c2c_stats_merge(&dest->lcl_hitm, &src->lcl_hitm);
	c2c_stats_merge(&dest->rmt_peer, &src->rmt_peer);
	c2c_stats_merge(&dest->lcl_peer, &src->lcl_peer);
	c2c_stats_merge(&dest->load, &src->load);
}

static bool hist_entry__add_c2c_stats(struct hist_entry *he,
				      const struct c2c_stats *stats)
{
	u64 nr_events = c2c_hitm_count(stats) + stats->rmt_peer + stats->lcl_peer;
	u64 weight1 = c2c_hitm_count(stats);

	/*
	 * Allocate before touching he->stat, so a failure here leaves the
	 * entry unmodified and the caller can bail out without having
	 * half-updated the statistics.
	 */
	if (symbol_conf.cumulate_callchain && !he->stat_acc) {
		he->stat_acc = calloc(1, sizeof(struct he_stat));
		if (!he->stat_acc)
			return false;
	}

	he->stat.nr_events += nr_events;
	he->stat.period += nr_events;
	he->stat.weight1 += weight1;

	if (!symbol_conf.cumulate_callchain)
		return true;

	he->stat_acc->nr_events += nr_events;
	he->stat_acc->period += nr_events;
	he->stat_acc->weight1 += weight1;

	return true;
}

static void c2c_he__free_hierarchy(struct hist_entry *he);

/*
 * Free a function-view histogram entry (hist_entry_ops::free).
 */
static void c2c_function_he_free(void *ptr)
{
	struct hist_entry *he = ptr;
	struct c2c_hist_entry *c2c_he;

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	if (c2c_he->hists) {
		perf_hpp__reset_output_field(&c2c_he->hists->list);
		hists__delete_all_entries(&c2c_he->hists->hists);
		zfree(&c2c_he->hists);
	}

	c2c_he__free_hierarchy(he);

	free(c2c_he);
}

static void c2c_he__free_hierarchy(struct hist_entry *he)
{
	struct rb_node *nd;
	struct hist_entry *child_he;

	/*
	 * A leaf entry stores its callchains in the sorted_chain member, which
	 * shares a union with the hroot_in/hroot_out child trees, so its
	 * hroot_out is not a valid subtree to walk. Leaf entries never have a
	 * child hierarchy here, so stop before touching hroot_out.
	 */
	if (he->leaf)
		return;

	if (RB_EMPTY_ROOT(&he->hroot_out.rb_root))
		return;

	nd = rb_first_cached(&he->hroot_out);
	while (nd) {
		struct rb_node *next = rb_next(nd);

		child_he = rb_entry(nd, struct hist_entry, rb_node);
		rb_erase_cached(&child_he->rb_node, &he->hroot_out);
		hist_entry__delete(child_he);

		nd = next;
	}

	/* All children erased; clear the tree (and its cached leftmost). */
	he->hroot_out = RB_ROOT_CACHED;
}

/*
 * Drop level-2 writing functions that carry no stores or
 * no cacheline children. Writers are only added when they store into a shared
 * line, so this is mainly a safety net. Returns the number of surviving
 * writers.
 */
static int c2c_he__prune_empty_writers(struct hist_entry *l1_he)
{
	struct rb_node *nd;
	int surviving = 0;

	if (!l1_he->has_children)
		return 0;

	nd = rb_first_cached(&l1_he->hroot_out);
	while (nd) {
		struct rb_node *next = rb_next(nd);
		struct hist_entry *l2_he = rb_entry(nd, struct hist_entry, rb_node);

		if (l2_he->has_children && hist_entry__displayed_stores(l2_he) > 0) {
			surviving++;
		} else {
			rb_erase_cached(&l2_he->rb_node, &l1_he->hroot_out);
			hist_entry__delete(l2_he);
		}
		nd = next;
	}

	if (!surviving) {
		l1_he->hroot_out = RB_ROOT_CACHED;
		l1_he->has_children = false;
		l1_he->unfolded = false;
	}
	return surviving;
}

static void *c2c_function_he_zalloc(size_t size)
{
	struct c2c_hist_entry *c2c_he = zalloc(sizeof(*c2c_he) + size);

	if (!c2c_he)
		return NULL;

	init_stats(&c2c_he->cstats.lcl_hitm);
	init_stats(&c2c_he->cstats.rmt_hitm);
	init_stats(&c2c_he->cstats.lcl_peer);
	init_stats(&c2c_he->cstats.rmt_peer);
	init_stats(&c2c_he->cstats.load);

	return &c2c_he->he;
}

/* Entry operations for function view */
static struct hist_entry_ops c2c_function_entry_ops = {
	.new	= c2c_function_he_zalloc,
	.free	= c2c_function_he_free,
};

static struct c2c_hist_entry *
c2c_child_entry__alloc(struct hist_entry *parent_he, struct hist_entry *src_he,
		       int depth, u64 ip)
{
	struct c2c_hist_entry *child_c2c;
	struct hist_entry *child_he;

	/* Function-view children never own or display callchains. */
	child_he = c2c_function_he_zalloc(0);
	if (!child_he)
		return NULL;

	child_c2c = container_of(child_he, struct c2c_hist_entry, he);
	child_he->ops = &c2c_function_entry_ops;
	map_symbol__copy(&child_he->ms, &src_he->ms);

	if (src_he->mem_info) {
		child_he->mem_info = mem_info__clone(src_he->mem_info);
		if (!child_he->mem_info)
			goto out_free;
	}

	child_he->thread = thread__get(src_he->thread);
	child_he->cpumode = src_he->cpumode;
	child_he->cpu = src_he->cpu;
	child_he->socket = src_he->socket;
	child_he->level = src_he->level;
	child_he->ip = ip;

	child_he->parent_he = parent_he;
	child_he->depth = depth;
	child_he->leaf = (depth >= 2);
	child_he->hists = &c2c_ext.function_hists.hists;
	child_he->filtered = false;
	child_he->unfolded = false;
	child_he->has_children = false;
	child_he->has_no_entry = false;
	child_he->nr_rows = 0;
	child_he->row_offset = 0;

	memset(&child_he->stat, 0, sizeof(child_he->stat));
	child_he->hroot_in = RB_ROOT_CACHED;
	child_he->hroot_out = RB_ROOT_CACHED;
	INIT_LIST_HEAD(&child_he->pairs.node);
	child_he->hpp_list = &c2c_ext.function_hists.list;
	if (symbol_conf.cumulate_callchain) {
		child_he->stat_acc = calloc(1, sizeof(struct he_stat));
		if (!child_he->stat_acc)
			goto out_free;
	}

	return child_c2c;

out_free:
	hist_entry__delete(child_he);
	return NULL;
}

static void
c2c_child_entry__insert(struct hist_entry *parent_he, struct hist_entry *child_he,
			struct rb_node **p, struct rb_node *rb_parent, bool leftmost)
{
	rb_link_node(&child_he->rb_node, rb_parent, p);
	rb_insert_color_cached(&child_he->rb_node, &parent_he->hroot_out, leftmost);

	parent_he->has_children = true;
	parent_he->leaf = false;
}

static struct hist_entry *
c2c_function_hists__level1_entry(struct symbol *sym,
				 struct hist_entry *detail_he,
				 struct thread *synthetic_thread)
{
	struct addr_location al;
	struct perf_sample sample = {};
	struct mem_info *mi;
	struct hist_entry *he;
	/*
	 * Key the level-1 entry by the function, not by a specific code
	 * address: use the symbol start so every instruction address inside
	 * the same function collapses into one entry. This makes level 1 a
	 * true "function view" rather than a per-code-address view.
	 */
	u64 sym_start = (sym && detail_he->ms.map) ?
			map__unmap_ip(detail_he->ms.map, sym->start) : detail_he->ip;

	mi = mem_info__new();
	if (!mi)
		return NULL;

	mem_info__iaddr(mi)->addr = sym_start;
	/* mem_info__put() will map_symbol__exit() these, so take refs. */
	mem_info__iaddr(mi)->ms.thread = thread__get(detail_he->ms.thread);
	mem_info__iaddr(mi)->ms.map = map__get(detail_he->ms.map);
	mem_info__iaddr(mi)->ms.sym = sym;
	mem_info__daddr(mi)->addr = 0;

	addr_location__init(&al);
	al.thread = thread__get(synthetic_thread);
	al.map = map__get(detail_he->ms.map);
	al.sym = sym;
	al.addr = sym_start;
	al.level = detail_he->level;
	al.cpumode = detail_he->cpumode;
	al.cpu = 0;
	al.socket = 0;
	al.filtered = 0;
	al.latency = 0;

	/*
	 * Synthetic sample: period/weight are placeholders only. The real
	 * c2c counters live in c2c_hist_entry::stats and are added via
	 * hist_entry__add_c2c_stats(); no function-view column or sort key
	 * reads he->stat.period/nr_events, so the +1 that __hists__add_entry()
	 * accrues on each dedup hit has no effect on what is displayed.
	 */
	sample.period = 1;
	sample.weight = 1;
	sample.ip = sym_start;
	sample.pid = thread__pid(synthetic_thread);
	sample.tid = thread__tid(synthetic_thread);
	sample.cpu = 0;

	/* Add entry - histogram handles dedup */
	he = hists__add_entry_ops(&c2c_ext.function_hists.hists,
				  &c2c_function_entry_ops,
				  &al, NULL, NULL, mi,
				  NULL, &sample, true);

	addr_location__exit(&al);
	mem_info__put(mi);

	if (he)
		he->hpp_list = &c2c_ext.function_hists.list;

	return he;
}

/*
 * Level 2: a function that writes a cacheline the level-1 function reads,
 * keyed by the DSO display name and symbol, consistently with perf's symbol
 * sort semantics. All code addresses and cachelines for the same writer
 * function aggregate into one row.
 */
static struct c2c_hist_entry *
c2c_function_hists__level2_entry(struct c2c_hist_entry *level1_c2c,
				 struct symbol *sym, struct hist_entry *detail_he)
{
	struct hist_entry *level1_he = &level1_c2c->he;
	struct rb_node **p = &level1_he->hroot_out.rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct c2c_hist_entry *level2_c2c;
	bool leftmost = true;

	while (*p) {
		struct hist_entry *iter = rb_entry(*p, struct hist_entry, rb_node);
		struct map_symbol key = detail_he->ms;
		int64_t cmp;

		key.sym = sym;
		parent = *p;
		cmp = c2c_function_cmp(&key, &iter->ms);

		if (cmp < 0) {
			p = &parent->rb_left;
		} else if (cmp > 0) {
			p = &parent->rb_right;
			leftmost = false;
		} else {
			return container_of(iter, struct c2c_hist_entry, he);
		}
	}

	/* Key by the function symbol start so all code addresses collapse. */
	level2_c2c = c2c_child_entry__alloc(level1_he, detail_he, 1,
					    (sym && detail_he->ms.map) ?
						  map__unmap_ip(detail_he->ms.map, sym->start) :
						  hist_entry__iaddr(detail_he));
	if (!level2_c2c)
		return NULL;

	/* Key this level by the looked-up symbol, not detail_he's. */
	level2_c2c->he.ms.sym = sym;
	if (level2_c2c->he.mem_info)
		mem_info__iaddr(level2_c2c->he.mem_info)->ms.sym = sym;

	c2c_child_entry__insert(level1_he, &level2_c2c->he, p, parent, leftmost);

	return level2_c2c;
}

/* Level 3: one source cacheline where the L1/L2 functions contend. */
static struct c2c_hist_entry *
c2c_function_hists__level3_entry(struct c2c_hist_entry *level2_c2c,
				 struct c2c_hist_entry *cacheline_src_he)
{
	struct hist_entry *level2_he = &level2_c2c->he;
	struct rb_node **p = &level2_he->hroot_out.rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct c2c_hist_entry *level3_c2c;
	bool leftmost = true;

	while (*p) {
		struct c2c_hist_entry *iter_c2c =
			rb_entry(*p, struct c2c_hist_entry, he.rb_node);

		parent = *p;
		if (cacheline_src_he->cacheline_idx < iter_c2c->cacheline_idx) {
			p = &parent->rb_left;
		} else if (cacheline_src_he->cacheline_idx > iter_c2c->cacheline_idx) {
			p = &parent->rb_right;
			leftmost = false;
		} else {
			return iter_c2c;
		}
	}

	level3_c2c = c2c_child_entry__alloc(level2_he, &cacheline_src_he->he, 2,
					    hist_entry__iaddr(&cacheline_src_he->he));
	if (!level3_c2c)
		return NULL;
	level3_c2c->cacheline_idx = cacheline_src_he->cacheline_idx;

	c2c_child_entry__insert(level2_he, &level3_c2c->he, p, parent, leftmost);

	return level3_c2c;
}

struct hist_entry *c2c_function__find_cacheline(struct hist_entry *he_selection)
{
	struct c2c_hist_entry *c2c_he;
	struct rb_node *nd;

	if (!c2c_ext.cl_hists || !he_selection || !he_selection->parent_he ||
	    !he_selection->parent_he->parent_he)
		return NULL;

	c2c_he = container_of(he_selection, struct c2c_hist_entry, he);

	for (nd = rb_first_cached(&c2c_ext.cl_hists->hists.entries); nd;
	     nd = rb_next(nd)) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		struct c2c_hist_entry *cacheline_he;

		if (he->filtered)
			continue;

		cacheline_he = container_of(he, struct c2c_hist_entry, he);
		if (cacheline_he->hists &&
		    cacheline_he->cacheline_idx == c2c_he->cacheline_idx)
			return he;
	}

	return NULL;
}

/*
 * Re-sort child entries of @parent_he by total store count, descending.
 */
static void c2c_he__resort_by_stores(struct hist_entry *parent_he)
{
	struct rb_root_cached new_root = RB_ROOT_CACHED;
	struct rb_node *nd;

	if (!parent_he->has_children)
		return;

	/* Extract all nodes and re-insert sorted by displayed store count */
	while ((nd = rb_first_cached(&parent_he->hroot_out))) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		u64 he_store = hist_entry__displayed_stores(he);
		struct rb_node **p = &new_root.rb_root.rb_node;
		struct rb_node *parent = NULL;
		bool leftmost = true;
		int cmp;

		/* Remove from current tree */
		rb_erase_cached(&he->rb_node, &parent_he->hroot_out);

		/*
		 * Insert sorted by store count, descending. Use the displayed
		 * store count so a level-1 function and level-2 writer (whose own
		 * stats.store is 0 / partial) sort by the aggregated write traffic
		 * beneath them, not by their own store field.
		 */
		while (*p) {
			struct hist_entry *iter = rb_entry(*p, struct hist_entry, rb_node);
			u64 iter_store = hist_entry__displayed_stores(iter);

			parent = *p;
			if (he_store != iter_store) {
				cmp = he_store > iter_store ? -1 : 1;
			} else {
				/* Stable tie-break: instruction address, name, then cacheline. */
				u64 a = hist_entry__iaddr(he), b = hist_entry__iaddr(iter);

				if (a != b)
					cmp = a < b ? -1 : 1;
				else if (he->ms.sym && iter->ms.sym)
					cmp = strcmp(he->ms.sym->name,
						     iter->ms.sym->name);
				else
					cmp = (iter->ms.sym ? 1 : 0) - (he->ms.sym ? 1 : 0);

				if (!cmp) {
					struct c2c_hist_entry *he_c2c;
					struct c2c_hist_entry *iter_c2c;

					he_c2c = container_of(he, struct c2c_hist_entry, he);
					iter_c2c = container_of(iter, struct c2c_hist_entry, he);
					if (he_c2c->cacheline_idx != iter_c2c->cacheline_idx)
						cmp = he_c2c->cacheline_idx <
						      iter_c2c->cacheline_idx ? -1 : 1;
				}
			}

			if (cmp < 0) {
				p = &parent->rb_left;
			} else {
				p = &parent->rb_right;
				leftmost = false;
			}
		}

		rb_link_node(&he->rb_node, parent, p);
		rb_insert_color_cached(&he->rb_node, &new_root, leftmost);
	}

	parent_he->hroot_out = new_root;
}

/* Initial per-cacheline capacity for the seen[] set; grown on demand. */
#define DEFAULT_SYMBOLS_PER_CL 64

struct function_seen {
	struct map_symbol ms;
};

static bool function_seen__find(const struct function_seen *seen, int nr,
				const struct map_symbol *ms)
{
	int i;

	for (i = 0; i < nr; i++) {
		if (!c2c_function_cmp(&seen[i].ms, ms))
			return true;
	}
	return false;
}

/* Aggregate stats from the cacheline-side entry @c2c_b into level 2/3 @dst. */
static bool c2c_he__add_sharing(struct c2c_hist_entry *dst, struct c2c_hist_entry *src)
{
	/* Do the fallible update first so a failure leaves dst unmodified. */
	if (!hist_entry__add_c2c_stats(&dst->he, &src->stats))
		return false;

	c2c_add_stats(&dst->stats, &src->stats);
	c2c_add_cstats(&dst->cstats, &src->cstats);
	return true;
}

/*
 * Process one cacheline: for every function reading it, create/update its
 * level-1 function entry, then for each function that writes the line
 * add it as a level-2 writer and add this cacheline as a level-3 child.
 */
static int c2c_function__process_cl(struct c2c_hist_entry *cacheline_he,
				    struct thread *synthetic_thread)
{
	struct rb_node *nd_a, *nd_b;
	struct function_seen *seen = NULL;
	int nr_seen = 0, nr_alloc = 0;
	int ret = 0;

	for (nd_a = rb_first_cached(&cacheline_he->hists->hists.entries); nd_a;
	     nd_a = rb_next(nd_a)) {
		struct hist_entry *he_a = rb_entry(nd_a, struct hist_entry, rb_node);
		struct c2c_hist_entry *c2c_a;
		struct hist_entry *level1_he;
		struct c2c_hist_entry *level1_c2c;

		if (!he_a->ms.sym || he_a->filtered)
			continue;

		c2c_a = container_of(he_a, struct c2c_hist_entry, he);
		if (c2c_a->stats.load == 0)
			continue;

		level1_he = c2c_function_hists__level1_entry(he_a->ms.sym,
							     he_a, synthetic_thread);
		if (!level1_he) {
			ret = -ENOMEM;
			goto out;
		}

		level1_c2c = container_of(level1_he, struct c2c_hist_entry, he);

		/*
		 * Aggregate every source entry into its level-1 function parent.
		 * level1_he is keyed by symbol, so all code addresses inside the
		 * same function collapse into one parent. When the cacheline view
		 * splits a function into siblings (different code addresses, or
		 * --coalesce pid/tid/dso), each sibling holds a DISJOINT slice of the
		 * traffic, so summing them here is correct accumulation, not
		 * double counting. The seen[] set below therefore guards only the
		 * subtree build (to avoid building a function's level-2/3 subtree
		 * twice for the same cacheline), never this L1 update. Update
		 * he->stat first; on failure leave the aggregates untouched.
		 */
		if (!hist_entry__add_c2c_stats(level1_he, &c2c_a->stats)) {
			ret = -ENOMEM;
			goto out;
		}
		c2c_add_stats(&level1_c2c->stats, &c2c_a->stats);
		c2c_add_cstats(&level1_c2c->cstats, &c2c_a->cstats);
		c2c_add_stats(&c2c_ext.function_hists.stats, &c2c_a->stats);

		/* Build this function's subtree for this cacheline only once. */
		if (function_seen__find(seen, nr_seen, &he_a->ms))
			continue;

		if (nr_seen == nr_alloc) {
			struct function_seen *tmp;
			int new_alloc = nr_alloc ? nr_alloc * 2 : DEFAULT_SYMBOLS_PER_CL;

			tmp = reallocarray(seen, new_alloc, sizeof(*seen));
			if (!tmp) {
				ret = -ENOMEM;
				goto out;
			}
			seen = tmp;
			nr_alloc = new_alloc;
		}
		seen[nr_seen].ms = he_a->ms;
		nr_seen++;

		for (nd_b = rb_first_cached(&cacheline_he->hists->hists.entries); nd_b;
		     nd_b = rb_next(nd_b)) {
			struct hist_entry *he_b = rb_entry(nd_b, struct hist_entry, rb_node);
			struct c2c_hist_entry *c2c_b, *level2_c2c, *level3_c2c;

			if (!he_b->ms.sym || he_b->filtered)
				continue;

			c2c_b = container_of(he_b, struct c2c_hist_entry, he);

			/*
			 * The level-1 function contributes read-side load weight for this
			 * cacheline. Associate it with functions sampled storing to the
			 * same line.
			 * The writer can be the same function; after detail coalescing and
			 * function-level grouping there is not enough identity to attribute
			 * that case to a specific thread.
			 * Only writers are contending functions, so keep the ones
			 * that actually store into the line.
			 */
			if (c2c_b->stats.store == 0)
				continue;

			/* Level 2: the writing function (aggregated across cachelines). */
			level2_c2c = c2c_function_hists__level2_entry(level1_c2c, he_b->ms.sym,
								      he_b);
			if (!level2_c2c || !c2c_he__add_sharing(level2_c2c, c2c_b)) {
				ret = -ENOMEM;
				goto out;
			}

			/* Level 3: the specific cacheline they contend over. */
			level3_c2c = c2c_function_hists__level3_entry(level2_c2c,
								      cacheline_he);
			if (!level3_c2c || !c2c_he__add_sharing(level3_c2c, c2c_b)) {
				ret = -ENOMEM;
				goto out;
			}
		}
	}

out:
	free(seen);
	return ret;
}

/*
 * Remove a level-1 function that has no contended cachelines left. It is a
 * normal (owned) hist_entry in function_hists, so mirror hists__delete_entry()
 * for the no-collapse case: unlink from both trees, fix the counters, then
 * delete. Its hroot_out is already empty after pruning.
 */
static void c2c_function__drop_level1(struct hist_entry *he)
{
	struct hists *hists = &c2c_ext.function_hists.hists;

	rb_erase_cached(&he->rb_node_in, hists->entries_in);
	rb_erase_cached(&he->rb_node, &hists->entries);

	--hists->nr_entries;
	if (!he->filtered)
		--hists->nr_non_filtered_entries;

	hist_entry__delete(he);
}

/* Length of the identity text (symbol name or cacheline address) at @he. */
static int c2c_function__ident_len(struct hist_entry *he)
{
	char buf[512];
	char *symbuf;
	size_t size;
	int len;

	if (hist_entry__is_cacheline(he)) {
		u64 addr = he->mem_info ?
			cl_address(mem_info__daddr(he->mem_info)->addr, chk_double_cl) : 0;

		return scnprintf(buf, sizeof(buf), "0x%" PRIx64, addr);
	}

	if (!he->ms.sym)
		return 0;

	/*
	 * Match symbol_view_entry(): sort_sym adds the cpumode prefix and, in
	 * verbose mode, the address and DSO origin before the symbol name.
	 */
	size = strlen(he->ms.sym->name) + 64;
	symbuf = malloc(size);
	if (!symbuf)
		return size - 1;

	len = sort_sym.se_snprintf(he, symbuf, size, size - 1);
	free(symbuf);
	return len;
}

/*
 * Grow the symbol column so the deepest, longest identity cell fits. The
 * generic hists__calc_col_len() only measures the top-level (L1) entries; the
 * hand-linked L2 writers and L3 cacheline addresses live in hroot_out and are
 * never measured, so with a short L1 name the indented L2/L3 text would be
 * truncated. Account for the per-level indent and the folded-sign prefix.
 */
static void c2c_function__update_symbol_width(struct hist_entry *he)
{
	struct hists *hists = &c2c_ext.function_hists.hists;
	int need = he->depth * C2C_FUNC_INDENT + C2C_FUNC_FOLD_WIDTH +
		   c2c_function__ident_len(he);

	if (need > hists__col_len(hists, HISTC_SYMBOL))
		hists__set_col_len(hists, HISTC_SYMBOL, need);
}

/*
 * Prune writers with no stores, drop functions left with no contending
 * writer, sort the survivors by store count, then compute the global total.
 */
static void c2c_function__finalize(void)
{
	struct rb_node *nd_l1;

	nd_l1 = rb_first_cached(&c2c_ext.function_hists.hists.entries);
	while (nd_l1) {
		struct hist_entry *he_l1 = rb_entry(nd_l1, struct hist_entry, rb_node);
		struct rb_node *next_l1 = rb_next(nd_l1);
		struct rb_node *nd_l2;

		/* Drop writers with no stores before sorting. */
		if (!he_l1->has_children || !c2c_he__prune_empty_writers(he_l1)) {
			/* No contending writer: this function is not shared. */
			c2c_function__drop_level1(he_l1);
			nd_l1 = next_l1;
			continue;
		}

		c2c_he__resort_by_stores(he_l1);
		c2c_function__update_symbol_width(he_l1);

		for (nd_l2 = rb_first_cached(&he_l1->hroot_out); nd_l2;
		     nd_l2 = rb_next(nd_l2)) {
			struct hist_entry *he_l2 = rb_entry(nd_l2, struct hist_entry, rb_node);
			struct rb_node *nd_l3;

			c2c_function__update_symbol_width(he_l2);

			if (he_l2->has_children)
				c2c_he__resort_by_stores(he_l2);

			for (nd_l3 = rb_first_cached(&he_l2->hroot_out); nd_l3;
			     nd_l3 = rb_next(nd_l3)) {
				struct hist_entry *he_l3 = rb_entry(nd_l3, struct hist_entry,
								    rb_node);

				c2c_function__update_symbol_width(he_l3);
			}
		}

		nd_l1 = next_l1;
	}

	/*
	 * Compute the Cycles % denominator from the surviving level-1 entries
	 * after pruning, so the column shows each function's share of the
	 * functions retained in this table -- not of the whole recording. See
	 * the Cycles % description in perf-c2c.txt.
	 */
	c2c_ext.total_cycles = c2c_ext__total_cycles();
}

/*
 * Release all per-visit function-view state. Keep the hists object itself
 * initialized so its mutex is initialized exactly once across TAB re-entry.
 */
void c2c_function__reset(void)
{
	bool saved_use_callchain = symbol_conf.use_callchain;

	/*
	 * Function-view entries never carry callchains. Keep their generic
	 * destructor independent of the caller's current callchain setting.
	 */
	symbol_conf.use_callchain = false;
	hists__delete_all_entries(&c2c_ext.function_hists.hists);
	if (c2c_ext.function_hists.list.fields.next)
		perf_hpp__reset_output_field(&c2c_ext.function_hists.list);

	memset(&c2c_ext.function_hists.stats, 0,
	       sizeof(c2c_ext.function_hists.stats));
	c2c_ext.total_cycles = 0;
	c2c_ext.cl_hists = NULL;
	c2c_ext.cl_sort = NULL;
	c2c_ext.symbol_full = false;
	symbol_conf.use_callchain = saved_use_callchain;
}

static bool c2c_function__has_iaddr(const char *cl_sort)
{
	const char *field = cl_sort;

	while (field && *field) {
		const char *end = strchr(field, ',');
		size_t len = end ? (size_t)(end - field) : strlen(field);

		if (len == sizeof("iaddr") - 1 && !strncmp(field, "iaddr", len))
			return true;
		field = end ? end + 1 : NULL;
	}
	return false;
}

/*
 * Build the three-level function view in a single pass over the cacheline
 * entries:
 *   L1: read-side functions (aggregated across all their code addresses)
 *   L2: writing functions contending with each level-1 function
 *   L3: shared cachelines for each function pair
 */
int c2c_function__build(struct c2c_hists *cl_hists, const char *cl_sort,
			bool symbol_full, struct hists **hists)
{
	static const char output_fields[] =
		"cycles_percent,total_stores,symbol_view";
	static bool hists_initialized;
	struct rb_node *nd_cl;
	bool saved_use_callchain;
	int ret;

	if (!hists)
		return -EINVAL;
	*hists = NULL;

	if (!cl_hists || !cl_sort)
		return -EINVAL;
	if (!c2c_function__has_iaddr(cl_sort))
		return -EOPNOTSUPP;

	saved_use_callchain = symbol_conf.use_callchain;
	symbol_conf.use_callchain = false;
	c2c_function__reset();

	c2c_ext.cl_hists = cl_hists;
	c2c_ext.cl_sort = cl_sort;
	c2c_ext.symbol_full = symbol_full;

	/*
	 * __hists__init() (called by c2c_function_hists__init()) also
	 * mutex_init()s the hists lock, so only run it once for this static
	 * hists; on re-entry just re-parse the columns via reinit().
	 */
	if (!hists_initialized) {
		ret = c2c_function_hists__init(&c2c_ext.function_hists,
					       "symbol_view", 2, NULL);
		hists_initialized = true;
	} else {
		ret = c2c_function_hists__reinit(&c2c_ext.function_hists,
						 /*output=*/NULL, "symbol_view", NULL);
	}
	if (ret)
		goto out_reset;

	nd_cl = rb_first_cached(&c2c_ext.cl_hists->hists.entries);

	/* An empty C2C report yields an empty (but valid) function view. */
	for (; nd_cl; nd_cl = rb_next(nd_cl)) {
		struct hist_entry *he_cl = rb_entry(nd_cl, struct hist_entry, rb_node);
		struct c2c_hist_entry *cacheline_he = container_of(he_cl,
								   struct c2c_hist_entry, he);
		struct thread *synthetic_thread = he_cl->thread;

		/*
		 * Detail hists are finalized only for cachelines retained by the
		 * top-level C2C filter. Among those, include any line with sharing
		 * activity, not just HITM.
		 */
		if (he_cl->filtered ||
		    (c2c_hitm_count(&cacheline_he->stats) == 0 &&
		     cacheline_he->stats.tot_peer == 0 &&
		     cacheline_he->stats.store == 0 &&
		     cacheline_he->stats.load == 0) ||
		    !cacheline_he->hists ||
		    RB_EMPTY_ROOT(&cacheline_he->hists->hists.entries.rb_root) ||
		    !he_cl->mem_info || !synthetic_thread)
			continue;

		ret = c2c_function__process_cl(cacheline_he, synthetic_thread);
		if (ret)
			goto out_err;
	}

	ret = c2c_function_hists__reinit(&c2c_ext.function_hists, output_fields,
					 "cycles_percent", NULL);
	if (ret)
		goto out_err;

	hists__collapse_resort(&c2c_ext.function_hists.hists, NULL);
	hists__output_resort(&c2c_ext.function_hists.hists, NULL);

	c2c_function__finalize();

	*hists = &c2c_ext.function_hists.hists;
	symbol_conf.use_callchain = saved_use_callchain;
	return 0;

out_err:
	/*
	 * On error, migrate any entries still in entries_in to entries and
	 * delete them, so a later rebuild does not strand them (the top-level
	 * __hists__init() memset would otherwise lose the pointers).
	 */
	hists__collapse_resort(&c2c_ext.function_hists.hists, NULL);
	hists__output_resort(&c2c_ext.function_hists.hists, NULL);
out_reset:
	c2c_function__reset();
	symbol_conf.use_callchain = saved_use_callchain;
	return ret;
}
