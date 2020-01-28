// SPDX-License-Identifier: GPL-2.0
#include <sys/mman.h>
#include <inttypes.h>
#include <asm/bug.h>
#include <errno.h>
#include <string.h>
#include <linux/ring_buffer.h>
#include <linux/perf_event.h>
#include <perf/mmap.h>
#include <perf/event.h>
#include <internal/mmap.h>
#include <internal/lib.h>
#include <linux/kernel.h>
#include "internal.h"

void perf_mmap__init(struct perf_mmap *map, struct perf_mmap *prev,
		     bool overwrite, libperf_unmap_cb_t unmap_cb)
{
	map->fd = -1;
	map->overwrite = overwrite;
	map->unmap_cb  = unmap_cb;
	refcount_set(&map->refcnt, 0);
	if (prev)
		prev->next = map;
}

size_t perf_mmap__mmap_len(struct perf_mmap *map)
{
	return map->mask + 1 + page_size;
}

int perf_mmap__mmap(struct perf_mmap *map, struct perf_mmap_param *mp,
		    int fd, int cpu)
{
	map->prev = 0;
	map->mask = mp->mask;
	map->base = mmap(NULL, perf_mmap__mmap_len(map), mp->prot,
			 MAP_SHARED, fd, 0);
	if (map->base == MAP_FAILED) {
		map->base = NULL;
		return -1;
	}

	map->fd  = fd;
	map->cpu = cpu;
	return 0;
}

void perf_mmap__munmap(struct perf_mmap *map)
{
	if (map && map->base != NULL) {
		munmap(map->base, perf_mmap__mmap_len(map));
		map->base = NULL;
		map->fd = -1;
		refcount_set(&map->refcnt, 0);
	}
	if (map && map->unmap_cb)
		map->unmap_cb(map);
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

static inline void perf_mmap__write_tail(struct perf_mmap *md, u64 tail)
{
	ring_buffer_write_tail(md->base, tail);
}

u64 perf_mmap__read_head(struct perf_mmap *map)
{
	return ring_buffer_read_head(map->base);
}

static bool perf_mmap__empty(struct perf_mmap *map)
{
	struct perf_event_mmap_page *pc = map->base;

	return perf_mmap__read_head(map) == map->prev && !pc->aux_size;
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

	if ((md->end - md->start) < md->flush)
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

/*
 * Mandatory for overwrite mode
 * The direction of overwrite mode is backward.
 * The last perf_mmap__read() will set tail to map->core.prev.
 * Need to correct the map->core.prev to head which is the end of next read.
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
