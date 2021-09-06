// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <perf/evsel.h>
#include <perf/cpumap.h>
#include <perf/threadmap.h>
#include <linux/list.h>
#include <internal/evsel.h>
#include <linux/zalloc.h>
#include <stdlib.h>
#include <internal/xyarray.h>
#include <internal/cpumap.h>
#include <internal/mmap.h>
#include <internal/threadmap.h>
#include <internal/lib.h>
#include <linux/string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

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
#define MMAP(e, x, y) (e->mmap ? ((struct perf_mmap *) xyarray__entry(e->mmap, x, y)) : NULL)

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

static int perf_evsel__alloc_mmap(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	evsel->mmap = xyarray__new(ncpus, nthreads, sizeof(struct perf_mmap));

	return evsel->mmap != NULL ? 0 : -ENOMEM;
}

static int
sys_perf_event_open(struct perf_event_attr *attr,
		    pid_t pid, int cpu, int group_fd,
		    unsigned long flags)
{
	return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

int perf_evsel__open(struct perf_evsel *evsel, struct perf_cpu_map *cpus,
		     struct perf_thread_map *threads)
{
	int cpu, thread, err = 0;

	if (cpus == NULL) {
		static struct perf_cpu_map *empty_cpu_map;

		if (empty_cpu_map == NULL) {
			empty_cpu_map = perf_cpu_map__dummy_new();
			if (empty_cpu_map == NULL)
				return -ENOMEM;
		}

		cpus = empty_cpu_map;
	}

	if (threads == NULL) {
		static struct perf_thread_map *empty_thread_map;

		if (empty_thread_map == NULL) {
			empty_thread_map = perf_thread_map__new_dummy();
			if (empty_thread_map == NULL)
				return -ENOMEM;
		}

		threads = empty_thread_map;
	}

	if (evsel->fd == NULL &&
	    perf_evsel__alloc_fd(evsel, cpus->nr, threads->nr) < 0)
		return -ENOMEM;

	for (cpu = 0; cpu < cpus->nr; cpu++) {
		for (thread = 0; thread < threads->nr; thread++) {
			int fd;

			fd = sys_perf_event_open(&evsel->attr,
						 threads->map[thread].pid,
						 cpus->map[cpu], -1, 0);

			if (fd < 0)
				return -errno;

			FD(evsel, cpu, thread) = fd;
		}
	}

	return err;
}

static void perf_evsel__close_fd_cpu(struct perf_evsel *evsel, int cpu)
{
	int thread;

	for (thread = 0; thread < xyarray__max_y(evsel->fd); ++thread) {
		if (FD(evsel, cpu, thread) >= 0)
			close(FD(evsel, cpu, thread));
		FD(evsel, cpu, thread) = -1;
	}
}

void perf_evsel__close_fd(struct perf_evsel *evsel)
{
	int cpu;

	for (cpu = 0; cpu < xyarray__max_x(evsel->fd); cpu++)
		perf_evsel__close_fd_cpu(evsel, cpu);
}

void perf_evsel__free_fd(struct perf_evsel *evsel)
{
	xyarray__delete(evsel->fd);
	evsel->fd = NULL;
}

void perf_evsel__close(struct perf_evsel *evsel)
{
	if (evsel->fd == NULL)
		return;

	perf_evsel__close_fd(evsel);
	perf_evsel__free_fd(evsel);
}

void perf_evsel__close_cpu(struct perf_evsel *evsel, int cpu)
{
	if (evsel->fd == NULL)
		return;

	perf_evsel__close_fd_cpu(evsel, cpu);
}

void perf_evsel__munmap(struct perf_evsel *evsel)
{
	int cpu, thread;

	if (evsel->fd == NULL || evsel->mmap == NULL)
		return;

	for (cpu = 0; cpu < xyarray__max_x(evsel->fd); cpu++) {
		for (thread = 0; thread < xyarray__max_y(evsel->fd); thread++) {
			int fd = FD(evsel, cpu, thread);
			struct perf_mmap *map = MMAP(evsel, cpu, thread);

			if (fd < 0)
				continue;

			perf_mmap__munmap(map);
		}
	}

	xyarray__delete(evsel->mmap);
	evsel->mmap = NULL;
}

int perf_evsel__mmap(struct perf_evsel *evsel, int pages)
{
	int ret, cpu, thread;
	struct perf_mmap_param mp = {
		.prot = PROT_READ | PROT_WRITE,
		.mask = (pages * page_size) - 1,
	};

	if (evsel->fd == NULL || evsel->mmap)
		return -EINVAL;

	if (perf_evsel__alloc_mmap(evsel, xyarray__max_x(evsel->fd), xyarray__max_y(evsel->fd)) < 0)
		return -ENOMEM;

	for (cpu = 0; cpu < xyarray__max_x(evsel->fd); cpu++) {
		for (thread = 0; thread < xyarray__max_y(evsel->fd); thread++) {
			int fd = FD(evsel, cpu, thread);
			struct perf_mmap *map = MMAP(evsel, cpu, thread);

			if (fd < 0)
				continue;

			perf_mmap__init(map, NULL, false, NULL);

			ret = perf_mmap__mmap(map, &mp, fd, cpu);
			if (ret) {
				perf_evsel__munmap(evsel);
				return ret;
			}
		}
	}

	return 0;
}

void *perf_evsel__mmap_base(struct perf_evsel *evsel, int cpu, int thread)
{
	if (FD(evsel, cpu, thread) < 0 || MMAP(evsel, cpu, thread) == NULL)
		return NULL;

	return MMAP(evsel, cpu, thread)->base;
}

int perf_evsel__read_size(struct perf_evsel *evsel)
{
	u64 read_format = evsel->attr.read_format;
	int entry = sizeof(u64); /* value */
	int size = 0;
	int nr = 1;

	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		size += sizeof(u64);

	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		size += sizeof(u64);

	if (read_format & PERF_FORMAT_ID)
		entry += sizeof(u64);

	if (read_format & PERF_FORMAT_GROUP) {
		nr = evsel->nr_members;
		size += sizeof(u64);
	}

	size += entry * nr;
	return size;
}

int perf_evsel__read(struct perf_evsel *evsel, int cpu, int thread,
		     struct perf_counts_values *count)
{
	size_t size = perf_evsel__read_size(evsel);

	memset(count, 0, sizeof(*count));

	if (FD(evsel, cpu, thread) < 0)
		return -EINVAL;

	if (MMAP(evsel, cpu, thread) &&
	    !perf_mmap__read_self(MMAP(evsel, cpu, thread), count))
		return 0;

	if (readn(FD(evsel, cpu, thread), count->values, size) <= 0)
		return -errno;

	return 0;
}

static int perf_evsel__run_ioctl(struct perf_evsel *evsel,
				 int ioc,  void *arg,
				 int cpu)
{
	int thread;

	for (thread = 0; thread < xyarray__max_y(evsel->fd); thread++) {
		int fd = FD(evsel, cpu, thread),
		    err = ioctl(fd, ioc, arg);

		if (err)
			return err;
	}

	return 0;
}

int perf_evsel__enable_cpu(struct perf_evsel *evsel, int cpu)
{
	return perf_evsel__run_ioctl(evsel, PERF_EVENT_IOC_ENABLE, NULL, cpu);
}

int perf_evsel__enable(struct perf_evsel *evsel)
{
	int i;
	int err = 0;

	for (i = 0; i < xyarray__max_x(evsel->fd) && !err; i++)
		err = perf_evsel__run_ioctl(evsel, PERF_EVENT_IOC_ENABLE, NULL, i);
	return err;
}

int perf_evsel__disable_cpu(struct perf_evsel *evsel, int cpu)
{
	return perf_evsel__run_ioctl(evsel, PERF_EVENT_IOC_DISABLE, NULL, cpu);
}

int perf_evsel__disable(struct perf_evsel *evsel)
{
	int i;
	int err = 0;

	for (i = 0; i < xyarray__max_x(evsel->fd) && !err; i++)
		err = perf_evsel__run_ioctl(evsel, PERF_EVENT_IOC_DISABLE, NULL, i);
	return err;
}

int perf_evsel__apply_filter(struct perf_evsel *evsel, const char *filter)
{
	int err = 0, i;

	for (i = 0; i < evsel->cpus->nr && !err; i++)
		err = perf_evsel__run_ioctl(evsel,
				     PERF_EVENT_IOC_SET_FILTER,
				     (void *)filter, i);
	return err;
}

struct perf_cpu_map *perf_evsel__cpus(struct perf_evsel *evsel)
{
	return evsel->cpus;
}

struct perf_thread_map *perf_evsel__threads(struct perf_evsel *evsel)
{
	return evsel->threads;
}

struct perf_event_attr *perf_evsel__attr(struct perf_evsel *evsel)
{
	return &evsel->attr;
}

int perf_evsel__alloc_id(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	if (ncpus == 0 || nthreads == 0)
		return 0;

	if (evsel->system_wide)
		nthreads = 1;

	evsel->sample_id = xyarray__new(ncpus, nthreads, sizeof(struct perf_sample_id));
	if (evsel->sample_id == NULL)
		return -ENOMEM;

	evsel->id = zalloc(ncpus * nthreads * sizeof(u64));
	if (evsel->id == NULL) {
		xyarray__delete(evsel->sample_id);
		evsel->sample_id = NULL;
		return -ENOMEM;
	}

	return 0;
}

void perf_evsel__free_id(struct perf_evsel *evsel)
{
	xyarray__delete(evsel->sample_id);
	evsel->sample_id = NULL;
	zfree(&evsel->id);
	evsel->ids = 0;
}
