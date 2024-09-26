// SPDX-License-Identifier: GPL-2.0
/*
 * builtin-diff.c
 *
 * Builtin diff command: Analyze two perf.data input files, look up and read
 * DSOs and symbol information, sort them and produce a diff.
 */
#include "builtin.h"

#include "util/debug.h"
#include "util/event.h"
#include "util/hist.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/session.h"
#include "util/tool.h"
#include "util/sort.h"
#include "util/srcline.h"
#include "util/symbol.h"
#include "util/data.h"
#include "util/config.h"
#include "util/time-utils.h"
#include "util/annotate.h"
#include "util/map.h"
#include "util/spark.h"
#include "util/block-info.h"
#include "util/stream.h"
#include "util/util.h"
#include <linux/err.h>
#include <linux/zalloc.h>
#include <subcmd/pager.h>
#include <subcmd/parse-options.h>

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <math.h>

struct perf_diff {
	struct perf_tool		 tool;
	const char			*time_str;
	struct perf_time_interval	*ptime_range;
	int				 range_size;
	int				 range_num;
	bool				 has_br_stack;
	bool				 stream;
};

/* Diff command specific HPP columns. */
enum {
	PERF_HPP_DIFF__BASELINE,
	PERF_HPP_DIFF__PERIOD,
	PERF_HPP_DIFF__PERIOD_BASELINE,
	PERF_HPP_DIFF__DELTA,
	PERF_HPP_DIFF__RATIO,
	PERF_HPP_DIFF__WEIGHTED_DIFF,
	PERF_HPP_DIFF__FORMULA,
	PERF_HPP_DIFF__DELTA_ABS,
	PERF_HPP_DIFF__CYCLES,
	PERF_HPP_DIFF__CYCLES_HIST,

	PERF_HPP_DIFF__MAX_INDEX
};

struct diff_hpp_fmt {
	struct perf_hpp_fmt	 fmt;
	int			 idx;
	char			*header;
	int			 header_width;
};

struct data__file {
	struct perf_session	*session;
	struct perf_data	 data;
	int			 idx;
	struct hists		*hists;
	struct evlist_streams	*evlist_streams;
	struct diff_hpp_fmt	 fmt[PERF_HPP_DIFF__MAX_INDEX];
};

static struct data__file *data__files;
static int data__files_cnt;

#define data__for_each_file_start(i, d, s)	\
	for (i = s, d = &data__files[s];	\
	     i < data__files_cnt;		\
	     i++, d = &data__files[i])

#define data__for_each_file(i, d) data__for_each_file_start(i, d, 0)
#define data__for_each_file_new(i, d) data__for_each_file_start(i, d, 1)

static bool force;
static bool show_period;
static bool show_formula;
static bool show_baseline_only;
static bool cycles_hist;
static unsigned int sort_compute = 1;

static s64 compute_wdiff_w1;
static s64 compute_wdiff_w2;

static const char		*cpu_list;
static DECLARE_BITMAP(cpu_bitmap, MAX_NR_CPUS);

enum {
	COMPUTE_DELTA,
	COMPUTE_RATIO,
	COMPUTE_WEIGHTED_DIFF,
	COMPUTE_DELTA_ABS,
	COMPUTE_CYCLES,
	COMPUTE_MAX,
	COMPUTE_STREAM,	/* After COMPUTE_MAX to avoid use current compute arrays */
};

const char *compute_names[COMPUTE_MAX] = {
	[COMPUTE_DELTA] = "delta",
	[COMPUTE_DELTA_ABS] = "delta-abs",
	[COMPUTE_RATIO] = "ratio",
	[COMPUTE_WEIGHTED_DIFF] = "wdiff",
	[COMPUTE_CYCLES] = "cycles",
};

static int compute = COMPUTE_DELTA_ABS;

static int compute_2_hpp[COMPUTE_MAX] = {
	[COMPUTE_DELTA]		= PERF_HPP_DIFF__DELTA,
	[COMPUTE_DELTA_ABS]	= PERF_HPP_DIFF__DELTA_ABS,
	[COMPUTE_RATIO]		= PERF_HPP_DIFF__RATIO,
	[COMPUTE_WEIGHTED_DIFF]	= PERF_HPP_DIFF__WEIGHTED_DIFF,
	[COMPUTE_CYCLES]	= PERF_HPP_DIFF__CYCLES,
};

#define MAX_COL_WIDTH 70

static struct header_column {
	const char *name;
	int width;
} columns[PERF_HPP_DIFF__MAX_INDEX] = {
	[PERF_HPP_DIFF__BASELINE] = {
		.name  = "Baseline",
	},
	[PERF_HPP_DIFF__PERIOD] = {
		.name  = "Period",
		.width = 14,
	},
	[PERF_HPP_DIFF__PERIOD_BASELINE] = {
		.name  = "Base period",
		.width = 14,
	},
	[PERF_HPP_DIFF__DELTA] = {
		.name  = "Delta",
		.width = 7,
	},
	[PERF_HPP_DIFF__DELTA_ABS] = {
		.name  = "Delta Abs",
		.width = 7,
	},
	[PERF_HPP_DIFF__RATIO] = {
		.name  = "Ratio",
		.width = 14,
	},
	[PERF_HPP_DIFF__WEIGHTED_DIFF] = {
		.name  = "Weighted diff",
		.width = 14,
	},
	[PERF_HPP_DIFF__FORMULA] = {
		.name  = "Formula",
		.width = MAX_COL_WIDTH,
	},
	[PERF_HPP_DIFF__CYCLES] = {
		.name  = "[Program Block Range] Cycles Diff",
		.width = 70,
	},
	[PERF_HPP_DIFF__CYCLES_HIST] = {
		.name  = "stddev/Hist",
		.width = NUM_SPARKS + 9,
	}
};

static int setup_compute_opt_wdiff(char *opt)
{
	char *w1_str = opt;
	char *w2_str;

	int ret = -EINVAL;

	if (!opt)
		goto out;

	w2_str = strchr(opt, ',');
	if (!w2_str)
		goto out;

	*w2_str++ = 0x0;
	if (!*w2_str)
		goto out;

	compute_wdiff_w1 = strtol(w1_str, NULL, 10);
	compute_wdiff_w2 = strtol(w2_str, NULL, 10);

	if (!compute_wdiff_w1 || !compute_wdiff_w2)
		goto out;

	pr_debug("compute wdiff w1(%" PRId64 ") w2(%" PRId64 ")\n",
		  compute_wdiff_w1, compute_wdiff_w2);

	ret = 0;

 out:
	if (ret)
		pr_err("Failed: wrong weight data, use 'wdiff:w1,w2'\n");

	return ret;
}

static int setup_compute_opt(char *opt)
{
	if (compute == COMPUTE_WEIGHTED_DIFF)
		return setup_compute_opt_wdiff(opt);

	if (opt) {
		pr_err("Failed: extra option specified '%s'", opt);
		return -EINVAL;
	}

	return 0;
}

static int setup_compute(const struct option *opt, const char *str,
			 int unset __maybe_unused)
{
	int *cp = (int *) opt->value;
	char *cstr = (char *) str;
	char buf[50];
	unsigned i;
	char *option;

	if (!str) {
		*cp = COMPUTE_DELTA;
		return 0;
	}

	option = strchr(str, ':');
	if (option) {
		unsigned len = option++ - str;

		/*
		 * The str data are not writeable, so we need
		 * to use another buffer.
		 */

		/* No option value is longer. */
		if (len >= sizeof(buf))
			return -EINVAL;

		strncpy(buf, str, len);
		buf[len] = 0x0;
		cstr = buf;
	}

	for (i = 0; i < COMPUTE_MAX; i++)
		if (!strcmp(cstr, compute_names[i])) {
			*cp = i;
			return setup_compute_opt(option);
		}

	pr_err("Failed: '%s' is not computation method "
	       "(use 'delta','ratio' or 'wdiff')\n", str);
	return -EINVAL;
}

static double period_percent(struct hist_entry *he, u64 period)
{
	u64 total = hists__total_period(he->hists);

	return (period * 100.0) / total;
}

static double compute_delta(struct hist_entry *he, struct hist_entry *pair)
{
	double old_percent = period_percent(he, he->stat.period);
	double new_percent = period_percent(pair, pair->stat.period);

	pair->diff.period_ratio_delta = new_percent - old_percent;
	pair->diff.computed = true;
	return pair->diff.period_ratio_delta;
}

static double compute_ratio(struct hist_entry *he, struct hist_entry *pair)
{
	double old_period = he->stat.period ?: 1;
	double new_period = pair->stat.period;

	pair->diff.computed = true;
	pair->diff.period_ratio = new_period / old_period;
	return pair->diff.period_ratio;
}

static s64 compute_wdiff(struct hist_entry *he, struct hist_entry *pair)
{
	u64 old_period = he->stat.period;
	u64 new_period = pair->stat.period;

	pair->diff.computed = true;
	pair->diff.wdiff = new_period * compute_wdiff_w2 -
			   old_period * compute_wdiff_w1;

	return pair->diff.wdiff;
}

static int formula_delta(struct hist_entry *he, struct hist_entry *pair,
			 char *buf, size_t size)
{
	u64 he_total = he->hists->stats.total_period;
	u64 pair_total = pair->hists->stats.total_period;

	if (symbol_conf.filter_relative) {
		he_total = he->hists->stats.total_non_filtered_period;
		pair_total = pair->hists->stats.total_non_filtered_period;
	}
	return scnprintf(buf, size,
			 "(%" PRIu64 " * 100 / %" PRIu64 ") - "
			 "(%" PRIu64 " * 100 / %" PRIu64 ")",
			 pair->stat.period, pair_total,
			 he->stat.period, he_total);
}

static int formula_ratio(struct hist_entry *he, struct hist_entry *pair,
			 char *buf, size_t size)
{
	double old_period = he->stat.period;
	double new_period = pair->stat.period;

	return scnprintf(buf, size, "%.0F / %.0F", new_period, old_period);
}

static int formula_wdiff(struct hist_entry *he, struct hist_entry *pair,
			 char *buf, size_t size)
{
	u64 old_period = he->stat.period;
	u64 new_period = pair->stat.period;

	return scnprintf(buf, size,
		  "(%" PRIu64 " * " "%" PRId64 ") - (%" PRIu64 " * " "%" PRId64 ")",
		  new_period, compute_wdiff_w2, old_period, compute_wdiff_w1);
}

static int formula_fprintf(struct hist_entry *he, struct hist_entry *pair,
			   char *buf, size_t size)
{
	switch (compute) {
	case COMPUTE_DELTA:
	case COMPUTE_DELTA_ABS:
		return formula_delta(he, pair, buf, size);
	case COMPUTE_RATIO:
		return formula_ratio(he, pair, buf, size);
	case COMPUTE_WEIGHTED_DIFF:
		return formula_wdiff(he, pair, buf, size);
	default:
		BUG_ON(1);
	}

	return -1;
}

static void *block_hist_zalloc(size_t size)
{
	struct block_hist *bh;

	bh = zalloc(size + sizeof(*bh));
	if (!bh)
		return NULL;

	return &bh->he;
}

static void block_hist_free(void *he)
{
	struct block_hist *bh;

	bh = container_of(he, struct block_hist, he);
	hists__delete_entries(&bh->block_hists);
	free(bh);
}

struct hist_entry_ops block_hist_ops = {
	.new    = block_hist_zalloc,
	.free   = block_hist_free,
};

static int diff__process_sample_event(const struct perf_tool *tool,
				      union perf_event *event,
				      struct perf_sample *sample,
				      struct evsel *evsel,
				      struct machine *machine)
{
	struct perf_diff *pdiff = container_of(tool, struct perf_diff, tool);
	struct addr_location al;
	struct hists *hists = evsel__hists(evsel);
	struct hist_entry_iter iter = {
		.evsel	= evsel,
		.sample	= sample,
		.ops	= &hist_iter_normal,
	};
	int ret = -1;

	if (perf_time__ranges_skip_sample(pdiff->ptime_range, pdiff->range_num,
					  sample->time)) {
		return 0;
	}

	addr_location__init(&al);
	if (machine__resolve(machine, &al, sample) < 0) {
		pr_warning("problem processing %d event, skipping it.\n",
			   event->header.type);
		ret = -1;
		goto out;
	}

	if (cpu_list && !test_bit(sample->cpu, cpu_bitmap)) {
		ret = 0;
		goto out;
	}

	switch (compute) {
	case COMPUTE_CYCLES:
		if (!hists__add_entry_ops(hists, &block_hist_ops, &al, NULL,
					  NULL, NULL, NULL, sample, true)) {
			pr_warning("problem incrementing symbol period, "
				   "skipping event\n");
			goto out;
		}

		hist__account_cycles(sample->branch_stack, &al, sample,
				     false, NULL, evsel);
		break;

	case COMPUTE_STREAM:
		if (hist_entry_iter__add(&iter, &al, PERF_MAX_STACK_DEPTH,
					 NULL)) {
			pr_debug("problem adding hist entry, skipping event\n");
			goto out;
		}
		break;

	default:
		if (!hists__add_entry(hists, &al, NULL, NULL, NULL, NULL, sample,
				      true)) {
			pr_warning("problem incrementing symbol period, "
				   "skipping event\n");
			goto out;
		}
	}

	/*
	 * The total_period is updated here before going to the output
	 * tree since normally only the baseline hists will call
	 * hists__output_resort() and precompute needs the total
	 * period in order to sort entries by percentage delta.
	 */
	hists->stats.total_period += sample->period;
	if (!al.filtered)
		hists->stats.total_non_filtered_period += sample->period;
	ret = 0;
out:
	addr_location__exit(&al);
	return ret;
}

static struct perf_diff pdiff;

static struct evsel *evsel_match(struct evsel *evsel,
				      struct evlist *evlist)
{
	struct evsel *e;

	evlist__for_each_entry(evlist, e) {
		if (evsel__match2(evsel, e))
			return e;
	}

	return NULL;
}

static void evlist__collapse_resort(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		struct hists *hists = evsel__hists(evsel);

		hists__collapse_resort(hists, NULL);
	}
}

static struct data__file *fmt_to_data_file(struct perf_hpp_fmt *fmt)
{
	struct diff_hpp_fmt *dfmt = container_of(fmt, struct diff_hpp_fmt, fmt);
	void *ptr = dfmt - dfmt->idx;
	struct data__file *d = container_of(ptr, struct data__file, fmt);

	return d;
}

static struct hist_entry*
get_pair_data(struct hist_entry *he, struct data__file *d)
{
	if (hist_entry__has_pairs(he)) {
		struct hist_entry *pair;

		list_for_each_entry(pair, &he->pairs.head, pairs.node)
			if (pair->hists == d->hists)
				return pair;
	}

	return NULL;
}

static struct hist_entry*
get_pair_fmt(struct hist_entry *he, struct diff_hpp_fmt *dfmt)
{
	struct data__file *d = fmt_to_data_file(&dfmt->fmt);

	return get_pair_data(he, d);
}

static void hists__baseline_only(struct hists *hists)
{
	struct rb_root_cached *root;
	struct rb_node *next;

	if (hists__has(hists, need_collapse))
		root = &hists->entries_collapsed;
	else
		root = hists->entries_in;

	next = rb_first_cached(root);
	while (next != NULL) {
		struct hist_entry *he = rb_entry(next, struct hist_entry, rb_node_in);

		next = rb_next(&he->rb_node_in);
		if (!hist_entry__next_pair(he)) {
			rb_erase_cached(&he->rb_node_in, root);
			hist_entry__delete(he);
		}
	}
}

static int64_t block_cycles_diff_cmp(struct hist_entry *left,
				     struct hist_entry *right)
{
	bool pairs_left  = hist_entry__has_pairs(left);
	bool pairs_right = hist_entry__has_pairs(right);
	s64 l, r;

	if (!pairs_left && !pairs_right)
		return 0;

	l = llabs(left->diff.cycles);
	r = llabs(right->diff.cycles);
	return r - l;
}

static int64_t block_sort(struct perf_hpp_fmt *fmt __maybe_unused,
			  struct hist_entry *left, struct hist_entry *right)
{
	return block_cycles_diff_cmp(right, left);
}

static void init_block_hist(struct block_hist *bh)
{
	__hists__init(&bh->block_hists, &bh->block_list);
	perf_hpp_list__init(&bh->block_list);

	INIT_LIST_HEAD(&bh->block_fmt.list);
	INIT_LIST_HEAD(&bh->block_fmt.sort_list);
	bh->block_fmt.cmp = block_info__cmp;
	bh->block_fmt.sort = block_sort;
	perf_hpp_list__register_sort_field(&bh->block_list,
					   &bh->block_fmt);
	bh->valid = true;
}

static struct hist_entry *get_block_pair(struct hist_entry *he,
					 struct hists *hists_pair)
{
	struct rb_root_cached *root = hists_pair->entries_in;
	struct rb_node *next = rb_first_cached(root);
	int64_t cmp;

	while (next != NULL) {
		struct hist_entry *he_pair = rb_entry(next, struct hist_entry,
						      rb_node_in);

		next = rb_next(&he_pair->rb_node_in);

		cmp = __block_info__cmp(he_pair, he);
		if (!cmp)
			return he_pair;
	}

	return NULL;
}

static void init_spark_values(unsigned long *svals, int num)
{
	for (int i = 0; i < num; i++)
		svals[i] = 0;
}

static void update_spark_value(unsigned long *svals, int num,
			       struct stats *stats, u64 val)
{
	int n = stats->n;

	if (n < num)
		svals[n] = val;
}

static void compute_cycles_diff(struct hist_entry *he,
				struct hist_entry *pair)
{
	pair->diff.computed = true;
	if (pair->block_info->num && he->block_info->num) {
		pair->diff.cycles =
			pair->block_info->cycles_aggr / pair->block_info->num_aggr -
			he->block_info->cycles_aggr / he->block_info->num_aggr;

		if (!cycles_hist)
			return;

		init_stats(&pair->diff.stats);
		init_spark_values(pair->diff.svals, NUM_SPARKS);

		for (int i = 0; i < pair->block_info->num; i++) {
			u64 val;

			if (i >= he->block_info->num || i >= NUM_SPARKS)
				break;

			val = llabs(pair->block_info->cycles_spark[i] -
				     he->block_info->cycles_spark[i]);

			update_spark_value(pair->diff.svals, NUM_SPARKS,
					   &pair->diff.stats, val);
			update_stats(&pair->diff.stats, val);
		}
	}
}

static void block_hists_match(struct hists *hists_base,
			      struct hists *hists_pair)
{
	struct rb_root_cached *root = hists_base->entries_in;
	struct rb_node *next = rb_first_cached(root);

	while (next != NULL) {
		struct hist_entry *he = rb_entry(next, struct hist_entry,
						 rb_node_in);
		struct hist_entry *pair = get_block_pair(he, hists_pair);

		next = rb_next(&he->rb_node_in);

		if (pair) {
			hist_entry__add_pair(pair, he);
			compute_cycles_diff(he, pair);
		}
	}
}

static void hists__precompute(struct hists *hists)
{
	struct rb_root_cached *root;
	struct rb_node *next;

	if (hists__has(hists, need_collapse))
		root = &hists->entries_collapsed;
	else
		root = hists->entries_in;

	next = rb_first_cached(root);
	while (next != NULL) {
		struct block_hist *bh, *pair_bh;
		struct hist_entry *he, *pair;
		struct data__file *d;
		int i;

		he   = rb_entry(next, struct hist_entry, rb_node_in);
		next = rb_next(&he->rb_node_in);

		if (compute == COMPUTE_CYCLES) {
			bh = container_of(he, struct block_hist, he);
			init_block_hist(bh);
			block_info__process_sym(he, bh, NULL, 0, 0);
		}

		data__for_each_file_new(i, d) {
			pair = get_pair_data(he, d);
			if (!pair)
				continue;

			switch (compute) {
			case COMPUTE_DELTA:
			case COMPUTE_DELTA_ABS:
				compute_delta(he, pair);
				break;
			case COMPUTE_RATIO:
				compute_ratio(he, pair);
				break;
			case COMPUTE_WEIGHTED_DIFF:
				compute_wdiff(he, pair);
				break;
			case COMPUTE_CYCLES:
				pair_bh = container_of(pair, struct block_hist,
						       he);
				init_block_hist(pair_bh);
				block_info__process_sym(pair, pair_bh, NULL, 0, 0);

				bh = container_of(he, struct block_hist, he);

				if (bh->valid && pair_bh->valid) {
					block_hists_match(&bh->block_hists,
							  &pair_bh->block_hists);
					hists__output_resort(&pair_bh->block_hists,
							     NULL);
				}
				break;
			default:
				BUG_ON(1);
			}
		}
	}
}

static int64_t cmp_doubles(double l, double r)
{
	if (l > r)
		return -1;
	else if (l < r)
		return 1;
	else
		return 0;
}

static int64_t
__hist_entry__cmp_compute(struct hist_entry *left, struct hist_entry *right,
			int c)
{
	switch (c) {
	case COMPUTE_DELTA:
	{
		double l = left->diff.period_ratio_delta;
		double r = right->diff.period_ratio_delta;

		return cmp_doubles(l, r);
	}
	case COMPUTE_DELTA_ABS:
	{
		double l = fabs(left->diff.period_ratio_delta);
		double r = fabs(right->diff.period_ratio_delta);

		return cmp_doubles(l, r);
	}
	case COMPUTE_RATIO:
	{
		double l = left->diff.period_ratio;
		double r = right->diff.period_ratio;

		return cmp_doubles(l, r);
	}
	case COMPUTE_WEIGHTED_DIFF:
	{
		s64 l = left->diff.wdiff;
		s64 r = right->diff.wdiff;

		return r - l;
	}
	default:
		BUG_ON(1);
	}

	return 0;
}

static int64_t
hist_entry__cmp_compute(struct hist_entry *left, struct hist_entry *right,
			int c, int sort_idx)
{
	bool pairs_left  = hist_entry__has_pairs(left);
	bool pairs_right = hist_entry__has_pairs(right);
	struct hist_entry *p_right, *p_left;

	if (!pairs_left && !pairs_right)
		return 0;

	if (!pairs_left || !pairs_right)
		return pairs_left ? -1 : 1;

	p_left  = get_pair_data(left,  &data__files[sort_idx]);
	p_right = get_pair_data(right, &data__files[sort_idx]);

	if (!p_left && !p_right)
		return 0;

	if (!p_left || !p_right)
		return p_left ? -1 : 1;

	/*
	 * We have 2 entries of same kind, let's
	 * make the data comparison.
	 */
	return __hist_entry__cmp_compute(p_left, p_right, c);
}

static int64_t
hist_entry__cmp_compute_idx(struct hist_entry *left, struct hist_entry *right,
			    int c, int sort_idx)
{
	struct hist_entry *p_right, *p_left;

	p_left  = get_pair_data(left,  &data__files[sort_idx]);
	p_right = get_pair_data(right, &data__files[sort_idx]);

	if (!p_left && !p_right)
		return 0;

	if (!p_left || !p_right)
		return p_left ? -1 : 1;

	if (c != COMPUTE_DELTA && c != COMPUTE_DELTA_ABS) {
		/*
		 * The delta can be computed without the baseline, but
		 * others are not.  Put those entries which have no
		 * values below.
		 */
		if (left->dummy && right->dummy)
			return 0;

		if (left->dummy || right->dummy)
			return left->dummy ? 1 : -1;
	}

	return __hist_entry__cmp_compute(p_left, p_right, c);
}

static int64_t
hist_entry__cmp_nop(struct perf_hpp_fmt *fmt __maybe_unused,
		    struct hist_entry *left __maybe_unused,
		    struct hist_entry *right __maybe_unused)
{
	return 0;
}

static int64_t
hist_entry__cmp_baseline(struct perf_hpp_fmt *fmt __maybe_unused,
			 struct hist_entry *left, struct hist_entry *right)
{
	if (left->stat.period == right->stat.period)
		return 0;
	return left->stat.period > right->stat.period ? 1 : -1;
}

static int64_t
hist_entry__cmp_delta(struct perf_hpp_fmt *fmt,
		      struct hist_entry *left, struct hist_entry *right)
{
	struct data__file *d = fmt_to_data_file(fmt);

	return hist_entry__cmp_compute(right, left, COMPUTE_DELTA, d->idx);
}

static int64_t
hist_entry__cmp_delta_abs(struct perf_hpp_fmt *fmt,
		      struct hist_entry *left, struct hist_entry *right)
{
	struct data__file *d = fmt_to_data_file(fmt);

	return hist_entry__cmp_compute(right, left, COMPUTE_DELTA_ABS, d->idx);
}

static int64_t
hist_entry__cmp_ratio(struct perf_hpp_fmt *fmt,
		      struct hist_entry *left, struct hist_entry *right)
{
	struct data__file *d = fmt_to_data_file(fmt);

	return hist_entry__cmp_compute(right, left, COMPUTE_RATIO, d->idx);
}

static int64_t
hist_entry__cmp_wdiff(struct perf_hpp_fmt *fmt,
		      struct hist_entry *left, struct hist_entry *right)
{
	struct data__file *d = fmt_to_data_file(fmt);

	return hist_entry__cmp_compute(right, left, COMPUTE_WEIGHTED_DIFF, d->idx);
}

static int64_t
hist_entry__cmp_delta_idx(struct perf_hpp_fmt *fmt __maybe_unused,
			  struct hist_entry *left, struct hist_entry *right)
{
	return hist_entry__cmp_compute_idx(right, left, COMPUTE_DELTA,
					   sort_compute);
}

static int64_t
hist_entry__cmp_delta_abs_idx(struct perf_hpp_fmt *fmt __maybe_unused,
			      struct hist_entry *left, struct hist_entry *right)
{
	return hist_entry__cmp_compute_idx(right, left, COMPUTE_DELTA_ABS,
					   sort_compute);
}

static int64_t
hist_entry__cmp_ratio_idx(struct perf_hpp_fmt *fmt __maybe_unused,
			  struct hist_entry *left, struct hist_entry *right)
{
	return hist_entry__cmp_compute_idx(right, left, COMPUTE_RATIO,
					   sort_compute);
}

static int64_t
hist_entry__cmp_wdiff_idx(struct perf_hpp_fmt *fmt __maybe_unused,
			  struct hist_entry *left, struct hist_entry *right)
{
	return hist_entry__cmp_compute_idx(right, left, COMPUTE_WEIGHTED_DIFF,
					   sort_compute);
}

static void hists__process(struct hists *hists)
{
	if (show_baseline_only)
		hists__baseline_only(hists);

	hists__precompute(hists);
	hists__output_resort(hists, NULL);

	if (compute == COMPUTE_CYCLES)
		symbol_conf.report_block = true;

	hists__fprintf(hists, !quiet, 0, 0, 0, stdout,
		       !symbol_conf.use_callchain);
}

static void data__fprintf(void)
{
	struct data__file *d;
	int i;

	fprintf(stdout, "# Data files:\n");

	data__for_each_file(i, d)
		fprintf(stdout, "#  [%d] %s %s\n",
			d->idx, d->data.path,
			!d->idx ? "(Baseline)" : "");

	fprintf(stdout, "#\n");
}

static void data_process(void)
{
	struct evlist *evlist_base = data__files[0].session->evlist;
	struct evsel *evsel_base;
	bool first = true;

	evlist__for_each_entry(evlist_base, evsel_base) {
		struct hists *hists_base = evsel__hists(evsel_base);
		struct data__file *d;
		int i;

		data__for_each_file_new(i, d) {
			struct evlist *evlist = d->session->evlist;
			struct evsel *evsel;
			struct hists *hists;

			evsel = evsel_match(evsel_base, evlist);
			if (!evsel)
				continue;

			hists = evsel__hists(evsel);
			d->hists = hists;

			hists__match(hists_base, hists);

			if (!show_baseline_only)
				hists__link(hists_base, hists);
		}

		if (!quiet) {
			fprintf(stdout, "%s# Event '%s'\n#\n", first ? "" : "\n",
				evsel__name(evsel_base));
		}

		first = false;

		if (verbose > 0 || ((data__files_cnt > 2) && !quiet))
			data__fprintf();

		/* Don't sort callchain for perf diff */
		evsel__reset_sample_bit(evsel_base, CALLCHAIN);

		hists__process(hists_base);
	}
}

static int process_base_stream(struct data__file *data_base,
			       struct data__file *data_pair,
			       const char *title __maybe_unused)
{
	struct evlist *evlist_base = data_base->session->evlist;
	struct evlist *evlist_pair = data_pair->session->evlist;
	struct evsel *evsel_base, *evsel_pair;
	struct evsel_streams *es_base, *es_pair;

	evlist__for_each_entry(evlist_base, evsel_base) {
		evsel_pair = evsel_match(evsel_base, evlist_pair);
		if (!evsel_pair)
			continue;

		es_base = evsel_streams__entry(data_base->evlist_streams,
					       evsel_base->core.idx);
		if (!es_base)
			return -1;

		es_pair = evsel_streams__entry(data_pair->evlist_streams,
					       evsel_pair->core.idx);
		if (!es_pair)
			return -1;

		evsel_streams__match(es_base, es_pair);
		evsel_streams__report(es_base, es_pair);
	}

	return 0;
}

static void stream_process(void)
{
	/*
	 * Stream comparison only supports two data files.
	 * perf.data.old and perf.data. data__files[0] is perf.data.old,
	 * data__files[1] is perf.data.
	 */
	process_base_stream(&data__files[0], &data__files[1],
			    "# Output based on old perf data:\n#\n");
}

static void data__free(struct data__file *d)
{
	int col;

	if (d->evlist_streams)
		evlist_streams__delete(d->evlist_streams);

	for (col = 0; col < PERF_HPP_DIFF__MAX_INDEX; col++) {
		struct diff_hpp_fmt *fmt = &d->fmt[col];

		zfree(&fmt->header);
	}
}

static int abstime_str_dup(char **pstr)
{
	char *str = NULL;

	if (pdiff.time_str && strchr(pdiff.time_str, ':')) {
		str = strdup(pdiff.time_str);
		if (!str)
			return -ENOMEM;
	}

	*pstr = str;
	return 0;
}

static int parse_absolute_time(struct data__file *d, char **pstr)
{
	char *p = *pstr;
	int ret;

	/*
	 * Absolute timestamp for one file has the format: a.b,c.d
	 * For multiple files, the format is: a.b,c.d:a.b,c.d
	 */
	p = strchr(*pstr, ':');
	if (p) {
		if (p == *pstr) {
			pr_err("Invalid time string\n");
			return -EINVAL;
		}

		*p = 0;
		p++;
		if (*p == 0) {
			pr_err("Invalid time string\n");
			return -EINVAL;
		}
	}

	ret = perf_time__parse_for_ranges(*pstr, d->session,
					  &pdiff.ptime_range,
					  &pdiff.range_size,
					  &pdiff.range_num);
	if (ret < 0)
		return ret;

	if (!p || *p == 0)
		*pstr = NULL;
	else
		*pstr = p;

	return ret;
}

static int parse_percent_time(struct data__file *d)
{
	int ret;

	ret = perf_time__parse_for_ranges(pdiff.time_str, d->session,
					  &pdiff.ptime_range,
					  &pdiff.range_size,
					  &pdiff.range_num);
	return ret;
}

static int parse_time_str(struct data__file *d, char *abstime_ostr,
			   char **pabstime_tmp)
{
	int ret = 0;

	if (abstime_ostr)
		ret = parse_absolute_time(d, pabstime_tmp);
	else if (pdiff.time_str)
		ret = parse_percent_time(d);

	return ret;
}

static int check_file_brstack(void)
{
	struct data__file *d;
	bool has_br_stack;
	int i;

	data__for_each_file(i, d) {
		d->session = perf_session__new(&d->data, &pdiff.tool);
		if (IS_ERR(d->session)) {
			pr_err("Failed to open %s\n", d->data.path);
			return PTR_ERR(d->session);
		}

		has_br_stack = perf_header__has_feat(&d->session->header,
						     HEADER_BRANCH_STACK);
		perf_session__delete(d->session);
		if (!has_br_stack)
			return 0;
	}

	/* Set only all files having branch stacks */
	pdiff.has_br_stack = true;
	return 0;
}

static int __cmd_diff(void)
{
	struct data__file *d;
	int ret, i;
	char *abstime_ostr, *abstime_tmp;

	ret = abstime_str_dup(&abstime_ostr);
	if (ret)
		return ret;

	abstime_tmp = abstime_ostr;
	ret = -EINVAL;

	data__for_each_file(i, d) {
		d->session = perf_session__new(&d->data, &pdiff.tool);
		if (IS_ERR(d->session)) {
			ret = PTR_ERR(d->session);
			pr_err("Failed to open %s\n", d->data.path);
			goto out_delete;
		}

		if (pdiff.time_str) {
			ret = parse_time_str(d, abstime_ostr, &abstime_tmp);
			if (ret < 0)
				goto out_delete;
		}

		if (cpu_list) {
			ret = perf_session__cpu_bitmap(d->session, cpu_list,
						       cpu_bitmap);
			if (ret < 0)
				goto out_delete;
		}

		ret = perf_session__process_events(d->session);
		if (ret) {
			pr_err("Failed to process %s\n", d->data.path);
			goto out_delete;
		}

		evlist__collapse_resort(d->session->evlist);

		if (pdiff.ptime_range)
			zfree(&pdiff.ptime_range);

		if (compute == COMPUTE_STREAM) {
			d->evlist_streams = evlist__create_streams(
						d->session->evlist, 5);
			if (!d->evlist_streams) {
				ret = -ENOMEM;
				goto out_delete;
			}
		}
	}

	if (compute == COMPUTE_STREAM)
		stream_process();
	else
		data_process();

 out_delete:
	data__for_each_file(i, d) {
		if (!IS_ERR(d->session))
			perf_session__delete(d->session);
		data__free(d);
	}

	free(data__files);

	if (pdiff.ptime_range)
		zfree(&pdiff.ptime_range);

	if (abstime_ostr)
		free(abstime_ostr);

	return ret;
}

static const char * const diff_usage[] = {
	"perf diff [<options>] [old_file] [new_file]",
	NULL,
};

static const struct option options[] = {
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('q', "quiet", &quiet, "Do not show any warnings or messages"),
	OPT_BOOLEAN('b', "baseline-only", &show_baseline_only,
		    "Show only items with match in baseline"),
	OPT_CALLBACK('c', "compute", &compute,
		     "delta,delta-abs,ratio,wdiff:w1,w2 (default delta-abs),cycles",
		     "Entries differential computation selection",
		     setup_compute),
	OPT_BOOLEAN('p', "period", &show_period,
		    "Show period values."),
	OPT_BOOLEAN('F', "formula", &show_formula,
		    "Show formula."),
	OPT_BOOLEAN(0, "cycles-hist", &cycles_hist,
		    "Show cycles histogram and standard deviation "
		    "- WARNING: use only with -c cycles."),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_STRING(0, "kallsyms", &symbol_conf.kallsyms_name,
		   "file", "kallsyms pathname"),
	OPT_BOOLEAN('m', "modules", &symbol_conf.use_modules,
		    "load module symbols - WARNING: use only with -k and LIVE kernel"),
	OPT_STRING('d', "dsos", &symbol_conf.dso_list_str, "dso[,dso...]",
		   "only consider symbols in these dsos"),
	OPT_STRING('C', "comms", &symbol_conf.comm_list_str, "comm[,comm...]",
		   "only consider symbols in these comms"),
	OPT_STRING('S', "symbols", &symbol_conf.sym_list_str, "symbol[,symbol...]",
		   "only consider these symbols"),
	OPT_STRING('s', "sort", &sort_order, "key[,key2...]",
		   "sort by key(s): pid, comm, dso, symbol, parent, cpu, srcline, ..."
		   " Please refer the man page for the complete list."),
	OPT_STRING_NOEMPTY('t', "field-separator", &symbol_conf.field_sep, "separator",
		   "separator for columns, no spaces will be added between "
		   "columns '.' is reserved."),
	OPT_CALLBACK(0, "symfs", NULL, "directory",
		     "Look for files with symbols relative to this directory",
		     symbol__config_symfs),
	OPT_UINTEGER('o', "order", &sort_compute, "Specify compute sorting."),
	OPT_CALLBACK(0, "percentage", NULL, "relative|absolute",
		     "How to display percentage of filtered entries", parse_filter_percentage),
	OPT_STRING(0, "time", &pdiff.time_str, "str",
		   "Time span (time percent or absolute timestamp)"),
	OPT_STRING(0, "cpu", &cpu_list, "cpu", "list of cpus to profile"),
	OPT_STRING(0, "pid", &symbol_conf.pid_list_str, "pid[,pid...]",
		   "only consider symbols in these pids"),
	OPT_STRING(0, "tid", &symbol_conf.tid_list_str, "tid[,tid...]",
		   "only consider symbols in these tids"),
	OPT_BOOLEAN(0, "stream", &pdiff.stream,
		    "Enable hot streams comparison."),
	OPT_END()
};

static double baseline_percent(struct hist_entry *he)
{
	u64 total = hists__total_period(he->hists);

	return 100.0 * he->stat.period / total;
}

static int hpp__color_baseline(struct perf_hpp_fmt *fmt,
			       struct perf_hpp *hpp, struct hist_entry *he)
{
	struct diff_hpp_fmt *dfmt =
		container_of(fmt, struct diff_hpp_fmt, fmt);
	double percent = baseline_percent(he);
	char pfmt[20] = " ";

	if (!he->dummy) {
		scnprintf(pfmt, 20, "%%%d.2f%%%%", dfmt->header_width - 1);
		return percent_color_snprintf(hpp->buf, hpp->size,
					      pfmt, percent);
	} else
		return scnprintf(hpp->buf, hpp->size, "%*s",
				 dfmt->header_width, pfmt);
}

static int hpp__entry_baseline(struct hist_entry *he, char *buf, size_t size)
{
	double percent = baseline_percent(he);
	const char *fmt = symbol_conf.field_sep ? "%.2f" : "%6.2f%%";
	int ret = 0;

	if (!he->dummy)
		ret = scnprintf(buf, size, fmt, percent);

	return ret;
}

static int cycles_printf(struct hist_entry *he, struct hist_entry *pair,
			 struct perf_hpp *hpp, int width)
{
	struct block_hist *bh = container_of(he, struct block_hist, he);
	struct block_hist *bh_pair = container_of(pair, struct block_hist, he);
	struct hist_entry *block_he;
	struct block_info *bi;
	char buf[128];
	char *start_line, *end_line;

	block_he = hists__get_entry(&bh_pair->block_hists, bh->block_idx);
	if (!block_he) {
		hpp->skip = true;
		return 0;
	}

	/*
	 * Avoid printing the warning "addr2line_init failed for ..."
	 */
	symbol_conf.disable_add2line_warn = true;

	bi = block_he->block_info;

	start_line = map__srcline(he->ms.map, bi->sym->start + bi->start,
				  he->ms.sym);

	end_line = map__srcline(he->ms.map, bi->sym->start + bi->end,
				he->ms.sym);

	if (start_line != SRCLINE_UNKNOWN &&
	    end_line != SRCLINE_UNKNOWN) {
		scnprintf(buf, sizeof(buf), "[%s -> %s] %4ld",
			  start_line, end_line, block_he->diff.cycles);
	} else {
		scnprintf(buf, sizeof(buf), "[%7lx -> %7lx] %4ld",
			  bi->start, bi->end, block_he->diff.cycles);
	}

	zfree_srcline(&start_line);
	zfree_srcline(&end_line);

	return scnprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int __hpp__color_compare(struct perf_hpp_fmt *fmt,
				struct perf_hpp *hpp, struct hist_entry *he,
				int comparison_method)
{
	struct diff_hpp_fmt *dfmt =
		container_of(fmt, struct diff_hpp_fmt, fmt);
	struct hist_entry *pair = get_pair_fmt(he, dfmt);
	double diff;
	s64 wdiff;
	char pfmt[20] = " ";

	if (!pair) {
		if (comparison_method == COMPUTE_CYCLES) {
			struct block_hist *bh;

			bh = container_of(he, struct block_hist, he);
			if (bh->block_idx)
				hpp->skip = true;
		}

		goto no_print;
	}

	switch (comparison_method) {
	case COMPUTE_DELTA:
		if (pair->diff.computed)
			diff = pair->diff.period_ratio_delta;
		else
			diff = compute_delta(he, pair);

		scnprintf(pfmt, 20, "%%%+d.2f%%%%", dfmt->header_width - 1);
		return percent_color_snprintf(hpp->buf, hpp->size,
					pfmt, diff);
	case COMPUTE_RATIO:
		if (he->dummy)
			goto dummy_print;
		if (pair->diff.computed)
			diff = pair->diff.period_ratio;
		else
			diff = compute_ratio(he, pair);

		scnprintf(pfmt, 20, "%%%d.6f", dfmt->header_width);
		return value_color_snprintf(hpp->buf, hpp->size,
					pfmt, diff);
	case COMPUTE_WEIGHTED_DIFF:
		if (he->dummy)
			goto dummy_print;
		if (pair->diff.computed)
			wdiff = pair->diff.wdiff;
		else
			wdiff = compute_wdiff(he, pair);

		scnprintf(pfmt, 20, "%%14ld", dfmt->header_width);
		return color_snprintf(hpp->buf, hpp->size,
				get_percent_color(wdiff),
				pfmt, wdiff);
	case COMPUTE_CYCLES:
		return cycles_printf(he, pair, hpp, dfmt->header_width);
	default:
		BUG_ON(1);
	}
dummy_print:
	return scnprintf(hpp->buf, hpp->size, "%*s",
			dfmt->header_width, "N/A");
no_print:
	return scnprintf(hpp->buf, hpp->size, "%*s",
			dfmt->header_width, pfmt);
}

static int hpp__color_delta(struct perf_hpp_fmt *fmt,
			struct perf_hpp *hpp, struct hist_entry *he)
{
	return __hpp__color_compare(fmt, hpp, he, COMPUTE_DELTA);
}

static int hpp__color_ratio(struct perf_hpp_fmt *fmt,
			struct perf_hpp *hpp, struct hist_entry *he)
{
	return __hpp__color_compare(fmt, hpp, he, COMPUTE_RATIO);
}

static int hpp__color_wdiff(struct perf_hpp_fmt *fmt,
			struct perf_hpp *hpp, struct hist_entry *he)
{
	return __hpp__color_compare(fmt, hpp, he, COMPUTE_WEIGHTED_DIFF);
}

static int hpp__color_cycles(struct perf_hpp_fmt *fmt,
			     struct perf_hpp *hpp, struct hist_entry *he)
{
	return __hpp__color_compare(fmt, hpp, he, COMPUTE_CYCLES);
}

static int all_zero(unsigned long *vals, int len)
{
	int i;

	for (i = 0; i < len; i++)
		if (vals[i] != 0)
			return 0;
	return 1;
}

static int print_cycles_spark(char *bf, int size, unsigned long *svals, u64 n)
{
	int printed;

	if (n <= 1)
		return 0;

	if (n > NUM_SPARKS)
		n = NUM_SPARKS;
	if (all_zero(svals, n))
		return 0;

	printed = print_spark(bf, size, svals, n);
	printed += scnprintf(bf + printed, size - printed, " ");
	return printed;
}

static int hpp__color_cycles_hist(struct perf_hpp_fmt *fmt,
			    struct perf_hpp *hpp, struct hist_entry *he)
{
	struct diff_hpp_fmt *dfmt =
		container_of(fmt, struct diff_hpp_fmt, fmt);
	struct hist_entry *pair = get_pair_fmt(he, dfmt);
	struct block_hist *bh = container_of(he, struct block_hist, he);
	struct block_hist *bh_pair;
	struct hist_entry *block_he;
	char spark[32], buf[128];
	double r;
	int ret, pad;

	if (!pair) {
		if (bh->block_idx)
			hpp->skip = true;

		goto no_print;
	}

	bh_pair = container_of(pair, struct block_hist, he);

	block_he = hists__get_entry(&bh_pair->block_hists, bh->block_idx);
	if (!block_he) {
		hpp->skip = true;
		goto no_print;
	}

	ret = print_cycles_spark(spark, sizeof(spark), block_he->diff.svals,
				 block_he->diff.stats.n);

	r = rel_stddev_stats(stddev_stats(&block_he->diff.stats),
			     avg_stats(&block_he->diff.stats));

	if (ret) {
		/*
		 * Padding spaces if number of sparks less than NUM_SPARKS
		 * otherwise the output is not aligned.
		 */
		pad = NUM_SPARKS - ((ret - 1) / 3);
		scnprintf(buf, sizeof(buf), "%s%5.1f%% %s", "\u00B1", r, spark);
		ret = scnprintf(hpp->buf, hpp->size, "%*s",
				dfmt->header_width, buf);

		if (pad) {
			ret += scnprintf(hpp->buf + ret, hpp->size - ret,
					 "%-*s", pad, " ");
		}

		return ret;
	}

no_print:
	return scnprintf(hpp->buf, hpp->size, "%*s",
			dfmt->header_width, " ");
}

static void
hpp__entry_unpair(struct hist_entry *he, int idx, char *buf, size_t size)
{
	switch (idx) {
	case PERF_HPP_DIFF__PERIOD_BASELINE:
		scnprintf(buf, size, "%" PRIu64, he->stat.period);
		break;

	default:
		break;
	}
}

static void
hpp__entry_pair(struct hist_entry *he, struct hist_entry *pair,
		int idx, char *buf, size_t size)
{
	double diff;
	double ratio;
	s64 wdiff;

	switch (idx) {
	case PERF_HPP_DIFF__DELTA:
	case PERF_HPP_DIFF__DELTA_ABS:
		if (pair->diff.computed)
			diff = pair->diff.period_ratio_delta;
		else
			diff = compute_delta(he, pair);

		scnprintf(buf, size, "%+4.2F%%", diff);
		break;

	case PERF_HPP_DIFF__RATIO:
		/* No point for ratio number if we are dummy.. */
		if (he->dummy) {
			scnprintf(buf, size, "N/A");
			break;
		}

		if (pair->diff.computed)
			ratio = pair->diff.period_ratio;
		else
			ratio = compute_ratio(he, pair);

		if (ratio > 0.0)
			scnprintf(buf, size, "%14.6F", ratio);
		break;

	case PERF_HPP_DIFF__WEIGHTED_DIFF:
		/* No point for wdiff number if we are dummy.. */
		if (he->dummy) {
			scnprintf(buf, size, "N/A");
			break;
		}

		if (pair->diff.computed)
			wdiff = pair->diff.wdiff;
		else
			wdiff = compute_wdiff(he, pair);

		if (wdiff != 0)
			scnprintf(buf, size, "%14ld", wdiff);
		break;

	case PERF_HPP_DIFF__FORMULA:
		formula_fprintf(he, pair, buf, size);
		break;

	case PERF_HPP_DIFF__PERIOD:
		scnprintf(buf, size, "%" PRIu64, pair->stat.period);
		break;

	default:
		BUG_ON(1);
	}
}

static void
__hpp__entry_global(struct hist_entry *he, struct diff_hpp_fmt *dfmt,
		    char *buf, size_t size)
{
	struct hist_entry *pair = get_pair_fmt(he, dfmt);
	int idx = dfmt->idx;

	/* baseline is special */
	if (idx == PERF_HPP_DIFF__BASELINE)
		hpp__entry_baseline(he, buf, size);
	else {
		if (pair)
			hpp__entry_pair(he, pair, idx, buf, size);
		else
			hpp__entry_unpair(he, idx, buf, size);
	}
}

static int hpp__entry_global(struct perf_hpp_fmt *_fmt, struct perf_hpp *hpp,
			     struct hist_entry *he)
{
	struct diff_hpp_fmt *dfmt =
		container_of(_fmt, struct diff_hpp_fmt, fmt);
	char buf[MAX_COL_WIDTH] = " ";

	__hpp__entry_global(he, dfmt, buf, MAX_COL_WIDTH);

	if (symbol_conf.field_sep)
		return scnprintf(hpp->buf, hpp->size, "%s", buf);
	else
		return scnprintf(hpp->buf, hpp->size, "%*s",
				 dfmt->header_width, buf);
}

static int hpp__header(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hists *hists __maybe_unused,
		       int line __maybe_unused,
		       int *span __maybe_unused)
{
	struct diff_hpp_fmt *dfmt =
		container_of(fmt, struct diff_hpp_fmt, fmt);

	BUG_ON(!dfmt->header);
	return scnprintf(hpp->buf, hpp->size, dfmt->header);
}

static int hpp__width(struct perf_hpp_fmt *fmt,
		      struct perf_hpp *hpp __maybe_unused,
		      struct hists *hists __maybe_unused)
{
	struct diff_hpp_fmt *dfmt =
		container_of(fmt, struct diff_hpp_fmt, fmt);

	BUG_ON(dfmt->header_width <= 0);
	return dfmt->header_width;
}

static void init_header(struct data__file *d, struct diff_hpp_fmt *dfmt)
{
#define MAX_HEADER_NAME 100
	char buf_indent[MAX_HEADER_NAME];
	char buf[MAX_HEADER_NAME];
	const char *header = NULL;
	int width = 0;

	BUG_ON(dfmt->idx >= PERF_HPP_DIFF__MAX_INDEX);
	header = columns[dfmt->idx].name;
	width  = columns[dfmt->idx].width;

	/* Only our defined HPP fmts should appear here. */
	BUG_ON(!header);

	if (data__files_cnt > 2)
		scnprintf(buf, MAX_HEADER_NAME, "%s/%d", header, d->idx);

#define NAME (data__files_cnt > 2 ? buf : header)
	dfmt->header_width = width;
	width = (int) strlen(NAME);
	if (dfmt->header_width < width)
		dfmt->header_width = width;

	scnprintf(buf_indent, MAX_HEADER_NAME, "%*s",
		  dfmt->header_width, NAME);

	dfmt->header = strdup(buf_indent);
#undef MAX_HEADER_NAME
#undef NAME
}

static void data__hpp_register(struct data__file *d, int idx)
{
	struct diff_hpp_fmt *dfmt = &d->fmt[idx];
	struct perf_hpp_fmt *fmt = &dfmt->fmt;

	dfmt->idx = idx;

	fmt->header = hpp__header;
	fmt->width  = hpp__width;
	fmt->entry  = hpp__entry_global;
	fmt->cmp    = hist_entry__cmp_nop;
	fmt->collapse = hist_entry__cmp_nop;

	/* TODO more colors */
	switch (idx) {
	case PERF_HPP_DIFF__BASELINE:
		fmt->color = hpp__color_baseline;
		fmt->sort  = hist_entry__cmp_baseline;
		break;
	case PERF_HPP_DIFF__DELTA:
		fmt->color = hpp__color_delta;
		fmt->sort  = hist_entry__cmp_delta;
		break;
	case PERF_HPP_DIFF__RATIO:
		fmt->color = hpp__color_ratio;
		fmt->sort  = hist_entry__cmp_ratio;
		break;
	case PERF_HPP_DIFF__WEIGHTED_DIFF:
		fmt->color = hpp__color_wdiff;
		fmt->sort  = hist_entry__cmp_wdiff;
		break;
	case PERF_HPP_DIFF__DELTA_ABS:
		fmt->color = hpp__color_delta;
		fmt->sort  = hist_entry__cmp_delta_abs;
		break;
	case PERF_HPP_DIFF__CYCLES:
		fmt->color = hpp__color_cycles;
		fmt->sort  = hist_entry__cmp_nop;
		break;
	case PERF_HPP_DIFF__CYCLES_HIST:
		fmt->color = hpp__color_cycles_hist;
		fmt->sort  = hist_entry__cmp_nop;
		break;
	default:
		fmt->sort  = hist_entry__cmp_nop;
		break;
	}

	init_header(d, dfmt);
	perf_hpp__column_register(fmt);
	perf_hpp__register_sort_field(fmt);
}

static int ui_init(void)
{
	struct data__file *d;
	struct perf_hpp_fmt *fmt;
	int i;

	data__for_each_file(i, d) {

		/*
		 * Baseline or compute related columns:
		 *
		 *   PERF_HPP_DIFF__BASELINE
		 *   PERF_HPP_DIFF__DELTA
		 *   PERF_HPP_DIFF__RATIO
		 *   PERF_HPP_DIFF__WEIGHTED_DIFF
		 *   PERF_HPP_DIFF__CYCLES
		 */
		data__hpp_register(d, i ? compute_2_hpp[compute] :
					  PERF_HPP_DIFF__BASELINE);

		if (cycles_hist && i)
			data__hpp_register(d, PERF_HPP_DIFF__CYCLES_HIST);

		/*
		 * And the rest:
		 *
		 * PERF_HPP_DIFF__FORMULA
		 * PERF_HPP_DIFF__PERIOD
		 * PERF_HPP_DIFF__PERIOD_BASELINE
		 */
		if (show_formula && i)
			data__hpp_register(d, PERF_HPP_DIFF__FORMULA);

		if (show_period)
			data__hpp_register(d, i ? PERF_HPP_DIFF__PERIOD :
						  PERF_HPP_DIFF__PERIOD_BASELINE);
	}

	if (!sort_compute)
		return 0;

	/*
	 * Prepend an fmt to sort on columns at 'sort_compute' first.
	 * This fmt is added only to the sort list but not to the
	 * output fields list.
	 *
	 * Note that this column (data) can be compared twice - one
	 * for this 'sort_compute' fmt and another for the normal
	 * diff_hpp_fmt.  But it shouldn't a problem as most entries
	 * will be sorted out by first try or baseline and comparing
	 * is not a costly operation.
	 */
	fmt = zalloc(sizeof(*fmt));
	if (fmt == NULL) {
		pr_err("Memory allocation failed\n");
		return -1;
	}

	fmt->cmp      = hist_entry__cmp_nop;
	fmt->collapse = hist_entry__cmp_nop;

	switch (compute) {
	case COMPUTE_DELTA:
		fmt->sort = hist_entry__cmp_delta_idx;
		break;
	case COMPUTE_RATIO:
		fmt->sort = hist_entry__cmp_ratio_idx;
		break;
	case COMPUTE_WEIGHTED_DIFF:
		fmt->sort = hist_entry__cmp_wdiff_idx;
		break;
	case COMPUTE_DELTA_ABS:
		fmt->sort = hist_entry__cmp_delta_abs_idx;
		break;
	case COMPUTE_CYCLES:
		/*
		 * Should set since 'fmt->sort' is called without
		 * checking valid during sorting
		 */
		fmt->sort = hist_entry__cmp_nop;
		break;
	default:
		BUG_ON(1);
	}

	perf_hpp__prepend_sort_field(fmt);
	return 0;
}

static int data_init(int argc, const char **argv)
{
	struct data__file *d;
	static const char *defaults[] = {
		"perf.data.old",
		"perf.data",
	};
	bool use_default = true;
	int i;

	data__files_cnt = 2;

	if (argc) {
		if (argc == 1)
			defaults[1] = argv[0];
		else {
			data__files_cnt = argc;
			use_default = false;
		}
	} else if (perf_guest) {
		defaults[0] = "perf.data.host";
		defaults[1] = "perf.data.guest";
	}

	if (sort_compute >= (unsigned int) data__files_cnt) {
		pr_err("Order option out of limit.\n");
		return -EINVAL;
	}

	data__files = zalloc(sizeof(*data__files) * data__files_cnt);
	if (!data__files)
		return -ENOMEM;

	data__for_each_file(i, d) {
		struct perf_data *data = &d->data;

		data->path  = use_default ? defaults[i] : argv[i];
		data->mode  = PERF_DATA_MODE_READ;
		data->force = force;

		d->idx  = i;
	}

	return 0;
}

static int diff__config(const char *var, const char *value,
			void *cb __maybe_unused)
{
	if (!strcmp(var, "diff.order")) {
		int ret;
		if (perf_config_int(&ret, var, value) < 0)
			return -1;
		sort_compute = ret;
		return 0;
	}
	if (!strcmp(var, "diff.compute")) {
		if (!strcmp(value, "delta")) {
			compute = COMPUTE_DELTA;
		} else if (!strcmp(value, "delta-abs")) {
			compute = COMPUTE_DELTA_ABS;
		} else if (!strcmp(value, "ratio")) {
			compute = COMPUTE_RATIO;
		} else if (!strcmp(value, "wdiff")) {
			compute = COMPUTE_WEIGHTED_DIFF;
		} else {
			pr_err("Invalid compute method: %s\n", value);
			return -1;
		}
	}

	return 0;
}

int cmd_diff(int argc, const char **argv)
{
	int ret = hists__init();

	if (ret < 0)
		return ret;

	perf_tool__init(&pdiff.tool, /*ordered_events=*/true);
	pdiff.tool.sample	= diff__process_sample_event;
	pdiff.tool.mmap	= perf_event__process_mmap;
	pdiff.tool.mmap2	= perf_event__process_mmap2;
	pdiff.tool.comm	= perf_event__process_comm;
	pdiff.tool.exit	= perf_event__process_exit;
	pdiff.tool.fork	= perf_event__process_fork;
	pdiff.tool.lost	= perf_event__process_lost;
	pdiff.tool.namespaces = perf_event__process_namespaces;
	pdiff.tool.cgroup = perf_event__process_cgroup;
	pdiff.tool.ordering_requires_timestamps = true;

	perf_config(diff__config, NULL);

	argc = parse_options(argc, argv, options, diff_usage, 0);

	if (quiet)
		perf_quiet_option();

	if (cycles_hist && (compute != COMPUTE_CYCLES))
		usage_with_options(diff_usage, options);

	if (pdiff.stream)
		compute = COMPUTE_STREAM;

	symbol__annotation_init();

	if (symbol__init(NULL) < 0)
		return -1;

	if (data_init(argc, argv) < 0)
		return -1;

	if (check_file_brstack() < 0)
		return -1;

	if ((compute == COMPUTE_CYCLES || compute == COMPUTE_STREAM)
	    && !pdiff.has_br_stack) {
		return -1;
	}

	if (compute == COMPUTE_STREAM) {
		symbol_conf.show_branchflag_count = true;
		symbol_conf.disable_add2line_warn = true;
		callchain_param.mode = CHAIN_FLAT;
		callchain_param.key = CCKEY_SRCLINE;
		callchain_param.branch_callstack = 1;
		symbol_conf.use_callchain = true;
		callchain_register_param(&callchain_param);
		sort_order = "srcline,symbol,dso";
	} else {
		if (ui_init() < 0)
			return -1;

		sort__mode = SORT_MODE__DIFF;
	}

	if (setup_sorting(NULL) < 0)
		usage_with_options(diff_usage, options);

	setup_pager();

	sort__setup_elide(NULL);

	return __cmd_diff();
}
