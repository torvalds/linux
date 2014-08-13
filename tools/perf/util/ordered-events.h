#ifndef __ORDERED_EVENTS_H
#define __ORDERED_EVENTS_H

#include <linux/types.h>
#include "tool.h"

struct perf_session;

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
	int			buffer_idx;
	unsigned int		nr_events;
	enum oe_flush		last_flush_type;
};

struct ordered_event *ordered_events__new(struct ordered_events *oe, u64 timestamp);
void ordered_events__delete(struct ordered_events *oe, struct ordered_event *event);
int ordered_events__flush(struct perf_session *s, struct perf_tool *tool,
			  enum oe_flush how);
void ordered_events__init(struct ordered_events *oe);
void ordered_events__free(struct ordered_events *oe);

static inline
void ordered_events__set_alloc_size(struct ordered_events *oe, u64 size)
{
	oe->max_alloc_size = size;
}
#endif /* __ORDERED_EVENTS_H */
