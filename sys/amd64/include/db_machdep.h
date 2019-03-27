/*-
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
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_DB_MACHDEP_H_
#define	_MACHINE_DB_MACHDEP_H_

#include <machine/frame.h>
#include <machine/reg.h>
#include <machine/trap.h>

typedef	vm_offset_t	db_addr_t;	/* address - unsigned */
typedef	long		db_expr_t;	/* expression - signed */

#define	PC_REGS()	((db_addr_t)kdb_thrctx->pcb_rip)

#define	BKPT_INST	0xcc		/* breakpoint instruction */
#define	BKPT_SIZE	(1)		/* size of breakpoint inst */
#define	BKPT_SET(inst)	(BKPT_INST)

#define BKPT_SKIP				\
do {						\
	kdb_frame->tf_rip += 1;			\
	kdb_thrctx->pcb_rip += 1;		\
} while(0)

#define	FIXUP_PC_AFTER_BREAK			\
do {						\
	kdb_frame->tf_rip -= 1;			\
	kdb_thrctx->pcb_rip -= 1;		\
} while(0);

#define	db_clear_single_step	kdb_cpu_clear_singlestep
#define	db_set_single_step	kdb_cpu_set_singlestep

/*
 * The debug exception type is copied from %dr6 to 'code' and used to
 * disambiguate single step traps.  Watchpoints have no special support.
 * Our hardware breakpoints are not well integrated with ddb and are too
 * different from watchpoints.  ddb treats them as unknown traps with
 * unknown addresses and doesn't turn them off while it is running.
 */
#define	IS_BREAKPOINT_TRAP(type, code)	((type) == T_BPTFLT)
#define	IS_SSTEP_TRAP(type, code)					\
	((type) == T_TRCTRAP && (code) & DBREG_DR6_BS)
#define	IS_WATCHPOINT_TRAP(type, code)	0

#define	I_CALL		0xe8
#define	I_CALLI		0xff
#define	i_calli(ins)	(((ins)&0xff) == I_CALLI && ((ins)&0x3800) == 0x1000)
#define	I_RET		0xc3
#define	I_IRET		0xcf
#define	i_rex(ins)	(((ins) & 0xff) == 0x41 || ((ins) & 0xff) == 0x43)

#define	inst_trap_return(ins)	(((ins)&0xff) == I_IRET)
#define	inst_return(ins)	(((ins)&0xff) == I_RET)
#define	inst_call(ins)		(((ins)&0xff) == I_CALL || i_calli(ins) || \
				 (i_calli((ins) >> 8) && i_rex(ins)))
#define inst_load(ins)		0
#define inst_store(ins)		0

#endif /* !_MACHINE_DB_MACHDEP_H_ */
