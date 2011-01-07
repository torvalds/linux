#include "evsel.h"
#include "../perf.h"
#include "util.h"
#include "cpumap.h"
#include "thread.h"

#define FD(e, x, y) (*(int *)xyarray__entry(e->fd, x, y))

struct perf_evsel *perf_evsel__new(u32 type, u64 config, int idx)
{
	struct perf_evsel *evsel = zalloc(sizeof(*evsel));

	if (evsel != NULL) {
		evsel->idx	   = idx;
		evsel->attr.type   = type;
		evsel->attr.config = config;
		INIT_LIST_HEAD(&evsel->node);
	}

	return evsel;
}

int perf_evsel__alloc_fd(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	evsel->fd = xyarray__new(ncpus, nthreads, sizeof(int));
	return evsel->fd != NULL ? 0 : -ENOMEM;
}

int perf_evsel__alloc_counts(struct perf_evsel *evsel, int ncpus)
{
	evsel->counts = zalloc((sizeof(*evsel->counts) +
				(ncpus * sizeof(struct perf_counts_values))));
	return evsel->counts != NULL ? 0 : -ENOMEM;
}

void perf_evsel__free_fd(struct perf_evsel *evsel)
{
	xyarray__delete(evsel->fd);
	evsel->fd = NULL;
}

void perf_evsel__close_fd(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	int cpu, thread;

	for (cpu = 0; cpu < ncpus; cpu++)
		for (thread = 0; thread < nthreads; ++thread) {
			close(FD(evsel, cpu, thread));
			FD(evsel, cpu, thread) = -1;
		}
}

void perf_evsel__delete(struct perf_evsel *evsel)
{
	assert(list_empty(&evsel->node));
	xyarray__delete(evsel->fd);
	free(evsel);
}

int __perf_evsel__read_on_cpu(struct perf_evsel *evsel,
			      int cpu, int thread, bool scale)
{
	struct perf_counts_values count;
	size_t nv = scale ? 3 : 1;

	if (FD(evsel, cpu, thread) < 0)
		return -EINVAL;

	if (evsel->counts == NULL && perf_evsel__alloc_counts(evsel, cpu + 1) < 0)
		return -ENOMEM;

	if (readn(FD(evsel, cpu, thread), &count, nv * sizeof(u64)) < 0)
		return -errno;

	if (scale) {
		if (count.run == 0)
			count.val = 0;
		else if (count.run < count.ena)
			count.val = (u64)((double)count.val * count.ena / count.run + 0.5);
	} else
		count.ena = count.run = 0;

	evsel->counts->cpu[cpu] = count;
	return 0;
}

int __perf_evsel__read(struct perf_evsel *evsel,
		       int ncpus, int nthreads, bool scale)
{
	size_t nv = scale ? 3 : 1;
	int cpu, thread;
	struct perf_counts_values *aggr = &evsel->counts->aggr, count;

	aggr->val = 0;

	for (cpu = 0; cpu < ncpus; cpu++) {
		for (thread = 0; thread < nthreads; thread++) {
			if (FD(evsel, cpu, thread) < 0)
				continue;

			if (readn(FD(evsel, cpu, thread),
				  &count, nv * sizeof(u64)) < 0)
				return -errno;

			aggr->val += count.val;
			if (scale) {
				aggr->ena += count.ena;
				aggr->run += count.run;
			}
		}
	}

	evsel->counts->scaled = 0;
	if (scale) {
		if (aggr->run == 0) {
			evsel->counts->scaled = -1;
			aggr->val = 0;
			return 0;
		}

		if (aggr->run < aggr->ena) {
			evsel->counts->scaled = 1;
			aggr->val = (u64)((double)aggr->val * aggr->ena / aggr->run + 0.5);
		}
	} else
		aggr->ena = aggr->run = 0;

	return 0;
}

int perf_evsel__open_per_cpu(struct perf_evsel *evsel, struct cpu_map *cpus)
{
	int cpu;

	if (evsel->fd == NULL && perf_evsel__alloc_fd(evsel, cpus->nr, 1) < 0)
		return -1;

	for (cpu = 0; cpu < cpus->nr; cpu++) {
		FD(evsel, cpu, 0) = sys_perf_event_open(&evsel->attr, -1,
							cpus->map[cpu], -1, 0);
		if (FD(evsel, cpu, 0) < 0)
			goto out_close;
	}

	return 0;

out_close:
	while (--cpu >= 0) {
		close(FD(evsel, cpu, 0));
		FD(evsel, cpu, 0) = -1;
	}
	return -1;
}

int perf_evsel__open_per_thread(struct perf_evsel *evsel, struct thread_map *threads)
{
	int thread;

	if (evsel->fd == NULL && perf_evsel__alloc_fd(evsel, 1, threads->nr))
		return -1;

	for (thread = 0; thread < threads->nr; thread++) {
		FD(evsel, 0, thread) = sys_perf_event_open(&evsel->attr,
							   threads->map[thread], -1, -1, 0);
		if (FD(evsel, 0, thread) < 0)
			goto out_close;
	}

	return 0;

out_close:
	while (--thread >= 0) {
		close(FD(evsel, 0, thread));
		FD(evsel, 0, thread) = -1;
	}
	return -1;
}

int perf_evsel__open(struct perf_evsel *evsel, 
		     struct cpu_map *cpus, struct thread_map *threads)
{
	if (threads == NULL)
		return perf_evsel__open_per_cpu(evsel, cpus);

	return perf_evsel__open_per_thread(evsel, threads);
}
