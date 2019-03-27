/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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
 *
 * $FreeBSD$
 *
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/rwlock.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>
/*
#include <sys/ioccom.h>
*/
#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_subr.h>

#include <fs/smbfs/smbfs.h>
#include <fs/smbfs/smbfs_node.h>
#include <fs/smbfs/smbfs_subr.h>

/*#define SMBFS_RWGENERIC*/

extern uma_zone_t smbfs_pbuf_zone;

static int smbfs_fastlookup = 1;

SYSCTL_DECL(_vfs_smbfs);
SYSCTL_INT(_vfs_smbfs, OID_AUTO, fastlookup, CTLFLAG_RW, &smbfs_fastlookup, 0, "");


#define DE_SIZE	(sizeof(struct dirent))

static int
smbfs_readvdir(struct vnode *vp, struct uio *uio, struct ucred *cred)
{
	struct dirent de;
	struct componentname cn;
	struct smb_cred *scred;
	struct smbfs_fctx *ctx;
	struct vnode *newvp;
	struct smbnode *np = VTOSMB(vp);
	int error/*, *eofflag = ap->a_eofflag*/;
	long offset, limit;

	np = VTOSMB(vp);
	SMBVDEBUG("dirname='%s'\n", np->n_name);
	scred = smbfs_malloc_scred();
	smb_makescred(scred, uio->uio_td, cred);
	offset = uio->uio_offset / DE_SIZE;	/* offset in the directory */
	limit = uio->uio_resid / DE_SIZE;
	if (uio->uio_resid < DE_SIZE || uio->uio_offset < 0) {
		error = EINVAL;
		goto out;
	}
	while (limit && offset < 2) {
		limit--;
		bzero((caddr_t)&de, DE_SIZE);
		de.d_reclen = DE_SIZE;
		de.d_fileno = (offset == 0) ? np->n_ino :
		    (np->n_parent ? np->n_parentino : 2);
		if (de.d_fileno == 0)
			de.d_fileno = 0x7ffffffd + offset;
		de.d_namlen = offset + 1;
		de.d_name[0] = '.';
		de.d_name[1] = '.';
		de.d_type = DT_DIR;
		dirent_terminate(&de);
		error = uiomove(&de, DE_SIZE, uio);
		if (error)
			goto out;
		offset++;
		uio->uio_offset += DE_SIZE;
	}
	if (limit == 0) {
		error = 0;
		goto out;
	}
	if (offset != np->n_dirofs || np->n_dirseq == NULL) {
		SMBVDEBUG("Reopening search %ld:%ld\n", offset, np->n_dirofs);
		if (np->n_dirseq) {
			smbfs_findclose(np->n_dirseq, scred);
			np->n_dirseq = NULL;
		}
		np->n_dirofs = 2;
		error = smbfs_findopen(np, "*", 1,
		    SMB_FA_SYSTEM | SMB_FA_HIDDEN | SMB_FA_DIR,
		    scred, &ctx);
		if (error) {
			SMBVDEBUG("can not open search, error = %d", error);
			goto out;
		}
		np->n_dirseq = ctx;
	} else
		ctx = np->n_dirseq;
	while (np->n_dirofs < offset) {
		error = smbfs_findnext(ctx, offset - np->n_dirofs++, scred);
		if (error) {
			smbfs_findclose(np->n_dirseq, scred);
			np->n_dirseq = NULL;
			error = ENOENT ? 0 : error;
			goto out;
		}
	}
	error = 0;
	for (; limit; limit--, offset++) {
		error = smbfs_findnext(ctx, limit, scred);
		if (error)
			break;
		np->n_dirofs++;
		bzero((caddr_t)&de, DE_SIZE);
		de.d_reclen = DE_SIZE;
		de.d_fileno = ctx->f_attr.fa_ino;
		de.d_type = (ctx->f_attr.fa_attr & SMB_FA_DIR) ? DT_DIR : DT_REG;
		de.d_namlen = ctx->f_nmlen;
		bcopy(ctx->f_name, de.d_name, de.d_namlen);
		dirent_terminate(&de);
		if (smbfs_fastlookup) {
			error = smbfs_nget(vp->v_mount, vp, ctx->f_name,
			    ctx->f_nmlen, &ctx->f_attr, &newvp);
			if (!error) {
				cn.cn_nameptr = de.d_name;
				cn.cn_namelen = de.d_namlen;
				cache_enter(vp, newvp, &cn);
				vput(newvp);
			}
		}
		error = uiomove(&de, DE_SIZE, uio);
		if (error)
			break;
	}
	if (error == ENOENT)
		error = 0;
	uio->uio_offset = offset * DE_SIZE;
out:
	smbfs_free_scred(scred);
	return error;
}

int
smbfs_readvnode(struct vnode *vp, struct uio *uiop, struct ucred *cred)
{
	struct smbmount *smp = VFSTOSMBFS(vp->v_mount);
	struct smbnode *np = VTOSMB(vp);
	struct thread *td;
	struct vattr vattr;
	struct smb_cred *scred;
	int error, lks;

	/*
	 * Protect against method which is not supported for now
	 */
	if (uiop->uio_segflg == UIO_NOCOPY)
		return EOPNOTSUPP;

	if (vp->v_type != VREG && vp->v_type != VDIR) {
		SMBFSERR("vn types other than VREG or VDIR are unsupported !\n");
		return EIO;
	}
	if (uiop->uio_resid == 0)
		return 0;
	if (uiop->uio_offset < 0)
		return EINVAL;
/*	if (uiop->uio_offset + uiop->uio_resid > smp->nm_maxfilesize)
		return EFBIG;*/
	td = uiop->uio_td;
	if (vp->v_type == VDIR) {
		lks = LK_EXCLUSIVE;	/* lockstatus(vp->v_vnlock); */
		if (lks == LK_SHARED)
			vn_lock(vp, LK_UPGRADE | LK_RETRY);
		error = smbfs_readvdir(vp, uiop, cred);
		if (lks == LK_SHARED)
			vn_lock(vp, LK_DOWNGRADE | LK_RETRY);
		return error;
	}

/*	biosize = SSTOCN(smp->sm_share)->sc_txmax;*/
	if (np->n_flag & NMODIFIED) {
		smbfs_attr_cacheremove(vp);
		error = VOP_GETATTR(vp, &vattr, cred);
		if (error)
			return error;
		np->n_mtime.tv_sec = vattr.va_mtime.tv_sec;
	} else {
		error = VOP_GETATTR(vp, &vattr, cred);
		if (error)
			return error;
		if (np->n_mtime.tv_sec != vattr.va_mtime.tv_sec) {
			error = smbfs_vinvalbuf(vp, td);
			if (error)
				return error;
			np->n_mtime.tv_sec = vattr.va_mtime.tv_sec;
		}
	}
	scred = smbfs_malloc_scred();
	smb_makescred(scred, td, cred);
	error = smb_read(smp->sm_share, np->n_fid, uiop, scred);
	smbfs_free_scred(scred);
	return (error);
}

int
smbfs_writevnode(struct vnode *vp, struct uio *uiop,
	struct ucred *cred, int ioflag)
{
	struct smbmount *smp = VTOSMBFS(vp);
	struct smbnode *np = VTOSMB(vp);
	struct smb_cred *scred;
	struct thread *td;
	int error = 0;

	if (vp->v_type != VREG) {
		SMBERROR("vn types other than VREG unsupported !\n");
		return EIO;
	}
	SMBVDEBUG("ofs=%jd,resid=%zd\n", (intmax_t)uiop->uio_offset, 
	    uiop->uio_resid);
	if (uiop->uio_offset < 0)
		return EINVAL;
/*	if (uiop->uio_offset + uiop->uio_resid > smp->nm_maxfilesize)
		return (EFBIG);*/
	td = uiop->uio_td;
	if (ioflag & (IO_APPEND | IO_SYNC)) {
		if (np->n_flag & NMODIFIED) {
			smbfs_attr_cacheremove(vp);
			error = smbfs_vinvalbuf(vp, td);
			if (error)
				return error;
		}
		if (ioflag & IO_APPEND) {
#ifdef notyet
			/*
			 * File size can be changed by another client
			 */
			smbfs_attr_cacheremove(vp);
			error = VOP_GETATTR(vp, &vattr, cred);
			if (error) return (error);
#endif
			uiop->uio_offset = np->n_size;
		}
	}
	if (uiop->uio_resid == 0)
		return 0;

	if (vn_rlimit_fsize(vp, uiop, td))
		return (EFBIG);
	
	scred = smbfs_malloc_scred();
	smb_makescred(scred, td, cred);
	error = smb_write(smp->sm_share, np->n_fid, uiop, scred);
	smbfs_free_scred(scred);
	SMBVDEBUG("after: ofs=%jd,resid=%zd\n", (intmax_t)uiop->uio_offset, 
	    uiop->uio_resid);
	if (!error) {
		if (uiop->uio_offset > np->n_size) {
			np->n_size = uiop->uio_offset;
			vnode_pager_setsize(vp, np->n_size);
		}
	}
	return error;
}

/*
 * Do an I/O operation to/from a cache block.
 */
int
smbfs_doio(struct vnode *vp, struct buf *bp, struct ucred *cr, struct thread *td)
{
	struct smbmount *smp = VFSTOSMBFS(vp->v_mount);
	struct smbnode *np = VTOSMB(vp);
	struct uio *uiop;
	struct iovec io;
	struct smb_cred *scred;
	int error = 0;

	uiop = malloc(sizeof(struct uio), M_SMBFSDATA, M_WAITOK);
	uiop->uio_iov = &io;
	uiop->uio_iovcnt = 1;
	uiop->uio_segflg = UIO_SYSSPACE;
	uiop->uio_td = td;

	scred = smbfs_malloc_scred();
	smb_makescred(scred, td, cr);

	if (bp->b_iocmd == BIO_READ) {
	    io.iov_len = uiop->uio_resid = bp->b_bcount;
	    io.iov_base = bp->b_data;
	    uiop->uio_rw = UIO_READ;
	    switch (vp->v_type) {
	      case VREG:
		uiop->uio_offset = ((off_t)bp->b_blkno) * DEV_BSIZE;
		error = smb_read(smp->sm_share, np->n_fid, uiop, scred);
		if (error)
			break;
		if (uiop->uio_resid) {
			int left = uiop->uio_resid;
			int nread = bp->b_bcount - left;
			if (left > 0)
			    bzero((char *)bp->b_data + nread, left);
		}
		break;
	    default:
		printf("smbfs_doio:  type %x unexpected\n",vp->v_type);
		break;
	    }
	    if (error) {
		bp->b_error = error;
		bp->b_ioflags |= BIO_ERROR;
	    }
	} else { /* write */
	    if (((bp->b_blkno * DEV_BSIZE) + bp->b_dirtyend) > np->n_size)
		bp->b_dirtyend = np->n_size - (bp->b_blkno * DEV_BSIZE);

	    if (bp->b_dirtyend > bp->b_dirtyoff) {
		io.iov_len = uiop->uio_resid = bp->b_dirtyend - bp->b_dirtyoff;
		uiop->uio_offset = ((off_t)bp->b_blkno) * DEV_BSIZE + bp->b_dirtyoff;
		io.iov_base = (char *)bp->b_data + bp->b_dirtyoff;
		uiop->uio_rw = UIO_WRITE;
		error = smb_write(smp->sm_share, np->n_fid, uiop, scred);

		/*
		 * For an interrupted write, the buffer is still valid
		 * and the write hasn't been pushed to the server yet,
		 * so we can't set BIO_ERROR and report the interruption
		 * by setting B_EINTR. For the B_ASYNC case, B_EINTR
		 * is not relevant, so the rpc attempt is essentially
		 * a noop.  For the case of a V3 write rpc not being
		 * committed to stable storage, the block is still
		 * dirty and requires either a commit rpc or another
		 * write rpc with iomode == NFSV3WRITE_FILESYNC before
		 * the block is reused. This is indicated by setting
		 * the B_DELWRI and B_NEEDCOMMIT flags.
		 */
		if (error == EINTR
		    || (!error && (bp->b_flags & B_NEEDCOMMIT))) {
			int s;

			s = splbio();
			bp->b_flags &= ~(B_INVAL|B_NOCACHE);
			if ((bp->b_flags & B_ASYNC) == 0)
			    bp->b_flags |= B_EINTR;
			if ((bp->b_flags & B_PAGING) == 0) {
			    bdirty(bp);
			    bp->b_flags &= ~B_DONE;
			}
			if ((bp->b_flags & B_ASYNC) == 0)
			    bp->b_flags |= B_EINTR;
			splx(s);
		} else {
			if (error) {
				bp->b_ioflags |= BIO_ERROR;
				bp->b_error = error;
			}
			bp->b_dirtyoff = bp->b_dirtyend = 0;
		}
	    } else {
		bp->b_resid = 0;
		bufdone(bp);
		free(uiop, M_SMBFSDATA);
		smbfs_free_scred(scred);
		return 0;
	    }
	}
	bp->b_resid = uiop->uio_resid;
	bufdone(bp);
	free(uiop, M_SMBFSDATA);
	smbfs_free_scred(scred);
	return error;
}

/*
 * Vnode op for VM getpages.
 * Wish wish .... get rid from multiple IO routines
 */
int
smbfs_getpages(ap)
	struct vop_getpages_args /* {
		struct vnode *a_vp;
		vm_page_t *a_m;
		int a_count;
		int a_reqpage;
	} */ *ap;
{
#ifdef SMBFS_RWGENERIC
	return vop_stdgetpages(ap);
#else
	int i, error, nextoff, size, toff, npages, count;
	struct uio uio;
	struct iovec iov;
	vm_offset_t kva;
	struct buf *bp;
	struct vnode *vp;
	struct thread *td;
	struct ucred *cred;
	struct smbmount *smp;
	struct smbnode *np;
	struct smb_cred *scred;
	vm_object_t object;
	vm_page_t *pages;

	vp = ap->a_vp;
	if ((object = vp->v_object) == NULL) {
		printf("smbfs_getpages: called with non-merged cache vnode??\n");
		return VM_PAGER_ERROR;
	}

	td = curthread;				/* XXX */
	cred = td->td_ucred;		/* XXX */
	np = VTOSMB(vp);
	smp = VFSTOSMBFS(vp->v_mount);
	pages = ap->a_m;
	npages = ap->a_count;

	/*
	 * If the requested page is partially valid, just return it and
	 * allow the pager to zero-out the blanks.  Partially valid pages
	 * can only occur at the file EOF.
	 *
	 * XXXGL: is that true for SMB filesystem?
	 */
	VM_OBJECT_WLOCK(object);
	if (pages[npages - 1]->valid != 0 && --npages == 0)
		goto out;
	VM_OBJECT_WUNLOCK(object);

	scred = smbfs_malloc_scred();
	smb_makescred(scred, td, cred);

	bp = uma_zalloc(smbfs_pbuf_zone, M_WAITOK);

	kva = (vm_offset_t) bp->b_data;
	pmap_qenter(kva, pages, npages);
	VM_CNT_INC(v_vnodein);
	VM_CNT_ADD(v_vnodepgsin, npages);

	count = npages << PAGE_SHIFT;
	iov.iov_base = (caddr_t) kva;
	iov.iov_len = count;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = IDX_TO_OFF(pages[0]->pindex);
	uio.uio_resid = count;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_td = td;

	error = smb_read(smp->sm_share, np->n_fid, &uio, scred);
	smbfs_free_scred(scred);
	pmap_qremove(kva, npages);

	uma_zfree(smbfs_pbuf_zone, bp);

	if (error && (uio.uio_resid == count)) {
		printf("smbfs_getpages: error %d\n",error);
		return VM_PAGER_ERROR;
	}

	size = count - uio.uio_resid;

	VM_OBJECT_WLOCK(object);
	for (i = 0, toff = 0; i < npages; i++, toff = nextoff) {
		vm_page_t m;
		nextoff = toff + PAGE_SIZE;
		m = pages[i];

		if (nextoff <= size) {
			/*
			 * Read operation filled an entire page
			 */
			m->valid = VM_PAGE_BITS_ALL;
			KASSERT(m->dirty == 0,
			    ("smbfs_getpages: page %p is dirty", m));
		} else if (size > toff) {
			/*
			 * Read operation filled a partial page.
			 */
			m->valid = 0;
			vm_page_set_valid_range(m, 0, size - toff);
			KASSERT(m->dirty == 0,
			    ("smbfs_getpages: page %p is dirty", m));
		} else {
			/*
			 * Read operation was short.  If no error occurred
			 * we may have hit a zero-fill section.   We simply
			 * leave valid set to 0.
			 */
			;
		}
	}
out:
	VM_OBJECT_WUNLOCK(object);
	if (ap->a_rbehind)
		*ap->a_rbehind = 0;
	if (ap->a_rahead)
		*ap->a_rahead = 0;
	return (VM_PAGER_OK);
#endif /* SMBFS_RWGENERIC */
}

/*
 * Vnode op for VM putpages.
 * possible bug: all IO done in sync mode
 * Note that vop_close always invalidate pages before close, so it's
 * not necessary to open vnode.
 */
int
smbfs_putpages(ap)
	struct vop_putpages_args /* {
		struct vnode *a_vp;
		vm_page_t *a_m;
		int a_count;
		int a_sync;
		int *a_rtvals;
	} */ *ap;
{
	int error;
	struct vnode *vp = ap->a_vp;
	struct thread *td;
	struct ucred *cred;

#ifdef SMBFS_RWGENERIC
	td = curthread;			/* XXX */
	cred = td->td_ucred;		/* XXX */
	VOP_OPEN(vp, FWRITE, cred, td, NULL);
	error = vop_stdputpages(ap);
	VOP_CLOSE(vp, FWRITE, cred, td);
	return error;
#else
	struct uio uio;
	struct iovec iov;
	vm_offset_t kva;
	struct buf *bp;
	int i, npages, count;
	int *rtvals;
	struct smbmount *smp;
	struct smbnode *np;
	struct smb_cred *scred;
	vm_page_t *pages;

	td = curthread;			/* XXX */
	cred = td->td_ucred;		/* XXX */
/*	VOP_OPEN(vp, FWRITE, cred, td, NULL);*/
	np = VTOSMB(vp);
	smp = VFSTOSMBFS(vp->v_mount);
	pages = ap->a_m;
	count = ap->a_count;
	rtvals = ap->a_rtvals;
	npages = btoc(count);

	for (i = 0; i < npages; i++) {
		rtvals[i] = VM_PAGER_ERROR;
	}

	bp = uma_zalloc(smbfs_pbuf_zone, M_WAITOK);

	kva = (vm_offset_t) bp->b_data;
	pmap_qenter(kva, pages, npages);
	VM_CNT_INC(v_vnodeout);
	VM_CNT_ADD(v_vnodepgsout, count);

	iov.iov_base = (caddr_t) kva;
	iov.iov_len = count;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = IDX_TO_OFF(pages[0]->pindex);
	uio.uio_resid = count;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_WRITE;
	uio.uio_td = td;
	SMBVDEBUG("ofs=%jd,resid=%zd\n", (intmax_t)uio.uio_offset, 
	    uio.uio_resid);

	scred = smbfs_malloc_scred();
	smb_makescred(scred, td, cred);
	error = smb_write(smp->sm_share, np->n_fid, &uio, scred);
	smbfs_free_scred(scred);
/*	VOP_CLOSE(vp, FWRITE, cred, td);*/
	SMBVDEBUG("paged write done: %d\n", error);

	pmap_qremove(kva, npages);

	uma_zfree(smbfs_pbuf_zone, bp);

	if (error == 0) {
		vnode_pager_undirty_pages(pages, rtvals, count - uio.uio_resid,
		    npages * PAGE_SIZE, npages * PAGE_SIZE);
	}
	return (rtvals[0]);
#endif /* SMBFS_RWGENERIC */
}

/*
 * Flush and invalidate all dirty buffers. If another process is already
 * doing the flush, just wait for completion.
 */
int
smbfs_vinvalbuf(struct vnode *vp, struct thread *td)
{
	struct smbnode *np = VTOSMB(vp);
	int error = 0;

	if (vp->v_iflag & VI_DOOMED)
		return 0;

	while (np->n_flag & NFLUSHINPROG) {
		np->n_flag |= NFLUSHWANT;
		error = tsleep(&np->n_flag, PRIBIO + 2, "smfsvinv", 2 * hz);
		error = smb_td_intr(td);
		if (error == EINTR)
			return EINTR;
	}
	np->n_flag |= NFLUSHINPROG;

	if (vp->v_bufobj.bo_object != NULL) {
		VM_OBJECT_WLOCK(vp->v_bufobj.bo_object);
		vm_object_page_clean(vp->v_bufobj.bo_object, 0, 0, OBJPC_SYNC);
		VM_OBJECT_WUNLOCK(vp->v_bufobj.bo_object);
	}

	error = vinvalbuf(vp, V_SAVE, PCATCH, 0);
	while (error) {
		if (error == ERESTART || error == EINTR) {
			np->n_flag &= ~NFLUSHINPROG;
			if (np->n_flag & NFLUSHWANT) {
				np->n_flag &= ~NFLUSHWANT;
				wakeup(&np->n_flag);
			}
			return EINTR;
		}
		error = vinvalbuf(vp, V_SAVE, PCATCH, 0);
	}
	np->n_flag &= ~(NMODIFIED | NFLUSHINPROG);
	if (np->n_flag & NFLUSHWANT) {
		np->n_flag &= ~NFLUSHWANT;
		wakeup(&np->n_flag);
	}
	return (error);
}
