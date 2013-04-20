/*
 * linux/include/linux/jbd2.h
 *
 * Written by Stephen C. Tweedie <sct@redhat.com>
 *
 * Copyright 1998-2000 Red Hat, Inc --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * Definitions for transaction data structures for the buffer cache
 * filesystem journaling support.
 */

#ifndef _LINUX_JBD2_H
#define _LINUX_JBD2_H

/* Allow this file to be included directly into e2fsprogs */
#ifndef __KERNEL__
#include "jfs_compat.h"
#define JBD2_DEBUG
#else

#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/journal-head.h>
#include <linux/stddef.h>
#include <linux/bit_spinlock.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <crypto/hash.h>
#endif

#define journal_oom_retry 1

/*
 * Define JBD2_PARANIOD_IOFAIL to cause a kernel BUG() if ext4 finds
 * certain classes of error which can occur due to failed IOs.  Under
 * normal use we want ext4 to continue after such errors, because
 * hardware _can_ fail, but for debugging purposes when running tests on
 * known-good hardware we may want to trap these errors.
 */
#undef JBD2_PARANOID_IOFAIL

/*
 * The default maximum commit age, in seconds.
 */
#define JBD2_DEFAULT_MAX_COMMIT_AGE 5

#ifdef CONFIG_JBD2_DEBUG
/*
 * Define JBD2_EXPENSIVE_CHECKING to enable more expensive internal
 * consistency checks.  By default we don't do this unless
 * CONFIG_JBD2_DEBUG is on.
 */
#define JBD2_EXPENSIVE_CHECKING
extern ushort jbd2_journal_enable_debug;

#define jbd_debug(n, f, a...)						\
	do {								\
		if ((n) <= jbd2_journal_enable_debug) {			\
			printk (KERN_DEBUG "(%s, %d): %s: ",		\
				__FILE__, __LINE__, __func__);	\
			printk (f, ## a);				\
		}							\
	} while (0)
#else
#define jbd_debug(f, a...)	/**/
#endif

extern void *jbd2_alloc(size_t size, gfp_t flags);
extern void jbd2_free(void *ptr, size_t size);

#define JBD2_MIN_JOURNAL_BLOCKS 1024

#ifdef __KERNEL__

/**
 * typedef handle_t - The handle_t type represents a single atomic update being performed by some process.
 *
 * All filesystem modifications made by the process go
 * through this handle.  Recursive operations (such as quota operations)
 * are gathered into a single update.
 *
 * The buffer credits field is used to account for journaled buffers
 * being modified by the running process.  To ensure that there is
 * enough log space for all outstanding operations, we need to limit the
 * number of outstanding buffers possible at any time.  When the
 * operation completes, any buffer credits not used are credited back to
 * the transaction, so that at all times we know how many buffers the
 * outstanding updates on a transaction might possibly touch.
 *
 * This is an opaque datatype.
 **/
typedef struct jbd2_journal_handle handle_t;	/* Atomic operation type */


/**
 * typedef journal_t - The journal_t maintains all of the journaling state information for a single filesystem.
 *
 * journal_t is linked to from the fs superblock structure.
 *
 * We use the journal_t to keep track of all outstanding transaction
 * activity on the filesystem, and to manage the state of the log
 * writing process.
 *
 * This is an opaque datatype.
 **/
typedef struct journal_s	journal_t;	/* Journal control structure */
#endif

/*
 * Internal structures used by the logging mechanism:
 */

#define JBD2_MAGIC_NUMBER 0xc03b3998U /* The first 4 bytes of /dev/random! */

/*
 * On-disk structures
 */

/*
 * Descriptor block types:
 */

#define JBD2_DESCRIPTOR_BLOCK	1
#define JBD2_COMMIT_BLOCK	2
#define JBD2_SUPERBLOCK_V1	3
#define JBD2_SUPERBLOCK_V2	4
#define JBD2_REVOKE_BLOCK	5

/*
 * Standard header for all descriptor blocks:
 */
typedef struct journal_header_s
{
	__be32		h_magic;
	__be32		h_blocktype;
	__be32		h_sequence;
} journal_header_t;

/*
 * Checksum types.
 */
#define JBD2_CRC32_CHKSUM   1
#define JBD2_MD5_CHKSUM     2
#define JBD2_SHA1_CHKSUM    3
#define JBD2_CRC32C_CHKSUM  4

#define JBD2_CRC32_CHKSUM_SIZE 4

#define JBD2_CHECKSUM_BYTES (32 / sizeof(u32))
/*
 * Commit block header for storing transactional checksums:
 *
 * NOTE: If FEATURE_COMPAT_CHECKSUM (checksum v1) is set, the h_chksum*
 * fields are used to store a checksum of the descriptor and data blocks.
 *
 * If FEATURE_INCOMPAT_CSUM_V2 (checksum v2) is set, then the h_chksum
 * field is used to store crc32c(uuid+commit_block).  Each journal metadata
 * block gets its own checksum, and data block checksums are stored in
 * journal_block_tag (in the descriptor).  The other h_chksum* fields are
 * not used.
 *
 * Checksum v1 and v2 are mutually exclusive features.
 */
struct commit_header {
	__be32		h_magic;
	__be32          h_blocktype;
	__be32          h_sequence;
	unsigned char   h_chksum_type;
	unsigned char   h_chksum_size;
	unsigned char 	h_padding[2];
	__be32 		h_chksum[JBD2_CHECKSUM_BYTES];
	__be64		h_commit_sec;
	__be32		h_commit_nsec;
};

/*
 * The block tag: used to describe a single buffer in the journal.
 * t_blocknr_high is only used if INCOMPAT_64BIT is set, so this
 * raw struct shouldn't be used for pointer math or sizeof() - use
 * journal_tag_bytes(journal) instead to compute this.
 */
typedef struct journal_block_tag_s
{
	__be32		t_blocknr;	/* The on-disk block number */
	__be16		t_checksum;	/* truncated crc32c(uuid+seq+block) */
	__be16		t_flags;	/* See below */
	__be32		t_blocknr_high; /* most-significant high 32bits. */
} journal_block_tag_t;

#define JBD2_TAG_SIZE32 (offsetof(journal_block_tag_t, t_blocknr_high))
#define JBD2_TAG_SIZE64 (sizeof(journal_block_tag_t))

/* Tail of descriptor block, for checksumming */
struct jbd2_journal_block_tail {
	__be32		t_checksum;	/* crc32c(uuid+descr_block) */
};

/*
 * The revoke descriptor: used on disk to describe a series of blocks to
 * be revoked from the log
 */
typedef struct jbd2_journal_revoke_header_s
{
	journal_header_t r_header;
	__be32		 r_count;	/* Count of bytes used in the block */
} jbd2_journal_revoke_header_t;

/* Tail of revoke block, for checksumming */
struct jbd2_journal_revoke_tail {
	__be32		r_checksum;	/* crc32c(uuid+revoke_block) */
};

/* Definitions for the journal tag flags word: */
#define JBD2_FLAG_ESCAPE		1	/* on-disk block is escaped */
#define JBD2_FLAG_SAME_UUID	2	/* block has same uuid as previous */
#define JBD2_FLAG_DELETED	4	/* block deleted by this transaction */
#define JBD2_FLAG_LAST_TAG	8	/* last tag in this descriptor block */


/*
 * The journal superblock.  All fields are in big-endian byte order.
 */
typedef struct journal_superblock_s
{
/* 0x0000 */
	journal_header_t s_header;

/* 0x000C */
	/* Static information describing the journal */
	__be32	s_blocksize;		/* journal device blocksize */
	__be32	s_maxlen;		/* total blocks in journal file */
	__be32	s_first;		/* first block of log information */

/* 0x0018 */
	/* Dynamic information describing the current state of the log */
	__be32	s_sequence;		/* first commit ID expected in log */
	__be32	s_start;		/* blocknr of start of log */

/* 0x0020 */
	/* Error value, as set by jbd2_journal_abort(). */
	__be32	s_errno;

/* 0x0024 */
	/* Remaining fields are only valid in a version-2 superblock */
	__be32	s_feature_compat;	/* compatible feature set */
	__be32	s_feature_incompat;	/* incompatible feature set */
	__be32	s_feature_ro_compat;	/* readonly-compatible feature set */
/* 0x0030 */
	__u8	s_uuid[16];		/* 128-bit uuid for journal */

/* 0x0040 */
	__be32	s_nr_users;		/* Nr of filesystems sharing log */

	__be32	s_dynsuper;		/* Blocknr of dynamic superblock copy*/

/* 0x0048 */
	__be32	s_max_transaction;	/* Limit of journal blocks per trans.*/
	__be32	s_max_trans_data;	/* Limit of data blocks per trans. */

/* 0x0050 */
	__u8	s_checksum_type;	/* checksum type */
	__u8	s_padding2[3];
	__u32	s_padding[42];
	__be32	s_checksum;		/* crc32c(superblock) */

/* 0x0100 */
	__u8	s_users[16*48];		/* ids of all fs'es sharing the log */
/* 0x0400 */
} journal_superblock_t;

#define JBD2_HAS_COMPAT_FEATURE(j,mask)					\
	((j)->j_format_version >= 2 &&					\
	 ((j)->j_superblock->s_feature_compat & cpu_to_be32((mask))))
#define JBD2_HAS_RO_COMPAT_FEATURE(j,mask)				\
	((j)->j_format_version >= 2 &&					\
	 ((j)->j_superblock->s_feature_ro_compat & cpu_to_be32((mask))))
#define JBD2_HAS_INCOMPAT_FEATURE(j,mask)				\
	((j)->j_format_version >= 2 &&					\
	 ((j)->j_superblock->s_feature_incompat & cpu_to_be32((mask))))

#define JBD2_FEATURE_COMPAT_CHECKSUM	0x00000001

#define JBD2_FEATURE_INCOMPAT_REVOKE		0x00000001
#define JBD2_FEATURE_INCOMPAT_64BIT		0x00000002
#define JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT	0x00000004
#define JBD2_FEATURE_INCOMPAT_CSUM_V2		0x00000008

/* Features known to this kernel version: */
#define JBD2_KNOWN_COMPAT_FEATURES	JBD2_FEATURE_COMPAT_CHECKSUM
#define JBD2_KNOWN_ROCOMPAT_FEATURES	0
#define JBD2_KNOWN_INCOMPAT_FEATURES	(JBD2_FEATURE_INCOMPAT_REVOKE | \
					JBD2_FEATURE_INCOMPAT_64BIT | \
					JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT | \
					JBD2_FEATURE_INCOMPAT_CSUM_V2)

#ifdef __KERNEL__

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/jbd_common.h>

#define J_ASSERT(assert)	BUG_ON(!(assert))

#define J_ASSERT_BH(bh, expr)	J_ASSERT(expr)
#define J_ASSERT_JH(jh, expr)	J_ASSERT(expr)

#if defined(JBD2_PARANOID_IOFAIL)
#define J_EXPECT(expr, why...)		J_ASSERT(expr)
#define J_EXPECT_BH(bh, expr, why...)	J_ASSERT_BH(bh, expr)
#define J_EXPECT_JH(jh, expr, why...)	J_ASSERT_JH(jh, expr)
#else
#define __journal_expect(expr, why...)					     \
	({								     \
		int val = (expr);					     \
		if (!val) {						     \
			printk(KERN_ERR					     \
			       "JBD2 unexpected failure: %s: %s;\n",	     \
			       __func__, #expr);			     \
			printk(KERN_ERR why "\n");			     \
		}							     \
		val;							     \
	})
#define J_EXPECT(expr, why...)		__journal_expect(expr, ## why)
#define J_EXPECT_BH(bh, expr, why...)	__journal_expect(expr, ## why)
#define J_EXPECT_JH(jh, expr, why...)	__journal_expect(expr, ## why)
#endif

/* Flags in jbd_inode->i_flags */
#define __JI_COMMIT_RUNNING 0
/* Commit of the inode data in progress. We use this flag to protect us from
 * concurrent deletion of inode. We cannot use reference to inode for this
 * since we cannot afford doing last iput() on behalf of kjournald
 */
#define JI_COMMIT_RUNNING (1 << __JI_COMMIT_RUNNING)

/**
 * struct jbd_inode is the structure linking inodes in ordered mode
 *   present in a transaction so that we can sync them during commit.
 */
struct jbd2_inode {
	/* Which transaction does this inode belong to? Either the running
	 * transaction or the committing one. [j_list_lock] */
	transaction_t *i_transaction;

	/* Pointer to the running transaction modifying inode's data in case
	 * there is already a committing transaction touching it. [j_list_lock] */
	transaction_t *i_next_transaction;

	/* List of inodes in the i_transaction [j_list_lock] */
	struct list_head i_list;

	/* VFS inode this inode belongs to [constant during the lifetime
	 * of the structure] */
	struct inode *i_vfs_inode;

	/* Flags of inode [j_list_lock] */
	unsigned long i_flags;
};

struct jbd2_revoke_table_s;

/**
 * struct handle_s - The handle_s type is the concrete type associated with
 *     handle_t.
 * @h_transaction: Which compound transaction is this update a part of?
 * @h_buffer_credits: Number of remaining buffers we are allowed to dirty.
 * @h_ref: Reference count on this handle
 * @h_err: Field for caller's use to track errors through large fs operations
 * @h_sync: flag for sync-on-close
 * @h_jdata: flag to force data journaling
 * @h_aborted: flag indicating fatal error on handle
 **/

/* Docbook can't yet cope with the bit fields, but will leave the documentation
 * in so it can be fixed later.
 */

struct jbd2_journal_handle
{
	/* Which compound transaction is this update a part of? */
	transaction_t		*h_transaction;

	/* Number of remaining buffers we are allowed to dirty: */
	int			h_buffer_credits;

	/* Reference count on this handle */
	int			h_ref;

	/* Field for caller's use to track errors through large fs */
	/* operations */
	int			h_err;

	/* Flags [no locking] */
	unsigned int	h_sync:		1;	/* sync-on-close */
	unsigned int	h_jdata:	1;	/* force data journaling */
	unsigned int	h_aborted:	1;	/* fatal error on handle */
	unsigned int	h_type:		8;	/* for handle statistics */
	unsigned int	h_line_no:	16;	/* for handle statistics */

	unsigned long		h_start_jiffies;
	unsigned int		h_requested_credits;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	h_lockdep_map;
#endif
};


/*
 * Some stats for checkpoint phase
 */
struct transaction_chp_stats_s {
	unsigned long		cs_chp_time;
	__u32			cs_forced_to_close;
	__u32			cs_written;
	__u32			cs_dropped;
};

/* The transaction_t type is the guts of the journaling mechanism.  It
 * tracks a compound transaction through its various states:
 *
 * RUNNING:	accepting new updates
 * LOCKED:	Updates still running but we don't accept new ones
 * RUNDOWN:	Updates are tidying up but have finished requesting
 *		new buffers to modify (state not used for now)
 * FLUSH:       All updates complete, but we are still writing to disk
 * COMMIT:      All data on disk, writing commit record
 * FINISHED:	We still have to keep the transaction for checkpointing.
 *
 * The transaction keeps track of all of the buffers modified by a
 * running transaction, and all of the buffers committed but not yet
 * flushed to home for finished transactions.
 */

/*
 * Lock ranking:
 *
 *    j_list_lock
 *      ->jbd_lock_bh_journal_head()	(This is "innermost")
 *
 *    j_state_lock
 *    ->jbd_lock_bh_state()
 *
 *    jbd_lock_bh_state()
 *    ->j_list_lock
 *
 *    j_state_lock
 *    ->t_handle_lock
 *
 *    j_state_lock
 *    ->j_list_lock			(journal_unmap_buffer)
 *
 */

struct transaction_s
{
	/* Pointer to the journal for this transaction. [no locking] */
	journal_t		*t_journal;

	/* Sequence number for this transaction [no locking] */
	tid_t			t_tid;

	/*
	 * Transaction's current state
	 * [no locking - only kjournald2 alters this]
	 * [j_list_lock] guards transition of a transaction into T_FINISHED
	 * state and subsequent call of __jbd2_journal_drop_transaction()
	 * FIXME: needs barriers
	 * KLUDGE: [use j_state_lock]
	 */
	enum {
		T_RUNNING,
		T_LOCKED,
		T_FLUSH,
		T_COMMIT,
		T_COMMIT_DFLUSH,
		T_COMMIT_JFLUSH,
		T_FINISHED
	}			t_state;

	/*
	 * Where in the log does this transaction's commit start? [no locking]
	 */
	unsigned long		t_log_start;

	/* Number of buffers on the t_buffers list [j_list_lock] */
	int			t_nr_buffers;

	/*
	 * Doubly-linked circular list of all buffers reserved but not yet
	 * modified by this transaction [j_list_lock]
	 */
	struct journal_head	*t_reserved_list;

	/*
	 * Doubly-linked circular list of all metadata buffers owned by this
	 * transaction [j_list_lock]
	 */
	struct journal_head	*t_buffers;

	/*
	 * Doubly-linked circular list of all forget buffers (superseded
	 * buffers which we can un-checkpoint once this transaction commits)
	 * [j_list_lock]
	 */
	struct journal_head	*t_forget;

	/*
	 * Doubly-linked circular list of all buffers still to be flushed before
	 * this transaction can be checkpointed. [j_list_lock]
	 */
	struct journal_head	*t_checkpoint_list;

	/*
	 * Doubly-linked circular list of all buffers submitted for IO while
	 * checkpointing. [j_list_lock]
	 */
	struct journal_head	*t_checkpoint_io_list;

	/*
	 * Doubly-linked circular list of temporary buffers currently undergoing
	 * IO in the log [j_list_lock]
	 */
	struct journal_head	*t_iobuf_list;

	/*
	 * Doubly-linked circular list of metadata buffers being shadowed by log
	 * IO.  The IO buffers on the iobuf list and the shadow buffers on this
	 * list match each other one for one at all times. [j_list_lock]
	 */
	struct journal_head	*t_shadow_list;

	/*
	 * Doubly-linked circular list of control buffers being written to the
	 * log. [j_list_lock]
	 */
	struct journal_head	*t_log_list;

	/*
	 * List of inodes whose data we've modified in data=ordered mode.
	 * [j_list_lock]
	 */
	struct list_head	t_inode_list;

	/*
	 * Protects info related to handles
	 */
	spinlock_t		t_handle_lock;

	/*
	 * Longest time some handle had to wait for running transaction
	 */
	unsigned long		t_max_wait;

	/*
	 * When transaction started
	 */
	unsigned long		t_start;

	/*
	 * When commit was requested
	 */
	unsigned long		t_requested;

	/*
	 * Checkpointing stats [j_checkpoint_sem]
	 */
	struct transaction_chp_stats_s t_chp_stats;

	/*
	 * Number of outstanding updates running on this transaction
	 * [t_handle_lock]
	 */
	atomic_t		t_updates;

	/*
	 * Number of buffers reserved for use by all handles in this transaction
	 * handle but not yet modified. [t_handle_lock]
	 */
	atomic_t		t_outstanding_credits;

	/*
	 * Forward and backward links for the circular list of all transactions
	 * awaiting checkpoint. [j_list_lock]
	 */
	transaction_t		*t_cpnext, *t_cpprev;

	/*
	 * When will the transaction expire (become due for commit), in jiffies?
	 * [no locking]
	 */
	unsigned long		t_expires;

	/*
	 * When this transaction started, in nanoseconds [no locking]
	 */
	ktime_t			t_start_time;

	/*
	 * How many handles used this transaction? [t_handle_lock]
	 */
	atomic_t		t_handle_count;

	/*
	 * This transaction is being forced and some process is
	 * waiting for it to finish.
	 */
	unsigned int t_synchronous_commit:1;

	/* Disk flush needs to be sent to fs partition [no locking] */
	int			t_need_data_flush;

	/*
	 * For use by the filesystem to store fs-specific data
	 * structures associated with the transaction
	 */
	struct list_head	t_private_list;
};

struct transaction_run_stats_s {
	unsigned long		rs_wait;
	unsigned long		rs_request_delay;
	unsigned long		rs_running;
	unsigned long		rs_locked;
	unsigned long		rs_flushing;
	unsigned long		rs_logging;

	__u32			rs_handle_count;
	__u32			rs_blocks;
	__u32			rs_blocks_logged;
};

struct transaction_stats_s {
	unsigned long		ts_tid;
	unsigned long		ts_requested;
	struct transaction_run_stats_s run;
};

static inline unsigned long
jbd2_time_diff(unsigned long start, unsigned long end)
{
	if (end >= start)
		return end - start;

	return end + (MAX_JIFFY_OFFSET - start);
}

#define JBD2_NR_BATCH	64

/**
 * struct journal_s - The journal_s type is the concrete type associated with
 *     journal_t.
 * @j_flags:  General journaling state flags
 * @j_errno:  Is there an outstanding uncleared error on the journal (from a
 *     prior abort)?
 * @j_sb_buffer: First part of superblock buffer
 * @j_superblock: Second part of superblock buffer
 * @j_format_version: Version of the superblock format
 * @j_state_lock: Protect the various scalars in the journal
 * @j_barrier_count:  Number of processes waiting to create a barrier lock
 * @j_barrier: The barrier lock itself
 * @j_running_transaction: The current running transaction..
 * @j_committing_transaction: the transaction we are pushing to disk
 * @j_checkpoint_transactions: a linked circular list of all transactions
 *  waiting for checkpointing
 * @j_wait_transaction_locked: Wait queue for waiting for a locked transaction
 *  to start committing, or for a barrier lock to be released
 * @j_wait_logspace: Wait queue for waiting for checkpointing to complete
 * @j_wait_done_commit: Wait queue for waiting for commit to complete
 * @j_wait_checkpoint:  Wait queue to trigger checkpointing
 * @j_wait_commit: Wait queue to trigger commit
 * @j_wait_updates: Wait queue to wait for updates to complete
 * @j_checkpoint_mutex: Mutex for locking against concurrent checkpoints
 * @j_head: Journal head - identifies the first unused block in the journal
 * @j_tail: Journal tail - identifies the oldest still-used block in the
 *  journal.
 * @j_free: Journal free - how many free blocks are there in the journal?
 * @j_first: The block number of the first usable block
 * @j_last: The block number one beyond the last usable block
 * @j_dev: Device where we store the journal
 * @j_blocksize: blocksize for the location where we store the journal.
 * @j_blk_offset: starting block offset for into the device where we store the
 *     journal
 * @j_fs_dev: Device which holds the client fs.  For internal journal this will
 *     be equal to j_dev
 * @j_maxlen: Total maximum capacity of the journal region on disk.
 * @j_list_lock: Protects the buffer lists and internal buffer state.
 * @j_inode: Optional inode where we store the journal.  If present, all journal
 *     block numbers are mapped into this inode via bmap().
 * @j_tail_sequence:  Sequence number of the oldest transaction in the log
 * @j_transaction_sequence: Sequence number of the next transaction to grant
 * @j_commit_sequence: Sequence number of the most recently committed
 *  transaction
 * @j_commit_request: Sequence number of the most recent transaction wanting
 *     commit
 * @j_uuid: Uuid of client object.
 * @j_task: Pointer to the current commit thread for this journal
 * @j_max_transaction_buffers:  Maximum number of metadata buffers to allow in a
 *     single compound commit transaction
 * @j_commit_interval: What is the maximum transaction lifetime before we begin
 *  a commit?
 * @j_commit_timer:  The timer used to wakeup the commit thread
 * @j_revoke_lock: Protect the revoke table
 * @j_revoke: The revoke table - maintains the list of revoked blocks in the
 *     current transaction.
 * @j_revoke_table: alternate revoke tables for j_revoke
 * @j_wbuf: array of buffer_heads for jbd2_journal_commit_transaction
 * @j_wbufsize: maximum number of buffer_heads allowed in j_wbuf, the
 *	number that will fit in j_blocksize
 * @j_last_sync_writer: most recent pid which did a synchronous write
 * @j_history: Buffer storing the transactions statistics history
 * @j_history_max: Maximum number of transactions in the statistics history
 * @j_history_cur: Current number of transactions in the statistics history
 * @j_history_lock: Protect the transactions statistics history
 * @j_proc_entry: procfs entry for the jbd statistics directory
 * @j_stats: Overall statistics
 * @j_private: An opaque pointer to fs-private information.
 */

struct journal_s
{
	/* General journaling state flags [j_state_lock] */
	unsigned long		j_flags;

	/*
	 * Is there an outstanding uncleared error on the journal (from a prior
	 * abort)? [j_state_lock]
	 */
	int			j_errno;

	/* The superblock buffer */
	struct buffer_head	*j_sb_buffer;
	journal_superblock_t	*j_superblock;

	/* Version of the superblock format */
	int			j_format_version;

	/*
	 * Protect the various scalars in the journal
	 */
	rwlock_t		j_state_lock;

	/*
	 * Number of processes waiting to create a barrier lock [j_state_lock]
	 */
	int			j_barrier_count;

	/* The barrier lock itself */
	struct mutex		j_barrier;

	/*
	 * Transactions: The current running transaction...
	 * [j_state_lock] [caller holding open handle]
	 */
	transaction_t		*j_running_transaction;

	/*
	 * the transaction we are pushing to disk
	 * [j_state_lock] [caller holding open handle]
	 */
	transaction_t		*j_committing_transaction;

	/*
	 * ... and a linked circular list of all transactions waiting for
	 * checkpointing. [j_list_lock]
	 */
	transaction_t		*j_checkpoint_transactions;

	/*
	 * Wait queue for waiting for a locked transaction to start committing,
	 * or for a barrier lock to be released
	 */
	wait_queue_head_t	j_wait_transaction_locked;

	/* Wait queue for waiting for checkpointing to complete */
	wait_queue_head_t	j_wait_logspace;

	/* Wait queue for waiting for commit to complete */
	wait_queue_head_t	j_wait_done_commit;

	/* Wait queue to trigger checkpointing */
	wait_queue_head_t	j_wait_checkpoint;

	/* Wait queue to trigger commit */
	wait_queue_head_t	j_wait_commit;

	/* Wait queue to wait for updates to complete */
	wait_queue_head_t	j_wait_updates;

	/* Semaphore for locking against concurrent checkpoints */
	struct mutex		j_checkpoint_mutex;

	/*
	 * List of buffer heads used by the checkpoint routine.  This
	 * was moved from jbd2_log_do_checkpoint() to reduce stack
	 * usage.  Access to this array is controlled by the
	 * j_checkpoint_mutex.  [j_checkpoint_mutex]
	 */
	struct buffer_head	*j_chkpt_bhs[JBD2_NR_BATCH];
	
	/*
	 * Journal head: identifies the first unused block in the journal.
	 * [j_state_lock]
	 */
	unsigned long		j_head;

	/*
	 * Journal tail: identifies the oldest still-used block in the journal.
	 * [j_state_lock]
	 */
	unsigned long		j_tail;

	/*
	 * Journal free: how many free blocks are there in the journal?
	 * [j_state_lock]
	 */
	unsigned long		j_free;

	/*
	 * Journal start and end: the block numbers of the first usable block
	 * and one beyond the last usable block in the journal. [j_state_lock]
	 */
	unsigned long		j_first;
	unsigned long		j_last;

	/*
	 * Device, blocksize and starting block offset for the location where we
	 * store the journal.
	 */
	struct block_device	*j_dev;
	int			j_blocksize;
	unsigned long long	j_blk_offset;
	char			j_devname[BDEVNAME_SIZE+24];

	/*
	 * Device which holds the client fs.  For internal journal this will be
	 * equal to j_dev.
	 */
	struct block_device	*j_fs_dev;

	/* Total maximum capacity of the journal region on disk. */
	unsigned int		j_maxlen;

	/*
	 * Protects the buffer lists and internal buffer state.
	 */
	spinlock_t		j_list_lock;

	/* Optional inode where we store the journal.  If present, all */
	/* journal block numbers are mapped into this inode via */
	/* bmap(). */
	struct inode		*j_inode;

	/*
	 * Sequence number of the oldest transaction in the log [j_state_lock]
	 */
	tid_t			j_tail_sequence;

	/*
	 * Sequence number of the next transaction to grant [j_state_lock]
	 */
	tid_t			j_transaction_sequence;

	/*
	 * Sequence number of the most recently committed transaction
	 * [j_state_lock].
	 */
	tid_t			j_commit_sequence;

	/*
	 * Sequence number of the most recent transaction wanting commit
	 * [j_state_lock]
	 */
	tid_t			j_commit_request;

	/*
	 * Journal uuid: identifies the object (filesystem, LVM volume etc)
	 * backed by this journal.  This will eventually be replaced by an array
	 * of uuids, allowing us to index multiple devices within a single
	 * journal and to perform atomic updates across them.
	 */
	__u8			j_uuid[16];

	/* Pointer to the current commit thread for this journal */
	struct task_struct	*j_task;

	/*
	 * Maximum number of metadata buffers to allow in a single compound
	 * commit transaction
	 */
	int			j_max_transaction_buffers;

	/*
	 * What is the maximum transaction lifetime before we begin a commit?
	 */
	unsigned long		j_commit_interval;

	/* The timer used to wakeup the commit thread: */
	struct timer_list	j_commit_timer;

	/*
	 * The revoke table: maintains the list of revoked blocks in the
	 * current transaction.  [j_revoke_lock]
	 */
	spinlock_t		j_revoke_lock;
	struct jbd2_revoke_table_s *j_revoke;
	struct jbd2_revoke_table_s *j_revoke_table[2];

	/*
	 * array of bhs for jbd2_journal_commit_transaction
	 */
	struct buffer_head	**j_wbuf;
	int			j_wbufsize;

	/*
	 * this is the pid of hte last person to run a synchronous operation
	 * through the journal
	 */
	pid_t			j_last_sync_writer;

	/*
	 * the average amount of time in nanoseconds it takes to commit a
	 * transaction to disk. [j_state_lock]
	 */
	u64			j_average_commit_time;

	/*
	 * minimum and maximum times that we should wait for
	 * additional filesystem operations to get batched into a
	 * synchronous handle in microseconds
	 */
	u32			j_min_batch_time;
	u32			j_max_batch_time;

	/* This function is called when a transaction is closed */
	void			(*j_commit_callback)(journal_t *,
						     transaction_t *);

	/*
	 * Journal statistics
	 */
	spinlock_t		j_history_lock;
	struct proc_dir_entry	*j_proc_entry;
	struct transaction_stats_s j_stats;

	/* Failed journal commit ID */
	unsigned int		j_failed_commit;

	/*
	 * An opaque pointer to fs-private information.  ext3 puts its
	 * superblock pointer here
	 */
	void *j_private;

	/* Reference to checksum algorithm driver via cryptoapi */
	struct crypto_shash *j_chksum_driver;

	/* Precomputed journal UUID checksum for seeding other checksums */
	__u32 j_csum_seed;
};

/*
 * Journal flag definitions
 */
#define JBD2_UNMOUNT	0x001	/* Journal thread is being destroyed */
#define JBD2_ABORT	0x002	/* Journaling has been aborted for errors. */
#define JBD2_ACK_ERR	0x004	/* The errno in the sb has been acked */
#define JBD2_FLUSHED	0x008	/* The journal superblock has been flushed */
#define JBD2_LOADED	0x010	/* The journal superblock has been loaded */
#define JBD2_BARRIER	0x020	/* Use IDE barriers */
#define JBD2_ABORT_ON_SYNCDATA_ERR	0x040	/* Abort the journal on file
						 * data write error in ordered
						 * mode */

/*
 * Function declarations for the journaling transaction and buffer
 * management
 */

/* Filing buffers */
extern void jbd2_journal_unfile_buffer(journal_t *, struct journal_head *);
extern void __jbd2_journal_refile_buffer(struct journal_head *);
extern void jbd2_journal_refile_buffer(journal_t *, struct journal_head *);
extern void __jbd2_journal_file_buffer(struct journal_head *, transaction_t *, int);
extern void __journal_free_buffer(struct journal_head *bh);
extern void jbd2_journal_file_buffer(struct journal_head *, transaction_t *, int);
extern void __journal_clean_data_list(transaction_t *transaction);

/* Log buffer allocation */
extern struct journal_head * jbd2_journal_get_descriptor_buffer(journal_t *);
int jbd2_journal_next_log_block(journal_t *, unsigned long long *);
int jbd2_journal_get_log_tail(journal_t *journal, tid_t *tid,
			      unsigned long *block);
void __jbd2_update_log_tail(journal_t *journal, tid_t tid, unsigned long block);
void jbd2_update_log_tail(journal_t *journal, tid_t tid, unsigned long block);

/* Commit management */
extern void jbd2_journal_commit_transaction(journal_t *);

/* Checkpoint list management */
int __jbd2_journal_clean_checkpoint_list(journal_t *journal);
int __jbd2_journal_remove_checkpoint(struct journal_head *);
void __jbd2_journal_insert_checkpoint(struct journal_head *, transaction_t *);


/*
 * Triggers
 */

struct jbd2_buffer_trigger_type {
	/*
	 * Fired a the moment data to write to the journal are known to be
	 * stable - so either at the moment b_frozen_data is created or just
	 * before a buffer is written to the journal.  mapped_data is a mapped
	 * buffer that is the frozen data for commit.
	 */
	void (*t_frozen)(struct jbd2_buffer_trigger_type *type,
			 struct buffer_head *bh, void *mapped_data,
			 size_t size);

	/*
	 * Fired during journal abort for dirty buffers that will not be
	 * committed.
	 */
	void (*t_abort)(struct jbd2_buffer_trigger_type *type,
			struct buffer_head *bh);
};

extern void jbd2_buffer_frozen_trigger(struct journal_head *jh,
				       void *mapped_data,
				       struct jbd2_buffer_trigger_type *triggers);
extern void jbd2_buffer_abort_trigger(struct journal_head *jh,
				      struct jbd2_buffer_trigger_type *triggers);

/* Buffer IO */
extern int
jbd2_journal_write_metadata_buffer(transaction_t	  *transaction,
			      struct journal_head  *jh_in,
			      struct journal_head **jh_out,
			      unsigned long long   blocknr);

/* Transaction locking */
extern void		__wait_on_journal (journal_t *);

/* Transaction cache support */
extern void jbd2_journal_destroy_transaction_cache(void);
extern int  jbd2_journal_init_transaction_cache(void);
extern void jbd2_journal_free_transaction(transaction_t *);

/*
 * Journal locking.
 *
 * We need to lock the journal during transaction state changes so that nobody
 * ever tries to take a handle on the running transaction while we are in the
 * middle of moving it to the commit phase.  j_state_lock does this.
 *
 * Note that the locking is completely interrupt unsafe.  We never touch
 * journal structures from interrupts.
 */

static inline handle_t *journal_current_handle(void)
{
	return current->journal_info;
}

/* The journaling code user interface:
 *
 * Create and destroy handles
 * Register buffer modifications against the current transaction.
 */

extern handle_t *jbd2_journal_start(journal_t *, int nblocks);
extern handle_t *jbd2__journal_start(journal_t *, int nblocks, gfp_t gfp_mask,
				     unsigned int type, unsigned int line_no);
extern int	 jbd2_journal_restart(handle_t *, int nblocks);
extern int	 jbd2__journal_restart(handle_t *, int nblocks, gfp_t gfp_mask);
extern int	 jbd2_journal_extend (handle_t *, int nblocks);
extern int	 jbd2_journal_get_write_access(handle_t *, struct buffer_head *);
extern int	 jbd2_journal_get_create_access (handle_t *, struct buffer_head *);
extern int	 jbd2_journal_get_undo_access(handle_t *, struct buffer_head *);
void		 jbd2_journal_set_triggers(struct buffer_head *,
					   struct jbd2_buffer_trigger_type *type);
extern int	 jbd2_journal_dirty_metadata (handle_t *, struct buffer_head *);
extern int	 jbd2_journal_forget (handle_t *, struct buffer_head *);
extern void	 journal_sync_buffer (struct buffer_head *);
extern int	 jbd2_journal_invalidatepage(journal_t *,
				struct page *, unsigned long);
extern int	 jbd2_journal_try_to_free_buffers(journal_t *, struct page *, gfp_t);
extern int	 jbd2_journal_stop(handle_t *);
extern int	 jbd2_journal_flush (journal_t *);
extern void	 jbd2_journal_lock_updates (journal_t *);
extern void	 jbd2_journal_unlock_updates (journal_t *);

extern journal_t * jbd2_journal_init_dev(struct block_device *bdev,
				struct block_device *fs_dev,
				unsigned long long start, int len, int bsize);
extern journal_t * jbd2_journal_init_inode (struct inode *);
extern int	   jbd2_journal_update_format (journal_t *);
extern int	   jbd2_journal_check_used_features
		   (journal_t *, unsigned long, unsigned long, unsigned long);
extern int	   jbd2_journal_check_available_features
		   (journal_t *, unsigned long, unsigned long, unsigned long);
extern int	   jbd2_journal_set_features
		   (journal_t *, unsigned long, unsigned long, unsigned long);
extern void	   jbd2_journal_clear_features
		   (journal_t *, unsigned long, unsigned long, unsigned long);
extern int	   jbd2_journal_load       (journal_t *journal);
extern int	   jbd2_journal_destroy    (journal_t *);
extern int	   jbd2_journal_recover    (journal_t *journal);
extern int	   jbd2_journal_wipe       (journal_t *, int);
extern int	   jbd2_journal_skip_recovery	(journal_t *);
extern void	   jbd2_journal_update_sb_errno(journal_t *);
extern void	   jbd2_journal_update_sb_log_tail	(journal_t *, tid_t,
				unsigned long, int);
extern void	   __jbd2_journal_abort_hard	(journal_t *);
extern void	   jbd2_journal_abort      (journal_t *, int);
extern int	   jbd2_journal_errno      (journal_t *);
extern void	   jbd2_journal_ack_err    (journal_t *);
extern int	   jbd2_journal_clear_err  (journal_t *);
extern int	   jbd2_journal_bmap(journal_t *, unsigned long, unsigned long long *);
extern int	   jbd2_journal_force_commit(journal_t *);
extern int	   jbd2_journal_file_inode(handle_t *handle, struct jbd2_inode *inode);
extern int	   jbd2_journal_begin_ordered_truncate(journal_t *journal,
				struct jbd2_inode *inode, loff_t new_size);
extern void	   jbd2_journal_init_jbd_inode(struct jbd2_inode *jinode, struct inode *inode);
extern void	   jbd2_journal_release_jbd_inode(journal_t *journal, struct jbd2_inode *jinode);

/*
 * journal_head management
 */
struct journal_head *jbd2_journal_add_journal_head(struct buffer_head *bh);
struct journal_head *jbd2_journal_grab_journal_head(struct buffer_head *bh);
void jbd2_journal_put_journal_head(struct journal_head *jh);

/*
 * handle management
 */
extern struct kmem_cache *jbd2_handle_cache;

static inline handle_t *jbd2_alloc_handle(gfp_t gfp_flags)
{
	return kmem_cache_alloc(jbd2_handle_cache, gfp_flags);
}

static inline void jbd2_free_handle(handle_t *handle)
{
	kmem_cache_free(jbd2_handle_cache, handle);
}

/*
 * jbd2_inode management (optional, for those file systems that want to use
 * dynamically allocated jbd2_inode structures)
 */
extern struct kmem_cache *jbd2_inode_cache;

static inline struct jbd2_inode *jbd2_alloc_inode(gfp_t gfp_flags)
{
	return kmem_cache_alloc(jbd2_inode_cache, gfp_flags);
}

static inline void jbd2_free_inode(struct jbd2_inode *jinode)
{
	kmem_cache_free(jbd2_inode_cache, jinode);
}

/* Primary revoke support */
#define JOURNAL_REVOKE_DEFAULT_HASH 256
extern int	   jbd2_journal_init_revoke(journal_t *, int);
extern void	   jbd2_journal_destroy_revoke_caches(void);
extern int	   jbd2_journal_init_revoke_caches(void);

extern void	   jbd2_journal_destroy_revoke(journal_t *);
extern int	   jbd2_journal_revoke (handle_t *, unsigned long long, struct buffer_head *);
extern int	   jbd2_journal_cancel_revoke(handle_t *, struct journal_head *);
extern void	   jbd2_journal_write_revoke_records(journal_t *,
						     transaction_t *, int);

/* Recovery revoke support */
extern int	jbd2_journal_set_revoke(journal_t *, unsigned long long, tid_t);
extern int	jbd2_journal_test_revoke(journal_t *, unsigned long long, tid_t);
extern void	jbd2_journal_clear_revoke(journal_t *);
extern void	jbd2_journal_switch_revoke_table(journal_t *journal);
extern void	jbd2_clear_buffer_revoked_flags(journal_t *journal);

/*
 * The log thread user interface:
 *
 * Request space in the current transaction, and force transaction commit
 * transitions on demand.
 */

int __jbd2_log_space_left(journal_t *); /* Called with journal locked */
int jbd2_log_start_commit(journal_t *journal, tid_t tid);
int __jbd2_log_start_commit(journal_t *journal, tid_t tid);
int jbd2_journal_start_commit(journal_t *journal, tid_t *tid);
int jbd2_journal_force_commit_nested(journal_t *journal);
int jbd2_log_wait_commit(journal_t *journal, tid_t tid);
int jbd2_log_do_checkpoint(journal_t *journal);
int jbd2_trans_will_send_data_barrier(journal_t *journal, tid_t tid);

void __jbd2_log_wait_for_space(journal_t *journal);
extern void __jbd2_journal_drop_transaction(journal_t *, transaction_t *);
extern int jbd2_cleanup_journal_tail(journal_t *);

/* Debugging code only: */

#define jbd_ENOSYS() \
do {								           \
	printk (KERN_ERR "JBD unimplemented function %s\n", __func__); \
	current->state = TASK_UNINTERRUPTIBLE;			           \
	schedule();						           \
} while (1)

/*
 * is_journal_abort
 *
 * Simple test wrapper function to test the JBD2_ABORT state flag.  This
 * bit, when set, indicates that we have had a fatal error somewhere,
 * either inside the journaling layer or indicated to us by the client
 * (eg. ext3), and that we and should not commit any further
 * transactions.
 */

static inline int is_journal_aborted(journal_t *journal)
{
	return journal->j_flags & JBD2_ABORT;
}

static inline int is_handle_aborted(handle_t *handle)
{
	if (handle->h_aborted)
		return 1;
	return is_journal_aborted(handle->h_transaction->t_journal);
}

static inline void jbd2_journal_abort_handle(handle_t *handle)
{
	handle->h_aborted = 1;
}

#endif /* __KERNEL__   */

/* Comparison functions for transaction IDs: perform comparisons using
 * modulo arithmetic so that they work over sequence number wraps. */

static inline int tid_gt(tid_t x, tid_t y)
{
	int difference = (x - y);
	return (difference > 0);
}

static inline int tid_geq(tid_t x, tid_t y)
{
	int difference = (x - y);
	return (difference >= 0);
}

extern int jbd2_journal_blocks_per_page(struct inode *inode);
extern size_t journal_tag_bytes(journal_t *journal);

/*
 * Return the minimum number of blocks which must be free in the journal
 * before a new transaction may be started.  Must be called under j_state_lock.
 */
static inline int jbd_space_needed(journal_t *journal)
{
	int nblocks = journal->j_max_transaction_buffers;
	if (journal->j_committing_transaction)
		nblocks += atomic_read(&journal->j_committing_transaction->
				       t_outstanding_credits);
	return nblocks;
}

/*
 * Definitions which augment the buffer_head layer
 */

/* journaling buffer types */
#define BJ_None		0	/* Not journaled */
#define BJ_Metadata	1	/* Normal journaled metadata */
#define BJ_Forget	2	/* Buffer superseded by this transaction */
#define BJ_IO		3	/* Buffer is for temporary IO use */
#define BJ_Shadow	4	/* Buffer contents being shadowed to the log */
#define BJ_LogCtl	5	/* Buffer contains log descriptors */
#define BJ_Reserved	6	/* Buffer is reserved for access by journal */
#define BJ_Types	7

extern int jbd_blocks_per_page(struct inode *inode);

/* JBD uses a CRC32 checksum */
#define JBD_MAX_CHECKSUM_SIZE 4

static inline u32 jbd2_chksum(journal_t *journal, u32 crc,
			      const void *address, unsigned int length)
{
	struct {
		struct shash_desc shash;
		char ctx[JBD_MAX_CHECKSUM_SIZE];
	} desc;
	int err;

	BUG_ON(crypto_shash_descsize(journal->j_chksum_driver) >
		JBD_MAX_CHECKSUM_SIZE);

	desc.shash.tfm = journal->j_chksum_driver;
	desc.shash.flags = 0;
	*(u32 *)desc.ctx = crc;

	err = crypto_shash_update(&desc.shash, address, length);
	BUG_ON(err);

	return *(u32 *)desc.ctx;
}

#ifdef __KERNEL__

#define buffer_trace_init(bh)	do {} while (0)
#define print_buffer_fields(bh)	do {} while (0)
#define print_buffer_trace(bh)	do {} while (0)
#define BUFFER_TRACE(bh, info)	do {} while (0)
#define BUFFER_TRACE2(bh, bh2, info)	do {} while (0)
#define JBUFFER_TRACE(jh, info)	do {} while (0)

#endif	/* __KERNEL__ */

#endif	/* _LINUX_JBD2_H */
