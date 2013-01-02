/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_BUF_H__
#define __XFS_BUF_H__

#include <linux/list.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/uio.h>

/*
 *	Base types
 */

#define XFS_BUF_DADDR_NULL	((xfs_daddr_t) (-1LL))

typedef enum {
	XBRW_READ = 1,			/* transfer into target memory */
	XBRW_WRITE = 2,			/* transfer from target memory */
	XBRW_ZERO = 3,			/* Zero target memory */
} xfs_buf_rw_t;

#define XBF_READ	 (1 << 0) /* buffer intended for reading from device */
#define XBF_WRITE	 (1 << 1) /* buffer intended for writing to device */
#define XBF_READ_AHEAD	 (1 << 2) /* asynchronous read-ahead */
#define XBF_ASYNC	 (1 << 4) /* initiator will not wait for completion */
#define XBF_DONE	 (1 << 5) /* all pages in the buffer uptodate */
#define XBF_STALE	 (1 << 6) /* buffer has been staled, do not find it */

/* I/O hints for the BIO layer */
#define XBF_SYNCIO	 (1 << 10)/* treat this buffer as synchronous I/O */
#define XBF_FUA		 (1 << 11)/* force cache write through mode */
#define XBF_FLUSH	 (1 << 12)/* flush the disk cache before a write */

/* flags used only as arguments to access routines */
#define XBF_TRYLOCK	 (1 << 16)/* lock requested, but do not wait */
#define XBF_UNMAPPED	 (1 << 17)/* do not map the buffer */

/* flags used only internally */
#define _XBF_PAGES	 (1 << 20)/* backed by refcounted pages */
#define _XBF_KMEM	 (1 << 21)/* backed by heap memory */
#define _XBF_DELWRI_Q	 (1 << 22)/* buffer on a delwri queue */
#define _XBF_COMPOUND	 (1 << 23)/* compound buffer */
#define _XBF_LRU_DISPOSE (1 << 24)/* buffer being discarded */

typedef unsigned int xfs_buf_flags_t;

#define XFS_BUF_FLAGS \
	{ XBF_READ,		"READ" }, \
	{ XBF_WRITE,		"WRITE" }, \
	{ XBF_READ_AHEAD,	"READ_AHEAD" }, \
	{ XBF_ASYNC,		"ASYNC" }, \
	{ XBF_DONE,		"DONE" }, \
	{ XBF_STALE,		"STALE" }, \
	{ XBF_SYNCIO,		"SYNCIO" }, \
	{ XBF_FUA,		"FUA" }, \
	{ XBF_FLUSH,		"FLUSH" }, \
	{ XBF_TRYLOCK,		"TRYLOCK" },	/* should never be set */\
	{ XBF_UNMAPPED,		"UNMAPPED" },	/* ditto */\
	{ _XBF_PAGES,		"PAGES" }, \
	{ _XBF_KMEM,		"KMEM" }, \
	{ _XBF_DELWRI_Q,	"DELWRI_Q" }, \
	{ _XBF_COMPOUND,	"COMPOUND" }, \
	{ _XBF_LRU_DISPOSE,	"LRU_DISPOSE" }

typedef struct xfs_buftarg {
	dev_t			bt_dev;
	struct block_device	*bt_bdev;
	struct backing_dev_info	*bt_bdi;
	struct xfs_mount	*bt_mount;
	unsigned int		bt_bsize;
	unsigned int		bt_sshift;
	size_t			bt_smask;

	/* LRU control structures */
	struct shrinker		bt_shrinker;
	struct list_head	bt_lru;
	spinlock_t		bt_lru_lock;
	unsigned int		bt_lru_nr;
} xfs_buftarg_t;

struct xfs_buf;
typedef void (*xfs_buf_iodone_t)(struct xfs_buf *);


#define XB_PAGES	2

struct xfs_buf_map {
	xfs_daddr_t		bm_bn;	/* block number for I/O */
	int			bm_len;	/* size of I/O */
};

#define DEFINE_SINGLE_BUF_MAP(map, blkno, numblk) \
	struct xfs_buf_map (map) = { .bm_bn = (blkno), .bm_len = (numblk) };

struct xfs_buf_ops {
	void (*verify_read)(struct xfs_buf *);
	void (*verify_write)(struct xfs_buf *);
};

typedef struct xfs_buf {
	/*
	 * first cacheline holds all the fields needed for an uncontended cache
	 * hit to be fully processed. The semaphore straddles the cacheline
	 * boundary, but the counter and lock sits on the first cacheline,
	 * which is the only bit that is touched if we hit the semaphore
	 * fast-path on locking.
	 */
	struct rb_node		b_rbnode;	/* rbtree node */
	xfs_daddr_t		b_bn;		/* block number of buffer */
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
	xfs_buf_flags_t		b_lru_flags;	/* internal lru status flags */
	wait_queue_head_t	b_waiters;	/* unpin waiters */
	struct list_head	b_list;
	struct xfs_perag	*b_pag;		/* contains rbtree root */
	xfs_buftarg_t		*b_target;	/* buffer target (device) */
	void			*b_addr;	/* virtual address of buffer */
	struct work_struct	b_iodone_work;
	xfs_buf_iodone_t	b_iodone;	/* I/O completion function */
	struct completion	b_iowait;	/* queue for I/O waiters */
	void			*b_fspriv;
	struct xfs_trans	*b_transp;
	struct page		**b_pages;	/* array of page pointers */
	struct page		*b_page_array[XB_PAGES]; /* inline pages */
	struct xfs_buf_map	*b_maps;	/* compound buffer map */
	struct xfs_buf_map	b_map;		/* inline compound buffer map */
	int			b_map_count;
	int			b_io_length;	/* IO size in BBs */
	atomic_t		b_pin_count;	/* pin count */
	atomic_t		b_io_remaining;	/* #outstanding I/O requests */
	unsigned int		b_page_count;	/* size of page array */
	unsigned int		b_offset;	/* page offset in first page */
	unsigned short		b_error;	/* error code on I/O */
	const struct xfs_buf_ops	*b_ops;

#ifdef XFS_BUF_LOCK_TRACKING
	int			b_last_holder;
#endif
} xfs_buf_t;

/* Finding and Reading Buffers */
struct xfs_buf *_xfs_buf_find(struct xfs_buftarg *target,
			      struct xfs_buf_map *map, int nmaps,
			      xfs_buf_flags_t flags, struct xfs_buf *new_bp);

static inline struct xfs_buf *
xfs_incore(
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	size_t			numblks,
	xfs_buf_flags_t		flags)
{
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);
	return _xfs_buf_find(target, &map, 1, flags, NULL);
}

struct xfs_buf *_xfs_buf_alloc(struct xfs_buftarg *target,
			       struct xfs_buf_map *map, int nmaps,
			       xfs_buf_flags_t flags);

static inline struct xfs_buf *
xfs_buf_alloc(
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	size_t			numblks,
	xfs_buf_flags_t		flags)
{
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);
	return _xfs_buf_alloc(target, &map, 1, flags);
}

struct xfs_buf *xfs_buf_get_map(struct xfs_buftarg *target,
			       struct xfs_buf_map *map, int nmaps,
			       xfs_buf_flags_t flags);
struct xfs_buf *xfs_buf_read_map(struct xfs_buftarg *target,
			       struct xfs_buf_map *map, int nmaps,
			       xfs_buf_flags_t flags,
			       const struct xfs_buf_ops *ops);
void xfs_buf_readahead_map(struct xfs_buftarg *target,
			       struct xfs_buf_map *map, int nmaps,
			       const struct xfs_buf_ops *ops);

static inline struct xfs_buf *
xfs_buf_get(
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	size_t			numblks,
	xfs_buf_flags_t		flags)
{
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);
	return xfs_buf_get_map(target, &map, 1, flags);
}

static inline struct xfs_buf *
xfs_buf_read(
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	size_t			numblks,
	xfs_buf_flags_t		flags,
	const struct xfs_buf_ops *ops)
{
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);
	return xfs_buf_read_map(target, &map, 1, flags, ops);
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

struct xfs_buf *xfs_buf_get_empty(struct xfs_buftarg *target, size_t numblks);
void xfs_buf_set_empty(struct xfs_buf *bp, size_t numblks);
int xfs_buf_associate_memory(struct xfs_buf *bp, void *mem, size_t length);

struct xfs_buf *xfs_buf_get_uncached(struct xfs_buftarg *target, size_t numblks,
				int flags);
struct xfs_buf *xfs_buf_read_uncached(struct xfs_buftarg *target,
				xfs_daddr_t daddr, size_t numblks, int flags,
				const struct xfs_buf_ops *ops);
void xfs_buf_hold(struct xfs_buf *bp);

/* Releasing Buffers */
extern void xfs_buf_free(xfs_buf_t *);
extern void xfs_buf_rele(xfs_buf_t *);

/* Locking and Unlocking Buffers */
extern int xfs_buf_trylock(xfs_buf_t *);
extern void xfs_buf_lock(xfs_buf_t *);
extern void xfs_buf_unlock(xfs_buf_t *);
#define xfs_buf_islocked(bp) \
	((bp)->b_sema.count <= 0)

/* Buffer Read and Write Routines */
extern int xfs_bwrite(struct xfs_buf *bp);

extern void xfsbdstrat(struct xfs_mount *, struct xfs_buf *);

extern void xfs_buf_ioend(xfs_buf_t *,	int);
extern void xfs_buf_ioerror(xfs_buf_t *, int);
extern void xfs_buf_ioerror_alert(struct xfs_buf *, const char *func);
extern void xfs_buf_iorequest(xfs_buf_t *);
extern int xfs_buf_iowait(xfs_buf_t *);
extern void xfs_buf_iomove(xfs_buf_t *, size_t, size_t, void *,
				xfs_buf_rw_t);
#define xfs_buf_zero(bp, off, len) \
	    xfs_buf_iomove((bp), (off), (len), NULL, XBRW_ZERO)

static inline int xfs_buf_geterror(xfs_buf_t *bp)
{
	return bp ? bp->b_error : ENOMEM;
}

/* Buffer Utility Routines */
extern xfs_caddr_t xfs_buf_offset(xfs_buf_t *, size_t);

/* Delayed Write Buffer Routines */
extern bool xfs_buf_delwri_queue(struct xfs_buf *, struct list_head *);
extern int xfs_buf_delwri_submit(struct list_head *);
extern int xfs_buf_delwri_submit_nowait(struct list_head *);

/* Buffer Daemon Setup Routines */
extern int xfs_buf_init(void);
extern void xfs_buf_terminate(void);

#define XFS_BUF_ZEROFLAGS(bp) \
	((bp)->b_flags &= ~(XBF_READ|XBF_WRITE|XBF_ASYNC| \
			    XBF_SYNCIO|XBF_FUA|XBF_FLUSH))

void xfs_buf_stale(struct xfs_buf *bp);
#define XFS_BUF_UNSTALE(bp)	((bp)->b_flags &= ~XBF_STALE)
#define XFS_BUF_ISSTALE(bp)	((bp)->b_flags & XBF_STALE)

#define XFS_BUF_DONE(bp)	((bp)->b_flags |= XBF_DONE)
#define XFS_BUF_UNDONE(bp)	((bp)->b_flags &= ~XBF_DONE)
#define XFS_BUF_ISDONE(bp)	((bp)->b_flags & XBF_DONE)

#define XFS_BUF_ASYNC(bp)	((bp)->b_flags |= XBF_ASYNC)
#define XFS_BUF_UNASYNC(bp)	((bp)->b_flags &= ~XBF_ASYNC)
#define XFS_BUF_ISASYNC(bp)	((bp)->b_flags & XBF_ASYNC)

#define XFS_BUF_READ(bp)	((bp)->b_flags |= XBF_READ)
#define XFS_BUF_UNREAD(bp)	((bp)->b_flags &= ~XBF_READ)
#define XFS_BUF_ISREAD(bp)	((bp)->b_flags & XBF_READ)

#define XFS_BUF_WRITE(bp)	((bp)->b_flags |= XBF_WRITE)
#define XFS_BUF_UNWRITE(bp)	((bp)->b_flags &= ~XBF_WRITE)
#define XFS_BUF_ISWRITE(bp)	((bp)->b_flags & XBF_WRITE)

/*
 * These macros use the IO block map rather than b_bn. b_bn is now really
 * just for the buffer cache index for cached buffers. As IO does not use b_bn
 * anymore, uncached buffers do not use b_bn at all and hence must modify the IO
 * map directly. Uncached buffers are not allowed to be discontiguous, so this
 * is safe to do.
 *
 * In future, uncached buffers will pass the block number directly to the io
 * request function and hence these macros will go away at that point.
 */
#define XFS_BUF_ADDR(bp)		((bp)->b_map.bm_bn)
#define XFS_BUF_SET_ADDR(bp, bno)	((bp)->b_map.bm_bn = (xfs_daddr_t)(bno))

static inline void xfs_buf_set_ref(struct xfs_buf *bp, int lru_ref)
{
	atomic_set(&bp->b_lru_ref, lru_ref);
}

static inline int xfs_buf_ispinned(struct xfs_buf *bp)
{
	return atomic_read(&bp->b_pin_count);
}

static inline void xfs_buf_relse(xfs_buf_t *bp)
{
	xfs_buf_unlock(bp);
	xfs_buf_rele(bp);
}

/*
 *	Handling of buftargs.
 */
extern xfs_buftarg_t *xfs_alloc_buftarg(struct xfs_mount *,
			struct block_device *, int, const char *);
extern void xfs_free_buftarg(struct xfs_mount *, struct xfs_buftarg *);
extern void xfs_wait_buftarg(xfs_buftarg_t *);
extern int xfs_setsize_buftarg(xfs_buftarg_t *, unsigned int, unsigned int);

#define xfs_getsize_buftarg(buftarg)	block_size((buftarg)->bt_bdev)
#define xfs_readonly_buftarg(buftarg)	bdev_read_only((buftarg)->bt_bdev)

#endif	/* __XFS_BUF_H__ */
