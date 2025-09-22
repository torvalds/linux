/*	$OpenBSD: db_machdep.h,v 1.8 2025/07/22 09:20:41 kettenis Exp $	*/
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

#ifndef	_MACHINE_DB_MACHDEP_H_
#define	_MACHINE_DB_MACHDEP_H_

/*
 * Machine-dependent defines for new kernel debugger.
 */

#include <sys/param.h>
#include <uvm/uvm_extern.h>
#include <machine/armreg.h>
#include <machine/frame.h>

/* end of mangling */

typedef	long		db_expr_t;	/* expression - signed */

typedef trapframe_t db_regs_t;

extern db_regs_t		ddb_regs;	/* register state */
#define	DDB_REGS	(&ddb_regs)

#define	PC_REGS(regs)	((vaddr_t)(regs)->tf_elr)
#define	SET_PC_REGS(regs, value)	(regs)->tf_elr = (register_t)(value)

#define	BKPT_INST	0xd4200000		/* breakpoint instruction */
#define	BKPT_SIZE	(INSN_SIZE)		/* size of breakpoint inst */
#define	BKPT_SET(inst)	(BKPT_INST)

#define	db_clear_single_step(regs)	((regs)->tf_spsr &= ~PSR_SS)
#define	db_set_single_step(regs)	((regs)->tf_spsr |= PSR_SS)

#define	IS_BREAKPOINT_TRAP(type, code)	((type) == EXCP_BRK)
#define	IS_WATCHPOINT_TRAP(type, code)	((type) == EXCP_WATCHPT_EL1)

// ALL BROKEN!!!
#define	inst_trap_return(ins)	((ins) == 0 && (ins) == 1)
#define	inst_return(ins)	((ins) == 0 && (ins) == 1)
#define	inst_call(ins)		((ins) == 0 && (ins) == 1)

#define DB_MACHINE_COMMANDS

int db_ktrap(int, db_regs_t *);
void db_machine_init (void);

#define DDB_STATE_NOT_RUNNING	0  
#define DDB_STATE_RUNNING	1
#define DDB_STATE_EXITING	2

#endif	/* _MACHINE_DB_MACHDEP_H_ */
