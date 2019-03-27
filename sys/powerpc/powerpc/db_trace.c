/*	$FreeBSD$ */
/*	$NetBSD: db_trace.c,v 1.20 2002/05/13 20:30:09 matt Exp $	*/
/*	$OpenBSD: db_trace.c,v 1.3 1997/03/21 02:10:48 niklas Exp $	*/

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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/proc.h>
#include <sys/stack.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <machine/db_machdep.h>
#include <machine/pcb.h>
#include <machine/spr.h>
#include <machine/stack.h>
#include <machine/trap.h>

#include <ddb/ddb.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_variables.h>

static db_varfcn_t db_frame;

#define DB_OFFSET(x)	(db_expr_t *)offsetof(struct trapframe, x)

#ifdef __powerpc64__
#define	CALLOFFSET	8	/* Include TOC reload slot */
#else
#define	CALLOFFSET	4
#endif

struct db_variable db_regs[] = {
	{ "r0",	 DB_OFFSET(fixreg[0]),	db_frame },
	{ "r1",	 DB_OFFSET(fixreg[1]),	db_frame },
	{ "r2",	 DB_OFFSET(fixreg[2]),	db_frame },
	{ "r3",	 DB_OFFSET(fixreg[3]),	db_frame },
	{ "r4",	 DB_OFFSET(fixreg[4]),	db_frame },
	{ "r5",	 DB_OFFSET(fixreg[5]),	db_frame },
	{ "r6",	 DB_OFFSET(fixreg[6]),	db_frame },
	{ "r7",	 DB_OFFSET(fixreg[7]),	db_frame },
	{ "r8",	 DB_OFFSET(fixreg[8]),	db_frame },
	{ "r9",	 DB_OFFSET(fixreg[9]),	db_frame },
	{ "r10", DB_OFFSET(fixreg[10]),	db_frame },
	{ "r11", DB_OFFSET(fixreg[11]),	db_frame },
	{ "r12", DB_OFFSET(fixreg[12]),	db_frame },
	{ "r13", DB_OFFSET(fixreg[13]),	db_frame },
	{ "r14", DB_OFFSET(fixreg[14]),	db_frame },
	{ "r15", DB_OFFSET(fixreg[15]),	db_frame },
	{ "r16", DB_OFFSET(fixreg[16]),	db_frame },
	{ "r17", DB_OFFSET(fixreg[17]),	db_frame },
	{ "r18", DB_OFFSET(fixreg[18]),	db_frame },
	{ "r19", DB_OFFSET(fixreg[19]),	db_frame },
	{ "r20", DB_OFFSET(fixreg[20]),	db_frame },
	{ "r21", DB_OFFSET(fixreg[21]),	db_frame },
	{ "r22", DB_OFFSET(fixreg[22]),	db_frame },
	{ "r23", DB_OFFSET(fixreg[23]),	db_frame },
	{ "r24", DB_OFFSET(fixreg[24]),	db_frame },
	{ "r25", DB_OFFSET(fixreg[25]),	db_frame },
	{ "r26", DB_OFFSET(fixreg[26]),	db_frame },
	{ "r27", DB_OFFSET(fixreg[27]),	db_frame },
	{ "r28", DB_OFFSET(fixreg[28]),	db_frame },
	{ "r29", DB_OFFSET(fixreg[29]),	db_frame },
	{ "r30", DB_OFFSET(fixreg[30]),	db_frame },
	{ "r31", DB_OFFSET(fixreg[31]),	db_frame },
	{ "srr0", DB_OFFSET(srr0),	db_frame },
	{ "srr1", DB_OFFSET(srr1),	db_frame },
	{ "lr",	 DB_OFFSET(lr),		db_frame },
	{ "ctr", DB_OFFSET(ctr),	db_frame },
	{ "cr",	 DB_OFFSET(cr),		db_frame },
	{ "xer", DB_OFFSET(xer),	db_frame },
	{ "dar", DB_OFFSET(dar),	db_frame },
#ifdef AIM
	{ "dsisr", DB_OFFSET(cpu.aim.dsisr),	db_frame },
#endif
#if defined(BOOKE)
	{ "esr", DB_OFFSET(cpu.booke.esr),	db_frame },
#endif
};
struct db_variable *db_eregs = db_regs + nitems(db_regs);

/*
 * register variable handling
 */
static int
db_frame(struct db_variable *vp, db_expr_t *valuep, int op)
{
	register_t *reg;

	if (kdb_frame == NULL)
		return (0);
	reg = (register_t*)((uintptr_t)kdb_frame + (uintptr_t)vp->valuep);
	if (op == DB_VAR_GET)
		*valuep = *reg;
	else
		*reg = *valuep;
	return (1);
}


/*
 *	Frame tracing.
 */
static int
db_backtrace(struct thread *td, db_addr_t fp, int count)
{
	db_addr_t stackframe, lr, *args;
	boolean_t kernel_only = TRUE;
	boolean_t full = FALSE;

#if 0
	{
		register char *cp = modif;
		register char c;

		while ((c = *cp++) != 0) {
			if (c == 't')
				trace_thread = TRUE;
			if (c == 'u')
				kernel_only = FALSE;
			if (c == 'f')
				full = TRUE;
		}
	}
#endif

	stackframe = fp;

	while (!db_pager_quit) {
		if (stackframe < PAGE_SIZE)
			break;

		/*
		 * Locate the next frame by grabbing the backchain ptr
		 * from frame[0]
		 */
		stackframe = *(db_addr_t *)stackframe;

	next_frame:
	    #ifdef __powerpc64__
		/* The saved arg values start at frame[6] */
		args = (db_addr_t *)(stackframe + 48);
	    #else
		/* The saved arg values start at frame[2] */
		args = (db_addr_t *)(stackframe + 8);
	    #endif

		if (stackframe < PAGE_SIZE)
			break;

	        if (count-- == 0)
			break;

		/*
		 * Extract link register from frame and subtract
		 * 4 to convert into calling address (as opposed to
		 * return address)
		 */
	    #ifdef __powerpc64__
		lr = *(db_addr_t *)(stackframe + 16) - 4;
	    #else
		lr = *(db_addr_t *)(stackframe + 4) - 4;
	    #endif
		if ((lr & 3) || (lr < 0x100)) {
			db_printf("saved LR(0x%zx) is invalid.", lr);
			break;
		}

		#ifdef __powerpc64__
		db_printf("0x%016lx: ", stackframe);
		#else
		db_printf("0x%08x: ", stackframe);
		#endif

		/*
		 * The trap code labels the return addresses from the
		 * call to C code as 'trapexit' and 'asttrapexit. Use this
		 * to determine if the callframe has to traverse a saved
		 * trap context
		 */
		if ((lr + CALLOFFSET == (db_addr_t) &trapexit) ||
		    (lr + CALLOFFSET == (db_addr_t) &asttrapexit)) {
			const char *trapstr;
			struct trapframe *tf = (struct trapframe *)(args);
			db_printf("%s ", tf->srr1 & PSL_PR ? "user" : "kernel");
			switch (tf->exc) {
			case EXC_DSI:
				/* XXX take advantage of the union. */
#ifdef BOOKE
				db_printf("DSI %s trap @ %#zx by ",
				    (tf->cpu.booke.esr & ESR_ST) ? "write"
				    : "read", tf->dar);
#else
				db_printf("DSI %s trap @ %#zx by ",
				    (tf->cpu.aim.dsisr & DSISR_STORE) ? "write"
				    : "read", tf->dar);
#endif
				goto print_trap;
			case EXC_ALI:
				db_printf("ALI trap @ %#zx (xSR %#x) ",
				    tf->dar, (uint32_t)tf->cpu.aim.dsisr);
				goto print_trap;
#ifdef __powerpc64__
			case EXC_DSE:
				db_printf("DSE trap @ %#zx by ", tf->dar);
				goto print_trap;
			case EXC_ISE:
				db_printf("ISE trap @ %#zx by ", tf->srr0);
				goto print_trap;
#endif
			case EXC_ISI: trapstr = "ISI"; break;
			case EXC_PGM: trapstr = "PGM"; break;
			case EXC_SC: trapstr = "SC"; break;
			case EXC_EXI: trapstr = "EXI"; break;
			case EXC_MCHK: trapstr = "MCHK"; break;
			case EXC_VEC: trapstr = "VEC"; break;
#if !defined(BOOKE)
			case EXC_FPA: trapstr = "FPA"; break;
			case EXC_BPT: trapstr = "BPT"; break;
			case EXC_TRC: trapstr = "TRC"; break;
			case EXC_RUNMODETRC: trapstr = "RUNMODETRC"; break;
			case EXC_SMI: trapstr = "SMI"; break;
			case EXC_RST: trapstr = "RST"; break;
#endif
			case EXC_FPU: trapstr = "FPU"; break;
			case EXC_DECR: trapstr = "DECR"; break;
			case EXC_PERF: trapstr = "PERF"; break;
			case EXC_VSX: trapstr = "VSX"; break;
			case EXC_SOFT_PATCH: trapstr = "SOFT_PATCH"; break;
			default: trapstr = NULL; break;
			}
			if (trapstr != NULL) {
				db_printf("%s trap by ", trapstr);
			} else {
				db_printf("trap %#zx by ", tf->exc);
			}

		   print_trap:
			lr = (db_addr_t) tf->srr0;
			db_printsym(lr, DB_STGY_ANY);
			db_printf(": srr1=%#zx\n", tf->srr1);
			db_printf("%-10s  r1=%#zx cr=%#x xer=%#x ctr=%#zx",
			    "", tf->fixreg[1], (uint32_t)tf->cr,
			    (uint32_t)tf->xer, tf->ctr);
#ifdef __powerpc64__
			db_printf(" r2=%#zx", tf->fixreg[2]);
#endif
			if (tf->exc == EXC_DSI)
				db_printf(" sr=%#x",
				    (uint32_t)tf->cpu.aim.dsisr);
			db_printf(" frame=%p\n", tf);
			stackframe = (db_addr_t) tf->fixreg[1];
			if (kernel_only && (tf->srr1 & PSL_PR))
				break;
			goto next_frame;
		}

		db_printf("at ");
		db_printsym(lr, DB_STGY_PROC);
		if (full)
			/* Print all the args stored in that stackframe. */
			db_printf("(%zx, %zx, %zx, %zx, %zx, %zx, %zx, %zx)",
				args[0], args[1], args[2], args[3],
				args[4], args[5], args[6], args[7]);
		db_printf("\n");
	}

	return (0);
}

void
db_trace_self(void)
{
	db_addr_t addr;

	addr = (db_addr_t)__builtin_frame_address(0);
	if (addr == 0) {
		db_printf("Null frame address\n");
		return;
	}
	db_backtrace(curthread, *(db_addr_t *)addr, -1);
}

int
db_trace_thread(struct thread *td, int count)
{
	struct pcb *ctx;

	ctx = kdb_thr_ctx(td);
	return (db_backtrace(td, (db_addr_t)ctx->pcb_sp, count));
}
