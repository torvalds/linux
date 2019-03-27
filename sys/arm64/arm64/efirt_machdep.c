/*-
 * Copyright (c) 2004 Marcel Moolenaar
 * Copyright (c) 2001 Doug Rabson
 * Copyright (c) 2016 The FreeBSD Foundation
 * Copyright (c) 2017 Andrew Turner
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <sys/efi.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/systm.h>
#include <sys/vmmeter.h>

#include <machine/metadata.h>
#include <machine/pcb.h>
#include <machine/pte.h>
#include <machine/vfp.h>
#include <machine/vmparam.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

static vm_object_t obj_1t1_pt;
static vm_page_t efi_l0_page;
static pd_entry_t *efi_l0;
static vm_pindex_t efi_1t1_idx;

void
efi_destroy_1t1_map(void)
{
	vm_page_t m;

	if (obj_1t1_pt != NULL) {
		VM_OBJECT_RLOCK(obj_1t1_pt);
		TAILQ_FOREACH(m, &obj_1t1_pt->memq, listq)
			m->wire_count = 0;
		vm_wire_sub(obj_1t1_pt->resident_page_count);
		VM_OBJECT_RUNLOCK(obj_1t1_pt);
		vm_object_deallocate(obj_1t1_pt);
	}

	obj_1t1_pt = NULL;
	efi_l0 = NULL;
	efi_l0_page = NULL;
}

static vm_page_t
efi_1t1_page(void)
{

	return (vm_page_grab(obj_1t1_pt, efi_1t1_idx++, VM_ALLOC_NOBUSY |
	    VM_ALLOC_WIRED | VM_ALLOC_ZERO));
}

static pt_entry_t *
efi_1t1_l3(vm_offset_t va)
{
	pd_entry_t *l0, *l1, *l2;
	pt_entry_t *l3;
	vm_pindex_t l0_idx, l1_idx, l2_idx;
	vm_page_t m;
	vm_paddr_t mphys;

	l0_idx = pmap_l0_index(va);
	l0 = &efi_l0[l0_idx];
	if (*l0 == 0) {
		m = efi_1t1_page();
		mphys = VM_PAGE_TO_PHYS(m);
		*l0 = mphys | L0_TABLE;
	} else {
		mphys = *l0 & ~ATTR_MASK;
	}

	l1 = (pd_entry_t *)PHYS_TO_DMAP(mphys);
	l1_idx = pmap_l1_index(va);
	l1 += l1_idx;
	if (*l1 == 0) {
		m = efi_1t1_page();
		mphys = VM_PAGE_TO_PHYS(m);
		*l1 = mphys | L1_TABLE;
	} else {
		mphys = *l1 & ~ATTR_MASK;
	}

	l2 = (pd_entry_t *)PHYS_TO_DMAP(mphys);
	l2_idx = pmap_l2_index(va);
	l2 += l2_idx;
	if (*l2 == 0) {
		m = efi_1t1_page();
		mphys = VM_PAGE_TO_PHYS(m);
		*l2 = mphys | L2_TABLE;
	} else {
		mphys = *l2 & ~ATTR_MASK;
	}

	l3 = (pt_entry_t *)PHYS_TO_DMAP(mphys);
	l3 += pmap_l3_index(va);
	KASSERT(*l3 == 0, ("%s: Already mapped: va %#jx *pt %#jx", __func__,
	    va, *l3));

	return (l3);
}

/*
 * Map a physical address from EFI runtime space into KVA space.  Returns 0 to
 * indicate a failed mapping so that the caller may handle error.
 */
vm_offset_t
efi_phys_to_kva(vm_paddr_t paddr)
{

	if (!PHYS_IN_DMAP(paddr))
		return (0);
	return (PHYS_TO_DMAP(paddr));
}

/*
 * Create the 1:1 virtual to physical map for EFI
 */
bool
efi_create_1t1_map(struct efi_md *map, int ndesc, int descsz)
{
	struct efi_md *p;
	pt_entry_t *l3, l3_attr;
	vm_offset_t va;
	uint64_t idx;
	int i, mode;

	obj_1t1_pt = vm_pager_allocate(OBJT_PHYS, NULL, L0_ENTRIES +
	    L0_ENTRIES * Ln_ENTRIES + L0_ENTRIES * Ln_ENTRIES * Ln_ENTRIES +
	    L0_ENTRIES * Ln_ENTRIES * Ln_ENTRIES * Ln_ENTRIES,
	    VM_PROT_ALL, 0, NULL);
	VM_OBJECT_WLOCK(obj_1t1_pt);
	efi_1t1_idx = 0;
	efi_l0_page = efi_1t1_page();
	VM_OBJECT_WUNLOCK(obj_1t1_pt);
	efi_l0 = (pd_entry_t *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(efi_l0_page));
	bzero(efi_l0, L0_ENTRIES * sizeof(*efi_l0));

	for (i = 0, p = map; i < ndesc; i++, p = efi_next_descriptor(p,
	    descsz)) {
		if ((p->md_attr & EFI_MD_ATTR_RT) == 0)
			continue;
		if (p->md_virt != NULL && (uint64_t)p->md_virt != p->md_phys) {
			if (bootverbose)
				printf("EFI Runtime entry %d is mapped\n", i);
			goto fail;
		}
		if ((p->md_phys & EFI_PAGE_MASK) != 0) {
			if (bootverbose)
				printf("EFI Runtime entry %d is not aligned\n",
				    i);
			goto fail;
		}
		if (p->md_phys + p->md_pages * EFI_PAGE_SIZE < p->md_phys ||
		    p->md_phys + p->md_pages * EFI_PAGE_SIZE >=
		    VM_MAXUSER_ADDRESS) {
			printf("EFI Runtime entry %d is not in mappable for RT:"
			    "base %#016jx %#jx pages\n",
			    i, (uintmax_t)p->md_phys,
			    (uintmax_t)p->md_pages);
			goto fail;
		}
		if ((p->md_attr & EFI_MD_ATTR_WB) != 0)
			mode = VM_MEMATTR_WRITE_BACK;
		else if ((p->md_attr & EFI_MD_ATTR_WT) != 0)
			mode = VM_MEMATTR_WRITE_THROUGH;
		else if ((p->md_attr & EFI_MD_ATTR_WC) != 0)
			mode = VM_MEMATTR_WRITE_COMBINING;
		else if ((p->md_attr & EFI_MD_ATTR_UC) != 0)
			mode = VM_MEMATTR_DEVICE;
		else {
			if (bootverbose)
				printf("EFI Runtime entry %d mapping "
				    "attributes unsupported\n", i);
			mode = VM_MEMATTR_UNCACHEABLE;
		}

		printf("MAP %lx mode %x pages %lu\n", p->md_phys, mode, p->md_pages);

		l3_attr = ATTR_DEFAULT | ATTR_IDX(mode) | ATTR_AP(ATTR_AP_RW) |
		    L3_PAGE;
		if (mode == VM_MEMATTR_DEVICE)
			l3_attr |= ATTR_UXN | ATTR_PXN;

		VM_OBJECT_WLOCK(obj_1t1_pt);
		for (va = p->md_phys, idx = 0; idx < p->md_pages; idx++,
		    va += PAGE_SIZE) {
			l3 = efi_1t1_l3(va);
			*l3 = va | l3_attr;
		}
		VM_OBJECT_WUNLOCK(obj_1t1_pt);
	}

	return (true);
fail:
	efi_destroy_1t1_map();
	return (false);
}

int
efi_arch_enter(void)
{

	__asm __volatile(
	    "msr ttbr0_el1, %0	\n"
	    "dsb  ishst		\n"
	    "tlbi vmalle1is	\n"
	    "dsb  ish		\n"
	    "isb		\n"
	     : : "r"(VM_PAGE_TO_PHYS(efi_l0_page)));

	return (0);
}

void
efi_arch_leave(void)
{
	struct thread *td;

	td = curthread;
	__asm __volatile(
	    "msr ttbr0_el1, %0	\n"
	    "dsb  ishst		\n"
	    "tlbi vmalle1is	\n"
	    "dsb  ish		\n"
	    "isb		\n"
	     : : "r"(td->td_proc->p_md.md_l0addr));
}

int
efi_rt_arch_call(struct efirt_callinfo *ec)
{

	panic("not implemented");
}
