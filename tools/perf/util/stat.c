// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <linux/err.h>
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include "counts.h"
#include "cpumap.h"
#include "debug.h"
#include "header.h"
#include "stat.h"
#include "session.h"
#include "target.h"
#include "evlist.h"
#include "evsel.h"
#include "thread_map.h"
#include "util/hashmap.h"
#include <linux/zalloc.h>

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

static void evsel__reset_aggr_stats(struct evsel *evsel)
{
	struct perf_stat_evsel *ps = evsel->stats;
	struct perf_stat_aggr *aggr = ps->aggr;

	if (aggr)
		memset(aggr, 0, sizeof(*aggr) * ps->nr_aggr);
}

static void evsel__reset_stat_priv(struct evsel *evsel)
{
	struct perf_stat_evsel *ps = evsel->stats;

	init_stats(&ps->res_stats);
	evsel__reset_aggr_stats(evsel);
}

static int evsel__alloc_aggr_stats(struct evsel *evsel, int nr_aggr)
{
	struct perf_stat_evsel *ps = evsel->stats;

	if (ps == NULL)
		return 0;

	ps->nr_aggr = nr_aggr;
	ps->aggr = calloc(nr_aggr, sizeof(*ps->aggr));
	if (ps->aggr == NULL)
		return -ENOMEM;

	return 0;
}

int evlist__alloc_aggr_stats(struct evlist *evlist, int nr_aggr)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel__alloc_aggr_stats(evsel, nr_aggr) < 0)
			return -1;
	}
	return 0;
}

static int evsel__alloc_stat_priv(struct evsel *evsel, int nr_aggr)
{
	struct perf_stat_evsel *ps;

	ps = zalloc(sizeof(*ps));
	if (ps == NULL)
		return -ENOMEM;

	evsel->stats = ps;

	if (nr_aggr && evsel__alloc_aggr_stats(evsel, nr_aggr) < 0) {
		evsel->stats = NULL;
		free(ps);
		return -ENOMEM;
	}

	evsel__reset_stat_priv(evsel);
	return 0;
}

static void evsel__free_stat_priv(struct evsel *evsel)
{
	struct perf_stat_evsel *ps = evsel->stats;

	if (ps) {
		zfree(&ps->aggr);
		zfree(&ps->group_data);
	}
	zfree(&evsel->stats);
}

static int evsel__alloc_prev_raw_counts(struct evsel *evsel)
{
	int cpu_map_nr = evsel__nr_cpus(evsel);
	int nthreads = perf_thread_map__nr(evsel->core.threads);
	struct perf_counts *counts;

	counts = perf_counts__new(cpu_map_nr, nthreads);
	if (counts)
		evsel->prev_raw_counts = counts;

	return counts ? 0 : -ENOMEM;
}

static void evsel__free_prev_raw_counts(struct evsel *evsel)
{
	perf_counts__delete(evsel->prev_raw_counts);
	evsel->prev_raw_counts = NULL;
}

static void evsel__reset_prev_raw_counts(struct evsel *evsel)
{
	if (evsel->prev_raw_counts)
		perf_counts__reset(evsel->prev_raw_counts);
}

static int evsel__alloc_stats(struct evsel *evsel, int nr_aggr, bool alloc_raw)
{
	if (evsel__alloc_stat_priv(evsel, nr_aggr) < 0 ||
	    evsel__alloc_counts(evsel) < 0 ||
	    (alloc_raw && evsel__alloc_prev_raw_counts(evsel) < 0))
		return -ENOMEM;

	return 0;
}

int evlist__alloc_stats(struct perf_stat_config *config,
			struct evlist *evlist, bool alloc_raw)
{
	struct evsel *evsel;
	int nr_aggr = 0;

	if (config && config->aggr_map)
		nr_aggr = config->aggr_map->nr;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel__alloc_stats(evsel, nr_aggr, alloc_raw))
			goto out_free;
	}

	return 0;

out_free:
	evlist__free_stats(evlist);
	return -1;
}

void evlist__free_stats(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		evsel__free_stat_priv(evsel);
		evsel__free_counts(evsel);
		evsel__free_prev_raw_counts(evsel);
	}
}

void evlist__reset_stats(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		evsel__reset_stat_priv(evsel);
		evsel__reset_counts(evsel);
	}
}

void evlist__reset_aggr_stats(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel)
		evsel__reset_aggr_stats(evsel);
}

void evlist__reset_prev_raw_counts(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel)
		evsel__reset_prev_raw_counts(evsel);
}

static void evsel__copy_prev_raw_counts(struct evsel *evsel)
{
	int idx, nthreads = perf_thread_map__nr(evsel->core.threads);

	for (int thread = 0; thread < nthreads; thread++) {
		perf_cpu_map__for_each_idx(idx, evsel__cpus(evsel)) {
			*perf_counts(evsel->counts, idx, thread) =
				*perf_counts(evsel->prev_raw_counts, idx, thread);
		}
	}
}

void evlist__copy_prev_raw_counts(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel)
		evsel__copy_prev_raw_counts(evsel);
}

static void evsel__copy_res_stats(struct evsel *evsel)
{
	struct perf_stat_evsel *ps = evsel->stats;

	/*
	 * For GLOBAL aggregation mode, it updates the counts for each run
	 * in the evsel->stats.res_stats.  See perf_stat_process_counter().
	 */
	*ps->aggr[0].counts.values = avg_stats(&ps->res_stats);
}

void evlist__copy_res_stats(struct perf_stat_config *config, struct evlist *evlist)
{
	struct evsel *evsel;

	if (config->aggr_mode != AGGR_GLOBAL)
		return;

	evlist__for_each_entry(evlist, evsel)
		evsel__copy_res_stats(evsel);
}

static size_t pkg_id_hash(long __key, void *ctx __maybe_unused)
{
	uint64_t *key = (uint64_t *) __key;

	return *key & 0xffffffff;
}

static bool pkg_id_equal(long __key1, long __key2, void *ctx __maybe_unused)
{
	uint64_t *key1 = (uint64_t *) __key1;
	uint64_t *key2 = (uint64_t *) __key2;

	return *key1 == *key2;
}

static int check_per_pkg(struct evsel *counter, struct perf_counts_values *vals,
			 int cpu_map_idx, bool *skip)
{
	struct hashmap *mask = counter->per_pkg_mask;
	struct perf_cpu_map *cpus = evsel__cpus(counter);
	struct perf_cpu cpu = perf_cpu_map__cpu(cpus, cpu_map_idx);
	int s, d, ret = 0;
	uint64_t *key;

	*skip = false;

	if (!counter->per_pkg)
		return 0;

	if (perf_cpu_map__is_any_cpu_or_is_empty(cpus))
		return 0;

	if (!mask) {
		mask = hashmap__new(pkg_id_hash, pkg_id_equal, NULL);
		if (IS_ERR(mask))
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

	s = cpu__get_socket_id(cpu);
	if (s < 0)
		return -1;

	/*
	 * On multi-die system, die_id > 0. On no-die system, die_id = 0.
	 * We use hashmap(socket, die) to check the used socket+die pair.
	 */
	d = cpu__get_die_id(cpu);
	if (d < 0)
		return -1;

	key = malloc(sizeof(*key));
	if (!key)
		return -ENOMEM;

	*key = (uint64_t)d << 32 | s;
	if (hashmap__find(mask, key, NULL)) {
		*skip = true;
		free(key);
	} else
		ret = hashmap__add(mask, key, 1);

	return ret;
}

static bool evsel__count_has_error(struct evsel *evsel,
				   struct perf_counts_values *count,
				   struct perf_stat_config *config)
{
	/* the evsel was failed already */
	if (evsel->err || evsel->counts->scaled == -1)
		return true;

	/* this is meaningful for CPU aggregation modes only */
	if (config->aggr_mode == AGGR_GLOBAL)
		return false;

	/* it's considered ok when it actually ran */
	if (count->ena != 0 && count->run != 0)
		return false;

	return true;
}

static int
process_counter_values(struct perf_stat_config *config, struct evsel *evsel,
		       int cpu_map_idx, int thread,
		       struct perf_counts_values *count)
{
	struct perf_stat_evsel *ps = evsel->stats;
	static struct perf_counts_values zero;
	bool skip = false;

	if (check_per_pkg(evsel, count, cpu_map_idx, &skip)) {
		pr_err("failed to read per-pkg counter\n");
		return -1;
	}

	if (skip)
		count = &zero;

	if (!evsel->snapshot)
		evsel__compute_deltas(evsel, cpu_map_idx, thread, count);
	perf_counts_values__scale(count, config->scale, NULL);

	if (config->aggr_mode == AGGR_THREAD) {
		struct perf_counts_values *aggr_counts = &ps->aggr[thread].counts;

		/*
		 * Skip value 0 when enabling --per-thread globally,
		 * otherwise too many 0 output.
		 */
		if (count->val == 0 && config->system_wide)
			return 0;

		ps->aggr[thread].nr++;

		aggr_counts->val += count->val;
		aggr_counts->ena += count->ena;
		aggr_counts->run += count->run;
		return 0;
	}

	if (ps->aggr) {
		struct perf_cpu cpu = perf_cpu_map__cpu(evsel->core.cpus, cpu_map_idx);
		struct aggr_cpu_id aggr_id = config->aggr_get_id(config, cpu);
		struct perf_stat_aggr *ps_aggr;
		int i;

		for (i = 0; i < ps->nr_aggr; i++) {
			if (!aggr_cpu_id__equal(&aggr_id, &config->aggr_map->map[i]))
				continue;

			ps_aggr = &ps->aggr[i];
			ps_aggr->nr++;

			/*
			 * When any result is bad, make them all to give consistent output
			 * in interval mode.  But per-task counters can have 0 enabled time
			 * when some tasks are idle.
			 */
			if (evsel__count_has_error(evsel, count, config) && !ps_aggr->failed) {
				ps_aggr->counts.val = 0;
				ps_aggr->counts.ena = 0;
				ps_aggr->counts.run = 0;
				ps_aggr->failed = true;
			}

			if (!ps_aggr->failed) {
				ps_aggr->counts.val += count->val;
				ps_aggr->counts.ena += count->ena;
				ps_aggr->counts.run += count->run;
			}
			break;
		}
	}

	return 0;
}

static int process_counter_maps(struct perf_stat_config *config,
				struct evsel *counter)
{
	int nthreads = perf_thread_map__nr(counter->core.threads);
	int ncpus = evsel__nr_cpus(counter);
	int idx, thread;

	for (thread = 0; thread < nthreads; thread++) {
		for (idx = 0; idx < ncpus; idx++) {
			if (process_counter_values(config, counter, idx, thread,
						   perf_counts(counter->counts, idx, thread)))
				return -1;
		}
	}

	return 0;
}

int perf_stat_process_counter(struct perf_stat_config *config,
			      struct evsel *counter)
{
	struct perf_stat_evsel *ps = counter->stats;
	u64 *count;
	int ret;

	if (counter->per_pkg)
		evsel__zero_per_pkg(counter);

	ret = process_counter_maps(config, counter);
	if (ret)
		return ret;

	if (config->aggr_mode != AGGR_GLOBAL)
		return 0;

	/*
	 * GLOBAL aggregation mode only has a single aggr counts,
	 * so we can use ps->aggr[0] as the actual output.
	 */
	count = ps->aggr[0].counts.values;
	update_stats(&ps->res_stats, *count);

	if (verbose > 0) {
		fprintf(config->output, "%s: %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
			evsel__name(counter), count[0], count[1], count[2]);
	}

	return 0;
}

static int evsel__merge_aggr_counters(struct evsel *evsel, struct evsel *alias)
{
	struct perf_stat_evsel *ps_a = evsel->stats;
	struct perf_stat_evsel *ps_b = alias->stats;
	int i;

	if (ps_a->aggr == NULL && ps_b->aggr == NULL)
		return 0;

	if (ps_a->nr_aggr != ps_b->nr_aggr) {
		pr_err("Unmatched aggregation mode between aliases\n");
		return -1;
	}

	for (i = 0; i < ps_a->nr_aggr; i++) {
		struct perf_counts_values *aggr_counts_a = &ps_a->aggr[i].counts;
		struct perf_counts_values *aggr_counts_b = &ps_b->aggr[i].counts;

		ps_a->aggr[i].nr += ps_b->aggr[i].nr;

		aggr_counts_a->val += aggr_counts_b->val;
		aggr_counts_a->ena += aggr_counts_b->ena;
		aggr_counts_a->run += aggr_counts_b->run;
	}

	return 0;
}

static void evsel__merge_aliases(struct evsel *evsel)
{
	struct evlist *evlist = evsel->evlist;
	struct evsel *alias;

	alias = list_prepare_entry(evsel, &(evlist->core.entries), core.node);
	list_for_each_entry_continue(alias, &evlist->core.entries, core.node) {
		if (alias->first_wildcard_match == evsel) {
			/* Merge the same events on different PMUs. */
			evsel__merge_aggr_counters(evsel, alias);
		}
	}
}

static bool evsel__should_merge_hybrid(const struct evsel *evsel,
				       const struct perf_stat_config *config)
{
	return config->hybrid_merge && evsel__is_hybrid(evsel);
}

static void evsel__merge_stats(struct evsel *evsel, struct perf_stat_config *config)
{
	if (!evsel->pmu || !evsel->pmu->is_core || evsel__should_merge_hybrid(evsel, config))
		evsel__merge_aliases(evsel);
}

/* merge the same uncore and hybrid events if requested */
void perf_stat_merge_counters(struct perf_stat_config *config, struct evlist *evlist)
{
	struct evsel *evsel;

	if (config->aggr_mode == AGGR_NONE)
		return;

	evlist__for_each_entry(evlist, evsel)
		evsel__merge_stats(evsel, config);
}

static void evsel__update_percore_stats(struct evsel *evsel, struct aggr_cpu_id *core_id)
{
	struct perf_stat_evsel *ps = evsel->stats;
	struct perf_counts_values counts = { 0, };
	struct aggr_cpu_id id;
	struct perf_cpu cpu;
	int idx;

	/* collect per-core counts */
	perf_cpu_map__for_each_cpu(cpu, idx, evsel->core.cpus) {
		struct perf_stat_aggr *aggr = &ps->aggr[idx];

		id = aggr_cpu_id__core(cpu, NULL);
		if (!aggr_cpu_id__equal(core_id, &id))
			continue;

		counts.val += aggr->counts.val;
		counts.ena += aggr->counts.ena;
		counts.run += aggr->counts.run;
	}

	/* update aggregated per-core counts for each CPU */
	perf_cpu_map__for_each_cpu(cpu, idx, evsel->core.cpus) {
		struct perf_stat_aggr *aggr = &ps->aggr[idx];

		id = aggr_cpu_id__core(cpu, NULL);
		if (!aggr_cpu_id__equal(core_id, &id))
			continue;

		aggr->counts.val = counts.val;
		aggr->counts.ena = counts.ena;
		aggr->counts.run = counts.run;

		aggr->used = true;
	}
}

/* we have an aggr_map for cpu, but want to aggregate the counters per-core */
static void evsel__process_percore(struct evsel *evsel)
{
	struct perf_stat_evsel *ps = evsel->stats;
	struct aggr_cpu_id core_id;
	struct perf_cpu cpu;
	int idx;

	if (!evsel->percore)
		return;

	perf_cpu_map__for_each_cpu(cpu, idx, evsel->core.cpus) {
		struct perf_stat_aggr *aggr = &ps->aggr[idx];

		if (aggr->used)
			continue;

		core_id = aggr_cpu_id__core(cpu, NULL);
		evsel__update_percore_stats(evsel, &core_id);
	}
}

/* process cpu stats on per-core events */
void perf_stat_process_percore(struct perf_stat_config *config, struct evlist *evlist)
{
	struct evsel *evsel;

	if (config->aggr_mode != AGGR_NONE)
		return;

	evlist__for_each_entry(evlist, evsel)
		evsel__process_percore(evsel);
}

int perf_event__process_stat_event(struct perf_session *session,
				   union perf_event *event)
{
	struct perf_counts_values count, *ptr;
	struct perf_record_stat *st = &event->stat;
	struct evsel *counter;
	int cpu_map_idx;

	count.val = st->val;
	count.ena = st->ena;
	count.run = st->run;

	counter = evlist__id2evsel(session->evlist, st->id);
	if (!counter) {
		pr_err("Failed to resolve counter for stat event.\n");
		return -EINVAL;
	}
	cpu_map_idx = perf_cpu_map__idx(evsel__cpus(counter), (struct perf_cpu){.cpu = st->cpu});
	if (cpu_map_idx == -1) {
		pr_err("Invalid CPU %d for event %s.\n", st->cpu, evsel__name(counter));
		return -EINVAL;
	}
	ptr = perf_counts(counter->counts, cpu_map_idx, st->thread);
	if (ptr == NULL) {
		pr_err("Failed to find perf count for CPU %d thread %d on event %s.\n",
			st->cpu, st->thread, evsel__name(counter));
		return -EINVAL;
	}
	*ptr = count;
	counter->supported = true;
	return 0;
}

size_t perf_event__fprintf_stat(union perf_event *event, FILE *fp)
{
	struct perf_record_stat *st = (struct perf_record_stat *)event;
	size_t ret;

	ret  = fprintf(fp, "\n... id %" PRI_lu64 ", cpu %d, thread %d\n",
		       st->id, st->cpu, st->thread);
	ret += fprintf(fp, "... value %" PRI_lu64 ", enabled %" PRI_lu64 ", running %" PRI_lu64 "\n",
		       st->val, st->ena, st->run);

	return ret;
}

size_t perf_event__fprintf_stat_round(union perf_event *event, FILE *fp)
{
	struct perf_record_stat_round *rd = (struct perf_record_stat_round *)event;
	size_t ret;

	ret = fprintf(fp, "\n... time %" PRI_lu64 ", type %s\n", rd->time,
		      rd->type == PERF_STAT_ROUND_TYPE__FINAL ? "FINAL" : "INTERVAL");

	return ret;
}

size_t perf_event__fprintf_stat_config(union perf_event *event, FILE *fp)
{
	struct perf_stat_config sc = {};
	size_t ret;

	perf_event__read_stat_config(&sc, &event->stat_config);

	ret  = fprintf(fp, "\n");
	ret += fprintf(fp, "... aggr_mode %d\n", sc.aggr_mode);
	ret += fprintf(fp, "... scale     %d\n", sc.scale);
	ret += fprintf(fp, "... interval  %u\n", sc.interval);

	return ret;
}
