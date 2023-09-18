// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_BUF_H__
#define __XFS_BUF_H__

#include <linux/list.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/dax.h>
#include <linux/uio.h>
#include <linux/list_lru.h>

extern struct kmem_cache *xfs_buf_cache;

/*
 *	Base types
 */
struct xfs_buf;

#define XFS_BUF_DADDR_NULL	((xfs_daddr_t) (-1LL))

#define XBF_READ	 (1u << 0) /* buffer intended for reading from device */
#define XBF_WRITE	 (1u << 1) /* buffer intended for writing to device */
#define XBF_READ_AHEAD	 (1u << 2) /* asynchronous read-ahead */
#define XBF_NO_IOACCT	 (1u << 3) /* bypass I/O accounting (non-LRU bufs) */
#define XBF_ASYNC	 (1u << 4) /* initiator will not wait for completion */
#define XBF_DONE	 (1u << 5) /* all pages in the buffer uptodate */
#define XBF_STALE	 (1u << 6) /* buffer has been staled, do not find it */
#define XBF_WRITE_FAIL	 (1u << 7) /* async writes have failed on this buffer */

/* buffer type flags for write callbacks */
#define _XBF_INODES	 (1u << 16)/* inode buffer */
#define _XBF_DQUOTS	 (1u << 17)/* dquot buffer */
#define _XBF_LOGRECOVERY (1u << 18)/* log recovery buffer */

/* flags used only internally */
#define _XBF_PAGES	 (1u << 20)/* backed by refcounted pages */
#define _XBF_KMEM	 (1u << 21)/* backed by heap memory */
#define _XBF_DELWRI_Q	 (1u << 22)/* buffer on a delwri queue */

/* flags used only as arguments to access routines */
/*
 * Online fsck is scanning the buffer cache for live buffers.  Do not warn
 * about length mismatches during lookups and do not return stale buffers.
 */
#define XBF_LIVESCAN	 (1u << 28)
#define XBF_INCORE	 (1u << 29)/* lookup only, return if found in cache */
#define XBF_TRYLOCK	 (1u << 30)/* lock requested, but do not wait */
#define XBF_UNMAPPED	 (1u << 31)/* do not map the buffer */


typedef unsigned int xfs_buf_flags_t;

#define XFS_BUF_FLAGS \
	{ XBF_READ,		"READ" }, \
	{ XBF_WRITE,		"WRITE" }, \
	{ XBF_READ_AHEAD,	"READ_AHEAD" }, \
	{ XBF_NO_IOACCT,	"NO_IOACCT" }, \
	{ XBF_ASYNC,		"ASYNC" }, \
	{ XBF_DONE,		"DONE" }, \
	{ XBF_STALE,		"STALE" }, \
	{ XBF_WRITE_FAIL,	"WRITE_FAIL" }, \
	{ _XBF_INODES,		"INODES" }, \
	{ _XBF_DQUOTS,		"DQUOTS" }, \
	{ _XBF_LOGRECOVERY,	"LOG_RECOVERY" }, \
	{ _XBF_PAGES,		"PAGES" }, \
	{ _XBF_KMEM,		"KMEM" }, \
	{ _XBF_DELWRI_Q,	"DELWRI_Q" }, \
	/* The following interface flags should never be set */ \
	{ XBF_LIVESCAN,		"LIVESCAN" }, \
	{ XBF_INCORE,		"INCORE" }, \
	{ XBF_TRYLOCK,		"TRYLOCK" }, \
	{ XBF_UNMAPPED,		"UNMAPPED" }

/*
 * Internal state flags.
 */
#define XFS_BSTATE_DISPOSE	 (1 << 0)	/* buffer being discarded */
#define XFS_BSTATE_IN_FLIGHT	 (1 << 1)	/* I/O in flight */

/*
 * The xfs_buftarg contains 2 notions of "sector size" -
 *
 * 1) The metadata sector size, which is the minimum unit and
 *    alignment of IO which will be performed by metadata operations.
 * 2) The device logical sector size
 *
 * The first is specified at mkfs time, and is stored on-disk in the
 * superblock's sb_sectsize.
 *
 * The latter is derived from the underlying device, and controls direct IO
 * alignment constraints.
 */
typedef struct xfs_buftarg {
	dev_t			bt_dev;
	struct block_device	*bt_bdev;
	struct dax_device	*bt_daxdev;
	u64			bt_dax_part_off;
	struct xfs_mount	*bt_mount;
	unsigned int		bt_meta_sectorsize;
	size_t			bt_meta_sectormask;
	size_t			bt_logical_sectorsize;
	size_t			bt_logical_sectormask;

	/* LRU control structures */
	struct shrinker		bt_shrinker;
	struct list_lru		bt_lru;

	struct percpu_counter	bt_io_count;
	struct ratelimit_state	bt_ioerror_rl;
} xfs_buftarg_t;

#define XB_PAGES	2

struct xfs_buf_map {
	xfs_daddr_t		bm_bn;	/* block number for I/O */
	int			bm_len;	/* size of I/O */
	unsigned int		bm_flags;
};

/*
 * Online fsck is scanning the buffer cache for live buffers.  Do not warn
 * about length mismatches during lookups and do not return stale buffers.
 */
#define XBM_LIVESCAN		(1U << 0)

#define DEFINE_SINGLE_BUF_MAP(map, blkno, numblk) \
	struct xfs_buf_map (map) = { .bm_bn = (blkno), .bm_len = (numblk) };

struct xfs_buf_ops {
	char *name;
	union {
		__be32 magic[2];	/* v4 and v5 on disk magic values */
		__be16 magic16[2];	/* v4 and v5 on disk magic values */
	};
	void (*verify_read)(struct xfs_buf *);
	void (*verify_write)(struct xfs_buf *);
	xfs_failaddr_t (*verify_struct)(struct xfs_buf *bp);
};

struct xfs_buf {
	/*
	 * first cacheline holds all the fields needed for an uncontended cache
	 * hit to be fully processed. The semaphore straddles the cacheline
	 * boundary, but the counter and lock sits on the first cacheline,
	 * which is the only bit that is touched if we hit the semaphore
	 * fast-path on locking.
	 */
	struct rhash_head	b_rhash_head;	/* pag buffer hash node */

	xfs_daddr_t		b_rhash_key;	/* buffer cache index */
	int			b_length;	/* size of buffer in BBs */
	atomic_t		b_hold;		/* reference count */
	atomic_t		b_lru_ref;	/* lru reclaim ref count */
	xfs_buf_flags_t		b_flags;	/* status flags */
	struct semaphore	b_sema;		/* semaphore for lockables */

	/*
	 * concurrent access to b_lru and b_lru_flags are protected by
	 * bt_lru_lock and not by b_sema
	 */
	struct list_head	b_lru;		/* lru list */
	spinlock_t		b_lock;		/* internal state lock */
	unsigned int		b_state;	/* internal state flags */
	int			b_io_error;	/* internal IO error state */
	wait_queue_head_t	b_waiters;	/* unpin waiters */
	struct list_head	b_list;
	struct xfs_perag	*b_pag;		/* contains rbtree root */
	struct xfs_mount	*b_mount;
	struct xfs_buftarg	*b_target;	/* buffer target (device) */
	void			*b_addr;	/* virtual address of buffer */
	struct work_struct	b_ioend_work;
	struct completion	b_iowait;	/* queue for I/O waiters */
	struct xfs_buf_log_item	*b_log_item;
	struct list_head	b_li_list;	/* Log items list head */
	struct xfs_trans	*b_transp;
	struct page		**b_pages;	/* array of page pointers */
	struct page		*b_page_array[XB_PAGES]; /* inline pages */
	struct xfs_buf_map	*b_maps;	/* compound buffer map */
	struct xfs_buf_map	__b_map;	/* inline compound buffer map */
	int			b_map_count;
	atomic_t		b_pin_count;	/* pin count */
	atomic_t		b_io_remaining;	/* #outstanding I/O requests */
	unsigned int		b_page_count;	/* size of page array */
	unsigned int		b_offset;	/* page offset of b_addr,
						   only for _XBF_KMEM buffers */
	int			b_error;	/* error code on I/O */

	/*
	 * async write failure retry count. Initialised to zero on the first
	 * failure, then when it exceeds the maximum configured without a
	 * success the write is considered to be failed permanently and the
	 * iodone handler will take appropriate action.
	 *
	 * For retry timeouts, we record the jiffie of the first failure. This
	 * means that we can change the retry timeout for buffers already under
	 * I/O and thus avoid getting stuck in a retry loop with a long timeout.
	 *
	 * last_error is used to ensure that we are getting repeated errors, not
	 * different errors. e.g. a block device might change ENOSPC to EIO when
	 * a failure timeout occurs, so we want to re-initialise the error
	 * retry behaviour appropriately when that happens.
	 */
	int			b_retries;
	unsigned long		b_first_retry_time; /* in jiffies */
	int			b_last_error;

	const struct xfs_buf_ops	*b_ops;
	struct rcu_head		b_rcu;
};

/* Finding and Reading Buffers */
int xfs_buf_get_map(struct xfs_buftarg *target, struct xfs_buf_map *map,
		int nmaps, xfs_buf_flags_t flags, struct xfs_buf **bpp);
int xfs_buf_read_map(struct xfs_buftarg *target, struct xfs_buf_map *map,
		int nmaps, xfs_buf_flags_t flags, struct xfs_buf **bpp,
		const struct xfs_buf_ops *ops, xfs_failaddr_t fa);
void xfs_buf_readahead_map(struct xfs_buftarg *target,
			       struct xfs_buf_map *map, int nmaps,
			       const struct xfs_buf_ops *ops);

static inline int
xfs_buf_incore(
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	size_t			numblks,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp)
{
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);

	return xfs_buf_get_map(target, &map, 1, XBF_INCORE | flags, bpp);
}

static inline int
xfs_buf_get(
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	size_t			numblks,
	struct xfs_buf		**bpp)
{
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);

	return xfs_buf_get_map(target, &map, 1, 0, bpp);
}

static inline int
xfs_buf_read(
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	size_t			numblks,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp,
	const struct xfs_buf_ops *ops)
{
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);

	return xfs_buf_read_map(target, &map, 1, flags, bpp, ops,
			__builtin_return_address(0));
}

static inline void
xfs_buf_readahead(
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	size_t			numblks,
	const struct xfs_buf_ops *ops)
{
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);
	return xfs_buf_readahead_map(target, &map, 1, ops);
}

int xfs_buf_get_uncached(struct xfs_buftarg *target, size_t numblks,
		xfs_buf_flags_t flags, struct xfs_buf **bpp);
int xfs_buf_read_uncached(struct xfs_buftarg *target, xfs_daddr_t daddr,
		size_t numblks, xfs_buf_flags_t flags, struct xfs_buf **bpp,
		const struct xfs_buf_ops *ops);
int _xfs_buf_read(struct xfs_buf *bp, xfs_buf_flags_t flags);
void xfs_buf_hold(struct xfs_buf *bp);

/* Releasing Buffers */
extern void xfs_buf_rele(struct xfs_buf *);

/* Locking and Unlocking Buffers */
extern int xfs_buf_trylock(struct xfs_buf *);
extern void xfs_buf_lock(struct xfs_buf *);
extern void xfs_buf_unlock(struct xfs_buf *);
#define xfs_buf_islocked(bp) \
	((bp)->b_sema.count <= 0)

static inline void xfs_buf_relse(struct xfs_buf *bp)
{
	xfs_buf_unlock(bp);
	xfs_buf_rele(bp);
}

/* Buffer Read and Write Routines */
extern int xfs_bwrite(struct xfs_buf *bp);

extern void __xfs_buf_ioerror(struct xfs_buf *bp, int error,
		xfs_failaddr_t failaddr);
#define xfs_buf_ioerror(bp, err) __xfs_buf_ioerror((bp), (err), __this_address)
extern void xfs_buf_ioerror_alert(struct xfs_buf *bp, xfs_failaddr_t fa);
void xfs_buf_ioend_fail(struct xfs_buf *);
void xfs_buf_zero(struct xfs_buf *bp, size_t boff, size_t bsize);
void __xfs_buf_mark_corrupt(struct xfs_buf *bp, xfs_failaddr_t fa);
#define xfs_buf_mark_corrupt(bp) __xfs_buf_mark_corrupt((bp), __this_address)

/* Buffer Utility Routines */
extern void *xfs_buf_offset(struct xfs_buf *, size_t);
extern void xfs_buf_stale(struct xfs_buf *bp);

/* Delayed Write Buffer Routines */
extern void xfs_buf_delwri_cancel(struct list_head *);
extern bool xfs_buf_delwri_queue(struct xfs_buf *, struct list_head *);
extern int xfs_buf_delwri_submit(struct list_head *);
extern int xfs_buf_delwri_submit_nowait(struct list_head *);
extern int xfs_buf_delwri_pushbuf(struct xfs_buf *, struct list_head *);

static inline xfs_daddr_t xfs_buf_daddr(struct xfs_buf *bp)
{
	return bp->b_maps[0].bm_bn;
}

void xfs_buf_set_ref(struct xfs_buf *bp, int lru_ref);

/*
 * If the buffer is already on the LRU, do nothing. Otherwise set the buffer
 * up with a reference count of 0 so it will be tossed from the cache when
 * released.
 */
static inline void xfs_buf_oneshot(struct xfs_buf *bp)
{
	if (!list_empty(&bp->b_lru) || atomic_read(&bp->b_lru_ref) > 1)
		return;
	atomic_set(&bp->b_lru_ref, 0);
}

static inline int xfs_buf_ispinned(struct xfs_buf *bp)
{
	return atomic_read(&bp->b_pin_count);
}

static inline int
xfs_buf_verify_cksum(struct xfs_buf *bp, unsigned long cksum_offset)
{
	return xfs_verify_cksum(bp->b_addr, BBTOB(bp->b_length),
				cksum_offset);
}

static inline void
xfs_buf_update_cksum(struct xfs_buf *bp, unsigned long cksum_offset)
{
	xfs_update_cksum(bp->b_addr, BBTOB(bp->b_length),
			 cksum_offset);
}

/*
 *	Handling of buftargs.
 */
struct xfs_buftarg *xfs_alloc_buftarg(struct xfs_mount *mp,
		struct block_device *bdev);
extern void xfs_free_buftarg(struct xfs_buftarg *);
extern void xfs_buftarg_wait(struct xfs_buftarg *);
extern void xfs_buftarg_drain(struct xfs_buftarg *);
extern int xfs_setsize_buftarg(struct xfs_buftarg *, unsigned int);

#define xfs_getsize_buftarg(buftarg)	block_size((buftarg)->bt_bdev)
#define xfs_readonly_buftarg(buftarg)	bdev_read_only((buftarg)->bt_bdev)

int xfs_buf_reverify(struct xfs_buf *bp, const struct xfs_buf_ops *ops);
bool xfs_verify_magic(struct xfs_buf *bp, __be32 dmagic);
bool xfs_verify_magic16(struct xfs_buf *bp, __be16 dmagic);

#endif	/* __XFS_BUF_H__ */
