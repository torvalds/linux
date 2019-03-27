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
 *	from: FreeBSD: src/sys/i386/include/db_machdep.h,v 1.16 1999/10/04
 * $FreeBSD$
 */

#ifndef	_MACHINE_DB_MACHDEP_H_
#define	_MACHINE_DB_MACHDEP_H_

#include <machine/frame.h>
#include <machine/trap.h>
#include <machine/armreg.h>

#define T_BREAKPOINT	(1)
#define T_WATCHPOINT	(2)
typedef vm_offset_t	db_addr_t;
typedef int		db_expr_t;

#define	PC_REGS()	((db_addr_t)kdb_thrctx->pcb_regs.sf_pc)

#define	BKPT_INST	(KERNEL_BREAKPOINT)
#define	BKPT_SIZE	(INSN_SIZE)
#define	BKPT_SET(inst)	(BKPT_INST)

#define	BKPT_SKIP do {							\
	kdb_frame->tf_pc += BKPT_SIZE; \
} while (0)

#if __ARM_ARCH >= 6
#define	db_clear_single_step	kdb_cpu_clear_singlestep
#define	db_set_single_step	kdb_cpu_set_singlestep
#define	db_pc_is_singlestep	kdb_cpu_pc_is_singlestep
#else
#define	SOFTWARE_SSTEP  1
#endif

#define	IS_BREAKPOINT_TRAP(type, code)	(type == T_BREAKPOINT)
#define	IS_WATCHPOINT_TRAP(type, code)	(type == T_WATCHPOINT)

#define	inst_trap_return(ins)	(0)
/* ldmxx reg, {..., pc}
					    01800000  stack mode
					    000f0000  register
					    0000ffff  register list */
/* mov pc, reg
					    0000000f  register */
#define	inst_return(ins)	(((ins) & 0x0e108000) == 0x08108000 || \
				 ((ins) & 0x0ff0fff0) == 0x01a0f000 ||	\
				 ((ins) & 0x0ffffff0) == 0x012fff10) /* bx */
/* bl ...
					    00ffffff  offset>>2 */
#define	inst_call(ins)		(((ins) & 0x0f000000) == 0x0b000000)
/* b ...
					    00ffffff  offset>>2 */
/* ldr pc, [pc, reg, lsl #2]
					    0000000f  register */

#define	inst_branch(ins)	(((ins) & 0x0f000000) == 0x0a000000 || \
				 ((ins) & 0x0fdffff0) == 0x079ff100 || \
				 ((ins) & 0x0cd0f000) == 0x0490f000 || \
				 ((ins) & 0x0ffffff0) == 0x012fff30 || /* blx */ \
				 ((ins) & 0x0de0f000) == 0x0080f000)

#define	inst_load(ins)		(0)
#define	inst_store(ins)		(0)

#define next_instr_address(pc, bd)	((bd) ? (pc) : ((pc) + INSN_SIZE))

#define	DB_ELFSIZE		32

int db_validate_address(vm_offset_t);

u_int branch_taken (u_int insn, db_addr_t pc);

#endif /* !_MACHINE_DB_MACHDEP_H_ */
