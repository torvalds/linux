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
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2017 Nexenta Systems, Inc.
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
#include <sys/vm.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/taskq.h>
#include <sys/uio.h>
#include <sys/atomic.h>
#include <sys/namei.h>
#include <sys/mman.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/zfs_dir.h>
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
#include <sys/filio.h>
#include <sys/sid.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_fuid.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_rlock.h>
#include <sys/extdirent.h>
#include <sys/kidmap.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/sched.h>
#include <sys/acl.h>
#include <sys/vmmeter.h>
#include <vm/vm_param.h>
#include <sys/zil.h>

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
 *  (1)	A check must be made in each zfs thread for a mounted file system.
 *	This is done avoiding races using ZFS_ENTER(zfsvfs).
 *	A ZFS_EXIT(zfsvfs) is needed before all returns.  Any znodes
 *	must be checked with ZFS_VERIFY_ZP(zp).  Both of these macros
 *	can return EIO from the calling function.
 *
 *  (2)	VN_RELE() should always be the last thing except for zil_commit()
 *	(if necessary) and ZFS_EXIT(). This is for 3 reasons:
 *	First, if it's the last reference, the vnode/znode
 *	can be freed, so the zp may point to freed memory.  Second, the last
 *	reference will call zfs_zinactive(), which may induce a lot of work --
 *	pushing cached pages (which acquires range locks) and syncing out
 *	cached atime changes.  Third, zfs_zinactive() may require a new tx,
 *	which could deadlock the system if you were already holding one.
 *	If you must call VN_RELE() within a tx then use VN_RELE_ASYNC().
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
 *	If dmu_tx_assign() returns ERESTART and zfsvfs->z_assign is TXG_NOWAIT,
 *	then drop all locks, call dmu_tx_wait(), and try again.  On subsequent
 *	calls to dmu_tx_assign(), pass TXG_NOTHROTTLE in addition to TXG_NOWAIT,
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
 *	ZFS_ENTER(zfsvfs);		// exit if unmounted
 * top:
 *	zfs_dirent_lookup(&dl, ...)	// lock directory entry (may VN_HOLD())
 *	rw_enter(...);			// grab any other locks you need
 *	tx = dmu_tx_create(...);	// get DMU tx
 *	dmu_tx_hold_*();		// hold each object you might modify
 *	error = dmu_tx_assign(tx, (waited ? TXG_NOTHROTTLE : 0) | TXG_NOWAIT);
 *	if (error) {
 *		rw_exit(...);		// drop locks
 *		zfs_dirent_unlock(dl);	// unlock directory entry
 *		VN_RELE(...);		// release held vnodes
 *		if (error == ERESTART) {
 *			waited = B_TRUE;
 *			dmu_tx_wait(tx);
 *			dmu_tx_abort(tx);
 *			goto top;
 *		}
 *		dmu_tx_abort(tx);	// abort DMU tx
 *		ZFS_EXIT(zfsvfs);	// finished in zfs
 *		return (error);		// really out of space
 *	}
 *	error = do_real_work();		// do whatever this VOP does
 *	if (error == 0)
 *		zfs_log_*(...);		// on success, make ZIL entry
 *	dmu_tx_commit(tx);		// commit DMU tx -- error or not
 *	rw_exit(...);			// drop locks
 *	zfs_dirent_unlock(dl);		// unlock directory entry
 *	VN_RELE(...);			// release held vnodes
 *	zil_commit(zilog, foid);	// synchronous when necessary
 *	ZFS_EXIT(zfsvfs);		// finished in zfs
 *	return (error);			// done, report error
 */

/* ARGSUSED */
static int
zfs_open(vnode_t **vpp, int flag, cred_t *cr, caller_context_t *ct)
{
	znode_t	*zp = VTOZ(*vpp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if ((flag & FWRITE) && (zp->z_pflags & ZFS_APPENDONLY) &&
	    ((flag & FAPPEND) == 0)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	if (!zfs_has_ctldir(zp) && zp->z_zfsvfs->z_vscan &&
	    ZTOV(zp)->v_type == VREG &&
	    !(zp->z_pflags & ZFS_AV_QUARANTINED) && zp->z_size > 0) {
		if (fs_vscan(*vpp, cr, 0) != 0) {
			ZFS_EXIT(zfsvfs);
			return (SET_ERROR(EACCES));
		}
	}

	/* Keep a count of the synchronous opens in the znode */
	if (flag & (FSYNC | FDSYNC))
		atomic_inc_32(&zp->z_sync_cnt);

	ZFS_EXIT(zfsvfs);
	return (0);
}

/* ARGSUSED */
static int
zfs_close(vnode_t *vp, int flag, int count, offset_t offset, cred_t *cr,
    caller_context_t *ct)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	/*
	 * Clean up any locks held by this process on the vp.
	 */
	cleanlocks(vp, ddi_get_pid(), 0);
	cleanshares(vp, ddi_get_pid());

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	/* Decrement the synchronous opens in the znode */
	if ((flag & (FSYNC | FDSYNC)) && (count == 1))
		atomic_dec_32(&zp->z_sync_cnt);

	if (!zfs_has_ctldir(zp) && zp->z_zfsvfs->z_vscan &&
	    ZTOV(zp)->v_type == VREG &&
	    !(zp->z_pflags & ZFS_AV_QUARANTINED) && zp->z_size > 0)
		VERIFY(fs_vscan(vp, cr, 1) == 0);

	ZFS_EXIT(zfsvfs);
	return (0);
}

/*
 * Lseek support for finding holes (cmd == _FIO_SEEK_HOLE) and
 * data (cmd == _FIO_SEEK_DATA). "off" is an in/out parameter.
 */
static int
zfs_holey(vnode_t *vp, u_long cmd, offset_t *off)
{
	znode_t	*zp = VTOZ(vp);
	uint64_t noff = (uint64_t)*off; /* new offset */
	uint64_t file_sz;
	int error;
	boolean_t hole;

	file_sz = zp->z_size;
	if (noff >= file_sz)  {
		return (SET_ERROR(ENXIO));
	}

	if (cmd == _FIO_SEEK_HOLE)
		hole = B_TRUE;
	else
		hole = B_FALSE;

	error = dmu_offset_next(zp->z_zfsvfs->z_os, zp->z_id, hole, &noff);

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

/* ARGSUSED */
static int
zfs_ioctl(vnode_t *vp, u_long com, intptr_t data, int flag, cred_t *cred,
    int *rvalp, caller_context_t *ct)
{
	offset_t off;
	offset_t ndata;
	dmu_object_info_t doi;
	int error;
	zfsvfs_t *zfsvfs;
	znode_t *zp;

	switch (com) {
	case _FIOFFS:
	{
		return (0);

		/*
		 * The following two ioctls are used by bfu.  Faking out,
		 * necessary to avoid bfu errors.
		 */
	}
	case _FIOGDIO:
	case _FIOSDIO:
	{
		return (0);
	}

	case _FIO_SEEK_DATA:
	case _FIO_SEEK_HOLE:
	{
#ifdef illumos
		if (ddi_copyin((void *)data, &off, sizeof (off), flag))
			return (SET_ERROR(EFAULT));
#else
		off = *(offset_t *)data;
#endif
		zp = VTOZ(vp);
		zfsvfs = zp->z_zfsvfs;
		ZFS_ENTER(zfsvfs);
		ZFS_VERIFY_ZP(zp);

		/* offset parameter is in/out */
		error = zfs_holey(vp, com, &off);
		ZFS_EXIT(zfsvfs);
		if (error)
			return (error);
#ifdef illumos
		if (ddi_copyout(&off, (void *)data, sizeof (off), flag))
			return (SET_ERROR(EFAULT));
#else
		*(offset_t *)data = off;
#endif
		return (0);
	}
#ifdef illumos
	case _FIO_COUNT_FILLED:
	{
		/*
		 * _FIO_COUNT_FILLED adds a new ioctl command which
		 * exposes the number of filled blocks in a
		 * ZFS object.
		 */
		zp = VTOZ(vp);
		zfsvfs = zp->z_zfsvfs;
		ZFS_ENTER(zfsvfs);
		ZFS_VERIFY_ZP(zp);

		/*
		 * Wait for all dirty blocks for this object
		 * to get synced out to disk, and the DMU info
		 * updated.
		 */
		error = dmu_object_wait_synced(zfsvfs->z_os, zp->z_id);
		if (error) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}

		/*
		 * Retrieve fill count from DMU object.
		 */
		error = dmu_object_info(zfsvfs->z_os, zp->z_id, &doi);
		if (error) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}

		ndata = doi.doi_fill_count;

		ZFS_EXIT(zfsvfs);
		if (ddi_copyout(&ndata, (void *)data, sizeof (ndata), flag))
			return (SET_ERROR(EFAULT));
		return (0);
	}
#endif
	}
	return (SET_ERROR(ENOTTY));
}

static vm_page_t
page_busy(vnode_t *vp, int64_t start, int64_t off, int64_t nbytes)
{
	vm_object_t obj;
	vm_page_t pp;
	int64_t end;

	/*
	 * At present vm_page_clear_dirty extends the cleared range to DEV_BSIZE
	 * aligned boundaries, if the range is not aligned.  As a result a
	 * DEV_BSIZE subrange with partially dirty data may get marked as clean.
	 * It may happen that all DEV_BSIZE subranges are marked clean and thus
	 * the whole page would be considred clean despite have some dirty data.
	 * For this reason we should shrink the range to DEV_BSIZE aligned
	 * boundaries before calling vm_page_clear_dirty.
	 */
	end = rounddown2(off + nbytes, DEV_BSIZE);
	off = roundup2(off, DEV_BSIZE);
	nbytes = end - off;

	obj = vp->v_object;
	zfs_vmobject_assert_wlocked(obj);

	for (;;) {
		if ((pp = vm_page_lookup(obj, OFF_TO_IDX(start))) != NULL &&
		    pp->valid) {
			if (vm_page_xbusied(pp)) {
				/*
				 * Reference the page before unlocking and
				 * sleeping so that the page daemon is less
				 * likely to reclaim it.
				 */
				vm_page_reference(pp);
				vm_page_lock(pp);
				zfs_vmobject_wunlock(obj);
				vm_page_busy_sleep(pp, "zfsmwb", true);
				zfs_vmobject_wlock(obj);
				continue;
			}
			vm_page_sbusy(pp);
		} else if (pp != NULL) {
			ASSERT(!pp->valid);
			pp = NULL;
		}

		if (pp != NULL) {
			ASSERT3U(pp->valid, ==, VM_PAGE_BITS_ALL);
			vm_object_pip_add(obj, 1);
			pmap_remove_write(pp);
			if (nbytes != 0)
				vm_page_clear_dirty(pp, off, nbytes);
		}
		break;
	}
	return (pp);
}

static void
page_unbusy(vm_page_t pp)
{

	vm_page_sunbusy(pp);
	vm_object_pip_subtract(pp->object, 1);
}

static vm_page_t
page_hold(vnode_t *vp, int64_t start)
{
	vm_object_t obj;
	vm_page_t pp;

	obj = vp->v_object;
	zfs_vmobject_assert_wlocked(obj);

	for (;;) {
		if ((pp = vm_page_lookup(obj, OFF_TO_IDX(start))) != NULL &&
		    pp->valid) {
			if (vm_page_xbusied(pp)) {
				/*
				 * Reference the page before unlocking and
				 * sleeping so that the page daemon is less
				 * likely to reclaim it.
				 */
				vm_page_reference(pp);
				vm_page_lock(pp);
				zfs_vmobject_wunlock(obj);
				vm_page_busy_sleep(pp, "zfsmwb", true);
				zfs_vmobject_wlock(obj);
				continue;
			}

			ASSERT3U(pp->valid, ==, VM_PAGE_BITS_ALL);
			vm_page_lock(pp);
			vm_page_hold(pp);
			vm_page_unlock(pp);

		} else
			pp = NULL;
		break;
	}
	return (pp);
}

static void
page_unhold(vm_page_t pp)
{

	vm_page_lock(pp);
	vm_page_unhold(pp);
	vm_page_unlock(pp);
}

/*
 * When a file is memory mapped, we must keep the IO data synchronized
 * between the DMU cache and the memory mapped pages.  What this means:
 *
 * On Write:	If we find a memory mapped page, we write to *both*
 *		the page and the dmu buffer.
 */
static void
update_pages(vnode_t *vp, int64_t start, int len, objset_t *os, uint64_t oid,
    int segflg, dmu_tx_t *tx)
{
	vm_object_t obj;
	struct sf_buf *sf;
	caddr_t va;
	int off;

	ASSERT(segflg != UIO_NOCOPY);
	ASSERT(vp->v_mount != NULL);
	obj = vp->v_object;
	ASSERT(obj != NULL);

	off = start & PAGEOFFSET;
	zfs_vmobject_wlock(obj);
	for (start &= PAGEMASK; len > 0; start += PAGESIZE) {
		vm_page_t pp;
		int nbytes = imin(PAGESIZE - off, len);

		if ((pp = page_busy(vp, start, off, nbytes)) != NULL) {
			zfs_vmobject_wunlock(obj);

			va = zfs_map_page(pp, &sf);
			(void) dmu_read(os, oid, start+off, nbytes,
			    va+off, DMU_READ_PREFETCH);;
			zfs_unmap_page(sf);

			zfs_vmobject_wlock(obj);
			page_unbusy(pp);
		}
		len -= nbytes;
		off = 0;
	}
	vm_object_pip_wakeupn(obj, 0);
	zfs_vmobject_wunlock(obj);
}

/*
 * Read with UIO_NOCOPY flag means that sendfile(2) requests
 * ZFS to populate a range of page cache pages with data.
 *
 * NOTE: this function could be optimized to pre-allocate
 * all pages in advance, drain exclusive busy on all of them,
 * map them into contiguous KVA region and populate them
 * in one single dmu_read() call.
 */
static int
mappedread_sf(vnode_t *vp, int nbytes, uio_t *uio)
{
	znode_t *zp = VTOZ(vp);
	objset_t *os = zp->z_zfsvfs->z_os;
	struct sf_buf *sf;
	vm_object_t obj;
	vm_page_t pp;
	int64_t start;
	caddr_t va;
	int len = nbytes;
	int off;
	int error = 0;

	ASSERT(uio->uio_segflg == UIO_NOCOPY);
	ASSERT(vp->v_mount != NULL);
	obj = vp->v_object;
	ASSERT(obj != NULL);
	ASSERT((uio->uio_loffset & PAGEOFFSET) == 0);

	zfs_vmobject_wlock(obj);
	for (start = uio->uio_loffset; len > 0; start += PAGESIZE) {
		int bytes = MIN(PAGESIZE, len);

		pp = vm_page_grab(obj, OFF_TO_IDX(start), VM_ALLOC_SBUSY |
		    VM_ALLOC_NORMAL | VM_ALLOC_IGN_SBUSY);
		if (pp->valid == 0) {
			zfs_vmobject_wunlock(obj);
			va = zfs_map_page(pp, &sf);
			error = dmu_read(os, zp->z_id, start, bytes, va,
			    DMU_READ_PREFETCH);
			if (bytes != PAGESIZE && error == 0)
				bzero(va + bytes, PAGESIZE - bytes);
			zfs_unmap_page(sf);
			zfs_vmobject_wlock(obj);
			vm_page_sunbusy(pp);
			vm_page_lock(pp);
			if (error) {
				if (pp->wire_count == 0 && pp->valid == 0 &&
				    !vm_page_busied(pp))
					vm_page_free(pp);
			} else {
				pp->valid = VM_PAGE_BITS_ALL;
				vm_page_activate(pp);
			}
			vm_page_unlock(pp);
		} else {
			ASSERT3U(pp->valid, ==, VM_PAGE_BITS_ALL);
			vm_page_sunbusy(pp);
		}
		if (error)
			break;
		uio->uio_resid -= bytes;
		uio->uio_offset += bytes;
		len -= bytes;
	}
	zfs_vmobject_wunlock(obj);
	return (error);
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
mappedread(vnode_t *vp, int nbytes, uio_t *uio)
{
	znode_t *zp = VTOZ(vp);
	vm_object_t obj;
	int64_t start;
	caddr_t va;
	int len = nbytes;
	int off;
	int error = 0;

	ASSERT(vp->v_mount != NULL);
	obj = vp->v_object;
	ASSERT(obj != NULL);

	start = uio->uio_loffset;
	off = start & PAGEOFFSET;
	zfs_vmobject_wlock(obj);
	for (start &= PAGEMASK; len > 0; start += PAGESIZE) {
		vm_page_t pp;
		uint64_t bytes = MIN(PAGESIZE - off, len);

		if (pp = page_hold(vp, start)) {
			struct sf_buf *sf;
			caddr_t va;

			zfs_vmobject_wunlock(obj);
			va = zfs_map_page(pp, &sf);
#ifdef illumos
			error = uiomove(va + off, bytes, UIO_READ, uio);
#else
			error = vn_io_fault_uiomove(va + off, bytes, uio);
#endif
			zfs_unmap_page(sf);
			zfs_vmobject_wlock(obj);
			page_unhold(pp);
		} else {
			zfs_vmobject_wunlock(obj);
			error = dmu_read_uio_dbuf(sa_get_db(zp->z_sa_hdl),
			    uio, bytes);
			zfs_vmobject_wlock(obj);
		}
		len -= bytes;
		off = 0;
		if (error)
			break;
	}
	zfs_vmobject_wunlock(obj);
	return (error);
}

offset_t zfs_read_chunk_size = 1024 * 1024; /* Tunable */

/*
 * Read bytes from specified file into supplied buffer.
 *
 *	IN:	vp	- vnode of file to be read from.
 *		uio	- structure supplying read location, range info,
 *			  and return buffer.
 *		ioflag	- SYNC flags; used to provide FRSYNC semantics.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	OUT:	uio	- updated offset and range, buffer filled.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Side Effects:
 *	vp - atime updated if byte count > 0
 */
/* ARGSUSED */
static int
zfs_read(vnode_t *vp, uio_t *uio, int ioflag, cred_t *cr, caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	ssize_t		n, nbytes;
	int		error = 0;
	rl_t		*rl;
	xuio_t		*xuio = NULL;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (zp->z_pflags & ZFS_AV_QUARANTINED) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EACCES));
	}

	/*
	 * Validate file offset
	 */
	if (uio->uio_loffset < (offset_t)0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Fasttrack empty reads
	 */
	if (uio->uio_resid == 0) {
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	/*
	 * Check for mandatory locks
	 */
	if (MANDMODE(zp->z_mode)) {
		if (error = chklock(vp, FREAD,
		    uio->uio_loffset, uio->uio_resid, uio->uio_fmode, ct)) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}

	/*
	 * If we're in FRSYNC mode, sync out this znode before reading it.
	 */
	if (zfsvfs->z_log &&
	    (ioflag & FRSYNC || zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS))
		zil_commit(zfsvfs->z_log, zp->z_id);

	/*
	 * Lock the range against changes.
	 */
	rl = zfs_range_lock(zp, uio->uio_loffset, uio->uio_resid, RL_READER);

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

#ifdef illumos
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

		if (vn_has_cached_data(vp)) {
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
#endif	/* illumos */

	while (n > 0) {
		nbytes = MIN(n, zfs_read_chunk_size -
		    P2PHASE(uio->uio_loffset, zfs_read_chunk_size));

#ifdef __FreeBSD__
		if (uio->uio_segflg == UIO_NOCOPY)
			error = mappedread_sf(vp, nbytes, uio);
		else
#endif /* __FreeBSD__ */
		if (vn_has_cached_data(vp)) {
			error = mappedread(vp, nbytes, uio);
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

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);
	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Write the bytes to a file.
 *
 *	IN:	vp	- vnode of file to be written to.
 *		uio	- structure supplying write location, range info,
 *			  and data buffer.
 *		ioflag	- FAPPEND, FSYNC, and/or FDSYNC.  FAPPEND is
 *			  set if in append mode.
 *		cr	- credentials of caller.
 *		ct	- caller context (NFS/CIFS fem monitor only)
 *
 *	OUT:	uio	- updated offset and range.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	vp - ctime|mtime updated if byte count > 0
 */

/* ARGSUSED */
static int
zfs_write(vnode_t *vp, uio_t *uio, int ioflag, cred_t *cr, caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	rlim64_t	limit = MAXOFFSET_T;
	ssize_t		start_resid = uio->uio_resid;
	ssize_t		tx_bytes;
	uint64_t	end_size;
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	zilog_t		*zilog;
	offset_t	woff;
	ssize_t		n, nbytes;
	rl_t		*rl;
	int		max_blksz = zfsvfs->z_max_blksz;
	int		error = 0;
	arc_buf_t	*abuf;
	iovec_t		*aiov = NULL;
	xuio_t		*xuio = NULL;
	int		i_iov = 0;
	int		iovcnt = uio->uio_iovcnt;
	iovec_t		*iovp = uio->uio_iov;
	int		write_eof;
	int		count = 0;
	sa_bulk_attr_t	bulk[4];
	uint64_t	mtime[2], ctime[2];

	/*
	 * Fasttrack empty write
	 */
	n = start_resid;
	if (n == 0)
		return (0);

	if (limit == RLIM64_INFINITY || limit > MAXOFFSET_T)
		limit = MAXOFFSET_T;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs), NULL,
	    &zp->z_size, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, 8);

	/*
	 * In a case vp->v_vfsp != zp->z_zfsvfs->z_vfs (e.g. snapshots) our
	 * callers might not be able to detect properly that we are read-only,
	 * so check it explicitly here.
	 */
	if (zfsvfs->z_vfs->vfs_flag & VFS_RDONLY) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EROFS));
	}

	/*
	 * If immutable or not appending then return EPERM.
	 * Intentionally allow ZFS_READONLY through here.
	 * See zfs_zaccess_common()
	 */
	if ((zp->z_pflags & ZFS_IMMUTABLE) ||
	    ((zp->z_pflags & ZFS_APPENDONLY) && !(ioflag & FAPPEND) &&
	    (uio->uio_loffset < zp->z_size))) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	zilog = zfsvfs->z_log;

	/*
	 * Validate file offset
	 */
	woff = ioflag & FAPPEND ? zp->z_size : uio->uio_loffset;
	if (woff < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Check for mandatory locks before calling zfs_range_lock()
	 * in order to prevent a deadlock with locks set via fcntl().
	 */
	if (MANDMODE((mode_t)zp->z_mode) &&
	    (error = chklock(vp, FWRITE, woff, n, uio->uio_fmode, ct)) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

#ifdef illumos
	/*
	 * Pre-fault the pages to ensure slow (eg NFS) pages
	 * don't hold up txg.
	 * Skip this if uio contains loaned arc_buf.
	 */
	if ((uio->uio_extflg == UIO_XUIO) &&
	    (((xuio_t *)uio)->xu_type == UIOTYPE_ZEROCOPY))
		xuio = (xuio_t *)uio;
	else
		uio_prefaultpages(MIN(n, max_blksz), uio);
#endif

	/*
	 * If in append mode, set the io offset pointer to eof.
	 */
	if (ioflag & FAPPEND) {
		/*
		 * Obtain an appending range lock to guarantee file append
		 * semantics.  We reset the write offset once we have the lock.
		 */
		rl = zfs_range_lock(zp, 0, n, RL_APPEND);
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
		rl = zfs_range_lock(zp, woff, n, RL_WRITER);
	}

	if (vn_rlimit_fsize(vp, uio, uio->uio_td)) {
		zfs_range_unlock(rl);
		ZFS_EXIT(zfsvfs);
		return (EFBIG);
	}

	if (woff >= limit) {
		zfs_range_unlock(rl);
		ZFS_EXIT(zfsvfs);
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
		if (zfs_owner_overquota(zfsvfs, zp, B_FALSE) ||
		    zfs_owner_overquota(zfsvfs, zp, B_TRUE)) {
			if (abuf != NULL)
				dmu_return_arcbuf(abuf);
			error = SET_ERROR(EDQUOT);
			break;
		}

		if (xuio && abuf == NULL) {
			ASSERT(i_iov < iovcnt);
			aiov = &iovp[i_iov];
			abuf = dmu_xuio_arcbuf(xuio, i_iov);
			dmu_xuio_clear(xuio, i_iov);
			DTRACE_PROBE3(zfs_cp_write, int, i_iov,
			    iovec_t *, aiov, arc_buf_t *, abuf);
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
			if (error = uiocopy(abuf->b_data, max_blksz,
			    UIO_WRITE, uio, &cbytes)) {
				dmu_return_arcbuf(abuf);
				break;
			}
			ASSERT(cbytes == max_blksz);
		}

		/*
		 * Start a transaction.
		 */
		tx = dmu_tx_create(zfsvfs->z_os);
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

		if (woff + nbytes > zp->z_size)
			vnode_pager_setsize(vp, woff + nbytes);

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
				dmu_write(zfsvfs->z_os, zp->z_id, woff,
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
		if (tx_bytes && vn_has_cached_data(vp)) {
			update_pages(vp, woff, tx_bytes, zfsvfs->z_os,
			    zp->z_id, uio->uio_segflg, tx);
		}

		/*
		 * If we made no progress, we're done.  If we made even
		 * partial progress, update the znode and ZIL accordingly.
		 */
		if (tx_bytes == 0) {
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
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
		    secpolicy_vnode_setid_retain(vp, cr,
		    (zp->z_mode & S_ISUID) != 0 && zp->z_uid == 0) != 0) {
			uint64_t newmode;
			zp->z_mode &= ~(S_ISUID | S_ISGID);
			newmode = zp->z_mode;
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_MODE(zfsvfs),
			    (void *)&newmode, sizeof (uint64_t), tx);
		}
		mutex_exit(&zp->z_acl_lock);

		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime,
		    B_TRUE);

		/*
		 * Update the file size (zp_size) if it has changed;
		 * account for possible concurrent updates.
		 */
		while ((end_size = zp->z_size) < uio->uio_loffset) {
			(void) atomic_cas_64(&zp->z_size, end_size,
			    uio->uio_loffset);
#ifdef illumos
			ASSERT(error == 0);
#else
			ASSERT(error == 0 || error == EFAULT);
#endif
		}
		/*
		 * If we are replaying and eof is non zero then force
		 * the file size to the specified eof. Note, there's no
		 * concurrency during replay.
		 */
		if (zfsvfs->z_replay && zfsvfs->z_replay_eof != 0)
			zp->z_size = zfsvfs->z_replay_eof;

		if (error == 0)
			error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		else
			(void) sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);

		zfs_log_write(zilog, tx, TX_WRITE, zp, woff, tx_bytes, ioflag);
		dmu_tx_commit(tx);

		if (error != 0)
			break;
		ASSERT(tx_bytes == nbytes);
		n -= nbytes;

#ifdef illumos
		if (!xuio && n > 0)
			uio_prefaultpages(MIN(n, max_blksz), uio);
#endif
	}

	zfs_range_unlock(rl);

	/*
	 * If we're in replay mode, or we made no progress, return error.
	 * Otherwise, it's at least a partial write, so it's successful.
	 */
	if (zfsvfs->z_replay || uio->uio_resid == start_resid) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

#ifdef __FreeBSD__
	/*
	 * EFAULT means that at least one page of the source buffer was not
	 * available.  VFS will re-try remaining I/O upon this error.
	 */
	if (error == EFAULT) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}
#endif

	if (ioflag & (FSYNC | FDSYNC) ||
	    zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, zp->z_id);

	ZFS_EXIT(zfsvfs);
	return (0);
}

void
zfs_get_done(zgd_t *zgd, int error)
{
	znode_t *zp = zgd->zgd_private;
	objset_t *os = zp->z_zfsvfs->z_os;

	if (zgd->zgd_db)
		dmu_buf_rele(zgd->zgd_db, zgd);

	zfs_range_unlock(zgd->zgd_rl);

	/*
	 * Release the vnode asynchronously as we currently have the
	 * txg stopped from syncing.
	 */
	VN_RELE_ASYNC(ZTOV(zp), dsl_pool_vnrele_taskq(dmu_objset_pool(os)));

	if (error == 0 && zgd->zgd_bp)
		zil_lwb_add_block(zgd->zgd_lwb, zgd->zgd_bp);

	kmem_free(zgd, sizeof (zgd_t));
}

#ifdef DEBUG
static int zil_fault_io = 0;
#endif

/*
 * Get data to generate a TX_WRITE intent log record.
 */
int
zfs_get_data(void *arg, lr_write_t *lr, char *buf, struct lwb *lwb, zio_t *zio)
{
	zfsvfs_t *zfsvfs = arg;
	objset_t *os = zfsvfs->z_os;
	znode_t *zp;
	uint64_t object = lr->lr_foid;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;
	dmu_buf_t *db;
	zgd_t *zgd;
	int error = 0;

	ASSERT3P(lwb, !=, NULL);
	ASSERT3P(zio, !=, NULL);
	ASSERT3U(size, !=, 0);

	/*
	 * Nothing to do if the file has been removed
	 */
	if (zfs_zget(zfsvfs, object, &zp) != 0)
		return (SET_ERROR(ENOENT));
	if (zp->z_unlinked) {
		/*
		 * Release the vnode asynchronously as we currently have the
		 * txg stopped from syncing.
		 */
		VN_RELE_ASYNC(ZTOV(zp),
		    dsl_pool_vnrele_taskq(dmu_objset_pool(os)));
		return (SET_ERROR(ENOENT));
	}

	zgd = (zgd_t *)kmem_zalloc(sizeof (zgd_t), KM_SLEEP);
	zgd->zgd_lwb = lwb;
	zgd->zgd_private = zp;

	/*
	 * Write records come in two flavors: immediate and indirect.
	 * For small writes it's cheaper to store the data with the
	 * log record (immediate); for large writes it's cheaper to
	 * sync the data and get a pointer to it (indirect) so that
	 * we don't have to write the data twice.
	 */
	if (buf != NULL) { /* immediate write */
		zgd->zgd_rl = zfs_range_lock(zp, offset, size, RL_READER);
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
		 * written out and its checksum is being calculated
		 * that no one can change the data. We need to re-check
		 * blocksize after we get the lock in case it's changed!
		 */
		for (;;) {
			uint64_t blkoff;
			size = zp->z_blksz;
			blkoff = ISP2(size) ? P2PHASE(offset, size) : offset;
			offset -= blkoff;
			zgd->zgd_rl = zfs_range_lock(zp, offset, size,
			    RL_READER);
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
			blkptr_t *bp = &lr->lr_blkptr;

			zgd->zgd_db = db;
			zgd->zgd_bp = bp;

			ASSERT(db->db_offset == offset);
			ASSERT(db->db_size == size);

			error = dmu_sync(zio, lr->lr_common.lrc_txg,
			    zfs_get_done, zgd);
			ASSERT(error || lr->lr_length <= size);

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
				/*
				 * TX_WRITE2 relies on the data previously
				 * written by the TX_WRITE that caused
				 * EALREADY.  We zero out the BP because
				 * it is the old, currently-on-disk BP,
				 * so there's no need to zio_flush() its
				 * vdevs (flushing would needlesly hurt
				 * performance, and doesn't work on
				 * indirect vdevs).
				 */
				zgd->zgd_bp = NULL;
				BP_ZERO(bp);
				error = 0;
			}
		}
	}

	zfs_get_done(zgd, error);

	return (error);
}

/*ARGSUSED*/
static int
zfs_access(vnode_t *vp, int mode, int flag, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (flag & V_ACE_MASK)
		error = zfs_zaccess(zp, mode, flag, B_FALSE, cr);
	else
		error = zfs_zaccess_rwx(zp, mode, flag, cr);

	ZFS_EXIT(zfsvfs);
	return (error);
}

static int
zfs_dd_callback(struct mount *mp, void *arg, int lkflags, struct vnode **vpp)
{
	int error;

	*vpp = arg;
	error = vn_lock(*vpp, lkflags);
	if (error != 0)
		vrele(*vpp);
	return (error);
}

static int
zfs_lookup_lock(vnode_t *dvp, vnode_t *vp, const char *name, int lkflags)
{
	znode_t *zdp = VTOZ(dvp);
	zfsvfs_t *zfsvfs = zdp->z_zfsvfs;
	int error;
	int ltype;

	ASSERT_VOP_LOCKED(dvp, __func__);
#ifdef DIAGNOSTIC
	if ((zdp->z_pflags & ZFS_XATTR) == 0)
		VERIFY(!RRM_LOCK_HELD(&zfsvfs->z_teardown_lock));
#endif

	if (name[0] == 0 || (name[0] == '.' && name[1] == 0)) {
		ASSERT3P(dvp, ==, vp);
		vref(dvp);
		ltype = lkflags & LK_TYPE_MASK;
		if (ltype != VOP_ISLOCKED(dvp)) {
			if (ltype == LK_EXCLUSIVE)
				vn_lock(dvp, LK_UPGRADE | LK_RETRY);
			else /* if (ltype == LK_SHARED) */
				vn_lock(dvp, LK_DOWNGRADE | LK_RETRY);

			/*
			 * Relock for the "." case could leave us with
			 * reclaimed vnode.
			 */
			if (dvp->v_iflag & VI_DOOMED) {
				vrele(dvp);
				return (SET_ERROR(ENOENT));
			}
		}
		return (0);
	} else if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
		/*
		 * Note that in this case, dvp is the child vnode, and we
		 * are looking up the parent vnode - exactly reverse from
		 * normal operation.  Unlocking dvp requires some rather
		 * tricky unlock/relock dance to prevent mp from being freed;
		 * use vn_vget_ino_gen() which takes care of all that.
		 *
		 * XXX Note that there is a time window when both vnodes are
		 * unlocked.  It is possible, although highly unlikely, that
		 * during that window the parent-child relationship between
		 * the vnodes may change, for example, get reversed.
		 * In that case we would have a wrong lock order for the vnodes.
		 * All other filesystems seem to ignore this problem, so we
		 * do the same here.
		 * A potential solution could be implemented as follows:
		 * - using LK_NOWAIT when locking the second vnode and retrying
		 *   if necessary
		 * - checking that the parent-child relationship still holds
		 *   after locking both vnodes and retrying if it doesn't
		 */
		error = vn_vget_ino_gen(dvp, zfs_dd_callback, vp, lkflags, &vp);
		return (error);
	} else {
		error = vn_lock(vp, lkflags);
		if (error != 0)
			vrele(vp);
		return (error);
	}
}

/*
 * Lookup an entry in a directory, or an extended attribute directory.
 * If it exists, return a held vnode reference for it.
 *
 *	IN:	dvp	- vnode of directory to search.
 *		nm	- name of entry to lookup.
 *		pnp	- full pathname to lookup [UNUSED].
 *		flags	- LOOKUP_XATTR set if looking for an attribute.
 *		rdir	- root directory vnode [UNUSED].
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	OUT:	vpp	- vnode of located entry, NULL if not found.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	NA
 */
/* ARGSUSED */
static int
zfs_lookup(vnode_t *dvp, char *nm, vnode_t **vpp, struct componentname *cnp,
    int nameiop, cred_t *cr, kthread_t *td, int flags)
{
	znode_t *zdp = VTOZ(dvp);
	znode_t *zp;
	zfsvfs_t *zfsvfs = zdp->z_zfsvfs;
	int	error = 0;

	/*
	 * Fast path lookup, however we must skip DNLC lookup
	 * for case folding or normalizing lookups because the
	 * DNLC code only stores the passed in name.  This means
	 * creating 'a' and removing 'A' on a case insensitive
	 * file system would work, but DNLC still thinks 'a'
	 * exists and won't let you create it again on the next
	 * pass through fast path.
	 */
	if (!(flags & LOOKUP_XATTR)) {
		if (dvp->v_type != VDIR) {
			return (SET_ERROR(ENOTDIR));
		} else if (zdp->z_sa_hdl == NULL) {
			return (SET_ERROR(EIO));
		}
	}

	DTRACE_PROBE2(zfs__fastpath__lookup__miss, vnode_t *, dvp, char *, nm);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zdp);

	*vpp = NULL;

	if (flags & LOOKUP_XATTR) {
#ifdef TODO
		/*
		 * If the xattr property is off, refuse the lookup request.
		 */
		if (!(zfsvfs->z_vfs->vfs_flag & VFS_XATTR)) {
			ZFS_EXIT(zfsvfs);
			return (SET_ERROR(EINVAL));
		}
#endif

		/*
		 * We don't allow recursive attributes..
		 * Maybe someday we will.
		 */
		if (zdp->z_pflags & ZFS_XATTR) {
			ZFS_EXIT(zfsvfs);
			return (SET_ERROR(EINVAL));
		}

		if (error = zfs_get_xattrdir(VTOZ(dvp), vpp, cr, flags)) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}

		/*
		 * Do we have permission to get into attribute directory?
		 */
		if (error = zfs_zaccess(VTOZ(*vpp), ACE_EXECUTE, 0,
		    B_FALSE, cr)) {
			vrele(*vpp);
			*vpp = NULL;
		}

		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Check accessibility of directory.
	 */
	if (error = zfs_zaccess(zdp, ACE_EXECUTE, 0, B_FALSE, cr)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (zfsvfs->z_utf8 && u8_validate(nm, strlen(nm),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EILSEQ));
	}


	/*
	 * First handle the special cases.
	 */
	if ((cnp->cn_flags & ISDOTDOT) != 0) {
		/*
		 * If we are a snapshot mounted under .zfs, return
		 * the vp for the snapshot directory.
		 */
		if (zdp->z_id == zfsvfs->z_root && zfsvfs->z_parent != zfsvfs) {
			struct componentname cn;
			vnode_t *zfsctl_vp;
			int ltype;

			ZFS_EXIT(zfsvfs);
			ltype = VOP_ISLOCKED(dvp);
			VOP_UNLOCK(dvp, 0);
			error = zfsctl_root(zfsvfs->z_parent, LK_SHARED,
			    &zfsctl_vp);
			if (error == 0) {
				cn.cn_nameptr = "snapshot";
				cn.cn_namelen = strlen(cn.cn_nameptr);
				cn.cn_nameiop = cnp->cn_nameiop;
				cn.cn_flags = cnp->cn_flags & ~ISDOTDOT;
				cn.cn_lkflags = cnp->cn_lkflags;
				error = VOP_LOOKUP(zfsctl_vp, vpp, &cn);
				vput(zfsctl_vp);
			}
			vn_lock(dvp, ltype | LK_RETRY);
			return (error);
		}
	}
	if (zfs_has_ctldir(zdp) && strcmp(nm, ZFS_CTLDIR_NAME) == 0) {
		ZFS_EXIT(zfsvfs);
		if ((cnp->cn_flags & ISLASTCN) != 0 && nameiop != LOOKUP)
			return (SET_ERROR(ENOTSUP));
		error = zfsctl_root(zfsvfs, cnp->cn_lkflags, vpp);
		return (error);
	}

	/*
	 * The loop is retry the lookup if the parent-child relationship
	 * changes during the dot-dot locking complexities.
	 */
	for (;;) {
		uint64_t parent;

		error = zfs_dirlook(zdp, nm, &zp);
		if (error == 0)
			*vpp = ZTOV(zp);

		ZFS_EXIT(zfsvfs);
		if (error != 0)
			break;

		error = zfs_lookup_lock(dvp, *vpp, nm, cnp->cn_lkflags);
		if (error != 0) {
			/*
			 * If we've got a locking error, then the vnode
			 * got reclaimed because of a force unmount.
			 * We never enter doomed vnodes into the name cache.
			 */
			*vpp = NULL;
			return (error);
		}

		if ((cnp->cn_flags & ISDOTDOT) == 0)
			break;

		ZFS_ENTER(zfsvfs);
		if (zdp->z_sa_hdl == NULL) {
			error = SET_ERROR(EIO);
		} else {
			error = sa_lookup(zdp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
			    &parent, sizeof (parent));
		}
		if (error != 0) {
			ZFS_EXIT(zfsvfs);
			vput(ZTOV(zp));
			break;
		}
		if (zp->z_id == parent) {
			ZFS_EXIT(zfsvfs);
			break;
		}
		vput(ZTOV(zp));
	}

out:
	if (error != 0)
		*vpp = NULL;

	/* Translate errors and add SAVENAME when needed. */
	if (cnp->cn_flags & ISLASTCN) {
		switch (nameiop) {
		case CREATE:
		case RENAME:
			if (error == ENOENT) {
				error = EJUSTRETURN;
				cnp->cn_flags |= SAVENAME;
				break;
			}
			/* FALLTHROUGH */
		case DELETE:
			if (error == 0)
				cnp->cn_flags |= SAVENAME;
			break;
		}
	}

	/* Insert name into cache (as non-existent) if appropriate. */
	if (zfsvfs->z_use_namecache &&
	    error == ENOENT && (cnp->cn_flags & MAKEENTRY) != 0)
		cache_enter(dvp, NULL, cnp);

	/* Insert name into cache if appropriate. */
	if (zfsvfs->z_use_namecache &&
	    error == 0 && (cnp->cn_flags & MAKEENTRY)) {
		if (!(cnp->cn_flags & ISLASTCN) ||
		    (nameiop != DELETE && nameiop != RENAME)) {
			cache_enter(dvp, *vpp, cnp);
		}
	}

	return (error);
}

/*
 * Attempt to create a new entry in a directory.  If the entry
 * already exists, truncate the file if permissible, else return
 * an error.  Return the vp of the created or trunc'd file.
 *
 *	IN:	dvp	- vnode of directory to put new file entry in.
 *		name	- name of new file entry.
 *		vap	- attributes of new file.
 *		excl	- flag indicating exclusive or non-exclusive mode.
 *		mode	- mode to open file with.
 *		cr	- credentials of caller.
 *		flag	- large file flag [UNUSED].
 *		ct	- caller context
 *		vsecp	- ACL to be set
 *
 *	OUT:	vpp	- vnode of created or trunc'd entry.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dvp - ctime|mtime updated if new entry created
 *	 vp - ctime|mtime always, atime if new
 */

/* ARGSUSED */
static int
zfs_create(vnode_t *dvp, char *name, vattr_t *vap, int excl, int mode,
    vnode_t **vpp, cred_t *cr, kthread_t *td)
{
	znode_t		*zp, *dzp = VTOZ(dvp);
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	objset_t	*os;
	dmu_tx_t	*tx;
	int		error;
	ksid_t		*ksid;
	uid_t		uid;
	gid_t		gid = crgetgid(cr);
	zfs_acl_ids_t   acl_ids;
	boolean_t	fuid_dirtied;
	void		*vsecp = NULL;
	int		flag = 0;
	uint64_t	txtype;

	/*
	 * If we have an ephemeral id, ACL, or XVATTR then
	 * make sure file system is at proper version
	 */

	ksid = crgetsid(cr, KSID_OWNER);
	if (ksid)
		uid = ksid_getid(ksid);
	else
		uid = crgetuid(cr);

	if (zfsvfs->z_use_fuids == B_FALSE &&
	    (vsecp || (vap->va_mask & AT_XVATTR) ||
	    IS_EPHEMERAL(uid) || IS_EPHEMERAL(gid)))
		return (SET_ERROR(EINVAL));

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	os = zfsvfs->z_os;
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EILSEQ));
	}

	if (vap->va_mask & AT_XVATTR) {
		if ((error = secpolicy_xvattr(dvp, (xvattr_t *)vap,
		    crgetuid(cr), cr, vap->va_type)) != 0) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}

	*vpp = NULL;

	if ((vap->va_mode & S_ISVTX) && secpolicy_vnode_stky_modify(cr))
		vap->va_mode &= ~S_ISVTX;

	error = zfs_dirent_lookup(dzp, name, &zp, ZNEW);
	if (error) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}
	ASSERT3P(zp, ==, NULL);

	/*
	 * Create a new file object and update the directory
	 * to reference it.
	 */
	if (error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr)) {
		goto out;
	}

	/*
	 * We only support the creation of regular files in
	 * extended attribute directories.
	 */

	if ((dzp->z_pflags & ZFS_XATTR) &&
	    (vap->va_type != VREG)) {
		error = SET_ERROR(EINVAL);
		goto out;
	}

	if ((error = zfs_acl_ids_create(dzp, 0, vap,
	    cr, vsecp, &acl_ids)) != 0)
		goto out;

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids)) {
		zfs_acl_ids_free(&acl_ids);
		error = SET_ERROR(EDQUOT);
		goto out;
	}

	getnewvnode_reserve(1);

	tx = dmu_tx_create(os);

	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE);

	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
	if (!zfsvfs->z_use_sa &&
	    acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
		    0, acl_ids.z_aclp->z_acl_bytes);
	}
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		getnewvnode_drop_reserve();
		ZFS_EXIT(zfsvfs);
		return (error);
	}
	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	(void) zfs_link_create(dzp, name, zp, tx, ZNEW);
	txtype = zfs_log_create_txtype(Z_FILE, vsecp, vap);
	zfs_log_create(zilog, tx, txtype, dzp, zp, name,
	    vsecp, acl_ids.z_fuidp, vap);
	zfs_acl_ids_free(&acl_ids);
	dmu_tx_commit(tx);

	getnewvnode_drop_reserve();

out:
	if (error == 0) {
		*vpp = ZTOV(zp);
	}

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Remove an entry from a directory.
 *
 *	IN:	dvp	- vnode of directory to remove entry from.
 *		name	- name of entry to remove.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dvp - ctime|mtime
 *	 vp - ctime (if nlink > 0)
 */

/*ARGSUSED*/
static int
zfs_remove(vnode_t *dvp, vnode_t *vp, char *name, cred_t *cr)
{
	znode_t		*dzp = VTOZ(dvp);
	znode_t		*zp = VTOZ(vp);
	znode_t		*xzp;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	uint64_t	acl_obj, xattr_obj;
	uint64_t	obj = 0;
	dmu_tx_t	*tx;
	boolean_t	unlinked, toobig = FALSE;
	uint64_t	txtype;
	int		error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	ZFS_VERIFY_ZP(zp);
	zilog = zfsvfs->z_log;
	zp = VTOZ(vp);

	xattr_obj = 0;
	xzp = NULL;

	if (error = zfs_zaccess_delete(dzp, zp, cr)) {
		goto out;
	}

	/*
	 * Need to use rmdir for removing directories.
	 */
	if (vp->v_type == VDIR) {
		error = SET_ERROR(EPERM);
		goto out;
	}

	vnevent_remove(vp, dvp, name, ct);

	obj = zp->z_id;

	/* are there any extended attributes? */
	error = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
	    &xattr_obj, sizeof (xattr_obj));
	if (error == 0 && xattr_obj) {
		error = zfs_zget(zfsvfs, xattr_obj, &xzp);
		ASSERT0(error);
	}

	/*
	 * We may delete the znode now, or we may put it in the unlinked set;
	 * it depends on whether we're the last link, and on whether there are
	 * other holds on the vnode.  So we dmu_tx_hold() the right things to
	 * allow for either case.
	 */
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);

	if (xzp) {
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		dmu_tx_hold_sa(tx, xzp->z_sa_hdl, B_FALSE);
	}

	/* charge as an update -- would be nice not to charge at all */
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);

	/*
	 * Mark this transaction as typically resulting in a net free of space
	 */
	dmu_tx_mark_netfree(tx);

	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Remove the directory entry.
	 */
	error = zfs_link_destroy(dzp, name, zp, tx, ZEXISTS, &unlinked);

	if (error) {
		dmu_tx_commit(tx);
		goto out;
	}

	if (unlinked) {
		zfs_unlinked_add(zp, tx);
		vp->v_vflag |= VV_NOSYNC;
	}

	txtype = TX_REMOVE;
	zfs_log_remove(zilog, tx, txtype, dzp, name, obj);

	dmu_tx_commit(tx);
out:

	if (xzp)
		vrele(ZTOV(xzp));

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Create a new directory and insert it into dvp using the name
 * provided.  Return a pointer to the inserted directory.
 *
 *	IN:	dvp	- vnode of directory to add subdir to.
 *		dirname	- name of new directory.
 *		vap	- attributes of new directory.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *		vsecp	- ACL to be set
 *
 *	OUT:	vpp	- vnode of created directory.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dvp - ctime|mtime updated
 *	 vp - ctime|mtime|atime updated
 */
/*ARGSUSED*/
static int
zfs_mkdir(vnode_t *dvp, char *dirname, vattr_t *vap, vnode_t **vpp, cred_t *cr)
{
	znode_t		*zp, *dzp = VTOZ(dvp);
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	uint64_t	txtype;
	dmu_tx_t	*tx;
	int		error;
	ksid_t		*ksid;
	uid_t		uid;
	gid_t		gid = crgetgid(cr);
	zfs_acl_ids_t   acl_ids;
	boolean_t	fuid_dirtied;

	ASSERT(vap->va_type == VDIR);

	/*
	 * If we have an ephemeral id, ACL, or XVATTR then
	 * make sure file system is at proper version
	 */

	ksid = crgetsid(cr, KSID_OWNER);
	if (ksid)
		uid = ksid_getid(ksid);
	else
		uid = crgetuid(cr);
	if (zfsvfs->z_use_fuids == B_FALSE &&
	    ((vap->va_mask & AT_XVATTR) ||
	    IS_EPHEMERAL(uid) || IS_EPHEMERAL(gid)))
		return (SET_ERROR(EINVAL));

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	if (dzp->z_pflags & ZFS_XATTR) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	if (zfsvfs->z_utf8 && u8_validate(dirname,
	    strlen(dirname), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EILSEQ));
	}

	if (vap->va_mask & AT_XVATTR) {
		if ((error = secpolicy_xvattr(dvp, (xvattr_t *)vap,
		    crgetuid(cr), cr, vap->va_type)) != 0) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}

	if ((error = zfs_acl_ids_create(dzp, 0, vap, cr,
	    NULL, &acl_ids)) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * First make sure the new directory doesn't exist.
	 *
	 * Existence is checked first to make sure we don't return
	 * EACCES instead of EEXIST which can cause some applications
	 * to fail.
	 */
	*vpp = NULL;

	if (error = zfs_dirent_lookup(dzp, dirname, &zp, ZNEW)) {
		zfs_acl_ids_free(&acl_ids);
		ZFS_EXIT(zfsvfs);
		return (error);
	}
	ASSERT3P(zp, ==, NULL);

	if (error = zfs_zaccess(dzp, ACE_ADD_SUBDIRECTORY, 0, B_FALSE, cr)) {
		zfs_acl_ids_free(&acl_ids);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids)) {
		zfs_acl_ids_free(&acl_ids);
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EDQUOT));
	}

	/*
	 * Add a new entry to the directory.
	 */
	getnewvnode_reserve(1);
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, dirname);
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	if (!zfsvfs->z_use_sa && acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    acl_ids.z_aclp->z_acl_bytes);
	}

	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE);

	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		getnewvnode_drop_reserve();
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Create new node.
	 */
	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	/*
	 * Now put new name in parent dir.
	 */
	(void) zfs_link_create(dzp, dirname, zp, tx, ZNEW);

	*vpp = ZTOV(zp);

	txtype = zfs_log_create_txtype(Z_DIR, NULL, vap);
	zfs_log_create(zilog, tx, txtype, dzp, zp, dirname, NULL,
	    acl_ids.z_fuidp, vap);

	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);

	getnewvnode_drop_reserve();

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (0);
}

/*
 * Remove a directory subdir entry.  If the current working
 * directory is the same as the subdir to be removed, the
 * remove will fail.
 *
 *	IN:	dvp	- vnode of directory to remove from.
 *		name	- name of directory to be removed.
 *		cwd	- vnode of current working directory.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dvp - ctime|mtime updated
 */
/*ARGSUSED*/
static int
zfs_rmdir(vnode_t *dvp, vnode_t *vp, char *name, cred_t *cr)
{
	znode_t		*dzp = VTOZ(dvp);
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	dmu_tx_t	*tx;
	int		error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	ZFS_VERIFY_ZP(zp);
	zilog = zfsvfs->z_log;


	if (error = zfs_zaccess_delete(dzp, zp, cr)) {
		goto out;
	}

	if (vp->v_type != VDIR) {
		error = SET_ERROR(ENOTDIR);
		goto out;
	}

	vnevent_rmdir(vp, dvp, name, ct);

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	cache_purge(dvp);

	error = zfs_link_destroy(dzp, name, zp, tx, ZEXISTS, NULL);

	if (error == 0) {
		uint64_t txtype = TX_RMDIR;
		zfs_log_remove(zilog, tx, txtype, dzp, name, ZFS_NO_OBJECT);
	}

	dmu_tx_commit(tx);

	cache_purge(vp);
out:
	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Read as many directory entries as will fit into the provided
 * buffer from the given directory cursor position (specified in
 * the uio structure).
 *
 *	IN:	vp	- vnode of directory to read.
 *		uio	- structure supplying read location, range info,
 *			  and return buffer.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	OUT:	uio	- updated offset and range, buffer filled.
 *		eofp	- set to true if end-of-file detected.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	vp - atime updated
 *
 * Note that the low 4 bits of the cookie returned by zap is always zero.
 * This allows us to use the low range for "special" directory entries:
 * We use 0 for '.', and 1 for '..'.  If this is the root of the filesystem,
 * we use the offset 2 for the '.zfs' directory.
 */
/* ARGSUSED */
static int
zfs_readdir(vnode_t *vp, uio_t *uio, cred_t *cr, int *eofp, int *ncookies, u_long **cookies)
{
	znode_t		*zp = VTOZ(vp);
	iovec_t		*iovp;
	edirent_t	*eodp;
	dirent64_t	*odp;
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	objset_t	*os;
	caddr_t		outbuf;
	size_t		bufsize;
	zap_cursor_t	zc;
	zap_attribute_t	zap;
	uint_t		bytes_wanted;
	uint64_t	offset; /* must be unsigned; checks for < 1 */
	uint64_t	parent;
	int		local_eof;
	int		outcount;
	int		error;
	uint8_t		prefetch;
	boolean_t	check_sysattrs;
	uint8_t		type;
	int		ncooks;
	u_long		*cooks = NULL;
	int		flags = 0;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (parent))) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * If we are not given an eof variable,
	 * use a local one.
	 */
	if (eofp == NULL)
		eofp = &local_eof;

	/*
	 * Check for valid iov_len.
	 */
	if (uio->uio_iov->iov_len <= 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Quit if directory has been removed (posix)
	 */
	if ((*eofp = zp->z_unlinked) != 0) {
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	error = 0;
	os = zfsvfs->z_os;
	offset = uio->uio_loffset;
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
	 * Get space to change directory entries into fs independent format.
	 */
	iovp = uio->uio_iov;
	bytes_wanted = iovp->iov_len;
	if (uio->uio_segflg != UIO_SYSSPACE || uio->uio_iovcnt != 1) {
		bufsize = bytes_wanted;
		outbuf = kmem_alloc(bufsize, KM_SLEEP);
		odp = (struct dirent64 *)outbuf;
	} else {
		bufsize = bytes_wanted;
		outbuf = NULL;
		odp = (struct dirent64 *)iovp->iov_base;
	}
	eodp = (struct edirent *)odp;

	if (ncookies != NULL) {
		/*
		 * Minimum entry size is dirent size and 1 byte for a file name.
		 */
		ncooks = uio->uio_resid / (sizeof(struct dirent) - sizeof(((struct dirent *)NULL)->d_name) + 1);
		cooks = malloc(ncooks * sizeof(u_long), M_TEMP, M_WAITOK);
		*cookies = cooks;
		*ncookies = ncooks;
	}
	/*
	 * If this VFS supports the system attribute view interface; and
	 * we're looking at an extended attribute directory; and we care
	 * about normalization conflicts on this vfs; then we must check
	 * for normalization conflicts with the sysattr name space.
	 */
#ifdef TODO
	check_sysattrs = vfs_has_feature(vp->v_vfsp, VFSFT_SYSATTR_VIEWS) &&
	    (vp->v_flag & V_XATTRDIR) && zfsvfs->z_norm &&
	    (flags & V_RDDIR_ENTFLAGS);
#else
	check_sysattrs = 0;
#endif

	/*
	 * Transform to file-system independent format
	 */
	outcount = 0;
	while (outcount < bytes_wanted) {
		ino64_t objnum;
		ushort_t reclen;
		off64_t *next = NULL;

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
			if (error = zap_cursor_retrieve(&zc, &zap)) {
				if ((*eofp = (error == ENOENT)) != 0)
					break;
				else
					goto update;
			}

			if (zap.za_integer_length != 8 ||
			    zap.za_num_integers != 1) {
				cmn_err(CE_WARN, "zap_readdir: bad directory "
				    "entry, obj = %lld, offset = %lld\n",
				    (u_longlong_t)zp->z_id,
				    (u_longlong_t)offset);
				error = SET_ERROR(ENXIO);
				goto update;
			}

			objnum = ZFS_DIRENT_OBJ(zap.za_first_integer);
			/*
			 * MacOS X can extract the object type here such as:
			 * uint8_t type = ZFS_DIRENT_TYPE(zap.za_first_integer);
			 */
			type = ZFS_DIRENT_TYPE(zap.za_first_integer);

			if (check_sysattrs && !zap.za_normalization_conflict) {
#ifdef TODO
				zap.za_normalization_conflict =
				    xattr_sysattr_casechk(zap.za_name);
#else
				panic("%s:%u: TODO", __func__, __LINE__);
#endif
			}
		}

		if (flags & V_RDDIR_ACCFILTER) {
			/*
			 * If we have no access at all, don't include
			 * this entry in the returned information
			 */
			znode_t	*ezp;
			if (zfs_zget(zp->z_zfsvfs, objnum, &ezp) != 0)
				goto skip_entry;
			if (!zfs_has_access(ezp, cr)) {
				vrele(ZTOV(ezp));
				goto skip_entry;
			}
			vrele(ZTOV(ezp));
		}

		if (flags & V_RDDIR_ENTFLAGS)
			reclen = EDIRENT_RECLEN(strlen(zap.za_name));
		else
			reclen = DIRENT64_RECLEN(strlen(zap.za_name));

		/*
		 * Will this entry fit in the buffer?
		 */
		if (outcount + reclen > bufsize) {
			/*
			 * Did we manage to fit anything in the buffer?
			 */
			if (!outcount) {
				error = SET_ERROR(EINVAL);
				goto update;
			}
			break;
		}
		if (flags & V_RDDIR_ENTFLAGS) {
			/*
			 * Add extended flag entry:
			 */
			eodp->ed_ino = objnum;
			eodp->ed_reclen = reclen;
			/* NOTE: ed_off is the offset for the *next* entry. */
			next = &eodp->ed_off;
			eodp->ed_eflags = zap.za_normalization_conflict ?
			    ED_CASE_CONFLICT : 0;
			(void) strncpy(eodp->ed_name, zap.za_name,
			    EDIRENT_NAMELEN(reclen));
			eodp = (edirent_t *)((intptr_t)eodp + reclen);
		} else {
			/*
			 * Add normal entry:
			 */
			odp->d_ino = objnum;
			odp->d_reclen = reclen;
			odp->d_namlen = strlen(zap.za_name);
			/* NOTE: d_off is the offset for the *next* entry. */
			next = &odp->d_off;
			(void) strlcpy(odp->d_name, zap.za_name, odp->d_namlen + 1);
			odp->d_type = type;
			dirent_terminate(odp);
			odp = (dirent64_t *)((intptr_t)odp + reclen);
		}
		outcount += reclen;

		ASSERT(outcount <= bufsize);

		/* Prefetch znode */
		if (prefetch)
			dmu_prefetch(os, objnum, 0, 0, 0,
			    ZIO_PRIORITY_SYNC_READ);

	skip_entry:
		/*
		 * Move to the next entry, fill in the previous offset.
		 */
		if (offset > 2 || (offset == 2 && !zfs_show_ctldir(zp))) {
			zap_cursor_advance(&zc);
			offset = zap_cursor_serialize(&zc);
		} else {
			offset += 1;
		}

		/* Fill the offset right after advancing the cursor. */
		if (next != NULL)
			*next = offset;
		if (cooks != NULL) {
			*cooks++ = offset;
			ncooks--;
			KASSERT(ncooks >= 0, ("ncookies=%d", ncooks));
		}
	}
	zp->z_zn_prefetch = B_FALSE; /* a lookup will re-enable pre-fetching */

	/* Subtract unused cookies */
	if (ncookies != NULL)
		*ncookies -= ncooks;

	if (uio->uio_segflg == UIO_SYSSPACE && uio->uio_iovcnt == 1) {
		iovp->iov_base += outcount;
		iovp->iov_len -= outcount;
		uio->uio_resid -= outcount;
	} else if (error = uiomove(outbuf, (long)outcount, UIO_READ, uio)) {
		/*
		 * Reset the pointer.
		 */
		offset = uio->uio_loffset;
	}

update:
	zap_cursor_fini(&zc);
	if (uio->uio_segflg != UIO_SYSSPACE || uio->uio_iovcnt != 1)
		kmem_free(outbuf, bufsize);

	if (error == ENOENT)
		error = 0;

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);

	uio->uio_loffset = offset;
	ZFS_EXIT(zfsvfs);
	if (error != 0 && cookies != NULL) {
		free(*cookies, M_TEMP);
		*cookies = NULL;
		*ncookies = 0;
	}
	return (error);
}

ulong_t zfs_fsync_sync_cnt = 4;

static int
zfs_fsync(vnode_t *vp, int syncflag, cred_t *cr, caller_context_t *ct)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	(void) tsd_set(zfs_fsyncer_key, (void *)zfs_fsync_sync_cnt);

	if (zfsvfs->z_os->os_sync != ZFS_SYNC_DISABLED) {
		ZFS_ENTER(zfsvfs);
		ZFS_VERIFY_ZP(zp);
		zil_commit(zfsvfs->z_log, zp->z_id);
		ZFS_EXIT(zfsvfs);
	}
	return (0);
}


/*
 * Get the requested file attributes and place them in the provided
 * vattr structure.
 *
 *	IN:	vp	- vnode of file.
 *		vap	- va_mask identifies requested attributes.
 *			  If AT_XVATTR set, then optional attrs are requested
 *		flags	- ATTR_NOACLCHECK (CIFS server context)
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	OUT:	vap	- attribute values.
 *
 *	RETURN:	0 (always succeeds).
 */
/* ARGSUSED */
static int
zfs_getattr(vnode_t *vp, vattr_t *vap, int flags, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int	error = 0;
	uint32_t blksize;
	u_longlong_t nblocks;
	uint64_t mtime[2], ctime[2], crtime[2], rdev;
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t *xoap = NULL;
	boolean_t skipaclchk = (flags & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	sa_bulk_attr_t bulk[4];
	int count = 0;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	zfs_fuid_map_ids(zp, cr, &vap->va_uid, &vap->va_gid);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CRTIME(zfsvfs), NULL, &crtime, 16);
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_RDEV(zfsvfs), NULL,
		    &rdev, 8);

	if ((error = sa_bulk_lookup(zp->z_sa_hdl, bulk, count)) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * If ACL is trivial don't bother looking for ACE_READ_ATTRIBUTES.
	 * Also, if we are the owner don't bother, since owner should
	 * always be allowed to read basic attributes of file.
	 */
	if (!(zp->z_pflags & ZFS_ACL_TRIVIAL) &&
	    (vap->va_uid != crgetuid(cr))) {
		if (error = zfs_zaccess(zp, ACE_READ_ATTRIBUTES, 0,
		    skipaclchk, cr)) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}

	/*
	 * Return all attributes.  It's cheaper to provide the answer
	 * than to determine whether we were asked the question.
	 */

	vap->va_type = IFTOVT(zp->z_mode);
	vap->va_mode = zp->z_mode & ~S_IFMT;
#ifdef illumos
	vap->va_fsid = zp->z_zfsvfs->z_vfs->vfs_dev;
#else
	vn_fsid(vp, vap);
#endif
	vap->va_nodeid = zp->z_id;
	vap->va_nlink = zp->z_links;
	if ((vp->v_flag & VROOT) && zfs_show_ctldir(zp) &&
	    zp->z_links < ZFS_LINK_MAX)
		vap->va_nlink++;
	vap->va_size = zp->z_size;
#ifdef illumos
	vap->va_rdev = vp->v_rdev;
#else
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		vap->va_rdev = zfs_cmpldev(rdev);
#endif
	vap->va_seq = zp->z_seq;
	vap->va_flags = 0;	/* FreeBSD: Reset chflags(2) flags. */
     	vap->va_filerev = zp->z_seq;

	/*
	 * Add in any requested optional attributes and the create time.
	 * Also set the corresponding bits in the returned attribute bitmap.
	 */
	if ((xoap = xva_getxoptattr(xvap)) != NULL && zfsvfs->z_use_fuids) {
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
		    vp->v_type == VREG) {
			zfs_sa_get_scanstamp(zp, xvap);
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

	ZFS_TIME_DECODE(&vap->va_atime, zp->z_atime);
	ZFS_TIME_DECODE(&vap->va_mtime, mtime);
	ZFS_TIME_DECODE(&vap->va_ctime, ctime);
	ZFS_TIME_DECODE(&vap->va_birthtime, crtime);


	sa_object_size(zp->z_sa_hdl, &blksize, &nblocks);
	vap->va_blksize = blksize;
	vap->va_bytes = nblocks << 9;	/* nblocks * 512 */

	if (zp->z_blksz == 0) {
		/*
		 * Block size hasn't been set; suggest maximal I/O transfers.
		 */
		vap->va_blksize = zfsvfs->z_max_blksz;
	}

	ZFS_EXIT(zfsvfs);
	return (0);
}

/*
 * Set the file attributes to the values contained in the
 * vattr structure.
 *
 *	IN:	vp	- vnode of file to be modified.
 *		vap	- new attribute values.
 *			  If AT_XVATTR set, then optional attrs are being set
 *		flags	- ATTR_UTIME set if non-default time values provided.
 *			- ATTR_NOACLCHECK (CIFS context only).
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	vp - ctime updated, mtime updated if size changed.
 */
/* ARGSUSED */
static int
zfs_setattr(vnode_t *vp, vattr_t *vap, int flags, cred_t *cr,
    caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	zilog_t		*zilog;
	dmu_tx_t	*tx;
	vattr_t		oldva;
	xvattr_t	tmpxvattr;
	uint_t		mask = vap->va_mask;
	uint_t		saved_mask = 0;
	uint64_t	saved_mode;
	int		trim_mask = 0;
	uint64_t	new_mode;
	uint64_t	new_uid, new_gid;
	uint64_t	xattr_obj;
	uint64_t	mtime[2], ctime[2];
	znode_t		*attrzp;
	int		need_policy = FALSE;
	int		err, err2;
	zfs_fuid_info_t *fuidp = NULL;
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t	*xoap;
	zfs_acl_t	*aclp;
	boolean_t skipaclchk = (flags & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	boolean_t	fuid_dirtied = B_FALSE;
	sa_bulk_attr_t	bulk[7], xattr_bulk[7];
	int		count = 0, xattr_count = 0;

	if (mask == 0)
		return (0);

	if (mask & AT_NOSET)
		return (SET_ERROR(EINVAL));

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	zilog = zfsvfs->z_log;

	/*
	 * Make sure that if we have ephemeral uid/gid or xvattr specified
	 * that file system is at proper version level
	 */

	if (zfsvfs->z_use_fuids == B_FALSE &&
	    (((mask & AT_UID) && IS_EPHEMERAL(vap->va_uid)) ||
	    ((mask & AT_GID) && IS_EPHEMERAL(vap->va_gid)) ||
	    (mask & AT_XVATTR))) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	if (mask & AT_SIZE && vp->v_type == VDIR) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EISDIR));
	}

	if (mask & AT_SIZE && vp->v_type != VREG && vp->v_type != VFIFO) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * If this is an xvattr_t, then get a pointer to the structure of
	 * optional attributes.  If this is NULL, then we have a vattr_t.
	 */
	xoap = xva_getxoptattr(xvap);

	xva_init(&tmpxvattr);

	/*
	 * Immutable files can only alter immutable bit and atime
	 */
	if ((zp->z_pflags & ZFS_IMMUTABLE) &&
	    ((mask & (AT_SIZE|AT_UID|AT_GID|AT_MTIME|AT_MODE)) ||
	    ((mask & AT_XVATTR) && XVA_ISSET_REQ(xvap, XAT_CREATETIME)))) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	/*
	 * Note: ZFS_READONLY is handled in zfs_zaccess_common.
	 */

	/*
	 * Verify timestamps doesn't overflow 32 bits.
	 * ZFS can handle large timestamps, but 32bit syscalls can't
	 * handle times greater than 2039.  This check should be removed
	 * once large timestamps are fully supported.
	 */
	if (mask & (AT_ATIME | AT_MTIME)) {
		if (((mask & AT_ATIME) && TIMESPEC_OVERFLOW(&vap->va_atime)) ||
		    ((mask & AT_MTIME) && TIMESPEC_OVERFLOW(&vap->va_mtime))) {
			ZFS_EXIT(zfsvfs);
			return (SET_ERROR(EOVERFLOW));
		}
	}
	if (xoap && (mask & AT_XVATTR) && XVA_ISSET_REQ(xvap, XAT_CREATETIME) &&
	    TIMESPEC_OVERFLOW(&vap->va_birthtime)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EOVERFLOW));
	}

	attrzp = NULL;
	aclp = NULL;

	/* Can this be moved to before the top label? */
	if (zfsvfs->z_vfs->vfs_flag & VFS_RDONLY) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EROFS));
	}

	/*
	 * First validate permissions
	 */

	if (mask & AT_SIZE) {
		/*
		 * XXX - Note, we are not providing any open
		 * mode flags here (like FNDELAY), so we may
		 * block if there are locks present... this
		 * should be addressed in openat().
		 */
		/* XXX - would it be OK to generate a log record here? */
		err = zfs_freesp(zp, vap->va_size, 0, 0, FALSE);
		if (err) {
			ZFS_EXIT(zfsvfs);
			return (err);
		}
	}

	if (mask & (AT_ATIME|AT_MTIME) ||
	    ((mask & AT_XVATTR) && (XVA_ISSET_REQ(xvap, XAT_HIDDEN) ||
	    XVA_ISSET_REQ(xvap, XAT_READONLY) ||
	    XVA_ISSET_REQ(xvap, XAT_ARCHIVE) ||
	    XVA_ISSET_REQ(xvap, XAT_OFFLINE) ||
	    XVA_ISSET_REQ(xvap, XAT_SPARSE) ||
	    XVA_ISSET_REQ(xvap, XAT_CREATETIME) ||
	    XVA_ISSET_REQ(xvap, XAT_SYSTEM)))) {
		need_policy = zfs_zaccess(zp, ACE_WRITE_ATTRIBUTES, 0,
		    skipaclchk, cr);
	}

	if (mask & (AT_UID|AT_GID)) {
		int	idmask = (mask & (AT_UID|AT_GID));
		int	take_owner;
		int	take_group;

		/*
		 * NOTE: even if a new mode is being set,
		 * we may clear S_ISUID/S_ISGID bits.
		 */

		if (!(mask & AT_MODE))
			vap->va_mode = zp->z_mode;

		/*
		 * Take ownership or chgrp to group we are a member of
		 */

		take_owner = (mask & AT_UID) && (vap->va_uid == crgetuid(cr));
		take_group = (mask & AT_GID) &&
		    zfs_groupmember(zfsvfs, vap->va_gid, cr);

		/*
		 * If both AT_UID and AT_GID are set then take_owner and
		 * take_group must both be set in order to allow taking
		 * ownership.
		 *
		 * Otherwise, send the check through secpolicy_vnode_setattr()
		 *
		 */

		if (((idmask == (AT_UID|AT_GID)) && take_owner && take_group) ||
		    ((idmask == AT_UID) && take_owner) ||
		    ((idmask == AT_GID) && take_group)) {
			if (zfs_zaccess(zp, ACE_WRITE_OWNER, 0,
			    skipaclchk, cr) == 0) {
				/*
				 * Remove setuid/setgid for non-privileged users
				 */
				secpolicy_setid_clear(vap, vp, cr);
				trim_mask = (mask & (AT_UID|AT_GID));
			} else {
				need_policy =  TRUE;
			}
		} else {
			need_policy =  TRUE;
		}
	}

	oldva.va_mode = zp->z_mode;
	zfs_fuid_map_ids(zp, cr, &oldva.va_uid, &oldva.va_gid);
	if (mask & AT_XVATTR) {
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
				XVA_SET_REQ(&tmpxvattr, XAT_APPENDONLY);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_NOUNLINK)) {
			if (xoap->xoa_nounlink !=
			    ((zp->z_pflags & ZFS_NOUNLINK) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_NOUNLINK);
				XVA_SET_REQ(&tmpxvattr, XAT_NOUNLINK);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE)) {
			if (xoap->xoa_immutable !=
			    ((zp->z_pflags & ZFS_IMMUTABLE) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_IMMUTABLE);
				XVA_SET_REQ(&tmpxvattr, XAT_IMMUTABLE);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_NODUMP)) {
			if (xoap->xoa_nodump !=
			    ((zp->z_pflags & ZFS_NODUMP) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_NODUMP);
				XVA_SET_REQ(&tmpxvattr, XAT_NODUMP);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED)) {
			if (xoap->xoa_av_modified !=
			    ((zp->z_pflags & ZFS_AV_MODIFIED) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_AV_MODIFIED);
				XVA_SET_REQ(&tmpxvattr, XAT_AV_MODIFIED);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED)) {
			if ((vp->v_type != VREG &&
			    xoap->xoa_av_quarantined) ||
			    xoap->xoa_av_quarantined !=
			    ((zp->z_pflags & ZFS_AV_QUARANTINED) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_AV_QUARANTINED);
				XVA_SET_REQ(&tmpxvattr, XAT_AV_QUARANTINED);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_REPARSE)) {
			ZFS_EXIT(zfsvfs);
			return (SET_ERROR(EPERM));
		}

		if (need_policy == FALSE &&
		    (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP) ||
		    XVA_ISSET_REQ(xvap, XAT_OPAQUE))) {
			need_policy = TRUE;
		}
	}

	if (mask & AT_MODE) {
		if (zfs_zaccess(zp, ACE_WRITE_ACL, 0, skipaclchk, cr) == 0) {
			err = secpolicy_setid_setsticky_clear(vp, vap,
			    &oldva, cr);
			if (err) {
				ZFS_EXIT(zfsvfs);
				return (err);
			}
			trim_mask |= AT_MODE;
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
			if (trim_mask & AT_MODE) {
				/*
				 * Save the mode, as secpolicy_vnode_setattr()
				 * will overwrite it with ova.va_mode.
				 */
				saved_mode = vap->va_mode;
			}
		}
		err = secpolicy_vnode_setattr(cr, vp, vap, &oldva, flags,
		    (int (*)(void *, int, cred_t *))zfs_zaccess_unix, zp);
		if (err) {
			ZFS_EXIT(zfsvfs);
			return (err);
		}

		if (trim_mask) {
			vap->va_mask |= saved_mask;
			if (trim_mask & AT_MODE) {
				/*
				 * Recover the mode after
				 * secpolicy_vnode_setattr().
				 */
				vap->va_mode = saved_mode;
			}
		}
	}

	/*
	 * secpolicy_vnode_setattr, or take ownership may have
	 * changed va_mask
	 */
	mask = vap->va_mask;

	if ((mask & (AT_UID | AT_GID))) {
		err = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
		    &xattr_obj, sizeof (xattr_obj));

		if (err == 0 && xattr_obj) {
			err = zfs_zget(zp->z_zfsvfs, xattr_obj, &attrzp);
			if (err == 0) {
				err = vn_lock(ZTOV(attrzp), LK_EXCLUSIVE);
				if (err != 0)
					vrele(ZTOV(attrzp));
			}
			if (err)
				goto out2;
		}
		if (mask & AT_UID) {
			new_uid = zfs_fuid_create(zfsvfs,
			    (uint64_t)vap->va_uid, cr, ZFS_OWNER, &fuidp);
			if (new_uid != zp->z_uid &&
			    zfs_fuid_overquota(zfsvfs, B_FALSE, new_uid)) {
				if (attrzp)
					vput(ZTOV(attrzp));
				err = SET_ERROR(EDQUOT);
				goto out2;
			}
		}

		if (mask & AT_GID) {
			new_gid = zfs_fuid_create(zfsvfs, (uint64_t)vap->va_gid,
			    cr, ZFS_GROUP, &fuidp);
			if (new_gid != zp->z_gid &&
			    zfs_fuid_overquota(zfsvfs, B_TRUE, new_gid)) {
				if (attrzp)
					vput(ZTOV(attrzp));
				err = SET_ERROR(EDQUOT);
				goto out2;
			}
		}
	}
	tx = dmu_tx_create(zfsvfs->z_os);

	if (mask & AT_MODE) {
		uint64_t pmode = zp->z_mode;
		uint64_t acl_obj;
		new_mode = (pmode & S_IFMT) | (vap->va_mode & ~S_IFMT);

		if (zp->z_zfsvfs->z_acl_mode == ZFS_ACL_RESTRICTED &&
		    !(zp->z_pflags & ZFS_ACL_TRIVIAL)) {
			err = SET_ERROR(EPERM);
			goto out;
		}

		if (err = zfs_acl_chmod_setattr(zp, &aclp, new_mode))
			goto out;

		if (!zp->z_is_sa && ((acl_obj = zfs_external_acl(zp)) != 0)) {
			/*
			 * Are we upgrading ACL from old V0 format
			 * to V1 format?
			 */
			if (zfsvfs->z_version >= ZPL_VERSION_FUID &&
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
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
	} else {
		if ((mask & AT_XVATTR) &&
		    XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP))
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		else
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	}

	if (attrzp) {
		dmu_tx_hold_sa(tx, attrzp->z_sa_hdl, B_FALSE);
	}

	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);

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

	if (mask & (AT_UID|AT_GID|AT_MODE))
		mutex_enter(&zp->z_acl_lock);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));

	if (attrzp) {
		if (mask & (AT_UID|AT_GID|AT_MODE))
			mutex_enter(&attrzp->z_acl_lock);
		SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
		    SA_ZPL_FLAGS(zfsvfs), NULL, &attrzp->z_pflags,
		    sizeof (attrzp->z_pflags));
	}

	if (mask & (AT_UID|AT_GID)) {

		if (mask & AT_UID) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
			    &new_uid, sizeof (new_uid));
			zp->z_uid = new_uid;
			if (attrzp) {
				SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
				    SA_ZPL_UID(zfsvfs), NULL, &new_uid,
				    sizeof (new_uid));
				attrzp->z_uid = new_uid;
			}
		}

		if (mask & AT_GID) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs),
			    NULL, &new_gid, sizeof (new_gid));
			zp->z_gid = new_gid;
			if (attrzp) {
				SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
				    SA_ZPL_GID(zfsvfs), NULL, &new_gid,
				    sizeof (new_gid));
				attrzp->z_gid = new_gid;
			}
		}
		if (!(mask & AT_MODE)) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs),
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

	if (mask & AT_MODE) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs), NULL,
		    &new_mode, sizeof (new_mode));
		zp->z_mode = new_mode;
		ASSERT3U((uintptr_t)aclp, !=, 0);
		err = zfs_aclset_common(zp, aclp, cr, tx);
		ASSERT0(err);
		if (zp->z_acl_cached)
			zfs_acl_free(zp->z_acl_cached);
		zp->z_acl_cached = aclp;
		aclp = NULL;
	}


	if (mask & AT_ATIME) {
		ZFS_TIME_ENCODE(&vap->va_atime, zp->z_atime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zfsvfs), NULL,
		    &zp->z_atime, sizeof (zp->z_atime));
	}

	if (mask & AT_MTIME) {
		ZFS_TIME_ENCODE(&vap->va_mtime, mtime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
		    mtime, sizeof (mtime));
	}

	/* XXX - shouldn't this be done *before* the ATIME/MTIME checks? */
	if (mask & AT_SIZE && !(mask & AT_MTIME)) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs),
		    NULL, mtime, sizeof (mtime));
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    &ctime, sizeof (ctime));
		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime,
		    B_TRUE);
	} else if (mask != 0) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    &ctime, sizeof (ctime));
		zfs_tstamp_update_setup(zp, STATE_CHANGED, mtime, ctime,
		    B_TRUE);
		if (attrzp) {
			SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
			    SA_ZPL_CTIME(zfsvfs), NULL,
			    &ctime, sizeof (ctime));
			zfs_tstamp_update_setup(attrzp, STATE_CHANGED,
			    mtime, ctime, B_TRUE);
		}
	}
	/*
	 * Do this after setting timestamps to prevent timestamp
	 * update from toggling bit
	 */

	if (xoap && (mask & AT_XVATTR)) {

		if (XVA_ISSET_REQ(xvap, XAT_CREATETIME))
			xoap->xoa_createtime = vap->va_birthtime;
		/*
		 * restore trimmed off masks
		 * so that return masks can be set for caller.
		 */

		if (XVA_ISSET_REQ(&tmpxvattr, XAT_APPENDONLY)) {
			XVA_SET_REQ(xvap, XAT_APPENDONLY);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_NOUNLINK)) {
			XVA_SET_REQ(xvap, XAT_NOUNLINK);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_IMMUTABLE)) {
			XVA_SET_REQ(xvap, XAT_IMMUTABLE);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_NODUMP)) {
			XVA_SET_REQ(xvap, XAT_NODUMP);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_AV_MODIFIED)) {
			XVA_SET_REQ(xvap, XAT_AV_MODIFIED);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_AV_QUARANTINED)) {
			XVA_SET_REQ(xvap, XAT_AV_QUARANTINED);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP))
			ASSERT(vp->v_type == VREG);

		zfs_xvattr_set(zp, xvap, tx);
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	if (mask != 0)
		zfs_log_setattr(zilog, tx, TX_SETATTR, zp, vap, mask, fuidp);

	if (mask & (AT_UID|AT_GID|AT_MODE))
		mutex_exit(&zp->z_acl_lock);

	if (attrzp) {
		if (mask & (AT_UID|AT_GID|AT_MODE))
			mutex_exit(&attrzp->z_acl_lock);
	}
out:
	if (err == 0 && attrzp) {
		err2 = sa_bulk_update(attrzp->z_sa_hdl, xattr_bulk,
		    xattr_count, tx);
		ASSERT(err2 == 0);
	}

	if (attrzp)
		vput(ZTOV(attrzp));

	if (aclp)
		zfs_acl_free(aclp);

	if (fuidp) {
		zfs_fuid_info_free(fuidp);
		fuidp = NULL;
	}

	if (err) {
		dmu_tx_abort(tx);
	} else {
		err2 = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		dmu_tx_commit(tx);
	}

out2:
	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (err);
}

/*
 * We acquire all but fdvp locks using non-blocking acquisitions.  If we
 * fail to acquire any lock in the path we will drop all held locks,
 * acquire the new lock in a blocking fashion, and then release it and
 * restart the rename.  This acquire/release step ensures that we do not
 * spin on a lock waiting for release.  On error release all vnode locks
 * and decrement references the way tmpfs_rename() would do.
 */
static int
zfs_rename_relock(struct vnode *sdvp, struct vnode **svpp,
    struct vnode *tdvp, struct vnode **tvpp,
    const struct componentname *scnp, const struct componentname *tcnp)
{
	zfsvfs_t	*zfsvfs;
	struct vnode	*nvp, *svp, *tvp;
	znode_t		*sdzp, *tdzp, *szp, *tzp;
	const char	*snm = scnp->cn_nameptr;
	const char	*tnm = tcnp->cn_nameptr;
	int error;

	VOP_UNLOCK(tdvp, 0);
	if (*tvpp != NULL && *tvpp != tdvp)
		VOP_UNLOCK(*tvpp, 0);

relock:
	error = vn_lock(sdvp, LK_EXCLUSIVE);
	if (error)
		goto out;
	sdzp = VTOZ(sdvp);

	error = vn_lock(tdvp, LK_EXCLUSIVE | LK_NOWAIT);
	if (error != 0) {
		VOP_UNLOCK(sdvp, 0);
		if (error != EBUSY)
			goto out;
		error = vn_lock(tdvp, LK_EXCLUSIVE);
		if (error)
			goto out;
		VOP_UNLOCK(tdvp, 0);
		goto relock;
	}
	tdzp = VTOZ(tdvp);

	/*
	 * Before using sdzp and tdzp we must ensure that they are live.
	 * As a porting legacy from illumos we have two things to worry
	 * about.  One is typical for FreeBSD and it is that the vnode is
	 * not reclaimed (doomed).  The other is that the znode is live.
	 * The current code can invalidate the znode without acquiring the
	 * corresponding vnode lock if the object represented by the znode
	 * and vnode is no longer valid after a rollback or receive operation.
	 * z_teardown_lock hidden behind ZFS_ENTER and ZFS_EXIT is the lock
	 * that protects the znodes from the invalidation.
	 */
	zfsvfs = sdzp->z_zfsvfs;
	ASSERT3P(zfsvfs, ==, tdzp->z_zfsvfs);
	ZFS_ENTER(zfsvfs);

	/*
	 * We can not use ZFS_VERIFY_ZP() here because it could directly return
	 * bypassing the cleanup code in the case of an error.
	 */
	if (tdzp->z_sa_hdl == NULL || sdzp->z_sa_hdl == NULL) {
		ZFS_EXIT(zfsvfs);
		VOP_UNLOCK(sdvp, 0);
		VOP_UNLOCK(tdvp, 0);
		error = SET_ERROR(EIO);
		goto out;
	}

	/*
	 * Re-resolve svp to be certain it still exists and fetch the
	 * correct vnode.
	 */
	error = zfs_dirent_lookup(sdzp, snm, &szp, ZEXISTS);
	if (error != 0) {
		/* Source entry invalid or not there. */
		ZFS_EXIT(zfsvfs);
		VOP_UNLOCK(sdvp, 0);
		VOP_UNLOCK(tdvp, 0);
		if ((scnp->cn_flags & ISDOTDOT) != 0 ||
		    (scnp->cn_namelen == 1 && scnp->cn_nameptr[0] == '.'))
			error = SET_ERROR(EINVAL);
		goto out;
	}
	svp = ZTOV(szp);

	/*
	 * Re-resolve tvp, if it disappeared we just carry on.
	 */
	error = zfs_dirent_lookup(tdzp, tnm, &tzp, 0);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		VOP_UNLOCK(sdvp, 0);
		VOP_UNLOCK(tdvp, 0);
		vrele(svp);
		if ((tcnp->cn_flags & ISDOTDOT) != 0)
			error = SET_ERROR(EINVAL);
		goto out;
	}
	if (tzp != NULL)
		tvp = ZTOV(tzp);
	else
		tvp = NULL;

	/*
	 * At present the vnode locks must be acquired before z_teardown_lock,
	 * although it would be more logical to use the opposite order.
	 */
	ZFS_EXIT(zfsvfs);

	/*
	 * Now try acquire locks on svp and tvp.
	 */
	nvp = svp;
	error = vn_lock(nvp, LK_EXCLUSIVE | LK_NOWAIT);
	if (error != 0) {
		VOP_UNLOCK(sdvp, 0);
		VOP_UNLOCK(tdvp, 0);
		if (tvp != NULL)
			vrele(tvp);
		if (error != EBUSY) {
			vrele(nvp);
			goto out;
		}
		error = vn_lock(nvp, LK_EXCLUSIVE);
		if (error != 0) {
			vrele(nvp);
			goto out;
		}
		VOP_UNLOCK(nvp, 0);
		/*
		 * Concurrent rename race.
		 * XXX ?
		 */
		if (nvp == tdvp) {
			vrele(nvp);
			error = SET_ERROR(EINVAL);
			goto out;
		}
		vrele(*svpp);
		*svpp = nvp;
		goto relock;
	}
	vrele(*svpp);
	*svpp = nvp;

	if (*tvpp != NULL)
		vrele(*tvpp);
	*tvpp = NULL;
	if (tvp != NULL) {
		nvp = tvp;
		error = vn_lock(nvp, LK_EXCLUSIVE | LK_NOWAIT);
		if (error != 0) {
			VOP_UNLOCK(sdvp, 0);
			VOP_UNLOCK(tdvp, 0);
			VOP_UNLOCK(*svpp, 0);
			if (error != EBUSY) {
				vrele(nvp);
				goto out;
			}
			error = vn_lock(nvp, LK_EXCLUSIVE);
			if (error != 0) {
				vrele(nvp);
				goto out;
			}
			vput(nvp);
			goto relock;
		}
		*tvpp = nvp;
	}

	return (0);

out:
	return (error);
}

/*
 * Note that we must use VRELE_ASYNC in this function as it walks
 * up the directory tree and vrele may need to acquire an exclusive
 * lock if a last reference to a vnode is dropped.
 */
static int
zfs_rename_check(znode_t *szp, znode_t *sdzp, znode_t *tdzp)
{
	zfsvfs_t	*zfsvfs;
	znode_t		*zp, *zp1;
	uint64_t	parent;
	int		error;

	zfsvfs = tdzp->z_zfsvfs;
	if (tdzp == szp)
		return (SET_ERROR(EINVAL));
	if (tdzp == sdzp)
		return (0);
	if (tdzp->z_id == zfsvfs->z_root)
		return (0);
	zp = tdzp;
	for (;;) {
		ASSERT(!zp->z_unlinked);
		if ((error = sa_lookup(zp->z_sa_hdl,
		    SA_ZPL_PARENT(zfsvfs), &parent, sizeof (parent))) != 0)
			break;

		if (parent == szp->z_id) {
			error = SET_ERROR(EINVAL);
			break;
		}
		if (parent == zfsvfs->z_root)
			break;
		if (parent == sdzp->z_id)
			break;

		error = zfs_zget(zfsvfs, parent, &zp1);
		if (error != 0)
			break;

		if (zp != tdzp)
			VN_RELE_ASYNC(ZTOV(zp),
			    dsl_pool_vnrele_taskq(dmu_objset_pool(zfsvfs->z_os)));
		zp = zp1;
	}

	if (error == ENOTDIR)
		panic("checkpath: .. not a directory\n");
	if (zp != tdzp)
		VN_RELE_ASYNC(ZTOV(zp),
		    dsl_pool_vnrele_taskq(dmu_objset_pool(zfsvfs->z_os)));
	return (error);
}

/*
 * Move an entry from the provided source directory to the target
 * directory.  Change the entry name as indicated.
 *
 *	IN:	sdvp	- Source directory containing the "old entry".
 *		snm	- Old entry name.
 *		tdvp	- Target directory to contain the "new entry".
 *		tnm	- New entry name.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	sdvp,tdvp - ctime|mtime updated
 */
/*ARGSUSED*/
static int
zfs_rename(vnode_t *sdvp, vnode_t **svpp, struct componentname *scnp,
    vnode_t *tdvp, vnode_t **tvpp, struct componentname *tcnp,
    cred_t *cr)
{
	zfsvfs_t	*zfsvfs;
	znode_t		*sdzp, *tdzp, *szp, *tzp;
	zilog_t		*zilog = NULL;
	dmu_tx_t	*tx;
	char		*snm = scnp->cn_nameptr;
	char		*tnm = tcnp->cn_nameptr;
	int		error = 0;

	/* Reject renames across filesystems. */
	if ((*svpp)->v_mount != tdvp->v_mount ||
	    ((*tvpp) != NULL && (*svpp)->v_mount != (*tvpp)->v_mount)) {
		error = SET_ERROR(EXDEV);
		goto out;
	}

	if (zfsctl_is_node(tdvp)) {
		error = SET_ERROR(EXDEV);
		goto out;
	}

	/*
	 * Lock all four vnodes to ensure safety and semantics of renaming.
	 */
	error = zfs_rename_relock(sdvp, svpp, tdvp, tvpp, scnp, tcnp);
	if (error != 0) {
		/* no vnodes are locked in the case of error here */
		return (error);
	}

	tdzp = VTOZ(tdvp);
	sdzp = VTOZ(sdvp);
	zfsvfs = tdzp->z_zfsvfs;
	zilog = zfsvfs->z_log;

	/*
	 * After we re-enter ZFS_ENTER() we will have to revalidate all
	 * znodes involved.
	 */
	ZFS_ENTER(zfsvfs);

	if (zfsvfs->z_utf8 && u8_validate(tnm,
	    strlen(tnm), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		error = SET_ERROR(EILSEQ);
		goto unlockout;
	}

	/* If source and target are the same file, there is nothing to do. */
	if ((*svpp) == (*tvpp)) {
		error = 0;
		goto unlockout;
	}

	if (((*svpp)->v_type == VDIR && (*svpp)->v_mountedhere != NULL) ||
	    ((*tvpp) != NULL && (*tvpp)->v_type == VDIR &&
	    (*tvpp)->v_mountedhere != NULL)) {
		error = SET_ERROR(EXDEV);
		goto unlockout;
	}

	/*
	 * We can not use ZFS_VERIFY_ZP() here because it could directly return
	 * bypassing the cleanup code in the case of an error.
	 */
	if (tdzp->z_sa_hdl == NULL || sdzp->z_sa_hdl == NULL) {
		error = SET_ERROR(EIO);
		goto unlockout;
	}

	szp = VTOZ(*svpp);
	tzp = *tvpp == NULL ? NULL : VTOZ(*tvpp);
	if (szp->z_sa_hdl == NULL || (tzp != NULL && tzp->z_sa_hdl == NULL)) {
		error = SET_ERROR(EIO);
		goto unlockout;
	}

	/*
	 * This is to prevent the creation of links into attribute space
	 * by renaming a linked file into/outof an attribute directory.
	 * See the comment in zfs_link() for why this is considered bad.
	 */
	if ((tdzp->z_pflags & ZFS_XATTR) != (sdzp->z_pflags & ZFS_XATTR)) {
		error = SET_ERROR(EINVAL);
		goto unlockout;
	}

	/*
	 * Must have write access at the source to remove the old entry
	 * and write access at the target to create the new entry.
	 * Note that if target and source are the same, this can be
	 * done in a single check.
	 */
	if (error = zfs_zaccess_rename(sdzp, szp, tdzp, tzp, cr))
		goto unlockout;

	if ((*svpp)->v_type == VDIR) {
		/*
		 * Avoid ".", "..", and aliases of "." for obvious reasons.
		 */
		if ((scnp->cn_namelen == 1 && scnp->cn_nameptr[0] == '.') ||
		    sdzp == szp ||
		    (scnp->cn_flags | tcnp->cn_flags) & ISDOTDOT) {
			error = EINVAL;
			goto unlockout;
		}

		/*
		 * Check to make sure rename is valid.
		 * Can't do a move like this: /usr/a/b to /usr/a/b/c/d
		 */
		if (error = zfs_rename_check(szp, sdzp, tdzp))
			goto unlockout;
	}

	/*
	 * Does target exist?
	 */
	if (tzp) {
		/*
		 * Source and target must be the same type.
		 */
		if ((*svpp)->v_type == VDIR) {
			if ((*tvpp)->v_type != VDIR) {
				error = SET_ERROR(ENOTDIR);
				goto unlockout;
			} else {
				cache_purge(tdvp);
				if (sdvp != tdvp)
					cache_purge(sdvp);
			}
		} else {
			if ((*tvpp)->v_type == VDIR) {
				error = SET_ERROR(EISDIR);
				goto unlockout;
			}
		}
	}

	vnevent_rename_src(*svpp, sdvp, scnp->cn_nameptr, ct);
	if (tzp)
		vnevent_rename_dest(*tvpp, tdvp, tnm, ct);

	/*
	 * notify the target directory if it is not the same
	 * as source directory.
	 */
	if (tdvp != sdvp) {
		vnevent_rename_dest_dir(tdvp, ct);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
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
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		goto unlockout;
	}


	if (tzp)	/* Attempt to remove the existing target */
		error = zfs_link_destroy(tdzp, tnm, tzp, tx, 0, NULL);

	if (error == 0) {
		error = zfs_link_create(tdzp, tnm, szp, tx, ZRENAMING);
		if (error == 0) {
			szp->z_pflags |= ZFS_AV_MODIFIED;

			error = sa_update(szp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
			    (void *)&szp->z_pflags, sizeof (uint64_t), tx);
			ASSERT0(error);

			error = zfs_link_destroy(sdzp, snm, szp, tx, ZRENAMING,
			    NULL);
			if (error == 0) {
				zfs_log_rename(zilog, tx, TX_RENAME, sdzp,
				    snm, tdzp, tnm, szp);

				/*
				 * Update path information for the target vnode
				 */
				vn_renamepath(tdvp, *svpp, tnm, strlen(tnm));
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
				VERIFY3U(zfs_link_destroy(tdzp, tnm, szp, tx,
				    ZRENAMING, NULL), ==, 0);
			}
		}
		if (error == 0) {
			cache_purge(*svpp);
			if (*tvpp != NULL)
				cache_purge(*tvpp);
			cache_purge_negative(tdvp);
		}
	}

	dmu_tx_commit(tx);

unlockout:			/* all 4 vnodes are locked, ZFS_ENTER called */
	ZFS_EXIT(zfsvfs);
	VOP_UNLOCK(*svpp, 0);
	VOP_UNLOCK(sdvp, 0);

out:				/* original two vnodes are locked */
	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	if (*tvpp != NULL)
		VOP_UNLOCK(*tvpp, 0);
	if (tdvp != *tvpp)
		VOP_UNLOCK(tdvp, 0);
	return (error);
}

/*
 * Insert the indicated symbolic reference entry into the directory.
 *
 *	IN:	dvp	- Directory to contain new symbolic link.
 *		link	- Name for new symlink entry.
 *		vap	- Attributes of new entry.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dvp - ctime|mtime updated
 */
/*ARGSUSED*/
static int
zfs_symlink(vnode_t *dvp, vnode_t **vpp, char *name, vattr_t *vap, char *link,
    cred_t *cr, kthread_t *td)
{
	znode_t		*zp, *dzp = VTOZ(dvp);
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	uint64_t	len = strlen(link);
	int		error;
	zfs_acl_ids_t	acl_ids;
	boolean_t	fuid_dirtied;
	uint64_t	txtype = TX_SYMLINK;
	int		flags = 0;

	ASSERT(vap->va_type == VLNK);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EILSEQ));
	}

	if (len > MAXPATHLEN) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(ENAMETOOLONG));
	}

	if ((error = zfs_acl_ids_create(dzp, 0,
	    vap, cr, NULL, &acl_ids)) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lookup(dzp, name, &zp, ZNEW);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr)) {
		zfs_acl_ids_free(&acl_ids);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids)) {
		zfs_acl_ids_free(&acl_ids);
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EDQUOT));
	}

	getnewvnode_reserve(1);
	tx = dmu_tx_create(zfsvfs->z_os);
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, MAX(1, len));
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE + len);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
	if (!zfsvfs->z_use_sa && acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    acl_ids.z_aclp->z_acl_bytes);
	}
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		getnewvnode_drop_reserve();
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Create a new object for the symlink.
	 * for version 4 ZPL datsets the symlink will be an SA attribute
	 */
	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	if (zp->z_is_sa)
		error = sa_update(zp->z_sa_hdl, SA_ZPL_SYMLINK(zfsvfs),
		    link, len, tx);
	else
		zfs_sa_symlink(zp, link, len, tx);

	zp->z_size = len;
	(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
	    &zp->z_size, sizeof (zp->z_size), tx);
	/*
	 * Insert the new object into the directory.
	 */
	(void) zfs_link_create(dzp, name, zp, tx, ZNEW);

	zfs_log_symlink(zilog, tx, txtype, dzp, zp, name, link);
	*vpp = ZTOV(zp);

	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);

	getnewvnode_drop_reserve();

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Return, in the buffer contained in the provided uio structure,
 * the symbolic path referred to by vp.
 *
 *	IN:	vp	- vnode of symbolic link.
 *		uio	- structure to contain the link path.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	OUT:	uio	- structure containing the link path.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	vp - atime updated
 */
/* ARGSUSED */
static int
zfs_readlink(vnode_t *vp, uio_t *uio, cred_t *cr, caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	int		error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (zp->z_is_sa)
		error = sa_lookup_uio(zp->z_sa_hdl,
		    SA_ZPL_SYMLINK(zfsvfs), uio);
	else
		error = zfs_sa_readlink(zp, uio);

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Insert a new entry into directory tdvp referencing svp.
 *
 *	IN:	tdvp	- Directory to contain new entry.
 *		svp	- vnode of new entry.
 *		name	- name of new entry.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	tdvp - ctime|mtime updated
 *	 svp - ctime updated
 */
/* ARGSUSED */
static int
zfs_link(vnode_t *tdvp, vnode_t *svp, char *name, cred_t *cr,
    caller_context_t *ct, int flags)
{
	znode_t		*dzp = VTOZ(tdvp);
	znode_t		*tzp, *szp;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	dmu_tx_t	*tx;
	int		error;
	uint64_t	parent;
	uid_t		owner;

	ASSERT(tdvp->v_type == VDIR);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	/*
	 * POSIX dictates that we return EPERM here.
	 * Better choices include ENOTSUP or EISDIR.
	 */
	if (svp->v_type == VDIR) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	szp = VTOZ(svp);
	ZFS_VERIFY_ZP(szp);

	if (szp->z_pflags & (ZFS_APPENDONLY | ZFS_IMMUTABLE | ZFS_READONLY)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	/* Prevent links to .zfs/shares files */

	if ((error = sa_lookup(szp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (uint64_t))) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}
	if (parent == zfsvfs->z_shares_dir) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	if (zfsvfs->z_utf8 && u8_validate(name,
	    strlen(name), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EILSEQ));
	}

	/*
	 * We do not support links between attributes and non-attributes
	 * because of the potential security risk of creating links
	 * into "normal" file space in order to circumvent restrictions
	 * imposed in attribute space.
	 */
	if ((szp->z_pflags & ZFS_XATTR) != (dzp->z_pflags & ZFS_XATTR)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}


	owner = zfs_fuid_map_id(zfsvfs, szp->z_uid, cr, ZFS_OWNER);
	if (owner != crgetuid(cr) && secpolicy_basic_link(svp, cr) != 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	if (error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lookup(dzp, name, &tzp, ZNEW);
	if (error) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	zfs_sa_upgrade_txholds(tx, szp);
	zfs_sa_upgrade_txholds(tx, dzp);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	error = zfs_link_create(dzp, name, szp, tx, 0);

	if (error == 0) {
		uint64_t txtype = TX_LINK;
		zfs_log_link(zilog, tx, txtype, dzp, szp, name);
	}

	dmu_tx_commit(tx);

	if (error == 0) {
		vnevent_link(svp, ct);
	}

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}


/*ARGSUSED*/
void
zfs_inactive(vnode_t *vp, cred_t *cr, caller_context_t *ct)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;

	rw_enter(&zfsvfs->z_teardown_inactive_lock, RW_READER);
	if (zp->z_sa_hdl == NULL) {
		/*
		 * The fs has been unmounted, or we did a
		 * suspend/resume and this file no longer exists.
		 */
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		vrecycle(vp);
		return;
	}

	if (zp->z_unlinked) {
		/*
		 * Fast path to recycle a vnode of a removed file.
		 */
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		vrecycle(vp);
		return;
	}

	if (zp->z_atime_dirty && zp->z_unlinked == 0) {
		dmu_tx_t *tx = dmu_tx_create(zfsvfs->z_os);

		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, zp);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
		} else {
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_ATIME(zfsvfs),
			    (void *)&zp->z_atime, sizeof (zp->z_atime), tx);
			zp->z_atime_dirty = 0;
			dmu_tx_commit(tx);
		}
	}
	rw_exit(&zfsvfs->z_teardown_inactive_lock);
}


CTASSERT(sizeof(struct zfid_short) <= sizeof(struct fid));
CTASSERT(sizeof(struct zfid_long) <= sizeof(struct fid));

/*ARGSUSED*/
static int
zfs_fid(vnode_t *vp, fid_t *fidp, caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	uint32_t	gen;
	uint64_t	gen64;
	uint64_t	object = zp->z_id;
	zfid_short_t	*zfid;
	int		size, i, error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_GEN(zfsvfs),
	    &gen64, sizeof (uint64_t))) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	gen = (uint32_t)gen64;

	size = (zfsvfs->z_parent != zfsvfs) ? LONG_FID_LEN : SHORT_FID_LEN;

#ifdef illumos
	if (fidp->fid_len < size) {
		fidp->fid_len = size;
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(ENOSPC));
	}
#else
	fidp->fid_len = size;
#endif

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
		uint64_t	objsetid = dmu_objset_id(zfsvfs->z_os);
		zfid_long_t	*zlfid;

		zlfid = (zfid_long_t *)fidp;

		for (i = 0; i < sizeof (zlfid->zf_setid); i++)
			zlfid->zf_setid[i] = (uint8_t)(objsetid >> (8 * i));

		/* XXX - this should be the generation number for the objset */
		for (i = 0; i < sizeof (zlfid->zf_setgen); i++)
			zlfid->zf_setgen[i] = 0;
	}

	ZFS_EXIT(zfsvfs);
	return (0);
}

static int
zfs_pathconf(vnode_t *vp, int cmd, ulong_t *valp, cred_t *cr,
    caller_context_t *ct)
{
	znode_t		*zp, *xzp;
	zfsvfs_t	*zfsvfs;
	int		error;

	switch (cmd) {
	case _PC_LINK_MAX:
		*valp = MIN(LONG_MAX, ZFS_LINK_MAX);
		return (0);

	case _PC_FILESIZEBITS:
		*valp = 64;
		return (0);
#ifdef illumos
	case _PC_XATTR_EXISTS:
		zp = VTOZ(vp);
		zfsvfs = zp->z_zfsvfs;
		ZFS_ENTER(zfsvfs);
		ZFS_VERIFY_ZP(zp);
		*valp = 0;
		error = zfs_dirent_lookup(zp, "", &xzp,
		    ZXATTR | ZEXISTS | ZSHARED);
		if (error == 0) {
			if (!zfs_dirempty(xzp))
				*valp = 1;
			vrele(ZTOV(xzp));
		} else if (error == ENOENT) {
			/*
			 * If there aren't extended attributes, it's the
			 * same as having zero of them.
			 */
			error = 0;
		}
		ZFS_EXIT(zfsvfs);
		return (error);

	case _PC_SATTR_ENABLED:
	case _PC_SATTR_EXISTS:
		*valp = vfs_has_feature(vp->v_vfsp, VFSFT_SYSATTR_VIEWS) &&
		    (vp->v_type == VREG || vp->v_type == VDIR);
		return (0);

	case _PC_ACCESS_FILTERING:
		*valp = vfs_has_feature(vp->v_vfsp, VFSFT_ACCESS_FILTER) &&
		    vp->v_type == VDIR;
		return (0);

	case _PC_ACL_ENABLED:
		*valp = _ACL_ACE_ENABLED;
		return (0);
#endif	/* illumos */
	case _PC_MIN_HOLE_SIZE:
		*valp = (int)SPA_MINBLOCKSIZE;
		return (0);
#ifdef illumos
	case _PC_TIMESTAMP_RESOLUTION:
		/* nanosecond timestamp resolution */
		*valp = 1L;
		return (0);
#endif
	case _PC_ACL_EXTENDED:
		*valp = 0;
		return (0);

	case _PC_ACL_NFS4:
		*valp = 1;
		return (0);

	case _PC_ACL_PATH_MAX:
		*valp = ACL_MAX_ENTRIES;
		return (0);

	default:
		return (EOPNOTSUPP);
	}
}

/*ARGSUSED*/
static int
zfs_getsecattr(vnode_t *vp, vsecattr_t *vsecp, int flag, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;
	boolean_t skipaclchk = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	error = zfs_getacl(zp, vsecp, skipaclchk, cr);
	ZFS_EXIT(zfsvfs);

	return (error);
}

/*ARGSUSED*/
int
zfs_setsecattr(vnode_t *vp, vsecattr_t *vsecp, int flag, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;
	boolean_t skipaclchk = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	zilog_t	*zilog = zfsvfs->z_log;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	error = zfs_setacl(zp, vsecp, skipaclchk, cr);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

static int
zfs_getpages(struct vnode *vp, vm_page_t *ma, int count, int *rbehind,
    int *rahead)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	objset_t *os = zp->z_zfsvfs->z_os;
	rl_t *rl;
	vm_object_t object;
	off_t start, end, obj_size;
	uint_t blksz;
	int pgsin_b, pgsin_a;
	int error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	start = IDX_TO_OFF(ma[0]->pindex);
	end = IDX_TO_OFF(ma[count - 1]->pindex + 1);

	/*
	 * Lock a range covering all required and optional pages.
	 * Note that we need to handle the case of the block size growing.
	 */
	for (;;) {
		blksz = zp->z_blksz;
		rl = zfs_range_lock(zp, rounddown(start, blksz),
		    roundup(end, blksz) - rounddown(start, blksz), RL_READER);
		if (blksz == zp->z_blksz)
			break;
		zfs_range_unlock(rl);
	}

	object = ma[0]->object;
	zfs_vmobject_wlock(object);
	obj_size = object->un_pager.vnp.vnp_size;
	zfs_vmobject_wunlock(object);
	if (IDX_TO_OFF(ma[count - 1]->pindex) >= obj_size) {
		zfs_range_unlock(rl);
		ZFS_EXIT(zfsvfs);
		return (zfs_vm_pagerret_bad);
	}

	pgsin_b = 0;
	if (rbehind != NULL) {
		pgsin_b = OFF_TO_IDX(start - rounddown(start, blksz));
		pgsin_b = MIN(*rbehind, pgsin_b);
	}

	pgsin_a = 0;
	if (rahead != NULL) {
		pgsin_a = OFF_TO_IDX(roundup(end, blksz) - end);
		if (end + IDX_TO_OFF(pgsin_a) >= obj_size)
			pgsin_a = OFF_TO_IDX(round_page(obj_size) - end);
		pgsin_a = MIN(*rahead, pgsin_a);
	}

	/*
	 * NB: we need to pass the exact byte size of the data that we expect
	 * to read after accounting for the file size.  This is required because
	 * ZFS will panic if we request DMU to read beyond the end of the last
	 * allocated block.
	 */
	error = dmu_read_pages(os, zp->z_id, ma, count, &pgsin_b, &pgsin_a,
	    MIN(end, obj_size) - (end - PAGE_SIZE));

	zfs_range_unlock(rl);
	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);
	ZFS_EXIT(zfsvfs);

	if (error != 0)
		return (zfs_vm_pagerret_error);

	VM_CNT_INC(v_vnodein);
	VM_CNT_ADD(v_vnodepgsin, count + pgsin_b + pgsin_a);
	if (rbehind != NULL)
		*rbehind = pgsin_b;
	if (rahead != NULL)
		*rahead = pgsin_a;
	return (zfs_vm_pagerret_ok);
}

static int
zfs_freebsd_getpages(ap)
	struct vop_getpages_args /* {
		struct vnode *a_vp;
		vm_page_t *a_m;
		int a_count;
		int *a_rbehind;
		int *a_rahead;
	} */ *ap;
{

	return (zfs_getpages(ap->a_vp, ap->a_m, ap->a_count, ap->a_rbehind,
	    ap->a_rahead));
}

static int
zfs_putpages(struct vnode *vp, vm_page_t *ma, size_t len, int flags,
    int *rtvals)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	rl_t		*rl;
	dmu_tx_t	*tx;
	struct sf_buf	*sf;
	vm_object_t	object;
	vm_page_t	m;
	caddr_t		va;
	size_t		tocopy;
	size_t		lo_len;
	vm_ooffset_t	lo_off;
	vm_ooffset_t	off;
	uint_t		blksz;
	int		ncount;
	int		pcount;
	int		err;
	int		i;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	object = vp->v_object;
	pcount = btoc(len);
	ncount = pcount;

	KASSERT(ma[0]->object == object, ("mismatching object"));
	KASSERT(len > 0 && (len & PAGE_MASK) == 0, ("unexpected length"));

	for (i = 0; i < pcount; i++)
		rtvals[i] = zfs_vm_pagerret_error;

	off = IDX_TO_OFF(ma[0]->pindex);
	blksz = zp->z_blksz;
	lo_off = rounddown(off, blksz);
	lo_len = roundup(len + (off - lo_off), blksz);
	rl = zfs_range_lock(zp, lo_off, lo_len, RL_WRITER);

	zfs_vmobject_wlock(object);
	if (len + off > object->un_pager.vnp.vnp_size) {
		if (object->un_pager.vnp.vnp_size > off) {
			int pgoff;

			len = object->un_pager.vnp.vnp_size - off;
			ncount = btoc(len);
			if ((pgoff = (int)len & PAGE_MASK) != 0) {
				/*
				 * If the object is locked and the following
				 * conditions hold, then the page's dirty
				 * field cannot be concurrently changed by a
				 * pmap operation.
				 */
				m = ma[ncount - 1];
				vm_page_assert_sbusied(m);
				KASSERT(!pmap_page_is_write_mapped(m),
				    ("zfs_putpages: page %p is not read-only", m));
				vm_page_clear_dirty(m, pgoff, PAGE_SIZE -
				    pgoff);
			}
		} else {
			len = 0;
			ncount = 0;
		}
		if (ncount < pcount) {
			for (i = ncount; i < pcount; i++) {
				rtvals[i] = zfs_vm_pagerret_bad;
			}
		}
	}
	zfs_vmobject_wunlock(object);

	if (ncount == 0)
		goto out;

	if (zfs_owner_overquota(zfsvfs, zp, B_FALSE) ||
	    zfs_owner_overquota(zfsvfs, zp, B_TRUE)) {
		goto out;
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_write(tx, zp->z_id, off, len);

	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		goto out;
	}

	if (zp->z_blksz < PAGE_SIZE) {
		for (i = 0; len > 0; off += tocopy, len -= tocopy, i++) {
			tocopy = len > PAGE_SIZE ? PAGE_SIZE : len;
			va = zfs_map_page(ma[i], &sf);
			dmu_write(zfsvfs->z_os, zp->z_id, off, tocopy, va, tx);
			zfs_unmap_page(sf);
		}
	} else {
		err = dmu_write_pages(zfsvfs->z_os, zp->z_id, off, len, ma, tx);
	}

	if (err == 0) {
		uint64_t mtime[2], ctime[2];
		sa_bulk_attr_t bulk[3];
		int count = 0;

		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
		    &mtime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    &ctime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
		    &zp->z_pflags, 8);
		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime,
		    B_TRUE);
		err = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		ASSERT0(err);
		zfs_log_write(zfsvfs->z_log, tx, TX_WRITE, zp, off, len, 0);

		zfs_vmobject_wlock(object);
		for (i = 0; i < ncount; i++) {
			rtvals[i] = zfs_vm_pagerret_ok;
			vm_page_undirty(ma[i]);
		}
		zfs_vmobject_wunlock(object);
		VM_CNT_INC(v_vnodeout);
		VM_CNT_ADD(v_vnodepgsout, ncount);
	}
	dmu_tx_commit(tx);

out:
	zfs_range_unlock(rl);
	if ((flags & (zfs_vm_pagerput_sync | zfs_vm_pagerput_inval)) != 0 ||
	    zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zfsvfs->z_log, zp->z_id);
	ZFS_EXIT(zfsvfs);
	return (rtvals[0]);
}

int
zfs_freebsd_putpages(ap)
	struct vop_putpages_args /* {
		struct vnode *a_vp;
		vm_page_t *a_m;
		int a_count;
		int a_sync;
		int *a_rtvals;
	} */ *ap;
{

	return (zfs_putpages(ap->a_vp, ap->a_m, ap->a_count, ap->a_sync,
	    ap->a_rtvals));
}

static int
zfs_freebsd_bmap(ap)
	struct vop_bmap_args /* {
		struct vnode *a_vp;
		daddr_t  a_bn;
		struct bufobj **a_bop;
		daddr_t *a_bnp;
		int *a_runp;
		int *a_runb;
	} */ *ap;
{

	if (ap->a_bop != NULL)
		*ap->a_bop = &ap->a_vp->v_bufobj;
	if (ap->a_bnp != NULL)
		*ap->a_bnp = ap->a_bn;
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;

	return (0);
}

static int
zfs_freebsd_open(ap)
	struct vop_open_args /* {
		struct vnode *a_vp;
		int a_mode;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	vnode_t	*vp = ap->a_vp;
	znode_t *zp = VTOZ(vp);
	int error;

	error = zfs_open(&vp, ap->a_mode, ap->a_cred, NULL);
	if (error == 0)
		vnode_create_vobject(vp, zp->z_size, ap->a_td);
	return (error);
}

static int
zfs_freebsd_close(ap)
	struct vop_close_args /* {
		struct vnode *a_vp;
		int  a_fflag;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{

	return (zfs_close(ap->a_vp, ap->a_fflag, 1, 0, ap->a_cred, NULL));
}

static int
zfs_freebsd_ioctl(ap)
	struct vop_ioctl_args /* {
		struct vnode *a_vp;
		u_long a_command;
		caddr_t a_data;
		int a_fflag;
		struct ucred *cred;
		struct thread *td;
	} */ *ap;
{

	return (zfs_ioctl(ap->a_vp, ap->a_command, (intptr_t)ap->a_data,
	    ap->a_fflag, ap->a_cred, NULL, NULL));
}

static int
ioflags(int ioflags)
{
	int flags = 0;

	if (ioflags & IO_APPEND)
		flags |= FAPPEND;
	if (ioflags & IO_NDELAY)
		flags |= FNONBLOCK;
	if (ioflags & IO_SYNC)
		flags |= (FSYNC | FDSYNC | FRSYNC);

	return (flags);
}

static int
zfs_freebsd_read(ap)
	struct vop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{

	return (zfs_read(ap->a_vp, ap->a_uio, ioflags(ap->a_ioflag),
	    ap->a_cred, NULL));
}

static int
zfs_freebsd_write(ap)
	struct vop_write_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{

	return (zfs_write(ap->a_vp, ap->a_uio, ioflags(ap->a_ioflag),
	    ap->a_cred, NULL));
}

static int
zfs_freebsd_access(ap)
	struct vop_access_args /* {
		struct vnode *a_vp;
		accmode_t a_accmode;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	vnode_t *vp = ap->a_vp;
	znode_t *zp = VTOZ(vp);
	accmode_t accmode;
	int error = 0;

	/*
	 * ZFS itself only knowns about VREAD, VWRITE, VEXEC and VAPPEND,
	 */
	accmode = ap->a_accmode & (VREAD|VWRITE|VEXEC|VAPPEND);
	if (accmode != 0)
		error = zfs_access(ap->a_vp, accmode, 0, ap->a_cred, NULL);

	/*
	 * VADMIN has to be handled by vaccess().
	 */
	if (error == 0) {
		accmode = ap->a_accmode & ~(VREAD|VWRITE|VEXEC|VAPPEND);
		if (accmode != 0) {
			error = vaccess(vp->v_type, zp->z_mode, zp->z_uid,
			    zp->z_gid, accmode, ap->a_cred, NULL);
		}
	}

	/*
	 * For VEXEC, ensure that at least one execute bit is set for
	 * non-directories.
	 */
	if (error == 0 && (ap->a_accmode & VEXEC) != 0 && vp->v_type != VDIR &&
	    (zp->z_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0) {
		error = EACCES;
	}

	return (error);
}

static int
zfs_freebsd_lookup(ap)
	struct vop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct componentname *cnp = ap->a_cnp;
	char nm[NAME_MAX + 1];

	ASSERT(cnp->cn_namelen < sizeof(nm));
	strlcpy(nm, cnp->cn_nameptr, MIN(cnp->cn_namelen + 1, sizeof(nm)));

	return (zfs_lookup(ap->a_dvp, nm, ap->a_vpp, cnp, cnp->cn_nameiop,
	    cnp->cn_cred, cnp->cn_thread, 0));
}

static int
zfs_cache_lookup(ap)
	struct vop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
	zfsvfs_t *zfsvfs;

	zfsvfs = ap->a_dvp->v_mount->mnt_data;
	if (zfsvfs->z_use_namecache)
		return (vfs_cache_lookup(ap));
	else
		return (zfs_freebsd_lookup(ap));
}

static int
zfs_freebsd_create(ap)
	struct vop_create_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
	} */ *ap;
{
	zfsvfs_t *zfsvfs;
	struct componentname *cnp = ap->a_cnp;
	vattr_t *vap = ap->a_vap;
	int error, mode;

	ASSERT(cnp->cn_flags & SAVENAME);

	vattr_init_mask(vap);
	mode = vap->va_mode & ALLPERMS;
	zfsvfs = ap->a_dvp->v_mount->mnt_data;

	error = zfs_create(ap->a_dvp, cnp->cn_nameptr, vap, !EXCL, mode,
	    ap->a_vpp, cnp->cn_cred, cnp->cn_thread);
	if (zfsvfs->z_use_namecache &&
	    error == 0 && (cnp->cn_flags & MAKEENTRY) != 0)
		cache_enter(ap->a_dvp, *ap->a_vpp, cnp);
	return (error);
}

static int
zfs_freebsd_remove(ap)
	struct vop_remove_args /* {
		struct vnode *a_dvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap;
{

	ASSERT(ap->a_cnp->cn_flags & SAVENAME);

	return (zfs_remove(ap->a_dvp, ap->a_vp, ap->a_cnp->cn_nameptr,
	    ap->a_cnp->cn_cred));
}

static int
zfs_freebsd_mkdir(ap)
	struct vop_mkdir_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
	} */ *ap;
{
	vattr_t *vap = ap->a_vap;

	ASSERT(ap->a_cnp->cn_flags & SAVENAME);

	vattr_init_mask(vap);

	return (zfs_mkdir(ap->a_dvp, ap->a_cnp->cn_nameptr, vap, ap->a_vpp,
	    ap->a_cnp->cn_cred));
}

static int
zfs_freebsd_rmdir(ap)
	struct vop_rmdir_args /* {
		struct vnode *a_dvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct componentname *cnp = ap->a_cnp;

	ASSERT(cnp->cn_flags & SAVENAME);

	return (zfs_rmdir(ap->a_dvp, ap->a_vp, cnp->cn_nameptr, cnp->cn_cred));
}

static int
zfs_freebsd_readdir(ap)
	struct vop_readdir_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
		int *a_eofflag;
		int *a_ncookies;
		u_long **a_cookies;
	} */ *ap;
{

	return (zfs_readdir(ap->a_vp, ap->a_uio, ap->a_cred, ap->a_eofflag,
	    ap->a_ncookies, ap->a_cookies));
}

static int
zfs_freebsd_fsync(ap)
	struct vop_fsync_args /* {
		struct vnode *a_vp;
		int a_waitfor;
		struct thread *a_td;
	} */ *ap;
{

	vop_stdfsync(ap);
	return (zfs_fsync(ap->a_vp, 0, ap->a_td->td_ucred, NULL));
}

static int
zfs_freebsd_getattr(ap)
	struct vop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
	} */ *ap;
{
	vattr_t *vap = ap->a_vap;
	xvattr_t xvap;
	u_long fflags = 0;
	int error;

	xva_init(&xvap);
	xvap.xva_vattr = *vap;
	xvap.xva_vattr.va_mask |= AT_XVATTR;

	/* Convert chflags into ZFS-type flags. */
	/* XXX: what about SF_SETTABLE?. */
	XVA_SET_REQ(&xvap, XAT_IMMUTABLE);
	XVA_SET_REQ(&xvap, XAT_APPENDONLY);
	XVA_SET_REQ(&xvap, XAT_NOUNLINK);
	XVA_SET_REQ(&xvap, XAT_NODUMP);
	XVA_SET_REQ(&xvap, XAT_READONLY);
	XVA_SET_REQ(&xvap, XAT_ARCHIVE);
	XVA_SET_REQ(&xvap, XAT_SYSTEM);
	XVA_SET_REQ(&xvap, XAT_HIDDEN);
	XVA_SET_REQ(&xvap, XAT_REPARSE);
	XVA_SET_REQ(&xvap, XAT_OFFLINE);
	XVA_SET_REQ(&xvap, XAT_SPARSE);

	error = zfs_getattr(ap->a_vp, (vattr_t *)&xvap, 0, ap->a_cred, NULL);
	if (error != 0)
		return (error);

	/* Convert ZFS xattr into chflags. */
#define	FLAG_CHECK(fflag, xflag, xfield)	do {			\
	if (XVA_ISSET_RTN(&xvap, (xflag)) && (xfield) != 0)		\
		fflags |= (fflag);					\
} while (0)
	FLAG_CHECK(SF_IMMUTABLE, XAT_IMMUTABLE,
	    xvap.xva_xoptattrs.xoa_immutable);
	FLAG_CHECK(SF_APPEND, XAT_APPENDONLY,
	    xvap.xva_xoptattrs.xoa_appendonly);
	FLAG_CHECK(SF_NOUNLINK, XAT_NOUNLINK,
	    xvap.xva_xoptattrs.xoa_nounlink);
	FLAG_CHECK(UF_ARCHIVE, XAT_ARCHIVE,
	    xvap.xva_xoptattrs.xoa_archive);
	FLAG_CHECK(UF_NODUMP, XAT_NODUMP,
	    xvap.xva_xoptattrs.xoa_nodump);
	FLAG_CHECK(UF_READONLY, XAT_READONLY,
	    xvap.xva_xoptattrs.xoa_readonly);
	FLAG_CHECK(UF_SYSTEM, XAT_SYSTEM,
	    xvap.xva_xoptattrs.xoa_system);
	FLAG_CHECK(UF_HIDDEN, XAT_HIDDEN,
	    xvap.xva_xoptattrs.xoa_hidden);
	FLAG_CHECK(UF_REPARSE, XAT_REPARSE,
	    xvap.xva_xoptattrs.xoa_reparse);
	FLAG_CHECK(UF_OFFLINE, XAT_OFFLINE,
	    xvap.xva_xoptattrs.xoa_offline);
	FLAG_CHECK(UF_SPARSE, XAT_SPARSE,
	    xvap.xva_xoptattrs.xoa_sparse);

#undef	FLAG_CHECK
	*vap = xvap.xva_vattr;
	vap->va_flags = fflags;
	return (0);
}

static int
zfs_freebsd_setattr(ap)
	struct vop_setattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
	} */ *ap;
{
	vnode_t *vp = ap->a_vp;
	vattr_t *vap = ap->a_vap;
	cred_t *cred = ap->a_cred;
	xvattr_t xvap;
	u_long fflags;
	uint64_t zflags;

	vattr_init_mask(vap);
	vap->va_mask &= ~AT_NOSET;

	xva_init(&xvap);
	xvap.xva_vattr = *vap;

	zflags = VTOZ(vp)->z_pflags;

	if (vap->va_flags != VNOVAL) {
		zfsvfs_t *zfsvfs = VTOZ(vp)->z_zfsvfs;
		int error;

		if (zfsvfs->z_use_fuids == B_FALSE)
			return (EOPNOTSUPP);

		fflags = vap->va_flags;
		/*
		 * XXX KDM 
		 * We need to figure out whether it makes sense to allow
		 * UF_REPARSE through, since we don't really have other
		 * facilities to handle reparse points and zfs_setattr()
		 * doesn't currently allow setting that attribute anyway.
		 */
		if ((fflags & ~(SF_IMMUTABLE|SF_APPEND|SF_NOUNLINK|UF_ARCHIVE|
		     UF_NODUMP|UF_SYSTEM|UF_HIDDEN|UF_READONLY|UF_REPARSE|
		     UF_OFFLINE|UF_SPARSE)) != 0)
			return (EOPNOTSUPP);
		/*
		 * Unprivileged processes are not permitted to unset system
		 * flags, or modify flags if any system flags are set.
		 * Privileged non-jail processes may not modify system flags
		 * if securelevel > 0 and any existing system flags are set.
		 * Privileged jail processes behave like privileged non-jail
		 * processes if the PR_ALLOW_CHFLAGS permission bit is set;
		 * otherwise, they behave like unprivileged processes.
		 */
		if (secpolicy_fs_owner(vp->v_mount, cred) == 0 ||
		    priv_check_cred(cred, PRIV_VFS_SYSFLAGS) == 0) {
			if (zflags &
			    (ZFS_IMMUTABLE | ZFS_APPENDONLY | ZFS_NOUNLINK)) {
				error = securelevel_gt(cred, 0);
				if (error != 0)
					return (error);
			}
		} else {
			/*
			 * Callers may only modify the file flags on objects they
			 * have VADMIN rights for.
			 */
			if ((error = VOP_ACCESS(vp, VADMIN, cred, curthread)) != 0)
				return (error);
			if (zflags &
			    (ZFS_IMMUTABLE | ZFS_APPENDONLY | ZFS_NOUNLINK)) {
				return (EPERM);
			}
			if (fflags &
			    (SF_IMMUTABLE | SF_APPEND | SF_NOUNLINK)) {
				return (EPERM);
			}
		}

#define	FLAG_CHANGE(fflag, zflag, xflag, xfield)	do {		\
	if (((fflags & (fflag)) && !(zflags & (zflag))) ||		\
	    ((zflags & (zflag)) && !(fflags & (fflag)))) {		\
		XVA_SET_REQ(&xvap, (xflag));				\
		(xfield) = ((fflags & (fflag)) != 0);			\
	}								\
} while (0)
		/* Convert chflags into ZFS-type flags. */
		/* XXX: what about SF_SETTABLE?. */
		FLAG_CHANGE(SF_IMMUTABLE, ZFS_IMMUTABLE, XAT_IMMUTABLE,
		    xvap.xva_xoptattrs.xoa_immutable);
		FLAG_CHANGE(SF_APPEND, ZFS_APPENDONLY, XAT_APPENDONLY,
		    xvap.xva_xoptattrs.xoa_appendonly);
		FLAG_CHANGE(SF_NOUNLINK, ZFS_NOUNLINK, XAT_NOUNLINK,
		    xvap.xva_xoptattrs.xoa_nounlink);
		FLAG_CHANGE(UF_ARCHIVE, ZFS_ARCHIVE, XAT_ARCHIVE,
		    xvap.xva_xoptattrs.xoa_archive);
		FLAG_CHANGE(UF_NODUMP, ZFS_NODUMP, XAT_NODUMP,
		    xvap.xva_xoptattrs.xoa_nodump);
		FLAG_CHANGE(UF_READONLY, ZFS_READONLY, XAT_READONLY,
		    xvap.xva_xoptattrs.xoa_readonly);
		FLAG_CHANGE(UF_SYSTEM, ZFS_SYSTEM, XAT_SYSTEM,
		    xvap.xva_xoptattrs.xoa_system);
		FLAG_CHANGE(UF_HIDDEN, ZFS_HIDDEN, XAT_HIDDEN,
		    xvap.xva_xoptattrs.xoa_hidden);
		FLAG_CHANGE(UF_REPARSE, ZFS_REPARSE, XAT_REPARSE,
		    xvap.xva_xoptattrs.xoa_hidden);
		FLAG_CHANGE(UF_OFFLINE, ZFS_OFFLINE, XAT_OFFLINE,
		    xvap.xva_xoptattrs.xoa_offline);
		FLAG_CHANGE(UF_SPARSE, ZFS_SPARSE, XAT_SPARSE,
		    xvap.xva_xoptattrs.xoa_sparse);
#undef	FLAG_CHANGE
	}
	if (vap->va_birthtime.tv_sec != VNOVAL) {
		xvap.xva_vattr.va_mask |= AT_XVATTR;
		XVA_SET_REQ(&xvap, XAT_CREATETIME);
	}
	return (zfs_setattr(vp, (vattr_t *)&xvap, 0, cred, NULL));
}

static int
zfs_freebsd_rename(ap)
	struct vop_rename_args  /* {
		struct vnode *a_fdvp;
		struct vnode *a_fvp;
		struct componentname *a_fcnp;
		struct vnode *a_tdvp;
		struct vnode *a_tvp;
		struct componentname *a_tcnp;
	} */ *ap;
{
	vnode_t *fdvp = ap->a_fdvp;
	vnode_t *fvp = ap->a_fvp;
	vnode_t *tdvp = ap->a_tdvp;
	vnode_t *tvp = ap->a_tvp;
	int error;

	ASSERT(ap->a_fcnp->cn_flags & (SAVENAME|SAVESTART));
	ASSERT(ap->a_tcnp->cn_flags & (SAVENAME|SAVESTART));

	error = zfs_rename(fdvp, &fvp, ap->a_fcnp, tdvp, &tvp,
	    ap->a_tcnp, ap->a_fcnp->cn_cred);

	vrele(fdvp);
	vrele(fvp);
	vrele(tdvp);
	if (tvp != NULL)
		vrele(tvp);

	return (error);
}

static int
zfs_freebsd_symlink(ap)
	struct vop_symlink_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
		char *a_target;
	} */ *ap;
{
	struct componentname *cnp = ap->a_cnp;
	vattr_t *vap = ap->a_vap;

	ASSERT(cnp->cn_flags & SAVENAME);

	vap->va_type = VLNK;	/* FreeBSD: Syscall only sets va_mode. */
	vattr_init_mask(vap);

	return (zfs_symlink(ap->a_dvp, ap->a_vpp, cnp->cn_nameptr, vap,
	    __DECONST(char *, ap->a_target), cnp->cn_cred, cnp->cn_thread));
}

static int
zfs_freebsd_readlink(ap)
	struct vop_readlink_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
	} */ *ap;
{

	return (zfs_readlink(ap->a_vp, ap->a_uio, ap->a_cred, NULL));
}

static int
zfs_freebsd_link(ap)
	struct vop_link_args /* {
		struct vnode *a_tdvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct componentname *cnp = ap->a_cnp;
	vnode_t *vp = ap->a_vp;
	vnode_t *tdvp = ap->a_tdvp;

	if (tdvp->v_mount != vp->v_mount)
		return (EXDEV);

	ASSERT(cnp->cn_flags & SAVENAME);

	return (zfs_link(tdvp, vp, cnp->cn_nameptr, cnp->cn_cred, NULL, 0));
}

static int
zfs_freebsd_inactive(ap)
	struct vop_inactive_args /* {
		struct vnode *a_vp;
		struct thread *a_td;
	} */ *ap;
{
	vnode_t *vp = ap->a_vp;

	zfs_inactive(vp, ap->a_td->td_ucred, NULL);
	return (0);
}

static int
zfs_freebsd_reclaim(ap)
	struct vop_reclaim_args /* {
		struct vnode *a_vp;
		struct thread *a_td;
	} */ *ap;
{
	vnode_t	*vp = ap->a_vp;
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	ASSERT(zp != NULL);

	/* Destroy the vm object and flush associated pages. */
	vnode_destroy_vobject(vp);

	/*
	 * z_teardown_inactive_lock protects from a race with
	 * zfs_znode_dmu_fini in zfsvfs_teardown during
	 * force unmount.
	 */
	rw_enter(&zfsvfs->z_teardown_inactive_lock, RW_READER);
	if (zp->z_sa_hdl == NULL)
		zfs_znode_free(zp);
	else
		zfs_zinactive(zp);
	rw_exit(&zfsvfs->z_teardown_inactive_lock);

	vp->v_data = NULL;
	return (0);
}

static int
zfs_freebsd_fid(ap)
	struct vop_fid_args /* {
		struct vnode *a_vp;
		struct fid *a_fid;
	} */ *ap;
{

	return (zfs_fid(ap->a_vp, (void *)ap->a_fid, NULL));
}

static int
zfs_freebsd_pathconf(ap)
	struct vop_pathconf_args /* {
		struct vnode *a_vp;
		int a_name;
		register_t *a_retval;
	} */ *ap;
{
	ulong_t val;
	int error;

	error = zfs_pathconf(ap->a_vp, ap->a_name, &val, curthread->td_ucred, NULL);
	if (error == 0) {
		*ap->a_retval = val;
		return (error);
	}
	if (error != EOPNOTSUPP)
		return (error);

	switch (ap->a_name) {
	case _PC_NAME_MAX:
		*ap->a_retval = NAME_MAX;
		return (0);
	case _PC_PIPE_BUF:
		if (ap->a_vp->v_type == VDIR || ap->a_vp->v_type == VFIFO) {
			*ap->a_retval = PIPE_BUF;
			return (0);
		}
		return (EINVAL);
	default:
		return (vop_stdpathconf(ap));
	}
}

/*
 * FreeBSD's extended attributes namespace defines file name prefix for ZFS'
 * extended attribute name:
 *
 *	NAMESPACE	PREFIX	
 *	system		freebsd:system:
 *	user		(none, can be used to access ZFS fsattr(5) attributes
 *			created on Solaris)
 */
static int
zfs_create_attrname(int attrnamespace, const char *name, char *attrname,
    size_t size)
{
	const char *namespace, *prefix, *suffix;

	/* We don't allow '/' character in attribute name. */
	if (strchr(name, '/') != NULL)
		return (EINVAL);
	/* We don't allow attribute names that start with "freebsd:" string. */
	if (strncmp(name, "freebsd:", 8) == 0)
		return (EINVAL);

	bzero(attrname, size);

	switch (attrnamespace) {
	case EXTATTR_NAMESPACE_USER:
#if 0
		prefix = "freebsd:";
		namespace = EXTATTR_NAMESPACE_USER_STRING;
		suffix = ":";
#else
		/*
		 * This is the default namespace by which we can access all
		 * attributes created on Solaris.
		 */
		prefix = namespace = suffix = "";
#endif
		break;
	case EXTATTR_NAMESPACE_SYSTEM:
		prefix = "freebsd:";
		namespace = EXTATTR_NAMESPACE_SYSTEM_STRING;
		suffix = ":";
		break;
	case EXTATTR_NAMESPACE_EMPTY:
	default:
		return (EINVAL);
	}
	if (snprintf(attrname, size, "%s%s%s%s", prefix, namespace, suffix,
	    name) >= size) {
		return (ENAMETOOLONG);
	}
	return (0);
}

/*
 * Vnode operating to retrieve a named extended attribute.
 */
static int
zfs_getextattr(struct vop_getextattr_args *ap)
/*
vop_getextattr {
	IN struct vnode *a_vp;
	IN int a_attrnamespace;
	IN const char *a_name;
	INOUT struct uio *a_uio;
	OUT size_t *a_size;
	IN struct ucred *a_cred;
	IN struct thread *a_td;
};
*/
{
	zfsvfs_t *zfsvfs = VTOZ(ap->a_vp)->z_zfsvfs;
	struct thread *td = ap->a_td;
	struct nameidata nd;
	char attrname[255];
	struct vattr va;
	vnode_t *xvp = NULL, *vp;
	int error, flags;

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace,
	    ap->a_cred, ap->a_td, VREAD);
	if (error != 0)
		return (error);

	error = zfs_create_attrname(ap->a_attrnamespace, ap->a_name, attrname,
	    sizeof(attrname));
	if (error != 0)
		return (error);

	ZFS_ENTER(zfsvfs);

	error = zfs_lookup(ap->a_vp, NULL, &xvp, NULL, 0, ap->a_cred, td,
	    LOOKUP_XATTR);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	flags = FREAD;
	NDINIT_ATVP(&nd, LOOKUP, NOFOLLOW, UIO_SYSSPACE, attrname,
	    xvp, td);
	error = vn_open_cred(&nd, &flags, 0, 0, ap->a_cred, NULL);
	vp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		if (error == ENOENT)
			error = ENOATTR;
		return (error);
	}

	if (ap->a_size != NULL) {
		error = VOP_GETATTR(vp, &va, ap->a_cred);
		if (error == 0)
			*ap->a_size = (size_t)va.va_size;
	} else if (ap->a_uio != NULL)
		error = VOP_READ(vp, ap->a_uio, IO_UNIT, ap->a_cred);

	VOP_UNLOCK(vp, 0);
	vn_close(vp, flags, ap->a_cred, td);
	ZFS_EXIT(zfsvfs);

	return (error);
}

/*
 * Vnode operation to remove a named attribute.
 */
int
zfs_deleteextattr(struct vop_deleteextattr_args *ap)
/*
vop_deleteextattr {
	IN struct vnode *a_vp;
	IN int a_attrnamespace;
	IN const char *a_name;
	IN struct ucred *a_cred;
	IN struct thread *a_td;
};
*/
{
	zfsvfs_t *zfsvfs = VTOZ(ap->a_vp)->z_zfsvfs;
	struct thread *td = ap->a_td;
	struct nameidata nd;
	char attrname[255];
	struct vattr va;
	vnode_t *xvp = NULL, *vp;
	int error, flags;

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace,
	    ap->a_cred, ap->a_td, VWRITE);
	if (error != 0)
		return (error);

	error = zfs_create_attrname(ap->a_attrnamespace, ap->a_name, attrname,
	    sizeof(attrname));
	if (error != 0)
		return (error);

	ZFS_ENTER(zfsvfs);

	error = zfs_lookup(ap->a_vp, NULL, &xvp, NULL, 0, ap->a_cred, td,
	    LOOKUP_XATTR);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	NDINIT_ATVP(&nd, DELETE, NOFOLLOW | LOCKPARENT | LOCKLEAF,
	    UIO_SYSSPACE, attrname, xvp, td);
	error = namei(&nd);
	vp = nd.ni_vp;
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		NDFREE(&nd, NDF_ONLY_PNBUF);
		if (error == ENOENT)
			error = ENOATTR;
		return (error);
	}

	error = VOP_REMOVE(nd.ni_dvp, vp, &nd.ni_cnd);
	NDFREE(&nd, NDF_ONLY_PNBUF);

	vput(nd.ni_dvp);
	if (vp == nd.ni_dvp)
		vrele(vp);
	else
		vput(vp);
	ZFS_EXIT(zfsvfs);

	return (error);
}

/*
 * Vnode operation to set a named attribute.
 */
static int
zfs_setextattr(struct vop_setextattr_args *ap)
/*
vop_setextattr {
	IN struct vnode *a_vp;
	IN int a_attrnamespace;
	IN const char *a_name;
	INOUT struct uio *a_uio;
	IN struct ucred *a_cred;
	IN struct thread *a_td;
};
*/
{
	zfsvfs_t *zfsvfs = VTOZ(ap->a_vp)->z_zfsvfs;
	struct thread *td = ap->a_td;
	struct nameidata nd;
	char attrname[255];
	struct vattr va;
	vnode_t *xvp = NULL, *vp;
	int error, flags;

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace,
	    ap->a_cred, ap->a_td, VWRITE);
	if (error != 0)
		return (error);

	error = zfs_create_attrname(ap->a_attrnamespace, ap->a_name, attrname,
	    sizeof(attrname));
	if (error != 0)
		return (error);

	ZFS_ENTER(zfsvfs);

	error = zfs_lookup(ap->a_vp, NULL, &xvp, NULL, 0, ap->a_cred, td,
	    LOOKUP_XATTR | CREATE_XATTR_DIR);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	flags = FFLAGS(O_WRONLY | O_CREAT);
	NDINIT_ATVP(&nd, LOOKUP, NOFOLLOW, UIO_SYSSPACE, attrname,
	    xvp, td);
	error = vn_open_cred(&nd, &flags, 0600, 0, ap->a_cred, NULL);
	vp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	VATTR_NULL(&va);
	va.va_size = 0;
	error = VOP_SETATTR(vp, &va, ap->a_cred);
	if (error == 0)
		VOP_WRITE(vp, ap->a_uio, IO_UNIT, ap->a_cred);

	VOP_UNLOCK(vp, 0);
	vn_close(vp, flags, ap->a_cred, td);
	ZFS_EXIT(zfsvfs);

	return (error);
}

/*
 * Vnode operation to retrieve extended attributes on a vnode.
 */
static int
zfs_listextattr(struct vop_listextattr_args *ap)
/*
vop_listextattr {
	IN struct vnode *a_vp;
	IN int a_attrnamespace;
	INOUT struct uio *a_uio;
	OUT size_t *a_size;
	IN struct ucred *a_cred;
	IN struct thread *a_td;
};
*/
{
	zfsvfs_t *zfsvfs = VTOZ(ap->a_vp)->z_zfsvfs;
	struct thread *td = ap->a_td;
	struct nameidata nd;
	char attrprefix[16];
	u_char dirbuf[sizeof(struct dirent)];
	struct dirent *dp;
	struct iovec aiov;
	struct uio auio, *uio = ap->a_uio;
	size_t *sizep = ap->a_size;
	size_t plen;
	vnode_t *xvp = NULL, *vp;
	int done, error, eof, pos;

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace,
	    ap->a_cred, ap->a_td, VREAD);
	if (error != 0)
		return (error);

	error = zfs_create_attrname(ap->a_attrnamespace, "", attrprefix,
	    sizeof(attrprefix));
	if (error != 0)
		return (error);
	plen = strlen(attrprefix);

	ZFS_ENTER(zfsvfs);

	if (sizep != NULL)
		*sizep = 0;

	error = zfs_lookup(ap->a_vp, NULL, &xvp, NULL, 0, ap->a_cred, td,
	    LOOKUP_XATTR);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		/*
		 * ENOATTR means that the EA directory does not yet exist,
		 * i.e. there are no extended attributes there.
		 */
		if (error == ENOATTR)
			error = 0;
		return (error);
	}

	NDINIT_ATVP(&nd, LOOKUP, NOFOLLOW | LOCKLEAF | LOCKSHARED,
	    UIO_SYSSPACE, ".", xvp, td);
	error = namei(&nd);
	vp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_td = td;
	auio.uio_rw = UIO_READ;
	auio.uio_offset = 0;

	do {
		u_char nlen;

		aiov.iov_base = (void *)dirbuf;
		aiov.iov_len = sizeof(dirbuf);
		auio.uio_resid = sizeof(dirbuf);
		error = VOP_READDIR(vp, &auio, ap->a_cred, &eof, NULL, NULL);
		done = sizeof(dirbuf) - auio.uio_resid;
		if (error != 0)
			break;
		for (pos = 0; pos < done;) {
			dp = (struct dirent *)(dirbuf + pos);
			pos += dp->d_reclen;
			/*
			 * XXX: Temporarily we also accept DT_UNKNOWN, as this
			 * is what we get when attribute was created on Solaris.
			 */
			if (dp->d_type != DT_REG && dp->d_type != DT_UNKNOWN)
				continue;
			if (plen == 0 && strncmp(dp->d_name, "freebsd:", 8) == 0)
				continue;
			else if (strncmp(dp->d_name, attrprefix, plen) != 0)
				continue;
			nlen = dp->d_namlen - plen;
			if (sizep != NULL)
				*sizep += 1 + nlen;
			else if (uio != NULL) {
				/*
				 * Format of extattr name entry is one byte for
				 * length and the rest for name.
				 */
				error = uiomove(&nlen, 1, uio->uio_rw, uio);
				if (error == 0) {
					error = uiomove(dp->d_name + plen, nlen,
					    uio->uio_rw, uio);
				}
				if (error != 0)
					break;
			}
		}
	} while (!eof && error == 0);

	vput(vp);
	ZFS_EXIT(zfsvfs);

	return (error);
}

int
zfs_freebsd_getacl(ap)
	struct vop_getacl_args /* {
		struct vnode *vp;
		acl_type_t type;
		struct acl *aclp;
		struct ucred *cred;
		struct thread *td;
	} */ *ap;
{
	int		error;
	vsecattr_t      vsecattr;

	if (ap->a_type != ACL_TYPE_NFS4)
		return (EINVAL);

	vsecattr.vsa_mask = VSA_ACE | VSA_ACECNT;
	if (error = zfs_getsecattr(ap->a_vp, &vsecattr, 0, ap->a_cred, NULL))
		return (error);

	error = acl_from_aces(ap->a_aclp, vsecattr.vsa_aclentp, vsecattr.vsa_aclcnt);
	if (vsecattr.vsa_aclentp != NULL)
		kmem_free(vsecattr.vsa_aclentp, vsecattr.vsa_aclentsz);

	return (error);
}

int
zfs_freebsd_setacl(ap)
	struct vop_setacl_args /* {
		struct vnode *vp;
		acl_type_t type;
		struct acl *aclp;
		struct ucred *cred;
		struct thread *td;
	} */ *ap;
{
	int		error;
	vsecattr_t      vsecattr;
	int		aclbsize;	/* size of acl list in bytes */
	aclent_t	*aaclp;

	if (ap->a_type != ACL_TYPE_NFS4)
		return (EINVAL);

	if (ap->a_aclp == NULL)
		return (EINVAL);

	if (ap->a_aclp->acl_cnt < 1 || ap->a_aclp->acl_cnt > MAX_ACL_ENTRIES)
		return (EINVAL);

	/*
	 * With NFSv4 ACLs, chmod(2) may need to add additional entries,
	 * splitting every entry into two and appending "canonical six"
	 * entries at the end.  Don't allow for setting an ACL that would
	 * cause chmod(2) to run out of ACL entries.
	 */
	if (ap->a_aclp->acl_cnt * 2 + 6 > ACL_MAX_ENTRIES)
		return (ENOSPC);

	error = acl_nfs4_check(ap->a_aclp, ap->a_vp->v_type == VDIR);
	if (error != 0)
		return (error);

	vsecattr.vsa_mask = VSA_ACE;
	aclbsize = ap->a_aclp->acl_cnt * sizeof(ace_t);
	vsecattr.vsa_aclentp = kmem_alloc(aclbsize, KM_SLEEP);
	aaclp = vsecattr.vsa_aclentp;
	vsecattr.vsa_aclentsz = aclbsize;

	aces_from_acl(vsecattr.vsa_aclentp, &vsecattr.vsa_aclcnt, ap->a_aclp);
	error = zfs_setsecattr(ap->a_vp, &vsecattr, 0, ap->a_cred, NULL);
	kmem_free(aaclp, aclbsize);

	return (error);
}

int
zfs_freebsd_aclcheck(ap)
	struct vop_aclcheck_args /* {
		struct vnode *vp;
		acl_type_t type;
		struct acl *aclp;
		struct ucred *cred;
		struct thread *td;
	} */ *ap;
{

	return (EOPNOTSUPP);
}

static int
zfs_vptocnp(struct vop_vptocnp_args *ap)
{
	vnode_t *covered_vp;
	vnode_t *vp = ap->a_vp;;
	zfsvfs_t *zfsvfs = vp->v_vfsp->vfs_data;
	znode_t *zp = VTOZ(vp);
	int ltype;
	int error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	/*
	 * If we are a snapshot mounted under .zfs, run the operation
	 * on the covered vnode.
	 */
	if (zp->z_id != zfsvfs->z_root || zfsvfs->z_parent == zfsvfs) {
		char name[MAXNAMLEN + 1];
		znode_t *dzp;
		size_t len;

		error = zfs_znode_parent_and_name(zp, &dzp, name);
		if (error == 0) {
			len = strlen(name);
			if (*ap->a_buflen < len)
				error = SET_ERROR(ENOMEM);
		}
		if (error == 0) {
			*ap->a_buflen -= len;
			bcopy(name, ap->a_buf + *ap->a_buflen, len);
			*ap->a_vpp = ZTOV(dzp);
		}
		ZFS_EXIT(zfsvfs);
		return (error);
	}
	ZFS_EXIT(zfsvfs);

	covered_vp = vp->v_mount->mnt_vnodecovered;
	vhold(covered_vp);
	ltype = VOP_ISLOCKED(vp);
	VOP_UNLOCK(vp, 0);
	error = vget(covered_vp, LK_SHARED | LK_VNHELD, curthread);
	if (error == 0) {
		error = VOP_VPTOCNP(covered_vp, ap->a_vpp, ap->a_cred,
		    ap->a_buf, ap->a_buflen);
		vput(covered_vp);
	}
	vn_lock(vp, ltype | LK_RETRY);
	if ((vp->v_iflag & VI_DOOMED) != 0)
		error = SET_ERROR(ENOENT);
	return (error);
}

#ifdef DIAGNOSTIC
static int
zfs_lock(ap)
	struct vop_lock1_args /* {
		struct vnode *a_vp;
		int a_flags;
		char *file;
		int line;
	} */ *ap;
{
	vnode_t *vp;
	znode_t *zp;
	int err;

	err = vop_stdlock(ap);
	if (err == 0 && (ap->a_flags & LK_NOWAIT) == 0) {
		vp = ap->a_vp;
		zp = vp->v_data;
		if (vp->v_mount != NULL && (vp->v_iflag & VI_DOOMED) == 0 &&
		    zp != NULL && (zp->z_pflags & ZFS_XATTR) == 0)
			VERIFY(!RRM_LOCK_HELD(&zp->z_zfsvfs->z_teardown_lock));
	}
	return (err);
}
#endif

struct vop_vector zfs_vnodeops;
struct vop_vector zfs_fifoops;
struct vop_vector zfs_shareops;

struct vop_vector zfs_vnodeops = {
	.vop_default =		&default_vnodeops,
	.vop_inactive =		zfs_freebsd_inactive,
	.vop_reclaim =		zfs_freebsd_reclaim,
	.vop_access =		zfs_freebsd_access,
	.vop_allocate =		VOP_EINVAL,
	.vop_lookup =		zfs_cache_lookup,
	.vop_cachedlookup =	zfs_freebsd_lookup,
	.vop_getattr =		zfs_freebsd_getattr,
	.vop_setattr =		zfs_freebsd_setattr,
	.vop_create =		zfs_freebsd_create,
	.vop_mknod =		zfs_freebsd_create,
	.vop_mkdir =		zfs_freebsd_mkdir,
	.vop_readdir =		zfs_freebsd_readdir,
	.vop_fsync =		zfs_freebsd_fsync,
	.vop_open =		zfs_freebsd_open,
	.vop_close =		zfs_freebsd_close,
	.vop_rmdir =		zfs_freebsd_rmdir,
	.vop_ioctl =		zfs_freebsd_ioctl,
	.vop_link =		zfs_freebsd_link,
	.vop_symlink =		zfs_freebsd_symlink,
	.vop_readlink =		zfs_freebsd_readlink,
	.vop_read =		zfs_freebsd_read,
	.vop_write =		zfs_freebsd_write,
	.vop_remove =		zfs_freebsd_remove,
	.vop_rename =		zfs_freebsd_rename,
	.vop_pathconf =		zfs_freebsd_pathconf,
	.vop_bmap =		zfs_freebsd_bmap,
	.vop_fid =		zfs_freebsd_fid,
	.vop_getextattr =	zfs_getextattr,
	.vop_deleteextattr =	zfs_deleteextattr,
	.vop_setextattr =	zfs_setextattr,
	.vop_listextattr =	zfs_listextattr,
	.vop_getacl =		zfs_freebsd_getacl,
	.vop_setacl =		zfs_freebsd_setacl,
	.vop_aclcheck =		zfs_freebsd_aclcheck,
	.vop_getpages =		zfs_freebsd_getpages,
	.vop_putpages =		zfs_freebsd_putpages,
	.vop_vptocnp =		zfs_vptocnp,
#ifdef DIAGNOSTIC
	.vop_lock1 =		zfs_lock,
#endif
};

struct vop_vector zfs_fifoops = {
	.vop_default =		&fifo_specops,
	.vop_fsync =		zfs_freebsd_fsync,
	.vop_access =		zfs_freebsd_access,
	.vop_getattr =		zfs_freebsd_getattr,
	.vop_inactive =		zfs_freebsd_inactive,
	.vop_read =		VOP_PANIC,
	.vop_reclaim =		zfs_freebsd_reclaim,
	.vop_setattr =		zfs_freebsd_setattr,
	.vop_write =		VOP_PANIC,
	.vop_pathconf = 	zfs_freebsd_pathconf,
	.vop_fid =		zfs_freebsd_fid,
	.vop_getacl =		zfs_freebsd_getacl,
	.vop_setacl =		zfs_freebsd_setacl,
	.vop_aclcheck =		zfs_freebsd_aclcheck,
};

/*
 * special share hidden files vnode operations template
 */
struct vop_vector zfs_shareops = {
	.vop_default =		&default_vnodeops,
	.vop_access =		zfs_freebsd_access,
	.vop_inactive =		zfs_freebsd_inactive,
	.vop_reclaim =		zfs_freebsd_reclaim,
	.vop_fid =		zfs_freebsd_fid,
	.vop_pathconf =		zfs_freebsd_pathconf,
};
