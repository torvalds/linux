/*	$OpenBSD: db_trap.c,v 1.30 2019/11/06 07:30:08 mpi Exp $	*/
/*	$NetBSD: db_trap.c,v 1.9 1996/02/05 01:57:18 christos Exp $	*/

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
 * Trap entry point to kernel debugger.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>

#include <ddb/db_run.h>
#include <ddb/db_command.h>
#include <ddb/db_output.h>
#include <ddb/db_sym.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>
#include <ddb/db_var.h>

void
db_trap(int type, int code)
{
	int	bkpt;
	int	watchpt;

	bkpt = IS_BREAKPOINT_TRAP(type, code);
	watchpt = IS_WATCHPOINT_TRAP(type, code);

	if (db_stop_at_pc(&ddb_regs, &bkpt)) {
		if (db_inst_count) {
			db_printf("After %d instructions\n", db_inst_count);
		}
		if (bkpt)
			db_printf("Breakpoint at\t");
		else if (watchpt)
			db_printf("Watchpoint at\t");
		else
			db_printf("Stopped at\t");
		db_dot = PC_REGS(&ddb_regs);
		db_print_loc_and_inst(db_dot);

		if (panicstr != NULL) {
			static int ddb_msg_shown;

			if (! ddb_msg_shown) {
				/* show on-proc threads */
				db_show_all_procs(0, 0, 0, "o");
			}
			/* then the backtrace */
			db_stack_trace_print(db_dot, 0, 14 /* arbitrary */, "",
			    db_printf);

			if (db_print_position() != 0)
				db_printf("\n");
			if (! ddb_msg_shown) {
				db_printf(
"https://www.openbsd.org/ddb.html describes the minimum info required in bug\n"
"reports.  Insufficient info makes it difficult to find and fix bugs.\n");
				ddb_msg_shown = 1;
			}
		}

		db_command_loop();
	}

	db_restart_at_pc(&ddb_regs, watchpt);
}
