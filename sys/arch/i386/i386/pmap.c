/*	$OpenBSD: pmap.c,v 1.229 2025/08/15 13:40:43 kettenis Exp $	*/
/*	$NetBSD: pmap.c,v 1.91 2000/06/02 17:46:37 thorpej Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/proc.h>
#include <sys/pool.h>
#include <sys/user.h>
#include <sys/mutex.h>

#include <uvm/uvm.h>

#include <machine/specialreg.h>

#include <sys/msgbuf.h>
#include <stand/boot/bootarg.h>

/* #define PMAP_DEBUG */

#ifdef PMAP_DEBUG
#define DPRINTF(x...)	do { printf(x); } while(0)
#else
#define DPRINTF(x...)
#endif	/* PMAP_DEBUG */

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
 * i386 MMU hardware structure:
 *
 * the i386 MMU is a two-level MMU which maps 4GB of virtual memory.
 * the pagesize is 4K (4096 [0x1000] bytes), although newer pentium
 * processors can support a 4MB pagesize as well.
 *
 * the first level table (segment table?) is called a "page directory"
 * and it contains 1024 page directory entries (PDEs).   each PDE is
 * 4 bytes (an int), so a PD fits in a single 4K page.   this page is
 * the page directory page (PDP).  each PDE in a PDP maps 4MB of space
 * (1024 * 4MB = 4GB).   a PDE contains the physical address of the
 * second level table: the page table.   or, if 4MB pages are being used,
 * then the PDE contains the PA of the 4MB page being mapped.
 *
 * a page table consists of 1024 page table entries (PTEs).  each PTE is
 * 4 bytes (an int), so a page table also fits in a single 4K page.  a
 * 4K page being used as a page table is called a page table page (PTP).
 * each PTE in a PTP maps one 4K page (1024 * 4K = 4MB).   a PTE contains
 * the physical address of the page it maps and some flag bits (described
 * below).
 *
 * the processor has a special register, "cr3", which points to the
 * the PDP which is currently controlling the mappings of the virtual
 * address space.
 *
 * the following picture shows the translation process for a 4K page:
 *
 * %cr3 register [PA of PDP]
 *      |
 *      |
 *      |   bits <31-22> of VA         bits <21-12> of VA   bits <11-0>
 *      |   index the PDP (0 - 1023)   index the PTP        are the page offset
 *      |         |                           |                  |
 *      |         v                           |                  |
 *      +--->+----------+                     |                  |
 *           | PD Page  |   PA of             v                  |
 *           |          |---PTP-------->+------------+           |
 *           | 1024 PDE |               | page table |--PTE--+   |
 *           | entries  |               | (aka PTP)  |       |   |
 *           +----------+               | 1024 PTE   |       |   |
 *                                      | entries    |       |   |
 *                                      +------------+       |   |
 *                                                           |   |
 *                                                bits <31-12>   bits <11-0>
 *                                                p h y s i c a l  a d d r
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
 * virtual address space can be broken up into 1024 4MB regions which
 * are described by PDEs in the PDP.  The PDEs are defined as follows:
 *
 * Ranges are inclusive -> exclusive, just like vm_map_entry start/end.
 * The following assumes that KERNBASE is 0xd0000000.
 *
 * PDE#s	VA range		Usage
 * 0->831	0x0 -> 0xcfc00000	user address space, note that the
 *					max user address is 0xcfbfe000
 *					the final two pages in the last 4MB
 *					used to be reserved for the UAREA
 *					but now are no longer used.
 * 831		0xcfc00000->		recursive mapping of PDP (used for
 *			0xd0000000	linear mapping of PTPs).
 * 832->1023	0xd0000000->		kernel address space (constant
 *			0xffc00000	across all pmaps/processes).
 * 1023		0xffc00000->		"alternate" recursive PDP mapping
 *			<end>		(for other pmaps).
 *
 *
 * Note: A recursive PDP mapping provides a way to map all the PTEs for
 * a 4GB address space into a linear chunk of virtual memory.  In other
 * words, the PTE for page 0 is the first int mapped into the 4MB recursive
 * area.  The PTE for page 1 is the second int.  The very last int in the
 * 4MB range is the PTE that maps VA 0xffffe000 (the last page in a 4GB
 * address).
 *
 * All pmaps' PDs must have the same values in slots 832->1023 so that
 * the kernel is always mapped in every process.  These values are loaded
 * into the PD at pmap creation time.
 *
 * At any one time only one pmap can be active on a processor.  This is
 * the pmap whose PDP is pointed to by processor register %cr3.  This pmap
 * will have all its PTEs mapped into memory at the recursive mapping
 * point (slot #831 as show above).  When the pmap code wants to find the
 * PTE for a virtual address, all it has to do is the following:
 *
 * Address of PTE = (831 * 4MB) + (VA / PAGE_SIZE) * sizeof(pt_entry_t)
 *                = 0xcfc00000 + (VA / 4096) * 4
 *
 * What happens if the pmap layer is asked to perform an operation
 * on a pmap that is not the one which is currently active?  In that
 * case we take the PA of the PDP of the non-active pmap and put it in
 * slot 1023 of the active pmap.  This causes the non-active pmap's
 * PTEs to get mapped in the final 4MB of the 4GB address space
 * (e.g. starting at 0xffc00000).
 *
 * The following figure shows the effects of the recursive PDP mapping:
 *
 *   PDP (%cr3)
 *   +----+
 *   |   0| -> PTP#0 that maps VA 0x0 -> 0x400000
 *   |    |
 *   |    |
 *   | 831| -> points back to PDP (%cr3) mapping VA 0xcfc00000 -> 0xd0000000
 *   | 832| -> first kernel PTP (maps 0xd0000000 -> 0xe0400000)
 *   |    |
 *   |1023| -> points to alternate pmap's PDP (maps 0xffc00000 -> end)
 *   +----+
 *
 * Note that the PDE#831 VA (0xcfc00000) is defined as "PTE_BASE".
 * Note that the PDE#1023 VA (0xffc00000) is defined as "APTE_BASE".
 *
 * Starting at VA 0xcfc00000 the current active PDP (%cr3) acts as a
 * PTP:
 *
 * PTP#831 == PDP(%cr3) => maps VA 0xcfc00000 -> 0xd0000000
 *   +----+
 *   |   0| -> maps the contents of PTP#0 at VA 0xcfc00000->0xcfc01000
 *   |    |
 *   |    |
 *   | 831| -> maps the contents of PTP#831 (the PDP) at VA 0xcff3f000
 *   | 832| -> maps the contents of first kernel PTP
 *   |    |
 *   |1023|
 *   +----+
 *
 * Note that mapping of the PDP at PTP#831's VA (0xcff3f000) is
 * defined as "PDP_BASE".... within that mapping there are two
 * defines:
 *   "PDP_PDE" (0xcff3fcfc) is the VA of the PDE in the PDP
 *      which points back to itself.
 *   "APDP_PDE" (0xcff3fffc) is the VA of the PDE in the PDP which
 *      establishes the recursive mapping of the alternate pmap.
 *      To set the alternate PDP, one just has to put the correct
 *	PA info in *APDP_PDE.
 *
 * Note that in the APTE_BASE space, the APDP appears at VA
 * "APDP_BASE" (0xfffff000).
 */
#define PG_FRAME	0xfffff000	/* page frame mask */
#define PG_LGFRAME	0xffc00000	/* large (4M) page frame mask */

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
#define PD_MASK		0xffc00000	/* page directory address bits */
#define PT_MASK		0x003ff000	/* page table address bits */
#define pdei(VA)	(((VA) & PD_MASK) >> PDSHIFT)
#define ptei(VA)	(((VA) & PT_MASK) >> PGSHIFT)

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
 * Note that NBPG == number of bytes in a PTP (4096 bytes == 1024 entries)
 *           NBPD == number of bytes a PTP can map (4MB)
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
 * here we define the data types for PDEs and PTEs
 */
typedef u_int32_t pd_entry_t;		/* PDE */
typedef u_int32_t pt_entry_t;		/* PTE */

/*
 * Number of PTEs per cache line. 4 byte pte, 64-byte cache line
 * Used to avoid false sharing of cache lines.
 */
#define NPTECL			16

/*
 * global data structures
 */

/* The kernel's pmap (proc0), 32 byte aligned in case we are using PAE */
struct pmap __attribute__ ((aligned (32))) kernel_pmap_store;

/*
 * nkpde is the number of kernel PTPs allocated for the kernel at
 * boot time (NKPTP is a compile time override).   this number can
 * grow dynamically as needed (but once allocated, we never free
 * kernel PTPs).
 */

int nkpde = NKPTP;
int nkptp_max = 1024 - (KERNBASE / NBPD) - 1;

/*
 * pg_g_kern:  if CPU is affected by Meltdown pg_g_kern is 0,
 * otherwise it is set to PG_G.  pmap_pg_g will be derived
 * from pg_g_kern, see pmap_bootstrap().
 */
extern int pg_g_kern;

/*
 * pmap_pg_g: if our processor supports PG_G in the PTE then we
 * set pmap_pg_g to PG_G (otherwise it is zero).
 */

int pmap_pg_g = 0;

/*
 * pmap_pg_wc: if our processor supports PAT then we set this
 * to be the pte bits for Write Combining. Else we fall back to
 * UC- so mtrrs can override the cacheability
 */
int pmap_pg_wc = PG_UCMINUS;

/*
 * other data structures
 */

uint32_t protection_codes[8];		/* maps MI prot to i386 prot code */
int pmap_initialized = 0;	/* pmap_init done yet? */

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
 * pv management structures.
 */
struct pool pmap_pv_pool;

#define PVE_LOWAT (PVE_PER_PVPAGE / 2)	/* free pv_entry low water mark */
#define PVE_HIWAT (PVE_LOWAT + (PVE_PER_PVPAGE * 2))
					/* high water mark */

/*
 * the following two vaddr_t's are used during system startup
 * to keep track of how much of the kernel's VM space we have used.
 * once the system is started, the management of the remaining kernel
 * VM space is turned over to the kernel_map vm_map.
 */

static vaddr_t virtual_avail;	/* VA of first free KVA */
static vaddr_t virtual_end;	/* VA of last free KVA */

/*
 * linked list of all non-kernel pmaps
 */

struct pmap_head pmaps;
struct mutex pmaps_lock = MUTEX_INITIALIZER(IPL_VM);

/*
 * pool that pmap structures are allocated from
 */

struct pool pmap_pmap_pool;

/*
 * special VAs and the PTEs that map them
 */

pt_entry_t *csrc_pte, *cdst_pte, *zero_pte, *ptp_pte, *flsh_pte;
caddr_t pmap_csrcp, pmap_cdstp, pmap_zerop, pmap_ptpp, pmap_flshp;
caddr_t vmmap; /* XXX: used by mem.c... it should really uvm_map_reserve it */

extern uint32_t cpu_meltdown;

/*
 * local prototypes
 */
struct vm_page	*pmap_alloc_ptp_86(struct pmap *, int, pt_entry_t);
struct vm_page	*pmap_get_ptp_86(struct pmap *, int);
pt_entry_t	*pmap_map_ptes_86(struct pmap *);
void		 pmap_unmap_ptes_86(struct pmap *);
void		 pmap_do_remove_86(struct pmap *, vaddr_t, vaddr_t, int);
void		 pmap_remove_ptes_86(struct pmap *, struct vm_page *, vaddr_t,
		    vaddr_t, vaddr_t, int, struct pv_entry **);
void		*pmap_pv_page_alloc(struct pool *, int, int *);
void		pmap_pv_page_free(struct pool *, void *);

struct pool_allocator pmap_pv_page_allocator = {
	pmap_pv_page_alloc, pmap_pv_page_free,
};

void		 pmap_sync_flags_pte_86(struct vm_page *, pt_entry_t);

void		 pmap_drop_ptp_86(struct pmap *, vaddr_t, struct vm_page *,
    pt_entry_t *);

void		 setcslimit(struct pmap *, struct trapframe *, struct pcb *,
		     vaddr_t);
void		 pmap_pinit_pd_86(struct pmap *);

static __inline u_int
pmap_pte2flags(pt_entry_t pte)
{
	return (((pte & PG_U) ? PG_PMAP_REF : 0) |
	    ((pte & PG_M) ? PG_PMAP_MOD : 0));
}

void
pmap_sync_flags_pte_86(struct vm_page *pg, pt_entry_t pte)
{
	if (pte & (PG_U|PG_M)) {
		atomic_setbits_int(&pg->pg_flags, pmap_pte2flags(pte));
	}
}

void
pmap_apte_flush(void)
{
	pmap_tlb_shoottlb();
	pmap_tlb_shootwait();
}

/*
 * pmap_map_ptes: map a pmap's PTEs into KVM and lock them in
 *
 * => we lock enough pmaps to keep things locked in
 * => must be undone with pmap_unmap_ptes before returning
 */

pt_entry_t *
pmap_map_ptes_86(struct pmap *pmap)
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
		panic("pmap_map_ptes_86: APTE valid");
#endif
	if (!pmap_valid_entry(opde) || (opde & PG_FRAME) != pmap->pm_pdirpa) {
		*APDP_PDE = (pd_entry_t) (pmap->pm_pdirpa | PG_RW | PG_V |
		    PG_U | PG_M);
		if (pmap_valid_entry(opde))
			pmap_apte_flush();
	}
	return(APTE_BASE);
}

/*
 * pmap_unmap_ptes: unlock the PTE mapping of "pmap"
 */

void
pmap_unmap_ptes_86(struct pmap *pmap)
{
	if (pmap == pmap_kernel())
		return;

	if (!pmap_is_curpmap(pmap)) {
#if defined(MULTIPROCESSOR)
		*APDP_PDE = 0;
		pmap_apte_flush();
#endif
		mtx_leave(&curcpu()->ci_curpmap->pm_apte_mtx);
	}

	mtx_leave(&pmap->pm_mtx);
}

void
pmap_exec_account(struct pmap *pm, vaddr_t va,
    uint32_t opte, uint32_t npte)
{
	if (pm == pmap_kernel())
		return;

	if (curproc->p_vmspace == NULL ||
	    pm != vm_map_pmap(&curproc->p_vmspace->vm_map))
		return;

	if ((opte ^ npte) & PG_X)
		pmap_tlb_shootpage(pm, va);
			
	if (cpu_pae)
		return;

	/*
	 * Executability was removed on the last executable change.
	 * Reset the code segment to something conservative and
	 * let the trap handler deal with setting the right limit.
	 * We can't do that because of locking constraints on the vm map.
	 *
	 * XXX - floating cs - set this _really_ low.
	 */
	if ((opte & PG_X) && (npte & PG_X) == 0 && va == pm->pm_hiexec) {
		struct trapframe *tf = curproc->p_md.md_regs;
		struct pcb *pcb = &curproc->p_addr->u_pcb;

		KERNEL_LOCK();
		pm->pm_hiexec = I386_MAX_EXE_ADDR;
		setcslimit(pm, tf, pcb, I386_MAX_EXE_ADDR);
		KERNEL_UNLOCK();
	}
}

/*
 * Fixup the code segment to cover all potential executable mappings.
 * Called by kernel SEGV trap handler.
 * returns 0 if no changes to the code segment were made.
 */
int
pmap_exec_fixup(struct vm_map *map, struct trapframe *tf, vaddr_t gdt_cs,
    struct pcb *pcb)
{
	struct vm_map_entry *ent;
	struct pmap *pm = vm_map_pmap(map);
	vaddr_t va = 0;
	vaddr_t pm_cs;

	KERNEL_LOCK();

	vm_map_lock(map);
	RBT_FOREACH_REVERSE(ent, uvm_map_addr, &map->addr) {
		if (ent->protection & PROT_EXEC)
			break;
	}
	/*
	 * This entry has greater va than the entries before.
	 * We need to make it point to the last page, not past it.
	 */
	if (ent)
		va = trunc_page(ent->end - 1);
	vm_map_unlock(map);

	KERNEL_ASSERT_LOCKED();

	pm_cs = SEGDESC_LIMIT(pm->pm_codeseg);

	/*
	 * Another thread running on another cpu can change
	 * pm_hiexec and pm_codeseg. If this has happened
	 * during our timeslice, our gdt code segment will
	 * be stale. So only allow the fault through if the
	 * faulting address is less then pm_hiexec and our
	 * gdt code segment is not stale.
	 */
	if (va <= pm->pm_hiexec && pm_cs == pm->pm_hiexec &&
	    gdt_cs == pm->pm_hiexec) {
		KERNEL_UNLOCK();
		return (0);
	}

	pm->pm_hiexec = va;

	/*
	 * We have a new 'highest executable' va, so we need to update
	 * the value for the code segment limit, which is stored in the
	 * PCB.
	 */
	setcslimit(pm, tf, pcb, va);

	KERNEL_UNLOCK();
	return (1);
}

u_int32_t
pmap_pte_set_86(vaddr_t va, paddr_t pa, u_int32_t bits)
{
	pt_entry_t pte, *ptep = vtopte(va);

	pa &= PMAP_PA_MASK;

	pte = i386_atomic_testset_ul(ptep, pa | bits);  /* zap! */
	return (pte & ~PG_FRAME);
}

u_int32_t
pmap_pte_setbits_86(vaddr_t va, u_int32_t set, u_int32_t clr)
{
	pt_entry_t *ptep = vtopte(va);
	pt_entry_t pte = *ptep;

	*ptep = (pte | set) & ~clr;
	return (pte & ~PG_FRAME);
}

u_int32_t
pmap_pte_bits_86(vaddr_t va)
{
	pt_entry_t *ptep = vtopte(va);

	return (*ptep & ~PG_FRAME);
}

paddr_t
pmap_pte_paddr_86(vaddr_t va)
{
	pt_entry_t *ptep = vtopte(va);

	return (*ptep & PG_FRAME);
}

/*
 * pmap_tmpmap_pa: map a page in for tmp usage
 */

vaddr_t
pmap_tmpmap_pa_86(paddr_t pa)
{
#ifdef MULTIPROCESSOR
	int id = cpu_number();
#endif
	pt_entry_t *ptpte;
	caddr_t ptpva;

	ptpte = PTESLEW(ptp_pte, id);
	ptpva = VASLEW(pmap_ptpp, id);

#if defined(DIAGNOSTIC)
	if (*ptpte)
		panic("pmap_tmpmap_pa: ptp_pte in use?");
#endif
	*ptpte = PG_V | PG_RW | pa;	/* always a new mapping */
	return((vaddr_t)ptpva);
}


vaddr_t
pmap_tmpmap_pa(paddr_t pa)
{
	if (cpu_pae)
		return pmap_tmpmap_pa_pae(pa);

	return pmap_tmpmap_pa_86(pa);
}

/*
 * pmap_tmpunmap_pa: unmap a tmp use page (undoes pmap_tmpmap_pa)
 */

void
pmap_tmpunmap_pa_86(void)
{
#ifdef MULTIPROCESSOR
	int id = cpu_number();
#endif
	pt_entry_t *ptpte;
	caddr_t ptpva;

	ptpte = PTESLEW(ptp_pte, id);
	ptpva = VASLEW(pmap_ptpp, id);

#if defined(DIAGNOSTIC)
	if (!pmap_valid_entry(*ptpte))
		panic("pmap_tmpunmap_pa: our pte invalid?");
#endif

	*ptpte = 0;
	pmap_update_pg((vaddr_t)ptpva);
#ifdef MULTIPROCESSOR
	/*
	 * No need for tlb shootdown here, since ptp_pte is per-CPU.
	 */
#endif
}

void
pmap_tmpunmap_pa(void)
{
	if (cpu_pae) {
		pmap_tmpunmap_pa_pae();
		return;
	}

	pmap_tmpunmap_pa_86();
}

paddr_t
vtophys(vaddr_t va)
{
	if (cpu_pae)
		return vtophys_pae(va);
	else
		return ((*vtopte(va) & PG_FRAME) | (va & ~PG_FRAME));
}

void
setcslimit(struct pmap *pm, struct trapframe *tf, struct pcb *pcb,
    vaddr_t limit)
{
	/*
	 * Called when we have a new 'highest executable' va, so we need
	 * to update the value for the code segment limit, which is stored
	 * in the PCB.
	 *
	 * There are no caching issues to be concerned with: the
	 * processor reads the whole descriptor from the GDT when the
	 * appropriate selector is loaded into a segment register, and
	 * this only happens on the return to userland.
	 *
	 * This also works in the MP case, since whichever CPU gets to
	 * run the process will pick up the right descriptor value from
	 * the PCB.
	 */
	limit = min(limit, VM_MAXUSER_ADDRESS - 1);

	setsegment(&pm->pm_codeseg, 0, atop(limit),
	    SDT_MEMERA, SEL_UPL, 1, 1);

	/* And update the GDT since we may be called by the
	 * trap handler (cpu_switch won't get a chance).
	 */
	curcpu()->ci_gdt[GUCODE_SEL].sd = pm->pm_codeseg;

	tf->tf_cs = GSEL(GUCODE_SEL, SEL_UPL);
}

/*
 * p m a p   k e n t e r   f u n c t i o n s
 *
 * functions to quickly enter/remove pages from the kernel address
 * space.   pmap_kremove is exported to MI kernel.  we make use of
 * the recursive PTE mappings.
 */

/*
 * pmap_kenter_pa: enter a kernel mapping without R/M (pv_entry) tracking
 *
 * => no need to lock anything, assume va is already allocated
 * => should be faster than normal pmap enter function
 */

void
pmap_kenter_pa(vaddr_t va, paddr_t pa, vm_prot_t prot)
{
	uint32_t bits;
	uint32_t global = 0;

	/* special 1:1 mappings in the first large page must not be global */
	if (!cpu_pae) {
		if (va >= (vaddr_t)NBPD)	/* 4MB pages on non-PAE */
			global = pmap_pg_g;
	} else {
		if (va >= (vaddr_t)NBPD / 2)	/* 2MB pages on PAE */
			global = pmap_pg_g;
	}

	bits = pmap_pte_set(va, pa, ((prot & PROT_WRITE) ? PG_RW : PG_RO) |
		PG_V | global | PG_U | PG_M |
		((prot & PROT_EXEC) ? PG_X : 0) |
		((pa & PMAP_NOCACHE) ? PG_N : 0) |
		((pa & PMAP_WC) ? pmap_pg_wc : 0));
	if (pmap_valid_entry(bits)) {
		if (pa & PMAP_NOCACHE && (bits & PG_N) == 0)
			wbinvd_on_all_cpus();
		/* NB. - this should not happen. */
		pmap_tlb_shootpage(pmap_kernel(), va);
		pmap_tlb_shootwait();
	}
}

/*
 * pmap_kremove: remove a kernel mapping(s) without R/M (pv_entry) tracking
 *
 * => no need to lock anything
 * => caller must dispose of any vm_page mapped in the va range
 * => note: not an inline function
 * => we assume the va is page aligned and the len is a multiple of PAGE_SIZE
 */

void
pmap_kremove(vaddr_t sva, vsize_t len)
{
	uint32_t bits;
	vaddr_t va, eva;

	eva = sva + len;

	for (va = sva; va != eva; va += PAGE_SIZE) {
		bits = pmap_pte_set(va, 0, 0);
#ifdef DIAGNOSTIC
		if (bits & PG_PVLIST)
			panic("pmap_kremove: PG_PVLIST mapping for 0x%lx", va);
#endif
	}
	pmap_tlb_shootrange(pmap_kernel(), sva, eva);
	pmap_tlb_shootwait();
}

/*
 * Allocate a new PD for Intel's U-K.
 */
void
pmap_alloc_pdir_intel_x86(struct pmap *pmap)
{
	vaddr_t va;

	KASSERT(pmap->pm_pdir_intel == 0);

	va = (vaddr_t)km_alloc(NBPG, &kv_any, &kp_zero, &kd_waitok);
	if (va == 0)
		panic("kernel_map out of virtual space");
	pmap->pm_pdir_intel = va;
	if (!pmap_extract(pmap_kernel(), (vaddr_t)pmap->pm_pdir_intel,
	    &pmap->pm_pdirpa_intel))
		panic("can't locate PD page");
}

/*
 * p m a p   i n i t   f u n c t i o n s
 *
 * pmap_bootstrap and pmap_init are called during system startup
 * to init the pmap module.   pmap_bootstrap() does a low level
 * init just to get things rolling.   pmap_init() finishes the job.
 */

/*
 * pmap_bootstrap: get the system in a state where it can run with VM
 *	properly enabled (called before main()).   the VM system is
 *      fully init'd later...
 *
 * => on i386, locore.s has already enabled the MMU by allocating
 *	a PDP for the kernel, and nkpde PTPs for the kernel.
 * => kva_start is the first free virtual address in kernel space
 */

void
pmap_bootstrap(vaddr_t kva_start)
{
	struct pmap *kpm;
	vaddr_t kva;
	pt_entry_t *pte;

	/*
	 * set the page size (default value is 4K which is ok)
	 */

	uvm_setpagesize();

	/*
	 * a quick sanity check
	 */

	if (PAGE_SIZE != NBPG)
		panic("pmap_bootstrap: PAGE_SIZE != NBPG");

	/*
	 * set up our local static global vars that keep track of the
	 * usage of KVM before kernel_map is set up
	 */

	virtual_avail = kva_start;		/* first free KVA */
	virtual_end = VM_MAX_KERNEL_ADDRESS;	/* last KVA */

	/*
	 * set up protection_codes: we need to be able to convert from
	 * a MI protection code (some combo of VM_PROT...) to something
	 * we can jam into a i386 PTE.
	 */

	protection_codes[PROT_NONE] = 0;  			/* --- */
	protection_codes[PROT_EXEC] = PG_X;			/* --x */
	protection_codes[PROT_READ] = PG_RO;			/* -r- */
	protection_codes[PROT_READ | PROT_EXEC] = PG_X;		/* -rx */
	protection_codes[PROT_WRITE] = PG_RW;			/* w-- */
	protection_codes[PROT_WRITE | PROT_EXEC] = PG_RW|PG_X;	/* w-x */
	protection_codes[PROT_READ | PROT_WRITE] = PG_RW;	/* wr- */
	protection_codes[PROT_READ | PROT_WRITE | PROT_EXEC] = PG_RW|PG_X; /* wrx */

	/*
	 * now we init the kernel's pmap
	 *
	 * the kernel pmap's pm_obj is not used for much.   however, in
	 * user pmaps the pm_obj contains the list of active PTPs.
	 * the pm_obj currently does not have a pager.   it might be possible
	 * to add a pager that would allow a process to read-only mmap its
	 * own page tables (fast user level vtophys?).   this may or may not
	 * be useful.
	 */

	kpm = pmap_kernel();
	mtx_init(&kpm->pm_mtx, -1); /* must not be used */
	mtx_init(&kpm->pm_apte_mtx, IPL_VM);
	uvm_obj_init(&kpm->pm_obj, &pmap_pager, 1);
	bzero(&kpm->pm_list, sizeof(kpm->pm_list));  /* pm_list not used */
	kpm->pm_pdir = (vaddr_t)(proc0.p_addr->u_pcb.pcb_cr3 + KERNBASE);
	kpm->pm_pdirpa = proc0.p_addr->u_pcb.pcb_cr3;
	kpm->pm_pdir_intel = 0;
	kpm->pm_pdirpa_intel = 0;
	kpm->pm_stats.wired_count = kpm->pm_stats.resident_count =
		atop(kva_start - VM_MIN_KERNEL_ADDRESS);

	/*
	 * the above is just a rough estimate and not critical to the proper
	 * operation of the system.
	 */

	/*
	 * enable global TLB entries if they are supported and the
	 * CPU is not affected by Meltdown.
	 */

	if (cpu_feature & CPUID_PGE) {
		lcr4(rcr4() | CR4_PGE);	/* enable hardware (via %cr4) */
		pmap_pg_g = pg_g_kern;	/* if safe to use, enable software */

		/* add PG_G attribute to already mapped kernel pages */
		for (kva = VM_MIN_KERNEL_ADDRESS; kva < virtual_avail;
		     kva += PAGE_SIZE)
			if (pmap_valid_entry(PTE_BASE[atop(kva)]))
				PTE_BASE[atop(kva)] |= pmap_pg_g;
	}

	/*
	 * now we allocate the "special" VAs which are used for tmp mappings
	 * by the pmap (and other modules).    we allocate the VAs by advancing
	 * virtual_avail (note that there are no pages mapped at these VAs).
	 * we find the PTE that maps the allocated VA via the linear PTE
	 * mapping.
	 */

	pte = PTE_BASE + atop(virtual_avail);

#ifdef MULTIPROCESSOR
	/*
	 * Waste some VA space to avoid false sharing of cache lines
	 * for page table pages: Give each possible CPU a cache line
	 * of PTEs (16) to play with, though we only need 4.  We could
	 * recycle some of this waste by putting the idle stacks here
	 * as well; we could waste less space if we knew the largest
	 * CPU ID beforehand.
	 */
	pmap_csrcp = (caddr_t) virtual_avail;  csrc_pte = pte;

	pmap_cdstp = (caddr_t) virtual_avail+PAGE_SIZE;  cdst_pte = pte+1;

	pmap_zerop = (caddr_t) virtual_avail+PAGE_SIZE*2;  zero_pte = pte+2;

	pmap_ptpp = (caddr_t) virtual_avail+PAGE_SIZE*3;  ptp_pte = pte+3;

	pmap_flshp = (caddr_t) virtual_avail+PAGE_SIZE*4;  flsh_pte = pte+4;

	virtual_avail += PAGE_SIZE * MAXCPUS * NPTECL;
	pte += MAXCPUS * NPTECL;
#else
	pmap_csrcp = (caddr_t) virtual_avail;  csrc_pte = pte;	/* allocate */
	virtual_avail += PAGE_SIZE; pte++;			/* advance */

	pmap_cdstp = (caddr_t) virtual_avail;  cdst_pte = pte;
	virtual_avail += PAGE_SIZE; pte++;

	pmap_zerop = (caddr_t) virtual_avail;  zero_pte = pte;
	virtual_avail += PAGE_SIZE; pte++;

	pmap_ptpp = (caddr_t) virtual_avail;  ptp_pte = pte;
	virtual_avail += PAGE_SIZE; pte++;

	pmap_flshp = (caddr_t) virtual_avail;  flsh_pte = pte;
	virtual_avail += PAGE_SIZE; pte++;
#endif

	/* XXX: vmmap used by mem.c... should be uvm_map_reserve */
	vmmap = (char *)virtual_avail;			/* don't need pte */
	virtual_avail += PAGE_SIZE;

	msgbufp = (struct msgbuf *)virtual_avail;	/* don't need pte */
	virtual_avail += round_page(MSGBUFSIZE); pte++;

	bootargp = (bootarg_t *)virtual_avail;
	virtual_avail += round_page(bootargc); pte++;

	/*
	 * now we reserve some VM for mapping pages when doing a crash dump
	 */

	virtual_avail = reserve_dumppages(virtual_avail);

	/*
	 * init the static-global locks and global lists.
	 */

	LIST_INIT(&pmaps);

	/*
	 * initialize the pmap pool.
	 */

	pool_init(&pmap_pmap_pool, sizeof(struct pmap), 32, IPL_NONE, 0,
	    "pmappl", NULL);
	pool_init(&pmap_pv_pool, sizeof(struct pv_entry), 0, IPL_VM, 0,
	    "pvpl", &pmap_pv_page_allocator);

	/*
	 * ensure the TLB is sync'd with reality by flushing it...
	 */

	tlbflush();
}

/*
 * Pre-allocate PTP 0 for low memory, so that 1:1 mappings for various
 * trampoline code can be entered.
 */
void
pmap_prealloc_lowmem_ptp(void)
{
	pt_entry_t *pte, npte;
	vaddr_t ptpva = (vaddr_t)vtopte(0);

	/* If PAE, use the PAE-specific preallocator */
	if (cpu_pae) {
		pmap_prealloc_lowmem_ptp_pae();
		return;
	}

	/* enter pa for pte 0 into recursive map */
	pte = vtopte(ptpva);
	npte = PTP0_PA | PG_RW | PG_V | PG_U | PG_M;

	i386_atomic_testset_ul(pte, npte);

	/* make sure it is clean before using */
	memset((void *)ptpva, 0, NBPG);
}

/*
 * pmap_init: called from uvm_init, our job is to get the pmap
 * system ready to manage mappings... this mainly means initing
 * the pv_entry stuff.
 */

void
pmap_init(void)
{
	/*
	 * prime the pool with pv_entry structures to allow us to get
	 * the kmem_map allocated and inited (done after this function
	 * is finished).  we do this by setting a low water mark such
	 * that we are more likely to have these around in extreme
	 * memory starvation.
	 */

	pool_setlowat(&pmap_pv_pool, PVE_LOWAT);
	pool_sethiwat(&pmap_pv_pool, PVE_HIWAT);

	/*
	 * done: pmap module is up (and ready for business)
	 */

	pmap_initialized = 1;
}

/*
 * p v _ e n t r y   f u n c t i o n s
 */

void *
pmap_pv_page_alloc(struct pool *pp, int flags, int *slowdown)
{
	struct kmem_dyn_mode kd = KMEM_DYN_INITIALIZER;

	kd.kd_waitok = ISSET(flags, PR_WAITOK);
	kd.kd_slowdown = slowdown;

	return (km_alloc(pp->pr_pgsize,
	    pmap_initialized ? &kv_page : &kv_any, pp->pr_crange, &kd));
}

void
pmap_pv_page_free(struct pool *pp, void *v)
{
	km_free(v, pp->pr_pgsize, &kv_page, pp->pr_crange);
}

/*
 * main pv_entry manipulation functions:
 *   pmap_enter_pv: enter a mapping onto a pv list
 *   pmap_remove_pv: remove a mapping from a pv list
 */

/*
 * pmap_enter_pv: enter a mapping onto a pv list
 *
 * => caller should have pmap locked
 * => we will gain the lock on the pv and allocate the new pv_entry
 * => caller should adjust ptp's wire_count before calling
 *
 * pve: preallocated pve for us to use
 * ptp: PTP in pmap that maps this VA
 */

void
pmap_enter_pv(struct vm_page *pg, struct pv_entry *pve, struct pmap *pmap,
    vaddr_t va, struct vm_page *ptp)
{
	pve->pv_pmap = pmap;
	pve->pv_va = va;
	pve->pv_ptp = ptp;			/* NULL for kernel pmap */
	mtx_enter(&pg->mdpage.pv_mtx);
	pve->pv_next = pg->mdpage.pv_list;	/* add to ... */
	pg->mdpage.pv_list = pve;		/* ... locked list */
	mtx_leave(&pg->mdpage.pv_mtx);
}

/*
 * pmap_remove_pv: try to remove a mapping from a pv_list
 *
 * => pmap should be locked
 * => caller should hold lock on pv [so that attrs can be adjusted]
 * => caller should adjust ptp's wire_count and free PTP if needed
 * => we return the removed pve
 */

struct pv_entry *
pmap_remove_pv(struct vm_page *pg, struct pmap *pmap, vaddr_t va)
{
	struct pv_entry *pve, **prevptr;

	mtx_enter(&pg->mdpage.pv_mtx);
	prevptr = &pg->mdpage.pv_list;		/* previous pv_entry pointer */
	while ((pve = *prevptr) != NULL) {
		if (pve->pv_pmap == pmap && pve->pv_va == va) {	/* match? */
			*prevptr = pve->pv_next;		/* remove it! */
			break;
		}
		prevptr = &pve->pv_next;		/* previous pointer */
	}
	mtx_leave(&pg->mdpage.pv_mtx);
	return(pve);				/* return removed pve */
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
 */

struct vm_page *
pmap_alloc_ptp_86(struct pmap *pmap, int pde_index, pt_entry_t pde_flags)
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
	 * usermode addresses, just copy the PDE to the U-K page
	 * table.
	 */
	if (pmap->pm_pdir_intel && ptp_i2v(pde_index) < VM_MAXUSER_ADDRESS) {
		pva_intel = (pd_entry_t *)pmap->pm_pdir_intel;
		pva_intel[pde_index] = PDE(pmap, pde_index);
		DPRINTF("%s: copying usermode PDE (content=0x%x) pde_index %d "
		    "from 0x%x -> 0x%x\n", __func__, PDE(pmap, pde_index),
		    pde_index, (uint32_t)&PDE(pmap, pde_index),
		    (uint32_t)&(pva_intel[pde_index]));
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
pmap_get_ptp_86(struct pmap *pmap, int pde_index)
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
			panic("pmap_get_ptp_86: unmanaged user PTP");
#endif
		pmap->pm_ptphint = ptp;
		return(ptp);
	}

	/* allocate a new PTP (updates ptphint) */
	return (pmap_alloc_ptp_86(pmap, pde_index, PG_u));
}

void
pmap_drop_ptp_86(struct pmap *pm, vaddr_t va, struct vm_page *ptp,
    pt_entry_t *ptes)
{
	pd_entry_t *pva_intel;

	i386_atomic_testset_ul(&PDE(pm, pdei(va)), 0);
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
		i386_atomic_testset_ul(&pva_intel[pdei(va)], 0);
		DPRINTF("%s: cleared meltdown PDE @ index %lu "
		    "(va range start 0x%x)\n", __func__, pdei(va),
		    (uint32_t)va);
	}
}

/*
 * p m a p  l i f e c y c l e   f u n c t i o n s
 */

/*
 * pmap_create: create a pmap
 *
 * => note: old pmap interface took a "size" args which allowed for
 *	the creation of "software only" pmaps (not in bsd).
 */

struct pmap *
pmap_create(void)
{
	struct pmap *pmap;

	pmap = pool_get(&pmap_pmap_pool, PR_WAITOK);

	mtx_init(&pmap->pm_mtx, IPL_VM);
	mtx_init(&pmap->pm_apte_mtx, IPL_VM);

	/* init uvm_object */
	uvm_obj_init(&pmap->pm_obj, &pmap_pager, 1);
	pmap->pm_stats.wired_count = 0;
	pmap->pm_stats.resident_count = 1;	/* count the PDP allocd below */
	pmap->pm_ptphint = NULL;
	pmap->pm_hiexec = 0;
	pmap->pm_flags = 0;
	pmap->pm_pdir_intel = 0;
	pmap->pm_pdirpa_intel = 0;

	initcodesegment(&pmap->pm_codeseg);

	pmap_pinit_pd(pmap);
	return (pmap);
}

void
pmap_pinit_pd_86(struct pmap *pmap)
{
	/* allocate PDP */
	pmap->pm_pdir = (vaddr_t)km_alloc(NBPG, &kv_any, &kp_dirty, &kd_waitok);
	if (pmap->pm_pdir == 0)
		panic("kernel_map out of virtual space");
	pmap_extract(pmap_kernel(), (vaddr_t)pmap->pm_pdir,
			    &pmap->pm_pdirpa);
	pmap->pm_pdirsize = NBPG;

	/* init PDP */
	/* zero init area */
	bzero((void *)pmap->pm_pdir, PDSLOT_PTE * sizeof(pd_entry_t));
	/* put in recursive PDE to map the PTEs */
	PDE(pmap, PDSLOT_PTE) = pmap->pm_pdirpa | PG_V | PG_KW | PG_U | PG_M;
	PDE(pmap, PDSLOT_PTE + 1) = 0;

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
	bzero(&PDE(pmap, PDSLOT_KERN + nkpde),
	       NBPG - ((PDSLOT_KERN + nkpde) * sizeof(pd_entry_t)));

	/*
	 * Intel CPUs need a special page table to be used during usermode
	 * execution, one that lacks all kernel mappings.
	 */
	if (cpu_meltdown) {
		pmap_alloc_pdir_intel_x86(pmap);

		/* Copy PDEs from pmap_kernel's U-K view */
		bcopy((void *)pmap_kernel()->pm_pdir_intel,
		    (void *)pmap->pm_pdir_intel, NBPG);

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
 * pmap_destroy: drop reference count on pmap.   free pmap if
 *	reference count goes to zero.
 */

void
pmap_destroy(struct pmap *pmap)
{
	struct vm_page *pg;
	int refs;

	refs = atomic_dec_int_nv(&pmap->pm_obj.uo_refs);
	if (refs > 0)
		return;

#ifdef MULTIPROCESSOR
	pmap_tlb_droppmap(pmap);	
#endif

	mtx_enter(&pmaps_lock);
	LIST_REMOVE(pmap, pm_list);
	mtx_leave(&pmaps_lock);

	/* Free any remaining PTPs. */
	while ((pg = RBT_ROOT(uvm_objtree, &pmap->pm_obj.memt)) != NULL) {
		pg->wire_count = 0;
		uvm_pagefree(pg);
	}

	km_free((void *)pmap->pm_pdir, pmap->pm_pdirsize, &kv_any, &kp_dirty);
	pmap->pm_pdir = 0;

	if (pmap->pm_pdir_intel) {
		km_free((void *)pmap->pm_pdir_intel, pmap->pm_pdirsize,
		    &kv_any, &kp_dirty);
		pmap->pm_pdir_intel = 0;
	}

	pool_put(&pmap_pmap_pool, pmap);
}


/*
 *	Add a reference to the specified pmap.
 */

void
pmap_reference(struct pmap *pmap)
{
	atomic_inc_int(&pmap->pm_obj.uo_refs);
}

void
pmap_activate(struct proc *p)
{
	KASSERT(curproc == p);
	KASSERT(&p->p_addr->u_pcb == curpcb);
	pmap_switch(NULL, p);
}

int nlazy_cr3_hit;
int nlazy_cr3;

void
pmap_switch(struct proc *o, struct proc *p)
{
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct pmap *pmap, *opmap;
	struct cpu_info *self = curcpu();

	pmap = p->p_vmspace->vm_map.pmap;
	opmap = self->ci_curpmap;

	pcb->pcb_pmap = pmap;
	pcb->pcb_cr3 = pmap->pm_pdirpa;

	if (opmap == pmap) {
		if (pmap != pmap_kernel())
			nlazy_cr3_hit++;
	} else {
		self->ci_curpmap = pmap;
		lcr3(pmap->pm_pdirpa);
	}

	/*
	 * Meltdown: iff we're doing separate U+K and U-K page tables,
	 * then record them in cpu_info for easy access in syscall and
	 * interrupt trampolines.
	 */
	if (pmap->pm_pdirpa_intel) {
		self->ci_kern_cr3 = pmap->pm_pdirpa;
		self->ci_user_cr3 = pmap->pm_pdirpa_intel;
	}

	/*
	 * Set the correct descriptor value (i.e. with the
	 * correct code segment X limit) in the GDT.
	 */
	self->ci_gdt[GUCODE_SEL].sd = pmap->pm_codeseg;
	self->ci_gdt[GUFS_SEL].sd = pcb->pcb_threadsegs[TSEG_FS];
	self->ci_gdt[GUGS_SEL].sd = pcb->pcb_threadsegs[TSEG_GS];
}

void
pmap_deactivate(struct proc *p)
{
}

/*
 * pmap_extract: extract a PA for the given VA
 */

int
pmap_extract_86(struct pmap *pmap, vaddr_t va, paddr_t *pap)
{
	pt_entry_t *ptes, pte;

	ptes = pmap_map_ptes_86(pmap);
	if (pmap_valid_entry(PDE(pmap, pdei(va)))) {
		pte = ptes[atop(va)];
		pmap_unmap_ptes_86(pmap);
		if (!pmap_valid_entry(pte))
			return 0;
		if (pap != NULL)
			*pap = (pte & PG_FRAME) | (va & ~PG_FRAME);
		return 1;
	}
	pmap_unmap_ptes_86(pmap);
	return 0;
}

/*
 * pmap_virtual_space: used during bootup [uvm_pageboot_alloc] to
 *	determine the bounds of the kernel virtual address space.
 */

void
pmap_virtual_space(vaddr_t *startp, vaddr_t *endp)
{
	*startp = virtual_avail;
	*endp = virtual_end;
}

/*
 * pmap_zero_page: zero a page
 */
void (*pagezero)(void *, size_t) = bzero;

void
pmap_zero_page(struct vm_page *pg)
{
	pmap_zero_phys(VM_PAGE_TO_PHYS(pg));
}

/*
 * pmap_zero_phys: same as pmap_zero_page, but for use before vm_pages are
 * initialized.
 */
void
pmap_zero_phys_86(paddr_t pa)
{
#ifdef MULTIPROCESSOR
	int id = cpu_number();
#endif
	pt_entry_t *zpte = PTESLEW(zero_pte, id);
	caddr_t zerova = VASLEW(pmap_zerop, id);

#ifdef DIAGNOSTIC
	if (*zpte)
		panic("pmap_zero_phys_86: lock botch");
#endif

	*zpte = (pa & PG_FRAME) | PG_V | PG_RW;	/* map in */
	pmap_update_pg((vaddr_t)zerova);	/* flush TLB */
	pagezero(zerova, PAGE_SIZE);		/* zero */
	*zpte = 0;
}

/*
 * pmap_flush_cache: flush the cache for a virtual address.
 */
void
pmap_flush_cache(vaddr_t addr, vsize_t len)
{
	vaddr_t i;

	if (curcpu()->ci_cflushsz == 0) {
		wbinvd_on_all_cpus();
		return;
	}
	
	mfence();
	for (i = addr; i < addr + len; i += curcpu()->ci_cflushsz)
		clflush(i);
	mfence();
}

void
pmap_flush_page(paddr_t pa)
{
#ifdef MULTIPROCESSOR
	int id = cpu_number();
#endif
	pt_entry_t *pte;
	caddr_t va;

	KDASSERT(PHYS_TO_VM_PAGE(pa) != NULL);

	if (cpu_pae) {
		pmap_flush_page_pae(pa);
		return;
	}	

	pte = PTESLEW(flsh_pte, id);
	va = VASLEW(pmap_flshp, id);

#ifdef DIAGNOSTIC
	if (*pte)
		panic("pmap_flush_page: lock botch");
#endif

	*pte = (pa & PG_FRAME) | PG_V | PG_RW;
	pmap_update_pg(va);
	pmap_flush_cache((vaddr_t)va, PAGE_SIZE);
	*pte = 0;
	pmap_update_pg(va);
}

/*
 * pmap_copy_page: copy a page
 */

void
pmap_copy_page_86(struct vm_page *srcpg, struct vm_page *dstpg)
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
		panic("pmap_copy_page_86: lock botch");
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
pmap_remove_ptes_86(struct pmap *pmap, struct vm_page *ptp, vaddr_t ptpva,
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
		opte = i386_atomic_testset_ul(pte, 0);

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
				panic("pmap_remove_ptes_86: managed page "
				     "without PG_PVLIST for 0x%lx", startva);
#endif
			continue;
		}

#ifdef DIAGNOSTIC
		if (pg == NULL)
			panic("pmap_remove_ptes_86: unmanaged page marked "
			      "PG_PVLIST, va = 0x%lx, pa = 0x%lx",
			      startva, (u_long)(opte & PG_FRAME));
#endif

		/* sync R/M bits */
		pmap_sync_flags_pte_86(pg, opte);
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
pmap_remove(struct pmap *pmap, vaddr_t sva, vaddr_t eva)
{
	pmap_do_remove(pmap, sva, eva, PMAP_REMOVE_ALL);
}

void
pmap_do_remove_86(struct pmap *pmap, vaddr_t sva, vaddr_t eva, int flags)
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

	ptes = pmap_map_ptes_86(pmap);	/* locks pmap */

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

		if (pdei(va) == PDSLOT_PTE)
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
					panic("pmap_do_remove_86: unmanaged "
					      "PTP detected");
#endif
			}
		}
		pmap_remove_ptes_86(pmap, ptp, (vaddr_t)&ptes[atop(va)],
		    va, blkendva, flags, &free_pvs);

		/* If PTP is no longer being used, free it. */
		if (ptp && ptp->wire_count <= 1) {
			pmap_drop_ptp_86(pmap, va, ptp, ptes);
			TAILQ_INSERT_TAIL(&empty_ptps, ptp, pageq);
		}

		if (!shootall)
			pmap_tlb_shootrange(pmap, va, blkendva);
	}

	if (shootall)
		pmap_tlb_shoottlb();

	pmap_unmap_ptes_86(pmap);
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
pmap_page_remove_86(struct vm_page *pg)
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

		ptes = pmap_map_ptes_86(pm);		/* locks pmap */

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
			pmap_unmap_ptes_86(pm);		/* unlocks pmap */
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
			printf("pmap_page_remove_86: pg=%p: va=%lx, "
				"pv_ptp=%p\n",
				pg, pve->pv_va, pve->pv_ptp);
			printf("pmap_page_remove_86: PTP's phys addr: "
				"actual=%x, recorded=%lx\n",
				(PDE(pve->pv_pmap, pdei(pve->pv_va)) &
				PG_FRAME), VM_PAGE_TO_PHYS(pve->pv_ptp));
			panic("pmap_page_remove_86: mapped managed page has "
				"invalid pv_ptp field");
		}
#endif
		opte = i386_atomic_testset_ul(&ptes[atop(pve->pv_va)], 0);

		if (opte & PG_W)
			pve->pv_pmap->pm_stats.wired_count--;
		pve->pv_pmap->pm_stats.resident_count--;

		/* sync R/M bits */
		pmap_sync_flags_pte_86(pg, opte);

		/* update the PTP reference count.  free if last reference. */
		if (pve->pv_ptp && --pve->pv_ptp->wire_count <= 1) {
			pmap_drop_ptp_86(pve->pv_pmap, pve->pv_va,
			    pve->pv_ptp, ptes);
			TAILQ_INSERT_TAIL(&empty_ptps, pve->pv_ptp, pageq);
		}

		pmap_tlb_shootpage(pve->pv_pmap, pve->pv_va);

		pmap_unmap_ptes_86(pve->pv_pmap);	/* unlocks pmap */
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
 */

int
pmap_test_attrs_86(struct vm_page *pg, int testbits)
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
pmap_clear_attrs_86(struct vm_page *pg, int clearbits)
{
	struct pv_entry *pve;
	pt_entry_t *ptes, opte;
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
			panic("pmap_clear_attrs_86: mapping without PTP "
			      "detected");
#endif

		opte = ptes[ptei(pve->pv_va)];
		if (opte & clearbits) {
			result = 1;
			i386_atomic_clearbits_l(&ptes[ptei(pve->pv_va)],
			    (opte & clearbits));
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
pmap_write_protect_86(struct pmap *pmap, vaddr_t sva, vaddr_t eva,
    vm_prot_t prot)
{
	pt_entry_t *ptes, *spte, *epte, npte, opte;
	vaddr_t blkendva;
	u_int32_t md_prot;
	vaddr_t va;
	int shootall = 0;

	ptes = pmap_map_ptes_86(pmap);		/* locks pmap */

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
		if (pdei(va) == PDSLOT_PTE)
			continue;

		/* empty block? */
		if (!pmap_valid_entry(PDE(pmap, pdei(va))))
			continue;

		md_prot = protection_codes[prot];
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
			npte = (opte & ~PG_PROT) | md_prot;

			if (npte != opte) {
				pmap_exec_account(pmap, va, *spte, npte);
				i386_atomic_clearbits_l(spte,
				    (~md_prot & opte) & PG_PROT);
				i386_atomic_setbits_l(spte, md_prot);
			}
		}
	}
	if (shootall)
		pmap_tlb_shoottlb();
	else
		pmap_tlb_shootrange(pmap, sva, eva);

	pmap_unmap_ptes_86(pmap);		/* unlocks pmap */
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
pmap_unwire_86(struct pmap *pmap, vaddr_t va)
{
	pt_entry_t *ptes;

	if (pmap_valid_entry(PDE(pmap, pdei(va)))) {
		ptes = pmap_map_ptes_86(pmap);		/* locks pmap */

#ifdef DIAGNOSTIC
		if (!pmap_valid_entry(ptes[atop(va)]))
			panic("pmap_unwire_86: invalid (unmapped) va "
			      "0x%lx", va);
#endif

		if ((ptes[atop(va)] & PG_W) != 0) {
			i386_atomic_clearbits_l(&ptes[atop(va)], PG_W);
			pmap->pm_stats.wired_count--;
		}
#ifdef DIAGNOSTIC
		else {
			printf("pmap_unwire_86: wiring for pmap %p va 0x%lx "
			       "didn't change!\n", pmap, va);
		}
#endif
		pmap_unmap_ptes_86(pmap);		/* unlocks map */
	}
#ifdef DIAGNOSTIC
	else {
		panic("pmap_unwire_86: invalid PDE");
	}
#endif
}

/*
 * pmap_enter: enter a mapping into a pmap
 *
 * => must be done "now" ... no lazy-evaluation
 */

int
pmap_enter_86(struct pmap *pmap, vaddr_t va, paddr_t pa,
    vm_prot_t prot, int flags)
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
		panic("pmap_enter_86: too big");

	if (va == (vaddr_t) PDP_BASE || va == (vaddr_t) APDP_BASE)
		panic("pmap_enter_86: trying to map over PDP/APDP!");

	/* sanity check: kernel PTPs should already have been pre-allocated */
	if (va >= VM_MIN_KERNEL_ADDRESS &&
	    !pmap_valid_entry(PDE(pmap, pdei(va))))
		panic("pmap_enter: missing kernel PTP!");
#endif
	if (pmap_initialized)
		pve = pool_get(&pmap_pv_pool, PR_NOWAIT);
	else
		pve = NULL;
	wired_count = resident_count = ptp_count = 0;

	/*
	 * map in ptes and get a pointer to our PTP (unless we are the kernel)
	 */

	ptes = pmap_map_ptes_86(pmap);		/* locks pmap */
	if (pmap == pmap_kernel()) {
		ptp = NULL;
	} else {
		ptp = pmap_get_ptp_86(pmap, pdei(va));
		if (ptp == NULL) {
			if (flags & PMAP_CANFAIL) {
				pmap_unmap_ptes_86(pmap);
				error = ENOMEM;
				goto out;
			}
			panic("pmap_enter_86: get ptp failed");
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
					panic("pmap_enter_86: same pa "
					     "PG_PVLIST mapping with "
					     "unmanaged page "
					     "pa = 0x%lx (0x%lx)", pa,
					     atop(pa));
#endif
				pmap_sync_flags_pte_86(pg, opte);
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
				panic("pmap_enter_86: PG_PVLIST mapping with "
				      "unmanaged page "
				      "pa = 0x%lx (0x%lx)", pa, atop(pa));
#endif
			pmap_sync_flags_pte_86(pg, opte);
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
				pmap_unmap_ptes_86(pmap);
				error = ENOMEM;
				goto out;
			}
			panic("pmap_enter_86: no pv entries available");
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
		pmap_sync_flags_pte_86(pg, npte);
	}
	if (wc)
		npte |= pmap_pg_wc;

	opte = i386_atomic_testset_ul(&ptes[atop(va)], npte);
	if (ptp)
		ptp->wire_count += ptp_count;
	pmap->pm_stats.resident_count += resident_count;
	pmap->pm_stats.wired_count += wired_count;

	if (pmap_valid_entry(opte)) {
		if (nocache && (opte & PG_N) == 0)
			wbinvd_on_all_cpus(); /* XXX clflush before we enter? */
		pmap_tlb_shootpage(pmap, va);
	}

	pmap_unmap_ptes_86(pmap);
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
 * Allocate an extra PD page and PT pages as needed to map kernel
 * pages used for the U-K mappings.  These special mappings are set
 * up during bootstrap and get never removed and are part of
 * pmap_kernel.
 *
 * New pmaps inherit the kernel portion of pmap_kernel including
 * the special mappings (see pmap_pinit_pd_86()).
 *
 * To be able to release PT pages when migrating to PAE paging, use
 * wire_count for number of PTEs in the PT page.
 */
void
pmap_enter_special_86(vaddr_t va, paddr_t pa, vm_prot_t prot, u_int32_t flags)
{
	struct pmap	*pmap = pmap_kernel();
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

	if (!pmap->pm_pdir_intel)
		pmap_alloc_pdir_intel_x86(pmap);

	DPRINTF("%s: pm_pdir_intel 0x%x pm_pdirpa_intel 0x%x\n", __func__,
	    (uint32_t)pmap->pm_pdir_intel, (uint32_t)pmap->pm_pdirpa_intel);

	l2idx = pdei(va);
	l1idx = ptei(va);

	DPRINTF("%s: va 0x%08lx pa 0x%08lx prot 0x%08lx flags 0x%08x "
	    "l2idx %u l1idx %u\n", __func__, va, pa, (unsigned long)prot,
	    flags, l2idx, l1idx);

	if ((pd = (pd_entry_t *)pmap->pm_pdir_intel) == NULL)
		panic("%s: PD not initialized for pmap @ %p", __func__, pmap);

	/* npa = physaddr of PT page */
	npa = pd[l2idx] & PMAP_PA_MASK;

	/* Valid PDE for the 4MB region containing va? */
	if (!npa) {
		/*
		 * No valid PDE - allocate PT page and set PDE.  We
		 * get it from pm_obj, which is used for PT pages.
		 * We calculate the offset  from l2idx+1024, so we are
		 * beyond the regular PT pages. For their l2dix
		 * 0 <= l2idx < 1024 holds.
		 */
		ptppg = uvm_pagealloc(&pmap->pm_obj, ptp_i2o(l2idx + 1024),
		    NULL, UVM_PGA_USERESERVE|UVM_PGA_ZERO);
		if (ptppg == NULL)
			panic("%s: failed to allocate PT page", __func__);

		atomic_clearbits_int(&ptppg->pg_flags, PG_BUSY);
		ptppg->wire_count = 1;	/* no mappings yet */

		npa = VM_PAGE_TO_PHYS(ptppg);
		pd[l2idx] = (npa | PG_RW | PG_V | PG_M | PG_U);

		DPRINTF("%s: allocated new PT page at phys 0x%x, "
		    "setting PDE[%d] = 0x%x\n", __func__, (uint32_t)npa,
		    l2idx, pd[l2idx]);
	}

	/* temporarily map PT page and set PTE for U-K mapping */
	if (ptppg == NULL && (ptppg = PHYS_TO_VM_PAGE(npa)) == NULL)
		panic("%s: no vm_page for PT page", __func__);
	mtx_enter(&ptppg->mdpage.pv_mtx);
	ptp = (pd_entry_t *)pmap_tmpmap_pa(npa);
	ptp[l1idx] = (pa | protection_codes[prot] | PG_V | PG_M | PG_U | flags);
	ptppg->wire_count++;
	DPRINTF("%s: setting PTE[%d] = 0x%x (wire_count %d)\n", __func__,
	    l1idx, ptp[l1idx], ptppg->wire_count);
	pmap_tmpunmap_pa();
	mtx_leave(&ptppg->mdpage.pv_mtx);

	/*
	 * if supported, set the PG_G flag on the corresponding U+K
	 * entry.  U+K mappings can use PG_G, as they are mapped
	 * along with user land anyway.
	 */
	if (!(cpu_feature & CPUID_PGE))
		return;
	ptes = pmap_map_ptes_86(pmap);	/* pmap_kernel -> PTE_BASE */
	if (pmap_valid_entry(ptes[atop(va)]))
		ptes[atop(va)] |= PG_G;
	else
		DPRINTF("%s: no U+K mapping for special mapping?\n", __func__);
	pmap_unmap_ptes_86(pmap);	/* pmap_kernel -> nothing */
}

/*
 * pmap_growkernel: increase usage of KVM space
 *
 * => we allocate new PTPs for the kernel and install them in all
 *	the pmaps on the system.
 */

vaddr_t
pmap_growkernel_86(vaddr_t maxkvaddr)
{
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
			pmap_zero_phys_86(ptaddr);

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

		while (!pmap_alloc_ptp_86(kpm, PDSLOT_KERN + nkpde, 0))
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

#ifdef MULTIPROCESSOR
/*
 * Locking for tlb shootdown.
 *
 * We lock by setting tlb_shoot_wait to the number of cpus that will
 * receive our tlb shootdown. After sending the IPIs, we don't need to
 * worry about locking order or interrupts spinning for the lock because
 * the call that grabs the "lock" isn't the one that releases it. And
 * there is nothing that can block the IPI that releases the lock.
 *
 * The functions are organized so that we first count the number of
 * cpus we need to send the IPI to, then we grab the counter, then
 * we send the IPIs, then we finally do our own shootdown.
 *
 * Our shootdown is last to make it parallel with the other cpus
 * to shorten the spin time.
 *
 * Notice that we depend on failures to send IPIs only being able to
 * happen during boot. If they happen later, the above assumption
 * doesn't hold since we can end up in situations where noone will
 * release the lock if we get an interrupt in a bad moment.
 */

volatile int tlb_shoot_wait __attribute__((section(".kudata")));

volatile vaddr_t tlb_shoot_addr1 __attribute__((section(".kudata")));
volatile vaddr_t tlb_shoot_addr2 __attribute__((section(".kudata")));

void
pmap_tlb_shootpage(struct pmap *pm, vaddr_t va)
{
	struct cpu_info *ci, *self = curcpu();
	CPU_INFO_ITERATOR cii;
	int wait = 0;
	u_int64_t mask = 0;

	CPU_INFO_FOREACH(cii, ci) {
		if (ci == self || !pmap_is_active(pm, ci) ||
		    !(ci->ci_flags & CPUF_RUNNING))
			continue;
		mask |= (1ULL << ci->ci_cpuid);
		wait++;
	}

	if (wait > 0) {
		int s = splvm();

		while (atomic_cas_uint(&tlb_shoot_wait, 0, wait) != 0) {
			while (tlb_shoot_wait != 0)
				CPU_BUSY_CYCLE();
		}
		tlb_shoot_addr1 = va;
		CPU_INFO_FOREACH(cii, ci) {
			if ((mask & (1ULL << ci->ci_cpuid)) == 0)
				continue;
			if (i386_fast_ipi(ci, LAPIC_IPI_INVLPG) != 0)
				panic("pmap_tlb_shootpage: ipi failed");
		}
		splx(s);
	}

	if (pmap_is_curpmap(pm))
		pmap_update_pg(va);
}

void
pmap_tlb_shootrange(struct pmap *pm, vaddr_t sva, vaddr_t eva)
{
	struct cpu_info *ci, *self = curcpu();
	CPU_INFO_ITERATOR cii;
	int wait = 0;
	u_int64_t mask = 0;
	vaddr_t va;

	CPU_INFO_FOREACH(cii, ci) {
		if (ci == self || !pmap_is_active(pm, ci) ||
		    !(ci->ci_flags & CPUF_RUNNING))
			continue;
		mask |= (1ULL << ci->ci_cpuid);
		wait++;
	}

	if (wait > 0) {
		int s = splvm();

		while (atomic_cas_uint(&tlb_shoot_wait, 0, wait) != 0) {
			while (tlb_shoot_wait != 0)
				CPU_BUSY_CYCLE();
		}
		tlb_shoot_addr1 = sva;
		tlb_shoot_addr2 = eva;
		CPU_INFO_FOREACH(cii, ci) {
			if ((mask & (1ULL << ci->ci_cpuid)) == 0)
				continue;
			if (i386_fast_ipi(ci, LAPIC_IPI_INVLRANGE) != 0)
				panic("pmap_tlb_shootrange: ipi failed");
		}
		splx(s);
	}

	if (pmap_is_curpmap(pm))
		for (va = sva; va < eva; va += PAGE_SIZE)
			pmap_update_pg(va);
}

void
pmap_tlb_shoottlb(void)
{
	struct cpu_info *ci, *self = curcpu();
	CPU_INFO_ITERATOR cii;
	int wait = 0;
	u_int64_t mask = 0;

	CPU_INFO_FOREACH(cii, ci) {
		if (ci == self || !(ci->ci_flags & CPUF_RUNNING))
			continue;
		mask |= (1ULL << ci->ci_cpuid);
		wait++;
	}

	if (wait) {
		int s = splvm();

		while (atomic_cas_uint(&tlb_shoot_wait, 0, wait) != 0) {
			while (tlb_shoot_wait != 0)
				CPU_BUSY_CYCLE();
		}

		CPU_INFO_FOREACH(cii, ci) {
			if ((mask & (1ULL << ci->ci_cpuid)) == 0)
				continue;
			if (i386_fast_ipi(ci, LAPIC_IPI_INVLTLB) != 0)
				panic("pmap_tlb_shoottlb: ipi failed");
		}
		splx(s);
	}

	tlbflush();
}

void
pmap_tlb_droppmap(struct pmap *pm)
{
	struct cpu_info *ci, *self = curcpu();
	CPU_INFO_ITERATOR cii;
	int wait = 0;
	u_int64_t mask = 0;

	CPU_INFO_FOREACH(cii, ci) {
		if (ci == self || !(ci->ci_flags & CPUF_RUNNING) ||
		    ci->ci_curpmap != pm)
			continue;
		mask |= (1ULL << ci->ci_cpuid);
		wait++;
	}

	if (wait) {
		int s = splvm();

		while (atomic_cas_uint(&tlb_shoot_wait, 0, wait) != 0) {
			while (tlb_shoot_wait != 0)
				CPU_BUSY_CYCLE();
		}

		CPU_INFO_FOREACH(cii, ci) {
			if ((mask & (1ULL << ci->ci_cpuid)) == 0)
				continue;
			if (i386_fast_ipi(ci, LAPIC_IPI_RELOADCR3) != 0)
				panic("pmap_tlb_droppmap: ipi failed");
		}
		splx(s);
	}

	if (self->ci_curpmap == pm)
		pmap_activate(curproc);

	pmap_tlb_shootwait();
}

void
pmap_tlb_shootwait(void)
{
	while (tlb_shoot_wait != 0)
		CPU_BUSY_CYCLE();
}

#else

void
pmap_tlb_shootpage(struct pmap *pm, vaddr_t va)
{
	if (pmap_is_curpmap(pm))
		pmap_update_pg(va);

}

void
pmap_tlb_shootrange(struct pmap *pm, vaddr_t sva, vaddr_t eva)
{
	vaddr_t va;

	for (va = sva; va < eva; va += PAGE_SIZE)
		pmap_update_pg(va);	
}

void
pmap_tlb_shoottlb(void)
{
	tlbflush();
}
#endif /* MULTIPROCESSOR */

u_int32_t	(*pmap_pte_set_p)(vaddr_t, paddr_t, u_int32_t) =
    pmap_pte_set_86;
u_int32_t	(*pmap_pte_setbits_p)(vaddr_t, u_int32_t, u_int32_t) =
    pmap_pte_setbits_86;
u_int32_t	(*pmap_pte_bits_p)(vaddr_t) = pmap_pte_bits_86;
paddr_t		(*pmap_pte_paddr_p)(vaddr_t) = pmap_pte_paddr_86;
int		(*pmap_clear_attrs_p)(struct vm_page *, int) =
    pmap_clear_attrs_86;
int		(*pmap_enter_p)(pmap_t, vaddr_t, paddr_t, vm_prot_t, int) =
    pmap_enter_86;
void		(*pmap_enter_special_p)(vaddr_t, paddr_t, vm_prot_t,
    u_int32_t) = pmap_enter_special_86;
int		(*pmap_extract_p)(pmap_t, vaddr_t, paddr_t *) =
    pmap_extract_86;
vaddr_t		(*pmap_growkernel_p)(vaddr_t) = pmap_growkernel_86;
void		(*pmap_page_remove_p)(struct vm_page *) = pmap_page_remove_86;
void		(*pmap_do_remove_p)(struct pmap *, vaddr_t, vaddr_t, int) =
    pmap_do_remove_86;
int		 (*pmap_test_attrs_p)(struct vm_page *, int) =
    pmap_test_attrs_86;
void		(*pmap_unwire_p)(struct pmap *, vaddr_t) = pmap_unwire_86;
void		(*pmap_write_protect_p)(struct pmap *, vaddr_t, vaddr_t,
    vm_prot_t) = pmap_write_protect_86;
void		(*pmap_pinit_pd_p)(pmap_t) = pmap_pinit_pd_86;
void		(*pmap_zero_phys_p)(paddr_t) = pmap_zero_phys_86;
void		(*pmap_copy_page_p)(struct vm_page *, struct vm_page *) =
    pmap_copy_page_86;
