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
 *	from: @(#)vm_object.c	8.5 (Berkeley) 3/22/94
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
 *	Virtual memory object module.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpuset.h>
#include <sys/lock.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/pctrie.h>
#include <sys/sysctl.h>
#include <sys/mutex.h>
#include <sys/proc.h>		/* for curproc, pageproc */
#include <sys/socket.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/vmmeter.h>
#include <sys/sx.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/vm_phys.h>
#include <vm/vm_pagequeue.h>
#include <vm/swap_pager.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/vm_radix.h>
#include <vm/vm_reserv.h>
#include <vm/uma.h>

static int old_msync;
SYSCTL_INT(_vm, OID_AUTO, old_msync, CTLFLAG_RW, &old_msync, 0,
    "Use old (insecure) msync behavior");

static int	vm_object_page_collect_flush(vm_object_t object, vm_page_t p,
		    int pagerflags, int flags, boolean_t *clearobjflags,
		    boolean_t *eio);
static boolean_t vm_object_page_remove_write(vm_page_t p, int flags,
		    boolean_t *clearobjflags);
static void	vm_object_qcollapse(vm_object_t object);
static void	vm_object_vndeallocate(vm_object_t object);

/*
 *	Virtual memory objects maintain the actual data
 *	associated with allocated virtual memory.  A given
 *	page of memory exists within exactly one object.
 *
 *	An object is only deallocated when all "references"
 *	are given up.  Only one "reference" to a given
 *	region of an object should be writeable.
 *
 *	Associated with each object is a list of all resident
 *	memory pages belonging to that object; this list is
 *	maintained by the "vm_page" module, and locked by the object's
 *	lock.
 *
 *	Each object also records a "pager" routine which is
 *	used to retrieve (and store) pages to the proper backing
 *	storage.  In addition, objects may be backed by other
 *	objects from which they were virtual-copied.
 *
 *	The only items within the object structure which are
 *	modified after time of creation are:
 *		reference count		locked by object's lock
 *		pager routine		locked by object's lock
 *
 */

struct object_q vm_object_list;
struct mtx vm_object_list_mtx;	/* lock for object list and count */

struct vm_object kernel_object_store;

static SYSCTL_NODE(_vm_stats, OID_AUTO, object, CTLFLAG_RD, 0,
    "VM object stats");

static counter_u64_t object_collapses = EARLY_COUNTER;
SYSCTL_COUNTER_U64(_vm_stats_object, OID_AUTO, collapses, CTLFLAG_RD,
    &object_collapses,
    "VM object collapses");

static counter_u64_t object_bypasses = EARLY_COUNTER;
SYSCTL_COUNTER_U64(_vm_stats_object, OID_AUTO, bypasses, CTLFLAG_RD,
    &object_bypasses,
    "VM object bypasses");

static void
counter_startup(void)
{

	object_collapses = counter_u64_alloc(M_WAITOK);
	object_bypasses = counter_u64_alloc(M_WAITOK);
}
SYSINIT(object_counters, SI_SUB_CPU, SI_ORDER_ANY, counter_startup, NULL);

static uma_zone_t obj_zone;

static int vm_object_zinit(void *mem, int size, int flags);

#ifdef INVARIANTS
static void vm_object_zdtor(void *mem, int size, void *arg);

static void
vm_object_zdtor(void *mem, int size, void *arg)
{
	vm_object_t object;

	object = (vm_object_t)mem;
	KASSERT(object->ref_count == 0,
	    ("object %p ref_count = %d", object, object->ref_count));
	KASSERT(TAILQ_EMPTY(&object->memq),
	    ("object %p has resident pages in its memq", object));
	KASSERT(vm_radix_is_empty(&object->rtree),
	    ("object %p has resident pages in its trie", object));
#if VM_NRESERVLEVEL > 0
	KASSERT(LIST_EMPTY(&object->rvq),
	    ("object %p has reservations",
	    object));
#endif
	KASSERT(object->paging_in_progress == 0,
	    ("object %p paging_in_progress = %d",
	    object, object->paging_in_progress));
	KASSERT(object->resident_page_count == 0,
	    ("object %p resident_page_count = %d",
	    object, object->resident_page_count));
	KASSERT(object->shadow_count == 0,
	    ("object %p shadow_count = %d",
	    object, object->shadow_count));
	KASSERT(object->type == OBJT_DEAD,
	    ("object %p has non-dead type %d",
	    object, object->type));
}
#endif

static int
vm_object_zinit(void *mem, int size, int flags)
{
	vm_object_t object;

	object = (vm_object_t)mem;
	rw_init_flags(&object->lock, "vm object", RW_DUPOK | RW_NEW);

	/* These are true for any object that has been freed */
	object->type = OBJT_DEAD;
	object->ref_count = 0;
	vm_radix_init(&object->rtree);
	object->paging_in_progress = 0;
	object->resident_page_count = 0;
	object->shadow_count = 0;
	object->flags = OBJ_DEAD;

	mtx_lock(&vm_object_list_mtx);
	TAILQ_INSERT_TAIL(&vm_object_list, object, object_list);
	mtx_unlock(&vm_object_list_mtx);
	return (0);
}

static void
_vm_object_allocate(objtype_t type, vm_pindex_t size, vm_object_t object)
{

	TAILQ_INIT(&object->memq);
	LIST_INIT(&object->shadow_head);

	object->type = type;
	if (type == OBJT_SWAP)
		pctrie_init(&object->un_pager.swp.swp_blks);

	/*
	 * Ensure that swap_pager_swapoff() iteration over object_list
	 * sees up to date type and pctrie head if it observed
	 * non-dead object.
	 */
	atomic_thread_fence_rel();

	switch (type) {
	case OBJT_DEAD:
		panic("_vm_object_allocate: can't create OBJT_DEAD");
	case OBJT_DEFAULT:
	case OBJT_SWAP:
		object->flags = OBJ_ONEMAPPING;
		break;
	case OBJT_DEVICE:
	case OBJT_SG:
		object->flags = OBJ_FICTITIOUS | OBJ_UNMANAGED;
		break;
	case OBJT_MGTDEVICE:
		object->flags = OBJ_FICTITIOUS;
		break;
	case OBJT_PHYS:
		object->flags = OBJ_UNMANAGED;
		break;
	case OBJT_VNODE:
		object->flags = 0;
		break;
	default:
		panic("_vm_object_allocate: type %d is undefined", type);
	}
	object->size = size;
	object->domain.dr_policy = NULL;
	object->generation = 1;
	object->ref_count = 1;
	object->memattr = VM_MEMATTR_DEFAULT;
	object->cred = NULL;
	object->charge = 0;
	object->handle = NULL;
	object->backing_object = NULL;
	object->backing_object_offset = (vm_ooffset_t) 0;
#if VM_NRESERVLEVEL > 0
	LIST_INIT(&object->rvq);
#endif
	umtx_shm_object_init(object);
}

/*
 *	vm_object_init:
 *
 *	Initialize the VM objects module.
 */
void
vm_object_init(void)
{
	TAILQ_INIT(&vm_object_list);
	mtx_init(&vm_object_list_mtx, "vm object_list", NULL, MTX_DEF);
	
	rw_init(&kernel_object->lock, "kernel vm object");
	_vm_object_allocate(OBJT_PHYS, atop(VM_MAX_KERNEL_ADDRESS -
	    VM_MIN_KERNEL_ADDRESS), kernel_object);
#if VM_NRESERVLEVEL > 0
	kernel_object->flags |= OBJ_COLORED;
	kernel_object->pg_color = (u_short)atop(VM_MIN_KERNEL_ADDRESS);
#endif

	/*
	 * The lock portion of struct vm_object must be type stable due
	 * to vm_pageout_fallback_object_lock locking a vm object
	 * without holding any references to it.
	 */
	obj_zone = uma_zcreate("VM OBJECT", sizeof (struct vm_object), NULL,
#ifdef INVARIANTS
	    vm_object_zdtor,
#else
	    NULL,
#endif
	    vm_object_zinit, NULL, UMA_ALIGN_PTR, UMA_ZONE_NOFREE);

	vm_radix_zinit();
}

void
vm_object_clear_flag(vm_object_t object, u_short bits)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	object->flags &= ~bits;
}

/*
 *	Sets the default memory attribute for the specified object.  Pages
 *	that are allocated to this object are by default assigned this memory
 *	attribute.
 *
 *	Presently, this function must be called before any pages are allocated
 *	to the object.  In the future, this requirement may be relaxed for
 *	"default" and "swap" objects.
 */
int
vm_object_set_memattr(vm_object_t object, vm_memattr_t memattr)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	switch (object->type) {
	case OBJT_DEFAULT:
	case OBJT_DEVICE:
	case OBJT_MGTDEVICE:
	case OBJT_PHYS:
	case OBJT_SG:
	case OBJT_SWAP:
	case OBJT_VNODE:
		if (!TAILQ_EMPTY(&object->memq))
			return (KERN_FAILURE);
		break;
	case OBJT_DEAD:
		return (KERN_INVALID_ARGUMENT);
	default:
		panic("vm_object_set_memattr: object %p is of undefined type",
		    object);
	}
	object->memattr = memattr;
	return (KERN_SUCCESS);
}

void
vm_object_pip_add(vm_object_t object, short i)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	object->paging_in_progress += i;
}

void
vm_object_pip_subtract(vm_object_t object, short i)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	object->paging_in_progress -= i;
}

void
vm_object_pip_wakeup(vm_object_t object)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	object->paging_in_progress--;
	if ((object->flags & OBJ_PIPWNT) && object->paging_in_progress == 0) {
		vm_object_clear_flag(object, OBJ_PIPWNT);
		wakeup(object);
	}
}

void
vm_object_pip_wakeupn(vm_object_t object, short i)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	if (i)
		object->paging_in_progress -= i;
	if ((object->flags & OBJ_PIPWNT) && object->paging_in_progress == 0) {
		vm_object_clear_flag(object, OBJ_PIPWNT);
		wakeup(object);
	}
}

void
vm_object_pip_wait(vm_object_t object, char *waitid)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	while (object->paging_in_progress) {
		object->flags |= OBJ_PIPWNT;
		VM_OBJECT_SLEEP(object, object, PVM, waitid, 0);
	}
}

/*
 *	vm_object_allocate:
 *
 *	Returns a new object with the given size.
 */
vm_object_t
vm_object_allocate(objtype_t type, vm_pindex_t size)
{
	vm_object_t object;

	object = (vm_object_t)uma_zalloc(obj_zone, M_WAITOK);
	_vm_object_allocate(type, size, object);
	return (object);
}


/*
 *	vm_object_reference:
 *
 *	Gets another reference to the given object.  Note: OBJ_DEAD
 *	objects can be referenced during final cleaning.
 */
void
vm_object_reference(vm_object_t object)
{
	if (object == NULL)
		return;
	VM_OBJECT_WLOCK(object);
	vm_object_reference_locked(object);
	VM_OBJECT_WUNLOCK(object);
}

/*
 *	vm_object_reference_locked:
 *
 *	Gets another reference to the given object.
 *
 *	The object must be locked.
 */
void
vm_object_reference_locked(vm_object_t object)
{
	struct vnode *vp;

	VM_OBJECT_ASSERT_WLOCKED(object);
	object->ref_count++;
	if (object->type == OBJT_VNODE) {
		vp = object->handle;
		vref(vp);
	}
}

/*
 * Handle deallocating an object of type OBJT_VNODE.
 */
static void
vm_object_vndeallocate(vm_object_t object)
{
	struct vnode *vp = (struct vnode *) object->handle;

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(object->type == OBJT_VNODE,
	    ("vm_object_vndeallocate: not a vnode object"));
	KASSERT(vp != NULL, ("vm_object_vndeallocate: missing vp"));
#ifdef INVARIANTS
	if (object->ref_count == 0) {
		vn_printf(vp, "vm_object_vndeallocate ");
		panic("vm_object_vndeallocate: bad object reference count");
	}
#endif

	if (!umtx_shm_vnobj_persistent && object->ref_count == 1)
		umtx_shm_object_terminated(object);

	/*
	 * The test for text of vp vnode does not need a bypass to
	 * reach right VV_TEXT there, since it is obtained from
	 * object->handle.
	 */
	if (object->ref_count > 1 || (vp->v_vflag & VV_TEXT) == 0) {
		object->ref_count--;
		VM_OBJECT_WUNLOCK(object);
		/* vrele may need the vnode lock. */
		vrele(vp);
	} else {
		vhold(vp);
		VM_OBJECT_WUNLOCK(object);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		vdrop(vp);
		VM_OBJECT_WLOCK(object);
		object->ref_count--;
		if (object->type == OBJT_DEAD) {
			VM_OBJECT_WUNLOCK(object);
			VOP_UNLOCK(vp, 0);
		} else {
			if (object->ref_count == 0)
				VOP_UNSET_TEXT(vp);
			VM_OBJECT_WUNLOCK(object);
			vput(vp);
		}
	}
}

/*
 *	vm_object_deallocate:
 *
 *	Release a reference to the specified object,
 *	gained either through a vm_object_allocate
 *	or a vm_object_reference call.  When all references
 *	are gone, storage associated with this object
 *	may be relinquished.
 *
 *	No object may be locked.
 */
void
vm_object_deallocate(vm_object_t object)
{
	vm_object_t temp;
	struct vnode *vp;

	while (object != NULL) {
		VM_OBJECT_WLOCK(object);
		if (object->type == OBJT_VNODE) {
			vm_object_vndeallocate(object);
			return;
		}

		KASSERT(object->ref_count != 0,
			("vm_object_deallocate: object deallocated too many times: %d", object->type));

		/*
		 * If the reference count goes to 0 we start calling
		 * vm_object_terminate() on the object chain.
		 * A ref count of 1 may be a special case depending on the
		 * shadow count being 0 or 1.
		 */
		object->ref_count--;
		if (object->ref_count > 1) {
			VM_OBJECT_WUNLOCK(object);
			return;
		} else if (object->ref_count == 1) {
			if (object->type == OBJT_SWAP &&
			    (object->flags & OBJ_TMPFS) != 0) {
				vp = object->un_pager.swp.swp_tmpfs;
				vhold(vp);
				VM_OBJECT_WUNLOCK(object);
				vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
				VM_OBJECT_WLOCK(object);
				if (object->type == OBJT_DEAD ||
				    object->ref_count != 1) {
					VM_OBJECT_WUNLOCK(object);
					VOP_UNLOCK(vp, 0);
					vdrop(vp);
					return;
				}
				if ((object->flags & OBJ_TMPFS) != 0)
					VOP_UNSET_TEXT(vp);
				VOP_UNLOCK(vp, 0);
				vdrop(vp);
			}
			if (object->shadow_count == 0 &&
			    object->handle == NULL &&
			    (object->type == OBJT_DEFAULT ||
			    (object->type == OBJT_SWAP &&
			    (object->flags & OBJ_TMPFS_NODE) == 0))) {
				vm_object_set_flag(object, OBJ_ONEMAPPING);
			} else if ((object->shadow_count == 1) &&
			    (object->handle == NULL) &&
			    (object->type == OBJT_DEFAULT ||
			     object->type == OBJT_SWAP)) {
				vm_object_t robject;

				robject = LIST_FIRST(&object->shadow_head);
				KASSERT(robject != NULL,
				    ("vm_object_deallocate: ref_count: %d, shadow_count: %d",
					 object->ref_count,
					 object->shadow_count));
				KASSERT((robject->flags & OBJ_TMPFS_NODE) == 0,
				    ("shadowed tmpfs v_object %p", object));
				if (!VM_OBJECT_TRYWLOCK(robject)) {
					/*
					 * Avoid a potential deadlock.
					 */
					object->ref_count++;
					VM_OBJECT_WUNLOCK(object);
					/*
					 * More likely than not the thread
					 * holding robject's lock has lower
					 * priority than the current thread.
					 * Let the lower priority thread run.
					 */
					pause("vmo_de", 1);
					continue;
				}
				/*
				 * Collapse object into its shadow unless its
				 * shadow is dead.  In that case, object will
				 * be deallocated by the thread that is
				 * deallocating its shadow.
				 */
				if ((robject->flags & OBJ_DEAD) == 0 &&
				    (robject->handle == NULL) &&
				    (robject->type == OBJT_DEFAULT ||
				     robject->type == OBJT_SWAP)) {

					robject->ref_count++;
retry:
					if (robject->paging_in_progress) {
						VM_OBJECT_WUNLOCK(object);
						vm_object_pip_wait(robject,
						    "objde1");
						temp = robject->backing_object;
						if (object == temp) {
							VM_OBJECT_WLOCK(object);
							goto retry;
						}
					} else if (object->paging_in_progress) {
						VM_OBJECT_WUNLOCK(robject);
						object->flags |= OBJ_PIPWNT;
						VM_OBJECT_SLEEP(object, object,
						    PDROP | PVM, "objde2", 0);
						VM_OBJECT_WLOCK(robject);
						temp = robject->backing_object;
						if (object == temp) {
							VM_OBJECT_WLOCK(object);
							goto retry;
						}
					} else
						VM_OBJECT_WUNLOCK(object);

					if (robject->ref_count == 1) {
						robject->ref_count--;
						object = robject;
						goto doterm;
					}
					object = robject;
					vm_object_collapse(object);
					VM_OBJECT_WUNLOCK(object);
					continue;
				}
				VM_OBJECT_WUNLOCK(robject);
			}
			VM_OBJECT_WUNLOCK(object);
			return;
		}
doterm:
		umtx_shm_object_terminated(object);
		temp = object->backing_object;
		if (temp != NULL) {
			KASSERT((object->flags & OBJ_TMPFS_NODE) == 0,
			    ("shadowed tmpfs v_object 2 %p", object));
			VM_OBJECT_WLOCK(temp);
			LIST_REMOVE(object, shadow_list);
			temp->shadow_count--;
			VM_OBJECT_WUNLOCK(temp);
			object->backing_object = NULL;
		}
		/*
		 * Don't double-terminate, we could be in a termination
		 * recursion due to the terminate having to sync data
		 * to disk.
		 */
		if ((object->flags & OBJ_DEAD) == 0)
			vm_object_terminate(object);
		else
			VM_OBJECT_WUNLOCK(object);
		object = temp;
	}
}

/*
 *	vm_object_destroy removes the object from the global object list
 *      and frees the space for the object.
 */
void
vm_object_destroy(vm_object_t object)
{

	/*
	 * Release the allocation charge.
	 */
	if (object->cred != NULL) {
		swap_release_by_cred(object->charge, object->cred);
		object->charge = 0;
		crfree(object->cred);
		object->cred = NULL;
	}

	/*
	 * Free the space for the object.
	 */
	uma_zfree(obj_zone, object);
}

/*
 *	vm_object_terminate_pages removes any remaining pageable pages
 *	from the object and resets the object to an empty state.
 */
static void
vm_object_terminate_pages(vm_object_t object)
{
	vm_page_t p, p_next;
	struct mtx *mtx;

	VM_OBJECT_ASSERT_WLOCKED(object);

	mtx = NULL;

	/*
	 * Free any remaining pageable pages.  This also removes them from the
	 * paging queues.  However, don't free wired pages, just remove them
	 * from the object.  Rather than incrementally removing each page from
	 * the object, the page and object are reset to any empty state. 
	 */
	TAILQ_FOREACH_SAFE(p, &object->memq, listq, p_next) {
		vm_page_assert_unbusied(p);
		if ((object->flags & OBJ_UNMANAGED) == 0)
			/*
			 * vm_page_free_prep() only needs the page
			 * lock for managed pages.
			 */
			vm_page_change_lock(p, &mtx);
		p->object = NULL;
		if (p->wire_count != 0)
			continue;
		VM_CNT_INC(v_pfree);
		vm_page_free(p);
	}
	if (mtx != NULL)
		mtx_unlock(mtx);

	/*
	 * If the object contained any pages, then reset it to an empty state.
	 * None of the object's fields, including "resident_page_count", were
	 * modified by the preceding loop.
	 */
	if (object->resident_page_count != 0) {
		vm_radix_reclaim_allnodes(&object->rtree);
		TAILQ_INIT(&object->memq);
		object->resident_page_count = 0;
		if (object->type == OBJT_VNODE)
			vdrop(object->handle);
	}
}

/*
 *	vm_object_terminate actually destroys the specified object, freeing
 *	up all previously used resources.
 *
 *	The object must be locked.
 *	This routine may block.
 */
void
vm_object_terminate(vm_object_t object)
{

	VM_OBJECT_ASSERT_WLOCKED(object);

	/*
	 * Make sure no one uses us.
	 */
	vm_object_set_flag(object, OBJ_DEAD);

	/*
	 * wait for the pageout daemon to be done with the object
	 */
	vm_object_pip_wait(object, "objtrm");

	KASSERT(!object->paging_in_progress,
		("vm_object_terminate: pageout in progress"));

	/*
	 * Clean and free the pages, as appropriate. All references to the
	 * object are gone, so we don't need to lock it.
	 */
	if (object->type == OBJT_VNODE) {
		struct vnode *vp = (struct vnode *)object->handle;

		/*
		 * Clean pages and flush buffers.
		 */
		vm_object_page_clean(object, 0, 0, OBJPC_SYNC);
		VM_OBJECT_WUNLOCK(object);

		vinvalbuf(vp, V_SAVE, 0, 0);

		BO_LOCK(&vp->v_bufobj);
		vp->v_bufobj.bo_flag |= BO_DEAD;
		BO_UNLOCK(&vp->v_bufobj);

		VM_OBJECT_WLOCK(object);
	}

	KASSERT(object->ref_count == 0, 
		("vm_object_terminate: object with references, ref_count=%d",
		object->ref_count));

	if ((object->flags & OBJ_PG_DTOR) == 0)
		vm_object_terminate_pages(object);

#if VM_NRESERVLEVEL > 0
	if (__predict_false(!LIST_EMPTY(&object->rvq)))
		vm_reserv_break_all(object);
#endif

	KASSERT(object->cred == NULL || object->type == OBJT_DEFAULT ||
	    object->type == OBJT_SWAP,
	    ("%s: non-swap obj %p has cred", __func__, object));

	/*
	 * Let the pager know object is dead.
	 */
	vm_pager_deallocate(object);
	VM_OBJECT_WUNLOCK(object);

	vm_object_destroy(object);
}

/*
 * Make the page read-only so that we can clear the object flags.  However, if
 * this is a nosync mmap then the object is likely to stay dirty so do not
 * mess with the page and do not clear the object flags.  Returns TRUE if the
 * page should be flushed, and FALSE otherwise.
 */
static boolean_t
vm_object_page_remove_write(vm_page_t p, int flags, boolean_t *clearobjflags)
{

	/*
	 * If we have been asked to skip nosync pages and this is a
	 * nosync page, skip it.  Note that the object flags were not
	 * cleared in this case so we do not have to set them.
	 */
	if ((flags & OBJPC_NOSYNC) != 0 && (p->oflags & VPO_NOSYNC) != 0) {
		*clearobjflags = FALSE;
		return (FALSE);
	} else {
		pmap_remove_write(p);
		return (p->dirty != 0);
	}
}

/*
 *	vm_object_page_clean
 *
 *	Clean all dirty pages in the specified range of object.  Leaves page 
 * 	on whatever queue it is currently on.   If NOSYNC is set then do not
 *	write out pages with VPO_NOSYNC set (originally comes from MAP_NOSYNC),
 *	leaving the object dirty.
 *
 *	When stuffing pages asynchronously, allow clustering.  XXX we need a
 *	synchronous clustering mode implementation.
 *
 *	Odd semantics: if start == end, we clean everything.
 *
 *	The object must be locked.
 *
 *	Returns FALSE if some page from the range was not written, as
 *	reported by the pager, and TRUE otherwise.
 */
boolean_t
vm_object_page_clean(vm_object_t object, vm_ooffset_t start, vm_ooffset_t end,
    int flags)
{
	vm_page_t np, p;
	vm_pindex_t pi, tend, tstart;
	int curgeneration, n, pagerflags;
	boolean_t clearobjflags, eio, res;

	VM_OBJECT_ASSERT_WLOCKED(object);

	/*
	 * The OBJ_MIGHTBEDIRTY flag is only set for OBJT_VNODE
	 * objects.  The check below prevents the function from
	 * operating on non-vnode objects.
	 */
	if ((object->flags & OBJ_MIGHTBEDIRTY) == 0 ||
	    object->resident_page_count == 0)
		return (TRUE);

	pagerflags = (flags & (OBJPC_SYNC | OBJPC_INVAL)) != 0 ?
	    VM_PAGER_PUT_SYNC : VM_PAGER_CLUSTER_OK;
	pagerflags |= (flags & OBJPC_INVAL) != 0 ? VM_PAGER_PUT_INVAL : 0;

	tstart = OFF_TO_IDX(start);
	tend = (end == 0) ? object->size : OFF_TO_IDX(end + PAGE_MASK);
	clearobjflags = tstart == 0 && tend >= object->size;
	res = TRUE;

rescan:
	curgeneration = object->generation;

	for (p = vm_page_find_least(object, tstart); p != NULL; p = np) {
		pi = p->pindex;
		if (pi >= tend)
			break;
		np = TAILQ_NEXT(p, listq);
		if (p->valid == 0)
			continue;
		if (vm_page_sleep_if_busy(p, "vpcwai")) {
			if (object->generation != curgeneration) {
				if ((flags & OBJPC_SYNC) != 0)
					goto rescan;
				else
					clearobjflags = FALSE;
			}
			np = vm_page_find_least(object, pi);
			continue;
		}
		if (!vm_object_page_remove_write(p, flags, &clearobjflags))
			continue;

		n = vm_object_page_collect_flush(object, p, pagerflags,
		    flags, &clearobjflags, &eio);
		if (eio) {
			res = FALSE;
			clearobjflags = FALSE;
		}
		if (object->generation != curgeneration) {
			if ((flags & OBJPC_SYNC) != 0)
				goto rescan;
			else
				clearobjflags = FALSE;
		}

		/*
		 * If the VOP_PUTPAGES() did a truncated write, so
		 * that even the first page of the run is not fully
		 * written, vm_pageout_flush() returns 0 as the run
		 * length.  Since the condition that caused truncated
		 * write may be permanent, e.g. exhausted free space,
		 * accepting n == 0 would cause an infinite loop.
		 *
		 * Forwarding the iterator leaves the unwritten page
		 * behind, but there is not much we can do there if
		 * filesystem refuses to write it.
		 */
		if (n == 0) {
			n = 1;
			clearobjflags = FALSE;
		}
		np = vm_page_find_least(object, pi + n);
	}
#if 0
	VOP_FSYNC(vp, (pagerflags & VM_PAGER_PUT_SYNC) ? MNT_WAIT : 0);
#endif

	if (clearobjflags)
		vm_object_clear_flag(object, OBJ_MIGHTBEDIRTY);
	return (res);
}

static int
vm_object_page_collect_flush(vm_object_t object, vm_page_t p, int pagerflags,
    int flags, boolean_t *clearobjflags, boolean_t *eio)
{
	vm_page_t ma[vm_pageout_page_count], p_first, tp;
	int count, i, mreq, runlen;

	vm_page_lock_assert(p, MA_NOTOWNED);
	VM_OBJECT_ASSERT_WLOCKED(object);

	count = 1;
	mreq = 0;

	for (tp = p; count < vm_pageout_page_count; count++) {
		tp = vm_page_next(tp);
		if (tp == NULL || vm_page_busied(tp))
			break;
		if (!vm_object_page_remove_write(tp, flags, clearobjflags))
			break;
	}

	for (p_first = p; count < vm_pageout_page_count; count++) {
		tp = vm_page_prev(p_first);
		if (tp == NULL || vm_page_busied(tp))
			break;
		if (!vm_object_page_remove_write(tp, flags, clearobjflags))
			break;
		p_first = tp;
		mreq++;
	}

	for (tp = p_first, i = 0; i < count; tp = TAILQ_NEXT(tp, listq), i++)
		ma[i] = tp;

	vm_pageout_flush(ma, count, pagerflags, mreq, &runlen, eio);
	return (runlen);
}

/*
 * Note that there is absolutely no sense in writing out
 * anonymous objects, so we track down the vnode object
 * to write out.
 * We invalidate (remove) all pages from the address space
 * for semantic correctness.
 *
 * If the backing object is a device object with unmanaged pages, then any
 * mappings to the specified range of pages must be removed before this
 * function is called.
 *
 * Note: certain anonymous maps, such as MAP_NOSYNC maps,
 * may start out with a NULL object.
 */
boolean_t
vm_object_sync(vm_object_t object, vm_ooffset_t offset, vm_size_t size,
    boolean_t syncio, boolean_t invalidate)
{
	vm_object_t backing_object;
	struct vnode *vp;
	struct mount *mp;
	int error, flags, fsync_after;
	boolean_t res;

	if (object == NULL)
		return (TRUE);
	res = TRUE;
	error = 0;
	VM_OBJECT_WLOCK(object);
	while ((backing_object = object->backing_object) != NULL) {
		VM_OBJECT_WLOCK(backing_object);
		offset += object->backing_object_offset;
		VM_OBJECT_WUNLOCK(object);
		object = backing_object;
		if (object->size < OFF_TO_IDX(offset + size))
			size = IDX_TO_OFF(object->size) - offset;
	}
	/*
	 * Flush pages if writing is allowed, invalidate them
	 * if invalidation requested.  Pages undergoing I/O
	 * will be ignored by vm_object_page_remove().
	 *
	 * We cannot lock the vnode and then wait for paging
	 * to complete without deadlocking against vm_fault.
	 * Instead we simply call vm_object_page_remove() and
	 * allow it to block internally on a page-by-page
	 * basis when it encounters pages undergoing async
	 * I/O.
	 */
	if (object->type == OBJT_VNODE &&
	    (object->flags & OBJ_MIGHTBEDIRTY) != 0 &&
	    ((vp = object->handle)->v_vflag & VV_NOSYNC) == 0) {
		VM_OBJECT_WUNLOCK(object);
		(void) vn_start_write(vp, &mp, V_WAIT);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		if (syncio && !invalidate && offset == 0 &&
		    atop(size) == object->size) {
			/*
			 * If syncing the whole mapping of the file,
			 * it is faster to schedule all the writes in
			 * async mode, also allowing the clustering,
			 * and then wait for i/o to complete.
			 */
			flags = 0;
			fsync_after = TRUE;
		} else {
			flags = (syncio || invalidate) ? OBJPC_SYNC : 0;
			flags |= invalidate ? (OBJPC_SYNC | OBJPC_INVAL) : 0;
			fsync_after = FALSE;
		}
		VM_OBJECT_WLOCK(object);
		res = vm_object_page_clean(object, offset, offset + size,
		    flags);
		VM_OBJECT_WUNLOCK(object);
		if (fsync_after)
			error = VOP_FSYNC(vp, MNT_WAIT, curthread);
		VOP_UNLOCK(vp, 0);
		vn_finished_write(mp);
		if (error != 0)
			res = FALSE;
		VM_OBJECT_WLOCK(object);
	}
	if ((object->type == OBJT_VNODE ||
	     object->type == OBJT_DEVICE) && invalidate) {
		if (object->type == OBJT_DEVICE)
			/*
			 * The option OBJPR_NOTMAPPED must be passed here
			 * because vm_object_page_remove() cannot remove
			 * unmanaged mappings.
			 */
			flags = OBJPR_NOTMAPPED;
		else if (old_msync)
			flags = 0;
		else
			flags = OBJPR_CLEANONLY;
		vm_object_page_remove(object, OFF_TO_IDX(offset),
		    OFF_TO_IDX(offset + size + PAGE_MASK), flags);
	}
	VM_OBJECT_WUNLOCK(object);
	return (res);
}

/*
 * Determine whether the given advice can be applied to the object.  Advice is
 * not applied to unmanaged pages since they never belong to page queues, and
 * since MADV_FREE is destructive, it can apply only to anonymous pages that
 * have been mapped at most once.
 */
static bool
vm_object_advice_applies(vm_object_t object, int advice)
{

	if ((object->flags & OBJ_UNMANAGED) != 0)
		return (false);
	if (advice != MADV_FREE)
		return (true);
	return ((object->type == OBJT_DEFAULT || object->type == OBJT_SWAP) &&
	    (object->flags & OBJ_ONEMAPPING) != 0);
}

static void
vm_object_madvise_freespace(vm_object_t object, int advice, vm_pindex_t pindex,
    vm_size_t size)
{

	if (advice == MADV_FREE && object->type == OBJT_SWAP)
		swap_pager_freespace(object, pindex, size);
}

/*
 *	vm_object_madvise:
 *
 *	Implements the madvise function at the object/page level.
 *
 *	MADV_WILLNEED	(any object)
 *
 *	    Activate the specified pages if they are resident.
 *
 *	MADV_DONTNEED	(any object)
 *
 *	    Deactivate the specified pages if they are resident.
 *
 *	MADV_FREE	(OBJT_DEFAULT/OBJT_SWAP objects,
 *			 OBJ_ONEMAPPING only)
 *
 *	    Deactivate and clean the specified pages if they are
 *	    resident.  This permits the process to reuse the pages
 *	    without faulting or the kernel to reclaim the pages
 *	    without I/O.
 */
void
vm_object_madvise(vm_object_t object, vm_pindex_t pindex, vm_pindex_t end,
    int advice)
{
	vm_pindex_t tpindex;
	vm_object_t backing_object, tobject;
	vm_page_t m, tm;

	if (object == NULL)
		return;

relookup:
	VM_OBJECT_WLOCK(object);
	if (!vm_object_advice_applies(object, advice)) {
		VM_OBJECT_WUNLOCK(object);
		return;
	}
	for (m = vm_page_find_least(object, pindex); pindex < end; pindex++) {
		tobject = object;

		/*
		 * If the next page isn't resident in the top-level object, we
		 * need to search the shadow chain.  When applying MADV_FREE, we
		 * take care to release any swap space used to store
		 * non-resident pages.
		 */
		if (m == NULL || pindex < m->pindex) {
			/*
			 * Optimize a common case: if the top-level object has
			 * no backing object, we can skip over the non-resident
			 * range in constant time.
			 */
			if (object->backing_object == NULL) {
				tpindex = (m != NULL && m->pindex < end) ?
				    m->pindex : end;
				vm_object_madvise_freespace(object, advice,
				    pindex, tpindex - pindex);
				if ((pindex = tpindex) == end)
					break;
				goto next_page;
			}

			tpindex = pindex;
			do {
				vm_object_madvise_freespace(tobject, advice,
				    tpindex, 1);
				/*
				 * Prepare to search the next object in the
				 * chain.
				 */
				backing_object = tobject->backing_object;
				if (backing_object == NULL)
					goto next_pindex;
				VM_OBJECT_WLOCK(backing_object);
				tpindex +=
				    OFF_TO_IDX(tobject->backing_object_offset);
				if (tobject != object)
					VM_OBJECT_WUNLOCK(tobject);
				tobject = backing_object;
				if (!vm_object_advice_applies(tobject, advice))
					goto next_pindex;
			} while ((tm = vm_page_lookup(tobject, tpindex)) ==
			    NULL);
		} else {
next_page:
			tm = m;
			m = TAILQ_NEXT(m, listq);
		}

		/*
		 * If the page is not in a normal state, skip it.
		 */
		if (tm->valid != VM_PAGE_BITS_ALL)
			goto next_pindex;
		vm_page_lock(tm);
		if (vm_page_held(tm)) {
			vm_page_unlock(tm);
			goto next_pindex;
		}
		KASSERT((tm->flags & PG_FICTITIOUS) == 0,
		    ("vm_object_madvise: page %p is fictitious", tm));
		KASSERT((tm->oflags & VPO_UNMANAGED) == 0,
		    ("vm_object_madvise: page %p is not managed", tm));
		if (vm_page_busied(tm)) {
			if (object != tobject)
				VM_OBJECT_WUNLOCK(tobject);
			VM_OBJECT_WUNLOCK(object);
			if (advice == MADV_WILLNEED) {
				/*
				 * Reference the page before unlocking and
				 * sleeping so that the page daemon is less
				 * likely to reclaim it.
				 */
				vm_page_aflag_set(tm, PGA_REFERENCED);
			}
			vm_page_busy_sleep(tm, "madvpo", false);
  			goto relookup;
		}
		vm_page_advise(tm, advice);
		vm_page_unlock(tm);
		vm_object_madvise_freespace(tobject, advice, tm->pindex, 1);
next_pindex:
		if (tobject != object)
			VM_OBJECT_WUNLOCK(tobject);
	}
	VM_OBJECT_WUNLOCK(object);
}

/*
 *	vm_object_shadow:
 *
 *	Create a new object which is backed by the
 *	specified existing object range.  The source
 *	object reference is deallocated.
 *
 *	The new object and offset into that object
 *	are returned in the source parameters.
 */
void
vm_object_shadow(
	vm_object_t *object,	/* IN/OUT */
	vm_ooffset_t *offset,	/* IN/OUT */
	vm_size_t length)
{
	vm_object_t source;
	vm_object_t result;

	source = *object;

	/*
	 * Don't create the new object if the old object isn't shared.
	 */
	if (source != NULL) {
		VM_OBJECT_WLOCK(source);
		if (source->ref_count == 1 &&
		    source->handle == NULL &&
		    (source->type == OBJT_DEFAULT ||
		     source->type == OBJT_SWAP)) {
			VM_OBJECT_WUNLOCK(source);
			return;
		}
		VM_OBJECT_WUNLOCK(source);
	}

	/*
	 * Allocate a new object with the given length.
	 */
	result = vm_object_allocate(OBJT_DEFAULT, atop(length));

	/*
	 * The new object shadows the source object, adding a reference to it.
	 * Our caller changes his reference to point to the new object,
	 * removing a reference to the source object.  Net result: no change
	 * of reference count.
	 *
	 * Try to optimize the result object's page color when shadowing
	 * in order to maintain page coloring consistency in the combined 
	 * shadowed object.
	 */
	result->backing_object = source;
	/*
	 * Store the offset into the source object, and fix up the offset into
	 * the new object.
	 */
	result->backing_object_offset = *offset;
	if (source != NULL) {
		VM_OBJECT_WLOCK(source);
		result->domain = source->domain;
		LIST_INSERT_HEAD(&source->shadow_head, result, shadow_list);
		source->shadow_count++;
#if VM_NRESERVLEVEL > 0
		result->flags |= source->flags & OBJ_COLORED;
		result->pg_color = (source->pg_color + OFF_TO_IDX(*offset)) &
		    ((1 << (VM_NFREEORDER - 1)) - 1);
#endif
		VM_OBJECT_WUNLOCK(source);
	}


	/*
	 * Return the new things
	 */
	*offset = 0;
	*object = result;
}

/*
 *	vm_object_split:
 *
 * Split the pages in a map entry into a new object.  This affords
 * easier removal of unused pages, and keeps object inheritance from
 * being a negative impact on memory usage.
 */
void
vm_object_split(vm_map_entry_t entry)
{
	vm_page_t m, m_next;
	vm_object_t orig_object, new_object, source;
	vm_pindex_t idx, offidxstart;
	vm_size_t size;

	orig_object = entry->object.vm_object;
	if (orig_object->type != OBJT_DEFAULT && orig_object->type != OBJT_SWAP)
		return;
	if (orig_object->ref_count <= 1)
		return;
	VM_OBJECT_WUNLOCK(orig_object);

	offidxstart = OFF_TO_IDX(entry->offset);
	size = atop(entry->end - entry->start);

	/*
	 * If swap_pager_copy() is later called, it will convert new_object
	 * into a swap object.
	 */
	new_object = vm_object_allocate(OBJT_DEFAULT, size);

	/*
	 * At this point, the new object is still private, so the order in
	 * which the original and new objects are locked does not matter.
	 */
	VM_OBJECT_WLOCK(new_object);
	VM_OBJECT_WLOCK(orig_object);
	new_object->domain = orig_object->domain;
	source = orig_object->backing_object;
	if (source != NULL) {
		VM_OBJECT_WLOCK(source);
		if ((source->flags & OBJ_DEAD) != 0) {
			VM_OBJECT_WUNLOCK(source);
			VM_OBJECT_WUNLOCK(orig_object);
			VM_OBJECT_WUNLOCK(new_object);
			vm_object_deallocate(new_object);
			VM_OBJECT_WLOCK(orig_object);
			return;
		}
		LIST_INSERT_HEAD(&source->shadow_head,
				  new_object, shadow_list);
		source->shadow_count++;
		vm_object_reference_locked(source);	/* for new_object */
		vm_object_clear_flag(source, OBJ_ONEMAPPING);
		VM_OBJECT_WUNLOCK(source);
		new_object->backing_object_offset = 
			orig_object->backing_object_offset + entry->offset;
		new_object->backing_object = source;
	}
	if (orig_object->cred != NULL) {
		new_object->cred = orig_object->cred;
		crhold(orig_object->cred);
		new_object->charge = ptoa(size);
		KASSERT(orig_object->charge >= ptoa(size),
		    ("orig_object->charge < 0"));
		orig_object->charge -= ptoa(size);
	}
retry:
	m = vm_page_find_least(orig_object, offidxstart);
	for (; m != NULL && (idx = m->pindex - offidxstart) < size;
	    m = m_next) {
		m_next = TAILQ_NEXT(m, listq);

		/*
		 * We must wait for pending I/O to complete before we can
		 * rename the page.
		 *
		 * We do not have to VM_PROT_NONE the page as mappings should
		 * not be changed by this operation.
		 */
		if (vm_page_busied(m)) {
			VM_OBJECT_WUNLOCK(new_object);
			vm_page_lock(m);
			VM_OBJECT_WUNLOCK(orig_object);
			vm_page_busy_sleep(m, "spltwt", false);
			VM_OBJECT_WLOCK(orig_object);
			VM_OBJECT_WLOCK(new_object);
			goto retry;
		}

		/* vm_page_rename() will dirty the page. */
		if (vm_page_rename(m, new_object, idx)) {
			VM_OBJECT_WUNLOCK(new_object);
			VM_OBJECT_WUNLOCK(orig_object);
			vm_radix_wait();
			VM_OBJECT_WLOCK(orig_object);
			VM_OBJECT_WLOCK(new_object);
			goto retry;
		}
#if VM_NRESERVLEVEL > 0
		/*
		 * If some of the reservation's allocated pages remain with
		 * the original object, then transferring the reservation to
		 * the new object is neither particularly beneficial nor
		 * particularly harmful as compared to leaving the reservation
		 * with the original object.  If, however, all of the
		 * reservation's allocated pages are transferred to the new
		 * object, then transferring the reservation is typically
		 * beneficial.  Determining which of these two cases applies
		 * would be more costly than unconditionally renaming the
		 * reservation.
		 */
		vm_reserv_rename(m, new_object, orig_object, offidxstart);
#endif
		if (orig_object->type == OBJT_SWAP)
			vm_page_xbusy(m);
	}
	if (orig_object->type == OBJT_SWAP) {
		/*
		 * swap_pager_copy() can sleep, in which case the orig_object's
		 * and new_object's locks are released and reacquired. 
		 */
		swap_pager_copy(orig_object, new_object, offidxstart, 0);
		TAILQ_FOREACH(m, &new_object->memq, listq)
			vm_page_xunbusy(m);
	}
	VM_OBJECT_WUNLOCK(orig_object);
	VM_OBJECT_WUNLOCK(new_object);
	entry->object.vm_object = new_object;
	entry->offset = 0LL;
	vm_object_deallocate(orig_object);
	VM_OBJECT_WLOCK(new_object);
}

#define	OBSC_COLLAPSE_NOWAIT	0x0002
#define	OBSC_COLLAPSE_WAIT	0x0004

static vm_page_t
vm_object_collapse_scan_wait(vm_object_t object, vm_page_t p, vm_page_t next,
    int op)
{
	vm_object_t backing_object;

	VM_OBJECT_ASSERT_WLOCKED(object);
	backing_object = object->backing_object;
	VM_OBJECT_ASSERT_WLOCKED(backing_object);

	KASSERT(p == NULL || vm_page_busied(p), ("unbusy page %p", p));
	KASSERT(p == NULL || p->object == object || p->object == backing_object,
	    ("invalid ownership %p %p %p", p, object, backing_object));
	if ((op & OBSC_COLLAPSE_NOWAIT) != 0)
		return (next);
	if (p != NULL)
		vm_page_lock(p);
	VM_OBJECT_WUNLOCK(object);
	VM_OBJECT_WUNLOCK(backing_object);
	/* The page is only NULL when rename fails. */
	if (p == NULL)
		vm_radix_wait();
	else
		vm_page_busy_sleep(p, "vmocol", false);
	VM_OBJECT_WLOCK(object);
	VM_OBJECT_WLOCK(backing_object);
	return (TAILQ_FIRST(&backing_object->memq));
}

static bool
vm_object_scan_all_shadowed(vm_object_t object)
{
	vm_object_t backing_object;
	vm_page_t p, pp;
	vm_pindex_t backing_offset_index, new_pindex, pi, ps;

	VM_OBJECT_ASSERT_WLOCKED(object);
	VM_OBJECT_ASSERT_WLOCKED(object->backing_object);

	backing_object = object->backing_object;

	if (backing_object->type != OBJT_DEFAULT &&
	    backing_object->type != OBJT_SWAP)
		return (false);

	pi = backing_offset_index = OFF_TO_IDX(object->backing_object_offset);
	p = vm_page_find_least(backing_object, pi);
	ps = swap_pager_find_least(backing_object, pi);

	/*
	 * Only check pages inside the parent object's range and
	 * inside the parent object's mapping of the backing object.
	 */
	for (;; pi++) {
		if (p != NULL && p->pindex < pi)
			p = TAILQ_NEXT(p, listq);
		if (ps < pi)
			ps = swap_pager_find_least(backing_object, pi);
		if (p == NULL && ps >= backing_object->size)
			break;
		else if (p == NULL)
			pi = ps;
		else
			pi = MIN(p->pindex, ps);

		new_pindex = pi - backing_offset_index;
		if (new_pindex >= object->size)
			break;

		/*
		 * See if the parent has the page or if the parent's object
		 * pager has the page.  If the parent has the page but the page
		 * is not valid, the parent's object pager must have the page.
		 *
		 * If this fails, the parent does not completely shadow the
		 * object and we might as well give up now.
		 */
		pp = vm_page_lookup(object, new_pindex);
		if ((pp == NULL || pp->valid == 0) &&
		    !vm_pager_has_page(object, new_pindex, NULL, NULL))
			return (false);
	}
	return (true);
}

static bool
vm_object_collapse_scan(vm_object_t object, int op)
{
	vm_object_t backing_object;
	vm_page_t next, p, pp;
	vm_pindex_t backing_offset_index, new_pindex;

	VM_OBJECT_ASSERT_WLOCKED(object);
	VM_OBJECT_ASSERT_WLOCKED(object->backing_object);

	backing_object = object->backing_object;
	backing_offset_index = OFF_TO_IDX(object->backing_object_offset);

	/*
	 * Initial conditions
	 */
	if ((op & OBSC_COLLAPSE_WAIT) != 0)
		vm_object_set_flag(backing_object, OBJ_DEAD);

	/*
	 * Our scan
	 */
	for (p = TAILQ_FIRST(&backing_object->memq); p != NULL; p = next) {
		next = TAILQ_NEXT(p, listq);
		new_pindex = p->pindex - backing_offset_index;

		/*
		 * Check for busy page
		 */
		if (vm_page_busied(p)) {
			next = vm_object_collapse_scan_wait(object, p, next, op);
			continue;
		}

		KASSERT(p->object == backing_object,
		    ("vm_object_collapse_scan: object mismatch"));

		if (p->pindex < backing_offset_index ||
		    new_pindex >= object->size) {
			if (backing_object->type == OBJT_SWAP)
				swap_pager_freespace(backing_object, p->pindex,
				    1);

			/*
			 * Page is out of the parent object's range, we can
			 * simply destroy it.
			 */
			vm_page_lock(p);
			KASSERT(!pmap_page_is_mapped(p),
			    ("freeing mapped page %p", p));
			if (p->wire_count == 0)
				vm_page_free(p);
			else
				vm_page_remove(p);
			vm_page_unlock(p);
			continue;
		}

		pp = vm_page_lookup(object, new_pindex);
		if (pp != NULL && vm_page_busied(pp)) {
			/*
			 * The page in the parent is busy and possibly not
			 * (yet) valid.  Until its state is finalized by the
			 * busy bit owner, we can't tell whether it shadows the
			 * original page.  Therefore, we must either skip it
			 * and the original (backing_object) page or wait for
			 * its state to be finalized.
			 *
			 * This is due to a race with vm_fault() where we must
			 * unbusy the original (backing_obj) page before we can
			 * (re)lock the parent.  Hence we can get here.
			 */
			next = vm_object_collapse_scan_wait(object, pp, next,
			    op);
			continue;
		}

		KASSERT(pp == NULL || pp->valid != 0,
		    ("unbusy invalid page %p", pp));

		if (pp != NULL || vm_pager_has_page(object, new_pindex, NULL,
			NULL)) {
			/*
			 * The page already exists in the parent OR swap exists
			 * for this location in the parent.  Leave the parent's
			 * page alone.  Destroy the original page from the
			 * backing object.
			 */
			if (backing_object->type == OBJT_SWAP)
				swap_pager_freespace(backing_object, p->pindex,
				    1);
			vm_page_lock(p);
			KASSERT(!pmap_page_is_mapped(p),
			    ("freeing mapped page %p", p));
			if (p->wire_count == 0)
				vm_page_free(p);
			else
				vm_page_remove(p);
			vm_page_unlock(p);
			continue;
		}

		/*
		 * Page does not exist in parent, rename the page from the
		 * backing object to the main object.
		 *
		 * If the page was mapped to a process, it can remain mapped
		 * through the rename.  vm_page_rename() will dirty the page.
		 */
		if (vm_page_rename(p, object, new_pindex)) {
			next = vm_object_collapse_scan_wait(object, NULL, next,
			    op);
			continue;
		}

		/* Use the old pindex to free the right page. */
		if (backing_object->type == OBJT_SWAP)
			swap_pager_freespace(backing_object,
			    new_pindex + backing_offset_index, 1);

#if VM_NRESERVLEVEL > 0
		/*
		 * Rename the reservation.
		 */
		vm_reserv_rename(p, object, backing_object,
		    backing_offset_index);
#endif
	}
	return (true);
}


/*
 * this version of collapse allows the operation to occur earlier and
 * when paging_in_progress is true for an object...  This is not a complete
 * operation, but should plug 99.9% of the rest of the leaks.
 */
static void
vm_object_qcollapse(vm_object_t object)
{
	vm_object_t backing_object = object->backing_object;

	VM_OBJECT_ASSERT_WLOCKED(object);
	VM_OBJECT_ASSERT_WLOCKED(backing_object);

	if (backing_object->ref_count != 1)
		return;

	vm_object_collapse_scan(object, OBSC_COLLAPSE_NOWAIT);
}

/*
 *	vm_object_collapse:
 *
 *	Collapse an object with the object backing it.
 *	Pages in the backing object are moved into the
 *	parent, and the backing object is deallocated.
 */
void
vm_object_collapse(vm_object_t object)
{
	vm_object_t backing_object, new_backing_object;

	VM_OBJECT_ASSERT_WLOCKED(object);

	while (TRUE) {
		/*
		 * Verify that the conditions are right for collapse:
		 *
		 * The object exists and the backing object exists.
		 */
		if ((backing_object = object->backing_object) == NULL)
			break;

		/*
		 * we check the backing object first, because it is most likely
		 * not collapsable.
		 */
		VM_OBJECT_WLOCK(backing_object);
		if (backing_object->handle != NULL ||
		    (backing_object->type != OBJT_DEFAULT &&
		     backing_object->type != OBJT_SWAP) ||
		    (backing_object->flags & OBJ_DEAD) ||
		    object->handle != NULL ||
		    (object->type != OBJT_DEFAULT &&
		     object->type != OBJT_SWAP) ||
		    (object->flags & OBJ_DEAD)) {
			VM_OBJECT_WUNLOCK(backing_object);
			break;
		}

		if (object->paging_in_progress != 0 ||
		    backing_object->paging_in_progress != 0) {
			vm_object_qcollapse(object);
			VM_OBJECT_WUNLOCK(backing_object);
			break;
		}

		/*
		 * We know that we can either collapse the backing object (if
		 * the parent is the only reference to it) or (perhaps) have
		 * the parent bypass the object if the parent happens to shadow
		 * all the resident pages in the entire backing object.
		 *
		 * This is ignoring pager-backed pages such as swap pages.
		 * vm_object_collapse_scan fails the shadowing test in this
		 * case.
		 */
		if (backing_object->ref_count == 1) {
			vm_object_pip_add(object, 1);
			vm_object_pip_add(backing_object, 1);

			/*
			 * If there is exactly one reference to the backing
			 * object, we can collapse it into the parent.
			 */
			vm_object_collapse_scan(object, OBSC_COLLAPSE_WAIT);

#if VM_NRESERVLEVEL > 0
			/*
			 * Break any reservations from backing_object.
			 */
			if (__predict_false(!LIST_EMPTY(&backing_object->rvq)))
				vm_reserv_break_all(backing_object);
#endif

			/*
			 * Move the pager from backing_object to object.
			 */
			if (backing_object->type == OBJT_SWAP) {
				/*
				 * swap_pager_copy() can sleep, in which case
				 * the backing_object's and object's locks are
				 * released and reacquired.
				 * Since swap_pager_copy() is being asked to
				 * destroy the source, it will change the
				 * backing_object's type to OBJT_DEFAULT.
				 */
				swap_pager_copy(
				    backing_object,
				    object,
				    OFF_TO_IDX(object->backing_object_offset), TRUE);
			}
			/*
			 * Object now shadows whatever backing_object did.
			 * Note that the reference to 
			 * backing_object->backing_object moves from within 
			 * backing_object to within object.
			 */
			LIST_REMOVE(object, shadow_list);
			backing_object->shadow_count--;
			if (backing_object->backing_object) {
				VM_OBJECT_WLOCK(backing_object->backing_object);
				LIST_REMOVE(backing_object, shadow_list);
				LIST_INSERT_HEAD(
				    &backing_object->backing_object->shadow_head,
				    object, shadow_list);
				/*
				 * The shadow_count has not changed.
				 */
				VM_OBJECT_WUNLOCK(backing_object->backing_object);
			}
			object->backing_object = backing_object->backing_object;
			object->backing_object_offset +=
			    backing_object->backing_object_offset;

			/*
			 * Discard backing_object.
			 *
			 * Since the backing object has no pages, no pager left,
			 * and no object references within it, all that is
			 * necessary is to dispose of it.
			 */
			KASSERT(backing_object->ref_count == 1, (
"backing_object %p was somehow re-referenced during collapse!",
			    backing_object));
			vm_object_pip_wakeup(backing_object);
			backing_object->type = OBJT_DEAD;
			backing_object->ref_count = 0;
			VM_OBJECT_WUNLOCK(backing_object);
			vm_object_destroy(backing_object);

			vm_object_pip_wakeup(object);
			counter_u64_add(object_collapses, 1);
		} else {
			/*
			 * If we do not entirely shadow the backing object,
			 * there is nothing we can do so we give up.
			 */
			if (object->resident_page_count != object->size &&
			    !vm_object_scan_all_shadowed(object)) {
				VM_OBJECT_WUNLOCK(backing_object);
				break;
			}

			/*
			 * Make the parent shadow the next object in the
			 * chain.  Deallocating backing_object will not remove
			 * it, since its reference count is at least 2.
			 */
			LIST_REMOVE(object, shadow_list);
			backing_object->shadow_count--;

			new_backing_object = backing_object->backing_object;
			if ((object->backing_object = new_backing_object) != NULL) {
				VM_OBJECT_WLOCK(new_backing_object);
				LIST_INSERT_HEAD(
				    &new_backing_object->shadow_head,
				    object,
				    shadow_list
				);
				new_backing_object->shadow_count++;
				vm_object_reference_locked(new_backing_object);
				VM_OBJECT_WUNLOCK(new_backing_object);
				object->backing_object_offset +=
					backing_object->backing_object_offset;
			}

			/*
			 * Drop the reference count on backing_object. Since
			 * its ref_count was at least 2, it will not vanish.
			 */
			backing_object->ref_count--;
			VM_OBJECT_WUNLOCK(backing_object);
			counter_u64_add(object_bypasses, 1);
		}

		/*
		 * Try again with this object's new backing object.
		 */
	}
}

/*
 *	vm_object_page_remove:
 *
 *	For the given object, either frees or invalidates each of the
 *	specified pages.  In general, a page is freed.  However, if a page is
 *	wired for any reason other than the existence of a managed, wired
 *	mapping, then it may be invalidated but not removed from the object.
 *	Pages are specified by the given range ["start", "end") and the option
 *	OBJPR_CLEANONLY.  As a special case, if "end" is zero, then the range
 *	extends from "start" to the end of the object.  If the option
 *	OBJPR_CLEANONLY is specified, then only the non-dirty pages within the
 *	specified range are affected.  If the option OBJPR_NOTMAPPED is
 *	specified, then the pages within the specified range must have no
 *	mappings.  Otherwise, if this option is not specified, any mappings to
 *	the specified pages are removed before the pages are freed or
 *	invalidated.
 *
 *	In general, this operation should only be performed on objects that
 *	contain managed pages.  There are, however, two exceptions.  First, it
 *	is performed on the kernel and kmem objects by vm_map_entry_delete().
 *	Second, it is used by msync(..., MS_INVALIDATE) to invalidate device-
 *	backed pages.  In both of these cases, the option OBJPR_CLEANONLY must
 *	not be specified and the option OBJPR_NOTMAPPED must be specified.
 *
 *	The object must be locked.
 */
void
vm_object_page_remove(vm_object_t object, vm_pindex_t start, vm_pindex_t end,
    int options)
{
	vm_page_t p, next;
	struct mtx *mtx;

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT((object->flags & OBJ_UNMANAGED) == 0 ||
	    (options & (OBJPR_CLEANONLY | OBJPR_NOTMAPPED)) == OBJPR_NOTMAPPED,
	    ("vm_object_page_remove: illegal options for object %p", object));
	if (object->resident_page_count == 0)
		return;
	vm_object_pip_add(object, 1);
again:
	p = vm_page_find_least(object, start);
	mtx = NULL;

	/*
	 * Here, the variable "p" is either (1) the page with the least pindex
	 * greater than or equal to the parameter "start" or (2) NULL. 
	 */
	for (; p != NULL && (p->pindex < end || end == 0); p = next) {
		next = TAILQ_NEXT(p, listq);

		/*
		 * If the page is wired for any reason besides the existence
		 * of managed, wired mappings, then it cannot be freed.  For
		 * example, fictitious pages, which represent device memory,
		 * are inherently wired and cannot be freed.  They can,
		 * however, be invalidated if the option OBJPR_CLEANONLY is
		 * not specified.
		 */
		vm_page_change_lock(p, &mtx);
		if (vm_page_xbusied(p)) {
			VM_OBJECT_WUNLOCK(object);
			vm_page_busy_sleep(p, "vmopax", true);
			VM_OBJECT_WLOCK(object);
			goto again;
		}
		if (p->wire_count != 0) {
			if ((options & OBJPR_NOTMAPPED) == 0 &&
			    object->ref_count != 0)
				pmap_remove_all(p);
			if ((options & OBJPR_CLEANONLY) == 0) {
				p->valid = 0;
				vm_page_undirty(p);
			}
			continue;
		}
		if (vm_page_busied(p)) {
			VM_OBJECT_WUNLOCK(object);
			vm_page_busy_sleep(p, "vmopar", false);
			VM_OBJECT_WLOCK(object);
			goto again;
		}
		KASSERT((p->flags & PG_FICTITIOUS) == 0,
		    ("vm_object_page_remove: page %p is fictitious", p));
		if ((options & OBJPR_CLEANONLY) != 0 && p->valid != 0) {
			if ((options & OBJPR_NOTMAPPED) == 0 &&
			    object->ref_count != 0)
				pmap_remove_write(p);
			if (p->dirty != 0)
				continue;
		}
		if ((options & OBJPR_NOTMAPPED) == 0 && object->ref_count != 0)
			pmap_remove_all(p);
		vm_page_free(p);
	}
	if (mtx != NULL)
		mtx_unlock(mtx);
	vm_object_pip_wakeup(object);
}

/*
 *	vm_object_page_noreuse:
 *
 *	For the given object, attempt to move the specified pages to
 *	the head of the inactive queue.  This bypasses regular LRU
 *	operation and allows the pages to be reused quickly under memory
 *	pressure.  If a page is wired for any reason, then it will not
 *	be queued.  Pages are specified by the range ["start", "end").
 *	As a special case, if "end" is zero, then the range extends from
 *	"start" to the end of the object.
 *
 *	This operation should only be performed on objects that
 *	contain non-fictitious, managed pages.
 *
 *	The object must be locked.
 */
void
vm_object_page_noreuse(vm_object_t object, vm_pindex_t start, vm_pindex_t end)
{
	struct mtx *mtx;
	vm_page_t p, next;

	VM_OBJECT_ASSERT_LOCKED(object);
	KASSERT((object->flags & (OBJ_FICTITIOUS | OBJ_UNMANAGED)) == 0,
	    ("vm_object_page_noreuse: illegal object %p", object));
	if (object->resident_page_count == 0)
		return;
	p = vm_page_find_least(object, start);

	/*
	 * Here, the variable "p" is either (1) the page with the least pindex
	 * greater than or equal to the parameter "start" or (2) NULL. 
	 */
	mtx = NULL;
	for (; p != NULL && (p->pindex < end || end == 0); p = next) {
		next = TAILQ_NEXT(p, listq);
		vm_page_change_lock(p, &mtx);
		vm_page_deactivate_noreuse(p);
	}
	if (mtx != NULL)
		mtx_unlock(mtx);
}

/*
 *	Populate the specified range of the object with valid pages.  Returns
 *	TRUE if the range is successfully populated and FALSE otherwise.
 *
 *	Note: This function should be optimized to pass a larger array of
 *	pages to vm_pager_get_pages() before it is applied to a non-
 *	OBJT_DEVICE object.
 *
 *	The object must be locked.
 */
boolean_t
vm_object_populate(vm_object_t object, vm_pindex_t start, vm_pindex_t end)
{
	vm_page_t m;
	vm_pindex_t pindex;
	int rv;

	VM_OBJECT_ASSERT_WLOCKED(object);
	for (pindex = start; pindex < end; pindex++) {
		m = vm_page_grab(object, pindex, VM_ALLOC_NORMAL);
		if (m->valid != VM_PAGE_BITS_ALL) {
			rv = vm_pager_get_pages(object, &m, 1, NULL, NULL);
			if (rv != VM_PAGER_OK) {
				vm_page_lock(m);
				vm_page_free(m);
				vm_page_unlock(m);
				break;
			}
		}
		/*
		 * Keep "m" busy because a subsequent iteration may unlock
		 * the object.
		 */
	}
	if (pindex > start) {
		m = vm_page_lookup(object, start);
		while (m != NULL && m->pindex < pindex) {
			vm_page_xunbusy(m);
			m = TAILQ_NEXT(m, listq);
		}
	}
	return (pindex == end);
}

/*
 *	Routine:	vm_object_coalesce
 *	Function:	Coalesces two objects backing up adjoining
 *			regions of memory into a single object.
 *
 *	returns TRUE if objects were combined.
 *
 *	NOTE:	Only works at the moment if the second object is NULL -
 *		if it's not, which object do we lock first?
 *
 *	Parameters:
 *		prev_object	First object to coalesce
 *		prev_offset	Offset into prev_object
 *		prev_size	Size of reference to prev_object
 *		next_size	Size of reference to the second object
 *		reserved	Indicator that extension region has
 *				swap accounted for
 *
 *	Conditions:
 *	The object must *not* be locked.
 */
boolean_t
vm_object_coalesce(vm_object_t prev_object, vm_ooffset_t prev_offset,
    vm_size_t prev_size, vm_size_t next_size, boolean_t reserved)
{
	vm_pindex_t next_pindex;

	if (prev_object == NULL)
		return (TRUE);
	VM_OBJECT_WLOCK(prev_object);
	if ((prev_object->type != OBJT_DEFAULT &&
	    prev_object->type != OBJT_SWAP) ||
	    (prev_object->flags & OBJ_TMPFS_NODE) != 0) {
		VM_OBJECT_WUNLOCK(prev_object);
		return (FALSE);
	}

	/*
	 * Try to collapse the object first
	 */
	vm_object_collapse(prev_object);

	/*
	 * Can't coalesce if: . more than one reference . paged out . shadows
	 * another object . has a copy elsewhere (any of which mean that the
	 * pages not mapped to prev_entry may be in use anyway)
	 */
	if (prev_object->backing_object != NULL) {
		VM_OBJECT_WUNLOCK(prev_object);
		return (FALSE);
	}

	prev_size >>= PAGE_SHIFT;
	next_size >>= PAGE_SHIFT;
	next_pindex = OFF_TO_IDX(prev_offset) + prev_size;

	if (prev_object->ref_count > 1 &&
	    prev_object->size != next_pindex &&
	    (prev_object->flags & OBJ_ONEMAPPING) == 0) {
		VM_OBJECT_WUNLOCK(prev_object);
		return (FALSE);
	}

	/*
	 * Account for the charge.
	 */
	if (prev_object->cred != NULL) {

		/*
		 * If prev_object was charged, then this mapping,
		 * although not charged now, may become writable
		 * later. Non-NULL cred in the object would prevent
		 * swap reservation during enabling of the write
		 * access, so reserve swap now. Failed reservation
		 * cause allocation of the separate object for the map
		 * entry, and swap reservation for this entry is
		 * managed in appropriate time.
		 */
		if (!reserved && !swap_reserve_by_cred(ptoa(next_size),
		    prev_object->cred)) {
			VM_OBJECT_WUNLOCK(prev_object);
			return (FALSE);
		}
		prev_object->charge += ptoa(next_size);
	}

	/*
	 * Remove any pages that may still be in the object from a previous
	 * deallocation.
	 */
	if (next_pindex < prev_object->size) {
		vm_object_page_remove(prev_object, next_pindex, next_pindex +
		    next_size, 0);
		if (prev_object->type == OBJT_SWAP)
			swap_pager_freespace(prev_object,
					     next_pindex, next_size);
#if 0
		if (prev_object->cred != NULL) {
			KASSERT(prev_object->charge >=
			    ptoa(prev_object->size - next_pindex),
			    ("object %p overcharged 1 %jx %jx", prev_object,
				(uintmax_t)next_pindex, (uintmax_t)next_size));
			prev_object->charge -= ptoa(prev_object->size -
			    next_pindex);
		}
#endif
	}

	/*
	 * Extend the object if necessary.
	 */
	if (next_pindex + next_size > prev_object->size)
		prev_object->size = next_pindex + next_size;

	VM_OBJECT_WUNLOCK(prev_object);
	return (TRUE);
}

void
vm_object_set_writeable_dirty(vm_object_t object)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	if (object->type != OBJT_VNODE) {
		if ((object->flags & OBJ_TMPFS_NODE) != 0) {
			KASSERT(object->type == OBJT_SWAP, ("non-swap tmpfs"));
			vm_object_set_flag(object, OBJ_TMPFS_DIRTY);
		}
		return;
	}
	object->generation++;
	if ((object->flags & OBJ_MIGHTBEDIRTY) != 0)
		return;
	vm_object_set_flag(object, OBJ_MIGHTBEDIRTY);
}

/*
 *	vm_object_unwire:
 *
 *	For each page offset within the specified range of the given object,
 *	find the highest-level page in the shadow chain and unwire it.  A page
 *	must exist at every page offset, and the highest-level page must be
 *	wired.
 */
void
vm_object_unwire(vm_object_t object, vm_ooffset_t offset, vm_size_t length,
    uint8_t queue)
{
	vm_object_t tobject, t1object;
	vm_page_t m, tm;
	vm_pindex_t end_pindex, pindex, tpindex;
	int depth, locked_depth;

	KASSERT((offset & PAGE_MASK) == 0,
	    ("vm_object_unwire: offset is not page aligned"));
	KASSERT((length & PAGE_MASK) == 0,
	    ("vm_object_unwire: length is not a multiple of PAGE_SIZE"));
	/* The wired count of a fictitious page never changes. */
	if ((object->flags & OBJ_FICTITIOUS) != 0)
		return;
	pindex = OFF_TO_IDX(offset);
	end_pindex = pindex + atop(length);
again:
	locked_depth = 1;
	VM_OBJECT_RLOCK(object);
	m = vm_page_find_least(object, pindex);
	while (pindex < end_pindex) {
		if (m == NULL || pindex < m->pindex) {
			/*
			 * The first object in the shadow chain doesn't
			 * contain a page at the current index.  Therefore,
			 * the page must exist in a backing object.
			 */
			tobject = object;
			tpindex = pindex;
			depth = 0;
			do {
				tpindex +=
				    OFF_TO_IDX(tobject->backing_object_offset);
				tobject = tobject->backing_object;
				KASSERT(tobject != NULL,
				    ("vm_object_unwire: missing page"));
				if ((tobject->flags & OBJ_FICTITIOUS) != 0)
					goto next_page;
				depth++;
				if (depth == locked_depth) {
					locked_depth++;
					VM_OBJECT_RLOCK(tobject);
				}
			} while ((tm = vm_page_lookup(tobject, tpindex)) ==
			    NULL);
		} else {
			tm = m;
			m = TAILQ_NEXT(m, listq);
		}
		vm_page_lock(tm);
		if (vm_page_xbusied(tm)) {
			for (tobject = object; locked_depth >= 1;
			    locked_depth--) {
				t1object = tobject->backing_object;
				VM_OBJECT_RUNLOCK(tobject);
				tobject = t1object;
			}
			vm_page_busy_sleep(tm, "unwbo", true);
			goto again;
		}
		vm_page_unwire(tm, queue);
		vm_page_unlock(tm);
next_page:
		pindex++;
	}
	/* Release the accumulated object locks. */
	for (tobject = object; locked_depth >= 1; locked_depth--) {
		t1object = tobject->backing_object;
		VM_OBJECT_RUNLOCK(tobject);
		tobject = t1object;
	}
}

/*
 * Return the vnode for the given object, or NULL if none exists.
 * For tmpfs objects, the function may return NULL if there is
 * no vnode allocated at the time of the call.
 */
struct vnode *
vm_object_vnode(vm_object_t object)
{
	struct vnode *vp;

	VM_OBJECT_ASSERT_LOCKED(object);
	if (object->type == OBJT_VNODE) {
		vp = object->handle;
		KASSERT(vp != NULL, ("%s: OBJT_VNODE has no vnode", __func__));
	} else if (object->type == OBJT_SWAP &&
	    (object->flags & OBJ_TMPFS) != 0) {
		vp = object->un_pager.swp.swp_tmpfs;
		KASSERT(vp != NULL, ("%s: OBJT_TMPFS has no vnode", __func__));
	} else {
		vp = NULL;
	}
	return (vp);
}

/*
 * Return the kvme type of the given object.
 * If vpp is not NULL, set it to the object's vm_object_vnode() or NULL.
 */
int
vm_object_kvme_type(vm_object_t object, struct vnode **vpp)
{

	VM_OBJECT_ASSERT_LOCKED(object);
	if (vpp != NULL)
		*vpp = vm_object_vnode(object);
	switch (object->type) {
	case OBJT_DEFAULT:
		return (KVME_TYPE_DEFAULT);
	case OBJT_VNODE:
		return (KVME_TYPE_VNODE);
	case OBJT_SWAP:
		if ((object->flags & OBJ_TMPFS_NODE) != 0)
			return (KVME_TYPE_VNODE);
		return (KVME_TYPE_SWAP);
	case OBJT_DEVICE:
		return (KVME_TYPE_DEVICE);
	case OBJT_PHYS:
		return (KVME_TYPE_PHYS);
	case OBJT_DEAD:
		return (KVME_TYPE_DEAD);
	case OBJT_SG:
		return (KVME_TYPE_SG);
	case OBJT_MGTDEVICE:
		return (KVME_TYPE_MGTDEVICE);
	default:
		return (KVME_TYPE_UNKNOWN);
	}
}

static int
sysctl_vm_object_list(SYSCTL_HANDLER_ARGS)
{
	struct kinfo_vmobject *kvo;
	char *fullpath, *freepath;
	struct vnode *vp;
	struct vattr va;
	vm_object_t obj;
	vm_page_t m;
	int count, error;

	if (req->oldptr == NULL) {
		/*
		 * If an old buffer has not been provided, generate an
		 * estimate of the space needed for a subsequent call.
		 */
		mtx_lock(&vm_object_list_mtx);
		count = 0;
		TAILQ_FOREACH(obj, &vm_object_list, object_list) {
			if (obj->type == OBJT_DEAD)
				continue;
			count++;
		}
		mtx_unlock(&vm_object_list_mtx);
		return (SYSCTL_OUT(req, NULL, sizeof(struct kinfo_vmobject) *
		    count * 11 / 10));
	}

	kvo = malloc(sizeof(*kvo), M_TEMP, M_WAITOK);
	error = 0;

	/*
	 * VM objects are type stable and are never removed from the
	 * list once added.  This allows us to safely read obj->object_list
	 * after reacquiring the VM object lock.
	 */
	mtx_lock(&vm_object_list_mtx);
	TAILQ_FOREACH(obj, &vm_object_list, object_list) {
		if (obj->type == OBJT_DEAD)
			continue;
		VM_OBJECT_RLOCK(obj);
		if (obj->type == OBJT_DEAD) {
			VM_OBJECT_RUNLOCK(obj);
			continue;
		}
		mtx_unlock(&vm_object_list_mtx);
		kvo->kvo_size = ptoa(obj->size);
		kvo->kvo_resident = obj->resident_page_count;
		kvo->kvo_ref_count = obj->ref_count;
		kvo->kvo_shadow_count = obj->shadow_count;
		kvo->kvo_memattr = obj->memattr;
		kvo->kvo_active = 0;
		kvo->kvo_inactive = 0;
		TAILQ_FOREACH(m, &obj->memq, listq) {
			/*
			 * A page may belong to the object but be
			 * dequeued and set to PQ_NONE while the
			 * object lock is not held.  This makes the
			 * reads of m->queue below racy, and we do not
			 * count pages set to PQ_NONE.  However, this
			 * sysctl is only meant to give an
			 * approximation of the system anyway.
			 */
			if (m->queue == PQ_ACTIVE)
				kvo->kvo_active++;
			else if (m->queue == PQ_INACTIVE)
				kvo->kvo_inactive++;
		}

		kvo->kvo_vn_fileid = 0;
		kvo->kvo_vn_fsid = 0;
		kvo->kvo_vn_fsid_freebsd11 = 0;
		freepath = NULL;
		fullpath = "";
		kvo->kvo_type = vm_object_kvme_type(obj, &vp);
		if (vp != NULL)
			vref(vp);
		VM_OBJECT_RUNLOCK(obj);
		if (vp != NULL) {
			vn_fullpath(curthread, vp, &fullpath, &freepath);
			vn_lock(vp, LK_SHARED | LK_RETRY);
			if (VOP_GETATTR(vp, &va, curthread->td_ucred) == 0) {
				kvo->kvo_vn_fileid = va.va_fileid;
				kvo->kvo_vn_fsid = va.va_fsid;
				kvo->kvo_vn_fsid_freebsd11 = va.va_fsid;
								/* truncate */
			}
			vput(vp);
		}

		strlcpy(kvo->kvo_path, fullpath, sizeof(kvo->kvo_path));
		if (freepath != NULL)
			free(freepath, M_TEMP);

		/* Pack record size down */
		kvo->kvo_structsize = offsetof(struct kinfo_vmobject, kvo_path)
		    + strlen(kvo->kvo_path) + 1;
		kvo->kvo_structsize = roundup(kvo->kvo_structsize,
		    sizeof(uint64_t));
		error = SYSCTL_OUT(req, kvo, kvo->kvo_structsize);
		mtx_lock(&vm_object_list_mtx);
		if (error)
			break;
	}
	mtx_unlock(&vm_object_list_mtx);
	free(kvo, M_TEMP);
	return (error);
}
SYSCTL_PROC(_vm, OID_AUTO, objects, CTLTYPE_STRUCT | CTLFLAG_RW | CTLFLAG_SKIP |
    CTLFLAG_MPSAFE, NULL, 0, sysctl_vm_object_list, "S,kinfo_vmobject",
    "List of VM objects");

#include "opt_ddb.h"
#ifdef DDB
#include <sys/kernel.h>

#include <sys/cons.h>

#include <ddb/ddb.h>

static int
_vm_object_in_map(vm_map_t map, vm_object_t object, vm_map_entry_t entry)
{
	vm_map_t tmpm;
	vm_map_entry_t tmpe;
	vm_object_t obj;
	int entcount;

	if (map == 0)
		return 0;

	if (entry == 0) {
		tmpe = map->header.next;
		entcount = map->nentries;
		while (entcount-- && (tmpe != &map->header)) {
			if (_vm_object_in_map(map, object, tmpe)) {
				return 1;
			}
			tmpe = tmpe->next;
		}
	} else if (entry->eflags & MAP_ENTRY_IS_SUB_MAP) {
		tmpm = entry->object.sub_map;
		tmpe = tmpm->header.next;
		entcount = tmpm->nentries;
		while (entcount-- && tmpe != &tmpm->header) {
			if (_vm_object_in_map(tmpm, object, tmpe)) {
				return 1;
			}
			tmpe = tmpe->next;
		}
	} else if ((obj = entry->object.vm_object) != NULL) {
		for (; obj; obj = obj->backing_object)
			if (obj == object) {
				return 1;
			}
	}
	return 0;
}

static int
vm_object_in_map(vm_object_t object)
{
	struct proc *p;

	/* sx_slock(&allproc_lock); */
	FOREACH_PROC_IN_SYSTEM(p) {
		if (!p->p_vmspace /* || (p->p_flag & (P_SYSTEM|P_WEXIT)) */)
			continue;
		if (_vm_object_in_map(&p->p_vmspace->vm_map, object, 0)) {
			/* sx_sunlock(&allproc_lock); */
			return 1;
		}
	}
	/* sx_sunlock(&allproc_lock); */
	if (_vm_object_in_map(kernel_map, object, 0))
		return 1;
	return 0;
}

DB_SHOW_COMMAND(vmochk, vm_object_check)
{
	vm_object_t object;

	/*
	 * make sure that internal objs are in a map somewhere
	 * and none have zero ref counts.
	 */
	TAILQ_FOREACH(object, &vm_object_list, object_list) {
		if (object->handle == NULL &&
		    (object->type == OBJT_DEFAULT || object->type == OBJT_SWAP)) {
			if (object->ref_count == 0) {
				db_printf("vmochk: internal obj has zero ref count: %ld\n",
					(long)object->size);
			}
			if (!vm_object_in_map(object)) {
				db_printf(
			"vmochk: internal obj is not in a map: "
			"ref: %d, size: %lu: 0x%lx, backing_object: %p\n",
				    object->ref_count, (u_long)object->size, 
				    (u_long)object->size,
				    (void *)object->backing_object);
			}
		}
	}
}

/*
 *	vm_object_print:	[ debug ]
 */
DB_SHOW_COMMAND(object, vm_object_print_static)
{
	/* XXX convert args. */
	vm_object_t object = (vm_object_t)addr;
	boolean_t full = have_addr;

	vm_page_t p;

	/* XXX count is an (unused) arg.  Avoid shadowing it. */
#define	count	was_count

	int count;

	if (object == NULL)
		return;

	db_iprintf(
	    "Object %p: type=%d, size=0x%jx, res=%d, ref=%d, flags=0x%x ruid %d charge %jx\n",
	    object, (int)object->type, (uintmax_t)object->size,
	    object->resident_page_count, object->ref_count, object->flags,
	    object->cred ? object->cred->cr_ruid : -1, (uintmax_t)object->charge);
	db_iprintf(" sref=%d, backing_object(%d)=(%p)+0x%jx\n",
	    object->shadow_count, 
	    object->backing_object ? object->backing_object->ref_count : 0,
	    object->backing_object, (uintmax_t)object->backing_object_offset);

	if (!full)
		return;

	db_indent += 2;
	count = 0;
	TAILQ_FOREACH(p, &object->memq, listq) {
		if (count == 0)
			db_iprintf("memory:=");
		else if (count == 6) {
			db_printf("\n");
			db_iprintf(" ...");
			count = 0;
		} else
			db_printf(",");
		count++;

		db_printf("(off=0x%jx,page=0x%jx)",
		    (uintmax_t)p->pindex, (uintmax_t)VM_PAGE_TO_PHYS(p));
	}
	if (count != 0)
		db_printf("\n");
	db_indent -= 2;
}

/* XXX. */
#undef count

/* XXX need this non-static entry for calling from vm_map_print. */
void
vm_object_print(
        /* db_expr_t */ long addr,
	boolean_t have_addr,
	/* db_expr_t */ long count,
	char *modif)
{
	vm_object_print_static(addr, have_addr, count, modif);
}

DB_SHOW_COMMAND(vmopag, vm_object_print_pages)
{
	vm_object_t object;
	vm_pindex_t fidx;
	vm_paddr_t pa;
	vm_page_t m, prev_m;
	int rcount, nl, c;

	nl = 0;
	TAILQ_FOREACH(object, &vm_object_list, object_list) {
		db_printf("new object: %p\n", (void *)object);
		if (nl > 18) {
			c = cngetc();
			if (c != ' ')
				return;
			nl = 0;
		}
		nl++;
		rcount = 0;
		fidx = 0;
		pa = -1;
		TAILQ_FOREACH(m, &object->memq, listq) {
			if (m->pindex > 128)
				break;
			if ((prev_m = TAILQ_PREV(m, pglist, listq)) != NULL &&
			    prev_m->pindex + 1 != m->pindex) {
				if (rcount) {
					db_printf(" index(%ld)run(%d)pa(0x%lx)\n",
						(long)fidx, rcount, (long)pa);
					if (nl > 18) {
						c = cngetc();
						if (c != ' ')
							return;
						nl = 0;
					}
					nl++;
					rcount = 0;
				}
			}				
			if (rcount &&
				(VM_PAGE_TO_PHYS(m) == pa + rcount * PAGE_SIZE)) {
				++rcount;
				continue;
			}
			if (rcount) {
				db_printf(" index(%ld)run(%d)pa(0x%lx)\n",
					(long)fidx, rcount, (long)pa);
				if (nl > 18) {
					c = cngetc();
					if (c != ' ')
						return;
					nl = 0;
				}
				nl++;
			}
			fidx = m->pindex;
			pa = VM_PAGE_TO_PHYS(m);
			rcount = 1;
		}
		if (rcount) {
			db_printf(" index(%ld)run(%d)pa(0x%lx)\n",
				(long)fidx, rcount, (long)pa);
			if (nl > 18) {
				c = cngetc();
				if (c != ' ')
					return;
				nl = 0;
			}
			nl++;
		}
	}
}
#endif /* DDB */
