/*-
 * Copyright (c) 2002 Jake Burkholder.
 * Copyright (c) 2008 Marius Strobl <marius@FreeBSD.org>
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

#include <machine/asi.h>
#include <machine/asmacros.h>
#include <machine/intr_machdep.h>
#include <machine/ktr.h>
#include <machine/pstate.h>
#include <machine/smp.h>
#include <machine/ver.h>

#include "assym.inc"

	.register	%g2, #ignore
	.register	%g3, #ignore

	.text
	_ALIGN_TEXT
	/*
	 * Initialize misc. state to known values: interrupts disabled, normal
	 * globals, windows flushed (cr = 0, cs = nwindows - 1), PIL_TICK and
	 * floating point disabled.
	 * Note that some firmware versions don't implement a clean window
	 * trap handler so we unfortunately can't clear the windows by setting
	 * %cleanwin to zero here.
	 */
1:	wrpr	%g0, PSTATE_NORMAL, %pstate
	wrpr	%g0, PIL_TICK, %pil
	wr	%g0, 0, %fprs

	rdpr	%ver, %l7
	srlx	%l7, VER_IMPL_SHIFT, %l7
	sll	%l7, VER_IMPL_SIZE, %l7
	srl	%l7, VER_IMPL_SIZE, %l7
	cmp	%l7, CPU_IMPL_ULTRASPARCIIIp
	bne	%icc, 3f
	 nop

	/*
	 * Relocate the locked entry in it16 slot 0 (if existent)
	 * as part of working around Cheetah+ erratum 34.
	 */

	setx	TD_V | TD_L, %l1, %l0
	/*
	 * We read ASI_ITLB_DATA_ACCESS_REG twice in order to work
	 * around errata of USIII and beyond.
	 */
	ldxa	[%g0] ASI_ITLB_DATA_ACCESS_REG, %g0
	ldxa	[%g0] ASI_ITLB_DATA_ACCESS_REG, %l6
	and	%l6, %l0, %l1
	cmp	%l0, %l1
	bne	%xcc, 3f
	 nop

	/* Flush the mapping of slot 0. */
	ldxa	[%g0] ASI_ITLB_TAG_READ_REG, %l5
	srlx	%l5, TAR_VPN_SHIFT, %l0
	sllx	%l0, TAR_VPN_SHIFT, %l0
	or	%l0, TLB_DEMAP_PRIMARY | TLB_DEMAP_PAGE, %l0
	stxa	%g0, [%l0] ASI_IMMU_DEMAP
	/* The USIII-family ignores the address. */
	flush	%g0

	/*
	 * Search a replacement slot != 0 and enter the data and tag
	 * that formerly were in slot 0.
	 */
	mov	(1 << TLB_DAR_SLOT_SHIFT), %l4
	setx	TD_V, %l1, %l0
	/*
	 * We read ASI_ITLB_DATA_ACCESS_REG twice in order to work
	 * around errata of USIII and beyond.
	 */
2:	ldxa	[%l4] ASI_ITLB_DATA_ACCESS_REG, %g0
	ldxa	[%l4] ASI_ITLB_DATA_ACCESS_REG, %l1
	and	%l1, %l0, %l1
	cmp	%l0, %l1
	be,a	%xcc, 2b
	 add	%l4, (1 << TLB_DAR_SLOT_SHIFT), %l4
	wr	%g0, ASI_IMMU, %asi
	stxa	%l5, [%g0 + AA_IMMU_TAR] %asi
	stxa	%l6, [%l4] ASI_ITLB_DATA_ACCESS_REG
	/* The USIII-family ignores the address. */
	flush	%g0

3:	rd	%pc, %l6
	ldx	[%l6 + (9f-3b)], %l1
	add	%l6, (11f-3b), %l2
	clr	%l3
4:	cmp	%l3, %l1
	be	%xcc, 8f
	 nop
	ldx	[%l2 + TTE_VPN], %l4
	ldx	[%l2 + TTE_DATA], %l5
	srlx	%l4, TV_SIZE_BITS, %l4
	sllx	%l4, PAGE_SHIFT_4M, %l4
	wr	%g0, ASI_DMMU, %asi
	stxa	%l4, [%g0 + AA_DMMU_TAR] %asi
	stxa	%l5, [%g0] ASI_DTLB_DATA_IN_REG
	membar	#Sync

	cmp	%l7, CPU_IMPL_ULTRASPARCIIIp
	bne	%icc, 6f
	 wr	%g0, ASI_IMMU, %asi

	/*
	 * Search an unused slot != 0 and explicitly enter the data
	 * and tag there in order to avoid Cheetah+ erratum 34.
	 */
	mov	(1 << TLB_DAR_SLOT_SHIFT), %l0
	setx	TD_V, %o1, %o0
	/*
	 * We read ASI_ITLB_DATA_ACCESS_REG twice in order to work
	 * around errata of USIII and beyond.
	 */
5:	ldxa	[%l0] ASI_ITLB_DATA_ACCESS_REG, %g0
	ldxa	[%l0] ASI_ITLB_DATA_ACCESS_REG, %o1
	and	%o1, %o0, %o1
	cmp	%o0, %o1
	be,a	%xcc, 5b
	 add	%l0, (1 << TLB_DAR_SLOT_SHIFT), %l0
	sethi	%hi(KERNBASE), %o0
	stxa	%l4, [%g0 + AA_IMMU_TAR] %asi
	stxa	%l5, [%l0] ASI_ITLB_DATA_ACCESS_REG
	flush	%o0
	ba	%xcc, 7f
	 nop

6:	sethi	%hi(KERNBASE), %l0
	stxa	%l4, [%g0 + AA_IMMU_TAR] %asi
	stxa	%l5, [%g0] ASI_ITLB_DATA_IN_REG
	flush	%l0
7:	add	%l2, 1 << TTE_SHIFT, %l2
	add	%l3, 1, %l3
	ba	%xcc, 4b
	 nop
8:	ldx	[%l6 + (10f-3b)], %l1
	jmpl	%l1, %g0
	 nop
	_ALIGN_DATA
9:	.xword	0x0
10:	.xword	0x0
11:

DATA(mp_tramp_code)
	.xword	1b
DATA(mp_tramp_code_len)
	.xword	11b-1b
DATA(mp_tramp_tlb_slots)
	.xword	9b-1b
DATA(mp_tramp_func)
	.xword	10b-1b

/*
 * void mp_startup(void)
 */
ENTRY(mp_startup)
	SET(cpu_start_args, %l1, %l0)

	mov	CPU_TICKSYNC, %l1
	membar	#StoreLoad
	stw	%l1, [%l0 + CSA_STATE]

1:	ldx	[%l0 + CSA_TICK], %l1
	brz	%l1, 1b
	 nop
	wrpr	%l1, 0, %tick

	rdpr	%ver, %l1
	stx	%l1, [%l0 + CSA_VER]

	srlx	%l1, VER_IMPL_SHIFT, %l1
	sll	%l1, VER_IMPL_SIZE, %l1
	srl	%l1, VER_IMPL_SIZE, %l1
	cmp	%l1, CPU_IMPL_SPARC64V
	bl	%icc, 4f
	 nop
	cmp	%l1, CPU_IMPL_ULTRASPARCI
	bl	%icc, 2f
	 nop
	cmp	%l1, CPU_IMPL_ULTRASPARCIII
	bl	%icc, 4f
	 nop
2:	mov	CPU_STICKSYNC, %l2
	membar	#StoreLoad
	stw	%l2, [%l0 + CSA_STATE]

3:	ldx	[%l0 + CSA_STICK], %l2
	brz	%l2, 3b
	 nop
	wr	%l2, 0, %asr24

4:	call	cpu_get_mid
	 mov	%l1, %o0

	/*
	 * Inform the boot processor we have inited.
	 */
	mov	CPU_INIT, %l1
	membar	#LoadStore
	stw	%l1, [%l0 + CSA_STATE]

	/*
	 * Wait till its our turn to bootstrap.
	 */
5:	lduw	[%l0 + CSA_MID], %l1
	cmp	%l1, %o0
	bne	%xcc, 5b
	 nop

	add	%l0, CSA_TTES, %l1
	clr	%l2

	/*
	 * Map the per-CPU pages.
	 */
6:	sllx	%l2, TTE_SHIFT, %l3
	add	%l1, %l3, %l3

	ldx	[%l3 + TTE_VPN], %l4
	ldx	[%l3 + TTE_DATA], %l5

	wr	%g0, ASI_DMMU, %asi
	srlx	%l4, TV_SIZE_BITS, %l4
	sllx	%l4, PAGE_SHIFT_8K, %l4
	stxa	%l4, [%g0 + AA_DMMU_TAR] %asi
	stxa	%l5, [%g0] ASI_DTLB_DATA_IN_REG
	membar	#Sync

	add	%l2, 1, %l2
	cmp	%l2, PCPU_PAGES
	bne	%xcc, 6b
	 nop

	/*
	 * Get onto our per-CPU panic stack, which precedes the struct pcpu
	 * in the per-CPU page.
	 */
	ldx	[%l0 + CSA_PCPU], %l1
	set	PCPU_PAGES * PAGE_SIZE - PC_SIZEOF, %l2
	add	%l1, %l2, %l1
	sub	%l1, SPOFF + CCFSZ, %sp

	/* Initialize global registers. */
	call	cpu_setregs
	 mov	%l1, %o0

#if KTR_COMPILE & KTR_SMP
	CATR(KTR_SMP,
	    "mp_startup: bootstrap cpuid=%d mid=%d pcpu=%#lx data=%#lx sp=%#lx"
	    , %g1, %g2, %g3, 7, 8, 9)
	lduw	[PCPU(CPUID)], %g2
	stx	%g2, [%g1 + KTR_PARM1]
	lduw	[PCPU(MID)], %g2
	stx	%g2, [%g1 + KTR_PARM2]
	stx	%l1, [%g1 + KTR_PARM3]
	stx	%sp, [%g1 + KTR_PARM5]
9:
#endif

	/*
	 * And away we go.  This doesn't return.
	 */
	call	cpu_mp_bootstrap
	 mov	%l1, %o0
	sir
	! NOTREACHED
END(mp_startup)
