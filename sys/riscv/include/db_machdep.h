/*-
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

#include <machine/riscvreg.h>
#include <machine/frame.h>
#include <machine/trap.h>

#define	T_BREAKPOINT	(EXCP_BREAKPOINT)
#define	T_WATCHPOINT	(0)

typedef vm_offset_t	db_addr_t;
typedef long		db_expr_t;

#define	PC_REGS()	((db_addr_t)kdb_thrctx->pcb_sepc)

#define	BKPT_INST	(0x00100073)
#define	BKPT_SIZE	(INSN_SIZE)
#define	BKPT_SET(inst)	(BKPT_INST)

#define	BKPT_SKIP do {				\
	kdb_frame->tf_sepc += BKPT_SIZE;	\
} while (0)

#define	db_clear_single_step	kdb_cpu_clear_singlestep
#define	db_set_single_step	kdb_cpu_set_singlestep

#define	IS_BREAKPOINT_TRAP(type, code)	(type == T_BREAKPOINT)
#define	IS_WATCHPOINT_TRAP(type, code)	(type == T_WATCHPOINT)

#define	inst_trap_return(ins)	(ins == 0x10000073)	/* eret */
#define	inst_return(ins)	(ins == 0x00008067)	/* ret */
#define	inst_call(ins)		(((ins) & 0x7f) == 111 || \
				 ((ins) & 0x7f) == 103) /* jal, jalr */

#define	inst_load(ins) ({							\
	uint32_t tmp_instr = db_get_value(PC_REGS(), sizeof(uint32_t), FALSE);	\
	is_load_instr(tmp_instr);						\
})

#define	inst_store(ins) ({							\
	uint32_t tmp_instr = db_get_value(PC_REGS(), sizeof(uint32_t), FALSE);	\
	is_store_instr(tmp_instr);						\
})

#define	is_load_instr(ins)	(((ins) & 0x7f) == 3)
#define	is_store_instr(ins)	(((ins) & 0x7f) == 35)

#define	next_instr_address(pc, bd)	((bd) ? (pc) : ((pc) + 4))

#define	DB_ELFSIZE		64

#endif /* !_MACHINE_DB_MACHDEP_H_ */
