/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RING_BUFFER_H
#define _LINUX_RING_BUFFER_H

#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/poll.h>

struct trace_buffer;
struct ring_buffer_iter;

/*
 * Don't refer to this struct directly, use functions below.
 */
struct ring_buffer_event {
	u32		type_len:5, time_delta:27;

	u32		array[];
};

/**
 * enum ring_buffer_type - internal ring buffer types
 *
 * @RINGBUF_TYPE_PADDING:	Left over page padding or discarded event
 *				 If time_delta is 0:
 *				  array is ignored
 *				  size is variable depending on how much
 *				  padding is needed
 *				 If time_delta is non zero:
 *				  array[0] holds the actual length
 *				  size = 4 + length (bytes)
 *
 * @RINGBUF_TYPE_TIME_EXTEND:	Extend the time delta
 *				 array[0] = time delta (28 .. 59)
 *				 size = 8 bytes
 *
 * @RINGBUF_TYPE_TIME_STAMP:	Absolute timestamp
 *				 Same format as TIME_EXTEND except that the
 *				 value is an absolute timestamp, not a delta
 *				 event.time_delta contains bottom 27 bits
 *				 array[0] = top (28 .. 59) bits
 *				 size = 8 bytes
 *
 * <= @RINGBUF_TYPE_DATA_TYPE_LEN_MAX:
 *				Data record
 *				 If type_len is zero:
 *				  array[0] holds the actual length
 *				  array[1..(length+3)/4] holds data
 *				  size = 4 + length (bytes)
 *				 else
 *				  length = type_len << 2
 *				  array[0..(length+3)/4-1] holds data
 *				  size = 4 + length (bytes)
 */
enum ring_buffer_type {
	RINGBUF_TYPE_DATA_TYPE_LEN_MAX = 28,
	RINGBUF_TYPE_PADDING,
	RINGBUF_TYPE_TIME_EXTEND,
	RINGBUF_TYPE_TIME_STAMP,
};

unsigned ring_buffer_event_length(struct ring_buffer_event *event);
void *ring_buffer_event_data(struct ring_buffer_event *event);
u64 ring_buffer_event_time_stamp(struct ring_buffer_event *event);

/*
 * ring_buffer_discard_commit will remove an event that has not
 *   been committed yet. If this is used, then ring_buffer_unlock_commit
 *   must not be called on the discarded event. This function
 *   will try to remove the event from the ring buffer completely
 *   if another event has not been written after it.
 *
 * Example use:
 *
 *  if (some_condition)
 *    ring_buffer_discard_commit(buffer, event);
 *  else
 *    ring_buffer_unlock_commit(buffer, event);
 */
void ring_buffer_discard_commit(struct trace_buffer *buffer,
				struct ring_buffer_event *event);

/*
 * size is in bytes for each per CPU buffer.
 */
struct trace_buffer *
__ring_buffer_alloc(unsigned long size, unsigned flags, struct lock_class_key *key);

/*
 * Because the ring buffer is generic, if other users of the ring buffer get
 * traced by ftrace, it can produce lockdep warnings. We need to keep each
 * ring buffer's lock class separate.
 */
#define ring_buffer_alloc(size, flags)			\
({							\
	static struct lock_class_key __key;		\
	__ring_buffer_alloc((size), (flags), &__key);	\
})

int ring_buffer_wait(struct trace_buffer *buffer, int cpu, int full);
__poll_t ring_buffer_poll_wait(struct trace_buffer *buffer, int cpu,
			  struct file *filp, poll_table *poll_table);


#define RING_BUFFER_ALL_CPUS -1

void ring_buffer_free(struct trace_buffer *buffer);

int ring_buffer_resize(struct trace_buffer *buffer, unsigned long size, int cpu);

void ring_buffer_change_overwrite(struct trace_buffer *buffer, int val);

struct ring_buffer_event *ring_buffer_lock_reserve(struct trace_buffer *buffer,
						   unsigned long length);
int ring_buffer_unlock_commit(struct trace_buffer *buffer,
			      struct ring_buffer_event *event);
int ring_buffer_write(struct trace_buffer *buffer,
		      unsigned long length, void *data);

void ring_buffer_nest_start(struct trace_buffer *buffer);
void ring_buffer_nest_end(struct trace_buffer *buffer);

struct ring_buffer_event *
ring_buffer_peek(struct trace_buffer *buffer, int cpu, u64 *ts,
		 unsigned long *lost_events);
struct ring_buffer_event *
ring_buffer_consume(struct trace_buffer *buffer, int cpu, u64 *ts,
		    unsigned long *lost_events);

struct ring_buffer_iter *
ring_buffer_read_prepare(struct trace_buffer *buffer, int cpu, gfp_t flags);
void ring_buffer_read_prepare_sync(void);
void ring_buffer_read_start(struct ring_buffer_iter *iter);
void ring_buffer_read_finish(struct ring_buffer_iter *iter);

struct ring_buffer_event *
ring_buffer_iter_peek(struct ring_buffer_iter *iter, u64 *ts);
void ring_buffer_iter_advance(struct ring_buffer_iter *iter);
void ring_buffer_iter_reset(struct ring_buffer_iter *iter);
int ring_buffer_iter_empty(struct ring_buffer_iter *iter);
bool ring_buffer_iter_dropped(struct ring_buffer_iter *iter);

unsigned long ring_buffer_size(struct trace_buffer *buffer, int cpu);

void ring_buffer_reset_cpu(struct trace_buffer *buffer, int cpu);
void ring_buffer_reset(struct trace_buffer *buffer);

#ifdef CONFIG_RING_BUFFER_ALLOW_SWAP
int ring_buffer_swap_cpu(struct trace_buffer *buffer_a,
			 struct trace_buffer *buffer_b, int cpu);
#else
static inline int
ring_buffer_swap_cpu(struct trace_buffer *buffer_a,
		     struct trace_buffer *buffer_b, int cpu)
{
	return -ENODEV;
}
#endif

bool ring_buffer_empty(struct trace_buffer *buffer);
bool ring_buffer_empty_cpu(struct trace_buffer *buffer, int cpu);

void ring_buffer_record_disable(struct trace_buffer *buffer);
void ring_buffer_record_enable(struct trace_buffer *buffer);
void ring_buffer_record_off(struct trace_buffer *buffer);
void ring_buffer_record_on(struct trace_buffer *buffer);
bool ring_buffer_record_is_on(struct trace_buffer *buffer);
bool ring_buffer_record_is_set_on(struct trace_buffer *buffer);
void ring_buffer_record_disable_cpu(struct trace_buffer *buffer, int cpu);
void ring_buffer_record_enable_cpu(struct trace_buffer *buffer, int cpu);

u64 ring_buffer_oldest_event_ts(struct trace_buffer *buffer, int cpu);
unsigned long ring_buffer_bytes_cpu(struct trace_buffer *buffer, int cpu);
unsigned long ring_buffer_entries(struct trace_buffer *buffer);
unsigned long ring_buffer_overruns(struct trace_buffer *buffer);
unsigned long ring_buffer_entries_cpu(struct trace_buffer *buffer, int cpu);
unsigned long ring_buffer_overrun_cpu(struct trace_buffer *buffer, int cpu);
unsigned long ring_buffer_commit_overrun_cpu(struct trace_buffer *buffer, int cpu);
unsigned long ring_buffer_dropped_events_cpu(struct trace_buffer *buffer, int cpu);
unsigned long ring_buffer_read_events_cpu(struct trace_buffer *buffer, int cpu);

u64 ring_buffer_time_stamp(struct trace_buffer *buffer, int cpu);
void ring_buffer_normalize_time_stamp(struct trace_buffer *buffer,
				      int cpu, u64 *ts);
void ring_buffer_set_clock(struct trace_buffer *buffer,
			   u64 (*clock)(void));
void ring_buffer_set_time_stamp_abs(struct trace_buffer *buffer, bool abs);
bool ring_buffer_time_stamp_abs(struct trace_buffer *buffer);

size_t ring_buffer_nr_pages(struct trace_buffer *buffer, int cpu);
size_t ring_buffer_nr_dirty_pages(struct trace_buffer *buffer, int cpu);

void *ring_buffer_alloc_read_page(struct trace_buffer *buffer, int cpu);
void ring_buffer_free_read_page(struct trace_buffer *buffer, int cpu, void *data);
int ring_buffer_read_page(struct trace_buffer *buffer, void **data_page,
			  size_t len, int cpu, int full);

struct trace_seq;

int ring_buffer_print_entry_header(struct trace_seq *s);
int ring_buffer_print_page_header(struct trace_seq *s);

enum ring_buffer_flags {
	RB_FL_OVERWRITE		= 1 << 0,
};

#ifdef CONFIG_RING_BUFFER
int trace_rb_cpu_prepare(unsigned int cpu, struct hlist_node *node);
#else
#define trace_rb_cpu_prepare	NULL
#endif

#endif /* _LINUX_RING_BUFFER_H */
