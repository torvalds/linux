/*	$OpenBSD: pmap.c,v 1.91 2025/08/13 16:23:14 miod Exp $	*/

/*
 * Copyright (c) 2001-2004, 2010, Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Copyright (c) 1998-2001 Steve Murphree, Jr.
 * Copyright (c) 1996 Nivas Madhur
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
 *      This product includes software developed by Nivas Madhur.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 */
/*
 * Mach Operating System
 * Copyright (c) 1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/pool.h>

#include <uvm/uvm.h>

#include <machine/asm_macro.h>
#include <machine/cmmu.h>
#include <machine/cpu.h>
#include <machine/pmap_table.h>
#ifdef M88100
#include <machine/m8820x.h>
#endif
#ifdef M88110
#include <machine/m88110.h>
#endif

/*
 * VM externals
 */
extern paddr_t last_addr;
vaddr_t avail_start;
vaddr_t avail_end;
vaddr_t virtual_avail = VM_MIN_KERNEL_ADDRESS;
vaddr_t virtual_end = VM_MAX_KERNEL_ADDRESS;


#ifdef	PMAPDEBUG
/*
 * conditional debugging
 */
#define CD_ACTIVATE	0x00000001	/* pmap_activate */
#define CD_KMAP		0x00000002	/* pmap_expand_kmap */
#define CD_MAP		0x00000004	/* pmap_map */
#define CD_CACHE	0x00000008	/* pmap_cache_ctrl */
#define CD_INIT		0x00000010	/* pmap_init */
#define CD_CREAT	0x00000020	/* pmap_create */
#define CD_DESTR	0x00000040	/* pmap_destroy */
#define CD_RM		0x00000080	/* pmap_remove / pmap_kremove */
#define CD_RMPG		0x00000100	/* pmap_remove_page */
#define CD_EXP		0x00000200	/* pmap_expand */
#define CD_ENT		0x00000400	/* pmap_enter / pmap_kenter_pa */
#define CD_WP		0x00001000	/* pmap_write_protect */
#define CD_TBIT		0x00002000	/* pmap_testbit */
#define CD_USBIT	0x00004000	/* pmap_unsetbit */
#define	CD_COPY		0x00008000	/* pmap_copy_page */
#define	CD_ZERO		0x00010000	/* pmap_zero_page */
#define	CD_BOOT		0x00020000	/* pmap_bootstrap */
#define CD_ALL		0xffffffff

int pmap_debug = CD_BOOT | CD_KMAP | CD_MAP;

#define	DPRINTF(flg, stmt) \
do { \
	if (pmap_debug & (flg)) \
		printf stmt; \
} while (0)

#else

#define	DPRINTF(flg, stmt) do { } while (0)

#endif	/* PMAPDEBUG */

struct pool pmappool, pvpool;
struct pmap kernel_pmap_store;

/*
 * Cacheability settings for page tables and kernel data.
 */

apr_t	pte_cmode = CACHE_WT;
apr_t	kernel_apr = CACHE_GLOBAL | CACHE_DFL | APR_V;
apr_t	userland_apr = CACHE_GLOBAL | CACHE_DFL | APR_V;

#define	KERNEL_APR_CMODE	(kernel_apr & (CACHE_MASK & ~CACHE_GLOBAL))
#define	USERLAND_APR_CMODE	(userland_apr & (CACHE_MASK & ~CACHE_GLOBAL))

/*
 * Address and size of the temporary firmware mapping
 */
paddr_t	s_firmware;
psize_t	l_firmware;

/*
 * Current BATC values.
 */

batc_t global_dbatc[BATC_MAX];
batc_t global_ibatc[BATC_MAX];

/*
 * Internal routines
 */
void		 pmap_write_protect(struct vm_page *);
void		 pmap_clean_page(paddr_t);
pt_entry_t	*pmap_expand(pmap_t, vaddr_t, int);
pt_entry_t	*pmap_expand_kmap(vaddr_t, int);
void		 pmap_map(paddr_t, psize_t, vm_prot_t, u_int, boolean_t);
pt_entry_t	*pmap_pte(pmap_t, vaddr_t);
void		 pmap_remove_page(struct vm_page *);
void		 pmap_remove_pte(pmap_t, vaddr_t, pt_entry_t *,
		    struct vm_page *, boolean_t);
void		 pmap_remove_range(pmap_t, vaddr_t, vaddr_t);
boolean_t	 pmap_testbit(struct vm_page *, int);

static __inline pv_entry_t
pg_to_pvh(struct vm_page *pg)
{
	return &pg->mdpage.pv_ent;
}

static __inline pt_entry_t
invalidate_pte(pt_entry_t *pte)
{
	pt_entry_t oldpte;

	oldpte = PG_NV;
	__asm__ volatile
	    ("xmem %0, %2, %%r0" : "+r"(oldpte), "+m"(*pte) : "r"(pte));
	return oldpte;
}

/*
 * PTE routines
 */

#define	m88k_protection(prot)	((prot) & PROT_WRITE ? PG_RW : PG_RO)
#define	pmap_pte_w(pte)		(*(pte) & PG_W)

#define SDTENT(pm, va)		((pm)->pm_stab + SDTIDX(va))

/*
 * [INTERNAL]
 * Return the address of the pte for `va' within the page table pointed
 * to by the segment table entry `sdt'. Assumes *sdt is a valid segment
 * table entry.
 */
static __inline__
pt_entry_t *
sdt_pte(sdt_entry_t *sdt, vaddr_t va)
{
	return (pt_entry_t *)(*sdt & PG_FRAME) + PDTIDX(va);
}

/*
 * [INTERNAL]
 * Return the address of the pte for `va' in `pmap'. NULL if there is no
 * page table for `va'.
 */
pt_entry_t *
pmap_pte(pmap_t pmap, vaddr_t va)
{
	sdt_entry_t *sdt;

	sdt = SDTENT(pmap, va);
	if (!SDT_VALID(sdt))
		return NULL;

	return sdt_pte(sdt, va);
}

/*
 * [MI]
 * Checks how virtual address `va' would translate with `pmap' as the active
 * pmap. Returns TRUE and matching physical address in `pap' (if not NULL) if
 * translation is possible, FAILS otherwise.
 */
boolean_t
pmap_extract(pmap_t pmap, vaddr_t va, paddr_t *pap)
{
	paddr_t pa;
	uint32_t ti;
	int rv;

	rv = pmap_translation_info(pmap, va, &pa, &ti);
	if (rv == PTI_INVALID)
		return FALSE;
	else {
		if (pap != NULL)
			*pap = pa;
		return TRUE;
	}
}

/*
 * [MD PUBLIC]
 * Checks how virtual address `va' would translate with `pmap' as the active
 * pmap. Returns a PTI_xxx constant indicating which translation hardware
 * would perform the translation; if not PTI_INVALID, the matching physical
 * address is returned into `pap', and cacheability of the mapping is
 * returned into `ti'.
 */
int
pmap_translation_info(pmap_t pmap, vaddr_t va, paddr_t *pap, uint32_t *ti)
{
	pt_entry_t *pte;
	vaddr_t var;
	uint batcno;
	int s;
	int rv;

	/*
	 * Check for a BATC translation first.
	 * We only use BATC for supervisor mappings (i.e. pmap_kernel()).
	 */

	if (pmap == pmap_kernel()) {
		/*
		 * 88100-based designs (with 8820x CMMUs) have two hardwired
		 * BATC entries which map the upper 1MB (so-called
		 * `utility space') 1:1 in supervisor space.
		 */
#ifdef M88100
		if (CPU_IS88100) {
			if (va >= BATC9_VA) {
				*pap = va;
				*ti = 0;
				if (BATC9 & BATC_INH)
					*ti |= CACHE_INH;
				if (BATC9 & BATC_GLOBAL)
					*ti |= CACHE_GLOBAL;
				if (BATC9 & BATC_WT)
					*ti |= CACHE_WT;
				return PTI_BATC;
			}
			if (va >= BATC8_VA) {
				*pap = va;
				*ti = 0;
				if (BATC8 & BATC_INH)
					*ti |= CACHE_INH;
				if (BATC8 & BATC_GLOBAL)
					*ti |= CACHE_GLOBAL;
				if (BATC8 & BATC_WT)
					*ti |= CACHE_WT;
				return PTI_BATC;
			}
		}
#endif

		/*
		 * Now try all DBATC entries.
		 * Note that pmap_translation_info() might be invoked (via
		 * pmap_extract() ) for instruction faults; we *rely* upon
		 * the fact that all executable mappings covered by IBATC
		 * will be:
		 * - read-only, with no RO->RW upgrade allowed
		 * - dual mapped by ptes, so that pmap_extract() can still
		 *   return a meaningful result.
		 * Should this ever change, some kernel interfaces will need
		 * to be made aware of (and carry on to callees) whether the
		 * address should be resolved as an instruction or data
		 * address.
		 */
		var = trunc_batc(va);
		for (batcno = 0; batcno < BATC_MAX; batcno++) {
			vaddr_t batcva;
			paddr_t batcpa;
			batc_t batc;

			batc = global_dbatc[batcno];
			if ((batc & BATC_V) == 0)
				continue;

			batcva = (batc << (BATC_BLKSHIFT - BATC_VSHIFT)) &
			    ~BATC_BLKMASK;
			if (batcva == var) {
				batcpa = (batc <<
				    (BATC_BLKSHIFT - BATC_PSHIFT)) &
				    ~BATC_BLKMASK;
				*pap = batcpa + (va - var);
				*ti = 0;
				if (batc & BATC_INH)
					*ti |= CACHE_INH;
				if (batc & BATC_GLOBAL)
					*ti |= CACHE_GLOBAL;
				if (batc & BATC_WT)
					*ti |= CACHE_WT;
				return PTI_BATC;
			}
		}
	}

	/*
	 * Check for a regular PTE translation.
	 */

	s = splvm();
	pte = pmap_pte(pmap, va);
	if (pte != NULL && PDT_VALID(pte)) {
		*pap = ptoa(PG_PFNUM(*pte)) | (va & PAGE_MASK);
		*ti = (*pte | pmap->pm_apr) & CACHE_MASK;
		rv = PTI_PTE;
	} else
		rv = PTI_INVALID;

	splx(s);

	return rv;
}

/*
 * TLB (ATC) routines
 */

void		 tlb_flush(pmap_t, vaddr_t, pt_entry_t);
void		 tlb_kflush(vaddr_t, pt_entry_t);

/*
 * [INTERNAL]
 * Update translation cache entry for `va' in `pmap' to `pte'. May flush
 * instead of updating.
 */
void
tlb_flush(pmap_t pmap, vaddr_t va, pt_entry_t pte)
{
	struct cpu_info *ci;
	boolean_t kernel = pmap == pmap_kernel();
#ifdef MULTIPROCESSOR
	CPU_INFO_ITERATOR cpu;
#endif

#ifdef MULTIPROCESSOR
	CPU_INFO_FOREACH(cpu, ci)
#else
	ci = curcpu();
#endif
	{
		if (kernel)
			cmmu_tlbis(ci->ci_cpuid, va, pte);
		else if (pmap == ci->ci_curpmap)
			cmmu_tlbiu(ci->ci_cpuid, va, pte);
	}
}

/*
 * [INTERNAL]
 * Update translation cache entry for `va' in pmap_kernel() to `pte'. May
 * flush instead of updating.
 */
void
tlb_kflush(vaddr_t va, pt_entry_t pte)
{
	struct cpu_info *ci;
#ifdef MULTIPROCESSOR
	CPU_INFO_ITERATOR cpu;
#endif

#ifdef MULTIPROCESSOR		/* { */
	CPU_INFO_FOREACH(cpu, ci) {
		cmmu_tlbis(ci->ci_cpuid, va, pte);
	}
#else	/* MULTIPROCESSOR */	/* } { */
	ci = curcpu();
	cmmu_tlbis(ci->ci_cpuid, va, pte);
#endif	/* MULTIPROCESSOR */	/* } */
}

/*
 * [MI]
 * Activate the pmap of process `p'.
 */
void
pmap_activate(struct proc *p)
{
	pmap_t pmap = vm_map_pmap(&p->p_vmspace->vm_map);
	struct cpu_info *ci = curcpu();

	DPRINTF(CD_ACTIVATE, ("pmap_activate(%p) pmap %p\n", p, pmap));

	if (pmap == pmap_kernel()) {
		ci->ci_curpmap = NULL;
	} else {
		if (pmap != ci->ci_curpmap) {
			cmmu_set_uapr(pmap->pm_apr);
			cmmu_tlbia(ci->ci_cpuid);
			ci->ci_curpmap = pmap;
		}
	}
}

/*
 * [MI]
 * Deactivates the pmap of process `p'.
 */
void
pmap_deactivate(struct proc *p)
{
	struct cpu_info *ci = curcpu();

	ci->ci_curpmap = NULL;
}

/*
 * Segment and page table management routines
 */

/*
 * [INTERNAL]
 * Expand pmap_kernel() to be able to map a page at `va', by allocating
 * a page table. Returns a pointer to the pte of this page, or NULL
 * if allocation failed and `canfail' is nonzero. Panics if allocation
 * fails and `canfail' is zero.
 * Caller is supposed to only invoke this function if
 * pmap_pte(pmap_kernel(), va) returns NULL.
 */
pt_entry_t *
pmap_expand_kmap(vaddr_t va, int canfail)
{
	sdt_entry_t *sdt;
	struct vm_page *pg;
	paddr_t pa;

	DPRINTF(CD_KMAP, ("pmap_expand_kmap(%lx, %d)\n", va, canfail));

	if (__predict_true(uvm.page_init_done)) {
		pg = uvm_pagealloc(NULL, 0, NULL,
		    (canfail ? 0 : UVM_PGA_USERESERVE) | UVM_PGA_ZERO);
		if (pg == NULL) {
			if (canfail)
				return NULL;
			panic("pmap_expand_kmap(%p): uvm_pagealloc() failed",
			    (void *)va);
		}
		pa = VM_PAGE_TO_PHYS(pg);
	} else {
		pa = (paddr_t)uvm_pageboot_alloc(PAGE_SIZE);
		if (pa == 0)
			panic("pmap_expand_kmap(%p): uvm_pageboot_alloc() failed",
			    (void *)va);
		bzero((void *)pa, PAGE_SIZE);
	}

	pmap_cache_ctrl(pa, pa + PAGE_SIZE, pte_cmode);
	sdt = SDTENT(pmap_kernel(), va);
	*sdt = pa | SG_SO | SG_RW | PG_M | SG_V;
	return sdt_pte(sdt, va);
}

/*
 * [INTERNAL]
 * Expand `pmap' to be able to map a page at `va', by allocating
 * a page table. Returns a pointer to the pte of this page, or NULL
 * if allocation failed and `canfail' is nonzero. Waits until memory is
 * available if allocation fails and `canfail' is zero.
 * Caller is supposed to only invoke this function if
 * pmap_pte(pmap, va) returns NULL.
 */
pt_entry_t *
pmap_expand(pmap_t pmap, vaddr_t va, int canfail)
{
	struct vm_page *pg;
	paddr_t pa;
	sdt_entry_t *sdt;

	DPRINTF(CD_EXP, ("pmap_expand(%p, %lx, %d)\n", pmap, va, canfail));

	sdt = SDTENT(pmap, va);
	for (;;) {
		pg = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO);
		if (pg != NULL)
			break;
		if (canfail)
			return NULL;
		uvm_wait(__func__);
	}

	pa = VM_PAGE_TO_PHYS(pg);
	pmap_cache_ctrl(pa, pa + PAGE_SIZE, pte_cmode);

	*sdt = pa | SG_RW | PG_M | SG_V;

	return sdt_pte(sdt, va);
}

/*
 * Bootstrap routines
 */

/*
 * [MI]
 * Early allocation, directly from the vm_physseg ranges of managed pages
 * passed to UVM. Pages ``stolen'' by this routine will never be seen as
 * managed pages and will not have vm_page structs created for them,
 */
vaddr_t
pmap_steal_memory(vsize_t size, vaddr_t *vstartp, vaddr_t *vendp)
{
	vaddr_t va;
	u_int npg;

	size = round_page(size);
	npg = atop(size);

	/* m88k systems only have one segment. */
#ifdef DIAGNOSTIC
	if (vm_physmem[0].avail_end - vm_physmem[0].avail_start < npg)
		panic("pmap_steal_memory(%lx): out of memory", size);
#endif

	va = ptoa(vm_physmem[0].avail_start);
	vm_physmem[0].avail_start += npg;
	vm_physmem[0].start += npg;

	if (vstartp != NULL)
		*vstartp = virtual_avail;
	if (vendp != NULL)
		*vendp = virtual_end;
	
	bzero((void *)va, size);
	return (va);
}

/*
 * [INTERNAL]
 * Setup a wired mapping in pmap_kernel(). Similar to pmap_kenter_pa(),
 * but allows explicit cacheability control.
 * This is only used at bootstrap time. Mappings may also be backed up
 * by a BATC entry if requested and possible; but note that the BATC
 * entries set up here may be overwritten by cmmu_batc_setup() later on
 * (which is harmless since we are creating proper ptes anyway).
 */
void
pmap_map(paddr_t pa, psize_t sz, vm_prot_t prot, u_int cmode,
    boolean_t may_use_batc)
{
	pt_entry_t *pte, npte;
	batc_t batc;
	uint npg, batcno;
	paddr_t curpa;

	DPRINTF(CD_MAP, ("pmap_map(%lx, %lx, %x, %x)\n",
	    pa, sz, prot, cmode));
#ifdef DIAGNOSTIC
	if (pa != 0 && pa < VM_MAX_KERNEL_ADDRESS)
		panic("pmap_map: virtual range %p-%p overlaps KVM",
		    (void *)pa, (void *)(pa + sz));
#endif

	sz = round_page(pa + sz) - trunc_page(pa);
	pa = trunc_page(pa);

	npte = m88k_protection(prot) | cmode | PG_W | PG_V;
#ifdef M88110
	if (CPU_IS88110 && m88k_protection(prot) != PG_RO)
		npte |= PG_M;
#endif

	npg = atop(sz);
	curpa = pa;
	while (npg-- != 0) {
		if ((pte = pmap_pte(pmap_kernel(), curpa)) == NULL)
			pte = pmap_expand_kmap(curpa, 0);

		*pte = npte | curpa;
		curpa += PAGE_SIZE;
		pmap_kernel()->pm_stats.resident_count++;
		pmap_kernel()->pm_stats.wired_count++;
	}

	if (may_use_batc) {
		sz = round_batc(pa + sz) - trunc_batc(pa);
		pa = trunc_batc(pa);

		batc = BATC_SO | BATC_V;
		if ((prot & PROT_WRITE) == 0)
			batc |= BATC_PROT;
		if (cmode & CACHE_INH)
			batc |= BATC_INH;
		if (cmode & CACHE_WT)
			batc |= BATC_WT;
		batc |= BATC_GLOBAL;	/* XXX 88110 SP */

		for (; sz != 0; sz -= BATC_BLKBYTES, pa += BATC_BLKBYTES) {
			/* check if an existing BATC covers this area */
			for (batcno = 0; batcno < BATC_MAX; batcno++) {
				if ((global_dbatc[batcno] & BATC_V) == 0)
					continue;
				curpa = (global_dbatc[batcno] <<
				    (BATC_BLKSHIFT - BATC_PSHIFT)) &
				    ~BATC_BLKMASK;
				if (curpa == pa)
					break;
			}

			/*
			 * If there is a BATC covering this range, reuse it.
			 * We assume all BATC-possible mappings will use the
			 * same protection and cacheability settings.
			 */
			if (batcno != BATC_MAX)
				continue;

			/* create a new DBATC if possible */
			for (batcno = BATC_MAX; batcno != 0; batcno--) {
				if (global_dbatc[batcno - 1] & BATC_V)
					continue;
				global_dbatc[batcno - 1] = batc |
				    ((pa >> BATC_BLKSHIFT) << BATC_PSHIFT) |
				    ((pa >> BATC_BLKSHIFT) << BATC_VSHIFT);
				break;
			}
		}
	}
}

/*
 * [MD]
 * Initialize kernel translation tables.
 */
void
pmap_bootstrap(paddr_t s_rom, paddr_t e_rom)
{
	paddr_t s_low, s_text, e_rodata;
	unsigned int npdtpg, nsdt, npdt;
	unsigned int i;
	sdt_entry_t *sdt;
	pt_entry_t *pte, template;
	paddr_t pa, sdtpa, ptepa;
	const struct pmap_table *ptable;
	extern void *kernelstart;
	extern void *erodata;

	virtual_avail = (vaddr_t)avail_end;

	s_text = trunc_page((vaddr_t)&kernelstart);
	e_rodata = round_page((vaddr_t)&erodata);

	/*
	 * Reserve space for 1:1 memory mapping in supervisor space.
	 * We need:
	 * - roundup(avail_end, SDT_SIZE) / SDT_SIZE segment tables;
	 *   these will fit in one page.
	 * - roundup(avail_end, PDT_SIZE) / PDT_SIZE page tables;
	 *   these will span several pages.
	 */

	nsdt = roundup(avail_end, (1 << SDT_SHIFT)) >> SDT_SHIFT;
	npdt = roundup(avail_end, (1 << PDT_SHIFT)) >> PDT_SHIFT;
	DPRINTF(CD_BOOT, ("avail_end %08lx pages %08lx nsdt %08x npdt %08x\n",
	    avail_end, atop(avail_end), nsdt, npdt));

	/*
	 * Since page tables may need specific cacheability settings,
	 * we need to make sure they will not end up in the BATC
	 * mapping the end of the kernel data.
	 *
	 * The CMMU initialization code will try, whenever possible, to
	 * setup 512KB BATC entries to map the kernel text and data,
	 * therefore platform-specific code is expected to register a
	 * non-overlapping range of pages (so that their cacheability
	 * can be controlled at the PTE level).
	 *
	 * If there is enough room between the firmware image and the
	 * beginning of the BATC-mapped region, we will setup the
	 * initial page tables there (and actually try to setup as many
	 * second level pages as possible, since this memory is not
	 * given to the VM system).
	 */

	npdtpg = atop(round_page(npdt * sizeof(pt_entry_t)));
	s_low = trunc_batc(s_text);

	if (e_rom == 0)
		s_rom = e_rom = PAGE_SIZE;
	DPRINTF(CD_BOOT, ("nsdt %d npdt %d npdtpg %d\n", nsdt, npdt, npdtpg));
	DPRINTF(CD_BOOT, ("area below the kernel %lx-%lx: %ld pages, need %d\n",
	    e_rom, s_low, atop(s_low - e_rom), npdtpg + 1));
	if (e_rom < s_low && npdtpg + 1 <= atop(s_low - e_rom)) {
		sdtpa = e_rom;
		ptepa = sdtpa + PAGE_SIZE;
	} else {
		sdtpa = (paddr_t)uvm_pageboot_alloc(PAGE_SIZE);
		ptepa = (paddr_t)uvm_pageboot_alloc(ptoa(npdtpg));
	}

	sdt = (sdt_entry_t *)sdtpa;
	pte = (pt_entry_t *)ptepa;
	pmap_kernel()->pm_stab = sdt;

	DPRINTF(CD_BOOT, ("kernel sdt %p", sdt));
	pa = ptepa;
	for (i = nsdt; i != 0; i--) {
		*sdt++ = pa | SG_SO | SG_RW | PG_M | SG_V;
		pa += PAGE_SIZE;
	}
	DPRINTF(CD_BOOT, ("-%p\n", sdt));
	for (i = (PAGE_SIZE / sizeof(sdt_entry_t)) - nsdt; i != 0; i--)
		*sdt++ = SG_NV;
	KDASSERT((vaddr_t)sdt == ptepa);

	DPRINTF(CD_BOOT, ("kernel pte %p", pte));
	/* memory below the kernel image */
	for (i = atop(s_text); i != 0; i--)
		*pte++ = PG_NV;
	/* kernel text and rodata */
	pa = s_text;
	for (i = atop(e_rodata) - atop(pa); i != 0; i--) {
		*pte++ = pa | PG_SO | PG_RO | PG_W | PG_V;
		pa += PAGE_SIZE;
	}
	/* kernel data and symbols */
	for (i = atop(avail_start) - atop(pa); i != 0; i--) {
#ifdef MULTIPROCESSOR
		*pte++ = pa | PG_SO | PG_RW | PG_M_U | PG_W | PG_V | CACHE_WT;
#else
		*pte++ = pa | PG_SO | PG_RW | PG_M_U | PG_W | PG_V;
#endif
		pa += PAGE_SIZE;
	}
	/* regular memory */
	for (i = atop(avail_end) - atop(pa); i != 0; i--) {
		*pte++ = pa | PG_SO | PG_RW | PG_M_U | PG_V;
		pa += PAGE_SIZE;
	}
	DPRINTF(CD_BOOT, ("-%p, pa %08lx\n", pte, pa));
	for (i = (pt_entry_t *)round_page((vaddr_t)pte) - pte; i != 0; i--)
		*pte++ = PG_NV;

	/* kernel page tables */
	pte_cmode = cmmu_pte_cmode();
	template = PG_SO | PG_RW | PG_M_U | PG_W | PG_V | pte_cmode;
	pa = sdtpa;
	pte = (pt_entry_t *)ptepa + atop(pa);
	for (i = 1 + npdtpg; i != 0; i--) {
		*pte++ = pa | template;
		pa += PAGE_SIZE;
	}

	/*
	 * Create all the machine-specific mappings.
	 * XXX This should eventually get done in machdep.c instead of here;
	 * XXX and on a driver basis on luna88k... If only to be able to grow
	 * XXX VM_MAX_KERNEL_ADDRESS.
	 */

	if (e_rom != s_rom) {
		s_firmware = s_rom;
		l_firmware = e_rom - s_rom;
		pmap_map(s_firmware, l_firmware, PROT_READ | PROT_WRITE,
		    CACHE_INH, FALSE);
	}

	for (ptable = pmap_table_build(); ptable->size != (vsize_t)-1; ptable++)
		if (ptable->size != 0)
			pmap_map(ptable->start, ptable->size,
			    ptable->prot, ptable->cacheability,
			    ptable->may_use_batc);

	/*
	 * Adjust cache settings according to the hardware we are running on.
	 */

	kernel_apr = (kernel_apr & ~(CACHE_MASK & ~CACHE_GLOBAL)) |
	    cmmu_apr_cmode();
#if defined(M88110) && !defined(MULTIPROCESSOR)
	if (CPU_IS88110)
		kernel_apr &= ~CACHE_GLOBAL;
#endif
	userland_apr = (userland_apr & ~CACHE_MASK) | (kernel_apr & CACHE_MASK);

	/*
	 * Switch to using new page tables
	 */

	pmap_kernel()->pm_count = 1;
	pmap_kernel()->pm_apr = sdtpa | kernel_apr;

	DPRINTF(CD_BOOT, ("default apr %08x kernel apr %08lx\n",
	    kernel_apr, sdtpa));

	pmap_bootstrap_cpu(cpu_number());
}

/*
 * [MD]
 * Enable address translation on the current processor.
 */
void
pmap_bootstrap_cpu(cpuid_t cpu)
{
	/* Load supervisor pointer to segment table. */
	cmmu_set_sapr(pmap_kernel()->pm_apr);
#ifdef PMAPDEBUG
	printf("cpu%lu: running virtual\n", cpu);
#endif

	cmmu_batc_setup(cpu, kernel_apr & CACHE_MASK);

	curcpu()->ci_curpmap = NULL;
}

/*
 * [MD]
 * Remove firmware mappings when they are no longer necessary.
 */
void
pmap_unmap_firmware()
{
	if (l_firmware != 0) {
		pmap_kremove(s_firmware, l_firmware);
		pmap_update(pmap_kernel());
	}
}

/*
 * [MI]
 * Complete the pmap layer initialization, to be able to manage userland
 * pmaps.
 */
void
pmap_init(void)
{
	DPRINTF(CD_INIT, ("pmap_init()\n"));
	pool_init(&pmappool, sizeof(struct pmap), 0, IPL_NONE, 0,
	    "pmappl", &pool_allocator_single);
	pool_init(&pvpool, sizeof(pv_entry_t), 0, IPL_VM, 0, "pvpl", NULL);
}

/*
 * Pmap structure management
 */

/*
 * [MI]
 * Create a new pmap.
 */
pmap_t
pmap_create(void)
{
	pmap_t pmap;
	struct vm_page *pg;
	paddr_t pa;

	pmap = pool_get(&pmappool, PR_WAITOK | PR_ZERO);

	/* Allocate the segment table page immediately. */
	for (;;) {
		pg = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO);
		if (pg != NULL)
			break;
		uvm_wait(__func__);
	}

	pa = VM_PAGE_TO_PHYS(pg);
	pmap_cache_ctrl(pa, pa + PAGE_SIZE, pte_cmode);

	pmap->pm_stab = (sdt_entry_t *)pa;
	pmap->pm_apr = pa | userland_apr;
	pmap->pm_count = 1;

	DPRINTF(CD_CREAT, ("pmap_create() -> pmap %p, pm_stab %lx\n", pmap, pa));

	return pmap;
}

/*
 * [MI]
 * Decreased the pmap reference count, and destroy it when it reaches zero.
 */
void
pmap_destroy(pmap_t pmap)
{
	u_int u;
	sdt_entry_t *sdt;
	paddr_t pa;

	DPRINTF(CD_DESTR, ("pmap_destroy(%p)\n", pmap));
	if (--pmap->pm_count == 0) {
		for (u = SDT_ENTRIES, sdt = pmap->pm_stab; u != 0; sdt++, u--) {
			if (SDT_VALID(sdt)) {
				pa = *sdt & PG_FRAME;
				pmap_cache_ctrl(pa, pa + PAGE_SIZE, CACHE_DFL);
				uvm_pagefree(PHYS_TO_VM_PAGE(pa));
			}
		}
		pa = (paddr_t)pmap->pm_stab;
		pmap_cache_ctrl(pa, pa + PAGE_SIZE, CACHE_DFL);
		uvm_pagefree(PHYS_TO_VM_PAGE(pa));
		pool_put(&pmappool, pmap);
	}
}

/*
 * [MI]
 * Increase the pmap reference count.
 */
void
pmap_reference(pmap_t pmap)
{
	pmap->pm_count++;
}

/*
 * Virtual mapping/unmapping routines
 */

/*
 * [MI]
 * Establish a `va' to `pa' translation with protection `prot' in `pmap'.
 * The `flags' argument contains the expected usage protection of the
 * mapping (and may differ from the currently requested protection), as
 * well as a possible PMAP_WIRED flag.
 */
int
pmap_enter(pmap_t pmap, vaddr_t va, paddr_t pa, vm_prot_t prot, int flags)
{
	int s;
	pt_entry_t *pte, npte;
	paddr_t old_pa;
	pv_entry_t pv_e, head;
	boolean_t wired = (flags & PMAP_WIRED) != 0;
	struct vm_page *pg;

	DPRINTF(CD_ENT, ("pmap_enter(%p, %lx, %lx, %x, %x)\n",
	    pmap, va, pa, prot, flags));

	npte = m88k_protection(prot);

	/*
	 * Expand pmap to include this pte.
	 */
	if ((pte = pmap_pte(pmap, va)) == NULL) {
		if (pmap == pmap_kernel())
			pte = pmap_expand_kmap(va, flags & PMAP_CANFAIL);
		else
			pte = pmap_expand(pmap, va, flags & PMAP_CANFAIL);

		/* will only return NULL if PMAP_CANFAIL is set */
		if (pte == NULL) {
			DPRINTF(CD_ENT, ("failed (ENOMEM)\n"));
			return (ENOMEM);
		}
	}

	/*
	 * Special case if the physical page is already mapped at this address.
	 */
	old_pa = ptoa(PG_PFNUM(*pte));
	DPRINTF(CD_ENT, ("pmap_enter: old_pa %lx pte %x\n", old_pa, *pte));

	pg = PHYS_TO_VM_PAGE(pa);
	s = splvm();

	if (old_pa == pa) {
		/* May be changing its wired attributes or protection */
		if (wired && !(pmap_pte_w(pte)))
			pmap->pm_stats.wired_count++;
		else if (!wired && pmap_pte_w(pte))
			pmap->pm_stats.wired_count--;
	} else {
		/* Remove old mapping from the PV list if necessary. */
		if (PDT_VALID(pte))
			pmap_remove_pte(pmap, va, pte, NULL, FALSE);

		if (pg != NULL) {
			/*
			 * Enter the mapping in the PV list for this
			 * managed page.
			 */
			head = pg_to_pvh(pg);
			if (head->pv_pmap == NULL) {
				/*
				 * No mappings yet.
				 */
				head->pv_va = va;
				head->pv_pmap = pmap;
				head->pv_next = NULL;
				pg->mdpage.pv_flags = 0;
			} else {
				/*
				 * Add new pv_entry after header.
				 */
				pv_e = pool_get(&pvpool, PR_NOWAIT);
				if (pv_e == NULL) {
					/* Invalidate the old pte anyway */
					tlb_flush(pmap, va, PG_NV);

					if (flags & PMAP_CANFAIL) {
						splx(s);
						return (ENOMEM);
					} else
						panic("pmap_enter: "
						    "pvpool exhausted");
				}
				pv_e->pv_va = va;
				pv_e->pv_pmap = pmap;
				pv_e->pv_next = head->pv_next;
				head->pv_next = pv_e;
			}
		}

		/*
		 * And count the mapping.
		 */
		pmap->pm_stats.resident_count++;
		if (wired)
			pmap->pm_stats.wired_count++;
	} /* if (pa == old_pa) ... else */

	npte |= PG_V;
	if (wired)
		npte |= PG_W;

	if (prot & PROT_WRITE) {
		/*
		 * On 88110, do not mark writable mappings as dirty unless we
		 * know the page is dirty, or we are using the kernel pmap.
		 */
		if (CPU_IS88110 && pmap != pmap_kernel() &&
		    pg != NULL && (pg->mdpage.pv_flags & PG_M) == 0)
			npte |= PG_U;
		else
			npte |= PG_M_U;
	} else if (prot & PROT_MASK)
		npte |= PG_U;

	/*
	 * If outside physical memory, disable cache on this (device) page.
	 */
	if (pa >= last_addr)
		npte |= CACHE_INH;

	/*
	 * Invalidate pte temporarily to avoid being written
	 * back the modified bit and/or the reference bit by
	 * any other cpu.
	 */
	npte |= invalidate_pte(pte) & PG_M_U;
	npte |= pa;
	*pte = npte;
	tlb_flush(pmap, va, npte);
	DPRINTF(CD_ENT, ("pmap_enter: new pte %x\n", npte));

	/*
	 * Cache attribute flags
	 */
	if (pg != NULL) {
		if (flags & PROT_WRITE) {
			if (CPU_IS88110 && pmap != pmap_kernel())
				pg->mdpage.pv_flags |= PG_U;
			else
				pg->mdpage.pv_flags |= PG_M_U;
		} else if (flags & PROT_MASK)
			pg->mdpage.pv_flags |= PG_U;
	}

	splx(s);

	return 0;
}

/*
 * [MI]
 * Fast pmap_enter() version for pmap_kernel() and unmanaged pages.
 */
void
pmap_kenter_pa(vaddr_t va, paddr_t pa, vm_prot_t prot)
{
	pt_entry_t *pte, npte;

	DPRINTF(CD_ENT, ("pmap_kenter_pa(%lx, %lx, %x)\n", va, pa, prot));

	npte = m88k_protection(prot) | PG_W | PG_V;
#ifdef M88110
	if (CPU_IS88110 && m88k_protection(prot) != PG_RO)
		npte |= PG_M;
#endif
	/*
	 * If outside physical memory, disable cache on this (device) page.
	 */
	if (pa >= last_addr)
		npte |= CACHE_INH;

	/*
	 * Expand pmap to include this pte.
	 */
	if ((pte = pmap_pte(pmap_kernel(), va)) == NULL)
		pte = pmap_expand_kmap(va, 0);

	/*
	 * And count the mapping.
	 */
	pmap_kernel()->pm_stats.resident_count++;
	pmap_kernel()->pm_stats.wired_count++;

	invalidate_pte(pte);
	npte |= pa;
	*pte = npte;
	tlb_kflush(va, npte);
}

/*
 * [INTERNAL]
 * Remove the page at `va' in `pmap', which pte is pointed to by `pte', and
 * update the status of the vm_page matching this translation (if this is
 * indeed a managed page). Flush the tlb entry if `flush' is nonzero.
 */
void
pmap_remove_pte(pmap_t pmap, vaddr_t va, pt_entry_t *pte, struct vm_page *pg,
   boolean_t flush)
{
	pt_entry_t opte;
	pv_entry_t prev, cur, head;
	paddr_t pa;

	splassert(IPL_VM);
	DPRINTF(CD_RM, ("pmap_remove_pte(%p, %lx, %d)\n", pmap, va, flush));

	/*
	 * Update statistics.
	 */
	pmap->pm_stats.resident_count--;
	if (pmap_pte_w(pte))
		pmap->pm_stats.wired_count--;

	pa = ptoa(PG_PFNUM(*pte));

	/*
	 * Invalidate the pte.
	 */

	opte = invalidate_pte(pte) & PG_M_U;
	if (flush)
		tlb_flush(pmap, va, PG_NV);

	if (pg == NULL) {
		pg = PHYS_TO_VM_PAGE(pa);
		/* If this isn't a managed page, just return. */
		if (pg == NULL)
			return;
	}

	/*
	 * Remove the mapping from the pvlist for
	 * this physical page.
	 */
	head = pg_to_pvh(pg);

#ifdef DIAGNOSTIC
	if (head->pv_pmap == NULL)
		panic("pmap_remove_pte(%p, %p, %p, %p/%p, %d): null pv_list",
		   pmap, (void *)va, pte, (void *)pa, pg, flush);
#endif

	prev = NULL;
	for (cur = head; cur != NULL; cur = cur->pv_next) {
		if (cur->pv_va == va && cur->pv_pmap == pmap)
			break;
		prev = cur;
	}
	if (cur == NULL) {
		panic("pmap_remove_pte(%p, %p, %p, %p, %d): mapping for va "
		    "(pa %p) not in pv list at %p",
		    pmap, (void *)va, pte, pg, flush, (void *)pa, head);
	}

	if (prev == NULL) {
		/*
		 * Handler is the pv_entry. Copy the next one
		 * to handler and free the next one (we can't
		 * free the handler)
		 */
		cur = cur->pv_next;
		if (cur != NULL) {
			*head = *cur;
			pool_put(&pvpool, cur);
		} else {
			head->pv_pmap = NULL;
			/*
			 * This page is no longer in use, and is likely
			 * to be reused soon; since it may still have
			 * dirty cache lines and may be used for I/O
			 * (and risk being invalidated by the bus_dma
			 * code without getting a chance of writeback),
			 * we make sure the page gets written back.
			 */
			pmap_clean_page(pa);
		}
	} else {
		prev->pv_next = cur->pv_next;
		pool_put(&pvpool, cur);
	}

	/* Update saved attributes for managed page */
	pg->mdpage.pv_flags |= opte;
}

/*
 * [INTERNAL]
 * Removes all mappings within the `sva'..`eva' range in `pmap'.
 */
void
pmap_remove_range(pmap_t pmap, vaddr_t sva, vaddr_t eva)
{
	vaddr_t va, eseg;
	pt_entry_t *pte;

	DPRINTF(CD_RM, ("pmap_remove_range(%p, %lx, %lx)\n", pmap, sva, eva));

	/*
	 * Loop through the range in PAGE_SIZE increments.
	 */
	va = sva;
	while (va != eva) {
		sdt_entry_t *sdt;

		eseg = (va & SDT_MASK) + (1 << SDT_SHIFT);
		if (eseg > eva || eseg == 0)
			eseg = eva;

		sdt = SDTENT(pmap, va);
		/* If no segment table, skip a whole segment */
		if (!SDT_VALID(sdt))
			va = eseg;
		else {
			pte = sdt_pte(sdt, va);
			while (va != eseg) {
				if (PDT_VALID(pte))
					pmap_remove_pte(pmap, va, pte, NULL,
					    TRUE);
				va += PAGE_SIZE;
				pte++;
			}
		}
	}
}

/*
 * [MI]
 * Removes all mappings within the `sva'..`eva' range in `pmap'.
 */
void
pmap_remove(pmap_t pmap, vaddr_t sva, vaddr_t eva)
{
	int s;

	KERNEL_LOCK();
	s = splvm();
	pmap_remove_range(pmap, sva, eva);
	splx(s);
	KERNEL_UNLOCK();
}

/*
 * [MI]
 * Fast pmap_remove() version for pmap_kernel() and unmanaged pages.
 */
void
pmap_kremove(vaddr_t va, vsize_t len)
{
	vaddr_t e, eseg;

	DPRINTF(CD_RM, ("pmap_kremove(%lx, %lx)\n", va, len));

	e = va + len;
	while (va != e) {
		sdt_entry_t *sdt;
		pt_entry_t *pte, opte;

		eseg = (va & SDT_MASK) + (1 << SDT_SHIFT);
		if (eseg > e || eseg == 0)
			eseg = e;

		sdt = SDTENT(pmap_kernel(), va);

		/* If no segment table, skip a whole segment */
		if (!SDT_VALID(sdt))
			va = eseg;
		else {
			pte = sdt_pte(sdt, va);
			while (va != eseg) {
				if (PDT_VALID(pte)) {
					/* Update the counts */
					pmap_kernel()->pm_stats.resident_count--;
					pmap_kernel()->pm_stats.wired_count--;

					opte = invalidate_pte(pte);
					tlb_kflush(va, PG_NV);

					/*
					 * Make sure the page is written back
					 * if it was cached.
					 */
					if ((opte & (CACHE_INH | CACHE_WT)) ==
					    0)
						pmap_clean_page(
						    ptoa(PG_PFNUM(opte)));
				}
				va += PAGE_SIZE;
				pte++;
			}
		}
	}
}

/*
 * [INTERNAL]
 * Removes all mappings of managed page `pg'.
 */
void
pmap_remove_page(struct vm_page *pg)
{
	pt_entry_t *pte;
	pv_entry_t head, pvep;
	vaddr_t va;
	pmap_t pmap;
	int s;

	DPRINTF(CD_RMPG, ("pmap_remove_page(%p)\n", pg));

	s = splvm();
	/*
	 * Walk down PV list, removing all mappings.
	 */
	pvep = head = pg_to_pvh(pg);
	while (pvep != NULL && (pmap = pvep->pv_pmap) != NULL) {
		va = pvep->pv_va;
		pte = pmap_pte(pmap, va);

		if (pte == NULL || !PDT_VALID(pte)) {
			pvep = pvep->pv_next;
			continue;	/* no page mapping */
		}

		pmap_remove_pte(pmap, va, pte, pg, TRUE);
		pvep = head;
		/*
		 * Do not free any empty page tables.
		 */
	}
	splx(s);
}

/*
 * [MI]
 * Strengthens the protection of the `sva'..`eva' range within `pmap' to `prot'.
 */
void
pmap_protect(pmap_t pmap, vaddr_t sva, vaddr_t eva, vm_prot_t prot)
{
	int s;
	pt_entry_t *pte, ap, opte, npte;
	vaddr_t va, eseg;

	if ((prot & PROT_READ) == 0) {
		pmap_remove(pmap, sva, eva);
		return;
	}

	ap = m88k_protection(prot);

	s = splvm();
	/*
	 * Loop through the range in PAGE_SIZE increments.
	 */
	va = sva;
	while (va != eva) {
		sdt_entry_t *sdt;

		eseg = (va & SDT_MASK) + (1 << SDT_SHIFT);
		if (eseg > eva || eseg == 0)
			eseg = eva;

		sdt = SDTENT(pmap, va);
		/* If no segment table, skip a whole segment */
		if (!SDT_VALID(sdt))
			va = eseg;
		else {
			pte = sdt_pte(sdt, va);
			while (va != eseg) {
				if (PDT_VALID(pte)) {
					/*
					 * Invalidate pte temporarily to avoid
					 * the modified bit and/or the
					 * reference bit being written back by
					 * any other cpu.
					 */
					opte = invalidate_pte(pte);
					npte = ap | (opte & ~PG_PROT);
					*pte = npte;
					tlb_flush(pmap, va, npte);
				}
				va += PAGE_SIZE;
				pte++;
			}
		}
	}
	splx(s);
}

/*
 * [MI]
 * Removes the wired state of the page at `va' in `pmap'.
 */
void
pmap_unwire(pmap_t pmap, vaddr_t va)
{
	pt_entry_t *pte;

	pte = pmap_pte(pmap, va);
	if (pmap_pte_w(pte)) {
		pmap->pm_stats.wired_count--;
		*pte &= ~PG_W;
	}
}

/*
 * vm_page management routines
 */

/*
 * [MI]
 * Copies vm_page `srcpg' to `dstpg'.
 */
void
pmap_copy_page(struct vm_page *srcpg, struct vm_page *dstpg)
{
	paddr_t src = VM_PAGE_TO_PHYS(srcpg);
	paddr_t dst = VM_PAGE_TO_PHYS(dstpg);

	DPRINTF(CD_COPY, ("pmap_copy_page(%p,%p) pa %lx %lx\n",
	    srcpg, dstpg, src, dst));
	curcpu()->ci_copypage((vaddr_t)src, (vaddr_t)dst);

	if (KERNEL_APR_CMODE == CACHE_DFL)
		cmmu_dcache_wb(cpu_number(), dst, PAGE_SIZE);
}

/*
 * [MI]
 * Clears vm_page `pg'.
 */
void
pmap_zero_page(struct vm_page *pg)
{
	paddr_t pa = VM_PAGE_TO_PHYS(pg);

	DPRINTF(CD_ZERO, ("pmap_zero_page(%p) pa %lx\n", pg, pa));
	curcpu()->ci_zeropage((vaddr_t)pa);

	if (KERNEL_APR_CMODE == CACHE_DFL)
		cmmu_dcache_wb(cpu_number(), pa, PAGE_SIZE);
}

/*
 * [INTERNAL]
 * Set the PG_RO bit in the pte of all mappings of `pg'.
 */
void
pmap_write_protect(struct vm_page *pg)
{
	pv_entry_t head, pvep;
	pt_entry_t *pte, npte, opte;
	pmap_t pmap;
	int s;
	vaddr_t va;

	DPRINTF(CD_WP, ("pmap_write_protect(%p)\n", pg));

	s = splvm();

	head = pg_to_pvh(pg);
	if (head->pv_pmap != NULL) {
		/* for each listed pmap, update the affected bits */
		for (pvep = head; pvep != NULL; pvep = pvep->pv_next) {
			pmap = pvep->pv_pmap;
			va = pvep->pv_va;
			pte = pmap_pte(pmap, va);

			/*
			 * Check for existing and valid pte
			 */
			if (pte == NULL || !PDT_VALID(pte))
				continue;	 /* no page mapping */
#ifdef PMAPDEBUG
			if (ptoa(PG_PFNUM(*pte)) != VM_PAGE_TO_PHYS(pg))
				panic("pmap_write_protect: pte %08x in pmap %p doesn't point to page %p@%lx",
				    *pte, pmap, pg, VM_PAGE_TO_PHYS(pg));
#endif

			/*
			 * Update bits
			 */
			opte = *pte;
			npte = opte | PG_RO;

			/*
			 * Invalidate pte temporarily to avoid the modified bit
			 * and/or the reference being written back by any other
			 * cpu.
			 */
			if (npte != opte) {
				invalidate_pte(pte);
				*pte = npte;
				tlb_flush(pmap, va, npte);
			}
		}
	}

	splx(s);
}

/*
 * [INTERNAL]
 * Checks for `bit' being set in at least one pte of all mappings of `pg'.
 * The flags summary at the head of the pv list is checked first, and will
 * be set if it wasn't but the bit is found set in one pte.
 * Returns TRUE if the bit is found, FALSE if not.
 */
boolean_t
pmap_testbit(struct vm_page *pg, int bit)
{
	pv_entry_t head, pvep;
	pt_entry_t *pte;
	pmap_t pmap;
	int s;

	DPRINTF(CD_TBIT, ("pmap_testbit(%p, %x): ", pg, bit));

	s = splvm();

	if (pg->mdpage.pv_flags & bit) {
		/* we've already cached this flag for this page,
		   no use looking further... */
		DPRINTF(CD_TBIT, ("cached\n"));
		splx(s);
		return (TRUE);
	}

	head = pg_to_pvh(pg);
	if (head->pv_pmap != NULL) {
		/* for each listed pmap, check modified bit for given page */
		for (pvep = head; pvep != NULL; pvep = pvep->pv_next) {
			pmap = pvep->pv_pmap;

			pte = pmap_pte(pmap, pvep->pv_va);
			if (pte == NULL || !PDT_VALID(pte))
				continue;

#ifdef PMAPDEBUG
			if (ptoa(PG_PFNUM(*pte)) != VM_PAGE_TO_PHYS(pg))
				panic("pmap_testbit: pte %08x in pmap %p doesn't point to page %p@%lx",
				    *pte, pmap, pg, VM_PAGE_TO_PHYS(pg));
#endif

			if ((*pte & bit) != 0) {
				pg->mdpage.pv_flags |= bit;
				DPRINTF(CD_TBIT, ("found\n"));
				splx(s);
				return (TRUE);
			}
		}
	}

	DPRINTF(CD_TBIT, ("not found\n"));
	splx(s);
	return (FALSE);
}

/*
 * [INTERNAL]
 * Clears `bit' in the pte of all mapping of `pg', as well as in the flags
 * summary at the head of the pv list.
 * Returns TRUE if the bit was found set in either a mapping or the summary,
 * FALSE if not.
 */
boolean_t
pmap_unsetbit(struct vm_page *pg, int bit)
{
	boolean_t rv = FALSE;
	pv_entry_t head, pvep;
	pt_entry_t *pte, opte, npte;
	pmap_t pmap;
	int s;
	vaddr_t va;

	DPRINTF(CD_USBIT, ("pmap_unsetbit(%p, %x): ", pg, bit));

	s = splvm();

	/*
	 * Clear saved attributes
	 */
	if (pg->mdpage.pv_flags & bit) {
		pg->mdpage.pv_flags ^= bit;
		rv = TRUE;
	}

	head = pg_to_pvh(pg);
	if (head->pv_pmap != NULL) {
		/* for each listed pmap, update the specified bit */
		for (pvep = head; pvep != NULL; pvep = pvep->pv_next) {
			pmap = pvep->pv_pmap;
			va = pvep->pv_va;
			pte = pmap_pte(pmap, va);

			/*
			 * Check for existing and valid pte
			 */
			if (pte == NULL || !PDT_VALID(pte))
				continue;	 /* no page mapping */
#ifdef PMAPDEBUG
			if (ptoa(PG_PFNUM(*pte)) != VM_PAGE_TO_PHYS(pg))
				panic("pmap_unsetbit: pte %08x in pmap %p doesn't point to page %p@%lx",
				    *pte, pmap, pg, VM_PAGE_TO_PHYS(pg));
#endif

			/*
			 * Update bits
			 */
			opte = *pte;
			if (opte & bit) {
				/*
				 * Invalidate pte temporarily to avoid the
				 * specified bit being written back by any
				 * other cpu.
				 */
				invalidate_pte(pte);
				npte = opte ^ bit;
				*pte = npte;
				tlb_flush(pmap, va, npte);
				rv = TRUE;
			}
		}
	}
	splx(s);

	DPRINTF(CD_USBIT, (rv ? "TRUE\n" : "FALSE\n"));
	return (rv);
}

/*
 * [MI]
 * Checks whether `pg' is dirty.
 * Returns TRUE if there is at least one mapping of `pg' with the modified
 * bit set in its pte, FALSE if not.
 */
boolean_t
pmap_is_modified(struct vm_page *pg)
{
#ifdef M88110
	/*
	 * Since on 88110 PG_M bit tracking is done in software, we can
	 * trust the page flags without having to walk the individual
	 * ptes in case the page flags are behind actual usage.
	 */
	if (CPU_IS88110) {
		boolean_t rc = FALSE;

		if (pg->mdpage.pv_flags & PG_M)
			rc = TRUE;
		DPRINTF(CD_TBIT, ("pmap_is_modified(%p) -> %x\n", pg, rc));
		return (rc);
	}
#endif

	return pmap_testbit(pg, PG_M);
}

/*
 * [MI]
 * Checks whether `pg' is in use.
 * Returns TRUE if there is at least one mapping of `pg' with the used bit
 * set in its pte, FALSE if not.
 */
boolean_t
pmap_is_referenced(struct vm_page *pg)
{
	return pmap_testbit(pg, PG_U);
}

/*
 * [MI]
 * Strengthens protection of `pg' to `prot'.
 */
void
pmap_page_protect(struct vm_page *pg, vm_prot_t prot)
{
	if ((prot & PROT_READ) == PROT_NONE)
		pmap_remove_page(pg);
	else if ((prot & PROT_WRITE) == PROT_NONE)
		pmap_write_protect(pg);
}

/*
 * Miscellaneous routines
 */

/*
 * [INTERNAL]
 * Writeback the data cache for the given page, on all processors.
 */
void
pmap_clean_page(paddr_t pa)
{
	struct cpu_info *ci;
#ifdef MULTIPROCESSOR
	CPU_INFO_ITERATOR cpu;
#endif

	if (KERNEL_APR_CMODE != CACHE_DFL && USERLAND_APR_CMODE != CACHE_DFL)
		return;

#ifdef MULTIPROCESSOR
	CPU_INFO_FOREACH(cpu, ci)
#else
	ci = curcpu();
#endif
	/* CPU_INFO_FOREACH(cpu, ci) */
		cmmu_dcache_wb(ci->ci_cpuid, pa, PAGE_SIZE);
}

/*
 * [MI]
 * Flushes instruction cache for the range `va'..`va'+`len' in proc `p'.
 */
void
pmap_proc_iflush(struct process *pr, vaddr_t va, vsize_t len)
{
	pmap_t pmap = vm_map_pmap(&pr->ps_vmspace->vm_map);
	paddr_t pa;
	vsize_t count;
	struct cpu_info *ci;

	if (KERNEL_APR_CMODE != CACHE_DFL && USERLAND_APR_CMODE != CACHE_DFL)
		return;

	while (len != 0) {
		count = min(len, PAGE_SIZE - (va & PAGE_MASK));
		if (pmap_extract(pmap, va, &pa)) {
#ifdef MULTIPROCESSOR
			CPU_INFO_ITERATOR cpu;

			CPU_INFO_FOREACH(cpu, ci)
#else
			ci = curcpu();
#endif
			/* CPU_INFO_FOREACH(cpu, ci) */ {
				cmmu_dcache_wb(ci->ci_cpuid, pa, count);
				/* XXX this should not be necessary, */
				/* XXX I$ is configured to snoop D$ */
				cmmu_icache_inv(ci->ci_cpuid, pa, count);
			}
		}
		va += count;
		len -= count;
	}
}

#ifdef M88110
/*
 * [INTERNAL]
 * Updates the pte mapping `va' in `pmap' upon write fault, to set the
 * modified bit in the pte (the 88110 MMU doesn't do this and relies upon
 * the kernel to achieve this).
 * Returns TRUE if the page was indeed writeable but not marked as dirty,
 * FALSE if this is a genuine write fault.
 */
int
pmap_set_modify(pmap_t pmap, vaddr_t va)
{
	pt_entry_t *pte;
	paddr_t pa;
	vm_page_t pg;

	pte = pmap_pte(pmap, va);
#ifdef DEBUG
	if (pte == NULL)
		panic("NULL pte on write fault??");
#endif

	/* Not a first write to a writable page */
	if ((*pte & (PG_M | PG_RO)) != 0)
		return (FALSE);

	/* Mark the page as dirty */
	*pte |= PG_M;
	pa = *pte & PG_FRAME;
	pg = PHYS_TO_VM_PAGE(pa);
#ifdef DIAGNOSTIC
	if (pg == NULL)
		panic("Write fault to unmanaged page %p", (void *)pa);
#endif

	pg->mdpage.pv_flags |= PG_M_U;

	tlb_flush(pmap, va, *pte);

	return (TRUE);
}
#endif

/*
 * [MD PUBLIC]
 * Change the cache control bits of the address range `sva'..`eva' in
 * pmap_kernel to `mode'.
 */
void
pmap_cache_ctrl(vaddr_t sva, vaddr_t eva, u_int mode)
{
	int s;
	pt_entry_t *pte, opte, npte;
	vaddr_t va;
	paddr_t pa;
	cpuid_t cpu;

	DPRINTF(CD_CACHE, ("pmap_cache_ctrl(%lx, %lx, %x)\n",
	    sva, eva, mode));

	s = splvm();
	for (va = sva; va != eva; va += PAGE_SIZE) {
		if ((pte = pmap_pte(pmap_kernel(), va)) == NULL)
			continue;
		DPRINTF(CD_CACHE, ("cache_ctrl: pte@%p\n", pte));

		/*
		 * Data cache should be copied back and invalidated if
		 * the old mapping was cached and the new isn't, or if
		 * we are downgrading from writeback to writethrough.
		 */
		if (((*pte & CACHE_INH) == 0 && (mode & CACHE_INH) != 0) ||
		    ((*pte & CACHE_WT) == 0 && (mode & CACHE_WT) != 0)) {
			pa = ptoa(PG_PFNUM(*pte));
#ifdef MULTIPROCESSOR
			for (cpu = 0; cpu < MAX_CPUS; cpu++)
				if (ISSET(m88k_cpus[cpu].ci_flags, CIF_ALIVE)) {
#else
			cpu = cpu_number();
#endif
					if (mode & CACHE_INH)
						cmmu_cache_wbinv(cpu,
						    pa, PAGE_SIZE);
					else if (KERNEL_APR_CMODE == CACHE_DFL ||
					    USERLAND_APR_CMODE == CACHE_DFL)
						cmmu_dcache_wb(cpu,
						    pa, PAGE_SIZE);
#ifdef MULTIPROCESSOR
				}
#endif
		}

		/*
		 * Invalidate pte temporarily to avoid being written back
		 * the modified bit and/or the reference bit by any other cpu.
		 */

		opte = invalidate_pte(pte);
		npte = (opte & ~CACHE_MASK) | mode;
		*pte = npte;
		tlb_kflush(va, npte);
	}
	splx(s);
}

/*
 * [MD PUBLIC]
 * Change the cache control bits of all mappings of the given physical page to
 * disable cached accesses.
 */
void
pmap_page_uncache(paddr_t pa)
{
	struct vm_page *pg = PHYS_TO_VM_PAGE(pa);
	struct pmap *pmap;
	pv_entry_t head, pvep;
	pt_entry_t *pte, opte, npte;
	vaddr_t va;
	int s;

	s = splvm();
	head = pg_to_pvh(pg);
	if (head->pv_pmap != NULL) {
		for (pvep = head; pvep != NULL; pvep = pvep->pv_next) {
			pmap = pvep->pv_pmap;
			va = pvep->pv_va;
			pte = pmap_pte(pmap, va);

			if (pte == NULL || !PDT_VALID(pte))
				continue;	 /* no page mapping */
			opte = *pte;
			if ((opte & CACHE_MASK) != CACHE_INH) {
				/*
				 * Skip the direct mapping; it will be changed
				 * by the pmap_cache_ctrl() call below.
				 */
				if (pmap == pmap_kernel() && va == pa)
					continue;
				/*
				 * Invalidate pte temporarily to avoid the
				 * specified bit being written back by any
				 * other cpu.
				 */
				invalidate_pte(pte);
				npte = (opte & ~CACHE_MASK) | CACHE_INH;
				*pte = npte;
				tlb_flush(pmap, va, npte);
			}
		}
	}
	splx(s);
	pmap_cache_ctrl(pa, pa + PAGE_SIZE, CACHE_INH);
}

/*
 * [MI]
 * Marks a "direct" page as unused.
 */
vm_page_t
pmap_unmap_direct(vaddr_t va)
{
	paddr_t pa = (paddr_t)va;
	vm_page_t pg = PHYS_TO_VM_PAGE(pa);

	pmap_clean_page(pa);

	return pg;
}
