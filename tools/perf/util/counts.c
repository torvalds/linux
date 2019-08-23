// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdlib.h>
#include "evsel.h"
#include "counts.h"
#include <linux/zalloc.h>

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
