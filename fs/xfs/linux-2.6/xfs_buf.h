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
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/uio.h>

/*
 *	Base types
 */

#define XFS_BUF_DADDR_NULL	((xfs_daddr_t) (-1LL))

#define xfs_buf_ctob(pp)	((pp) * PAGE_CACHE_SIZE)
#define xfs_buf_btoc(dd)	(((dd) + PAGE_CACHE_SIZE-1) >> PAGE_CACHE_SHIFT)
#define xfs_buf_btoct(dd)	((dd) >> PAGE_CACHE_SHIFT)
#define xfs_buf_poff(aa)	((aa) & ~PAGE_CACHE_MASK)

typedef enum {
	XBRW_READ = 1,			/* transfer into target memory */
	XBRW_WRITE = 2,			/* transfer from target memory */
	XBRW_ZERO = 3,			/* Zero target memory */
} xfs_buf_rw_t;

typedef enum {
	XBF_READ = (1 << 0),	/* buffer intended for reading from device */
	XBF_WRITE = (1 << 1),	/* buffer intended for writing to device   */
	XBF_MAPPED = (1 << 2),  /* buffer mapped (b_addr valid)            */
	XBF_ASYNC = (1 << 4),   /* initiator will not wait for completion  */
	XBF_DONE = (1 << 5),    /* all pages in the buffer uptodate	   */
	XBF_DELWRI = (1 << 6),  /* buffer has dirty pages                  */
	XBF_STALE = (1 << 7),	/* buffer has been staled, do not find it  */
	XBF_FS_MANAGED = (1 << 8),  /* filesystem controls freeing memory  */
 	XBF_ORDERED = (1 << 11),    /* use ordered writes		   */
	XBF_READ_AHEAD = (1 << 12), /* asynchronous read-ahead		   */

	/* flags used only as arguments to access routines */
	XBF_LOCK = (1 << 14),       /* lock requested			   */
	XBF_TRYLOCK = (1 << 15),    /* lock requested, but do not wait	   */
	XBF_DONT_BLOCK = (1 << 16), /* do not block in current thread	   */

	/* flags used only internally */
	_XBF_PAGE_CACHE = (1 << 17),/* backed by pagecache		   */
	_XBF_PAGES = (1 << 18),	    /* backed by refcounted pages	   */
	_XBF_RUN_QUEUES = (1 << 19),/* run block device task queue	   */
	_XBF_DELWRI_Q = (1 << 21),   /* buffer on delwri queue		   */

	/*
	 * Special flag for supporting metadata blocks smaller than a FSB.
	 *
	 * In this case we can have multiple xfs_buf_t on a single page and
	 * need to lock out concurrent xfs_buf_t readers as they only
	 * serialise access to the buffer.
	 *
	 * If the FSB size >= PAGE_CACHE_SIZE case, we have no serialisation
	 * between reads of the page. Hence we can have one thread read the
	 * page and modify it, but then race with another thread that thinks
	 * the page is not up-to-date and hence reads it again.
	 *
	 * The result is that the first modifcation to the page is lost.
	 * This sort of AGF/AGI reading race can happen when unlinking inodes
	 * that require truncation and results in the AGI unlinked list
	 * modifications being lost.
	 */
	_XBF_PAGE_LOCKED = (1 << 22),
} xfs_buf_flags_t;

typedef enum {
	XBT_FORCE_SLEEP = 0,
	XBT_FORCE_FLUSH = 1,
} xfs_buftarg_flags_t;

typedef struct xfs_bufhash {
	struct list_head	bh_list;
	spinlock_t		bh_lock;
} xfs_bufhash_t;

typedef struct xfs_buftarg {
	dev_t			bt_dev;
	struct block_device	*bt_bdev;
	struct address_space	*bt_mapping;
	unsigned int		bt_bsize;
	unsigned int		bt_sshift;
	size_t			bt_smask;

	/* per device buffer hash table */
	uint			bt_hashmask;
	uint			bt_hashshift;
	xfs_bufhash_t		*bt_hash;

	/* per device delwri queue */
	struct task_struct	*bt_task;
	struct list_head	bt_list;
	struct list_head	bt_delwrite_queue;
	spinlock_t		bt_delwrite_lock;
	unsigned long		bt_flags;
} xfs_buftarg_t;

/*
 *	xfs_buf_t:  Buffer structure for pagecache-based buffers
 *
 * This buffer structure is used by the pagecache buffer management routines
 * to refer to an assembly of pages forming a logical buffer.
 *
 * The buffer structure is used on a temporary basis only, and discarded when
 * released.  The real data storage is recorded in the pagecache. Buffers are
 * hashed to the block device on which the file system resides.
 */

struct xfs_buf;
typedef void (*xfs_buf_iodone_t)(struct xfs_buf *);
typedef void (*xfs_buf_relse_t)(struct xfs_buf *);
typedef int (*xfs_buf_bdstrat_t)(struct xfs_buf *);

#define XB_PAGES	2

typedef struct xfs_buf {
	struct semaphore	b_sema;		/* semaphore for lockables */
	unsigned long		b_queuetime;	/* time buffer was queued */
	atomic_t		b_pin_count;	/* pin count */
	wait_queue_head_t	b_waiters;	/* unpin waiters */
	struct list_head	b_list;
	xfs_buf_flags_t		b_flags;	/* status flags */
	struct list_head	b_hash_list;	/* hash table list */
	xfs_bufhash_t		*b_hash;	/* hash table list start */
	xfs_buftarg_t		*b_target;	/* buffer target (device) */
	atomic_t		b_hold;		/* reference count */
	xfs_daddr_t		b_bn;		/* block number for I/O */
	xfs_off_t		b_file_offset;	/* offset in file */
	size_t			b_buffer_length;/* size of buffer in bytes */
	size_t			b_count_desired;/* desired transfer size */
	void			*b_addr;	/* virtual address of buffer */
	struct work_struct	b_iodone_work;
	atomic_t		b_io_remaining;	/* #outstanding I/O requests */
	xfs_buf_iodone_t	b_iodone;	/* I/O completion function */
	xfs_buf_relse_t		b_relse;	/* releasing function */
	xfs_buf_bdstrat_t	b_strat;	/* pre-write function */
	struct completion	b_iowait;	/* queue for I/O waiters */
	void			*b_fspriv;
	void			*b_fspriv2;
	void			*b_fspriv3;
	unsigned short		b_error;	/* error code on I/O */
	unsigned int		b_page_count;	/* size of page array */
	unsigned int		b_offset;	/* page offset in first page */
	struct page		**b_pages;	/* array of page pointers */
	struct page		*b_page_array[XB_PAGES]; /* inline pages */
#ifdef XFS_BUF_LOCK_TRACKING
	int			b_last_holder;
#endif
} xfs_buf_t;


/* Finding and Reading Buffers */
extern xfs_buf_t *_xfs_buf_find(xfs_buftarg_t *, xfs_off_t, size_t,
				xfs_buf_flags_t, xfs_buf_t *);
#define xfs_incore(buftarg,blkno,len,lockit) \
	_xfs_buf_find(buftarg, blkno ,len, lockit, NULL)

extern xfs_buf_t *xfs_buf_get_flags(xfs_buftarg_t *, xfs_off_t, size_t,
				xfs_buf_flags_t);
#define xfs_buf_get(target, blkno, len, flags) \
	xfs_buf_get_flags((target), (blkno), (len), XBF_LOCK | XBF_MAPPED)

extern xfs_buf_t *xfs_buf_read_flags(xfs_buftarg_t *, xfs_off_t, size_t,
				xfs_buf_flags_t);
#define xfs_buf_read(target, blkno, len, flags) \
	xfs_buf_read_flags((target), (blkno), (len), XBF_LOCK | XBF_MAPPED)

extern xfs_buf_t *xfs_buf_get_empty(size_t, xfs_buftarg_t *);
extern xfs_buf_t *xfs_buf_get_noaddr(size_t, xfs_buftarg_t *);
extern int xfs_buf_associate_memory(xfs_buf_t *, void *, size_t);
extern void xfs_buf_hold(xfs_buf_t *);
extern void xfs_buf_readahead(xfs_buftarg_t *, xfs_off_t, size_t,
				xfs_buf_flags_t);

/* Releasing Buffers */
extern void xfs_buf_free(xfs_buf_t *);
extern void xfs_buf_rele(xfs_buf_t *);

/* Locking and Unlocking Buffers */
extern int xfs_buf_cond_lock(xfs_buf_t *);
extern int xfs_buf_lock_value(xfs_buf_t *);
extern void xfs_buf_lock(xfs_buf_t *);
extern void xfs_buf_unlock(xfs_buf_t *);

/* Buffer Read and Write Routines */
extern void xfs_buf_ioend(xfs_buf_t *,	int);
extern void xfs_buf_ioerror(xfs_buf_t *, int);
extern int xfs_buf_iostart(xfs_buf_t *, xfs_buf_flags_t);
extern int xfs_buf_iorequest(xfs_buf_t *);
extern int xfs_buf_iowait(xfs_buf_t *);
extern void xfs_buf_iomove(xfs_buf_t *, size_t, size_t, xfs_caddr_t,
				xfs_buf_rw_t);

static inline int xfs_buf_iostrategy(xfs_buf_t *bp)
{
	return bp->b_strat ? bp->b_strat(bp) : xfs_buf_iorequest(bp);
}

static inline int xfs_buf_geterror(xfs_buf_t *bp)
{
	return bp ? bp->b_error : ENOMEM;
}

/* Buffer Utility Routines */
extern xfs_caddr_t xfs_buf_offset(xfs_buf_t *, size_t);

/* Pinning Buffer Storage in Memory */
extern void xfs_buf_pin(xfs_buf_t *);
extern void xfs_buf_unpin(xfs_buf_t *);
extern int xfs_buf_ispin(xfs_buf_t *);

/* Delayed Write Buffer Routines */
extern void xfs_buf_delwri_dequeue(xfs_buf_t *);

/* Buffer Daemon Setup Routines */
extern int xfs_buf_init(void);
extern void xfs_buf_terminate(void);

#ifdef XFS_BUF_TRACE
extern ktrace_t *xfs_buf_trace_buf;
extern void xfs_buf_trace(xfs_buf_t *, char *, void *, void *);
#else
#define xfs_buf_trace(bp,id,ptr,ra)	do { } while (0)
#endif

#define xfs_buf_target_name(target)	\
	({ char __b[BDEVNAME_SIZE]; bdevname((target)->bt_bdev, __b); __b; })


#define XFS_B_ASYNC		XBF_ASYNC
#define XFS_B_DELWRI		XBF_DELWRI
#define XFS_B_READ		XBF_READ
#define XFS_B_WRITE		XBF_WRITE
#define XFS_B_STALE		XBF_STALE

#define XFS_BUF_TRYLOCK		XBF_TRYLOCK
#define XFS_INCORE_TRYLOCK	XBF_TRYLOCK
#define XFS_BUF_LOCK		XBF_LOCK
#define XFS_BUF_MAPPED		XBF_MAPPED

#define BUF_BUSY		XBF_DONT_BLOCK

#define XFS_BUF_BFLAGS(bp)	((bp)->b_flags)
#define XFS_BUF_ZEROFLAGS(bp)	((bp)->b_flags &= \
		~(XBF_READ|XBF_WRITE|XBF_ASYNC|XBF_DELWRI|XBF_ORDERED))

#define XFS_BUF_STALE(bp)	((bp)->b_flags |= XFS_B_STALE)
#define XFS_BUF_UNSTALE(bp)	((bp)->b_flags &= ~XFS_B_STALE)
#define XFS_BUF_ISSTALE(bp)	((bp)->b_flags & XFS_B_STALE)
#define XFS_BUF_SUPER_STALE(bp)	do {				\
					XFS_BUF_STALE(bp);	\
					xfs_buf_delwri_dequeue(bp);	\
					XFS_BUF_DONE(bp);	\
				} while (0)

#define XFS_BUF_MANAGE		XBF_FS_MANAGED
#define XFS_BUF_UNMANAGE(bp)	((bp)->b_flags &= ~XBF_FS_MANAGED)

#define XFS_BUF_DELAYWRITE(bp)		((bp)->b_flags |= XBF_DELWRI)
#define XFS_BUF_UNDELAYWRITE(bp)	xfs_buf_delwri_dequeue(bp)
#define XFS_BUF_ISDELAYWRITE(bp)	((bp)->b_flags & XBF_DELWRI)

#define XFS_BUF_ERROR(bp,no)	xfs_buf_ioerror(bp,no)
#define XFS_BUF_GETERROR(bp)	xfs_buf_geterror(bp)
#define XFS_BUF_ISERROR(bp)	(xfs_buf_geterror(bp) ? 1 : 0)

#define XFS_BUF_DONE(bp)	((bp)->b_flags |= XBF_DONE)
#define XFS_BUF_UNDONE(bp)	((bp)->b_flags &= ~XBF_DONE)
#define XFS_BUF_ISDONE(bp)	((bp)->b_flags & XBF_DONE)

#define XFS_BUF_BUSY(bp)	do { } while (0)
#define XFS_BUF_UNBUSY(bp)	do { } while (0)
#define XFS_BUF_ISBUSY(bp)	(1)

#define XFS_BUF_ASYNC(bp)	((bp)->b_flags |= XBF_ASYNC)
#define XFS_BUF_UNASYNC(bp)	((bp)->b_flags &= ~XBF_ASYNC)
#define XFS_BUF_ISASYNC(bp)	((bp)->b_flags & XBF_ASYNC)

#define XFS_BUF_ORDERED(bp)	((bp)->b_flags |= XBF_ORDERED)
#define XFS_BUF_UNORDERED(bp)	((bp)->b_flags &= ~XBF_ORDERED)
#define XFS_BUF_ISORDERED(bp)	((bp)->b_flags & XBF_ORDERED)

#define XFS_BUF_SHUT(bp)	do { } while (0)
#define XFS_BUF_UNSHUT(bp)	do { } while (0)
#define XFS_BUF_ISSHUT(bp)	(0)

#define XFS_BUF_HOLD(bp)	xfs_buf_hold(bp)
#define XFS_BUF_READ(bp)	((bp)->b_flags |= XBF_READ)
#define XFS_BUF_UNREAD(bp)	((bp)->b_flags &= ~XBF_READ)
#define XFS_BUF_ISREAD(bp)	((bp)->b_flags & XBF_READ)

#define XFS_BUF_WRITE(bp)	((bp)->b_flags |= XBF_WRITE)
#define XFS_BUF_UNWRITE(bp)	((bp)->b_flags &= ~XBF_WRITE)
#define XFS_BUF_ISWRITE(bp)	((bp)->b_flags & XBF_WRITE)

#define XFS_BUF_IODONE_FUNC(bp)			((bp)->b_iodone)
#define XFS_BUF_SET_IODONE_FUNC(bp, func)	((bp)->b_iodone = (func))
#define XFS_BUF_CLR_IODONE_FUNC(bp)		((bp)->b_iodone = NULL)
#define XFS_BUF_SET_BDSTRAT_FUNC(bp, func)	((bp)->b_strat = (func))
#define XFS_BUF_CLR_BDSTRAT_FUNC(bp)		((bp)->b_strat = NULL)

#define XFS_BUF_FSPRIVATE(bp, type)		((type)(bp)->b_fspriv)
#define XFS_BUF_SET_FSPRIVATE(bp, val)		((bp)->b_fspriv = (void*)(val))
#define XFS_BUF_FSPRIVATE2(bp, type)		((type)(bp)->b_fspriv2)
#define XFS_BUF_SET_FSPRIVATE2(bp, val)		((bp)->b_fspriv2 = (void*)(val))
#define XFS_BUF_FSPRIVATE3(bp, type)		((type)(bp)->b_fspriv3)
#define XFS_BUF_SET_FSPRIVATE3(bp, val)		((bp)->b_fspriv3 = (void*)(val))
#define XFS_BUF_SET_START(bp)			do { } while (0)
#define XFS_BUF_SET_BRELSE_FUNC(bp, func)	((bp)->b_relse = (func))

#define XFS_BUF_PTR(bp)			(xfs_caddr_t)((bp)->b_addr)
#define XFS_BUF_SET_PTR(bp, val, cnt)	xfs_buf_associate_memory(bp, val, cnt)
#define XFS_BUF_ADDR(bp)		((bp)->b_bn)
#define XFS_BUF_SET_ADDR(bp, bno)	((bp)->b_bn = (xfs_daddr_t)(bno))
#define XFS_BUF_OFFSET(bp)		((bp)->b_file_offset)
#define XFS_BUF_SET_OFFSET(bp, off)	((bp)->b_file_offset = (off))
#define XFS_BUF_COUNT(bp)		((bp)->b_count_desired)
#define XFS_BUF_SET_COUNT(bp, cnt)	((bp)->b_count_desired = (cnt))
#define XFS_BUF_SIZE(bp)		((bp)->b_buffer_length)
#define XFS_BUF_SET_SIZE(bp, cnt)	((bp)->b_buffer_length = (cnt))

#define XFS_BUF_SET_VTYPE_REF(bp, type, ref)	do { } while (0)
#define XFS_BUF_SET_VTYPE(bp, type)		do { } while (0)
#define XFS_BUF_SET_REF(bp, ref)		do { } while (0)

#define XFS_BUF_ISPINNED(bp)	xfs_buf_ispin(bp)

#define XFS_BUF_VALUSEMA(bp)	xfs_buf_lock_value(bp)
#define XFS_BUF_CPSEMA(bp)	(xfs_buf_cond_lock(bp) == 0)
#define XFS_BUF_VSEMA(bp)	xfs_buf_unlock(bp)
#define XFS_BUF_PSEMA(bp,x)	xfs_buf_lock(bp)
#define XFS_BUF_FINISH_IOWAIT(bp)	complete(&bp->b_iowait);

#define XFS_BUF_SET_TARGET(bp, target)	((bp)->b_target = (target))
#define XFS_BUF_TARGET(bp)		((bp)->b_target)
#define XFS_BUFTARG_NAME(target)	xfs_buf_target_name(target)

static inline int xfs_bawrite(void *mp, xfs_buf_t *bp)
{
	bp->b_fspriv3 = mp;
	bp->b_strat = xfs_bdstrat_cb;
	xfs_buf_delwri_dequeue(bp);
	return xfs_buf_iostart(bp, XBF_WRITE | XBF_ASYNC | _XBF_RUN_QUEUES);
}

static inline void xfs_buf_relse(xfs_buf_t *bp)
{
	if (!bp->b_relse)
		xfs_buf_unlock(bp);
	xfs_buf_rele(bp);
}

#define xfs_bpin(bp)		xfs_buf_pin(bp)
#define xfs_bunpin(bp)		xfs_buf_unpin(bp)

#define xfs_buftrace(id, bp)	\
	    xfs_buf_trace(bp, id, NULL, (void *)__builtin_return_address(0))

#define xfs_biodone(bp)		xfs_buf_ioend(bp, 0)

#define xfs_biomove(bp, off, len, data, rw) \
	    xfs_buf_iomove((bp), (off), (len), (data), \
		((rw) == XFS_B_WRITE) ? XBRW_WRITE : XBRW_READ)

#define xfs_biozero(bp, off, len) \
	    xfs_buf_iomove((bp), (off), (len), NULL, XBRW_ZERO)


static inline int XFS_bwrite(xfs_buf_t *bp)
{
	int	iowait = (bp->b_flags & XBF_ASYNC) == 0;
	int	error = 0;

	if (!iowait)
		bp->b_flags |= _XBF_RUN_QUEUES;

	xfs_buf_delwri_dequeue(bp);
	xfs_buf_iostrategy(bp);
	if (iowait) {
		error = xfs_buf_iowait(bp);
		xfs_buf_relse(bp);
	}
	return error;
}

/*
 * No error can be returned from xfs_buf_iostart for delwri
 * buffers as they are queued and no I/O is issued.
 */
static inline void xfs_bdwrite(void *mp, xfs_buf_t *bp)
{
	bp->b_strat = xfs_bdstrat_cb;
	bp->b_fspriv3 = mp;
	(void)xfs_buf_iostart(bp, XBF_DELWRI | XBF_ASYNC);
}

#define XFS_bdstrat(bp) xfs_buf_iorequest(bp)

#define xfs_iowait(bp)	xfs_buf_iowait(bp)

#define xfs_baread(target, rablkno, ralen)  \
	xfs_buf_readahead((target), (rablkno), (ralen), XBF_DONT_BLOCK)


/*
 *	Handling of buftargs.
 */
extern xfs_buftarg_t *xfs_alloc_buftarg(struct block_device *, int);
extern void xfs_free_buftarg(xfs_buftarg_t *);
extern void xfs_wait_buftarg(xfs_buftarg_t *);
extern int xfs_setsize_buftarg(xfs_buftarg_t *, unsigned int, unsigned int);
extern int xfs_flush_buftarg(xfs_buftarg_t *, int);
#ifdef CONFIG_KDB_MODULES
extern struct list_head *xfs_get_buftarg_list(void);
#endif

#define xfs_getsize_buftarg(buftarg)	block_size((buftarg)->bt_bdev)
#define xfs_readonly_buftarg(buftarg)	bdev_read_only((buftarg)->bt_bdev)

#define xfs_binval(buftarg)		xfs_flush_buftarg(buftarg, 1)
#define XFS_bflush(buftarg)		xfs_flush_buftarg(buftarg, 1)

#endif	/* __XFS_BUF_H__ */
