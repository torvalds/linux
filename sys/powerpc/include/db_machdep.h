/*-
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
 *
 *	$OpenBSD: db_machdep.h,v 1.2 1997/03/21 00:48:48 niklas Exp $
 *	$NetBSD: db_machdep.h,v 1.4.22.1 2000/08/05 11:10:43 wiz Exp $
 * $FreeBSD$
 */

/*
 * Machine-dependent defines for new kernel debugger.
 */
#ifndef _POWERPC_DB_MACHDEP_H_
#define	_POWERPC_DB_MACHDEP_H_

#include <vm/vm_param.h>
#include <machine/elf.h>

#define	DB_ELF_SYMBOLS
#define	DB_ELFSIZE	__ELF_WORD_SIZE

typedef	vm_offset_t	db_addr_t;	/* address - unsigned */
typedef	intptr_t	db_expr_t;	/* expression - signed */

#define	PC_REGS(regs)	((db_addr_t)kdb_thrctx->pcb_lr)

#define	BKPT_INST	0x7C810808	/* breakpoint instruction */

#define	BKPT_SIZE	(4)		/* size of breakpoint inst */
#define	BKPT_SET(inst)	(BKPT_INST)

#define db_clear_single_step	kdb_cpu_clear_singlestep
#define db_set_single_step	kdb_cpu_set_singlestep

#if 0
#define	SR_SINGLESTEP	0x400
#define	db_clear_single_step(regs)	((regs)->msr &= ~SR_SINGLESTEP)
#define	db_set_single_step(regs)	((regs)->msr |=  SR_SINGLESTEP)
#endif

#define	T_BREAKPOINT	0xffff
#define	IS_BREAKPOINT_TRAP(type, code)	((type) == T_BREAKPOINT)

#define T_WATCHPOINT	0xeeee
#ifdef T_WATCHPOINT
#define	IS_WATCHPOINT_TRAP(type, code)	((type) == T_WATCHPOINT)
#else
#define	IS_WATCHPOINT_TRAP(type, code)	0
#endif

#define	M_RTS		0xfc0007fe
#define	I_RTS		0x4c000020
#define	M_BC		0xfc000000
#define	I_BC		0x40000000
#define	M_B		0xfc000000
#define	I_B		0x50000000
#define	M_RFI		0xfc0007fe
#define	I_RFI		0x4c000064

#define	inst_trap_return(ins)	(((ins)&M_RFI) == I_RFI)
#define	inst_return(ins)	(((ins)&M_RTS) == I_RTS)
#define	inst_call(ins)		(((ins)&M_BC ) == I_BC  || \
				 ((ins)&M_B  ) == I_B )
#define	inst_load(ins)		0
#define	inst_store(ins)		0

#ifdef __powerpc64__
#define DB_STOFFS(offs)		((offs) & ~DMAP_BASE_ADDRESS)
#endif

#endif	/* _POWERPC_DB_MACHDEP_H_ */
