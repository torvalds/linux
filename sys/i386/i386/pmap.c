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

/*
 *	Manages physical address maps.
 *
 *	Since the information managed by this module is
 *	also stored by the logical address mapping module,
 *	this module may throw away valid virtual-to-physical
 *	mappings at almost any time.  However, invalidations
 *	of virtual-to-physical mappings must be done as
 *	requested.
 *
 *	In order to cope with hardware architectures which
 *	make virtual-to-physical map invalidates expensive,
 *	this module may delay invalidate or reduced protection
 *	operations until such time as they are actually
 *	necessary.  This module is given full information as
 *	to which processors are currently using which maps,
 *	and to when physical maps must be made correct.
 */

#include "opt_apic.h"
#include "opt_cpu.h"
#include "opt_pmap.h"
#include "opt_smp.h"
#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/msgbuf.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sf_buf.h>
#include <sys/sx.h>
#include <sys/vmmeter.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <sys/smp.h>
#include <sys/vmem.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/vm_phys.h>
#include <vm/vm_radix.h>
#include <vm/vm_reserv.h>
#include <vm/uma.h>

#ifdef DEV_APIC
#include <sys/bus.h>
#include <machine/intr_machdep.h>
#include <x86/apicvar.h>
#endif
#include <x86/ifunc.h>
#include <machine/bootinfo.h>
#include <machine/cpu.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/specialreg.h>
#ifdef SMP
#include <machine/smp.h>
#endif
#include <machine/pmap_base.h>

#if !defined(DIAGNOSTIC)
#ifdef __GNUC_GNU_INLINE__
#define PMAP_INLINE	__attribute__((__gnu_inline__)) inline
#else
#define PMAP_INLINE	extern inline
#endif
#else
#define PMAP_INLINE
#endif

#ifdef PV_STATS
#define PV_STAT(x)	do { x ; } while (0)
#else
#define PV_STAT(x)	do { } while (0)
#endif

#define	pa_index(pa)	((pa) >> PDRSHIFT)
#define	pa_to_pvh(pa)	(&pv_table[pa_index(pa)])

/*
 * PTmap is recursive pagemap at top of virtual address space.
 * Within PTmap, the page directory can be found (third indirection).
 */
#define	PTmap	((pt_entry_t *)(PTDPTDI << PDRSHIFT))
#define	PTD	((pd_entry_t *)((PTDPTDI << PDRSHIFT) + (PTDPTDI * PAGE_SIZE)))
#define	PTDpde	((pd_entry_t *)((PTDPTDI << PDRSHIFT) + (PTDPTDI * PAGE_SIZE) + \
    (PTDPTDI * PDESIZE)))

/*
 * Translate a virtual address to the kernel virtual address of its page table
 * entry (PTE).  This can be used recursively.  If the address of a PTE as
 * previously returned by this macro is itself given as the argument, then the
 * address of the page directory entry (PDE) that maps the PTE will be
 * returned.
 *
 * This macro may be used before pmap_bootstrap() is called.
 */
#define	vtopte(va)	(PTmap + i386_btop(va))

/*
 * Get PDEs and PTEs for user/kernel address space
 */
#define	pmap_pde(m, v)	(&((m)->pm_pdir[(vm_offset_t)(v) >> PDRSHIFT]))
#define pdir_pde(m, v) (m[(vm_offset_t)(v) >> PDRSHIFT])

#define pmap_pde_v(pte)		((*(int *)pte & PG_V) != 0)
#define pmap_pte_w(pte)		((*(int *)pte & PG_W) != 0)
#define pmap_pte_m(pte)		((*(int *)pte & PG_M) != 0)
#define pmap_pte_u(pte)		((*(int *)pte & PG_A) != 0)
#define pmap_pte_v(pte)		((*(int *)pte & PG_V) != 0)

#define pmap_pte_set_w(pte, v)	((v) ? atomic_set_int((u_int *)(pte), PG_W) : \
    atomic_clear_int((u_int *)(pte), PG_W))
#define pmap_pte_set_prot(pte, v) ((*(int *)pte &= ~PG_PROT), (*(int *)pte |= (v)))

_Static_assert(sizeof(struct pmap) <= sizeof(struct pmap_KBI),
    "pmap_KBI");

static int pgeflag = 0;		/* PG_G or-in */
static int pseflag = 0;		/* PG_PS or-in */

static int nkpt = NKPT;

#ifdef PMAP_PAE_COMP
pt_entry_t pg_nx;
static uma_zone_t pdptzone;
#endif

_Static_assert(VM_MAXUSER_ADDRESS == VADDR(TRPTDI, 0), "VM_MAXUSER_ADDRESS");
_Static_assert(VM_MAX_KERNEL_ADDRESS <= VADDR(PTDPTDI, 0),
    "VM_MAX_KERNEL_ADDRESS");
_Static_assert(PMAP_MAP_LOW == VADDR(LOWPTDI, 0), "PMAP_MAP_LOW");
_Static_assert(KERNLOAD == (KERNPTDI << PDRSHIFT), "KERNLOAD");

extern int pat_works;
extern int pg_ps_enabled;

extern int elf32_nxstack;

#define	PAT_INDEX_SIZE	8
static int pat_index[PAT_INDEX_SIZE];	/* cache mode to PAT index conversion */

/*
 * pmap_mapdev support pre initialization (i.e. console)
 */
#define	PMAP_PREINIT_MAPPING_COUNT	8
static struct pmap_preinit_mapping {
	vm_paddr_t	pa;
	vm_offset_t	va;
	vm_size_t	sz;
	int		mode;
} pmap_preinit_mapping[PMAP_PREINIT_MAPPING_COUNT];
static int pmap_initialized;

static struct rwlock_padalign pvh_global_lock;

/*
 * Data for the pv entry allocation mechanism
 */
static TAILQ_HEAD(pch, pv_chunk) pv_chunks = TAILQ_HEAD_INITIALIZER(pv_chunks);
extern int pv_entry_max, pv_entry_count;
static int pv_entry_high_water = 0;
static struct md_page *pv_table;
extern int shpgperproc;

static struct pv_chunk *pv_chunkbase;	/* KVA block for pv_chunks */
static int pv_maxchunks;		/* How many chunks we have KVA for */
static vm_offset_t pv_vafree;		/* freelist stored in the PTE */

/*
 * All those kernel PT submaps that BSD is so fond of
 */
static pt_entry_t *CMAP3;
static pd_entry_t *KPTD;
static caddr_t CADDR3;

/*
 * Crashdump maps.
 */
static caddr_t crashdumpmap;

static pt_entry_t *PMAP1 = NULL, *PMAP2, *PMAP3;
static pt_entry_t *PADDR1 = NULL, *PADDR2, *PADDR3;
#ifdef SMP
static int PMAP1cpu, PMAP3cpu;
extern int PMAP1changedcpu;
#endif
extern int PMAP1changed;
extern int PMAP1unchanged;
static struct mtx PMAP2mutex;

/*
 * Internal flags for pmap_enter()'s helper functions.
 */
#define	PMAP_ENTER_NORECLAIM	0x1000000	/* Don't reclaim PV entries. */
#define	PMAP_ENTER_NOREPLACE	0x2000000	/* Don't replace mappings. */

static void	free_pv_chunk(struct pv_chunk *pc);
static void	free_pv_entry(pmap_t pmap, pv_entry_t pv);
static pv_entry_t get_pv_entry(pmap_t pmap, boolean_t try);
static void	pmap_pv_demote_pde(pmap_t pmap, vm_offset_t va, vm_paddr_t pa);
static bool	pmap_pv_insert_pde(pmap_t pmap, vm_offset_t va, pd_entry_t pde,
		    u_int flags);
#if VM_NRESERVLEVEL > 0
static void	pmap_pv_promote_pde(pmap_t pmap, vm_offset_t va, vm_paddr_t pa);
#endif
static void	pmap_pvh_free(struct md_page *pvh, pmap_t pmap, vm_offset_t va);
static pv_entry_t pmap_pvh_remove(struct md_page *pvh, pmap_t pmap,
		    vm_offset_t va);
static int	pmap_pvh_wired_mappings(struct md_page *pvh, int count);

static boolean_t pmap_demote_pde(pmap_t pmap, pd_entry_t *pde, vm_offset_t va);
static bool	pmap_enter_4mpage(pmap_t pmap, vm_offset_t va, vm_page_t m,
		    vm_prot_t prot);
static int	pmap_enter_pde(pmap_t pmap, vm_offset_t va, pd_entry_t newpde,
		    u_int flags, vm_page_t m);
static vm_page_t pmap_enter_quick_locked(pmap_t pmap, vm_offset_t va,
    vm_page_t m, vm_prot_t prot, vm_page_t mpte);
static int pmap_insert_pt_page(pmap_t pmap, vm_page_t mpte);
static void pmap_invalidate_pde_page(pmap_t pmap, vm_offset_t va,
		    pd_entry_t pde);
static void pmap_fill_ptp(pt_entry_t *firstpte, pt_entry_t newpte);
static boolean_t pmap_is_modified_pvh(struct md_page *pvh);
static boolean_t pmap_is_referenced_pvh(struct md_page *pvh);
static void pmap_kenter_attr(vm_offset_t va, vm_paddr_t pa, int mode);
static void pmap_kenter_pde(vm_offset_t va, pd_entry_t newpde);
static void pmap_pde_attr(pd_entry_t *pde, int cache_bits);
#if VM_NRESERVLEVEL > 0
static void pmap_promote_pde(pmap_t pmap, pd_entry_t *pde, vm_offset_t va);
#endif
static boolean_t pmap_protect_pde(pmap_t pmap, pd_entry_t *pde, vm_offset_t sva,
    vm_prot_t prot);
static void pmap_pte_attr(pt_entry_t *pte, int cache_bits);
static void pmap_remove_pde(pmap_t pmap, pd_entry_t *pdq, vm_offset_t sva,
    struct spglist *free);
static int pmap_remove_pte(pmap_t pmap, pt_entry_t *ptq, vm_offset_t sva,
    struct spglist *free);
static vm_page_t pmap_remove_pt_page(pmap_t pmap, vm_offset_t va);
static void pmap_remove_page(struct pmap *pmap, vm_offset_t va,
    struct spglist *free);
static bool	pmap_remove_ptes(pmap_t pmap, vm_offset_t sva, vm_offset_t eva,
		    struct spglist *free);
static void pmap_remove_entry(struct pmap *pmap, vm_page_t m,
					vm_offset_t va);
static void pmap_insert_entry(pmap_t pmap, vm_offset_t va, vm_page_t m);
static boolean_t pmap_try_insert_pv_entry(pmap_t pmap, vm_offset_t va,
    vm_page_t m);
static void pmap_update_pde(pmap_t pmap, vm_offset_t va, pd_entry_t *pde,
    pd_entry_t newpde);
static void pmap_update_pde_invalidate(vm_offset_t va, pd_entry_t newpde);

static vm_page_t pmap_allocpte(pmap_t pmap, vm_offset_t va, u_int flags);

static vm_page_t _pmap_allocpte(pmap_t pmap, u_int ptepindex, u_int flags);
static void _pmap_unwire_ptp(pmap_t pmap, vm_page_t m, struct spglist *free);
static pt_entry_t *pmap_pte_quick(pmap_t pmap, vm_offset_t va);
static void pmap_pte_release(pt_entry_t *pte);
static int pmap_unuse_pt(pmap_t, vm_offset_t, struct spglist *);
#ifdef PMAP_PAE_COMP
static void *pmap_pdpt_allocf(uma_zone_t zone, vm_size_t bytes, int domain,
    uint8_t *flags, int wait);
#endif
static void pmap_init_trm(void);
static void pmap_invalidate_all_int(pmap_t pmap);

static __inline void pagezero(void *page);

CTASSERT(1 << PDESHIFT == sizeof(pd_entry_t));
CTASSERT(1 << PTESHIFT == sizeof(pt_entry_t));

extern char _end[];
extern u_long physfree;	/* phys addr of next free page */
extern u_long vm86phystk;/* PA of vm86/bios stack */
extern u_long vm86paddr;/* address of vm86 region */
extern int vm86pa;	/* phys addr of vm86 region */
extern u_long KERNend;	/* phys addr end of kernel (just after bss) */
#ifdef PMAP_PAE_COMP
pd_entry_t *IdlePTD_pae;	/* phys addr of kernel PTD */
pdpt_entry_t *IdlePDPT;	/* phys addr of kernel PDPT */
pt_entry_t *KPTmap_pae;	/* address of kernel page tables */
#define	IdlePTD	IdlePTD_pae
#define	KPTmap	KPTmap_pae
#else
pd_entry_t *IdlePTD_nopae;
pt_entry_t *KPTmap_nopae;
#define	IdlePTD	IdlePTD_nopae
#define	KPTmap	KPTmap_nopae
#endif
extern u_long KPTphys;	/* phys addr of kernel page tables */
extern u_long tramp_idleptd;

static u_long
allocpages(u_int cnt, u_long *physfree)
{
	u_long res;

	res = *physfree;
	*physfree += PAGE_SIZE * cnt;
	bzero((void *)res, PAGE_SIZE * cnt);
	return (res);
}

static void
pmap_cold_map(u_long pa, u_long va, u_long cnt)
{
	pt_entry_t *pt;

	for (pt = (pt_entry_t *)KPTphys + atop(va); cnt > 0;
	    cnt--, pt++, va += PAGE_SIZE, pa += PAGE_SIZE)
		*pt = pa | PG_V | PG_RW | PG_A | PG_M;
}

static void
pmap_cold_mapident(u_long pa, u_long cnt)
{

	pmap_cold_map(pa, pa, cnt);
}

_Static_assert(LOWPTDI * 2 * NBPDR == KERNBASE,
    "Broken double-map of zero PTD");

static void
__CONCAT(PMTYPE, remap_lower)(bool enable)
{
	int i;

	for (i = 0; i < LOWPTDI; i++)
		IdlePTD[i] = enable ? IdlePTD[LOWPTDI + i] : 0;
	load_cr3(rcr3());		/* invalidate TLB */
}

/*
 * Called from locore.s before paging is enabled.  Sets up the first
 * kernel page table.  Since kernel is mapped with PA == VA, this code
 * does not require relocations.
 */
void
__CONCAT(PMTYPE, cold)(void)
{
	pt_entry_t *pt;
	u_long a;
	u_int cr3, ncr4;

	physfree = (u_long)&_end;
	if (bootinfo.bi_esymtab != 0)
		physfree = bootinfo.bi_esymtab;
	if (bootinfo.bi_kernend != 0)
		physfree = bootinfo.bi_kernend;
	physfree = roundup2(physfree, NBPDR);
	KERNend = physfree;

	/* Allocate Kernel Page Tables */
	KPTphys = allocpages(NKPT, &physfree);
	KPTmap = (pt_entry_t *)KPTphys;

	/* Allocate Page Table Directory */
#ifdef PMAP_PAE_COMP
	/* XXX only need 32 bytes (easier for now) */
	IdlePDPT = (pdpt_entry_t *)allocpages(1, &physfree);
#endif
	IdlePTD = (pd_entry_t *)allocpages(NPGPTD, &physfree);

	/*
	 * Allocate KSTACK.  Leave a guard page between IdlePTD and
	 * proc0kstack, to control stack overflow for thread0 and
	 * prevent corruption of the page table.  We leak the guard
	 * physical memory due to 1:1 mappings.
	 */
	allocpages(1, &physfree);
	proc0kstack = allocpages(TD0_KSTACK_PAGES, &physfree);

	/* vm86/bios stack */
	vm86phystk = allocpages(1, &physfree);

	/* pgtable + ext + IOPAGES */
	vm86paddr = vm86pa = allocpages(3, &physfree);

	/* Install page tables into PTD.  Page table page 1 is wasted. */
	for (a = 0; a < NKPT; a++)
		IdlePTD[a] = (KPTphys + ptoa(a)) | PG_V | PG_RW | PG_A | PG_M;

#ifdef PMAP_PAE_COMP
	/* PAE install PTD pointers into PDPT */
	for (a = 0; a < NPGPTD; a++)
		IdlePDPT[a] = ((u_int)IdlePTD + ptoa(a)) | PG_V;
#endif

	/*
	 * Install recursive mapping for kernel page tables into
	 * itself.
	 */
	for (a = 0; a < NPGPTD; a++)
		IdlePTD[PTDPTDI + a] = ((u_int)IdlePTD + ptoa(a)) | PG_V |
		    PG_RW;

	/*
	 * Initialize page table pages mapping physical address zero
	 * through the (physical) end of the kernel.  Many of these
	 * pages must be reserved, and we reserve them all and map
	 * them linearly for convenience.  We do this even if we've
	 * enabled PSE above; we'll just switch the corresponding
	 * kernel PDEs before we turn on paging.
	 *
	 * This and all other page table entries allow read and write
	 * access for various reasons.  Kernel mappings never have any
	 * access restrictions.
	 */
	pmap_cold_mapident(0, atop(NBPDR) * LOWPTDI);
	pmap_cold_map(0, NBPDR * LOWPTDI, atop(NBPDR) * LOWPTDI);
	pmap_cold_mapident(KERNBASE, atop(KERNend - KERNBASE));

	/* Map page table directory */
#ifdef PMAP_PAE_COMP
	pmap_cold_mapident((u_long)IdlePDPT, 1);
#endif
	pmap_cold_mapident((u_long)IdlePTD, NPGPTD);

	/* Map early KPTmap.  It is really pmap_cold_mapident. */
	pmap_cold_map(KPTphys, (u_long)KPTmap, NKPT);

	/* Map proc0kstack */
	pmap_cold_mapident(proc0kstack, TD0_KSTACK_PAGES);
	/* ISA hole already mapped */

	pmap_cold_mapident(vm86phystk, 1);
	pmap_cold_mapident(vm86pa, 3);

	/* Map page 0 into the vm86 page table */
	*(pt_entry_t *)vm86pa = 0 | PG_RW | PG_U | PG_A | PG_M | PG_V;

	/* ...likewise for the ISA hole for vm86 */
	for (pt = (pt_entry_t *)vm86pa + atop(ISA_HOLE_START), a = 0;
	    a < atop(ISA_HOLE_LENGTH); a++, pt++)
		*pt = (ISA_HOLE_START + ptoa(a)) | PG_RW | PG_U | PG_A |
		    PG_M | PG_V;

	/* Enable PSE, PGE, VME, and PAE if configured. */
	ncr4 = 0;
	if ((cpu_feature & CPUID_PSE) != 0) {
		ncr4 |= CR4_PSE;
		pseflag = PG_PS;
		/*
		 * Superpage mapping of the kernel text.  Existing 4k
		 * page table pages are wasted.
		 */
		for (a = KERNBASE; a < KERNend; a += NBPDR)
			IdlePTD[a >> PDRSHIFT] = a | PG_PS | PG_A | PG_M |
			    PG_RW | PG_V;
	}
	if ((cpu_feature & CPUID_PGE) != 0) {
		ncr4 |= CR4_PGE;
		pgeflag = PG_G;
	}
	ncr4 |= (cpu_feature & CPUID_VME) != 0 ? CR4_VME : 0;
#ifdef PMAP_PAE_COMP
	ncr4 |= CR4_PAE;
#endif
	if (ncr4 != 0)
		load_cr4(rcr4() | ncr4);

	/* Now enable paging */
#ifdef PMAP_PAE_COMP
	cr3 = (u_int)IdlePDPT;
	if ((cpu_feature & CPUID_PAT) == 0)
		wbinvd();
#else
	cr3 = (u_int)IdlePTD;
#endif
	tramp_idleptd = cr3;
	load_cr3(cr3);
	load_cr0(rcr0() | CR0_PG);

	/*
	 * Now running relocated at KERNBASE where the system is
	 * linked to run.
	 */

	/*
	 * Remove the lowest part of the double mapping of low memory
	 * to get some null pointer checks.
	 */
	__CONCAT(PMTYPE, remap_lower)(false);

	kernel_vm_end = /* 0 + */ NKPT * NBPDR;
#ifdef PMAP_PAE_COMP
	i386_pmap_VM_NFREEORDER = VM_NFREEORDER_PAE;
	i386_pmap_VM_LEVEL_0_ORDER = VM_LEVEL_0_ORDER_PAE;
	i386_pmap_PDRSHIFT = PDRSHIFT_PAE;
#else
	i386_pmap_VM_NFREEORDER = VM_NFREEORDER_NOPAE;
	i386_pmap_VM_LEVEL_0_ORDER = VM_LEVEL_0_ORDER_NOPAE;
	i386_pmap_PDRSHIFT = PDRSHIFT_NOPAE;
#endif
}

static void
__CONCAT(PMTYPE, set_nx)(void)
{

#ifdef PMAP_PAE_COMP
	if ((amd_feature & AMDID_NX) == 0)
		return;
	pg_nx = PG_NX;
	elf32_nxstack = 1;
	/* EFER.EFER_NXE is set in initializecpu(). */
#endif
}

/*
 *	Bootstrap the system enough to run with virtual memory.
 *
 *	On the i386 this is called after pmap_cold() created initial
 *	kernel page table and enabled paging, and just syncs the pmap
 *	module with what has already been done.
 */
static void
__CONCAT(PMTYPE, bootstrap)(vm_paddr_t firstaddr)
{
	vm_offset_t va;
	pt_entry_t *pte, *unused;
	struct pcpu *pc;
	u_long res;
	int i;

	res = atop(firstaddr - (vm_paddr_t)KERNLOAD);

	/*
	 * Add a physical memory segment (vm_phys_seg) corresponding to the
	 * preallocated kernel page table pages so that vm_page structures
	 * representing these pages will be created.  The vm_page structures
	 * are required for promotion of the corresponding kernel virtual
	 * addresses to superpage mappings.
	 */
	vm_phys_add_seg(KPTphys, KPTphys + ptoa(nkpt));

	/*
	 * Initialize the first available kernel virtual address.
	 * However, using "firstaddr" may waste a few pages of the
	 * kernel virtual address space, because pmap_cold() may not
	 * have mapped every physical page that it allocated.
	 * Preferably, pmap_cold() would provide a first unused
	 * virtual address in addition to "firstaddr".
	 */
	virtual_avail = (vm_offset_t)firstaddr;
	virtual_end = VM_MAX_KERNEL_ADDRESS;

	/*
	 * Initialize the kernel pmap (which is statically allocated).
	 * Count bootstrap data as being resident in case any of this data is
	 * later unmapped (using pmap_remove()) and freed.
	 */
	PMAP_LOCK_INIT(kernel_pmap);
	kernel_pmap->pm_pdir = IdlePTD;
#ifdef PMAP_PAE_COMP
	kernel_pmap->pm_pdpt = IdlePDPT;
#endif
	CPU_FILL(&kernel_pmap->pm_active);	/* don't allow deactivation */
	kernel_pmap->pm_stats.resident_count = res;
	TAILQ_INIT(&kernel_pmap->pm_pvchunk);

 	/*
	 * Initialize the global pv list lock.
	 */
	rw_init(&pvh_global_lock, "pmap pv global");

	/*
	 * Reserve some special page table entries/VA space for temporary
	 * mapping of pages.
	 */
#define	SYSMAP(c, p, v, n)	\
	v = (c)va; va += ((n)*PAGE_SIZE); p = pte; pte += (n);

	va = virtual_avail;
	pte = vtopte(va);


	/*
	 * Initialize temporary map objects on the current CPU for use
	 * during early boot.
	 * CMAP1/CMAP2 are used for zeroing and copying pages.
	 * CMAP3 is used for the boot-time memory test.
	 */
	pc = get_pcpu();
	mtx_init(&pc->pc_cmap_lock, "SYSMAPS", NULL, MTX_DEF);
	SYSMAP(caddr_t, pc->pc_cmap_pte1, pc->pc_cmap_addr1, 1)
	SYSMAP(caddr_t, pc->pc_cmap_pte2, pc->pc_cmap_addr2, 1)
	SYSMAP(vm_offset_t, pte, pc->pc_qmap_addr, 1)

	SYSMAP(caddr_t, CMAP3, CADDR3, 1);

	/*
	 * Crashdump maps.
	 */
	SYSMAP(caddr_t, unused, crashdumpmap, MAXDUMPPGS)

	/*
	 * ptvmmap is used for reading arbitrary physical pages via /dev/mem.
	 */
	SYSMAP(caddr_t, unused, ptvmmap, 1)

	/*
	 * msgbufp is used to map the system message buffer.
	 */
	SYSMAP(struct msgbuf *, unused, msgbufp, atop(round_page(msgbufsize)))

	/*
	 * KPTmap is used by pmap_kextract().
	 *
	 * KPTmap is first initialized by pmap_cold().  However, that initial
	 * KPTmap can only support NKPT page table pages.  Here, a larger
	 * KPTmap is created that can support KVA_PAGES page table pages.
	 */
	SYSMAP(pt_entry_t *, KPTD, KPTmap, KVA_PAGES)

	for (i = 0; i < NKPT; i++)
		KPTD[i] = (KPTphys + ptoa(i)) | PG_RW | PG_V;

	/*
	 * PADDR1 and PADDR2 are used by pmap_pte_quick() and pmap_pte(),
	 * respectively.
	 */
	SYSMAP(pt_entry_t *, PMAP1, PADDR1, 1)
	SYSMAP(pt_entry_t *, PMAP2, PADDR2, 1)
	SYSMAP(pt_entry_t *, PMAP3, PADDR3, 1)

	mtx_init(&PMAP2mutex, "PMAP2", NULL, MTX_DEF);

	virtual_avail = va;

	/*
	 * Initialize the PAT MSR if present.
	 * pmap_init_pat() clears and sets CR4_PGE, which, as a
	 * side-effect, invalidates stale PG_G TLB entries that might
	 * have been created in our pre-boot environment.  We assume
	 * that PAT support implies PGE and in reverse, PGE presence
	 * comes with PAT.  Both features were added for Pentium Pro.
	 */
	pmap_init_pat();
}

static void
pmap_init_reserved_pages(void)
{
	struct pcpu *pc;
	vm_offset_t pages;
	int i;

#ifdef PMAP_PAE_COMP
	if (!pae_mode)
		return;
#else
	if (pae_mode)
		return;
#endif
	CPU_FOREACH(i) {
		pc = pcpu_find(i);
		mtx_init(&pc->pc_copyout_mlock, "cpmlk", NULL, MTX_DEF |
		    MTX_NEW);
		pc->pc_copyout_maddr = kva_alloc(ptoa(2));
		if (pc->pc_copyout_maddr == 0)
			panic("unable to allocate non-sleepable copyout KVA");
		sx_init(&pc->pc_copyout_slock, "cpslk");
		pc->pc_copyout_saddr = kva_alloc(ptoa(2));
		if (pc->pc_copyout_saddr == 0)
			panic("unable to allocate sleepable copyout KVA");
		pc->pc_pmap_eh_va = kva_alloc(ptoa(1));
		if (pc->pc_pmap_eh_va == 0)
			panic("unable to allocate pmap_extract_and_hold KVA");
		pc->pc_pmap_eh_ptep = (char *)vtopte(pc->pc_pmap_eh_va);

		/*
		 * Skip if the mappings have already been initialized,
		 * i.e. this is the BSP.
		 */
		if (pc->pc_cmap_addr1 != 0)
			continue;

		mtx_init(&pc->pc_cmap_lock, "SYSMAPS", NULL, MTX_DEF);
		pages = kva_alloc(PAGE_SIZE * 3);
		if (pages == 0)
			panic("unable to allocate CMAP KVA");
		pc->pc_cmap_pte1 = vtopte(pages);
		pc->pc_cmap_pte2 = vtopte(pages + PAGE_SIZE);
		pc->pc_cmap_addr1 = (caddr_t)pages;
		pc->pc_cmap_addr2 = (caddr_t)(pages + PAGE_SIZE);
		pc->pc_qmap_addr = pages + ptoa(2);
	}
}
 
SYSINIT(rpages_init, SI_SUB_CPU, SI_ORDER_ANY, pmap_init_reserved_pages, NULL);

/*
 * Setup the PAT MSR.
 */
static void
__CONCAT(PMTYPE, init_pat)(void)
{
	int pat_table[PAT_INDEX_SIZE];
	uint64_t pat_msr;
	u_long cr0, cr4;
	int i;

	/* Set default PAT index table. */
	for (i = 0; i < PAT_INDEX_SIZE; i++)
		pat_table[i] = -1;
	pat_table[PAT_WRITE_BACK] = 0;
	pat_table[PAT_WRITE_THROUGH] = 1;
	pat_table[PAT_UNCACHEABLE] = 3;
	pat_table[PAT_WRITE_COMBINING] = 3;
	pat_table[PAT_WRITE_PROTECTED] = 3;
	pat_table[PAT_UNCACHED] = 3;

	/*
	 * Bail if this CPU doesn't implement PAT.
	 * We assume that PAT support implies PGE.
	 */
	if ((cpu_feature & CPUID_PAT) == 0) {
		for (i = 0; i < PAT_INDEX_SIZE; i++)
			pat_index[i] = pat_table[i];
		pat_works = 0;
		return;
	}

	/*
	 * Due to some Intel errata, we can only safely use the lower 4
	 * PAT entries.
	 *
	 *   Intel Pentium III Processor Specification Update
	 * Errata E.27 (Upper Four PAT Entries Not Usable With Mode B
	 * or Mode C Paging)
	 *
	 *   Intel Pentium IV  Processor Specification Update
	 * Errata N46 (PAT Index MSB May Be Calculated Incorrectly)
	 */
	if (cpu_vendor_id == CPU_VENDOR_INTEL &&
	    !(CPUID_TO_FAMILY(cpu_id) == 6 && CPUID_TO_MODEL(cpu_id) >= 0xe))
		pat_works = 0;

	/* Initialize default PAT entries. */
	pat_msr = PAT_VALUE(0, PAT_WRITE_BACK) |
	    PAT_VALUE(1, PAT_WRITE_THROUGH) |
	    PAT_VALUE(2, PAT_UNCACHED) |
	    PAT_VALUE(3, PAT_UNCACHEABLE) |
	    PAT_VALUE(4, PAT_WRITE_BACK) |
	    PAT_VALUE(5, PAT_WRITE_THROUGH) |
	    PAT_VALUE(6, PAT_UNCACHED) |
	    PAT_VALUE(7, PAT_UNCACHEABLE);

	if (pat_works) {
		/*
		 * Leave the indices 0-3 at the default of WB, WT, UC-, and UC.
		 * Program 5 and 6 as WP and WC.
		 * Leave 4 and 7 as WB and UC.
		 */
		pat_msr &= ~(PAT_MASK(5) | PAT_MASK(6));
		pat_msr |= PAT_VALUE(5, PAT_WRITE_PROTECTED) |
		    PAT_VALUE(6, PAT_WRITE_COMBINING);
		pat_table[PAT_UNCACHED] = 2;
		pat_table[PAT_WRITE_PROTECTED] = 5;
		pat_table[PAT_WRITE_COMBINING] = 6;
	} else {
		/*
		 * Just replace PAT Index 2 with WC instead of UC-.
		 */
		pat_msr &= ~PAT_MASK(2);
		pat_msr |= PAT_VALUE(2, PAT_WRITE_COMBINING);
		pat_table[PAT_WRITE_COMBINING] = 2;
	}

	/* Disable PGE. */
	cr4 = rcr4();
	load_cr4(cr4 & ~CR4_PGE);

	/* Disable caches (CD = 1, NW = 0). */
	cr0 = rcr0();
	load_cr0((cr0 & ~CR0_NW) | CR0_CD);

	/* Flushes caches and TLBs. */
	wbinvd();
	invltlb();

	/* Update PAT and index table. */
	wrmsr(MSR_PAT, pat_msr);
	for (i = 0; i < PAT_INDEX_SIZE; i++)
		pat_index[i] = pat_table[i];

	/* Flush caches and TLBs again. */
	wbinvd();
	invltlb();

	/* Restore caches and PGE. */
	load_cr0(cr0);
	load_cr4(cr4);
}

#ifdef PMAP_PAE_COMP
static void *
pmap_pdpt_allocf(uma_zone_t zone, vm_size_t bytes, int domain, uint8_t *flags,
    int wait)
{

	/* Inform UMA that this allocator uses kernel_map/object. */
	*flags = UMA_SLAB_KERNEL;
	return ((void *)kmem_alloc_contig_domainset(DOMAINSET_FIXED(domain),
	    bytes, wait, 0x0ULL, 0xffffffffULL, 1, 0, VM_MEMATTR_DEFAULT));
}
#endif

/*
 * Abuse the pte nodes for unmapped kva to thread a kva freelist through.
 * Requirements:
 *  - Must deal with pages in order to ensure that none of the PG_* bits
 *    are ever set, PG_V in particular.
 *  - Assumes we can write to ptes without pte_store() atomic ops, even
 *    on PAE systems.  This should be ok.
 *  - Assumes nothing will ever test these addresses for 0 to indicate
 *    no mapping instead of correctly checking PG_V.
 *  - Assumes a vm_offset_t will fit in a pte (true for i386).
 * Because PG_V is never set, there can be no mappings to invalidate.
 */
static vm_offset_t
pmap_ptelist_alloc(vm_offset_t *head)
{
	pt_entry_t *pte;
	vm_offset_t va;

	va = *head;
	if (va == 0)
		panic("pmap_ptelist_alloc: exhausted ptelist KVA");
	pte = vtopte(va);
	*head = *pte;
	if (*head & PG_V)
		panic("pmap_ptelist_alloc: va with PG_V set!");
	*pte = 0;
	return (va);
}

static void
pmap_ptelist_free(vm_offset_t *head, vm_offset_t va)
{
	pt_entry_t *pte;

	if (va & PG_V)
		panic("pmap_ptelist_free: freeing va with PG_V set!");
	pte = vtopte(va);
	*pte = *head;		/* virtual! PG_V is 0 though */
	*head = va;
}

static void
pmap_ptelist_init(vm_offset_t *head, void *base, int npages)
{
	int i;
	vm_offset_t va;

	*head = 0;
	for (i = npages - 1; i >= 0; i--) {
		va = (vm_offset_t)base + i * PAGE_SIZE;
		pmap_ptelist_free(head, va);
	}
}


/*
 *	Initialize the pmap module.
 *	Called by vm_init, to initialize any structures that the pmap
 *	system needs to map virtual memory.
 */
static void
__CONCAT(PMTYPE, init)(void)
{
	struct pmap_preinit_mapping *ppim;
	vm_page_t mpte;
	vm_size_t s;
	int i, pv_npg;

	/*
	 * Initialize the vm page array entries for the kernel pmap's
	 * page table pages.
	 */ 
	PMAP_LOCK(kernel_pmap);
	for (i = 0; i < NKPT; i++) {
		mpte = PHYS_TO_VM_PAGE(KPTphys + ptoa(i));
		KASSERT(mpte >= vm_page_array &&
		    mpte < &vm_page_array[vm_page_array_size],
		    ("pmap_init: page table page is out of range"));
		mpte->pindex = i + KPTDI;
		mpte->phys_addr = KPTphys + ptoa(i);
		mpte->wire_count = 1;
		if (pseflag != 0 &&
		    KERNBASE <= i << PDRSHIFT && i << PDRSHIFT < KERNend &&
		    pmap_insert_pt_page(kernel_pmap, mpte))
			panic("pmap_init: pmap_insert_pt_page failed");
	}
	PMAP_UNLOCK(kernel_pmap);
	vm_wire_add(NKPT);

	/*
	 * Initialize the address space (zone) for the pv entries.  Set a
	 * high water mark so that the system can recover from excessive
	 * numbers of pv entries.
	 */
	TUNABLE_INT_FETCH("vm.pmap.shpgperproc", &shpgperproc);
	pv_entry_max = shpgperproc * maxproc + vm_cnt.v_page_count;
	TUNABLE_INT_FETCH("vm.pmap.pv_entries", &pv_entry_max);
	pv_entry_max = roundup(pv_entry_max, _NPCPV);
	pv_entry_high_water = 9 * (pv_entry_max / 10);

	/*
	 * If the kernel is running on a virtual machine, then it must assume
	 * that MCA is enabled by the hypervisor.  Moreover, the kernel must
	 * be prepared for the hypervisor changing the vendor and family that
	 * are reported by CPUID.  Consequently, the workaround for AMD Family
	 * 10h Erratum 383 is enabled if the processor's feature set does not
	 * include at least one feature that is only supported by older Intel
	 * or newer AMD processors.
	 */
	if (vm_guest != VM_GUEST_NO && (cpu_feature & CPUID_SS) == 0 &&
	    (cpu_feature2 & (CPUID2_SSSE3 | CPUID2_SSE41 | CPUID2_AESNI |
	    CPUID2_AVX | CPUID2_XSAVE)) == 0 && (amd_feature2 & (AMDID2_XOP |
	    AMDID2_FMA4)) == 0)
		workaround_erratum383 = 1;

	/*
	 * Are large page mappings supported and enabled?
	 */
	TUNABLE_INT_FETCH("vm.pmap.pg_ps_enabled", &pg_ps_enabled);
	if (pseflag == 0)
		pg_ps_enabled = 0;
	else if (pg_ps_enabled) {
		KASSERT(MAXPAGESIZES > 1 && pagesizes[1] == 0,
		    ("pmap_init: can't assign to pagesizes[1]"));
		pagesizes[1] = NBPDR;
	}

	/*
	 * Calculate the size of the pv head table for superpages.
	 * Handle the possibility that "vm_phys_segs[...].end" is zero.
	 */
	pv_npg = trunc_4mpage(vm_phys_segs[vm_phys_nsegs - 1].end -
	    PAGE_SIZE) / NBPDR + 1;

	/*
	 * Allocate memory for the pv head table for superpages.
	 */
	s = (vm_size_t)(pv_npg * sizeof(struct md_page));
	s = round_page(s);
	pv_table = (struct md_page *)kmem_malloc(s, M_WAITOK | M_ZERO);
	for (i = 0; i < pv_npg; i++)
		TAILQ_INIT(&pv_table[i].pv_list);

	pv_maxchunks = MAX(pv_entry_max / _NPCPV, maxproc);
	pv_chunkbase = (struct pv_chunk *)kva_alloc(PAGE_SIZE * pv_maxchunks);
	if (pv_chunkbase == NULL)
		panic("pmap_init: not enough kvm for pv chunks");
	pmap_ptelist_init(&pv_vafree, pv_chunkbase, pv_maxchunks);
#ifdef PMAP_PAE_COMP
	pdptzone = uma_zcreate("PDPT", NPGPTD * sizeof(pdpt_entry_t), NULL,
	    NULL, NULL, NULL, (NPGPTD * sizeof(pdpt_entry_t)) - 1,
	    UMA_ZONE_VM | UMA_ZONE_NOFREE);
	uma_zone_set_allocf(pdptzone, pmap_pdpt_allocf);
#endif

	pmap_initialized = 1;
	pmap_init_trm();

	if (!bootverbose)
		return;
	for (i = 0; i < PMAP_PREINIT_MAPPING_COUNT; i++) {
		ppim = pmap_preinit_mapping + i;
		if (ppim->va == 0)
			continue;
		printf("PPIM %u: PA=%#jx, VA=%#x, size=%#x, mode=%#x\n", i,
		    (uintmax_t)ppim->pa, ppim->va, ppim->sz, ppim->mode);
	}

}

extern u_long pmap_pde_demotions;
extern u_long pmap_pde_mappings;
extern u_long pmap_pde_p_failures;
extern u_long pmap_pde_promotions;

/***************************************************
 * Low level helper routines.....
 ***************************************************/

static boolean_t
__CONCAT(PMTYPE, is_valid_memattr)(pmap_t pmap __unused, vm_memattr_t mode)
{

	return (mode >= 0 && mode < PAT_INDEX_SIZE &&
	    pat_index[(int)mode] >= 0);
}

/*
 * Determine the appropriate bits to set in a PTE or PDE for a specified
 * caching mode.
 */
static int
__CONCAT(PMTYPE, cache_bits)(pmap_t pmap, int mode, boolean_t is_pde)
{
	int cache_bits, pat_flag, pat_idx;

	if (!pmap_is_valid_memattr(pmap, mode))
		panic("Unknown caching mode %d\n", mode);

	/* The PAT bit is different for PTE's and PDE's. */
	pat_flag = is_pde ? PG_PDE_PAT : PG_PTE_PAT;

	/* Map the caching mode to a PAT index. */
	pat_idx = pat_index[mode];

	/* Map the 3-bit index value into the PAT, PCD, and PWT bits. */
	cache_bits = 0;
	if (pat_idx & 0x4)
		cache_bits |= pat_flag;
	if (pat_idx & 0x2)
		cache_bits |= PG_NC_PCD;
	if (pat_idx & 0x1)
		cache_bits |= PG_NC_PWT;
	return (cache_bits);
}

static bool
__CONCAT(PMTYPE, ps_enabled)(pmap_t pmap __unused)
{

	return (pg_ps_enabled);
}

/*
 * The caller is responsible for maintaining TLB consistency.
 */
static void
pmap_kenter_pde(vm_offset_t va, pd_entry_t newpde)
{
	pd_entry_t *pde;

	pde = pmap_pde(kernel_pmap, va);
	pde_store(pde, newpde);
}

/*
 * After changing the page size for the specified virtual address in the page
 * table, flush the corresponding entries from the processor's TLB.  Only the
 * calling processor's TLB is affected.
 *
 * The calling thread must be pinned to a processor.
 */
static void
pmap_update_pde_invalidate(vm_offset_t va, pd_entry_t newpde)
{

	if ((newpde & PG_PS) == 0)
		/* Demotion: flush a specific 2MB page mapping. */
		invlpg(va);
	else /* if ((newpde & PG_G) == 0) */
		/*
		 * Promotion: flush every 4KB page mapping from the TLB
		 * because there are too many to flush individually.
		 */
		invltlb();
}

#ifdef SMP
/*
 * For SMP, these functions have to use the IPI mechanism for coherence.
 *
 * N.B.: Before calling any of the following TLB invalidation functions,
 * the calling processor must ensure that all stores updating a non-
 * kernel page table are globally performed.  Otherwise, another
 * processor could cache an old, pre-update entry without being
 * invalidated.  This can happen one of two ways: (1) The pmap becomes
 * active on another processor after its pm_active field is checked by
 * one of the following functions but before a store updating the page
 * table is globally performed. (2) The pmap becomes active on another
 * processor before its pm_active field is checked but due to
 * speculative loads one of the following functions stills reads the
 * pmap as inactive on the other processor.
 * 
 * The kernel page table is exempt because its pm_active field is
 * immutable.  The kernel page table is always active on every
 * processor.
 */
static void
pmap_invalidate_page_int(pmap_t pmap, vm_offset_t va)
{
	cpuset_t *mask, other_cpus;
	u_int cpuid;

	sched_pin();
	if (pmap == kernel_pmap) {
		invlpg(va);
		mask = &all_cpus;
	} else if (!CPU_CMP(&pmap->pm_active, &all_cpus)) {
		mask = &all_cpus;
	} else {
		cpuid = PCPU_GET(cpuid);
		other_cpus = all_cpus;
		CPU_CLR(cpuid, &other_cpus);
		CPU_AND(&other_cpus, &pmap->pm_active);
		mask = &other_cpus;
	}
	smp_masked_invlpg(*mask, va, pmap);
	sched_unpin();
}

/* 4k PTEs -- Chosen to exceed the total size of Broadwell L2 TLB */
#define	PMAP_INVLPG_THRESHOLD	(4 * 1024 * PAGE_SIZE)

static void
pmap_invalidate_range_int(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	cpuset_t *mask, other_cpus;
	vm_offset_t addr;
	u_int cpuid;

	if (eva - sva >= PMAP_INVLPG_THRESHOLD) {
		pmap_invalidate_all_int(pmap);
		return;
	}

	sched_pin();
	if (pmap == kernel_pmap) {
		for (addr = sva; addr < eva; addr += PAGE_SIZE)
			invlpg(addr);
		mask = &all_cpus;
	} else  if (!CPU_CMP(&pmap->pm_active, &all_cpus)) {
		mask = &all_cpus;
	} else {
		cpuid = PCPU_GET(cpuid);
		other_cpus = all_cpus;
		CPU_CLR(cpuid, &other_cpus);
		CPU_AND(&other_cpus, &pmap->pm_active);
		mask = &other_cpus;
	}
	smp_masked_invlpg_range(*mask, sva, eva, pmap);
	sched_unpin();
}

static void
pmap_invalidate_all_int(pmap_t pmap)
{
	cpuset_t *mask, other_cpus;
	u_int cpuid;

	sched_pin();
	if (pmap == kernel_pmap) {
		invltlb();
		mask = &all_cpus;
	} else if (!CPU_CMP(&pmap->pm_active, &all_cpus)) {
		mask = &all_cpus;
	} else {
		cpuid = PCPU_GET(cpuid);
		other_cpus = all_cpus;
		CPU_CLR(cpuid, &other_cpus);
		CPU_AND(&other_cpus, &pmap->pm_active);
		mask = &other_cpus;
	}
	smp_masked_invltlb(*mask, pmap);
	sched_unpin();
}

static void
__CONCAT(PMTYPE, invalidate_cache)(void)
{

	sched_pin();
	wbinvd();
	smp_cache_flush();
	sched_unpin();
}

struct pde_action {
	cpuset_t invalidate;	/* processors that invalidate their TLB */
	vm_offset_t va;
	pd_entry_t *pde;
	pd_entry_t newpde;
	u_int store;		/* processor that updates the PDE */
};

static void
pmap_update_pde_kernel(void *arg)
{
	struct pde_action *act = arg;
	pd_entry_t *pde;

	if (act->store == PCPU_GET(cpuid)) {
		pde = pmap_pde(kernel_pmap, act->va);
		pde_store(pde, act->newpde);
	}
}

static void
pmap_update_pde_user(void *arg)
{
	struct pde_action *act = arg;

	if (act->store == PCPU_GET(cpuid))
		pde_store(act->pde, act->newpde);
}

static void
pmap_update_pde_teardown(void *arg)
{
	struct pde_action *act = arg;

	if (CPU_ISSET(PCPU_GET(cpuid), &act->invalidate))
		pmap_update_pde_invalidate(act->va, act->newpde);
}

/*
 * Change the page size for the specified virtual address in a way that
 * prevents any possibility of the TLB ever having two entries that map the
 * same virtual address using different page sizes.  This is the recommended
 * workaround for Erratum 383 on AMD Family 10h processors.  It prevents a
 * machine check exception for a TLB state that is improperly diagnosed as a
 * hardware error.
 */
static void
pmap_update_pde(pmap_t pmap, vm_offset_t va, pd_entry_t *pde, pd_entry_t newpde)
{
	struct pde_action act;
	cpuset_t active, other_cpus;
	u_int cpuid;

	sched_pin();
	cpuid = PCPU_GET(cpuid);
	other_cpus = all_cpus;
	CPU_CLR(cpuid, &other_cpus);
	if (pmap == kernel_pmap)
		active = all_cpus;
	else
		active = pmap->pm_active;
	if (CPU_OVERLAP(&active, &other_cpus)) {
		act.store = cpuid;
		act.invalidate = active;
		act.va = va;
		act.pde = pde;
		act.newpde = newpde;
		CPU_SET(cpuid, &active);
		smp_rendezvous_cpus(active,
		    smp_no_rendezvous_barrier, pmap == kernel_pmap ?
		    pmap_update_pde_kernel : pmap_update_pde_user,
		    pmap_update_pde_teardown, &act);
	} else {
		if (pmap == kernel_pmap)
			pmap_kenter_pde(va, newpde);
		else
			pde_store(pde, newpde);
		if (CPU_ISSET(cpuid, &active))
			pmap_update_pde_invalidate(va, newpde);
	}
	sched_unpin();
}
#else /* !SMP */
/*
 * Normal, non-SMP, 486+ invalidation functions.
 * We inline these within pmap.c for speed.
 */
static void
pmap_invalidate_page_int(pmap_t pmap, vm_offset_t va)
{

	if (pmap == kernel_pmap)
		invlpg(va);
}

static void
pmap_invalidate_range_int(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	vm_offset_t addr;

	if (pmap == kernel_pmap)
		for (addr = sva; addr < eva; addr += PAGE_SIZE)
			invlpg(addr);
}

static void
pmap_invalidate_all_int(pmap_t pmap)
{

	if (pmap == kernel_pmap)
		invltlb();
}

static void
__CONCAT(PMTYPE, invalidate_cache)(void)
{

	wbinvd();
}

static void
pmap_update_pde(pmap_t pmap, vm_offset_t va, pd_entry_t *pde, pd_entry_t newpde)
{

	if (pmap == kernel_pmap)
		pmap_kenter_pde(va, newpde);
	else
		pde_store(pde, newpde);
	if (pmap == kernel_pmap || !CPU_EMPTY(&pmap->pm_active))
		pmap_update_pde_invalidate(va, newpde);
}
#endif /* !SMP */

static void
__CONCAT(PMTYPE, invalidate_page)(pmap_t pmap, vm_offset_t va)
{

	pmap_invalidate_page_int(pmap, va);
}

static void
__CONCAT(PMTYPE, invalidate_range)(pmap_t pmap, vm_offset_t sva,
    vm_offset_t eva)
{

	pmap_invalidate_range_int(pmap, sva, eva);
}

static void
__CONCAT(PMTYPE, invalidate_all)(pmap_t pmap)
{

	pmap_invalidate_all_int(pmap);
}

static void
pmap_invalidate_pde_page(pmap_t pmap, vm_offset_t va, pd_entry_t pde)
{

	/*
	 * When the PDE has PG_PROMOTED set, the 2- or 4MB page mapping was
	 * created by a promotion that did not invalidate the 512 or 1024 4KB
	 * page mappings that might exist in the TLB.  Consequently, at this
	 * point, the TLB may hold both 4KB and 2- or 4MB page mappings for
	 * the address range [va, va + NBPDR).  Therefore, the entire range
	 * must be invalidated here.  In contrast, when PG_PROMOTED is clear,
	 * the TLB will not hold any 4KB page mappings for the address range
	 * [va, va + NBPDR), and so a single INVLPG suffices to invalidate the
	 * 2- or 4MB page mapping from the TLB.
	 */
	if ((pde & PG_PROMOTED) != 0)
		pmap_invalidate_range_int(pmap, va, va + NBPDR - 1);
	else
		pmap_invalidate_page_int(pmap, va);
}

/*
 * Are we current address space or kernel?
 */
static __inline int
pmap_is_current(pmap_t pmap)
{

	return (pmap == kernel_pmap);
}

/*
 * If the given pmap is not the current or kernel pmap, the returned pte must
 * be released by passing it to pmap_pte_release().
 */
static pt_entry_t *
__CONCAT(PMTYPE, pte)(pmap_t pmap, vm_offset_t va)
{
	pd_entry_t newpf;
	pd_entry_t *pde;

	pde = pmap_pde(pmap, va);
	if (*pde & PG_PS)
		return (pde);
	if (*pde != 0) {
		/* are we current address space or kernel? */
		if (pmap_is_current(pmap))
			return (vtopte(va));
		mtx_lock(&PMAP2mutex);
		newpf = *pde & PG_FRAME;
		if ((*PMAP2 & PG_FRAME) != newpf) {
			*PMAP2 = newpf | PG_RW | PG_V | PG_A | PG_M;
			pmap_invalidate_page_int(kernel_pmap,
			    (vm_offset_t)PADDR2);
		}
		return (PADDR2 + (i386_btop(va) & (NPTEPG - 1)));
	}
	return (NULL);
}

/*
 * Releases a pte that was obtained from pmap_pte().  Be prepared for the pte
 * being NULL.
 */
static __inline void
pmap_pte_release(pt_entry_t *pte)
{

	if ((pt_entry_t *)((vm_offset_t)pte & ~PAGE_MASK) == PADDR2)
		mtx_unlock(&PMAP2mutex);
}

/*
 * NB:  The sequence of updating a page table followed by accesses to the
 * corresponding pages is subject to the situation described in the "AMD64
 * Architecture Programmer's Manual Volume 2: System Programming" rev. 3.23,
 * "7.3.1 Special Coherency Considerations".  Therefore, issuing the INVLPG
 * right after modifying the PTE bits is crucial.
 */
static __inline void
invlcaddr(void *caddr)
{

	invlpg((u_int)caddr);
}

/*
 * Super fast pmap_pte routine best used when scanning
 * the pv lists.  This eliminates many coarse-grained
 * invltlb calls.  Note that many of the pv list
 * scans are across different pmaps.  It is very wasteful
 * to do an entire invltlb for checking a single mapping.
 *
 * If the given pmap is not the current pmap, pvh_global_lock
 * must be held and curthread pinned to a CPU.
 */
static pt_entry_t *
pmap_pte_quick(pmap_t pmap, vm_offset_t va)
{
	pd_entry_t newpf;
	pd_entry_t *pde;

	pde = pmap_pde(pmap, va);
	if (*pde & PG_PS)
		return (pde);
	if (*pde != 0) {
		/* are we current address space or kernel? */
		if (pmap_is_current(pmap))
			return (vtopte(va));
		rw_assert(&pvh_global_lock, RA_WLOCKED);
		KASSERT(curthread->td_pinned > 0, ("curthread not pinned"));
		newpf = *pde & PG_FRAME;
		if ((*PMAP1 & PG_FRAME) != newpf) {
			*PMAP1 = newpf | PG_RW | PG_V | PG_A | PG_M;
#ifdef SMP
			PMAP1cpu = PCPU_GET(cpuid);
#endif
			invlcaddr(PADDR1);
			PMAP1changed++;
		} else
#ifdef SMP
		if (PMAP1cpu != PCPU_GET(cpuid)) {
			PMAP1cpu = PCPU_GET(cpuid);
			invlcaddr(PADDR1);
			PMAP1changedcpu++;
		} else
#endif
			PMAP1unchanged++;
		return (PADDR1 + (i386_btop(va) & (NPTEPG - 1)));
	}
	return (0);
}

static pt_entry_t *
pmap_pte_quick3(pmap_t pmap, vm_offset_t va)
{
	pd_entry_t newpf;
	pd_entry_t *pde;

	pde = pmap_pde(pmap, va);
	if (*pde & PG_PS)
		return (pde);
	if (*pde != 0) {
		rw_assert(&pvh_global_lock, RA_WLOCKED);
		KASSERT(curthread->td_pinned > 0, ("curthread not pinned"));
		newpf = *pde & PG_FRAME;
		if ((*PMAP3 & PG_FRAME) != newpf) {
			*PMAP3 = newpf | PG_RW | PG_V | PG_A | PG_M;
#ifdef SMP
			PMAP3cpu = PCPU_GET(cpuid);
#endif
			invlcaddr(PADDR3);
			PMAP1changed++;
		} else
#ifdef SMP
		if (PMAP3cpu != PCPU_GET(cpuid)) {
			PMAP3cpu = PCPU_GET(cpuid);
			invlcaddr(PADDR3);
			PMAP1changedcpu++;
		} else
#endif
			PMAP1unchanged++;
		return (PADDR3 + (i386_btop(va) & (NPTEPG - 1)));
	}
	return (0);
}

static pt_entry_t
pmap_pte_ufast(pmap_t pmap, vm_offset_t va, pd_entry_t pde)
{
	pt_entry_t *eh_ptep, pte, *ptep;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	pde &= PG_FRAME;
	critical_enter();
	eh_ptep = (pt_entry_t *)PCPU_GET(pmap_eh_ptep);
	if ((*eh_ptep & PG_FRAME) != pde) {
		*eh_ptep = pde | PG_RW | PG_V | PG_A | PG_M;
		invlcaddr((void *)PCPU_GET(pmap_eh_va));
	}
	ptep = (pt_entry_t *)PCPU_GET(pmap_eh_va) + (i386_btop(va) &
	    (NPTEPG - 1));
	pte = *ptep;
	critical_exit();
	return (pte);
}

/*
 * Extract from the kernel page table the physical address that is mapped by
 * the given virtual address "va".
 *
 * This function may be used before pmap_bootstrap() is called.
 */
static vm_paddr_t
__CONCAT(PMTYPE, kextract)(vm_offset_t va)
{
	vm_paddr_t pa;

	if ((pa = pte_load(&PTD[va >> PDRSHIFT])) & PG_PS) {
		pa = (pa & PG_PS_FRAME) | (va & PDRMASK);
	} else {
		/*
		 * Beware of a concurrent promotion that changes the PDE at
		 * this point!  For example, vtopte() must not be used to
		 * access the PTE because it would use the new PDE.  It is,
		 * however, safe to use the old PDE because the page table
		 * page is preserved by the promotion.
		 */
		pa = KPTmap[i386_btop(va)];
		pa = (pa & PG_FRAME) | (va & PAGE_MASK);
	}
	return (pa);
}

/*
 *	Routine:	pmap_extract
 *	Function:
 *		Extract the physical page address associated
 *		with the given map/virtual_address pair.
 */
static vm_paddr_t
__CONCAT(PMTYPE, extract)(pmap_t pmap, vm_offset_t va)
{
	vm_paddr_t rtval;
	pt_entry_t pte;
	pd_entry_t pde;

	rtval = 0;
	PMAP_LOCK(pmap);
	pde = pmap->pm_pdir[va >> PDRSHIFT];
	if (pde != 0) {
		if ((pde & PG_PS) != 0)
			rtval = (pde & PG_PS_FRAME) | (va & PDRMASK);
		else {
			pte = pmap_pte_ufast(pmap, va, pde);
			rtval = (pte & PG_FRAME) | (va & PAGE_MASK);
		}
	}
	PMAP_UNLOCK(pmap);
	return (rtval);
}

/*
 *	Routine:	pmap_extract_and_hold
 *	Function:
 *		Atomically extract and hold the physical page
 *		with the given pmap and virtual address pair
 *		if that mapping permits the given protection.
 */
static vm_page_t
__CONCAT(PMTYPE, extract_and_hold)(pmap_t pmap, vm_offset_t va, vm_prot_t prot)
{
	pd_entry_t pde;
	pt_entry_t pte;
	vm_page_t m;
	vm_paddr_t pa;

	pa = 0;
	m = NULL;
	PMAP_LOCK(pmap);
retry:
	pde = *pmap_pde(pmap, va);
	if (pde != 0) {
		if (pde & PG_PS) {
			if ((pde & PG_RW) || (prot & VM_PROT_WRITE) == 0) {
				if (vm_page_pa_tryrelock(pmap, (pde &
				    PG_PS_FRAME) | (va & PDRMASK), &pa))
					goto retry;
				m = PHYS_TO_VM_PAGE(pa);
			}
		} else {
			pte = pmap_pte_ufast(pmap, va, pde);
			if (pte != 0 &&
			    ((pte & PG_RW) || (prot & VM_PROT_WRITE) == 0)) {
				if (vm_page_pa_tryrelock(pmap, pte & PG_FRAME,
				    &pa))
					goto retry;
				m = PHYS_TO_VM_PAGE(pa);
			}
		}
		if (m != NULL)
			vm_page_hold(m);
	}
	PA_UNLOCK_COND(pa);
	PMAP_UNLOCK(pmap);
	return (m);
}

/***************************************************
 * Low level mapping routines.....
 ***************************************************/

/*
 * Add a wired page to the kva.
 * Note: not SMP coherent.
 *
 * This function may be used before pmap_bootstrap() is called.
 */
static void
__CONCAT(PMTYPE, kenter)(vm_offset_t va, vm_paddr_t pa)
{
	pt_entry_t *pte;

	pte = vtopte(va);
	pte_store(pte, pa | PG_RW | PG_V);
}

static __inline void
pmap_kenter_attr(vm_offset_t va, vm_paddr_t pa, int mode)
{
	pt_entry_t *pte;

	pte = vtopte(va);
	pte_store(pte, pa | PG_RW | PG_V | pmap_cache_bits(kernel_pmap,
	    mode, 0));
}

/*
 * Remove a page from the kernel pagetables.
 * Note: not SMP coherent.
 *
 * This function may be used before pmap_bootstrap() is called.
 */
static void
__CONCAT(PMTYPE, kremove)(vm_offset_t va)
{
	pt_entry_t *pte;

	pte = vtopte(va);
	pte_clear(pte);
}

/*
 *	Used to map a range of physical addresses into kernel
 *	virtual address space.
 *
 *	The value passed in '*virt' is a suggested virtual address for
 *	the mapping. Architectures which can support a direct-mapped
 *	physical to virtual region can return the appropriate address
 *	within that region, leaving '*virt' unchanged. Other
 *	architectures should map the pages starting at '*virt' and
 *	update '*virt' with the first usable address after the mapped
 *	region.
 */
static vm_offset_t
__CONCAT(PMTYPE, map)(vm_offset_t *virt, vm_paddr_t start, vm_paddr_t end,
    int prot)
{
	vm_offset_t va, sva;
	vm_paddr_t superpage_offset;
	pd_entry_t newpde;

	va = *virt;
	/*
	 * Does the physical address range's size and alignment permit at
	 * least one superpage mapping to be created?
	 */ 
	superpage_offset = start & PDRMASK;
	if ((end - start) - ((NBPDR - superpage_offset) & PDRMASK) >= NBPDR) {
		/*
		 * Increase the starting virtual address so that its alignment
		 * does not preclude the use of superpage mappings.
		 */
		if ((va & PDRMASK) < superpage_offset)
			va = (va & ~PDRMASK) + superpage_offset;
		else if ((va & PDRMASK) > superpage_offset)
			va = ((va + PDRMASK) & ~PDRMASK) + superpage_offset;
	}
	sva = va;
	while (start < end) {
		if ((start & PDRMASK) == 0 && end - start >= NBPDR &&
		    pseflag != 0) {
			KASSERT((va & PDRMASK) == 0,
			    ("pmap_map: misaligned va %#x", va));
			newpde = start | PG_PS | PG_RW | PG_V;
			pmap_kenter_pde(va, newpde);
			va += NBPDR;
			start += NBPDR;
		} else {
			pmap_kenter(va, start);
			va += PAGE_SIZE;
			start += PAGE_SIZE;
		}
	}
	pmap_invalidate_range_int(kernel_pmap, sva, va);
	*virt = va;
	return (sva);
}


/*
 * Add a list of wired pages to the kva
 * this routine is only used for temporary
 * kernel mappings that do not need to have
 * page modification or references recorded.
 * Note that old mappings are simply written
 * over.  The page *must* be wired.
 * Note: SMP coherent.  Uses a ranged shootdown IPI.
 */
static void
__CONCAT(PMTYPE, qenter)(vm_offset_t sva, vm_page_t *ma, int count)
{
	pt_entry_t *endpte, oldpte, pa, *pte;
	vm_page_t m;

	oldpte = 0;
	pte = vtopte(sva);
	endpte = pte + count;
	while (pte < endpte) {
		m = *ma++;
		pa = VM_PAGE_TO_PHYS(m) | pmap_cache_bits(kernel_pmap,
		    m->md.pat_mode, 0);
		if ((*pte & (PG_FRAME | PG_PTE_CACHE)) != pa) {
			oldpte |= *pte;
#ifdef PMAP_PAE_COMP
			pte_store(pte, pa | pg_nx | PG_RW | PG_V);
#else
			pte_store(pte, pa | PG_RW | PG_V);
#endif
		}
		pte++;
	}
	if (__predict_false((oldpte & PG_V) != 0))
		pmap_invalidate_range_int(kernel_pmap, sva, sva + count *
		    PAGE_SIZE);
}

/*
 * This routine tears out page mappings from the
 * kernel -- it is meant only for temporary mappings.
 * Note: SMP coherent.  Uses a ranged shootdown IPI.
 */
static void
__CONCAT(PMTYPE, qremove)(vm_offset_t sva, int count)
{
	vm_offset_t va;

	va = sva;
	while (count-- > 0) {
		pmap_kremove(va);
		va += PAGE_SIZE;
	}
	pmap_invalidate_range_int(kernel_pmap, sva, va);
}

/***************************************************
 * Page table page management routines.....
 ***************************************************/
/*
 * Schedule the specified unused page table page to be freed.  Specifically,
 * add the page to the specified list of pages that will be released to the
 * physical memory manager after the TLB has been updated.
 */
static __inline void
pmap_add_delayed_free_list(vm_page_t m, struct spglist *free,
    boolean_t set_PG_ZERO)
{

	if (set_PG_ZERO)
		m->flags |= PG_ZERO;
	else
		m->flags &= ~PG_ZERO;
	SLIST_INSERT_HEAD(free, m, plinks.s.ss);
}

/*
 * Inserts the specified page table page into the specified pmap's collection
 * of idle page table pages.  Each of a pmap's page table pages is responsible
 * for mapping a distinct range of virtual addresses.  The pmap's collection is
 * ordered by this virtual address range.
 */
static __inline int
pmap_insert_pt_page(pmap_t pmap, vm_page_t mpte)
{

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	return (vm_radix_insert(&pmap->pm_root, mpte));
}

/*
 * Removes the page table page mapping the specified virtual address from the
 * specified pmap's collection of idle page table pages, and returns it.
 * Otherwise, returns NULL if there is no page table page corresponding to the
 * specified virtual address.
 */
static __inline vm_page_t
pmap_remove_pt_page(pmap_t pmap, vm_offset_t va)
{

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	return (vm_radix_remove(&pmap->pm_root, va >> PDRSHIFT));
}

/*
 * Decrements a page table page's wire count, which is used to record the
 * number of valid page table entries within the page.  If the wire count
 * drops to zero, then the page table page is unmapped.  Returns TRUE if the
 * page table page was unmapped and FALSE otherwise.
 */
static inline boolean_t
pmap_unwire_ptp(pmap_t pmap, vm_page_t m, struct spglist *free)
{

	--m->wire_count;
	if (m->wire_count == 0) {
		_pmap_unwire_ptp(pmap, m, free);
		return (TRUE);
	} else
		return (FALSE);
}

static void
_pmap_unwire_ptp(pmap_t pmap, vm_page_t m, struct spglist *free)
{

	/*
	 * unmap the page table page
	 */
	pmap->pm_pdir[m->pindex] = 0;
	--pmap->pm_stats.resident_count;

	/*
	 * There is not need to invalidate the recursive mapping since
	 * we never instantiate such mapping for the usermode pmaps,
	 * and never remove page table pages from the kernel pmap.
	 * Put page on a list so that it is released since all TLB
	 * shootdown is done.
	 */
	MPASS(pmap != kernel_pmap);
	pmap_add_delayed_free_list(m, free, TRUE);
}

/*
 * After removing a page table entry, this routine is used to
 * conditionally free the page, and manage the hold/wire counts.
 */
static int
pmap_unuse_pt(pmap_t pmap, vm_offset_t va, struct spglist *free)
{
	pd_entry_t ptepde;
	vm_page_t mpte;

	if (pmap == kernel_pmap)
		return (0);
	ptepde = *pmap_pde(pmap, va);
	mpte = PHYS_TO_VM_PAGE(ptepde & PG_FRAME);
	return (pmap_unwire_ptp(pmap, mpte, free));
}

/*
 * Initialize the pmap for the swapper process.
 */
static void
__CONCAT(PMTYPE, pinit0)(pmap_t pmap)
{

	PMAP_LOCK_INIT(pmap);
	pmap->pm_pdir = IdlePTD;
#ifdef PMAP_PAE_COMP
	pmap->pm_pdpt = IdlePDPT;
#endif
	pmap->pm_root.rt_root = 0;
	CPU_ZERO(&pmap->pm_active);
	TAILQ_INIT(&pmap->pm_pvchunk);
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);
	pmap_activate_boot(pmap);
}

/*
 * Initialize a preallocated and zeroed pmap structure,
 * such as one in a vmspace structure.
 */
static int
__CONCAT(PMTYPE, pinit)(pmap_t pmap)
{
	vm_page_t m;
	int i;

	/*
	 * No need to allocate page table space yet but we do need a valid
	 * page directory table.
	 */
	if (pmap->pm_pdir == NULL) {
		pmap->pm_pdir = (pd_entry_t *)kva_alloc(NBPTD);
		if (pmap->pm_pdir == NULL)
			return (0);
#ifdef PMAP_PAE_COMP
		pmap->pm_pdpt = uma_zalloc(pdptzone, M_WAITOK | M_ZERO);
		KASSERT(((vm_offset_t)pmap->pm_pdpt &
		    ((NPGPTD * sizeof(pdpt_entry_t)) - 1)) == 0,
		    ("pmap_pinit: pdpt misaligned"));
		KASSERT(pmap_kextract((vm_offset_t)pmap->pm_pdpt) < (4ULL<<30),
		    ("pmap_pinit: pdpt above 4g"));
#endif
		pmap->pm_root.rt_root = 0;
	}
	KASSERT(vm_radix_is_empty(&pmap->pm_root),
	    ("pmap_pinit: pmap has reserved page table page(s)"));

	/*
	 * allocate the page directory page(s)
	 */
	for (i = 0; i < NPGPTD; i++) {
		m = vm_page_alloc(NULL, 0, VM_ALLOC_NORMAL | VM_ALLOC_NOOBJ |
		    VM_ALLOC_WIRED | VM_ALLOC_ZERO | VM_ALLOC_WAITOK);
		pmap->pm_ptdpg[i] = m;
#ifdef PMAP_PAE_COMP
		pmap->pm_pdpt[i] = VM_PAGE_TO_PHYS(m) | PG_V;
#endif
	}

	pmap_qenter((vm_offset_t)pmap->pm_pdir, pmap->pm_ptdpg, NPGPTD);
#ifdef PMAP_PAE_COMP
	if ((cpu_feature & CPUID_PAT) == 0) {
		pmap_invalidate_cache_range(
		    trunc_page((vm_offset_t)pmap->pm_pdpt),
		    round_page((vm_offset_t)pmap->pm_pdpt +
		    NPGPTD * sizeof(pdpt_entry_t)));
	}
#endif

	for (i = 0; i < NPGPTD; i++)
		if ((pmap->pm_ptdpg[i]->flags & PG_ZERO) == 0)
			pagezero(pmap->pm_pdir + (i * NPDEPG));

	/* Install the trampoline mapping. */
	pmap->pm_pdir[TRPTDI] = PTD[TRPTDI];

	CPU_ZERO(&pmap->pm_active);
	TAILQ_INIT(&pmap->pm_pvchunk);
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);

	return (1);
}

/*
 * this routine is called if the page table page is not
 * mapped correctly.
 */
static vm_page_t
_pmap_allocpte(pmap_t pmap, u_int ptepindex, u_int flags)
{
	vm_paddr_t ptepa;
	vm_page_t m;

	/*
	 * Allocate a page table page.
	 */
	if ((m = vm_page_alloc(NULL, ptepindex, VM_ALLOC_NOOBJ |
	    VM_ALLOC_WIRED | VM_ALLOC_ZERO)) == NULL) {
		if ((flags & PMAP_ENTER_NOSLEEP) == 0) {
			PMAP_UNLOCK(pmap);
			rw_wunlock(&pvh_global_lock);
			vm_wait(NULL);
			rw_wlock(&pvh_global_lock);
			PMAP_LOCK(pmap);
		}

		/*
		 * Indicate the need to retry.  While waiting, the page table
		 * page may have been allocated.
		 */
		return (NULL);
	}
	if ((m->flags & PG_ZERO) == 0)
		pmap_zero_page(m);

	/*
	 * Map the pagetable page into the process address space, if
	 * it isn't already there.
	 */

	pmap->pm_stats.resident_count++;

	ptepa = VM_PAGE_TO_PHYS(m);
	pmap->pm_pdir[ptepindex] =
		(pd_entry_t) (ptepa | PG_U | PG_RW | PG_V | PG_A | PG_M);

	return (m);
}

static vm_page_t
pmap_allocpte(pmap_t pmap, vm_offset_t va, u_int flags)
{
	u_int ptepindex;
	pd_entry_t ptepa;
	vm_page_t m;

	/*
	 * Calculate pagetable page index
	 */
	ptepindex = va >> PDRSHIFT;
retry:
	/*
	 * Get the page directory entry
	 */
	ptepa = pmap->pm_pdir[ptepindex];

	/*
	 * This supports switching from a 4MB page to a
	 * normal 4K page.
	 */
	if (ptepa & PG_PS) {
		(void)pmap_demote_pde(pmap, &pmap->pm_pdir[ptepindex], va);
		ptepa = pmap->pm_pdir[ptepindex];
	}

	/*
	 * If the page table page is mapped, we just increment the
	 * hold count, and activate it.
	 */
	if (ptepa) {
		m = PHYS_TO_VM_PAGE(ptepa & PG_FRAME);
		m->wire_count++;
	} else {
		/*
		 * Here if the pte page isn't mapped, or if it has
		 * been deallocated. 
		 */
		m = _pmap_allocpte(pmap, ptepindex, flags);
		if (m == NULL && (flags & PMAP_ENTER_NOSLEEP) == 0)
			goto retry;
	}
	return (m);
}


/***************************************************
* Pmap allocation/deallocation routines.
 ***************************************************/

/*
 * Release any resources held by the given physical map.
 * Called when a pmap initialized by pmap_pinit is being released.
 * Should only be called if the map contains no valid mappings.
 */
static void
__CONCAT(PMTYPE, release)(pmap_t pmap)
{
	vm_page_t m;
	int i;

	KASSERT(pmap->pm_stats.resident_count == 0,
	    ("pmap_release: pmap resident count %ld != 0",
	    pmap->pm_stats.resident_count));
	KASSERT(vm_radix_is_empty(&pmap->pm_root),
	    ("pmap_release: pmap has reserved page table page(s)"));
	KASSERT(CPU_EMPTY(&pmap->pm_active),
	    ("releasing active pmap %p", pmap));

	pmap_qremove((vm_offset_t)pmap->pm_pdir, NPGPTD);

	for (i = 0; i < NPGPTD; i++) {
		m = pmap->pm_ptdpg[i];
#ifdef PMAP_PAE_COMP
		KASSERT(VM_PAGE_TO_PHYS(m) == (pmap->pm_pdpt[i] & PG_FRAME),
		    ("pmap_release: got wrong ptd page"));
#endif
		vm_page_unwire_noq(m);
		vm_page_free(m);
	}
}

/*
 * grow the number of kernel page table entries, if needed
 */
static void
__CONCAT(PMTYPE, growkernel)(vm_offset_t addr)
{
	vm_paddr_t ptppaddr;
	vm_page_t nkpg;
	pd_entry_t newpdir;

	mtx_assert(&kernel_map->system_mtx, MA_OWNED);
	addr = roundup2(addr, NBPDR);
	if (addr - 1 >= vm_map_max(kernel_map))
		addr = vm_map_max(kernel_map);
	while (kernel_vm_end < addr) {
		if (pdir_pde(PTD, kernel_vm_end)) {
			kernel_vm_end = (kernel_vm_end + NBPDR) & ~PDRMASK;
			if (kernel_vm_end - 1 >= vm_map_max(kernel_map)) {
				kernel_vm_end = vm_map_max(kernel_map);
				break;
			}
			continue;
		}

		nkpg = vm_page_alloc(NULL, kernel_vm_end >> PDRSHIFT,
		    VM_ALLOC_INTERRUPT | VM_ALLOC_NOOBJ | VM_ALLOC_WIRED |
		    VM_ALLOC_ZERO);
		if (nkpg == NULL)
			panic("pmap_growkernel: no memory to grow kernel");

		nkpt++;

		if ((nkpg->flags & PG_ZERO) == 0)
			pmap_zero_page(nkpg);
		ptppaddr = VM_PAGE_TO_PHYS(nkpg);
		newpdir = (pd_entry_t) (ptppaddr | PG_V | PG_RW | PG_A | PG_M);
		pdir_pde(KPTD, kernel_vm_end) = newpdir;

		pmap_kenter_pde(kernel_vm_end, newpdir);
		kernel_vm_end = (kernel_vm_end + NBPDR) & ~PDRMASK;
		if (kernel_vm_end - 1 >= vm_map_max(kernel_map)) {
			kernel_vm_end = vm_map_max(kernel_map);
			break;
		}
	}
}


/***************************************************
 * page management routines.
 ***************************************************/

CTASSERT(sizeof(struct pv_chunk) == PAGE_SIZE);
CTASSERT(_NPCM == 11);
CTASSERT(_NPCPV == 336);

static __inline struct pv_chunk *
pv_to_chunk(pv_entry_t pv)
{

	return ((struct pv_chunk *)((uintptr_t)pv & ~(uintptr_t)PAGE_MASK));
}

#define PV_PMAP(pv) (pv_to_chunk(pv)->pc_pmap)

#define	PC_FREE0_9	0xfffffffful	/* Free values for index 0 through 9 */
#define	PC_FREE10	0x0000fffful	/* Free values for index 10 */

static const uint32_t pc_freemask[_NPCM] = {
	PC_FREE0_9, PC_FREE0_9, PC_FREE0_9,
	PC_FREE0_9, PC_FREE0_9, PC_FREE0_9,
	PC_FREE0_9, PC_FREE0_9, PC_FREE0_9,
	PC_FREE0_9, PC_FREE10
};

#ifdef PV_STATS
extern int pc_chunk_count, pc_chunk_allocs, pc_chunk_frees, pc_chunk_tryfail;
extern long pv_entry_frees, pv_entry_allocs;
extern int pv_entry_spare;
#endif

/*
 * We are in a serious low memory condition.  Resort to
 * drastic measures to free some pages so we can allocate
 * another pv entry chunk.
 */
static vm_page_t
pmap_pv_reclaim(pmap_t locked_pmap)
{
	struct pch newtail;
	struct pv_chunk *pc;
	struct md_page *pvh;
	pd_entry_t *pde;
	pmap_t pmap;
	pt_entry_t *pte, tpte;
	pv_entry_t pv;
	vm_offset_t va;
	vm_page_t m, m_pc;
	struct spglist free;
	uint32_t inuse;
	int bit, field, freed;

	PMAP_LOCK_ASSERT(locked_pmap, MA_OWNED);
	pmap = NULL;
	m_pc = NULL;
	SLIST_INIT(&free);
	TAILQ_INIT(&newtail);
	while ((pc = TAILQ_FIRST(&pv_chunks)) != NULL && (pv_vafree == 0 ||
	    SLIST_EMPTY(&free))) {
		TAILQ_REMOVE(&pv_chunks, pc, pc_lru);
		if (pmap != pc->pc_pmap) {
			if (pmap != NULL) {
				pmap_invalidate_all_int(pmap);
				if (pmap != locked_pmap)
					PMAP_UNLOCK(pmap);
			}
			pmap = pc->pc_pmap;
			/* Avoid deadlock and lock recursion. */
			if (pmap > locked_pmap)
				PMAP_LOCK(pmap);
			else if (pmap != locked_pmap && !PMAP_TRYLOCK(pmap)) {
				pmap = NULL;
				TAILQ_INSERT_TAIL(&newtail, pc, pc_lru);
				continue;
			}
		}

		/*
		 * Destroy every non-wired, 4 KB page mapping in the chunk.
		 */
		freed = 0;
		for (field = 0; field < _NPCM; field++) {
			for (inuse = ~pc->pc_map[field] & pc_freemask[field];
			    inuse != 0; inuse &= ~(1UL << bit)) {
				bit = bsfl(inuse);
				pv = &pc->pc_pventry[field * 32 + bit];
				va = pv->pv_va;
				pde = pmap_pde(pmap, va);
				if ((*pde & PG_PS) != 0)
					continue;
				pte = __CONCAT(PMTYPE, pte)(pmap, va);
				tpte = *pte;
				if ((tpte & PG_W) == 0)
					tpte = pte_load_clear(pte);
				pmap_pte_release(pte);
				if ((tpte & PG_W) != 0)
					continue;
				KASSERT(tpte != 0,
				    ("pmap_pv_reclaim: pmap %p va %x zero pte",
				    pmap, va));
				if ((tpte & PG_G) != 0)
					pmap_invalidate_page_int(pmap, va);
				m = PHYS_TO_VM_PAGE(tpte & PG_FRAME);
				if ((tpte & (PG_M | PG_RW)) == (PG_M | PG_RW))
					vm_page_dirty(m);
				if ((tpte & PG_A) != 0)
					vm_page_aflag_set(m, PGA_REFERENCED);
				TAILQ_REMOVE(&m->md.pv_list, pv, pv_next);
				if (TAILQ_EMPTY(&m->md.pv_list) &&
				    (m->flags & PG_FICTITIOUS) == 0) {
					pvh = pa_to_pvh(VM_PAGE_TO_PHYS(m));
					if (TAILQ_EMPTY(&pvh->pv_list)) {
						vm_page_aflag_clear(m,
						    PGA_WRITEABLE);
					}
				}
				pc->pc_map[field] |= 1UL << bit;
				pmap_unuse_pt(pmap, va, &free);
				freed++;
			}
		}
		if (freed == 0) {
			TAILQ_INSERT_TAIL(&newtail, pc, pc_lru);
			continue;
		}
		/* Every freed mapping is for a 4 KB page. */
		pmap->pm_stats.resident_count -= freed;
		PV_STAT(pv_entry_frees += freed);
		PV_STAT(pv_entry_spare += freed);
		pv_entry_count -= freed;
		TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
		for (field = 0; field < _NPCM; field++)
			if (pc->pc_map[field] != pc_freemask[field]) {
				TAILQ_INSERT_HEAD(&pmap->pm_pvchunk, pc,
				    pc_list);
				TAILQ_INSERT_TAIL(&newtail, pc, pc_lru);

				/*
				 * One freed pv entry in locked_pmap is
				 * sufficient.
				 */
				if (pmap == locked_pmap)
					goto out;
				break;
			}
		if (field == _NPCM) {
			PV_STAT(pv_entry_spare -= _NPCPV);
			PV_STAT(pc_chunk_count--);
			PV_STAT(pc_chunk_frees++);
			/* Entire chunk is free; return it. */
			m_pc = PHYS_TO_VM_PAGE(pmap_kextract((vm_offset_t)pc));
			pmap_qremove((vm_offset_t)pc, 1);
			pmap_ptelist_free(&pv_vafree, (vm_offset_t)pc);
			break;
		}
	}
out:
	TAILQ_CONCAT(&pv_chunks, &newtail, pc_lru);
	if (pmap != NULL) {
		pmap_invalidate_all_int(pmap);
		if (pmap != locked_pmap)
			PMAP_UNLOCK(pmap);
	}
	if (m_pc == NULL && pv_vafree != 0 && SLIST_EMPTY(&free)) {
		m_pc = SLIST_FIRST(&free);
		SLIST_REMOVE_HEAD(&free, plinks.s.ss);
		/* Recycle a freed page table page. */
		m_pc->wire_count = 1;
	}
	vm_page_free_pages_toq(&free, true);
	return (m_pc);
}

/*
 * free the pv_entry back to the free list
 */
static void
free_pv_entry(pmap_t pmap, pv_entry_t pv)
{
	struct pv_chunk *pc;
	int idx, field, bit;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	PV_STAT(pv_entry_frees++);
	PV_STAT(pv_entry_spare++);
	pv_entry_count--;
	pc = pv_to_chunk(pv);
	idx = pv - &pc->pc_pventry[0];
	field = idx / 32;
	bit = idx % 32;
	pc->pc_map[field] |= 1ul << bit;
	for (idx = 0; idx < _NPCM; idx++)
		if (pc->pc_map[idx] != pc_freemask[idx]) {
			/*
			 * 98% of the time, pc is already at the head of the
			 * list.  If it isn't already, move it to the head.
			 */
			if (__predict_false(TAILQ_FIRST(&pmap->pm_pvchunk) !=
			    pc)) {
				TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
				TAILQ_INSERT_HEAD(&pmap->pm_pvchunk, pc,
				    pc_list);
			}
			return;
		}
	TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
	free_pv_chunk(pc);
}

static void
free_pv_chunk(struct pv_chunk *pc)
{
	vm_page_t m;

 	TAILQ_REMOVE(&pv_chunks, pc, pc_lru);
	PV_STAT(pv_entry_spare -= _NPCPV);
	PV_STAT(pc_chunk_count--);
	PV_STAT(pc_chunk_frees++);
	/* entire chunk is free, return it */
	m = PHYS_TO_VM_PAGE(pmap_kextract((vm_offset_t)pc));
	pmap_qremove((vm_offset_t)pc, 1);
	vm_page_unwire(m, PQ_NONE);
	vm_page_free(m);
	pmap_ptelist_free(&pv_vafree, (vm_offset_t)pc);
}

/*
 * get a new pv_entry, allocating a block from the system
 * when needed.
 */
static pv_entry_t
get_pv_entry(pmap_t pmap, boolean_t try)
{
	static const struct timeval printinterval = { 60, 0 };
	static struct timeval lastprint;
	int bit, field;
	pv_entry_t pv;
	struct pv_chunk *pc;
	vm_page_t m;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	PV_STAT(pv_entry_allocs++);
	pv_entry_count++;
	if (pv_entry_count > pv_entry_high_water)
		if (ratecheck(&lastprint, &printinterval))
			printf("Approaching the limit on PV entries, consider "
			    "increasing either the vm.pmap.shpgperproc or the "
			    "vm.pmap.pv_entries tunable.\n");
retry:
	pc = TAILQ_FIRST(&pmap->pm_pvchunk);
	if (pc != NULL) {
		for (field = 0; field < _NPCM; field++) {
			if (pc->pc_map[field]) {
				bit = bsfl(pc->pc_map[field]);
				break;
			}
		}
		if (field < _NPCM) {
			pv = &pc->pc_pventry[field * 32 + bit];
			pc->pc_map[field] &= ~(1ul << bit);
			/* If this was the last item, move it to tail */
			for (field = 0; field < _NPCM; field++)
				if (pc->pc_map[field] != 0) {
					PV_STAT(pv_entry_spare--);
					return (pv);	/* not full, return */
				}
			TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
			TAILQ_INSERT_TAIL(&pmap->pm_pvchunk, pc, pc_list);
			PV_STAT(pv_entry_spare--);
			return (pv);
		}
	}
	/*
	 * Access to the ptelist "pv_vafree" is synchronized by the pvh
	 * global lock.  If "pv_vafree" is currently non-empty, it will
	 * remain non-empty until pmap_ptelist_alloc() completes.
	 */
	if (pv_vafree == 0 || (m = vm_page_alloc(NULL, 0, VM_ALLOC_NORMAL |
	    VM_ALLOC_NOOBJ | VM_ALLOC_WIRED)) == NULL) {
		if (try) {
			pv_entry_count--;
			PV_STAT(pc_chunk_tryfail++);
			return (NULL);
		}
		m = pmap_pv_reclaim(pmap);
		if (m == NULL)
			goto retry;
	}
	PV_STAT(pc_chunk_count++);
	PV_STAT(pc_chunk_allocs++);
	pc = (struct pv_chunk *)pmap_ptelist_alloc(&pv_vafree);
	pmap_qenter((vm_offset_t)pc, &m, 1);
	pc->pc_pmap = pmap;
	pc->pc_map[0] = pc_freemask[0] & ~1ul;	/* preallocated bit 0 */
	for (field = 1; field < _NPCM; field++)
		pc->pc_map[field] = pc_freemask[field];
	TAILQ_INSERT_TAIL(&pv_chunks, pc, pc_lru);
	pv = &pc->pc_pventry[0];
	TAILQ_INSERT_HEAD(&pmap->pm_pvchunk, pc, pc_list);
	PV_STAT(pv_entry_spare += _NPCPV - 1);
	return (pv);
}

static __inline pv_entry_t
pmap_pvh_remove(struct md_page *pvh, pmap_t pmap, vm_offset_t va)
{
	pv_entry_t pv;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	TAILQ_FOREACH(pv, &pvh->pv_list, pv_next) {
		if (pmap == PV_PMAP(pv) && va == pv->pv_va) {
			TAILQ_REMOVE(&pvh->pv_list, pv, pv_next);
			break;
		}
	}
	return (pv);
}

static void
pmap_pv_demote_pde(pmap_t pmap, vm_offset_t va, vm_paddr_t pa)
{
	struct md_page *pvh;
	pv_entry_t pv;
	vm_offset_t va_last;
	vm_page_t m;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	KASSERT((pa & PDRMASK) == 0,
	    ("pmap_pv_demote_pde: pa is not 4mpage aligned"));

	/*
	 * Transfer the 4mpage's pv entry for this mapping to the first
	 * page's pv list.
	 */
	pvh = pa_to_pvh(pa);
	va = trunc_4mpage(va);
	pv = pmap_pvh_remove(pvh, pmap, va);
	KASSERT(pv != NULL, ("pmap_pv_demote_pde: pv not found"));
	m = PHYS_TO_VM_PAGE(pa);
	TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_next);
	/* Instantiate the remaining NPTEPG - 1 pv entries. */
	va_last = va + NBPDR - PAGE_SIZE;
	do {
		m++;
		KASSERT((m->oflags & VPO_UNMANAGED) == 0,
		    ("pmap_pv_demote_pde: page %p is not managed", m));
		va += PAGE_SIZE;
		pmap_insert_entry(pmap, va, m);
	} while (va < va_last);
}

#if VM_NRESERVLEVEL > 0
static void
pmap_pv_promote_pde(pmap_t pmap, vm_offset_t va, vm_paddr_t pa)
{
	struct md_page *pvh;
	pv_entry_t pv;
	vm_offset_t va_last;
	vm_page_t m;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	KASSERT((pa & PDRMASK) == 0,
	    ("pmap_pv_promote_pde: pa is not 4mpage aligned"));

	/*
	 * Transfer the first page's pv entry for this mapping to the
	 * 4mpage's pv list.  Aside from avoiding the cost of a call
	 * to get_pv_entry(), a transfer avoids the possibility that
	 * get_pv_entry() calls pmap_collect() and that pmap_collect()
	 * removes one of the mappings that is being promoted.
	 */
	m = PHYS_TO_VM_PAGE(pa);
	va = trunc_4mpage(va);
	pv = pmap_pvh_remove(&m->md, pmap, va);
	KASSERT(pv != NULL, ("pmap_pv_promote_pde: pv not found"));
	pvh = pa_to_pvh(pa);
	TAILQ_INSERT_TAIL(&pvh->pv_list, pv, pv_next);
	/* Free the remaining NPTEPG - 1 pv entries. */
	va_last = va + NBPDR - PAGE_SIZE;
	do {
		m++;
		va += PAGE_SIZE;
		pmap_pvh_free(&m->md, pmap, va);
	} while (va < va_last);
}
#endif /* VM_NRESERVLEVEL > 0 */

static void
pmap_pvh_free(struct md_page *pvh, pmap_t pmap, vm_offset_t va)
{
	pv_entry_t pv;

	pv = pmap_pvh_remove(pvh, pmap, va);
	KASSERT(pv != NULL, ("pmap_pvh_free: pv not found"));
	free_pv_entry(pmap, pv);
}

static void
pmap_remove_entry(pmap_t pmap, vm_page_t m, vm_offset_t va)
{
	struct md_page *pvh;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	pmap_pvh_free(&m->md, pmap, va);
	if (TAILQ_EMPTY(&m->md.pv_list) && (m->flags & PG_FICTITIOUS) == 0) {
		pvh = pa_to_pvh(VM_PAGE_TO_PHYS(m));
		if (TAILQ_EMPTY(&pvh->pv_list))
			vm_page_aflag_clear(m, PGA_WRITEABLE);
	}
}

/*
 * Create a pv entry for page at pa for
 * (pmap, va).
 */
static void
pmap_insert_entry(pmap_t pmap, vm_offset_t va, vm_page_t m)
{
	pv_entry_t pv;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	pv = get_pv_entry(pmap, FALSE);
	pv->pv_va = va;
	TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_next);
}

/*
 * Conditionally create a pv entry.
 */
static boolean_t
pmap_try_insert_pv_entry(pmap_t pmap, vm_offset_t va, vm_page_t m)
{
	pv_entry_t pv;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	if (pv_entry_count < pv_entry_high_water && 
	    (pv = get_pv_entry(pmap, TRUE)) != NULL) {
		pv->pv_va = va;
		TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_next);
		return (TRUE);
	} else
		return (FALSE);
}

/*
 * Create the pv entries for each of the pages within a superpage.
 */
static bool
pmap_pv_insert_pde(pmap_t pmap, vm_offset_t va, pd_entry_t pde, u_int flags)
{
	struct md_page *pvh;
	pv_entry_t pv;
	bool noreclaim;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	noreclaim = (flags & PMAP_ENTER_NORECLAIM) != 0;
	if ((noreclaim && pv_entry_count >= pv_entry_high_water) ||
	    (pv = get_pv_entry(pmap, noreclaim)) == NULL)
		return (false);
	pv->pv_va = va;
	pvh = pa_to_pvh(pde & PG_PS_FRAME);
	TAILQ_INSERT_TAIL(&pvh->pv_list, pv, pv_next);
	return (true);
}

/*
 * Fills a page table page with mappings to consecutive physical pages.
 */
static void
pmap_fill_ptp(pt_entry_t *firstpte, pt_entry_t newpte)
{
	pt_entry_t *pte;

	for (pte = firstpte; pte < firstpte + NPTEPG; pte++) {
		*pte = newpte;	
		newpte += PAGE_SIZE;
	}
}

/*
 * Tries to demote a 2- or 4MB page mapping.  If demotion fails, the
 * 2- or 4MB page mapping is invalidated.
 */
static boolean_t
pmap_demote_pde(pmap_t pmap, pd_entry_t *pde, vm_offset_t va)
{
	pd_entry_t newpde, oldpde;
	pt_entry_t *firstpte, newpte;
	vm_paddr_t mptepa;
	vm_page_t mpte;
	struct spglist free;
	vm_offset_t sva;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	oldpde = *pde;
	KASSERT((oldpde & (PG_PS | PG_V)) == (PG_PS | PG_V),
	    ("pmap_demote_pde: oldpde is missing PG_PS and/or PG_V"));
	if ((oldpde & PG_A) == 0 || (mpte = pmap_remove_pt_page(pmap, va)) ==
	    NULL) {
		KASSERT((oldpde & PG_W) == 0,
		    ("pmap_demote_pde: page table page for a wired mapping"
		    " is missing"));

		/*
		 * Invalidate the 2- or 4MB page mapping and return
		 * "failure" if the mapping was never accessed or the
		 * allocation of the new page table page fails.
		 */
		if ((oldpde & PG_A) == 0 || (mpte = vm_page_alloc(NULL,
		    va >> PDRSHIFT, VM_ALLOC_NOOBJ | VM_ALLOC_NORMAL |
		    VM_ALLOC_WIRED)) == NULL) {
			SLIST_INIT(&free);
			sva = trunc_4mpage(va);
			pmap_remove_pde(pmap, pde, sva, &free);
			if ((oldpde & PG_G) == 0)
				pmap_invalidate_pde_page(pmap, sva, oldpde);
			vm_page_free_pages_toq(&free, true);
			CTR2(KTR_PMAP, "pmap_demote_pde: failure for va %#x"
			    " in pmap %p", va, pmap);
			return (FALSE);
		}
		if (pmap != kernel_pmap)
			pmap->pm_stats.resident_count++;
	}
	mptepa = VM_PAGE_TO_PHYS(mpte);

	/*
	 * If the page mapping is in the kernel's address space, then the
	 * KPTmap can provide access to the page table page.  Otherwise,
	 * temporarily map the page table page (mpte) into the kernel's
	 * address space at either PADDR1 or PADDR2. 
	 */
	if (pmap == kernel_pmap)
		firstpte = &KPTmap[i386_btop(trunc_4mpage(va))];
	else if (curthread->td_pinned > 0 && rw_wowned(&pvh_global_lock)) {
		if ((*PMAP1 & PG_FRAME) != mptepa) {
			*PMAP1 = mptepa | PG_RW | PG_V | PG_A | PG_M;
#ifdef SMP
			PMAP1cpu = PCPU_GET(cpuid);
#endif
			invlcaddr(PADDR1);
			PMAP1changed++;
		} else
#ifdef SMP
		if (PMAP1cpu != PCPU_GET(cpuid)) {
			PMAP1cpu = PCPU_GET(cpuid);
			invlcaddr(PADDR1);
			PMAP1changedcpu++;
		} else
#endif
			PMAP1unchanged++;
		firstpte = PADDR1;
	} else {
		mtx_lock(&PMAP2mutex);
		if ((*PMAP2 & PG_FRAME) != mptepa) {
			*PMAP2 = mptepa | PG_RW | PG_V | PG_A | PG_M;
			pmap_invalidate_page_int(kernel_pmap,
			    (vm_offset_t)PADDR2);
		}
		firstpte = PADDR2;
	}
	newpde = mptepa | PG_M | PG_A | (oldpde & PG_U) | PG_RW | PG_V;
	KASSERT((oldpde & PG_A) != 0,
	    ("pmap_demote_pde: oldpde is missing PG_A"));
	KASSERT((oldpde & (PG_M | PG_RW)) != PG_RW,
	    ("pmap_demote_pde: oldpde is missing PG_M"));
	newpte = oldpde & ~PG_PS;
	if ((newpte & PG_PDE_PAT) != 0)
		newpte ^= PG_PDE_PAT | PG_PTE_PAT;

	/*
	 * If the page table page is new, initialize it.
	 */
	if (mpte->wire_count == 1) {
		mpte->wire_count = NPTEPG;
		pmap_fill_ptp(firstpte, newpte);
	}
	KASSERT((*firstpte & PG_FRAME) == (newpte & PG_FRAME),
	    ("pmap_demote_pde: firstpte and newpte map different physical"
	    " addresses"));

	/*
	 * If the mapping has changed attributes, update the page table
	 * entries.
	 */ 
	if ((*firstpte & PG_PTE_PROMOTE) != (newpte & PG_PTE_PROMOTE))
		pmap_fill_ptp(firstpte, newpte);
	
	/*
	 * Demote the mapping.  This pmap is locked.  The old PDE has
	 * PG_A set.  If the old PDE has PG_RW set, it also has PG_M
	 * set.  Thus, there is no danger of a race with another
	 * processor changing the setting of PG_A and/or PG_M between
	 * the read above and the store below. 
	 */
	if (workaround_erratum383)
		pmap_update_pde(pmap, va, pde, newpde);
	else if (pmap == kernel_pmap)
		pmap_kenter_pde(va, newpde);
	else
		pde_store(pde, newpde);	
	if (firstpte == PADDR2)
		mtx_unlock(&PMAP2mutex);

	/*
	 * Invalidate the recursive mapping of the page table page.
	 */
	pmap_invalidate_page_int(pmap, (vm_offset_t)vtopte(va));

	/*
	 * Demote the pv entry.  This depends on the earlier demotion
	 * of the mapping.  Specifically, the (re)creation of a per-
	 * page pv entry might trigger the execution of pmap_collect(),
	 * which might reclaim a newly (re)created per-page pv entry
	 * and destroy the associated mapping.  In order to destroy
	 * the mapping, the PDE must have already changed from mapping
	 * the 2mpage to referencing the page table page.
	 */
	if ((oldpde & PG_MANAGED) != 0)
		pmap_pv_demote_pde(pmap, va, oldpde & PG_PS_FRAME);

	pmap_pde_demotions++;
	CTR2(KTR_PMAP, "pmap_demote_pde: success for va %#x"
	    " in pmap %p", va, pmap);
	return (TRUE);
}

/*
 * Removes a 2- or 4MB page mapping from the kernel pmap.
 */
static void
pmap_remove_kernel_pde(pmap_t pmap, pd_entry_t *pde, vm_offset_t va)
{
	pd_entry_t newpde;
	vm_paddr_t mptepa;
	vm_page_t mpte;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	mpte = pmap_remove_pt_page(pmap, va);
	if (mpte == NULL)
		panic("pmap_remove_kernel_pde: Missing pt page.");

	mptepa = VM_PAGE_TO_PHYS(mpte);
	newpde = mptepa | PG_M | PG_A | PG_RW | PG_V;

	/*
	 * Initialize the page table page.
	 */
	pagezero((void *)&KPTmap[i386_btop(trunc_4mpage(va))]);

	/*
	 * Remove the mapping.
	 */
	if (workaround_erratum383)
		pmap_update_pde(pmap, va, pde, newpde);
	else 
		pmap_kenter_pde(va, newpde);

	/*
	 * Invalidate the recursive mapping of the page table page.
	 */
	pmap_invalidate_page_int(pmap, (vm_offset_t)vtopte(va));
}

/*
 * pmap_remove_pde: do the things to unmap a superpage in a process
 */
static void
pmap_remove_pde(pmap_t pmap, pd_entry_t *pdq, vm_offset_t sva,
    struct spglist *free)
{
	struct md_page *pvh;
	pd_entry_t oldpde;
	vm_offset_t eva, va;
	vm_page_t m, mpte;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	KASSERT((sva & PDRMASK) == 0,
	    ("pmap_remove_pde: sva is not 4mpage aligned"));
	oldpde = pte_load_clear(pdq);
	if (oldpde & PG_W)
		pmap->pm_stats.wired_count -= NBPDR / PAGE_SIZE;

	/*
	 * Machines that don't support invlpg, also don't support
	 * PG_G.
	 */
	if ((oldpde & PG_G) != 0)
		pmap_invalidate_pde_page(kernel_pmap, sva, oldpde);

	pmap->pm_stats.resident_count -= NBPDR / PAGE_SIZE;
	if (oldpde & PG_MANAGED) {
		pvh = pa_to_pvh(oldpde & PG_PS_FRAME);
		pmap_pvh_free(pvh, pmap, sva);
		eva = sva + NBPDR;
		for (va = sva, m = PHYS_TO_VM_PAGE(oldpde & PG_PS_FRAME);
		    va < eva; va += PAGE_SIZE, m++) {
			if ((oldpde & (PG_M | PG_RW)) == (PG_M | PG_RW))
				vm_page_dirty(m);
			if (oldpde & PG_A)
				vm_page_aflag_set(m, PGA_REFERENCED);
			if (TAILQ_EMPTY(&m->md.pv_list) &&
			    TAILQ_EMPTY(&pvh->pv_list))
				vm_page_aflag_clear(m, PGA_WRITEABLE);
		}
	}
	if (pmap == kernel_pmap) {
		pmap_remove_kernel_pde(pmap, pdq, sva);
	} else {
		mpte = pmap_remove_pt_page(pmap, sva);
		if (mpte != NULL) {
			pmap->pm_stats.resident_count--;
			KASSERT(mpte->wire_count == NPTEPG,
			    ("pmap_remove_pde: pte page wire count error"));
			mpte->wire_count = 0;
			pmap_add_delayed_free_list(mpte, free, FALSE);
		}
	}
}

/*
 * pmap_remove_pte: do the things to unmap a page in a process
 */
static int
pmap_remove_pte(pmap_t pmap, pt_entry_t *ptq, vm_offset_t va,
    struct spglist *free)
{
	pt_entry_t oldpte;
	vm_page_t m;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	oldpte = pte_load_clear(ptq);
	KASSERT(oldpte != 0,
	    ("pmap_remove_pte: pmap %p va %x zero pte", pmap, va));
	if (oldpte & PG_W)
		pmap->pm_stats.wired_count -= 1;
	/*
	 * Machines that don't support invlpg, also don't support
	 * PG_G.
	 */
	if (oldpte & PG_G)
		pmap_invalidate_page_int(kernel_pmap, va);
	pmap->pm_stats.resident_count -= 1;
	if (oldpte & PG_MANAGED) {
		m = PHYS_TO_VM_PAGE(oldpte & PG_FRAME);
		if ((oldpte & (PG_M | PG_RW)) == (PG_M | PG_RW))
			vm_page_dirty(m);
		if (oldpte & PG_A)
			vm_page_aflag_set(m, PGA_REFERENCED);
		pmap_remove_entry(pmap, m, va);
	}
	return (pmap_unuse_pt(pmap, va, free));
}

/*
 * Remove a single page from a process address space
 */
static void
pmap_remove_page(pmap_t pmap, vm_offset_t va, struct spglist *free)
{
	pt_entry_t *pte;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	KASSERT(curthread->td_pinned > 0, ("curthread not pinned"));
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	if ((pte = pmap_pte_quick(pmap, va)) == NULL || *pte == 0)
		return;
	pmap_remove_pte(pmap, pte, va, free);
	pmap_invalidate_page_int(pmap, va);
}

/*
 * Removes the specified range of addresses from the page table page.
 */
static bool
pmap_remove_ptes(pmap_t pmap, vm_offset_t sva, vm_offset_t eva,
    struct spglist *free)
{
	pt_entry_t *pte;
	bool anyvalid;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	KASSERT(curthread->td_pinned > 0, ("curthread not pinned"));
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	anyvalid = false;
	for (pte = pmap_pte_quick(pmap, sva); sva != eva; pte++,
	    sva += PAGE_SIZE) {
		if (*pte == 0)
			continue;

		/*
		 * The TLB entry for a PG_G mapping is invalidated by
		 * pmap_remove_pte().
		 */
		if ((*pte & PG_G) == 0)
			anyvalid = true;

		if (pmap_remove_pte(pmap, pte, sva, free))
			break;
	}
	return (anyvalid);
}

/*
 *	Remove the given range of addresses from the specified map.
 *
 *	It is assumed that the start and end are properly
 *	rounded to the page size.
 */
static void
__CONCAT(PMTYPE, remove)(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	vm_offset_t pdnxt;
	pd_entry_t ptpaddr;
	struct spglist free;
	int anyvalid;

	/*
	 * Perform an unsynchronized read.  This is, however, safe.
	 */
	if (pmap->pm_stats.resident_count == 0)
		return;

	anyvalid = 0;
	SLIST_INIT(&free);

	rw_wlock(&pvh_global_lock);
	sched_pin();
	PMAP_LOCK(pmap);

	/*
	 * special handling of removing one page.  a very
	 * common operation and easy to short circuit some
	 * code.
	 */
	if ((sva + PAGE_SIZE == eva) && 
	    ((pmap->pm_pdir[(sva >> PDRSHIFT)] & PG_PS) == 0)) {
		pmap_remove_page(pmap, sva, &free);
		goto out;
	}

	for (; sva < eva; sva = pdnxt) {
		u_int pdirindex;

		/*
		 * Calculate index for next page table.
		 */
		pdnxt = (sva + NBPDR) & ~PDRMASK;
		if (pdnxt < sva)
			pdnxt = eva;
		if (pmap->pm_stats.resident_count == 0)
			break;

		pdirindex = sva >> PDRSHIFT;
		ptpaddr = pmap->pm_pdir[pdirindex];

		/*
		 * Weed out invalid mappings. Note: we assume that the page
		 * directory table is always allocated, and in kernel virtual.
		 */
		if (ptpaddr == 0)
			continue;

		/*
		 * Check for large page.
		 */
		if ((ptpaddr & PG_PS) != 0) {
			/*
			 * Are we removing the entire large page?  If not,
			 * demote the mapping and fall through.
			 */
			if (sva + NBPDR == pdnxt && eva >= pdnxt) {
				/*
				 * The TLB entry for a PG_G mapping is
				 * invalidated by pmap_remove_pde().
				 */
				if ((ptpaddr & PG_G) == 0)
					anyvalid = 1;
				pmap_remove_pde(pmap,
				    &pmap->pm_pdir[pdirindex], sva, &free);
				continue;
			} else if (!pmap_demote_pde(pmap,
			    &pmap->pm_pdir[pdirindex], sva)) {
				/* The large page mapping was destroyed. */
				continue;
			}
		}

		/*
		 * Limit our scan to either the end of the va represented
		 * by the current page table page, or to the end of the
		 * range being removed.
		 */
		if (pdnxt > eva)
			pdnxt = eva;

		if (pmap_remove_ptes(pmap, sva, pdnxt, &free))
			anyvalid = 1;
	}
out:
	sched_unpin();
	if (anyvalid)
		pmap_invalidate_all_int(pmap);
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
	vm_page_free_pages_toq(&free, true);
}

/*
 *	Routine:	pmap_remove_all
 *	Function:
 *		Removes this physical page from
 *		all physical maps in which it resides.
 *		Reflects back modify bits to the pager.
 *
 *	Notes:
 *		Original versions of this routine were very
 *		inefficient because they iteratively called
 *		pmap_remove (slow...)
 */

static void
__CONCAT(PMTYPE, remove_all)(vm_page_t m)
{
	struct md_page *pvh;
	pv_entry_t pv;
	pmap_t pmap;
	pt_entry_t *pte, tpte;
	pd_entry_t *pde;
	vm_offset_t va;
	struct spglist free;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_remove_all: page %p is not managed", m));
	SLIST_INIT(&free);
	rw_wlock(&pvh_global_lock);
	sched_pin();
	if ((m->flags & PG_FICTITIOUS) != 0)
		goto small_mappings;
	pvh = pa_to_pvh(VM_PAGE_TO_PHYS(m));
	while ((pv = TAILQ_FIRST(&pvh->pv_list)) != NULL) {
		va = pv->pv_va;
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pde = pmap_pde(pmap, va);
		(void)pmap_demote_pde(pmap, pde, va);
		PMAP_UNLOCK(pmap);
	}
small_mappings:
	while ((pv = TAILQ_FIRST(&m->md.pv_list)) != NULL) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pmap->pm_stats.resident_count--;
		pde = pmap_pde(pmap, pv->pv_va);
		KASSERT((*pde & PG_PS) == 0, ("pmap_remove_all: found"
		    " a 4mpage in page %p's pv list", m));
		pte = pmap_pte_quick(pmap, pv->pv_va);
		tpte = pte_load_clear(pte);
		KASSERT(tpte != 0, ("pmap_remove_all: pmap %p va %x zero pte",
		    pmap, pv->pv_va));
		if (tpte & PG_W)
			pmap->pm_stats.wired_count--;
		if (tpte & PG_A)
			vm_page_aflag_set(m, PGA_REFERENCED);

		/*
		 * Update the vm_page_t clean and reference bits.
		 */
		if ((tpte & (PG_M | PG_RW)) == (PG_M | PG_RW))
			vm_page_dirty(m);
		pmap_unuse_pt(pmap, pv->pv_va, &free);
		pmap_invalidate_page_int(pmap, pv->pv_va);
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_next);
		free_pv_entry(pmap, pv);
		PMAP_UNLOCK(pmap);
	}
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
	vm_page_free_pages_toq(&free, true);
}

/*
 * pmap_protect_pde: do the things to protect a 4mpage in a process
 */
static boolean_t
pmap_protect_pde(pmap_t pmap, pd_entry_t *pde, vm_offset_t sva, vm_prot_t prot)
{
	pd_entry_t newpde, oldpde;
	vm_offset_t eva, va;
	vm_page_t m;
	boolean_t anychanged;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	KASSERT((sva & PDRMASK) == 0,
	    ("pmap_protect_pde: sva is not 4mpage aligned"));
	anychanged = FALSE;
retry:
	oldpde = newpde = *pde;
	if ((oldpde & (PG_MANAGED | PG_M | PG_RW)) ==
	    (PG_MANAGED | PG_M | PG_RW)) {
		eva = sva + NBPDR;
		for (va = sva, m = PHYS_TO_VM_PAGE(oldpde & PG_PS_FRAME);
		    va < eva; va += PAGE_SIZE, m++)
			vm_page_dirty(m);
	}
	if ((prot & VM_PROT_WRITE) == 0)
		newpde &= ~(PG_RW | PG_M);
#ifdef PMAP_PAE_COMP
	if ((prot & VM_PROT_EXECUTE) == 0 && !i386_read_exec)
		newpde |= pg_nx;
#endif
	if (newpde != oldpde) {
		/*
		 * As an optimization to future operations on this PDE, clear
		 * PG_PROMOTED.  The impending invalidation will remove any
		 * lingering 4KB page mappings from the TLB.
		 */
		if (!pde_cmpset(pde, oldpde, newpde & ~PG_PROMOTED))
			goto retry;
		if ((oldpde & PG_G) != 0)
			pmap_invalidate_pde_page(kernel_pmap, sva, oldpde);
		else
			anychanged = TRUE;
	}
	return (anychanged);
}

/*
 *	Set the physical protection on the
 *	specified range of this map as requested.
 */
static void
__CONCAT(PMTYPE, protect)(pmap_t pmap, vm_offset_t sva, vm_offset_t eva,
    vm_prot_t prot)
{
	vm_offset_t pdnxt;
	pd_entry_t ptpaddr;
	pt_entry_t *pte;
	boolean_t anychanged, pv_lists_locked;

	KASSERT((prot & ~VM_PROT_ALL) == 0, ("invalid prot %x", prot));
	if (prot == VM_PROT_NONE) {
		pmap_remove(pmap, sva, eva);
		return;
	}

#ifdef PMAP_PAE_COMP
	if ((prot & (VM_PROT_WRITE | VM_PROT_EXECUTE)) ==
	    (VM_PROT_WRITE | VM_PROT_EXECUTE))
		return;
#else
	if (prot & VM_PROT_WRITE)
		return;
#endif

	if (pmap_is_current(pmap))
		pv_lists_locked = FALSE;
	else {
		pv_lists_locked = TRUE;
resume:
		rw_wlock(&pvh_global_lock);
		sched_pin();
	}
	anychanged = FALSE;

	PMAP_LOCK(pmap);
	for (; sva < eva; sva = pdnxt) {
		pt_entry_t obits, pbits;
		u_int pdirindex;

		pdnxt = (sva + NBPDR) & ~PDRMASK;
		if (pdnxt < sva)
			pdnxt = eva;

		pdirindex = sva >> PDRSHIFT;
		ptpaddr = pmap->pm_pdir[pdirindex];

		/*
		 * Weed out invalid mappings. Note: we assume that the page
		 * directory table is always allocated, and in kernel virtual.
		 */
		if (ptpaddr == 0)
			continue;

		/*
		 * Check for large page.
		 */
		if ((ptpaddr & PG_PS) != 0) {
			/*
			 * Are we protecting the entire large page?  If not,
			 * demote the mapping and fall through.
			 */
			if (sva + NBPDR == pdnxt && eva >= pdnxt) {
				/*
				 * The TLB entry for a PG_G mapping is
				 * invalidated by pmap_protect_pde().
				 */
				if (pmap_protect_pde(pmap,
				    &pmap->pm_pdir[pdirindex], sva, prot))
					anychanged = TRUE;
				continue;
			} else {
				if (!pv_lists_locked) {
					pv_lists_locked = TRUE;
					if (!rw_try_wlock(&pvh_global_lock)) {
						if (anychanged)
							pmap_invalidate_all_int(
							    pmap);
						PMAP_UNLOCK(pmap);
						goto resume;
					}
					sched_pin();
				}
				if (!pmap_demote_pde(pmap,
				    &pmap->pm_pdir[pdirindex], sva)) {
					/*
					 * The large page mapping was
					 * destroyed.
					 */
					continue;
				}
			}
		}

		if (pdnxt > eva)
			pdnxt = eva;

		for (pte = pmap_pte_quick(pmap, sva); sva != pdnxt; pte++,
		    sva += PAGE_SIZE) {
			vm_page_t m;

retry:
			/*
			 * Regardless of whether a pte is 32 or 64 bits in
			 * size, PG_RW, PG_A, and PG_M are among the least
			 * significant 32 bits.
			 */
			obits = pbits = *pte;
			if ((pbits & PG_V) == 0)
				continue;

			if ((prot & VM_PROT_WRITE) == 0) {
				if ((pbits & (PG_MANAGED | PG_M | PG_RW)) ==
				    (PG_MANAGED | PG_M | PG_RW)) {
					m = PHYS_TO_VM_PAGE(pbits & PG_FRAME);
					vm_page_dirty(m);
				}
				pbits &= ~(PG_RW | PG_M);
			}
#ifdef PMAP_PAE_COMP
			if ((prot & VM_PROT_EXECUTE) == 0 && !i386_read_exec)
				pbits |= pg_nx;
#endif

			if (pbits != obits) {
#ifdef PMAP_PAE_COMP
				if (!atomic_cmpset_64(pte, obits, pbits))
					goto retry;
#else
				if (!atomic_cmpset_int((u_int *)pte, obits,
				    pbits))
					goto retry;
#endif
				if (obits & PG_G)
					pmap_invalidate_page_int(pmap, sva);
				else
					anychanged = TRUE;
			}
		}
	}
	if (anychanged)
		pmap_invalidate_all_int(pmap);
	if (pv_lists_locked) {
		sched_unpin();
		rw_wunlock(&pvh_global_lock);
	}
	PMAP_UNLOCK(pmap);
}

#if VM_NRESERVLEVEL > 0
/*
 * Tries to promote the 512 or 1024, contiguous 4KB page mappings that are
 * within a single page table page (PTP) to a single 2- or 4MB page mapping.
 * For promotion to occur, two conditions must be met: (1) the 4KB page
 * mappings must map aligned, contiguous physical memory and (2) the 4KB page
 * mappings must have identical characteristics.
 *
 * Managed (PG_MANAGED) mappings within the kernel address space are not
 * promoted.  The reason is that kernel PDEs are replicated in each pmap but
 * pmap_clear_ptes() and pmap_ts_referenced() only read the PDE from the kernel
 * pmap.
 */
static void
pmap_promote_pde(pmap_t pmap, pd_entry_t *pde, vm_offset_t va)
{
	pd_entry_t newpde;
	pt_entry_t *firstpte, oldpte, pa, *pte;
	vm_offset_t oldpteva;
	vm_page_t mpte;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	/*
	 * Examine the first PTE in the specified PTP.  Abort if this PTE is
	 * either invalid, unused, or does not map the first 4KB physical page
	 * within a 2- or 4MB page.
	 */
	firstpte = pmap_pte_quick(pmap, trunc_4mpage(va));
setpde:
	newpde = *firstpte;
	if ((newpde & ((PG_FRAME & PDRMASK) | PG_A | PG_V)) != (PG_A | PG_V)) {
		pmap_pde_p_failures++;
		CTR2(KTR_PMAP, "pmap_promote_pde: failure for va %#x"
		    " in pmap %p", va, pmap);
		return;
	}
	if ((*firstpte & PG_MANAGED) != 0 && pmap == kernel_pmap) {
		pmap_pde_p_failures++;
		CTR2(KTR_PMAP, "pmap_promote_pde: failure for va %#x"
		    " in pmap %p", va, pmap);
		return;
	}
	if ((newpde & (PG_M | PG_RW)) == PG_RW) {
		/*
		 * When PG_M is already clear, PG_RW can be cleared without
		 * a TLB invalidation.
		 */
		if (!atomic_cmpset_int((u_int *)firstpte, newpde, newpde &
		    ~PG_RW))  
			goto setpde;
		newpde &= ~PG_RW;
	}

	/* 
	 * Examine each of the other PTEs in the specified PTP.  Abort if this
	 * PTE maps an unexpected 4KB physical page or does not have identical
	 * characteristics to the first PTE.
	 */
	pa = (newpde & (PG_PS_FRAME | PG_A | PG_V)) + NBPDR - PAGE_SIZE;
	for (pte = firstpte + NPTEPG - 1; pte > firstpte; pte--) {
setpte:
		oldpte = *pte;
		if ((oldpte & (PG_FRAME | PG_A | PG_V)) != pa) {
			pmap_pde_p_failures++;
			CTR2(KTR_PMAP, "pmap_promote_pde: failure for va %#x"
			    " in pmap %p", va, pmap);
			return;
		}
		if ((oldpte & (PG_M | PG_RW)) == PG_RW) {
			/*
			 * When PG_M is already clear, PG_RW can be cleared
			 * without a TLB invalidation.
			 */
			if (!atomic_cmpset_int((u_int *)pte, oldpte,
			    oldpte & ~PG_RW))
				goto setpte;
			oldpte &= ~PG_RW;
			oldpteva = (oldpte & PG_FRAME & PDRMASK) |
			    (va & ~PDRMASK);
			CTR2(KTR_PMAP, "pmap_promote_pde: protect for va %#x"
			    " in pmap %p", oldpteva, pmap);
		}
		if ((oldpte & PG_PTE_PROMOTE) != (newpde & PG_PTE_PROMOTE)) {
			pmap_pde_p_failures++;
			CTR2(KTR_PMAP, "pmap_promote_pde: failure for va %#x"
			    " in pmap %p", va, pmap);
			return;
		}
		pa -= PAGE_SIZE;
	}

	/*
	 * Save the page table page in its current state until the PDE
	 * mapping the superpage is demoted by pmap_demote_pde() or
	 * destroyed by pmap_remove_pde(). 
	 */
	mpte = PHYS_TO_VM_PAGE(*pde & PG_FRAME);
	KASSERT(mpte >= vm_page_array &&
	    mpte < &vm_page_array[vm_page_array_size],
	    ("pmap_promote_pde: page table page is out of range"));
	KASSERT(mpte->pindex == va >> PDRSHIFT,
	    ("pmap_promote_pde: page table page's pindex is wrong"));
	if (pmap_insert_pt_page(pmap, mpte)) {
		pmap_pde_p_failures++;
		CTR2(KTR_PMAP,
		    "pmap_promote_pde: failure for va %#x in pmap %p", va,
		    pmap);
		return;
	}

	/*
	 * Promote the pv entries.
	 */
	if ((newpde & PG_MANAGED) != 0)
		pmap_pv_promote_pde(pmap, va, newpde & PG_PS_FRAME);

	/*
	 * Propagate the PAT index to its proper position.
	 */
	if ((newpde & PG_PTE_PAT) != 0)
		newpde ^= PG_PDE_PAT | PG_PTE_PAT;

	/*
	 * Map the superpage.
	 */
	if (workaround_erratum383)
		pmap_update_pde(pmap, va, pde, PG_PS | newpde);
	else if (pmap == kernel_pmap)
		pmap_kenter_pde(va, PG_PROMOTED | PG_PS | newpde);
	else
		pde_store(pde, PG_PROMOTED | PG_PS | newpde);

	pmap_pde_promotions++;
	CTR2(KTR_PMAP, "pmap_promote_pde: success for va %#x"
	    " in pmap %p", va, pmap);
}
#endif /* VM_NRESERVLEVEL > 0 */

/*
 *	Insert the given physical page (p) at
 *	the specified virtual address (v) in the
 *	target physical map with the protection requested.
 *
 *	If specified, the page will be wired down, meaning
 *	that the related pte can not be reclaimed.
 *
 *	NB:  This is the only routine which MAY NOT lazy-evaluate
 *	or lose information.  That is, this routine must actually
 *	insert this page into the given map NOW.
 */
static int
__CONCAT(PMTYPE, enter)(pmap_t pmap, vm_offset_t va, vm_page_t m,
    vm_prot_t prot, u_int flags, int8_t psind)
{
	pd_entry_t *pde;
	pt_entry_t *pte;
	pt_entry_t newpte, origpte;
	pv_entry_t pv;
	vm_paddr_t opa, pa;
	vm_page_t mpte, om;
	int rv;

	va = trunc_page(va);
	KASSERT((pmap == kernel_pmap && va < VM_MAX_KERNEL_ADDRESS) ||
	    (pmap != kernel_pmap && va < VM_MAXUSER_ADDRESS),
	    ("pmap_enter: toobig k%d %#x", pmap == kernel_pmap, va));
	KASSERT(va < PMAP_TRM_MIN_ADDRESS,
	    ("pmap_enter: invalid to pmap_enter into trampoline (va: 0x%x)",
	    va));
	KASSERT(pmap != kernel_pmap || (m->oflags & VPO_UNMANAGED) != 0 ||
	    va < kmi.clean_sva || va >= kmi.clean_eva,
	    ("pmap_enter: managed mapping within the clean submap"));
	if ((m->oflags & VPO_UNMANAGED) == 0 && !vm_page_xbusied(m))
		VM_OBJECT_ASSERT_LOCKED(m->object);
	KASSERT((flags & PMAP_ENTER_RESERVED) == 0,
	    ("pmap_enter: flags %u has reserved bits set", flags));
	pa = VM_PAGE_TO_PHYS(m);
	newpte = (pt_entry_t)(pa | PG_A | PG_V);
	if ((flags & VM_PROT_WRITE) != 0)
		newpte |= PG_M;
	if ((prot & VM_PROT_WRITE) != 0)
		newpte |= PG_RW;
	KASSERT((newpte & (PG_M | PG_RW)) != PG_M,
	    ("pmap_enter: flags includes VM_PROT_WRITE but prot doesn't"));
#ifdef PMAP_PAE_COMP
	if ((prot & VM_PROT_EXECUTE) == 0 && !i386_read_exec)
		newpte |= pg_nx;
#endif
	if ((flags & PMAP_ENTER_WIRED) != 0)
		newpte |= PG_W;
	if (pmap != kernel_pmap)
		newpte |= PG_U;
	newpte |= pmap_cache_bits(pmap, m->md.pat_mode, psind > 0);
	if ((m->oflags & VPO_UNMANAGED) == 0)
		newpte |= PG_MANAGED;

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	sched_pin();
	if (psind == 1) {
		/* Assert the required virtual and physical alignment. */ 
		KASSERT((va & PDRMASK) == 0, ("pmap_enter: va unaligned"));
		KASSERT(m->psind > 0, ("pmap_enter: m->psind < psind"));
		rv = pmap_enter_pde(pmap, va, newpte | PG_PS, flags, m);
		goto out;
	}

	pde = pmap_pde(pmap, va);
	if (pmap != kernel_pmap) {
		/*
		 * va is for UVA.
		 * In the case that a page table page is not resident,
		 * we are creating it here.  pmap_allocpte() handles
		 * demotion.
		 */
		mpte = pmap_allocpte(pmap, va, flags);
		if (mpte == NULL) {
			KASSERT((flags & PMAP_ENTER_NOSLEEP) != 0,
			    ("pmap_allocpte failed with sleep allowed"));
			rv = KERN_RESOURCE_SHORTAGE;
			goto out;
		}
	} else {
		/*
		 * va is for KVA, so pmap_demote_pde() will never fail
		 * to install a page table page.  PG_V is also
		 * asserted by pmap_demote_pde().
		 */
		mpte = NULL;
		KASSERT(pde != NULL && (*pde & PG_V) != 0,
		    ("KVA %#x invalid pde pdir %#jx", va,
		    (uintmax_t)pmap->pm_pdir[PTDPTDI]));
		if ((*pde & PG_PS) != 0)
			pmap_demote_pde(pmap, pde, va);
	}
	pte = pmap_pte_quick(pmap, va);

	/*
	 * Page Directory table entry is not valid, which should not
	 * happen.  We should have either allocated the page table
	 * page or demoted the existing mapping above.
	 */
	if (pte == NULL) {
		panic("pmap_enter: invalid page directory pdir=%#jx, va=%#x",
		    (uintmax_t)pmap->pm_pdir[PTDPTDI], va);
	}

	origpte = *pte;
	pv = NULL;

	/*
	 * Is the specified virtual address already mapped?
	 */
	if ((origpte & PG_V) != 0) {
		/*
		 * Wiring change, just update stats. We don't worry about
		 * wiring PT pages as they remain resident as long as there
		 * are valid mappings in them. Hence, if a user page is wired,
		 * the PT page will be also.
		 */
		if ((newpte & PG_W) != 0 && (origpte & PG_W) == 0)
			pmap->pm_stats.wired_count++;
		else if ((newpte & PG_W) == 0 && (origpte & PG_W) != 0)
			pmap->pm_stats.wired_count--;

		/*
		 * Remove the extra PT page reference.
		 */
		if (mpte != NULL) {
			mpte->wire_count--;
			KASSERT(mpte->wire_count > 0,
			    ("pmap_enter: missing reference to page table page,"
			     " va: 0x%x", va));
		}

		/*
		 * Has the physical page changed?
		 */
		opa = origpte & PG_FRAME;
		if (opa == pa) {
			/*
			 * No, might be a protection or wiring change.
			 */
			if ((origpte & PG_MANAGED) != 0 &&
			    (newpte & PG_RW) != 0)
				vm_page_aflag_set(m, PGA_WRITEABLE);
			if (((origpte ^ newpte) & ~(PG_M | PG_A)) == 0)
				goto unchanged;
			goto validate;
		}

		/*
		 * The physical page has changed.  Temporarily invalidate
		 * the mapping.  This ensures that all threads sharing the
		 * pmap keep a consistent view of the mapping, which is
		 * necessary for the correct handling of COW faults.  It
		 * also permits reuse of the old mapping's PV entry,
		 * avoiding an allocation.
		 *
		 * For consistency, handle unmanaged mappings the same way.
		 */
		origpte = pte_load_clear(pte);
		KASSERT((origpte & PG_FRAME) == opa,
		    ("pmap_enter: unexpected pa update for %#x", va));
		if ((origpte & PG_MANAGED) != 0) {
			om = PHYS_TO_VM_PAGE(opa);

			/*
			 * The pmap lock is sufficient to synchronize with
			 * concurrent calls to pmap_page_test_mappings() and
			 * pmap_ts_referenced().
			 */
			if ((origpte & (PG_M | PG_RW)) == (PG_M | PG_RW))
				vm_page_dirty(om);
			if ((origpte & PG_A) != 0)
				vm_page_aflag_set(om, PGA_REFERENCED);
			pv = pmap_pvh_remove(&om->md, pmap, va);
			KASSERT(pv != NULL,
			    ("pmap_enter: no PV entry for %#x", va));
			if ((newpte & PG_MANAGED) == 0)
				free_pv_entry(pmap, pv);
			if ((om->aflags & PGA_WRITEABLE) != 0 &&
			    TAILQ_EMPTY(&om->md.pv_list) &&
			    ((om->flags & PG_FICTITIOUS) != 0 ||
			    TAILQ_EMPTY(&pa_to_pvh(opa)->pv_list)))
				vm_page_aflag_clear(om, PGA_WRITEABLE);
		}
		if ((origpte & PG_A) != 0)
			pmap_invalidate_page_int(pmap, va);
		origpte = 0;
	} else {
		/*
		 * Increment the counters.
		 */
		if ((newpte & PG_W) != 0)
			pmap->pm_stats.wired_count++;
		pmap->pm_stats.resident_count++;
	}

	/*
	 * Enter on the PV list if part of our managed memory.
	 */
	if ((newpte & PG_MANAGED) != 0) {
		if (pv == NULL) {
			pv = get_pv_entry(pmap, FALSE);
			pv->pv_va = va;
		}
		TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_next);
		if ((newpte & PG_RW) != 0)
			vm_page_aflag_set(m, PGA_WRITEABLE);
	}

	/*
	 * Update the PTE.
	 */
	if ((origpte & PG_V) != 0) {
validate:
		origpte = pte_load_store(pte, newpte);
		KASSERT((origpte & PG_FRAME) == pa,
		    ("pmap_enter: unexpected pa update for %#x", va));
		if ((newpte & PG_M) == 0 && (origpte & (PG_M | PG_RW)) ==
		    (PG_M | PG_RW)) {
			if ((origpte & PG_MANAGED) != 0)
				vm_page_dirty(m);

			/*
			 * Although the PTE may still have PG_RW set, TLB
			 * invalidation may nonetheless be required because
			 * the PTE no longer has PG_M set.
			 */
		}
#ifdef PMAP_PAE_COMP
		else if ((origpte & PG_NX) != 0 || (newpte & PG_NX) == 0) {
			/*
			 * This PTE change does not require TLB invalidation.
			 */
			goto unchanged;
		}
#endif
		if ((origpte & PG_A) != 0)
			pmap_invalidate_page_int(pmap, va);
	} else
		pte_store_zero(pte, newpte);

unchanged:

#if VM_NRESERVLEVEL > 0
	/*
	 * If both the page table page and the reservation are fully
	 * populated, then attempt promotion.
	 */
	if ((mpte == NULL || mpte->wire_count == NPTEPG) &&
	    pg_ps_enabled && (m->flags & PG_FICTITIOUS) == 0 &&
	    vm_reserv_level_iffullpop(m) == 0)
		pmap_promote_pde(pmap, pde, va);
#endif

	rv = KERN_SUCCESS;
out:
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
	return (rv);
}

/*
 * Tries to create a read- and/or execute-only 2 or 4 MB page mapping.  Returns
 * true if successful.  Returns false if (1) a mapping already exists at the
 * specified virtual address or (2) a PV entry cannot be allocated without
 * reclaiming another PV entry.
 */
static bool
pmap_enter_4mpage(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot)
{
	pd_entry_t newpde;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	newpde = VM_PAGE_TO_PHYS(m) | pmap_cache_bits(pmap, m->md.pat_mode, 1) |
	    PG_PS | PG_V;
	if ((m->oflags & VPO_UNMANAGED) == 0)
		newpde |= PG_MANAGED;
#ifdef PMAP_PAE_COMP
	if ((prot & VM_PROT_EXECUTE) == 0 && !i386_read_exec)
		newpde |= pg_nx;
#endif
	if (pmap != kernel_pmap)
		newpde |= PG_U;
	return (pmap_enter_pde(pmap, va, newpde, PMAP_ENTER_NOSLEEP |
	    PMAP_ENTER_NOREPLACE | PMAP_ENTER_NORECLAIM, NULL) ==
	    KERN_SUCCESS);
}

/*
 * Tries to create the specified 2 or 4 MB page mapping.  Returns KERN_SUCCESS
 * if the mapping was created, and either KERN_FAILURE or
 * KERN_RESOURCE_SHORTAGE otherwise.  Returns KERN_FAILURE if
 * PMAP_ENTER_NOREPLACE was specified and a mapping already exists at the
 * specified virtual address.  Returns KERN_RESOURCE_SHORTAGE if
 * PMAP_ENTER_NORECLAIM was specified and a PV entry allocation failed.
 *
 * The parameter "m" is only used when creating a managed, writeable mapping.
 */
static int
pmap_enter_pde(pmap_t pmap, vm_offset_t va, pd_entry_t newpde, u_int flags,
    vm_page_t m)
{
	struct spglist free;
	pd_entry_t oldpde, *pde;
	vm_page_t mt;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	KASSERT((newpde & (PG_M | PG_RW)) != PG_RW,
	    ("pmap_enter_pde: newpde is missing PG_M"));
	KASSERT(pmap == kernel_pmap || (newpde & PG_W) == 0,
	    ("pmap_enter_pde: cannot create wired user mapping"));
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	pde = pmap_pde(pmap, va);
	oldpde = *pde;
	if ((oldpde & PG_V) != 0) {
		if ((flags & PMAP_ENTER_NOREPLACE) != 0) {
			CTR2(KTR_PMAP, "pmap_enter_pde: failure for va %#lx"
			    " in pmap %p", va, pmap);
			return (KERN_FAILURE);
		}
		/* Break the existing mapping(s). */
		SLIST_INIT(&free);
		if ((oldpde & PG_PS) != 0) {
			/*
			 * If the PDE resulted from a promotion, then a
			 * reserved PT page could be freed.
			 */
			(void)pmap_remove_pde(pmap, pde, va, &free);
			if ((oldpde & PG_G) == 0)
				pmap_invalidate_pde_page(pmap, va, oldpde);
		} else {
			if (pmap_remove_ptes(pmap, va, va + NBPDR, &free))
		               pmap_invalidate_all_int(pmap);
		}
		vm_page_free_pages_toq(&free, true);
		if (pmap == kernel_pmap) {
			mt = PHYS_TO_VM_PAGE(*pde & PG_FRAME);
			if (pmap_insert_pt_page(pmap, mt)) {
				/*
				 * XXX Currently, this can't happen because
				 * we do not perform pmap_enter(psind == 1)
				 * on the kernel pmap.
				 */
				panic("pmap_enter_pde: trie insert failed");
			}
		} else
			KASSERT(*pde == 0, ("pmap_enter_pde: non-zero pde %p",
			    pde));
	}
	if ((newpde & PG_MANAGED) != 0) {
		/*
		 * Abort this mapping if its PV entry could not be created.
		 */
		if (!pmap_pv_insert_pde(pmap, va, newpde, flags)) {
			CTR2(KTR_PMAP, "pmap_enter_pde: failure for va %#lx"
			    " in pmap %p", va, pmap);
			return (KERN_RESOURCE_SHORTAGE);
		}
		if ((newpde & PG_RW) != 0) {
			for (mt = m; mt < &m[NBPDR / PAGE_SIZE]; mt++)
				vm_page_aflag_set(mt, PGA_WRITEABLE);
		}
	}

	/*
	 * Increment counters.
	 */
	if ((newpde & PG_W) != 0)
		pmap->pm_stats.wired_count += NBPDR / PAGE_SIZE;
	pmap->pm_stats.resident_count += NBPDR / PAGE_SIZE;

	/*
	 * Map the superpage.  (This is not a promoted mapping; there will not
	 * be any lingering 4KB page mappings in the TLB.)
	 */
	pde_store(pde, newpde);

	pmap_pde_mappings++;
	CTR2(KTR_PMAP, "pmap_enter_pde: success for va %#lx"
	    " in pmap %p", va, pmap);
	return (KERN_SUCCESS);
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
static void
__CONCAT(PMTYPE, enter_object)(pmap_t pmap, vm_offset_t start, vm_offset_t end,
    vm_page_t m_start, vm_prot_t prot)
{
	vm_offset_t va;
	vm_page_t m, mpte;
	vm_pindex_t diff, psize;

	VM_OBJECT_ASSERT_LOCKED(m_start->object);

	psize = atop(end - start);
	mpte = NULL;
	m = m_start;
	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	while (m != NULL && (diff = m->pindex - m_start->pindex) < psize) {
		va = start + ptoa(diff);
		if ((va & PDRMASK) == 0 && va + NBPDR <= end &&
		    m->psind == 1 && pg_ps_enabled &&
		    pmap_enter_4mpage(pmap, va, m, prot))
			m = &m[NBPDR / PAGE_SIZE - 1];
		else
			mpte = pmap_enter_quick_locked(pmap, va, m, prot,
			    mpte);
		m = TAILQ_NEXT(m, listq);
	}
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
}

/*
 * this code makes some *MAJOR* assumptions:
 * 1. Current pmap & pmap exists.
 * 2. Not wired.
 * 3. Read access.
 * 4. No page table pages.
 * but is *MUCH* faster than pmap_enter...
 */

static void
__CONCAT(PMTYPE, enter_quick)(pmap_t pmap, vm_offset_t va, vm_page_t m,
    vm_prot_t prot)
{

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	(void)pmap_enter_quick_locked(pmap, va, m, prot, NULL);
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
}

static vm_page_t
pmap_enter_quick_locked(pmap_t pmap, vm_offset_t va, vm_page_t m,
    vm_prot_t prot, vm_page_t mpte)
{
	pt_entry_t newpte, *pte;
	struct spglist free;

	KASSERT(pmap != kernel_pmap || va < kmi.clean_sva ||
	    va >= kmi.clean_eva || (m->oflags & VPO_UNMANAGED) != 0,
	    ("pmap_enter_quick_locked: managed mapping within the clean submap"));
	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	/*
	 * In the case that a page table page is not
	 * resident, we are creating it here.
	 */
	if (pmap != kernel_pmap) {
		u_int ptepindex;
		pd_entry_t ptepa;

		/*
		 * Calculate pagetable page index
		 */
		ptepindex = va >> PDRSHIFT;
		if (mpte && (mpte->pindex == ptepindex)) {
			mpte->wire_count++;
		} else {
			/*
			 * Get the page directory entry
			 */
			ptepa = pmap->pm_pdir[ptepindex];

			/*
			 * If the page table page is mapped, we just increment
			 * the hold count, and activate it.
			 */
			if (ptepa) {
				if (ptepa & PG_PS)
					return (NULL);
				mpte = PHYS_TO_VM_PAGE(ptepa & PG_FRAME);
				mpte->wire_count++;
			} else {
				mpte = _pmap_allocpte(pmap, ptepindex,
				    PMAP_ENTER_NOSLEEP);
				if (mpte == NULL)
					return (mpte);
			}
		}
	} else {
		mpte = NULL;
	}

	sched_pin();
	pte = pmap_pte_quick(pmap, va);
	if (*pte) {
		if (mpte != NULL) {
			mpte->wire_count--;
			mpte = NULL;
		}
		sched_unpin();
		return (mpte);
	}

	/*
	 * Enter on the PV list if part of our managed memory.
	 */
	if ((m->oflags & VPO_UNMANAGED) == 0 &&
	    !pmap_try_insert_pv_entry(pmap, va, m)) {
		if (mpte != NULL) {
			SLIST_INIT(&free);
			if (pmap_unwire_ptp(pmap, mpte, &free)) {
				pmap_invalidate_page_int(pmap, va);
				vm_page_free_pages_toq(&free, true);
			}
			
			mpte = NULL;
		}
		sched_unpin();
		return (mpte);
	}

	/*
	 * Increment counters
	 */
	pmap->pm_stats.resident_count++;

	newpte = VM_PAGE_TO_PHYS(m) | PG_V |
	    pmap_cache_bits(pmap, m->md.pat_mode, 0);
	if ((m->oflags & VPO_UNMANAGED) == 0)
		newpte |= PG_MANAGED;
#ifdef PMAP_PAE_COMP
	if ((prot & VM_PROT_EXECUTE) == 0 && !i386_read_exec)
		newpte |= pg_nx;
#endif
	if (pmap != kernel_pmap)
		newpte |= PG_U;
	pte_store_zero(pte, newpte);
	sched_unpin();
	return (mpte);
}

/*
 * Make a temporary mapping for a physical address.  This is only intended
 * to be used for panic dumps.
 */
static void *
__CONCAT(PMTYPE, kenter_temporary)(vm_paddr_t pa, int i)
{
	vm_offset_t va;

	va = (vm_offset_t)crashdumpmap + (i * PAGE_SIZE);
	pmap_kenter(va, pa);
	invlpg(va);
	return ((void *)crashdumpmap);
}

/*
 * This code maps large physical mmap regions into the
 * processor address space.  Note that some shortcuts
 * are taken, but the code works.
 */
static void
__CONCAT(PMTYPE, object_init_pt)(pmap_t pmap, vm_offset_t addr,
    vm_object_t object, vm_pindex_t pindex, vm_size_t size)
{
	pd_entry_t *pde;
	vm_paddr_t pa, ptepa;
	vm_page_t p;
	int pat_mode;

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(object->type == OBJT_DEVICE || object->type == OBJT_SG,
	    ("pmap_object_init_pt: non-device object"));
	if (pg_ps_enabled &&
	    (addr & (NBPDR - 1)) == 0 && (size & (NBPDR - 1)) == 0) {
		if (!vm_object_populate(object, pindex, pindex + atop(size)))
			return;
		p = vm_page_lookup(object, pindex);
		KASSERT(p->valid == VM_PAGE_BITS_ALL,
		    ("pmap_object_init_pt: invalid page %p", p));
		pat_mode = p->md.pat_mode;

		/*
		 * Abort the mapping if the first page is not physically
		 * aligned to a 2/4MB page boundary.
		 */
		ptepa = VM_PAGE_TO_PHYS(p);
		if (ptepa & (NBPDR - 1))
			return;

		/*
		 * Skip the first page.  Abort the mapping if the rest of
		 * the pages are not physically contiguous or have differing
		 * memory attributes.
		 */
		p = TAILQ_NEXT(p, listq);
		for (pa = ptepa + PAGE_SIZE; pa < ptepa + size;
		    pa += PAGE_SIZE) {
			KASSERT(p->valid == VM_PAGE_BITS_ALL,
			    ("pmap_object_init_pt: invalid page %p", p));
			if (pa != VM_PAGE_TO_PHYS(p) ||
			    pat_mode != p->md.pat_mode)
				return;
			p = TAILQ_NEXT(p, listq);
		}

		/*
		 * Map using 2/4MB pages.  Since "ptepa" is 2/4M aligned and
		 * "size" is a multiple of 2/4M, adding the PAT setting to
		 * "pa" will not affect the termination of this loop.
		 */
		PMAP_LOCK(pmap);
		for (pa = ptepa | pmap_cache_bits(pmap, pat_mode, 1);
		    pa < ptepa + size; pa += NBPDR) {
			pde = pmap_pde(pmap, addr);
			if (*pde == 0) {
				pde_store(pde, pa | PG_PS | PG_M | PG_A |
				    PG_U | PG_RW | PG_V);
				pmap->pm_stats.resident_count += NBPDR /
				    PAGE_SIZE;
				pmap_pde_mappings++;
			}
			/* Else continue on if the PDE is already valid. */
			addr += NBPDR;
		}
		PMAP_UNLOCK(pmap);
	}
}

/*
 *	Clear the wired attribute from the mappings for the specified range of
 *	addresses in the given pmap.  Every valid mapping within that range
 *	must have the wired attribute set.  In contrast, invalid mappings
 *	cannot have the wired attribute set, so they are ignored.
 *
 *	The wired attribute of the page table entry is not a hardware feature,
 *	so there is no need to invalidate any TLB entries.
 */
static void
__CONCAT(PMTYPE, unwire)(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	vm_offset_t pdnxt;
	pd_entry_t *pde;
	pt_entry_t *pte;
	boolean_t pv_lists_locked;

	if (pmap_is_current(pmap))
		pv_lists_locked = FALSE;
	else {
		pv_lists_locked = TRUE;
resume:
		rw_wlock(&pvh_global_lock);
		sched_pin();
	}
	PMAP_LOCK(pmap);
	for (; sva < eva; sva = pdnxt) {
		pdnxt = (sva + NBPDR) & ~PDRMASK;
		if (pdnxt < sva)
			pdnxt = eva;
		pde = pmap_pde(pmap, sva);
		if ((*pde & PG_V) == 0)
			continue;
		if ((*pde & PG_PS) != 0) {
			if ((*pde & PG_W) == 0)
				panic("pmap_unwire: pde %#jx is missing PG_W",
				    (uintmax_t)*pde);

			/*
			 * Are we unwiring the entire large page?  If not,
			 * demote the mapping and fall through.
			 */
			if (sva + NBPDR == pdnxt && eva >= pdnxt) {
				/*
				 * Regardless of whether a pde (or pte) is 32
				 * or 64 bits in size, PG_W is among the least
				 * significant 32 bits.
				 */
				atomic_clear_int((u_int *)pde, PG_W);
				pmap->pm_stats.wired_count -= NBPDR /
				    PAGE_SIZE;
				continue;
			} else {
				if (!pv_lists_locked) {
					pv_lists_locked = TRUE;
					if (!rw_try_wlock(&pvh_global_lock)) {
						PMAP_UNLOCK(pmap);
						/* Repeat sva. */
						goto resume;
					}
					sched_pin();
				}
				if (!pmap_demote_pde(pmap, pde, sva))
					panic("pmap_unwire: demotion failed");
			}
		}
		if (pdnxt > eva)
			pdnxt = eva;
		for (pte = pmap_pte_quick(pmap, sva); sva != pdnxt; pte++,
		    sva += PAGE_SIZE) {
			if ((*pte & PG_V) == 0)
				continue;
			if ((*pte & PG_W) == 0)
				panic("pmap_unwire: pte %#jx is missing PG_W",
				    (uintmax_t)*pte);

			/*
			 * PG_W must be cleared atomically.  Although the pmap
			 * lock synchronizes access to PG_W, another processor
			 * could be setting PG_M and/or PG_A concurrently.
			 *
			 * PG_W is among the least significant 32 bits.
			 */
			atomic_clear_int((u_int *)pte, PG_W);
			pmap->pm_stats.wired_count--;
		}
	}
	if (pv_lists_locked) {
		sched_unpin();
		rw_wunlock(&pvh_global_lock);
	}
	PMAP_UNLOCK(pmap);
}


/*
 *	Copy the range specified by src_addr/len
 *	from the source map to the range dst_addr/len
 *	in the destination map.
 *
 *	This routine is only advisory and need not do anything.  Since
 *	current pmap is always the kernel pmap when executing in
 *	kernel, and we do not copy from the kernel pmap to a user
 *	pmap, this optimization is not usable in 4/4G full split i386
 *	world.
 */

static void
__CONCAT(PMTYPE, copy)(pmap_t dst_pmap, pmap_t src_pmap, vm_offset_t dst_addr,
    vm_size_t len, vm_offset_t src_addr)
{
	struct spglist free;
	pt_entry_t *src_pte, *dst_pte, ptetemp;
	pd_entry_t srcptepaddr;
	vm_page_t dstmpte, srcmpte;
	vm_offset_t addr, end_addr, pdnxt;
	u_int ptepindex;

	if (dst_addr != src_addr)
		return;

	end_addr = src_addr + len;

	rw_wlock(&pvh_global_lock);
	if (dst_pmap < src_pmap) {
		PMAP_LOCK(dst_pmap);
		PMAP_LOCK(src_pmap);
	} else {
		PMAP_LOCK(src_pmap);
		PMAP_LOCK(dst_pmap);
	}
	sched_pin();
	for (addr = src_addr; addr < end_addr; addr = pdnxt) {
		KASSERT(addr < PMAP_TRM_MIN_ADDRESS,
		    ("pmap_copy: invalid to pmap_copy the trampoline"));

		pdnxt = (addr + NBPDR) & ~PDRMASK;
		if (pdnxt < addr)
			pdnxt = end_addr;
		ptepindex = addr >> PDRSHIFT;

		srcptepaddr = src_pmap->pm_pdir[ptepindex];
		if (srcptepaddr == 0)
			continue;

		if (srcptepaddr & PG_PS) {
			if ((addr & PDRMASK) != 0 || addr + NBPDR > end_addr)
				continue;
			if (dst_pmap->pm_pdir[ptepindex] == 0 &&
			    ((srcptepaddr & PG_MANAGED) == 0 ||
			    pmap_pv_insert_pde(dst_pmap, addr, srcptepaddr,
			    PMAP_ENTER_NORECLAIM))) {
				dst_pmap->pm_pdir[ptepindex] = srcptepaddr &
				    ~PG_W;
				dst_pmap->pm_stats.resident_count +=
				    NBPDR / PAGE_SIZE;
				pmap_pde_mappings++;
			}
			continue;
		}

		srcmpte = PHYS_TO_VM_PAGE(srcptepaddr & PG_FRAME);
		KASSERT(srcmpte->wire_count > 0,
		    ("pmap_copy: source page table page is unused"));

		if (pdnxt > end_addr)
			pdnxt = end_addr;

		src_pte = pmap_pte_quick3(src_pmap, addr);
		while (addr < pdnxt) {
			ptetemp = *src_pte;
			/*
			 * we only virtual copy managed pages
			 */
			if ((ptetemp & PG_MANAGED) != 0) {
				dstmpte = pmap_allocpte(dst_pmap, addr,
				    PMAP_ENTER_NOSLEEP);
				if (dstmpte == NULL)
					goto out;
				dst_pte = pmap_pte_quick(dst_pmap, addr);
				if (*dst_pte == 0 &&
				    pmap_try_insert_pv_entry(dst_pmap, addr,
				    PHYS_TO_VM_PAGE(ptetemp & PG_FRAME))) {
					/*
					 * Clear the wired, modified, and
					 * accessed (referenced) bits
					 * during the copy.
					 */
					*dst_pte = ptetemp & ~(PG_W | PG_M |
					    PG_A);
					dst_pmap->pm_stats.resident_count++;
				} else {
					SLIST_INIT(&free);
					if (pmap_unwire_ptp(dst_pmap, dstmpte,
					    &free)) {
						pmap_invalidate_page_int(
						    dst_pmap, addr);
						vm_page_free_pages_toq(&free,
						    true);
					}
					goto out;
				}
				if (dstmpte->wire_count >= srcmpte->wire_count)
					break;
			}
			addr += PAGE_SIZE;
			src_pte++;
		}
	}
out:
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(src_pmap);
	PMAP_UNLOCK(dst_pmap);
}

/*
 * Zero 1 page of virtual memory mapped from a hardware page by the caller.
 */
static __inline void
pagezero(void *page)
{
#if defined(I686_CPU)
	if (cpu_class == CPUCLASS_686) {
		if (cpu_feature & CPUID_SSE2)
			sse2_pagezero(page);
		else
			i686_pagezero(page);
	} else
#endif
		bzero(page, PAGE_SIZE);
}

/*
 * Zero the specified hardware page.
 */
static void
__CONCAT(PMTYPE, zero_page)(vm_page_t m)
{
	pt_entry_t *cmap_pte2;
	struct pcpu *pc;

	sched_pin();
	pc = get_pcpu();
	cmap_pte2 = pc->pc_cmap_pte2;
	mtx_lock(&pc->pc_cmap_lock);
	if (*cmap_pte2)
		panic("pmap_zero_page: CMAP2 busy");
	*cmap_pte2 = PG_V | PG_RW | VM_PAGE_TO_PHYS(m) | PG_A | PG_M |
	    pmap_cache_bits(kernel_pmap, m->md.pat_mode, 0);
	invlcaddr(pc->pc_cmap_addr2);
	pagezero(pc->pc_cmap_addr2);
	*cmap_pte2 = 0;

	/*
	 * Unpin the thread before releasing the lock.  Otherwise the thread
	 * could be rescheduled while still bound to the current CPU, only
	 * to unpin itself immediately upon resuming execution.
	 */
	sched_unpin();
	mtx_unlock(&pc->pc_cmap_lock);
}

/*
 * Zero an an area within a single hardware page.  off and size must not
 * cover an area beyond a single hardware page.
 */
static void
__CONCAT(PMTYPE, zero_page_area)(vm_page_t m, int off, int size)
{
	pt_entry_t *cmap_pte2;
	struct pcpu *pc;

	sched_pin();
	pc = get_pcpu();
	cmap_pte2 = pc->pc_cmap_pte2;
	mtx_lock(&pc->pc_cmap_lock);
	if (*cmap_pte2)
		panic("pmap_zero_page_area: CMAP2 busy");
	*cmap_pte2 = PG_V | PG_RW | VM_PAGE_TO_PHYS(m) | PG_A | PG_M |
	    pmap_cache_bits(kernel_pmap, m->md.pat_mode, 0);
	invlcaddr(pc->pc_cmap_addr2);
	if (off == 0 && size == PAGE_SIZE) 
		pagezero(pc->pc_cmap_addr2);
	else
		bzero(pc->pc_cmap_addr2 + off, size);
	*cmap_pte2 = 0;
	sched_unpin();
	mtx_unlock(&pc->pc_cmap_lock);
}

/*
 * Copy 1 specified hardware page to another.
 */
static void
__CONCAT(PMTYPE, copy_page)(vm_page_t src, vm_page_t dst)
{
	pt_entry_t *cmap_pte1, *cmap_pte2;
	struct pcpu *pc;

	sched_pin();
	pc = get_pcpu();
	cmap_pte1 = pc->pc_cmap_pte1; 
	cmap_pte2 = pc->pc_cmap_pte2;
	mtx_lock(&pc->pc_cmap_lock);
	if (*cmap_pte1)
		panic("pmap_copy_page: CMAP1 busy");
	if (*cmap_pte2)
		panic("pmap_copy_page: CMAP2 busy");
	*cmap_pte1 = PG_V | VM_PAGE_TO_PHYS(src) | PG_A |
	    pmap_cache_bits(kernel_pmap, src->md.pat_mode, 0);
	invlcaddr(pc->pc_cmap_addr1);
	*cmap_pte2 = PG_V | PG_RW | VM_PAGE_TO_PHYS(dst) | PG_A | PG_M |
	    pmap_cache_bits(kernel_pmap, dst->md.pat_mode, 0);
	invlcaddr(pc->pc_cmap_addr2);
	bcopy(pc->pc_cmap_addr1, pc->pc_cmap_addr2, PAGE_SIZE);
	*cmap_pte1 = 0;
	*cmap_pte2 = 0;
	sched_unpin();
	mtx_unlock(&pc->pc_cmap_lock);
}

static void
__CONCAT(PMTYPE, copy_pages)(vm_page_t ma[], vm_offset_t a_offset,
    vm_page_t mb[], vm_offset_t b_offset, int xfersize)
{
	vm_page_t a_pg, b_pg;
	char *a_cp, *b_cp;
	vm_offset_t a_pg_offset, b_pg_offset;
	pt_entry_t *cmap_pte1, *cmap_pte2;
	struct pcpu *pc;
	int cnt;

	sched_pin();
	pc = get_pcpu();
	cmap_pte1 = pc->pc_cmap_pte1; 
	cmap_pte2 = pc->pc_cmap_pte2;
	mtx_lock(&pc->pc_cmap_lock);
	if (*cmap_pte1 != 0)
		panic("pmap_copy_pages: CMAP1 busy");
	if (*cmap_pte2 != 0)
		panic("pmap_copy_pages: CMAP2 busy");
	while (xfersize > 0) {
		a_pg = ma[a_offset >> PAGE_SHIFT];
		a_pg_offset = a_offset & PAGE_MASK;
		cnt = min(xfersize, PAGE_SIZE - a_pg_offset);
		b_pg = mb[b_offset >> PAGE_SHIFT];
		b_pg_offset = b_offset & PAGE_MASK;
		cnt = min(cnt, PAGE_SIZE - b_pg_offset);
		*cmap_pte1 = PG_V | VM_PAGE_TO_PHYS(a_pg) | PG_A |
		    pmap_cache_bits(kernel_pmap, a_pg->md.pat_mode, 0);
		invlcaddr(pc->pc_cmap_addr1);
		*cmap_pte2 = PG_V | PG_RW | VM_PAGE_TO_PHYS(b_pg) | PG_A |
		    PG_M | pmap_cache_bits(kernel_pmap, b_pg->md.pat_mode, 0);
		invlcaddr(pc->pc_cmap_addr2);
		a_cp = pc->pc_cmap_addr1 + a_pg_offset;
		b_cp = pc->pc_cmap_addr2 + b_pg_offset;
		bcopy(a_cp, b_cp, cnt);
		a_offset += cnt;
		b_offset += cnt;
		xfersize -= cnt;
	}
	*cmap_pte1 = 0;
	*cmap_pte2 = 0;
	sched_unpin();
	mtx_unlock(&pc->pc_cmap_lock);
}

/*
 * Returns true if the pmap's pv is one of the first
 * 16 pvs linked to from this page.  This count may
 * be changed upwards or downwards in the future; it
 * is only necessary that true be returned for a small
 * subset of pmaps for proper page aging.
 */
static boolean_t
__CONCAT(PMTYPE, page_exists_quick)(pmap_t pmap, vm_page_t m)
{
	struct md_page *pvh;
	pv_entry_t pv;
	int loops = 0;
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_page_exists_quick: page %p is not managed", m));
	rv = FALSE;
	rw_wlock(&pvh_global_lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_next) {
		if (PV_PMAP(pv) == pmap) {
			rv = TRUE;
			break;
		}
		loops++;
		if (loops >= 16)
			break;
	}
	if (!rv && loops < 16 && (m->flags & PG_FICTITIOUS) == 0) {
		pvh = pa_to_pvh(VM_PAGE_TO_PHYS(m));
		TAILQ_FOREACH(pv, &pvh->pv_list, pv_next) {
			if (PV_PMAP(pv) == pmap) {
				rv = TRUE;
				break;
			}
			loops++;
			if (loops >= 16)
				break;
		}
	}
	rw_wunlock(&pvh_global_lock);
	return (rv);
}

/*
 *	pmap_page_wired_mappings:
 *
 *	Return the number of managed mappings to the given physical page
 *	that are wired.
 */
static int
__CONCAT(PMTYPE, page_wired_mappings)(vm_page_t m)
{
	int count;

	count = 0;
	if ((m->oflags & VPO_UNMANAGED) != 0)
		return (count);
	rw_wlock(&pvh_global_lock);
	count = pmap_pvh_wired_mappings(&m->md, count);
	if ((m->flags & PG_FICTITIOUS) == 0) {
	    count = pmap_pvh_wired_mappings(pa_to_pvh(VM_PAGE_TO_PHYS(m)),
	        count);
	}
	rw_wunlock(&pvh_global_lock);
	return (count);
}

/*
 *	pmap_pvh_wired_mappings:
 *
 *	Return the updated number "count" of managed mappings that are wired.
 */
static int
pmap_pvh_wired_mappings(struct md_page *pvh, int count)
{
	pmap_t pmap;
	pt_entry_t *pte;
	pv_entry_t pv;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	sched_pin();
	TAILQ_FOREACH(pv, &pvh->pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pte = pmap_pte_quick(pmap, pv->pv_va);
		if ((*pte & PG_W) != 0)
			count++;
		PMAP_UNLOCK(pmap);
	}
	sched_unpin();
	return (count);
}

/*
 * Returns TRUE if the given page is mapped individually or as part of
 * a 4mpage.  Otherwise, returns FALSE.
 */
static boolean_t
__CONCAT(PMTYPE, page_is_mapped)(vm_page_t m)
{
	boolean_t rv;

	if ((m->oflags & VPO_UNMANAGED) != 0)
		return (FALSE);
	rw_wlock(&pvh_global_lock);
	rv = !TAILQ_EMPTY(&m->md.pv_list) ||
	    ((m->flags & PG_FICTITIOUS) == 0 &&
	    !TAILQ_EMPTY(&pa_to_pvh(VM_PAGE_TO_PHYS(m))->pv_list));
	rw_wunlock(&pvh_global_lock);
	return (rv);
}

/*
 * Remove all pages from specified address space
 * this aids process exit speeds.  Also, this code
 * is special cased for current process only, but
 * can have the more generic (and slightly slower)
 * mode enabled.  This is much faster than pmap_remove
 * in the case of running down an entire address space.
 */
static void
__CONCAT(PMTYPE, remove_pages)(pmap_t pmap)
{
	pt_entry_t *pte, tpte;
	vm_page_t m, mpte, mt;
	pv_entry_t pv;
	struct md_page *pvh;
	struct pv_chunk *pc, *npc;
	struct spglist free;
	int field, idx;
	int32_t bit;
	uint32_t inuse, bitmask;
	int allfree;

	if (pmap != PCPU_GET(curpmap)) {
		printf("warning: pmap_remove_pages called with non-current pmap\n");
		return;
	}
	SLIST_INIT(&free);
	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	sched_pin();
	TAILQ_FOREACH_SAFE(pc, &pmap->pm_pvchunk, pc_list, npc) {
		KASSERT(pc->pc_pmap == pmap, ("Wrong pmap %p %p", pmap,
		    pc->pc_pmap));
		allfree = 1;
		for (field = 0; field < _NPCM; field++) {
			inuse = ~pc->pc_map[field] & pc_freemask[field];
			while (inuse != 0) {
				bit = bsfl(inuse);
				bitmask = 1UL << bit;
				idx = field * 32 + bit;
				pv = &pc->pc_pventry[idx];
				inuse &= ~bitmask;

				pte = pmap_pde(pmap, pv->pv_va);
				tpte = *pte;
				if ((tpte & PG_PS) == 0) {
					pte = pmap_pte_quick(pmap, pv->pv_va);
					tpte = *pte & ~PG_PTE_PAT;
				}

				if (tpte == 0) {
					printf(
					    "TPTE at %p  IS ZERO @ VA %08x\n",
					    pte, pv->pv_va);
					panic("bad pte");
				}

/*
 * We cannot remove wired pages from a process' mapping at this time
 */
				if (tpte & PG_W) {
					allfree = 0;
					continue;
				}

				m = PHYS_TO_VM_PAGE(tpte & PG_FRAME);
				KASSERT(m->phys_addr == (tpte & PG_FRAME),
				    ("vm_page_t %p phys_addr mismatch %016jx %016jx",
				    m, (uintmax_t)m->phys_addr,
				    (uintmax_t)tpte));

				KASSERT((m->flags & PG_FICTITIOUS) != 0 ||
				    m < &vm_page_array[vm_page_array_size],
				    ("pmap_remove_pages: bad tpte %#jx",
				    (uintmax_t)tpte));

				pte_clear(pte);

				/*
				 * Update the vm_page_t clean/reference bits.
				 */
				if ((tpte & (PG_M | PG_RW)) == (PG_M | PG_RW)) {
					if ((tpte & PG_PS) != 0) {
						for (mt = m; mt < &m[NBPDR / PAGE_SIZE]; mt++)
							vm_page_dirty(mt);
					} else
						vm_page_dirty(m);
				}

				/* Mark free */
				PV_STAT(pv_entry_frees++);
				PV_STAT(pv_entry_spare++);
				pv_entry_count--;
				pc->pc_map[field] |= bitmask;
				if ((tpte & PG_PS) != 0) {
					pmap->pm_stats.resident_count -= NBPDR / PAGE_SIZE;
					pvh = pa_to_pvh(tpte & PG_PS_FRAME);
					TAILQ_REMOVE(&pvh->pv_list, pv, pv_next);
					if (TAILQ_EMPTY(&pvh->pv_list)) {
						for (mt = m; mt < &m[NBPDR / PAGE_SIZE]; mt++)
							if (TAILQ_EMPTY(&mt->md.pv_list))
								vm_page_aflag_clear(mt, PGA_WRITEABLE);
					}
					mpte = pmap_remove_pt_page(pmap, pv->pv_va);
					if (mpte != NULL) {
						pmap->pm_stats.resident_count--;
						KASSERT(mpte->wire_count == NPTEPG,
						    ("pmap_remove_pages: pte page wire count error"));
						mpte->wire_count = 0;
						pmap_add_delayed_free_list(mpte, &free, FALSE);
					}
				} else {
					pmap->pm_stats.resident_count--;
					TAILQ_REMOVE(&m->md.pv_list, pv, pv_next);
					if (TAILQ_EMPTY(&m->md.pv_list) &&
					    (m->flags & PG_FICTITIOUS) == 0) {
						pvh = pa_to_pvh(VM_PAGE_TO_PHYS(m));
						if (TAILQ_EMPTY(&pvh->pv_list))
							vm_page_aflag_clear(m, PGA_WRITEABLE);
					}
					pmap_unuse_pt(pmap, pv->pv_va, &free);
				}
			}
		}
		if (allfree) {
			TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
			free_pv_chunk(pc);
		}
	}
	sched_unpin();
	pmap_invalidate_all_int(pmap);
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
	vm_page_free_pages_toq(&free, true);
}

/*
 *	pmap_is_modified:
 *
 *	Return whether or not the specified physical page was modified
 *	in any physical maps.
 */
static boolean_t
__CONCAT(PMTYPE, is_modified)(vm_page_t m)
{
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_is_modified: page %p is not managed", m));

	/*
	 * If the page is not exclusive busied, then PGA_WRITEABLE cannot be
	 * concurrently set while the object is locked.  Thus, if PGA_WRITEABLE
	 * is clear, no PTEs can have PG_M set.
	 */
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (!vm_page_xbusied(m) && (m->aflags & PGA_WRITEABLE) == 0)
		return (FALSE);
	rw_wlock(&pvh_global_lock);
	rv = pmap_is_modified_pvh(&m->md) ||
	    ((m->flags & PG_FICTITIOUS) == 0 &&
	    pmap_is_modified_pvh(pa_to_pvh(VM_PAGE_TO_PHYS(m))));
	rw_wunlock(&pvh_global_lock);
	return (rv);
}

/*
 * Returns TRUE if any of the given mappings were used to modify
 * physical memory.  Otherwise, returns FALSE.  Both page and 2mpage
 * mappings are supported.
 */
static boolean_t
pmap_is_modified_pvh(struct md_page *pvh)
{
	pv_entry_t pv;
	pt_entry_t *pte;
	pmap_t pmap;
	boolean_t rv;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	rv = FALSE;
	sched_pin();
	TAILQ_FOREACH(pv, &pvh->pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pte = pmap_pte_quick(pmap, pv->pv_va);
		rv = (*pte & (PG_M | PG_RW)) == (PG_M | PG_RW);
		PMAP_UNLOCK(pmap);
		if (rv)
			break;
	}
	sched_unpin();
	return (rv);
}

/*
 *	pmap_is_prefaultable:
 *
 *	Return whether or not the specified virtual address is elgible
 *	for prefault.
 */
static boolean_t
__CONCAT(PMTYPE, is_prefaultable)(pmap_t pmap, vm_offset_t addr)
{
	pd_entry_t pde;
	boolean_t rv;

	rv = FALSE;
	PMAP_LOCK(pmap);
	pde = *pmap_pde(pmap, addr);
	if (pde != 0 && (pde & PG_PS) == 0)
		rv = pmap_pte_ufast(pmap, addr, pde) == 0;
	PMAP_UNLOCK(pmap);
	return (rv);
}

/*
 *	pmap_is_referenced:
 *
 *	Return whether or not the specified physical page was referenced
 *	in any physical maps.
 */
static boolean_t
__CONCAT(PMTYPE, is_referenced)(vm_page_t m)
{
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_is_referenced: page %p is not managed", m));
	rw_wlock(&pvh_global_lock);
	rv = pmap_is_referenced_pvh(&m->md) ||
	    ((m->flags & PG_FICTITIOUS) == 0 &&
	    pmap_is_referenced_pvh(pa_to_pvh(VM_PAGE_TO_PHYS(m))));
	rw_wunlock(&pvh_global_lock);
	return (rv);
}

/*
 * Returns TRUE if any of the given mappings were referenced and FALSE
 * otherwise.  Both page and 4mpage mappings are supported.
 */
static boolean_t
pmap_is_referenced_pvh(struct md_page *pvh)
{
	pv_entry_t pv;
	pt_entry_t *pte;
	pmap_t pmap;
	boolean_t rv;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	rv = FALSE;
	sched_pin();
	TAILQ_FOREACH(pv, &pvh->pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pte = pmap_pte_quick(pmap, pv->pv_va);
		rv = (*pte & (PG_A | PG_V)) == (PG_A | PG_V);
		PMAP_UNLOCK(pmap);
		if (rv)
			break;
	}
	sched_unpin();
	return (rv);
}

/*
 * Clear the write and modified bits in each of the given page's mappings.
 */
static void
__CONCAT(PMTYPE, remove_write)(vm_page_t m)
{
	struct md_page *pvh;
	pv_entry_t next_pv, pv;
	pmap_t pmap;
	pd_entry_t *pde;
	pt_entry_t oldpte, *pte;
	vm_offset_t va;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_remove_write: page %p is not managed", m));

	/*
	 * If the page is not exclusive busied, then PGA_WRITEABLE cannot be
	 * set by another thread while the object is locked.  Thus,
	 * if PGA_WRITEABLE is clear, no page table entries need updating.
	 */
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (!vm_page_xbusied(m) && (m->aflags & PGA_WRITEABLE) == 0)
		return;
	rw_wlock(&pvh_global_lock);
	sched_pin();
	if ((m->flags & PG_FICTITIOUS) != 0)
		goto small_mappings;
	pvh = pa_to_pvh(VM_PAGE_TO_PHYS(m));
	TAILQ_FOREACH_SAFE(pv, &pvh->pv_list, pv_next, next_pv) {
		va = pv->pv_va;
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pde = pmap_pde(pmap, va);
		if ((*pde & PG_RW) != 0)
			(void)pmap_demote_pde(pmap, pde, va);
		PMAP_UNLOCK(pmap);
	}
small_mappings:
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pde = pmap_pde(pmap, pv->pv_va);
		KASSERT((*pde & PG_PS) == 0, ("pmap_clear_write: found"
		    " a 4mpage in page %p's pv list", m));
		pte = pmap_pte_quick(pmap, pv->pv_va);
retry:
		oldpte = *pte;
		if ((oldpte & PG_RW) != 0) {
			/*
			 * Regardless of whether a pte is 32 or 64 bits
			 * in size, PG_RW and PG_M are among the least
			 * significant 32 bits.
			 */
			if (!atomic_cmpset_int((u_int *)pte, oldpte,
			    oldpte & ~(PG_RW | PG_M)))
				goto retry;
			if ((oldpte & PG_M) != 0)
				vm_page_dirty(m);
			pmap_invalidate_page_int(pmap, pv->pv_va);
		}
		PMAP_UNLOCK(pmap);
	}
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
}

/*
 *	pmap_ts_referenced:
 *
 *	Return a count of reference bits for a page, clearing those bits.
 *	It is not necessary for every reference bit to be cleared, but it
 *	is necessary that 0 only be returned when there are truly no
 *	reference bits set.
 *
 *	As an optimization, update the page's dirty field if a modified bit is
 *	found while counting reference bits.  This opportunistic update can be
 *	performed at low cost and can eliminate the need for some future calls
 *	to pmap_is_modified().  However, since this function stops after
 *	finding PMAP_TS_REFERENCED_MAX reference bits, it may not detect some
 *	dirty pages.  Those dirty pages will only be detected by a future call
 *	to pmap_is_modified().
 */
static int
__CONCAT(PMTYPE, ts_referenced)(vm_page_t m)
{
	struct md_page *pvh;
	pv_entry_t pv, pvf;
	pmap_t pmap;
	pd_entry_t *pde;
	pt_entry_t *pte;
	vm_paddr_t pa;
	int rtval = 0;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_ts_referenced: page %p is not managed", m));
	pa = VM_PAGE_TO_PHYS(m);
	pvh = pa_to_pvh(pa);
	rw_wlock(&pvh_global_lock);
	sched_pin();
	if ((m->flags & PG_FICTITIOUS) != 0 ||
	    (pvf = TAILQ_FIRST(&pvh->pv_list)) == NULL)
		goto small_mappings;
	pv = pvf;
	do {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pde = pmap_pde(pmap, pv->pv_va);
		if ((*pde & (PG_M | PG_RW)) == (PG_M | PG_RW)) {
			/*
			 * Although "*pde" is mapping a 2/4MB page, because
			 * this function is called at a 4KB page granularity,
			 * we only update the 4KB page under test.
			 */
			vm_page_dirty(m);
		}
		if ((*pde & PG_A) != 0) {
			/*
			 * Since this reference bit is shared by either 1024
			 * or 512 4KB pages, it should not be cleared every
			 * time it is tested.  Apply a simple "hash" function
			 * on the physical page number, the virtual superpage
			 * number, and the pmap address to select one 4KB page
			 * out of the 1024 or 512 on which testing the
			 * reference bit will result in clearing that bit.
			 * This function is designed to avoid the selection of
			 * the same 4KB page for every 2- or 4MB page mapping.
			 *
			 * On demotion, a mapping that hasn't been referenced
			 * is simply destroyed.  To avoid the possibility of a
			 * subsequent page fault on a demoted wired mapping,
			 * always leave its reference bit set.  Moreover,
			 * since the superpage is wired, the current state of
			 * its reference bit won't affect page replacement.
			 */
			if ((((pa >> PAGE_SHIFT) ^ (pv->pv_va >> PDRSHIFT) ^
			    (uintptr_t)pmap) & (NPTEPG - 1)) == 0 &&
			    (*pde & PG_W) == 0) {
				atomic_clear_int((u_int *)pde, PG_A);
				pmap_invalidate_page_int(pmap, pv->pv_va);
			}
			rtval++;
		}
		PMAP_UNLOCK(pmap);
		/* Rotate the PV list if it has more than one entry. */
		if (TAILQ_NEXT(pv, pv_next) != NULL) {
			TAILQ_REMOVE(&pvh->pv_list, pv, pv_next);
			TAILQ_INSERT_TAIL(&pvh->pv_list, pv, pv_next);
		}
		if (rtval >= PMAP_TS_REFERENCED_MAX)
			goto out;
	} while ((pv = TAILQ_FIRST(&pvh->pv_list)) != pvf);
small_mappings:
	if ((pvf = TAILQ_FIRST(&m->md.pv_list)) == NULL)
		goto out;
	pv = pvf;
	do {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pde = pmap_pde(pmap, pv->pv_va);
		KASSERT((*pde & PG_PS) == 0,
		    ("pmap_ts_referenced: found a 4mpage in page %p's pv list",
		    m));
		pte = pmap_pte_quick(pmap, pv->pv_va);
		if ((*pte & (PG_M | PG_RW)) == (PG_M | PG_RW))
			vm_page_dirty(m);
		if ((*pte & PG_A) != 0) {
			atomic_clear_int((u_int *)pte, PG_A);
			pmap_invalidate_page_int(pmap, pv->pv_va);
			rtval++;
		}
		PMAP_UNLOCK(pmap);
		/* Rotate the PV list if it has more than one entry. */
		if (TAILQ_NEXT(pv, pv_next) != NULL) {
			TAILQ_REMOVE(&m->md.pv_list, pv, pv_next);
			TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_next);
		}
	} while ((pv = TAILQ_FIRST(&m->md.pv_list)) != pvf && rtval <
	    PMAP_TS_REFERENCED_MAX);
out:
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
	return (rtval);
}

/*
 *	Apply the given advice to the specified range of addresses within the
 *	given pmap.  Depending on the advice, clear the referenced and/or
 *	modified flags in each mapping and set the mapped page's dirty field.
 */
static void
__CONCAT(PMTYPE, advise)(pmap_t pmap, vm_offset_t sva, vm_offset_t eva,
    int advice)
{
	pd_entry_t oldpde, *pde;
	pt_entry_t *pte;
	vm_offset_t va, pdnxt;
	vm_page_t m;
	boolean_t anychanged, pv_lists_locked;

	if (advice != MADV_DONTNEED && advice != MADV_FREE)
		return;
	if (pmap_is_current(pmap))
		pv_lists_locked = FALSE;
	else {
		pv_lists_locked = TRUE;
resume:
		rw_wlock(&pvh_global_lock);
		sched_pin();
	}
	anychanged = FALSE;
	PMAP_LOCK(pmap);
	for (; sva < eva; sva = pdnxt) {
		pdnxt = (sva + NBPDR) & ~PDRMASK;
		if (pdnxt < sva)
			pdnxt = eva;
		pde = pmap_pde(pmap, sva);
		oldpde = *pde;
		if ((oldpde & PG_V) == 0)
			continue;
		else if ((oldpde & PG_PS) != 0) {
			if ((oldpde & PG_MANAGED) == 0)
				continue;
			if (!pv_lists_locked) {
				pv_lists_locked = TRUE;
				if (!rw_try_wlock(&pvh_global_lock)) {
					if (anychanged)
						pmap_invalidate_all_int(pmap);
					PMAP_UNLOCK(pmap);
					goto resume;
				}
				sched_pin();
			}
			if (!pmap_demote_pde(pmap, pde, sva)) {
				/*
				 * The large page mapping was destroyed.
				 */
				continue;
			}

			/*
			 * Unless the page mappings are wired, remove the
			 * mapping to a single page so that a subsequent
			 * access may repromote.  Since the underlying page
			 * table page is fully populated, this removal never
			 * frees a page table page.
			 */
			if ((oldpde & PG_W) == 0) {
				pte = pmap_pte_quick(pmap, sva);
				KASSERT((*pte & PG_V) != 0,
				    ("pmap_advise: invalid PTE"));
				pmap_remove_pte(pmap, pte, sva, NULL);
				anychanged = TRUE;
			}
		}
		if (pdnxt > eva)
			pdnxt = eva;
		va = pdnxt;
		for (pte = pmap_pte_quick(pmap, sva); sva != pdnxt; pte++,
		    sva += PAGE_SIZE) {
			if ((*pte & (PG_MANAGED | PG_V)) != (PG_MANAGED | PG_V))
				goto maybe_invlrng;
			else if ((*pte & (PG_M | PG_RW)) == (PG_M | PG_RW)) {
				if (advice == MADV_DONTNEED) {
					/*
					 * Future calls to pmap_is_modified()
					 * can be avoided by making the page
					 * dirty now.
					 */
					m = PHYS_TO_VM_PAGE(*pte & PG_FRAME);
					vm_page_dirty(m);
				}
				atomic_clear_int((u_int *)pte, PG_M | PG_A);
			} else if ((*pte & PG_A) != 0)
				atomic_clear_int((u_int *)pte, PG_A);
			else
				goto maybe_invlrng;
			if ((*pte & PG_G) != 0) {
				if (va == pdnxt)
					va = sva;
			} else
				anychanged = TRUE;
			continue;
maybe_invlrng:
			if (va != pdnxt) {
				pmap_invalidate_range_int(pmap, va, sva);
				va = pdnxt;
			}
		}
		if (va != pdnxt)
			pmap_invalidate_range_int(pmap, va, sva);
	}
	if (anychanged)
		pmap_invalidate_all_int(pmap);
	if (pv_lists_locked) {
		sched_unpin();
		rw_wunlock(&pvh_global_lock);
	}
	PMAP_UNLOCK(pmap);
}

/*
 *	Clear the modify bits on the specified physical page.
 */
static void
__CONCAT(PMTYPE, clear_modify)(vm_page_t m)
{
	struct md_page *pvh;
	pv_entry_t next_pv, pv;
	pmap_t pmap;
	pd_entry_t oldpde, *pde;
	pt_entry_t oldpte, *pte;
	vm_offset_t va;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_clear_modify: page %p is not managed", m));
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	KASSERT(!vm_page_xbusied(m),
	    ("pmap_clear_modify: page %p is exclusive busied", m));

	/*
	 * If the page is not PGA_WRITEABLE, then no PTEs can have PG_M set.
	 * If the object containing the page is locked and the page is not
	 * exclusive busied, then PGA_WRITEABLE cannot be concurrently set.
	 */
	if ((m->aflags & PGA_WRITEABLE) == 0)
		return;
	rw_wlock(&pvh_global_lock);
	sched_pin();
	if ((m->flags & PG_FICTITIOUS) != 0)
		goto small_mappings;
	pvh = pa_to_pvh(VM_PAGE_TO_PHYS(m));
	TAILQ_FOREACH_SAFE(pv, &pvh->pv_list, pv_next, next_pv) {
		va = pv->pv_va;
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pde = pmap_pde(pmap, va);
		oldpde = *pde;
		if ((oldpde & PG_RW) != 0) {
			if (pmap_demote_pde(pmap, pde, va)) {
				if ((oldpde & PG_W) == 0) {
					/*
					 * Write protect the mapping to a
					 * single page so that a subsequent
					 * write access may repromote.
					 */
					va += VM_PAGE_TO_PHYS(m) - (oldpde &
					    PG_PS_FRAME);
					pte = pmap_pte_quick(pmap, va);
					oldpte = *pte;
					if ((oldpte & PG_V) != 0) {
						/*
						 * Regardless of whether a pte is 32 or 64 bits
						 * in size, PG_RW and PG_M are among the least
						 * significant 32 bits.
						 */
						while (!atomic_cmpset_int((u_int *)pte,
						    oldpte,
						    oldpte & ~(PG_M | PG_RW)))
							oldpte = *pte;
						vm_page_dirty(m);
						pmap_invalidate_page_int(pmap,
						    va);
					}
				}
			}
		}
		PMAP_UNLOCK(pmap);
	}
small_mappings:
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pde = pmap_pde(pmap, pv->pv_va);
		KASSERT((*pde & PG_PS) == 0, ("pmap_clear_modify: found"
		    " a 4mpage in page %p's pv list", m));
		pte = pmap_pte_quick(pmap, pv->pv_va);
		if ((*pte & (PG_M | PG_RW)) == (PG_M | PG_RW)) {
			/*
			 * Regardless of whether a pte is 32 or 64 bits
			 * in size, PG_M is among the least significant
			 * 32 bits. 
			 */
			atomic_clear_int((u_int *)pte, PG_M);
			pmap_invalidate_page_int(pmap, pv->pv_va);
		}
		PMAP_UNLOCK(pmap);
	}
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
}

/*
 * Miscellaneous support routines follow
 */

/* Adjust the cache mode for a 4KB page mapped via a PTE. */
static __inline void
pmap_pte_attr(pt_entry_t *pte, int cache_bits)
{
	u_int opte, npte;

	/*
	 * The cache mode bits are all in the low 32-bits of the
	 * PTE, so we can just spin on updating the low 32-bits.
	 */
	do {
		opte = *(u_int *)pte;
		npte = opte & ~PG_PTE_CACHE;
		npte |= cache_bits;
	} while (npte != opte && !atomic_cmpset_int((u_int *)pte, opte, npte));
}

/* Adjust the cache mode for a 2/4MB page mapped via a PDE. */
static __inline void
pmap_pde_attr(pd_entry_t *pde, int cache_bits)
{
	u_int opde, npde;

	/*
	 * The cache mode bits are all in the low 32-bits of the
	 * PDE, so we can just spin on updating the low 32-bits.
	 */
	do {
		opde = *(u_int *)pde;
		npde = opde & ~PG_PDE_CACHE;
		npde |= cache_bits;
	} while (npde != opde && !atomic_cmpset_int((u_int *)pde, opde, npde));
}

/*
 * Map a set of physical memory pages into the kernel virtual
 * address space. Return a pointer to where it is mapped. This
 * routine is intended to be used for mapping device memory,
 * NOT real memory.
 */
static void *
__CONCAT(PMTYPE, mapdev_attr)(vm_paddr_t pa, vm_size_t size, int mode)
{
	struct pmap_preinit_mapping *ppim;
	vm_offset_t va, offset;
	vm_size_t tmpsize;
	int i;

	offset = pa & PAGE_MASK;
	size = round_page(offset + size);
	pa = pa & PG_FRAME;

	if (pa < PMAP_MAP_LOW && pa + size <= PMAP_MAP_LOW)
		va = pa + PMAP_MAP_LOW;
	else if (!pmap_initialized) {
		va = 0;
		for (i = 0; i < PMAP_PREINIT_MAPPING_COUNT; i++) {
			ppim = pmap_preinit_mapping + i;
			if (ppim->va == 0) {
				ppim->pa = pa;
				ppim->sz = size;
				ppim->mode = mode;
				ppim->va = virtual_avail;
				virtual_avail += size;
				va = ppim->va;
				break;
			}
		}
		if (va == 0)
			panic("%s: too many preinit mappings", __func__);
	} else {
		/*
		 * If we have a preinit mapping, re-use it.
		 */
		for (i = 0; i < PMAP_PREINIT_MAPPING_COUNT; i++) {
			ppim = pmap_preinit_mapping + i;
			if (ppim->pa == pa && ppim->sz == size &&
			    ppim->mode == mode)
				return ((void *)(ppim->va + offset));
		}
		va = kva_alloc(size);
		if (va == 0)
			panic("%s: Couldn't allocate KVA", __func__);
	}
	for (tmpsize = 0; tmpsize < size; tmpsize += PAGE_SIZE)
		pmap_kenter_attr(va + tmpsize, pa + tmpsize, mode);
	pmap_invalidate_range_int(kernel_pmap, va, va + tmpsize);
	pmap_invalidate_cache_range(va, va + size);
	return ((void *)(va + offset));
}

static void
__CONCAT(PMTYPE, unmapdev)(vm_offset_t va, vm_size_t size)
{
	struct pmap_preinit_mapping *ppim;
	vm_offset_t offset;
	int i;

	if (va >= PMAP_MAP_LOW && va <= KERNBASE && va + size <= KERNBASE)
		return;
	offset = va & PAGE_MASK;
	size = round_page(offset + size);
	va = trunc_page(va);
	for (i = 0; i < PMAP_PREINIT_MAPPING_COUNT; i++) {
		ppim = pmap_preinit_mapping + i;
		if (ppim->va == va && ppim->sz == size) {
			if (pmap_initialized)
				return;
			ppim->pa = 0;
			ppim->va = 0;
			ppim->sz = 0;
			ppim->mode = 0;
			if (va + size == virtual_avail)
				virtual_avail = va;
			return;
		}
	}
	if (pmap_initialized)
		kva_free(va, size);
}

/*
 * Sets the memory attribute for the specified page.
 */
static void
__CONCAT(PMTYPE, page_set_memattr)(vm_page_t m, vm_memattr_t ma)
{

	m->md.pat_mode = ma;
	if ((m->flags & PG_FICTITIOUS) != 0)
		return;

	/*
	 * If "m" is a normal page, flush it from the cache.
	 * See pmap_invalidate_cache_range().
	 *
	 * First, try to find an existing mapping of the page by sf
	 * buffer. sf_buf_invalidate_cache() modifies mapping and
	 * flushes the cache.
	 */    
	if (sf_buf_invalidate_cache(m))
		return;

	/*
	 * If page is not mapped by sf buffer, but CPU does not
	 * support self snoop, map the page transient and do
	 * invalidation. In the worst case, whole cache is flushed by
	 * pmap_invalidate_cache_range().
	 */
	if ((cpu_feature & CPUID_SS) == 0)
		pmap_flush_page(m);
}

static void
__CONCAT(PMTYPE, flush_page)(vm_page_t m)
{
	pt_entry_t *cmap_pte2;
	struct pcpu *pc;
	vm_offset_t sva, eva;
	bool useclflushopt;

	useclflushopt = (cpu_stdext_feature & CPUID_STDEXT_CLFLUSHOPT) != 0;
	if (useclflushopt || (cpu_feature & CPUID_CLFSH) != 0) {
		sched_pin();
		pc = get_pcpu();
		cmap_pte2 = pc->pc_cmap_pte2; 
		mtx_lock(&pc->pc_cmap_lock);
		if (*cmap_pte2)
			panic("pmap_flush_page: CMAP2 busy");
		*cmap_pte2 = PG_V | PG_RW | VM_PAGE_TO_PHYS(m) |
		    PG_A | PG_M | pmap_cache_bits(kernel_pmap, m->md.pat_mode,
		    0);
		invlcaddr(pc->pc_cmap_addr2);
		sva = (vm_offset_t)pc->pc_cmap_addr2;
		eva = sva + PAGE_SIZE;

		/*
		 * Use mfence or sfence despite the ordering implied by
		 * mtx_{un,}lock() because clflush on non-Intel CPUs
		 * and clflushopt are not guaranteed to be ordered by
		 * any other instruction.
		 */
		if (useclflushopt)
			sfence();
		else if (cpu_vendor_id != CPU_VENDOR_INTEL)
			mfence();
		for (; sva < eva; sva += cpu_clflush_line_size) {
			if (useclflushopt)
				clflushopt(sva);
			else
				clflush(sva);
		}
		if (useclflushopt)
			sfence();
		else if (cpu_vendor_id != CPU_VENDOR_INTEL)
			mfence();
		*cmap_pte2 = 0;
		sched_unpin();
		mtx_unlock(&pc->pc_cmap_lock);
	} else
		pmap_invalidate_cache();
}

/*
 * Changes the specified virtual address range's memory type to that given by
 * the parameter "mode".  The specified virtual address range must be
 * completely contained within either the kernel map.
 *
 * Returns zero if the change completed successfully, and either EINVAL or
 * ENOMEM if the change failed.  Specifically, EINVAL is returned if some part
 * of the virtual address range was not mapped, and ENOMEM is returned if
 * there was insufficient memory available to complete the change.
 */
static int
__CONCAT(PMTYPE, change_attr)(vm_offset_t va, vm_size_t size, int mode)
{
	vm_offset_t base, offset, tmpva;
	pd_entry_t *pde;
	pt_entry_t *pte;
	int cache_bits_pte, cache_bits_pde;
	boolean_t changed;

	base = trunc_page(va);
	offset = va & PAGE_MASK;
	size = round_page(offset + size);

	/*
	 * Only supported on kernel virtual addresses above the recursive map.
	 */
	if (base < VM_MIN_KERNEL_ADDRESS)
		return (EINVAL);

	cache_bits_pde = pmap_cache_bits(kernel_pmap, mode, 1);
	cache_bits_pte = pmap_cache_bits(kernel_pmap, mode, 0);
	changed = FALSE;

	/*
	 * Pages that aren't mapped aren't supported.  Also break down
	 * 2/4MB pages into 4KB pages if required.
	 */
	PMAP_LOCK(kernel_pmap);
	for (tmpva = base; tmpva < base + size; ) {
		pde = pmap_pde(kernel_pmap, tmpva);
		if (*pde == 0) {
			PMAP_UNLOCK(kernel_pmap);
			return (EINVAL);
		}
		if (*pde & PG_PS) {
			/*
			 * If the current 2/4MB page already has
			 * the required memory type, then we need not
			 * demote this page.  Just increment tmpva to
			 * the next 2/4MB page frame.
			 */
			if ((*pde & PG_PDE_CACHE) == cache_bits_pde) {
				tmpva = trunc_4mpage(tmpva) + NBPDR;
				continue;
			}

			/*
			 * If the current offset aligns with a 2/4MB
			 * page frame and there is at least 2/4MB left
			 * within the range, then we need not break
			 * down this page into 4KB pages.
			 */
			if ((tmpva & PDRMASK) == 0 &&
			    tmpva + PDRMASK < base + size) {
				tmpva += NBPDR;
				continue;
			}
			if (!pmap_demote_pde(kernel_pmap, pde, tmpva)) {
				PMAP_UNLOCK(kernel_pmap);
				return (ENOMEM);
			}
		}
		pte = vtopte(tmpva);
		if (*pte == 0) {
			PMAP_UNLOCK(kernel_pmap);
			return (EINVAL);
		}
		tmpva += PAGE_SIZE;
	}
	PMAP_UNLOCK(kernel_pmap);

	/*
	 * Ok, all the pages exist, so run through them updating their
	 * cache mode if required.
	 */
	for (tmpva = base; tmpva < base + size; ) {
		pde = pmap_pde(kernel_pmap, tmpva);
		if (*pde & PG_PS) {
			if ((*pde & PG_PDE_CACHE) != cache_bits_pde) {
				pmap_pde_attr(pde, cache_bits_pde);
				changed = TRUE;
			}
			tmpva = trunc_4mpage(tmpva) + NBPDR;
		} else {
			pte = vtopte(tmpva);
			if ((*pte & PG_PTE_CACHE) != cache_bits_pte) {
				pmap_pte_attr(pte, cache_bits_pte);
				changed = TRUE;
			}
			tmpva += PAGE_SIZE;
		}
	}

	/*
	 * Flush CPU caches to make sure any data isn't cached that
	 * shouldn't be, etc.
	 */
	if (changed) {
		pmap_invalidate_range_int(kernel_pmap, base, tmpva);
		pmap_invalidate_cache_range(base, tmpva);
	}
	return (0);
}

/*
 * perform the pmap work for mincore
 */
static int
__CONCAT(PMTYPE, mincore)(pmap_t pmap, vm_offset_t addr, vm_paddr_t *locked_pa)
{
	pd_entry_t pde;
	pt_entry_t pte;
	vm_paddr_t pa;
	int val;

	PMAP_LOCK(pmap);
retry:
	pde = *pmap_pde(pmap, addr);
	if (pde != 0) {
		if ((pde & PG_PS) != 0) {
			pte = pde;
			/* Compute the physical address of the 4KB page. */
			pa = ((pde & PG_PS_FRAME) | (addr & PDRMASK)) &
			    PG_FRAME;
			val = MINCORE_SUPER;
		} else {
			pte = pmap_pte_ufast(pmap, addr, pde);
			pa = pte & PG_FRAME;
			val = 0;
		}
	} else {
		pte = 0;
		pa = 0;
		val = 0;
	}
	if ((pte & PG_V) != 0) {
		val |= MINCORE_INCORE;
		if ((pte & (PG_M | PG_RW)) == (PG_M | PG_RW))
			val |= MINCORE_MODIFIED | MINCORE_MODIFIED_OTHER;
		if ((pte & PG_A) != 0)
			val |= MINCORE_REFERENCED | MINCORE_REFERENCED_OTHER;
	}
	if ((val & (MINCORE_MODIFIED_OTHER | MINCORE_REFERENCED_OTHER)) !=
	    (MINCORE_MODIFIED_OTHER | MINCORE_REFERENCED_OTHER) &&
	    (pte & (PG_MANAGED | PG_V)) == (PG_MANAGED | PG_V)) {
		/* Ensure that "PHYS_TO_VM_PAGE(pa)->object" doesn't change. */
		if (vm_page_pa_tryrelock(pmap, pa, locked_pa))
			goto retry;
	} else
		PA_UNLOCK_COND(*locked_pa);
	PMAP_UNLOCK(pmap);
	return (val);
}

static void
__CONCAT(PMTYPE, activate)(struct thread *td)
{
	pmap_t	pmap, oldpmap;
	u_int	cpuid;
	u_int32_t  cr3;

	critical_enter();
	pmap = vmspace_pmap(td->td_proc->p_vmspace);
	oldpmap = PCPU_GET(curpmap);
	cpuid = PCPU_GET(cpuid);
#if defined(SMP)
	CPU_CLR_ATOMIC(cpuid, &oldpmap->pm_active);
	CPU_SET_ATOMIC(cpuid, &pmap->pm_active);
#else
	CPU_CLR(cpuid, &oldpmap->pm_active);
	CPU_SET(cpuid, &pmap->pm_active);
#endif
#ifdef PMAP_PAE_COMP
	cr3 = vtophys(pmap->pm_pdpt);
#else
	cr3 = vtophys(pmap->pm_pdir);
#endif
	/*
	 * pmap_activate is for the current thread on the current cpu
	 */
	td->td_pcb->pcb_cr3 = cr3;
	PCPU_SET(curpmap, pmap);
	critical_exit();
}

static void
__CONCAT(PMTYPE, activate_boot)(pmap_t pmap)
{
	u_int cpuid;

	cpuid = PCPU_GET(cpuid);
#if defined(SMP)
	CPU_SET_ATOMIC(cpuid, &pmap->pm_active);
#else
	CPU_SET(cpuid, &pmap->pm_active);
#endif
	PCPU_SET(curpmap, pmap);
}

/*
 *	Increase the starting virtual address of the given mapping if a
 *	different alignment might result in more superpage mappings.
 */
static void
__CONCAT(PMTYPE, align_superpage)(vm_object_t object, vm_ooffset_t offset,
    vm_offset_t *addr, vm_size_t size)
{
	vm_offset_t superpage_offset;

	if (size < NBPDR)
		return;
	if (object != NULL && (object->flags & OBJ_COLORED) != 0)
		offset += ptoa(object->pg_color);
	superpage_offset = offset & PDRMASK;
	if (size - ((NBPDR - superpage_offset) & PDRMASK) < NBPDR ||
	    (*addr & PDRMASK) == superpage_offset)
		return;
	if ((*addr & PDRMASK) < superpage_offset)
		*addr = (*addr & ~PDRMASK) + superpage_offset;
	else
		*addr = ((*addr + PDRMASK) & ~PDRMASK) + superpage_offset;
}

static vm_offset_t
__CONCAT(PMTYPE, quick_enter_page)(vm_page_t m)
{
	vm_offset_t qaddr;
	pt_entry_t *pte;

	critical_enter();
	qaddr = PCPU_GET(qmap_addr);
	pte = vtopte(qaddr);

	KASSERT(*pte == 0,
	    ("pmap_quick_enter_page: PTE busy %#jx", (uintmax_t)*pte));
	*pte = PG_V | PG_RW | VM_PAGE_TO_PHYS(m) | PG_A | PG_M |
	    pmap_cache_bits(kernel_pmap, pmap_page_get_memattr(m), 0);
	invlpg(qaddr);

	return (qaddr);
}

static void
__CONCAT(PMTYPE, quick_remove_page)(vm_offset_t addr)
{
	vm_offset_t qaddr;
	pt_entry_t *pte;

	qaddr = PCPU_GET(qmap_addr);
	pte = vtopte(qaddr);

	KASSERT(*pte != 0, ("pmap_quick_remove_page: PTE not in use"));
	KASSERT(addr == qaddr, ("pmap_quick_remove_page: invalid address"));

	*pte = 0;
	critical_exit();
}

static vmem_t *pmap_trm_arena;
static vmem_addr_t pmap_trm_arena_last = PMAP_TRM_MIN_ADDRESS;
static int trm_guard = PAGE_SIZE;

static int
pmap_trm_import(void *unused __unused, vmem_size_t size, int flags,
    vmem_addr_t *addrp)
{
	vm_page_t m;
	vmem_addr_t af, addr, prev_addr;
	pt_entry_t *trm_pte;

	prev_addr = atomic_load_long(&pmap_trm_arena_last);
	size = round_page(size) + trm_guard;
	for (;;) {
		if (prev_addr + size < prev_addr || prev_addr + size < size ||
		    prev_addr + size > PMAP_TRM_MAX_ADDRESS)
			return (ENOMEM);
		addr = prev_addr + size;
		if (atomic_fcmpset_int(&pmap_trm_arena_last, &prev_addr, addr))
			break;
	}
	prev_addr += trm_guard;
	trm_pte = PTmap + atop(prev_addr);
	for (af = prev_addr; af < addr; af += PAGE_SIZE) {
		m = vm_page_alloc(NULL, 0, VM_ALLOC_NOOBJ | VM_ALLOC_NOBUSY |
		    VM_ALLOC_NORMAL | VM_ALLOC_WIRED | VM_ALLOC_WAITOK);
		pte_store(&trm_pte[atop(af - prev_addr)], VM_PAGE_TO_PHYS(m) |
		    PG_M | PG_A | PG_RW | PG_V | pgeflag |
		    pmap_cache_bits(kernel_pmap, VM_MEMATTR_DEFAULT, FALSE));
	}
	*addrp = prev_addr;
	return (0);
}

void
pmap_init_trm(void)
{
	vm_page_t pd_m;

	TUNABLE_INT_FETCH("machdep.trm_guard", &trm_guard);
	if ((trm_guard & PAGE_MASK) != 0)
		trm_guard = 0;
	pmap_trm_arena = vmem_create("i386trampoline", 0, 0, 1, 0, M_WAITOK);
	vmem_set_import(pmap_trm_arena, pmap_trm_import, NULL, NULL, PAGE_SIZE);
	pd_m = vm_page_alloc(NULL, 0, VM_ALLOC_NOOBJ | VM_ALLOC_NOBUSY |
	    VM_ALLOC_NORMAL | VM_ALLOC_WIRED | VM_ALLOC_WAITOK | VM_ALLOC_ZERO);
	if ((pd_m->flags & PG_ZERO) == 0)
		pmap_zero_page(pd_m);
	PTD[TRPTDI] = VM_PAGE_TO_PHYS(pd_m) | PG_M | PG_A | PG_RW | PG_V |
	    pmap_cache_bits(kernel_pmap, VM_MEMATTR_DEFAULT, TRUE);
}

static void *
__CONCAT(PMTYPE, trm_alloc)(size_t size, int flags)
{
	vmem_addr_t res;
	int error;

	MPASS((flags & ~(M_WAITOK | M_NOWAIT | M_ZERO)) == 0);
	error = vmem_xalloc(pmap_trm_arena, roundup2(size, 4), sizeof(int),
	    0, 0, VMEM_ADDR_MIN, VMEM_ADDR_MAX, flags | M_FIRSTFIT, &res);
	if (error != 0)
		return (NULL);
	if ((flags & M_ZERO) != 0)
		bzero((void *)res, size);
	return ((void *)res);
}

static void
__CONCAT(PMTYPE, trm_free)(void *addr, size_t size)
{

	vmem_free(pmap_trm_arena, (uintptr_t)addr, roundup2(size, 4));
}

#if defined(PMAP_DEBUG)
pmap_pid_dump(int pid)
{
	pmap_t pmap;
	struct proc *p;
	int npte = 0;
	int index;

	sx_slock(&allproc_lock);
	FOREACH_PROC_IN_SYSTEM(p) {
		if (p->p_pid != pid)
			continue;

		if (p->p_vmspace) {
			int i,j;
			index = 0;
			pmap = vmspace_pmap(p->p_vmspace);
			for (i = 0; i < NPDEPTD; i++) {
				pd_entry_t *pde;
				pt_entry_t *pte;
				vm_offset_t base = i << PDRSHIFT;
				
				pde = &pmap->pm_pdir[i];
				if (pde && pmap_pde_v(pde)) {
					for (j = 0; j < NPTEPG; j++) {
						vm_offset_t va = base + (j << PAGE_SHIFT);
						if (va >= (vm_offset_t) VM_MIN_KERNEL_ADDRESS) {
							if (index) {
								index = 0;
								printf("\n");
							}
							sx_sunlock(&allproc_lock);
							return (npte);
						}
						pte = pmap_pte(pmap, va);
						if (pte && pmap_pte_v(pte)) {
							pt_entry_t pa;
							vm_page_t m;
							pa = *pte;
							m = PHYS_TO_VM_PAGE(pa & PG_FRAME);
							printf("va: 0x%x, pt: 0x%x, h: %d, w: %d, f: 0x%x",
								va, pa, m->hold_count, m->wire_count, m->flags);
							npte++;
							index++;
							if (index >= 2) {
								index = 0;
								printf("\n");
							} else {
								printf(" ");
							}
						}
					}
				}
			}
		}
	}
	sx_sunlock(&allproc_lock);
	return (npte);
}
#endif

static void
__CONCAT(PMTYPE, ksetrw)(vm_offset_t va)
{

	*vtopte(va) |= PG_RW;
}

static void
__CONCAT(PMTYPE, remap_lowptdi)(bool enable)
{

	PTD[KPTDI] = enable ? PTD[LOWPTDI] : 0;
	invltlb_glob();
}

static vm_offset_t
__CONCAT(PMTYPE, get_map_low)(void)
{

	return (PMAP_MAP_LOW);
}

static vm_offset_t
__CONCAT(PMTYPE, get_vm_maxuser_address)(void)
{

	return (VM_MAXUSER_ADDRESS);
}

static vm_paddr_t
__CONCAT(PMTYPE, pg_frame)(vm_paddr_t pa)
{

	return (pa & PG_FRAME);
}

static void
__CONCAT(PMTYPE, sf_buf_map)(struct sf_buf *sf)
{
	pt_entry_t opte, *ptep;

	/*
	 * Update the sf_buf's virtual-to-physical mapping, flushing the
	 * virtual address from the TLB.  Since the reference count for
	 * the sf_buf's old mapping was zero, that mapping is not
	 * currently in use.  Consequently, there is no need to exchange
	 * the old and new PTEs atomically, even under PAE.
	 */
	ptep = vtopte(sf->kva);
	opte = *ptep;
	*ptep = VM_PAGE_TO_PHYS(sf->m) | PG_RW | PG_V |
	    pmap_cache_bits(kernel_pmap, sf->m->md.pat_mode, 0);

	/*
	 * Avoid unnecessary TLB invalidations: If the sf_buf's old
	 * virtual-to-physical mapping was not used, then any processor
	 * that has invalidated the sf_buf's virtual address from its TLB
	 * since the last used mapping need not invalidate again.
	 */
#ifdef SMP
	if ((opte & (PG_V | PG_A)) ==  (PG_V | PG_A))
		CPU_ZERO(&sf->cpumask);
#else
	if ((opte & (PG_V | PG_A)) ==  (PG_V | PG_A))
		pmap_invalidate_page_int(kernel_pmap, sf->kva);
#endif
}

static void
__CONCAT(PMTYPE, cp_slow0_map)(vm_offset_t kaddr, int plen, vm_page_t *ma)
{
	pt_entry_t *pte;
	int i;

	for (i = 0, pte = vtopte(kaddr); i < plen; i++, pte++) {
		*pte = PG_V | PG_RW | PG_A | PG_M | VM_PAGE_TO_PHYS(ma[i]) |
		    pmap_cache_bits(kernel_pmap, pmap_page_get_memattr(ma[i]),
		    FALSE);
		invlpg(kaddr + ptoa(i));
	}
}

static u_int
__CONCAT(PMTYPE, get_kcr3)(void)
{

#ifdef PMAP_PAE_COMP
	return ((u_int)IdlePDPT);
#else
	return ((u_int)IdlePTD);
#endif
}

static u_int
__CONCAT(PMTYPE, get_cr3)(pmap_t pmap)
{

#ifdef PMAP_PAE_COMP
	return ((u_int)vtophys(pmap->pm_pdpt));
#else
	return ((u_int)vtophys(pmap->pm_pdir));
#endif
}

static caddr_t
__CONCAT(PMTYPE, cmap3)(vm_paddr_t pa, u_int pte_bits)
{
	pt_entry_t *pte;

	pte = CMAP3;
	*pte = pa | pte_bits;
	invltlb();
	return (CADDR3);
}

static void
__CONCAT(PMTYPE, basemem_setup)(u_int basemem)
{
	pt_entry_t *pte;
	int i;

	/*
	 * Map pages between basemem and ISA_HOLE_START, if any, r/w into
	 * the vm86 page table so that vm86 can scribble on them using
	 * the vm86 map too.  XXX: why 2 ways for this and only 1 way for
	 * page 0, at least as initialized here?
	 */
	pte = (pt_entry_t *)vm86paddr;
	for (i = basemem / 4; i < 160; i++)
		pte[i] = (i << PAGE_SHIFT) | PG_V | PG_RW | PG_U;
}

struct bios16_pmap_handle {
	pt_entry_t	*pte;
	pd_entry_t	*ptd;
	pt_entry_t	orig_ptd;
};

static void *
__CONCAT(PMTYPE, bios16_enter)(void)
{
	struct bios16_pmap_handle *h;

	/*
	 * no page table, so create one and install it.
	 */
	h = malloc(sizeof(struct bios16_pmap_handle), M_TEMP, M_WAITOK);
	h->pte = (pt_entry_t *)malloc(PAGE_SIZE, M_TEMP, M_WAITOK);
	h->ptd = IdlePTD;
	*h->pte = vm86phystk | PG_RW | PG_V;
	h->orig_ptd = *h->ptd;
	*h->ptd = vtophys(h->pte) | PG_RW | PG_V;
	pmap_invalidate_all_int(kernel_pmap);	/* XXX insurance for now */
	return (h);
}

static void
__CONCAT(PMTYPE, bios16_leave)(void *arg)
{
	struct bios16_pmap_handle *h;

	h = arg;
	*h->ptd = h->orig_ptd;		/* remove page table */
	/*
	 * XXX only needs to be invlpg(0) but that doesn't work on the 386
	 */
	pmap_invalidate_all_int(kernel_pmap);
	free(h->pte, M_TEMP);		/* ... and free it */
}

#define	PMM(a)	\
	.pm_##a = __CONCAT(PMTYPE, a),

struct pmap_methods __CONCAT(PMTYPE, methods) = {
	PMM(ksetrw)
	PMM(remap_lower)
	PMM(remap_lowptdi)
	PMM(align_superpage)
	PMM(quick_enter_page)
	PMM(quick_remove_page)
	PMM(trm_alloc)
	PMM(trm_free)
	PMM(get_map_low)
	PMM(get_vm_maxuser_address)
	PMM(kextract)
	PMM(pg_frame)
	PMM(sf_buf_map)
	PMM(cp_slow0_map)
	PMM(get_kcr3)
	PMM(get_cr3)
	PMM(cmap3)
	PMM(basemem_setup)
	PMM(set_nx)
	PMM(bios16_enter)
	PMM(bios16_leave)
	PMM(bootstrap)
	PMM(is_valid_memattr)
	PMM(cache_bits)
	PMM(ps_enabled)
	PMM(pinit0)
	PMM(pinit)
	PMM(activate)
	PMM(activate_boot)
	PMM(advise)
	PMM(clear_modify)
	PMM(change_attr)
	PMM(mincore)
	PMM(copy)
	PMM(copy_page)
	PMM(copy_pages)
	PMM(zero_page)
	PMM(zero_page_area)
	PMM(enter)
	PMM(enter_object)
	PMM(enter_quick)
	PMM(kenter_temporary)
	PMM(object_init_pt)
	PMM(unwire)
	PMM(page_exists_quick)
	PMM(page_wired_mappings)
	PMM(page_is_mapped)
	PMM(remove_pages)
	PMM(is_modified)
	PMM(is_prefaultable)
	PMM(is_referenced)
	PMM(remove_write)
	PMM(ts_referenced)
	PMM(mapdev_attr)
	PMM(unmapdev)
	PMM(page_set_memattr)
	PMM(extract)
	PMM(extract_and_hold)
	PMM(map)
	PMM(qenter)
	PMM(qremove)
	PMM(release)
	PMM(remove)
	PMM(protect)
	PMM(remove_all)
	PMM(init)
	PMM(init_pat)
	PMM(growkernel)
	PMM(invalidate_page)
	PMM(invalidate_range)
	PMM(invalidate_all)
	PMM(invalidate_cache)
	PMM(flush_page)
	PMM(kenter)
	PMM(kremove)
};
