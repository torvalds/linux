/*	$OpenBSD: db_machdep.h,v 1.28 2021/08/30 08:11:12 jasper Exp $	*/

/*
 * Copyright (c) 1997 Niklas Hallqvist.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_MACHINE_DB_MACHDEP_H_
#define	_MACHINE_DB_MACHDEP_H_

#include <uvm/uvm_param.h>

struct opcode {
	enum opc_fmt { OPC_PAL, OPC_RES, OPC_MEM, OPC_OP, OPC_BR } opc_fmt;
	char *opc_name;
	int opc_print;
};
extern struct opcode opcode[];

/* types the generic ddb module needs */
typedef	long db_expr_t;
typedef struct trapframe db_regs_t;

extern db_regs_t	ddb_regs;

#define	PC_REGS(regs)	((vaddr_t)(regs)->tf_regs[FRAME_PC])
#define	SET_PC_REGS(regs, value) (regs)->tf_regs[FRAME_PC] = (unsigned long)(value)

/* Breakpoint related definitions */
#define	BKPT_INST	0x00000080	/* call_pal bpt */
#define	BKPT_SIZE	sizeof(int)
#define	BKPT_SET(inst)	BKPT_INST

#define	IS_BREAKPOINT_TRAP(type, code) \
    ((type) == ALPHA_KENTRY_IF && (code) == ALPHA_IF_CODE_BPT)
#ifdef notyet
#define	IS_WATCHPOINT_TRAP(type, code)	((type) == ALPHA_KENTRY_MM)
#else
#define	IS_WATCHPOINT_TRAP(type, code)	0
#endif

#define	FIXUP_PC_AFTER_BREAK(regs) ((regs)->tf_regs[FRAME_PC] -= sizeof(int))

#define SOFTWARE_SSTEP
#define DB_VALID_BREAKPOINT(addr) db_valid_breakpoint(addr)

/* Hack to skip GCC "unused" warnings. */
#define	inst_trap_return(ins)	((ins) & 0)		/* XXX */
#define	inst_return(ins)	(((ins) & 0xfc000000) == 0x68000000)

int	alpha_debug(unsigned long, unsigned long, unsigned long,
    unsigned long, struct trapframe *);
vaddr_t db_branch_taken(int, vaddr_t, db_regs_t *);
int	db_inst_branch(int);
int	db_inst_call(int);
int	db_inst_load(int);
int	db_inst_return(int);
int	db_inst_trap_return(int);
int	db_inst_unconditional_flow_transfer(int);
u_long	db_register_value(db_regs_t *, int);
int	db_valid_breakpoint(vaddr_t);
int	ddb_trap(unsigned long, unsigned long, unsigned long,
    unsigned long, struct trapframe *);
int	db_ktrap(int, int, db_regs_t *);
vaddr_t next_instr_address(vaddr_t, int);

#if 1
/* Backwards compatibility until we switch all archs to use the db_ prefix */
#define branch_taken(ins, pc, fun, regs) db_branch_taken((ins), (pc), (regs))
#define inst_branch db_inst_branch
#define inst_call db_inst_call
#endif

#define	DB_MACHINE_COMMANDS

#endif	/* _MACHINE_DB_MACHDEP_H_ */
