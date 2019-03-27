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

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#include "assym.inc"

#include <machine/trap.h>
#include <machine/riscvreg.h>

.macro save_registers el
	addi	sp, sp, -(TF_SIZE)

	sd	ra, (TF_RA)(sp)
	sd	tp, (TF_TP)(sp)

.if \el == 0	/* We came from userspace. Load our pcpu */
	sd	gp, (TF_GP)(sp)
	ld	gp, (TF_SIZE)(sp)
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

#if 0
	/* XXX: temporary test: spin if stack is not kernel one */
.if \el == 1	/* kernel */
	mv	t0, sp
	srli	t0, t0, 63
1:
	beqz	t0, 1b
.endif
#endif

.if \el == 1
	/* Store kernel sp */
	li	t1, TF_SIZE
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
	csrr	t0, stval
	sd	t0, (TF_STVAL)(sp)
	csrr	t0, scause
	sd	t0, (TF_SCAUSE)(sp)
.endm

.macro load_registers el
	ld	t0, (TF_SSTATUS)(sp)
.if \el == 0
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

.if \el == 0
	/* We go to userspace. Load user sp */
	ld	t0, (TF_SP)(sp)
	csrw	sscratch, t0

	/* And store our pcpu */
	sd	gp, (TF_SIZE)(sp)
	ld	gp, (TF_GP)(sp)
.endif

	ld	ra, (TF_RA)(sp)
	ld	tp, (TF_TP)(sp)

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

	addi	sp, sp, (TF_SIZE)
.endm

.macro	do_ast
	/* Disable interrupts */
	csrr	a4, sstatus
1:
	csrci	sstatus, (SSTATUS_SIE)

	ld	a1, PC_CURTHREAD(gp)
	lw	a2, TD_FLAGS(a1)

	li	a3, (TDF_ASTPENDING|TDF_NEEDRESCHED)
	and	a2, a2, a3
	beqz	a2, 2f

	/* Restore interrupts */
	andi	a4, a4, (SSTATUS_SIE)
	csrs	sstatus, a4

	/* Handle the ast */
	mv	a0, sp
	call	_C_LABEL(ast)

	/* Re-check for new ast scheduled */
	j	1b
2:
.endm

ENTRY(cpu_exception_handler)
	csrrw	sp, sscratch, sp
	beqz	sp, 1f
	/* User mode detected */
	csrrw	sp, sscratch, sp
	j	cpu_exception_handler_user
1:
	/* Supervisor mode detected */
	csrrw	sp, sscratch, sp
	j	cpu_exception_handler_supervisor
END(cpu_exception_handler)

ENTRY(cpu_exception_handler_supervisor)
	save_registers 1
	mv	a0, sp
	call	_C_LABEL(do_trap_supervisor)
	load_registers 1
	sret
END(cpu_exception_handler_supervisor)

ENTRY(cpu_exception_handler_user)
	csrrw	sp, sscratch, sp
	save_registers 0
	mv	a0, sp
	call	_C_LABEL(do_trap_user)
	do_ast
	load_registers 0
	csrrw	sp, sscratch, sp
	sret
END(cpu_exception_handler_user)
