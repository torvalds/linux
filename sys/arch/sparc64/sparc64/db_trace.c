/*	$OpenBSD: db_trace.c,v 1.28 2024/11/07 16:02:29 miod Exp $	*/
/*	$NetBSD: db_trace.c,v 1.23 2001/07/10 06:06:16 eeh Exp $ */

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
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/stacktrace.h>
#include <sys/user.h>
#include <machine/db_machdep.h>
#include <machine/ctlreg.h>

#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_interface.h>
#include <ddb/db_output.h>

void db_dump_fpstate(db_expr_t, int, db_expr_t, char *);
void db_dump_window(db_expr_t, int, db_expr_t, char *);
void db_dump_stack(db_expr_t, int, db_expr_t, char *);
void db_dump_trap(db_expr_t, int, db_expr_t, char *);
void db_dump_ts(db_expr_t, int, db_expr_t, char *);
void db_print_window(u_int64_t);

#define	KLOAD(x)	probeget((paddr_t)(u_long)&(x), ASI_PRIMARY, sizeof(x))
#define ULOAD(x)	probeget((paddr_t)(u_long)&(x), ASI_AIUS, sizeof(x))

void
db_stack_trace_print(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif, int (*pr)(const char *, ...))
{
	vaddr_t		frame;
	int		kernel_only = 1;
	int		trace_thread = 0;
	char		c, *cp = modif;

	while ((c = *cp++) != 0) {
		if (c == 't')
			trace_thread = 1;
		if (c == 'u')
			kernel_only = 0;
	}

	if (!have_addr)
		frame = (vaddr_t)DDB_TF->tf_out[6];
	else {
		if (trace_thread) {
			struct proc *p;
			struct user *u;
			(*pr)("trace: pid %d ", (int)addr);
			p = tfind(addr);
			if (p == NULL) {
				(*pr)("not found\n");
				return;
			}
			u = p->p_addr;
			frame = (vaddr_t)u->u_pcb.pcb_sp;
			(*pr)("at %p\n", frame);
		} else {
			write_all_windows();

			frame = (vaddr_t)addr - BIAS;
		}
	}

	if ((frame & 1) == 0) {
		db_printf("WARNING: corrupt frame at %lx\n", frame);
		return;
	}

	while (count--) {
		int		i;
		db_expr_t	offset;
		const char	*name;
		vaddr_t		pc;
		struct frame	*f64;

		/*
		 * Switch to frame that contains arguments
		 */

		f64 = (struct frame *)(frame + BIAS);
		pc = (vaddr_t)KLOAD(f64->fr_pc);

		frame = KLOAD(f64->fr_fp);

		if (kernel_only) {
			if (pc < KERNBASE || pc >= KERNEND)
				break;
			if (frame < KERNBASE)
				break;
		} else {
			if (frame == 0 || frame == (vaddr_t)-1)
				break;
		}

		db_find_sym_and_offset(pc, &name, &offset);

		if (name == NULL)
			(*pr)("%lx(", pc);
		else
			(*pr)("%s(", name);

		if ((frame & 1) == 0) {
			db_printf(")\nWARNING: corrupt frame at %lx\n", frame);
			break;
		}

		/*
		 * Print %i0..%i5; hope these still reflect the
		 * actual arguments somewhat...
		 */
		f64 = (struct frame *)(frame + BIAS);
		for (i = 0; i < 5; i++)
			(*pr)("%lx, ", (long)KLOAD(f64->fr_arg[i]));
		(*pr)("%lx) at ", (long)KLOAD(f64->fr_arg[i]));
		db_printsym(pc, DB_STGY_PROC, pr);
		(*pr)("\n");
	}
}

void
stacktrace_save_at(struct stacktrace *st, unsigned int skip)
{
	struct frame	*f64;
	vaddr_t		pc;
	vaddr_t		frame;

	write_all_windows();

	frame = (vaddr_t)__builtin_frame_address(0) - BIAS;
	if ((frame & 1) == 0)
		return;

	st->st_count = 0;
	while (st->st_count < STACKTRACE_MAX) {
		f64 = (struct frame *)(frame + BIAS);
		pc = (vaddr_t)KLOAD(f64->fr_pc);

		frame = KLOAD(f64->fr_fp);

		if (pc < KERNBASE || pc >= KERNEND)
			break;
		if (frame < KERNBASE)
			break;
		if ((frame & 1) == 0)
			break;

		if (skip == 0)
			st->st_pc[st->st_count++] = pc;
		else
			skip--;
	}
}

void
stacktrace_save_utrace(struct stacktrace *st)
{
	st->st_count = 0;
}

void
db_dump_window(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int i;
	u_int64_t frame = DDB_TF->tf_out[6];

	/* Addr is really window number */
	if (!have_addr)
		addr = 0;

	/* Traverse window stack */
	for (i = 0; i < addr && frame; i++) {
		if ((frame & 1) == 0)
			break;
		frame = ((struct frame *)(frame + BIAS))->fr_fp;
	}

	if ((frame & 1) == 0) {
		db_printf("WARNING: corrupt frame at %llx\n", frame);
		return;
	}

	db_printf("Window %lx ", addr);
	db_print_window(frame);
}

void
db_print_window(u_int64_t frame)
{
	struct frame *f = (struct frame *)(frame + BIAS);

	db_printf("frame %p locals, ins:\n", f);
	db_printf("%llx %llx %llx %llx ",
		  (unsigned long long)f->fr_local[0],
		  (unsigned long long)f->fr_local[1],
		  (unsigned long long)f->fr_local[2],
		  (unsigned long long)f->fr_local[3]);
	db_printf("%llx %llx %llx %llx\n",
		  (unsigned long long)f->fr_local[4],
		  (unsigned long long)f->fr_local[5],
		  (unsigned long long)f->fr_local[6],
		  (unsigned long long)f->fr_local[7]);
	db_printf("%llx %llx %llx %llx ",
		  (unsigned long long)f->fr_arg[0],
		  (unsigned long long)f->fr_arg[1],
		  (unsigned long long)f->fr_arg[2],
		  (unsigned long long)f->fr_arg[3]);
	db_printf("%llx %llx %llx=sp %llx=pc:",
		  (unsigned long long)f->fr_arg[4],
		  (unsigned long long)f->fr_arg[5],
		  (unsigned long long)f->fr_fp,
		  (unsigned long long)f->fr_pc);
	/* Sometimes this don't work.  Dunno why. */
	db_printsym(f->fr_pc, DB_STGY_PROC, db_printf);
	db_printf("\n");
}

void
db_dump_stack(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int		i;
	u_int64_t	frame, oldframe;

	if (count == -1)
		count = 65535;

	if (!have_addr)
		frame = DDB_TF->tf_out[6];
	else
		frame = addr;

	/* Traverse window stack */
	oldframe = 0;
	for (i = 0; i < count && frame; i++) {
		if (oldframe == frame) {
			db_printf("WARNING: stack loop at %llx\n", frame);
			break;
		}
		oldframe = frame;

		if ((frame & 1) == 0) {
			db_printf("WARNING: corrupt stack at %llx\n", frame);
			break;
		}

		frame += BIAS;
		db_printf("Window %x ", i);
		db_print_window(frame - BIAS);
		frame = ((struct frame *)frame)->fr_fp;
	}
}


void
db_dump_trap(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct trapframe *tf;

	/* Use our last trapframe? */
	tf = &ddb_regs.ddb_tf;
	{
		/* Or the user trapframe? */
		register char c, *cp = modif;
		while ((c = *cp++) != 0)
			if (c == 'u')
				tf = curproc->p_md.md_tf;
	}
	/* Or an arbitrary trapframe */
	if (have_addr)
		tf = (struct trapframe *)addr;

	db_printf("Trapframe %p:\ttstate: %llx\tpc: %llx\tnpc: %llx\n",
		  tf, (unsigned long long)tf->tf_tstate,
		  (unsigned long long)tf->tf_pc,
		  (unsigned long long)tf->tf_npc);
	db_printf("y: %x\tpil: %d\toldpil: %d\ttt: %x\nGlobals:\n",
		  (int)tf->tf_y, (int)tf->tf_pil, (int)tf->tf_oldpil,
		  (int)tf->tf_tt);
	db_printf("%016llx %016llx %016llx %016llx\n",
		  (unsigned long long)tf->tf_global[0],
		  (unsigned long long)tf->tf_global[1],
		  (unsigned long long)tf->tf_global[2],
		  (unsigned long long)tf->tf_global[3]);
	db_printf("%016llx %016llx %016llx %016llx\nouts:\n",
		  (unsigned long long)tf->tf_global[4],
		  (unsigned long long)tf->tf_global[5],
		  (unsigned long long)tf->tf_global[6],
		  (unsigned long long)tf->tf_global[7]);
	db_printf("%016llx %016llx %016llx %016llx\n",
		  (unsigned long long)tf->tf_out[0],
		  (unsigned long long)tf->tf_out[1],
		  (unsigned long long)tf->tf_out[2],
		  (unsigned long long)tf->tf_out[3]);
	db_printf("%016llx %016llx %016llx %016llx\nlocals:\n",
		  (unsigned long long)tf->tf_out[4],
		  (unsigned long long)tf->tf_out[5],
		  (unsigned long long)tf->tf_out[6],
		  (unsigned long long)tf->tf_out[7]);
	db_printf("%016llx %016llx %016llx %016llx\n",
		  (unsigned long long)tf->tf_local[0],
		  (unsigned long long)tf->tf_local[1],
		  (unsigned long long)tf->tf_local[2],
		  (unsigned long long)tf->tf_local[3]);
	db_printf("%016llx %016llx %016llx %016llx\nins:\n",
		  (unsigned long long)tf->tf_local[4],
		  (unsigned long long)tf->tf_local[5],
		  (unsigned long long)tf->tf_local[6],
		  (unsigned long long)tf->tf_local[7]);
	db_printf("%016llx %016llx %016llx %016llx\n",
		  (unsigned long long)tf->tf_in[0],
		  (unsigned long long)tf->tf_in[1],
		  (unsigned long long)tf->tf_in[2],
		  (unsigned long long)tf->tf_in[3]);
	db_printf("%016llx %016llx %016llx %016llx\n",
		  (unsigned long long)tf->tf_in[4],
		  (unsigned long long)tf->tf_in[5],
		  (unsigned long long)tf->tf_in[6],
		  (unsigned long long)tf->tf_in[7]);
}

void
db_dump_fpstate(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct fpstate *fpstate;

	/* Use our last trapframe? */
	fpstate = &ddb_regs.ddb_fpstate;
	/* Or an arbitrary trapframe */
	if (have_addr)
		fpstate = (struct fpstate *)addr;

	db_printf("fpstate %p: fsr = %llx gsr = %lx\nfpregs:\n",
		fpstate, (unsigned long long)fpstate->fs_fsr,
		(unsigned long)fpstate->fs_gsr);
	db_printf(" 0: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		(unsigned int)fpstate->fs_regs[0],
		(unsigned int)fpstate->fs_regs[1],
		(unsigned int)fpstate->fs_regs[2],
		(unsigned int)fpstate->fs_regs[3],
		(unsigned int)fpstate->fs_regs[4],
		(unsigned int)fpstate->fs_regs[5],
		(unsigned int)fpstate->fs_regs[6],
		(unsigned int)fpstate->fs_regs[7]);
	db_printf(" 8: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		(unsigned int)fpstate->fs_regs[8],
		(unsigned int)fpstate->fs_regs[9],
		(unsigned int)fpstate->fs_regs[10],
		(unsigned int)fpstate->fs_regs[11],
		(unsigned int)fpstate->fs_regs[12],
		(unsigned int)fpstate->fs_regs[13],
		(unsigned int)fpstate->fs_regs[14],
		(unsigned int)fpstate->fs_regs[15]);
	db_printf("16: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		(unsigned int)fpstate->fs_regs[16],
		(unsigned int)fpstate->fs_regs[17],
		(unsigned int)fpstate->fs_regs[18],
		(unsigned int)fpstate->fs_regs[19],
		(unsigned int)fpstate->fs_regs[20],
		(unsigned int)fpstate->fs_regs[21],
		(unsigned int)fpstate->fs_regs[22],
		(unsigned int)fpstate->fs_regs[23]);
	db_printf("24: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		(unsigned int)fpstate->fs_regs[24],
		(unsigned int)fpstate->fs_regs[25],
		(unsigned int)fpstate->fs_regs[26],
		(unsigned int)fpstate->fs_regs[27],
		(unsigned int)fpstate->fs_regs[28],
		(unsigned int)fpstate->fs_regs[29],
		(unsigned int)fpstate->fs_regs[30],
		(unsigned int)fpstate->fs_regs[31]);
	db_printf("32: %08x%08x %08x%08x %08x%08x %08x%08x\n",
		(unsigned int)fpstate->fs_regs[32],
		(unsigned int)fpstate->fs_regs[33],
		(unsigned int)fpstate->fs_regs[34],
		(unsigned int)fpstate->fs_regs[35],
		(unsigned int)fpstate->fs_regs[36],
		(unsigned int)fpstate->fs_regs[37],
		(unsigned int)fpstate->fs_regs[38],
		(unsigned int)fpstate->fs_regs[39]);
	db_printf("40: %08x%08x %08x%08x %08x%08x %08x%08x\n",
		(unsigned int)fpstate->fs_regs[40],
		(unsigned int)fpstate->fs_regs[41],
		(unsigned int)fpstate->fs_regs[42],
		(unsigned int)fpstate->fs_regs[43],
		(unsigned int)fpstate->fs_regs[44],
		(unsigned int)fpstate->fs_regs[45],
		(unsigned int)fpstate->fs_regs[46],
		(unsigned int)fpstate->fs_regs[47]);
	db_printf("48: %08x%08x %08x%08x %08x%08x %08x%08x\n",
		(unsigned int)fpstate->fs_regs[48],
		(unsigned int)fpstate->fs_regs[49],
		(unsigned int)fpstate->fs_regs[50],
		(unsigned int)fpstate->fs_regs[51],
		(unsigned int)fpstate->fs_regs[52],
		(unsigned int)fpstate->fs_regs[53],
		(unsigned int)fpstate->fs_regs[54],
		(unsigned int)fpstate->fs_regs[55]);
	db_printf("56: %08x%08x %08x%08x %08x%08x %08x%08x\n",
		(unsigned int)fpstate->fs_regs[56],
		(unsigned int)fpstate->fs_regs[57],
		(unsigned int)fpstate->fs_regs[58],
		(unsigned int)fpstate->fs_regs[59],
		(unsigned int)fpstate->fs_regs[60],
		(unsigned int)fpstate->fs_regs[61],
		(unsigned int)fpstate->fs_regs[62],
		(unsigned int)fpstate->fs_regs[63]);
}

void
db_dump_ts(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct trapstate	*ts;
	int			i, tl;

	/* Use our last trapframe? */
	ts = &ddb_regs.ddb_ts[0];
	tl = ddb_regs.ddb_tl;
	for (i = 0; i < tl; i++) {
		printf("%d tt=%lx tstate=%lx tpc=%p tnpc=%p\n",
		       i+1, (long)ts[i].tt, (u_long)ts[i].tstate,
		       (void *)(u_long)ts[i].tpc, (void *)(u_long)ts[i].tnpc);
	}

}
