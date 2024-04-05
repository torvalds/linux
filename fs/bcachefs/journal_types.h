/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_JOURNAL_TYPES_H
#define _BCACHEFS_JOURNAL_TYPES_H

#include <linux/cache.h>
#include <linux/workqueue.h>

#include "alloc_types.h"
#include "super_types.h"
#include "fifo.h"

#define JOURNAL_BUF_BITS	2
#define JOURNAL_BUF_NR		(1U << JOURNAL_BUF_BITS)
#define JOURNAL_BUF_MASK	(JOURNAL_BUF_NR - 1)

/*
 * We put JOURNAL_BUF_NR of these in struct journal; we used them for writes to
 * the journal that are being staged or in flight.
 */
struct journal_buf {
	struct closure		io;
	struct jset		*data;

	__BKEY_PADDED(key, BCH_REPLICAS_MAX);
	struct bch_devs_list	devs_written;

	struct closure_waitlist	wait;
	u64			last_seq;	/* copy of data->last_seq */
	long			expires;
	u64			flush_time;

	unsigned		buf_size;	/* size in bytes of @data */
	unsigned		sectors;	/* maximum size for current entry */
	unsigned		disk_sectors;	/* maximum size entry could have been, if
						   buf_size was bigger */
	unsigned		u64s_reserved;
	bool			noflush:1;	/* write has already been kicked off, and was noflush */
	bool			must_flush:1;	/* something wants a flush */
	bool			separate_flush:1;
	bool			need_flush_to_write_buffer:1;
	bool			write_started:1;
	bool			write_allocated:1;
	bool			write_done:1;
	u8			idx;
};

/*
 * Something that makes a journal entry dirty - i.e. a btree node that has to be
 * flushed:
 */

enum journal_pin_type {
	JOURNAL_PIN_btree,
	JOURNAL_PIN_key_cache,
	JOURNAL_PIN_other,
	JOURNAL_PIN_NR,
};

struct journal_entry_pin_list {
	struct list_head		list[JOURNAL_PIN_NR];
	struct list_head		flushed;
	atomic_t			count;
	struct bch_devs_list		devs;
};

struct journal;
struct journal_entry_pin;
typedef int (*journal_pin_flush_fn)(struct journal *j,
				struct journal_entry_pin *, u64);

struct journal_entry_pin {
	struct list_head		list;
	journal_pin_flush_fn		flush;
	u64				seq;
};

struct journal_res {
	bool			ref;
	u8			idx;
	u16			u64s;
	u32			offset;
	u64			seq;
};

union journal_res_state {
	struct {
		atomic64_t	counter;
	};

	struct {
		u64		v;
	};

	struct {
		u64		cur_entry_offset:20,
				idx:2,
				unwritten_idx:2,
				buf0_count:10,
				buf1_count:10,
				buf2_count:10,
				buf3_count:10;
	};
};

/* bytes: */
#define JOURNAL_ENTRY_SIZE_MIN		(64U << 10) /* 64k */
#define JOURNAL_ENTRY_SIZE_MAX		(4U  << 20) /* 4M */

/*
 * We stash some journal state as sentinal values in cur_entry_offset:
 * note - cur_entry_offset is in units of u64s
 */
#define JOURNAL_ENTRY_OFFSET_MAX	((1U << 20) - 1)

#define JOURNAL_ENTRY_CLOSED_VAL	(JOURNAL_ENTRY_OFFSET_MAX - 1)
#define JOURNAL_ENTRY_ERROR_VAL		(JOURNAL_ENTRY_OFFSET_MAX)

struct journal_space {
	/* Units of 512 bytes sectors: */
	unsigned	next_entry; /* How big the next journal entry can be */
	unsigned	total;
};

enum journal_space_from {
	journal_space_discarded,
	journal_space_clean_ondisk,
	journal_space_clean,
	journal_space_total,
	journal_space_nr,
};

enum journal_flags {
	JOURNAL_REPLAY_DONE,
	JOURNAL_STARTED,
	JOURNAL_MAY_SKIP_FLUSH,
	JOURNAL_NEED_FLUSH_WRITE,
};

/* Reasons we may fail to get a journal reservation: */
#define JOURNAL_ERRORS()		\
	x(ok)				\
	x(retry)			\
	x(blocked)			\
	x(max_in_flight)		\
	x(journal_full)			\
	x(journal_pin_full)		\
	x(journal_stuck)		\
	x(insufficient_devices)

enum journal_errors {
#define x(n)	JOURNAL_ERR_##n,
	JOURNAL_ERRORS()
#undef x
};

typedef DARRAY(u64)		darray_u64;

struct journal_bio {
	struct bch_dev		*ca;
	unsigned		buf_idx;

	struct bio		bio;
};

/* Embedded in struct bch_fs */
struct journal {
	/* Fastpath stuff up front: */
	struct {

	union journal_res_state reservations;
	enum bch_watermark	watermark;

	} __aligned(SMP_CACHE_BYTES);

	unsigned long		flags;

	/* Max size of current journal entry */
	unsigned		cur_entry_u64s;
	unsigned		cur_entry_sectors;

	/* Reserved space in journal entry to be used just prior to write */
	unsigned		entry_u64s_reserved;


	/*
	 * 0, or -ENOSPC if waiting on journal reclaim, or -EROFS if
	 * insufficient devices:
	 */
	enum journal_errors	cur_entry_error;

	unsigned		buf_size_want;
	/*
	 * We may queue up some things to be journalled (log messages) before
	 * the journal has actually started - stash them here:
	 */
	darray_u64		early_journal_entries;

	/*
	 * Protects journal_buf->data, when accessing without a jorunal
	 * reservation: for synchronization between the btree write buffer code
	 * and the journal write path:
	 */
	struct mutex		buf_lock;
	/*
	 * Two journal entries -- one is currently open for new entries, the
	 * other is possibly being written out.
	 */
	struct journal_buf	buf[JOURNAL_BUF_NR];

	spinlock_t		lock;

	/* if nonzero, we may not open a new journal entry: */
	unsigned		blocked;

	/* Used when waiting because the journal was full */
	wait_queue_head_t	wait;
	struct closure_waitlist	async_wait;

	struct delayed_work	write_work;
	struct workqueue_struct *wq;

	/* Sequence number of most recent journal entry (last entry in @pin) */
	atomic64_t		seq;

	/* seq, last_seq from the most recent journal entry successfully written */
	u64			seq_ondisk;
	u64			flushed_seq_ondisk;
	u64			last_seq_ondisk;
	u64			err_seq;
	u64			last_empty_seq;

	/*
	 * FIFO of journal entries whose btree updates have not yet been
	 * written out.
	 *
	 * Each entry is a reference count. The position in the FIFO is the
	 * entry's sequence number relative to @seq.
	 *
	 * The journal entry itself holds a reference count, put when the
	 * journal entry is written out. Each btree node modified by the journal
	 * entry also holds a reference count, put when the btree node is
	 * written.
	 *
	 * When a reference count reaches zero, the journal entry is no longer
	 * needed. When all journal entries in the oldest journal bucket are no
	 * longer needed, the bucket can be discarded and reused.
	 */
	struct {
		u64 front, back, size, mask;
		struct journal_entry_pin_list *data;
	}			pin;

	struct journal_space	space[journal_space_nr];

	u64			replay_journal_seq;
	u64			replay_journal_seq_end;

	struct write_point	wp;
	spinlock_t		err_lock;

	struct mutex		reclaim_lock;
	/*
	 * Used for waiting until journal reclaim has freed up space in the
	 * journal:
	 */
	wait_queue_head_t	reclaim_wait;
	struct task_struct	*reclaim_thread;
	bool			reclaim_kicked;
	unsigned long		next_reclaim;
	u64			nr_direct_reclaim;
	u64			nr_background_reclaim;

	unsigned long		last_flushed;
	struct journal_entry_pin *flush_in_progress;
	bool			flush_in_progress_dropped;
	wait_queue_head_t	pin_flush_wait;

	/* protects advancing ja->discard_idx: */
	struct mutex		discard_lock;
	bool			can_discard;

	unsigned long		last_flush_write;

	u64			write_start_time;

	u64			nr_flush_writes;
	u64			nr_noflush_writes;
	u64			entry_bytes_written;

	struct bch2_time_stats	*flush_write_time;
	struct bch2_time_stats	*noflush_write_time;
	struct bch2_time_stats	*flush_seq_time;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	res_map;
#endif
} __aligned(SMP_CACHE_BYTES);

/*
 * Embedded in struct bch_dev. First three fields refer to the array of journal
 * buckets, in bch_sb.
 */
struct journal_device {
	/*
	 * For each journal bucket, contains the max sequence number of the
	 * journal writes it contains - so we know when a bucket can be reused.
	 */
	u64			*bucket_seq;

	unsigned		sectors_free;

	/*
	 * discard_idx <= dirty_idx_ondisk <= dirty_idx <= cur_idx:
	 */
	unsigned		discard_idx;		/* Next bucket to discard */
	unsigned		dirty_idx_ondisk;
	unsigned		dirty_idx;
	unsigned		cur_idx;		/* Journal bucket we're currently writing to */
	unsigned		nr;

	u64			*buckets;

	/* Bio for journal reads/writes to this device */
	struct journal_bio	*bio[JOURNAL_BUF_NR];

	/* for bch_journal_read_device */
	struct closure		read;
};

/*
 * journal_entry_res - reserve space in every journal entry:
 */
struct journal_entry_res {
	unsigned		u64s;
};

#endif /* _BCACHEFS_JOURNAL_TYPES_H */
