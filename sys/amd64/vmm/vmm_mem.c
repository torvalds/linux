/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sglist.h>
#include <sys/lock.h>
#include <sys/rwlock.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

#include <machine/md_var.h>

#include "vmm_mem.h"

int
vmm_mem_init(void)
{

	return (0);
}

vm_object_t
vmm_mmio_alloc(struct vmspace *vmspace, vm_paddr_t gpa, size_t len,
	       vm_paddr_t hpa)
{
	int error;
	vm_object_t obj;
	struct sglist *sg;

	sg = sglist_alloc(1, M_WAITOK);
	error = sglist_append_phys(sg, hpa, len);
	KASSERT(error == 0, ("error %d appending physaddr to sglist", error));

	obj = vm_pager_allocate(OBJT_SG, sg, len, VM_PROT_RW, 0, NULL);
	if (obj != NULL) {
		/*
		 * VT-x ignores the MTRR settings when figuring out the
		 * memory type for translations obtained through EPT.
		 *
		 * Therefore we explicitly force the pages provided by
		 * this object to be mapped as uncacheable.
		 */
		VM_OBJECT_WLOCK(obj);
		error = vm_object_set_memattr(obj, VM_MEMATTR_UNCACHEABLE);
		VM_OBJECT_WUNLOCK(obj);
		if (error != KERN_SUCCESS) {
			panic("vmm_mmio_alloc: vm_object_set_memattr error %d",
				error);
		}
		error = vm_map_find(&vmspace->vm_map, obj, 0, &gpa, len, 0,
				    VMFS_NO_SPACE, VM_PROT_RW, VM_PROT_RW, 0);
		if (error != KERN_SUCCESS) {
			vm_object_deallocate(obj);
			obj = NULL;
		}
	}

	/*
	 * Drop the reference on the sglist.
	 *
	 * If the scatter/gather object was successfully allocated then it
	 * has incremented the reference count on the sglist. Dropping the
	 * initial reference count ensures that the sglist will be freed
	 * when the object is deallocated.
	 * 
	 * If the object could not be allocated then we end up freeing the
	 * sglist.
	 */
	sglist_free(sg);

	return (obj);
}

void
vmm_mmio_free(struct vmspace *vmspace, vm_paddr_t gpa, size_t len)
{

	vm_map_remove(&vmspace->vm_map, gpa, gpa + len);
}

vm_paddr_t
vmm_mem_maxaddr(void)
{

	return (ptoa(Maxmem));
}
