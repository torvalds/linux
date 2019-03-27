/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2010 Juli Mallett <jmallett@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/pcpu.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/pte.h>
#include <machine/tlb.h>

#if defined(CPU_CNMIPS)
#define	MIPS_MAX_TLB_ENTRIES	128
#elif defined(CPU_NLM)
#define	MIPS_MAX_TLB_ENTRIES	(2048 + 128)
#else
#define	MIPS_MAX_TLB_ENTRIES	64
#endif

struct tlb_state {
	unsigned wired;
	struct tlb_entry {
		register_t entryhi;
		register_t entrylo0;
		register_t entrylo1;
		register_t pagemask;
	} entry[MIPS_MAX_TLB_ENTRIES];
};

static struct tlb_state tlb_state[MAXCPU];

#if 0
/*
 * PageMask must increment in steps of 2 bits.
 */
COMPILE_TIME_ASSERT(POPCNT(TLBMASK_MASK) % 2 == 0);
#endif

static inline void
tlb_probe(void)
{
	__asm __volatile ("tlbp" : : : "memory");
	mips_cp0_sync();
}

static inline void
tlb_read(void)
{
	__asm __volatile ("tlbr" : : : "memory");
	mips_cp0_sync();
}

static inline void
tlb_write_indexed(void)
{
	__asm __volatile ("tlbwi" : : : "memory");
	mips_cp0_sync();
}

static void tlb_invalidate_one(unsigned);

void
tlb_insert_wired(unsigned i, vm_offset_t va, pt_entry_t pte0, pt_entry_t pte1)
{
	register_t asid;
	register_t s;

	va &= ~PAGE_MASK;

	s = intr_disable();
	asid = mips_rd_entryhi() & TLBHI_ASID_MASK;

	mips_wr_index(i);
	mips_wr_pagemask(0);
	mips_wr_entryhi(TLBHI_ENTRY(va, 0));
	mips_wr_entrylo0(pte0);
	mips_wr_entrylo1(pte1);
	tlb_write_indexed();

	mips_wr_entryhi(asid);
	intr_restore(s);
}

void
tlb_invalidate_address(struct pmap *pmap, vm_offset_t va)
{
	register_t asid;
	register_t s;
	int i;

	va &= ~PAGE_MASK;

	s = intr_disable();
	asid = mips_rd_entryhi() & TLBHI_ASID_MASK;

	mips_wr_pagemask(0);
	mips_wr_entryhi(TLBHI_ENTRY(va, pmap_asid(pmap)));
	tlb_probe();
	i = mips_rd_index();
	if (i >= 0)
		tlb_invalidate_one(i);

	mips_wr_entryhi(asid);
	intr_restore(s);
}

void
tlb_invalidate_all(void)
{
	register_t asid;
	register_t s;
	unsigned i;

	s = intr_disable();
	asid = mips_rd_entryhi() & TLBHI_ASID_MASK;

	for (i = mips_rd_wired(); i < num_tlbentries; i++)
		tlb_invalidate_one(i);

	mips_wr_entryhi(asid);
	intr_restore(s);
}

void
tlb_invalidate_all_user(struct pmap *pmap)
{
	register_t asid;
	register_t s;
	unsigned i;

	s = intr_disable();
	asid = mips_rd_entryhi() & TLBHI_ASID_MASK;

	for (i = mips_rd_wired(); i < num_tlbentries; i++) {
		register_t uasid;

		mips_wr_index(i);
		tlb_read();

		uasid = mips_rd_entryhi() & TLBHI_ASID_MASK;
		if (pmap == NULL) {
			/*
			 * Invalidate all non-kernel entries.
			 */
			if (uasid == 0)
				continue;
		} else {
			/*
			 * Invalidate this pmap's entries.
			 */
			if (uasid != pmap_asid(pmap))
				continue;
		}
		tlb_invalidate_one(i);
	}

	mips_wr_entryhi(asid);
	intr_restore(s);
}

/*
 * Invalidates any TLB entries that map a virtual page from the specified
 * address range.  If "end" is zero, then every virtual page is considered to
 * be within the address range's upper bound.
 */
void
tlb_invalidate_range(pmap_t pmap, vm_offset_t start, vm_offset_t end)
{
	register_t asid, end_hi, hi, hi_pagemask, s, save_asid, start_hi;
	int i;

	KASSERT(start < end || (end == 0 && start > 0),
	    ("tlb_invalidate_range: invalid range"));

	/*
	 * Truncate the virtual address "start" to an even page frame number,
	 * and round the virtual address "end" to an even page frame number.
	 */
	start &= ~((1 << TLBMASK_SHIFT) - 1);
	end = roundup2(end, 1 << TLBMASK_SHIFT);

	s = intr_disable();
	save_asid = mips_rd_entryhi() & TLBHI_ASID_MASK;

	asid = pmap_asid(pmap);
	start_hi = TLBHI_ENTRY(start, asid);
	end_hi = TLBHI_ENTRY(end, asid);

	/*
	 * Select the fastest method for invalidating the TLB entries.
	 */
	if (end - start < num_tlbentries << TLBMASK_SHIFT || (end == 0 &&
	    start >= -(num_tlbentries << TLBMASK_SHIFT))) {
		/*
		 * The virtual address range is small compared to the size of
		 * the TLB.  Probe the TLB for each even numbered page frame
		 * within the virtual address range.
		 */
		for (hi = start_hi; hi != end_hi; hi += 1 << TLBMASK_SHIFT) {
			mips_wr_pagemask(0);
			mips_wr_entryhi(hi);
			tlb_probe();
			i = mips_rd_index();
			if (i >= 0)
				tlb_invalidate_one(i);
		}
	} else {
		/*
		 * The virtual address range is large compared to the size of
		 * the TLB.  Test every non-wired TLB entry.
		 */
		for (i = mips_rd_wired(); i < num_tlbentries; i++) {
			mips_wr_index(i);
			tlb_read();
			hi = mips_rd_entryhi();
			if ((hi & TLBHI_ASID_MASK) == asid && (hi < end_hi ||
			    end == 0)) {
				/*
				 * If "hi" is a large page that spans
				 * "start_hi", then it must be invalidated.
				 */
				hi_pagemask = mips_rd_pagemask();
				if (hi >= (start_hi & ~(hi_pagemask <<
				    TLBMASK_SHIFT)))
					tlb_invalidate_one(i);
			}
		}
	}

	mips_wr_entryhi(save_asid);
	intr_restore(s);
}

/* XXX Only if DDB?  */
void
tlb_save(void)
{
	unsigned ntlb, i, cpu;

	cpu = PCPU_GET(cpuid);
	if (num_tlbentries > MIPS_MAX_TLB_ENTRIES)
		ntlb = MIPS_MAX_TLB_ENTRIES;
	else
		ntlb = num_tlbentries;
	tlb_state[cpu].wired = mips_rd_wired();
	for (i = 0; i < ntlb; i++) {
		mips_wr_index(i);
		tlb_read();

		tlb_state[cpu].entry[i].entryhi = mips_rd_entryhi();
		tlb_state[cpu].entry[i].pagemask = mips_rd_pagemask();
		tlb_state[cpu].entry[i].entrylo0 = mips_rd_entrylo0();
		tlb_state[cpu].entry[i].entrylo1 = mips_rd_entrylo1();
	}
}

void
tlb_update(struct pmap *pmap, vm_offset_t va, pt_entry_t pte)
{
	register_t asid;
	register_t s;
	int i;

	va &= ~PAGE_MASK;
	pte &= ~TLBLO_SWBITS_MASK;

	s = intr_disable();
	asid = mips_rd_entryhi() & TLBHI_ASID_MASK;

	mips_wr_pagemask(0);
	mips_wr_entryhi(TLBHI_ENTRY(va, pmap_asid(pmap)));
	tlb_probe();
	i = mips_rd_index();
	if (i >= 0) {
		tlb_read();

		if ((va & PAGE_SIZE) == 0) {
			mips_wr_entrylo0(pte);
		} else {
			mips_wr_entrylo1(pte);
		}
		tlb_write_indexed();
	}

	mips_wr_entryhi(asid);
	intr_restore(s);
}

static void
tlb_invalidate_one(unsigned i)
{
	/* XXX an invalid ASID? */
	mips_wr_entryhi(TLBHI_ENTRY(MIPS_KSEG0_START + (2 * i * PAGE_SIZE), 0));
	mips_wr_entrylo0(0);
	mips_wr_entrylo1(0);
	mips_wr_pagemask(0);
	mips_wr_index(i);
	tlb_write_indexed();
}

#ifdef DDB
#include <ddb/ddb.h>

DB_SHOW_COMMAND(tlb, ddb_dump_tlb)
{
	register_t ehi, elo0, elo1, epagemask;
	unsigned i, cpu, ntlb;

	/*
	 * XXX
	 * The worst conversion from hex to decimal ever.
	 */
	if (have_addr)
		cpu = ((addr >> 4) % 16) * 10 + (addr % 16);
	else
		cpu = PCPU_GET(cpuid);

	if (cpu >= mp_ncpus) {
		db_printf("Invalid CPU %u\n", cpu);
		return;
	}
	if (num_tlbentries > MIPS_MAX_TLB_ENTRIES) {
		ntlb = MIPS_MAX_TLB_ENTRIES;
		db_printf("Warning: Only %d of %d TLB entries saved!\n",
		    ntlb, num_tlbentries);
	} else
		ntlb = num_tlbentries;

	if (cpu == PCPU_GET(cpuid))
		tlb_save();

	db_printf("Beginning TLB dump for CPU %u...\n", cpu);
	for (i = 0; i < ntlb; i++) {
		if (i == tlb_state[cpu].wired) {
			if (i != 0)
				db_printf("^^^ WIRED ENTRIES ^^^\n");
			else
				db_printf("(No wired entries.)\n");
		}

		/* XXX PageMask.  */
		ehi = tlb_state[cpu].entry[i].entryhi;
		elo0 = tlb_state[cpu].entry[i].entrylo0;
		elo1 = tlb_state[cpu].entry[i].entrylo1;
		epagemask = tlb_state[cpu].entry[i].pagemask;

		if (elo0 == 0 && elo1 == 0)
			continue;

		db_printf("#%u\t=> %jx (pagemask %jx)\n", i, (intmax_t)ehi, (intmax_t) epagemask);
		db_printf(" Lo0\t%jx\t(%#jx)\n", (intmax_t)elo0, (intmax_t)TLBLO_PTE_TO_PA(elo0));
		db_printf(" Lo1\t%jx\t(%#jx)\n", (intmax_t)elo1, (intmax_t)TLBLO_PTE_TO_PA(elo1));
	}
	db_printf("Finished.\n");
}
#endif
