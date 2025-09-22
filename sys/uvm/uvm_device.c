/*	$OpenBSD: uvm_device.c,v 1.68 2024/12/15 11:02:59 mpi Exp $	*/
/*	$NetBSD: uvm_device.c,v 1.30 2000/11/25 06:27:59 chs Exp $	*/

/*
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * from: Id: uvm_device.c,v 1.1.2.9 1998/02/06 05:11:47 chs Exp
 */

/*
 * uvm_device.c: the device pager.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <uvm/uvm.h>
#include <uvm/uvm_device.h>

#include "drm.h"

/*
 * private global data structure
 *
 * we keep a list of active device objects in the system.
 */

LIST_HEAD(, uvm_device) udv_list = LIST_HEAD_INITIALIZER(udv_list);
struct mutex udv_lock = MUTEX_INITIALIZER(IPL_NONE);

/*
 * functions
 */
static void             udv_reference(struct uvm_object *);
static void             udv_detach(struct uvm_object *);
static int		udv_fault(struct uvm_faultinfo *, vaddr_t,
				       vm_page_t *, int, int, vm_fault_t,
				       vm_prot_t, int);
static boolean_t        udv_flush(struct uvm_object *, voff_t, voff_t,
				       int);

/*
 * master pager structure
 */
const struct uvm_pagerops uvm_deviceops = {
	.pgo_reference = udv_reference,
	.pgo_detach = udv_detach,
	.pgo_fault = udv_fault,
	.pgo_flush = udv_flush,
};

/*
 * the ops!
 */


/*
 * udv_attach
 *
 * get a VM object that is associated with a device.   allocate a new
 * one if needed.
 *
 * => nothing should be locked so that we can sleep here.
 *
 * The last two arguments (off and size) are only used for access checking.
 */
struct uvm_object *
udv_attach(dev_t device, vm_prot_t accessprot, voff_t off, vsize_t size)
{
	struct uvm_device *udv, *lcv;
	paddr_t (*mapfn)(dev_t, off_t, int);
#if NDRM > 0
	struct uvm_object *obj;
#endif

	/*
	 * before we do anything, ensure this device supports mmap
	 */
	mapfn = cdevsw[major(device)].d_mmap;
	if (mapfn == NULL ||
	    mapfn == (paddr_t (*)(dev_t, off_t, int)) enodev ||
	    mapfn == (paddr_t (*)(dev_t, off_t, int)) nullop)
		return(NULL);

	/*
	 * Negative offsets on the object are not allowed.
	 */
	if (off < 0)
		return(NULL);

#if NDRM > 0
	obj = udv_attach_drm(device, accessprot, off, size);
	if (obj)
		return(obj);
#endif

	/*
	 * Check that the specified range of the device allows the
	 * desired protection.
	 * 
	 * XXX clobbers off and size, but nothing else here needs them.
	 */
	while (size != 0) {
		if ((*mapfn)(device, off, accessprot) == -1)
			return (NULL);
		off += PAGE_SIZE; size -= PAGE_SIZE;
	}

	/*
	 * keep looping until we get it
	 */
	for (;;) {
		/*
		 * first, attempt to find it on the main list
		 */
		mtx_enter(&udv_lock);
		LIST_FOREACH(lcv, &udv_list, u_list) {
			if (device == lcv->u_device)
				break;
		}

		/*
		 * got it on main list.  put a hold on it and unlock udv_lock.
		 */
		if (lcv) {
			/*
			 * if someone else has a hold on it, sleep and start
			 * over again. Else, we need take HOLD flag so we
			 * don't have to re-order locking here.
			 */
			if (lcv->u_flags & UVM_DEVICE_HOLD) {
				lcv->u_flags |= UVM_DEVICE_WANTED;
				msleep_nsec(lcv, &udv_lock, PVM | PNORELOCK,
				    "udv_attach", INFSLP);
				continue;
			}

			/* we are now holding it */
			lcv->u_flags |= UVM_DEVICE_HOLD;
			mtx_leave(&udv_lock);

			/*
			 * bump reference count, unhold, return.
			 */
			rw_enter(lcv->u_obj.vmobjlock, RW_WRITE);
			lcv->u_obj.uo_refs++;
			rw_exit(lcv->u_obj.vmobjlock);

			mtx_enter(&udv_lock);
			if (lcv->u_flags & UVM_DEVICE_WANTED)
				wakeup(lcv);
			lcv->u_flags &= ~(UVM_DEVICE_WANTED|UVM_DEVICE_HOLD);
			mtx_leave(&udv_lock);
			return(&lcv->u_obj);
		}

		/*
		 * Did not find it on main list.  Need to allocate a new one.
		 */
		mtx_leave(&udv_lock);
		/* NOTE: we could sleep in the following malloc() */
		udv = malloc(sizeof(*udv), M_TEMP, M_WAITOK);
		uvm_obj_init(&udv->u_obj, &uvm_deviceops, 1);
		mtx_enter(&udv_lock);

		/*
		 * now we have to double check to make sure no one added it
		 * to the list while we were sleeping...
		 */
		LIST_FOREACH(lcv, &udv_list, u_list) {
			if (device == lcv->u_device)
				break;
		}

		/*
		 * did we lose a race to someone else?
		 * free our memory and retry.
		 */
		if (lcv) {
			mtx_leave(&udv_lock);
			uvm_obj_destroy(&udv->u_obj);
			free(udv, M_TEMP, sizeof(*udv));
			continue;
		}

		/*
		 * we have it!   init the data structures, add to list
		 * and return.
		 */
		udv->u_flags = 0;
		udv->u_device = device;
		LIST_INSERT_HEAD(&udv_list, udv, u_list);
		mtx_leave(&udv_lock);
		return(&udv->u_obj);
	}
	/*NOTREACHED*/
}
	
/*
 * udv_reference
 *
 * add a reference to a VM object.   Note that the reference count must
 * already be one (the passed in reference) so there is no chance of the
 * udv being released or locked out here.
 */
static void
udv_reference(struct uvm_object *uobj)
{
	rw_enter(uobj->vmobjlock, RW_WRITE);
	uobj->uo_refs++;
	rw_exit(uobj->vmobjlock);
}

/*
 * udv_detach
 *
 * remove a reference to a VM object.
 */
static void
udv_detach(struct uvm_object *uobj)
{
	struct uvm_device *udv = (struct uvm_device *)uobj;

	/*
	 * loop until done
	 */
again:
	rw_enter(uobj->vmobjlock, RW_WRITE);
	if (uobj->uo_refs > 1) {
		uobj->uo_refs--;
		rw_exit(uobj->vmobjlock);
		return;
	}
	KASSERT(uobj->uo_npages == 0 && RBT_EMPTY(uvm_objtree, &uobj->memt));

	/*
	 * is it being held?   if so, wait until others are done.
	 */
	mtx_enter(&udv_lock);
	if (udv->u_flags & UVM_DEVICE_HOLD) {
		udv->u_flags |= UVM_DEVICE_WANTED;
		rw_exit(uobj->vmobjlock);
		msleep_nsec(udv, &udv_lock, PVM | PNORELOCK, "udv_detach",
		    INFSLP);
		goto again;
	}

	/*
	 * got it!   nuke it now.
	 */
	LIST_REMOVE(udv, u_list);
	if (udv->u_flags & UVM_DEVICE_WANTED)
		wakeup(udv);
	mtx_leave(&udv_lock);
	rw_exit(uobj->vmobjlock);

	uvm_obj_destroy(uobj);
	free(udv, M_TEMP, sizeof(*udv));
}


/*
 * udv_flush
 *
 * flush pages out of a uvm object.   a no-op for devices.
 */
static boolean_t
udv_flush(struct uvm_object *uobj, voff_t start, voff_t stop, int flags)
{

	return(TRUE);
}

/*
 * udv_fault: non-standard fault routine for device "pages"
 *
 * => rather than having a "get" function, we have a fault routine
 *	since we don't return vm_pages we need full control over the
 *	pmap_enter map in
 * => on return, we unlock all fault data structures
 * => flags: PGO_ALLPAGES: get all of the pages
 *	     PGO_LOCKED: fault data structures are locked
 *    XXX: currently PGO_LOCKED is always required ... consider removing
 *	it as a flag
 * => NOTE: vaddr is the VA of pps[0] in ufi->entry, _NOT_ pps[centeridx]
 */
static int
udv_fault(struct uvm_faultinfo *ufi, vaddr_t vaddr, vm_page_t *pps, int npages,
    int centeridx, vm_fault_t fault_type, vm_prot_t access_type, int flags)
{
	struct vm_map_entry *entry = ufi->entry;
	struct uvm_object *uobj = entry->object.uvm_obj;
	struct uvm_device *udv = (struct uvm_device *)uobj;
	vaddr_t curr_va;
	off_t curr_offset;
	paddr_t paddr;
	int lcv, retval;
	dev_t device;
	paddr_t (*mapfn)(dev_t, off_t, int);
	vm_prot_t mapprot;

	KERNEL_ASSERT_LOCKED();

	/*
	 * we do not allow device mappings to be mapped copy-on-write
	 * so we kill any attempt to do so here.
	 */
	if (UVM_ET_ISCOPYONWRITE(entry)) {
		uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, uobj);
		return EACCES;
	}

	/*
	 * get device map function.
	 */
	device = udv->u_device;
	mapfn = cdevsw[major(device)].d_mmap;

	/*
	 * now we must determine the offset in udv to use and the VA to
	 * use for pmap_enter.  note that we always use orig_map's pmap
	 * for pmap_enter (even if we have a submap).   since virtual
	 * addresses in a submap must match the main map, this is ok.
	 */
	/* udv offset = (offset from start of entry) + entry's offset */
	curr_offset = entry->offset + (vaddr - entry->start);
	/* pmap va = vaddr (virtual address of pps[0]) */
	curr_va = vaddr;

	/*
	 * loop over the page range entering in as needed
	 */
	retval = 0;
	for (lcv = 0 ; lcv < npages ; lcv++, curr_offset += PAGE_SIZE,
	    curr_va += PAGE_SIZE) {
		if ((flags & PGO_ALLPAGES) == 0 && lcv != centeridx)
			continue;

		if (pps[lcv] == PGO_DONTCARE)
			continue;

		paddr = (*mapfn)(device, curr_offset, access_type);
		if (paddr == -1) {
			retval = EACCES; /* XXX */
			break;
		}
		mapprot = ufi->entry->protection;
		if (pmap_enter(ufi->orig_map->pmap, curr_va, paddr,
		    mapprot, PMAP_CANFAIL | mapprot) != 0) {
			/*
			 * pmap_enter() didn't have the resource to
			 * enter this mapping.  Unlock everything,
			 * wait for the pagedaemon to free up some
			 * pages, and then tell uvm_fault() to start
			 * the fault again.
			 *
			 * XXX Needs some rethinking for the PGO_ALLPAGES
			 * XXX case.
			 */
			uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap,
			    uobj);

			/* sync what we have so far */
			pmap_update(ufi->orig_map->pmap);      
			uvm_wait("udv_fault");
			return ERESTART;
		}
	}

	uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, uobj);
	pmap_update(ufi->orig_map->pmap);
	return retval;
}
