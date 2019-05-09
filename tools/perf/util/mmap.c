/*
 * Copyright (C) 2011-2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from evlist.c builtin-{top,stat,record}.c, see those files for further
 * copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include <sys/mman.h>
#include <inttypes.h>
#include <asm/bug.h>
#ifdef HAVE_LIBNUMA_SUPPORT
#include <numaif.h>
#endif
#include "debug.h"
#include "event.h"
#include "mmap.h"
#include "util.h" /* page_size */

size_t perf_mmap__mmap_len(struct perf_mmap *map)
{
	return map->mask + 1 + page_size;
}

/* When check_messup is true, 'end' must points to a good entry */
static union perf_event *perf_mmap__read(struct perf_mmap *map,
					 u64 *startp, u64 end)
{
	unsigned char *data = map->base + page_size;
	union perf_event *event = NULL;
	int diff = end - *startp;

	if (diff >= (int)sizeof(event->header)) {
		size_t size;

		event = (union perf_event *)&data[*startp & map->mask];
		size = event->header.size;

		if (size < sizeof(event->header) || diff < (int)size)
			return NULL;

		/*
		 * Event straddles the mmap boundary -- header should always
		 * be inside due to u64 alignment of output.
		 */
		if ((*startp & map->mask) + size != ((*startp + size) & map->mask)) {
			unsigned int offset = *startp;
			unsigned int len = min(sizeof(*event), size), cpy;
			void *dst = map->event_copy;

			do {
				cpy = min(map->mask + 1 - (offset & map->mask), len);
				memcpy(dst, &data[offset & map->mask], cpy);
				offset += cpy;
				dst += cpy;
				len -= cpy;
			} while (len);

			event = (union perf_event *)map->event_copy;
		}

		*startp += size;
	}

	return event;
}

/*
 * Read event from ring buffer one by one.
 * Return one event for each call.
 *
 * Usage:
 * perf_mmap__read_init()
 * while(event = perf_mmap__read_event()) {
 *	//process the event
 *	perf_mmap__consume()
 * }
 * perf_mmap__read_done()
 */
union perf_event *perf_mmap__read_event(struct perf_mmap *map)
{
	union perf_event *event;

	/*
	 * Check if event was unmapped due to a POLLHUP/POLLERR.
	 */
	if (!refcount_read(&map->refcnt))
		return NULL;

	/* non-overwirte doesn't pause the ringbuffer */
	if (!map->overwrite)
		map->end = perf_mmap__read_head(map);

	event = perf_mmap__read(map, &map->start, map->end);

	if (!map->overwrite)
		map->prev = map->start;

	return event;
}

static bool perf_mmap__empty(struct perf_mmap *map)
{
	return perf_mmap__read_head(map) == map->prev && !map->auxtrace_mmap.base;
}

void perf_mmap__get(struct perf_mmap *map)
{
	refcount_inc(&map->refcnt);
}

void perf_mmap__put(struct perf_mmap *map)
{
	BUG_ON(map->base && refcount_read(&map->refcnt) == 0);

	if (refcount_dec_and_test(&map->refcnt))
		perf_mmap__munmap(map);
}

void perf_mmap__consume(struct perf_mmap *map)
{
	if (!map->overwrite) {
		u64 old = map->prev;

		perf_mmap__write_tail(map, old);
	}

	if (refcount_read(&map->refcnt) == 1 && perf_mmap__empty(map))
		perf_mmap__put(map);
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

void __weak auxtrace_mmap_params__init(struct auxtrace_mmap_params *mp __maybe_unused,
				       off_t auxtrace_offset __maybe_unused,
				       unsigned int auxtrace_pages __maybe_unused,
				       bool auxtrace_overwrite __maybe_unused)
{
}

void __weak auxtrace_mmap_params__set_idx(struct auxtrace_mmap_params *mp __maybe_unused,
					  struct perf_evlist *evlist __maybe_unused,
					  int idx __maybe_unused,
					  bool per_cpu __maybe_unused)
{
}

#ifdef HAVE_AIO_SUPPORT

#ifdef HAVE_LIBNUMA_SUPPORT
static int perf_mmap__aio_alloc(struct perf_mmap *map, int idx)
{
	map->aio.data[idx] = mmap(NULL, perf_mmap__mmap_len(map), PROT_READ|PROT_WRITE,
				  MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
	if (map->aio.data[idx] == MAP_FAILED) {
		map->aio.data[idx] = NULL;
		return -1;
	}

	return 0;
}

static void perf_mmap__aio_free(struct perf_mmap *map, int idx)
{
	if (map->aio.data[idx]) {
		munmap(map->aio.data[idx], perf_mmap__mmap_len(map));
		map->aio.data[idx] = NULL;
	}
}

static int perf_mmap__aio_bind(struct perf_mmap *map, int idx, int cpu, int affinity)
{
	void *data;
	size_t mmap_len;
	unsigned long node_mask;

	if (affinity != PERF_AFFINITY_SYS && cpu__max_node() > 1) {
		data = map->aio.data[idx];
		mmap_len = perf_mmap__mmap_len(map);
		node_mask = 1UL << cpu__get_node(cpu);
		if (mbind(data, mmap_len, MPOL_BIND, &node_mask, 1, 0)) {
			pr_err("Failed to bind [%p-%p] AIO buffer to node %d: error %m\n",
				data, data + mmap_len, cpu__get_node(cpu));
			return -1;
		}
	}

	return 0;
}
#else
static int perf_mmap__aio_alloc(struct perf_mmap *map, int idx)
{
	map->aio.data[idx] = malloc(perf_mmap__mmap_len(map));
	if (map->aio.data[idx] == NULL)
		return -1;

	return 0;
}

static void perf_mmap__aio_free(struct perf_mmap *map, int idx)
{
	zfree(&(map->aio.data[idx]));
}

static int perf_mmap__aio_bind(struct perf_mmap *map __maybe_unused, int idx __maybe_unused,
		int cpu __maybe_unused, int affinity __maybe_unused)
{
	return 0;
}
#endif

static int perf_mmap__aio_mmap(struct perf_mmap *map, struct mmap_params *mp)
{
	int delta_max, i, prio, ret;

	map->aio.nr_cblocks = mp->nr_cblocks;
	if (map->aio.nr_cblocks) {
		map->aio.aiocb = calloc(map->aio.nr_cblocks, sizeof(struct aiocb *));
		if (!map->aio.aiocb) {
			pr_debug2("failed to allocate aiocb for data buffer, error %m\n");
			return -1;
		}
		map->aio.cblocks = calloc(map->aio.nr_cblocks, sizeof(struct aiocb));
		if (!map->aio.cblocks) {
			pr_debug2("failed to allocate cblocks for data buffer, error %m\n");
			return -1;
		}
		map->aio.data = calloc(map->aio.nr_cblocks, sizeof(void *));
		if (!map->aio.data) {
			pr_debug2("failed to allocate data buffer, error %m\n");
			return -1;
		}
		delta_max = sysconf(_SC_AIO_PRIO_DELTA_MAX);
		for (i = 0; i < map->aio.nr_cblocks; ++i) {
			ret = perf_mmap__aio_alloc(map, i);
			if (ret == -1) {
				pr_debug2("failed to allocate data buffer area, error %m");
				return -1;
			}
			ret = perf_mmap__aio_bind(map, i, map->cpu, mp->affinity);
			if (ret == -1)
				return -1;
			/*
			 * Use cblock.aio_fildes value different from -1
			 * to denote started aio write operation on the
			 * cblock so it requires explicit record__aio_sync()
			 * call prior the cblock may be reused again.
			 */
			map->aio.cblocks[i].aio_fildes = -1;
			/*
			 * Allocate cblocks with priority delta to have
			 * faster aio write system calls because queued requests
			 * are kept in separate per-prio queues and adding
			 * a new request will iterate thru shorter per-prio
			 * list. Blocks with numbers higher than
			 *  _SC_AIO_PRIO_DELTA_MAX go with priority 0.
			 */
			prio = delta_max - i;
			map->aio.cblocks[i].aio_reqprio = prio >= 0 ? prio : 0;
		}
	}

	return 0;
}

static void perf_mmap__aio_munmap(struct perf_mmap *map)
{
	int i;

	for (i = 0; i < map->aio.nr_cblocks; ++i)
		perf_mmap__aio_free(map, i);
	if (map->aio.data)
		zfree(&map->aio.data);
	zfree(&map->aio.cblocks);
	zfree(&map->aio.aiocb);
}

int perf_mmap__aio_push(struct perf_mmap *md, void *to, int idx,
			int push(void *to, struct aiocb *cblock, void *buf, size_t size, off_t off),
			off_t *off)
{
	u64 head = perf_mmap__read_head(md);
	unsigned char *data = md->base + page_size;
	unsigned long size, size0 = 0;
	void *buf;
	int rc = 0;

	rc = perf_mmap__read_init(md);
	if (rc < 0)
		return (rc == -EAGAIN) ? 0 : -1;

	/*
	 * md->base data is copied into md->data[idx] buffer to
	 * release space in the kernel buffer as fast as possible,
	 * thru perf_mmap__consume() below.
	 *
	 * That lets the kernel to proceed with storing more
	 * profiling data into the kernel buffer earlier than other
	 * per-cpu kernel buffers are handled.
	 *
	 * Coping can be done in two steps in case the chunk of
	 * profiling data crosses the upper bound of the kernel buffer.
	 * In this case we first move part of data from md->start
	 * till the upper bound and then the reminder from the
	 * beginning of the kernel buffer till the end of
	 * the data chunk.
	 */

	size = md->end - md->start;

	if ((md->start & md->mask) + size != (md->end & md->mask)) {
		buf = &data[md->start & md->mask];
		size = md->mask + 1 - (md->start & md->mask);
		md->start += size;
		memcpy(md->aio.data[idx], buf, size);
		size0 = size;
	}

	buf = &data[md->start & md->mask];
	size = md->end - md->start;
	md->start += size;
	memcpy(md->aio.data[idx] + size0, buf, size);

	/*
	 * Increment md->refcount to guard md->data[idx] buffer
	 * from premature deallocation because md object can be
	 * released earlier than aio write request started
	 * on mmap->data[idx] is complete.
	 *
	 * perf_mmap__put() is done at record__aio_complete()
	 * after started request completion.
	 */
	perf_mmap__get(md);

	md->prev = head;
	perf_mmap__consume(md);

	rc = push(to, &md->aio.cblocks[idx], md->aio.data[idx], size0 + size, *off);
	if (!rc) {
		*off += size0 + size;
	} else {
		/*
		 * Decrement md->refcount back if aio write
		 * operation failed to start.
		 */
		perf_mmap__put(md);
	}

	return rc;
}
#else
static int perf_mmap__aio_mmap(struct perf_mmap *map __maybe_unused,
			       struct mmap_params *mp __maybe_unused)
{
	return 0;
}

static void perf_mmap__aio_munmap(struct perf_mmap *map __maybe_unused)
{
}
#endif

void perf_mmap__munmap(struct perf_mmap *map)
{
	perf_mmap__aio_munmap(map);
	if (map->base != NULL) {
		munmap(map->base, perf_mmap__mmap_len(map));
		map->base = NULL;
		map->fd = -1;
		refcount_set(&map->refcnt, 0);
	}
	auxtrace_mmap__munmap(&map->auxtrace_mmap);
}

static void build_node_mask(int node, cpu_set_t *mask)
{
	int c, cpu, nr_cpus;
	const struct cpu_map *cpu_map = NULL;

	cpu_map = cpu_map__online();
	if (!cpu_map)
		return;

	nr_cpus = cpu_map__nr(cpu_map);
	for (c = 0; c < nr_cpus; c++) {
		cpu = cpu_map->map[c]; /* map c index to online cpu index */
		if (cpu__get_node(cpu) == node)
			CPU_SET(cpu, mask);
	}
}

static void perf_mmap__setup_affinity_mask(struct perf_mmap *map, struct mmap_params *mp)
{
	CPU_ZERO(&map->affinity_mask);
	if (mp->affinity == PERF_AFFINITY_NODE && cpu__max_node() > 1)
		build_node_mask(cpu__get_node(map->cpu), &map->affinity_mask);
	else if (mp->affinity == PERF_AFFINITY_CPU)
		CPU_SET(map->cpu, &map->affinity_mask);
}

int perf_mmap__mmap(struct perf_mmap *map, struct mmap_params *mp, int fd, int cpu)
{
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
	map->cpu = cpu;

	perf_mmap__setup_affinity_mask(map, mp);

	if (auxtrace_mmap__mmap(&map->auxtrace_mmap,
				&mp->auxtrace_mp, map->base, fd))
		return -1;

	return perf_mmap__aio_mmap(map, mp);
}

static int overwrite_rb_find_range(void *buf, int mask, u64 *start, u64 *end)
{
	struct perf_event_header *pheader;
	u64 evt_head = *start;
	int size = mask + 1;

	pr_debug2("%s: buf=%p, start=%"PRIx64"\n", __func__, buf, *start);
	pheader = (struct perf_event_header *)(buf + (*start & mask));
	while (true) {
		if (evt_head - *start >= (unsigned int)size) {
			pr_debug("Finished reading overwrite ring buffer: rewind\n");
			if (evt_head - *start > (unsigned int)size)
				evt_head -= pheader->size;
			*end = evt_head;
			return 0;
		}

		pheader = (struct perf_event_header *)(buf + (evt_head & mask));

		if (pheader->size == 0) {
			pr_debug("Finished reading overwrite ring buffer: get start\n");
			*end = evt_head;
			return 0;
		}

		evt_head += pheader->size;
		pr_debug3("move evt_head: %"PRIx64"\n", evt_head);
	}
	WARN_ONCE(1, "Shouldn't get here\n");
	return -1;
}

/*
 * Report the start and end of the available data in ringbuffer
 */
static int __perf_mmap__read_init(struct perf_mmap *md)
{
	u64 head = perf_mmap__read_head(md);
	u64 old = md->prev;
	unsigned char *data = md->base + page_size;
	unsigned long size;

	md->start = md->overwrite ? head : old;
	md->end = md->overwrite ? old : head;

	if (md->start == md->end)
		return -EAGAIN;

	size = md->end - md->start;
	if (size > (unsigned long)(md->mask) + 1) {
		if (!md->overwrite) {
			WARN_ONCE(1, "failed to keep up with mmap data. (warn only once)\n");

			md->prev = head;
			perf_mmap__consume(md);
			return -EAGAIN;
		}

		/*
		 * Backward ring buffer is full. We still have a chance to read
		 * most of data from it.
		 */
		if (overwrite_rb_find_range(data, md->mask, &md->start, &md->end))
			return -EINVAL;
	}

	return 0;
}

int perf_mmap__read_init(struct perf_mmap *map)
{
	/*
	 * Check if event was unmapped due to a POLLHUP/POLLERR.
	 */
	if (!refcount_read(&map->refcnt))
		return -ENOENT;

	return __perf_mmap__read_init(map);
}

int perf_mmap__push(struct perf_mmap *md, void *to,
		    int push(struct perf_mmap *map, void *to, void *buf, size_t size))
{
	u64 head = perf_mmap__read_head(md);
	unsigned char *data = md->base + page_size;
	unsigned long size;
	void *buf;
	int rc = 0;

	rc = perf_mmap__read_init(md);
	if (rc < 0)
		return (rc == -EAGAIN) ? 0 : -1;

	size = md->end - md->start;

	if ((md->start & md->mask) + size != (md->end & md->mask)) {
		buf = &data[md->start & md->mask];
		size = md->mask + 1 - (md->start & md->mask);
		md->start += size;

		if (push(md, to, buf, size) < 0) {
			rc = -1;
			goto out;
		}
	}

	buf = &data[md->start & md->mask];
	size = md->end - md->start;
	md->start += size;

	if (push(md, to, buf, size) < 0) {
		rc = -1;
		goto out;
	}

	md->prev = head;
	perf_mmap__consume(md);
out:
	return rc;
}

/*
 * Mandatory for overwrite mode
 * The direction of overwrite mode is backward.
 * The last perf_mmap__read() will set tail to map->prev.
 * Need to correct the map->prev to head which is the end of next read.
 */
void perf_mmap__read_done(struct perf_mmap *map)
{
	/*
	 * Check if event was unmapped due to a POLLHUP/POLLERR.
	 */
	if (!refcount_read(&map->refcnt))
		return;

	map->prev = perf_mmap__read_head(map);
}
