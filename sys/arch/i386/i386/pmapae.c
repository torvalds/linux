/*	$OpenBSD: pmapae.c,v 1.77 2025/08/15 13:40:43 kettenis Exp $	*/

/*
 * Copyright (c) 2006-2008 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
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
 *	from OpenBSD: pmap.c,v 1.85 2005/11/18 17:05:04 brad Exp
 */

/*
 * pmap.c: i386 pmap module rewrite
 * Chuck Cranor <chuck@ccrc.wustl.edu>
 * 11-Aug-97
 *
 * history of this pmap module: in addition to my own input, i used
 *    the following references for this rewrite of the i386 pmap:
 *
 * [1] the NetBSD i386 pmap.   this pmap appears to be based on the
 *     BSD hp300 pmap done by Mike Hibler at University of Utah.
 *     it was then ported to the i386 by William Jolitz of UUNET
 *     Technologies, Inc.   Then Charles M. Hannum of the NetBSD
 *     project fixed some bugs and provided some speed ups.
 *
 * [2] the FreeBSD i386 pmap.   this pmap seems to be the
 *     Hibler/Jolitz pmap, as modified for FreeBSD by John S. Dyson
 *     and David Greenman.
 *
 * [3] the Mach pmap.   this pmap, from CMU, seems to have migrated
 *     between several processors.   the VAX version was done by
 *     Avadis Tevanian, Jr., and Michael Wayne Young.    the i386
 *     version was done by Lance Berc, Mike Kupfer, Bob Baron,
 *     David Golub, and Richard Draves.    the alpha version was
 *     done by Alessandro Forin (CMU/Mach) and Chris Demetriou
 *     (NetBSD/alpha).
 */
/*
 * PAE support
 * Michael Shalayeff <mickey@lucifier.net>
 *
 * This module implements PAE mode for i386.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/pool.h>
#include <sys/user.h>
#include <sys/mutex.h>

#include <uvm/uvm.h>

#include <machine/specialreg.h>

#include <dev/isa/isareg.h>
#include <i386/isa/isa_machdep.h>

#include "ksyms.h"

/* #define PMAPAE_DEBUG */

#ifdef PMAPAE_DEBUG
#define DPRINTF(x...)	do { printf(x); } while(0)
#else
#define DPRINTF(x...)
#endif	/* PMAPAE_DEBUG */

/*
 * this file contains the code for the "pmap module."   the module's
 * job is to manage the hardware's virtual to physical address mappings.
 * note that there are two levels of mapping in the VM system:
 *
 *  [1] the upper layer of the VM system uses vm_map's and vm_map_entry's
 *      to map ranges of virtual address space to objects/files.  for
 *      example, the vm_map may say: "map VA 0x1000 to 0x22000 read-only
 *      to the file /bin/ls starting at offset zero."   note that
 *      the upper layer mapping is not concerned with how individual
 *      vm_pages are mapped.
 *
 *  [2] the lower layer of the VM system (the pmap) maintains the mappings
 *      from virtual addresses.   it is concerned with which vm_page is
 *      mapped where.   for example, when you run /bin/ls and start
 *      at page 0x1000 the fault routine may lookup the correct page
 *      of the /bin/ls file and then ask the pmap layer to establish
 *      a mapping for it.
 *
 * note that information in the lower layer of the VM system can be
 * thrown away since it can easily be reconstructed from the info
 * in the upper layer.
 *
 * data structures we use include:
 *
 *  - struct pmap: describes the address space of one thread
 *  - struct pv_entry: describes one <PMAP,VA> mapping of a PA
 *  - struct pv_head: there is one pv_head per managed page of
 *	physical memory.   the pv_head points to a list of pv_entry
 *	structures which describe all the <PMAP,VA> pairs that this
 *      page is mapped in.    this is critical for page based operations
 *      such as pmap_page_protect() [change protection on _all_ mappings
 *      of a page]
 */
/*
 * i386 PAE hardware Page Tables structure:
 *
 * the i386 PAE Page Table is a three-level PT which maps 4GB of VA.
 * the pagesize is 4K (4096 [0x1000] bytes) or 2MB.
 *
 * the first level table is called "page directory index" and consists
 * of 4 page directory index entries (PDIE) each 64 bits in size.
 *
 * the second level table is called a "page directory" and it contains
 * 512 page directory entries (PDEs).   each PDE is
 * 8 bytes (a long long), so a PD fits in a single 4K page.   this page is
 * the page directory page (PDP).  each PDE in a PDP maps 1GB of space
 * (512 * 2MB = 1GB).   a PDE contains the physical address of the
 * second level table: the page table.   or, if 2MB pages are being used,
 * then the PDE contains the PA of the 2MB page being mapped.
 *
 * a page table consists of 512 page table entries (PTEs).  each PTE is
 * 8 bytes (a long long), so a page table also fits in a single 4K page.
 * a 4K page being used as a page table is called a page table page (PTP).
 * each PTE in a PTP maps one 4K page (512 * 4K = 2MB).   a PTE contains
 * the physical address of the page it maps and some flag bits (described
 * below).
 *
 * the processor has a special register, "cr3", which points to the
 * the PDP which is currently controlling the mappings of the virtual
 * address space.
 *
 * the following picture shows the translation process for a 4K page:
 *
 * %cr3 register [PA of PDPT]
 *  |
 *  |  bits <31-30> of VA
 *  |  index the DPE (0-3)
 *  |        |
 *  v        v
 *  +-----------+
 *  |  PDP Ptr  |
 *  | 4 entries |
 *  +-----------+
 *       |
 *    PA of PDP
 *       |
 *       |
 *       |  bits <29-21> of VA       bits <20-12> of VA   bits <11-0>
 *       |  index the PDP (0 - 512)  index the PTP        are the page offset
 *       |        |                         |                    |
 *       |        v                         |                    |
 *       +-->+---------+                    |                    |
 *           | PD Page |    PA of           v                    |
 *           |         |-----PTP----->+------------+             |
 *           | 512 PDE |              | page table |--PTE--+     |
 *           | entries |              | (aka PTP)  |       |     |
 *           +---------+              |  512 PTE   |       |     |
 *                                    |  entries   |       |     |
 *                                    +------------+       |     |
 *                                                         |     |
 *                                              bits <35-12>   bits <11-0>
 *                                               p h y s i c a l  a d d r
 *
 * the i386 caches PTEs in a TLB.   it is important to flush out old
 * TLB mappings when making a change to a mapping.   writing to the
 * %cr3 will flush the entire TLB.    newer processors also have an
 * instruction that will invalidate the mapping of a single page (which
 * is useful if you are changing a single mapping because it preserves
 * all the cached TLB entries).
 *
 * as shows, bits 31-12 of the PTE contain PA of the page being mapped.
 * the rest of the PTE is defined as follows:
 *   bit#	name	use
 *   63		NX	no-execute bit (0=ITLB, 1=DTLB), optional
 *   11		n/a	available for OS use, hardware ignores it
 *   10		n/a	available for OS use, hardware ignores it
 *   9		n/a	available for OS use, hardware ignores it
 *   8		G	global bit (see discussion below)
 *   7		PS	page size [for PDEs] (0=4k, 1=4M <if supported>)
 *   6		D	dirty (modified) page
 *   5		A	accessed (referenced) page
 *   4		PCD	cache disable
 *   3		PWT	prevent write through (cache)
 *   2		U/S	user/supervisor bit (0=supervisor only, 1=both u&s)
 *   1		R/W	read/write bit (0=read only, 1=read-write)
 *   0		P	present (valid)
 *
 * notes:
 *  - on the i386 the R/W bit is ignored if processor is in supervisor
 *    state (bug!)
 *  - PS is only supported on newer processors
 *  - PTEs with the G bit are global in the sense that they are not
 *    flushed from the TLB when %cr3 is written (to flush, use the
 *    "flush single page" instruction).   this is only supported on
 *    newer processors.    this bit can be used to keep the kernel's
 *    TLB entries around while context switching.   since the kernel
 *    is mapped into all processes at the same place it does not make
 *    sense to flush these entries when switching from one process'
 *    pmap to another.
 */
/*
 * A pmap describes a process' 4GB virtual address space.  This
 * virtual address space can be broken up into 2048 2MB regions which
 * are described by PDEs in the PDP.  The PDEs are defined as follows:
 *
 * Ranges are inclusive -> exclusive, just like vm_map_entry start/end.
 * The following assumes that KERNBASE is 0xd0000000.
 *
 * PDE#s	VA range		Usage
 * 0->1660	0x0 -> 0xcf800000	user address space, note that the
 *					max user address is 0xcfbfe000
 *					the final two pages in the last 4MB
 *					used to be reserved for the UAREA
 *					but now are no longer used.
 * 1660		0xcf800000->		recursive mapping of PDP (used for
 *			0xd0000000	linear mapping of PTPs).
 * 1664->2044	0xd0000000->		kernel address space (constant
 *			0xff800000	across all pmaps/processes).
 * 2044		0xff800000->		"alternate" recursive PDP mapping
 *			<end>		(for other pmaps).
 *
 *
 * Note: A recursive PDP mapping provides a way to map all the PTEs for
 * a 4GB address space into a linear chunk of virtual memory.  In other
 * words, the PTE for page 0 is the first 8b mapped into the 2MB recursive
 * area.  The PTE for page 1 is the second 8b.  The very last 8b in the
 * 2MB range is the PTE that maps VA 0xffffe000 (the last page in a 4GB
 * address).
 *
 * All pmaps' PDs must have the same values in slots 1660->2043 so that
 * the kernel is always mapped in every process.  These values are loaded
 * into the PD at pmap creation time.
 *
 * At any one time only one pmap can be active on a processor.  This is
 * the pmap whose PDP is pointed to by processor register %cr3.  This pmap
 * will have all its PTEs mapped into memory at the recursive mapping
 * point (slots #1660-3 as show above).  When the pmap code wants to find the
 * PTE for a virtual address, all it has to do is the following:
 *
 * Address of PTE = (1660 * 2MB) + (VA / NBPG) * sizeof(pt_entry_t)
 *                = 0xcf800000 + (VA / 4096) * 8
 *
 * What happens if the pmap layer is asked to perform an operation
 * on a pmap that is not the one which is currently active?  In that
 * case we take the PA of the PDP of the non-active pmap and put it in
 * slots 2044-7 of the active pmap.  This causes the non-active pmap's
 * PTEs to get mapped in the final 4MB of the 4GB address space
 * (e.g. starting at 0xffc00000).
 *
 * The following figure shows the effects of the recursive PDP mapping:
 *
 *   PDP (%cr3->PDPTP)
 *   +----+
 *   |   0| -> PTP#0 that maps VA 0x0 -> 0x200000
 *   |    |
 *   |    |
 *   |1660| -> points back to PDP (%cr3) mapping VA 0xcf800000 -> 0xd0000000
 *   |1661|    (PDP is 4 pages)
 *   |1662|
 *   |1663|
 *   |1664| -> first kernel PTP (maps 0xd0000000 -> 0xe0200000)
 *   |    |
 *   |2044| -> points to alternate pmap's PDP (maps 0xff800000 -> end)
 *   |2045|
 *   |2046|
 *   |2047|
 *   +----+
 *
 * Note that the PDE#1660 VA (0xcf8033e0) is defined as "PTE_BASE".
 * Note that the PDE#2044 VA (0xff803fe0) is defined as "APTE_BASE".
 *
 * Starting at VA 0xcf8033e0 the current active PDPs (%cr3) act as a
 * PDPTP and reference four consecutively mapped pages:
 *
 * PTP#1660-3 == PDP(%cr3) => maps VA 0xcf800000 -> 0xd0000000
 *   +----+
 *   |   0| -> maps the contents of PTP#0 at VA 0xcf800000->0xcf801000
 *   |    |
 *   |    |
 *   |1660| -> maps the contents of PTP#1660 (the PDP) at VA 0xcfe7c000
 *   |1661|
 *   |1662|
 *   |1663|
 *   |1664| -> maps the contents of first kernel PTP
 *   |    |
 *   |2047|
 *   +----+
 *
 * Note that mapping of the PDP at PTP#1660's VA (0xcfe7c000) is
 * defined as "PDP_BASE".... within that mapping there are two
 * defines:
 *   "PDP_PDE" (0xcfe7f3e0) is the VA of the PDE in the PDP
 *      which points back to itself.
 *   "APDP_PDE" (0xfff02fe0) is the VA of the PDE in the PDP which
 *      establishes the recursive mapping of the alternate pmap.
 *      To set the alternate PDP, one just has to put the correct
 *	PA info in *APDP_PDE.
 *
 * Note that in the APTE_BASE space, the APDP appears at VA
 * "APDP_BASE" (0xffffc000).
 *
 * unfortunately, we cannot use recursive PDPT from the page tables
 * because cr3 is only 32 bits wide.
 *
 */
#define PG_FRAME	0xffffff000ULL	/* page frame mask */
#define PG_LGFRAME	0xfffe00000ULL	/* large (2M) page frame mask */

/*
 * Redefine the PDSHIFT and NBPD macros for PAE
 */
#undef PDSHIFT
#define PDSHIFT		21		/* page directory address shift */
#undef NBPD
#define NBPD		(1U << PDSHIFT)	/* # bytes mapped by PD (2MB) */

#define PDSHIFT86	22		/* for pmap86 transfer */

#undef PDSLOT_PTE
#define PDSLOT_PTE	(1660U)	/* 1660: for recursive PDP map */
#undef PDSLOT_KERN
#define PDSLOT_KERN	(1664U)	/* 1664: start of kernel space */
#undef PDSLOT_APTE
#define PDSLOT_APTE	(2044U)	/* 2044: alternative recursive slot */

/*
 * The following defines give the virtual addresses of various MMU
 * data structures:
 * PTE_BASE and APTE_BASE: the base VA of the linear PTE mappings
 * PDP_PDE and APDP_PDE: the VA of the PDE that points back to the PDP/APDP
 */
#define PTE_BASE	((pt_entry_t *) (PDSLOT_PTE * NBPD))
#define APTE_BASE	((pt_entry_t *) (PDSLOT_APTE * NBPD))
#define PDP_BASE ((pd_entry_t *)(((char *)PTE_BASE) + (PDSLOT_PTE * NBPG)))
#define APDP_BASE ((pd_entry_t *)(((char *)APTE_BASE) + (PDSLOT_APTE * NBPG)))
#define PDP_PDE		(PDP_BASE + PDSLOT_PTE)
#define APDP_PDE	(PDP_BASE + PDSLOT_APTE)

/*
 * pdei/ptei: generate index into PDP/PTP from a VA
 */
#define PD_MASK		0xffe00000	/* page directory address bits */
#define PT_MASK		0x001ff000	/* page table address bits */
#define pdei(VA)	(((VA) & PD_MASK) >> PDSHIFT)
#define ptei(VA)	(((VA) & PT_MASK) >> PGSHIFT)

#define PD_MASK86	0xffc00000	/* for pmap86 transfer */
#define PT_MASK86	0x003ff000	/* for pmap86 transfer */

/*
 * Mach derived conversion macros
 */
#define i386_round_pdr(x)	((((unsigned)(x)) + ~PD_MASK) & PD_MASK)

/*
 * various address macros
 *
 *  vtopte: return a pointer to the PTE mapping a VA
 */
#define vtopte(VA)	(PTE_BASE + atop((vaddr_t)VA))

/*
 * PTP macros:
 *   A PTP's index is the PD index of the PDE that points to it.
 *   A PTP's offset is the byte-offset in the PTE space that this PTP is at.
 *   A PTP's VA is the first VA mapped by that PTP.
 *
 * Note that NBPG == number of bytes in a PTP (4096 bytes == 512 entries)
 *           NBPD == number of bytes a PTP can map (2MB)
 */

#define ptp_i2o(I)	((I) * NBPG)	/* index => offset */
#define ptp_o2i(O)	((O) / NBPG)	/* offset => index */
#define ptp_i2v(I)	((I) * NBPD)	/* index => VA */
#define ptp_v2i(V)	((V) / NBPD)	/* VA => index (same as pdei) */

/*
 * Access PD and PT
 */
#define PDE(pm,i)	(((pd_entry_t *)(pm)->pm_pdir)[(i)])

/*
 * here we define the data types for PDEs and PTEs for PAE
 */
typedef u_int64_t pd_entry_t;		/* PDE */
typedef u_int64_t pt_entry_t;		/* PTE */

#define PG_NX	0x8000000000000000ULL	/* execute-disable */

/*
 * Number of PTEs per cache line. 8 byte pte, 64-byte cache line
 * Used to avoid false sharing of cache lines.
 */
#define NPTECL			8

/*
 * other data structures
 */

extern u_int32_t protection_codes[];	/* maps MI prot to i386 prot code */
extern int pmap_initialized;	/* pmap_init done yet? */

/* Segment boundaries */
extern vaddr_t kernel_text, etext, __rodata_start, erodata, __data_start;
extern vaddr_t edata, __bss_start, end, ssym, esym, PTmap;

/*
 * MULTIPROCESSOR: special VAs/ PTEs are actually allocated inside a
 * MAXCPUS*NPTECL array of PTEs, to avoid cache line thrashing
 * due to false sharing.
 */

#ifdef MULTIPROCESSOR
#define PTESLEW(pte, id) ((pte)+(id)*NPTECL)
#define VASLEW(va,id) ((va)+(id)*NPTECL*NBPG)
#else
#define PTESLEW(pte, id) (pte)
#define VASLEW(va,id) (va)
#endif

/*
 * special VAs and the PTEs that map them
 */

static pt_entry_t *csrc_pte, *cdst_pte, *zero_pte, *ptp_pte, *flsh_pte;
extern caddr_t pmap_csrcp, pmap_cdstp, pmap_zerop, pmap_ptpp, pmap_flshp;

extern int pmap_pg_g;
extern int pmap_pg_wc;
extern struct pmap_head pmaps;
extern struct mutex pmaps_lock;

extern uint32_t	cpu_meltdown;

/*
 * local prototypes
 */
struct vm_page	*pmap_alloc_ptp_pae(struct pmap *, int, pt_entry_t);
struct vm_page	*pmap_get_ptp_pae(struct pmap *, int);
void		 pmap_drop_ptp_pae(struct pmap *, vaddr_t, struct vm_page *,
    pt_entry_t *);
pt_entry_t	*pmap_map_ptes_pae(struct pmap *);
void		 pmap_unmap_ptes_pae(struct pmap *);
void		 pmap_do_remove_pae(struct pmap *, vaddr_t, vaddr_t, int);
void		 pmap_remove_ptes_pae(struct pmap *, struct vm_page *,
		     vaddr_t, vaddr_t, vaddr_t, int, struct pv_entry **);
void		 pmap_sync_flags_pte_pae(struct vm_page *, pt_entry_t);

static __inline u_int
pmap_pte2flags(pt_entry_t pte)
{
	return (((pte & PG_U) ? PG_PMAP_REF : 0) |
	    ((pte & PG_M) ? PG_PMAP_MOD : 0));
}

void
pmap_sync_flags_pte_pae(struct vm_page *pg, pt_entry_t pte)
{
	if (pte & (PG_U|PG_M)) {
		atomic_setbits_int(&pg->pg_flags, pmap_pte2flags(pte));
	}
}

/*
 * pmap_map_ptes: map a pmap's PTEs into KVM and lock them in
 *
 * => we lock enough pmaps to keep things locked in
 * => must be undone with pmap_unmap_ptes before returning
 */

pt_entry_t *
pmap_map_ptes_pae(struct pmap *pmap)
{
	pd_entry_t opde;

	/* the kernel's pmap is always accessible */
	if (pmap == pmap_kernel()) {
		return(PTE_BASE);
	}

	mtx_enter(&pmap->pm_mtx);

	/* if curpmap then we are always mapped */
	if (pmap_is_curpmap(pmap)) {
		return(PTE_BASE);
	}

	mtx_enter(&curcpu()->ci_curpmap->pm_apte_mtx);

	/* need to load a new alternate pt space into curpmap? */
	opde = *APDP_PDE;
#if defined(MULTIPROCESSOR) && defined(DIAGNOSTIC)
	if (pmap_valid_entry(opde))
		panic("pmap_map_ptes_pae: APTE valid");
#endif
	if (!pmap_valid_entry(opde) || (opde & PG_FRAME) != pmap->pm_pdidx[0]) {
		APDP_PDE[0] = pmap->pm_pdidx[0] | PG_RW | PG_V | PG_U | PG_M;
		APDP_PDE[1] = pmap->pm_pdidx[1] | PG_RW | PG_V | PG_U | PG_M;
		APDP_PDE[2] = pmap->pm_pdidx[2] | PG_RW | PG_V | PG_U | PG_M;
		APDP_PDE[3] = pmap->pm_pdidx[3] | PG_RW | PG_V | PG_U | PG_M;
		if (pmap_valid_entry(opde))
			pmap_apte_flush();
	}
	return(APTE_BASE);
}

/*
 * pmap_unmap_ptes: unlock the PTE mapping of "pmap"
 */

void
pmap_unmap_ptes_pae(struct pmap *pmap)
{
	if (pmap == pmap_kernel())
		return;

	if (!pmap_is_curpmap(pmap)) {
#if defined(MULTIPROCESSOR)
		APDP_PDE[0] = 0;
		APDP_PDE[1] = 0;
		APDP_PDE[2] = 0;
		APDP_PDE[3] = 0;
		pmap_apte_flush();
#endif
		mtx_leave(&curcpu()->ci_curpmap->pm_apte_mtx);
	}

	mtx_leave(&pmap->pm_mtx);
}

u_int32_t
pmap_pte_set_pae(vaddr_t va, paddr_t pa, u_int32_t bits)
{
	pt_entry_t pte, *ptep = vtopte(va);
	uint64_t nx;

	pa &= PMAP_PA_MASK;

	if (bits & PG_X)
		nx = 0;
	else
		nx = PG_NX;

	pte = i386_atomic_testset_uq(ptep, pa | bits | nx);  /* zap! */
	return (pte & ~PG_FRAME);
}

u_int32_t
pmap_pte_setbits_pae(vaddr_t va, u_int32_t set, u_int32_t clr)
{
	pt_entry_t *ptep = vtopte(va);
	pt_entry_t pte = *ptep;

	i386_atomic_testset_uq(ptep, (pte | set) & ~(pt_entry_t)clr);
	return (pte & ~PG_FRAME);
}

u_int32_t
pmap_pte_bits_pae(vaddr_t va)
{
	pt_entry_t *ptep = vtopte(va);

	return (*ptep & ~PG_FRAME);
}

paddr_t
pmap_pte_paddr_pae(vaddr_t va)
{
	pt_entry_t *ptep = vtopte(va);

	return (*ptep & PG_FRAME);
}

/*
 * Allocate a new PD for Intel's U-K.
 */
void
pmap_alloc_pdir_intel_pae(struct pmap *pmap)
{
	vaddr_t		 va;
	int		 i;

	KASSERT(pmap->pm_pdir_intel == 0);

	va = (vaddr_t)km_alloc(4 * NBPG, &kv_any, &kp_zero, &kd_waitok);
	if (va == 0)
		panic("kernel_map out of virtual space");
	pmap->pm_pdir_intel = va;
	if (!pmap_extract(pmap_kernel(), (vaddr_t)&pmap->pm_pdidx_intel,
	    &pmap->pm_pdirpa_intel))
		panic("can't locate PDPT");

	for (i = 0; i < 4; i++) {
		pmap->pm_pdidx_intel[i] = 0;
		if (!pmap_extract(pmap, va + i * NBPG,
		    (paddr_t *)&pmap->pm_pdidx_intel[i]))
			panic("can't locate PD page");

		pmap->pm_pdidx_intel[i] |= PG_V;

		DPRINTF("%s: pm_pdidx_intel[%d] = 0x%llx\n", __func__,
		    i, pmap->pm_pdidx_intel[i]);
	}
}

/*
 * Switch over to PAE page tables
 */
void
pmap_bootstrap_pae(void)
{
	extern int nkpde;
	struct pmap *kpm = pmap_kernel();
	struct vm_page *ptp;
	paddr_t ptaddr;
	u_int32_t bits, *pd = NULL;
	vaddr_t va, eva;
	pt_entry_t pte;

	if ((cpu_feature & CPUID_PAE) == 0 ||
	    (ecpu_feature & CPUID_NXE) == 0)
		return;

	cpu_pae = 1;

	DPRINTF("%s: pm_pdir 0x%x pm_pdirpa 0x%x pm_pdirsize %d\n", __func__,
	    (uint32_t)kpm->pm_pdir, (uint32_t)kpm->pm_pdirpa,
	    kpm->pm_pdirsize);

	va = (vaddr_t)kpm->pm_pdir;
	kpm->pm_pdidx[0] = (va + 0*NBPG - KERNBASE) | PG_V;
	kpm->pm_pdidx[1] = (va + 1*NBPG - KERNBASE) | PG_V;
	kpm->pm_pdidx[2] = (va + 2*NBPG - KERNBASE) | PG_V;
	kpm->pm_pdidx[3] = (va + 3*NBPG - KERNBASE) | PG_V;
	/* map pde recursively into itself */
	PDE(kpm, PDSLOT_PTE+0) = kpm->pm_pdidx[0] | PG_KW | PG_M | PG_U;
	PDE(kpm, PDSLOT_PTE+1) = kpm->pm_pdidx[1] | PG_KW | PG_M | PG_U;
	PDE(kpm, PDSLOT_PTE+2) = kpm->pm_pdidx[2] | PG_KW | PG_M | PG_U;
	PDE(kpm, PDSLOT_PTE+3) = kpm->pm_pdidx[3] | PG_KW | PG_M | PG_U;

	/* allocate new special PD before transferring all mappings. */
	if (kpm->pm_pdir_intel) {
		pd = (uint32_t *)kpm->pm_pdir_intel;
		kpm->pm_pdir_intel = kpm->pm_pdirpa_intel = 0;
		pmap_alloc_pdir_intel_pae(kpm);
	}

	/* transfer all kernel mappings over into pae tables */
	for (va = KERNBASE, eva = va + (nkpde << PDSHIFT86);
	    va < eva; va += PAGE_SIZE) {
		if (!pmap_valid_entry(PDE(kpm, pdei(va)))) {
			ptp = uvm_pagealloc(&kpm->pm_obj, va, NULL,
			    UVM_PGA_ZERO);
			if (ptp == NULL)
				panic("%s: uvm_pagealloc() failed", __func__);
			ptaddr = VM_PAGE_TO_PHYS(ptp);
			PDE(kpm, pdei(va)) = ptaddr | PG_KW | PG_V |
			    PG_U | PG_M;
			pmap_pte_set_86((vaddr_t)vtopte(va),
			    ptaddr, PG_KW | PG_V | PG_U | PG_M);

			/* count PTP as resident */
			kpm->pm_stats.resident_count++;
		}
		bits = pmap_pte_bits_86(va) | pmap_pg_g;

		/*
		 * At this point, ideally only kernel text should be executable.
		 * However, we need to leave the ISA hole executable to handle
		 * bios32, pcibios, and apmbios calls that may potentially
		 * happen later since we don't know (yet) which of those may be
		 * in use. Later (in biosattach), we will reset the permissions
		 * according to what we actually need.
		 */
		if ((va >= (vaddr_t)&kernel_text && va <= (vaddr_t)&etext) ||
		    (va >= (vaddr_t)atdevbase && va <=
		     (vaddr_t)(atdevbase + IOM_SIZE)))
			bits |= PG_X;
		else
			bits &= ~PG_X;

		if (pmap_valid_entry(bits))
			pmap_pte_set_pae(va, pmap_pte_paddr_86(va), bits);
	}

	/* Transfer special mappings */
	if (pd) {
		uint32_t	*ptp;
		uint32_t	 l1idx, l2idx;
		paddr_t		 npa;
		struct vm_page	*ptppg;

		for (va = KERNBASE, eva = va + (nkpde << PDSHIFT86); va < eva;
		    va += PAGE_SIZE) {
			l1idx = ((va & PT_MASK86) >> PGSHIFT);
			l2idx = ((va & PD_MASK86) >> PDSHIFT86);

			if (!pmap_valid_entry(pd[l2idx]))
				continue;

			npa = pd[l2idx]	& PMAP_PA_MASK;
			ptppg = PHYS_TO_VM_PAGE(npa);
			mtx_enter(&ptppg->mdpage.pv_mtx);

			/* still running on pmap86 */
			ptp = (uint32_t *)pmap_tmpmap_pa_86(npa);

			if (!pmap_valid_entry(ptp[l1idx])) {
				mtx_leave(&ptppg->mdpage.pv_mtx);
				pmap_tmpunmap_pa_86();
				continue;
			}
			DPRINTF("%s: va 0x%x l2idx %u 0x%x lx1idx %u 0x%x\n",
			    __func__, (uint32_t)va, l2idx, (uint32_t)pd[l2idx],
			    l1idx, (uint32_t)ptp[l1idx]);

			/* protection and cacheability */
			bits = ptp[l1idx] & (PG_PROT|PG_N|PG_WT);
			npa = ptp[l1idx] & PMAP_PA_MASK;

			/* still running on pmap86 */
			pmap_tmpunmap_pa_86();
			mtx_leave(&ptppg->mdpage.pv_mtx);

			/* enforce use of pmap86 */
			cpu_pae = 0;
			pmap_enter_special_pae(va, npa, 0, bits);
			cpu_pae = 1;

			if (--ptppg->wire_count == 1) {
				ptppg->wire_count = 0;
				uvm_pagerealloc(ptppg, NULL, 0);
				DPRINTF("%s: freeing PT page 0x%x\n", __func__,
				    (uint32_t)VM_PAGE_TO_PHYS(ptppg));
			}
		}
		km_free(pd, NBPG, &kv_any, &kp_dirty);
		DPRINTF("%s: freeing PDP 0x%x\n", __func__, (uint32_t)pd);
	}

	if (!cpu_paenable(&kpm->pm_pdidx[0])) {
		extern struct user *proc0paddr;

		proc0paddr->u_pcb.pcb_cr3 = kpm->pm_pdirpa =
		    (vaddr_t)kpm - KERNBASE;
		kpm->pm_pdirsize = 4 * NBPG;

		/* Reset cr3 for NMI task switch */
		cpu_update_nmi_cr3(kpm->pm_pdirpa);

		DPRINTF("%s: pm_pdir 0x%x pm_pdirpa 0x%x pm_pdirsize %d\n",
		    __func__, (uint32_t)kpm->pm_pdir, (uint32_t)kpm->pm_pdirpa,
		    kpm->pm_pdirsize);

		csrc_pte = vtopte(pmap_csrcp);
		cdst_pte = vtopte(pmap_cdstp);
		zero_pte = vtopte(pmap_zerop);
		ptp_pte = vtopte(pmap_ptpp);
		flsh_pte = vtopte(pmap_flshp);

		nkpde *= 2;
		nkptp_max = 2048 - PDSLOT_KERN - 4;

		pmap_pte_set_p = pmap_pte_set_pae;
		pmap_pte_setbits_p = pmap_pte_setbits_pae;
		pmap_pte_bits_p = pmap_pte_bits_pae;
		pmap_pte_paddr_p = pmap_pte_paddr_pae;
		pmap_clear_attrs_p = pmap_clear_attrs_pae;
		pmap_enter_p = pmap_enter_pae;
		pmap_enter_special_p = pmap_enter_special_pae;
		pmap_extract_p = pmap_extract_pae;
		pmap_growkernel_p = pmap_growkernel_pae;
		pmap_page_remove_p = pmap_page_remove_pae;
		pmap_do_remove_p = pmap_do_remove_pae;
		pmap_test_attrs_p = pmap_test_attrs_pae;
		pmap_unwire_p = pmap_unwire_pae;
		pmap_write_protect_p = pmap_write_protect_pae;
		pmap_pinit_pd_p = pmap_pinit_pd_pae;
		pmap_zero_phys_p = pmap_zero_phys_pae;
		pmap_copy_page_p = pmap_copy_page_pae;

		bzero((void *)kpm->pm_pdir + 8, (PDSLOT_PTE-1) * 8);
		/* TODO also reclaim old PDPs */
	}

	/* Set region permissions */
	for (va = (vaddr_t)&PTmap; va < KERNBASE; va += NBPD) {
		pte = PDE(kpm, pdei(va));
		PDE(kpm, pdei(va)) = pte | PG_NX;
	}

	va = (vaddr_t)APTE_BASE;
	pte = PDE(kpm, pdei(va));
	PDE(kpm, pdei(va)) = pte | PG_NX;

	pmap_write_protect(kpm, (vaddr_t)&kernel_text, (vaddr_t)&etext,
	    PROT_READ | PROT_EXEC);
	pmap_write_protect(kpm, (vaddr_t)&__rodata_start,
	    (vaddr_t)&erodata, PROT_READ);
	pmap_write_protect(kpm, (vaddr_t)&__data_start, (vaddr_t)&edata,
	    PROT_READ | PROT_WRITE);
	pmap_write_protect(kpm, (vaddr_t)&__bss_start, (vaddr_t)&end,
	    PROT_READ | PROT_WRITE);

#if defined(DDB) || NKSYMS > 0
	pmap_write_protect(kpm, ssym, esym, PROT_READ);
#endif
}

/*
 * p t p   f u n c t i o n s
 */

/*
 * pmap_alloc_ptp: allocate a PTP for a PMAP
 *
 * => pmap should already be locked by caller
 * => we use the ptp's wire_count to count the number of active mappings
 *	in the PTP (we start it at one to prevent any chance this PTP
 *	will ever leak onto the active/inactive queues)
 * => we should not be holding any pv_head locks (in case we are forced
 *	to call pmap_steal_ptp())
 * => we may need to lock pv_head's if we have to steal a PTP
 */

struct vm_page *
pmap_alloc_ptp_pae(struct pmap *pmap, int pde_index, pt_entry_t pde_flags)
{
	struct vm_page *ptp;
	pd_entry_t *pva_intel;

	ptp = uvm_pagealloc(&pmap->pm_obj, ptp_i2o(pde_index), NULL,
			    UVM_PGA_USERESERVE|UVM_PGA_ZERO);
	if (ptp == NULL)
		return (NULL);

	/* got one! */
	atomic_clearbits_int(&ptp->pg_flags, PG_BUSY);
	ptp->wire_count = 1;	/* no mappings yet */
	PDE(pmap, pde_index) = (pd_entry_t)(VM_PAGE_TO_PHYS(ptp) |
	    PG_RW | PG_V | PG_M | PG_U | pde_flags);

	/*
	 * Meltdown special case - if we are adding a new PDE for
	 * usermode addresses, just copy the PDE to the U-K
	 * table.
	 */
	if (pmap->pm_pdir_intel && ptp_i2v(pde_index) < VM_MAXUSER_ADDRESS) {
		pva_intel = (pd_entry_t *)pmap->pm_pdir_intel;
		pva_intel[pde_index] = PDE(pmap, pde_index);
		DPRINTF("%s: copying usermode PDE (content=0x%llx) pde_index "
		    "%d from 0x%llx -> 0x%llx\n", __func__,
		    PDE(pmap, pde_index), pde_index,
		    (uint64_t)&PDE(pmap, pde_index),
		    (uint64_t)&(pva_intel[pde_index]));
	}

	pmap->pm_stats.resident_count++;	/* count PTP as resident */
	pmap->pm_ptphint = ptp;
	return(ptp);
}

/*
 * pmap_get_ptp: get a PTP (if there isn't one, allocate a new one)
 *
 * => pmap should NOT be pmap_kernel()
 * => pmap should be locked
 */

struct vm_page *
pmap_get_ptp_pae(struct pmap *pmap, int pde_index)
{
	struct vm_page *ptp;

	if (pmap_valid_entry(PDE(pmap, pde_index))) {
		/* valid... check hint (saves us a PA->PG lookup) */
		if (pmap->pm_ptphint &&
		    (PDE(pmap, pde_index) & PG_FRAME) ==
		    VM_PAGE_TO_PHYS(pmap->pm_ptphint))
			return(pmap->pm_ptphint);

		ptp = uvm_pagelookup(&pmap->pm_obj, ptp_i2o(pde_index));
#ifdef DIAGNOSTIC
		if (ptp == NULL)
			panic("pmap_get_ptp_pae: unmanaged user PTP");
#endif
		pmap->pm_ptphint = ptp;
		return(ptp);
	}

	/* allocate a new PTP (updates ptphint) */
	return (pmap_alloc_ptp_pae(pmap, pde_index, PG_u));
}

void
pmap_drop_ptp_pae(struct pmap *pm, vaddr_t va, struct vm_page *ptp,
    pt_entry_t *ptes)
{
	pd_entry_t *pva_intel;

	i386_atomic_testset_uq(&PDE(pm, pdei(va)), 0);
	pmap_tlb_shootpage(curcpu()->ci_curpmap, ((vaddr_t)ptes) + ptp->offset);
#ifdef MULTIPROCESSOR
	/*
	 * Always shoot down the other pmap's
	 * self-mapping of the PTP.
	 */
	pmap_tlb_shootpage(pm, ((vaddr_t)PTE_BASE) + ptp->offset);
#endif
	pm->pm_stats.resident_count--;
	/* update hint */
	if (pm->pm_ptphint == ptp)
		pm->pm_ptphint = RBT_ROOT(uvm_objtree, &pm->pm_obj.memt);
	ptp->wire_count = 0;
	/* Postpone free to after shootdown. */
	uvm_pagerealloc(ptp, NULL, 0);

	if (pm->pm_pdir_intel) {
		KASSERT(va < VM_MAXUSER_ADDRESS);
		/* Zap special meltdown PDE */
		pva_intel = (pd_entry_t *)pm->pm_pdir_intel;
		i386_atomic_testset_uq(&pva_intel[pdei(va)], 0);
		DPRINTF("%s: cleared meltdown PDE @ index %lu "
		    "(va range start 0x%x)\n", __func__, pdei(va),
		    (uint32_t)va);
	}
}

/*
 * pmap_pinit_pd: given a freshly allocated pmap structure, give it a PD
 */
void
pmap_pinit_pd_pae(struct pmap *pmap)
{
	extern int nkpde;
	vaddr_t va;
	paddr_t pdidx[4];

	/* allocate PDP */
	pmap->pm_pdir = (vaddr_t)km_alloc(4 * NBPG, &kv_any, &kp_dirty,
	    &kd_waitok);
	if (pmap->pm_pdir == 0)
		panic("kernel_map out of virtual space");
	/* page index is in the pmap! */
	pmap_extract(pmap_kernel(), (vaddr_t)pmap, &pmap->pm_pdirpa);
	va = (vaddr_t)pmap->pm_pdir;
	pmap_extract(pmap_kernel(), va + 0*NBPG, &pdidx[0]);
	pmap_extract(pmap_kernel(), va + 1*NBPG, &pdidx[1]);
	pmap_extract(pmap_kernel(), va + 2*NBPG, &pdidx[2]);
	pmap_extract(pmap_kernel(), va + 3*NBPG, &pdidx[3]);
	pmap->pm_pdidx[0] = (uint64_t)pdidx[0];
	pmap->pm_pdidx[1] = (uint64_t)pdidx[1];
	pmap->pm_pdidx[2] = (uint64_t)pdidx[2];
	pmap->pm_pdidx[3] = (uint64_t)pdidx[3];
	pmap->pm_pdidx[0] |= PG_V;
	pmap->pm_pdidx[1] |= PG_V;
	pmap->pm_pdidx[2] |= PG_V;
	pmap->pm_pdidx[3] |= PG_V;
	pmap->pm_pdirsize = 4 * NBPG;

	/* init PDP */
	/* zero init area */
	bzero((void *)pmap->pm_pdir, PDSLOT_PTE * sizeof(pd_entry_t));
	/* put in recursive PDE to map the PTEs */
	PDE(pmap, PDSLOT_PTE+0) = pmap->pm_pdidx[0] | PG_KW | PG_U |
	    PG_M | PG_V | PG_NX;
	PDE(pmap, PDSLOT_PTE+1) = pmap->pm_pdidx[1] | PG_KW | PG_U |
	    PG_M | PG_V | PG_NX;
	PDE(pmap, PDSLOT_PTE+2) = pmap->pm_pdidx[2] | PG_KW | PG_U |
	    PG_M | PG_V | PG_NX;
	PDE(pmap, PDSLOT_PTE+3) = pmap->pm_pdidx[3] | PG_KW | PG_U |
	    PG_M | PG_V | PG_NX;

	/*
	 * we need to lock pmaps_lock to prevent nkpde from changing on
	 * us.   note that there is no need to splvm to protect us from
	 * malloc since malloc allocates out of a submap and we should have
	 * already allocated kernel PTPs to cover the range...
	 */
	/* put in kernel VM PDEs */
	bcopy(&PDP_BASE[PDSLOT_KERN], &PDE(pmap, PDSLOT_KERN),
	       nkpde * sizeof(pd_entry_t));
	/* zero the rest */
	bzero(&PDE(pmap, PDSLOT_KERN + nkpde), pmap->pm_pdirsize -
	    ((PDSLOT_KERN + nkpde) * sizeof(pd_entry_t)));

	/*
	 * Intel CPUs need a special page table to be used during usermode
	 * execution, one that lacks all kernel mappings.
	 */
	if (cpu_meltdown) {
		pmap_alloc_pdir_intel_pae(pmap);

		/* Copy PDEs from pmap_kernel's U-K view */
		bcopy((void *)pmap_kernel()->pm_pdir_intel,
		    (void *)pmap->pm_pdir_intel, 4 * NBPG);

		DPRINTF("%s: pmap %p pm_pdir 0x%lx pm_pdirpa 0x%lx "
		    "pdir_intel 0x%lx pdirpa_intel 0x%lx\n",
		    __func__, pmap, pmap->pm_pdir, pmap->pm_pdirpa,
		    pmap->pm_pdir_intel, pmap->pm_pdirpa_intel);
	}

	mtx_enter(&pmaps_lock);
	LIST_INSERT_HEAD(&pmaps, pmap, pm_list);
	mtx_leave(&pmaps_lock);
}

/*
 * some misc. functions
 */

/*
 * pmap_extract: extract a PA for the given VA
 */

int
pmap_extract_pae(struct pmap *pmap, vaddr_t va, paddr_t *pap)
{
	pt_entry_t *ptes, pte;

	ptes = pmap_map_ptes_pae(pmap);
	if (pmap_valid_entry(PDE(pmap, pdei(va)))) {
		pte = ptes[atop(va)];
		pmap_unmap_ptes_pae(pmap);
		if (!pmap_valid_entry(pte))
			return 0;
		if (pap != NULL)
			*pap = (pte & PG_FRAME) | (va & ~PG_FRAME);
		return 1;
	}
	pmap_unmap_ptes_pae(pmap);
	return 0;
}

extern void (*pagezero)(void *, size_t);

/*
 * pmap_zero_phys: same as pmap_zero_page, but for use before vm_pages are
 * initialized.
 */
void
pmap_zero_phys_pae(paddr_t pa)
{
#ifdef MULTIPROCESSOR
	int id = cpu_number();
#endif
	pt_entry_t *zpte = PTESLEW(zero_pte, id);
	caddr_t zerova = VASLEW(pmap_zerop, id);

#ifdef DIAGNOSTIC
	if (*zpte)
		panic("pmap_zero_phys_pae: lock botch");
#endif

	*zpte = (pa & PG_FRAME) | PG_V | PG_RW;	/* map in */
	pmap_update_pg((vaddr_t)zerova);	/* flush TLB */
	pagezero(zerova, PAGE_SIZE);		/* zero */
	*zpte = 0;
}

/*
 * pmap_copy_page: copy a page
 */

void
pmap_copy_page_pae(struct vm_page *srcpg, struct vm_page *dstpg)
{
	paddr_t srcpa = VM_PAGE_TO_PHYS(srcpg);
	paddr_t dstpa = VM_PAGE_TO_PHYS(dstpg);
#ifdef MULTIPROCESSOR
	int id = cpu_number();
#endif
	pt_entry_t *spte = PTESLEW(csrc_pte, id);
	pt_entry_t *dpte = PTESLEW(cdst_pte, id);
	caddr_t csrcva = VASLEW(pmap_csrcp, id);
	caddr_t cdstva = VASLEW(pmap_cdstp, id);

#ifdef DIAGNOSTIC
	if (*spte || *dpte)
		panic("pmap_copy_page_pae: lock botch");
#endif

	*spte = (srcpa & PG_FRAME) | PG_V | PG_RW;
	*dpte = (dstpa & PG_FRAME) | PG_V | PG_RW;
	pmap_update_2pg((vaddr_t)csrcva, (vaddr_t)cdstva);
	bcopy(csrcva, cdstva, PAGE_SIZE);
	*spte = *dpte = 0;
	pmap_update_2pg((vaddr_t)csrcva, (vaddr_t)cdstva);
}

/*
 * p m a p   r e m o v e   f u n c t i o n s
 *
 * functions that remove mappings
 */

/*
 * pmap_remove_ptes: remove PTEs from a PTP
 *
 * => caller must hold pmap's lock
 * => PTP must be mapped into KVA
 * => PTP should be null if pmap == pmap_kernel()
*/

void
pmap_remove_ptes_pae(struct pmap *pmap, struct vm_page *ptp, vaddr_t ptpva,
    vaddr_t startva, vaddr_t endva, int flags, struct pv_entry **free_pvs)
{
	struct pv_entry *pve;
	pt_entry_t *pte = (pt_entry_t *) ptpva;
	struct vm_page *pg;
	pt_entry_t opte;

	/*
	 * note that ptpva points to the PTE that maps startva.   this may
	 * or may not be the first PTE in the PTP.
	 *
	 * we loop through the PTP while there are still PTEs to look at
	 * and the wire_count is greater than 1 (because we use the wire_count
	 * to keep track of the number of real PTEs in the PTP).
	 */

	for (/*null*/; startva < endva && (ptp == NULL || ptp->wire_count > 1)
			     ; pte++, startva += NBPG) {
		if (!pmap_valid_entry(*pte))
			continue;			/* VA not mapped */

		if ((flags & PMAP_REMOVE_SKIPWIRED) && (*pte & PG_W))
			continue;

		/* atomically save the old PTE and zero it */
		opte = i386_atomic_testset_uq(pte, 0);

		if (opte & PG_W)
			pmap->pm_stats.wired_count--;
		pmap->pm_stats.resident_count--;

		if (ptp)
			ptp->wire_count--;		/* dropping a PTE */

		/*
		 * Unnecessary work if not PG_PVLIST.
		 */
		pg = PHYS_TO_VM_PAGE(opte & PG_FRAME);

		/*
		 * if we are not on a pv list we are done.
		 */
		if ((opte & PG_PVLIST) == 0) {
#ifdef DIAGNOSTIC
			if (pg != NULL)
				panic("pmap_remove_ptes_pae: managed page "
				     "without PG_PVLIST for 0x%lx", startva);
#endif
			continue;
		}

#ifdef DIAGNOSTIC
		if (pg == NULL)
			panic("pmap_remove_ptes_pae: unmanaged page marked "
			      "PG_PVLIST, va = 0x%lx, pa = 0x%lx",
			      startva, (u_long)(opte & PG_FRAME));
#endif

		/* sync R/M bits */
		pmap_sync_flags_pte_pae(pg, opte);
		pve = pmap_remove_pv(pg, pmap, startva);
		if (pve) {
			pve->pv_next = *free_pvs;
			*free_pvs = pve;
		}

		/* end of "for" loop: time for next pte */
	}
}

/*
 * pmap_remove: top level mapping removal function
 *
 * => caller should not be holding any pmap locks
 */

void
pmap_do_remove_pae(struct pmap *pmap, vaddr_t sva, vaddr_t eva, int flags)
{
	pt_entry_t *ptes;
	paddr_t ptppa;
	vaddr_t blkendva;
	struct vm_page *ptp;
	struct pv_entry *pve;
	struct pv_entry *free_pvs = NULL;
	TAILQ_HEAD(, vm_page) empty_ptps;
	int shootall;
	vaddr_t va;

	TAILQ_INIT(&empty_ptps);

	ptes = pmap_map_ptes_pae(pmap);	/* locks pmap */

	/*
	 * Decide if we want to shoot the whole tlb or just the range.
	 * Right now, we simply shoot everything when we remove more
	 * than 32 pages, but never in the kernel pmap. XXX - tune.
	 */
	if ((eva - sva > 32 * PAGE_SIZE) && pmap != pmap_kernel())
		shootall = 1;
	else
		shootall = 0;

	for (va = sva ; va < eva ; va = blkendva) {
		/* determine range of block */
		blkendva = i386_round_pdr(va + 1);
		if (blkendva > eva)
			blkendva = eva;

		/*
		 * XXXCDC: our PTE mappings should never be removed
		 * with pmap_remove!  if we allow this (and why would
		 * we?) then we end up freeing the pmap's page
		 * directory page (PDP) before we are finished using
		 * it when we hit it in the recursive mapping.  this
		 * is BAD.
		 *
		 * long term solution is to move the PTEs out of user
		 * address space.  and into kernel address space (up
		 * with APTE).  then we can set VM_MAXUSER_ADDRESS to
		 * be VM_MAX_ADDRESS.
		 */

		if (pdei(va) >= PDSLOT_PTE && pdei(va) <= (PDSLOT_PTE + 3))
			/* XXXCDC: ugly hack to avoid freeing PDP here */
			continue;

		if (!pmap_valid_entry(PDE(pmap, pdei(va))))
			/* valid block? */
			continue;

		/* PA of the PTP */
		ptppa = PDE(pmap, pdei(va)) & PG_FRAME;

		/* get PTP if non-kernel mapping */
		if (pmap == pmap_kernel()) {
			/* we never free kernel PTPs */
			ptp = NULL;
		} else {
			if (pmap->pm_ptphint &&
			    VM_PAGE_TO_PHYS(pmap->pm_ptphint) == ptppa) {
				ptp = pmap->pm_ptphint;
			} else {
				ptp = PHYS_TO_VM_PAGE(ptppa);
#ifdef DIAGNOSTIC
				if (ptp == NULL)
					panic("pmap_do_remove_pae: unmanaged "
					      "PTP detected");
#endif
			}
		}

		pmap_remove_ptes_pae(pmap, ptp, (vaddr_t)&ptes[atop(va)],
		    va, blkendva, flags, &free_pvs);

		/* If PTP is no longer being used, free it. */
		if (ptp && ptp->wire_count <= 1) {
			pmap_drop_ptp_pae(pmap, va, ptp, ptes);
			TAILQ_INSERT_TAIL(&empty_ptps, ptp, pageq);
		}

		if (!shootall)
			pmap_tlb_shootrange(pmap, va, blkendva);
	}

	if (shootall)
		pmap_tlb_shoottlb();

	pmap_unmap_ptes_pae(pmap);
	pmap_tlb_shootwait();

	while ((pve = free_pvs) != NULL) {
		free_pvs = pve->pv_next;
		pool_put(&pmap_pv_pool, pve);
	}

	while ((ptp = TAILQ_FIRST(&empty_ptps)) != NULL) {
		TAILQ_REMOVE(&empty_ptps, ptp, pageq);
		uvm_pagefree(ptp);
	}
}

/*
 * pmap_page_remove: remove a managed vm_page from all pmaps that map it
 *
 * => R/M bits are sync'd back to attrs
 */

void
pmap_page_remove_pae(struct vm_page *pg)
{
	struct pv_entry *pve;
	struct pmap *pm;
	pt_entry_t *ptes, opte;
	TAILQ_HEAD(, vm_page) empty_ptps;
	struct vm_page *ptp;

	if (pg->mdpage.pv_list == NULL)
		return;

	TAILQ_INIT(&empty_ptps);

	mtx_enter(&pg->mdpage.pv_mtx);
	while ((pve = pg->mdpage.pv_list) != NULL) {
		pmap_reference(pve->pv_pmap);
		pm = pve->pv_pmap;
		mtx_leave(&pg->mdpage.pv_mtx);

		ptes = pmap_map_ptes_pae(pm);	/* locks pmap */

		/*
		 * We dropped the pvlist lock before grabbing the pmap
		 * lock to avoid lock ordering problems.  This means
		 * we have to check the pvlist again since somebody
		 * else might have modified it.  All we care about is
		 * that the pvlist entry matches the pmap we just
		 * locked.  If it doesn't, unlock the pmap and try
		 * again.
		 */
		mtx_enter(&pg->mdpage.pv_mtx);
		if ((pve = pg->mdpage.pv_list) == NULL ||
		    pve->pv_pmap != pm) {
			mtx_leave(&pg->mdpage.pv_mtx);
			pmap_unmap_ptes_pae(pm);	/* unlocks pmap */
			pmap_destroy(pm);
			mtx_enter(&pg->mdpage.pv_mtx);
			continue;
		}

		pg->mdpage.pv_list = pve->pv_next;
		mtx_leave(&pg->mdpage.pv_mtx);

#ifdef DIAGNOSTIC
		if (pve->pv_ptp && (PDE(pve->pv_pmap, pdei(pve->pv_va)) &
				    PG_FRAME)
		    != VM_PAGE_TO_PHYS(pve->pv_ptp)) {
			printf("pmap_page_remove_pae: pg=%p: va=%lx, "
				"pv_ptp=%p\n",
				pg, pve->pv_va, pve->pv_ptp);
			printf("pmap_page_remove_pae: PTP's phys addr: "
				"actual=%llx, recorded=%lx\n",
				(PDE(pve->pv_pmap, pdei(pve->pv_va)) &
				PG_FRAME), VM_PAGE_TO_PHYS(pve->pv_ptp));
			panic("pmap_page_remove_pae: mapped managed page has "
				"invalid pv_ptp field");
		}
#endif
		opte = i386_atomic_testset_uq(&ptes[atop(pve->pv_va)], 0);

		if (opte & PG_W)
			pve->pv_pmap->pm_stats.wired_count--;
		pve->pv_pmap->pm_stats.resident_count--;

		/* sync R/M bits */
		pmap_sync_flags_pte_pae(pg, opte);

		/* update the PTP reference count.  free if last reference. */
		if (pve->pv_ptp && --pve->pv_ptp->wire_count <= 1) {
			pmap_drop_ptp_pae(pve->pv_pmap, pve->pv_va,
			    pve->pv_ptp, ptes);
			TAILQ_INSERT_TAIL(&empty_ptps, pve->pv_ptp, pageq);
		}

		pmap_tlb_shootpage(pve->pv_pmap, pve->pv_va);

		pmap_unmap_ptes_pae(pve->pv_pmap);	/* unlocks pmap */
		pmap_destroy(pve->pv_pmap);
		pool_put(&pmap_pv_pool, pve);
		mtx_enter(&pg->mdpage.pv_mtx);
	}
	mtx_leave(&pg->mdpage.pv_mtx);

	pmap_tlb_shootwait();

	while ((ptp = TAILQ_FIRST(&empty_ptps)) != NULL) {
		TAILQ_REMOVE(&empty_ptps, ptp, pageq);
		uvm_pagefree(ptp);
	}
}

/*
 * p m a p   a t t r i b u t e  f u n c t i o n s
 * functions that test/change managed page's attributes
 * since a page can be mapped multiple times we must check each PTE that
 * maps it by going down the pv lists.
 */

/*
 * pmap_test_attrs: test a page's attributes
 *
 * => we set pv_head => pmap locking
 */

int
pmap_test_attrs_pae(struct vm_page *pg, int testbits)
{
	struct pv_entry *pve;
	pt_entry_t *ptes, pte;
	u_long mybits, testflags;
	paddr_t ptppa;

	testflags = pmap_pte2flags(testbits);

	if (pg->pg_flags & testflags)
		return 1;

	mybits = 0;
	mtx_enter(&pg->mdpage.pv_mtx);
	for (pve = pg->mdpage.pv_list; pve != NULL && mybits == 0;
	    pve = pve->pv_next) {
		ptppa = PDE(pve->pv_pmap, pdei(pve->pv_va)) & PG_FRAME;
		ptes = (pt_entry_t *)pmap_tmpmap_pa(ptppa);
		pte = ptes[ptei(pve->pv_va)];
		pmap_tmpunmap_pa();
		mybits |= (pte & testbits);
	}
	mtx_leave(&pg->mdpage.pv_mtx);

	if (mybits == 0)
		return 0;

	atomic_setbits_int(&pg->pg_flags, pmap_pte2flags(mybits));

	return 1;
}

/*
 * pmap_clear_attrs: change a page's attributes
 *
 * => we return 1 if we cleared one of the bits we were asked to
 */
int
pmap_clear_attrs_pae(struct vm_page *pg, int clearbits)
{
	struct pv_entry *pve;
	pt_entry_t *ptes, npte, opte;
	u_long clearflags;
	paddr_t ptppa;
	int result;

	clearflags = pmap_pte2flags(clearbits);

	result = pg->pg_flags & clearflags;
	if (result)
		atomic_clearbits_int(&pg->pg_flags, clearflags);

	mtx_enter(&pg->mdpage.pv_mtx);
	for (pve = pg->mdpage.pv_list; pve != NULL; pve = pve->pv_next) {
		ptppa = PDE(pve->pv_pmap, pdei(pve->pv_va)) & PG_FRAME;
		ptes = (pt_entry_t *)pmap_tmpmap_pa(ptppa);
#ifdef DIAGNOSTIC
		if (!pmap_valid_entry(PDE(pve->pv_pmap, pdei(pve->pv_va))))
			panic("pmap_clear_attrs_pae: mapping without PTP "
				"detected");
#endif

		opte = ptes[ptei(pve->pv_va)];
		if (opte & clearbits) {
			result = 1;
			npte = opte & ~clearbits;
			opte = i386_atomic_testset_uq(
			   &ptes[ptei(pve->pv_va)], npte);
			pmap_tlb_shootpage(pve->pv_pmap, pve->pv_va);
		}
		pmap_tmpunmap_pa();
	}
	mtx_leave(&pg->mdpage.pv_mtx);

	pmap_tlb_shootwait();

	return (result != 0);
}


/*
 * p m a p   p r o t e c t i o n   f u n c t i o n s
 */

/*
 * pmap_page_protect: change the protection of all recorded mappings
 *	of a managed page
 *
 * => NOTE: this is an inline function in pmap.h
 */

/* see pmap.h */

/*
 * pmap_protect: set the protection in of the pages in a pmap
 *
 * => NOTE: this is an inline function in pmap.h
 */

/* see pmap.h */

/*
 * pmap_write_protect: write-protect pages in a pmap
 */

void
pmap_write_protect_pae(struct pmap *pmap, vaddr_t sva, vaddr_t eva,
    vm_prot_t prot)
{
	pt_entry_t *ptes, *spte, *epte, npte, opte;
	vaddr_t blkendva;
	u_int64_t md_prot;
	vaddr_t va;
	int shootall = 0;

	ptes = pmap_map_ptes_pae(pmap);		/* locks pmap */

	if ((eva - sva > 32 * PAGE_SIZE) && pmap != pmap_kernel())
		shootall = 1;

	for (va = sva; va < eva; va = blkendva) {
		/* determine range of block */
		blkendva = i386_round_pdr(va + 1);
		if (blkendva > eva)
			blkendva = eva;

		/*
		 * XXXCDC: our PTE mappings should never be write-protected!
		 *
		 * long term solution is to move the PTEs out of user
		 * address space.  and into kernel address space (up
		 * with APTE).  then we can set VM_MAXUSER_ADDRESS to
		 * be VM_MAX_ADDRESS.
		 */

		/* XXXCDC: ugly hack to avoid freeing PDP here */
		if (pdei(va) >= PDSLOT_PTE && pdei(va) <= (PDSLOT_PTE + 3))
			continue;

		/* empty block? */
		if (!pmap_valid_entry(PDE(pmap, pdei(va))))
			continue;

		md_prot = protection_codes[prot];
		if (!(prot & PROT_EXEC))
			md_prot |= PG_NX;
		if (va < VM_MAXUSER_ADDRESS)
			md_prot |= PG_u;
		else if (va < VM_MAX_ADDRESS)
			/* XXX: write-prot our PTES? never! */
			md_prot |= PG_RW;

		spte = &ptes[atop(va)];
		epte = &ptes[atop(blkendva)];

		for (/*null */; spte < epte ; spte++, va += PAGE_SIZE) {

			if (!pmap_valid_entry(*spte))	/* no mapping? */
				continue;

			opte = *spte;
			npte = (opte & ~(pt_entry_t)PG_PROT) | md_prot;

			if (npte != opte) {
				pmap_exec_account(pmap, va, *spte, npte);
				i386_atomic_testset_uq(spte, npte);
			}
		}
	}
	if (shootall)
		pmap_tlb_shoottlb();
	else
		pmap_tlb_shootrange(pmap, sva, eva);

	pmap_unmap_ptes_pae(pmap);		/* unlocks pmap */
	pmap_tlb_shootwait();
}

/*
 * end of protection functions
 */

/*
 * pmap_unwire: clear the wired bit in the PTE
 *
 * => mapping should already be in map
 */

void
pmap_unwire_pae(struct pmap *pmap, vaddr_t va)
{
	pt_entry_t *ptes;

	if (pmap_valid_entry(PDE(pmap, pdei(va)))) {
		ptes = pmap_map_ptes_pae(pmap);		/* locks pmap */

#ifdef DIAGNOSTIC
		if (!pmap_valid_entry(ptes[atop(va)]))
			panic("pmap_unwire_pae: invalid (unmapped) va "
			      "0x%lx", va);
#endif
		if ((ptes[atop(va)] & PG_W) != 0) {
			i386_atomic_testset_uq(&ptes[atop(va)],
			    ptes[atop(va)] & ~PG_W);
			pmap->pm_stats.wired_count--;
		}
#ifdef DIAGNOSTIC
		else {
			printf("pmap_unwire_pae: wiring for pmap %p va 0x%lx "
			       "didn't change!\n", pmap, va);
		}
#endif
		pmap_unmap_ptes_pae(pmap);		/* unlocks map */
	}
#ifdef DIAGNOSTIC
	else {
		panic("pmap_unwire_pae: invalid PDE");
	}
#endif
}

/*
 * pmap_enter: enter a mapping into a pmap
 *
 * => must be done "now" ... no lazy-evaluation
 */

int
pmap_enter_pae(struct pmap *pmap, vaddr_t va, paddr_t pa, vm_prot_t prot,
    int flags)
{
	pt_entry_t *ptes, opte, npte;
	struct vm_page *ptp;
	struct pv_entry *pve, *opve = NULL;
	int wired = (flags & PMAP_WIRED) != 0;
	int nocache = (pa & PMAP_NOCACHE) != 0;
	int wc = (pa & PMAP_WC) != 0;
	struct vm_page *pg = NULL;
	int error, wired_count, resident_count, ptp_count;

	KASSERT(!(wc && nocache));
	pa &= PMAP_PA_MASK;	/* nuke flags from pa */

#ifdef DIAGNOSTIC
	/* sanity check: totally out of range? */
	if (va >= VM_MAX_KERNEL_ADDRESS)
		panic("pmap_enter_pae: too big");

	if (va == (vaddr_t) PDP_BASE || va == (vaddr_t) APDP_BASE)
		panic("pmap_enter_pae: trying to map over PDP/APDP!");

	/* sanity check: kernel PTPs should already have been pre-allocated */
	if (va >= VM_MIN_KERNEL_ADDRESS &&
	    !pmap_valid_entry(PDE(pmap, pdei(va))))
		panic("pmap_enter_pae: missing kernel PTP!");
#endif

	if (pmap_initialized)
		pve = pool_get(&pmap_pv_pool, PR_NOWAIT);
	else
		pve = NULL;
	wired_count = resident_count = ptp_count = 0;

	/*
	 * map in ptes and get a pointer to our PTP (unless we are the kernel)
	 */

	ptes = pmap_map_ptes_pae(pmap);		/* locks pmap */
	if (pmap == pmap_kernel()) {
		ptp = NULL;
	} else {
		ptp = pmap_get_ptp_pae(pmap, pdei(va));
		if (ptp == NULL) {
			if (flags & PMAP_CANFAIL) {
				error = ENOMEM;
				pmap_unmap_ptes_pae(pmap);
				goto out;
			}
			panic("pmap_enter_pae: get ptp failed");
		}
	}
	/*
	 * not allowed to sleep after here!
	 */
	opte = ptes[atop(va)];			/* old PTE */

	/*
	 * is there currently a valid mapping at our VA?
	 */

	if (pmap_valid_entry(opte)) {

		/*
		 * first, calculate pm_stats updates.  resident count will not
		 * change since we are replacing/changing a valid
		 * mapping.  wired count might change...
		 */

		if (wired && (opte & PG_W) == 0)
			wired_count++;
		else if (!wired && (opte & PG_W) != 0)
			wired_count--;

		/*
		 * is the currently mapped PA the same as the one we
		 * want to map?
		 */

		if ((opte & PG_FRAME) == pa) {

			/* if this is on the PVLIST, sync R/M bit */
			if (opte & PG_PVLIST) {
				pg = PHYS_TO_VM_PAGE(pa);
#ifdef DIAGNOSTIC
				if (pg == NULL)
					panic("pmap_enter_pae: same pa "
					     "PG_PVLIST mapping with "
					     "unmanaged page "
					     "pa = 0x%lx (0x%lx)", pa,
					     atop(pa));
#endif
				pmap_sync_flags_pte_pae(pg, opte);
			}
			goto enter_now;
		}

		/*
		 * changing PAs: we must remove the old one first
		 */

		/*
		 * if current mapping is on a pvlist,
		 * remove it (sync R/M bits)
		 */

		if (opte & PG_PVLIST) {
			pg = PHYS_TO_VM_PAGE(opte & PG_FRAME);
#ifdef DIAGNOSTIC
			if (pg == NULL)
				panic("pmap_enter_pae: PG_PVLIST mapping with "
				      "unmanaged page "
				      "pa = 0x%lx (0x%lx)", pa, atop(pa));
#endif
			pmap_sync_flags_pte_pae(pg, opte);
			opve = pmap_remove_pv(pg, pmap, va);
			pg = NULL; /* This is not the page we are looking for */
		}
	} else {	/* opte not valid */
		resident_count++;
		if (wired)
			wired_count++;
		if (ptp)
			ptp_count++;	/* count # of valid entries */
	}

	/*
	 * pve is either NULL or points to a now-free pv_entry structure
	 * (the latter case is if we called pmap_remove_pv above).
	 *
	 * if this entry is to be on a pvlist, enter it now.
	 */

	if (pmap_initialized && pg == NULL)
		pg = PHYS_TO_VM_PAGE(pa);

	if (pg != NULL) {
		if (pve == NULL) {
			pve = opve;
			opve = NULL;
		}
		if (pve == NULL) {
			if (flags & PMAP_CANFAIL) {
				pmap_unmap_ptes_pae(pmap);
				error = ENOMEM;
				goto out;
			}
			panic("pmap_enter_pae: no pv entries available");
		}
		/* lock pg when adding */
		pmap_enter_pv(pg, pve, pmap, va, ptp);
		pve = NULL;
	}

enter_now:
	/*
	 * at this point pg is !NULL if we want the PG_PVLIST bit set
	 */

	npte = pa | protection_codes[prot] | PG_V;
	if (!(prot & PROT_EXEC))
		npte |= PG_NX;
	pmap_exec_account(pmap, va, opte, npte);
	if (wired)
		npte |= PG_W;
	if (nocache)
		npte |= PG_N;
	if (va < VM_MAXUSER_ADDRESS)
		npte |= PG_u;
	else if (va < VM_MAX_ADDRESS)
		npte |= PG_RW;	/* XXXCDC: no longer needed? */
	if (pmap == pmap_kernel())
		npte |= pmap_pg_g;
	if (flags & PROT_READ)
		npte |= PG_U;
	if (flags & PROT_WRITE)
		npte |= PG_M;
	if (pg) {
		npte |= PG_PVLIST;
		if (pg->pg_flags & PG_PMAP_WC) {
			KASSERT(nocache == 0);
			wc = 1;
		}
		pmap_sync_flags_pte_pae(pg, npte);
	}
	if (wc)
		npte |= pmap_pg_wc;

	opte = i386_atomic_testset_uq(&ptes[atop(va)], npte);
	if (ptp)
		ptp->wire_count += ptp_count;
	pmap->pm_stats.resident_count += resident_count;
	pmap->pm_stats.wired_count += wired_count;

	if (pmap_valid_entry(opte)) {
		if (nocache && (opte & PG_N) == 0)
			wbinvd_on_all_cpus(); /* XXX clflush before we enter? */
		pmap_tlb_shootpage(pmap, va);
	}

	pmap_unmap_ptes_pae(pmap);
	pmap_tlb_shootwait();

	error = 0;

out:
	if (pve)
		pool_put(&pmap_pv_pool, pve);
	if (opve)
		pool_put(&pmap_pv_pool, opve);

	return error;
}

/*
 * Allocate an extra PDPT and PT pages as needed to map kernel pages
 * used for the U-K mappings.  These special mappings are set up
 * during bootstrap and get never removed and are part of pmap_kernel.
 *
 * New pmaps inherit the kernel portion of pmap_kernel including
 * the special mappings (see pmap_pinit_pd_pae()).
 */
void
pmap_enter_special_pae(vaddr_t va, paddr_t pa, vm_prot_t prot, u_int32_t flags)
{
	struct pmap 	*pmap = pmap_kernel();
	struct vm_page	*ptppg = NULL;
	pd_entry_t	*pd, *ptp;
	pt_entry_t	*ptes;
	uint32_t	 l2idx, l1idx;
	paddr_t		 npa;

	/* If CPU is secure, no need to do anything */
	if (!cpu_meltdown)
		return;

	/* Must be kernel VA */
	if (va < VM_MIN_KERNEL_ADDRESS)
		panic("invalid special mapping va 0x%lx requested", va);

	KASSERT(pmap->pm_pdir_intel != 0);

	DPRINTF("%s: pm_pdir_intel 0x%x pm_pdirpa_intel 0x%x\n", __func__,
	    (uint32_t)pmap->pm_pdir_intel, (uint32_t)pmap->pm_pdirpa_intel);

	/* These are the PAE versions of pdei() and ptei() */
	l2idx = pdei(va);
	l1idx = ptei(va);

	DPRINTF("%s: va 0x%08lx pa 0x%08lx prot 0x%08lx flags 0x%08x "
	    "l2idx %u l1idx %u\n", __func__, va, pa, (unsigned long)prot,
	    flags, l2idx, l1idx);

	if ((pd = (pd_entry_t *)pmap->pm_pdir_intel) == 0)
		panic("%s: PD not initialized for pmap @ %p", __func__, pmap);

	/* npa = physaddr of PT page */
	npa = pd[l2idx] & PMAP_PA_MASK;

	/* Valid PDE for the 2MB region containing va? */
	if (!npa) {
		/*
		 * No valid PDE - allocate PT page and set PDE.  We
		 * get it from pm_obj, which is used for PT pages.
		 * We calculate the offset  from l2idx+2048, so we are
		 * beyond the regular PT pages. For their l2dix
		 * 0 <= l2idx < 2048 holds.
		 */
		ptppg = uvm_pagealloc(&pmap->pm_obj, ptp_i2o(l2idx + 2048),
		    NULL, UVM_PGA_USERESERVE|UVM_PGA_ZERO);
		if (ptppg == NULL)
			panic("%s: failed to allocate PT page", __func__);

		atomic_clearbits_int(&ptppg->pg_flags, PG_BUSY);
		ptppg->wire_count = 1;	/* no mappings yet */

		npa = VM_PAGE_TO_PHYS(ptppg);
		pd[l2idx] = (npa | PG_RW | PG_V | PG_M | PG_U);

		DPRINTF("%s: allocated new PT page at phys 0x%x, "
		    "setting PDE[%d] = 0x%llx\n", __func__, (uint32_t)npa,
		    l2idx, pd[l2idx]);
	}

	/* temporarily map PT page and set PTE for U-K mapping */
	if (ptppg == NULL && (ptppg = PHYS_TO_VM_PAGE(npa)) == NULL)
		panic("%s: no vm_page for PT page", __func__);
	mtx_enter(&ptppg->mdpage.pv_mtx);
	ptp = (pd_entry_t *)pmap_tmpmap_pa(npa);
	ptp[l1idx] = (pa | protection_codes[prot] | PG_V | PG_M | PG_U | flags);
	DPRINTF("%s: setting PTE[%d] = 0x%llx\n", __func__, l1idx, ptp[l1idx]);
	pmap_tmpunmap_pa();
	mtx_leave(&ptppg->mdpage.pv_mtx);

	/* if supported, set the PG_G flag on the corresponding U+K entry */
	if (!(cpu_feature & CPUID_PGE))
		return;
	ptes = pmap_map_ptes_pae(pmap);	/* pmap_kernel -> PTE_BASE */
	if (pmap_valid_entry(ptes[atop(va)]))
		ptes[atop(va)] |= PG_G;
	else
		DPRINTF("%s: no U+K mapping for special mapping?\n", __func__);
	pmap_unmap_ptes_pae(pmap);	/* pmap_kernel -> nothing */
}

/*
 * pmap_growkernel: increase usage of KVM space
 *
 * => we allocate new PTPs for the kernel and install them in all
 *	the pmaps on the system.
 */

vaddr_t
pmap_growkernel_pae(vaddr_t maxkvaddr)
{
	extern int nkpde;
	struct pmap *kpm = pmap_kernel(), *pm;
	int needed_kpde;   /* needed number of kernel PTPs */
	int s;
	paddr_t ptaddr;

	needed_kpde = (int)(maxkvaddr - VM_MIN_KERNEL_ADDRESS + (NBPD-1))
		/ NBPD;
	if (needed_kpde <= nkpde)
		goto out;		/* we are OK */

	/*
	 * whoops!   we need to add kernel PTPs
	 */

	s = splhigh();	/* to be safe */

	for (/*null*/ ; nkpde < needed_kpde ; nkpde++) {

		if (uvm.page_init_done == 0) {

			/*
			 * we're growing the kernel pmap early (from
			 * uvm_pageboot_alloc()).  this case must be
			 * handled a little differently.
			 */

			if (uvm_page_physget(&ptaddr) == 0)
				panic("pmap_growkernel: out of memory");
			pmap_zero_phys_pae(ptaddr);

			PDE(kpm, PDSLOT_KERN + nkpde) =
				ptaddr | PG_RW | PG_V | PG_U | PG_M;

			/* count PTP as resident */
			kpm->pm_stats.resident_count++;
			continue;
		}

		/*
		 * THIS *MUST* BE CODED SO AS TO WORK IN THE
		 * pmap_initialized == 0 CASE!  WE MAY BE
		 * INVOKED WHILE pmap_init() IS RUNNING!
		 */

		while (!pmap_alloc_ptp_pae(kpm, PDSLOT_KERN + nkpde, 0))
			uvm_wait("pmap_growkernel");

		/* distribute new kernel PTP to all active pmaps */
		mtx_enter(&pmaps_lock);
		LIST_FOREACH(pm, &pmaps, pm_list) {
			PDE(pm, PDSLOT_KERN + nkpde) =
				PDE(kpm, PDSLOT_KERN + nkpde);
		}
		mtx_leave(&pmaps_lock);
	}

	splx(s);

out:
	return (VM_MIN_KERNEL_ADDRESS + (nkpde * NBPD));
}

/*
 * Pre-allocate PTP 0 for low memory, so that 1:1 mappings for various
 * trampoline code can be entered.
 */
void
pmap_prealloc_lowmem_ptp_pae(void)
{
	pt_entry_t *pte, npte;
	vaddr_t ptpva = (vaddr_t)vtopte(0);

	/* enter pa for pte 0 into recursive map */
	pte = vtopte(ptpva);
	npte = PTP0_PA | PG_RW | PG_V | PG_U | PG_M;

	i386_atomic_testset_uq(pte, npte);

	/* make sure it is clean before using */
	memset((void *)ptpva, 0, NBPG);
}

/*
 * pmap_tmpmap_pa_pae: map a page in for tmp usage
 */

vaddr_t
pmap_tmpmap_pa_pae(paddr_t pa)
{
#ifdef MULTIPROCESSOR
	int id = cpu_number();
#endif
	pt_entry_t *ptpte = PTESLEW(ptp_pte, id);
	caddr_t ptpva = VASLEW(pmap_ptpp, id);
#if defined(DIAGNOSTIC)
	if (*ptpte)
		panic("pmap_tmpmap_pa_pae: ptp_pte in use?");
#endif
	*ptpte = PG_V | PG_RW | pa;	/* always a new mapping */
	return((vaddr_t)ptpva);
}

/*
 * pmap_tmpunmap_pa_pae: unmap a tmp use page (undoes pmap_tmpmap_pa_pae)
 */

void
pmap_tmpunmap_pa_pae(void)
{
#ifdef MULTIPROCESSOR
	int id = cpu_number();
#endif
	pt_entry_t *ptpte = PTESLEW(ptp_pte, id);
	caddr_t ptpva = VASLEW(pmap_ptpp, id);
#if defined(DIAGNOSTIC)
	if (!pmap_valid_entry(*ptpte))
		panic("pmap_tmpunmap_pa_pae: our pte invalid?");
#endif
	*ptpte = 0;
	pmap_update_pg((vaddr_t)ptpva);
#ifdef MULTIPROCESSOR
	/*
	 * No need for tlb shootdown here, since ptp_pte is per-CPU.
	 */
#endif
}

paddr_t
vtophys_pae(vaddr_t va)
{
	return ((*vtopte(va) & PG_FRAME) | (va & ~PG_FRAME));
}

void
pmap_flush_page_pae(paddr_t pa)
{
#ifdef MULTIPROCESSOR
	int id = cpu_number();
#endif
	pt_entry_t *pte = PTESLEW(flsh_pte, id);
	caddr_t va = VASLEW(pmap_flshp, id);

	KDASSERT(PHYS_TO_VM_PAGE(pa) != NULL);
#ifdef DIAGNOSTIC
	if (*pte)
		panic("pmap_flush_page_pae: lock botch");
#endif

	*pte = (pa & PG_FRAME) | PG_V | PG_RW;
	pmap_update_pg(va);
	pmap_flush_cache((vaddr_t)va, PAGE_SIZE);
	*pte = 0;
	pmap_update_pg(va);
}
