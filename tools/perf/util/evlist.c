/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-{top,stat,record}.c, see those files for further
 * copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */
#include "util.h"
#include <api/fs/debugfs.h>
#include <poll.h>
#include "cpumap.h"
#include "thread_map.h"
#include "target.h"
#include "evlist.h"
#include "evsel.h"
#include "debug.h"
#include <unistd.h>

#include "parse-events.h"
#include "parse-options.h"

#include <sys/mman.h>

#include <linux/bitops.h>
#include <linux/hash.h>

static void perf_evlist__mmap_put(struct perf_evlist *evlist, int idx);
static void __perf_evlist__munmap(struct perf_evlist *evlist, int idx);

#define FD(e, x, y) (*(int *)xyarray__entry(e->fd, x, y))
#define SID(e, x, y) xyarray__entry(e->sample_id, x, y)

void perf_evlist__init(struct perf_evlist *evlist, struct cpu_map *cpus,
		       struct thread_map *threads)
{
	int i;

	for (i = 0; i < PERF_EVLIST__HLIST_SIZE; ++i)
		INIT_HLIST_HEAD(&evlist->heads[i]);
	INIT_LIST_HEAD(&evlist->entries);
	perf_evlist__set_maps(evlist, cpus, threads);
	fdarray__init(&evlist->pollfd, 64);
	evlist->workload.pid = -1;
}

struct perf_evlist *perf_evlist__new(void)
{
	struct perf_evlist *evlist = zalloc(sizeof(*evlist));

	if (evlist != NULL)
		perf_evlist__init(evlist, NULL, NULL);

	return evlist;
}

struct perf_evlist *perf_evlist__new_default(void)
{
	struct perf_evlist *evlist = perf_evlist__new();

	if (evlist && perf_evlist__add_default(evlist)) {
		perf_evlist__delete(evlist);
		evlist = NULL;
	}

	return evlist;
}

/**
 * perf_evlist__set_id_pos - set the positions of event ids.
 * @evlist: selected event list
 *
 * Events with compatible sample types all have the same id_pos
 * and is_pos.  For convenience, put a copy on evlist.
 */
void perf_evlist__set_id_pos(struct perf_evlist *evlist)
{
	struct perf_evsel *first = perf_evlist__first(evlist);

	evlist->id_pos = first->id_pos;
	evlist->is_pos = first->is_pos;
}

static void perf_evlist__update_id_pos(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	evlist__for_each(evlist, evsel)
		perf_evsel__calc_id_pos(evsel);

	perf_evlist__set_id_pos(evlist);
}

static void perf_evlist__purge(struct perf_evlist *evlist)
{
	struct perf_evsel *pos, *n;

	evlist__for_each_safe(evlist, n, pos) {
		list_del_init(&pos->node);
		perf_evsel__delete(pos);
	}

	evlist->nr_entries = 0;
}

void perf_evlist__exit(struct perf_evlist *evlist)
{
	zfree(&evlist->mmap);
	fdarray__exit(&evlist->pollfd);
}

void perf_evlist__delete(struct perf_evlist *evlist)
{
	perf_evlist__munmap(evlist);
	perf_evlist__close(evlist);
	cpu_map__delete(evlist->cpus);
	thread_map__delete(evlist->threads);
	evlist->cpus = NULL;
	evlist->threads = NULL;
	perf_evlist__purge(evlist);
	perf_evlist__exit(evlist);
	free(evlist);
}

void perf_evlist__add(struct perf_evlist *evlist, struct perf_evsel *entry)
{
	list_add_tail(&entry->node, &evlist->entries);
	entry->idx = evlist->nr_entries;
	entry->tracking = !entry->idx;

	if (!evlist->nr_entries++)
		perf_evlist__set_id_pos(evlist);
}

void perf_evlist__splice_list_tail(struct perf_evlist *evlist,
				   struct list_head *list,
				   int nr_entries)
{
	bool set_id_pos = !evlist->nr_entries;

	list_splice_tail(list, &evlist->entries);
	evlist->nr_entries += nr_entries;
	if (set_id_pos)
		perf_evlist__set_id_pos(evlist);
}

void __perf_evlist__set_leader(struct list_head *list)
{
	struct perf_evsel *evsel, *leader;

	leader = list_entry(list->next, struct perf_evsel, node);
	evsel = list_entry(list->prev, struct perf_evsel, node);

	leader->nr_members = evsel->idx - leader->idx + 1;

	__evlist__for_each(list, evsel) {
		evsel->leader = leader;
	}
}

void perf_evlist__set_leader(struct perf_evlist *evlist)
{
	if (evlist->nr_entries) {
		evlist->nr_groups = evlist->nr_entries > 1 ? 1 : 0;
		__perf_evlist__set_leader(&evlist->entries);
	}
}

int perf_evlist__add_default(struct perf_evlist *evlist)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
	};
	struct perf_evsel *evsel;

	event_attr_init(&attr);

	evsel = perf_evsel__new(&attr);
	if (evsel == NULL)
		goto error;

	/* use strdup() because free(evsel) assumes name is allocated */
	evsel->name = strdup("cycles");
	if (!evsel->name)
		goto error_free;

	perf_evlist__add(evlist, evsel);
	return 0;
error_free:
	perf_evsel__delete(evsel);
error:
	return -ENOMEM;
}

static int perf_evlist__add_attrs(struct perf_evlist *evlist,
				  struct perf_event_attr *attrs, size_t nr_attrs)
{
	struct perf_evsel *evsel, *n;
	LIST_HEAD(head);
	size_t i;

	for (i = 0; i < nr_attrs; i++) {
		evsel = perf_evsel__new_idx(attrs + i, evlist->nr_entries + i);
		if (evsel == NULL)
			goto out_delete_partial_list;
		list_add_tail(&evsel->node, &head);
	}

	perf_evlist__splice_list_tail(evlist, &head, nr_attrs);

	return 0;

out_delete_partial_list:
	__evlist__for_each_safe(&head, n, evsel)
		perf_evsel__delete(evsel);
	return -1;
}

int __perf_evlist__add_default_attrs(struct perf_evlist *evlist,
				     struct perf_event_attr *attrs, size_t nr_attrs)
{
	size_t i;

	for (i = 0; i < nr_attrs; i++)
		event_attr_init(attrs + i);

	return perf_evlist__add_attrs(evlist, attrs, nr_attrs);
}

struct perf_evsel *
perf_evlist__find_tracepoint_by_id(struct perf_evlist *evlist, int id)
{
	struct perf_evsel *evsel;

	evlist__for_each(evlist, evsel) {
		if (evsel->attr.type   == PERF_TYPE_TRACEPOINT &&
		    (int)evsel->attr.config == id)
			return evsel;
	}

	return NULL;
}

struct perf_evsel *
perf_evlist__find_tracepoint_by_name(struct perf_evlist *evlist,
				     const char *name)
{
	struct perf_evsel *evsel;

	evlist__for_each(evlist, evsel) {
		if ((evsel->attr.type == PERF_TYPE_TRACEPOINT) &&
		    (strcmp(evsel->name, name) == 0))
			return evsel;
	}

	return NULL;
}

int perf_evlist__add_newtp(struct perf_evlist *evlist,
			   const char *sys, const char *name, void *handler)
{
	struct perf_evsel *evsel = perf_evsel__newtp(sys, name);

	if (evsel == NULL)
		return -1;

	evsel->handler = handler;
	perf_evlist__add(evlist, evsel);
	return 0;
}

static int perf_evlist__nr_threads(struct perf_evlist *evlist,
				   struct perf_evsel *evsel)
{
	if (evsel->system_wide)
		return 1;
	else
		return thread_map__nr(evlist->threads);
}

void perf_evlist__disable(struct perf_evlist *evlist)
{
	int cpu, thread;
	struct perf_evsel *pos;
	int nr_cpus = cpu_map__nr(evlist->cpus);
	int nr_threads;

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		evlist__for_each(evlist, pos) {
			if (!perf_evsel__is_group_leader(pos) || !pos->fd)
				continue;
			nr_threads = perf_evlist__nr_threads(evlist, pos);
			for (thread = 0; thread < nr_threads; thread++)
				ioctl(FD(pos, cpu, thread),
				      PERF_EVENT_IOC_DISABLE, 0);
		}
	}
}

void perf_evlist__enable(struct perf_evlist *evlist)
{
	int cpu, thread;
	struct perf_evsel *pos;
	int nr_cpus = cpu_map__nr(evlist->cpus);
	int nr_threads;

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		evlist__for_each(evlist, pos) {
			if (!perf_evsel__is_group_leader(pos) || !pos->fd)
				continue;
			nr_threads = perf_evlist__nr_threads(evlist, pos);
			for (thread = 0; thread < nr_threads; thread++)
				ioctl(FD(pos, cpu, thread),
				      PERF_EVENT_IOC_ENABLE, 0);
		}
	}
}

int perf_evlist__disable_event(struct perf_evlist *evlist,
			       struct perf_evsel *evsel)
{
	int cpu, thread, err;
	int nr_cpus = cpu_map__nr(evlist->cpus);
	int nr_threads = perf_evlist__nr_threads(evlist, evsel);

	if (!evsel->fd)
		return 0;

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		for (thread = 0; thread < nr_threads; thread++) {
			err = ioctl(FD(evsel, cpu, thread),
				    PERF_EVENT_IOC_DISABLE, 0);
			if (err)
				return err;
		}
	}
	return 0;
}

int perf_evlist__enable_event(struct perf_evlist *evlist,
			      struct perf_evsel *evsel)
{
	int cpu, thread, err;
	int nr_cpus = cpu_map__nr(evlist->cpus);
	int nr_threads = perf_evlist__nr_threads(evlist, evsel);

	if (!evsel->fd)
		return -EINVAL;

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		for (thread = 0; thread < nr_threads; thread++) {
			err = ioctl(FD(evsel, cpu, thread),
				    PERF_EVENT_IOC_ENABLE, 0);
			if (err)
				return err;
		}
	}
	return 0;
}

static int perf_evlist__enable_event_cpu(struct perf_evlist *evlist,
					 struct perf_evsel *evsel, int cpu)
{
	int thread, err;
	int nr_threads = perf_evlist__nr_threads(evlist, evsel);

	if (!evsel->fd)
		return -EINVAL;

	for (thread = 0; thread < nr_threads; thread++) {
		err = ioctl(FD(evsel, cpu, thread),
			    PERF_EVENT_IOC_ENABLE, 0);
		if (err)
			return err;
	}
	return 0;
}

static int perf_evlist__enable_event_thread(struct perf_evlist *evlist,
					    struct perf_evsel *evsel,
					    int thread)
{
	int cpu, err;
	int nr_cpus = cpu_map__nr(evlist->cpus);

	if (!evsel->fd)
		return -EINVAL;

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		err = ioctl(FD(evsel, cpu, thread), PERF_EVENT_IOC_ENABLE, 0);
		if (err)
			return err;
	}
	return 0;
}

int perf_evlist__enable_event_idx(struct perf_evlist *evlist,
				  struct perf_evsel *evsel, int idx)
{
	bool per_cpu_mmaps = !cpu_map__empty(evlist->cpus);

	if (per_cpu_mmaps)
		return perf_evlist__enable_event_cpu(evlist, evsel, idx);
	else
		return perf_evlist__enable_event_thread(evlist, evsel, idx);
}

int perf_evlist__alloc_pollfd(struct perf_evlist *evlist)
{
	int nr_cpus = cpu_map__nr(evlist->cpus);
	int nr_threads = thread_map__nr(evlist->threads);
	int nfds = 0;
	struct perf_evsel *evsel;

	list_for_each_entry(evsel, &evlist->entries, node) {
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

static int __perf_evlist__add_pollfd(struct perf_evlist *evlist, int fd, int idx)
{
	int pos = fdarray__add(&evlist->pollfd, fd, POLLIN | POLLERR | POLLHUP);
	/*
	 * Save the idx so that when we filter out fds POLLHUP'ed we can
	 * close the associated evlist->mmap[] entry.
	 */
	if (pos >= 0) {
		evlist->pollfd.priv[pos].idx = idx;

		fcntl(fd, F_SETFL, O_NONBLOCK);
	}

	return pos;
}

int perf_evlist__add_pollfd(struct perf_evlist *evlist, int fd)
{
	return __perf_evlist__add_pollfd(evlist, fd, -1);
}

static void perf_evlist__munmap_filtered(struct fdarray *fda, int fd)
{
	struct perf_evlist *evlist = container_of(fda, struct perf_evlist, pollfd);

	perf_evlist__mmap_put(evlist, fda->priv[fd].idx);
}

int perf_evlist__filter_pollfd(struct perf_evlist *evlist, short revents_and_mask)
{
	return fdarray__filter(&evlist->pollfd, revents_and_mask,
			       perf_evlist__munmap_filtered);
}

int perf_evlist__poll(struct perf_evlist *evlist, int timeout)
{
	return fdarray__poll(&evlist->pollfd, timeout);
}

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

void perf_evlist__id_add(struct perf_evlist *evlist, struct perf_evsel *evsel,
			 int cpu, int thread, u64 id)
{
	perf_evlist__id_hash(evlist, evsel, cpu, thread, id);
	evsel->id[evsel->ids++] = id;
}

static int perf_evlist__id_add_fd(struct perf_evlist *evlist,
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

struct perf_sample_id *perf_evlist__id2sid(struct perf_evlist *evlist, u64 id)
{
	struct hlist_head *head;
	struct perf_sample_id *sid;
	int hash;

	hash = hash_64(id, PERF_EVLIST__HLIST_BITS);
	head = &evlist->heads[hash];

	hlist_for_each_entry(sid, head, node)
		if (sid->id == id)
			return sid;

	return NULL;
}

struct perf_evsel *perf_evlist__id2evsel(struct perf_evlist *evlist, u64 id)
{
	struct perf_sample_id *sid;

	if (evlist->nr_entries == 1)
		return perf_evlist__first(evlist);

	sid = perf_evlist__id2sid(evlist, id);
	if (sid)
		return sid->evsel;

	if (!perf_evlist__sample_id_all(evlist))
		return perf_evlist__first(evlist);

	return NULL;
}

static int perf_evlist__event2id(struct perf_evlist *evlist,
				 union perf_event *event, u64 *id)
{
	const u64 *array = event->sample.array;
	ssize_t n;

	n = (event->header.size - sizeof(event->header)) >> 3;

	if (event->header.type == PERF_RECORD_SAMPLE) {
		if (evlist->id_pos >= n)
			return -1;
		*id = array[evlist->id_pos];
	} else {
		if (evlist->is_pos > n)
			return -1;
		n -= evlist->is_pos;
		*id = array[n];
	}
	return 0;
}

static struct perf_evsel *perf_evlist__event2evsel(struct perf_evlist *evlist,
						   union perf_event *event)
{
	struct perf_evsel *first = perf_evlist__first(evlist);
	struct hlist_head *head;
	struct perf_sample_id *sid;
	int hash;
	u64 id;

	if (evlist->nr_entries == 1)
		return first;

	if (!first->attr.sample_id_all &&
	    event->header.type != PERF_RECORD_SAMPLE)
		return first;

	if (perf_evlist__event2id(evlist, event, &id))
		return NULL;

	/* Synthesized events have an id of zero */
	if (!id)
		return first;

	hash = hash_64(id, PERF_EVLIST__HLIST_BITS);
	head = &evlist->heads[hash];

	hlist_for_each_entry(sid, head, node) {
		if (sid->id == id)
			return sid->evsel;
	}
	return NULL;
}

union perf_event *perf_evlist__mmap_read(struct perf_evlist *evlist, int idx)
{
	struct perf_mmap *md = &evlist->mmap[idx];
	unsigned int head = perf_mmap__read_head(md);
	unsigned int old = md->prev;
	unsigned char *data = md->base + page_size;
	union perf_event *event = NULL;

	if (evlist->overwrite) {
		/*
		 * If we're further behind than half the buffer, there's a chance
		 * the writer will bite our tail and mess up the samples under us.
		 *
		 * If we somehow ended up ahead of the head, we got messed up.
		 *
		 * In either case, truncate and restart at head.
		 */
		int diff = head - old;
		if (diff > md->mask / 2 || diff < 0) {
			fprintf(stderr, "WARNING: failed to keep up with mmap data.\n");

			/*
			 * head points to a known good entry, start there.
			 */
			old = head;
		}
	}

	if (old != head) {
		size_t size;

		event = (union perf_event *)&data[old & md->mask];
		size = event->header.size;

		/*
		 * Event straddles the mmap boundary -- header should always
		 * be inside due to u64 alignment of output.
		 */
		if ((old & md->mask) + size != ((old + size) & md->mask)) {
			unsigned int offset = old;
			unsigned int len = min(sizeof(*event), size), cpy;
			void *dst = md->event_copy;

			do {
				cpy = min(md->mask + 1 - (offset & md->mask), len);
				memcpy(dst, &data[offset & md->mask], cpy);
				offset += cpy;
				dst += cpy;
				len -= cpy;
			} while (len);

			event = (union perf_event *) md->event_copy;
		}

		old += size;
	}

	md->prev = old;

	return event;
}

static bool perf_mmap__empty(struct perf_mmap *md)
{
	return perf_mmap__read_head(md) != md->prev;
}

static void perf_evlist__mmap_get(struct perf_evlist *evlist, int idx)
{
	++evlist->mmap[idx].refcnt;
}

static void perf_evlist__mmap_put(struct perf_evlist *evlist, int idx)
{
	BUG_ON(evlist->mmap[idx].refcnt == 0);

	if (--evlist->mmap[idx].refcnt == 0)
		__perf_evlist__munmap(evlist, idx);
}

void perf_evlist__mmap_consume(struct perf_evlist *evlist, int idx)
{
	struct perf_mmap *md = &evlist->mmap[idx];

	if (!evlist->overwrite) {
		unsigned int old = md->prev;

		perf_mmap__write_tail(md, old);
	}

	if (md->refcnt == 1 && perf_mmap__empty(md))
		perf_evlist__mmap_put(evlist, idx);
}

static void __perf_evlist__munmap(struct perf_evlist *evlist, int idx)
{
	if (evlist->mmap[idx].base != NULL) {
		munmap(evlist->mmap[idx].base, evlist->mmap_len);
		evlist->mmap[idx].base = NULL;
		evlist->mmap[idx].refcnt = 0;
	}
}

void perf_evlist__munmap(struct perf_evlist *evlist)
{
	int i;

	if (evlist->mmap == NULL)
		return;

	for (i = 0; i < evlist->nr_mmaps; i++)
		__perf_evlist__munmap(evlist, i);

	zfree(&evlist->mmap);
}

static int perf_evlist__alloc_mmap(struct perf_evlist *evlist)
{
	evlist->nr_mmaps = cpu_map__nr(evlist->cpus);
	if (cpu_map__empty(evlist->cpus))
		evlist->nr_mmaps = thread_map__nr(evlist->threads);
	evlist->mmap = zalloc(evlist->nr_mmaps * sizeof(struct perf_mmap));
	return evlist->mmap != NULL ? 0 : -ENOMEM;
}

struct mmap_params {
	int prot;
	int mask;
};

static int __perf_evlist__mmap(struct perf_evlist *evlist, int idx,
			       struct mmap_params *mp, int fd)
{
	/*
	 * The last one will be done at perf_evlist__mmap_consume(), so that we
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
	evlist->mmap[idx].refcnt = 2;
	evlist->mmap[idx].prev = 0;
	evlist->mmap[idx].mask = mp->mask;
	evlist->mmap[idx].base = mmap(NULL, evlist->mmap_len, mp->prot,
				      MAP_SHARED, fd, 0);
	if (evlist->mmap[idx].base == MAP_FAILED) {
		pr_debug2("failed to mmap perf event ring buffer, error %d\n",
			  errno);
		evlist->mmap[idx].base = NULL;
		return -1;
	}

	return 0;
}

static int perf_evlist__mmap_per_evsel(struct perf_evlist *evlist, int idx,
				       struct mmap_params *mp, int cpu,
				       int thread, int *output)
{
	struct perf_evsel *evsel;

	evlist__for_each(evlist, evsel) {
		int fd;

		if (evsel->system_wide && thread)
			continue;

		fd = FD(evsel, cpu, thread);

		if (*output == -1) {
			*output = fd;
			if (__perf_evlist__mmap(evlist, idx, mp, *output) < 0)
				return -1;
		} else {
			if (ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, *output) != 0)
				return -1;

			perf_evlist__mmap_get(evlist, idx);
		}

		if (__perf_evlist__add_pollfd(evlist, fd, idx) < 0) {
			perf_evlist__mmap_put(evlist, idx);
			return -1;
		}

		if ((evsel->attr.read_format & PERF_FORMAT_ID) &&
		    perf_evlist__id_add_fd(evlist, evsel, cpu, thread, fd) < 0)
			return -1;
	}

	return 0;
}

static int perf_evlist__mmap_per_cpu(struct perf_evlist *evlist,
				     struct mmap_params *mp)
{
	int cpu, thread;
	int nr_cpus = cpu_map__nr(evlist->cpus);
	int nr_threads = thread_map__nr(evlist->threads);

	pr_debug2("perf event ring buffer mmapped per cpu\n");
	for (cpu = 0; cpu < nr_cpus; cpu++) {
		int output = -1;

		for (thread = 0; thread < nr_threads; thread++) {
			if (perf_evlist__mmap_per_evsel(evlist, cpu, mp, cpu,
							thread, &output))
				goto out_unmap;
		}
	}

	return 0;

out_unmap:
	for (cpu = 0; cpu < nr_cpus; cpu++)
		__perf_evlist__munmap(evlist, cpu);
	return -1;
}

static int perf_evlist__mmap_per_thread(struct perf_evlist *evlist,
					struct mmap_params *mp)
{
	int thread;
	int nr_threads = thread_map__nr(evlist->threads);

	pr_debug2("perf event ring buffer mmapped per thread\n");
	for (thread = 0; thread < nr_threads; thread++) {
		int output = -1;

		if (perf_evlist__mmap_per_evsel(evlist, thread, mp, 0, thread,
						&output))
			goto out_unmap;
	}

	return 0;

out_unmap:
	for (thread = 0; thread < nr_threads; thread++)
		__perf_evlist__munmap(evlist, thread);
	return -1;
}

static size_t perf_evlist__mmap_size(unsigned long pages)
{
	/* 512 kiB: default amount of unprivileged mlocked memory */
	if (pages == UINT_MAX)
		pages = (512 * 1024) / page_size;
	else if (!is_power_of_2(pages))
		return 0;

	return (pages + 1) * page_size;
}

static long parse_pages_arg(const char *str, unsigned long min,
			    unsigned long max)
{
	unsigned long pages, val;
	static struct parse_tag tags[] = {
		{ .tag  = 'B', .mult = 1       },
		{ .tag  = 'K', .mult = 1 << 10 },
		{ .tag  = 'M', .mult = 1 << 20 },
		{ .tag  = 'G', .mult = 1 << 30 },
		{ .tag  = 0 },
	};

	if (str == NULL)
		return -EINVAL;

	val = parse_tag_value(str, tags);
	if (val != (unsigned long) -1) {
		/* we got file size value */
		pages = PERF_ALIGN(val, page_size) / page_size;
	} else {
		/* we got pages count value */
		char *eptr;
		pages = strtoul(str, &eptr, 10);
		if (*eptr != '\0')
			return -EINVAL;
	}

	if (pages == 0 && min == 0) {
		/* leave number of pages at 0 */
	} else if (!is_power_of_2(pages)) {
		/* round pages up to next power of 2 */
		pages = next_pow2_l(pages);
		if (!pages)
			return -EINVAL;
		pr_info("rounding mmap pages size to %lu bytes (%lu pages)\n",
			pages * page_size, pages);
	}

	if (pages > max)
		return -EINVAL;

	return pages;
}

int perf_evlist__parse_mmap_pages(const struct option *opt, const char *str,
				  int unset __maybe_unused)
{
	unsigned int *mmap_pages = opt->value;
	unsigned long max = UINT_MAX;
	long pages;

	if (max > SIZE_MAX / page_size)
		max = SIZE_MAX / page_size;

	pages = parse_pages_arg(str, 1, max);
	if (pages < 0) {
		pr_err("Invalid argument for --mmap_pages/-m\n");
		return -1;
	}

	*mmap_pages = pages;
	return 0;
}

/**
 * perf_evlist__mmap - Create mmaps to receive events.
 * @evlist: list of events
 * @pages: map length in pages
 * @overwrite: overwrite older events?
 *
 * If @overwrite is %false the user needs to signal event consumption using
 * perf_mmap__write_tail().  Using perf_evlist__mmap_read() does this
 * automatically.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int perf_evlist__mmap(struct perf_evlist *evlist, unsigned int pages,
		      bool overwrite)
{
	struct perf_evsel *evsel;
	const struct cpu_map *cpus = evlist->cpus;
	const struct thread_map *threads = evlist->threads;
	struct mmap_params mp = {
		.prot = PROT_READ | (overwrite ? 0 : PROT_WRITE),
	};

	if (evlist->mmap == NULL && perf_evlist__alloc_mmap(evlist) < 0)
		return -ENOMEM;

	if (evlist->pollfd.entries == NULL && perf_evlist__alloc_pollfd(evlist) < 0)
		return -ENOMEM;

	evlist->overwrite = overwrite;
	evlist->mmap_len = perf_evlist__mmap_size(pages);
	pr_debug("mmap size %zuB\n", evlist->mmap_len);
	mp.mask = evlist->mmap_len - page_size - 1;

	evlist__for_each(evlist, evsel) {
		if ((evsel->attr.read_format & PERF_FORMAT_ID) &&
		    evsel->sample_id == NULL &&
		    perf_evsel__alloc_id(evsel, cpu_map__nr(cpus), threads->nr) < 0)
			return -ENOMEM;
	}

	if (cpu_map__empty(cpus))
		return perf_evlist__mmap_per_thread(evlist, &mp);

	return perf_evlist__mmap_per_cpu(evlist, &mp);
}

int perf_evlist__create_maps(struct perf_evlist *evlist, struct target *target)
{
	evlist->threads = thread_map__new_str(target->pid, target->tid,
					      target->uid);

	if (evlist->threads == NULL)
		return -1;

	if (target__uses_dummy_map(target))
		evlist->cpus = cpu_map__dummy_new();
	else
		evlist->cpus = cpu_map__new(target->cpu_list);

	if (evlist->cpus == NULL)
		goto out_delete_threads;

	return 0;

out_delete_threads:
	thread_map__delete(evlist->threads);
	return -1;
}

int perf_evlist__apply_filters(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	int err = 0;
	const int ncpus = cpu_map__nr(evlist->cpus),
		  nthreads = thread_map__nr(evlist->threads);

	evlist__for_each(evlist, evsel) {
		if (evsel->filter == NULL)
			continue;

		err = perf_evsel__set_filter(evsel, ncpus, nthreads, evsel->filter);
		if (err)
			break;
	}

	return err;
}

int perf_evlist__set_filter(struct perf_evlist *evlist, const char *filter)
{
	struct perf_evsel *evsel;
	int err = 0;
	const int ncpus = cpu_map__nr(evlist->cpus),
		  nthreads = thread_map__nr(evlist->threads);

	evlist__for_each(evlist, evsel) {
		err = perf_evsel__set_filter(evsel, ncpus, nthreads, filter);
		if (err)
			break;
	}

	return err;
}

bool perf_evlist__valid_sample_type(struct perf_evlist *evlist)
{
	struct perf_evsel *pos;

	if (evlist->nr_entries == 1)
		return true;

	if (evlist->id_pos < 0 || evlist->is_pos < 0)
		return false;

	evlist__for_each(evlist, pos) {
		if (pos->id_pos != evlist->id_pos ||
		    pos->is_pos != evlist->is_pos)
			return false;
	}

	return true;
}

u64 __perf_evlist__combined_sample_type(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	if (evlist->combined_sample_type)
		return evlist->combined_sample_type;

	evlist__for_each(evlist, evsel)
		evlist->combined_sample_type |= evsel->attr.sample_type;

	return evlist->combined_sample_type;
}

u64 perf_evlist__combined_sample_type(struct perf_evlist *evlist)
{
	evlist->combined_sample_type = 0;
	return __perf_evlist__combined_sample_type(evlist);
}

bool perf_evlist__valid_read_format(struct perf_evlist *evlist)
{
	struct perf_evsel *first = perf_evlist__first(evlist), *pos = first;
	u64 read_format = first->attr.read_format;
	u64 sample_type = first->attr.sample_type;

	evlist__for_each(evlist, pos) {
		if (read_format != pos->attr.read_format)
			return false;
	}

	/* PERF_SAMPLE_READ imples PERF_FORMAT_ID. */
	if ((sample_type & PERF_SAMPLE_READ) &&
	    !(read_format & PERF_FORMAT_ID)) {
		return false;
	}

	return true;
}

u64 perf_evlist__read_format(struct perf_evlist *evlist)
{
	struct perf_evsel *first = perf_evlist__first(evlist);
	return first->attr.read_format;
}

u16 perf_evlist__id_hdr_size(struct perf_evlist *evlist)
{
	struct perf_evsel *first = perf_evlist__first(evlist);
	struct perf_sample *data;
	u64 sample_type;
	u16 size = 0;

	if (!first->attr.sample_id_all)
		goto out;

	sample_type = first->attr.sample_type;

	if (sample_type & PERF_SAMPLE_TID)
		size += sizeof(data->tid) * 2;

       if (sample_type & PERF_SAMPLE_TIME)
		size += sizeof(data->time);

	if (sample_type & PERF_SAMPLE_ID)
		size += sizeof(data->id);

	if (sample_type & PERF_SAMPLE_STREAM_ID)
		size += sizeof(data->stream_id);

	if (sample_type & PERF_SAMPLE_CPU)
		size += sizeof(data->cpu) * 2;

	if (sample_type & PERF_SAMPLE_IDENTIFIER)
		size += sizeof(data->id);
out:
	return size;
}

bool perf_evlist__valid_sample_id_all(struct perf_evlist *evlist)
{
	struct perf_evsel *first = perf_evlist__first(evlist), *pos = first;

	evlist__for_each_continue(evlist, pos) {
		if (first->attr.sample_id_all != pos->attr.sample_id_all)
			return false;
	}

	return true;
}

bool perf_evlist__sample_id_all(struct perf_evlist *evlist)
{
	struct perf_evsel *first = perf_evlist__first(evlist);
	return first->attr.sample_id_all;
}

void perf_evlist__set_selected(struct perf_evlist *evlist,
			       struct perf_evsel *evsel)
{
	evlist->selected = evsel;
}

void perf_evlist__close(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	int ncpus = cpu_map__nr(evlist->cpus);
	int nthreads = thread_map__nr(evlist->threads);
	int n;

	evlist__for_each_reverse(evlist, evsel) {
		n = evsel->cpus ? evsel->cpus->nr : ncpus;
		perf_evsel__close(evsel, n, nthreads);
	}
}

int perf_evlist__open(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	int err;

	perf_evlist__update_id_pos(evlist);

	evlist__for_each(evlist, evsel) {
		err = perf_evsel__open(evsel, evlist->cpus, evlist->threads);
		if (err < 0)
			goto out_err;
	}

	return 0;
out_err:
	perf_evlist__close(evlist);
	errno = -err;
	return err;
}

int perf_evlist__prepare_workload(struct perf_evlist *evlist, struct target *target,
				  const char *argv[], bool pipe_output,
				  void (*exec_error)(int signo, siginfo_t *info, void *ucontext))
{
	int child_ready_pipe[2], go_pipe[2];
	char bf;

	if (pipe(child_ready_pipe) < 0) {
		perror("failed to create 'ready' pipe");
		return -1;
	}

	if (pipe(go_pipe) < 0) {
		perror("failed to create 'go' pipe");
		goto out_close_ready_pipe;
	}

	evlist->workload.pid = fork();
	if (evlist->workload.pid < 0) {
		perror("failed to fork");
		goto out_close_pipes;
	}

	if (!evlist->workload.pid) {
		int ret;

		if (pipe_output)
			dup2(2, 1);

		signal(SIGTERM, SIG_DFL);

		close(child_ready_pipe[0]);
		close(go_pipe[1]);
		fcntl(go_pipe[0], F_SETFD, FD_CLOEXEC);

		/*
		 * Tell the parent we're ready to go
		 */
		close(child_ready_pipe[1]);

		/*
		 * Wait until the parent tells us to go.
		 */
		ret = read(go_pipe[0], &bf, 1);
		/*
		 * The parent will ask for the execvp() to be performed by
		 * writing exactly one byte, in workload.cork_fd, usually via
		 * perf_evlist__start_workload().
		 *
		 * For cancelling the workload without actuallin running it,
		 * the parent will just close workload.cork_fd, without writing
		 * anything, i.e. read will return zero and we just exit()
		 * here.
		 */
		if (ret != 1) {
			if (ret == -1)
				perror("unable to read pipe");
			exit(ret);
		}

		execvp(argv[0], (char **)argv);

		if (exec_error) {
			union sigval val;

			val.sival_int = errno;
			if (sigqueue(getppid(), SIGUSR1, val))
				perror(argv[0]);
		} else
			perror(argv[0]);
		exit(-1);
	}

	if (exec_error) {
		struct sigaction act = {
			.sa_flags     = SA_SIGINFO,
			.sa_sigaction = exec_error,
		};
		sigaction(SIGUSR1, &act, NULL);
	}

	if (target__none(target))
		evlist->threads->map[0] = evlist->workload.pid;

	close(child_ready_pipe[1]);
	close(go_pipe[0]);
	/*
	 * wait for child to settle
	 */
	if (read(child_ready_pipe[0], &bf, 1) == -1) {
		perror("unable to read pipe");
		goto out_close_pipes;
	}

	fcntl(go_pipe[1], F_SETFD, FD_CLOEXEC);
	evlist->workload.cork_fd = go_pipe[1];
	close(child_ready_pipe[0]);
	return 0;

out_close_pipes:
	close(go_pipe[0]);
	close(go_pipe[1]);
out_close_ready_pipe:
	close(child_ready_pipe[0]);
	close(child_ready_pipe[1]);
	return -1;
}

int perf_evlist__start_workload(struct perf_evlist *evlist)
{
	if (evlist->workload.cork_fd > 0) {
		char bf = 0;
		int ret;
		/*
		 * Remove the cork, let it rip!
		 */
		ret = write(evlist->workload.cork_fd, &bf, 1);
		if (ret < 0)
			perror("enable to write to pipe");

		close(evlist->workload.cork_fd);
		return ret;
	}

	return 0;
}

int perf_evlist__parse_sample(struct perf_evlist *evlist, union perf_event *event,
			      struct perf_sample *sample)
{
	struct perf_evsel *evsel = perf_evlist__event2evsel(evlist, event);

	if (!evsel)
		return -EFAULT;
	return perf_evsel__parse_sample(evsel, event, sample);
}

size_t perf_evlist__fprintf(struct perf_evlist *evlist, FILE *fp)
{
	struct perf_evsel *evsel;
	size_t printed = 0;

	evlist__for_each(evlist, evsel) {
		printed += fprintf(fp, "%s%s", evsel->idx ? ", " : "",
				   perf_evsel__name(evsel));
	}

	return printed + fprintf(fp, "\n");
}

int perf_evlist__strerror_tp(struct perf_evlist *evlist __maybe_unused,
			     int err, char *buf, size_t size)
{
	char sbuf[128];

	switch (err) {
	case ENOENT:
		scnprintf(buf, size, "%s",
			  "Error:\tUnable to find debugfs\n"
			  "Hint:\tWas your kernel was compiled with debugfs support?\n"
			  "Hint:\tIs the debugfs filesystem mounted?\n"
			  "Hint:\tTry 'sudo mount -t debugfs nodev /sys/kernel/debug'");
		break;
	case EACCES:
		scnprintf(buf, size,
			  "Error:\tNo permissions to read %s/tracing/events/raw_syscalls\n"
			  "Hint:\tTry 'sudo mount -o remount,mode=755 %s'\n",
			  debugfs_mountpoint, debugfs_mountpoint);
		break;
	default:
		scnprintf(buf, size, "%s", strerror_r(err, sbuf, sizeof(sbuf)));
		break;
	}

	return 0;
}

int perf_evlist__strerror_open(struct perf_evlist *evlist __maybe_unused,
			       int err, char *buf, size_t size)
{
	int printed, value;
	char sbuf[STRERR_BUFSIZE], *emsg = strerror_r(err, sbuf, sizeof(sbuf));

	switch (err) {
	case EACCES:
	case EPERM:
		printed = scnprintf(buf, size,
				    "Error:\t%s.\n"
				    "Hint:\tCheck /proc/sys/kernel/perf_event_paranoid setting.", emsg);

		value = perf_event_paranoid();

		printed += scnprintf(buf + printed, size - printed, "\nHint:\t");

		if (value >= 2) {
			printed += scnprintf(buf + printed, size - printed,
					     "For your workloads it needs to be <= 1\nHint:\t");
		}
		printed += scnprintf(buf + printed, size - printed,
				     "For system wide tracing it needs to be set to -1.\n");

		printed += scnprintf(buf + printed, size - printed,
				    "Hint:\tTry: 'sudo sh -c \"echo -1 > /proc/sys/kernel/perf_event_paranoid\"'\n"
				    "Hint:\tThe current value is %d.", value);
		break;
	default:
		scnprintf(buf, size, "%s", emsg);
		break;
	}

	return 0;
}

void perf_evlist__to_front(struct perf_evlist *evlist,
			   struct perf_evsel *move_evsel)
{
	struct perf_evsel *evsel, *n;
	LIST_HEAD(move);

	if (move_evsel == perf_evlist__first(evlist))
		return;

	evlist__for_each_safe(evlist, n, evsel) {
		if (evsel->leader == move_evsel->leader)
			list_move_tail(&evsel->node, &move);
	}

	list_splice(&move, &evlist->entries);
}

void perf_evlist__set_tracking_event(struct perf_evlist *evlist,
				     struct perf_evsel *tracking_evsel)
{
	struct perf_evsel *evsel;

	if (tracking_evsel->tracking)
		return;

	evlist__for_each(evlist, evsel) {
		if (evsel != tracking_evsel)
			evsel->tracking = false;
	}

	tracking_evsel->tracking = true;
}
