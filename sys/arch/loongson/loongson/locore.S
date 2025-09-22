/*	$OpenBSD: locore.S,v 1.9 2021/02/11 14:44:13 visa Exp $ */

/*
 * Copyright (c) 2001-2004 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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

#include "assym.h"

	.set	noreorder		# Noreorder is default style!

	.globl	locore_start
	.ent	locore_start, 0
locore_start:
	mfc0    v0, COP_0_STATUS_REG
	li	v1, ~SR_INT_ENAB
	and	v0, v1
	mtc0    v0, COP_0_STATUS_REG    # disable all interrupts

	mtc0	zero, COP_0_CAUSE_REG	# Clear soft interrupts

	/*
	 * Initialize stack and call machine startup.
	 */
	LA	t0, start - FRAMESZ(CF_SZ)
	PTR_S	ra, CF_RA_OFFS(t0)		# save pmon return address
	PTR_S	sp, 0(t0)			# and stack
	move	sp, t0
	jal	mips_init			# mips_init(argc, argv, envp,
	nop					#    callvec, esym)

	beqz	v0, 1f				# upon failure, return to pmon
	nop

	PTR_S	zero, CF_RA_OFFS(sp)		# Zero out old ra for debugger
	move	sp, v0				# switch to new stack
	jal	main				# main(regs)
	move	a0, zero
	PANIC("Startup failed!")

1:	PTR_L	ra, CF_RA_OFFS(sp)
	PTR_L	sp, 0(sp)
	jr	ra
	nop
	.end	locore_start

#ifdef HIBERNATE

	/* Switch to hibernate resume pagetable */
LEAF(hibernate_activate_resume_pt_machdep, 0)				/* { */
	/* XXX TBD */
	j	ra
END(hibernate_activate_resume_pt_machdep)				/* } */

LEAF(hibernate_switch_stack_machdep, 0)					/* { */
	/* XXX TBD */
	j	ra
END(hibernate_switch_stack_machdep)					/* } */

LEAF(hibernate_resume_machdep, 0)					/* { */
	j	ra
END(hibernate_resume_machdep)					/* } */

#endif /* HIBERNATE */

#ifdef MULTIPROCESSOR

LEAF(hw_cpu_spinup_trampoline, 0)
	/* Disable interrupts. */
	mfc0    v0, COP_0_STATUS_REG
	li	v1, ~SR_INT_ENAB
	and	v0, v1
	mtc0    v0, COP_0_STATUS_REG

	/* Clear any pending soft interrupts. */
	mtc0	zero, COP_0_CAUSE_REG

	LA	t0, cpu_spinup_a0
	ld	a0, 0(t0)
	LA	t0, cpu_spinup_sp
	ld	sp, 0(t0)
	jal	hw_cpu_hatch
	nop
END(hw_cpu_spinup_trampoline)

LEAF(hw_getcurcpu, 0)
	GET_CPU_INFO(v0, v1)
	jr	ra
	nop
END(hw_getcurcpu)

LEAF(hw_setcurcpu, 0)
	dmtc0	a0, COP_0_ERROR_PC
	jr	ra
	nop
END(hw_setcurcpu)

#endif /* MULTIPROCESSOR */
