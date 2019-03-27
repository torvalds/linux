/*	$OpenBSD: locore.S,v 1.18 1998/09/15 10:58:53 pefo Exp $	*/
/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Digital Equipment Corporation and Ralph Campbell.
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
 * Copyright (C) 1989 Digital Equipment Corporation.
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies.
 * Digital Equipment Corporation makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * from: Header: /sprite/src/kernel/mach/ds3100.md/RCS/loMem.s,
 *	v 1.1 89/07/11 17:55:04 nelson Exp  SPRITE (DECWRL)
 * from: Header: /sprite/src/kernel/mach/ds3100.md/RCS/machAsm.s,
 *	v 9.2 90/01/29 18:00:39 shirriff Exp  SPRITE (DECWRL)
 * from: Header: /sprite/src/kernel/vm/ds3100.md/vmPmaxAsm.s,
 *	v 1.1 89/07/10 14:27:41 nelson Exp  SPRITE (DECWRL)
 *
 *	from: @(#)locore.s	8.5 (Berkeley) 1/4/94
 *	JNPR: locore.S,v 1.6.2.1 2007/08/29 12:24:49 girish
 * $FreeBSD$
 */

/*
 * FREEBSD_DEVELOPERS_FIXME
 * The start routine below was written for a multi-core CPU
 * with each core being hyperthreaded. This serves as an example
 * for a complex CPU architecture. For a different CPU complex
 * please make necessary changes to read CPU-ID etc.
 * A clean solution would be to have a different locore file for
 * each CPU type.
 */

/*
 *	Contains code that is the first executed at boot time plus
 *	assembly language support routines.
 */

#include <machine/asm.h>
#include <machine/cpu.h>
#include <machine/cpuregs.h>
#include <machine/regnum.h>

#include "assym.inc"

	.data
#ifdef YAMON
GLOBAL(fenvp)
	.space 4			# Assumes mips32?  Is that OK?
#endif

	.set noreorder

	.text

GLOBAL(btext)
ASM_ENTRY(_start)
VECTOR(_locore, unknown)
	/* UNSAFE TO USE a0..a3, need to preserve the args from boot loader */
	mtc0	zero, MIPS_COP_0_CAUSE	# Clear soft interrupts
	
#if defined(CPU_CNMIPS)
	/*
	 * t1: Bits to set explicitly:
	 *	Enable FPU
	 */

	/* Set these bits */
        li	t1, (MIPS_SR_COP_0_BIT | MIPS_SR_PX | MIPS_SR_KX | MIPS_SR_UX | MIPS_SR_SX | MIPS_SR_BEV)

	/* Reset these bits */
        li	t0, ~(MIPS_SR_DE | MIPS_SR_SR | MIPS_SR_ERL | MIPS_SR_EXL | MIPS_SR_INT_IE | MIPS_SR_COP_2_BIT)
#elif defined (CPU_RMI) || defined (CPU_NLM) 
	/* Set these bits */
        li	t1, (MIPS_SR_COP_2_BIT | MIPS_SR_COP_0_BIT | MIPS_SR_KX | MIPS_SR_UX)

	/* Reset these bits */
        li	t0, ~(MIPS_SR_BEV | MIPS_SR_SR | MIPS_SR_INT_IE)
#else
	/*
	 * t0: Bits to preserve if set:
	 * 	Soft reset
	 *	Boot exception vectors (firmware-provided)
	 */
	li	t0, (MIPS_SR_BEV | MIPS_SR_SR)
	/*
	 * t1: Bits to set explicitly:
	 *	Enable FPU
	 */
	li	t1, MIPS_SR_COP_1_BIT
#if defined(__mips_n32) || defined(__mips_n64)
	or	t1, MIPS_SR_FR
#endif
#ifdef __mips_n64
	or	t1, MIPS_SR_KX | MIPS_SR_SX | MIPS_SR_UX
#endif
#endif
	/*
	 * Read coprocessor 0 status register, clear bits not
	 * preserved (namely, clearing interrupt bits), and set
	 * bits we want to explicitly set.
	 */
	mfc0	t2, MIPS_COP_0_STATUS
	and	t2, t0
	or	t2, t1
	mtc0	t2, MIPS_COP_0_STATUS
	COP0_SYNC

	/* Make sure KSEG0 is cached */
	li	t0, MIPS_CCA_CACHED
	mtc0	t0, MIPS_COP_0_CONFIG
	COP0_SYNC

	/*xxximp
	 * now that we pass a0...a3 to the platform_init routine, do we need
	 * to stash this stuff here?
	 */
#ifdef YAMON
	/* Save YAMON boot environment pointer */
	sw	a2, _C_LABEL(fenvp)
#endif

#if defined(CPU_CNMIPS) && defined(SMP)
	.set push
	.set mips32r2
	rdhwr	t2, $0
	beqz	t2, 1f
	nop
	j	octeon_ap_wait
	nop
	.set pop
1:
#endif

#if defined(CPU_MALTA) && defined(SMP)
	.set push
	.set mips32r2
	jal	malta_cpu_configure
	nop
	jal	platform_processor_id
	nop
	beqz	v0, 1f
	nop
	j	malta_ap_wait
	nop
	.set pop
1:
#endif

	/*
	 * Initialize stack and call machine startup.
	 */
	PTR_LA		sp, _C_LABEL(pcpu_space)
	PTR_ADDU	sp, (PAGE_SIZE * 2) - CALLFRAME_SIZ

	REG_S	zero, CALLFRAME_RA(sp)	# Zero out old ra for debugger
	REG_S	zero, CALLFRAME_SP(sp)	# Zero out old fp for debugger

	PTR_LA	gp, _C_LABEL(_gp)

	/* Call the platform-specific startup code. */
	jal	_C_LABEL(platform_start)
	nop

	PTR_LA	sp, _C_LABEL(thread0_st)
	PTR_L	a0, TD_PCB(sp)
	REG_LI	t0, ~7
	and	a0, a0, t0
	PTR_SUBU	sp, a0, CALLFRAME_SIZ

	jal	_C_LABEL(mi_startup)		# mi_startup(frame)
	sw	zero, CALLFRAME_SIZ - 8(sp)	# Zero out old fp for debugger

	PANIC("Startup failed!")

VECTOR_END(_locore)
