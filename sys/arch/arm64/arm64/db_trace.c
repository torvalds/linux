/*	$OpenBSD: db_trace.c,v 1.20 2025/07/22 09:11:13 kettenis Exp $	*/
/*	$NetBSD: db_trace.c,v 1.8 2003/01/17 22:28:48 thorpej Exp $	*/

/*
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

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/proc.h>
#include <sys/stacktrace.h>
#include <sys/user.h>
#include <arm64/armreg.h>
#include <machine/db_machdep.h>

#include <ddb/db_access.h>
#include <ddb/db_interface.h>
#include <ddb/db_variables.h>
#include <ddb/db_sym.h>
#include <ddb/db_output.h>

db_regs_t ddb_regs;

#define INKERNEL(va)	(((vaddr_t)(va)) & (1ULL << 63))

void
db_stack_trace_print(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif, int (*pr)(const char *, ...))
{
	struct switchframe sf;
	vaddr_t		frame, lastframe, lr, lastlr;
	char		c, *cp = modif;
	db_expr_t	offset;
	Elf_Sym *	sym;
	const char	*name;
	int		kernel_only = 1;
	int		trace_thread = 0;
	struct proc	*p;

	while ((c = *cp++) != 0) {
		if (c == 'u')
			kernel_only = 0;
		if (c == 't')
			trace_thread = 1;
	}

	if (trace_thread) {
		p = tfind((pid_t)addr);
		if (p == NULL) {
			(*pr)("not found\n");
			return;
		}
	}

	if (!have_addr) {
		frame = ddb_regs.tf_x[29];
		lr = ddb_regs.tf_elr;
	} else if (trace_thread) {
		db_read_bytes(p->p_addr->u_pcb.pcb_sp, sizeof(sf), &sf);
		frame = sf.sf_x29;
		lr = sf.sf_lr;
	} else {
		frame = (vaddr_t)db_get_value(addr, 8, 0);
		lr = (vaddr_t)db_get_value(addr + 8, 8, 0);
	}

	while (count-- && frame != 0) {
		lastlr = lr;
		lr = (vaddr_t)db_get_value(frame + 8, 8, 0);

		if (INKERNEL(frame)) {
			sym = db_search_symbol(lastlr, DB_STGY_ANY, &offset);
			db_symbol_values(sym, &name, NULL);
		} else {
			sym = NULL;
			name = NULL;
		}

		if (name == NULL || strcmp(name, "end") == 0) {
			(*pr)("%llx at 0x%lx", lastlr, lr - 4);
		} else {
			(*pr)("%s() at ", name);
			db_printsym(lr - 4, DB_STGY_PROC, pr);
		}
		(*pr)("\n");

		if (name != NULL) {
			if ((strcmp (name, "handle_el0_irq") == 0) ||
			    (strcmp (name, "handle_el1h_irq") == 0) ||
			    (strcmp (name, "handle_el0_fiq") == 0) ||
			    (strcmp (name, "handle_el1h_fiq") == 0)) {
				(*pr)("--- interrupt ---\n");
			} else if (
			    (strcmp (name, "handle_el0_sync") == 0) ||
			    (strcmp (name, "handle_el1h_sync") == 0)) {
				(*pr)("--- trap ---\n");
			}
		}

		lastframe = frame;
		frame = (vaddr_t)db_get_value(frame, 8, 0);

		if (frame == 0) {
			/* end of chain */
			break;
		}

		if (INKERNEL(frame)) {
			/* staying in kernel */
			if (frame <= lastframe) {
				(*pr)("Bad frame pointer: 0x%lx\n", frame);
				break;
			}
		} else if (INKERNEL(lastframe)) {
			/* switch from user to kernel */
			if (kernel_only) {
				(*pr)("end of kernel\n");
				break;	/* kernel stack only */
			}
		} else {
			/* in user */
			if (frame <= lastframe) {
				(*pr)("Bad user frame pointer: 0x%lx\n",
					frame);
				break;
			}
		}

		--count;
	}
}

void
stacktrace_save_at(struct stacktrace *st, unsigned int skip)
{
	struct callframe *frame, *lastframe, *limit;
	struct proc *p = curproc;

	st->st_count = 0;

	if (p == NULL)
		return;

	frame = __builtin_frame_address(0);
	KASSERT(INKERNEL(frame));
	limit = (struct callframe *)STACKALIGN(p->p_addr + USPACE -
	    sizeof(struct trapframe) - 0x10);

	while (st->st_count < STACKTRACE_MAX) {
		if (skip == 0)
			st->st_pc[st->st_count++] = frame->f_lr;
		else
			skip--;

		lastframe = frame;
		frame = frame->f_frame;

		if (frame <= lastframe)
			break;
		if (frame >= limit)
			break;
		if (!INKERNEL(frame->f_lr))
			break;
	}
}

void
stacktrace_save_utrace(struct stacktrace *st)
{
	st->st_count = 0;
}
