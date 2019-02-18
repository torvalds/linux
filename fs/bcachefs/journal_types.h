/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_JOURNAL_TYPES_H
#define _BCACHEFS_JOURNAL_TYPES_H

#include <linux/cache.h>
#include <linux/workqueue.h>

#include "alloc_types.h"
#include "super_types.h"
#include "fifo.h"

struct journal_res;

/*
 * We put two of these in struct journal; we used them for writes to the
 * journal that are being staged or in flight.
 */
struct journal_buf {
	struct jset		*data;

	BKEY_PADDED(key);

	struct closure_waitlist	wait;

	unsigned		buf_size;	/* size in bytes of @data */
	unsigned		sectors;	/* maximum size for current entry */
	unsigned		disk_sectors;	/* maximum size entry could have been, if
						   buf_size was bigger */
	unsigned		u64s_reserved;
	/* bloom filter: */
	unsigned long		has_inode[1024 / sizeof(unsigned long)];
};

/*
 * Something that makes a journal entry dirty - i.e. a btree node that has to be
 * flushed:
 */

struct journal_entry_pin_list {
	struct list_head		list;
	struct list_head		flushed;
	atomic_t			count;
	struct bch_devs_list		devs;
};

struct journal;
struct journal_entry_pin;
typedef void (*journal_pin_flush_fn)(struct journal *j,
				struct journal_entry_pin *, u64);

struct journal_entry_pin {
	struct list_head		list;
	journal_pin_flush_fn		flush;
	u64				seq;
};

/* corresponds to a btree node with a blacklisted bset: */
struct blacklisted_node {
	__le64			seq;
	enum btree_id		btree_id;
	struct bpos		pos;
};

struct journal_seq_blacklist {
	struct list_head	list;
	u64			start;
	u64			end;

	struct journal_entry_pin pin;

	struct blacklisted_node	*entries;
	size_t			nr_entries;
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
				idx:1,
				prev_buf_unwritten:1,
				buf0_count:21,
				buf1_count:21;
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

/*
 * JOURNAL_NEED_WRITE - current (pending) journal entry should be written ASAP,
 * either because something's waiting on the write to complete or because it's
 * been dirty too long and the timer's expired.
 */

enum {
	JOURNAL_REPLAY_DONE,
	JOURNAL_STARTED,
	JOURNAL_NEED_WRITE,
	JOURNAL_NOT_EMPTY,
};

/* Embedded in struct bch_fs */
struct journal {
	/* Fastpath stuff up front: */

	unsigned long		flags;

	union journal_res_state reservations;

	/* Max size of current journal entry */
	unsigned		cur_entry_u64s;
	unsigned		cur_entry_sectors;

	/* Reserved space in journal entry to be used just prior to write */
	unsigned		entry_u64s_reserved;

	unsigned		buf_size_want;

	/*
	 * Two journal entries -- one is currently open for new entries, the
	 * other is possibly being written out.
	 */
	struct journal_buf	buf[2];

	spinlock_t		lock;

	/* if nonzero, we may not open a new journal entry: */
	unsigned		blocked;

	/* Used when waiting because the journal was full */
	wait_queue_head_t	wait;
	struct closure_waitlist	async_wait;

	struct closure		io;
	struct delayed_work	write_work;

	/* Sequence number of most recent journal entry (last entry in @pin) */
	atomic64_t		seq;

	/* seq, last_seq from the most recent journal entry successfully written */
	u64			seq_ondisk;
	u64			last_seq_ondisk;

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

	struct journal_entry_pin *flush_in_progress;
	wait_queue_head_t	pin_flush_wait;

	u64			replay_journal_seq;

	struct mutex		blacklist_lock;
	struct list_head	seq_blacklist;
	struct journal_seq_blacklist *new_blacklist;

	struct write_point	wp;
	spinlock_t		err_lock;

	struct delayed_work	reclaim_work;
	unsigned long		last_flushed;

	/* protects advancing ja->last_idx: */
	struct mutex		reclaim_lock;
	unsigned		write_delay_ms;
	unsigned		reclaim_delay_ms;

	u64			res_get_blocked_start;
	u64			need_write_time;
	u64			write_start_time;

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

	/* Journal bucket we're currently writing to */
	unsigned		cur_idx;

	/* Last journal bucket that still contains an open journal entry */

	/*
	 * j->lock and j->reclaim_lock must both be held to modify, j->lock
	 * sufficient to read:
	 */
	unsigned		last_idx;
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
