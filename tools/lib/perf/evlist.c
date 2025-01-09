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
#include <internal/lib.h>
#include <linux/zalloc.h>
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
#include "internal.h"

void perf_evlist__init(struct perf_evlist *evlist)
{
	INIT_LIST_HEAD(&evlist->entries);
	evlist->nr_entries = 0;
	fdarray__init(&evlist->pollfd, 64);
	perf_evlist__reset_id_hash(evlist);
}

static void __perf_evlist__propagate_maps(struct perf_evlist *evlist,
					  struct perf_evsel *evsel)
{
	if (evsel->system_wide) {
		/* System wide: set the cpu map of the evsel to all online CPUs. */
		perf_cpu_map__put(evsel->cpus);
		evsel->cpus = perf_cpu_map__new_online_cpus();
	} else if (evlist->has_user_cpus && evsel->is_pmu_core) {
		/*
		 * User requested CPUs on a core PMU, ensure the requested CPUs
		 * are valid by intersecting with those of the PMU.
		 */
		perf_cpu_map__put(evsel->cpus);
		evsel->cpus = perf_cpu_map__intersect(evlist->user_requested_cpus, evsel->own_cpus);

		/*
		 * Empty cpu lists would eventually get opened as "any" so remove
		 * genuinely empty ones before they're opened in the wrong place.
		 */
		if (perf_cpu_map__is_empty(evsel->cpus)) {
			struct perf_evsel *next = perf_evlist__next(evlist, evsel);

			perf_evlist__remove(evlist, evsel);
			/* Keep idx contiguous */
			if (next)
				list_for_each_entry_from(next, &evlist->entries, node)
					next->idx--;
		}
	} else if (!evsel->own_cpus || evlist->has_user_cpus ||
		(!evsel->requires_cpu && perf_cpu_map__has_any_cpu(evlist->user_requested_cpus))) {
		/*
		 * The PMU didn't specify a default cpu map, this isn't a core
		 * event and the user requested CPUs or the evlist user
		 * requested CPUs have the "any CPU" (aka dummy) CPU value. In
		 * which case use the user requested CPUs rather than the PMU
		 * ones.
		 */
		perf_cpu_map__put(evsel->cpus);
		evsel->cpus = perf_cpu_map__get(evlist->user_requested_cpus);
	} else if (evsel->cpus != evsel->own_cpus) {
		/*
		 * No user requested cpu map but the PMU cpu map doesn't match
		 * the evsel's. Reset it back to the PMU cpu map.
		 */
		perf_cpu_map__put(evsel->cpus);
		evsel->cpus = perf_cpu_map__get(evsel->own_cpus);
	}

	if (evsel->system_wide) {
		perf_thread_map__put(evsel->threads);
		evsel->threads = perf_thread_map__new_dummy();
	} else {
		perf_thread_map__put(evsel->threads);
		evsel->threads = perf_thread_map__get(evlist->threads);
	}

	evlist->all_cpus = perf_cpu_map__merge(evlist->all_cpus, evsel->cpus);
}

static void perf_evlist__propagate_maps(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel, *n;

	evlist->needs_map_propagation = true;

	list_for_each_entry_safe(evsel, n, &evlist->entries, node)
		__perf_evlist__propagate_maps(evlist, evsel);
}

void perf_evlist__add(struct perf_evlist *evlist,
		      struct perf_evsel *evsel)
{
	evsel->idx = evlist->nr_entries;
	list_add_tail(&evsel->node, &evlist->entries);
	evlist->nr_entries += 1;

	if (evlist->needs_map_propagation)
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

static void perf_evlist__purge(struct perf_evlist *evlist)
{
	struct perf_evsel *pos, *n;

	perf_evlist__for_each_entry_safe(evlist, n, pos) {
		list_del_init(&pos->node);
		perf_evsel__delete(pos);
	}

	evlist->nr_entries = 0;
}

void perf_evlist__exit(struct perf_evlist *evlist)
{
	perf_cpu_map__put(evlist->user_requested_cpus);
	perf_cpu_map__put(evlist->all_cpus);
	perf_thread_map__put(evlist->threads);
	evlist->user_requested_cpus = NULL;
	evlist->all_cpus = NULL;
	evlist->threads = NULL;
	fdarray__exit(&evlist->pollfd);
}

void perf_evlist__delete(struct perf_evlist *evlist)
{
	if (evlist == NULL)
		return;

	perf_evlist__munmap(evlist);
	perf_evlist__close(evlist);
	perf_evlist__purge(evlist);
	perf_evlist__exit(evlist);
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
	if (cpus != evlist->user_requested_cpus) {
		perf_cpu_map__put(evlist->user_requested_cpus);
		evlist->user_requested_cpus = perf_cpu_map__get(cpus);
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
				 int cpu_map_idx, int thread, u64 id)
{
	int hash;
	struct perf_sample_id *sid = SID(evsel, cpu_map_idx, thread);

	sid->id = id;
	sid->evsel = evsel;
	hash = hash_64(sid->id, PERF_EVLIST__HLIST_BITS);
	hlist_add_head(&sid->node, &evlist->heads[hash]);
}

void perf_evlist__reset_id_hash(struct perf_evlist *evlist)
{
	int i;

	for (i = 0; i < PERF_EVLIST__HLIST_SIZE; ++i)
		INIT_HLIST_HEAD(&evlist->heads[i]);
}

void perf_evlist__id_add(struct perf_evlist *evlist,
			 struct perf_evsel *evsel,
			 int cpu_map_idx, int thread, u64 id)
{
	if (!SID(evsel, cpu_map_idx, thread))
		return;

	perf_evlist__id_hash(evlist, evsel, cpu_map_idx, thread, id);
	evsel->id[evsel->ids++] = id;
}

int perf_evlist__id_add_fd(struct perf_evlist *evlist,
			   struct perf_evsel *evsel,
			   int cpu_map_idx, int thread, int fd)
{
	u64 read_data[4] = { 0, };
	int id_idx = 1; /* The first entry is the counter value */
	u64 id;
	int ret;

	if (!SID(evsel, cpu_map_idx, thread))
		return -1;

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
	perf_evlist__id_add(evlist, evsel, cpu_map_idx, thread, id);
	return 0;
}

int perf_evlist__alloc_pollfd(struct perf_evlist *evlist)
{
	int nr_cpus = perf_cpu_map__nr(evlist->all_cpus);
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
			    void *ptr, short revent, enum fdarray_flags flags)
{
	int pos = fdarray__add(&evlist->pollfd, fd, revent | POLLERR | POLLHUP, flags);

	if (pos >= 0) {
		evlist->pollfd.priv[pos].ptr = ptr;
		fcntl(fd, F_SETFL, O_NONBLOCK);
	}

	return pos;
}

static void perf_evlist__munmap_filtered(struct fdarray *fda, int fd,
					 void *arg __maybe_unused)
{
	struct perf_mmap *map = fda->priv[fd].ptr;

	if (map)
		perf_mmap__put(map);
}

int perf_evlist__filter_pollfd(struct perf_evlist *evlist, short revents_and_mask)
{
	return fdarray__filter(&evlist->pollfd, revents_and_mask,
			       perf_evlist__munmap_filtered, NULL);
}

int perf_evlist__poll(struct perf_evlist *evlist, int timeout)
{
	return fdarray__poll(&evlist->pollfd, timeout);
}

static struct perf_mmap* perf_evlist__alloc_mmap(struct perf_evlist *evlist, bool overwrite)
{
	int i;
	struct perf_mmap *map;

	map = zalloc(evlist->nr_mmaps * sizeof(struct perf_mmap));
	if (!map)
		return NULL;

	for (i = 0; i < evlist->nr_mmaps; i++) {
		struct perf_mmap *prev = i ? &map[i - 1] : NULL;

		/*
		 * When the perf_mmap() call is made we grab one refcount, plus
		 * one extra to let perf_mmap__consume() get the last
		 * events after all real references (perf_mmap__get()) are
		 * dropped.
		 *
		 * Each PERF_EVENT_IOC_SET_OUTPUT points to this mmap and
		 * thus does perf_mmap__get() on it.
		 */
		perf_mmap__init(&map[i], prev, overwrite, NULL);
	}

	return map;
}

static void perf_evsel__set_sid_idx(struct perf_evsel *evsel, int idx, int cpu, int thread)
{
	struct perf_sample_id *sid = SID(evsel, cpu, thread);

	sid->idx = idx;
	sid->cpu = perf_cpu_map__cpu(evsel->cpus, cpu);
	sid->tid = perf_thread_map__pid(evsel->threads, thread);
}

static struct perf_mmap*
perf_evlist__mmap_cb_get(struct perf_evlist *evlist, bool overwrite, int idx)
{
	struct perf_mmap *maps;

	maps = overwrite ? evlist->mmap_ovw : evlist->mmap;

	if (!maps) {
		maps = perf_evlist__alloc_mmap(evlist, overwrite);
		if (!maps)
			return NULL;

		if (overwrite)
			evlist->mmap_ovw = maps;
		else
			evlist->mmap = maps;
	}

	return &maps[idx];
}

#define FD(e, x, y) (*(int *) xyarray__entry(e->fd, x, y))

static int
perf_evlist__mmap_cb_mmap(struct perf_mmap *map, struct perf_mmap_param *mp,
			  int output, struct perf_cpu cpu)
{
	return perf_mmap__mmap(map, mp, output, cpu);
}

static void perf_evlist__set_mmap_first(struct perf_evlist *evlist, struct perf_mmap *map,
					bool overwrite)
{
	if (overwrite)
		evlist->mmap_ovw_first = map;
	else
		evlist->mmap_first = map;
}

static int
mmap_per_evsel(struct perf_evlist *evlist, struct perf_evlist_mmap_ops *ops,
	       int idx, struct perf_mmap_param *mp, int cpu_idx,
	       int thread, int *_output, int *_output_overwrite, int *nr_mmaps)
{
	struct perf_cpu evlist_cpu = perf_cpu_map__cpu(evlist->all_cpus, cpu_idx);
	struct perf_evsel *evsel;
	int revent;

	perf_evlist__for_each_entry(evlist, evsel) {
		bool overwrite = evsel->attr.write_backward;
		enum fdarray_flags flgs;
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

			/*
			 * The last one will be done at perf_mmap__consume(), so that we
			 * make sure we don't prevent tools from consuming every last event in
			 * the ring buffer.
			 *
			 * I.e. we can get the POLLHUP meaning that the fd doesn't exist
			 * anymore, but the last events for it are still in the ring buffer,
			 * waiting to be consumed.
			 *
			 * Tools can chose to ignore this at their own discretion, but the
			 * evlist layer can't just drop it when filtering events in
			 * perf_evlist__filter_pollfd().
			 */
			refcount_set(&map->refcnt, 2);

			if (ops->idx)
				ops->idx(evlist, evsel, mp, idx);

			/* Debug message used by test scripts */
			pr_debug("idx %d: mmapping fd %d\n", idx, *output);
			if (ops->mmap(map, mp, *output, evlist_cpu) < 0)
				return -1;

			*nr_mmaps += 1;

			if (!idx)
				perf_evlist__set_mmap_first(evlist, map, overwrite);
		} else {
			/* Debug message used by test scripts */
			pr_debug("idx %d: set output fd %d -> %d\n", idx, fd, *output);
			if (ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, *output) != 0)
				return -1;

			perf_mmap__get(map);
		}

		revent = !overwrite ? POLLIN : 0;

		flgs = evsel->system_wide ? fdarray_flag__nonfilterable : fdarray_flag__default;
		if (perf_evlist__add_pollfd(evlist, fd, map, revent, flgs) < 0) {
			perf_mmap__put(map);
			return -1;
		}

		if (evsel->attr.read_format & PERF_FORMAT_ID) {
			if (perf_evlist__id_add_fd(evlist, evsel, cpu, thread,
						   fd) < 0)
				return -1;
			perf_evsel__set_sid_idx(evsel, idx, cpu, thread);
		}
	}

	return 0;
}

static int
mmap_per_thread(struct perf_evlist *evlist, struct perf_evlist_mmap_ops *ops,
		struct perf_mmap_param *mp)
{
	int nr_threads = perf_thread_map__nr(evlist->threads);
	int nr_cpus    = perf_cpu_map__nr(evlist->all_cpus);
	int cpu, thread, idx = 0;
	int nr_mmaps = 0;

	pr_debug("%s: nr cpu values (may include -1) %d nr threads %d\n",
		 __func__, nr_cpus, nr_threads);

	/* per-thread mmaps */
	for (thread = 0; thread < nr_threads; thread++, idx++) {
		int output = -1;
		int output_overwrite = -1;

		if (mmap_per_evsel(evlist, ops, idx, mp, 0, thread, &output,
				   &output_overwrite, &nr_mmaps))
			goto out_unmap;
	}

	/* system-wide mmaps i.e. per-cpu */
	for (cpu = 1; cpu < nr_cpus; cpu++, idx++) {
		int output = -1;
		int output_overwrite = -1;

		if (mmap_per_evsel(evlist, ops, idx, mp, cpu, 0, &output,
				   &output_overwrite, &nr_mmaps))
			goto out_unmap;
	}

	if (nr_mmaps != evlist->nr_mmaps)
		pr_err("Miscounted nr_mmaps %d vs %d\n", nr_mmaps, evlist->nr_mmaps);

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
	int nr_cpus    = perf_cpu_map__nr(evlist->all_cpus);
	int nr_mmaps = 0;
	int cpu, thread;

	pr_debug("%s: nr cpu values %d nr threads %d\n", __func__, nr_cpus, nr_threads);

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		int output = -1;
		int output_overwrite = -1;

		for (thread = 0; thread < nr_threads; thread++) {
			if (mmap_per_evsel(evlist, ops, cpu, mp, cpu,
					   thread, &output, &output_overwrite, &nr_mmaps))
				goto out_unmap;
		}
	}

	if (nr_mmaps != evlist->nr_mmaps)
		pr_err("Miscounted nr_mmaps %d vs %d\n", nr_mmaps, evlist->nr_mmaps);

	return 0;

out_unmap:
	perf_evlist__munmap(evlist);
	return -1;
}

static int perf_evlist__nr_mmaps(struct perf_evlist *evlist)
{
	int nr_mmaps;

	/* One for each CPU */
	nr_mmaps = perf_cpu_map__nr(evlist->all_cpus);
	if (perf_cpu_map__has_any_cpu_or_is_empty(evlist->all_cpus)) {
		/* Plus one for each thread */
		nr_mmaps += perf_thread_map__nr(evlist->threads);
		/* Minus the per-thread CPU (-1) */
		nr_mmaps -= 1;
	}

	return nr_mmaps;
}

int perf_evlist__mmap_ops(struct perf_evlist *evlist,
			  struct perf_evlist_mmap_ops *ops,
			  struct perf_mmap_param *mp)
{
	const struct perf_cpu_map *cpus = evlist->all_cpus;
	struct perf_evsel *evsel;

	if (!ops || !ops->get || !ops->mmap)
		return -EINVAL;

	mp->mask = evlist->mmap_len - page_size - 1;

	evlist->nr_mmaps = perf_evlist__nr_mmaps(evlist);

	perf_evlist__for_each_entry(evlist, evsel) {
		if ((evsel->attr.read_format & PERF_FORMAT_ID) &&
		    evsel->sample_id == NULL &&
		    perf_evsel__alloc_id(evsel, evsel->fd->max_x, evsel->fd->max_y) < 0)
			return -ENOMEM;
	}

	if (evlist->pollfd.entries == NULL && perf_evlist__alloc_pollfd(evlist) < 0)
		return -ENOMEM;

	if (perf_cpu_map__has_any_cpu_or_is_empty(cpus))
		return mmap_per_thread(evlist, ops, mp);

	return mmap_per_cpu(evlist, ops, mp);
}

int perf_evlist__mmap(struct perf_evlist *evlist, int pages)
{
	struct perf_mmap_param mp;
	struct perf_evlist_mmap_ops ops = {
		.get  = perf_evlist__mmap_cb_get,
		.mmap = perf_evlist__mmap_cb_mmap,
	};

	evlist->mmap_len = (pages + 1) * page_size;

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

struct perf_mmap*
perf_evlist__next_mmap(struct perf_evlist *evlist, struct perf_mmap *map,
		       bool overwrite)
{
	if (map)
		return map->next;

	return overwrite ? evlist->mmap_ovw_first : evlist->mmap_first;
}

void __perf_evlist__set_leader(struct list_head *list, struct perf_evsel *leader)
{
	struct perf_evsel *evsel;
	int n = 0;

	__perf_evlist__for_each_entry(list, evsel) {
		evsel->leader = leader;
		n++;
	}
	leader->nr_members = n;
}

void perf_evlist__set_leader(struct perf_evlist *evlist)
{
	if (evlist->nr_entries) {
		struct perf_evsel *first = list_entry(evlist->entries.next,
						struct perf_evsel, node);

		__perf_evlist__set_leader(&evlist->entries, first);
	}
}

int perf_evlist__nr_groups(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	int nr_groups = 0;

	perf_evlist__for_each_evsel(evlist, evsel) {
		/*
		 * evsels by default have a nr_members of 1, and they are their
		 * own leader. If the nr_members is >1 then this is an
		 * indication of a group.
		 */
		if (evsel->leader == evsel && evsel->nr_members > 1)
			nr_groups++;
	}
	return nr_groups;
}

void perf_evlist__go_system_wide(struct perf_evlist *evlist, struct perf_evsel *evsel)
{
	if (!evsel->system_wide) {
		evsel->system_wide = true;
		if (evlist->needs_map_propagation)
			__perf_evlist__propagate_maps(evlist, evsel);
	}
}
