/* $OpenBSD: db_interface.c,v 1.31 2025/06/28 16:04:09 miod Exp $ */
/* $NetBSD: db_interface.c,v 1.8 1999/10/12 17:08:57 jdolecek Exp $ */

/* 
 * Mach Operating System
 * Copyright (c) 1992,1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS ``AS IS''
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
 *
 *	db_interface.c,v 2.4 1991/02/05 17:11:13 mrt (CMU)
 */

/*
 * Parts of this file are derived from Mach 3:
 *
 *	File: alpha_instruction.c
 *	Author: Alessandro Forin, Carnegie Mellon University
 *	Date:	6/92
 */

/*
 * Interface to DDB.
 *
 * Modified for NetBSD/alpha by:
 *
 *	Christopher G. Demetriou, Carnegie Mellon University
 *
 *	Jason R. Thorpe, Numerical Aerospace Simulation Facility,
 *	NASA Ames Research Center
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <dev/cons.h>

#include <machine/db_machdep.h>
#include <machine/pal.h>
#include <machine/prom.h>

#include <alpha/alpha/db_instruction.h>

#include <ddb/db_sym.h>
#include <ddb/db_command.h>
#include <ddb/db_extern.h>
#include <ddb/db_access.h>
#include <ddb/db_output.h>
#include <ddb/db_variables.h>
#include <ddb/db_interface.h>


extern label_t	*db_recover;

#if 0
extern char *trap_type[];
extern int trap_types;
#endif

db_regs_t ddb_regs;

#if defined(MULTIPROCESSOR)
void	db_mach_cpu(db_expr_t, int, db_expr_t, char *);
#endif

const struct db_command db_machine_command_table[] = {
#if defined(MULTIPROCESSOR)
	{ "ddbcpu",	db_mach_cpu,	0,	NULL },
#endif
	{ NULL,		NULL,		0,	NULL }
};

struct db_variable db_regs[] = {
	{	"v0",	&ddb_regs.tf_regs[FRAME_V0],	FCN_NULL	},
	{	"t0",	&ddb_regs.tf_regs[FRAME_T0],	FCN_NULL	},
	{	"t1",	&ddb_regs.tf_regs[FRAME_T1],	FCN_NULL	},
	{	"t2",	&ddb_regs.tf_regs[FRAME_T2],	FCN_NULL	},
	{	"t3",	&ddb_regs.tf_regs[FRAME_T3],	FCN_NULL	},
	{	"t4",	&ddb_regs.tf_regs[FRAME_T4],	FCN_NULL	},
	{	"t5",	&ddb_regs.tf_regs[FRAME_T5],	FCN_NULL	},
	{	"t6",	&ddb_regs.tf_regs[FRAME_T6],	FCN_NULL	},
	{	"t7",	&ddb_regs.tf_regs[FRAME_T7],	FCN_NULL	},
	{	"s0",	&ddb_regs.tf_regs[FRAME_S0],	FCN_NULL	},
	{	"s1",	&ddb_regs.tf_regs[FRAME_S1],	FCN_NULL	},
	{	"s2",	&ddb_regs.tf_regs[FRAME_S2],	FCN_NULL	},
	{	"s3",	&ddb_regs.tf_regs[FRAME_S3],	FCN_NULL	},
	{	"s4",	&ddb_regs.tf_regs[FRAME_S4],	FCN_NULL	},
	{	"s5",	&ddb_regs.tf_regs[FRAME_S5],	FCN_NULL	},
	{	"s6",	&ddb_regs.tf_regs[FRAME_S6],	FCN_NULL	},
	{	"a0",	&ddb_regs.tf_regs[FRAME_A0],	FCN_NULL	},
	{	"a1",	&ddb_regs.tf_regs[FRAME_A1],	FCN_NULL	},
	{	"a2",	&ddb_regs.tf_regs[FRAME_A2],	FCN_NULL	},
	{	"a3",	&ddb_regs.tf_regs[FRAME_A3],	FCN_NULL	},
	{	"a4",	&ddb_regs.tf_regs[FRAME_A4],	FCN_NULL	},
	{	"a5",	&ddb_regs.tf_regs[FRAME_A5],	FCN_NULL	},
	{	"t8",	&ddb_regs.tf_regs[FRAME_T8],	FCN_NULL	},
	{	"t9",	&ddb_regs.tf_regs[FRAME_T9],	FCN_NULL	},
	{	"t10",	&ddb_regs.tf_regs[FRAME_T10],	FCN_NULL	},
	{	"t11",	&ddb_regs.tf_regs[FRAME_T11],	FCN_NULL	},
	{	"ra",	&ddb_regs.tf_regs[FRAME_RA],	FCN_NULL	},
	{	"t12",	&ddb_regs.tf_regs[FRAME_T12],	FCN_NULL	},
	{	"at",	&ddb_regs.tf_regs[FRAME_AT],	FCN_NULL	},
	{	"gp",	&ddb_regs.tf_regs[FRAME_GP],	FCN_NULL	},
	{	"sp",	&ddb_regs.tf_regs[FRAME_SP],	FCN_NULL	},
	{	"pc",	&ddb_regs.tf_regs[FRAME_PC],	FCN_NULL	},
	{	"ps",	&ddb_regs.tf_regs[FRAME_PS],	FCN_NULL	},
	{	"ai",	&ddb_regs.tf_regs[FRAME_T11],	FCN_NULL	},
	{	"pv",	&ddb_regs.tf_regs[FRAME_T12],	FCN_NULL	},
};
struct db_variable *db_eregs = db_regs + nitems(db_regs);

/*
 * ddb_trap - field a kernel trap
 */
int
ddb_trap(unsigned long a0, unsigned long a1, unsigned long a2,
    unsigned long entry, db_regs_t *regs)
{
	struct cpu_info *ci = curcpu();
	int s;

	if (entry != ALPHA_KENTRY_IF ||
	    (a0 != ALPHA_IF_CODE_BPT && a0 != ALPHA_IF_CODE_BUGCHK)) {
		if (db_recover != 0) {
			/* This will longjmp back into db_command_loop() */
			db_error("Caught exception in ddb.\n");
			/* NOTREACHED */
		}

		/*
		 * Tell caller "We did NOT handle the trap."
		 * Caller should panic, or whatever.
		 */
		return (0);
	}

	/*
	 * alpha_debug() switches us to the debugger stack.
	 */

	ci->ci_db_regs = regs;
	ddb_regs = *regs;

	s = splhigh();

	db_active++;
	cnpollc(1);		/* Set polling mode, unblank video */

	db_trap(entry, a0);	/* Where the work happens */

	cnpollc(0);		/* Resume interrupt mode */
	db_active--;

	splx(s);

	*regs = ddb_regs;

	/*
	 * Tell caller "We HAVE handled the trap."
	 */
	return (1);
}

/*
 * Read bytes from kernel address space for debugger.
 */
void
db_read_bytes(vaddr_t addr, size_t size, void *datap)
{
	char *data = datap, *src;

	src = (char *)addr;
	while (size-- > 0)
		*data++ = *src++;
}

/*
 * Write bytes to kernel address space for debugger.
 */
void
db_write_bytes(vaddr_t addr, size_t size, void *datap)
{
	char *data = datap, *dst;

	dst = (char *)addr;
	while (size-- > 0)
		*dst++ = *data++;
	alpha_pal_imb();
}

void
db_enter(void)
{

	__asm volatile("call_pal 0x81");		/* bugchk */
}

/*
 * Map Alpha register numbers to trapframe/db_regs_t offsets.
 */
static int reg_to_frame[32] = {
	FRAME_V0,
	FRAME_T0,
	FRAME_T1,
	FRAME_T2,
	FRAME_T3,
	FRAME_T4,
	FRAME_T5,
	FRAME_T6,
	FRAME_T7,

	FRAME_S0,
	FRAME_S1,
	FRAME_S2,
	FRAME_S3,
	FRAME_S4,
	FRAME_S5,
	FRAME_S6,

	FRAME_A0,
	FRAME_A1,
	FRAME_A2,
	FRAME_A3,
	FRAME_A4,
	FRAME_A5,

	FRAME_T8,
	FRAME_T9,
	FRAME_T10,
	FRAME_T11,
	FRAME_RA,
	FRAME_T12,
	FRAME_AT,
	FRAME_GP,
	FRAME_SP,
	-1,		/* zero */
};

u_long
db_register_value(db_regs_t *regs, int regno)
{

	if (regno > 31 || regno < 0) {
		db_printf(" **** STRANGE REGISTER NUMBER %d **** ", regno);
		return (0);
	}

	if (regno == 31)
		return (0);

	return (regs->tf_regs[reg_to_frame[regno]]);
}

/*
 * Support functions for software single-step.
 */

int
db_inst_call(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;
	return ((insn.branch_format.opcode == op_bsr) ||
	    ((insn.jump_format.opcode == op_j) &&
	     (insn.jump_format.action & 1)));
}

int
db_inst_return(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;
	return ((insn.jump_format.opcode == op_j) &&
	    (insn.jump_format.action == op_ret));
}

int
db_inst_trap_return(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;
	return ((insn.pal_format.opcode == op_pal) &&
	    (insn.pal_format.function == PAL_OSF1_rti));
}

int
db_inst_branch(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;
	switch (insn.branch_format.opcode) {
	case op_j:
	case op_br:
	case op_fbeq:
	case op_fblt:
	case op_fble:
	case op_fbne:
	case op_fbge:
	case op_fbgt:
	case op_blbc:
	case op_beq:
	case op_blt:
	case op_ble:
	case op_blbs:
	case op_bne:
	case op_bge:
	case op_bgt:
		return 1;
	}

	return 0;
}

int
db_inst_unconditional_flow_transfer(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;
	switch (insn.branch_format.opcode) {
	case op_j:
	case op_br:
		return 1;

	case op_pal:
		switch (insn.pal_format.function) {
		case PAL_OSF1_retsys:
		case PAL_OSF1_rti:
		case PAL_OSF1_callsys:
			return 1;
		}
	}

	return 0;
}

int
db_inst_load(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;

	/* Loads. */
	if (insn.mem_format.opcode == op_ldbu ||
	    insn.mem_format.opcode == op_ldq_u ||
	    insn.mem_format.opcode == op_ldwu)
		return 1;
	if ((insn.mem_format.opcode >= op_ldf) &&
	    (insn.mem_format.opcode <= op_ldt))
		return 1;
	if ((insn.mem_format.opcode >= op_ldl) &&
	    (insn.mem_format.opcode <= op_ldq_l))
		return 1;

	/* Prefetches. */
	if (insn.mem_format.opcode == op_special) {
		/* Note: MB is treated as a store. */
		if ((insn.mem_format.displacement == (short)op_fetch) ||
		    (insn.mem_format.displacement == (short)op_fetch_m))
			return 1;
	}

	return 0;
}

vaddr_t
db_branch_taken(int ins, vaddr_t pc, db_regs_t *regs)
{
	long signed_immediate;
	alpha_instruction insn;
	vaddr_t newpc;

	insn.bits = ins;
	switch (insn.branch_format.opcode) {
	/*
	 * Jump format: target PC is (contents of instruction's "RB") & ~3.
	 */
	case op_j:
		newpc = db_register_value(regs, insn.jump_format.rb) & ~3;
		break;

	/*
	 * Branch format: target PC is
	 *	(new PC) + (4 * sign-ext(displacement)).
	 */
	case op_br:
	case op_fbeq:
	case op_fblt:
	case op_fble:
	case op_bsr:
	case op_fbne:
	case op_fbge:
	case op_fbgt:
	case op_blbc:
	case op_beq:
	case op_blt:
	case op_ble:
	case op_blbs:
	case op_bne:
	case op_bge:
	case op_bgt:
		signed_immediate = insn.branch_format.displacement;
		newpc = (pc + 4) + (signed_immediate << 2);
		break;

	default:
		printf("DDB: db_inst_branch_taken on non-branch!\n");
		newpc = pc;	/* XXX */
	}

	return (newpc);
}

/*
 * Validate an address for use as a breakpoint.  We cannot let some
 * addresses have breakpoints as the ddb code itself uses that codepath.
 * Recursion and kernel stack space exhaustion will follow.
 */
int
db_valid_breakpoint(vaddr_t addr)
{
	const char *name;
	db_expr_t offset;

	db_find_sym_and_offset(addr, &name, &offset);
	if (name && strcmp(name, "alpha_pal_swpipl") == 0)
		return (0);
	return (1);
}

vaddr_t
next_instr_address(vaddr_t pc, int branch)
{
	if (!branch)
		return (pc + sizeof(int));
	return (branch_taken(*(u_int *)pc, pc, getreg_val, &ddb_regs));
}

#if defined(MULTIPROCESSOR)
void
db_mach_cpu(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	if (have_addr == 0) {
		db_printf("addr               dev   id flg mtx ipis "
		    "curproc            fpcurproc\n");
		CPU_INFO_FOREACH(cii, ci)
			db_printf("%p %-5s %02lu %03lx %03d %04lx %p %p\n",
			    ci, ci->ci_dev->dv_xname, ci->ci_cpuid,
			    ci->ci_flags, ci->ci_mutex_level, ci->ci_ipis,
			    ci->ci_curproc, ci->ci_fpcurproc);
		return;
	}

	if (addr < 0 || addr >= ALPHA_MAXPROCS) {
		db_printf("CPU %ld out of range\n", addr);
		return;
	}

	ci = cpu_info[addr];
	if (ci == NULL) {
		db_printf("CPU %ld is not configured\n", addr);
		return;
	}

	if (ci != curcpu()) {
		if ((ci->ci_flags & CPUF_PAUSED) == 0) {
			db_printf("CPU %ld not paused\n", addr);
			return;
		}
	}

	if (ci->ci_db_regs == NULL) {
		db_printf("CPU %ld has no register state\n", addr);
		return;
	}

	db_printf("Using CPU %ld\n", addr);
	ddb_regs = *ci->ci_db_regs;	/* struct copy */
}
#endif /* MULTIPROCESSOR */

void
db_machine_init(void)
{
}
