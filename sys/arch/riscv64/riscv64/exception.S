/*	$OpenBSD: exception.S,v 1.9 2024/10/17 01:57:18 jsg Exp $	*/

/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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

#include "assym.h"
#include <machine/asm.h>
#include <machine/riscvreg.h>

.macro save_registers mode
	addi	sp, sp, -(TRAPFRAME_SIZEOF)

	sd	ra, (TF_RA)(sp)

.if \mode == 0	/* We came from userspace. */
	sd	gp, (TF_GP)(sp)
.option push
.option norelax
	/* Load the kernel's global pointer */
	la	gp, __global_pointer$
.option pop

	/* Load our pcpu */
	sd	tp, (TF_TP)(sp)
	ld	tp, (TRAPFRAME_SIZEOF)(sp)
.endif

	sd	t0, (TF_T + 0 * 8)(sp)
	sd	t1, (TF_T + 1 * 8)(sp)
	sd	t2, (TF_T + 2 * 8)(sp)
	sd	t3, (TF_T + 3 * 8)(sp)
	sd	t4, (TF_T + 4 * 8)(sp)
	sd	t5, (TF_T + 5 * 8)(sp)
	sd	t6, (TF_T + 6 * 8)(sp)

	sd	s0, (TF_S + 0 * 8)(sp)
	sd	s1, (TF_S + 1 * 8)(sp)
	sd	s2, (TF_S + 2 * 8)(sp)
	sd	s3, (TF_S + 3 * 8)(sp)
	sd	s4, (TF_S + 4 * 8)(sp)
	sd	s5, (TF_S + 5 * 8)(sp)
	sd	s6, (TF_S + 6 * 8)(sp)
	sd	s7, (TF_S + 7 * 8)(sp)
	sd	s8, (TF_S + 8 * 8)(sp)
	sd	s9, (TF_S + 9 * 8)(sp)
	sd	s10, (TF_S + 10 * 8)(sp)
	sd	s11, (TF_S + 11 * 8)(sp)

	sd	a0, (TF_A + 0 * 8)(sp)
	sd	a1, (TF_A + 1 * 8)(sp)
	sd	a2, (TF_A + 2 * 8)(sp)
	sd	a3, (TF_A + 3 * 8)(sp)
	sd	a4, (TF_A + 4 * 8)(sp)
	sd	a5, (TF_A + 5 * 8)(sp)
	sd	a6, (TF_A + 6 * 8)(sp)
	sd	a7, (TF_A + 7 * 8)(sp)

.if \mode == 1
	/* Store kernel sp */
	li	t1, TRAPFRAME_SIZEOF
	add	t0, sp, t1
	sd	t0, (TF_SP)(sp)
.else
	/* Store user sp */
	csrr	t0, sscratch
	sd	t0, (TF_SP)(sp)
.endif
	li	t0, 0
	csrw	sscratch, t0
	csrr	t0, sepc
	sd	t0, (TF_SEPC)(sp)
	csrr	t0, sstatus
	sd	t0, (TF_SSTATUS)(sp)
.if \mode == 1
	/* Disable user address access for supervisor mode exceptions. */
	li	t0, SSTATUS_SUM
	csrc	sstatus, t0
.endif
	li	t0, SSTATUS_FS_MASK
	csrc	sstatus, t0
	csrr	t0, stval
	sd	t0, (TF_STVAL)(sp)
	csrr	t0, scause
	sd	t0, (TF_SCAUSE)(sp)
.endm

.macro restore_registers mode
	ld	t0, (TF_SSTATUS)(sp)
.if \mode == 0
	/* Ensure user interrupts will be enabled on eret */
	li	t1, SSTATUS_SPIE
	or	t0, t0, t1
.else
	/*
	 * Disable interrupts for supervisor mode exceptions.
	 * For user mode exceptions we have already done this
	 * in do_ast.
	 */
	li	t1, ~SSTATUS_SIE
	and	t0, t0, t1
.endif
	csrw	sstatus, t0

	ld	t0, (TF_SEPC)(sp)
	csrw	sepc, t0

.if \mode == 0
	/* We go to userspace. Load user sp */
	ld	t0, (TF_SP)(sp)
	csrw	sscratch, t0

	/* Store our pcpu */
	sd	tp, (TRAPFRAME_SIZEOF)(sp)
	ld	tp, (TF_TP)(sp)

	/* And restore the user's global pointer */
	ld	gp, (TF_GP)(sp)
.endif

	ld	ra, (TF_RA)(sp)

	ld	t0, (TF_T + 0 * 8)(sp)
	ld	t1, (TF_T + 1 * 8)(sp)
	ld	t2, (TF_T + 2 * 8)(sp)
	ld	t3, (TF_T + 3 * 8)(sp)
	ld	t4, (TF_T + 4 * 8)(sp)
	ld	t5, (TF_T + 5 * 8)(sp)
	ld	t6, (TF_T + 6 * 8)(sp)

	ld	s0, (TF_S + 0 * 8)(sp)
	ld	s1, (TF_S + 1 * 8)(sp)
	ld	s2, (TF_S + 2 * 8)(sp)
	ld	s3, (TF_S + 3 * 8)(sp)
	ld	s4, (TF_S + 4 * 8)(sp)
	ld	s5, (TF_S + 5 * 8)(sp)
	ld	s6, (TF_S + 6 * 8)(sp)
	ld	s7, (TF_S + 7 * 8)(sp)
	ld	s8, (TF_S + 8 * 8)(sp)
	ld	s9, (TF_S + 9 * 8)(sp)
	ld	s10, (TF_S + 10 * 8)(sp)
	ld	s11, (TF_S + 11 * 8)(sp)

	ld	a0, (TF_A + 0 * 8)(sp)
	ld	a1, (TF_A + 1 * 8)(sp)
	ld	a2, (TF_A + 2 * 8)(sp)
	ld	a3, (TF_A + 3 * 8)(sp)
	ld	a4, (TF_A + 4 * 8)(sp)
	ld	a5, (TF_A + 5 * 8)(sp)
	ld	a6, (TF_A + 6 * 8)(sp)
	ld	a7, (TF_A + 7 * 8)(sp)

	addi	sp, sp, (TRAPFRAME_SIZEOF)
.endm

.macro	do_ast
1:
	/* Disable interrupts */
	csrci	sstatus, (SSTATUS_SIE)

	/* Check for astpending */
	ld	a1, CI_CURPROC(tp)
	beqz	a1, 2f
	lw	a2, P_ASTPENDING(a1)
	beqz	a2, 2f

	sw	x0, P_ASTPENDING(a1)

	/* Enable interrupts */
	csrsi	sstatus, (SSTATUS_SIE)

	/* handle the ast */
	mv	a0, sp
	call	ast
	j	1b
2:
.endm

	/*
	 * cpu_exception_handler needs to be aligned to a 32-bit
	 * boundary, but ENTRY() only enforces 16-bit alignment.
	 */
	.text
	.p2align 2
ENTRY(cpu_exception_handler)
	csrrw	sp, sscratch, sp
	beqz	sp, 1f
	/* User mode detected */
	j	cpu_exception_handler_user
1:
	/* Supervisor mode detected */
	csrrw	sp, sscratch, sp
	j	cpu_exception_handler_supervisor
END(cpu_exception_handler)

ENTRY(cpu_exception_handler_supervisor)
	save_registers 1
	mv	a0, sp
	call	do_trap_supervisor
	restore_registers 1
	sret
END(cpu_exception_handler_supervisor)

ENTRY(cpu_exception_handler_user)
	save_registers 0
	mv	a0, sp
	call	do_trap_user
	do_ast
	restore_registers 0
	csrrw	sp, sscratch, sp
	sret
END(cpu_exception_handler_user)

ENTRY(syscall_return)
	do_ast
	restore_registers 0
	csrrw	sp, sscratch, sp
	sret
END(syscall_return)
