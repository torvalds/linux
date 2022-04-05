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
#include <linux/zalloc.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
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
