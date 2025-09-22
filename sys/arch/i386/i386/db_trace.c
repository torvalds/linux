/*	$OpenBSD: db_trace.c,v 1.50 2025/08/03 11:17:08 sashan Exp $	*/
/*	$NetBSD: db_trace.c,v 1.18 1996/05/03 19:42:01 christos Exp $	*/

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
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/stacktrace.h>
#include <sys/user.h>

#include <machine/db_machdep.h>

#include <ddb/db_sym.h>
#include <ddb/db_access.h>
#include <ddb/db_variables.h>
#include <ddb/db_interface.h>

/*
 * Machine register set.
 */
struct db_variable db_regs[] = {
	{ "ds",		(long *)&ddb_regs.tf_ds,     FCN_NULL },
	{ "es",		(long *)&ddb_regs.tf_es,     FCN_NULL },
	{ "fs",		(long *)&ddb_regs.tf_fs,     FCN_NULL },
	{ "gs",		(long *)&ddb_regs.tf_gs,     FCN_NULL },
	{ "edi",	(long *)&ddb_regs.tf_edi,    FCN_NULL },
	{ "esi",	(long *)&ddb_regs.tf_esi,    FCN_NULL },
	{ "ebp",	(long *)&ddb_regs.tf_ebp,    FCN_NULL },
	{ "ebx",	(long *)&ddb_regs.tf_ebx,    FCN_NULL },
	{ "edx",	(long *)&ddb_regs.tf_edx,    FCN_NULL },
	{ "ecx",	(long *)&ddb_regs.tf_ecx,    FCN_NULL },
	{ "eax",	(long *)&ddb_regs.tf_eax,    FCN_NULL },
	{ "eip",	(long *)&ddb_regs.tf_eip,    FCN_NULL },
	{ "cs",		(long *)&ddb_regs.tf_cs,     FCN_NULL },
	{ "eflags",	(long *)&ddb_regs.tf_eflags, FCN_NULL },
	{ "esp",	(long *)&ddb_regs.tf_esp,    FCN_NULL },
	{ "ss",		(long *)&ddb_regs.tf_ss,     FCN_NULL },
};
struct db_variable *db_eregs = db_regs + nitems(db_regs);

/*
 * Stack trace.
 */
#define	INKERNEL(va)	(((vaddr_t)(va)) >= VM_MIN_KERNEL_ADDRESS)

int db_i386_numargs(struct callframe *);

/*
 * Figure out how many arguments were passed into the frame at "fp".
 */
int
db_i386_numargs(struct callframe *fp)
{
	int	*argp;
	int	inst;
	int	args;
	extern char	etext[];

	argp = (int *)db_get_value((int)&fp->f_retaddr, 4, 0);
	if (argp < (int *)VM_MIN_KERNEL_ADDRESS || argp > (int *)etext) {
		args = 5;
	} else {
		inst = db_get_value((int)argp, 4, 0);
		if ((inst & 0xff) == 0x59)	/* popl %ecx */
			args = 1;
		else if ((inst & 0xffff) == 0xc483)	/* addl %n, %esp */
			args = ((inst >> 16) & 0xff) / 4;
		else
			args = 5;
	}
	return args;
}

void
db_stack_trace_print(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif, int (*pr)(const char *, ...))
{
	struct callframe *frame, *lastframe;
	int		*argp, *arg0;
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

	if (count == -1)
		count = 65535;

	if (trace_proc) {
		p = tfind((pid_t)addr);
		if (p == NULL) {
			(*pr) ("not found\n");
			return;
		}
	}

	if (curcpu()->ci_feature_sefflags_ebx & SEFF0EBX_SMAP) {
		cr4save = rcr4();
		if (cr4save & CR4_SMAP)
			lcr4(cr4save & ~CR4_SMAP);
	} else {
		cr4save = 0;
	}

	if (!have_addr) {
		frame = (struct callframe *)ddb_regs.tf_ebp;
		callpc = (vaddr_t)ddb_regs.tf_eip;
	} else if (trace_proc) {
		frame = (struct callframe *)p->p_addr->u_pcb.pcb_ebp;
		callpc = (vaddr_t)
		    db_get_value((int)&frame->f_retaddr, 4, 0);
	} else {
		frame = (struct callframe *)addr;
		callpc = (vaddr_t)
		    db_get_value((int)&frame->f_retaddr, 4, 0);
	}

	lastframe = 0;
	while (count && frame != 0) {
		int		narg;
		const char *	name;
		db_expr_t	offset;
		Elf_Sym		*sym;

		if (INKERNEL(frame)) {
			sym = db_search_symbol(callpc, DB_STGY_ANY, &offset);
			db_symbol_values(sym, &name, NULL);
		} else {
			sym = NULL;
			name = NULL;
		}

		if (lastframe == 0 && sym == NULL) {
			/* Symbol not found, peek at code */
			int	instr = db_get_value(callpc, 4, 0);

			offset = 1;
			if ((instr & 0x00ffffff) == 0x00e58955 ||
					/* enter: pushl %ebp, movl %esp, %ebp */
			    (instr & 0x0000ffff) == 0x0000e589
					/* enter+1: movl %esp, %ebp */) {
				offset = 0;
			}
		}

		narg = db_ctf_func_numargs(sym);
		if (narg < 0)
			narg = db_i386_numargs(frame);

		if (name == NULL)
			(*pr)("%lx(", callpc);
		else
			(*pr)("%s(", name);

		if (lastframe == 0 && offset == 0 && !have_addr) {
			/*
			 * We have a breakpoint before the frame is set up
			 * Use %esp instead
			 */
			arg0 =
			    &((struct callframe *)(ddb_regs.tf_esp-4))->f_arg0;
		} else {
			arg0 = &frame->f_arg0;
		}

		for (argp = arg0; narg > 0; ) {
			(*pr)("%x", db_get_value((int)argp, 4, 0));
			argp++;
			if (--narg != 0)
				(*pr)(",");
		}
		(*pr)(") at ");
		db_printsym(callpc, DB_STGY_PROC, pr);
		(*pr)("\n");

		if (lastframe == 0 && offset == 0 && !have_addr) {
			/* Frame really belongs to next callpc */
			lastframe = (struct callframe *)(ddb_regs.tf_esp-4);
			callpc = (vaddr_t)
				 db_get_value((int)&lastframe->f_retaddr, 4, 0);
			continue;
		}

		lastframe = frame;
		callpc = db_get_value((int)&frame->f_retaddr, 4, 0);
		frame = (void *)db_get_value((int)&frame->f_frame, 4, 0);

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
	struct callframe *cf;

	if (KERNELMODE(tf->tf_cs, tf->tf_eflags))
		cf = (struct callframe *)((long)&tf->tf_esp - sizeof(long));
	else
		cf = (struct callframe *)(tf->tf_esp - sizeof(long));

	return db_get_value((vaddr_t)&cf->f_retaddr, sizeof(long), 0);
}

vaddr_t
db_get_probe_addr(struct trapframe *tf)
{
	return tf->tf_eip - BKPT_SIZE;
}
