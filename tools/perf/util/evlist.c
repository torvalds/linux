/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-{top,stat,record}.c, see those files for further
 * copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */
#include "util.h"
#include <api/fs/fs.h>
#include <poll.h>
#include "cpumap.h"
#include "thread_map.h"
#include "target.h"
#include "evlist.h"
#include "evsel.h"
#include "debug.h"
#include "asm/bug.h"
#include <unistd.h>

#include "parse-events.h"
#include <subcmd/parse-options.h>

#include <sys/mman.h>

#include <linux/bitops.h>
#include <linux/hash.h>
#include <linux/log2.h>
#include <linux/err.h>

static void perf_mmap__munmap(struct perf_mmap *map);
static void perf_mmap__put(struct perf_mmap *map);

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
	evlist->bkw_mmap_state = BKW_MMAP_NOTREADY;
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

struct perf_evlist *perf_evlist__new_dummy(void)
{
	struct perf_evlist *evlist = perf_evlist__new();

	if (evlist && perf_evlist__add_dummy(evlist)) {
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

	evlist__for_each_entry(evlist, evsel)
		perf_evsel__calc_id_pos(evsel);

	perf_evlist__set_id_pos(evlist);
}

static void perf_evlist__purge(struct perf_evlist *evlist)
{
	struct perf_evsel *pos, *n;

	evlist__for_each_entry_safe(evlist, n, pos) {
		list_del_init(&pos->node);
		pos->evlist = NULL;
		perf_evsel__delete(pos);
	}

	evlist->nr_entries = 0;
}

void perf_evlist__exit(struct perf_evlist *evlist)
{
	zfree(&evlist->mmap);
	zfree(&evlist->backward_mmap);
	fdarray__exit(&evlist->pollfd);
}

void perf_evlist__delete(struct perf_evlist *evlist)
{
	if (evlist == NULL)
		return;

	perf_evlist__munmap(evlist);
	perf_evlist__close(evlist);
	cpu_map__put(evlist->cpus);
	thread_map__put(evlist->threads);
	evlist->cpus = NULL;
	evlist->threads = NULL;
	perf_evlist__purge(evlist);
	perf_evlist__exit(evlist);
	free(evlist);
}

static void __perf_evlist__propagate_maps(struct perf_evlist *evlist,
					  struct perf_evsel *evsel)
{
	/*
	 * We already have cpus for evsel (via PMU sysfs) so
	 * keep it, if there's no target cpu list defined.
	 */
	if (!evsel->own_cpus || evlist->has_user_cpus) {
		cpu_map__put(evsel->cpus);
		evsel->cpus = cpu_map__get(evlist->cpus);
	} else if (evsel->cpus != evsel->own_cpus) {
		cpu_map__put(evsel->cpus);
		evsel->cpus = cpu_map__get(evsel->own_cpus);
	}

	thread_map__put(evsel->threads);
	evsel->threads = thread_map__get(evlist->threads);
}

static void perf_evlist__propagate_maps(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(evlist, evsel)
		__perf_evlist__propagate_maps(evlist, evsel);
}

void perf_evlist__add(struct perf_evlist *evlist, struct perf_evsel *entry)
{
	entry->evlist = evlist;
	list_add_tail(&entry->node, &evlist->entries);
	entry->idx = evlist->nr_entries;
	entry->tracking = !entry->idx;

	if (!evlist->nr_entries++)
		perf_evlist__set_id_pos(evlist);

	__perf_evlist__propagate_maps(evlist, entry);
}

void perf_evlist__remove(struct perf_evlist *evlist, struct perf_evsel *evsel)
{
	evsel->evlist = NULL;
	list_del_init(&evsel->node);
	evlist->nr_entries -= 1;
}

void perf_evlist__splice_list_tail(struct perf_evlist *evlist,
				   struct list_head *list)
{
	struct perf_evsel *evsel, *temp;

	__evlist__for_each_entry_safe(list, temp, evsel) {
		list_del_init(&evsel->node);
		perf_evlist__add(evlist, evsel);
	}
}

void __perf_evlist__set_leader(struct list_head *list)
{
	struct perf_evsel *evsel, *leader;

	leader = list_entry(list->next, struct perf_evsel, node);
	evsel = list_entry(list->prev, struct perf_evsel, node);

	leader->nr_members = evsel->idx - leader->idx + 1;

	__evlist__for_each_entry(list, evsel) {
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

void perf_event_attr__set_max_precise_ip(struct perf_event_attr *attr)
{
	attr->precise_ip = 3;

	while (attr->precise_ip != 0) {
		int fd = sys_perf_event_open(attr, 0, -1, -1, 0);
		if (fd != -1) {
			close(fd);
			break;
		}
		--attr->precise_ip;
	}
}

int perf_evlist__add_default(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evsel__new_cycles();

	if (evsel == NULL)
		return -ENOMEM;

	perf_evlist__add(evlist, evsel);
	return 0;
}

int perf_evlist__add_dummy(struct perf_evlist *evlist)
{
	struct perf_event_attr attr = {
		.type	= PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_DUMMY,
		.size	= sizeof(attr), /* to capture ABI version */
	};
	struct perf_evsel *evsel = perf_evsel__new(&attr);

	if (evsel == NULL)
		return -ENOMEM;

	perf_evlist__add(evlist, evsel);
	return 0;
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

	perf_evlist__splice_list_tail(evlist, &head);

	return 0;

out_delete_partial_list:
	__evlist__for_each_entry_safe(&head, n, evsel)
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

	evlist__for_each_entry(evlist, evsel) {
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

	evlist__for_each_entry(evlist, evsel) {
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

	if (IS_ERR(evsel))
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
	struct perf_evsel *pos;

	evlist__for_each_entry(evlist, pos) {
		if (!perf_evsel__is_group_leader(pos) || !pos->fd)
			continue;
		perf_evsel__disable(pos);
	}

	evlist->enabled = false;
}

void perf_evlist__enable(struct perf_evlist *evlist)
{
	struct perf_evsel *pos;

	evlist__for_each_entry(evlist, pos) {
		if (!perf_evsel__is_group_leader(pos) || !pos->fd)
			continue;
		perf_evsel__enable(pos);
	}

	evlist->enabled = true;
}

void perf_evlist__toggle_enable(struct perf_evlist *evlist)
{
	(evlist->enabled ? perf_evlist__disable : perf_evlist__enable)(evlist);
}

static int perf_evlist__enable_event_cpu(struct perf_evlist *evlist,
					 struct perf_evsel *evsel, int cpu)
{
	int thread;
	int nr_threads = perf_evlist__nr_threads(evlist, evsel);

	if (!evsel->fd)
		return -EINVAL;

	for (thread = 0; thread < nr_threads; thread++) {
		int err = ioctl(FD(evsel, cpu, thread), PERF_EVENT_IOC_ENABLE, 0);
		if (err)
			return err;
	}
	return 0;
}

static int perf_evlist__enable_event_thread(struct perf_evlist *evlist,
					    struct perf_evsel *evsel,
					    int thread)
{
	int cpu;
	int nr_cpus = cpu_map__nr(evlist->cpus);

	if (!evsel->fd)
		return -EINVAL;

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		int err = ioctl(FD(evsel, cpu, thread), PERF_EVENT_IOC_ENABLE, 0);
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

	evlist__for_each_entry(evlist, evsel) {
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

static int __perf_evlist__add_pollfd(struct perf_evlist *evlist, int fd,
				     struct perf_mmap *map, short revent)
{
	int pos = fdarray__add(&evlist->pollfd, fd, revent | POLLERR | POLLHUP);
	/*
	 * Save the idx so that when we filter out fds POLLHUP'ed we can
	 * close the associated evlist->mmap[] entry.
	 */
	if (pos >= 0) {
		evlist->pollfd.priv[pos].ptr = map;

		fcntl(fd, F_SETFL, O_NONBLOCK);
	}

	return pos;
}

int perf_evlist__add_pollfd(struct perf_evlist *evlist, int fd)
{
	return __perf_evlist__add_pollfd(evlist, fd, NULL, POLLIN);
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
		sid->tid = thread_map__pid(evlist->threads, thread);
	else
		sid->tid = -1;
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

	if (evlist->nr_entries == 1 || !id)
		return perf_evlist__first(evlist);

	sid = perf_evlist__id2sid(evlist, id);
	if (sid)
		return sid->evsel;

	if (!perf_evlist__sample_id_all(evlist))
		return perf_evlist__first(evlist);

	return NULL;
}

struct perf_evsel *perf_evlist__id2evsel_strict(struct perf_evlist *evlist,
						u64 id)
{
	struct perf_sample_id *sid;

	if (!id)
		return NULL;

	sid = perf_evlist__id2sid(evlist, id);
	if (sid)
		return sid->evsel;

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

struct perf_evsel *perf_evlist__event2evsel(struct perf_evlist *evlist,
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

static int perf_evlist__set_paused(struct perf_evlist *evlist, bool value)
{
	int i;

	if (!evlist->backward_mmap)
		return 0;

	for (i = 0; i < evlist->nr_mmaps; i++) {
		int fd = evlist->backward_mmap[i].fd;
		int err;

		if (fd < 0)
			continue;
		err = ioctl(fd, PERF_EVENT_IOC_PAUSE_OUTPUT, value ? 1 : 0);
		if (err)
			return err;
	}
	return 0;
}

static int perf_evlist__pause(struct perf_evlist *evlist)
{
	return perf_evlist__set_paused(evlist, true);
}

static int perf_evlist__resume(struct perf_evlist *evlist)
{
	return perf_evlist__set_paused(evlist, false);
}

/* When check_messup is true, 'end' must points to a good entry */
static union perf_event *
perf_mmap__read(struct perf_mmap *md, bool check_messup, u64 start,
		u64 end, u64 *prev)
{
	unsigned char *data = md->base + page_size;
	union perf_event *event = NULL;
	int diff = end - start;

	if (check_messup) {
		/*
		 * If we're further behind than half the buffer, there's a chance
		 * the writer will bite our tail and mess up the samples under us.
		 *
		 * If we somehow ended up ahead of the 'end', we got messed up.
		 *
		 * In either case, truncate and restart at 'end'.
		 */
		if (diff > md->mask / 2 || diff < 0) {
			fprintf(stderr, "WARNING: failed to keep up with mmap data.\n");

			/*
			 * 'end' points to a known good entry, start there.
			 */
			start = end;
			diff = 0;
		}
	}

	if (diff >= (int)sizeof(event->header)) {
		size_t size;

		event = (union perf_event *)&data[start & md->mask];
		size = event->header.size;

		if (size < sizeof(event->header) || diff < (int)size) {
			event = NULL;
			goto broken_event;
		}

		/*
		 * Event straddles the mmap boundary -- header should always
		 * be inside due to u64 alignment of output.
		 */
		if ((start & md->mask) + size != ((start + size) & md->mask)) {
			unsigned int offset = start;
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

		start += size;
	}

broken_event:
	if (prev)
		*prev = start;

	return event;
}

union perf_event *perf_mmap__read_forward(struct perf_mmap *md, bool check_messup)
{
	u64 head;
	u64 old = md->prev;

	/*
	 * Check if event was unmapped due to a POLLHUP/POLLERR.
	 */
	if (!refcount_read(&md->refcnt))
		return NULL;

	head = perf_mmap__read_head(md);

	return perf_mmap__read(md, check_messup, old, head, &md->prev);
}

union perf_event *
perf_mmap__read_backward(struct perf_mmap *md)
{
	u64 head, end;
	u64 start = md->prev;

	/*
	 * Check if event was unmapped due to a POLLHUP/POLLERR.
	 */
	if (!refcount_read(&md->refcnt))
		return NULL;

	head = perf_mmap__read_head(md);
	if (!head)
		return NULL;

	/*
	 * 'head' pointer starts from 0. Kernel minus sizeof(record) form
	 * it each time when kernel writes to it, so in fact 'head' is
	 * negative. 'end' pointer is made manually by adding the size of
	 * the ring buffer to 'head' pointer, means the validate data can
	 * read is the whole ring buffer. If 'end' is positive, the ring
	 * buffer has not fully filled, so we must adjust 'end' to 0.
	 *
	 * However, since both 'head' and 'end' is unsigned, we can't
	 * simply compare 'end' against 0. Here we compare '-head' and
	 * the size of the ring buffer, where -head is the number of bytes
	 * kernel write to the ring buffer.
	 */
	if (-head < (u64)(md->mask + 1))
		end = 0;
	else
		end = head + md->mask + 1;

	return perf_mmap__read(md, false, start, end, &md->prev);
}

union perf_event *perf_evlist__mmap_read_forward(struct perf_evlist *evlist, int idx)
{
	struct perf_mmap *md = &evlist->mmap[idx];

	/*
	 * Check messup is required for forward overwritable ring buffer:
	 * memory pointed by md->prev can be overwritten in this case.
	 * No need for read-write ring buffer: kernel stop outputting when
	 * it hit md->prev (perf_mmap__consume()).
	 */
	return perf_mmap__read_forward(md, evlist->overwrite);
}

union perf_event *perf_evlist__mmap_read_backward(struct perf_evlist *evlist, int idx)
{
	struct perf_mmap *md = &evlist->mmap[idx];

	/*
	 * No need to check messup for backward ring buffer:
	 * We can always read arbitrary long data from a backward
	 * ring buffer unless we forget to pause it before reading.
	 */
	return perf_mmap__read_backward(md);
}

union perf_event *perf_evlist__mmap_read(struct perf_evlist *evlist, int idx)
{
	return perf_evlist__mmap_read_forward(evlist, idx);
}

void perf_mmap__read_catchup(struct perf_mmap *md)
{
	u64 head;

	if (!refcount_read(&md->refcnt))
		return;

	head = perf_mmap__read_head(md);
	md->prev = head;
}

void perf_evlist__mmap_read_catchup(struct perf_evlist *evlist, int idx)
{
	perf_mmap__read_catchup(&evlist->mmap[idx]);
}

static bool perf_mmap__empty(struct perf_mmap *md)
{
	return perf_mmap__read_head(md) == md->prev && !md->auxtrace_mmap.base;
}

static void perf_mmap__get(struct perf_mmap *map)
{
	refcount_inc(&map->refcnt);
}

static void perf_mmap__put(struct perf_mmap *md)
{
	BUG_ON(md->base && refcount_read(&md->refcnt) == 0);

	if (refcount_dec_and_test(&md->refcnt))
		perf_mmap__munmap(md);
}

void perf_mmap__consume(struct perf_mmap *md, bool overwrite)
{
	if (!overwrite) {
		u64 old = md->prev;

		perf_mmap__write_tail(md, old);
	}

	if (refcount_read(&md->refcnt) == 1 && perf_mmap__empty(md))
		perf_mmap__put(md);
}

void perf_evlist__mmap_consume(struct perf_evlist *evlist, int idx)
{
	perf_mmap__consume(&evlist->mmap[idx], evlist->overwrite);
}

int __weak auxtrace_mmap__mmap(struct auxtrace_mmap *mm __maybe_unused,
			       struct auxtrace_mmap_params *mp __maybe_unused,
			       void *userpg __maybe_unused,
			       int fd __maybe_unused)
{
	return 0;
}

void __weak auxtrace_mmap__munmap(struct auxtrace_mmap *mm __maybe_unused)
{
}

void __weak auxtrace_mmap_params__init(
			struct auxtrace_mmap_params *mp __maybe_unused,
			off_t auxtrace_offset __maybe_unused,
			unsigned int auxtrace_pages __maybe_unused,
			bool auxtrace_overwrite __maybe_unused)
{
}

void __weak auxtrace_mmap_params__set_idx(
			struct auxtrace_mmap_params *mp __maybe_unused,
			struct perf_evlist *evlist __maybe_unused,
			int idx __maybe_unused,
			bool per_cpu __maybe_unused)
{
}

static void perf_mmap__munmap(struct perf_mmap *map)
{
	if (map->base != NULL) {
		munmap(map->base, perf_mmap__mmap_len(map));
		map->base = NULL;
		map->fd = -1;
		refcount_set(&map->refcnt, 0);
	}
	auxtrace_mmap__munmap(&map->auxtrace_mmap);
}

static void perf_evlist__munmap_nofree(struct perf_evlist *evlist)
{
	int i;

	if (evlist->mmap)
		for (i = 0; i < evlist->nr_mmaps; i++)
			perf_mmap__munmap(&evlist->mmap[i]);

	if (evlist->backward_mmap)
		for (i = 0; i < evlist->nr_mmaps; i++)
			perf_mmap__munmap(&evlist->backward_mmap[i]);
}

void perf_evlist__munmap(struct perf_evlist *evlist)
{
	perf_evlist__munmap_nofree(evlist);
	zfree(&evlist->mmap);
	zfree(&evlist->backward_mmap);
}

static struct perf_mmap *perf_evlist__alloc_mmap(struct perf_evlist *evlist)
{
	int i;
	struct perf_mmap *map;

	evlist->nr_mmaps = cpu_map__nr(evlist->cpus);
	if (cpu_map__empty(evlist->cpus))
		evlist->nr_mmaps = thread_map__nr(evlist->threads);
	map = zalloc(evlist->nr_mmaps * sizeof(struct perf_mmap));
	if (!map)
		return NULL;

	for (i = 0; i < evlist->nr_mmaps; i++) {
		map[i].fd = -1;
		/*
		 * When the perf_mmap() call is made we grab one refcount, plus
		 * one extra to let perf_evlist__mmap_consume() get the last
		 * events after all real references (perf_mmap__get()) are
		 * dropped.
		 *
		 * Each PERF_EVENT_IOC_SET_OUTPUT points to this mmap and
		 * thus does perf_mmap__get() on it.
		 */
		refcount_set(&map[i].refcnt, 0);
	}
	return map;
}

struct mmap_params {
	int prot;
	int mask;
	struct auxtrace_mmap_params auxtrace_mp;
};

static int perf_mmap__mmap(struct perf_mmap *map,
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
	refcount_set(&map->refcnt, 2);
	map->prev = 0;
	map->mask = mp->mask;
	map->base = mmap(NULL, perf_mmap__mmap_len(map), mp->prot,
			 MAP_SHARED, fd, 0);
	if (map->base == MAP_FAILED) {
		pr_debug2("failed to mmap perf event ring buffer, error %d\n",
			  errno);
		map->base = NULL;
		return -1;
	}
	map->fd = fd;

	if (auxtrace_mmap__mmap(&map->auxtrace_mmap,
				&mp->auxtrace_mp, map->base, fd))
		return -1;

	return 0;
}

static bool
perf_evlist__should_poll(struct perf_evlist *evlist __maybe_unused,
			 struct perf_evsel *evsel)
{
	if (evsel->attr.write_backward)
		return false;
	return true;
}

static int perf_evlist__mmap_per_evsel(struct perf_evlist *evlist, int idx,
				       struct mmap_params *mp, int cpu_idx,
				       int thread, int *_output, int *_output_backward)
{
	struct perf_evsel *evsel;
	int revent;
	int evlist_cpu = cpu_map__cpu(evlist->cpus, cpu_idx);

	evlist__for_each_entry(evlist, evsel) {
		struct perf_mmap *maps = evlist->mmap;
		int *output = _output;
		int fd;
		int cpu;

		if (evsel->attr.write_backward) {
			output = _output_backward;
			maps = evlist->backward_mmap;

			if (!maps) {
				maps = perf_evlist__alloc_mmap(evlist);
				if (!maps)
					return -1;
				evlist->backward_mmap = maps;
				if (evlist->bkw_mmap_state == BKW_MMAP_NOTREADY)
					perf_evlist__toggle_bkw_mmap(evlist, BKW_MMAP_RUNNING);
			}
		}

		if (evsel->system_wide && thread)
			continue;

		cpu = cpu_map__idx(evsel->cpus, evlist_cpu);
		if (cpu == -1)
			continue;

		fd = FD(evsel, cpu, thread);

		if (*output == -1) {
			*output = fd;

			if (perf_mmap__mmap(&maps[idx], mp, *output)  < 0)
				return -1;
		} else {
			if (ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, *output) != 0)
				return -1;

			perf_mmap__get(&maps[idx]);
		}

		revent = perf_evlist__should_poll(evlist, evsel) ? POLLIN : 0;

		/*
		 * The system_wide flag causes a selected event to be opened
		 * always without a pid.  Consequently it will never get a
		 * POLLHUP, but it is used for tracking in combination with
		 * other events, so it should not need to be polled anyway.
		 * Therefore don't add it for polling.
		 */
		if (!evsel->system_wide &&
		    __perf_evlist__add_pollfd(evlist, fd, &maps[idx], revent) < 0) {
			perf_mmap__put(&maps[idx]);
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

static int perf_evlist__mmap_per_cpu(struct perf_evlist *evlist,
				     struct mmap_params *mp)
{
	int cpu, thread;
	int nr_cpus = cpu_map__nr(evlist->cpus);
	int nr_threads = thread_map__nr(evlist->threads);

	pr_debug2("perf event ring buffer mmapped per cpu\n");
	for (cpu = 0; cpu < nr_cpus; cpu++) {
		int output = -1;
		int output_backward = -1;

		auxtrace_mmap_params__set_idx(&mp->auxtrace_mp, evlist, cpu,
					      true);

		for (thread = 0; thread < nr_threads; thread++) {
			if (perf_evlist__mmap_per_evsel(evlist, cpu, mp, cpu,
							thread, &output, &output_backward))
				goto out_unmap;
		}
	}

	return 0;

out_unmap:
	perf_evlist__munmap_nofree(evlist);
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
		int output_backward = -1;

		auxtrace_mmap_params__set_idx(&mp->auxtrace_mp, evlist, thread,
					      false);

		if (perf_evlist__mmap_per_evsel(evlist, thread, mp, 0, thread,
						&output, &output_backward))
			goto out_unmap;
	}

	return 0;

out_unmap:
	perf_evlist__munmap_nofree(evlist);
	return -1;
}

unsigned long perf_event_mlock_kb_in_pages(void)
{
	unsigned long pages;
	int max;

	if (sysctl__read_int("kernel/perf_event_mlock_kb", &max) < 0) {
		/*
		 * Pick a once upon a time good value, i.e. things look
		 * strange since we can't read a sysctl value, but lets not
		 * die yet...
		 */
		max = 512;
	} else {
		max -= (page_size / 1024);
	}

	pages = (max * 1024) / page_size;
	if (!is_power_of_2(pages))
		pages = rounddown_pow_of_two(pages);

	return pages;
}

size_t perf_evlist__mmap_size(unsigned long pages)
{
	if (pages == UINT_MAX)
		pages = perf_event_mlock_kb_in_pages();
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
		char buf[100];

		/* round pages up to next power of 2 */
		pages = roundup_pow_of_two(pages);
		if (!pages)
			return -EINVAL;

		unit_number__scnprintf(buf, sizeof(buf), pages * page_size);
		pr_info("rounding mmap pages size to %s (%lu pages)\n",
			buf, pages);
	}

	if (pages > max)
		return -EINVAL;

	return pages;
}

int __perf_evlist__parse_mmap_pages(unsigned int *mmap_pages, const char *str)
{
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

int perf_evlist__parse_mmap_pages(const struct option *opt, const char *str,
				  int unset __maybe_unused)
{
	return __perf_evlist__parse_mmap_pages(opt->value, str);
}

/**
 * perf_evlist__mmap_ex - Create mmaps to receive events.
 * @evlist: list of events
 * @pages: map length in pages
 * @overwrite: overwrite older events?
 * @auxtrace_pages - auxtrace map length in pages
 * @auxtrace_overwrite - overwrite older auxtrace data?
 *
 * If @overwrite is %false the user needs to signal event consumption using
 * perf_mmap__write_tail().  Using perf_evlist__mmap_read() does this
 * automatically.
 *
 * Similarly, if @auxtrace_overwrite is %false the user needs to signal data
 * consumption using auxtrace_mmap__write_tail().
 *
 * Return: %0 on success, negative error code otherwise.
 */
int perf_evlist__mmap_ex(struct perf_evlist *evlist, unsigned int pages,
			 bool overwrite, unsigned int auxtrace_pages,
			 bool auxtrace_overwrite)
{
	struct perf_evsel *evsel;
	const struct cpu_map *cpus = evlist->cpus;
	const struct thread_map *threads = evlist->threads;
	struct mmap_params mp = {
		.prot = PROT_READ | (overwrite ? 0 : PROT_WRITE),
	};

	if (!evlist->mmap)
		evlist->mmap = perf_evlist__alloc_mmap(evlist);
	if (!evlist->mmap)
		return -ENOMEM;

	if (evlist->pollfd.entries == NULL && perf_evlist__alloc_pollfd(evlist) < 0)
		return -ENOMEM;

	evlist->overwrite = overwrite;
	evlist->mmap_len = perf_evlist__mmap_size(pages);
	pr_debug("mmap size %zuB\n", evlist->mmap_len);
	mp.mask = evlist->mmap_len - page_size - 1;

	auxtrace_mmap_params__init(&mp.auxtrace_mp, evlist->mmap_len,
				   auxtrace_pages, auxtrace_overwrite);

	evlist__for_each_entry(evlist, evsel) {
		if ((evsel->attr.read_format & PERF_FORMAT_ID) &&
		    evsel->sample_id == NULL &&
		    perf_evsel__alloc_id(evsel, cpu_map__nr(cpus), threads->nr) < 0)
			return -ENOMEM;
	}

	if (cpu_map__empty(cpus))
		return perf_evlist__mmap_per_thread(evlist, &mp);

	return perf_evlist__mmap_per_cpu(evlist, &mp);
}

int perf_evlist__mmap(struct perf_evlist *evlist, unsigned int pages,
		      bool overwrite)
{
	return perf_evlist__mmap_ex(evlist, pages, overwrite, 0, false);
}

int perf_evlist__create_maps(struct perf_evlist *evlist, struct target *target)
{
	struct cpu_map *cpus;
	struct thread_map *threads;

	threads = thread_map__new_str(target->pid, target->tid, target->uid);

	if (!threads)
		return -1;

	if (target__uses_dummy_map(target))
		cpus = cpu_map__dummy_new();
	else
		cpus = cpu_map__new(target->cpu_list);

	if (!cpus)
		goto out_delete_threads;

	evlist->has_user_cpus = !!target->cpu_list;

	perf_evlist__set_maps(evlist, cpus, threads);

	return 0;

out_delete_threads:
	thread_map__put(threads);
	return -1;
}

void perf_evlist__set_maps(struct perf_evlist *evlist, struct cpu_map *cpus,
			   struct thread_map *threads)
{
	/*
	 * Allow for the possibility that one or another of the maps isn't being
	 * changed i.e. don't put it.  Note we are assuming the maps that are
	 * being applied are brand new and evlist is taking ownership of the
	 * original reference count of 1.  If that is not the case it is up to
	 * the caller to increase the reference count.
	 */
	if (cpus != evlist->cpus) {
		cpu_map__put(evlist->cpus);
		evlist->cpus = cpu_map__get(cpus);
	}

	if (threads != evlist->threads) {
		thread_map__put(evlist->threads);
		evlist->threads = thread_map__get(threads);
	}

	perf_evlist__propagate_maps(evlist);
}

void __perf_evlist__set_sample_bit(struct perf_evlist *evlist,
				   enum perf_event_sample_format bit)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(evlist, evsel)
		__perf_evsel__set_sample_bit(evsel, bit);
}

void __perf_evlist__reset_sample_bit(struct perf_evlist *evlist,
				     enum perf_event_sample_format bit)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(evlist, evsel)
		__perf_evsel__reset_sample_bit(evsel, bit);
}

int perf_evlist__apply_filters(struct perf_evlist *evlist, struct perf_evsel **err_evsel)
{
	struct perf_evsel *evsel;
	int err = 0;
	const int ncpus = cpu_map__nr(evlist->cpus),
		  nthreads = thread_map__nr(evlist->threads);

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->filter == NULL)
			continue;

		/*
		 * filters only work for tracepoint event, which doesn't have cpu limit.
		 * So evlist and evsel should always be same.
		 */
		err = perf_evsel__apply_filter(evsel, ncpus, nthreads, evsel->filter);
		if (err) {
			*err_evsel = evsel;
			break;
		}
	}

	return err;
}

int perf_evlist__set_filter(struct perf_evlist *evlist, const char *filter)
{
	struct perf_evsel *evsel;
	int err = 0;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->attr.type != PERF_TYPE_TRACEPOINT)
			continue;

		err = perf_evsel__set_filter(evsel, filter);
		if (err)
			break;
	}

	return err;
}

int perf_evlist__set_filter_pids(struct perf_evlist *evlist, size_t npids, pid_t *pids)
{
	char *filter;
	int ret = -1;
	size_t i;

	for (i = 0; i < npids; ++i) {
		if (i == 0) {
			if (asprintf(&filter, "common_pid != %d", pids[i]) < 0)
				return -1;
		} else {
			char *tmp;

			if (asprintf(&tmp, "%s && common_pid != %d", filter, pids[i]) < 0)
				goto out_free;

			free(filter);
			filter = tmp;
		}
	}

	ret = perf_evlist__set_filter(evlist, filter);
out_free:
	free(filter);
	return ret;
}

int perf_evlist__set_filter_pid(struct perf_evlist *evlist, pid_t pid)
{
	return perf_evlist__set_filter_pids(evlist, 1, &pid);
}

bool perf_evlist__valid_sample_type(struct perf_evlist *evlist)
{
	struct perf_evsel *pos;

	if (evlist->nr_entries == 1)
		return true;

	if (evlist->id_pos < 0 || evlist->is_pos < 0)
		return false;

	evlist__for_each_entry(evlist, pos) {
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

	evlist__for_each_entry(evlist, evsel)
		evlist->combined_sample_type |= evsel->attr.sample_type;

	return evlist->combined_sample_type;
}

u64 perf_evlist__combined_sample_type(struct perf_evlist *evlist)
{
	evlist->combined_sample_type = 0;
	return __perf_evlist__combined_sample_type(evlist);
}

u64 perf_evlist__combined_branch_type(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	u64 branch_type = 0;

	evlist__for_each_entry(evlist, evsel)
		branch_type |= evsel->attr.branch_sample_type;
	return branch_type;
}

bool perf_evlist__valid_read_format(struct perf_evlist *evlist)
{
	struct perf_evsel *first = perf_evlist__first(evlist), *pos = first;
	u64 read_format = first->attr.read_format;
	u64 sample_type = first->attr.sample_type;

	evlist__for_each_entry(evlist, pos) {
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

	evlist__for_each_entry_continue(evlist, pos) {
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

	evlist__for_each_entry_reverse(evlist, evsel) {
		int n = evsel->cpus ? evsel->cpus->nr : ncpus;
		perf_evsel__close(evsel, n, nthreads);
	}
}

static int perf_evlist__create_syswide_maps(struct perf_evlist *evlist)
{
	struct cpu_map	  *cpus;
	struct thread_map *threads;
	int err = -ENOMEM;

	/*
	 * Try reading /sys/devices/system/cpu/online to get
	 * an all cpus map.
	 *
	 * FIXME: -ENOMEM is the best we can do here, the cpu_map
	 * code needs an overhaul to properly forward the
	 * error, and we may not want to do that fallback to a
	 * default cpu identity map :-\
	 */
	cpus = cpu_map__new(NULL);
	if (!cpus)
		goto out;

	threads = thread_map__new_dummy();
	if (!threads)
		goto out_put;

	perf_evlist__set_maps(evlist, cpus, threads);
out:
	return err;
out_put:
	cpu_map__put(cpus);
	goto out;
}

int perf_evlist__open(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	int err;

	/*
	 * Default: one fd per CPU, all threads, aka systemwide
	 * as sys_perf_event_open(cpu = -1, thread = -1) is EINVAL
	 */
	if (evlist->threads == NULL && evlist->cpus == NULL) {
		err = perf_evlist__create_syswide_maps(evlist);
		if (err < 0)
			goto out_err;
	}

	perf_evlist__update_id_pos(evlist);

	evlist__for_each_entry(evlist, evsel) {
		err = perf_evsel__open(evsel, evsel->cpus, evsel->threads);
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
		 * For cancelling the workload without actually running it,
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

	if (target__none(target)) {
		if (evlist->threads == NULL) {
			fprintf(stderr, "FATAL: evlist->threads need to be set at this point (%s:%d).\n",
				__func__, __LINE__);
			goto out_close_pipes;
		}
		thread_map__set_pid(evlist->threads, 0, evlist->workload.pid);
	}

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
			perror("unable to write to pipe");

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

	evlist__for_each_entry(evlist, evsel) {
		printed += fprintf(fp, "%s%s", evsel->idx ? ", " : "",
				   perf_evsel__name(evsel));
	}

	return printed + fprintf(fp, "\n");
}

int perf_evlist__strerror_open(struct perf_evlist *evlist,
			       int err, char *buf, size_t size)
{
	int printed, value;
	char sbuf[STRERR_BUFSIZE], *emsg = str_error_r(err, sbuf, sizeof(sbuf));

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
	case EINVAL: {
		struct perf_evsel *first = perf_evlist__first(evlist);
		int max_freq;

		if (sysctl__read_int("kernel/perf_event_max_sample_rate", &max_freq) < 0)
			goto out_default;

		if (first->attr.sample_freq < (u64)max_freq)
			goto out_default;

		printed = scnprintf(buf, size,
				    "Error:\t%s.\n"
				    "Hint:\tCheck /proc/sys/kernel/perf_event_max_sample_rate.\n"
				    "Hint:\tThe current value is %d and %" PRIu64 " is being requested.",
				    emsg, max_freq, first->attr.sample_freq);
		break;
	}
	default:
out_default:
		scnprintf(buf, size, "%s", emsg);
		break;
	}

	return 0;
}

int perf_evlist__strerror_mmap(struct perf_evlist *evlist, int err, char *buf, size_t size)
{
	char sbuf[STRERR_BUFSIZE], *emsg = str_error_r(err, sbuf, sizeof(sbuf));
	int pages_attempted = evlist->mmap_len / 1024, pages_max_per_user, printed = 0;

	switch (err) {
	case EPERM:
		sysctl__read_int("kernel/perf_event_mlock_kb", &pages_max_per_user);
		printed += scnprintf(buf + printed, size - printed,
				     "Error:\t%s.\n"
				     "Hint:\tCheck /proc/sys/kernel/perf_event_mlock_kb (%d kB) setting.\n"
				     "Hint:\tTried using %zd kB.\n",
				     emsg, pages_max_per_user, pages_attempted);

		if (pages_attempted >= pages_max_per_user) {
			printed += scnprintf(buf + printed, size - printed,
					     "Hint:\tTry 'sudo sh -c \"echo %d > /proc/sys/kernel/perf_event_mlock_kb\"', or\n",
					     pages_max_per_user + pages_attempted);
		}

		printed += scnprintf(buf + printed, size - printed,
				     "Hint:\tTry using a smaller -m/--mmap-pages value.");
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

	evlist__for_each_entry_safe(evlist, n, evsel) {
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

	evlist__for_each_entry(evlist, evsel) {
		if (evsel != tracking_evsel)
			evsel->tracking = false;
	}

	tracking_evsel->tracking = true;
}

struct perf_evsel *
perf_evlist__find_evsel_by_str(struct perf_evlist *evlist,
			       const char *str)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (!evsel->name)
			continue;
		if (strcmp(str, evsel->name) == 0)
			return evsel;
	}

	return NULL;
}

void perf_evlist__toggle_bkw_mmap(struct perf_evlist *evlist,
				  enum bkw_mmap_state state)
{
	enum bkw_mmap_state old_state = evlist->bkw_mmap_state;
	enum action {
		NONE,
		PAUSE,
		RESUME,
	} action = NONE;

	if (!evlist->backward_mmap)
		return;

	switch (old_state) {
	case BKW_MMAP_NOTREADY: {
		if (state != BKW_MMAP_RUNNING)
			goto state_err;;
		break;
	}
	case BKW_MMAP_RUNNING: {
		if (state != BKW_MMAP_DATA_PENDING)
			goto state_err;
		action = PAUSE;
		break;
	}
	case BKW_MMAP_DATA_PENDING: {
		if (state != BKW_MMAP_EMPTY)
			goto state_err;
		break;
	}
	case BKW_MMAP_EMPTY: {
		if (state != BKW_MMAP_RUNNING)
			goto state_err;
		action = RESUME;
		break;
	}
	default:
		WARN_ONCE(1, "Shouldn't get there\n");
	}

	evlist->bkw_mmap_state = state;

	switch (action) {
	case PAUSE:
		perf_evlist__pause(evlist);
		break;
	case RESUME:
		perf_evlist__resume(evlist);
		break;
	case NONE:
	default:
		break;
	}

state_err:
	return;
}
