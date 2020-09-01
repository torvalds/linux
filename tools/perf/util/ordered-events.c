// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <inttypes.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/string.h>
#include "ordered-events.h"
#include "session.h"
#include "asm/bug.h"
#include "debug.h"
#include "ui/progress.h"

#define pr_N(n, fmt, ...) \
	eprintf(n, debug_ordered_events, fmt, ##__VA_ARGS__)

#define pr(fmt, ...) pr_N(1, pr_fmt(fmt), ##__VA_ARGS__)

static void queue_event(struct ordered_events *oe, struct ordered_event *new)
{
	struct ordered_event *last = oe->last;
	u64 timestamp = new->timestamp;
	struct list_head *p;

	++oe->nr_events;
	oe->last = new;

	pr_oe_time2(timestamp, "queue_event nr_events %u\n", oe->nr_events);

	if (!last) {
		list_add(&new->list, &oe->events);
		oe->max_timestamp = timestamp;
		return;
	}

	/*
	 * last event might point to some random place in the list as it's
	 * the last queued event. We expect that the new event is close to
	 * this.
	 */
	if (last->timestamp <= timestamp) {
		while (last->timestamp <= timestamp) {
			p = last->list.next;
			if (p == &oe->events) {
				list_add_tail(&new->list, &oe->events);
				oe->max_timestamp = timestamp;
				return;
			}
			last = list_entry(p, struct ordered_event, list);
		}
		list_add_tail(&new->list, &last->list);
	} else {
		while (last->timestamp > timestamp) {
			p = last->list.prev;
			if (p == &oe->events) {
				list_add(&new->list, &oe->events);
				return;
			}
			last = list_entry(p, struct ordered_event, list);
		}
		list_add(&new->list, &last->list);
	}
}

static union perf_event *__dup_event(struct ordered_events *oe,
				     union perf_event *event)
{
	union perf_event *new_event = NULL;

	if (oe->cur_alloc_size < oe->max_alloc_size) {
		new_event = memdup(event, event->header.size);
		if (new_event)
			oe->cur_alloc_size += event->header.size;
	}

	return new_event;
}

static union perf_event *dup_event(struct ordered_events *oe,
				   union perf_event *event)
{
	return oe->copy_on_queue ? __dup_event(oe, event) : event;
}

static void __free_dup_event(struct ordered_events *oe, union perf_event *event)
{
	if (event) {
		oe->cur_alloc_size -= event->header.size;
		free(event);
	}
}

static void free_dup_event(struct ordered_events *oe, union perf_event *event)
{
	if (oe->copy_on_queue)
		__free_dup_event(oe, event);
}

#define MAX_SAMPLE_BUFFER	(64 * 1024 / sizeof(struct ordered_event))
static struct ordered_event *alloc_event(struct ordered_events *oe,
					 union perf_event *event)
{
	struct list_head *cache = &oe->cache;
	struct ordered_event *new = NULL;
	union perf_event *new_event;
	size_t size;

	new_event = dup_event(oe, event);
	if (!new_event)
		return NULL;

	/*
	 * We maintain the following scheme of buffers for ordered
	 * event allocation:
	 *
	 *   to_free list -> buffer1 (64K)
	 *                   buffer2 (64K)
	 *                   ...
	 *
	 * Each buffer keeps an array of ordered events objects:
	 *    buffer -> event[0]
	 *              event[1]
	 *              ...
	 *
	 * Each allocated ordered event is linked to one of
	 * following lists:
	 *   - time ordered list 'events'
	 *   - list of currently removed events 'cache'
	 *
	 * Allocation of the ordered event uses the following order
	 * to get the memory:
	 *   - use recently removed object from 'cache' list
	 *   - use available object in current allocation buffer
	 *   - allocate new buffer if the current buffer is full
	 *
	 * Removal of ordered event object moves it from events to
	 * the cache list.
	 */
	size = sizeof(*oe->buffer) + MAX_SAMPLE_BUFFER * sizeof(*new);

	if (!list_empty(cache)) {
		new = list_entry(cache->next, struct ordered_event, list);
		list_del_init(&new->list);
	} else if (oe->buffer) {
		new = &oe->buffer->event[oe->buffer_idx];
		if (++oe->buffer_idx == MAX_SAMPLE_BUFFER)
			oe->buffer = NULL;
	} else if ((oe->cur_alloc_size + size) < oe->max_alloc_size) {
		oe->buffer = malloc(size);
		if (!oe->buffer) {
			free_dup_event(oe, new_event);
			return NULL;
		}

		pr("alloc size %" PRIu64 "B (+%zu), max %" PRIu64 "B\n",
		   oe->cur_alloc_size, size, oe->max_alloc_size);

		oe->cur_alloc_size += size;
		list_add(&oe->buffer->list, &oe->to_free);

		oe->buffer_idx = 1;
		new = &oe->buffer->event[0];
	} else {
		pr("allocation limit reached %" PRIu64 "B\n", oe->max_alloc_size);
		return NULL;
	}

	new->event = new_event;
	return new;
}

static struct ordered_event *
ordered_events__new_event(struct ordered_events *oe, u64 timestamp,
		    union perf_event *event)
{
	struct ordered_event *new;

	new = alloc_event(oe, event);
	if (new) {
		new->timestamp = timestamp;
		queue_event(oe, new);
	}

	return new;
}

void ordered_events__delete(struct ordered_events *oe, struct ordered_event *event)
{
	list_move(&event->list, &oe->cache);
	oe->nr_events--;
	free_dup_event(oe, event->event);
	event->event = NULL;
}

int ordered_events__queue(struct ordered_events *oe, union perf_event *event,
			  u64 timestamp, u64 file_offset)
{
	struct ordered_event *oevent;

	if (!timestamp || timestamp == ~0ULL)
		return -ETIME;

	if (timestamp < oe->last_flush) {
		pr_oe_time(timestamp,      "out of order event\n");
		pr_oe_time(oe->last_flush, "last flush, last_flush_type %d\n",
			   oe->last_flush_type);

		oe->nr_unordered_events++;
	}

	oevent = ordered_events__new_event(oe, timestamp, event);
	if (!oevent) {
		ordered_events__flush(oe, OE_FLUSH__HALF);
		oevent = ordered_events__new_event(oe, timestamp, event);
	}

	if (!oevent)
		return -ENOMEM;

	oevent->file_offset = file_offset;
	return 0;
}

static int do_flush(struct ordered_events *oe, bool show_progress)
{
	struct list_head *head = &oe->events;
	struct ordered_event *tmp, *iter;
	u64 limit = oe->next_flush;
	u64 last_ts = oe->last ? oe->last->timestamp : 0ULL;
	struct ui_progress prog;
	int ret;

	if (!limit)
		return 0;

	if (show_progress)
		ui_progress__init(&prog, oe->nr_events, "Processing time ordered events...");

	list_for_each_entry_safe(iter, tmp, head, list) {
		if (session_done())
			return 0;

		if (iter->timestamp > limit)
			break;
		ret = oe->deliver(oe, iter);
		if (ret)
			return ret;

		ordered_events__delete(oe, iter);
		oe->last_flush = iter->timestamp;

		if (show_progress)
			ui_progress__update(&prog, 1);
	}

	if (list_empty(head))
		oe->last = NULL;
	else if (last_ts <= limit)
		oe->last = list_entry(head->prev, struct ordered_event, list);

	if (show_progress)
		ui_progress__finish();

	return 0;
}

static int __ordered_events__flush(struct ordered_events *oe, enum oe_flush how,
				   u64 timestamp)
{
	static const char * const str[] = {
		"NONE",
		"FINAL",
		"ROUND",
		"HALF ",
		"TOP  ",
		"TIME ",
	};
	int err;
	bool show_progress = false;

	if (oe->nr_events == 0)
		return 0;

	switch (how) {
	case OE_FLUSH__FINAL:
		show_progress = true;
		__fallthrough;
	case OE_FLUSH__TOP:
		oe->next_flush = ULLONG_MAX;
		break;

	case OE_FLUSH__HALF:
	{
		struct ordered_event *first, *last;
		struct list_head *head = &oe->events;

		first = list_entry(head->next, struct ordered_event, list);
		last = oe->last;

		/* Warn if we are called before any event got allocated. */
		if (WARN_ONCE(!last || list_empty(head), "empty queue"))
			return 0;

		oe->next_flush  = first->timestamp;
		oe->next_flush += (last->timestamp - first->timestamp) / 2;
		break;
	}

	case OE_FLUSH__TIME:
		oe->next_flush = timestamp;
		show_progress = false;
		break;

	case OE_FLUSH__ROUND:
	case OE_FLUSH__NONE:
	default:
		break;
	}

	pr_oe_time(oe->next_flush, "next_flush - ordered_events__flush PRE  %s, nr_events %u\n",
		   str[how], oe->nr_events);
	pr_oe_time(oe->max_timestamp, "max_timestamp\n");

	err = do_flush(oe, show_progress);

	if (!err) {
		if (how == OE_FLUSH__ROUND)
			oe->next_flush = oe->max_timestamp;

		oe->last_flush_type = how;
	}

	pr_oe_time(oe->next_flush, "next_flush - ordered_events__flush POST %s, nr_events %u\n",
		   str[how], oe->nr_events);
	pr_oe_time(oe->last_flush, "last_flush\n");

	return err;
}

int ordered_events__flush(struct ordered_events *oe, enum oe_flush how)
{
	return __ordered_events__flush(oe, how, 0);
}

int ordered_events__flush_time(struct ordered_events *oe, u64 timestamp)
{
	return __ordered_events__flush(oe, OE_FLUSH__TIME, timestamp);
}

u64 ordered_events__first_time(struct ordered_events *oe)
{
	struct ordered_event *event;

	if (list_empty(&oe->events))
		return 0;

	event = list_first_entry(&oe->events, struct ordered_event, list);
	return event->timestamp;
}

void ordered_events__init(struct ordered_events *oe, ordered_events__deliver_t deliver,
			  void *data)
{
	INIT_LIST_HEAD(&oe->events);
	INIT_LIST_HEAD(&oe->cache);
	INIT_LIST_HEAD(&oe->to_free);
	oe->max_alloc_size = (u64) -1;
	oe->cur_alloc_size = 0;
	oe->deliver	   = deliver;
	oe->data	   = data;
}

static void
ordered_events_buffer__free(struct ordered_events_buffer *buffer,
			    unsigned int max, struct ordered_events *oe)
{
	if (oe->copy_on_queue) {
		unsigned int i;

		for (i = 0; i < max; i++)
			__free_dup_event(oe, buffer->event[i].event);
	}

	free(buffer);
}

void ordered_events__free(struct ordered_events *oe)
{
	struct ordered_events_buffer *buffer, *tmp;

	if (list_empty(&oe->to_free))
		return;

	/*
	 * Current buffer might not have all the events allocated
	 * yet, we need to free only allocated ones ...
	 */
	if (oe->buffer) {
		list_del_init(&oe->buffer->list);
		ordered_events_buffer__free(oe->buffer, oe->buffer_idx, oe);
	}

	/* ... and continue with the rest */
	list_for_each_entry_safe(buffer, tmp, &oe->to_free, list) {
		list_del_init(&buffer->list);
		ordered_events_buffer__free(buffer, MAX_SAMPLE_BUFFER, oe);
	}
}

void ordered_events__reinit(struct ordered_events *oe)
{
	ordered_events__deliver_t old_deliver = oe->deliver;

	ordered_events__free(oe);
	memset(oe, '\0', sizeof(*oe));
	ordered_events__init(oe, old_deliver, oe->data);
}
