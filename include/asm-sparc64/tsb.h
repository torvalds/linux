#ifndef _SPARC64_TSB_H
#define _SPARC64_TSB_H

/* The sparc64 TSB is similar to the powerpc hashtables.  It's a
 * power-of-2 sized table of TAG/PTE pairs.  The cpu precomputes
 * pointers into this table for 8K and 64K page sizes, and also a
 * comparison TAG based upon the virtual address and context which
 * faults.
 *
 * TLB miss trap handler software does the actual lookup via something
 * of the form:
 *
 * 	ldxa		[%g0] ASI_{D,I}MMU_TSB_8KB_PTR, %g1
 * 	ldxa		[%g0] ASI_{D,I}MMU, %g6
 * 	ldda		[%g1] ASI_NUCLEUS_QUAD_LDD, %g4
 * 	cmp		%g4, %g6
 * 	bne,pn	%xcc, tsb_miss_{d,i}tlb
 * 	 mov		FAULT_CODE_{D,I}TLB, %g3
 * 	stxa		%g5, [%g0] ASI_{D,I}TLB_DATA_IN
 * 	retry
 *
 *
 * Each 16-byte slot of the TSB is the 8-byte tag and then the 8-byte
 * PTE.  The TAG is of the same layout as the TLB TAG TARGET mmu
 * register which is:
 *
 * -------------------------------------------------
 * |  -  |  CONTEXT |  -  |    VADDR bits 63:22    |
 * -------------------------------------------------
 *  63 61 60      48 47 42 41                     0
 *
 * Like the powerpc hashtables we need to use locking in order to
 * synchronize while we update the entries.  PTE updates need locking
 * as well.
 *
 * We need to carefully choose a lock bits for the TSB entry.  We
 * choose to use bit 47 in the tag.  Also, since we never map anything
 * at page zero in context zero, we use zero as an invalid tag entry.
 * When the lock bit is set, this forces a tag comparison failure.
 *
 * Currently, we allocate an 8K TSB per-process and we use it for both
 * I-TLB and D-TLB misses.  Perhaps at some point we'll add code that
 * monitors the number of active pages in the process as we get
 * major/minor faults, and grow the TSB in response.  The only trick
 * in implementing that is synchronizing the freeing of the old TSB
 * wrt.  parallel TSB updates occuring on other processors.  On
 * possible solution is to use RCU for the freeing of the TSB.
 */

#define TSB_TAG_LOCK	(1 << (47 - 32))

#define TSB_MEMBAR	membar	#StoreStore

#define TSB_LOCK_TAG(TSB, REG1, REG2)	\
99:	lduwa	[TSB] ASI_N, REG1;	\
	sethi	%hi(TSB_TAG_LOCK), REG2;\
	andcc	REG1, REG2, %g0;	\
	bne,pn	%icc, 99b;		\
	 nop;				\
	casa	[TSB] ASI_N, REG1, REG2;\
	cmp	REG1, REG2;		\
	bne,pn	%icc, 99b;		\
	 nop;				\
	TSB_MEMBAR

#define TSB_WRITE(TSB, TTE, TAG)	   \
	stx		TTE, [TSB + 0x08]; \
	TSB_MEMBAR;			   \
	stx		TAG, [TSB + 0x00];

	/* Do a kernel page table walk.  Leaves physical PTE pointer in
	 * REG1.  Jumps to FAIL_LABEL on early page table walk termination.
	 * VADDR will not be clobbered, but REG2 will.
	 */
#define KERN_PGTABLE_WALK(VADDR, REG1, REG2, FAIL_LABEL)	\
	sethi		%hi(swapper_pg_dir), REG1; \
	or		REG1, %lo(swapper_pg_dir), REG1; \
	sllx		VADDR, 64 - (PGDIR_SHIFT + PGDIR_BITS), REG2; \
	srlx		REG2, 64 - PAGE_SHIFT, REG2; \
	andn		REG2, 0x3, REG2; \
	lduw		[REG1 + REG2], REG1; \
	brz,pn		REG1, FAIL_LABEL; \
	 sllx		VADDR, 64 - (PMD_SHIFT + PMD_BITS), REG2; \
	srlx		REG2, 64 - PAGE_SHIFT, REG2; \
	sllx		REG1, 11, REG1; \
	andn		REG2, 0x3, REG2; \
	lduwa		[REG1 + REG2] ASI_PHYS_USE_EC, REG1; \
	brz,pn		REG1, FAIL_LABEL; \
	 sllx		VADDR, 64 - PMD_SHIFT, REG2; \
	srlx		REG2, 64 - PAGE_SHIFT, REG2; \
	sllx		REG1, 11, REG1; \
	andn		REG2, 0x7, REG2; \
	add		REG1, REG2, REG1;

	/* Do a user page table walk in MMU globals.  Leaves physical PTE
	 * pointer in REG1.  Jumps to FAIL_LABEL on early page table walk
	 * termination.  Physical base of page tables is in PHYS_PGD which
	 * will not be modified.
	 *
	 * VADDR will not be clobbered, but REG1 and REG2 will.
	 */
#define USER_PGTABLE_WALK_TL1(VADDR, PHYS_PGD, REG1, REG2, FAIL_LABEL)	\
	sllx		VADDR, 64 - (PGDIR_SHIFT + PGDIR_BITS), REG2; \
	srlx		REG2, 64 - PAGE_SHIFT, REG2; \
	andn		REG2, 0x3, REG2; \
	lduwa		[PHYS_PGD + REG2] ASI_PHYS_USE_EC, REG1; \
	brz,pn		REG1, FAIL_LABEL; \
	 sllx		VADDR, 64 - (PMD_SHIFT + PMD_BITS), REG2; \
	srlx		REG2, 64 - PAGE_SHIFT, REG2; \
	sllx		REG1, 11, REG1; \
	andn		REG2, 0x3, REG2; \
	lduwa		[REG1 + REG2] ASI_PHYS_USE_EC, REG1; \
	brz,pn		REG1, FAIL_LABEL; \
	 sllx		VADDR, 64 - PMD_SHIFT, REG2; \
	srlx		REG2, 64 - PAGE_SHIFT, REG2; \
	sllx		REG1, 11, REG1; \
	andn		REG2, 0x7, REG2; \
	add		REG1, REG2, REG1;

/* Lookup a OBP mapping on VADDR in the prom_trans[] table at TL>0.
 * If no entry is found, FAIL_LABEL will be branched to.  On success
 * the resulting PTE value will be left in REG1.  VADDR is preserved
 * by this routine.
 */
#define OBP_TRANS_LOOKUP(VADDR, REG1, REG2, REG3, FAIL_LABEL) \
	sethi		%hi(prom_trans), REG1; \
	or		REG1, %lo(prom_trans), REG1; \
97:	ldx		[REG1 + 0x00], REG2; \
	brz,pn		REG2, FAIL_LABEL; \
	 nop; \
	ldx		[REG1 + 0x08], REG3; \
	add		REG2, REG3, REG3; \
	cmp		REG2, VADDR; \
	bgu,pt		%xcc, 98f; \
	 cmp		VADDR, REG3; \
	bgeu,pt		%xcc, 98f; \
	 ldx		[REG1 + 0x10], REG3; \
	sub		VADDR, REG2, REG2; \
	ba,pt		%xcc, 99f; \
	 add		REG3, REG2, REG1; \
98:	ba,pt		%xcc, 97b; \
	 add		REG1, (3 * 8), REG1; \
99:

	/* Do a kernel TSB lookup at tl>0 on VADDR+TAG, branch to OK_LABEL
	 * on TSB hit.  REG1, REG2, REG3, and REG4 are used as temporaries
	 * and the found TTE will be left in REG1.  REG3 and REG4 must
	 * be an even/odd pair of registers.
	 *
	 * VADDR and TAG will be preserved and not clobbered by this macro.
	 */
	/* XXX non-8K base page size support... */
#define KERN_TSB_LOOKUP_TL1(VADDR, TAG, REG1, REG2, REG3, REG4, OK_LABEL) \
	sethi		%hi(swapper_tsb), REG1; \
	or		REG1, %lo(swapper_tsb), REG1; \
	srlx		VADDR, 13, REG2; \
	and		REG2, (512 - 1), REG2; \
	sllx		REG2, 4, REG2; \
	add		REG1, REG2, REG2; \
	ldda		[REG2] ASI_NUCLEUS_QUAD_LDD, REG3; \
	cmp		REG3, TAG; \
	be,a,pt		%xcc, OK_LABEL; \
	 mov		REG4, REG1;

#endif /* !(_SPARC64_TSB_H) */
