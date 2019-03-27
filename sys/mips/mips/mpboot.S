/*-
 * Copyright (c) 2010 Neelkanth Natu
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

#include <machine/asm.h>
#include <machine/cpu.h>
#include <machine/cpuregs.h>

#include "assym.inc"

	.text
	.set	noat
	.set	noreorder

/* XXX move this to a header file */
#if defined(CPU_CNMIPS)
#define CLEAR_STATUS \
	mfc0    a0, MIPS_COP_0_STATUS   ;\
	li      a2, (MIPS_SR_KX | MIPS_SR_SX | MIPS_SR_UX) ; \
	or      a0, a0, a2	        ; \
	li      a2, ~(MIPS_SR_INT_IE | MIPS_SR_EXL | MIPS_SR_KSU_USER | MIPS_SR_BEV) ; \
	and     a0, a0, a2              ; \
        mtc0    a0, MIPS_COP_0_STATUS
#elif defined(__mips_n64)
#define CLEAR_STATUS \
	li	a0, (MIPS_SR_KX | MIPS_SR_UX) ; \
	mtc0	a0, MIPS_COP_0_STATUS
#else
#define CLEAR_STATUS \
	mtc0	zero, MIPS_COP_0_STATUS
#endif

GLOBAL(mpentry)
	CLEAR_STATUS			/* disable interrupts */

	mtc0	zero, MIPS_COP_0_CAUSE	/* clear soft interrupts */

	li	t0, MIPS_CCA_CACHED	/* make sure kseg0 is cached */
	mtc0	t0, MIPS_COP_0_CONFIG
	COP0_SYNC

	jal	platform_processor_id	/* get the processor number */
	nop
	move	s0, v0

	/*
	 * Initialize stack and call machine startup
	 */
	PTR_LA	sp, _C_LABEL(pcpu_space)
	addiu	sp, (PAGE_SIZE * 2) - CALLFRAME_SIZ
	sll	t0, s0, PAGE_SHIFT + 1
	addu	sp, sp, t0

	/* Zero out old ra and old fp for debugger */
	sw      zero, CALLFRAME_SIZ - 4(sp)
	sw      zero, CALLFRAME_SIZ - 8(sp)

	PTR_LA	gp, _C_LABEL(_gp)

	jal	platform_init_ap
	move	a0, s0
	jal	smp_init_secondary
	move	a0, s0

	PANIC("AP startup failed!")
