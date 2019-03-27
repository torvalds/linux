/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2003 Tor Egge
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/limits.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/conf.h>
#include <sys/filio.h>
#include <sys/ttycom.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/rwlock.h>
#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>
#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

static int ffs_rawread_readahead(struct vnode *vp,
				 caddr_t udata,
				 off_t offset,
				 size_t len,
				 struct thread *td,
				 struct buf *bp);
static int ffs_rawread_main(struct vnode *vp,
			    struct uio *uio);

static int ffs_rawread_sync(struct vnode *vp);

int ffs_rawread(struct vnode *vp, struct uio *uio, int *workdone);

SYSCTL_DECL(_vfs_ffs);

static uma_zone_t ffsraw_pbuf_zone;

static int allowrawread = 1;
SYSCTL_INT(_vfs_ffs, OID_AUTO, allowrawread, CTLFLAG_RW, &allowrawread, 0,
	   "Flag to enable raw reads");

static int rawreadahead = 1;
SYSCTL_INT(_vfs_ffs, OID_AUTO, rawreadahead, CTLFLAG_RW, &rawreadahead, 0,
	   "Flag to enable readahead for long raw reads");

static void
ffs_rawread_setup(void *arg __unused)
{

	ffsraw_pbuf_zone = pbuf_zsecond_create("ffsrawpbuf",
	    (nswbuf > 100 ) ?  (nswbuf - (nswbuf >> 4)) : nswbuf - 8);
}
SYSINIT(ffs_raw, SI_SUB_VM_CONF, SI_ORDER_ANY, ffs_rawread_setup, NULL);

static int
ffs_rawread_sync(struct vnode *vp)
{
	int error;
	int upgraded;
	struct bufobj *bo;
	struct mount *mp;
	vm_object_t obj;

	/* Check for dirty mmap, pending writes and dirty buffers */
	bo = &vp->v_bufobj;
	BO_LOCK(bo);
	VI_LOCK(vp);
	if (bo->bo_numoutput > 0 ||
	    bo->bo_dirty.bv_cnt > 0 ||
	    ((obj = vp->v_object) != NULL &&
	     (obj->flags & OBJ_MIGHTBEDIRTY) != 0)) {
		VI_UNLOCK(vp);
		BO_UNLOCK(bo);
		
		if (vn_start_write(vp, &mp, V_NOWAIT) != 0) {
			if (VOP_ISLOCKED(vp) != LK_EXCLUSIVE)
				upgraded = 1;
			else
				upgraded = 0;
			VOP_UNLOCK(vp, 0);
			(void) vn_start_write(vp, &mp, V_WAIT);
			VOP_LOCK(vp, LK_EXCLUSIVE);
		} else if (VOP_ISLOCKED(vp) != LK_EXCLUSIVE) {
			upgraded = 1;
			/* Upgrade to exclusive lock, this might block */
			VOP_LOCK(vp, LK_UPGRADE);
		} else
			upgraded = 0;
			
		
		VI_LOCK(vp);
		/* Check if vnode was reclaimed while unlocked. */
		if ((vp->v_iflag & VI_DOOMED) != 0) {
			VI_UNLOCK(vp);
			if (upgraded != 0)
				VOP_LOCK(vp, LK_DOWNGRADE);
			vn_finished_write(mp);
			return (EIO);
		}
		/* Attempt to msync mmap() regions to clean dirty mmap */ 
		if ((obj = vp->v_object) != NULL &&
		    (obj->flags & OBJ_MIGHTBEDIRTY) != 0) {
			VI_UNLOCK(vp);
			VM_OBJECT_WLOCK(obj);
			vm_object_page_clean(obj, 0, 0, OBJPC_SYNC);
			VM_OBJECT_WUNLOCK(obj);
		} else
			VI_UNLOCK(vp);

		/* Wait for pending writes to complete */
		BO_LOCK(bo);
		error = bufobj_wwait(&vp->v_bufobj, 0, 0);
		if (error != 0) {
			/* XXX: can't happen with a zero timeout ??? */
			BO_UNLOCK(bo);
			if (upgraded != 0)
				VOP_LOCK(vp, LK_DOWNGRADE);
			vn_finished_write(mp);
			return (error);
		}
		/* Flush dirty buffers */
		if (bo->bo_dirty.bv_cnt > 0) {
			BO_UNLOCK(bo);
			if ((error = ffs_syncvnode(vp, MNT_WAIT, 0)) != 0) {
				if (upgraded != 0)
					VOP_LOCK(vp, LK_DOWNGRADE);
				vn_finished_write(mp);
				return (error);
			}
			BO_LOCK(bo);
			if (bo->bo_numoutput > 0 || bo->bo_dirty.bv_cnt > 0)
				panic("ffs_rawread_sync: dirty bufs");
		}
		BO_UNLOCK(bo);
		if (upgraded != 0)
			VOP_LOCK(vp, LK_DOWNGRADE);
		vn_finished_write(mp);
	} else {
		VI_UNLOCK(vp);
		BO_UNLOCK(bo);
	}
	return 0;
}


static int
ffs_rawread_readahead(struct vnode *vp,
		      caddr_t udata,
		      off_t offset,
		      size_t len,
		      struct thread *td,
		      struct buf *bp)
{
	int error;
	u_int iolen;
	off_t blockno;
	int blockoff;
	int bsize;
	struct vnode *dp;
	int bforwards;
	struct inode *ip;
	ufs2_daddr_t blkno;
	
	bsize = vp->v_mount->mnt_stat.f_iosize;
	
	ip = VTOI(vp);
	dp = ITODEVVP(ip);

	iolen = ((vm_offset_t) udata) & PAGE_MASK;
	bp->b_bcount = len;
	if (bp->b_bcount + iolen > bp->b_kvasize) {
		bp->b_bcount = bp->b_kvasize;
		if (iolen != 0)
			bp->b_bcount -= PAGE_SIZE;
	}
	bp->b_flags = 0;	/* XXX necessary ? */
	bp->b_iocmd = BIO_READ;
	bp->b_iodone = bdone;
	bp->b_data = udata;
	blockno = offset / bsize;
	blockoff = (offset % bsize) / DEV_BSIZE;
	if ((daddr_t) blockno != blockno) {
		return EINVAL; /* blockno overflow */
	}
	
	bp->b_lblkno = bp->b_blkno = blockno;
	
	error = ufs_bmaparray(vp, bp->b_lblkno, &blkno, NULL, &bforwards, NULL);
	if (error != 0)
		return error;
	if (blkno == -1) {

		/* Fill holes with NULs to preserve semantics */
		
		if (bp->b_bcount + blockoff * DEV_BSIZE > bsize)
			bp->b_bcount = bsize - blockoff * DEV_BSIZE;
		bp->b_bufsize = bp->b_bcount;
		
		if (vmapbuf(bp, 1) < 0)
			return EFAULT;
		
		maybe_yield();
		bzero(bp->b_data, bp->b_bufsize);

		/* Mark operation completed (similar to bufdone()) */

		bp->b_resid = 0;
		bp->b_flags |= B_DONE;
		return 0;
	}
	bp->b_blkno = blkno + blockoff;
	bp->b_offset = bp->b_iooffset = (blkno + blockoff) * DEV_BSIZE;
	
	if (bp->b_bcount + blockoff * DEV_BSIZE > bsize * (1 + bforwards))
		bp->b_bcount = bsize * (1 + bforwards) - blockoff * DEV_BSIZE;
	bp->b_bufsize = bp->b_bcount;
	
	if (vmapbuf(bp, 1) < 0)
		return EFAULT;
	
	BO_STRATEGY(&dp->v_bufobj, bp);
	return 0;
}


static int
ffs_rawread_main(struct vnode *vp,
		 struct uio *uio)
{
	int error, nerror;
	struct buf *bp, *nbp, *tbp;
	u_int iolen;
	caddr_t udata;
	long resid;
	off_t offset;
	struct thread *td;
	
	td = uio->uio_td ? uio->uio_td : curthread;
	udata = uio->uio_iov->iov_base;
	resid = uio->uio_resid;
	offset = uio->uio_offset;

	/*
	 * keep the process from being swapped
	 */
	PHOLD(td->td_proc);
	
	error = 0;
	nerror = 0;
	
	bp = NULL;
	nbp = NULL;
	
	while (resid > 0) {
		
		if (bp == NULL) { /* Setup first read */
			bp = uma_zalloc(ffsraw_pbuf_zone, M_WAITOK);
			pbgetvp(vp, bp);
			error = ffs_rawread_readahead(vp, udata, offset,
						     resid, td, bp);
			if (error != 0)
				break;
			
			if (resid > bp->b_bufsize) { /* Setup fist readahead */
				if (rawreadahead != 0) 
					nbp = uma_zalloc(ffsraw_pbuf_zone,
					    M_NOWAIT);
				else
					nbp = NULL;
				if (nbp != NULL) {
					pbgetvp(vp, nbp);
					
					nerror = ffs_rawread_readahead(vp, 
								       udata +
								       bp->b_bufsize,
								       offset +
								       bp->b_bufsize,
								       resid -
								       bp->b_bufsize,
								       td,
								       nbp);
					if (nerror) {
						pbrelvp(nbp);
						uma_zfree(ffsraw_pbuf_zone,
						    nbp);
						nbp = NULL;
					}
				}
			}
		}
		
		bwait(bp, PRIBIO, "rawrd");
		vunmapbuf(bp);
		
		iolen = bp->b_bcount - bp->b_resid;
		if (iolen == 0 && (bp->b_ioflags & BIO_ERROR) == 0) {
			nerror = 0;	/* Ignore possible beyond EOF error */
			break; /* EOF */
		}
		
		if ((bp->b_ioflags & BIO_ERROR) != 0) {
			error = bp->b_error;
			break;
		}
		resid -= iolen;
		udata += iolen;
		offset += iolen;
		if (iolen < bp->b_bufsize) {
			/* Incomplete read.  Try to read remaining part */
			error = ffs_rawread_readahead(vp,
						      udata,
						      offset,
						      bp->b_bufsize - iolen,
						      td,
						      bp);
			if (error != 0)
				break;
		} else if (nbp != NULL) { /* Complete read with readahead */
			
			tbp = bp;
			bp = nbp;
			nbp = tbp;
			
			if (resid <= bp->b_bufsize) { /* No more readaheads */
				pbrelvp(nbp);
				uma_zfree(ffsraw_pbuf_zone, nbp);
				nbp = NULL;
			} else { /* Setup next readahead */
				nerror = ffs_rawread_readahead(vp,
							       udata +
							       bp->b_bufsize,
							       offset +
							       bp->b_bufsize,
							       resid -
							       bp->b_bufsize,
							       td,
							       nbp);
				if (nerror != 0) {
					pbrelvp(nbp);
					uma_zfree(ffsraw_pbuf_zone, nbp);
					nbp = NULL;
				}
			}
		} else if (nerror != 0) {/* Deferred Readahead error */
			break;		
		}  else if (resid > 0) { /* More to read, no readahead */
			error = ffs_rawread_readahead(vp, udata, offset,
						      resid, td, bp);
			if (error != 0)
				break;
		}
	}
	
	if (bp != NULL) {
		pbrelvp(bp);
		uma_zfree(ffsraw_pbuf_zone, bp);
	}
	if (nbp != NULL) {			/* Run down readahead buffer */
		bwait(nbp, PRIBIO, "rawrd");
		vunmapbuf(nbp);
		pbrelvp(nbp);
		uma_zfree(ffsraw_pbuf_zone, nbp);
	}
	
	if (error == 0)
		error = nerror;
	PRELE(td->td_proc);
	uio->uio_iov->iov_base = udata;
	uio->uio_resid = resid;
	uio->uio_offset = offset;
	return error;
}


int
ffs_rawread(struct vnode *vp,
	    struct uio *uio,
	    int *workdone)
{
	if (allowrawread != 0 &&
	    uio->uio_iovcnt == 1 && 
	    uio->uio_segflg == UIO_USERSPACE &&
	    uio->uio_resid == uio->uio_iov->iov_len &&
	    (((uio->uio_td != NULL) ? uio->uio_td : curthread)->td_pflags &
	     TDP_DEADLKTREAT) == 0) {
		int secsize;		/* Media sector size */
		off_t filebytes;	/* Bytes left of file */
		int blockbytes;		/* Bytes left of file in full blocks */
		int partialbytes;	/* Bytes in last partial block */
		int skipbytes;		/* Bytes not to read in ffs_rawread */
		struct inode *ip;
		int error;
		

		/* Only handle sector aligned reads */
		ip = VTOI(vp);
		secsize = ITODEVVP(ip)->v_bufobj.bo_bsize;
		if ((uio->uio_offset & (secsize - 1)) == 0 &&
		    (uio->uio_resid & (secsize - 1)) == 0) {
			
			/* Sync dirty pages and buffers if needed */
			error = ffs_rawread_sync(vp);
			if (error != 0)
				return error;
			
			/* Check for end of file */
			if (ip->i_size > uio->uio_offset) {
				filebytes = ip->i_size - uio->uio_offset;

				/* No special eof handling needed ? */
				if (uio->uio_resid <= filebytes) {
					*workdone = 1;
					return ffs_rawread_main(vp, uio);
				}
				
				partialbytes = ((unsigned int) ip->i_size) %
				    ITOFS(ip)->fs_bsize;
				blockbytes = (int) filebytes - partialbytes;
				if (blockbytes > 0) {
					skipbytes = uio->uio_resid -
						blockbytes;
					uio->uio_resid = blockbytes;
					error = ffs_rawread_main(vp, uio);
					uio->uio_resid += skipbytes;
					if (error != 0)
						return error;
					/* Read remaining part using buffer */
				}
			}
		}
	}
	*workdone = 0;
	return 0;
}
