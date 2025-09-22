/*	$OpenBSD: db_trace.c,v 1.10 2024/11/07 16:02:29 miod Exp $	*/
/*	$NetBSD: db_trace.c,v 1.15 1996/02/22 23:23:41 gwr Exp $	*/

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

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/stacktrace.h>
#include <sys/systm.h>
#include <sys/user.h>

#include <machine/db_machdep.h>

#include <ddb/db_access.h>
#include <ddb/db_interface.h>
#include <ddb/db_sym.h>
#include <ddb/db_variables.h>

db_regs_t ddb_regs;

struct db_variable db_regs[] = {
	{ "r0",  (long *)&ddb_regs.fixreg[0],	FCN_NULL },
	{ "r1",  (long *)&ddb_regs.fixreg[1],	FCN_NULL },
	{ "r2",  (long *)&ddb_regs.fixreg[2],	FCN_NULL },
	{ "r3",  (long *)&ddb_regs.fixreg[3],	FCN_NULL },
	{ "r4",  (long *)&ddb_regs.fixreg[4],	FCN_NULL },
	{ "r5",  (long *)&ddb_regs.fixreg[5],	FCN_NULL },
	{ "r6",  (long *)&ddb_regs.fixreg[6],	FCN_NULL },
	{ "r7",  (long *)&ddb_regs.fixreg[7],	FCN_NULL },
	{ "r8",  (long *)&ddb_regs.fixreg[8],	FCN_NULL },
	{ "r9",  (long *)&ddb_regs.fixreg[9],	FCN_NULL },
	{ "r10", (long *)&ddb_regs.fixreg[10],	FCN_NULL },
	{ "r11", (long *)&ddb_regs.fixreg[11],	FCN_NULL },
	{ "r12", (long *)&ddb_regs.fixreg[12],	FCN_NULL },
	{ "r13", (long *)&ddb_regs.fixreg[13],	FCN_NULL },
	{ "r14", (long *)&ddb_regs.fixreg[14],	FCN_NULL },
	{ "r15", (long *)&ddb_regs.fixreg[15],	FCN_NULL },
	{ "r16", (long *)&ddb_regs.fixreg[16],	FCN_NULL },
	{ "r17", (long *)&ddb_regs.fixreg[17],	FCN_NULL },
	{ "r18", (long *)&ddb_regs.fixreg[18],	FCN_NULL },
	{ "r19", (long *)&ddb_regs.fixreg[19],	FCN_NULL },
	{ "r20", (long *)&ddb_regs.fixreg[20],	FCN_NULL },
	{ "r21", (long *)&ddb_regs.fixreg[21],	FCN_NULL },
	{ "r22", (long *)&ddb_regs.fixreg[22],	FCN_NULL },
	{ "r23", (long *)&ddb_regs.fixreg[23],	FCN_NULL },
	{ "r24", (long *)&ddb_regs.fixreg[24],	FCN_NULL },
	{ "r25", (long *)&ddb_regs.fixreg[25],	FCN_NULL },
	{ "r26", (long *)&ddb_regs.fixreg[26],	FCN_NULL },
	{ "r27", (long *)&ddb_regs.fixreg[27],	FCN_NULL },
	{ "r28", (long *)&ddb_regs.fixreg[28],	FCN_NULL },
	{ "r29", (long *)&ddb_regs.fixreg[29],	FCN_NULL },
	{ "r30", (long *)&ddb_regs.fixreg[30],	FCN_NULL },
	{ "r31", (long *)&ddb_regs.fixreg[31],	FCN_NULL },
	{ "lr",  (long *)&ddb_regs.lr,		FCN_NULL },
	{ "cr",  (long *)&ddb_regs.cr,		FCN_NULL },
	{ "xer", (long *)&ddb_regs.xer,		FCN_NULL },
	{ "ctr", (long *)&ddb_regs.ctr,		FCN_NULL },
	{ "iar", (long *)&ddb_regs.srr0,	FCN_NULL },
	{ "msr", (long *)&ddb_regs.srr1,	FCN_NULL },
	{ "dar", (long *)&ddb_regs.dar,		FCN_NULL },
	{ "dsisr", (long *)&ddb_regs.dsisr,	FCN_NULL },
};

struct db_variable *db_eregs = db_regs + nitems(db_regs);

extern vaddr_t trapexit;

/* stdu r1,_(r1) */
#define inst_establish_frame(ins) ((ins & 0xffff0003) == 0xf8210001)

void
db_stack_trace_print(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif, int (*pr)(const char *, ...))
{
	vaddr_t		 callpc, lr, sp, lastsp;
	db_expr_t	 offset;
	const char	*name;
	char		 c, *cp = modif;
	Elf_Sym		*sym;
	int		 has_frame, trace_proc = 0;
	int		 end_trace = 0;

	while ((c = *cp++) != 0) {
		if (c == 't')
			trace_proc = 1;
	}

	if (!have_addr) {
		sp = ddb_regs.fixreg[1];
		callpc = ddb_regs.srr0;
		has_frame = 0;
	} else {
		if (trace_proc) {
			(*pr)("trace/t not yet implemented!\n");
			return;
		} else
			sp = addr;
		/* The 1st return address is in the 2nd frame. */
		db_read_bytes(sp, sizeof(vaddr_t), (char *)&sp);
		db_read_bytes(sp + 16, sizeof(vaddr_t), (char *)&lr);
		callpc = lr - 4;
		has_frame = 1;
	}

	while (count && sp != 0) {
		sym = db_search_symbol(callpc, DB_STGY_ANY, &offset);
		db_symbol_values(sym, &name, NULL);

		/* Guess whether this function has a stack frame. */
		if (!has_frame && sym) {
			vaddr_t iaddr, limit;
			uint32_t ins;

			iaddr = sym->st_value;
			limit = MIN(iaddr + 0x100, callpc);
			for (; iaddr < limit; iaddr += 4) {
				db_read_bytes(iaddr, sizeof(ins),
				    (char *)&ins);
				if (inst_establish_frame(ins)) {
					has_frame = 1;
					break;
				}
			}
		}

		if (name == NULL || strcmp(name, "end") == 0) {
			(*pr)("at 0x%lx", callpc);
		} else {
			db_printsym(callpc, DB_STGY_PROC, pr);
		}
		(*pr)("\n");

		/* Go to the next frame. */
		lastsp = sp;

		if (lr == (vaddr_t)&trapexit) {
			struct trapframe *frame =
			    (struct trapframe *)(sp + 32);

			if ((frame->srr1 & PSL_PR) && frame->exc == EXC_SC) {
				(*pr)("--- syscall (number %ld) ---\n",
				      frame->fixreg[0]);
			} else {
				(*pr)("--- trap (type 0x%x) ---\n",
				      frame->exc);
			}

			if (frame->srr1 & PSL_PR)
				end_trace = 1;

			sp = frame->fixreg[1];
			lr = frame->srr0 + 4;
		} else if (!has_frame) {
			lr = ddb_regs.lr;
			has_frame = 1;
		} else {
			db_read_bytes(sp, sizeof(vaddr_t), (char *)&sp);
			if (sp == 0)
				break;
			if (sp <= lastsp) {
				(*pr)("Bad frame pointer: 0x%lx\n", sp);
				break;
			}
			db_read_bytes(sp + 16, sizeof(vaddr_t), (char *)&lr);
		}
		callpc = lr - 4;

		if (end_trace) {
			(*pr)("End of kernel: 0x%lx lr 0x%lx\n", sp, callpc);
			break;
		}

		--count;
	}
}

extern char _start[], _etext[];

void
stacktrace_save_at(struct stacktrace *st, unsigned int skip)
{
	struct callframe *frame, *lastframe, *limit;
	struct proc *p = curproc;

	st->st_count = 0;

	if (p == NULL)
		return;

	frame = __builtin_frame_address(0);
	limit = (struct callframe *)(p->p_addr + USPACE - FRAMELEN);

	while (st->st_count < STACKTRACE_MAX) {
		if (skip == 0)
			st->st_pc[st->st_count++] = frame->cf_lr;
		else
			skip--;

		lastframe = frame;
		frame = (struct callframe *)frame->cf_sp;

		if (frame <= lastframe)
			break;
		if (frame >= limit)
			break;
		if (frame->cf_lr < (vaddr_t)_start ||
		    frame->cf_lr >= (vaddr_t)_etext)
			break;
	}
}

void
stacktrace_save_utrace(struct stacktrace *st)
{
	st->st_count = 0;
}
