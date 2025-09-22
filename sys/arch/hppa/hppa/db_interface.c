/*	$OpenBSD: db_interface.c,v 1.51 2024/11/07 16:02:29 miod Exp $	*/

/*
 * Copyright (c) 1999-2003 Michael Shalayeff
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#undef DDB_DEBUG

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/stacktrace.h>

#include <machine/db_machdep.h>
#include <machine/frame.h>
#include <machine/cpufunc.h>

#include <ddb/db_access.h>
#include <ddb/db_command.h>
#include <ddb/db_output.h>
#include <ddb/db_run.h>
#include <ddb/db_sym.h>
#include <ddb/db_var.h>
#include <ddb/db_variables.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>

#include <dev/cons.h>

void kdbprinttrap(int, int);

extern char *trap_type[];
extern int trap_types;

db_regs_t	ddb_regs;
struct db_variable db_regs[] = {
	{ "flags", (long *)&ddb_regs.tf_flags,  FCN_NULL },
	{ "r1",    (long *)&ddb_regs.tf_r1,  FCN_NULL },
	{ "rp",    (long *)&ddb_regs.tf_rp,  FCN_NULL },
	{ "r3",    (long *)&ddb_regs.tf_r3,  FCN_NULL },
	{ "r4",    (long *)&ddb_regs.tf_r4,  FCN_NULL },
	{ "r5",    (long *)&ddb_regs.tf_r5,  FCN_NULL },
	{ "r6",    (long *)&ddb_regs.tf_r6,  FCN_NULL },
	{ "r7",    (long *)&ddb_regs.tf_r7,  FCN_NULL },
	{ "r8",    (long *)&ddb_regs.tf_r8,  FCN_NULL },
	{ "r9",    (long *)&ddb_regs.tf_r9,  FCN_NULL },
	{ "r10",   (long *)&ddb_regs.tf_r10, FCN_NULL },
	{ "r11",   (long *)&ddb_regs.tf_r11, FCN_NULL },
	{ "r12",   (long *)&ddb_regs.tf_r12, FCN_NULL },
	{ "r13",   (long *)&ddb_regs.tf_r13, FCN_NULL },
	{ "r14",   (long *)&ddb_regs.tf_r14, FCN_NULL },
	{ "r15",   (long *)&ddb_regs.tf_r15, FCN_NULL },
	{ "r16",   (long *)&ddb_regs.tf_r16, FCN_NULL },
	{ "r17",   (long *)&ddb_regs.tf_r17, FCN_NULL },
	{ "r18",   (long *)&ddb_regs.tf_r18, FCN_NULL },
	{ "r19",   (long *)&ddb_regs.tf_t4,  FCN_NULL },
	{ "r20",   (long *)&ddb_regs.tf_t3,  FCN_NULL },
	{ "r21",   (long *)&ddb_regs.tf_t2,  FCN_NULL },
	{ "r22",   (long *)&ddb_regs.tf_t1,  FCN_NULL },
	{ "r23",   (long *)&ddb_regs.tf_arg3,  FCN_NULL },
	{ "r24",   (long *)&ddb_regs.tf_arg2,  FCN_NULL },
	{ "r25",   (long *)&ddb_regs.tf_arg1,  FCN_NULL },
	{ "r26",   (long *)&ddb_regs.tf_arg0,  FCN_NULL },
	{ "r27",   (long *)&ddb_regs.tf_dp,    FCN_NULL },
	{ "r28",   (long *)&ddb_regs.tf_ret0,  FCN_NULL },
	{ "r29",   (long *)&ddb_regs.tf_ret1,  FCN_NULL },
	{ "r30",   (long *)&ddb_regs.tf_sp,    FCN_NULL },
	{ "r31",   (long *)&ddb_regs.tf_r31,   FCN_NULL },
	{ "sar",   (long *)&ddb_regs.tf_sar,   FCN_NULL },

	{ "rctr",  (long *)&ddb_regs.tf_rctr,  FCN_NULL },
	{ "ccr",   (long *)&ddb_regs.tf_ccr,   FCN_NULL },
	{ "eirr",  (long *)&ddb_regs.tf_eirr,  FCN_NULL },
	{ "eiem",  (long *)&ddb_regs.tf_eiem,  FCN_NULL },
	{ "iir",   (long *)&ddb_regs.tf_iir,   FCN_NULL },
	{ "isr",   (long *)&ddb_regs.tf_isr,   FCN_NULL },
	{ "ior",   (long *)&ddb_regs.tf_ior,   FCN_NULL },
	{ "ipsw",  (long *)&ddb_regs.tf_ipsw,  FCN_NULL },
	{ "iisqh", (long *)&ddb_regs.tf_iisq_head,  FCN_NULL },
	{ "iioqh", (long *)&ddb_regs.tf_iioq_head,  FCN_NULL },
	{ "iisqt", (long *)&ddb_regs.tf_iisq_tail,  FCN_NULL },
	{ "iioqt", (long *)&ddb_regs.tf_iioq_tail,  FCN_NULL },

	{ "sr0",   (long *)&ddb_regs.tf_sr0,   FCN_NULL },
	{ "sr1",   (long *)&ddb_regs.tf_sr1,   FCN_NULL },
	{ "sr2",   (long *)&ddb_regs.tf_sr2,   FCN_NULL },
	{ "sr3",   (long *)&ddb_regs.tf_sr3,   FCN_NULL },
	{ "sr4",   (long *)&ddb_regs.tf_sr4,   FCN_NULL },
	{ "sr5",   (long *)&ddb_regs.tf_sr5,   FCN_NULL },
	{ "sr6",   (long *)&ddb_regs.tf_sr6,   FCN_NULL },
	{ "sr7",   (long *)&ddb_regs.tf_sr7,   FCN_NULL },

	{ "pidr1", (long *)&ddb_regs.tf_pidr1, FCN_NULL },
	{ "pidr2", (long *)&ddb_regs.tf_pidr2, FCN_NULL },
#ifdef pbably_not_worth_it
	{ "pidr3", (long *)&ddb_regs.tf_pidr3, FCN_NULL },
	{ "pidr4", (long *)&ddb_regs.tf_pidr4, FCN_NULL },
#endif

	{ "vtop",  (long *)&ddb_regs.tf_vtop,  FCN_NULL },
	{ "cr28",  (long *)&ddb_regs.tf_cr28,  FCN_NULL },
	{ "cr30",  (long *)&ddb_regs.tf_cr30,  FCN_NULL },
};
struct db_variable *db_eregs = db_regs + nitems(db_regs);

void
db_enter(void)
{
	extern int kernelmapped;	/* from locore.S */
	if (kernelmapped)
		__asm volatile ("break %0, %1"
		    :: "i" (HPPA_BREAK_KERNEL), "i" (HPPA_BREAK_KGDB));
}

void
db_read_bytes(vaddr_t addr, size_t size, void *datap)
{
	char *data = datap;
	register char *src = (char *)addr;

	while (size--)
		*data++ = *src++;
}

void
db_write_bytes(vaddr_t addr, size_t size, void *datap)
{
	char *data = datap;
	register char *dst = (char *)addr;

	while (size--)
		*dst++ = *data++;

	/* unfortunately ddb does not provide any hooks for these */
	ficache(HPPA_SID_KERNEL, (vaddr_t)data, size);
	fdcache(HPPA_SID_KERNEL, (vaddr_t)data, size);
}


/*
 * Print trap reason.
 */
void
kdbprinttrap(int type, int code)
{
	type &= ~T_USER;	/* just in case */
	db_printf("kernel: ");
	if (type >= trap_types || type < 0)
		db_printf("type 0x%x", type);
	else
		db_printf("%s", trap_type[type]);
	db_printf(" trap, code=0x%x\n", code);
}

/*
 *  db_ktrap - field a BPT trap
 */
int
db_ktrap(int type, int code, db_regs_t *regs)
{
	extern label_t *db_recover;
	int s;

	switch (type) {
	case T_IBREAK:
	case T_DBREAK:
	case -1:
		break;
	default:
		if (!db_panic)
			return (0);

		kdbprinttrap(type, code);
		if (db_recover != 0) {
			db_error("Caught exception in DDB; continuing...\n");
			/* NOT REACHED */
		}
	}

	/* XXX Should switch to kdb`s own stack here. */

	s = splhigh();
	ddb_regs = *regs;
	db_active++;
	cnpollc(1);
	db_trap(type, code);
	cnpollc(0);
	db_active--;
	splx(s);

	*regs = ddb_regs;

	return (1);
}

/*
 *  Validate an address for use as a breakpoint.
 *  Any address is allowed for now.
 */
int
db_valid_breakpoint(vaddr_t addr)
{
	return (1);
}

void
db_stack_trace_print(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif, int (*pr)(const char *, ...))
{
	register_t *fp, pc, rp, *argp;
	Elf_Sym *sym;
	db_expr_t off;
	const char *name;
	int nargs;

	if (count < 0)
		count = 65536;

	if (!have_addr) {
		fp = (register_t *)ddb_regs.tf_r3;
		pc = ddb_regs.tf_iioq_head;
		rp = ddb_regs.tf_rp;
	} else {
		fp = (register_t *)addr;
		pc = 0;
		rp = ((register_t *)fp)[-5];
	}

#ifdef DDB_DEBUG
	(*pr) (">> %p, 0x%x, 0x%x\t", fp, pc, rp);
#endif
	while (fp && count--) {

		if (USERMODE(pc))
			return;

		sym = db_search_symbol(pc, DB_STGY_ANY, &off);
		db_symbol_values (sym, &name, NULL);

		if (name == NULL)
			(*pr)("%lx(", pc);
		else
			(*pr)("%s(", name);

		/* args */
		nargs = 4;
		/*
		 * XXX first four args are passed on registers, and may not
		 * be stored on stack, dunno how to recover their values yet
		 */
		for (argp = &fp[-9]; nargs--; argp--) {
			(*pr)("%x%s", db_get_value((int)argp, 4, 0),
				  nargs? ",":"");
		}
		(*pr)(") at ");
		db_printsym(pc, DB_STGY_PROC, pr);
		(*pr)("\n");

		/* TODO: print locals */

		/* next frame */
		pc = rp;
		rp = fp[-5];

		/* if a terminal frame and not a start of a page
		 * then skip the trapframe and the terminal frame */
		if (!fp[0]) {
			struct trapframe *tf;

			tf = (struct trapframe *)((char *)fp - sizeof(*tf));

			if (tf->tf_flags & TFF_SYS)
				(*pr)("-- syscall #%d(%x, %x, %x, %x, ...)\n",
				    tf->tf_t1, tf->tf_arg0, tf->tf_arg1,
				    tf->tf_arg2, tf->tf_arg3);
			else
				(*pr)("-- trap #%d%s\n", tf->tf_flags & 0x3f,
				    (tf->tf_flags & T_USER)? " from user" : "");

			if (!(tf->tf_flags & TFF_LAST)) {
				fp = (register_t *)tf->tf_r3;
				pc = tf->tf_iioq_head;
				rp = tf->tf_rp;
			} else
				fp = 0;
		} else
			fp = (register_t *)fp[0];
#ifdef DDB_DEBUG
		(*pr) (">> %x, %x, %x\t", fp, pc, rp);
#endif
	}

	if (count && pc) {
		db_printsym(pc, DB_STGY_XTRN, pr);
		(*pr)(":\n");
	}
}

void
stacktrace_save_at(struct stacktrace *st, unsigned int skip)
{
	register_t *fp, pc, rp;
	int	i;

	fp = (register_t *)__builtin_frame_address(0);
	pc = 0;
	rp = fp[-5];

	st->st_count = 0;
	for (i = 0; i < STACKTRACE_MAX; i++) {
		if (skip == 0)
			st->st_pc[st->st_count++] = rp;
		else
			skip--;

		/* next frame */
		pc = rp;
		if (!fp[0] || USERMODE(pc))
			break;

		rp = fp[-5];
		fp = (register_t *)fp[0];
	}
}

void
stacktrace_save_utrace(struct stacktrace *st)
{
	st->st_count = 0;
}
