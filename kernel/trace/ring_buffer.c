// SPDX-License-Identifier: GPL-2.0
/*
 * Generic ring buffer
 *
 * Copyright (C) 2008 Steven Rostedt <srostedt@redhat.com>
 */
#include <linux/trace_recursion.h>
#include <linux/trace_events.h>
#include <linux/ring_buffer.h>
#include <linux/trace_clock.h>
#include <linux/sched/clock.h>
#include <linux/cacheflush.h>
#include <linux/trace_seq.h>
#include <linux/spinlock.h>
#include <linux/irq_work.h>
#include <linux/security.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <linux/kthread.h>	/* for self test */
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/list.h>
#include <linux/cpu.h>
#include <linux/oom.h>
#include <linux/mm.h>

#include <asm/local64.h>
#include <asm/local.h>

#include "trace.h"

/*
 * The "absolute" timestamp in the buffer is only 59 bits.
 * If a clock has the 5 MSBs set, it needs to be saved and
 * reinserted.
 */
#define TS_MSB		(0xf8ULL << 56)
#define ABS_TS_MASK	(~TS_MSB)

static void update_pages_handler(struct work_struct *work);

#define RING_BUFFER_META_MAGIC	0xBADFEED

struct ring_buffer_meta {
	int		magic;
	int		struct_size;
	unsigned long	text_addr;
	unsigned long	data_addr;
	unsigned long	first_buffer;
	unsigned long	head_buffer;
	unsigned long	commit_buffer;
	__u32		subbuf_size;
	__u32		nr_subbufs;
	int		buffers[];
};

/*
 * The ring buffer header is special. We must manually up keep it.
 */
int ring_buffer_print_entry_header(struct trace_seq *s)
{
	trace_seq_puts(s, "# compressed entry header\n");
	trace_seq_puts(s, "\ttype_len    :    5 bits\n");
	trace_seq_puts(s, "\ttime_delta  :   27 bits\n");
	trace_seq_puts(s, "\tarray       :   32 bits\n");
	trace_seq_putc(s, '\n');
	trace_seq_printf(s, "\tpadding     : type == %d\n",
			 RINGBUF_TYPE_PADDING);
	trace_seq_printf(s, "\ttime_extend : type == %d\n",
			 RINGBUF_TYPE_TIME_EXTEND);
	trace_seq_printf(s, "\ttime_stamp : type == %d\n",
			 RINGBUF_TYPE_TIME_STAMP);
	trace_seq_printf(s, "\tdata max type_len  == %d\n",
			 RINGBUF_TYPE_DATA_TYPE_LEN_MAX);

	return !trace_seq_has_overflowed(s);
}

/*
 * The ring buffer is made up of a list of pages. A separate list of pages is
 * allocated for each CPU. A writer may only write to a buffer that is
 * associated with the CPU it is currently executing on.  A reader may read
 * from any per cpu buffer.
 *
 * The reader is special. For each per cpu buffer, the reader has its own
 * reader page. When a reader has read the entire reader page, this reader
 * page is swapped with another page in the ring buffer.
 *
 * Now, as long as the writer is off the reader page, the reader can do what
 * ever it wants with that page. The writer will never write to that page
 * again (as long as it is out of the ring buffer).
 *
 * Here's some silly ASCII art.
 *
 *   +------+
 *   |reader|          RING BUFFER
 *   |page  |
 *   +------+        +---+   +---+   +---+
 *                   |   |-->|   |-->|   |
 *                   +---+   +---+   +---+
 *                     ^               |
 *                     |               |
 *                     +---------------+
 *
 *
 *   +------+
 *   |reader|          RING BUFFER
 *   |page  |------------------v
 *   +------+        +---+   +---+   +---+
 *                   |   |-->|   |-->|   |
 *                   +---+   +---+   +---+
 *                     ^               |
 *                     |               |
 *                     +---------------+
 *
 *
 *   +------+
 *   |reader|          RING BUFFER
 *   |page  |------------------v
 *   +------+        +---+   +---+   +---+
 *      ^            |   |-->|   |-->|   |
 *      |            +---+   +---+   +---+
 *      |                              |
 *      |                              |
 *      +------------------------------+
 *
 *
 *   +------+
 *   |buffer|          RING BUFFER
 *   |page  |------------------v
 *   +------+        +---+   +---+   +---+
 *      ^            |   |   |   |-->|   |
 *      |   New      +---+   +---+   +---+
 *      |  Reader------^               |
 *      |   page                       |
 *      +------------------------------+
 *
 *
 * After we make this swap, the reader can hand this page off to the splice
 * code and be done with it. It can even allocate a new page if it needs to
 * and swap that into the ring buffer.
 *
 * We will be using cmpxchg soon to make all this lockless.
 *
 */

/* Used for individual buffers (after the counter) */
#define RB_BUFFER_OFF		(1 << 20)

#define BUF_PAGE_HDR_SIZE offsetof(struct buffer_data_page, data)

#define RB_EVNT_HDR_SIZE (offsetof(struct ring_buffer_event, array))
#define RB_ALIGNMENT		4U
#define RB_MAX_SMALL_DATA	(RB_ALIGNMENT * RINGBUF_TYPE_DATA_TYPE_LEN_MAX)
#define RB_EVNT_MIN_SIZE	8U	/* two 32bit words */

#ifndef CONFIG_HAVE_64BIT_ALIGNED_ACCESS
# define RB_FORCE_8BYTE_ALIGNMENT	0
# define RB_ARCH_ALIGNMENT		RB_ALIGNMENT
#else
# define RB_FORCE_8BYTE_ALIGNMENT	1
# define RB_ARCH_ALIGNMENT		8U
#endif

#define RB_ALIGN_DATA		__aligned(RB_ARCH_ALIGNMENT)

/* define RINGBUF_TYPE_DATA for 'case RINGBUF_TYPE_DATA:' */
#define RINGBUF_TYPE_DATA 0 ... RINGBUF_TYPE_DATA_TYPE_LEN_MAX

enum {
	RB_LEN_TIME_EXTEND = 8,
	RB_LEN_TIME_STAMP =  8,
};

#define skip_time_extend(event) \
	((struct ring_buffer_event *)((char *)event + RB_LEN_TIME_EXTEND))

#define extended_time(event) \
	(event->type_len >= RINGBUF_TYPE_TIME_EXTEND)

static inline bool rb_null_event(struct ring_buffer_event *event)
{
	return event->type_len == RINGBUF_TYPE_PADDING && !event->time_delta;
}

static void rb_event_set_padding(struct ring_buffer_event *event)
{
	/* padding has a NULL time_delta */
	event->type_len = RINGBUF_TYPE_PADDING;
	event->time_delta = 0;
}

static unsigned
rb_event_data_length(struct ring_buffer_event *event)
{
	unsigned length;

	if (event->type_len)
		length = event->type_len * RB_ALIGNMENT;
	else
		length = event->array[0];
	return length + RB_EVNT_HDR_SIZE;
}

/*
 * Return the length of the given event. Will return
 * the length of the time extend if the event is a
 * time extend.
 */
static inline unsigned
rb_event_length(struct ring_buffer_event *event)
{
	switch (event->type_len) {
	case RINGBUF_TYPE_PADDING:
		if (rb_null_event(event))
			/* undefined */
			return -1;
		return  event->array[0] + RB_EVNT_HDR_SIZE;

	case RINGBUF_TYPE_TIME_EXTEND:
		return RB_LEN_TIME_EXTEND;

	case RINGBUF_TYPE_TIME_STAMP:
		return RB_LEN_TIME_STAMP;

	case RINGBUF_TYPE_DATA:
		return rb_event_data_length(event);
	default:
		WARN_ON_ONCE(1);
	}
	/* not hit */
	return 0;
}

/*
 * Return total length of time extend and data,
 *   or just the event length for all other events.
 */
static inline unsigned
rb_event_ts_length(struct ring_buffer_event *event)
{
	unsigned len = 0;

	if (extended_time(event)) {
		/* time extends include the data event after it */
		len = RB_LEN_TIME_EXTEND;
		event = skip_time_extend(event);
	}
	return len + rb_event_length(event);
}

/**
 * ring_buffer_event_length - return the length of the event
 * @event: the event to get the length of
 *
 * Returns the size of the data load of a data event.
 * If the event is something other than a data event, it
 * returns the size of the event itself. With the exception
 * of a TIME EXTEND, where it still returns the size of the
 * data load of the data event after it.
 */
unsigned ring_buffer_event_length(struct ring_buffer_event *event)
{
	unsigned length;

	if (extended_time(event))
		event = skip_time_extend(event);

	length = rb_event_length(event);
	if (event->type_len > RINGBUF_TYPE_DATA_TYPE_LEN_MAX)
		return length;
	length -= RB_EVNT_HDR_SIZE;
	if (length > RB_MAX_SMALL_DATA + sizeof(event->array[0]))
                length -= sizeof(event->array[0]);
	return length;
}
EXPORT_SYMBOL_GPL(ring_buffer_event_length);

/* inline for ring buffer fast paths */
static __always_inline void *
rb_event_data(struct ring_buffer_event *event)
{
	if (extended_time(event))
		event = skip_time_extend(event);
	WARN_ON_ONCE(event->type_len > RINGBUF_TYPE_DATA_TYPE_LEN_MAX);
	/* If length is in len field, then array[0] has the data */
	if (event->type_len)
		return (void *)&event->array[0];
	/* Otherwise length is in array[0] and array[1] has the data */
	return (void *)&event->array[1];
}

/**
 * ring_buffer_event_data - return the data of the event
 * @event: the event to get the data from
 */
void *ring_buffer_event_data(struct ring_buffer_event *event)
{
	return rb_event_data(event);
}
EXPORT_SYMBOL_GPL(ring_buffer_event_data);

#define for_each_buffer_cpu(buffer, cpu)		\
	for_each_cpu(cpu, buffer->cpumask)

#define for_each_online_buffer_cpu(buffer, cpu)		\
	for_each_cpu_and(cpu, buffer->cpumask, cpu_online_mask)

#define TS_SHIFT	27
#define TS_MASK		((1ULL << TS_SHIFT) - 1)
#define TS_DELTA_TEST	(~TS_MASK)

static u64 rb_event_time_stamp(struct ring_buffer_event *event)
{
	u64 ts;

	ts = event->array[0];
	ts <<= TS_SHIFT;
	ts += event->time_delta;

	return ts;
}

/* Flag when events were overwritten */
#define RB_MISSED_EVENTS	(1 << 31)
/* Missed count stored at end */
#define RB_MISSED_STORED	(1 << 30)

#define RB_MISSED_MASK		(3 << 30)

struct buffer_data_page {
	u64		 time_stamp;	/* page time stamp */
	local_t		 commit;	/* write committed index */
	unsigned char	 data[] RB_ALIGN_DATA;	/* data of buffer page */
};

struct buffer_data_read_page {
	unsigned		order;	/* order of the page */
	struct buffer_data_page	*data;	/* actual data, stored in this page */
};

/*
 * Note, the buffer_page list must be first. The buffer pages
 * are allocated in cache lines, which means that each buffer
 * page will be at the beginning of a cache line, and thus
 * the least significant bits will be zero. We use this to
 * add flags in the list struct pointers, to make the ring buffer
 * lockless.
 */
struct buffer_page {
	struct list_head list;		/* list of buffer pages */
	local_t		 write;		/* index for next write */
	unsigned	 read;		/* index for next read */
	local_t		 entries;	/* entries on this page */
	unsigned long	 real_end;	/* real end of data */
	unsigned	 order;		/* order of the page */
	u32		 id:30;		/* ID for external mapping */
	u32		 range:1;	/* Mapped via a range */
	struct buffer_data_page *page;	/* Actual data page */
};

/*
 * The buffer page counters, write and entries, must be reset
 * atomically when crossing page boundaries. To synchronize this
 * update, two counters are inserted into the number. One is
 * the actual counter for the write position or count on the page.
 *
 * The other is a counter of updaters. Before an update happens
 * the update partition of the counter is incremented. This will
 * allow the updater to update the counter atomically.
 *
 * The counter is 20 bits, and the state data is 12.
 */
#define RB_WRITE_MASK		0xfffff
#define RB_WRITE_INTCNT		(1 << 20)

static void rb_init_page(struct buffer_data_page *bpage)
{
	local_set(&bpage->commit, 0);
}

static __always_inline unsigned int rb_page_commit(struct buffer_page *bpage)
{
	return local_read(&bpage->page->commit);
}

static void free_buffer_page(struct buffer_page *bpage)
{
	/* Range pages are not to be freed */
	if (!bpage->range)
		free_pages((unsigned long)bpage->page, bpage->order);
	kfree(bpage);
}

/*
 * We need to fit the time_stamp delta into 27 bits.
 */
static inline bool test_time_stamp(u64 delta)
{
	return !!(delta & TS_DELTA_TEST);
}

struct rb_irq_work {
	struct irq_work			work;
	wait_queue_head_t		waiters;
	wait_queue_head_t		full_waiters;
	atomic_t			seq;
	bool				waiters_pending;
	bool				full_waiters_pending;
	bool				wakeup_full;
};

/*
 * Structure to hold event state and handle nested events.
 */
struct rb_event_info {
	u64			ts;
	u64			delta;
	u64			before;
	u64			after;
	unsigned long		length;
	struct buffer_page	*tail_page;
	int			add_timestamp;
};

/*
 * Used for the add_timestamp
 *  NONE
 *  EXTEND - wants a time extend
 *  ABSOLUTE - the buffer requests all events to have absolute time stamps
 *  FORCE - force a full time stamp.
 */
enum {
	RB_ADD_STAMP_NONE		= 0,
	RB_ADD_STAMP_EXTEND		= BIT(1),
	RB_ADD_STAMP_ABSOLUTE		= BIT(2),
	RB_ADD_STAMP_FORCE		= BIT(3)
};
/*
 * Used for which event context the event is in.
 *  TRANSITION = 0
 *  NMI     = 1
 *  IRQ     = 2
 *  SOFTIRQ = 3
 *  NORMAL  = 4
 *
 * See trace_recursive_lock() comment below for more details.
 */
enum {
	RB_CTX_TRANSITION,
	RB_CTX_NMI,
	RB_CTX_IRQ,
	RB_CTX_SOFTIRQ,
	RB_CTX_NORMAL,
	RB_CTX_MAX
};

struct rb_time_struct {
	local64_t	time;
};
typedef struct rb_time_struct rb_time_t;

#define MAX_NEST	5

/*
 * head_page == tail_page && head == tail then buffer is empty.
 */
struct ring_buffer_per_cpu {
	int				cpu;
	atomic_t			record_disabled;
	atomic_t			resize_disabled;
	struct trace_buffer	*buffer;
	raw_spinlock_t			reader_lock;	/* serialize readers */
	arch_spinlock_t			lock;
	struct lock_class_key		lock_key;
	struct buffer_data_page		*free_page;
	unsigned long			nr_pages;
	unsigned int			current_context;
	struct list_head		*pages;
	/* pages generation counter, incremented when the list changes */
	unsigned long			cnt;
	struct buffer_page		*head_page;	/* read from head */
	struct buffer_page		*tail_page;	/* write to tail */
	struct buffer_page		*commit_page;	/* committed pages */
	struct buffer_page		*reader_page;
	unsigned long			lost_events;
	unsigned long			last_overrun;
	unsigned long			nest;
	local_t				entries_bytes;
	local_t				entries;
	local_t				overrun;
	local_t				commit_overrun;
	local_t				dropped_events;
	local_t				committing;
	local_t				commits;
	local_t				pages_touched;
	local_t				pages_lost;
	local_t				pages_read;
	long				last_pages_touch;
	size_t				shortest_full;
	unsigned long			read;
	unsigned long			read_bytes;
	rb_time_t			write_stamp;
	rb_time_t			before_stamp;
	u64				event_stamp[MAX_NEST];
	u64				read_stamp;
	/* pages removed since last reset */
	unsigned long			pages_removed;

	unsigned int			mapped;
	unsigned int			user_mapped;	/* user space mapping */
	struct mutex			mapping_lock;
	unsigned long			*subbuf_ids;	/* ID to subbuf VA */
	struct trace_buffer_meta	*meta_page;
	struct ring_buffer_meta		*ring_meta;

	/* ring buffer pages to update, > 0 to add, < 0 to remove */
	long				nr_pages_to_update;
	struct list_head		new_pages; /* new pages to add */
	struct work_struct		update_pages_work;
	struct completion		update_done;

	struct rb_irq_work		irq_work;
};

struct trace_buffer {
	unsigned			flags;
	int				cpus;
	atomic_t			record_disabled;
	atomic_t			resizing;
	cpumask_var_t			cpumask;

	struct lock_class_key		*reader_lock_key;

	struct mutex			mutex;

	struct ring_buffer_per_cpu	**buffers;

	struct hlist_node		node;
	u64				(*clock)(void);

	struct rb_irq_work		irq_work;
	bool				time_stamp_abs;

	unsigned long			range_addr_start;
	unsigned long			range_addr_end;

	long				last_text_delta;
	long				last_data_delta;

	unsigned int			subbuf_size;
	unsigned int			subbuf_order;
	unsigned int			max_data_size;
};

struct ring_buffer_iter {
	struct ring_buffer_per_cpu	*cpu_buffer;
	unsigned long			head;
	unsigned long			next_event;
	struct buffer_page		*head_page;
	struct buffer_page		*cache_reader_page;
	unsigned long			cache_read;
	unsigned long			cache_pages_removed;
	u64				read_stamp;
	u64				page_stamp;
	struct ring_buffer_event	*event;
	size_t				event_size;
	int				missed_events;
};

int ring_buffer_print_page_header(struct trace_buffer *buffer, struct trace_seq *s)
{
	struct buffer_data_page field;

	trace_seq_printf(s, "\tfield: u64 timestamp;\t"
			 "offset:0;\tsize:%u;\tsigned:%u;\n",
			 (unsigned int)sizeof(field.time_stamp),
			 (unsigned int)is_signed_type(u64));

	trace_seq_printf(s, "\tfield: local_t commit;\t"
			 "offset:%u;\tsize:%u;\tsigned:%u;\n",
			 (unsigned int)offsetof(typeof(field), commit),
			 (unsigned int)sizeof(field.commit),
			 (unsigned int)is_signed_type(long));

	trace_seq_printf(s, "\tfield: int overwrite;\t"
			 "offset:%u;\tsize:%u;\tsigned:%u;\n",
			 (unsigned int)offsetof(typeof(field), commit),
			 1,
			 (unsigned int)is_signed_type(long));

	trace_seq_printf(s, "\tfield: char data;\t"
			 "offset:%u;\tsize:%u;\tsigned:%u;\n",
			 (unsigned int)offsetof(typeof(field), data),
			 (unsigned int)buffer->subbuf_size,
			 (unsigned int)is_signed_type(char));

	return !trace_seq_has_overflowed(s);
}

static inline void rb_time_read(rb_time_t *t, u64 *ret)
{
	*ret = local64_read(&t->time);
}
static void rb_time_set(rb_time_t *t, u64 val)
{
	local64_set(&t->time, val);
}

/*
 * Enable this to make sure that the event passed to
 * ring_buffer_event_time_stamp() is not committed and also
 * is on the buffer that it passed in.
 */
//#define RB_VERIFY_EVENT
#ifdef RB_VERIFY_EVENT
static struct list_head *rb_list_head(struct list_head *list);
static void verify_event(struct ring_buffer_per_cpu *cpu_buffer,
			 void *event)
{
	struct buffer_page *page = cpu_buffer->commit_page;
	struct buffer_page *tail_page = READ_ONCE(cpu_buffer->tail_page);
	struct list_head *next;
	long commit, write;
	unsigned long addr = (unsigned long)event;
	bool done = false;
	int stop = 0;

	/* Make sure the event exists and is not committed yet */
	do {
		if (page == tail_page || WARN_ON_ONCE(stop++ > 100))
			done = true;
		commit = local_read(&page->page->commit);
		write = local_read(&page->write);
		if (addr >= (unsigned long)&page->page->data[commit] &&
		    addr < (unsigned long)&page->page->data[write])
			return;

		next = rb_list_head(page->list.next);
		page = list_entry(next, struct buffer_page, list);
	} while (!done);
	WARN_ON_ONCE(1);
}
#else
static inline void verify_event(struct ring_buffer_per_cpu *cpu_buffer,
			 void *event)
{
}
#endif

/*
 * The absolute time stamp drops the 5 MSBs and some clocks may
 * require them. The rb_fix_abs_ts() will take a previous full
 * time stamp, and add the 5 MSB of that time stamp on to the
 * saved absolute time stamp. Then they are compared in case of
 * the unlikely event that the latest time stamp incremented
 * the 5 MSB.
 */
static inline u64 rb_fix_abs_ts(u64 abs, u64 save_ts)
{
	if (save_ts & TS_MSB) {
		abs |= save_ts & TS_MSB;
		/* Check for overflow */
		if (unlikely(abs < save_ts))
			abs += 1ULL << 59;
	}
	return abs;
}

static inline u64 rb_time_stamp(struct trace_buffer *buffer);

/**
 * ring_buffer_event_time_stamp - return the event's current time stamp
 * @buffer: The buffer that the event is on
 * @event: the event to get the time stamp of
 *
 * Note, this must be called after @event is reserved, and before it is
 * committed to the ring buffer. And must be called from the same
 * context where the event was reserved (normal, softirq, irq, etc).
 *
 * Returns the time stamp associated with the current event.
 * If the event has an extended time stamp, then that is used as
 * the time stamp to return.
 * In the highly unlikely case that the event was nested more than
 * the max nesting, then the write_stamp of the buffer is returned,
 * otherwise  current time is returned, but that really neither of
 * the last two cases should ever happen.
 */
u64 ring_buffer_event_time_stamp(struct trace_buffer *buffer,
				 struct ring_buffer_event *event)
{
	struct ring_buffer_per_cpu *cpu_buffer = buffer->buffers[smp_processor_id()];
	unsigned int nest;
	u64 ts;

	/* If the event includes an absolute time, then just use that */
	if (event->type_len == RINGBUF_TYPE_TIME_STAMP) {
		ts = rb_event_time_stamp(event);
		return rb_fix_abs_ts(ts, cpu_buffer->tail_page->page->time_stamp);
	}

	nest = local_read(&cpu_buffer->committing);
	verify_event(cpu_buffer, event);
	if (WARN_ON_ONCE(!nest))
		goto fail;

	/* Read the current saved nesting level time stamp */
	if (likely(--nest < MAX_NEST))
		return cpu_buffer->event_stamp[nest];

	/* Shouldn't happen, warn if it does */
	WARN_ONCE(1, "nest (%d) greater than max", nest);

 fail:
	rb_time_read(&cpu_buffer->write_stamp, &ts);

	return ts;
}

/**
 * ring_buffer_nr_dirty_pages - get the number of used pages in the ring buffer
 * @buffer: The ring_buffer to get the number of pages from
 * @cpu: The cpu of the ring_buffer to get the number of pages from
 *
 * Returns the number of pages that have content in the ring buffer.
 */
size_t ring_buffer_nr_dirty_pages(struct trace_buffer *buffer, int cpu)
{
	size_t read;
	size_t lost;
	size_t cnt;

	read = local_read(&buffer->buffers[cpu]->pages_read);
	lost = local_read(&buffer->buffers[cpu]->pages_lost);
	cnt = local_read(&buffer->buffers[cpu]->pages_touched);

	if (WARN_ON_ONCE(cnt < lost))
		return 0;

	cnt -= lost;

	/* The reader can read an empty page, but not more than that */
	if (cnt < read) {
		WARN_ON_ONCE(read > cnt + 1);
		return 0;
	}

	return cnt - read;
}

static __always_inline bool full_hit(struct trace_buffer *buffer, int cpu, int full)
{
	struct ring_buffer_per_cpu *cpu_buffer = buffer->buffers[cpu];
	size_t nr_pages;
	size_t dirty;

	nr_pages = cpu_buffer->nr_pages;
	if (!nr_pages || !full)
		return true;

	/*
	 * Add one as dirty will never equal nr_pages, as the sub-buffer
	 * that the writer is on is not counted as dirty.
	 * This is needed if "buffer_percent" is set to 100.
	 */
	dirty = ring_buffer_nr_dirty_pages(buffer, cpu) + 1;

	return (dirty * 100) >= (full * nr_pages);
}

/*
 * rb_wake_up_waiters - wake up tasks waiting for ring buffer input
 *
 * Schedules a delayed work to wake up any task that is blocked on the
 * ring buffer waiters queue.
 */
static void rb_wake_up_waiters(struct irq_work *work)
{
	struct rb_irq_work *rbwork = container_of(work, struct rb_irq_work, work);

	/* For waiters waiting for the first wake up */
	(void)atomic_fetch_inc_release(&rbwork->seq);

	wake_up_all(&rbwork->waiters);
	if (rbwork->full_waiters_pending || rbwork->wakeup_full) {
		/* Only cpu_buffer sets the above flags */
		struct ring_buffer_per_cpu *cpu_buffer =
			container_of(rbwork, struct ring_buffer_per_cpu, irq_work);

		/* Called from interrupt context */
		raw_spin_lock(&cpu_buffer->reader_lock);
		rbwork->wakeup_full = false;
		rbwork->full_waiters_pending = false;

		/* Waking up all waiters, they will reset the shortest full */
		cpu_buffer->shortest_full = 0;
		raw_spin_unlock(&cpu_buffer->reader_lock);

		wake_up_all(&rbwork->full_waiters);
	}
}

/**
 * ring_buffer_wake_waiters - wake up any waiters on this ring buffer
 * @buffer: The ring buffer to wake waiters on
 * @cpu: The CPU buffer to wake waiters on
 *
 * In the case of a file that represents a ring buffer is closing,
 * it is prudent to wake up any waiters that are on this.
 */
void ring_buffer_wake_waiters(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct rb_irq_work *rbwork;

	if (!buffer)
		return;

	if (cpu == RING_BUFFER_ALL_CPUS) {

		/* Wake up individual ones too. One level recursion */
		for_each_buffer_cpu(buffer, cpu)
			ring_buffer_wake_waiters(buffer, cpu);

		rbwork = &buffer->irq_work;
	} else {
		if (WARN_ON_ONCE(!buffer->buffers))
			return;
		if (WARN_ON_ONCE(cpu >= nr_cpu_ids))
			return;

		cpu_buffer = buffer->buffers[cpu];
		/* The CPU buffer may not have been initialized yet */
		if (!cpu_buffer)
			return;
		rbwork = &cpu_buffer->irq_work;
	}

	/* This can be called in any context */
	irq_work_queue(&rbwork->work);
}

static bool rb_watermark_hit(struct trace_buffer *buffer, int cpu, int full)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	bool ret = false;

	/* Reads of all CPUs always waits for any data */
	if (cpu == RING_BUFFER_ALL_CPUS)
		return !ring_buffer_empty(buffer);

	cpu_buffer = buffer->buffers[cpu];

	if (!ring_buffer_empty_cpu(buffer, cpu)) {
		unsigned long flags;
		bool pagebusy;

		if (!full)
			return true;

		raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);
		pagebusy = cpu_buffer->reader_page == cpu_buffer->commit_page;
		ret = !pagebusy && full_hit(buffer, cpu, full);

		if (!ret && (!cpu_buffer->shortest_full ||
			     cpu_buffer->shortest_full > full)) {
		    cpu_buffer->shortest_full = full;
		}
		raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);
	}
	return ret;
}

static inline bool
rb_wait_cond(struct rb_irq_work *rbwork, struct trace_buffer *buffer,
	     int cpu, int full, ring_buffer_cond_fn cond, void *data)
{
	if (rb_watermark_hit(buffer, cpu, full))
		return true;

	if (cond(data))
		return true;

	/*
	 * The events can happen in critical sections where
	 * checking a work queue can cause deadlocks.
	 * After adding a task to the queue, this flag is set
	 * only to notify events to try to wake up the queue
	 * using irq_work.
	 *
	 * We don't clear it even if the buffer is no longer
	 * empty. The flag only causes the next event to run
	 * irq_work to do the work queue wake up. The worse
	 * that can happen if we race with !trace_empty() is that
	 * an event will cause an irq_work to try to wake up
	 * an empty queue.
	 *
	 * There's no reason to protect this flag either, as
	 * the work queue and irq_work logic will do the necessary
	 * synchronization for the wake ups. The only thing
	 * that is necessary is that the wake up happens after
	 * a task has been queued. It's OK for spurious wake ups.
	 */
	if (full)
		rbwork->full_waiters_pending = true;
	else
		rbwork->waiters_pending = true;

	return false;
}

struct rb_wait_data {
	struct rb_irq_work		*irq_work;
	int				seq;
};

/*
 * The default wait condition for ring_buffer_wait() is to just to exit the
 * wait loop the first time it is woken up.
 */
static bool rb_wait_once(void *data)
{
	struct rb_wait_data *rdata = data;
	struct rb_irq_work *rbwork = rdata->irq_work;

	return atomic_read_acquire(&rbwork->seq) != rdata->seq;
}

/**
 * ring_buffer_wait - wait for input to the ring buffer
 * @buffer: buffer to wait on
 * @cpu: the cpu buffer to wait on
 * @full: wait until the percentage of pages are available, if @cpu != RING_BUFFER_ALL_CPUS
 * @cond: condition function to break out of wait (NULL to run once)
 * @data: the data to pass to @cond.
 *
 * If @cpu == RING_BUFFER_ALL_CPUS then the task will wake up as soon
 * as data is added to any of the @buffer's cpu buffers. Otherwise
 * it will wait for data to be added to a specific cpu buffer.
 */
int ring_buffer_wait(struct trace_buffer *buffer, int cpu, int full,
		     ring_buffer_cond_fn cond, void *data)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct wait_queue_head *waitq;
	struct rb_irq_work *rbwork;
	struct rb_wait_data rdata;
	int ret = 0;

	/*
	 * Depending on what the caller is waiting for, either any
	 * data in any cpu buffer, or a specific buffer, put the
	 * caller on the appropriate wait queue.
	 */
	if (cpu == RING_BUFFER_ALL_CPUS) {
		rbwork = &buffer->irq_work;
		/* Full only makes sense on per cpu reads */
		full = 0;
	} else {
		if (!cpumask_test_cpu(cpu, buffer->cpumask))
			return -ENODEV;
		cpu_buffer = buffer->buffers[cpu];
		rbwork = &cpu_buffer->irq_work;
	}

	if (full)
		waitq = &rbwork->full_waiters;
	else
		waitq = &rbwork->waiters;

	/* Set up to exit loop as soon as it is woken */
	if (!cond) {
		cond = rb_wait_once;
		rdata.irq_work = rbwork;
		rdata.seq = atomic_read_acquire(&rbwork->seq);
		data = &rdata;
	}

	ret = wait_event_interruptible((*waitq),
				rb_wait_cond(rbwork, buffer, cpu, full, cond, data));

	return ret;
}

/**
 * ring_buffer_poll_wait - poll on buffer input
 * @buffer: buffer to wait on
 * @cpu: the cpu buffer to wait on
 * @filp: the file descriptor
 * @poll_table: The poll descriptor
 * @full: wait until the percentage of pages are available, if @cpu != RING_BUFFER_ALL_CPUS
 *
 * If @cpu == RING_BUFFER_ALL_CPUS then the task will wake up as soon
 * as data is added to any of the @buffer's cpu buffers. Otherwise
 * it will wait for data to be added to a specific cpu buffer.
 *
 * Returns EPOLLIN | EPOLLRDNORM if data exists in the buffers,
 * zero otherwise.
 */
__poll_t ring_buffer_poll_wait(struct trace_buffer *buffer, int cpu,
			  struct file *filp, poll_table *poll_table, int full)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct rb_irq_work *rbwork;

	if (cpu == RING_BUFFER_ALL_CPUS) {
		rbwork = &buffer->irq_work;
		full = 0;
	} else {
		if (!cpumask_test_cpu(cpu, buffer->cpumask))
			return EPOLLERR;

		cpu_buffer = buffer->buffers[cpu];
		rbwork = &cpu_buffer->irq_work;
	}

	if (full) {
		poll_wait(filp, &rbwork->full_waiters, poll_table);

		if (rb_watermark_hit(buffer, cpu, full))
			return EPOLLIN | EPOLLRDNORM;
		/*
		 * Only allow full_waiters_pending update to be seen after
		 * the shortest_full is set (in rb_watermark_hit). If the
		 * writer sees the full_waiters_pending flag set, it will
		 * compare the amount in the ring buffer to shortest_full.
		 * If the amount in the ring buffer is greater than the
		 * shortest_full percent, it will call the irq_work handler
		 * to wake up this list. The irq_handler will reset shortest_full
		 * back to zero. That's done under the reader_lock, but
		 * the below smp_mb() makes sure that the update to
		 * full_waiters_pending doesn't leak up into the above.
		 */
		smp_mb();
		rbwork->full_waiters_pending = true;
		return 0;
	}

	poll_wait(filp, &rbwork->waiters, poll_table);
	rbwork->waiters_pending = true;

	/*
	 * There's a tight race between setting the waiters_pending and
	 * checking if the ring buffer is empty.  Once the waiters_pending bit
	 * is set, the next event will wake the task up, but we can get stuck
	 * if there's only a single event in.
	 *
	 * FIXME: Ideally, we need a memory barrier on the writer side as well,
	 * but adding a memory barrier to all events will cause too much of a
	 * performance hit in the fast path.  We only need a memory barrier when
	 * the buffer goes from empty to having content.  But as this race is
	 * extremely small, and it's not a problem if another event comes in, we
	 * will fix it later.
	 */
	smp_mb();

	if ((cpu == RING_BUFFER_ALL_CPUS && !ring_buffer_empty(buffer)) ||
	    (cpu != RING_BUFFER_ALL_CPUS && !ring_buffer_empty_cpu(buffer, cpu)))
		return EPOLLIN | EPOLLRDNORM;
	return 0;
}

/* buffer may be either ring_buffer or ring_buffer_per_cpu */
#define RB_WARN_ON(b, cond)						\
	({								\
		int _____ret = unlikely(cond);				\
		if (_____ret) {						\
			if (__same_type(*(b), struct ring_buffer_per_cpu)) { \
				struct ring_buffer_per_cpu *__b =	\
					(void *)b;			\
				atomic_inc(&__b->buffer->record_disabled); \
			} else						\
				atomic_inc(&b->record_disabled);	\
			WARN_ON(1);					\
		}							\
		_____ret;						\
	})

/* Up this if you want to test the TIME_EXTENTS and normalization */
#define DEBUG_SHIFT 0

static inline u64 rb_time_stamp(struct trace_buffer *buffer)
{
	u64 ts;

	/* Skip retpolines :-( */
	if (IS_ENABLED(CONFIG_MITIGATION_RETPOLINE) && likely(buffer->clock == trace_clock_local))
		ts = trace_clock_local();
	else
		ts = buffer->clock();

	/* shift to debug/test normalization and TIME_EXTENTS */
	return ts << DEBUG_SHIFT;
}

u64 ring_buffer_time_stamp(struct trace_buffer *buffer)
{
	u64 time;

	preempt_disable_notrace();
	time = rb_time_stamp(buffer);
	preempt_enable_notrace();

	return time;
}
EXPORT_SYMBOL_GPL(ring_buffer_time_stamp);

void ring_buffer_normalize_time_stamp(struct trace_buffer *buffer,
				      int cpu, u64 *ts)
{
	/* Just stupid testing the normalize function and deltas */
	*ts >>= DEBUG_SHIFT;
}
EXPORT_SYMBOL_GPL(ring_buffer_normalize_time_stamp);

/*
 * Making the ring buffer lockless makes things tricky.
 * Although writes only happen on the CPU that they are on,
 * and they only need to worry about interrupts. Reads can
 * happen on any CPU.
 *
 * The reader page is always off the ring buffer, but when the
 * reader finishes with a page, it needs to swap its page with
 * a new one from the buffer. The reader needs to take from
 * the head (writes go to the tail). But if a writer is in overwrite
 * mode and wraps, it must push the head page forward.
 *
 * Here lies the problem.
 *
 * The reader must be careful to replace only the head page, and
 * not another one. As described at the top of the file in the
 * ASCII art, the reader sets its old page to point to the next
 * page after head. It then sets the page after head to point to
 * the old reader page. But if the writer moves the head page
 * during this operation, the reader could end up with the tail.
 *
 * We use cmpxchg to help prevent this race. We also do something
 * special with the page before head. We set the LSB to 1.
 *
 * When the writer must push the page forward, it will clear the
 * bit that points to the head page, move the head, and then set
 * the bit that points to the new head page.
 *
 * We also don't want an interrupt coming in and moving the head
 * page on another writer. Thus we use the second LSB to catch
 * that too. Thus:
 *
 * head->list->prev->next        bit 1          bit 0
 *                              -------        -------
 * Normal page                     0              0
 * Points to head page             0              1
 * New head page                   1              0
 *
 * Note we can not trust the prev pointer of the head page, because:
 *
 * +----+       +-----+        +-----+
 * |    |------>|  T  |---X--->|  N  |
 * |    |<------|     |        |     |
 * +----+       +-----+        +-----+
 *   ^                           ^ |
 *   |          +-----+          | |
 *   +----------|  R  |----------+ |
 *              |     |<-----------+
 *              +-----+
 *
 * Key:  ---X-->  HEAD flag set in pointer
 *         T      Tail page
 *         R      Reader page
 *         N      Next page
 *
 * (see __rb_reserve_next() to see where this happens)
 *
 *  What the above shows is that the reader just swapped out
 *  the reader page with a page in the buffer, but before it
 *  could make the new header point back to the new page added
 *  it was preempted by a writer. The writer moved forward onto
 *  the new page added by the reader and is about to move forward
 *  again.
 *
 *  You can see, it is legitimate for the previous pointer of
 *  the head (or any page) not to point back to itself. But only
 *  temporarily.
 */

#define RB_PAGE_NORMAL		0UL
#define RB_PAGE_HEAD		1UL
#define RB_PAGE_UPDATE		2UL


#define RB_FLAG_MASK		3UL

/* PAGE_MOVED is not part of the mask */
#define RB_PAGE_MOVED		4UL

/*
 * rb_list_head - remove any bit
 */
static struct list_head *rb_list_head(struct list_head *list)
{
	unsigned long val = (unsigned long)list;

	return (struct list_head *)(val & ~RB_FLAG_MASK);
}

/*
 * rb_is_head_page - test if the given page is the head page
 *
 * Because the reader may move the head_page pointer, we can
 * not trust what the head page is (it may be pointing to
 * the reader page). But if the next page is a header page,
 * its flags will be non zero.
 */
static inline int
rb_is_head_page(struct buffer_page *page, struct list_head *list)
{
	unsigned long val;

	val = (unsigned long)list->next;

	if ((val & ~RB_FLAG_MASK) != (unsigned long)&page->list)
		return RB_PAGE_MOVED;

	return val & RB_FLAG_MASK;
}

/*
 * rb_is_reader_page
 *
 * The unique thing about the reader page, is that, if the
 * writer is ever on it, the previous pointer never points
 * back to the reader page.
 */
static bool rb_is_reader_page(struct buffer_page *page)
{
	struct list_head *list = page->list.prev;

	return rb_list_head(list->next) != &page->list;
}

/*
 * rb_set_list_to_head - set a list_head to be pointing to head.
 */
static void rb_set_list_to_head(struct list_head *list)
{
	unsigned long *ptr;

	ptr = (unsigned long *)&list->next;
	*ptr |= RB_PAGE_HEAD;
	*ptr &= ~RB_PAGE_UPDATE;
}

/*
 * rb_head_page_activate - sets up head page
 */
static void rb_head_page_activate(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct buffer_page *head;

	head = cpu_buffer->head_page;
	if (!head)
		return;

	/*
	 * Set the previous list pointer to have the HEAD flag.
	 */
	rb_set_list_to_head(head->list.prev);

	if (cpu_buffer->ring_meta) {
		struct ring_buffer_meta *meta = cpu_buffer->ring_meta;
		meta->head_buffer = (unsigned long)head->page;
	}
}

static void rb_list_head_clear(struct list_head *list)
{
	unsigned long *ptr = (unsigned long *)&list->next;

	*ptr &= ~RB_FLAG_MASK;
}

/*
 * rb_head_page_deactivate - clears head page ptr (for free list)
 */
static void
rb_head_page_deactivate(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct list_head *hd;

	/* Go through the whole list and clear any pointers found. */
	rb_list_head_clear(cpu_buffer->pages);

	list_for_each(hd, cpu_buffer->pages)
		rb_list_head_clear(hd);
}

static int rb_head_page_set(struct ring_buffer_per_cpu *cpu_buffer,
			    struct buffer_page *head,
			    struct buffer_page *prev,
			    int old_flag, int new_flag)
{
	struct list_head *list;
	unsigned long val = (unsigned long)&head->list;
	unsigned long ret;

	list = &prev->list;

	val &= ~RB_FLAG_MASK;

	ret = cmpxchg((unsigned long *)&list->next,
		      val | old_flag, val | new_flag);

	/* check if the reader took the page */
	if ((ret & ~RB_FLAG_MASK) != val)
		return RB_PAGE_MOVED;

	return ret & RB_FLAG_MASK;
}

static int rb_head_page_set_update(struct ring_buffer_per_cpu *cpu_buffer,
				   struct buffer_page *head,
				   struct buffer_page *prev,
				   int old_flag)
{
	return rb_head_page_set(cpu_buffer, head, prev,
				old_flag, RB_PAGE_UPDATE);
}

static int rb_head_page_set_head(struct ring_buffer_per_cpu *cpu_buffer,
				 struct buffer_page *head,
				 struct buffer_page *prev,
				 int old_flag)
{
	return rb_head_page_set(cpu_buffer, head, prev,
				old_flag, RB_PAGE_HEAD);
}

static int rb_head_page_set_normal(struct ring_buffer_per_cpu *cpu_buffer,
				   struct buffer_page *head,
				   struct buffer_page *prev,
				   int old_flag)
{
	return rb_head_page_set(cpu_buffer, head, prev,
				old_flag, RB_PAGE_NORMAL);
}

static inline void rb_inc_page(struct buffer_page **bpage)
{
	struct list_head *p = rb_list_head((*bpage)->list.next);

	*bpage = list_entry(p, struct buffer_page, list);
}

static struct buffer_page *
rb_set_head_page(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct buffer_page *head;
	struct buffer_page *page;
	struct list_head *list;
	int i;

	if (RB_WARN_ON(cpu_buffer, !cpu_buffer->head_page))
		return NULL;

	/* sanity check */
	list = cpu_buffer->pages;
	if (RB_WARN_ON(cpu_buffer, rb_list_head(list->prev->next) != list))
		return NULL;

	page = head = cpu_buffer->head_page;
	/*
	 * It is possible that the writer moves the header behind
	 * where we started, and we miss in one loop.
	 * A second loop should grab the header, but we'll do
	 * three loops just because I'm paranoid.
	 */
	for (i = 0; i < 3; i++) {
		do {
			if (rb_is_head_page(page, page->list.prev)) {
				cpu_buffer->head_page = page;
				return page;
			}
			rb_inc_page(&page);
		} while (page != head);
	}

	RB_WARN_ON(cpu_buffer, 1);

	return NULL;
}

static bool rb_head_page_replace(struct buffer_page *old,
				struct buffer_page *new)
{
	unsigned long *ptr = (unsigned long *)&old->list.prev->next;
	unsigned long val;

	val = *ptr & ~RB_FLAG_MASK;
	val |= RB_PAGE_HEAD;

	return try_cmpxchg(ptr, &val, (unsigned long)&new->list);
}

/*
 * rb_tail_page_update - move the tail page forward
 */
static void rb_tail_page_update(struct ring_buffer_per_cpu *cpu_buffer,
			       struct buffer_page *tail_page,
			       struct buffer_page *next_page)
{
	unsigned long old_entries;
	unsigned long old_write;

	/*
	 * The tail page now needs to be moved forward.
	 *
	 * We need to reset the tail page, but without messing
	 * with possible erasing of data brought in by interrupts
	 * that have moved the tail page and are currently on it.
	 *
	 * We add a counter to the write field to denote this.
	 */
	old_write = local_add_return(RB_WRITE_INTCNT, &next_page->write);
	old_entries = local_add_return(RB_WRITE_INTCNT, &next_page->entries);

	/*
	 * Just make sure we have seen our old_write and synchronize
	 * with any interrupts that come in.
	 */
	barrier();

	/*
	 * If the tail page is still the same as what we think
	 * it is, then it is up to us to update the tail
	 * pointer.
	 */
	if (tail_page == READ_ONCE(cpu_buffer->tail_page)) {
		/* Zero the write counter */
		unsigned long val = old_write & ~RB_WRITE_MASK;
		unsigned long eval = old_entries & ~RB_WRITE_MASK;

		/*
		 * This will only succeed if an interrupt did
		 * not come in and change it. In which case, we
		 * do not want to modify it.
		 *
		 * We add (void) to let the compiler know that we do not care
		 * about the return value of these functions. We use the
		 * cmpxchg to only update if an interrupt did not already
		 * do it for us. If the cmpxchg fails, we don't care.
		 */
		(void)local_cmpxchg(&next_page->write, old_write, val);
		(void)local_cmpxchg(&next_page->entries, old_entries, eval);

		/*
		 * No need to worry about races with clearing out the commit.
		 * it only can increment when a commit takes place. But that
		 * only happens in the outer most nested commit.
		 */
		local_set(&next_page->page->commit, 0);

		/* Either we update tail_page or an interrupt does */
		if (try_cmpxchg(&cpu_buffer->tail_page, &tail_page, next_page))
			local_inc(&cpu_buffer->pages_touched);
	}
}

static void rb_check_bpage(struct ring_buffer_per_cpu *cpu_buffer,
			  struct buffer_page *bpage)
{
	unsigned long val = (unsigned long)bpage;

	RB_WARN_ON(cpu_buffer, val & RB_FLAG_MASK);
}

static bool rb_check_links(struct ring_buffer_per_cpu *cpu_buffer,
			   struct list_head *list)
{
	if (RB_WARN_ON(cpu_buffer,
		       rb_list_head(rb_list_head(list->next)->prev) != list))
		return false;

	if (RB_WARN_ON(cpu_buffer,
		       rb_list_head(rb_list_head(list->prev)->next) != list))
		return false;

	return true;
}

/**
 * rb_check_pages - integrity check of buffer pages
 * @cpu_buffer: CPU buffer with pages to test
 *
 * As a safety measure we check to make sure the data pages have not
 * been corrupted.
 */
static void rb_check_pages(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct list_head *head, *tmp;
	unsigned long buffer_cnt;
	unsigned long flags;
	int nr_loops = 0;

	/*
	 * Walk the linked list underpinning the ring buffer and validate all
	 * its next and prev links.
	 *
	 * The check acquires the reader_lock to avoid concurrent processing
	 * with code that could be modifying the list. However, the lock cannot
	 * be held for the entire duration of the walk, as this would make the
	 * time when interrupts are disabled non-deterministic, dependent on the
	 * ring buffer size. Therefore, the code releases and re-acquires the
	 * lock after checking each page. The ring_buffer_per_cpu.cnt variable
	 * is then used to detect if the list was modified while the lock was
	 * not held, in which case the check needs to be restarted.
	 *
	 * The code attempts to perform the check at most three times before
	 * giving up. This is acceptable because this is only a self-validation
	 * to detect problems early on. In practice, the list modification
	 * operations are fairly spaced, and so this check typically succeeds at
	 * most on the second try.
	 */
again:
	if (++nr_loops > 3)
		return;

	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);
	head = rb_list_head(cpu_buffer->pages);
	if (!rb_check_links(cpu_buffer, head))
		goto out_locked;
	buffer_cnt = cpu_buffer->cnt;
	tmp = head;
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);

	while (true) {
		raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);

		if (buffer_cnt != cpu_buffer->cnt) {
			/* The list was updated, try again. */
			raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);
			goto again;
		}

		tmp = rb_list_head(tmp->next);
		if (tmp == head)
			/* The iteration circled back, all is done. */
			goto out_locked;

		if (!rb_check_links(cpu_buffer, tmp))
			goto out_locked;

		raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);
	}

out_locked:
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);
}

/*
 * Take an address, add the meta data size as well as the array of
 * array subbuffer indexes, then align it to a subbuffer size.
 *
 * This is used to help find the next per cpu subbuffer within a mapped range.
 */
static unsigned long
rb_range_align_subbuf(unsigned long addr, int subbuf_size, int nr_subbufs)
{
	addr += sizeof(struct ring_buffer_meta) +
		sizeof(int) * nr_subbufs;
	return ALIGN(addr, subbuf_size);
}

/*
 * Return the ring_buffer_meta for a given @cpu.
 */
static void *rb_range_meta(struct trace_buffer *buffer, int nr_pages, int cpu)
{
	int subbuf_size = buffer->subbuf_size + BUF_PAGE_HDR_SIZE;
	unsigned long ptr = buffer->range_addr_start;
	struct ring_buffer_meta *meta;
	int nr_subbufs;

	if (!ptr)
		return NULL;

	/* When nr_pages passed in is zero, the first meta has already been initialized */
	if (!nr_pages) {
		meta = (struct ring_buffer_meta *)ptr;
		nr_subbufs = meta->nr_subbufs;
	} else {
		meta = NULL;
		/* Include the reader page */
		nr_subbufs = nr_pages + 1;
	}

	/*
	 * The first chunk may not be subbuffer aligned, where as
	 * the rest of the chunks are.
	 */
	if (cpu) {
		ptr = rb_range_align_subbuf(ptr, subbuf_size, nr_subbufs);
		ptr += subbuf_size * nr_subbufs;

		/* We can use multiplication to find chunks greater than 1 */
		if (cpu > 1) {
			unsigned long size;
			unsigned long p;

			/* Save the beginning of this CPU chunk */
			p = ptr;
			ptr = rb_range_align_subbuf(ptr, subbuf_size, nr_subbufs);
			ptr += subbuf_size * nr_subbufs;

			/* Now all chunks after this are the same size */
			size = ptr - p;
			ptr += size * (cpu - 2);
		}
	}
	return (void *)ptr;
}

/* Return the start of subbufs given the meta pointer */
static void *rb_subbufs_from_meta(struct ring_buffer_meta *meta)
{
	int subbuf_size = meta->subbuf_size;
	unsigned long ptr;

	ptr = (unsigned long)meta;
	ptr = rb_range_align_subbuf(ptr, subbuf_size, meta->nr_subbufs);

	return (void *)ptr;
}

/*
 * Return a specific sub-buffer for a given @cpu defined by @idx.
 */
static void *rb_range_buffer(struct ring_buffer_per_cpu *cpu_buffer, int idx)
{
	struct ring_buffer_meta *meta;
	unsigned long ptr;
	int subbuf_size;

	meta = rb_range_meta(cpu_buffer->buffer, 0, cpu_buffer->cpu);
	if (!meta)
		return NULL;

	if (WARN_ON_ONCE(idx >= meta->nr_subbufs))
		return NULL;

	subbuf_size = meta->subbuf_size;

	/* Map this buffer to the order that's in meta->buffers[] */
	idx = meta->buffers[idx];

	ptr = (unsigned long)rb_subbufs_from_meta(meta);

	ptr += subbuf_size * idx;
	if (ptr + subbuf_size > cpu_buffer->buffer->range_addr_end)
		return NULL;

	return (void *)ptr;
}

/*
 * See if the existing memory contains valid ring buffer data.
 * As the previous kernel must be the same as this kernel, all
 * the calculations (size of buffers and number of buffers)
 * must be the same.
 */
static bool rb_meta_valid(struct ring_buffer_meta *meta, int cpu,
			  struct trace_buffer *buffer, int nr_pages)
{
	int subbuf_size = PAGE_SIZE;
	struct buffer_data_page *subbuf;
	unsigned long buffers_start;
	unsigned long buffers_end;
	int i;

	/* Check the meta magic and meta struct size */
	if (meta->magic != RING_BUFFER_META_MAGIC ||
	    meta->struct_size != sizeof(*meta)) {
		pr_info("Ring buffer boot meta[%d] mismatch of magic or struct size\n", cpu);
		return false;
	}

	/* The subbuffer's size and number of subbuffers must match */
	if (meta->subbuf_size != subbuf_size ||
	    meta->nr_subbufs != nr_pages + 1) {
		pr_info("Ring buffer boot meta [%d] mismatch of subbuf_size/nr_pages\n", cpu);
		return false;
	}

	buffers_start = meta->first_buffer;
	buffers_end = meta->first_buffer + (subbuf_size * meta->nr_subbufs);

	/* Is the head and commit buffers within the range of buffers? */
	if (meta->head_buffer < buffers_start ||
	    meta->head_buffer >= buffers_end) {
		pr_info("Ring buffer boot meta [%d] head buffer out of range\n", cpu);
		return false;
	}

	if (meta->commit_buffer < buffers_start ||
	    meta->commit_buffer >= buffers_end) {
		pr_info("Ring buffer boot meta [%d] commit buffer out of range\n", cpu);
		return false;
	}

	subbuf = rb_subbufs_from_meta(meta);

	/* Is the meta buffers and the subbufs themselves have correct data? */
	for (i = 0; i < meta->nr_subbufs; i++) {
		if (meta->buffers[i] < 0 ||
		    meta->buffers[i] >= meta->nr_subbufs) {
			pr_info("Ring buffer boot meta [%d] array out of range\n", cpu);
			return false;
		}

		if ((unsigned)local_read(&subbuf->commit) > subbuf_size) {
			pr_info("Ring buffer boot meta [%d] buffer invalid commit\n", cpu);
			return false;
		}

		subbuf = (void *)subbuf + subbuf_size;
	}

	return true;
}

static int rb_meta_subbuf_idx(struct ring_buffer_meta *meta, void *subbuf);

static int rb_read_data_buffer(struct buffer_data_page *dpage, int tail, int cpu,
			       unsigned long long *timestamp, u64 *delta_ptr)
{
	struct ring_buffer_event *event;
	u64 ts, delta;
	int events = 0;
	int e;

	*delta_ptr = 0;
	*timestamp = 0;

	ts = dpage->time_stamp;

	for (e = 0; e < tail; e += rb_event_length(event)) {

		event = (struct ring_buffer_event *)(dpage->data + e);

		switch (event->type_len) {

		case RINGBUF_TYPE_TIME_EXTEND:
			delta = rb_event_time_stamp(event);
			ts += delta;
			break;

		case RINGBUF_TYPE_TIME_STAMP:
			delta = rb_event_time_stamp(event);
			delta = rb_fix_abs_ts(delta, ts);
			if (delta < ts) {
				*delta_ptr = delta;
				*timestamp = ts;
				return -1;
			}
			ts = delta;
			break;

		case RINGBUF_TYPE_PADDING:
			if (event->time_delta == 1)
				break;
			fallthrough;
		case RINGBUF_TYPE_DATA:
			events++;
			ts += event->time_delta;
			break;

		default:
			return -1;
		}
	}
	*timestamp = ts;
	return events;
}

static int rb_validate_buffer(struct buffer_data_page *dpage, int cpu)
{
	unsigned long long ts;
	u64 delta;
	int tail;

	tail = local_read(&dpage->commit);
	return rb_read_data_buffer(dpage, tail, cpu, &ts, &delta);
}

/* If the meta data has been validated, now validate the events */
static void rb_meta_validate_events(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct ring_buffer_meta *meta = cpu_buffer->ring_meta;
	struct buffer_page *head_page;
	unsigned long entry_bytes = 0;
	unsigned long entries = 0;
	int ret;
	int i;

	if (!meta || !meta->head_buffer)
		return;

	/* Do the reader page first */
	ret = rb_validate_buffer(cpu_buffer->reader_page->page, cpu_buffer->cpu);
	if (ret < 0) {
		pr_info("Ring buffer reader page is invalid\n");
		goto invalid;
	}
	entries += ret;
	entry_bytes += local_read(&cpu_buffer->reader_page->page->commit);
	local_set(&cpu_buffer->reader_page->entries, ret);

	head_page = cpu_buffer->head_page;

	/* If both the head and commit are on the reader_page then we are done. */
	if (head_page == cpu_buffer->reader_page &&
	    head_page == cpu_buffer->commit_page)
		goto done;

	/* Iterate until finding the commit page */
	for (i = 0; i < meta->nr_subbufs + 1; i++, rb_inc_page(&head_page)) {

		/* Reader page has already been done */
		if (head_page == cpu_buffer->reader_page)
			continue;

		ret = rb_validate_buffer(head_page->page, cpu_buffer->cpu);
		if (ret < 0) {
			pr_info("Ring buffer meta [%d] invalid buffer page\n",
				cpu_buffer->cpu);
			goto invalid;
		}
		entries += ret;
		entry_bytes += local_read(&head_page->page->commit);
		local_set(&cpu_buffer->head_page->entries, ret);

		if (head_page == cpu_buffer->commit_page)
			break;
	}

	if (head_page != cpu_buffer->commit_page) {
		pr_info("Ring buffer meta [%d] commit page not found\n",
			cpu_buffer->cpu);
		goto invalid;
	}
 done:
	local_set(&cpu_buffer->entries, entries);
	local_set(&cpu_buffer->entries_bytes, entry_bytes);

	pr_info("Ring buffer meta [%d] is from previous boot!\n", cpu_buffer->cpu);
	return;

 invalid:
	/* The content of the buffers are invalid, reset the meta data */
	meta->head_buffer = 0;
	meta->commit_buffer = 0;

	/* Reset the reader page */
	local_set(&cpu_buffer->reader_page->entries, 0);
	local_set(&cpu_buffer->reader_page->page->commit, 0);

	/* Reset all the subbuffers */
	for (i = 0; i < meta->nr_subbufs - 1; i++, rb_inc_page(&head_page)) {
		local_set(&head_page->entries, 0);
		local_set(&head_page->page->commit, 0);
	}
}

/* Used to calculate data delta */
static char rb_data_ptr[] = "";

#define THIS_TEXT_PTR		((unsigned long)rb_meta_init_text_addr)
#define THIS_DATA_PTR		((unsigned long)rb_data_ptr)

static void rb_meta_init_text_addr(struct ring_buffer_meta *meta)
{
	meta->text_addr = THIS_TEXT_PTR;
	meta->data_addr = THIS_DATA_PTR;
}

static void rb_range_meta_init(struct trace_buffer *buffer, int nr_pages)
{
	struct ring_buffer_meta *meta;
	unsigned long delta;
	void *subbuf;
	int cpu;
	int i;

	for (cpu = 0; cpu < nr_cpu_ids; cpu++) {
		void *next_meta;

		meta = rb_range_meta(buffer, nr_pages, cpu);

		if (rb_meta_valid(meta, cpu, buffer, nr_pages)) {
			/* Make the mappings match the current address */
			subbuf = rb_subbufs_from_meta(meta);
			delta = (unsigned long)subbuf - meta->first_buffer;
			meta->first_buffer += delta;
			meta->head_buffer += delta;
			meta->commit_buffer += delta;
			buffer->last_text_delta = THIS_TEXT_PTR - meta->text_addr;
			buffer->last_data_delta = THIS_DATA_PTR - meta->data_addr;
			continue;
		}

		if (cpu < nr_cpu_ids - 1)
			next_meta = rb_range_meta(buffer, nr_pages, cpu + 1);
		else
			next_meta = (void *)buffer->range_addr_end;

		memset(meta, 0, next_meta - (void *)meta);

		meta->magic = RING_BUFFER_META_MAGIC;
		meta->struct_size = sizeof(*meta);

		meta->nr_subbufs = nr_pages + 1;
		meta->subbuf_size = PAGE_SIZE;

		subbuf = rb_subbufs_from_meta(meta);

		meta->first_buffer = (unsigned long)subbuf;
		rb_meta_init_text_addr(meta);

		/*
		 * The buffers[] array holds the order of the sub-buffers
		 * that are after the meta data. The sub-buffers may
		 * be swapped out when read and inserted into a different
		 * location of the ring buffer. Although their addresses
		 * remain the same, the buffers[] array contains the
		 * index into the sub-buffers holding their actual order.
		 */
		for (i = 0; i < meta->nr_subbufs; i++) {
			meta->buffers[i] = i;
			rb_init_page(subbuf);
			subbuf += meta->subbuf_size;
		}
	}
}

static void *rbm_start(struct seq_file *m, loff_t *pos)
{
	struct ring_buffer_per_cpu *cpu_buffer = m->private;
	struct ring_buffer_meta *meta = cpu_buffer->ring_meta;
	unsigned long val;

	if (!meta)
		return NULL;

	if (*pos > meta->nr_subbufs)
		return NULL;

	val = *pos;
	val++;

	return (void *)val;
}

static void *rbm_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;

	return rbm_start(m, pos);
}

static int rbm_show(struct seq_file *m, void *v)
{
	struct ring_buffer_per_cpu *cpu_buffer = m->private;
	struct ring_buffer_meta *meta = cpu_buffer->ring_meta;
	unsigned long val = (unsigned long)v;

	if (val == 1) {
		seq_printf(m, "head_buffer:   %d\n",
			   rb_meta_subbuf_idx(meta, (void *)meta->head_buffer));
		seq_printf(m, "commit_buffer: %d\n",
			   rb_meta_subbuf_idx(meta, (void *)meta->commit_buffer));
		seq_printf(m, "subbuf_size:   %d\n", meta->subbuf_size);
		seq_printf(m, "nr_subbufs:    %d\n", meta->nr_subbufs);
		return 0;
	}

	val -= 2;
	seq_printf(m, "buffer[%ld]:    %d\n", val, meta->buffers[val]);

	return 0;
}

static void rbm_stop(struct seq_file *m, void *p)
{
}

static const struct seq_operations rb_meta_seq_ops = {
	.start		= rbm_start,
	.next		= rbm_next,
	.show		= rbm_show,
	.stop		= rbm_stop,
};

int ring_buffer_meta_seq_init(struct file *file, struct trace_buffer *buffer, int cpu)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &rb_meta_seq_ops);
	if (ret)
		return ret;

	m = file->private_data;
	m->private = buffer->buffers[cpu];

	return 0;
}

/* Map the buffer_pages to the previous head and commit pages */
static void rb_meta_buffer_update(struct ring_buffer_per_cpu *cpu_buffer,
				  struct buffer_page *bpage)
{
	struct ring_buffer_meta *meta = cpu_buffer->ring_meta;

	if (meta->head_buffer == (unsigned long)bpage->page)
		cpu_buffer->head_page = bpage;

	if (meta->commit_buffer == (unsigned long)bpage->page) {
		cpu_buffer->commit_page = bpage;
		cpu_buffer->tail_page = bpage;
	}
}

static int __rb_allocate_pages(struct ring_buffer_per_cpu *cpu_buffer,
		long nr_pages, struct list_head *pages)
{
	struct trace_buffer *buffer = cpu_buffer->buffer;
	struct ring_buffer_meta *meta = NULL;
	struct buffer_page *bpage, *tmp;
	bool user_thread = current->mm != NULL;
	gfp_t mflags;
	long i;

	/*
	 * Check if the available memory is there first.
	 * Note, si_mem_available() only gives us a rough estimate of available
	 * memory. It may not be accurate. But we don't care, we just want
	 * to prevent doing any allocation when it is obvious that it is
	 * not going to succeed.
	 */
	i = si_mem_available();
	if (i < nr_pages)
		return -ENOMEM;

	/*
	 * __GFP_RETRY_MAYFAIL flag makes sure that the allocation fails
	 * gracefully without invoking oom-killer and the system is not
	 * destabilized.
	 */
	mflags = GFP_KERNEL | __GFP_RETRY_MAYFAIL;

	/*
	 * If a user thread allocates too much, and si_mem_available()
	 * reports there's enough memory, even though there is not.
	 * Make sure the OOM killer kills this thread. This can happen
	 * even with RETRY_MAYFAIL because another task may be doing
	 * an allocation after this task has taken all memory.
	 * This is the task the OOM killer needs to take out during this
	 * loop, even if it was triggered by an allocation somewhere else.
	 */
	if (user_thread)
		set_current_oom_origin();

	if (buffer->range_addr_start)
		meta = rb_range_meta(buffer, nr_pages, cpu_buffer->cpu);

	for (i = 0; i < nr_pages; i++) {
		struct page *page;

		bpage = kzalloc_node(ALIGN(sizeof(*bpage), cache_line_size()),
				    mflags, cpu_to_node(cpu_buffer->cpu));
		if (!bpage)
			goto free_pages;

		rb_check_bpage(cpu_buffer, bpage);

		/*
		 * Append the pages as for mapped buffers we want to keep
		 * the order
		 */
		list_add_tail(&bpage->list, pages);

		if (meta) {
			/* A range was given. Use that for the buffer page */
			bpage->page = rb_range_buffer(cpu_buffer, i + 1);
			if (!bpage->page)
				goto free_pages;
			/* If this is valid from a previous boot */
			if (meta->head_buffer)
				rb_meta_buffer_update(cpu_buffer, bpage);
			bpage->range = 1;
			bpage->id = i + 1;
		} else {
			page = alloc_pages_node(cpu_to_node(cpu_buffer->cpu),
						mflags | __GFP_COMP | __GFP_ZERO,
						cpu_buffer->buffer->subbuf_order);
			if (!page)
				goto free_pages;
			bpage->page = page_address(page);
			rb_init_page(bpage->page);
		}
		bpage->order = cpu_buffer->buffer->subbuf_order;

		if (user_thread && fatal_signal_pending(current))
			goto free_pages;
	}
	if (user_thread)
		clear_current_oom_origin();

	return 0;

free_pages:
	list_for_each_entry_safe(bpage, tmp, pages, list) {
		list_del_init(&bpage->list);
		free_buffer_page(bpage);
	}
	if (user_thread)
		clear_current_oom_origin();

	return -ENOMEM;
}

static int rb_allocate_pages(struct ring_buffer_per_cpu *cpu_buffer,
			     unsigned long nr_pages)
{
	LIST_HEAD(pages);

	WARN_ON(!nr_pages);

	if (__rb_allocate_pages(cpu_buffer, nr_pages, &pages))
		return -ENOMEM;

	/*
	 * The ring buffer page list is a circular list that does not
	 * start and end with a list head. All page list items point to
	 * other pages.
	 */
	cpu_buffer->pages = pages.next;
	list_del(&pages);

	cpu_buffer->nr_pages = nr_pages;

	rb_check_pages(cpu_buffer);

	return 0;
}

static struct ring_buffer_per_cpu *
rb_allocate_cpu_buffer(struct trace_buffer *buffer, long nr_pages, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct ring_buffer_meta *meta;
	struct buffer_page *bpage;
	struct page *page;
	int ret;

	cpu_buffer = kzalloc_node(ALIGN(sizeof(*cpu_buffer), cache_line_size()),
				  GFP_KERNEL, cpu_to_node(cpu));
	if (!cpu_buffer)
		return NULL;

	cpu_buffer->cpu = cpu;
	cpu_buffer->buffer = buffer;
	raw_spin_lock_init(&cpu_buffer->reader_lock);
	lockdep_set_class(&cpu_buffer->reader_lock, buffer->reader_lock_key);
	cpu_buffer->lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;
	INIT_WORK(&cpu_buffer->update_pages_work, update_pages_handler);
	init_completion(&cpu_buffer->update_done);
	init_irq_work(&cpu_buffer->irq_work.work, rb_wake_up_waiters);
	init_waitqueue_head(&cpu_buffer->irq_work.waiters);
	init_waitqueue_head(&cpu_buffer->irq_work.full_waiters);
	mutex_init(&cpu_buffer->mapping_lock);

	bpage = kzalloc_node(ALIGN(sizeof(*bpage), cache_line_size()),
			    GFP_KERNEL, cpu_to_node(cpu));
	if (!bpage)
		goto fail_free_buffer;

	rb_check_bpage(cpu_buffer, bpage);

	cpu_buffer->reader_page = bpage;

	if (buffer->range_addr_start) {
		/*
		 * Range mapped buffers have the same restrictions as memory
		 * mapped ones do.
		 */
		cpu_buffer->mapped = 1;
		cpu_buffer->ring_meta = rb_range_meta(buffer, nr_pages, cpu);
		bpage->page = rb_range_buffer(cpu_buffer, 0);
		if (!bpage->page)
			goto fail_free_reader;
		if (cpu_buffer->ring_meta->head_buffer)
			rb_meta_buffer_update(cpu_buffer, bpage);
		bpage->range = 1;
	} else {
		page = alloc_pages_node(cpu_to_node(cpu),
					GFP_KERNEL | __GFP_COMP | __GFP_ZERO,
					cpu_buffer->buffer->subbuf_order);
		if (!page)
			goto fail_free_reader;
		bpage->page = page_address(page);
		rb_init_page(bpage->page);
	}

	INIT_LIST_HEAD(&cpu_buffer->reader_page->list);
	INIT_LIST_HEAD(&cpu_buffer->new_pages);

	ret = rb_allocate_pages(cpu_buffer, nr_pages);
	if (ret < 0)
		goto fail_free_reader;

	rb_meta_validate_events(cpu_buffer);

	/* If the boot meta was valid then this has already been updated */
	meta = cpu_buffer->ring_meta;
	if (!meta || !meta->head_buffer ||
	    !cpu_buffer->head_page || !cpu_buffer->commit_page || !cpu_buffer->tail_page) {
		if (meta && meta->head_buffer &&
		    (cpu_buffer->head_page || cpu_buffer->commit_page || cpu_buffer->tail_page)) {
			pr_warn("Ring buffer meta buffers not all mapped\n");
			if (!cpu_buffer->head_page)
				pr_warn("   Missing head_page\n");
			if (!cpu_buffer->commit_page)
				pr_warn("   Missing commit_page\n");
			if (!cpu_buffer->tail_page)
				pr_warn("   Missing tail_page\n");
		}

		cpu_buffer->head_page
			= list_entry(cpu_buffer->pages, struct buffer_page, list);
		cpu_buffer->tail_page = cpu_buffer->commit_page = cpu_buffer->head_page;

		rb_head_page_activate(cpu_buffer);

		if (cpu_buffer->ring_meta)
			meta->commit_buffer = meta->head_buffer;
	} else {
		/* The valid meta buffer still needs to activate the head page */
		rb_head_page_activate(cpu_buffer);
	}

	return cpu_buffer;

 fail_free_reader:
	free_buffer_page(cpu_buffer->reader_page);

 fail_free_buffer:
	kfree(cpu_buffer);
	return NULL;
}

static void rb_free_cpu_buffer(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct list_head *head = cpu_buffer->pages;
	struct buffer_page *bpage, *tmp;

	irq_work_sync(&cpu_buffer->irq_work.work);

	free_buffer_page(cpu_buffer->reader_page);

	if (head) {
		rb_head_page_deactivate(cpu_buffer);

		list_for_each_entry_safe(bpage, tmp, head, list) {
			list_del_init(&bpage->list);
			free_buffer_page(bpage);
		}
		bpage = list_entry(head, struct buffer_page, list);
		free_buffer_page(bpage);
	}

	free_page((unsigned long)cpu_buffer->free_page);

	kfree(cpu_buffer);
}

static struct trace_buffer *alloc_buffer(unsigned long size, unsigned flags,
					 int order, unsigned long start,
					 unsigned long end,
					 struct lock_class_key *key)
{
	struct trace_buffer *buffer;
	long nr_pages;
	int subbuf_size;
	int bsize;
	int cpu;
	int ret;

	/* keep it in its own cache line */
	buffer = kzalloc(ALIGN(sizeof(*buffer), cache_line_size()),
			 GFP_KERNEL);
	if (!buffer)
		return NULL;

	if (!zalloc_cpumask_var(&buffer->cpumask, GFP_KERNEL))
		goto fail_free_buffer;

	buffer->subbuf_order = order;
	subbuf_size = (PAGE_SIZE << order);
	buffer->subbuf_size = subbuf_size - BUF_PAGE_HDR_SIZE;

	/* Max payload is buffer page size - header (8bytes) */
	buffer->max_data_size = buffer->subbuf_size - (sizeof(u32) * 2);

	buffer->flags = flags;
	buffer->clock = trace_clock_local;
	buffer->reader_lock_key = key;

	init_irq_work(&buffer->irq_work.work, rb_wake_up_waiters);
	init_waitqueue_head(&buffer->irq_work.waiters);

	buffer->cpus = nr_cpu_ids;

	bsize = sizeof(void *) * nr_cpu_ids;
	buffer->buffers = kzalloc(ALIGN(bsize, cache_line_size()),
				  GFP_KERNEL);
	if (!buffer->buffers)
		goto fail_free_cpumask;

	/* If start/end are specified, then that overrides size */
	if (start && end) {
		unsigned long ptr;
		int n;

		size = end - start;
		size = size / nr_cpu_ids;

		/*
		 * The number of sub-buffers (nr_pages) is determined by the
		 * total size allocated minus the meta data size.
		 * Then that is divided by the number of per CPU buffers
		 * needed, plus account for the integer array index that
		 * will be appended to the meta data.
		 */
		nr_pages = (size - sizeof(struct ring_buffer_meta)) /
			(subbuf_size + sizeof(int));
		/* Need at least two pages plus the reader page */
		if (nr_pages < 3)
			goto fail_free_buffers;

 again:
		/* Make sure that the size fits aligned */
		for (n = 0, ptr = start; n < nr_cpu_ids; n++) {
			ptr += sizeof(struct ring_buffer_meta) +
				sizeof(int) * nr_pages;
			ptr = ALIGN(ptr, subbuf_size);
			ptr += subbuf_size * nr_pages;
		}
		if (ptr > end) {
			if (nr_pages <= 3)
				goto fail_free_buffers;
			nr_pages--;
			goto again;
		}

		/* nr_pages should not count the reader page */
		nr_pages--;
		buffer->range_addr_start = start;
		buffer->range_addr_end = end;

		rb_range_meta_init(buffer, nr_pages);
	} else {

		/* need at least two pages */
		nr_pages = DIV_ROUND_UP(size, buffer->subbuf_size);
		if (nr_pages < 2)
			nr_pages = 2;
	}

	cpu = raw_smp_processor_id();
	cpumask_set_cpu(cpu, buffer->cpumask);
	buffer->buffers[cpu] = rb_allocate_cpu_buffer(buffer, nr_pages, cpu);
	if (!buffer->buffers[cpu])
		goto fail_free_buffers;

	ret = cpuhp_state_add_instance(CPUHP_TRACE_RB_PREPARE, &buffer->node);
	if (ret < 0)
		goto fail_free_buffers;

	mutex_init(&buffer->mutex);

	return buffer;

 fail_free_buffers:
	for_each_buffer_cpu(buffer, cpu) {
		if (buffer->buffers[cpu])
			rb_free_cpu_buffer(buffer->buffers[cpu]);
	}
	kfree(buffer->buffers);

 fail_free_cpumask:
	free_cpumask_var(buffer->cpumask);

 fail_free_buffer:
	kfree(buffer);
	return NULL;
}

/**
 * __ring_buffer_alloc - allocate a new ring_buffer
 * @size: the size in bytes per cpu that is needed.
 * @flags: attributes to set for the ring buffer.
 * @key: ring buffer reader_lock_key.
 *
 * Currently the only flag that is available is the RB_FL_OVERWRITE
 * flag. This flag means that the buffer will overwrite old data
 * when the buffer wraps. If this flag is not set, the buffer will
 * drop data when the tail hits the head.
 */
struct trace_buffer *__ring_buffer_alloc(unsigned long size, unsigned flags,
					struct lock_class_key *key)
{
	/* Default buffer page size - one system page */
	return alloc_buffer(size, flags, 0, 0, 0,key);

}
EXPORT_SYMBOL_GPL(__ring_buffer_alloc);

/**
 * __ring_buffer_alloc_range - allocate a new ring_buffer from existing memory
 * @size: the size in bytes per cpu that is needed.
 * @flags: attributes to set for the ring buffer.
 * @order: sub-buffer order
 * @start: start of allocated range
 * @range_size: size of allocated range
 * @key: ring buffer reader_lock_key.
 *
 * Currently the only flag that is available is the RB_FL_OVERWRITE
 * flag. This flag means that the buffer will overwrite old data
 * when the buffer wraps. If this flag is not set, the buffer will
 * drop data when the tail hits the head.
 */
struct trace_buffer *__ring_buffer_alloc_range(unsigned long size, unsigned flags,
					       int order, unsigned long start,
					       unsigned long range_size,
					       struct lock_class_key *key)
{
	return alloc_buffer(size, flags, order, start, start + range_size, key);
}

/**
 * ring_buffer_last_boot_delta - return the delta offset from last boot
 * @buffer: The buffer to return the delta from
 * @text: Return text delta
 * @data: Return data delta
 *
 * Returns: The true if the delta is non zero
 */
bool ring_buffer_last_boot_delta(struct trace_buffer *buffer, long *text,
				 long *data)
{
	if (!buffer)
		return false;

	if (!buffer->last_text_delta)
		return false;

	*text = buffer->last_text_delta;
	*data = buffer->last_data_delta;

	return true;
}

/**
 * ring_buffer_free - free a ring buffer.
 * @buffer: the buffer to free.
 */
void
ring_buffer_free(struct trace_buffer *buffer)
{
	int cpu;

	cpuhp_state_remove_instance(CPUHP_TRACE_RB_PREPARE, &buffer->node);

	irq_work_sync(&buffer->irq_work.work);

	for_each_buffer_cpu(buffer, cpu)
		rb_free_cpu_buffer(buffer->buffers[cpu]);

	kfree(buffer->buffers);
	free_cpumask_var(buffer->cpumask);

	kfree(buffer);
}
EXPORT_SYMBOL_GPL(ring_buffer_free);

void ring_buffer_set_clock(struct trace_buffer *buffer,
			   u64 (*clock)(void))
{
	buffer->clock = clock;
}

void ring_buffer_set_time_stamp_abs(struct trace_buffer *buffer, bool abs)
{
	buffer->time_stamp_abs = abs;
}

bool ring_buffer_time_stamp_abs(struct trace_buffer *buffer)
{
	return buffer->time_stamp_abs;
}

static inline unsigned long rb_page_entries(struct buffer_page *bpage)
{
	return local_read(&bpage->entries) & RB_WRITE_MASK;
}

static inline unsigned long rb_page_write(struct buffer_page *bpage)
{
	return local_read(&bpage->write) & RB_WRITE_MASK;
}

static bool
rb_remove_pages(struct ring_buffer_per_cpu *cpu_buffer, unsigned long nr_pages)
{
	struct list_head *tail_page, *to_remove, *next_page;
	struct buffer_page *to_remove_page, *tmp_iter_page;
	struct buffer_page *last_page, *first_page;
	unsigned long nr_removed;
	unsigned long head_bit;
	int page_entries;

	head_bit = 0;

	raw_spin_lock_irq(&cpu_buffer->reader_lock);
	atomic_inc(&cpu_buffer->record_disabled);
	/*
	 * We don't race with the readers since we have acquired the reader
	 * lock. We also don't race with writers after disabling recording.
	 * This makes it easy to figure out the first and the last page to be
	 * removed from the list. We unlink all the pages in between including
	 * the first and last pages. This is done in a busy loop so that we
	 * lose the least number of traces.
	 * The pages are freed after we restart recording and unlock readers.
	 */
	tail_page = &cpu_buffer->tail_page->list;

	/*
	 * tail page might be on reader page, we remove the next page
	 * from the ring buffer
	 */
	if (cpu_buffer->tail_page == cpu_buffer->reader_page)
		tail_page = rb_list_head(tail_page->next);
	to_remove = tail_page;

	/* start of pages to remove */
	first_page = list_entry(rb_list_head(to_remove->next),
				struct buffer_page, list);

	for (nr_removed = 0; nr_removed < nr_pages; nr_removed++) {
		to_remove = rb_list_head(to_remove)->next;
		head_bit |= (unsigned long)to_remove & RB_PAGE_HEAD;
	}
	/* Read iterators need to reset themselves when some pages removed */
	cpu_buffer->pages_removed += nr_removed;

	next_page = rb_list_head(to_remove)->next;

	/*
	 * Now we remove all pages between tail_page and next_page.
	 * Make sure that we have head_bit value preserved for the
	 * next page
	 */
	tail_page->next = (struct list_head *)((unsigned long)next_page |
						head_bit);
	next_page = rb_list_head(next_page);
	next_page->prev = tail_page;

	/* make sure pages points to a valid page in the ring buffer */
	cpu_buffer->pages = next_page;
	cpu_buffer->cnt++;

	/* update head page */
	if (head_bit)
		cpu_buffer->head_page = list_entry(next_page,
						struct buffer_page, list);

	/* pages are removed, resume tracing and then free the pages */
	atomic_dec(&cpu_buffer->record_disabled);
	raw_spin_unlock_irq(&cpu_buffer->reader_lock);

	RB_WARN_ON(cpu_buffer, list_empty(cpu_buffer->pages));

	/* last buffer page to remove */
	last_page = list_entry(rb_list_head(to_remove), struct buffer_page,
				list);
	tmp_iter_page = first_page;

	do {
		cond_resched();

		to_remove_page = tmp_iter_page;
		rb_inc_page(&tmp_iter_page);

		/* update the counters */
		page_entries = rb_page_entries(to_remove_page);
		if (page_entries) {
			/*
			 * If something was added to this page, it was full
			 * since it is not the tail page. So we deduct the
			 * bytes consumed in ring buffer from here.
			 * Increment overrun to account for the lost events.
			 */
			local_add(page_entries, &cpu_buffer->overrun);
			local_sub(rb_page_commit(to_remove_page), &cpu_buffer->entries_bytes);
			local_inc(&cpu_buffer->pages_lost);
		}

		/*
		 * We have already removed references to this list item, just
		 * free up the buffer_page and its page
		 */
		free_buffer_page(to_remove_page);
		nr_removed--;

	} while (to_remove_page != last_page);

	RB_WARN_ON(cpu_buffer, nr_removed);

	return nr_removed == 0;
}

static bool
rb_insert_pages(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct list_head *pages = &cpu_buffer->new_pages;
	unsigned long flags;
	bool success;
	int retries;

	/* Can be called at early boot up, where interrupts must not been enabled */
	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);
	/*
	 * We are holding the reader lock, so the reader page won't be swapped
	 * in the ring buffer. Now we are racing with the writer trying to
	 * move head page and the tail page.
	 * We are going to adapt the reader page update process where:
	 * 1. We first splice the start and end of list of new pages between
	 *    the head page and its previous page.
	 * 2. We cmpxchg the prev_page->next to point from head page to the
	 *    start of new pages list.
	 * 3. Finally, we update the head->prev to the end of new list.
	 *
	 * We will try this process 10 times, to make sure that we don't keep
	 * spinning.
	 */
	retries = 10;
	success = false;
	while (retries--) {
		struct list_head *head_page, *prev_page;
		struct list_head *last_page, *first_page;
		struct list_head *head_page_with_bit;
		struct buffer_page *hpage = rb_set_head_page(cpu_buffer);

		if (!hpage)
			break;
		head_page = &hpage->list;
		prev_page = head_page->prev;

		first_page = pages->next;
		last_page  = pages->prev;

		head_page_with_bit = (struct list_head *)
				     ((unsigned long)head_page | RB_PAGE_HEAD);

		last_page->next = head_page_with_bit;
		first_page->prev = prev_page;

		/* caution: head_page_with_bit gets updated on cmpxchg failure */
		if (try_cmpxchg(&prev_page->next,
				&head_page_with_bit, first_page)) {
			/*
			 * yay, we replaced the page pointer to our new list,
			 * now, we just have to update to head page's prev
			 * pointer to point to end of list
			 */
			head_page->prev = last_page;
			cpu_buffer->cnt++;
			success = true;
			break;
		}
	}

	if (success)
		INIT_LIST_HEAD(pages);
	/*
	 * If we weren't successful in adding in new pages, warn and stop
	 * tracing
	 */
	RB_WARN_ON(cpu_buffer, !success);
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);

	/* free pages if they weren't inserted */
	if (!success) {
		struct buffer_page *bpage, *tmp;
		list_for_each_entry_safe(bpage, tmp, &cpu_buffer->new_pages,
					 list) {
			list_del_init(&bpage->list);
			free_buffer_page(bpage);
		}
	}
	return success;
}

static void rb_update_pages(struct ring_buffer_per_cpu *cpu_buffer)
{
	bool success;

	if (cpu_buffer->nr_pages_to_update > 0)
		success = rb_insert_pages(cpu_buffer);
	else
		success = rb_remove_pages(cpu_buffer,
					-cpu_buffer->nr_pages_to_update);

	if (success)
		cpu_buffer->nr_pages += cpu_buffer->nr_pages_to_update;
}

static void update_pages_handler(struct work_struct *work)
{
	struct ring_buffer_per_cpu *cpu_buffer = container_of(work,
			struct ring_buffer_per_cpu, update_pages_work);
	rb_update_pages(cpu_buffer);
	complete(&cpu_buffer->update_done);
}

/**
 * ring_buffer_resize - resize the ring buffer
 * @buffer: the buffer to resize.
 * @size: the new size.
 * @cpu_id: the cpu buffer to resize
 *
 * Minimum size is 2 * buffer->subbuf_size.
 *
 * Returns 0 on success and < 0 on failure.
 */
int ring_buffer_resize(struct trace_buffer *buffer, unsigned long size,
			int cpu_id)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long nr_pages;
	int cpu, err;

	/*
	 * Always succeed at resizing a non-existent buffer:
	 */
	if (!buffer)
		return 0;

	/* Make sure the requested buffer exists */
	if (cpu_id != RING_BUFFER_ALL_CPUS &&
	    !cpumask_test_cpu(cpu_id, buffer->cpumask))
		return 0;

	nr_pages = DIV_ROUND_UP(size, buffer->subbuf_size);

	/* we need a minimum of two pages */
	if (nr_pages < 2)
		nr_pages = 2;

	/* prevent another thread from changing buffer sizes */
	mutex_lock(&buffer->mutex);
	atomic_inc(&buffer->resizing);

	if (cpu_id == RING_BUFFER_ALL_CPUS) {
		/*
		 * Don't succeed if resizing is disabled, as a reader might be
		 * manipulating the ring buffer and is expecting a sane state while
		 * this is true.
		 */
		for_each_buffer_cpu(buffer, cpu) {
			cpu_buffer = buffer->buffers[cpu];
			if (atomic_read(&cpu_buffer->resize_disabled)) {
				err = -EBUSY;
				goto out_err_unlock;
			}
		}

		/* calculate the pages to update */
		for_each_buffer_cpu(buffer, cpu) {
			cpu_buffer = buffer->buffers[cpu];

			cpu_buffer->nr_pages_to_update = nr_pages -
							cpu_buffer->nr_pages;
			/*
			 * nothing more to do for removing pages or no update
			 */
			if (cpu_buffer->nr_pages_to_update <= 0)
				continue;
			/*
			 * to add pages, make sure all new pages can be
			 * allocated without receiving ENOMEM
			 */
			INIT_LIST_HEAD(&cpu_buffer->new_pages);
			if (__rb_allocate_pages(cpu_buffer, cpu_buffer->nr_pages_to_update,
						&cpu_buffer->new_pages)) {
				/* not enough memory for new pages */
				err = -ENOMEM;
				goto out_err;
			}

			cond_resched();
		}

		cpus_read_lock();
		/*
		 * Fire off all the required work handlers
		 * We can't schedule on offline CPUs, but it's not necessary
		 * since we can change their buffer sizes without any race.
		 */
		for_each_buffer_cpu(buffer, cpu) {
			cpu_buffer = buffer->buffers[cpu];
			if (!cpu_buffer->nr_pages_to_update)
				continue;

			/* Can't run something on an offline CPU. */
			if (!cpu_online(cpu)) {
				rb_update_pages(cpu_buffer);
				cpu_buffer->nr_pages_to_update = 0;
			} else {
				/* Run directly if possible. */
				migrate_disable();
				if (cpu != smp_processor_id()) {
					migrate_enable();
					schedule_work_on(cpu,
							 &cpu_buffer->update_pages_work);
				} else {
					update_pages_handler(&cpu_buffer->update_pages_work);
					migrate_enable();
				}
			}
		}

		/* wait for all the updates to complete */
		for_each_buffer_cpu(buffer, cpu) {
			cpu_buffer = buffer->buffers[cpu];
			if (!cpu_buffer->nr_pages_to_update)
				continue;

			if (cpu_online(cpu))
				wait_for_completion(&cpu_buffer->update_done);
			cpu_buffer->nr_pages_to_update = 0;
		}

		cpus_read_unlock();
	} else {
		cpu_buffer = buffer->buffers[cpu_id];

		if (nr_pages == cpu_buffer->nr_pages)
			goto out;

		/*
		 * Don't succeed if resizing is disabled, as a reader might be
		 * manipulating the ring buffer and is expecting a sane state while
		 * this is true.
		 */
		if (atomic_read(&cpu_buffer->resize_disabled)) {
			err = -EBUSY;
			goto out_err_unlock;
		}

		cpu_buffer->nr_pages_to_update = nr_pages -
						cpu_buffer->nr_pages;

		INIT_LIST_HEAD(&cpu_buffer->new_pages);
		if (cpu_buffer->nr_pages_to_update > 0 &&
			__rb_allocate_pages(cpu_buffer, cpu_buffer->nr_pages_to_update,
					    &cpu_buffer->new_pages)) {
			err = -ENOMEM;
			goto out_err;
		}

		cpus_read_lock();

		/* Can't run something on an offline CPU. */
		if (!cpu_online(cpu_id))
			rb_update_pages(cpu_buffer);
		else {
			/* Run directly if possible. */
			migrate_disable();
			if (cpu_id == smp_processor_id()) {
				rb_update_pages(cpu_buffer);
				migrate_enable();
			} else {
				migrate_enable();
				schedule_work_on(cpu_id,
						 &cpu_buffer->update_pages_work);
				wait_for_completion(&cpu_buffer->update_done);
			}
		}

		cpu_buffer->nr_pages_to_update = 0;
		cpus_read_unlock();
	}

 out:
	/*
	 * The ring buffer resize can happen with the ring buffer
	 * enabled, so that the update disturbs the tracing as little
	 * as possible. But if the buffer is disabled, we do not need
	 * to worry about that, and we can take the time to verify
	 * that the buffer is not corrupt.
	 */
	if (atomic_read(&buffer->record_disabled)) {
		atomic_inc(&buffer->record_disabled);
		/*
		 * Even though the buffer was disabled, we must make sure
		 * that it is truly disabled before calling rb_check_pages.
		 * There could have been a race between checking
		 * record_disable and incrementing it.
		 */
		synchronize_rcu();
		for_each_buffer_cpu(buffer, cpu) {
			cpu_buffer = buffer->buffers[cpu];
			rb_check_pages(cpu_buffer);
		}
		atomic_dec(&buffer->record_disabled);
	}

	atomic_dec(&buffer->resizing);
	mutex_unlock(&buffer->mutex);
	return 0;

 out_err:
	for_each_buffer_cpu(buffer, cpu) {
		struct buffer_page *bpage, *tmp;

		cpu_buffer = buffer->buffers[cpu];
		cpu_buffer->nr_pages_to_update = 0;

		if (list_empty(&cpu_buffer->new_pages))
			continue;

		list_for_each_entry_safe(bpage, tmp, &cpu_buffer->new_pages,
					list) {
			list_del_init(&bpage->list);
			free_buffer_page(bpage);
		}
	}
 out_err_unlock:
	atomic_dec(&buffer->resizing);
	mutex_unlock(&buffer->mutex);
	return err;
}
EXPORT_SYMBOL_GPL(ring_buffer_resize);

void ring_buffer_change_overwrite(struct trace_buffer *buffer, int val)
{
	mutex_lock(&buffer->mutex);
	if (val)
		buffer->flags |= RB_FL_OVERWRITE;
	else
		buffer->flags &= ~RB_FL_OVERWRITE;
	mutex_unlock(&buffer->mutex);
}
EXPORT_SYMBOL_GPL(ring_buffer_change_overwrite);

static __always_inline void *__rb_page_index(struct buffer_page *bpage, unsigned index)
{
	return bpage->page->data + index;
}

static __always_inline struct ring_buffer_event *
rb_reader_event(struct ring_buffer_per_cpu *cpu_buffer)
{
	return __rb_page_index(cpu_buffer->reader_page,
			       cpu_buffer->reader_page->read);
}

static struct ring_buffer_event *
rb_iter_head_event(struct ring_buffer_iter *iter)
{
	struct ring_buffer_event *event;
	struct buffer_page *iter_head_page = iter->head_page;
	unsigned long commit;
	unsigned length;

	if (iter->head != iter->next_event)
		return iter->event;

	/*
	 * When the writer goes across pages, it issues a cmpxchg which
	 * is a mb(), which will synchronize with the rmb here.
	 * (see rb_tail_page_update() and __rb_reserve_next())
	 */
	commit = rb_page_commit(iter_head_page);
	smp_rmb();

	/* An event needs to be at least 8 bytes in size */
	if (iter->head > commit - 8)
		goto reset;

	event = __rb_page_index(iter_head_page, iter->head);
	length = rb_event_length(event);

	/*
	 * READ_ONCE() doesn't work on functions and we don't want the
	 * compiler doing any crazy optimizations with length.
	 */
	barrier();

	if ((iter->head + length) > commit || length > iter->event_size)
		/* Writer corrupted the read? */
		goto reset;

	memcpy(iter->event, event, length);
	/*
	 * If the page stamp is still the same after this rmb() then the
	 * event was safely copied without the writer entering the page.
	 */
	smp_rmb();

	/* Make sure the page didn't change since we read this */
	if (iter->page_stamp != iter_head_page->page->time_stamp ||
	    commit > rb_page_commit(iter_head_page))
		goto reset;

	iter->next_event = iter->head + length;
	return iter->event;
 reset:
	/* Reset to the beginning */
	iter->page_stamp = iter->read_stamp = iter->head_page->page->time_stamp;
	iter->head = 0;
	iter->next_event = 0;
	iter->missed_events = 1;
	return NULL;
}

/* Size is determined by what has been committed */
static __always_inline unsigned rb_page_size(struct buffer_page *bpage)
{
	return rb_page_commit(bpage) & ~RB_MISSED_MASK;
}

static __always_inline unsigned
rb_commit_index(struct ring_buffer_per_cpu *cpu_buffer)
{
	return rb_page_commit(cpu_buffer->commit_page);
}

static __always_inline unsigned
rb_event_index(struct ring_buffer_per_cpu *cpu_buffer, struct ring_buffer_event *event)
{
	unsigned long addr = (unsigned long)event;

	addr &= (PAGE_SIZE << cpu_buffer->buffer->subbuf_order) - 1;

	return addr - BUF_PAGE_HDR_SIZE;
}

static void rb_inc_iter(struct ring_buffer_iter *iter)
{
	struct ring_buffer_per_cpu *cpu_buffer = iter->cpu_buffer;

	/*
	 * The iterator could be on the reader page (it starts there).
	 * But the head could have moved, since the reader was
	 * found. Check for this case and assign the iterator
	 * to the head page instead of next.
	 */
	if (iter->head_page == cpu_buffer->reader_page)
		iter->head_page = rb_set_head_page(cpu_buffer);
	else
		rb_inc_page(&iter->head_page);

	iter->page_stamp = iter->read_stamp = iter->head_page->page->time_stamp;
	iter->head = 0;
	iter->next_event = 0;
}

/* Return the index into the sub-buffers for a given sub-buffer */
static int rb_meta_subbuf_idx(struct ring_buffer_meta *meta, void *subbuf)
{
	void *subbuf_array;

	subbuf_array = (void *)meta + sizeof(int) * meta->nr_subbufs;
	subbuf_array = (void *)ALIGN((unsigned long)subbuf_array, meta->subbuf_size);
	return (subbuf - subbuf_array) / meta->subbuf_size;
}

static void rb_update_meta_head(struct ring_buffer_per_cpu *cpu_buffer,
				struct buffer_page *next_page)
{
	struct ring_buffer_meta *meta = cpu_buffer->ring_meta;
	unsigned long old_head = (unsigned long)next_page->page;
	unsigned long new_head;

	rb_inc_page(&next_page);
	new_head = (unsigned long)next_page->page;

	/*
	 * Only move it forward once, if something else came in and
	 * moved it forward, then we don't want to touch it.
	 */
	(void)cmpxchg(&meta->head_buffer, old_head, new_head);
}

static void rb_update_meta_reader(struct ring_buffer_per_cpu *cpu_buffer,
				  struct buffer_page *reader)
{
	struct ring_buffer_meta *meta = cpu_buffer->ring_meta;
	void *old_reader = cpu_buffer->reader_page->page;
	void *new_reader = reader->page;
	int id;

	id = reader->id;
	cpu_buffer->reader_page->id = id;
	reader->id = 0;

	meta->buffers[0] = rb_meta_subbuf_idx(meta, new_reader);
	meta->buffers[id] = rb_meta_subbuf_idx(meta, old_reader);

	/* The head pointer is the one after the reader */
	rb_update_meta_head(cpu_buffer, reader);
}

/*
 * rb_handle_head_page - writer hit the head page
 *
 * Returns: +1 to retry page
 *           0 to continue
 *          -1 on error
 */
static int
rb_handle_head_page(struct ring_buffer_per_cpu *cpu_buffer,
		    struct buffer_page *tail_page,
		    struct buffer_page *next_page)
{
	struct buffer_page *new_head;
	int entries;
	int type;
	int ret;

	entries = rb_page_entries(next_page);

	/*
	 * The hard part is here. We need to move the head
	 * forward, and protect against both readers on
	 * other CPUs and writers coming in via interrupts.
	 */
	type = rb_head_page_set_update(cpu_buffer, next_page, tail_page,
				       RB_PAGE_HEAD);

	/*
	 * type can be one of four:
	 *  NORMAL - an interrupt already moved it for us
	 *  HEAD   - we are the first to get here.
	 *  UPDATE - we are the interrupt interrupting
	 *           a current move.
	 *  MOVED  - a reader on another CPU moved the next
	 *           pointer to its reader page. Give up
	 *           and try again.
	 */

	switch (type) {
	case RB_PAGE_HEAD:
		/*
		 * We changed the head to UPDATE, thus
		 * it is our responsibility to update
		 * the counters.
		 */
		local_add(entries, &cpu_buffer->overrun);
		local_sub(rb_page_commit(next_page), &cpu_buffer->entries_bytes);
		local_inc(&cpu_buffer->pages_lost);

		if (cpu_buffer->ring_meta)
			rb_update_meta_head(cpu_buffer, next_page);
		/*
		 * The entries will be zeroed out when we move the
		 * tail page.
		 */

		/* still more to do */
		break;

	case RB_PAGE_UPDATE:
		/*
		 * This is an interrupt that interrupt the
		 * previous update. Still more to do.
		 */
		break;
	case RB_PAGE_NORMAL:
		/*
		 * An interrupt came in before the update
		 * and processed this for us.
		 * Nothing left to do.
		 */
		return 1;
	case RB_PAGE_MOVED:
		/*
		 * The reader is on another CPU and just did
		 * a swap with our next_page.
		 * Try again.
		 */
		return 1;
	default:
		RB_WARN_ON(cpu_buffer, 1); /* WTF??? */
		return -1;
	}

	/*
	 * Now that we are here, the old head pointer is
	 * set to UPDATE. This will keep the reader from
	 * swapping the head page with the reader page.
	 * The reader (on another CPU) will spin till
	 * we are finished.
	 *
	 * We just need to protect against interrupts
	 * doing the job. We will set the next pointer
	 * to HEAD. After that, we set the old pointer
	 * to NORMAL, but only if it was HEAD before.
	 * otherwise we are an interrupt, and only
	 * want the outer most commit to reset it.
	 */
	new_head = next_page;
	rb_inc_page(&new_head);

	ret = rb_head_page_set_head(cpu_buffer, new_head, next_page,
				    RB_PAGE_NORMAL);

	/*
	 * Valid returns are:
	 *  HEAD   - an interrupt came in and already set it.
	 *  NORMAL - One of two things:
	 *            1) We really set it.
	 *            2) A bunch of interrupts came in and moved
	 *               the page forward again.
	 */
	switch (ret) {
	case RB_PAGE_HEAD:
	case RB_PAGE_NORMAL:
		/* OK */
		break;
	default:
		RB_WARN_ON(cpu_buffer, 1);
		return -1;
	}

	/*
	 * It is possible that an interrupt came in,
	 * set the head up, then more interrupts came in
	 * and moved it again. When we get back here,
	 * the page would have been set to NORMAL but we
	 * just set it back to HEAD.
	 *
	 * How do you detect this? Well, if that happened
	 * the tail page would have moved.
	 */
	if (ret == RB_PAGE_NORMAL) {
		struct buffer_page *buffer_tail_page;

		buffer_tail_page = READ_ONCE(cpu_buffer->tail_page);
		/*
		 * If the tail had moved passed next, then we need
		 * to reset the pointer.
		 */
		if (buffer_tail_page != tail_page &&
		    buffer_tail_page != next_page)
			rb_head_page_set_normal(cpu_buffer, new_head,
						next_page,
						RB_PAGE_HEAD);
	}

	/*
	 * If this was the outer most commit (the one that
	 * changed the original pointer from HEAD to UPDATE),
	 * then it is up to us to reset it to NORMAL.
	 */
	if (type == RB_PAGE_HEAD) {
		ret = rb_head_page_set_normal(cpu_buffer, next_page,
					      tail_page,
					      RB_PAGE_UPDATE);
		if (RB_WARN_ON(cpu_buffer,
			       ret != RB_PAGE_UPDATE))
			return -1;
	}

	return 0;
}

static inline void
rb_reset_tail(struct ring_buffer_per_cpu *cpu_buffer,
	      unsigned long tail, struct rb_event_info *info)
{
	unsigned long bsize = READ_ONCE(cpu_buffer->buffer->subbuf_size);
	struct buffer_page *tail_page = info->tail_page;
	struct ring_buffer_event *event;
	unsigned long length = info->length;

	/*
	 * Only the event that crossed the page boundary
	 * must fill the old tail_page with padding.
	 */
	if (tail >= bsize) {
		/*
		 * If the page was filled, then we still need
		 * to update the real_end. Reset it to zero
		 * and the reader will ignore it.
		 */
		if (tail == bsize)
			tail_page->real_end = 0;

		local_sub(length, &tail_page->write);
		return;
	}

	event = __rb_page_index(tail_page, tail);

	/*
	 * Save the original length to the meta data.
	 * This will be used by the reader to add lost event
	 * counter.
	 */
	tail_page->real_end = tail;

	/*
	 * If this event is bigger than the minimum size, then
	 * we need to be careful that we don't subtract the
	 * write counter enough to allow another writer to slip
	 * in on this page.
	 * We put in a discarded commit instead, to make sure
	 * that this space is not used again, and this space will
	 * not be accounted into 'entries_bytes'.
	 *
	 * If we are less than the minimum size, we don't need to
	 * worry about it.
	 */
	if (tail > (bsize - RB_EVNT_MIN_SIZE)) {
		/* No room for any events */

		/* Mark the rest of the page with padding */
		rb_event_set_padding(event);

		/* Make sure the padding is visible before the write update */
		smp_wmb();

		/* Set the write back to the previous setting */
		local_sub(length, &tail_page->write);
		return;
	}

	/* Put in a discarded event */
	event->array[0] = (bsize - tail) - RB_EVNT_HDR_SIZE;
	event->type_len = RINGBUF_TYPE_PADDING;
	/* time delta must be non zero */
	event->time_delta = 1;

	/* account for padding bytes */
	local_add(bsize - tail, &cpu_buffer->entries_bytes);

	/* Make sure the padding is visible before the tail_page->write update */
	smp_wmb();

	/* Set write to end of buffer */
	length = (tail + length) - bsize;
	local_sub(length, &tail_page->write);
}

static inline void rb_end_commit(struct ring_buffer_per_cpu *cpu_buffer);

/*
 * This is the slow path, force gcc not to inline it.
 */
static noinline struct ring_buffer_event *
rb_move_tail(struct ring_buffer_per_cpu *cpu_buffer,
	     unsigned long tail, struct rb_event_info *info)
{
	struct buffer_page *tail_page = info->tail_page;
	struct buffer_page *commit_page = cpu_buffer->commit_page;
	struct trace_buffer *buffer = cpu_buffer->buffer;
	struct buffer_page *next_page;
	int ret;

	next_page = tail_page;

	rb_inc_page(&next_page);

	/*
	 * If for some reason, we had an interrupt storm that made
	 * it all the way around the buffer, bail, and warn
	 * about it.
	 */
	if (unlikely(next_page == commit_page)) {
		local_inc(&cpu_buffer->commit_overrun);
		goto out_reset;
	}

	/*
	 * This is where the fun begins!
	 *
	 * We are fighting against races between a reader that
	 * could be on another CPU trying to swap its reader
	 * page with the buffer head.
	 *
	 * We are also fighting against interrupts coming in and
	 * moving the head or tail on us as well.
	 *
	 * If the next page is the head page then we have filled
	 * the buffer, unless the commit page is still on the
	 * reader page.
	 */
	if (rb_is_head_page(next_page, &tail_page->list)) {

		/*
		 * If the commit is not on the reader page, then
		 * move the header page.
		 */
		if (!rb_is_reader_page(cpu_buffer->commit_page)) {
			/*
			 * If we are not in overwrite mode,
			 * this is easy, just stop here.
			 */
			if (!(buffer->flags & RB_FL_OVERWRITE)) {
				local_inc(&cpu_buffer->dropped_events);
				goto out_reset;
			}

			ret = rb_handle_head_page(cpu_buffer,
						  tail_page,
						  next_page);
			if (ret < 0)
				goto out_reset;
			if (ret)
				goto out_again;
		} else {
			/*
			 * We need to be careful here too. The
			 * commit page could still be on the reader
			 * page. We could have a small buffer, and
			 * have filled up the buffer with events
			 * from interrupts and such, and wrapped.
			 *
			 * Note, if the tail page is also on the
			 * reader_page, we let it move out.
			 */
			if (unlikely((cpu_buffer->commit_page !=
				      cpu_buffer->tail_page) &&
				     (cpu_buffer->commit_page ==
				      cpu_buffer->reader_page))) {
				local_inc(&cpu_buffer->commit_overrun);
				goto out_reset;
			}
		}
	}

	rb_tail_page_update(cpu_buffer, tail_page, next_page);

 out_again:

	rb_reset_tail(cpu_buffer, tail, info);

	/* Commit what we have for now. */
	rb_end_commit(cpu_buffer);
	/* rb_end_commit() decs committing */
	local_inc(&cpu_buffer->committing);

	/* fail and let the caller try again */
	return ERR_PTR(-EAGAIN);

 out_reset:
	/* reset write */
	rb_reset_tail(cpu_buffer, tail, info);

	return NULL;
}

/* Slow path */
static struct ring_buffer_event *
rb_add_time_stamp(struct ring_buffer_per_cpu *cpu_buffer,
		  struct ring_buffer_event *event, u64 delta, bool abs)
{
	if (abs)
		event->type_len = RINGBUF_TYPE_TIME_STAMP;
	else
		event->type_len = RINGBUF_TYPE_TIME_EXTEND;

	/* Not the first event on the page, or not delta? */
	if (abs || rb_event_index(cpu_buffer, event)) {
		event->time_delta = delta & TS_MASK;
		event->array[0] = delta >> TS_SHIFT;
	} else {
		/* nope, just zero it */
		event->time_delta = 0;
		event->array[0] = 0;
	}

	return skip_time_extend(event);
}

#ifndef CONFIG_HAVE_UNSTABLE_SCHED_CLOCK
static inline bool sched_clock_stable(void)
{
	return true;
}
#endif

static void
rb_check_timestamp(struct ring_buffer_per_cpu *cpu_buffer,
		   struct rb_event_info *info)
{
	u64 write_stamp;

	WARN_ONCE(1, "Delta way too big! %llu ts=%llu before=%llu after=%llu write stamp=%llu\n%s",
		  (unsigned long long)info->delta,
		  (unsigned long long)info->ts,
		  (unsigned long long)info->before,
		  (unsigned long long)info->after,
		  (unsigned long long)({rb_time_read(&cpu_buffer->write_stamp, &write_stamp); write_stamp;}),
		  sched_clock_stable() ? "" :
		  "If you just came from a suspend/resume,\n"
		  "please switch to the trace global clock:\n"
		  "  echo global > /sys/kernel/tracing/trace_clock\n"
		  "or add trace_clock=global to the kernel command line\n");
}

static void rb_add_timestamp(struct ring_buffer_per_cpu *cpu_buffer,
				      struct ring_buffer_event **event,
				      struct rb_event_info *info,
				      u64 *delta,
				      unsigned int *length)
{
	bool abs = info->add_timestamp &
		(RB_ADD_STAMP_FORCE | RB_ADD_STAMP_ABSOLUTE);

	if (unlikely(info->delta > (1ULL << 59))) {
		/*
		 * Some timers can use more than 59 bits, and when a timestamp
		 * is added to the buffer, it will lose those bits.
		 */
		if (abs && (info->ts & TS_MSB)) {
			info->delta &= ABS_TS_MASK;

		/* did the clock go backwards */
		} else if (info->before == info->after && info->before > info->ts) {
			/* not interrupted */
			static int once;

			/*
			 * This is possible with a recalibrating of the TSC.
			 * Do not produce a call stack, but just report it.
			 */
			if (!once) {
				once++;
				pr_warn("Ring buffer clock went backwards: %llu -> %llu\n",
					info->before, info->ts);
			}
		} else
			rb_check_timestamp(cpu_buffer, info);
		if (!abs)
			info->delta = 0;
	}
	*event = rb_add_time_stamp(cpu_buffer, *event, info->delta, abs);
	*length -= RB_LEN_TIME_EXTEND;
	*delta = 0;
}

/**
 * rb_update_event - update event type and data
 * @cpu_buffer: The per cpu buffer of the @event
 * @event: the event to update
 * @info: The info to update the @event with (contains length and delta)
 *
 * Update the type and data fields of the @event. The length
 * is the actual size that is written to the ring buffer,
 * and with this, we can determine what to place into the
 * data field.
 */
static void
rb_update_event(struct ring_buffer_per_cpu *cpu_buffer,
		struct ring_buffer_event *event,
		struct rb_event_info *info)
{
	unsigned length = info->length;
	u64 delta = info->delta;
	unsigned int nest = local_read(&cpu_buffer->committing) - 1;

	if (!WARN_ON_ONCE(nest >= MAX_NEST))
		cpu_buffer->event_stamp[nest] = info->ts;

	/*
	 * If we need to add a timestamp, then we
	 * add it to the start of the reserved space.
	 */
	if (unlikely(info->add_timestamp))
		rb_add_timestamp(cpu_buffer, &event, info, &delta, &length);

	event->time_delta = delta;
	length -= RB_EVNT_HDR_SIZE;
	if (length > RB_MAX_SMALL_DATA || RB_FORCE_8BYTE_ALIGNMENT) {
		event->type_len = 0;
		event->array[0] = length;
	} else
		event->type_len = DIV_ROUND_UP(length, RB_ALIGNMENT);
}

static unsigned rb_calculate_event_length(unsigned length)
{
	struct ring_buffer_event event; /* Used only for sizeof array */

	/* zero length can cause confusions */
	if (!length)
		length++;

	if (length > RB_MAX_SMALL_DATA || RB_FORCE_8BYTE_ALIGNMENT)
		length += sizeof(event.array[0]);

	length += RB_EVNT_HDR_SIZE;
	length = ALIGN(length, RB_ARCH_ALIGNMENT);

	/*
	 * In case the time delta is larger than the 27 bits for it
	 * in the header, we need to add a timestamp. If another
	 * event comes in when trying to discard this one to increase
	 * the length, then the timestamp will be added in the allocated
	 * space of this event. If length is bigger than the size needed
	 * for the TIME_EXTEND, then padding has to be used. The events
	 * length must be either RB_LEN_TIME_EXTEND, or greater than or equal
	 * to RB_LEN_TIME_EXTEND + 8, as 8 is the minimum size for padding.
	 * As length is a multiple of 4, we only need to worry if it
	 * is 12 (RB_LEN_TIME_EXTEND + 4).
	 */
	if (length == RB_LEN_TIME_EXTEND + RB_ALIGNMENT)
		length += RB_ALIGNMENT;

	return length;
}

static inline bool
rb_try_to_discard(struct ring_buffer_per_cpu *cpu_buffer,
		  struct ring_buffer_event *event)
{
	unsigned long new_index, old_index;
	struct buffer_page *bpage;
	unsigned long addr;

	new_index = rb_event_index(cpu_buffer, event);
	old_index = new_index + rb_event_ts_length(event);
	addr = (unsigned long)event;
	addr &= ~((PAGE_SIZE << cpu_buffer->buffer->subbuf_order) - 1);

	bpage = READ_ONCE(cpu_buffer->tail_page);

	/*
	 * Make sure the tail_page is still the same and
	 * the next write location is the end of this event
	 */
	if (bpage->page == (void *)addr && rb_page_write(bpage) == old_index) {
		unsigned long write_mask =
			local_read(&bpage->write) & ~RB_WRITE_MASK;
		unsigned long event_length = rb_event_length(event);

		/*
		 * For the before_stamp to be different than the write_stamp
		 * to make sure that the next event adds an absolute
		 * value and does not rely on the saved write stamp, which
		 * is now going to be bogus.
		 *
		 * By setting the before_stamp to zero, the next event
		 * is not going to use the write_stamp and will instead
		 * create an absolute timestamp. This means there's no
		 * reason to update the wirte_stamp!
		 */
		rb_time_set(&cpu_buffer->before_stamp, 0);

		/*
		 * If an event were to come in now, it would see that the
		 * write_stamp and the before_stamp are different, and assume
		 * that this event just added itself before updating
		 * the write stamp. The interrupting event will fix the
		 * write stamp for us, and use an absolute timestamp.
		 */

		/*
		 * This is on the tail page. It is possible that
		 * a write could come in and move the tail page
		 * and write to the next page. That is fine
		 * because we just shorten what is on this page.
		 */
		old_index += write_mask;
		new_index += write_mask;

		/* caution: old_index gets updated on cmpxchg failure */
		if (local_try_cmpxchg(&bpage->write, &old_index, new_index)) {
			/* update counters */
			local_sub(event_length, &cpu_buffer->entries_bytes);
			return true;
		}
	}

	/* could not discard */
	return false;
}

static void rb_start_commit(struct ring_buffer_per_cpu *cpu_buffer)
{
	local_inc(&cpu_buffer->committing);
	local_inc(&cpu_buffer->commits);
}

static __always_inline void
rb_set_commit_to_write(struct ring_buffer_per_cpu *cpu_buffer)
{
	unsigned long max_count;

	/*
	 * We only race with interrupts and NMIs on this CPU.
	 * If we own the commit event, then we can commit
	 * all others that interrupted us, since the interruptions
	 * are in stack format (they finish before they come
	 * back to us). This allows us to do a simple loop to
	 * assign the commit to the tail.
	 */
 again:
	max_count = cpu_buffer->nr_pages * 100;

	while (cpu_buffer->commit_page != READ_ONCE(cpu_buffer->tail_page)) {
		if (RB_WARN_ON(cpu_buffer, !(--max_count)))
			return;
		if (RB_WARN_ON(cpu_buffer,
			       rb_is_reader_page(cpu_buffer->tail_page)))
			return;
		/*
		 * No need for a memory barrier here, as the update
		 * of the tail_page did it for this page.
		 */
		local_set(&cpu_buffer->commit_page->page->commit,
			  rb_page_write(cpu_buffer->commit_page));
		rb_inc_page(&cpu_buffer->commit_page);
		if (cpu_buffer->ring_meta) {
			struct ring_buffer_meta *meta = cpu_buffer->ring_meta;
			meta->commit_buffer = (unsigned long)cpu_buffer->commit_page->page;
		}
		/* add barrier to keep gcc from optimizing too much */
		barrier();
	}
	while (rb_commit_index(cpu_buffer) !=
	       rb_page_write(cpu_buffer->commit_page)) {

		/* Make sure the readers see the content of what is committed. */
		smp_wmb();
		local_set(&cpu_buffer->commit_page->page->commit,
			  rb_page_write(cpu_buffer->commit_page));
		RB_WARN_ON(cpu_buffer,
			   local_read(&cpu_buffer->commit_page->page->commit) &
			   ~RB_WRITE_MASK);
		barrier();
	}

	/* again, keep gcc from optimizing */
	barrier();

	/*
	 * If an interrupt came in just after the first while loop
	 * and pushed the tail page forward, we will be left with
	 * a dangling commit that will never go forward.
	 */
	if (unlikely(cpu_buffer->commit_page != READ_ONCE(cpu_buffer->tail_page)))
		goto again;
}

static __always_inline void rb_end_commit(struct ring_buffer_per_cpu *cpu_buffer)
{
	unsigned long commits;

	if (RB_WARN_ON(cpu_buffer,
		       !local_read(&cpu_buffer->committing)))
		return;

 again:
	commits = local_read(&cpu_buffer->commits);
	/* synchronize with interrupts */
	barrier();
	if (local_read(&cpu_buffer->committing) == 1)
		rb_set_commit_to_write(cpu_buffer);

	local_dec(&cpu_buffer->committing);

	/* synchronize with interrupts */
	barrier();

	/*
	 * Need to account for interrupts coming in between the
	 * updating of the commit page and the clearing of the
	 * committing counter.
	 */
	if (unlikely(local_read(&cpu_buffer->commits) != commits) &&
	    !local_read(&cpu_buffer->committing)) {
		local_inc(&cpu_buffer->committing);
		goto again;
	}
}

static inline void rb_event_discard(struct ring_buffer_event *event)
{
	if (extended_time(event))
		event = skip_time_extend(event);

	/* array[0] holds the actual length for the discarded event */
	event->array[0] = rb_event_data_length(event) - RB_EVNT_HDR_SIZE;
	event->type_len = RINGBUF_TYPE_PADDING;
	/* time delta must be non zero */
	if (!event->time_delta)
		event->time_delta = 1;
}

static void rb_commit(struct ring_buffer_per_cpu *cpu_buffer)
{
	local_inc(&cpu_buffer->entries);
	rb_end_commit(cpu_buffer);
}

static __always_inline void
rb_wakeups(struct trace_buffer *buffer, struct ring_buffer_per_cpu *cpu_buffer)
{
	if (buffer->irq_work.waiters_pending) {
		buffer->irq_work.waiters_pending = false;
		/* irq_work_queue() supplies it's own memory barriers */
		irq_work_queue(&buffer->irq_work.work);
	}

	if (cpu_buffer->irq_work.waiters_pending) {
		cpu_buffer->irq_work.waiters_pending = false;
		/* irq_work_queue() supplies it's own memory barriers */
		irq_work_queue(&cpu_buffer->irq_work.work);
	}

	if (cpu_buffer->last_pages_touch == local_read(&cpu_buffer->pages_touched))
		return;

	if (cpu_buffer->reader_page == cpu_buffer->commit_page)
		return;

	if (!cpu_buffer->irq_work.full_waiters_pending)
		return;

	cpu_buffer->last_pages_touch = local_read(&cpu_buffer->pages_touched);

	if (!full_hit(buffer, cpu_buffer->cpu, cpu_buffer->shortest_full))
		return;

	cpu_buffer->irq_work.wakeup_full = true;
	cpu_buffer->irq_work.full_waiters_pending = false;
	/* irq_work_queue() supplies it's own memory barriers */
	irq_work_queue(&cpu_buffer->irq_work.work);
}

#ifdef CONFIG_RING_BUFFER_RECORD_RECURSION
# define do_ring_buffer_record_recursion()	\
	do_ftrace_record_recursion(_THIS_IP_, _RET_IP_)
#else
# define do_ring_buffer_record_recursion() do { } while (0)
#endif

/*
 * The lock and unlock are done within a preempt disable section.
 * The current_context per_cpu variable can only be modified
 * by the current task between lock and unlock. But it can
 * be modified more than once via an interrupt. To pass this
 * information from the lock to the unlock without having to
 * access the 'in_interrupt()' functions again (which do show
 * a bit of overhead in something as critical as function tracing,
 * we use a bitmask trick.
 *
 *  bit 1 =  NMI context
 *  bit 2 =  IRQ context
 *  bit 3 =  SoftIRQ context
 *  bit 4 =  normal context.
 *
 * This works because this is the order of contexts that can
 * preempt other contexts. A SoftIRQ never preempts an IRQ
 * context.
 *
 * When the context is determined, the corresponding bit is
 * checked and set (if it was set, then a recursion of that context
 * happened).
 *
 * On unlock, we need to clear this bit. To do so, just subtract
 * 1 from the current_context and AND it to itself.
 *
 * (binary)
 *  101 - 1 = 100
 *  101 & 100 = 100 (clearing bit zero)
 *
 *  1010 - 1 = 1001
 *  1010 & 1001 = 1000 (clearing bit 1)
 *
 * The least significant bit can be cleared this way, and it
 * just so happens that it is the same bit corresponding to
 * the current context.
 *
 * Now the TRANSITION bit breaks the above slightly. The TRANSITION bit
 * is set when a recursion is detected at the current context, and if
 * the TRANSITION bit is already set, it will fail the recursion.
 * This is needed because there's a lag between the changing of
 * interrupt context and updating the preempt count. In this case,
 * a false positive will be found. To handle this, one extra recursion
 * is allowed, and this is done by the TRANSITION bit. If the TRANSITION
 * bit is already set, then it is considered a recursion and the function
 * ends. Otherwise, the TRANSITION bit is set, and that bit is returned.
 *
 * On the trace_recursive_unlock(), the TRANSITION bit will be the first
 * to be cleared. Even if it wasn't the context that set it. That is,
 * if an interrupt comes in while NORMAL bit is set and the ring buffer
 * is called before preempt_count() is updated, since the check will
 * be on the NORMAL bit, the TRANSITION bit will then be set. If an
 * NMI then comes in, it will set the NMI bit, but when the NMI code
 * does the trace_recursive_unlock() it will clear the TRANSITION bit
 * and leave the NMI bit set. But this is fine, because the interrupt
 * code that set the TRANSITION bit will then clear the NMI bit when it
 * calls trace_recursive_unlock(). If another NMI comes in, it will
 * set the TRANSITION bit and continue.
 *
 * Note: The TRANSITION bit only handles a single transition between context.
 */

static __always_inline bool
trace_recursive_lock(struct ring_buffer_per_cpu *cpu_buffer)
{
	unsigned int val = cpu_buffer->current_context;
	int bit = interrupt_context_level();

	bit = RB_CTX_NORMAL - bit;

	if (unlikely(val & (1 << (bit + cpu_buffer->nest)))) {
		/*
		 * It is possible that this was called by transitioning
		 * between interrupt context, and preempt_count() has not
		 * been updated yet. In this case, use the TRANSITION bit.
		 */
		bit = RB_CTX_TRANSITION;
		if (val & (1 << (bit + cpu_buffer->nest))) {
			do_ring_buffer_record_recursion();
			return true;
		}
	}

	val |= (1 << (bit + cpu_buffer->nest));
	cpu_buffer->current_context = val;

	return false;
}

static __always_inline void
trace_recursive_unlock(struct ring_buffer_per_cpu *cpu_buffer)
{
	cpu_buffer->current_context &=
		cpu_buffer->current_context - (1 << cpu_buffer->nest);
}

/* The recursive locking above uses 5 bits */
#define NESTED_BITS 5

/**
 * ring_buffer_nest_start - Allow to trace while nested
 * @buffer: The ring buffer to modify
 *
 * The ring buffer has a safety mechanism to prevent recursion.
 * But there may be a case where a trace needs to be done while
 * tracing something else. In this case, calling this function
 * will allow this function to nest within a currently active
 * ring_buffer_lock_reserve().
 *
 * Call this function before calling another ring_buffer_lock_reserve() and
 * call ring_buffer_nest_end() after the nested ring_buffer_unlock_commit().
 */
void ring_buffer_nest_start(struct trace_buffer *buffer)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	int cpu;

	/* Enabled by ring_buffer_nest_end() */
	preempt_disable_notrace();
	cpu = raw_smp_processor_id();
	cpu_buffer = buffer->buffers[cpu];
	/* This is the shift value for the above recursive locking */
	cpu_buffer->nest += NESTED_BITS;
}

/**
 * ring_buffer_nest_end - Allow to trace while nested
 * @buffer: The ring buffer to modify
 *
 * Must be called after ring_buffer_nest_start() and after the
 * ring_buffer_unlock_commit().
 */
void ring_buffer_nest_end(struct trace_buffer *buffer)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	int cpu;

	/* disabled by ring_buffer_nest_start() */
	cpu = raw_smp_processor_id();
	cpu_buffer = buffer->buffers[cpu];
	/* This is the shift value for the above recursive locking */
	cpu_buffer->nest -= NESTED_BITS;
	preempt_enable_notrace();
}

/**
 * ring_buffer_unlock_commit - commit a reserved
 * @buffer: The buffer to commit to
 *
 * This commits the data to the ring buffer, and releases any locks held.
 *
 * Must be paired with ring_buffer_lock_reserve.
 */
int ring_buffer_unlock_commit(struct trace_buffer *buffer)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	int cpu = raw_smp_processor_id();

	cpu_buffer = buffer->buffers[cpu];

	rb_commit(cpu_buffer);

	rb_wakeups(buffer, cpu_buffer);

	trace_recursive_unlock(cpu_buffer);

	preempt_enable_notrace();

	return 0;
}
EXPORT_SYMBOL_GPL(ring_buffer_unlock_commit);

/* Special value to validate all deltas on a page. */
#define CHECK_FULL_PAGE		1L

#ifdef CONFIG_RING_BUFFER_VALIDATE_TIME_DELTAS

static const char *show_irq_str(int bits)
{
	const char *type[] = {
		".",	// 0
		"s",	// 1
		"h",	// 2
		"Hs",	// 3
		"n",	// 4
		"Ns",	// 5
		"Nh",	// 6
		"NHs",	// 7
	};

	return type[bits];
}

/* Assume this is a trace event */
static const char *show_flags(struct ring_buffer_event *event)
{
	struct trace_entry *entry;
	int bits = 0;

	if (rb_event_data_length(event) - RB_EVNT_HDR_SIZE < sizeof(*entry))
		return "X";

	entry = ring_buffer_event_data(event);

	if (entry->flags & TRACE_FLAG_SOFTIRQ)
		bits |= 1;

	if (entry->flags & TRACE_FLAG_HARDIRQ)
		bits |= 2;

	if (entry->flags & TRACE_FLAG_NMI)
		bits |= 4;

	return show_irq_str(bits);
}

static const char *show_irq(struct ring_buffer_event *event)
{
	struct trace_entry *entry;

	if (rb_event_data_length(event) - RB_EVNT_HDR_SIZE < sizeof(*entry))
		return "";

	entry = ring_buffer_event_data(event);
	if (entry->flags & TRACE_FLAG_IRQS_OFF)
		return "d";
	return "";
}

static const char *show_interrupt_level(void)
{
	unsigned long pc = preempt_count();
	unsigned char level = 0;

	if (pc & SOFTIRQ_OFFSET)
		level |= 1;

	if (pc & HARDIRQ_MASK)
		level |= 2;

	if (pc & NMI_MASK)
		level |= 4;

	return show_irq_str(level);
}

static void dump_buffer_page(struct buffer_data_page *bpage,
			     struct rb_event_info *info,
			     unsigned long tail)
{
	struct ring_buffer_event *event;
	u64 ts, delta;
	int e;

	ts = bpage->time_stamp;
	pr_warn("  [%lld] PAGE TIME STAMP\n", ts);

	for (e = 0; e < tail; e += rb_event_length(event)) {

		event = (struct ring_buffer_event *)(bpage->data + e);

		switch (event->type_len) {

		case RINGBUF_TYPE_TIME_EXTEND:
			delta = rb_event_time_stamp(event);
			ts += delta;
			pr_warn(" 0x%x: [%lld] delta:%lld TIME EXTEND\n",
				e, ts, delta);
			break;

		case RINGBUF_TYPE_TIME_STAMP:
			delta = rb_event_time_stamp(event);
			ts = rb_fix_abs_ts(delta, ts);
			pr_warn(" 0x%x:  [%lld] absolute:%lld TIME STAMP\n",
				e, ts, delta);
			break;

		case RINGBUF_TYPE_PADDING:
			ts += event->time_delta;
			pr_warn(" 0x%x:  [%lld] delta:%d PADDING\n",
				e, ts, event->time_delta);
			break;

		case RINGBUF_TYPE_DATA:
			ts += event->time_delta;
			pr_warn(" 0x%x:  [%lld] delta:%d %s%s\n",
				e, ts, event->time_delta,
				show_flags(event), show_irq(event));
			break;

		default:
			break;
		}
	}
	pr_warn("expected end:0x%lx last event actually ended at:0x%x\n", tail, e);
}

static DEFINE_PER_CPU(atomic_t, checking);
static atomic_t ts_dump;

#define buffer_warn_return(fmt, ...)					\
	do {								\
		/* If another report is happening, ignore this one */	\
		if (atomic_inc_return(&ts_dump) != 1) {			\
			atomic_dec(&ts_dump);				\
			goto out;					\
		}							\
		atomic_inc(&cpu_buffer->record_disabled);		\
		pr_warn(fmt, ##__VA_ARGS__);				\
		dump_buffer_page(bpage, info, tail);			\
		atomic_dec(&ts_dump);					\
		/* There's some cases in boot up that this can happen */ \
		if (WARN_ON_ONCE(system_state != SYSTEM_BOOTING))	\
			/* Do not re-enable checking */			\
			return;						\
	} while (0)

/*
 * Check if the current event time stamp matches the deltas on
 * the buffer page.
 */
static void check_buffer(struct ring_buffer_per_cpu *cpu_buffer,
			 struct rb_event_info *info,
			 unsigned long tail)
{
	struct buffer_data_page *bpage;
	u64 ts, delta;
	bool full = false;
	int ret;

	bpage = info->tail_page->page;

	if (tail == CHECK_FULL_PAGE) {
		full = true;
		tail = local_read(&bpage->commit);
	} else if (info->add_timestamp &
		   (RB_ADD_STAMP_FORCE | RB_ADD_STAMP_ABSOLUTE)) {
		/* Ignore events with absolute time stamps */
		return;
	}

	/*
	 * Do not check the first event (skip possible extends too).
	 * Also do not check if previous events have not been committed.
	 */
	if (tail <= 8 || tail > local_read(&bpage->commit))
		return;

	/*
	 * If this interrupted another event,
	 */
	if (atomic_inc_return(this_cpu_ptr(&checking)) != 1)
		goto out;

	ret = rb_read_data_buffer(bpage, tail, cpu_buffer->cpu, &ts, &delta);
	if (ret < 0) {
		if (delta < ts) {
			buffer_warn_return("[CPU: %d]ABSOLUTE TIME WENT BACKWARDS: last ts: %lld absolute ts: %lld\n",
					   cpu_buffer->cpu, ts, delta);
			goto out;
		}
	}
	if ((full && ts > info->ts) ||
	    (!full && ts + info->delta != info->ts)) {
		buffer_warn_return("[CPU: %d]TIME DOES NOT MATCH expected:%lld actual:%lld delta:%lld before:%lld after:%lld%s context:%s\n",
				   cpu_buffer->cpu,
				   ts + info->delta, info->ts, info->delta,
				   info->before, info->after,
				   full ? " (full)" : "", show_interrupt_level());
	}
out:
	atomic_dec(this_cpu_ptr(&checking));
}
#else
static inline void check_buffer(struct ring_buffer_per_cpu *cpu_buffer,
			 struct rb_event_info *info,
			 unsigned long tail)
{
}
#endif /* CONFIG_RING_BUFFER_VALIDATE_TIME_DELTAS */

static struct ring_buffer_event *
__rb_reserve_next(struct ring_buffer_per_cpu *cpu_buffer,
		  struct rb_event_info *info)
{
	struct ring_buffer_event *event;
	struct buffer_page *tail_page;
	unsigned long tail, write, w;

	/* Don't let the compiler play games with cpu_buffer->tail_page */
	tail_page = info->tail_page = READ_ONCE(cpu_buffer->tail_page);

 /*A*/	w = local_read(&tail_page->write) & RB_WRITE_MASK;
	barrier();
	rb_time_read(&cpu_buffer->before_stamp, &info->before);
	rb_time_read(&cpu_buffer->write_stamp, &info->after);
	barrier();
	info->ts = rb_time_stamp(cpu_buffer->buffer);

	if ((info->add_timestamp & RB_ADD_STAMP_ABSOLUTE)) {
		info->delta = info->ts;
	} else {
		/*
		 * If interrupting an event time update, we may need an
		 * absolute timestamp.
		 * Don't bother if this is the start of a new page (w == 0).
		 */
		if (!w) {
			/* Use the sub-buffer timestamp */
			info->delta = 0;
		} else if (unlikely(info->before != info->after)) {
			info->add_timestamp |= RB_ADD_STAMP_FORCE | RB_ADD_STAMP_EXTEND;
			info->length += RB_LEN_TIME_EXTEND;
		} else {
			info->delta = info->ts - info->after;
			if (unlikely(test_time_stamp(info->delta))) {
				info->add_timestamp |= RB_ADD_STAMP_EXTEND;
				info->length += RB_LEN_TIME_EXTEND;
			}
		}
	}

 /*B*/	rb_time_set(&cpu_buffer->before_stamp, info->ts);

 /*C*/	write = local_add_return(info->length, &tail_page->write);

	/* set write to only the index of the write */
	write &= RB_WRITE_MASK;

	tail = write - info->length;

	/* See if we shot pass the end of this buffer page */
	if (unlikely(write > cpu_buffer->buffer->subbuf_size)) {
		check_buffer(cpu_buffer, info, CHECK_FULL_PAGE);
		return rb_move_tail(cpu_buffer, tail, info);
	}

	if (likely(tail == w)) {
		/* Nothing interrupted us between A and C */
 /*D*/		rb_time_set(&cpu_buffer->write_stamp, info->ts);
		/*
		 * If something came in between C and D, the write stamp
		 * may now not be in sync. But that's fine as the before_stamp
		 * will be different and then next event will just be forced
		 * to use an absolute timestamp.
		 */
		if (likely(!(info->add_timestamp &
			     (RB_ADD_STAMP_FORCE | RB_ADD_STAMP_ABSOLUTE))))
			/* This did not interrupt any time update */
			info->delta = info->ts - info->after;
		else
			/* Just use full timestamp for interrupting event */
			info->delta = info->ts;
		check_buffer(cpu_buffer, info, tail);
	} else {
		u64 ts;
		/* SLOW PATH - Interrupted between A and C */

		/* Save the old before_stamp */
		rb_time_read(&cpu_buffer->before_stamp, &info->before);

		/*
		 * Read a new timestamp and update the before_stamp to make
		 * the next event after this one force using an absolute
		 * timestamp. This is in case an interrupt were to come in
		 * between E and F.
		 */
		ts = rb_time_stamp(cpu_buffer->buffer);
		rb_time_set(&cpu_buffer->before_stamp, ts);

		barrier();
 /*E*/		rb_time_read(&cpu_buffer->write_stamp, &info->after);
		barrier();
 /*F*/		if (write == (local_read(&tail_page->write) & RB_WRITE_MASK) &&
		    info->after == info->before && info->after < ts) {
			/*
			 * Nothing came after this event between C and F, it is
			 * safe to use info->after for the delta as it
			 * matched info->before and is still valid.
			 */
			info->delta = ts - info->after;
		} else {
			/*
			 * Interrupted between C and F:
			 * Lost the previous events time stamp. Just set the
			 * delta to zero, and this will be the same time as
			 * the event this event interrupted. And the events that
			 * came after this will still be correct (as they would
			 * have built their delta on the previous event.
			 */
			info->delta = 0;
		}
		info->ts = ts;
		info->add_timestamp &= ~RB_ADD_STAMP_FORCE;
	}

	/*
	 * If this is the first commit on the page, then it has the same
	 * timestamp as the page itself.
	 */
	if (unlikely(!tail && !(info->add_timestamp &
				(RB_ADD_STAMP_FORCE | RB_ADD_STAMP_ABSOLUTE))))
		info->delta = 0;

	/* We reserved something on the buffer */

	event = __rb_page_index(tail_page, tail);
	rb_update_event(cpu_buffer, event, info);

	local_inc(&tail_page->entries);

	/*
	 * If this is the first commit on the page, then update
	 * its timestamp.
	 */
	if (unlikely(!tail))
		tail_page->page->time_stamp = info->ts;

	/* account for these added bytes */
	local_add(info->length, &cpu_buffer->entries_bytes);

	return event;
}

static __always_inline struct ring_buffer_event *
rb_reserve_next_event(struct trace_buffer *buffer,
		      struct ring_buffer_per_cpu *cpu_buffer,
		      unsigned long length)
{
	struct ring_buffer_event *event;
	struct rb_event_info info;
	int nr_loops = 0;
	int add_ts_default;

	/* ring buffer does cmpxchg, make sure it is safe in NMI context */
	if (!IS_ENABLED(CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG) &&
	    (unlikely(in_nmi()))) {
		return NULL;
	}

	rb_start_commit(cpu_buffer);
	/* The commit page can not change after this */

#ifdef CONFIG_RING_BUFFER_ALLOW_SWAP
	/*
	 * Due to the ability to swap a cpu buffer from a buffer
	 * it is possible it was swapped before we committed.
	 * (committing stops a swap). We check for it here and
	 * if it happened, we have to fail the write.
	 */
	barrier();
	if (unlikely(READ_ONCE(cpu_buffer->buffer) != buffer)) {
		local_dec(&cpu_buffer->committing);
		local_dec(&cpu_buffer->commits);
		return NULL;
	}
#endif

	info.length = rb_calculate_event_length(length);

	if (ring_buffer_time_stamp_abs(cpu_buffer->buffer)) {
		add_ts_default = RB_ADD_STAMP_ABSOLUTE;
		info.length += RB_LEN_TIME_EXTEND;
		if (info.length > cpu_buffer->buffer->max_data_size)
			goto out_fail;
	} else {
		add_ts_default = RB_ADD_STAMP_NONE;
	}

 again:
	info.add_timestamp = add_ts_default;
	info.delta = 0;

	/*
	 * We allow for interrupts to reenter here and do a trace.
	 * If one does, it will cause this original code to loop
	 * back here. Even with heavy interrupts happening, this
	 * should only happen a few times in a row. If this happens
	 * 1000 times in a row, there must be either an interrupt
	 * storm or we have something buggy.
	 * Bail!
	 */
	if (RB_WARN_ON(cpu_buffer, ++nr_loops > 1000))
		goto out_fail;

	event = __rb_reserve_next(cpu_buffer, &info);

	if (unlikely(PTR_ERR(event) == -EAGAIN)) {
		if (info.add_timestamp & (RB_ADD_STAMP_FORCE | RB_ADD_STAMP_EXTEND))
			info.length -= RB_LEN_TIME_EXTEND;
		goto again;
	}

	if (likely(event))
		return event;
 out_fail:
	rb_end_commit(cpu_buffer);
	return NULL;
}

/**
 * ring_buffer_lock_reserve - reserve a part of the buffer
 * @buffer: the ring buffer to reserve from
 * @length: the length of the data to reserve (excluding event header)
 *
 * Returns a reserved event on the ring buffer to copy directly to.
 * The user of this interface will need to get the body to write into
 * and can use the ring_buffer_event_data() interface.
 *
 * The length is the length of the data needed, not the event length
 * which also includes the event header.
 *
 * Must be paired with ring_buffer_unlock_commit, unless NULL is returned.
 * If NULL is returned, then nothing has been allocated or locked.
 */
struct ring_buffer_event *
ring_buffer_lock_reserve(struct trace_buffer *buffer, unsigned long length)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct ring_buffer_event *event;
	int cpu;

	/* If we are tracing schedule, we don't want to recurse */
	preempt_disable_notrace();

	if (unlikely(atomic_read(&buffer->record_disabled)))
		goto out;

	cpu = raw_smp_processor_id();

	if (unlikely(!cpumask_test_cpu(cpu, buffer->cpumask)))
		goto out;

	cpu_buffer = buffer->buffers[cpu];

	if (unlikely(atomic_read(&cpu_buffer->record_disabled)))
		goto out;

	if (unlikely(length > buffer->max_data_size))
		goto out;

	if (unlikely(trace_recursive_lock(cpu_buffer)))
		goto out;

	event = rb_reserve_next_event(buffer, cpu_buffer, length);
	if (!event)
		goto out_unlock;

	return event;

 out_unlock:
	trace_recursive_unlock(cpu_buffer);
 out:
	preempt_enable_notrace();
	return NULL;
}
EXPORT_SYMBOL_GPL(ring_buffer_lock_reserve);

/*
 * Decrement the entries to the page that an event is on.
 * The event does not even need to exist, only the pointer
 * to the page it is on. This may only be called before the commit
 * takes place.
 */
static inline void
rb_decrement_entry(struct ring_buffer_per_cpu *cpu_buffer,
		   struct ring_buffer_event *event)
{
	unsigned long addr = (unsigned long)event;
	struct buffer_page *bpage = cpu_buffer->commit_page;
	struct buffer_page *start;

	addr &= ~((PAGE_SIZE << cpu_buffer->buffer->subbuf_order) - 1);

	/* Do the likely case first */
	if (likely(bpage->page == (void *)addr)) {
		local_dec(&bpage->entries);
		return;
	}

	/*
	 * Because the commit page may be on the reader page we
	 * start with the next page and check the end loop there.
	 */
	rb_inc_page(&bpage);
	start = bpage;
	do {
		if (bpage->page == (void *)addr) {
			local_dec(&bpage->entries);
			return;
		}
		rb_inc_page(&bpage);
	} while (bpage != start);

	/* commit not part of this buffer?? */
	RB_WARN_ON(cpu_buffer, 1);
}

/**
 * ring_buffer_discard_commit - discard an event that has not been committed
 * @buffer: the ring buffer
 * @event: non committed event to discard
 *
 * Sometimes an event that is in the ring buffer needs to be ignored.
 * This function lets the user discard an event in the ring buffer
 * and then that event will not be read later.
 *
 * This function only works if it is called before the item has been
 * committed. It will try to free the event from the ring buffer
 * if another event has not been added behind it.
 *
 * If another event has been added behind it, it will set the event
 * up as discarded, and perform the commit.
 *
 * If this function is called, do not call ring_buffer_unlock_commit on
 * the event.
 */
void ring_buffer_discard_commit(struct trace_buffer *buffer,
				struct ring_buffer_event *event)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	int cpu;

	/* The event is discarded regardless */
	rb_event_discard(event);

	cpu = smp_processor_id();
	cpu_buffer = buffer->buffers[cpu];

	/*
	 * This must only be called if the event has not been
	 * committed yet. Thus we can assume that preemption
	 * is still disabled.
	 */
	RB_WARN_ON(buffer, !local_read(&cpu_buffer->committing));

	rb_decrement_entry(cpu_buffer, event);
	if (rb_try_to_discard(cpu_buffer, event))
		goto out;

 out:
	rb_end_commit(cpu_buffer);

	trace_recursive_unlock(cpu_buffer);

	preempt_enable_notrace();

}
EXPORT_SYMBOL_GPL(ring_buffer_discard_commit);

/**
 * ring_buffer_write - write data to the buffer without reserving
 * @buffer: The ring buffer to write to.
 * @length: The length of the data being written (excluding the event header)
 * @data: The data to write to the buffer.
 *
 * This is like ring_buffer_lock_reserve and ring_buffer_unlock_commit as
 * one function. If you already have the data to write to the buffer, it
 * may be easier to simply call this function.
 *
 * Note, like ring_buffer_lock_reserve, the length is the length of the data
 * and not the length of the event which would hold the header.
 */
int ring_buffer_write(struct trace_buffer *buffer,
		      unsigned long length,
		      void *data)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct ring_buffer_event *event;
	void *body;
	int ret = -EBUSY;
	int cpu;

	preempt_disable_notrace();

	if (atomic_read(&buffer->record_disabled))
		goto out;

	cpu = raw_smp_processor_id();

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		goto out;

	cpu_buffer = buffer->buffers[cpu];

	if (atomic_read(&cpu_buffer->record_disabled))
		goto out;

	if (length > buffer->max_data_size)
		goto out;

	if (unlikely(trace_recursive_lock(cpu_buffer)))
		goto out;

	event = rb_reserve_next_event(buffer, cpu_buffer, length);
	if (!event)
		goto out_unlock;

	body = rb_event_data(event);

	memcpy(body, data, length);

	rb_commit(cpu_buffer);

	rb_wakeups(buffer, cpu_buffer);

	ret = 0;

 out_unlock:
	trace_recursive_unlock(cpu_buffer);

 out:
	preempt_enable_notrace();

	return ret;
}
EXPORT_SYMBOL_GPL(ring_buffer_write);

static bool rb_per_cpu_empty(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct buffer_page *reader = cpu_buffer->reader_page;
	struct buffer_page *head = rb_set_head_page(cpu_buffer);
	struct buffer_page *commit = cpu_buffer->commit_page;

	/* In case of error, head will be NULL */
	if (unlikely(!head))
		return true;

	/* Reader should exhaust content in reader page */
	if (reader->read != rb_page_size(reader))
		return false;

	/*
	 * If writers are committing on the reader page, knowing all
	 * committed content has been read, the ring buffer is empty.
	 */
	if (commit == reader)
		return true;

	/*
	 * If writers are committing on a page other than reader page
	 * and head page, there should always be content to read.
	 */
	if (commit != head)
		return false;

	/*
	 * Writers are committing on the head page, we just need
	 * to care about there're committed data, and the reader will
	 * swap reader page with head page when it is to read data.
	 */
	return rb_page_commit(commit) == 0;
}

/**
 * ring_buffer_record_disable - stop all writes into the buffer
 * @buffer: The ring buffer to stop writes to.
 *
 * This prevents all writes to the buffer. Any attempt to write
 * to the buffer after this will fail and return NULL.
 *
 * The caller should call synchronize_rcu() after this.
 */
void ring_buffer_record_disable(struct trace_buffer *buffer)
{
	atomic_inc(&buffer->record_disabled);
}
EXPORT_SYMBOL_GPL(ring_buffer_record_disable);

/**
 * ring_buffer_record_enable - enable writes to the buffer
 * @buffer: The ring buffer to enable writes
 *
 * Note, multiple disables will need the same number of enables
 * to truly enable the writing (much like preempt_disable).
 */
void ring_buffer_record_enable(struct trace_buffer *buffer)
{
	atomic_dec(&buffer->record_disabled);
}
EXPORT_SYMBOL_GPL(ring_buffer_record_enable);

/**
 * ring_buffer_record_off - stop all writes into the buffer
 * @buffer: The ring buffer to stop writes to.
 *
 * This prevents all writes to the buffer. Any attempt to write
 * to the buffer after this will fail and return NULL.
 *
 * This is different than ring_buffer_record_disable() as
 * it works like an on/off switch, where as the disable() version
 * must be paired with a enable().
 */
void ring_buffer_record_off(struct trace_buffer *buffer)
{
	unsigned int rd;
	unsigned int new_rd;

	rd = atomic_read(&buffer->record_disabled);
	do {
		new_rd = rd | RB_BUFFER_OFF;
	} while (!atomic_try_cmpxchg(&buffer->record_disabled, &rd, new_rd));
}
EXPORT_SYMBOL_GPL(ring_buffer_record_off);

/**
 * ring_buffer_record_on - restart writes into the buffer
 * @buffer: The ring buffer to start writes to.
 *
 * This enables all writes to the buffer that was disabled by
 * ring_buffer_record_off().
 *
 * This is different than ring_buffer_record_enable() as
 * it works like an on/off switch, where as the enable() version
 * must be paired with a disable().
 */
void ring_buffer_record_on(struct trace_buffer *buffer)
{
	unsigned int rd;
	unsigned int new_rd;

	rd = atomic_read(&buffer->record_disabled);
	do {
		new_rd = rd & ~RB_BUFFER_OFF;
	} while (!atomic_try_cmpxchg(&buffer->record_disabled, &rd, new_rd));
}
EXPORT_SYMBOL_GPL(ring_buffer_record_on);

/**
 * ring_buffer_record_is_on - return true if the ring buffer can write
 * @buffer: The ring buffer to see if write is enabled
 *
 * Returns true if the ring buffer is in a state that it accepts writes.
 */
bool ring_buffer_record_is_on(struct trace_buffer *buffer)
{
	return !atomic_read(&buffer->record_disabled);
}

/**
 * ring_buffer_record_is_set_on - return true if the ring buffer is set writable
 * @buffer: The ring buffer to see if write is set enabled
 *
 * Returns true if the ring buffer is set writable by ring_buffer_record_on().
 * Note that this does NOT mean it is in a writable state.
 *
 * It may return true when the ring buffer has been disabled by
 * ring_buffer_record_disable(), as that is a temporary disabling of
 * the ring buffer.
 */
bool ring_buffer_record_is_set_on(struct trace_buffer *buffer)
{
	return !(atomic_read(&buffer->record_disabled) & RB_BUFFER_OFF);
}

/**
 * ring_buffer_record_disable_cpu - stop all writes into the cpu_buffer
 * @buffer: The ring buffer to stop writes to.
 * @cpu: The CPU buffer to stop
 *
 * This prevents all writes to the buffer. Any attempt to write
 * to the buffer after this will fail and return NULL.
 *
 * The caller should call synchronize_rcu() after this.
 */
void ring_buffer_record_disable_cpu(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return;

	cpu_buffer = buffer->buffers[cpu];
	atomic_inc(&cpu_buffer->record_disabled);
}
EXPORT_SYMBOL_GPL(ring_buffer_record_disable_cpu);

/**
 * ring_buffer_record_enable_cpu - enable writes to the buffer
 * @buffer: The ring buffer to enable writes
 * @cpu: The CPU to enable.
 *
 * Note, multiple disables will need the same number of enables
 * to truly enable the writing (much like preempt_disable).
 */
void ring_buffer_record_enable_cpu(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return;

	cpu_buffer = buffer->buffers[cpu];
	atomic_dec(&cpu_buffer->record_disabled);
}
EXPORT_SYMBOL_GPL(ring_buffer_record_enable_cpu);

/*
 * The total entries in the ring buffer is the running counter
 * of entries entered into the ring buffer, minus the sum of
 * the entries read from the ring buffer and the number of
 * entries that were overwritten.
 */
static inline unsigned long
rb_num_of_entries(struct ring_buffer_per_cpu *cpu_buffer)
{
	return local_read(&cpu_buffer->entries) -
		(local_read(&cpu_buffer->overrun) + cpu_buffer->read);
}

/**
 * ring_buffer_oldest_event_ts - get the oldest event timestamp from the buffer
 * @buffer: The ring buffer
 * @cpu: The per CPU buffer to read from.
 */
u64 ring_buffer_oldest_event_ts(struct trace_buffer *buffer, int cpu)
{
	unsigned long flags;
	struct ring_buffer_per_cpu *cpu_buffer;
	struct buffer_page *bpage;
	u64 ret = 0;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return 0;

	cpu_buffer = buffer->buffers[cpu];
	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);
	/*
	 * if the tail is on reader_page, oldest time stamp is on the reader
	 * page
	 */
	if (cpu_buffer->tail_page == cpu_buffer->reader_page)
		bpage = cpu_buffer->reader_page;
	else
		bpage = rb_set_head_page(cpu_buffer);
	if (bpage)
		ret = bpage->page->time_stamp;
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(ring_buffer_oldest_event_ts);

/**
 * ring_buffer_bytes_cpu - get the number of bytes unconsumed in a cpu buffer
 * @buffer: The ring buffer
 * @cpu: The per CPU buffer to read from.
 */
unsigned long ring_buffer_bytes_cpu(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long ret;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return 0;

	cpu_buffer = buffer->buffers[cpu];
	ret = local_read(&cpu_buffer->entries_bytes) - cpu_buffer->read_bytes;

	return ret;
}
EXPORT_SYMBOL_GPL(ring_buffer_bytes_cpu);

/**
 * ring_buffer_entries_cpu - get the number of entries in a cpu buffer
 * @buffer: The ring buffer
 * @cpu: The per CPU buffer to get the entries from.
 */
unsigned long ring_buffer_entries_cpu(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return 0;

	cpu_buffer = buffer->buffers[cpu];

	return rb_num_of_entries(cpu_buffer);
}
EXPORT_SYMBOL_GPL(ring_buffer_entries_cpu);

/**
 * ring_buffer_overrun_cpu - get the number of overruns caused by the ring
 * buffer wrapping around (only if RB_FL_OVERWRITE is on).
 * @buffer: The ring buffer
 * @cpu: The per CPU buffer to get the number of overruns from
 */
unsigned long ring_buffer_overrun_cpu(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long ret;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return 0;

	cpu_buffer = buffer->buffers[cpu];
	ret = local_read(&cpu_buffer->overrun);

	return ret;
}
EXPORT_SYMBOL_GPL(ring_buffer_overrun_cpu);

/**
 * ring_buffer_commit_overrun_cpu - get the number of overruns caused by
 * commits failing due to the buffer wrapping around while there are uncommitted
 * events, such as during an interrupt storm.
 * @buffer: The ring buffer
 * @cpu: The per CPU buffer to get the number of overruns from
 */
unsigned long
ring_buffer_commit_overrun_cpu(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long ret;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return 0;

	cpu_buffer = buffer->buffers[cpu];
	ret = local_read(&cpu_buffer->commit_overrun);

	return ret;
}
EXPORT_SYMBOL_GPL(ring_buffer_commit_overrun_cpu);

/**
 * ring_buffer_dropped_events_cpu - get the number of dropped events caused by
 * the ring buffer filling up (only if RB_FL_OVERWRITE is off).
 * @buffer: The ring buffer
 * @cpu: The per CPU buffer to get the number of overruns from
 */
unsigned long
ring_buffer_dropped_events_cpu(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long ret;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return 0;

	cpu_buffer = buffer->buffers[cpu];
	ret = local_read(&cpu_buffer->dropped_events);

	return ret;
}
EXPORT_SYMBOL_GPL(ring_buffer_dropped_events_cpu);

/**
 * ring_buffer_read_events_cpu - get the number of events successfully read
 * @buffer: The ring buffer
 * @cpu: The per CPU buffer to get the number of events read
 */
unsigned long
ring_buffer_read_events_cpu(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return 0;

	cpu_buffer = buffer->buffers[cpu];
	return cpu_buffer->read;
}
EXPORT_SYMBOL_GPL(ring_buffer_read_events_cpu);

/**
 * ring_buffer_entries - get the number of entries in a buffer
 * @buffer: The ring buffer
 *
 * Returns the total number of entries in the ring buffer
 * (all CPU entries)
 */
unsigned long ring_buffer_entries(struct trace_buffer *buffer)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long entries = 0;
	int cpu;

	/* if you care about this being correct, lock the buffer */
	for_each_buffer_cpu(buffer, cpu) {
		cpu_buffer = buffer->buffers[cpu];
		entries += rb_num_of_entries(cpu_buffer);
	}

	return entries;
}
EXPORT_SYMBOL_GPL(ring_buffer_entries);

/**
 * ring_buffer_overruns - get the number of overruns in buffer
 * @buffer: The ring buffer
 *
 * Returns the total number of overruns in the ring buffer
 * (all CPU entries)
 */
unsigned long ring_buffer_overruns(struct trace_buffer *buffer)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long overruns = 0;
	int cpu;

	/* if you care about this being correct, lock the buffer */
	for_each_buffer_cpu(buffer, cpu) {
		cpu_buffer = buffer->buffers[cpu];
		overruns += local_read(&cpu_buffer->overrun);
	}

	return overruns;
}
EXPORT_SYMBOL_GPL(ring_buffer_overruns);

static void rb_iter_reset(struct ring_buffer_iter *iter)
{
	struct ring_buffer_per_cpu *cpu_buffer = iter->cpu_buffer;

	/* Iterator usage is expected to have record disabled */
	iter->head_page = cpu_buffer->reader_page;
	iter->head = cpu_buffer->reader_page->read;
	iter->next_event = iter->head;

	iter->cache_reader_page = iter->head_page;
	iter->cache_read = cpu_buffer->read;
	iter->cache_pages_removed = cpu_buffer->pages_removed;

	if (iter->head) {
		iter->read_stamp = cpu_buffer->read_stamp;
		iter->page_stamp = cpu_buffer->reader_page->page->time_stamp;
	} else {
		iter->read_stamp = iter->head_page->page->time_stamp;
		iter->page_stamp = iter->read_stamp;
	}
}

/**
 * ring_buffer_iter_reset - reset an iterator
 * @iter: The iterator to reset
 *
 * Resets the iterator, so that it will start from the beginning
 * again.
 */
void ring_buffer_iter_reset(struct ring_buffer_iter *iter)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long flags;

	if (!iter)
		return;

	cpu_buffer = iter->cpu_buffer;

	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);
	rb_iter_reset(iter);
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);
}
EXPORT_SYMBOL_GPL(ring_buffer_iter_reset);

/**
 * ring_buffer_iter_empty - check if an iterator has no more to read
 * @iter: The iterator to check
 */
int ring_buffer_iter_empty(struct ring_buffer_iter *iter)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct buffer_page *reader;
	struct buffer_page *head_page;
	struct buffer_page *commit_page;
	struct buffer_page *curr_commit_page;
	unsigned commit;
	u64 curr_commit_ts;
	u64 commit_ts;

	cpu_buffer = iter->cpu_buffer;
	reader = cpu_buffer->reader_page;
	head_page = cpu_buffer->head_page;
	commit_page = READ_ONCE(cpu_buffer->commit_page);
	commit_ts = commit_page->page->time_stamp;

	/*
	 * When the writer goes across pages, it issues a cmpxchg which
	 * is a mb(), which will synchronize with the rmb here.
	 * (see rb_tail_page_update())
	 */
	smp_rmb();
	commit = rb_page_commit(commit_page);
	/* We want to make sure that the commit page doesn't change */
	smp_rmb();

	/* Make sure commit page didn't change */
	curr_commit_page = READ_ONCE(cpu_buffer->commit_page);
	curr_commit_ts = READ_ONCE(curr_commit_page->page->time_stamp);

	/* If the commit page changed, then there's more data */
	if (curr_commit_page != commit_page ||
	    curr_commit_ts != commit_ts)
		return 0;

	/* Still racy, as it may return a false positive, but that's OK */
	return ((iter->head_page == commit_page && iter->head >= commit) ||
		(iter->head_page == reader && commit_page == head_page &&
		 head_page->read == commit &&
		 iter->head == rb_page_size(cpu_buffer->reader_page)));
}
EXPORT_SYMBOL_GPL(ring_buffer_iter_empty);

static void
rb_update_read_stamp(struct ring_buffer_per_cpu *cpu_buffer,
		     struct ring_buffer_event *event)
{
	u64 delta;

	switch (event->type_len) {
	case RINGBUF_TYPE_PADDING:
		return;

	case RINGBUF_TYPE_TIME_EXTEND:
		delta = rb_event_time_stamp(event);
		cpu_buffer->read_stamp += delta;
		return;

	case RINGBUF_TYPE_TIME_STAMP:
		delta = rb_event_time_stamp(event);
		delta = rb_fix_abs_ts(delta, cpu_buffer->read_stamp);
		cpu_buffer->read_stamp = delta;
		return;

	case RINGBUF_TYPE_DATA:
		cpu_buffer->read_stamp += event->time_delta;
		return;

	default:
		RB_WARN_ON(cpu_buffer, 1);
	}
}

static void
rb_update_iter_read_stamp(struct ring_buffer_iter *iter,
			  struct ring_buffer_event *event)
{
	u64 delta;

	switch (event->type_len) {
	case RINGBUF_TYPE_PADDING:
		return;

	case RINGBUF_TYPE_TIME_EXTEND:
		delta = rb_event_time_stamp(event);
		iter->read_stamp += delta;
		return;

	case RINGBUF_TYPE_TIME_STAMP:
		delta = rb_event_time_stamp(event);
		delta = rb_fix_abs_ts(delta, iter->read_stamp);
		iter->read_stamp = delta;
		return;

	case RINGBUF_TYPE_DATA:
		iter->read_stamp += event->time_delta;
		return;

	default:
		RB_WARN_ON(iter->cpu_buffer, 1);
	}
}

static struct buffer_page *
rb_get_reader_page(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct buffer_page *reader = NULL;
	unsigned long bsize = READ_ONCE(cpu_buffer->buffer->subbuf_size);
	unsigned long overwrite;
	unsigned long flags;
	int nr_loops = 0;
	bool ret;

	local_irq_save(flags);
	arch_spin_lock(&cpu_buffer->lock);

 again:
	/*
	 * This should normally only loop twice. But because the
	 * start of the reader inserts an empty page, it causes
	 * a case where we will loop three times. There should be no
	 * reason to loop four times (that I know of).
	 */
	if (RB_WARN_ON(cpu_buffer, ++nr_loops > 3)) {
		reader = NULL;
		goto out;
	}

	reader = cpu_buffer->reader_page;

	/* If there's more to read, return this page */
	if (cpu_buffer->reader_page->read < rb_page_size(reader))
		goto out;

	/* Never should we have an index greater than the size */
	if (RB_WARN_ON(cpu_buffer,
		       cpu_buffer->reader_page->read > rb_page_size(reader)))
		goto out;

	/* check if we caught up to the tail */
	reader = NULL;
	if (cpu_buffer->commit_page == cpu_buffer->reader_page)
		goto out;

	/* Don't bother swapping if the ring buffer is empty */
	if (rb_num_of_entries(cpu_buffer) == 0)
		goto out;

	/*
	 * Reset the reader page to size zero.
	 */
	local_set(&cpu_buffer->reader_page->write, 0);
	local_set(&cpu_buffer->reader_page->entries, 0);
	local_set(&cpu_buffer->reader_page->page->commit, 0);
	cpu_buffer->reader_page->real_end = 0;

 spin:
	/*
	 * Splice the empty reader page into the list around the head.
	 */
	reader = rb_set_head_page(cpu_buffer);
	if (!reader)
		goto out;
	cpu_buffer->reader_page->list.next = rb_list_head(reader->list.next);
	cpu_buffer->reader_page->list.prev = reader->list.prev;

	/*
	 * cpu_buffer->pages just needs to point to the buffer, it
	 *  has no specific buffer page to point to. Lets move it out
	 *  of our way so we don't accidentally swap it.
	 */
	cpu_buffer->pages = reader->list.prev;

	/* The reader page will be pointing to the new head */
	rb_set_list_to_head(&cpu_buffer->reader_page->list);

	/*
	 * We want to make sure we read the overruns after we set up our
	 * pointers to the next object. The writer side does a
	 * cmpxchg to cross pages which acts as the mb on the writer
	 * side. Note, the reader will constantly fail the swap
	 * while the writer is updating the pointers, so this
	 * guarantees that the overwrite recorded here is the one we
	 * want to compare with the last_overrun.
	 */
	smp_mb();
	overwrite = local_read(&(cpu_buffer->overrun));

	/*
	 * Here's the tricky part.
	 *
	 * We need to move the pointer past the header page.
	 * But we can only do that if a writer is not currently
	 * moving it. The page before the header page has the
	 * flag bit '1' set if it is pointing to the page we want.
	 * but if the writer is in the process of moving it
	 * than it will be '2' or already moved '0'.
	 */

	ret = rb_head_page_replace(reader, cpu_buffer->reader_page);

	/*
	 * If we did not convert it, then we must try again.
	 */
	if (!ret)
		goto spin;

	if (cpu_buffer->ring_meta)
		rb_update_meta_reader(cpu_buffer, reader);

	/*
	 * Yay! We succeeded in replacing the page.
	 *
	 * Now make the new head point back to the reader page.
	 */
	rb_list_head(reader->list.next)->prev = &cpu_buffer->reader_page->list;
	rb_inc_page(&cpu_buffer->head_page);

	cpu_buffer->cnt++;
	local_inc(&cpu_buffer->pages_read);

	/* Finally update the reader page to the new head */
	cpu_buffer->reader_page = reader;
	cpu_buffer->reader_page->read = 0;

	if (overwrite != cpu_buffer->last_overrun) {
		cpu_buffer->lost_events = overwrite - cpu_buffer->last_overrun;
		cpu_buffer->last_overrun = overwrite;
	}

	goto again;

 out:
	/* Update the read_stamp on the first event */
	if (reader && reader->read == 0)
		cpu_buffer->read_stamp = reader->page->time_stamp;

	arch_spin_unlock(&cpu_buffer->lock);
	local_irq_restore(flags);

	/*
	 * The writer has preempt disable, wait for it. But not forever
	 * Although, 1 second is pretty much "forever"
	 */
#define USECS_WAIT	1000000
        for (nr_loops = 0; nr_loops < USECS_WAIT; nr_loops++) {
		/* If the write is past the end of page, a writer is still updating it */
		if (likely(!reader || rb_page_write(reader) <= bsize))
			break;

		udelay(1);

		/* Get the latest version of the reader write value */
		smp_rmb();
	}

	/* The writer is not moving forward? Something is wrong */
	if (RB_WARN_ON(cpu_buffer, nr_loops == USECS_WAIT))
		reader = NULL;

	/*
	 * Make sure we see any padding after the write update
	 * (see rb_reset_tail()).
	 *
	 * In addition, a writer may be writing on the reader page
	 * if the page has not been fully filled, so the read barrier
	 * is also needed to make sure we see the content of what is
	 * committed by the writer (see rb_set_commit_to_write()).
	 */
	smp_rmb();


	return reader;
}

static void rb_advance_reader(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct ring_buffer_event *event;
	struct buffer_page *reader;
	unsigned length;

	reader = rb_get_reader_page(cpu_buffer);

	/* This function should not be called when buffer is empty */
	if (RB_WARN_ON(cpu_buffer, !reader))
		return;

	event = rb_reader_event(cpu_buffer);

	if (event->type_len <= RINGBUF_TYPE_DATA_TYPE_LEN_MAX)
		cpu_buffer->read++;

	rb_update_read_stamp(cpu_buffer, event);

	length = rb_event_length(event);
	cpu_buffer->reader_page->read += length;
	cpu_buffer->read_bytes += length;
}

static void rb_advance_iter(struct ring_buffer_iter *iter)
{
	struct ring_buffer_per_cpu *cpu_buffer;

	cpu_buffer = iter->cpu_buffer;

	/* If head == next_event then we need to jump to the next event */
	if (iter->head == iter->next_event) {
		/* If the event gets overwritten again, there's nothing to do */
		if (rb_iter_head_event(iter) == NULL)
			return;
	}

	iter->head = iter->next_event;

	/*
	 * Check if we are at the end of the buffer.
	 */
	if (iter->next_event >= rb_page_size(iter->head_page)) {
		/* discarded commits can make the page empty */
		if (iter->head_page == cpu_buffer->commit_page)
			return;
		rb_inc_iter(iter);
		return;
	}

	rb_update_iter_read_stamp(iter, iter->event);
}

static int rb_lost_events(struct ring_buffer_per_cpu *cpu_buffer)
{
	return cpu_buffer->lost_events;
}

static struct ring_buffer_event *
rb_buffer_peek(struct ring_buffer_per_cpu *cpu_buffer, u64 *ts,
	       unsigned long *lost_events)
{
	struct ring_buffer_event *event;
	struct buffer_page *reader;
	int nr_loops = 0;

	if (ts)
		*ts = 0;
 again:
	/*
	 * We repeat when a time extend is encountered.
	 * Since the time extend is always attached to a data event,
	 * we should never loop more than once.
	 * (We never hit the following condition more than twice).
	 */
	if (RB_WARN_ON(cpu_buffer, ++nr_loops > 2))
		return NULL;

	reader = rb_get_reader_page(cpu_buffer);
	if (!reader)
		return NULL;

	event = rb_reader_event(cpu_buffer);

	switch (event->type_len) {
	case RINGBUF_TYPE_PADDING:
		if (rb_null_event(event))
			RB_WARN_ON(cpu_buffer, 1);
		/*
		 * Because the writer could be discarding every
		 * event it creates (which would probably be bad)
		 * if we were to go back to "again" then we may never
		 * catch up, and will trigger the warn on, or lock
		 * the box. Return the padding, and we will release
		 * the current locks, and try again.
		 */
		return event;

	case RINGBUF_TYPE_TIME_EXTEND:
		/* Internal data, OK to advance */
		rb_advance_reader(cpu_buffer);
		goto again;

	case RINGBUF_TYPE_TIME_STAMP:
		if (ts) {
			*ts = rb_event_time_stamp(event);
			*ts = rb_fix_abs_ts(*ts, reader->page->time_stamp);
			ring_buffer_normalize_time_stamp(cpu_buffer->buffer,
							 cpu_buffer->cpu, ts);
		}
		/* Internal data, OK to advance */
		rb_advance_reader(cpu_buffer);
		goto again;

	case RINGBUF_TYPE_DATA:
		if (ts && !(*ts)) {
			*ts = cpu_buffer->read_stamp + event->time_delta;
			ring_buffer_normalize_time_stamp(cpu_buffer->buffer,
							 cpu_buffer->cpu, ts);
		}
		if (lost_events)
			*lost_events = rb_lost_events(cpu_buffer);
		return event;

	default:
		RB_WARN_ON(cpu_buffer, 1);
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(ring_buffer_peek);

static struct ring_buffer_event *
rb_iter_peek(struct ring_buffer_iter *iter, u64 *ts)
{
	struct trace_buffer *buffer;
	struct ring_buffer_per_cpu *cpu_buffer;
	struct ring_buffer_event *event;
	int nr_loops = 0;

	if (ts)
		*ts = 0;

	cpu_buffer = iter->cpu_buffer;
	buffer = cpu_buffer->buffer;

	/*
	 * Check if someone performed a consuming read to the buffer
	 * or removed some pages from the buffer. In these cases,
	 * iterator was invalidated and we need to reset it.
	 */
	if (unlikely(iter->cache_read != cpu_buffer->read ||
		     iter->cache_reader_page != cpu_buffer->reader_page ||
		     iter->cache_pages_removed != cpu_buffer->pages_removed))
		rb_iter_reset(iter);

 again:
	if (ring_buffer_iter_empty(iter))
		return NULL;

	/*
	 * As the writer can mess with what the iterator is trying
	 * to read, just give up if we fail to get an event after
	 * three tries. The iterator is not as reliable when reading
	 * the ring buffer with an active write as the consumer is.
	 * Do not warn if the three failures is reached.
	 */
	if (++nr_loops > 3)
		return NULL;

	if (rb_per_cpu_empty(cpu_buffer))
		return NULL;

	if (iter->head >= rb_page_size(iter->head_page)) {
		rb_inc_iter(iter);
		goto again;
	}

	event = rb_iter_head_event(iter);
	if (!event)
		goto again;

	switch (event->type_len) {
	case RINGBUF_TYPE_PADDING:
		if (rb_null_event(event)) {
			rb_inc_iter(iter);
			goto again;
		}
		rb_advance_iter(iter);
		return event;

	case RINGBUF_TYPE_TIME_EXTEND:
		/* Internal data, OK to advance */
		rb_advance_iter(iter);
		goto again;

	case RINGBUF_TYPE_TIME_STAMP:
		if (ts) {
			*ts = rb_event_time_stamp(event);
			*ts = rb_fix_abs_ts(*ts, iter->head_page->page->time_stamp);
			ring_buffer_normalize_time_stamp(cpu_buffer->buffer,
							 cpu_buffer->cpu, ts);
		}
		/* Internal data, OK to advance */
		rb_advance_iter(iter);
		goto again;

	case RINGBUF_TYPE_DATA:
		if (ts && !(*ts)) {
			*ts = iter->read_stamp + event->time_delta;
			ring_buffer_normalize_time_stamp(buffer,
							 cpu_buffer->cpu, ts);
		}
		return event;

	default:
		RB_WARN_ON(cpu_buffer, 1);
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(ring_buffer_iter_peek);

static inline bool rb_reader_lock(struct ring_buffer_per_cpu *cpu_buffer)
{
	if (likely(!in_nmi())) {
		raw_spin_lock(&cpu_buffer->reader_lock);
		return true;
	}

	/*
	 * If an NMI die dumps out the content of the ring buffer
	 * trylock must be used to prevent a deadlock if the NMI
	 * preempted a task that holds the ring buffer locks. If
	 * we get the lock then all is fine, if not, then continue
	 * to do the read, but this can corrupt the ring buffer,
	 * so it must be permanently disabled from future writes.
	 * Reading from NMI is a oneshot deal.
	 */
	if (raw_spin_trylock(&cpu_buffer->reader_lock))
		return true;

	/* Continue without locking, but disable the ring buffer */
	atomic_inc(&cpu_buffer->record_disabled);
	return false;
}

static inline void
rb_reader_unlock(struct ring_buffer_per_cpu *cpu_buffer, bool locked)
{
	if (likely(locked))
		raw_spin_unlock(&cpu_buffer->reader_lock);
}

/**
 * ring_buffer_peek - peek at the next event to be read
 * @buffer: The ring buffer to read
 * @cpu: The cpu to peak at
 * @ts: The timestamp counter of this event.
 * @lost_events: a variable to store if events were lost (may be NULL)
 *
 * This will return the event that will be read next, but does
 * not consume the data.
 */
struct ring_buffer_event *
ring_buffer_peek(struct trace_buffer *buffer, int cpu, u64 *ts,
		 unsigned long *lost_events)
{
	struct ring_buffer_per_cpu *cpu_buffer = buffer->buffers[cpu];
	struct ring_buffer_event *event;
	unsigned long flags;
	bool dolock;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return NULL;

 again:
	local_irq_save(flags);
	dolock = rb_reader_lock(cpu_buffer);
	event = rb_buffer_peek(cpu_buffer, ts, lost_events);
	if (event && event->type_len == RINGBUF_TYPE_PADDING)
		rb_advance_reader(cpu_buffer);
	rb_reader_unlock(cpu_buffer, dolock);
	local_irq_restore(flags);

	if (event && event->type_len == RINGBUF_TYPE_PADDING)
		goto again;

	return event;
}

/** ring_buffer_iter_dropped - report if there are dropped events
 * @iter: The ring buffer iterator
 *
 * Returns true if there was dropped events since the last peek.
 */
bool ring_buffer_iter_dropped(struct ring_buffer_iter *iter)
{
	bool ret = iter->missed_events != 0;

	iter->missed_events = 0;
	return ret;
}
EXPORT_SYMBOL_GPL(ring_buffer_iter_dropped);

/**
 * ring_buffer_iter_peek - peek at the next event to be read
 * @iter: The ring buffer iterator
 * @ts: The timestamp counter of this event.
 *
 * This will return the event that will be read next, but does
 * not increment the iterator.
 */
struct ring_buffer_event *
ring_buffer_iter_peek(struct ring_buffer_iter *iter, u64 *ts)
{
	struct ring_buffer_per_cpu *cpu_buffer = iter->cpu_buffer;
	struct ring_buffer_event *event;
	unsigned long flags;

 again:
	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);
	event = rb_iter_peek(iter, ts);
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);

	if (event && event->type_len == RINGBUF_TYPE_PADDING)
		goto again;

	return event;
}

/**
 * ring_buffer_consume - return an event and consume it
 * @buffer: The ring buffer to get the next event from
 * @cpu: the cpu to read the buffer from
 * @ts: a variable to store the timestamp (may be NULL)
 * @lost_events: a variable to store if events were lost (may be NULL)
 *
 * Returns the next event in the ring buffer, and that event is consumed.
 * Meaning, that sequential reads will keep returning a different event,
 * and eventually empty the ring buffer if the producer is slower.
 */
struct ring_buffer_event *
ring_buffer_consume(struct trace_buffer *buffer, int cpu, u64 *ts,
		    unsigned long *lost_events)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct ring_buffer_event *event = NULL;
	unsigned long flags;
	bool dolock;

 again:
	/* might be called in atomic */
	preempt_disable();

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		goto out;

	cpu_buffer = buffer->buffers[cpu];
	local_irq_save(flags);
	dolock = rb_reader_lock(cpu_buffer);

	event = rb_buffer_peek(cpu_buffer, ts, lost_events);
	if (event) {
		cpu_buffer->lost_events = 0;
		rb_advance_reader(cpu_buffer);
	}

	rb_reader_unlock(cpu_buffer, dolock);
	local_irq_restore(flags);

 out:
	preempt_enable();

	if (event && event->type_len == RINGBUF_TYPE_PADDING)
		goto again;

	return event;
}
EXPORT_SYMBOL_GPL(ring_buffer_consume);

/**
 * ring_buffer_read_prepare - Prepare for a non consuming read of the buffer
 * @buffer: The ring buffer to read from
 * @cpu: The cpu buffer to iterate over
 * @flags: gfp flags to use for memory allocation
 *
 * This performs the initial preparations necessary to iterate
 * through the buffer.  Memory is allocated, buffer resizing
 * is disabled, and the iterator pointer is returned to the caller.
 *
 * After a sequence of ring_buffer_read_prepare calls, the user is
 * expected to make at least one call to ring_buffer_read_prepare_sync.
 * Afterwards, ring_buffer_read_start is invoked to get things going
 * for real.
 *
 * This overall must be paired with ring_buffer_read_finish.
 */
struct ring_buffer_iter *
ring_buffer_read_prepare(struct trace_buffer *buffer, int cpu, gfp_t flags)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct ring_buffer_iter *iter;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return NULL;

	iter = kzalloc(sizeof(*iter), flags);
	if (!iter)
		return NULL;

	/* Holds the entire event: data and meta data */
	iter->event_size = buffer->subbuf_size;
	iter->event = kmalloc(iter->event_size, flags);
	if (!iter->event) {
		kfree(iter);
		return NULL;
	}

	cpu_buffer = buffer->buffers[cpu];

	iter->cpu_buffer = cpu_buffer;

	atomic_inc(&cpu_buffer->resize_disabled);

	return iter;
}
EXPORT_SYMBOL_GPL(ring_buffer_read_prepare);

/**
 * ring_buffer_read_prepare_sync - Synchronize a set of prepare calls
 *
 * All previously invoked ring_buffer_read_prepare calls to prepare
 * iterators will be synchronized.  Afterwards, read_buffer_read_start
 * calls on those iterators are allowed.
 */
void
ring_buffer_read_prepare_sync(void)
{
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(ring_buffer_read_prepare_sync);

/**
 * ring_buffer_read_start - start a non consuming read of the buffer
 * @iter: The iterator returned by ring_buffer_read_prepare
 *
 * This finalizes the startup of an iteration through the buffer.
 * The iterator comes from a call to ring_buffer_read_prepare and
 * an intervening ring_buffer_read_prepare_sync must have been
 * performed.
 *
 * Must be paired with ring_buffer_read_finish.
 */
void
ring_buffer_read_start(struct ring_buffer_iter *iter)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long flags;

	if (!iter)
		return;

	cpu_buffer = iter->cpu_buffer;

	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);
	arch_spin_lock(&cpu_buffer->lock);
	rb_iter_reset(iter);
	arch_spin_unlock(&cpu_buffer->lock);
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);
}
EXPORT_SYMBOL_GPL(ring_buffer_read_start);

/**
 * ring_buffer_read_finish - finish reading the iterator of the buffer
 * @iter: The iterator retrieved by ring_buffer_start
 *
 * This re-enables resizing of the buffer, and frees the iterator.
 */
void
ring_buffer_read_finish(struct ring_buffer_iter *iter)
{
	struct ring_buffer_per_cpu *cpu_buffer = iter->cpu_buffer;

	/* Use this opportunity to check the integrity of the ring buffer. */
	rb_check_pages(cpu_buffer);

	atomic_dec(&cpu_buffer->resize_disabled);
	kfree(iter->event);
	kfree(iter);
}
EXPORT_SYMBOL_GPL(ring_buffer_read_finish);

/**
 * ring_buffer_iter_advance - advance the iterator to the next location
 * @iter: The ring buffer iterator
 *
 * Move the location of the iterator such that the next read will
 * be the next location of the iterator.
 */
void ring_buffer_iter_advance(struct ring_buffer_iter *iter)
{
	struct ring_buffer_per_cpu *cpu_buffer = iter->cpu_buffer;
	unsigned long flags;

	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);

	rb_advance_iter(iter);

	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);
}
EXPORT_SYMBOL_GPL(ring_buffer_iter_advance);

/**
 * ring_buffer_size - return the size of the ring buffer (in bytes)
 * @buffer: The ring buffer.
 * @cpu: The CPU to get ring buffer size from.
 */
unsigned long ring_buffer_size(struct trace_buffer *buffer, int cpu)
{
	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return 0;

	return buffer->subbuf_size * buffer->buffers[cpu]->nr_pages;
}
EXPORT_SYMBOL_GPL(ring_buffer_size);

/**
 * ring_buffer_max_event_size - return the max data size of an event
 * @buffer: The ring buffer.
 *
 * Returns the maximum size an event can be.
 */
unsigned long ring_buffer_max_event_size(struct trace_buffer *buffer)
{
	/* If abs timestamp is requested, events have a timestamp too */
	if (ring_buffer_time_stamp_abs(buffer))
		return buffer->max_data_size - RB_LEN_TIME_EXTEND;
	return buffer->max_data_size;
}
EXPORT_SYMBOL_GPL(ring_buffer_max_event_size);

static void rb_clear_buffer_page(struct buffer_page *page)
{
	local_set(&page->write, 0);
	local_set(&page->entries, 0);
	rb_init_page(page->page);
	page->read = 0;
}

static void rb_update_meta_page(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct trace_buffer_meta *meta = cpu_buffer->meta_page;

	if (!meta)
		return;

	meta->reader.read = cpu_buffer->reader_page->read;
	meta->reader.id = cpu_buffer->reader_page->id;
	meta->reader.lost_events = cpu_buffer->lost_events;

	meta->entries = local_read(&cpu_buffer->entries);
	meta->overrun = local_read(&cpu_buffer->overrun);
	meta->read = cpu_buffer->read;

	/* Some archs do not have data cache coherency between kernel and user-space */
	flush_dcache_folio(virt_to_folio(cpu_buffer->meta_page));
}

static void
rb_reset_cpu(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct buffer_page *page;

	rb_head_page_deactivate(cpu_buffer);

	cpu_buffer->head_page
		= list_entry(cpu_buffer->pages, struct buffer_page, list);
	rb_clear_buffer_page(cpu_buffer->head_page);
	list_for_each_entry(page, cpu_buffer->pages, list) {
		rb_clear_buffer_page(page);
	}

	cpu_buffer->tail_page = cpu_buffer->head_page;
	cpu_buffer->commit_page = cpu_buffer->head_page;

	INIT_LIST_HEAD(&cpu_buffer->reader_page->list);
	INIT_LIST_HEAD(&cpu_buffer->new_pages);
	rb_clear_buffer_page(cpu_buffer->reader_page);

	local_set(&cpu_buffer->entries_bytes, 0);
	local_set(&cpu_buffer->overrun, 0);
	local_set(&cpu_buffer->commit_overrun, 0);
	local_set(&cpu_buffer->dropped_events, 0);
	local_set(&cpu_buffer->entries, 0);
	local_set(&cpu_buffer->committing, 0);
	local_set(&cpu_buffer->commits, 0);
	local_set(&cpu_buffer->pages_touched, 0);
	local_set(&cpu_buffer->pages_lost, 0);
	local_set(&cpu_buffer->pages_read, 0);
	cpu_buffer->last_pages_touch = 0;
	cpu_buffer->shortest_full = 0;
	cpu_buffer->read = 0;
	cpu_buffer->read_bytes = 0;

	rb_time_set(&cpu_buffer->write_stamp, 0);
	rb_time_set(&cpu_buffer->before_stamp, 0);

	memset(cpu_buffer->event_stamp, 0, sizeof(cpu_buffer->event_stamp));

	cpu_buffer->lost_events = 0;
	cpu_buffer->last_overrun = 0;

	rb_head_page_activate(cpu_buffer);
	cpu_buffer->pages_removed = 0;

	if (cpu_buffer->mapped) {
		rb_update_meta_page(cpu_buffer);
		if (cpu_buffer->ring_meta) {
			struct ring_buffer_meta *meta = cpu_buffer->ring_meta;
			meta->commit_buffer = meta->head_buffer;
		}
	}
}

/* Must have disabled the cpu buffer then done a synchronize_rcu */
static void reset_disabled_cpu_buffer(struct ring_buffer_per_cpu *cpu_buffer)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);

	if (RB_WARN_ON(cpu_buffer, local_read(&cpu_buffer->committing)))
		goto out;

	arch_spin_lock(&cpu_buffer->lock);

	rb_reset_cpu(cpu_buffer);

	arch_spin_unlock(&cpu_buffer->lock);

 out:
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);
}

/**
 * ring_buffer_reset_cpu - reset a ring buffer per CPU buffer
 * @buffer: The ring buffer to reset a per cpu buffer of
 * @cpu: The CPU buffer to be reset
 */
void ring_buffer_reset_cpu(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer = buffer->buffers[cpu];
	struct ring_buffer_meta *meta;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return;

	/* prevent another thread from changing buffer sizes */
	mutex_lock(&buffer->mutex);

	atomic_inc(&cpu_buffer->resize_disabled);
	atomic_inc(&cpu_buffer->record_disabled);

	/* Make sure all commits have finished */
	synchronize_rcu();

	reset_disabled_cpu_buffer(cpu_buffer);

	atomic_dec(&cpu_buffer->record_disabled);
	atomic_dec(&cpu_buffer->resize_disabled);

	/* Make sure persistent meta now uses this buffer's addresses */
	meta = rb_range_meta(buffer, 0, cpu_buffer->cpu);
	if (meta)
		rb_meta_init_text_addr(meta);

	mutex_unlock(&buffer->mutex);
}
EXPORT_SYMBOL_GPL(ring_buffer_reset_cpu);

/* Flag to ensure proper resetting of atomic variables */
#define RESET_BIT	(1 << 30)

/**
 * ring_buffer_reset_online_cpus - reset a ring buffer per CPU buffer
 * @buffer: The ring buffer to reset a per cpu buffer of
 */
void ring_buffer_reset_online_cpus(struct trace_buffer *buffer)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct ring_buffer_meta *meta;
	int cpu;

	/* prevent another thread from changing buffer sizes */
	mutex_lock(&buffer->mutex);

	for_each_online_buffer_cpu(buffer, cpu) {
		cpu_buffer = buffer->buffers[cpu];

		atomic_add(RESET_BIT, &cpu_buffer->resize_disabled);
		atomic_inc(&cpu_buffer->record_disabled);
	}

	/* Make sure all commits have finished */
	synchronize_rcu();

	for_each_buffer_cpu(buffer, cpu) {
		cpu_buffer = buffer->buffers[cpu];

		/*
		 * If a CPU came online during the synchronize_rcu(), then
		 * ignore it.
		 */
		if (!(atomic_read(&cpu_buffer->resize_disabled) & RESET_BIT))
			continue;

		reset_disabled_cpu_buffer(cpu_buffer);

		/* Make sure persistent meta now uses this buffer's addresses */
		meta = rb_range_meta(buffer, 0, cpu_buffer->cpu);
		if (meta)
			rb_meta_init_text_addr(meta);

		atomic_dec(&cpu_buffer->record_disabled);
		atomic_sub(RESET_BIT, &cpu_buffer->resize_disabled);
	}

	mutex_unlock(&buffer->mutex);
}

/**
 * ring_buffer_reset - reset a ring buffer
 * @buffer: The ring buffer to reset all cpu buffers
 */
void ring_buffer_reset(struct trace_buffer *buffer)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	int cpu;

	/* prevent another thread from changing buffer sizes */
	mutex_lock(&buffer->mutex);

	for_each_buffer_cpu(buffer, cpu) {
		cpu_buffer = buffer->buffers[cpu];

		atomic_inc(&cpu_buffer->resize_disabled);
		atomic_inc(&cpu_buffer->record_disabled);
	}

	/* Make sure all commits have finished */
	synchronize_rcu();

	for_each_buffer_cpu(buffer, cpu) {
		cpu_buffer = buffer->buffers[cpu];

		reset_disabled_cpu_buffer(cpu_buffer);

		atomic_dec(&cpu_buffer->record_disabled);
		atomic_dec(&cpu_buffer->resize_disabled);
	}

	mutex_unlock(&buffer->mutex);
}
EXPORT_SYMBOL_GPL(ring_buffer_reset);

/**
 * ring_buffer_empty - is the ring buffer empty?
 * @buffer: The ring buffer to test
 */
bool ring_buffer_empty(struct trace_buffer *buffer)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long flags;
	bool dolock;
	bool ret;
	int cpu;

	/* yes this is racy, but if you don't like the race, lock the buffer */
	for_each_buffer_cpu(buffer, cpu) {
		cpu_buffer = buffer->buffers[cpu];
		local_irq_save(flags);
		dolock = rb_reader_lock(cpu_buffer);
		ret = rb_per_cpu_empty(cpu_buffer);
		rb_reader_unlock(cpu_buffer, dolock);
		local_irq_restore(flags);

		if (!ret)
			return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(ring_buffer_empty);

/**
 * ring_buffer_empty_cpu - is a cpu buffer of a ring buffer empty?
 * @buffer: The ring buffer
 * @cpu: The CPU buffer to test
 */
bool ring_buffer_empty_cpu(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long flags;
	bool dolock;
	bool ret;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return true;

	cpu_buffer = buffer->buffers[cpu];
	local_irq_save(flags);
	dolock = rb_reader_lock(cpu_buffer);
	ret = rb_per_cpu_empty(cpu_buffer);
	rb_reader_unlock(cpu_buffer, dolock);
	local_irq_restore(flags);

	return ret;
}
EXPORT_SYMBOL_GPL(ring_buffer_empty_cpu);

#ifdef CONFIG_RING_BUFFER_ALLOW_SWAP
/**
 * ring_buffer_swap_cpu - swap a CPU buffer between two ring buffers
 * @buffer_a: One buffer to swap with
 * @buffer_b: The other buffer to swap with
 * @cpu: the CPU of the buffers to swap
 *
 * This function is useful for tracers that want to take a "snapshot"
 * of a CPU buffer and has another back up buffer lying around.
 * it is expected that the tracer handles the cpu buffer not being
 * used at the moment.
 */
int ring_buffer_swap_cpu(struct trace_buffer *buffer_a,
			 struct trace_buffer *buffer_b, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer_a;
	struct ring_buffer_per_cpu *cpu_buffer_b;
	int ret = -EINVAL;

	if (!cpumask_test_cpu(cpu, buffer_a->cpumask) ||
	    !cpumask_test_cpu(cpu, buffer_b->cpumask))
		goto out;

	cpu_buffer_a = buffer_a->buffers[cpu];
	cpu_buffer_b = buffer_b->buffers[cpu];

	/* It's up to the callers to not try to swap mapped buffers */
	if (WARN_ON_ONCE(cpu_buffer_a->mapped || cpu_buffer_b->mapped)) {
		ret = -EBUSY;
		goto out;
	}

	/* At least make sure the two buffers are somewhat the same */
	if (cpu_buffer_a->nr_pages != cpu_buffer_b->nr_pages)
		goto out;

	if (buffer_a->subbuf_order != buffer_b->subbuf_order)
		goto out;

	ret = -EAGAIN;

	if (atomic_read(&buffer_a->record_disabled))
		goto out;

	if (atomic_read(&buffer_b->record_disabled))
		goto out;

	if (atomic_read(&cpu_buffer_a->record_disabled))
		goto out;

	if (atomic_read(&cpu_buffer_b->record_disabled))
		goto out;

	/*
	 * We can't do a synchronize_rcu here because this
	 * function can be called in atomic context.
	 * Normally this will be called from the same CPU as cpu.
	 * If not it's up to the caller to protect this.
	 */
	atomic_inc(&cpu_buffer_a->record_disabled);
	atomic_inc(&cpu_buffer_b->record_disabled);

	ret = -EBUSY;
	if (local_read(&cpu_buffer_a->committing))
		goto out_dec;
	if (local_read(&cpu_buffer_b->committing))
		goto out_dec;

	/*
	 * When resize is in progress, we cannot swap it because
	 * it will mess the state of the cpu buffer.
	 */
	if (atomic_read(&buffer_a->resizing))
		goto out_dec;
	if (atomic_read(&buffer_b->resizing))
		goto out_dec;

	buffer_a->buffers[cpu] = cpu_buffer_b;
	buffer_b->buffers[cpu] = cpu_buffer_a;

	cpu_buffer_b->buffer = buffer_a;
	cpu_buffer_a->buffer = buffer_b;

	ret = 0;

out_dec:
	atomic_dec(&cpu_buffer_a->record_disabled);
	atomic_dec(&cpu_buffer_b->record_disabled);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(ring_buffer_swap_cpu);
#endif /* CONFIG_RING_BUFFER_ALLOW_SWAP */

/**
 * ring_buffer_alloc_read_page - allocate a page to read from buffer
 * @buffer: the buffer to allocate for.
 * @cpu: the cpu buffer to allocate.
 *
 * This function is used in conjunction with ring_buffer_read_page.
 * When reading a full page from the ring buffer, these functions
 * can be used to speed up the process. The calling function should
 * allocate a few pages first with this function. Then when it
 * needs to get pages from the ring buffer, it passes the result
 * of this function into ring_buffer_read_page, which will swap
 * the page that was allocated, with the read page of the buffer.
 *
 * Returns:
 *  The page allocated, or ERR_PTR
 */
struct buffer_data_read_page *
ring_buffer_alloc_read_page(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct buffer_data_read_page *bpage = NULL;
	unsigned long flags;
	struct page *page;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return ERR_PTR(-ENODEV);

	bpage = kzalloc(sizeof(*bpage), GFP_KERNEL);
	if (!bpage)
		return ERR_PTR(-ENOMEM);

	bpage->order = buffer->subbuf_order;
	cpu_buffer = buffer->buffers[cpu];
	local_irq_save(flags);
	arch_spin_lock(&cpu_buffer->lock);

	if (cpu_buffer->free_page) {
		bpage->data = cpu_buffer->free_page;
		cpu_buffer->free_page = NULL;
	}

	arch_spin_unlock(&cpu_buffer->lock);
	local_irq_restore(flags);

	if (bpage->data)
		goto out;

	page = alloc_pages_node(cpu_to_node(cpu),
				GFP_KERNEL | __GFP_NORETRY | __GFP_COMP | __GFP_ZERO,
				cpu_buffer->buffer->subbuf_order);
	if (!page) {
		kfree(bpage);
		return ERR_PTR(-ENOMEM);
	}

	bpage->data = page_address(page);

 out:
	rb_init_page(bpage->data);

	return bpage;
}
EXPORT_SYMBOL_GPL(ring_buffer_alloc_read_page);

/**
 * ring_buffer_free_read_page - free an allocated read page
 * @buffer: the buffer the page was allocate for
 * @cpu: the cpu buffer the page came from
 * @data_page: the page to free
 *
 * Free a page allocated from ring_buffer_alloc_read_page.
 */
void ring_buffer_free_read_page(struct trace_buffer *buffer, int cpu,
				struct buffer_data_read_page *data_page)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct buffer_data_page *bpage = data_page->data;
	struct page *page = virt_to_page(bpage);
	unsigned long flags;

	if (!buffer || !buffer->buffers || !buffer->buffers[cpu])
		return;

	cpu_buffer = buffer->buffers[cpu];

	/*
	 * If the page is still in use someplace else, or order of the page
	 * is different from the subbuffer order of the buffer -
	 * we can't reuse it
	 */
	if (page_ref_count(page) > 1 || data_page->order != buffer->subbuf_order)
		goto out;

	local_irq_save(flags);
	arch_spin_lock(&cpu_buffer->lock);

	if (!cpu_buffer->free_page) {
		cpu_buffer->free_page = bpage;
		bpage = NULL;
	}

	arch_spin_unlock(&cpu_buffer->lock);
	local_irq_restore(flags);

 out:
	free_pages((unsigned long)bpage, data_page->order);
	kfree(data_page);
}
EXPORT_SYMBOL_GPL(ring_buffer_free_read_page);

/**
 * ring_buffer_read_page - extract a page from the ring buffer
 * @buffer: buffer to extract from
 * @data_page: the page to use allocated from ring_buffer_alloc_read_page
 * @len: amount to extract
 * @cpu: the cpu of the buffer to extract
 * @full: should the extraction only happen when the page is full.
 *
 * This function will pull out a page from the ring buffer and consume it.
 * @data_page must be the address of the variable that was returned
 * from ring_buffer_alloc_read_page. This is because the page might be used
 * to swap with a page in the ring buffer.
 *
 * for example:
 *	rpage = ring_buffer_alloc_read_page(buffer, cpu);
 *	if (IS_ERR(rpage))
 *		return PTR_ERR(rpage);
 *	ret = ring_buffer_read_page(buffer, rpage, len, cpu, 0);
 *	if (ret >= 0)
 *		process_page(ring_buffer_read_page_data(rpage), ret);
 *	ring_buffer_free_read_page(buffer, cpu, rpage);
 *
 * When @full is set, the function will not return true unless
 * the writer is off the reader page.
 *
 * Note: it is up to the calling functions to handle sleeps and wakeups.
 *  The ring buffer can be used anywhere in the kernel and can not
 *  blindly call wake_up. The layer that uses the ring buffer must be
 *  responsible for that.
 *
 * Returns:
 *  >=0 if data has been transferred, returns the offset of consumed data.
 *  <0 if no data has been transferred.
 */
int ring_buffer_read_page(struct trace_buffer *buffer,
			  struct buffer_data_read_page *data_page,
			  size_t len, int cpu, int full)
{
	struct ring_buffer_per_cpu *cpu_buffer = buffer->buffers[cpu];
	struct ring_buffer_event *event;
	struct buffer_data_page *bpage;
	struct buffer_page *reader;
	unsigned long missed_events;
	unsigned long flags;
	unsigned int commit;
	unsigned int read;
	u64 save_timestamp;
	int ret = -1;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		goto out;

	/*
	 * If len is not big enough to hold the page header, then
	 * we can not copy anything.
	 */
	if (len <= BUF_PAGE_HDR_SIZE)
		goto out;

	len -= BUF_PAGE_HDR_SIZE;

	if (!data_page || !data_page->data)
		goto out;
	if (data_page->order != buffer->subbuf_order)
		goto out;

	bpage = data_page->data;
	if (!bpage)
		goto out;

	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);

	reader = rb_get_reader_page(cpu_buffer);
	if (!reader)
		goto out_unlock;

	event = rb_reader_event(cpu_buffer);

	read = reader->read;
	commit = rb_page_size(reader);

	/* Check if any events were dropped */
	missed_events = cpu_buffer->lost_events;

	/*
	 * If this page has been partially read or
	 * if len is not big enough to read the rest of the page or
	 * a writer is still on the page, then
	 * we must copy the data from the page to the buffer.
	 * Otherwise, we can simply swap the page with the one passed in.
	 */
	if (read || (len < (commit - read)) ||
	    cpu_buffer->reader_page == cpu_buffer->commit_page ||
	    cpu_buffer->mapped) {
		struct buffer_data_page *rpage = cpu_buffer->reader_page->page;
		unsigned int rpos = read;
		unsigned int pos = 0;
		unsigned int size;

		/*
		 * If a full page is expected, this can still be returned
		 * if there's been a previous partial read and the
		 * rest of the page can be read and the commit page is off
		 * the reader page.
		 */
		if (full &&
		    (!read || (len < (commit - read)) ||
		     cpu_buffer->reader_page == cpu_buffer->commit_page))
			goto out_unlock;

		if (len > (commit - read))
			len = (commit - read);

		/* Always keep the time extend and data together */
		size = rb_event_ts_length(event);

		if (len < size)
			goto out_unlock;

		/* save the current timestamp, since the user will need it */
		save_timestamp = cpu_buffer->read_stamp;

		/* Need to copy one event at a time */
		do {
			/* We need the size of one event, because
			 * rb_advance_reader only advances by one event,
			 * whereas rb_event_ts_length may include the size of
			 * one or two events.
			 * We have already ensured there's enough space if this
			 * is a time extend. */
			size = rb_event_length(event);
			memcpy(bpage->data + pos, rpage->data + rpos, size);

			len -= size;

			rb_advance_reader(cpu_buffer);
			rpos = reader->read;
			pos += size;

			if (rpos >= commit)
				break;

			event = rb_reader_event(cpu_buffer);
			/* Always keep the time extend and data together */
			size = rb_event_ts_length(event);
		} while (len >= size);

		/* update bpage */
		local_set(&bpage->commit, pos);
		bpage->time_stamp = save_timestamp;

		/* we copied everything to the beginning */
		read = 0;
	} else {
		/* update the entry counter */
		cpu_buffer->read += rb_page_entries(reader);
		cpu_buffer->read_bytes += rb_page_size(reader);

		/* swap the pages */
		rb_init_page(bpage);
		bpage = reader->page;
		reader->page = data_page->data;
		local_set(&reader->write, 0);
		local_set(&reader->entries, 0);
		reader->read = 0;
		data_page->data = bpage;

		/*
		 * Use the real_end for the data size,
		 * This gives us a chance to store the lost events
		 * on the page.
		 */
		if (reader->real_end)
			local_set(&bpage->commit, reader->real_end);
	}
	ret = read;

	cpu_buffer->lost_events = 0;

	commit = local_read(&bpage->commit);
	/*
	 * Set a flag in the commit field if we lost events
	 */
	if (missed_events) {
		/* If there is room at the end of the page to save the
		 * missed events, then record it there.
		 */
		if (buffer->subbuf_size - commit >= sizeof(missed_events)) {
			memcpy(&bpage->data[commit], &missed_events,
			       sizeof(missed_events));
			local_add(RB_MISSED_STORED, &bpage->commit);
			commit += sizeof(missed_events);
		}
		local_add(RB_MISSED_EVENTS, &bpage->commit);
	}

	/*
	 * This page may be off to user land. Zero it out here.
	 */
	if (commit < buffer->subbuf_size)
		memset(&bpage->data[commit], 0, buffer->subbuf_size - commit);

 out_unlock:
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);

 out:
	return ret;
}
EXPORT_SYMBOL_GPL(ring_buffer_read_page);

/**
 * ring_buffer_read_page_data - get pointer to the data in the page.
 * @page:  the page to get the data from
 *
 * Returns pointer to the actual data in this page.
 */
void *ring_buffer_read_page_data(struct buffer_data_read_page *page)
{
	return page->data;
}
EXPORT_SYMBOL_GPL(ring_buffer_read_page_data);

/**
 * ring_buffer_subbuf_size_get - get size of the sub buffer.
 * @buffer: the buffer to get the sub buffer size from
 *
 * Returns size of the sub buffer, in bytes.
 */
int ring_buffer_subbuf_size_get(struct trace_buffer *buffer)
{
	return buffer->subbuf_size + BUF_PAGE_HDR_SIZE;
}
EXPORT_SYMBOL_GPL(ring_buffer_subbuf_size_get);

/**
 * ring_buffer_subbuf_order_get - get order of system sub pages in one buffer page.
 * @buffer: The ring_buffer to get the system sub page order from
 *
 * By default, one ring buffer sub page equals to one system page. This parameter
 * is configurable, per ring buffer. The size of the ring buffer sub page can be
 * extended, but must be an order of system page size.
 *
 * Returns the order of buffer sub page size, in system pages:
 * 0 means the sub buffer size is 1 system page and so forth.
 * In case of an error < 0 is returned.
 */
int ring_buffer_subbuf_order_get(struct trace_buffer *buffer)
{
	if (!buffer)
		return -EINVAL;

	return buffer->subbuf_order;
}
EXPORT_SYMBOL_GPL(ring_buffer_subbuf_order_get);

/**
 * ring_buffer_subbuf_order_set - set the size of ring buffer sub page.
 * @buffer: The ring_buffer to set the new page size.
 * @order: Order of the system pages in one sub buffer page
 *
 * By default, one ring buffer pages equals to one system page. This API can be
 * used to set new size of the ring buffer page. The size must be order of
 * system page size, that's why the input parameter @order is the order of
 * system pages that are allocated for one ring buffer page:
 *  0 - 1 system page
 *  1 - 2 system pages
 *  3 - 4 system pages
 *  ...
 *
 * Returns 0 on success or < 0 in case of an error.
 */
int ring_buffer_subbuf_order_set(struct trace_buffer *buffer, int order)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct buffer_page *bpage, *tmp;
	int old_order, old_size;
	int nr_pages;
	int psize;
	int err;
	int cpu;

	if (!buffer || order < 0)
		return -EINVAL;

	if (buffer->subbuf_order == order)
		return 0;

	psize = (1 << order) * PAGE_SIZE;
	if (psize <= BUF_PAGE_HDR_SIZE)
		return -EINVAL;

	/* Size of a subbuf cannot be greater than the write counter */
	if (psize > RB_WRITE_MASK + 1)
		return -EINVAL;

	old_order = buffer->subbuf_order;
	old_size = buffer->subbuf_size;

	/* prevent another thread from changing buffer sizes */
	mutex_lock(&buffer->mutex);
	atomic_inc(&buffer->record_disabled);

	/* Make sure all commits have finished */
	synchronize_rcu();

	buffer->subbuf_order = order;
	buffer->subbuf_size = psize - BUF_PAGE_HDR_SIZE;

	/* Make sure all new buffers are allocated, before deleting the old ones */
	for_each_buffer_cpu(buffer, cpu) {

		if (!cpumask_test_cpu(cpu, buffer->cpumask))
			continue;

		cpu_buffer = buffer->buffers[cpu];

		if (cpu_buffer->mapped) {
			err = -EBUSY;
			goto error;
		}

		/* Update the number of pages to match the new size */
		nr_pages = old_size * buffer->buffers[cpu]->nr_pages;
		nr_pages = DIV_ROUND_UP(nr_pages, buffer->subbuf_size);

		/* we need a minimum of two pages */
		if (nr_pages < 2)
			nr_pages = 2;

		cpu_buffer->nr_pages_to_update = nr_pages;

		/* Include the reader page */
		nr_pages++;

		/* Allocate the new size buffer */
		INIT_LIST_HEAD(&cpu_buffer->new_pages);
		if (__rb_allocate_pages(cpu_buffer, nr_pages,
					&cpu_buffer->new_pages)) {
			/* not enough memory for new pages */
			err = -ENOMEM;
			goto error;
		}
	}

	for_each_buffer_cpu(buffer, cpu) {
		struct buffer_data_page *old_free_data_page;
		struct list_head old_pages;
		unsigned long flags;

		if (!cpumask_test_cpu(cpu, buffer->cpumask))
			continue;

		cpu_buffer = buffer->buffers[cpu];

		raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);

		/* Clear the head bit to make the link list normal to read */
		rb_head_page_deactivate(cpu_buffer);

		/*
		 * Collect buffers from the cpu_buffer pages list and the
		 * reader_page on old_pages, so they can be freed later when not
		 * under a spinlock. The pages list is a linked list with no
		 * head, adding old_pages turns it into a regular list with
		 * old_pages being the head.
		 */
		list_add(&old_pages, cpu_buffer->pages);
		list_add(&cpu_buffer->reader_page->list, &old_pages);

		/* One page was allocated for the reader page */
		cpu_buffer->reader_page = list_entry(cpu_buffer->new_pages.next,
						     struct buffer_page, list);
		list_del_init(&cpu_buffer->reader_page->list);

		/* Install the new pages, remove the head from the list */
		cpu_buffer->pages = cpu_buffer->new_pages.next;
		list_del_init(&cpu_buffer->new_pages);
		cpu_buffer->cnt++;

		cpu_buffer->head_page
			= list_entry(cpu_buffer->pages, struct buffer_page, list);
		cpu_buffer->tail_page = cpu_buffer->commit_page = cpu_buffer->head_page;

		cpu_buffer->nr_pages = cpu_buffer->nr_pages_to_update;
		cpu_buffer->nr_pages_to_update = 0;

		old_free_data_page = cpu_buffer->free_page;
		cpu_buffer->free_page = NULL;

		rb_head_page_activate(cpu_buffer);

		raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);

		/* Free old sub buffers */
		list_for_each_entry_safe(bpage, tmp, &old_pages, list) {
			list_del_init(&bpage->list);
			free_buffer_page(bpage);
		}
		free_pages((unsigned long)old_free_data_page, old_order);

		rb_check_pages(cpu_buffer);
	}

	atomic_dec(&buffer->record_disabled);
	mutex_unlock(&buffer->mutex);

	return 0;

error:
	buffer->subbuf_order = old_order;
	buffer->subbuf_size = old_size;

	atomic_dec(&buffer->record_disabled);
	mutex_unlock(&buffer->mutex);

	for_each_buffer_cpu(buffer, cpu) {
		cpu_buffer = buffer->buffers[cpu];

		if (!cpu_buffer->nr_pages_to_update)
			continue;

		list_for_each_entry_safe(bpage, tmp, &cpu_buffer->new_pages, list) {
			list_del_init(&bpage->list);
			free_buffer_page(bpage);
		}
	}

	return err;
}
EXPORT_SYMBOL_GPL(ring_buffer_subbuf_order_set);

static int rb_alloc_meta_page(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct page *page;

	if (cpu_buffer->meta_page)
		return 0;

	page = alloc_page(GFP_USER | __GFP_ZERO);
	if (!page)
		return -ENOMEM;

	cpu_buffer->meta_page = page_to_virt(page);

	return 0;
}

static void rb_free_meta_page(struct ring_buffer_per_cpu *cpu_buffer)
{
	unsigned long addr = (unsigned long)cpu_buffer->meta_page;

	free_page(addr);
	cpu_buffer->meta_page = NULL;
}

static void rb_setup_ids_meta_page(struct ring_buffer_per_cpu *cpu_buffer,
				   unsigned long *subbuf_ids)
{
	struct trace_buffer_meta *meta = cpu_buffer->meta_page;
	unsigned int nr_subbufs = cpu_buffer->nr_pages + 1;
	struct buffer_page *first_subbuf, *subbuf;
	int id = 0;

	subbuf_ids[id] = (unsigned long)cpu_buffer->reader_page->page;
	cpu_buffer->reader_page->id = id++;

	first_subbuf = subbuf = rb_set_head_page(cpu_buffer);
	do {
		if (WARN_ON(id >= nr_subbufs))
			break;

		subbuf_ids[id] = (unsigned long)subbuf->page;
		subbuf->id = id;

		rb_inc_page(&subbuf);
		id++;
	} while (subbuf != first_subbuf);

	/* install subbuf ID to kern VA translation */
	cpu_buffer->subbuf_ids = subbuf_ids;

	meta->meta_struct_len = sizeof(*meta);
	meta->nr_subbufs = nr_subbufs;
	meta->subbuf_size = cpu_buffer->buffer->subbuf_size + BUF_PAGE_HDR_SIZE;
	meta->meta_page_size = meta->subbuf_size;

	rb_update_meta_page(cpu_buffer);
}

static struct ring_buffer_per_cpu *
rb_get_mapped_buffer(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return ERR_PTR(-EINVAL);

	cpu_buffer = buffer->buffers[cpu];

	mutex_lock(&cpu_buffer->mapping_lock);

	if (!cpu_buffer->user_mapped) {
		mutex_unlock(&cpu_buffer->mapping_lock);
		return ERR_PTR(-ENODEV);
	}

	return cpu_buffer;
}

static void rb_put_mapped_buffer(struct ring_buffer_per_cpu *cpu_buffer)
{
	mutex_unlock(&cpu_buffer->mapping_lock);
}

/*
 * Fast-path for rb_buffer_(un)map(). Called whenever the meta-page doesn't need
 * to be set-up or torn-down.
 */
static int __rb_inc_dec_mapped(struct ring_buffer_per_cpu *cpu_buffer,
			       bool inc)
{
	unsigned long flags;

	lockdep_assert_held(&cpu_buffer->mapping_lock);

	/* mapped is always greater or equal to user_mapped */
	if (WARN_ON(cpu_buffer->mapped < cpu_buffer->user_mapped))
		return -EINVAL;

	if (inc && cpu_buffer->mapped == UINT_MAX)
		return -EBUSY;

	if (WARN_ON(!inc && cpu_buffer->user_mapped == 0))
		return -EINVAL;

	mutex_lock(&cpu_buffer->buffer->mutex);
	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);

	if (inc) {
		cpu_buffer->user_mapped++;
		cpu_buffer->mapped++;
	} else {
		cpu_buffer->user_mapped--;
		cpu_buffer->mapped--;
	}

	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);
	mutex_unlock(&cpu_buffer->buffer->mutex);

	return 0;
}

/*
 *   +--------------+  pgoff == 0
 *   |   meta page  |
 *   +--------------+  pgoff == 1
 *   | subbuffer 0  |
 *   |              |
 *   +--------------+  pgoff == (1 + (1 << subbuf_order))
 *   | subbuffer 1  |
 *   |              |
 *         ...
 */
#ifdef CONFIG_MMU
static int __rb_map_vma(struct ring_buffer_per_cpu *cpu_buffer,
			struct vm_area_struct *vma)
{
	unsigned long nr_subbufs, nr_pages, nr_vma_pages, pgoff = vma->vm_pgoff;
	unsigned int subbuf_pages, subbuf_order;
	struct page **pages;
	int p = 0, s = 0;
	int err;

	/* Refuse MP_PRIVATE or writable mappings */
	if (vma->vm_flags & VM_WRITE || vma->vm_flags & VM_EXEC ||
	    !(vma->vm_flags & VM_MAYSHARE))
		return -EPERM;

	subbuf_order = cpu_buffer->buffer->subbuf_order;
	subbuf_pages = 1 << subbuf_order;

	if (subbuf_order && pgoff % subbuf_pages)
		return -EINVAL;

	/*
	 * Make sure the mapping cannot become writable later. Also tell the VM
	 * to not touch these pages (VM_DONTCOPY | VM_DONTEXPAND).
	 */
	vm_flags_mod(vma, VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP,
		     VM_MAYWRITE);

	lockdep_assert_held(&cpu_buffer->mapping_lock);

	nr_subbufs = cpu_buffer->nr_pages + 1; /* + reader-subbuf */
	nr_pages = ((nr_subbufs + 1) << subbuf_order); /* + meta-page */
	if (nr_pages <= pgoff)
		return -EINVAL;

	nr_pages -= pgoff;

	nr_vma_pages = vma_pages(vma);
	if (!nr_vma_pages || nr_vma_pages > nr_pages)
		return -EINVAL;

	nr_pages = nr_vma_pages;

	pages = kcalloc(nr_pages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	if (!pgoff) {
		unsigned long meta_page_padding;

		pages[p++] = virt_to_page(cpu_buffer->meta_page);

		/*
		 * Pad with the zero-page to align the meta-page with the
		 * sub-buffers.
		 */
		meta_page_padding = subbuf_pages - 1;
		while (meta_page_padding-- && p < nr_pages) {
			unsigned long __maybe_unused zero_addr =
				vma->vm_start + (PAGE_SIZE * p);

			pages[p++] = ZERO_PAGE(zero_addr);
		}
	} else {
		/* Skip the meta-page */
		pgoff -= subbuf_pages;

		s += pgoff / subbuf_pages;
	}

	while (p < nr_pages) {
		struct page *page = virt_to_page((void *)cpu_buffer->subbuf_ids[s]);
		int off = 0;

		if (WARN_ON_ONCE(s >= nr_subbufs)) {
			err = -EINVAL;
			goto out;
		}

		for (; off < (1 << (subbuf_order)); off++, page++) {
			if (p >= nr_pages)
				break;

			pages[p++] = page;
		}
		s++;
	}

	err = vm_insert_pages(vma, vma->vm_start, pages, &nr_pages);

out:
	kfree(pages);

	return err;
}
#else
static int __rb_map_vma(struct ring_buffer_per_cpu *cpu_buffer,
			struct vm_area_struct *vma)
{
	return -EOPNOTSUPP;
}
#endif

int ring_buffer_map(struct trace_buffer *buffer, int cpu,
		    struct vm_area_struct *vma)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long flags, *subbuf_ids;
	int err = 0;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return -EINVAL;

	cpu_buffer = buffer->buffers[cpu];

	mutex_lock(&cpu_buffer->mapping_lock);

	if (cpu_buffer->user_mapped) {
		err = __rb_map_vma(cpu_buffer, vma);
		if (!err)
			err = __rb_inc_dec_mapped(cpu_buffer, true);
		mutex_unlock(&cpu_buffer->mapping_lock);
		return err;
	}

	/* prevent another thread from changing buffer/sub-buffer sizes */
	mutex_lock(&buffer->mutex);

	err = rb_alloc_meta_page(cpu_buffer);
	if (err)
		goto unlock;

	/* subbuf_ids include the reader while nr_pages does not */
	subbuf_ids = kcalloc(cpu_buffer->nr_pages + 1, sizeof(*subbuf_ids), GFP_KERNEL);
	if (!subbuf_ids) {
		rb_free_meta_page(cpu_buffer);
		err = -ENOMEM;
		goto unlock;
	}

	atomic_inc(&cpu_buffer->resize_disabled);

	/*
	 * Lock all readers to block any subbuf swap until the subbuf IDs are
	 * assigned.
	 */
	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);
	rb_setup_ids_meta_page(cpu_buffer, subbuf_ids);

	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);

	err = __rb_map_vma(cpu_buffer, vma);
	if (!err) {
		raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);
		/* This is the first time it is mapped by user */
		cpu_buffer->mapped++;
		cpu_buffer->user_mapped = 1;
		raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);
	} else {
		kfree(cpu_buffer->subbuf_ids);
		cpu_buffer->subbuf_ids = NULL;
		rb_free_meta_page(cpu_buffer);
	}

unlock:
	mutex_unlock(&buffer->mutex);
	mutex_unlock(&cpu_buffer->mapping_lock);

	return err;
}

int ring_buffer_unmap(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long flags;
	int err = 0;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return -EINVAL;

	cpu_buffer = buffer->buffers[cpu];

	mutex_lock(&cpu_buffer->mapping_lock);

	if (!cpu_buffer->user_mapped) {
		err = -ENODEV;
		goto out;
	} else if (cpu_buffer->user_mapped > 1) {
		__rb_inc_dec_mapped(cpu_buffer, false);
		goto out;
	}

	mutex_lock(&buffer->mutex);
	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);

	/* This is the last user space mapping */
	if (!WARN_ON_ONCE(cpu_buffer->mapped < cpu_buffer->user_mapped))
		cpu_buffer->mapped--;
	cpu_buffer->user_mapped = 0;

	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);

	kfree(cpu_buffer->subbuf_ids);
	cpu_buffer->subbuf_ids = NULL;
	rb_free_meta_page(cpu_buffer);
	atomic_dec(&cpu_buffer->resize_disabled);

	mutex_unlock(&buffer->mutex);

out:
	mutex_unlock(&cpu_buffer->mapping_lock);

	return err;
}

int ring_buffer_map_get_reader(struct trace_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct buffer_page *reader;
	unsigned long missed_events;
	unsigned long reader_size;
	unsigned long flags;

	cpu_buffer = rb_get_mapped_buffer(buffer, cpu);
	if (IS_ERR(cpu_buffer))
		return (int)PTR_ERR(cpu_buffer);

	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);

consume:
	if (rb_per_cpu_empty(cpu_buffer))
		goto out;

	reader_size = rb_page_size(cpu_buffer->reader_page);

	/*
	 * There are data to be read on the current reader page, we can
	 * return to the caller. But before that, we assume the latter will read
	 * everything. Let's update the kernel reader accordingly.
	 */
	if (cpu_buffer->reader_page->read < reader_size) {
		while (cpu_buffer->reader_page->read < reader_size)
			rb_advance_reader(cpu_buffer);
		goto out;
	}

	reader = rb_get_reader_page(cpu_buffer);
	if (WARN_ON(!reader))
		goto out;

	/* Check if any events were dropped */
	missed_events = cpu_buffer->lost_events;

	if (cpu_buffer->reader_page != cpu_buffer->commit_page) {
		if (missed_events) {
			struct buffer_data_page *bpage = reader->page;
			unsigned int commit;
			/*
			 * Use the real_end for the data size,
			 * This gives us a chance to store the lost events
			 * on the page.
			 */
			if (reader->real_end)
				local_set(&bpage->commit, reader->real_end);
			/*
			 * If there is room at the end of the page to save the
			 * missed events, then record it there.
			 */
			commit = rb_page_size(reader);
			if (buffer->subbuf_size - commit >= sizeof(missed_events)) {
				memcpy(&bpage->data[commit], &missed_events,
				       sizeof(missed_events));
				local_add(RB_MISSED_STORED, &bpage->commit);
			}
			local_add(RB_MISSED_EVENTS, &bpage->commit);
		}
	} else {
		/*
		 * There really shouldn't be any missed events if the commit
		 * is on the reader page.
		 */
		WARN_ON_ONCE(missed_events);
	}

	cpu_buffer->lost_events = 0;

	goto consume;

out:
	/* Some archs do not have data cache coherency between kernel and user-space */
	flush_dcache_folio(virt_to_folio(cpu_buffer->reader_page->page));

	rb_update_meta_page(cpu_buffer);

	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);
	rb_put_mapped_buffer(cpu_buffer);

	return 0;
}

/*
 * We only allocate new buffers, never free them if the CPU goes down.
 * If we were to free the buffer, then the user would lose any trace that was in
 * the buffer.
 */
int trace_rb_cpu_prepare(unsigned int cpu, struct hlist_node *node)
{
	struct trace_buffer *buffer;
	long nr_pages_same;
	int cpu_i;
	unsigned long nr_pages;

	buffer = container_of(node, struct trace_buffer, node);
	if (cpumask_test_cpu(cpu, buffer->cpumask))
		return 0;

	nr_pages = 0;
	nr_pages_same = 1;
	/* check if all cpu sizes are same */
	for_each_buffer_cpu(buffer, cpu_i) {
		/* fill in the size from first enabled cpu */
		if (nr_pages == 0)
			nr_pages = buffer->buffers[cpu_i]->nr_pages;
		if (nr_pages != buffer->buffers[cpu_i]->nr_pages) {
			nr_pages_same = 0;
			break;
		}
	}
	/* allocate minimum pages, user can later expand it */
	if (!nr_pages_same)
		nr_pages = 2;
	buffer->buffers[cpu] =
		rb_allocate_cpu_buffer(buffer, nr_pages, cpu);
	if (!buffer->buffers[cpu]) {
		WARN(1, "failed to allocate ring buffer on CPU %u\n",
		     cpu);
		return -ENOMEM;
	}
	smp_wmb();
	cpumask_set_cpu(cpu, buffer->cpumask);
	return 0;
}

#ifdef CONFIG_RING_BUFFER_STARTUP_TEST
/*
 * This is a basic integrity check of the ring buffer.
 * Late in the boot cycle this test will run when configured in.
 * It will kick off a thread per CPU that will go into a loop
 * writing to the per cpu ring buffer various sizes of data.
 * Some of the data will be large items, some small.
 *
 * Another thread is created that goes into a spin, sending out
 * IPIs to the other CPUs to also write into the ring buffer.
 * this is to test the nesting ability of the buffer.
 *
 * Basic stats are recorded and reported. If something in the
 * ring buffer should happen that's not expected, a big warning
 * is displayed and all ring buffers are disabled.
 */
static struct task_struct *rb_threads[NR_CPUS] __initdata;

struct rb_test_data {
	struct trace_buffer *buffer;
	unsigned long		events;
	unsigned long		bytes_written;
	unsigned long		bytes_alloc;
	unsigned long		bytes_dropped;
	unsigned long		events_nested;
	unsigned long		bytes_written_nested;
	unsigned long		bytes_alloc_nested;
	unsigned long		bytes_dropped_nested;
	int			min_size_nested;
	int			max_size_nested;
	int			max_size;
	int			min_size;
	int			cpu;
	int			cnt;
};

static struct rb_test_data rb_data[NR_CPUS] __initdata;

/* 1 meg per cpu */
#define RB_TEST_BUFFER_SIZE	1048576

static char rb_string[] __initdata =
	"abcdefghijklmnopqrstuvwxyz1234567890!@#$%^&*()?+\\"
	"?+|:';\",.<>/?abcdefghijklmnopqrstuvwxyz1234567890"
	"!@#$%^&*()?+\\?+|:';\",.<>/?abcdefghijklmnopqrstuv";

static bool rb_test_started __initdata;

struct rb_item {
	int size;
	char str[];
};

static __init int rb_write_something(struct rb_test_data *data, bool nested)
{
	struct ring_buffer_event *event;
	struct rb_item *item;
	bool started;
	int event_len;
	int size;
	int len;
	int cnt;

	/* Have nested writes different that what is written */
	cnt = data->cnt + (nested ? 27 : 0);

	/* Multiply cnt by ~e, to make some unique increment */
	size = (cnt * 68 / 25) % (sizeof(rb_string) - 1);

	len = size + sizeof(struct rb_item);

	started = rb_test_started;
	/* read rb_test_started before checking buffer enabled */
	smp_rmb();

	event = ring_buffer_lock_reserve(data->buffer, len);
	if (!event) {
		/* Ignore dropped events before test starts. */
		if (started) {
			if (nested)
				data->bytes_dropped += len;
			else
				data->bytes_dropped_nested += len;
		}
		return len;
	}

	event_len = ring_buffer_event_length(event);

	if (RB_WARN_ON(data->buffer, event_len < len))
		goto out;

	item = ring_buffer_event_data(event);
	item->size = size;
	memcpy(item->str, rb_string, size);

	if (nested) {
		data->bytes_alloc_nested += event_len;
		data->bytes_written_nested += len;
		data->events_nested++;
		if (!data->min_size_nested || len < data->min_size_nested)
			data->min_size_nested = len;
		if (len > data->max_size_nested)
			data->max_size_nested = len;
	} else {
		data->bytes_alloc += event_len;
		data->bytes_written += len;
		data->events++;
		if (!data->min_size || len < data->min_size)
			data->max_size = len;
		if (len > data->max_size)
			data->max_size = len;
	}

 out:
	ring_buffer_unlock_commit(data->buffer);

	return 0;
}

static __init int rb_test(void *arg)
{
	struct rb_test_data *data = arg;

	while (!kthread_should_stop()) {
		rb_write_something(data, false);
		data->cnt++;

		set_current_state(TASK_INTERRUPTIBLE);
		/* Now sleep between a min of 100-300us and a max of 1ms */
		usleep_range(((data->cnt % 3) + 1) * 100, 1000);
	}

	return 0;
}

static __init void rb_ipi(void *ignore)
{
	struct rb_test_data *data;
	int cpu = smp_processor_id();

	data = &rb_data[cpu];
	rb_write_something(data, true);
}

static __init int rb_hammer_test(void *arg)
{
	while (!kthread_should_stop()) {

		/* Send an IPI to all cpus to write data! */
		smp_call_function(rb_ipi, NULL, 1);
		/* No sleep, but for non preempt, let others run */
		schedule();
	}

	return 0;
}

static __init int test_ringbuffer(void)
{
	struct task_struct *rb_hammer;
	struct trace_buffer *buffer;
	int cpu;
	int ret = 0;

	if (security_locked_down(LOCKDOWN_TRACEFS)) {
		pr_warn("Lockdown is enabled, skipping ring buffer tests\n");
		return 0;
	}

	pr_info("Running ring buffer tests...\n");

	buffer = ring_buffer_alloc(RB_TEST_BUFFER_SIZE, RB_FL_OVERWRITE);
	if (WARN_ON(!buffer))
		return 0;

	/* Disable buffer so that threads can't write to it yet */
	ring_buffer_record_off(buffer);

	for_each_online_cpu(cpu) {
		rb_data[cpu].buffer = buffer;
		rb_data[cpu].cpu = cpu;
		rb_data[cpu].cnt = cpu;
		rb_threads[cpu] = kthread_run_on_cpu(rb_test, &rb_data[cpu],
						     cpu, "rbtester/%u");
		if (WARN_ON(IS_ERR(rb_threads[cpu]))) {
			pr_cont("FAILED\n");
			ret = PTR_ERR(rb_threads[cpu]);
			goto out_free;
		}
	}

	/* Now create the rb hammer! */
	rb_hammer = kthread_run(rb_hammer_test, NULL, "rbhammer");
	if (WARN_ON(IS_ERR(rb_hammer))) {
		pr_cont("FAILED\n");
		ret = PTR_ERR(rb_hammer);
		goto out_free;
	}

	ring_buffer_record_on(buffer);
	/*
	 * Show buffer is enabled before setting rb_test_started.
	 * Yes there's a small race window where events could be
	 * dropped and the thread wont catch it. But when a ring
	 * buffer gets enabled, there will always be some kind of
	 * delay before other CPUs see it. Thus, we don't care about
	 * those dropped events. We care about events dropped after
	 * the threads see that the buffer is active.
	 */
	smp_wmb();
	rb_test_started = true;

	set_current_state(TASK_INTERRUPTIBLE);
	/* Just run for 10 seconds */;
	schedule_timeout(10 * HZ);

	kthread_stop(rb_hammer);

 out_free:
	for_each_online_cpu(cpu) {
		if (!rb_threads[cpu])
			break;
		kthread_stop(rb_threads[cpu]);
	}
	if (ret) {
		ring_buffer_free(buffer);
		return ret;
	}

	/* Report! */
	pr_info("finished\n");
	for_each_online_cpu(cpu) {
		struct ring_buffer_event *event;
		struct rb_test_data *data = &rb_data[cpu];
		struct rb_item *item;
		unsigned long total_events;
		unsigned long total_dropped;
		unsigned long total_written;
		unsigned long total_alloc;
		unsigned long total_read = 0;
		unsigned long total_size = 0;
		unsigned long total_len = 0;
		unsigned long total_lost = 0;
		unsigned long lost;
		int big_event_size;
		int small_event_size;

		ret = -1;

		total_events = data->events + data->events_nested;
		total_written = data->bytes_written + data->bytes_written_nested;
		total_alloc = data->bytes_alloc + data->bytes_alloc_nested;
		total_dropped = data->bytes_dropped + data->bytes_dropped_nested;

		big_event_size = data->max_size + data->max_size_nested;
		small_event_size = data->min_size + data->min_size_nested;

		pr_info("CPU %d:\n", cpu);
		pr_info("              events:    %ld\n", total_events);
		pr_info("       dropped bytes:    %ld\n", total_dropped);
		pr_info("       alloced bytes:    %ld\n", total_alloc);
		pr_info("       written bytes:    %ld\n", total_written);
		pr_info("       biggest event:    %d\n", big_event_size);
		pr_info("      smallest event:    %d\n", small_event_size);

		if (RB_WARN_ON(buffer, total_dropped))
			break;

		ret = 0;

		while ((event = ring_buffer_consume(buffer, cpu, NULL, &lost))) {
			total_lost += lost;
			item = ring_buffer_event_data(event);
			total_len += ring_buffer_event_length(event);
			total_size += item->size + sizeof(struct rb_item);
			if (memcmp(&item->str[0], rb_string, item->size) != 0) {
				pr_info("FAILED!\n");
				pr_info("buffer had: %.*s\n", item->size, item->str);
				pr_info("expected:   %.*s\n", item->size, rb_string);
				RB_WARN_ON(buffer, 1);
				ret = -1;
				break;
			}
			total_read++;
		}
		if (ret)
			break;

		ret = -1;

		pr_info("         read events:   %ld\n", total_read);
		pr_info("         lost events:   %ld\n", total_lost);
		pr_info("        total events:   %ld\n", total_lost + total_read);
		pr_info("  recorded len bytes:   %ld\n", total_len);
		pr_info(" recorded size bytes:   %ld\n", total_size);
		if (total_lost) {
			pr_info(" With dropped events, record len and size may not match\n"
				" alloced and written from above\n");
		} else {
			if (RB_WARN_ON(buffer, total_len != total_alloc ||
				       total_size != total_written))
				break;
		}
		if (RB_WARN_ON(buffer, total_lost + total_read != total_events))
			break;

		ret = 0;
	}
	if (!ret)
		pr_info("Ring buffer PASSED!\n");

	ring_buffer_free(buffer);
	return 0;
}

late_initcall(test_ringbuffer);
#endif /* CONFIG_RING_BUFFER_STARTUP_TEST */
