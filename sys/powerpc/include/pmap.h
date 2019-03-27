/*-
 * SPDX-License-Identifier: BSD-3-Clause AND BSD-4-Clause
 *
 * Copyright (C) 2006 Semihalf, Marian Balakowicz <m8@semihalf.com>
 * All rights reserved.
 *
 * Adapted for Freescale's e500 core CPUs.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
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
 *	from: $NetBSD: pmap.h,v 1.17 2000/03/30 16:18:24 jdolecek Exp $
 */

#ifndef	_MACHINE_PMAP_H_
#define	_MACHINE_PMAP_H_

#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/_cpuset.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <machine/sr.h>
#include <machine/pte.h>
#include <machine/slb.h>
#include <machine/tlb.h>

struct pmap;
typedef struct pmap *pmap_t;

#if defined(AIM)

#if !defined(NPMAPS)
#define	NPMAPS		32768
#endif /* !defined(NPMAPS) */

struct	slbtnode;

struct pvo_entry {
	LIST_ENTRY(pvo_entry) pvo_vlink;	/* Link to common virt page */
#ifndef __powerpc64__
	LIST_ENTRY(pvo_entry) pvo_olink;	/* Link to overflow entry */
#endif
	RB_ENTRY(pvo_entry) pvo_plink;	/* Link to pmap entries */
	struct {
#ifndef __powerpc64__
		/* 32-bit fields */
		struct	pte pte;
#endif
		/* 64-bit fields */
		uintptr_t   slot;
		vm_paddr_t  pa;
		vm_prot_t   prot;
	} pvo_pte;
	pmap_t		pvo_pmap;		/* Owning pmap */
	vm_offset_t	pvo_vaddr;		/* VA of entry */
	uint64_t	pvo_vpn;		/* Virtual page number */
};
LIST_HEAD(pvo_head, pvo_entry);
RB_HEAD(pvo_tree, pvo_entry);
int pvo_vaddr_compare(struct pvo_entry *, struct pvo_entry *);
RB_PROTOTYPE(pvo_tree, pvo_entry, pvo_plink, pvo_vaddr_compare);

/* Used by 32-bit PMAP */
#define	PVO_PTEGIDX_MASK	0x007UL		/* which PTEG slot */
#define	PVO_PTEGIDX_VALID	0x008UL		/* slot is valid */
/* Used by 64-bit PMAP */
#define	PVO_HID			0x008UL		/* PVO entry in alternate hash*/
/* Used by both */
#define	PVO_WIRED		0x010UL		/* PVO entry is wired */
#define	PVO_MANAGED		0x020UL		/* PVO entry is managed */
#define	PVO_BOOTSTRAP		0x080UL		/* PVO entry allocated during
						   bootstrap */
#define PVO_DEAD		0x100UL		/* waiting to be deleted */
#define PVO_LARGE		0x200UL		/* large page */
#define	PVO_VADDR(pvo)		((pvo)->pvo_vaddr & ~ADDR_POFF)
#define	PVO_PTEGIDX_GET(pvo)	((pvo)->pvo_vaddr & PVO_PTEGIDX_MASK)
#define	PVO_PTEGIDX_ISSET(pvo)	((pvo)->pvo_vaddr & PVO_PTEGIDX_VALID)
#define	PVO_PTEGIDX_CLR(pvo)	\
	((void)((pvo)->pvo_vaddr &= ~(PVO_PTEGIDX_VALID|PVO_PTEGIDX_MASK)))
#define	PVO_PTEGIDX_SET(pvo, i)	\
	((void)((pvo)->pvo_vaddr |= (i)|PVO_PTEGIDX_VALID))
#define	PVO_VSID(pvo)		((pvo)->pvo_vpn >> 16)

struct	pmap {
	struct		pmap_statistics	pm_stats;
	struct	mtx	pm_mtx;
	
    #ifdef __powerpc64__
	struct slbtnode	*pm_slb_tree_root;
	struct slb	**pm_slb;
	int		pm_slb_len;
    #else
	register_t	pm_sr[16];
    #endif
	cpuset_t	pm_active;

	struct pmap	*pmap_phys;
	struct pvo_tree pmap_pvo;
};

struct	md_page {
	volatile int32_t mdpg_attrs;
	vm_memattr_t	 mdpg_cache_attrs;
	struct	pvo_head mdpg_pvoh;
};

#define	pmap_page_get_memattr(m)	((m)->md.mdpg_cache_attrs)
#define	pmap_page_is_mapped(m)	(!LIST_EMPTY(&(m)->md.mdpg_pvoh))

/*
 * Return the VSID corresponding to a given virtual address.
 * If no VSID is currently defined, it will allocate one, and add
 * it to a free slot if available.
 *
 * NB: The PMAP MUST be locked already.
 */
uint64_t va_to_vsid(pmap_t pm, vm_offset_t va);

/* Lock-free, non-allocating lookup routines */
uint64_t kernel_va_to_slbv(vm_offset_t va);
struct slb *user_va_to_slb_entry(pmap_t pm, vm_offset_t va);

uint64_t allocate_user_vsid(pmap_t pm, uint64_t esid, int large);
void	free_vsid(pmap_t pm, uint64_t esid, int large);
void	slb_insert_user(pmap_t pm, struct slb *slb);
void	slb_insert_kernel(uint64_t slbe, uint64_t slbv);

struct slbtnode *slb_alloc_tree(void);
void     slb_free_tree(pmap_t pm);
struct slb **slb_alloc_user_cache(void);
void	slb_free_user_cache(struct slb **);

#elif defined(BOOKE)

struct pmap {
	struct pmap_statistics	pm_stats;	/* pmap statistics */
	struct mtx		pm_mtx;		/* pmap mutex */
	tlbtid_t		pm_tid[MAXCPU];	/* TID to identify this pmap entries in TLB */
	cpuset_t		pm_active;	/* active on cpus */

#ifdef __powerpc64__
	/* Page table directory, array of pointers to page directories. */
	pte_t **pm_pp2d[PP2D_NENTRIES];

	/* List of allocated pdir bufs (pdir kva regions). */
	TAILQ_HEAD(, ptbl_buf)	pm_pdir_list;
#else
	/* Page table directory, array of pointers to page tables. */
	pte_t			*pm_pdir[PDIR_NENTRIES];
#endif

	/* List of allocated ptbl bufs (ptbl kva regions). */
	TAILQ_HEAD(, ptbl_buf)	pm_ptbl_list;
};

struct pv_entry {
	pmap_t pv_pmap;
	vm_offset_t pv_va;
	TAILQ_ENTRY(pv_entry) pv_link;
};
typedef struct pv_entry *pv_entry_t;

struct md_page {
	TAILQ_HEAD(, pv_entry) pv_list;
	int	pv_tracked;
};

#define	pmap_page_get_memattr(m)	VM_MEMATTR_DEFAULT
#define	pmap_page_is_mapped(m)	(!TAILQ_EMPTY(&(m)->md.pv_list))

#else
/*
 * Common pmap members between AIM and BOOKE.
 * libkvm needs pm_stats at the same location between both, as it doesn't define
 * AIM nor BOOKE, and is expected to work across all.
 */
struct pmap {
	struct pmap_statistics	pm_stats;	/* pmap statistics */
	struct mtx		pm_mtx;		/* pmap mutex */
};
#endif /* AIM */

extern	struct pmap kernel_pmap_store;
#define	kernel_pmap	(&kernel_pmap_store)

#ifdef _KERNEL

#define	PMAP_LOCK(pmap)		mtx_lock(&(pmap)->pm_mtx)
#define	PMAP_LOCK_ASSERT(pmap, type) \
				mtx_assert(&(pmap)->pm_mtx, (type))
#define	PMAP_LOCK_DESTROY(pmap)	mtx_destroy(&(pmap)->pm_mtx)
#define	PMAP_LOCK_INIT(pmap)	mtx_init(&(pmap)->pm_mtx, \
				    (pmap == kernel_pmap) ? "kernelpmap" : \
				    "pmap", NULL, MTX_DEF)
#define	PMAP_LOCKED(pmap)	mtx_owned(&(pmap)->pm_mtx)
#define	PMAP_MTX(pmap)		(&(pmap)->pm_mtx)
#define	PMAP_TRYLOCK(pmap)	mtx_trylock(&(pmap)->pm_mtx)
#define	PMAP_UNLOCK(pmap)	mtx_unlock(&(pmap)->pm_mtx)

#define	pmap_page_is_write_mapped(m)	(((m)->aflags & PGA_WRITEABLE) != 0)

void		pmap_bootstrap(vm_offset_t, vm_offset_t);
void		pmap_kenter(vm_offset_t va, vm_paddr_t pa);
void		pmap_kenter_attr(vm_offset_t va, vm_paddr_t pa, vm_memattr_t);
void		pmap_kremove(vm_offset_t);
void		*pmap_mapdev(vm_paddr_t, vm_size_t);
void		*pmap_mapdev_attr(vm_paddr_t, vm_size_t, vm_memattr_t);
void		pmap_unmapdev(vm_offset_t, vm_size_t);
void		pmap_page_set_memattr(vm_page_t, vm_memattr_t);
int		pmap_change_attr(vm_offset_t, vm_size_t, vm_memattr_t);
int		pmap_map_user_ptr(pmap_t pm, volatile const void *uaddr,
		    void **kaddr, size_t ulen, size_t *klen);
int		pmap_decode_kernel_ptr(vm_offset_t addr, int *is_user,
		    vm_offset_t *decoded_addr);
void		pmap_deactivate(struct thread *);
vm_paddr_t	pmap_kextract(vm_offset_t);
int		pmap_dev_direct_mapped(vm_paddr_t, vm_size_t);
boolean_t	pmap_mmu_install(char *name, int prio);

#define	vtophys(va)	pmap_kextract((vm_offset_t)(va))

#define PHYS_AVAIL_SZ	256	/* Allows up to 16GB Ram on pSeries with
				 * logical memory block size of 64MB.
				 * For more Ram increase the lmb or this value.
				 */

extern	vm_paddr_t phys_avail[PHYS_AVAIL_SZ];
extern	vm_offset_t virtual_avail;
extern	vm_offset_t virtual_end;

extern	vm_offset_t msgbuf_phys;

extern	int pmap_bootstrapped;

vm_offset_t pmap_early_io_map(vm_paddr_t pa, vm_size_t size);
void pmap_early_io_unmap(vm_offset_t va, vm_size_t size);
void pmap_track_page(pmap_t pmap, vm_offset_t va);

static inline int
pmap_vmspace_copy(pmap_t dst_pmap __unused, pmap_t src_pmap __unused)
{

	return (0);
}

#endif

#endif /* !_MACHINE_PMAP_H_ */
