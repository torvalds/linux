/*-
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	BSDI $Id: locore.s,v 1.36.2.15 1999/08/23 22:34:41 cp Exp $
 */
/*-
 * Copyright (c) 2001 Jake Burkholder.
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
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_kstack_pages.h"

#include <machine/asi.h>
#include <machine/asmacros.h>
#include <machine/frame.h>
#include <machine/fsr.h>
#include <machine/intr_machdep.h>
#include <machine/ktr.h>
#include <machine/pcb.h>
#include <machine/pstate.h>
#include <machine/trap.h>
#include <machine/tsb.h>
#include <machine/tstate.h>
#include <machine/utrap.h>
#include <machine/wstate.h>

#include "assym.inc"

#define	TSB_ASI			0x0
#define	TSB_KERNEL		0x0
#define	TSB_KERNEL_MASK		0x0
#define	TSB_KERNEL_PHYS		0x0
#define	TSB_KERNEL_PHYS_END	0x0
#define	TSB_QUAD_LDD		0x0

	.register %g2,#ignore
	.register %g3,#ignore
	.register %g6,#ignore
	.register %g7,#ignore

/*
 * Atomically set a bit in a TTE.
 */
#define	TTE_SET_BIT(r1, r2, r3, bit, a, asi) \
	add	r1, TTE_DATA, r1 ; \
	LD(x, a) [r1] asi, r2 ; \
9:	or	r2, bit, r3 ; \
	CAS(x, a) [r1] asi, r2, r3 ; \
	cmp	r2, r3 ; \
	bne,pn	%xcc, 9b ; \
	 mov	r3, r2

#define	TTE_SET_REF(r1, r2, r3, a, asi)	TTE_SET_BIT(r1, r2, r3, TD_REF, a, asi)
#define	TTE_SET_W(r1, r2, r3, a, asi)	TTE_SET_BIT(r1, r2, r3, TD_W, a, asi)

/*
 * Macros for spilling and filling live windows.
 *
 * NOTE: These macros use exactly 16 instructions, and it is assumed that the
 * handler will not use more than 24 instructions total, to leave room for
 * resume vectors which occupy the last 8 instructions.
 */

#define	SPILL(storer, base, size, asi) \
	storer	%l0, [base + (0 * size)] asi ; \
	storer	%l1, [base + (1 * size)] asi ; \
	storer	%l2, [base + (2 * size)] asi ; \
	storer	%l3, [base + (3 * size)] asi ; \
	storer	%l4, [base + (4 * size)] asi ; \
	storer	%l5, [base + (5 * size)] asi ; \
	storer	%l6, [base + (6 * size)] asi ; \
	storer	%l7, [base + (7 * size)] asi ; \
	storer	%i0, [base + (8 * size)] asi ; \
	storer	%i1, [base + (9 * size)] asi ; \
	storer	%i2, [base + (10 * size)] asi ; \
	storer	%i3, [base + (11 * size)] asi ; \
	storer	%i4, [base + (12 * size)] asi ; \
	storer	%i5, [base + (13 * size)] asi ; \
	storer	%i6, [base + (14 * size)] asi ; \
	storer	%i7, [base + (15 * size)] asi

#define	FILL(loader, base, size, asi) \
	loader	[base + (0 * size)] asi, %l0 ; \
	loader	[base + (1 * size)] asi, %l1 ; \
	loader	[base + (2 * size)] asi, %l2 ; \
	loader	[base + (3 * size)] asi, %l3 ; \
	loader	[base + (4 * size)] asi, %l4 ; \
	loader	[base + (5 * size)] asi, %l5 ; \
	loader	[base + (6 * size)] asi, %l6 ; \
	loader	[base + (7 * size)] asi, %l7 ; \
	loader	[base + (8 * size)] asi, %i0 ; \
	loader	[base + (9 * size)] asi, %i1 ; \
	loader	[base + (10 * size)] asi, %i2 ; \
	loader	[base + (11 * size)] asi, %i3 ; \
	loader	[base + (12 * size)] asi, %i4 ; \
	loader	[base + (13 * size)] asi, %i5 ; \
	loader	[base + (14 * size)] asi, %i6 ; \
	loader	[base + (15 * size)] asi, %i7

#define	ERRATUM50(reg)	mov reg, reg

#define	KSTACK_SLOP	1024

/*
 * Sanity check the kernel stack and bail out if it's wrong.
 * XXX: doesn't handle being on the panic stack.
 */
#define	KSTACK_CHECK \
	dec	16, ASP_REG ; \
	stx	%g1, [ASP_REG + 0] ; \
	stx	%g2, [ASP_REG + 8] ; \
	add	%sp, SPOFF, %g1 ; \
	andcc	%g1, (1 << PTR_SHIFT) - 1, %g0 ; \
	bnz,a	%xcc, tl1_kstack_fault ; \
	 inc	16, ASP_REG ; \
	ldx	[PCPU(CURTHREAD)], %g2 ; \
	ldx	[%g2 + TD_KSTACK], %g2 ; \
	add	%g2, KSTACK_SLOP, %g2 ; \
	subcc	%g1, %g2, %g1 ; \
	ble,a	%xcc, tl1_kstack_fault ; \
	 inc	16, ASP_REG ; \
	set	KSTACK_PAGES * PAGE_SIZE, %g2 ; \
	cmp	%g1, %g2 ; \
	bgt,a	%xcc, tl1_kstack_fault ; \
	 inc	16, ASP_REG ; \
	ldx	[ASP_REG + 8], %g2 ; \
	ldx	[ASP_REG + 0], %g1 ; \
	inc	16, ASP_REG

	.globl	tl_text_begin
tl_text_begin:
	nop

ENTRY(tl1_kstack_fault)
	rdpr	%tl, %g1
1:	cmp	%g1, 2
	be,a	2f
	 nop

#if KTR_COMPILE & KTR_TRAP
	CATR(KTR_TRAP, "tl1_kstack_fault: tl=%#lx tpc=%#lx tnpc=%#lx"
	    , %g2, %g3, %g4, 7, 8, 9)
	rdpr	%tl, %g3
	stx	%g3, [%g2 + KTR_PARM1]
	rdpr	%tpc, %g3
	stx	%g3, [%g2 + KTR_PARM1]
	rdpr	%tnpc, %g3
	stx	%g3, [%g2 + KTR_PARM1]
9:
#endif

	sub	%g1, 1, %g1
	wrpr	%g1, 0, %tl
	ba,a	%xcc, 1b
	 nop

2:
#if KTR_COMPILE & KTR_TRAP
	CATR(KTR_TRAP,
	    "tl1_kstack_fault: sp=%#lx ks=%#lx cr=%#lx cs=%#lx ow=%#lx ws=%#lx"
	    , %g1, %g2, %g3, 7, 8, 9)
	add	%sp, SPOFF, %g2
	stx	%g2, [%g1 + KTR_PARM1]
	ldx	[PCPU(CURTHREAD)], %g2
	ldx	[%g2 + TD_KSTACK], %g2
	stx	%g2, [%g1 + KTR_PARM2]
	rdpr	%canrestore, %g2
	stx	%g2, [%g1 + KTR_PARM3]
	rdpr	%cansave, %g2
	stx	%g2, [%g1 + KTR_PARM4]
	rdpr	%otherwin, %g2
	stx	%g2, [%g1 + KTR_PARM5]
	rdpr	%wstate, %g2
	stx	%g2, [%g1 + KTR_PARM6]
9:
#endif

	wrpr	%g0, 0, %canrestore
	wrpr	%g0, 6, %cansave
	wrpr	%g0, 0, %otherwin
	wrpr	%g0, WSTATE_KERNEL, %wstate

	sub	ASP_REG, SPOFF + CCFSZ, %sp
	clr	%fp

	set	trap, %o2
	ba	%xcc, tl1_trap
	 mov	T_KSTACK_FAULT | T_KERNEL, %o0
END(tl1_kstack_fault)

/*
 * Magic to resume from a spill or fill trap.  If we get an alignment or an
 * MMU fault during a spill or a fill, this macro will detect the fault and
 * resume at a set instruction offset in the trap handler.
 *
 * To check if the previous trap was a spill/fill we convert the trapped pc
 * to a trap type and verify that it is in the range of spill/fill vectors.
 * The spill/fill vectors are types 0x80-0xff and 0x280-0x2ff, masking off the
 * tl bit allows us to detect both ranges with one test.
 *
 * This is:
 *	0x80 <= (((%tpc - %tba) >> 5) & ~0x200) < 0x100
 *
 * To calculate the new pc we take advantage of the xor feature of wrpr.
 * Forcing all the low bits of the trapped pc on we can produce any offset
 * into the spill/fill vector.  The size of a spill/fill trap vector is 0x80.
 *
 *	0x7f ^ 0x1f == 0x60
 *	0x1f == (0x80 - 0x60) - 1
 *
 * Which are the offset and xor value used to resume from alignment faults.
 */

/*
 * Determine if we have trapped inside of a spill/fill vector, and if so resume
 * at a fixed instruction offset in the trap vector.  Must be called on
 * alternate globals.
 */
#define	RESUME_SPILLFILL_MAGIC(stxa_g0_sfsr, xor) \
	dec	16, ASP_REG ; \
	stx	%g1, [ASP_REG + 0] ; \
	stx	%g2, [ASP_REG + 8] ; \
	rdpr	%tpc, %g1 ; \
	ERRATUM50(%g1) ; \
	rdpr	%tba, %g2 ; \
	sub	%g1, %g2, %g2 ; \
	srlx	%g2, 5, %g2 ; \
	andn	%g2, 0x200, %g2 ; \
	cmp	%g2, 0x80 ; \
	blu,pt	%xcc, 9f ; \
	 cmp	%g2, 0x100 ; \
	bgeu,pt	%xcc, 9f ; \
	 or	%g1, 0x7f, %g1 ; \
	wrpr	%g1, xor, %tnpc ; \
	stxa_g0_sfsr ; \
	ldx	[ASP_REG + 8], %g2 ; \
	ldx	[ASP_REG + 0], %g1 ; \
	inc	16, ASP_REG ; \
	done ; \
9:	ldx	[ASP_REG + 8], %g2 ; \
	ldx	[ASP_REG + 0], %g1 ; \
	inc	16, ASP_REG

/*
 * For certain faults we need to clear the SFSR MMU register before returning.
 */
#define	RSF_CLR_SFSR \
	wr	%g0, ASI_DMMU, %asi ; \
	stxa	%g0, [%g0 + AA_DMMU_SFSR] %asi

#define	RSF_XOR(off)	((0x80 - off) - 1)

/*
 * Instruction offsets in spill and fill trap handlers for handling certain
 * nested traps, and corresponding xor constants for wrpr.
 */
#define	RSF_OFF_ALIGN	0x60
#define	RSF_OFF_MMU	0x70

#define	RESUME_SPILLFILL_ALIGN \
	RESUME_SPILLFILL_MAGIC(RSF_CLR_SFSR, RSF_XOR(RSF_OFF_ALIGN))
#define	RESUME_SPILLFILL_MMU \
	RESUME_SPILLFILL_MAGIC(EMPTY, RSF_XOR(RSF_OFF_MMU))
#define	RESUME_SPILLFILL_MMU_CLR_SFSR \
	RESUME_SPILLFILL_MAGIC(RSF_CLR_SFSR, RSF_XOR(RSF_OFF_MMU))

/*
 * Constant to add to %tnpc when taking a fill trap just before returning to
 * user mode.
 */
#define	RSF_FILL_INC	tl0_ret_fill_end - tl0_ret_fill

/*
 * Generate a T_SPILL or T_FILL trap if the window operation fails.
 */
#define	RSF_TRAP(type) \
	ba	%xcc, tl0_sftrap ; \
	 mov	type, %g2 ; \
	.align	16

/*
 * Game over if the window operation fails.
 */
#define	RSF_FATAL(type) \
	ba	%xcc, rsf_fatal ; \
	 mov	type, %g2 ; \
	.align	16

/*
 * Magic to resume from a failed fill a few instructions after the corrsponding
 * restore.  This is used on return from the kernel to usermode.
 */
#define	RSF_FILL_MAGIC \
	rdpr	%tnpc, %g1 ; \
	add	%g1, RSF_FILL_INC, %g1 ; \
	wrpr	%g1, 0, %tnpc ; \
	done ; \
	.align	16

/*
 * Spill to the pcb if a spill to the user stack in kernel mode fails.
 */
#define	RSF_SPILL_TOPCB \
	ba,a	%xcc, tl1_spill_topcb ; \
	 nop ; \
	.align	16

ENTRY(rsf_fatal)
#if KTR_COMPILE & KTR_TRAP
	CATR(KTR_TRAP, "rsf_fatal: bad window trap tt=%#lx type=%#lx"
	    , %g1, %g3, %g4, 7, 8, 9)
	rdpr	%tt, %g3
	stx	%g3, [%g1 + KTR_PARM1]
	stx	%g2, [%g1 + KTR_PARM2]
9:
#endif

	KSTACK_CHECK

	sir
END(rsf_fatal)

	.data
	_ALIGN_DATA
	.globl	intrnames, sintrnames
intrnames:
	.space	(IV_MAX + PIL_MAX) * (MAXCOMLEN + 1)
sintrnames:
	.quad	(IV_MAX + PIL_MAX) * (MAXCOMLEN + 1)

	.globl	intrcnt, sintrcnt
intrcnt:
	.space	(IV_MAX + PIL_MAX) * 8
sintrcnt:
	.quad	(IV_MAX + PIL_MAX) * 8

	.text

/*
 * Trap table and associated macros
 *
 * Due to its size a trap table is an inherently hard thing to represent in
 * code in a clean way.  There are approximately 1024 vectors, of 8 or 32
 * instructions each, many of which are identical.  The way that this is
 * laid out is the instructions (8 or 32) for the actual trap vector appear
 * as an AS macro.  In general this code branches to tl0_trap or tl1_trap,
 * but if not supporting code can be placed just after the definition of the
 * macro.  The macros are then instantiated in a different section (.trap),
 * which is setup to be placed by the linker at the beginning of .text, and the
 * code around the macros is moved to the end of trap table.  In this way the
 * code that must be sequential in memory can be split up, and located near
 * its supporting code so that it is easier to follow.
 */

	/*
	 * Clean window traps occur when %cleanwin is zero to ensure that data
	 * is not leaked between address spaces in registers.
	 */
	.macro	clean_window
	clr	%o0
	clr	%o1
	clr	%o2
	clr	%o3
	clr	%o4
	clr	%o5
	clr	%o6
	clr	%o7
	clr	%l0
	clr	%l1
	clr	%l2
	clr	%l3
	clr	%l4
	clr	%l5
	clr	%l6
	rdpr	%cleanwin, %l7
	inc	%l7
	wrpr	%l7, 0, %cleanwin
	clr	%l7
	retry
	.align	128
	.endm

	/*
	 * Stack fixups for entry from user mode.  We are still running on the
	 * user stack, and with its live registers, so we must save soon.  We
	 * are on alternate globals so we do have some registers.  Set the
	 * transitional window state, and do the save.  If this traps we
	 * attempt to spill a window to the user stack.  If this fails, we
	 * spill the window to the pcb and continue.  Spilling to the pcb
	 * must not fail.
	 *
	 * NOTE: Must be called with alternate globals and clobbers %g1.
	 */

	.macro	tl0_split
	rdpr	%wstate, %g1
	wrpr	%g1, WSTATE_TRANSITION, %wstate
	save
	.endm

	.macro	tl0_setup	type
	tl0_split
	clr	%o1
	set	trap, %o2
	ba	%xcc, tl0_utrap
	 mov	\type, %o0
	.endm

	/*
	 * Generic trap type.  Call trap() with the specified type.
	 */
	.macro	tl0_gen		type
	tl0_setup \type
	.align	32
	.endm

	/*
	 * This is used to suck up the massive swaths of reserved trap types.
	 * Generates count "reserved" trap vectors.
	 */
	.macro	tl0_reserved	count
	.rept	\count
	tl0_gen	T_RESERVED
	.endr
	.endm

	.macro	tl1_split
	rdpr	%wstate, %g1
	wrpr	%g1, WSTATE_NESTED, %wstate
	save	%sp, -(CCFSZ + TF_SIZEOF), %sp
	.endm

	.macro	tl1_setup	type
	tl1_split
	clr	%o1
	set	trap, %o2
	ba	%xcc, tl1_trap
	 mov	\type | T_KERNEL, %o0
	.endm

	.macro	tl1_gen		type
	tl1_setup \type
	.align	32
	.endm

	.macro	tl1_reserved	count
	.rept	\count
	tl1_gen	T_RESERVED
	.endr
	.endm

	.macro	tl0_insn_excptn
	wrpr	%g0, PSTATE_ALT, %pstate
	wr	%g0, ASI_IMMU, %asi
	rdpr	%tpc, %g3
	ldxa	[%g0 + AA_IMMU_SFSR] %asi, %g4
	/*
	 * XXX in theory, a store to AA_IMMU_SFSR must be immediately
	 * followed by a DONE, FLUSH or RETRY for USIII.  In practice,
	 * this triggers a RED state exception though.
	 */
	stxa	%g0, [%g0 + AA_IMMU_SFSR] %asi
	membar	#Sync
	ba	%xcc, tl0_sfsr_trap
	 mov	T_INSTRUCTION_EXCEPTION, %g2
	.align	32
	.endm

	.macro	tl0_data_excptn
	wrpr	%g0, PSTATE_ALT, %pstate
	wr	%g0, ASI_DMMU, %asi
	ldxa	[%g0 + AA_DMMU_SFAR] %asi, %g3
	ldxa	[%g0 + AA_DMMU_SFSR] %asi, %g4
	stxa	%g0, [%g0 + AA_DMMU_SFSR] %asi
	membar	#Sync
	ba	%xcc, tl0_sfsr_trap
	 mov	T_DATA_EXCEPTION, %g2
	.align	32
	.endm

	.macro	tl0_align
	wr	%g0, ASI_DMMU, %asi
	ldxa	[%g0 + AA_DMMU_SFAR] %asi, %g3
	ldxa	[%g0 + AA_DMMU_SFSR] %asi, %g4
	stxa	%g0, [%g0 + AA_DMMU_SFSR] %asi
	membar	#Sync
	ba	%xcc, tl0_sfsr_trap
	 mov	T_MEM_ADDRESS_NOT_ALIGNED, %g2
	.align	32
	.endm

ENTRY(tl0_sfsr_trap)
	tl0_split
	clr	%o1
	set	trap, %o2
	mov	%g3, %o4
	mov	%g4, %o5
	ba	%xcc, tl0_utrap
	 mov	%g2, %o0
END(tl0_sfsr_trap)

	.macro	tl0_intr level, mask
	tl0_split
	set	\mask, %o1
	ba	%xcc, tl0_intr
	 mov	\level, %o0
	.align	32
	.endm

#define	INTR(level, traplvl)						\
	tl ## traplvl ## _intr	level, 1 << level

#define	TICK(traplvl) \
	tl ## traplvl ## _intr	PIL_TICK, 0x10001

#define	INTR_LEVEL(tl)							\
	INTR(1, tl) ;							\
	INTR(2, tl) ;							\
	INTR(3, tl) ;							\
	INTR(4, tl) ;							\
	INTR(5, tl) ;							\
	INTR(6, tl) ;							\
	INTR(7, tl) ;							\
	INTR(8, tl) ;							\
	INTR(9, tl) ;							\
	INTR(10, tl) ;							\
	INTR(11, tl) ;							\
	INTR(12, tl) ;							\
	INTR(13, tl) ;							\
	TICK(tl) ;							\
	INTR(15, tl) ;

	.macro	tl0_intr_level
	INTR_LEVEL(0)
	.endm

	.macro	intr_vector
	ldxa	[%g0] ASI_INTR_RECEIVE, %g1
	andcc	%g1, IRSR_BUSY, %g0
	bnz,a,pt %xcc, intr_vector
	 nop
	ba,a,pt	%xcc, intr_vector_stray
	 nop
	.align	32
	.endm

	.macro	tl0_immu_miss
	/*
	 * Load the context and the virtual page number from the tag access
	 * register.  We ignore the context.
	 */
	wr	%g0, ASI_IMMU, %asi
	ldxa	[%g0 + AA_IMMU_TAR] %asi, %g1

	/*
	 * Initialize the page size walker.
	 */
	mov	TS_MIN, %g2

	/*
	 * Loop over all supported page sizes.
	 */

	/*
	 * Compute the page shift for the page size we are currently looking
	 * for.
	 */
1:	add	%g2, %g2, %g3
	add	%g3, %g2, %g3
	add	%g3, PAGE_SHIFT, %g3

	/*
	 * Extract the virtual page number from the contents of the tag
	 * access register.
	 */
	srlx	%g1, %g3, %g3

	/*
	 * Compute the TTE bucket address.
	 */
	ldxa	[%g0 + AA_IMMU_TSB] %asi, %g5
	and	%g3, TSB_BUCKET_MASK, %g4
	sllx	%g4, TSB_BUCKET_SHIFT + TTE_SHIFT, %g4
	add	%g4, %g5, %g4

	/*
	 * Compute the TTE tag target.
	 */
	sllx	%g3, TV_SIZE_BITS, %g3
	or	%g3, %g2, %g3

	/*
	 * Loop over the TTEs in this bucket.
	 */

	/*
	 * Load the TTE.  Note that this instruction may fault, clobbering
	 * the contents of the tag access register, %g5, %g6, and %g7.  We
	 * do not use %g5, and %g6 and %g7 are not used until this instruction
	 * completes successfully.
	 */
2:	ldda	[%g4] ASI_NUCLEUS_QUAD_LDD, %g6 /*, %g7 */

	/*
	 * Check that it's valid and executable and that the TTE tags match.
	 */
	brgez,pn %g7, 3f
	 andcc	%g7, TD_EXEC, %g0
	bz,pn	%xcc, 3f
	 cmp	%g3, %g6
	bne,pn	%xcc, 3f
	 EMPTY

	/*
	 * We matched a TTE, load the TLB.
	 */

	/*
	 * Set the reference bit, if it's currently clear.
	 */
	 andcc	%g7, TD_REF, %g0
	bz,a,pn	%xcc, tl0_immu_miss_set_ref
	 nop

	/*
	 * Load the TTE tag and data into the TLB and retry the instruction.
	 */
	stxa	%g1, [%g0 + AA_IMMU_TAR] %asi
	stxa	%g7, [%g0] ASI_ITLB_DATA_IN_REG
	retry

	/*
	 * Advance to the next TTE in this bucket, and check the low bits
	 * of the bucket pointer to see if we've finished the bucket.
	 */
3:	add	%g4, 1 << TTE_SHIFT, %g4
	andcc	%g4, (1 << (TSB_BUCKET_SHIFT + TTE_SHIFT)) - 1, %g0
	bnz,pt	%xcc, 2b
	 EMPTY

	/*
	 * See if we just checked the largest page size, and advance to the
	 * next one if not.
	 */
	 cmp	%g2, TS_MAX
	bne,pt	%xcc, 1b
	 add	%g2, 1, %g2

	/*
	 * Not in user TSB, call C code.
	 */
	ba,a	%xcc, tl0_immu_miss_trap
	.align	128
	.endm

ENTRY(tl0_immu_miss_set_ref)
	/*
	 * Set the reference bit.
	 */
	TTE_SET_REF(%g4, %g2, %g3, a, ASI_N)

	/*
	 * May have become invalid during casxa, in which case start over.
	 */
	brgez,pn %g2, 1f
	 nop

	/*
	 * Load the TTE tag and data into the TLB and retry the instruction.
	 */
	stxa	%g1, [%g0 + AA_IMMU_TAR] %asi
	stxa	%g2, [%g0] ASI_ITLB_DATA_IN_REG
1:	retry
END(tl0_immu_miss_set_ref)

ENTRY(tl0_immu_miss_trap)
	/*
	 * Put back the contents of the tag access register, in case we
	 * faulted.
	 */
	sethi	%hi(KERNBASE), %g2
	stxa	%g1, [%g0 + AA_IMMU_TAR] %asi
	flush	%g2

	/*
	 * Switch to alternate globals.
	 */
	wrpr	%g0, PSTATE_ALT, %pstate

	/*
	 * Reload the tag access register.
	 */
	ldxa	[%g0 + AA_IMMU_TAR] %asi, %g2

	/*
	 * Save the tag access register, and call common trap code.
	 */
	tl0_split
	clr	%o1
	set	trap, %o2
	mov	%g2, %o3
	ba	%xcc, tl0_utrap
	 mov	T_INSTRUCTION_MISS, %o0
END(tl0_immu_miss_trap)

	.macro	tl0_dmmu_miss
	/*
	 * Load the context and the virtual page number from the tag access
	 * register.  We ignore the context.
	 */
	wr	%g0, ASI_DMMU, %asi
	ldxa	[%g0 + AA_DMMU_TAR] %asi, %g1

	/*
	 * Initialize the page size walker.
	 */
tl1_dmmu_miss_user:
	mov	TS_MIN, %g2

	/*
	 * Loop over all supported page sizes.
	 */

	/*
	 * Compute the page shift for the page size we are currently looking
	 * for.
	 */
1:	add	%g2, %g2, %g3
	add	%g3, %g2, %g3
	add	%g3, PAGE_SHIFT, %g3

	/*
	 * Extract the virtual page number from the contents of the tag
	 * access register.
	 */
	srlx	%g1, %g3, %g3

	/*
	 * Compute the TTE bucket address.
	 */
	ldxa	[%g0 + AA_DMMU_TSB] %asi, %g5
	and	%g3, TSB_BUCKET_MASK, %g4
	sllx	%g4, TSB_BUCKET_SHIFT + TTE_SHIFT, %g4
	add	%g4, %g5, %g4

	/*
	 * Compute the TTE tag target.
	 */
	sllx	%g3, TV_SIZE_BITS, %g3
	or	%g3, %g2, %g3

	/*
	 * Loop over the TTEs in this bucket.
	 */

	/*
	 * Load the TTE.  Note that this instruction may fault, clobbering
	 * the contents of the tag access register, %g5, %g6, and %g7.  We
	 * do not use %g5, and %g6 and %g7 are not used until this instruction
	 * completes successfully.
	 */
2:	ldda	[%g4] ASI_NUCLEUS_QUAD_LDD, %g6 /*, %g7 */

	/*
	 * Check that it's valid and that the virtual page numbers match.
	 */
	brgez,pn %g7, 3f
	 cmp	%g3, %g6
	bne,pn	%xcc, 3f
	 EMPTY

	/*
	 * We matched a TTE, load the TLB.
	 */

	/*
	 * Set the reference bit, if it's currently clear.
	 */
	 andcc	%g7, TD_REF, %g0
	bz,a,pn	%xcc, tl0_dmmu_miss_set_ref
	 nop

	/*
	 * Load the TTE tag and data into the TLB and retry the instruction.
	 */
	stxa	%g1, [%g0 + AA_DMMU_TAR] %asi
	stxa	%g7, [%g0] ASI_DTLB_DATA_IN_REG
	retry

	/*
	 * Advance to the next TTE in this bucket, and check the low bits
	 * of the bucket pointer to see if we've finished the bucket.
	 */
3:	add	%g4, 1 << TTE_SHIFT, %g4
	andcc	%g4, (1 << (TSB_BUCKET_SHIFT + TTE_SHIFT)) - 1, %g0
	bnz,pt	%xcc, 2b
	 EMPTY

	/*
	 * See if we just checked the largest page size, and advance to the
	 * next one if not.
	 */
	 cmp	%g2, TS_MAX
	bne,pt	%xcc, 1b
	 add	%g2, 1, %g2

	/*
	 * Not in user TSB, call C code.
	 */
	ba,a	%xcc, tl0_dmmu_miss_trap
	.align	128
	.endm

ENTRY(tl0_dmmu_miss_set_ref)
	/*
	 * Set the reference bit.
	 */
	TTE_SET_REF(%g4, %g2, %g3, a, ASI_N)

	/*
	 * May have become invalid during casxa, in which case start over.
	 */
	brgez,pn %g2, 1f
	 nop

	/*
	 * Load the TTE tag and data into the TLB and retry the instruction.
	 */
	stxa	%g1, [%g0 + AA_DMMU_TAR] %asi
	stxa	%g2, [%g0] ASI_DTLB_DATA_IN_REG
1:	retry
END(tl0_dmmu_miss_set_ref)

ENTRY(tl0_dmmu_miss_trap)
	/*
	 * Put back the contents of the tag access register, in case we
	 * faulted.
	 */
	stxa	%g1, [%g0 + AA_DMMU_TAR] %asi
	membar	#Sync

	/*
	 * Switch to alternate globals.
	 */
	wrpr	%g0, PSTATE_ALT, %pstate

	/*
	 * Check if we actually came from the kernel.
	 */
	rdpr	%tl, %g1
	cmp	%g1, 1
	bgt,a,pn %xcc, 1f
	 nop

	/*
	 * Reload the tag access register.
	 */
	ldxa	[%g0 + AA_DMMU_TAR] %asi, %g2

	/*
	 * Save the tag access register and call common trap code.
	 */
	tl0_split
	clr	%o1
	set	trap, %o2
	mov	%g2, %o3
	ba	%xcc, tl0_utrap
	 mov	T_DATA_MISS, %o0

	/*
	 * Handle faults during window spill/fill.
	 */
1:	RESUME_SPILLFILL_MMU

	/*
	 * Reload the tag access register.
	 */
	ldxa	[%g0 + AA_DMMU_TAR] %asi, %g2

	tl1_split
	clr	%o1
	set	trap, %o2
	mov	%g2, %o3
	ba	%xcc, tl1_trap
	 mov	T_DATA_MISS | T_KERNEL, %o0
END(tl0_dmmu_miss_trap)

	.macro	tl0_dmmu_prot
	ba,a	%xcc, tl0_dmmu_prot_1
	 nop
	.align	128
	.endm

ENTRY(tl0_dmmu_prot_1)
	/*
	 * Load the context and the virtual page number from the tag access
	 * register.  We ignore the context.
	 */
	wr	%g0, ASI_DMMU, %asi
	ldxa	[%g0 + AA_DMMU_TAR] %asi, %g1

	/*
	 * Initialize the page size walker.
	 */
tl1_dmmu_prot_user:
	mov	TS_MIN, %g2

	/*
	 * Loop over all supported page sizes.
	 */

	/*
	 * Compute the page shift for the page size we are currently looking
	 * for.
	 */
1:	add	%g2, %g2, %g3
	add	%g3, %g2, %g3
	add	%g3, PAGE_SHIFT, %g3

	/*
	 * Extract the virtual page number from the contents of the tag
	 * access register.
	 */
	srlx	%g1, %g3, %g3

	/*
	 * Compute the TTE bucket address.
	 */
	ldxa	[%g0 + AA_DMMU_TSB] %asi, %g5
	and	%g3, TSB_BUCKET_MASK, %g4
	sllx	%g4, TSB_BUCKET_SHIFT + TTE_SHIFT, %g4
	add	%g4, %g5, %g4

	/*
	 * Compute the TTE tag target.
	 */
	sllx	%g3, TV_SIZE_BITS, %g3
	or	%g3, %g2, %g3

	/*
	 * Loop over the TTEs in this bucket.
	 */

	/*
	 * Load the TTE.  Note that this instruction may fault, clobbering
	 * the contents of the tag access register, %g5, %g6, and %g7.  We
	 * do not use %g5, and %g6 and %g7 are not used until this instruction
	 * completes successfully.
	 */
2:	ldda	[%g4] ASI_NUCLEUS_QUAD_LDD, %g6 /*, %g7 */

	/*
	 * Check that it's valid and writable and that the virtual page
	 * numbers match.
	 */
	brgez,pn %g7, 4f
	 andcc	%g7, TD_SW, %g0
	bz,pn	%xcc, 4f
	 cmp	%g3, %g6
	bne,pn	%xcc, 4f
	 nop

	/*
	 * Set the hardware write bit.
	 */
	TTE_SET_W(%g4, %g2, %g3, a, ASI_N)

	/*
	 * Delete the old TLB entry and clear the SFSR.
	 */
	srlx	%g1, PAGE_SHIFT, %g3
	sllx	%g3, PAGE_SHIFT, %g3
	stxa	%g0, [%g3] ASI_DMMU_DEMAP
	stxa	%g0, [%g0 + AA_DMMU_SFSR] %asi
	membar	#Sync

	/*
	 * May have become invalid during casxa, in which case start over.
	 */
	brgez,pn %g2, 3f
	 or	%g2, TD_W, %g2

	/*
	 * Load the TTE data into the TLB and retry the instruction.
	 */
	stxa	%g1, [%g0 + AA_DMMU_TAR] %asi
	stxa	%g2, [%g0] ASI_DTLB_DATA_IN_REG
3:	retry

	/*
	 * Check the low bits to see if we've finished the bucket.
	 */
4:	add	%g4, 1 << TTE_SHIFT, %g4
	andcc	%g4, (1 << (TSB_BUCKET_SHIFT + TTE_SHIFT)) - 1, %g0
	bnz,pt	%xcc, 2b
	 EMPTY

	/*
	 * See if we just checked the largest page size, and advance to the
	 * next one if not.
	 */
	 cmp	%g2, TS_MAX
	bne,pt	%xcc, 1b
	 add	%g2, 1, %g2

	/*
	 * Not in user TSB, call C code.
	 */
	ba,a	%xcc, tl0_dmmu_prot_trap
	 nop
END(tl0_dmmu_prot_1)

ENTRY(tl0_dmmu_prot_trap)
	/*
	 * Put back the contents of the tag access register, in case we
	 * faulted.
	 */
	stxa	%g1, [%g0 + AA_DMMU_TAR] %asi
	membar	#Sync

	/*
	 * Switch to alternate globals.
	 */
	wrpr	%g0, PSTATE_ALT, %pstate

	/*
	 * Check if we actually came from the kernel.
	 */
	rdpr	%tl, %g1
	cmp	%g1, 1
	bgt,a,pn %xcc, 1f
	 nop

	/*
	 * Load the SFAR, SFSR and TAR.
	 */
	ldxa	[%g0 + AA_DMMU_TAR] %asi, %g2
	ldxa	[%g0 + AA_DMMU_SFAR] %asi, %g3
	ldxa	[%g0 + AA_DMMU_SFSR] %asi, %g4
	stxa	%g0, [%g0 + AA_DMMU_SFSR] %asi
	membar	#Sync

	/*
	 * Save the MMU registers and call common trap code.
	 */
	tl0_split
	clr	%o1
	set	trap, %o2
	mov	%g2, %o3
	mov	%g3, %o4
	mov	%g4, %o5
	ba	%xcc, tl0_utrap
	 mov	T_DATA_PROTECTION, %o0

	/*
	 * Handle faults during window spill/fill.
	 */
1:	RESUME_SPILLFILL_MMU_CLR_SFSR

	/*
	 * Load the SFAR, SFSR and TAR.  Clear the SFSR.
	 */
	ldxa	[%g0 + AA_DMMU_TAR] %asi, %g2
	ldxa	[%g0 + AA_DMMU_SFAR] %asi, %g3
	ldxa	[%g0 + AA_DMMU_SFSR] %asi, %g4
	stxa	%g0, [%g0 + AA_DMMU_SFSR] %asi
	membar	#Sync

	tl1_split
	clr	%o1
	set	trap, %o2
	mov	%g2, %o3
	mov	%g3, %o4
	mov	%g4, %o5
	ba	%xcc, tl1_trap
	 mov	T_DATA_PROTECTION | T_KERNEL, %o0
END(tl0_dmmu_prot_trap)

	.macro	tl0_spill_0_n
	wr	%g0, ASI_AIUP, %asi
	SPILL(stxa, %sp + SPOFF, 8, %asi)
	saved
	retry
	.align	32
	RSF_TRAP(T_SPILL)
	RSF_TRAP(T_SPILL)
	.endm

	.macro	tl0_spill_1_n
	wr	%g0, ASI_AIUP, %asi
	SPILL(stwa, %sp, 4, %asi)
	saved
	retry
	.align	32
	RSF_TRAP(T_SPILL)
	RSF_TRAP(T_SPILL)
	.endm

	.macro	tl0_fill_0_n
	wr	%g0, ASI_AIUP, %asi
	FILL(ldxa, %sp + SPOFF, 8, %asi)
	restored
	retry
	.align	32
	RSF_TRAP(T_FILL)
	RSF_TRAP(T_FILL)
	.endm

	.macro	tl0_fill_1_n
	wr	%g0, ASI_AIUP, %asi
	FILL(lduwa, %sp, 4, %asi)
	restored
	retry
	.align	32
	RSF_TRAP(T_FILL)
	RSF_TRAP(T_FILL)
	.endm

ENTRY(tl0_sftrap)
	rdpr	%tstate, %g1
	and	%g1, TSTATE_CWP_MASK, %g1
	wrpr	%g1, 0, %cwp
	tl0_split
	clr	%o1
	set	trap, %o2
	ba	%xcc, tl0_trap
	 mov	%g2, %o0
END(tl0_sftrap)

	.macro	tl0_spill_bad	count
	.rept	\count
	sir
	.align	128
	.endr
	.endm

	.macro	tl0_fill_bad	count
	.rept	\count
	sir
	.align	128
	.endr
	.endm

	.macro	tl0_syscall
	tl0_split
	clr	%o1
	set	syscall, %o2
	ba	%xcc, tl0_trap
	 mov	T_SYSCALL, %o0
	.align	32
	.endm

	.macro	tl0_fp_restore
	ba,a	%xcc, tl0_fp_restore
	 nop
	.align	32
	.endm

ENTRY(tl0_fp_restore)
	ldx	[PCB_REG + PCB_FLAGS], %g1
	andn	%g1, PCB_FEF, %g1
	stx	%g1, [PCB_REG + PCB_FLAGS]

	wr	%g0, FPRS_FEF, %fprs
	wr	%g0, ASI_BLK_S, %asi
	ldda	[PCB_REG + PCB_UFP + (0 * 64)] %asi, %f0
	ldda	[PCB_REG + PCB_UFP + (1 * 64)] %asi, %f16
	ldda	[PCB_REG + PCB_UFP + (2 * 64)] %asi, %f32
	ldda	[PCB_REG + PCB_UFP + (3 * 64)] %asi, %f48
	membar	#Sync
	done
END(tl0_fp_restore)

	.macro	tl1_insn_excptn
	wrpr	%g0, PSTATE_ALT, %pstate
	wr	%g0, ASI_IMMU, %asi
	rdpr	%tpc, %g3
	ldxa	[%g0 + AA_IMMU_SFSR] %asi, %g4
	/*
	 * XXX in theory, a store to AA_IMMU_SFSR must be immediately
	 * followed by a DONE, FLUSH or RETRY for USIII.  In practice,
	 * this triggers a RED state exception though.
	 */
	stxa	%g0, [%g0 + AA_IMMU_SFSR] %asi
	membar	#Sync
	ba	%xcc, tl1_insn_exceptn_trap
	 mov	T_INSTRUCTION_EXCEPTION | T_KERNEL, %g2
	.align	32
	.endm

ENTRY(tl1_insn_exceptn_trap)
	tl1_split
	clr	%o1
	set	trap, %o2
	mov	%g3, %o4
	mov	%g4, %o5
	ba	%xcc, tl1_trap
	 mov	%g2, %o0
END(tl1_insn_exceptn_trap)

	.macro	tl1_fp_disabled
	ba,a	%xcc, tl1_fp_disabled_1
	 nop
	.align	32
	.endm

ENTRY(tl1_fp_disabled_1)
	rdpr	%tpc, %g1
	set	fpu_fault_begin, %g2
	sub	%g1, %g2, %g1
	cmp	%g1, fpu_fault_size
	bgeu,a,pn %xcc, 1f
	 nop

	wr	%g0, FPRS_FEF, %fprs
	wr	%g0, ASI_BLK_S, %asi
	ldda	[PCB_REG + PCB_KFP + (0 * 64)] %asi, %f0
	ldda	[PCB_REG + PCB_KFP + (1 * 64)] %asi, %f16
	ldda	[PCB_REG + PCB_KFP + (2 * 64)] %asi, %f32
	ldda	[PCB_REG + PCB_KFP + (3 * 64)] %asi, %f48
	membar	#Sync
	retry

1:	tl1_split
	clr	%o1
	set	trap, %o2
	ba	%xcc, tl1_trap
	 mov	T_FP_DISABLED | T_KERNEL, %o0
END(tl1_fp_disabled_1)

	.macro	tl1_data_excptn
	wrpr	%g0, PSTATE_ALT, %pstate
	ba,a	%xcc, tl1_data_excptn_trap
	 nop
	.align	32
	.endm

ENTRY(tl1_data_excptn_trap)
	RESUME_SPILLFILL_MMU_CLR_SFSR
	ba	%xcc, tl1_sfsr_trap
	 mov	T_DATA_EXCEPTION | T_KERNEL, %g2
END(tl1_data_excptn_trap)

	.macro	tl1_align
	wrpr	%g0, PSTATE_ALT, %pstate
	ba,a	%xcc, tl1_align_trap
	 nop
	.align	32
	.endm

ENTRY(tl1_align_trap)
	RESUME_SPILLFILL_ALIGN
	ba	%xcc, tl1_sfsr_trap
	 mov	T_MEM_ADDRESS_NOT_ALIGNED | T_KERNEL, %g2
END(tl1_align_trap)

ENTRY(tl1_sfsr_trap)
	wr	%g0, ASI_DMMU, %asi
	ldxa	[%g0 + AA_DMMU_SFAR] %asi, %g3
	ldxa	[%g0 + AA_DMMU_SFSR] %asi, %g4
	stxa	%g0, [%g0 + AA_DMMU_SFSR] %asi
	membar	#Sync

	tl1_split
	clr	%o1
	set	trap, %o2
	mov	%g3, %o4
	mov	%g4, %o5
	ba	%xcc, tl1_trap
	 mov	%g2, %o0
END(tl1_sfsr_trap)

	.macro	tl1_intr level, mask
	tl1_split
	set	\mask, %o1
	ba	%xcc, tl1_intr
	 mov	\level, %o0
	.align	32
	.endm

	.macro	tl1_intr_level
	INTR_LEVEL(1)
	.endm

	.macro	tl1_immu_miss
	/*
	 * Load the context and the virtual page number from the tag access
	 * register.  We ignore the context.
	 */
	wr	%g0, ASI_IMMU, %asi
	ldxa	[%g0 + AA_IMMU_TAR] %asi, %g5

	/*
	 * Compute the address of the TTE.  The TSB mask and address of the
	 * TSB are patched at startup.
	 */
	.globl	tl1_immu_miss_patch_tsb_1
tl1_immu_miss_patch_tsb_1:
	sethi	%uhi(TSB_KERNEL), %g6
	or	%g6, %ulo(TSB_KERNEL), %g6
	sllx	%g6, 32, %g6
	sethi	%hi(TSB_KERNEL), %g7
	or	%g7, %g6, %g7
	.globl	tl1_immu_miss_patch_tsb_mask_1
tl1_immu_miss_patch_tsb_mask_1:
	sethi	%hi(TSB_KERNEL_MASK), %g6
	or	%g6, %lo(TSB_KERNEL_MASK), %g6

	srlx	%g5, TAR_VPN_SHIFT, %g5
	and	%g5, %g6, %g6
	sllx	%g6, TTE_SHIFT, %g6
	add	%g6, %g7, %g6

	/*
	 * Load the TTE.
	 */
	.globl	tl1_immu_miss_patch_quad_ldd_1
tl1_immu_miss_patch_quad_ldd_1:
	ldda	[%g6] TSB_QUAD_LDD, %g6 /*, %g7 */

	/*
	 * Check that it's valid and executable and that the virtual page
	 * numbers match.
	 */
	brgez,pn %g7, tl1_immu_miss_trap
	 andcc	%g7, TD_EXEC, %g0
	bz,pn	%xcc, tl1_immu_miss_trap
	 srlx	%g6, TV_SIZE_BITS, %g6
	cmp	%g5, %g6
	bne,pn	%xcc, tl1_immu_miss_trap
	 EMPTY

	/*
	 * Set the reference bit if it's currently clear.
	 */
	 andcc	%g7, TD_REF, %g0
	bz,a,pn	%xcc, tl1_immu_miss_set_ref
	 nop

	/*
	 * Load the TTE data into the TLB and retry the instruction.
	 */
	stxa	%g7, [%g0] ASI_ITLB_DATA_IN_REG
	retry
	.align	128
	.endm

ENTRY(tl1_immu_miss_set_ref)
	/*
	 * Recompute the TTE address, which we clobbered loading the TTE.
	 * The TSB mask and address of the TSB are patched at startup.
	 */
	.globl	tl1_immu_miss_patch_tsb_2
tl1_immu_miss_patch_tsb_2:
	sethi	%uhi(TSB_KERNEL), %g6
	or	%g6, %ulo(TSB_KERNEL), %g6
	sllx	%g6, 32, %g6
	sethi	%hi(TSB_KERNEL), %g7
	or	%g7, %g6, %g7
	.globl	tl1_immu_miss_patch_tsb_mask_2
tl1_immu_miss_patch_tsb_mask_2:
	sethi	%hi(TSB_KERNEL_MASK), %g6
	or	%g6, %lo(TSB_KERNEL_MASK), %g6

	and	%g5, %g6, %g5
	sllx	%g5, TTE_SHIFT, %g5
	add	%g5, %g7, %g5

	/*
	 * Set the reference bit.
	 */
	.globl	tl1_immu_miss_patch_asi_1
tl1_immu_miss_patch_asi_1:
	wr	%g0, TSB_ASI, %asi
	TTE_SET_REF(%g5, %g6, %g7, a, %asi)

	/*
	 * May have become invalid during casxa, in which case start over.
	 */
	brgez,pn %g6, 1f
	 nop

	/*
	 * Load the TTE data into the TLB and retry the instruction.
	 */
	stxa	%g6, [%g0] ASI_ITLB_DATA_IN_REG
1:	retry
END(tl1_immu_miss_set_ref)

ENTRY(tl1_immu_miss_trap)
	/*
	 * Switch to alternate globals.
	 */
	wrpr	%g0, PSTATE_ALT, %pstate

	ldxa	[%g0 + AA_IMMU_TAR] %asi, %g2

	tl1_split
	clr	%o1
	set	trap, %o2
	mov	%g2, %o3
	ba	%xcc, tl1_trap
	 mov	T_INSTRUCTION_MISS | T_KERNEL, %o0
END(tl1_immu_miss_trap)

	.macro	tl1_dmmu_miss
	/*
	 * Load the context and the virtual page number from the tag access
	 * register.
	 */
	wr	%g0, ASI_DMMU, %asi
	ldxa	[%g0 + AA_DMMU_TAR] %asi, %g5

	/*
	 * Extract the context from the contents of the tag access register.
	 * If it's non-zero this is a fault on a user address.  Note that the
	 * faulting address is passed in %g1.
	 */
	sllx	%g5, 64 - TAR_VPN_SHIFT, %g6
	brnz,a,pn %g6, tl1_dmmu_miss_user
	 mov	%g5, %g1

	/*
	 * Check for the direct mapped physical region.  These addresses have
	 * the high bit set so they are negative.
	 */
	brlz,pn %g5, tl1_dmmu_miss_direct
	 EMPTY

	/*
	 * Compute the address of the TTE.  The TSB mask and address of the
	 * TSB are patched at startup.
	 */
	.globl	tl1_dmmu_miss_patch_tsb_1
tl1_dmmu_miss_patch_tsb_1:
	sethi	%uhi(TSB_KERNEL), %g6
	or	%g6, %ulo(TSB_KERNEL), %g6
	sllx	%g6, 32, %g6
	sethi	%hi(TSB_KERNEL), %g7
	or	%g7, %g6, %g7
	.globl	tl1_dmmu_miss_patch_tsb_mask_1
tl1_dmmu_miss_patch_tsb_mask_1:
	sethi	%hi(TSB_KERNEL_MASK), %g6
	or	%g6, %lo(TSB_KERNEL_MASK), %g6

	srlx	%g5, TAR_VPN_SHIFT, %g5
	and	%g5, %g6, %g6
	sllx	%g6, TTE_SHIFT, %g6
	add	%g6, %g7, %g6

	/*
	 * Load the TTE.
	 */
	.globl	tl1_dmmu_miss_patch_quad_ldd_1
tl1_dmmu_miss_patch_quad_ldd_1:
	ldda	[%g6] TSB_QUAD_LDD, %g6 /*, %g7 */

	/*
	 * Check that it's valid and that the virtual page numbers match.
	 */
	brgez,pn %g7, tl1_dmmu_miss_trap
	 srlx	%g6, TV_SIZE_BITS, %g6
	cmp	%g5, %g6
	bne,pn %xcc, tl1_dmmu_miss_trap
	 EMPTY

	/*
	 * Set the reference bit if it's currently clear.
	 */
	 andcc	%g7, TD_REF, %g0
	bz,a,pt	%xcc, tl1_dmmu_miss_set_ref
	 nop

	/*
	 * Load the TTE data into the TLB and retry the instruction.
	 */
	stxa	%g7, [%g0] ASI_DTLB_DATA_IN_REG
	retry
	.align	128
	.endm

ENTRY(tl1_dmmu_miss_set_ref)
	/*
	 * Recompute the TTE address, which we clobbered loading the TTE.
	 * The TSB mask and address of the TSB are patched at startup.
	 */
	.globl	tl1_dmmu_miss_patch_tsb_mask_2
tl1_dmmu_miss_patch_tsb_2:
	sethi	%uhi(TSB_KERNEL), %g6
	or	%g6, %ulo(TSB_KERNEL), %g6
	sllx	%g6, 32, %g6
	sethi	%hi(TSB_KERNEL), %g7
	or	%g7, %g6, %g7
	.globl	tl1_dmmu_miss_patch_tsb_2
tl1_dmmu_miss_patch_tsb_mask_2:
	sethi	%hi(TSB_KERNEL_MASK), %g6
	or	%g6, %lo(TSB_KERNEL_MASK), %g6

	and	%g5, %g6, %g5
	sllx	%g5, TTE_SHIFT, %g5
	add	%g5, %g7, %g5

	/*
	 * Set the reference bit.
	 */
	.globl	tl1_dmmu_miss_patch_asi_1
tl1_dmmu_miss_patch_asi_1:
	wr	%g0, TSB_ASI, %asi
	TTE_SET_REF(%g5, %g6, %g7, a, %asi)

	/*
	 * May have become invalid during casxa, in which case start over.
	 */
	brgez,pn %g6, 1f
	 nop

	/*
	 * Load the TTE data into the TLB and retry the instruction.
	 */
	stxa	%g6, [%g0] ASI_DTLB_DATA_IN_REG
1:	retry
END(tl1_dmmu_miss_set_ref)

ENTRY(tl1_dmmu_miss_trap)
	/*
	 * Switch to alternate globals.
	 */
	wrpr	%g0, PSTATE_ALT, %pstate

	ldxa	[%g0 + AA_DMMU_TAR] %asi, %g2

	KSTACK_CHECK

	tl1_split
	clr	%o1
	set	trap, %o2
	mov	%g2, %o3
	ba	%xcc, tl1_trap
	 mov	T_DATA_MISS | T_KERNEL, %o0
END(tl1_dmmu_miss_trap)

ENTRY(tl1_dmmu_miss_direct)
	/*
	 * Mask off the high bits of the virtual address to get the physical
	 * address, and or in the TTE bits.  The virtual address bits that
	 * correspond to the TTE valid and page size bits are left set, so
	 * they don't have to be included in the TTE bits below.  We know they
	 * are set because the virtual address is in the upper va hole.
	 * NB: if we are taking advantage of the ASI_ATOMIC_QUAD_LDD_PHYS
	 * and we get a miss on the directly accessed kernel TSB we must not
	 * set TD_CV in order to access it uniformly bypassing the D$.
	 */
	setx	TLB_DIRECT_ADDRESS_MASK, %g7, %g4
	and	%g5, %g4, %g4
	setx	TLB_DIRECT_TO_TTE_MASK, %g7, %g6
	and	%g5, %g6, %g5
	.globl	tl1_dmmu_miss_direct_patch_tsb_phys_1
tl1_dmmu_miss_direct_patch_tsb_phys_1:
	sethi	%uhi(TSB_KERNEL_PHYS), %g3
	or	%g3, %ulo(TSB_KERNEL_PHYS), %g3
	sllx	%g3, 32, %g3
	sethi	%hi(TSB_KERNEL_PHYS), %g3
	or	%g7, %g3, %g7
	cmp	%g4, %g7
	bl,pt	%xcc, 1f
	 or	%g5, TD_CP | TD_W, %g5
	.globl	tl1_dmmu_miss_direct_patch_tsb_phys_end_1
tl1_dmmu_miss_direct_patch_tsb_phys_end_1:
	sethi	%uhi(TSB_KERNEL_PHYS_END), %g3
	or	%g3, %ulo(TSB_KERNEL_PHYS_END), %g3
	sllx	%g3, 32, %g3
	sethi	%hi(TSB_KERNEL_PHYS_END), %g7
	or	%g7, %g3, %g7
	cmp	%g4, %g7
	bg,a,pt	%xcc, 1f
	 nop
	ba,pt	%xcc, 2f
	 nop
1:	or	%g5, TD_CV, %g5

	/*
	 * Load the TTE data into the TLB and retry the instruction.
	 */
2:	stxa	%g5, [%g0] ASI_DTLB_DATA_IN_REG
	retry
END(tl1_dmmu_miss_direct)

	.macro	tl1_dmmu_prot
	ba,a	%xcc, tl1_dmmu_prot_1
	 nop
	.align	128
	.endm

ENTRY(tl1_dmmu_prot_1)
	/*
	 * Load the context and the virtual page number from the tag access
	 * register.
	 */
	wr	%g0, ASI_DMMU, %asi
	ldxa	[%g0 + AA_DMMU_TAR] %asi, %g5

	/*
	 * Extract the context from the contents of the tag access register.
	 * If it's non-zero this is a fault on a user address.  Note that the
	 * faulting address is passed in %g1.
	 */
	sllx	%g5, 64 - TAR_VPN_SHIFT, %g6
	brnz,a,pn %g6, tl1_dmmu_prot_user
	 mov	%g5, %g1

	/*
	 * Compute the address of the TTE.  The TSB mask and address of the
	 * TSB are patched at startup.
	 */
	.globl	tl1_dmmu_prot_patch_tsb_1
tl1_dmmu_prot_patch_tsb_1:
	sethi	%uhi(TSB_KERNEL), %g6
	or	%g6, %ulo(TSB_KERNEL), %g6
	sllx	%g6, 32, %g6
	sethi	%hi(TSB_KERNEL), %g7
	or	%g7, %g6, %g7
	.globl	tl1_dmmu_prot_patch_tsb_mask_1
tl1_dmmu_prot_patch_tsb_mask_1:
	sethi	%hi(TSB_KERNEL_MASK), %g6
	or	%g6, %lo(TSB_KERNEL_MASK), %g6

	srlx	%g5, TAR_VPN_SHIFT, %g5
	and	%g5, %g6, %g6
	sllx	%g6, TTE_SHIFT, %g6
	add	%g6, %g7, %g6

	/*
	 * Load the TTE.
	 */
	.globl	tl1_dmmu_prot_patch_quad_ldd_1
tl1_dmmu_prot_patch_quad_ldd_1:
	ldda	[%g6] TSB_QUAD_LDD, %g6 /*, %g7 */

	/*
	 * Check that it's valid and writeable and that the virtual page
	 * numbers match.
	 */
	brgez,pn %g7, tl1_dmmu_prot_trap
	 andcc	%g7, TD_SW, %g0
	bz,pn	%xcc, tl1_dmmu_prot_trap
	 srlx	%g6, TV_SIZE_BITS, %g6
	cmp	%g5, %g6
	bne,pn	%xcc, tl1_dmmu_prot_trap
	 EMPTY

	/*
	 * Delete the old TLB entry and clear the SFSR.
	 */
	 sllx	%g5, TAR_VPN_SHIFT, %g6
	or	%g6, TLB_DEMAP_NUCLEUS, %g6
	stxa	%g0, [%g6] ASI_DMMU_DEMAP
	stxa	%g0, [%g0 + AA_DMMU_SFSR] %asi
	membar	#Sync

	/*
	 * Recompute the TTE address, which we clobbered loading the TTE.
	 * The TSB mask and address of the TSB are patched at startup.
	 */
	.globl	tl1_dmmu_prot_patch_tsb_2
tl1_dmmu_prot_patch_tsb_2:
	sethi	%uhi(TSB_KERNEL), %g6
	or	%g6, %ulo(TSB_KERNEL), %g6
	sllx	%g6, 32, %g6
	sethi	%hi(TSB_KERNEL), %g7
	or	%g7, %g6, %g7
	.globl	tl1_dmmu_prot_patch_tsb_mask_2
tl1_dmmu_prot_patch_tsb_mask_2:
	sethi	%hi(TSB_KERNEL_MASK), %g6
	or	%g6, %lo(TSB_KERNEL_MASK), %g6
	and	%g5, %g6, %g5
	sllx	%g5, TTE_SHIFT, %g5
	add	%g5, %g7, %g5

	/*
	 * Set the hardware write bit.
	 */
	.globl	tl1_dmmu_prot_patch_asi_1
tl1_dmmu_prot_patch_asi_1:
	wr	%g0, TSB_ASI, %asi
	TTE_SET_W(%g5, %g6, %g7, a, %asi)

	/*
	 * May have become invalid during casxa, in which case start over.
	 */
	brgez,pn %g6, 1f
	 or	%g6, TD_W, %g6

	/*
	 * Load the TTE data into the TLB and retry the instruction.
	 */
	stxa	%g6, [%g0] ASI_DTLB_DATA_IN_REG
1:	retry
END(tl1_dmmu_prot_1)

ENTRY(tl1_dmmu_prot_trap)
	/*
	 * Switch to alternate globals.
	 */
	wrpr	%g0, PSTATE_ALT, %pstate

	/*
	 * Load the SFAR, SFSR and TAR.  Clear the SFSR.
	 */
	ldxa	[%g0 + AA_DMMU_TAR] %asi, %g2
	ldxa	[%g0 + AA_DMMU_SFAR] %asi, %g3
	ldxa	[%g0 + AA_DMMU_SFSR] %asi, %g4
	stxa	%g0, [%g0 + AA_DMMU_SFSR] %asi
	membar	#Sync

	tl1_split
	clr	%o1
	set	trap, %o2
	mov	%g2, %o3
	mov	%g3, %o4
	mov	%g4, %o5
	ba	%xcc, tl1_trap
	 mov	T_DATA_PROTECTION | T_KERNEL, %o0
END(tl1_dmmu_prot_trap)

	.macro	tl1_spill_0_n
	SPILL(stx, %sp + SPOFF, 8, EMPTY)
	saved
	retry
	.align	32
	RSF_FATAL(T_SPILL)
	RSF_FATAL(T_SPILL)
	.endm

	.macro	tl1_spill_2_n
	wr	%g0, ASI_AIUP, %asi
	SPILL(stxa, %sp + SPOFF, 8, %asi)
	saved
	retry
	.align	32
	RSF_SPILL_TOPCB
	RSF_SPILL_TOPCB
	.endm

	.macro	tl1_spill_3_n
	wr	%g0, ASI_AIUP, %asi
	SPILL(stwa, %sp, 4, %asi)
	saved
	retry
	.align	32
	RSF_SPILL_TOPCB
	RSF_SPILL_TOPCB
	.endm

	.macro	tl1_spill_7_n
	btst	1, %sp
	bnz,a,pn %xcc, tl1_spill_0_n
	 nop
	srl	%sp, 0, %sp
	SPILL(stw, %sp, 4, EMPTY)
	saved
	retry
	.align	32
	RSF_FATAL(T_SPILL)
	RSF_FATAL(T_SPILL)
	.endm

	.macro	tl1_spill_0_o
	wr	%g0, ASI_AIUP, %asi
	SPILL(stxa, %sp + SPOFF, 8, %asi)
	saved
	retry
	.align	32
	RSF_SPILL_TOPCB
	RSF_SPILL_TOPCB
	.endm

	.macro	tl1_spill_1_o
	wr	%g0, ASI_AIUP, %asi
	SPILL(stwa, %sp, 4, %asi)
	saved
	retry
	.align	32
	RSF_SPILL_TOPCB
	RSF_SPILL_TOPCB
	.endm

	.macro	tl1_spill_2_o
	RSF_SPILL_TOPCB
	.align	128
	.endm

	.macro	tl1_fill_0_n
	FILL(ldx, %sp + SPOFF, 8, EMPTY)
	restored
	retry
	.align	32
	RSF_FATAL(T_FILL)
	RSF_FATAL(T_FILL)
	.endm

	.macro	tl1_fill_2_n
	wr	%g0, ASI_AIUP, %asi
	FILL(ldxa, %sp + SPOFF, 8, %asi)
	restored
	retry
	.align 32
	RSF_FILL_MAGIC
	RSF_FILL_MAGIC
	.endm

	.macro	tl1_fill_3_n
	wr	%g0, ASI_AIUP, %asi
	FILL(lduwa, %sp, 4, %asi)
	restored
	retry
	.align 32
	RSF_FILL_MAGIC
	RSF_FILL_MAGIC
	.endm

	.macro	tl1_fill_7_n
	btst	1, %sp
	bnz,a,pt %xcc, tl1_fill_0_n
	 nop
	srl	%sp, 0, %sp
	FILL(lduw, %sp, 4, EMPTY)
	restored
	retry
	.align	32
	RSF_FATAL(T_FILL)
	RSF_FATAL(T_FILL)
	.endm

/*
 * This is used to spill windows that are still occupied with user
 * data on kernel entry to the pcb.
 */
ENTRY(tl1_spill_topcb)
	wrpr	%g0, PSTATE_ALT, %pstate

	/* Free some globals for our use. */
	dec	24, ASP_REG
	stx	%g1, [ASP_REG + 0]
	stx	%g2, [ASP_REG + 8]
	stx	%g3, [ASP_REG + 16]

	ldx	[PCB_REG + PCB_NSAVED], %g1

	sllx	%g1, PTR_SHIFT, %g2
	add	%g2, PCB_REG, %g2
	stx	%sp, [%g2 + PCB_RWSP]

	sllx	%g1, RW_SHIFT, %g2
	add	%g2, PCB_REG, %g2
	SPILL(stx, %g2 + PCB_RW, 8, EMPTY)

	inc	%g1
	stx	%g1, [PCB_REG + PCB_NSAVED]

#if KTR_COMPILE & KTR_TRAP
	CATR(KTR_TRAP, "tl1_spill_topcb: pc=%#lx npc=%#lx sp=%#lx nsaved=%d"
	   , %g1, %g2, %g3, 7, 8, 9)
	rdpr	%tpc, %g2
	stx	%g2, [%g1 + KTR_PARM1]
	rdpr	%tnpc, %g2
	stx	%g2, [%g1 + KTR_PARM2]
	stx	%sp, [%g1 + KTR_PARM3]
	ldx	[PCB_REG + PCB_NSAVED], %g2
	stx	%g2, [%g1 + KTR_PARM4]
9:
#endif

	saved

	ldx	[ASP_REG + 16], %g3
	ldx	[ASP_REG + 8], %g2
	ldx	[ASP_REG + 0], %g1
	inc	24, ASP_REG
	retry
END(tl1_spill_topcb)

	.macro	tl1_spill_bad	count
	.rept	\count
	sir
	.align	128
	.endr
	.endm

	.macro	tl1_fill_bad	count
	.rept	\count
	sir
	.align	128
	.endr
	.endm

	.macro	tl1_soft	count
	.rept	\count
	tl1_gen	T_SOFT | T_KERNEL
	.endr
	.endm

	.sect	.trap
	.globl	tl_trap_begin
tl_trap_begin:
	nop

	.align	0x8000
	.globl	tl0_base

tl0_base:
	tl0_reserved	8				! 0x0-0x7
tl0_insn_excptn:
	tl0_insn_excptn					! 0x8
	tl0_reserved	1				! 0x9
tl0_insn_error:
	tl0_gen		T_INSTRUCTION_ERROR		! 0xa
	tl0_reserved	5				! 0xb-0xf
tl0_insn_illegal:
	tl0_gen		T_ILLEGAL_INSTRUCTION		! 0x10
tl0_priv_opcode:
	tl0_gen		T_PRIVILEGED_OPCODE		! 0x11
	tl0_reserved	14				! 0x12-0x1f
tl0_fp_disabled:
	tl0_gen		T_FP_DISABLED			! 0x20
tl0_fp_ieee:
	tl0_gen		T_FP_EXCEPTION_IEEE_754		! 0x21
tl0_fp_other:
	tl0_gen		T_FP_EXCEPTION_OTHER		! 0x22
tl0_tag_ovflw:
	tl0_gen		T_TAG_OVERFLOW			! 0x23
tl0_clean_window:
	clean_window					! 0x24
tl0_divide:
	tl0_gen		T_DIVISION_BY_ZERO		! 0x28
	tl0_reserved	7				! 0x29-0x2f
tl0_data_excptn:
	tl0_data_excptn					! 0x30
	tl0_reserved	1				! 0x31
tl0_data_error:
	tl0_gen		T_DATA_ERROR			! 0x32
	tl0_reserved	1				! 0x33
tl0_align:
	tl0_align					! 0x34
tl0_align_lddf:
	tl0_gen		T_RESERVED			! 0x35
tl0_align_stdf:
	tl0_gen		T_RESERVED			! 0x36
tl0_priv_action:
	tl0_gen		T_PRIVILEGED_ACTION		! 0x37
	tl0_reserved	9				! 0x38-0x40
tl0_intr_level:
	tl0_intr_level					! 0x41-0x4f
	tl0_reserved	16				! 0x50-0x5f
tl0_intr_vector:
	intr_vector					! 0x60
tl0_watch_phys:
	tl0_gen		T_PA_WATCHPOINT			! 0x61
tl0_watch_virt:
	tl0_gen		T_VA_WATCHPOINT			! 0x62
tl0_ecc:
	tl0_gen		T_CORRECTED_ECC_ERROR		! 0x63
tl0_immu_miss:
	tl0_immu_miss					! 0x64
tl0_dmmu_miss:
	tl0_dmmu_miss					! 0x68
tl0_dmmu_prot:
	tl0_dmmu_prot					! 0x6c
	tl0_reserved	16				! 0x70-0x7f
tl0_spill_0_n:
	tl0_spill_0_n					! 0x80
tl0_spill_1_n:
	tl0_spill_1_n					! 0x84
	tl0_spill_bad	14				! 0x88-0xbf
tl0_fill_0_n:
	tl0_fill_0_n					! 0xc0
tl0_fill_1_n:
	tl0_fill_1_n					! 0xc4
	tl0_fill_bad	14				! 0xc8-0xff
tl0_soft:
	tl0_gen		T_SYSCALL			! 0x100
	tl0_gen		T_BREAKPOINT			! 0x101
	tl0_gen		T_DIVISION_BY_ZERO		! 0x102
	tl0_reserved	1				! 0x103
	tl0_gen		T_CLEAN_WINDOW			! 0x104
	tl0_gen		T_RANGE_CHECK			! 0x105
	tl0_gen		T_FIX_ALIGNMENT			! 0x106
	tl0_gen		T_INTEGER_OVERFLOW		! 0x107
	tl0_gen		T_SYSCALL			! 0x108
	tl0_gen		T_SYSCALL			! 0x109
	tl0_fp_restore					! 0x10a
	tl0_reserved	5				! 0x10b-0x10f
	tl0_gen		T_TRAP_INSTRUCTION_16		! 0x110
	tl0_gen		T_TRAP_INSTRUCTION_17		! 0x111
	tl0_gen		T_TRAP_INSTRUCTION_18		! 0x112
	tl0_gen		T_TRAP_INSTRUCTION_19		! 0x113
	tl0_gen		T_TRAP_INSTRUCTION_20		! 0x114
	tl0_gen		T_TRAP_INSTRUCTION_21		! 0x115
	tl0_gen		T_TRAP_INSTRUCTION_22		! 0x116
	tl0_gen		T_TRAP_INSTRUCTION_23		! 0x117
	tl0_gen		T_TRAP_INSTRUCTION_24		! 0x118
	tl0_gen		T_TRAP_INSTRUCTION_25		! 0x119
	tl0_gen		T_TRAP_INSTRUCTION_26		! 0x11a
	tl0_gen		T_TRAP_INSTRUCTION_27		! 0x11b
	tl0_gen		T_TRAP_INSTRUCTION_28		! 0x11c
	tl0_gen		T_TRAP_INSTRUCTION_29		! 0x11d
	tl0_gen		T_TRAP_INSTRUCTION_30		! 0x11e
	tl0_gen		T_TRAP_INSTRUCTION_31		! 0x11f
	tl0_reserved	32				! 0x120-0x13f
	tl0_gen		T_SYSCALL			! 0x140
	tl0_syscall					! 0x141
	tl0_gen		T_SYSCALL			! 0x142
	tl0_gen		T_SYSCALL			! 0x143
	tl0_reserved	188				! 0x144-0x1ff

tl1_base:
	tl1_reserved	8				! 0x200-0x207
tl1_insn_excptn:
	tl1_insn_excptn					! 0x208
	tl1_reserved	1				! 0x209
tl1_insn_error:
	tl1_gen		T_INSTRUCTION_ERROR		! 0x20a
	tl1_reserved	5				! 0x20b-0x20f
tl1_insn_illegal:
	tl1_gen		T_ILLEGAL_INSTRUCTION		! 0x210
tl1_priv_opcode:
	tl1_gen		T_PRIVILEGED_OPCODE		! 0x211
	tl1_reserved	14				! 0x212-0x21f
tl1_fp_disabled:
	tl1_fp_disabled					! 0x220
tl1_fp_ieee:
	tl1_gen		T_FP_EXCEPTION_IEEE_754		! 0x221
tl1_fp_other:
	tl1_gen		T_FP_EXCEPTION_OTHER		! 0x222
tl1_tag_ovflw:
	tl1_gen		T_TAG_OVERFLOW			! 0x223
tl1_clean_window:
	clean_window					! 0x224
tl1_divide:
	tl1_gen		T_DIVISION_BY_ZERO		! 0x228
	tl1_reserved	7				! 0x229-0x22f
tl1_data_excptn:
	tl1_data_excptn					! 0x230
	tl1_reserved	1				! 0x231
tl1_data_error:
	tl1_gen		T_DATA_ERROR			! 0x232
	tl1_reserved	1				! 0x233
tl1_align:
	tl1_align					! 0x234
tl1_align_lddf:
	tl1_gen		T_RESERVED			! 0x235
tl1_align_stdf:
	tl1_gen		T_RESERVED			! 0x236
tl1_priv_action:
	tl1_gen		T_PRIVILEGED_ACTION		! 0x237
	tl1_reserved	9				! 0x238-0x240
tl1_intr_level:
	tl1_intr_level					! 0x241-0x24f
	tl1_reserved	16				! 0x250-0x25f
tl1_intr_vector:
	intr_vector					! 0x260
tl1_watch_phys:
	tl1_gen		T_PA_WATCHPOINT			! 0x261
tl1_watch_virt:
	tl1_gen		T_VA_WATCHPOINT			! 0x262
tl1_ecc:
	tl1_gen		T_CORRECTED_ECC_ERROR		! 0x263
tl1_immu_miss:
	tl1_immu_miss					! 0x264
tl1_dmmu_miss:
	tl1_dmmu_miss					! 0x268
tl1_dmmu_prot:
	tl1_dmmu_prot					! 0x26c
	tl1_reserved	16				! 0x270-0x27f
tl1_spill_0_n:
	tl1_spill_0_n					! 0x280
	tl1_spill_bad	1				! 0x284
tl1_spill_2_n:
	tl1_spill_2_n					! 0x288
tl1_spill_3_n:
	tl1_spill_3_n					! 0x28c
	tl1_spill_bad	3				! 0x290-0x29b
tl1_spill_7_n:
	tl1_spill_7_n					! 0x29c
tl1_spill_0_o:
	tl1_spill_0_o					! 0x2a0
tl1_spill_1_o:
	tl1_spill_1_o					! 0x2a4
tl1_spill_2_o:
	tl1_spill_2_o					! 0x2a8
	tl1_spill_bad	5				! 0x2ac-0x2bf
tl1_fill_0_n:
	tl1_fill_0_n					! 0x2c0
	tl1_fill_bad	1				! 0x2c4
tl1_fill_2_n:
	tl1_fill_2_n					! 0x2c8
tl1_fill_3_n:
	tl1_fill_3_n					! 0x2cc
	tl1_fill_bad	3				! 0x2d0-0x2db
tl1_fill_7_n:
	tl1_fill_7_n					! 0x2dc
	tl1_fill_bad	8				! 0x2e0-0x2ff
	tl1_reserved	1				! 0x300
tl1_breakpoint:
	tl1_gen		T_BREAKPOINT			! 0x301
	tl1_gen		T_RSTRWP_PHYS			! 0x302
	tl1_gen		T_RSTRWP_VIRT			! 0x303
	tl1_reserved	252				! 0x304-0x3ff

	.globl	tl_trap_end
tl_trap_end:
	nop

/*
 * User trap entry point
 *
 * void tl0_utrap(u_long type, u_long o1, u_long o2, u_long tar, u_long sfar,
 *     u_long sfsr)
 *
 * This handles redirecting a trap back to usermode as a user trap.  The user
 * program must have first registered a trap handler with the kernel using
 * sysarch(SPARC_UTRAP_INSTALL).  The trap handler is passed enough state
 * for it to return to the trapping code directly, it will not return through
 * the kernel.  The trap type is passed in %o0, all out registers must be
 * passed through to tl0_trap or to usermode untouched.  Note that the
 * parameters passed in out registers may be used by the user trap handler.
 * Do not change the registers they are passed in or you will break the ABI.
 *
 * If the trap type allows user traps, setup state to execute the user trap
 * handler and bounce back to usermode, otherwise branch to tl0_trap.
 */
ENTRY(tl0_utrap)
	/*
	 * Check if the trap type allows user traps.
	 */
	cmp	%o0, UT_MAX
	bge,a,pt %xcc, tl0_trap
	 nop

	/*
	 * Load the user trap handler from the utrap table.
	 */
	ldx	[PCPU(CURTHREAD)], %l0
	ldx	[%l0 + TD_PROC], %l0
	ldx	[%l0 + P_MD + MD_UTRAP], %l0
	brz,pt	%l0, tl0_trap
	 sllx	%o0, PTR_SHIFT, %l1
	ldx	[%l0 + %l1], %l0
	brz,a,pt %l0, tl0_trap
	 nop

	/*
	 * If the save we did on entry to the kernel had to spill a window
	 * to the pcb, pretend we took a spill trap instead.  Any windows
	 * that are in the pcb must be copied out or the fill handler will
	 * not be able to find them, since the user trap handler returns
	 * directly to the trapping code.  Note that we only support precise
	 * user traps, which implies that the condition that caused the trap
	 * in the first place is still valid, so it will occur again when we
	 * re-execute the trapping instruction.
	 */
	ldx	[PCB_REG + PCB_NSAVED], %l1
	brnz,a,pn %l1, tl0_trap
	 mov	T_SPILL, %o0

	/*
	 * Pass %fsr in %l4, %tstate in %l5, %tpc in %l6 and %tnpc in %l7.
	 * The ABI specifies only %l6 and %l7, but we need to pass %fsr or
	 * it may be clobbered by an interrupt before the user trap code
	 * can read it, and we must pass %tstate in order to restore %ccr
	 * and %asi.  The %fsr must be stored to memory, so we use the
	 * temporary stack for that.
	 */
	rd	%fprs, %l1
	or	%l1, FPRS_FEF, %l2
	wr	%l2, 0, %fprs
	dec	8, ASP_REG
	stx	%fsr, [ASP_REG]
	ldx	[ASP_REG], %l4
	inc	8, ASP_REG
	wr	%l1, 0, %fprs

	rdpr	%tstate, %l5
	rdpr	%tpc, %l6
	rdpr	%tnpc, %l7

	/*
	 * Setup %tnpc to return to.
	 */
	wrpr	%l0, 0, %tnpc

	/*
	 * Setup %wstate for return, clear WSTATE_TRANSITION.
	 */
	rdpr	%wstate, %l1
	and	%l1, WSTATE_NORMAL_MASK, %l1
	wrpr	%l1, 0, %wstate

	/*
	 * Setup %tstate for return, change the saved cwp to point to the
	 * current window instead of the window at the time of the trap.
	 */
	andn	%l5, TSTATE_CWP_MASK, %l1
	rdpr	%cwp, %l2
	wrpr	%l1, %l2, %tstate

	/*
	 * Setup %sp.  Userland processes will crash if this is not setup.
	 */
	sub	%fp, CCFSZ, %sp

	/*
	 * Execute the user trap handler.
	 */
	done
END(tl0_utrap)

/*
 * (Real) User trap entry point
 *
 * void tl0_trap(u_int type, u_long o1, u_long o2, u_long tar, u_long sfsr,
 *     u_int sfsr)
 *
 * The following setup has been performed:
 *	- the windows have been split and the active user window has been saved
 *	  (maybe just to the pcb)
 *	- we are on alternate globals and interrupts are disabled
 *
 * We switch to the kernel stack, build a trapframe, switch to normal
 * globals, enable interrupts and call trap.
 *
 * NOTE: We must be very careful setting up the per-cpu pointer.  We know that
 * it has been pre-set in alternate globals, so we read it from there and setup
 * the normal %g7 *before* enabling interrupts.  This avoids any possibility
 * of cpu migration and using the wrong pcpup.
 */
ENTRY(tl0_trap)
	/*
	 * Force kernel store order.
	 */
	wrpr	%g0, PSTATE_ALT, %pstate

	rdpr	%tstate, %l0
	rdpr	%tpc, %l1
	rdpr	%tnpc, %l2
	rd	%y, %l3
	rd	%fprs, %l4
	rdpr	%wstate, %l5

#if KTR_COMPILE & KTR_TRAP
	CATR(KTR_TRAP,
	    "tl0_trap: td=%p type=%#x pil=%#lx pc=%#lx npc=%#lx sp=%#lx"
	    , %g1, %g2, %g3, 7, 8, 9)
	ldx	[PCPU(CURTHREAD)], %g2
	stx	%g2, [%g1 + KTR_PARM1]
	stx	%o0, [%g1 + KTR_PARM2]
	rdpr	%pil, %g2
	stx	%g2, [%g1 + KTR_PARM3]
	stx	%l1, [%g1 + KTR_PARM4]
	stx	%l2, [%g1 + KTR_PARM5]
	stx	%i6, [%g1 + KTR_PARM6]
9:
#endif

1:	and	%l5, WSTATE_NORMAL_MASK, %l5
	sllx	%l5, WSTATE_OTHER_SHIFT, %l5
	wrpr	%l5, WSTATE_KERNEL, %wstate
	rdpr	%canrestore, %l6
	wrpr	%l6, 0, %otherwin
	wrpr	%g0, 0, %canrestore

	sub	PCB_REG, SPOFF + CCFSZ + TF_SIZEOF, %sp

	stx	%o0, [%sp + SPOFF + CCFSZ + TF_TYPE]
	stx	%o1, [%sp + SPOFF + CCFSZ + TF_LEVEL]
	stx	%o3, [%sp + SPOFF + CCFSZ + TF_TAR]
	stx	%o4, [%sp + SPOFF + CCFSZ + TF_SFAR]
	stx	%o5, [%sp + SPOFF + CCFSZ + TF_SFSR]

	stx	%l0, [%sp + SPOFF + CCFSZ + TF_TSTATE]
	stx	%l1, [%sp + SPOFF + CCFSZ + TF_TPC]
	stx	%l2, [%sp + SPOFF + CCFSZ + TF_TNPC]
	stx	%l3, [%sp + SPOFF + CCFSZ + TF_Y]
	stx	%l4, [%sp + SPOFF + CCFSZ + TF_FPRS]
	stx	%l5, [%sp + SPOFF + CCFSZ + TF_WSTATE]

	wr	%g0, FPRS_FEF, %fprs
	stx	%fsr, [%sp + SPOFF + CCFSZ + TF_FSR]
	rd	%gsr, %l6
	stx	%l6, [%sp + SPOFF + CCFSZ + TF_GSR]
	wr	%g0, 0, %fprs

	mov	PCB_REG, %l0
	mov	PCPU_REG, %l1
	wrpr	%g0, PSTATE_NORMAL, %pstate

	stx	%g6, [%sp + SPOFF + CCFSZ + TF_G6]
	stx	%g7, [%sp + SPOFF + CCFSZ + TF_G7]

	mov	%l0, PCB_REG
	mov	%l1, PCPU_REG
	wrpr	%g0, PSTATE_KERNEL, %pstate

	stx	%i0, [%sp + SPOFF + CCFSZ + TF_O0]
	stx	%i1, [%sp + SPOFF + CCFSZ + TF_O1]
	stx	%i2, [%sp + SPOFF + CCFSZ + TF_O2]
	stx	%i3, [%sp + SPOFF + CCFSZ + TF_O3]
	stx	%i4, [%sp + SPOFF + CCFSZ + TF_O4]
	stx	%i5, [%sp + SPOFF + CCFSZ + TF_O5]
	stx	%i6, [%sp + SPOFF + CCFSZ + TF_O6]
	stx	%i7, [%sp + SPOFF + CCFSZ + TF_O7]

	stx	%g1, [%sp + SPOFF + CCFSZ + TF_G1]
	stx	%g2, [%sp + SPOFF + CCFSZ + TF_G2]
	stx	%g3, [%sp + SPOFF + CCFSZ + TF_G3]
	stx	%g4, [%sp + SPOFF + CCFSZ + TF_G4]
	stx	%g5, [%sp + SPOFF + CCFSZ + TF_G5]

	set	tl0_ret - 8, %o7
	jmpl	%o2, %g0
	 add	%sp, CCFSZ + SPOFF, %o0
END(tl0_trap)

/*
 * void tl0_intr(u_int level, u_int mask)
 */
ENTRY(tl0_intr)
	/*
	 * Force kernel store order.
	 */
	wrpr	%g0, PSTATE_ALT, %pstate

	rdpr	%tstate, %l0
	rdpr	%tpc, %l1
	rdpr	%tnpc, %l2
	rd	%y, %l3
	rd	%fprs, %l4
	rdpr	%wstate, %l5

#if KTR_COMPILE & KTR_INTR
	CATR(KTR_INTR,
	    "tl0_intr: td=%p level=%#x pil=%#lx pc=%#lx npc=%#lx sp=%#lx"
	    , %g1, %g2, %g3, 7, 8, 9)
	ldx	[PCPU(CURTHREAD)], %g2
	stx	%g2, [%g1 + KTR_PARM1]
	stx	%o0, [%g1 + KTR_PARM2]
	rdpr	%pil, %g2
	stx	%g2, [%g1 + KTR_PARM3]
	stx	%l1, [%g1 + KTR_PARM4]
	stx	%l2, [%g1 + KTR_PARM5]
	stx	%i6, [%g1 + KTR_PARM6]
9:
#endif

	wrpr	%o0, 0, %pil
	wr	%o1, 0, %clear_softint

	and	%l5, WSTATE_NORMAL_MASK, %l5
	sllx	%l5, WSTATE_OTHER_SHIFT, %l5
	wrpr	%l5, WSTATE_KERNEL, %wstate
	rdpr	%canrestore, %l6
	wrpr	%l6, 0, %otherwin
	wrpr	%g0, 0, %canrestore

	sub	PCB_REG, SPOFF + CCFSZ + TF_SIZEOF, %sp

	stx	%l0, [%sp + SPOFF + CCFSZ + TF_TSTATE]
	stx	%l1, [%sp + SPOFF + CCFSZ + TF_TPC]
	stx	%l2, [%sp + SPOFF + CCFSZ + TF_TNPC]
	stx	%l3, [%sp + SPOFF + CCFSZ + TF_Y]
	stx	%l4, [%sp + SPOFF + CCFSZ + TF_FPRS]
	stx	%l5, [%sp + SPOFF + CCFSZ + TF_WSTATE]

	wr	%g0, FPRS_FEF, %fprs
	stx	%fsr, [%sp + SPOFF + CCFSZ + TF_FSR]
	rd	%gsr, %l6
	stx	%l6, [%sp + SPOFF + CCFSZ + TF_GSR]
	wr	%g0, 0, %fprs

	mov	%o0, %l3
	mov	T_INTERRUPT, %o1

	stx	%o0, [%sp + SPOFF + CCFSZ + TF_LEVEL]
	stx	%o1, [%sp + SPOFF + CCFSZ + TF_TYPE]

	mov	PCB_REG, %l0
	mov	PCPU_REG, %l1
	wrpr	%g0, PSTATE_NORMAL, %pstate

	stx	%g1, [%sp + SPOFF + CCFSZ + TF_G1]
	stx	%g2, [%sp + SPOFF + CCFSZ + TF_G2]
	stx	%g3, [%sp + SPOFF + CCFSZ + TF_G3]
	stx	%g4, [%sp + SPOFF + CCFSZ + TF_G4]
	stx	%g5, [%sp + SPOFF + CCFSZ + TF_G5]
	stx	%g6, [%sp + SPOFF + CCFSZ + TF_G6]
	stx	%g7, [%sp + SPOFF + CCFSZ + TF_G7]

	mov	%l0, PCB_REG
	mov	%l1, PCPU_REG
	wrpr	%g0, PSTATE_KERNEL, %pstate

	stx	%i0, [%sp + SPOFF + CCFSZ + TF_O0]
	stx	%i1, [%sp + SPOFF + CCFSZ + TF_O1]
	stx	%i2, [%sp + SPOFF + CCFSZ + TF_O2]
	stx	%i3, [%sp + SPOFF + CCFSZ + TF_O3]
	stx	%i4, [%sp + SPOFF + CCFSZ + TF_O4]
	stx	%i5, [%sp + SPOFF + CCFSZ + TF_O5]
	stx	%i6, [%sp + SPOFF + CCFSZ + TF_O6]
	stx	%i7, [%sp + SPOFF + CCFSZ + TF_O7]

	SET(intr_handlers, %l1, %l0)
	sllx	%l3, IH_SHIFT, %l1
	ldx	[%l0 + %l1], %l1
	KASSERT(%l1, "tl0_intr: ih null")
	call	%l1
	 add	%sp, CCFSZ + SPOFF, %o0

	/* %l3 contains PIL */
	SET(intrcnt, %l1, %l2)
	prefetcha [%l2] ASI_N, 1
	SET(pil_countp, %l1, %l0)
	sllx	%l3, 1, %l1
	lduh	[%l0 + %l1], %l0
	sllx	%l0, 3, %l0
	add	%l0, %l2, %l0
	ldx	[%l0], %l1
	inc	%l1
	stx	%l1, [%l0]

	call	counter_intr_inc
	 nop

	ba,a	%xcc, tl0_ret
	 nop
END(tl0_intr)

/*
 * Initiate return to usermode.
 *
 * Called with a trapframe on the stack.  The window that was setup in
 * tl0_trap may have been used by "fast" trap handlers that pretend to be
 * leaf functions, so all ins and locals may have been clobbered since
 * then.
 *
 * This code is rather long and complicated.
 */
ENTRY(tl0_ret)
	/*
	 * Check for pending asts atomically with returning.  We must raise
	 * the PIL before checking, and if no asts are found the PIL must
	 * remain raised until the retry is executed, or we risk missing asts
	 * caused by interrupts occurring after the test.  If the PIL is
	 * lowered, as it is when we call ast, the check must be re-executed.
	 */
	wrpr	%g0, PIL_TICK, %pil
	ldx	[PCPU(CURTHREAD)], %l0
	lduw	[%l0 + TD_FLAGS], %l1
	set	TDF_ASTPENDING | TDF_NEEDRESCHED, %l2
	and	%l1, %l2, %l1
	brz,a,pt %l1, 1f
	 nop

	/*
	 * We have an AST.  Re-enable interrupts and handle it, then restart
	 * the return sequence.
	 */
	wrpr	%g0, 0, %pil
	call	ast
	 add	%sp, CCFSZ + SPOFF, %o0
	ba,a	%xcc, tl0_ret
	 nop

	/*
	 * Check for windows that were spilled to the pcb and need to be
	 * copied out.  This must be the last thing that is done before the
	 * return to usermode.  If there are still user windows in the cpu
	 * and we call a nested function after this, which causes them to be
	 * spilled to the pcb, they will not be copied out and the stack will
	 * be inconsistent.
	 */
1:	ldx	[PCB_REG + PCB_NSAVED], %l1
	brz,a,pt %l1, 2f
	 nop
	wrpr	%g0, 0, %pil
	mov	T_SPILL, %o0
	stx	%o0, [%sp + SPOFF + CCFSZ + TF_TYPE]
	call	trap
	 add	%sp, SPOFF + CCFSZ, %o0
	ba,a	%xcc, tl0_ret
	 nop

	/*
	 * Restore the out and most global registers from the trapframe.
	 * The ins will become the outs when we restore below.
	 */
2:	ldx	[%sp + SPOFF + CCFSZ + TF_O0], %i0
	ldx	[%sp + SPOFF + CCFSZ + TF_O1], %i1
	ldx	[%sp + SPOFF + CCFSZ + TF_O2], %i2
	ldx	[%sp + SPOFF + CCFSZ + TF_O3], %i3
	ldx	[%sp + SPOFF + CCFSZ + TF_O4], %i4
	ldx	[%sp + SPOFF + CCFSZ + TF_O5], %i5
	ldx	[%sp + SPOFF + CCFSZ + TF_O6], %i6
	ldx	[%sp + SPOFF + CCFSZ + TF_O7], %i7

	ldx	[%sp + SPOFF + CCFSZ + TF_G1], %g1
	ldx	[%sp + SPOFF + CCFSZ + TF_G2], %g2
	ldx	[%sp + SPOFF + CCFSZ + TF_G3], %g3
	ldx	[%sp + SPOFF + CCFSZ + TF_G4], %g4
	ldx	[%sp + SPOFF + CCFSZ + TF_G5], %g5

	/*
	 * Load everything we need to restore below before disabling
	 * interrupts.
	 */
	ldx	[%sp + SPOFF + CCFSZ + TF_FPRS], %l0
	ldx	[%sp + SPOFF + CCFSZ + TF_GSR], %l1
	ldx	[%sp + SPOFF + CCFSZ + TF_TNPC], %l2
	ldx	[%sp + SPOFF + CCFSZ + TF_TPC], %l3
	ldx	[%sp + SPOFF + CCFSZ + TF_TSTATE], %l4
	ldx	[%sp + SPOFF + CCFSZ + TF_Y], %l5
	ldx	[%sp + SPOFF + CCFSZ + TF_WSTATE], %l6

	/*
	 * Disable interrupts to restore the special globals.  They are not
	 * saved and restored for all kernel traps, so an interrupt at the
	 * wrong time would clobber them.
	 */
	wrpr	%g0, PSTATE_NORMAL, %pstate

	ldx	[%sp + SPOFF + CCFSZ + TF_G6], %g6
	ldx	[%sp + SPOFF + CCFSZ + TF_G7], %g7

	/*
	 * Switch to alternate globals.  This frees up some registers we
	 * can use after the restore changes our window.
	 */
	wrpr	%g0, PSTATE_ALT, %pstate

	/*
	 * Drop %pil to zero.  It must have been zero at the time of the
	 * trap, since we were in usermode, but it was raised above in
	 * order to check for asts atomically.  We have interrupts disabled
	 * so any interrupts will not be serviced until we complete the
	 * return to usermode.
	 */
	wrpr	%g0, 0, %pil

	/*
	 * Save %fprs in an alternate global so it can be restored after the
	 * restore instruction below.  If we restore it before the restore,
	 * and the restore traps we may run for a while with floating point
	 * enabled in the kernel, which we want to avoid.
	 */
	mov	%l0, %g1

	/*
	 * Restore %fsr and %gsr.  These need floating point enabled in %fprs,
	 * so we set it temporarily and then clear it.
	 */
	wr	%g0, FPRS_FEF, %fprs
	ldx	[%sp + SPOFF + CCFSZ + TF_FSR], %fsr
	wr	%l1, 0, %gsr
	wr	%g0, 0, %fprs

	/*
	 * Restore program counters.  This could be done after the restore
	 * but we're out of alternate globals to store them in...
	 */
	wrpr	%l2, 0, %tnpc
	wrpr	%l3, 0, %tpc

	/*
	 * Save %tstate in an alternate global and clear the %cwp field.  %cwp
	 * will be affected by the restore below and we need to make sure it
	 * points to the current window at that time, not the window that was
	 * active at the time of the trap.
	 */
	andn	%l4, TSTATE_CWP_MASK, %g2

	/*
	 * Restore %y.  Could also be below if we had more alternate globals.
	 */
	wr	%l5, 0, %y

	/*
	 * Setup %wstate for return.  We need to restore the user window state
	 * which we saved in wstate.other when we trapped.  We also need to
	 * set the transition bit so the restore will be handled specially
	 * if it traps, use the xor feature of wrpr to do that.
	 */
	srlx	%l6, WSTATE_OTHER_SHIFT, %g3
	wrpr	%g3, WSTATE_TRANSITION, %wstate

	/*
	 * Setup window management registers for return.  If not all user
	 * windows were spilled in the kernel %otherwin will be non-zero,
	 * so we need to transfer it to %canrestore to correctly restore
	 * those windows.  Otherwise everything gets set to zero and the
	 * restore below will fill a window directly from the user stack.
	 */
	rdpr	%otherwin, %o0
	wrpr	%o0, 0, %canrestore
	wrpr	%g0, 0, %otherwin
	wrpr	%o0, 0, %cleanwin

	/*
	 * Now do the restore.  If this instruction causes a fill trap which
	 * fails to fill a window from the user stack, we will resume at
	 * tl0_ret_fill_end and call back into the kernel.
	 */
	restore
tl0_ret_fill:

	/*
	 * We made it.  We're back in the window that was active at the time
	 * of the trap, and ready to return to usermode.
	 */

	/*
	 * Restore %frps.  This was saved in an alternate global above.
	 */
	wr	%g1, 0, %fprs

	/*
	 * Fixup %tstate so the saved %cwp points to the current window and
	 * restore it.
	 */
	rdpr	%cwp, %g4
	wrpr	%g2, %g4, %tstate

	/*
	 * Restore the user window state.  The transition bit was set above
	 * for special handling of the restore, this clears it.
	 */
	wrpr	%g3, 0, %wstate

#if KTR_COMPILE & KTR_TRAP
	CATR(KTR_TRAP, "tl0_ret: td=%#lx pil=%#lx pc=%#lx npc=%#lx sp=%#lx"
	    , %g2, %g3, %g4, 7, 8, 9)
	ldx	[PCPU(CURTHREAD)], %g3
	stx	%g3, [%g2 + KTR_PARM1]
	rdpr	%pil, %g3
	stx	%g3, [%g2 + KTR_PARM2]
	rdpr	%tpc, %g3
	stx	%g3, [%g2 + KTR_PARM3]
	rdpr	%tnpc, %g3
	stx	%g3, [%g2 + KTR_PARM4]
	stx	%sp, [%g2 + KTR_PARM5]
9:
#endif

	/*
	 * Return to usermode.
	 */
	retry
tl0_ret_fill_end:

#if KTR_COMPILE & KTR_TRAP
	CATR(KTR_TRAP, "tl0_ret: fill magic ps=%#lx ws=%#lx sp=%#lx"
	    , %l0, %l1, %l2, 7, 8, 9)
	rdpr	%pstate, %l1
	stx	%l1, [%l0 + KTR_PARM1]
	stx	%l6, [%l0 + KTR_PARM2]
	stx	%sp, [%l0 + KTR_PARM3]
9:
#endif

	/*
	 * The restore above caused a fill trap and the fill handler was
	 * unable to fill a window from the user stack.  The special fill
	 * handler recognized this and punted, sending us here.  We need
	 * to carefully undo any state that was restored before the restore
	 * was executed and call trap again.  Trap will copyin a window
	 * from the user stack which will fault in the page we need so the
	 * restore above will succeed when we try again.  If this fails
	 * the process has trashed its stack, so we kill it.
	 */

	/*
	 * Restore the kernel window state.  This was saved in %l6 above, and
	 * since the restore failed we're back in the same window.
	 */
	wrpr	%l6, 0, %wstate

	/*
	 * Restore the normal globals which have predefined values in the
	 * kernel.  We clobbered them above restoring the user's globals
	 * so this is very important.
	 * XXX PSTATE_ALT must already be set.
	 */
	wrpr	%g0, PSTATE_ALT, %pstate
	mov	PCB_REG, %o0
	mov	PCPU_REG, %o1
	wrpr	%g0, PSTATE_NORMAL, %pstate
	mov	%o0, PCB_REG
	mov	%o1, PCPU_REG
	wrpr	%g0, PSTATE_KERNEL, %pstate

	/*
	 * Simulate a fill trap and then start the whole return sequence over
	 * again.  This is special because it only copies in 1 window, not 2
	 * as we would for a normal failed fill.  This may be the first time
	 * the process has been run, so there may not be 2 windows worth of
	 * stack to copyin.
	 */
	mov	T_FILL_RET, %o0
	stx	%o0, [%sp + SPOFF + CCFSZ + TF_TYPE]
	call	trap
	 add	%sp, SPOFF + CCFSZ, %o0
	ba,a	%xcc, tl0_ret
	 nop
END(tl0_ret)

/*
 * Kernel trap entry point
 *
 * void tl1_trap(u_int type, u_long o1, u_long o2, u_long tar, u_long sfar,
 *     u_int sfsr)
 *
 * This is easy because the stack is already setup and the windows don't need
 * to be split.  We build a trapframe and call trap(), the same as above, but
 * the outs don't need to be saved.
 */
ENTRY(tl1_trap)
	rdpr	%tstate, %l0
	rdpr	%tpc, %l1
	rdpr	%tnpc, %l2
	rdpr	%pil, %l3
	rd	%y, %l4
	rdpr	%wstate, %l5

#if KTR_COMPILE & KTR_TRAP
	CATR(KTR_TRAP, "tl1_trap: td=%p type=%#lx pil=%#lx pc=%#lx sp=%#lx"
	    , %g1, %g2, %g3, 7, 8, 9)
	ldx	[PCPU(CURTHREAD)], %g2
	stx	%g2, [%g1 + KTR_PARM1]
	stx	%o0, [%g1 + KTR_PARM2]
	stx	%l3, [%g1 + KTR_PARM3]
	stx	%l1, [%g1 + KTR_PARM4]
	stx	%i6, [%g1 + KTR_PARM5]
9:
#endif

	wrpr	%g0, 1, %tl

	and	%l5, WSTATE_OTHER_MASK, %l5
	wrpr	%l5, WSTATE_KERNEL, %wstate

	stx	%o0, [%sp + SPOFF + CCFSZ + TF_TYPE]
	stx	%o1, [%sp + SPOFF + CCFSZ + TF_LEVEL]
	stx	%o3, [%sp + SPOFF + CCFSZ + TF_TAR]
	stx	%o4, [%sp + SPOFF + CCFSZ + TF_SFAR]
	stx	%o5, [%sp + SPOFF + CCFSZ + TF_SFSR]

	stx	%l0, [%sp + SPOFF + CCFSZ + TF_TSTATE]
	stx	%l1, [%sp + SPOFF + CCFSZ + TF_TPC]
	stx	%l2, [%sp + SPOFF + CCFSZ + TF_TNPC]
	stx	%l3, [%sp + SPOFF + CCFSZ + TF_PIL]
	stx	%l4, [%sp + SPOFF + CCFSZ + TF_Y]

	mov	PCB_REG, %l0
	mov	PCPU_REG, %l1
	wrpr	%g0, PSTATE_NORMAL, %pstate

	stx	%g6, [%sp + SPOFF + CCFSZ + TF_G6]
	stx	%g7, [%sp + SPOFF + CCFSZ + TF_G7]

	mov	%l0, PCB_REG
	mov	%l1, PCPU_REG
	wrpr	%g0, PSTATE_KERNEL, %pstate

	stx	%i0, [%sp + SPOFF + CCFSZ + TF_O0]
	stx	%i1, [%sp + SPOFF + CCFSZ + TF_O1]
	stx	%i2, [%sp + SPOFF + CCFSZ + TF_O2]
	stx	%i3, [%sp + SPOFF + CCFSZ + TF_O3]
	stx	%i4, [%sp + SPOFF + CCFSZ + TF_O4]
	stx	%i5, [%sp + SPOFF + CCFSZ + TF_O5]
	stx	%i6, [%sp + SPOFF + CCFSZ + TF_O6]
	stx	%i7, [%sp + SPOFF + CCFSZ + TF_O7]

	stx	%g1, [%sp + SPOFF + CCFSZ + TF_G1]
	stx	%g2, [%sp + SPOFF + CCFSZ + TF_G2]
	stx	%g3, [%sp + SPOFF + CCFSZ + TF_G3]
	stx	%g4, [%sp + SPOFF + CCFSZ + TF_G4]
	stx	%g5, [%sp + SPOFF + CCFSZ + TF_G5]

	set	tl1_ret - 8, %o7
	jmpl	%o2, %g0
	 add	%sp, CCFSZ + SPOFF, %o0
END(tl1_trap)

ENTRY(tl1_ret)
	ldx	[%sp + SPOFF + CCFSZ + TF_O0], %i0
	ldx	[%sp + SPOFF + CCFSZ + TF_O1], %i1
	ldx	[%sp + SPOFF + CCFSZ + TF_O2], %i2
	ldx	[%sp + SPOFF + CCFSZ + TF_O3], %i3
	ldx	[%sp + SPOFF + CCFSZ + TF_O4], %i4
	ldx	[%sp + SPOFF + CCFSZ + TF_O5], %i5
	ldx	[%sp + SPOFF + CCFSZ + TF_O6], %i6
	ldx	[%sp + SPOFF + CCFSZ + TF_O7], %i7

	ldx	[%sp + SPOFF + CCFSZ + TF_G1], %g1
	ldx	[%sp + SPOFF + CCFSZ + TF_G2], %g2
	ldx	[%sp + SPOFF + CCFSZ + TF_G3], %g3
	ldx	[%sp + SPOFF + CCFSZ + TF_G4], %g4
	ldx	[%sp + SPOFF + CCFSZ + TF_G5], %g5

	ldx	[%sp + SPOFF + CCFSZ + TF_TSTATE], %l0
	ldx	[%sp + SPOFF + CCFSZ + TF_TPC], %l1
	ldx	[%sp + SPOFF + CCFSZ + TF_TNPC], %l2
	ldx	[%sp + SPOFF + CCFSZ + TF_PIL], %l3
	ldx	[%sp + SPOFF + CCFSZ + TF_Y], %l4

	set	VM_MIN_PROM_ADDRESS, %l5
	cmp	%l1, %l5
	bl,a,pt	%xcc, 1f
	 nop
	set	VM_MAX_PROM_ADDRESS, %l5
	cmp	%l1, %l5
	bg,a,pt	%xcc, 1f
	 nop

	wrpr	%g0, PSTATE_NORMAL, %pstate

	ldx	[%sp + SPOFF + CCFSZ + TF_G6], %g6
	ldx	[%sp + SPOFF + CCFSZ + TF_G7], %g7

1:	wrpr	%g0, PSTATE_ALT, %pstate

	andn	%l0, TSTATE_CWP_MASK, %g1
	mov	%l1, %g2
	mov	%l2, %g3

	wrpr	%l3, 0, %pil
	wr	%l4, 0, %y

	restore

	wrpr	%g0, 2, %tl

	rdpr	%cwp, %g4
	wrpr	%g1, %g4, %tstate
	wrpr	%g2, 0, %tpc
	wrpr	%g3, 0, %tnpc

#if KTR_COMPILE & KTR_TRAP
	CATR(KTR_TRAP, "tl1_ret: td=%#lx pil=%#lx ts=%#lx pc=%#lx sp=%#lx"
	    , %g2, %g3, %g4, 7, 8, 9)
	ldx	[PCPU(CURTHREAD)], %g3
	stx	%g3, [%g2 + KTR_PARM1]
	rdpr	%pil, %g3
	stx	%g3, [%g2 + KTR_PARM2]
	rdpr	%tstate, %g3
	stx	%g3, [%g2 + KTR_PARM3]
	rdpr	%tpc, %g3
	stx	%g3, [%g2 + KTR_PARM4]
	stx	%sp, [%g2 + KTR_PARM5]
9:
#endif

	retry
END(tl1_ret)

/*
 * void tl1_intr(u_int level, u_int mask)
 */
ENTRY(tl1_intr)
	rdpr	%tstate, %l0
	rdpr	%tpc, %l1
	rdpr	%tnpc, %l2
	rdpr	%pil, %l3
	rd	%y, %l4
	rdpr	%wstate, %l5

#if KTR_COMPILE & KTR_INTR
	CATR(KTR_INTR,
	    "tl1_intr: td=%p level=%#x pil=%#lx pc=%#lx sp=%#lx"
	    , %g1, %g2, %g3, 7, 8, 9)
	ldx	[PCPU(CURTHREAD)], %g2
	stx	%g2, [%g1 + KTR_PARM1]
	stx	%o0, [%g1 + KTR_PARM2]
	stx	%l3, [%g1 + KTR_PARM3]
	stx	%l1, [%g1 + KTR_PARM4]
	stx	%i6, [%g1 + KTR_PARM5]
9:
#endif

	wrpr	%o0, 0, %pil
	wr	%o1, 0, %clear_softint

	wrpr	%g0, 1, %tl

	and	%l5, WSTATE_OTHER_MASK, %l5
	wrpr	%l5, WSTATE_KERNEL, %wstate

	stx	%l0, [%sp + SPOFF + CCFSZ + TF_TSTATE]
	stx	%l1, [%sp + SPOFF + CCFSZ + TF_TPC]
	stx	%l2, [%sp + SPOFF + CCFSZ + TF_TNPC]
	stx	%l3, [%sp + SPOFF + CCFSZ + TF_PIL]
	stx	%l4, [%sp + SPOFF + CCFSZ + TF_Y]

	mov	%o0, %l7
	mov	T_INTERRUPT | T_KERNEL, %o1

	stx	%o0, [%sp + SPOFF + CCFSZ + TF_LEVEL]
	stx	%o1, [%sp + SPOFF + CCFSZ + TF_TYPE]

	stx	%i6, [%sp + SPOFF + CCFSZ + TF_O6]
	stx	%i7, [%sp + SPOFF + CCFSZ + TF_O7]

	mov	PCB_REG, %l4
	mov	PCPU_REG, %l5
	wrpr	%g0, PSTATE_NORMAL, %pstate

	stx	%g1, [%sp + SPOFF + CCFSZ + TF_G1]
	stx	%g2, [%sp + SPOFF + CCFSZ + TF_G2]
	stx	%g3, [%sp + SPOFF + CCFSZ + TF_G3]
	stx	%g4, [%sp + SPOFF + CCFSZ + TF_G4]
	stx	%g5, [%sp + SPOFF + CCFSZ + TF_G5]

	mov	%l4, PCB_REG
	mov	%l5, PCPU_REG
	wrpr	%g0, PSTATE_KERNEL, %pstate

	SET(intr_handlers, %l5, %l4)
	sllx	%l7, IH_SHIFT, %l5
	ldx	[%l4 + %l5], %l5
	KASSERT(%l5, "tl1_intr: ih null")
	call	%l5
	 add	%sp, CCFSZ + SPOFF, %o0

	/* %l7 contains PIL */
	SET(intrcnt, %l5, %l4)
	prefetcha [%l4] ASI_N, 1
	SET(pil_countp, %l5, %l6)
	sllx	%l7, 1, %l5
	lduh	[%l5 + %l6], %l5
	sllx	%l5, 3, %l5
	add	%l5, %l4, %l4
	ldx	[%l4], %l5
	inc	%l5
	call	counter_intr_inc
	 stx	%l5, [%l4]

	ldx	[%sp + SPOFF + CCFSZ + TF_Y], %l4

	ldx	[%sp + SPOFF + CCFSZ + TF_G1], %g1
	ldx	[%sp + SPOFF + CCFSZ + TF_G2], %g2
	ldx	[%sp + SPOFF + CCFSZ + TF_G3], %g3
	ldx	[%sp + SPOFF + CCFSZ + TF_G4], %g4
	ldx	[%sp + SPOFF + CCFSZ + TF_G5], %g5

	wrpr	%g0, PSTATE_ALT, %pstate

	andn	%l0, TSTATE_CWP_MASK, %g1
	mov	%l1, %g2
	mov	%l2, %g3
	wrpr	%l3, 0, %pil
	wr	%l4, 0, %y

	restore

	wrpr	%g0, 2, %tl

	rdpr	%cwp, %g4
	wrpr	%g1, %g4, %tstate
	wrpr	%g2, 0, %tpc
	wrpr	%g3, 0, %tnpc

#if KTR_COMPILE & KTR_INTR
	CATR(KTR_INTR, "tl1_intr: td=%#x pil=%#lx ts=%#lx pc=%#lx sp=%#lx"
	    , %g2, %g3, %g4, 7, 8, 9)
	ldx	[PCPU(CURTHREAD)], %g3
	stx	%g3, [%g2 + KTR_PARM1]
	rdpr	%pil, %g3
	stx	%g3, [%g2 + KTR_PARM2]
	rdpr	%tstate, %g3
	stx	%g3, [%g2 + KTR_PARM3]
	rdpr	%tpc, %g3
	stx	%g3, [%g2 + KTR_PARM4]
	stx	%sp, [%g2 + KTR_PARM5]
9:
#endif

	retry
END(tl1_intr)

	.globl	tl_text_end
tl_text_end:
	nop

/*
 * Freshly forked processes come here when switched to for the first time.
 * The arguments to fork_exit() have been setup in the locals, we must move
 * them to the outs.
 */
ENTRY(fork_trampoline)
#if KTR_COMPILE & KTR_PROC
	CATR(KTR_PROC, "fork_trampoline: td=%p (%s) cwp=%#lx"
	    , %g1, %g2, %g3, 7, 8, 9)
	ldx	[PCPU(CURTHREAD)], %g2
	stx	%g2, [%g1 + KTR_PARM1]
	ldx	[%g2 + TD_PROC], %g2
	add	%g2, P_COMM, %g2
	stx	%g2, [%g1 + KTR_PARM2]
	rdpr	%cwp, %g2
	stx	%g2, [%g1 + KTR_PARM3]
9:
#endif
	mov	%l0, %o0
	mov	%l1, %o1
	call	fork_exit
	 mov	%l2, %o2
	ba,a	%xcc, tl0_ret
	 nop
END(fork_trampoline)
