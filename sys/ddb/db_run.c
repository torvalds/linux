/*	$OpenBSD: db_run.c,v 1.33 2025/07/22 09:09:50 kettenis Exp $	*/
/*	$NetBSD: db_run.c,v 1.8 1996/02/05 01:57:12 christos Exp $	*/

/*
 * Mach Operating System
 * Copyright (c) 1993,1992,1991,1990 Carnegie Mellon University
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
 * 	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

/*
 * Commands to run process.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>

#include <ddb/db_run.h>
#include <ddb/db_break.h>
#include <ddb/db_access.h>

#ifdef SOFTWARE_SSTEP
db_breakpoint_t	db_not_taken_bkpt = 0;
db_breakpoint_t	db_taken_bkpt = 0;
#endif

int		db_inst_count;

#include <ddb/db_watch.h>
#include <ddb/db_output.h>
#include <ddb/db_sym.h>
#include <ddb/db_extern.h>

int	db_run_mode;
#define	STEP_NONE	0
#define	STEP_ONCE	1
#define	STEP_RETURN	2
#define	STEP_CALLT	3
#define	STEP_CONTINUE	4
#define STEP_INVISIBLE	5
#define	STEP_COUNT	6

int	db_sstep_print;
int		db_loop_count;
int		db_call_depth;

int
db_stop_at_pc(db_regs_t *regs, int *is_breakpoint)
{
	vaddr_t		pc, old_pc;
	db_breakpoint_t	bkpt;

	db_clear_breakpoints();
	db_clear_watchpoints();
	old_pc = pc = PC_REGS(regs);

#ifdef	FIXUP_PC_AFTER_BREAK
	if (*is_breakpoint) {
		/*
		 * Breakpoint trap.  Fix up the PC if the
		 * machine requires it.
		 */
		FIXUP_PC_AFTER_BREAK(regs);
		pc = PC_REGS(regs);
	}
#endif

	/*
	 * Now check for a breakpoint at this address.
	 */
	bkpt = db_find_breakpoint(pc);
	if (bkpt) {
		if (--bkpt->count == 0) {
			db_clear_single_step(regs);
			bkpt->count = bkpt->init_count;
			*is_breakpoint = 1;
			return 1;	/* stop here */
		} else {
			return 0;	/* continue */
		}
	} else if (*is_breakpoint
#ifdef SOFTWARE_SSTEP
	    && !((db_taken_bkpt && db_taken_bkpt->address == pc) ||
	    (db_not_taken_bkpt && db_not_taken_bkpt->address == pc))
#endif
	    ) {
#ifdef PC_ADVANCE
		PC_ADVANCE(regs);
#else
# ifdef SET_PC_REGS
		SET_PC_REGS(regs, old_pc);
# else
		PC_REGS(regs) = old_pc;
# endif
#endif
	}
	db_clear_single_step(regs);

	*is_breakpoint = 0;

	if (db_run_mode == STEP_INVISIBLE) {
		db_run_mode = STEP_CONTINUE;
		return 0;	/* continue */
	}
	if (db_run_mode == STEP_COUNT) {
		return 0; /* continue */
	}
	if (db_run_mode == STEP_ONCE) {
		if (--db_loop_count > 0) {
			if (db_sstep_print) {
				db_printf("\t\t");
				db_print_loc_and_inst(pc);
				db_printf("\n");
			}
			return 0;	/* continue */
		}
	}
	if (db_run_mode == STEP_RETURN) {
		db_expr_t ins = db_get_value(pc, sizeof(int), 0);

		/* continue until matching return */

		if (!inst_trap_return(ins) &&
		    (!inst_return(ins) || --db_call_depth != 0)) {
			if (db_sstep_print) {
				if (inst_call(ins) || inst_return(ins)) {
					int i;

					db_printf("[after %6d]     ", db_inst_count);
					for (i = db_call_depth; --i > 0; )
						db_printf("  ");
					db_print_loc_and_inst(pc);
					db_printf("\n");
				}
			}
			if (inst_call(ins))
				db_call_depth++;
			return 0;	/* continue */
		}
	}
	if (db_run_mode == STEP_CALLT) {
		db_expr_t ins = db_get_value(pc, sizeof(int), 0);

		/* continue until call or return */

		if (!inst_call(ins) && !inst_return(ins) &&
		    !inst_trap_return(ins)) {
			return 0;	/* continue */
		}
	}
	db_run_mode = STEP_NONE;
	return 1;
}

void
db_restart_at_pc(db_regs_t *regs, int watchpt)
{
	vaddr_t pc = PC_REGS(regs);

	if ((db_run_mode == STEP_COUNT) || (db_run_mode == STEP_RETURN) ||
	    (db_run_mode == STEP_CALLT)) {
		db_expr_t	ins;

		/*
		 * We are about to execute this instruction,
		 * so count it now.
		 */
		ins = db_get_value(pc, sizeof(int), 0);
		db_inst_count++;
#ifdef	SOFTWARE_SSTEP
		/* XXX works on mips, but... */
		if (inst_branch(ins) || inst_call(ins)) {
			ins = db_get_value(next_instr_address(pc, 1),
			    sizeof(int), 0);
			db_inst_count++;
		}
#endif	/* SOFTWARE_SSTEP */
	}

	if (db_run_mode == STEP_CONTINUE) {
		if (watchpt || db_find_breakpoint(pc)) {
			/*
			 * Step over breakpoint/watchpoint.
			 */
			db_run_mode = STEP_INVISIBLE;
			db_set_single_step(regs);
		} else {
			db_set_breakpoints();
			db_set_watchpoints();
		}
	} else if (db_run_mode != STEP_NONE) {
		db_set_single_step(regs);
	}
}

void
db_single_step(db_regs_t *regs)
{
	if (db_run_mode == STEP_CONTINUE) {
		db_run_mode = STEP_INVISIBLE;
		db_set_single_step(regs);
	}
}

/* single-step */
void
db_single_step_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int	print = 0;

	if (count == -1)
		count = 1;

	if (modif[0] == 'p')
		print = 1;

	db_run_mode = STEP_ONCE;
	db_loop_count = count;
	db_sstep_print = print;
	db_inst_count = 0;

	db_cmd_loop_done = 1;
}

/* trace and print until call/return */
void
db_trace_until_call_cmd(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif)
{
	int	print = 0;

	if (modif[0] == 'p')
		print = 1;

	db_run_mode = STEP_CALLT;
	db_sstep_print = print;
	db_inst_count = 0;

	db_cmd_loop_done = 1;
}

void
db_trace_until_matching_cmd(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif)
{
	int	print = 0;

	if (modif[0] == 'p')
		print = 1;

	db_run_mode = STEP_RETURN;
	db_call_depth = 1;
	db_sstep_print = print;
	db_inst_count = 0;

	db_cmd_loop_done = 1;
}

/* continue */
void
db_continue_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	if (modif[0] == 'c')
		db_run_mode = STEP_COUNT;
	else
		db_run_mode = STEP_CONTINUE;
	db_inst_count = 0;

	db_cmd_loop_done = 1;
}

#ifdef	SOFTWARE_SSTEP
/*
 *	Software implementation of single-stepping.
 *	If your machine does not have a trace mode
 *	similar to the vax or sun ones you can use
 *	this implementation, done for the mips.
 *	Just define the above conditional and provide
 *	the functions/macros defined below.
 *
 * extern int
 *	inst_branch(ins),	returns true if the instruction might branch
 * extern unsigned
 *	branch_taken(ins, pc, getreg_val, regs),
 *				return the address the instruction might
 *				branch to
 *	getreg_val(regs, reg),	return the value of a user register,
 *				as indicated in the hardware instruction
 *				encoding, e.g. 8 for r8
 *
 * next_instr_address(pc, bd)	returns the address of the first
 *				instruction following the one at "pc",
 *				which is either in the taken path of
 *				the branch (bd==1) or not.  This is
 *				for machines (mips) with branch delays.
 *
 *	A single-step may involve at most 2 breakpoints -
 *	one for branch-not-taken and one for branch taken.
 *	If one of these addresses does not already have a breakpoint,
 *	we allocate a breakpoint and save it here.
 *	These breakpoints are deleted on return.
 */

void
db_set_single_step(db_regs_t *regs)
{
	vaddr_t pc = PC_REGS(regs);
#ifndef SOFTWARE_SSTEP_EMUL
	vaddr_t brpc;
	u_int inst;

	/*
	 * User was stopped at pc, e.g. the instruction
	 * at pc was not executed.
	 */
	inst = db_get_value(pc, sizeof(int), 0);
	if (inst_branch(inst) || inst_call(inst) || inst_return(inst)) {
		brpc = branch_taken(inst, pc, getreg_val, regs);
		if (brpc != pc) {	/* self-branches are hopeless */
			db_taken_bkpt = db_set_temp_breakpoint(brpc);
		}
#if 0
		/* XXX this seems like a true bug, no?  */
		pc = next_instr_address(pc, 1);
#endif
	}
#endif /*SOFTWARE_SSTEP_EMUL*/
	pc = next_instr_address(pc, 0);
	db_not_taken_bkpt = db_set_temp_breakpoint(pc);
}

void
db_clear_single_step(db_regs_t *regs)
{
	if (db_taken_bkpt != 0) {
		db_delete_temp_breakpoint(db_taken_bkpt);
		db_taken_bkpt = 0;
	}
	if (db_not_taken_bkpt != 0) {
		db_delete_temp_breakpoint(db_not_taken_bkpt);
		db_not_taken_bkpt = 0;
	}
}

#endif	/* SOFTWARE_SSTEP */
