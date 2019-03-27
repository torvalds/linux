/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2016 Matthew Macy (mmacy@mattmacy.io)
 * Copyright (c) 2017 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/proc.h>
#include <sys/sched.h>

#include <machine/bus.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/vm_radix.h>
#include <vm/vm_reserv.h>
#include <vm/vm_extern.h>

#include <vm/uma.h>
#include <vm/uma_int.h>

#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/preempt.h>
#include <linux/fs.h>

void
si_meminfo(struct sysinfo *si)
{
	si->totalram = physmem;
	si->totalhigh = 0;
	si->mem_unit = PAGE_SIZE;
}

void *
linux_page_address(struct page *page)
{

	if (page->object != kmem_object && page->object != kernel_object) {
		return (PMAP_HAS_DMAP ?
		    ((void *)(uintptr_t)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(page))) :
		    NULL);
	}
	return ((void *)(uintptr_t)(VM_MIN_KERNEL_ADDRESS +
	    IDX_TO_OFF(page->pindex)));
}

vm_page_t
linux_alloc_pages(gfp_t flags, unsigned int order)
{
	vm_page_t page;

	if (PMAP_HAS_DMAP) {
		unsigned long npages = 1UL << order;
		int req = (flags & M_ZERO) ? (VM_ALLOC_ZERO | VM_ALLOC_NOOBJ |
		    VM_ALLOC_NORMAL) : (VM_ALLOC_NOOBJ | VM_ALLOC_NORMAL);

		if (order == 0 && (flags & GFP_DMA32) == 0) {
			page = vm_page_alloc(NULL, 0, req);
			if (page == NULL)
				return (NULL);
		} else {
			vm_paddr_t pmax = (flags & GFP_DMA32) ?
			    BUS_SPACE_MAXADDR_32BIT : BUS_SPACE_MAXADDR;
		retry:
			page = vm_page_alloc_contig(NULL, 0, req,
			    npages, 0, pmax, PAGE_SIZE, 0, VM_MEMATTR_DEFAULT);

			if (page == NULL) {
				if (flags & M_WAITOK) {
					if (!vm_page_reclaim_contig(req,
					    npages, 0, pmax, PAGE_SIZE, 0)) {
						vm_wait(NULL);
					}
					flags &= ~M_WAITOK;
					goto retry;
				}
				return (NULL);
			}
		}
		if (flags & M_ZERO) {
			unsigned long x;

			for (x = 0; x != npages; x++) {
				vm_page_t pgo = page + x;

				if ((pgo->flags & PG_ZERO) == 0)
					pmap_zero_page(pgo);
			}
		}
	} else {
		vm_offset_t vaddr;

		vaddr = linux_alloc_kmem(flags, order);
		if (vaddr == 0)
			return (NULL);

		page = PHYS_TO_VM_PAGE(vtophys((void *)vaddr));

		KASSERT(vaddr == (vm_offset_t)page_address(page),
		    ("Page address mismatch"));
	}

	return (page);
}

void
linux_free_pages(vm_page_t page, unsigned int order)
{
	if (PMAP_HAS_DMAP) {
		unsigned long npages = 1UL << order;
		unsigned long x;

		for (x = 0; x != npages; x++) {
			vm_page_t pgo = page + x;

			vm_page_lock(pgo);
			vm_page_free(pgo);
			vm_page_unlock(pgo);
		}
	} else {
		vm_offset_t vaddr;

		vaddr = (vm_offset_t)page_address(page);

		linux_free_kmem(vaddr, order);
	}
}

vm_offset_t
linux_alloc_kmem(gfp_t flags, unsigned int order)
{
	size_t size = ((size_t)PAGE_SIZE) << order;
	vm_offset_t addr;

	if ((flags & GFP_DMA32) == 0) {
		addr = kmem_malloc(size, flags & GFP_NATIVE_MASK);
	} else {
		addr = kmem_alloc_contig(size, flags & GFP_NATIVE_MASK, 0,
		    BUS_SPACE_MAXADDR_32BIT, PAGE_SIZE, 0, VM_MEMATTR_DEFAULT);
	}
	return (addr);
}

void
linux_free_kmem(vm_offset_t addr, unsigned int order)
{
	size_t size = ((size_t)PAGE_SIZE) << order;

	kmem_free(addr, size);
}

static int
linux_get_user_pages_internal(vm_map_t map, unsigned long start, int nr_pages,
    int write, struct page **pages)
{
	vm_prot_t prot;
	size_t len;
	int count;
	int i;

	prot = write ? (VM_PROT_READ | VM_PROT_WRITE) : VM_PROT_READ;
	len = ((size_t)nr_pages) << PAGE_SHIFT;
	count = vm_fault_quick_hold_pages(map, start, len, prot, pages, nr_pages);
	if (count == -1)
		return (-EFAULT);

	for (i = 0; i != nr_pages; i++) {
		struct page *pg = pages[i];

		vm_page_lock(pg);
		vm_page_wire(pg);
		vm_page_unhold(pg);
		vm_page_unlock(pg);
	}
	return (nr_pages);
}

int
__get_user_pages_fast(unsigned long start, int nr_pages, int write,
    struct page **pages)
{
	vm_map_t map;
	vm_page_t *mp;
	vm_offset_t va;
	vm_offset_t end;
	vm_prot_t prot;
	int count;

	if (nr_pages == 0 || in_interrupt())
		return (0);

	MPASS(pages != NULL);
	va = start;
	map = &curthread->td_proc->p_vmspace->vm_map;
	end = start + (((size_t)nr_pages) << PAGE_SHIFT);
	if (start < vm_map_min(map) || end > vm_map_max(map))
		return (-EINVAL);
	prot = write ? (VM_PROT_READ | VM_PROT_WRITE) : VM_PROT_READ;
	for (count = 0, mp = pages, va = start; va < end;
	    mp++, va += PAGE_SIZE, count++) {
		*mp = pmap_extract_and_hold(map->pmap, va, prot);
		if (*mp == NULL)
			break;

		vm_page_lock(*mp);
		vm_page_wire(*mp);
		vm_page_unhold(*mp);
		vm_page_unlock(*mp);

		if ((prot & VM_PROT_WRITE) != 0 &&
		    (*mp)->dirty != VM_PAGE_BITS_ALL) {
			/*
			 * Explicitly dirty the physical page.  Otherwise, the
			 * caller's changes may go unnoticed because they are
			 * performed through an unmanaged mapping or by a DMA
			 * operation.
			 *
			 * The object lock is not held here.
			 * See vm_page_clear_dirty_mask().
			 */
			vm_page_dirty(*mp);
		}
	}
	return (count);
}

long
get_user_pages_remote(struct task_struct *task, struct mm_struct *mm,
    unsigned long start, unsigned long nr_pages, int gup_flags,
    struct page **pages, struct vm_area_struct **vmas)
{
	vm_map_t map;

	map = &task->task_thread->td_proc->p_vmspace->vm_map;
	return (linux_get_user_pages_internal(map, start, nr_pages,
	    !!(gup_flags & FOLL_WRITE), pages));
}

long
get_user_pages(unsigned long start, unsigned long nr_pages, int gup_flags,
    struct page **pages, struct vm_area_struct **vmas)
{
	vm_map_t map;

	map = &curthread->td_proc->p_vmspace->vm_map;
	return (linux_get_user_pages_internal(map, start, nr_pages,
	    !!(gup_flags & FOLL_WRITE), pages));
}

int
is_vmalloc_addr(const void *addr)
{
	return (vtoslab((vm_offset_t)addr & ~UMA_SLAB_MASK) != NULL);
}

struct page *
linux_shmem_read_mapping_page_gfp(vm_object_t obj, int pindex, gfp_t gfp)
{
	vm_page_t page;
	int rv;

	if ((gfp & GFP_NOWAIT) != 0)
		panic("GFP_NOWAIT is unimplemented");

	VM_OBJECT_WLOCK(obj);
	page = vm_page_grab(obj, pindex, VM_ALLOC_NORMAL | VM_ALLOC_NOBUSY |
	    VM_ALLOC_WIRED);
	if (page->valid != VM_PAGE_BITS_ALL) {
		vm_page_xbusy(page);
		if (vm_pager_has_page(obj, pindex, NULL, NULL)) {
			rv = vm_pager_get_pages(obj, &page, 1, NULL, NULL);
			if (rv != VM_PAGER_OK) {
				vm_page_lock(page);
				vm_page_unwire(page, PQ_NONE);
				vm_page_free(page);
				vm_page_unlock(page);
				VM_OBJECT_WUNLOCK(obj);
				return (ERR_PTR(-EINVAL));
			}
			MPASS(page->valid == VM_PAGE_BITS_ALL);
		} else {
			pmap_zero_page(page);
			page->valid = VM_PAGE_BITS_ALL;
			page->dirty = 0;
		}
		vm_page_xunbusy(page);
	}
	VM_OBJECT_WUNLOCK(obj);
	return (page);
}

struct linux_file *
linux_shmem_file_setup(const char *name, loff_t size, unsigned long flags)
{
	struct fileobj {
		struct linux_file file __aligned(sizeof(void *));
		struct vnode vnode __aligned(sizeof(void *));
	};
	struct fileobj *fileobj;
	struct linux_file *filp;
	struct vnode *vp;
	int error;

	fileobj = kzalloc(sizeof(*fileobj), GFP_KERNEL);
	if (fileobj == NULL) {
		error = -ENOMEM;
		goto err_0;
	}
	filp = &fileobj->file;
	vp = &fileobj->vnode;

	filp->f_count = 1;
	filp->f_vnode = vp;
	filp->f_shmem = vm_pager_allocate(OBJT_DEFAULT, NULL, size,
	    VM_PROT_READ | VM_PROT_WRITE, 0, curthread->td_ucred);
	if (filp->f_shmem == NULL) {
		error = -ENOMEM;
		goto err_1;
	}
	return (filp);
err_1:
	kfree(filp);
err_0:
	return (ERR_PTR(error));
}

static vm_ooffset_t
linux_invalidate_mapping_pages_sub(vm_object_t obj, vm_pindex_t start,
    vm_pindex_t end, int flags)
{
	int start_count, end_count;

	VM_OBJECT_WLOCK(obj);
	start_count = obj->resident_page_count;
	vm_object_page_remove(obj, start, end, flags);
	end_count = obj->resident_page_count;
	VM_OBJECT_WUNLOCK(obj);
	return (start_count - end_count);
}

unsigned long
linux_invalidate_mapping_pages(vm_object_t obj, pgoff_t start, pgoff_t end)
{

	return (linux_invalidate_mapping_pages_sub(obj, start, end, OBJPR_CLEANONLY));
}

void
linux_shmem_truncate_range(vm_object_t obj, loff_t lstart, loff_t lend)
{
	vm_pindex_t start = OFF_TO_IDX(lstart + PAGE_SIZE - 1);
	vm_pindex_t end = OFF_TO_IDX(lend + 1);

	(void) linux_invalidate_mapping_pages_sub(obj, start, end, 0);
}
