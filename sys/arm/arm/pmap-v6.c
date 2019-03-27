/*-
 * SPDX-License-Identifier: BSD-3-Clause AND BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1991 Regents of the University of California.
 * Copyright (c) 1994 John S. Dyson
 * Copyright (c) 1994 David Greenman
 * Copyright (c) 2005-2010 Alan L. Cox <alc@cs.rice.edu>
 * Copyright (c) 2014-2016 Svatopluk Kraus <skra@FreeBSD.org>
 * Copyright (c) 2014-2016 Michal Meloun <mmel@FreeBSD.org>
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
 *	from:	@(#)pmap.c	7.7 (Berkeley)	5/12/91
 */
/*-
 * Copyright (c) 2003 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Jake Burkholder,
 * Safeport Network Services, and Network Associates Laboratories, the
 * Security Research Division of Network Associates, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
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

#include "opt_vm.h"
#include "opt_pmap.h"
#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/malloc.h>
#include <sys/vmmeter.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/sf_buf.h>
#include <sys/smp.h>
#include <sys/sched.h>
#include <sys/sysctl.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <machine/physmem.h>

#include <vm/vm.h>
#include <vm/uma.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_phys.h>
#include <vm/vm_extern.h>
#include <vm/vm_reserv.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/md_var.h>
#include <machine/pmap_var.h>
#include <machine/cpu.h>
#include <machine/pcb.h>
#include <machine/sf_buf.h>
#ifdef SMP
#include <machine/smp.h>
#endif
#ifndef PMAP_SHPGPERPROC
#define PMAP_SHPGPERPROC 200
#endif

#ifndef DIAGNOSTIC
#define PMAP_INLINE	__inline
#else
#define PMAP_INLINE
#endif

#ifdef PMAP_DEBUG
static void pmap_zero_page_check(vm_page_t m);
void pmap_debug(int level);
int pmap_pid_dump(int pid);

#define PDEBUG(_lev_,_stat_) \
	if (pmap_debug_level >= (_lev_)) \
		((_stat_))
#define dprintf printf
int pmap_debug_level = 1;
#else   /* PMAP_DEBUG */
#define PDEBUG(_lev_,_stat_) /* Nothing */
#define dprintf(x, arg...)
#endif  /* PMAP_DEBUG */

/*
 *  Level 2 page tables map definion ('max' is excluded).
 */

#define PT2V_MIN_ADDRESS	((vm_offset_t)PT2MAP)
#define PT2V_MAX_ADDRESS	((vm_offset_t)PT2MAP + PT2MAP_SIZE)

#define UPT2V_MIN_ADDRESS	((vm_offset_t)PT2MAP)
#define UPT2V_MAX_ADDRESS \
    ((vm_offset_t)(PT2MAP + (KERNBASE >> PT2MAP_SHIFT)))

/*
 *  Promotion to a 1MB (PTE1) page mapping requires that the corresponding
 *  4KB (PTE2) page mappings have identical settings for the following fields:
 */
#define PTE2_PROMOTE	(PTE2_V | PTE2_A | PTE2_NM | PTE2_S | PTE2_NG |	\
			 PTE2_NX | PTE2_RO | PTE2_U | PTE2_W |		\
			 PTE2_ATTR_MASK)

#define PTE1_PROMOTE	(PTE1_V | PTE1_A | PTE1_NM | PTE1_S | PTE1_NG |	\
			 PTE1_NX | PTE1_RO | PTE1_U | PTE1_W |		\
			 PTE1_ATTR_MASK)

#define ATTR_TO_L1(l2_attr)	((((l2_attr) & L2_TEX0) ? L1_S_TEX0 : 0) | \
				 (((l2_attr) & L2_C)    ? L1_S_C    : 0) | \
				 (((l2_attr) & L2_B)    ? L1_S_B    : 0) | \
				 (((l2_attr) & PTE2_A)  ? PTE1_A    : 0) | \
				 (((l2_attr) & PTE2_NM) ? PTE1_NM   : 0) | \
				 (((l2_attr) & PTE2_S)  ? PTE1_S    : 0) | \
				 (((l2_attr) & PTE2_NG) ? PTE1_NG   : 0) | \
				 (((l2_attr) & PTE2_NX) ? PTE1_NX   : 0) | \
				 (((l2_attr) & PTE2_RO) ? PTE1_RO   : 0) | \
				 (((l2_attr) & PTE2_U)  ? PTE1_U    : 0) | \
				 (((l2_attr) & PTE2_W)  ? PTE1_W    : 0))

#define ATTR_TO_L2(l1_attr)	((((l1_attr) & L1_S_TEX0) ? L2_TEX0 : 0) | \
				 (((l1_attr) & L1_S_C)    ? L2_C    : 0) | \
				 (((l1_attr) & L1_S_B)    ? L2_B    : 0) | \
				 (((l1_attr) & PTE1_A)    ? PTE2_A  : 0) | \
				 (((l1_attr) & PTE1_NM)   ? PTE2_NM : 0) | \
				 (((l1_attr) & PTE1_S)    ? PTE2_S  : 0) | \
				 (((l1_attr) & PTE1_NG)   ? PTE2_NG : 0) | \
				 (((l1_attr) & PTE1_NX)   ? PTE2_NX : 0) | \
				 (((l1_attr) & PTE1_RO)   ? PTE2_RO : 0) | \
				 (((l1_attr) & PTE1_U)    ? PTE2_U  : 0) | \
				 (((l1_attr) & PTE1_W)    ? PTE2_W  : 0))

/*
 *  PTE2 descriptors creation macros.
 */
#define PTE2_ATTR_DEFAULT	vm_memattr_to_pte2(VM_MEMATTR_DEFAULT)
#define PTE2_ATTR_PT		vm_memattr_to_pte2(pt_memattr)

#define PTE2_KPT(pa)	PTE2_KERN(pa, PTE2_AP_KRW, PTE2_ATTR_PT)
#define PTE2_KPT_NG(pa)	PTE2_KERN_NG(pa, PTE2_AP_KRW, PTE2_ATTR_PT)

#define PTE2_KRW(pa)	PTE2_KERN(pa, PTE2_AP_KRW, PTE2_ATTR_DEFAULT)
#define PTE2_KRO(pa)	PTE2_KERN(pa, PTE2_AP_KR, PTE2_ATTR_DEFAULT)

#define PV_STATS
#ifdef PV_STATS
#define PV_STAT(x)	do { x ; } while (0)
#else
#define PV_STAT(x)	do { } while (0)
#endif

/*
 *  The boot_pt1 is used temporary in very early boot stage as L1 page table.
 *  We can init many things with no memory allocation thanks to its static
 *  allocation and this brings two main advantages:
 *  (1) other cores can be started very simply,
 *  (2) various boot loaders can be supported as its arguments can be processed
 *      in virtual address space and can be moved to safe location before
 *      first allocation happened.
 *  Only disadvantage is that boot_pt1 is used only in very early boot stage.
 *  However, the table is uninitialized and so lays in bss. Therefore kernel
 *  image size is not influenced.
 *
 *  QQQ: In the future, maybe, boot_pt1 can be used for soft reset and
 *       CPU suspend/resume game.
 */
extern pt1_entry_t boot_pt1[];

vm_paddr_t base_pt1;
pt1_entry_t *kern_pt1;
pt2_entry_t *kern_pt2tab;
pt2_entry_t *PT2MAP;

static uint32_t ttb_flags;
static vm_memattr_t pt_memattr;
ttb_entry_t pmap_kern_ttb;

struct pmap kernel_pmap_store;
LIST_HEAD(pmaplist, pmap);
static struct pmaplist allpmaps;
static struct mtx allpmaps_lock;

vm_offset_t virtual_avail;	/* VA of first avail page (after kernel bss) */
vm_offset_t virtual_end;	/* VA of last avail page (end of kernel AS) */

static vm_offset_t kernel_vm_end_new;
vm_offset_t kernel_vm_end = KERNBASE + NKPT2PG * NPT2_IN_PG * PTE1_SIZE;
vm_offset_t vm_max_kernel_address;
vm_paddr_t kernel_l1pa;

static struct rwlock __aligned(CACHE_LINE_SIZE) pvh_global_lock;

/*
 *  Data for the pv entry allocation mechanism
 */
static TAILQ_HEAD(pch, pv_chunk) pv_chunks = TAILQ_HEAD_INITIALIZER(pv_chunks);
static int pv_entry_count = 0, pv_entry_max = 0, pv_entry_high_water = 0;
static struct md_page *pv_table; /* XXX: Is it used only the list in md_page? */
static int shpgperproc = PMAP_SHPGPERPROC;

struct pv_chunk *pv_chunkbase;		/* KVA block for pv_chunks */
int pv_maxchunks;			/* How many chunks we have KVA for */
vm_offset_t pv_vafree;			/* freelist stored in the PTE */

vm_paddr_t first_managed_pa;
#define	pa_to_pvh(pa)	(&pv_table[pte1_index(pa - first_managed_pa)])

/*
 *  All those kernel PT submaps that BSD is so fond of
 */
caddr_t _tmppt = 0;

/*
 *  Crashdump maps.
 */
static caddr_t crashdumpmap;

static pt2_entry_t *PMAP1 = NULL, *PMAP2;
static pt2_entry_t *PADDR1 = NULL, *PADDR2;
#ifdef DDB
static pt2_entry_t *PMAP3;
static pt2_entry_t *PADDR3;
static int PMAP3cpu __unused; /* for SMP only */
#endif
#ifdef SMP
static int PMAP1cpu;
static int PMAP1changedcpu;
SYSCTL_INT(_debug, OID_AUTO, PMAP1changedcpu, CTLFLAG_RD,
    &PMAP1changedcpu, 0,
    "Number of times pmap_pte2_quick changed CPU with same PMAP1");
#endif
static int PMAP1changed;
SYSCTL_INT(_debug, OID_AUTO, PMAP1changed, CTLFLAG_RD,
    &PMAP1changed, 0,
    "Number of times pmap_pte2_quick changed PMAP1");
static int PMAP1unchanged;
SYSCTL_INT(_debug, OID_AUTO, PMAP1unchanged, CTLFLAG_RD,
    &PMAP1unchanged, 0,
    "Number of times pmap_pte2_quick didn't change PMAP1");
static struct mtx PMAP2mutex;

/*
 * Internal flags for pmap_enter()'s helper functions.
 */
#define	PMAP_ENTER_NORECLAIM	0x1000000	/* Don't reclaim PV entries. */
#define	PMAP_ENTER_NOREPLACE	0x2000000	/* Don't replace mappings. */

static __inline void pt2_wirecount_init(vm_page_t m);
static boolean_t pmap_demote_pte1(pmap_t pmap, pt1_entry_t *pte1p,
    vm_offset_t va);
static int pmap_enter_pte1(pmap_t pmap, vm_offset_t va, pt1_entry_t pte1,
    u_int flags, vm_page_t m);
void cache_icache_sync_fresh(vm_offset_t va, vm_paddr_t pa, vm_size_t size);

/*
 *  Function to set the debug level of the pmap code.
 */
#ifdef PMAP_DEBUG
void
pmap_debug(int level)
{

	pmap_debug_level = level;
	dprintf("pmap_debug: level=%d\n", pmap_debug_level);
}
#endif /* PMAP_DEBUG */

/*
 *  This table must corespond with memory attribute configuration in vm.h.
 *  First entry is used for normal system mapping.
 *
 *  Device memory is always marked as shared.
 *  Normal memory is shared only in SMP .
 *  Not outer shareable bits are not used yet.
 *  Class 6 cannot be used on ARM11.
 */
#define TEXDEF_TYPE_SHIFT	0
#define TEXDEF_TYPE_MASK	0x3
#define TEXDEF_INNER_SHIFT	2
#define TEXDEF_INNER_MASK	0x3
#define TEXDEF_OUTER_SHIFT	4
#define TEXDEF_OUTER_MASK	0x3
#define TEXDEF_NOS_SHIFT	6
#define TEXDEF_NOS_MASK		0x1

#define TEX(t, i, o, s) 			\
		((t) << TEXDEF_TYPE_SHIFT) |	\
		((i) << TEXDEF_INNER_SHIFT) |	\
		((o) << TEXDEF_OUTER_SHIFT | 	\
		((s) << TEXDEF_NOS_SHIFT))

static uint32_t tex_class[8] = {
/*	    type      inner cache outer cache */
	TEX(PRRR_MEM, NMRR_WB_WA, NMRR_WB_WA, 0),  /* 0 - ATTR_WB_WA	*/
	TEX(PRRR_MEM, NMRR_NC,	  NMRR_NC,    0),  /* 1 - ATTR_NOCACHE	*/
	TEX(PRRR_DEV, NMRR_NC,	  NMRR_NC,    0),  /* 2 - ATTR_DEVICE	*/
	TEX(PRRR_SO,  NMRR_NC,	  NMRR_NC,    0),  /* 3 - ATTR_SO	*/
	TEX(PRRR_MEM, NMRR_WT,	  NMRR_WT,    0),  /* 4 - ATTR_WT	*/
	TEX(PRRR_MEM, NMRR_NC,	  NMRR_NC,    0),  /* 5 - NOT USED YET	*/
	TEX(PRRR_MEM, NMRR_NC,	  NMRR_NC,    0),  /* 6 - NOT USED YET	*/
	TEX(PRRR_MEM, NMRR_NC,	  NMRR_NC,    0),  /* 7 - NOT USED YET	*/
};
#undef TEX

static uint32_t pte2_attr_tab[8] = {
	PTE2_ATTR_WB_WA,	/* 0 - VM_MEMATTR_WB_WA */
	PTE2_ATTR_NOCACHE,	/* 1 - VM_MEMATTR_NOCACHE */
	PTE2_ATTR_DEVICE,	/* 2 - VM_MEMATTR_DEVICE */
	PTE2_ATTR_SO,		/* 3 - VM_MEMATTR_SO */
	PTE2_ATTR_WT,		/* 4 - VM_MEMATTR_WRITE_THROUGH */
	0,			/* 5 - NOT USED YET */
	0,			/* 6 - NOT USED YET */
	0			/* 7 - NOT USED YET */
};
CTASSERT(VM_MEMATTR_WB_WA == 0);
CTASSERT(VM_MEMATTR_NOCACHE == 1);
CTASSERT(VM_MEMATTR_DEVICE == 2);
CTASSERT(VM_MEMATTR_SO == 3);
CTASSERT(VM_MEMATTR_WRITE_THROUGH == 4);
#define	VM_MEMATTR_END	(VM_MEMATTR_WRITE_THROUGH + 1)

boolean_t
pmap_is_valid_memattr(pmap_t pmap __unused, vm_memattr_t mode)
{

	return (mode >= 0 && mode < VM_MEMATTR_END);
}

static inline uint32_t
vm_memattr_to_pte2(vm_memattr_t ma)
{

	KASSERT((u_int)ma < VM_MEMATTR_END,
	    ("%s: bad vm_memattr_t %d", __func__, ma));
	return (pte2_attr_tab[(u_int)ma]);
}

static inline uint32_t
vm_page_pte2_attr(vm_page_t m)
{

	return (vm_memattr_to_pte2(m->md.pat_mode));
}

/*
 * Convert TEX definition entry to TTB flags.
 */
static uint32_t
encode_ttb_flags(int idx)
{
	uint32_t inner, outer, nos, reg;

	inner = (tex_class[idx] >> TEXDEF_INNER_SHIFT) &
		TEXDEF_INNER_MASK;
	outer = (tex_class[idx] >> TEXDEF_OUTER_SHIFT) &
		TEXDEF_OUTER_MASK;
	nos = (tex_class[idx] >> TEXDEF_NOS_SHIFT) &
		TEXDEF_NOS_MASK;

	reg = nos << 5;
	reg |= outer << 3;
	if (cpuinfo.coherent_walk)
		reg |= (inner & 0x1) << 6;
	reg |= (inner & 0x2) >> 1;
#ifdef SMP
	ARM_SMP_UP(
		reg |= 1 << 1,
	);
#endif
	return reg;
}

/*
 *  Set TEX remapping registers in current CPU.
 */
void
pmap_set_tex(void)
{
	uint32_t prrr, nmrr;
	uint32_t type, inner, outer, nos;
	int i;

#ifdef PMAP_PTE_NOCACHE
	/* XXX fixme */
	if (cpuinfo.coherent_walk) {
		pt_memattr = VM_MEMATTR_WB_WA;
		ttb_flags = encode_ttb_flags(0);
	}
	else {
		pt_memattr = VM_MEMATTR_NOCACHE;
		ttb_flags = encode_ttb_flags(1);
	}
#else
	pt_memattr = VM_MEMATTR_WB_WA;
	ttb_flags = encode_ttb_flags(0);
#endif

	prrr = 0;
	nmrr = 0;

	/* Build remapping register from TEX classes. */
	for (i = 0; i < 8; i++) {
		type = (tex_class[i] >> TEXDEF_TYPE_SHIFT) &
			TEXDEF_TYPE_MASK;
		inner = (tex_class[i] >> TEXDEF_INNER_SHIFT) &
			TEXDEF_INNER_MASK;
		outer = (tex_class[i] >> TEXDEF_OUTER_SHIFT) &
			TEXDEF_OUTER_MASK;
		nos = (tex_class[i] >> TEXDEF_NOS_SHIFT) &
			TEXDEF_NOS_MASK;

		prrr |= type  << (i * 2);
		prrr |= nos   << (i + 24);
		nmrr |= inner << (i * 2);
		nmrr |= outer << (i * 2 + 16);
	}
	/* Add shareable bits for device memory. */
	prrr |= PRRR_DS0 | PRRR_DS1;

	/* Add shareable bits for normal memory in SMP case. */
#ifdef SMP
	ARM_SMP_UP(
		prrr |= PRRR_NS1,
	);
#endif
	cp15_prrr_set(prrr);
	cp15_nmrr_set(nmrr);

	/* Caches are disabled, so full TLB flush should be enough. */
	tlb_flush_all_local();
}

/*
 * Remap one vm_meattr class to another one. This can be useful as
 * workaround for SOC errata, e.g. if devices must be accessed using
 * SO memory class.
 *
 * !!! Please note that this function is absolutely last resort thing.
 * It should not be used under normal circumstances. !!!
 *
 * Usage rules:
 * - it shall be called after pmap_bootstrap_prepare() and before
 *   cpu_mp_start() (thus only on boot CPU). In practice, it's expected
 *   to be called from platform_attach() or platform_late_init().
 *
 * - if remapping doesn't change caching mode, or until uncached class
 *   is remapped to any kind of cached one, then no other restriction exists.
 *
 * - if pmap_remap_vm_attr() changes caching mode, but both (original and
 *   remapped) remain cached, then caller is resposible for calling
 *   of dcache_wbinv_poc_all().
 *
 * - remapping of any kind of cached class to uncached is not permitted.
 */
void
pmap_remap_vm_attr(vm_memattr_t old_attr, vm_memattr_t new_attr)
{
	int old_idx, new_idx;

	/* Map VM memattrs to indexes to tex_class table. */
	old_idx = PTE2_ATTR2IDX(pte2_attr_tab[(int)old_attr]);
	new_idx = PTE2_ATTR2IDX(pte2_attr_tab[(int)new_attr]);

	/* Replace TEX attribute and apply it. */
	tex_class[old_idx] = tex_class[new_idx];
	pmap_set_tex();
}

/*
 * KERNBASE must be multiple of NPT2_IN_PG * PTE1_SIZE. In other words,
 * KERNBASE is mapped by first L2 page table in L2 page table page. It
 * meets same constrain due to PT2MAP being placed just under KERNBASE.
 */
CTASSERT((KERNBASE & (NPT2_IN_PG * PTE1_SIZE - 1)) == 0);
CTASSERT((KERNBASE - VM_MAXUSER_ADDRESS) >= PT2MAP_SIZE);

/*
 *  In crazy dreams, PAGE_SIZE could be a multiple of PTE2_SIZE in general.
 *  For now, anyhow, the following check must be fulfilled.
 */
CTASSERT(PAGE_SIZE == PTE2_SIZE);
/*
 *  We don't want to mess up MI code with all MMU and PMAP definitions,
 *  so some things, which depend on other ones, are defined independently.
 *  Now, it is time to check that we don't screw up something.
 */
CTASSERT(PDRSHIFT == PTE1_SHIFT);
/*
 *  Check L1 and L2 page table entries definitions consistency.
 */
CTASSERT(NB_IN_PT1 == (sizeof(pt1_entry_t) * NPTE1_IN_PT1));
CTASSERT(NB_IN_PT2 == (sizeof(pt2_entry_t) * NPTE2_IN_PT2));
/*
 *  Check L2 page tables page consistency.
 */
CTASSERT(PAGE_SIZE == (NPT2_IN_PG * NB_IN_PT2));
CTASSERT((1 << PT2PG_SHIFT) == NPT2_IN_PG);
/*
 *  Check PT2TAB consistency.
 *  PT2TAB_ENTRIES is defined as a division of NPTE1_IN_PT1 by NPT2_IN_PG.
 *  This should be done without remainder.
 */
CTASSERT(NPTE1_IN_PT1 == (PT2TAB_ENTRIES * NPT2_IN_PG));

/*
 *	A PT2MAP magic.
 *
 *  All level 2 page tables (PT2s) are mapped continuously and accordingly
 *  into PT2MAP address space. As PT2 size is less than PAGE_SIZE, this can
 *  be done only if PAGE_SIZE is a multiple of PT2 size. All PT2s in one page
 *  must be used together, but not necessary at once. The first PT2 in a page
 *  must map things on correctly aligned address and the others must follow
 *  in right order.
 */
#define NB_IN_PT2TAB	(PT2TAB_ENTRIES * sizeof(pt2_entry_t))
#define NPT2_IN_PT2TAB	(NB_IN_PT2TAB / NB_IN_PT2)
#define NPG_IN_PT2TAB	(NB_IN_PT2TAB / PAGE_SIZE)

/*
 *  Check PT2TAB consistency.
 *  NPT2_IN_PT2TAB is defined as a division of NB_IN_PT2TAB by NB_IN_PT2.
 *  NPG_IN_PT2TAB is defined as a division of NB_IN_PT2TAB by PAGE_SIZE.
 *  The both should be done without remainder.
 */
CTASSERT(NB_IN_PT2TAB == (NPT2_IN_PT2TAB * NB_IN_PT2));
CTASSERT(NB_IN_PT2TAB == (NPG_IN_PT2TAB * PAGE_SIZE));
/*
 *  The implementation was made general, however, with the assumption
 *  bellow in mind. In case of another value of NPG_IN_PT2TAB,
 *  the code should be once more rechecked.
 */
CTASSERT(NPG_IN_PT2TAB == 1);

/*
 *  Get offset of PT2 in a page
 *  associated with given PT1 index.
 */
static __inline u_int
page_pt2off(u_int pt1_idx)
{

	return ((pt1_idx & PT2PG_MASK) * NB_IN_PT2);
}

/*
 *  Get physical address of PT2
 *  associated with given PT2s page and PT1 index.
 */
static __inline vm_paddr_t
page_pt2pa(vm_paddr_t pgpa, u_int pt1_idx)
{

	return (pgpa + page_pt2off(pt1_idx));
}

/*
 *  Get first entry of PT2
 *  associated with given PT2s page and PT1 index.
 */
static __inline pt2_entry_t *
page_pt2(vm_offset_t pgva, u_int pt1_idx)
{

	return ((pt2_entry_t *)(pgva + page_pt2off(pt1_idx)));
}

/*
 *  Get virtual address of PT2s page (mapped in PT2MAP)
 *  which holds PT2 which holds entry which maps given virtual address.
 */
static __inline vm_offset_t
pt2map_pt2pg(vm_offset_t va)
{

	va &= ~(NPT2_IN_PG * PTE1_SIZE - 1);
	return ((vm_offset_t)pt2map_entry(va));
}

/*****************************************************************************
 *
 *     THREE pmap initialization milestones exist:
 *
 *  locore.S
 *    -> fundamental init (including MMU) in ASM
 *
 *  initarm()
 *    -> fundamental init continues in C
 *    -> first available physical address is known
 *
 *    pmap_bootstrap_prepare() -> FIRST PMAP MILESTONE (first epoch begins)
 *      -> basic (safe) interface for physical address allocation is made
 *      -> basic (safe) interface for virtual mapping is made
 *      -> limited not SMP coherent work is possible
 *
 *    -> more fundamental init continues in C
 *    -> locks and some more things are available
 *    -> all fundamental allocations and mappings are done
 *
 *    pmap_bootstrap() -> SECOND PMAP MILESTONE (second epoch begins)
 *      -> phys_avail[] and virtual_avail is set
 *      -> control is passed to vm subsystem
 *      -> physical and virtual address allocation are off limit
 *      -> low level mapping functions, some SMP coherent,
 *         are available, which cannot be used before vm subsystem
 *         is being inited
 *
 *  mi_startup()
 *    -> vm subsystem is being inited
 *
 *      pmap_init() -> THIRD PMAP MILESTONE (third epoch begins)
 *        -> pmap is fully inited
 *
 *****************************************************************************/

/*****************************************************************************
 *
 *	PMAP first stage initialization and utility functions
 *	for pre-bootstrap epoch.
 *
 *  After pmap_bootstrap_prepare() is called, the following functions
 *  can be used:
 *
 *  (1) strictly only for this stage functions for physical page allocations,
 *      virtual space allocations, and mappings:
 *
 *  vm_paddr_t pmap_preboot_get_pages(u_int num);
 *  void pmap_preboot_map_pages(vm_paddr_t pa, vm_offset_t va, u_int num);
 *  vm_offset_t pmap_preboot_reserve_pages(u_int num);
 *  vm_offset_t pmap_preboot_get_vpages(u_int num);
 *  void pmap_preboot_map_attr(vm_paddr_t pa, vm_offset_t va, vm_size_t size,
 *      vm_prot_t prot, vm_memattr_t attr);
 *
 *  (2) for all stages:
 *
 *  vm_paddr_t pmap_kextract(vm_offset_t va);
 *
 *  NOTE: This is not SMP coherent stage.
 *
 *****************************************************************************/

#define KERNEL_P2V(pa) \
    ((vm_offset_t)((pa) - arm_physmem_kernaddr + KERNVIRTADDR))
#define KERNEL_V2P(va) \
    ((vm_paddr_t)((va) - KERNVIRTADDR + arm_physmem_kernaddr))

static vm_paddr_t last_paddr;

/*
 *  Pre-bootstrap epoch page allocator.
 */
vm_paddr_t
pmap_preboot_get_pages(u_int num)
{
	vm_paddr_t ret;

	ret = last_paddr;
	last_paddr += num * PAGE_SIZE;

	return (ret);
}

/*
 *	The fundamental initialization of PMAP stuff.
 *
 *  Some things already happened in locore.S and some things could happen
 *  before pmap_bootstrap_prepare() is called, so let's recall what is done:
 *  1. Caches are disabled.
 *  2. We are running on virtual addresses already with 'boot_pt1'
 *     as L1 page table.
 *  3. So far, all virtual addresses can be converted to physical ones and
 *     vice versa by the following macros:
 *       KERNEL_P2V(pa) .... physical to virtual ones,
 *       KERNEL_V2P(va) .... virtual to physical ones.
 *
 *  What is done herein:
 *  1. The 'boot_pt1' is replaced by real kernel L1 page table 'kern_pt1'.
 *  2. PT2MAP magic is brought to live.
 *  3. Basic preboot functions for page allocations and mappings can be used.
 *  4. Everything is prepared for L1 cache enabling.
 *
 *  Variations:
 *  1. To use second TTB register, so kernel and users page tables will be
 *     separated. This way process forking - pmap_pinit() - could be faster,
 *     it saves physical pages and KVA per a process, and it's simple change.
 *     However, it will lead, due to hardware matter, to the following:
 *     (a) 2G space for kernel and 2G space for users.
 *     (b) 1G space for kernel in low addresses and 3G for users above it.
 *     A question is: Is the case (b) really an option? Note that case (b)
 *     does save neither physical memory and KVA.
 */
void
pmap_bootstrap_prepare(vm_paddr_t last)
{
	vm_paddr_t pt2pg_pa, pt2tab_pa, pa, size;
	vm_offset_t pt2pg_va;
	pt1_entry_t *pte1p;
	pt2_entry_t *pte2p;
	u_int i;
	uint32_t l1_attr;

	/*
	 * Now, we are going to make real kernel mapping. Note that we are
	 * already running on some mapping made in locore.S and we expect
	 * that it's large enough to ensure nofault access to physical memory
	 * allocated herein before switch.
	 *
	 * As kernel image and everything needed before are and will be mapped
	 * by section mappings, we align last physical address to PTE1_SIZE.
	 */
	last_paddr = pte1_roundup(last);

	/*
	 * Allocate and zero page(s) for kernel L1 page table.
	 *
	 * Note that it's first allocation on space which was PTE1_SIZE
	 * aligned and as such base_pt1 is aligned to NB_IN_PT1 too.
	 */
	base_pt1 = pmap_preboot_get_pages(NPG_IN_PT1);
	kern_pt1 = (pt1_entry_t *)KERNEL_P2V(base_pt1);
	bzero((void*)kern_pt1, NB_IN_PT1);
	pte1_sync_range(kern_pt1, NB_IN_PT1);

	/* Allocate and zero page(s) for kernel PT2TAB. */
	pt2tab_pa = pmap_preboot_get_pages(NPG_IN_PT2TAB);
	kern_pt2tab = (pt2_entry_t *)KERNEL_P2V(pt2tab_pa);
	bzero(kern_pt2tab, NB_IN_PT2TAB);
	pte2_sync_range(kern_pt2tab, NB_IN_PT2TAB);

	/* Allocate and zero page(s) for kernel L2 page tables. */
	pt2pg_pa = pmap_preboot_get_pages(NKPT2PG);
	pt2pg_va = KERNEL_P2V(pt2pg_pa);
	size = NKPT2PG * PAGE_SIZE;
	bzero((void*)pt2pg_va, size);
	pte2_sync_range((pt2_entry_t *)pt2pg_va, size);

	/*
	 * Add a physical memory segment (vm_phys_seg) corresponding to the
	 * preallocated pages for kernel L2 page tables so that vm_page
	 * structures representing these pages will be created. The vm_page
	 * structures are required for promotion of the corresponding kernel
	 * virtual addresses to section mappings.
	 */
	vm_phys_add_seg(pt2tab_pa, pmap_preboot_get_pages(0));

	/*
	 * Insert allocated L2 page table pages to PT2TAB and make
	 * link to all PT2s in L1 page table. See how kernel_vm_end
	 * is initialized.
	 *
	 * We play simple and safe. So every KVA will have underlaying
	 * L2 page table, even kernel image mapped by sections.
	 */
	pte2p = kern_pt2tab_entry(KERNBASE);
	for (pa = pt2pg_pa; pa < pt2pg_pa + size; pa += PTE2_SIZE)
		pt2tab_store(pte2p++, PTE2_KPT(pa));

	pte1p = kern_pte1(KERNBASE);
	for (pa = pt2pg_pa; pa < pt2pg_pa + size; pa += NB_IN_PT2)
		pte1_store(pte1p++, PTE1_LINK(pa));

	/* Make section mappings for kernel. */
	l1_attr = ATTR_TO_L1(PTE2_ATTR_DEFAULT);
	pte1p = kern_pte1(KERNBASE);
	for (pa = KERNEL_V2P(KERNBASE); pa < last; pa += PTE1_SIZE)
		pte1_store(pte1p++, PTE1_KERN(pa, PTE1_AP_KRW, l1_attr));

	/*
	 * Get free and aligned space for PT2MAP and make L1 page table links
	 * to L2 page tables held in PT2TAB.
	 *
	 * Note that pages holding PT2s are stored in PT2TAB as pt2_entry_t
	 * descriptors and PT2TAB page(s) itself is(are) used as PT2s. Thus
	 * each entry in PT2TAB maps all PT2s in a page. This implies that
	 * virtual address of PT2MAP must be aligned to NPT2_IN_PG * PTE1_SIZE.
	 */
	PT2MAP = (pt2_entry_t *)(KERNBASE - PT2MAP_SIZE);
	pte1p = kern_pte1((vm_offset_t)PT2MAP);
	for (pa = pt2tab_pa, i = 0; i < NPT2_IN_PT2TAB; i++, pa += NB_IN_PT2) {
		pte1_store(pte1p++, PTE1_LINK(pa));
	}

	/*
	 * Store PT2TAB in PT2TAB itself, i.e. self reference mapping.
	 * Each pmap will hold own PT2TAB, so the mapping should be not global.
	 */
	pte2p = kern_pt2tab_entry((vm_offset_t)PT2MAP);
	for (pa = pt2tab_pa, i = 0; i < NPG_IN_PT2TAB; i++, pa += PTE2_SIZE) {
		pt2tab_store(pte2p++, PTE2_KPT_NG(pa));
	}

	/*
	 * Choose correct L2 page table and make mappings for allocations
	 * made herein which replaces temporary locore.S mappings after a while.
	 * Note that PT2MAP cannot be used until we switch to kern_pt1.
	 *
	 * Note, that these allocations started aligned on 1M section and
	 * kernel PT1 was allocated first. Making of mappings must follow
	 * order of physical allocations as we've used KERNEL_P2V() macro
	 * for virtual addresses resolution.
	 */
	pte2p = kern_pt2tab_entry((vm_offset_t)kern_pt1);
	pt2pg_va = KERNEL_P2V(pte2_pa(pte2_load(pte2p)));

	pte2p = page_pt2(pt2pg_va, pte1_index((vm_offset_t)kern_pt1));

	/* Make mapping for kernel L1 page table. */
	for (pa = base_pt1, i = 0; i < NPG_IN_PT1; i++, pa += PTE2_SIZE)
		pte2_store(pte2p++, PTE2_KPT(pa));

	/* Make mapping for kernel PT2TAB. */
	for (pa = pt2tab_pa, i = 0; i < NPG_IN_PT2TAB; i++, pa += PTE2_SIZE)
		pte2_store(pte2p++, PTE2_KPT(pa));

	/* Finally, switch from 'boot_pt1' to 'kern_pt1'. */
	pmap_kern_ttb = base_pt1 | ttb_flags;
	cpuinfo_reinit_mmu(pmap_kern_ttb);
	/*
	 * Initialize the first available KVA. As kernel image is mapped by
	 * sections, we are leaving some gap behind.
	 */
	virtual_avail = (vm_offset_t)kern_pt2tab + NPG_IN_PT2TAB * PAGE_SIZE;
}

/*
 *  Setup L2 page table page for given KVA.
 *  Used in pre-bootstrap epoch.
 *
 *  Note that we have allocated NKPT2PG pages for L2 page tables in advance
 *  and used them for mapping KVA starting from KERNBASE. However, this is not
 *  enough. Vectors and devices need L2 page tables too. Note that they are
 *  even above VM_MAX_KERNEL_ADDRESS.
 */
static __inline vm_paddr_t
pmap_preboot_pt2pg_setup(vm_offset_t va)
{
	pt2_entry_t *pte2p, pte2;
	vm_paddr_t pt2pg_pa;

	/* Get associated entry in PT2TAB. */
	pte2p = kern_pt2tab_entry(va);

	/* Just return, if PT2s page exists already. */
	pte2 = pt2tab_load(pte2p);
	if (pte2_is_valid(pte2))
		return (pte2_pa(pte2));

	KASSERT(va >= VM_MAX_KERNEL_ADDRESS,
	    ("%s: NKPT2PG too small", __func__));

	/*
	 * Allocate page for PT2s and insert it to PT2TAB.
	 * In other words, map it into PT2MAP space.
	 */
	pt2pg_pa = pmap_preboot_get_pages(1);
	pt2tab_store(pte2p, PTE2_KPT(pt2pg_pa));

	/* Zero all PT2s in allocated page. */
	bzero((void*)pt2map_pt2pg(va), PAGE_SIZE);
	pte2_sync_range((pt2_entry_t *)pt2map_pt2pg(va), PAGE_SIZE);

	return (pt2pg_pa);
}

/*
 *  Setup L2 page table for given KVA.
 *  Used in pre-bootstrap epoch.
 */
static void
pmap_preboot_pt2_setup(vm_offset_t va)
{
	pt1_entry_t *pte1p;
	vm_paddr_t pt2pg_pa, pt2_pa;

	/* Setup PT2's page. */
	pt2pg_pa = pmap_preboot_pt2pg_setup(va);
	pt2_pa = page_pt2pa(pt2pg_pa, pte1_index(va));

	/* Insert PT2 to PT1. */
	pte1p = kern_pte1(va);
	pte1_store(pte1p, PTE1_LINK(pt2_pa));
}

/*
 *  Get L2 page entry associated with given KVA.
 *  Used in pre-bootstrap epoch.
 */
static __inline pt2_entry_t*
pmap_preboot_vtopte2(vm_offset_t va)
{
	pt1_entry_t *pte1p;

	/* Setup PT2 if needed. */
	pte1p = kern_pte1(va);
	if (!pte1_is_valid(pte1_load(pte1p))) /* XXX - sections ?! */
		pmap_preboot_pt2_setup(va);

	return (pt2map_entry(va));
}

/*
 *  Pre-bootstrap epoch page(s) mapping(s).
 */
void
pmap_preboot_map_pages(vm_paddr_t pa, vm_offset_t va, u_int num)
{
	u_int i;
	pt2_entry_t *pte2p;

	/* Map all the pages. */
	for (i = 0; i < num; i++) {
		pte2p = pmap_preboot_vtopte2(va);
		pte2_store(pte2p, PTE2_KRW(pa));
		va += PAGE_SIZE;
		pa += PAGE_SIZE;
	}
}

/*
 *  Pre-bootstrap epoch virtual space alocator.
 */
vm_offset_t
pmap_preboot_reserve_pages(u_int num)
{
	u_int i;
	vm_offset_t start, va;
	pt2_entry_t *pte2p;

	/* Allocate virtual space. */
	start = va = virtual_avail;
	virtual_avail += num * PAGE_SIZE;

	/* Zero the mapping. */
	for (i = 0; i < num; i++) {
		pte2p = pmap_preboot_vtopte2(va);
		pte2_store(pte2p, 0);
		va += PAGE_SIZE;
	}

	return (start);
}

/*
 *  Pre-bootstrap epoch page(s) allocation and mapping(s).
 */
vm_offset_t
pmap_preboot_get_vpages(u_int num)
{
	vm_paddr_t  pa;
	vm_offset_t va;

	/* Allocate physical page(s). */
	pa = pmap_preboot_get_pages(num);

	/* Allocate virtual space. */
	va = virtual_avail;
	virtual_avail += num * PAGE_SIZE;

	/* Map and zero all. */
	pmap_preboot_map_pages(pa, va, num);
	bzero((void *)va, num * PAGE_SIZE);

	return (va);
}

/*
 *  Pre-bootstrap epoch page mapping(s) with attributes.
 */
void
pmap_preboot_map_attr(vm_paddr_t pa, vm_offset_t va, vm_size_t size,
    vm_prot_t prot, vm_memattr_t attr)
{
	u_int num;
	u_int l1_attr, l1_prot, l2_prot, l2_attr;
	pt1_entry_t *pte1p;
	pt2_entry_t *pte2p;

	l2_prot = prot & VM_PROT_WRITE ? PTE2_AP_KRW : PTE2_AP_KR;
	l2_prot |= (prot & VM_PROT_EXECUTE) ? PTE2_X : PTE2_NX;
	l2_attr = vm_memattr_to_pte2(attr);
	l1_prot = ATTR_TO_L1(l2_prot);
	l1_attr = ATTR_TO_L1(l2_attr);

	/* Map all the pages. */
	num = round_page(size);
	while (num > 0) {
		if ((((va | pa) & PTE1_OFFSET) == 0) && (num >= PTE1_SIZE)) {
			pte1p = kern_pte1(va);
			pte1_store(pte1p, PTE1_KERN(pa, l1_prot, l1_attr));
			va += PTE1_SIZE;
			pa += PTE1_SIZE;
			num -= PTE1_SIZE;
		} else {
			pte2p = pmap_preboot_vtopte2(va);
			pte2_store(pte2p, PTE2_KERN(pa, l2_prot, l2_attr));
			va += PAGE_SIZE;
			pa += PAGE_SIZE;
			num -= PAGE_SIZE;
		}
	}
}

/*
 *  Extract from the kernel page table the physical address
 *  that is mapped by the given virtual address "va".
 */
vm_paddr_t
pmap_kextract(vm_offset_t va)
{
	vm_paddr_t pa;
	pt1_entry_t pte1;
	pt2_entry_t pte2;

	pte1 = pte1_load(kern_pte1(va));
	if (pte1_is_section(pte1)) {
		pa = pte1_pa(pte1) | (va & PTE1_OFFSET);
	} else if (pte1_is_link(pte1)) {
		/*
		 * We should beware of concurrent promotion that changes
		 * pte1 at this point. However, it's not a problem as PT2
		 * page is preserved by promotion in PT2TAB. So even if
		 * it happens, using of PT2MAP is still safe.
		 *
		 * QQQ: However, concurrent removing is a problem which
		 *      ends in abort on PT2MAP space. Locking must be used
		 *      to deal with this.
		 */
		pte2 = pte2_load(pt2map_entry(va));
		pa = pte2_pa(pte2) | (va & PTE2_OFFSET);
	}
	else {
		panic("%s: va %#x pte1 %#x", __func__, va, pte1);
	}
	return (pa);
}

/*
 *  Extract from the kernel page table the physical address
 *  that is mapped by the given virtual address "va". Also
 *  return L2 page table entry which maps the address.
 *
 *  This is only intended to be used for panic dumps.
 */
vm_paddr_t
pmap_dump_kextract(vm_offset_t va, pt2_entry_t *pte2p)
{
	vm_paddr_t pa;
	pt1_entry_t pte1;
	pt2_entry_t pte2;

	pte1 = pte1_load(kern_pte1(va));
	if (pte1_is_section(pte1)) {
		pa = pte1_pa(pte1) | (va & PTE1_OFFSET);
		pte2 = pa | ATTR_TO_L2(pte1) | PTE2_V;
	} else if (pte1_is_link(pte1)) {
		pte2 = pte2_load(pt2map_entry(va));
		pa = pte2_pa(pte2);
	} else {
		pte2 = 0;
		pa = 0;
	}
	if (pte2p != NULL)
		*pte2p = pte2;
	return (pa);
}

/*****************************************************************************
 *
 *	PMAP second stage initialization and utility functions
 *	for bootstrap epoch.
 *
 *  After pmap_bootstrap() is called, the following functions for
 *  mappings can be used:
 *
 *  void pmap_kenter(vm_offset_t va, vm_paddr_t pa);
 *  void pmap_kremove(vm_offset_t va);
 *  vm_offset_t pmap_map(vm_offset_t *virt, vm_paddr_t start, vm_paddr_t end,
 *      int prot);
 *
 *  NOTE: This is not SMP coherent stage. And physical page allocation is not
 *        allowed during this stage.
 *
 *****************************************************************************/

/*
 *  Initialize kernel PMAP locks and lists, kernel_pmap itself, and
 *  reserve various virtual spaces for temporary mappings.
 */
void
pmap_bootstrap(vm_offset_t firstaddr)
{
	pt2_entry_t *unused __unused;
	struct pcpu *pc;

	/*
	 * Initialize the kernel pmap (which is statically allocated).
	 */
	PMAP_LOCK_INIT(kernel_pmap);
	kernel_l1pa = (vm_paddr_t)kern_pt1;  /* for libkvm */
	kernel_pmap->pm_pt1 = kern_pt1;
	kernel_pmap->pm_pt2tab = kern_pt2tab;
	CPU_FILL(&kernel_pmap->pm_active);  /* don't allow deactivation */
	TAILQ_INIT(&kernel_pmap->pm_pvchunk);

	/*
	 * Initialize the global pv list lock.
	 */
	rw_init(&pvh_global_lock, "pmap pv global");

	LIST_INIT(&allpmaps);

	/*
	 * Request a spin mutex so that changes to allpmaps cannot be
	 * preempted by smp_rendezvous_cpus().
	 */
	mtx_init(&allpmaps_lock, "allpmaps", NULL, MTX_SPIN);
	mtx_lock_spin(&allpmaps_lock);
	LIST_INSERT_HEAD(&allpmaps, kernel_pmap, pm_list);
	mtx_unlock_spin(&allpmaps_lock);

	/*
	 * Reserve some special page table entries/VA space for temporary
	 * mapping of pages.
	 */
#define	SYSMAP(c, p, v, n)  do {		\
	v = (c)pmap_preboot_reserve_pages(n);	\
	p = pt2map_entry((vm_offset_t)v);	\
	} while (0)

	/*
	 * Local CMAP1/CMAP2 are used for zeroing and copying pages.
	 * Local CMAP2 is also used for data cache cleaning.
	 */
	pc = get_pcpu();
	mtx_init(&pc->pc_cmap_lock, "SYSMAPS", NULL, MTX_DEF);
	SYSMAP(caddr_t, pc->pc_cmap1_pte2p, pc->pc_cmap1_addr, 1);
	SYSMAP(caddr_t, pc->pc_cmap2_pte2p, pc->pc_cmap2_addr, 1);
	SYSMAP(vm_offset_t, pc->pc_qmap_pte2p, pc->pc_qmap_addr, 1);

	/*
	 * Crashdump maps.
	 */
	SYSMAP(caddr_t, unused, crashdumpmap, MAXDUMPPGS);

	/*
	 * _tmppt is used for reading arbitrary physical pages via /dev/mem.
	 */
	SYSMAP(caddr_t, unused, _tmppt, 1);

	/*
	 * PADDR1 and PADDR2 are used by pmap_pte2_quick() and pmap_pte2(),
	 * respectively. PADDR3 is used by pmap_pte2_ddb().
	 */
	SYSMAP(pt2_entry_t *, PMAP1, PADDR1, 1);
	SYSMAP(pt2_entry_t *, PMAP2, PADDR2, 1);
#ifdef DDB
	SYSMAP(pt2_entry_t *, PMAP3, PADDR3, 1);
#endif
	mtx_init(&PMAP2mutex, "PMAP2", NULL, MTX_DEF);

	/*
	 * Note that in very short time in initarm(), we are going to
	 * initialize phys_avail[] array and no further page allocation
	 * can happen after that until vm subsystem will be initialized.
	 */
	kernel_vm_end_new = kernel_vm_end;
	virtual_end = vm_max_kernel_address;
}

static void
pmap_init_reserved_pages(void)
{
	struct pcpu *pc;
	vm_offset_t pages;
	int i;

	CPU_FOREACH(i) {
		pc = pcpu_find(i);
		/*
		 * Skip if the mapping has already been initialized,
		 * i.e. this is the BSP.
		 */
		if (pc->pc_cmap1_addr != 0)
			continue;
		mtx_init(&pc->pc_cmap_lock, "SYSMAPS", NULL, MTX_DEF);
		pages = kva_alloc(PAGE_SIZE * 3);
		if (pages == 0)
			panic("%s: unable to allocate KVA", __func__);
		pc->pc_cmap1_pte2p = pt2map_entry(pages);
		pc->pc_cmap2_pte2p = pt2map_entry(pages + PAGE_SIZE);
		pc->pc_qmap_pte2p = pt2map_entry(pages + (PAGE_SIZE * 2));
		pc->pc_cmap1_addr = (caddr_t)pages;
		pc->pc_cmap2_addr = (caddr_t)(pages + PAGE_SIZE);
		pc->pc_qmap_addr = pages + (PAGE_SIZE * 2);
	}
}
SYSINIT(rpages_init, SI_SUB_CPU, SI_ORDER_ANY, pmap_init_reserved_pages, NULL);

/*
 *  The function can already be use in second initialization stage.
 *  As such, the function DOES NOT call pmap_growkernel() where PT2
 *  allocation can happen. So if used, be sure that PT2 for given
 *  virtual address is allocated already!
 *
 *  Add a wired page to the kva.
 *  Note: not SMP coherent.
 */
static __inline void
pmap_kenter_prot_attr(vm_offset_t va, vm_paddr_t pa, uint32_t prot,
    uint32_t attr)
{
	pt1_entry_t *pte1p;
	pt2_entry_t *pte2p;

	pte1p = kern_pte1(va);
	if (!pte1_is_valid(pte1_load(pte1p))) { /* XXX - sections ?! */
		/*
		 * This is a very low level function, so PT2 and particularly
		 * PT2PG associated with given virtual address must be already
		 * allocated. It's a pain mainly during pmap initialization
		 * stage. However, called after pmap initialization with
		 * virtual address not under kernel_vm_end will lead to
		 * the same misery.
		 */
		if (!pte2_is_valid(pte2_load(kern_pt2tab_entry(va))))
			panic("%s: kernel PT2 not allocated!", __func__);
	}

	pte2p = pt2map_entry(va);
	pte2_store(pte2p, PTE2_KERN(pa, prot, attr));
}

PMAP_INLINE void
pmap_kenter(vm_offset_t va, vm_paddr_t pa)
{

	pmap_kenter_prot_attr(va, pa, PTE2_AP_KRW, PTE2_ATTR_DEFAULT);
}

/*
 *  Remove a page from the kernel pagetables.
 *  Note: not SMP coherent.
 */
PMAP_INLINE void
pmap_kremove(vm_offset_t va)
{
	pt1_entry_t *pte1p;
	pt2_entry_t *pte2p;

	pte1p = kern_pte1(va);
	if (pte1_is_section(pte1_load(pte1p))) {
		pte1_clear(pte1p);
	} else {
		pte2p = pt2map_entry(va);
		pte2_clear(pte2p);
	}
}

/*
 *  Share new kernel PT2PG with all pmaps.
 *  The caller is responsible for maintaining TLB consistency.
 */
static void
pmap_kenter_pt2tab(vm_offset_t va, pt2_entry_t npte2)
{
	pmap_t pmap;
	pt2_entry_t *pte2p;

	mtx_lock_spin(&allpmaps_lock);
	LIST_FOREACH(pmap, &allpmaps, pm_list) {
		pte2p = pmap_pt2tab_entry(pmap, va);
		pt2tab_store(pte2p, npte2);
	}
	mtx_unlock_spin(&allpmaps_lock);
}

/*
 *  Share new kernel PTE1 with all pmaps.
 *  The caller is responsible for maintaining TLB consistency.
 */
static void
pmap_kenter_pte1(vm_offset_t va, pt1_entry_t npte1)
{
	pmap_t pmap;
	pt1_entry_t *pte1p;

	mtx_lock_spin(&allpmaps_lock);
	LIST_FOREACH(pmap, &allpmaps, pm_list) {
		pte1p = pmap_pte1(pmap, va);
		pte1_store(pte1p, npte1);
	}
	mtx_unlock_spin(&allpmaps_lock);
}

/*
 *  Used to map a range of physical addresses into kernel
 *  virtual address space.
 *
 *  The value passed in '*virt' is a suggested virtual address for
 *  the mapping. Architectures which can support a direct-mapped
 *  physical to virtual region can return the appropriate address
 *  within that region, leaving '*virt' unchanged. Other
 *  architectures should map the pages starting at '*virt' and
 *  update '*virt' with the first usable address after the mapped
 *  region.
 *
 *  NOTE: Read the comments above pmap_kenter_prot_attr() as
 *        the function is used herein!
 */
vm_offset_t
pmap_map(vm_offset_t *virt, vm_paddr_t start, vm_paddr_t end, int prot)
{
	vm_offset_t va, sva;
	vm_paddr_t pte1_offset;
	pt1_entry_t npte1;
	uint32_t l1prot, l2prot;
	uint32_t l1attr, l2attr;

	PDEBUG(1, printf("%s: virt = %#x, start = %#x, end = %#x (size = %#x),"
	    " prot = %d\n", __func__, *virt, start, end, end - start,  prot));

	l2prot = (prot & VM_PROT_WRITE) ? PTE2_AP_KRW : PTE2_AP_KR;
	l2prot |= (prot & VM_PROT_EXECUTE) ? PTE2_X : PTE2_NX;
	l1prot = ATTR_TO_L1(l2prot);

	l2attr = PTE2_ATTR_DEFAULT;
	l1attr = ATTR_TO_L1(l2attr);

	va = *virt;
	/*
	 * Does the physical address range's size and alignment permit at
	 * least one section mapping to be created?
	 */
	pte1_offset = start & PTE1_OFFSET;
	if ((end - start) - ((PTE1_SIZE - pte1_offset) & PTE1_OFFSET) >=
	    PTE1_SIZE) {
		/*
		 * Increase the starting virtual address so that its alignment
		 * does not preclude the use of section mappings.
		 */
		if ((va & PTE1_OFFSET) < pte1_offset)
			va = pte1_trunc(va) + pte1_offset;
		else if ((va & PTE1_OFFSET) > pte1_offset)
			va = pte1_roundup(va) + pte1_offset;
	}
	sva = va;
	while (start < end) {
		if ((start & PTE1_OFFSET) == 0 && end - start >= PTE1_SIZE) {
			KASSERT((va & PTE1_OFFSET) == 0,
			    ("%s: misaligned va %#x", __func__, va));
			npte1 = PTE1_KERN(start, l1prot, l1attr);
			pmap_kenter_pte1(va, npte1);
			va += PTE1_SIZE;
			start += PTE1_SIZE;
		} else {
			pmap_kenter_prot_attr(va, start, l2prot, l2attr);
			va += PAGE_SIZE;
			start += PAGE_SIZE;
		}
	}
	tlb_flush_range(sva, va - sva);
	*virt = va;
	return (sva);
}

/*
 *  Make a temporary mapping for a physical address.
 *  This is only intended to be used for panic dumps.
 */
void *
pmap_kenter_temporary(vm_paddr_t pa, int i)
{
	vm_offset_t va;

	/* QQQ: 'i' should be less or equal to MAXDUMPPGS. */

	va = (vm_offset_t)crashdumpmap + (i * PAGE_SIZE);
	pmap_kenter(va, pa);
	tlb_flush_local(va);
	return ((void *)crashdumpmap);
}


/*************************************
 *
 *  TLB & cache maintenance routines.
 *
 *************************************/

/*
 *  We inline these within pmap.c for speed.
 */
PMAP_INLINE void
pmap_tlb_flush(pmap_t pmap, vm_offset_t va)
{

	if (pmap == kernel_pmap || !CPU_EMPTY(&pmap->pm_active))
		tlb_flush(va);
}

PMAP_INLINE void
pmap_tlb_flush_range(pmap_t pmap, vm_offset_t sva, vm_size_t size)
{

	if (pmap == kernel_pmap || !CPU_EMPTY(&pmap->pm_active))
		tlb_flush_range(sva, size);
}

/*
 *  Abuse the pte2 nodes for unmapped kva to thread a kva freelist through.
 *  Requirements:
 *   - Must deal with pages in order to ensure that none of the PTE2_* bits
 *     are ever set, PTE2_V in particular.
 *   - Assumes we can write to pte2s without pte2_store() atomic ops.
 *   - Assumes nothing will ever test these addresses for 0 to indicate
 *     no mapping instead of correctly checking PTE2_V.
 *   - Assumes a vm_offset_t will fit in a pte2 (true for arm).
 *  Because PTE2_V is never set, there can be no mappings to invalidate.
 */
static vm_offset_t
pmap_pte2list_alloc(vm_offset_t *head)
{
	pt2_entry_t *pte2p;
	vm_offset_t va;

	va = *head;
	if (va == 0)
		panic("pmap_ptelist_alloc: exhausted ptelist KVA");
	pte2p = pt2map_entry(va);
	*head = *pte2p;
	if (*head & PTE2_V)
		panic("%s: va with PTE2_V set!", __func__);
	*pte2p = 0;
	return (va);
}

static void
pmap_pte2list_free(vm_offset_t *head, vm_offset_t va)
{
	pt2_entry_t *pte2p;

	if (va & PTE2_V)
		panic("%s: freeing va with PTE2_V set!", __func__);
	pte2p = pt2map_entry(va);
	*pte2p = *head;		/* virtual! PTE2_V is 0 though */
	*head = va;
}

static void
pmap_pte2list_init(vm_offset_t *head, void *base, int npages)
{
	int i;
	vm_offset_t va;

	*head = 0;
	for (i = npages - 1; i >= 0; i--) {
		va = (vm_offset_t)base + i * PAGE_SIZE;
		pmap_pte2list_free(head, va);
	}
}

/*****************************************************************************
 *
 *	PMAP third and final stage initialization.
 *
 *  After pmap_init() is called, PMAP subsystem is fully initialized.
 *
 *****************************************************************************/

SYSCTL_NODE(_vm, OID_AUTO, pmap, CTLFLAG_RD, 0, "VM/pmap parameters");

SYSCTL_INT(_vm_pmap, OID_AUTO, pv_entry_max, CTLFLAG_RD, &pv_entry_max, 0,
    "Max number of PV entries");
SYSCTL_INT(_vm_pmap, OID_AUTO, shpgperproc, CTLFLAG_RD, &shpgperproc, 0,
    "Page share factor per proc");

static u_long nkpt2pg = NKPT2PG;
SYSCTL_ULONG(_vm_pmap, OID_AUTO, nkpt2pg, CTLFLAG_RD,
    &nkpt2pg, 0, "Pre-allocated pages for kernel PT2s");

static int sp_enabled = 1;
SYSCTL_INT(_vm_pmap, OID_AUTO, sp_enabled, CTLFLAG_RDTUN | CTLFLAG_NOFETCH,
    &sp_enabled, 0, "Are large page mappings enabled?");

bool
pmap_ps_enabled(pmap_t pmap __unused)
{

	return (sp_enabled != 0);
}

static SYSCTL_NODE(_vm_pmap, OID_AUTO, pte1, CTLFLAG_RD, 0,
    "1MB page mapping counters");

static u_long pmap_pte1_demotions;
SYSCTL_ULONG(_vm_pmap_pte1, OID_AUTO, demotions, CTLFLAG_RD,
    &pmap_pte1_demotions, 0, "1MB page demotions");

static u_long pmap_pte1_mappings;
SYSCTL_ULONG(_vm_pmap_pte1, OID_AUTO, mappings, CTLFLAG_RD,
    &pmap_pte1_mappings, 0, "1MB page mappings");

static u_long pmap_pte1_p_failures;
SYSCTL_ULONG(_vm_pmap_pte1, OID_AUTO, p_failures, CTLFLAG_RD,
    &pmap_pte1_p_failures, 0, "1MB page promotion failures");

static u_long pmap_pte1_promotions;
SYSCTL_ULONG(_vm_pmap_pte1, OID_AUTO, promotions, CTLFLAG_RD,
    &pmap_pte1_promotions, 0, "1MB page promotions");

static u_long pmap_pte1_kern_demotions;
SYSCTL_ULONG(_vm_pmap_pte1, OID_AUTO, kern_demotions, CTLFLAG_RD,
    &pmap_pte1_kern_demotions, 0, "1MB page kernel demotions");

static u_long pmap_pte1_kern_promotions;
SYSCTL_ULONG(_vm_pmap_pte1, OID_AUTO, kern_promotions, CTLFLAG_RD,
    &pmap_pte1_kern_promotions, 0, "1MB page kernel promotions");

static __inline ttb_entry_t
pmap_ttb_get(pmap_t pmap)
{

	return (vtophys(pmap->pm_pt1) | ttb_flags);
}

/*
 *  Initialize a vm_page's machine-dependent fields.
 *
 *  Variations:
 *  1. Pages for L2 page tables are always not managed. So, pv_list and
 *     pt2_wirecount can share same physical space. However, proper
 *     initialization on a page alloc for page tables and reinitialization
 *     on the page free must be ensured.
 */
void
pmap_page_init(vm_page_t m)
{

	TAILQ_INIT(&m->md.pv_list);
	pt2_wirecount_init(m);
	m->md.pat_mode = VM_MEMATTR_DEFAULT;
}

/*
 *  Virtualization for faster way how to zero whole page.
 */
static __inline void
pagezero(void *page)
{

	bzero(page, PAGE_SIZE);
}

/*
 *  Zero L2 page table page.
 *  Use same KVA as in pmap_zero_page().
 */
static __inline vm_paddr_t
pmap_pt2pg_zero(vm_page_t m)
{
	pt2_entry_t *cmap2_pte2p;
	vm_paddr_t pa;
	struct pcpu *pc;

	pa = VM_PAGE_TO_PHYS(m);

	/*
	 * XXX: For now, we map whole page even if it's already zero,
	 *      to sync it even if the sync is only DSB.
	 */
	sched_pin();
	pc = get_pcpu();
	cmap2_pte2p = pc->pc_cmap2_pte2p;
	mtx_lock(&pc->pc_cmap_lock);
	if (pte2_load(cmap2_pte2p) != 0)
		panic("%s: CMAP2 busy", __func__);
	pte2_store(cmap2_pte2p, PTE2_KERN_NG(pa, PTE2_AP_KRW,
	    vm_page_pte2_attr(m)));
	/*  Even VM_ALLOC_ZERO request is only advisory. */
	if ((m->flags & PG_ZERO) == 0)
		pagezero(pc->pc_cmap2_addr);
	pte2_sync_range((pt2_entry_t *)pc->pc_cmap2_addr, PAGE_SIZE);
	pte2_clear(cmap2_pte2p);
	tlb_flush((vm_offset_t)pc->pc_cmap2_addr);

	/*
	 * Unpin the thread before releasing the lock.  Otherwise the thread
	 * could be rescheduled while still bound to the current CPU, only
	 * to unpin itself immediately upon resuming execution.
	 */
	sched_unpin();
	mtx_unlock(&pc->pc_cmap_lock);

	return (pa);
}

/*
 *  Init just allocated page as L2 page table(s) holder
 *  and return its physical address.
 */
static __inline vm_paddr_t
pmap_pt2pg_init(pmap_t pmap, vm_offset_t va, vm_page_t m)
{
	vm_paddr_t pa;
	pt2_entry_t *pte2p;

	/* Check page attributes. */
	if (m->md.pat_mode != pt_memattr)
		pmap_page_set_memattr(m, pt_memattr);

	/* Zero page and init wire counts. */
	pa = pmap_pt2pg_zero(m);
	pt2_wirecount_init(m);

	/*
	 * Map page to PT2MAP address space for given pmap.
	 * Note that PT2MAP space is shared with all pmaps.
	 */
	if (pmap == kernel_pmap)
		pmap_kenter_pt2tab(va, PTE2_KPT(pa));
	else {
		pte2p = pmap_pt2tab_entry(pmap, va);
		pt2tab_store(pte2p, PTE2_KPT_NG(pa));
	}

	return (pa);
}

/*
 *  Initialize the pmap module.
 *  Called by vm_init, to initialize any structures that the pmap
 *  system needs to map virtual memory.
 */
void
pmap_init(void)
{
	vm_size_t s;
	pt2_entry_t *pte2p, pte2;
	u_int i, pte1_idx, pv_npg;

	PDEBUG(1, printf("%s: phys_start = %#x\n", __func__, PHYSADDR));

	/*
	 * Initialize the vm page array entries for kernel pmap's
	 * L2 page table pages allocated in advance.
	 */
	pte1_idx = pte1_index(KERNBASE - PT2MAP_SIZE);
	pte2p = kern_pt2tab_entry(KERNBASE - PT2MAP_SIZE);
	for (i = 0; i < nkpt2pg + NPG_IN_PT2TAB; i++, pte2p++) {
		vm_paddr_t pa;
		vm_page_t m;

		pte2 = pte2_load(pte2p);
		KASSERT(pte2_is_valid(pte2), ("%s: no valid entry", __func__));

		pa = pte2_pa(pte2);
		m = PHYS_TO_VM_PAGE(pa);
		KASSERT(m >= vm_page_array &&
		    m < &vm_page_array[vm_page_array_size],
		    ("%s: L2 page table page is out of range", __func__));

		m->pindex = pte1_idx;
		m->phys_addr = pa;
		pte1_idx += NPT2_IN_PG;
	}

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
	 * Are large page mappings enabled?
	 */
	TUNABLE_INT_FETCH("vm.pmap.sp_enabled", &sp_enabled);
	if (sp_enabled) {
		KASSERT(MAXPAGESIZES > 1 && pagesizes[1] == 0,
		    ("%s: can't assign to pagesizes[1]", __func__));
		pagesizes[1] = PTE1_SIZE;
	}

	/*
	 * Calculate the size of the pv head table for sections.
	 * Handle the possibility that "vm_phys_segs[...].end" is zero.
	 * Note that the table is only for sections which could be promoted.
	 */
	first_managed_pa = pte1_trunc(vm_phys_segs[0].start);
	pv_npg = (pte1_trunc(vm_phys_segs[vm_phys_nsegs - 1].end - PAGE_SIZE)
	    - first_managed_pa) / PTE1_SIZE + 1;

	/*
	 * Allocate memory for the pv head table for sections.
	 */
	s = (vm_size_t)(pv_npg * sizeof(struct md_page));
	s = round_page(s);
	pv_table = (struct md_page *)kmem_malloc(s, M_WAITOK | M_ZERO);
	for (i = 0; i < pv_npg; i++)
		TAILQ_INIT(&pv_table[i].pv_list);

	pv_maxchunks = MAX(pv_entry_max / _NPCPV, maxproc);
	pv_chunkbase = (struct pv_chunk *)kva_alloc(PAGE_SIZE * pv_maxchunks);
	if (pv_chunkbase == NULL)
		panic("%s: not enough kvm for pv chunks", __func__);
	pmap_pte2list_init(&pv_vafree, pv_chunkbase, pv_maxchunks);
}

/*
 *  Add a list of wired pages to the kva
 *  this routine is only used for temporary
 *  kernel mappings that do not need to have
 *  page modification or references recorded.
 *  Note that old mappings are simply written
 *  over.  The page *must* be wired.
 *  Note: SMP coherent.  Uses a ranged shootdown IPI.
 */
void
pmap_qenter(vm_offset_t sva, vm_page_t *ma, int count)
{
	u_int anychanged;
	pt2_entry_t *epte2p, *pte2p, pte2;
	vm_page_t m;
	vm_paddr_t pa;

	anychanged = 0;
	pte2p = pt2map_entry(sva);
	epte2p = pte2p + count;
	while (pte2p < epte2p) {
		m = *ma++;
		pa = VM_PAGE_TO_PHYS(m);
		pte2 = pte2_load(pte2p);
		if ((pte2_pa(pte2) != pa) ||
		    (pte2_attr(pte2) != vm_page_pte2_attr(m))) {
			anychanged++;
			pte2_store(pte2p, PTE2_KERN(pa, PTE2_AP_KRW,
			    vm_page_pte2_attr(m)));
		}
		pte2p++;
	}
	if (__predict_false(anychanged))
		tlb_flush_range(sva, count * PAGE_SIZE);
}

/*
 *  This routine tears out page mappings from the
 *  kernel -- it is meant only for temporary mappings.
 *  Note: SMP coherent.  Uses a ranged shootdown IPI.
 */
void
pmap_qremove(vm_offset_t sva, int count)
{
	vm_offset_t va;

	va = sva;
	while (count-- > 0) {
		pmap_kremove(va);
		va += PAGE_SIZE;
	}
	tlb_flush_range(sva, va - sva);
}

/*
 *  Are we current address space or kernel?
 */
static __inline int
pmap_is_current(pmap_t pmap)
{

	return (pmap == kernel_pmap ||
		(pmap == vmspace_pmap(curthread->td_proc->p_vmspace)));
}

/*
 *  If the given pmap is not the current or kernel pmap, the returned
 *  pte2 must be released by passing it to pmap_pte2_release().
 */
static pt2_entry_t *
pmap_pte2(pmap_t pmap, vm_offset_t va)
{
	pt1_entry_t pte1;
	vm_paddr_t pt2pg_pa;

	pte1 = pte1_load(pmap_pte1(pmap, va));
	if (pte1_is_section(pte1))
		panic("%s: attempt to map PTE1", __func__);
	if (pte1_is_link(pte1)) {
		/* Are we current address space or kernel? */
		if (pmap_is_current(pmap))
			return (pt2map_entry(va));
		/* Note that L2 page table size is not equal to PAGE_SIZE. */
		pt2pg_pa = trunc_page(pte1_link_pa(pte1));
		mtx_lock(&PMAP2mutex);
		if (pte2_pa(pte2_load(PMAP2)) != pt2pg_pa) {
			pte2_store(PMAP2, PTE2_KPT(pt2pg_pa));
			tlb_flush((vm_offset_t)PADDR2);
		}
		return (PADDR2 + (arm32_btop(va) & (NPTE2_IN_PG - 1)));
	}
	return (NULL);
}

/*
 *  Releases a pte2 that was obtained from pmap_pte2().
 *  Be prepared for the pte2p being NULL.
 */
static __inline void
pmap_pte2_release(pt2_entry_t *pte2p)
{

	if ((pt2_entry_t *)(trunc_page((vm_offset_t)pte2p)) == PADDR2) {
		mtx_unlock(&PMAP2mutex);
	}
}

/*
 *  Super fast pmap_pte2 routine best used when scanning
 *  the pv lists.  This eliminates many coarse-grained
 *  invltlb calls.  Note that many of the pv list
 *  scans are across different pmaps.  It is very wasteful
 *  to do an entire tlb flush for checking a single mapping.
 *
 *  If the given pmap is not the current pmap, pvh_global_lock
 *  must be held and curthread pinned to a CPU.
 */
static pt2_entry_t *
pmap_pte2_quick(pmap_t pmap, vm_offset_t va)
{
	pt1_entry_t pte1;
	vm_paddr_t pt2pg_pa;

	pte1 = pte1_load(pmap_pte1(pmap, va));
	if (pte1_is_section(pte1))
		panic("%s: attempt to map PTE1", __func__);
	if (pte1_is_link(pte1)) {
		/* Are we current address space or kernel? */
		if (pmap_is_current(pmap))
			return (pt2map_entry(va));
		rw_assert(&pvh_global_lock, RA_WLOCKED);
		KASSERT(curthread->td_pinned > 0,
		    ("%s: curthread not pinned", __func__));
		/* Note that L2 page table size is not equal to PAGE_SIZE. */
		pt2pg_pa = trunc_page(pte1_link_pa(pte1));
		if (pte2_pa(pte2_load(PMAP1)) != pt2pg_pa) {
			pte2_store(PMAP1, PTE2_KPT(pt2pg_pa));
#ifdef SMP
			PMAP1cpu = PCPU_GET(cpuid);
#endif
			tlb_flush_local((vm_offset_t)PADDR1);
			PMAP1changed++;
		} else
#ifdef SMP
		if (PMAP1cpu != PCPU_GET(cpuid)) {
			PMAP1cpu = PCPU_GET(cpuid);
			tlb_flush_local((vm_offset_t)PADDR1);
			PMAP1changedcpu++;
		} else
#endif
			PMAP1unchanged++;
		return (PADDR1 + (arm32_btop(va) & (NPTE2_IN_PG - 1)));
	}
	return (NULL);
}

/*
 *  Routine: pmap_extract
 *  Function:
 * 	Extract the physical page address associated
 *	with the given map/virtual_address pair.
 */
vm_paddr_t
pmap_extract(pmap_t pmap, vm_offset_t va)
{
	vm_paddr_t pa;
	pt1_entry_t pte1;
	pt2_entry_t *pte2p;

	PMAP_LOCK(pmap);
	pte1 = pte1_load(pmap_pte1(pmap, va));
	if (pte1_is_section(pte1))
		pa = pte1_pa(pte1) | (va & PTE1_OFFSET);
	else if (pte1_is_link(pte1)) {
		pte2p = pmap_pte2(pmap, va);
		pa = pte2_pa(pte2_load(pte2p)) | (va & PTE2_OFFSET);
		pmap_pte2_release(pte2p);
	} else
		pa = 0;
	PMAP_UNLOCK(pmap);
	return (pa);
}

/*
 *  Routine: pmap_extract_and_hold
 *  Function:
 *	Atomically extract and hold the physical page
 *	with the given pmap and virtual address pair
 *	if that mapping permits the given protection.
 */
vm_page_t
pmap_extract_and_hold(pmap_t pmap, vm_offset_t va, vm_prot_t prot)
{
	vm_paddr_t pa, lockpa;
	pt1_entry_t pte1;
	pt2_entry_t pte2, *pte2p;
	vm_page_t m;

	lockpa = 0;
	m = NULL;
	PMAP_LOCK(pmap);
retry:
	pte1 = pte1_load(pmap_pte1(pmap, va));
	if (pte1_is_section(pte1)) {
		if (!(pte1 & PTE1_RO) || !(prot & VM_PROT_WRITE)) {
			pa = pte1_pa(pte1) | (va & PTE1_OFFSET);
			if (vm_page_pa_tryrelock(pmap, pa, &lockpa))
				goto retry;
			m = PHYS_TO_VM_PAGE(pa);
			vm_page_hold(m);
		}
	} else if (pte1_is_link(pte1)) {
		pte2p = pmap_pte2(pmap, va);
		pte2 = pte2_load(pte2p);
		pmap_pte2_release(pte2p);
		if (pte2_is_valid(pte2) &&
		    (!(pte2 & PTE2_RO) || !(prot & VM_PROT_WRITE))) {
			pa = pte2_pa(pte2);
			if (vm_page_pa_tryrelock(pmap, pa, &lockpa))
				goto retry;
			m = PHYS_TO_VM_PAGE(pa);
			vm_page_hold(m);
		}
	}
	PA_UNLOCK_COND(lockpa);
	PMAP_UNLOCK(pmap);
	return (m);
}

/*
 *  Grow the number of kernel L2 page table entries, if needed.
 */
void
pmap_growkernel(vm_offset_t addr)
{
	vm_page_t m;
	vm_paddr_t pt2pg_pa, pt2_pa;
	pt1_entry_t pte1;
	pt2_entry_t pte2;

	PDEBUG(1, printf("%s: addr = %#x\n", __func__, addr));
	/*
	 * All the time kernel_vm_end is first KVA for which underlying
	 * L2 page table is either not allocated or linked from L1 page table
	 * (not considering sections). Except for two possible cases:
	 *
	 *   (1) in the very beginning as long as pmap_growkernel() was
	 *       not called, it could be first unused KVA (which is not
	 *       rounded up to PTE1_SIZE),
	 *
	 *   (2) when all KVA space is mapped and vm_map_max(kernel_map)
	 *       address is not rounded up to PTE1_SIZE. (For example,
	 *       it could be 0xFFFFFFFF.)
	 */
	kernel_vm_end = pte1_roundup(kernel_vm_end);
	mtx_assert(&kernel_map->system_mtx, MA_OWNED);
	addr = roundup2(addr, PTE1_SIZE);
	if (addr - 1 >= vm_map_max(kernel_map))
		addr = vm_map_max(kernel_map);
	while (kernel_vm_end < addr) {
		pte1 = pte1_load(kern_pte1(kernel_vm_end));
		if (pte1_is_valid(pte1)) {
			kernel_vm_end += PTE1_SIZE;
			if (kernel_vm_end - 1 >= vm_map_max(kernel_map)) {
				kernel_vm_end = vm_map_max(kernel_map);
				break;
			}
			continue;
		}

		/*
		 * kernel_vm_end_new is used in pmap_pinit() when kernel
		 * mappings are entered to new pmap all at once to avoid race
		 * between pmap_kenter_pte1() and kernel_vm_end increase.
		 * The same aplies to pmap_kenter_pt2tab().
		 */
		kernel_vm_end_new = kernel_vm_end + PTE1_SIZE;

		pte2 = pt2tab_load(kern_pt2tab_entry(kernel_vm_end));
		if (!pte2_is_valid(pte2)) {
			/*
			 * Install new PT2s page into kernel PT2TAB.
			 */
			m = vm_page_alloc(NULL,
			    pte1_index(kernel_vm_end) & ~PT2PG_MASK,
			    VM_ALLOC_INTERRUPT | VM_ALLOC_NOOBJ |
			    VM_ALLOC_WIRED | VM_ALLOC_ZERO);
			if (m == NULL)
				panic("%s: no memory to grow kernel", __func__);
			/*
			 * QQQ: To link all new L2 page tables from L1 page
			 *      table now and so pmap_kenter_pte1() them
			 *      at once together with pmap_kenter_pt2tab()
			 *      could be nice speed up. However,
			 *      pmap_growkernel() does not happen so often...
			 * QQQ: The other TTBR is another option.
			 */
			pt2pg_pa = pmap_pt2pg_init(kernel_pmap, kernel_vm_end,
			    m);
		} else
			pt2pg_pa = pte2_pa(pte2);

		pt2_pa = page_pt2pa(pt2pg_pa, pte1_index(kernel_vm_end));
		pmap_kenter_pte1(kernel_vm_end, PTE1_LINK(pt2_pa));

		kernel_vm_end = kernel_vm_end_new;
		if (kernel_vm_end - 1 >= vm_map_max(kernel_map)) {
			kernel_vm_end = vm_map_max(kernel_map);
			break;
		}
	}
}

static int
kvm_size(SYSCTL_HANDLER_ARGS)
{
	unsigned long ksize = vm_max_kernel_address - KERNBASE;

	return (sysctl_handle_long(oidp, &ksize, 0, req));
}
SYSCTL_PROC(_vm, OID_AUTO, kvm_size, CTLTYPE_LONG|CTLFLAG_RD,
    0, 0, kvm_size, "IU", "Size of KVM");

static int
kvm_free(SYSCTL_HANDLER_ARGS)
{
	unsigned long kfree = vm_max_kernel_address - kernel_vm_end;

	return (sysctl_handle_long(oidp, &kfree, 0, req));
}
SYSCTL_PROC(_vm, OID_AUTO, kvm_free, CTLTYPE_LONG|CTLFLAG_RD,
    0, 0, kvm_free, "IU", "Amount of KVM free");

/***********************************************
 *
 *  Pmap allocation/deallocation routines.
 *
 ***********************************************/

/*
 *  Initialize the pmap for the swapper process.
 */
void
pmap_pinit0(pmap_t pmap)
{
	PDEBUG(1, printf("%s: pmap = %p\n", __func__, pmap));

	PMAP_LOCK_INIT(pmap);

	/*
	 * Kernel page table directory and pmap stuff around is already
	 * initialized, we are using it right now and here. So, finish
	 * only PMAP structures initialization for process0 ...
	 *
	 * Since the L1 page table and PT2TAB is shared with the kernel pmap,
	 * which is already included in the list "allpmaps", this pmap does
	 * not need to be inserted into that list.
	 */
	pmap->pm_pt1 = kern_pt1;
	pmap->pm_pt2tab = kern_pt2tab;
	CPU_ZERO(&pmap->pm_active);
	PCPU_SET(curpmap, pmap);
	TAILQ_INIT(&pmap->pm_pvchunk);
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);
	CPU_SET(0, &pmap->pm_active);
}

static __inline void
pte1_copy_nosync(pt1_entry_t *spte1p, pt1_entry_t *dpte1p, vm_offset_t sva,
    vm_offset_t eva)
{
	u_int idx, count;

	idx = pte1_index(sva);
	count = (pte1_index(eva) - idx + 1) * sizeof(pt1_entry_t);
	bcopy(spte1p + idx, dpte1p + idx, count);
}

static __inline void
pt2tab_copy_nosync(pt2_entry_t *spte2p, pt2_entry_t *dpte2p, vm_offset_t sva,
    vm_offset_t eva)
{
	u_int idx, count;

	idx = pt2tab_index(sva);
	count = (pt2tab_index(eva) - idx + 1) * sizeof(pt2_entry_t);
	bcopy(spte2p + idx, dpte2p + idx, count);
}

/*
 *  Initialize a preallocated and zeroed pmap structure,
 *  such as one in a vmspace structure.
 */
int
pmap_pinit(pmap_t pmap)
{
	pt1_entry_t *pte1p;
	pt2_entry_t *pte2p;
	vm_paddr_t pa, pt2tab_pa;
	u_int i;

	PDEBUG(6, printf("%s: pmap = %p, pm_pt1 = %p\n", __func__, pmap,
	    pmap->pm_pt1));

	/*
	 * No need to allocate L2 page table space yet but we do need
	 * a valid L1 page table and PT2TAB table.
	 *
	 * Install shared kernel mappings to these tables. It's a little
	 * tricky as some parts of KVA are reserved for vectors, devices,
	 * and whatever else. These parts are supposed to be above
	 * vm_max_kernel_address. Thus two regions should be installed:
	 *
	 *   (1) <KERNBASE, kernel_vm_end),
	 *   (2) <vm_max_kernel_address, 0xFFFFFFFF>.
	 *
	 * QQQ: The second region should be stable enough to be installed
	 *      only once in time when the tables are allocated.
	 * QQQ: Maybe copy of both regions at once could be faster ...
	 * QQQ: Maybe the other TTBR is an option.
	 *
	 * Finally, install own PT2TAB table to these tables.
	 */

	if (pmap->pm_pt1 == NULL) {
		pmap->pm_pt1 = (pt1_entry_t *)kmem_alloc_contig(NB_IN_PT1,
		    M_NOWAIT | M_ZERO, 0, -1UL, NB_IN_PT1, 0, pt_memattr);
		if (pmap->pm_pt1 == NULL)
			return (0);
	}
	if (pmap->pm_pt2tab == NULL) {
		/*
		 * QQQ: (1) PT2TAB must be contiguous. If PT2TAB is one page
		 *      only, what should be the only size for 32 bit systems,
		 *      then we could allocate it with vm_page_alloc() and all
		 *      the stuff needed as other L2 page table pages.
		 *      (2) Note that a process PT2TAB is special L2 page table
		 *      page. Its mapping in kernel_arena is permanent and can
		 *      be used no matter which process is current. Its mapping
		 *      in PT2MAP can be used only for current process.
		 */
		pmap->pm_pt2tab = (pt2_entry_t *)kmem_alloc_attr(NB_IN_PT2TAB,
		    M_NOWAIT | M_ZERO, 0, -1UL, pt_memattr);
		if (pmap->pm_pt2tab == NULL) {
			/*
			 * QQQ: As struct pmap is allocated from UMA with
			 *      UMA_ZONE_NOFREE flag, it's important to leave
			 *      no allocation in pmap if initialization failed.
			 */
			kmem_free((vm_offset_t)pmap->pm_pt1, NB_IN_PT1);
			pmap->pm_pt1 = NULL;
			return (0);
		}
		/*
		 * QQQ: Each L2 page table page vm_page_t has pindex set to
		 *      pte1 index of virtual address mapped by this page.
		 *      It's not valid for non kernel PT2TABs themselves.
		 *      The pindex of these pages can not be altered because
		 *      of the way how they are allocated now. However, it
		 *      should not be a problem.
		 */
	}

	mtx_lock_spin(&allpmaps_lock);
	/*
	 * To avoid race with pmap_kenter_pte1() and pmap_kenter_pt2tab(),
	 * kernel_vm_end_new is used here instead of kernel_vm_end.
	 */
	pte1_copy_nosync(kern_pt1, pmap->pm_pt1, KERNBASE,
	    kernel_vm_end_new - 1);
	pte1_copy_nosync(kern_pt1, pmap->pm_pt1, vm_max_kernel_address,
	    0xFFFFFFFF);
	pt2tab_copy_nosync(kern_pt2tab, pmap->pm_pt2tab, KERNBASE,
	    kernel_vm_end_new - 1);
	pt2tab_copy_nosync(kern_pt2tab, pmap->pm_pt2tab, vm_max_kernel_address,
	    0xFFFFFFFF);
	LIST_INSERT_HEAD(&allpmaps, pmap, pm_list);
	mtx_unlock_spin(&allpmaps_lock);

	/*
	 * Store PT2MAP PT2 pages (a.k.a. PT2TAB) in PT2TAB itself.
	 * I.e. self reference mapping.  The PT2TAB is private, however mapped
	 * into shared PT2MAP space, so the mapping should be not global.
	 */
	pt2tab_pa = vtophys(pmap->pm_pt2tab);
	pte2p = pmap_pt2tab_entry(pmap, (vm_offset_t)PT2MAP);
	for (pa = pt2tab_pa, i = 0; i < NPG_IN_PT2TAB; i++, pa += PTE2_SIZE) {
		pt2tab_store(pte2p++, PTE2_KPT_NG(pa));
	}

	/* Insert PT2MAP PT2s into pmap PT1. */
	pte1p = pmap_pte1(pmap, (vm_offset_t)PT2MAP);
	for (pa = pt2tab_pa, i = 0; i < NPT2_IN_PT2TAB; i++, pa += NB_IN_PT2) {
		pte1_store(pte1p++, PTE1_LINK(pa));
	}

	/*
	 * Now synchronize new mapping which was made above.
	 */
	pte1_sync_range(pmap->pm_pt1, NB_IN_PT1);
	pte2_sync_range(pmap->pm_pt2tab, NB_IN_PT2TAB);

	CPU_ZERO(&pmap->pm_active);
	TAILQ_INIT(&pmap->pm_pvchunk);
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);

	return (1);
}

#ifdef INVARIANTS
static boolean_t
pt2tab_user_is_empty(pt2_entry_t *tab)
{
	u_int i, end;

	end = pt2tab_index(VM_MAXUSER_ADDRESS);
	for (i = 0; i < end; i++)
		if (tab[i] != 0) return (FALSE);
	return (TRUE);
}
#endif
/*
 *  Release any resources held by the given physical map.
 *  Called when a pmap initialized by pmap_pinit is being released.
 *  Should only be called if the map contains no valid mappings.
 */
void
pmap_release(pmap_t pmap)
{
#ifdef INVARIANTS
	vm_offset_t start, end;
#endif
	KASSERT(pmap->pm_stats.resident_count == 0,
	    ("%s: pmap resident count %ld != 0", __func__,
	    pmap->pm_stats.resident_count));
	KASSERT(pt2tab_user_is_empty(pmap->pm_pt2tab),
	    ("%s: has allocated user PT2(s)", __func__));
	KASSERT(CPU_EMPTY(&pmap->pm_active),
	    ("%s: pmap %p is active on some CPU(s)", __func__, pmap));

	mtx_lock_spin(&allpmaps_lock);
	LIST_REMOVE(pmap, pm_list);
	mtx_unlock_spin(&allpmaps_lock);

#ifdef INVARIANTS
	start = pte1_index(KERNBASE) * sizeof(pt1_entry_t);
	end = (pte1_index(0xFFFFFFFF) + 1) * sizeof(pt1_entry_t);
	bzero((char *)pmap->pm_pt1 + start, end - start);

	start = pt2tab_index(KERNBASE) * sizeof(pt2_entry_t);
	end = (pt2tab_index(0xFFFFFFFF) + 1) * sizeof(pt2_entry_t);
	bzero((char *)pmap->pm_pt2tab + start, end - start);
#endif
	/*
	 * We are leaving PT1 and PT2TAB allocated on released pmap,
	 * so hopefully UMA vmspace_zone will always be inited with
	 * UMA_ZONE_NOFREE flag.
	 */
}

/*********************************************************
 *
 *  L2 table pages and their pages management routines.
 *
 *********************************************************/

/*
 *  Virtual interface for L2 page table wire counting.
 *
 *  Each L2 page table in a page has own counter which counts a number of
 *  valid mappings in a table. Global page counter counts mappings in all
 *  tables in a page plus a single itself mapping in PT2TAB.
 *
 *  During a promotion we leave the associated L2 page table counter
 *  untouched, so the table (strictly speaking a page which holds it)
 *  is never freed if promoted.
 *
 *  If a page m->wire_count == 1 then no valid mappings exist in any L2 page
 *  table in the page and the page itself is only mapped in PT2TAB.
 */

static __inline void
pt2_wirecount_init(vm_page_t m)
{
	u_int i;

	/*
	 * Note: A page m is allocated with VM_ALLOC_WIRED flag and
	 *       m->wire_count should be already set correctly.
	 *       So, there is no need to set it again herein.
	 */
	for (i = 0; i < NPT2_IN_PG; i++)
		m->md.pt2_wirecount[i] = 0;
}

static __inline void
pt2_wirecount_inc(vm_page_t m, uint32_t pte1_idx)
{

	/*
	 * Note: A just modificated pte2 (i.e. already allocated)
	 *       is acquiring one extra reference which must be
	 *       explicitly cleared. It influences the KASSERTs herein.
	 *       All L2 page tables in a page always belong to the same
	 *       pmap, so we allow only one extra reference for the page.
	 */
	KASSERT(m->md.pt2_wirecount[pte1_idx & PT2PG_MASK] < (NPTE2_IN_PT2 + 1),
	    ("%s: PT2 is overflowing ...", __func__));
	KASSERT(m->wire_count <= (NPTE2_IN_PG + 1),
	    ("%s: PT2PG is overflowing ...", __func__));

	m->wire_count++;
	m->md.pt2_wirecount[pte1_idx & PT2PG_MASK]++;
}

static __inline void
pt2_wirecount_dec(vm_page_t m, uint32_t pte1_idx)
{

	KASSERT(m->md.pt2_wirecount[pte1_idx & PT2PG_MASK] != 0,
	    ("%s: PT2 is underflowing ...", __func__));
	KASSERT(m->wire_count > 1,
	    ("%s: PT2PG is underflowing ...", __func__));

	m->wire_count--;
	m->md.pt2_wirecount[pte1_idx & PT2PG_MASK]--;
}

static __inline void
pt2_wirecount_set(vm_page_t m, uint32_t pte1_idx, uint16_t count)
{

	KASSERT(count <= NPTE2_IN_PT2,
	    ("%s: invalid count %u", __func__, count));
	KASSERT(m->wire_count >  m->md.pt2_wirecount[pte1_idx & PT2PG_MASK],
	    ("%s: PT2PG corrupting (%u, %u) ...", __func__, m->wire_count,
	    m->md.pt2_wirecount[pte1_idx & PT2PG_MASK]));

	m->wire_count -= m->md.pt2_wirecount[pte1_idx & PT2PG_MASK];
	m->wire_count += count;
	m->md.pt2_wirecount[pte1_idx & PT2PG_MASK] = count;

	KASSERT(m->wire_count <= (NPTE2_IN_PG + 1),
	    ("%s: PT2PG is overflowed (%u) ...", __func__, m->wire_count));
}

static __inline uint32_t
pt2_wirecount_get(vm_page_t m, uint32_t pte1_idx)
{

	return (m->md.pt2_wirecount[pte1_idx & PT2PG_MASK]);
}

static __inline boolean_t
pt2_is_empty(vm_page_t m, vm_offset_t va)
{

	return (m->md.pt2_wirecount[pte1_index(va) & PT2PG_MASK] == 0);
}

static __inline boolean_t
pt2_is_full(vm_page_t m, vm_offset_t va)
{

	return (m->md.pt2_wirecount[pte1_index(va) & PT2PG_MASK] ==
	    NPTE2_IN_PT2);
}

static __inline boolean_t
pt2pg_is_empty(vm_page_t m)
{

	return (m->wire_count == 1);
}

/*
 *  This routine is called if the L2 page table
 *  is not mapped correctly.
 */
static vm_page_t
_pmap_allocpte2(pmap_t pmap, vm_offset_t va, u_int flags)
{
	uint32_t pte1_idx;
	pt1_entry_t *pte1p;
	pt2_entry_t pte2;
	vm_page_t  m;
	vm_paddr_t pt2pg_pa, pt2_pa;

	pte1_idx = pte1_index(va);
	pte1p = pmap->pm_pt1 + pte1_idx;

	KASSERT(pte1_load(pte1p) == 0,
	    ("%s: pm_pt1[%#x] is not zero: %#x", __func__, pte1_idx,
	    pte1_load(pte1p)));

	pte2 = pt2tab_load(pmap_pt2tab_entry(pmap, va));
	if (!pte2_is_valid(pte2)) {
		/*
		 * Install new PT2s page into pmap PT2TAB.
		 */
		m = vm_page_alloc(NULL, pte1_idx & ~PT2PG_MASK,
		    VM_ALLOC_NOOBJ | VM_ALLOC_WIRED | VM_ALLOC_ZERO);
		if (m == NULL) {
			if ((flags & PMAP_ENTER_NOSLEEP) == 0) {
				PMAP_UNLOCK(pmap);
				rw_wunlock(&pvh_global_lock);
				vm_wait(NULL);
				rw_wlock(&pvh_global_lock);
				PMAP_LOCK(pmap);
			}

			/*
			 * Indicate the need to retry.  While waiting,
			 * the L2 page table page may have been allocated.
			 */
			return (NULL);
		}
		pmap->pm_stats.resident_count++;
		pt2pg_pa = pmap_pt2pg_init(pmap, va, m);
	} else {
		pt2pg_pa = pte2_pa(pte2);
		m = PHYS_TO_VM_PAGE(pt2pg_pa);
	}

	pt2_wirecount_inc(m, pte1_idx);
	pt2_pa = page_pt2pa(pt2pg_pa, pte1_idx);
	pte1_store(pte1p, PTE1_LINK(pt2_pa));

	return (m);
}

static vm_page_t
pmap_allocpte2(pmap_t pmap, vm_offset_t va, u_int flags)
{
	u_int pte1_idx;
	pt1_entry_t *pte1p, pte1;
	vm_page_t m;

	pte1_idx = pte1_index(va);
retry:
	pte1p = pmap->pm_pt1 + pte1_idx;
	pte1 = pte1_load(pte1p);

	/*
	 * This supports switching from a 1MB page to a
	 * normal 4K page.
	 */
	if (pte1_is_section(pte1)) {
		(void)pmap_demote_pte1(pmap, pte1p, va);
		/*
		 * Reload pte1 after demotion.
		 *
		 * Note: Demotion can even fail as either PT2 is not find for
		 *       the virtual address or PT2PG can not be allocated.
		 */
		pte1 = pte1_load(pte1p);
	}

	/*
	 * If the L2 page table page is mapped, we just increment the
	 * hold count, and activate it.
	 */
	if (pte1_is_link(pte1)) {
		m = PHYS_TO_VM_PAGE(pte1_link_pa(pte1));
		pt2_wirecount_inc(m, pte1_idx);
	} else  {
		/*
		 * Here if the PT2 isn't mapped, or if it has
		 * been deallocated.
		 */
		m = _pmap_allocpte2(pmap, va, flags);
		if (m == NULL && (flags & PMAP_ENTER_NOSLEEP) == 0)
			goto retry;
	}

	return (m);
}

/*
 *  Schedule the specified unused L2 page table page to be freed. Specifically,
 *  add the page to the specified list of pages that will be released to the
 *  physical memory manager after the TLB has been updated.
 */
static __inline void
pmap_add_delayed_free_list(vm_page_t m, struct spglist *free)
{

	/*
	 * Put page on a list so that it is released after
	 * *ALL* TLB shootdown is done
	 */
#ifdef PMAP_DEBUG
	pmap_zero_page_check(m);
#endif
	m->flags |= PG_ZERO;
	SLIST_INSERT_HEAD(free, m, plinks.s.ss);
}

/*
 *  Unwire L2 page tables page.
 */
static void
pmap_unwire_pt2pg(pmap_t pmap, vm_offset_t va, vm_page_t m)
{
	pt1_entry_t *pte1p, opte1 __unused;
	pt2_entry_t *pte2p;
	uint32_t i;

	KASSERT(pt2pg_is_empty(m),
	    ("%s: pmap %p PT2PG %p wired", __func__, pmap, m));

	/*
	 * Unmap all L2 page tables in the page from L1 page table.
	 *
	 * QQQ: Individual L2 page tables (except the last one) can be unmapped
	 * earlier. However, we are doing that this way.
	 */
	KASSERT(m->pindex == (pte1_index(va) & ~PT2PG_MASK),
	    ("%s: pmap %p va %#x PT2PG %p bad index", __func__, pmap, va, m));
	pte1p = pmap->pm_pt1 + m->pindex;
	for (i = 0; i < NPT2_IN_PG; i++, pte1p++) {
		KASSERT(m->md.pt2_wirecount[i] == 0,
		    ("%s: pmap %p PT2 %u (PG %p) wired", __func__, pmap, i, m));
		opte1 = pte1_load(pte1p);
		if (pte1_is_link(opte1)) {
			pte1_clear(pte1p);
			/*
			 * Flush intermediate TLB cache.
			 */
			pmap_tlb_flush(pmap, (m->pindex + i) << PTE1_SHIFT);
		}
#ifdef INVARIANTS
		else
			KASSERT((opte1 == 0) || pte1_is_section(opte1),
			    ("%s: pmap %p va %#x bad pte1 %x at %u", __func__,
			    pmap, va, opte1, i));
#endif
	}

	/*
	 * Unmap the page from PT2TAB.
	 */
	pte2p = pmap_pt2tab_entry(pmap, va);
	(void)pt2tab_load_clear(pte2p);
	pmap_tlb_flush(pmap, pt2map_pt2pg(va));

	m->wire_count = 0;
	pmap->pm_stats.resident_count--;

	/*
	 * This barrier is so that the ordinary store unmapping
	 * the L2 page table page is globally performed before TLB shoot-
	 * down is begun.
	 */
	wmb();
	vm_wire_sub(1);
}

/*
 *  Decrements a L2 page table page's wire count, which is used to record the
 *  number of valid page table entries within the page.  If the wire count
 *  drops to zero, then the page table page is unmapped.  Returns TRUE if the
 *  page table page was unmapped and FALSE otherwise.
 */
static __inline boolean_t
pmap_unwire_pt2(pmap_t pmap, vm_offset_t va, vm_page_t m, struct spglist *free)
{
	pt2_wirecount_dec(m, pte1_index(va));
	if (pt2pg_is_empty(m)) {
		/*
		 * QQQ: Wire count is zero, so whole page should be zero and
		 *      we can set PG_ZERO flag to it.
		 *      Note that when promotion is enabled, it takes some
		 *      more efforts. See pmap_unwire_pt2_all() below.
		 */
		pmap_unwire_pt2pg(pmap, va, m);
		pmap_add_delayed_free_list(m, free);
		return (TRUE);
	} else
		return (FALSE);
}

/*
 *  Drop a L2 page table page's wire count at once, which is used to record
 *  the number of valid L2 page table entries within the page. If the wire
 *  count drops to zero, then the L2 page table page is unmapped.
 */
static __inline void
pmap_unwire_pt2_all(pmap_t pmap, vm_offset_t va, vm_page_t m,
    struct spglist *free)
{
	u_int pte1_idx = pte1_index(va);

	KASSERT(m->pindex == (pte1_idx & ~PT2PG_MASK),
		("%s: PT2 page's pindex is wrong", __func__));
	KASSERT(m->wire_count > pt2_wirecount_get(m, pte1_idx),
	    ("%s: bad pt2 wire count %u > %u", __func__, m->wire_count,
	    pt2_wirecount_get(m, pte1_idx)));

	/*
	 * It's possible that the L2 page table was never used.
	 * It happened in case that a section was created without promotion.
	 */
	if (pt2_is_full(m, va)) {
		pt2_wirecount_set(m, pte1_idx, 0);

		/*
		 * QQQ: We clear L2 page table now, so when L2 page table page
		 *      is going to be freed, we can set it PG_ZERO flag ...
		 *      This function is called only on section mappings, so
		 *      hopefully it's not to big overload.
		 *
		 * XXX: If pmap is current, existing PT2MAP mapping could be
		 *      used for zeroing.
		 */
		pmap_zero_page_area(m, page_pt2off(pte1_idx), NB_IN_PT2);
	}
#ifdef INVARIANTS
	else
		KASSERT(pt2_is_empty(m, va), ("%s: PT2 is not empty (%u)",
		    __func__, pt2_wirecount_get(m, pte1_idx)));
#endif
	if (pt2pg_is_empty(m)) {
		pmap_unwire_pt2pg(pmap, va, m);
		pmap_add_delayed_free_list(m, free);
	}
}

/*
 *  After removing a L2 page table entry, this routine is used to
 *  conditionally free the page, and manage the hold/wire counts.
 */
static boolean_t
pmap_unuse_pt2(pmap_t pmap, vm_offset_t va, struct spglist *free)
{
	pt1_entry_t pte1;
	vm_page_t mpte;

	if (va >= VM_MAXUSER_ADDRESS)
		return (FALSE);
	pte1 = pte1_load(pmap_pte1(pmap, va));
	mpte = PHYS_TO_VM_PAGE(pte1_link_pa(pte1));
	return (pmap_unwire_pt2(pmap, va, mpte, free));
}

/*************************************
 *
 *  Page management routines.
 *
 *************************************/

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

SYSCTL_INT(_vm_pmap, OID_AUTO, pv_entry_count, CTLFLAG_RD, &pv_entry_count, 0,
	"Current number of pv entries");

#ifdef PV_STATS
static int pc_chunk_count, pc_chunk_allocs, pc_chunk_frees, pc_chunk_tryfail;

SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_count, CTLFLAG_RD, &pc_chunk_count, 0,
    "Current number of pv entry chunks");
SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_allocs, CTLFLAG_RD, &pc_chunk_allocs, 0,
    "Current number of pv entry chunks allocated");
SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_frees, CTLFLAG_RD, &pc_chunk_frees, 0,
    "Current number of pv entry chunks frees");
SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_tryfail, CTLFLAG_RD, &pc_chunk_tryfail,
    0, "Number of times tried to get a chunk page but failed.");

static long pv_entry_frees, pv_entry_allocs;
static int pv_entry_spare;

SYSCTL_LONG(_vm_pmap, OID_AUTO, pv_entry_frees, CTLFLAG_RD, &pv_entry_frees, 0,
    "Current number of pv entry frees");
SYSCTL_LONG(_vm_pmap, OID_AUTO, pv_entry_allocs, CTLFLAG_RD, &pv_entry_allocs,
    0, "Current number of pv entry allocs");
SYSCTL_INT(_vm_pmap, OID_AUTO, pv_entry_spare, CTLFLAG_RD, &pv_entry_spare, 0,
    "Current number of spare pv entries");
#endif

/*
 *  Is given page managed?
 */
static __inline bool
is_managed(vm_paddr_t pa)
{
	vm_page_t m;

	m = PHYS_TO_VM_PAGE(pa);
	if (m == NULL)
		return (false);
	return ((m->oflags & VPO_UNMANAGED) == 0);
}

static __inline bool
pte1_is_managed(pt1_entry_t pte1)
{

	return (is_managed(pte1_pa(pte1)));
}

static __inline bool
pte2_is_managed(pt2_entry_t pte2)
{

	return (is_managed(pte2_pa(pte2)));
}

/*
 *  We are in a serious low memory condition.  Resort to
 *  drastic measures to free some pages so we can allocate
 *  another pv entry chunk.
 */
static vm_page_t
pmap_pv_reclaim(pmap_t locked_pmap)
{
	struct pch newtail;
	struct pv_chunk *pc;
	struct md_page *pvh;
	pt1_entry_t *pte1p;
	pmap_t pmap;
	pt2_entry_t *pte2p, tpte2;
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
				bit = ffs(inuse) - 1;
				pv = &pc->pc_pventry[field * 32 + bit];
				va = pv->pv_va;
				pte1p = pmap_pte1(pmap, va);
				if (pte1_is_section(pte1_load(pte1p)))
					continue;
				pte2p = pmap_pte2(pmap, va);
				tpte2 = pte2_load(pte2p);
				if ((tpte2 & PTE2_W) == 0)
					tpte2 = pte2_load_clear(pte2p);
				pmap_pte2_release(pte2p);
				if ((tpte2 & PTE2_W) != 0)
					continue;
				KASSERT(tpte2 != 0,
				    ("pmap_pv_reclaim: pmap %p va %#x zero pte",
				    pmap, va));
				pmap_tlb_flush(pmap, va);
				m = PHYS_TO_VM_PAGE(pte2_pa(tpte2));
				if (pte2_is_dirty(tpte2))
					vm_page_dirty(m);
				if ((tpte2 & PTE2_A) != 0)
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
				pmap_unuse_pt2(pmap, va, &free);
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
			pmap_pte2list_free(&pv_vafree, (vm_offset_t)pc);
			break;
		}
	}
out:
	TAILQ_CONCAT(&pv_chunks, &newtail, pc_lru);
	if (pmap != NULL) {
		if (pmap != locked_pmap)
			PMAP_UNLOCK(pmap);
	}
	if (m_pc == NULL && pv_vafree != 0 && SLIST_EMPTY(&free)) {
		m_pc = SLIST_FIRST(&free);
		SLIST_REMOVE_HEAD(&free, plinks.s.ss);
		/* Recycle a freed page table page. */
		m_pc->wire_count = 1;
		vm_wire_add(1);
	}
	vm_page_free_pages_toq(&free, false);
	return (m_pc);
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
	pmap_pte2list_free(&pv_vafree, (vm_offset_t)pc);
}

/*
 *  Free the pv_entry back to the free list.
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

/*
 *  Get a new pv_entry, allocating a block from the system
 *  when needed.
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
				bit = ffs(pc->pc_map[field]) - 1;
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
	 * Access to the pte2list "pv_vafree" is synchronized by the pvh
	 * global lock.  If "pv_vafree" is currently non-empty, it will
	 * remain non-empty until pmap_pte2list_alloc() completes.
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
	pc = (struct pv_chunk *)pmap_pte2list_alloc(&pv_vafree);
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

/*
 *  Create a pv entry for page at pa for
 *  (pmap, va).
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

static void
pmap_pv_demote_pte1(pmap_t pmap, vm_offset_t va, vm_paddr_t pa)
{
	struct md_page *pvh;
	pv_entry_t pv;
	vm_offset_t va_last;
	vm_page_t m;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	KASSERT((pa & PTE1_OFFSET) == 0,
	    ("pmap_pv_demote_pte1: pa is not 1mpage aligned"));

	/*
	 * Transfer the 1mpage's pv entry for this mapping to the first
	 * page's pv list.
	 */
	pvh = pa_to_pvh(pa);
	va = pte1_trunc(va);
	pv = pmap_pvh_remove(pvh, pmap, va);
	KASSERT(pv != NULL, ("pmap_pv_demote_pte1: pv not found"));
	m = PHYS_TO_VM_PAGE(pa);
	TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_next);
	/* Instantiate the remaining NPTE2_IN_PT2 - 1 pv entries. */
	va_last = va + PTE1_SIZE - PAGE_SIZE;
	do {
		m++;
		KASSERT((m->oflags & VPO_UNMANAGED) == 0,
		    ("pmap_pv_demote_pte1: page %p is not managed", m));
		va += PAGE_SIZE;
		pmap_insert_entry(pmap, va, m);
	} while (va < va_last);
}

#if VM_NRESERVLEVEL > 0
static void
pmap_pv_promote_pte1(pmap_t pmap, vm_offset_t va, vm_paddr_t pa)
{
	struct md_page *pvh;
	pv_entry_t pv;
	vm_offset_t va_last;
	vm_page_t m;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	KASSERT((pa & PTE1_OFFSET) == 0,
	    ("pmap_pv_promote_pte1: pa is not 1mpage aligned"));

	/*
	 * Transfer the first page's pv entry for this mapping to the
	 * 1mpage's pv list.  Aside from avoiding the cost of a call
	 * to get_pv_entry(), a transfer avoids the possibility that
	 * get_pv_entry() calls pmap_pv_reclaim() and that pmap_pv_reclaim()
	 * removes one of the mappings that is being promoted.
	 */
	m = PHYS_TO_VM_PAGE(pa);
	va = pte1_trunc(va);
	pv = pmap_pvh_remove(&m->md, pmap, va);
	KASSERT(pv != NULL, ("pmap_pv_promote_pte1: pv not found"));
	pvh = pa_to_pvh(pa);
	TAILQ_INSERT_TAIL(&pvh->pv_list, pv, pv_next);
	/* Free the remaining NPTE2_IN_PT2 - 1 pv entries. */
	va_last = va + PTE1_SIZE - PAGE_SIZE;
	do {
		m++;
		va += PAGE_SIZE;
		pmap_pvh_free(&m->md, pmap, va);
	} while (va < va_last);
}
#endif

/*
 *  Conditionally create a pv entry.
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
 *  Create the pv entries for each of the pages within a section.
 */
static bool
pmap_pv_insert_pte1(pmap_t pmap, vm_offset_t va, pt1_entry_t pte1, u_int flags)
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
	pvh = pa_to_pvh(pte1_pa(pte1));
	TAILQ_INSERT_TAIL(&pvh->pv_list, pv, pv_next);
	return (true);
}

static inline void
pmap_tlb_flush_pte1(pmap_t pmap, vm_offset_t va, pt1_entry_t npte1)
{

	/* Kill all the small mappings or the big one only. */
	if (pte1_is_section(npte1))
		pmap_tlb_flush_range(pmap, pte1_trunc(va), PTE1_SIZE);
	else
		pmap_tlb_flush(pmap, pte1_trunc(va));
}

/*
 *  Update kernel pte1 on all pmaps.
 *
 *  The following function is called only on one cpu with disabled interrupts.
 *  In SMP case, smp_rendezvous_cpus() is used to stop other cpus. This way
 *  nobody can invoke explicit hardware table walk during the update of pte1.
 *  Unsolicited hardware table walk can still happen, invoked by speculative
 *  data or instruction prefetch or even by speculative hardware table walk.
 *
 *  The break-before-make approach should be implemented here. However, it's
 *  not so easy to do that for kernel mappings as it would be unhappy to unmap
 *  itself unexpectedly but voluntarily.
 */
static void
pmap_update_pte1_kernel(vm_offset_t va, pt1_entry_t npte1)
{
	pmap_t pmap;
	pt1_entry_t *pte1p;

	/*
	 * Get current pmap. Interrupts should be disabled here
	 * so PCPU_GET() is done atomically.
	 */
	pmap = PCPU_GET(curpmap);
	if (pmap == NULL)
		pmap = kernel_pmap;

	/*
	 * (1) Change pte1 on current pmap.
	 * (2) Flush all obsolete TLB entries on current CPU.
	 * (3) Change pte1 on all pmaps.
	 * (4) Flush all obsolete TLB entries on all CPUs in SMP case.
	 */

	pte1p = pmap_pte1(pmap, va);
	pte1_store(pte1p, npte1);

	/* Kill all the small mappings or the big one only. */
	if (pte1_is_section(npte1)) {
		pmap_pte1_kern_promotions++;
		tlb_flush_range_local(pte1_trunc(va), PTE1_SIZE);
	} else {
		pmap_pte1_kern_demotions++;
		tlb_flush_local(pte1_trunc(va));
	}

	/*
	 * In SMP case, this function is called when all cpus are at smp
	 * rendezvous, so there is no need to use 'allpmaps_lock' lock here.
	 * In UP case, the function is called with this lock locked.
	 */
	LIST_FOREACH(pmap, &allpmaps, pm_list) {
		pte1p = pmap_pte1(pmap, va);
		pte1_store(pte1p, npte1);
	}

#ifdef SMP
	/* Kill all the small mappings or the big one only. */
	if (pte1_is_section(npte1))
		tlb_flush_range(pte1_trunc(va), PTE1_SIZE);
	else
		tlb_flush(pte1_trunc(va));
#endif
}

#ifdef SMP
struct pte1_action {
	vm_offset_t va;
	pt1_entry_t npte1;
	u_int update;		/* CPU that updates the PTE1 */
};

static void
pmap_update_pte1_action(void *arg)
{
	struct pte1_action *act = arg;

	if (act->update == PCPU_GET(cpuid))
		pmap_update_pte1_kernel(act->va, act->npte1);
}

/*
 *  Change pte1 on current pmap.
 *  Note that kernel pte1 must be changed on all pmaps.
 *
 *  According to the architecture reference manual published by ARM,
 *  the behaviour is UNPREDICTABLE when two or more TLB entries map the same VA.
 *  According to this manual, UNPREDICTABLE behaviours must never happen in
 *  a viable system. In contrast, on x86 processors, it is not specified which
 *  TLB entry mapping the virtual address will be used, but the MMU doesn't
 *  generate a bogus translation the way it does on Cortex-A8 rev 2 (Beaglebone
 *  Black).
 *
 *  It's a problem when either promotion or demotion is being done. The pte1
 *  update and appropriate TLB flush must be done atomically in general.
 */
static void
pmap_change_pte1(pmap_t pmap, pt1_entry_t *pte1p, vm_offset_t va,
    pt1_entry_t npte1)
{

	if (pmap == kernel_pmap) {
		struct pte1_action act;

		sched_pin();
		act.va = va;
		act.npte1 = npte1;
		act.update = PCPU_GET(cpuid);
		smp_rendezvous_cpus(all_cpus, smp_no_rendezvous_barrier,
		    pmap_update_pte1_action, NULL, &act);
		sched_unpin();
	} else {
		register_t cspr;

		/*
		 * Use break-before-make approach for changing userland
		 * mappings. It can cause L1 translation aborts on other
		 * cores in SMP case. So, special treatment is implemented
		 * in pmap_fault(). To reduce the likelihood that another core
		 * will be affected by the broken mapping, disable interrupts
		 * until the mapping change is completed.
		 */
		cspr = disable_interrupts(PSR_I | PSR_F);
		pte1_clear(pte1p);
		pmap_tlb_flush_pte1(pmap, va, npte1);
		pte1_store(pte1p, npte1);
		restore_interrupts(cspr);
	}
}
#else
static void
pmap_change_pte1(pmap_t pmap, pt1_entry_t *pte1p, vm_offset_t va,
    pt1_entry_t npte1)
{

	if (pmap == kernel_pmap) {
		mtx_lock_spin(&allpmaps_lock);
		pmap_update_pte1_kernel(va, npte1);
		mtx_unlock_spin(&allpmaps_lock);
	} else {
		register_t cspr;

		/*
		 * Use break-before-make approach for changing userland
		 * mappings. It's absolutely safe in UP case when interrupts
		 * are disabled.
		 */
		cspr = disable_interrupts(PSR_I | PSR_F);
		pte1_clear(pte1p);
		pmap_tlb_flush_pte1(pmap, va, npte1);
		pte1_store(pte1p, npte1);
		restore_interrupts(cspr);
	}
}
#endif

#if VM_NRESERVLEVEL > 0
/*
 *  Tries to promote the NPTE2_IN_PT2, contiguous 4KB page mappings that are
 *  within a single page table page (PT2) to a single 1MB page mapping.
 *  For promotion to occur, two conditions must be met: (1) the 4KB page
 *  mappings must map aligned, contiguous physical memory and (2) the 4KB page
 *  mappings must have identical characteristics.
 *
 *  Managed (PG_MANAGED) mappings within the kernel address space are not
 *  promoted.  The reason is that kernel PTE1s are replicated in each pmap but
 *  pmap_remove_write(), pmap_clear_modify(), and pmap_clear_reference() only
 *  read the PTE1 from the kernel pmap.
 */
static void
pmap_promote_pte1(pmap_t pmap, pt1_entry_t *pte1p, vm_offset_t va)
{
	pt1_entry_t npte1;
	pt2_entry_t *fpte2p, fpte2, fpte2_fav;
	pt2_entry_t *pte2p, pte2;
	vm_offset_t pteva __unused;
	vm_page_t m __unused;

	PDEBUG(6, printf("%s(%p): try for va %#x pte1 %#x at %p\n", __func__,
	    pmap, va, pte1_load(pte1p), pte1p));

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	/*
	 * Examine the first PTE2 in the specified PT2. Abort if this PTE2 is
	 * either invalid, unused, or does not map the first 4KB physical page
	 * within a 1MB page.
	 */
	fpte2p = pmap_pte2_quick(pmap, pte1_trunc(va));
	fpte2 = pte2_load(fpte2p);
	if ((fpte2 & ((PTE2_FRAME & PTE1_OFFSET) | PTE2_A | PTE2_V)) !=
	    (PTE2_A | PTE2_V)) {
		pmap_pte1_p_failures++;
		CTR3(KTR_PMAP, "%s: failure(1) for va %#x in pmap %p",
		    __func__, va, pmap);
		return;
	}
	if (pte2_is_managed(fpte2) && pmap == kernel_pmap) {
		pmap_pte1_p_failures++;
		CTR3(KTR_PMAP, "%s: failure(2) for va %#x in pmap %p",
		    __func__, va, pmap);
		return;
	}
	if ((fpte2 & (PTE2_NM | PTE2_RO)) == PTE2_NM) {
		/*
		 * When page is not modified, PTE2_RO can be set without
		 * a TLB invalidation.
		 */
		fpte2 |= PTE2_RO;
		pte2_store(fpte2p, fpte2);
	}

	/*
	 * Examine each of the other PTE2s in the specified PT2. Abort if this
	 * PTE2 maps an unexpected 4KB physical page or does not have identical
	 * characteristics to the first PTE2.
	 */
	fpte2_fav = (fpte2 & (PTE2_FRAME | PTE2_A | PTE2_V));
	fpte2_fav += PTE1_SIZE - PTE2_SIZE; /* examine from the end */
	for (pte2p = fpte2p + NPTE2_IN_PT2 - 1; pte2p > fpte2p; pte2p--) {
		pte2 = pte2_load(pte2p);
		if ((pte2 & (PTE2_FRAME | PTE2_A | PTE2_V)) != fpte2_fav) {
			pmap_pte1_p_failures++;
			CTR3(KTR_PMAP, "%s: failure(3) for va %#x in pmap %p",
			    __func__, va, pmap);
			return;
		}
		if ((pte2 & (PTE2_NM | PTE2_RO)) == PTE2_NM) {
			/*
			 * When page is not modified, PTE2_RO can be set
			 * without a TLB invalidation. See note above.
			 */
			pte2 |= PTE2_RO;
			pte2_store(pte2p, pte2);
			pteva = pte1_trunc(va) | (pte2 & PTE1_OFFSET &
			    PTE2_FRAME);
			CTR3(KTR_PMAP, "%s: protect for va %#x in pmap %p",
			    __func__, pteva, pmap);
		}
		if ((pte2 & PTE2_PROMOTE) != (fpte2 & PTE2_PROMOTE)) {
			pmap_pte1_p_failures++;
			CTR3(KTR_PMAP, "%s: failure(4) for va %#x in pmap %p",
			    __func__, va, pmap);
			return;
		}

		fpte2_fav -= PTE2_SIZE;
	}
	/*
	 * The page table page in its current state will stay in PT2TAB
	 * until the PTE1 mapping the section is demoted by pmap_demote_pte1()
	 * or destroyed by pmap_remove_pte1().
	 *
	 * Note that L2 page table size is not equal to PAGE_SIZE.
	 */
	m = PHYS_TO_VM_PAGE(trunc_page(pte1_link_pa(pte1_load(pte1p))));
	KASSERT(m >= vm_page_array && m < &vm_page_array[vm_page_array_size],
	    ("%s: PT2 page is out of range", __func__));
	KASSERT(m->pindex == (pte1_index(va) & ~PT2PG_MASK),
	    ("%s: PT2 page's pindex is wrong", __func__));

	/*
	 * Get pte1 from pte2 format.
	 */
	npte1 = (fpte2 & PTE1_FRAME) | ATTR_TO_L1(fpte2) | PTE1_V;

	/*
	 * Promote the pv entries.
	 */
	if (pte2_is_managed(fpte2))
		pmap_pv_promote_pte1(pmap, va, pte1_pa(npte1));

	/*
	 * Promote the mappings.
	 */
	pmap_change_pte1(pmap, pte1p, va, npte1);

	pmap_pte1_promotions++;
	CTR3(KTR_PMAP, "%s: success for va %#x in pmap %p",
	    __func__, va, pmap);

	PDEBUG(6, printf("%s(%p): success for va %#x pte1 %#x(%#x) at %p\n",
	    __func__, pmap, va, npte1, pte1_load(pte1p), pte1p));
}
#endif /* VM_NRESERVLEVEL > 0 */

/*
 *  Zero L2 page table page.
 */
static __inline void
pmap_clear_pt2(pt2_entry_t *fpte2p)
{
	pt2_entry_t *pte2p;

	for (pte2p = fpte2p; pte2p < fpte2p + NPTE2_IN_PT2; pte2p++)
		pte2_clear(pte2p);

}

/*
 *  Removes a 1MB page mapping from the kernel pmap.
 */
static void
pmap_remove_kernel_pte1(pmap_t pmap, pt1_entry_t *pte1p, vm_offset_t va)
{
	vm_page_t m;
	uint32_t pte1_idx;
	pt2_entry_t *fpte2p;
	vm_paddr_t pt2_pa;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	m = pmap_pt2_page(pmap, va);
	if (m == NULL)
		/*
		 * QQQ: Is this function called only on promoted pte1?
		 *      We certainly do section mappings directly
		 *      (without promotion) in kernel !!!
		 */
		panic("%s: missing pt2 page", __func__);

	pte1_idx = pte1_index(va);

	/*
	 * Initialize the L2 page table.
	 */
	fpte2p = page_pt2(pt2map_pt2pg(va), pte1_idx);
	pmap_clear_pt2(fpte2p);

	/*
	 * Remove the mapping.
	 */
	pt2_pa = page_pt2pa(VM_PAGE_TO_PHYS(m), pte1_idx);
	pmap_kenter_pte1(va, PTE1_LINK(pt2_pa));

	/*
	 * QQQ: We do not need to invalidate PT2MAP mapping
	 * as we did not change it. I.e. the L2 page table page
	 * was and still is mapped the same way.
	 */
}

/*
 *  Do the things to unmap a section in a process
 */
static void
pmap_remove_pte1(pmap_t pmap, pt1_entry_t *pte1p, vm_offset_t sva,
    struct spglist *free)
{
	pt1_entry_t opte1;
	struct md_page *pvh;
	vm_offset_t eva, va;
	vm_page_t m;

	PDEBUG(6, printf("%s(%p): va %#x pte1 %#x at %p\n", __func__, pmap, sva,
	    pte1_load(pte1p), pte1p));

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	KASSERT((sva & PTE1_OFFSET) == 0,
	    ("%s: sva is not 1mpage aligned", __func__));

	/*
	 * Clear and invalidate the mapping. It should occupy one and only TLB
	 * entry. So, pmap_tlb_flush() called with aligned address should be
	 * sufficient.
	 */
	opte1 = pte1_load_clear(pte1p);
	pmap_tlb_flush(pmap, sva);

	if (pte1_is_wired(opte1))
		pmap->pm_stats.wired_count -= PTE1_SIZE / PAGE_SIZE;
	pmap->pm_stats.resident_count -= PTE1_SIZE / PAGE_SIZE;
	if (pte1_is_managed(opte1)) {
		pvh = pa_to_pvh(pte1_pa(opte1));
		pmap_pvh_free(pvh, pmap, sva);
		eva = sva + PTE1_SIZE;
		for (va = sva, m = PHYS_TO_VM_PAGE(pte1_pa(opte1));
		    va < eva; va += PAGE_SIZE, m++) {
			if (pte1_is_dirty(opte1))
				vm_page_dirty(m);
			if (opte1 & PTE1_A)
				vm_page_aflag_set(m, PGA_REFERENCED);
			if (TAILQ_EMPTY(&m->md.pv_list) &&
			    TAILQ_EMPTY(&pvh->pv_list))
				vm_page_aflag_clear(m, PGA_WRITEABLE);
		}
	}
	if (pmap == kernel_pmap) {
		/*
		 * L2 page table(s) can't be removed from kernel map as
		 * kernel counts on it (stuff around pmap_growkernel()).
		 */
		 pmap_remove_kernel_pte1(pmap, pte1p, sva);
	} else {
		/*
		 * Get associated L2 page table page.
		 * It's possible that the page was never allocated.
		 */
		m = pmap_pt2_page(pmap, sva);
		if (m != NULL)
			pmap_unwire_pt2_all(pmap, sva, m, free);
	}
}

/*
 *  Fills L2 page table page with mappings to consecutive physical pages.
 */
static __inline void
pmap_fill_pt2(pt2_entry_t *fpte2p, pt2_entry_t npte2)
{
	pt2_entry_t *pte2p;

	for (pte2p = fpte2p; pte2p < fpte2p + NPTE2_IN_PT2; pte2p++) {
		pte2_store(pte2p, npte2);
		npte2 += PTE2_SIZE;
	}
}

/*
 *  Tries to demote a 1MB page mapping. If demotion fails, the
 *  1MB page mapping is invalidated.
 */
static boolean_t
pmap_demote_pte1(pmap_t pmap, pt1_entry_t *pte1p, vm_offset_t va)
{
	pt1_entry_t opte1, npte1;
	pt2_entry_t *fpte2p, npte2;
	vm_paddr_t pt2pg_pa, pt2_pa;
	vm_page_t m;
	struct spglist free;
	uint32_t pte1_idx, isnew = 0;

	PDEBUG(6, printf("%s(%p): try for va %#x pte1 %#x at %p\n", __func__,
	    pmap, va, pte1_load(pte1p), pte1p));

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	opte1 = pte1_load(pte1p);
	KASSERT(pte1_is_section(opte1), ("%s: opte1 not a section", __func__));

	if ((opte1 & PTE1_A) == 0 || (m = pmap_pt2_page(pmap, va)) == NULL) {
		KASSERT(!pte1_is_wired(opte1),
		    ("%s: PT2 page for a wired mapping is missing", __func__));

		/*
		 * Invalidate the 1MB page mapping and return
		 * "failure" if the mapping was never accessed or the
		 * allocation of the new page table page fails.
		 */
		if ((opte1 & PTE1_A) == 0 || (m = vm_page_alloc(NULL,
		    pte1_index(va) & ~PT2PG_MASK, VM_ALLOC_NOOBJ |
		    VM_ALLOC_NORMAL | VM_ALLOC_WIRED)) == NULL) {
			SLIST_INIT(&free);
			pmap_remove_pte1(pmap, pte1p, pte1_trunc(va), &free);
			vm_page_free_pages_toq(&free, false);
			CTR3(KTR_PMAP, "%s: failure for va %#x in pmap %p",
			    __func__, va, pmap);
			return (FALSE);
		}
		if (va < VM_MAXUSER_ADDRESS)
			pmap->pm_stats.resident_count++;

		isnew = 1;

		/*
		 * We init all L2 page tables in the page even if
		 * we are going to change everything for one L2 page
		 * table in a while.
		 */
		pt2pg_pa = pmap_pt2pg_init(pmap, va, m);
	} else {
		if (va < VM_MAXUSER_ADDRESS) {
			if (pt2_is_empty(m, va))
				isnew = 1; /* Demoting section w/o promotion. */
#ifdef INVARIANTS
			else
				KASSERT(pt2_is_full(m, va), ("%s: bad PT2 wire"
				    " count %u", __func__,
				    pt2_wirecount_get(m, pte1_index(va))));
#endif
		}
	}

	pt2pg_pa = VM_PAGE_TO_PHYS(m);
	pte1_idx = pte1_index(va);
	/*
	 * If the pmap is current, then the PT2MAP can provide access to
	 * the page table page (promoted L2 page tables are not unmapped).
	 * Otherwise, temporarily map the L2 page table page (m) into
	 * the kernel's address space at either PADDR1 or PADDR2.
	 *
	 * Note that L2 page table size is not equal to PAGE_SIZE.
	 */
	if (pmap_is_current(pmap))
		fpte2p = page_pt2(pt2map_pt2pg(va), pte1_idx);
	else if (curthread->td_pinned > 0 && rw_wowned(&pvh_global_lock)) {
		if (pte2_pa(pte2_load(PMAP1)) != pt2pg_pa) {
			pte2_store(PMAP1, PTE2_KPT(pt2pg_pa));
#ifdef SMP
			PMAP1cpu = PCPU_GET(cpuid);
#endif
			tlb_flush_local((vm_offset_t)PADDR1);
			PMAP1changed++;
		} else
#ifdef SMP
		if (PMAP1cpu != PCPU_GET(cpuid)) {
			PMAP1cpu = PCPU_GET(cpuid);
			tlb_flush_local((vm_offset_t)PADDR1);
			PMAP1changedcpu++;
		} else
#endif
			PMAP1unchanged++;
		fpte2p = page_pt2((vm_offset_t)PADDR1, pte1_idx);
	} else {
		mtx_lock(&PMAP2mutex);
		if (pte2_pa(pte2_load(PMAP2)) != pt2pg_pa) {
			pte2_store(PMAP2, PTE2_KPT(pt2pg_pa));
			tlb_flush((vm_offset_t)PADDR2);
		}
		fpte2p = page_pt2((vm_offset_t)PADDR2, pte1_idx);
	}
	pt2_pa = page_pt2pa(pt2pg_pa, pte1_idx);
	npte1 = PTE1_LINK(pt2_pa);

	KASSERT((opte1 & PTE1_A) != 0,
	    ("%s: opte1 is missing PTE1_A", __func__));
	KASSERT((opte1 & (PTE1_NM | PTE1_RO)) != PTE1_NM,
	    ("%s: opte1 has PTE1_NM", __func__));

	/*
	 *  Get pte2 from pte1 format.
	*/
	npte2 = pte1_pa(opte1) | ATTR_TO_L2(opte1) | PTE2_V;

	/*
	 * If the L2 page table page is new, initialize it. If the mapping
	 * has changed attributes, update the page table entries.
	 */
	if (isnew != 0) {
		pt2_wirecount_set(m, pte1_idx, NPTE2_IN_PT2);
		pmap_fill_pt2(fpte2p, npte2);
	} else if ((pte2_load(fpte2p) & PTE2_PROMOTE) !=
		    (npte2 & PTE2_PROMOTE))
		pmap_fill_pt2(fpte2p, npte2);

	KASSERT(pte2_pa(pte2_load(fpte2p)) == pte2_pa(npte2),
	    ("%s: fpte2p and npte2 map different physical addresses",
	    __func__));

	if (fpte2p == PADDR2)
		mtx_unlock(&PMAP2mutex);

	/*
	 * Demote the mapping. This pmap is locked. The old PTE1 has
	 * PTE1_A set. If the old PTE1 has not PTE1_RO set, it also
	 * has not PTE1_NM set. Thus, there is no danger of a race with
	 * another processor changing the setting of PTE1_A and/or PTE1_NM
	 * between the read above and the store below.
	 */
	pmap_change_pte1(pmap, pte1p, va, npte1);

	/*
	 * Demote the pv entry. This depends on the earlier demotion
	 * of the mapping. Specifically, the (re)creation of a per-
	 * page pv entry might trigger the execution of pmap_pv_reclaim(),
	 * which might reclaim a newly (re)created per-page pv entry
	 * and destroy the associated mapping. In order to destroy
	 * the mapping, the PTE1 must have already changed from mapping
	 * the 1mpage to referencing the page table page.
	 */
	if (pte1_is_managed(opte1))
		pmap_pv_demote_pte1(pmap, va, pte1_pa(opte1));

	pmap_pte1_demotions++;
	CTR3(KTR_PMAP, "%s: success for va %#x in pmap %p",
	    __func__, va, pmap);

	PDEBUG(6, printf("%s(%p): success for va %#x pte1 %#x(%#x) at %p\n",
	    __func__, pmap, va, npte1, pte1_load(pte1p), pte1p));
	return (TRUE);
}

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
int
pmap_enter(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot,
    u_int flags, int8_t psind)
{
	pt1_entry_t *pte1p;
	pt2_entry_t *pte2p;
	pt2_entry_t npte2, opte2;
	pv_entry_t pv;
	vm_paddr_t opa, pa;
	vm_page_t mpte2, om;
	int rv;

	va = trunc_page(va);
	KASSERT(va <= vm_max_kernel_address, ("%s: toobig", __func__));
	KASSERT(va < UPT2V_MIN_ADDRESS || va >= UPT2V_MAX_ADDRESS,
	    ("%s: invalid to pmap_enter page table pages (va: 0x%x)", __func__,
	    va));
	KASSERT((m->oflags & VPO_UNMANAGED) != 0 || va < kmi.clean_sva ||
	    va >= kmi.clean_eva,
	    ("%s: managed mapping within the clean submap", __func__));
	if ((m->oflags & VPO_UNMANAGED) == 0 && !vm_page_xbusied(m))
		VM_OBJECT_ASSERT_LOCKED(m->object);
	KASSERT((flags & PMAP_ENTER_RESERVED) == 0,
	    ("%s: flags %u has reserved bits set", __func__, flags));
	pa = VM_PAGE_TO_PHYS(m);
	npte2 = PTE2(pa, PTE2_A, vm_page_pte2_attr(m));
	if ((flags & VM_PROT_WRITE) == 0)
		npte2 |= PTE2_NM;
	if ((prot & VM_PROT_WRITE) == 0)
		npte2 |= PTE2_RO;
	KASSERT((npte2 & (PTE2_NM | PTE2_RO)) != PTE2_RO,
	    ("%s: flags includes VM_PROT_WRITE but prot doesn't", __func__));
	if ((prot & VM_PROT_EXECUTE) == 0)
		npte2 |= PTE2_NX;
	if ((flags & PMAP_ENTER_WIRED) != 0)
		npte2 |= PTE2_W;
	if (va < VM_MAXUSER_ADDRESS)
		npte2 |= PTE2_U;
	if (pmap != kernel_pmap)
		npte2 |= PTE2_NG;

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	sched_pin();
	if (psind == 1) {
		/* Assert the required virtual and physical alignment. */
		KASSERT((va & PTE1_OFFSET) == 0,
		    ("%s: va unaligned", __func__));
		KASSERT(m->psind > 0, ("%s: m->psind < psind", __func__));
		rv = pmap_enter_pte1(pmap, va, PTE1_PA(pa) | ATTR_TO_L1(npte2) |
		    PTE1_V, flags, m);
		goto out;
	}

	/*
	 * In the case that a page table page is not
	 * resident, we are creating it here.
	 */
	if (va < VM_MAXUSER_ADDRESS) {
		mpte2 = pmap_allocpte2(pmap, va, flags);
		if (mpte2 == NULL) {
			KASSERT((flags & PMAP_ENTER_NOSLEEP) != 0,
			    ("pmap_allocpte2 failed with sleep allowed"));
			rv = KERN_RESOURCE_SHORTAGE;
			goto out;
		}
	} else
		mpte2 = NULL;
	pte1p = pmap_pte1(pmap, va);
	if (pte1_is_section(pte1_load(pte1p)))
		panic("%s: attempted on 1MB page", __func__);
	pte2p = pmap_pte2_quick(pmap, va);
	if (pte2p == NULL)
		panic("%s: invalid L1 page table entry va=%#x", __func__, va);

	om = NULL;
	opte2 = pte2_load(pte2p);
	opa = pte2_pa(opte2);
	/*
	 * Mapping has not changed, must be protection or wiring change.
	 */
	if (pte2_is_valid(opte2) && (opa == pa)) {
		/*
		 * Wiring change, just update stats. We don't worry about
		 * wiring PT2 pages as they remain resident as long as there
		 * are valid mappings in them. Hence, if a user page is wired,
		 * the PT2 page will be also.
		 */
		if (pte2_is_wired(npte2) && !pte2_is_wired(opte2))
			pmap->pm_stats.wired_count++;
		else if (!pte2_is_wired(npte2) && pte2_is_wired(opte2))
			pmap->pm_stats.wired_count--;

		/*
		 * Remove extra pte2 reference
		 */
		if (mpte2)
			pt2_wirecount_dec(mpte2, pte1_index(va));
		if ((m->oflags & VPO_UNMANAGED) == 0)
			om = m;
		goto validate;
	}

	/*
	 * QQQ: We think that changing physical address on writeable mapping
	 *      is not safe. Well, maybe on kernel address space with correct
	 *      locking, it can make a sense. However, we have no idea why
	 *      anyone should do that on user address space. Are we wrong?
	 */
	KASSERT((opa == 0) || (opa == pa) ||
	    !pte2_is_valid(opte2) || ((opte2 & PTE2_RO) != 0),
	    ("%s: pmap %p va %#x(%#x) opa %#x pa %#x - gotcha %#x %#x!",
	    __func__, pmap, va, opte2, opa, pa, flags, prot));

	pv = NULL;

	/*
	 * Mapping has changed, invalidate old range and fall through to
	 * handle validating new mapping.
	 */
	if (opa) {
		if (pte2_is_wired(opte2))
			pmap->pm_stats.wired_count--;
		om = PHYS_TO_VM_PAGE(opa);
		if (om != NULL && (om->oflags & VPO_UNMANAGED) != 0)
			om = NULL;
		if (om != NULL)
			pv = pmap_pvh_remove(&om->md, pmap, va);

		/*
		 * Remove extra pte2 reference
		 */
		if (mpte2 != NULL)
			pt2_wirecount_dec(mpte2, va >> PTE1_SHIFT);
	} else
		pmap->pm_stats.resident_count++;

	/*
	 * Enter on the PV list if part of our managed memory.
	 */
	if ((m->oflags & VPO_UNMANAGED) == 0) {
		if (pv == NULL) {
			pv = get_pv_entry(pmap, FALSE);
			pv->pv_va = va;
		}
		TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_next);
	} else if (pv != NULL)
		free_pv_entry(pmap, pv);

	/*
	 * Increment counters
	 */
	if (pte2_is_wired(npte2))
		pmap->pm_stats.wired_count++;

validate:
	/*
	 * Now validate mapping with desired protection/wiring.
	 */
	if (prot & VM_PROT_WRITE) {
		if ((m->oflags & VPO_UNMANAGED) == 0)
			vm_page_aflag_set(m, PGA_WRITEABLE);
	}

	/*
	 * If the mapping or permission bits are different, we need
	 * to update the pte2.
	 *
	 * QQQ: Think again and again what to do
	 *      if the mapping is going to be changed!
	 */
	if ((opte2 & ~(PTE2_NM | PTE2_A)) != (npte2 & ~(PTE2_NM | PTE2_A))) {
		/*
		 * Sync icache if exec permission and attribute VM_MEMATTR_WB_WA
		 * is set. Do it now, before the mapping is stored and made
		 * valid for hardware table walk. If done later, there is a race
		 * for other threads of current process in lazy loading case.
		 * Don't do it for kernel memory which is mapped with exec
		 * permission even if the memory isn't going to hold executable
		 * code. The only time when icache sync is needed is after
		 * kernel module is loaded and the relocation info is processed.
		 * And it's done in elf_cpu_load_file().
		 *
		 * QQQ: (1) Does it exist any better way where
		 *          or how to sync icache?
		 *      (2) Now, we do it on a page basis.
		 */
		if ((prot & VM_PROT_EXECUTE) && pmap != kernel_pmap &&
		    m->md.pat_mode == VM_MEMATTR_WB_WA &&
		    (opa != pa || (opte2 & PTE2_NX)))
			cache_icache_sync_fresh(va, pa, PAGE_SIZE);

		if (opte2 & PTE2_V) {
			/* Change mapping with break-before-make approach. */
			opte2 = pte2_load_clear(pte2p);
			pmap_tlb_flush(pmap, va);
			pte2_store(pte2p, npte2);
			if (om != NULL) {
				KASSERT((om->oflags & VPO_UNMANAGED) == 0,
				    ("%s: om %p unmanaged", __func__, om));
				if ((opte2 & PTE2_A) != 0)
					vm_page_aflag_set(om, PGA_REFERENCED);
				if (pte2_is_dirty(opte2))
					vm_page_dirty(om);
				if (TAILQ_EMPTY(&om->md.pv_list) &&
				    ((om->flags & PG_FICTITIOUS) != 0 ||
				    TAILQ_EMPTY(&pa_to_pvh(opa)->pv_list)))
					vm_page_aflag_clear(om, PGA_WRITEABLE);
			}
		} else
			pte2_store(pte2p, npte2);
	}
#if 0
	else {
		/*
		 * QQQ: In time when both access and not mofified bits are
		 *      emulated by software, this should not happen. Some
		 *      analysis is need, if this really happen. Missing
		 *      tlb flush somewhere could be the reason.
		 */
		panic("%s: pmap %p va %#x opte2 %x npte2 %x !!", __func__, pmap,
		    va, opte2, npte2);
	}
#endif

#if VM_NRESERVLEVEL > 0
	/*
	 * If both the L2 page table page and the reservation are fully
	 * populated, then attempt promotion.
	 */
	if ((mpte2 == NULL || pt2_is_full(mpte2, va)) &&
	    sp_enabled && (m->flags & PG_FICTITIOUS) == 0 &&
	    vm_reserv_level_iffullpop(m) == 0)
		pmap_promote_pte1(pmap, pte1p, va);
#endif

	rv = KERN_SUCCESS;
out:
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
	return (rv);
}

/*
 *  Do the things to unmap a page in a process.
 */
static int
pmap_remove_pte2(pmap_t pmap, pt2_entry_t *pte2p, vm_offset_t va,
    struct spglist *free)
{
	pt2_entry_t opte2;
	vm_page_t m;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	/* Clear and invalidate the mapping. */
	opte2 = pte2_load_clear(pte2p);
	pmap_tlb_flush(pmap, va);

	KASSERT(pte2_is_valid(opte2), ("%s: pmap %p va %#x not link pte2 %#x",
	    __func__, pmap, va, opte2));

	if (opte2 & PTE2_W)
		pmap->pm_stats.wired_count -= 1;
	pmap->pm_stats.resident_count -= 1;
	if (pte2_is_managed(opte2)) {
		m = PHYS_TO_VM_PAGE(pte2_pa(opte2));
		if (pte2_is_dirty(opte2))
			vm_page_dirty(m);
		if (opte2 & PTE2_A)
			vm_page_aflag_set(m, PGA_REFERENCED);
		pmap_remove_entry(pmap, m, va);
	}
	return (pmap_unuse_pt2(pmap, va, free));
}

/*
 *  Remove a single page from a process address space.
 */
static void
pmap_remove_page(pmap_t pmap, vm_offset_t va, struct spglist *free)
{
	pt2_entry_t *pte2p;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	KASSERT(curthread->td_pinned > 0,
	    ("%s: curthread not pinned", __func__));
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	if ((pte2p = pmap_pte2_quick(pmap, va)) == NULL ||
	    !pte2_is_valid(pte2_load(pte2p)))
		return;
	pmap_remove_pte2(pmap, pte2p, va, free);
}

/*
 *  Remove the given range of addresses from the specified map.
 *
 *  It is assumed that the start and end are properly
 *  rounded to the page size.
 */
void
pmap_remove(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	vm_offset_t nextva;
	pt1_entry_t *pte1p, pte1;
	pt2_entry_t *pte2p, pte2;
	struct spglist free;

	/*
	 * Perform an unsynchronized read. This is, however, safe.
	 */
	if (pmap->pm_stats.resident_count == 0)
		return;

	SLIST_INIT(&free);

	rw_wlock(&pvh_global_lock);
	sched_pin();
	PMAP_LOCK(pmap);

	/*
	 * Special handling of removing one page. A very common
	 * operation and easy to short circuit some code.
	 */
	if (sva + PAGE_SIZE == eva) {
		pte1 = pte1_load(pmap_pte1(pmap, sva));
		if (pte1_is_link(pte1)) {
			pmap_remove_page(pmap, sva, &free);
			goto out;
		}
	}

	for (; sva < eva; sva = nextva) {
		/*
		 * Calculate address for next L2 page table.
		 */
		nextva = pte1_trunc(sva + PTE1_SIZE);
		if (nextva < sva)
			nextva = eva;
		if (pmap->pm_stats.resident_count == 0)
			break;

		pte1p = pmap_pte1(pmap, sva);
		pte1 = pte1_load(pte1p);

		/*
		 * Weed out invalid mappings. Note: we assume that the L1 page
		 * table is always allocated, and in kernel virtual.
		 */
		if (pte1 == 0)
			continue;

		if (pte1_is_section(pte1)) {
			/*
			 * Are we removing the entire large page?  If not,
			 * demote the mapping and fall through.
			 */
			if (sva + PTE1_SIZE == nextva && eva >= nextva) {
				pmap_remove_pte1(pmap, pte1p, sva, &free);
				continue;
			} else if (!pmap_demote_pte1(pmap, pte1p, sva)) {
				/* The large page mapping was destroyed. */
				continue;
			}
#ifdef INVARIANTS
			else {
				/* Update pte1 after demotion. */
				pte1 = pte1_load(pte1p);
			}
#endif
		}

		KASSERT(pte1_is_link(pte1), ("%s: pmap %p va %#x pte1 %#x at %p"
		    " is not link", __func__, pmap, sva, pte1, pte1p));

		/*
		 * Limit our scan to either the end of the va represented
		 * by the current L2 page table page, or to the end of the
		 * range being removed.
		 */
		if (nextva > eva)
			nextva = eva;

		for (pte2p = pmap_pte2_quick(pmap, sva); sva != nextva;
		    pte2p++, sva += PAGE_SIZE) {
			pte2 = pte2_load(pte2p);
			if (!pte2_is_valid(pte2))
				continue;
			if (pmap_remove_pte2(pmap, pte2p, sva, &free))
				break;
		}
	}
out:
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
	vm_page_free_pages_toq(&free, false);
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

void
pmap_remove_all(vm_page_t m)
{
	struct md_page *pvh;
	pv_entry_t pv;
	pmap_t pmap;
	pt2_entry_t *pte2p, opte2;
	pt1_entry_t *pte1p;
	vm_offset_t va;
	struct spglist free;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("%s: page %p is not managed", __func__, m));
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
		pte1p = pmap_pte1(pmap, va);
		(void)pmap_demote_pte1(pmap, pte1p, va);
		PMAP_UNLOCK(pmap);
	}
small_mappings:
	while ((pv = TAILQ_FIRST(&m->md.pv_list)) != NULL) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pmap->pm_stats.resident_count--;
		pte1p = pmap_pte1(pmap, pv->pv_va);
		KASSERT(!pte1_is_section(pte1_load(pte1p)), ("%s: found "
		    "a 1mpage in page %p's pv list", __func__, m));
		pte2p = pmap_pte2_quick(pmap, pv->pv_va);
		opte2 = pte2_load_clear(pte2p);
		pmap_tlb_flush(pmap, pv->pv_va);
		KASSERT(pte2_is_valid(opte2), ("%s: pmap %p va %x zero pte2",
		    __func__, pmap, pv->pv_va));
		if (pte2_is_wired(opte2))
			pmap->pm_stats.wired_count--;
		if (opte2 & PTE2_A)
			vm_page_aflag_set(m, PGA_REFERENCED);

		/*
		 * Update the vm_page_t clean and reference bits.
		 */
		if (pte2_is_dirty(opte2))
			vm_page_dirty(m);
		pmap_unuse_pt2(pmap, pv->pv_va, &free);
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_next);
		free_pv_entry(pmap, pv);
		PMAP_UNLOCK(pmap);
	}
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
	vm_page_free_pages_toq(&free, false);
}

/*
 *  Just subroutine for pmap_remove_pages() to reasonably satisfy
 *  good coding style, a.k.a. 80 character line width limit hell.
 */
static __inline void
pmap_remove_pte1_quick(pmap_t pmap, pt1_entry_t pte1, pv_entry_t pv,
    struct spglist *free)
{
	vm_paddr_t pa;
	vm_page_t m, mt, mpt2pg;
	struct md_page *pvh;

	pa = pte1_pa(pte1);
	m = PHYS_TO_VM_PAGE(pa);

	KASSERT(m->phys_addr == pa, ("%s: vm_page_t %p addr mismatch %#x %#x",
	    __func__, m, m->phys_addr, pa));
	KASSERT((m->flags & PG_FICTITIOUS) != 0 ||
	    m < &vm_page_array[vm_page_array_size],
	    ("%s: bad pte1 %#x", __func__, pte1));

	if (pte1_is_dirty(pte1)) {
		for (mt = m; mt < &m[PTE1_SIZE / PAGE_SIZE]; mt++)
			vm_page_dirty(mt);
	}

	pmap->pm_stats.resident_count -= PTE1_SIZE / PAGE_SIZE;
	pvh = pa_to_pvh(pa);
	TAILQ_REMOVE(&pvh->pv_list, pv, pv_next);
	if (TAILQ_EMPTY(&pvh->pv_list)) {
		for (mt = m; mt < &m[PTE1_SIZE / PAGE_SIZE]; mt++)
			if (TAILQ_EMPTY(&mt->md.pv_list))
				vm_page_aflag_clear(mt, PGA_WRITEABLE);
	}
	mpt2pg = pmap_pt2_page(pmap, pv->pv_va);
	if (mpt2pg != NULL)
		pmap_unwire_pt2_all(pmap, pv->pv_va, mpt2pg, free);
}

/*
 *  Just subroutine for pmap_remove_pages() to reasonably satisfy
 *  good coding style, a.k.a. 80 character line width limit hell.
 */
static __inline void
pmap_remove_pte2_quick(pmap_t pmap, pt2_entry_t pte2, pv_entry_t pv,
    struct spglist *free)
{
	vm_paddr_t pa;
	vm_page_t m;
	struct md_page *pvh;

	pa = pte2_pa(pte2);
	m = PHYS_TO_VM_PAGE(pa);

	KASSERT(m->phys_addr == pa, ("%s: vm_page_t %p addr mismatch %#x %#x",
	    __func__, m, m->phys_addr, pa));
	KASSERT((m->flags & PG_FICTITIOUS) != 0 ||
	    m < &vm_page_array[vm_page_array_size],
	    ("%s: bad pte2 %#x", __func__, pte2));

	if (pte2_is_dirty(pte2))
		vm_page_dirty(m);

	pmap->pm_stats.resident_count--;
	TAILQ_REMOVE(&m->md.pv_list, pv, pv_next);
	if (TAILQ_EMPTY(&m->md.pv_list) && (m->flags & PG_FICTITIOUS) == 0) {
		pvh = pa_to_pvh(pa);
		if (TAILQ_EMPTY(&pvh->pv_list))
			vm_page_aflag_clear(m, PGA_WRITEABLE);
	}
	pmap_unuse_pt2(pmap, pv->pv_va, free);
}

/*
 *  Remove all pages from specified address space this aids process
 *  exit speeds. Also, this code is special cased for current process
 *  only, but can have the more generic (and slightly slower) mode enabled.
 *  This is much faster than pmap_remove in the case of running down
 *  an entire address space.
 */
void
pmap_remove_pages(pmap_t pmap)
{
	pt1_entry_t *pte1p, pte1;
	pt2_entry_t *pte2p, pte2;
	pv_entry_t pv;
	struct pv_chunk *pc, *npc;
	struct spglist free;
	int field, idx;
	int32_t bit;
	uint32_t inuse, bitmask;
	boolean_t allfree;

	/*
	 * Assert that the given pmap is only active on the current
	 * CPU.  Unfortunately, we cannot block another CPU from
	 * activating the pmap while this function is executing.
	 */
	KASSERT(pmap == vmspace_pmap(curthread->td_proc->p_vmspace),
	    ("%s: non-current pmap %p", __func__, pmap));
#if defined(SMP) && defined(INVARIANTS)
	{
		cpuset_t other_cpus;

		sched_pin();
		other_cpus = pmap->pm_active;
		CPU_CLR(PCPU_GET(cpuid), &other_cpus);
		sched_unpin();
		KASSERT(CPU_EMPTY(&other_cpus),
		    ("%s: pmap %p active on other cpus", __func__, pmap));
	}
#endif
	SLIST_INIT(&free);
	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	sched_pin();
	TAILQ_FOREACH_SAFE(pc, &pmap->pm_pvchunk, pc_list, npc) {
		KASSERT(pc->pc_pmap == pmap, ("%s: wrong pmap %p %p",
		    __func__, pmap, pc->pc_pmap));
		allfree = TRUE;
		for (field = 0; field < _NPCM; field++) {
			inuse = (~(pc->pc_map[field])) & pc_freemask[field];
			while (inuse != 0) {
				bit = ffs(inuse) - 1;
				bitmask = 1UL << bit;
				idx = field * 32 + bit;
				pv = &pc->pc_pventry[idx];
				inuse &= ~bitmask;

				/*
				 * Note that we cannot remove wired pages
				 * from a process' mapping at this time
				 */
				pte1p = pmap_pte1(pmap, pv->pv_va);
				pte1 = pte1_load(pte1p);
				if (pte1_is_section(pte1)) {
					if (pte1_is_wired(pte1))  {
						allfree = FALSE;
						continue;
					}
					pte1_clear(pte1p);
					pmap_remove_pte1_quick(pmap, pte1, pv,
					    &free);
				}
				else if (pte1_is_link(pte1)) {
					pte2p = pt2map_entry(pv->pv_va);
					pte2 = pte2_load(pte2p);

					if (!pte2_is_valid(pte2)) {
						printf("%s: pmap %p va %#x "
						    "pte2 %#x\n", __func__,
						    pmap, pv->pv_va, pte2);
						panic("bad pte2");
					}

					if (pte2_is_wired(pte2))   {
						allfree = FALSE;
						continue;
					}
					pte2_clear(pte2p);
					pmap_remove_pte2_quick(pmap, pte2, pv,
					    &free);
				} else {
					printf("%s: pmap %p va %#x pte1 %#x\n",
					    __func__, pmap, pv->pv_va, pte1);
					panic("bad pte1");
				}

				/* Mark free */
				PV_STAT(pv_entry_frees++);
				PV_STAT(pv_entry_spare++);
				pv_entry_count--;
				pc->pc_map[field] |= bitmask;
			}
		}
		if (allfree) {
			TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
			free_pv_chunk(pc);
		}
	}
	tlb_flush_all_ng_local();
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
	vm_page_free_pages_toq(&free, false);
}

/*
 *  This code makes some *MAJOR* assumptions:
 *  1. Current pmap & pmap exists.
 *  2. Not wired.
 *  3. Read access.
 *  4. No L2 page table pages.
 *  but is *MUCH* faster than pmap_enter...
 */
static vm_page_t
pmap_enter_quick_locked(pmap_t pmap, vm_offset_t va, vm_page_t m,
    vm_prot_t prot, vm_page_t mpt2pg)
{
	pt2_entry_t *pte2p, pte2;
	vm_paddr_t pa;
	struct spglist free;
	uint32_t l2prot;

	KASSERT(va < kmi.clean_sva || va >= kmi.clean_eva ||
	    (m->oflags & VPO_UNMANAGED) != 0,
	    ("%s: managed mapping within the clean submap", __func__));
	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	/*
	 * In the case that a L2 page table page is not
	 * resident, we are creating it here.
	 */
	if (va < VM_MAXUSER_ADDRESS) {
		u_int pte1_idx;
		pt1_entry_t pte1, *pte1p;
		vm_paddr_t pt2_pa;

		/*
		 * Get L1 page table things.
		 */
		pte1_idx = pte1_index(va);
		pte1p = pmap_pte1(pmap, va);
		pte1 = pte1_load(pte1p);

		if (mpt2pg && (mpt2pg->pindex == (pte1_idx & ~PT2PG_MASK))) {
			/*
			 * Each of NPT2_IN_PG L2 page tables on the page can
			 * come here. Make sure that associated L1 page table
			 * link is established.
			 *
			 * QQQ: It comes that we don't establish all links to
			 *      L2 page tables for newly allocated L2 page
			 *      tables page.
			 */
			KASSERT(!pte1_is_section(pte1),
			    ("%s: pte1 %#x is section", __func__, pte1));
			if (!pte1_is_link(pte1)) {
				pt2_pa = page_pt2pa(VM_PAGE_TO_PHYS(mpt2pg),
				    pte1_idx);
				pte1_store(pte1p, PTE1_LINK(pt2_pa));
			}
			pt2_wirecount_inc(mpt2pg, pte1_idx);
		} else {
			/*
			 * If the L2 page table page is mapped, we just
			 * increment the hold count, and activate it.
			 */
			if (pte1_is_section(pte1)) {
				return (NULL);
			} else if (pte1_is_link(pte1)) {
				mpt2pg = PHYS_TO_VM_PAGE(pte1_link_pa(pte1));
				pt2_wirecount_inc(mpt2pg, pte1_idx);
			} else {
				mpt2pg = _pmap_allocpte2(pmap, va,
				    PMAP_ENTER_NOSLEEP);
				if (mpt2pg == NULL)
					return (NULL);
			}
		}
	} else {
		mpt2pg = NULL;
	}

	/*
	 * This call to pt2map_entry() makes the assumption that we are
	 * entering the page into the current pmap.  In order to support
	 * quick entry into any pmap, one would likely use pmap_pte2_quick().
	 * But that isn't as quick as pt2map_entry().
	 */
	pte2p = pt2map_entry(va);
	pte2 = pte2_load(pte2p);
	if (pte2_is_valid(pte2)) {
		if (mpt2pg != NULL) {
			/*
			 * Remove extra pte2 reference
			 */
			pt2_wirecount_dec(mpt2pg, pte1_index(va));
			mpt2pg = NULL;
		}
		return (NULL);
	}

	/*
	 * Enter on the PV list if part of our managed memory.
	 */
	if ((m->oflags & VPO_UNMANAGED) == 0 &&
	    !pmap_try_insert_pv_entry(pmap, va, m)) {
		if (mpt2pg != NULL) {
			SLIST_INIT(&free);
			if (pmap_unwire_pt2(pmap, va, mpt2pg, &free)) {
				pmap_tlb_flush(pmap, va);
				vm_page_free_pages_toq(&free, false);
			}

			mpt2pg = NULL;
		}
		return (NULL);
	}

	/*
	 * Increment counters
	 */
	pmap->pm_stats.resident_count++;

	/*
	 * Now validate mapping with RO protection
	 */
	pa = VM_PAGE_TO_PHYS(m);
	l2prot = PTE2_RO | PTE2_NM;
	if (va < VM_MAXUSER_ADDRESS)
		l2prot |= PTE2_U | PTE2_NG;
	if ((prot & VM_PROT_EXECUTE) == 0)
		l2prot |= PTE2_NX;
	else if (m->md.pat_mode == VM_MEMATTR_WB_WA && pmap != kernel_pmap) {
		/*
		 * Sync icache if exec permission and attribute VM_MEMATTR_WB_WA
		 * is set. QQQ: For more info, see comments in pmap_enter().
		 */
		cache_icache_sync_fresh(va, pa, PAGE_SIZE);
	}
	pte2_store(pte2p, PTE2(pa, l2prot, vm_page_pte2_attr(m)));

	return (mpt2pg);
}

void
pmap_enter_quick(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot)
{

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	(void)pmap_enter_quick_locked(pmap, va, m, prot, NULL);
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
}

/*
 *  Tries to create a read- and/or execute-only 1 MB page mapping.  Returns
 *  true if successful.  Returns false if (1) a mapping already exists at the
 *  specified virtual address or (2) a PV entry cannot be allocated without
 *  reclaiming another PV entry.
 */
static bool
pmap_enter_1mpage(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot)
{
	pt1_entry_t pte1;
	vm_paddr_t pa;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	pa = VM_PAGE_TO_PHYS(m);
	pte1 = PTE1(pa, PTE1_NM | PTE1_RO, ATTR_TO_L1(vm_page_pte2_attr(m)));
	if ((prot & VM_PROT_EXECUTE) == 0)
		pte1 |= PTE1_NX;
	if (va < VM_MAXUSER_ADDRESS)
		pte1 |= PTE1_U;
	if (pmap != kernel_pmap)
		pte1 |= PTE1_NG;
	return (pmap_enter_pte1(pmap, va, pte1, PMAP_ENTER_NOSLEEP |
	    PMAP_ENTER_NOREPLACE | PMAP_ENTER_NORECLAIM, m) == KERN_SUCCESS);
}

/*
 *  Tries to create the specified 1 MB page mapping.  Returns KERN_SUCCESS if
 *  the mapping was created, and either KERN_FAILURE or KERN_RESOURCE_SHORTAGE
 *  otherwise.  Returns KERN_FAILURE if PMAP_ENTER_NOREPLACE was specified and
 *  a mapping already exists at the specified virtual address.  Returns
 *  KERN_RESOURCE_SHORTAGE if PMAP_ENTER_NORECLAIM was specified and PV entry
 *  allocation failed.
 */
static int
pmap_enter_pte1(pmap_t pmap, vm_offset_t va, pt1_entry_t pte1, u_int flags,
    vm_page_t m)
{
	struct spglist free;
	pt1_entry_t opte1, *pte1p;
	pt2_entry_t pte2, *pte2p;
	vm_offset_t cur, end;
	vm_page_t mt;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	KASSERT((pte1 & (PTE1_NM | PTE1_RO)) == 0 ||
	    (pte1 & (PTE1_NM | PTE1_RO)) == (PTE1_NM | PTE1_RO),
	    ("%s: pte1 has inconsistent NM and RO attributes", __func__));
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	pte1p = pmap_pte1(pmap, va);
	opte1 = pte1_load(pte1p);
	if (pte1_is_valid(opte1)) {
		if ((flags & PMAP_ENTER_NOREPLACE) != 0) {
			CTR3(KTR_PMAP, "%s: failure for va %#lx in pmap %p",
			    __func__, va, pmap);
			return (KERN_FAILURE);
		}
		/* Break the existing mapping(s). */
		SLIST_INIT(&free);
		if (pte1_is_section(opte1)) {
			/*
			 * If the section resulted from a promotion, then a
			 * reserved PT page could be freed.
			 */
			pmap_remove_pte1(pmap, pte1p, va, &free);
		} else {
			sched_pin();
			end = va + PTE1_SIZE;
			for (cur = va, pte2p = pmap_pte2_quick(pmap, va);
			    cur != end; cur += PAGE_SIZE, pte2p++) {
				pte2 = pte2_load(pte2p);
				if (!pte2_is_valid(pte2))
					continue;
				if (pmap_remove_pte2(pmap, pte2p, cur, &free))
					break;
			}
			sched_unpin();
		}
		vm_page_free_pages_toq(&free, false);
	}
	if ((m->oflags & VPO_UNMANAGED) == 0) {
		/*
		 * Abort this mapping if its PV entry could not be created.
		 */
		if (!pmap_pv_insert_pte1(pmap, va, pte1, flags)) {
			CTR3(KTR_PMAP, "%s: failure for va %#lx in pmap %p",
			    __func__, va, pmap);
			return (KERN_RESOURCE_SHORTAGE);
		}
		if ((pte1 & PTE1_RO) == 0) {
			for (mt = m; mt < &m[PTE1_SIZE / PAGE_SIZE]; mt++)
				vm_page_aflag_set(mt, PGA_WRITEABLE);
		}
	}

	/*
	 * Increment counters.
	 */
	if (pte1_is_wired(pte1))
		pmap->pm_stats.wired_count += PTE1_SIZE / PAGE_SIZE;
	pmap->pm_stats.resident_count += PTE1_SIZE / PAGE_SIZE;

	/*
	 * Sync icache if exec permission and attribute VM_MEMATTR_WB_WA
	 * is set.  QQQ: For more info, see comments in pmap_enter().
	 */
	if ((pte1 & PTE1_NX) == 0 && m->md.pat_mode == VM_MEMATTR_WB_WA &&
	    pmap != kernel_pmap && (!pte1_is_section(opte1) ||
	    pte1_pa(opte1) != VM_PAGE_TO_PHYS(m) || (opte1 & PTE2_NX) != 0))
		cache_icache_sync_fresh(va, VM_PAGE_TO_PHYS(m), PTE1_SIZE);

	/*
	 * Map the section.
	 */
	pte1_store(pte1p, pte1);

	pmap_pte1_mappings++;
	CTR3(KTR_PMAP, "%s: success for va %#lx in pmap %p", __func__, va,
	    pmap);
	return (KERN_SUCCESS);
}

/*
 *  Maps a sequence of resident pages belonging to the same object.
 *  The sequence begins with the given page m_start.  This page is
 *  mapped at the given virtual address start.  Each subsequent page is
 *  mapped at a virtual address that is offset from start by the same
 *  amount as the page is offset from m_start within the object.  The
 *  last page in the sequence is the page with the largest offset from
 *  m_start that can be mapped at a virtual address less than the given
 *  virtual address end.  Not every virtual page between start and end
 *  is mapped; only those for which a resident page exists with the
 *  corresponding offset from m_start are mapped.
 */
void
pmap_enter_object(pmap_t pmap, vm_offset_t start, vm_offset_t end,
    vm_page_t m_start, vm_prot_t prot)
{
	vm_offset_t va;
	vm_page_t m, mpt2pg;
	vm_pindex_t diff, psize;

	PDEBUG(6, printf("%s: pmap %p start %#x end  %#x m %p prot %#x\n",
	    __func__, pmap, start, end, m_start, prot));

	VM_OBJECT_ASSERT_LOCKED(m_start->object);
	psize = atop(end - start);
	mpt2pg = NULL;
	m = m_start;
	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	while (m != NULL && (diff = m->pindex - m_start->pindex) < psize) {
		va = start + ptoa(diff);
		if ((va & PTE1_OFFSET) == 0 && va + PTE1_SIZE <= end &&
		    m->psind == 1 && sp_enabled &&
		    pmap_enter_1mpage(pmap, va, m, prot))
			m = &m[PTE1_SIZE / PAGE_SIZE - 1];
		else
			mpt2pg = pmap_enter_quick_locked(pmap, va, m, prot,
			    mpt2pg);
		m = TAILQ_NEXT(m, listq);
	}
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
}

/*
 *  This code maps large physical mmap regions into the
 *  processor address space.  Note that some shortcuts
 *  are taken, but the code works.
 */
void
pmap_object_init_pt(pmap_t pmap, vm_offset_t addr, vm_object_t object,
    vm_pindex_t pindex, vm_size_t size)
{
	pt1_entry_t *pte1p;
	vm_paddr_t pa, pte2_pa;
	vm_page_t p;
	vm_memattr_t pat_mode;
	u_int l1attr, l1prot;

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(object->type == OBJT_DEVICE || object->type == OBJT_SG,
	    ("%s: non-device object", __func__));
	if ((addr & PTE1_OFFSET) == 0 && (size & PTE1_OFFSET) == 0) {
		if (!vm_object_populate(object, pindex, pindex + atop(size)))
			return;
		p = vm_page_lookup(object, pindex);
		KASSERT(p->valid == VM_PAGE_BITS_ALL,
		    ("%s: invalid page %p", __func__, p));
		pat_mode = p->md.pat_mode;

		/*
		 * Abort the mapping if the first page is not physically
		 * aligned to a 1MB page boundary.
		 */
		pte2_pa = VM_PAGE_TO_PHYS(p);
		if (pte2_pa & PTE1_OFFSET)
			return;

		/*
		 * Skip the first page. Abort the mapping if the rest of
		 * the pages are not physically contiguous or have differing
		 * memory attributes.
		 */
		p = TAILQ_NEXT(p, listq);
		for (pa = pte2_pa + PAGE_SIZE; pa < pte2_pa + size;
		    pa += PAGE_SIZE) {
			KASSERT(p->valid == VM_PAGE_BITS_ALL,
			    ("%s: invalid page %p", __func__, p));
			if (pa != VM_PAGE_TO_PHYS(p) ||
			    pat_mode != p->md.pat_mode)
				return;
			p = TAILQ_NEXT(p, listq);
		}

		/*
		 * Map using 1MB pages.
		 *
		 * QQQ: Well, we are mapping a section, so same condition must
		 * be hold like during promotion. It looks that only RW mapping
		 * is done here, so readonly mapping must be done elsewhere.
		 */
		l1prot = PTE1_U | PTE1_NG | PTE1_RW | PTE1_M | PTE1_A;
		l1attr = ATTR_TO_L1(vm_memattr_to_pte2(pat_mode));
		PMAP_LOCK(pmap);
		for (pa = pte2_pa; pa < pte2_pa + size; pa += PTE1_SIZE) {
			pte1p = pmap_pte1(pmap, addr);
			if (!pte1_is_valid(pte1_load(pte1p))) {
				pte1_store(pte1p, PTE1(pa, l1prot, l1attr));
				pmap->pm_stats.resident_count += PTE1_SIZE /
				    PAGE_SIZE;
				pmap_pte1_mappings++;
			}
			/* Else continue on if the PTE1 is already valid. */
			addr += PTE1_SIZE;
		}
		PMAP_UNLOCK(pmap);
	}
}

/*
 *  Do the things to protect a 1mpage in a process.
 */
static void
pmap_protect_pte1(pmap_t pmap, pt1_entry_t *pte1p, vm_offset_t sva,
    vm_prot_t prot)
{
	pt1_entry_t npte1, opte1;
	vm_offset_t eva, va;
	vm_page_t m;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	KASSERT((sva & PTE1_OFFSET) == 0,
	    ("%s: sva is not 1mpage aligned", __func__));

	opte1 = npte1 = pte1_load(pte1p);
	if (pte1_is_managed(opte1) && pte1_is_dirty(opte1)) {
		eva = sva + PTE1_SIZE;
		for (va = sva, m = PHYS_TO_VM_PAGE(pte1_pa(opte1));
		    va < eva; va += PAGE_SIZE, m++)
			vm_page_dirty(m);
	}
	if ((prot & VM_PROT_WRITE) == 0)
		npte1 |= PTE1_RO | PTE1_NM;
	if ((prot & VM_PROT_EXECUTE) == 0)
		npte1 |= PTE1_NX;

	/*
	 * QQQ: Herein, execute permission is never set.
	 *      It only can be cleared. So, no icache
	 *      syncing is needed.
	 */

	if (npte1 != opte1) {
		pte1_store(pte1p, npte1);
		pmap_tlb_flush(pmap, sva);
	}
}

/*
 *	Set the physical protection on the
 *	specified range of this map as requested.
 */
void
pmap_protect(pmap_t pmap, vm_offset_t sva, vm_offset_t eva, vm_prot_t prot)
{
	boolean_t pv_lists_locked;
	vm_offset_t nextva;
	pt1_entry_t *pte1p, pte1;
	pt2_entry_t *pte2p, opte2, npte2;

	KASSERT((prot & ~VM_PROT_ALL) == 0, ("invalid prot %x", prot));
	if (prot == VM_PROT_NONE) {
		pmap_remove(pmap, sva, eva);
		return;
	}

	if ((prot & (VM_PROT_WRITE | VM_PROT_EXECUTE)) ==
	    (VM_PROT_WRITE | VM_PROT_EXECUTE))
		return;

	if (pmap_is_current(pmap))
		pv_lists_locked = FALSE;
	else {
		pv_lists_locked = TRUE;
resume:
		rw_wlock(&pvh_global_lock);
		sched_pin();
	}

	PMAP_LOCK(pmap);
	for (; sva < eva; sva = nextva) {
		/*
		 * Calculate address for next L2 page table.
		 */
		nextva = pte1_trunc(sva + PTE1_SIZE);
		if (nextva < sva)
			nextva = eva;

		pte1p = pmap_pte1(pmap, sva);
		pte1 = pte1_load(pte1p);

		/*
		 * Weed out invalid mappings. Note: we assume that L1 page
		 * page table is always allocated, and in kernel virtual.
		 */
		if (pte1 == 0)
			continue;

		if (pte1_is_section(pte1)) {
			/*
			 * Are we protecting the entire large page?  If not,
			 * demote the mapping and fall through.
			 */
			if (sva + PTE1_SIZE == nextva && eva >= nextva) {
				pmap_protect_pte1(pmap, pte1p, sva, prot);
				continue;
			} else {
				if (!pv_lists_locked) {
					pv_lists_locked = TRUE;
					if (!rw_try_wlock(&pvh_global_lock)) {
						PMAP_UNLOCK(pmap);
						goto resume;
					}
					sched_pin();
				}
				if (!pmap_demote_pte1(pmap, pte1p, sva)) {
					/*
					 * The large page mapping
					 * was destroyed.
					 */
					continue;
				}
#ifdef INVARIANTS
				else {
					/* Update pte1 after demotion */
					pte1 = pte1_load(pte1p);
				}
#endif
			}
		}

		KASSERT(pte1_is_link(pte1), ("%s: pmap %p va %#x pte1 %#x at %p"
		    " is not link", __func__, pmap, sva, pte1, pte1p));

		/*
		 * Limit our scan to either the end of the va represented
		 * by the current L2 page table page, or to the end of the
		 * range being protected.
		 */
		if (nextva > eva)
			nextva = eva;

		for (pte2p = pmap_pte2_quick(pmap, sva); sva != nextva; pte2p++,
		    sva += PAGE_SIZE) {
			vm_page_t m;

			opte2 = npte2 = pte2_load(pte2p);
			if (!pte2_is_valid(opte2))
				continue;

			if ((prot & VM_PROT_WRITE) == 0) {
				if (pte2_is_managed(opte2) &&
				    pte2_is_dirty(opte2)) {
					m = PHYS_TO_VM_PAGE(pte2_pa(opte2));
					vm_page_dirty(m);
				}
				npte2 |= PTE2_RO | PTE2_NM;
			}

			if ((prot & VM_PROT_EXECUTE) == 0)
				npte2 |= PTE2_NX;

			/*
			 * QQQ: Herein, execute permission is never set.
			 *      It only can be cleared. So, no icache
			 *      syncing is needed.
			 */

			if (npte2 != opte2) {
				pte2_store(pte2p, npte2);
				pmap_tlb_flush(pmap, sva);
			}
		}
	}
	if (pv_lists_locked) {
		sched_unpin();
		rw_wunlock(&pvh_global_lock);
	}
	PMAP_UNLOCK(pmap);
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
	pt1_entry_t pte1;
	pt2_entry_t pte2;
	pv_entry_t pv;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	sched_pin();
	TAILQ_FOREACH(pv, &pvh->pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pte1 = pte1_load(pmap_pte1(pmap, pv->pv_va));
		if (pte1_is_section(pte1)) {
			if (pte1_is_wired(pte1))
				count++;
		} else {
			KASSERT(pte1_is_link(pte1),
			    ("%s: pte1 %#x is not link", __func__, pte1));
			pte2 = pte2_load(pmap_pte2_quick(pmap, pv->pv_va));
			if (pte2_is_wired(pte2))
				count++;
		}
		PMAP_UNLOCK(pmap);
	}
	sched_unpin();
	return (count);
}

/*
 *	pmap_page_wired_mappings:
 *
 *	Return the number of managed mappings to the given physical page
 *	that are wired.
 */
int
pmap_page_wired_mappings(vm_page_t m)
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
 *  Returns TRUE if any of the given mappings were used to modify
 *  physical memory.  Otherwise, returns FALSE.  Both page and 1mpage
 *  mappings are supported.
 */
static boolean_t
pmap_is_modified_pvh(struct md_page *pvh)
{
	pv_entry_t pv;
	pt1_entry_t pte1;
	pt2_entry_t pte2;
	pmap_t pmap;
	boolean_t rv;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	rv = FALSE;
	sched_pin();
	TAILQ_FOREACH(pv, &pvh->pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pte1 = pte1_load(pmap_pte1(pmap, pv->pv_va));
		if (pte1_is_section(pte1)) {
			rv = pte1_is_dirty(pte1);
		} else {
			KASSERT(pte1_is_link(pte1),
			    ("%s: pte1 %#x is not link", __func__, pte1));
			pte2 = pte2_load(pmap_pte2_quick(pmap, pv->pv_va));
			rv = pte2_is_dirty(pte2);
		}
		PMAP_UNLOCK(pmap);
		if (rv)
			break;
	}
	sched_unpin();
	return (rv);
}

/*
 *	pmap_is_modified:
 *
 *	Return whether or not the specified physical page was modified
 *	in any physical maps.
 */
boolean_t
pmap_is_modified(vm_page_t m)
{
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("%s: page %p is not managed", __func__, m));

	/*
	 * If the page is not exclusive busied, then PGA_WRITEABLE cannot be
	 * concurrently set while the object is locked.  Thus, if PGA_WRITEABLE
	 * is clear, no PTE2s can have PG_M set.
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
 *	pmap_is_prefaultable:
 *
 *	Return whether or not the specified virtual address is eligible
 *	for prefault.
 */
boolean_t
pmap_is_prefaultable(pmap_t pmap, vm_offset_t addr)
{
	pt1_entry_t pte1;
	pt2_entry_t pte2;
	boolean_t rv;

	rv = FALSE;
	PMAP_LOCK(pmap);
	pte1 = pte1_load(pmap_pte1(pmap, addr));
	if (pte1_is_link(pte1)) {
		pte2 = pte2_load(pt2map_entry(addr));
		rv = !pte2_is_valid(pte2) ;
	}
	PMAP_UNLOCK(pmap);
	return (rv);
}

/*
 *  Returns TRUE if any of the given mappings were referenced and FALSE
 *  otherwise. Both page and 1mpage mappings are supported.
 */
static boolean_t
pmap_is_referenced_pvh(struct md_page *pvh)
{

	pv_entry_t pv;
	pt1_entry_t pte1;
	pt2_entry_t pte2;
	pmap_t pmap;
	boolean_t rv;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	rv = FALSE;
	sched_pin();
	TAILQ_FOREACH(pv, &pvh->pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pte1 = pte1_load(pmap_pte1(pmap, pv->pv_va));
		if (pte1_is_section(pte1)) {
			rv = (pte1 & (PTE1_A | PTE1_V)) == (PTE1_A | PTE1_V);
		} else {
			pte2 = pte2_load(pmap_pte2_quick(pmap, pv->pv_va));
			rv = (pte2 & (PTE2_A | PTE2_V)) == (PTE2_A | PTE2_V);
		}
		PMAP_UNLOCK(pmap);
		if (rv)
			break;
	}
	sched_unpin();
	return (rv);
}

/*
 *	pmap_is_referenced:
 *
 *	Return whether or not the specified physical page was referenced
 *	in any physical maps.
 */
boolean_t
pmap_is_referenced(vm_page_t m)
{
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("%s: page %p is not managed", __func__, m));
	rw_wlock(&pvh_global_lock);
	rv = pmap_is_referenced_pvh(&m->md) ||
	    ((m->flags & PG_FICTITIOUS) == 0 &&
	    pmap_is_referenced_pvh(pa_to_pvh(VM_PAGE_TO_PHYS(m))));
	rw_wunlock(&pvh_global_lock);
	return (rv);
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
int
pmap_ts_referenced(vm_page_t m)
{
	struct md_page *pvh;
	pv_entry_t pv, pvf;
	pmap_t pmap;
	pt1_entry_t  *pte1p, opte1;
	pt2_entry_t *pte2p, opte2;
	vm_paddr_t pa;
	int rtval = 0;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("%s: page %p is not managed", __func__, m));
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
		pte1p = pmap_pte1(pmap, pv->pv_va);
		opte1 = pte1_load(pte1p);
		if (pte1_is_dirty(opte1)) {
			/*
			 * Although "opte1" is mapping a 1MB page, because
			 * this function is called at a 4KB page granularity,
			 * we only update the 4KB page under test.
			 */
			vm_page_dirty(m);
		}
		if ((opte1 & PTE1_A) != 0) {
			/*
			 * Since this reference bit is shared by 256 4KB pages,
			 * it should not be cleared every time it is tested.
			 * Apply a simple "hash" function on the physical page
			 * number, the virtual section number, and the pmap
			 * address to select one 4KB page out of the 256
			 * on which testing the reference bit will result
			 * in clearing that bit. This function is designed
			 * to avoid the selection of the same 4KB page
			 * for every 1MB page mapping.
			 *
			 * On demotion, a mapping that hasn't been referenced
			 * is simply destroyed.  To avoid the possibility of a
			 * subsequent page fault on a demoted wired mapping,
			 * always leave its reference bit set.  Moreover,
			 * since the section is wired, the current state of
			 * its reference bit won't affect page replacement.
			 */
			 if ((((pa >> PAGE_SHIFT) ^ (pv->pv_va >> PTE1_SHIFT) ^
			    (uintptr_t)pmap) & (NPTE2_IN_PG - 1)) == 0 &&
			    !pte1_is_wired(opte1)) {
				pte1_clear_bit(pte1p, PTE1_A);
				pmap_tlb_flush(pmap, pv->pv_va);
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
		pte1p = pmap_pte1(pmap, pv->pv_va);
		KASSERT(pte1_is_link(pte1_load(pte1p)),
		    ("%s: not found a link in page %p's pv list", __func__, m));

		pte2p = pmap_pte2_quick(pmap, pv->pv_va);
		opte2 = pte2_load(pte2p);
		if (pte2_is_dirty(opte2))
			vm_page_dirty(m);
		if ((opte2 & PTE2_A) != 0) {
			pte2_clear_bit(pte2p, PTE2_A);
			pmap_tlb_flush(pmap, pv->pv_va);
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
 *	Clear the wired attribute from the mappings for the specified range of
 *	addresses in the given pmap.  Every valid mapping within that range
 *	must have the wired attribute set.  In contrast, invalid mappings
 *	cannot have the wired attribute set, so they are ignored.
 *
 *	The wired attribute of the page table entry is not a hardware feature,
 *	so there is no need to invalidate any TLB entries.
 */
void
pmap_unwire(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	vm_offset_t nextva;
	pt1_entry_t *pte1p, pte1;
	pt2_entry_t *pte2p, pte2;
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
	for (; sva < eva; sva = nextva) {
		nextva = pte1_trunc(sva + PTE1_SIZE);
		if (nextva < sva)
			nextva = eva;

		pte1p = pmap_pte1(pmap, sva);
		pte1 = pte1_load(pte1p);

		/*
		 * Weed out invalid mappings. Note: we assume that L1 page
		 * page table is always allocated, and in kernel virtual.
		 */
		if (pte1 == 0)
			continue;

		if (pte1_is_section(pte1)) {
			if (!pte1_is_wired(pte1))
				panic("%s: pte1 %#x not wired", __func__, pte1);

			/*
			 * Are we unwiring the entire large page?  If not,
			 * demote the mapping and fall through.
			 */
			if (sva + PTE1_SIZE == nextva && eva >= nextva) {
				pte1_clear_bit(pte1p, PTE1_W);
				pmap->pm_stats.wired_count -= PTE1_SIZE /
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
				if (!pmap_demote_pte1(pmap, pte1p, sva))
					panic("%s: demotion failed", __func__);
#ifdef INVARIANTS
				else {
					/* Update pte1 after demotion */
					pte1 = pte1_load(pte1p);
				}
#endif
			}
		}

		KASSERT(pte1_is_link(pte1), ("%s: pmap %p va %#x pte1 %#x at %p"
		    " is not link", __func__, pmap, sva, pte1, pte1p));

		/*
		 * Limit our scan to either the end of the va represented
		 * by the current L2 page table page, or to the end of the
		 * range being protected.
		 */
		if (nextva > eva)
			nextva = eva;

		for (pte2p = pmap_pte2_quick(pmap, sva); sva != nextva; pte2p++,
		    sva += PAGE_SIZE) {
			pte2 = pte2_load(pte2p);
			if (!pte2_is_valid(pte2))
				continue;
			if (!pte2_is_wired(pte2))
				panic("%s: pte2 %#x is missing PTE2_W",
				    __func__, pte2);

			/*
			 * PTE2_W must be cleared atomically. Although the pmap
			 * lock synchronizes access to PTE2_W, another processor
			 * could be changing PTE2_NM and/or PTE2_A concurrently.
			 */
			pte2_clear_bit(pte2p, PTE2_W);
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
 *  Clear the write and modified bits in each of the given page's mappings.
 */
void
pmap_remove_write(vm_page_t m)
{
	struct md_page *pvh;
	pv_entry_t next_pv, pv;
	pmap_t pmap;
	pt1_entry_t *pte1p;
	pt2_entry_t *pte2p, opte2;
	vm_offset_t va;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("%s: page %p is not managed", __func__, m));

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
		pte1p = pmap_pte1(pmap, va);
		if (!(pte1_load(pte1p) & PTE1_RO))
			(void)pmap_demote_pte1(pmap, pte1p, va);
		PMAP_UNLOCK(pmap);
	}
small_mappings:
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pte1p = pmap_pte1(pmap, pv->pv_va);
		KASSERT(!pte1_is_section(pte1_load(pte1p)), ("%s: found"
		    " a section in page %p's pv list", __func__, m));
		pte2p = pmap_pte2_quick(pmap, pv->pv_va);
		opte2 = pte2_load(pte2p);
		if (!(opte2 & PTE2_RO)) {
			pte2_store(pte2p, opte2 | PTE2_RO | PTE2_NM);
			if (pte2_is_dirty(opte2))
				vm_page_dirty(m);
			pmap_tlb_flush(pmap, pv->pv_va);
		}
		PMAP_UNLOCK(pmap);
	}
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
}

/*
 *	Apply the given advice to the specified range of addresses within the
 *	given pmap.  Depending on the advice, clear the referenced and/or
 *	modified flags in each mapping and set the mapped page's dirty field.
 */
void
pmap_advise(pmap_t pmap, vm_offset_t sva, vm_offset_t eva, int advice)
{
	pt1_entry_t *pte1p, opte1;
	pt2_entry_t *pte2p, pte2;
	vm_offset_t pdnxt;
	vm_page_t m;
	boolean_t pv_lists_locked;

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
	PMAP_LOCK(pmap);
	for (; sva < eva; sva = pdnxt) {
		pdnxt = pte1_trunc(sva + PTE1_SIZE);
		if (pdnxt < sva)
			pdnxt = eva;
		pte1p = pmap_pte1(pmap, sva);
		opte1 = pte1_load(pte1p);
		if (!pte1_is_valid(opte1)) /* XXX */
			continue;
		else if (pte1_is_section(opte1)) {
			if (!pte1_is_managed(opte1))
				continue;
			if (!pv_lists_locked) {
				pv_lists_locked = TRUE;
				if (!rw_try_wlock(&pvh_global_lock)) {
					PMAP_UNLOCK(pmap);
					goto resume;
				}
				sched_pin();
			}
			if (!pmap_demote_pte1(pmap, pte1p, sva)) {
				/*
				 * The large page mapping was destroyed.
				 */
				continue;
			}

			/*
			 * Unless the page mappings are wired, remove the
			 * mapping to a single page so that a subsequent
			 * access may repromote.  Since the underlying L2 page
			 * table is fully populated, this removal never
			 * frees a L2 page table page.
			 */
			if (!pte1_is_wired(opte1)) {
				pte2p = pmap_pte2_quick(pmap, sva);
				KASSERT(pte2_is_valid(pte2_load(pte2p)),
				    ("%s: invalid PTE2", __func__));
				pmap_remove_pte2(pmap, pte2p, sva, NULL);
			}
		}
		if (pdnxt > eva)
			pdnxt = eva;
		for (pte2p = pmap_pte2_quick(pmap, sva); sva != pdnxt; pte2p++,
		    sva += PAGE_SIZE) {
			pte2 = pte2_load(pte2p);
			if (!pte2_is_valid(pte2) || !pte2_is_managed(pte2))
				continue;
			else if (pte2_is_dirty(pte2)) {
				if (advice == MADV_DONTNEED) {
					/*
					 * Future calls to pmap_is_modified()
					 * can be avoided by making the page
					 * dirty now.
					 */
					m = PHYS_TO_VM_PAGE(pte2_pa(pte2));
					vm_page_dirty(m);
				}
				pte2_set_bit(pte2p, PTE2_NM);
				pte2_clear_bit(pte2p, PTE2_A);
			} else if ((pte2 & PTE2_A) != 0)
				pte2_clear_bit(pte2p, PTE2_A);
			else
				continue;
			pmap_tlb_flush(pmap, sva);
		}
	}
	if (pv_lists_locked) {
		sched_unpin();
		rw_wunlock(&pvh_global_lock);
	}
	PMAP_UNLOCK(pmap);
}

/*
 *	Clear the modify bits on the specified physical page.
 */
void
pmap_clear_modify(vm_page_t m)
{
	struct md_page *pvh;
	pv_entry_t next_pv, pv;
	pmap_t pmap;
	pt1_entry_t *pte1p, opte1;
	pt2_entry_t *pte2p, opte2;
	vm_offset_t va;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("%s: page %p is not managed", __func__, m));
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	KASSERT(!vm_page_xbusied(m),
	    ("%s: page %p is exclusive busy", __func__, m));

	/*
	 * If the page is not PGA_WRITEABLE, then no PTE2s can have PTE2_NM
	 * cleared. If the object containing the page is locked and the page
	 * is not exclusive busied, then PGA_WRITEABLE cannot be concurrently
	 * set.
	 */
	if ((m->flags & PGA_WRITEABLE) == 0)
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
		pte1p = pmap_pte1(pmap, va);
		opte1 = pte1_load(pte1p);
		if (!(opte1 & PTE1_RO)) {
			if (pmap_demote_pte1(pmap, pte1p, va) &&
			    !pte1_is_wired(opte1)) {
				/*
				 * Write protect the mapping to a
				 * single page so that a subsequent
				 * write access may repromote.
				 */
				va += VM_PAGE_TO_PHYS(m) - pte1_pa(opte1);
				pte2p = pmap_pte2_quick(pmap, va);
				opte2 = pte2_load(pte2p);
				if ((opte2 & PTE2_V)) {
					pte2_set_bit(pte2p, PTE2_NM | PTE2_RO);
					vm_page_dirty(m);
					pmap_tlb_flush(pmap, va);
				}
			}
		}
		PMAP_UNLOCK(pmap);
	}
small_mappings:
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pte1p = pmap_pte1(pmap, pv->pv_va);
		KASSERT(!pte1_is_section(pte1_load(pte1p)), ("%s: found"
		    " a section in page %p's pv list", __func__, m));
		pte2p = pmap_pte2_quick(pmap, pv->pv_va);
		if (pte2_is_dirty(pte2_load(pte2p))) {
			pte2_set_bit(pte2p, PTE2_NM);
			pmap_tlb_flush(pmap, pv->pv_va);
		}
		PMAP_UNLOCK(pmap);
	}
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
}


/*
 *  Sets the memory attribute for the specified page.
 */
void
pmap_page_set_memattr(vm_page_t m, vm_memattr_t ma)
{
	pt2_entry_t *cmap2_pte2p;
	vm_memattr_t oma;
	vm_paddr_t pa;
	struct pcpu *pc;

	oma = m->md.pat_mode;
	m->md.pat_mode = ma;

	CTR5(KTR_PMAP, "%s: page %p - 0x%08X oma: %d, ma: %d", __func__, m,
	    VM_PAGE_TO_PHYS(m), oma, ma);
	if ((m->flags & PG_FICTITIOUS) != 0)
		return;
#if 0
	/*
	 * If "m" is a normal page, flush it from the cache.
	 *
	 * First, try to find an existing mapping of the page by sf
	 * buffer. sf_buf_invalidate_cache() modifies mapping and
	 * flushes the cache.
	 */
	if (sf_buf_invalidate_cache(m, oma))
		return;
#endif
	/*
	 * If page is not mapped by sf buffer, map the page
	 * transient and do invalidation.
	 */
	if (ma != oma) {
		pa = VM_PAGE_TO_PHYS(m);
		sched_pin();
		pc = get_pcpu();
		cmap2_pte2p = pc->pc_cmap2_pte2p;
		mtx_lock(&pc->pc_cmap_lock);
		if (pte2_load(cmap2_pte2p) != 0)
			panic("%s: CMAP2 busy", __func__);
		pte2_store(cmap2_pte2p, PTE2_KERN_NG(pa, PTE2_AP_KRW,
		    vm_memattr_to_pte2(ma)));
		dcache_wbinv_poc((vm_offset_t)pc->pc_cmap2_addr, pa, PAGE_SIZE);
		pte2_clear(cmap2_pte2p);
		tlb_flush((vm_offset_t)pc->pc_cmap2_addr);
		sched_unpin();
		mtx_unlock(&pc->pc_cmap_lock);
	}
}

/*
 *  Miscellaneous support routines follow
 */

/*
 *  Returns TRUE if the given page is mapped individually or as part of
 *  a 1mpage.  Otherwise, returns FALSE.
 */
boolean_t
pmap_page_is_mapped(vm_page_t m)
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
 *  Returns true if the pmap's pv is one of the first
 *  16 pvs linked to from this page.  This count may
 *  be changed upwards or downwards in the future; it
 *  is only necessary that true be returned for a small
 *  subset of pmaps for proper page aging.
 */
boolean_t
pmap_page_exists_quick(pmap_t pmap, vm_page_t m)
{
	struct md_page *pvh;
	pv_entry_t pv;
	int loops = 0;
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("%s: page %p is not managed", __func__, m));
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
 *	pmap_zero_page zeros the specified hardware page by mapping
 *	the page into KVM and using bzero to clear its contents.
 */
void
pmap_zero_page(vm_page_t m)
{
	pt2_entry_t *cmap2_pte2p;
	struct pcpu *pc;

	sched_pin();
	pc = get_pcpu();
	cmap2_pte2p = pc->pc_cmap2_pte2p;
	mtx_lock(&pc->pc_cmap_lock);
	if (pte2_load(cmap2_pte2p) != 0)
		panic("%s: CMAP2 busy", __func__);
	pte2_store(cmap2_pte2p, PTE2_KERN_NG(VM_PAGE_TO_PHYS(m), PTE2_AP_KRW,
	    vm_page_pte2_attr(m)));
	pagezero(pc->pc_cmap2_addr);
	pte2_clear(cmap2_pte2p);
	tlb_flush((vm_offset_t)pc->pc_cmap2_addr);
	sched_unpin();
	mtx_unlock(&pc->pc_cmap_lock);
}

/*
 *	pmap_zero_page_area zeros the specified hardware page by mapping
 *	the page into KVM and using bzero to clear its contents.
 *
 *	off and size may not cover an area beyond a single hardware page.
 */
void
pmap_zero_page_area(vm_page_t m, int off, int size)
{
	pt2_entry_t *cmap2_pte2p;
	struct pcpu *pc;

	sched_pin();
	pc = get_pcpu();
	cmap2_pte2p = pc->pc_cmap2_pte2p;
	mtx_lock(&pc->pc_cmap_lock);
	if (pte2_load(cmap2_pte2p) != 0)
		panic("%s: CMAP2 busy", __func__);
	pte2_store(cmap2_pte2p, PTE2_KERN_NG(VM_PAGE_TO_PHYS(m), PTE2_AP_KRW,
	    vm_page_pte2_attr(m)));
	if (off == 0 && size == PAGE_SIZE)
		pagezero(pc->pc_cmap2_addr);
	else
		bzero(pc->pc_cmap2_addr + off, size);
	pte2_clear(cmap2_pte2p);
	tlb_flush((vm_offset_t)pc->pc_cmap2_addr);
	sched_unpin();
	mtx_unlock(&pc->pc_cmap_lock);
}

/*
 *	pmap_copy_page copies the specified (machine independent)
 *	page by mapping the page into virtual memory and using
 *	bcopy to copy the page, one machine dependent page at a
 *	time.
 */
void
pmap_copy_page(vm_page_t src, vm_page_t dst)
{
	pt2_entry_t *cmap1_pte2p, *cmap2_pte2p;
	struct pcpu *pc;

	sched_pin();
	pc = get_pcpu();
	cmap1_pte2p = pc->pc_cmap1_pte2p;
	cmap2_pte2p = pc->pc_cmap2_pte2p;
	mtx_lock(&pc->pc_cmap_lock);
	if (pte2_load(cmap1_pte2p) != 0)
		panic("%s: CMAP1 busy", __func__);
	if (pte2_load(cmap2_pte2p) != 0)
		panic("%s: CMAP2 busy", __func__);
	pte2_store(cmap1_pte2p, PTE2_KERN_NG(VM_PAGE_TO_PHYS(src),
	    PTE2_AP_KR | PTE2_NM, vm_page_pte2_attr(src)));
	pte2_store(cmap2_pte2p, PTE2_KERN_NG(VM_PAGE_TO_PHYS(dst),
	    PTE2_AP_KRW, vm_page_pte2_attr(dst)));
	bcopy(pc->pc_cmap1_addr, pc->pc_cmap2_addr, PAGE_SIZE);
	pte2_clear(cmap1_pte2p);
	tlb_flush((vm_offset_t)pc->pc_cmap1_addr);
	pte2_clear(cmap2_pte2p);
	tlb_flush((vm_offset_t)pc->pc_cmap2_addr);
	sched_unpin();
	mtx_unlock(&pc->pc_cmap_lock);
}

int unmapped_buf_allowed = 1;

void
pmap_copy_pages(vm_page_t ma[], vm_offset_t a_offset, vm_page_t mb[],
    vm_offset_t b_offset, int xfersize)
{
	pt2_entry_t *cmap1_pte2p, *cmap2_pte2p;
	vm_page_t a_pg, b_pg;
	char *a_cp, *b_cp;
	vm_offset_t a_pg_offset, b_pg_offset;
	struct pcpu *pc;
	int cnt;

	sched_pin();
	pc = get_pcpu();
	cmap1_pte2p = pc->pc_cmap1_pte2p;
	cmap2_pte2p = pc->pc_cmap2_pte2p;
	mtx_lock(&pc->pc_cmap_lock);
	if (pte2_load(cmap1_pte2p) != 0)
		panic("pmap_copy_pages: CMAP1 busy");
	if (pte2_load(cmap2_pte2p) != 0)
		panic("pmap_copy_pages: CMAP2 busy");
	while (xfersize > 0) {
		a_pg = ma[a_offset >> PAGE_SHIFT];
		a_pg_offset = a_offset & PAGE_MASK;
		cnt = min(xfersize, PAGE_SIZE - a_pg_offset);
		b_pg = mb[b_offset >> PAGE_SHIFT];
		b_pg_offset = b_offset & PAGE_MASK;
		cnt = min(cnt, PAGE_SIZE - b_pg_offset);
		pte2_store(cmap1_pte2p, PTE2_KERN_NG(VM_PAGE_TO_PHYS(a_pg),
		    PTE2_AP_KR | PTE2_NM, vm_page_pte2_attr(a_pg)));
		tlb_flush_local((vm_offset_t)pc->pc_cmap1_addr);
		pte2_store(cmap2_pte2p, PTE2_KERN_NG(VM_PAGE_TO_PHYS(b_pg),
		    PTE2_AP_KRW, vm_page_pte2_attr(b_pg)));
		tlb_flush_local((vm_offset_t)pc->pc_cmap2_addr);
		a_cp = pc->pc_cmap1_addr + a_pg_offset;
		b_cp = pc->pc_cmap2_addr + b_pg_offset;
		bcopy(a_cp, b_cp, cnt);
		a_offset += cnt;
		b_offset += cnt;
		xfersize -= cnt;
	}
	pte2_clear(cmap1_pte2p);
	tlb_flush((vm_offset_t)pc->pc_cmap1_addr);
	pte2_clear(cmap2_pte2p);
	tlb_flush((vm_offset_t)pc->pc_cmap2_addr);
	sched_unpin();
	mtx_unlock(&pc->pc_cmap_lock);
}

vm_offset_t
pmap_quick_enter_page(vm_page_t m)
{
	struct pcpu *pc;
	pt2_entry_t *pte2p;

	critical_enter();
	pc = get_pcpu();
	pte2p = pc->pc_qmap_pte2p;

	KASSERT(pte2_load(pte2p) == 0, ("%s: PTE2 busy", __func__));

	pte2_store(pte2p, PTE2_KERN_NG(VM_PAGE_TO_PHYS(m), PTE2_AP_KRW,
	    vm_page_pte2_attr(m)));
	return (pc->pc_qmap_addr);
}

void
pmap_quick_remove_page(vm_offset_t addr)
{
	struct pcpu *pc;
	pt2_entry_t *pte2p;

	pc = get_pcpu();
	pte2p = pc->pc_qmap_pte2p;

	KASSERT(addr == pc->pc_qmap_addr, ("%s: invalid address", __func__));
	KASSERT(pte2_load(pte2p) != 0, ("%s: PTE2 not in use", __func__));

	pte2_clear(pte2p);
	tlb_flush(pc->pc_qmap_addr);
	critical_exit();
}

/*
 *	Copy the range specified by src_addr/len
 *	from the source map to the range dst_addr/len
 *	in the destination map.
 *
 *	This routine is only advisory and need not do anything.
 */
void
pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, vm_offset_t dst_addr, vm_size_t len,
    vm_offset_t src_addr)
{
	struct spglist free;
	vm_offset_t addr;
	vm_offset_t end_addr = src_addr + len;
	vm_offset_t nextva;

	if (dst_addr != src_addr)
		return;

	if (!pmap_is_current(src_pmap))
		return;

	rw_wlock(&pvh_global_lock);
	if (dst_pmap < src_pmap) {
		PMAP_LOCK(dst_pmap);
		PMAP_LOCK(src_pmap);
	} else {
		PMAP_LOCK(src_pmap);
		PMAP_LOCK(dst_pmap);
	}
	sched_pin();
	for (addr = src_addr; addr < end_addr; addr = nextva) {
		pt2_entry_t *src_pte2p, *dst_pte2p;
		vm_page_t dst_mpt2pg, src_mpt2pg;
		pt1_entry_t src_pte1;
		u_int pte1_idx;

		KASSERT(addr < VM_MAXUSER_ADDRESS,
		    ("%s: invalid to pmap_copy page tables", __func__));

		nextva = pte1_trunc(addr + PTE1_SIZE);
		if (nextva < addr)
			nextva = end_addr;

		pte1_idx = pte1_index(addr);
		src_pte1 = src_pmap->pm_pt1[pte1_idx];
		if (pte1_is_section(src_pte1)) {
			if ((addr & PTE1_OFFSET) != 0 ||
			    (addr + PTE1_SIZE) > end_addr)
				continue;
			if (dst_pmap->pm_pt1[pte1_idx] == 0 &&
			    (!pte1_is_managed(src_pte1) ||
			    pmap_pv_insert_pte1(dst_pmap, addr, src_pte1,
			    PMAP_ENTER_NORECLAIM))) {
				dst_pmap->pm_pt1[pte1_idx] = src_pte1 &
				    ~PTE1_W;
				dst_pmap->pm_stats.resident_count +=
				    PTE1_SIZE / PAGE_SIZE;
				pmap_pte1_mappings++;
			}
			continue;
		} else if (!pte1_is_link(src_pte1))
			continue;

		src_mpt2pg = PHYS_TO_VM_PAGE(pte1_link_pa(src_pte1));

		/*
		 * We leave PT2s to be linked from PT1 even if they are not
		 * referenced until all PT2s in a page are without reference.
		 *
		 * QQQ: It could be changed ...
		 */
#if 0 /* single_pt2_link_is_cleared */
		KASSERT(pt2_wirecount_get(src_mpt2pg, pte1_idx) > 0,
		    ("%s: source page table page is unused", __func__));
#else
		if (pt2_wirecount_get(src_mpt2pg, pte1_idx) == 0)
			continue;
#endif
		if (nextva > end_addr)
			nextva = end_addr;

		src_pte2p = pt2map_entry(addr);
		while (addr < nextva) {
			pt2_entry_t temp_pte2;
			temp_pte2 = pte2_load(src_pte2p);
			/*
			 * we only virtual copy managed pages
			 */
			if (pte2_is_managed(temp_pte2)) {
				dst_mpt2pg = pmap_allocpte2(dst_pmap, addr,
				    PMAP_ENTER_NOSLEEP);
				if (dst_mpt2pg == NULL)
					goto out;
				dst_pte2p = pmap_pte2_quick(dst_pmap, addr);
				if (!pte2_is_valid(pte2_load(dst_pte2p)) &&
				    pmap_try_insert_pv_entry(dst_pmap, addr,
				    PHYS_TO_VM_PAGE(pte2_pa(temp_pte2)))) {
					/*
					 * Clear the wired, modified, and
					 * accessed (referenced) bits
					 * during the copy.
					 */
					temp_pte2 &=  ~(PTE2_W | PTE2_A);
					temp_pte2 |= PTE2_NM;
					pte2_store(dst_pte2p, temp_pte2);
					dst_pmap->pm_stats.resident_count++;
				} else {
					SLIST_INIT(&free);
					if (pmap_unwire_pt2(dst_pmap, addr,
					    dst_mpt2pg, &free)) {
						pmap_tlb_flush(dst_pmap, addr);
						vm_page_free_pages_toq(&free,
						    false);
					}
					goto out;
				}
				if (pt2_wirecount_get(dst_mpt2pg, pte1_idx) >=
				    pt2_wirecount_get(src_mpt2pg, pte1_idx))
					break;
			}
			addr += PAGE_SIZE;
			src_pte2p++;
		}
	}
out:
	sched_unpin();
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(src_pmap);
	PMAP_UNLOCK(dst_pmap);
}

/*
 *	Increase the starting virtual address of the given mapping if a
 *	different alignment might result in more section mappings.
 */
void
pmap_align_superpage(vm_object_t object, vm_ooffset_t offset,
    vm_offset_t *addr, vm_size_t size)
{
	vm_offset_t pte1_offset;

	if (size < PTE1_SIZE)
		return;
	if (object != NULL && (object->flags & OBJ_COLORED) != 0)
		offset += ptoa(object->pg_color);
	pte1_offset = offset & PTE1_OFFSET;
	if (size - ((PTE1_SIZE - pte1_offset) & PTE1_OFFSET) < PTE1_SIZE ||
	    (*addr & PTE1_OFFSET) == pte1_offset)
		return;
	if ((*addr & PTE1_OFFSET) < pte1_offset)
		*addr = pte1_trunc(*addr) + pte1_offset;
	else
		*addr = pte1_roundup(*addr) + pte1_offset;
}

void
pmap_activate(struct thread *td)
{
	pmap_t pmap, oldpmap;
	u_int cpuid, ttb;

	PDEBUG(9, printf("%s: td = %08x\n", __func__, (uint32_t)td));

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

	ttb = pmap_ttb_get(pmap);

	/*
	 * pmap_activate is for the current thread on the current cpu
	 */
	td->td_pcb->pcb_pagedir = ttb;
	cp15_ttbr_set(ttb);
	PCPU_SET(curpmap, pmap);
	critical_exit();
}

/*
 *  Perform the pmap work for mincore.
 */
int
pmap_mincore(pmap_t pmap, vm_offset_t addr, vm_paddr_t *locked_pa)
{
	pt1_entry_t *pte1p, pte1;
	pt2_entry_t *pte2p, pte2;
	vm_paddr_t pa;
	bool managed;
	int val;

	PMAP_LOCK(pmap);
retry:
	pte1p = pmap_pte1(pmap, addr);
	pte1 = pte1_load(pte1p);
	if (pte1_is_section(pte1)) {
		pa = trunc_page(pte1_pa(pte1) | (addr & PTE1_OFFSET));
		managed = pte1_is_managed(pte1);
		val = MINCORE_SUPER | MINCORE_INCORE;
		if (pte1_is_dirty(pte1))
			val |= MINCORE_MODIFIED | MINCORE_MODIFIED_OTHER;
		if (pte1 & PTE1_A)
			val |= MINCORE_REFERENCED | MINCORE_REFERENCED_OTHER;
	} else if (pte1_is_link(pte1)) {
		pte2p = pmap_pte2(pmap, addr);
		pte2 = pte2_load(pte2p);
		pmap_pte2_release(pte2p);
		pa = pte2_pa(pte2);
		managed = pte2_is_managed(pte2);
		val = MINCORE_INCORE;
		if (pte2_is_dirty(pte2))
			val |= MINCORE_MODIFIED | MINCORE_MODIFIED_OTHER;
		if (pte2 & PTE2_A)
			val |= MINCORE_REFERENCED | MINCORE_REFERENCED_OTHER;
	} else {
		managed = false;
		val = 0;
	}
	if ((val & (MINCORE_MODIFIED_OTHER | MINCORE_REFERENCED_OTHER)) !=
	    (MINCORE_MODIFIED_OTHER | MINCORE_REFERENCED_OTHER) && managed) {
		/* Ensure that "PHYS_TO_VM_PAGE(pa)->object" doesn't change. */
		if (vm_page_pa_tryrelock(pmap, pa, locked_pa))
			goto retry;
	} else
		PA_UNLOCK_COND(*locked_pa);
	PMAP_UNLOCK(pmap);
	return (val);
}

void
pmap_kenter_device(vm_offset_t va, vm_size_t size, vm_paddr_t pa)
{
	vm_offset_t sva;
	uint32_t l2attr;

	KASSERT((size & PAGE_MASK) == 0,
	    ("%s: device mapping not page-sized", __func__));

	sva = va;
	l2attr = vm_memattr_to_pte2(VM_MEMATTR_DEVICE);
	while (size != 0) {
		pmap_kenter_prot_attr(va, pa, PTE2_AP_KRW, l2attr);
		va += PAGE_SIZE;
		pa += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	tlb_flush_range(sva, va - sva);
}

void
pmap_kremove_device(vm_offset_t va, vm_size_t size)
{
	vm_offset_t sva;

	KASSERT((size & PAGE_MASK) == 0,
	    ("%s: device mapping not page-sized", __func__));

	sva = va;
	while (size != 0) {
		pmap_kremove(va);
		va += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	tlb_flush_range(sva, va - sva);
}

void
pmap_set_pcb_pagedir(pmap_t pmap, struct pcb *pcb)
{

	pcb->pcb_pagedir = pmap_ttb_get(pmap);
}


/*
 *  Clean L1 data cache range by physical address.
 *  The range must be within a single page.
 */
static void
pmap_dcache_wb_pou(vm_paddr_t pa, vm_size_t size, uint32_t attr)
{
	pt2_entry_t *cmap2_pte2p;
	struct pcpu *pc;

	KASSERT(((pa & PAGE_MASK) + size) <= PAGE_SIZE,
	    ("%s: not on single page", __func__));

	sched_pin();
	pc = get_pcpu();
	cmap2_pte2p = pc->pc_cmap2_pte2p;
	mtx_lock(&pc->pc_cmap_lock);
	if (pte2_load(cmap2_pte2p) != 0)
		panic("%s: CMAP2 busy", __func__);
	pte2_store(cmap2_pte2p, PTE2_KERN_NG(pa, PTE2_AP_KRW, attr));
	dcache_wb_pou((vm_offset_t)pc->pc_cmap2_addr + (pa & PAGE_MASK), size);
	pte2_clear(cmap2_pte2p);
	tlb_flush((vm_offset_t)pc->pc_cmap2_addr);
	sched_unpin();
	mtx_unlock(&pc->pc_cmap_lock);
}

/*
 *  Sync instruction cache range which is not mapped yet.
 */
void
cache_icache_sync_fresh(vm_offset_t va, vm_paddr_t pa, vm_size_t size)
{
	uint32_t len, offset;
	vm_page_t m;

	/* Write back d-cache on given address range. */
	offset = pa & PAGE_MASK;
	for ( ; size != 0; size -= len, pa += len, offset = 0) {
		len = min(PAGE_SIZE - offset, size);
		m = PHYS_TO_VM_PAGE(pa);
		KASSERT(m != NULL, ("%s: vm_page_t is null for %#x",
		  __func__, pa));
		pmap_dcache_wb_pou(pa, len, vm_page_pte2_attr(m));
	}
	/*
	 * I-cache is VIPT. Only way how to flush all virtual mappings
	 * on given physical address is to invalidate all i-cache.
	 */
	icache_inv_all();
}

void
pmap_sync_icache(pmap_t pmap, vm_offset_t va, vm_size_t size)
{

	/* Write back d-cache on given address range. */
	if (va >= VM_MIN_KERNEL_ADDRESS) {
		dcache_wb_pou(va, size);
	} else {
		uint32_t len, offset;
		vm_paddr_t pa;
		vm_page_t m;

		offset = va & PAGE_MASK;
		for ( ; size != 0; size -= len, va += len, offset = 0) {
			pa = pmap_extract(pmap, va); /* offset is preserved */
			len = min(PAGE_SIZE - offset, size);
			m = PHYS_TO_VM_PAGE(pa);
			KASSERT(m != NULL, ("%s: vm_page_t is null for %#x",
				__func__, pa));
			pmap_dcache_wb_pou(pa, len, vm_page_pte2_attr(m));
		}
	}
	/*
	 * I-cache is VIPT. Only way how to flush all virtual mappings
	 * on given physical address is to invalidate all i-cache.
	 */
	icache_inv_all();
}

/*
 *  The implementation of pmap_fault() uses IN_RANGE2() macro which
 *  depends on the fact that given range size is a power of 2.
 */
CTASSERT(powerof2(NB_IN_PT1));
CTASSERT(powerof2(PT2MAP_SIZE));

#define IN_RANGE2(addr, start, size)	\
    ((vm_offset_t)(start) == ((vm_offset_t)(addr) & ~((size) - 1)))

/*
 *  Handle access and R/W emulation faults.
 */
int
pmap_fault(pmap_t pmap, vm_offset_t far, uint32_t fsr, int idx, bool usermode)
{
	pt1_entry_t *pte1p, pte1;
	pt2_entry_t *pte2p, pte2;

	if (pmap == NULL)
		pmap = kernel_pmap;

	/*
	 * In kernel, we should never get abort with FAR which is in range of
	 * pmap->pm_pt1 or PT2MAP address spaces. If it happens, stop here
	 * and print out a useful abort message and even get to the debugger
	 * otherwise it likely ends with never ending loop of aborts.
	 */
	if (__predict_false(IN_RANGE2(far, pmap->pm_pt1, NB_IN_PT1))) {
		/*
		 * All L1 tables should always be mapped and present.
		 * However, we check only current one herein. For user mode,
		 * only permission abort from malicious user is not fatal.
		 * And alignment abort as it may have higher priority.
		 */
		if (!usermode || (idx != FAULT_ALIGN && idx != FAULT_PERM_L2)) {
			CTR4(KTR_PMAP, "%s: pmap %#x pm_pt1 %#x far %#x",
			    __func__, pmap, pmap->pm_pt1, far);
			panic("%s: pm_pt1 abort", __func__);
		}
		return (KERN_INVALID_ADDRESS);
	}
	if (__predict_false(IN_RANGE2(far, PT2MAP, PT2MAP_SIZE))) {
		/*
		 * PT2MAP should be always mapped and present in current
		 * L1 table. However, only existing L2 tables are mapped
		 * in PT2MAP. For user mode, only L2 translation abort and
		 * permission abort from malicious user is not fatal.
		 * And alignment abort as it may have higher priority.
		 */
		if (!usermode || (idx != FAULT_ALIGN &&
		    idx != FAULT_TRAN_L2 && idx != FAULT_PERM_L2)) {
			CTR4(KTR_PMAP, "%s: pmap %#x PT2MAP %#x far %#x",
			    __func__, pmap, PT2MAP, far);
			panic("%s: PT2MAP abort", __func__);
		}
		return (KERN_INVALID_ADDRESS);
	}

	/*
	 * A pmap lock is used below for handling of access and R/W emulation
	 * aborts. They were handled by atomic operations before so some
	 * analysis of new situation is needed to answer the following question:
	 * Is it safe to use the lock even for these aborts?
	 *
	 * There may happen two cases in general:
	 *
	 * (1) Aborts while the pmap lock is locked already - this should not
	 * happen as pmap lock is not recursive. However, under pmap lock only
	 * internal kernel data should be accessed and such data should be
	 * mapped with A bit set and NM bit cleared. If double abort happens,
	 * then a mapping of data which has caused it must be fixed. Further,
	 * all new mappings are always made with A bit set and the bit can be
	 * cleared only on managed mappings.
	 *
	 * (2) Aborts while another lock(s) is/are locked - this already can
	 * happen. However, there is no difference here if it's either access or
	 * R/W emulation abort, or if it's some other abort.
	 */

	PMAP_LOCK(pmap);
#ifdef INVARIANTS
	pte1 = pte1_load(pmap_pte1(pmap, far));
	if (pte1_is_link(pte1)) {
		/*
		 * Check in advance that associated L2 page table is mapped into
		 * PT2MAP space. Note that faulty access to not mapped L2 page
		 * table is caught in more general check above where "far" is
		 * checked that it does not lay in PT2MAP space. Note also that
		 * L1 page table and PT2TAB always exist and are mapped.
		 */
		pte2 = pt2tab_load(pmap_pt2tab_entry(pmap, far));
		if (!pte2_is_valid(pte2))
			panic("%s: missing L2 page table (%p, %#x)",
			    __func__, pmap, far);
	}
#endif
#ifdef SMP
	/*
	 * Special treatment is due to break-before-make approach done when
	 * pte1 is updated for userland mapping during section promotion or
	 * demotion. If not caught here, pmap_enter() can find a section
	 * mapping on faulting address. That is not allowed.
	 */
	if (idx == FAULT_TRAN_L1 && usermode && cp15_ats1cur_check(far) == 0) {
		PMAP_UNLOCK(pmap);
		return (KERN_SUCCESS);
	}
#endif
	/*
	 * Accesss bits for page and section. Note that the entry
	 * is not in TLB yet, so TLB flush is not necessary.
	 *
	 * QQQ: This is hardware emulation, we do not call userret()
	 *      for aborts from user mode.
	 */
	if (idx == FAULT_ACCESS_L2) {
		pte1 = pte1_load(pmap_pte1(pmap, far));
		if (pte1_is_link(pte1)) {
			/* L2 page table should exist and be mapped. */
			pte2p = pt2map_entry(far);
			pte2 = pte2_load(pte2p);
			if (pte2_is_valid(pte2)) {
				pte2_store(pte2p, pte2 | PTE2_A);
				PMAP_UNLOCK(pmap);
				return (KERN_SUCCESS);
			}
		} else {
			/*
			 * We got L2 access fault but PTE1 is not a link.
			 * Probably some race happened, do nothing.
			 */
			CTR3(KTR_PMAP, "%s: FAULT_ACCESS_L2 - pmap %#x far %#x",
			    __func__, pmap, far);
			PMAP_UNLOCK(pmap);
			return (KERN_SUCCESS);
		}
	}
	if (idx == FAULT_ACCESS_L1) {
		pte1p = pmap_pte1(pmap, far);
		pte1 = pte1_load(pte1p);
		if (pte1_is_section(pte1)) {
			pte1_store(pte1p, pte1 | PTE1_A);
			PMAP_UNLOCK(pmap);
			return (KERN_SUCCESS);
		} else {
			/*
			 * We got L1 access fault but PTE1 is not section
			 * mapping. Probably some race happened, do nothing.
			 */
			CTR3(KTR_PMAP, "%s: FAULT_ACCESS_L1 - pmap %#x far %#x",
			    __func__, pmap, far);
			PMAP_UNLOCK(pmap);
			return (KERN_SUCCESS);
		}
	}

	/*
	 * Handle modify bits for page and section. Note that the modify
	 * bit is emulated by software. So PTEx_RO is software read only
	 * bit and PTEx_NM flag is real hardware read only bit.
	 *
	 * QQQ: This is hardware emulation, we do not call userret()
	 *      for aborts from user mode.
	 */
	if ((fsr & FSR_WNR) && (idx == FAULT_PERM_L2)) {
		pte1 = pte1_load(pmap_pte1(pmap, far));
		if (pte1_is_link(pte1)) {
			/* L2 page table should exist and be mapped. */
			pte2p = pt2map_entry(far);
			pte2 = pte2_load(pte2p);
			if (pte2_is_valid(pte2) && !(pte2 & PTE2_RO) &&
			    (pte2 & PTE2_NM)) {
				pte2_store(pte2p, pte2 & ~PTE2_NM);
				tlb_flush(trunc_page(far));
				PMAP_UNLOCK(pmap);
				return (KERN_SUCCESS);
			}
		} else {
			/*
			 * We got L2 permission fault but PTE1 is not a link.
			 * Probably some race happened, do nothing.
			 */
			CTR3(KTR_PMAP, "%s: FAULT_PERM_L2 - pmap %#x far %#x",
			    __func__, pmap, far);
			PMAP_UNLOCK(pmap);
			return (KERN_SUCCESS);
		}
	}
	if ((fsr & FSR_WNR) && (idx == FAULT_PERM_L1)) {
		pte1p = pmap_pte1(pmap, far);
		pte1 = pte1_load(pte1p);
		if (pte1_is_section(pte1)) {
			if (!(pte1 & PTE1_RO) && (pte1 & PTE1_NM)) {
				pte1_store(pte1p, pte1 & ~PTE1_NM);
				tlb_flush(pte1_trunc(far));
				PMAP_UNLOCK(pmap);
				return (KERN_SUCCESS);
			}
		} else {
			/*
			 * We got L1 permission fault but PTE1 is not section
			 * mapping. Probably some race happened, do nothing.
			 */
			CTR3(KTR_PMAP, "%s: FAULT_PERM_L1 - pmap %#x far %#x",
			    __func__, pmap, far);
			PMAP_UNLOCK(pmap);
			return (KERN_SUCCESS);
		}
	}

	/*
	 * QQQ: The previous code, mainly fast handling of access and
	 *      modify bits aborts, could be moved to ASM. Now we are
	 *      starting to deal with not fast aborts.
	 */
	PMAP_UNLOCK(pmap);
	return (KERN_FAILURE);
}

#if defined(PMAP_DEBUG)
/*
 *  Reusing of KVA used in pmap_zero_page function !!!
 */
static void
pmap_zero_page_check(vm_page_t m)
{
	pt2_entry_t *cmap2_pte2p;
	uint32_t *p, *end;
	struct pcpu *pc;

	sched_pin();
	pc = get_pcpu();
	cmap2_pte2p = pc->pc_cmap2_pte2p;
	mtx_lock(&pc->pc_cmap_lock);
	if (pte2_load(cmap2_pte2p) != 0)
		panic("%s: CMAP2 busy", __func__);
	pte2_store(cmap2_pte2p, PTE2_KERN_NG(VM_PAGE_TO_PHYS(m), PTE2_AP_KRW,
	    vm_page_pte2_attr(m)));
	end = (uint32_t*)(pc->pc_cmap2_addr + PAGE_SIZE);
	for (p = (uint32_t*)pc->pc_cmap2_addr; p < end; p++)
		if (*p != 0)
			panic("%s: page %p not zero, va: %p", __func__, m,
			    pc->pc_cmap2_addr);
	pte2_clear(cmap2_pte2p);
	tlb_flush((vm_offset_t)pc->pc_cmap2_addr);
	sched_unpin();
	mtx_unlock(&pc->pc_cmap_lock);
}

int
pmap_pid_dump(int pid)
{
	pmap_t pmap;
	struct proc *p;
	int npte2 = 0;
	int i, j, index;

	sx_slock(&allproc_lock);
	FOREACH_PROC_IN_SYSTEM(p) {
		if (p->p_pid != pid || p->p_vmspace == NULL)
			continue;
		index = 0;
		pmap = vmspace_pmap(p->p_vmspace);
		for (i = 0; i < NPTE1_IN_PT1; i++) {
			pt1_entry_t pte1;
			pt2_entry_t *pte2p, pte2;
			vm_offset_t base, va;
			vm_paddr_t pa;
			vm_page_t m;

			base = i << PTE1_SHIFT;
			pte1 = pte1_load(&pmap->pm_pt1[i]);

			if (pte1_is_section(pte1)) {
				/*
				 * QQQ: Do something here!
				 */
			} else if (pte1_is_link(pte1)) {
				for (j = 0; j < NPTE2_IN_PT2; j++) {
					va = base + (j << PAGE_SHIFT);
					if (va >= VM_MIN_KERNEL_ADDRESS) {
						if (index) {
							index = 0;
							printf("\n");
						}
						sx_sunlock(&allproc_lock);
						return (npte2);
					}
					pte2p = pmap_pte2(pmap, va);
					pte2 = pte2_load(pte2p);
					pmap_pte2_release(pte2p);
					if (!pte2_is_valid(pte2))
						continue;

					pa = pte2_pa(pte2);
					m = PHYS_TO_VM_PAGE(pa);
					printf("va: 0x%x, pa: 0x%x, h: %d, w:"
					    " %d, f: 0x%x", va, pa,
					    m->hold_count, m->wire_count,
					    m->flags);
					npte2++;
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
	sx_sunlock(&allproc_lock);
	return (npte2);
}

#endif

#ifdef DDB
static pt2_entry_t *
pmap_pte2_ddb(pmap_t pmap, vm_offset_t va)
{
	pt1_entry_t pte1;
	vm_paddr_t pt2pg_pa;

	pte1 = pte1_load(pmap_pte1(pmap, va));
	if (!pte1_is_link(pte1))
		return (NULL);

	if (pmap_is_current(pmap))
		return (pt2map_entry(va));

	/* Note that L2 page table size is not equal to PAGE_SIZE. */
	pt2pg_pa = trunc_page(pte1_link_pa(pte1));
	if (pte2_pa(pte2_load(PMAP3)) != pt2pg_pa) {
		pte2_store(PMAP3, PTE2_KPT(pt2pg_pa));
#ifdef SMP
		PMAP3cpu = PCPU_GET(cpuid);
#endif
		tlb_flush_local((vm_offset_t)PADDR3);
	}
#ifdef SMP
	else if (PMAP3cpu != PCPU_GET(cpuid)) {
		PMAP3cpu = PCPU_GET(cpuid);
		tlb_flush_local((vm_offset_t)PADDR3);
	}
#endif
	return (PADDR3 + (arm32_btop(va) & (NPTE2_IN_PG - 1)));
}

static void
dump_pmap(pmap_t pmap)
{

	printf("pmap %p\n", pmap);
	printf("  pm_pt1: %p\n", pmap->pm_pt1);
	printf("  pm_pt2tab: %p\n", pmap->pm_pt2tab);
	printf("  pm_active: 0x%08lX\n", pmap->pm_active.__bits[0]);
}

DB_SHOW_COMMAND(pmaps, pmap_list_pmaps)
{

	pmap_t pmap;
	LIST_FOREACH(pmap, &allpmaps, pm_list) {
		dump_pmap(pmap);
	}
}

static int
pte2_class(pt2_entry_t pte2)
{
	int cls;

	cls = (pte2 >> 2) & 0x03;
	cls |= (pte2 >> 4) & 0x04;
	return (cls);
}

static void
dump_section(pmap_t pmap, uint32_t pte1_idx)
{
}

static void
dump_link(pmap_t pmap, uint32_t pte1_idx, boolean_t invalid_ok)
{
	uint32_t i;
	vm_offset_t va;
	pt2_entry_t *pte2p, pte2;
	vm_page_t m;

	va = pte1_idx << PTE1_SHIFT;
	pte2p = pmap_pte2_ddb(pmap, va);
	for (i = 0; i < NPTE2_IN_PT2; i++, pte2p++, va += PAGE_SIZE) {
		pte2 = pte2_load(pte2p);
		if (pte2 == 0)
			continue;
		if (!pte2_is_valid(pte2)) {
			printf(" 0x%08X: 0x%08X", va, pte2);
			if (!invalid_ok)
				printf(" - not valid !!!");
			printf("\n");
			continue;
		}
		m = PHYS_TO_VM_PAGE(pte2_pa(pte2));
		printf(" 0x%08X: 0x%08X, TEX%d, s:%d, g:%d, m:%p", va , pte2,
		    pte2_class(pte2), !!(pte2 & PTE2_S), !(pte2 & PTE2_NG), m);
		if (m != NULL) {
			printf(" v:%d h:%d w:%d f:0x%04X\n", m->valid,
			    m->hold_count, m->wire_count, m->flags);
		} else {
			printf("\n");
		}
	}
}

static __inline boolean_t
is_pv_chunk_space(vm_offset_t va)
{

	if ((((vm_offset_t)pv_chunkbase) <= va) &&
	    (va < ((vm_offset_t)pv_chunkbase + PAGE_SIZE * pv_maxchunks)))
		return (TRUE);
	return (FALSE);
}

DB_SHOW_COMMAND(pmap, pmap_pmap_print)
{
	/* XXX convert args. */
	pmap_t pmap = (pmap_t)addr;
	pt1_entry_t pte1;
	pt2_entry_t pte2;
	vm_offset_t va, eva;
	vm_page_t m;
	uint32_t i;
	boolean_t invalid_ok, dump_link_ok, dump_pv_chunk;

	if (have_addr) {
		pmap_t pm;

		LIST_FOREACH(pm, &allpmaps, pm_list)
			if (pm == pmap) break;
		if (pm == NULL) {
			printf("given pmap %p is not in allpmaps list\n", pmap);
			return;
		}
	} else
		pmap = PCPU_GET(curpmap);

	eva = (modif[0] == 'u') ? VM_MAXUSER_ADDRESS : 0xFFFFFFFF;
	dump_pv_chunk = FALSE; /* XXX evaluate from modif[] */

	printf("pmap: 0x%08X\n", (uint32_t)pmap);
	printf("PT2MAP: 0x%08X\n", (uint32_t)PT2MAP);
	printf("pt2tab: 0x%08X\n", (uint32_t)pmap->pm_pt2tab);

	for(i = 0; i < NPTE1_IN_PT1; i++) {
		pte1 = pte1_load(&pmap->pm_pt1[i]);
		if (pte1 == 0)
			continue;
		va = i << PTE1_SHIFT;
		if (va >= eva)
			break;

		if (pte1_is_section(pte1)) {
			printf("0x%08X: Section 0x%08X, s:%d g:%d\n", va, pte1,
			    !!(pte1 & PTE1_S), !(pte1 & PTE1_NG));
			dump_section(pmap, i);
		} else if (pte1_is_link(pte1)) {
			dump_link_ok = TRUE;
			invalid_ok = FALSE;
			pte2 = pte2_load(pmap_pt2tab_entry(pmap, va));
			m = PHYS_TO_VM_PAGE(pte1_link_pa(pte1));
			printf("0x%08X: Link 0x%08X, pt2tab: 0x%08X m: %p",
			    va, pte1, pte2, m);
			if (is_pv_chunk_space(va)) {
				printf(" - pv_chunk space");
				if (dump_pv_chunk)
					invalid_ok = TRUE;
				else
					dump_link_ok = FALSE;
			}
			else if (m != NULL)
				printf(" w:%d w2:%u", m->wire_count,
				    pt2_wirecount_get(m, pte1_index(va)));
			if (pte2 == 0)
				printf(" !!! pt2tab entry is ZERO");
			else if (pte2_pa(pte1) != pte2_pa(pte2))
				printf(" !!! pt2tab entry is DIFFERENT - m: %p",
				    PHYS_TO_VM_PAGE(pte2_pa(pte2)));
			printf("\n");
			if (dump_link_ok)
				dump_link(pmap, i, invalid_ok);
		} else
			printf("0x%08X: Invalid entry 0x%08X\n", va, pte1);
	}
}

static void
dump_pt2tab(pmap_t pmap)
{
	uint32_t i;
	pt2_entry_t pte2;
	vm_offset_t va;
	vm_paddr_t pa;
	vm_page_t m;

	printf("PT2TAB:\n");
	for (i = 0; i < PT2TAB_ENTRIES; i++) {
		pte2 = pte2_load(&pmap->pm_pt2tab[i]);
		if (!pte2_is_valid(pte2))
			continue;
		va = i << PT2TAB_SHIFT;
		pa = pte2_pa(pte2);
		m = PHYS_TO_VM_PAGE(pa);
		printf(" 0x%08X: 0x%08X, TEX%d, s:%d, m:%p", va, pte2,
		    pte2_class(pte2), !!(pte2 & PTE2_S), m);
		if (m != NULL)
			printf(" , h: %d, w: %d, f: 0x%04X pidx: %lld",
			    m->hold_count, m->wire_count, m->flags, m->pindex);
		printf("\n");
	}
}

DB_SHOW_COMMAND(pmap_pt2tab, pmap_pt2tab_print)
{
	/* XXX convert args. */
	pmap_t pmap = (pmap_t)addr;
	pt1_entry_t pte1;
	pt2_entry_t pte2;
	vm_offset_t va;
	uint32_t i, start;

	if (have_addr) {
		printf("supported only on current pmap\n");
		return;
	}

	pmap = PCPU_GET(curpmap);
	printf("curpmap: 0x%08X\n", (uint32_t)pmap);
	printf("PT2MAP: 0x%08X\n", (uint32_t)PT2MAP);
	printf("pt2tab: 0x%08X\n", (uint32_t)pmap->pm_pt2tab);

	start = pte1_index((vm_offset_t)PT2MAP);
	for (i = start; i < (start + NPT2_IN_PT2TAB); i++) {
		pte1 = pte1_load(&pmap->pm_pt1[i]);
		if (pte1 == 0)
			continue;
		va = i << PTE1_SHIFT;
		if (pte1_is_section(pte1)) {
			printf("0x%08X: Section 0x%08X, s:%d\n", va, pte1,
			    !!(pte1 & PTE1_S));
			dump_section(pmap, i);
		} else if (pte1_is_link(pte1)) {
			pte2 = pte2_load(pmap_pt2tab_entry(pmap, va));
			printf("0x%08X: Link 0x%08X, pt2tab: 0x%08X\n", va,
			    pte1, pte2);
			if (pte2 == 0)
				printf("  !!! pt2tab entry is ZERO\n");
		} else
			printf("0x%08X: Invalid entry 0x%08X\n", va, pte1);
	}
	dump_pt2tab(pmap);
}
#endif
