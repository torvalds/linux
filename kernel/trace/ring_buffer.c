// SPDX-License-Identifier: GPL-2.0
/*
 * Generic ring buffer
 *
 * Copyright (C) 2008 Steven Rostedt <srostedt@redhat.com>
 */
#include <linux/trace_events.h>
#include <linux/ring_buffer.h>
#include <linux/trace_clock.h>
#include <linux/sched/clock.h>
#include <linux/trace_seq.h>
#include <linux/spinlock.h>
#include <linux/irq_work.h>
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

#include <asm/local.h>

static void update_pages_handler(struct work_struct *work);

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

static inline int rb_null_event(struct ring_buffer_event *event)
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
		BUG();
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
	BUG_ON(event->type_len > RINGBUF_TYPE_DATA_TYPE_LEN_MAX);
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

#define TS_SHIFT	27
#define TS_MASK		((1ULL << TS_SHIFT) - 1)
#define TS_DELTA_TEST	(~TS_MASK)

/**
 * ring_buffer_event_time_stamp - return the event's extended timestamp
 * @event: the event to get the timestamp of
 *
 * Returns the extended timestamp associated with a data event.
 * An extended time_stamp is a 64-bit timestamp represented
 * internally in a special way that makes the best use of space
 * contained within a ring buffer event.  This function decodes
 * it and maps it to a straight u64 value.
 */
u64 ring_buffer_event_time_stamp(struct ring_buffer_event *event)
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

#define RB_MISSED_FLAGS		(RB_MISSED_EVENTS|RB_MISSED_STORED)

struct buffer_data_page {
	u64		 time_stamp;	/* page time stamp */
	local_t		 commit;	/* write committed index */
	unsigned char	 data[] RB_ALIGN_DATA;	/* data of buffer page */
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

/*
 * Also stolen from mm/slob.c. Thanks to Mathieu Desnoyers for pointing
 * this issue out.
 */
static void free_buffer_page(struct buffer_page *bpage)
{
	free_page((unsigned long)bpage->page);
	kfree(bpage);
}

/*
 * We need to fit the time_stamp delta into 27 bits.
 */
static inline int test_time_stamp(u64 delta)
{
	if (delta & TS_DELTA_TEST)
		return 1;
	return 0;
}

#define BUF_PAGE_SIZE (PAGE_SIZE - BUF_PAGE_HDR_SIZE)

/* Max payload is BUF_PAGE_SIZE - header (8bytes) */
#define BUF_MAX_DATA_SIZE (BUF_PAGE_SIZE - (sizeof(u32) * 2))

int ring_buffer_print_page_header(struct trace_seq *s)
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
			 (unsigned int)BUF_PAGE_SIZE,
			 (unsigned int)is_signed_type(char));

	return !trace_seq_has_overflowed(s);
}

struct rb_irq_work {
	struct irq_work			work;
	wait_queue_head_t		waiters;
	wait_queue_head_t		full_waiters;
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
	unsigned long		length;
	struct buffer_page	*tail_page;
	int			add_timestamp;
};

/*
 * Used for which event context the event is in.
 *  NMI     = 0
 *  IRQ     = 1
 *  SOFTIRQ = 2
 *  NORMAL  = 3
 *
 * See trace_recursive_lock() comment below for more details.
 */
enum {
	RB_CTX_NMI,
	RB_CTX_IRQ,
	RB_CTX_SOFTIRQ,
	RB_CTX_NORMAL,
	RB_CTX_MAX
};

/*
 * head_page == tail_page && head == tail then buffer is empty.
 */
struct ring_buffer_per_cpu {
	int				cpu;
	atomic_t			record_disabled;
	struct ring_buffer		*buffer;
	raw_spinlock_t			reader_lock;	/* serialize readers */
	arch_spinlock_t			lock;
	struct lock_class_key		lock_key;
	struct buffer_data_page		*free_page;
	unsigned long			nr_pages;
	unsigned int			current_context;
	struct list_head		*pages;
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
	local_t				pages_read;
	long				last_pages_touch;
	size_t				shortest_full;
	unsigned long			read;
	unsigned long			read_bytes;
	u64				write_stamp;
	u64				read_stamp;
	/* ring buffer pages to update, > 0 to add, < 0 to remove */
	long				nr_pages_to_update;
	struct list_head		new_pages; /* new pages to add */
	struct work_struct		update_pages_work;
	struct completion		update_done;

	struct rb_irq_work		irq_work;
};

struct ring_buffer {
	unsigned			flags;
	int				cpus;
	atomic_t			record_disabled;
	atomic_t			resize_disabled;
	cpumask_var_t			cpumask;

	struct lock_class_key		*reader_lock_key;

	struct mutex			mutex;

	struct ring_buffer_per_cpu	**buffers;

	struct hlist_node		node;
	u64				(*clock)(void);

	struct rb_irq_work		irq_work;
	bool				time_stamp_abs;
};

struct ring_buffer_iter {
	struct ring_buffer_per_cpu	*cpu_buffer;
	unsigned long			head;
	struct buffer_page		*head_page;
	struct buffer_page		*cache_reader_page;
	unsigned long			cache_read;
	u64				read_stamp;
};

/**
 * ring_buffer_nr_pages - get the number of buffer pages in the ring buffer
 * @buffer: The ring_buffer to get the number of pages from
 * @cpu: The cpu of the ring_buffer to get the number of pages from
 *
 * Returns the number of pages used by a per_cpu buffer of the ring buffer.
 */
size_t ring_buffer_nr_pages(struct ring_buffer *buffer, int cpu)
{
	return buffer->buffers[cpu]->nr_pages;
}

/**
 * ring_buffer_nr_pages_dirty - get the number of used pages in the ring buffer
 * @buffer: The ring_buffer to get the number of pages from
 * @cpu: The cpu of the ring_buffer to get the number of pages from
 *
 * Returns the number of pages that have content in the ring buffer.
 */
size_t ring_buffer_nr_dirty_pages(struct ring_buffer *buffer, int cpu)
{
	size_t read;
	size_t cnt;

	read = local_read(&buffer->buffers[cpu]->pages_read);
	cnt = local_read(&buffer->buffers[cpu]->pages_touched);
	/* The reader can read an empty page, but not more than that */
	if (cnt < read) {
		WARN_ON_ONCE(read > cnt + 1);
		return 0;
	}

	return cnt - read;
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

	wake_up_all(&rbwork->waiters);
	if (rbwork->wakeup_full) {
		rbwork->wakeup_full = false;
		wake_up_all(&rbwork->full_waiters);
	}
}

/**
 * ring_buffer_wait - wait for input to the ring buffer
 * @buffer: buffer to wait on
 * @cpu: the cpu buffer to wait on
 * @full: wait until a full page is available, if @cpu != RING_BUFFER_ALL_CPUS
 *
 * If @cpu == RING_BUFFER_ALL_CPUS then the task will wake up as soon
 * as data is added to any of the @buffer's cpu buffers. Otherwise
 * it will wait for data to be added to a specific cpu buffer.
 */
int ring_buffer_wait(struct ring_buffer *buffer, int cpu, int full)
{
	struct ring_buffer_per_cpu *uninitialized_var(cpu_buffer);
	DEFINE_WAIT(wait);
	struct rb_irq_work *work;
	int ret = 0;

	/*
	 * Depending on what the caller is waiting for, either any
	 * data in any cpu buffer, or a specific buffer, put the
	 * caller on the appropriate wait queue.
	 */
	if (cpu == RING_BUFFER_ALL_CPUS) {
		work = &buffer->irq_work;
		/* Full only makes sense on per cpu reads */
		full = 0;
	} else {
		if (!cpumask_test_cpu(cpu, buffer->cpumask))
			return -ENODEV;
		cpu_buffer = buffer->buffers[cpu];
		work = &cpu_buffer->irq_work;
	}


	while (true) {
		if (full)
			prepare_to_wait(&work->full_waiters, &wait, TASK_INTERRUPTIBLE);
		else
			prepare_to_wait(&work->waiters, &wait, TASK_INTERRUPTIBLE);

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
			work->full_waiters_pending = true;
		else
			work->waiters_pending = true;

		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		if (cpu == RING_BUFFER_ALL_CPUS && !ring_buffer_empty(buffer))
			break;

		if (cpu != RING_BUFFER_ALL_CPUS &&
		    !ring_buffer_empty_cpu(buffer, cpu)) {
			unsigned long flags;
			bool pagebusy;
			size_t nr_pages;
			size_t dirty;

			if (!full)
				break;

			raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);
			pagebusy = cpu_buffer->reader_page == cpu_buffer->commit_page;
			nr_pages = cpu_buffer->nr_pages;
			dirty = ring_buffer_nr_dirty_pages(buffer, cpu);
			if (!cpu_buffer->shortest_full ||
			    cpu_buffer->shortest_full < full)
				cpu_buffer->shortest_full = full;
			raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);
			if (!pagebusy &&
			    (!nr_pages || (dirty * 100) > full * nr_pages))
				break;
		}

		schedule();
	}

	if (full)
		finish_wait(&work->full_waiters, &wait);
	else
		finish_wait(&work->waiters, &wait);

	return ret;
}

/**
 * ring_buffer_poll_wait - poll on buffer input
 * @buffer: buffer to wait on
 * @cpu: the cpu buffer to wait on
 * @filp: the file descriptor
 * @poll_table: The poll descriptor
 *
 * If @cpu == RING_BUFFER_ALL_CPUS then the task will wake up as soon
 * as data is added to any of the @buffer's cpu buffers. Otherwise
 * it will wait for data to be added to a specific cpu buffer.
 *
 * Returns EPOLLIN | EPOLLRDNORM if data exists in the buffers,
 * zero otherwise.
 */
__poll_t ring_buffer_poll_wait(struct ring_buffer *buffer, int cpu,
			  struct file *filp, poll_table *poll_table)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct rb_irq_work *work;

	if (cpu == RING_BUFFER_ALL_CPUS)
		work = &buffer->irq_work;
	else {
		if (!cpumask_test_cpu(cpu, buffer->cpumask))
			return -EINVAL;

		cpu_buffer = buffer->buffers[cpu];
		work = &cpu_buffer->irq_work;
	}

	poll_wait(filp, &work->waiters, poll_table);
	work->waiters_pending = true;
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

static inline u64 rb_time_stamp(struct ring_buffer *buffer)
{
	/* shift to debug/test normalization and TIME_EXTENTS */
	return buffer->clock() << DEBUG_SHIFT;
}

u64 ring_buffer_time_stamp(struct ring_buffer *buffer, int cpu)
{
	u64 time;

	preempt_disable_notrace();
	time = rb_time_stamp(buffer);
	preempt_enable_no_resched_notrace();

	return time;
}
EXPORT_SYMBOL_GPL(ring_buffer_time_stamp);

void ring_buffer_normalize_time_stamp(struct ring_buffer *buffer,
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
rb_is_head_page(struct ring_buffer_per_cpu *cpu_buffer,
		struct buffer_page *page, struct list_head *list)
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
static void rb_set_list_to_head(struct ring_buffer_per_cpu *cpu_buffer,
				struct list_head *list)
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
	rb_set_list_to_head(cpu_buffer, head->list.prev);
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

static inline void rb_inc_page(struct ring_buffer_per_cpu *cpu_buffer,
			       struct buffer_page **bpage)
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
			if (rb_is_head_page(cpu_buffer, page, page->list.prev)) {
				cpu_buffer->head_page = page;
				return page;
			}
			rb_inc_page(cpu_buffer, &page);
		} while (page != head);
	}

	RB_WARN_ON(cpu_buffer, 1);

	return NULL;
}

static int rb_head_page_replace(struct buffer_page *old,
				struct buffer_page *new)
{
	unsigned long *ptr = (unsigned long *)&old->list.prev->next;
	unsigned long val;
	unsigned long ret;

	val = *ptr & ~RB_FLAG_MASK;
	val |= RB_PAGE_HEAD;

	ret = cmpxchg(ptr, val, (unsigned long)&new->list);

	return ret == val;
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

	local_inc(&cpu_buffer->pages_touched);
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

		/* Again, either we update tail_page or an interrupt does */
		(void)cmpxchg(&cpu_buffer->tail_page, tail_page, next_page);
	}
}

static int rb_check_bpage(struct ring_buffer_per_cpu *cpu_buffer,
			  struct buffer_page *bpage)
{
	unsigned long val = (unsigned long)bpage;

	if (RB_WARN_ON(cpu_buffer, val & RB_FLAG_MASK))
		return 1;

	return 0;
}

/**
 * rb_check_list - make sure a pointer to a list has the last bits zero
 */
static int rb_check_list(struct ring_buffer_per_cpu *cpu_buffer,
			 struct list_head *list)
{
	if (RB_WARN_ON(cpu_buffer, rb_list_head(list->prev) != list->prev))
		return 1;
	if (RB_WARN_ON(cpu_buffer, rb_list_head(list->next) != list->next))
		return 1;
	return 0;
}

/**
 * rb_check_pages - integrity check of buffer pages
 * @cpu_buffer: CPU buffer with pages to test
 *
 * As a safety measure we check to make sure the data pages have not
 * been corrupted.
 */
static int rb_check_pages(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct list_head *head = cpu_buffer->pages;
	struct buffer_page *bpage, *tmp;

	/* Reset the head page if it exists */
	if (cpu_buffer->head_page)
		rb_set_head_page(cpu_buffer);

	rb_head_page_deactivate(cpu_buffer);

	if (RB_WARN_ON(cpu_buffer, head->next->prev != head))
		return -1;
	if (RB_WARN_ON(cpu_buffer, head->prev->next != head))
		return -1;

	if (rb_check_list(cpu_buffer, head))
		return -1;

	list_for_each_entry_safe(bpage, tmp, head, list) {
		if (RB_WARN_ON(cpu_buffer,
			       bpage->list.next->prev != &bpage->list))
			return -1;
		if (RB_WARN_ON(cpu_buffer,
			       bpage->list.prev->next != &bpage->list))
			return -1;
		if (rb_check_list(cpu_buffer, &bpage->list))
			return -1;
	}

	rb_head_page_activate(cpu_buffer);

	return 0;
}

static int __rb_allocate_pages(long nr_pages, struct list_head *pages, int cpu)
{
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
	for (i = 0; i < nr_pages; i++) {
		struct page *page;

		bpage = kzalloc_node(ALIGN(sizeof(*bpage), cache_line_size()),
				    mflags, cpu_to_node(cpu));
		if (!bpage)
			goto free_pages;

		list_add(&bpage->list, pages);

		page = alloc_pages_node(cpu_to_node(cpu), mflags, 0);
		if (!page)
			goto free_pages;
		bpage->page = page_address(page);
		rb_init_page(bpage->page);

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

	if (__rb_allocate_pages(nr_pages, &pages, cpu_buffer->cpu))
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
rb_allocate_cpu_buffer(struct ring_buffer *buffer, long nr_pages, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
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

	bpage = kzalloc_node(ALIGN(sizeof(*bpage), cache_line_size()),
			    GFP_KERNEL, cpu_to_node(cpu));
	if (!bpage)
		goto fail_free_buffer;

	rb_check_bpage(cpu_buffer, bpage);

	cpu_buffer->reader_page = bpage;
	page = alloc_pages_node(cpu_to_node(cpu), GFP_KERNEL, 0);
	if (!page)
		goto fail_free_reader;
	bpage->page = page_address(page);
	rb_init_page(bpage->page);

	INIT_LIST_HEAD(&cpu_buffer->reader_page->list);
	INIT_LIST_HEAD(&cpu_buffer->new_pages);

	ret = rb_allocate_pages(cpu_buffer, nr_pages);
	if (ret < 0)
		goto fail_free_reader;

	cpu_buffer->head_page
		= list_entry(cpu_buffer->pages, struct buffer_page, list);
	cpu_buffer->tail_page = cpu_buffer->commit_page = cpu_buffer->head_page;

	rb_head_page_activate(cpu_buffer);

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

	free_buffer_page(cpu_buffer->reader_page);

	rb_head_page_deactivate(cpu_buffer);

	if (head) {
		list_for_each_entry_safe(bpage, tmp, head, list) {
			list_del_init(&bpage->list);
			free_buffer_page(bpage);
		}
		bpage = list_entry(head, struct buffer_page, list);
		free_buffer_page(bpage);
	}

	kfree(cpu_buffer);
}

/**
 * __ring_buffer_alloc - allocate a new ring_buffer
 * @size: the size in bytes per cpu that is needed.
 * @flags: attributes to set for the ring buffer.
 *
 * Currently the only flag that is available is the RB_FL_OVERWRITE
 * flag. This flag means that the buffer will overwrite old data
 * when the buffer wraps. If this flag is not set, the buffer will
 * drop data when the tail hits the head.
 */
struct ring_buffer *__ring_buffer_alloc(unsigned long size, unsigned flags,
					struct lock_class_key *key)
{
	struct ring_buffer *buffer;
	long nr_pages;
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

	nr_pages = DIV_ROUND_UP(size, BUF_PAGE_SIZE);
	buffer->flags = flags;
	buffer->clock = trace_clock_local;
	buffer->reader_lock_key = key;

	init_irq_work(&buffer->irq_work.work, rb_wake_up_waiters);
	init_waitqueue_head(&buffer->irq_work.waiters);

	/* need at least two pages */
	if (nr_pages < 2)
		nr_pages = 2;

	buffer->cpus = nr_cpu_ids;

	bsize = sizeof(void *) * nr_cpu_ids;
	buffer->buffers = kzalloc(ALIGN(bsize, cache_line_size()),
				  GFP_KERNEL);
	if (!buffer->buffers)
		goto fail_free_cpumask;

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
EXPORT_SYMBOL_GPL(__ring_buffer_alloc);

/**
 * ring_buffer_free - free a ring buffer.
 * @buffer: the buffer to free.
 */
void
ring_buffer_free(struct ring_buffer *buffer)
{
	int cpu;

	cpuhp_state_remove_instance(CPUHP_TRACE_RB_PREPARE, &buffer->node);

	for_each_buffer_cpu(buffer, cpu)
		rb_free_cpu_buffer(buffer->buffers[cpu]);

	kfree(buffer->buffers);
	free_cpumask_var(buffer->cpumask);

	kfree(buffer);
}
EXPORT_SYMBOL_GPL(ring_buffer_free);

void ring_buffer_set_clock(struct ring_buffer *buffer,
			   u64 (*clock)(void))
{
	buffer->clock = clock;
}

void ring_buffer_set_time_stamp_abs(struct ring_buffer *buffer, bool abs)
{
	buffer->time_stamp_abs = abs;
}

bool ring_buffer_time_stamp_abs(struct ring_buffer *buffer)
{
	return buffer->time_stamp_abs;
}

static void rb_reset_cpu(struct ring_buffer_per_cpu *cpu_buffer);

static inline unsigned long rb_page_entries(struct buffer_page *bpage)
{
	return local_read(&bpage->entries) & RB_WRITE_MASK;
}

static inline unsigned long rb_page_write(struct buffer_page *bpage)
{
	return local_read(&bpage->write) & RB_WRITE_MASK;
}

static int
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

	/* update head page */
	if (head_bit)
		cpu_buffer->head_page = list_entry(next_page,
						struct buffer_page, list);

	/*
	 * change read pointer to make sure any read iterators reset
	 * themselves
	 */
	cpu_buffer->read = 0;

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
		rb_inc_page(cpu_buffer, &tmp_iter_page);

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
			local_sub(BUF_PAGE_SIZE, &cpu_buffer->entries_bytes);
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

static int
rb_insert_pages(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct list_head *pages = &cpu_buffer->new_pages;
	int retries, success;

	raw_spin_lock_irq(&cpu_buffer->reader_lock);
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
	success = 0;
	while (retries--) {
		struct list_head *head_page, *prev_page, *r;
		struct list_head *last_page, *first_page;
		struct list_head *head_page_with_bit;

		head_page = &rb_set_head_page(cpu_buffer)->list;
		if (!head_page)
			break;
		prev_page = head_page->prev;

		first_page = pages->next;
		last_page  = pages->prev;

		head_page_with_bit = (struct list_head *)
				     ((unsigned long)head_page | RB_PAGE_HEAD);

		last_page->next = head_page_with_bit;
		first_page->prev = prev_page;

		r = cmpxchg(&prev_page->next, head_page_with_bit, first_page);

		if (r == head_page_with_bit) {
			/*
			 * yay, we replaced the page pointer to our new list,
			 * now, we just have to update to head page's prev
			 * pointer to point to end of list
			 */
			head_page->prev = last_page;
			success = 1;
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
	raw_spin_unlock_irq(&cpu_buffer->reader_lock);

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
	int success;

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
 * Minimum size is 2 * BUF_PAGE_SIZE.
 *
 * Returns 0 on success and < 0 on failure.
 */
int ring_buffer_resize(struct ring_buffer *buffer, unsigned long size,
			int cpu_id)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long nr_pages;
	int cpu, err = 0;

	/*
	 * Always succeed at resizing a non-existent buffer:
	 */
	if (!buffer)
		return size;

	/* Make sure the requested buffer exists */
	if (cpu_id != RING_BUFFER_ALL_CPUS &&
	    !cpumask_test_cpu(cpu_id, buffer->cpumask))
		return size;

	nr_pages = DIV_ROUND_UP(size, BUF_PAGE_SIZE);

	/* we need a minimum of two pages */
	if (nr_pages < 2)
		nr_pages = 2;

	size = nr_pages * BUF_PAGE_SIZE;

	/*
	 * Don't succeed if resizing is disabled, as a reader might be
	 * manipulating the ring buffer and is expecting a sane state while
	 * this is true.
	 */
	if (atomic_read(&buffer->resize_disabled))
		return -EBUSY;

	/* prevent another thread from changing buffer sizes */
	mutex_lock(&buffer->mutex);

	if (cpu_id == RING_BUFFER_ALL_CPUS) {
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
			if (__rb_allocate_pages(cpu_buffer->nr_pages_to_update,
						&cpu_buffer->new_pages, cpu)) {
				/* not enough memory for new pages */
				err = -ENOMEM;
				goto out_err;
			}
		}

		get_online_cpus();
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
				schedule_work_on(cpu,
						&cpu_buffer->update_pages_work);
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

		put_online_cpus();
	} else {
		/* Make sure this CPU has been initialized */
		if (!cpumask_test_cpu(cpu_id, buffer->cpumask))
			goto out;

		cpu_buffer = buffer->buffers[cpu_id];

		if (nr_pages == cpu_buffer->nr_pages)
			goto out;

		cpu_buffer->nr_pages_to_update = nr_pages -
						cpu_buffer->nr_pages;

		INIT_LIST_HEAD(&cpu_buffer->new_pages);
		if (cpu_buffer->nr_pages_to_update > 0 &&
			__rb_allocate_pages(cpu_buffer->nr_pages_to_update,
					    &cpu_buffer->new_pages, cpu_id)) {
			err = -ENOMEM;
			goto out_err;
		}

		get_online_cpus();

		/* Can't run something on an offline CPU. */
		if (!cpu_online(cpu_id))
			rb_update_pages(cpu_buffer);
		else {
			schedule_work_on(cpu_id,
					 &cpu_buffer->update_pages_work);
			wait_for_completion(&cpu_buffer->update_done);
		}

		cpu_buffer->nr_pages_to_update = 0;
		put_online_cpus();
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

	mutex_unlock(&buffer->mutex);
	return size;

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
	mutex_unlock(&buffer->mutex);
	return err;
}
EXPORT_SYMBOL_GPL(ring_buffer_resize);

void ring_buffer_change_overwrite(struct ring_buffer *buffer, int val)
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

static __always_inline struct ring_buffer_event *
rb_iter_head_event(struct ring_buffer_iter *iter)
{
	return __rb_page_index(iter->head_page, iter->head);
}

static __always_inline unsigned rb_page_commit(struct buffer_page *bpage)
{
	return local_read(&bpage->page->commit);
}

/* Size is determined by what has been committed */
static __always_inline unsigned rb_page_size(struct buffer_page *bpage)
{
	return rb_page_commit(bpage);
}

static __always_inline unsigned
rb_commit_index(struct ring_buffer_per_cpu *cpu_buffer)
{
	return rb_page_commit(cpu_buffer->commit_page);
}

static __always_inline unsigned
rb_event_index(struct ring_buffer_event *event)
{
	unsigned long addr = (unsigned long)event;

	return (addr & ~PAGE_MASK) - BUF_PAGE_HDR_SIZE;
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
		rb_inc_page(cpu_buffer, &iter->head_page);

	iter->read_stamp = iter->head_page->page->time_stamp;
	iter->head = 0;
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
		local_sub(BUF_PAGE_SIZE, &cpu_buffer->entries_bytes);

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
	rb_inc_page(cpu_buffer, &new_head);

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
	struct buffer_page *tail_page = info->tail_page;
	struct ring_buffer_event *event;
	unsigned long length = info->length;

	/*
	 * Only the event that crossed the page boundary
	 * must fill the old tail_page with padding.
	 */
	if (tail >= BUF_PAGE_SIZE) {
		/*
		 * If the page was filled, then we still need
		 * to update the real_end. Reset it to zero
		 * and the reader will ignore it.
		 */
		if (tail == BUF_PAGE_SIZE)
			tail_page->real_end = 0;

		local_sub(length, &tail_page->write);
		return;
	}

	event = __rb_page_index(tail_page, tail);

	/* account for padding bytes */
	local_add(BUF_PAGE_SIZE - tail, &cpu_buffer->entries_bytes);

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
	 * that this space is not used again.
	 *
	 * If we are less than the minimum size, we don't need to
	 * worry about it.
	 */
	if (tail > (BUF_PAGE_SIZE - RB_EVNT_MIN_SIZE)) {
		/* No room for any events */

		/* Mark the rest of the page with padding */
		rb_event_set_padding(event);

		/* Set the write back to the previous setting */
		local_sub(length, &tail_page->write);
		return;
	}

	/* Put in a discarded event */
	event->array[0] = (BUF_PAGE_SIZE - tail) - RB_EVNT_HDR_SIZE;
	event->type_len = RINGBUF_TYPE_PADDING;
	/* time delta must be non zero */
	event->time_delta = 1;

	/* Set write to end of buffer */
	length = (tail + length) - BUF_PAGE_SIZE;
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
	struct ring_buffer *buffer = cpu_buffer->buffer;
	struct buffer_page *next_page;
	int ret;

	next_page = tail_page;

	rb_inc_page(cpu_buffer, &next_page);

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
	if (rb_is_head_page(cpu_buffer, next_page, &tail_page->list)) {

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
			 * Note, if the tail page is also the on the
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

/* Slow path, do not inline */
static noinline struct ring_buffer_event *
rb_add_time_stamp(struct ring_buffer_event *event, u64 delta, bool abs)
{
	if (abs)
		event->type_len = RINGBUF_TYPE_TIME_STAMP;
	else
		event->type_len = RINGBUF_TYPE_TIME_EXTEND;

	/* Not the first event on the page, or not delta? */
	if (abs || rb_event_index(event)) {
		event->time_delta = delta & TS_MASK;
		event->array[0] = delta >> TS_SHIFT;
	} else {
		/* nope, just zero it */
		event->time_delta = 0;
		event->array[0] = 0;
	}

	return skip_time_extend(event);
}

static inline bool rb_event_is_commit(struct ring_buffer_per_cpu *cpu_buffer,
				     struct ring_buffer_event *event);

/**
 * rb_update_event - update event type and data
 * @event: the event to update
 * @type: the type of event
 * @length: the size of the event field in the ring buffer
 *
 * Update the type and data fields of the event. The length
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

	/* Only a commit updates the timestamp */
	if (unlikely(!rb_event_is_commit(cpu_buffer, event)))
		delta = 0;

	/*
	 * If we need to add a timestamp, then we
	 * add it to the start of the reserved space.
	 */
	if (unlikely(info->add_timestamp)) {
		bool abs = ring_buffer_time_stamp_abs(cpu_buffer->buffer);

		event = rb_add_time_stamp(event, info->delta, abs);
		length -= RB_LEN_TIME_EXTEND;
		delta = 0;
	}

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

#ifndef CONFIG_HAVE_UNSTABLE_SCHED_CLOCK
static inline bool sched_clock_stable(void)
{
	return true;
}
#endif

static inline int
rb_try_to_discard(struct ring_buffer_per_cpu *cpu_buffer,
		  struct ring_buffer_event *event)
{
	unsigned long new_index, old_index;
	struct buffer_page *bpage;
	unsigned long index;
	unsigned long addr;

	new_index = rb_event_index(event);
	old_index = new_index + rb_event_ts_length(event);
	addr = (unsigned long)event;
	addr &= PAGE_MASK;

	bpage = READ_ONCE(cpu_buffer->tail_page);

	if (bpage->page == (void *)addr && rb_page_write(bpage) == old_index) {
		unsigned long write_mask =
			local_read(&bpage->write) & ~RB_WRITE_MASK;
		unsigned long event_length = rb_event_length(event);
		/*
		 * This is on the tail page. It is possible that
		 * a write could come in and move the tail page
		 * and write to the next page. That is fine
		 * because we just shorten what is on this page.
		 */
		old_index += write_mask;
		new_index += write_mask;
		index = local_cmpxchg(&bpage->write, old_index, new_index);
		if (index == old_index) {
			/* update counters */
			local_sub(event_length, &cpu_buffer->entries_bytes);
			return 1;
		}
	}

	/* could not discard */
	return 0;
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
		local_set(&cpu_buffer->commit_page->page->commit,
			  rb_page_write(cpu_buffer->commit_page));
		rb_inc_page(cpu_buffer, &cpu_buffer->commit_page);
		/* Only update the write stamp if the page has an event */
		if (rb_page_write(cpu_buffer->commit_page))
			cpu_buffer->write_stamp =
				cpu_buffer->commit_page->page->time_stamp;
		/* add barrier to keep gcc from optimizing too much */
		barrier();
	}
	while (rb_commit_index(cpu_buffer) !=
	       rb_page_write(cpu_buffer->commit_page)) {

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

static __always_inline bool
rb_event_is_commit(struct ring_buffer_per_cpu *cpu_buffer,
		   struct ring_buffer_event *event)
{
	unsigned long addr = (unsigned long)event;
	unsigned long index;

	index = rb_event_index(event);
	addr &= PAGE_MASK;

	return cpu_buffer->commit_page->page == (void *)addr &&
		rb_commit_index(cpu_buffer) == index;
}

static __always_inline void
rb_update_write_stamp(struct ring_buffer_per_cpu *cpu_buffer,
		      struct ring_buffer_event *event)
{
	u64 delta;

	/*
	 * The event first in the commit queue updates the
	 * time stamp.
	 */
	if (rb_event_is_commit(cpu_buffer, event)) {
		/*
		 * A commit event that is first on a page
		 * updates the write timestamp with the page stamp
		 */
		if (!rb_event_index(event))
			cpu_buffer->write_stamp =
				cpu_buffer->commit_page->page->time_stamp;
		else if (event->type_len == RINGBUF_TYPE_TIME_EXTEND) {
			delta = ring_buffer_event_time_stamp(event);
			cpu_buffer->write_stamp += delta;
		} else if (event->type_len == RINGBUF_TYPE_TIME_STAMP) {
			delta = ring_buffer_event_time_stamp(event);
			cpu_buffer->write_stamp = delta;
		} else
			cpu_buffer->write_stamp += event->time_delta;
	}
}

static void rb_commit(struct ring_buffer_per_cpu *cpu_buffer,
		      struct ring_buffer_event *event)
{
	local_inc(&cpu_buffer->entries);
	rb_update_write_stamp(cpu_buffer, event);
	rb_end_commit(cpu_buffer);
}

static __always_inline void
rb_wakeups(struct ring_buffer *buffer, struct ring_buffer_per_cpu *cpu_buffer)
{
	size_t nr_pages;
	size_t dirty;
	size_t full;

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

	full = cpu_buffer->shortest_full;
	nr_pages = cpu_buffer->nr_pages;
	dirty = ring_buffer_nr_dirty_pages(buffer, cpu_buffer->cpu);
	if (full && nr_pages && (dirty * 100) <= full * nr_pages)
		return;

	cpu_buffer->irq_work.wakeup_full = true;
	cpu_buffer->irq_work.full_waiters_pending = false;
	/* irq_work_queue() supplies it's own memory barriers */
	irq_work_queue(&cpu_buffer->irq_work.work);
}

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
 *  bit 0 =  NMI context
 *  bit 1 =  IRQ context
 *  bit 2 =  SoftIRQ context
 *  bit 3 =  normal context.
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
 */

static __always_inline int
trace_recursive_lock(struct ring_buffer_per_cpu *cpu_buffer)
{
	unsigned int val = cpu_buffer->current_context;
	unsigned long pc = preempt_count();
	int bit;

	if (!(pc & (NMI_MASK | HARDIRQ_MASK | SOFTIRQ_OFFSET)))
		bit = RB_CTX_NORMAL;
	else
		bit = pc & NMI_MASK ? RB_CTX_NMI :
			pc & HARDIRQ_MASK ? RB_CTX_IRQ : RB_CTX_SOFTIRQ;

	if (unlikely(val & (1 << (bit + cpu_buffer->nest))))
		return 1;

	val |= (1 << (bit + cpu_buffer->nest));
	cpu_buffer->current_context = val;

	return 0;
}

static __always_inline void
trace_recursive_unlock(struct ring_buffer_per_cpu *cpu_buffer)
{
	cpu_buffer->current_context &=
		cpu_buffer->current_context - (1 << cpu_buffer->nest);
}

/* The recursive locking above uses 4 bits */
#define NESTED_BITS 4

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
void ring_buffer_nest_start(struct ring_buffer *buffer)
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
void ring_buffer_nest_end(struct ring_buffer *buffer)
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
 * @event: The event pointer to commit.
 *
 * This commits the data to the ring buffer, and releases any locks held.
 *
 * Must be paired with ring_buffer_lock_reserve.
 */
int ring_buffer_unlock_commit(struct ring_buffer *buffer,
			      struct ring_buffer_event *event)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	int cpu = raw_smp_processor_id();

	cpu_buffer = buffer->buffers[cpu];

	rb_commit(cpu_buffer, event);

	rb_wakeups(buffer, cpu_buffer);

	trace_recursive_unlock(cpu_buffer);

	preempt_enable_notrace();

	return 0;
}
EXPORT_SYMBOL_GPL(ring_buffer_unlock_commit);

static noinline void
rb_handle_timestamp(struct ring_buffer_per_cpu *cpu_buffer,
		    struct rb_event_info *info)
{
	WARN_ONCE(info->delta > (1ULL << 59),
		  KERN_WARNING "Delta way too big! %llu ts=%llu write stamp = %llu\n%s",
		  (unsigned long long)info->delta,
		  (unsigned long long)info->ts,
		  (unsigned long long)cpu_buffer->write_stamp,
		  sched_clock_stable() ? "" :
		  "If you just came from a suspend/resume,\n"
		  "please switch to the trace global clock:\n"
		  "  echo global > /sys/kernel/debug/tracing/trace_clock\n"
		  "or add trace_clock=global to the kernel command line\n");
	info->add_timestamp = 1;
}

static struct ring_buffer_event *
__rb_reserve_next(struct ring_buffer_per_cpu *cpu_buffer,
		  struct rb_event_info *info)
{
	struct ring_buffer_event *event;
	struct buffer_page *tail_page;
	unsigned long tail, write;

	/*
	 * If the time delta since the last event is too big to
	 * hold in the time field of the event, then we append a
	 * TIME EXTEND event ahead of the data event.
	 */
	if (unlikely(info->add_timestamp))
		info->length += RB_LEN_TIME_EXTEND;

	/* Don't let the compiler play games with cpu_buffer->tail_page */
	tail_page = info->tail_page = READ_ONCE(cpu_buffer->tail_page);
	write = local_add_return(info->length, &tail_page->write);

	/* set write to only the index of the write */
	write &= RB_WRITE_MASK;
	tail = write - info->length;

	/*
	 * If this is the first commit on the page, then it has the same
	 * timestamp as the page itself.
	 */
	if (!tail && !ring_buffer_time_stamp_abs(cpu_buffer->buffer))
		info->delta = 0;

	/* See if we shot pass the end of this buffer page */
	if (unlikely(write > BUF_PAGE_SIZE))
		return rb_move_tail(cpu_buffer, tail, info);

	/* We reserved something on the buffer */

	event = __rb_page_index(tail_page, tail);
	rb_update_event(cpu_buffer, event, info);

	local_inc(&tail_page->entries);

	/*
	 * If this is the first commit on the page, then update
	 * its timestamp.
	 */
	if (!tail)
		tail_page->page->time_stamp = info->ts;

	/* account for these added bytes */
	local_add(info->length, &cpu_buffer->entries_bytes);

	return event;
}

static __always_inline struct ring_buffer_event *
rb_reserve_next_event(struct ring_buffer *buffer,
		      struct ring_buffer_per_cpu *cpu_buffer,
		      unsigned long length)
{
	struct ring_buffer_event *event;
	struct rb_event_info info;
	int nr_loops = 0;
	u64 diff;

	rb_start_commit(cpu_buffer);

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
 again:
	info.add_timestamp = 0;
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

	info.ts = rb_time_stamp(cpu_buffer->buffer);
	diff = info.ts - cpu_buffer->write_stamp;

	/* make sure this diff is calculated here */
	barrier();

	if (ring_buffer_time_stamp_abs(buffer)) {
		info.delta = info.ts;
		rb_handle_timestamp(cpu_buffer, &info);
	} else /* Did the write stamp get updated already? */
		if (likely(info.ts >= cpu_buffer->write_stamp)) {
		info.delta = diff;
		if (unlikely(test_time_stamp(info.delta)))
			rb_handle_timestamp(cpu_buffer, &info);
	}

	event = __rb_reserve_next(cpu_buffer, &info);

	if (unlikely(PTR_ERR(event) == -EAGAIN)) {
		if (info.add_timestamp)
			info.length -= RB_LEN_TIME_EXTEND;
		goto again;
	}

	if (!event)
		goto out_fail;

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
ring_buffer_lock_reserve(struct ring_buffer *buffer, unsigned long length)
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

	if (unlikely(length > BUF_MAX_DATA_SIZE))
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

	addr &= PAGE_MASK;

	/* Do the likely case first */
	if (likely(bpage->page == (void *)addr)) {
		local_dec(&bpage->entries);
		return;
	}

	/*
	 * Because the commit page may be on the reader page we
	 * start with the next page and check the end loop there.
	 */
	rb_inc_page(cpu_buffer, &bpage);
	start = bpage;
	do {
		if (bpage->page == (void *)addr) {
			local_dec(&bpage->entries);
			return;
		}
		rb_inc_page(cpu_buffer, &bpage);
	} while (bpage != start);

	/* commit not part of this buffer?? */
	RB_WARN_ON(cpu_buffer, 1);
}

/**
 * ring_buffer_commit_discard - discard an event that has not been committed
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
void ring_buffer_discard_commit(struct ring_buffer *buffer,
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

	/*
	 * The commit is still visible by the reader, so we
	 * must still update the timestamp.
	 */
	rb_update_write_stamp(cpu_buffer, event);
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
int ring_buffer_write(struct ring_buffer *buffer,
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

	if (length > BUF_MAX_DATA_SIZE)
		goto out;

	if (unlikely(trace_recursive_lock(cpu_buffer)))
		goto out;

	event = rb_reserve_next_event(buffer, cpu_buffer, length);
	if (!event)
		goto out_unlock;

	body = rb_event_data(event);

	memcpy(body, data, length);

	rb_commit(cpu_buffer, event);

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

	return reader->read == rb_page_commit(reader) &&
		(commit == reader ||
		 (commit == head &&
		  head->read == rb_page_commit(commit)));
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
void ring_buffer_record_disable(struct ring_buffer *buffer)
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
void ring_buffer_record_enable(struct ring_buffer *buffer)
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
void ring_buffer_record_off(struct ring_buffer *buffer)
{
	unsigned int rd;
	unsigned int new_rd;

	do {
		rd = atomic_read(&buffer->record_disabled);
		new_rd = rd | RB_BUFFER_OFF;
	} while (atomic_cmpxchg(&buffer->record_disabled, rd, new_rd) != rd);
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
void ring_buffer_record_on(struct ring_buffer *buffer)
{
	unsigned int rd;
	unsigned int new_rd;

	do {
		rd = atomic_read(&buffer->record_disabled);
		new_rd = rd & ~RB_BUFFER_OFF;
	} while (atomic_cmpxchg(&buffer->record_disabled, rd, new_rd) != rd);
}
EXPORT_SYMBOL_GPL(ring_buffer_record_on);

/**
 * ring_buffer_record_is_on - return true if the ring buffer can write
 * @buffer: The ring buffer to see if write is enabled
 *
 * Returns true if the ring buffer is in a state that it accepts writes.
 */
bool ring_buffer_record_is_on(struct ring_buffer *buffer)
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
bool ring_buffer_record_is_set_on(struct ring_buffer *buffer)
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
void ring_buffer_record_disable_cpu(struct ring_buffer *buffer, int cpu)
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
void ring_buffer_record_enable_cpu(struct ring_buffer *buffer, int cpu)
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
u64 ring_buffer_oldest_event_ts(struct ring_buffer *buffer, int cpu)
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
 * ring_buffer_bytes_cpu - get the number of bytes consumed in a cpu buffer
 * @buffer: The ring buffer
 * @cpu: The per CPU buffer to read from.
 */
unsigned long ring_buffer_bytes_cpu(struct ring_buffer *buffer, int cpu)
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
unsigned long ring_buffer_entries_cpu(struct ring_buffer *buffer, int cpu)
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
unsigned long ring_buffer_overrun_cpu(struct ring_buffer *buffer, int cpu)
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
ring_buffer_commit_overrun_cpu(struct ring_buffer *buffer, int cpu)
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
ring_buffer_dropped_events_cpu(struct ring_buffer *buffer, int cpu)
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
ring_buffer_read_events_cpu(struct ring_buffer *buffer, int cpu)
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
unsigned long ring_buffer_entries(struct ring_buffer *buffer)
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
unsigned long ring_buffer_overruns(struct ring_buffer *buffer)
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

	iter->cache_reader_page = iter->head_page;
	iter->cache_read = cpu_buffer->read;

	if (iter->head)
		iter->read_stamp = cpu_buffer->read_stamp;
	else
		iter->read_stamp = iter->head_page->page->time_stamp;
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
	unsigned commit;

	cpu_buffer = iter->cpu_buffer;

	/* Remember, trace recording is off when iterator is in use */
	reader = cpu_buffer->reader_page;
	head_page = cpu_buffer->head_page;
	commit_page = cpu_buffer->commit_page;
	commit = rb_page_commit(commit_page);

	return ((iter->head_page == commit_page && iter->head == commit) ||
		(iter->head_page == reader && commit_page == head_page &&
		 head_page->read == commit &&
		 iter->head == rb_page_commit(cpu_buffer->reader_page)));
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
		delta = ring_buffer_event_time_stamp(event);
		cpu_buffer->read_stamp += delta;
		return;

	case RINGBUF_TYPE_TIME_STAMP:
		delta = ring_buffer_event_time_stamp(event);
		cpu_buffer->read_stamp = delta;
		return;

	case RINGBUF_TYPE_DATA:
		cpu_buffer->read_stamp += event->time_delta;
		return;

	default:
		BUG();
	}
	return;
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
		delta = ring_buffer_event_time_stamp(event);
		iter->read_stamp += delta;
		return;

	case RINGBUF_TYPE_TIME_STAMP:
		delta = ring_buffer_event_time_stamp(event);
		iter->read_stamp = delta;
		return;

	case RINGBUF_TYPE_DATA:
		iter->read_stamp += event->time_delta;
		return;

	default:
		BUG();
	}
	return;
}

static struct buffer_page *
rb_get_reader_page(struct ring_buffer_per_cpu *cpu_buffer)
{
	struct buffer_page *reader = NULL;
	unsigned long overwrite;
	unsigned long flags;
	int nr_loops = 0;
	int ret;

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
	rb_set_list_to_head(cpu_buffer, &cpu_buffer->reader_page->list);

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

	/*
	 * Yay! We succeeded in replacing the page.
	 *
	 * Now make the new head point back to the reader page.
	 */
	rb_list_head(reader->list.next)->prev = &cpu_buffer->reader_page->list;
	rb_inc_page(cpu_buffer, &cpu_buffer->head_page);

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
}

static void rb_advance_iter(struct ring_buffer_iter *iter)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct ring_buffer_event *event;
	unsigned length;

	cpu_buffer = iter->cpu_buffer;

	/*
	 * Check if we are at the end of the buffer.
	 */
	if (iter->head >= rb_page_size(iter->head_page)) {
		/* discarded commits can make the page empty */
		if (iter->head_page == cpu_buffer->commit_page)
			return;
		rb_inc_iter(iter);
		return;
	}

	event = rb_iter_head_event(iter);

	length = rb_event_length(event);

	/*
	 * This should not be called to advance the header if we are
	 * at the tail of the buffer.
	 */
	if (RB_WARN_ON(cpu_buffer,
		       (iter->head_page == cpu_buffer->commit_page) &&
		       (iter->head + length > rb_commit_index(cpu_buffer))))
		return;

	rb_update_iter_read_stamp(iter, event);

	iter->head += length;

	/* check for end of page padding */
	if ((iter->head >= rb_page_size(iter->head_page)) &&
	    (iter->head_page != cpu_buffer->commit_page))
		rb_inc_iter(iter);
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
			*ts = ring_buffer_event_time_stamp(event);
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
		BUG();
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(ring_buffer_peek);

static struct ring_buffer_event *
rb_iter_peek(struct ring_buffer_iter *iter, u64 *ts)
{
	struct ring_buffer *buffer;
	struct ring_buffer_per_cpu *cpu_buffer;
	struct ring_buffer_event *event;
	int nr_loops = 0;

	if (ts)
		*ts = 0;

	cpu_buffer = iter->cpu_buffer;
	buffer = cpu_buffer->buffer;

	/*
	 * Check if someone performed a consuming read to
	 * the buffer. A consuming read invalidates the iterator
	 * and we need to reset the iterator in this case.
	 */
	if (unlikely(iter->cache_read != cpu_buffer->read ||
		     iter->cache_reader_page != cpu_buffer->reader_page))
		rb_iter_reset(iter);

 again:
	if (ring_buffer_iter_empty(iter))
		return NULL;

	/*
	 * We repeat when a time extend is encountered or we hit
	 * the end of the page. Since the time extend is always attached
	 * to a data event, we should never loop more than three times.
	 * Once for going to next page, once on time extend, and
	 * finally once to get the event.
	 * (We never hit the following condition more than thrice).
	 */
	if (RB_WARN_ON(cpu_buffer, ++nr_loops > 3))
		return NULL;

	if (rb_per_cpu_empty(cpu_buffer))
		return NULL;

	if (iter->head >= rb_page_size(iter->head_page)) {
		rb_inc_iter(iter);
		goto again;
	}

	event = rb_iter_head_event(iter);

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
			*ts = ring_buffer_event_time_stamp(event);
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
		BUG();
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
	return;
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
ring_buffer_peek(struct ring_buffer *buffer, int cpu, u64 *ts,
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
ring_buffer_consume(struct ring_buffer *buffer, int cpu, u64 *ts,
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
 * through the buffer.  Memory is allocated, buffer recording
 * is disabled, and the iterator pointer is returned to the caller.
 *
 * Disabling buffer recording prevents the reading from being
 * corrupted. This is not a consuming read, so a producer is not
 * expected.
 *
 * After a sequence of ring_buffer_read_prepare calls, the user is
 * expected to make at least one call to ring_buffer_read_prepare_sync.
 * Afterwards, ring_buffer_read_start is invoked to get things going
 * for real.
 *
 * This overall must be paired with ring_buffer_read_finish.
 */
struct ring_buffer_iter *
ring_buffer_read_prepare(struct ring_buffer *buffer, int cpu, gfp_t flags)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct ring_buffer_iter *iter;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return NULL;

	iter = kmalloc(sizeof(*iter), flags);
	if (!iter)
		return NULL;

	cpu_buffer = buffer->buffers[cpu];

	iter->cpu_buffer = cpu_buffer;

	atomic_inc(&buffer->resize_disabled);
	atomic_inc(&cpu_buffer->record_disabled);

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
 * This re-enables the recording to the buffer, and frees the
 * iterator.
 */
void
ring_buffer_read_finish(struct ring_buffer_iter *iter)
{
	struct ring_buffer_per_cpu *cpu_buffer = iter->cpu_buffer;
	unsigned long flags;

	/*
	 * Ring buffer is disabled from recording, here's a good place
	 * to check the integrity of the ring buffer.
	 * Must prevent readers from trying to read, as the check
	 * clears the HEAD page and readers require it.
	 */
	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);
	rb_check_pages(cpu_buffer);
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);

	atomic_dec(&cpu_buffer->record_disabled);
	atomic_dec(&cpu_buffer->buffer->resize_disabled);
	kfree(iter);
}
EXPORT_SYMBOL_GPL(ring_buffer_read_finish);

/**
 * ring_buffer_read - read the next item in the ring buffer by the iterator
 * @iter: The ring buffer iterator
 * @ts: The time stamp of the event read.
 *
 * This reads the next event in the ring buffer and increments the iterator.
 */
struct ring_buffer_event *
ring_buffer_read(struct ring_buffer_iter *iter, u64 *ts)
{
	struct ring_buffer_event *event;
	struct ring_buffer_per_cpu *cpu_buffer = iter->cpu_buffer;
	unsigned long flags;

	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);
 again:
	event = rb_iter_peek(iter, ts);
	if (!event)
		goto out;

	if (event->type_len == RINGBUF_TYPE_PADDING)
		goto again;

	rb_advance_iter(iter);
 out:
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);

	return event;
}
EXPORT_SYMBOL_GPL(ring_buffer_read);

/**
 * ring_buffer_size - return the size of the ring buffer (in bytes)
 * @buffer: The ring buffer.
 */
unsigned long ring_buffer_size(struct ring_buffer *buffer, int cpu)
{
	/*
	 * Earlier, this method returned
	 *	BUF_PAGE_SIZE * buffer->nr_pages
	 * Since the nr_pages field is now removed, we have converted this to
	 * return the per cpu buffer value.
	 */
	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return 0;

	return BUF_PAGE_SIZE * buffer->buffers[cpu]->nr_pages;
}
EXPORT_SYMBOL_GPL(ring_buffer_size);

static void
rb_reset_cpu(struct ring_buffer_per_cpu *cpu_buffer)
{
	rb_head_page_deactivate(cpu_buffer);

	cpu_buffer->head_page
		= list_entry(cpu_buffer->pages, struct buffer_page, list);
	local_set(&cpu_buffer->head_page->write, 0);
	local_set(&cpu_buffer->head_page->entries, 0);
	local_set(&cpu_buffer->head_page->page->commit, 0);

	cpu_buffer->head_page->read = 0;

	cpu_buffer->tail_page = cpu_buffer->head_page;
	cpu_buffer->commit_page = cpu_buffer->head_page;

	INIT_LIST_HEAD(&cpu_buffer->reader_page->list);
	INIT_LIST_HEAD(&cpu_buffer->new_pages);
	local_set(&cpu_buffer->reader_page->write, 0);
	local_set(&cpu_buffer->reader_page->entries, 0);
	local_set(&cpu_buffer->reader_page->page->commit, 0);
	cpu_buffer->reader_page->read = 0;

	local_set(&cpu_buffer->entries_bytes, 0);
	local_set(&cpu_buffer->overrun, 0);
	local_set(&cpu_buffer->commit_overrun, 0);
	local_set(&cpu_buffer->dropped_events, 0);
	local_set(&cpu_buffer->entries, 0);
	local_set(&cpu_buffer->committing, 0);
	local_set(&cpu_buffer->commits, 0);
	local_set(&cpu_buffer->pages_touched, 0);
	local_set(&cpu_buffer->pages_read, 0);
	cpu_buffer->last_pages_touch = 0;
	cpu_buffer->shortest_full = 0;
	cpu_buffer->read = 0;
	cpu_buffer->read_bytes = 0;

	cpu_buffer->write_stamp = 0;
	cpu_buffer->read_stamp = 0;

	cpu_buffer->lost_events = 0;
	cpu_buffer->last_overrun = 0;

	rb_head_page_activate(cpu_buffer);
}

/**
 * ring_buffer_reset_cpu - reset a ring buffer per CPU buffer
 * @buffer: The ring buffer to reset a per cpu buffer of
 * @cpu: The CPU buffer to be reset
 */
void ring_buffer_reset_cpu(struct ring_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer = buffer->buffers[cpu];
	unsigned long flags;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return;

	atomic_inc(&buffer->resize_disabled);
	atomic_inc(&cpu_buffer->record_disabled);

	/* Make sure all commits have finished */
	synchronize_rcu();

	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);

	if (RB_WARN_ON(cpu_buffer, local_read(&cpu_buffer->committing)))
		goto out;

	arch_spin_lock(&cpu_buffer->lock);

	rb_reset_cpu(cpu_buffer);

	arch_spin_unlock(&cpu_buffer->lock);

 out:
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);

	atomic_dec(&cpu_buffer->record_disabled);
	atomic_dec(&buffer->resize_disabled);
}
EXPORT_SYMBOL_GPL(ring_buffer_reset_cpu);

/**
 * ring_buffer_reset - reset a ring buffer
 * @buffer: The ring buffer to reset all cpu buffers
 */
void ring_buffer_reset(struct ring_buffer *buffer)
{
	int cpu;

	for_each_buffer_cpu(buffer, cpu)
		ring_buffer_reset_cpu(buffer, cpu);
}
EXPORT_SYMBOL_GPL(ring_buffer_reset);

/**
 * rind_buffer_empty - is the ring buffer empty?
 * @buffer: The ring buffer to test
 */
bool ring_buffer_empty(struct ring_buffer *buffer)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long flags;
	bool dolock;
	int cpu;
	int ret;

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
bool ring_buffer_empty_cpu(struct ring_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	unsigned long flags;
	bool dolock;
	int ret;

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
 *
 * This function is useful for tracers that want to take a "snapshot"
 * of a CPU buffer and has another back up buffer lying around.
 * it is expected that the tracer handles the cpu buffer not being
 * used at the moment.
 */
int ring_buffer_swap_cpu(struct ring_buffer *buffer_a,
			 struct ring_buffer *buffer_b, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer_a;
	struct ring_buffer_per_cpu *cpu_buffer_b;
	int ret = -EINVAL;

	if (!cpumask_test_cpu(cpu, buffer_a->cpumask) ||
	    !cpumask_test_cpu(cpu, buffer_b->cpumask))
		goto out;

	cpu_buffer_a = buffer_a->buffers[cpu];
	cpu_buffer_b = buffer_b->buffers[cpu];

	/* At least make sure the two buffers are somewhat the same */
	if (cpu_buffer_a->nr_pages != cpu_buffer_b->nr_pages)
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
void *ring_buffer_alloc_read_page(struct ring_buffer *buffer, int cpu)
{
	struct ring_buffer_per_cpu *cpu_buffer;
	struct buffer_data_page *bpage = NULL;
	unsigned long flags;
	struct page *page;

	if (!cpumask_test_cpu(cpu, buffer->cpumask))
		return ERR_PTR(-ENODEV);

	cpu_buffer = buffer->buffers[cpu];
	local_irq_save(flags);
	arch_spin_lock(&cpu_buffer->lock);

	if (cpu_buffer->free_page) {
		bpage = cpu_buffer->free_page;
		cpu_buffer->free_page = NULL;
	}

	arch_spin_unlock(&cpu_buffer->lock);
	local_irq_restore(flags);

	if (bpage)
		goto out;

	page = alloc_pages_node(cpu_to_node(cpu),
				GFP_KERNEL | __GFP_NORETRY, 0);
	if (!page)
		return ERR_PTR(-ENOMEM);

	bpage = page_address(page);

 out:
	rb_init_page(bpage);

	return bpage;
}
EXPORT_SYMBOL_GPL(ring_buffer_alloc_read_page);

/**
 * ring_buffer_free_read_page - free an allocated read page
 * @buffer: the buffer the page was allocate for
 * @cpu: the cpu buffer the page came from
 * @data: the page to free
 *
 * Free a page allocated from ring_buffer_alloc_read_page.
 */
void ring_buffer_free_read_page(struct ring_buffer *buffer, int cpu, void *data)
{
	struct ring_buffer_per_cpu *cpu_buffer = buffer->buffers[cpu];
	struct buffer_data_page *bpage = data;
	struct page *page = virt_to_page(bpage);
	unsigned long flags;

	/* If the page is still in use someplace else, we can't reuse it */
	if (page_ref_count(page) > 1)
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
	free_page((unsigned long)bpage);
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
 *	ret = ring_buffer_read_page(buffer, &rpage, len, cpu, 0);
 *	if (ret >= 0)
 *		process_page(rpage, ret);
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
int ring_buffer_read_page(struct ring_buffer *buffer,
			  void **data_page, size_t len, int cpu, int full)
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

	if (!data_page)
		goto out;

	bpage = *data_page;
	if (!bpage)
		goto out;

	raw_spin_lock_irqsave(&cpu_buffer->reader_lock, flags);

	reader = rb_get_reader_page(cpu_buffer);
	if (!reader)
		goto out_unlock;

	event = rb_reader_event(cpu_buffer);

	read = reader->read;
	commit = rb_page_commit(reader);

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
	    cpu_buffer->reader_page == cpu_buffer->commit_page) {
		struct buffer_data_page *rpage = cpu_buffer->reader_page->page;
		unsigned int rpos = read;
		unsigned int pos = 0;
		unsigned int size;

		if (full)
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
		cpu_buffer->read_bytes += BUF_PAGE_SIZE;

		/* swap the pages */
		rb_init_page(bpage);
		bpage = reader->page;
		reader->page = *data_page;
		local_set(&reader->write, 0);
		local_set(&reader->entries, 0);
		reader->read = 0;
		*data_page = bpage;

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
		if (BUF_PAGE_SIZE - commit >= sizeof(missed_events)) {
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
	if (commit < BUF_PAGE_SIZE)
		memset(&bpage->data[commit], 0, BUF_PAGE_SIZE - commit);

 out_unlock:
	raw_spin_unlock_irqrestore(&cpu_buffer->reader_lock, flags);

 out:
	return ret;
}
EXPORT_SYMBOL_GPL(ring_buffer_read_page);

/*
 * We only allocate new buffers, never free them if the CPU goes down.
 * If we were to free the buffer, then the user would lose any trace that was in
 * the buffer.
 */
int trace_rb_cpu_prepare(unsigned int cpu, struct hlist_node *node)
{
	struct ring_buffer *buffer;
	long nr_pages_same;
	int cpu_i;
	unsigned long nr_pages;

	buffer = container_of(node, struct ring_buffer, node);
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
	struct ring_buffer	*buffer;
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
	ring_buffer_unlock_commit(data->buffer, event);

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
	struct ring_buffer *buffer;
	int cpu;
	int ret = 0;

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
		rb_threads[cpu] = kthread_create(rb_test, &rb_data[cpu],
						 "rbtester/%d", cpu);
		if (WARN_ON(IS_ERR(rb_threads[cpu]))) {
			pr_cont("FAILED\n");
			ret = PTR_ERR(rb_threads[cpu]);
			goto out_free;
		}

		kthread_bind(rb_threads[cpu], cpu);
 		wake_up_process(rb_threads[cpu]);
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
		if (total_lost)
			pr_info(" With dropped events, record len and size may not match\n"
				" alloced and written from above\n");
		if (!total_lost) {
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
