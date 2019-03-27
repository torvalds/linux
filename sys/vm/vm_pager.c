/*-
 * SPDX-License-Identifier: (BSD-3-Clause AND MIT-CMU)
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	from: @(#)vm_pager.c	8.6 (Berkeley) 1/12/94
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 *	Paging space routine stubs.  Emulates a matchmaker-like interface
 *	for builtin pagers.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_param.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/ucred.h>
#include <sys/malloc.h>
#include <sys/rwlock.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/uma.h>

uma_zone_t pbuf_zone;
static int	pbuf_init(void *, int, int);
static int	pbuf_ctor(void *, int, void *, int);
static void	pbuf_dtor(void *, int, void *);

static int dead_pager_getpages(vm_object_t, vm_page_t *, int, int *, int *);
static vm_object_t dead_pager_alloc(void *, vm_ooffset_t, vm_prot_t,
    vm_ooffset_t, struct ucred *);
static void dead_pager_putpages(vm_object_t, vm_page_t *, int, int, int *);
static boolean_t dead_pager_haspage(vm_object_t, vm_pindex_t, int *, int *);
static void dead_pager_dealloc(vm_object_t);

static int
dead_pager_getpages(vm_object_t obj, vm_page_t *ma, int count, int *rbehind,
    int *rahead)
{

	return (VM_PAGER_FAIL);
}

static vm_object_t
dead_pager_alloc(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t off, struct ucred *cred)
{

	return (NULL);
}

static void
dead_pager_putpages(vm_object_t object, vm_page_t *m, int count,
    int flags, int *rtvals)
{
	int i;

	for (i = 0; i < count; i++)
		rtvals[i] = VM_PAGER_AGAIN;
}

static int
dead_pager_haspage(vm_object_t object, vm_pindex_t pindex, int *prev, int *next)
{

	if (prev != NULL)
		*prev = 0;
	if (next != NULL)
		*next = 0;
	return (FALSE);
}

static void
dead_pager_dealloc(vm_object_t object)
{

}

static struct pagerops deadpagerops = {
	.pgo_alloc = 	dead_pager_alloc,
	.pgo_dealloc =	dead_pager_dealloc,
	.pgo_getpages =	dead_pager_getpages,
	.pgo_putpages =	dead_pager_putpages,
	.pgo_haspage =	dead_pager_haspage,
};

struct pagerops *pagertab[] = {
	&defaultpagerops,	/* OBJT_DEFAULT */
	&swappagerops,		/* OBJT_SWAP */
	&vnodepagerops,		/* OBJT_VNODE */
	&devicepagerops,	/* OBJT_DEVICE */
	&physpagerops,		/* OBJT_PHYS */
	&deadpagerops,		/* OBJT_DEAD */
	&sgpagerops,		/* OBJT_SG */
	&mgtdevicepagerops,	/* OBJT_MGTDEVICE */
};

void
vm_pager_init(void)
{
	struct pagerops **pgops;

	/*
	 * Initialize known pagers
	 */
	for (pgops = pagertab; pgops < &pagertab[nitems(pagertab)]; pgops++)
		if ((*pgops)->pgo_init != NULL)
			(*(*pgops)->pgo_init)();
}

static int nswbuf_max;

void
vm_pager_bufferinit(void)
{

	/* Main zone for paging bufs. */
	pbuf_zone = uma_zcreate("pbuf", sizeof(struct buf),
	    pbuf_ctor, pbuf_dtor, pbuf_init, NULL, UMA_ALIGN_CACHE,
	    UMA_ZONE_VM | UMA_ZONE_NOFREE);
	/* Few systems may still use this zone directly, so it needs a limit. */
	nswbuf_max += uma_zone_set_max(pbuf_zone, NSWBUF_MIN);
}

uma_zone_t
pbuf_zsecond_create(char *name, int max)
{
	uma_zone_t zone;

	zone = uma_zsecond_create(name, pbuf_ctor, pbuf_dtor, NULL, NULL,
	    pbuf_zone);
	/*
	 * uma_prealloc() rounds up to items per slab. If we would prealloc
	 * immediately on every pbuf_zsecond_create(), we may accumulate too
	 * much of difference between hard limit and prealloced items, which
	 * means wasted memory.
	 */
	if (nswbuf_max > 0)
		nswbuf_max += uma_zone_set_max(zone, max);
	else
		uma_prealloc(pbuf_zone, uma_zone_set_max(zone, max));

	return (zone);
}

static void
pbuf_prealloc(void *arg __unused)
{

	uma_prealloc(pbuf_zone, nswbuf_max);
	nswbuf_max = -1;
}

SYSINIT(pbuf, SI_SUB_KTHREAD_BUF, SI_ORDER_ANY, pbuf_prealloc, NULL);

/*
 * Allocate an instance of a pager of the given type.
 * Size, protection and offset parameters are passed in for pagers that
 * need to perform page-level validation (e.g. the device pager).
 */
vm_object_t
vm_pager_allocate(objtype_t type, void *handle, vm_ooffset_t size,
    vm_prot_t prot, vm_ooffset_t off, struct ucred *cred)
{
	vm_object_t ret;
	struct pagerops *ops;

	ops = pagertab[type];
	if (ops)
		ret = (*ops->pgo_alloc)(handle, size, prot, off, cred);
	else
		ret = NULL;
	return (ret);
}

/*
 *	The object must be locked.
 */
void
vm_pager_deallocate(vm_object_t object)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	(*pagertab[object->type]->pgo_dealloc) (object);
}

static void
vm_pager_assert_in(vm_object_t object, vm_page_t *m, int count)
{
#ifdef INVARIANTS

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(count > 0, ("%s: 0 count", __func__));
	/*
	 * All pages must be busied, not mapped, not fully valid,
	 * not dirty and belong to the proper object.
	 */
	for (int i = 0 ; i < count; i++) {
		if (m[i] == bogus_page)
			continue;
		vm_page_assert_xbusied(m[i]);
		KASSERT(!pmap_page_is_mapped(m[i]),
		    ("%s: page %p is mapped", __func__, m[i]));
		KASSERT(m[i]->valid != VM_PAGE_BITS_ALL,
		    ("%s: request for a valid page %p", __func__, m[i]));
		KASSERT(m[i]->dirty == 0,
		    ("%s: page %p is dirty", __func__, m[i]));
		KASSERT(m[i]->object == object,
		    ("%s: wrong object %p/%p", __func__, object, m[i]->object));
	}
#endif
}

/*
 * Page in the pages for the object using its associated pager.
 * The requested page must be fully valid on successful return.
 */
int
vm_pager_get_pages(vm_object_t object, vm_page_t *m, int count, int *rbehind,
    int *rahead)
{
#ifdef INVARIANTS
	vm_pindex_t pindex = m[0]->pindex;
#endif
	int r;

	vm_pager_assert_in(object, m, count);

	r = (*pagertab[object->type]->pgo_getpages)(object, m, count, rbehind,
	    rahead);
	if (r != VM_PAGER_OK)
		return (r);

	for (int i = 0; i < count; i++) {
		/*
		 * If pager has replaced a page, assert that it had
		 * updated the array.
		 */
		KASSERT(m[i] == vm_page_lookup(object, pindex++),
		    ("%s: mismatch page %p pindex %ju", __func__,
		    m[i], (uintmax_t )pindex - 1));
		/*
		 * Zero out partially filled data.
		 */
		if (m[i]->valid != VM_PAGE_BITS_ALL)
			vm_page_zero_invalid(m[i], TRUE);
	}
	return (VM_PAGER_OK);
}

int
vm_pager_get_pages_async(vm_object_t object, vm_page_t *m, int count,
    int *rbehind, int *rahead, pgo_getpages_iodone_t iodone, void *arg)
{

	vm_pager_assert_in(object, m, count);

	return ((*pagertab[object->type]->pgo_getpages_async)(object, m,
	    count, rbehind, rahead, iodone, arg));
}

/*
 * vm_pager_put_pages() - inline, see vm/vm_pager.h
 * vm_pager_has_page() - inline, see vm/vm_pager.h
 */

/*
 * Search the specified pager object list for an object with the
 * specified handle.  If an object with the specified handle is found,
 * increase its reference count and return it.  Otherwise, return NULL.
 *
 * The pager object list must be locked.
 */
vm_object_t
vm_pager_object_lookup(struct pagerlst *pg_list, void *handle)
{
	vm_object_t object;

	TAILQ_FOREACH(object, pg_list, pager_object_list) {
		if (object->handle == handle) {
			VM_OBJECT_WLOCK(object);
			if ((object->flags & OBJ_DEAD) == 0) {
				vm_object_reference_locked(object);
				VM_OBJECT_WUNLOCK(object);
				break;
			}
			VM_OBJECT_WUNLOCK(object);
		}
	}
	return (object);
}

static int
pbuf_ctor(void *mem, int size, void *arg, int flags)
{
	struct buf *bp = mem;

	bp->b_vp = NULL;
	bp->b_bufobj = NULL;

	/* copied from initpbuf() */
	bp->b_rcred = NOCRED;
	bp->b_wcred = NOCRED;
	bp->b_qindex = 0;       /* On no queue (QUEUE_NONE) */
	bp->b_data = bp->b_kvabase;
	bp->b_xflags = 0;
	bp->b_flags = 0;
	bp->b_ioflags = 0;
	bp->b_iodone = NULL;
	bp->b_error = 0;
	BUF_LOCK(bp, LK_EXCLUSIVE, NULL);

	return (0);
}

static void
pbuf_dtor(void *mem, int size, void *arg)
{
	struct buf *bp = mem;

	if (bp->b_rcred != NOCRED) {
		crfree(bp->b_rcred);
		bp->b_rcred = NOCRED;
	}
	if (bp->b_wcred != NOCRED) {
		crfree(bp->b_wcred);
		bp->b_wcred = NOCRED;
	}

	BUF_UNLOCK(bp);
}

static int
pbuf_init(void *mem, int size, int flags)
{
	struct buf *bp = mem;

	bp->b_kvabase = (void *)kva_alloc(MAXPHYS);
	if (bp->b_kvabase == NULL)
		return (ENOMEM);
	bp->b_kvasize = MAXPHYS;
	BUF_LOCKINIT(bp);
	LIST_INIT(&bp->b_dep);
	bp->b_rcred = bp->b_wcred = NOCRED;
	bp->b_xflags = 0;

	return (0);
}

/*
 * Associate a p-buffer with a vnode.
 *
 * Also sets B_PAGING flag to indicate that vnode is not fully associated
 * with the buffer.  i.e. the bp has not been linked into the vnode or
 * ref-counted.
 */
void
pbgetvp(struct vnode *vp, struct buf *bp)
{

	KASSERT(bp->b_vp == NULL, ("pbgetvp: not free"));
	KASSERT(bp->b_bufobj == NULL, ("pbgetvp: not free (bufobj)"));

	bp->b_vp = vp;
	bp->b_flags |= B_PAGING;
	bp->b_bufobj = &vp->v_bufobj;
}

/*
 * Associate a p-buffer with a vnode.
 *
 * Also sets B_PAGING flag to indicate that vnode is not fully associated
 * with the buffer.  i.e. the bp has not been linked into the vnode or
 * ref-counted.
 */
void
pbgetbo(struct bufobj *bo, struct buf *bp)
{

	KASSERT(bp->b_vp == NULL, ("pbgetbo: not free (vnode)"));
	KASSERT(bp->b_bufobj == NULL, ("pbgetbo: not free (bufobj)"));

	bp->b_flags |= B_PAGING;
	bp->b_bufobj = bo;
}

/*
 * Disassociate a p-buffer from a vnode.
 */
void
pbrelvp(struct buf *bp)
{

	KASSERT(bp->b_vp != NULL, ("pbrelvp: NULL"));
	KASSERT(bp->b_bufobj != NULL, ("pbrelvp: NULL bufobj"));
	KASSERT((bp->b_xflags & (BX_VNDIRTY | BX_VNCLEAN)) == 0,
	    ("pbrelvp: pager buf on vnode list."));

	bp->b_vp = NULL;
	bp->b_bufobj = NULL;
	bp->b_flags &= ~B_PAGING;
}

/*
 * Disassociate a p-buffer from a bufobj.
 */
void
pbrelbo(struct buf *bp)
{

	KASSERT(bp->b_vp == NULL, ("pbrelbo: vnode"));
	KASSERT(bp->b_bufobj != NULL, ("pbrelbo: NULL bufobj"));
	KASSERT((bp->b_xflags & (BX_VNDIRTY | BX_VNCLEAN)) == 0,
	    ("pbrelbo: pager buf on vnode list."));

	bp->b_bufobj = NULL;
	bp->b_flags &= ~B_PAGING;
}
