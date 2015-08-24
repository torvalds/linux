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
	struct perf_stat *ps = evsel->priv;

	return ps->id == id;
}

#define ID(id, name) [PERF_STAT_EVSEL_ID__##id] = #name
static const char *id_str[PERF_STAT_EVSEL_ID__MAX] = {
	ID(NONE,		x),
	ID(CYCLES_IN_TX,	cpu/cycles-t/),
	ID(TRANSACTION_START,	cpu/tx-start/),
	ID(ELISION_START,	cpu/el-start/),
	ID(CYCLES_IN_TX_CP,	cpu/cycles-ct/),
};
#undef ID

void perf_stat_evsel_id_init(struct perf_evsel *evsel)
{
	struct perf_stat *ps = evsel->priv;
	int i;

	/* ps->id is 0 hence PERF_STAT_EVSEL_ID__NONE by default */

	for (i = 0; i < PERF_STAT_EVSEL_ID__MAX; i++) {
		if (!strcmp(perf_evsel__name(evsel), id_str[i])) {
			ps->id = i;
			break;
		}
	}
}

struct perf_counts *perf_counts__new(int ncpus, int nthreads)
{
	struct perf_counts *counts = zalloc(sizeof(*counts));

	if (counts) {
		struct xyarray *values;

		values = xyarray__new(ncpus, nthreads, sizeof(struct perf_counts_values));
		if (!values) {
			free(counts);
			return NULL;
		}

		counts->values = values;
	}

	return counts;
}

void perf_counts__delete(struct perf_counts *counts)
{
	if (counts) {
		xyarray__delete(counts->values);
		free(counts);
	}
}

static void perf_counts__reset(struct perf_counts *counts)
{
	xyarray__reset(counts->values);
}

void perf_evsel__reset_counts(struct perf_evsel *evsel)
{
	perf_counts__reset(evsel->counts);
}

int perf_evsel__alloc_counts(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	evsel->counts = perf_counts__new(ncpus, nthreads);
	return evsel->counts != NULL ? 0 : -ENOMEM;
}

void perf_evsel__free_counts(struct perf_evsel *evsel)
{
	perf_counts__delete(evsel->counts);
	evsel->counts = NULL;
}

void perf_evsel__reset_stat_priv(struct perf_evsel *evsel)
{
	int i;
	struct perf_stat *ps = evsel->priv;

	for (i = 0; i < 3; i++)
		init_stats(&ps->res_stats[i]);

	perf_stat_evsel_id_init(evsel);
}

int perf_evsel__alloc_stat_priv(struct perf_evsel *evsel)
{
	evsel->priv = zalloc(sizeof(struct perf_stat));
	if (evsel->priv == NULL)
		return -ENOMEM;
	perf_evsel__reset_stat_priv(evsel);
	return 0;
}

void perf_evsel__free_stat_priv(struct perf_evsel *evsel)
{
	zfree(&evsel->priv);
}

int perf_evsel__alloc_prev_raw_counts(struct perf_evsel *evsel,
				      int ncpus, int nthreads)
{
	struct perf_counts *counts;

	counts = perf_counts__new(ncpus, nthreads);
	if (counts)
		evsel->prev_raw_counts = counts;

	return counts ? 0 : -ENOMEM;
}

void perf_evsel__free_prev_raw_counts(struct perf_evsel *evsel)
{
	perf_counts__delete(evsel->prev_raw_counts);
	evsel->prev_raw_counts = NULL;
}

int perf_evsel__alloc_stats(struct perf_evsel *evsel, bool alloc_raw)
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

	evlist__for_each(evlist, evsel) {
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

	evlist__for_each(evlist, evsel) {
		perf_evsel__free_stat_priv(evsel);
		perf_evsel__free_counts(evsel);
		perf_evsel__free_prev_raw_counts(evsel);
	}
}

void perf_evlist__reset_stats(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	evlist__for_each(evlist, evsel) {
		perf_evsel__reset_stat_priv(evsel);
		perf_evsel__reset_counts(evsel);
	}
}
