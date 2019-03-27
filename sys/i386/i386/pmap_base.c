/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1994 John S. Dyson
 * All rights reserved.
 * Copyright (c) 1994 David Greenman
 * All rights reserved.
 * Copyright (c) 2005-2010 Alan L. Cox <alc@cs.rice.edu>
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and William Jolitz of UUNET Technologies Inc.
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
 *	from:	@(#)pmap.c	7.7 (Berkeley)	5/12/91
 */
/*-
 * Copyright (c) 2003 Networks Associates Technology, Inc.
 * All rights reserved.
 * Copyright (c) 2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Jake Burkholder,
 * Safeport Network Services, and Network Associates Laboratories, the
 * Security Research Division of Network Associates, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
 *
 * Portions of this software were developed by
 * Konstantin Belousov <kib@FreeBSD.org> under sponsorship from
 * the FreeBSD Foundation.
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

#include "opt_apic.h"
#include "opt_cpu.h"
#include "opt_pmap.h"
#include "opt_smp.h"
#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vmmeter.h>
#include <sys/sysctl.h>
#include <machine/bootinfo.h>
#include <machine/cpu.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#ifdef DEV_APIC
#include <sys/bus.h>
#include <machine/intr_machdep.h>
#include <x86/apicvar.h>
#endif
#include <x86/ifunc.h>

static SYSCTL_NODE(_vm, OID_AUTO, pmap, CTLFLAG_RD, 0, "VM/pmap parameters");

#include <machine/vmparam.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>
#include <machine/pmap_base.h>

vm_offset_t virtual_avail;	/* VA of first avail page (after kernel bss) */
vm_offset_t virtual_end;	/* VA of last avail page (end of kernel AS) */

int unmapped_buf_allowed = 1;

int pti;

u_long physfree;	/* phys addr of next free page */
u_long vm86phystk;	/* PA of vm86/bios stack */
u_long vm86paddr;	/* address of vm86 region */
int vm86pa;		/* phys addr of vm86 region */
u_long KERNend;		/* phys addr end of kernel (just after bss) */
u_long KPTphys;		/* phys addr of kernel page tables */
caddr_t ptvmmap = 0;
vm_offset_t kernel_vm_end;

int i386_pmap_VM_NFREEORDER;
int i386_pmap_VM_LEVEL_0_ORDER;
int i386_pmap_PDRSHIFT;

int pat_works = 1;
SYSCTL_INT(_vm_pmap, OID_AUTO, pat_works, CTLFLAG_RD,
    &pat_works, 0,
    "Is page attribute table fully functional?");

int pg_ps_enabled = 1;
SYSCTL_INT(_vm_pmap, OID_AUTO, pg_ps_enabled, CTLFLAG_RDTUN | CTLFLAG_NOFETCH,
    &pg_ps_enabled, 0,
    "Are large page mappings enabled?");

int pv_entry_max = 0;
SYSCTL_INT(_vm_pmap, OID_AUTO, pv_entry_max, CTLFLAG_RD,
    &pv_entry_max, 0,
    "Max number of PV entries");

int pv_entry_count = 0;
SYSCTL_INT(_vm_pmap, OID_AUTO, pv_entry_count, CTLFLAG_RD,
    &pv_entry_count, 0,
    "Current number of pv entries");

#ifndef PMAP_SHPGPERPROC
#define PMAP_SHPGPERPROC 200
#endif

int shpgperproc = PMAP_SHPGPERPROC;
SYSCTL_INT(_vm_pmap, OID_AUTO, shpgperproc, CTLFLAG_RD,
    &shpgperproc, 0,
    "Page share factor per proc");

static SYSCTL_NODE(_vm_pmap, OID_AUTO, pde, CTLFLAG_RD, 0,
    "2/4MB page mapping counters");

u_long pmap_pde_demotions;
SYSCTL_ULONG(_vm_pmap_pde, OID_AUTO, demotions, CTLFLAG_RD,
    &pmap_pde_demotions, 0,
    "2/4MB page demotions");

u_long pmap_pde_mappings;
SYSCTL_ULONG(_vm_pmap_pde, OID_AUTO, mappings, CTLFLAG_RD,
    &pmap_pde_mappings, 0,
    "2/4MB page mappings");

u_long pmap_pde_p_failures;
SYSCTL_ULONG(_vm_pmap_pde, OID_AUTO, p_failures, CTLFLAG_RD,
    &pmap_pde_p_failures, 0,
    "2/4MB page promotion failures");

u_long pmap_pde_promotions;
SYSCTL_ULONG(_vm_pmap_pde, OID_AUTO, promotions, CTLFLAG_RD,
    &pmap_pde_promotions, 0,
    "2/4MB page promotions");

#ifdef SMP
int PMAP1changedcpu;
SYSCTL_INT(_debug, OID_AUTO, PMAP1changedcpu, CTLFLAG_RD,
    &PMAP1changedcpu, 0,
    "Number of times pmap_pte_quick changed CPU with same PMAP1");
#endif

int PMAP1changed;
SYSCTL_INT(_debug, OID_AUTO, PMAP1changed, CTLFLAG_RD,
    &PMAP1changed, 0,
    "Number of times pmap_pte_quick changed PMAP1");
int PMAP1unchanged;
SYSCTL_INT(_debug, OID_AUTO, PMAP1unchanged, CTLFLAG_RD,
    &PMAP1unchanged, 0,
    "Number of times pmap_pte_quick didn't change PMAP1");

static int
kvm_size(SYSCTL_HANDLER_ARGS)
{
	unsigned long ksize;

	ksize = VM_MAX_KERNEL_ADDRESS - KERNBASE;
	return (sysctl_handle_long(oidp, &ksize, 0, req));
}
SYSCTL_PROC(_vm, OID_AUTO, kvm_size, CTLTYPE_LONG | CTLFLAG_RD | CTLFLAG_MPSAFE,
    0, 0, kvm_size, "IU",
    "Size of KVM");

static int
kvm_free(SYSCTL_HANDLER_ARGS)
{
	unsigned long kfree;

	kfree = VM_MAX_KERNEL_ADDRESS - kernel_vm_end;
	return (sysctl_handle_long(oidp, &kfree, 0, req));
}
SYSCTL_PROC(_vm, OID_AUTO, kvm_free, CTLTYPE_LONG | CTLFLAG_RD | CTLFLAG_MPSAFE,
    0, 0, kvm_free, "IU",
    "Amount of KVM free");

#ifdef PV_STATS
int pc_chunk_count, pc_chunk_allocs, pc_chunk_frees, pc_chunk_tryfail;
long pv_entry_frees, pv_entry_allocs;
int pv_entry_spare;

SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_count, CTLFLAG_RD,
    &pc_chunk_count, 0,
    "Current number of pv entry chunks");
SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_allocs, CTLFLAG_RD,
    &pc_chunk_allocs, 0,
    "Current number of pv entry chunks allocated");
SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_frees, CTLFLAG_RD,
    &pc_chunk_frees, 0,
    "Current number of pv entry chunks frees");
SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_tryfail, CTLFLAG_RD,
    &pc_chunk_tryfail, 0,
    "Number of times tried to get a chunk page but failed.");
SYSCTL_LONG(_vm_pmap, OID_AUTO, pv_entry_frees, CTLFLAG_RD,
    &pv_entry_frees, 0,
    "Current number of pv entry frees");
SYSCTL_LONG(_vm_pmap, OID_AUTO, pv_entry_allocs, CTLFLAG_RD,
    &pv_entry_allocs, 0,
    "Current number of pv entry allocs");
SYSCTL_INT(_vm_pmap, OID_AUTO, pv_entry_spare, CTLFLAG_RD,
    &pv_entry_spare, 0,
    "Current number of spare pv entries");
#endif

struct pmap kernel_pmap_store;
static struct pmap_methods *pmap_methods_ptr;

/*
 * Initialize a vm_page's machine-dependent fields.
 */
void
pmap_page_init(vm_page_t m)
{

	TAILQ_INIT(&m->md.pv_list);
	m->md.pat_mode = PAT_WRITE_BACK;
}

void
invltlb_glob(void)
{

	invltlb();
}

static void pmap_invalidate_cache_range_selfsnoop(vm_offset_t sva,
    vm_offset_t eva);
static void pmap_invalidate_cache_range_all(vm_offset_t sva,
    vm_offset_t eva);

void
pmap_flush_page(vm_page_t m)
{

	pmap_methods_ptr->pm_flush_page(m);
}

DEFINE_IFUNC(, void, pmap_invalidate_cache_range, (vm_offset_t, vm_offset_t),
    static)
{

	if ((cpu_feature & CPUID_SS) != 0)
		return (pmap_invalidate_cache_range_selfsnoop);
	if ((cpu_feature & CPUID_CLFSH) != 0)
		return (pmap_force_invalidate_cache_range);
	return (pmap_invalidate_cache_range_all);
}

#define	PMAP_CLFLUSH_THRESHOLD	(2 * 1024 * 1024)

static void
pmap_invalidate_cache_range_check_align(vm_offset_t sva, vm_offset_t eva)
{

	KASSERT((sva & PAGE_MASK) == 0,
	    ("pmap_invalidate_cache_range: sva not page-aligned"));
	KASSERT((eva & PAGE_MASK) == 0,
	    ("pmap_invalidate_cache_range: eva not page-aligned"));
}

static void
pmap_invalidate_cache_range_selfsnoop(vm_offset_t sva, vm_offset_t eva)
{

	pmap_invalidate_cache_range_check_align(sva, eva);
}

void
pmap_force_invalidate_cache_range(vm_offset_t sva, vm_offset_t eva)
{

	sva &= ~(vm_offset_t)(cpu_clflush_line_size - 1);
	if (eva - sva >= PMAP_CLFLUSH_THRESHOLD) {
		/*
		 * The supplied range is bigger than 2MB.
		 * Globally invalidate cache.
		 */
		pmap_invalidate_cache();
		return;
	}

#ifdef DEV_APIC
	/*
	 * XXX: Some CPUs fault, hang, or trash the local APIC
	 * registers if we use CLFLUSH on the local APIC
	 * range.  The local APIC is always uncached, so we
	 * don't need to flush for that range anyway.
	 */
	if (pmap_kextract(sva) == lapic_paddr)
		return;
#endif

	if ((cpu_stdext_feature & CPUID_STDEXT_CLFLUSHOPT) != 0) {
		/*
		 * Do per-cache line flush.  Use the sfence
		 * instruction to insure that previous stores are
		 * included in the write-back.  The processor
		 * propagates flush to other processors in the cache
		 * coherence domain.
		 */
		sfence();
		for (; sva < eva; sva += cpu_clflush_line_size)
			clflushopt(sva);
		sfence();
	} else {
		/*
		 * Writes are ordered by CLFLUSH on Intel CPUs.
		 */
		if (cpu_vendor_id != CPU_VENDOR_INTEL)
			mfence();
		for (; sva < eva; sva += cpu_clflush_line_size)
			clflush(sva);
		if (cpu_vendor_id != CPU_VENDOR_INTEL)
			mfence();
	}
}

static void
pmap_invalidate_cache_range_all(vm_offset_t sva, vm_offset_t eva)
{

	pmap_invalidate_cache_range_check_align(sva, eva);
	pmap_invalidate_cache();
}

void
pmap_invalidate_cache_pages(vm_page_t *pages, int count)
{
	int i;

	if (count >= PMAP_CLFLUSH_THRESHOLD / PAGE_SIZE ||
	    (cpu_feature & CPUID_CLFSH) == 0) {
		pmap_invalidate_cache();
	} else {
		for (i = 0; i < count; i++)
			pmap_flush_page(pages[i]);
	}
}

void
pmap_ksetrw(vm_offset_t va)
{

	pmap_methods_ptr->pm_ksetrw(va);
}

void
pmap_remap_lower(bool enable)
{

	pmap_methods_ptr->pm_remap_lower(enable);
}

void
pmap_remap_lowptdi(bool enable)
{

	pmap_methods_ptr->pm_remap_lowptdi(enable);
}

void
pmap_align_superpage(vm_object_t object, vm_ooffset_t offset,
    vm_offset_t *addr, vm_size_t size)
{

	return (pmap_methods_ptr->pm_align_superpage(object, offset,
	    addr, size));
}

vm_offset_t
pmap_quick_enter_page(vm_page_t m)
{

	return (pmap_methods_ptr->pm_quick_enter_page(m));
}

void
pmap_quick_remove_page(vm_offset_t addr)
{

	return (pmap_methods_ptr->pm_quick_remove_page(addr));
}

void *
pmap_trm_alloc(size_t size, int flags)
{

	return (pmap_methods_ptr->pm_trm_alloc(size, flags));
}

void
pmap_trm_free(void *addr, size_t size)
{

	pmap_methods_ptr->pm_trm_free(addr, size);
}

void
pmap_sync_icache(pmap_t pm, vm_offset_t va, vm_size_t sz)
{
}

vm_offset_t
pmap_get_map_low(void)
{

	return (pmap_methods_ptr->pm_get_map_low());
}

vm_offset_t
pmap_get_vm_maxuser_address(void)
{

	return (pmap_methods_ptr->pm_get_vm_maxuser_address());
}

vm_paddr_t
pmap_kextract(vm_offset_t va)
{

	return (pmap_methods_ptr->pm_kextract(va));
}

vm_paddr_t
pmap_pg_frame(vm_paddr_t pa)
{

	return (pmap_methods_ptr->pm_pg_frame(pa));
}

void
pmap_sf_buf_map(struct sf_buf *sf)
{

	pmap_methods_ptr->pm_sf_buf_map(sf);
}

void
pmap_cp_slow0_map(vm_offset_t kaddr, int plen, vm_page_t *ma)
{

	pmap_methods_ptr->pm_cp_slow0_map(kaddr, plen, ma);
}

u_int
pmap_get_kcr3(void)
{

	return (pmap_methods_ptr->pm_get_kcr3());
}

u_int
pmap_get_cr3(pmap_t pmap)
{

	return (pmap_methods_ptr->pm_get_cr3(pmap));
}

caddr_t
pmap_cmap3(vm_paddr_t pa, u_int pte_flags)
{

	return (pmap_methods_ptr->pm_cmap3(pa, pte_flags));
}

void
pmap_basemem_setup(u_int basemem)
{

	pmap_methods_ptr->pm_basemem_setup(basemem);
}

void
pmap_set_nx(void)
{

	pmap_methods_ptr->pm_set_nx();
}

void *
pmap_bios16_enter(void)
{

	return (pmap_methods_ptr->pm_bios16_enter());
}

void
pmap_bios16_leave(void *handle)
{

	pmap_methods_ptr->pm_bios16_leave(handle);
}

void
pmap_bootstrap(vm_paddr_t firstaddr)
{

	pmap_methods_ptr->pm_bootstrap(firstaddr);
}

boolean_t
pmap_is_valid_memattr(pmap_t pmap, vm_memattr_t mode)
{

	return (pmap_methods_ptr->pm_is_valid_memattr(pmap, mode));
}

int
pmap_cache_bits(pmap_t pmap, int mode, boolean_t is_pde)
{

	return (pmap_methods_ptr->pm_cache_bits(pmap, mode, is_pde));
}

bool
pmap_ps_enabled(pmap_t pmap)
{

	return (pmap_methods_ptr->pm_ps_enabled(pmap));
}

void
pmap_pinit0(pmap_t pmap)
{

	pmap_methods_ptr->pm_pinit0(pmap);
}

int
pmap_pinit(pmap_t pmap)
{

	return (pmap_methods_ptr->pm_pinit(pmap));
}

void
pmap_activate(struct thread *td)
{

	pmap_methods_ptr->pm_activate(td);
}

void
pmap_activate_boot(pmap_t pmap)
{

	pmap_methods_ptr->pm_activate_boot(pmap);
}

void
pmap_advise(pmap_t pmap, vm_offset_t sva, vm_offset_t eva, int advice)
{

	pmap_methods_ptr->pm_advise(pmap, sva, eva, advice);
}

void
pmap_clear_modify(vm_page_t m)
{

	pmap_methods_ptr->pm_clear_modify(m);
}

int
pmap_change_attr(vm_offset_t va, vm_size_t size, int mode)
{

	return (pmap_methods_ptr->pm_change_attr(va, size, mode));
}

int
pmap_mincore(pmap_t pmap, vm_offset_t addr, vm_paddr_t *locked_pa)
{

	return (pmap_methods_ptr->pm_mincore(pmap, addr, locked_pa));
}

void
pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, vm_offset_t dst_addr, vm_size_t len,
    vm_offset_t src_addr)
{

	pmap_methods_ptr->pm_copy(dst_pmap, src_pmap, dst_addr, len, src_addr);
}

void
pmap_copy_page(vm_page_t src, vm_page_t dst)
{

	pmap_methods_ptr->pm_copy_page(src, dst);
}

void
pmap_copy_pages(vm_page_t ma[], vm_offset_t a_offset, vm_page_t mb[],
    vm_offset_t b_offset, int xfersize)
{

	pmap_methods_ptr->pm_copy_pages(ma, a_offset, mb, b_offset, xfersize);
}

void
pmap_zero_page(vm_page_t m)
{

	pmap_methods_ptr->pm_zero_page(m);
}

void
pmap_zero_page_area(vm_page_t m, int off, int size)
{

	pmap_methods_ptr->pm_zero_page_area(m, off, size);
}

int
pmap_enter(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot,
    u_int flags, int8_t psind)
{

	return (pmap_methods_ptr->pm_enter(pmap, va, m, prot, flags, psind));
}

void
pmap_enter_object(pmap_t pmap, vm_offset_t start, vm_offset_t end,
    vm_page_t m_start, vm_prot_t prot)
{

	pmap_methods_ptr->pm_enter_object(pmap, start, end, m_start, prot);
}

void
pmap_enter_quick(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot)
{

	pmap_methods_ptr->pm_enter_quick(pmap, va, m, prot);
}

void *
pmap_kenter_temporary(vm_paddr_t pa, int i)
{

	return (pmap_methods_ptr->pm_kenter_temporary(pa, i));
}

void
pmap_object_init_pt(pmap_t pmap, vm_offset_t addr, vm_object_t object,
    vm_pindex_t pindex, vm_size_t size)
{

	pmap_methods_ptr->pm_object_init_pt(pmap, addr, object, pindex, size);
}

void
pmap_unwire(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{

	pmap_methods_ptr->pm_unwire(pmap, sva, eva);
}

boolean_t
pmap_page_exists_quick(pmap_t pmap, vm_page_t m)
{

	return (pmap_methods_ptr->pm_page_exists_quick(pmap, m));
}

int
pmap_page_wired_mappings(vm_page_t m)
{

	return (pmap_methods_ptr->pm_page_wired_mappings(m));
}

boolean_t
pmap_page_is_mapped(vm_page_t m)
{

	return (pmap_methods_ptr->pm_page_is_mapped(m));
}

void
pmap_remove_pages(pmap_t pmap)
{

	pmap_methods_ptr->pm_remove_pages(pmap);
}

boolean_t
pmap_is_modified(vm_page_t m)
{

	return (pmap_methods_ptr->pm_is_modified(m));
}

boolean_t
pmap_is_prefaultable(pmap_t pmap, vm_offset_t addr)
{

	return (pmap_methods_ptr->pm_is_prefaultable(pmap, addr));
}

boolean_t
pmap_is_referenced(vm_page_t m)
{

	return (pmap_methods_ptr->pm_is_referenced(m));
}

void
pmap_remove_write(vm_page_t m)
{

	pmap_methods_ptr->pm_remove_write(m);
}

int
pmap_ts_referenced(vm_page_t m)
{

	return (pmap_methods_ptr->pm_ts_referenced(m));
}

void *
pmap_mapdev_attr(vm_paddr_t pa, vm_size_t size, int mode)
{

	return (pmap_methods_ptr->pm_mapdev_attr(pa, size, mode));
}

void *
pmap_mapdev(vm_paddr_t pa, vm_size_t size)
{

	return (pmap_methods_ptr->pm_mapdev_attr(pa, size, PAT_UNCACHEABLE));
}

void *
pmap_mapbios(vm_paddr_t pa, vm_size_t size)
{

	return (pmap_methods_ptr->pm_mapdev_attr(pa, size, PAT_WRITE_BACK));
}

void
pmap_unmapdev(vm_offset_t va, vm_size_t size)
{

	pmap_methods_ptr->pm_unmapdev(va, size);
}

void
pmap_page_set_memattr(vm_page_t m, vm_memattr_t ma)
{

	pmap_methods_ptr->pm_page_set_memattr(m, ma);
}

vm_paddr_t
pmap_extract(pmap_t pmap, vm_offset_t va)
{

	return (pmap_methods_ptr->pm_extract(pmap, va));
}

vm_page_t
pmap_extract_and_hold(pmap_t pmap, vm_offset_t va, vm_prot_t prot)
{

	return (pmap_methods_ptr->pm_extract_and_hold(pmap, va, prot));
}

vm_offset_t
pmap_map(vm_offset_t *virt, vm_paddr_t start, vm_paddr_t end, int prot)
{

	return (pmap_methods_ptr->pm_map(virt, start, end, prot));
}

void
pmap_qenter(vm_offset_t sva, vm_page_t *ma, int count)
{

	pmap_methods_ptr->pm_qenter(sva, ma, count);
}

void
pmap_qremove(vm_offset_t sva, int count)
{

	pmap_methods_ptr->pm_qremove(sva, count);
}

void
pmap_release(pmap_t pmap)
{

	pmap_methods_ptr->pm_release(pmap);
}

void
pmap_remove(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{

	pmap_methods_ptr->pm_remove(pmap, sva, eva);
}

void
pmap_protect(pmap_t pmap, vm_offset_t sva, vm_offset_t eva, vm_prot_t prot)
{

	pmap_methods_ptr->pm_protect(pmap, sva, eva, prot);
}

void
pmap_remove_all(vm_page_t m)
{

	pmap_methods_ptr->pm_remove_all(m);
}

void
pmap_init(void)
{

	pmap_methods_ptr->pm_init();
}

void
pmap_init_pat(void)
{

	pmap_methods_ptr->pm_init_pat();
}

void
pmap_growkernel(vm_offset_t addr)
{

	pmap_methods_ptr->pm_growkernel(addr);
}

void
pmap_invalidate_page(pmap_t pmap, vm_offset_t va)
{

	pmap_methods_ptr->pm_invalidate_page(pmap, va);
}

void
pmap_invalidate_range(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{

	pmap_methods_ptr->pm_invalidate_range(pmap, sva, eva);
}

void
pmap_invalidate_all(pmap_t pmap)
{

	pmap_methods_ptr->pm_invalidate_all(pmap);
}

void
pmap_invalidate_cache(void)
{

	pmap_methods_ptr->pm_invalidate_cache();
}

void
pmap_kenter(vm_offset_t va, vm_paddr_t pa)
{

	pmap_methods_ptr->pm_kenter(va, pa);
}

void
pmap_kremove(vm_offset_t va)
{

	pmap_methods_ptr->pm_kremove(va);
}

extern struct pmap_methods pmap_pae_methods, pmap_nopae_methods;
int pae_mode;
SYSCTL_INT(_vm_pmap, OID_AUTO, pae_mode, CTLFLAG_RDTUN | CTLFLAG_NOFETCH,
    &pae_mode, 0,
    "PAE");

void
pmap_cold(void)
{

	init_static_kenv((char *)bootinfo.bi_envp, 0);
	pae_mode = (cpu_feature & CPUID_PAE) != 0;
	if (pae_mode)
		TUNABLE_INT_FETCH("vm.pmap.pae_mode", &pae_mode);
	if (pae_mode) {
		pmap_methods_ptr = &pmap_pae_methods;
		pmap_pae_cold();
	} else {
		pmap_methods_ptr = &pmap_nopae_methods;
		pmap_nopae_cold();
	}
}
