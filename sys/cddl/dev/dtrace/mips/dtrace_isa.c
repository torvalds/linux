/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 */
/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/stack.h>
#include <sys/pcpu.h>

#include <machine/frame.h>
#include <machine/md_var.h>
#include <machine/reg.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include <machine/db_machdep.h>
#include <machine/md_var.h>
#include <machine/mips_opcode.h>
#include <ddb/db_sym.h>
#include <ddb/ddb.h>
#include <sys/kdb.h>

#include "regset.h"

#ifdef __mips_n64
#define	MIPS_IS_VALID_KERNELADDR(reg)	((((reg) & 3) == 0) && \
					((vm_offset_t)(reg) >= MIPS_XKPHYS_START))
#else
#define	MIPS_IS_VALID_KERNELADDR(reg)	((((reg) & 3) == 0) && \
					((vm_offset_t)(reg) >= MIPS_KSEG0_START))
#endif



/*
 * Wee need some reasonable default to prevent backtrace code
 * from wandering too far
 */
#define	MAX_FUNCTION_SIZE 0x10000
#define	MAX_PROLOGUE_SIZE 0x100

uint8_t dtrace_fuword8_nocheck(void *);
uint16_t dtrace_fuword16_nocheck(void *);
uint32_t dtrace_fuword32_nocheck(void *);
uint64_t dtrace_fuword64_nocheck(void *);

static int dtrace_next_frame(register_t *pc, register_t *sp, register_t *args, int *valid_args);
static int dtrace_next_uframe(register_t *pc, register_t *sp, register_t *ra);

void
dtrace_getpcstack(pc_t *pcstack, int pcstack_limit, int aframes,
    uint32_t *intrpc)
{
	int depth = 0;
	vm_offset_t callpc;
	pc_t caller = (pc_t) solaris_cpu[curcpu].cpu_dtrace_caller;
	register_t sp, ra, pc;

	if (intrpc != 0)
		pcstack[depth++] = (pc_t) intrpc;

	aframes++;

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

	while (depth < pcstack_limit) {

		callpc = pc;

		if (aframes > 0) {
			aframes--;
			if ((aframes == 0) && (caller != 0)) {
				pcstack[depth++] = caller;
			}
		}
		else {
			pcstack[depth++] = callpc;
		}

		if (dtrace_next_frame(&pc, &sp, NULL, NULL) < 0)
			break;
	}

	for (; depth < pcstack_limit; depth++) {
		pcstack[depth] = 0;
	}
}

void
dtrace_getupcstack(uint64_t *pcstack, int pcstack_limit)
{
	proc_t *p = curproc;
	struct trapframe *tf;
	register_t sp, ra, pc;
	volatile uint16_t *flags =
	    (volatile uint16_t *)&cpu_core[curcpu].cpuc_dtrace_flags;

	if (*flags & CPU_DTRACE_FAULT)
		return;

	if (pcstack_limit <= 0)
		return;

	/*
	 * If there's no user context we still need to zero the stack.
	 */
	if (p == NULL || (tf = curthread->td_frame) == NULL)
		goto zero;

	*pcstack++ = (uint64_t)p->p_pid;
	pcstack_limit--;

	if (pcstack_limit <= 0)
		return;

	pc = (uint64_t)tf->pc;
	sp = (uint64_t)tf->sp;
	ra = (uint64_t)tf->ra;
	*pcstack++ = (uint64_t)tf->pc;
	
	/*
	 * Unwind, and unwind, and unwind
	 */
	while (1) {
		if (dtrace_next_uframe(&pc, &sp, &ra) < 0)
			break;

		*pcstack++ = pc;
		pcstack_limit--;

		if (pcstack_limit <= 0)
			break;
	}

zero:
	while (pcstack_limit-- > 0)
		*pcstack++ = 0;
}

int
dtrace_getustackdepth(void)
{
	int n = 0;
	proc_t *p = curproc;
	struct trapframe *tf;
	register_t sp, ra, pc;
	volatile uint16_t *flags =
	    (volatile uint16_t *)&cpu_core[curcpu].cpuc_dtrace_flags;

	if (*flags & CPU_DTRACE_FAULT)
		return (0);

	if (p == NULL || (tf = curthread->td_frame) == NULL)
		return (0);

	pc = (uint64_t)tf->pc;
	sp = (uint64_t)tf->sp;
	ra = (uint64_t)tf->ra;
	n++;
	
	/*
	 * Unwind, and unwind, and unwind
	 */
	while (1) {
		if (dtrace_next_uframe(&pc, &sp, &ra) < 0)
			break;
		n++;
	}

	return (n);
}

void
dtrace_getufpstack(uint64_t *pcstack, uint64_t *fpstack, int pcstack_limit)
{
	printf("IMPLEMENT ME: %s\n", __func__);
}

/*ARGSUSED*/
uint64_t
dtrace_getarg(int arg, int aframes)
{
	int i;
	register_t sp, ra, pc;
	/* XXX: Fix this ugly code */
	register_t args[8];
	int valid[8];

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

	for (i = 0; i <= aframes + 1; i++) {
		if (dtrace_next_frame(&pc, &sp, args, valid) < 0) {
			printf("%s: stack ends at frame #%d\n", __func__, i);
			return (0);
		}
	}

	if (arg < 8) {
		if (valid[arg])
			return (args[arg]);
		else
			printf("%s: request arg%d is not valid\n", __func__, arg);
	}

	return (0);
}

int
dtrace_getstackdepth(int aframes)
{
	register_t sp, ra, pc;
	int depth = 0;

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

	for (;;) {
		if (dtrace_next_frame(&pc, &sp, NULL, NULL) < 0)
			break;
		depth++;
	}

	if (depth < aframes)
		return 0;
	else
		return depth - aframes;
}

ulong_t
dtrace_getreg(struct trapframe *rp, uint_t reg)
{

	return (0);
}

static int
dtrace_next_frame(register_t *pc, register_t *sp,
	register_t *args, int *valid_args)
{
	InstFmt i;
	/*
	 * Arrays for a0..a3 registers and flags if content
	 * of these registers is valid, e.g. obtained from the stack
	 */
	uintptr_t va;
	unsigned instr, mask;
	unsigned int frames = 0;
	int more, stksize;
	register_t ra = 0;
	int arg, r;
	vm_offset_t addr;

	/*
	 * Invalidate arguments values
	 */
	if (valid_args) {
		for (r = 0; r < 8; r++)
			valid_args[r] = 0;
	}

	/* Jump here after a nonstandard (interrupt handler) frame */
	stksize = 0;
	if (frames++ > 100) {
		/* return breaks stackframe-size heuristics with gcc -O2 */
		goto error;	/* XXX */
	}

	/* check for bad SP: could foul up next frame */
	if (!MIPS_IS_VALID_KERNELADDR(*sp)) {
		goto error;
	}

	/* check for bad PC */
	if (!MIPS_IS_VALID_KERNELADDR(*pc)) {
		goto error;
	}

	/*
	 * Find the beginning of the current subroutine by scanning
	 * backwards from the current PC for the end of the previous
	 * subroutine.
	 */
	va = *pc - sizeof(int);
	while (1) {
		instr = kdbpeek((int *)va);

		/* [d]addiu sp,sp,-X */
		if (((instr & 0xffff8000) == 0x27bd8000)
		    || ((instr & 0xffff8000) == 0x67bd8000))
			break;

		/* jr	ra */
		if (instr == 0x03e00008) {
			/* skip over branch-delay slot instruction */
			va += 2 * sizeof(int);
			break;
		}

		va -= sizeof(int);
	}

	/* skip over nulls which might separate .o files */
	while ((instr = kdbpeek((int *)va)) == 0)
		va += sizeof(int);

	/* scan forwards to find stack size and any saved registers */
	stksize = 0;
	more = 3;
	mask = 0;
	for (; more; va += sizeof(int),
	    more = (more == 3) ? 3 : more - 1) {
		/* stop if hit our current position */
		if (va >= *pc)
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
			};
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
			};
			break;

		case OP_SW:
			/* look for saved registers on the stack */
			if (i.IType.rs != 29)
				break;
			/* only restore the first one */
			if (mask & (1 << i.IType.rt))
				break;
			mask |= (1 << i.IType.rt);
			addr = (vm_offset_t)(*sp + (short)i.IType.imm);
			switch (i.IType.rt) {
			case 4:/* a0 */
			case 5:/* a1 */
			case 6:/* a2 */
			case 7:/* a3 */
#if defined(__mips_n64) || defined(__mips_n32)
			case 8:/* a4 */
			case 9:/* a5 */
			case 10:/* a6 */
			case 11:/* a7 */
#endif
				arg = i.IType.rt - 4;
				if (args)
					args[arg] = kdbpeek((int*)addr);
				if (valid_args)
					valid_args[arg] = 1;
				break;
			case 31:	/* ra */
				ra = kdbpeek((int *)addr);
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
			addr = (vm_offset_t)(*sp + (short)i.IType.imm);
			switch (i.IType.rt) {
			case 4:/* a0 */
			case 5:/* a1 */
			case 6:/* a2 */
			case 7:/* a3 */
#if defined(__mips_n64) || defined(__mips_n32)
			case 8:/* a4 */
			case 9:/* a5 */
			case 10:/* a6 */
			case 11:/* a7 */
#endif
				arg = i.IType.rt - 4;
				if (args)
					args[arg] = kdbpeekd((int *)addr);
				if (valid_args)
					valid_args[arg] = 1;
				break;

			case 31:	/* ra */
				ra = kdbpeekd((int *)addr);
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

	if (!MIPS_IS_VALID_KERNELADDR(ra)) 
		return (-1);

	*pc = ra;
	*sp += stksize;

#if defined(__mips_o32)
	/*
	 * For MIPS32 fill out arguments 5..8 from the stack
	 */
	for (arg = 4; arg < 8; arg++) {
		addr = (vm_offset_t)(*sp + arg*sizeof(register_t));
		if (args)
			args[arg] = kdbpeekd((int *)addr);
		if (valid_args)
			valid_args[arg] = 1;
	}
#endif

	return (0);
error:
	return (-1);
}

static int
dtrace_next_uframe(register_t *pc, register_t *sp, register_t *ra)
{
	int offset, registers_on_stack;
	uint32_t opcode, mask;
	register_t function_start;
	int stksize;
	InstFmt i;

	volatile uint16_t *flags =
	    (volatile uint16_t *)&cpu_core[curcpu].cpuc_dtrace_flags;

	registers_on_stack = 0;
	mask = 0;
	function_start = 0;
	offset = 0;
	stksize = 0;

	while (offset < MAX_FUNCTION_SIZE) {
		opcode = dtrace_fuword32((void *)(vm_offset_t)(*pc - offset));

		if (*flags & CPU_DTRACE_FAULT)
			goto fault;

		/* [d]addiu sp, sp, -X*/
		if (((opcode & 0xffff8000) == 0x27bd8000)
		    || ((opcode & 0xffff8000) == 0x67bd8000)) {
			function_start = *pc - offset;
			registers_on_stack = 1;
			break;
		}

		/* lui gp, X */
		if ((opcode & 0xffff8000) == 0x3c1c0000) {
			/*
			 * Function might start with this instruction
			 * Keep an eye on "jr ra" and sp correction
			 * with positive value further on
			 */
			function_start = *pc - offset;
		}

		if (function_start) {
			/* 
			 * Stop looking further. Possible end of
			 * function instruction: it means there is no
			 * stack modifications, sp is unchanged
			 */

			/* [d]addiu sp,sp,X */
			if (((opcode & 0xffff8000) == 0x27bd0000)
			    || ((opcode & 0xffff8000) == 0x67bd0000))
				break;

			if (opcode == 0x03e00008)
				break;
		}

		offset += sizeof(int);
	}

	if (!function_start)
		return (-1);

	if (registers_on_stack) {
		offset = 0;
		while ((offset < MAX_PROLOGUE_SIZE) 
		    && ((function_start + offset) < *pc)) {
			i.word = 
			    dtrace_fuword32((void *)(vm_offset_t)(function_start + offset));
			switch (i.JType.op) {
			case OP_SW:
				/* look for saved registers on the stack */
				if (i.IType.rs != 29)
					break;
				/* only restore the first one */
				if (mask & (1 << i.IType.rt))
					break;
				mask |= (1 << i.IType.rt);
				if (i.IType.rt == 31)
					*ra = dtrace_fuword32((void *)(vm_offset_t)(*sp + (short)i.IType.imm));
				break;

			case OP_SD:
				/* look for saved registers on the stack */
				if (i.IType.rs != 29)
					break;
				/* only restore the first one */
				if (mask & (1 << i.IType.rt))
					break;
				mask |= (1 << i.IType.rt);
				/* ra */
				if (i.IType.rt == 31)
					*ra = dtrace_fuword64((void *)(vm_offset_t)(*sp + (short)i.IType.imm));
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

			offset += sizeof(int);

			if (*flags & CPU_DTRACE_FAULT)
				goto fault;
		}
	}

	/*
	 * We reached the end of backtrace
	 */
	if (*pc == *ra)
		return (-1);

	*pc = *ra;
	*sp += stksize;

	return (0);
fault:
	/*
	 * We just got lost in backtrace, no big deal
	 */
	*flags &= ~CPU_DTRACE_FAULT;
	return (-1);
}

static int
dtrace_copycheck(uintptr_t uaddr, uintptr_t kaddr, size_t size)
{

	if (uaddr + size > VM_MAXUSER_ADDRESS || uaddr + size < uaddr) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
		cpu_core[curcpu].cpuc_dtrace_illval = uaddr;
		return (0);
	}

	return (1);
}

void
dtrace_copyin(uintptr_t uaddr, uintptr_t kaddr, size_t size,
    volatile uint16_t *flags)
{
	if (dtrace_copycheck(uaddr, kaddr, size))
		dtrace_copy(uaddr, kaddr, size);
}

void
dtrace_copyout(uintptr_t kaddr, uintptr_t uaddr, size_t size,
    volatile uint16_t *flags)
{
	if (dtrace_copycheck(uaddr, kaddr, size))
		dtrace_copy(kaddr, uaddr, size);
}

void
dtrace_copyinstr(uintptr_t uaddr, uintptr_t kaddr, size_t size,
    volatile uint16_t *flags)
{
	if (dtrace_copycheck(uaddr, kaddr, size))
		dtrace_copystr(uaddr, kaddr, size, flags);
}

void
dtrace_copyoutstr(uintptr_t kaddr, uintptr_t uaddr, size_t size,
    volatile uint16_t *flags)
{
	if (dtrace_copycheck(uaddr, kaddr, size))
		dtrace_copystr(kaddr, uaddr, size, flags);
}

uint8_t
dtrace_fuword8(void *uaddr)
{
	if ((uintptr_t)uaddr > VM_MAXUSER_ADDRESS) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
		cpu_core[curcpu].cpuc_dtrace_illval = (uintptr_t)uaddr;
		return (0);
	}
	return (dtrace_fuword8_nocheck(uaddr));
}

uint16_t
dtrace_fuword16(void *uaddr)
{
	if ((uintptr_t)uaddr > VM_MAXUSER_ADDRESS) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
		cpu_core[curcpu].cpuc_dtrace_illval = (uintptr_t)uaddr;
		return (0);
	}
	return (dtrace_fuword16_nocheck(uaddr));
}

uint32_t
dtrace_fuword32(void *uaddr)
{
	if ((uintptr_t)uaddr > VM_MAXUSER_ADDRESS) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
		cpu_core[curcpu].cpuc_dtrace_illval = (uintptr_t)uaddr;
		return (0);
	}
	return (dtrace_fuword32_nocheck(uaddr));
}

uint64_t
dtrace_fuword64(void *uaddr)
{
	if ((uintptr_t)uaddr > VM_MAXUSER_ADDRESS) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
		cpu_core[curcpu].cpuc_dtrace_illval = (uintptr_t)uaddr;
		return (0);
	}
	return (dtrace_fuword64_nocheck(uaddr));
}
