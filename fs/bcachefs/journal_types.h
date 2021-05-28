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
	struct jset		*data;

	__BKEY_PADDED(key, BCH_REPLICAS_MAX);
	struct bch_devs_list	devs_written;

	struct closure_waitlist	wait;
	u64			last_seq;	/* copy of data->last_seq */

	unsigned		buf_size;	/* size in bytes of @data */
	unsigned		sectors;	/* maximum size for current entry */
	unsigned		disk_sectors;	/* maximum size entry could have been, if
						   buf_size was bigger */
	unsigned		u64s_reserved;
	bool			noflush;	/* write has already been kicked off, and was noflush */
	bool			must_flush;	/* something wants a flush */
	bool			separate_flush;
	/* bloom filter: */
	unsigned long		has_inode[1024 / sizeof(unsigned long)];
};

/*
 * Something that makes a journal entry dirty - i.e. a btree node that has to be
 * flushed:
 */

struct journal_entry_pin_list {
	struct list_head		list;
	struct list_head		key_cache_list;
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

/*
 * For reserving space in the journal prior to getting a reservation on a
 * particular journal entry:
 */
struct journal_preres {
	unsigned		u64s;
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

union journal_preres_state {
	struct {
		atomic64_t	counter;
	};

	struct {
		u64		v;
	};

	struct {
		u64		waiting:1,
				reserved:31,
				remaining:32;
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

/*
 * JOURNAL_NEED_WRITE - current (pending) journal entry should be written ASAP,
 * either because something's waiting on the write to complete or because it's
 * been dirty too long and the timer's expired.
 */

enum {
	JOURNAL_REPLAY_DONE,
	JOURNAL_STARTED,
	JOURNAL_RECLAIM_STARTED,
	JOURNAL_NEED_WRITE,
	JOURNAL_MAY_GET_UNRESERVED,
	JOURNAL_MAY_SKIP_FLUSH,
};

/* Embedded in struct bch_fs */
struct journal {
	/* Fastpath stuff up front: */

	unsigned long		flags;

	union journal_res_state reservations;

	/* Max size of current journal entry */
	unsigned		cur_entry_u64s;
	unsigned		cur_entry_sectors;

	/*
	 * 0, or -ENOSPC if waiting on journal reclaim, or -EROFS if
	 * insufficient devices:
	 */
	enum {
		cur_entry_ok,
		cur_entry_blocked,
		cur_entry_journal_full,
		cur_entry_journal_pin_full,
		cur_entry_journal_stuck,
		cur_entry_insufficient_devices,
	}			cur_entry_error;

	union journal_preres_state prereserved;

	/* Reserved space in journal entry to be used just prior to write */
	unsigned		entry_u64s_reserved;

	unsigned		buf_size_want;

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
	struct closure_waitlist	preres_wait;

	struct closure		io;
	struct delayed_work	write_work;

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

	unsigned		write_delay_ms;
	unsigned		reclaim_delay_ms;
	unsigned long		last_flush_write;

	u64			res_get_blocked_start;
	u64			need_write_time;
	u64			write_start_time;

	u64			nr_flush_writes;
	u64			nr_noflush_writes;

	struct bch2_time_stats	*write_time;
	struct bch2_time_stats	*delay_time;
	struct bch2_time_stats	*blocked_time;
	struct bch2_time_stats	*flush_seq_time;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	res_map;
#endif
};

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
	struct bio		*bio;

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
