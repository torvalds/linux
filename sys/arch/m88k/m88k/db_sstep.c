/*	$OpenBSD: db_sstep.c,v 1.9 2025/06/26 20:28:07 miod Exp $	*/
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>

#include <ddb/db_access.h>	/* db_get_value() */
#include <ddb/db_break.h>	/* db_breakpoint_t */
#include <ddb/db_run.h>

/*
 * Support routines for software single step.
 *
 * Author: Daniel Stodolsky (danner@cs.cmu.edu)
 *
 */

/*
 * We can not use the MI ddb SOFTWARE_SSTEP facility, since the 88110 will use
 * hardware single stepping.
 * Moreover, our software single stepping implementation is tailor-made for the
 * 88100 and faster than the MI code.
 */

#ifdef M88100

int		inst_branch_or_call(u_int);
vaddr_t		branch_taken(u_int, vaddr_t, db_regs_t *);

db_breakpoint_t db_not_taken_bkpt = 0;
db_breakpoint_t db_taken_bkpt = 0;

/*
 * Returns `1' is the instruction a branch, jump or call instruction
 * (br, bb0, bb1, bcnd, jmp, bsr, jsr)
 */
int
inst_branch_or_call(u_int ins)
{
	/* check high five bits */
	switch (ins >> (32 - 5)) {
	case 0x18: /* br */
	case 0x19: /* bsr */
	case 0x1a: /* bb0 */
	case 0x1b: /* bb1 */
	case 0x1d: /* bcnd */
		return 1;
	case 0x1e: /* could be jmp or jsr */
		if ((ins & 0xfffff3e0) == 0xf400c000)
			return 1;
	}

	return 0;
}

/*
 * branch_taken(instruction, program counter, regs)
 *
 * instruction will be a control flow instruction location at address pc.
 * Branch taken is supposed to return the address to which the instruction
 * would jump if the branch is taken.
 */
vaddr_t
branch_taken(u_int inst, vaddr_t pc, db_regs_t *regs)
{
	u_int regno;

	/*
	 * Quick check of the instruction. Note that we know we are only
	 * invoked if inst_branch_or_call() returns `1', so we do not
	 * need to repeat the jmp and jsr stricter checks here.
	 */
	switch (inst >> (32 - 5)) {
	case 0x18: /* br */
	case 0x19: /* bsr */
		/* signed 26 bit pc relative displacement, shift left two bits */
		inst = (inst & 0x03ffffff) << 2;
		/* check if sign extension is needed */
		if (inst & 0x08000000)
			inst |= 0xf0000000;
		return (pc + inst);

	case 0x1a: /* bb0 */
	case 0x1b: /* bb1 */
	case 0x1d: /* bcnd */
		/* signed 16 bit pc relative displacement, shift left two bits */
		inst = (inst & 0x0000ffff) << 2;
		/* check if sign extension is needed */
		if (inst & 0x00020000)
			inst |= 0xfffc0000;
		return (pc + inst);

	default: /* jmp or jsr */
		regno = inst & 0x1f;
		return (regno == 0 ? 0 : regs->r[regno]);
	}
}

#endif	/* M88100 */

void
db_set_single_step(db_regs_t *regs)
{
#ifdef M88110
	if (CPU_IS88110) {
		/*
		 * On the 88110, we can use the hardware tracing facility...
		 */
		regs->epsr |= PSR_TRACE | PSR_SER;
	}
#endif
#ifdef M88100
	if (CPU_IS88100) {
		/*
		 * ... while the 88100 will use two breakpoints.
		 */
		vaddr_t pc = PC_REGS(regs);
		vaddr_t brpc;
		u_int inst;

		/*
		 * User was stopped at pc, e.g. the instruction
		 * at pc was not executed.
		 */
		db_read_bytes(pc, sizeof(inst), (caddr_t)&inst);

		/*
		 * Find if this instruction may cause a branch, and set up a
		 * breakpoint at the branch location.
		 */
		if (inst_branch_or_call(inst)) {
			brpc = branch_taken(inst, pc, regs);

			/* self-branches are hopeless */
			if (brpc != pc && brpc != 0)
				db_taken_bkpt = db_set_temp_breakpoint(brpc);
		}

		db_not_taken_bkpt = db_set_temp_breakpoint(pc + 4);
	}
#endif
}

void
db_clear_single_step(db_regs_t *regs)
{
#ifdef M88110
	if (CPU_IS88110) {
		regs->epsr &= ~(PSR_TRACE | PSR_SER);
	}
#endif
#ifdef M88100
	if (CPU_IS88100) {
		if (db_taken_bkpt != 0) {
			db_delete_temp_breakpoint(db_taken_bkpt);
			db_taken_bkpt = 0;
		}
		if (db_not_taken_bkpt != 0) {
			db_delete_temp_breakpoint(db_not_taken_bkpt);
			db_not_taken_bkpt = 0;
		}
	}
#endif
}
