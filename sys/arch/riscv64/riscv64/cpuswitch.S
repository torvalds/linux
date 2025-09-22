/*	$OpenBSD: cpuswitch.S,v 1.10 2024/10/17 02:20:53 jsg Exp $	*/

/*
 * Copyright (c) 2015 Dale Rahn <drahn@dalerahn.com>
 * Copyright (c) 2020 Brian Bamsch <bbamsch@google.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <machine/asm.h>
#include "assym.h"

/*
 * cpu_switchto(struct proc *oldproc, struct proc *newproc)
 *      a0	'struct proc *' of the old context
 *      a1	'struct proc *' of the new context
 */
ENTRY(cpu_switchto_asm)
	// check if old context needs to be saved
	beqz	a0, 1f

	// create switchframe
	addi	sp, sp, -SWITCHFRAME_SIZEOF
	sd	s0, (SF_S + 0 * 8)(sp)
	sd	s1, (SF_S + 1 * 8)(sp)
	sd	s2, (SF_S + 2 * 8)(sp)
	sd	s3, (SF_S + 3 * 8)(sp)
	sd	s4, (SF_S + 4 * 8)(sp)
	sd	s5, (SF_S + 5 * 8)(sp)
	sd	s6, (SF_S + 6 * 8)(sp)
	sd	s7, (SF_S + 7 * 8)(sp)
	sd	s8, (SF_S + 8 * 8)(sp)
	sd	s9, (SF_S + 9 * 8)(sp)
	sd	s10, (SF_S + 10 * 8)(sp)
	sd	s11, (SF_S + 11 * 8)(sp)
	sd	ra, SF_RA(sp)

	// store switchframe
	ld	a5, CI_CURPCB(tp)
	sd	sp, PCB_SP(a5)

1:
	RETGUARD_SYMBOL(cpu_switchto)
	RETGUARD_LOAD_RANDOM(cpu_switchto, s0)

	li	a5, SONPROC
	sb	a5, P_STAT(a1)		// Mark new on cpu
	sd	tp, P_CPU(a1)		// Store curcpu
	ld	a5, P_ADDR(a1)		// Load new pcb
	sd	a5, CI_CURPCB(tp)
	sd	a1, CI_CURPROC(tp)

	// Unlike AArch64, RISC-V does not have a dedicated register in which
	// we can also store pcb_tcb. Supervisor must access tcb indirectly.

	ld	s1, PCB_SP(a5)		// load new stack pointer
	mv	a0, a1
	call	pmap_set_satp

	mv	a7, s0			// move retguard random
	mv	sp, s1			// restore stack pointer

	ld	s0, (SF_S + 0 * 8)(sp)
	ld	s1, (SF_S + 1 * 8)(sp)
	ld	s2, (SF_S + 2 * 8)(sp)
	ld	s3, (SF_S + 3 * 8)(sp)
	ld	s4, (SF_S + 4 * 8)(sp)
	ld	s5, (SF_S + 5 * 8)(sp)
	ld	s6, (SF_S + 6 * 8)(sp)
	ld	s7, (SF_S + 7 * 8)(sp)
	ld	s8, (SF_S + 8 * 8)(sp)
	ld	s9, (SF_S + 9 * 8)(sp)
	ld	s10, (SF_S + 10 * 8)(sp)
	ld	s11, (SF_S + 11 * 8)(sp)
	ld	ra, SF_RA(sp)

	RETGUARD_CALC_COOKIE(a7)
	addi	sp, sp, SWITCHFRAME_SIZEOF
	RETGUARD_CHECK(cpu_switchto, a7)
	ret
END(cpu_switch_asm)

ENTRY(proc_trampoline)
	call	proc_trampoline_mi
	mv	a0, s2
	jalr	s1
	la	t0, syscall_return
	jr	t0
END(cpu_switch)
