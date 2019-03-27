/*	$NetBSD: db_trace.c,v 1.8 2003/01/17 22:28:48 thorpej Exp $	*/

/*-
 * Copyright (c) 2000, 2001 Ben Harris
 * Copyright (c) 1996 Scott K. Stevens
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
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */
#include "opt_ddb.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/systm.h>


#include <sys/proc.h>
#include <sys/kdb.h>
#include <sys/stack.h>

#include <machine/armreg.h>
#include <machine/asm.h>
#include <machine/cpufunc.h>
#include <machine/db_machdep.h>
#include <machine/debug_monitor.h>
#include <machine/pcb.h>
#include <machine/stack.h>
#include <machine/vmparam.h>

#include <ddb/ddb.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_output.h>

static void
db_stack_trace_cmd(struct unwind_state *state)
{
	const char *name;
	db_expr_t value;
	db_expr_t offset;
	c_db_sym_t sym;
	u_int reg, i;
	char *sep;
	uint16_t upd_mask;
	bool finished;

	finished = false;
	while (!finished) {
		finished = unwind_stack_one(state, 1);

		/* Print the frame details */
		sym = db_search_symbol(state->start_pc, DB_STGY_ANY, &offset);
		if (sym == C_DB_SYM_NULL) {
			value = 0;
			name = "(null)";
		} else
			db_symbol_values(sym, &name, &value);
		db_printf("%s() at ", name);
		db_printsym(state->start_pc, DB_STGY_PROC);
		db_printf("\n");
		db_printf("\t pc = 0x%08x  lr = 0x%08x (", state->start_pc,
		    state->registers[LR]);
		db_printsym(state->registers[LR], DB_STGY_PROC);
		db_printf(")\n");
		db_printf("\t sp = 0x%08x  fp = 0x%08x",
		    state->registers[SP], state->registers[FP]);

		/* Don't print the registers we have already printed */
		upd_mask = state->update_mask &
		    ~((1 << SP) | (1 << FP) | (1 << LR) | (1 << PC));
		sep = "\n\t";
		for (i = 0, reg = 0; upd_mask != 0; upd_mask >>= 1, reg++) {
			if ((upd_mask & 1) != 0) {
				db_printf("%s%sr%d = 0x%08x", sep,
				    (reg < 10) ? " " : "", reg,
				    state->registers[reg]);
				i++;
				if (i == 2) {
					sep = "\n\t";
					i = 0;
				} else
					sep = " ";

			}
		}
		db_printf("\n");

		if (finished)
			break;

		/*
		 * Stop if directed to do so, or if we've unwound back to the
		 * kernel entry point, or if the unwind function didn't change
		 * anything (to avoid getting stuck in this loop forever).
		 * If the latter happens, it's an indication that the unwind
		 * information is incorrect somehow for the function named in
		 * the last frame printed before you see the unwind failure
		 * message (maybe it needs a STOP_UNWINDING).
		 */
		if (state->registers[PC] < VM_MIN_KERNEL_ADDRESS) {
			db_printf("Unable to unwind into user mode\n");
			finished = true;
		} else if (state->update_mask == 0) {
			db_printf("Unwind failure (no registers changed)\n");
			finished = true;
		}
	}
}

void
db_md_list_watchpoints(void)
{

	dbg_show_watchpoint();
}

int
db_md_clr_watchpoint(db_expr_t addr, db_expr_t size)
{

	return (dbg_remove_watchpoint(addr, size));
}

int
db_md_set_watchpoint(db_expr_t addr, db_expr_t size)
{

	return (dbg_setup_watchpoint(addr, size, HW_WATCHPOINT_RW));
}

int
db_trace_thread(struct thread *thr, int count)
{
	struct unwind_state state;
	struct pcb *ctx;

	if (thr != curthread) {
		ctx = kdb_thr_ctx(thr);

		state.registers[FP] = ctx->pcb_regs.sf_r11;
		state.registers[SP] = ctx->pcb_regs.sf_sp;
		state.registers[LR] = ctx->pcb_regs.sf_lr;
		state.registers[PC] = ctx->pcb_regs.sf_pc;

		db_stack_trace_cmd(&state);
	} else
		db_trace_self();
	return (0);
}

void
db_trace_self(void)
{
	struct unwind_state state;
	uint32_t sp;

	/* Read the stack pointer */
	__asm __volatile("mov %0, sp" : "=&r" (sp));

	state.registers[FP] = (uint32_t)__builtin_frame_address(0);
	state.registers[SP] = sp;
	state.registers[LR] = (uint32_t)__builtin_return_address(0);
	state.registers[PC] = (uint32_t)db_trace_self;

	db_stack_trace_cmd(&state);
}
