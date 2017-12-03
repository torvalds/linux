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
					 u64 start, u64 end, u64 *prev)
{
	unsigned char *data = map->base + page_size;
	union perf_event *event = NULL;
	int diff = end - start;

	if (diff >= (int)sizeof(event->header)) {
		size_t size;

		event = (union perf_event *)&data[start & map->mask];
		size = event->header.size;

		if (size < sizeof(event->header) || diff < (int)size) {
			event = NULL;
			goto broken_event;
		}

		/*
		 * Event straddles the mmap boundary -- header should always
		 * be inside due to u64 alignment of output.
		 */
		if ((start & map->mask) + size != ((start + size) & map->mask)) {
			unsigned int offset = start;
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

		start += size;
	}

broken_event:
	if (prev)
		*prev = start;

	return event;
}

union perf_event *perf_mmap__read_forward(struct perf_mmap *map)
{
	u64 head;
	u64 old = map->prev;

	/*
	 * Check if event was unmapped due to a POLLHUP/POLLERR.
	 */
	if (!refcount_read(&map->refcnt))
		return NULL;

	head = perf_mmap__read_head(map);

	return perf_mmap__read(map, old, head, &map->prev);
}

union perf_event *perf_mmap__read_backward(struct perf_mmap *map)
{
	u64 head, end;
	u64 start = map->prev;

	/*
	 * Check if event was unmapped due to a POLLHUP/POLLERR.
	 */
	if (!refcount_read(&map->refcnt))
		return NULL;

	head = perf_mmap__read_head(map);
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
	if (-head < (u64)(map->mask + 1))
		end = 0;
	else
		end = head + map->mask + 1;

	return perf_mmap__read(map, start, end, &map->prev);
}

void perf_mmap__read_catchup(struct perf_mmap *map)
{
	u64 head;

	if (!refcount_read(&map->refcnt))
		return;

	head = perf_mmap__read_head(map);
	map->prev = head;
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

void perf_mmap__consume(struct perf_mmap *map, bool overwrite)
{
	if (!overwrite) {
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

void perf_mmap__munmap(struct perf_mmap *map)
{
	if (map->base != NULL) {
		munmap(map->base, perf_mmap__mmap_len(map));
		map->base = NULL;
		map->fd = -1;
		refcount_set(&map->refcnt, 0);
	}
	auxtrace_mmap__munmap(&map->auxtrace_mmap);
}

int perf_mmap__mmap(struct perf_mmap *map, struct mmap_params *mp, int fd)
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

static int backward_rb_find_range(void *buf, int mask, u64 head, u64 *start, u64 *end)
{
	struct perf_event_header *pheader;
	u64 evt_head = head;
	int size = mask + 1;

	pr_debug2("backward_rb_find_range: buf=%p, head=%"PRIx64"\n", buf, head);
	pheader = (struct perf_event_header *)(buf + (head & mask));
	*start = head;
	while (true) {
		if (evt_head - head >= (unsigned int)size) {
			pr_debug("Finished reading backward ring buffer: rewind\n");
			if (evt_head - head > (unsigned int)size)
				evt_head -= pheader->size;
			*end = evt_head;
			return 0;
		}

		pheader = (struct perf_event_header *)(buf + (evt_head & mask));

		if (pheader->size == 0) {
			pr_debug("Finished reading backward ring buffer: get start\n");
			*end = evt_head;
			return 0;
		}

		evt_head += pheader->size;
		pr_debug3("move evt_head: %"PRIx64"\n", evt_head);
	}
	WARN_ONCE(1, "Shouldn't get here\n");
	return -1;
}

static int rb_find_range(void *data, int mask, u64 head, u64 old,
			 u64 *start, u64 *end, bool backward)
{
	if (!backward) {
		*start = old;
		*end = head;
		return 0;
	}

	return backward_rb_find_range(data, mask, head, start, end);
}

int perf_mmap__push(struct perf_mmap *md, bool backward,
		    void *to, int push(void *to, void *buf, size_t size))
{
	u64 head = perf_mmap__read_head(md);
	u64 old = md->prev;
	u64 end = head, start = old;
	unsigned char *data = md->base + page_size;
	unsigned long size;
	void *buf;
	int rc = 0;

	if (rb_find_range(data, md->mask, head, old, &start, &end, backward))
		return -1;

	if (start == end)
		return 0;

	size = end - start;
	if (size > (unsigned long)(md->mask) + 1) {
		WARN_ONCE(1, "failed to keep up with mmap data. (warn only once)\n");

		md->prev = head;
		perf_mmap__consume(md, backward);
		return 0;
	}

	if ((start & md->mask) + size != (end & md->mask)) {
		buf = &data[start & md->mask];
		size = md->mask + 1 - (start & md->mask);
		start += size;

		if (push(to, buf, size) < 0) {
			rc = -1;
			goto out;
		}
	}

	buf = &data[start & md->mask];
	size = end - start;
	start += size;

	if (push(to, buf, size) < 0) {
		rc = -1;
		goto out;
	}

	md->prev = head;
	perf_mmap__consume(md, backward);
out:
	return rc;
}
