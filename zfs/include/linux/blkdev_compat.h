/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 * LLNL-CODE-403049.
 */

#ifndef _ZFS_BLKDEV_H
#define	_ZFS_BLKDEV_H

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/backing-dev.h>

#ifndef HAVE_FMODE_T
typedef unsigned __bitwise__ fmode_t;
#endif /* HAVE_FMODE_T */

/*
 * 4.7 - 4.x API,
 * The blk_queue_write_cache() interface has replaced blk_queue_flush()
 * interface.  However, the new interface is GPL-only thus we implement
 * our own trivial wrapper when the GPL-only version is detected.
 *
 * 2.6.36 - 4.6 API,
 * The blk_queue_flush() interface has replaced blk_queue_ordered()
 * interface.  However, while the old interface was available to all the
 * new one is GPL-only.   Thus if the GPL-only version is detected we
 * implement our own trivial helper.
 *
 * 2.6.x - 2.6.35
 * Legacy blk_queue_ordered() interface.
 */
static inline void
blk_queue_set_write_cache(struct request_queue *q, bool wc, bool fua)
{
#if defined(HAVE_BLK_QUEUE_WRITE_CACHE_GPL_ONLY)
	spin_lock_irq(q->queue_lock);
	if (wc)
		queue_flag_set(QUEUE_FLAG_WC, q);
	else
		queue_flag_clear(QUEUE_FLAG_WC, q);
	if (fua)
		queue_flag_set(QUEUE_FLAG_FUA, q);
	else
		queue_flag_clear(QUEUE_FLAG_FUA, q);
	spin_unlock_irq(q->queue_lock);
#elif defined(HAVE_BLK_QUEUE_WRITE_CACHE)
	blk_queue_write_cache(q, wc, fua);
#elif defined(HAVE_BLK_QUEUE_FLUSH_GPL_ONLY)
	if (wc)
		q->flush_flags |= REQ_FLUSH;
	if (fua)
		q->flush_flags |= REQ_FUA;
#elif defined(HAVE_BLK_QUEUE_FLUSH)
	blk_queue_flush(q, (wc ? REQ_FLUSH : 0) | (fua ? REQ_FUA : 0));
#else
	blk_queue_ordered(q, QUEUE_ORDERED_DRAIN, NULL);
#endif
}

/*
 * Most of the blk_* macros were removed in 2.6.36.  Ostensibly this was
 * done to improve readability and allow easier grepping.  However, from
 * a portability stand point the macros are helpful.  Therefore the needed
 * macros are redefined here if they are missing from the kernel.
 */
#ifndef blk_fs_request
#define	blk_fs_request(rq)	((rq)->cmd_type == REQ_TYPE_FS)
#endif

/*
 * 2.6.27 API change,
 * The blk_queue_stackable() queue flag was added in 2.6.27 to handle dm
 * stacking drivers.  Prior to this request stacking drivers were detected
 * by checking (q->request_fn == NULL), for earlier kernels we revert to
 * this legacy behavior.
 */
#ifndef blk_queue_stackable
#define	blk_queue_stackable(q)	((q)->request_fn == NULL)
#endif

/*
 * 2.6.34 API change,
 * The blk_queue_max_hw_sectors() function replaces blk_queue_max_sectors().
 */
#ifndef HAVE_BLK_QUEUE_MAX_HW_SECTORS
#define	blk_queue_max_hw_sectors __blk_queue_max_hw_sectors
static inline void
__blk_queue_max_hw_sectors(struct request_queue *q, unsigned int max_hw_sectors)
{
	blk_queue_max_sectors(q, max_hw_sectors);
}
#endif

/*
 * 2.6.34 API change,
 * The blk_queue_max_segments() function consolidates
 * blk_queue_max_hw_segments() and blk_queue_max_phys_segments().
 */
#ifndef HAVE_BLK_QUEUE_MAX_SEGMENTS
#define	blk_queue_max_segments __blk_queue_max_segments
static inline void
__blk_queue_max_segments(struct request_queue *q, unsigned short max_segments)
{
	blk_queue_max_phys_segments(q, max_segments);
	blk_queue_max_hw_segments(q, max_segments);
}
#endif

static inline void
blk_queue_set_read_ahead(struct request_queue *q, unsigned long ra_pages)
{
#ifdef HAVE_BLK_QUEUE_BDI_DYNAMIC
	q->backing_dev_info->ra_pages = ra_pages;
#else
	q->backing_dev_info.ra_pages = ra_pages;
#endif
}

#ifndef HAVE_GET_DISK_RO
static inline int
get_disk_ro(struct gendisk *disk)
{
	int policy = 0;

	if (disk->part[0])
		policy = disk->part[0]->policy;

	return (policy);
}
#endif /* HAVE_GET_DISK_RO */

#ifdef HAVE_BIO_BVEC_ITER
#define	BIO_BI_SECTOR(bio)	(bio)->bi_iter.bi_sector
#define	BIO_BI_SIZE(bio)	(bio)->bi_iter.bi_size
#define	BIO_BI_IDX(bio)		(bio)->bi_iter.bi_idx
#define	BIO_BI_SKIP(bio)	(bio)->bi_iter.bi_bvec_done
#define	bio_for_each_segment4(bv, bvp, b, i)	\
	bio_for_each_segment((bv), (b), (i))
typedef struct bvec_iter bvec_iterator_t;
#else
#define	BIO_BI_SECTOR(bio)	(bio)->bi_sector
#define	BIO_BI_SIZE(bio)	(bio)->bi_size
#define	BIO_BI_IDX(bio)		(bio)->bi_idx
#define	BIO_BI_SKIP(bio)	(0)
#define	bio_for_each_segment4(bv, bvp, b, i)	\
	bio_for_each_segment((bvp), (b), (i))
typedef int bvec_iterator_t;
#endif

/*
 * Portable helper for correctly setting the FAILFAST flags.  The
 * correct usage has changed 3 times from 2.6.12 to 2.6.38.
 */
static inline void
bio_set_flags_failfast(struct block_device *bdev, int *flags)
{
#ifdef CONFIG_BUG
	/*
	 * Disable FAILFAST for loopback devices because of the
	 * following incorrect BUG_ON() in loop_make_request().
	 * This support is also disabled for md devices because the
	 * test suite layers md devices on top of loopback devices.
	 * This may be removed when the loopback driver is fixed.
	 *
	 *   BUG_ON(!lo || (rw != READ && rw != WRITE));
	 */
	if ((MAJOR(bdev->bd_dev) == LOOP_MAJOR) ||
	    (MAJOR(bdev->bd_dev) == MD_MAJOR))
		return;

#ifdef BLOCK_EXT_MAJOR
	if (MAJOR(bdev->bd_dev) == BLOCK_EXT_MAJOR)
		return;
#endif /* BLOCK_EXT_MAJOR */
#endif /* CONFIG_BUG */

#if defined(HAVE_BIO_RW_FAILFAST_DTD)
	/* BIO_RW_FAILFAST_* preferred interface from 2.6.28 - 2.6.35 */
	*flags |= (
	    (1 << BIO_RW_FAILFAST_DEV) |
	    (1 << BIO_RW_FAILFAST_TRANSPORT) |
	    (1 << BIO_RW_FAILFAST_DRIVER));
#elif defined(HAVE_REQ_FAILFAST_MASK)
	/*
	 * REQ_FAILFAST_* preferred interface from 2.6.36 - 2.6.xx,
	 * the BIO_* and REQ_* flags were unified under REQ_* flags.
	 */
	*flags |= REQ_FAILFAST_MASK;
#else
#error "Undefined block IO FAILFAST interface."
#endif
}

/*
 * Maximum disk label length, it may be undefined for some kernels.
 */
#ifndef DISK_NAME_LEN
#define	DISK_NAME_LEN	32
#endif /* DISK_NAME_LEN */

#ifdef HAVE_BIO_BI_STATUS
static inline int
bi_status_to_errno(blk_status_t status)
{
	switch (status)	{
	case BLK_STS_OK:
		return (0);
	case BLK_STS_NOTSUPP:
		return (EOPNOTSUPP);
	case BLK_STS_TIMEOUT:
		return (ETIMEDOUT);
	case BLK_STS_NOSPC:
		return (ENOSPC);
	case BLK_STS_TRANSPORT:
		return (ENOLINK);
	case BLK_STS_TARGET:
		return (EREMOTEIO);
	case BLK_STS_NEXUS:
		return (EBADE);
	case BLK_STS_MEDIUM:
		return (ENODATA);
	case BLK_STS_PROTECTION:
		return (EILSEQ);
	case BLK_STS_RESOURCE:
		return (ENOMEM);
	case BLK_STS_AGAIN:
		return (EAGAIN);
	case BLK_STS_IOERR:
		return (EIO);
	default:
		return (EIO);
	}
}

static inline blk_status_t
errno_to_bi_status(int error)
{
	switch (error) {
	case 0:
		return (BLK_STS_OK);
	case EOPNOTSUPP:
		return (BLK_STS_NOTSUPP);
	case ETIMEDOUT:
		return (BLK_STS_TIMEOUT);
	case ENOSPC:
		return (BLK_STS_NOSPC);
	case ENOLINK:
		return (BLK_STS_TRANSPORT);
	case EREMOTEIO:
		return (BLK_STS_TARGET);
	case EBADE:
		return (BLK_STS_NEXUS);
	case ENODATA:
		return (BLK_STS_MEDIUM);
	case EILSEQ:
		return (BLK_STS_PROTECTION);
	case ENOMEM:
		return (BLK_STS_RESOURCE);
	case EAGAIN:
		return (BLK_STS_AGAIN);
	case EIO:
		return (BLK_STS_IOERR);
	default:
		return (BLK_STS_IOERR);
	}
}
#endif /* HAVE_BIO_BI_STATUS */

/*
 * 4.3 API change
 * The bio_endio() prototype changed slightly.  These are helper
 * macro's to ensure the prototype and invocation are handled.
 */
#ifdef HAVE_1ARG_BIO_END_IO_T
#ifdef HAVE_BIO_BI_STATUS
#define	BIO_END_IO_ERROR(bio)		bi_status_to_errno(bio->bi_status)
#define	BIO_END_IO_PROTO(fn, x, z)	static void fn(struct bio *x)
#define	BIO_END_IO(bio, error)		bio_set_bi_status(bio, error)
static inline void
bio_set_bi_status(struct bio *bio, int error)
{
	ASSERT3S(error, <=, 0);
	bio->bi_status = errno_to_bi_status(-error);
	bio_endio(bio);
}
#else
#define	BIO_END_IO_ERROR(bio)		(-(bio->bi_error))
#define	BIO_END_IO_PROTO(fn, x, z)	static void fn(struct bio *x)
#define	BIO_END_IO(bio, error)		bio_set_bi_error(bio, error)
static inline void
bio_set_bi_error(struct bio *bio, int error)
{
	ASSERT3S(error, <=, 0);
	bio->bi_error = error;
	bio_endio(bio);
}
#endif /* HAVE_BIO_BI_STATUS */

#else
#define	BIO_END_IO_PROTO(fn, x, z)	static void fn(struct bio *x, int z)
#define	BIO_END_IO(bio, error)		bio_endio(bio, error);
#endif /* HAVE_1ARG_BIO_END_IO_T */

/*
 * 2.6.38 - 2.6.x API,
 *   blkdev_get_by_path()
 *   blkdev_put()
 *
 * 2.6.28 - 2.6.37 API,
 *   open_bdev_exclusive()
 *   close_bdev_exclusive()
 *
 * 2.6.12 - 2.6.27 API,
 *   open_bdev_excl()
 *   close_bdev_excl()
 *
 * Used to exclusively open a block device from within the kernel.
 */
#if defined(HAVE_BLKDEV_GET_BY_PATH)
#define	vdev_bdev_open(path, md, hld)	blkdev_get_by_path(path, \
					    (md) | FMODE_EXCL, hld)
#define	vdev_bdev_close(bdev, md)	blkdev_put(bdev, (md) | FMODE_EXCL)
#elif defined(HAVE_OPEN_BDEV_EXCLUSIVE)
#define	vdev_bdev_open(path, md, hld)	open_bdev_exclusive(path, md, hld)
#define	vdev_bdev_close(bdev, md)	close_bdev_exclusive(bdev, md)
#else
#define	vdev_bdev_open(path, md, hld)	open_bdev_excl(path, md, hld)
#define	vdev_bdev_close(bdev, md)	close_bdev_excl(bdev)
#endif /* HAVE_BLKDEV_GET_BY_PATH | HAVE_OPEN_BDEV_EXCLUSIVE */

/*
 * 2.6.22 API change
 * The function invalidate_bdev() lost it's second argument because
 * it was unused.
 */
#ifdef HAVE_1ARG_INVALIDATE_BDEV
#define	vdev_bdev_invalidate(bdev)	invalidate_bdev(bdev)
#else
#define	vdev_bdev_invalidate(bdev)	invalidate_bdev(bdev, 1)
#endif /* HAVE_1ARG_INVALIDATE_BDEV */

/*
 * 2.6.27 API change
 * The function was exported for use, prior to this it existed but the
 * symbol was not exported.
 *
 * 4.4.0-6.21 API change for Ubuntu
 * lookup_bdev() gained a second argument, FMODE_*, to check inode permissions.
 */
#ifdef HAVE_1ARG_LOOKUP_BDEV
#define	vdev_lookup_bdev(path)	lookup_bdev(path)
#else
#ifdef HAVE_2ARGS_LOOKUP_BDEV
#define	vdev_lookup_bdev(path)	lookup_bdev(path, 0)
#else
#define	vdev_lookup_bdev(path)	ERR_PTR(-ENOTSUP)
#endif /* HAVE_2ARGS_LOOKUP_BDEV */
#endif /* HAVE_1ARG_LOOKUP_BDEV */

/*
 * 2.6.30 API change
 * To ensure good performance preferentially use the physical block size
 * for proper alignment.  The physical size is supposed to be the internal
 * sector size used by the device.  This is often 4096 byte for AF devices,
 * while a smaller 512 byte logical size is supported for compatibility.
 *
 * Unfortunately, many drives still misreport their physical sector size.
 * For devices which are known to lie you may need to manually set this
 * at pool creation time with 'zpool create -o ashift=12 ...'.
 *
 * When the physical block size interface isn't available, we fall back to
 * the logical block size interface and then the older hard sector size.
 */
#ifdef HAVE_BDEV_PHYSICAL_BLOCK_SIZE
#define	vdev_bdev_block_size(bdev)	bdev_physical_block_size(bdev)
#else
#ifdef HAVE_BDEV_LOGICAL_BLOCK_SIZE
#define	vdev_bdev_block_size(bdev)	bdev_logical_block_size(bdev)
#else
#define	vdev_bdev_block_size(bdev)	bdev_hardsect_size(bdev)
#endif /* HAVE_BDEV_LOGICAL_BLOCK_SIZE */
#endif /* HAVE_BDEV_PHYSICAL_BLOCK_SIZE */

#ifndef HAVE_BIO_SET_OP_ATTRS
/*
 * Kernels without bio_set_op_attrs use bi_rw for the bio flags.
 */
static inline void
bio_set_op_attrs(struct bio *bio, unsigned rw, unsigned flags)
{
	bio->bi_rw |= rw | flags;
}
#endif

/*
 * bio_set_flush - Set the appropriate flags in a bio to guarantee
 * data are on non-volatile media on completion.
 *
 * 2.6.X - 2.6.36 API,
 *   WRITE_BARRIER - Tells the block layer to commit all previously submitted
 *   writes to stable storage before this one is started and that the current
 *   write is on stable storage upon completion.  Also prevents reordering
 *   on both sides of the current operation.
 *
 * 2.6.37 - 4.8 API,
 *   Introduce  WRITE_FLUSH, WRITE_FUA, and WRITE_FLUSH_FUA flags as a
 *   replacement for WRITE_BARRIER to allow expressing richer semantics
 *   to the block layer.  It's up to the block layer to implement the
 *   semantics correctly. Use the WRITE_FLUSH_FUA flag combination.
 *
 * 4.8 - 4.9 API,
 *   REQ_FLUSH was renamed to REQ_PREFLUSH.  For consistency with previous
 *   ZoL releases, prefer the WRITE_FLUSH_FUA flag set if it's available.
 *
 * 4.10 API,
 *   The read/write flags and their modifiers, including WRITE_FLUSH,
 *   WRITE_FUA and WRITE_FLUSH_FUA were removed from fs.h in
 *   torvalds/linux@70fd7614 and replaced by direct flag modification
 *   of the REQ_ flags in bio->bi_opf.  Use REQ_PREFLUSH.
 */
static inline void
bio_set_flush(struct bio *bio)
{
#if defined(REQ_PREFLUSH)	/* >= 4.10 */
	bio_set_op_attrs(bio, 0, REQ_PREFLUSH);
#elif defined(WRITE_FLUSH_FUA)	/* >= 2.6.37 and <= 4.9 */
	bio_set_op_attrs(bio, 0, WRITE_FLUSH_FUA);
#elif defined(WRITE_BARRIER)	/* < 2.6.37 */
	bio_set_op_attrs(bio, 0, WRITE_BARRIER);
#else
#error	"Allowing the build will cause bio_set_flush requests to be ignored."
#endif
}

/*
 * 4.8 - 4.x API,
 *   REQ_OP_FLUSH
 *
 * 4.8-rc0 - 4.8-rc1,
 *   REQ_PREFLUSH
 *
 * 2.6.36 - 4.7 API,
 *   REQ_FLUSH
 *
 * 2.6.x - 2.6.35 API,
 *   HAVE_BIO_RW_BARRIER
 *
 * Used to determine if a cache flush has been requested.  This check has
 * been left intentionally broad in order to cover both a legacy flush
 * and the new preflush behavior introduced in Linux 4.8.  This is correct
 * in all cases but may have a performance impact for some kernels.  It
 * has the advantage of minimizing kernel specific changes in the zvol code.
 *
 */
static inline boolean_t
bio_is_flush(struct bio *bio)
{
#if defined(HAVE_REQ_OP_FLUSH) && defined(HAVE_BIO_BI_OPF)
	return ((bio_op(bio) == REQ_OP_FLUSH) || (bio->bi_opf & REQ_PREFLUSH));
#elif defined(REQ_PREFLUSH) && defined(HAVE_BIO_BI_OPF)
	return (bio->bi_opf & REQ_PREFLUSH);
#elif defined(REQ_PREFLUSH) && !defined(HAVE_BIO_BI_OPF)
	return (bio->bi_rw & REQ_PREFLUSH);
#elif defined(REQ_FLUSH)
	return (bio->bi_rw & REQ_FLUSH);
#elif defined(HAVE_BIO_RW_BARRIER)
	return (bio->bi_rw & (1 << BIO_RW_BARRIER));
#else
#error	"Allowing the build will cause flush requests to be ignored."
#endif
}

/*
 * 4.8 - 4.x API,
 *   REQ_FUA flag moved to bio->bi_opf
 *
 * 2.6.x - 4.7 API,
 *   REQ_FUA
 */
static inline boolean_t
bio_is_fua(struct bio *bio)
{
#if defined(HAVE_BIO_BI_OPF)
	return (bio->bi_opf & REQ_FUA);
#elif defined(REQ_FUA)
	return (bio->bi_rw & REQ_FUA);
#else
#error	"Allowing the build will cause fua requests to be ignored."
#endif
}

/*
 * 4.8 - 4.x API,
 *   REQ_OP_DISCARD
 *
 * 2.6.36 - 4.7 API,
 *   REQ_DISCARD
 *
 * 2.6.28 - 2.6.35 API,
 *   BIO_RW_DISCARD
 *
 * In all cases the normal I/O path is used for discards.  The only
 * difference is how the kernel tags individual I/Os as discards.
 *
 * Note that 2.6.32 era kernels provide both BIO_RW_DISCARD and REQ_DISCARD,
 * where BIO_RW_DISCARD is the correct interface.  Therefore, it is important
 * that the HAVE_BIO_RW_DISCARD check occur before the REQ_DISCARD check.
 */
static inline boolean_t
bio_is_discard(struct bio *bio)
{
#if defined(HAVE_REQ_OP_DISCARD)
	return (bio_op(bio) == REQ_OP_DISCARD);
#elif defined(HAVE_BIO_RW_DISCARD)
	return (bio->bi_rw & (1 << BIO_RW_DISCARD));
#elif defined(REQ_DISCARD)
	return (bio->bi_rw & REQ_DISCARD);
#else
/* potentially triggering the DMU_MAX_ACCESS assertion.  */
#error	"Allowing the build will cause discard requests to become writes."
#endif
}

/*
 * 4.8 - 4.x API,
 *   REQ_OP_SECURE_ERASE
 *
 * 2.6.36 - 4.7 API,
 *   REQ_SECURE
 *
 * 2.6.x - 2.6.35 API,
 *   Unsupported by kernel
 */
static inline boolean_t
bio_is_secure_erase(struct bio *bio)
{
#if defined(HAVE_REQ_OP_SECURE_ERASE)
	return (bio_op(bio) == REQ_OP_SECURE_ERASE);
#elif defined(REQ_SECURE)
	return (bio->bi_rw & REQ_SECURE);
#else
	return (0);
#endif
}

/*
 * 2.6.33 API change
 * Discard granularity and alignment restrictions may now be set.  For
 * older kernels which do not support this it is safe to skip it.
 */
#ifdef HAVE_DISCARD_GRANULARITY
static inline void
blk_queue_discard_granularity(struct request_queue *q, unsigned int dg)
{
	q->limits.discard_granularity = dg;
}
#else
#define	blk_queue_discard_granularity(x, dg)	((void)0)
#endif /* HAVE_DISCARD_GRANULARITY */

/*
 * Default Linux IO Scheduler,
 * Setting the scheduler to noop will allow the Linux IO scheduler to
 * still perform front and back merging, while leaving the request
 * ordering and prioritization to the ZFS IO scheduler.
 */
#define	VDEV_SCHEDULER			"noop"

/*
 * A common holder for vdev_bdev_open() is used to relax the exclusive open
 * semantics slightly.  Internal vdev disk callers may pass VDEV_HOLDER to
 * allow them to open the device multiple times.  Other kernel callers and
 * user space processes which don't pass this value will get EBUSY.  This is
 * currently required for the correct operation of hot spares.
 */
#define	VDEV_HOLDER			((void *)0x2401de7)

static inline void
blk_generic_start_io_acct(struct request_queue *q, int rw,
    unsigned long sectors, struct hd_struct *part)
{
#if defined(HAVE_GENERIC_IO_ACCT_3ARG)
	generic_start_io_acct(rw, sectors, part);
#elif defined(HAVE_GENERIC_IO_ACCT_4ARG)
	generic_start_io_acct(q, rw, sectors, part);
#endif
}

static inline void
blk_generic_end_io_acct(struct request_queue *q, int rw,
    struct hd_struct *part, unsigned long start_time)
{
#if defined(HAVE_GENERIC_IO_ACCT_3ARG)
	generic_end_io_acct(rw, part, start_time);
#elif defined(HAVE_GENERIC_IO_ACCT_4ARG)
	generic_end_io_acct(q, rw, part, start_time);
#endif
}

#endif /* _ZFS_BLKDEV_H */
