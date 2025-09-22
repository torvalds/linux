/*	$OpenBSD: db_machdep.h,v 1.11 2021/08/30 08:11:12 jasper Exp $	*/
/*	$NetBSD: db_machdep.h,v 1.12 2006/05/10 06:24:03 skrll Exp $	*/

/*
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

#ifndef	_SH_DB_MACHDEP_H_
#define	_SH_DB_MACHDEP_H_

/*
 * Machine-dependent defines for the kernel debugger.
 */

#include <sys/param.h>
#include <uvm/uvm_extern.h>
#include <sh/trap.h>

typedef	long		db_expr_t;	/* expression - signed */

typedef struct trapframe db_regs_t;
extern db_regs_t	ddb_regs;	/* register state */

#define	PC_REGS(regs)	((vaddr_t)(regs)->tf_spc)
#define PC_ADVANCE(regs) ((regs)->tf_spc += BKPT_SIZE)

#define	BKPT_INST	0xc3c3		/* breakpoint instruction */
#define	BKPT_SIZE	2		/* size of breakpoint inst */
#define	BKPT_SET(inst)	BKPT_INST

#define	FIXUP_PC_AFTER_BREAK(regs)	((regs)->tf_spc -= BKPT_SIZE)

#define	IS_BREAKPOINT_TRAP(type, code)	((type) == EXPEVT_TRAPA)
#define	IS_WATCHPOINT_TRAP(type, code)	(0) /* XXX (msaitoh) */

int db_ktrap(int, int, db_regs_t *);
void db_machine_init (void);
int inst_call(int);
int inst_return(int);
int inst_trap_return(int);

/*
 * We have machine-dependent commands.
 */
#define	DB_MACHINE_COMMANDS

#endif	/* !_SH_DB_MACHDEP_H_ */
