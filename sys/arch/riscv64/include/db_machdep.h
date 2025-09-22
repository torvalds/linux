/*	$OpenBSD: db_machdep.h,v 1.5 2021/08/30 08:11:12 jasper Exp $	*/

/*
 * Copyright (c) 2019 Brian Bamsch <bbamsch@google.com>
 * Copyright (c) 2015-2016 Ruslan Bukin <br@bsdpad.com>
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
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE_DB_MACHDEP_H_
#define	_MACHINE_DB_MACHDEP_H_

#include <sys/param.h>
#include <uvm/uvm_extern.h>
#include <machine/riscvreg.h>
#include <machine/frame.h>
#include <machine/trap.h>

#define	T_BREAKPOINT	(EXCP_BREAKPOINT)
#define	T_WATCHPOINT	(0)

typedef long		db_expr_t;

typedef trapframe_t	db_regs_t;

extern db_regs_t	ddb_regs;
#define DDB_REGS	(&ddb_regs)

#define	PC_REGS(regs)			((vaddr_t)(regs)->tf_ra)
#define	SET_PC_REGS(regs, value)	(regs)->tf_ra = (register_t)(value)

#define	BKPT_INST	(KERNEL_BREAKPOINT)
#define	BKPT_SIZE	(INSN_SIZE)
#define	BKPT_SET(inst)	(BKPT_INST)

#define	IS_BREAKPOINT_TRAP(type, code)	((type) == T_BREAKPOINT)
#define	IS_WATCHPOINT_TRAP(type, code)	((type) == T_WATCHPOINT)

#define	inst_trap_return(ins)	(ins == 0x10000073)	/* eret */
#define	inst_return(ins)	(ins == 0x00008067)	/* ret */
#define	inst_call(ins)		(((ins) & 0x7f) == 0x6f || \
				 ((ins) & 0x7f) == 0x67)	/* jal, jalr */
#define	inst_branch(ins)	(((ins) & 0x7f) == 0x63)	/* branch */

#define	next_instr_address(pc, bd)	((bd) ? (pc) : ((pc) + INSN_SIZE))

#define DB_MACHINE_COMMANDS

#define SOFTWARE_SSTEP

int db_trapper(vaddr_t, u_int, trapframe_t *, int);
void db_machine_init (void);
vaddr_t db_branch_taken(u_int inst, vaddr_t pc, db_regs_t *regs);

#define branch_taken(ins, pc, fun, regs) \
	db_branch_taken((ins), (pc), (regs))

/* For ddb_state */
#define DDB_STATE_NOT_RUNNING	0
#define DDB_STATE_RUNNING	1
#define DDB_STATE_EXITING	2

#endif /* !_MACHINE_DB_MACHDEP_H_ */
