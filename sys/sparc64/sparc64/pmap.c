/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1994 John S. Dyson
 * All rights reserved.
 * Copyright (c) 1994 David Greenman
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
 * 3. Neither the name of the University nor the names of its contributors
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
 *      from:   @(#)pmap.c      7.7 (Berkeley)  5/12/91
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
#include "opt_pmap.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/msgbuf.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/smp.h>
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
#include <vm/vm_pager.h>
#include <vm/vm_phys.h>

#include <machine/cache.h>
#include <machine/frame.h>
#include <machine/instr.h>
#include <machine/md_var.h>
#include <machine/metadata.h>
#include <machine/ofw_mem.h>
#include <machine/smp.h>
#include <machine/tlb.h>
#include <machine/tte.h>
#include <machine/tsb.h>
#include <machine/ver.h>

/*
 * Map of physical memory reagions
 */
vm_paddr_t phys_avail[128];
static struct ofw_mem_region mra[128];
struct ofw_mem_region sparc64_memreg[128];
int sparc64_nmemreg;
static struct ofw_map translations[128];
static int translations_size;

static vm_offset_t pmap_idle_map;
static vm_offset_t pmap_temp_map_1;
static vm_offset_t pmap_temp_map_2;

/*
 * First and last available kernel virtual addresses
 */
vm_offset_t virtual_avail;
vm_offset_t virtual_end;
vm_offset_t kernel_vm_end;

vm_offset_t vm_max_kernel_address;

/*
 * Kernel pmap
 */
struct pmap kernel_pmap_store;

struct rwlock_padalign tte_list_global_lock;

/*
 * Allocate physical memory for use in pmap_bootstrap.
 */
static vm_paddr_t pmap_bootstrap_alloc(vm_size_t size, uint32_t colors);

static void pmap_bootstrap_set_tte(struct tte *tp, u_long vpn, u_long data);
static void pmap_cache_remove(vm_page_t m, vm_offset_t va);
static int pmap_protect_tte(struct pmap *pm1, struct pmap *pm2,
    struct tte *tp, vm_offset_t va);
static int pmap_unwire_tte(pmap_t pm, pmap_t pm2, struct tte *tp,
    vm_offset_t va);
static void pmap_init_qpages(void);

/*
 * Map the given physical page at the specified virtual address in the
 * target pmap with the protection requested.  If specified the page
 * will be wired down.
 *
 * The page queues and pmap must be locked.
 */
static int pmap_enter_locked(pmap_t pm, vm_offset_t va, vm_page_t m,
    vm_prot_t prot, u_int flags, int8_t psind);

extern int tl1_dmmu_miss_direct_patch_tsb_phys_1[];
extern int tl1_dmmu_miss_direct_patch_tsb_phys_end_1[];
extern int tl1_dmmu_miss_patch_asi_1[];
extern int tl1_dmmu_miss_patch_quad_ldd_1[];
extern int tl1_dmmu_miss_patch_tsb_1[];
extern int tl1_dmmu_miss_patch_tsb_2[];
extern int tl1_dmmu_miss_patch_tsb_mask_1[];
extern int tl1_dmmu_miss_patch_tsb_mask_2[];
extern int tl1_dmmu_prot_patch_asi_1[];
extern int tl1_dmmu_prot_patch_quad_ldd_1[];
extern int tl1_dmmu_prot_patch_tsb_1[];
extern int tl1_dmmu_prot_patch_tsb_2[];
extern int tl1_dmmu_prot_patch_tsb_mask_1[];
extern int tl1_dmmu_prot_patch_tsb_mask_2[];
extern int tl1_immu_miss_patch_asi_1[];
extern int tl1_immu_miss_patch_quad_ldd_1[];
extern int tl1_immu_miss_patch_tsb_1[];
extern int tl1_immu_miss_patch_tsb_2[];
extern int tl1_immu_miss_patch_tsb_mask_1[];
extern int tl1_immu_miss_patch_tsb_mask_2[];

/*
 * If user pmap is processed with pmap_remove and with pmap_remove and the
 * resident count drops to 0, there are no more pages to remove, so we
 * need not continue.
 */
#define	PMAP_REMOVE_DONE(pm) \
	((pm) != kernel_pmap && (pm)->pm_stats.resident_count == 0)

/*
 * The threshold (in bytes) above which tsb_foreach() is used in pmap_remove()
 * and pmap_protect() instead of trying each virtual address.
 */
#define	PMAP_TSB_THRESH	((TSB_SIZE / 2) * PAGE_SIZE)

SYSCTL_NODE(_debug, OID_AUTO, pmap_stats, CTLFLAG_RD, 0, "");

PMAP_STATS_VAR(pmap_nenter);
PMAP_STATS_VAR(pmap_nenter_update);
PMAP_STATS_VAR(pmap_nenter_replace);
PMAP_STATS_VAR(pmap_nenter_new);
PMAP_STATS_VAR(pmap_nkenter);
PMAP_STATS_VAR(pmap_nkenter_oc);
PMAP_STATS_VAR(pmap_nkenter_stupid);
PMAP_STATS_VAR(pmap_nkremove);
PMAP_STATS_VAR(pmap_nqenter);
PMAP_STATS_VAR(pmap_nqremove);
PMAP_STATS_VAR(pmap_ncache_enter);
PMAP_STATS_VAR(pmap_ncache_enter_c);
PMAP_STATS_VAR(pmap_ncache_enter_oc);
PMAP_STATS_VAR(pmap_ncache_enter_cc);
PMAP_STATS_VAR(pmap_ncache_enter_coc);
PMAP_STATS_VAR(pmap_ncache_enter_nc);
PMAP_STATS_VAR(pmap_ncache_enter_cnc);
PMAP_STATS_VAR(pmap_ncache_remove);
PMAP_STATS_VAR(pmap_ncache_remove_c);
PMAP_STATS_VAR(pmap_ncache_remove_oc);
PMAP_STATS_VAR(pmap_ncache_remove_cc);
PMAP_STATS_VAR(pmap_ncache_remove_coc);
PMAP_STATS_VAR(pmap_ncache_remove_nc);
PMAP_STATS_VAR(pmap_nzero_page);
PMAP_STATS_VAR(pmap_nzero_page_c);
PMAP_STATS_VAR(pmap_nzero_page_oc);
PMAP_STATS_VAR(pmap_nzero_page_nc);
PMAP_STATS_VAR(pmap_nzero_page_area);
PMAP_STATS_VAR(pmap_nzero_page_area_c);
PMAP_STATS_VAR(pmap_nzero_page_area_oc);
PMAP_STATS_VAR(pmap_nzero_page_area_nc);
PMAP_STATS_VAR(pmap_ncopy_page);
PMAP_STATS_VAR(pmap_ncopy_page_c);
PMAP_STATS_VAR(pmap_ncopy_page_oc);
PMAP_STATS_VAR(pmap_ncopy_page_nc);
PMAP_STATS_VAR(pmap_ncopy_page_dc);
PMAP_STATS_VAR(pmap_ncopy_page_doc);
PMAP_STATS_VAR(pmap_ncopy_page_sc);
PMAP_STATS_VAR(pmap_ncopy_page_soc);

PMAP_STATS_VAR(pmap_nnew_thread);
PMAP_STATS_VAR(pmap_nnew_thread_oc);

static inline u_long dtlb_get_data(u_int tlb, u_int slot);

/*
 * Quick sort callout for comparing memory regions
 */
static int mr_cmp(const void *a, const void *b);
static int om_cmp(const void *a, const void *b);

static int
mr_cmp(const void *a, const void *b)
{
	const struct ofw_mem_region *mra;
	const struct ofw_mem_region *mrb;

	mra = a;
	mrb = b;
	if (mra->mr_start < mrb->mr_start)
		return (-1);
	else if (mra->mr_start > mrb->mr_start)
		return (1);
	else
		return (0);
}

static int
om_cmp(const void *a, const void *b)
{
	const struct ofw_map *oma;
	const struct ofw_map *omb;

	oma = a;
	omb = b;
	if (oma->om_start < omb->om_start)
		return (-1);
	else if (oma->om_start > omb->om_start)
		return (1);
	else
		return (0);
}

static inline u_long
dtlb_get_data(u_int tlb, u_int slot)
{
	u_long data;
	register_t s;

	slot = TLB_DAR_SLOT(tlb, slot);
	/*
	 * We read ASI_DTLB_DATA_ACCESS_REG twice back-to-back in order to
	 * work around errata of USIII and beyond.
	 */
	s = intr_disable();
	(void)ldxa(slot, ASI_DTLB_DATA_ACCESS_REG);
	data = ldxa(slot, ASI_DTLB_DATA_ACCESS_REG);
	intr_restore(s);
	return (data);
}

/*
 * Bootstrap the system enough to run with virtual memory.
 */
void
pmap_bootstrap(u_int cpu_impl)
{
	struct pmap *pm;
	struct tte *tp;
	vm_offset_t off;
	vm_offset_t va;
	vm_paddr_t pa;
	vm_size_t physsz;
	vm_size_t virtsz;
	u_long data;
	u_long vpn;
	phandle_t pmem;
	phandle_t vmem;
	u_int dtlb_slots_avail;
	int i;
	int j;
	int sz;
	uint32_t asi;
	uint32_t colors;
	uint32_t ldd;

	/*
	 * Set the kernel context.
	 */
	pmap_set_kctx();

	colors = dcache_color_ignore != 0 ? 1 : DCACHE_COLORS;

	/*
	 * Find out what physical memory is available from the PROM and
	 * initialize the phys_avail array.  This must be done before
	 * pmap_bootstrap_alloc is called.
	 */
	if ((pmem = OF_finddevice("/memory")) == -1)
		OF_panic("%s: finddevice /memory", __func__);
	if ((sz = OF_getproplen(pmem, "available")) == -1)
		OF_panic("%s: getproplen /memory/available", __func__);
	if (sizeof(phys_avail) < sz)
		OF_panic("%s: phys_avail too small", __func__);
	if (sizeof(mra) < sz)
		OF_panic("%s: mra too small", __func__);
	bzero(mra, sz);
	if (OF_getprop(pmem, "available", mra, sz) == -1)
		OF_panic("%s: getprop /memory/available", __func__);
	sz /= sizeof(*mra);
#ifdef DIAGNOSTIC
	OF_printf("pmap_bootstrap: physical memory\n");
#endif
	qsort(mra, sz, sizeof (*mra), mr_cmp);
	physsz = 0;
	getenv_quad("hw.physmem", &physmem);
	physmem = btoc(physmem);
	for (i = 0, j = 0; i < sz; i++, j += 2) {
#ifdef DIAGNOSTIC
		OF_printf("start=%#lx size=%#lx\n", mra[i].mr_start,
		    mra[i].mr_size);
#endif
		if (physmem != 0 && btoc(physsz + mra[i].mr_size) >= physmem) {
			if (btoc(physsz) < physmem) {
				phys_avail[j] = mra[i].mr_start;
				phys_avail[j + 1] = mra[i].mr_start +
				    (ctob(physmem) - physsz);
				physsz = ctob(physmem);
			}
			break;
		}
		phys_avail[j] = mra[i].mr_start;
		phys_avail[j + 1] = mra[i].mr_start + mra[i].mr_size;
		physsz += mra[i].mr_size;
	}
	physmem = btoc(physsz);

	/*
	 * Calculate the size of kernel virtual memory, and the size and mask
	 * for the kernel TSB based on the phsyical memory size but limited
	 * by the amount of dTLB slots available for locked entries if we have
	 * to lock the TSB in the TLB (given that for spitfire-class CPUs all
	 * of the dt64 slots can hold locked entries but there is no large
	 * dTLB for unlocked ones, we don't use more than half of it for the
	 * TSB).
	 * Note that for reasons unknown OpenSolaris doesn't take advantage of
	 * ASI_ATOMIC_QUAD_LDD_PHYS on UltraSPARC-III.  However, given that no
	 * public documentation is available for these, the latter just might
	 * not support it, yet.
	 */
	if (cpu_impl == CPU_IMPL_SPARC64V ||
	    cpu_impl >= CPU_IMPL_ULTRASPARCIIIp) {
		tsb_kernel_ldd_phys = 1;
		virtsz = roundup(5 / 3 * physsz, PAGE_SIZE_4M <<
		    (PAGE_SHIFT - TTE_SHIFT));
	} else {
		dtlb_slots_avail = 0;
		for (i = 0; i < dtlb_slots; i++) {
			data = dtlb_get_data(cpu_impl ==
			    CPU_IMPL_ULTRASPARCIII ? TLB_DAR_T16 :
			    TLB_DAR_T32, i);
			if ((data & (TD_V | TD_L)) != (TD_V | TD_L))
				dtlb_slots_avail++;
		}
#ifdef SMP
		dtlb_slots_avail -= PCPU_PAGES;
#endif
		if (cpu_impl >= CPU_IMPL_ULTRASPARCI &&
		    cpu_impl < CPU_IMPL_ULTRASPARCIII)
			dtlb_slots_avail /= 2;
		virtsz = roundup(physsz, PAGE_SIZE_4M <<
		    (PAGE_SHIFT - TTE_SHIFT));
		virtsz = MIN(virtsz, (dtlb_slots_avail * PAGE_SIZE_4M) <<
		    (PAGE_SHIFT - TTE_SHIFT));
	}
	vm_max_kernel_address = VM_MIN_KERNEL_ADDRESS + virtsz;
	tsb_kernel_size = virtsz >> (PAGE_SHIFT - TTE_SHIFT);
	tsb_kernel_mask = (tsb_kernel_size >> TTE_SHIFT) - 1;

	/*
	 * Allocate the kernel TSB and lock it in the TLB if necessary.
	 */
	pa = pmap_bootstrap_alloc(tsb_kernel_size, colors);
	if (pa & PAGE_MASK_4M)
		OF_panic("%s: TSB unaligned", __func__);
	tsb_kernel_phys = pa;
	if (tsb_kernel_ldd_phys == 0) {
		tsb_kernel =
		    (struct tte *)(VM_MIN_KERNEL_ADDRESS - tsb_kernel_size);
		pmap_map_tsb();
		bzero(tsb_kernel, tsb_kernel_size);
	} else {
		tsb_kernel =
		    (struct tte *)TLB_PHYS_TO_DIRECT(tsb_kernel_phys);
		aszero(ASI_PHYS_USE_EC, tsb_kernel_phys, tsb_kernel_size);
	}

	/*
	 * Allocate and map the dynamic per-CPU area for the BSP.
	 */
	pa = pmap_bootstrap_alloc(DPCPU_SIZE, colors);
	dpcpu0 = (void *)TLB_PHYS_TO_DIRECT(pa);

	/*
	 * Allocate and map the message buffer.
	 */
	pa = pmap_bootstrap_alloc(msgbufsize, colors);
	msgbufp = (struct msgbuf *)TLB_PHYS_TO_DIRECT(pa);

	/*
	 * Patch the TSB addresses and mask as well as the ASIs used to load
	 * it into the trap table.
	 */

#define	LDDA_R_I_R(rd, imm_asi, rs1, rs2)				\
	(EIF_OP(IOP_LDST) | EIF_F3_RD(rd) | EIF_F3_OP3(INS3_LDDA) |	\
	    EIF_F3_RS1(rs1) | EIF_F3_I(0) | EIF_F3_IMM_ASI(imm_asi) |	\
	    EIF_F3_RS2(rs2))
#define	OR_R_I_R(rd, imm13, rs1)					\
	(EIF_OP(IOP_MISC) | EIF_F3_RD(rd) | EIF_F3_OP3(INS2_OR) |	\
	    EIF_F3_RS1(rs1) | EIF_F3_I(1) | EIF_IMM(imm13, 13))
#define	SETHI(rd, imm22)						\
	(EIF_OP(IOP_FORM2) | EIF_F2_RD(rd) | EIF_F2_OP2(INS0_SETHI) |	\
	    EIF_IMM((imm22) >> 10, 22))
#define	WR_R_I(rd, imm13, rs1)						\
	(EIF_OP(IOP_MISC) | EIF_F3_RD(rd) | EIF_F3_OP3(INS2_WR) |	\
	    EIF_F3_RS1(rs1) | EIF_F3_I(1) | EIF_IMM(imm13, 13))

#define	PATCH_ASI(addr, asi) do {					\
	if (addr[0] != WR_R_I(IF_F3_RD(addr[0]), 0x0,			\
	    IF_F3_RS1(addr[0])))					\
		OF_panic("%s: patched instructions have changed",	\
		    __func__);						\
	addr[0] |= EIF_IMM((asi), 13);					\
	flush(addr);							\
} while (0)

#define	PATCH_LDD(addr, asi) do {					\
	if (addr[0] != LDDA_R_I_R(IF_F3_RD(addr[0]), 0x0,		\
	    IF_F3_RS1(addr[0]), IF_F3_RS2(addr[0])))			\
		OF_panic("%s: patched instructions have changed",	\
		    __func__);						\
	addr[0] |= EIF_F3_IMM_ASI(asi);					\
	flush(addr);							\
} while (0)

#define	PATCH_TSB(addr, val) do {					\
	if (addr[0] != SETHI(IF_F2_RD(addr[0]), 0x0) ||			\
	    addr[1] != OR_R_I_R(IF_F3_RD(addr[1]), 0x0,			\
	    IF_F3_RS1(addr[1]))	||					\
	    addr[3] != SETHI(IF_F2_RD(addr[3]), 0x0))			\
		OF_panic("%s: patched instructions have changed",	\
		    __func__);						\
	addr[0] |= EIF_IMM((val) >> 42, 22);				\
	addr[1] |= EIF_IMM((val) >> 32, 10);				\
	addr[3] |= EIF_IMM((val) >> 10, 22);				\
	flush(addr);							\
	flush(addr + 1);						\
	flush(addr + 3);						\
} while (0)

#define	PATCH_TSB_MASK(addr, val) do {					\
	if (addr[0] != SETHI(IF_F2_RD(addr[0]), 0x0) ||			\
	    addr[1] != OR_R_I_R(IF_F3_RD(addr[1]), 0x0,			\
	    IF_F3_RS1(addr[1])))					\
		OF_panic("%s: patched instructions have changed",	\
		    __func__);						\
	addr[0] |= EIF_IMM((val) >> 10, 22);				\
	addr[1] |= EIF_IMM((val), 10);					\
	flush(addr);							\
	flush(addr + 1);						\
} while (0)

	if (tsb_kernel_ldd_phys == 0) {
		asi = ASI_N;
		ldd = ASI_NUCLEUS_QUAD_LDD;
		off = (vm_offset_t)tsb_kernel;
	} else {
		asi = ASI_PHYS_USE_EC;
		ldd = ASI_ATOMIC_QUAD_LDD_PHYS;
		off = (vm_offset_t)tsb_kernel_phys;
	}
	PATCH_TSB(tl1_dmmu_miss_direct_patch_tsb_phys_1, tsb_kernel_phys);
	PATCH_TSB(tl1_dmmu_miss_direct_patch_tsb_phys_end_1,
	    tsb_kernel_phys + tsb_kernel_size - 1);
	PATCH_ASI(tl1_dmmu_miss_patch_asi_1, asi);
	PATCH_LDD(tl1_dmmu_miss_patch_quad_ldd_1, ldd);
	PATCH_TSB(tl1_dmmu_miss_patch_tsb_1, off);
	PATCH_TSB(tl1_dmmu_miss_patch_tsb_2, off);
	PATCH_TSB_MASK(tl1_dmmu_miss_patch_tsb_mask_1, tsb_kernel_mask);
	PATCH_TSB_MASK(tl1_dmmu_miss_patch_tsb_mask_2, tsb_kernel_mask);
	PATCH_ASI(tl1_dmmu_prot_patch_asi_1, asi);
	PATCH_LDD(tl1_dmmu_prot_patch_quad_ldd_1, ldd);
	PATCH_TSB(tl1_dmmu_prot_patch_tsb_1, off);
	PATCH_TSB(tl1_dmmu_prot_patch_tsb_2, off);
	PATCH_TSB_MASK(tl1_dmmu_prot_patch_tsb_mask_1, tsb_kernel_mask);
	PATCH_TSB_MASK(tl1_dmmu_prot_patch_tsb_mask_2, tsb_kernel_mask);
	PATCH_ASI(tl1_immu_miss_patch_asi_1, asi);
	PATCH_LDD(tl1_immu_miss_patch_quad_ldd_1, ldd);
	PATCH_TSB(tl1_immu_miss_patch_tsb_1, off);
	PATCH_TSB(tl1_immu_miss_patch_tsb_2, off);
	PATCH_TSB_MASK(tl1_immu_miss_patch_tsb_mask_1, tsb_kernel_mask);
	PATCH_TSB_MASK(tl1_immu_miss_patch_tsb_mask_2, tsb_kernel_mask);

	/*
	 * Enter fake 8k pages for the 4MB kernel pages, so that
	 * pmap_kextract() will work for them.
	 */
	for (i = 0; i < kernel_tlb_slots; i++) {
		pa = kernel_tlbs[i].te_pa;
		va = kernel_tlbs[i].te_va;
		for (off = 0; off < PAGE_SIZE_4M; off += PAGE_SIZE) {
			tp = tsb_kvtotte(va + off);
			vpn = TV_VPN(va + off, TS_8K);
			data = TD_V | TD_8K | TD_PA(pa + off) | TD_REF |
			    TD_SW | TD_CP | TD_CV | TD_P | TD_W;
			pmap_bootstrap_set_tte(tp, vpn, data);
		}
	}

	/*
	 * Set the start and end of KVA.  The kernel is loaded starting
	 * at the first available 4MB super page, so we advance to the
	 * end of the last one used for it.
	 */
	virtual_avail = KERNBASE + kernel_tlb_slots * PAGE_SIZE_4M;
	virtual_end = vm_max_kernel_address;
	kernel_vm_end = vm_max_kernel_address;

	/*
	 * Allocate kva space for temporary mappings.
	 */
	pmap_idle_map = virtual_avail;
	virtual_avail += PAGE_SIZE * colors;
	pmap_temp_map_1 = virtual_avail;
	virtual_avail += PAGE_SIZE * colors;
	pmap_temp_map_2 = virtual_avail;
	virtual_avail += PAGE_SIZE * colors;

	/*
	 * Allocate a kernel stack with guard page for thread0 and map it
	 * into the kernel TSB.  We must ensure that the virtual address is
	 * colored properly for corresponding CPUs, since we're allocating
	 * from phys_avail so the memory won't have an associated vm_page_t.
	 */
	pa = pmap_bootstrap_alloc(KSTACK_PAGES * PAGE_SIZE, colors);
	kstack0_phys = pa;
	virtual_avail += roundup(KSTACK_GUARD_PAGES, colors) * PAGE_SIZE;
	kstack0 = virtual_avail;
	virtual_avail += roundup(KSTACK_PAGES, colors) * PAGE_SIZE;
	if (dcache_color_ignore == 0)
		KASSERT(DCACHE_COLOR(kstack0) == DCACHE_COLOR(kstack0_phys),
		    ("pmap_bootstrap: kstack0 miscolored"));
	for (i = 0; i < KSTACK_PAGES; i++) {
		pa = kstack0_phys + i * PAGE_SIZE;
		va = kstack0 + i * PAGE_SIZE;
		tp = tsb_kvtotte(va);
		vpn = TV_VPN(va, TS_8K);
		data = TD_V | TD_8K | TD_PA(pa) | TD_REF | TD_SW | TD_CP |
		    TD_CV | TD_P | TD_W;
		pmap_bootstrap_set_tte(tp, vpn, data);
	}

	/*
	 * Calculate the last available physical address.
	 */
	for (i = 0; phys_avail[i + 2] != 0; i += 2)
		;
	Maxmem = sparc64_btop(phys_avail[i + 1]);

	/*
	 * Add the PROM mappings to the kernel TSB.
	 */
	if ((vmem = OF_finddevice("/virtual-memory")) == -1)
		OF_panic("%s: finddevice /virtual-memory", __func__);
	if ((sz = OF_getproplen(vmem, "translations")) == -1)
		OF_panic("%s: getproplen translations", __func__);
	if (sizeof(translations) < sz)
		OF_panic("%s: translations too small", __func__);
	bzero(translations, sz);
	if (OF_getprop(vmem, "translations", translations, sz) == -1)
		OF_panic("%s: getprop /virtual-memory/translations",
		    __func__);
	sz /= sizeof(*translations);
	translations_size = sz;
#ifdef DIAGNOSTIC
	OF_printf("pmap_bootstrap: translations\n");
#endif
	qsort(translations, sz, sizeof (*translations), om_cmp);
	for (i = 0; i < sz; i++) {
#ifdef DIAGNOSTIC
		OF_printf("translation: start=%#lx size=%#lx tte=%#lx\n",
		    translations[i].om_start, translations[i].om_size,
		    translations[i].om_tte);
#endif
		if ((translations[i].om_tte & TD_V) == 0)
			continue;
		if (translations[i].om_start < VM_MIN_PROM_ADDRESS ||
		    translations[i].om_start > VM_MAX_PROM_ADDRESS)
			continue;
		for (off = 0; off < translations[i].om_size;
		    off += PAGE_SIZE) {
			va = translations[i].om_start + off;
			tp = tsb_kvtotte(va);
			vpn = TV_VPN(va, TS_8K);
			data = ((translations[i].om_tte &
			    ~((TD_SOFT2_MASK << TD_SOFT2_SHIFT) |
			    (cpu_impl >= CPU_IMPL_ULTRASPARCI &&
			    cpu_impl < CPU_IMPL_ULTRASPARCIII ?
			    (TD_DIAG_SF_MASK << TD_DIAG_SF_SHIFT) :
			    (TD_RSVD_CH_MASK << TD_RSVD_CH_SHIFT)) |
			    (TD_SOFT_MASK << TD_SOFT_SHIFT))) | TD_EXEC) +
			    off;
			pmap_bootstrap_set_tte(tp, vpn, data);
		}
	}

	/*
	 * Get the available physical memory ranges from /memory/reg.  These
	 * are only used for kernel dumps, but it may not be wise to do PROM
	 * calls in that situation.
	 */
	if ((sz = OF_getproplen(pmem, "reg")) == -1)
		OF_panic("%s: getproplen /memory/reg", __func__);
	if (sizeof(sparc64_memreg) < sz)
		OF_panic("%s: sparc64_memreg too small", __func__);
	if (OF_getprop(pmem, "reg", sparc64_memreg, sz) == -1)
		OF_panic("%s: getprop /memory/reg", __func__);
	sparc64_nmemreg = sz / sizeof(*sparc64_memreg);

	/*
	 * Initialize the kernel pmap (which is statically allocated).
	 */
	pm = kernel_pmap;
	PMAP_LOCK_INIT(pm);
	for (i = 0; i < MAXCPU; i++)
		pm->pm_context[i] = TLB_CTX_KERNEL;
	CPU_FILL(&pm->pm_active);

	/*
	 * Initialize the global tte list lock, which is more commonly
	 * known as the pmap pv global lock.
	 */
	rw_init(&tte_list_global_lock, "pmap pv global");

	/*
	 * Flush all non-locked TLB entries possibly left over by the
	 * firmware.
	 */
	tlb_flush_nonlocked();
}

static void
pmap_init_qpages(void)
{
	struct pcpu *pc;
	int i;

	if (dcache_color_ignore != 0)
		return;

	CPU_FOREACH(i) {
		pc = pcpu_find(i);
		pc->pc_qmap_addr = kva_alloc(PAGE_SIZE * DCACHE_COLORS);
		if (pc->pc_qmap_addr == 0)
			panic("pmap_init_qpages: unable to allocate KVA");
	}
}

SYSINIT(qpages_init, SI_SUB_CPU, SI_ORDER_ANY, pmap_init_qpages, NULL);

/*
 * Map the 4MB kernel TSB pages.
 */
void
pmap_map_tsb(void)
{
	vm_offset_t va;
	vm_paddr_t pa;
	u_long data;
	int i;

	for (i = 0; i < tsb_kernel_size; i += PAGE_SIZE_4M) {
		va = (vm_offset_t)tsb_kernel + i;
		pa = tsb_kernel_phys + i;
		data = TD_V | TD_4M | TD_PA(pa) | TD_L | TD_CP | TD_CV |
		    TD_P | TD_W;
		stxa(AA_DMMU_TAR, ASI_DMMU, TLB_TAR_VA(va) |
		    TLB_TAR_CTX(TLB_CTX_KERNEL));
		stxa_sync(0, ASI_DTLB_DATA_IN_REG, data);
	}
}

/*
 * Set the secondary context to be the kernel context (needed for FP block
 * operations in the kernel).
 */
void
pmap_set_kctx(void)
{

	stxa(AA_DMMU_SCXR, ASI_DMMU, (ldxa(AA_DMMU_SCXR, ASI_DMMU) &
	    TLB_CXR_PGSZ_MASK) | TLB_CTX_KERNEL);
	flush(KERNBASE);
}

/*
 * Allocate a physical page of memory directly from the phys_avail map.
 * Can only be called from pmap_bootstrap before avail start and end are
 * calculated.
 */
static vm_paddr_t
pmap_bootstrap_alloc(vm_size_t size, uint32_t colors)
{
	vm_paddr_t pa;
	int i;

	size = roundup(size, PAGE_SIZE * colors);
	for (i = 0; phys_avail[i + 1] != 0; i += 2) {
		if (phys_avail[i + 1] - phys_avail[i] < size)
			continue;
		pa = phys_avail[i];
		phys_avail[i] += size;
		return (pa);
	}
	OF_panic("%s: no suitable region found", __func__);
}

/*
 * Set a TTE.  This function is intended as a helper when tsb_kernel is
 * direct-mapped but we haven't taken over the trap table, yet, as it's the
 * case when we are taking advantage of ASI_ATOMIC_QUAD_LDD_PHYS to access
 * the kernel TSB.
 */
void
pmap_bootstrap_set_tte(struct tte *tp, u_long vpn, u_long data)
{

	if (tsb_kernel_ldd_phys == 0) {
		tp->tte_vpn = vpn;
		tp->tte_data = data;
	} else {
		stxa((vm_paddr_t)tp + offsetof(struct tte, tte_vpn),
		    ASI_PHYS_USE_EC, vpn);
		stxa((vm_paddr_t)tp + offsetof(struct tte, tte_data),
		    ASI_PHYS_USE_EC, data);
	}
}

/*
 * Initialize a vm_page's machine-dependent fields.
 */
void
pmap_page_init(vm_page_t m)
{

	TAILQ_INIT(&m->md.tte_list);
	m->md.color = DCACHE_COLOR(VM_PAGE_TO_PHYS(m));
	m->md.pmap = NULL;
}

/*
 * Initialize the pmap module.
 */
void
pmap_init(void)
{
	vm_offset_t addr;
	vm_size_t size;
	int result;
	int i;

	for (i = 0; i < translations_size; i++) {
		addr = translations[i].om_start;
		size = translations[i].om_size;
		if ((translations[i].om_tte & TD_V) == 0)
			continue;
		if (addr < VM_MIN_PROM_ADDRESS || addr > VM_MAX_PROM_ADDRESS)
			continue;
		result = vm_map_find(kernel_map, NULL, 0, &addr, size, 0,
		    VMFS_NO_SPACE, VM_PROT_ALL, VM_PROT_ALL, MAP_NOFAULT);
		if (result != KERN_SUCCESS || addr != translations[i].om_start)
			panic("pmap_init: vm_map_find");
	}
}

/*
 * Extract the physical page address associated with the given
 * map/virtual_address pair.
 */
vm_paddr_t
pmap_extract(pmap_t pm, vm_offset_t va)
{
	struct tte *tp;
	vm_paddr_t pa;

	if (pm == kernel_pmap)
		return (pmap_kextract(va));
	PMAP_LOCK(pm);
	tp = tsb_tte_lookup(pm, va);
	if (tp == NULL)
		pa = 0;
	else
		pa = TTE_GET_PA(tp) | (va & TTE_GET_PAGE_MASK(tp));
	PMAP_UNLOCK(pm);
	return (pa);
}

/*
 * Atomically extract and hold the physical page with the given
 * pmap and virtual address pair if that mapping permits the given
 * protection.
 */
vm_page_t
pmap_extract_and_hold(pmap_t pm, vm_offset_t va, vm_prot_t prot)
{
	struct tte *tp;
	vm_page_t m;
	vm_paddr_t pa;

	m = NULL;
	pa = 0;
	PMAP_LOCK(pm);
retry:
	if (pm == kernel_pmap) {
		if (va >= VM_MIN_DIRECT_ADDRESS) {
			tp = NULL;
			m = PHYS_TO_VM_PAGE(TLB_DIRECT_TO_PHYS(va));
			(void)vm_page_pa_tryrelock(pm, TLB_DIRECT_TO_PHYS(va),
			    &pa);
			vm_page_hold(m);
		} else {
			tp = tsb_kvtotte(va);
			if ((tp->tte_data & TD_V) == 0)
				tp = NULL;
		}
	} else
		tp = tsb_tte_lookup(pm, va);
	if (tp != NULL && ((tp->tte_data & TD_SW) ||
	    (prot & VM_PROT_WRITE) == 0)) {
		if (vm_page_pa_tryrelock(pm, TTE_GET_PA(tp), &pa))
			goto retry;
		m = PHYS_TO_VM_PAGE(TTE_GET_PA(tp));
		vm_page_hold(m);
	}
	PA_UNLOCK_COND(pa);
	PMAP_UNLOCK(pm);
	return (m);
}

/*
 * Extract the physical page address associated with the given kernel virtual
 * address.
 */
vm_paddr_t
pmap_kextract(vm_offset_t va)
{
	struct tte *tp;

	if (va >= VM_MIN_DIRECT_ADDRESS)
		return (TLB_DIRECT_TO_PHYS(va));
	tp = tsb_kvtotte(va);
	if ((tp->tte_data & TD_V) == 0)
		return (0);
	return (TTE_GET_PA(tp) | (va & TTE_GET_PAGE_MASK(tp)));
}

int
pmap_cache_enter(vm_page_t m, vm_offset_t va)
{
	struct tte *tp;
	int color;

	rw_assert(&tte_list_global_lock, RA_WLOCKED);
	KASSERT((m->flags & PG_FICTITIOUS) == 0,
	    ("pmap_cache_enter: fake page"));
	PMAP_STATS_INC(pmap_ncache_enter);

	if (dcache_color_ignore != 0)
		return (1);

	/*
	 * Find the color for this virtual address and note the added mapping.
	 */
	color = DCACHE_COLOR(va);
	m->md.colors[color]++;

	/*
	 * If all existing mappings have the same color, the mapping is
	 * cacheable.
	 */
	if (m->md.color == color) {
		KASSERT(m->md.colors[DCACHE_OTHER_COLOR(color)] == 0,
		    ("pmap_cache_enter: cacheable, mappings of other color"));
		if (m->md.color == DCACHE_COLOR(VM_PAGE_TO_PHYS(m)))
			PMAP_STATS_INC(pmap_ncache_enter_c);
		else
			PMAP_STATS_INC(pmap_ncache_enter_oc);
		return (1);
	}

	/*
	 * If there are no mappings of the other color, and the page still has
	 * the wrong color, this must be a new mapping.  Change the color to
	 * match the new mapping, which is cacheable.  We must flush the page
	 * from the cache now.
	 */
	if (m->md.colors[DCACHE_OTHER_COLOR(color)] == 0) {
		KASSERT(m->md.colors[color] == 1,
		    ("pmap_cache_enter: changing color, not new mapping"));
		dcache_page_inval(VM_PAGE_TO_PHYS(m));
		m->md.color = color;
		if (m->md.color == DCACHE_COLOR(VM_PAGE_TO_PHYS(m)))
			PMAP_STATS_INC(pmap_ncache_enter_cc);
		else
			PMAP_STATS_INC(pmap_ncache_enter_coc);
		return (1);
	}

	/*
	 * If the mapping is already non-cacheable, just return.
	 */
	if (m->md.color == -1) {
		PMAP_STATS_INC(pmap_ncache_enter_nc);
		return (0);
	}

	PMAP_STATS_INC(pmap_ncache_enter_cnc);

	/*
	 * Mark all mappings as uncacheable, flush any lines with the other
	 * color out of the dcache, and set the color to none (-1).
	 */
	TAILQ_FOREACH(tp, &m->md.tte_list, tte_link) {
		atomic_clear_long(&tp->tte_data, TD_CV);
		tlb_page_demap(TTE_GET_PMAP(tp), TTE_GET_VA(tp));
	}
	dcache_page_inval(VM_PAGE_TO_PHYS(m));
	m->md.color = -1;
	return (0);
}

static void
pmap_cache_remove(vm_page_t m, vm_offset_t va)
{
	struct tte *tp;
	int color;

	rw_assert(&tte_list_global_lock, RA_WLOCKED);
	CTR3(KTR_PMAP, "pmap_cache_remove: m=%p va=%#lx c=%d", m, va,
	    m->md.colors[DCACHE_COLOR(va)]);
	KASSERT((m->flags & PG_FICTITIOUS) == 0,
	    ("pmap_cache_remove: fake page"));
	PMAP_STATS_INC(pmap_ncache_remove);

	if (dcache_color_ignore != 0)
		return;

	KASSERT(m->md.colors[DCACHE_COLOR(va)] > 0,
	    ("pmap_cache_remove: no mappings %d <= 0",
	    m->md.colors[DCACHE_COLOR(va)]));

	/*
	 * Find the color for this virtual address and note the removal of
	 * the mapping.
	 */
	color = DCACHE_COLOR(va);
	m->md.colors[color]--;

	/*
	 * If the page is cacheable, just return and keep the same color, even
	 * if there are no longer any mappings.
	 */
	if (m->md.color != -1) {
		if (m->md.color == DCACHE_COLOR(VM_PAGE_TO_PHYS(m)))
			PMAP_STATS_INC(pmap_ncache_remove_c);
		else
			PMAP_STATS_INC(pmap_ncache_remove_oc);
		return;
	}

	KASSERT(m->md.colors[DCACHE_OTHER_COLOR(color)] != 0,
	    ("pmap_cache_remove: uncacheable, no mappings of other color"));

	/*
	 * If the page is not cacheable (color is -1), and the number of
	 * mappings for this color is not zero, just return.  There are
	 * mappings of the other color still, so remain non-cacheable.
	 */
	if (m->md.colors[color] != 0) {
		PMAP_STATS_INC(pmap_ncache_remove_nc);
		return;
	}

	/*
	 * The number of mappings for this color is now zero.  Recache the
	 * other colored mappings, and change the page color to the other
	 * color.  There should be no lines in the data cache for this page,
	 * so flushing should not be needed.
	 */
	TAILQ_FOREACH(tp, &m->md.tte_list, tte_link) {
		atomic_set_long(&tp->tte_data, TD_CV);
		tlb_page_demap(TTE_GET_PMAP(tp), TTE_GET_VA(tp));
	}
	m->md.color = DCACHE_OTHER_COLOR(color);

	if (m->md.color == DCACHE_COLOR(VM_PAGE_TO_PHYS(m)))
		PMAP_STATS_INC(pmap_ncache_remove_cc);
	else
		PMAP_STATS_INC(pmap_ncache_remove_coc);
}

/*
 * Map a wired page into kernel virtual address space.
 */
void
pmap_kenter(vm_offset_t va, vm_page_t m)
{
	vm_offset_t ova;
	struct tte *tp;
	vm_page_t om;
	u_long data;

	rw_assert(&tte_list_global_lock, RA_WLOCKED);
	PMAP_STATS_INC(pmap_nkenter);
	tp = tsb_kvtotte(va);
	CTR4(KTR_PMAP, "pmap_kenter: va=%#lx pa=%#lx tp=%p data=%#lx",
	    va, VM_PAGE_TO_PHYS(m), tp, tp->tte_data);
	if (DCACHE_COLOR(VM_PAGE_TO_PHYS(m)) != DCACHE_COLOR(va)) {
		CTR5(KTR_SPARE2,
	"pmap_kenter: off color va=%#lx pa=%#lx o=%p ot=%d pi=%#lx",
		    va, VM_PAGE_TO_PHYS(m), m->object,
		    m->object ? m->object->type : -1,
		    m->pindex);
		PMAP_STATS_INC(pmap_nkenter_oc);
	}
	if ((tp->tte_data & TD_V) != 0) {
		om = PHYS_TO_VM_PAGE(TTE_GET_PA(tp));
		ova = TTE_GET_VA(tp);
		if (m == om && va == ova) {
			PMAP_STATS_INC(pmap_nkenter_stupid);
			return;
		}
		TAILQ_REMOVE(&om->md.tte_list, tp, tte_link);
		pmap_cache_remove(om, ova);
		if (va != ova)
			tlb_page_demap(kernel_pmap, ova);
	}
	data = TD_V | TD_8K | VM_PAGE_TO_PHYS(m) | TD_REF | TD_SW | TD_CP |
	    TD_P | TD_W;
	if (pmap_cache_enter(m, va) != 0)
		data |= TD_CV;
	tp->tte_vpn = TV_VPN(va, TS_8K);
	tp->tte_data = data;
	TAILQ_INSERT_TAIL(&m->md.tte_list, tp, tte_link);
}

/*
 * Map a wired page into kernel virtual address space.  This additionally
 * takes a flag argument which is or'ed to the TTE data.  This is used by
 * sparc64_bus_mem_map().
 * NOTE: if the mapping is non-cacheable, it's the caller's responsibility
 * to flush entries that might still be in the cache, if applicable.
 */
void
pmap_kenter_flags(vm_offset_t va, vm_paddr_t pa, u_long flags)
{
	struct tte *tp;

	tp = tsb_kvtotte(va);
	CTR4(KTR_PMAP, "pmap_kenter_flags: va=%#lx pa=%#lx tp=%p data=%#lx",
	    va, pa, tp, tp->tte_data);
	tp->tte_vpn = TV_VPN(va, TS_8K);
	tp->tte_data = TD_V | TD_8K | TD_PA(pa) | TD_REF | TD_P | flags;
}

/*
 * Remove a wired page from kernel virtual address space.
 */
void
pmap_kremove(vm_offset_t va)
{
	struct tte *tp;
	vm_page_t m;

	rw_assert(&tte_list_global_lock, RA_WLOCKED);
	PMAP_STATS_INC(pmap_nkremove);
	tp = tsb_kvtotte(va);
	CTR3(KTR_PMAP, "pmap_kremove: va=%#lx tp=%p data=%#lx", va, tp,
	    tp->tte_data);
	if ((tp->tte_data & TD_V) == 0)
		return;
	m = PHYS_TO_VM_PAGE(TTE_GET_PA(tp));
	TAILQ_REMOVE(&m->md.tte_list, tp, tte_link);
	pmap_cache_remove(m, va);
	TTE_ZERO(tp);
}

/*
 * Inverse of pmap_kenter_flags, used by bus_space_unmap().
 */
void
pmap_kremove_flags(vm_offset_t va)
{
	struct tte *tp;

	tp = tsb_kvtotte(va);
	CTR3(KTR_PMAP, "pmap_kremove_flags: va=%#lx tp=%p data=%#lx", va, tp,
	    tp->tte_data);
	TTE_ZERO(tp);
}

/*
 * Map a range of physical addresses into kernel virtual address space.
 *
 * The value passed in *virt is a suggested virtual address for the mapping.
 * Architectures which can support a direct-mapped physical to virtual region
 * can return the appropriate address within that region, leaving '*virt'
 * unchanged.
 */
vm_offset_t
pmap_map(vm_offset_t *virt, vm_paddr_t start, vm_paddr_t end, int prot)
{

	return (TLB_PHYS_TO_DIRECT(start));
}

/*
 * Map a list of wired pages into kernel virtual address space.  This is
 * intended for temporary mappings which do not need page modification or
 * references recorded.  Existing mappings in the region are overwritten.
 */
void
pmap_qenter(vm_offset_t sva, vm_page_t *m, int count)
{
	vm_offset_t va;

	PMAP_STATS_INC(pmap_nqenter);
	va = sva;
	rw_wlock(&tte_list_global_lock);
	while (count-- > 0) {
		pmap_kenter(va, *m);
		va += PAGE_SIZE;
		m++;
	}
	rw_wunlock(&tte_list_global_lock);
	tlb_range_demap(kernel_pmap, sva, va);
}

/*
 * Remove page mappings from kernel virtual address space.  Intended for
 * temporary mappings entered by pmap_qenter.
 */
void
pmap_qremove(vm_offset_t sva, int count)
{
	vm_offset_t va;

	PMAP_STATS_INC(pmap_nqremove);
	va = sva;
	rw_wlock(&tte_list_global_lock);
	while (count-- > 0) {
		pmap_kremove(va);
		va += PAGE_SIZE;
	}
	rw_wunlock(&tte_list_global_lock);
	tlb_range_demap(kernel_pmap, sva, va);
}

/*
 * Initialize the pmap associated with process 0.
 */
void
pmap_pinit0(pmap_t pm)
{
	int i;

	PMAP_LOCK_INIT(pm);
	for (i = 0; i < MAXCPU; i++)
		pm->pm_context[i] = TLB_CTX_KERNEL;
	CPU_ZERO(&pm->pm_active);
	pm->pm_tsb = NULL;
	pm->pm_tsb_obj = NULL;
	bzero(&pm->pm_stats, sizeof(pm->pm_stats));
}

/*
 * Initialize a preallocated and zeroed pmap structure, such as one in a
 * vmspace structure.
 */
int
pmap_pinit(pmap_t pm)
{
	vm_page_t ma[TSB_PAGES];
	int i;

	/*
	 * Allocate KVA space for the TSB.
	 */
	if (pm->pm_tsb == NULL) {
		pm->pm_tsb = (struct tte *)kva_alloc(TSB_BSIZE);
		if (pm->pm_tsb == NULL)
			return (0);
		}

	/*
	 * Allocate an object for it.
	 */
	if (pm->pm_tsb_obj == NULL)
		pm->pm_tsb_obj = vm_object_allocate(OBJT_PHYS, TSB_PAGES);

	for (i = 0; i < MAXCPU; i++)
		pm->pm_context[i] = -1;
	CPU_ZERO(&pm->pm_active);

	VM_OBJECT_WLOCK(pm->pm_tsb_obj);
	(void)vm_page_grab_pages(pm->pm_tsb_obj, 0, VM_ALLOC_NORMAL |
	    VM_ALLOC_NOBUSY | VM_ALLOC_WIRED | VM_ALLOC_ZERO, ma, TSB_PAGES);
	VM_OBJECT_WUNLOCK(pm->pm_tsb_obj);
	for (i = 0; i < TSB_PAGES; i++)
		ma[i]->md.pmap = pm;
	pmap_qenter((vm_offset_t)pm->pm_tsb, ma, TSB_PAGES);

	bzero(&pm->pm_stats, sizeof(pm->pm_stats));
	return (1);
}

/*
 * Release any resources held by the given physical map.
 * Called when a pmap initialized by pmap_pinit is being released.
 * Should only be called if the map contains no valid mappings.
 */
void
pmap_release(pmap_t pm)
{
	vm_object_t obj;
	vm_page_t m;
#ifdef SMP
	struct pcpu *pc;
#endif

	CTR2(KTR_PMAP, "pmap_release: ctx=%#x tsb=%p",
	    pm->pm_context[curcpu], pm->pm_tsb);
	KASSERT(pmap_resident_count(pm) == 0,
	    ("pmap_release: resident pages %ld != 0",
	    pmap_resident_count(pm)));

	/*
	 * After the pmap was freed, it might be reallocated to a new process.
	 * When switching, this might lead us to wrongly assume that we need
	 * not switch contexts because old and new pmap pointer are equal.
	 * Therefore, make sure that this pmap is not referenced by any PCPU
	 * pointer any more.  This could happen in two cases:
	 * - A process that referenced the pmap is currently exiting on a CPU.
	 *   However, it is guaranteed to not switch in any more after setting
	 *   its state to PRS_ZOMBIE.
	 * - A process that referenced this pmap ran on a CPU, but we switched
	 *   to a kernel thread, leaving the pmap pointer unchanged.
	 */
#ifdef SMP
	sched_pin();
	STAILQ_FOREACH(pc, &cpuhead, pc_allcpu)
		atomic_cmpset_rel_ptr((uintptr_t *)&pc->pc_pmap,
		    (uintptr_t)pm, (uintptr_t)NULL);
	sched_unpin();
#else
	critical_enter();
	if (PCPU_GET(pmap) == pm)
		PCPU_SET(pmap, NULL);
	critical_exit();
#endif

	pmap_qremove((vm_offset_t)pm->pm_tsb, TSB_PAGES);
	obj = pm->pm_tsb_obj;
	VM_OBJECT_WLOCK(obj);
	KASSERT(obj->ref_count == 1, ("pmap_release: tsbobj ref count != 1"));
	while (!TAILQ_EMPTY(&obj->memq)) {
		m = TAILQ_FIRST(&obj->memq);
		m->md.pmap = NULL;
		vm_page_unwire_noq(m);
		vm_page_free_zero(m);
	}
	VM_OBJECT_WUNLOCK(obj);
}

/*
 * Grow the number of kernel page table entries.  Unneeded.
 */
void
pmap_growkernel(vm_offset_t addr)
{

	panic("pmap_growkernel: can't grow kernel");
}

int
pmap_remove_tte(struct pmap *pm, struct pmap *pm2, struct tte *tp,
    vm_offset_t va)
{
	vm_page_t m;
	u_long data;

	rw_assert(&tte_list_global_lock, RA_WLOCKED);
	data = atomic_readandclear_long(&tp->tte_data);
	if ((data & TD_FAKE) == 0) {
		m = PHYS_TO_VM_PAGE(TD_PA(data));
		TAILQ_REMOVE(&m->md.tte_list, tp, tte_link);
		if ((data & TD_WIRED) != 0)
			pm->pm_stats.wired_count--;
		if ((data & TD_PV) != 0) {
			if ((data & TD_W) != 0)
				vm_page_dirty(m);
			if ((data & TD_REF) != 0)
				vm_page_aflag_set(m, PGA_REFERENCED);
			if (TAILQ_EMPTY(&m->md.tte_list))
				vm_page_aflag_clear(m, PGA_WRITEABLE);
			pm->pm_stats.resident_count--;
		}
		pmap_cache_remove(m, va);
	}
	TTE_ZERO(tp);
	if (PMAP_REMOVE_DONE(pm))
		return (0);
	return (1);
}

/*
 * Remove the given range of addresses from the specified map.
 */
void
pmap_remove(pmap_t pm, vm_offset_t start, vm_offset_t end)
{
	struct tte *tp;
	vm_offset_t va;

	CTR3(KTR_PMAP, "pmap_remove: ctx=%#lx start=%#lx end=%#lx",
	    pm->pm_context[curcpu], start, end);
	if (PMAP_REMOVE_DONE(pm))
		return;
	rw_wlock(&tte_list_global_lock);
	PMAP_LOCK(pm);
	if (end - start > PMAP_TSB_THRESH) {
		tsb_foreach(pm, NULL, start, end, pmap_remove_tte);
		tlb_context_demap(pm);
	} else {
		for (va = start; va < end; va += PAGE_SIZE)
			if ((tp = tsb_tte_lookup(pm, va)) != NULL &&
			    !pmap_remove_tte(pm, NULL, tp, va))
				break;
		tlb_range_demap(pm, start, end - 1);
	}
	PMAP_UNLOCK(pm);
	rw_wunlock(&tte_list_global_lock);
}

void
pmap_remove_all(vm_page_t m)
{
	struct pmap *pm;
	struct tte *tpn;
	struct tte *tp;
	vm_offset_t va;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_remove_all: page %p is not managed", m));
	rw_wlock(&tte_list_global_lock);
	for (tp = TAILQ_FIRST(&m->md.tte_list); tp != NULL; tp = tpn) {
		tpn = TAILQ_NEXT(tp, tte_link);
		if ((tp->tte_data & TD_PV) == 0)
			continue;
		pm = TTE_GET_PMAP(tp);
		va = TTE_GET_VA(tp);
		PMAP_LOCK(pm);
		if ((tp->tte_data & TD_WIRED) != 0)
			pm->pm_stats.wired_count--;
		if ((tp->tte_data & TD_REF) != 0)
			vm_page_aflag_set(m, PGA_REFERENCED);
		if ((tp->tte_data & TD_W) != 0)
			vm_page_dirty(m);
		tp->tte_data &= ~TD_V;
		tlb_page_demap(pm, va);
		TAILQ_REMOVE(&m->md.tte_list, tp, tte_link);
		pm->pm_stats.resident_count--;
		pmap_cache_remove(m, va);
		TTE_ZERO(tp);
		PMAP_UNLOCK(pm);
	}
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	rw_wunlock(&tte_list_global_lock);
}

static int
pmap_protect_tte(struct pmap *pm, struct pmap *pm2, struct tte *tp,
    vm_offset_t va)
{
	u_long data;
	vm_page_t m;

	PMAP_LOCK_ASSERT(pm, MA_OWNED);
	data = atomic_clear_long(&tp->tte_data, TD_SW | TD_W);
	if ((data & (TD_PV | TD_W)) == (TD_PV | TD_W)) {
		m = PHYS_TO_VM_PAGE(TD_PA(data));
		vm_page_dirty(m);
	}
	return (1);
}

/*
 * Set the physical protection on the specified range of this map as requested.
 */
void
pmap_protect(pmap_t pm, vm_offset_t sva, vm_offset_t eva, vm_prot_t prot)
{
	vm_offset_t va;
	struct tte *tp;

	CTR4(KTR_PMAP, "pmap_protect: ctx=%#lx sva=%#lx eva=%#lx prot=%#lx",
	    pm->pm_context[curcpu], sva, eva, prot);

	if ((prot & VM_PROT_READ) == VM_PROT_NONE) {
		pmap_remove(pm, sva, eva);
		return;
	}

	if (prot & VM_PROT_WRITE)
		return;

	PMAP_LOCK(pm);
	if (eva - sva > PMAP_TSB_THRESH) {
		tsb_foreach(pm, NULL, sva, eva, pmap_protect_tte);
		tlb_context_demap(pm);
	} else {
		for (va = sva; va < eva; va += PAGE_SIZE)
			if ((tp = tsb_tte_lookup(pm, va)) != NULL)
				pmap_protect_tte(pm, NULL, tp, va);
		tlb_range_demap(pm, sva, eva - 1);
	}
	PMAP_UNLOCK(pm);
}

/*
 * Map the given physical page at the specified virtual address in the
 * target pmap with the protection requested.  If specified the page
 * will be wired down.
 */
int
pmap_enter(pmap_t pm, vm_offset_t va, vm_page_t m, vm_prot_t prot,
    u_int flags, int8_t psind)
{
	int rv;

	rw_wlock(&tte_list_global_lock);
	PMAP_LOCK(pm);
	rv = pmap_enter_locked(pm, va, m, prot, flags, psind);
	rw_wunlock(&tte_list_global_lock);
	PMAP_UNLOCK(pm);
	return (rv);
}

/*
 * Map the given physical page at the specified virtual address in the
 * target pmap with the protection requested.  If specified the page
 * will be wired down.
 *
 * The page queues and pmap must be locked.
 */
static int
pmap_enter_locked(pmap_t pm, vm_offset_t va, vm_page_t m, vm_prot_t prot,
    u_int flags, int8_t psind __unused)
{
	struct tte *tp;
	vm_paddr_t pa;
	vm_page_t real;
	u_long data;
	boolean_t wired;

	rw_assert(&tte_list_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pm, MA_OWNED);
	if ((m->oflags & VPO_UNMANAGED) == 0 && !vm_page_xbusied(m))
		VM_OBJECT_ASSERT_LOCKED(m->object);
	PMAP_STATS_INC(pmap_nenter);
	pa = VM_PAGE_TO_PHYS(m);
	wired = (flags & PMAP_ENTER_WIRED) != 0;

	/*
	 * If this is a fake page from the device_pager, but it covers actual
	 * physical memory, convert to the real backing page.
	 */
	if ((m->flags & PG_FICTITIOUS) != 0) {
		real = vm_phys_paddr_to_vm_page(pa);
		if (real != NULL)
			m = real;
	}

	CTR6(KTR_PMAP,
	    "pmap_enter_locked: ctx=%p m=%p va=%#lx pa=%#lx prot=%#x wired=%d",
	    pm->pm_context[curcpu], m, va, pa, prot, wired);

	/*
	 * If there is an existing mapping, and the physical address has not
	 * changed, must be protection or wiring change.
	 */
	if ((tp = tsb_tte_lookup(pm, va)) != NULL && TTE_GET_PA(tp) == pa) {
		CTR0(KTR_PMAP, "pmap_enter_locked: update");
		PMAP_STATS_INC(pmap_nenter_update);

		/*
		 * Wiring change, just update stats.
		 */
		if (wired) {
			if ((tp->tte_data & TD_WIRED) == 0) {
				tp->tte_data |= TD_WIRED;
				pm->pm_stats.wired_count++;
			}
		} else {
			if ((tp->tte_data & TD_WIRED) != 0) {
				tp->tte_data &= ~TD_WIRED;
				pm->pm_stats.wired_count--;
			}
		}

		/*
		 * Save the old bits and clear the ones we're interested in.
		 */
		data = tp->tte_data;
		tp->tte_data &= ~(TD_EXEC | TD_SW | TD_W);

		/*
		 * If we're turning off write permissions, sense modify status.
		 */
		if ((prot & VM_PROT_WRITE) != 0) {
			tp->tte_data |= TD_SW;
			if (wired)
				tp->tte_data |= TD_W;
			if ((m->oflags & VPO_UNMANAGED) == 0)
				vm_page_aflag_set(m, PGA_WRITEABLE);
		} else if ((data & TD_W) != 0)
			vm_page_dirty(m);

		/*
		 * If we're turning on execute permissions, flush the icache.
		 */
		if ((prot & VM_PROT_EXECUTE) != 0) {
			if ((data & TD_EXEC) == 0)
				icache_page_inval(pa);
			tp->tte_data |= TD_EXEC;
		}

		/*
		 * Delete the old mapping.
		 */
		tlb_page_demap(pm, TTE_GET_VA(tp));
	} else {
		/*
		 * If there is an existing mapping, but its for a different
		 * physical address, delete the old mapping.
		 */
		if (tp != NULL) {
			CTR0(KTR_PMAP, "pmap_enter_locked: replace");
			PMAP_STATS_INC(pmap_nenter_replace);
			pmap_remove_tte(pm, NULL, tp, va);
			tlb_page_demap(pm, va);
		} else {
			CTR0(KTR_PMAP, "pmap_enter_locked: new");
			PMAP_STATS_INC(pmap_nenter_new);
		}

		/*
		 * Now set up the data and install the new mapping.
		 */
		data = TD_V | TD_8K | TD_PA(pa);
		if (pm == kernel_pmap)
			data |= TD_P;
		if ((prot & VM_PROT_WRITE) != 0) {
			data |= TD_SW;
			if ((m->oflags & VPO_UNMANAGED) == 0)
				vm_page_aflag_set(m, PGA_WRITEABLE);
		}
		if (prot & VM_PROT_EXECUTE) {
			data |= TD_EXEC;
			icache_page_inval(pa);
		}

		/*
		 * If its wired update stats.  We also don't need reference or
		 * modify tracking for wired mappings, so set the bits now.
		 */
		if (wired) {
			pm->pm_stats.wired_count++;
			data |= TD_REF | TD_WIRED;
			if ((prot & VM_PROT_WRITE) != 0)
				data |= TD_W;
		}

		tsb_tte_enter(pm, m, va, TS_8K, data);
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
void
pmap_enter_object(pmap_t pm, vm_offset_t start, vm_offset_t end,
    vm_page_t m_start, vm_prot_t prot)
{
	vm_page_t m;
	vm_pindex_t diff, psize;

	VM_OBJECT_ASSERT_LOCKED(m_start->object);

	psize = atop(end - start);
	m = m_start;
	rw_wlock(&tte_list_global_lock);
	PMAP_LOCK(pm);
	while (m != NULL && (diff = m->pindex - m_start->pindex) < psize) {
		pmap_enter_locked(pm, start + ptoa(diff), m, prot &
		    (VM_PROT_READ | VM_PROT_EXECUTE), 0, 0);
		m = TAILQ_NEXT(m, listq);
	}
	rw_wunlock(&tte_list_global_lock);
	PMAP_UNLOCK(pm);
}

void
pmap_enter_quick(pmap_t pm, vm_offset_t va, vm_page_t m, vm_prot_t prot)
{

	rw_wlock(&tte_list_global_lock);
	PMAP_LOCK(pm);
	pmap_enter_locked(pm, va, m, prot & (VM_PROT_READ | VM_PROT_EXECUTE),
	    0, 0);
	rw_wunlock(&tte_list_global_lock);
	PMAP_UNLOCK(pm);
}

void
pmap_object_init_pt(pmap_t pm, vm_offset_t addr, vm_object_t object,
    vm_pindex_t pindex, vm_size_t size)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(object->type == OBJT_DEVICE || object->type == OBJT_SG,
	    ("pmap_object_init_pt: non-device object"));
}

static int
pmap_unwire_tte(pmap_t pm, pmap_t pm2, struct tte *tp, vm_offset_t va)
{

	PMAP_LOCK_ASSERT(pm, MA_OWNED);
	if ((tp->tte_data & TD_WIRED) == 0)
		panic("pmap_unwire_tte: tp %p is missing TD_WIRED", tp);
	atomic_clear_long(&tp->tte_data, TD_WIRED);
	pm->pm_stats.wired_count--;
	return (1);
}

/*
 * Clear the wired attribute from the mappings for the specified range of
 * addresses in the given pmap.  Every valid mapping within that range must
 * have the wired attribute set.  In contrast, invalid mappings cannot have
 * the wired attribute set, so they are ignored.
 *
 * The wired attribute of the translation table entry is not a hardware
 * feature, so there is no need to invalidate any TLB entries.
 */
void
pmap_unwire(pmap_t pm, vm_offset_t sva, vm_offset_t eva)
{
	vm_offset_t va;
	struct tte *tp;

	PMAP_LOCK(pm);
	if (eva - sva > PMAP_TSB_THRESH)
		tsb_foreach(pm, NULL, sva, eva, pmap_unwire_tte);
	else {
		for (va = sva; va < eva; va += PAGE_SIZE)
			if ((tp = tsb_tte_lookup(pm, va)) != NULL)
				pmap_unwire_tte(pm, NULL, tp, va);
	}
	PMAP_UNLOCK(pm);
}

static int
pmap_copy_tte(pmap_t src_pmap, pmap_t dst_pmap, struct tte *tp,
    vm_offset_t va)
{
	vm_page_t m;
	u_long data;

	if ((tp->tte_data & TD_FAKE) != 0)
		return (1);
	if (tsb_tte_lookup(dst_pmap, va) == NULL) {
		data = tp->tte_data &
		    ~(TD_PV | TD_REF | TD_SW | TD_CV | TD_W);
		m = PHYS_TO_VM_PAGE(TTE_GET_PA(tp));
		tsb_tte_enter(dst_pmap, m, va, TS_8K, data);
	}
	return (1);
}

void
pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, vm_offset_t dst_addr,
    vm_size_t len, vm_offset_t src_addr)
{
	struct tte *tp;
	vm_offset_t va;

	if (dst_addr != src_addr)
		return;
	rw_wlock(&tte_list_global_lock);
	if (dst_pmap < src_pmap) {
		PMAP_LOCK(dst_pmap);
		PMAP_LOCK(src_pmap);
	} else {
		PMAP_LOCK(src_pmap);
		PMAP_LOCK(dst_pmap);
	}
	if (len > PMAP_TSB_THRESH) {
		tsb_foreach(src_pmap, dst_pmap, src_addr, src_addr + len,
		    pmap_copy_tte);
		tlb_context_demap(dst_pmap);
	} else {
		for (va = src_addr; va < src_addr + len; va += PAGE_SIZE)
			if ((tp = tsb_tte_lookup(src_pmap, va)) != NULL)
				pmap_copy_tte(src_pmap, dst_pmap, tp, va);
		tlb_range_demap(dst_pmap, src_addr, src_addr + len - 1);
	}
	rw_wunlock(&tte_list_global_lock);
	PMAP_UNLOCK(src_pmap);
	PMAP_UNLOCK(dst_pmap);
}

void
pmap_zero_page(vm_page_t m)
{
	struct tte *tp;
	vm_offset_t va;
	vm_paddr_t pa;

	KASSERT((m->flags & PG_FICTITIOUS) == 0,
	    ("pmap_zero_page: fake page"));
	PMAP_STATS_INC(pmap_nzero_page);
	pa = VM_PAGE_TO_PHYS(m);
	if (dcache_color_ignore != 0 || m->md.color == DCACHE_COLOR(pa)) {
		PMAP_STATS_INC(pmap_nzero_page_c);
		va = TLB_PHYS_TO_DIRECT(pa);
		cpu_block_zero((void *)va, PAGE_SIZE);
	} else if (m->md.color == -1) {
		PMAP_STATS_INC(pmap_nzero_page_nc);
		aszero(ASI_PHYS_USE_EC, pa, PAGE_SIZE);
	} else {
		PMAP_STATS_INC(pmap_nzero_page_oc);
		PMAP_LOCK(kernel_pmap);
		va = pmap_temp_map_1 + (m->md.color * PAGE_SIZE);
		tp = tsb_kvtotte(va);
		tp->tte_data = TD_V | TD_8K | TD_PA(pa) | TD_CP | TD_CV | TD_W;
		tp->tte_vpn = TV_VPN(va, TS_8K);
		cpu_block_zero((void *)va, PAGE_SIZE);
		tlb_page_demap(kernel_pmap, va);
		PMAP_UNLOCK(kernel_pmap);
	}
}

void
pmap_zero_page_area(vm_page_t m, int off, int size)
{
	struct tte *tp;
	vm_offset_t va;
	vm_paddr_t pa;

	KASSERT((m->flags & PG_FICTITIOUS) == 0,
	    ("pmap_zero_page_area: fake page"));
	KASSERT(off + size <= PAGE_SIZE, ("pmap_zero_page_area: bad off/size"));
	PMAP_STATS_INC(pmap_nzero_page_area);
	pa = VM_PAGE_TO_PHYS(m);
	if (dcache_color_ignore != 0 || m->md.color == DCACHE_COLOR(pa)) {
		PMAP_STATS_INC(pmap_nzero_page_area_c);
		va = TLB_PHYS_TO_DIRECT(pa);
		bzero((void *)(va + off), size);
	} else if (m->md.color == -1) {
		PMAP_STATS_INC(pmap_nzero_page_area_nc);
		aszero(ASI_PHYS_USE_EC, pa + off, size);
	} else {
		PMAP_STATS_INC(pmap_nzero_page_area_oc);
		PMAP_LOCK(kernel_pmap);
		va = pmap_temp_map_1 + (m->md.color * PAGE_SIZE);
		tp = tsb_kvtotte(va);
		tp->tte_data = TD_V | TD_8K | TD_PA(pa) | TD_CP | TD_CV | TD_W;
		tp->tte_vpn = TV_VPN(va, TS_8K);
		bzero((void *)(va + off), size);
		tlb_page_demap(kernel_pmap, va);
		PMAP_UNLOCK(kernel_pmap);
	}
}

void
pmap_copy_page(vm_page_t msrc, vm_page_t mdst)
{
	vm_offset_t vdst;
	vm_offset_t vsrc;
	vm_paddr_t pdst;
	vm_paddr_t psrc;
	struct tte *tp;

	KASSERT((mdst->flags & PG_FICTITIOUS) == 0,
	    ("pmap_copy_page: fake dst page"));
	KASSERT((msrc->flags & PG_FICTITIOUS) == 0,
	    ("pmap_copy_page: fake src page"));
	PMAP_STATS_INC(pmap_ncopy_page);
	pdst = VM_PAGE_TO_PHYS(mdst);
	psrc = VM_PAGE_TO_PHYS(msrc);
	if (dcache_color_ignore != 0 ||
	    (msrc->md.color == DCACHE_COLOR(psrc) &&
	    mdst->md.color == DCACHE_COLOR(pdst))) {
		PMAP_STATS_INC(pmap_ncopy_page_c);
		vdst = TLB_PHYS_TO_DIRECT(pdst);
		vsrc = TLB_PHYS_TO_DIRECT(psrc);
		cpu_block_copy((void *)vsrc, (void *)vdst, PAGE_SIZE);
	} else if (msrc->md.color == -1 && mdst->md.color == -1) {
		PMAP_STATS_INC(pmap_ncopy_page_nc);
		ascopy(ASI_PHYS_USE_EC, psrc, pdst, PAGE_SIZE);
	} else if (msrc->md.color == -1) {
		if (mdst->md.color == DCACHE_COLOR(pdst)) {
			PMAP_STATS_INC(pmap_ncopy_page_dc);
			vdst = TLB_PHYS_TO_DIRECT(pdst);
			ascopyfrom(ASI_PHYS_USE_EC, psrc, (void *)vdst,
			    PAGE_SIZE);
		} else {
			PMAP_STATS_INC(pmap_ncopy_page_doc);
			PMAP_LOCK(kernel_pmap);
			vdst = pmap_temp_map_1 + (mdst->md.color * PAGE_SIZE);
			tp = tsb_kvtotte(vdst);
			tp->tte_data =
			    TD_V | TD_8K | TD_PA(pdst) | TD_CP | TD_CV | TD_W;
			tp->tte_vpn = TV_VPN(vdst, TS_8K);
			ascopyfrom(ASI_PHYS_USE_EC, psrc, (void *)vdst,
			    PAGE_SIZE);
			tlb_page_demap(kernel_pmap, vdst);
			PMAP_UNLOCK(kernel_pmap);
		}
	} else if (mdst->md.color == -1) {
		if (msrc->md.color == DCACHE_COLOR(psrc)) {
			PMAP_STATS_INC(pmap_ncopy_page_sc);
			vsrc = TLB_PHYS_TO_DIRECT(psrc);
			ascopyto((void *)vsrc, ASI_PHYS_USE_EC, pdst,
			    PAGE_SIZE);
		} else {
			PMAP_STATS_INC(pmap_ncopy_page_soc);
			PMAP_LOCK(kernel_pmap);
			vsrc = pmap_temp_map_1 + (msrc->md.color * PAGE_SIZE);
			tp = tsb_kvtotte(vsrc);
			tp->tte_data =
			    TD_V | TD_8K | TD_PA(psrc) | TD_CP | TD_CV | TD_W;
			tp->tte_vpn = TV_VPN(vsrc, TS_8K);
			ascopyto((void *)vsrc, ASI_PHYS_USE_EC, pdst,
			    PAGE_SIZE);
			tlb_page_demap(kernel_pmap, vsrc);
			PMAP_UNLOCK(kernel_pmap);
		}
	} else {
		PMAP_STATS_INC(pmap_ncopy_page_oc);
		PMAP_LOCK(kernel_pmap);
		vdst = pmap_temp_map_1 + (mdst->md.color * PAGE_SIZE);
		tp = tsb_kvtotte(vdst);
		tp->tte_data =
		    TD_V | TD_8K | TD_PA(pdst) | TD_CP | TD_CV | TD_W;
		tp->tte_vpn = TV_VPN(vdst, TS_8K);
		vsrc = pmap_temp_map_2 + (msrc->md.color * PAGE_SIZE);
		tp = tsb_kvtotte(vsrc);
		tp->tte_data =
		    TD_V | TD_8K | TD_PA(psrc) | TD_CP | TD_CV | TD_W;
		tp->tte_vpn = TV_VPN(vsrc, TS_8K);
		cpu_block_copy((void *)vsrc, (void *)vdst, PAGE_SIZE);
		tlb_page_demap(kernel_pmap, vdst);
		tlb_page_demap(kernel_pmap, vsrc);
		PMAP_UNLOCK(kernel_pmap);
	}
}

vm_offset_t
pmap_quick_enter_page(vm_page_t m)
{
	vm_paddr_t pa;
	vm_offset_t qaddr;
	struct tte *tp;

	pa = VM_PAGE_TO_PHYS(m);
	if (dcache_color_ignore != 0 || m->md.color == DCACHE_COLOR(pa))
		return (TLB_PHYS_TO_DIRECT(pa));

	critical_enter();
	qaddr = PCPU_GET(qmap_addr);
	qaddr += (PAGE_SIZE * ((DCACHE_COLORS + DCACHE_COLOR(pa) -
	    DCACHE_COLOR(qaddr)) % DCACHE_COLORS));
	tp = tsb_kvtotte(qaddr);

	KASSERT(tp->tte_data == 0, ("pmap_quick_enter_page: PTE busy"));
	
	tp->tte_data = TD_V | TD_8K | TD_PA(pa) | TD_CP | TD_CV | TD_W;
	tp->tte_vpn = TV_VPN(qaddr, TS_8K);

	return (qaddr);
}

void
pmap_quick_remove_page(vm_offset_t addr)
{
	vm_offset_t qaddr;
	struct tte *tp;

	if (addr >= VM_MIN_DIRECT_ADDRESS)
		return;

	tp = tsb_kvtotte(addr);
	qaddr = PCPU_GET(qmap_addr);
	
	KASSERT((addr >= qaddr) && (addr < (qaddr + (PAGE_SIZE * DCACHE_COLORS))),
	    ("pmap_quick_remove_page: invalid address"));
	KASSERT(tp->tte_data != 0, ("pmap_quick_remove_page: PTE not in use"));
	
	stxa(TLB_DEMAP_VA(addr) | TLB_DEMAP_NUCLEUS | TLB_DEMAP_PAGE, ASI_DMMU_DEMAP, 0);
	stxa(TLB_DEMAP_VA(addr) | TLB_DEMAP_NUCLEUS | TLB_DEMAP_PAGE, ASI_IMMU_DEMAP, 0);
	flush(KERNBASE);
	TTE_ZERO(tp);
	critical_exit();
}

int unmapped_buf_allowed;

void
pmap_copy_pages(vm_page_t ma[], vm_offset_t a_offset, vm_page_t mb[],
    vm_offset_t b_offset, int xfersize)
{

	panic("pmap_copy_pages: not implemented");
}

/*
 * Returns true if the pmap's pv is one of the first
 * 16 pvs linked to from this page.  This count may
 * be changed upwards or downwards in the future; it
 * is only necessary that true be returned for a small
 * subset of pmaps for proper page aging.
 */
boolean_t
pmap_page_exists_quick(pmap_t pm, vm_page_t m)
{
	struct tte *tp;
	int loops;
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_page_exists_quick: page %p is not managed", m));
	loops = 0;
	rv = FALSE;
	rw_wlock(&tte_list_global_lock);
	TAILQ_FOREACH(tp, &m->md.tte_list, tte_link) {
		if ((tp->tte_data & TD_PV) == 0)
			continue;
		if (TTE_GET_PMAP(tp) == pm) {
			rv = TRUE;
			break;
		}
		if (++loops >= 16)
			break;
	}
	rw_wunlock(&tte_list_global_lock);
	return (rv);
}

/*
 * Return the number of managed mappings to the given physical page
 * that are wired.
 */
int
pmap_page_wired_mappings(vm_page_t m)
{
	struct tte *tp;
	int count;

	count = 0;
	if ((m->oflags & VPO_UNMANAGED) != 0)
		return (count);
	rw_wlock(&tte_list_global_lock);
	TAILQ_FOREACH(tp, &m->md.tte_list, tte_link)
		if ((tp->tte_data & (TD_PV | TD_WIRED)) == (TD_PV | TD_WIRED))
			count++;
	rw_wunlock(&tte_list_global_lock);
	return (count);
}

/*
 * Remove all pages from specified address space, this aids process exit
 * speeds.  This is much faster than pmap_remove in the case of running down
 * an entire address space.  Only works for the current pmap.
 */
void
pmap_remove_pages(pmap_t pm)
{

}

/*
 * Returns TRUE if the given page has a managed mapping.
 */
boolean_t
pmap_page_is_mapped(vm_page_t m)
{
	struct tte *tp;
	boolean_t rv;

	rv = FALSE;
	if ((m->oflags & VPO_UNMANAGED) != 0)
		return (rv);
	rw_wlock(&tte_list_global_lock);
	TAILQ_FOREACH(tp, &m->md.tte_list, tte_link)
		if ((tp->tte_data & TD_PV) != 0) {
			rv = TRUE;
			break;
		}
	rw_wunlock(&tte_list_global_lock);
	return (rv);
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
int
pmap_ts_referenced(vm_page_t m)
{
	struct tte *tpf;
	struct tte *tpn;
	struct tte *tp;
	u_long data;
	int count;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_ts_referenced: page %p is not managed", m));
	count = 0;
	rw_wlock(&tte_list_global_lock);
	if ((tp = TAILQ_FIRST(&m->md.tte_list)) != NULL) {
		tpf = tp;
		do {
			tpn = TAILQ_NEXT(tp, tte_link);
			TAILQ_REMOVE(&m->md.tte_list, tp, tte_link);
			TAILQ_INSERT_TAIL(&m->md.tte_list, tp, tte_link);
			if ((tp->tte_data & TD_PV) == 0)
				continue;
			data = atomic_clear_long(&tp->tte_data, TD_REF);
			if ((data & TD_W) != 0)
				vm_page_dirty(m);
			if ((data & TD_REF) != 0 && ++count >=
			    PMAP_TS_REFERENCED_MAX)
				break;
		} while ((tp = tpn) != NULL && tp != tpf);
	}
	rw_wunlock(&tte_list_global_lock);
	return (count);
}

boolean_t
pmap_is_modified(vm_page_t m)
{
	struct tte *tp;
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_is_modified: page %p is not managed", m));
	rv = FALSE;

	/*
	 * If the page is not exclusive busied, then PGA_WRITEABLE cannot be
	 * concurrently set while the object is locked.  Thus, if PGA_WRITEABLE
	 * is clear, no TTEs can have TD_W set.
	 */
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (!vm_page_xbusied(m) && (m->aflags & PGA_WRITEABLE) == 0)
		return (rv);
	rw_wlock(&tte_list_global_lock);
	TAILQ_FOREACH(tp, &m->md.tte_list, tte_link) {
		if ((tp->tte_data & TD_PV) == 0)
			continue;
		if ((tp->tte_data & TD_W) != 0) {
			rv = TRUE;
			break;
		}
	}
	rw_wunlock(&tte_list_global_lock);
	return (rv);
}

/*
 *	pmap_is_prefaultable:
 *
 *	Return whether or not the specified virtual address is elgible
 *	for prefault.
 */
boolean_t
pmap_is_prefaultable(pmap_t pmap, vm_offset_t addr)
{
	boolean_t rv;

	PMAP_LOCK(pmap);
	rv = tsb_tte_lookup(pmap, addr) == NULL;
	PMAP_UNLOCK(pmap);
	return (rv);
}

/*
 * Return whether or not the specified physical page was referenced
 * in any physical maps.
 */
boolean_t
pmap_is_referenced(vm_page_t m)
{
	struct tte *tp;
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_is_referenced: page %p is not managed", m));
	rv = FALSE;
	rw_wlock(&tte_list_global_lock);
	TAILQ_FOREACH(tp, &m->md.tte_list, tte_link) {
		if ((tp->tte_data & TD_PV) == 0)
			continue;
		if ((tp->tte_data & TD_REF) != 0) {
			rv = TRUE;
			break;
		}
	}
	rw_wunlock(&tte_list_global_lock);
	return (rv);
}

/*
 * This function is advisory.
 */
void
pmap_advise(pmap_t pmap, vm_offset_t sva, vm_offset_t eva, int advice)
{
}

void
pmap_clear_modify(vm_page_t m)
{
	struct tte *tp;
	u_long data;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_clear_modify: page %p is not managed", m));
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	KASSERT(!vm_page_xbusied(m),
	    ("pmap_clear_modify: page %p is exclusive busied", m));

	/*
	 * If the page is not PGA_WRITEABLE, then no TTEs can have TD_W set.
	 * If the object containing the page is locked and the page is not
	 * exclusive busied, then PGA_WRITEABLE cannot be concurrently set.
	 */
	if ((m->aflags & PGA_WRITEABLE) == 0)
		return;
	rw_wlock(&tte_list_global_lock);
	TAILQ_FOREACH(tp, &m->md.tte_list, tte_link) {
		if ((tp->tte_data & TD_PV) == 0)
			continue;
		data = atomic_clear_long(&tp->tte_data, TD_W);
		if ((data & TD_W) != 0)
			tlb_page_demap(TTE_GET_PMAP(tp), TTE_GET_VA(tp));
	}
	rw_wunlock(&tte_list_global_lock);
}

void
pmap_remove_write(vm_page_t m)
{
	struct tte *tp;
	u_long data;

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
	rw_wlock(&tte_list_global_lock);
	TAILQ_FOREACH(tp, &m->md.tte_list, tte_link) {
		if ((tp->tte_data & TD_PV) == 0)
			continue;
		data = atomic_clear_long(&tp->tte_data, TD_SW | TD_W);
		if ((data & TD_W) != 0) {
			vm_page_dirty(m);
			tlb_page_demap(TTE_GET_PMAP(tp), TTE_GET_VA(tp));
		}
	}
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	rw_wunlock(&tte_list_global_lock);
}

int
pmap_mincore(pmap_t pm, vm_offset_t addr, vm_paddr_t *locked_pa)
{

	/* TODO; */
	return (0);
}

/*
 * Activate a user pmap.  The pmap must be activated before its address space
 * can be accessed in any way.
 */
void
pmap_activate(struct thread *td)
{
	struct vmspace *vm;
	struct pmap *pm;
	int context;

	critical_enter();
	vm = td->td_proc->p_vmspace;
	pm = vmspace_pmap(vm);

	context = PCPU_GET(tlb_ctx);
	if (context == PCPU_GET(tlb_ctx_max)) {
		tlb_flush_user();
		context = PCPU_GET(tlb_ctx_min);
	}
	PCPU_SET(tlb_ctx, context + 1);

	pm->pm_context[curcpu] = context;
#ifdef SMP
	CPU_SET_ATOMIC(PCPU_GET(cpuid), &pm->pm_active);
	atomic_store_acq_ptr((uintptr_t *)PCPU_PTR(pmap), (uintptr_t)pm);
#else
	CPU_SET(PCPU_GET(cpuid), &pm->pm_active);
	PCPU_SET(pmap, pm);
#endif

	stxa(AA_DMMU_TSB, ASI_DMMU, pm->pm_tsb);
	stxa(AA_IMMU_TSB, ASI_IMMU, pm->pm_tsb);
	stxa(AA_DMMU_PCXR, ASI_DMMU, (ldxa(AA_DMMU_PCXR, ASI_DMMU) &
	    TLB_CXR_PGSZ_MASK) | context);
	flush(KERNBASE);
	critical_exit();
}

void
pmap_sync_icache(pmap_t pm, vm_offset_t va, vm_size_t sz)
{

}

/*
 * Increase the starting virtual address of the given mapping if a
 * different alignment might result in more superpage mappings.
 */
void
pmap_align_superpage(vm_object_t object, vm_ooffset_t offset,
    vm_offset_t *addr, vm_size_t size)
{

}

boolean_t
pmap_is_valid_memattr(pmap_t pmap __unused, vm_memattr_t mode)
{

	return (mode == VM_MEMATTR_DEFAULT);
}
