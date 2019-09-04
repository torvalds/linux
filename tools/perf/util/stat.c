// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include "stat.h"
#include "evlist.h"
#include "evsel.h"
#include "thread_map.h"

void update_stats(struct stats *stats, u64 val)
{
	double delta;

	stats->n++;
	delta = val - stats->mean;
	stats->mean += delta / stats->n;
	stats->M2 += delta*(val - stats->mean);

	if (val > stats->max)
		stats->max = val;

	if (val < stats->min)
		stats->min = val;
}

double avg_stats(struct stats *stats)
{
	return stats->mean;
}

/*
 * http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
 *
 *       (\Sum n_i^2) - ((\Sum n_i)^2)/n
 * s^2 = -------------------------------
 *                  n - 1
 *
 * http://en.wikipedia.org/wiki/Stddev
 *
 * The std dev of the mean is related to the std dev by:
 *
 *             s
 * s_mean = -------
 *          sqrt(n)
 *
 */
double stddev_stats(struct stats *stats)
{
	double variance, variance_mean;

	if (stats->n < 2)
		return 0.0;

	variance = stats->M2 / (stats->n - 1);
	variance_mean = variance / stats->n;

	return sqrt(variance_mean);
}

double rel_stddev_stats(double stddev, double avg)
{
	double pct = 0.0;

	if (avg)
		pct = 100.0 * stddev/avg;

	return pct;
}

bool __perf_evsel_stat__is(struct perf_evsel *evsel,
			   enum perf_stat_evsel_id id)
{
	struct perf_stat_evsel *ps = evsel->stats;

	return ps->id == id;
}

#define ID(id, name) [PERF_STAT_EVSEL_ID__##id] = #name
static const char *id_str[PERF_STAT_EVSEL_ID__MAX] = {
	ID(NONE,		x),
	ID(CYCLES_IN_TX,	cpu/cycles-t/),
	ID(TRANSACTION_START,	cpu/tx-start/),
	ID(ELISION_START,	cpu/el-start/),
	ID(CYCLES_IN_TX_CP,	cpu/cycles-ct/),
	ID(TOPDOWN_TOTAL_SLOTS, topdown-total-slots),
	ID(TOPDOWN_SLOTS_ISSUED, topdown-slots-issued),
	ID(TOPDOWN_SLOTS_RETIRED, topdown-slots-retired),
	ID(TOPDOWN_FETCH_BUBBLES, topdown-fetch-bubbles),
	ID(TOPDOWN_RECOVERY_BUBBLES, topdown-recovery-bubbles),
	ID(SMI_NUM, msr/smi/),
	ID(APERF, msr/aperf/),
};
#undef ID

static void perf_stat_evsel_id_init(struct perf_evsel *evsel)
{
	struct perf_stat_evsel *ps = evsel->stats;
	int i;

	/* ps->id is 0 hence PERF_STAT_EVSEL_ID__NONE by default */

	for (i = 0; i < PERF_STAT_EVSEL_ID__MAX; i++) {
		if (!strcmp(perf_evsel__name(evsel), id_str[i])) {
			ps->id = i;
			break;
		}
	}
}

static void perf_evsel__reset_stat_priv(struct perf_evsel *evsel)
{
	int i;
	struct perf_stat_evsel *ps = evsel->stats;

	for (i = 0; i < 3; i++)
		init_stats(&ps->res_stats[i]);

	perf_stat_evsel_id_init(evsel);
}

static int perf_evsel__alloc_stat_priv(struct perf_evsel *evsel)
{
	evsel->stats = zalloc(sizeof(struct perf_stat_evsel));
	if (evsel->stats == NULL)
		return -ENOMEM;
	perf_evsel__reset_stat_priv(evsel);
	return 0;
}

static void perf_evsel__free_stat_priv(struct perf_evsel *evsel)
{
	struct perf_stat_evsel *ps = evsel->stats;

	if (ps)
		free(ps->group_data);
	zfree(&evsel->stats);
}

static int perf_evsel__alloc_prev_raw_counts(struct perf_evsel *evsel,
					     int ncpus, int nthreads)
{
	struct perf_counts *counts;

	counts = perf_counts__new(ncpus, nthreads);
	if (counts)
		evsel->prev_raw_counts = counts;

	return counts ? 0 : -ENOMEM;
}

static void perf_evsel__free_prev_raw_counts(struct perf_evsel *evsel)
{
	perf_counts__delete(evsel->prev_raw_counts);
	evsel->prev_raw_counts = NULL;
}

static void perf_evsel__reset_prev_raw_counts(struct perf_evsel *evsel)
{
	if (evsel->prev_raw_counts) {
		evsel->prev_raw_counts->aggr.val = 0;
		evsel->prev_raw_counts->aggr.ena = 0;
		evsel->prev_raw_counts->aggr.run = 0;
       }
}

static int perf_evsel__alloc_stats(struct perf_evsel *evsel, bool alloc_raw)
{
	int ncpus = perf_evsel__nr_cpus(evsel);
	int nthreads = thread_map__nr(evsel->threads);

	if (perf_evsel__alloc_stat_priv(evsel) < 0 ||
	    perf_evsel__alloc_counts(evsel, ncpus, nthreads) < 0 ||
	    (alloc_raw && perf_evsel__alloc_prev_raw_counts(evsel, ncpus, nthreads) < 0))
		return -ENOMEM;

	return 0;
}

int perf_evlist__alloc_stats(struct perf_evlist *evlist, bool alloc_raw)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (perf_evsel__alloc_stats(evsel, alloc_raw))
			goto out_free;
	}

	return 0;

out_free:
	perf_evlist__free_stats(evlist);
	return -1;
}

void perf_evlist__free_stats(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		perf_evsel__free_stat_priv(evsel);
		perf_evsel__free_counts(evsel);
		perf_evsel__free_prev_raw_counts(evsel);
	}
}

void perf_evlist__reset_stats(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		perf_evsel__reset_stat_priv(evsel);
		perf_evsel__reset_counts(evsel);
	}
}

void perf_evlist__reset_prev_raw_counts(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(evlist, evsel)
		perf_evsel__reset_prev_raw_counts(evsel);
}

static void zero_per_pkg(struct perf_evsel *counter)
{
	if (counter->per_pkg_mask)
		memset(counter->per_pkg_mask, 0, MAX_NR_CPUS);
}

static int check_per_pkg(struct perf_evsel *counter,
			 struct perf_counts_values *vals, int cpu, bool *skip)
{
	unsigned long *mask = counter->per_pkg_mask;
	struct cpu_map *cpus = perf_evsel__cpus(counter);
	int s;

	*skip = false;

	if (!counter->per_pkg)
		return 0;

	if (cpu_map__empty(cpus))
		return 0;

	if (!mask) {
		mask = zalloc(MAX_NR_CPUS);
		if (!mask)
			return -ENOMEM;

		counter->per_pkg_mask = mask;
	}

	/*
	 * we do not consider an event that has not run as a good
	 * instance to mark a package as used (skip=1). Otherwise
	 * we may run into a situation where the first CPU in a package
	 * is not running anything, yet the second is, and this function
	 * would mark the package as used after the first CPU and would
	 * not read the values from the second CPU.
	 */
	if (!(vals->run && vals->ena))
		return 0;

	s = cpu_map__get_socket(cpus, cpu, NULL);
	if (s < 0)
		return -1;

	*skip = test_and_set_bit(s, mask) == 1;
	return 0;
}

static int
process_counter_values(struct perf_stat_config *config, struct perf_evsel *evsel,
		       int cpu, int thread,
		       struct perf_counts_values *count)
{
	struct perf_counts_values *aggr = &evsel->counts->aggr;
	static struct perf_counts_values zero;
	bool skip = false;

	if (check_per_pkg(evsel, count, cpu, &skip)) {
		pr_err("failed to read per-pkg counter\n");
		return -1;
	}

	if (skip)
		count = &zero;

	switch (config->aggr_mode) {
	case AGGR_THREAD:
	case AGGR_CORE:
	case AGGR_SOCKET:
	case AGGR_NONE:
		if (!evsel->snapshot)
			perf_evsel__compute_deltas(evsel, cpu, thread, count);
		perf_counts_values__scale(count, config->scale, NULL);
		if (config->aggr_mode == AGGR_NONE)
			perf_stat__update_shadow_stats(evsel, count->val, cpu,
						       &rt_stat);
		if (config->aggr_mode == AGGR_THREAD) {
			if (config->stats)
				perf_stat__update_shadow_stats(evsel,
					count->val, 0, &config->stats[thread]);
			else
				perf_stat__update_shadow_stats(evsel,
					count->val, 0, &rt_stat);
		}
		break;
	case AGGR_GLOBAL:
		aggr->val += count->val;
		if (config->scale) {
			aggr->ena += count->ena;
			aggr->run += count->run;
		}
	case AGGR_UNSET:
	default:
		break;
	}

	return 0;
}

static int process_counter_maps(struct perf_stat_config *config,
				struct perf_evsel *counter)
{
	int nthreads = thread_map__nr(counter->threads);
	int ncpus = perf_evsel__nr_cpus(counter);
	int cpu, thread;

	if (counter->system_wide)
		nthreads = 1;

	for (thread = 0; thread < nthreads; thread++) {
		for (cpu = 0; cpu < ncpus; cpu++) {
			if (process_counter_values(config, counter, cpu, thread,
						   perf_counts(counter->counts, cpu, thread)))
				return -1;
		}
	}

	return 0;
}

int perf_stat_process_counter(struct perf_stat_config *config,
			      struct perf_evsel *counter)
{
	struct perf_counts_values *aggr = &counter->counts->aggr;
	struct perf_stat_evsel *ps = counter->stats;
	u64 *count = counter->counts->aggr.values;
	int i, ret;

	aggr->val = aggr->ena = aggr->run = 0;

	/*
	 * We calculate counter's data every interval,
	 * and the display code shows ps->res_stats
	 * avg value. We need to zero the stats for
	 * interval mode, otherwise overall avg running
	 * averages will be shown for each interval.
	 */
	if (config->interval)
		init_stats(ps->res_stats);

	if (counter->per_pkg)
		zero_per_pkg(counter);

	ret = process_counter_maps(config, counter);
	if (ret)
		return ret;

	if (config->aggr_mode != AGGR_GLOBAL)
		return 0;

	if (!counter->snapshot)
		perf_evsel__compute_deltas(counter, -1, -1, aggr);
	perf_counts_values__scale(aggr, config->scale, &counter->counts->scaled);

	for (i = 0; i < 3; i++)
		update_stats(&ps->res_stats[i], count[i]);

	if (verbose > 0) {
		fprintf(config->output, "%s: %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
			perf_evsel__name(counter), count[0], count[1], count[2]);
	}

	/*
	 * Save the full runtime - to allow normalization during printout:
	 */
	perf_stat__update_shadow_stats(counter, *count, 0, &rt_stat);

	return 0;
}

int perf_event__process_stat_event(struct perf_tool *tool __maybe_unused,
				   union perf_event *event,
				   struct perf_session *session)
{
	struct perf_counts_values count;
	struct stat_event *st = &event->stat;
	struct perf_evsel *counter;

	count.val = st->val;
	count.ena = st->ena;
	count.run = st->run;

	counter = perf_evlist__id2evsel(session->evlist, st->id);
	if (!counter) {
		pr_err("Failed to resolve counter for stat event.\n");
		return -EINVAL;
	}

	*perf_counts(counter->counts, st->cpu, st->thread) = count;
	counter->supported = true;
	return 0;
}

size_t perf_event__fprintf_stat(union perf_event *event, FILE *fp)
{
	struct stat_event *st = (struct stat_event *) event;
	size_t ret;

	ret  = fprintf(fp, "\n... id %" PRIu64 ", cpu %d, thread %d\n",
		       st->id, st->cpu, st->thread);
	ret += fprintf(fp, "... value %" PRIu64 ", enabled %" PRIu64 ", running %" PRIu64 "\n",
		       st->val, st->ena, st->run);

	return ret;
}

size_t perf_event__fprintf_stat_round(union perf_event *event, FILE *fp)
{
	struct stat_round_event *rd = (struct stat_round_event *)event;
	size_t ret;

	ret = fprintf(fp, "\n... time %" PRIu64 ", type %s\n", rd->time,
		      rd->type == PERF_STAT_ROUND_TYPE__FINAL ? "FINAL" : "INTERVAL");

	return ret;
}

size_t perf_event__fprintf_stat_config(union perf_event *event, FILE *fp)
{
	struct perf_stat_config sc;
	size_t ret;

	perf_event__read_stat_config(&sc, &event->stat_config);

	ret  = fprintf(fp, "\n");
	ret += fprintf(fp, "... aggr_mode %d\n", sc.aggr_mode);
	ret += fprintf(fp, "... scale     %d\n", sc.scale);
	ret += fprintf(fp, "... interval  %u\n", sc.interval);

	return ret;
}
