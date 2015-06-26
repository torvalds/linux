#include <math.h>
#include "stat.h"
#include "evsel.h"

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

struct perf_counts *perf_counts__new(int ncpus)
{
	struct perf_counts *counts = zalloc(sizeof(*counts));

	if (counts) {
		struct xyarray *cpu;

		cpu = xyarray__new(ncpus, 1, sizeof(struct perf_counts_values));
		if (!cpu) {
			free(counts);
			return NULL;
		}

		counts->cpu = cpu;
	}

	return counts;
}

void perf_counts__delete(struct perf_counts *counts)
{
	if (counts) {
		xyarray__delete(counts->cpu);
		free(counts);
	}
}

static void perf_counts__reset(struct perf_counts *counts)
{
	xyarray__reset(counts->cpu);
}

void perf_evsel__reset_counts(struct perf_evsel *evsel)
{
	perf_counts__reset(evsel->counts);
}

int perf_evsel__alloc_counts(struct perf_evsel *evsel, int ncpus)
{
	evsel->counts = perf_counts__new(ncpus);
	return evsel->counts != NULL ? 0 : -ENOMEM;
}

void perf_evsel__free_counts(struct perf_evsel *evsel)
{
	perf_counts__delete(evsel->counts);
	evsel->counts = NULL;
}
