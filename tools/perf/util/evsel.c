#include "evsel.h"
#include "evlist.h"
#include "../perf.h"
#include "util.h"
#include "cpumap.h"
#include "thread.h"

#include <unistd.h>
#include <sys/mman.h>

#define FD(e, x, y) (*(int *)xyarray__entry(e->fd, x, y))

struct perf_evsel *perf_evsel__new(struct perf_event_attr *attr, int idx)
{
	struct perf_evsel *evsel = zalloc(sizeof(*evsel));

	if (evsel != NULL) {
		evsel->idx	   = idx;
		evsel->attr	   = *attr;
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

void perf_evsel__munmap(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	struct perf_mmap *mm;
	int cpu, thread;

	for (cpu = 0; cpu < ncpus; cpu++)
		for (thread = 0; thread < nthreads; ++thread) {
			mm = xyarray__entry(evsel->mmap, cpu, thread);
			if (mm->base != NULL) {
				munmap(mm->base, evsel->mmap_len);
				mm->base = NULL;
			}
		}
}

int perf_evsel__alloc_mmap(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	evsel->mmap = xyarray__new(ncpus, nthreads, sizeof(struct perf_mmap));
	return evsel->mmap != NULL ? 0 : -ENOMEM;
}

void perf_evsel__delete(struct perf_evsel *evsel)
{
	assert(list_empty(&evsel->node));
	xyarray__delete(evsel->fd);
	xyarray__delete(evsel->mmap);
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

static int __perf_evsel__open(struct perf_evsel *evsel, struct cpu_map *cpus,
			      struct thread_map *threads, bool group, bool inherit)
{
	int cpu, thread;

	if (evsel->fd == NULL &&
	    perf_evsel__alloc_fd(evsel, cpus->nr, threads->nr) < 0)
		return -1;

	for (cpu = 0; cpu < cpus->nr; cpu++) {
		int group_fd = -1;

		evsel->attr.inherit = (cpus->map[cpu] < 0) && inherit;

		for (thread = 0; thread < threads->nr; thread++) {
			FD(evsel, cpu, thread) = sys_perf_event_open(&evsel->attr,
								     threads->map[thread],
								     cpus->map[cpu],
								     group_fd, 0);
			if (FD(evsel, cpu, thread) < 0)
				goto out_close;

			if (group && group_fd == -1)
				group_fd = FD(evsel, cpu, thread);
		}
	}

	return 0;

out_close:
	do {
		while (--thread >= 0) {
			close(FD(evsel, cpu, thread));
			FD(evsel, cpu, thread) = -1;
		}
		thread = threads->nr;
	} while (--cpu >= 0);
	return -1;
}

static struct {
	struct cpu_map map;
	int cpus[1];
} empty_cpu_map = {
	.map.nr	= 1,
	.cpus	= { -1, },
};

static struct {
	struct thread_map map;
	int threads[1];
} empty_thread_map = {
	.map.nr	 = 1,
	.threads = { -1, },
};

int perf_evsel__open(struct perf_evsel *evsel, struct cpu_map *cpus,
		     struct thread_map *threads, bool group, bool inherit)
{
	if (cpus == NULL) {
		/* Work around old compiler warnings about strict aliasing */
		cpus = &empty_cpu_map.map;
	}

	if (threads == NULL)
		threads = &empty_thread_map.map;

	return __perf_evsel__open(evsel, cpus, threads, group, inherit);
}

int perf_evsel__open_per_cpu(struct perf_evsel *evsel,
			     struct cpu_map *cpus, bool group, bool inherit)
{
	return __perf_evsel__open(evsel, cpus, &empty_thread_map.map, group, inherit);
}

int perf_evsel__open_per_thread(struct perf_evsel *evsel,
				struct thread_map *threads, bool group, bool inherit)
{
	return __perf_evsel__open(evsel, &empty_cpu_map.map, threads, group, inherit);
}

int perf_evsel__mmap(struct perf_evsel *evsel, struct cpu_map *cpus,
		     struct thread_map *threads, int pages,
		     struct perf_evlist *evlist)
{
	unsigned int page_size = sysconf(_SC_PAGE_SIZE);
	int mask = pages * page_size - 1, cpu;
	struct perf_mmap *mm;
	int thread;

	if (evsel->mmap == NULL &&
	    perf_evsel__alloc_mmap(evsel, cpus->nr, threads->nr) < 0)
		return -ENOMEM;

	evsel->mmap_len = (pages + 1) * page_size;

	for (cpu = 0; cpu < cpus->nr; cpu++) {
		for (thread = 0; thread < threads->nr; thread++) {
			mm = xyarray__entry(evsel->mmap, cpu, thread);
			mm->prev = 0;
			mm->mask = mask;
			mm->base = mmap(NULL, evsel->mmap_len, PROT_READ,
					MAP_SHARED, FD(evsel, cpu, thread), 0);
			if (mm->base == MAP_FAILED)
				goto out_unmap;

			if (evlist != NULL)
				 perf_evlist__add_pollfd(evlist, FD(evsel, cpu, thread));
		}
	}

	return 0;

out_unmap:
	do {
		while (--thread >= 0) {
			mm = xyarray__entry(evsel->mmap, cpu, thread);
			munmap(mm->base, evsel->mmap_len);
			mm->base = NULL;
		}
		thread = threads->nr;
	} while (--cpu >= 0);

	return -1;
}
