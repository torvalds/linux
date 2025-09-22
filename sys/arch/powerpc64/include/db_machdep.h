/*	$OpenBSD: db_machdep.h,v 1.6 2021/08/30 08:11:12 jasper Exp $*/
/*	$NetBSD: db_machdep.h,v 1.13 1996/04/29 20:50:08 leo Exp $	*/

/*
 * Mach Operating System
 * Copyright (c) 1992 Carnegie Mellon University
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

/*
 * Machine-dependent defines for new kernel debugger.
 */
#ifndef _MACHINE_DB_MACHDEP_H_
#define _MACHINE_DB_MACHDEP_H_

#include <sys/types.h>
#include <uvm/uvm_param.h>
#include <machine/psl.h>
#include <machine/trap.h>

typedef long		db_expr_t;	/* expression - signed */
typedef struct trapframe db_regs_t;
extern db_regs_t ddb_regs;		/* register state */

#define PC_REGS(regs)	((regs)->srr0)
#define SET_PC_REGS(regs, value)	PC_REGS(regs) = (value)

#define BKPT_INST	0x7C810808	/* breakpoint instruction */

#define BKPT_SIZE	(4)		/* size of breakpoint inst */
#define BKPT_SET(inst)	(BKPT_INST)

#define db_clear_single_step(regs)	((regs)->srr1 &= ~PSL_SE)
#define db_set_single_step(regs)	((regs)->srr1 |=  PSL_SE)

#define T_BREAKPOINT	0xffff
#define IS_BREAKPOINT_TRAP(type, code)	((type) == T_BREAKPOINT)
#define IS_WATCHPOINT_TRAP(type, code)	0

#define M_RTS		0xfc0007fe
#define I_RTS		0x4c000020
#define M_BC		0xfc000000
#define I_BC		0x40000000
#define M_B		0xfc000000
#define I_B		0x50000000
#define M_RFI		0xfc0007fe
#define I_RFI		0x4c000064

#define inst_trap_return(ins)	(((ins)&M_RFI) == I_RFI)
#define inst_return(ins)	(((ins)&M_RTS) == I_RTS)
#define inst_call(ins)		(((ins)&M_BC ) == I_BC  || \
				 ((ins)&M_B  ) == I_B )

struct trapframe;
void db_ktrap(int, db_regs_t *);

#define DDB_STATE_NOT_RUNNING	0  
#define DDB_STATE_RUNNING	1
#define DDB_STATE_EXITING	2

/*
 * We define some of our own commands
 */
#define DB_MACHINE_COMMANDS

#endif /* _MACHINE_DB_MACHDEP_H_ */
