/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2015 Nathan Whitehorn
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/vmmeter.h>
#include <sys/smp.h>

#include <sys/kdb.h>

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

#include <machine/_inttypes.h>
#include <machine/cpu.h>
#include <machine/platform.h>
#include <machine/frame.h>
#include <machine/md_var.h>
#include <machine/psl.h>
#include <machine/bat.h>
#include <machine/hid.h>
#include <machine/pte.h>
#include <machine/sr.h>
#include <machine/trap.h>
#include <machine/mmuvar.h>

#include "mmu_oea64.h"
#include "mmu_if.h"
#include "moea64_if.h"

void moea64_release_vsid(uint64_t vsid);
uintptr_t moea64_get_unique_vsid(void); 

#define DISABLE_TRANS(msr)	msr = mfmsr(); mtmsr(msr & ~PSL_DR)
#define ENABLE_TRANS(msr)	mtmsr(msr)

#define	VSID_MAKE(sr, hash)	((sr) | (((hash) & 0xfffff) << 4))
#define	VSID_TO_HASH(vsid)	(((vsid) >> 4) & 0xfffff)
#define	VSID_HASH_MASK		0x0000007fffffffffULL

/*
 * Locking semantics:
 * 
 * There are two locks of interest: the page locks and the pmap locks, which
 * protect their individual PVO lists and are locked in that order. The contents
 * of all PVO entries are protected by the locks of their respective pmaps.
 * The pmap of any PVO is guaranteed not to change so long as the PVO is linked
 * into any list.
 *
 */

#define PV_LOCK_COUNT	PA_LOCK_COUNT*3
static struct mtx_padalign pv_lock[PV_LOCK_COUNT];
 
#define PV_LOCKPTR(pa)	((struct mtx *)(&pv_lock[pa_index(pa) % PV_LOCK_COUNT]))
#define PV_LOCK(pa)		mtx_lock(PV_LOCKPTR(pa))
#define PV_UNLOCK(pa)		mtx_unlock(PV_LOCKPTR(pa))
#define PV_LOCKASSERT(pa) 	mtx_assert(PV_LOCKPTR(pa), MA_OWNED)
#define PV_PAGE_LOCK(m)		PV_LOCK(VM_PAGE_TO_PHYS(m))
#define PV_PAGE_UNLOCK(m)	PV_UNLOCK(VM_PAGE_TO_PHYS(m))
#define PV_PAGE_LOCKASSERT(m)	PV_LOCKASSERT(VM_PAGE_TO_PHYS(m))

struct ofw_map {
	cell_t	om_va;
	cell_t	om_len;
	uint64_t om_pa;
	cell_t	om_mode;
};

extern unsigned char _etext[];
extern unsigned char _end[];

extern void *slbtrap, *slbtrapend;

/*
 * Map of physical memory regions.
 */
static struct	mem_region *regions;
static struct	mem_region *pregions;
static u_int	phys_avail_count;
static int	regions_sz, pregions_sz;

extern void bs_remap_earlyboot(void);

/*
 * Lock for the SLB tables.
 */
struct mtx	moea64_slb_mutex;

/*
 * PTEG data.
 */
u_long		moea64_pteg_count;
u_long		moea64_pteg_mask;

/*
 * PVO data.
 */

uma_zone_t	moea64_pvo_zone; /* zone for pvo entries */

static struct	pvo_entry *moea64_bpvo_pool;
static int	moea64_bpvo_pool_index = 0;
static int	moea64_bpvo_pool_size = 327680;
TUNABLE_INT("machdep.moea64_bpvo_pool_size", &moea64_bpvo_pool_size);
SYSCTL_INT(_machdep, OID_AUTO, moea64_allocated_bpvo_entries, CTLFLAG_RD, 
    &moea64_bpvo_pool_index, 0, "");

#define	VSID_NBPW	(sizeof(u_int32_t) * 8)
#ifdef __powerpc64__
#define	NVSIDS		(NPMAPS * 16)
#define VSID_HASHMASK	0xffffffffUL
#else
#define NVSIDS		NPMAPS
#define VSID_HASHMASK	0xfffffUL
#endif
static u_int	moea64_vsid_bitmap[NVSIDS / VSID_NBPW];

static boolean_t moea64_initialized = FALSE;

/*
 * Statistics.
 */
u_int	moea64_pte_valid = 0;
u_int	moea64_pte_overflow = 0;
u_int	moea64_pvo_entries = 0;
u_int	moea64_pvo_enter_calls = 0;
u_int	moea64_pvo_remove_calls = 0;
SYSCTL_INT(_machdep, OID_AUTO, moea64_pte_valid, CTLFLAG_RD, 
    &moea64_pte_valid, 0, "");
SYSCTL_INT(_machdep, OID_AUTO, moea64_pte_overflow, CTLFLAG_RD,
    &moea64_pte_overflow, 0, "");
SYSCTL_INT(_machdep, OID_AUTO, moea64_pvo_entries, CTLFLAG_RD, 
    &moea64_pvo_entries, 0, "");
SYSCTL_INT(_machdep, OID_AUTO, moea64_pvo_enter_calls, CTLFLAG_RD,
    &moea64_pvo_enter_calls, 0, "");
SYSCTL_INT(_machdep, OID_AUTO, moea64_pvo_remove_calls, CTLFLAG_RD,
    &moea64_pvo_remove_calls, 0, "");

vm_offset_t	moea64_scratchpage_va[2];
struct pvo_entry *moea64_scratchpage_pvo[2];
struct	mtx	moea64_scratchpage_mtx;

uint64_t 	moea64_large_page_mask = 0;
uint64_t	moea64_large_page_size = 0;
int		moea64_large_page_shift = 0;

/*
 * PVO calls.
 */
static int	moea64_pvo_enter(mmu_t mmu, struct pvo_entry *pvo,
		    struct pvo_head *pvo_head);
static void	moea64_pvo_remove_from_pmap(mmu_t mmu, struct pvo_entry *pvo);
static void	moea64_pvo_remove_from_page(mmu_t mmu, struct pvo_entry *pvo);
static struct	pvo_entry *moea64_pvo_find_va(pmap_t, vm_offset_t);

/*
 * Utility routines.
 */
static boolean_t	moea64_query_bit(mmu_t, vm_page_t, uint64_t);
static u_int		moea64_clear_bit(mmu_t, vm_page_t, uint64_t);
static void		moea64_kremove(mmu_t, vm_offset_t);
static void		moea64_syncicache(mmu_t, pmap_t pmap, vm_offset_t va, 
			    vm_paddr_t pa, vm_size_t sz);
static void		moea64_pmap_init_qpages(void);

/*
 * Kernel MMU interface
 */
void moea64_clear_modify(mmu_t, vm_page_t);
void moea64_copy_page(mmu_t, vm_page_t, vm_page_t);
void moea64_copy_pages(mmu_t mmu, vm_page_t *ma, vm_offset_t a_offset,
    vm_page_t *mb, vm_offset_t b_offset, int xfersize);
int moea64_enter(mmu_t, pmap_t, vm_offset_t, vm_page_t, vm_prot_t,
    u_int flags, int8_t psind);
void moea64_enter_object(mmu_t, pmap_t, vm_offset_t, vm_offset_t, vm_page_t,
    vm_prot_t);
void moea64_enter_quick(mmu_t, pmap_t, vm_offset_t, vm_page_t, vm_prot_t);
vm_paddr_t moea64_extract(mmu_t, pmap_t, vm_offset_t);
vm_page_t moea64_extract_and_hold(mmu_t, pmap_t, vm_offset_t, vm_prot_t);
void moea64_init(mmu_t);
boolean_t moea64_is_modified(mmu_t, vm_page_t);
boolean_t moea64_is_prefaultable(mmu_t, pmap_t, vm_offset_t);
boolean_t moea64_is_referenced(mmu_t, vm_page_t);
int moea64_ts_referenced(mmu_t, vm_page_t);
vm_offset_t moea64_map(mmu_t, vm_offset_t *, vm_paddr_t, vm_paddr_t, int);
boolean_t moea64_page_exists_quick(mmu_t, pmap_t, vm_page_t);
void moea64_page_init(mmu_t, vm_page_t);
int moea64_page_wired_mappings(mmu_t, vm_page_t);
void moea64_pinit(mmu_t, pmap_t);
void moea64_pinit0(mmu_t, pmap_t);
void moea64_protect(mmu_t, pmap_t, vm_offset_t, vm_offset_t, vm_prot_t);
void moea64_qenter(mmu_t, vm_offset_t, vm_page_t *, int);
void moea64_qremove(mmu_t, vm_offset_t, int);
void moea64_release(mmu_t, pmap_t);
void moea64_remove(mmu_t, pmap_t, vm_offset_t, vm_offset_t);
void moea64_remove_pages(mmu_t, pmap_t);
void moea64_remove_all(mmu_t, vm_page_t);
void moea64_remove_write(mmu_t, vm_page_t);
void moea64_unwire(mmu_t, pmap_t, vm_offset_t, vm_offset_t);
void moea64_zero_page(mmu_t, vm_page_t);
void moea64_zero_page_area(mmu_t, vm_page_t, int, int);
void moea64_activate(mmu_t, struct thread *);
void moea64_deactivate(mmu_t, struct thread *);
void *moea64_mapdev(mmu_t, vm_paddr_t, vm_size_t);
void *moea64_mapdev_attr(mmu_t, vm_paddr_t, vm_size_t, vm_memattr_t);
void moea64_unmapdev(mmu_t, vm_offset_t, vm_size_t);
vm_paddr_t moea64_kextract(mmu_t, vm_offset_t);
void moea64_page_set_memattr(mmu_t, vm_page_t m, vm_memattr_t ma);
void moea64_kenter_attr(mmu_t, vm_offset_t, vm_paddr_t, vm_memattr_t ma);
void moea64_kenter(mmu_t, vm_offset_t, vm_paddr_t);
boolean_t moea64_dev_direct_mapped(mmu_t, vm_paddr_t, vm_size_t);
static void moea64_sync_icache(mmu_t, pmap_t, vm_offset_t, vm_size_t);
void moea64_dumpsys_map(mmu_t mmu, vm_paddr_t pa, size_t sz,
    void **va);
void moea64_scan_init(mmu_t mmu);
vm_offset_t moea64_quick_enter_page(mmu_t mmu, vm_page_t m);
void moea64_quick_remove_page(mmu_t mmu, vm_offset_t addr);
static int moea64_map_user_ptr(mmu_t mmu, pmap_t pm,
    volatile const void *uaddr, void **kaddr, size_t ulen, size_t *klen);
static int moea64_decode_kernel_ptr(mmu_t mmu, vm_offset_t addr,
    int *is_user, vm_offset_t *decoded_addr);


static mmu_method_t moea64_methods[] = {
	MMUMETHOD(mmu_clear_modify,	moea64_clear_modify),
	MMUMETHOD(mmu_copy_page,	moea64_copy_page),
	MMUMETHOD(mmu_copy_pages,	moea64_copy_pages),
	MMUMETHOD(mmu_enter,		moea64_enter),
	MMUMETHOD(mmu_enter_object,	moea64_enter_object),
	MMUMETHOD(mmu_enter_quick,	moea64_enter_quick),
	MMUMETHOD(mmu_extract,		moea64_extract),
	MMUMETHOD(mmu_extract_and_hold,	moea64_extract_and_hold),
	MMUMETHOD(mmu_init,		moea64_init),
	MMUMETHOD(mmu_is_modified,	moea64_is_modified),
	MMUMETHOD(mmu_is_prefaultable,	moea64_is_prefaultable),
	MMUMETHOD(mmu_is_referenced,	moea64_is_referenced),
	MMUMETHOD(mmu_ts_referenced,	moea64_ts_referenced),
	MMUMETHOD(mmu_map,     		moea64_map),
	MMUMETHOD(mmu_page_exists_quick,moea64_page_exists_quick),
	MMUMETHOD(mmu_page_init,	moea64_page_init),
	MMUMETHOD(mmu_page_wired_mappings,moea64_page_wired_mappings),
	MMUMETHOD(mmu_pinit,		moea64_pinit),
	MMUMETHOD(mmu_pinit0,		moea64_pinit0),
	MMUMETHOD(mmu_protect,		moea64_protect),
	MMUMETHOD(mmu_qenter,		moea64_qenter),
	MMUMETHOD(mmu_qremove,		moea64_qremove),
	MMUMETHOD(mmu_release,		moea64_release),
	MMUMETHOD(mmu_remove,		moea64_remove),
	MMUMETHOD(mmu_remove_pages,	moea64_remove_pages),
	MMUMETHOD(mmu_remove_all,      	moea64_remove_all),
	MMUMETHOD(mmu_remove_write,	moea64_remove_write),
	MMUMETHOD(mmu_sync_icache,	moea64_sync_icache),
	MMUMETHOD(mmu_unwire,		moea64_unwire),
	MMUMETHOD(mmu_zero_page,       	moea64_zero_page),
	MMUMETHOD(mmu_zero_page_area,	moea64_zero_page_area),
	MMUMETHOD(mmu_activate,		moea64_activate),
	MMUMETHOD(mmu_deactivate,      	moea64_deactivate),
	MMUMETHOD(mmu_page_set_memattr,	moea64_page_set_memattr),
	MMUMETHOD(mmu_quick_enter_page, moea64_quick_enter_page),
	MMUMETHOD(mmu_quick_remove_page, moea64_quick_remove_page),

	/* Internal interfaces */
	MMUMETHOD(mmu_mapdev,		moea64_mapdev),
	MMUMETHOD(mmu_mapdev_attr,	moea64_mapdev_attr),
	MMUMETHOD(mmu_unmapdev,		moea64_unmapdev),
	MMUMETHOD(mmu_kextract,		moea64_kextract),
	MMUMETHOD(mmu_kenter,		moea64_kenter),
	MMUMETHOD(mmu_kenter_attr,	moea64_kenter_attr),
	MMUMETHOD(mmu_dev_direct_mapped,moea64_dev_direct_mapped),
	MMUMETHOD(mmu_scan_init,	moea64_scan_init),
	MMUMETHOD(mmu_dumpsys_map,	moea64_dumpsys_map),
	MMUMETHOD(mmu_map_user_ptr,	moea64_map_user_ptr),
	MMUMETHOD(mmu_decode_kernel_ptr, moea64_decode_kernel_ptr),

	{ 0, 0 }
};

MMU_DEF(oea64_mmu, "mmu_oea64_base", moea64_methods, 0);

static struct pvo_head *
vm_page_to_pvoh(vm_page_t m)
{

	mtx_assert(PV_LOCKPTR(VM_PAGE_TO_PHYS(m)), MA_OWNED);
	return (&m->md.mdpg_pvoh);
}

static struct pvo_entry *
alloc_pvo_entry(int bootstrap)
{
	struct pvo_entry *pvo;

	if (!moea64_initialized || bootstrap) {
		if (moea64_bpvo_pool_index >= moea64_bpvo_pool_size) {
			panic("moea64_enter: bpvo pool exhausted, %d, %d, %zd",
			      moea64_bpvo_pool_index, moea64_bpvo_pool_size, 
			      moea64_bpvo_pool_size * sizeof(struct pvo_entry));
		}
		pvo = &moea64_bpvo_pool[
		    atomic_fetchadd_int(&moea64_bpvo_pool_index, 1)];
		bzero(pvo, sizeof(*pvo));
		pvo->pvo_vaddr = PVO_BOOTSTRAP;
	} else {
		pvo = uma_zalloc(moea64_pvo_zone, M_NOWAIT);
		bzero(pvo, sizeof(*pvo));
	}

	return (pvo);
}


static void
init_pvo_entry(struct pvo_entry *pvo, pmap_t pmap, vm_offset_t va)
{
	uint64_t vsid;
	uint64_t hash;
	int shift;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	pvo->pvo_pmap = pmap;
	va &= ~ADDR_POFF;
	pvo->pvo_vaddr |= va;
	vsid = va_to_vsid(pmap, va);
	pvo->pvo_vpn = (uint64_t)((va & ADDR_PIDX) >> ADDR_PIDX_SHFT)
	    | (vsid << 16);

	shift = (pvo->pvo_vaddr & PVO_LARGE) ? moea64_large_page_shift :
	    ADDR_PIDX_SHFT;
	hash = (vsid & VSID_HASH_MASK) ^ (((uint64_t)va & ADDR_PIDX) >> shift);
	pvo->pvo_pte.slot = (hash & moea64_pteg_mask) << 3;
}

static void
free_pvo_entry(struct pvo_entry *pvo)
{

	if (!(pvo->pvo_vaddr & PVO_BOOTSTRAP))
		uma_zfree(moea64_pvo_zone, pvo);
}

void
moea64_pte_from_pvo(const struct pvo_entry *pvo, struct lpte *lpte)
{

	lpte->pte_hi = (pvo->pvo_vpn >> (ADDR_API_SHFT64 - ADDR_PIDX_SHFT)) &
	    LPTE_AVPN_MASK;
	lpte->pte_hi |= LPTE_VALID;
	
	if (pvo->pvo_vaddr & PVO_LARGE)
		lpte->pte_hi |= LPTE_BIG;
	if (pvo->pvo_vaddr & PVO_WIRED)
		lpte->pte_hi |= LPTE_WIRED;
	if (pvo->pvo_vaddr & PVO_HID)
		lpte->pte_hi |= LPTE_HID;

	lpte->pte_lo = pvo->pvo_pte.pa; /* Includes WIMG bits */
	if (pvo->pvo_pte.prot & VM_PROT_WRITE)
		lpte->pte_lo |= LPTE_BW;
	else
		lpte->pte_lo |= LPTE_BR;

	if (!(pvo->pvo_pte.prot & VM_PROT_EXECUTE))
		lpte->pte_lo |= LPTE_NOEXEC;
}

static __inline uint64_t
moea64_calc_wimg(vm_paddr_t pa, vm_memattr_t ma)
{
	uint64_t pte_lo;
	int i;

	if (ma != VM_MEMATTR_DEFAULT) {
		switch (ma) {
		case VM_MEMATTR_UNCACHEABLE:
			return (LPTE_I | LPTE_G);
		case VM_MEMATTR_CACHEABLE:
			return (LPTE_M);
		case VM_MEMATTR_WRITE_COMBINING:
		case VM_MEMATTR_WRITE_BACK:
		case VM_MEMATTR_PREFETCHABLE:
			return (LPTE_I);
		case VM_MEMATTR_WRITE_THROUGH:
			return (LPTE_W | LPTE_M);
		}
	}

	/*
	 * Assume the page is cache inhibited and access is guarded unless
	 * it's in our available memory array.
	 */
	pte_lo = LPTE_I | LPTE_G;
	for (i = 0; i < pregions_sz; i++) {
		if ((pa >= pregions[i].mr_start) &&
		    (pa < (pregions[i].mr_start + pregions[i].mr_size))) {
			pte_lo &= ~(LPTE_I | LPTE_G);
			pte_lo |= LPTE_M;
			break;
		}
	}

	return pte_lo;
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

static void
moea64_add_ofw_mappings(mmu_t mmup, phandle_t mmu, size_t sz)
{
	struct ofw_map	translations[sz/(4*sizeof(cell_t))]; /*>= 4 cells per */
	pcell_t		acells, trans_cells[sz/sizeof(cell_t)];
	struct pvo_entry *pvo;
	register_t	msr;
	vm_offset_t	off;
	vm_paddr_t	pa_base;
	int		i, j;

	bzero(translations, sz);
	OF_getencprop(OF_finddevice("/"), "#address-cells", &acells,
	    sizeof(acells));
	if (OF_getencprop(mmu, "translations", trans_cells, sz) == -1)
		panic("moea64_bootstrap: can't get ofw translations");

	CTR0(KTR_PMAP, "moea64_add_ofw_mappings: translations");
	sz /= sizeof(cell_t);
	for (i = 0, j = 0; i < sz; j++) {
		translations[j].om_va = trans_cells[i++];
		translations[j].om_len = trans_cells[i++];
		translations[j].om_pa = trans_cells[i++];
		if (acells == 2) {
			translations[j].om_pa <<= 32;
			translations[j].om_pa |= trans_cells[i++];
		}
		translations[j].om_mode = trans_cells[i++];
	}
	KASSERT(i == sz, ("Translations map has incorrect cell count (%d/%zd)",
	    i, sz));

	sz = j;
	qsort(translations, sz, sizeof (*translations), om_cmp);

	for (i = 0; i < sz; i++) {
		pa_base = translations[i].om_pa;
	      #ifndef __powerpc64__
		if ((translations[i].om_pa >> 32) != 0)
			panic("OFW translations above 32-bit boundary!");
	      #endif

		if (pa_base % PAGE_SIZE)
			panic("OFW translation not page-aligned (phys)!");
		if (translations[i].om_va % PAGE_SIZE)
			panic("OFW translation not page-aligned (virt)!");

		CTR3(KTR_PMAP, "translation: pa=%#zx va=%#x len=%#x",
		    pa_base, translations[i].om_va, translations[i].om_len);

		/* Now enter the pages for this mapping */

		DISABLE_TRANS(msr);
		for (off = 0; off < translations[i].om_len; off += PAGE_SIZE) {
			/* If this address is direct-mapped, skip remapping */
			if (hw_direct_map &&
			    translations[i].om_va == PHYS_TO_DMAP(pa_base) &&
			    moea64_calc_wimg(pa_base + off, VM_MEMATTR_DEFAULT)
 			    == LPTE_M)
				continue;

			PMAP_LOCK(kernel_pmap);
			pvo = moea64_pvo_find_va(kernel_pmap,
			    translations[i].om_va + off);
			PMAP_UNLOCK(kernel_pmap);
			if (pvo != NULL)
				continue;

			moea64_kenter(mmup, translations[i].om_va + off,
			    pa_base + off);
		}
		ENABLE_TRANS(msr);
	}
}

#ifdef __powerpc64__
static void
moea64_probe_large_page(void)
{
	uint16_t pvr = mfpvr() >> 16;

	switch (pvr) {
	case IBM970:
	case IBM970FX:
	case IBM970MP:
		powerpc_sync(); isync();
		mtspr(SPR_HID4, mfspr(SPR_HID4) & ~HID4_970_DISABLE_LG_PG);
		powerpc_sync(); isync();
		
		/* FALLTHROUGH */
	default:
		if (moea64_large_page_size == 0) {
			moea64_large_page_size = 0x1000000; /* 16 MB */
			moea64_large_page_shift = 24;
		}
	}

	moea64_large_page_mask = moea64_large_page_size - 1;
}

static void
moea64_bootstrap_slb_prefault(vm_offset_t va, int large)
{
	struct slb *cache;
	struct slb entry;
	uint64_t esid, slbe;
	uint64_t i;

	cache = PCPU_GET(aim.slb);
	esid = va >> ADDR_SR_SHFT;
	slbe = (esid << SLBE_ESID_SHIFT) | SLBE_VALID;

	for (i = 0; i < 64; i++) {
		if (cache[i].slbe == (slbe | i))
			return;
	}

	entry.slbe = slbe;
	entry.slbv = KERNEL_VSID(esid) << SLBV_VSID_SHIFT;
	if (large)
		entry.slbv |= SLBV_L;

	slb_insert_kernel(entry.slbe, entry.slbv);
}
#endif

static void
moea64_setup_direct_map(mmu_t mmup, vm_offset_t kernelstart,
    vm_offset_t kernelend)
{
	struct pvo_entry *pvo;
	register_t msr;
	vm_paddr_t pa;
	vm_offset_t size, off;
	uint64_t pte_lo;
	int i;

	if (moea64_large_page_size == 0) 
		hw_direct_map = 0;

	DISABLE_TRANS(msr);
	if (hw_direct_map) {
		PMAP_LOCK(kernel_pmap);
		for (i = 0; i < pregions_sz; i++) {
		  for (pa = pregions[i].mr_start; pa < pregions[i].mr_start +
		     pregions[i].mr_size; pa += moea64_large_page_size) {
			pte_lo = LPTE_M;

			pvo = alloc_pvo_entry(1 /* bootstrap */);
			pvo->pvo_vaddr |= PVO_WIRED | PVO_LARGE;
			init_pvo_entry(pvo, kernel_pmap, PHYS_TO_DMAP(pa));

			/*
			 * Set memory access as guarded if prefetch within
			 * the page could exit the available physmem area.
			 */
			if (pa & moea64_large_page_mask) {
				pa &= moea64_large_page_mask;
				pte_lo |= LPTE_G;
			}
			if (pa + moea64_large_page_size >
			    pregions[i].mr_start + pregions[i].mr_size)
				pte_lo |= LPTE_G;

			pvo->pvo_pte.prot = VM_PROT_READ | VM_PROT_WRITE |
			    VM_PROT_EXECUTE;
			pvo->pvo_pte.pa = pa | pte_lo;
			moea64_pvo_enter(mmup, pvo, NULL);
		  }
		}
		PMAP_UNLOCK(kernel_pmap);
	}

	/*
	 * Make sure the kernel and BPVO pool stay mapped on systems either
	 * without a direct map or on which the kernel is not already executing
	 * out of the direct-mapped region.
	 */

	if (!hw_direct_map || kernelstart < DMAP_BASE_ADDRESS) {
		for (pa = kernelstart & ~PAGE_MASK; pa < kernelend;
		    pa += PAGE_SIZE)
			moea64_kenter(mmup, pa, pa);
	}

	if (!hw_direct_map) {
		size = moea64_bpvo_pool_size*sizeof(struct pvo_entry);
		off = (vm_offset_t)(moea64_bpvo_pool);
		for (pa = off; pa < off + size; pa += PAGE_SIZE)
			moea64_kenter(mmup, pa, pa);
	}
	ENABLE_TRANS(msr);

	/*
	 * Allow user to override unmapped_buf_allowed for testing.
	 * XXXKIB Only direct map implementation was tested.
	 */
	if (!TUNABLE_INT_FETCH("vfs.unmapped_buf_allowed",
	    &unmapped_buf_allowed))
		unmapped_buf_allowed = hw_direct_map;
}

/* Quick sort callout for comparing physical addresses. */
static int
pa_cmp(const void *a, const void *b)
{
	const vm_paddr_t *pa = a, *pb = b;

	if (*pa < *pb)
		return (-1);
	else if (*pa > *pb)
		return (1);
	else
		return (0);
}

void
moea64_early_bootstrap(mmu_t mmup, vm_offset_t kernelstart, vm_offset_t kernelend)
{
	int		i, j;
	vm_size_t	physsz, hwphyssz;
	vm_paddr_t	kernelphysstart, kernelphysend;
	int		rm_pavail;

#ifndef __powerpc64__
	/* We don't have a direct map since there is no BAT */
	hw_direct_map = 0;

	/* Make sure battable is zero, since we have no BAT */
	for (i = 0; i < 16; i++) {
		battable[i].batu = 0;
		battable[i].batl = 0;
	}
#else
	moea64_probe_large_page();

	/* Use a direct map if we have large page support */
	if (moea64_large_page_size > 0)
		hw_direct_map = 1;
	else
		hw_direct_map = 0;

	/* Install trap handlers for SLBs */
	bcopy(&slbtrap, (void *)EXC_DSE,(size_t)&slbtrapend - (size_t)&slbtrap);
	bcopy(&slbtrap, (void *)EXC_ISE,(size_t)&slbtrapend - (size_t)&slbtrap);
	__syncicache((void *)EXC_DSE, 0x80);
	__syncicache((void *)EXC_ISE, 0x80);
#endif

	kernelphysstart = kernelstart & ~DMAP_BASE_ADDRESS;
	kernelphysend = kernelend & ~DMAP_BASE_ADDRESS;

	/* Get physical memory regions from firmware */
	mem_regions(&pregions, &pregions_sz, &regions, &regions_sz);
	CTR0(KTR_PMAP, "moea64_bootstrap: physical memory");

	if (sizeof(phys_avail)/sizeof(phys_avail[0]) < regions_sz)
		panic("moea64_bootstrap: phys_avail too small");

	phys_avail_count = 0;
	physsz = 0;
	hwphyssz = 0;
	TUNABLE_ULONG_FETCH("hw.physmem", (u_long *) &hwphyssz);
	for (i = 0, j = 0; i < regions_sz; i++, j += 2) {
		CTR3(KTR_PMAP, "region: %#zx - %#zx (%#zx)",
		    regions[i].mr_start, regions[i].mr_start +
		    regions[i].mr_size, regions[i].mr_size);
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
	rm_pavail = 0;
	for (j = 0; j < 2*phys_avail_count; j+=2) {
		if (phys_avail[j] < EXC_LAST)
			phys_avail[j] += EXC_LAST;

		if (phys_avail[j] >= kernelphysstart &&
		    phys_avail[j+1] <= kernelphysend) {
			phys_avail[j] = phys_avail[j+1] = ~0;
			rm_pavail++;
			continue;
		}

		if (kernelphysstart >= phys_avail[j] &&
		    kernelphysstart < phys_avail[j+1]) {
			if (kernelphysend < phys_avail[j+1]) {
				phys_avail[2*phys_avail_count] =
				    (kernelphysend & ~PAGE_MASK) + PAGE_SIZE;
				phys_avail[2*phys_avail_count + 1] =
				    phys_avail[j+1];
				phys_avail_count++;
			}

			phys_avail[j+1] = kernelphysstart & ~PAGE_MASK;
		}

		if (kernelphysend >= phys_avail[j] &&
		    kernelphysend < phys_avail[j+1]) {
			if (kernelphysstart > phys_avail[j]) {
				phys_avail[2*phys_avail_count] = phys_avail[j];
				phys_avail[2*phys_avail_count + 1] =
				    kernelphysstart & ~PAGE_MASK;
				phys_avail_count++;
			}

			phys_avail[j] = (kernelphysend & ~PAGE_MASK) +
			    PAGE_SIZE;
		}
	}

	/* Remove physical available regions marked for removal (~0) */
	if (rm_pavail) {
		qsort(phys_avail, 2*phys_avail_count, sizeof(phys_avail[0]),
			pa_cmp);
		phys_avail_count -= rm_pavail;
		for (i = 2*phys_avail_count;
		     i < 2*(phys_avail_count + rm_pavail); i+=2)
			phys_avail[i] = phys_avail[i+1] = 0;
	}

	physmem = btoc(physsz);

#ifdef PTEGCOUNT
	moea64_pteg_count = PTEGCOUNT;
#else
	moea64_pteg_count = 0x1000;

	while (moea64_pteg_count < physmem)
		moea64_pteg_count <<= 1;

	moea64_pteg_count >>= 1;
#endif /* PTEGCOUNT */
}

void
moea64_mid_bootstrap(mmu_t mmup, vm_offset_t kernelstart, vm_offset_t kernelend)
{
	int		i;

	/*
	 * Set PTEG mask
	 */
	moea64_pteg_mask = moea64_pteg_count - 1;

	/*
	 * Initialize SLB table lock and page locks
	 */
	mtx_init(&moea64_slb_mutex, "SLB table", NULL, MTX_DEF);
	for (i = 0; i < PV_LOCK_COUNT; i++)
		mtx_init(&pv_lock[i], "page pv", NULL, MTX_DEF);

	/*
	 * Initialise the bootstrap pvo pool.
	 */
	moea64_bpvo_pool = (struct pvo_entry *)moea64_bootstrap_alloc(
		moea64_bpvo_pool_size*sizeof(struct pvo_entry), 0);
	moea64_bpvo_pool_index = 0;

	/* Place at address usable through the direct map */
	if (hw_direct_map)
		moea64_bpvo_pool = (struct pvo_entry *)
		    PHYS_TO_DMAP((uintptr_t)moea64_bpvo_pool);

	/*
	 * Make sure kernel vsid is allocated as well as VSID 0.
	 */
	#ifndef __powerpc64__
	moea64_vsid_bitmap[(KERNEL_VSIDBITS & (NVSIDS - 1)) / VSID_NBPW]
		|= 1 << (KERNEL_VSIDBITS % VSID_NBPW);
	moea64_vsid_bitmap[0] |= 1;
	#endif

	/*
	 * Initialize the kernel pmap (which is statically allocated).
	 */
	#ifdef __powerpc64__
	for (i = 0; i < 64; i++) {
		pcpup->pc_aim.slb[i].slbv = 0;
		pcpup->pc_aim.slb[i].slbe = 0;
	}
	#else
	for (i = 0; i < 16; i++) 
		kernel_pmap->pm_sr[i] = EMPTY_SEGMENT + i;
	#endif

	kernel_pmap->pmap_phys = kernel_pmap;
	CPU_FILL(&kernel_pmap->pm_active);
	RB_INIT(&kernel_pmap->pmap_pvo);

	PMAP_LOCK_INIT(kernel_pmap);

	/*
	 * Now map in all the other buffers we allocated earlier
	 */

	moea64_setup_direct_map(mmup, kernelstart, kernelend);
}

void
moea64_late_bootstrap(mmu_t mmup, vm_offset_t kernelstart, vm_offset_t kernelend)
{
	ihandle_t	mmui;
	phandle_t	chosen;
	phandle_t	mmu;
	ssize_t		sz;
	int		i;
	vm_offset_t	pa, va;
	void		*dpcpu;

	/*
	 * Set up the Open Firmware pmap and add its mappings if not in real
	 * mode.
	 */

	chosen = OF_finddevice("/chosen");
	if (chosen != -1 && OF_getencprop(chosen, "mmu", &mmui, 4) != -1) {
		mmu = OF_instance_to_package(mmui);
		if (mmu == -1 ||
		    (sz = OF_getproplen(mmu, "translations")) == -1)
			sz = 0;
		if (sz > 6144 /* tmpstksz - 2 KB headroom */)
			panic("moea64_bootstrap: too many ofw translations");

		if (sz > 0)
			moea64_add_ofw_mappings(mmup, mmu, sz);
	}

	/*
	 * Calculate the last available physical address.
	 */
	Maxmem = 0;
	for (i = 0; phys_avail[i + 2] != 0; i += 2)
		Maxmem = MAX(Maxmem, powerpc_btop(phys_avail[i + 1]));

	/*
	 * Initialize MMU.
	 */
	MMU_CPU_BOOTSTRAP(mmup,0);
	mtmsr(mfmsr() | PSL_DR | PSL_IR);
	pmap_bootstrapped++;

	/*
	 * Set the start and end of kva.
	 */
	virtual_avail = VM_MIN_KERNEL_ADDRESS;
	virtual_end = VM_MAX_SAFE_KERNEL_ADDRESS; 

	/*
	 * Map the entire KVA range into the SLB. We must not fault there.
	 */
	#ifdef __powerpc64__
	for (va = virtual_avail; va < virtual_end; va += SEGMENT_LENGTH)
		moea64_bootstrap_slb_prefault(va, 0);
	#endif

	/*
	 * Remap any early IO mappings (console framebuffer, etc.)
	 */
	bs_remap_earlyboot();

	/*
	 * Figure out how far we can extend virtual_end into segment 16
	 * without running into existing mappings. Segment 16 is guaranteed
	 * to contain neither RAM nor devices (at least on Apple hardware),
	 * but will generally contain some OFW mappings we should not
	 * step on.
	 */

	#ifndef __powerpc64__	/* KVA is in high memory on PPC64 */
	PMAP_LOCK(kernel_pmap);
	while (virtual_end < VM_MAX_KERNEL_ADDRESS &&
	    moea64_pvo_find_va(kernel_pmap, virtual_end+1) == NULL)
		virtual_end += PAGE_SIZE;
	PMAP_UNLOCK(kernel_pmap);
	#endif

	/*
	 * Allocate a kernel stack with a guard page for thread0 and map it
	 * into the kernel page map.
	 */
	pa = moea64_bootstrap_alloc(kstack_pages * PAGE_SIZE, PAGE_SIZE);
	va = virtual_avail + KSTACK_GUARD_PAGES * PAGE_SIZE;
	virtual_avail = va + kstack_pages * PAGE_SIZE;
	CTR2(KTR_PMAP, "moea64_bootstrap: kstack0 at %#x (%#x)", pa, va);
	thread0.td_kstack = va;
	thread0.td_kstack_pages = kstack_pages;
	for (i = 0; i < kstack_pages; i++) {
		moea64_kenter(mmup, va, pa);
		pa += PAGE_SIZE;
		va += PAGE_SIZE;
	}

	/*
	 * Allocate virtual address space for the message buffer.
	 */
	pa = msgbuf_phys = moea64_bootstrap_alloc(msgbufsize, PAGE_SIZE);
	msgbufp = (struct msgbuf *)virtual_avail;
	va = virtual_avail;
	virtual_avail += round_page(msgbufsize);
	while (va < virtual_avail) {
		moea64_kenter(mmup, va, pa);
		pa += PAGE_SIZE;
		va += PAGE_SIZE;
	}

	/*
	 * Allocate virtual address space for the dynamic percpu area.
	 */
	pa = moea64_bootstrap_alloc(DPCPU_SIZE, PAGE_SIZE);
	dpcpu = (void *)virtual_avail;
	va = virtual_avail;
	virtual_avail += DPCPU_SIZE;
	while (va < virtual_avail) {
		moea64_kenter(mmup, va, pa);
		pa += PAGE_SIZE;
		va += PAGE_SIZE;
	}
	dpcpu_init(dpcpu, curcpu);

	/*
	 * Allocate some things for page zeroing. We put this directly
	 * in the page table and use MOEA64_PTE_REPLACE to avoid any
	 * of the PVO book-keeping or other parts of the VM system
	 * from even knowing that this hack exists.
	 */

	if (!hw_direct_map) {
		mtx_init(&moea64_scratchpage_mtx, "pvo zero page", NULL,
		    MTX_DEF);
		for (i = 0; i < 2; i++) {
			moea64_scratchpage_va[i] = (virtual_end+1) - PAGE_SIZE;
			virtual_end -= PAGE_SIZE;

			moea64_kenter(mmup, moea64_scratchpage_va[i], 0);

			PMAP_LOCK(kernel_pmap);
			moea64_scratchpage_pvo[i] = moea64_pvo_find_va(
			    kernel_pmap, (vm_offset_t)moea64_scratchpage_va[i]);
			PMAP_UNLOCK(kernel_pmap);
		}
	}
}

static void
moea64_pmap_init_qpages(void)
{
	struct pcpu *pc;
	int i;

	if (hw_direct_map)
		return;

	CPU_FOREACH(i) {
		pc = pcpu_find(i);
		pc->pc_qmap_addr = kva_alloc(PAGE_SIZE);
		if (pc->pc_qmap_addr == 0)
			panic("pmap_init_qpages: unable to allocate KVA");
		PMAP_LOCK(kernel_pmap);
		pc->pc_aim.qmap_pvo =
		    moea64_pvo_find_va(kernel_pmap, pc->pc_qmap_addr);
		PMAP_UNLOCK(kernel_pmap);
		mtx_init(&pc->pc_aim.qmap_lock, "qmap lock", NULL, MTX_DEF);
	}
}

SYSINIT(qpages_init, SI_SUB_CPU, SI_ORDER_ANY, moea64_pmap_init_qpages, NULL);

/*
 * Activate a user pmap.  This mostly involves setting some non-CPU
 * state.
 */
void
moea64_activate(mmu_t mmu, struct thread *td)
{
	pmap_t	pm;

	pm = &td->td_proc->p_vmspace->vm_pmap;
	CPU_SET(PCPU_GET(cpuid), &pm->pm_active);

	#ifdef __powerpc64__
	PCPU_SET(aim.userslb, pm->pm_slb);
	__asm __volatile("slbmte %0, %1; isync" ::
	    "r"(td->td_pcb->pcb_cpu.aim.usr_vsid), "r"(USER_SLB_SLBE));
	#else
	PCPU_SET(curpmap, pm->pmap_phys);
	mtsrin(USER_SR << ADDR_SR_SHFT, td->td_pcb->pcb_cpu.aim.usr_vsid);
	#endif
}

void
moea64_deactivate(mmu_t mmu, struct thread *td)
{
	pmap_t	pm;

	__asm __volatile("isync; slbie %0" :: "r"(USER_ADDR));

	pm = &td->td_proc->p_vmspace->vm_pmap;
	CPU_CLR(PCPU_GET(cpuid), &pm->pm_active);
	#ifdef __powerpc64__
	PCPU_SET(aim.userslb, NULL);
	#else
	PCPU_SET(curpmap, NULL);
	#endif
}

void
moea64_unwire(mmu_t mmu, pmap_t pm, vm_offset_t sva, vm_offset_t eva)
{
	struct	pvo_entry key, *pvo;
	vm_page_t m;
	int64_t	refchg;

	key.pvo_vaddr = sva;
	PMAP_LOCK(pm);
	for (pvo = RB_NFIND(pvo_tree, &pm->pmap_pvo, &key);
	    pvo != NULL && PVO_VADDR(pvo) < eva;
	    pvo = RB_NEXT(pvo_tree, &pm->pmap_pvo, pvo)) {
		if ((pvo->pvo_vaddr & PVO_WIRED) == 0)
			panic("moea64_unwire: pvo %p is missing PVO_WIRED",
			    pvo);
		pvo->pvo_vaddr &= ~PVO_WIRED;
		refchg = MOEA64_PTE_REPLACE(mmu, pvo, 0 /* No invalidation */);
		if ((pvo->pvo_vaddr & PVO_MANAGED) &&
		    (pvo->pvo_pte.prot & VM_PROT_WRITE)) {
			if (refchg < 0)
				refchg = LPTE_CHG;
			m = PHYS_TO_VM_PAGE(pvo->pvo_pte.pa & LPTE_RPGN);

			refchg |= atomic_readandclear_32(&m->md.mdpg_attrs);
			if (refchg & LPTE_CHG)
				vm_page_dirty(m);
			if (refchg & LPTE_REF)
				vm_page_aflag_set(m, PGA_REFERENCED);
		}
		pm->pm_stats.wired_count--;
	}
	PMAP_UNLOCK(pm);
}

/*
 * This goes through and sets the physical address of our
 * special scratch PTE to the PA we want to zero or copy. Because
 * of locking issues (this can get called in pvo_enter() by
 * the UMA allocator), we can't use most other utility functions here
 */

static __inline
void moea64_set_scratchpage_pa(mmu_t mmup, int which, vm_paddr_t pa) {

	KASSERT(!hw_direct_map, ("Using OEA64 scratchpage with a direct map!"));
	mtx_assert(&moea64_scratchpage_mtx, MA_OWNED);

	moea64_scratchpage_pvo[which]->pvo_pte.pa =
	    moea64_calc_wimg(pa, VM_MEMATTR_DEFAULT) | (uint64_t)pa;
	MOEA64_PTE_REPLACE(mmup, moea64_scratchpage_pvo[which],
	    MOEA64_PTE_INVALIDATE);
	isync();
}

void
moea64_copy_page(mmu_t mmu, vm_page_t msrc, vm_page_t mdst)
{
	vm_offset_t	dst;
	vm_offset_t	src;

	dst = VM_PAGE_TO_PHYS(mdst);
	src = VM_PAGE_TO_PHYS(msrc);

	if (hw_direct_map) {
		bcopy((void *)PHYS_TO_DMAP(src), (void *)PHYS_TO_DMAP(dst),
		    PAGE_SIZE);
	} else {
		mtx_lock(&moea64_scratchpage_mtx);

		moea64_set_scratchpage_pa(mmu, 0, src);
		moea64_set_scratchpage_pa(mmu, 1, dst);

		bcopy((void *)moea64_scratchpage_va[0], 
		    (void *)moea64_scratchpage_va[1], PAGE_SIZE);

		mtx_unlock(&moea64_scratchpage_mtx);
	}
}

static inline void
moea64_copy_pages_dmap(mmu_t mmu, vm_page_t *ma, vm_offset_t a_offset,
    vm_page_t *mb, vm_offset_t b_offset, int xfersize)
{
	void *a_cp, *b_cp;
	vm_offset_t a_pg_offset, b_pg_offset;
	int cnt;

	while (xfersize > 0) {
		a_pg_offset = a_offset & PAGE_MASK;
		cnt = min(xfersize, PAGE_SIZE - a_pg_offset);
		a_cp = (char *)(uintptr_t)PHYS_TO_DMAP(
		    VM_PAGE_TO_PHYS(ma[a_offset >> PAGE_SHIFT])) +
		    a_pg_offset;
		b_pg_offset = b_offset & PAGE_MASK;
		cnt = min(cnt, PAGE_SIZE - b_pg_offset);
		b_cp = (char *)(uintptr_t)PHYS_TO_DMAP(
		    VM_PAGE_TO_PHYS(mb[b_offset >> PAGE_SHIFT])) +
		    b_pg_offset;
		bcopy(a_cp, b_cp, cnt);
		a_offset += cnt;
		b_offset += cnt;
		xfersize -= cnt;
	}
}

static inline void
moea64_copy_pages_nodmap(mmu_t mmu, vm_page_t *ma, vm_offset_t a_offset,
    vm_page_t *mb, vm_offset_t b_offset, int xfersize)
{
	void *a_cp, *b_cp;
	vm_offset_t a_pg_offset, b_pg_offset;
	int cnt;

	mtx_lock(&moea64_scratchpage_mtx);
	while (xfersize > 0) {
		a_pg_offset = a_offset & PAGE_MASK;
		cnt = min(xfersize, PAGE_SIZE - a_pg_offset);
		moea64_set_scratchpage_pa(mmu, 0,
		    VM_PAGE_TO_PHYS(ma[a_offset >> PAGE_SHIFT]));
		a_cp = (char *)moea64_scratchpage_va[0] + a_pg_offset;
		b_pg_offset = b_offset & PAGE_MASK;
		cnt = min(cnt, PAGE_SIZE - b_pg_offset);
		moea64_set_scratchpage_pa(mmu, 1,
		    VM_PAGE_TO_PHYS(mb[b_offset >> PAGE_SHIFT]));
		b_cp = (char *)moea64_scratchpage_va[1] + b_pg_offset;
		bcopy(a_cp, b_cp, cnt);
		a_offset += cnt;
		b_offset += cnt;
		xfersize -= cnt;
	}
	mtx_unlock(&moea64_scratchpage_mtx);
}

void
moea64_copy_pages(mmu_t mmu, vm_page_t *ma, vm_offset_t a_offset,
    vm_page_t *mb, vm_offset_t b_offset, int xfersize)
{

	if (hw_direct_map) {
		moea64_copy_pages_dmap(mmu, ma, a_offset, mb, b_offset,
		    xfersize);
	} else {
		moea64_copy_pages_nodmap(mmu, ma, a_offset, mb, b_offset,
		    xfersize);
	}
}

void
moea64_zero_page_area(mmu_t mmu, vm_page_t m, int off, int size)
{
	vm_paddr_t pa = VM_PAGE_TO_PHYS(m);

	if (size + off > PAGE_SIZE)
		panic("moea64_zero_page: size + off > PAGE_SIZE");

	if (hw_direct_map) {
		bzero((caddr_t)(uintptr_t)PHYS_TO_DMAP(pa) + off, size);
	} else {
		mtx_lock(&moea64_scratchpage_mtx);
		moea64_set_scratchpage_pa(mmu, 0, pa);
		bzero((caddr_t)moea64_scratchpage_va[0] + off, size);
		mtx_unlock(&moea64_scratchpage_mtx);
	}
}

/*
 * Zero a page of physical memory by temporarily mapping it
 */
void
moea64_zero_page(mmu_t mmu, vm_page_t m)
{
	vm_paddr_t pa = VM_PAGE_TO_PHYS(m);
	vm_offset_t va, off;

	if (!hw_direct_map) {
		mtx_lock(&moea64_scratchpage_mtx);

		moea64_set_scratchpage_pa(mmu, 0, pa);
		va = moea64_scratchpage_va[0];
	} else {
		va = PHYS_TO_DMAP(pa);
	}

	for (off = 0; off < PAGE_SIZE; off += cacheline_size)
		__asm __volatile("dcbz 0,%0" :: "r"(va + off));

	if (!hw_direct_map)
		mtx_unlock(&moea64_scratchpage_mtx);
}

vm_offset_t
moea64_quick_enter_page(mmu_t mmu, vm_page_t m)
{
	struct pvo_entry *pvo;
	vm_paddr_t pa = VM_PAGE_TO_PHYS(m);

	if (hw_direct_map)
		return (PHYS_TO_DMAP(pa));

	/*
 	 * MOEA64_PTE_REPLACE does some locking, so we can't just grab
	 * a critical section and access the PCPU data like on i386.
	 * Instead, pin the thread and grab the PCPU lock to prevent
	 * a preempting thread from using the same PCPU data.
	 */
	sched_pin();

	mtx_assert(PCPU_PTR(aim.qmap_lock), MA_NOTOWNED);
	pvo = PCPU_GET(aim.qmap_pvo);

	mtx_lock(PCPU_PTR(aim.qmap_lock));
	pvo->pvo_pte.pa = moea64_calc_wimg(pa, pmap_page_get_memattr(m)) |
	    (uint64_t)pa;
	MOEA64_PTE_REPLACE(mmu, pvo, MOEA64_PTE_INVALIDATE);
	isync();

	return (PCPU_GET(qmap_addr));
}

void
moea64_quick_remove_page(mmu_t mmu, vm_offset_t addr)
{
	if (hw_direct_map)
		return;

	mtx_assert(PCPU_PTR(aim.qmap_lock), MA_OWNED);
	KASSERT(PCPU_GET(qmap_addr) == addr,
	    ("moea64_quick_remove_page: invalid address"));
	mtx_unlock(PCPU_PTR(aim.qmap_lock));
	sched_unpin();	
}

/*
 * Map the given physical page at the specified virtual address in the
 * target pmap with the protection requested.  If specified the page
 * will be wired down.
 */

int
moea64_enter(mmu_t mmu, pmap_t pmap, vm_offset_t va, vm_page_t m, 
    vm_prot_t prot, u_int flags, int8_t psind)
{
	struct		pvo_entry *pvo, *oldpvo;
	struct		pvo_head *pvo_head;
	uint64_t	pte_lo;
	int		error;

	if ((m->oflags & VPO_UNMANAGED) == 0 && !vm_page_xbusied(m))
		VM_OBJECT_ASSERT_LOCKED(m->object);

	pvo = alloc_pvo_entry(0);
	pvo->pvo_pmap = NULL; /* to be filled in later */
	pvo->pvo_pte.prot = prot;

	pte_lo = moea64_calc_wimg(VM_PAGE_TO_PHYS(m), pmap_page_get_memattr(m));
	pvo->pvo_pte.pa = VM_PAGE_TO_PHYS(m) | pte_lo;

	if ((flags & PMAP_ENTER_WIRED) != 0)
		pvo->pvo_vaddr |= PVO_WIRED;

	if ((m->oflags & VPO_UNMANAGED) != 0 || !moea64_initialized) {
		pvo_head = NULL;
	} else {
		pvo_head = &m->md.mdpg_pvoh;
		pvo->pvo_vaddr |= PVO_MANAGED;
	}
	
	for (;;) {
		PV_PAGE_LOCK(m);
		PMAP_LOCK(pmap);
		if (pvo->pvo_pmap == NULL)
			init_pvo_entry(pvo, pmap, va);
		if (prot & VM_PROT_WRITE)
			if (pmap_bootstrapped &&
			    (m->oflags & VPO_UNMANAGED) == 0)
				vm_page_aflag_set(m, PGA_WRITEABLE);

		oldpvo = moea64_pvo_find_va(pmap, va);
		if (oldpvo != NULL) {
			if (oldpvo->pvo_vaddr == pvo->pvo_vaddr &&
			    oldpvo->pvo_pte.pa == pvo->pvo_pte.pa &&
			    oldpvo->pvo_pte.prot == prot) {
				/* Identical mapping already exists */
				error = 0;

				/* If not in page table, reinsert it */
				if (MOEA64_PTE_SYNCH(mmu, oldpvo) < 0) {
					moea64_pte_overflow--;
					MOEA64_PTE_INSERT(mmu, oldpvo);
				}

				/* Then just clean up and go home */
				PV_PAGE_UNLOCK(m);
				PMAP_UNLOCK(pmap);
				free_pvo_entry(pvo);
				break;
			}

			/* Otherwise, need to kill it first */
			KASSERT(oldpvo->pvo_pmap == pmap, ("pmap of old "
			    "mapping does not match new mapping"));
			moea64_pvo_remove_from_pmap(mmu, oldpvo);
		}
		error = moea64_pvo_enter(mmu, pvo, pvo_head);
		PV_PAGE_UNLOCK(m);
		PMAP_UNLOCK(pmap);

		/* Free any dead pages */
		if (oldpvo != NULL) {
			PV_LOCK(oldpvo->pvo_pte.pa & LPTE_RPGN);
			moea64_pvo_remove_from_page(mmu, oldpvo);
			PV_UNLOCK(oldpvo->pvo_pte.pa & LPTE_RPGN);
			free_pvo_entry(oldpvo);
		}

		if (error != ENOMEM)
			break;
		if ((flags & PMAP_ENTER_NOSLEEP) != 0)
			return (KERN_RESOURCE_SHORTAGE);
		VM_OBJECT_ASSERT_UNLOCKED(m->object);
		vm_wait(NULL);
	}

	/*
	 * Flush the page from the instruction cache if this page is
	 * mapped executable and cacheable.
	 */
	if (pmap != kernel_pmap && !(m->aflags & PGA_EXECUTABLE) &&
	    (pte_lo & (LPTE_I | LPTE_G | LPTE_NOEXEC)) == 0) {
		vm_page_aflag_set(m, PGA_EXECUTABLE);
		moea64_syncicache(mmu, pmap, va, VM_PAGE_TO_PHYS(m), PAGE_SIZE);
	}
	return (KERN_SUCCESS);
}

static void
moea64_syncicache(mmu_t mmu, pmap_t pmap, vm_offset_t va, vm_paddr_t pa,
    vm_size_t sz)
{

	/*
	 * This is much trickier than on older systems because
	 * we can't sync the icache on physical addresses directly
	 * without a direct map. Instead we check a couple of cases
	 * where the memory is already mapped in and, failing that,
	 * use the same trick we use for page zeroing to create
	 * a temporary mapping for this physical address.
	 */

	if (!pmap_bootstrapped) {
		/*
		 * If PMAP is not bootstrapped, we are likely to be
		 * in real mode.
		 */
		__syncicache((void *)(uintptr_t)pa, sz);
	} else if (pmap == kernel_pmap) {
		__syncicache((void *)va, sz);
	} else if (hw_direct_map) {
		__syncicache((void *)(uintptr_t)PHYS_TO_DMAP(pa), sz);
	} else {
		/* Use the scratch page to set up a temp mapping */

		mtx_lock(&moea64_scratchpage_mtx);

		moea64_set_scratchpage_pa(mmu, 1, pa & ~ADDR_POFF);
		__syncicache((void *)(moea64_scratchpage_va[1] + 
		    (va & ADDR_POFF)), sz);

		mtx_unlock(&moea64_scratchpage_mtx);
	}
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
moea64_enter_object(mmu_t mmu, pmap_t pm, vm_offset_t start, vm_offset_t end,
    vm_page_t m_start, vm_prot_t prot)
{
	vm_page_t m;
	vm_pindex_t diff, psize;

	VM_OBJECT_ASSERT_LOCKED(m_start->object);

	psize = atop(end - start);
	m = m_start;
	while (m != NULL && (diff = m->pindex - m_start->pindex) < psize) {
		moea64_enter(mmu, pm, start + ptoa(diff), m, prot &
		    (VM_PROT_READ | VM_PROT_EXECUTE), PMAP_ENTER_NOSLEEP, 0);
		m = TAILQ_NEXT(m, listq);
	}
}

void
moea64_enter_quick(mmu_t mmu, pmap_t pm, vm_offset_t va, vm_page_t m,
    vm_prot_t prot)
{

	moea64_enter(mmu, pm, va, m, prot & (VM_PROT_READ | VM_PROT_EXECUTE),
	    PMAP_ENTER_NOSLEEP, 0);
}

vm_paddr_t
moea64_extract(mmu_t mmu, pmap_t pm, vm_offset_t va)
{
	struct	pvo_entry *pvo;
	vm_paddr_t pa;

	PMAP_LOCK(pm);
	pvo = moea64_pvo_find_va(pm, va);
	if (pvo == NULL)
		pa = 0;
	else
		pa = (pvo->pvo_pte.pa & LPTE_RPGN) | (va - PVO_VADDR(pvo));
	PMAP_UNLOCK(pm);

	return (pa);
}

/*
 * Atomically extract and hold the physical page with the given
 * pmap and virtual address pair if that mapping permits the given
 * protection.
 */
vm_page_t
moea64_extract_and_hold(mmu_t mmu, pmap_t pmap, vm_offset_t va, vm_prot_t prot)
{
	struct	pvo_entry *pvo;
	vm_page_t m;
        vm_paddr_t pa;
        
	m = NULL;
	pa = 0;
	PMAP_LOCK(pmap);
retry:
	pvo = moea64_pvo_find_va(pmap, va & ~ADDR_POFF);
	if (pvo != NULL && (pvo->pvo_pte.prot & prot) == prot) {
		if (vm_page_pa_tryrelock(pmap,
		    pvo->pvo_pte.pa & LPTE_RPGN, &pa))
			goto retry;
		m = PHYS_TO_VM_PAGE(pvo->pvo_pte.pa & LPTE_RPGN);
		vm_page_hold(m);
	}
	PA_UNLOCK_COND(pa);
	PMAP_UNLOCK(pmap);
	return (m);
}

static mmu_t installed_mmu;

static void *
moea64_uma_page_alloc(uma_zone_t zone, vm_size_t bytes, int domain,
    uint8_t *flags, int wait)
{
	struct pvo_entry *pvo;
        vm_offset_t va;
        vm_page_t m;
        int needed_lock;

	/*
	 * This entire routine is a horrible hack to avoid bothering kmem
	 * for new KVA addresses. Because this can get called from inside
	 * kmem allocation routines, calling kmem for a new address here
	 * can lead to multiply locking non-recursive mutexes.
	 */

	*flags = UMA_SLAB_PRIV;
	needed_lock = !PMAP_LOCKED(kernel_pmap);

	m = vm_page_alloc_domain(NULL, 0, domain,
	    malloc2vm_flags(wait) | VM_ALLOC_WIRED | VM_ALLOC_NOOBJ);
	if (m == NULL)
		return (NULL);

	va = VM_PAGE_TO_PHYS(m);

	pvo = alloc_pvo_entry(1 /* bootstrap */);

	pvo->pvo_pte.prot = VM_PROT_READ | VM_PROT_WRITE;
	pvo->pvo_pte.pa = VM_PAGE_TO_PHYS(m) | LPTE_M;

	if (needed_lock)
		PMAP_LOCK(kernel_pmap);

	init_pvo_entry(pvo, kernel_pmap, va);
	pvo->pvo_vaddr |= PVO_WIRED;

	moea64_pvo_enter(installed_mmu, pvo, NULL);

	if (needed_lock)
		PMAP_UNLOCK(kernel_pmap);
	
	if ((wait & M_ZERO) && (m->flags & PG_ZERO) == 0)
                bzero((void *)va, PAGE_SIZE);

	return (void *)va;
}

extern int elf32_nxstack;

void
moea64_init(mmu_t mmu)
{

	CTR0(KTR_PMAP, "moea64_init");

	moea64_pvo_zone = uma_zcreate("UPVO entry", sizeof (struct pvo_entry),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR,
	    UMA_ZONE_VM | UMA_ZONE_NOFREE);

	if (!hw_direct_map) {
		installed_mmu = mmu;
		uma_zone_set_allocf(moea64_pvo_zone, moea64_uma_page_alloc);
	}

#ifdef COMPAT_FREEBSD32
	elf32_nxstack = 1;
#endif

	moea64_initialized = TRUE;
}

boolean_t
moea64_is_referenced(mmu_t mmu, vm_page_t m)
{

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("moea64_is_referenced: page %p is not managed", m));

	return (moea64_query_bit(mmu, m, LPTE_REF));
}

boolean_t
moea64_is_modified(mmu_t mmu, vm_page_t m)
{

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("moea64_is_modified: page %p is not managed", m));

	/*
	 * If the page is not exclusive busied, then PGA_WRITEABLE cannot be
	 * concurrently set while the object is locked.  Thus, if PGA_WRITEABLE
	 * is clear, no PTEs can have LPTE_CHG set.
	 */
	VM_OBJECT_ASSERT_LOCKED(m->object);
	if (!vm_page_xbusied(m) && (m->aflags & PGA_WRITEABLE) == 0)
		return (FALSE);
	return (moea64_query_bit(mmu, m, LPTE_CHG));
}

boolean_t
moea64_is_prefaultable(mmu_t mmu, pmap_t pmap, vm_offset_t va)
{
	struct pvo_entry *pvo;
	boolean_t rv = TRUE;

	PMAP_LOCK(pmap);
	pvo = moea64_pvo_find_va(pmap, va & ~ADDR_POFF);
	if (pvo != NULL)
		rv = FALSE;
	PMAP_UNLOCK(pmap);
	return (rv);
}

void
moea64_clear_modify(mmu_t mmu, vm_page_t m)
{

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("moea64_clear_modify: page %p is not managed", m));
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	KASSERT(!vm_page_xbusied(m),
	    ("moea64_clear_modify: page %p is exclusive busied", m));

	/*
	 * If the page is not PGA_WRITEABLE, then no PTEs can have LPTE_CHG
	 * set.  If the object containing the page is locked and the page is
	 * not exclusive busied, then PGA_WRITEABLE cannot be concurrently set.
	 */
	if ((m->aflags & PGA_WRITEABLE) == 0)
		return;
	moea64_clear_bit(mmu, m, LPTE_CHG);
}

/*
 * Clear the write and modified bits in each of the given page's mappings.
 */
void
moea64_remove_write(mmu_t mmu, vm_page_t m)
{
	struct	pvo_entry *pvo;
	int64_t	refchg, ret;
	pmap_t	pmap;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("moea64_remove_write: page %p is not managed", m));

	/*
	 * If the page is not exclusive busied, then PGA_WRITEABLE cannot be
	 * set by another thread while the object is locked.  Thus,
	 * if PGA_WRITEABLE is clear, no page table entries need updating.
	 */
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (!vm_page_xbusied(m) && (m->aflags & PGA_WRITEABLE) == 0)
		return;
	powerpc_sync();
	PV_PAGE_LOCK(m);
	refchg = 0;
	LIST_FOREACH(pvo, vm_page_to_pvoh(m), pvo_vlink) {
		pmap = pvo->pvo_pmap;
		PMAP_LOCK(pmap);
		if (!(pvo->pvo_vaddr & PVO_DEAD) &&
		    (pvo->pvo_pte.prot & VM_PROT_WRITE)) {
			pvo->pvo_pte.prot &= ~VM_PROT_WRITE;
			ret = MOEA64_PTE_REPLACE(mmu, pvo,
			    MOEA64_PTE_PROT_UPDATE);
			if (ret < 0)
				ret = LPTE_CHG;
			refchg |= ret;
			if (pvo->pvo_pmap == kernel_pmap)
				isync();
		}
		PMAP_UNLOCK(pmap);
	}
	if ((refchg | atomic_readandclear_32(&m->md.mdpg_attrs)) & LPTE_CHG)
		vm_page_dirty(m);
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	PV_PAGE_UNLOCK(m);
}

/*
 *	moea64_ts_referenced:
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
moea64_ts_referenced(mmu_t mmu, vm_page_t m)
{

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("moea64_ts_referenced: page %p is not managed", m));
	return (moea64_clear_bit(mmu, m, LPTE_REF));
}

/*
 * Modify the WIMG settings of all mappings for a page.
 */
void
moea64_page_set_memattr(mmu_t mmu, vm_page_t m, vm_memattr_t ma)
{
	struct	pvo_entry *pvo;
	int64_t	refchg;
	pmap_t	pmap;
	uint64_t lo;

	if ((m->oflags & VPO_UNMANAGED) != 0) {
		m->md.mdpg_cache_attrs = ma;
		return;
	}

	lo = moea64_calc_wimg(VM_PAGE_TO_PHYS(m), ma);

	PV_PAGE_LOCK(m);
	LIST_FOREACH(pvo, vm_page_to_pvoh(m), pvo_vlink) {
		pmap = pvo->pvo_pmap;
		PMAP_LOCK(pmap);
		if (!(pvo->pvo_vaddr & PVO_DEAD)) {
			pvo->pvo_pte.pa &= ~LPTE_WIMG;
			pvo->pvo_pte.pa |= lo;
			refchg = MOEA64_PTE_REPLACE(mmu, pvo,
			    MOEA64_PTE_INVALIDATE);
			if (refchg < 0)
				refchg = (pvo->pvo_pte.prot & VM_PROT_WRITE) ?
				    LPTE_CHG : 0;
			if ((pvo->pvo_vaddr & PVO_MANAGED) &&
			    (pvo->pvo_pte.prot & VM_PROT_WRITE)) {
				refchg |=
				    atomic_readandclear_32(&m->md.mdpg_attrs);
				if (refchg & LPTE_CHG)
					vm_page_dirty(m);
				if (refchg & LPTE_REF)
					vm_page_aflag_set(m, PGA_REFERENCED);
			}
			if (pvo->pvo_pmap == kernel_pmap)
				isync();
		}
		PMAP_UNLOCK(pmap);
	}
	m->md.mdpg_cache_attrs = ma;
	PV_PAGE_UNLOCK(m);
}

/*
 * Map a wired page into kernel virtual address space.
 */
void
moea64_kenter_attr(mmu_t mmu, vm_offset_t va, vm_paddr_t pa, vm_memattr_t ma)
{
	int		error;	
	struct pvo_entry *pvo, *oldpvo;

	pvo = alloc_pvo_entry(0);
	pvo->pvo_pte.prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
	pvo->pvo_pte.pa = (pa & ~ADDR_POFF) | moea64_calc_wimg(pa, ma);
	pvo->pvo_vaddr |= PVO_WIRED;

	PMAP_LOCK(kernel_pmap);
	oldpvo = moea64_pvo_find_va(kernel_pmap, va);
	if (oldpvo != NULL)
		moea64_pvo_remove_from_pmap(mmu, oldpvo);
	init_pvo_entry(pvo, kernel_pmap, va);
	error = moea64_pvo_enter(mmu, pvo, NULL);
	PMAP_UNLOCK(kernel_pmap);

	/* Free any dead pages */
	if (oldpvo != NULL) {
		PV_LOCK(oldpvo->pvo_pte.pa & LPTE_RPGN);
		moea64_pvo_remove_from_page(mmu, oldpvo);
		PV_UNLOCK(oldpvo->pvo_pte.pa & LPTE_RPGN);
		free_pvo_entry(oldpvo);
	}

	if (error != 0 && error != ENOENT)
		panic("moea64_kenter: failed to enter va %#zx pa %#jx: %d", va,
		    (uintmax_t)pa, error);
}

void
moea64_kenter(mmu_t mmu, vm_offset_t va, vm_paddr_t pa)
{

	moea64_kenter_attr(mmu, va, pa, VM_MEMATTR_DEFAULT);
}

/*
 * Extract the physical page address associated with the given kernel virtual
 * address.
 */
vm_paddr_t
moea64_kextract(mmu_t mmu, vm_offset_t va)
{
	struct		pvo_entry *pvo;
	vm_paddr_t pa;

	/*
	 * Shortcut the direct-mapped case when applicable.  We never put
	 * anything but 1:1 (or 62-bit aliased) mappings below
	 * VM_MIN_KERNEL_ADDRESS.
	 */
	if (va < VM_MIN_KERNEL_ADDRESS)
		return (va & ~DMAP_BASE_ADDRESS);

	PMAP_LOCK(kernel_pmap);
	pvo = moea64_pvo_find_va(kernel_pmap, va);
	KASSERT(pvo != NULL, ("moea64_kextract: no addr found for %#" PRIxPTR,
	    va));
	pa = (pvo->pvo_pte.pa & LPTE_RPGN) | (va - PVO_VADDR(pvo));
	PMAP_UNLOCK(kernel_pmap);
	return (pa);
}

/*
 * Remove a wired page from kernel virtual address space.
 */
void
moea64_kremove(mmu_t mmu, vm_offset_t va)
{
	moea64_remove(mmu, kernel_pmap, va, va + PAGE_SIZE);
}

/*
 * Provide a kernel pointer corresponding to a given userland pointer.
 * The returned pointer is valid until the next time this function is
 * called in this thread. This is used internally in copyin/copyout.
 */
static int
moea64_map_user_ptr(mmu_t mmu, pmap_t pm, volatile const void *uaddr,
    void **kaddr, size_t ulen, size_t *klen)
{
	size_t l;
#ifdef __powerpc64__
	struct slb *slb;
#endif
	register_t slbv;

	*kaddr = (char *)USER_ADDR + ((uintptr_t)uaddr & ~SEGMENT_MASK);
	l = ((char *)USER_ADDR + SEGMENT_LENGTH) - (char *)(*kaddr);
	if (l > ulen)
		l = ulen;
	if (klen)
		*klen = l;
	else if (l != ulen)
		return (EFAULT);

#ifdef __powerpc64__
	/* Try lockless look-up first */
	slb = user_va_to_slb_entry(pm, (vm_offset_t)uaddr);

	if (slb == NULL) {
		/* If it isn't there, we need to pre-fault the VSID */
		PMAP_LOCK(pm);
		slbv = va_to_vsid(pm, (vm_offset_t)uaddr) << SLBV_VSID_SHIFT;
		PMAP_UNLOCK(pm);
	} else {
		slbv = slb->slbv;
	}

	/* Mark segment no-execute */
	slbv |= SLBV_N;
#else
	slbv = va_to_vsid(pm, (vm_offset_t)uaddr);

	/* Mark segment no-execute */
	slbv |= SR_N;
#endif

	/* If we have already set this VSID, we can just return */
	if (curthread->td_pcb->pcb_cpu.aim.usr_vsid == slbv)
		return (0);
  
	__asm __volatile("isync");
	curthread->td_pcb->pcb_cpu.aim.usr_segm =
	    (uintptr_t)uaddr >> ADDR_SR_SHFT;
	curthread->td_pcb->pcb_cpu.aim.usr_vsid = slbv;
#ifdef __powerpc64__
	__asm __volatile ("slbie %0; slbmte %1, %2; isync" ::
	    "r"(USER_ADDR), "r"(slbv), "r"(USER_SLB_SLBE));
#else
	__asm __volatile("mtsr %0,%1; isync" :: "n"(USER_SR), "r"(slbv));
#endif

	return (0);
}

/*
 * Figure out where a given kernel pointer (usually in a fault) points
 * to from the VM's perspective, potentially remapping into userland's
 * address space.
 */
static int
moea64_decode_kernel_ptr(mmu_t mmu, vm_offset_t addr, int *is_user,
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
 * unchanged.  Other architectures should map the pages starting at '*virt' and
 * update '*virt' with the first usable address after the mapped region.
 */
vm_offset_t
moea64_map(mmu_t mmu, vm_offset_t *virt, vm_paddr_t pa_start,
    vm_paddr_t pa_end, int prot)
{
	vm_offset_t	sva, va;

	if (hw_direct_map) {
		/*
		 * Check if every page in the region is covered by the direct
		 * map. The direct map covers all of physical memory. Use
		 * moea64_calc_wimg() as a shortcut to see if the page is in
		 * physical memory as a way to see if the direct map covers it.
		 */
		for (va = pa_start; va < pa_end; va += PAGE_SIZE)
			if (moea64_calc_wimg(va, VM_MEMATTR_DEFAULT) != LPTE_M)
				break;
		if (va == pa_end)
			return (PHYS_TO_DMAP(pa_start));
	}
	sva = *virt;
	va = sva;
	/* XXX respect prot argument */
	for (; pa_start < pa_end; pa_start += PAGE_SIZE, va += PAGE_SIZE)
		moea64_kenter(mmu, va, pa_start);
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
moea64_page_exists_quick(mmu_t mmu, pmap_t pmap, vm_page_t m)
{
        int loops;
	struct pvo_entry *pvo;
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("moea64_page_exists_quick: page %p is not managed", m));
	loops = 0;
	rv = FALSE;
	PV_PAGE_LOCK(m);
	LIST_FOREACH(pvo, vm_page_to_pvoh(m), pvo_vlink) {
		if (!(pvo->pvo_vaddr & PVO_DEAD) && pvo->pvo_pmap == pmap) {
			rv = TRUE;
			break;
		}
		if (++loops >= 16)
			break;
	}
	PV_PAGE_UNLOCK(m);
	return (rv);
}

void
moea64_page_init(mmu_t mmu __unused, vm_page_t m)
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
moea64_page_wired_mappings(mmu_t mmu, vm_page_t m)
{
	struct pvo_entry *pvo;
	int count;

	count = 0;
	if ((m->oflags & VPO_UNMANAGED) != 0)
		return (count);
	PV_PAGE_LOCK(m);
	LIST_FOREACH(pvo, vm_page_to_pvoh(m), pvo_vlink)
		if ((pvo->pvo_vaddr & (PVO_DEAD | PVO_WIRED)) == PVO_WIRED)
			count++;
	PV_PAGE_UNLOCK(m);
	return (count);
}

static uintptr_t	moea64_vsidcontext;

uintptr_t
moea64_get_unique_vsid(void) {
	u_int entropy;
	register_t hash;
	uint32_t mask;
	int i;

	entropy = 0;
	__asm __volatile("mftb %0" : "=r"(entropy));

	mtx_lock(&moea64_slb_mutex);
	for (i = 0; i < NVSIDS; i += VSID_NBPW) {
		u_int	n;

		/*
		 * Create a new value by mutiplying by a prime and adding in
		 * entropy from the timebase register.  This is to make the
		 * VSID more random so that the PT hash function collides
		 * less often.  (Note that the prime casues gcc to do shifts
		 * instead of a multiply.)
		 */
		moea64_vsidcontext = (moea64_vsidcontext * 0x1105) + entropy;
		hash = moea64_vsidcontext & (NVSIDS - 1);
		if (hash == 0)		/* 0 is special, avoid it */
			continue;
		n = hash >> 5;
		mask = 1 << (hash & (VSID_NBPW - 1));
		hash = (moea64_vsidcontext & VSID_HASHMASK);
		if (moea64_vsid_bitmap[n] & mask) {	/* collision? */
			/* anything free in this bucket? */
			if (moea64_vsid_bitmap[n] == 0xffffffff) {
				entropy = (moea64_vsidcontext >> 20);
				continue;
			}
			i = ffs(~moea64_vsid_bitmap[n]) - 1;
			mask = 1 << i;
			hash &= rounddown2(VSID_HASHMASK, VSID_NBPW);
			hash |= i;
		}
		if (hash == VSID_VRMA)	/* also special, avoid this too */
			continue;
		KASSERT(!(moea64_vsid_bitmap[n] & mask),
		    ("Allocating in-use VSID %#zx\n", hash));
		moea64_vsid_bitmap[n] |= mask;
		mtx_unlock(&moea64_slb_mutex);
		return (hash);
	}

	mtx_unlock(&moea64_slb_mutex);
	panic("%s: out of segments",__func__);
}

#ifdef __powerpc64__
void
moea64_pinit(mmu_t mmu, pmap_t pmap)
{

	RB_INIT(&pmap->pmap_pvo);

	pmap->pm_slb_tree_root = slb_alloc_tree();
	pmap->pm_slb = slb_alloc_user_cache();
	pmap->pm_slb_len = 0;
}
#else
void
moea64_pinit(mmu_t mmu, pmap_t pmap)
{
	int	i;
	uint32_t hash;

	RB_INIT(&pmap->pmap_pvo);

	if (pmap_bootstrapped)
		pmap->pmap_phys = (pmap_t)moea64_kextract(mmu,
		    (vm_offset_t)pmap);
	else
		pmap->pmap_phys = pmap;

	/*
	 * Allocate some segment registers for this pmap.
	 */
	hash = moea64_get_unique_vsid();

	for (i = 0; i < 16; i++) 
		pmap->pm_sr[i] = VSID_MAKE(i, hash);

	KASSERT(pmap->pm_sr[0] != 0, ("moea64_pinit: pm_sr[0] = 0"));
}
#endif

/*
 * Initialize the pmap associated with process 0.
 */
void
moea64_pinit0(mmu_t mmu, pmap_t pm)
{

	PMAP_LOCK_INIT(pm);
	moea64_pinit(mmu, pm);
	bzero(&pm->pm_stats, sizeof(pm->pm_stats));
}

/*
 * Set the physical protection on the specified range of this map as requested.
 */
static void
moea64_pvo_protect(mmu_t mmu,  pmap_t pm, struct pvo_entry *pvo, vm_prot_t prot)
{
	struct vm_page *pg;
	vm_prot_t oldprot;
	int32_t refchg;

	PMAP_LOCK_ASSERT(pm, MA_OWNED);

	/*
	 * Change the protection of the page.
	 */
	oldprot = pvo->pvo_pte.prot;
	pvo->pvo_pte.prot = prot;
	pg = PHYS_TO_VM_PAGE(pvo->pvo_pte.pa & LPTE_RPGN);

	/*
	 * If the PVO is in the page table, update mapping
	 */
	refchg = MOEA64_PTE_REPLACE(mmu, pvo, MOEA64_PTE_PROT_UPDATE);
	if (refchg < 0)
		refchg = (oldprot & VM_PROT_WRITE) ? LPTE_CHG : 0;

	if (pm != kernel_pmap && pg != NULL && !(pg->aflags & PGA_EXECUTABLE) &&
	    (pvo->pvo_pte.pa & (LPTE_I | LPTE_G | LPTE_NOEXEC)) == 0) {
		if ((pg->oflags & VPO_UNMANAGED) == 0)
			vm_page_aflag_set(pg, PGA_EXECUTABLE);
		moea64_syncicache(mmu, pm, PVO_VADDR(pvo),
		    pvo->pvo_pte.pa & LPTE_RPGN, PAGE_SIZE);
	}

	/*
	 * Update vm about the REF/CHG bits if the page is managed and we have
	 * removed write access.
	 */
	if (pg != NULL && (pvo->pvo_vaddr & PVO_MANAGED) &&
	    (oldprot & VM_PROT_WRITE)) {
		refchg |= atomic_readandclear_32(&pg->md.mdpg_attrs);
		if (refchg & LPTE_CHG)
			vm_page_dirty(pg);
		if (refchg & LPTE_REF)
			vm_page_aflag_set(pg, PGA_REFERENCED);
	}
}

void
moea64_protect(mmu_t mmu, pmap_t pm, vm_offset_t sva, vm_offset_t eva,
    vm_prot_t prot)
{
	struct	pvo_entry *pvo, *tpvo, key;

	CTR4(KTR_PMAP, "moea64_protect: pm=%p sva=%#x eva=%#x prot=%#x", pm,
	    sva, eva, prot);

	KASSERT(pm == &curproc->p_vmspace->vm_pmap || pm == kernel_pmap,
	    ("moea64_protect: non current pmap"));

	if ((prot & VM_PROT_READ) == VM_PROT_NONE) {
		moea64_remove(mmu, pm, sva, eva);
		return;
	}

	PMAP_LOCK(pm);
	key.pvo_vaddr = sva;
	for (pvo = RB_NFIND(pvo_tree, &pm->pmap_pvo, &key);
	    pvo != NULL && PVO_VADDR(pvo) < eva; pvo = tpvo) {
		tpvo = RB_NEXT(pvo_tree, &pm->pmap_pvo, pvo);
		moea64_pvo_protect(mmu, pm, pvo, prot);
	}
	PMAP_UNLOCK(pm);
}

/*
 * Map a list of wired pages into kernel virtual address space.  This is
 * intended for temporary mappings which do not need page modification or
 * references recorded.  Existing mappings in the region are overwritten.
 */
void
moea64_qenter(mmu_t mmu, vm_offset_t va, vm_page_t *m, int count)
{
	while (count-- > 0) {
		moea64_kenter(mmu, va, VM_PAGE_TO_PHYS(*m));
		va += PAGE_SIZE;
		m++;
	}
}

/*
 * Remove page mappings from kernel virtual address space.  Intended for
 * temporary mappings entered by moea64_qenter.
 */
void
moea64_qremove(mmu_t mmu, vm_offset_t va, int count)
{
	while (count-- > 0) {
		moea64_kremove(mmu, va);
		va += PAGE_SIZE;
	}
}

void
moea64_release_vsid(uint64_t vsid)
{
	int idx, mask;

	mtx_lock(&moea64_slb_mutex);
	idx = vsid & (NVSIDS-1);
	mask = 1 << (idx % VSID_NBPW);
	idx /= VSID_NBPW;
	KASSERT(moea64_vsid_bitmap[idx] & mask,
	    ("Freeing unallocated VSID %#jx", vsid));
	moea64_vsid_bitmap[idx] &= ~mask;
	mtx_unlock(&moea64_slb_mutex);
}
	

void
moea64_release(mmu_t mmu, pmap_t pmap)
{
        
	/*
	 * Free segment registers' VSIDs
	 */
    #ifdef __powerpc64__
	slb_free_tree(pmap);
	slb_free_user_cache(pmap->pm_slb);
    #else
	KASSERT(pmap->pm_sr[0] != 0, ("moea64_release: pm_sr[0] = 0"));

	moea64_release_vsid(VSID_TO_HASH(pmap->pm_sr[0]));
    #endif
}

/*
 * Remove all pages mapped by the specified pmap
 */
void
moea64_remove_pages(mmu_t mmu, pmap_t pm)
{
	struct pvo_entry *pvo, *tpvo;
	struct pvo_tree tofree;

	RB_INIT(&tofree);

	PMAP_LOCK(pm);
	RB_FOREACH_SAFE(pvo, pvo_tree, &pm->pmap_pvo, tpvo) {
		if (pvo->pvo_vaddr & PVO_WIRED)
			continue;

		/*
		 * For locking reasons, remove this from the page table and
		 * pmap, but save delinking from the vm_page for a second
		 * pass
		 */
		moea64_pvo_remove_from_pmap(mmu, pvo);
		RB_INSERT(pvo_tree, &tofree, pvo);
	}
	PMAP_UNLOCK(pm);

	RB_FOREACH_SAFE(pvo, pvo_tree, &tofree, tpvo) {
		PV_LOCK(pvo->pvo_pte.pa & LPTE_RPGN);
		moea64_pvo_remove_from_page(mmu, pvo);
		PV_UNLOCK(pvo->pvo_pte.pa & LPTE_RPGN);
		RB_REMOVE(pvo_tree, &tofree, pvo);
		free_pvo_entry(pvo);
	}
}

/*
 * Remove the given range of addresses from the specified map.
 */
void
moea64_remove(mmu_t mmu, pmap_t pm, vm_offset_t sva, vm_offset_t eva)
{
	struct  pvo_entry *pvo, *tpvo, key;
	struct pvo_tree tofree;

	/*
	 * Perform an unsynchronized read.  This is, however, safe.
	 */
	if (pm->pm_stats.resident_count == 0)
		return;

	key.pvo_vaddr = sva;

	RB_INIT(&tofree);

	PMAP_LOCK(pm);
	for (pvo = RB_NFIND(pvo_tree, &pm->pmap_pvo, &key);
	    pvo != NULL && PVO_VADDR(pvo) < eva; pvo = tpvo) {
		tpvo = RB_NEXT(pvo_tree, &pm->pmap_pvo, pvo);

		/*
		 * For locking reasons, remove this from the page table and
		 * pmap, but save delinking from the vm_page for a second
		 * pass
		 */
		moea64_pvo_remove_from_pmap(mmu, pvo);
		RB_INSERT(pvo_tree, &tofree, pvo);
	}
	PMAP_UNLOCK(pm);

	RB_FOREACH_SAFE(pvo, pvo_tree, &tofree, tpvo) {
		PV_LOCK(pvo->pvo_pte.pa & LPTE_RPGN);
		moea64_pvo_remove_from_page(mmu, pvo);
		PV_UNLOCK(pvo->pvo_pte.pa & LPTE_RPGN);
		RB_REMOVE(pvo_tree, &tofree, pvo);
		free_pvo_entry(pvo);
	}
}

/*
 * Remove physical page from all pmaps in which it resides. moea64_pvo_remove()
 * will reflect changes in pte's back to the vm_page.
 */
void
moea64_remove_all(mmu_t mmu, vm_page_t m)
{
	struct	pvo_entry *pvo, *next_pvo;
	struct	pvo_head freequeue;
	int	wasdead;
	pmap_t	pmap;

	LIST_INIT(&freequeue);

	PV_PAGE_LOCK(m);
	LIST_FOREACH_SAFE(pvo, vm_page_to_pvoh(m), pvo_vlink, next_pvo) {
		pmap = pvo->pvo_pmap;
		PMAP_LOCK(pmap);
		wasdead = (pvo->pvo_vaddr & PVO_DEAD);
		if (!wasdead)
			moea64_pvo_remove_from_pmap(mmu, pvo);
		moea64_pvo_remove_from_page(mmu, pvo);
		if (!wasdead)
			LIST_INSERT_HEAD(&freequeue, pvo, pvo_vlink);
		PMAP_UNLOCK(pmap);
		
	}
	KASSERT(!pmap_page_is_mapped(m), ("Page still has mappings"));
	KASSERT(!(m->aflags & PGA_WRITEABLE), ("Page still writable"));
	PV_PAGE_UNLOCK(m);

	/* Clean up UMA allocations */
	LIST_FOREACH_SAFE(pvo, &freequeue, pvo_vlink, next_pvo)
		free_pvo_entry(pvo);
}

/*
 * Allocate a physical page of memory directly from the phys_avail map.
 * Can only be called from moea64_bootstrap before avail start and end are
 * calculated.
 */
vm_offset_t
moea64_bootstrap_alloc(vm_size_t size, vm_size_t align)
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

		if (s + size > platform_real_maxaddr())
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
	panic("moea64_bootstrap_alloc: could not allocate memory");
}

static int
moea64_pvo_enter(mmu_t mmu, struct pvo_entry *pvo, struct pvo_head *pvo_head)
{
	int first, err;

	PMAP_LOCK_ASSERT(pvo->pvo_pmap, MA_OWNED);
	KASSERT(moea64_pvo_find_va(pvo->pvo_pmap, PVO_VADDR(pvo)) == NULL,
	    ("Existing mapping for VA %#jx", (uintmax_t)PVO_VADDR(pvo)));

	moea64_pvo_enter_calls++;

	/*
	 * Add to pmap list
	 */
	RB_INSERT(pvo_tree, &pvo->pvo_pmap->pmap_pvo, pvo);

	/*
	 * Remember if the list was empty and therefore will be the first
	 * item.
	 */
	if (pvo_head != NULL) {
		if (LIST_FIRST(pvo_head) == NULL)
			first = 1;
		LIST_INSERT_HEAD(pvo_head, pvo, pvo_vlink);
	}

	if (pvo->pvo_vaddr & PVO_WIRED)
		pvo->pvo_pmap->pm_stats.wired_count++;
	pvo->pvo_pmap->pm_stats.resident_count++;

	/*
	 * Insert it into the hardware page table
	 */
	err = MOEA64_PTE_INSERT(mmu, pvo);
	if (err != 0) {
		panic("moea64_pvo_enter: overflow");
	}

	moea64_pvo_entries++;

	if (pvo->pvo_pmap == kernel_pmap)
		isync();

#ifdef __powerpc64__
	/*
	 * Make sure all our bootstrap mappings are in the SLB as soon
	 * as virtual memory is switched on.
	 */
	if (!pmap_bootstrapped)
		moea64_bootstrap_slb_prefault(PVO_VADDR(pvo),
		    pvo->pvo_vaddr & PVO_LARGE);
#endif

	return (first ? ENOENT : 0);
}

static void
moea64_pvo_remove_from_pmap(mmu_t mmu, struct pvo_entry *pvo)
{
	struct	vm_page *pg;
	int32_t refchg;

	KASSERT(pvo->pvo_pmap != NULL, ("Trying to remove PVO with no pmap"));
	PMAP_LOCK_ASSERT(pvo->pvo_pmap, MA_OWNED);
	KASSERT(!(pvo->pvo_vaddr & PVO_DEAD), ("Trying to remove dead PVO"));

	/*
	 * If there is an active pte entry, we need to deactivate it
	 */
	refchg = MOEA64_PTE_UNSET(mmu, pvo);
	if (refchg < 0) {
		/*
		 * If it was evicted from the page table, be pessimistic and
		 * dirty the page.
		 */
		if (pvo->pvo_pte.prot & VM_PROT_WRITE)
			refchg = LPTE_CHG;
		else
			refchg = 0;
	}

	/*
	 * Update our statistics.
	 */
	pvo->pvo_pmap->pm_stats.resident_count--;
	if (pvo->pvo_vaddr & PVO_WIRED)
		pvo->pvo_pmap->pm_stats.wired_count--;

	/*
	 * Remove this PVO from the pmap list.
	 */
	RB_REMOVE(pvo_tree, &pvo->pvo_pmap->pmap_pvo, pvo);

	/*
	 * Mark this for the next sweep
	 */
	pvo->pvo_vaddr |= PVO_DEAD;

	/* Send RC bits to VM */
	if ((pvo->pvo_vaddr & PVO_MANAGED) &&
	    (pvo->pvo_pte.prot & VM_PROT_WRITE)) {
		pg = PHYS_TO_VM_PAGE(pvo->pvo_pte.pa & LPTE_RPGN);
		if (pg != NULL) {
			refchg |= atomic_readandclear_32(&pg->md.mdpg_attrs);
			if (refchg & LPTE_CHG)
				vm_page_dirty(pg);
			if (refchg & LPTE_REF)
				vm_page_aflag_set(pg, PGA_REFERENCED);
		}
	}
}

static void
moea64_pvo_remove_from_page(mmu_t mmu, struct pvo_entry *pvo)
{
	struct	vm_page *pg;

	KASSERT(pvo->pvo_vaddr & PVO_DEAD, ("Trying to delink live page"));

	/* Use NULL pmaps as a sentinel for races in page deletion */
	if (pvo->pvo_pmap == NULL)
		return;
	pvo->pvo_pmap = NULL;

	/*
	 * Update vm about page writeability/executability if managed
	 */
	PV_LOCKASSERT(pvo->pvo_pte.pa & LPTE_RPGN);
	if (pvo->pvo_vaddr & PVO_MANAGED) {
		pg = PHYS_TO_VM_PAGE(pvo->pvo_pte.pa & LPTE_RPGN);

		if (pg != NULL) {
			LIST_REMOVE(pvo, pvo_vlink);
			if (LIST_EMPTY(vm_page_to_pvoh(pg)))
				vm_page_aflag_clear(pg,
				    PGA_WRITEABLE | PGA_EXECUTABLE);
		}
	}

	moea64_pvo_entries--;
	moea64_pvo_remove_calls++;
}

static struct pvo_entry *
moea64_pvo_find_va(pmap_t pm, vm_offset_t va)
{
	struct pvo_entry key;

	PMAP_LOCK_ASSERT(pm, MA_OWNED);

	key.pvo_vaddr = va & ~ADDR_POFF;
	return (RB_FIND(pvo_tree, &pm->pmap_pvo, &key));
}

static boolean_t
moea64_query_bit(mmu_t mmu, vm_page_t m, uint64_t ptebit)
{
	struct	pvo_entry *pvo;
	int64_t ret;
	boolean_t rv;

	/*
	 * See if this bit is stored in the page already.
	 */
	if (m->md.mdpg_attrs & ptebit)
		return (TRUE);

	/*
	 * Examine each PTE.  Sync so that any pending REF/CHG bits are
	 * flushed to the PTEs.
	 */
	rv = FALSE;
	powerpc_sync();
	PV_PAGE_LOCK(m);
	LIST_FOREACH(pvo, vm_page_to_pvoh(m), pvo_vlink) {
		ret = 0;

		/*
		 * See if this pvo has a valid PTE.  if so, fetch the
		 * REF/CHG bits from the valid PTE.  If the appropriate
		 * ptebit is set, return success.
		 */
		PMAP_LOCK(pvo->pvo_pmap);
		if (!(pvo->pvo_vaddr & PVO_DEAD))
			ret = MOEA64_PTE_SYNCH(mmu, pvo);
		PMAP_UNLOCK(pvo->pvo_pmap);

		if (ret > 0) {
			atomic_set_32(&m->md.mdpg_attrs,
			    ret & (LPTE_CHG | LPTE_REF));
			if (ret & ptebit) {
				rv = TRUE;
				break;
			}
		}
	}
	PV_PAGE_UNLOCK(m);

	return (rv);
}

static u_int
moea64_clear_bit(mmu_t mmu, vm_page_t m, u_int64_t ptebit)
{
	u_int	count;
	struct	pvo_entry *pvo;
	int64_t ret;

	/*
	 * Sync so that any pending REF/CHG bits are flushed to the PTEs (so
	 * we can reset the right ones).
	 */
	powerpc_sync();

	/*
	 * For each pvo entry, clear the pte's ptebit.
	 */
	count = 0;
	PV_PAGE_LOCK(m);
	LIST_FOREACH(pvo, vm_page_to_pvoh(m), pvo_vlink) {
		ret = 0;

		PMAP_LOCK(pvo->pvo_pmap);
		if (!(pvo->pvo_vaddr & PVO_DEAD))
			ret = MOEA64_PTE_CLEAR(mmu, pvo, ptebit);
		PMAP_UNLOCK(pvo->pvo_pmap);

		if (ret > 0 && (ret & ptebit))
			count++;
	}
	atomic_clear_32(&m->md.mdpg_attrs, ptebit);
	PV_PAGE_UNLOCK(m);

	return (count);
}

boolean_t
moea64_dev_direct_mapped(mmu_t mmu, vm_paddr_t pa, vm_size_t size)
{
	struct pvo_entry *pvo, key;
	vm_offset_t ppa;
	int error = 0;

	if (hw_direct_map && mem_valid(pa, size) == 0)
		return (0);

	PMAP_LOCK(kernel_pmap);
	ppa = pa & ~ADDR_POFF;
	key.pvo_vaddr = DMAP_BASE_ADDRESS + ppa;
	for (pvo = RB_FIND(pvo_tree, &kernel_pmap->pmap_pvo, &key);
	    ppa < pa + size; ppa += PAGE_SIZE,
	    pvo = RB_NEXT(pvo_tree, &kernel_pmap->pmap_pvo, pvo)) {
		if (pvo == NULL || (pvo->pvo_pte.pa & LPTE_RPGN) != ppa) {
			error = EFAULT;
			break;
		}
	}
	PMAP_UNLOCK(kernel_pmap);

	return (error);
}

/*
 * Map a set of physical memory pages into the kernel virtual
 * address space. Return a pointer to where it is mapped. This
 * routine is intended to be used for mapping device memory,
 * NOT real memory.
 */
void *
moea64_mapdev_attr(mmu_t mmu, vm_paddr_t pa, vm_size_t size, vm_memattr_t ma)
{
	vm_offset_t va, tmpva, ppa, offset;

	ppa = trunc_page(pa);
	offset = pa & PAGE_MASK;
	size = roundup2(offset + size, PAGE_SIZE);

	va = kva_alloc(size);

	if (!va)
		panic("moea64_mapdev: Couldn't alloc kernel virtual memory");

	for (tmpva = va; size > 0;) {
		moea64_kenter_attr(mmu, tmpva, ppa, ma);
		size -= PAGE_SIZE;
		tmpva += PAGE_SIZE;
		ppa += PAGE_SIZE;
	}

	return ((void *)(va + offset));
}

void *
moea64_mapdev(mmu_t mmu, vm_paddr_t pa, vm_size_t size)
{

	return moea64_mapdev_attr(mmu, pa, size, VM_MEMATTR_DEFAULT);
}

void
moea64_unmapdev(mmu_t mmu, vm_offset_t va, vm_size_t size)
{
	vm_offset_t base, offset;

	base = trunc_page(va);
	offset = va & PAGE_MASK;
	size = roundup2(offset + size, PAGE_SIZE);

	kva_free(base, size);
}

void
moea64_sync_icache(mmu_t mmu, pmap_t pm, vm_offset_t va, vm_size_t sz)
{
	struct pvo_entry *pvo;
	vm_offset_t lim;
	vm_paddr_t pa;
	vm_size_t len;

	PMAP_LOCK(pm);
	while (sz > 0) {
		lim = round_page(va+1);
		len = MIN(lim - va, sz);
		pvo = moea64_pvo_find_va(pm, va & ~ADDR_POFF);
		if (pvo != NULL && !(pvo->pvo_pte.pa & LPTE_I)) {
			pa = (pvo->pvo_pte.pa & LPTE_RPGN) | (va & ADDR_POFF);
			moea64_syncicache(mmu, pm, va, pa, len);
		}
		va += len;
		sz -= len;
	}
	PMAP_UNLOCK(pm);
}

void
moea64_dumpsys_map(mmu_t mmu, vm_paddr_t pa, size_t sz, void **va)
{

	*va = (void *)(uintptr_t)pa;
}

extern struct dump_pa dump_map[PHYS_AVAIL_SZ + 1];

void
moea64_scan_init(mmu_t mmu)
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
	dump_map[0].pa_size = round_page((uintptr_t)_end) -
	    dump_map[0].pa_start;

	/* 2nd: msgbuf and tables (see pmap_bootstrap()). */
	dump_map[1].pa_start = (vm_paddr_t)(uintptr_t)msgbufp->msg_ptr;
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
		pvo = moea64_pvo_find_va(kernel_pmap, va & ~ADDR_POFF);
		if (pvo != NULL && !(pvo->pvo_vaddr & PVO_DEAD))
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
			pvo = moea64_pvo_find_va(kernel_pmap, va & ~ADDR_POFF);
			if (pvo == NULL || (pvo->pvo_vaddr & PVO_DEAD))
				break;
			va += PAGE_SIZE;
		}
		dump_map[2].pa_size = va - dump_map[2].pa_start;
	}
}

