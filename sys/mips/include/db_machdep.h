/*	$OpenBSD: db_machdep.h,v 1.2 1998/09/15 10:50:12 pefo Exp $ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1998 Per Fogelstrom, Opsycon AB
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed under OpenBSD by
 *	Per Fogelstrom, Opsycon AB, Sweden.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *	JNPR: db_machdep.h,v 1.7 2006/10/16 12:30:34 katta
 * $FreeBSD$
 */

#ifndef	_MIPS_DB_MACHDEP_H_
#define	_MIPS_DB_MACHDEP_H_

#include <machine/frame.h>
#include <machine/trap.h>

typedef struct trapframe db_regs_t;
extern db_regs_t	ddb_regs;	/* register state */

typedef	vm_offset_t	db_addr_t;	/* address - unsigned */
typedef	register_t	db_expr_t;	/* expression - signed */

#define	SOFTWARE_SSTEP		/* Need software single step */
#define	SOFTWARE_SSTEP_EMUL	/* next_instr_address() emulates 100% */
db_addr_t	next_instr_address(db_addr_t, boolean_t);
#define	BKPT_SIZE			(4)
#define	BKPT_SET(ins)			(MIPS_BREAK_DDB)
#define	DB_VALID_BREAKPOINT(addr)	(((addr) & 3) == 0)

#define	IS_BREAKPOINT_TRAP(type, code)	((type) == T_BREAK)
#define	IS_WATCHPOINT_TRAP(type, code)	(0)	/* XXX mips3 watchpoint */

#define	PC_REGS()	((db_addr_t)kdb_thrctx->pcb_regs.pc)
#define	BKPT_SKIP					\
	do {							\
		if((db_get_value(kdb_frame->pc, sizeof(int), FALSE) &	\
		    ~MIPS_BREAK_VAL_MASK) == MIPS_BREAK_INSTR) {	\
			kdb_frame->pc += BKPT_SIZE;			\
			kdb_thrctx->pcb_regs.pc +=  BKPT_SIZE;		\
		}							\
	} while (0);


/*
 *  Test of instructions to see class.
 */
#define	IT_CALL		0x01
#define	IT_BRANCH	0x02
#define	IT_LOAD		0x03
#define	IT_STORE	0x04

#define	inst_branch(i)		(db_inst_type(i) == IT_BRANCH)
#define	inst_trap_return(i)	((i) & 0)
#define	inst_call(i)		(db_inst_type(i) == IT_CALL)
#define	inst_return(i)		((i) == 0x03e00008)
#define	inst_load(i)		(db_inst_type(i) == IT_LOAD)
#define	inst_store(i)		(db_inst_type(i) == IT_STORE)

int db_inst_type(int);
db_addr_t branch_taken(int inst, db_addr_t pc);
int32_t kdbpeek(int *);
int64_t kdbpeekd(int *);

#endif	/* !_MIPS_DB_MACHDEP_H_ */
