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

#include <linux/config.h>
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

#define XFS_BUF_DADDR_NULL ((xfs_daddr_t) (-1LL))

#define page_buf_ctob(pp)	((pp) * PAGE_CACHE_SIZE)
#define page_buf_btoc(dd)	(((dd) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT)
#define page_buf_btoct(dd)	((dd) >> PAGE_CACHE_SHIFT)
#define page_buf_poff(aa)	((aa) & ~PAGE_CACHE_MASK)

typedef enum page_buf_rw_e {
	PBRW_READ = 1,			/* transfer into target memory */
	PBRW_WRITE = 2,			/* transfer from target memory */
	PBRW_ZERO = 3			/* Zero target memory */
} page_buf_rw_t;


typedef enum page_buf_flags_e {		/* pb_flags values */
	PBF_READ = (1 << 0),	/* buffer intended for reading from device */
	PBF_WRITE = (1 << 1),	/* buffer intended for writing to device   */
	PBF_MAPPED = (1 << 2),  /* buffer mapped (pb_addr valid)           */
	PBF_ASYNC = (1 << 4),   /* initiator will not wait for completion  */
	PBF_DONE = (1 << 5),    /* all pages in the buffer uptodate	   */
	PBF_DELWRI = (1 << 6),  /* buffer has dirty pages                  */
	PBF_STALE = (1 << 7),	/* buffer has been staled, do not find it  */
	PBF_FS_MANAGED = (1 << 8),  /* filesystem controls freeing memory  */
 	PBF_ORDERED = (1 << 11),    /* use ordered writes		   */
	PBF_READ_AHEAD = (1 << 12), /* asynchronous read-ahead		   */

	/* flags used only as arguments to access routines */
	PBF_LOCK = (1 << 14),       /* lock requested			   */
	PBF_TRYLOCK = (1 << 15),    /* lock requested, but do not wait	   */
	PBF_DONT_BLOCK = (1 << 16), /* do not block in current thread	   */

	/* flags used only internally */
	_PBF_PAGE_CACHE = (1 << 17),/* backed by pagecache		   */
	_PBF_KMEM_ALLOC = (1 << 18),/* backed by kmem_alloc()		   */
	_PBF_RUN_QUEUES = (1 << 19),/* run block device task queue	   */
	_PBF_DELWRI_Q = (1 << 21),   /* buffer on delwri queue		   */
} page_buf_flags_t;


typedef struct xfs_bufhash {
	struct list_head	bh_list;
	spinlock_t		bh_lock;
} xfs_bufhash_t;

typedef struct xfs_buftarg {
	dev_t			pbr_dev;
	struct block_device	*pbr_bdev;
	struct address_space	*pbr_mapping;
	unsigned int		pbr_bsize;
	unsigned int		pbr_sshift;
	size_t			pbr_smask;

	/* per-device buffer hash table */
	uint			bt_hashmask;
	uint			bt_hashshift;
	xfs_bufhash_t		*bt_hash;
} xfs_buftarg_t;

/*
 *	xfs_buf_t:  Buffer structure for page cache-based buffers
 *
 * This buffer structure is used by the page cache buffer management routines
 * to refer to an assembly of pages forming a logical buffer.  The actual I/O
 * is performed with buffer_head structures, as required by drivers.
 * 
 * The buffer structure is used on temporary basis only, and discarded when
 * released.  The real data storage is recorded in the page cache.  Metadata is
 * hashed to the block device on which the file system resides.
 */

struct xfs_buf;

/* call-back function on I/O completion */
typedef void (*page_buf_iodone_t)(struct xfs_buf *);
/* call-back function on I/O completion */
typedef void (*page_buf_relse_t)(struct xfs_buf *);
/* pre-write function */
typedef int (*page_buf_bdstrat_t)(struct xfs_buf *);

#define PB_PAGES	2

typedef struct xfs_buf {
	struct semaphore	pb_sema;	/* semaphore for lockables  */
	unsigned long		pb_queuetime;	/* time buffer was queued   */
	atomic_t		pb_pin_count;	/* pin count		    */
	wait_queue_head_t	pb_waiters;	/* unpin waiters	    */
	struct list_head	pb_list;
	page_buf_flags_t	pb_flags;	/* status flags */
	struct list_head	pb_hash_list;	/* hash table list */
	xfs_bufhash_t		*pb_hash;	/* hash table list start */
	xfs_buftarg_t		*pb_target;	/* buffer target (device) */
	atomic_t		pb_hold;	/* reference count */
	xfs_daddr_t		pb_bn;		/* block number for I/O */
	loff_t			pb_file_offset;	/* offset in file */
	size_t			pb_buffer_length; /* size of buffer in bytes */
	size_t			pb_count_desired; /* desired transfer size */
	void			*pb_addr;	/* virtual address of buffer */
	struct work_struct	pb_iodone_work;
	atomic_t		pb_io_remaining;/* #outstanding I/O requests */
	page_buf_iodone_t	pb_iodone;	/* I/O completion function */
	page_buf_relse_t	pb_relse;	/* releasing function */
	page_buf_bdstrat_t	pb_strat;	/* pre-write function */
	struct semaphore	pb_iodonesema;	/* Semaphore for I/O waiters */
	void			*pb_fspriv;
	void			*pb_fspriv2;
	void			*pb_fspriv3;
	unsigned short		pb_error;	/* error code on I/O */
 	unsigned short		pb_locked;	/* page array is locked */
 	unsigned int		pb_page_count;	/* size of page array */
	unsigned int		pb_offset;	/* page offset in first page */
	struct page		**pb_pages;	/* array of page pointers */
	struct page		*pb_page_array[PB_PAGES]; /* inline pages */
#ifdef PAGEBUF_LOCK_TRACKING
	int			pb_last_holder;
#endif
} xfs_buf_t;


/* Finding and Reading Buffers */

extern xfs_buf_t *_pagebuf_find(	/* find buffer for block if	*/
					/* the block is in memory	*/
		xfs_buftarg_t *,	/* inode for block		*/
		loff_t,			/* starting offset of range	*/
		size_t,			/* length of range		*/
		page_buf_flags_t,	/* PBF_LOCK			*/
	        xfs_buf_t *);		/* newly allocated buffer	*/

#define xfs_incore(buftarg,blkno,len,lockit) \
	_pagebuf_find(buftarg, blkno ,len, lockit, NULL)

extern xfs_buf_t *xfs_buf_get_flags(	/* allocate a buffer		*/
		xfs_buftarg_t *,	/* inode for buffer		*/
		loff_t,			/* starting offset of range     */
		size_t,			/* length of range              */
		page_buf_flags_t);	/* PBF_LOCK, PBF_READ,		*/
					/* PBF_ASYNC			*/

#define xfs_buf_get(target, blkno, len, flags) \
	xfs_buf_get_flags((target), (blkno), (len), PBF_LOCK | PBF_MAPPED)

extern xfs_buf_t *xfs_buf_read_flags(	/* allocate and read a buffer	*/
		xfs_buftarg_t *,	/* inode for buffer		*/
		loff_t,			/* starting offset of range	*/
		size_t,			/* length of range		*/
		page_buf_flags_t);	/* PBF_LOCK, PBF_ASYNC		*/

#define xfs_buf_read(target, blkno, len, flags) \
	xfs_buf_read_flags((target), (blkno), (len), PBF_LOCK | PBF_MAPPED)

extern xfs_buf_t *pagebuf_get_empty(	/* allocate pagebuf struct with	*/
					/*  no memory or disk address	*/
		size_t len,
		xfs_buftarg_t *);	/* mount point "fake" inode	*/

extern xfs_buf_t *pagebuf_get_no_daddr(/* allocate pagebuf struct	*/
					/* without disk address		*/
		size_t len,
		xfs_buftarg_t *);	/* mount point "fake" inode	*/

extern int pagebuf_associate_memory(
		xfs_buf_t *,
		void *,
		size_t);

extern void pagebuf_hold(		/* increment reference count	*/
		xfs_buf_t *);		/* buffer to hold		*/

extern void pagebuf_readahead(		/* read ahead into cache	*/
		xfs_buftarg_t  *,	/* target for buffer (or NULL)	*/
		loff_t,			/* starting offset of range     */
		size_t,			/* length of range              */
		page_buf_flags_t);	/* additional read flags	*/

/* Releasing Buffers */

extern void pagebuf_free(		/* deallocate a buffer		*/
		xfs_buf_t *);		/* buffer to deallocate		*/

extern void pagebuf_rele(		/* release hold on a buffer	*/
		xfs_buf_t *);		/* buffer to release		*/

/* Locking and Unlocking Buffers */

extern int pagebuf_cond_lock(		/* lock buffer, if not locked	*/
					/* (returns -EBUSY if locked)	*/
		xfs_buf_t *);		/* buffer to lock		*/

extern int pagebuf_lock_value(		/* return count on lock		*/
		xfs_buf_t *);          /* buffer to check              */

extern int pagebuf_lock(		/* lock buffer                  */
		xfs_buf_t *);          /* buffer to lock               */

extern void pagebuf_unlock(		/* unlock buffer		*/
		xfs_buf_t *);		/* buffer to unlock		*/

/* Buffer Read and Write Routines */

extern void pagebuf_iodone(		/* mark buffer I/O complete	*/
		xfs_buf_t *,		/* buffer to mark		*/
		int);			/* run completion locally, or in
					 * a helper thread.		*/

extern void pagebuf_ioerror(		/* mark buffer in error	(or not) */
		xfs_buf_t *,		/* buffer to mark		*/
		int);			/* error to store (0 if none)	*/

extern int pagebuf_iostart(		/* start I/O on a buffer	*/
		xfs_buf_t *,		/* buffer to start		*/
		page_buf_flags_t);	/* PBF_LOCK, PBF_ASYNC,		*/
					/* PBF_READ, PBF_WRITE,		*/
					/* PBF_DELWRI			*/

extern int pagebuf_iorequest(		/* start real I/O		*/
		xfs_buf_t *);		/* buffer to convey to device	*/

extern int pagebuf_iowait(		/* wait for buffer I/O done	*/
		xfs_buf_t *);		/* buffer to wait on		*/

extern void pagebuf_iomove(		/* move data in/out of pagebuf	*/
		xfs_buf_t *,		/* buffer to manipulate		*/
		size_t,			/* starting buffer offset	*/
		size_t,			/* length in buffer		*/
		caddr_t,		/* data pointer			*/
		page_buf_rw_t);		/* direction			*/

static inline int pagebuf_iostrategy(xfs_buf_t *pb)
{
	return pb->pb_strat ? pb->pb_strat(pb) : pagebuf_iorequest(pb);
}

static inline int pagebuf_geterror(xfs_buf_t *pb)
{
	return pb ? pb->pb_error : ENOMEM;
}

/* Buffer Utility Routines */

extern caddr_t pagebuf_offset(		/* pointer at offset in buffer	*/
		xfs_buf_t *,		/* buffer to offset into	*/
		size_t);		/* offset			*/

/* Pinning Buffer Storage in Memory */

extern void pagebuf_pin(		/* pin buffer in memory		*/
		xfs_buf_t *);		/* buffer to pin		*/

extern void pagebuf_unpin(		/* unpin buffered data		*/
		xfs_buf_t *);		/* buffer to unpin		*/

extern int pagebuf_ispin(		/* check if buffer is pinned	*/
		xfs_buf_t *);		/* buffer to check		*/

/* Delayed Write Buffer Routines */

extern void pagebuf_delwri_dequeue(xfs_buf_t *);

/* Buffer Daemon Setup Routines */

extern int pagebuf_init(void);
extern void pagebuf_terminate(void);


#ifdef PAGEBUF_TRACE
extern ktrace_t *pagebuf_trace_buf;
extern void pagebuf_trace(
		xfs_buf_t *,		/* buffer being traced		*/
		char *,			/* description of operation	*/
		void *,			/* arbitrary diagnostic value	*/
		void *);		/* return address		*/
#else
# define pagebuf_trace(pb, id, ptr, ra)	do { } while (0)
#endif

#define pagebuf_target_name(target)	\
	({ char __b[BDEVNAME_SIZE]; bdevname((target)->pbr_bdev, __b); __b; })



/* These are just for xfs_syncsub... it sets an internal variable
 * then passes it to VOP_FLUSH_PAGES or adds the flags to a newly gotten buf_t
 */
#define XFS_B_ASYNC		PBF_ASYNC
#define XFS_B_DELWRI		PBF_DELWRI
#define XFS_B_READ		PBF_READ
#define XFS_B_WRITE		PBF_WRITE
#define XFS_B_STALE		PBF_STALE

#define XFS_BUF_TRYLOCK		PBF_TRYLOCK
#define XFS_INCORE_TRYLOCK	PBF_TRYLOCK
#define XFS_BUF_LOCK		PBF_LOCK
#define XFS_BUF_MAPPED		PBF_MAPPED

#define BUF_BUSY		PBF_DONT_BLOCK

#define XFS_BUF_BFLAGS(x)	((x)->pb_flags)
#define XFS_BUF_ZEROFLAGS(x)	\
	((x)->pb_flags &= ~(PBF_READ|PBF_WRITE|PBF_ASYNC|PBF_DELWRI))

#define XFS_BUF_STALE(x)	((x)->pb_flags |= XFS_B_STALE)
#define XFS_BUF_UNSTALE(x)	((x)->pb_flags &= ~XFS_B_STALE)
#define XFS_BUF_ISSTALE(x)	((x)->pb_flags & XFS_B_STALE)
#define XFS_BUF_SUPER_STALE(x)	do {				\
					XFS_BUF_STALE(x);	\
					pagebuf_delwri_dequeue(x);	\
					XFS_BUF_DONE(x);	\
				} while (0)

#define XFS_BUF_MANAGE		PBF_FS_MANAGED
#define XFS_BUF_UNMANAGE(x)	((x)->pb_flags &= ~PBF_FS_MANAGED)

#define XFS_BUF_DELAYWRITE(x)	 ((x)->pb_flags |= PBF_DELWRI)
#define XFS_BUF_UNDELAYWRITE(x)	 pagebuf_delwri_dequeue(x)
#define XFS_BUF_ISDELAYWRITE(x)	 ((x)->pb_flags & PBF_DELWRI)

#define XFS_BUF_ERROR(x,no)	 pagebuf_ioerror(x,no)
#define XFS_BUF_GETERROR(x)	 pagebuf_geterror(x)
#define XFS_BUF_ISERROR(x)	 (pagebuf_geterror(x)?1:0)

#define XFS_BUF_DONE(x)		 ((x)->pb_flags |= PBF_DONE)
#define XFS_BUF_UNDONE(x)	 ((x)->pb_flags &= ~PBF_DONE)
#define XFS_BUF_ISDONE(x)	 ((x)->pb_flags & PBF_DONE)

#define XFS_BUF_BUSY(x)		 do { } while (0)
#define XFS_BUF_UNBUSY(x)	 do { } while (0)
#define XFS_BUF_ISBUSY(x)	 (1)

#define XFS_BUF_ASYNC(x)	 ((x)->pb_flags |= PBF_ASYNC)
#define XFS_BUF_UNASYNC(x)	 ((x)->pb_flags &= ~PBF_ASYNC)
#define XFS_BUF_ISASYNC(x)	 ((x)->pb_flags & PBF_ASYNC)

#define XFS_BUF_ORDERED(x)	 ((x)->pb_flags |= PBF_ORDERED)
#define XFS_BUF_UNORDERED(x)	 ((x)->pb_flags &= ~PBF_ORDERED)
#define XFS_BUF_ISORDERED(x)	 ((x)->pb_flags & PBF_ORDERED)

#define XFS_BUF_SHUT(x)		 printk("XFS_BUF_SHUT not implemented yet\n")
#define XFS_BUF_UNSHUT(x)	 printk("XFS_BUF_UNSHUT not implemented yet\n")
#define XFS_BUF_ISSHUT(x)	 (0)

#define XFS_BUF_HOLD(x)		pagebuf_hold(x)
#define XFS_BUF_READ(x)		((x)->pb_flags |= PBF_READ)
#define XFS_BUF_UNREAD(x)	((x)->pb_flags &= ~PBF_READ)
#define XFS_BUF_ISREAD(x)	((x)->pb_flags & PBF_READ)

#define XFS_BUF_WRITE(x)	((x)->pb_flags |= PBF_WRITE)
#define XFS_BUF_UNWRITE(x)	((x)->pb_flags &= ~PBF_WRITE)
#define XFS_BUF_ISWRITE(x)	((x)->pb_flags & PBF_WRITE)

#define XFS_BUF_ISUNINITIAL(x)	 (0)
#define XFS_BUF_UNUNINITIAL(x)	 (0)

#define XFS_BUF_BP_ISMAPPED(bp)	 1

#define XFS_BUF_IODONE_FUNC(buf)	(buf)->pb_iodone
#define XFS_BUF_SET_IODONE_FUNC(buf, func)	\
			(buf)->pb_iodone = (func)
#define XFS_BUF_CLR_IODONE_FUNC(buf)		\
			(buf)->pb_iodone = NULL
#define XFS_BUF_SET_BDSTRAT_FUNC(buf, func)	\
			(buf)->pb_strat = (func)
#define XFS_BUF_CLR_BDSTRAT_FUNC(buf)		\
			(buf)->pb_strat = NULL

#define XFS_BUF_FSPRIVATE(buf, type)		\
			((type)(buf)->pb_fspriv)
#define XFS_BUF_SET_FSPRIVATE(buf, value)	\
			(buf)->pb_fspriv = (void *)(value)
#define XFS_BUF_FSPRIVATE2(buf, type)		\
			((type)(buf)->pb_fspriv2)
#define XFS_BUF_SET_FSPRIVATE2(buf, value)	\
			(buf)->pb_fspriv2 = (void *)(value)
#define XFS_BUF_FSPRIVATE3(buf, type)		\
			((type)(buf)->pb_fspriv3)
#define XFS_BUF_SET_FSPRIVATE3(buf, value)	\
			(buf)->pb_fspriv3  = (void *)(value)
#define XFS_BUF_SET_START(buf)

#define XFS_BUF_SET_BRELSE_FUNC(buf, value) \
			(buf)->pb_relse = (value)

#define XFS_BUF_PTR(bp)		(xfs_caddr_t)((bp)->pb_addr)

static inline xfs_caddr_t xfs_buf_offset(xfs_buf_t *bp, size_t offset)
{
	if (bp->pb_flags & PBF_MAPPED)
		return XFS_BUF_PTR(bp) + offset;
	return (xfs_caddr_t) pagebuf_offset(bp, offset);
}

#define XFS_BUF_SET_PTR(bp, val, count)		\
				pagebuf_associate_memory(bp, val, count)
#define XFS_BUF_ADDR(bp)	((bp)->pb_bn)
#define XFS_BUF_SET_ADDR(bp, blk)		\
			((bp)->pb_bn = (xfs_daddr_t)(blk))
#define XFS_BUF_OFFSET(bp)	((bp)->pb_file_offset)
#define XFS_BUF_SET_OFFSET(bp, off)		\
			((bp)->pb_file_offset = (off))
#define XFS_BUF_COUNT(bp)	((bp)->pb_count_desired)
#define XFS_BUF_SET_COUNT(bp, cnt)		\
			((bp)->pb_count_desired = (cnt))
#define XFS_BUF_SIZE(bp)	((bp)->pb_buffer_length)
#define XFS_BUF_SET_SIZE(bp, cnt)		\
			((bp)->pb_buffer_length = (cnt))
#define XFS_BUF_SET_VTYPE_REF(bp, type, ref)
#define XFS_BUF_SET_VTYPE(bp, type)
#define XFS_BUF_SET_REF(bp, ref)

#define XFS_BUF_ISPINNED(bp)	pagebuf_ispin(bp)

#define XFS_BUF_VALUSEMA(bp)	pagebuf_lock_value(bp)
#define XFS_BUF_CPSEMA(bp)	(pagebuf_cond_lock(bp) == 0)
#define XFS_BUF_VSEMA(bp)	pagebuf_unlock(bp)
#define XFS_BUF_PSEMA(bp,x)	pagebuf_lock(bp)
#define XFS_BUF_V_IODONESEMA(bp) up(&bp->pb_iodonesema);

/* setup the buffer target from a buftarg structure */
#define XFS_BUF_SET_TARGET(bp, target)	\
		(bp)->pb_target = (target)
#define XFS_BUF_TARGET(bp)	((bp)->pb_target)
#define XFS_BUFTARG_NAME(target)	\
		pagebuf_target_name(target)

#define XFS_BUF_SET_VTYPE_REF(bp, type, ref)
#define XFS_BUF_SET_VTYPE(bp, type)
#define XFS_BUF_SET_REF(bp, ref)

static inline int	xfs_bawrite(void *mp, xfs_buf_t *bp)
{
	bp->pb_fspriv3 = mp;
	bp->pb_strat = xfs_bdstrat_cb;
	pagebuf_delwri_dequeue(bp);
	return pagebuf_iostart(bp, PBF_WRITE | PBF_ASYNC | _PBF_RUN_QUEUES);
}

static inline void	xfs_buf_relse(xfs_buf_t *bp)
{
	if (!bp->pb_relse)
		pagebuf_unlock(bp);
	pagebuf_rele(bp);
}

#define xfs_bpin(bp)		pagebuf_pin(bp)
#define xfs_bunpin(bp)		pagebuf_unpin(bp)

#define xfs_buftrace(id, bp)	\
	    pagebuf_trace(bp, id, NULL, (void *)__builtin_return_address(0))

#define xfs_biodone(pb)		    \
	    pagebuf_iodone(pb, 0)

#define xfs_biomove(pb, off, len, data, rw) \
	    pagebuf_iomove((pb), (off), (len), (data), \
		((rw) == XFS_B_WRITE) ? PBRW_WRITE : PBRW_READ)

#define xfs_biozero(pb, off, len) \
	    pagebuf_iomove((pb), (off), (len), NULL, PBRW_ZERO)


static inline int	XFS_bwrite(xfs_buf_t *pb)
{
	int	iowait = (pb->pb_flags & PBF_ASYNC) == 0;
	int	error = 0;

	if (!iowait)
		pb->pb_flags |= _PBF_RUN_QUEUES;

	pagebuf_delwri_dequeue(pb);
	pagebuf_iostrategy(pb);
	if (iowait) {
		error = pagebuf_iowait(pb);
		xfs_buf_relse(pb);
	}
	return error;
}

#define XFS_bdwrite(pb)		     \
	    pagebuf_iostart(pb, PBF_DELWRI | PBF_ASYNC)

static inline int xfs_bdwrite(void *mp, xfs_buf_t *bp)
{
	bp->pb_strat = xfs_bdstrat_cb;
	bp->pb_fspriv3 = mp;

	return pagebuf_iostart(bp, PBF_DELWRI | PBF_ASYNC);
}

#define XFS_bdstrat(bp) pagebuf_iorequest(bp)

#define xfs_iowait(pb)	pagebuf_iowait(pb)

#define xfs_baread(target, rablkno, ralen)  \
	pagebuf_readahead((target), (rablkno), (ralen), PBF_DONT_BLOCK)

#define xfs_buf_get_empty(len, target)	pagebuf_get_empty((len), (target))
#define xfs_buf_get_noaddr(len, target)	pagebuf_get_no_daddr((len), (target))
#define xfs_buf_free(bp)		pagebuf_free(bp)


/*
 *	Handling of buftargs.
 */

extern xfs_buftarg_t *xfs_alloc_buftarg(struct block_device *, int);
extern void xfs_free_buftarg(xfs_buftarg_t *, int);
extern void xfs_wait_buftarg(xfs_buftarg_t *);
extern int xfs_setsize_buftarg(xfs_buftarg_t *, unsigned int, unsigned int);
extern int xfs_flush_buftarg(xfs_buftarg_t *, int);

#define xfs_getsize_buftarg(buftarg) \
	block_size((buftarg)->pbr_bdev)
#define xfs_readonly_buftarg(buftarg) \
	bdev_read_only((buftarg)->pbr_bdev)
#define xfs_binval(buftarg) \
	xfs_flush_buftarg(buftarg, 1)
#define XFS_bflush(buftarg) \
	xfs_flush_buftarg(buftarg, 1)

#endif	/* __XFS_BUF_H__ */
