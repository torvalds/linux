/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1990 University of Utah.
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1993, 1994 John S. Dyson
 * Copyright (c) 1995, David Greenman
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)vnode_pager.c	7.5 (Berkeley) 4/20/91
 */

/*
 * Page to/from files (vnodes).
 */

/*
 * TODO:
 *	Implement VOP_GETPAGES/PUTPAGES interface for filesystems. Will
 *	greatly re-simplify the vnode_pager.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_vm.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/vmmeter.h>
#include <sys/limits.h>
#include <sys/conf.h>
#include <sys/rwlock.h>
#include <sys/sf_buf.h>
#include <sys/domainset.h>

#include <machine/atomic.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_map.h>
#include <vm/vnode_pager.h>
#include <vm/vm_extern.h>
#include <vm/uma.h>

static int vnode_pager_addr(struct vnode *vp, vm_ooffset_t address,
    daddr_t *rtaddress, int *run);
static int vnode_pager_input_smlfs(vm_object_t object, vm_page_t m);
static int vnode_pager_input_old(vm_object_t object, vm_page_t m);
static void vnode_pager_dealloc(vm_object_t);
static int vnode_pager_getpages(vm_object_t, vm_page_t *, int, int *, int *);
static int vnode_pager_getpages_async(vm_object_t, vm_page_t *, int, int *,
    int *, vop_getpages_iodone_t, void *);
static void vnode_pager_putpages(vm_object_t, vm_page_t *, int, int, int *);
static boolean_t vnode_pager_haspage(vm_object_t, vm_pindex_t, int *, int *);
static vm_object_t vnode_pager_alloc(void *, vm_ooffset_t, vm_prot_t,
    vm_ooffset_t, struct ucred *cred);
static int vnode_pager_generic_getpages_done(struct buf *);
static void vnode_pager_generic_getpages_done_async(struct buf *);

struct pagerops vnodepagerops = {
	.pgo_alloc =	vnode_pager_alloc,
	.pgo_dealloc =	vnode_pager_dealloc,
	.pgo_getpages =	vnode_pager_getpages,
	.pgo_getpages_async = vnode_pager_getpages_async,
	.pgo_putpages =	vnode_pager_putpages,
	.pgo_haspage =	vnode_pager_haspage,
};

static struct domainset *vnode_domainset = NULL;

SYSCTL_PROC(_debug, OID_AUTO, vnode_domainset, CTLTYPE_STRING | CTLFLAG_RW,
    &vnode_domainset, 0, sysctl_handle_domainset, "A",
    "Default vnode NUMA policy");

static int nvnpbufs;
SYSCTL_INT(_vm, OID_AUTO, vnode_pbufs, CTLFLAG_RDTUN | CTLFLAG_NOFETCH,
    &nvnpbufs, 0, "number of physical buffers allocated for vnode pager");

static uma_zone_t vnode_pbuf_zone;

static void
vnode_pager_init(void *dummy)
{

#ifdef __LP64__
	nvnpbufs = nswbuf * 2;
#else
	nvnpbufs = nswbuf / 2;
#endif
	TUNABLE_INT_FETCH("vm.vnode_pbufs", &nvnpbufs);
	vnode_pbuf_zone = pbuf_zsecond_create("vnpbuf", nvnpbufs);
}
SYSINIT(vnode_pager, SI_SUB_CPU, SI_ORDER_ANY, vnode_pager_init, NULL);

/* Create the VM system backing object for this vnode */
int
vnode_create_vobject(struct vnode *vp, off_t isize, struct thread *td)
{
	vm_object_t object;
	vm_ooffset_t size = isize;
	struct vattr va;

	if (!vn_isdisk(vp, NULL) && vn_canvmio(vp) == FALSE)
		return (0);

	while ((object = vp->v_object) != NULL) {
		VM_OBJECT_WLOCK(object);
		if (!(object->flags & OBJ_DEAD)) {
			VM_OBJECT_WUNLOCK(object);
			return (0);
		}
		VOP_UNLOCK(vp, 0);
		vm_object_set_flag(object, OBJ_DISCONNECTWNT);
		VM_OBJECT_SLEEP(object, object, PDROP | PVM, "vodead", 0);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	}

	if (size == 0) {
		if (vn_isdisk(vp, NULL)) {
			size = IDX_TO_OFF(INT_MAX);
		} else {
			if (VOP_GETATTR(vp, &va, td->td_ucred))
				return (0);
			size = va.va_size;
		}
	}

	object = vnode_pager_alloc(vp, size, 0, 0, td->td_ucred);
	/*
	 * Dereference the reference we just created.  This assumes
	 * that the object is associated with the vp.
	 */
	VM_OBJECT_WLOCK(object);
	object->ref_count--;
	VM_OBJECT_WUNLOCK(object);
	vrele(vp);

	KASSERT(vp->v_object != NULL, ("vnode_create_vobject: NULL object"));

	return (0);
}

void
vnode_destroy_vobject(struct vnode *vp)
{
	struct vm_object *obj;

	obj = vp->v_object;
	if (obj == NULL)
		return;
	ASSERT_VOP_ELOCKED(vp, "vnode_destroy_vobject");
	VM_OBJECT_WLOCK(obj);
	umtx_shm_object_terminated(obj);
	if (obj->ref_count == 0) {
		/*
		 * don't double-terminate the object
		 */
		if ((obj->flags & OBJ_DEAD) == 0) {
			vm_object_terminate(obj);
		} else {
			/*
			 * Waiters were already handled during object
			 * termination.  The exclusive vnode lock hopefully
			 * prevented new waiters from referencing the dying
			 * object.
			 */
			KASSERT((obj->flags & OBJ_DISCONNECTWNT) == 0,
			    ("OBJ_DISCONNECTWNT set obj %p flags %x",
			    obj, obj->flags));
			vp->v_object = NULL;
			VM_OBJECT_WUNLOCK(obj);
		}
	} else {
		/*
		 * Woe to the process that tries to page now :-).
		 */
		vm_pager_deallocate(obj);
		VM_OBJECT_WUNLOCK(obj);
	}
	KASSERT(vp->v_object == NULL, ("vp %p obj %p", vp, vp->v_object));
}


/*
 * Allocate (or lookup) pager for a vnode.
 * Handle is a vnode pointer.
 *
 * MPSAFE
 */
vm_object_t
vnode_pager_alloc(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t offset, struct ucred *cred)
{
	vm_object_t object;
	struct vnode *vp;

	/*
	 * Pageout to vnode, no can do yet.
	 */
	if (handle == NULL)
		return (NULL);

	vp = (struct vnode *) handle;

	/*
	 * If the object is being terminated, wait for it to
	 * go away.
	 */
retry:
	while ((object = vp->v_object) != NULL) {
		VM_OBJECT_WLOCK(object);
		if ((object->flags & OBJ_DEAD) == 0)
			break;
		vm_object_set_flag(object, OBJ_DISCONNECTWNT);
		VM_OBJECT_SLEEP(object, object, PDROP | PVM, "vadead", 0);
	}

	KASSERT(vp->v_usecount != 0, ("vnode_pager_alloc: no vnode reference"));

	if (object == NULL) {
		/*
		 * Add an object of the appropriate size
		 */
		object = vm_object_allocate(OBJT_VNODE, OFF_TO_IDX(round_page(size)));

		object->un_pager.vnp.vnp_size = size;
		object->un_pager.vnp.writemappings = 0;
		object->domain.dr_policy = vnode_domainset;

		object->handle = handle;
		VI_LOCK(vp);
		if (vp->v_object != NULL) {
			/*
			 * Object has been created while we were sleeping
			 */
			VI_UNLOCK(vp);
			VM_OBJECT_WLOCK(object);
			KASSERT(object->ref_count == 1,
			    ("leaked ref %p %d", object, object->ref_count));
			object->type = OBJT_DEAD;
			object->ref_count = 0;
			VM_OBJECT_WUNLOCK(object);
			vm_object_destroy(object);
			goto retry;
		}
		vp->v_object = object;
		VI_UNLOCK(vp);
	} else {
		object->ref_count++;
#if VM_NRESERVLEVEL > 0
		vm_object_color(object, 0);
#endif
		VM_OBJECT_WUNLOCK(object);
	}
	vrefact(vp);
	return (object);
}

/*
 *	The object must be locked.
 */
static void
vnode_pager_dealloc(vm_object_t object)
{
	struct vnode *vp;
	int refs;

	vp = object->handle;
	if (vp == NULL)
		panic("vnode_pager_dealloc: pager already dealloced");

	VM_OBJECT_ASSERT_WLOCKED(object);
	vm_object_pip_wait(object, "vnpdea");
	refs = object->ref_count;

	object->handle = NULL;
	object->type = OBJT_DEAD;
	if (object->flags & OBJ_DISCONNECTWNT) {
		vm_object_clear_flag(object, OBJ_DISCONNECTWNT);
		wakeup(object);
	}
	ASSERT_VOP_ELOCKED(vp, "vnode_pager_dealloc");
	if (object->un_pager.vnp.writemappings > 0) {
		object->un_pager.vnp.writemappings = 0;
		VOP_ADD_WRITECOUNT(vp, -1);
		CTR3(KTR_VFS, "%s: vp %p v_writecount decreased to %d",
		    __func__, vp, vp->v_writecount);
	}
	vp->v_object = NULL;
	VOP_UNSET_TEXT(vp);
	VM_OBJECT_WUNLOCK(object);
	while (refs-- > 0)
		vunref(vp);
	VM_OBJECT_WLOCK(object);
}

static boolean_t
vnode_pager_haspage(vm_object_t object, vm_pindex_t pindex, int *before,
    int *after)
{
	struct vnode *vp = object->handle;
	daddr_t bn;
	int err;
	daddr_t reqblock;
	int poff;
	int bsize;
	int pagesperblock, blocksperpage;

	VM_OBJECT_ASSERT_WLOCKED(object);
	/*
	 * If no vp or vp is doomed or marked transparent to VM, we do not
	 * have the page.
	 */
	if (vp == NULL || vp->v_iflag & VI_DOOMED)
		return FALSE;
	/*
	 * If the offset is beyond end of file we do
	 * not have the page.
	 */
	if (IDX_TO_OFF(pindex) >= object->un_pager.vnp.vnp_size)
		return FALSE;

	bsize = vp->v_mount->mnt_stat.f_iosize;
	pagesperblock = bsize / PAGE_SIZE;
	blocksperpage = 0;
	if (pagesperblock > 0) {
		reqblock = pindex / pagesperblock;
	} else {
		blocksperpage = (PAGE_SIZE / bsize);
		reqblock = pindex * blocksperpage;
	}
	VM_OBJECT_WUNLOCK(object);
	err = VOP_BMAP(vp, reqblock, NULL, &bn, after, before);
	VM_OBJECT_WLOCK(object);
	if (err)
		return TRUE;
	if (bn == -1)
		return FALSE;
	if (pagesperblock > 0) {
		poff = pindex - (reqblock * pagesperblock);
		if (before) {
			*before *= pagesperblock;
			*before += poff;
		}
		if (after) {
			/*
			 * The BMAP vop can report a partial block in the
			 * 'after', but must not report blocks after EOF.
			 * Assert the latter, and truncate 'after' in case
			 * of the former.
			 */
			KASSERT((reqblock + *after) * pagesperblock <
			    roundup2(object->size, pagesperblock),
			    ("%s: reqblock %jd after %d size %ju", __func__,
			    (intmax_t )reqblock, *after,
			    (uintmax_t )object->size));
			*after *= pagesperblock;
			*after += pagesperblock - (poff + 1);
			if (pindex + *after >= object->size)
				*after = object->size - 1 - pindex;
		}
	} else {
		if (before) {
			*before /= blocksperpage;
		}

		if (after) {
			*after /= blocksperpage;
		}
	}
	return TRUE;
}

/*
 * Lets the VM system know about a change in size for a file.
 * We adjust our own internal size and flush any cached pages in
 * the associated object that are affected by the size change.
 *
 * Note: this routine may be invoked as a result of a pager put
 * operation (possibly at object termination time), so we must be careful.
 */
void
vnode_pager_setsize(struct vnode *vp, vm_ooffset_t nsize)
{
	vm_object_t object;
	vm_page_t m;
	vm_pindex_t nobjsize;

	if ((object = vp->v_object) == NULL)
		return;
/* 	ASSERT_VOP_ELOCKED(vp, "vnode_pager_setsize and not locked vnode"); */
	VM_OBJECT_WLOCK(object);
	if (object->type == OBJT_DEAD) {
		VM_OBJECT_WUNLOCK(object);
		return;
	}
	KASSERT(object->type == OBJT_VNODE,
	    ("not vnode-backed object %p", object));
	if (nsize == object->un_pager.vnp.vnp_size) {
		/*
		 * Hasn't changed size
		 */
		VM_OBJECT_WUNLOCK(object);
		return;
	}
	nobjsize = OFF_TO_IDX(nsize + PAGE_MASK);
	if (nsize < object->un_pager.vnp.vnp_size) {
		/*
		 * File has shrunk. Toss any cached pages beyond the new EOF.
		 */
		if (nobjsize < object->size)
			vm_object_page_remove(object, nobjsize, object->size,
			    0);
		/*
		 * this gets rid of garbage at the end of a page that is now
		 * only partially backed by the vnode.
		 *
		 * XXX for some reason (I don't know yet), if we take a
		 * completely invalid page and mark it partially valid
		 * it can screw up NFS reads, so we don't allow the case.
		 */
		if ((nsize & PAGE_MASK) &&
		    (m = vm_page_lookup(object, OFF_TO_IDX(nsize))) != NULL &&
		    m->valid != 0) {
			int base = (int)nsize & PAGE_MASK;
			int size = PAGE_SIZE - base;

			/*
			 * Clear out partial-page garbage in case
			 * the page has been mapped.
			 */
			pmap_zero_page_area(m, base, size);

			/*
			 * Update the valid bits to reflect the blocks that
			 * have been zeroed.  Some of these valid bits may
			 * have already been set.
			 */
			vm_page_set_valid_range(m, base, size);

			/*
			 * Round "base" to the next block boundary so that the
			 * dirty bit for a partially zeroed block is not
			 * cleared.
			 */
			base = roundup2(base, DEV_BSIZE);

			/*
			 * Clear out partial-page dirty bits.
			 *
			 * note that we do not clear out the valid
			 * bits.  This would prevent bogus_page
			 * replacement from working properly.
			 */
			vm_page_clear_dirty(m, base, PAGE_SIZE - base);
		}
	}
	object->un_pager.vnp.vnp_size = nsize;
	object->size = nobjsize;
	VM_OBJECT_WUNLOCK(object);
}

/*
 * calculate the linear (byte) disk address of specified virtual
 * file address
 */
static int
vnode_pager_addr(struct vnode *vp, vm_ooffset_t address, daddr_t *rtaddress,
    int *run)
{
	int bsize;
	int err;
	daddr_t vblock;
	daddr_t voffset;

	if (address < 0)
		return -1;

	if (vp->v_iflag & VI_DOOMED)
		return -1;

	bsize = vp->v_mount->mnt_stat.f_iosize;
	vblock = address / bsize;
	voffset = address % bsize;

	err = VOP_BMAP(vp, vblock, NULL, rtaddress, run, NULL);
	if (err == 0) {
		if (*rtaddress != -1)
			*rtaddress += voffset / DEV_BSIZE;
		if (run) {
			*run += 1;
			*run *= bsize/PAGE_SIZE;
			*run -= voffset/PAGE_SIZE;
		}
	}

	return (err);
}

/*
 * small block filesystem vnode pager input
 */
static int
vnode_pager_input_smlfs(vm_object_t object, vm_page_t m)
{
	struct vnode *vp;
	struct bufobj *bo;
	struct buf *bp;
	struct sf_buf *sf;
	daddr_t fileaddr;
	vm_offset_t bsize;
	vm_page_bits_t bits;
	int error, i;

	error = 0;
	vp = object->handle;
	if (vp->v_iflag & VI_DOOMED)
		return VM_PAGER_BAD;

	bsize = vp->v_mount->mnt_stat.f_iosize;

	VOP_BMAP(vp, 0, &bo, 0, NULL, NULL);

	sf = sf_buf_alloc(m, 0);

	for (i = 0; i < PAGE_SIZE / bsize; i++) {
		vm_ooffset_t address;

		bits = vm_page_bits(i * bsize, bsize);
		if (m->valid & bits)
			continue;

		address = IDX_TO_OFF(m->pindex) + i * bsize;
		if (address >= object->un_pager.vnp.vnp_size) {
			fileaddr = -1;
		} else {
			error = vnode_pager_addr(vp, address, &fileaddr, NULL);
			if (error)
				break;
		}
		if (fileaddr != -1) {
			bp = uma_zalloc(vnode_pbuf_zone, M_WAITOK);

			/* build a minimal buffer header */
			bp->b_iocmd = BIO_READ;
			bp->b_iodone = bdone;
			KASSERT(bp->b_rcred == NOCRED, ("leaking read ucred"));
			KASSERT(bp->b_wcred == NOCRED, ("leaking write ucred"));
			bp->b_rcred = crhold(curthread->td_ucred);
			bp->b_wcred = crhold(curthread->td_ucred);
			bp->b_data = (caddr_t)sf_buf_kva(sf) + i * bsize;
			bp->b_blkno = fileaddr;
			pbgetbo(bo, bp);
			bp->b_vp = vp;
			bp->b_bcount = bsize;
			bp->b_bufsize = bsize;
			bp->b_runningbufspace = bp->b_bufsize;
			atomic_add_long(&runningbufspace, bp->b_runningbufspace);

			/* do the input */
			bp->b_iooffset = dbtob(bp->b_blkno);
			bstrategy(bp);

			bwait(bp, PVM, "vnsrd");

			if ((bp->b_ioflags & BIO_ERROR) != 0)
				error = EIO;

			/*
			 * free the buffer header back to the swap buffer pool
			 */
			bp->b_vp = NULL;
			pbrelbo(bp);
			uma_zfree(vnode_pbuf_zone, bp);
			if (error)
				break;
		} else
			bzero((caddr_t)sf_buf_kva(sf) + i * bsize, bsize);
		KASSERT((m->dirty & bits) == 0,
		    ("vnode_pager_input_smlfs: page %p is dirty", m));
		VM_OBJECT_WLOCK(object);
		m->valid |= bits;
		VM_OBJECT_WUNLOCK(object);
	}
	sf_buf_free(sf);
	if (error) {
		return VM_PAGER_ERROR;
	}
	return VM_PAGER_OK;
}

/*
 * old style vnode pager input routine
 */
static int
vnode_pager_input_old(vm_object_t object, vm_page_t m)
{
	struct uio auio;
	struct iovec aiov;
	int error;
	int size;
	struct sf_buf *sf;
	struct vnode *vp;

	VM_OBJECT_ASSERT_WLOCKED(object);
	error = 0;

	/*
	 * Return failure if beyond current EOF
	 */
	if (IDX_TO_OFF(m->pindex) >= object->un_pager.vnp.vnp_size) {
		return VM_PAGER_BAD;
	} else {
		size = PAGE_SIZE;
		if (IDX_TO_OFF(m->pindex) + size > object->un_pager.vnp.vnp_size)
			size = object->un_pager.vnp.vnp_size - IDX_TO_OFF(m->pindex);
		vp = object->handle;
		VM_OBJECT_WUNLOCK(object);

		/*
		 * Allocate a kernel virtual address and initialize so that
		 * we can use VOP_READ/WRITE routines.
		 */
		sf = sf_buf_alloc(m, 0);

		aiov.iov_base = (caddr_t)sf_buf_kva(sf);
		aiov.iov_len = size;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = IDX_TO_OFF(m->pindex);
		auio.uio_segflg = UIO_SYSSPACE;
		auio.uio_rw = UIO_READ;
		auio.uio_resid = size;
		auio.uio_td = curthread;

		error = VOP_READ(vp, &auio, 0, curthread->td_ucred);
		if (!error) {
			int count = size - auio.uio_resid;

			if (count == 0)
				error = EINVAL;
			else if (count != PAGE_SIZE)
				bzero((caddr_t)sf_buf_kva(sf) + count,
				    PAGE_SIZE - count);
		}
		sf_buf_free(sf);

		VM_OBJECT_WLOCK(object);
	}
	KASSERT(m->dirty == 0, ("vnode_pager_input_old: page %p is dirty", m));
	if (!error)
		m->valid = VM_PAGE_BITS_ALL;
	return error ? VM_PAGER_ERROR : VM_PAGER_OK;
}

/*
 * generic vnode pager input routine
 */

/*
 * Local media VFS's that do not implement their own VOP_GETPAGES
 * should have their VOP_GETPAGES call to vnode_pager_generic_getpages()
 * to implement the previous behaviour.
 *
 * All other FS's should use the bypass to get to the local media
 * backing vp's VOP_GETPAGES.
 */
static int
vnode_pager_getpages(vm_object_t object, vm_page_t *m, int count, int *rbehind,
    int *rahead)
{
	struct vnode *vp;
	int rtval;

	vp = object->handle;
	VM_OBJECT_WUNLOCK(object);
	rtval = VOP_GETPAGES(vp, m, count, rbehind, rahead);
	KASSERT(rtval != EOPNOTSUPP,
	    ("vnode_pager: FS getpages not implemented\n"));
	VM_OBJECT_WLOCK(object);
	return rtval;
}

static int
vnode_pager_getpages_async(vm_object_t object, vm_page_t *m, int count,
    int *rbehind, int *rahead, vop_getpages_iodone_t iodone, void *arg)
{
	struct vnode *vp;
	int rtval;

	vp = object->handle;
	VM_OBJECT_WUNLOCK(object);
	rtval = VOP_GETPAGES_ASYNC(vp, m, count, rbehind, rahead, iodone, arg);
	KASSERT(rtval != EOPNOTSUPP,
	    ("vnode_pager: FS getpages_async not implemented\n"));
	VM_OBJECT_WLOCK(object);
	return (rtval);
}

/*
 * The implementation of VOP_GETPAGES() and VOP_GETPAGES_ASYNC() for
 * local filesystems, where partially valid pages can only occur at
 * the end of file.
 */
int
vnode_pager_local_getpages(struct vop_getpages_args *ap)
{

	return (vnode_pager_generic_getpages(ap->a_vp, ap->a_m, ap->a_count,
	    ap->a_rbehind, ap->a_rahead, NULL, NULL));
}

int
vnode_pager_local_getpages_async(struct vop_getpages_async_args *ap)
{

	return (vnode_pager_generic_getpages(ap->a_vp, ap->a_m, ap->a_count,
	    ap->a_rbehind, ap->a_rahead, ap->a_iodone, ap->a_arg));
}

/*
 * This is now called from local media FS's to operate against their
 * own vnodes if they fail to implement VOP_GETPAGES.
 */
int
vnode_pager_generic_getpages(struct vnode *vp, vm_page_t *m, int count,
    int *a_rbehind, int *a_rahead, vop_getpages_iodone_t iodone, void *arg)
{
	vm_object_t object;
	struct bufobj *bo;
	struct buf *bp;
	off_t foff;
#ifdef INVARIANTS
	off_t blkno0;
#endif
	int bsize, pagesperblock;
	int error, before, after, rbehind, rahead, poff, i;
	int bytecount, secmask;

	KASSERT(vp->v_type != VCHR && vp->v_type != VBLK,
	    ("%s does not support devices", __func__));

	if (vp->v_iflag & VI_DOOMED)
		return (VM_PAGER_BAD);

	object = vp->v_object;
	foff = IDX_TO_OFF(m[0]->pindex);
	bsize = vp->v_mount->mnt_stat.f_iosize;
	pagesperblock = bsize / PAGE_SIZE;

	KASSERT(foff < object->un_pager.vnp.vnp_size,
	    ("%s: page %p offset beyond vp %p size", __func__, m[0], vp));
	KASSERT(count <= nitems(bp->b_pages),
	    ("%s: requested %d pages", __func__, count));

	/*
	 * The last page has valid blocks.  Invalid part can only
	 * exist at the end of file, and the page is made fully valid
	 * by zeroing in vm_pager_get_pages().
	 */
	if (m[count - 1]->valid != 0 && --count == 0) {
		if (iodone != NULL)
			iodone(arg, m, 1, 0);
		return (VM_PAGER_OK);
	}

	bp = uma_zalloc(vnode_pbuf_zone, M_WAITOK);

	/*
	 * Get the underlying device blocks for the file with VOP_BMAP().
	 * If the file system doesn't support VOP_BMAP, use old way of
	 * getting pages via VOP_READ.
	 */
	error = VOP_BMAP(vp, foff / bsize, &bo, &bp->b_blkno, &after, &before);
	if (error == EOPNOTSUPP) {
		uma_zfree(vnode_pbuf_zone, bp);
		VM_OBJECT_WLOCK(object);
		for (i = 0; i < count; i++) {
			VM_CNT_INC(v_vnodein);
			VM_CNT_INC(v_vnodepgsin);
			error = vnode_pager_input_old(object, m[i]);
			if (error)
				break;
		}
		VM_OBJECT_WUNLOCK(object);
		return (error);
	} else if (error != 0) {
		uma_zfree(vnode_pbuf_zone, bp);
		return (VM_PAGER_ERROR);
	}

	/*
	 * If the file system supports BMAP, but blocksize is smaller
	 * than a page size, then use special small filesystem code.
	 */
	if (pagesperblock == 0) {
		uma_zfree(vnode_pbuf_zone, bp);
		for (i = 0; i < count; i++) {
			VM_CNT_INC(v_vnodein);
			VM_CNT_INC(v_vnodepgsin);
			error = vnode_pager_input_smlfs(object, m[i]);
			if (error)
				break;
		}
		return (error);
	}

	/*
	 * A sparse file can be encountered only for a single page request,
	 * which may not be preceded by call to vm_pager_haspage().
	 */
	if (bp->b_blkno == -1) {
		KASSERT(count == 1,
		    ("%s: array[%d] request to a sparse file %p", __func__,
		    count, vp));
		uma_zfree(vnode_pbuf_zone, bp);
		pmap_zero_page(m[0]);
		KASSERT(m[0]->dirty == 0, ("%s: page %p is dirty",
		    __func__, m[0]));
		VM_OBJECT_WLOCK(object);
		m[0]->valid = VM_PAGE_BITS_ALL;
		VM_OBJECT_WUNLOCK(object);
		return (VM_PAGER_OK);
	}

#ifdef INVARIANTS
	blkno0 = bp->b_blkno;
#endif
	bp->b_blkno += (foff % bsize) / DEV_BSIZE;

	/* Recalculate blocks available after/before to pages. */
	poff = (foff % bsize) / PAGE_SIZE;
	before *= pagesperblock;
	before += poff;
	after *= pagesperblock;
	after += pagesperblock - (poff + 1);
	if (m[0]->pindex + after >= object->size)
		after = object->size - 1 - m[0]->pindex;
	KASSERT(count <= after + 1, ("%s: %d pages asked, can do only %d",
	    __func__, count, after + 1));
	after -= count - 1;

	/* Trim requested rbehind/rahead to possible values. */   
	rbehind = a_rbehind ? *a_rbehind : 0;
	rahead = a_rahead ? *a_rahead : 0;
	rbehind = min(rbehind, before);
	rbehind = min(rbehind, m[0]->pindex);
	rahead = min(rahead, after);
	rahead = min(rahead, object->size - m[count - 1]->pindex);
	/*
	 * Check that total amount of pages fit into buf.  Trim rbehind and
	 * rahead evenly if not.
	 */
	if (rbehind + rahead + count > nitems(bp->b_pages)) {
		int trim, sum;

		trim = rbehind + rahead + count - nitems(bp->b_pages) + 1;
		sum = rbehind + rahead;
		if (rbehind == before) {
			/* Roundup rbehind trim to block size. */
			rbehind -= roundup(trim * rbehind / sum, pagesperblock);
			if (rbehind < 0)
				rbehind = 0;
		} else
			rbehind -= trim * rbehind / sum;
		rahead -= trim * rahead / sum;
	}
	KASSERT(rbehind + rahead + count <= nitems(bp->b_pages),
	    ("%s: behind %d ahead %d count %d", __func__,
	    rbehind, rahead, count));

	/*
	 * Fill in the bp->b_pages[] array with requested and optional   
	 * read behind or read ahead pages.  Read behind pages are looked
	 * up in a backward direction, down to a first cached page.  Same
	 * for read ahead pages, but there is no need to shift the array
	 * in case of encountering a cached page.
	 */
	i = bp->b_npages = 0;
	if (rbehind) {
		vm_pindex_t startpindex, tpindex;
		vm_page_t p;

		VM_OBJECT_WLOCK(object);
		startpindex = m[0]->pindex - rbehind;
		if ((p = TAILQ_PREV(m[0], pglist, listq)) != NULL &&
		    p->pindex >= startpindex)
			startpindex = p->pindex + 1;

		/* tpindex is unsigned; beware of numeric underflow. */
		for (tpindex = m[0]->pindex - 1;
		    tpindex >= startpindex && tpindex < m[0]->pindex;
		    tpindex--, i++) {
			p = vm_page_alloc(object, tpindex, VM_ALLOC_NORMAL);
			if (p == NULL) {
				/* Shift the array. */
				for (int j = 0; j < i; j++)
					bp->b_pages[j] = bp->b_pages[j + 
					    tpindex + 1 - startpindex]; 
				break;
			}
			bp->b_pages[tpindex - startpindex] = p;
		}

		bp->b_pgbefore = i;
		bp->b_npages += i;
		bp->b_blkno -= IDX_TO_OFF(i) / DEV_BSIZE;
	} else
		bp->b_pgbefore = 0;

	/* Requested pages. */
	for (int j = 0; j < count; j++, i++)
		bp->b_pages[i] = m[j];
	bp->b_npages += count;

	if (rahead) {
		vm_pindex_t endpindex, tpindex;
		vm_page_t p;

		if (!VM_OBJECT_WOWNED(object))
			VM_OBJECT_WLOCK(object);
		endpindex = m[count - 1]->pindex + rahead + 1;
		if ((p = TAILQ_NEXT(m[count - 1], listq)) != NULL &&
		    p->pindex < endpindex)
			endpindex = p->pindex;
		if (endpindex > object->size)
			endpindex = object->size;

		for (tpindex = m[count - 1]->pindex + 1;
		    tpindex < endpindex; i++, tpindex++) {
			p = vm_page_alloc(object, tpindex, VM_ALLOC_NORMAL);
			if (p == NULL)
				break;
			bp->b_pages[i] = p;
		}

		bp->b_pgafter = i - bp->b_npages;
		bp->b_npages = i;
	} else
		bp->b_pgafter = 0;

	if (VM_OBJECT_WOWNED(object))
		VM_OBJECT_WUNLOCK(object);

	/* Report back actual behind/ahead read. */
	if (a_rbehind)
		*a_rbehind = bp->b_pgbefore;
	if (a_rahead)
		*a_rahead = bp->b_pgafter;

#ifdef INVARIANTS
	KASSERT(bp->b_npages <= nitems(bp->b_pages),
	    ("%s: buf %p overflowed", __func__, bp));
	for (int j = 1, prev = 0; j < bp->b_npages; j++) {
		if (bp->b_pages[j] == bogus_page)
			continue;
		KASSERT(bp->b_pages[j]->pindex - bp->b_pages[prev]->pindex ==
		    j - prev, ("%s: pages array not consecutive, bp %p",
		     __func__, bp));
		prev = j;
	}
#endif

	/*
	 * Recalculate first offset and bytecount with regards to read behind.
	 * Truncate bytecount to vnode real size and round up physical size
	 * for real devices.
	 */
	foff = IDX_TO_OFF(bp->b_pages[0]->pindex);
	bytecount = bp->b_npages << PAGE_SHIFT;
	if ((foff + bytecount) > object->un_pager.vnp.vnp_size)
		bytecount = object->un_pager.vnp.vnp_size - foff;
	secmask = bo->bo_bsize - 1;
	KASSERT(secmask < PAGE_SIZE && secmask > 0,
	    ("%s: sector size %d too large", __func__, secmask + 1));
	bytecount = (bytecount + secmask) & ~secmask;

	/*
	 * And map the pages to be read into the kva, if the filesystem
	 * requires mapped buffers.
	 */
	if ((vp->v_mount->mnt_kern_flag & MNTK_UNMAPPED_BUFS) != 0 &&
	    unmapped_buf_allowed) {
		bp->b_data = unmapped_buf;
		bp->b_offset = 0;
	} else {
		bp->b_data = bp->b_kvabase;
		pmap_qenter((vm_offset_t)bp->b_data, bp->b_pages, bp->b_npages);
	}

	/* Build a minimal buffer header. */
	bp->b_iocmd = BIO_READ;
	KASSERT(bp->b_rcred == NOCRED, ("leaking read ucred"));
	KASSERT(bp->b_wcred == NOCRED, ("leaking write ucred"));
	bp->b_rcred = crhold(curthread->td_ucred);
	bp->b_wcred = crhold(curthread->td_ucred);
	pbgetbo(bo, bp);
	bp->b_vp = vp;
	bp->b_bcount = bp->b_bufsize = bp->b_runningbufspace = bytecount;
	bp->b_iooffset = dbtob(bp->b_blkno);
	KASSERT(IDX_TO_OFF(m[0]->pindex - bp->b_pages[0]->pindex) ==
	    (blkno0 - bp->b_blkno) * DEV_BSIZE +
	    IDX_TO_OFF(m[0]->pindex) % bsize,
	    ("wrong offsets bsize %d m[0] %ju b_pages[0] %ju "
	    "blkno0 %ju b_blkno %ju", bsize,
	    (uintmax_t)m[0]->pindex, (uintmax_t)bp->b_pages[0]->pindex,
	    (uintmax_t)blkno0, (uintmax_t)bp->b_blkno));

	atomic_add_long(&runningbufspace, bp->b_runningbufspace);
	VM_CNT_INC(v_vnodein);
	VM_CNT_ADD(v_vnodepgsin, bp->b_npages);

	if (iodone != NULL) { /* async */
		bp->b_pgiodone = iodone;
		bp->b_caller1 = arg;
		bp->b_iodone = vnode_pager_generic_getpages_done_async;
		bp->b_flags |= B_ASYNC;
		BUF_KERNPROC(bp);
		bstrategy(bp);
		return (VM_PAGER_OK);
	} else {
		bp->b_iodone = bdone;
		bstrategy(bp);
		bwait(bp, PVM, "vnread");
		error = vnode_pager_generic_getpages_done(bp);
		for (i = 0; i < bp->b_npages; i++)
			bp->b_pages[i] = NULL;
		bp->b_vp = NULL;
		pbrelbo(bp);
		uma_zfree(vnode_pbuf_zone, bp);
		return (error != 0 ? VM_PAGER_ERROR : VM_PAGER_OK);
	}
}

static void
vnode_pager_generic_getpages_done_async(struct buf *bp)
{
	int error;

	error = vnode_pager_generic_getpages_done(bp);
	/* Run the iodone upon the requested range. */
	bp->b_pgiodone(bp->b_caller1, bp->b_pages + bp->b_pgbefore,
	    bp->b_npages - bp->b_pgbefore - bp->b_pgafter, error);
	for (int i = 0; i < bp->b_npages; i++)
		bp->b_pages[i] = NULL;
	bp->b_vp = NULL;
	pbrelbo(bp);
	uma_zfree(vnode_pbuf_zone, bp);
}

static int
vnode_pager_generic_getpages_done(struct buf *bp)
{
	vm_object_t object;
	off_t tfoff, nextoff;
	int i, error;

	error = (bp->b_ioflags & BIO_ERROR) != 0 ? EIO : 0;
	object = bp->b_vp->v_object;

	if (error == 0 && bp->b_bcount != bp->b_npages * PAGE_SIZE) {
		if (!buf_mapped(bp)) {
			bp->b_data = bp->b_kvabase;
			pmap_qenter((vm_offset_t)bp->b_data, bp->b_pages,
			    bp->b_npages);
		}
		bzero(bp->b_data + bp->b_bcount,
		    PAGE_SIZE * bp->b_npages - bp->b_bcount);
	}
	if (buf_mapped(bp)) {
		pmap_qremove((vm_offset_t)bp->b_data, bp->b_npages);
		bp->b_data = unmapped_buf;
	}

	VM_OBJECT_WLOCK(object);
	for (i = 0, tfoff = IDX_TO_OFF(bp->b_pages[0]->pindex);
	    i < bp->b_npages; i++, tfoff = nextoff) {
		vm_page_t mt;

		nextoff = tfoff + PAGE_SIZE;
		mt = bp->b_pages[i];

		if (nextoff <= object->un_pager.vnp.vnp_size) {
			/*
			 * Read filled up entire page.
			 */
			mt->valid = VM_PAGE_BITS_ALL;
			KASSERT(mt->dirty == 0,
			    ("%s: page %p is dirty", __func__, mt));
			KASSERT(!pmap_page_is_mapped(mt),
			    ("%s: page %p is mapped", __func__, mt));
		} else {
			/*
			 * Read did not fill up entire page.
			 *
			 * Currently we do not set the entire page valid,
			 * we just try to clear the piece that we couldn't
			 * read.
			 */
			vm_page_set_valid_range(mt, 0,
			    object->un_pager.vnp.vnp_size - tfoff);
			KASSERT((mt->dirty & vm_page_bits(0,
			    object->un_pager.vnp.vnp_size - tfoff)) == 0,
			    ("%s: page %p is dirty", __func__, mt));
		}

		if (i < bp->b_pgbefore || i >= bp->b_npages - bp->b_pgafter)
			vm_page_readahead_finish(mt);
	}
	VM_OBJECT_WUNLOCK(object);
	if (error != 0)
		printf("%s: I/O read error %d\n", __func__, error);

	return (error);
}

/*
 * EOPNOTSUPP is no longer legal.  For local media VFS's that do not
 * implement their own VOP_PUTPAGES, their VOP_PUTPAGES should call to
 * vnode_pager_generic_putpages() to implement the previous behaviour.
 *
 * All other FS's should use the bypass to get to the local media
 * backing vp's VOP_PUTPAGES.
 */
static void
vnode_pager_putpages(vm_object_t object, vm_page_t *m, int count,
    int flags, int *rtvals)
{
	int rtval;
	struct vnode *vp;
	int bytes = count * PAGE_SIZE;

	/*
	 * Force synchronous operation if we are extremely low on memory
	 * to prevent a low-memory deadlock.  VOP operations often need to
	 * allocate more memory to initiate the I/O ( i.e. do a BMAP
	 * operation ).  The swapper handles the case by limiting the amount
	 * of asynchronous I/O, but that sort of solution doesn't scale well
	 * for the vnode pager without a lot of work.
	 *
	 * Also, the backing vnode's iodone routine may not wake the pageout
	 * daemon up.  This should be probably be addressed XXX.
	 */

	if (vm_page_count_min())
		flags |= VM_PAGER_PUT_SYNC;

	/*
	 * Call device-specific putpages function
	 */
	vp = object->handle;
	VM_OBJECT_WUNLOCK(object);
	rtval = VOP_PUTPAGES(vp, m, bytes, flags, rtvals);
	KASSERT(rtval != EOPNOTSUPP, 
	    ("vnode_pager: stale FS putpages\n"));
	VM_OBJECT_WLOCK(object);
}

static int
vn_off2bidx(vm_ooffset_t offset)
{

	return ((offset & PAGE_MASK) / DEV_BSIZE);
}

static bool
vn_dirty_blk(vm_page_t m, vm_ooffset_t offset)
{

	KASSERT(IDX_TO_OFF(m->pindex) <= offset &&
	    offset < IDX_TO_OFF(m->pindex + 1),
	    ("page %p pidx %ju offset %ju", m, (uintmax_t)m->pindex,
	    (uintmax_t)offset));
	return ((m->dirty & ((vm_page_bits_t)1 << vn_off2bidx(offset))) != 0);
}

/*
 * This is now called from local media FS's to operate against their
 * own vnodes if they fail to implement VOP_PUTPAGES.
 *
 * This is typically called indirectly via the pageout daemon and
 * clustering has already typically occurred, so in general we ask the
 * underlying filesystem to write the data out asynchronously rather
 * then delayed.
 */
int
vnode_pager_generic_putpages(struct vnode *vp, vm_page_t *ma, int bytecount,
    int flags, int *rtvals)
{
	vm_object_t object;
	vm_page_t m;
	vm_ooffset_t maxblksz, next_offset, poffset, prev_offset;
	struct uio auio;
	struct iovec aiov;
	off_t prev_resid, wrsz;
	int count, error, i, maxsize, ncount, pgoff, ppscheck;
	bool in_hole;
	static struct timeval lastfail;
	static int curfail;

	object = vp->v_object;
	count = bytecount / PAGE_SIZE;

	for (i = 0; i < count; i++)
		rtvals[i] = VM_PAGER_ERROR;

	if ((int64_t)ma[0]->pindex < 0) {
		printf("vnode_pager_generic_putpages: "
		    "attempt to write meta-data 0x%jx(%lx)\n",
		    (uintmax_t)ma[0]->pindex, (u_long)ma[0]->dirty);
		rtvals[0] = VM_PAGER_BAD;
		return (VM_PAGER_BAD);
	}

	maxsize = count * PAGE_SIZE;
	ncount = count;

	poffset = IDX_TO_OFF(ma[0]->pindex);

	/*
	 * If the page-aligned write is larger then the actual file we
	 * have to invalidate pages occurring beyond the file EOF.  However,
	 * there is an edge case where a file may not be page-aligned where
	 * the last page is partially invalid.  In this case the filesystem
	 * may not properly clear the dirty bits for the entire page (which
	 * could be VM_PAGE_BITS_ALL due to the page having been mmap()d).
	 * With the page locked we are free to fix-up the dirty bits here.
	 *
	 * We do not under any circumstances truncate the valid bits, as
	 * this will screw up bogus page replacement.
	 */
	VM_OBJECT_RLOCK(object);
	if (maxsize + poffset > object->un_pager.vnp.vnp_size) {
		if (!VM_OBJECT_TRYUPGRADE(object)) {
			VM_OBJECT_RUNLOCK(object);
			VM_OBJECT_WLOCK(object);
			if (maxsize + poffset <= object->un_pager.vnp.vnp_size)
				goto downgrade;
		}
		if (object->un_pager.vnp.vnp_size > poffset) {
			maxsize = object->un_pager.vnp.vnp_size - poffset;
			ncount = btoc(maxsize);
			if ((pgoff = (int)maxsize & PAGE_MASK) != 0) {
				pgoff = roundup2(pgoff, DEV_BSIZE);

				/*
				 * If the object is locked and the following
				 * conditions hold, then the page's dirty
				 * field cannot be concurrently changed by a
				 * pmap operation.
				 */
				m = ma[ncount - 1];
				vm_page_assert_sbusied(m);
				KASSERT(!pmap_page_is_write_mapped(m),
		("vnode_pager_generic_putpages: page %p is not read-only", m));
				MPASS(m->dirty != 0);
				vm_page_clear_dirty(m, pgoff, PAGE_SIZE -
				    pgoff);
			}
		} else {
			maxsize = 0;
			ncount = 0;
		}
		for (i = ncount; i < count; i++)
			rtvals[i] = VM_PAGER_BAD;
downgrade:
		VM_OBJECT_LOCK_DOWNGRADE(object);
	}

	auio.uio_iov = &aiov;
	auio.uio_segflg = UIO_NOCOPY;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = NULL;
	maxblksz = roundup2(poffset + maxsize, DEV_BSIZE);

	for (prev_offset = poffset; prev_offset < maxblksz;) {
		/* Skip clean blocks. */
		for (in_hole = true; in_hole && prev_offset < maxblksz;) {
			m = ma[OFF_TO_IDX(prev_offset - poffset)];
			for (i = vn_off2bidx(prev_offset);
			    i < sizeof(vm_page_bits_t) * NBBY &&
			    prev_offset < maxblksz; i++) {
				if (vn_dirty_blk(m, prev_offset)) {
					in_hole = false;
					break;
				}
				prev_offset += DEV_BSIZE;
			}
		}
		if (in_hole)
			goto write_done;

		/* Find longest run of dirty blocks. */
		for (next_offset = prev_offset; next_offset < maxblksz;) {
			m = ma[OFF_TO_IDX(next_offset - poffset)];
			for (i = vn_off2bidx(next_offset);
			    i < sizeof(vm_page_bits_t) * NBBY &&
			    next_offset < maxblksz; i++) {
				if (!vn_dirty_blk(m, next_offset))
					goto start_write;
				next_offset += DEV_BSIZE;
			}
		}
start_write:
		if (next_offset > poffset + maxsize)
			next_offset = poffset + maxsize;

		/*
		 * Getting here requires finding a dirty block in the
		 * 'skip clean blocks' loop.
		 */
		MPASS(prev_offset < next_offset);

		VM_OBJECT_RUNLOCK(object);
		aiov.iov_base = NULL;
		auio.uio_iovcnt = 1;
		auio.uio_offset = prev_offset;
		prev_resid = auio.uio_resid = aiov.iov_len = next_offset -
		    prev_offset;
		error = VOP_WRITE(vp, &auio,
		    vnode_pager_putpages_ioflags(flags), curthread->td_ucred);

		wrsz = prev_resid - auio.uio_resid;
		if (wrsz == 0) {
			if (ppsratecheck(&lastfail, &curfail, 1) != 0) {
				vn_printf(vp, "vnode_pager_putpages: "
				    "zero-length write at %ju resid %zd\n",
				    auio.uio_offset, auio.uio_resid);
			}
			VM_OBJECT_RLOCK(object);
			break;
		}

		/* Adjust the starting offset for next iteration. */
		prev_offset += wrsz;
		MPASS(auio.uio_offset == prev_offset);

		ppscheck = 0;
		if (error != 0 && (ppscheck = ppsratecheck(&lastfail,
		    &curfail, 1)) != 0)
			vn_printf(vp, "vnode_pager_putpages: I/O error %d\n",
			    error);
		if (auio.uio_resid != 0 && (ppscheck != 0 ||
		    ppsratecheck(&lastfail, &curfail, 1) != 0))
			vn_printf(vp, "vnode_pager_putpages: residual I/O %zd "
			    "at %ju\n", auio.uio_resid,
			    (uintmax_t)ma[0]->pindex);
		VM_OBJECT_RLOCK(object);
		if (error != 0 || auio.uio_resid != 0)
			break;
	}
write_done:
	/* Mark completely processed pages. */
	for (i = 0; i < OFF_TO_IDX(prev_offset - poffset); i++)
		rtvals[i] = VM_PAGER_OK;
	/* Mark partial EOF page. */
	if (prev_offset == poffset + maxsize && (prev_offset & PAGE_MASK) != 0)
		rtvals[i++] = VM_PAGER_OK;
	/* Unwritten pages in range, free bonus if the page is clean. */
	for (; i < ncount; i++)
		rtvals[i] = ma[i]->dirty == 0 ? VM_PAGER_OK : VM_PAGER_ERROR;
	VM_OBJECT_RUNLOCK(object);
	VM_CNT_ADD(v_vnodepgsout, i);
	VM_CNT_INC(v_vnodeout);
	return (rtvals[0]);
}

int
vnode_pager_putpages_ioflags(int pager_flags)
{
	int ioflags;

	/*
	 * Pageouts are already clustered, use IO_ASYNC to force a
	 * bawrite() rather then a bdwrite() to prevent paging I/O
	 * from saturating the buffer cache.  Dummy-up the sequential
	 * heuristic to cause large ranges to cluster.  If neither
	 * IO_SYNC or IO_ASYNC is set, the system decides how to
	 * cluster.
	 */
	ioflags = IO_VMIO;
	if ((pager_flags & (VM_PAGER_PUT_SYNC | VM_PAGER_PUT_INVAL)) != 0)
		ioflags |= IO_SYNC;
	else if ((pager_flags & VM_PAGER_CLUSTER_OK) == 0)
		ioflags |= IO_ASYNC;
	ioflags |= (pager_flags & VM_PAGER_PUT_INVAL) != 0 ? IO_INVAL: 0;
	ioflags |= (pager_flags & VM_PAGER_PUT_NOREUSE) != 0 ? IO_NOREUSE : 0;
	ioflags |= IO_SEQMAX << IO_SEQSHIFT;
	return (ioflags);
}

/*
 * vnode_pager_undirty_pages().
 *
 * A helper to mark pages as clean after pageout that was possibly
 * done with a short write.  The lpos argument specifies the page run
 * length in bytes, and the written argument specifies how many bytes
 * were actually written.  eof is the offset past the last valid byte
 * in the vnode using the absolute file position of the first byte in
 * the run as the base from which it is computed.
 */
void
vnode_pager_undirty_pages(vm_page_t *ma, int *rtvals, int written, off_t eof,
    int lpos)
{
	vm_object_t obj;
	int i, pos, pos_devb;

	if (written == 0 && eof >= lpos)
		return;
	obj = ma[0]->object;
	VM_OBJECT_WLOCK(obj);
	for (i = 0, pos = 0; pos < written; i++, pos += PAGE_SIZE) {
		if (pos < trunc_page(written)) {
			rtvals[i] = VM_PAGER_OK;
			vm_page_undirty(ma[i]);
		} else {
			/* Partially written page. */
			rtvals[i] = VM_PAGER_AGAIN;
			vm_page_clear_dirty(ma[i], 0, written & PAGE_MASK);
		}
	}
	if (eof >= lpos) /* avoid truncation */
		goto done;
	for (pos = eof, i = OFF_TO_IDX(trunc_page(pos)); pos < lpos; i++) {
		if (pos != trunc_page(pos)) {
			/*
			 * The page contains the last valid byte in
			 * the vnode, mark the rest of the page as
			 * clean, potentially making the whole page
			 * clean.
			 */
			pos_devb = roundup2(pos & PAGE_MASK, DEV_BSIZE);
			vm_page_clear_dirty(ma[i], pos_devb, PAGE_SIZE -
			    pos_devb);

			/*
			 * If the page was cleaned, report the pageout
			 * on it as successful.  msync() no longer
			 * needs to write out the page, endlessly
			 * creating write requests and dirty buffers.
			 */
			if (ma[i]->dirty == 0)
				rtvals[i] = VM_PAGER_OK;

			pos = round_page(pos);
		} else {
			/* vm_pageout_flush() clears dirty */
			rtvals[i] = VM_PAGER_BAD;
			pos += PAGE_SIZE;
		}
	}
done:
	VM_OBJECT_WUNLOCK(obj);
}

void
vnode_pager_update_writecount(vm_object_t object, vm_offset_t start,
    vm_offset_t end)
{
	struct vnode *vp;
	vm_ooffset_t old_wm;

	VM_OBJECT_WLOCK(object);
	if (object->type != OBJT_VNODE) {
		VM_OBJECT_WUNLOCK(object);
		return;
	}
	old_wm = object->un_pager.vnp.writemappings;
	object->un_pager.vnp.writemappings += (vm_ooffset_t)end - start;
	vp = object->handle;
	if (old_wm == 0 && object->un_pager.vnp.writemappings != 0) {
		ASSERT_VOP_ELOCKED(vp, "v_writecount inc");
		VOP_ADD_WRITECOUNT(vp, 1);
		CTR3(KTR_VFS, "%s: vp %p v_writecount increased to %d",
		    __func__, vp, vp->v_writecount);
	} else if (old_wm != 0 && object->un_pager.vnp.writemappings == 0) {
		ASSERT_VOP_ELOCKED(vp, "v_writecount dec");
		VOP_ADD_WRITECOUNT(vp, -1);
		CTR3(KTR_VFS, "%s: vp %p v_writecount decreased to %d",
		    __func__, vp, vp->v_writecount);
	}
	VM_OBJECT_WUNLOCK(object);
}

void
vnode_pager_release_writecount(vm_object_t object, vm_offset_t start,
    vm_offset_t end)
{
	struct vnode *vp;
	struct mount *mp;
	vm_offset_t inc;

	VM_OBJECT_WLOCK(object);

	/*
	 * First, recheck the object type to account for the race when
	 * the vnode is reclaimed.
	 */
	if (object->type != OBJT_VNODE) {
		VM_OBJECT_WUNLOCK(object);
		return;
	}

	/*
	 * Optimize for the case when writemappings is not going to
	 * zero.
	 */
	inc = end - start;
	if (object->un_pager.vnp.writemappings != inc) {
		object->un_pager.vnp.writemappings -= inc;
		VM_OBJECT_WUNLOCK(object);
		return;
	}

	vp = object->handle;
	vhold(vp);
	VM_OBJECT_WUNLOCK(object);
	mp = NULL;
	vn_start_write(vp, &mp, V_WAIT);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	/*
	 * Decrement the object's writemappings, by swapping the start
	 * and end arguments for vnode_pager_update_writecount().  If
	 * there was not a race with vnode reclaimation, then the
	 * vnode's v_writecount is decremented.
	 */
	vnode_pager_update_writecount(object, end, start);
	VOP_UNLOCK(vp, 0);
	vdrop(vp);
	if (mp != NULL)
		vn_finished_write(mp);
}
