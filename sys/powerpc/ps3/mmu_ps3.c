/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2010 Nathan Whitehorn
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
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/msgbuf.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_pageout.h>
#include <vm/uma.h>

#include <powerpc/aim/mmu_oea64.h>

#include "mmu_if.h"
#include "moea64_if.h"
#include "ps3-hvcall.h"

#define VSID_HASH_MASK		0x0000007fffffffffUL
#define PTESYNC()		__asm __volatile("ptesync")

extern int ps3fb_remap(void);

static uint64_t mps3_vas_id;

/*
 * Kernel MMU interface
 */

static void	mps3_bootstrap(mmu_t mmup, vm_offset_t kernelstart,
		    vm_offset_t kernelend);
static void	mps3_cpu_bootstrap(mmu_t mmup, int ap);
static int64_t	mps3_pte_synch(mmu_t, struct pvo_entry *);
static int64_t	mps3_pte_clear(mmu_t, struct pvo_entry *, uint64_t ptebit);
static int64_t	mps3_pte_unset(mmu_t, struct pvo_entry *);
static int	mps3_pte_insert(mmu_t, struct pvo_entry *);


static mmu_method_t mps3_methods[] = {
        MMUMETHOD(mmu_bootstrap,	mps3_bootstrap),
        MMUMETHOD(mmu_cpu_bootstrap,	mps3_cpu_bootstrap),

	MMUMETHOD(moea64_pte_synch,	mps3_pte_synch),
	MMUMETHOD(moea64_pte_clear,	mps3_pte_clear),
	MMUMETHOD(moea64_pte_unset,	mps3_pte_unset),
	MMUMETHOD(moea64_pte_insert,	mps3_pte_insert),

        { 0, 0 }
};

MMU_DEF_INHERIT(ps3_mmu, "mmu_ps3", mps3_methods, 0, oea64_mmu);

static struct mtx mps3_table_lock;

static void
mps3_bootstrap(mmu_t mmup, vm_offset_t kernelstart, vm_offset_t kernelend)
{
	uint64_t final_pteg_count;

	mtx_init(&mps3_table_lock, "page table", NULL, MTX_DEF);

	moea64_early_bootstrap(mmup, kernelstart, kernelend);

	/* In case we had a page table already */
	lv1_destruct_virtual_address_space(0);

	/* Allocate new hardware page table */
	lv1_construct_virtual_address_space(
	    20 /* log_2(moea64_pteg_count) */, 2 /* n page sizes */,
	    (24UL << 56) | (16UL << 48) /* page sizes 16 MB + 64 KB */,
	    &mps3_vas_id, &final_pteg_count
	);

	lv1_select_virtual_address_space(mps3_vas_id);

	moea64_pteg_count = final_pteg_count / sizeof(struct lpteg);

	moea64_mid_bootstrap(mmup, kernelstart, kernelend);
	moea64_late_bootstrap(mmup, kernelstart, kernelend);
}

static void
mps3_cpu_bootstrap(mmu_t mmup, int ap)
{
	struct slb *slb = PCPU_GET(aim.slb);
	register_t seg0;
	int i;

	mtmsr(mfmsr() & ~PSL_DR & ~PSL_IR);

	/*
	 * Select the page table we configured above and set up the FB mapping
	 * so we can have a console.
	 */
	lv1_select_virtual_address_space(mps3_vas_id);

	if (!ap)
		ps3fb_remap();

	/*
	 * Install kernel SLB entries
	 */

        __asm __volatile ("slbia");
        __asm __volatile ("slbmfee %0,%1; slbie %0;" : "=r"(seg0) : "r"(0));
	for (i = 0; i < 64; i++) {
		if (!(slb[i].slbe & SLBE_VALID))
			continue;

		__asm __volatile ("slbmte %0, %1" ::
		    "r"(slb[i].slbv), "r"(slb[i].slbe));
	}
}

static int64_t
mps3_pte_synch_locked(struct pvo_entry *pvo)
{
	uint64_t halfbucket[4], rcbits;
	
	PTESYNC();
	lv1_read_htab_entries(mps3_vas_id, pvo->pvo_pte.slot & ~0x3UL,
	    &halfbucket[0], &halfbucket[1], &halfbucket[2], &halfbucket[3],
	    &rcbits);

	/* Check if present in page table */
	if ((halfbucket[pvo->pvo_pte.slot & 0x3] & LPTE_AVPN_MASK) !=
	    ((pvo->pvo_vpn >> (ADDR_API_SHFT64 - ADDR_PIDX_SHFT)) &
	    LPTE_AVPN_MASK))
		return (-1);
	if (!(halfbucket[pvo->pvo_pte.slot & 0x3] & LPTE_VALID))
		return (-1);

	/*
	 * rcbits contains the low 12 bits of each PTE's 2nd part,
	 * spaced at 16-bit intervals
	 */

	return ((rcbits >> ((3 - (pvo->pvo_pte.slot & 0x3))*16)) &
	    (LPTE_CHG | LPTE_REF));
}

static int64_t
mps3_pte_synch(mmu_t mmu, struct pvo_entry *pvo)
{
	int64_t retval;

	mtx_lock(&mps3_table_lock);
	retval = mps3_pte_synch_locked(pvo);
	mtx_unlock(&mps3_table_lock);

	return (retval);
}

static int64_t
mps3_pte_clear(mmu_t mmu, struct pvo_entry *pvo, uint64_t ptebit)
{
	int64_t refchg;
	struct lpte pte;

	mtx_lock(&mps3_table_lock);

	refchg = mps3_pte_synch_locked(pvo);
	if (refchg < 0) {
		mtx_unlock(&mps3_table_lock);
		return (refchg);
	}

	moea64_pte_from_pvo(pvo, &pte);

	pte.pte_lo |= refchg;
	pte.pte_lo &= ~ptebit;
	/* XXX: race on RC bits between write and sync. Anything to do? */
	lv1_write_htab_entry(mps3_vas_id, pvo->pvo_pte.slot, pte.pte_hi,
	    pte.pte_lo);
	mtx_unlock(&mps3_table_lock);

	return (refchg);
}

static int64_t
mps3_pte_unset(mmu_t mmu, struct pvo_entry *pvo)
{
	int64_t refchg;

	mtx_lock(&mps3_table_lock);
	refchg = mps3_pte_synch_locked(pvo);
	if (refchg < 0) {
		moea64_pte_overflow--;
		mtx_unlock(&mps3_table_lock);
		return (-1);
	}
	/* XXX: race on RC bits between unset and sync. Anything to do? */
	lv1_write_htab_entry(mps3_vas_id, pvo->pvo_pte.slot, 0, 0);
	mtx_unlock(&mps3_table_lock);
	moea64_pte_valid--;

	return (refchg & (LPTE_REF | LPTE_CHG));
}

static int
mps3_pte_insert(mmu_t mmu, struct pvo_entry *pvo)
{
	int result;
	struct lpte pte, evicted;
	uint64_t index;

	if (pvo->pvo_vaddr & PVO_HID) {
		/* Hypercall needs primary PTEG */
		pvo->pvo_vaddr &= ~PVO_HID;
		pvo->pvo_pte.slot ^= (moea64_pteg_mask << 3);
	}

	pvo->pvo_pte.slot &= ~7UL;
	moea64_pte_from_pvo(pvo, &pte);
	evicted.pte_hi = 0;
	PTESYNC();
	mtx_lock(&mps3_table_lock);
	result = lv1_insert_htab_entry(mps3_vas_id, pvo->pvo_pte.slot,
	    pte.pte_hi, pte.pte_lo, LPTE_LOCKED | LPTE_WIRED, 0,
	    &index, &evicted.pte_hi, &evicted.pte_lo);
	mtx_unlock(&mps3_table_lock);

	if (result != 0) {
		/* No freeable slots in either PTEG? We're hosed. */
		panic("mps3_pte_insert: overflow (%d)", result);
		return (-1);
	}

	/*
	 * See where we ended up.
	 */
	if ((index & ~7UL) != pvo->pvo_pte.slot)
		pvo->pvo_vaddr |= PVO_HID;
	pvo->pvo_pte.slot = index;

	moea64_pte_valid++;

	if (evicted.pte_hi) {
		KASSERT((evicted.pte_hi & (LPTE_WIRED | LPTE_LOCKED)) == 0,
		    ("Evicted a wired PTE"));
		moea64_pte_valid--;
		moea64_pte_overflow++;
	}

	return (0);
}

