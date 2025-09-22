/*	$OpenBSD: db_trace.c,v 1.60 2025/08/03 11:17:08 sashan Exp $	*/
/*	$NetBSD: db_trace.c,v 1.1 2003/04/26 18:39:27 fvdl Exp $	*/

/*
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

#include <machine/db_machdep.h>
#include <machine/frame.h>
#include <machine/trap.h>

#include <ddb/db_sym.h>
#include <ddb/db_access.h>
#include <ddb/db_variables.h>
#include <ddb/db_output.h>

/*
 * Machine register set.
 */
struct db_variable db_regs[] = {
	{ "rdi",	(long *)&ddb_regs.tf_rdi,    FCN_NULL },
	{ "rsi",	(long *)&ddb_regs.tf_rsi,    FCN_NULL },
	{ "rbp",	(long *)&ddb_regs.tf_rbp,    FCN_NULL },
	{ "rbx",	(long *)&ddb_regs.tf_rbx,    FCN_NULL },
	{ "rdx",	(long *)&ddb_regs.tf_rdx,    FCN_NULL },
	{ "rcx",	(long *)&ddb_regs.tf_rcx,    FCN_NULL },
	{ "rax",	(long *)&ddb_regs.tf_rax,    FCN_NULL },
	{ "r8",		(long *)&ddb_regs.tf_r8,     FCN_NULL },
	{ "r9",		(long *)&ddb_regs.tf_r9,     FCN_NULL },
	{ "r10",	(long *)&ddb_regs.tf_r10,    FCN_NULL },
	{ "r11",	(long *)&ddb_regs.tf_r11,    FCN_NULL },
	{ "r12",	(long *)&ddb_regs.tf_r12,    FCN_NULL },
	{ "r13",	(long *)&ddb_regs.tf_r13,    FCN_NULL },
	{ "r14",	(long *)&ddb_regs.tf_r14,    FCN_NULL },
	{ "r15",	(long *)&ddb_regs.tf_r15,    FCN_NULL },
	{ "rip",	(long *)&ddb_regs.tf_rip,    FCN_NULL },
	{ "cs",		(long *)&ddb_regs.tf_cs,     FCN_NULL },
	{ "rflags",	(long *)&ddb_regs.tf_rflags, FCN_NULL },
	{ "rsp",	(long *)&ddb_regs.tf_rsp,    FCN_NULL },
	{ "ss",		(long *)&ddb_regs.tf_ss,     FCN_NULL },
};
struct db_variable * db_eregs = db_regs + nitems(db_regs);

/*
 * Stack trace.
 */
#define	INKERNEL(va)	(((vaddr_t)(va)) >= VM_MIN_KERNEL_ADDRESS)


const unsigned long *db_reg_args[6] = {
	(unsigned long *)&ddb_regs.tf_rdi,
	(unsigned long *)&ddb_regs.tf_rsi,
	(unsigned long *)&ddb_regs.tf_rdx,
	(unsigned long *)&ddb_regs.tf_rcx,
	(unsigned long *)&ddb_regs.tf_r8,
	(unsigned long *)&ddb_regs.tf_r9,
};

void
db_stack_trace_print(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif, int (*pr)(const char *, ...))
{
	struct callframe *frame, *lastframe;
	unsigned long	*argp, *arg0;
	vaddr_t		callpc;
	unsigned int	cr4save = CR4_SMEP|CR4_SMAP;
	int		kernel_only = 1;
	int		trace_proc = 0;
	struct proc	*p;

	{
		char *cp = modif;
		char c;

		while ((c = *cp++) != 0) {
			if (c == 't')
				trace_proc = 1;
			if (c == 'u')
				kernel_only = 0;
		}
	}

	if (trace_proc) {
		p = tfind((pid_t)addr);
		if (p == NULL) {
			(*pr) ("not found\n");
			return;
		}
	}

	cr4save = rcr4();
	if (cr4save & CR4_SMAP)
		lcr4(cr4save & ~CR4_SMAP);

	if (!have_addr) {
		frame = (struct callframe *)ddb_regs.tf_rbp;
		callpc = (vaddr_t)ddb_regs.tf_rip;
	} else if (trace_proc) {
		frame = (struct callframe *)p->p_addr->u_pcb.pcb_rbp;
		callpc = (vaddr_t)
		    db_get_value((vaddr_t)&frame->f_retaddr, 8, 0);
		frame = (struct callframe *)frame->f_frame;
	} else {
		frame = (struct callframe *)addr;
		callpc = (vaddr_t)
		    db_get_value((vaddr_t)&frame->f_retaddr, 8, 0);
		frame = (struct callframe *)frame->f_frame;
	}

	lastframe = 0;
	while (count && frame != 0) {
		int		narg;
		unsigned int	i;
		const char *	name;
		db_expr_t	offset;
		Elf_Sym *	sym;

		if (INKERNEL(frame)) {
			sym = db_search_symbol(callpc, DB_STGY_ANY, &offset);
			db_symbol_values(sym, &name, NULL);
		} else {
			sym = NULL;
			name = NULL;
		}

		if (lastframe == 0 && sym == NULL && callpc != 0) {
			/* Symbol not found, peek at code */
			unsigned long instr = db_get_value(callpc, 8, 0);

			offset = 1;
			if (instr == 0xe5894855 ||
					/* enter: pushq %rbp, movq %rsp, %rbp */
			    (instr & 0x00ffffff) == 0x00e58948
					/* enter+1: movq %rsp, %rbp */) {
				offset = 0;
			}
		}

		if ((narg = db_ctf_func_numargs(sym)) < 0)
			narg = 6;

		if (name == NULL)
			(*pr)("%lx(", callpc);
		else
			(*pr)("%s(", name);

		if (lastframe == 0 && offset == 0 && !have_addr) {
			/* We have a breakpoint before the frame is set up */
			for (i = 0; i < narg; i++) {
				(*pr)("%lx", *db_reg_args[i]);
				if (--narg != 0)
					(*pr)(",");
			}

			/* Use %rsp instead */
			arg0 =
			    &((struct callframe *)(ddb_regs.tf_rsp-8))->f_arg0;
		} else {
			argp = (unsigned long *)frame;
			for (i = narg; i > 0; i--) {
				argp--;
				(*pr)("%lx", db_get_value((vaddr_t)argp,
				    sizeof(*argp), 0));
				if (--narg != 0)
					(*pr)(",");
			}

			arg0 = &frame->f_arg0;
		}

		for (argp = arg0; narg > 0; ) {
			(*pr)("%lx", db_get_value((vaddr_t)argp,
			    sizeof(*argp), 0));
			argp++;
			if (--narg != 0)
				(*pr)(",");
		}
		(*pr)(") at ");
		db_printsym(callpc, DB_STGY_PROC, pr);
		(*pr)("\n");

		if (lastframe == 0 && offset == 0 && !have_addr) {
			/* Frame really belongs to next callpc */
			lastframe = (struct callframe *)(ddb_regs.tf_rsp-8);
			callpc = (vaddr_t)
				 db_get_value((vaddr_t)&lastframe->f_retaddr,
				    8, 0);
			continue;
		}

		lastframe = frame;
		callpc = (vaddr_t)db_get_value(
		    (vaddr_t)&frame->f_retaddr, 8, 0);
		frame = (struct callframe *)db_get_value(
		    (vaddr_t)&frame->f_frame, 8, 0);

		if (frame == 0) {
			/* end of chain */
			break;
		}
		if (INKERNEL(frame)) {
			/* staying in kernel */
			if (frame <= lastframe) {
				(*pr)("Bad frame pointer: %p\n", frame);
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
				(*pr)("Bad user frame pointer: %p\n",
					  frame);
				break;
			}
		}
		--count;
	}
	(*pr)("end trace frame: 0x%lx, count: %d\n", frame, count);

	if (cr4save & CR4_SMAP)
		lcr4(cr4save);
}

void
stacktrace_save_at(struct stacktrace *st, unsigned int skip)
{
	struct callframe *frame, *lastframe, *limit;
	struct pcb *pcb = curpcb;

	st->st_count = 0;

	if (pcb == NULL)
		return;

	frame = __builtin_frame_address(0);
	KASSERT(INKERNEL(frame));
	limit = (struct callframe *)((struct trapframe *)pcb->pcb_kstack - 1);

	while (st->st_count < STACKTRACE_MAX) {
		if (skip == 0)
			st->st_pc[st->st_count++] = frame->f_retaddr;
		else
			skip--;

		lastframe = frame;
		frame = frame->f_frame;

		if (frame <= lastframe)
			break;
		if (frame >= limit)
			break;
		if (!INKERNEL(frame->f_retaddr))
			break;
	}
}

void
stacktrace_save_utrace(struct stacktrace *st)
{
	struct callframe f, *frame, *lastframe;
	struct pcb *pcb = curpcb;

	st->st_count = 0;

	if (pcb == NULL)
		return;

	lastframe = NULL;
	frame = __builtin_frame_address(0);
	KASSERT(INKERNEL(frame));

	curcpu()->ci_inatomic++;
	/*
	 * skip kernel frames
	 */
	while (frame != NULL && lastframe < frame &&
	    frame <= (struct callframe *)pcb->pcb_kstack) {
		lastframe = frame;
		frame = frame->f_frame;
	}

	/*
	 * start saving userland frames
	 */
	if (lastframe != NULL)
		st->st_pc[st->st_count++] = lastframe->f_retaddr;

	while (frame != NULL && st->st_count < STACKTRACE_MAX) {
		if (copyin(frame, &f, sizeof(f)) != 0) {
			/*
			 * If the frame pointer read from the previous frame
			 * is invalid, assume the return address we read
			 * from that frame is invalid as well.
			 */
			if (st->st_count == 0)
				st->st_pc[0] = 0;
			else
				st->st_count--;
			break;
		}
		st->st_pc[st->st_count++] = f.f_retaddr;
		frame = f.f_frame;
	}
	curcpu()->ci_inatomic--;
}

vaddr_t
db_get_pc(struct trapframe *tf)
{
	struct callframe *cf = (struct callframe *)(tf->tf_rsp - sizeof(long));

	return db_get_value((vaddr_t)&cf->f_retaddr, sizeof(long), 0);
}

vaddr_t
db_get_probe_addr(struct trapframe *tf)
{
	return tf->tf_rip - BKPT_SIZE;
}
