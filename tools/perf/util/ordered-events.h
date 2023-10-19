/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ORDERED_EVENTS_H
#define __ORDERED_EVENTS_H

#include <linux/types.h>

struct perf_sample;

struct ordered_event {
	u64			timestamp;
	u64			file_offset;
	const char		*file_path;
	union perf_event	*event;
	struct list_head	list;
};

enum oe_flush {
	OE_FLUSH__NONE,
	OE_FLUSH__FINAL,
	OE_FLUSH__ROUND,
	OE_FLUSH__HALF,
	OE_FLUSH__TOP,
	OE_FLUSH__TIME,
};

struct ordered_events;

typedef int (*ordered_events__deliver_t)(struct ordered_events *oe,
					 struct ordered_event *event);

struct ordered_events_buffer {
	struct list_head	list;
	struct ordered_event	event[];
};

struct ordered_events {
	u64				 last_flush;
	u64				 next_flush;
	u64				 max_timestamp;
	u64				 max_alloc_size;
	u64				 cur_alloc_size;
	struct list_head		 events;
	struct list_head		 cache;
	struct list_head		 to_free;
	struct ordered_events_buffer	*buffer;
	struct ordered_event		*last;
	ordered_events__deliver_t	 deliver;
	int				 buffer_idx;
	unsigned int			 nr_events;
	enum oe_flush			 last_flush_type;
	u32				 nr_unordered_events;
	bool				 copy_on_queue;
	void				*data;
};

int ordered_events__queue(struct ordered_events *oe, union perf_event *event,
			  u64 timestamp, u64 file_offset, const char *file_path);
void ordered_events__delete(struct ordered_events *oe, struct ordered_event *event);
int ordered_events__flush(struct ordered_events *oe, enum oe_flush how);
int ordered_events__flush_time(struct ordered_events *oe, u64 timestamp);
void ordered_events__init(struct ordered_events *oe, ordered_events__deliver_t deliver,
			  void *data);
void ordered_events__free(struct ordered_events *oe);
void ordered_events__reinit(struct ordered_events *oe);
u64 ordered_events__first_time(struct ordered_events *oe);

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

static inline u64 ordered_events__last_flush_time(struct ordered_events *oe)
{
	return oe->last_flush;
}

#endif /* __ORDERED_EVENTS_H */
