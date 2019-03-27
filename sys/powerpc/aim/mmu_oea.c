/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-4-Clause
 *
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas <matt@3am-software.com> of Allegro Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $NetBSD: pmap.c,v 1.28 2000/03/26 20:42:36 kleink Exp $
 */
/*-
 * Copyright (C) 2001 Benno Rice.
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Manages physical address maps.
 *
 * Since the information managed by this module is also stored by the
 * logical address mapping module, this module may throw away valid virtual
 * to physical mappings at almost any time.  However, invalidations of
 * mappings must be done as requested.
 *
 * In order to cope with hardware architectures which make virtual to
 * physical map invalidates expensive, this module may delay invalidate
 * reduced protection operations until such time as they are actually
 * necessary.  This module is given full information as to which processors
 * are currently using which maps, and to when physical maps must be made
 * correct.
 */

#include "opt_kstack_pages.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/queue.h>
#include <sys/cpuset.h>
#include <sys/kerneldump.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/msgbuf.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/vmmeter.h>

#include <dev/ofw/openfirm.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_pageout.h>
#include <vm/uma.h>

#include <machine/cpu.h>
#include <machine/platform.h>
#include <machine/bat.h>
#include <machine/frame.h>
#include <machine/md_var.h>
#include <machine/psl.h>
#include <machine/pte.h>
#include <machine/smp.h>
#include <machine/sr.h>
#include <machine/mmuvar.h>
#include <machine/trap.h>

#include "mmu_if.h"

#define	MOEA_DEBUG

#define TODO	panic("%s: not implemented", __func__);

#define	VSID_MAKE(sr, hash)	((sr) | (((hash) & 0xfffff) << 4))
#define	VSID_TO_SR(vsid)	((vsid) & 0xf)
#define	VSID_TO_HASH(vsid)	(((vsid) >> 4) & 0xfffff)

struct ofw_map {
	vm_offset_t	om_va;
	vm_size_t	om_len;
	vm_offset_t	om_pa;
	u_int		om_mode;
};

extern unsigned char _etext[];
extern unsigned char _end[];

/*
 * Map of physical memory regions.
 */
static struct	mem_region *regions;
static struct	mem_region *pregions;
static u_int    phys_avail_count;
static int	regions_sz, pregions_sz;
static struct	ofw_map *translations;

/*
 * Lock for the pteg and pvo tables.
 */
struct mtx	moea_table_mutex;
struct mtx	moea_vsid_mutex;

/* tlbie instruction synchronization */
static struct mtx tlbie_mtx;

/*
 * PTEG data.
 */
static struct	pteg *moea_pteg_table;
u_int		moea_pteg_count;
u_int		moea_pteg_mask;

/*
 * PVO data.
 */
struct	pvo_head *moea_pvo_table;		/* pvo entries by pteg index */
struct	pvo_head moea_pvo_kunmanaged =
    LIST_HEAD_INITIALIZER(moea_pvo_kunmanaged);	/* list of unmanaged pages */

static struct rwlock_padalign pvh_global_lock;

uma_zone_t	moea_upvo_zone;	/* zone for pvo entries for unmanaged pages */
uma_zone_t	moea_mpvo_zone;	/* zone for pvo entries for managed pages */

#define	BPVO_POOL_SIZE	32768
static struct	pvo_entry *moea_bpvo_pool;
static int	moea_bpvo_pool_index = 0;

#define	VSID_NBPW	(sizeof(u_int32_t) * 8)
static u_int	moea_vsid_bitmap[NPMAPS / VSID_NBPW];

static boolean_t moea_initialized = FALSE;

/*
 * Statistics.
 */
u_int	moea_pte_valid = 0;
u_int	moea_pte_overflow = 0;
u_int	moea_pte_replacements = 0;
u_int	moea_pvo_entries = 0;
u_int	moea_pvo_enter_calls = 0;
u_int	moea_pvo_remove_calls = 0;
u_int	moea_pte_spills = 0;
SYSCTL_INT(_machdep, OID_AUTO, moea_pte_valid, CTLFLAG_RD, &moea_pte_valid,
    0, "");
SYSCTL_INT(_machdep, OID_AUTO, moea_pte_overflow, CTLFLAG_RD,
    &moea_pte_overflow, 0, "");
SYSCTL_INT(_machdep, OID_AUTO, moea_pte_replacements, CTLFLAG_RD,
    &moea_pte_replacements, 0, "");
SYSCTL_INT(_machdep, OID_AUTO, moea_pvo_entries, CTLFLAG_RD, &moea_pvo_entries,
    0, "");
SYSCTL_INT(_machdep, OID_AUTO, moea_pvo_enter_calls, CTLFLAG_RD,
    &moea_pvo_enter_calls, 0, "");
SYSCTL_INT(_machdep, OID_AUTO, moea_pvo_remove_calls, CTLFLAG_RD,
    &moea_pvo_remove_calls, 0, "");
SYSCTL_INT(_machdep, OID_AUTO, moea_pte_spills, CTLFLAG_RD,
    &moea_pte_spills, 0, "");

/*
 * Allocate physical memory for use in moea_bootstrap.
 */
static vm_offset_t	moea_bootstrap_alloc(vm_size_t, u_int);

/*
 * PTE calls.
 */
static int		moea_pte_insert(u_int, struct pte *);

/*
 * PVO calls.
 */
static int	moea_pvo_enter(pmap_t, uma_zone_t, struct pvo_head *,
		    vm_offset_t, vm_paddr_t, u_int, int);
static void	moea_pvo_remove(struct pvo_entry *, int);
static struct	pvo_entry *moea_pvo_find_va(pmap_t, vm_offset_t, int *);
static struct	pte *moea_pvo_to_pte(const struct pvo_entry *, int);

/*
 * Utility routines.
 */
static int		moea_enter_locked(pmap_t, vm_offset_t, vm_page_t,
			    vm_prot_t, u_int, int8_t);
static void		moea_syncicache(vm_paddr_t, vm_size_t);
static boolean_t	moea_query_bit(vm_page_t, int);
static u_int		moea_clear_bit(vm_page_t, int);
static void		moea_kremove(mmu_t, vm_offset_t);
int		moea_pte_spill(vm_offset_t);

/*
 * Kernel MMU interface
 */
void moea_clear_modify(mmu_t, vm_page_t);
void moea_copy_page(mmu_t, vm_page_t, vm_page_t);
void moea_copy_pages(mmu_t mmu, vm_page_t *ma, vm_offset_t a_offset,
    vm_page_t *mb, vm_offset_t b_offset, int xfersize);
int moea_enter(mmu_t, pmap_t, vm_offset_t, vm_page_t, vm_prot_t, u_int,
    int8_t);
void moea_enter_object(mmu_t, pmap_t, vm_offset_t, vm_offset_t, vm_page_t,
    vm_prot_t);
void moea_enter_quick(mmu_t, pmap_t, vm_offset_t, vm_page_t, vm_prot_t);
vm_paddr_t moea_extract(mmu_t, pmap_t, vm_offset_t);
vm_page_t moea_extract_and_hold(mmu_t, pmap_t, vm_offset_t, vm_prot_t);
void moea_init(mmu_t);
boolean_t moea_is_modified(mmu_t, vm_page_t);
boolean_t moea_is_prefaultable(mmu_t, pmap_t, vm_offset_t);
boolean_t moea_is_referenced(mmu_t, vm_page_t);
int moea_ts_referenced(mmu_t, vm_page_t);
vm_offset_t moea_map(mmu_t, vm_offset_t *, vm_paddr_t, vm_paddr_t, int);
boolean_t moea_page_exists_quick(mmu_t, pmap_t, vm_page_t);
void moea_page_init(mmu_t, vm_page_t);
int moea_page_wired_mappings(mmu_t, vm_page_t);
void moea_pinit(mmu_t, pmap_t);
void moea_pinit0(mmu_t, pmap_t);
void moea_protect(mmu_t, pmap_t, vm_offset_t, vm_offset_t, vm_prot_t);
void moea_qenter(mmu_t, vm_offset_t, vm_page_t *, int);
void moea_qremove(mmu_t, vm_offset_t, int);
void moea_release(mmu_t, pmap_t);
void moea_remove(mmu_t, pmap_t, vm_offset_t, vm_offset_t);
void moea_remove_all(mmu_t, vm_page_t);
void moea_remove_write(mmu_t, vm_page_t);
void moea_unwire(mmu_t, pmap_t, vm_offset_t, vm_offset_t);
void moea_zero_page(mmu_t, vm_page_t);
void moea_zero_page_area(mmu_t, vm_page_t, int, int);
void moea_activate(mmu_t, struct thread *);
void moea_deactivate(mmu_t, struct thread *);
void moea_cpu_bootstrap(mmu_t, int);
void moea_bootstrap(mmu_t, vm_offset_t, vm_offset_t);
void *moea_mapdev(mmu_t, vm_paddr_t, vm_size_t);
void *moea_mapdev_attr(mmu_t, vm_paddr_t, vm_size_t, vm_memattr_t);
void moea_unmapdev(mmu_t, vm_offset_t, vm_size_t);
vm_paddr_t moea_kextract(mmu_t, vm_offset_t);
void moea_kenter_attr(mmu_t, vm_offset_t, vm_paddr_t, vm_memattr_t);
void moea_kenter(mmu_t, vm_offset_t, vm_paddr_t);
void moea_page_set_memattr(mmu_t mmu, vm_page_t m, vm_memattr_t ma);
boolean_t moea_dev_direct_mapped(mmu_t, vm_paddr_t, vm_size_t);
static void moea_sync_icache(mmu_t, pmap_t, vm_offset_t, vm_size_t);
void moea_dumpsys_map(mmu_t mmu, vm_paddr_t pa, size_t sz, void **va);
void moea_scan_init(mmu_t mmu);
vm_offset_t moea_quick_enter_page(mmu_t mmu, vm_page_t m);
void moea_quick_remove_page(mmu_t mmu, vm_offset_t addr);
static int moea_map_user_ptr(mmu_t mmu, pmap_t pm,
    volatile const void *uaddr, void **kaddr, size_t ulen, size_t *klen);
static int moea_decode_kernel_ptr(mmu_t mmu, vm_offset_t addr,
    int *is_user, vm_offset_t *decoded_addr);


static mmu_method_t moea_methods[] = {
	MMUMETHOD(mmu_clear_modify,	moea_clear_modify),
	MMUMETHOD(mmu_copy_page,	moea_copy_page),
	MMUMETHOD(mmu_copy_pages,	moea_copy_pages),
	MMUMETHOD(mmu_enter,		moea_enter),
	MMUMETHOD(mmu_enter_object,	moea_enter_object),
	MMUMETHOD(mmu_enter_quick,	moea_enter_quick),
	MMUMETHOD(mmu_extract,		moea_extract),
	MMUMETHOD(mmu_extract_and_hold,	moea_extract_and_hold),
	MMUMETHOD(mmu_init,		moea_init),
	MMUMETHOD(mmu_is_modified,	moea_is_modified),
	MMUMETHOD(mmu_is_prefaultable,	moea_is_prefaultable),
	MMUMETHOD(mmu_is_referenced,	moea_is_referenced),
	MMUMETHOD(mmu_ts_referenced,	moea_ts_referenced),
	MMUMETHOD(mmu_map,     		moea_map),
	MMUMETHOD(mmu_page_exists_quick,moea_page_exists_quick),
	MMUMETHOD(mmu_page_init,	moea_page_init),
	MMUMETHOD(mmu_page_wired_mappings,moea_page_wired_mappings),
	MMUMETHOD(mmu_pinit,		moea_pinit),
	MMUMETHOD(mmu_pinit0,		moea_pinit0),
	MMUMETHOD(mmu_protect,		moea_protect),
	MMUMETHOD(mmu_qenter,		moea_qenter),
	MMUMETHOD(mmu_qremove,		moea_qremove),
	MMUMETHOD(mmu_release,		moea_release),
	MMUMETHOD(mmu_remove,		moea_remove),
	MMUMETHOD(mmu_remove_all,      	moea_remove_all),
	MMUMETHOD(mmu_remove_write,	moea_remove_write),
	MMUMETHOD(mmu_sync_icache,	moea_sync_icache),
	MMUMETHOD(mmu_unwire,		moea_unwire),
	MMUMETHOD(mmu_zero_page,       	moea_zero_page),
	MMUMETHOD(mmu_zero_page_area,	moea_zero_page_area),
	MMUMETHOD(mmu_activate,		moea_activate),
	MMUMETHOD(mmu_deactivate,      	moea_deactivate),
	MMUMETHOD(mmu_page_set_memattr,	moea_page_set_memattr),
	MMUMETHOD(mmu_quick_enter_page, moea_quick_enter_page),
	MMUMETHOD(mmu_quick_remove_page, moea_quick_remove_page),

	/* Internal interfaces */
	MMUMETHOD(mmu_bootstrap,       	moea_bootstrap),
	MMUMETHOD(mmu_cpu_bootstrap,   	moea_cpu_bootstrap),
	MMUMETHOD(mmu_mapdev_attr,	moea_mapdev_attr),
	MMUMETHOD(mmu_mapdev,		moea_mapdev),
	MMUMETHOD(mmu_unmapdev,		moea_unmapdev),
	MMUMETHOD(mmu_kextract,		moea_kextract),
	MMUMETHOD(mmu_kenter,		moea_kenter),
	MMUMETHOD(mmu_kenter_attr,	moea_kenter_attr),
	MMUMETHOD(mmu_dev_direct_mapped,moea_dev_direct_mapped),
	MMUMETHOD(mmu_scan_init,	moea_scan_init),
	MMUMETHOD(mmu_dumpsys_map,	moea_dumpsys_map),
	MMUMETHOD(mmu_map_user_ptr,	moea_map_user_ptr),
	MMUMETHOD(mmu_decode_kernel_ptr, moea_decode_kernel_ptr),

	{ 0, 0 }
};

MMU_DEF(oea_mmu, MMU_TYPE_OEA, moea_methods, 0);

static __inline uint32_t
moea_calc_wimg(vm_paddr_t pa, vm_memattr_t ma)
{
	uint32_t pte_lo;
	int i;

	if (ma != VM_MEMATTR_DEFAULT) {
		switch (ma) {
		case VM_MEMATTR_UNCACHEABLE:
			return (PTE_I | PTE_G);
		case VM_MEMATTR_CACHEABLE:
			return (PTE_M);
		case VM_MEMATTR_WRITE_COMBINING:
		case VM_MEMATTR_WRITE_BACK:
		case VM_MEMATTR_PREFETCHABLE:
			return (PTE_I);
		case VM_MEMATTR_WRITE_THROUGH:
			return (PTE_W | PTE_M);
		}
	}

	/*
	 * Assume the page is cache inhibited and access is guarded unless
	 * it's in our available memory array.
	 */
	pte_lo = PTE_I | PTE_G;
	for (i = 0; i < pregions_sz; i++) {
		if ((pa >= pregions[i].mr_start) &&
		    (pa < (pregions[i].mr_start + pregions[i].mr_size))) {
			pte_lo = PTE_M;
			break;
		}
	}

	return pte_lo;
}

static void
tlbie(vm_offset_t va)
{

	mtx_lock_spin(&tlbie_mtx);
	__asm __volatile("ptesync");
	__asm __volatile("tlbie %0" :: "r"(va));
	__asm __volatile("eieio; tlbsync; ptesync");
	mtx_unlock_spin(&tlbie_mtx);
}

static void
tlbia(void)
{
	vm_offset_t va;

	for (va = 0; va < 0x00040000; va += 0x00001000) {
		__asm __volatile("tlbie %0" :: "r"(va));
		powerpc_sync();
	}
	__asm __volatile("tlbsync");
	powerpc_sync();
}

static __inline int
va_to_sr(u_int *sr, vm_offset_t va)
{
	return (sr[(uintptr_t)va >> ADDR_SR_SHFT]);
}

static __inline u_int
va_to_pteg(u_int sr, vm_offset_t addr)
{
	u_int hash;

	hash = (sr & SR_VSID_MASK) ^ (((u_int)addr & ADDR_PIDX) >>
	    ADDR_PIDX_SHFT);
	return (hash & moea_pteg_mask);
}

static __inline struct pvo_head *
vm_page_to_pvoh(vm_page_t m)
{

	return (&m->md.mdpg_pvoh);
}

static __inline void
moea_attr_clear(vm_page_t m, int ptebit)
{

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	m->md.mdpg_attrs &= ~ptebit;
}

static __inline int
moea_attr_fetch(vm_page_t m)
{

	return (m->md.mdpg_attrs);
}

static __inline void
moea_attr_save(vm_page_t m, int ptebit)
{

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	m->md.mdpg_attrs |= ptebit;
}

static __inline int
moea_pte_compare(const struct pte *pt, const struct pte *pvo_pt)
{
	if (pt->pte_hi == pvo_pt->pte_hi)
		return (1);

	return (0);
}

static __inline int
moea_pte_match(struct pte *pt, u_int sr, vm_offset_t va, int which)
{
	return (pt->pte_hi & ~PTE_VALID) ==
	    (((sr & SR_VSID_MASK) << PTE_VSID_SHFT) |
	    ((va >> ADDR_API_SHFT) & PTE_API) | which);
}

static __inline void
moea_pte_create(struct pte *pt, u_int sr, vm_offset_t va, u_int pte_lo)
{

	mtx_assert(&moea_table_mutex, MA_OWNED);

	/*
	 * Construct a PTE.  Default to IMB initially.  Valid bit only gets
	 * set when the real pte is set in memory.
	 *
	 * Note: Don't set the valid bit for correct operation of tlb update.
	 */
	pt->pte_hi = ((sr & SR_VSID_MASK) << PTE_VSID_SHFT) |
	    (((va & ADDR_PIDX) >> ADDR_API_SHFT) & PTE_API);
	pt->pte_lo = pte_lo;
}

static __inline void
moea_pte_synch(struct pte *pt, struct pte *pvo_pt)
{

	mtx_assert(&moea_table_mutex, MA_OWNED);
	pvo_pt->pte_lo |= pt->pte_lo & (PTE_REF | PTE_CHG);
}

static __inline void
moea_pte_clear(struct pte *pt, vm_offset_t va, int ptebit)
{

	mtx_assert(&moea_table_mutex, MA_OWNED);

	/*
	 * As shown in Section 7.6.3.2.3
	 */
	pt->pte_lo &= ~ptebit;
	tlbie(va);
}

static __inline void
moea_pte_set(struct pte *pt, struct pte *pvo_pt)
{

	mtx_assert(&moea_table_mutex, MA_OWNED);
	pvo_pt->pte_hi |= PTE_VALID;

	/*
	 * Update the PTE as defined in section 7.6.3.1.
	 * Note that the REF/CHG bits are from pvo_pt and thus should have
	 * been saved so this routine can restore them (if desired).
	 */
	pt->pte_lo = pvo_pt->pte_lo;
	powerpc_sync();
	pt->pte_hi = pvo_pt->pte_hi;
	powerpc_sync();
	moea_pte_valid++;
}

static __inline void
moea_pte_unset(struct pte *pt, struct pte *pvo_pt, vm_offset_t va)
{

	mtx_assert(&moea_table_mutex, MA_OWNED);
	pvo_pt->pte_hi &= ~PTE_VALID;

	/*
	 * Force the reg & chg bits back into the PTEs.
	 */
	powerpc_sync();

	/*
	 * Invalidate the pte.
	 */
	pt->pte_hi &= ~PTE_VALID;

	tlbie(va);

	/*
	 * Save the reg & chg bits.
	 */
	moea_pte_synch(pt, pvo_pt);
	moea_pte_valid--;
}

static __inline void
moea_pte_change(struct pte *pt, struct pte *pvo_pt, vm_offset_t va)
{

	/*
	 * Invalidate the PTE
	 */
	moea_pte_unset(pt, pvo_pt, va);
	moea_pte_set(pt, pvo_pt);
}

/*
 * Quick sort callout for comparing memory regions.
 */
static int	om_cmp(const void *a, const void *b);

static int
om_cmp(const void *a, const void *b)
{
	const struct	ofw_map *mapa;
	const struct	ofw_map *mapb;

	mapa = a;
	mapb = b;
	if (mapa->om_pa < mapb->om_pa)
		return (-1);
	else if (mapa->om_pa > mapb->om_pa)
		return (1);
	else
		return (0);
}

void
moea_cpu_bootstrap(mmu_t mmup, int ap)
{
	u_int sdr;
	int i;

	if (ap) {
		powerpc_sync();
		__asm __volatile("mtdbatu 0,%0" :: "r"(battable[0].batu));
		__asm __volatile("mtdbatl 0,%0" :: "r"(battable[0].batl));
		isync();
		__asm __volatile("mtibatu 0,%0" :: "r"(battable[0].batu));
		__asm __volatile("mtibatl 0,%0" :: "r"(battable[0].batl));
		isync();
	}

	__asm __volatile("mtdbatu 1,%0" :: "r"(battable[8].batu));
	__asm __volatile("mtdbatl 1,%0" :: "r"(battable[8].batl));
	isync();

	__asm __volatile("mtibatu 1,%0" :: "r"(0));
	__asm __volatile("mtdbatu 2,%0" :: "r"(0));
	__asm __volatile("mtibatu 2,%0" :: "r"(0));
	__asm __volatile("mtdbatu 3,%0" :: "r"(0));
	__asm __volatile("mtibatu 3,%0" :: "r"(0));
	isync();

	for (i = 0; i < 16; i++)
		mtsrin(i << ADDR_SR_SHFT, kernel_pmap->pm_sr[i]);
	powerpc_sync();

	sdr = (u_int)moea_pteg_table | (moea_pteg_mask >> 10);
	__asm __volatile("mtsdr1 %0" :: "r"(sdr));
	isync();

	tlbia();
}

void
moea_bootstrap(mmu_t mmup, vm_offset_t kernelstart, vm_offset_t kernelend)
{
	ihandle_t	mmui;
	phandle_t	chosen, mmu;
	int		sz;
	int		i, j;
	vm_size_t	size, physsz, hwphyssz;
	vm_offset_t	pa, va, off;
	void		*dpcpu;
	register_t	msr;

        /*
         * Set up BAT0 to map the lowest 256 MB area
         */
        battable[0x0].batl = BATL(0x00000000, BAT_M, BAT_PP_RW);
        battable[0x0].batu = BATU(0x00000000, BAT_BL_256M, BAT_Vs);

	/*
	 * Map PCI memory space.
	 */
	battable[0x8].batl = BATL(0x80000000, BAT_I|BAT_G, BAT_PP_RW);
	battable[0x8].batu = BATU(0x80000000, BAT_BL_256M, BAT_Vs);

	battable[0x9].batl = BATL(0x90000000, BAT_I|BAT_G, BAT_PP_RW);
	battable[0x9].batu = BATU(0x90000000, BAT_BL_256M, BAT_Vs);

	battable[0xa].batl = BATL(0xa0000000, BAT_I|BAT_G, BAT_PP_RW);
	battable[0xa].batu = BATU(0xa0000000, BAT_BL_256M, BAT_Vs);

	battable[0xb].batl = BATL(0xb0000000, BAT_I|BAT_G, BAT_PP_RW);
	battable[0xb].batu = BATU(0xb0000000, BAT_BL_256M, BAT_Vs);

	/*
	 * Map obio devices.
	 */
	battable[0xf].batl = BATL(0xf0000000, BAT_I|BAT_G, BAT_PP_RW);
	battable[0xf].batu = BATU(0xf0000000, BAT_BL_256M, BAT_Vs);

	/*
	 * Use an IBAT and a DBAT to map the bottom segment of memory
	 * where we are. Turn off instruction relocation temporarily
	 * to prevent faults while reprogramming the IBAT.
	 */
	msr = mfmsr();
	mtmsr(msr & ~PSL_IR);
	__asm (".balign 32; \n"
	       "mtibatu 0,%0; mtibatl 0,%1; isync; \n"
	       "mtdbatu 0,%0; mtdbatl 0,%1; isync"
	    :: "r"(battable[0].batu), "r"(battable[0].batl));
	mtmsr(msr);

	/* map pci space */
	__asm __volatile("mtdbatu 1,%0" :: "r"(battable[8].batu));
	__asm __volatile("mtdbatl 1,%0" :: "r"(battable[8].batl));
	isync();

	/* set global direct map flag */
	hw_direct_map = 1;

	mem_regions(&pregions, &pregions_sz, &regions, &regions_sz);
	CTR0(KTR_PMAP, "moea_bootstrap: physical memory");

	for (i = 0; i < pregions_sz; i++) {
		vm_offset_t pa;
		vm_offset_t end;

		CTR3(KTR_PMAP, "physregion: %#x - %#x (%#x)",
			pregions[i].mr_start,
			pregions[i].mr_start + pregions[i].mr_size,
			pregions[i].mr_size);
		/*
		 * Install entries into the BAT table to allow all
		 * of physmem to be convered by on-demand BAT entries.
		 * The loop will sometimes set the same battable element
		 * twice, but that's fine since they won't be used for
		 * a while yet.
		 */
		pa = pregions[i].mr_start & 0xf0000000;
		end = pregions[i].mr_start + pregions[i].mr_size;
		do {
                        u_int n = pa >> ADDR_SR_SHFT;

			battable[n].batl = BATL(pa, BAT_M, BAT_PP_RW);
			battable[n].batu = BATU(pa, BAT_BL_256M, BAT_Vs);
			pa += SEGMENT_LENGTH;
		} while (pa < end);
	}

	if (sizeof(phys_avail)/sizeof(phys_avail[0]) < regions_sz)
		panic("moea_bootstrap: phys_avail too small");

	phys_avail_count = 0;
	physsz = 0;
	hwphyssz = 0;
	TUNABLE_ULONG_FETCH("hw.physmem", (u_long *) &hwphyssz);
	for (i = 0, j = 0; i < regions_sz; i++, j += 2) {
		CTR3(KTR_PMAP, "region: %#x - %#x (%#x)", regions[i].mr_start,
		    regions[i].mr_start + regions[i].mr_size,
		    regions[i].mr_size);
		if (hwphyssz != 0 &&
		    (physsz + regions[i].mr_size) >= hwphyssz) {
			if (physsz < hwphyssz) {
				phys_avail[j] = regions[i].mr_start;
				phys_avail[j + 1] = regions[i].mr_start +
				    hwphyssz - physsz;
				physsz = hwphyssz;
				phys_avail_count++;
			}
			break;
		}
		phys_avail[j] = regions[i].mr_start;
		phys_avail[j + 1] = regions[i].mr_start + regions[i].mr_size;
		phys_avail_count++;
		physsz += regions[i].mr_size;
	}

	/* Check for overlap with the kernel and exception vectors */
	for (j = 0; j < 2*phys_avail_count; j+=2) {
		if (phys_avail[j] < EXC_LAST)
			phys_avail[j] += EXC_LAST;

		if (kernelstart >= phys_avail[j] &&
		    kernelstart < phys_avail[j+1]) {
			if (kernelend < phys_avail[j+1]) {
				phys_avail[2*phys_avail_count] =
				    (kernelend & ~PAGE_MASK) + PAGE_SIZE;
				phys_avail[2*phys_avail_count + 1] =
				    phys_avail[j+1];
				phys_avail_count++;
			}

			phys_avail[j+1] = kernelstart & ~PAGE_MASK;
		}

		if (kernelend >= phys_avail[j] &&
		    kernelend < phys_avail[j+1]) {
			if (kernelstart > phys_avail[j]) {
				phys_avail[2*phys_avail_count] = phys_avail[j];
				phys_avail[2*phys_avail_count + 1] =
				    kernelstart & ~PAGE_MASK;
				phys_avail_count++;
			}

			phys_avail[j] = (kernelend & ~PAGE_MASK) + PAGE_SIZE;
		}
	}

	physmem = btoc(physsz);

	/*
	 * Allocate PTEG table.
	 */
#ifdef PTEGCOUNT
	moea_pteg_count = PTEGCOUNT;
#else
	moea_pteg_count = 0x1000;

	while (moea_pteg_count < physmem)
		moea_pteg_count <<= 1;

	moea_pteg_count >>= 1;
#endif /* PTEGCOUNT */

	size = moea_pteg_count * sizeof(struct pteg);
	CTR2(KTR_PMAP, "moea_bootstrap: %d PTEGs, %d bytes", moea_pteg_count,
	    size);
	moea_pteg_table = (struct pteg *)moea_bootstrap_alloc(size, size);
	CTR1(KTR_PMAP, "moea_bootstrap: PTEG table at %p", moea_pteg_table);
	bzero((void *)moea_pteg_table, moea_pteg_count * sizeof(struct pteg));
	moea_pteg_mask = moea_pteg_count - 1;

	/*
	 * Allocate pv/overflow lists.
	 */
	size = sizeof(struct pvo_head) * moea_pteg_count;
	moea_pvo_table = (struct pvo_head *)moea_bootstrap_alloc(size,
	    PAGE_SIZE);
	CTR1(KTR_PMAP, "moea_bootstrap: PVO table at %p", moea_pvo_table);
	for (i = 0; i < moea_pteg_count; i++)
		LIST_INIT(&moea_pvo_table[i]);

	/*
	 * Initialize the lock that synchronizes access to the pteg and pvo
	 * tables.
	 */
	mtx_init(&moea_table_mutex, "pmap table", NULL, MTX_DEF |
	    MTX_RECURSE);
	mtx_init(&moea_vsid_mutex, "VSID table", NULL, MTX_DEF);

	mtx_init(&tlbie_mtx, "tlbie", NULL, MTX_SPIN);

	/*
	 * Initialise the unmanaged pvo pool.
	 */
	moea_bpvo_pool = (struct pvo_entry *)moea_bootstrap_alloc(
		BPVO_POOL_SIZE*sizeof(struct pvo_entry), 0);
	moea_bpvo_pool_index = 0;

	/*
	 * Make sure kernel vsid is allocated as well as VSID 0.
	 */
	moea_vsid_bitmap[(KERNEL_VSIDBITS & (NPMAPS - 1)) / VSID_NBPW]
		|= 1 << (KERNEL_VSIDBITS % VSID_NBPW);
	moea_vsid_bitmap[0] |= 1;

	/*
	 * Initialize the kernel pmap (which is statically allocated).
	 */
	PMAP_LOCK_INIT(kernel_pmap);
	for (i = 0; i < 16; i++)
		kernel_pmap->pm_sr[i] = EMPTY_SEGMENT + i;
	CPU_FILL(&kernel_pmap->pm_active);
	RB_INIT(&kernel_pmap->pmap_pvo);

 	/*
	 * Initialize the global pv list lock.
	 */
	rw_init(&pvh_global_lock, "pmap pv global");

	/*
	 * Set up the Open Firmware mappings
	 */
	chosen = OF_finddevice("/chosen");
	if (chosen != -1 && OF_getprop(chosen, "mmu", &mmui, 4) != -1 &&
	    (mmu = OF_instance_to_package(mmui)) != -1 &&
	    (sz = OF_getproplen(mmu, "translations")) != -1) {
		translations = NULL;
		for (i = 0; phys_avail[i] != 0; i += 2) {
			if (phys_avail[i + 1] >= sz) {
				translations = (struct ofw_map *)phys_avail[i];
				break;
			}
		}
		if (translations == NULL)
			panic("moea_bootstrap: no space to copy translations");
		bzero(translations, sz);
		if (OF_getprop(mmu, "translations", translations, sz) == -1)
			panic("moea_bootstrap: can't get ofw translations");
		CTR0(KTR_PMAP, "moea_bootstrap: translations");
		sz /= sizeof(*translations);
		qsort(translations, sz, sizeof (*translations), om_cmp);
		for (i = 0; i < sz; i++) {
			CTR3(KTR_PMAP, "translation: pa=%#x va=%#x len=%#x",
			    translations[i].om_pa, translations[i].om_va,
			    translations[i].om_len);

			/*
			 * If the mapping is 1:1, let the RAM and device
			 * on-demand BAT tables take care of the translation.
			 */
			if (translations[i].om_va == translations[i].om_pa)
				continue;

			/* Enter the pages */
			for (off = 0; off < translations[i].om_len;
			    off += PAGE_SIZE)
				moea_kenter(mmup, translations[i].om_va + off,
					    translations[i].om_pa + off);
		}
	}

	/*
	 * Calculate the last available physical address.
	 */
	for (i = 0; phys_avail[i + 2] != 0; i += 2)
		;
	Maxmem = powerpc_btop(phys_avail[i + 1]);

	moea_cpu_bootstrap(mmup,0);
	mtmsr(mfmsr() | PSL_DR | PSL_IR);
	pmap_bootstrapped++;

	/*
	 * Set the start and end of kva.
	 */
	virtual_avail = VM_MIN_KERNEL_ADDRESS;
	virtual_end = VM_MAX_SAFE_KERNEL_ADDRESS;

	/*
	 * Allocate a kernel stack with a guard page for thread0 and map it
	 * into the kernel page map.
	 */
	pa = moea_bootstrap_alloc(kstack_pages * PAGE_SIZE, PAGE_SIZE);
	va = virtual_avail + KSTACK_GUARD_PAGES * PAGE_SIZE;
	virtual_avail = va + kstack_pages * PAGE_SIZE;
	CTR2(KTR_PMAP, "moea_bootstrap: kstack0 at %#x (%#x)", pa, va);
	thread0.td_kstack = va;
	thread0.td_kstack_pages = kstack_pages;
	for (i = 0; i < kstack_pages; i++) {
		moea_kenter(mmup, va, pa);
		pa += PAGE_SIZE;
		va += PAGE_SIZE;
	}

	/*
	 * Allocate virtual address space for the message buffer.
	 */
	pa = msgbuf_phys = moea_bootstrap_alloc(msgbufsize, PAGE_SIZE);
	msgbufp = (struct msgbuf *)virtual_avail;
	va = virtual_avail;
	virtual_avail += round_page(msgbufsize);
	while (va < virtual_avail) {
		moea_kenter(mmup, va, pa);
		pa += PAGE_SIZE;
		va += PAGE_SIZE;
	}

	/*
	 * Allocate virtual address space for the dynamic percpu area.
	 */
	pa = moea_bootstrap_alloc(DPCPU_SIZE, PAGE_SIZE);
	dpcpu = (void *)virtual_avail;
	va = virtual_avail;
	virtual_avail += DPCPU_SIZE;
	while (va < virtual_avail) {
		moea_kenter(mmup, va, pa);
		pa += PAGE_SIZE;
		va += PAGE_SIZE;
	}
	dpcpu_init(dpcpu, 0);
}

/*
 * Activate a user pmap.  The pmap must be activated before it's address
 * space can be accessed in any way.
 */
void
moea_activate(mmu_t mmu, struct thread *td)
{
	pmap_t	pm, pmr;

	/*
	 * Load all the data we need up front to encourage the compiler to
	 * not issue any loads while we have interrupts disabled below.
	 */
	pm = &td->td_proc->p_vmspace->vm_pmap;
	pmr = pm->pmap_phys;

	CPU_SET(PCPU_GET(cpuid), &pm->pm_active);
	PCPU_SET(curpmap, pmr);

	mtsrin(USER_SR << ADDR_SR_SHFT, td->td_pcb->pcb_cpu.aim.usr_vsid);
}

void
moea_deactivate(mmu_t mmu, struct thread *td)
{
	pmap_t	pm;

	pm = &td->td_proc->p_vmspace->vm_pmap;
	CPU_CLR(PCPU_GET(cpuid), &pm->pm_active);
	PCPU_SET(curpmap, NULL);
}

void
moea_unwire(mmu_t mmu, pmap_t pm, vm_offset_t sva, vm_offset_t eva)
{
	struct	pvo_entry key, *pvo;

	PMAP_LOCK(pm);
	key.pvo_vaddr = sva;
	for (pvo = RB_NFIND(pvo_tree, &pm->pmap_pvo, &key);
	    pvo != NULL && PVO_VADDR(pvo) < eva;
	    pvo = RB_NEXT(pvo_tree, &pm->pmap_pvo, pvo)) {
		if ((pvo->pvo_vaddr & PVO_WIRED) == 0)
			panic("moea_unwire: pvo %p is missing PVO_WIRED", pvo);
		pvo->pvo_vaddr &= ~PVO_WIRED;
		pm->pm_stats.wired_count--;
	}
	PMAP_UNLOCK(pm);
}

void
moea_copy_page(mmu_t mmu, vm_page_t msrc, vm_page_t mdst)
{
	vm_offset_t	dst;
	vm_offset_t	src;

	dst = VM_PAGE_TO_PHYS(mdst);
	src = VM_PAGE_TO_PHYS(msrc);

	bcopy((void *)src, (void *)dst, PAGE_SIZE);
}

void
moea_copy_pages(mmu_t mmu, vm_page_t *ma, vm_offset_t a_offset,
    vm_page_t *mb, vm_offset_t b_offset, int xfersize)
{
	void *a_cp, *b_cp;
	vm_offset_t a_pg_offset, b_pg_offset;
	int cnt;

	while (xfersize > 0) {
		a_pg_offset = a_offset & PAGE_MASK;
		cnt = min(xfersize, PAGE_SIZE - a_pg_offset);
		a_cp = (char *)VM_PAGE_TO_PHYS(ma[a_offset >> PAGE_SHIFT]) +
		    a_pg_offset;
		b_pg_offset = b_offset & PAGE_MASK;
		cnt = min(cnt, PAGE_SIZE - b_pg_offset);
		b_cp = (char *)VM_PAGE_TO_PHYS(mb[b_offset >> PAGE_SHIFT]) +
		    b_pg_offset;
		bcopy(a_cp, b_cp, cnt);
		a_offset += cnt;
		b_offset += cnt;
		xfersize -= cnt;
	}
}

/*
 * Zero a page of physical memory by temporarily mapping it into the tlb.
 */
void
moea_zero_page(mmu_t mmu, vm_page_t m)
{
	vm_offset_t off, pa = VM_PAGE_TO_PHYS(m);

	for (off = 0; off < PAGE_SIZE; off += cacheline_size)
		__asm __volatile("dcbz 0,%0" :: "r"(pa + off));
}

void
moea_zero_page_area(mmu_t mmu, vm_page_t m, int off, int size)
{
	vm_offset_t pa = VM_PAGE_TO_PHYS(m);
	void *va = (void *)(pa + off);

	bzero(va, size);
}

vm_offset_t
moea_quick_enter_page(mmu_t mmu, vm_page_t m)
{

	return (VM_PAGE_TO_PHYS(m));
}

void
moea_quick_remove_page(mmu_t mmu, vm_offset_t addr)
{
}

/*
 * Map the given physical page at the specified virtual address in the
 * target pmap with the protection requested.  If specified the page
 * will be wired down.
 */
int
moea_enter(mmu_t mmu, pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot,
    u_int flags, int8_t psind)
{
	int error;

	for (;;) {
		rw_wlock(&pvh_global_lock);
		PMAP_LOCK(pmap);
		error = moea_enter_locked(pmap, va, m, prot, flags, psind);
		rw_wunlock(&pvh_global_lock);
		PMAP_UNLOCK(pmap);
		if (error != ENOMEM)
			return (KERN_SUCCESS);
		if ((flags & PMAP_ENTER_NOSLEEP) != 0)
			return (KERN_RESOURCE_SHORTAGE);
		VM_OBJECT_ASSERT_UNLOCKED(m->object);
		vm_wait(NULL);
	}
}

/*
 * Map the given physical page at the specified virtual address in the
 * target pmap with the protection requested.  If specified the page
 * will be wired down.
 *
 * The global pvh and pmap must be locked.
 */
static int
moea_enter_locked(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot,
    u_int flags, int8_t psind __unused)
{
	struct		pvo_head *pvo_head;
	uma_zone_t	zone;
	u_int		pte_lo, pvo_flags;
	int		error;

	if (pmap_bootstrapped)
		rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	if ((m->oflags & VPO_UNMANAGED) == 0 && !vm_page_xbusied(m))
		VM_OBJECT_ASSERT_LOCKED(m->object);

	if ((m->oflags & VPO_UNMANAGED) != 0 || !moea_initialized) {
		pvo_head = &moea_pvo_kunmanaged;
		zone = moea_upvo_zone;
		pvo_flags = 0;
	} else {
		pvo_head = vm_page_to_pvoh(m);
		zone = moea_mpvo_zone;
		pvo_flags = PVO_MANAGED;
	}

	pte_lo = moea_calc_wimg(VM_PAGE_TO_PHYS(m), pmap_page_get_memattr(m));

	if (prot & VM_PROT_WRITE) {
		pte_lo |= PTE_BW;
		if (pmap_bootstrapped &&
		    (m->oflags & VPO_UNMANAGED) == 0)
			vm_page_aflag_set(m, PGA_WRITEABLE);
	} else
		pte_lo |= PTE_BR;

	if ((flags & PMAP_ENTER_WIRED) != 0)
		pvo_flags |= PVO_WIRED;

	error = moea_pvo_enter(pmap, zone, pvo_head, va, VM_PAGE_TO_PHYS(m),
	    pte_lo, pvo_flags);

	/*
	 * Flush the real page from the instruction cache. This has be done
	 * for all user mappings to prevent information leakage via the
	 * instruction cache. moea_pvo_enter() returns ENOENT for the first
	 * mapping for a page.
	 */
	if (pmap != kernel_pmap && error == ENOENT &&
	    (pte_lo & (PTE_I | PTE_G)) == 0)
		moea_syncicache(VM_PAGE_TO_PHYS(m), PAGE_SIZE);

	return (error);
}

/*
 * Maps a sequence of resident pages belonging to the same object.
 * The sequence begins with the given page m_start.  This page is
 * mapped at the given virtual address start.  Each subsequent page is
 * mapped at a virtual address that is offset from start by the same
 * amount as the page is offset from m_start within the object.  The
 * last page in the sequence is the page with the largest offset from
 * m_start that can be mapped at a virtual address less than the given
 * virtual address end.  Not every virtual page between start and end
 * is mapped; only those for which a resident page exists with the
 * corresponding offset from m_start are mapped.
 */
void
moea_enter_object(mmu_t mmu, pmap_t pm, vm_offset_t start, vm_offset_t end,
    vm_page_t m_start, vm_prot_t prot)
{
	vm_page_t m;
	vm_pindex_t diff, psize;

	VM_OBJECT_ASSERT_LOCKED(m_start->object);

	psize = atop(end - start);
	m = m_start;
	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pm);
	while (m != NULL && (diff = m->pindex - m_start->pindex) < psize) {
		moea_enter_locked(pm, start + ptoa(diff), m, prot &
		    (VM_PROT_READ | VM_PROT_EXECUTE), 0, 0);
		m = TAILQ_NEXT(m, listq);
	}
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pm);
}

void
moea_enter_quick(mmu_t mmu, pmap_t pm, vm_offset_t va, vm_page_t m,
    vm_prot_t prot)
{

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pm);
	moea_enter_locked(pm, va, m, prot & (VM_PROT_READ | VM_PROT_EXECUTE),
	    0, 0);
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pm);
}

vm_paddr_t
moea_extract(mmu_t mmu, pmap_t pm, vm_offset_t va)
{
	struct	pvo_entry *pvo;
	vm_paddr_t pa;

	PMAP_LOCK(pm);
	pvo = moea_pvo_find_va(pm, va & ~ADDR_POFF, NULL);
	if (pvo == NULL)
		pa = 0;
	else
		pa = (pvo->pvo_pte.pte.pte_lo & PTE_RPGN) | (va & ADDR_POFF);
	PMAP_UNLOCK(pm);
	return (pa);
}

/*
 * Atomically extract and hold the physical page with the given
 * pmap and virtual address pair if that mapping permits the given
 * protection.
 */
vm_page_t
moea_extract_and_hold(mmu_t mmu, pmap_t pmap, vm_offset_t va, vm_prot_t prot)
{
	struct	pvo_entry *pvo;
	vm_page_t m;
        vm_paddr_t pa;

	m = NULL;
	pa = 0;
	PMAP_LOCK(pmap);
retry:
	pvo = moea_pvo_find_va(pmap, va & ~ADDR_POFF, NULL);
	if (pvo != NULL && (pvo->pvo_pte.pte.pte_hi & PTE_VALID) &&
	    ((pvo->pvo_pte.pte.pte_lo & PTE_PP) == PTE_RW ||
	     (prot & VM_PROT_WRITE) == 0)) {
		if (vm_page_pa_tryrelock(pmap, pvo->pvo_pte.pte.pte_lo & PTE_RPGN, &pa))
			goto retry;
		m = PHYS_TO_VM_PAGE(pvo->pvo_pte.pte.pte_lo & PTE_RPGN);
		vm_page_hold(m);
	}
	PA_UNLOCK_COND(pa);
	PMAP_UNLOCK(pmap);
	return (m);
}

void
moea_init(mmu_t mmu)
{

	moea_upvo_zone = uma_zcreate("UPVO entry", sizeof (struct pvo_entry),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR,
	    UMA_ZONE_VM | UMA_ZONE_NOFREE);
	moea_mpvo_zone = uma_zcreate("MPVO entry", sizeof(struct pvo_entry),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR,
	    UMA_ZONE_VM | UMA_ZONE_NOFREE);
	moea_initialized = TRUE;
}

boolean_t
moea_is_referenced(mmu_t mmu, vm_page_t m)
{
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("moea_is_referenced: page %p is not managed", m));
	rw_wlock(&pvh_global_lock);
	rv = moea_query_bit(m, PTE_REF);
	rw_wunlock(&pvh_global_lock);
	return (rv);
}

boolean_t
moea_is_modified(mmu_t mmu, vm_page_t m)
{
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("moea_is_modified: page %p is not managed", m));

	/*
	 * If the page is not exclusive busied, then PGA_WRITEABLE cannot be
	 * concurrently set while the object is locked.  Thus, if PGA_WRITEABLE
	 * is clear, no PTEs can have PTE_CHG set.
	 */
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (!vm_page_xbusied(m) && (m->aflags & PGA_WRITEABLE) == 0)
		return (FALSE);
	rw_wlock(&pvh_global_lock);
	rv = moea_query_bit(m, PTE_CHG);
	rw_wunlock(&pvh_global_lock);
	return (rv);
}

boolean_t
moea_is_prefaultable(mmu_t mmu, pmap_t pmap, vm_offset_t va)
{
	struct pvo_entry *pvo;
	boolean_t rv;

	PMAP_LOCK(pmap);
	pvo = moea_pvo_find_va(pmap, va & ~ADDR_POFF, NULL);
	rv = pvo == NULL || (pvo->pvo_pte.pte.pte_hi & PTE_VALID) == 0;
	PMAP_UNLOCK(pmap);
	return (rv);
}

void
moea_clear_modify(mmu_t mmu, vm_page_t m)
{

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("moea_clear_modify: page %p is not managed", m));
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	KASSERT(!vm_page_xbusied(m),
	    ("moea_clear_modify: page %p is exclusive busy", m));

	/*
	 * If the page is not PGA_WRITEABLE, then no PTEs can have PTE_CHG
	 * set.  If the object containing the page is locked and the page is
	 * not exclusive busied, then PGA_WRITEABLE cannot be concurrently set.
	 */
	if ((m->aflags & PGA_WRITEABLE) == 0)
		return;
	rw_wlock(&pvh_global_lock);
	moea_clear_bit(m, PTE_CHG);
	rw_wunlock(&pvh_global_lock);
}

/*
 * Clear the write and modified bits in each of the given page's mappings.
 */
void
moea_remove_write(mmu_t mmu, vm_page_t m)
{
	struct	pvo_entry *pvo;
	struct	pte *pt;
	pmap_t	pmap;
	u_int	lo;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("moea_remove_write: page %p is not managed", m));

	/*
	 * If the page is not exclusive busied, then PGA_WRITEABLE cannot be
	 * set by another thread while the object is locked.  Thus,
	 * if PGA_WRITEABLE is clear, no page table entries need updating.
	 */
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (!vm_page_xbusied(m) && (m->aflags & PGA_WRITEABLE) == 0)
		return;
	rw_wlock(&pvh_global_lock);
	lo = moea_attr_fetch(m);
	powerpc_sync();
	LIST_FOREACH(pvo, vm_page_to_pvoh(m), pvo_vlink) {
		pmap = pvo->pvo_pmap;
		PMAP_LOCK(pmap);
		if ((pvo->pvo_pte.pte.pte_lo & PTE_PP) != PTE_BR) {
			pt = moea_pvo_to_pte(pvo, -1);
			pvo->pvo_pte.pte.pte_lo &= ~PTE_PP;
			pvo->pvo_pte.pte.pte_lo |= PTE_BR;
			if (pt != NULL) {
				moea_pte_synch(pt, &pvo->pvo_pte.pte);
				lo |= pvo->pvo_pte.pte.pte_lo;
				pvo->pvo_pte.pte.pte_lo &= ~PTE_CHG;
				moea_pte_change(pt, &pvo->pvo_pte.pte,
				    pvo->pvo_vaddr);
				mtx_unlock(&moea_table_mutex);
			}
		}
		PMAP_UNLOCK(pmap);
	}
	if ((lo & PTE_CHG) != 0) {
		moea_attr_clear(m, PTE_CHG);
		vm_page_dirty(m);
	}
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	rw_wunlock(&pvh_global_lock);
}

/*
 *	moea_ts_referenced:
 *
 *	Return a count of reference bits for a page, clearing those bits.
 *	It is not necessary for every reference bit to be cleared, but it
 *	is necessary that 0 only be returned when there are truly no
 *	reference bits set.
 *
 *	XXX: The exact number of bits to check and clear is a matter that
 *	should be tested and standardized at some point in the future for
 *	optimal aging of shared pages.
 */
int
moea_ts_referenced(mmu_t mmu, vm_page_t m)
{
	int count;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("moea_ts_referenced: page %p is not managed", m));
	rw_wlock(&pvh_global_lock);
	count = moea_clear_bit(m, PTE_REF);
	rw_wunlock(&pvh_global_lock);
	return (count);
}

/*
 * Modify the WIMG settings of all mappings for a page.
 */
void
moea_page_set_memattr(mmu_t mmu, vm_page_t m, vm_memattr_t ma)
{
	struct	pvo_entry *pvo;
	struct	pvo_head *pvo_head;
	struct	pte *pt;
	pmap_t	pmap;
	u_int	lo;

	if ((m->oflags & VPO_UNMANAGED) != 0) {
		m->md.mdpg_cache_attrs = ma;
		return;
	}

	rw_wlock(&pvh_global_lock);
	pvo_head = vm_page_to_pvoh(m);
	lo = moea_calc_wimg(VM_PAGE_TO_PHYS(m), ma);

	LIST_FOREACH(pvo, pvo_head, pvo_vlink) {
		pmap = pvo->pvo_pmap;
		PMAP_LOCK(pmap);
		pt = moea_pvo_to_pte(pvo, -1);
		pvo->pvo_pte.pte.pte_lo &= ~PTE_WIMG;
		pvo->pvo_pte.pte.pte_lo |= lo;
		if (pt != NULL) {
			moea_pte_change(pt, &pvo->pvo_pte.pte,
			    pvo->pvo_vaddr);
			if (pvo->pvo_pmap == kernel_pmap)
				isync();
		}
		mtx_unlock(&moea_table_mutex);
		PMAP_UNLOCK(pmap);
	}
	m->md.mdpg_cache_attrs = ma;
	rw_wunlock(&pvh_global_lock);
}

/*
 * Map a wired page into kernel virtual address space.
 */
void
moea_kenter(mmu_t mmu, vm_offset_t va, vm_paddr_t pa)
{

	moea_kenter_attr(mmu, va, pa, VM_MEMATTR_DEFAULT);
}

void
moea_kenter_attr(mmu_t mmu, vm_offset_t va, vm_paddr_t pa, vm_memattr_t ma)
{
	u_int		pte_lo;
	int		error;

#if 0
	if (va < VM_MIN_KERNEL_ADDRESS)
		panic("moea_kenter: attempt to enter non-kernel address %#x",
		    va);
#endif

	pte_lo = moea_calc_wimg(pa, ma);

	PMAP_LOCK(kernel_pmap);
	error = moea_pvo_enter(kernel_pmap, moea_upvo_zone,
	    &moea_pvo_kunmanaged, va, pa, pte_lo, PVO_WIRED);

	if (error != 0 && error != ENOENT)
		panic("moea_kenter: failed to enter va %#x pa %#x: %d", va,
		    pa, error);

	PMAP_UNLOCK(kernel_pmap);
}

/*
 * Extract the physical page address associated with the given kernel virtual
 * address.
 */
vm_paddr_t
moea_kextract(mmu_t mmu, vm_offset_t va)
{
	struct		pvo_entry *pvo;
	vm_paddr_t pa;

	/*
	 * Allow direct mappings on 32-bit OEA
	 */
	if (va < VM_MIN_KERNEL_ADDRESS) {
		return (va);
	}

	PMAP_LOCK(kernel_pmap);
	pvo = moea_pvo_find_va(kernel_pmap, va & ~ADDR_POFF, NULL);
	KASSERT(pvo != NULL, ("moea_kextract: no addr found"));
	pa = (pvo->pvo_pte.pte.pte_lo & PTE_RPGN) | (va & ADDR_POFF);
	PMAP_UNLOCK(kernel_pmap);
	return (pa);
}

/*
 * Remove a wired page from kernel virtual address space.
 */
void
moea_kremove(mmu_t mmu, vm_offset_t va)
{

	moea_remove(mmu, kernel_pmap, va, va + PAGE_SIZE);
}

/*
 * Provide a kernel pointer corresponding to a given userland pointer.
 * The returned pointer is valid until the next time this function is
 * called in this thread. This is used internally in copyin/copyout.
 */
int
moea_map_user_ptr(mmu_t mmu, pmap_t pm, volatile const void *uaddr,
    void **kaddr, size_t ulen, size_t *klen)
{
	size_t l;
	register_t vsid;

	*kaddr = (char *)USER_ADDR + ((uintptr_t)uaddr & ~SEGMENT_MASK);
	l = ((char *)USER_ADDR + SEGMENT_LENGTH) - (char *)(*kaddr);
	if (l > ulen)
		l = ulen;
	if (klen)
		*klen = l;
	else if (l != ulen)
		return (EFAULT);

	vsid = va_to_vsid(pm, (vm_offset_t)uaddr);
 
	/* Mark segment no-execute */
	vsid |= SR_N;
 
	/* If we have already set this VSID, we can just return */
	if (curthread->td_pcb->pcb_cpu.aim.usr_vsid == vsid)
		return (0);
 
	__asm __volatile("isync");
	curthread->td_pcb->pcb_cpu.aim.usr_segm =
	    (uintptr_t)uaddr >> ADDR_SR_SHFT;
	curthread->td_pcb->pcb_cpu.aim.usr_vsid = vsid;
	__asm __volatile("mtsr %0,%1; isync" :: "n"(USER_SR), "r"(vsid));

	return (0);
}

/*
 * Figure out where a given kernel pointer (usually in a fault) points
 * to from the VM's perspective, potentially remapping into userland's
 * address space.
 */
static int
moea_decode_kernel_ptr(mmu_t mmu, vm_offset_t addr, int *is_user,
    vm_offset_t *decoded_addr)
{
	vm_offset_t user_sr;

	if ((addr >> ADDR_SR_SHFT) == (USER_ADDR >> ADDR_SR_SHFT)) {
		user_sr = curthread->td_pcb->pcb_cpu.aim.usr_segm;
		addr &= ADDR_PIDX | ADDR_POFF;
		addr |= user_sr << ADDR_SR_SHFT;
		*decoded_addr = addr;
		*is_user = 1;
	} else {
		*decoded_addr = addr;
		*is_user = 0;
	}

	return (0);
}

/*
 * Map a range of physical addresses into kernel virtual address space.
 *
 * The value passed in *virt is a suggested virtual address for the mapping.
 * Architectures which can support a direct-mapped physical to virtual region
 * can return the appropriate address within that region, leaving '*virt'
 * unchanged.  We cannot and therefore do not; *virt is updated with the
 * first usable address after the mapped region.
 */
vm_offset_t
moea_map(mmu_t mmu, vm_offset_t *virt, vm_paddr_t pa_start,
    vm_paddr_t pa_end, int prot)
{
	vm_offset_t	sva, va;

	sva = *virt;
	va = sva;
	for (; pa_start < pa_end; pa_start += PAGE_SIZE, va += PAGE_SIZE)
		moea_kenter(mmu, va, pa_start);
	*virt = va;
	return (sva);
}

/*
 * Returns true if the pmap's pv is one of the first
 * 16 pvs linked to from this page.  This count may
 * be changed upwards or downwards in the future; it
 * is only necessary that true be returned for a small
 * subset of pmaps for proper page aging.
 */
boolean_t
moea_page_exists_quick(mmu_t mmu, pmap_t pmap, vm_page_t m)
{
        int loops;
	struct pvo_entry *pvo;
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("moea_page_exists_quick: page %p is not managed", m));
	loops = 0;
	rv = FALSE;
	rw_wlock(&pvh_global_lock);
	LIST_FOREACH(pvo, vm_page_to_pvoh(m), pvo_vlink) {
		if (pvo->pvo_pmap == pmap) {
			rv = TRUE;
			break;
		}
		if (++loops >= 16)
			break;
	}
	rw_wunlock(&pvh_global_lock);
	return (rv);
}

void
moea_page_init(mmu_t mmu __unused, vm_page_t m)
{

	m->md.mdpg_attrs = 0;
	m->md.mdpg_cache_attrs = VM_MEMATTR_DEFAULT;
	LIST_INIT(&m->md.mdpg_pvoh);
}

/*
 * Return the number of managed mappings to the given physical page
 * that are wired.
 */
int
moea_page_wired_mappings(mmu_t mmu, vm_page_t m)
{
	struct pvo_entry *pvo;
	int count;

	count = 0;
	if ((m->oflags & VPO_UNMANAGED) != 0)
		return (count);
	rw_wlock(&pvh_global_lock);
	LIST_FOREACH(pvo, vm_page_to_pvoh(m), pvo_vlink)
		if ((pvo->pvo_vaddr & PVO_WIRED) != 0)
			count++;
	rw_wunlock(&pvh_global_lock);
	return (count);
}

static u_int	moea_vsidcontext;

void
moea_pinit(mmu_t mmu, pmap_t pmap)
{
	int	i, mask;
	u_int	entropy;

	KASSERT((int)pmap < VM_MIN_KERNEL_ADDRESS, ("moea_pinit: virt pmap"));
	RB_INIT(&pmap->pmap_pvo);

	entropy = 0;
	__asm __volatile("mftb %0" : "=r"(entropy));

	if ((pmap->pmap_phys = (pmap_t)moea_kextract(mmu, (vm_offset_t)pmap))
	    == NULL) {
		pmap->pmap_phys = pmap;
	}


	mtx_lock(&moea_vsid_mutex);
	/*
	 * Allocate some segment registers for this pmap.
	 */
	for (i = 0; i < NPMAPS; i += VSID_NBPW) {
		u_int	hash, n;

		/*
		 * Create a new value by mutiplying by a prime and adding in
		 * entropy from the timebase register.  This is to make the
		 * VSID more random so that the PT hash function collides
		 * less often.  (Note that the prime casues gcc to do shifts
		 * instead of a multiply.)
		 */
		moea_vsidcontext = (moea_vsidcontext * 0x1105) + entropy;
		hash = moea_vsidcontext & (NPMAPS - 1);
		if (hash == 0)		/* 0 is special, avoid it */
			continue;
		n = hash >> 5;
		mask = 1 << (hash & (VSID_NBPW - 1));
		hash = (moea_vsidcontext & 0xfffff);
		if (moea_vsid_bitmap[n] & mask) {	/* collision? */
			/* anything free in this bucket? */
			if (moea_vsid_bitmap[n] == 0xffffffff) {
				entropy = (moea_vsidcontext >> 20);
				continue;
			}
			i = ffs(~moea_vsid_bitmap[n]) - 1;
			mask = 1 << i;
			hash &= rounddown2(0xfffff, VSID_NBPW);
			hash |= i;
		}
		KASSERT(!(moea_vsid_bitmap[n] & mask),
		    ("Allocating in-use VSID group %#x\n", hash));
		moea_vsid_bitmap[n] |= mask;
		for (i = 0; i < 16; i++)
			pmap->pm_sr[i] = VSID_MAKE(i, hash);
		mtx_unlock(&moea_vsid_mutex);
		return;
	}

	mtx_unlock(&moea_vsid_mutex);
	panic("moea_pinit: out of segments");
}

/*
 * Initialize the pmap associated with process 0.
 */
void
moea_pinit0(mmu_t mmu, pmap_t pm)
{

	PMAP_LOCK_INIT(pm);
	moea_pinit(mmu, pm);
	bzero(&pm->pm_stats, sizeof(pm->pm_stats));
}

/*
 * Set the physical protection on the specified range of this map as requested.
 */
void
moea_protect(mmu_t mmu, pmap_t pm, vm_offset_t sva, vm_offset_t eva,
    vm_prot_t prot)
{
	struct	pvo_entry *pvo, *tpvo, key;
	struct	pte *pt;

	KASSERT(pm == &curproc->p_vmspace->vm_pmap || pm == kernel_pmap,
	    ("moea_protect: non current pmap"));

	if ((prot & VM_PROT_READ) == VM_PROT_NONE) {
		moea_remove(mmu, pm, sva, eva);
		return;
	}

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pm);
	key.pvo_vaddr = sva;
	for (pvo = RB_NFIND(pvo_tree, &pm->pmap_pvo, &key);
	    pvo != NULL && PVO_VADDR(pvo) < eva; pvo = tpvo) {
		tpvo = RB_NEXT(pvo_tree, &pm->pmap_pvo, pvo);

		/*
		 * Grab the PTE pointer before we diddle with the cached PTE
		 * copy.
		 */
		pt = moea_pvo_to_pte(pvo, -1);
		/*
		 * Change the protection of the page.
		 */
		pvo->pvo_pte.pte.pte_lo &= ~PTE_PP;
		pvo->pvo_pte.pte.pte_lo |= PTE_BR;

		/*
		 * If the PVO is in the page table, update that pte as well.
		 */
		if (pt != NULL) {
			moea_pte_change(pt, &pvo->pvo_pte.pte, pvo->pvo_vaddr);
			mtx_unlock(&moea_table_mutex);
		}
	}
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pm);
}

/*
 * Map a list of wired pages into kernel virtual address space.  This is
 * intended for temporary mappings which do not need page modification or
 * references recorded.  Existing mappings in the region are overwritten.
 */
void
moea_qenter(mmu_t mmu, vm_offset_t sva, vm_page_t *m, int count)
{
	vm_offset_t va;

	va = sva;
	while (count-- > 0) {
		moea_kenter(mmu, va, VM_PAGE_TO_PHYS(*m));
		va += PAGE_SIZE;
		m++;
	}
}

/*
 * Remove page mappings from kernel virtual address space.  Intended for
 * temporary mappings entered by moea_qenter.
 */
void
moea_qremove(mmu_t mmu, vm_offset_t sva, int count)
{
	vm_offset_t va;

	va = sva;
	while (count-- > 0) {
		moea_kremove(mmu, va);
		va += PAGE_SIZE;
	}
}

void
moea_release(mmu_t mmu, pmap_t pmap)
{
        int idx, mask;

	/*
	 * Free segment register's VSID
	 */
        if (pmap->pm_sr[0] == 0)
                panic("moea_release");

	mtx_lock(&moea_vsid_mutex);
        idx = VSID_TO_HASH(pmap->pm_sr[0]) & (NPMAPS-1);
        mask = 1 << (idx % VSID_NBPW);
        idx /= VSID_NBPW;
        moea_vsid_bitmap[idx] &= ~mask;
	mtx_unlock(&moea_vsid_mutex);
}

/*
 * Remove the given range of addresses from the specified map.
 */
void
moea_remove(mmu_t mmu, pmap_t pm, vm_offset_t sva, vm_offset_t eva)
{
	struct	pvo_entry *pvo, *tpvo, key;

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pm);
	key.pvo_vaddr = sva;
	for (pvo = RB_NFIND(pvo_tree, &pm->pmap_pvo, &key);
	    pvo != NULL && PVO_VADDR(pvo) < eva; pvo = tpvo) {
		tpvo = RB_NEXT(pvo_tree, &pm->pmap_pvo, pvo);
		moea_pvo_remove(pvo, -1);
	}
	PMAP_UNLOCK(pm);
	rw_wunlock(&pvh_global_lock);
}

/*
 * Remove physical page from all pmaps in which it resides. moea_pvo_remove()
 * will reflect changes in pte's back to the vm_page.
 */
void
moea_remove_all(mmu_t mmu, vm_page_t m)
{
	struct  pvo_head *pvo_head;
	struct	pvo_entry *pvo, *next_pvo;
	pmap_t	pmap;

	rw_wlock(&pvh_global_lock);
	pvo_head = vm_page_to_pvoh(m);
	for (pvo = LIST_FIRST(pvo_head); pvo != NULL; pvo = next_pvo) {
		next_pvo = LIST_NEXT(pvo, pvo_vlink);

		pmap = pvo->pvo_pmap;
		PMAP_LOCK(pmap);
		moea_pvo_remove(pvo, -1);
		PMAP_UNLOCK(pmap);
	}
	if ((m->aflags & PGA_WRITEABLE) && moea_query_bit(m, PTE_CHG)) {
		moea_attr_clear(m, PTE_CHG);
		vm_page_dirty(m);
	}
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	rw_wunlock(&pvh_global_lock);
}

/*
 * Allocate a physical page of memory directly from the phys_avail map.
 * Can only be called from moea_bootstrap before avail start and end are
 * calculated.
 */
static vm_offset_t
moea_bootstrap_alloc(vm_size_t size, u_int align)
{
	vm_offset_t	s, e;
	int		i, j;

	size = round_page(size);
	for (i = 0; phys_avail[i + 1] != 0; i += 2) {
		if (align != 0)
			s = roundup2(phys_avail[i], align);
		else
			s = phys_avail[i];
		e = s + size;

		if (s < phys_avail[i] || e > phys_avail[i + 1])
			continue;

		if (s == phys_avail[i]) {
			phys_avail[i] += size;
		} else if (e == phys_avail[i + 1]) {
			phys_avail[i + 1] -= size;
		} else {
			for (j = phys_avail_count * 2; j > i; j -= 2) {
				phys_avail[j] = phys_avail[j - 2];
				phys_avail[j + 1] = phys_avail[j - 1];
			}

			phys_avail[i + 3] = phys_avail[i + 1];
			phys_avail[i + 1] = s;
			phys_avail[i + 2] = e;
			phys_avail_count++;
		}

		return (s);
	}
	panic("moea_bootstrap_alloc: could not allocate memory");
}

static void
moea_syncicache(vm_paddr_t pa, vm_size_t len)
{
	__syncicache((void *)pa, len);
}

static int
moea_pvo_enter(pmap_t pm, uma_zone_t zone, struct pvo_head *pvo_head,
    vm_offset_t va, vm_paddr_t pa, u_int pte_lo, int flags)
{
	struct	pvo_entry *pvo;
	u_int	sr;
	int	first;
	u_int	ptegidx;
	int	i;
	int     bootstrap;

	moea_pvo_enter_calls++;
	first = 0;
	bootstrap = 0;

	/*
	 * Compute the PTE Group index.
	 */
	va &= ~ADDR_POFF;
	sr = va_to_sr(pm->pm_sr, va);
	ptegidx = va_to_pteg(sr, va);

	/*
	 * Remove any existing mapping for this page.  Reuse the pvo entry if
	 * there is a mapping.
	 */
	mtx_lock(&moea_table_mutex);
	LIST_FOREACH(pvo, &moea_pvo_table[ptegidx], pvo_olink) {
		if (pvo->pvo_pmap == pm && PVO_VADDR(pvo) == va) {
			if ((pvo->pvo_pte.pte.pte_lo & PTE_RPGN) == pa &&
			    (pvo->pvo_pte.pte.pte_lo & PTE_PP) ==
			    (pte_lo & PTE_PP)) {
				/*
				 * The PTE is not changing.  Instead, this may
				 * be a request to change the mapping's wired
				 * attribute.
				 */
				mtx_unlock(&moea_table_mutex);
				if ((flags & PVO_WIRED) != 0 &&
				    (pvo->pvo_vaddr & PVO_WIRED) == 0) {
					pvo->pvo_vaddr |= PVO_WIRED;
					pm->pm_stats.wired_count++;
				} else if ((flags & PVO_WIRED) == 0 &&
				    (pvo->pvo_vaddr & PVO_WIRED) != 0) {
					pvo->pvo_vaddr &= ~PVO_WIRED;
					pm->pm_stats.wired_count--;
				}
				return (0);
			}
			moea_pvo_remove(pvo, -1);
			break;
		}
	}

	/*
	 * If we aren't overwriting a mapping, try to allocate.
	 */
	if (moea_initialized) {
		pvo = uma_zalloc(zone, M_NOWAIT);
	} else {
		if (moea_bpvo_pool_index >= BPVO_POOL_SIZE) {
			panic("moea_enter: bpvo pool exhausted, %d, %d, %d",
			      moea_bpvo_pool_index, BPVO_POOL_SIZE,
			      BPVO_POOL_SIZE * sizeof(struct pvo_entry));
		}
		pvo = &moea_bpvo_pool[moea_bpvo_pool_index];
		moea_bpvo_pool_index++;
		bootstrap = 1;
	}

	if (pvo == NULL) {
		mtx_unlock(&moea_table_mutex);
		return (ENOMEM);
	}

	moea_pvo_entries++;
	pvo->pvo_vaddr = va;
	pvo->pvo_pmap = pm;
	LIST_INSERT_HEAD(&moea_pvo_table[ptegidx], pvo, pvo_olink);
	pvo->pvo_vaddr &= ~ADDR_POFF;
	if (flags & PVO_WIRED)
		pvo->pvo_vaddr |= PVO_WIRED;
	if (pvo_head != &moea_pvo_kunmanaged)
		pvo->pvo_vaddr |= PVO_MANAGED;
	if (bootstrap)
		pvo->pvo_vaddr |= PVO_BOOTSTRAP;

	moea_pte_create(&pvo->pvo_pte.pte, sr, va, pa | pte_lo);

	/*
	 * Add to pmap list
	 */
	RB_INSERT(pvo_tree, &pm->pmap_pvo, pvo);

	/*
	 * Remember if the list was empty and therefore will be the first
	 * item.
	 */
	if (LIST_FIRST(pvo_head) == NULL)
		first = 1;
	LIST_INSERT_HEAD(pvo_head, pvo, pvo_vlink);

	if (pvo->pvo_vaddr & PVO_WIRED)
		pm->pm_stats.wired_count++;
	pm->pm_stats.resident_count++;

	i = moea_pte_insert(ptegidx, &pvo->pvo_pte.pte);
	KASSERT(i < 8, ("Invalid PTE index"));
	if (i >= 0) {
		PVO_PTEGIDX_SET(pvo, i);
	} else {
		panic("moea_pvo_enter: overflow");
		moea_pte_overflow++;
	}
	mtx_unlock(&moea_table_mutex);

	return (first ? ENOENT : 0);
}

static void
moea_pvo_remove(struct pvo_entry *pvo, int pteidx)
{
	struct	pte *pt;

	/*
	 * If there is an active pte entry, we need to deactivate it (and
	 * save the ref & cfg bits).
	 */
	pt = moea_pvo_to_pte(pvo, pteidx);
	if (pt != NULL) {
		moea_pte_unset(pt, &pvo->pvo_pte.pte, pvo->pvo_vaddr);
		mtx_unlock(&moea_table_mutex);
		PVO_PTEGIDX_CLR(pvo);
	} else {
		moea_pte_overflow--;
	}

	/*
	 * Update our statistics.
	 */
	pvo->pvo_pmap->pm_stats.resident_count--;
	if (pvo->pvo_vaddr & PVO_WIRED)
		pvo->pvo_pmap->pm_stats.wired_count--;

	/*
	 * Save the REF/CHG bits into their cache if the page is managed.
	 */
	if ((pvo->pvo_vaddr & PVO_MANAGED) == PVO_MANAGED) {
		struct	vm_page *pg;

		pg = PHYS_TO_VM_PAGE(pvo->pvo_pte.pte.pte_lo & PTE_RPGN);
		if (pg != NULL) {
			moea_attr_save(pg, pvo->pvo_pte.pte.pte_lo &
			    (PTE_REF | PTE_CHG));
		}
	}

	/*
	 * Remove this PVO from the PV and pmap lists.
	 */
	LIST_REMOVE(pvo, pvo_vlink);
	RB_REMOVE(pvo_tree, &pvo->pvo_pmap->pmap_pvo, pvo);

	/*
	 * Remove this from the overflow list and return it to the pool
	 * if we aren't going to reuse it.
	 */
	LIST_REMOVE(pvo, pvo_olink);
	if (!(pvo->pvo_vaddr & PVO_BOOTSTRAP))
		uma_zfree(pvo->pvo_vaddr & PVO_MANAGED ? moea_mpvo_zone :
		    moea_upvo_zone, pvo);
	moea_pvo_entries--;
	moea_pvo_remove_calls++;
}

static __inline int
moea_pvo_pte_index(const struct pvo_entry *pvo, int ptegidx)
{
	int	pteidx;

	/*
	 * We can find the actual pte entry without searching by grabbing
	 * the PTEG index from 3 unused bits in pte_lo[11:9] and by
	 * noticing the HID bit.
	 */
	pteidx = ptegidx * 8 + PVO_PTEGIDX_GET(pvo);
	if (pvo->pvo_pte.pte.pte_hi & PTE_HID)
		pteidx ^= moea_pteg_mask * 8;

	return (pteidx);
}

static struct pvo_entry *
moea_pvo_find_va(pmap_t pm, vm_offset_t va, int *pteidx_p)
{
	struct	pvo_entry *pvo;
	int	ptegidx;
	u_int	sr;

	va &= ~ADDR_POFF;
	sr = va_to_sr(pm->pm_sr, va);
	ptegidx = va_to_pteg(sr, va);

	mtx_lock(&moea_table_mutex);
	LIST_FOREACH(pvo, &moea_pvo_table[ptegidx], pvo_olink) {
		if (pvo->pvo_pmap == pm && PVO_VADDR(pvo) == va) {
			if (pteidx_p)
				*pteidx_p = moea_pvo_pte_index(pvo, ptegidx);
			break;
		}
	}
	mtx_unlock(&moea_table_mutex);

	return (pvo);
}

static struct pte *
moea_pvo_to_pte(const struct pvo_entry *pvo, int pteidx)
{
	struct	pte *pt;

	/*
	 * If we haven't been supplied the ptegidx, calculate it.
	 */
	if (pteidx == -1) {
		int	ptegidx;
		u_int	sr;

		sr = va_to_sr(pvo->pvo_pmap->pm_sr, pvo->pvo_vaddr);
		ptegidx = va_to_pteg(sr, pvo->pvo_vaddr);
		pteidx = moea_pvo_pte_index(pvo, ptegidx);
	}

	pt = &moea_pteg_table[pteidx >> 3].pt[pteidx & 7];
	mtx_lock(&moea_table_mutex);

	if ((pvo->pvo_pte.pte.pte_hi & PTE_VALID) && !PVO_PTEGIDX_ISSET(pvo)) {
		panic("moea_pvo_to_pte: pvo %p has valid pte in pvo but no "
		    "valid pte index", pvo);
	}

	if ((pvo->pvo_pte.pte.pte_hi & PTE_VALID) == 0 && PVO_PTEGIDX_ISSET(pvo)) {
		panic("moea_pvo_to_pte: pvo %p has valid pte index in pvo "
		    "pvo but no valid pte", pvo);
	}

	if ((pt->pte_hi ^ (pvo->pvo_pte.pte.pte_hi & ~PTE_VALID)) == PTE_VALID) {
		if ((pvo->pvo_pte.pte.pte_hi & PTE_VALID) == 0) {
			panic("moea_pvo_to_pte: pvo %p has valid pte in "
			    "moea_pteg_table %p but invalid in pvo", pvo, pt);
		}

		if (((pt->pte_lo ^ pvo->pvo_pte.pte.pte_lo) & ~(PTE_CHG|PTE_REF))
		    != 0) {
			panic("moea_pvo_to_pte: pvo %p pte does not match "
			    "pte %p in moea_pteg_table", pvo, pt);
		}

		mtx_assert(&moea_table_mutex, MA_OWNED);
		return (pt);
	}

	if (pvo->pvo_pte.pte.pte_hi & PTE_VALID) {
		panic("moea_pvo_to_pte: pvo %p has invalid pte %p in "
		    "moea_pteg_table but valid in pvo: %8x, %8x", pvo, pt, pvo->pvo_pte.pte.pte_hi, pt->pte_hi);
	}

	mtx_unlock(&moea_table_mutex);
	return (NULL);
}

/*
 * XXX: THIS STUFF SHOULD BE IN pte.c?
 */
int
moea_pte_spill(vm_offset_t addr)
{
	struct	pvo_entry *source_pvo, *victim_pvo;
	struct	pvo_entry *pvo;
	int	ptegidx, i, j;
	u_int	sr;
	struct	pteg *pteg;
	struct	pte *pt;

	moea_pte_spills++;

	sr = mfsrin(addr);
	ptegidx = va_to_pteg(sr, addr);

	/*
	 * Have to substitute some entry.  Use the primary hash for this.
	 * Use low bits of timebase as random generator.
	 */
	pteg = &moea_pteg_table[ptegidx];
	mtx_lock(&moea_table_mutex);
	__asm __volatile("mftb %0" : "=r"(i));
	i &= 7;
	pt = &pteg->pt[i];

	source_pvo = NULL;
	victim_pvo = NULL;
	LIST_FOREACH(pvo, &moea_pvo_table[ptegidx], pvo_olink) {
		/*
		 * We need to find a pvo entry for this address.
		 */
		if (source_pvo == NULL &&
		    moea_pte_match(&pvo->pvo_pte.pte, sr, addr,
		    pvo->pvo_pte.pte.pte_hi & PTE_HID)) {
			/*
			 * Now found an entry to be spilled into the pteg.
			 * The PTE is now valid, so we know it's active.
			 */
			j = moea_pte_insert(ptegidx, &pvo->pvo_pte.pte);

			if (j >= 0) {
				PVO_PTEGIDX_SET(pvo, j);
				moea_pte_overflow--;
				mtx_unlock(&moea_table_mutex);
				return (1);
			}

			source_pvo = pvo;

			if (victim_pvo != NULL)
				break;
		}

		/*
		 * We also need the pvo entry of the victim we are replacing
		 * so save the R & C bits of the PTE.
		 */
		if ((pt->pte_hi & PTE_HID) == 0 && victim_pvo == NULL &&
		    moea_pte_compare(pt, &pvo->pvo_pte.pte)) {
			victim_pvo = pvo;
			if (source_pvo != NULL)
				break;
		}
	}

	if (source_pvo == NULL) {
		mtx_unlock(&moea_table_mutex);
		return (0);
	}

	if (victim_pvo == NULL) {
		if ((pt->pte_hi & PTE_HID) == 0)
			panic("moea_pte_spill: victim p-pte (%p) has no pvo"
			    "entry", pt);

		/*
		 * If this is a secondary PTE, we need to search it's primary
		 * pvo bucket for the matching PVO.
		 */
		LIST_FOREACH(pvo, &moea_pvo_table[ptegidx ^ moea_pteg_mask],
		    pvo_olink) {
			/*
			 * We also need the pvo entry of the victim we are
			 * replacing so save the R & C bits of the PTE.
			 */
			if (moea_pte_compare(pt, &pvo->pvo_pte.pte)) {
				victim_pvo = pvo;
				break;
			}
		}

		if (victim_pvo == NULL)
			panic("moea_pte_spill: victim s-pte (%p) has no pvo"
			    "entry", pt);
	}

	/*
	 * We are invalidating the TLB entry for the EA we are replacing even
	 * though it's valid.  If we don't, we lose any ref/chg bit changes
	 * contained in the TLB entry.
	 */
	source_pvo->pvo_pte.pte.pte_hi &= ~PTE_HID;

	moea_pte_unset(pt, &victim_pvo->pvo_pte.pte, victim_pvo->pvo_vaddr);
	moea_pte_set(pt, &source_pvo->pvo_pte.pte);

	PVO_PTEGIDX_CLR(victim_pvo);
	PVO_PTEGIDX_SET(source_pvo, i);
	moea_pte_replacements++;

	mtx_unlock(&moea_table_mutex);
	return (1);
}

static __inline struct pvo_entry *
moea_pte_spillable_ident(u_int ptegidx)
{
	struct	pte *pt;
	struct	pvo_entry *pvo_walk, *pvo = NULL;

	LIST_FOREACH(pvo_walk, &moea_pvo_table[ptegidx], pvo_olink) {
		if (pvo_walk->pvo_vaddr & PVO_WIRED)
			continue;

		if (!(pvo_walk->pvo_pte.pte.pte_hi & PTE_VALID))
			continue;

		pt = moea_pvo_to_pte(pvo_walk, -1);

		if (pt == NULL)
			continue;

		pvo = pvo_walk;

		mtx_unlock(&moea_table_mutex);
		if (!(pt->pte_lo & PTE_REF))
			return (pvo_walk);
	}

	return (pvo);
}

static int
moea_pte_insert(u_int ptegidx, struct pte *pvo_pt)
{
	struct	pte *pt;
	struct	pvo_entry *victim_pvo;
	int	i;
	int	victim_idx;
	u_int	pteg_bkpidx = ptegidx;

	mtx_assert(&moea_table_mutex, MA_OWNED);

	/*
	 * First try primary hash.
	 */
	for (pt = moea_pteg_table[ptegidx].pt, i = 0; i < 8; i++, pt++) {
		if ((pt->pte_hi & PTE_VALID) == 0) {
			pvo_pt->pte_hi &= ~PTE_HID;
			moea_pte_set(pt, pvo_pt);
			return (i);
		}
	}

	/*
	 * Now try secondary hash.
	 */
	ptegidx ^= moea_pteg_mask;

	for (pt = moea_pteg_table[ptegidx].pt, i = 0; i < 8; i++, pt++) {
		if ((pt->pte_hi & PTE_VALID) == 0) {
			pvo_pt->pte_hi |= PTE_HID;
			moea_pte_set(pt, pvo_pt);
			return (i);
		}
	}

	/* Try again, but this time try to force a PTE out. */
	ptegidx = pteg_bkpidx;

	victim_pvo = moea_pte_spillable_ident(ptegidx);
	if (victim_pvo == NULL) {
		ptegidx ^= moea_pteg_mask;
		victim_pvo = moea_pte_spillable_ident(ptegidx);
	}

	if (victim_pvo == NULL) {
		panic("moea_pte_insert: overflow");
		return (-1);
	}

	victim_idx = moea_pvo_pte_index(victim_pvo, ptegidx);

	if (pteg_bkpidx == ptegidx)
		pvo_pt->pte_hi &= ~PTE_HID;
	else
		pvo_pt->pte_hi |= PTE_HID;

	/*
	 * Synchronize the sacrifice PTE with its PVO, then mark both
	 * invalid. The PVO will be reused when/if the VM system comes
	 * here after a fault.
	 */
	pt = &moea_pteg_table[victim_idx >> 3].pt[victim_idx & 7];

	if (pt->pte_hi != victim_pvo->pvo_pte.pte.pte_hi)
	    panic("Victim PVO doesn't match PTE! PVO: %8x, PTE: %8x", victim_pvo->pvo_pte.pte.pte_hi, pt->pte_hi);

	/*
	 * Set the new PTE.
	 */
	moea_pte_unset(pt, &victim_pvo->pvo_pte.pte, victim_pvo->pvo_vaddr);
	PVO_PTEGIDX_CLR(victim_pvo);
	moea_pte_overflow++;
	moea_pte_set(pt, pvo_pt);

	return (victim_idx & 7);
}

static boolean_t
moea_query_bit(vm_page_t m, int ptebit)
{
	struct	pvo_entry *pvo;
	struct	pte *pt;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	if (moea_attr_fetch(m) & ptebit)
		return (TRUE);

	LIST_FOREACH(pvo, vm_page_to_pvoh(m), pvo_vlink) {

		/*
		 * See if we saved the bit off.  If so, cache it and return
		 * success.
		 */
		if (pvo->pvo_pte.pte.pte_lo & ptebit) {
			moea_attr_save(m, ptebit);
			return (TRUE);
		}
	}

	/*
	 * No luck, now go through the hard part of looking at the PTEs
	 * themselves.  Sync so that any pending REF/CHG bits are flushed to
	 * the PTEs.
	 */
	powerpc_sync();
	LIST_FOREACH(pvo, vm_page_to_pvoh(m), pvo_vlink) {

		/*
		 * See if this pvo has a valid PTE.  if so, fetch the
		 * REF/CHG bits from the valid PTE.  If the appropriate
		 * ptebit is set, cache it and return success.
		 */
		pt = moea_pvo_to_pte(pvo, -1);
		if (pt != NULL) {
			moea_pte_synch(pt, &pvo->pvo_pte.pte);
			mtx_unlock(&moea_table_mutex);
			if (pvo->pvo_pte.pte.pte_lo & ptebit) {
				moea_attr_save(m, ptebit);
				return (TRUE);
			}
		}
	}

	return (FALSE);
}

static u_int
moea_clear_bit(vm_page_t m, int ptebit)
{
	u_int	count;
	struct	pvo_entry *pvo;
	struct	pte *pt;

	rw_assert(&pvh_global_lock, RA_WLOCKED);

	/*
	 * Clear the cached value.
	 */
	moea_attr_clear(m, ptebit);

	/*
	 * Sync so that any pending REF/CHG bits are flushed to the PTEs (so
	 * we can reset the right ones).  note that since the pvo entries and
	 * list heads are accessed via BAT0 and are never placed in the page
	 * table, we don't have to worry about further accesses setting the
	 * REF/CHG bits.
	 */
	powerpc_sync();

	/*
	 * For each pvo entry, clear the pvo's ptebit.  If this pvo has a
	 * valid pte clear the ptebit from the valid pte.
	 */
	count = 0;
	LIST_FOREACH(pvo, vm_page_to_pvoh(m), pvo_vlink) {
		pt = moea_pvo_to_pte(pvo, -1);
		if (pt != NULL) {
			moea_pte_synch(pt, &pvo->pvo_pte.pte);
			if (pvo->pvo_pte.pte.pte_lo & ptebit) {
				count++;
				moea_pte_clear(pt, PVO_VADDR(pvo), ptebit);
			}
			mtx_unlock(&moea_table_mutex);
		}
		pvo->pvo_pte.pte.pte_lo &= ~ptebit;
	}

	return (count);
}

/*
 * Return true if the physical range is encompassed by the battable[idx]
 */
static int
moea_bat_mapped(int idx, vm_paddr_t pa, vm_size_t size)
{
	u_int prot;
	u_int32_t start;
	u_int32_t end;
	u_int32_t bat_ble;

	/*
	 * Return immediately if not a valid mapping
	 */
	if (!(battable[idx].batu & BAT_Vs))
		return (EINVAL);

	/*
	 * The BAT entry must be cache-inhibited, guarded, and r/w
	 * so it can function as an i/o page
	 */
	prot = battable[idx].batl & (BAT_I|BAT_G|BAT_PP_RW);
	if (prot != (BAT_I|BAT_G|BAT_PP_RW))
		return (EPERM);

	/*
	 * The address should be within the BAT range. Assume that the
	 * start address in the BAT has the correct alignment (thus
	 * not requiring masking)
	 */
	start = battable[idx].batl & BAT_PBS;
	bat_ble = (battable[idx].batu & ~(BAT_EBS)) | 0x03;
	end = start | (bat_ble << 15) | 0x7fff;

	if ((pa < start) || ((pa + size) > end))
		return (ERANGE);

	return (0);
}

boolean_t
moea_dev_direct_mapped(mmu_t mmu, vm_paddr_t pa, vm_size_t size)
{
	int i;

	/*
	 * This currently does not work for entries that
	 * overlap 256M BAT segments.
	 */

	for(i = 0; i < 16; i++)
		if (moea_bat_mapped(i, pa, size) == 0)
			return (0);

	return (EFAULT);
}

/*
 * Map a set of physical memory pages into the kernel virtual
 * address space. Return a pointer to where it is mapped. This
 * routine is intended to be used for mapping device memory,
 * NOT real memory.
 */
void *
moea_mapdev(mmu_t mmu, vm_paddr_t pa, vm_size_t size)
{

	return (moea_mapdev_attr(mmu, pa, size, VM_MEMATTR_DEFAULT));
}

void *
moea_mapdev_attr(mmu_t mmu, vm_paddr_t pa, vm_size_t size, vm_memattr_t ma)
{
	vm_offset_t va, tmpva, ppa, offset;
	int i;

	ppa = trunc_page(pa);
	offset = pa & PAGE_MASK;
	size = roundup(offset + size, PAGE_SIZE);

	/*
	 * If the physical address lies within a valid BAT table entry,
	 * return the 1:1 mapping. This currently doesn't work
	 * for regions that overlap 256M BAT segments.
	 */
	for (i = 0; i < 16; i++) {
		if (moea_bat_mapped(i, pa, size) == 0)
			return ((void *) pa);
	}

	va = kva_alloc(size);
	if (!va)
		panic("moea_mapdev: Couldn't alloc kernel virtual memory");

	for (tmpva = va; size > 0;) {
		moea_kenter_attr(mmu, tmpva, ppa, ma);
		tlbie(tmpva);
		size -= PAGE_SIZE;
		tmpva += PAGE_SIZE;
		ppa += PAGE_SIZE;
	}

	return ((void *)(va + offset));
}

void
moea_unmapdev(mmu_t mmu, vm_offset_t va, vm_size_t size)
{
	vm_offset_t base, offset;

	/*
	 * If this is outside kernel virtual space, then it's a
	 * battable entry and doesn't require unmapping
	 */
	if ((va >= VM_MIN_KERNEL_ADDRESS) && (va <= virtual_end)) {
		base = trunc_page(va);
		offset = va & PAGE_MASK;
		size = roundup(offset + size, PAGE_SIZE);
		kva_free(base, size);
	}
}

static void
moea_sync_icache(mmu_t mmu, pmap_t pm, vm_offset_t va, vm_size_t sz)
{
	struct pvo_entry *pvo;
	vm_offset_t lim;
	vm_paddr_t pa;
	vm_size_t len;

	PMAP_LOCK(pm);
	while (sz > 0) {
		lim = round_page(va);
		len = MIN(lim - va, sz);
		pvo = moea_pvo_find_va(pm, va & ~ADDR_POFF, NULL);
		if (pvo != NULL) {
			pa = (pvo->pvo_pte.pte.pte_lo & PTE_RPGN) |
			    (va & ADDR_POFF);
			moea_syncicache(pa, len);
		}
		va += len;
		sz -= len;
	}
	PMAP_UNLOCK(pm);
}

void
moea_dumpsys_map(mmu_t mmu, vm_paddr_t pa, size_t sz, void **va)
{

	*va = (void *)pa;
}

extern struct dump_pa dump_map[PHYS_AVAIL_SZ + 1];

void
moea_scan_init(mmu_t mmu)
{
	struct pvo_entry *pvo;
	vm_offset_t va;
	int i;

	if (!do_minidump) {
		/* Initialize phys. segments for dumpsys(). */
		memset(&dump_map, 0, sizeof(dump_map));
		mem_regions(&pregions, &pregions_sz, &regions, &regions_sz);
		for (i = 0; i < pregions_sz; i++) {
			dump_map[i].pa_start = pregions[i].mr_start;
			dump_map[i].pa_size = pregions[i].mr_size;
		}
		return;
	}

	/* Virtual segments for minidumps: */
	memset(&dump_map, 0, sizeof(dump_map));

	/* 1st: kernel .data and .bss. */
	dump_map[0].pa_start = trunc_page((uintptr_t)_etext);
	dump_map[0].pa_size =
	    round_page((uintptr_t)_end) - dump_map[0].pa_start;

	/* 2nd: msgbuf and tables (see pmap_bootstrap()). */
	dump_map[1].pa_start = (vm_paddr_t)msgbufp->msg_ptr;
	dump_map[1].pa_size = round_page(msgbufp->msg_size);

	/* 3rd: kernel VM. */
	va = dump_map[1].pa_start + dump_map[1].pa_size;
	/* Find start of next chunk (from va). */
	while (va < virtual_end) {
		/* Don't dump the buffer cache. */
		if (va >= kmi.buffer_sva && va < kmi.buffer_eva) {
			va = kmi.buffer_eva;
			continue;
		}
		pvo = moea_pvo_find_va(kernel_pmap, va & ~ADDR_POFF, NULL);
		if (pvo != NULL && (pvo->pvo_pte.pte.pte_hi & PTE_VALID))
			break;
		va += PAGE_SIZE;
	}
	if (va < virtual_end) {
		dump_map[2].pa_start = va;
		va += PAGE_SIZE;
		/* Find last page in chunk. */
		while (va < virtual_end) {
			/* Don't run into the buffer cache. */
			if (va == kmi.buffer_sva)
				break;
			pvo = moea_pvo_find_va(kernel_pmap, va & ~ADDR_POFF,
			    NULL);
			if (pvo == NULL ||
			    !(pvo->pvo_pte.pte.pte_hi & PTE_VALID))
				break;
			va += PAGE_SIZE;
		}
		dump_map[2].pa_size = va - dump_map[2].pa_start;
	}
}
