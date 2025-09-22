/*	$OpenBSD: db_machdep.h,v 1.13 2021/08/30 08:11:12 jasper Exp $	*/
/*	$NetBSD: db_machdep.h,v 1.5 2001/11/22 18:00:00 thorpej Exp $	*/

/*
 * Copyright (c) 1996 Scott K Stevens
 * 
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#ifndef	_ARM_DB_MACHDEP_H_
#define	_ARM_DB_MACHDEP_H_

/*
 * Machine-dependent defines for new kernel debugger.
 */

#include <uvm/uvm_extern.h>
#include <arm/armreg.h>
#include <machine/frame.h>
#include <machine/trap.h>

/* end of mangling */

typedef	long		db_expr_t;	/* expression - signed */

typedef trapframe_t db_regs_t;

extern db_regs_t		ddb_regs;	/* register state */

#define	PC_REGS(regs)	((vaddr_t)(regs)->tf_pc)
#define	SET_PC_REGS(regs, value)	(regs)->tf_pc = (register_t)(value)

#define	BKPT_INST	(KERNEL_BREAKPOINT)	/* breakpoint instruction */
#define	BKPT_SIZE	(INSN_SIZE)		/* size of breakpoint inst */
#define	BKPT_SET(inst)	(BKPT_INST)

/*#define FIXUP_PC_AFTER_BREAK(regs)	((regs)->tf_pc -= BKPT_SIZE)*/

#define T_BREAKPOINT			(1)

#define	IS_BREAKPOINT_TRAP(type, code)	((type) == T_BREAKPOINT)
#define IS_WATCHPOINT_TRAP(type, code)	(0)

#define	inst_trap_return(ins)	(0)
/* ldmxx reg, {..., pc}
					    01800000  stack mode
					    000f0000  register
					    0000ffff  register list */
/* mov pc, reg
					    0000000f  register */
#define	inst_return(ins)	(((ins) & 0x0e108000) == 0x08108000 || \
				 ((ins) & 0x0ff0fff0) == 0x01a0f000)
/* bl ...
					    00ffffff  offset>>2 */
#define	inst_call(ins)		(((ins) & 0x0f000000) == 0x0b000000)
/* b ...
					    00ffffff  offset>>2 */
/* ldr pc, [pc, reg, lsl #2]
					    0000000f  register */
#define	inst_branch(ins)	(((ins) & 0x0f000000) == 0x0a000000 || \
				 ((ins) & 0x0fdffff0) == 0x079ff100)
#define inst_unconditional_flow_transfer(ins)	\
	((((ins) & INSN_COND_MASK) == INSN_COND_AL) && \
	 (inst_branch(ins) || inst_call(ins) || inst_return(ins)))

#define getreg_val			(0)
#define next_instr_address(pc, bd)	((bd) ? (pc) : ((pc) + INSN_SIZE))

#define DB_MACHINE_COMMANDS

#define SOFTWARE_SSTEP

vaddr_t	db_branch_taken(u_int inst, vaddr_t pc, db_regs_t *regs);
int db_ktrap (int, db_regs_t *);
void db_machine_init (void);

#define branch_taken(ins, pc, fun, regs) \
	db_branch_taken((ins), (pc), (regs))

void db_show_frame_cmd(db_expr_t, int, db_expr_t, char *);

#endif	/* _ARM_DB_MACHDEP_H_ */
