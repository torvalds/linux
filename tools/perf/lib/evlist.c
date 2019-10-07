// SPDX-License-Identifier: GPL-2.0
#include <perf/evlist.h>
#include <perf/evsel.h>
#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <sys/ioctl.h>
#include <internal/evlist.h>
#include <internal/evsel.h>
#include <internal/xyarray.h>
#include <internal/mmap.h>
#include <internal/cpumap.h>
#include <internal/threadmap.h>
#include <internal/xyarray.h>
#include <internal/lib.h>
#include <linux/zalloc.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <sys/mman.h>
#include <perf/cpumap.h>
#include <perf/threadmap.h>
#include <api/fd/array.h>

void perf_evlist__init(struct perf_evlist *evlist)
{
	int i;

	for (i = 0; i < PERF_EVLIST__HLIST_SIZE; ++i)
		INIT_HLIST_HEAD(&evlist->heads[i]);
	INIT_LIST_HEAD(&evlist->entries);
	evlist->nr_entries = 0;
}

static void __perf_evlist__propagate_maps(struct perf_evlist *evlist,
					  struct perf_evsel *evsel)
{
	/*
	 * We already have cpus for evsel (via PMU sysfs) so
	 * keep it, if there's no target cpu list defined.
	 */
	if (!evsel->own_cpus || evlist->has_user_cpus) {
		perf_cpu_map__put(evsel->cpus);
		evsel->cpus = perf_cpu_map__get(evlist->cpus);
	} else if (evsel->cpus != evsel->own_cpus) {
		perf_cpu_map__put(evsel->cpus);
		evsel->cpus = perf_cpu_map__get(evsel->own_cpus);
	}

	perf_thread_map__put(evsel->threads);
	evsel->threads = perf_thread_map__get(evlist->threads);
}

static void perf_evlist__propagate_maps(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	perf_evlist__for_each_evsel(evlist, evsel)
		__perf_evlist__propagate_maps(evlist, evsel);
}

void perf_evlist__add(struct perf_evlist *evlist,
		      struct perf_evsel *evsel)
{
	list_add_tail(&evsel->node, &evlist->entries);
	evlist->nr_entries += 1;
	__perf_evlist__propagate_maps(evlist, evsel);
}

void perf_evlist__remove(struct perf_evlist *evlist,
			 struct perf_evsel *evsel)
{
	list_del_init(&evsel->node);
	evlist->nr_entries -= 1;
}

struct perf_evlist *perf_evlist__new(void)
{
	struct perf_evlist *evlist = zalloc(sizeof(*evlist));

	if (evlist != NULL)
		perf_evlist__init(evlist);

	return evlist;
}

struct perf_evsel *
perf_evlist__next(struct perf_evlist *evlist, struct perf_evsel *prev)
{
	struct perf_evsel *next;

	if (!prev) {
		next = list_first_entry(&evlist->entries,
					struct perf_evsel,
					node);
	} else {
		next = list_next_entry(prev, node);
	}

	/* Empty list is noticed here so don't need checking on entry. */
	if (&next->node == &evlist->entries)
		return NULL;

	return next;
}

void perf_evlist__delete(struct perf_evlist *evlist)
{
	if (evlist == NULL)
		return;

	perf_evlist__munmap(evlist);
	free(evlist);
}

void perf_evlist__set_maps(struct perf_evlist *evlist,
			   struct perf_cpu_map *cpus,
			   struct perf_thread_map *threads)
{
	/*
	 * Allow for the possibility that one or another of the maps isn't being
	 * changed i.e. don't put it.  Note we are assuming the maps that are
	 * being applied are brand new and evlist is taking ownership of the
	 * original reference count of 1.  If that is not the case it is up to
	 * the caller to increase the reference count.
	 */
	if (cpus != evlist->cpus) {
		perf_cpu_map__put(evlist->cpus);
		evlist->cpus = perf_cpu_map__get(cpus);
	}

	if (threads != evlist->threads) {
		perf_thread_map__put(evlist->threads);
		evlist->threads = perf_thread_map__get(threads);
	}

	perf_evlist__propagate_maps(evlist);
}

int perf_evlist__open(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	int err;

	perf_evlist__for_each_entry(evlist, evsel) {
		err = perf_evsel__open(evsel, evsel->cpus, evsel->threads);
		if (err < 0)
			goto out_err;
	}

	return 0;

out_err:
	perf_evlist__close(evlist);
	return err;
}

void perf_evlist__close(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	perf_evlist__for_each_entry_reverse(evlist, evsel)
		perf_evsel__close(evsel);
}

void perf_evlist__enable(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	perf_evlist__for_each_entry(evlist, evsel)
		perf_evsel__enable(evsel);
}

void perf_evlist__disable(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	perf_evlist__for_each_entry(evlist, evsel)
		perf_evsel__disable(evsel);
}

u64 perf_evlist__read_format(struct perf_evlist *evlist)
{
	struct perf_evsel *first = perf_evlist__first(evlist);

	return first->attr.read_format;
}

#define SID(e, x, y) xyarray__entry(e->sample_id, x, y)

static void perf_evlist__id_hash(struct perf_evlist *evlist,
				 struct perf_evsel *evsel,
				 int cpu, int thread, u64 id)
{
	int hash;
	struct perf_sample_id *sid = SID(evsel, cpu, thread);

	sid->id = id;
	sid->evsel = evsel;
	hash = hash_64(sid->id, PERF_EVLIST__HLIST_BITS);
	hlist_add_head(&sid->node, &evlist->heads[hash]);
}

void perf_evlist__id_add(struct perf_evlist *evlist,
			 struct perf_evsel *evsel,
			 int cpu, int thread, u64 id)
{
	perf_evlist__id_hash(evlist, evsel, cpu, thread, id);
	evsel->id[evsel->ids++] = id;
}

int perf_evlist__id_add_fd(struct perf_evlist *evlist,
			   struct perf_evsel *evsel,
			   int cpu, int thread, int fd)
{
	u64 read_data[4] = { 0, };
	int id_idx = 1; /* The first entry is the counter value */
	u64 id;
	int ret;

	ret = ioctl(fd, PERF_EVENT_IOC_ID, &id);
	if (!ret)
		goto add;

	if (errno != ENOTTY)
		return -1;

	/* Legacy way to get event id.. All hail to old kernels! */

	/*
	 * This way does not work with group format read, so bail
	 * out in that case.
	 */
	if (perf_evlist__read_format(evlist) & PERF_FORMAT_GROUP)
		return -1;

	if (!(evsel->attr.read_format & PERF_FORMAT_ID) ||
	    read(fd, &read_data, sizeof(read_data)) == -1)
		return -1;

	if (evsel->attr.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		++id_idx;
	if (evsel->attr.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		++id_idx;

	id = read_data[id_idx];

add:
	perf_evlist__id_add(evlist, evsel, cpu, thread, id);
	return 0;
}

int perf_evlist__alloc_pollfd(struct perf_evlist *evlist)
{
	int nr_cpus = perf_cpu_map__nr(evlist->cpus);
	int nr_threads = perf_thread_map__nr(evlist->threads);
	int nfds = 0;
	struct perf_evsel *evsel;

	perf_evlist__for_each_entry(evlist, evsel) {
		if (evsel->system_wide)
			nfds += nr_cpus;
		else
			nfds += nr_cpus * nr_threads;
	}

	if (fdarray__available_entries(&evlist->pollfd) < nfds &&
	    fdarray__grow(&evlist->pollfd, nfds) < 0)
		return -ENOMEM;

	return 0;
}

int perf_evlist__add_pollfd(struct perf_evlist *evlist, int fd,
			    void *ptr, short revent)
{
	int pos = fdarray__add(&evlist->pollfd, fd, revent | POLLERR | POLLHUP);

	if (pos >= 0) {
		evlist->pollfd.priv[pos].ptr = ptr;
		fcntl(fd, F_SETFL, O_NONBLOCK);
	}

	return pos;
}

int perf_evlist__poll(struct perf_evlist *evlist, int timeout)
{
	return fdarray__poll(&evlist->pollfd, timeout);
}

static struct perf_mmap* perf_evlist__alloc_mmap(struct perf_evlist *evlist, bool overwrite)
{
	int i;
	struct perf_mmap *map;

	evlist->nr_mmaps = perf_cpu_map__nr(evlist->cpus);
	if (perf_cpu_map__empty(evlist->cpus))
		evlist->nr_mmaps = perf_thread_map__nr(evlist->threads);

	map = zalloc(evlist->nr_mmaps * sizeof(struct perf_mmap));
	if (!map)
		return NULL;

	for (i = 0; i < evlist->nr_mmaps; i++) {
		/*
		 * When the perf_mmap() call is made we grab one refcount, plus
		 * one extra to let perf_mmap__consume() get the last
		 * events after all real references (perf_mmap__get()) are
		 * dropped.
		 *
		 * Each PERF_EVENT_IOC_SET_OUTPUT points to this mmap and
		 * thus does perf_mmap__get() on it.
		 */
		perf_mmap__init(&map[i], overwrite, NULL);
	}

	return map;
}

static void perf_evlist__set_sid_idx(struct perf_evlist *evlist,
				     struct perf_evsel *evsel, int idx, int cpu,
				     int thread)
{
	struct perf_sample_id *sid = SID(evsel, cpu, thread);

	sid->idx = idx;
	if (evlist->cpus && cpu >= 0)
		sid->cpu = evlist->cpus->map[cpu];
	else
		sid->cpu = -1;
	if (!evsel->system_wide && evlist->threads && thread >= 0)
		sid->tid = perf_thread_map__pid(evlist->threads, thread);
	else
		sid->tid = -1;
}

static struct perf_mmap*
perf_evlist__mmap_cb_get(struct perf_evlist *evlist, bool overwrite, int idx)
{
	struct perf_mmap *map = &evlist->mmap[idx];

	if (overwrite) {
		if (!evlist->mmap_ovw) {
			evlist->mmap_ovw = perf_evlist__alloc_mmap(evlist, true);
			if (!evlist->mmap_ovw)
				return NULL;
		}
		map = &evlist->mmap_ovw[idx];
	}

	return map;
}

#define FD(e, x, y) (*(int *) xyarray__entry(e->fd, x, y))

static int
mmap_per_evsel(struct perf_evlist *evlist, struct perf_evlist_mmap_ops *ops,
	       int idx, struct perf_mmap_param *mp, int cpu_idx,
	       int thread, int *_output, int *_output_overwrite)
{
	int evlist_cpu = perf_cpu_map__cpu(evlist->cpus, cpu_idx);
	struct perf_evsel *evsel;
	int revent;

	perf_evlist__for_each_entry(evlist, evsel) {
		bool overwrite = evsel->attr.write_backward;
		struct perf_mmap *map;
		int *output, fd, cpu;

		if (evsel->system_wide && thread)
			continue;

		cpu = perf_cpu_map__idx(evsel->cpus, evlist_cpu);
		if (cpu == -1)
			continue;

		map = ops->get(evlist, overwrite, idx);
		if (map == NULL)
			return -ENOMEM;

		if (overwrite) {
			mp->prot = PROT_READ;
			output   = _output_overwrite;
		} else {
			mp->prot = PROT_READ | PROT_WRITE;
			output   = _output;
		}

		fd = FD(evsel, cpu, thread);

		if (*output == -1) {
			*output = fd;

			if (perf_mmap__mmap(map, mp, *output, evlist_cpu) < 0)
				return -1;
		} else {
			if (ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, *output) != 0)
				return -1;

			perf_mmap__get(map);
		}

		revent = !overwrite ? POLLIN : 0;

		if (!evsel->system_wide &&
		    perf_evlist__add_pollfd(evlist, fd, map, revent) < 0) {
			perf_mmap__put(map);
			return -1;
		}

		if (evsel->attr.read_format & PERF_FORMAT_ID) {
			if (perf_evlist__id_add_fd(evlist, evsel, cpu, thread,
						   fd) < 0)
				return -1;
			perf_evlist__set_sid_idx(evlist, evsel, idx, cpu,
						 thread);
		}
	}

	return 0;
}

static int
mmap_per_thread(struct perf_evlist *evlist, struct perf_evlist_mmap_ops *ops,
		struct perf_mmap_param *mp)
{
	int thread;
	int nr_threads = perf_thread_map__nr(evlist->threads);

	for (thread = 0; thread < nr_threads; thread++) {
		int output = -1;
		int output_overwrite = -1;

		if (ops->idx)
			ops->idx(evlist, mp, thread, false);

		if (mmap_per_evsel(evlist, ops, thread, mp, 0, thread,
				   &output, &output_overwrite))
			goto out_unmap;
	}

	return 0;

out_unmap:
	perf_evlist__munmap(evlist);
	return -1;
}

static int
mmap_per_cpu(struct perf_evlist *evlist, struct perf_evlist_mmap_ops *ops,
	     struct perf_mmap_param *mp)
{
	int nr_threads = perf_thread_map__nr(evlist->threads);
	int nr_cpus    = perf_cpu_map__nr(evlist->cpus);
	int cpu, thread;

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		int output = -1;
		int output_overwrite = -1;

		if (ops->idx)
			ops->idx(evlist, mp, cpu, true);

		for (thread = 0; thread < nr_threads; thread++) {
			if (mmap_per_evsel(evlist, ops, cpu, mp, cpu,
					   thread, &output, &output_overwrite))
				goto out_unmap;
		}
	}

	return 0;

out_unmap:
	perf_evlist__munmap(evlist);
	return -1;
}

int perf_evlist__mmap_ops(struct perf_evlist *evlist,
			  struct perf_evlist_mmap_ops *ops,
			  struct perf_mmap_param *mp)
{
	struct perf_evsel *evsel;
	const struct perf_cpu_map *cpus = evlist->cpus;
	const struct perf_thread_map *threads = evlist->threads;

	if (!ops || !ops->get)
		return -EINVAL;

	if (!evlist->mmap)
		evlist->mmap = perf_evlist__alloc_mmap(evlist, false);
	if (!evlist->mmap)
		return -ENOMEM;

	perf_evlist__for_each_entry(evlist, evsel) {
		if ((evsel->attr.read_format & PERF_FORMAT_ID) &&
		    evsel->sample_id == NULL &&
		    perf_evsel__alloc_id(evsel, perf_cpu_map__nr(cpus), threads->nr) < 0)
			return -ENOMEM;
	}

	if (perf_cpu_map__empty(cpus))
		return mmap_per_thread(evlist, ops, mp);

	return mmap_per_cpu(evlist, ops, mp);
}

int perf_evlist__mmap(struct perf_evlist *evlist, int pages)
{
	struct perf_mmap_param mp;
	struct perf_evlist_mmap_ops ops = {
		.get = perf_evlist__mmap_cb_get,
	};

	evlist->mmap_len = (pages + 1) * page_size;
	mp.mask = evlist->mmap_len - page_size - 1;

	return perf_evlist__mmap_ops(evlist, &ops, &mp);
}

void perf_evlist__munmap(struct perf_evlist *evlist)
{
	int i;

	if (evlist->mmap) {
		for (i = 0; i < evlist->nr_mmaps; i++)
			perf_mmap__munmap(&evlist->mmap[i]);
	}

	if (evlist->mmap_ovw) {
		for (i = 0; i < evlist->nr_mmaps; i++)
			perf_mmap__munmap(&evlist->mmap_ovw[i]);
	}

	zfree(&evlist->mmap);
	zfree(&evlist->mmap_ovw);
}
