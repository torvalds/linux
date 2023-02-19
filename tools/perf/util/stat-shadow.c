// SPDX-License-Identifier: GPL-2.0
#include <math.h>
#include <stdio.h>
#include "evsel.h"
#include "stat.h"
#include "color.h"
#include "debug.h"
#include "pmu.h"
#include "rblist.h"
#include "evlist.h"
#include "expr.h"
#include "metricgroup.h"
#include "cgroup.h"
#include "units.h"
#include <linux/zalloc.h>
#include "iostat.h"
#include "util/hashmap.h"

/*
 * AGGR_GLOBAL: Use CPU 0
 * AGGR_SOCKET: Use first CPU of socket
 * AGGR_DIE: Use first CPU of die
 * AGGR_CORE: Use first CPU of core
 * AGGR_NONE: Use matching CPU
 * AGGR_THREAD: Not supported?
 */

struct runtime_stat rt_stat;
struct stats walltime_nsecs_stats;
struct rusage_stats ru_stats;

struct saved_value {
	struct rb_node rb_node;
	struct evsel *evsel;
	enum stat_type type;
	int ctx;
	int map_idx;  /* cpu or thread map index */
	struct cgroup *cgrp;
	struct stats stats;
	u64 metric_total;
	int metric_other;
};

static int saved_value_cmp(struct rb_node *rb_node, const void *entry)
{
	struct saved_value *a = container_of(rb_node,
					     struct saved_value,
					     rb_node);
	const struct saved_value *b = entry;

	if (a->map_idx != b->map_idx)
		return a->map_idx - b->map_idx;

	/*
	 * Previously the rbtree was used to link generic metrics.
	 * The keys were evsel/cpu. Now the rbtree is extended to support
	 * per-thread shadow stats. For shadow stats case, the keys
	 * are cpu/type/ctx/stat (evsel is NULL). For generic metrics
	 * case, the keys are still evsel/cpu (type/ctx/stat are 0 or NULL).
	 */
	if (a->type != b->type)
		return a->type - b->type;

	if (a->ctx != b->ctx)
		return a->ctx - b->ctx;

	if (a->cgrp != b->cgrp)
		return (char *)a->cgrp < (char *)b->cgrp ? -1 : +1;

	if (a->evsel == b->evsel)
		return 0;
	if ((char *)a->evsel < (char *)b->evsel)
		return -1;
	return +1;
}

static struct rb_node *saved_value_new(struct rblist *rblist __maybe_unused,
				     const void *entry)
{
	struct saved_value *nd = malloc(sizeof(struct saved_value));

	if (!nd)
		return NULL;
	memcpy(nd, entry, sizeof(struct saved_value));
	return &nd->rb_node;
}

static void saved_value_delete(struct rblist *rblist __maybe_unused,
			       struct rb_node *rb_node)
{
	struct saved_value *v;

	BUG_ON(!rb_node);
	v = container_of(rb_node, struct saved_value, rb_node);
	free(v);
}

static struct saved_value *saved_value_lookup(struct evsel *evsel,
					      int map_idx,
					      bool create,
					      enum stat_type type,
					      int ctx,
					      struct runtime_stat *st,
					      struct cgroup *cgrp)
{
	struct rblist *rblist;
	struct rb_node *nd;
	struct saved_value dm = {
		.map_idx = map_idx,
		.evsel = evsel,
		.type = type,
		.ctx = ctx,
		.cgrp = cgrp,
	};

	rblist = &st->value_list;

	/* don't use context info for clock events */
	if (type == STAT_NSECS)
		dm.ctx = 0;

	nd = rblist__find(rblist, &dm);
	if (nd)
		return container_of(nd, struct saved_value, rb_node);
	if (create) {
		rblist__add_node(rblist, &dm);
		nd = rblist__find(rblist, &dm);
		if (nd)
			return container_of(nd, struct saved_value, rb_node);
	}
	return NULL;
}

void runtime_stat__init(struct runtime_stat *st)
{
	struct rblist *rblist = &st->value_list;

	rblist__init(rblist);
	rblist->node_cmp = saved_value_cmp;
	rblist->node_new = saved_value_new;
	rblist->node_delete = saved_value_delete;
}

void runtime_stat__exit(struct runtime_stat *st)
{
	rblist__exit(&st->value_list);
}

void perf_stat__init_shadow_stats(void)
{
	runtime_stat__init(&rt_stat);
}

static int evsel_context(struct evsel *evsel)
{
	int ctx = 0;

	if (evsel->core.attr.exclude_kernel)
		ctx |= CTX_BIT_KERNEL;
	if (evsel->core.attr.exclude_user)
		ctx |= CTX_BIT_USER;
	if (evsel->core.attr.exclude_hv)
		ctx |= CTX_BIT_HV;
	if (evsel->core.attr.exclude_host)
		ctx |= CTX_BIT_HOST;
	if (evsel->core.attr.exclude_idle)
		ctx |= CTX_BIT_IDLE;

	return ctx;
}

static void reset_stat(struct runtime_stat *st)
{
	struct rblist *rblist;
	struct rb_node *pos, *next;

	rblist = &st->value_list;
	next = rb_first_cached(&rblist->entries);
	while (next) {
		pos = next;
		next = rb_next(pos);
		memset(&container_of(pos, struct saved_value, rb_node)->stats,
		       0,
		       sizeof(struct stats));
	}
}

void perf_stat__reset_shadow_stats(void)
{
	reset_stat(&rt_stat);
	memset(&walltime_nsecs_stats, 0, sizeof(walltime_nsecs_stats));
	memset(&ru_stats, 0, sizeof(ru_stats));
}

void perf_stat__reset_shadow_per_stat(struct runtime_stat *st)
{
	reset_stat(st);
}

struct runtime_stat_data {
	int ctx;
	struct cgroup *cgrp;
};

static void update_runtime_stat(struct runtime_stat *st,
				enum stat_type type,
				int map_idx, u64 count,
				struct runtime_stat_data *rsd)
{
	struct saved_value *v = saved_value_lookup(NULL, map_idx, true, type,
						   rsd->ctx, st, rsd->cgrp);

	if (v)
		update_stats(&v->stats, count);
}

/*
 * Update various tracking values we maintain to print
 * more semantic information such as miss/hit ratios,
 * instruction rates, etc:
 */
void perf_stat__update_shadow_stats(struct evsel *counter, u64 count,
				    int map_idx, struct runtime_stat *st)
{
	u64 count_ns = count;
	struct saved_value *v;
	struct runtime_stat_data rsd = {
		.ctx = evsel_context(counter),
		.cgrp = counter->cgrp,
	};

	count *= counter->scale;

	if (evsel__is_clock(counter))
		update_runtime_stat(st, STAT_NSECS, map_idx, count_ns, &rsd);
	else if (evsel__match(counter, HARDWARE, HW_CPU_CYCLES))
		update_runtime_stat(st, STAT_CYCLES, map_idx, count, &rsd);
	else if (perf_stat_evsel__is(counter, CYCLES_IN_TX))
		update_runtime_stat(st, STAT_CYCLES_IN_TX, map_idx, count, &rsd);
	else if (perf_stat_evsel__is(counter, TRANSACTION_START))
		update_runtime_stat(st, STAT_TRANSACTION, map_idx, count, &rsd);
	else if (perf_stat_evsel__is(counter, ELISION_START))
		update_runtime_stat(st, STAT_ELISION, map_idx, count, &rsd);
	else if (evsel__match(counter, HARDWARE, HW_STALLED_CYCLES_FRONTEND))
		update_runtime_stat(st, STAT_STALLED_CYCLES_FRONT,
				    map_idx, count, &rsd);
	else if (evsel__match(counter, HARDWARE, HW_STALLED_CYCLES_BACKEND))
		update_runtime_stat(st, STAT_STALLED_CYCLES_BACK,
				    map_idx, count, &rsd);
	else if (evsel__match(counter, HARDWARE, HW_BRANCH_INSTRUCTIONS))
		update_runtime_stat(st, STAT_BRANCHES, map_idx, count, &rsd);
	else if (evsel__match(counter, HARDWARE, HW_CACHE_REFERENCES))
		update_runtime_stat(st, STAT_CACHEREFS, map_idx, count, &rsd);
	else if (evsel__match(counter, HW_CACHE, HW_CACHE_L1D))
		update_runtime_stat(st, STAT_L1_DCACHE, map_idx, count, &rsd);
	else if (evsel__match(counter, HW_CACHE, HW_CACHE_L1I))
		update_runtime_stat(st, STAT_L1_ICACHE, map_idx, count, &rsd);
	else if (evsel__match(counter, HW_CACHE, HW_CACHE_LL))
		update_runtime_stat(st, STAT_LL_CACHE, map_idx, count, &rsd);
	else if (evsel__match(counter, HW_CACHE, HW_CACHE_DTLB))
		update_runtime_stat(st, STAT_DTLB_CACHE, map_idx, count, &rsd);
	else if (evsel__match(counter, HW_CACHE, HW_CACHE_ITLB))
		update_runtime_stat(st, STAT_ITLB_CACHE, map_idx, count, &rsd);
	else if (perf_stat_evsel__is(counter, SMI_NUM))
		update_runtime_stat(st, STAT_SMI_NUM, map_idx, count, &rsd);
	else if (perf_stat_evsel__is(counter, APERF))
		update_runtime_stat(st, STAT_APERF, map_idx, count, &rsd);

	if (counter->collect_stat) {
		v = saved_value_lookup(counter, map_idx, true, STAT_NONE, 0, st,
				       rsd.cgrp);
		update_stats(&v->stats, count);
		if (counter->metric_leader)
			v->metric_total += count;
	} else if (counter->metric_leader && !counter->merged_stat) {
		v = saved_value_lookup(counter->metric_leader,
				       map_idx, true, STAT_NONE, 0, st, rsd.cgrp);
		v->metric_total += count;
		v->metric_other++;
	}
}

/* used for get_ratio_color() */
enum grc_type {
	GRC_STALLED_CYCLES_FE,
	GRC_STALLED_CYCLES_BE,
	GRC_CACHE_MISSES,
	GRC_MAX_NR
};

static const char *get_ratio_color(enum grc_type type, double ratio)
{
	static const double grc_table[GRC_MAX_NR][3] = {
		[GRC_STALLED_CYCLES_FE] = { 50.0, 30.0, 10.0 },
		[GRC_STALLED_CYCLES_BE] = { 75.0, 50.0, 20.0 },
		[GRC_CACHE_MISSES] 	= { 20.0, 10.0, 5.0 },
	};
	const char *color = PERF_COLOR_NORMAL;

	if (ratio > grc_table[type][0])
		color = PERF_COLOR_RED;
	else if (ratio > grc_table[type][1])
		color = PERF_COLOR_MAGENTA;
	else if (ratio > grc_table[type][2])
		color = PERF_COLOR_YELLOW;

	return color;
}

static double runtime_stat_avg(struct runtime_stat *st,
			       enum stat_type type, int map_idx,
			       struct runtime_stat_data *rsd)
{
	struct saved_value *v;

	v = saved_value_lookup(NULL, map_idx, false, type, rsd->ctx, st, rsd->cgrp);
	if (!v)
		return 0.0;

	return avg_stats(&v->stats);
}

static double runtime_stat_n(struct runtime_stat *st,
			     enum stat_type type, int map_idx,
			     struct runtime_stat_data *rsd)
{
	struct saved_value *v;

	v = saved_value_lookup(NULL, map_idx, false, type, rsd->ctx, st, rsd->cgrp);
	if (!v)
		return 0.0;

	return v->stats.n;
}

static void print_stalled_cycles_frontend(struct perf_stat_config *config,
					  int map_idx, double avg,
					  struct perf_stat_output_ctx *out,
					  struct runtime_stat *st,
					  struct runtime_stat_data *rsd)
{
	double total, ratio = 0.0;
	const char *color;

	total = runtime_stat_avg(st, STAT_CYCLES, map_idx, rsd);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_STALLED_CYCLES_FE, ratio);

	if (ratio)
		out->print_metric(config, out->ctx, color, "%7.2f%%", "frontend cycles idle",
				  ratio);
	else
		out->print_metric(config, out->ctx, NULL, NULL, "frontend cycles idle", 0);
}

static void print_stalled_cycles_backend(struct perf_stat_config *config,
					 int map_idx, double avg,
					 struct perf_stat_output_ctx *out,
					 struct runtime_stat *st,
					 struct runtime_stat_data *rsd)
{
	double total, ratio = 0.0;
	const char *color;

	total = runtime_stat_avg(st, STAT_CYCLES, map_idx, rsd);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_STALLED_CYCLES_BE, ratio);

	out->print_metric(config, out->ctx, color, "%7.2f%%", "backend cycles idle", ratio);
}

static void print_branch_misses(struct perf_stat_config *config,
				int map_idx, double avg,
				struct perf_stat_output_ctx *out,
				struct runtime_stat *st,
				struct runtime_stat_data *rsd)
{
	double total, ratio = 0.0;
	const char *color;

	total = runtime_stat_avg(st, STAT_BRANCHES, map_idx, rsd);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);

	out->print_metric(config, out->ctx, color, "%7.2f%%", "of all branches", ratio);
}

static void print_l1_dcache_misses(struct perf_stat_config *config,
				   int map_idx, double avg,
				   struct perf_stat_output_ctx *out,
				   struct runtime_stat *st,
				   struct runtime_stat_data *rsd)
{
	double total, ratio = 0.0;
	const char *color;

	total = runtime_stat_avg(st, STAT_L1_DCACHE, map_idx, rsd);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);

	out->print_metric(config, out->ctx, color, "%7.2f%%", "of all L1-dcache accesses", ratio);
}

static void print_l1_icache_misses(struct perf_stat_config *config,
				   int map_idx, double avg,
				   struct perf_stat_output_ctx *out,
				   struct runtime_stat *st,
				   struct runtime_stat_data *rsd)
{
	double total, ratio = 0.0;
	const char *color;

	total = runtime_stat_avg(st, STAT_L1_ICACHE, map_idx, rsd);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);
	out->print_metric(config, out->ctx, color, "%7.2f%%", "of all L1-icache accesses", ratio);
}

static void print_dtlb_cache_misses(struct perf_stat_config *config,
				    int map_idx, double avg,
				    struct perf_stat_output_ctx *out,
				    struct runtime_stat *st,
				    struct runtime_stat_data *rsd)
{
	double total, ratio = 0.0;
	const char *color;

	total = runtime_stat_avg(st, STAT_DTLB_CACHE, map_idx, rsd);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);
	out->print_metric(config, out->ctx, color, "%7.2f%%", "of all dTLB cache accesses", ratio);
}

static void print_itlb_cache_misses(struct perf_stat_config *config,
				    int map_idx, double avg,
				    struct perf_stat_output_ctx *out,
				    struct runtime_stat *st,
				    struct runtime_stat_data *rsd)
{
	double total, ratio = 0.0;
	const char *color;

	total = runtime_stat_avg(st, STAT_ITLB_CACHE, map_idx, rsd);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);
	out->print_metric(config, out->ctx, color, "%7.2f%%", "of all iTLB cache accesses", ratio);
}

static void print_ll_cache_misses(struct perf_stat_config *config,
				  int map_idx, double avg,
				  struct perf_stat_output_ctx *out,
				  struct runtime_stat *st,
				  struct runtime_stat_data *rsd)
{
	double total, ratio = 0.0;
	const char *color;

	total = runtime_stat_avg(st, STAT_LL_CACHE, map_idx, rsd);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);
	out->print_metric(config, out->ctx, color, "%7.2f%%", "of all LL-cache accesses", ratio);
}

static void print_smi_cost(struct perf_stat_config *config, int map_idx,
			   struct perf_stat_output_ctx *out,
			   struct runtime_stat *st,
			   struct runtime_stat_data *rsd)
{
	double smi_num, aperf, cycles, cost = 0.0;
	const char *color = NULL;

	smi_num = runtime_stat_avg(st, STAT_SMI_NUM, map_idx, rsd);
	aperf = runtime_stat_avg(st, STAT_APERF, map_idx, rsd);
	cycles = runtime_stat_avg(st, STAT_CYCLES, map_idx, rsd);

	if ((cycles == 0) || (aperf == 0))
		return;

	if (smi_num)
		cost = (aperf - cycles) / aperf * 100.00;

	if (cost > 10)
		color = PERF_COLOR_RED;
	out->print_metric(config, out->ctx, color, "%8.1f%%", "SMI cycles%", cost);
	out->print_metric(config, out->ctx, NULL, "%4.0f", "SMI#", smi_num);
}

static int prepare_metric(struct evsel **metric_events,
			  struct metric_ref *metric_refs,
			  struct expr_parse_ctx *pctx,
			  int map_idx,
			  struct runtime_stat *st)
{
	double scale;
	char *n;
	int i, j, ret;

	for (i = 0; metric_events[i]; i++) {
		struct saved_value *v;
		struct stats *stats;
		u64 metric_total = 0;
		int source_count;

		if (evsel__is_tool(metric_events[i])) {
			source_count = 1;
			switch (metric_events[i]->tool_event) {
			case PERF_TOOL_DURATION_TIME:
				stats = &walltime_nsecs_stats;
				scale = 1e-9;
				break;
			case PERF_TOOL_USER_TIME:
				stats = &ru_stats.ru_utime_usec_stat;
				scale = 1e-6;
				break;
			case PERF_TOOL_SYSTEM_TIME:
				stats = &ru_stats.ru_stime_usec_stat;
				scale = 1e-6;
				break;
			case PERF_TOOL_NONE:
				pr_err("Invalid tool event 'none'");
				abort();
			case PERF_TOOL_MAX:
				pr_err("Invalid tool event 'max'");
				abort();
			default:
				pr_err("Unknown tool event '%s'", evsel__name(metric_events[i]));
				abort();
			}
		} else {
			v = saved_value_lookup(metric_events[i], map_idx, false,
					       STAT_NONE, 0, st,
					       metric_events[i]->cgrp);
			if (!v)
				break;
			stats = &v->stats;
			/*
			 * If an event was scaled during stat gathering, reverse
			 * the scale before computing the metric.
			 */
			scale = 1.0 / metric_events[i]->scale;

			source_count = evsel__source_count(metric_events[i]);

			if (v->metric_other)
				metric_total = v->metric_total * scale;
		}
		n = strdup(evsel__metric_id(metric_events[i]));
		if (!n)
			return -ENOMEM;

		expr__add_id_val_source_count(pctx, n,
					metric_total ? : avg_stats(stats) * scale,
					source_count);
	}

	for (j = 0; metric_refs && metric_refs[j].metric_name; j++) {
		ret = expr__add_ref(pctx, &metric_refs[j]);
		if (ret)
			return ret;
	}

	return i;
}

static void generic_metric(struct perf_stat_config *config,
			   const char *metric_expr,
			   const char *metric_threshold,
			   struct evsel **metric_events,
			   struct metric_ref *metric_refs,
			   char *name,
			   const char *metric_name,
			   const char *metric_unit,
			   int runtime,
			   int map_idx,
			   struct perf_stat_output_ctx *out,
			   struct runtime_stat *st)
{
	print_metric_t print_metric = out->print_metric;
	struct expr_parse_ctx *pctx;
	double ratio, scale, threshold;
	int i;
	void *ctxp = out->ctx;
	const char *color = NULL;

	pctx = expr__ctx_new();
	if (!pctx)
		return;

	if (config->user_requested_cpu_list)
		pctx->sctx.user_requested_cpu_list = strdup(config->user_requested_cpu_list);
	pctx->sctx.runtime = runtime;
	pctx->sctx.system_wide = config->system_wide;
	i = prepare_metric(metric_events, metric_refs, pctx, map_idx, st);
	if (i < 0) {
		expr__ctx_free(pctx);
		return;
	}
	if (!metric_events[i]) {
		if (expr__parse(&ratio, pctx, metric_expr) == 0) {
			char *unit;
			char metric_bf[64];

			if (metric_threshold &&
			    expr__parse(&threshold, pctx, metric_threshold) == 0 &&
			    !isnan(threshold)) {
				color = fpclassify(threshold) == FP_ZERO
					? PERF_COLOR_GREEN : PERF_COLOR_RED;
			}

			if (metric_unit && metric_name) {
				if (perf_pmu__convert_scale(metric_unit,
					&unit, &scale) >= 0) {
					ratio *= scale;
				}
				if (strstr(metric_expr, "?"))
					scnprintf(metric_bf, sizeof(metric_bf),
					  "%s  %s_%d", unit, metric_name, runtime);
				else
					scnprintf(metric_bf, sizeof(metric_bf),
					  "%s  %s", unit, metric_name);

				print_metric(config, ctxp, color, "%8.1f",
					     metric_bf, ratio);
			} else {
				print_metric(config, ctxp, color, "%8.2f",
					metric_name ?
					metric_name :
					out->force_header ?  name : "",
					ratio);
			}
		} else {
			print_metric(config, ctxp, color, /*unit=*/NULL,
				     out->force_header ?
				     (metric_name ? metric_name : name) : "", 0);
		}
	} else {
		print_metric(config, ctxp, color, /*unit=*/NULL,
			     out->force_header ?
			     (metric_name ? metric_name : name) : "", 0);
	}

	expr__ctx_free(pctx);
}

double test_generic_metric(struct metric_expr *mexp, int map_idx, struct runtime_stat *st)
{
	struct expr_parse_ctx *pctx;
	double ratio = 0.0;

	pctx = expr__ctx_new();
	if (!pctx)
		return NAN;

	if (prepare_metric(mexp->metric_events, mexp->metric_refs, pctx, map_idx, st) < 0)
		goto out;

	if (expr__parse(&ratio, pctx, mexp->metric_expr))
		ratio = 0.0;

out:
	expr__ctx_free(pctx);
	return ratio;
}

void perf_stat__print_shadow_stats(struct perf_stat_config *config,
				   struct evsel *evsel,
				   double avg, int map_idx,
				   struct perf_stat_output_ctx *out,
				   struct rblist *metric_events,
				   struct runtime_stat *st)
{
	void *ctxp = out->ctx;
	print_metric_t print_metric = out->print_metric;
	double total, ratio = 0.0, total2;
	struct runtime_stat_data rsd = {
		.ctx = evsel_context(evsel),
		.cgrp = evsel->cgrp,
	};
	struct metric_event *me;
	int num = 1;

	if (config->iostat_run) {
		iostat_print_metric(config, evsel, out);
	} else if (evsel__match(evsel, HARDWARE, HW_INSTRUCTIONS)) {
		total = runtime_stat_avg(st, STAT_CYCLES, map_idx, &rsd);

		if (total) {
			ratio = avg / total;
			print_metric(config, ctxp, NULL, "%7.2f ",
					"insn per cycle", ratio);
		} else {
			print_metric(config, ctxp, NULL, NULL, "insn per cycle", 0);
		}

		total = runtime_stat_avg(st, STAT_STALLED_CYCLES_FRONT, map_idx, &rsd);

		total = max(total, runtime_stat_avg(st,
						    STAT_STALLED_CYCLES_BACK,
						    map_idx, &rsd));

		if (total && avg) {
			out->new_line(config, ctxp);
			ratio = total / avg;
			print_metric(config, ctxp, NULL, "%7.2f ",
					"stalled cycles per insn",
					ratio);
		}
	} else if (evsel__match(evsel, HARDWARE, HW_BRANCH_MISSES)) {
		if (runtime_stat_n(st, STAT_BRANCHES, map_idx, &rsd) != 0)
			print_branch_misses(config, map_idx, avg, out, st, &rsd);
		else
			print_metric(config, ctxp, NULL, NULL, "of all branches", 0);
	} else if (
		evsel->core.attr.type == PERF_TYPE_HW_CACHE &&
		evsel->core.attr.config ==  ( PERF_COUNT_HW_CACHE_L1D |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {

		if (runtime_stat_n(st, STAT_L1_DCACHE, map_idx, &rsd) != 0)
			print_l1_dcache_misses(config, map_idx, avg, out, st, &rsd);
		else
			print_metric(config, ctxp, NULL, NULL, "of all L1-dcache accesses", 0);
	} else if (
		evsel->core.attr.type == PERF_TYPE_HW_CACHE &&
		evsel->core.attr.config ==  ( PERF_COUNT_HW_CACHE_L1I |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {

		if (runtime_stat_n(st, STAT_L1_ICACHE, map_idx, &rsd) != 0)
			print_l1_icache_misses(config, map_idx, avg, out, st, &rsd);
		else
			print_metric(config, ctxp, NULL, NULL, "of all L1-icache accesses", 0);
	} else if (
		evsel->core.attr.type == PERF_TYPE_HW_CACHE &&
		evsel->core.attr.config ==  ( PERF_COUNT_HW_CACHE_DTLB |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {

		if (runtime_stat_n(st, STAT_DTLB_CACHE, map_idx, &rsd) != 0)
			print_dtlb_cache_misses(config, map_idx, avg, out, st, &rsd);
		else
			print_metric(config, ctxp, NULL, NULL, "of all dTLB cache accesses", 0);
	} else if (
		evsel->core.attr.type == PERF_TYPE_HW_CACHE &&
		evsel->core.attr.config ==  ( PERF_COUNT_HW_CACHE_ITLB |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {

		if (runtime_stat_n(st, STAT_ITLB_CACHE, map_idx, &rsd) != 0)
			print_itlb_cache_misses(config, map_idx, avg, out, st, &rsd);
		else
			print_metric(config, ctxp, NULL, NULL, "of all iTLB cache accesses", 0);
	} else if (
		evsel->core.attr.type == PERF_TYPE_HW_CACHE &&
		evsel->core.attr.config ==  ( PERF_COUNT_HW_CACHE_LL |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {

		if (runtime_stat_n(st, STAT_LL_CACHE, map_idx, &rsd) != 0)
			print_ll_cache_misses(config, map_idx, avg, out, st, &rsd);
		else
			print_metric(config, ctxp, NULL, NULL, "of all LL-cache accesses", 0);
	} else if (evsel__match(evsel, HARDWARE, HW_CACHE_MISSES)) {
		total = runtime_stat_avg(st, STAT_CACHEREFS, map_idx, &rsd);

		if (total)
			ratio = avg * 100 / total;

		if (runtime_stat_n(st, STAT_CACHEREFS, map_idx, &rsd) != 0)
			print_metric(config, ctxp, NULL, "%8.3f %%",
				     "of all cache refs", ratio);
		else
			print_metric(config, ctxp, NULL, NULL, "of all cache refs", 0);
	} else if (evsel__match(evsel, HARDWARE, HW_STALLED_CYCLES_FRONTEND)) {
		print_stalled_cycles_frontend(config, map_idx, avg, out, st, &rsd);
	} else if (evsel__match(evsel, HARDWARE, HW_STALLED_CYCLES_BACKEND)) {
		print_stalled_cycles_backend(config, map_idx, avg, out, st, &rsd);
	} else if (evsel__match(evsel, HARDWARE, HW_CPU_CYCLES)) {
		total = runtime_stat_avg(st, STAT_NSECS, map_idx, &rsd);

		if (total) {
			ratio = avg / total;
			print_metric(config, ctxp, NULL, "%8.3f", "GHz", ratio);
		} else {
			print_metric(config, ctxp, NULL, NULL, "Ghz", 0);
		}
	} else if (perf_stat_evsel__is(evsel, CYCLES_IN_TX)) {
		total = runtime_stat_avg(st, STAT_CYCLES, map_idx, &rsd);

		if (total)
			print_metric(config, ctxp, NULL,
					"%7.2f%%", "transactional cycles",
					100.0 * (avg / total));
		else
			print_metric(config, ctxp, NULL, NULL, "transactional cycles",
				     0);
	} else if (perf_stat_evsel__is(evsel, CYCLES_IN_TX_CP)) {
		total = runtime_stat_avg(st, STAT_CYCLES, map_idx, &rsd);
		total2 = runtime_stat_avg(st, STAT_CYCLES_IN_TX, map_idx, &rsd);

		if (total2 < avg)
			total2 = avg;
		if (total)
			print_metric(config, ctxp, NULL, "%7.2f%%", "aborted cycles",
				100.0 * ((total2-avg) / total));
		else
			print_metric(config, ctxp, NULL, NULL, "aborted cycles", 0);
	} else if (perf_stat_evsel__is(evsel, TRANSACTION_START)) {
		total = runtime_stat_avg(st, STAT_CYCLES_IN_TX, map_idx, &rsd);

		if (avg)
			ratio = total / avg;

		if (runtime_stat_n(st, STAT_CYCLES_IN_TX, map_idx, &rsd) != 0)
			print_metric(config, ctxp, NULL, "%8.0f",
				     "cycles / transaction", ratio);
		else
			print_metric(config, ctxp, NULL, NULL, "cycles / transaction",
				      0);
	} else if (perf_stat_evsel__is(evsel, ELISION_START)) {
		total = runtime_stat_avg(st, STAT_CYCLES_IN_TX, map_idx, &rsd);

		if (avg)
			ratio = total / avg;

		print_metric(config, ctxp, NULL, "%8.0f", "cycles / elision", ratio);
	} else if (evsel__is_clock(evsel)) {
		if ((ratio = avg_stats(&walltime_nsecs_stats)) != 0)
			print_metric(config, ctxp, NULL, "%8.3f", "CPUs utilized",
				     avg / (ratio * evsel->scale));
		else
			print_metric(config, ctxp, NULL, NULL, "CPUs utilized", 0);
	} else if (runtime_stat_n(st, STAT_NSECS, map_idx, &rsd) != 0) {
		char unit = ' ';
		char unit_buf[10] = "/sec";

		total = runtime_stat_avg(st, STAT_NSECS, map_idx, &rsd);
		if (total)
			ratio = convert_unit_double(1000000000.0 * avg / total, &unit);

		if (unit != ' ')
			snprintf(unit_buf, sizeof(unit_buf), "%c/sec", unit);
		print_metric(config, ctxp, NULL, "%8.3f", unit_buf, ratio);
	} else if (perf_stat_evsel__is(evsel, SMI_NUM)) {
		print_smi_cost(config, map_idx, out, st, &rsd);
	} else {
		num = 0;
	}

	if ((me = metricgroup__lookup(metric_events, evsel, false)) != NULL) {
		struct metric_expr *mexp;

		list_for_each_entry (mexp, &me->head, nd) {
			if (num++ > 0)
				out->new_line(config, ctxp);
			generic_metric(config, mexp->metric_expr, mexp->metric_threshold,
				       mexp->metric_events, mexp->metric_refs, evsel->name,
				       mexp->metric_name, mexp->metric_unit, mexp->runtime,
				       map_idx, out, st);
		}
	}
	if (num == 0)
		print_metric(config, ctxp, NULL, NULL, NULL, 0);
}
