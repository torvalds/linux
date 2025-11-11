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
#include "tool_pmu.h"

struct stats walltime_nsecs_stats;
struct rusage_stats ru_stats;

void perf_stat__reset_shadow_stats(void)
{
	memset(&walltime_nsecs_stats, 0, sizeof(walltime_nsecs_stats));
	memset(&ru_stats, 0, sizeof(ru_stats));
}

static int prepare_metric(const struct metric_expr *mexp,
			  const struct evsel *evsel,
			  struct expr_parse_ctx *pctx,
			  int aggr_idx)
{
	struct evsel * const *metric_events = mexp->metric_events;
	struct metric_ref *metric_refs = mexp->metric_refs;
	int i;

	for (i = 0; metric_events[i]; i++) {
		char *n;
		double val;
		int source_count = 0;

		if (evsel__is_tool(metric_events[i])) {
			struct stats *stats;
			double scale;

			switch (evsel__tool_event(metric_events[i])) {
			case TOOL_PMU__EVENT_DURATION_TIME:
				stats = &walltime_nsecs_stats;
				scale = 1e-9;
				break;
			case TOOL_PMU__EVENT_USER_TIME:
				stats = &ru_stats.ru_utime_usec_stat;
				scale = 1e-6;
				break;
			case TOOL_PMU__EVENT_SYSTEM_TIME:
				stats = &ru_stats.ru_stime_usec_stat;
				scale = 1e-6;
				break;
			case TOOL_PMU__EVENT_NONE:
				pr_err("Invalid tool event 'none'");
				abort();
			case TOOL_PMU__EVENT_MAX:
				pr_err("Invalid tool event 'max'");
				abort();
			case TOOL_PMU__EVENT_HAS_PMEM:
			case TOOL_PMU__EVENT_NUM_CORES:
			case TOOL_PMU__EVENT_NUM_CPUS:
			case TOOL_PMU__EVENT_NUM_CPUS_ONLINE:
			case TOOL_PMU__EVENT_NUM_DIES:
			case TOOL_PMU__EVENT_NUM_PACKAGES:
			case TOOL_PMU__EVENT_SLOTS:
			case TOOL_PMU__EVENT_SMT_ON:
			case TOOL_PMU__EVENT_SYSTEM_TSC_FREQ:
			case TOOL_PMU__EVENT_CORE_WIDE:
			case TOOL_PMU__EVENT_TARGET_CPU:
			default:
				pr_err("Unexpected tool event '%s'", evsel__name(metric_events[i]));
				abort();
			}
			val = avg_stats(stats) * scale;
			source_count = 1;
		} else {
			struct perf_stat_evsel *ps = metric_events[i]->stats;
			struct perf_stat_aggr *aggr;

			/*
			 * If there are multiple uncore PMUs and we're not
			 * reading the leader's stats, determine the stats for
			 * the appropriate uncore PMU.
			 */
			if (evsel && evsel->metric_leader &&
			    evsel->pmu != evsel->metric_leader->pmu &&
			    mexp->metric_events[i]->pmu == evsel->metric_leader->pmu) {
				struct evsel *pos;

				evlist__for_each_entry(evsel->evlist, pos) {
					if (pos->pmu != evsel->pmu)
						continue;
					if (pos->metric_leader != mexp->metric_events[i])
						continue;
					ps = pos->stats;
					source_count = 1;
					break;
				}
			}
			aggr = &ps->aggr[aggr_idx];
			if (!aggr)
				break;

			if (!metric_events[i]->supported) {
				/*
				 * Not supported events will have a count of 0,
				 * which can be confusing in a
				 * metric. Explicitly set the value to NAN. Not
				 * counted events (enable time of 0) are read as
				 * 0.
				 */
				val = NAN;
				source_count = 0;
			} else {
				val = aggr->counts.val;
				if (!source_count)
					source_count = evsel__source_count(metric_events[i]);
			}
		}
		n = strdup(evsel__metric_id(metric_events[i]));
		if (!n)
			return -ENOMEM;

		expr__add_id_val_source_count(pctx, n, val, source_count);
	}

	for (int j = 0; metric_refs && metric_refs[j].metric_name; j++) {
		int ret = expr__add_ref(pctx, &metric_refs[j]);

		if (ret)
			return ret;
	}

	return i;
}

static void generic_metric(struct perf_stat_config *config,
			   struct metric_expr *mexp,
			   struct evsel *evsel,
			   int aggr_idx,
			   struct perf_stat_output_ctx *out)
{
	print_metric_t print_metric = out->print_metric;
	const char *metric_name = mexp->metric_name;
	const char *metric_expr = mexp->metric_expr;
	const char *metric_threshold = mexp->metric_threshold;
	const char *metric_unit = mexp->metric_unit;
	struct evsel * const *metric_events = mexp->metric_events;
	int runtime = mexp->runtime;
	struct expr_parse_ctx *pctx;
	double ratio, scale, threshold;
	int i;
	void *ctxp = out->ctx;
	enum metric_threshold_classify thresh = METRIC_THRESHOLD_UNKNOWN;

	pctx = expr__ctx_new();
	if (!pctx)
		return;

	if (config->user_requested_cpu_list)
		pctx->sctx.user_requested_cpu_list = strdup(config->user_requested_cpu_list);
	pctx->sctx.runtime = runtime;
	pctx->sctx.system_wide = config->system_wide;
	i = prepare_metric(mexp, evsel, pctx, aggr_idx);
	if (i < 0) {
		expr__ctx_free(pctx);
		return;
	}
	if (!metric_events[i]) {
		if (expr__parse(&ratio, pctx, metric_expr) == 0) {
			char *unit;
			char metric_bf[128];

			if (metric_threshold &&
			    expr__parse(&threshold, pctx, metric_threshold) == 0 &&
			    !isnan(threshold)) {
				thresh = fpclassify(threshold) == FP_ZERO
					? METRIC_THRESHOLD_GOOD : METRIC_THRESHOLD_BAD;
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

				print_metric(config, ctxp, thresh, "%8.1f",
					     metric_bf, ratio);
			} else {
				print_metric(config, ctxp, thresh, "%8.2f",
					metric_name ?
					metric_name :
					out->force_header ?  evsel->name : "",
					ratio);
			}
		} else {
			print_metric(config, ctxp, thresh, /*fmt=*/NULL,
				     out->force_header ?
				     (metric_name ?: evsel->name) : "", 0);
		}
	} else {
		print_metric(config, ctxp, thresh, /*fmt=*/NULL,
			     out->force_header ?
			     (metric_name ?: evsel->name) : "", 0);
	}

	expr__ctx_free(pctx);
}

double test_generic_metric(struct metric_expr *mexp, int aggr_idx)
{
	struct expr_parse_ctx *pctx;
	double ratio = 0.0;

	pctx = expr__ctx_new();
	if (!pctx)
		return NAN;

	if (prepare_metric(mexp, /*evsel=*/NULL, pctx, aggr_idx) < 0)
		goto out;

	if (expr__parse(&ratio, pctx, mexp->metric_expr))
		ratio = 0.0;

out:
	expr__ctx_free(pctx);
	return ratio;
}

static void perf_stat__print_metricgroup_header(struct perf_stat_config *config,
						struct evsel *evsel,
						void *ctxp,
						const char *name,
						struct perf_stat_output_ctx *out)
{
	bool need_full_name = perf_pmus__num_core_pmus() > 1;
	static const char *last_name;
	static const struct perf_pmu *last_pmu;
	char full_name[64];

	/*
	 * A metricgroup may have several metric events,
	 * e.g.,TopdownL1 on e-core of ADL.
	 * The name has been output by the first metric
	 * event. Only align with other metics from
	 * different metric events.
	 */
	if (last_name && !strcmp(last_name, name) && last_pmu == evsel->pmu) {
		out->print_metricgroup_header(config, ctxp, NULL);
		return;
	}

	if (need_full_name && evsel->pmu)
		scnprintf(full_name, sizeof(full_name), "%s (%s)", name, evsel->pmu->name);
	else
		scnprintf(full_name, sizeof(full_name), "%s", name);

	out->print_metricgroup_header(config, ctxp, full_name);

	last_name = name;
	last_pmu = evsel->pmu;
}

/**
 * perf_stat__print_shadow_stats_metricgroup - Print out metrics associated with the evsel
 *					       For the non-default, all metrics associated
 *					       with the evsel are printed.
 *					       For the default mode, only the metrics from
 *					       the same metricgroup and the name of the
 *					       metricgroup are printed. To print the metrics
 *					       from the next metricgroup (if available),
 *					       invoke the function with correspoinding
 *					       metric_expr.
 */
void *perf_stat__print_shadow_stats_metricgroup(struct perf_stat_config *config,
						struct evsel *evsel,
						int aggr_idx,
						int *num,
						void *from,
						struct perf_stat_output_ctx *out)
{
	struct metric_event *me;
	struct metric_expr *mexp = from;
	void *ctxp = out->ctx;
	bool header_printed = false;
	const char *name = NULL;
	struct rblist *metric_events = &evsel->evlist->metric_events;

	me = metricgroup__lookup(metric_events, evsel, false);
	if (me == NULL)
		return NULL;

	if (!mexp)
		mexp = list_first_entry(&me->head, typeof(*mexp), nd);

	list_for_each_entry_from(mexp, &me->head, nd) {
		/* Print the display name of the Default metricgroup */
		if (!config->metric_only && me->is_default) {
			if (!name)
				name = mexp->default_metricgroup_name;
			/*
			 * Two or more metricgroup may share the same metric
			 * event, e.g., TopdownL1 and TopdownL2 on SPR.
			 * Return and print the prefix, e.g., noise, running
			 * for the next metricgroup.
			 */
			if (strcmp(name, mexp->default_metricgroup_name))
				return (void *)mexp;
			/* Only print the name of the metricgroup once */
			if (!header_printed && !evsel->default_show_events) {
				header_printed = true;
				perf_stat__print_metricgroup_header(config, evsel, ctxp,
								    name, out);
			}
		}

		if ((*num)++ > 0 && out->new_line)
			out->new_line(config, ctxp);
		generic_metric(config, mexp, evsel, aggr_idx, out);
	}

	return NULL;
}

void perf_stat__print_shadow_stats(struct perf_stat_config *config,
				   struct evsel *evsel,
				   int aggr_idx,
				   struct perf_stat_output_ctx *out)
{
	print_metric_t print_metric = out->print_metric;
	void *ctxp = out->ctx;
	int num = 0;

	if (config->iostat_run)
		iostat_print_metric(config, evsel, out);

	perf_stat__print_shadow_stats_metricgroup(config, evsel, aggr_idx,
						  &num, NULL, out);

	if (num == 0) {
		print_metric(config, ctxp, METRIC_THRESHOLD_UNKNOWN,
			     /*fmt=*/NULL, /*unit=*/NULL, 0);
	}
}

/**
 * perf_stat__skip_metric_event - Skip the evsel in the Default metricgroup,
 *				  if it's not running or not the metric event.
 */
bool perf_stat__skip_metric_event(struct evsel *evsel,
				  u64 ena, u64 run)
{
	if (!evsel->default_metricgroup)
		return false;

	if (!ena || !run)
		return true;

	return !metricgroup__lookup(&evsel->evlist->metric_events, evsel, false);
}
