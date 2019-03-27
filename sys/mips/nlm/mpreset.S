/*-
 * Copyright 2003-2011 Netlogic Microsystems (Netlogic). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Netlogic Microsystems ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NETLOGIC_BSD
 * $FreeBSD$
 */

#include <machine/asm.h>
#include <machine/cpu.h>
#include <machine/cpuregs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/sys.h>
#include <mips/nlm/hal/cpucontrol.h>

#define SYS_REG_KSEG1(node, reg)	(0xa0000000 + XLP_DEFAULT_IO_BASE + \
		XLP_IO_SYS_OFFSET(node)  + XLP_IO_PCI_HDRSZ + (reg) * 4)
#include "assym.inc"

	.text
	.set	noat
	.set	noreorder
	.set	mips64

#define	MFCR(rt,rs)	.word	((0x1c<<26)|((rs)<<21)|((rt)<<16)|(0x18))
#define	MTCR(rt,rs)	.word	((0x1c<<26)|((rs)<<21)|((rt)<<16)|(0x19))
/*
 * We need to do this to really flush the dcache before splitting it
 */
.macro flush_l1_dcache
	.set	push
	.set	noreorder
	li	$8, LSU_DEBUG_DATA0 /* use register number to handle */
	li	$9, LSU_DEBUG_ADDR  /* different ABIs */
	li	t2, 0		/* index */
	li	t3, 0x1000	/* loop count, 512 sets * 8 whatever? */
1:
	sll	v0, t2, 5
	MTCR(0, 8)
	ori	v1, v0, 0x3	/* way0 | write_enable | write_active */
	MTCR(3, 9)
2:
	MFCR(3, 9)
	andi	v1, 0x1		/* wait for write_active == 0 */
	bnez	v1, 2b
	nop
	MTCR(0, 8)
	ori	v1, v0, 0x7	/* way1 | write_enable | write_active */
	MTCR(3, 9)
3:
	MFCR(3, 9)
	andi	v1, 0x1		/* wait for write_active == 0 */
	bnez	v1, 3b
	nop
	addi	t2, 1
	bne	t3, t2, 1b
	nop
	.set pop
.endm

VECTOR(XLPResetEntry, unknown)
	mfc0	t0, MIPS_COP_0_STATUS
	li	t1, 0x80000
	and	t1, t0, t1
	bnez	t1, nmi_handler
	nop

#ifdef SMP
	/* Reset entry for secordary cores */
	mfc0	t0, MIPS_COP_0_PRID, 1
	srl	t0, t0, 2		/* discard thread id */
	andi	t0, t0, 0x7		/* core id */
	li	t1, 1
	sll	t0, t1, t0
	nor     t0, t0, zero		/* mask with core id bit clear */

	/* clear CPU non-coherent bit */
	li	t2, SYS_REG_KSEG1(0, SYS_CPU_NONCOHERENT_MODE)
	lw	t1, 0(t2)
	and	t1, t1, t0
	sw	t1, 0(t2)
	lw	t1, 0(t2)	/* read-back ensures operation complete */
	sync

	dla	t2, mpentry
	jr	t2
	nop
#endif
	nop
	/* NOT REACHED */
VECTOR_END(XLPResetEntry)


	/* Not yet */
nmi_handler:
	nop
	nop
	j	nmi_handler

#ifdef SMP
	/*
	 * Enable other threads in the core, called from thread 0
	 * of the core
	 */
LEAF(xlp_enable_threads)
	/*
	 * Save and restore callee saved registers of all ABIs
	 * Enabling threads trashes the registers
	 */
	dmtc0	sp, $4, 2	/* SP saved in UserLocal */
	ori	sp, sp, 0x7
	xori	sp, sp, 0x7	/* align 64 bit */
	addiu	sp, sp, -128
	mfc0	t1, MIPS_COP_0_STATUS
	sd	s0, 0(sp)
	sd	s1, 8(sp)
	sd	s2, 16(sp)
	sd	s3, 24(sp)
	sd	s4, 32(sp)
	sd	s5, 40(sp)
	sd	s6, 48(sp)
	sd	s7, 56(sp)
	sd	s8, 64(sp)
	sd	t1, 72(sp)
	sd	gp, 80(sp)
	sd	ra, 88(sp)

	flush_l1_dcache

	/* Use register number to work in o32 and n32 */
	li	$9, ((CPU_BLOCKID_MAP << 8) | MAP_THREADMODE)
	move	$8, a0
	sync
	MTCR(8, 9)
	mfc0	t0, MIPS_COP_0_PRID, 1
	andi	t0, 0x3
	beqz	t0, 2f
	nop
	dla	t1, mpentry	/* child thread, go to hardware init */
	jr	t1
	nop


2:	/*
	 * Parent hardware thread, restore registers, return
	 */
#if 1
	/*
	 * A0 Errata - Write MMU_SETUP after changing thread mode register.
	 */
	li	$9, 0x400
	li	$8, 0
	MTCR(8, 9)
	sll	zero,3		/* ehb */
#endif
	dmfc0	t0, $4, 2	/* SP saved in UserLocal */
	ori	sp, t0, 0x7
	xori	sp, sp, 0x7	/* align 64 bit */
	addiu	sp, sp, -128
	ld	s0, 0(sp)
	ld	s1, 8(sp)
	ld	s2, 16(sp)
	ld	s3, 24(sp)
	ld	s4, 32(sp)
	ld	s5, 40(sp)
	ld	s6, 48(sp)
	ld	s7, 56(sp)
	ld	s8, 64(sp)
	ld	t1, 72(sp)
	ld	gp, 80(sp)
	ld	ra, 88(sp)
	mfc0	t1, MIPS_COP_0_STATUS

	move	sp, t0		/* Restore the real SP */
	jr.hb	ra
	nop
END(xlp_enable_threads)
#endif
