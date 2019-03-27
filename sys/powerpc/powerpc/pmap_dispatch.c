/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Peter Grehan
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Dispatch MI pmap calls to the appropriate MMU implementation
 * through a previously registered kernel object.
 *
 * Before pmap_bootstrap() can be called, a CPU module must have
 * called pmap_mmu_install(). This may be called multiple times:
 * the highest priority call will be installed as the default
 * MMU handler when pmap_bootstrap() is called.
 *
 * It is required that mutex_init() be called before pmap_bootstrap(), 
 * as the PMAP layer makes extensive use of mutexes.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/lock.h>
#include <sys/kerneldump.h>
#include <sys/ktr.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <machine/dump.h>
#include <machine/md_var.h>
#include <machine/mmuvar.h>
#include <machine/smp.h>

#include "mmu_if.h"

static mmu_def_t	*mmu_def_impl;
static mmu_t		mmu_obj;
static struct mmu_kobj	mmu_kernel_obj;
static struct kobj_ops	mmu_kernel_kops;

/*
 * pmap globals
 */
struct pmap kernel_pmap_store;

vm_offset_t    msgbuf_phys;

vm_offset_t kernel_vm_end;
vm_paddr_t phys_avail[PHYS_AVAIL_SZ];
vm_offset_t virtual_avail;
vm_offset_t virtual_end;

int pmap_bootstrapped;

#ifdef AIM
int
pvo_vaddr_compare(struct pvo_entry *a, struct pvo_entry *b)
{
	if (PVO_VADDR(a) < PVO_VADDR(b))
		return (-1);
	else if (PVO_VADDR(a) > PVO_VADDR(b))
		return (1);
	return (0);
}
RB_GENERATE(pvo_tree, pvo_entry, pvo_plink, pvo_vaddr_compare);
#endif
	

void
pmap_advise(pmap_t pmap, vm_offset_t start, vm_offset_t end, int advice)
{

	CTR5(KTR_PMAP, "%s(%p, %#x, %#x, %d)", __func__, pmap, start, end,
	    advice);
	MMU_ADVISE(mmu_obj, pmap, start, end, advice);
}

void
pmap_clear_modify(vm_page_t m)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, m);
	MMU_CLEAR_MODIFY(mmu_obj, m);
}

void
pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, vm_offset_t dst_addr,
    vm_size_t len, vm_offset_t src_addr)
{

	CTR6(KTR_PMAP, "%s(%p, %p, %#x, %#x, %#x)", __func__, dst_pmap,
	    src_pmap, dst_addr, len, src_addr);
	MMU_COPY(mmu_obj, dst_pmap, src_pmap, dst_addr, len, src_addr);
}

void
pmap_copy_page(vm_page_t src, vm_page_t dst)
{

	CTR3(KTR_PMAP, "%s(%p, %p)", __func__, src, dst);
	MMU_COPY_PAGE(mmu_obj, src, dst);
}

void
pmap_copy_pages(vm_page_t ma[], vm_offset_t a_offset, vm_page_t mb[],
    vm_offset_t b_offset, int xfersize)
{

	CTR6(KTR_PMAP, "%s(%p, %#x, %p, %#x, %#x)", __func__, ma,
	    a_offset, mb, b_offset, xfersize);
	MMU_COPY_PAGES(mmu_obj, ma, a_offset, mb, b_offset, xfersize);
}

int
pmap_enter(pmap_t pmap, vm_offset_t va, vm_page_t p, vm_prot_t prot,
    u_int flags, int8_t psind)
{

	CTR6(KTR_PMAP, "pmap_enter(%p, %#x, %p, %#x, %x, %d)", pmap, va,
	    p, prot, flags, psind);
	return (MMU_ENTER(mmu_obj, pmap, va, p, prot, flags, psind));
}

void
pmap_enter_object(pmap_t pmap, vm_offset_t start, vm_offset_t end,
    vm_page_t m_start, vm_prot_t prot)
{

	CTR6(KTR_PMAP, "%s(%p, %#x, %#x, %p, %#x)", __func__, pmap, start,
	    end, m_start, prot);
	MMU_ENTER_OBJECT(mmu_obj, pmap, start, end, m_start, prot);
}

void
pmap_enter_quick(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot)
{

	CTR5(KTR_PMAP, "%s(%p, %#x, %p, %#x)", __func__, pmap, va, m, prot);
	MMU_ENTER_QUICK(mmu_obj, pmap, va, m, prot);
}

vm_paddr_t
pmap_extract(pmap_t pmap, vm_offset_t va)
{

	CTR3(KTR_PMAP, "%s(%p, %#x)", __func__, pmap, va);
	return (MMU_EXTRACT(mmu_obj, pmap, va));
}

vm_page_t
pmap_extract_and_hold(pmap_t pmap, vm_offset_t va, vm_prot_t prot)
{

	CTR4(KTR_PMAP, "%s(%p, %#x, %#x)", __func__, pmap, va, prot);
	return (MMU_EXTRACT_AND_HOLD(mmu_obj, pmap, va, prot));
}

void
pmap_growkernel(vm_offset_t va)
{

	CTR2(KTR_PMAP, "%s(%#x)", __func__, va);
	MMU_GROWKERNEL(mmu_obj, va);
}

void
pmap_init(void)
{

	CTR1(KTR_PMAP, "%s()", __func__);
	MMU_INIT(mmu_obj);
}

boolean_t
pmap_is_modified(vm_page_t m)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, m);
	return (MMU_IS_MODIFIED(mmu_obj, m));
}

boolean_t
pmap_is_prefaultable(pmap_t pmap, vm_offset_t va)
{

	CTR3(KTR_PMAP, "%s(%p, %#x)", __func__, pmap, va);
	return (MMU_IS_PREFAULTABLE(mmu_obj, pmap, va));
}

boolean_t
pmap_is_referenced(vm_page_t m)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, m);
	return (MMU_IS_REFERENCED(mmu_obj, m));
}

boolean_t
pmap_ts_referenced(vm_page_t m)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, m);
	return (MMU_TS_REFERENCED(mmu_obj, m));
}

vm_offset_t
pmap_map(vm_offset_t *virt, vm_paddr_t start, vm_paddr_t end, int prot)
{

	CTR5(KTR_PMAP, "%s(%p, %#x, %#x, %#x)", __func__, virt, start, end,
	    prot);
	return (MMU_MAP(mmu_obj, virt, start, end, prot));
}

void
pmap_object_init_pt(pmap_t pmap, vm_offset_t addr, vm_object_t object,
    vm_pindex_t pindex, vm_size_t size)
{

	CTR6(KTR_PMAP, "%s(%p, %#x, %p, %u, %#x)", __func__, pmap, addr,
	    object, pindex, size);
	MMU_OBJECT_INIT_PT(mmu_obj, pmap, addr, object, pindex, size);
}

boolean_t
pmap_page_exists_quick(pmap_t pmap, vm_page_t m)
{

	CTR3(KTR_PMAP, "%s(%p, %p)", __func__, pmap, m);
	return (MMU_PAGE_EXISTS_QUICK(mmu_obj, pmap, m));
}

void
pmap_page_init(vm_page_t m)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, m);
	MMU_PAGE_INIT(mmu_obj, m);
}

int
pmap_page_wired_mappings(vm_page_t m)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, m);
	return (MMU_PAGE_WIRED_MAPPINGS(mmu_obj, m));
}

int
pmap_pinit(pmap_t pmap)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, pmap);
	MMU_PINIT(mmu_obj, pmap);
	return (1);
}

void
pmap_pinit0(pmap_t pmap)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, pmap);
	MMU_PINIT0(mmu_obj, pmap);
}

void
pmap_protect(pmap_t pmap, vm_offset_t start, vm_offset_t end, vm_prot_t prot)
{

	CTR5(KTR_PMAP, "%s(%p, %#x, %#x, %#x)", __func__, pmap, start, end,
	    prot);
	MMU_PROTECT(mmu_obj, pmap, start, end, prot);
}

void
pmap_qenter(vm_offset_t start, vm_page_t *m, int count)
{

	CTR4(KTR_PMAP, "%s(%#x, %p, %d)", __func__, start, m, count);
	MMU_QENTER(mmu_obj, start, m, count);
}

void
pmap_qremove(vm_offset_t start, int count)
{

	CTR3(KTR_PMAP, "%s(%#x, %d)", __func__, start, count);
	MMU_QREMOVE(mmu_obj, start, count);
}

void
pmap_release(pmap_t pmap)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, pmap);
	MMU_RELEASE(mmu_obj, pmap);
}

void
pmap_remove(pmap_t pmap, vm_offset_t start, vm_offset_t end)
{

	CTR4(KTR_PMAP, "%s(%p, %#x, %#x)", __func__, pmap, start, end);
	MMU_REMOVE(mmu_obj, pmap, start, end);
}

void
pmap_remove_all(vm_page_t m)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, m);
	MMU_REMOVE_ALL(mmu_obj, m);
}

void
pmap_remove_pages(pmap_t pmap)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, pmap);
	MMU_REMOVE_PAGES(mmu_obj, pmap);
}

void
pmap_remove_write(vm_page_t m)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, m);
	MMU_REMOVE_WRITE(mmu_obj, m);
}

void
pmap_unwire(pmap_t pmap, vm_offset_t start, vm_offset_t end)
{

	CTR4(KTR_PMAP, "%s(%p, %#x, %#x)", __func__, pmap, start, end);
	MMU_UNWIRE(mmu_obj, pmap, start, end);
}

void
pmap_zero_page(vm_page_t m)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, m);
	MMU_ZERO_PAGE(mmu_obj, m);
}

void
pmap_zero_page_area(vm_page_t m, int off, int size)
{

	CTR4(KTR_PMAP, "%s(%p, %d, %d)", __func__, m, off, size);
	MMU_ZERO_PAGE_AREA(mmu_obj, m, off, size);
}

int
pmap_mincore(pmap_t pmap, vm_offset_t addr, vm_paddr_t *locked_pa)
{

	CTR3(KTR_PMAP, "%s(%p, %#x)", __func__, pmap, addr);
	return (MMU_MINCORE(mmu_obj, pmap, addr, locked_pa));
}

void
pmap_activate(struct thread *td)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, td);
	MMU_ACTIVATE(mmu_obj, td);
}

void
pmap_deactivate(struct thread *td)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, td);
	MMU_DEACTIVATE(mmu_obj, td);
}

/*
 *	Increase the starting virtual address of the given mapping if a
 *	different alignment might result in more superpage mappings.
 */
void
pmap_align_superpage(vm_object_t object, vm_ooffset_t offset,
    vm_offset_t *addr, vm_size_t size)
{

	CTR5(KTR_PMAP, "%s(%p, %#x, %p, %#x)", __func__, object, offset, addr,
	    size);
	MMU_ALIGN_SUPERPAGE(mmu_obj, object, offset, addr, size);
}

/*
 * Routines used in machine-dependent code
 */
void
pmap_bootstrap(vm_offset_t start, vm_offset_t end)
{
	mmu_obj = &mmu_kernel_obj;

	/*
	 * Take care of compiling the selected class, and
	 * then statically initialise the MMU object
	 */
	kobj_class_compile_static(mmu_def_impl, &mmu_kernel_kops);
	kobj_init_static((kobj_t)mmu_obj, mmu_def_impl);

	MMU_BOOTSTRAP(mmu_obj, start, end);
}

void
pmap_cpu_bootstrap(int ap)
{
	/*
	 * No KTR here because our console probably doesn't work yet
	 */

	return (MMU_CPU_BOOTSTRAP(mmu_obj, ap));
}

void *
pmap_mapdev(vm_paddr_t pa, vm_size_t size)
{

	CTR3(KTR_PMAP, "%s(%#x, %#x)", __func__, pa, size);
	return (MMU_MAPDEV(mmu_obj, pa, size));
}

void *
pmap_mapdev_attr(vm_paddr_t pa, vm_size_t size, vm_memattr_t attr)
{

	CTR4(KTR_PMAP, "%s(%#x, %#x, %#x)", __func__, pa, size, attr);
	return (MMU_MAPDEV_ATTR(mmu_obj, pa, size, attr));
}

void
pmap_page_set_memattr(vm_page_t m, vm_memattr_t ma)
{

	CTR3(KTR_PMAP, "%s(%p, %#x)", __func__, m, ma);
	return (MMU_PAGE_SET_MEMATTR(mmu_obj, m, ma));
}

void
pmap_unmapdev(vm_offset_t va, vm_size_t size)
{

	CTR3(KTR_PMAP, "%s(%#x, %#x)", __func__, va, size);
	MMU_UNMAPDEV(mmu_obj, va, size);
}

vm_paddr_t
pmap_kextract(vm_offset_t va)
{

	CTR2(KTR_PMAP, "%s(%#x)", __func__, va);
	return (MMU_KEXTRACT(mmu_obj, va));
}

void
pmap_kenter(vm_offset_t va, vm_paddr_t pa)
{

	CTR3(KTR_PMAP, "%s(%#x, %#x)", __func__, va, pa);
	MMU_KENTER(mmu_obj, va, pa);
}

void
pmap_kenter_attr(vm_offset_t va, vm_paddr_t pa, vm_memattr_t ma)
{

	CTR4(KTR_PMAP, "%s(%#x, %#x, %#x)", __func__, va, pa, ma);
	MMU_KENTER_ATTR(mmu_obj, va, pa, ma);
}

void
pmap_kremove(vm_offset_t va)
{

	CTR2(KTR_PMAP, "%s(%#x)", __func__, va);
	return (MMU_KREMOVE(mmu_obj, va));
}

int
pmap_map_user_ptr(pmap_t pm, volatile const void *uaddr, void **kaddr,
    size_t ulen, size_t *klen)
{

	CTR2(KTR_PMAP, "%s(%p)", __func__, uaddr);
	return (MMU_MAP_USER_PTR(mmu_obj, pm, uaddr, kaddr, ulen, klen));
}

int
pmap_decode_kernel_ptr(vm_offset_t addr, int *is_user, vm_offset_t *decoded)
{

	CTR2(KTR_PMAP, "%s(%#jx)", __func__, (uintmax_t)addr);
	return (MMU_DECODE_KERNEL_PTR(mmu_obj, addr, is_user, decoded));
}

boolean_t
pmap_dev_direct_mapped(vm_paddr_t pa, vm_size_t size)
{

	CTR3(KTR_PMAP, "%s(%#x, %#x)", __func__, pa, size);
	return (MMU_DEV_DIRECT_MAPPED(mmu_obj, pa, size));
}

void
pmap_sync_icache(pmap_t pm, vm_offset_t va, vm_size_t sz)
{
 
	CTR4(KTR_PMAP, "%s(%p, %#x, %#x)", __func__, pm, va, sz);
	return (MMU_SYNC_ICACHE(mmu_obj, pm, va, sz));
}

void
dumpsys_map_chunk(vm_paddr_t pa, size_t sz, void **va)
{

	CTR4(KTR_PMAP, "%s(%#jx, %#zx, %p)", __func__, (uintmax_t)pa, sz, va);
	return (MMU_DUMPSYS_MAP(mmu_obj, pa, sz, va));
}

void
dumpsys_unmap_chunk(vm_paddr_t pa, size_t sz, void *va)
{

	CTR4(KTR_PMAP, "%s(%#jx, %#zx, %p)", __func__, (uintmax_t)pa, sz, va);
	return (MMU_DUMPSYS_UNMAP(mmu_obj, pa, sz, va));
}

void
dumpsys_pa_init(void)
{

	CTR1(KTR_PMAP, "%s()", __func__);
	return (MMU_SCAN_INIT(mmu_obj));
}

vm_offset_t
pmap_quick_enter_page(vm_page_t m)
{
	CTR2(KTR_PMAP, "%s(%p)", __func__, m);
	return (MMU_QUICK_ENTER_PAGE(mmu_obj, m));
}

void
pmap_quick_remove_page(vm_offset_t addr)
{
	CTR2(KTR_PMAP, "%s(%#x)", __func__, addr);
	MMU_QUICK_REMOVE_PAGE(mmu_obj, addr);
}

int
pmap_change_attr(vm_offset_t addr, vm_size_t size, vm_memattr_t mode)
{
	CTR4(KTR_PMAP, "%s(%#x, %#zx, %d)", __func__, addr, size, mode);
	return (MMU_CHANGE_ATTR(mmu_obj, addr, size, mode));
}

/*
 * MMU install routines. Highest priority wins, equal priority also
 * overrides allowing last-set to win.
 */
SET_DECLARE(mmu_set, mmu_def_t);

boolean_t
pmap_mmu_install(char *name, int prio)
{
	mmu_def_t	**mmupp, *mmup;
	static int	curr_prio = 0;

	/*
	 * Try and locate the MMU kobj corresponding to the name
	 */
	SET_FOREACH(mmupp, mmu_set) {
		mmup = *mmupp;

		if (mmup->name &&
		    !strcmp(mmup->name, name) &&
		    (prio >= curr_prio || mmu_def_impl == NULL)) {
			curr_prio = prio;
			mmu_def_impl = mmup;
			return (TRUE);
		}
	}

	return (FALSE);
}

int unmapped_buf_allowed;

boolean_t
pmap_is_valid_memattr(pmap_t pmap __unused, vm_memattr_t mode)
{

	switch (mode) {
	case VM_MEMATTR_DEFAULT:
	case VM_MEMATTR_UNCACHEABLE:
	case VM_MEMATTR_CACHEABLE:
	case VM_MEMATTR_WRITE_COMBINING:
	case VM_MEMATTR_WRITE_BACK:
	case VM_MEMATTR_WRITE_THROUGH:
	case VM_MEMATTR_PREFETCHABLE:
		return (TRUE);
	default:
		return (FALSE);
	}
}
