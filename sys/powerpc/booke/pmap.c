/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2007-2009 Semihalf, Rafal Jaworowski <raj@semihalf.com>
 * Copyright (C) 2006 Semihalf, Marian Balakowicz <m8@semihalf.com>
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
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Some hw specific parts of this pmap were derived or influenced
 * by NetBSD's ibm4xx pmap module. More generic code is shared with
 * a few other pmap modules from the FreeBSD tree.
 */

 /*
  * VM layout notes:
  *
  * Kernel and user threads run within one common virtual address space
  * defined by AS=0.
  *
  * 32-bit pmap:
  * Virtual address space layout:
  * -----------------------------
  * 0x0000_0000 - 0x7fff_ffff	: user process
  * 0x8000_0000 - 0xbfff_ffff	: pmap_mapdev()-ed area (PCI/PCIE etc.)
  * 0xc000_0000 - 0xc0ff_ffff	: kernel reserved
  *   0xc000_0000 - data_end	: kernel code+data, env, metadata etc.
  * 0xc100_0000 - 0xffff_ffff	: KVA
  *   0xc100_0000 - 0xc100_3fff : reserved for page zero/copy
  *   0xc100_4000 - 0xc200_3fff : reserved for ptbl bufs
  *   0xc200_4000 - 0xc200_8fff : guard page + kstack0
  *   0xc200_9000 - 0xfeef_ffff	: actual free KVA space
  *
  * 64-bit pmap:
  * Virtual address space layout:
  * -----------------------------
  * 0x0000_0000_0000_0000 - 0xbfff_ffff_ffff_ffff      : user process
  *   0x0000_0000_0000_0000 - 0x8fff_ffff_ffff_ffff    : text, data, heap, maps, libraries
  *   0x9000_0000_0000_0000 - 0xafff_ffff_ffff_ffff    : mmio region
  *   0xb000_0000_0000_0000 - 0xbfff_ffff_ffff_ffff    : stack
  * 0xc000_0000_0000_0000 - 0xcfff_ffff_ffff_ffff      : kernel reserved
  *   0xc000_0000_0000_0000 - endkernel-1              : kernel code & data
  *               endkernel - msgbufp-1                : flat device tree
  *                 msgbufp - ptbl_bufs-1              : message buffer
  *               ptbl_bufs - kernel_pdir-1            : kernel page tables
  *             kernel_pdir - kernel_pp2d-1            : kernel page directory
  *             kernel_pp2d - .                        : kernel pointers to page directory
  *      pmap_zero_copy_min - crashdumpmap-1           : reserved for page zero/copy
  *            crashdumpmap - ptbl_buf_pool_vabase-1   : reserved for ptbl bufs
  *    ptbl_buf_pool_vabase - virtual_avail-1          : user page directories and page tables
  *           virtual_avail - 0xcfff_ffff_ffff_ffff    : actual free KVA space
  * 0xd000_0000_0000_0000 - 0xdfff_ffff_ffff_ffff      : coprocessor region
  * 0xe000_0000_0000_0000 - 0xefff_ffff_ffff_ffff      : mmio region
  * 0xf000_0000_0000_0000 - 0xffff_ffff_ffff_ffff      : direct map
  *   0xf000_0000_0000_0000 - +Maxmem                  : physmem map
  *                         - 0xffff_ffff_ffff_ffff    : device direct map
  */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_kstack_pages.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/ktr.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kerneldump.h>
#include <sys/linker.h>
#include <sys/msgbuf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_kern.h>
#include <vm/vm_pageout.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vm_param.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/vm_phys.h>
#include <vm/vm_pagequeue.h>
#include <vm/uma.h>

#include <machine/_inttypes.h>
#include <machine/cpu.h>
#include <machine/pcb.h>
#include <machine/platform.h>

#include <machine/tlb.h>
#include <machine/spr.h>
#include <machine/md_var.h>
#include <machine/mmuvar.h>
#include <machine/pmap.h>
#include <machine/pte.h>

#include <ddb/ddb.h>

#include "mmu_if.h"

#define	SPARSE_MAPDEV
#ifdef  DEBUG
#define debugf(fmt, args...) printf(fmt, ##args)
#else
#define debugf(fmt, args...)
#endif

#ifdef __powerpc64__
#define	PRI0ptrX	"016lx"
#else
#define	PRI0ptrX	"08x"
#endif

#define TODO			panic("%s: not implemented", __func__);

extern unsigned char _etext[];
extern unsigned char _end[];

extern uint32_t *bootinfo;

vm_paddr_t kernload;
vm_offset_t kernstart;
vm_size_t kernsize;

/* Message buffer and tables. */
static vm_offset_t data_start;
static vm_size_t data_end;

/* Phys/avail memory regions. */
static struct mem_region *availmem_regions;
static int availmem_regions_sz;
static struct mem_region *physmem_regions;
static int physmem_regions_sz;

/* Reserved KVA space and mutex for mmu_booke_zero_page. */
static vm_offset_t zero_page_va;
static struct mtx zero_page_mutex;

static struct mtx tlbivax_mutex;

/* Reserved KVA space and mutex for mmu_booke_copy_page. */
static vm_offset_t copy_page_src_va;
static vm_offset_t copy_page_dst_va;
static struct mtx copy_page_mutex;

/**************************************************************************/
/* PMAP */
/**************************************************************************/

static int mmu_booke_enter_locked(mmu_t, pmap_t, vm_offset_t, vm_page_t,
    vm_prot_t, u_int flags, int8_t psind);

unsigned int kptbl_min;		/* Index of the first kernel ptbl. */
unsigned int kernel_ptbls;	/* Number of KVA ptbls. */
#ifdef __powerpc64__
unsigned int kernel_pdirs;
#endif

/*
 * If user pmap is processed with mmu_booke_remove and the resident count
 * drops to 0, there are no more pages to remove, so we need not continue.
 */
#define PMAP_REMOVE_DONE(pmap) \
	((pmap) != kernel_pmap && (pmap)->pm_stats.resident_count == 0)

#if defined(COMPAT_FREEBSD32) || !defined(__powerpc64__)
extern int elf32_nxstack;
#endif

/**************************************************************************/
/* TLB and TID handling */
/**************************************************************************/

/* Translation ID busy table */
static volatile pmap_t tidbusy[MAXCPU][TID_MAX + 1];

/*
 * TLB0 capabilities (entry, way numbers etc.). These can vary between e500
 * core revisions and should be read from h/w registers during early config.
 */
uint32_t tlb0_entries;
uint32_t tlb0_ways;
uint32_t tlb0_entries_per_way;
uint32_t tlb1_entries;

#define TLB0_ENTRIES		(tlb0_entries)
#define TLB0_WAYS		(tlb0_ways)
#define TLB0_ENTRIES_PER_WAY	(tlb0_entries_per_way)

#define TLB1_ENTRIES (tlb1_entries)

static vm_offset_t tlb1_map_base = VM_MAXUSER_ADDRESS + PAGE_SIZE;

static tlbtid_t tid_alloc(struct pmap *);
static void tid_flush(tlbtid_t tid);

#ifdef DDB
#ifdef __powerpc64__
static void tlb_print_entry(int, uint32_t, uint64_t, uint32_t, uint32_t);
#else
static void tlb_print_entry(int, uint32_t, uint32_t, uint32_t, uint32_t);
#endif
#endif

static void tlb1_read_entry(tlb_entry_t *, unsigned int);
static void tlb1_write_entry(tlb_entry_t *, unsigned int);
static int tlb1_iomapped(int, vm_paddr_t, vm_size_t, vm_offset_t *);
static vm_size_t tlb1_mapin_region(vm_offset_t, vm_paddr_t, vm_size_t);

static vm_size_t tsize2size(unsigned int);
static unsigned int size2tsize(vm_size_t);
static unsigned int ilog2(unsigned long);

static void set_mas4_defaults(void);

static inline void tlb0_flush_entry(vm_offset_t);
static inline unsigned int tlb0_tableidx(vm_offset_t, unsigned int);

/**************************************************************************/
/* Page table management */
/**************************************************************************/

static struct rwlock_padalign pvh_global_lock;

/* Data for the pv entry allocation mechanism */
static uma_zone_t pvzone;
static int pv_entry_count = 0, pv_entry_max = 0, pv_entry_high_water = 0;

#define PV_ENTRY_ZONE_MIN	2048	/* min pv entries in uma zone */

#ifndef PMAP_SHPGPERPROC
#define PMAP_SHPGPERPROC	200
#endif

static void ptbl_init(void);
static struct ptbl_buf *ptbl_buf_alloc(void);
static void ptbl_buf_free(struct ptbl_buf *);
static void ptbl_free_pmap_ptbl(pmap_t, pte_t *);

#ifdef __powerpc64__
static pte_t *ptbl_alloc(mmu_t, pmap_t, pte_t **,
			 unsigned int, boolean_t);
static void ptbl_free(mmu_t, pmap_t, pte_t **, unsigned int);
static void ptbl_hold(mmu_t, pmap_t, pte_t **, unsigned int);
static int ptbl_unhold(mmu_t, pmap_t, vm_offset_t);
#else
static pte_t *ptbl_alloc(mmu_t, pmap_t, unsigned int, boolean_t);
static void ptbl_free(mmu_t, pmap_t, unsigned int);
static void ptbl_hold(mmu_t, pmap_t, unsigned int);
static int ptbl_unhold(mmu_t, pmap_t, unsigned int);
#endif

static vm_paddr_t pte_vatopa(mmu_t, pmap_t, vm_offset_t);
static int pte_enter(mmu_t, pmap_t, vm_page_t, vm_offset_t, uint32_t, boolean_t);
static int pte_remove(mmu_t, pmap_t, vm_offset_t, uint8_t);
static pte_t *pte_find(mmu_t, pmap_t, vm_offset_t);
static void kernel_pte_alloc(vm_offset_t, vm_offset_t, vm_offset_t);

static pv_entry_t pv_alloc(void);
static void pv_free(pv_entry_t);
static void pv_insert(pmap_t, vm_offset_t, vm_page_t);
static void pv_remove(pmap_t, vm_offset_t, vm_page_t);

static void booke_pmap_init_qpages(void);

/* Number of kva ptbl buffers, each covering one ptbl (PTBL_PAGES). */
#ifdef __powerpc64__
#define PTBL_BUFS               (16UL * 16 * 16)
#else
#define PTBL_BUFS		(128 * 16)
#endif

struct ptbl_buf {
	TAILQ_ENTRY(ptbl_buf) link;	/* list link */
	vm_offset_t kva;		/* va of mapping */
};

/* ptbl free list and a lock used for access synchronization. */
static TAILQ_HEAD(, ptbl_buf) ptbl_buf_freelist;
static struct mtx ptbl_buf_freelist_lock;

/* Base address of kva space allocated fot ptbl bufs. */
static vm_offset_t ptbl_buf_pool_vabase;

/* Pointer to ptbl_buf structures. */
static struct ptbl_buf *ptbl_bufs;

#ifdef SMP
extern tlb_entry_t __boot_tlb1[];
void pmap_bootstrap_ap(volatile uint32_t *);
#endif

/*
 * Kernel MMU interface
 */
static void		mmu_booke_clear_modify(mmu_t, vm_page_t);
static void		mmu_booke_copy(mmu_t, pmap_t, pmap_t, vm_offset_t,
    vm_size_t, vm_offset_t);
static void		mmu_booke_copy_page(mmu_t, vm_page_t, vm_page_t);
static void		mmu_booke_copy_pages(mmu_t, vm_page_t *,
    vm_offset_t, vm_page_t *, vm_offset_t, int);
static int		mmu_booke_enter(mmu_t, pmap_t, vm_offset_t, vm_page_t,
    vm_prot_t, u_int flags, int8_t psind);
static void		mmu_booke_enter_object(mmu_t, pmap_t, vm_offset_t, vm_offset_t,
    vm_page_t, vm_prot_t);
static void		mmu_booke_enter_quick(mmu_t, pmap_t, vm_offset_t, vm_page_t,
    vm_prot_t);
static vm_paddr_t	mmu_booke_extract(mmu_t, pmap_t, vm_offset_t);
static vm_page_t	mmu_booke_extract_and_hold(mmu_t, pmap_t, vm_offset_t,
    vm_prot_t);
static void		mmu_booke_init(mmu_t);
static boolean_t	mmu_booke_is_modified(mmu_t, vm_page_t);
static boolean_t	mmu_booke_is_prefaultable(mmu_t, pmap_t, vm_offset_t);
static boolean_t	mmu_booke_is_referenced(mmu_t, vm_page_t);
static int		mmu_booke_ts_referenced(mmu_t, vm_page_t);
static vm_offset_t	mmu_booke_map(mmu_t, vm_offset_t *, vm_paddr_t, vm_paddr_t,
    int);
static int		mmu_booke_mincore(mmu_t, pmap_t, vm_offset_t,
    vm_paddr_t *);
static void		mmu_booke_object_init_pt(mmu_t, pmap_t, vm_offset_t,
    vm_object_t, vm_pindex_t, vm_size_t);
static boolean_t	mmu_booke_page_exists_quick(mmu_t, pmap_t, vm_page_t);
static void		mmu_booke_page_init(mmu_t, vm_page_t);
static int		mmu_booke_page_wired_mappings(mmu_t, vm_page_t);
static void		mmu_booke_pinit(mmu_t, pmap_t);
static void		mmu_booke_pinit0(mmu_t, pmap_t);
static void		mmu_booke_protect(mmu_t, pmap_t, vm_offset_t, vm_offset_t,
    vm_prot_t);
static void		mmu_booke_qenter(mmu_t, vm_offset_t, vm_page_t *, int);
static void		mmu_booke_qremove(mmu_t, vm_offset_t, int);
static void		mmu_booke_release(mmu_t, pmap_t);
static void		mmu_booke_remove(mmu_t, pmap_t, vm_offset_t, vm_offset_t);
static void		mmu_booke_remove_all(mmu_t, vm_page_t);
static void		mmu_booke_remove_write(mmu_t, vm_page_t);
static void		mmu_booke_unwire(mmu_t, pmap_t, vm_offset_t, vm_offset_t);
static void		mmu_booke_zero_page(mmu_t, vm_page_t);
static void		mmu_booke_zero_page_area(mmu_t, vm_page_t, int, int);
static void		mmu_booke_activate(mmu_t, struct thread *);
static void		mmu_booke_deactivate(mmu_t, struct thread *);
static void		mmu_booke_bootstrap(mmu_t, vm_offset_t, vm_offset_t);
static void		*mmu_booke_mapdev(mmu_t, vm_paddr_t, vm_size_t);
static void		*mmu_booke_mapdev_attr(mmu_t, vm_paddr_t, vm_size_t, vm_memattr_t);
static void		mmu_booke_unmapdev(mmu_t, vm_offset_t, vm_size_t);
static vm_paddr_t	mmu_booke_kextract(mmu_t, vm_offset_t);
static void		mmu_booke_kenter(mmu_t, vm_offset_t, vm_paddr_t);
static void		mmu_booke_kenter_attr(mmu_t, vm_offset_t, vm_paddr_t, vm_memattr_t);
static void		mmu_booke_kremove(mmu_t, vm_offset_t);
static boolean_t	mmu_booke_dev_direct_mapped(mmu_t, vm_paddr_t, vm_size_t);
static void		mmu_booke_sync_icache(mmu_t, pmap_t, vm_offset_t,
    vm_size_t);
static void		mmu_booke_dumpsys_map(mmu_t, vm_paddr_t pa, size_t,
    void **);
static void		mmu_booke_dumpsys_unmap(mmu_t, vm_paddr_t pa, size_t,
    void *);
static void		mmu_booke_scan_init(mmu_t);
static vm_offset_t	mmu_booke_quick_enter_page(mmu_t mmu, vm_page_t m);
static void		mmu_booke_quick_remove_page(mmu_t mmu, vm_offset_t addr);
static int		mmu_booke_change_attr(mmu_t mmu, vm_offset_t addr,
    vm_size_t sz, vm_memattr_t mode);
static int		mmu_booke_map_user_ptr(mmu_t mmu, pmap_t pm,
    volatile const void *uaddr, void **kaddr, size_t ulen, size_t *klen);
static int		mmu_booke_decode_kernel_ptr(mmu_t mmu, vm_offset_t addr,
    int *is_user, vm_offset_t *decoded_addr);


static mmu_method_t mmu_booke_methods[] = {
	/* pmap dispatcher interface */
	MMUMETHOD(mmu_clear_modify,	mmu_booke_clear_modify),
	MMUMETHOD(mmu_copy,		mmu_booke_copy),
	MMUMETHOD(mmu_copy_page,	mmu_booke_copy_page),
	MMUMETHOD(mmu_copy_pages,	mmu_booke_copy_pages),
	MMUMETHOD(mmu_enter,		mmu_booke_enter),
	MMUMETHOD(mmu_enter_object,	mmu_booke_enter_object),
	MMUMETHOD(mmu_enter_quick,	mmu_booke_enter_quick),
	MMUMETHOD(mmu_extract,		mmu_booke_extract),
	MMUMETHOD(mmu_extract_and_hold,	mmu_booke_extract_and_hold),
	MMUMETHOD(mmu_init,		mmu_booke_init),
	MMUMETHOD(mmu_is_modified,	mmu_booke_is_modified),
	MMUMETHOD(mmu_is_prefaultable,	mmu_booke_is_prefaultable),
	MMUMETHOD(mmu_is_referenced,	mmu_booke_is_referenced),
	MMUMETHOD(mmu_ts_referenced,	mmu_booke_ts_referenced),
	MMUMETHOD(mmu_map,		mmu_booke_map),
	MMUMETHOD(mmu_mincore,		mmu_booke_mincore),
	MMUMETHOD(mmu_object_init_pt,	mmu_booke_object_init_pt),
	MMUMETHOD(mmu_page_exists_quick,mmu_booke_page_exists_quick),
	MMUMETHOD(mmu_page_init,	mmu_booke_page_init),
	MMUMETHOD(mmu_page_wired_mappings, mmu_booke_page_wired_mappings),
	MMUMETHOD(mmu_pinit,		mmu_booke_pinit),
	MMUMETHOD(mmu_pinit0,		mmu_booke_pinit0),
	MMUMETHOD(mmu_protect,		mmu_booke_protect),
	MMUMETHOD(mmu_qenter,		mmu_booke_qenter),
	MMUMETHOD(mmu_qremove,		mmu_booke_qremove),
	MMUMETHOD(mmu_release,		mmu_booke_release),
	MMUMETHOD(mmu_remove,		mmu_booke_remove),
	MMUMETHOD(mmu_remove_all,	mmu_booke_remove_all),
	MMUMETHOD(mmu_remove_write,	mmu_booke_remove_write),
	MMUMETHOD(mmu_sync_icache,	mmu_booke_sync_icache),
	MMUMETHOD(mmu_unwire,		mmu_booke_unwire),
	MMUMETHOD(mmu_zero_page,	mmu_booke_zero_page),
	MMUMETHOD(mmu_zero_page_area,	mmu_booke_zero_page_area),
	MMUMETHOD(mmu_activate,		mmu_booke_activate),
	MMUMETHOD(mmu_deactivate,	mmu_booke_deactivate),
	MMUMETHOD(mmu_quick_enter_page, mmu_booke_quick_enter_page),
	MMUMETHOD(mmu_quick_remove_page, mmu_booke_quick_remove_page),

	/* Internal interfaces */
	MMUMETHOD(mmu_bootstrap,	mmu_booke_bootstrap),
	MMUMETHOD(mmu_dev_direct_mapped,mmu_booke_dev_direct_mapped),
	MMUMETHOD(mmu_mapdev,		mmu_booke_mapdev),
	MMUMETHOD(mmu_mapdev_attr,	mmu_booke_mapdev_attr),
	MMUMETHOD(mmu_kenter,		mmu_booke_kenter),
	MMUMETHOD(mmu_kenter_attr,	mmu_booke_kenter_attr),
	MMUMETHOD(mmu_kextract,		mmu_booke_kextract),
	MMUMETHOD(mmu_kremove,		mmu_booke_kremove),
	MMUMETHOD(mmu_unmapdev,		mmu_booke_unmapdev),
	MMUMETHOD(mmu_change_attr,	mmu_booke_change_attr),
	MMUMETHOD(mmu_map_user_ptr,	mmu_booke_map_user_ptr),
	MMUMETHOD(mmu_decode_kernel_ptr, mmu_booke_decode_kernel_ptr),

	/* dumpsys() support */
	MMUMETHOD(mmu_dumpsys_map,	mmu_booke_dumpsys_map),
	MMUMETHOD(mmu_dumpsys_unmap,	mmu_booke_dumpsys_unmap),
	MMUMETHOD(mmu_scan_init,	mmu_booke_scan_init),

	{ 0, 0 }
};

MMU_DEF(booke_mmu, MMU_TYPE_BOOKE, mmu_booke_methods, 0);

static __inline uint32_t
tlb_calc_wimg(vm_paddr_t pa, vm_memattr_t ma)
{
	uint32_t attrib;
	int i;

	if (ma != VM_MEMATTR_DEFAULT) {
		switch (ma) {
		case VM_MEMATTR_UNCACHEABLE:
			return (MAS2_I | MAS2_G);
		case VM_MEMATTR_WRITE_COMBINING:
		case VM_MEMATTR_WRITE_BACK:
		case VM_MEMATTR_PREFETCHABLE:
			return (MAS2_I);
		case VM_MEMATTR_WRITE_THROUGH:
			return (MAS2_W | MAS2_M);
		case VM_MEMATTR_CACHEABLE:
			return (MAS2_M);
		}
	}

	/*
	 * Assume the page is cache inhibited and access is guarded unless
	 * it's in our available memory array.
	 */
	attrib = _TLB_ENTRY_IO;
	for (i = 0; i < physmem_regions_sz; i++) {
		if ((pa >= physmem_regions[i].mr_start) &&
		    (pa < (physmem_regions[i].mr_start +
		     physmem_regions[i].mr_size))) {
			attrib = _TLB_ENTRY_MEM;
			break;
		}
	}

	return (attrib);
}

static inline void
tlb_miss_lock(void)
{
#ifdef SMP
	struct pcpu *pc;

	if (!smp_started)
		return;

	STAILQ_FOREACH(pc, &cpuhead, pc_allcpu) {
		if (pc != pcpup) {

			CTR3(KTR_PMAP, "%s: tlb miss LOCK of CPU=%d, "
			    "tlb_lock=%p", __func__, pc->pc_cpuid, pc->pc_booke.tlb_lock);

			KASSERT((pc->pc_cpuid != PCPU_GET(cpuid)),
			    ("tlb_miss_lock: tried to lock self"));

			tlb_lock(pc->pc_booke.tlb_lock);

			CTR1(KTR_PMAP, "%s: locked", __func__);
		}
	}
#endif
}

static inline void
tlb_miss_unlock(void)
{
#ifdef SMP
	struct pcpu *pc;

	if (!smp_started)
		return;

	STAILQ_FOREACH(pc, &cpuhead, pc_allcpu) {
		if (pc != pcpup) {
			CTR2(KTR_PMAP, "%s: tlb miss UNLOCK of CPU=%d",
			    __func__, pc->pc_cpuid);

			tlb_unlock(pc->pc_booke.tlb_lock);

			CTR1(KTR_PMAP, "%s: unlocked", __func__);
		}
	}
#endif
}

/* Return number of entries in TLB0. */
static __inline void
tlb0_get_tlbconf(void)
{
	uint32_t tlb0_cfg;

	tlb0_cfg = mfspr(SPR_TLB0CFG);
	tlb0_entries = tlb0_cfg & TLBCFG_NENTRY_MASK;
	tlb0_ways = (tlb0_cfg & TLBCFG_ASSOC_MASK) >> TLBCFG_ASSOC_SHIFT;
	tlb0_entries_per_way = tlb0_entries / tlb0_ways;
}

/* Return number of entries in TLB1. */
static __inline void
tlb1_get_tlbconf(void)
{
	uint32_t tlb1_cfg;

	tlb1_cfg = mfspr(SPR_TLB1CFG);
	tlb1_entries = tlb1_cfg & TLBCFG_NENTRY_MASK;
}

/**************************************************************************/
/* Page table related */
/**************************************************************************/

#ifdef __powerpc64__
/* Initialize pool of kva ptbl buffers. */
static void
ptbl_init(void)
{
	int		i;

	mtx_init(&ptbl_buf_freelist_lock, "ptbl bufs lock", NULL, MTX_DEF);
	TAILQ_INIT(&ptbl_buf_freelist);

	for (i = 0; i < PTBL_BUFS; i++) {
		ptbl_bufs[i].kva = ptbl_buf_pool_vabase +
		    i * MAX(PTBL_PAGES,PDIR_PAGES) * PAGE_SIZE;
		TAILQ_INSERT_TAIL(&ptbl_buf_freelist, &ptbl_bufs[i], link);
	}
}

/* Get an sf_buf from the freelist. */
static struct ptbl_buf *
ptbl_buf_alloc(void)
{
	struct ptbl_buf *buf;

	mtx_lock(&ptbl_buf_freelist_lock);
	buf = TAILQ_FIRST(&ptbl_buf_freelist);
	if (buf != NULL)
		TAILQ_REMOVE(&ptbl_buf_freelist, buf, link);
	mtx_unlock(&ptbl_buf_freelist_lock);

	return (buf);
}

/* Return ptbl buff to free pool. */
static void
ptbl_buf_free(struct ptbl_buf *buf)
{
	mtx_lock(&ptbl_buf_freelist_lock);
	TAILQ_INSERT_TAIL(&ptbl_buf_freelist, buf, link);
	mtx_unlock(&ptbl_buf_freelist_lock);
}

/*
 * Search the list of allocated ptbl bufs and find on list of allocated ptbls
 */
static void
ptbl_free_pmap_ptbl(pmap_t pmap, pte_t * ptbl)
{
	struct ptbl_buf *pbuf;

	TAILQ_FOREACH(pbuf, &pmap->pm_ptbl_list, link) {
		if (pbuf->kva == (vm_offset_t) ptbl) {
			/* Remove from pmap ptbl buf list. */
			TAILQ_REMOVE(&pmap->pm_ptbl_list, pbuf, link);

			/* Free corresponding ptbl buf. */
			ptbl_buf_free(pbuf);

			break;
		}
	}
}

/* Get a pointer to a PTE in a page table. */
static __inline pte_t *
pte_find(mmu_t mmu, pmap_t pmap, vm_offset_t va)
{
	pte_t         **pdir;
	pte_t          *ptbl;

	KASSERT((pmap != NULL), ("pte_find: invalid pmap"));

	pdir = pmap->pm_pp2d[PP2D_IDX(va)];
	if (!pdir)
		return NULL;
	ptbl = pdir[PDIR_IDX(va)];
	return ((ptbl != NULL) ? &ptbl[PTBL_IDX(va)] : NULL);
}

/*
 * Search the list of allocated pdir bufs and find on list of allocated pdirs
 */
static void
ptbl_free_pmap_pdir(mmu_t mmu, pmap_t pmap, pte_t ** pdir)
{
	struct ptbl_buf *pbuf;

	TAILQ_FOREACH(pbuf, &pmap->pm_pdir_list, link) {
		if (pbuf->kva == (vm_offset_t) pdir) {
			/* Remove from pmap ptbl buf list. */
			TAILQ_REMOVE(&pmap->pm_pdir_list, pbuf, link);

			/* Free corresponding pdir buf. */
			ptbl_buf_free(pbuf);

			break;
		}
	}
}
/* Free pdir pages and invalidate pdir entry. */
static void
pdir_free(mmu_t mmu, pmap_t pmap, unsigned int pp2d_idx)
{
	pte_t         **pdir;
	vm_paddr_t	pa;
	vm_offset_t	va;
	vm_page_t	m;
	int		i;

	pdir = pmap->pm_pp2d[pp2d_idx];

	KASSERT((pdir != NULL), ("pdir_free: null pdir"));

	pmap->pm_pp2d[pp2d_idx] = NULL;

	for (i = 0; i < PDIR_PAGES; i++) {
		va = ((vm_offset_t) pdir + (i * PAGE_SIZE));
		pa = pte_vatopa(mmu, kernel_pmap, va);
		m = PHYS_TO_VM_PAGE(pa);
		vm_page_free_zero(m);
		vm_wire_sub(1);
		pmap_kremove(va);
	}

	ptbl_free_pmap_pdir(mmu, pmap, pdir);
}

/*
 * Decrement pdir pages hold count and attempt to free pdir pages. Called
 * when removing directory entry from pdir.
 * 
 * Return 1 if pdir pages were freed.
 */
static int
pdir_unhold(mmu_t mmu, pmap_t pmap, u_int pp2d_idx)
{
	pte_t         **pdir;
	vm_paddr_t	pa;
	vm_page_t	m;
	int		i;

	KASSERT((pmap != kernel_pmap),
		("pdir_unhold: unholding kernel pdir!"));

	pdir = pmap->pm_pp2d[pp2d_idx];

	KASSERT(((vm_offset_t) pdir >= VM_MIN_KERNEL_ADDRESS),
	    ("pdir_unhold: non kva pdir"));

	/* decrement hold count */
	for (i = 0; i < PDIR_PAGES; i++) {
		pa = pte_vatopa(mmu, kernel_pmap,
		    (vm_offset_t) pdir + (i * PAGE_SIZE));
		m = PHYS_TO_VM_PAGE(pa);
		m->wire_count--;
	}

	/*
	 * Free pdir pages if there are no dir entries in this pdir.
	 * wire_count has the same value for all ptbl pages, so check the
	 * last page.
	 */
	if (m->wire_count == 0) {
		pdir_free(mmu, pmap, pp2d_idx);
		return (1);
	}
	return (0);
}

/*
 * Increment hold count for pdir pages. This routine is used when new ptlb
 * entry is being inserted into pdir.
 */
static void
pdir_hold(mmu_t mmu, pmap_t pmap, pte_t ** pdir)
{
	vm_paddr_t	pa;
	vm_page_t	m;
	int		i;

	KASSERT((pmap != kernel_pmap),
		("pdir_hold: holding kernel pdir!"));

	KASSERT((pdir != NULL), ("pdir_hold: null pdir"));

	for (i = 0; i < PDIR_PAGES; i++) {
		pa = pte_vatopa(mmu, kernel_pmap,
				(vm_offset_t) pdir + (i * PAGE_SIZE));
		m = PHYS_TO_VM_PAGE(pa);
		m->wire_count++;
	}
}

/* Allocate page table. */
static pte_t   *
ptbl_alloc(mmu_t mmu, pmap_t pmap, pte_t ** pdir, unsigned int pdir_idx,
    boolean_t nosleep)
{
	vm_page_t	mtbl  [PTBL_PAGES];
	vm_page_t	m;
	struct ptbl_buf *pbuf;
	unsigned int	pidx;
	pte_t          *ptbl;
	int		i, j;
	int		req;

	KASSERT((pdir[pdir_idx] == NULL),
		("%s: valid ptbl entry exists!", __func__));

	pbuf = ptbl_buf_alloc();
	if (pbuf == NULL)
		panic("%s: couldn't alloc kernel virtual memory", __func__);

	ptbl = (pte_t *) pbuf->kva;

	for (i = 0; i < PTBL_PAGES; i++) {
		pidx = (PTBL_PAGES * pdir_idx) + i;
		req = VM_ALLOC_NOOBJ | VM_ALLOC_WIRED;
		while ((m = vm_page_alloc(NULL, pidx, req)) == NULL) {
			PMAP_UNLOCK(pmap);
			rw_wunlock(&pvh_global_lock);
			if (nosleep) {
				ptbl_free_pmap_ptbl(pmap, ptbl);
				for (j = 0; j < i; j++)
					vm_page_free(mtbl[j]);
				vm_wire_sub(i);
				return (NULL);
			}
			vm_wait(NULL);
			rw_wlock(&pvh_global_lock);
			PMAP_LOCK(pmap);
		}
		mtbl[i] = m;
	}

	/* Mapin allocated pages into kernel_pmap. */
	mmu_booke_qenter(mmu, (vm_offset_t) ptbl, mtbl, PTBL_PAGES);
	/* Zero whole ptbl. */
	bzero((caddr_t) ptbl, PTBL_PAGES * PAGE_SIZE);

	/* Add pbuf to the pmap ptbl bufs list. */
	TAILQ_INSERT_TAIL(&pmap->pm_ptbl_list, pbuf, link);

	return (ptbl);
}

/* Free ptbl pages and invalidate pdir entry. */
static void
ptbl_free(mmu_t mmu, pmap_t pmap, pte_t ** pdir, unsigned int pdir_idx)
{
	pte_t          *ptbl;
	vm_paddr_t	pa;
	vm_offset_t	va;
	vm_page_t	m;
	int		i;

	ptbl = pdir[pdir_idx];

	KASSERT((ptbl != NULL), ("ptbl_free: null ptbl"));

	pdir[pdir_idx] = NULL;

	for (i = 0; i < PTBL_PAGES; i++) {
		va = ((vm_offset_t) ptbl + (i * PAGE_SIZE));
		pa = pte_vatopa(mmu, kernel_pmap, va);
		m = PHYS_TO_VM_PAGE(pa);
		vm_page_free_zero(m);
		vm_wire_sub(1);
		pmap_kremove(va);
	}

	ptbl_free_pmap_ptbl(pmap, ptbl);
}

/*
 * Decrement ptbl pages hold count and attempt to free ptbl pages. Called
 * when removing pte entry from ptbl.
 * 
 * Return 1 if ptbl pages were freed.
 */
static int
ptbl_unhold(mmu_t mmu, pmap_t pmap, vm_offset_t va)
{
	pte_t          *ptbl;
	vm_paddr_t	pa;
	vm_page_t	m;
	u_int		pp2d_idx;
	pte_t         **pdir;
	u_int		pdir_idx;
	int		i;

	pp2d_idx = PP2D_IDX(va);
	pdir_idx = PDIR_IDX(va);

	KASSERT((pmap != kernel_pmap),
		("ptbl_unhold: unholding kernel ptbl!"));

	pdir = pmap->pm_pp2d[pp2d_idx];
	ptbl = pdir[pdir_idx];

	KASSERT(((vm_offset_t) ptbl >= VM_MIN_KERNEL_ADDRESS),
	    ("ptbl_unhold: non kva ptbl"));

	/* decrement hold count */
	for (i = 0; i < PTBL_PAGES; i++) {
		pa = pte_vatopa(mmu, kernel_pmap,
		    (vm_offset_t) ptbl + (i * PAGE_SIZE));
		m = PHYS_TO_VM_PAGE(pa);
		m->wire_count--;
	}

	/*
	 * Free ptbl pages if there are no pte entries in this ptbl.
	 * wire_count has the same value for all ptbl pages, so check the
	 * last page.
	 */
	if (m->wire_count == 0) {
		/* A pair of indirect entries might point to this ptbl page */
#if 0
		tlb_flush_entry(pmap, va & ~((2UL * PAGE_SIZE_1M) - 1),
				TLB_SIZE_1M, MAS6_SIND);
		tlb_flush_entry(pmap, (va & ~((2UL * PAGE_SIZE_1M) - 1)) | PAGE_SIZE_1M,
				TLB_SIZE_1M, MAS6_SIND);
#endif
		ptbl_free(mmu, pmap, pdir, pdir_idx);
		pdir_unhold(mmu, pmap, pp2d_idx);
		return (1);
	}
	return (0);
}

/*
 * Increment hold count for ptbl pages. This routine is used when new pte
 * entry is being inserted into ptbl.
 */
static void
ptbl_hold(mmu_t mmu, pmap_t pmap, pte_t ** pdir, unsigned int pdir_idx)
{
	vm_paddr_t	pa;
	pte_t          *ptbl;
	vm_page_t	m;
	int		i;

	KASSERT((pmap != kernel_pmap),
		("ptbl_hold: holding kernel ptbl!"));

	ptbl = pdir[pdir_idx];

	KASSERT((ptbl != NULL), ("ptbl_hold: null ptbl"));

	for (i = 0; i < PTBL_PAGES; i++) {
		pa = pte_vatopa(mmu, kernel_pmap,
				(vm_offset_t) ptbl + (i * PAGE_SIZE));
		m = PHYS_TO_VM_PAGE(pa);
		m->wire_count++;
	}
}
#else

/* Initialize pool of kva ptbl buffers. */
static void
ptbl_init(void)
{
	int i;

	CTR3(KTR_PMAP, "%s: s (ptbl_bufs = 0x%08x size 0x%08x)", __func__,
	    (uint32_t)ptbl_bufs, sizeof(struct ptbl_buf) * PTBL_BUFS);
	CTR3(KTR_PMAP, "%s: s (ptbl_buf_pool_vabase = 0x%08x size = 0x%08x)",
	    __func__, ptbl_buf_pool_vabase, PTBL_BUFS * PTBL_PAGES * PAGE_SIZE);

	mtx_init(&ptbl_buf_freelist_lock, "ptbl bufs lock", NULL, MTX_DEF);
	TAILQ_INIT(&ptbl_buf_freelist);

	for (i = 0; i < PTBL_BUFS; i++) {
		ptbl_bufs[i].kva =
		    ptbl_buf_pool_vabase + i * PTBL_PAGES * PAGE_SIZE;
		TAILQ_INSERT_TAIL(&ptbl_buf_freelist, &ptbl_bufs[i], link);
	}
}

/* Get a ptbl_buf from the freelist. */
static struct ptbl_buf *
ptbl_buf_alloc(void)
{
	struct ptbl_buf *buf;

	mtx_lock(&ptbl_buf_freelist_lock);
	buf = TAILQ_FIRST(&ptbl_buf_freelist);
	if (buf != NULL)
		TAILQ_REMOVE(&ptbl_buf_freelist, buf, link);
	mtx_unlock(&ptbl_buf_freelist_lock);

	CTR2(KTR_PMAP, "%s: buf = %p", __func__, buf);

	return (buf);
}

/* Return ptbl buff to free pool. */
static void
ptbl_buf_free(struct ptbl_buf *buf)
{

	CTR2(KTR_PMAP, "%s: buf = %p", __func__, buf);

	mtx_lock(&ptbl_buf_freelist_lock);
	TAILQ_INSERT_TAIL(&ptbl_buf_freelist, buf, link);
	mtx_unlock(&ptbl_buf_freelist_lock);
}

/*
 * Search the list of allocated ptbl bufs and find on list of allocated ptbls
 */
static void
ptbl_free_pmap_ptbl(pmap_t pmap, pte_t *ptbl)
{
	struct ptbl_buf *pbuf;

	CTR2(KTR_PMAP, "%s: ptbl = %p", __func__, ptbl);

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	TAILQ_FOREACH(pbuf, &pmap->pm_ptbl_list, link)
		if (pbuf->kva == (vm_offset_t)ptbl) {
			/* Remove from pmap ptbl buf list. */
			TAILQ_REMOVE(&pmap->pm_ptbl_list, pbuf, link);

			/* Free corresponding ptbl buf. */
			ptbl_buf_free(pbuf);
			break;
		}
}

/* Allocate page table. */
static pte_t *
ptbl_alloc(mmu_t mmu, pmap_t pmap, unsigned int pdir_idx, boolean_t nosleep)
{
	vm_page_t mtbl[PTBL_PAGES];
	vm_page_t m;
	struct ptbl_buf *pbuf;
	unsigned int pidx;
	pte_t *ptbl;
	int i, j;

	CTR4(KTR_PMAP, "%s: pmap = %p su = %d pdir_idx = %d", __func__, pmap,
	    (pmap == kernel_pmap), pdir_idx);

	KASSERT((pdir_idx <= (VM_MAXUSER_ADDRESS / PDIR_SIZE)),
	    ("ptbl_alloc: invalid pdir_idx"));
	KASSERT((pmap->pm_pdir[pdir_idx] == NULL),
	    ("pte_alloc: valid ptbl entry exists!"));

	pbuf = ptbl_buf_alloc();
	if (pbuf == NULL)
		panic("pte_alloc: couldn't alloc kernel virtual memory");
		
	ptbl = (pte_t *)pbuf->kva;

	CTR2(KTR_PMAP, "%s: ptbl kva = %p", __func__, ptbl);

	for (i = 0; i < PTBL_PAGES; i++) {
		pidx = (PTBL_PAGES * pdir_idx) + i;
		while ((m = vm_page_alloc(NULL, pidx,
		    VM_ALLOC_NOOBJ | VM_ALLOC_WIRED)) == NULL) {
			PMAP_UNLOCK(pmap);
			rw_wunlock(&pvh_global_lock);
			if (nosleep) {
				ptbl_free_pmap_ptbl(pmap, ptbl);
				for (j = 0; j < i; j++)
					vm_page_free(mtbl[j]);
				vm_wire_sub(i);
				return (NULL);
			}
			vm_wait(NULL);
			rw_wlock(&pvh_global_lock);
			PMAP_LOCK(pmap);
		}
		mtbl[i] = m;
	}

	/* Map allocated pages into kernel_pmap. */
	mmu_booke_qenter(mmu, (vm_offset_t)ptbl, mtbl, PTBL_PAGES);

	/* Zero whole ptbl. */
	bzero((caddr_t)ptbl, PTBL_PAGES * PAGE_SIZE);

	/* Add pbuf to the pmap ptbl bufs list. */
	TAILQ_INSERT_TAIL(&pmap->pm_ptbl_list, pbuf, link);

	return (ptbl);
}

/* Free ptbl pages and invalidate pdir entry. */
static void
ptbl_free(mmu_t mmu, pmap_t pmap, unsigned int pdir_idx)
{
	pte_t *ptbl;
	vm_paddr_t pa;
	vm_offset_t va;
	vm_page_t m;
	int i;

	CTR4(KTR_PMAP, "%s: pmap = %p su = %d pdir_idx = %d", __func__, pmap,
	    (pmap == kernel_pmap), pdir_idx);

	KASSERT((pdir_idx <= (VM_MAXUSER_ADDRESS / PDIR_SIZE)),
	    ("ptbl_free: invalid pdir_idx"));

	ptbl = pmap->pm_pdir[pdir_idx];

	CTR2(KTR_PMAP, "%s: ptbl = %p", __func__, ptbl);

	KASSERT((ptbl != NULL), ("ptbl_free: null ptbl"));

	/*
	 * Invalidate the pdir entry as soon as possible, so that other CPUs
	 * don't attempt to look up the page tables we are releasing.
	 */
	mtx_lock_spin(&tlbivax_mutex);
	tlb_miss_lock();
	
	pmap->pm_pdir[pdir_idx] = NULL;

	tlb_miss_unlock();
	mtx_unlock_spin(&tlbivax_mutex);

	for (i = 0; i < PTBL_PAGES; i++) {
		va = ((vm_offset_t)ptbl + (i * PAGE_SIZE));
		pa = pte_vatopa(mmu, kernel_pmap, va);
		m = PHYS_TO_VM_PAGE(pa);
		vm_page_free_zero(m);
		vm_wire_sub(1);
		mmu_booke_kremove(mmu, va);
	}

	ptbl_free_pmap_ptbl(pmap, ptbl);
}

/*
 * Decrement ptbl pages hold count and attempt to free ptbl pages.
 * Called when removing pte entry from ptbl.
 *
 * Return 1 if ptbl pages were freed.
 */
static int
ptbl_unhold(mmu_t mmu, pmap_t pmap, unsigned int pdir_idx)
{
	pte_t *ptbl;
	vm_paddr_t pa;
	vm_page_t m;
	int i;

	CTR4(KTR_PMAP, "%s: pmap = %p su = %d pdir_idx = %d", __func__, pmap,
	    (pmap == kernel_pmap), pdir_idx);

	KASSERT((pdir_idx <= (VM_MAXUSER_ADDRESS / PDIR_SIZE)),
	    ("ptbl_unhold: invalid pdir_idx"));
	KASSERT((pmap != kernel_pmap),
	    ("ptbl_unhold: unholding kernel ptbl!"));

	ptbl = pmap->pm_pdir[pdir_idx];

	//debugf("ptbl_unhold: ptbl = 0x%08x\n", (u_int32_t)ptbl);
	KASSERT(((vm_offset_t)ptbl >= VM_MIN_KERNEL_ADDRESS),
	    ("ptbl_unhold: non kva ptbl"));

	/* decrement hold count */
	for (i = 0; i < PTBL_PAGES; i++) {
		pa = pte_vatopa(mmu, kernel_pmap,
		    (vm_offset_t)ptbl + (i * PAGE_SIZE));
		m = PHYS_TO_VM_PAGE(pa);
		m->wire_count--;
	}

	/*
	 * Free ptbl pages if there are no pte etries in this ptbl.
	 * wire_count has the same value for all ptbl pages, so check the last
	 * page.
	 */
	if (m->wire_count == 0) {
		ptbl_free(mmu, pmap, pdir_idx);

		//debugf("ptbl_unhold: e (freed ptbl)\n");
		return (1);
	}

	return (0);
}

/*
 * Increment hold count for ptbl pages. This routine is used when a new pte
 * entry is being inserted into the ptbl.
 */
static void
ptbl_hold(mmu_t mmu, pmap_t pmap, unsigned int pdir_idx)
{
	vm_paddr_t pa;
	pte_t *ptbl;
	vm_page_t m;
	int i;

	CTR3(KTR_PMAP, "%s: pmap = %p pdir_idx = %d", __func__, pmap,
	    pdir_idx);

	KASSERT((pdir_idx <= (VM_MAXUSER_ADDRESS / PDIR_SIZE)),
	    ("ptbl_hold: invalid pdir_idx"));
	KASSERT((pmap != kernel_pmap),
	    ("ptbl_hold: holding kernel ptbl!"));

	ptbl = pmap->pm_pdir[pdir_idx];

	KASSERT((ptbl != NULL), ("ptbl_hold: null ptbl"));

	for (i = 0; i < PTBL_PAGES; i++) {
		pa = pte_vatopa(mmu, kernel_pmap,
		    (vm_offset_t)ptbl + (i * PAGE_SIZE));
		m = PHYS_TO_VM_PAGE(pa);
		m->wire_count++;
	}
}
#endif

/* Allocate pv_entry structure. */
pv_entry_t
pv_alloc(void)
{
	pv_entry_t pv;

	pv_entry_count++;
	if (pv_entry_count > pv_entry_high_water)
		pagedaemon_wakeup(0); /* XXX powerpc NUMA */
	pv = uma_zalloc(pvzone, M_NOWAIT);

	return (pv);
}

/* Free pv_entry structure. */
static __inline void
pv_free(pv_entry_t pve)
{

	pv_entry_count--;
	uma_zfree(pvzone, pve);
}


/* Allocate and initialize pv_entry structure. */
static void
pv_insert(pmap_t pmap, vm_offset_t va, vm_page_t m)
{
	pv_entry_t pve;

	//int su = (pmap == kernel_pmap);
	//debugf("pv_insert: s (su = %d pmap = 0x%08x va = 0x%08x m = 0x%08x)\n", su,
	//	(u_int32_t)pmap, va, (u_int32_t)m);

	pve = pv_alloc();
	if (pve == NULL)
		panic("pv_insert: no pv entries!");

	pve->pv_pmap = pmap;
	pve->pv_va = va;

	/* add to pv_list */
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	rw_assert(&pvh_global_lock, RA_WLOCKED);

	TAILQ_INSERT_TAIL(&m->md.pv_list, pve, pv_link);

	//debugf("pv_insert: e\n");
}

/* Destroy pv entry. */
static void
pv_remove(pmap_t pmap, vm_offset_t va, vm_page_t m)
{
	pv_entry_t pve;

	//int su = (pmap == kernel_pmap);
	//debugf("pv_remove: s (su = %d pmap = 0x%08x va = 0x%08x)\n", su, (u_int32_t)pmap, va);

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	rw_assert(&pvh_global_lock, RA_WLOCKED);

	/* find pv entry */
	TAILQ_FOREACH(pve, &m->md.pv_list, pv_link) {
		if ((pmap == pve->pv_pmap) && (va == pve->pv_va)) {
			/* remove from pv_list */
			TAILQ_REMOVE(&m->md.pv_list, pve, pv_link);
			if (TAILQ_EMPTY(&m->md.pv_list))
				vm_page_aflag_clear(m, PGA_WRITEABLE);

			/* free pv entry struct */
			pv_free(pve);
			break;
		}
	}

	//debugf("pv_remove: e\n");
}

#ifdef __powerpc64__
/*
 * Clean pte entry, try to free page table page if requested.
 * 
 * Return 1 if ptbl pages were freed, otherwise return 0.
 */
static int
pte_remove(mmu_t mmu, pmap_t pmap, vm_offset_t va, u_int8_t flags)
{
	vm_page_t	m;
	pte_t          *pte;

	pte = pte_find(mmu, pmap, va);
	KASSERT(pte != NULL, ("%s: NULL pte", __func__));

	if (!PTE_ISVALID(pte))
		return (0);

	/* Get vm_page_t for mapped pte. */
	m = PHYS_TO_VM_PAGE(PTE_PA(pte));

	if (PTE_ISWIRED(pte))
		pmap->pm_stats.wired_count--;

	/* Handle managed entry. */
	if (PTE_ISMANAGED(pte)) {

		/* Handle modified pages. */
		if (PTE_ISMODIFIED(pte))
			vm_page_dirty(m);

		/* Referenced pages. */
		if (PTE_ISREFERENCED(pte))
			vm_page_aflag_set(m, PGA_REFERENCED);

		/* Remove pv_entry from pv_list. */
		pv_remove(pmap, va, m);
	} else if (m->md.pv_tracked) {
		pv_remove(pmap, va, m);
		if (TAILQ_EMPTY(&m->md.pv_list))
			m->md.pv_tracked = false;
	}
	mtx_lock_spin(&tlbivax_mutex);
	tlb_miss_lock();

	tlb0_flush_entry(va);
	*pte = 0;

	tlb_miss_unlock();
	mtx_unlock_spin(&tlbivax_mutex);

	pmap->pm_stats.resident_count--;

	if (flags & PTBL_UNHOLD) {
		return (ptbl_unhold(mmu, pmap, va));
	}
	return (0);
}

/*
 * allocate a page of pointers to page directories, do not preallocate the
 * page tables
 */
static pte_t  **
pdir_alloc(mmu_t mmu, pmap_t pmap, unsigned int pp2d_idx, bool nosleep)
{
	vm_page_t	mtbl  [PDIR_PAGES];
	vm_page_t	m;
	struct ptbl_buf *pbuf;
	pte_t         **pdir;
	unsigned int	pidx;
	int		i;
	int		req;

	pbuf = ptbl_buf_alloc();

	if (pbuf == NULL)
		panic("%s: couldn't alloc kernel virtual memory", __func__);

	/* Allocate pdir pages, this will sleep! */
	for (i = 0; i < PDIR_PAGES; i++) {
		pidx = (PDIR_PAGES * pp2d_idx) + i;
		req = VM_ALLOC_NOOBJ | VM_ALLOC_WIRED;
		while ((m = vm_page_alloc(NULL, pidx, req)) == NULL) {
			PMAP_UNLOCK(pmap);
			vm_wait(NULL);
			PMAP_LOCK(pmap);
		}
		mtbl[i] = m;
	}

	/* Mapin allocated pages into kernel_pmap. */
	pdir = (pte_t **) pbuf->kva;
	pmap_qenter((vm_offset_t) pdir, mtbl, PDIR_PAGES);

	/* Zero whole pdir. */
	bzero((caddr_t) pdir, PDIR_PAGES * PAGE_SIZE);

	/* Add pdir to the pmap pdir bufs list. */
	TAILQ_INSERT_TAIL(&pmap->pm_pdir_list, pbuf, link);

	return pdir;
}

/*
 * Insert PTE for a given page and virtual address.
 */
static int
pte_enter(mmu_t mmu, pmap_t pmap, vm_page_t m, vm_offset_t va, uint32_t flags,
    boolean_t nosleep)
{
	unsigned int	pp2d_idx = PP2D_IDX(va);
	unsigned int	pdir_idx = PDIR_IDX(va);
	unsigned int	ptbl_idx = PTBL_IDX(va);
	pte_t          *ptbl, *pte;
	pte_t         **pdir;

	/* Get the page directory pointer. */
	pdir = pmap->pm_pp2d[pp2d_idx];
	if (pdir == NULL)
		pdir = pdir_alloc(mmu, pmap, pp2d_idx, nosleep);

	/* Get the page table pointer. */
	ptbl = pdir[pdir_idx];

	if (ptbl == NULL) {
		/* Allocate page table pages. */
		ptbl = ptbl_alloc(mmu, pmap, pdir, pdir_idx, nosleep);
		if (ptbl == NULL) {
			KASSERT(nosleep, ("nosleep and NULL ptbl"));
			return (ENOMEM);
		}
	} else {
		/*
		 * Check if there is valid mapping for requested va, if there
		 * is, remove it.
		 */
		pte = &pdir[pdir_idx][ptbl_idx];
		if (PTE_ISVALID(pte)) {
			pte_remove(mmu, pmap, va, PTBL_HOLD);
		} else {
			/*
			 * pte is not used, increment hold count for ptbl
			 * pages.
			 */
			if (pmap != kernel_pmap)
				ptbl_hold(mmu, pmap, pdir, pdir_idx);
		}
	}

	if (pdir[pdir_idx] == NULL) {
		if (pmap != kernel_pmap && pmap->pm_pp2d[pp2d_idx] != NULL)
			pdir_hold(mmu, pmap, pdir);
		pdir[pdir_idx] = ptbl;
	}
	if (pmap->pm_pp2d[pp2d_idx] == NULL)
		pmap->pm_pp2d[pp2d_idx] = pdir;

	/*
	 * Insert pv_entry into pv_list for mapped page if part of managed
	 * memory.
	 */
	if ((m->oflags & VPO_UNMANAGED) == 0) {
		flags |= PTE_MANAGED;

		/* Create and insert pv entry. */
		pv_insert(pmap, va, m);
	}

	mtx_lock_spin(&tlbivax_mutex);
	tlb_miss_lock();

	tlb0_flush_entry(va);
	pmap->pm_stats.resident_count++;
	pte = &pdir[pdir_idx][ptbl_idx];
	*pte = PTE_RPN_FROM_PA(VM_PAGE_TO_PHYS(m));
	*pte |= (PTE_VALID | flags);

	tlb_miss_unlock();
	mtx_unlock_spin(&tlbivax_mutex);

	return (0);
}

/* Return the pa for the given pmap/va. */
static	vm_paddr_t
pte_vatopa(mmu_t mmu, pmap_t pmap, vm_offset_t va)
{
	vm_paddr_t	pa = 0;
	pte_t          *pte;

	pte = pte_find(mmu, pmap, va);
	if ((pte != NULL) && PTE_ISVALID(pte))
		pa = (PTE_PA(pte) | (va & PTE_PA_MASK));
	return (pa);
}


/* allocate pte entries to manage (addr & mask) to (addr & mask) + size */
static void
kernel_pte_alloc(vm_offset_t data_end, vm_offset_t addr, vm_offset_t pdir)
{
	int		i, j;
	vm_offset_t	va;
	pte_t		*pte;

	va = addr;
	/* Initialize kernel pdir */
	for (i = 0; i < kernel_pdirs; i++) {
		kernel_pmap->pm_pp2d[i + PP2D_IDX(va)] =
		    (pte_t **)(pdir + (i * PAGE_SIZE * PDIR_PAGES));
		for (j = PDIR_IDX(va + (i * PAGE_SIZE * PDIR_NENTRIES * PTBL_NENTRIES));
		    j < PDIR_NENTRIES; j++) {
			kernel_pmap->pm_pp2d[i + PP2D_IDX(va)][j] =
			    (pte_t *)(pdir + (kernel_pdirs * PAGE_SIZE * PDIR_PAGES) +
			     (((i * PDIR_NENTRIES) + j) * PAGE_SIZE * PTBL_PAGES));
		}
	}

	/*
	 * Fill in PTEs covering kernel code and data. They are not required
	 * for address translation, as this area is covered by static TLB1
	 * entries, but for pte_vatopa() to work correctly with kernel area
	 * addresses.
	 */
	for (va = addr; va < data_end; va += PAGE_SIZE) {
		pte = &(kernel_pmap->pm_pp2d[PP2D_IDX(va)][PDIR_IDX(va)][PTBL_IDX(va)]);
		*pte = PTE_RPN_FROM_PA(kernload + (va - kernstart));
		*pte |= PTE_M | PTE_SR | PTE_SW | PTE_SX | PTE_WIRED |
		    PTE_VALID | PTE_PS_4KB;
	}
}
#else
/*
 * Clean pte entry, try to free page table page if requested.
 *
 * Return 1 if ptbl pages were freed, otherwise return 0.
 */
static int
pte_remove(mmu_t mmu, pmap_t pmap, vm_offset_t va, uint8_t flags)
{
	unsigned int pdir_idx = PDIR_IDX(va);
	unsigned int ptbl_idx = PTBL_IDX(va);
	vm_page_t m;
	pte_t *ptbl;
	pte_t *pte;

	//int su = (pmap == kernel_pmap);
	//debugf("pte_remove: s (su = %d pmap = 0x%08x va = 0x%08x flags = %d)\n",
	//		su, (u_int32_t)pmap, va, flags);

	ptbl = pmap->pm_pdir[pdir_idx];
	KASSERT(ptbl, ("pte_remove: null ptbl"));

	pte = &ptbl[ptbl_idx];

	if (pte == NULL || !PTE_ISVALID(pte))
		return (0);

	if (PTE_ISWIRED(pte))
		pmap->pm_stats.wired_count--;

	/* Get vm_page_t for mapped pte. */
	m = PHYS_TO_VM_PAGE(PTE_PA(pte));

	/* Handle managed entry. */
	if (PTE_ISMANAGED(pte)) {

		if (PTE_ISMODIFIED(pte))
			vm_page_dirty(m);

		if (PTE_ISREFERENCED(pte))
			vm_page_aflag_set(m, PGA_REFERENCED);

		pv_remove(pmap, va, m);
	} else if (m->md.pv_tracked) {
		/*
		 * Always pv_insert()/pv_remove() on MPC85XX, in case DPAA is
		 * used.  This is needed by the NCSW support code for fast
		 * VA<->PA translation.
		 */
		pv_remove(pmap, va, m);
		if (TAILQ_EMPTY(&m->md.pv_list))
			m->md.pv_tracked = false;
	}

	mtx_lock_spin(&tlbivax_mutex);
	tlb_miss_lock();

	tlb0_flush_entry(va);
	*pte = 0;

	tlb_miss_unlock();
	mtx_unlock_spin(&tlbivax_mutex);

	pmap->pm_stats.resident_count--;

	if (flags & PTBL_UNHOLD) {
		//debugf("pte_remove: e (unhold)\n");
		return (ptbl_unhold(mmu, pmap, pdir_idx));
	}

	//debugf("pte_remove: e\n");
	return (0);
}

/*
 * Insert PTE for a given page and virtual address.
 */
static int
pte_enter(mmu_t mmu, pmap_t pmap, vm_page_t m, vm_offset_t va, uint32_t flags,
    boolean_t nosleep)
{
	unsigned int pdir_idx = PDIR_IDX(va);
	unsigned int ptbl_idx = PTBL_IDX(va);
	pte_t *ptbl, *pte;

	CTR4(KTR_PMAP, "%s: su = %d pmap = %p va = %p", __func__,
	    pmap == kernel_pmap, pmap, va);

	/* Get the page table pointer. */
	ptbl = pmap->pm_pdir[pdir_idx];

	if (ptbl == NULL) {
		/* Allocate page table pages. */
		ptbl = ptbl_alloc(mmu, pmap, pdir_idx, nosleep);
		if (ptbl == NULL) {
			KASSERT(nosleep, ("nosleep and NULL ptbl"));
			return (ENOMEM);
		}
	} else {
		/*
		 * Check if there is valid mapping for requested
		 * va, if there is, remove it.
		 */
		pte = &pmap->pm_pdir[pdir_idx][ptbl_idx];
		if (PTE_ISVALID(pte)) {
			pte_remove(mmu, pmap, va, PTBL_HOLD);
		} else {
			/*
			 * pte is not used, increment hold count
			 * for ptbl pages.
			 */
			if (pmap != kernel_pmap)
				ptbl_hold(mmu, pmap, pdir_idx);
		}
	}

	/*
	 * Insert pv_entry into pv_list for mapped page if part of managed
	 * memory.
	 */
	if ((m->oflags & VPO_UNMANAGED) == 0) {
		flags |= PTE_MANAGED;

		/* Create and insert pv entry. */
		pv_insert(pmap, va, m);
	}

	pmap->pm_stats.resident_count++;
	
	mtx_lock_spin(&tlbivax_mutex);
	tlb_miss_lock();

	tlb0_flush_entry(va);
	if (pmap->pm_pdir[pdir_idx] == NULL) {
		/*
		 * If we just allocated a new page table, hook it in
		 * the pdir.
		 */
		pmap->pm_pdir[pdir_idx] = ptbl;
	}
	pte = &(pmap->pm_pdir[pdir_idx][ptbl_idx]);
	*pte = PTE_RPN_FROM_PA(VM_PAGE_TO_PHYS(m));
	*pte |= (PTE_VALID | flags | PTE_PS_4KB); /* 4KB pages only */

	tlb_miss_unlock();
	mtx_unlock_spin(&tlbivax_mutex);
	return (0);
}

/* Return the pa for the given pmap/va. */
static vm_paddr_t
pte_vatopa(mmu_t mmu, pmap_t pmap, vm_offset_t va)
{
	vm_paddr_t pa = 0;
	pte_t *pte;

	pte = pte_find(mmu, pmap, va);
	if ((pte != NULL) && PTE_ISVALID(pte))
		pa = (PTE_PA(pte) | (va & PTE_PA_MASK));
	return (pa);
}

/* Get a pointer to a PTE in a page table. */
static pte_t *
pte_find(mmu_t mmu, pmap_t pmap, vm_offset_t va)
{
	unsigned int pdir_idx = PDIR_IDX(va);
	unsigned int ptbl_idx = PTBL_IDX(va);

	KASSERT((pmap != NULL), ("pte_find: invalid pmap"));

	if (pmap->pm_pdir[pdir_idx])
		return (&(pmap->pm_pdir[pdir_idx][ptbl_idx]));

	return (NULL);
}

/* Set up kernel page tables. */
static void
kernel_pte_alloc(vm_offset_t data_end, vm_offset_t addr, vm_offset_t pdir)
{
	int		i;
	vm_offset_t	va;
	pte_t		*pte;

	/* Initialize kernel pdir */
	for (i = 0; i < kernel_ptbls; i++)
		kernel_pmap->pm_pdir[kptbl_min + i] =
		    (pte_t *)(pdir + (i * PAGE_SIZE * PTBL_PAGES));

	/*
	 * Fill in PTEs covering kernel code and data. They are not required
	 * for address translation, as this area is covered by static TLB1
	 * entries, but for pte_vatopa() to work correctly with kernel area
	 * addresses.
	 */
	for (va = addr; va < data_end; va += PAGE_SIZE) {
		pte = &(kernel_pmap->pm_pdir[PDIR_IDX(va)][PTBL_IDX(va)]);
		*pte = PTE_RPN_FROM_PA(kernload + (va - kernstart));
		*pte |= PTE_M | PTE_SR | PTE_SW | PTE_SX | PTE_WIRED |
		    PTE_VALID | PTE_PS_4KB;
	}
}
#endif

/**************************************************************************/
/* PMAP related */
/**************************************************************************/

/*
 * This is called during booke_init, before the system is really initialized.
 */
static void
mmu_booke_bootstrap(mmu_t mmu, vm_offset_t start, vm_offset_t kernelend)
{
	vm_paddr_t phys_kernelend;
	struct mem_region *mp, *mp1;
	int cnt, i, j;
	vm_paddr_t s, e, sz;
	vm_paddr_t physsz, hwphyssz;
	u_int phys_avail_count;
	vm_size_t kstack0_sz;
	vm_offset_t kernel_pdir, kstack0;
	vm_paddr_t kstack0_phys;
	void *dpcpu;

	debugf("mmu_booke_bootstrap: entered\n");

	/* Set interesting system properties */
#ifdef __powerpc64__
	hw_direct_map = 1;
#else
	hw_direct_map = 0;
#endif
#if defined(COMPAT_FREEBSD32) || !defined(__powerpc64__)
	elf32_nxstack = 1;
#endif

	/* Initialize invalidation mutex */
	mtx_init(&tlbivax_mutex, "tlbivax", NULL, MTX_SPIN);

	/* Read TLB0 size and associativity. */
	tlb0_get_tlbconf();

	/*
	 * Align kernel start and end address (kernel image).
	 * Note that kernel end does not necessarily relate to kernsize.
	 * kernsize is the size of the kernel that is actually mapped.
	 */
	kernstart = trunc_page(start);
	data_start = round_page(kernelend);
	data_end = data_start;

	/* Allocate the dynamic per-cpu area. */
	dpcpu = (void *)data_end;
	data_end += DPCPU_SIZE;

	/* Allocate space for the message buffer. */
	msgbufp = (struct msgbuf *)data_end;
	data_end += msgbufsize;
	debugf(" msgbufp at 0x%"PRI0ptrX" end = 0x%"PRI0ptrX"\n",
	    (uintptr_t)msgbufp, data_end);

	data_end = round_page(data_end);

	/* Allocate space for ptbl_bufs. */
	ptbl_bufs = (struct ptbl_buf *)data_end;
	data_end += sizeof(struct ptbl_buf) * PTBL_BUFS;
	debugf(" ptbl_bufs at 0x%"PRI0ptrX" end = 0x%"PRI0ptrX"\n",
	    (uintptr_t)ptbl_bufs, data_end);

	data_end = round_page(data_end);

	/* Allocate PTE tables for kernel KVA. */
	kernel_pdir = data_end;
	kernel_ptbls = howmany(VM_MAX_KERNEL_ADDRESS - VM_MIN_KERNEL_ADDRESS,
	    PDIR_SIZE);
#ifdef __powerpc64__
	kernel_pdirs = howmany(kernel_ptbls, PDIR_NENTRIES);
	data_end += kernel_pdirs * PDIR_PAGES * PAGE_SIZE;
#endif
	data_end += kernel_ptbls * PTBL_PAGES * PAGE_SIZE;
	debugf(" kernel ptbls: %d\n", kernel_ptbls);
	debugf(" kernel pdir at 0x%"PRI0ptrX" end = 0x%"PRI0ptrX"\n",
	    kernel_pdir, data_end);

	debugf(" data_end: 0x%"PRI0ptrX"\n", data_end);
	if (data_end - kernstart > kernsize) {
		kernsize += tlb1_mapin_region(kernstart + kernsize,
		    kernload + kernsize, (data_end - kernstart) - kernsize);
	}
	data_end = kernstart + kernsize;
	debugf(" updated data_end: 0x%"PRI0ptrX"\n", data_end);

	/*
	 * Clear the structures - note we can only do it safely after the
	 * possible additional TLB1 translations are in place (above) so that
	 * all range up to the currently calculated 'data_end' is covered.
	 */
	dpcpu_init(dpcpu, 0);
	memset((void *)ptbl_bufs, 0, sizeof(struct ptbl_buf) * PTBL_SIZE);
#ifdef __powerpc64__
	memset((void *)kernel_pdir, 0,
	    kernel_pdirs * PDIR_PAGES * PAGE_SIZE +
	    kernel_ptbls * PTBL_PAGES * PAGE_SIZE);
#else
	memset((void *)kernel_pdir, 0, kernel_ptbls * PTBL_PAGES * PAGE_SIZE);
#endif

	/*******************************************************/
	/* Set the start and end of kva. */
	/*******************************************************/
	virtual_avail = round_page(data_end);
	virtual_end = VM_MAX_KERNEL_ADDRESS;

	/* Allocate KVA space for page zero/copy operations. */
	zero_page_va = virtual_avail;
	virtual_avail += PAGE_SIZE;
	copy_page_src_va = virtual_avail;
	virtual_avail += PAGE_SIZE;
	copy_page_dst_va = virtual_avail;
	virtual_avail += PAGE_SIZE;
	debugf("zero_page_va = 0x%"PRI0ptrX"\n", zero_page_va);
	debugf("copy_page_src_va = 0x%"PRI0ptrX"\n", copy_page_src_va);
	debugf("copy_page_dst_va = 0x%"PRI0ptrX"\n", copy_page_dst_va);

	/* Initialize page zero/copy mutexes. */
	mtx_init(&zero_page_mutex, "mmu_booke_zero_page", NULL, MTX_DEF);
	mtx_init(&copy_page_mutex, "mmu_booke_copy_page", NULL, MTX_DEF);

	/* Allocate KVA space for ptbl bufs. */
	ptbl_buf_pool_vabase = virtual_avail;
	virtual_avail += PTBL_BUFS * PTBL_PAGES * PAGE_SIZE;
	debugf("ptbl_buf_pool_vabase = 0x%"PRI0ptrX" end = 0x%"PRI0ptrX"\n",
	    ptbl_buf_pool_vabase, virtual_avail);

	/* Calculate corresponding physical addresses for the kernel region. */
	phys_kernelend = kernload + kernsize;
	debugf("kernel image and allocated data:\n");
	debugf(" kernload    = 0x%09llx\n", (uint64_t)kernload);
	debugf(" kernstart   = 0x%"PRI0ptrX"\n", kernstart);
	debugf(" kernsize    = 0x%"PRI0ptrX"\n", kernsize);

	if (sizeof(phys_avail) / sizeof(phys_avail[0]) < availmem_regions_sz)
		panic("mmu_booke_bootstrap: phys_avail too small");

	/*
	 * Remove kernel physical address range from avail regions list. Page
	 * align all regions.  Non-page aligned memory isn't very interesting
	 * to us.  Also, sort the entries for ascending addresses.
	 */

	/* Retrieve phys/avail mem regions */
	mem_regions(&physmem_regions, &physmem_regions_sz,
	    &availmem_regions, &availmem_regions_sz);
	sz = 0;
	cnt = availmem_regions_sz;
	debugf("processing avail regions:\n");
	for (mp = availmem_regions; mp->mr_size; mp++) {
		s = mp->mr_start;
		e = mp->mr_start + mp->mr_size;
		debugf(" %09jx-%09jx -> ", (uintmax_t)s, (uintmax_t)e);
		/* Check whether this region holds all of the kernel. */
		if (s < kernload && e > phys_kernelend) {
			availmem_regions[cnt].mr_start = phys_kernelend;
			availmem_regions[cnt++].mr_size = e - phys_kernelend;
			e = kernload;
		}
		/* Look whether this regions starts within the kernel. */
		if (s >= kernload && s < phys_kernelend) {
			if (e <= phys_kernelend)
				goto empty;
			s = phys_kernelend;
		}
		/* Now look whether this region ends within the kernel. */
		if (e > kernload && e <= phys_kernelend) {
			if (s >= kernload)
				goto empty;
			e = kernload;
		}
		/* Now page align the start and size of the region. */
		s = round_page(s);
		e = trunc_page(e);
		if (e < s)
			e = s;
		sz = e - s;
		debugf("%09jx-%09jx = %jx\n",
		    (uintmax_t)s, (uintmax_t)e, (uintmax_t)sz);

		/* Check whether some memory is left here. */
		if (sz == 0) {
		empty:
			memmove(mp, mp + 1,
			    (cnt - (mp - availmem_regions)) * sizeof(*mp));
			cnt--;
			mp--;
			continue;
		}

		/* Do an insertion sort. */
		for (mp1 = availmem_regions; mp1 < mp; mp1++)
			if (s < mp1->mr_start)
				break;
		if (mp1 < mp) {
			memmove(mp1 + 1, mp1, (char *)mp - (char *)mp1);
			mp1->mr_start = s;
			mp1->mr_size = sz;
		} else {
			mp->mr_start = s;
			mp->mr_size = sz;
		}
	}
	availmem_regions_sz = cnt;

	/*******************************************************/
	/* Steal physical memory for kernel stack from the end */
	/* of the first avail region                           */
	/*******************************************************/
	kstack0_sz = kstack_pages * PAGE_SIZE;
	kstack0_phys = availmem_regions[0].mr_start +
	    availmem_regions[0].mr_size;
	kstack0_phys -= kstack0_sz;
	availmem_regions[0].mr_size -= kstack0_sz;

	/*******************************************************/
	/* Fill in phys_avail table, based on availmem_regions */
	/*******************************************************/
	phys_avail_count = 0;
	physsz = 0;
	hwphyssz = 0;
	TUNABLE_ULONG_FETCH("hw.physmem", (u_long *) &hwphyssz);

	debugf("fill in phys_avail:\n");
	for (i = 0, j = 0; i < availmem_regions_sz; i++, j += 2) {

		debugf(" region: 0x%jx - 0x%jx (0x%jx)\n",
		    (uintmax_t)availmem_regions[i].mr_start,
		    (uintmax_t)availmem_regions[i].mr_start +
		        availmem_regions[i].mr_size,
		    (uintmax_t)availmem_regions[i].mr_size);

		if (hwphyssz != 0 &&
		    (physsz + availmem_regions[i].mr_size) >= hwphyssz) {
			debugf(" hw.physmem adjust\n");
			if (physsz < hwphyssz) {
				phys_avail[j] = availmem_regions[i].mr_start;
				phys_avail[j + 1] =
				    availmem_regions[i].mr_start +
				    hwphyssz - physsz;
				physsz = hwphyssz;
				phys_avail_count++;
			}
			break;
		}

		phys_avail[j] = availmem_regions[i].mr_start;
		phys_avail[j + 1] = availmem_regions[i].mr_start +
		    availmem_regions[i].mr_size;
		phys_avail_count++;
		physsz += availmem_regions[i].mr_size;
	}
	physmem = btoc(physsz);

	/* Calculate the last available physical address. */
	for (i = 0; phys_avail[i + 2] != 0; i += 2)
		;
	Maxmem = powerpc_btop(phys_avail[i + 1]);

	debugf("Maxmem = 0x%08lx\n", Maxmem);
	debugf("phys_avail_count = %d\n", phys_avail_count);
	debugf("physsz = 0x%09jx physmem = %jd (0x%09jx)\n",
	    (uintmax_t)physsz, (uintmax_t)physmem, (uintmax_t)physmem);

#ifdef __powerpc64__
	/*
	 * Map the physical memory contiguously in TLB1.
	 * Round so it fits into a single mapping.
	 */
	tlb1_mapin_region(DMAP_BASE_ADDRESS, 0,
	    phys_avail[i + 1]);
#endif

	/*******************************************************/
	/* Initialize (statically allocated) kernel pmap. */
	/*******************************************************/
	PMAP_LOCK_INIT(kernel_pmap);
#ifndef __powerpc64__
	kptbl_min = VM_MIN_KERNEL_ADDRESS / PDIR_SIZE;
#endif

	debugf("kernel_pmap = 0x%"PRI0ptrX"\n", (uintptr_t)kernel_pmap);
	kernel_pte_alloc(virtual_avail, kernstart, kernel_pdir);
	for (i = 0; i < MAXCPU; i++) {
		kernel_pmap->pm_tid[i] = TID_KERNEL;
		
		/* Initialize each CPU's tidbusy entry 0 with kernel_pmap */
		tidbusy[i][TID_KERNEL] = kernel_pmap;
	}

	/* Mark kernel_pmap active on all CPUs */
	CPU_FILL(&kernel_pmap->pm_active);

 	/*
	 * Initialize the global pv list lock.
	 */
	rw_init(&pvh_global_lock, "pmap pv global");

	/*******************************************************/
	/* Final setup */
	/*******************************************************/

	/* Enter kstack0 into kernel map, provide guard page */
	kstack0 = virtual_avail + KSTACK_GUARD_PAGES * PAGE_SIZE;
	thread0.td_kstack = kstack0;
	thread0.td_kstack_pages = kstack_pages;

	debugf("kstack_sz = 0x%08x\n", kstack0_sz);
	debugf("kstack0_phys at 0x%09llx - 0x%09llx\n",
	    kstack0_phys, kstack0_phys + kstack0_sz);
	debugf("kstack0 at 0x%"PRI0ptrX" - 0x%"PRI0ptrX"\n",
	    kstack0, kstack0 + kstack0_sz);
	
	virtual_avail += KSTACK_GUARD_PAGES * PAGE_SIZE + kstack0_sz;
	for (i = 0; i < kstack_pages; i++) {
		mmu_booke_kenter(mmu, kstack0, kstack0_phys);
		kstack0 += PAGE_SIZE;
		kstack0_phys += PAGE_SIZE;
	}

	pmap_bootstrapped = 1;
	
	debugf("virtual_avail = %"PRI0ptrX"\n", virtual_avail);
	debugf("virtual_end   = %"PRI0ptrX"\n", virtual_end);

	debugf("mmu_booke_bootstrap: exit\n");
}

#ifdef SMP
 void
tlb1_ap_prep(void)
{
	tlb_entry_t *e, tmp;
	unsigned int i;

	/* Prepare TLB1 image for AP processors */
	e = __boot_tlb1;
	for (i = 0; i < TLB1_ENTRIES; i++) {
		tlb1_read_entry(&tmp, i);

		if ((tmp.mas1 & MAS1_VALID) && (tmp.mas2 & _TLB_ENTRY_SHARED))
			memcpy(e++, &tmp, sizeof(tmp));
	}
}

void
pmap_bootstrap_ap(volatile uint32_t *trcp __unused)
{
	int i;

	/*
	 * Finish TLB1 configuration: the BSP already set up its TLB1 and we
	 * have the snapshot of its contents in the s/w __boot_tlb1[] table
	 * created by tlb1_ap_prep(), so use these values directly to
	 * (re)program AP's TLB1 hardware.
	 *
	 * Start at index 1 because index 0 has the kernel map.
	 */
	for (i = 1; i < TLB1_ENTRIES; i++) {
		if (__boot_tlb1[i].mas1 & MAS1_VALID)
			tlb1_write_entry(&__boot_tlb1[i], i);
	}

	set_mas4_defaults();
}
#endif

static void
booke_pmap_init_qpages(void)
{
	struct pcpu *pc;
	int i;

	CPU_FOREACH(i) {
		pc = pcpu_find(i);
		pc->pc_qmap_addr = kva_alloc(PAGE_SIZE);
		if (pc->pc_qmap_addr == 0)
			panic("pmap_init_qpages: unable to allocate KVA");
	}
}

SYSINIT(qpages_init, SI_SUB_CPU, SI_ORDER_ANY, booke_pmap_init_qpages, NULL);

/*
 * Get the physical page address for the given pmap/virtual address.
 */
static vm_paddr_t
mmu_booke_extract(mmu_t mmu, pmap_t pmap, vm_offset_t va)
{
	vm_paddr_t pa;

	PMAP_LOCK(pmap);
	pa = pte_vatopa(mmu, pmap, va);
	PMAP_UNLOCK(pmap);

	return (pa);
}

/*
 * Extract the physical page address associated with the given
 * kernel virtual address.
 */
static vm_paddr_t
mmu_booke_kextract(mmu_t mmu, vm_offset_t va)
{
	tlb_entry_t e;
	vm_paddr_t p = 0;
	int i;

	if (va >= VM_MIN_KERNEL_ADDRESS && va <= VM_MAX_KERNEL_ADDRESS)
		p = pte_vatopa(mmu, kernel_pmap, va);
	
	if (p == 0) {
		/* Check TLB1 mappings */
		for (i = 0; i < TLB1_ENTRIES; i++) {
			tlb1_read_entry(&e, i);
			if (!(e.mas1 & MAS1_VALID))
				continue;
			if (va >= e.virt && va < e.virt + e.size)
				return (e.phys + (va - e.virt));
		}
	}

	return (p);
}

/*
 * Initialize the pmap module.
 * Called by vm_init, to initialize any structures that the pmap
 * system needs to map virtual memory.
 */
static void
mmu_booke_init(mmu_t mmu)
{
	int shpgperproc = PMAP_SHPGPERPROC;

	/*
	 * Initialize the address space (zone) for the pv entries.  Set a
	 * high water mark so that the system can recover from excessive
	 * numbers of pv entries.
	 */
	pvzone = uma_zcreate("PV ENTRY", sizeof(struct pv_entry), NULL, NULL,
	    NULL, NULL, UMA_ALIGN_PTR, UMA_ZONE_VM | UMA_ZONE_NOFREE);

	TUNABLE_INT_FETCH("vm.pmap.shpgperproc", &shpgperproc);
	pv_entry_max = shpgperproc * maxproc + vm_cnt.v_page_count;

	TUNABLE_INT_FETCH("vm.pmap.pv_entries", &pv_entry_max);
	pv_entry_high_water = 9 * (pv_entry_max / 10);

	uma_zone_reserve_kva(pvzone, pv_entry_max);

	/* Pre-fill pvzone with initial number of pv entries. */
	uma_prealloc(pvzone, PV_ENTRY_ZONE_MIN);

	/* Initialize ptbl allocation. */
	ptbl_init();
}

/*
 * Map a list of wired pages into kernel virtual address space.  This is
 * intended for temporary mappings which do not need page modification or
 * references recorded.  Existing mappings in the region are overwritten.
 */
static void
mmu_booke_qenter(mmu_t mmu, vm_offset_t sva, vm_page_t *m, int count)
{
	vm_offset_t va;

	va = sva;
	while (count-- > 0) {
		mmu_booke_kenter(mmu, va, VM_PAGE_TO_PHYS(*m));
		va += PAGE_SIZE;
		m++;
	}
}

/*
 * Remove page mappings from kernel virtual address space.  Intended for
 * temporary mappings entered by mmu_booke_qenter.
 */
static void
mmu_booke_qremove(mmu_t mmu, vm_offset_t sva, int count)
{
	vm_offset_t va;

	va = sva;
	while (count-- > 0) {
		mmu_booke_kremove(mmu, va);
		va += PAGE_SIZE;
	}
}

/*
 * Map a wired page into kernel virtual address space.
 */
static void
mmu_booke_kenter(mmu_t mmu, vm_offset_t va, vm_paddr_t pa)
{

	mmu_booke_kenter_attr(mmu, va, pa, VM_MEMATTR_DEFAULT);
}

static void
mmu_booke_kenter_attr(mmu_t mmu, vm_offset_t va, vm_paddr_t pa, vm_memattr_t ma)
{
	uint32_t flags;
	pte_t *pte;

	KASSERT(((va >= VM_MIN_KERNEL_ADDRESS) &&
	    (va <= VM_MAX_KERNEL_ADDRESS)), ("mmu_booke_kenter: invalid va"));

	flags = PTE_SR | PTE_SW | PTE_SX | PTE_WIRED | PTE_VALID;
	flags |= tlb_calc_wimg(pa, ma) << PTE_MAS2_SHIFT;
	flags |= PTE_PS_4KB;

	pte = pte_find(mmu, kernel_pmap, va);
	KASSERT((pte != NULL), ("mmu_booke_kenter: invalid va.  NULL PTE"));

	mtx_lock_spin(&tlbivax_mutex);
	tlb_miss_lock();
	
	if (PTE_ISVALID(pte)) {
	
		CTR1(KTR_PMAP, "%s: replacing entry!", __func__);

		/* Flush entry from TLB0 */
		tlb0_flush_entry(va);
	}

	*pte = PTE_RPN_FROM_PA(pa) | flags;

	//debugf("mmu_booke_kenter: pdir_idx = %d ptbl_idx = %d va=0x%08x "
	//		"pa=0x%08x rpn=0x%08x flags=0x%08x\n",
	//		pdir_idx, ptbl_idx, va, pa, pte->rpn, pte->flags);

	/* Flush the real memory from the instruction cache. */
	if ((flags & (PTE_I | PTE_G)) == 0)
		__syncicache((void *)va, PAGE_SIZE);

	tlb_miss_unlock();
	mtx_unlock_spin(&tlbivax_mutex);
}

/*
 * Remove a page from kernel page table.
 */
static void
mmu_booke_kremove(mmu_t mmu, vm_offset_t va)
{
	pte_t *pte;

	CTR2(KTR_PMAP,"%s: s (va = 0x%"PRI0ptrX")\n", __func__, va);

	KASSERT(((va >= VM_MIN_KERNEL_ADDRESS) &&
	    (va <= VM_MAX_KERNEL_ADDRESS)),
	    ("mmu_booke_kremove: invalid va"));

	pte = pte_find(mmu, kernel_pmap, va);

	if (!PTE_ISVALID(pte)) {
	
		CTR1(KTR_PMAP, "%s: invalid pte", __func__);

		return;
	}

	mtx_lock_spin(&tlbivax_mutex);
	tlb_miss_lock();

	/* Invalidate entry in TLB0, update PTE. */
	tlb0_flush_entry(va);
	*pte = 0;

	tlb_miss_unlock();
	mtx_unlock_spin(&tlbivax_mutex);
}

/*
 * Provide a kernel pointer corresponding to a given userland pointer.
 * The returned pointer is valid until the next time this function is
 * called in this thread. This is used internally in copyin/copyout.
 */
int
mmu_booke_map_user_ptr(mmu_t mmu, pmap_t pm, volatile const void *uaddr,
    void **kaddr, size_t ulen, size_t *klen)
{

	if ((uintptr_t)uaddr + ulen > VM_MAXUSER_ADDRESS + PAGE_SIZE)
		return (EFAULT);

	*kaddr = (void *)(uintptr_t)uaddr;
	if (klen)
		*klen = ulen;

	return (0);
}

/*
 * Figure out where a given kernel pointer (usually in a fault) points
 * to from the VM's perspective, potentially remapping into userland's
 * address space.
 */
static int
mmu_booke_decode_kernel_ptr(mmu_t mmu, vm_offset_t addr, int *is_user,
    vm_offset_t *decoded_addr)
{

	if (addr < VM_MAXUSER_ADDRESS)
		*is_user = 1;
	else
		*is_user = 0;

	*decoded_addr = addr;
	return (0);
}

/*
 * Initialize pmap associated with process 0.
 */
static void
mmu_booke_pinit0(mmu_t mmu, pmap_t pmap)
{

	PMAP_LOCK_INIT(pmap);
	mmu_booke_pinit(mmu, pmap);
	PCPU_SET(curpmap, pmap);
}

/*
 * Initialize a preallocated and zeroed pmap structure,
 * such as one in a vmspace structure.
 */
static void
mmu_booke_pinit(mmu_t mmu, pmap_t pmap)
{
	int i;

	CTR4(KTR_PMAP, "%s: pmap = %p, proc %d '%s'", __func__, pmap,
	    curthread->td_proc->p_pid, curthread->td_proc->p_comm);

	KASSERT((pmap != kernel_pmap), ("pmap_pinit: initializing kernel_pmap"));

	for (i = 0; i < MAXCPU; i++)
		pmap->pm_tid[i] = TID_NONE;
	CPU_ZERO(&kernel_pmap->pm_active);
	bzero(&pmap->pm_stats, sizeof(pmap->pm_stats));
#ifdef __powerpc64__
	bzero(&pmap->pm_pp2d, sizeof(pte_t **) * PP2D_NENTRIES);
	TAILQ_INIT(&pmap->pm_pdir_list);
#else
	bzero(&pmap->pm_pdir, sizeof(pte_t *) * PDIR_NENTRIES);
#endif
	TAILQ_INIT(&pmap->pm_ptbl_list);
}

/*
 * Release any resources held by the given physical map.
 * Called when a pmap initialized by mmu_booke_pinit is being released.
 * Should only be called if the map contains no valid mappings.
 */
static void
mmu_booke_release(mmu_t mmu, pmap_t pmap)
{

	KASSERT(pmap->pm_stats.resident_count == 0,
	    ("pmap_release: pmap resident count %ld != 0",
	    pmap->pm_stats.resident_count));
}

/*
 * Insert the given physical page at the specified virtual address in the
 * target physical map with the protection requested. If specified the page
 * will be wired down.
 */
static int
mmu_booke_enter(mmu_t mmu, pmap_t pmap, vm_offset_t va, vm_page_t m,
    vm_prot_t prot, u_int flags, int8_t psind)
{
	int error;

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	error = mmu_booke_enter_locked(mmu, pmap, va, m, prot, flags, psind);
	PMAP_UNLOCK(pmap);
	rw_wunlock(&pvh_global_lock);
	return (error);
}

static int
mmu_booke_enter_locked(mmu_t mmu, pmap_t pmap, vm_offset_t va, vm_page_t m,
    vm_prot_t prot, u_int pmap_flags, int8_t psind __unused)
{
	pte_t *pte;
	vm_paddr_t pa;
	uint32_t flags;
	int error, su, sync;

	pa = VM_PAGE_TO_PHYS(m);
	su = (pmap == kernel_pmap);
	sync = 0;

	//debugf("mmu_booke_enter_locked: s (pmap=0x%08x su=%d tid=%d m=0x%08x va=0x%08x "
	//		"pa=0x%08x prot=0x%08x flags=%#x)\n",
	//		(u_int32_t)pmap, su, pmap->pm_tid,
	//		(u_int32_t)m, va, pa, prot, flags);

	if (su) {
		KASSERT(((va >= virtual_avail) &&
		    (va <= VM_MAX_KERNEL_ADDRESS)),
		    ("mmu_booke_enter_locked: kernel pmap, non kernel va"));
	} else {
		KASSERT((va <= VM_MAXUSER_ADDRESS),
		    ("mmu_booke_enter_locked: user pmap, non user va"));
	}
	if ((m->oflags & VPO_UNMANAGED) == 0 && !vm_page_xbusied(m))
		VM_OBJECT_ASSERT_LOCKED(m->object);

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	/*
	 * If there is an existing mapping, and the physical address has not
	 * changed, must be protection or wiring change.
	 */
	if (((pte = pte_find(mmu, pmap, va)) != NULL) &&
	    (PTE_ISVALID(pte)) && (PTE_PA(pte) == pa)) {
	    
		/*
		 * Before actually updating pte->flags we calculate and
		 * prepare its new value in a helper var.
		 */
		flags = *pte;
		flags &= ~(PTE_UW | PTE_UX | PTE_SW | PTE_SX | PTE_MODIFIED);

		/* Wiring change, just update stats. */
		if ((pmap_flags & PMAP_ENTER_WIRED) != 0) {
			if (!PTE_ISWIRED(pte)) {
				flags |= PTE_WIRED;
				pmap->pm_stats.wired_count++;
			}
		} else {
			if (PTE_ISWIRED(pte)) {
				flags &= ~PTE_WIRED;
				pmap->pm_stats.wired_count--;
			}
		}

		if (prot & VM_PROT_WRITE) {
			/* Add write permissions. */
			flags |= PTE_SW;
			if (!su)
				flags |= PTE_UW;

			if ((flags & PTE_MANAGED) != 0)
				vm_page_aflag_set(m, PGA_WRITEABLE);
		} else {
			/* Handle modified pages, sense modify status. */

			/*
			 * The PTE_MODIFIED flag could be set by underlying
			 * TLB misses since we last read it (above), possibly
			 * other CPUs could update it so we check in the PTE
			 * directly rather than rely on that saved local flags
			 * copy.
			 */
			if (PTE_ISMODIFIED(pte))
				vm_page_dirty(m);
		}

		if (prot & VM_PROT_EXECUTE) {
			flags |= PTE_SX;
			if (!su)
				flags |= PTE_UX;

			/*
			 * Check existing flags for execute permissions: if we
			 * are turning execute permissions on, icache should
			 * be flushed.
			 */
			if ((*pte & (PTE_UX | PTE_SX)) == 0)
				sync++;
		}

		flags &= ~PTE_REFERENCED;

		/*
		 * The new flags value is all calculated -- only now actually
		 * update the PTE.
		 */
		mtx_lock_spin(&tlbivax_mutex);
		tlb_miss_lock();

		tlb0_flush_entry(va);
		*pte &= ~PTE_FLAGS_MASK;
		*pte |= flags;

		tlb_miss_unlock();
		mtx_unlock_spin(&tlbivax_mutex);

	} else {
		/*
		 * If there is an existing mapping, but it's for a different
		 * physical address, pte_enter() will delete the old mapping.
		 */
		//if ((pte != NULL) && PTE_ISVALID(pte))
		//	debugf("mmu_booke_enter_locked: replace\n");
		//else
		//	debugf("mmu_booke_enter_locked: new\n");

		/* Now set up the flags and install the new mapping. */
		flags = (PTE_SR | PTE_VALID);
		flags |= PTE_M;

		if (!su)
			flags |= PTE_UR;

		if (prot & VM_PROT_WRITE) {
			flags |= PTE_SW;
			if (!su)
				flags |= PTE_UW;

			if ((m->oflags & VPO_UNMANAGED) == 0)
				vm_page_aflag_set(m, PGA_WRITEABLE);
		}

		if (prot & VM_PROT_EXECUTE) {
			flags |= PTE_SX;
			if (!su)
				flags |= PTE_UX;
		}

		/* If its wired update stats. */
		if ((pmap_flags & PMAP_ENTER_WIRED) != 0)
			flags |= PTE_WIRED;

		error = pte_enter(mmu, pmap, m, va, flags,
		    (pmap_flags & PMAP_ENTER_NOSLEEP) != 0);
		if (error != 0)
			return (KERN_RESOURCE_SHORTAGE);

		if ((flags & PMAP_ENTER_WIRED) != 0)
			pmap->pm_stats.wired_count++;

		/* Flush the real memory from the instruction cache. */
		if (prot & VM_PROT_EXECUTE)
			sync++;
	}

	if (sync && (su || pmap == PCPU_GET(curpmap))) {
		__syncicache((void *)va, PAGE_SIZE);
		sync = 0;
	}

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
mmu_booke_enter_object(mmu_t mmu, pmap_t pmap, vm_offset_t start,
    vm_offset_t end, vm_page_t m_start, vm_prot_t prot)
{
	vm_page_t m;
	vm_pindex_t diff, psize;

	VM_OBJECT_ASSERT_LOCKED(m_start->object);

	psize = atop(end - start);
	m = m_start;
	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	while (m != NULL && (diff = m->pindex - m_start->pindex) < psize) {
		mmu_booke_enter_locked(mmu, pmap, start + ptoa(diff), m,
		    prot & (VM_PROT_READ | VM_PROT_EXECUTE),
		    PMAP_ENTER_NOSLEEP, 0);
		m = TAILQ_NEXT(m, listq);
	}
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
}

static void
mmu_booke_enter_quick(mmu_t mmu, pmap_t pmap, vm_offset_t va, vm_page_t m,
    vm_prot_t prot)
{

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	mmu_booke_enter_locked(mmu, pmap, va, m,
	    prot & (VM_PROT_READ | VM_PROT_EXECUTE), PMAP_ENTER_NOSLEEP,
	    0);
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
}

/*
 * Remove the given range of addresses from the specified map.
 *
 * It is assumed that the start and end are properly rounded to the page size.
 */
static void
mmu_booke_remove(mmu_t mmu, pmap_t pmap, vm_offset_t va, vm_offset_t endva)
{
	pte_t *pte;
	uint8_t hold_flag;

	int su = (pmap == kernel_pmap);

	//debugf("mmu_booke_remove: s (su = %d pmap=0x%08x tid=%d va=0x%08x endva=0x%08x)\n",
	//		su, (u_int32_t)pmap, pmap->pm_tid, va, endva);

	if (su) {
		KASSERT(((va >= virtual_avail) &&
		    (va <= VM_MAX_KERNEL_ADDRESS)),
		    ("mmu_booke_remove: kernel pmap, non kernel va"));
	} else {
		KASSERT((va <= VM_MAXUSER_ADDRESS),
		    ("mmu_booke_remove: user pmap, non user va"));
	}

	if (PMAP_REMOVE_DONE(pmap)) {
		//debugf("mmu_booke_remove: e (empty)\n");
		return;
	}

	hold_flag = PTBL_HOLD_FLAG(pmap);
	//debugf("mmu_booke_remove: hold_flag = %d\n", hold_flag);

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	for (; va < endva; va += PAGE_SIZE) {
		pte = pte_find(mmu, pmap, va);
		if ((pte != NULL) && PTE_ISVALID(pte))
			pte_remove(mmu, pmap, va, hold_flag);
	}
	PMAP_UNLOCK(pmap);
	rw_wunlock(&pvh_global_lock);

	//debugf("mmu_booke_remove: e\n");
}

/*
 * Remove physical page from all pmaps in which it resides.
 */
static void
mmu_booke_remove_all(mmu_t mmu, vm_page_t m)
{
	pv_entry_t pv, pvn;
	uint8_t hold_flag;

	rw_wlock(&pvh_global_lock);
	for (pv = TAILQ_FIRST(&m->md.pv_list); pv != NULL; pv = pvn) {
		pvn = TAILQ_NEXT(pv, pv_link);

		PMAP_LOCK(pv->pv_pmap);
		hold_flag = PTBL_HOLD_FLAG(pv->pv_pmap);
		pte_remove(mmu, pv->pv_pmap, pv->pv_va, hold_flag);
		PMAP_UNLOCK(pv->pv_pmap);
	}
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	rw_wunlock(&pvh_global_lock);
}

/*
 * Map a range of physical addresses into kernel virtual address space.
 */
static vm_offset_t
mmu_booke_map(mmu_t mmu, vm_offset_t *virt, vm_paddr_t pa_start,
    vm_paddr_t pa_end, int prot)
{
	vm_offset_t sva = *virt;
	vm_offset_t va = sva;

	//debugf("mmu_booke_map: s (sva = 0x%08x pa_start = 0x%08x pa_end = 0x%08x)\n",
	//		sva, pa_start, pa_end);

	while (pa_start < pa_end) {
		mmu_booke_kenter(mmu, va, pa_start);
		va += PAGE_SIZE;
		pa_start += PAGE_SIZE;
	}
	*virt = va;

	//debugf("mmu_booke_map: e (va = 0x%08x)\n", va);
	return (sva);
}

/*
 * The pmap must be activated before it's address space can be accessed in any
 * way.
 */
static void
mmu_booke_activate(mmu_t mmu, struct thread *td)
{
	pmap_t pmap;
	u_int cpuid;

	pmap = &td->td_proc->p_vmspace->vm_pmap;

	CTR5(KTR_PMAP, "%s: s (td = %p, proc = '%s', id = %d, pmap = 0x%"PRI0ptrX")",
	    __func__, td, td->td_proc->p_comm, td->td_proc->p_pid, pmap);

	KASSERT((pmap != kernel_pmap), ("mmu_booke_activate: kernel_pmap!"));

	sched_pin();

	cpuid = PCPU_GET(cpuid);
	CPU_SET_ATOMIC(cpuid, &pmap->pm_active);
	PCPU_SET(curpmap, pmap);
	
	if (pmap->pm_tid[cpuid] == TID_NONE)
		tid_alloc(pmap);

	/* Load PID0 register with pmap tid value. */
	mtspr(SPR_PID0, pmap->pm_tid[cpuid]);
	__asm __volatile("isync");

	mtspr(SPR_DBCR0, td->td_pcb->pcb_cpu.booke.dbcr0);

	sched_unpin();

	CTR3(KTR_PMAP, "%s: e (tid = %d for '%s')", __func__,
	    pmap->pm_tid[PCPU_GET(cpuid)], td->td_proc->p_comm);
}

/*
 * Deactivate the specified process's address space.
 */
static void
mmu_booke_deactivate(mmu_t mmu, struct thread *td)
{
	pmap_t pmap;

	pmap = &td->td_proc->p_vmspace->vm_pmap;
	
	CTR5(KTR_PMAP, "%s: td=%p, proc = '%s', id = %d, pmap = 0x%"PRI0ptrX,
	    __func__, td, td->td_proc->p_comm, td->td_proc->p_pid, pmap);

	td->td_pcb->pcb_cpu.booke.dbcr0 = mfspr(SPR_DBCR0);

	CPU_CLR_ATOMIC(PCPU_GET(cpuid), &pmap->pm_active);
	PCPU_SET(curpmap, NULL);
}

/*
 * Copy the range specified by src_addr/len
 * from the source map to the range dst_addr/len
 * in the destination map.
 *
 * This routine is only advisory and need not do anything.
 */
static void
mmu_booke_copy(mmu_t mmu, pmap_t dst_pmap, pmap_t src_pmap,
    vm_offset_t dst_addr, vm_size_t len, vm_offset_t src_addr)
{

}

/*
 * Set the physical protection on the specified range of this map as requested.
 */
static void
mmu_booke_protect(mmu_t mmu, pmap_t pmap, vm_offset_t sva, vm_offset_t eva,
    vm_prot_t prot)
{
	vm_offset_t va;
	vm_page_t m;
	pte_t *pte;

	if ((prot & VM_PROT_READ) == VM_PROT_NONE) {
		mmu_booke_remove(mmu, pmap, sva, eva);
		return;
	}

	if (prot & VM_PROT_WRITE)
		return;

	PMAP_LOCK(pmap);
	for (va = sva; va < eva; va += PAGE_SIZE) {
		if ((pte = pte_find(mmu, pmap, va)) != NULL) {
			if (PTE_ISVALID(pte)) {
				m = PHYS_TO_VM_PAGE(PTE_PA(pte));

				mtx_lock_spin(&tlbivax_mutex);
				tlb_miss_lock();

				/* Handle modified pages. */
				if (PTE_ISMODIFIED(pte) && PTE_ISMANAGED(pte))
					vm_page_dirty(m);

				tlb0_flush_entry(va);
				*pte &= ~(PTE_UW | PTE_SW | PTE_MODIFIED);

				tlb_miss_unlock();
				mtx_unlock_spin(&tlbivax_mutex);
			}
		}
	}
	PMAP_UNLOCK(pmap);
}

/*
 * Clear the write and modified bits in each of the given page's mappings.
 */
static void
mmu_booke_remove_write(mmu_t mmu, vm_page_t m)
{
	pv_entry_t pv;
	pte_t *pte;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("mmu_booke_remove_write: page %p is not managed", m));

	/*
	 * If the page is not exclusive busied, then PGA_WRITEABLE cannot be
	 * set by another thread while the object is locked.  Thus,
	 * if PGA_WRITEABLE is clear, no page table entries need updating.
	 */
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (!vm_page_xbusied(m) && (m->aflags & PGA_WRITEABLE) == 0)
		return;
	rw_wlock(&pvh_global_lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_link) {
		PMAP_LOCK(pv->pv_pmap);
		if ((pte = pte_find(mmu, pv->pv_pmap, pv->pv_va)) != NULL) {
			if (PTE_ISVALID(pte)) {
				m = PHYS_TO_VM_PAGE(PTE_PA(pte));

				mtx_lock_spin(&tlbivax_mutex);
				tlb_miss_lock();

				/* Handle modified pages. */
				if (PTE_ISMODIFIED(pte))
					vm_page_dirty(m);

				/* Flush mapping from TLB0. */
				*pte &= ~(PTE_UW | PTE_SW | PTE_MODIFIED);

				tlb_miss_unlock();
				mtx_unlock_spin(&tlbivax_mutex);
			}
		}
		PMAP_UNLOCK(pv->pv_pmap);
	}
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	rw_wunlock(&pvh_global_lock);
}

static void
mmu_booke_sync_icache(mmu_t mmu, pmap_t pm, vm_offset_t va, vm_size_t sz)
{
	pte_t *pte;
	pmap_t pmap;
	vm_page_t m;
	vm_offset_t addr;
	vm_paddr_t pa = 0;
	int active, valid;
 
	va = trunc_page(va);
	sz = round_page(sz);

	rw_wlock(&pvh_global_lock);
	pmap = PCPU_GET(curpmap);
	active = (pm == kernel_pmap || pm == pmap) ? 1 : 0;
	while (sz > 0) {
		PMAP_LOCK(pm);
		pte = pte_find(mmu, pm, va);
		valid = (pte != NULL && PTE_ISVALID(pte)) ? 1 : 0;
		if (valid)
			pa = PTE_PA(pte);
		PMAP_UNLOCK(pm);
		if (valid) {
			if (!active) {
				/* Create a mapping in the active pmap. */
				addr = 0;
				m = PHYS_TO_VM_PAGE(pa);
				PMAP_LOCK(pmap);
				pte_enter(mmu, pmap, m, addr,
				    PTE_SR | PTE_VALID | PTE_UR, FALSE);
				__syncicache((void *)addr, PAGE_SIZE);
				pte_remove(mmu, pmap, addr, PTBL_UNHOLD);
				PMAP_UNLOCK(pmap);
			} else
				__syncicache((void *)va, PAGE_SIZE);
		}
		va += PAGE_SIZE;
		sz -= PAGE_SIZE;
	}
	rw_wunlock(&pvh_global_lock);
}

/*
 * Atomically extract and hold the physical page with the given
 * pmap and virtual address pair if that mapping permits the given
 * protection.
 */
static vm_page_t
mmu_booke_extract_and_hold(mmu_t mmu, pmap_t pmap, vm_offset_t va,
    vm_prot_t prot)
{
	pte_t *pte;
	vm_page_t m;
	uint32_t pte_wbit;
	vm_paddr_t pa;
	
	m = NULL;
	pa = 0;	
	PMAP_LOCK(pmap);
retry:
	pte = pte_find(mmu, pmap, va);
	if ((pte != NULL) && PTE_ISVALID(pte)) {
		if (pmap == kernel_pmap)
			pte_wbit = PTE_SW;
		else
			pte_wbit = PTE_UW;

		if ((*pte & pte_wbit) || ((prot & VM_PROT_WRITE) == 0)) {
			if (vm_page_pa_tryrelock(pmap, PTE_PA(pte), &pa))
				goto retry;
			m = PHYS_TO_VM_PAGE(PTE_PA(pte));
			vm_page_hold(m);
		}
	}

	PA_UNLOCK_COND(pa);
	PMAP_UNLOCK(pmap);
	return (m);
}

/*
 * Initialize a vm_page's machine-dependent fields.
 */
static void
mmu_booke_page_init(mmu_t mmu, vm_page_t m)
{

	m->md.pv_tracked = 0;
	TAILQ_INIT(&m->md.pv_list);
}

/*
 * mmu_booke_zero_page_area zeros the specified hardware page by
 * mapping it into virtual memory and using bzero to clear
 * its contents.
 *
 * off and size must reside within a single page.
 */
static void
mmu_booke_zero_page_area(mmu_t mmu, vm_page_t m, int off, int size)
{
	vm_offset_t va;

	/* XXX KASSERT off and size are within a single page? */

	if (hw_direct_map) {
		va = PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m));
		bzero((caddr_t)va + off, size);
	} else {
		mtx_lock(&zero_page_mutex);
		va = zero_page_va;

		mmu_booke_kenter(mmu, va, VM_PAGE_TO_PHYS(m));
		bzero((caddr_t)va + off, size);
		mmu_booke_kremove(mmu, va);

		mtx_unlock(&zero_page_mutex);
	}
}

/*
 * mmu_booke_zero_page zeros the specified hardware page.
 */
static void
mmu_booke_zero_page(mmu_t mmu, vm_page_t m)
{
	vm_offset_t off, va;

	if (hw_direct_map) {
		va = PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m));
	} else {
		va = zero_page_va;
		mtx_lock(&zero_page_mutex);

		mmu_booke_kenter(mmu, va, VM_PAGE_TO_PHYS(m));
	}

	for (off = 0; off < PAGE_SIZE; off += cacheline_size)
		__asm __volatile("dcbz 0,%0" :: "r"(va + off));

	if (!hw_direct_map) {
		mmu_booke_kremove(mmu, va);

		mtx_unlock(&zero_page_mutex);
	}
}

/*
 * mmu_booke_copy_page copies the specified (machine independent) page by
 * mapping the page into virtual memory and using memcopy to copy the page,
 * one machine dependent page at a time.
 */
static void
mmu_booke_copy_page(mmu_t mmu, vm_page_t sm, vm_page_t dm)
{
	vm_offset_t sva, dva;

	sva = copy_page_src_va;
	dva = copy_page_dst_va;

	if (hw_direct_map) {
		sva = PHYS_TO_DMAP(VM_PAGE_TO_PHYS(sm));
		dva = PHYS_TO_DMAP(VM_PAGE_TO_PHYS(dm));
	} else {
		mtx_lock(&copy_page_mutex);
		mmu_booke_kenter(mmu, sva, VM_PAGE_TO_PHYS(sm));
		mmu_booke_kenter(mmu, dva, VM_PAGE_TO_PHYS(dm));
	}
	memcpy((caddr_t)dva, (caddr_t)sva, PAGE_SIZE);
	if (!hw_direct_map) {
		mmu_booke_kremove(mmu, dva);
		mmu_booke_kremove(mmu, sva);
		mtx_unlock(&copy_page_mutex);
	}
}

static inline void
mmu_booke_copy_pages(mmu_t mmu, vm_page_t *ma, vm_offset_t a_offset,
    vm_page_t *mb, vm_offset_t b_offset, int xfersize)
{
	void *a_cp, *b_cp;
	vm_offset_t a_pg_offset, b_pg_offset;
	int cnt;

	if (hw_direct_map) {
		a_cp = (caddr_t)((uintptr_t)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(*ma)) +
		    a_offset);
		b_cp = (caddr_t)((uintptr_t)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(*mb)) +
		    b_offset);
		bcopy(a_cp, b_cp, xfersize);
	} else {
		mtx_lock(&copy_page_mutex);
		while (xfersize > 0) {
			a_pg_offset = a_offset & PAGE_MASK;
			cnt = min(xfersize, PAGE_SIZE - a_pg_offset);
			mmu_booke_kenter(mmu, copy_page_src_va,
			    VM_PAGE_TO_PHYS(ma[a_offset >> PAGE_SHIFT]));
			a_cp = (char *)copy_page_src_va + a_pg_offset;
			b_pg_offset = b_offset & PAGE_MASK;
			cnt = min(cnt, PAGE_SIZE - b_pg_offset);
			mmu_booke_kenter(mmu, copy_page_dst_va,
			    VM_PAGE_TO_PHYS(mb[b_offset >> PAGE_SHIFT]));
			b_cp = (char *)copy_page_dst_va + b_pg_offset;
			bcopy(a_cp, b_cp, cnt);
			mmu_booke_kremove(mmu, copy_page_dst_va);
			mmu_booke_kremove(mmu, copy_page_src_va);
			a_offset += cnt;
			b_offset += cnt;
			xfersize -= cnt;
		}
		mtx_unlock(&copy_page_mutex);
	}
}

static vm_offset_t
mmu_booke_quick_enter_page(mmu_t mmu, vm_page_t m)
{
	vm_paddr_t paddr;
	vm_offset_t qaddr;
	uint32_t flags;
	pte_t *pte;

	paddr = VM_PAGE_TO_PHYS(m);

	if (hw_direct_map)
		return (PHYS_TO_DMAP(paddr));

	flags = PTE_SR | PTE_SW | PTE_SX | PTE_WIRED | PTE_VALID;
	flags |= tlb_calc_wimg(paddr, pmap_page_get_memattr(m)) << PTE_MAS2_SHIFT;
	flags |= PTE_PS_4KB;

	critical_enter();
	qaddr = PCPU_GET(qmap_addr);

	pte = pte_find(mmu, kernel_pmap, qaddr);

	KASSERT(*pte == 0, ("mmu_booke_quick_enter_page: PTE busy"));

	/* 
	 * XXX: tlbivax is broadcast to other cores, but qaddr should
 	 * not be present in other TLBs.  Is there a better instruction
	 * sequence to use? Or just forget it & use mmu_booke_kenter()... 
	 */
	__asm __volatile("tlbivax 0, %0" :: "r"(qaddr & MAS2_EPN_MASK));
	__asm __volatile("isync; msync");

	*pte = PTE_RPN_FROM_PA(paddr) | flags;

	/* Flush the real memory from the instruction cache. */
	if ((flags & (PTE_I | PTE_G)) == 0)
		__syncicache((void *)qaddr, PAGE_SIZE);

	return (qaddr);
}

static void
mmu_booke_quick_remove_page(mmu_t mmu, vm_offset_t addr)
{
	pte_t *pte;

	if (hw_direct_map)
		return;

	pte = pte_find(mmu, kernel_pmap, addr);

	KASSERT(PCPU_GET(qmap_addr) == addr,
	    ("mmu_booke_quick_remove_page: invalid address"));
	KASSERT(*pte != 0,
	    ("mmu_booke_quick_remove_page: PTE not in use"));

	*pte = 0;
	critical_exit();
}

/*
 * Return whether or not the specified physical page was modified
 * in any of physical maps.
 */
static boolean_t
mmu_booke_is_modified(mmu_t mmu, vm_page_t m)
{
	pte_t *pte;
	pv_entry_t pv;
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("mmu_booke_is_modified: page %p is not managed", m));
	rv = FALSE;

	/*
	 * If the page is not exclusive busied, then PGA_WRITEABLE cannot be
	 * concurrently set while the object is locked.  Thus, if PGA_WRITEABLE
	 * is clear, no PTEs can be modified.
	 */
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (!vm_page_xbusied(m) && (m->aflags & PGA_WRITEABLE) == 0)
		return (rv);
	rw_wlock(&pvh_global_lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_link) {
		PMAP_LOCK(pv->pv_pmap);
		if ((pte = pte_find(mmu, pv->pv_pmap, pv->pv_va)) != NULL &&
		    PTE_ISVALID(pte)) {
			if (PTE_ISMODIFIED(pte))
				rv = TRUE;
		}
		PMAP_UNLOCK(pv->pv_pmap);
		if (rv)
			break;
	}
	rw_wunlock(&pvh_global_lock);
	return (rv);
}

/*
 * Return whether or not the specified virtual address is eligible
 * for prefault.
 */
static boolean_t
mmu_booke_is_prefaultable(mmu_t mmu, pmap_t pmap, vm_offset_t addr)
{

	return (FALSE);
}

/*
 * Return whether or not the specified physical page was referenced
 * in any physical maps.
 */
static boolean_t
mmu_booke_is_referenced(mmu_t mmu, vm_page_t m)
{
	pte_t *pte;
	pv_entry_t pv;
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("mmu_booke_is_referenced: page %p is not managed", m));
	rv = FALSE;
	rw_wlock(&pvh_global_lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_link) {
		PMAP_LOCK(pv->pv_pmap);
		if ((pte = pte_find(mmu, pv->pv_pmap, pv->pv_va)) != NULL &&
		    PTE_ISVALID(pte)) {
			if (PTE_ISREFERENCED(pte))
				rv = TRUE;
		}
		PMAP_UNLOCK(pv->pv_pmap);
		if (rv)
			break;
	}
	rw_wunlock(&pvh_global_lock);
	return (rv);
}

/*
 * Clear the modify bits on the specified physical page.
 */
static void
mmu_booke_clear_modify(mmu_t mmu, vm_page_t m)
{
	pte_t *pte;
	pv_entry_t pv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("mmu_booke_clear_modify: page %p is not managed", m));
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	KASSERT(!vm_page_xbusied(m),
	    ("mmu_booke_clear_modify: page %p is exclusive busied", m));

	/*
	 * If the page is not PG_AWRITEABLE, then no PTEs can be modified.
	 * If the object containing the page is locked and the page is not
	 * exclusive busied, then PG_AWRITEABLE cannot be concurrently set.
	 */
	if ((m->aflags & PGA_WRITEABLE) == 0)
		return;
	rw_wlock(&pvh_global_lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_link) {
		PMAP_LOCK(pv->pv_pmap);
		if ((pte = pte_find(mmu, pv->pv_pmap, pv->pv_va)) != NULL &&
		    PTE_ISVALID(pte)) {
			mtx_lock_spin(&tlbivax_mutex);
			tlb_miss_lock();
			
			if (*pte & (PTE_SW | PTE_UW | PTE_MODIFIED)) {
				tlb0_flush_entry(pv->pv_va);
				*pte &= ~(PTE_SW | PTE_UW | PTE_MODIFIED |
				    PTE_REFERENCED);
			}

			tlb_miss_unlock();
			mtx_unlock_spin(&tlbivax_mutex);
		}
		PMAP_UNLOCK(pv->pv_pmap);
	}
	rw_wunlock(&pvh_global_lock);
}

/*
 * Return a count of reference bits for a page, clearing those bits.
 * It is not necessary for every reference bit to be cleared, but it
 * is necessary that 0 only be returned when there are truly no
 * reference bits set.
 *
 * As an optimization, update the page's dirty field if a modified bit is
 * found while counting reference bits.  This opportunistic update can be
 * performed at low cost and can eliminate the need for some future calls
 * to pmap_is_modified().  However, since this function stops after
 * finding PMAP_TS_REFERENCED_MAX reference bits, it may not detect some
 * dirty pages.  Those dirty pages will only be detected by a future call
 * to pmap_is_modified().
 */
static int
mmu_booke_ts_referenced(mmu_t mmu, vm_page_t m)
{
	pte_t *pte;
	pv_entry_t pv;
	int count;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("mmu_booke_ts_referenced: page %p is not managed", m));
	count = 0;
	rw_wlock(&pvh_global_lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_link) {
		PMAP_LOCK(pv->pv_pmap);
		if ((pte = pte_find(mmu, pv->pv_pmap, pv->pv_va)) != NULL &&
		    PTE_ISVALID(pte)) {
			if (PTE_ISMODIFIED(pte))
				vm_page_dirty(m);
			if (PTE_ISREFERENCED(pte)) {
				mtx_lock_spin(&tlbivax_mutex);
				tlb_miss_lock();

				tlb0_flush_entry(pv->pv_va);
				*pte &= ~PTE_REFERENCED;

				tlb_miss_unlock();
				mtx_unlock_spin(&tlbivax_mutex);

				if (++count >= PMAP_TS_REFERENCED_MAX) {
					PMAP_UNLOCK(pv->pv_pmap);
					break;
				}
			}
		}
		PMAP_UNLOCK(pv->pv_pmap);
	}
	rw_wunlock(&pvh_global_lock);
	return (count);
}

/*
 * Clear the wired attribute from the mappings for the specified range of
 * addresses in the given pmap.  Every valid mapping within that range must
 * have the wired attribute set.  In contrast, invalid mappings cannot have
 * the wired attribute set, so they are ignored.
 *
 * The wired attribute of the page table entry is not a hardware feature, so
 * there is no need to invalidate any TLB entries.
 */
static void
mmu_booke_unwire(mmu_t mmu, pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	vm_offset_t va;
	pte_t *pte;

	PMAP_LOCK(pmap);
	for (va = sva; va < eva; va += PAGE_SIZE) {
		if ((pte = pte_find(mmu, pmap, va)) != NULL &&
		    PTE_ISVALID(pte)) {
			if (!PTE_ISWIRED(pte))
				panic("mmu_booke_unwire: pte %p isn't wired",
				    pte);
			*pte &= ~PTE_WIRED;
			pmap->pm_stats.wired_count--;
		}
	}
	PMAP_UNLOCK(pmap);

}

/*
 * Return true if the pmap's pv is one of the first 16 pvs linked to from this
 * page.  This count may be changed upwards or downwards in the future; it is
 * only necessary that true be returned for a small subset of pmaps for proper
 * page aging.
 */
static boolean_t
mmu_booke_page_exists_quick(mmu_t mmu, pmap_t pmap, vm_page_t m)
{
	pv_entry_t pv;
	int loops;
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("mmu_booke_page_exists_quick: page %p is not managed", m));
	loops = 0;
	rv = FALSE;
	rw_wlock(&pvh_global_lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_link) {
		if (pv->pv_pmap == pmap) {
			rv = TRUE;
			break;
		}
		if (++loops >= 16)
			break;
	}
	rw_wunlock(&pvh_global_lock);
	return (rv);
}

/*
 * Return the number of managed mappings to the given physical page that are
 * wired.
 */
static int
mmu_booke_page_wired_mappings(mmu_t mmu, vm_page_t m)
{
	pv_entry_t pv;
	pte_t *pte;
	int count = 0;

	if ((m->oflags & VPO_UNMANAGED) != 0)
		return (count);
	rw_wlock(&pvh_global_lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_link) {
		PMAP_LOCK(pv->pv_pmap);
		if ((pte = pte_find(mmu, pv->pv_pmap, pv->pv_va)) != NULL)
			if (PTE_ISVALID(pte) && PTE_ISWIRED(pte))
				count++;
		PMAP_UNLOCK(pv->pv_pmap);
	}
	rw_wunlock(&pvh_global_lock);
	return (count);
}

static int
mmu_booke_dev_direct_mapped(mmu_t mmu, vm_paddr_t pa, vm_size_t size)
{
	int i;
	vm_offset_t va;

	/*
	 * This currently does not work for entries that
	 * overlap TLB1 entries.
	 */
	for (i = 0; i < TLB1_ENTRIES; i ++) {
		if (tlb1_iomapped(i, pa, size, &va) == 0)
			return (0);
	}

	return (EFAULT);
}

void
mmu_booke_dumpsys_map(mmu_t mmu, vm_paddr_t pa, size_t sz, void **va)
{
	vm_paddr_t ppa;
	vm_offset_t ofs;
	vm_size_t gran;

	/* Minidumps are based on virtual memory addresses. */
	if (do_minidump) {
		*va = (void *)(vm_offset_t)pa;
		return;
	}

	/* Raw physical memory dumps don't have a virtual address. */
	/* We always map a 256MB page at 256M. */
	gran = 256 * 1024 * 1024;
	ppa = rounddown2(pa, gran);
	ofs = pa - ppa;
	*va = (void *)gran;
	tlb1_set_entry((vm_offset_t)va, ppa, gran, _TLB_ENTRY_IO);

	if (sz > (gran - ofs))
		tlb1_set_entry((vm_offset_t)(va + gran), ppa + gran, gran,
		    _TLB_ENTRY_IO);
}

void
mmu_booke_dumpsys_unmap(mmu_t mmu, vm_paddr_t pa, size_t sz, void *va)
{
	vm_paddr_t ppa;
	vm_offset_t ofs;
	vm_size_t gran;
	tlb_entry_t e;
	int i;

	/* Minidumps are based on virtual memory addresses. */
	/* Nothing to do... */
	if (do_minidump)
		return;

	for (i = 0; i < TLB1_ENTRIES; i++) {
		tlb1_read_entry(&e, i);
		if (!(e.mas1 & MAS1_VALID))
			break;
	}

	/* Raw physical memory dumps don't have a virtual address. */
	i--;
	e.mas1 = 0;
	e.mas2 = 0;
	e.mas3 = 0;
	tlb1_write_entry(&e, i);

	gran = 256 * 1024 * 1024;
	ppa = rounddown2(pa, gran);
	ofs = pa - ppa;
	if (sz > (gran - ofs)) {
		i--;
		e.mas1 = 0;
		e.mas2 = 0;
		e.mas3 = 0;
		tlb1_write_entry(&e, i);
	}
}

extern struct dump_pa dump_map[PHYS_AVAIL_SZ + 1];

void
mmu_booke_scan_init(mmu_t mmu)
{
	vm_offset_t va;
	pte_t *pte;
	int i;

	if (!do_minidump) {
		/* Initialize phys. segments for dumpsys(). */
		memset(&dump_map, 0, sizeof(dump_map));
		mem_regions(&physmem_regions, &physmem_regions_sz, &availmem_regions,
		    &availmem_regions_sz);
		for (i = 0; i < physmem_regions_sz; i++) {
			dump_map[i].pa_start = physmem_regions[i].mr_start;
			dump_map[i].pa_size = physmem_regions[i].mr_size;
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
	dump_map[1].pa_start = data_start;
	dump_map[1].pa_size = data_end - data_start;

	/* 3rd: kernel VM. */
	va = dump_map[1].pa_start + dump_map[1].pa_size;
	/* Find start of next chunk (from va). */
	while (va < virtual_end) {
		/* Don't dump the buffer cache. */
		if (va >= kmi.buffer_sva && va < kmi.buffer_eva) {
			va = kmi.buffer_eva;
			continue;
		}
		pte = pte_find(mmu, kernel_pmap, va);
		if (pte != NULL && PTE_ISVALID(pte))
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
			pte = pte_find(mmu, kernel_pmap, va);
			if (pte == NULL || !PTE_ISVALID(pte))
				break;
			va += PAGE_SIZE;
		}
		dump_map[2].pa_size = va - dump_map[2].pa_start;
	}
}

/*
 * Map a set of physical memory pages into the kernel virtual address space.
 * Return a pointer to where it is mapped. This routine is intended to be used
 * for mapping device memory, NOT real memory.
 */
static void *
mmu_booke_mapdev(mmu_t mmu, vm_paddr_t pa, vm_size_t size)
{

	return (mmu_booke_mapdev_attr(mmu, pa, size, VM_MEMATTR_DEFAULT));
}

static void *
mmu_booke_mapdev_attr(mmu_t mmu, vm_paddr_t pa, vm_size_t size, vm_memattr_t ma)
{
	tlb_entry_t e;
	void *res;
	uintptr_t va, tmpva;
	vm_size_t sz;
	int i;

	/*
	 * Check if this is premapped in TLB1. Note: this should probably also
	 * check whether a sequence of TLB1 entries exist that match the
	 * requirement, but now only checks the easy case.
	 */
	for (i = 0; i < TLB1_ENTRIES; i++) {
		tlb1_read_entry(&e, i);
		if (!(e.mas1 & MAS1_VALID))
			continue;
		if (pa >= e.phys &&
		    (pa + size) <= (e.phys + e.size) &&
		    (ma == VM_MEMATTR_DEFAULT ||
		     tlb_calc_wimg(pa, ma) ==
		      (e.mas2 & (MAS2_WIMGE_MASK & ~_TLB_ENTRY_SHARED))))
			return (void *)(e.virt +
			    (vm_offset_t)(pa - e.phys));
	}

	size = roundup(size, PAGE_SIZE);

	/*
	 * The device mapping area is between VM_MAXUSER_ADDRESS and
	 * VM_MIN_KERNEL_ADDRESS.  This gives 1GB of device addressing.
	 */
#ifdef SPARSE_MAPDEV
	/*
	 * With a sparse mapdev, align to the largest starting region.  This
	 * could feasibly be optimized for a 'best-fit' alignment, but that
	 * calculation could be very costly.
	 * Align to the smaller of:
	 * - first set bit in overlap of (pa & size mask)
	 * - largest size envelope
	 *
	 * It's possible the device mapping may start at a PA that's not larger
	 * than the size mask, so we need to offset in to maximize the TLB entry
	 * range and minimize the number of used TLB entries.
	 */
	do {
	    tmpva = tlb1_map_base;
	    sz = ffsl(((1 << flsl(size-1)) - 1) & pa);
	    sz = sz ? min(roundup(sz + 3, 4), flsl(size) - 1) : flsl(size) - 1;
	    va = roundup(tlb1_map_base, 1 << sz) | (((1 << sz) - 1) & pa);
#ifdef __powerpc64__
	} while (!atomic_cmpset_long(&tlb1_map_base, tmpva, va + size));
#else
	} while (!atomic_cmpset_int(&tlb1_map_base, tmpva, va + size));
#endif
#else
#ifdef __powerpc64__
	va = atomic_fetchadd_long(&tlb1_map_base, size);
#else
	va = atomic_fetchadd_int(&tlb1_map_base, size);
#endif
#endif
	res = (void *)va;

	do {
		sz = 1 << (ilog2(size) & ~1);
		/* Align size to PA */
		if (pa % sz != 0) {
			do {
				sz >>= 2;
			} while (pa % sz != 0);
		}
		/* Now align from there to VA */
		if (va % sz != 0) {
			do {
				sz >>= 2;
			} while (va % sz != 0);
		}
		if (bootverbose)
			printf("Wiring VA=%lx to PA=%jx (size=%lx)\n",
			    va, (uintmax_t)pa, sz);
		if (tlb1_set_entry(va, pa, sz,
		    _TLB_ENTRY_SHARED | tlb_calc_wimg(pa, ma)) < 0)
			return (NULL);
		size -= sz;
		pa += sz;
		va += sz;
	} while (size > 0);

	return (res);
}

/*
 * 'Unmap' a range mapped by mmu_booke_mapdev().
 */
static void
mmu_booke_unmapdev(mmu_t mmu, vm_offset_t va, vm_size_t size)
{
#ifdef SUPPORTS_SHRINKING_TLB1
	vm_offset_t base, offset;

	/*
	 * Unmap only if this is inside kernel virtual space.
	 */
	if ((va >= VM_MIN_KERNEL_ADDRESS) && (va <= VM_MAX_KERNEL_ADDRESS)) {
		base = trunc_page(va);
		offset = va & PAGE_MASK;
		size = roundup(offset + size, PAGE_SIZE);
		kva_free(base, size);
	}
#endif
}

/*
 * mmu_booke_object_init_pt preloads the ptes for a given object into the
 * specified pmap. This eliminates the blast of soft faults on process startup
 * and immediately after an mmap.
 */
static void
mmu_booke_object_init_pt(mmu_t mmu, pmap_t pmap, vm_offset_t addr,
    vm_object_t object, vm_pindex_t pindex, vm_size_t size)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(object->type == OBJT_DEVICE || object->type == OBJT_SG,
	    ("mmu_booke_object_init_pt: non-device object"));
}

/*
 * Perform the pmap work for mincore.
 */
static int
mmu_booke_mincore(mmu_t mmu, pmap_t pmap, vm_offset_t addr,
    vm_paddr_t *locked_pa)
{

	/* XXX: this should be implemented at some point */
	return (0);
}

static int
mmu_booke_change_attr(mmu_t mmu, vm_offset_t addr, vm_size_t sz,
    vm_memattr_t mode)
{
	vm_offset_t va;
	pte_t *pte;
	int i, j;
	tlb_entry_t e;

	/* Check TLB1 mappings */
	for (i = 0; i < TLB1_ENTRIES; i++) {
		tlb1_read_entry(&e, i);
		if (!(e.mas1 & MAS1_VALID))
			continue;
		if (addr >= e.virt && addr < e.virt + e.size)
			break;
	}
	if (i < TLB1_ENTRIES) {
		/* Only allow full mappings to be modified for now. */
		/* Validate the range. */
		for (j = i, va = addr; va < addr + sz; va += e.size, j++) {
			tlb1_read_entry(&e, j);
			if (va != e.virt || (sz - (va - addr) < e.size))
				return (EINVAL);
		}
		for (va = addr; va < addr + sz; va += e.size, i++) {
			tlb1_read_entry(&e, i);
			e.mas2 &= ~MAS2_WIMGE_MASK;
			e.mas2 |= tlb_calc_wimg(e.phys, mode);

			/*
			 * Write it out to the TLB.  Should really re-sync with other
			 * cores.
			 */
			tlb1_write_entry(&e, i);
		}
		return (0);
	}

	/* Not in TLB1, try through pmap */
	/* First validate the range. */
	for (va = addr; va < addr + sz; va += PAGE_SIZE) {
		pte = pte_find(mmu, kernel_pmap, va);
		if (pte == NULL || !PTE_ISVALID(pte))
			return (EINVAL);
	}

	mtx_lock_spin(&tlbivax_mutex);
	tlb_miss_lock();
	for (va = addr; va < addr + sz; va += PAGE_SIZE) {
		pte = pte_find(mmu, kernel_pmap, va);
		*pte &= ~(PTE_MAS2_MASK << PTE_MAS2_SHIFT);
		*pte |= tlb_calc_wimg(PTE_PA(pte), mode) << PTE_MAS2_SHIFT;
		tlb0_flush_entry(va);
	}
	tlb_miss_unlock();
	mtx_unlock_spin(&tlbivax_mutex);

	return (0);
}

/**************************************************************************/
/* TID handling */
/**************************************************************************/

/*
 * Allocate a TID. If necessary, steal one from someone else.
 * The new TID is flushed from the TLB before returning.
 */
static tlbtid_t
tid_alloc(pmap_t pmap)
{
	tlbtid_t tid;
	int thiscpu;

	KASSERT((pmap != kernel_pmap), ("tid_alloc: kernel pmap"));

	CTR2(KTR_PMAP, "%s: s (pmap = %p)", __func__, pmap);

	thiscpu = PCPU_GET(cpuid);

	tid = PCPU_GET(booke.tid_next);
	if (tid > TID_MAX)
		tid = TID_MIN;
	PCPU_SET(booke.tid_next, tid + 1);

	/* If we are stealing TID then clear the relevant pmap's field */
	if (tidbusy[thiscpu][tid] != NULL) {

		CTR2(KTR_PMAP, "%s: warning: stealing tid %d", __func__, tid);
		
		tidbusy[thiscpu][tid]->pm_tid[thiscpu] = TID_NONE;

		/* Flush all entries from TLB0 matching this TID. */
		tid_flush(tid);
	}

	tidbusy[thiscpu][tid] = pmap;
	pmap->pm_tid[thiscpu] = tid;
	__asm __volatile("msync; isync");

	CTR3(KTR_PMAP, "%s: e (%02d next = %02d)", __func__, tid,
	    PCPU_GET(booke.tid_next));

	return (tid);
}

/**************************************************************************/
/* TLB0 handling */
/**************************************************************************/

/* Convert TLB0 va and way number to tlb0[] table index. */
static inline unsigned int
tlb0_tableidx(vm_offset_t va, unsigned int way)
{
	unsigned int idx;

	idx = (way * TLB0_ENTRIES_PER_WAY);
	idx += (va & MAS2_TLB0_ENTRY_IDX_MASK) >> MAS2_TLB0_ENTRY_IDX_SHIFT;
	return (idx);
}

/*
 * Invalidate TLB0 entry.
 */
static inline void
tlb0_flush_entry(vm_offset_t va)
{

	CTR2(KTR_PMAP, "%s: s va=0x%08x", __func__, va);

	mtx_assert(&tlbivax_mutex, MA_OWNED);

	__asm __volatile("tlbivax 0, %0" :: "r"(va & MAS2_EPN_MASK));
	__asm __volatile("isync; msync");
	__asm __volatile("tlbsync; msync");

	CTR1(KTR_PMAP, "%s: e", __func__);
}


/**************************************************************************/
/* TLB1 handling */
/**************************************************************************/

/*
 * TLB1 mapping notes:
 *
 * TLB1[0]	Kernel text and data.
 * TLB1[1-15]	Additional kernel text and data mappings (if required), PCI
 *		windows, other devices mappings.
 */

 /*
 * Read an entry from given TLB1 slot.
 */
void
tlb1_read_entry(tlb_entry_t *entry, unsigned int slot)
{
	register_t msr;
	uint32_t mas0;

	KASSERT((entry != NULL), ("%s(): Entry is NULL!", __func__));

	msr = mfmsr();
	__asm __volatile("wrteei 0");

	mas0 = MAS0_TLBSEL(1) | MAS0_ESEL(slot);
	mtspr(SPR_MAS0, mas0);
	__asm __volatile("isync; tlbre");

	entry->mas1 = mfspr(SPR_MAS1);
	entry->mas2 = mfspr(SPR_MAS2);
	entry->mas3 = mfspr(SPR_MAS3);

	switch ((mfpvr() >> 16) & 0xFFFF) {
	case FSL_E500v2:
	case FSL_E500mc:
	case FSL_E5500:
	case FSL_E6500:
		entry->mas7 = mfspr(SPR_MAS7);
		break;
	default:
		entry->mas7 = 0;
		break;
	}
	mtmsr(msr);

	entry->virt = entry->mas2 & MAS2_EPN_MASK;
	entry->phys = ((vm_paddr_t)(entry->mas7 & MAS7_RPN) << 32) |
	    (entry->mas3 & MAS3_RPN);
	entry->size =
	    tsize2size((entry->mas1 & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT);
}

struct tlbwrite_args {
	tlb_entry_t *e;
	unsigned int idx;
};

static void
tlb1_write_entry_int(void *arg)
{
	struct tlbwrite_args *args = arg;
	uint32_t mas0;

	/* Select entry */
	mas0 = MAS0_TLBSEL(1) | MAS0_ESEL(args->idx);

	mtspr(SPR_MAS0, mas0);
	mtspr(SPR_MAS1, args->e->mas1);
	mtspr(SPR_MAS2, args->e->mas2);
	mtspr(SPR_MAS3, args->e->mas3);
	switch ((mfpvr() >> 16) & 0xFFFF) {
	case FSL_E500mc:
	case FSL_E5500:
	case FSL_E6500:
		mtspr(SPR_MAS8, 0);
		/* FALLTHROUGH */
	case FSL_E500v2:
		mtspr(SPR_MAS7, args->e->mas7);
		break;
	default:
		break;
	}

	__asm __volatile("isync; tlbwe; isync; msync");

}

static void
tlb1_write_entry_sync(void *arg)
{
	/* Empty synchronization point for smp_rendezvous(). */
}

/*
 * Write given entry to TLB1 hardware.
 */
static void
tlb1_write_entry(tlb_entry_t *e, unsigned int idx)
{
	struct tlbwrite_args args;

	args.e = e;
	args.idx = idx;

#ifdef SMP
	if ((e->mas2 & _TLB_ENTRY_SHARED) && smp_started) {
		mb();
		smp_rendezvous(tlb1_write_entry_sync,
		    tlb1_write_entry_int,
		    tlb1_write_entry_sync, &args);
	} else
#endif
	{
		register_t msr;

		msr = mfmsr();
		__asm __volatile("wrteei 0");
		tlb1_write_entry_int(&args);
		mtmsr(msr);
	}
}

/*
 * Return the largest uint value log such that 2^log <= num.
 */
static unsigned int
ilog2(unsigned long num)
{
	long lz;

#ifdef __powerpc64__
	__asm ("cntlzd %0, %1" : "=r" (lz) : "r" (num));
	return (63 - lz);
#else
	__asm ("cntlzw %0, %1" : "=r" (lz) : "r" (num));
	return (31 - lz);
#endif
}

/*
 * Convert TLB TSIZE value to mapped region size.
 */
static vm_size_t
tsize2size(unsigned int tsize)
{

	/*
	 * size = 4^tsize KB
	 * size = 4^tsize * 2^10 = 2^(2 * tsize - 10)
	 */

	return ((1 << (2 * tsize)) * 1024);
}

/*
 * Convert region size (must be power of 4) to TLB TSIZE value.
 */
static unsigned int
size2tsize(vm_size_t size)
{

	return (ilog2(size) / 2 - 5);
}

/*
 * Register permanent kernel mapping in TLB1.
 *
 * Entries are created starting from index 0 (current free entry is
 * kept in tlb1_idx) and are not supposed to be invalidated.
 */
int
tlb1_set_entry(vm_offset_t va, vm_paddr_t pa, vm_size_t size,
    uint32_t flags)
{
	tlb_entry_t e;
	uint32_t ts, tid;
	int tsize, index;

	for (index = 0; index < TLB1_ENTRIES; index++) {
		tlb1_read_entry(&e, index);
		if ((e.mas1 & MAS1_VALID) == 0)
			break;
		/* Check if we're just updating the flags, and update them. */
		if (e.phys == pa && e.virt == va && e.size == size) {
			e.mas2 = (va & MAS2_EPN_MASK) | flags;
			tlb1_write_entry(&e, index);
			return (0);
		}
	}
	if (index >= TLB1_ENTRIES) {
		printf("tlb1_set_entry: TLB1 full!\n");
		return (-1);
	}

	/* Convert size to TSIZE */
	tsize = size2tsize(size);

	tid = (TID_KERNEL << MAS1_TID_SHIFT) & MAS1_TID_MASK;
	/* XXX TS is hard coded to 0 for now as we only use single address space */
	ts = (0 << MAS1_TS_SHIFT) & MAS1_TS_MASK;

	e.phys = pa;
	e.virt = va;
	e.size = size;
	e.mas1 = MAS1_VALID | MAS1_IPROT | ts | tid;
	e.mas1 |= ((tsize << MAS1_TSIZE_SHIFT) & MAS1_TSIZE_MASK);
	e.mas2 = (va & MAS2_EPN_MASK) | flags;

	/* Set supervisor RWX permission bits */
	e.mas3 = (pa & MAS3_RPN) | MAS3_SR | MAS3_SW | MAS3_SX;
	e.mas7 = (pa >> 32) & MAS7_RPN;

	tlb1_write_entry(&e, index);

	/*
	 * XXX in general TLB1 updates should be propagated between CPUs,
	 * since current design assumes to have the same TLB1 set-up on all
	 * cores.
	 */
	return (0);
}

/*
 * Map in contiguous RAM region into the TLB1 using maximum of
 * KERNEL_REGION_MAX_TLB_ENTRIES entries.
 *
 * If necessary round up last entry size and return total size
 * used by all allocated entries.
 */
vm_size_t
tlb1_mapin_region(vm_offset_t va, vm_paddr_t pa, vm_size_t size)
{
	vm_size_t pgs[KERNEL_REGION_MAX_TLB_ENTRIES];
	vm_size_t mapped, pgsz, base, mask;
	int idx, nents;

	/* Round up to the next 1M */
	size = roundup2(size, 1 << 20);

	mapped = 0;
	idx = 0;
	base = va;
	pgsz = 64*1024*1024;
	while (mapped < size) {
		while (mapped < size && idx < KERNEL_REGION_MAX_TLB_ENTRIES) {
			while (pgsz > (size - mapped))
				pgsz >>= 2;
			pgs[idx++] = pgsz;
			mapped += pgsz;
		}

		/* We under-map. Correct for this. */
		if (mapped < size) {
			while (pgs[idx - 1] == pgsz) {
				idx--;
				mapped -= pgsz;
			}
			/* XXX We may increase beyond out starting point. */
			pgsz <<= 2;
			pgs[idx++] = pgsz;
			mapped += pgsz;
		}
	}

	nents = idx;
	mask = pgs[0] - 1;
	/* Align address to the boundary */
	if (va & mask) {
		va = (va + mask) & ~mask;
		pa = (pa + mask) & ~mask;
	}

	for (idx = 0; idx < nents; idx++) {
		pgsz = pgs[idx];
		debugf("%u: %llx -> %jx, size=%jx\n", idx, pa,
		    (uintmax_t)va, (uintmax_t)pgsz);
		tlb1_set_entry(va, pa, pgsz,
		    _TLB_ENTRY_SHARED | _TLB_ENTRY_MEM);
		pa += pgsz;
		va += pgsz;
	}

	mapped = (va - base);
	if (bootverbose)
		printf("mapped size 0x%"PRIxPTR" (wasted space 0x%"PRIxPTR")\n",
		    mapped, mapped - size);
	return (mapped);
}

/*
 * TLB1 initialization routine, to be called after the very first
 * assembler level setup done in locore.S.
 */
void
tlb1_init()
{
	uint32_t mas0, mas1, mas2, mas3, mas7;
	uint32_t tsz;

	tlb1_get_tlbconf();

	mas0 = MAS0_TLBSEL(1) | MAS0_ESEL(0);
	mtspr(SPR_MAS0, mas0);
	__asm __volatile("isync; tlbre");

	mas1 = mfspr(SPR_MAS1);
	mas2 = mfspr(SPR_MAS2);
	mas3 = mfspr(SPR_MAS3);
	mas7 = mfspr(SPR_MAS7);

	kernload =  ((vm_paddr_t)(mas7 & MAS7_RPN) << 32) |
	    (mas3 & MAS3_RPN);

	tsz = (mas1 & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;
	kernsize += (tsz > 0) ? tsize2size(tsz) : 0;

	/* Setup TLB miss defaults */
	set_mas4_defaults();
}

/*
 * pmap_early_io_unmap() should be used in short conjunction with
 * pmap_early_io_map(), as in the following snippet:
 *
 * x = pmap_early_io_map(...);
 * <do something with x>
 * pmap_early_io_unmap(x, size);
 *
 * And avoiding more allocations between.
 */
void
pmap_early_io_unmap(vm_offset_t va, vm_size_t size)
{
	int i;
	tlb_entry_t e;
	vm_size_t isize;

	size = roundup(size, PAGE_SIZE);
	isize = size;
	for (i = 0; i < TLB1_ENTRIES && size > 0; i++) {
		tlb1_read_entry(&e, i);
		if (!(e.mas1 & MAS1_VALID))
			continue;
		if (va <= e.virt && (va + isize) >= (e.virt + e.size)) {
			size -= e.size;
			e.mas1 &= ~MAS1_VALID;
			tlb1_write_entry(&e, i);
		}
	}
	if (tlb1_map_base == va + isize)
		tlb1_map_base -= isize;
}	
		
vm_offset_t 
pmap_early_io_map(vm_paddr_t pa, vm_size_t size)
{
	vm_paddr_t pa_base;
	vm_offset_t va, sz;
	int i;
	tlb_entry_t e;

	KASSERT(!pmap_bootstrapped, ("Do not use after PMAP is up!"));
	
	for (i = 0; i < TLB1_ENTRIES; i++) {
		tlb1_read_entry(&e, i);
		if (!(e.mas1 & MAS1_VALID))
			continue;
		if (pa >= e.phys && (pa + size) <=
		    (e.phys + e.size))
			return (e.virt + (pa - e.phys));
	}

	pa_base = rounddown(pa, PAGE_SIZE);
	size = roundup(size + (pa - pa_base), PAGE_SIZE);
	tlb1_map_base = roundup2(tlb1_map_base, 1 << (ilog2(size) & ~1));
	va = tlb1_map_base + (pa - pa_base);

	do {
		sz = 1 << (ilog2(size) & ~1);
		tlb1_set_entry(tlb1_map_base, pa_base, sz,
		    _TLB_ENTRY_SHARED | _TLB_ENTRY_IO);
		size -= sz;
		pa_base += sz;
		tlb1_map_base += sz;
	} while (size > 0);

	return (va);
}

void
pmap_track_page(pmap_t pmap, vm_offset_t va)
{
	vm_paddr_t pa;
	vm_page_t page;
	struct pv_entry *pve;

	va = trunc_page(va);
	pa = pmap_kextract(va);
	page = PHYS_TO_VM_PAGE(pa);

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);

	TAILQ_FOREACH(pve, &page->md.pv_list, pv_link) {
		if ((pmap == pve->pv_pmap) && (va == pve->pv_va)) {
			goto out;
		}
	}
	page->md.pv_tracked = true;
	pv_insert(pmap, va, page);
out:
	PMAP_UNLOCK(pmap);
	rw_wunlock(&pvh_global_lock);
}


/*
 * Setup MAS4 defaults.
 * These values are loaded to MAS0-2 on a TLB miss.
 */
static void
set_mas4_defaults(void)
{
	uint32_t mas4;

	/* Defaults: TLB0, PID0, TSIZED=4K */
	mas4 = MAS4_TLBSELD0;
	mas4 |= (TLB_SIZE_4K << MAS4_TSIZED_SHIFT) & MAS4_TSIZED_MASK;
#ifdef SMP
	mas4 |= MAS4_MD;
#endif
	mtspr(SPR_MAS4, mas4);
	__asm __volatile("isync");
}


/*
 * Return 0 if the physical IO range is encompassed by one of the
 * the TLB1 entries, otherwise return related error code.
 */
static int
tlb1_iomapped(int i, vm_paddr_t pa, vm_size_t size, vm_offset_t *va)
{
	uint32_t prot;
	vm_paddr_t pa_start;
	vm_paddr_t pa_end;
	unsigned int entry_tsize;
	vm_size_t entry_size;
	tlb_entry_t e;

	*va = (vm_offset_t)NULL;

	tlb1_read_entry(&e, i);
	/* Skip invalid entries */
	if (!(e.mas1 & MAS1_VALID))
		return (EINVAL);

	/*
	 * The entry must be cache-inhibited, guarded, and r/w
	 * so it can function as an i/o page
	 */
	prot = e.mas2 & (MAS2_I | MAS2_G);
	if (prot != (MAS2_I | MAS2_G))
		return (EPERM);

	prot = e.mas3 & (MAS3_SR | MAS3_SW);
	if (prot != (MAS3_SR | MAS3_SW))
		return (EPERM);

	/* The address should be within the entry range. */
	entry_tsize = (e.mas1 & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;
	KASSERT((entry_tsize), ("tlb1_iomapped: invalid entry tsize"));

	entry_size = tsize2size(entry_tsize);
	pa_start = (((vm_paddr_t)e.mas7 & MAS7_RPN) << 32) | 
	    (e.mas3 & MAS3_RPN);
	pa_end = pa_start + entry_size;

	if ((pa < pa_start) || ((pa + size) > pa_end))
		return (ERANGE);

	/* Return virtual address of this mapping. */
	*va = (e.mas2 & MAS2_EPN_MASK) + (pa - pa_start);
	return (0);
}

/*
 * Invalidate all TLB0 entries which match the given TID. Note this is
 * dedicated for cases when invalidations should NOT be propagated to other
 * CPUs.
 */
static void
tid_flush(tlbtid_t tid)
{
	register_t msr;
	uint32_t mas0, mas1, mas2;
	int entry, way;


	/* Don't evict kernel translations */
	if (tid == TID_KERNEL)
		return;

	msr = mfmsr();
	__asm __volatile("wrteei 0");

	/*
	 * Newer (e500mc and later) have tlbilx, which doesn't broadcast, so use
	 * it for PID invalidation.
	 */
	switch ((mfpvr() >> 16) & 0xffff) {
	case FSL_E500mc:
	case FSL_E5500:
	case FSL_E6500:
		mtspr(SPR_MAS6, tid << MAS6_SPID0_SHIFT);
		/* tlbilxpid */
		__asm __volatile("isync; .long 0x7c000024; isync; msync");
		mtmsr(msr);
		return;
	}

	for (way = 0; way < TLB0_WAYS; way++)
		for (entry = 0; entry < TLB0_ENTRIES_PER_WAY; entry++) {

			mas0 = MAS0_TLBSEL(0) | MAS0_ESEL(way);
			mtspr(SPR_MAS0, mas0);

			mas2 = entry << MAS2_TLB0_ENTRY_IDX_SHIFT;
			mtspr(SPR_MAS2, mas2);

			__asm __volatile("isync; tlbre");

			mas1 = mfspr(SPR_MAS1);

			if (!(mas1 & MAS1_VALID))
				continue;
			if (((mas1 & MAS1_TID_MASK) >> MAS1_TID_SHIFT) != tid)
				continue;
			mas1 &= ~MAS1_VALID;
			mtspr(SPR_MAS1, mas1);
			__asm __volatile("isync; tlbwe; isync; msync");
		}
	mtmsr(msr);
}

#ifdef DDB
/* Print out contents of the MAS registers for each TLB0 entry */
static void
#ifdef __powerpc64__
tlb_print_entry(int i, uint32_t mas1, uint64_t mas2, uint32_t mas3,
#else
tlb_print_entry(int i, uint32_t mas1, uint32_t mas2, uint32_t mas3,
#endif
    uint32_t mas7)
{
	int as;
	char desc[3];
	tlbtid_t tid;
	vm_size_t size;
	unsigned int tsize;

	desc[2] = '\0';
	if (mas1 & MAS1_VALID)
		desc[0] = 'V';
	else
		desc[0] = ' ';

	if (mas1 & MAS1_IPROT)
		desc[1] = 'P';
	else
		desc[1] = ' ';

	as = (mas1 & MAS1_TS_MASK) ? 1 : 0;
	tid = MAS1_GETTID(mas1);

	tsize = (mas1 & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;
	size = 0;
	if (tsize)
		size = tsize2size(tsize);

	printf("%3d: (%s) [AS=%d] "
	    "sz = 0x%08x tsz = %d tid = %d mas1 = 0x%08x "
	    "mas2(va) = 0x%"PRI0ptrX" mas3(pa) = 0x%08x mas7 = 0x%08x\n",
	    i, desc, as, size, tsize, tid, mas1, mas2, mas3, mas7);
}

DB_SHOW_COMMAND(tlb0, tlb0_print_tlbentries)
{
	uint32_t mas0, mas1, mas3, mas7;
#ifdef __powerpc64__
	uint64_t mas2;
#else
	uint32_t mas2;
#endif
	int entryidx, way, idx;

	printf("TLB0 entries:\n");
	for (way = 0; way < TLB0_WAYS; way ++)
		for (entryidx = 0; entryidx < TLB0_ENTRIES_PER_WAY; entryidx++) {

			mas0 = MAS0_TLBSEL(0) | MAS0_ESEL(way);
			mtspr(SPR_MAS0, mas0);

			mas2 = entryidx << MAS2_TLB0_ENTRY_IDX_SHIFT;
			mtspr(SPR_MAS2, mas2);

			__asm __volatile("isync; tlbre");

			mas1 = mfspr(SPR_MAS1);
			mas2 = mfspr(SPR_MAS2);
			mas3 = mfspr(SPR_MAS3);
			mas7 = mfspr(SPR_MAS7);

			idx = tlb0_tableidx(mas2, way);
			tlb_print_entry(idx, mas1, mas2, mas3, mas7);
		}
}

/*
 * Print out contents of the MAS registers for each TLB1 entry
 */
DB_SHOW_COMMAND(tlb1, tlb1_print_tlbentries)
{
	uint32_t mas0, mas1, mas3, mas7;
#ifdef __powerpc64__
	uint64_t mas2;
#else
	uint32_t mas2;
#endif
	int i;

	printf("TLB1 entries:\n");
	for (i = 0; i < TLB1_ENTRIES; i++) {

		mas0 = MAS0_TLBSEL(1) | MAS0_ESEL(i);
		mtspr(SPR_MAS0, mas0);

		__asm __volatile("isync; tlbre");

		mas1 = mfspr(SPR_MAS1);
		mas2 = mfspr(SPR_MAS2);
		mas3 = mfspr(SPR_MAS3);
		mas7 = mfspr(SPR_MAS7);

		tlb_print_entry(i, mas1, mas2, mas3, mas7);
	}
}
#endif
