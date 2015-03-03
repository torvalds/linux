#ifndef __ORDERED_EVENTS_H
#define __ORDERED_EVENTS_H

#include <linux/types.h>

struct perf_tool;
struct perf_evlist;
struct perf_sample;
struct machines;

struct ordered_event {
	u64			timestamp;
	u64			file_offset;
	union perf_event	*event;
	struct list_head	list;
};

enum oe_flush {
	OE_FLUSH__NONE,
	OE_FLUSH__FINAL,
	OE_FLUSH__ROUND,
	OE_FLUSH__HALF,
};

struct ordered_events;

typedef int (*ordered_events__deliver_t)(struct ordered_events *oe,
					 struct ordered_event *event,
					 struct perf_sample *sample);

struct ordered_events {
	u64			last_flush;
	u64			next_flush;
	u64			max_timestamp;
	u64			max_alloc_size;
	u64			cur_alloc_size;
	struct list_head	events;
	struct list_head	cache;
	struct list_head	to_free;
	struct ordered_event	*buffer;
	struct ordered_event	*last;
	struct machines		*machines;
	struct perf_evlist	*evlist;
	struct perf_tool	*tool;
	ordered_events__deliver_t deliver;
	int			buffer_idx;
	unsigned int		nr_events;
	enum oe_flush		last_flush_type;
	bool                    copy_on_queue;
};

struct ordered_event *ordered_events__new(struct ordered_events *oe, u64 timestamp,
					  union perf_event *event);
void ordered_events__delete(struct ordered_events *oe, struct ordered_event *event);
int ordered_events__flush(struct ordered_events *oe, enum oe_flush how);
void ordered_events__init(struct ordered_events *oe, struct machines *machines,
			  struct perf_evlist *evlsit, struct perf_tool *tool,
			  ordered_events__deliver_t deliver);
void ordered_events__free(struct ordered_events *oe);

static inline
void ordered_events__set_alloc_size(struct ordered_events *oe, u64 size)
{
	oe->max_alloc_size = size;
}

static inline
void ordered_events__set_copy_on_queue(struct ordered_events *oe, bool copy)
{
	oe->copy_on_queue = copy;
}
#endif /* __ORDERED_EVENTS_H */
