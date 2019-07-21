// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <perf/evsel.h>
#include <linux/list.h>
#include <internal/evsel.h>
#include <linux/zalloc.h>
#include <stdlib.h>
#include <internal/xyarray.h>
#include <linux/string.h>

void perf_evsel__init(struct perf_evsel *evsel, struct perf_event_attr *attr)
{
	INIT_LIST_HEAD(&evsel->node);
	evsel->attr = *attr;
}

struct perf_evsel *perf_evsel__new(struct perf_event_attr *attr)
{
	struct perf_evsel *evsel = zalloc(sizeof(*evsel));

	if (evsel != NULL)
		perf_evsel__init(evsel, attr);

	return evsel;
}

void perf_evsel__delete(struct perf_evsel *evsel)
{
	free(evsel);
}

#define FD(e, x, y) (*(int *) xyarray__entry(e->fd, x, y))

int perf_evsel__alloc_fd(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	evsel->fd = xyarray__new(ncpus, nthreads, sizeof(int));

	if (evsel->fd) {
		int cpu, thread;
		for (cpu = 0; cpu < ncpus; cpu++) {
			for (thread = 0; thread < nthreads; thread++) {
				FD(evsel, cpu, thread) = -1;
			}
		}
	}

	return evsel->fd != NULL ? 0 : -ENOMEM;
}
