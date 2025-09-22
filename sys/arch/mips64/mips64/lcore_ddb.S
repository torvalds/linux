/*	$OpenBSD: lcore_ddb.S,v 1.15 2022/01/25 07:08:43 visa Exp $ */

/*
 * Copyright (c) 2001-2003 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/errno.h>
#include <sys/syscall.h>

#include <machine/param.h>
#include <machine/asm.h>
#include <machine/cpu.h>
#include <mips64/mips_cpu.h>
#include <machine/regnum.h>
#include <machine/cpustate.h>
#include <machine/trap.h>

#include "assym.h"

	.set	mips3
	.set	noreorder		# Noreorder is default style!

LEAF(kdbpeekd, 0)
	GET_CPU_INFO(t1, t0)
	PTR_L	t0, CI_CURPROCPADDR(t1)
	li	v0, KT_DDBERR
	lw	t1, PCB_ONFAULT(t0)
	and	v1, a0, 7			# unaligned ?
	bne	v1, zero, 1f
	 sw	v0, PCB_ONFAULT(t0)

	ld	v0, (a0)
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)

1:
	LDHI	v0, 0(a0)
	LDLO	v0, 7(a0)
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)
END(kdbpeekd)

LEAF(kdbpeek, 0)
	GET_CPU_INFO(t1, t0)
	PTR_L	t0, CI_CURPROCPADDR(t1)
	li	v0, KT_DDBERR
	lw	t1, PCB_ONFAULT(t0)
	and	v1, a0, 3			# unaligned ?
	bne	v1, zero, 1f
	 sw	v0, PCB_ONFAULT(t0)

	lwu	v0, (a0)
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)

1:
	LWHI	v0, 0(a0)
	LWLO	v0, 3(a0)
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)
END(kdbpeek)

LEAF(kdbpeekw, 0)
	GET_CPU_INFO(t1, t0)
	PTR_L	t0, CI_CURPROCPADDR(t1)
	li	v0, KT_DDBERR
	lw	t1, PCB_ONFAULT(t0)
	and	v1, a0, 1			# unaligned ?
	bne	v1, zero, 1f
	 sw	v0, PCB_ONFAULT(t0)

	lh	v0, (a0)
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)

1:
	li	v0, -1				# error!
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)
END(kdbpeekw)

LEAF(kdbpeekb, 0)
	GET_CPU_INFO(t1, t0)
	PTR_L	t0, CI_CURPROCPADDR(t1)
	li	v0, KT_DDBERR
	lw	t1, PCB_ONFAULT(t0)
	sw	v0, PCB_ONFAULT(t0)
	lb	v0, 0(a0)
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)
END(kdbpeekb)

	.globl	kt_ddberr
kt_ddberr:
	jr	ra
	 li	v0, -1

LEAF(kdbpoked, 0)
	GET_CPU_INFO(t1, t0)
	PTR_L	t0, CI_CURPROCPADDR(t1)
	li	v0, KT_DDBERR
	lw	t1, PCB_ONFAULT(t0)
	and	v1, a0, 7			# unaligned ?
	bne	v1, zero, 1f
	 sw	v0, PCB_ONFAULT(t0)

	sd	a1, (a0)
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)

1:
	SDHI	a1, 0(a0)
	SDLO	a1, 7(a0)
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)
END(kdbpoked)

LEAF(kdbpoke, 0)
	GET_CPU_INFO(t1, t0)
	PTR_L	t0, CI_CURPROCPADDR(t1)
	li	v0, KT_DDBERR
	lw	t1, PCB_ONFAULT(t0)
	and	v1, a0, 3			# unaligned ?
	bne	v1, zero, 1f
	 sw	v0, PCB_ONFAULT(t0)

	sw	a1, (a0)
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)

1:
	SWHI	a1, 0(a0)
	SWLO	a1, 3(a0)
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)
END(kdbpoke)

LEAF(kdbpokew, 0)
	GET_CPU_INFO(t1, t0)
	PTR_L	t0, CI_CURPROCPADDR(t1)
	li	v0, KT_DDBERR
	lw	t1, PCB_ONFAULT(t0)
	and	v1, a0, 1			# unaligned ?
	bne	v1, zero, 1f
	 sw	v0, PCB_ONFAULT(t0)

	sh	a1, (a0)
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)

1:
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)
END(kdbpokew)

LEAF(kdbpokeb, 0)
	GET_CPU_INFO(t1, t0)
	PTR_L	t0, CI_CURPROCPADDR(t1)
	li	v0, KT_DDBERR
	lw	t1, PCB_ONFAULT(t0)
	sw	v0, PCB_ONFAULT(t0)
	sb	a1, 0(a0)
	jr	ra
	 sw	t1, PCB_ONFAULT(t0)
END(kdbpokeb)

LEAF(db_enter, 0)
	break   BREAK_SOVER_VAL
	jr	ra
	 NOP
END(db_enter)

LEAF(setjmp, 0)
	MFC0	v0, COP_0_STATUS_REG
	MFC0_HAZARD
	REG_S	s0, REGSZ * 0(a0)
	REG_S	s1, REGSZ * 1(a0)
	REG_S	s2, REGSZ * 2(a0)
	REG_S	s3, REGSZ * 3(a0)
	REG_S	s4, REGSZ * 4(a0)
	REG_S	s5, REGSZ * 5(a0)
	REG_S	s6, REGSZ * 6(a0)
	REG_S	s7, REGSZ * 7(a0)
	REG_S	s8, REGSZ * 8(a0)
	REG_S	sp, REGSZ * 9(a0)
	REG_S	ra, REGSZ * 10(a0)
	REG_S	v0, REGSZ * 11(a0)
	jr	ra
	 li	v0, 0			# setjmp return
END(setjmp)

LEAF(longjmp, 0)
	REG_L	v0, REGSZ * 11(a0)
	REG_L	ra, REGSZ * 10(a0)
	REG_L	s0, REGSZ * 0(a0)
	REG_L	s1, REGSZ * 1(a0)
	REG_L	s2, REGSZ * 2(a0)
	REG_L	s3, REGSZ * 3(a0)
	REG_L	s4, REGSZ * 4(a0)
	REG_L	s5, REGSZ * 5(a0)
	REG_L	s6, REGSZ * 6(a0)
	REG_L	s7, REGSZ * 7(a0)
	REG_L	s8, REGSZ * 8(a0)
	REG_L	sp, REGSZ * 9(a0)
	MTC0	v0, COP_0_STATUS_REG
	MTC0_SR_IE_HAZARD
	jr	ra
	 li	v0, 1			# longjmp return
END(longjmp)
