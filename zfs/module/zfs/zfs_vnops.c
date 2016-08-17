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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 */

/* Portions Copyright 2007 Jeremy Teo */
/* Portions Copyright 2010 Robert Milkowski */


#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/resource.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/taskq.h>
#include <sys/uio.h>
#include <sys/vmsystm.h>
#include <sys/atomic.h>
#include <vm/pvn.h>
#include <sys/pathname.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/dirent.h>
#include <sys/policy.h>
#include <sys/sunddi.h>
#include <sys/sid.h>
#include <sys/mode.h>
#include "fs/fs_subr.h"
#include <sys/zfs_ctldir.h>
#include <sys/zfs_fuid.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_vnops.h>
#include <sys/dnlc.h>
#include <sys/zfs_rlock.h>
#include <sys/extdirent.h>
#include <sys/kidmap.h>
#include <sys/cred.h>
#include <sys/attr.h>
#include <sys/zpl.h>

/*
 * Programming rules.
 *
 * Each vnode op performs some logical unit of work.  To do this, the ZPL must
 * properly lock its in-core state, create a DMU transaction, do the work,
 * record this work in the intent log (ZIL), commit the DMU transaction,
 * and wait for the intent log to commit if it is a synchronous operation.
 * Moreover, the vnode ops must work in both normal and log replay context.
 * The ordering of events is important to avoid deadlocks and references
 * to freed memory.  The example below illustrates the following Big Rules:
 *
 *  (1) A check must be made in each zfs thread for a mounted file system.
 *	This is done avoiding races using ZFS_ENTER(zsb).
 *      A ZFS_EXIT(zsb) is needed before all returns.  Any znodes
 *      must be checked with ZFS_VERIFY_ZP(zp).  Both of these macros
 *      can return EIO from the calling function.
 *
 *  (2)	iput() should always be the last thing except for zil_commit()
 *	(if necessary) and ZFS_EXIT(). This is for 3 reasons:
 *	First, if it's the last reference, the vnode/znode
 *	can be freed, so the zp may point to freed memory.  Second, the last
 *	reference will call zfs_zinactive(), which may induce a lot of work --
 *	pushing cached pages (which acquires range locks) and syncing out
 *	cached atime changes.  Third, zfs_zinactive() may require a new tx,
 *	which could deadlock the system if you were already holding one.
 *	If you must call iput() within a tx then use zfs_iput_async().
 *
 *  (3)	All range locks must be grabbed before calling dmu_tx_assign(),
 *	as they can span dmu_tx_assign() calls.
 *
 *  (4) If ZPL locks are held, pass TXG_NOWAIT as the second argument to
 *      dmu_tx_assign().  This is critical because we don't want to block
 *      while holding locks.
 *
 *	If no ZPL locks are held (aside from ZFS_ENTER()), use TXG_WAIT.  This
 *	reduces lock contention and CPU usage when we must wait (note that if
 *	throughput is constrained by the storage, nearly every transaction
 *	must wait).
 *
 *      Note, in particular, that if a lock is sometimes acquired before
 *      the tx assigns, and sometimes after (e.g. z_lock), then failing
 *      to use a non-blocking assign can deadlock the system.  The scenario:
 *
 *	Thread A has grabbed a lock before calling dmu_tx_assign().
 *	Thread B is in an already-assigned tx, and blocks for this lock.
 *	Thread A calls dmu_tx_assign(TXG_WAIT) and blocks in txg_wait_open()
 *	forever, because the previous txg can't quiesce until B's tx commits.
 *
 *	If dmu_tx_assign() returns ERESTART and zsb->z_assign is TXG_NOWAIT,
 *	then drop all locks, call dmu_tx_wait(), and try again.  On subsequent
 *	calls to dmu_tx_assign(), pass TXG_WAITED rather than TXG_NOWAIT,
 *	to indicate that this operation has already called dmu_tx_wait().
 *	This will ensure that we don't retry forever, waiting a short bit
 *	each time.
 *
 *  (5)	If the operation succeeded, generate the intent log entry for it
 *	before dropping locks.  This ensures that the ordering of events
 *	in the intent log matches the order in which they actually occurred.
 *	During ZIL replay the zfs_log_* functions will update the sequence
 *	number to indicate the zil transaction has replayed.
 *
 *  (6)	At the end of each vnode op, the DMU tx must always commit,
 *	regardless of whether there were any errors.
 *
 *  (7)	After dropping all locks, invoke zil_commit(zilog, foid)
 *	to ensure that synchronous semantics are provided when necessary.
 *
 * In general, this is how things should be ordered in each vnode op:
 *
 *	ZFS_ENTER(zsb);		// exit if unmounted
 * top:
 *	zfs_dirent_lock(&dl, ...)	// lock directory entry (may igrab())
 *	rw_enter(...);			// grab any other locks you need
 *	tx = dmu_tx_create(...);	// get DMU tx
 *	dmu_tx_hold_*();		// hold each object you might modify
 *	error = dmu_tx_assign(tx, waited ? TXG_WAITED : TXG_NOWAIT);
 *	if (error) {
 *		rw_exit(...);		// drop locks
 *		zfs_dirent_unlock(dl);	// unlock directory entry
 *		iput(...);		// release held vnodes
 *		if (error == ERESTART) {
 *			waited = B_TRUE;
 *			dmu_tx_wait(tx);
 *			dmu_tx_abort(tx);
 *			goto top;
 *		}
 *		dmu_tx_abort(tx);	// abort DMU tx
 *		ZFS_EXIT(zsb);	// finished in zfs
 *		return (error);		// really out of space
 *	}
 *	error = do_real_work();		// do whatever this VOP does
 *	if (error == 0)
 *		zfs_log_*(...);		// on success, make ZIL entry
 *	dmu_tx_commit(tx);		// commit DMU tx -- error or not
 *	rw_exit(...);			// drop locks
 *	zfs_dirent_unlock(dl);		// unlock directory entry
 *	iput(...);			// release held vnodes
 *	zil_commit(zilog, foid);	// synchronous when necessary
 *	ZFS_EXIT(zsb);		// finished in zfs
 *	return (error);			// done, report error
 */

/*
 * Virus scanning is unsupported.  It would be possible to add a hook
 * here to performance the required virus scan.  This could be done
 * entirely in the kernel or potentially as an update to invoke a
 * scanning utility.
 */
static int
zfs_vscan(struct inode *ip, cred_t *cr, int async)
{
	return (0);
}

/* ARGSUSED */
int
zfs_open(struct inode *ip, int mode, int flag, cred_t *cr)
{
	znode_t	*zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	/* Honor ZFS_APPENDONLY file attribute */
	if ((mode & FMODE_WRITE) && (zp->z_pflags & ZFS_APPENDONLY) &&
	    ((flag & O_APPEND) == 0)) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EPERM));
	}

	/* Virus scan eligible files on open */
	if (!zfs_has_ctldir(zp) && zsb->z_vscan && S_ISREG(ip->i_mode) &&
	    !(zp->z_pflags & ZFS_AV_QUARANTINED) && zp->z_size > 0) {
		if (zfs_vscan(ip, cr, 0) != 0) {
			ZFS_EXIT(zsb);
			return (SET_ERROR(EACCES));
		}
	}

	/* Keep a count of the synchronous opens in the znode */
	if (flag & O_SYNC)
		atomic_inc_32(&zp->z_sync_cnt);

	ZFS_EXIT(zsb);
	return (0);
}
EXPORT_SYMBOL(zfs_open);

/* ARGSUSED */
int
zfs_close(struct inode *ip, int flag, cred_t *cr)
{
	znode_t	*zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	/* Decrement the synchronous opens in the znode */
	if (flag & O_SYNC)
		atomic_dec_32(&zp->z_sync_cnt);

	if (!zfs_has_ctldir(zp) && zsb->z_vscan && S_ISREG(ip->i_mode) &&
	    !(zp->z_pflags & ZFS_AV_QUARANTINED) && zp->z_size > 0)
		VERIFY(zfs_vscan(ip, cr, 1) == 0);

	ZFS_EXIT(zsb);
	return (0);
}
EXPORT_SYMBOL(zfs_close);

#if defined(SEEK_HOLE) && defined(SEEK_DATA)
/*
 * Lseek support for finding holes (cmd == SEEK_HOLE) and
 * data (cmd == SEEK_DATA). "off" is an in/out parameter.
 */
static int
zfs_holey_common(struct inode *ip, int cmd, loff_t *off)
{
	znode_t	*zp = ITOZ(ip);
	uint64_t noff = (uint64_t)*off; /* new offset */
	uint64_t file_sz;
	int error;
	boolean_t hole;

	file_sz = zp->z_size;
	if (noff >= file_sz)  {
		return (SET_ERROR(ENXIO));
	}

	if (cmd == SEEK_HOLE)
		hole = B_TRUE;
	else
		hole = B_FALSE;

	error = dmu_offset_next(ZTOZSB(zp)->z_os, zp->z_id, hole, &noff);

	if (error == ESRCH)
		return (SET_ERROR(ENXIO));

	/*
	 * We could find a hole that begins after the logical end-of-file,
	 * because dmu_offset_next() only works on whole blocks.  If the
	 * EOF falls mid-block, then indicate that the "virtual hole"
	 * at the end of the file begins at the logical EOF, rather than
	 * at the end of the last block.
	 */
	if (noff > file_sz) {
		ASSERT(hole);
		noff = file_sz;
	}

	if (noff < *off)
		return (error);
	*off = noff;
	return (error);
}

int
zfs_holey(struct inode *ip, int cmd, loff_t *off)
{
	znode_t	*zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);
	int error;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	error = zfs_holey_common(ip, cmd, off);

	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_holey);
#endif /* SEEK_HOLE && SEEK_DATA */

#if defined(_KERNEL)
/*
 * When a file is memory mapped, we must keep the IO data synchronized
 * between the DMU cache and the memory mapped pages.  What this means:
 *
 * On Write:	If we find a memory mapped page, we write to *both*
 *		the page and the dmu buffer.
 */
static void
update_pages(struct inode *ip, int64_t start, int len,
    objset_t *os, uint64_t oid)
{
	struct address_space *mp = ip->i_mapping;
	struct page *pp;
	uint64_t nbytes;
	int64_t	off;
	void *pb;

	off = start & (PAGE_SIZE-1);
	for (start &= PAGE_MASK; len > 0; start += PAGE_SIZE) {
		nbytes = MIN(PAGE_SIZE - off, len);

		pp = find_lock_page(mp, start >> PAGE_SHIFT);
		if (pp) {
			if (mapping_writably_mapped(mp))
				flush_dcache_page(pp);

			pb = kmap(pp);
			(void) dmu_read(os, oid, start+off, nbytes, pb+off,
			    DMU_READ_PREFETCH);
			kunmap(pp);

			if (mapping_writably_mapped(mp))
				flush_dcache_page(pp);

			mark_page_accessed(pp);
			SetPageUptodate(pp);
			ClearPageError(pp);
			unlock_page(pp);
			put_page(pp);
		}

		len -= nbytes;
		off = 0;
	}
}

/*
 * When a file is memory mapped, we must keep the IO data synchronized
 * between the DMU cache and the memory mapped pages.  What this means:
 *
 * On Read:	We "read" preferentially from memory mapped pages,
 *		else we default from the dmu buffer.
 *
 * NOTE: We will always "break up" the IO into PAGESIZE uiomoves when
 *	 the file is memory mapped.
 */
static int
mappedread(struct inode *ip, int nbytes, uio_t *uio)
{
	struct address_space *mp = ip->i_mapping;
	struct page *pp;
	znode_t *zp = ITOZ(ip);
	int64_t	start, off;
	uint64_t bytes;
	int len = nbytes;
	int error = 0;
	void *pb;

	start = uio->uio_loffset;
	off = start & (PAGE_SIZE-1);
	for (start &= PAGE_MASK; len > 0; start += PAGE_SIZE) {
		bytes = MIN(PAGE_SIZE - off, len);

		pp = find_lock_page(mp, start >> PAGE_SHIFT);
		if (pp) {
			ASSERT(PageUptodate(pp));

			pb = kmap(pp);
			error = uiomove(pb + off, bytes, UIO_READ, uio);
			kunmap(pp);

			if (mapping_writably_mapped(mp))
				flush_dcache_page(pp);

			mark_page_accessed(pp);
			unlock_page(pp);
			put_page(pp);
		} else {
			error = dmu_read_uio_dbuf(sa_get_db(zp->z_sa_hdl),
			    uio, bytes);
		}

		len -= bytes;
		off = 0;
		if (error)
			break;
	}
	return (error);
}
#endif /* _KERNEL */

unsigned long zfs_read_chunk_size = 1024 * 1024; /* Tunable */

/*
 * Read bytes from specified file into supplied buffer.
 *
 *	IN:	ip	- inode of file to be read from.
 *		uio	- structure supplying read location, range info,
 *			  and return buffer.
 *		ioflag	- FSYNC flags; used to provide FRSYNC semantics.
 *			  O_DIRECT flag; used to bypass page cache.
 *		cr	- credentials of caller.
 *
 *	OUT:	uio	- updated offset and range, buffer filled.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Side Effects:
 *	inode - atime updated if byte count > 0
 */
/* ARGSUSED */
int
zfs_read(struct inode *ip, uio_t *uio, int ioflag, cred_t *cr)
{
	znode_t		*zp = ITOZ(ip);
	zfs_sb_t	*zsb = ITOZSB(ip);
	ssize_t		n, nbytes;
	int		error = 0;
	rl_t		*rl;
#ifdef HAVE_UIO_ZEROCOPY
	xuio_t		*xuio = NULL;
#endif /* HAVE_UIO_ZEROCOPY */

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	if (zp->z_pflags & ZFS_AV_QUARANTINED) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EACCES));
	}

	/*
	 * Validate file offset
	 */
	if (uio->uio_loffset < (offset_t)0) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Fasttrack empty reads
	 */
	if (uio->uio_resid == 0) {
		ZFS_EXIT(zsb);
		return (0);
	}

	/*
	 * If we're in FRSYNC mode, sync out this znode before reading it.
	 */
	if (ioflag & FRSYNC || zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zsb->z_log, zp->z_id);

	/*
	 * Lock the range against changes.
	 */
	rl = zfs_range_lock(&zp->z_range_lock, uio->uio_loffset, uio->uio_resid,
	    RL_READER);

	/*
	 * If we are reading past end-of-file we can skip
	 * to the end; but we might still need to set atime.
	 */
	if (uio->uio_loffset >= zp->z_size) {
		error = 0;
		goto out;
	}

	ASSERT(uio->uio_loffset < zp->z_size);
	n = MIN(uio->uio_resid, zp->z_size - uio->uio_loffset);

#ifdef HAVE_UIO_ZEROCOPY
	if ((uio->uio_extflg == UIO_XUIO) &&
	    (((xuio_t *)uio)->xu_type == UIOTYPE_ZEROCOPY)) {
		int nblk;
		int blksz = zp->z_blksz;
		uint64_t offset = uio->uio_loffset;

		xuio = (xuio_t *)uio;
		if ((ISP2(blksz))) {
			nblk = (P2ROUNDUP(offset + n, blksz) - P2ALIGN(offset,
			    blksz)) / blksz;
		} else {
			ASSERT(offset + n <= blksz);
			nblk = 1;
		}
		(void) dmu_xuio_init(xuio, nblk);

		if (vn_has_cached_data(ip)) {
			/*
			 * For simplicity, we always allocate a full buffer
			 * even if we only expect to read a portion of a block.
			 */
			while (--nblk >= 0) {
				(void) dmu_xuio_add(xuio,
				    dmu_request_arcbuf(sa_get_db(zp->z_sa_hdl),
				    blksz), 0, blksz);
			}
		}
	}
#endif /* HAVE_UIO_ZEROCOPY */

	while (n > 0) {
		nbytes = MIN(n, zfs_read_chunk_size -
		    P2PHASE(uio->uio_loffset, zfs_read_chunk_size));

		if (zp->z_is_mapped && !(ioflag & O_DIRECT)) {
			error = mappedread(ip, nbytes, uio);
		} else {
			error = dmu_read_uio_dbuf(sa_get_db(zp->z_sa_hdl),
			    uio, nbytes);
		}

		if (error) {
			/* convert checksum errors into IO errors */
			if (error == ECKSUM)
				error = SET_ERROR(EIO);
			break;
		}

		n -= nbytes;
	}
out:
	zfs_range_unlock(rl);

	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_read);

/*
 * Write the bytes to a file.
 *
 *	IN:	ip	- inode of file to be written to.
 *		uio	- structure supplying write location, range info,
 *			  and data buffer.
 *		ioflag	- FAPPEND flag set if in append mode.
 *			  O_DIRECT flag; used to bypass page cache.
 *		cr	- credentials of caller.
 *
 *	OUT:	uio	- updated offset and range.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	ip - ctime|mtime updated if byte count > 0
 */

/* ARGSUSED */
int
zfs_write(struct inode *ip, uio_t *uio, int ioflag, cred_t *cr)
{
	znode_t		*zp = ITOZ(ip);
	rlim64_t	limit = uio->uio_limit;
	ssize_t		start_resid = uio->uio_resid;
	ssize_t		tx_bytes;
	uint64_t	end_size;
	dmu_tx_t	*tx;
	zfs_sb_t	*zsb = ZTOZSB(zp);
	zilog_t		*zilog;
	offset_t	woff;
	ssize_t		n, nbytes;
	rl_t		*rl;
	int		max_blksz = zsb->z_max_blksz;
	int		error = 0;
	arc_buf_t	*abuf;
	const iovec_t	*aiov = NULL;
	xuio_t		*xuio = NULL;
	int		i_iov = 0;
	const iovec_t	*iovp = uio->uio_iov;
	int		write_eof;
	int		count = 0;
	sa_bulk_attr_t	bulk[4];
	uint64_t	mtime[2], ctime[2];
	ASSERTV(int	iovcnt = uio->uio_iovcnt);

	/*
	 * Fasttrack empty write
	 */
	n = start_resid;
	if (n == 0)
		return (0);

	if (limit == RLIM64_INFINITY || limit > MAXOFFSET_T)
		limit = MAXOFFSET_T;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zsb), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zsb), NULL, &ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zsb), NULL, &zp->z_size, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zsb), NULL,
	    &zp->z_pflags, 8);

	/*
	 * If immutable or not appending then return EPERM
	 */
	if ((zp->z_pflags & (ZFS_IMMUTABLE | ZFS_READONLY)) ||
	    ((zp->z_pflags & ZFS_APPENDONLY) && !(ioflag & FAPPEND) &&
	    (uio->uio_loffset < zp->z_size))) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EPERM));
	}

	zilog = zsb->z_log;

	/*
	 * Validate file offset
	 */
	woff = ioflag & FAPPEND ? zp->z_size : uio->uio_loffset;
	if (woff < 0) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Pre-fault the pages to ensure slow (eg NFS) pages
	 * don't hold up txg.
	 * Skip this if uio contains loaned arc_buf.
	 */
#ifdef HAVE_UIO_ZEROCOPY
	if ((uio->uio_extflg == UIO_XUIO) &&
	    (((xuio_t *)uio)->xu_type == UIOTYPE_ZEROCOPY))
		xuio = (xuio_t *)uio;
	else
#endif
		uio_prefaultpages(MIN(n, max_blksz), uio);

	/*
	 * If in append mode, set the io offset pointer to eof.
	 */
	if (ioflag & FAPPEND) {
		/*
		 * Obtain an appending range lock to guarantee file append
		 * semantics.  We reset the write offset once we have the lock.
		 */
		rl = zfs_range_lock(&zp->z_range_lock, 0, n, RL_APPEND);
		woff = rl->r_off;
		if (rl->r_len == UINT64_MAX) {
			/*
			 * We overlocked the file because this write will cause
			 * the file block size to increase.
			 * Note that zp_size cannot change with this lock held.
			 */
			woff = zp->z_size;
		}
		uio->uio_loffset = woff;
	} else {
		/*
		 * Note that if the file block size will change as a result of
		 * this write, then this range lock will lock the entire file
		 * so that we can re-write the block safely.
		 */
		rl = zfs_range_lock(&zp->z_range_lock, woff, n, RL_WRITER);
	}

	if (woff >= limit) {
		zfs_range_unlock(rl);
		ZFS_EXIT(zsb);
		return (SET_ERROR(EFBIG));
	}

	if ((woff + n) > limit || woff > (limit - n))
		n = limit - woff;

	/* Will this write extend the file length? */
	write_eof = (woff + n > zp->z_size);

	end_size = MAX(zp->z_size, woff + n);

	/*
	 * Write the file in reasonable size chunks.  Each chunk is written
	 * in a separate transaction; this keeps the intent log records small
	 * and allows us to do more fine-grained space accounting.
	 */
	while (n > 0) {
		abuf = NULL;
		woff = uio->uio_loffset;
		if (zfs_owner_overquota(zsb, zp, B_FALSE) ||
		    zfs_owner_overquota(zsb, zp, B_TRUE)) {
			if (abuf != NULL)
				dmu_return_arcbuf(abuf);
			error = SET_ERROR(EDQUOT);
			break;
		}

		if (xuio && abuf == NULL) {
			ASSERT(i_iov < iovcnt);
			ASSERT3U(uio->uio_segflg, !=, UIO_BVEC);
			aiov = &iovp[i_iov];
			abuf = dmu_xuio_arcbuf(xuio, i_iov);
			dmu_xuio_clear(xuio, i_iov);
			ASSERT((aiov->iov_base == abuf->b_data) ||
			    ((char *)aiov->iov_base - (char *)abuf->b_data +
			    aiov->iov_len == arc_buf_size(abuf)));
			i_iov++;
		} else if (abuf == NULL && n >= max_blksz &&
		    woff >= zp->z_size &&
		    P2PHASE(woff, max_blksz) == 0 &&
		    zp->z_blksz == max_blksz) {
			/*
			 * This write covers a full block.  "Borrow" a buffer
			 * from the dmu so that we can fill it before we enter
			 * a transaction.  This avoids the possibility of
			 * holding up the transaction if the data copy hangs
			 * up on a pagefault (e.g., from an NFS server mapping).
			 */
			size_t cbytes;

			abuf = dmu_request_arcbuf(sa_get_db(zp->z_sa_hdl),
			    max_blksz);
			ASSERT(abuf != NULL);
			ASSERT(arc_buf_size(abuf) == max_blksz);
			if ((error = uiocopy(abuf->b_data, max_blksz,
			    UIO_WRITE, uio, &cbytes))) {
				dmu_return_arcbuf(abuf);
				break;
			}
			ASSERT(cbytes == max_blksz);
		}

		/*
		 * Start a transaction.
		 */
		tx = dmu_tx_create(zsb->z_os);
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
		dmu_tx_hold_write(tx, zp->z_id, woff, MIN(n, max_blksz));
		zfs_sa_upgrade_txholds(tx, zp);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			if (abuf != NULL)
				dmu_return_arcbuf(abuf);
			break;
		}

		/*
		 * If zfs_range_lock() over-locked we grow the blocksize
		 * and then reduce the lock range.  This will only happen
		 * on the first iteration since zfs_range_reduce() will
		 * shrink down r_len to the appropriate size.
		 */
		if (rl->r_len == UINT64_MAX) {
			uint64_t new_blksz;

			if (zp->z_blksz > max_blksz) {
				/*
				 * File's blocksize is already larger than the
				 * "recordsize" property.  Only let it grow to
				 * the next power of 2.
				 */
				ASSERT(!ISP2(zp->z_blksz));
				new_blksz = MIN(end_size,
				    1 << highbit64(zp->z_blksz));
			} else {
				new_blksz = MIN(end_size, max_blksz);
			}
			zfs_grow_blocksize(zp, new_blksz, tx);
			zfs_range_reduce(rl, woff, n);
		}

		/*
		 * XXX - should we really limit each write to z_max_blksz?
		 * Perhaps we should use SPA_MAXBLOCKSIZE chunks?
		 */
		nbytes = MIN(n, max_blksz - P2PHASE(woff, max_blksz));

		if (abuf == NULL) {
			tx_bytes = uio->uio_resid;
			error = dmu_write_uio_dbuf(sa_get_db(zp->z_sa_hdl),
			    uio, nbytes, tx);
			tx_bytes -= uio->uio_resid;
		} else {
			tx_bytes = nbytes;
			ASSERT(xuio == NULL || tx_bytes == aiov->iov_len);
			/*
			 * If this is not a full block write, but we are
			 * extending the file past EOF and this data starts
			 * block-aligned, use assign_arcbuf().  Otherwise,
			 * write via dmu_write().
			 */
			if (tx_bytes < max_blksz && (!write_eof ||
			    aiov->iov_base != abuf->b_data)) {
				ASSERT(xuio);
				dmu_write(zsb->z_os, zp->z_id, woff,
				    aiov->iov_len, aiov->iov_base, tx);
				dmu_return_arcbuf(abuf);
				xuio_stat_wbuf_copied();
			} else {
				ASSERT(xuio || tx_bytes == max_blksz);
				dmu_assign_arcbuf(sa_get_db(zp->z_sa_hdl),
				    woff, abuf, tx);
			}
			ASSERT(tx_bytes <= uio->uio_resid);
			uioskip(uio, tx_bytes);
		}

		if (tx_bytes && zp->z_is_mapped && !(ioflag & O_DIRECT))
			update_pages(ip, woff, tx_bytes, zsb->z_os, zp->z_id);

		/*
		 * If we made no progress, we're done.  If we made even
		 * partial progress, update the znode and ZIL accordingly.
		 */
		if (tx_bytes == 0) {
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zsb),
			    (void *)&zp->z_size, sizeof (uint64_t), tx);
			dmu_tx_commit(tx);
			ASSERT(error != 0);
			break;
		}

		/*
		 * Clear Set-UID/Set-GID bits on successful write if not
		 * privileged and at least one of the excute bits is set.
		 *
		 * It would be nice to to this after all writes have
		 * been done, but that would still expose the ISUID/ISGID
		 * to another app after the partial write is committed.
		 *
		 * Note: we don't call zfs_fuid_map_id() here because
		 * user 0 is not an ephemeral uid.
		 */
		mutex_enter(&zp->z_acl_lock);
		if ((zp->z_mode & (S_IXUSR | (S_IXUSR >> 3) |
		    (S_IXUSR >> 6))) != 0 &&
		    (zp->z_mode & (S_ISUID | S_ISGID)) != 0 &&
		    secpolicy_vnode_setid_retain(cr,
		    (zp->z_mode & S_ISUID) != 0 && zp->z_uid == 0) != 0) {
			uint64_t newmode;
			zp->z_mode &= ~(S_ISUID | S_ISGID);
			newmode = zp->z_mode;
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_MODE(zsb),
			    (void *)&newmode, sizeof (uint64_t), tx);
		}
		mutex_exit(&zp->z_acl_lock);

		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);

		/*
		 * Update the file size (zp_size) if it has changed;
		 * account for possible concurrent updates.
		 */
		while ((end_size = zp->z_size) < uio->uio_loffset) {
			(void) atomic_cas_64(&zp->z_size, end_size,
			    uio->uio_loffset);
			ASSERT(error == 0);
		}
		/*
		 * If we are replaying and eof is non zero then force
		 * the file size to the specified eof. Note, there's no
		 * concurrency during replay.
		 */
		if (zsb->z_replay && zsb->z_replay_eof != 0)
			zp->z_size = zsb->z_replay_eof;

		error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);

		zfs_log_write(zilog, tx, TX_WRITE, zp, woff, tx_bytes, ioflag,
		    NULL, NULL);
		dmu_tx_commit(tx);

		if (error != 0)
			break;
		ASSERT(tx_bytes == nbytes);
		n -= nbytes;

		if (!xuio && n > 0)
			uio_prefaultpages(MIN(n, max_blksz), uio);
	}

	zfs_inode_update(zp);
	zfs_range_unlock(rl);

	/*
	 * If we're in replay mode, or we made no progress, return error.
	 * Otherwise, it's at least a partial write, so it's successful.
	 */
	if (zsb->z_replay || uio->uio_resid == start_resid) {
		ZFS_EXIT(zsb);
		return (error);
	}

	if (ioflag & (FSYNC | FDSYNC) ||
	    zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, zp->z_id);

	ZFS_EXIT(zsb);
	return (0);
}
EXPORT_SYMBOL(zfs_write);

void
zfs_iput_async(struct inode *ip)
{
	objset_t *os = ITOZSB(ip)->z_os;

	ASSERT(atomic_read(&ip->i_count) > 0);
	ASSERT(os != NULL);

	if (atomic_read(&ip->i_count) == 1)
		taskq_dispatch(dsl_pool_iput_taskq(dmu_objset_pool(os)),
		    (task_func_t *)iput, ip, TQ_SLEEP);
	else
		iput(ip);
}

void
zfs_get_done(zgd_t *zgd, int error)
{
	znode_t *zp = zgd->zgd_private;

	if (zgd->zgd_db)
		dmu_buf_rele(zgd->zgd_db, zgd);

	zfs_range_unlock(zgd->zgd_rl);

	/*
	 * Release the vnode asynchronously as we currently have the
	 * txg stopped from syncing.
	 */
	zfs_iput_async(ZTOI(zp));

	if (error == 0 && zgd->zgd_bp)
		zil_add_block(zgd->zgd_zilog, zgd->zgd_bp);

	kmem_free(zgd, sizeof (zgd_t));
}

#ifdef DEBUG
static int zil_fault_io = 0;
#endif

/*
 * Get data to generate a TX_WRITE intent log record.
 */
int
zfs_get_data(void *arg, lr_write_t *lr, char *buf, zio_t *zio)
{
	zfs_sb_t *zsb = arg;
	objset_t *os = zsb->z_os;
	znode_t *zp;
	uint64_t object = lr->lr_foid;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;
	blkptr_t *bp = &lr->lr_blkptr;
	dmu_buf_t *db;
	zgd_t *zgd;
	int error = 0;

	ASSERT(zio != NULL);
	ASSERT(size != 0);

	/*
	 * Nothing to do if the file has been removed
	 */
	if (zfs_zget(zsb, object, &zp) != 0)
		return (SET_ERROR(ENOENT));
	if (zp->z_unlinked) {
		/*
		 * Release the vnode asynchronously as we currently have the
		 * txg stopped from syncing.
		 */
		zfs_iput_async(ZTOI(zp));
		return (SET_ERROR(ENOENT));
	}

	zgd = (zgd_t *)kmem_zalloc(sizeof (zgd_t), KM_SLEEP);
	zgd->zgd_zilog = zsb->z_log;
	zgd->zgd_private = zp;

	/*
	 * Write records come in two flavors: immediate and indirect.
	 * For small writes it's cheaper to store the data with the
	 * log record (immediate); for large writes it's cheaper to
	 * sync the data and get a pointer to it (indirect) so that
	 * we don't have to write the data twice.
	 */
	if (buf != NULL) { /* immediate write */
		zgd->zgd_rl = zfs_range_lock(&zp->z_range_lock, offset, size,
		    RL_READER);
		/* test for truncation needs to be done while range locked */
		if (offset >= zp->z_size) {
			error = SET_ERROR(ENOENT);
		} else {
			error = dmu_read(os, object, offset, size, buf,
			    DMU_READ_NO_PREFETCH);
		}
		ASSERT(error == 0 || error == ENOENT);
	} else { /* indirect write */
		/*
		 * Have to lock the whole block to ensure when it's
		 * written out and it's checksum is being calculated
		 * that no one can change the data. We need to re-check
		 * blocksize after we get the lock in case it's changed!
		 */
		for (;;) {
			uint64_t blkoff;
			size = zp->z_blksz;
			blkoff = ISP2(size) ? P2PHASE(offset, size) : offset;
			offset -= blkoff;
			zgd->zgd_rl = zfs_range_lock(&zp->z_range_lock, offset,
			    size, RL_READER);
			if (zp->z_blksz == size)
				break;
			offset += blkoff;
			zfs_range_unlock(zgd->zgd_rl);
		}
		/* test for truncation needs to be done while range locked */
		if (lr->lr_offset >= zp->z_size)
			error = SET_ERROR(ENOENT);
#ifdef DEBUG
		if (zil_fault_io) {
			error = SET_ERROR(EIO);
			zil_fault_io = 0;
		}
#endif
		if (error == 0)
			error = dmu_buf_hold(os, object, offset, zgd, &db,
			    DMU_READ_NO_PREFETCH);

		if (error == 0) {
			blkptr_t *obp = dmu_buf_get_blkptr(db);
			if (obp) {
				ASSERT(BP_IS_HOLE(bp));
				*bp = *obp;
			}

			zgd->zgd_db = db;
			zgd->zgd_bp = bp;

			ASSERT(db->db_offset == offset);
			ASSERT(db->db_size == size);

			error = dmu_sync(zio, lr->lr_common.lrc_txg,
			    zfs_get_done, zgd);
			ASSERT(error || lr->lr_length <= zp->z_blksz);

			/*
			 * On success, we need to wait for the write I/O
			 * initiated by dmu_sync() to complete before we can
			 * release this dbuf.  We will finish everything up
			 * in the zfs_get_done() callback.
			 */
			if (error == 0)
				return (0);

			if (error == EALREADY) {
				lr->lr_common.lrc_txtype = TX_WRITE2;
				error = 0;
			}
		}
	}

	zfs_get_done(zgd, error);

	return (error);
}

/*ARGSUSED*/
int
zfs_access(struct inode *ip, int mode, int flag, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);
	int error;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	if (flag & V_ACE_MASK)
		error = zfs_zaccess(zp, mode, flag, B_FALSE, cr);
	else
		error = zfs_zaccess_rwx(zp, mode, flag, cr);

	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_access);

/*
 * Lookup an entry in a directory, or an extended attribute directory.
 * If it exists, return a held inode reference for it.
 *
 *	IN:	dip	- inode of directory to search.
 *		nm	- name of entry to lookup.
 *		flags	- LOOKUP_XATTR set if looking for an attribute.
 *		cr	- credentials of caller.
 *		direntflags - directory lookup flags
 *		realpnp - returned pathname.
 *
 *	OUT:	ipp	- inode of located entry, NULL if not found.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	NA
 */
/* ARGSUSED */
int
zfs_lookup(struct inode *dip, char *nm, struct inode **ipp, int flags,
    cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	znode_t *zdp = ITOZ(dip);
	zfs_sb_t *zsb = ITOZSB(dip);
	int error = 0;

	/* fast path */
	if (!(flags & (LOOKUP_XATTR | FIGNORECASE))) {

		if (!S_ISDIR(dip->i_mode)) {
			return (SET_ERROR(ENOTDIR));
		} else if (zdp->z_sa_hdl == NULL) {
			return (SET_ERROR(EIO));
		}

		if (nm[0] == 0 || (nm[0] == '.' && nm[1] == '\0')) {
			error = zfs_fastaccesschk_execute(zdp, cr);
			if (!error) {
				*ipp = dip;
				igrab(*ipp);
				return (0);
			}
			return (error);
#ifdef HAVE_DNLC
		} else {
			vnode_t *tvp = dnlc_lookup(dvp, nm);

			if (tvp) {
				error = zfs_fastaccesschk_execute(zdp, cr);
				if (error) {
					iput(tvp);
					return (error);
				}
				if (tvp == DNLC_NO_VNODE) {
					iput(tvp);
					return (SET_ERROR(ENOENT));
				} else {
					*vpp = tvp;
					return (specvp_check(vpp, cr));
				}
			}
#endif /* HAVE_DNLC */
		}
	}

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zdp);

	*ipp = NULL;

	if (flags & LOOKUP_XATTR) {
		/*
		 * We don't allow recursive attributes..
		 * Maybe someday we will.
		 */
		if (zdp->z_pflags & ZFS_XATTR) {
			ZFS_EXIT(zsb);
			return (SET_ERROR(EINVAL));
		}

		if ((error = zfs_get_xattrdir(zdp, ipp, cr, flags))) {
			ZFS_EXIT(zsb);
			return (error);
		}

		/*
		 * Do we have permission to get into attribute directory?
		 */

		if ((error = zfs_zaccess(ITOZ(*ipp), ACE_EXECUTE, 0,
		    B_FALSE, cr))) {
			iput(*ipp);
			*ipp = NULL;
		}

		ZFS_EXIT(zsb);
		return (error);
	}

	if (!S_ISDIR(dip->i_mode)) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(ENOTDIR));
	}

	/*
	 * Check accessibility of directory.
	 */

	if ((error = zfs_zaccess(zdp, ACE_EXECUTE, 0, B_FALSE, cr))) {
		ZFS_EXIT(zsb);
		return (error);
	}

	if (zsb->z_utf8 && u8_validate(nm, strlen(nm),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EILSEQ));
	}

	error = zfs_dirlook(zdp, nm, ipp, flags, direntflags, realpnp);
	if ((error == 0) && (*ipp))
		zfs_inode_update(ITOZ(*ipp));

	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_lookup);

/*
 * Attempt to create a new entry in a directory.  If the entry
 * already exists, truncate the file if permissible, else return
 * an error.  Return the ip of the created or trunc'd file.
 *
 *	IN:	dip	- inode of directory to put new file entry in.
 *		name	- name of new file entry.
 *		vap	- attributes of new file.
 *		excl	- flag indicating exclusive or non-exclusive mode.
 *		mode	- mode to open file with.
 *		cr	- credentials of caller.
 *		flag	- large file flag [UNUSED].
 *		vsecp	- ACL to be set
 *
 *	OUT:	ipp	- inode of created or trunc'd entry.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dip - ctime|mtime updated if new entry created
 *	 ip - ctime|mtime always, atime if new
 */

/* ARGSUSED */
int
zfs_create(struct inode *dip, char *name, vattr_t *vap, int excl,
    int mode, struct inode **ipp, cred_t *cr, int flag, vsecattr_t *vsecp)
{
	znode_t		*zp, *dzp = ITOZ(dip);
	zfs_sb_t	*zsb = ITOZSB(dip);
	zilog_t		*zilog;
	objset_t	*os;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	int		error;
	uid_t		uid;
	gid_t		gid;
	zfs_acl_ids_t   acl_ids;
	boolean_t	fuid_dirtied;
	boolean_t	have_acl = B_FALSE;
	boolean_t	waited = B_FALSE;

	/*
	 * If we have an ephemeral id, ACL, or XVATTR then
	 * make sure file system is at proper version
	 */

	gid = crgetgid(cr);
	uid = crgetuid(cr);

	if (zsb->z_use_fuids == B_FALSE &&
	    (vsecp || IS_EPHEMERAL(uid) || IS_EPHEMERAL(gid)))
		return (SET_ERROR(EINVAL));

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(dzp);
	os = zsb->z_os;
	zilog = zsb->z_log;

	if (zsb->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EILSEQ));
	}

	if (vap->va_mask & ATTR_XVATTR) {
		if ((error = secpolicy_xvattr((xvattr_t *)vap,
		    crgetuid(cr), cr, vap->va_mode)) != 0) {
			ZFS_EXIT(zsb);
			return (error);
		}
	}

top:
	*ipp = NULL;
	if (*name == '\0') {
		/*
		 * Null component name refers to the directory itself.
		 */
		igrab(dip);
		zp = dzp;
		dl = NULL;
		error = 0;
	} else {
		/* possible igrab(zp) */
		int zflg = 0;

		if (flag & FIGNORECASE)
			zflg |= ZCILOOK;

		error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg,
		    NULL, NULL);
		if (error) {
			if (have_acl)
				zfs_acl_ids_free(&acl_ids);
			if (strcmp(name, "..") == 0)
				error = SET_ERROR(EISDIR);
			ZFS_EXIT(zsb);
			return (error);
		}
	}

	if (zp == NULL) {
		uint64_t txtype;

		/*
		 * Create a new file object and update the directory
		 * to reference it.
		 */
		if ((error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr))) {
			if (have_acl)
				zfs_acl_ids_free(&acl_ids);
			goto out;
		}

		/*
		 * We only support the creation of regular files in
		 * extended attribute directories.
		 */

		if ((dzp->z_pflags & ZFS_XATTR) && !S_ISREG(vap->va_mode)) {
			if (have_acl)
				zfs_acl_ids_free(&acl_ids);
			error = SET_ERROR(EINVAL);
			goto out;
		}

		if (!have_acl && (error = zfs_acl_ids_create(dzp, 0, vap,
		    cr, vsecp, &acl_ids)) != 0)
			goto out;
		have_acl = B_TRUE;

		if (zfs_acl_ids_overquota(zsb, &acl_ids)) {
			zfs_acl_ids_free(&acl_ids);
			error = SET_ERROR(EDQUOT);
			goto out;
		}

		tx = dmu_tx_create(os);

		dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
		    ZFS_SA_BASE_ATTR_SIZE);

		fuid_dirtied = zsb->z_fuid_dirty;
		if (fuid_dirtied)
			zfs_fuid_txhold(zsb, tx);
		dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
		dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
		if (!zsb->z_use_sa &&
		    acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
			dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
			    0, acl_ids.z_aclp->z_acl_bytes);
		}
		error = dmu_tx_assign(tx, waited ? TXG_WAITED : TXG_NOWAIT);
		if (error) {
			zfs_dirent_unlock(dl);
			if (error == ERESTART) {
				waited = B_TRUE;
				dmu_tx_wait(tx);
				dmu_tx_abort(tx);
				goto top;
			}
			zfs_acl_ids_free(&acl_ids);
			dmu_tx_abort(tx);
			ZFS_EXIT(zsb);
			return (error);
		}
		zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

		if (fuid_dirtied)
			zfs_fuid_sync(zsb, tx);

		(void) zfs_link_create(dl, zp, tx, ZNEW);
		txtype = zfs_log_create_txtype(Z_FILE, vsecp, vap);
		if (flag & FIGNORECASE)
			txtype |= TX_CI;
		zfs_log_create(zilog, tx, txtype, dzp, zp, name,
		    vsecp, acl_ids.z_fuidp, vap);
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_commit(tx);
	} else {
		int aflags = (flag & FAPPEND) ? V_APPEND : 0;

		if (have_acl)
			zfs_acl_ids_free(&acl_ids);
		have_acl = B_FALSE;

		/*
		 * A directory entry already exists for this name.
		 */
		/*
		 * Can't truncate an existing file if in exclusive mode.
		 */
		if (excl) {
			error = SET_ERROR(EEXIST);
			goto out;
		}
		/*
		 * Can't open a directory for writing.
		 */
		if (S_ISDIR(ZTOI(zp)->i_mode)) {
			error = SET_ERROR(EISDIR);
			goto out;
		}
		/*
		 * Verify requested access to file.
		 */
		if (mode && (error = zfs_zaccess_rwx(zp, mode, aflags, cr))) {
			goto out;
		}

		mutex_enter(&dzp->z_lock);
		dzp->z_seq++;
		mutex_exit(&dzp->z_lock);

		/*
		 * Truncate regular files if requested.
		 */
		if (S_ISREG(ZTOI(zp)->i_mode) &&
		    (vap->va_mask & ATTR_SIZE) && (vap->va_size == 0)) {
			/* we can't hold any locks when calling zfs_freesp() */
			zfs_dirent_unlock(dl);
			dl = NULL;
			error = zfs_freesp(zp, 0, 0, mode, TRUE);
		}
	}
out:

	if (dl)
		zfs_dirent_unlock(dl);

	if (error) {
		if (zp)
			iput(ZTOI(zp));
	} else {
		zfs_inode_update(dzp);
		zfs_inode_update(zp);
		*ipp = ZTOI(zp);
	}

	if (zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_create);

/*
 * Remove an entry from a directory.
 *
 *	IN:	dip	- inode of directory to remove entry from.
 *		name	- name of entry to remove.
 *		cr	- credentials of caller.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dip - ctime|mtime
 *	 ip - ctime (if nlink > 0)
 */

uint64_t null_xattr = 0;

/*ARGSUSED*/
int
zfs_remove(struct inode *dip, char *name, cred_t *cr)
{
	znode_t		*zp, *dzp = ITOZ(dip);
	znode_t		*xzp;
	struct inode	*ip;
	zfs_sb_t	*zsb = ITOZSB(dip);
	zilog_t		*zilog;
	uint64_t	xattr_obj;
	uint64_t	xattr_obj_unlinked = 0;
	uint64_t	obj = 0;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	boolean_t	unlinked;
	uint64_t	txtype;
	pathname_t	*realnmp = NULL;
#ifdef HAVE_PN_UTILS
	pathname_t	realnm;
#endif /* HAVE_PN_UTILS */
	int		error;
	int		zflg = ZEXISTS;
	boolean_t	waited = B_FALSE;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(dzp);
	zilog = zsb->z_log;

#ifdef HAVE_PN_UTILS
	if (flags & FIGNORECASE) {
		zflg |= ZCILOOK;
		pn_alloc(&realnm);
		realnmp = &realnm;
	}
#endif /* HAVE_PN_UTILS */

top:
	xattr_obj = 0;
	xzp = NULL;
	/*
	 * Attempt to lock directory; fail if entry doesn't exist.
	 */
	if ((error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg,
	    NULL, realnmp))) {
#ifdef HAVE_PN_UTILS
		if (realnmp)
			pn_free(realnmp);
#endif /* HAVE_PN_UTILS */
		ZFS_EXIT(zsb);
		return (error);
	}

	ip = ZTOI(zp);

	if ((error = zfs_zaccess_delete(dzp, zp, cr))) {
		goto out;
	}

	/*
	 * Need to use rmdir for removing directories.
	 */
	if (S_ISDIR(ip->i_mode)) {
		error = SET_ERROR(EPERM);
		goto out;
	}

#ifdef HAVE_DNLC
	if (realnmp)
		dnlc_remove(dvp, realnmp->pn_buf);
	else
		dnlc_remove(dvp, name);
#endif /* HAVE_DNLC */

	/*
	 * We never delete the znode and always place it in the unlinked
	 * set.  The dentry cache will always hold the last reference and
	 * is responsible for safely freeing the znode.
	 */
	obj = zp->z_id;
	tx = dmu_tx_create(zsb->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);

	/* are there any extended attributes? */
	error = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zsb),
	    &xattr_obj, sizeof (xattr_obj));
	if (error == 0 && xattr_obj) {
		error = zfs_zget(zsb, xattr_obj, &xzp);
		ASSERT0(error);
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		dmu_tx_hold_sa(tx, xzp->z_sa_hdl, B_FALSE);
	}

	/* charge as an update -- would be nice not to charge at all */
	dmu_tx_hold_zap(tx, zsb->z_unlinkedobj, FALSE, NULL);

	error = dmu_tx_assign(tx, waited ? TXG_WAITED : TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		iput(ip);
		if (xzp)
			iput(ZTOI(xzp));
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
#ifdef HAVE_PN_UTILS
		if (realnmp)
			pn_free(realnmp);
#endif /* HAVE_PN_UTILS */
		dmu_tx_abort(tx);
		ZFS_EXIT(zsb);
		return (error);
	}

	/*
	 * Remove the directory entry.
	 */
	error = zfs_link_destroy(dl, zp, tx, zflg, &unlinked);

	if (error) {
		dmu_tx_commit(tx);
		goto out;
	}

	if (unlinked) {
		/*
		 * Hold z_lock so that we can make sure that the ACL obj
		 * hasn't changed.  Could have been deleted due to
		 * zfs_sa_upgrade().
		 */
		mutex_enter(&zp->z_lock);
		(void) sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zsb),
		    &xattr_obj_unlinked, sizeof (xattr_obj_unlinked));
		mutex_exit(&zp->z_lock);
		zfs_unlinked_add(zp, tx);
	}

	txtype = TX_REMOVE;
#ifdef HAVE_PN_UTILS
	if (flags & FIGNORECASE)
		txtype |= TX_CI;
#endif /* HAVE_PN_UTILS */
	zfs_log_remove(zilog, tx, txtype, dzp, name, obj);

	dmu_tx_commit(tx);
out:
#ifdef HAVE_PN_UTILS
	if (realnmp)
		pn_free(realnmp);
#endif /* HAVE_PN_UTILS */

	zfs_dirent_unlock(dl);
	zfs_inode_update(dzp);
	zfs_inode_update(zp);
	if (xzp)
		zfs_inode_update(xzp);

	iput(ip);
	if (xzp)
		iput(ZTOI(xzp));

	if (zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_remove);

/*
 * Create a new directory and insert it into dip using the name
 * provided.  Return a pointer to the inserted directory.
 *
 *	IN:	dip	- inode of directory to add subdir to.
 *		dirname	- name of new directory.
 *		vap	- attributes of new directory.
 *		cr	- credentials of caller.
 *		vsecp	- ACL to be set
 *
 *	OUT:	ipp	- inode of created directory.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dip - ctime|mtime updated
 *	ipp - ctime|mtime|atime updated
 */
/*ARGSUSED*/
int
zfs_mkdir(struct inode *dip, char *dirname, vattr_t *vap, struct inode **ipp,
    cred_t *cr, int flags, vsecattr_t *vsecp)
{
	znode_t		*zp, *dzp = ITOZ(dip);
	zfs_sb_t	*zsb = ITOZSB(dip);
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	uint64_t	txtype;
	dmu_tx_t	*tx;
	int		error;
	int		zf = ZNEW;
	uid_t		uid;
	gid_t		gid = crgetgid(cr);
	zfs_acl_ids_t   acl_ids;
	boolean_t	fuid_dirtied;
	boolean_t	waited = B_FALSE;

	ASSERT(S_ISDIR(vap->va_mode));

	/*
	 * If we have an ephemeral id, ACL, or XVATTR then
	 * make sure file system is at proper version
	 */

	uid = crgetuid(cr);
	if (zsb->z_use_fuids == B_FALSE &&
	    (vsecp || IS_EPHEMERAL(uid) || IS_EPHEMERAL(gid)))
		return (SET_ERROR(EINVAL));

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(dzp);
	zilog = zsb->z_log;

	if (dzp->z_pflags & ZFS_XATTR) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EINVAL));
	}

	if (zsb->z_utf8 && u8_validate(dirname,
	    strlen(dirname), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EILSEQ));
	}
	if (flags & FIGNORECASE)
		zf |= ZCILOOK;

	if (vap->va_mask & ATTR_XVATTR) {
		if ((error = secpolicy_xvattr((xvattr_t *)vap,
		    crgetuid(cr), cr, vap->va_mode)) != 0) {
			ZFS_EXIT(zsb);
			return (error);
		}
	}

	if ((error = zfs_acl_ids_create(dzp, 0, vap, cr,
	    vsecp, &acl_ids)) != 0) {
		ZFS_EXIT(zsb);
		return (error);
	}
	/*
	 * First make sure the new directory doesn't exist.
	 *
	 * Existence is checked first to make sure we don't return
	 * EACCES instead of EEXIST which can cause some applications
	 * to fail.
	 */
top:
	*ipp = NULL;

	if ((error = zfs_dirent_lock(&dl, dzp, dirname, &zp, zf,
	    NULL, NULL))) {
		zfs_acl_ids_free(&acl_ids);
		ZFS_EXIT(zsb);
		return (error);
	}

	if ((error = zfs_zaccess(dzp, ACE_ADD_SUBDIRECTORY, 0, B_FALSE, cr))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zsb);
		return (error);
	}

	if (zfs_acl_ids_overquota(zsb, &acl_ids)) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zsb);
		return (SET_ERROR(EDQUOT));
	}

	/*
	 * Add a new entry to the directory.
	 */
	tx = dmu_tx_create(zsb->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, dirname);
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
	fuid_dirtied = zsb->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zsb, tx);
	if (!zsb->z_use_sa && acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    acl_ids.z_aclp->z_acl_bytes);
	}

	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE);

	error = dmu_tx_assign(tx, waited ? TXG_WAITED : TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		ZFS_EXIT(zsb);
		return (error);
	}

	/*
	 * Create new node.
	 */
	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zsb, tx);

	/*
	 * Now put new name in parent dir.
	 */
	(void) zfs_link_create(dl, zp, tx, ZNEW);

	*ipp = ZTOI(zp);

	txtype = zfs_log_create_txtype(Z_DIR, vsecp, vap);
	if (flags & FIGNORECASE)
		txtype |= TX_CI;
	zfs_log_create(zilog, tx, txtype, dzp, zp, dirname, vsecp,
	    acl_ids.z_fuidp, vap);

	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	if (zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	zfs_inode_update(dzp);
	zfs_inode_update(zp);
	ZFS_EXIT(zsb);
	return (0);
}
EXPORT_SYMBOL(zfs_mkdir);

/*
 * Remove a directory subdir entry.  If the current working
 * directory is the same as the subdir to be removed, the
 * remove will fail.
 *
 *	IN:	dip	- inode of directory to remove from.
 *		name	- name of directory to be removed.
 *		cwd	- inode of current working directory.
 *		cr	- credentials of caller.
 *		flags	- case flags
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dip - ctime|mtime updated
 */
/*ARGSUSED*/
int
zfs_rmdir(struct inode *dip, char *name, struct inode *cwd, cred_t *cr,
    int flags)
{
	znode_t		*dzp = ITOZ(dip);
	znode_t		*zp;
	struct inode	*ip;
	zfs_sb_t	*zsb = ITOZSB(dip);
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	int		error;
	int		zflg = ZEXISTS;
	boolean_t	waited = B_FALSE;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(dzp);
	zilog = zsb->z_log;

	if (flags & FIGNORECASE)
		zflg |= ZCILOOK;
top:
	zp = NULL;

	/*
	 * Attempt to lock directory; fail if entry doesn't exist.
	 */
	if ((error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg,
	    NULL, NULL))) {
		ZFS_EXIT(zsb);
		return (error);
	}

	ip = ZTOI(zp);

	if ((error = zfs_zaccess_delete(dzp, zp, cr))) {
		goto out;
	}

	if (!S_ISDIR(ip->i_mode)) {
		error = SET_ERROR(ENOTDIR);
		goto out;
	}

	if (ip == cwd) {
		error = SET_ERROR(EINVAL);
		goto out;
	}

	/*
	 * Grab a lock on the directory to make sure that noone is
	 * trying to add (or lookup) entries while we are removing it.
	 */
	rw_enter(&zp->z_name_lock, RW_WRITER);

	/*
	 * Grab a lock on the parent pointer to make sure we play well
	 * with the treewalk and directory rename code.
	 */
	rw_enter(&zp->z_parent_lock, RW_WRITER);

	tx = dmu_tx_create(zsb->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, zsb->z_unlinkedobj, FALSE, NULL);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);
	error = dmu_tx_assign(tx, waited ? TXG_WAITED : TXG_NOWAIT);
	if (error) {
		rw_exit(&zp->z_parent_lock);
		rw_exit(&zp->z_name_lock);
		zfs_dirent_unlock(dl);
		iput(ip);
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		ZFS_EXIT(zsb);
		return (error);
	}

	error = zfs_link_destroy(dl, zp, tx, zflg, NULL);

	if (error == 0) {
		uint64_t txtype = TX_RMDIR;
		if (flags & FIGNORECASE)
			txtype |= TX_CI;
		zfs_log_remove(zilog, tx, txtype, dzp, name, ZFS_NO_OBJECT);
	}

	dmu_tx_commit(tx);

	rw_exit(&zp->z_parent_lock);
	rw_exit(&zp->z_name_lock);
out:
	zfs_dirent_unlock(dl);

	zfs_inode_update(dzp);
	zfs_inode_update(zp);
	iput(ip);

	if (zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_rmdir);

/*
 * Read as many directory entries as will fit into the provided
 * dirent buffer from the given directory cursor position.
 *
 *	IN:	ip	- inode of directory to read.
 *		dirent	- buffer for directory entries.
 *
 *	OUT:	dirent	- filler buffer of directory entries.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	ip - atime updated
 *
 * Note that the low 4 bits of the cookie returned by zap is always zero.
 * This allows us to use the low range for "special" directory entries:
 * We use 0 for '.', and 1 for '..'.  If this is the root of the filesystem,
 * we use the offset 2 for the '.zfs' directory.
 */
/* ARGSUSED */
int
zfs_readdir(struct inode *ip, struct dir_context *ctx, cred_t *cr)
{
	znode_t		*zp = ITOZ(ip);
	zfs_sb_t	*zsb = ITOZSB(ip);
	objset_t	*os;
	zap_cursor_t	zc;
	zap_attribute_t	zap;
	int		error;
	uint8_t		prefetch;
	uint8_t		type;
	int		done = 0;
	uint64_t	parent;
	uint64_t	offset; /* must be unsigned; checks for < 1 */

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zsb),
	    &parent, sizeof (parent))) != 0)
		goto out;

	/*
	 * Quit if directory has been removed (posix)
	 */
	if (zp->z_unlinked)
		goto out;

	error = 0;
	os = zsb->z_os;
	offset = ctx->pos;
	prefetch = zp->z_zn_prefetch;

	/*
	 * Initialize the iterator cursor.
	 */
	if (offset <= 3) {
		/*
		 * Start iteration from the beginning of the directory.
		 */
		zap_cursor_init(&zc, os, zp->z_id);
	} else {
		/*
		 * The offset is a serialized cursor.
		 */
		zap_cursor_init_serialized(&zc, os, zp->z_id, offset);
	}

	/*
	 * Transform to file-system independent format
	 */
	while (!done) {
		uint64_t objnum;
		/*
		 * Special case `.', `..', and `.zfs'.
		 */
		if (offset == 0) {
			(void) strcpy(zap.za_name, ".");
			zap.za_normalization_conflict = 0;
			objnum = zp->z_id;
			type = DT_DIR;
		} else if (offset == 1) {
			(void) strcpy(zap.za_name, "..");
			zap.za_normalization_conflict = 0;
			objnum = parent;
			type = DT_DIR;
		} else if (offset == 2 && zfs_show_ctldir(zp)) {
			(void) strcpy(zap.za_name, ZFS_CTLDIR_NAME);
			zap.za_normalization_conflict = 0;
			objnum = ZFSCTL_INO_ROOT;
			type = DT_DIR;
		} else {
			/*
			 * Grab next entry.
			 */
			if ((error = zap_cursor_retrieve(&zc, &zap))) {
				if (error == ENOENT)
					break;
				else
					goto update;
			}

			/*
			 * Allow multiple entries provided the first entry is
			 * the object id.  Non-zpl consumers may safely make
			 * use of the additional space.
			 *
			 * XXX: This should be a feature flag for compatibility
			 */
			if (zap.za_integer_length != 8 ||
			    zap.za_num_integers == 0) {
				cmn_err(CE_WARN, "zap_readdir: bad directory "
				    "entry, obj = %lld, offset = %lld, "
				    "length = %d, num = %lld\n",
				    (u_longlong_t)zp->z_id,
				    (u_longlong_t)offset,
				    zap.za_integer_length,
				    (u_longlong_t)zap.za_num_integers);
				error = SET_ERROR(ENXIO);
				goto update;
			}

			objnum = ZFS_DIRENT_OBJ(zap.za_first_integer);
			type = ZFS_DIRENT_TYPE(zap.za_first_integer);
		}

		done = !dir_emit(ctx, zap.za_name, strlen(zap.za_name),
		    objnum, type);
		if (done)
			break;

		/* Prefetch znode */
		if (prefetch) {
			dmu_prefetch(os, objnum, 0, 0);
		}

		/*
		 * Move to the next entry, fill in the previous offset.
		 */
		if (offset > 2 || (offset == 2 && !zfs_show_ctldir(zp))) {
			zap_cursor_advance(&zc);
			offset = zap_cursor_serialize(&zc);
		} else {
			offset += 1;
		}
		ctx->pos = offset;
	}
	zp->z_zn_prefetch = B_FALSE; /* a lookup will re-enable pre-fetching */

update:
	zap_cursor_fini(&zc);
	if (error == ENOENT)
		error = 0;
out:
	ZFS_EXIT(zsb);

	return (error);
}
EXPORT_SYMBOL(zfs_readdir);

ulong_t zfs_fsync_sync_cnt = 4;

int
zfs_fsync(struct inode *ip, int syncflag, cred_t *cr)
{
	znode_t	*zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);

	(void) tsd_set(zfs_fsyncer_key, (void *)zfs_fsync_sync_cnt);

	if (zsb->z_os->os_sync != ZFS_SYNC_DISABLED) {
		ZFS_ENTER(zsb);
		ZFS_VERIFY_ZP(zp);
		zil_commit(zsb->z_log, zp->z_id);
		ZFS_EXIT(zsb);
	}
	tsd_set(zfs_fsyncer_key, NULL);

	return (0);
}
EXPORT_SYMBOL(zfs_fsync);


/*
 * Get the requested file attributes and place them in the provided
 * vattr structure.
 *
 *	IN:	ip	- inode of file.
 *		vap	- va_mask identifies requested attributes.
 *			  If ATTR_XVATTR set, then optional attrs are requested
 *		flags	- ATTR_NOACLCHECK (CIFS server context)
 *		cr	- credentials of caller.
 *
 *	OUT:	vap	- attribute values.
 *
 *	RETURN:	0 (always succeeds)
 */
/* ARGSUSED */
int
zfs_getattr(struct inode *ip, vattr_t *vap, int flags, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);
	int	error = 0;
	uint64_t links;
	uint64_t atime[2], mtime[2], ctime[2];
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t *xoap = NULL;
	boolean_t skipaclchk = (flags & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	sa_bulk_attr_t bulk[3];
	int count = 0;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	zfs_fuid_map_ids(zp, cr, &vap->va_uid, &vap->va_gid);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zsb), NULL, &atime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zsb), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zsb), NULL, &ctime, 16);

	if ((error = sa_bulk_lookup(zp->z_sa_hdl, bulk, count)) != 0) {
		ZFS_EXIT(zsb);
		return (error);
	}

	/*
	 * If ACL is trivial don't bother looking for ACE_READ_ATTRIBUTES.
	 * Also, if we are the owner don't bother, since owner should
	 * always be allowed to read basic attributes of file.
	 */
	if (!(zp->z_pflags & ZFS_ACL_TRIVIAL) &&
	    (vap->va_uid != crgetuid(cr))) {
		if ((error = zfs_zaccess(zp, ACE_READ_ATTRIBUTES, 0,
		    skipaclchk, cr))) {
			ZFS_EXIT(zsb);
			return (error);
		}
	}

	/*
	 * Return all attributes.  It's cheaper to provide the answer
	 * than to determine whether we were asked the question.
	 */

	mutex_enter(&zp->z_lock);
	vap->va_type = vn_mode_to_vtype(zp->z_mode);
	vap->va_mode = zp->z_mode;
	vap->va_fsid = ZTOI(zp)->i_sb->s_dev;
	vap->va_nodeid = zp->z_id;
	if ((zp->z_id == zsb->z_root) && zfs_show_ctldir(zp))
		links = zp->z_links + 1;
	else
		links = zp->z_links;
	vap->va_nlink = MIN(links, ZFS_LINK_MAX);
	vap->va_size = i_size_read(ip);
	vap->va_rdev = ip->i_rdev;
	vap->va_seq = ip->i_generation;

	/*
	 * Add in any requested optional attributes and the create time.
	 * Also set the corresponding bits in the returned attribute bitmap.
	 */
	if ((xoap = xva_getxoptattr(xvap)) != NULL && zsb->z_use_fuids) {
		if (XVA_ISSET_REQ(xvap, XAT_ARCHIVE)) {
			xoap->xoa_archive =
			    ((zp->z_pflags & ZFS_ARCHIVE) != 0);
			XVA_SET_RTN(xvap, XAT_ARCHIVE);
		}

		if (XVA_ISSET_REQ(xvap, XAT_READONLY)) {
			xoap->xoa_readonly =
			    ((zp->z_pflags & ZFS_READONLY) != 0);
			XVA_SET_RTN(xvap, XAT_READONLY);
		}

		if (XVA_ISSET_REQ(xvap, XAT_SYSTEM)) {
			xoap->xoa_system =
			    ((zp->z_pflags & ZFS_SYSTEM) != 0);
			XVA_SET_RTN(xvap, XAT_SYSTEM);
		}

		if (XVA_ISSET_REQ(xvap, XAT_HIDDEN)) {
			xoap->xoa_hidden =
			    ((zp->z_pflags & ZFS_HIDDEN) != 0);
			XVA_SET_RTN(xvap, XAT_HIDDEN);
		}

		if (XVA_ISSET_REQ(xvap, XAT_NOUNLINK)) {
			xoap->xoa_nounlink =
			    ((zp->z_pflags & ZFS_NOUNLINK) != 0);
			XVA_SET_RTN(xvap, XAT_NOUNLINK);
		}

		if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE)) {
			xoap->xoa_immutable =
			    ((zp->z_pflags & ZFS_IMMUTABLE) != 0);
			XVA_SET_RTN(xvap, XAT_IMMUTABLE);
		}

		if (XVA_ISSET_REQ(xvap, XAT_APPENDONLY)) {
			xoap->xoa_appendonly =
			    ((zp->z_pflags & ZFS_APPENDONLY) != 0);
			XVA_SET_RTN(xvap, XAT_APPENDONLY);
		}

		if (XVA_ISSET_REQ(xvap, XAT_NODUMP)) {
			xoap->xoa_nodump =
			    ((zp->z_pflags & ZFS_NODUMP) != 0);
			XVA_SET_RTN(xvap, XAT_NODUMP);
		}

		if (XVA_ISSET_REQ(xvap, XAT_OPAQUE)) {
			xoap->xoa_opaque =
			    ((zp->z_pflags & ZFS_OPAQUE) != 0);
			XVA_SET_RTN(xvap, XAT_OPAQUE);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED)) {
			xoap->xoa_av_quarantined =
			    ((zp->z_pflags & ZFS_AV_QUARANTINED) != 0);
			XVA_SET_RTN(xvap, XAT_AV_QUARANTINED);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED)) {
			xoap->xoa_av_modified =
			    ((zp->z_pflags & ZFS_AV_MODIFIED) != 0);
			XVA_SET_RTN(xvap, XAT_AV_MODIFIED);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP) &&
		    S_ISREG(ip->i_mode)) {
			zfs_sa_get_scanstamp(zp, xvap);
		}

		if (XVA_ISSET_REQ(xvap, XAT_CREATETIME)) {
			uint64_t times[2];

			(void) sa_lookup(zp->z_sa_hdl, SA_ZPL_CRTIME(zsb),
			    times, sizeof (times));
			ZFS_TIME_DECODE(&xoap->xoa_createtime, times);
			XVA_SET_RTN(xvap, XAT_CREATETIME);
		}

		if (XVA_ISSET_REQ(xvap, XAT_REPARSE)) {
			xoap->xoa_reparse = ((zp->z_pflags & ZFS_REPARSE) != 0);
			XVA_SET_RTN(xvap, XAT_REPARSE);
		}
		if (XVA_ISSET_REQ(xvap, XAT_GEN)) {
			xoap->xoa_generation = zp->z_gen;
			XVA_SET_RTN(xvap, XAT_GEN);
		}

		if (XVA_ISSET_REQ(xvap, XAT_OFFLINE)) {
			xoap->xoa_offline =
			    ((zp->z_pflags & ZFS_OFFLINE) != 0);
			XVA_SET_RTN(xvap, XAT_OFFLINE);
		}

		if (XVA_ISSET_REQ(xvap, XAT_SPARSE)) {
			xoap->xoa_sparse =
			    ((zp->z_pflags & ZFS_SPARSE) != 0);
			XVA_SET_RTN(xvap, XAT_SPARSE);
		}
	}

	ZFS_TIME_DECODE(&vap->va_atime, atime);
	ZFS_TIME_DECODE(&vap->va_mtime, mtime);
	ZFS_TIME_DECODE(&vap->va_ctime, ctime);

	mutex_exit(&zp->z_lock);

	sa_object_size(zp->z_sa_hdl, &vap->va_blksize, &vap->va_nblocks);

	if (zp->z_blksz == 0) {
		/*
		 * Block size hasn't been set; suggest maximal I/O transfers.
		 */
		vap->va_blksize = zsb->z_max_blksz;
	}

	ZFS_EXIT(zsb);
	return (0);
}
EXPORT_SYMBOL(zfs_getattr);

/*
 * Get the basic file attributes and place them in the provided kstat
 * structure.  The inode is assumed to be the authoritative source
 * for most of the attributes.  However, the znode currently has the
 * authoritative atime, blksize, and block count.
 *
 *	IN:	ip	- inode of file.
 *
 *	OUT:	sp	- kstat values.
 *
 *	RETURN:	0 (always succeeds)
 */
/* ARGSUSED */
int
zfs_getattr_fast(struct inode *ip, struct kstat *sp)
{
	znode_t *zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);
	uint32_t blksize;
	u_longlong_t nblocks;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	mutex_enter(&zp->z_lock);

	generic_fillattr(ip, sp);

	sa_object_size(zp->z_sa_hdl, &blksize, &nblocks);
	sp->blksize = blksize;
	sp->blocks = nblocks;

	if (unlikely(zp->z_blksz == 0)) {
		/*
		 * Block size hasn't been set; suggest maximal I/O transfers.
		 */
		sp->blksize = zsb->z_max_blksz;
	}

	mutex_exit(&zp->z_lock);

	/*
	 * Required to prevent NFS client from detecting different inode
	 * numbers of snapshot root dentry before and after snapshot mount.
	 */
	if (zsb->z_issnap) {
		if (ip->i_sb->s_root->d_inode == ip)
			sp->ino = ZFSCTL_INO_SNAPDIRS -
				dmu_objset_id(zsb->z_os);
	}

	ZFS_EXIT(zsb);

	return (0);
}
EXPORT_SYMBOL(zfs_getattr_fast);

/*
 * Set the file attributes to the values contained in the
 * vattr structure.
 *
 *	IN:	ip	- inode of file to be modified.
 *		vap	- new attribute values.
 *			  If ATTR_XVATTR set, then optional attrs are being set
 *		flags	- ATTR_UTIME set if non-default time values provided.
 *			- ATTR_NOACLCHECK (CIFS context only).
 *		cr	- credentials of caller.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	ip - ctime updated, mtime updated if size changed.
 */
/* ARGSUSED */
int
zfs_setattr(struct inode *ip, vattr_t *vap, int flags, cred_t *cr)
{
	znode_t		*zp = ITOZ(ip);
	zfs_sb_t	*zsb = ITOZSB(ip);
	zilog_t		*zilog;
	dmu_tx_t	*tx;
	vattr_t		oldva;
	xvattr_t	*tmpxvattr;
	uint_t		mask = vap->va_mask;
	uint_t		saved_mask = 0;
	int		trim_mask = 0;
	uint64_t	new_mode;
	uint64_t	new_uid, new_gid;
	uint64_t	xattr_obj;
	uint64_t	mtime[2], ctime[2], atime[2];
	znode_t		*attrzp;
	int		need_policy = FALSE;
	int		err, err2;
	zfs_fuid_info_t *fuidp = NULL;
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t	*xoap;
	zfs_acl_t	*aclp;
	boolean_t skipaclchk = (flags & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	boolean_t	fuid_dirtied = B_FALSE;
	sa_bulk_attr_t	*bulk, *xattr_bulk;
	int		count = 0, xattr_count = 0;

	if (mask == 0)
		return (0);

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	zilog = zsb->z_log;

	/*
	 * Make sure that if we have ephemeral uid/gid or xvattr specified
	 * that file system is at proper version level
	 */

	if (zsb->z_use_fuids == B_FALSE &&
	    (((mask & ATTR_UID) && IS_EPHEMERAL(vap->va_uid)) ||
	    ((mask & ATTR_GID) && IS_EPHEMERAL(vap->va_gid)) ||
	    (mask & ATTR_XVATTR))) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EINVAL));
	}

	if (mask & ATTR_SIZE && S_ISDIR(ip->i_mode)) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EISDIR));
	}

	if (mask & ATTR_SIZE && !S_ISREG(ip->i_mode) && !S_ISFIFO(ip->i_mode)) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * If this is an xvattr_t, then get a pointer to the structure of
	 * optional attributes.  If this is NULL, then we have a vattr_t.
	 */
	xoap = xva_getxoptattr(xvap);

	tmpxvattr = kmem_alloc(sizeof (xvattr_t), KM_SLEEP);
	xva_init(tmpxvattr);

	bulk = kmem_alloc(sizeof (sa_bulk_attr_t) * 7, KM_SLEEP);
	xattr_bulk = kmem_alloc(sizeof (sa_bulk_attr_t) * 7, KM_SLEEP);

	/*
	 * Immutable files can only alter immutable bit and atime
	 */
	if ((zp->z_pflags & ZFS_IMMUTABLE) &&
	    ((mask & (ATTR_SIZE|ATTR_UID|ATTR_GID|ATTR_MTIME|ATTR_MODE)) ||
	    ((mask & ATTR_XVATTR) && XVA_ISSET_REQ(xvap, XAT_CREATETIME)))) {
		err = EPERM;
		goto out3;
	}

	if ((mask & ATTR_SIZE) && (zp->z_pflags & ZFS_READONLY)) {
		err = EPERM;
		goto out3;
	}

	/*
	 * Verify timestamps doesn't overflow 32 bits.
	 * ZFS can handle large timestamps, but 32bit syscalls can't
	 * handle times greater than 2039.  This check should be removed
	 * once large timestamps are fully supported.
	 */
	if (mask & (ATTR_ATIME | ATTR_MTIME)) {
		if (((mask & ATTR_ATIME) &&
		    TIMESPEC_OVERFLOW(&vap->va_atime)) ||
		    ((mask & ATTR_MTIME) &&
		    TIMESPEC_OVERFLOW(&vap->va_mtime))) {
			err = EOVERFLOW;
			goto out3;
		}
	}

top:
	attrzp = NULL;
	aclp = NULL;

	/* Can this be moved to before the top label? */
	if (zfs_is_readonly(zsb)) {
		err = EROFS;
		goto out3;
	}

	/*
	 * First validate permissions
	 */

	if (mask & ATTR_SIZE) {
		err = zfs_zaccess(zp, ACE_WRITE_DATA, 0, skipaclchk, cr);
		if (err)
			goto out3;

		/*
		 * XXX - Note, we are not providing any open
		 * mode flags here (like FNDELAY), so we may
		 * block if there are locks present... this
		 * should be addressed in openat().
		 */
		/* XXX - would it be OK to generate a log record here? */
		err = zfs_freesp(zp, vap->va_size, 0, 0, FALSE);
		if (err)
			goto out3;
	}

	if (mask & (ATTR_ATIME|ATTR_MTIME) ||
	    ((mask & ATTR_XVATTR) && (XVA_ISSET_REQ(xvap, XAT_HIDDEN) ||
	    XVA_ISSET_REQ(xvap, XAT_READONLY) ||
	    XVA_ISSET_REQ(xvap, XAT_ARCHIVE) ||
	    XVA_ISSET_REQ(xvap, XAT_OFFLINE) ||
	    XVA_ISSET_REQ(xvap, XAT_SPARSE) ||
	    XVA_ISSET_REQ(xvap, XAT_CREATETIME) ||
	    XVA_ISSET_REQ(xvap, XAT_SYSTEM)))) {
		need_policy = zfs_zaccess(zp, ACE_WRITE_ATTRIBUTES, 0,
		    skipaclchk, cr);
	}

	if (mask & (ATTR_UID|ATTR_GID)) {
		int	idmask = (mask & (ATTR_UID|ATTR_GID));
		int	take_owner;
		int	take_group;

		/*
		 * NOTE: even if a new mode is being set,
		 * we may clear S_ISUID/S_ISGID bits.
		 */

		if (!(mask & ATTR_MODE))
			vap->va_mode = zp->z_mode;

		/*
		 * Take ownership or chgrp to group we are a member of
		 */

		take_owner = (mask & ATTR_UID) && (vap->va_uid == crgetuid(cr));
		take_group = (mask & ATTR_GID) &&
		    zfs_groupmember(zsb, vap->va_gid, cr);

		/*
		 * If both ATTR_UID and ATTR_GID are set then take_owner and
		 * take_group must both be set in order to allow taking
		 * ownership.
		 *
		 * Otherwise, send the check through secpolicy_vnode_setattr()
		 *
		 */

		if (((idmask == (ATTR_UID|ATTR_GID)) &&
		    take_owner && take_group) ||
		    ((idmask == ATTR_UID) && take_owner) ||
		    ((idmask == ATTR_GID) && take_group)) {
			if (zfs_zaccess(zp, ACE_WRITE_OWNER, 0,
			    skipaclchk, cr) == 0) {
				/*
				 * Remove setuid/setgid for non-privileged users
				 */
				(void) secpolicy_setid_clear(vap, cr);
				trim_mask = (mask & (ATTR_UID|ATTR_GID));
			} else {
				need_policy =  TRUE;
			}
		} else {
			need_policy =  TRUE;
		}
	}

	mutex_enter(&zp->z_lock);
	oldva.va_mode = zp->z_mode;
	zfs_fuid_map_ids(zp, cr, &oldva.va_uid, &oldva.va_gid);
	if (mask & ATTR_XVATTR) {
		/*
		 * Update xvattr mask to include only those attributes
		 * that are actually changing.
		 *
		 * the bits will be restored prior to actually setting
		 * the attributes so the caller thinks they were set.
		 */
		if (XVA_ISSET_REQ(xvap, XAT_APPENDONLY)) {
			if (xoap->xoa_appendonly !=
			    ((zp->z_pflags & ZFS_APPENDONLY) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_APPENDONLY);
				XVA_SET_REQ(tmpxvattr, XAT_APPENDONLY);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_NOUNLINK)) {
			if (xoap->xoa_nounlink !=
			    ((zp->z_pflags & ZFS_NOUNLINK) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_NOUNLINK);
				XVA_SET_REQ(tmpxvattr, XAT_NOUNLINK);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE)) {
			if (xoap->xoa_immutable !=
			    ((zp->z_pflags & ZFS_IMMUTABLE) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_IMMUTABLE);
				XVA_SET_REQ(tmpxvattr, XAT_IMMUTABLE);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_NODUMP)) {
			if (xoap->xoa_nodump !=
			    ((zp->z_pflags & ZFS_NODUMP) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_NODUMP);
				XVA_SET_REQ(tmpxvattr, XAT_NODUMP);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED)) {
			if (xoap->xoa_av_modified !=
			    ((zp->z_pflags & ZFS_AV_MODIFIED) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_AV_MODIFIED);
				XVA_SET_REQ(tmpxvattr, XAT_AV_MODIFIED);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED)) {
			if ((!S_ISREG(ip->i_mode) &&
			    xoap->xoa_av_quarantined) ||
			    xoap->xoa_av_quarantined !=
			    ((zp->z_pflags & ZFS_AV_QUARANTINED) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_AV_QUARANTINED);
				XVA_SET_REQ(tmpxvattr, XAT_AV_QUARANTINED);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_REPARSE)) {
			mutex_exit(&zp->z_lock);
			err = EPERM;
			goto out3;
		}

		if (need_policy == FALSE &&
		    (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP) ||
		    XVA_ISSET_REQ(xvap, XAT_OPAQUE))) {
			need_policy = TRUE;
		}
	}

	mutex_exit(&zp->z_lock);

	if (mask & ATTR_MODE) {
		if (zfs_zaccess(zp, ACE_WRITE_ACL, 0, skipaclchk, cr) == 0) {
			err = secpolicy_setid_setsticky_clear(ip, vap,
			    &oldva, cr);
			if (err)
				goto out3;

			trim_mask |= ATTR_MODE;
		} else {
			need_policy = TRUE;
		}
	}

	if (need_policy) {
		/*
		 * If trim_mask is set then take ownership
		 * has been granted or write_acl is present and user
		 * has the ability to modify mode.  In that case remove
		 * UID|GID and or MODE from mask so that
		 * secpolicy_vnode_setattr() doesn't revoke it.
		 */

		if (trim_mask) {
			saved_mask = vap->va_mask;
			vap->va_mask &= ~trim_mask;
		}
		err = secpolicy_vnode_setattr(cr, ip, vap, &oldva, flags,
		    (int (*)(void *, int, cred_t *))zfs_zaccess_unix, zp);
		if (err)
			goto out3;

		if (trim_mask)
			vap->va_mask |= saved_mask;
	}

	/*
	 * secpolicy_vnode_setattr, or take ownership may have
	 * changed va_mask
	 */
	mask = vap->va_mask;

	if ((mask & (ATTR_UID | ATTR_GID))) {
		err = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zsb),
		    &xattr_obj, sizeof (xattr_obj));

		if (err == 0 && xattr_obj) {
			err = zfs_zget(ZTOZSB(zp), xattr_obj, &attrzp);
			if (err)
				goto out2;
		}
		if (mask & ATTR_UID) {
			new_uid = zfs_fuid_create(zsb,
			    (uint64_t)vap->va_uid, cr, ZFS_OWNER, &fuidp);
			if (new_uid != zp->z_uid &&
			    zfs_fuid_overquota(zsb, B_FALSE, new_uid)) {
				if (attrzp)
					iput(ZTOI(attrzp));
				err = EDQUOT;
				goto out2;
			}
		}

		if (mask & ATTR_GID) {
			new_gid = zfs_fuid_create(zsb, (uint64_t)vap->va_gid,
			    cr, ZFS_GROUP, &fuidp);
			if (new_gid != zp->z_gid &&
			    zfs_fuid_overquota(zsb, B_TRUE, new_gid)) {
				if (attrzp)
					iput(ZTOI(attrzp));
				err = EDQUOT;
				goto out2;
			}
		}
	}
	tx = dmu_tx_create(zsb->z_os);

	if (mask & ATTR_MODE) {
		uint64_t pmode = zp->z_mode;
		uint64_t acl_obj;
		new_mode = (pmode & S_IFMT) | (vap->va_mode & ~S_IFMT);

		zfs_acl_chmod_setattr(zp, &aclp, new_mode);

		mutex_enter(&zp->z_lock);
		if (!zp->z_is_sa && ((acl_obj = zfs_external_acl(zp)) != 0)) {
			/*
			 * Are we upgrading ACL from old V0 format
			 * to V1 format?
			 */
			if (zsb->z_version >= ZPL_VERSION_FUID &&
			    zfs_znode_acl_version(zp) ==
			    ZFS_ACL_VERSION_INITIAL) {
				dmu_tx_hold_free(tx, acl_obj, 0,
				    DMU_OBJECT_END);
				dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
				    0, aclp->z_acl_bytes);
			} else {
				dmu_tx_hold_write(tx, acl_obj, 0,
				    aclp->z_acl_bytes);
			}
		} else if (!zp->z_is_sa && aclp->z_acl_bytes > ZFS_ACE_SPACE) {
			dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
			    0, aclp->z_acl_bytes);
		}
		mutex_exit(&zp->z_lock);
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
	} else {
		if ((mask & ATTR_XVATTR) &&
		    XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP))
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		else
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	}

	if (attrzp) {
		dmu_tx_hold_sa(tx, attrzp->z_sa_hdl, B_FALSE);
	}

	fuid_dirtied = zsb->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zsb, tx);

	zfs_sa_upgrade_txholds(tx, zp);

	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err)
		goto out;

	count = 0;
	/*
	 * Set each attribute requested.
	 * We group settings according to the locks they need to acquire.
	 *
	 * Note: you cannot set ctime directly, although it will be
	 * updated as a side-effect of calling this function.
	 */


	if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
		mutex_enter(&zp->z_acl_lock);
	mutex_enter(&zp->z_lock);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zsb), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));

	if (attrzp) {
		if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
			mutex_enter(&attrzp->z_acl_lock);
		mutex_enter(&attrzp->z_lock);
		SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
		    SA_ZPL_FLAGS(zsb), NULL, &attrzp->z_pflags,
		    sizeof (attrzp->z_pflags));
	}

	if (mask & (ATTR_UID|ATTR_GID)) {

		if (mask & ATTR_UID) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zsb), NULL,
			    &new_uid, sizeof (new_uid));
			zp->z_uid = new_uid;
			if (attrzp) {
				SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
				    SA_ZPL_UID(zsb), NULL, &new_uid,
				    sizeof (new_uid));
				attrzp->z_uid = new_uid;
			}
		}

		if (mask & ATTR_GID) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zsb),
			    NULL, &new_gid, sizeof (new_gid));
			zp->z_gid = new_gid;
			if (attrzp) {
				SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
				    SA_ZPL_GID(zsb), NULL, &new_gid,
				    sizeof (new_gid));
				attrzp->z_gid = new_gid;
			}
		}
		if (!(mask & ATTR_MODE)) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zsb),
			    NULL, &new_mode, sizeof (new_mode));
			new_mode = zp->z_mode;
		}
		err = zfs_acl_chown_setattr(zp);
		ASSERT(err == 0);
		if (attrzp) {
			err = zfs_acl_chown_setattr(attrzp);
			ASSERT(err == 0);
		}
	}

	if (mask & ATTR_MODE) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zsb), NULL,
		    &new_mode, sizeof (new_mode));
		zp->z_mode = new_mode;
		ASSERT3P(aclp, !=, NULL);
		err = zfs_aclset_common(zp, aclp, cr, tx);
		ASSERT0(err);
		if (zp->z_acl_cached)
			zfs_acl_free(zp->z_acl_cached);
		zp->z_acl_cached = aclp;
		aclp = NULL;
	}


	if ((mask & ATTR_ATIME) || zp->z_atime_dirty) {
		zp->z_atime_dirty = 0;
		ZFS_TIME_ENCODE(&ip->i_atime, atime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zsb), NULL,
		    &atime, sizeof (atime));
	}

	if (mask & ATTR_MTIME) {
		ZFS_TIME_ENCODE(&vap->va_mtime, mtime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zsb), NULL,
		    mtime, sizeof (mtime));
	}

	/* XXX - shouldn't this be done *before* the ATIME/MTIME checks? */
	if (mask & ATTR_SIZE && !(mask & ATTR_MTIME)) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zsb),
		    NULL, mtime, sizeof (mtime));
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zsb), NULL,
		    &ctime, sizeof (ctime));
		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);
	} else if (mask != 0) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zsb), NULL,
		    &ctime, sizeof (ctime));
		zfs_tstamp_update_setup(zp, STATE_CHANGED, mtime, ctime);
		if (attrzp) {
			SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
			    SA_ZPL_CTIME(zsb), NULL,
			    &ctime, sizeof (ctime));
			zfs_tstamp_update_setup(attrzp, STATE_CHANGED,
			    mtime, ctime);
		}
	}
	/*
	 * Do this after setting timestamps to prevent timestamp
	 * update from toggling bit
	 */

	if (xoap && (mask & ATTR_XVATTR)) {

		/*
		 * restore trimmed off masks
		 * so that return masks can be set for caller.
		 */

		if (XVA_ISSET_REQ(tmpxvattr, XAT_APPENDONLY)) {
			XVA_SET_REQ(xvap, XAT_APPENDONLY);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_NOUNLINK)) {
			XVA_SET_REQ(xvap, XAT_NOUNLINK);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_IMMUTABLE)) {
			XVA_SET_REQ(xvap, XAT_IMMUTABLE);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_NODUMP)) {
			XVA_SET_REQ(xvap, XAT_NODUMP);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_AV_MODIFIED)) {
			XVA_SET_REQ(xvap, XAT_AV_MODIFIED);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_AV_QUARANTINED)) {
			XVA_SET_REQ(xvap, XAT_AV_QUARANTINED);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP))
			ASSERT(S_ISREG(ip->i_mode));

		zfs_xvattr_set(zp, xvap, tx);
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zsb, tx);

	if (mask != 0)
		zfs_log_setattr(zilog, tx, TX_SETATTR, zp, vap, mask, fuidp);

	mutex_exit(&zp->z_lock);
	if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
		mutex_exit(&zp->z_acl_lock);

	if (attrzp) {
		if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
			mutex_exit(&attrzp->z_acl_lock);
		mutex_exit(&attrzp->z_lock);
	}
out:
	if (err == 0 && attrzp) {
		err2 = sa_bulk_update(attrzp->z_sa_hdl, xattr_bulk,
		    xattr_count, tx);
		ASSERT(err2 == 0);
	}

	if (attrzp)
		iput(ZTOI(attrzp));
	if (aclp)
		zfs_acl_free(aclp);

	if (fuidp) {
		zfs_fuid_info_free(fuidp);
		fuidp = NULL;
	}

	if (err) {
		dmu_tx_abort(tx);
		if (err == ERESTART)
			goto top;
	} else {
		err2 = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		dmu_tx_commit(tx);
		zfs_inode_update(zp);
	}

out2:
	if (zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

out3:
	kmem_free(xattr_bulk, sizeof (sa_bulk_attr_t) * 7);
	kmem_free(bulk, sizeof (sa_bulk_attr_t) * 7);
	kmem_free(tmpxvattr, sizeof (xvattr_t));
	ZFS_EXIT(zsb);
	return (err);
}
EXPORT_SYMBOL(zfs_setattr);

typedef struct zfs_zlock {
	krwlock_t	*zl_rwlock;	/* lock we acquired */
	znode_t		*zl_znode;	/* znode we held */
	struct zfs_zlock *zl_next;	/* next in list */
} zfs_zlock_t;

/*
 * Drop locks and release vnodes that were held by zfs_rename_lock().
 */
static void
zfs_rename_unlock(zfs_zlock_t **zlpp)
{
	zfs_zlock_t *zl;

	while ((zl = *zlpp) != NULL) {
		if (zl->zl_znode != NULL)
			iput(ZTOI(zl->zl_znode));
		rw_exit(zl->zl_rwlock);
		*zlpp = zl->zl_next;
		kmem_free(zl, sizeof (*zl));
	}
}

/*
 * Search back through the directory tree, using the ".." entries.
 * Lock each directory in the chain to prevent concurrent renames.
 * Fail any attempt to move a directory into one of its own descendants.
 * XXX - z_parent_lock can overlap with map or grow locks
 */
static int
zfs_rename_lock(znode_t *szp, znode_t *tdzp, znode_t *sdzp, zfs_zlock_t **zlpp)
{
	zfs_zlock_t	*zl;
	znode_t		*zp = tdzp;
	uint64_t	rootid = ZTOZSB(zp)->z_root;
	uint64_t	oidp = zp->z_id;
	krwlock_t	*rwlp = &szp->z_parent_lock;
	krw_t		rw = RW_WRITER;

	/*
	 * First pass write-locks szp and compares to zp->z_id.
	 * Later passes read-lock zp and compare to zp->z_parent.
	 */
	do {
		if (!rw_tryenter(rwlp, rw)) {
			/*
			 * Another thread is renaming in this path.
			 * Note that if we are a WRITER, we don't have any
			 * parent_locks held yet.
			 */
			if (rw == RW_READER && zp->z_id > szp->z_id) {
				/*
				 * Drop our locks and restart
				 */
				zfs_rename_unlock(&zl);
				*zlpp = NULL;
				zp = tdzp;
				oidp = zp->z_id;
				rwlp = &szp->z_parent_lock;
				rw = RW_WRITER;
				continue;
			} else {
				/*
				 * Wait for other thread to drop its locks
				 */
				rw_enter(rwlp, rw);
			}
		}

		zl = kmem_alloc(sizeof (*zl), KM_SLEEP);
		zl->zl_rwlock = rwlp;
		zl->zl_znode = NULL;
		zl->zl_next = *zlpp;
		*zlpp = zl;

		if (oidp == szp->z_id)		/* We're a descendant of szp */
			return (SET_ERROR(EINVAL));

		if (oidp == rootid)		/* We've hit the top */
			return (0);

		if (rw == RW_READER) {		/* i.e. not the first pass */
			int error = zfs_zget(ZTOZSB(zp), oidp, &zp);
			if (error)
				return (error);
			zl->zl_znode = zp;
		}
		(void) sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(ZTOZSB(zp)),
		    &oidp, sizeof (oidp));
		rwlp = &zp->z_parent_lock;
		rw = RW_READER;

	} while (zp->z_id != sdzp->z_id);

	return (0);
}

/*
 * Move an entry from the provided source directory to the target
 * directory.  Change the entry name as indicated.
 *
 *	IN:	sdip	- Source directory containing the "old entry".
 *		snm	- Old entry name.
 *		tdip	- Target directory to contain the "new entry".
 *		tnm	- New entry name.
 *		cr	- credentials of caller.
 *		flags	- case flags
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	sdip,tdip - ctime|mtime updated
 */
/*ARGSUSED*/
int
zfs_rename(struct inode *sdip, char *snm, struct inode *tdip, char *tnm,
    cred_t *cr, int flags)
{
	znode_t		*tdzp, *szp, *tzp;
	znode_t		*sdzp = ITOZ(sdip);
	zfs_sb_t	*zsb = ITOZSB(sdip);
	zilog_t		*zilog;
	zfs_dirlock_t	*sdl, *tdl;
	dmu_tx_t	*tx;
	zfs_zlock_t	*zl;
	int		cmp, serr, terr;
	int		error = 0;
	int		zflg = 0;
	boolean_t	waited = B_FALSE;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(sdzp);
	zilog = zsb->z_log;

	if (tdip->i_sb != sdip->i_sb || zfsctl_is_node(tdip)) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EXDEV));
	}

	tdzp = ITOZ(tdip);
	ZFS_VERIFY_ZP(tdzp);
	if (zsb->z_utf8 && u8_validate(tnm,
	    strlen(tnm), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EILSEQ));
	}

	if (flags & FIGNORECASE)
		zflg |= ZCILOOK;

top:
	szp = NULL;
	tzp = NULL;
	zl = NULL;

	/*
	 * This is to prevent the creation of links into attribute space
	 * by renaming a linked file into/outof an attribute directory.
	 * See the comment in zfs_link() for why this is considered bad.
	 */
	if ((tdzp->z_pflags & ZFS_XATTR) != (sdzp->z_pflags & ZFS_XATTR)) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Lock source and target directory entries.  To prevent deadlock,
	 * a lock ordering must be defined.  We lock the directory with
	 * the smallest object id first, or if it's a tie, the one with
	 * the lexically first name.
	 */
	if (sdzp->z_id < tdzp->z_id) {
		cmp = -1;
	} else if (sdzp->z_id > tdzp->z_id) {
		cmp = 1;
	} else {
		/*
		 * First compare the two name arguments without
		 * considering any case folding.
		 */
		int nofold = (zsb->z_norm & ~U8_TEXTPREP_TOUPPER);

		cmp = u8_strcmp(snm, tnm, 0, nofold, U8_UNICODE_LATEST, &error);
		ASSERT(error == 0 || !zsb->z_utf8);
		if (cmp == 0) {
			/*
			 * POSIX: "If the old argument and the new argument
			 * both refer to links to the same existing file,
			 * the rename() function shall return successfully
			 * and perform no other action."
			 */
			ZFS_EXIT(zsb);
			return (0);
		}
		/*
		 * If the file system is case-folding, then we may
		 * have some more checking to do.  A case-folding file
		 * system is either supporting mixed case sensitivity
		 * access or is completely case-insensitive.  Note
		 * that the file system is always case preserving.
		 *
		 * In mixed sensitivity mode case sensitive behavior
		 * is the default.  FIGNORECASE must be used to
		 * explicitly request case insensitive behavior.
		 *
		 * If the source and target names provided differ only
		 * by case (e.g., a request to rename 'tim' to 'Tim'),
		 * we will treat this as a special case in the
		 * case-insensitive mode: as long as the source name
		 * is an exact match, we will allow this to proceed as
		 * a name-change request.
		 */
		if ((zsb->z_case == ZFS_CASE_INSENSITIVE ||
		    (zsb->z_case == ZFS_CASE_MIXED &&
		    flags & FIGNORECASE)) &&
		    u8_strcmp(snm, tnm, 0, zsb->z_norm, U8_UNICODE_LATEST,
		    &error) == 0) {
			/*
			 * case preserving rename request, require exact
			 * name matches
			 */
			zflg |= ZCIEXACT;
			zflg &= ~ZCILOOK;
		}
	}

	/*
	 * If the source and destination directories are the same, we should
	 * grab the z_name_lock of that directory only once.
	 */
	if (sdzp == tdzp) {
		zflg |= ZHAVELOCK;
		rw_enter(&sdzp->z_name_lock, RW_READER);
	}

	if (cmp < 0) {
		serr = zfs_dirent_lock(&sdl, sdzp, snm, &szp,
		    ZEXISTS | zflg, NULL, NULL);
		terr = zfs_dirent_lock(&tdl,
		    tdzp, tnm, &tzp, ZRENAMING | zflg, NULL, NULL);
	} else {
		terr = zfs_dirent_lock(&tdl,
		    tdzp, tnm, &tzp, zflg, NULL, NULL);
		serr = zfs_dirent_lock(&sdl,
		    sdzp, snm, &szp, ZEXISTS | ZRENAMING | zflg,
		    NULL, NULL);
	}

	if (serr) {
		/*
		 * Source entry invalid or not there.
		 */
		if (!terr) {
			zfs_dirent_unlock(tdl);
			if (tzp)
				iput(ZTOI(tzp));
		}

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		if (strcmp(snm, "..") == 0)
			serr = EINVAL;
		ZFS_EXIT(zsb);
		return (serr);
	}
	if (terr) {
		zfs_dirent_unlock(sdl);
		iput(ZTOI(szp));

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		if (strcmp(tnm, "..") == 0)
			terr = EINVAL;
		ZFS_EXIT(zsb);
		return (terr);
	}

	/*
	 * Must have write access at the source to remove the old entry
	 * and write access at the target to create the new entry.
	 * Note that if target and source are the same, this can be
	 * done in a single check.
	 */

	if ((error = zfs_zaccess_rename(sdzp, szp, tdzp, tzp, cr)))
		goto out;

	if (S_ISDIR(ZTOI(szp)->i_mode)) {
		/*
		 * Check to make sure rename is valid.
		 * Can't do a move like this: /usr/a/b to /usr/a/b/c/d
		 */
		if ((error = zfs_rename_lock(szp, tdzp, sdzp, &zl)))
			goto out;
	}

	/*
	 * Does target exist?
	 */
	if (tzp) {
		/*
		 * Source and target must be the same type.
		 */
		if (S_ISDIR(ZTOI(szp)->i_mode)) {
			if (!S_ISDIR(ZTOI(tzp)->i_mode)) {
				error = SET_ERROR(ENOTDIR);
				goto out;
			}
		} else {
			if (S_ISDIR(ZTOI(tzp)->i_mode)) {
				error = SET_ERROR(EISDIR);
				goto out;
			}
		}
		/*
		 * POSIX dictates that when the source and target
		 * entries refer to the same file object, rename
		 * must do nothing and exit without error.
		 */
		if (szp->z_id == tzp->z_id) {
			error = 0;
			goto out;
		}
	}

	tx = dmu_tx_create(zsb->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_sa(tx, sdzp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, sdzp->z_id, FALSE, snm);
	dmu_tx_hold_zap(tx, tdzp->z_id, TRUE, tnm);
	if (sdzp != tdzp) {
		dmu_tx_hold_sa(tx, tdzp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, tdzp);
	}
	if (tzp) {
		dmu_tx_hold_sa(tx, tzp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, tzp);
	}

	zfs_sa_upgrade_txholds(tx, szp);
	dmu_tx_hold_zap(tx, zsb->z_unlinkedobj, FALSE, NULL);
	error = dmu_tx_assign(tx, waited ? TXG_WAITED : TXG_NOWAIT);
	if (error) {
		if (zl != NULL)
			zfs_rename_unlock(&zl);
		zfs_dirent_unlock(sdl);
		zfs_dirent_unlock(tdl);

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		iput(ZTOI(szp));
		if (tzp)
			iput(ZTOI(tzp));
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		ZFS_EXIT(zsb);
		return (error);
	}

	if (tzp)	/* Attempt to remove the existing target */
		error = zfs_link_destroy(tdl, tzp, tx, zflg, NULL);

	if (error == 0) {
		error = zfs_link_create(tdl, szp, tx, ZRENAMING);
		if (error == 0) {
			szp->z_pflags |= ZFS_AV_MODIFIED;

			error = sa_update(szp->z_sa_hdl, SA_ZPL_FLAGS(zsb),
			    (void *)&szp->z_pflags, sizeof (uint64_t), tx);
			ASSERT0(error);

			error = zfs_link_destroy(sdl, szp, tx, ZRENAMING, NULL);
			if (error == 0) {
				zfs_log_rename(zilog, tx, TX_RENAME |
				    (flags & FIGNORECASE ? TX_CI : 0), sdzp,
				    sdl->dl_name, tdzp, tdl->dl_name, szp);
			} else {
				/*
				 * At this point, we have successfully created
				 * the target name, but have failed to remove
				 * the source name.  Since the create was done
				 * with the ZRENAMING flag, there are
				 * complications; for one, the link count is
				 * wrong.  The easiest way to deal with this
				 * is to remove the newly created target, and
				 * return the original error.  This must
				 * succeed; fortunately, it is very unlikely to
				 * fail, since we just created it.
				 */
				VERIFY3U(zfs_link_destroy(tdl, szp, tx,
				    ZRENAMING, NULL), ==, 0);
			}
		}
	}

	dmu_tx_commit(tx);
out:
	if (zl != NULL)
		zfs_rename_unlock(&zl);

	zfs_dirent_unlock(sdl);
	zfs_dirent_unlock(tdl);

	zfs_inode_update(sdzp);
	if (sdzp == tdzp)
		rw_exit(&sdzp->z_name_lock);

	if (sdzp != tdzp)
		zfs_inode_update(tdzp);

	zfs_inode_update(szp);
	iput(ZTOI(szp));
	if (tzp) {
		zfs_inode_update(tzp);
		iput(ZTOI(tzp));
	}

	if (zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_rename);

/*
 * Insert the indicated symbolic reference entry into the directory.
 *
 *	IN:	dip	- Directory to contain new symbolic link.
 *		link	- Name for new symlink entry.
 *		vap	- Attributes of new entry.
 *		target	- Target path of new symlink.
 *
 *		cr	- credentials of caller.
 *		flags	- case flags
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dip - ctime|mtime updated
 */
/*ARGSUSED*/
int
zfs_symlink(struct inode *dip, char *name, vattr_t *vap, char *link,
    struct inode **ipp, cred_t *cr, int flags)
{
	znode_t		*zp, *dzp = ITOZ(dip);
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	zfs_sb_t	*zsb = ITOZSB(dip);
	zilog_t		*zilog;
	uint64_t	len = strlen(link);
	int		error;
	int		zflg = ZNEW;
	zfs_acl_ids_t	acl_ids;
	boolean_t	fuid_dirtied;
	uint64_t	txtype = TX_SYMLINK;
	boolean_t	waited = B_FALSE;

	ASSERT(S_ISLNK(vap->va_mode));

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(dzp);
	zilog = zsb->z_log;

	if (zsb->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EILSEQ));
	}
	if (flags & FIGNORECASE)
		zflg |= ZCILOOK;

	if (len > MAXPATHLEN) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(ENAMETOOLONG));
	}

	if ((error = zfs_acl_ids_create(dzp, 0,
	    vap, cr, NULL, &acl_ids)) != 0) {
		ZFS_EXIT(zsb);
		return (error);
	}
top:
	*ipp = NULL;

	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg, NULL, NULL);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		ZFS_EXIT(zsb);
		return (error);
	}

	if ((error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zsb);
		return (error);
	}

	if (zfs_acl_ids_overquota(zsb, &acl_ids)) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zsb);
		return (SET_ERROR(EDQUOT));
	}
	tx = dmu_tx_create(zsb->z_os);
	fuid_dirtied = zsb->z_fuid_dirty;
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, MAX(1, len));
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE + len);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
	if (!zsb->z_use_sa && acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    acl_ids.z_aclp->z_acl_bytes);
	}
	if (fuid_dirtied)
		zfs_fuid_txhold(zsb, tx);
	error = dmu_tx_assign(tx, waited ? TXG_WAITED : TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		ZFS_EXIT(zsb);
		return (error);
	}

	/*
	 * Create a new object for the symlink.
	 * for version 4 ZPL datsets the symlink will be an SA attribute
	 */
	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zsb, tx);

	mutex_enter(&zp->z_lock);
	if (zp->z_is_sa)
		error = sa_update(zp->z_sa_hdl, SA_ZPL_SYMLINK(zsb),
		    link, len, tx);
	else
		zfs_sa_symlink(zp, link, len, tx);
	mutex_exit(&zp->z_lock);

	zp->z_size = len;
	(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zsb),
	    &zp->z_size, sizeof (zp->z_size), tx);
	/*
	 * Insert the new object into the directory.
	 */
	(void) zfs_link_create(dl, zp, tx, ZNEW);

	if (flags & FIGNORECASE)
		txtype |= TX_CI;
	zfs_log_symlink(zilog, tx, txtype, dzp, zp, name, link);

	zfs_inode_update(dzp);
	zfs_inode_update(zp);

	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	*ipp = ZTOI(zp);

	if (zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_symlink);

/*
 * Return, in the buffer contained in the provided uio structure,
 * the symbolic path referred to by ip.
 *
 *	IN:	ip	- inode of symbolic link
 *		uio	- structure to contain the link path.
 *		cr	- credentials of caller.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	ip - atime updated
 */
/* ARGSUSED */
int
zfs_readlink(struct inode *ip, uio_t *uio, cred_t *cr)
{
	znode_t		*zp = ITOZ(ip);
	zfs_sb_t	*zsb = ITOZSB(ip);
	int		error;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	mutex_enter(&zp->z_lock);
	if (zp->z_is_sa)
		error = sa_lookup_uio(zp->z_sa_hdl,
		    SA_ZPL_SYMLINK(zsb), uio);
	else
		error = zfs_sa_readlink(zp, uio);
	mutex_exit(&zp->z_lock);

	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_readlink);

/*
 * Insert a new entry into directory tdip referencing sip.
 *
 *	IN:	tdip	- Directory to contain new entry.
 *		sip	- inode of new entry.
 *		name	- name of new entry.
 *		cr	- credentials of caller.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	tdip - ctime|mtime updated
 *	 sip - ctime updated
 */
/* ARGSUSED */
int
zfs_link(struct inode *tdip, struct inode *sip, char *name, cred_t *cr)
{
	znode_t		*dzp = ITOZ(tdip);
	znode_t		*tzp, *szp;
	zfs_sb_t	*zsb = ITOZSB(tdip);
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	int		error;
	int		zf = ZNEW;
	uint64_t	parent;
	uid_t		owner;
	boolean_t	waited = B_FALSE;

	ASSERT(S_ISDIR(tdip->i_mode));

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(dzp);
	zilog = zsb->z_log;

	/*
	 * POSIX dictates that we return EPERM here.
	 * Better choices include ENOTSUP or EISDIR.
	 */
	if (S_ISDIR(sip->i_mode)) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EPERM));
	}

	if (sip->i_sb != tdip->i_sb || zfsctl_is_node(sip)) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EXDEV));
	}

	szp = ITOZ(sip);
	ZFS_VERIFY_ZP(szp);

	/* Prevent links to .zfs/shares files */

	if ((error = sa_lookup(szp->z_sa_hdl, SA_ZPL_PARENT(zsb),
	    &parent, sizeof (uint64_t))) != 0) {
		ZFS_EXIT(zsb);
		return (error);
	}
	if (parent == zsb->z_shares_dir) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EPERM));
	}

	if (zsb->z_utf8 && u8_validate(name,
	    strlen(name), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EILSEQ));
	}
#ifdef HAVE_PN_UTILS
	if (flags & FIGNORECASE)
		zf |= ZCILOOK;
#endif /* HAVE_PN_UTILS */

	/*
	 * We do not support links between attributes and non-attributes
	 * because of the potential security risk of creating links
	 * into "normal" file space in order to circumvent restrictions
	 * imposed in attribute space.
	 */
	if ((szp->z_pflags & ZFS_XATTR) != (dzp->z_pflags & ZFS_XATTR)) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EINVAL));
	}

	owner = zfs_fuid_map_id(zsb, szp->z_uid, cr, ZFS_OWNER);
	if (owner != crgetuid(cr) && secpolicy_basic_link(cr) != 0) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EPERM));
	}

	if ((error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr))) {
		ZFS_EXIT(zsb);
		return (error);
	}

top:
	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lock(&dl, dzp, name, &tzp, zf, NULL, NULL);
	if (error) {
		ZFS_EXIT(zsb);
		return (error);
	}

	tx = dmu_tx_create(zsb->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	zfs_sa_upgrade_txholds(tx, szp);
	zfs_sa_upgrade_txholds(tx, dzp);
	error = dmu_tx_assign(tx, waited ? TXG_WAITED : TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		ZFS_EXIT(zsb);
		return (error);
	}

	error = zfs_link_create(dl, szp, tx, 0);

	if (error == 0) {
		uint64_t txtype = TX_LINK;
#ifdef HAVE_PN_UTILS
		if (flags & FIGNORECASE)
			txtype |= TX_CI;
#endif /* HAVE_PN_UTILS */
		zfs_log_link(zilog, tx, txtype, dzp, szp, name);
	}

	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	if (zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	zfs_inode_update(dzp);
	zfs_inode_update(szp);
	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_link);

static void
zfs_putpage_commit_cb(void *arg)
{
	struct page *pp = arg;

	ClearPageError(pp);
	end_page_writeback(pp);
}

/*
 * Push a page out to disk, once the page is on stable storage the
 * registered commit callback will be run as notification of completion.
 *
 *	IN:	ip	- page mapped for inode.
 *		pp	- page to push (page is locked)
 *		wbc	- writeback control data
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	ip - ctime|mtime updated
 */
/* ARGSUSED */
int
zfs_putpage(struct inode *ip, struct page *pp, struct writeback_control *wbc)
{
	znode_t		*zp = ITOZ(ip);
	zfs_sb_t	*zsb = ITOZSB(ip);
	loff_t		offset;
	loff_t		pgoff;
	unsigned int	pglen;
	rl_t		*rl;
	dmu_tx_t	*tx;
	caddr_t		va;
	int		err = 0;
	uint64_t	mtime[2], ctime[2];
	sa_bulk_attr_t	bulk[3];
	int		cnt = 0;
	struct address_space *mapping;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	ASSERT(PageLocked(pp));

	pgoff = page_offset(pp);	/* Page byte-offset in file */
	offset = i_size_read(ip);	/* File length in bytes */
	pglen = MIN(PAGE_SIZE,		/* Page length in bytes */
	    P2ROUNDUP(offset, PAGE_SIZE)-pgoff);

	/* Page is beyond end of file */
	if (pgoff >= offset) {
		unlock_page(pp);
		ZFS_EXIT(zsb);
		return (0);
	}

	/* Truncate page length to end of file */
	if (pgoff + pglen > offset)
		pglen = offset - pgoff;

#if 0
	/*
	 * FIXME: Allow mmap writes past its quota.  The correct fix
	 * is to register a page_mkwrite() handler to count the page
	 * against its quota when it is about to be dirtied.
	 */
	if (zfs_owner_overquota(zsb, zp, B_FALSE) ||
	    zfs_owner_overquota(zsb, zp, B_TRUE)) {
		err = EDQUOT;
	}
#endif

	/*
	 * The ordering here is critical and must adhere to the following
	 * rules in order to avoid deadlocking in either zfs_read() or
	 * zfs_free_range() due to a lock inversion.
	 *
	 * 1) The page must be unlocked prior to acquiring the range lock.
	 *    This is critical because zfs_read() calls find_lock_page()
	 *    which may block on the page lock while holding the range lock.
	 *
	 * 2) Before setting or clearing write back on a page the range lock
	 *    must be held in order to prevent a lock inversion with the
	 *    zfs_free_range() function.
	 *
	 * This presents a problem because upon entering this function the
	 * page lock is already held.  To safely acquire the range lock the
	 * page lock must be dropped.  This creates a window where another
	 * process could truncate, invalidate, dirty, or write out the page.
	 *
	 * Therefore, after successfully reacquiring the range and page locks
	 * the current page state is checked.  In the common case everything
	 * will be as is expected and it can be written out.  However, if
	 * the page state has changed it must be handled accordingly.
	 */
	mapping = pp->mapping;
	redirty_page_for_writepage(wbc, pp);
	unlock_page(pp);

	rl = zfs_range_lock(&zp->z_range_lock, pgoff, pglen, RL_WRITER);
	lock_page(pp);

	/* Page mapping changed or it was no longer dirty, we're done */
	if (unlikely((mapping != pp->mapping) || !PageDirty(pp))) {
		unlock_page(pp);
		zfs_range_unlock(rl);
		ZFS_EXIT(zsb);
		return (0);
	}

	/* Another process started write block if required */
	if (PageWriteback(pp)) {
		unlock_page(pp);
		zfs_range_unlock(rl);

		if (wbc->sync_mode != WB_SYNC_NONE)
			wait_on_page_writeback(pp);

		ZFS_EXIT(zsb);
		return (0);
	}

	/* Clear the dirty flag the required locks are held */
	if (!clear_page_dirty_for_io(pp)) {
		unlock_page(pp);
		zfs_range_unlock(rl);
		ZFS_EXIT(zsb);
		return (0);
	}

	/*
	 * Counterpart for redirty_page_for_writepage() above.  This page
	 * was in fact not skipped and should not be counted as if it were.
	 */
	wbc->pages_skipped--;
	set_page_writeback(pp);
	unlock_page(pp);

	tx = dmu_tx_create(zsb->z_os);
	dmu_tx_hold_write(tx, zp->z_id, pgoff, pglen);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);

	err = dmu_tx_assign(tx, TXG_NOWAIT);
	if (err != 0) {
		if (err == ERESTART)
			dmu_tx_wait(tx);

		dmu_tx_abort(tx);
		__set_page_dirty_nobuffers(pp);
		ClearPageError(pp);
		end_page_writeback(pp);
		zfs_range_unlock(rl);
		ZFS_EXIT(zsb);
		return (err);
	}

	va = kmap(pp);
	ASSERT3U(pglen, <=, PAGE_SIZE);
	dmu_write(zsb->z_os, zp->z_id, pgoff, pglen, va, tx);
	kunmap(pp);

	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_MTIME(zsb), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_CTIME(zsb), NULL, &ctime, 16);
	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_FLAGS(zsb), NULL, &zp->z_pflags, 8);

	/* Preserve the mtime and ctime provided by the inode */
	ZFS_TIME_ENCODE(&ip->i_mtime, mtime);
	ZFS_TIME_ENCODE(&ip->i_ctime, ctime);
	zp->z_atime_dirty = 0;
	zp->z_seq++;

	err = sa_bulk_update(zp->z_sa_hdl, bulk, cnt, tx);

	zfs_log_write(zsb->z_log, tx, TX_WRITE, zp, pgoff, pglen, 0,
	    zfs_putpage_commit_cb, pp);
	dmu_tx_commit(tx);

	zfs_range_unlock(rl);

	if (wbc->sync_mode != WB_SYNC_NONE) {
		/*
		 * Note that this is rarely called under writepages(), because
		 * writepages() normally handles the entire commit for
		 * performance reasons.
		 */
		if (zsb->z_log != NULL)
			zil_commit(zsb->z_log, zp->z_id);
	}

	ZFS_EXIT(zsb);
	return (err);
}

/*
 * Update the system attributes when the inode has been dirtied.  For the
 * moment we only update the mode, atime, mtime, and ctime.
 */
int
zfs_dirty_inode(struct inode *ip, int flags)
{
	znode_t		*zp = ITOZ(ip);
	zfs_sb_t	*zsb = ITOZSB(ip);
	dmu_tx_t	*tx;
	uint64_t	mode, atime[2], mtime[2], ctime[2];
	sa_bulk_attr_t	bulk[4];
	int		error = 0;
	int		cnt = 0;

	if (zfs_is_readonly(zsb) || dmu_objset_is_snapshot(zsb->z_os))
		return (0);

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

#ifdef I_DIRTY_TIME
	/*
	 * This is the lazytime semantic indroduced in Linux 4.0
	 * This flag will only be called from update_time when lazytime is set.
	 * (Note, I_DIRTY_SYNC will also set if not lazytime)
	 * Fortunately mtime and ctime are managed within ZFS itself, so we
	 * only need to dirty atime.
	 */
	if (flags == I_DIRTY_TIME) {
		zp->z_atime_dirty = 1;
		goto out;
	}
#endif

	tx = dmu_tx_create(zsb->z_os);

	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);

	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		goto out;
	}

	mutex_enter(&zp->z_lock);
	zp->z_atime_dirty = 0;

	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_MODE(zsb), NULL, &mode, 8);
	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_ATIME(zsb), NULL, &atime, 16);
	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_MTIME(zsb), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_CTIME(zsb), NULL, &ctime, 16);

	/* Preserve the mode, mtime and ctime provided by the inode */
	ZFS_TIME_ENCODE(&ip->i_atime, atime);
	ZFS_TIME_ENCODE(&ip->i_mtime, mtime);
	ZFS_TIME_ENCODE(&ip->i_ctime, ctime);
	mode = ip->i_mode;

	zp->z_mode = mode;

	error = sa_bulk_update(zp->z_sa_hdl, bulk, cnt, tx);
	mutex_exit(&zp->z_lock);

	dmu_tx_commit(tx);
out:
	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_dirty_inode);

/*ARGSUSED*/
void
zfs_inactive(struct inode *ip)
{
	znode_t	*zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);
	uint64_t atime[2];
	int error;
	int need_unlock = 0;

	/* Only read lock if we haven't already write locked, e.g. rollback */
	if (!RW_WRITE_HELD(&zsb->z_teardown_inactive_lock)) {
		need_unlock = 1;
		rw_enter(&zsb->z_teardown_inactive_lock, RW_READER);
	}
	if (zp->z_sa_hdl == NULL) {
		if (need_unlock)
			rw_exit(&zsb->z_teardown_inactive_lock);
		return;
	}

	if (zp->z_atime_dirty && zp->z_unlinked == 0) {
		dmu_tx_t *tx = dmu_tx_create(zsb->z_os);

		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, zp);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
		} else {
			ZFS_TIME_ENCODE(&ip->i_atime, atime);
			mutex_enter(&zp->z_lock);
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_ATIME(zsb),
			    (void *)&atime, sizeof (atime), tx);
			zp->z_atime_dirty = 0;
			mutex_exit(&zp->z_lock);
			dmu_tx_commit(tx);
		}
	}

	zfs_zinactive(zp);
	if (need_unlock)
		rw_exit(&zsb->z_teardown_inactive_lock);
}
EXPORT_SYMBOL(zfs_inactive);

/*
 * Bounds-check the seek operation.
 *
 *	IN:	ip	- inode seeking within
 *		ooff	- old file offset
 *		noffp	- pointer to new file offset
 *		ct	- caller context
 *
 *	RETURN:	0 if success
 *		EINVAL if new offset invalid
 */
/* ARGSUSED */
int
zfs_seek(struct inode *ip, offset_t ooff, offset_t *noffp)
{
	if (S_ISDIR(ip->i_mode))
		return (0);
	return ((*noffp < 0 || *noffp > MAXOFFSET_T) ? EINVAL : 0);
}
EXPORT_SYMBOL(zfs_seek);

/*
 * Fill pages with data from the disk.
 */
static int
zfs_fillpage(struct inode *ip, struct page *pl[], int nr_pages)
{
	znode_t *zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);
	objset_t *os;
	struct page *cur_pp;
	u_offset_t io_off, total;
	size_t io_len;
	loff_t i_size;
	unsigned page_idx;
	int err;

	os = zsb->z_os;
	io_len = nr_pages << PAGE_SHIFT;
	i_size = i_size_read(ip);
	io_off = page_offset(pl[0]);

	if (io_off + io_len > i_size)
		io_len = i_size - io_off;

	/*
	 * Iterate over list of pages and read each page individually.
	 */
	page_idx = 0;
	for (total = io_off + io_len; io_off < total; io_off += PAGESIZE) {
		caddr_t va;

		cur_pp = pl[page_idx++];
		va = kmap(cur_pp);
		err = dmu_read(os, zp->z_id, io_off, PAGESIZE, va,
		    DMU_READ_PREFETCH);
		kunmap(cur_pp);
		if (err) {
			/* convert checksum errors into IO errors */
			if (err == ECKSUM)
				err = SET_ERROR(EIO);
			return (err);
		}
	}

	return (0);
}

/*
 * Uses zfs_fillpage to read data from the file and fill the pages.
 *
 *	IN:	ip	 - inode of file to get data from.
 *		pl	 - list of pages to read
 *		nr_pages - number of pages to read
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	vp - atime updated
 */
/* ARGSUSED */
int
zfs_getpage(struct inode *ip, struct page *pl[], int nr_pages)
{
	znode_t	 *zp  = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);
	int	 err;

	if (pl == NULL)
		return (0);

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	err = zfs_fillpage(ip, pl, nr_pages);

	ZFS_EXIT(zsb);
	return (err);
}
EXPORT_SYMBOL(zfs_getpage);

/*
 * Check ZFS specific permissions to memory map a section of a file.
 *
 *	IN:	ip	- inode of the file to mmap
 *		off	- file offset
 *		addrp	- start address in memory region
 *		len	- length of memory region
 *		vm_flags- address flags
 *
 *	RETURN:	0 if success
 *		error code if failure
 */
/*ARGSUSED*/
int
zfs_map(struct inode *ip, offset_t off, caddr_t *addrp, size_t len,
    unsigned long vm_flags)
{
	znode_t  *zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	if ((vm_flags & VM_WRITE) && (zp->z_pflags &
	    (ZFS_IMMUTABLE | ZFS_READONLY | ZFS_APPENDONLY))) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EPERM));
	}

	if ((vm_flags & (VM_READ | VM_EXEC)) &&
	    (zp->z_pflags & ZFS_AV_QUARANTINED)) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EACCES));
	}

	if (off < 0 || len > MAXOFFSET_T - off) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(ENXIO));
	}

	ZFS_EXIT(zsb);
	return (0);
}
EXPORT_SYMBOL(zfs_map);

/*
 * convoff - converts the given data (start, whence) to the
 * given whence.
 */
int
convoff(struct inode *ip, flock64_t *lckdat, int  whence, offset_t offset)
{
	vattr_t vap;
	int error;

	if ((lckdat->l_whence == 2) || (whence == 2)) {
		if ((error = zfs_getattr(ip, &vap, 0, CRED()) != 0))
			return (error);
	}

	switch (lckdat->l_whence) {
	case 1:
		lckdat->l_start += offset;
		break;
	case 2:
		lckdat->l_start += vap.va_size;
		/* FALLTHRU */
	case 0:
		break;
	default:
		return (SET_ERROR(EINVAL));
	}

	if (lckdat->l_start < 0)
		return (SET_ERROR(EINVAL));

	switch (whence) {
	case 1:
		lckdat->l_start -= offset;
		break;
	case 2:
		lckdat->l_start -= vap.va_size;
		/* FALLTHRU */
	case 0:
		break;
	default:
		return (SET_ERROR(EINVAL));
	}

	lckdat->l_whence = (short)whence;
	return (0);
}

/*
 * Free or allocate space in a file.  Currently, this function only
 * supports the `F_FREESP' command.  However, this command is somewhat
 * misnamed, as its functionality includes the ability to allocate as
 * well as free space.
 *
 *	IN:	ip	- inode of file to free data in.
 *		cmd	- action to take (only F_FREESP supported).
 *		bfp	- section of file to free/alloc.
 *		flag	- current file open mode flags.
 *		offset	- current file offset.
 *		cr	- credentials of caller [UNUSED].
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	ip - ctime|mtime updated
 */
/* ARGSUSED */
int
zfs_space(struct inode *ip, int cmd, flock64_t *bfp, int flag,
    offset_t offset, cred_t *cr)
{
	znode_t		*zp = ITOZ(ip);
	zfs_sb_t	*zsb = ITOZSB(ip);
	uint64_t	off, len;
	int		error;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	if (cmd != F_FREESP) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EINVAL));
	}

	if ((error = convoff(ip, bfp, 0, offset))) {
		ZFS_EXIT(zsb);
		return (error);
	}

	if (bfp->l_len < 0) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Permissions aren't checked on Solaris because on this OS
	 * zfs_space() can only be called with an opened file handle.
	 * On Linux we can get here through truncate_range() which
	 * operates directly on inodes, so we need to check access rights.
	 */
	if ((error = zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, cr))) {
		ZFS_EXIT(zsb);
		return (error);
	}

	off = bfp->l_start;
	len = bfp->l_len; /* 0 means from off to end of file */

	error = zfs_freesp(zp, off, len, flag, TRUE);

	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_space);

/*ARGSUSED*/
int
zfs_fid(struct inode *ip, fid_t *fidp)
{
	znode_t		*zp = ITOZ(ip);
	zfs_sb_t	*zsb = ITOZSB(ip);
	uint32_t	gen;
	uint64_t	gen64;
	uint64_t	object = zp->z_id;
	zfid_short_t	*zfid;
	int		size, i, error;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_GEN(zsb),
	    &gen64, sizeof (uint64_t))) != 0) {
		ZFS_EXIT(zsb);
		return (error);
	}

	gen = (uint32_t)gen64;

	size = (zsb->z_parent != zsb) ? LONG_FID_LEN : SHORT_FID_LEN;
	if (fidp->fid_len < size) {
		fidp->fid_len = size;
		ZFS_EXIT(zsb);
		return (SET_ERROR(ENOSPC));
	}

	zfid = (zfid_short_t *)fidp;

	zfid->zf_len = size;

	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	/* Must have a non-zero generation number to distinguish from .zfs */
	if (gen == 0)
		gen = 1;
	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = (uint8_t)(gen >> (8 * i));

	if (size == LONG_FID_LEN) {
		uint64_t	objsetid = dmu_objset_id(zsb->z_os);
		zfid_long_t	*zlfid;

		zlfid = (zfid_long_t *)fidp;

		for (i = 0; i < sizeof (zlfid->zf_setid); i++)
			zlfid->zf_setid[i] = (uint8_t)(objsetid >> (8 * i));

		/* XXX - this should be the generation number for the objset */
		for (i = 0; i < sizeof (zlfid->zf_setgen); i++)
			zlfid->zf_setgen[i] = 0;
	}

	ZFS_EXIT(zsb);
	return (0);
}
EXPORT_SYMBOL(zfs_fid);

/*ARGSUSED*/
int
zfs_getsecattr(struct inode *ip, vsecattr_t *vsecp, int flag, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);
	int error;
	boolean_t skipaclchk = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);
	error = zfs_getacl(zp, vsecp, skipaclchk, cr);
	ZFS_EXIT(zsb);

	return (error);
}
EXPORT_SYMBOL(zfs_getsecattr);

/*ARGSUSED*/
int
zfs_setsecattr(struct inode *ip, vsecattr_t *vsecp, int flag, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);
	int error;
	boolean_t skipaclchk = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	zilog_t	*zilog = zsb->z_log;

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);

	error = zfs_setacl(zp, vsecp, skipaclchk, cr);

	if (zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zsb);
	return (error);
}
EXPORT_SYMBOL(zfs_setsecattr);

#ifdef HAVE_UIO_ZEROCOPY
/*
 * Tunable, both must be a power of 2.
 *
 * zcr_blksz_min: the smallest read we may consider to loan out an arcbuf
 * zcr_blksz_max: if set to less than the file block size, allow loaning out of
 *		an arcbuf for a partial block read
 */
int zcr_blksz_min = (1 << 10);	/* 1K */
int zcr_blksz_max = (1 << 17);	/* 128K */

/*ARGSUSED*/
static int
zfs_reqzcbuf(struct inode *ip, enum uio_rw ioflag, xuio_t *xuio, cred_t *cr)
{
	znode_t	*zp = ITOZ(ip);
	zfs_sb_t *zsb = ITOZSB(ip);
	int max_blksz = zsb->z_max_blksz;
	uio_t *uio = &xuio->xu_uio;
	ssize_t size = uio->uio_resid;
	offset_t offset = uio->uio_loffset;
	int blksz;
	int fullblk, i;
	arc_buf_t *abuf;
	ssize_t maxsize;
	int preamble, postamble;

	if (xuio->xu_type != UIOTYPE_ZEROCOPY)
		return (SET_ERROR(EINVAL));

	ZFS_ENTER(zsb);
	ZFS_VERIFY_ZP(zp);
	switch (ioflag) {
	case UIO_WRITE:
		/*
		 * Loan out an arc_buf for write if write size is bigger than
		 * max_blksz, and the file's block size is also max_blksz.
		 */
		blksz = max_blksz;
		if (size < blksz || zp->z_blksz != blksz) {
			ZFS_EXIT(zsb);
			return (SET_ERROR(EINVAL));
		}
		/*
		 * Caller requests buffers for write before knowing where the
		 * write offset might be (e.g. NFS TCP write).
		 */
		if (offset == -1) {
			preamble = 0;
		} else {
			preamble = P2PHASE(offset, blksz);
			if (preamble) {
				preamble = blksz - preamble;
				size -= preamble;
			}
		}

		postamble = P2PHASE(size, blksz);
		size -= postamble;

		fullblk = size / blksz;
		(void) dmu_xuio_init(xuio,
		    (preamble != 0) + fullblk + (postamble != 0));

		/*
		 * Have to fix iov base/len for partial buffers.  They
		 * currently represent full arc_buf's.
		 */
		if (preamble) {
			/* data begins in the middle of the arc_buf */
			abuf = dmu_request_arcbuf(sa_get_db(zp->z_sa_hdl),
			    blksz);
			ASSERT(abuf);
			(void) dmu_xuio_add(xuio, abuf,
			    blksz - preamble, preamble);
		}

		for (i = 0; i < fullblk; i++) {
			abuf = dmu_request_arcbuf(sa_get_db(zp->z_sa_hdl),
			    blksz);
			ASSERT(abuf);
			(void) dmu_xuio_add(xuio, abuf, 0, blksz);
		}

		if (postamble) {
			/* data ends in the middle of the arc_buf */
			abuf = dmu_request_arcbuf(sa_get_db(zp->z_sa_hdl),
			    blksz);
			ASSERT(abuf);
			(void) dmu_xuio_add(xuio, abuf, 0, postamble);
		}
		break;
	case UIO_READ:
		/*
		 * Loan out an arc_buf for read if the read size is larger than
		 * the current file block size.  Block alignment is not
		 * considered.  Partial arc_buf will be loaned out for read.
		 */
		blksz = zp->z_blksz;
		if (blksz < zcr_blksz_min)
			blksz = zcr_blksz_min;
		if (blksz > zcr_blksz_max)
			blksz = zcr_blksz_max;
		/* avoid potential complexity of dealing with it */
		if (blksz > max_blksz) {
			ZFS_EXIT(zsb);
			return (SET_ERROR(EINVAL));
		}

		maxsize = zp->z_size - uio->uio_loffset;
		if (size > maxsize)
			size = maxsize;

		if (size < blksz) {
			ZFS_EXIT(zsb);
			return (SET_ERROR(EINVAL));
		}
		break;
	default:
		ZFS_EXIT(zsb);
		return (SET_ERROR(EINVAL));
	}

	uio->uio_extflg = UIO_XUIO;
	XUIO_XUZC_RW(xuio) = ioflag;
	ZFS_EXIT(zsb);
	return (0);
}

/*ARGSUSED*/
static int
zfs_retzcbuf(struct inode *ip, xuio_t *xuio, cred_t *cr)
{
	int i;
	arc_buf_t *abuf;
	int ioflag = XUIO_XUZC_RW(xuio);

	ASSERT(xuio->xu_type == UIOTYPE_ZEROCOPY);

	i = dmu_xuio_cnt(xuio);
	while (i-- > 0) {
		abuf = dmu_xuio_arcbuf(xuio, i);
		/*
		 * if abuf == NULL, it must be a write buffer
		 * that has been returned in zfs_write().
		 */
		if (abuf)
			dmu_return_arcbuf(abuf);
		ASSERT(abuf || ioflag == UIO_WRITE);
	}

	dmu_xuio_fini(xuio);
	return (0);
}
#endif /* HAVE_UIO_ZEROCOPY */

#if defined(_KERNEL) && defined(HAVE_SPL)
module_param(zfs_read_chunk_size, long, 0644);
MODULE_PARM_DESC(zfs_read_chunk_size, "Bytes to read per chunk");
#endif
