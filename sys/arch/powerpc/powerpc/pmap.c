/*	$OpenBSD: pmap.c,v 1.185 2024/09/06 10:54:08 jsg Exp $ */

/*
 * Copyright (c) 2015 Martin Pieuchot
 * Copyright (c) 2001, 2002, 2007 Dale Rahn.
 * All rights reserved.
 *
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
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 */

/*
 * powerpc lazy icache management.
 * The icache does not snoop dcache accesses. The icache also will not load
 * modified data from the dcache, but the unmodified data in ram.
 * Before the icache is loaded, the dcache must be synced to ram to prevent
 * the icache from loading stale data.
 * pg->pg_flags PG_PMAP_EXE bit is used to track if the dcache is clean
 * and the icache may have valid data in it.
 * if the PG_PMAP_EXE bit is set (and the page is not currently RWX)
 * the icache will only have valid code in it. If the bit is clear
 * memory may not match the dcache contents or the icache may contain
 * data from a previous page.
 *
 * pmap enter
 * !E  NONE 	-> R	no action
 * !E  NONE|R 	-> RW	no action
 * !E  NONE|R 	-> RX	flush dcache, inval icache (that page only), set E
 * !E  NONE|R 	-> RWX	flush dcache, inval icache (that page only), set E
 * !E  NONE|RW 	-> RWX	flush dcache, inval icache (that page only), set E
 *  E  NONE 	-> R	no action
 *  E  NONE|R 	-> RW	clear PG_PMAP_EXE bit
 *  E  NONE|R 	-> RX	no action
 *  E  NONE|R 	-> RWX	no action
 *  E  NONE|RW 	-> RWX	-invalid source state
 *
 * pamp_protect
 *  E RW -> R	- invalid source state
 * !E RW -> R	- no action
 *  * RX -> R	- no action
 *  * RWX -> R	- sync dcache, inval icache
 *  * RWX -> RW	- clear PG_PMAP_EXE
 *  * RWX -> RX	- sync dcache, inval icache
 *  * * -> NONE	- no action
 * 
 * pmap_page_protect (called with arg PROT_NONE if page is to be reused)
 *  * RW -> R	- as pmap_protect
 *  * RX -> R	- as pmap_protect
 *  * RWX -> R	- as pmap_protect
 *  * RWX -> RW	- as pmap_protect
 *  * RWX -> RX	- as pmap_protect
 *  * * -> NONE - clear PG_PMAP_EXE
 * 
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/pool.h>
#include <sys/atomic.h>
#include <sys/user.h>

#include <uvm/uvm_extern.h>

#include <machine/pcb.h>
#include <powerpc/powerpc.h>
#include <powerpc/bat.h>
#include <machine/pmap.h>

struct bat battable[16];

struct dumpmem dumpmem[VM_PHYSSEG_MAX];
u_int ndumpmem;

struct pmap kernel_pmap_;
static struct mem_region *pmap_mem, *pmap_avail;
struct mem_region pmap_allocated[10];
int pmap_cnt_avail;
int pmap_cnt_allocated;

struct pte_64  *pmap_ptable64;
struct pte_32  *pmap_ptable32;
int	pmap_ptab_cnt;
u_int	pmap_ptab_mask;

#define HTABSIZE_32	(pmap_ptab_cnt * 64)
#define HTABMEMSZ_64	(pmap_ptab_cnt * 8 * sizeof(struct pte_64))
#define HTABSIZE_64	(ffs(pmap_ptab_cnt) - 12)

static u_int usedsr[NPMAPS / sizeof(u_int) / 8];

struct pte_desc {
	/* Linked list of phys -> virt entries */
	LIST_ENTRY(pte_desc) pted_pv_list;
	union {
		struct pte_32 pted_pte32;
		struct pte_64 pted_pte64;
	} p;
	pmap_t pted_pmap;
	vaddr_t pted_va;
};

void pmap_attr_save(paddr_t pa, u_int32_t bits);
void pmap_pted_ro(struct pte_desc *, vm_prot_t);
void pmap_pted_ro64(struct pte_desc *, vm_prot_t);
void pmap_pted_ro32(struct pte_desc *, vm_prot_t);

/*
 * Some functions are called in real mode and cannot be profiled.
 */
#define __noprof __attribute__((__no_instrument_function__))

/* VP routines */
int pmap_vp_enter(pmap_t pm, vaddr_t va, struct pte_desc *pted, int flags);
struct pte_desc *pmap_vp_remove(pmap_t pm, vaddr_t va);
void pmap_vp_destroy(pmap_t pm);
struct pte_desc *pmap_vp_lookup(pmap_t pm, vaddr_t va) __noprof;

/* PV routines */
void pmap_enter_pv(struct pte_desc *pted, struct vm_page *);
void pmap_remove_pv(struct pte_desc *pted);


/* pte hash table routines */
static inline void *pmap_ptedinhash(struct pte_desc *);
void pte_insert32(struct pte_desc *) __noprof;
void pte_insert64(struct pte_desc *) __noprof;
void pmap_fill_pte64(pmap_t, vaddr_t, paddr_t, struct pte_desc *, vm_prot_t,
    int) __noprof;
void pmap_fill_pte32(pmap_t, vaddr_t, paddr_t, struct pte_desc *, vm_prot_t,
    int) __noprof;

void pmap_syncicache_user_virt(pmap_t pm, vaddr_t va);

void pmap_remove_pted(pmap_t, struct pte_desc *);

/* setup/initialization functions */
void pmap_avail_setup(void);
void pmap_avail_fixup(void);
void pmap_remove_avail(paddr_t base, paddr_t end);
void *pmap_steal_avail(size_t size, int align);

/* asm interface */
int pte_spill_r(u_int32_t, u_int32_t, u_int32_t, int) __noprof;
int pte_spill_v(pmap_t, u_int32_t, u_int32_t, int) __noprof;

u_int32_t pmap_setusr(pmap_t pm, vaddr_t va);
void pmap_popusr(u_int32_t oldsr);

/* pte invalidation */
void pte_del(void *, vaddr_t);
void pte_zap(void *, struct pte_desc *);

/* XXX - panic on pool get failures? */
struct pool pmap_pmap_pool;
struct pool pmap_vp_pool;
struct pool pmap_pted_pool;

int pmap_initialized = 0;
int physmem;
int physmaxaddr;

#ifdef MULTIPROCESSOR
struct __ppc_lock pmap_hash_lock = PPC_LOCK_INITIALIZER;

#define	PMAP_HASH_LOCK(s)						\
do {									\
	s = ppc_intr_disable();						\
	__ppc_lock(&pmap_hash_lock);					\
} while (0)

#define	PMAP_HASH_UNLOCK(s)						\
do {									\
	__ppc_unlock(&pmap_hash_lock);					\
	ppc_intr_enable(s);						\
} while (0)

#define	PMAP_VP_LOCK_INIT(pm)		mtx_init(&pm->pm_mtx, IPL_VM)

#define	PMAP_VP_LOCK(pm)						\
do {									\
	if (pm != pmap_kernel())					\
		mtx_enter(&pm->pm_mtx);					\
} while (0)

#define	PMAP_VP_UNLOCK(pm)						\
do {									\
	if (pm != pmap_kernel())					\
		mtx_leave(&pm->pm_mtx);					\
} while (0)

#define PMAP_VP_ASSERT_LOCKED(pm)					\
do {									\
	if (pm != pmap_kernel())					\
		MUTEX_ASSERT_LOCKED(&pm->pm_mtx);			\
} while (0)

#else /* ! MULTIPROCESSOR */

#define	PMAP_HASH_LOCK(s)		(void)s
#define	PMAP_HASH_UNLOCK(s)		/* nothing */

#define	PMAP_VP_LOCK_INIT(pm)		/* nothing */
#define	PMAP_VP_LOCK(pm)		/* nothing */
#define	PMAP_VP_UNLOCK(pm)		/* nothing */
#define	PMAP_VP_ASSERT_LOCKED(pm)	/* nothing */
#endif /* MULTIPROCESSOR */

/* virtual to physical helpers */
static inline int
VP_SR(vaddr_t va)
{
	return (va >>VP_SR_POS) & VP_SR_MASK;
}

static inline int
VP_IDX1(vaddr_t va)
{
	return (va >> VP_IDX1_POS) & VP_IDX1_MASK;
}

static inline int
VP_IDX2(vaddr_t va)
{
	return (va >> VP_IDX2_POS) & VP_IDX2_MASK;
}

#if VP_IDX1_SIZE != VP_IDX2_SIZE 
#error pmap allocation code expects IDX1 and IDX2 size to be same
#endif
struct pmapvp {
	void *vp[VP_IDX1_SIZE];
};


/*
 * VP routines, virtual to physical translation information.
 * These data structures are based off of the pmap, per process.
 */

/*
 * This is used for pmap_kernel() mappings, they are not to be removed
 * from the vp table because they were statically initialized at the
 * initial pmap initialization. This is so that memory allocation 
 * is not necessary in the pmap_kernel() mappings.
 * Otherwise bad race conditions can appear.
 */
struct pte_desc *
pmap_vp_lookup(pmap_t pm, vaddr_t va)
{
	struct pmapvp *vp1;
	struct pmapvp *vp2;
	struct pte_desc *pted;

	PMAP_VP_ASSERT_LOCKED(pm);

	vp1 = pm->pm_vp[VP_SR(va)];
	if (vp1 == NULL) {
		return NULL;
	}

	vp2 = vp1->vp[VP_IDX1(va)];
	if (vp2 == NULL) {
		return NULL;
	}

	pted = vp2->vp[VP_IDX2(va)];

	return pted;
}

/*
 * Remove, and return, pted at specified address, NULL if not present
 */
struct pte_desc *
pmap_vp_remove(pmap_t pm, vaddr_t va)
{
	struct pmapvp *vp1;
	struct pmapvp *vp2;
	struct pte_desc *pted;

	PMAP_VP_ASSERT_LOCKED(pm);

	vp1 = pm->pm_vp[VP_SR(va)];
	if (vp1 == NULL) {
		return NULL;
	}

	vp2 = vp1->vp[VP_IDX1(va)];
	if (vp2 == NULL) {
		return NULL;
	}

	pted = vp2->vp[VP_IDX2(va)];
	vp2->vp[VP_IDX2(va)] = NULL;

	return pted;
}

/*
 * Create a V -> P mapping for the given pmap and virtual address
 * with reference to the pte descriptor that is used to map the page.
 * This code should track allocations of vp table allocations
 * so they can be freed efficiently.
 */
int
pmap_vp_enter(pmap_t pm, vaddr_t va, struct pte_desc *pted, int flags)
{
	struct pmapvp *vp1;
	struct pmapvp *vp2;

	PMAP_VP_ASSERT_LOCKED(pm);

	vp1 = pm->pm_vp[VP_SR(va)];
	if (vp1 == NULL) {
		vp1 = pool_get(&pmap_vp_pool, PR_NOWAIT | PR_ZERO);
		if (vp1 == NULL) {
			if ((flags & PMAP_CANFAIL) == 0)
				panic("pmap_vp_enter: failed to allocate vp1");
			return ENOMEM;
		}
		pm->pm_vp[VP_SR(va)] = vp1;
	}

	vp2 = vp1->vp[VP_IDX1(va)];
	if (vp2 == NULL) {
		vp2 = pool_get(&pmap_vp_pool, PR_NOWAIT | PR_ZERO);
		if (vp2 == NULL) {
			if ((flags & PMAP_CANFAIL) == 0)
				panic("pmap_vp_enter: failed to allocate vp2");
			return ENOMEM;
		}
		vp1->vp[VP_IDX1(va)] = vp2;
	}

	vp2->vp[VP_IDX2(va)] = pted;

	return 0;
}

static inline void
tlbie(vaddr_t va)
{
	asm volatile ("tlbie %0" :: "r"(va & ~PAGE_MASK));
}

static inline void
tlbsync(void)
{
	asm volatile ("tlbsync");
}
static inline void
eieio(void)
{
	asm volatile ("eieio");
}

static inline void
sync(void)
{
	asm volatile ("sync");
}

static inline void
tlbia(void)
{
	vaddr_t va;

	sync();
	for (va = 0; va < 0x00040000; va += 0x00001000)
		tlbie(va);
	eieio();
	tlbsync();
	sync();
}

static inline int
ptesr(sr_t *sr, vaddr_t va)
{
	return sr[(u_int)va >> ADDR_SR_SHIFT];
}

static inline int 
pteidx(sr_t sr, vaddr_t va)
{
	int hash;
	hash = (sr & SR_VSID) ^ (((u_int)va & ADDR_PIDX) >> ADDR_PIDX_SHIFT);
	return hash & pmap_ptab_mask;
}

#define PTED_VA_PTEGIDX_M	0x07
#define PTED_VA_HID_M		0x08
#define PTED_VA_MANAGED_M	0x10
#define PTED_VA_WIRED_M		0x20
#define PTED_VA_EXEC_M		0x40

static inline u_int32_t
PTED_HID(struct pte_desc *pted)
{
	return (pted->pted_va & PTED_VA_HID_M); 
}

static inline u_int32_t
PTED_PTEGIDX(struct pte_desc *pted)
{
	return (pted->pted_va & PTED_VA_PTEGIDX_M); 
}

static inline u_int32_t
PTED_MANAGED(struct pte_desc *pted)
{
	return (pted->pted_va & PTED_VA_MANAGED_M); 
}

static inline u_int32_t
PTED_VALID(struct pte_desc *pted)
{
	if (ppc_proc_is_64b)
		return (pted->p.pted_pte64.pte_hi & PTE_VALID_64);
	else 
		return (pted->p.pted_pte32.pte_hi & PTE_VALID_32);
}

/*
 * PV entries -
 * manipulate the physical to virtual translations for the entire system.
 * 
 * QUESTION: should all mapped memory be stored in PV tables? Or
 * is it alright to only store "ram" memory. Currently device mappings
 * are not stored.
 * It makes sense to pre-allocate mappings for all of "ram" memory, since
 * it is likely that it will be mapped at some point, but would it also
 * make sense to use a tree/table like is use for pmap to store device
 * mappings?
 * Further notes: It seems that the PV table is only used for pmap_protect
 * and other paging related operations. Given this, it is not necessary
 * to store any pmap_kernel() entries in PV tables and does not make
 * sense to store device mappings in PV either.
 *
 * Note: unlike other powerpc pmap designs, the array is only an array
 * of pointers. Since the same structure is used for holding information
 * in the VP table, the PV table, and for kernel mappings, the wired entries.
 * Allocate one data structure to hold all of the info, instead of replicating
 * it multiple times.
 *
 * One issue of making this a single data structure is that two pointers are
 * wasted for every page which does not map ram (device mappings), this 
 * should be a low percentage of mapped pages in the system, so should not
 * have too noticeable unnecessary ram consumption.
 */

void
pmap_enter_pv(struct pte_desc *pted, struct vm_page *pg)
{
	if (__predict_false(!pmap_initialized)) {
		return;
	}

	mtx_enter(&pg->mdpage.pv_mtx);
	LIST_INSERT_HEAD(&(pg->mdpage.pv_list), pted, pted_pv_list);
	pted->pted_va |= PTED_VA_MANAGED_M;
	mtx_leave(&pg->mdpage.pv_mtx);
}

void
pmap_remove_pv(struct pte_desc *pted)
{
	struct vm_page *pg;

	if (ppc_proc_is_64b)
		pg = PHYS_TO_VM_PAGE(pted->p.pted_pte64.pte_lo & PTE_RPGN_64);
	else
		pg = PHYS_TO_VM_PAGE(pted->p.pted_pte32.pte_lo & PTE_RPGN_32);

	mtx_enter(&pg->mdpage.pv_mtx);
	pted->pted_va &= ~PTED_VA_MANAGED_M;
	LIST_REMOVE(pted, pted_pv_list);
	mtx_leave(&pg->mdpage.pv_mtx);
}


/* PTE_CHG_32 == PTE_CHG_64 */
/* PTE_REF_32 == PTE_REF_64 */
static __inline u_int
pmap_pte2flags(u_int32_t pte)
{
	return (((pte & PTE_REF_32) ? PG_PMAP_REF : 0) |
	    ((pte & PTE_CHG_32) ? PG_PMAP_MOD : 0));
}

static __inline u_int
pmap_flags2pte(u_int32_t flags)
{
	return (((flags & PG_PMAP_REF) ? PTE_REF_32 : 0) |
	    ((flags & PG_PMAP_MOD) ? PTE_CHG_32 : 0));
}

void
pmap_attr_save(paddr_t pa, u_int32_t bits)
{
	struct vm_page *pg;

	pg = PHYS_TO_VM_PAGE(pa);
	if (pg == NULL)
		return;

	atomic_setbits_int(&pg->pg_flags,  pmap_pte2flags(bits));
}

int
pmap_enter(pmap_t pm, vaddr_t va, paddr_t pa, vm_prot_t prot, int flags)
{
	struct pte_desc *pted;
	struct vm_page *pg;
	boolean_t nocache = (pa & PMAP_NOCACHE) != 0;
	boolean_t wt = (pa & PMAP_WT) != 0;
	int need_sync = 0;
	int cache, error = 0;

	KASSERT(!(wt && nocache));
	pa &= PMAP_PA_MASK;

	PMAP_VP_LOCK(pm);
	pted = pmap_vp_lookup(pm, va);
	if (pted && PTED_VALID(pted)) {
		pmap_remove_pted(pm, pted);
		/* we lost our pted if it was user */
		if (pm != pmap_kernel())
			pted = pmap_vp_lookup(pm, va);
	}

	pm->pm_stats.resident_count++;

	/* Do not have pted for this, get one and put it in VP */
	if (pted == NULL) {
		pted = pool_get(&pmap_pted_pool, PR_NOWAIT | PR_ZERO);
		if (pted == NULL) {
			if ((flags & PMAP_CANFAIL) == 0) {
				error = ENOMEM;
				goto out;
			}
			panic("pmap_enter: failed to allocate pted");
		}
		error = pmap_vp_enter(pm, va, pted, flags);
		if (error) {
			pool_put(&pmap_pted_pool, pted);
			goto out;
		}
	}

	pg = PHYS_TO_VM_PAGE(pa);
	if (pg != NULL && (pg->pg_flags & PG_PMAP_UC))
		nocache = TRUE;
	if (wt)
		cache = PMAP_CACHE_WT;
	else if (pg != NULL && !(pg->pg_flags & PG_DEV) && !nocache)
		cache = PMAP_CACHE_WB;
	else
		cache = PMAP_CACHE_CI;

	/* Calculate PTE */
	if (ppc_proc_is_64b)
		pmap_fill_pte64(pm, va, pa, pted, prot, cache);
	else
		pmap_fill_pte32(pm, va, pa, pted, prot, cache);

	if (pg != NULL) {
		pmap_enter_pv(pted, pg); /* only managed mem */
	}

	/*
	 * Insert into HTAB
	 * We were told to map the page, probably called from vm_fault,
	 * so map the page!
	 */
	if (ppc_proc_is_64b)
		pte_insert64(pted);
	else
		pte_insert32(pted);

        if (prot & PROT_EXEC) {
		u_int sn = VP_SR(va);

        	pm->pm_exec[sn]++;
		if (pm->pm_sr[sn] & SR_NOEXEC)
			pm->pm_sr[sn] &= ~SR_NOEXEC;

		if (pg != NULL) {
			need_sync = ((pg->pg_flags & PG_PMAP_EXE) == 0);
			if (prot & PROT_WRITE)
				atomic_clearbits_int(&pg->pg_flags,
				    PG_PMAP_EXE);
			else
				atomic_setbits_int(&pg->pg_flags,
				    PG_PMAP_EXE);
		} else
			need_sync = 1;
	} else {
		/*
		 * Should we be paranoid about writeable non-exec 
		 * mappings ? if so, clear the exec tag
		 */
		if ((prot & PROT_WRITE) && (pg != NULL))
			atomic_clearbits_int(&pg->pg_flags, PG_PMAP_EXE);
	}

	/* only instruction sync executable pages */
	if (need_sync)
		pmap_syncicache_user_virt(pm, va);

out:
	PMAP_VP_UNLOCK(pm);
	return (error);
}

/*
 * Remove the given range of mapping entries.
 */
void
pmap_remove(pmap_t pm, vaddr_t sva, vaddr_t eva)
{
	struct pte_desc *pted;
	vaddr_t va;

	PMAP_VP_LOCK(pm);
	for (va = sva; va < eva; va += PAGE_SIZE) {
		pted = pmap_vp_lookup(pm, va);
		if (pted && PTED_VALID(pted))
			pmap_remove_pted(pm, pted);
	}
	PMAP_VP_UNLOCK(pm);
}

/*
 * remove a single mapping, notice that this code is O(1)
 */
void
pmap_remove_pted(pmap_t pm, struct pte_desc *pted)
{
	void *pte;
	int s;

	KASSERT(pm == pted->pted_pmap);
	PMAP_VP_ASSERT_LOCKED(pm);

	pm->pm_stats.resident_count--;

	PMAP_HASH_LOCK(s);
	if ((pte = pmap_ptedinhash(pted)) != NULL)
		pte_zap(pte, pted);
	PMAP_HASH_UNLOCK(s);

	if (pted->pted_va & PTED_VA_EXEC_M) {
		u_int sn = VP_SR(pted->pted_va);

		pted->pted_va &= ~PTED_VA_EXEC_M;
		pm->pm_exec[sn]--;
		if (pm->pm_exec[sn] == 0)
			pm->pm_sr[sn] |= SR_NOEXEC;
	}

	if (ppc_proc_is_64b)
		pted->p.pted_pte64.pte_hi &= ~PTE_VALID_64;
	else
		pted->p.pted_pte32.pte_hi &= ~PTE_VALID_32;

	if (PTED_MANAGED(pted))
		pmap_remove_pv(pted);

	if (pm != pmap_kernel()) {
		(void)pmap_vp_remove(pm, pted->pted_va);
		pool_put(&pmap_pted_pool, pted);
	}
}

/*
 * Enter a kernel mapping for the given page.
 * kernel mappings have a larger set of prerequisites than normal mappings.
 * 
 * 1. no memory should be allocated to create a kernel mapping.
 * 2. a vp mapping should already exist, even if invalid. (see 1)
 * 3. all vp tree mappings should already exist (see 1)
 * 
 */
void
pmap_kenter_pa(vaddr_t va, paddr_t pa, vm_prot_t prot)
{
	struct pte_desc *pted;
	struct vm_page *pg;
	boolean_t nocache = (pa & PMAP_NOCACHE) != 0;
	boolean_t wt = (pa & PMAP_WT) != 0;
	pmap_t pm;
	int cache;

	KASSERT(!(wt && nocache));
	pa &= PMAP_PA_MASK;

	pm = pmap_kernel();

	pted = pmap_vp_lookup(pm, va);
	if (pted && PTED_VALID(pted))
		pmap_remove_pted(pm, pted); /* pted is reused */

	pm->pm_stats.resident_count++;

	if (prot & PROT_WRITE) {
		pg = PHYS_TO_VM_PAGE(pa);
		if (pg != NULL)
			atomic_clearbits_int(&pg->pg_flags, PG_PMAP_EXE);
	}

	/* Do not have pted for this, get one and put it in VP */
	if (pted == NULL) {
		panic("pted not preallocated in pmap_kernel() va %lx pa %lx",
		    va, pa);
	}

	pg = PHYS_TO_VM_PAGE(pa);
	if (wt)
		cache = PMAP_CACHE_WT;
	else if (pg != NULL && !(pg->pg_flags & PG_DEV) && !nocache)
		cache = PMAP_CACHE_WB;
	else
		cache = PMAP_CACHE_CI;

	/* Calculate PTE */
	if (ppc_proc_is_64b)
		pmap_fill_pte64(pm, va, pa, pted, prot, cache);
	else
		pmap_fill_pte32(pm, va, pa, pted, prot, cache);

	/*
	 * Insert into HTAB
	 * We were told to map the page, probably called from vm_fault,
	 * so map the page!
	 */
	if (ppc_proc_is_64b)
		pte_insert64(pted);
	else
		pte_insert32(pted);

	pted->pted_va |= PTED_VA_WIRED_M;

        if (prot & PROT_EXEC) {
		u_int sn = VP_SR(va);

        	pm->pm_exec[sn]++;
		if (pm->pm_sr[sn] & SR_NOEXEC)
			pm->pm_sr[sn] &= ~SR_NOEXEC;
	}
}

/*
 * remove kernel (pmap_kernel()) mappings
 */
void
pmap_kremove(vaddr_t va, vsize_t len)
{
	struct pte_desc *pted;

	for (len >>= PAGE_SHIFT; len > 0; len--, va += PAGE_SIZE) {
		pted = pmap_vp_lookup(pmap_kernel(), va);
		if (pted && PTED_VALID(pted))
			pmap_remove_pted(pmap_kernel(), pted);
	}
}

static inline void *
pmap_ptedinhash(struct pte_desc *pted)
{
	vaddr_t va = pted->pted_va & ~PAGE_MASK;
	pmap_t pm = pted->pted_pmap;
	int sr, idx;

	sr = ptesr(pm->pm_sr, va);
	idx = pteidx(sr, va);

	if (ppc_proc_is_64b) {
		struct pte_64 *pte = pmap_ptable64;

		pte += (idx ^ (PTED_HID(pted) ? pmap_ptab_mask : 0)) * 8;
		pte += PTED_PTEGIDX(pted);

		/*
		 * We now have the pointer to where it will be, if it is
		 * currently mapped. If the mapping was thrown away in
		 * exchange for another page mapping, then this page is
		 * not currently in the HASH.
		 */
		if ((pted->p.pted_pte64.pte_hi |
		    (PTED_HID(pted) ? PTE_HID_64 : 0)) == pte->pte_hi)
			return (pte);
	} else {
		struct pte_32 *pte = pmap_ptable32;

		pte += (idx ^ (PTED_HID(pted) ? pmap_ptab_mask : 0)) * 8;
		pte += PTED_PTEGIDX(pted);

		/*
		 * We now have the pointer to where it will be, if it is
		 * currently mapped. If the mapping was thrown away in
		 * exchange for another page mapping, then this page is
		 * not currently in the HASH.
		 */
		if ((pted->p.pted_pte32.pte_hi |
		    (PTED_HID(pted) ? PTE_HID_32 : 0)) == pte->pte_hi)
			return (pte);
	}

	return (NULL);
}

/*
 * Delete a Page Table Entry, section 7.6.3.3.
 *
 * Note: pte must be locked.
 */
void
pte_del(void *pte, vaddr_t va)
{
	if (ppc_proc_is_64b)
		((struct pte_64 *)pte)->pte_hi &= ~PTE_VALID_64;
	else
		((struct pte_32 *)pte)->pte_hi &= ~PTE_VALID_32;

	sync();		/* Ensure update completed. */
	tlbie(va);	/* Invalidate old translation. */
	eieio();	/* Order tlbie before tlbsync. */
	tlbsync();	/* Ensure tlbie completed on all processors. */
	sync();		/* Ensure tlbsync and update completed. */
}

void
pte_zap(void *pte, struct pte_desc *pted)
{
	pte_del(pte, pted->pted_va);

	if (!PTED_MANAGED(pted))
		return;

	if (ppc_proc_is_64b) {
		pmap_attr_save(pted->p.pted_pte64.pte_lo & PTE_RPGN_64,
		    ((struct pte_64 *)pte)->pte_lo & (PTE_REF_64|PTE_CHG_64));
	} else {
		pmap_attr_save(pted->p.pted_pte32.pte_lo & PTE_RPGN_32,
		    ((struct pte_32 *)pte)->pte_lo & (PTE_REF_32|PTE_CHG_32));
	}
}

/*
 * What about execution control? Even at only a segment granularity.
 */
void
pmap_fill_pte64(pmap_t pm, vaddr_t va, paddr_t pa, struct pte_desc *pted,
	vm_prot_t prot, int cache)
{
	sr_t sr;
	struct pte_64 *pte64;

	sr = ptesr(pm->pm_sr, va);
	pte64 = &pted->p.pted_pte64;

	pte64->pte_hi = (((u_int64_t)sr & SR_VSID) <<
	   PTE_VSID_SHIFT_64) |
	    ((va >> ADDR_API_SHIFT_64) & PTE_API_64) | PTE_VALID_64;
	pte64->pte_lo = (pa & PTE_RPGN_64);


	if (cache == PMAP_CACHE_WB)
		pte64->pte_lo |= PTE_M_64;
	else if (cache == PMAP_CACHE_WT)
		pte64->pte_lo |= (PTE_W_64 | PTE_M_64);
	else
		pte64->pte_lo |= (PTE_M_64 | PTE_I_64 | PTE_G_64);

	if ((prot & (PROT_READ | PROT_WRITE)) == 0)
		pte64->pte_lo |= PTE_AC_64;

	if (prot & PROT_WRITE)
		pte64->pte_lo |= PTE_RW_64;
	else
		pte64->pte_lo |= PTE_RO_64;

	pted->pted_va = va & ~PAGE_MASK;

	if (prot & PROT_EXEC)
		pted->pted_va  |= PTED_VA_EXEC_M;
	else
		pte64->pte_lo |= PTE_N_64;

	pted->pted_pmap = pm;
}

/*
 * What about execution control? Even at only a segment granularity.
 */
void
pmap_fill_pte32(pmap_t pm, vaddr_t va, paddr_t pa, struct pte_desc *pted,
	vm_prot_t prot, int cache)
{
	sr_t sr;
	struct pte_32 *pte32;

	sr = ptesr(pm->pm_sr, va);
	pte32 = &pted->p.pted_pte32;

	pte32->pte_hi = ((sr & SR_VSID) << PTE_VSID_SHIFT_32) |
	    ((va >> ADDR_API_SHIFT_32) & PTE_API_32) | PTE_VALID_32;
	pte32->pte_lo = (pa & PTE_RPGN_32);

	if (cache == PMAP_CACHE_WB)
		pte32->pte_lo |= PTE_M_32;
	else if (cache == PMAP_CACHE_WT)
		pte32->pte_lo |= (PTE_W_32 | PTE_M_32);
	else
		pte32->pte_lo |= (PTE_M_32 | PTE_I_32 | PTE_G_32);

	if (prot & PROT_WRITE)
		pte32->pte_lo |= PTE_RW_32;
	else
		pte32->pte_lo |= PTE_RO_32;

	pted->pted_va = va & ~PAGE_MASK;

	/* XXX Per-page execution control. */
	if (prot & PROT_EXEC)
		pted->pted_va  |= PTED_VA_EXEC_M;

	pted->pted_pmap = pm;
}

int
pmap_test_attrs(struct vm_page *pg, u_int flagbit)
{
	u_int bits;
	struct pte_desc *pted;
	u_int ptebit = pmap_flags2pte(flagbit);
	int s;

	/* PTE_CHG_32 == PTE_CHG_64 */
	/* PTE_REF_32 == PTE_REF_64 */

	bits = pg->pg_flags & flagbit;
	if (bits == flagbit)
		return bits;

	mtx_enter(&pg->mdpage.pv_mtx);
	LIST_FOREACH(pted, &(pg->mdpage.pv_list), pted_pv_list) {
		void *pte;

		PMAP_HASH_LOCK(s);
		if ((pte = pmap_ptedinhash(pted)) != NULL) {
			if (ppc_proc_is_64b) {
				struct pte_64 *ptp64 = pte;
				bits |=	pmap_pte2flags(ptp64->pte_lo & ptebit);
			} else {
				struct pte_32 *ptp32 = pte;
				bits |=	pmap_pte2flags(ptp32->pte_lo & ptebit);
			}
		}
		PMAP_HASH_UNLOCK(s);

		if (bits == flagbit)
			break;
	}
	mtx_leave(&pg->mdpage.pv_mtx);

	atomic_setbits_int(&pg->pg_flags,  bits);

	return bits;
}

int
pmap_clear_attrs(struct vm_page *pg, u_int flagbit)
{
	u_int bits;
	struct pte_desc *pted;
	u_int ptebit = pmap_flags2pte(flagbit);
	int s;

	/* PTE_CHG_32 == PTE_CHG_64 */
	/* PTE_REF_32 == PTE_REF_64 */

	bits = pg->pg_flags & flagbit;

	mtx_enter(&pg->mdpage.pv_mtx);
	LIST_FOREACH(pted, &(pg->mdpage.pv_list), pted_pv_list) {
		void *pte;

		PMAP_HASH_LOCK(s);
		if ((pte = pmap_ptedinhash(pted)) != NULL) {
			if (ppc_proc_is_64b) {
				struct pte_64 *ptp64 = pte;

				bits |=	pmap_pte2flags(ptp64->pte_lo & ptebit);

				pte_del(ptp64, pted->pted_va);

				ptp64->pte_lo &= ~ptebit;
				eieio();
				ptp64->pte_hi |= PTE_VALID_64;
				sync();
			} else {
				struct pte_32 *ptp32 = pte;

				bits |=	pmap_pte2flags(ptp32->pte_lo & ptebit);

				pte_del(ptp32, pted->pted_va);

				ptp32->pte_lo &= ~ptebit;
				eieio();
				ptp32->pte_hi |= PTE_VALID_32;
				sync();
			}
		}
		PMAP_HASH_UNLOCK(s);
	}
	mtx_leave(&pg->mdpage.pv_mtx);

	/*
	 * this is done a second time, because while walking the list
	 * a bit could have been promoted via pmap_attr_save()
	 */
	bits |= pg->pg_flags & flagbit;
	atomic_clearbits_int(&pg->pg_flags,  flagbit);

	return bits;
}

/*
 * Fill the given physical page with zeros.
 */
void
pmap_zero_page(struct vm_page *pg)
{
	vaddr_t va = pmap_map_direct(pg);
	int i;

	/*
	 * Loop over & zero cache lines.  This code assumes that 64-bit
	 * CPUs have 128-byte cache lines.  We explicitly use ``dcbzl''
	 * here because we do not clear the DCBZ_SIZE bit of the HID5
	 * register in order to be compatible with code using ``dcbz''
	 * and assuming that cache line size is 32.
	 */
	if (ppc_proc_is_64b) {
		for (i = 0; i < PAGE_SIZE; i += 128)
			asm volatile ("dcbzl 0,%0" :: "r"(va + i));
		return;
	}

	for (i = 0; i < PAGE_SIZE; i += CACHELINESIZE)
		asm volatile ("dcbz 0,%0" :: "r"(va + i));
}

/*
 * Copy a page.
 */
void
pmap_copy_page(struct vm_page *srcpg, struct vm_page *dstpg)
{
	vaddr_t srcva = pmap_map_direct(srcpg);
	vaddr_t dstva = pmap_map_direct(dstpg);

	memcpy((void *)dstva, (void *)srcva, PAGE_SIZE);
}

int pmap_id_avail = 0;

pmap_t
pmap_create(void)
{
	u_int bits;
	int first, i, k, try, tblidx, tbloff;
	int seg;
	pmap_t pm;

	pm = pool_get(&pmap_pmap_pool, PR_WAITOK|PR_ZERO);

	pmap_reference(pm);
	PMAP_VP_LOCK_INIT(pm);

	/*
	 * Allocate segment registers for this pmap.
	 * Try not to reuse pmap ids, to spread the hash table usage.
	 */
	first = pmap_id_avail;
again:
	for (i = 0; i < NPMAPS; i++) {
		try = first + i;
		try = try % NPMAPS; /* truncate back into bounds */
		tblidx = try / (8 * sizeof usedsr[0]);
		tbloff = try % (8 * sizeof usedsr[0]);
		bits = usedsr[tblidx];
		if ((bits & (1U << tbloff)) == 0) {
			if (atomic_cas_uint(&usedsr[tblidx], bits,
			    bits | (1U << tbloff)) != bits) {
				first = try;
				goto again;
			}
			pmap_id_avail = try + 1;

			seg = try << 4;
			for (k = 0; k < 16; k++)
				pm->pm_sr[k] = (seg + k) | SR_NOEXEC;
			return (pm);
		}
	}
	panic("out of pmap slots");
}

/*
 * Add a reference to a given pmap.
 */
void
pmap_reference(pmap_t pm)
{
	atomic_inc_int(&pm->pm_refs);
}

/*
 * Retire the given pmap from service.
 * Should only be called if the map contains no valid mappings.
 */
void
pmap_destroy(pmap_t pm)
{
	int refs;

	refs = atomic_dec_int_nv(&pm->pm_refs);
	if (refs == -1)
		panic("re-entering pmap_destroy");
	if (refs > 0)
		return;

	/*
	 * reference count is zero, free pmap resources and free pmap.
	 */
	pmap_release(pm);
	pool_put(&pmap_pmap_pool, pm);
}

/*
 * Release any resources held by the given physical map.
 * Called when a pmap initialized by pmap_pinit is being released.
 */
void
pmap_release(pmap_t pm)
{
	int i, tblidx, tbloff;

	pmap_vp_destroy(pm);
	i = (pm->pm_sr[0] & SR_VSID) >> 4;
	tblidx = i / (8  * sizeof usedsr[0]);
	tbloff = i % (8  * sizeof usedsr[0]);

	/* powerpc can do atomic cas, clearbits on same word. */
	atomic_clearbits_int(&usedsr[tblidx], 1U << tbloff);
}

void
pmap_vp_destroy(pmap_t pm)
{
	int i, j;
	struct pmapvp *vp1;
	struct pmapvp *vp2;

	for (i = 0; i < VP_SR_SIZE; i++) {
		vp1 = pm->pm_vp[i];
		if (vp1 == NULL)
			continue;

		for (j = 0; j < VP_IDX1_SIZE; j++) {
			vp2 = vp1->vp[j];
			if (vp2 == NULL)
				continue;
			
			pool_put(&pmap_vp_pool, vp2);
		}
		pm->pm_vp[i] = NULL;
		pool_put(&pmap_vp_pool, vp1);
	}
}

void
pmap_avail_setup(void)
{
	struct mem_region *mp;

	ppc_mem_regions(&pmap_mem, &pmap_avail);

	for (mp = pmap_mem; mp->size !=0; mp++, ndumpmem++) {
		physmem += atop(mp->size);
		dumpmem[ndumpmem].start = atop(mp->start);
		dumpmem[ndumpmem].end = atop(mp->start + mp->size);
	}

	for (mp = pmap_avail; mp->size !=0 ; mp++) {
		if (physmaxaddr <  mp->start + mp->size)
			physmaxaddr = mp->start + mp->size;
	}

	for (mp = pmap_avail; mp->size !=0; mp++)
		pmap_cnt_avail += 1;
}

void
pmap_avail_fixup(void)
{
	struct mem_region *mp;
	u_int32_t align;
	u_int32_t end;

	mp = pmap_avail;
	while(mp->size !=0) {
		align = round_page(mp->start);
		if (mp->start != align) {
			pmap_remove_avail(mp->start, align);
			mp = pmap_avail;
			continue;
		}
		end = mp->start+mp->size;
		align = trunc_page(end);
		if (end != align) {
			pmap_remove_avail(align, end);
			mp = pmap_avail;
			continue;
		}
		mp++;
	}
}

/* remove a given region from avail memory */
void
pmap_remove_avail(paddr_t base, paddr_t end)
{
	struct mem_region *mp;
	int i;
	int mpend;

	/* remove given region from available */
	for (mp = pmap_avail; mp->size; mp++) {
		/*
		 * Check if this region holds all of the region
		 */
		mpend = mp->start + mp->size;
		if (base > mpend) {
			continue;
		}
		if (base <= mp->start) {
			if (end <= mp->start)
				break; /* region not present -??? */

			if (end >= mpend) {
				/* covers whole region */
				/* shorten */
				for (i = mp - pmap_avail;
				    i < pmap_cnt_avail;
				    i++) {
					pmap_avail[i] = pmap_avail[i+1];
				}
				pmap_cnt_avail--;
				pmap_avail[pmap_cnt_avail].size = 0;
			} else {
				mp->start = end;
				mp->size = mpend - end;
			}
		} else {
			/* start after the beginning */
			if (end >= mpend) {
				/* just truncate */
				mp->size = base - mp->start;
			} else {
				/* split */
				for (i = pmap_cnt_avail;
				    i > (mp - pmap_avail);
				    i--) {
					pmap_avail[i] = pmap_avail[i - 1];
				}
				pmap_cnt_avail++;
				mp->size = base - mp->start;
				mp++;
				mp->start = end;
				mp->size = mpend - end;
			}
		}
	}
	for (mp = pmap_allocated; mp->size != 0; mp++) {
		if (base < mp->start) {
			if (end == mp->start) {
				mp->start = base;
				mp->size += end - base;
				break;
			}
			/* lengthen */
			for (i = pmap_cnt_allocated; i > (mp - pmap_allocated);
			    i--) {
				pmap_allocated[i] = pmap_allocated[i - 1];
			}
			pmap_cnt_allocated++;
			mp->start = base;
			mp->size = end - base;
			return;
		}
		if (base == (mp->start + mp->size)) {
			mp->size += end - base;
			return;
		}
	}
	if (mp->size == 0) {
		mp->start = base;
		mp->size  = end - base;
		pmap_cnt_allocated++;
	}
}

void *
pmap_steal_avail(size_t size, int align)
{
	struct mem_region *mp;
	int start;
	int remsize;

	for (mp = pmap_avail; mp->size; mp++) {
		if (mp->size > size) {
			start = (mp->start + (align -1)) & ~(align -1);
			remsize = mp->size - (start - mp->start); 
			if (remsize >= 0) {
				pmap_remove_avail(start, start+size);
				return (void *)start;
			}
		}
	}
	panic ("unable to allocate region with size %zx align %x",
	    size, align);
}

/*
 * Similar to pmap_steal_avail, but operating on vm_physmem since
 * uvm_page_physload() has been called.
 */
vaddr_t
pmap_steal_memory(vsize_t size, vaddr_t *start, vaddr_t *end)
{
	int segno;
	u_int npg;
	vaddr_t va;
	paddr_t pa;
	struct vm_physseg *seg;

	size = round_page(size);
	npg = atop(size);

	for (segno = 0, seg = vm_physmem; segno < vm_nphysseg; segno++, seg++) {
		if (seg->avail_end - seg->avail_start < npg)
			continue;
		/*
		 * We can only steal at an ``unused'' segment boundary,
		 * i.e. either at the start or at the end.
		 */
		if (seg->avail_start == seg->start ||
		    seg->avail_end == seg->end)
			break;
	}
	if (segno == vm_nphysseg)
		va = 0;
	else {
		if (seg->avail_start == seg->start) {
			pa = ptoa(seg->avail_start);
			seg->avail_start += npg;
			seg->start += npg;
		} else {
			pa = ptoa(seg->avail_end) - size;
			seg->avail_end -= npg;
			seg->end -= npg;
		}
		/*
		 * If all the segment has been consumed now, remove it.
		 * Note that the crash dump code still knows about it
		 * and will dump it correctly.
		 */
		if (seg->start == seg->end) {
			if (vm_nphysseg-- == 1)
				panic("pmap_steal_memory: out of memory");
			while (segno < vm_nphysseg) {
				seg[0] = seg[1]; /* struct copy */
				seg++;
				segno++;
			}
		}

		va = (vaddr_t)pa;	/* 1:1 mapping */
		bzero((void *)va, size);
	}

	if (start != NULL)
		*start = VM_MIN_KERNEL_ADDRESS;
	if (end != NULL)
		*end = VM_MAX_KERNEL_ADDRESS;

	return (va);
}

void *msgbuf_addr;

/*
 * Initialize pmap setup.
 * ALL of the code which deals with avail needs rewritten as an actual
 * memory allocation.
 */ 
void
pmap_bootstrap(u_int kernelstart, u_int kernelend)
{
	struct mem_region *mp;
	int i, k;
	struct pmapvp *vp1;
	struct pmapvp *vp2;
	extern vaddr_t ppc_kvm_stolen;

	/*
	 * set the page size (default value is 4K which is ok)
	 */
	uvm_setpagesize();

	/*
	 * Get memory.
	 */
	pmap_avail_setup();

	/*
	 * Page align all regions.
	 * Non-page memory isn't very interesting to us.
	 * Also, sort the entries for ascending addresses.
	 */
	kernelstart = trunc_page(kernelstart);
	kernelend = round_page(kernelend);
	pmap_remove_avail(kernelstart, kernelend);

	msgbuf_addr = pmap_steal_avail(MSGBUFSIZE,4);

#ifdef DEBUG
	for (mp = pmap_avail; mp->size; mp++) {
		bzero((void *)mp->start, mp->size);
	}
#endif

#define HTABENTS_32 1024
#define HTABENTS_64 2048

	if (ppc_proc_is_64b) { 
		pmap_ptab_cnt = HTABENTS_64;
		while (pmap_ptab_cnt * 2 < physmem)
			pmap_ptab_cnt <<= 1;
	} else {
		pmap_ptab_cnt = HTABENTS_32;
		while (HTABSIZE_32 < (ptoa(physmem) >> 7))
			pmap_ptab_cnt <<= 1;
	}
	/*
	 * allocate suitably aligned memory for HTAB
	 */
	if (ppc_proc_is_64b) {
		pmap_ptable64 = pmap_steal_avail(HTABMEMSZ_64, HTABMEMSZ_64);
		bzero((void *)pmap_ptable64, HTABMEMSZ_64);
		pmap_ptab_mask = pmap_ptab_cnt - 1;
	} else {
		pmap_ptable32 = pmap_steal_avail(HTABSIZE_32, HTABSIZE_32);
		bzero((void *)pmap_ptable32, HTABSIZE_32);
		pmap_ptab_mask = pmap_ptab_cnt - 1;
	}

	/* allocate v->p mappings for pmap_kernel() */
	for (i = 0; i < VP_SR_SIZE; i++) {
		pmap_kernel()->pm_vp[i] = NULL;
	}
	vp1 = pmap_steal_avail(sizeof (struct pmapvp), 4);
	bzero (vp1, sizeof(struct pmapvp));
	pmap_kernel()->pm_vp[PPC_KERNEL_SR] = vp1;
	for (i = 0; i < VP_IDX1_SIZE; i++) {
		vp2 = vp1->vp[i] = pmap_steal_avail(sizeof (struct pmapvp), 4);
		bzero (vp2, sizeof(struct pmapvp));
		for (k = 0; k < VP_IDX2_SIZE; k++) {
			struct pte_desc *pted;
			pted = pmap_steal_avail(sizeof (struct pte_desc), 4);
			bzero (pted, sizeof (struct pte_desc));
			vp2->vp[k] = pted;
		}
	}

	/*
	 * Initialize kernel pmap and hardware.
	 */
#if NPMAPS >= PPC_KERNEL_SEGMENT / 16
	usedsr[PPC_KERNEL_SEGMENT / 16 / (sizeof usedsr[0] * 8)]
		|= 1 << ((PPC_KERNEL_SEGMENT / 16) % (sizeof usedsr[0] * 8));
#endif
	for (i = 0; i < 16; i++)
		pmap_kernel()->pm_sr[i] = (PPC_KERNEL_SEG0 + i) | SR_NOEXEC;

	if (ppc_nobat) {
		vp1 = pmap_steal_avail(sizeof (struct pmapvp), 4);
		bzero (vp1, sizeof(struct pmapvp));
		pmap_kernel()->pm_vp[0] = vp1;
		for (i = 0; i < VP_IDX1_SIZE; i++) {
			vp2 = vp1->vp[i] =
			    pmap_steal_avail(sizeof (struct pmapvp), 4);
			bzero (vp2, sizeof(struct pmapvp));
			for (k = 0; k < VP_IDX2_SIZE; k++) {
				struct pte_desc *pted;
				pted = pmap_steal_avail(sizeof (struct pte_desc), 4);
				bzero (pted, sizeof (struct pte_desc));
				vp2->vp[k] = pted;
			}
		}

		/* first segment contains executable pages */
		pmap_kernel()->pm_exec[0]++;
		pmap_kernel()->pm_sr[0] &= ~SR_NOEXEC;
	} else {
		/*
		 * Setup fixed BAT registers.
		 *
		 * Note that we still run in real mode, and the BAT
		 * registers were cleared in cpu_bootstrap().
		 */
		battable[0].batl = BATL(0x00000000, BAT_M);
		if (physmem > atop(0x08000000))
			battable[0].batu = BATU(0x00000000, BAT_BL_256M);
		else
			battable[0].batu = BATU(0x00000000, BAT_BL_128M);

		/* Map physical memory with BATs. */
		if (physmem > atop(0x10000000)) {
			battable[0x1].batl = BATL(0x10000000, BAT_M);
			battable[0x1].batu = BATU(0x10000000, BAT_BL_256M);
		}
		if (physmem > atop(0x20000000)) {
			battable[0x2].batl = BATL(0x20000000, BAT_M);
			battable[0x2].batu = BATU(0x20000000, BAT_BL_256M);
		}
		if (physmem > atop(0x30000000)) {
			battable[0x3].batl = BATL(0x30000000, BAT_M);
			battable[0x3].batu = BATU(0x30000000, BAT_BL_256M);
		}
		if (physmem > atop(0x40000000)) {
			battable[0x4].batl = BATL(0x40000000, BAT_M);
			battable[0x4].batu = BATU(0x40000000, BAT_BL_256M);
		}
		if (physmem > atop(0x50000000)) {
			battable[0x5].batl = BATL(0x50000000, BAT_M);
			battable[0x5].batu = BATU(0x50000000, BAT_BL_256M);
		}
		if (physmem > atop(0x60000000)) {
			battable[0x6].batl = BATL(0x60000000, BAT_M);
			battable[0x6].batu = BATU(0x60000000, BAT_BL_256M);
		}
		if (physmem > atop(0x70000000)) {
			battable[0x7].batl = BATL(0x70000000, BAT_M);
			battable[0x7].batu = BATU(0x70000000, BAT_BL_256M);
		}
	}

	ppc_kvm_stolen += reserve_dumppages( (caddr_t)(VM_MIN_KERNEL_ADDRESS +
	    ppc_kvm_stolen));

	pmap_avail_fixup();
	for (mp = pmap_avail; mp->size; mp++) {
		if (mp->start > 0x80000000)
			continue;
		if (mp->start + mp->size > 0x80000000)
			mp->size = 0x80000000 - mp->start;
		uvm_page_physload(atop(mp->start), atop(mp->start+mp->size),
		    atop(mp->start), atop(mp->start+mp->size), 0);
	}
}

void
pmap_enable_mmu(void)
{
	uint32_t scratch, sdr1;
	int i;

	/*
	 * For the PowerPC 970, ACCR = 3 inhibits loads and stores to
	 * pages with PTE_AC_64.  This is for execute-only mappings.
	 */
	if (ppc_proc_is_64b)
		asm volatile ("mtspr 29, %0" :: "r" (3));

	if (!ppc_nobat) {
		extern caddr_t etext;

		/* DBAT0 used for initial segment */
		ppc_mtdbat0l(battable[0].batl);
		ppc_mtdbat0u(battable[0].batu);

		/* IBAT0 only covering the kernel .text */
		ppc_mtibat0l(battable[0].batl);
		if (round_page((vaddr_t)&etext) < 8*1024*1024)
			ppc_mtibat0u(BATU(0x00000000, BAT_BL_8M));
		else
			ppc_mtibat0u(BATU(0x00000000, BAT_BL_16M));
	}

	for (i = 0; i < 16; i++)
		ppc_mtsrin(PPC_KERNEL_SEG0 + i, i << ADDR_SR_SHIFT);

	if (ppc_proc_is_64b)
		sdr1 = (uint32_t)pmap_ptable64 | HTABSIZE_64;
	else
		sdr1 = (uint32_t)pmap_ptable32 | (pmap_ptab_mask >> 10);

	asm volatile ("sync; mtsdr1 %0; isync" :: "r"(sdr1));
	tlbia();

	asm volatile ("eieio; mfmsr %0; ori %0,%0,%1; mtmsr %0; sync; isync"
	    : "=r"(scratch) : "K"(PSL_IR|PSL_DR|PSL_ME|PSL_RI));
}

/*
 * activate a pmap entry
 * All PTE entries exist in the same hash table.
 * Segment registers are filled on exit to user mode.
 */
void
pmap_activate(struct proc *p)
{
	struct pcb *pcb = &p->p_addr->u_pcb;

	/* Set the current pmap. */
	pcb->pcb_pm = p->p_vmspace->vm_map.pmap;
	pmap_extract(pmap_kernel(),
	    (vaddr_t)pcb->pcb_pm, (paddr_t *)&pcb->pcb_pmreal);
	curcpu()->ci_curpm = pcb->pcb_pmreal;
}

/*
 * deactivate a pmap entry
 * NOOP on powerpc
 */
void
pmap_deactivate(struct proc *p)
{
}

/*
 * pmap_extract: extract a PA for the given VA
 */

boolean_t
pmap_extract(pmap_t pm, vaddr_t va, paddr_t *pa)
{
	struct pte_desc *pted;

	if (pm == pmap_kernel() && va < physmaxaddr) {
		*pa = va;
		return TRUE;
	}

	PMAP_VP_LOCK(pm);
	pted = pmap_vp_lookup(pm, va);
	if (pted == NULL || !PTED_VALID(pted)) {
		PMAP_VP_UNLOCK(pm);
		return FALSE;
	}

	if (ppc_proc_is_64b)
		*pa = (pted->p.pted_pte64.pte_lo & PTE_RPGN_64) |
		    (va & ~PTE_RPGN_64);
	else
		*pa = (pted->p.pted_pte32.pte_lo & PTE_RPGN_32) |
		    (va & ~PTE_RPGN_32);

	PMAP_VP_UNLOCK(pm);
	return TRUE;
}

#ifdef ALTIVEC
/*
 * Read an instruction from a given virtual memory address.
 * Execute-only protection is bypassed.
 */
int
pmap_copyinsn(pmap_t pm, vaddr_t va, uint32_t *insn)
{
	struct pte_desc *pted;
	paddr_t pa;

	/* Assume pm != pmap_kernel(). */
	if (ppc_proc_is_64b) {
		/* inline pmap_extract */
		PMAP_VP_LOCK(pm);
		pted = pmap_vp_lookup(pm, va);
		if (pted == NULL || !PTED_VALID(pted)) {
			PMAP_VP_UNLOCK(pm);
			return EFAULT;
		}
		pa = (pted->p.pted_pte64.pte_lo & PTE_RPGN_64) |
		    (va & ~PTE_RPGN_64);
		PMAP_VP_UNLOCK(pm);

		if (pa > physmaxaddr - sizeof(*insn))
			return EFAULT;
		*insn = *(uint32_t *)pa;
		return 0;
	} else
		return copyin32((void *)va, insn);
}
#endif

u_int32_t
pmap_setusr(pmap_t pm, vaddr_t va)
{
	u_int32_t sr;
	u_int32_t oldsr;

	sr = ptesr(pm->pm_sr, va);

	/* user address range lock?? */
	asm volatile ("mfsr %0,%1" : "=r" (oldsr): "n"(PPC_USER_SR));
	asm volatile ("isync; mtsr %0,%1; isync" :: "n"(PPC_USER_SR), "r"(sr));
	return oldsr;
}

void
pmap_popusr(u_int32_t sr)
{
	asm volatile ("isync; mtsr %0,%1; isync"
	    :: "n"(PPC_USER_SR), "r"(sr));
}

int
_copyin(const void *udaddr, void *kaddr, size_t len)
{
	void *p;
	size_t l;
	u_int32_t oldsr;
	faultbuf env;
	void *oldh = curpcb->pcb_onfault;

	while (len > 0) {
		p = PPC_USER_ADDR + ((u_int)udaddr & ~PPC_SEGMENT_MASK);
		l = (PPC_USER_ADDR + PPC_SEGMENT_LENGTH) - p;
		if (l > len)
			l = len;
		oldsr = pmap_setusr(curpcb->pcb_pm, (vaddr_t)udaddr);
		if (setfault(&env)) {
			pmap_popusr(oldsr);
			curpcb->pcb_onfault = oldh;
			return EFAULT;
		}
		bcopy(p, kaddr, l);
		pmap_popusr(oldsr);
		udaddr += l;
		kaddr += l;
		len -= l;
	}
	curpcb->pcb_onfault = oldh;
	return 0;
}

int
copyout(const void *kaddr, void *udaddr, size_t len)
{
	void *p;
	size_t l;
	u_int32_t oldsr;
	faultbuf env;
	void *oldh = curpcb->pcb_onfault;

	while (len > 0) {
		p = PPC_USER_ADDR + ((u_int)udaddr & ~PPC_SEGMENT_MASK);
		l = (PPC_USER_ADDR + PPC_SEGMENT_LENGTH) - p;
		if (l > len)
			l = len;
		oldsr = pmap_setusr(curpcb->pcb_pm, (vaddr_t)udaddr);
		if (setfault(&env)) {
			pmap_popusr(oldsr);
			curpcb->pcb_onfault = oldh;
			return EFAULT;
		}

		bcopy(kaddr, p, l);
		pmap_popusr(oldsr);
		udaddr += l;
		kaddr += l;
		len -= l;
	}
	curpcb->pcb_onfault = oldh;
	return 0;
}

int
copyin32(const uint32_t *udaddr, uint32_t *kaddr)
{
	volatile uint32_t *p;
	u_int32_t oldsr;
	faultbuf env;
	void *oldh = curpcb->pcb_onfault;

	if ((u_int)udaddr & 0x3)
		return EFAULT;

	p = PPC_USER_ADDR + ((u_int)udaddr & ~PPC_SEGMENT_MASK);
	oldsr = pmap_setusr(curpcb->pcb_pm, (vaddr_t)udaddr);
	if (setfault(&env)) {
		pmap_popusr(oldsr);
		curpcb->pcb_onfault = oldh;
		return EFAULT;
	}
	*kaddr = *p;
	pmap_popusr(oldsr);
	curpcb->pcb_onfault = oldh;
	return 0;
}

int
_copyinstr(const void *udaddr, void *kaddr, size_t len, size_t *done)
{
	const u_char *uaddr = udaddr;
	u_char *kp    = kaddr;
	u_char *up;
	u_char c;
	void   *p;
	size_t	 l;
	u_int32_t oldsr;
	int cnt = 0;
	faultbuf env;
	void *oldh = curpcb->pcb_onfault;

	while (len > 0) {
		p = PPC_USER_ADDR + ((u_int)uaddr & ~PPC_SEGMENT_MASK);
		l = (PPC_USER_ADDR + PPC_SEGMENT_LENGTH) - p;
		up = p;
		if (l > len)
			l = len;
		len -= l;
		oldsr = pmap_setusr(curpcb->pcb_pm, (vaddr_t)uaddr);
		if (setfault(&env)) {
			if (done != NULL)
				*done =  cnt;

			curpcb->pcb_onfault = oldh;
			pmap_popusr(oldsr);
			return EFAULT;
		}
		while (l > 0) {
			c = *up;
			*kp = c;
			if (c == 0) {
				if (done != NULL)
					*done = cnt + 1;

				curpcb->pcb_onfault = oldh;
				pmap_popusr(oldsr);
				return 0;
			} 
			up++;
			kp++;
			l--;
			cnt++;
			uaddr++;
		}
		pmap_popusr(oldsr);
	}
	curpcb->pcb_onfault = oldh;
	if (done != NULL)
		*done = cnt;

	return ENAMETOOLONG;
}

int
copyoutstr(const void *kaddr, void *udaddr, size_t len, size_t *done)
{
	u_char *uaddr = (void *)udaddr;
	const u_char *kp    = kaddr;
	u_char *up;
	u_char c;
	void   *p;
	size_t	 l;
	u_int32_t oldsr;
	int cnt = 0;
	faultbuf env;
	void *oldh = curpcb->pcb_onfault;

	while (len > 0) {
		p = PPC_USER_ADDR + ((u_int)uaddr & ~PPC_SEGMENT_MASK);
		l = (PPC_USER_ADDR + PPC_SEGMENT_LENGTH) - p;
		up = p;
		if (l > len)
			l = len;
		len -= l;
		oldsr = pmap_setusr(curpcb->pcb_pm, (vaddr_t)uaddr);
		if (setfault(&env)) {
			if (done != NULL)
				*done =  cnt;

			curpcb->pcb_onfault = oldh;
			pmap_popusr(oldsr);
			return EFAULT;
		}
		while (l > 0) {
			c = *kp;
			*up = c;
			if (c == 0) {
				if (done != NULL)
					*done = cnt + 1;

				curpcb->pcb_onfault = oldh;
				pmap_popusr(oldsr);
				return 0;
			} 
			up++;
			kp++;
			l--;
			cnt++;
			uaddr++;
		}
		pmap_popusr(oldsr);
	}
	curpcb->pcb_onfault = oldh;
	if (done != NULL)
		*done = cnt;

	return ENAMETOOLONG;
}

/*
 * sync instruction cache for user virtual address.
 * The address WAS JUST MAPPED, so we have a VALID USERSPACE mapping
 */
void
pmap_syncicache_user_virt(pmap_t pm, vaddr_t va)
{
	vaddr_t start;
	int oldsr;

	if (pm != pmap_kernel()) {
		start = ((u_int)PPC_USER_ADDR + ((u_int)va &
		    ~PPC_SEGMENT_MASK));
		/* will only ever be page size, will not cross segments */

		/* USER SEGMENT LOCK - MPXXX */
		oldsr = pmap_setusr(pm, va);
	} else {
		start = va; /* flush mapped page */
	}

	syncicache((void *)start, PAGE_SIZE);

	if (pm != pmap_kernel()) {
		pmap_popusr(oldsr);
		/* USER SEGMENT UNLOCK -MPXXX */
	}
}

void
pmap_pted_ro(struct pte_desc *pted, vm_prot_t prot)
{
	if (ppc_proc_is_64b)
		pmap_pted_ro64(pted, prot);
	else
		pmap_pted_ro32(pted, prot);
}

void
pmap_pted_ro64(struct pte_desc *pted, vm_prot_t prot)
{
	pmap_t pm = pted->pted_pmap;
	vaddr_t va = pted->pted_va & ~PAGE_MASK;
	struct vm_page *pg;
	void *pte;
	int s;

	pg = PHYS_TO_VM_PAGE(pted->p.pted_pte64.pte_lo & PTE_RPGN_64);
	if (pg->pg_flags & PG_PMAP_EXE) {
		if ((prot & (PROT_WRITE | PROT_EXEC)) == PROT_WRITE) {
			atomic_clearbits_int(&pg->pg_flags, PG_PMAP_EXE);
		} else {
			pmap_syncicache_user_virt(pm, va);
		}
	}

	pted->p.pted_pte64.pte_lo &= ~PTE_PP_64;
	pted->p.pted_pte64.pte_lo |= PTE_RO_64;

	if ((prot & PROT_EXEC) == 0)
		pted->p.pted_pte64.pte_lo |= PTE_N_64;

	if ((prot & (PROT_READ | PROT_WRITE)) == 0)
		pted->p.pted_pte64.pte_lo |= PTE_AC_64;

	PMAP_HASH_LOCK(s);
	if ((pte = pmap_ptedinhash(pted)) != NULL) {
		struct pte_64 *ptp64 = pte;

		pte_del(ptp64, va);

		if (PTED_MANAGED(pted)) { /* XXX */
			pmap_attr_save(ptp64->pte_lo & PTE_RPGN_64,
			    ptp64->pte_lo & (PTE_REF_64|PTE_CHG_64));
		}

		/* Add a Page Table Entry, section 7.6.3.1. */
		ptp64->pte_lo = pted->p.pted_pte64.pte_lo;
		eieio();	/* Order 1st PTE update before 2nd. */
		ptp64->pte_hi |= PTE_VALID_64;
		sync();		/* Ensure updates completed. */
	}
	PMAP_HASH_UNLOCK(s);
}

void
pmap_pted_ro32(struct pte_desc *pted, vm_prot_t prot)
{
	pmap_t pm = pted->pted_pmap;
	vaddr_t va = pted->pted_va & ~PAGE_MASK;
	struct vm_page *pg;
	void *pte;
	int s;

	pg = PHYS_TO_VM_PAGE(pted->p.pted_pte32.pte_lo & PTE_RPGN_32);
	if (pg->pg_flags & PG_PMAP_EXE) {
		if ((prot & (PROT_WRITE | PROT_EXEC)) == PROT_WRITE) {
			atomic_clearbits_int(&pg->pg_flags, PG_PMAP_EXE);
		} else {
			pmap_syncicache_user_virt(pm, va);
		}
	}

	pted->p.pted_pte32.pte_lo &= ~PTE_PP_32;
	pted->p.pted_pte32.pte_lo |= PTE_RO_32;

	PMAP_HASH_LOCK(s);
	if ((pte = pmap_ptedinhash(pted)) != NULL) {
		struct pte_32 *ptp32 = pte;

		pte_del(ptp32, va);

		if (PTED_MANAGED(pted)) { /* XXX */
			pmap_attr_save(ptp32->pte_lo & PTE_RPGN_32,
			    ptp32->pte_lo & (PTE_REF_32|PTE_CHG_32));
		}

		/* Add a Page Table Entry, section 7.6.3.1. */
		ptp32->pte_lo &= ~(PTE_CHG_32|PTE_PP_32);
		ptp32->pte_lo |= PTE_RO_32;
		eieio();	/* Order 1st PTE update before 2nd. */
		ptp32->pte_hi |= PTE_VALID_32;
		sync();		/* Ensure updates completed. */
	}
	PMAP_HASH_UNLOCK(s);
}

/*
 * Lower the protection on the specified physical page.
 *
 * There are only two cases, either the protection is going to 0,
 * or it is going to read-only.
 */
void
pmap_page_protect(struct vm_page *pg, vm_prot_t prot)
{
	struct pte_desc *pted;
	void *pte;
	pmap_t pm;
	int s;

	if (prot == PROT_NONE) {
		mtx_enter(&pg->mdpage.pv_mtx);
		while ((pted = LIST_FIRST(&(pg->mdpage.pv_list))) != NULL) {
			pmap_reference(pted->pted_pmap);
			pm = pted->pted_pmap;
			mtx_leave(&pg->mdpage.pv_mtx);

			PMAP_VP_LOCK(pm);

			/*
			 * We dropped the pvlist lock before grabbing
			 * the pmap lock to avoid lock ordering
			 * problems.  This means we have to check the
			 * pvlist again since somebody else might have
			 * modified it.  All we care about is that the
			 * pvlist entry matches the pmap we just
			 * locked.  If it doesn't, unlock the pmap and
			 * try again.
			 */
			mtx_enter(&pg->mdpage.pv_mtx);
			if ((pted = LIST_FIRST(&(pg->mdpage.pv_list))) == NULL ||
			    pted->pted_pmap != pm) {
				mtx_leave(&pg->mdpage.pv_mtx);
				PMAP_VP_UNLOCK(pm);
				pmap_destroy(pm);
				mtx_enter(&pg->mdpage.pv_mtx);
				continue;
			}

			PMAP_HASH_LOCK(s);
			if ((pte = pmap_ptedinhash(pted)) != NULL)
				pte_zap(pte, pted);
			PMAP_HASH_UNLOCK(s);

			pted->pted_va &= ~PTED_VA_MANAGED_M;
			LIST_REMOVE(pted, pted_pv_list);
			mtx_leave(&pg->mdpage.pv_mtx);

			pmap_remove_pted(pm, pted);

			PMAP_VP_UNLOCK(pm);
			pmap_destroy(pm);
			mtx_enter(&pg->mdpage.pv_mtx);
		}
		mtx_leave(&pg->mdpage.pv_mtx);
		/* page is being reclaimed, sync icache next use */
		atomic_clearbits_int(&pg->pg_flags, PG_PMAP_EXE);
		return;
	}

	mtx_enter(&pg->mdpage.pv_mtx);
	LIST_FOREACH(pted, &(pg->mdpage.pv_list), pted_pv_list)
		pmap_pted_ro(pted, prot);
	mtx_leave(&pg->mdpage.pv_mtx);
}

void
pmap_protect(pmap_t pm, vaddr_t sva, vaddr_t eva, vm_prot_t prot)
{
	if (prot & (PROT_READ | PROT_EXEC)) {
		struct pte_desc *pted;

		PMAP_VP_LOCK(pm);
		while (sva < eva) {
			pted = pmap_vp_lookup(pm, sva);
			if (pted && PTED_VALID(pted))
				pmap_pted_ro(pted, prot);
			sva += PAGE_SIZE;
		}
		PMAP_VP_UNLOCK(pm);
		return;
	}
	pmap_remove(pm, sva, eva);
}

/*
 * Restrict given range to physical memory
 */
void
pmap_real_memory(paddr_t *start, vsize_t *size)
{
	struct mem_region *mp;

	for (mp = pmap_mem; mp->size; mp++) {
		if (((*start + *size) > mp->start)
			&& (*start < (mp->start + mp->size)))
		{
			if (*start < mp->start) {
				*size -= mp->start - *start;
				*start = mp->start;
			}
			if ((*start + *size) > (mp->start + mp->size))
				*size = mp->start + mp->size - *start;
			return;
		}
	}
	*size = 0;
}

void
pmap_init()
{
	pool_init(&pmap_pmap_pool, sizeof(struct pmap), 0, IPL_NONE, 0,
	    "pmap", NULL);
	pool_setlowat(&pmap_pmap_pool, 2);
	pool_init(&pmap_vp_pool, sizeof(struct pmapvp), 0, IPL_VM, 0,
	    "vp", &pool_allocator_single);
	pool_setlowat(&pmap_vp_pool, 10);
	pool_init(&pmap_pted_pool, sizeof(struct pte_desc), 0, IPL_VM, 0,
	    "pted", NULL);
	pool_setlowat(&pmap_pted_pool, 20);

	pmap_initialized = 1;
}

void
pmap_proc_iflush(struct process *pr, vaddr_t va, vsize_t len)
{
	paddr_t pa;
	vsize_t clen;

	while (len > 0) {
		/* add one to always round up to the next page */
		clen = round_page(va + 1) - va;
		if (clen > len)
			clen = len;

		if (pmap_extract(pr->ps_vmspace->vm_map.pmap, va, &pa)) {
			syncicache((void *)pa, clen);
		}

		len -= clen;
		va += clen;
	}
}

/* 
 * There are two routines, pte_spill_r and pte_spill_v
 * the _r version only handles kernel faults which are not user
 * accesses. The _v version handles all user faults and kernel copyin/copyout
 * "user" accesses.
 */
int
pte_spill_r(u_int32_t va, u_int32_t msr, u_int32_t dsisr, int exec_fault)
{
	pmap_t pm;
	struct pte_desc *pted;
	struct pte_desc pted_store;

	/* lookup is done physical to prevent faults */

	/* 
	 * This function only handles kernel faults, not supervisor copyins.
	 */
	if (msr & PSL_PR)
		return 0;

	/* if copyin, throw to full exception handler */
	if (VP_SR(va) == PPC_USER_SR)
		return 0;

	pm = pmap_kernel();

	/* 0 - physmaxaddr mapped 1-1 */
	if (va < physmaxaddr) {
		u_int32_t aligned_va;
		vm_prot_t prot = PROT_READ | PROT_WRITE;
		extern caddr_t kernel_text;
		extern caddr_t etext;

		pted = &pted_store;

		if (va >= trunc_page((vaddr_t)&kernel_text) &&
		    va < round_page((vaddr_t)&etext)) {
			prot |= PROT_EXEC;
		}

		aligned_va = trunc_page(va);
		if (ppc_proc_is_64b) {
			pmap_fill_pte64(pm, aligned_va, aligned_va,
			    pted, prot, PMAP_CACHE_WB);
			pte_insert64(pted);
		} else {
			pmap_fill_pte32(pm, aligned_va, aligned_va,
			    pted, prot, PMAP_CACHE_WB);
			pte_insert32(pted);
		}
		return 1;
	}

	return pte_spill_v(pm, va, dsisr, exec_fault);
}

int
pte_spill_v(pmap_t pm, u_int32_t va, u_int32_t dsisr, int exec_fault)
{
	struct pte_desc *pted;
	int inserted = 0;

	/*
	 * DSISR_DABR is set if the PowerPC 970 attempted to read or
	 * write an execute-only page.
	 */
	if (dsisr & DSISR_DABR)
		return 0;

	/*
	 * If the current mapping is RO and the access was a write
	 * we return 0
	 */
	PMAP_VP_LOCK(pm);
	pted = pmap_vp_lookup(pm, va);
	if (pted == NULL || !PTED_VALID(pted))
		goto out;

	/* Attempted to write a read-only page. */
	if (dsisr & DSISR_STORE) {
		if (ppc_proc_is_64b) {
			if ((pted->p.pted_pte64.pte_lo & PTE_PP_64) ==
			    PTE_RO_64)
				goto out;
		} else {
			if ((pted->p.pted_pte32.pte_lo & PTE_PP_32) ==
			    PTE_RO_32)
				goto out;
		}
	}

	/* Attempted to execute non-executable page. */
	if ((exec_fault != 0) && ((pted->pted_va & PTED_VA_EXEC_M) == 0))
		goto out;

	inserted = 1;
	if (ppc_proc_is_64b)
		pte_insert64(pted);
	else
		pte_insert32(pted);

out:
	PMAP_VP_UNLOCK(pm);
	return (inserted);
}


/*
 * should pte_insert code avoid wired mappings?
 * is the stack safe?
 * is the pted safe? (physical)
 * -ugh
 */
void
pte_insert64(struct pte_desc *pted)
{
	struct pte_64 *ptp64;
	int off, secondary;
	int sr, idx, i;
	void *pte;
	int s;

	PMAP_HASH_LOCK(s);
	if ((pte = pmap_ptedinhash(pted)) != NULL)
		pte_zap(pte, pted);

	pted->pted_va &= ~(PTED_VA_HID_M|PTED_VA_PTEGIDX_M);

	sr = ptesr(pted->pted_pmap->pm_sr, pted->pted_va);
	idx = pteidx(sr, pted->pted_va);

	/*
	 * instead of starting at the beginning of each pteg,
	 * the code should pick a random location with in the primary
	 * then search all of the entries, then if not yet found,
	 * do the same for the secondary.
	 * this would reduce the frontloading of the pteg.
	 */

	/* first just try fill of primary hash */
	ptp64 = pmap_ptable64 + (idx) * 8;
	for (i = 0; i < 8; i++) {
		if (ptp64[i].pte_hi & PTE_VALID_64)
			continue;

		pted->pted_va |= i;

		/* Add a Page Table Entry, section 7.6.3.1. */
		ptp64[i].pte_hi = pted->p.pted_pte64.pte_hi & ~PTE_VALID_64;
		ptp64[i].pte_lo = pted->p.pted_pte64.pte_lo;
		eieio();	/* Order 1st PTE update before 2nd. */
		ptp64[i].pte_hi |= PTE_VALID_64;
		sync();		/* Ensure updates completed. */

		goto out;
	}

	/* try fill of secondary hash */
	ptp64 = pmap_ptable64 + (idx ^ pmap_ptab_mask) * 8;
	for (i = 0; i < 8; i++) {
		if (ptp64[i].pte_hi & PTE_VALID_64)
			continue;

		pted->pted_va |= (i | PTED_VA_HID_M);

		/* Add a Page Table Entry, section 7.6.3.1. */
		ptp64[i].pte_hi = pted->p.pted_pte64.pte_hi & ~PTE_VALID_64;
		ptp64[i].pte_lo = pted->p.pted_pte64.pte_lo;
		eieio();	/* Order 1st PTE update before 2nd. */
		ptp64[i].pte_hi |= (PTE_HID_64|PTE_VALID_64);
		sync();		/* Ensure updates completed. */

		goto out;
	}

	/* need decent replacement algorithm */
	off = ppc_mftb();
	secondary = off & 8;


	pted->pted_va |= off & (PTED_VA_PTEGIDX_M|PTED_VA_HID_M);

	idx = (idx ^ (PTED_HID(pted) ? pmap_ptab_mask : 0));

	ptp64 = pmap_ptable64 + (idx * 8);
	ptp64 += PTED_PTEGIDX(pted); /* increment by index into pteg */

	if (ptp64->pte_hi & PTE_VALID_64) {
		vaddr_t va;

		/* Bits 9-19 */
		idx = (idx ^ ((ptp64->pte_hi & PTE_HID_64) ?
		    pmap_ptab_mask : 0));
		va = (ptp64->pte_hi >> PTE_VSID_SHIFT_64) ^ idx;
		va <<= ADDR_PIDX_SHIFT;
		/* Bits 4-8 */
		va |= (ptp64->pte_hi & PTE_API_64) << ADDR_API_SHIFT_32;
		/* Bits 0-3 */
		va |= (ptp64->pte_hi >> PTE_VSID_SHIFT_64)
		    << ADDR_SR_SHIFT;

		pte_del(ptp64, va);

		pmap_attr_save(ptp64->pte_lo & PTE_RPGN_64,
		    ptp64->pte_lo & (PTE_REF_64|PTE_CHG_64));
	}

	/* Add a Page Table Entry, section 7.6.3.1. */
	ptp64->pte_hi = pted->p.pted_pte64.pte_hi & ~PTE_VALID_64;
	if (secondary)
		ptp64->pte_hi |= PTE_HID_64;
	ptp64->pte_lo = pted->p.pted_pte64.pte_lo;
	eieio();	/* Order 1st PTE update before 2nd. */
	ptp64->pte_hi |= PTE_VALID_64;
	sync();		/* Ensure updates completed. */

out:
	PMAP_HASH_UNLOCK(s);
}

void
pte_insert32(struct pte_desc *pted)
{
	struct pte_32 *ptp32;
	int off, secondary;
	int sr, idx, i;
	void *pte;
	int s;

	PMAP_HASH_LOCK(s);
	if ((pte = pmap_ptedinhash(pted)) != NULL)
		pte_zap(pte, pted);

	pted->pted_va &= ~(PTED_VA_HID_M|PTED_VA_PTEGIDX_M);

	sr = ptesr(pted->pted_pmap->pm_sr, pted->pted_va);
	idx = pteidx(sr, pted->pted_va);

	/*
	 * instead of starting at the beginning of each pteg,
	 * the code should pick a random location with in the primary
	 * then search all of the entries, then if not yet found,
	 * do the same for the secondary.
	 * this would reduce the frontloading of the pteg.
	 */

	/* first just try fill of primary hash */
	ptp32 = pmap_ptable32 + (idx) * 8;
	for (i = 0; i < 8; i++) {
		if (ptp32[i].pte_hi & PTE_VALID_32)
			continue;

		pted->pted_va |= i;

		/* Add a Page Table Entry, section 7.6.3.1. */
		ptp32[i].pte_hi = pted->p.pted_pte32.pte_hi & ~PTE_VALID_32;
		ptp32[i].pte_lo = pted->p.pted_pte32.pte_lo;
		eieio();	/* Order 1st PTE update before 2nd. */
		ptp32[i].pte_hi |= PTE_VALID_32;
		sync();		/* Ensure updates completed. */

		goto out;
	}

	/* try fill of secondary hash */
	ptp32 = pmap_ptable32 + (idx ^ pmap_ptab_mask) * 8;
	for (i = 0; i < 8; i++) {
		if (ptp32[i].pte_hi & PTE_VALID_32)
			continue;

		pted->pted_va |= (i | PTED_VA_HID_M);

		/* Add a Page Table Entry, section 7.6.3.1. */
		ptp32[i].pte_hi = pted->p.pted_pte32.pte_hi & ~PTE_VALID_32;
		ptp32[i].pte_lo = pted->p.pted_pte32.pte_lo;
		eieio();	/* Order 1st PTE update before 2nd. */
		ptp32[i].pte_hi |= (PTE_HID_32|PTE_VALID_32);
		sync();		/* Ensure updates completed. */

		goto out;
	}

	/* need decent replacement algorithm */
	off = ppc_mftb();
	secondary = off & 8;

	pted->pted_va |= off & (PTED_VA_PTEGIDX_M|PTED_VA_HID_M);

	idx = (idx ^ (PTED_HID(pted) ? pmap_ptab_mask : 0));

	ptp32 = pmap_ptable32 + (idx * 8);
	ptp32 += PTED_PTEGIDX(pted); /* increment by index into pteg */

	if (ptp32->pte_hi & PTE_VALID_32) {
		vaddr_t va;

		va = ((ptp32->pte_hi & PTE_API_32) << ADDR_API_SHIFT_32) |
		     ((((ptp32->pte_hi >> PTE_VSID_SHIFT_32) & SR_VSID)
			^(idx ^ ((ptp32->pte_hi & PTE_HID_32) ? 0x3ff : 0)))
			    & 0x3ff) << PAGE_SHIFT;

		pte_del(ptp32, va);

		pmap_attr_save(ptp32->pte_lo & PTE_RPGN_32,
		    ptp32->pte_lo & (PTE_REF_32|PTE_CHG_32));
	}

	/* Add a Page Table Entry, section 7.6.3.1. */
	ptp32->pte_hi = pted->p.pted_pte32.pte_hi & ~PTE_VALID_32;
	if (secondary)
		ptp32->pte_hi |= PTE_HID_32;
	ptp32->pte_lo = pted->p.pted_pte32.pte_lo;
	eieio();	/* Order 1st PTE update before 2nd. */
	ptp32->pte_hi |= PTE_VALID_32;
	sync();		/* Ensure updates completed. */

out:
	PMAP_HASH_UNLOCK(s);
}
