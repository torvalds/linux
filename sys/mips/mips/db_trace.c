/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	JNPR: db_trace.c,v 1.8 2007/08/09 11:23:32 katta
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/proc.h>
#include <sys/stack.h>
#include <sys/sysent.h>

#include <machine/asm.h>
#include <machine/db_machdep.h>
#include <machine/md_var.h>
#include <machine/mips_opcode.h>
#include <machine/pcb.h>
#include <machine/trap.h>

#include <ddb/ddb.h>
#include <ddb/db_sym.h>

extern char _locore[];
extern char _locoreEnd[];
extern char edata[];

/*
 * A function using a stack frame has the following instruction as the first
 * one: [d]addiu sp,sp,-<frame_size>
 *
 * We make use of this to detect starting address of a function. This works
 * better than using 'j ra' instruction to signify end of the previous
 * function (for e.g. functions like boot() or panic() do not actually
 * emit a 'j ra' instruction).
 *
 * XXX the abi does not require that the addiu instruction be the first one.
 */
#define	MIPS_START_OF_FUNCTION(ins)	((((ins) & 0xffff8000) == 0x27bd8000) \
	|| (((ins) & 0xffff8000) == 0x67bd8000))

/*
 * MIPS ABI 3.0 requires that all functions return using the 'j ra' instruction
 *
 * XXX gcc doesn't do this for functions with __noreturn__ attribute.
 */
#define	MIPS_END_OF_FUNCTION(ins)	((ins) == 0x03e00008)

#if defined(__mips_n64)
#	define	MIPS_IS_VALID_KERNELADDR(reg)	((((reg) & 3) == 0) && \
					((vm_offset_t)(reg) >= MIPS_XKPHYS_START))
#else
#	define	MIPS_IS_VALID_KERNELADDR(reg)	((((reg) & 3) == 0) && \
					((vm_offset_t)(reg) >= MIPS_KSEG0_START))
#endif

static void
stacktrace_subr(register_t pc, register_t sp, register_t ra)
{
	InstFmt i;
	/*
	 * Arrays for a0..a3 registers and flags if content
	 * of these registers is valid, e.g. obtained from the stack
	 */
	int valid_args[4];
	register_t args[4];
	register_t va, subr, cause, badvaddr;
	unsigned instr, mask;
	unsigned int frames = 0;
	int more, stksize, j;
	register_t	next_ra;
	bool trapframe;

/* Jump here when done with a frame, to start a new one */
loop:

	/*
	 * Invalidate arguments values
	 */
	valid_args[0] = 0;
	valid_args[1] = 0;
	valid_args[2] = 0;
	valid_args[3] = 0;
	next_ra = 0;
	stksize = 0;
	subr = 0;
	trapframe = false;
	if (frames++ > 100) {
		db_printf("\nstackframe count exceeded\n");
		return;
	}

	/* Check for bad SP: could foul up next frame. */
	if (!MIPS_IS_VALID_KERNELADDR(sp)) {
		db_printf("SP 0x%jx: not in kernel\n", (uintmax_t)sp);
		ra = 0;
		subr = 0;
		goto done;
	}
#define Between(x, y, z) \
		( ((x) <= (y)) && ((y) < (z)) )
#define pcBetween(a,b) \
		Between((uintptr_t)a, pc, (uintptr_t)b)

	/*
	 * Check for current PC in  exception handler code that don't have a
	 * preceding "j ra" at the tail of the preceding function. Depends
	 * on relative ordering of functions in exception.S, swtch.S.
	 */
	if (pcBetween(MipsKernGenException, MipsUserGenException)) {
		subr = (uintptr_t)MipsKernGenException;
		trapframe = true;
	} else if (pcBetween(MipsUserGenException, MipsKernIntr))
		subr = (uintptr_t)MipsUserGenException;
	else if (pcBetween(MipsKernIntr, MipsUserIntr)) {
		subr = (uintptr_t)MipsKernIntr;
		trapframe = true;
	} else if (pcBetween(MipsUserIntr, MipsTLBInvalidException))
		subr = (uintptr_t)MipsUserIntr;
	else if (pcBetween(MipsTLBInvalidException, MipsTLBMissException)) {
		subr = (uintptr_t)MipsTLBInvalidException;
		if (pc == (uintptr_t)MipsKStackOverflow)
			trapframe = true;
	} else if (pcBetween(fork_trampoline, savectx))
		subr = (uintptr_t)fork_trampoline;
	else if (pcBetween(savectx, cpu_throw))
		subr = (uintptr_t)savectx;
	else if (pcBetween(cpu_throw, cpu_switch))
		subr = (uintptr_t)cpu_throw;
	else if (pcBetween(cpu_switch, MipsSwitchFPState))
		subr = (uintptr_t)cpu_switch;
	else if (pcBetween(_locore, _locoreEnd)) {
		subr = (uintptr_t)_locore;
		ra = 0;
		goto done;
	}

	/* Check for bad PC. */
	if (!MIPS_IS_VALID_KERNELADDR(pc)) {
		db_printf("PC 0x%jx: not in kernel\n", (uintmax_t)pc);
		ra = 0;
		goto done;
	}

	/*
	 * For a trapframe, skip to the output and afterwards pull the
	 * previous registers out of the trapframe instead of decoding
	 * the function prologue.
	 */
	if (trapframe)
		goto done;

	/*
	 * Find the beginning of the current subroutine by scanning
	 * backwards from the current PC for the end of the previous
	 * subroutine.
	 */
	if (!subr) {
		va = pc - sizeof(int);
		while (1) {
			instr = kdbpeek((int *)va);

			if (MIPS_START_OF_FUNCTION(instr))
				break;

			if (MIPS_END_OF_FUNCTION(instr)) {
				/* skip over branch-delay slot instruction */
				va += 2 * sizeof(int);
				break;
			}

 			va -= sizeof(int);
		}

		/* skip over nulls which might separate .o files */
		while ((instr = kdbpeek((int *)va)) == 0)
			va += sizeof(int);
		subr = va;
	}
	/* scan forwards to find stack size and any saved registers */
	stksize = 0;
	more = 3;
	mask = 0;
	for (va = subr; more; va += sizeof(int),
	    more = (more == 3) ? 3 : more - 1) {
		/* stop if hit our current position */
		if (va >= pc)
			break;
		instr = kdbpeek((int *)va);
		i.word = instr;
		switch (i.JType.op) {
		case OP_SPECIAL:
			switch (i.RType.func) {
			case OP_JR:
			case OP_JALR:
				more = 2;	/* stop after next instruction */
				break;

			case OP_SYSCALL:
			case OP_BREAK:
				more = 1;	/* stop now */
			}
			break;

		case OP_BCOND:
		case OP_J:
		case OP_JAL:
		case OP_BEQ:
		case OP_BNE:
		case OP_BLEZ:
		case OP_BGTZ:
			more = 2;	/* stop after next instruction */
			break;

		case OP_COP0:
		case OP_COP1:
		case OP_COP2:
		case OP_COP3:
			switch (i.RType.rs) {
			case OP_BCx:
			case OP_BCy:
				more = 2;	/* stop after next instruction */
			}
			break;

		case OP_SW:
			/* look for saved registers on the stack */
			if (i.IType.rs != 29)
				break;
			/*
			 * only restore the first one except RA for
			 * MipsKernGenException case
			 */
			if (mask & (1 << i.IType.rt)) {
				if (subr == (uintptr_t)MipsKernGenException &&
				    i.IType.rt == 31)
					next_ra = kdbpeek((int *)(sp +
					    (short)i.IType.imm));
				break;
			}
			mask |= (1 << i.IType.rt);
			switch (i.IType.rt) {
			case 4:/* a0 */
				args[0] = kdbpeek((int *)(sp + (short)i.IType.imm));
				valid_args[0] = 1;
				break;

			case 5:/* a1 */
				args[1] = kdbpeek((int *)(sp + (short)i.IType.imm));
				valid_args[1] = 1;
				break;

			case 6:/* a2 */
				args[2] = kdbpeek((int *)(sp + (short)i.IType.imm));
				valid_args[2] = 1;
				break;

			case 7:/* a3 */
				args[3] = kdbpeek((int *)(sp + (short)i.IType.imm));
				valid_args[3] = 1;
				break;

			case 31:	/* ra */
				ra = kdbpeek((int *)(sp + (short)i.IType.imm));
			}
			break;

		case OP_SD:
			/* look for saved registers on the stack */
			if (i.IType.rs != 29)
				break;
			/* only restore the first one */
			if (mask & (1 << i.IType.rt))
				break;
			mask |= (1 << i.IType.rt);
			switch (i.IType.rt) {
			case 4:/* a0 */
				args[0] = kdbpeekd((int *)(sp + (short)i.IType.imm));
				valid_args[0] = 1;
				break;

			case 5:/* a1 */
				args[1] = kdbpeekd((int *)(sp + (short)i.IType.imm));
				valid_args[1] = 1;
				break;

			case 6:/* a2 */
				args[2] = kdbpeekd((int *)(sp + (short)i.IType.imm));
				valid_args[2] = 1;
				break;

			case 7:/* a3 */
				args[3] = kdbpeekd((int *)(sp + (short)i.IType.imm));
				valid_args[3] = 1;
				break;

			case 31:	/* ra */
				ra = kdbpeekd((int *)(sp + (short)i.IType.imm));
			}
			break;

		case OP_ADDI:
		case OP_ADDIU:
		case OP_DADDI:
		case OP_DADDIU:
			/* look for stack pointer adjustment */
			if (i.IType.rs != 29 || i.IType.rt != 29)
				break;
			stksize = -((short)i.IType.imm);
		}
	}

done:
	db_printsym(pc, DB_STGY_PROC);
	db_printf(" (");
	for (j = 0; j < 4; j ++) {
		if (j > 0)
			db_printf(",");
		if (valid_args[j])
			db_printf("%jx", (uintmax_t)(u_register_t)args[j]);
		else
			db_printf("?");
	}

	db_printf(") ra %jx sp %jx sz %d\n",
	    (uintmax_t)(u_register_t) ra,
	    (uintmax_t)(u_register_t) sp,
	    stksize);

	if (trapframe) {
#define	TF_REG(base, reg)	((base) + CALLFRAME_SIZ + ((reg) * SZREG))
#if defined(__mips_n64) || defined(__mips_n32)
		pc = kdbpeekd((int *)TF_REG(sp, PC));
		ra = kdbpeekd((int *)TF_REG(sp, RA));
		sp = kdbpeekd((int *)TF_REG(sp, SP));
		cause = kdbpeekd((int *)TF_REG(sp, CAUSE));
		badvaddr = kdbpeekd((int *)TF_REG(sp, BADVADDR));
#else
		pc = kdbpeek((int *)TF_REG(sp, PC));
		ra = kdbpeek((int *)TF_REG(sp, RA));
		sp = kdbpeek((int *)TF_REG(sp, SP));
		cause = kdbpeek((int *)TF_REG(sp, CAUSE));
		badvaddr = kdbpeek((int *)TF_REG(sp, BADVADDR));
#endif
#undef TF_REG
		db_printf("--- exception, cause %jx badvaddr %jx ---\n",
		    (uintmax_t)cause, (uintmax_t)badvaddr);
		goto loop;
	} else if (ra) {
		if (pc == ra && stksize == 0)
			db_printf("stacktrace: loop!\n");
		else {
			pc = ra;
			sp += stksize;
			ra = next_ra;
			goto loop;
		}
	}
}


int
db_md_set_watchpoint(db_expr_t addr, db_expr_t size)
{

	return(0);
}


int
db_md_clr_watchpoint(db_expr_t addr, db_expr_t size)
{

	return(0);
}


void
db_md_list_watchpoints()
{
}

void
db_trace_self(void)
{
	register_t pc, ra, sp;

	sp = (register_t)(intptr_t)__builtin_frame_address(0);
	ra = (register_t)(intptr_t)__builtin_return_address(0);

	__asm __volatile(
		"jal 99f\n"
		"nop\n"
		"99:\n"
		 "move %0, $31\n" /* get ra */
		 "move $31, %1\n" /* restore ra */
		 : "=r" (pc)
		 : "r" (ra));
	stacktrace_subr(pc, sp, ra);
	return;
}

int
db_trace_thread(struct thread *thr, int count)
{
	register_t pc, ra, sp;
	struct pcb *ctx;

	ctx = kdb_thr_ctx(thr);
	sp = (register_t)ctx->pcb_context[PCB_REG_SP];
	pc = (register_t)ctx->pcb_context[PCB_REG_PC];
	ra = (register_t)ctx->pcb_context[PCB_REG_RA];
	stacktrace_subr(pc, sp, ra);

	return (0);
}

void
db_show_mdpcpu(struct pcpu *pc)
{

	db_printf("ipis         = 0x%x\n", pc->pc_pending_ipis);
	db_printf("next ASID    = %d\n", pc->pc_next_asid);
	db_printf("GENID        = %d\n", pc->pc_asid_generation);
	return;
}
