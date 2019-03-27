/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Antoine Brodin
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/stack.h>

#include <machine/mips_opcode.h>

#include <machine/pcb.h>
#include <machine/regnum.h>

static u_register_t
stack_register_fetch(u_register_t sp, u_register_t stack_pos)
{
	u_register_t * stack = 
	    ((u_register_t *)(intptr_t)sp + (size_t)stack_pos/sizeof(u_register_t));

	return *stack;
}

static void
stack_capture(struct stack *st, u_register_t pc, u_register_t sp)
{
	u_register_t  ra = 0, i, stacksize;
	short ra_stack_pos = 0;
	InstFmt insn;

	stack_zero(st);

	for (;;) {
		stacksize = 0;
		if (pc <= (u_register_t)(intptr_t)btext)
			break;
		for (i = pc; i >= (u_register_t)(intptr_t)btext; i -= sizeof (insn)) {
			bcopy((void *)(intptr_t)i, &insn, sizeof insn);
			switch (insn.IType.op) {
			case OP_ADDI:
			case OP_ADDIU:
			case OP_DADDI:
			case OP_DADDIU:
				if (insn.IType.rs != SP || insn.IType.rt != SP)
					break;
				stacksize = -(short)insn.IType.imm;
				break;

			case OP_SW:
			case OP_SD:
				if (insn.IType.rs != SP || insn.IType.rt != RA)
					break;
				ra_stack_pos = (short)insn.IType.imm;
				break;
			default:
				break;
			}

			if (stacksize)
				break;
		}

		if (stack_put(st, pc) == -1)
			break;

		for (i = pc; !ra; i += sizeof (insn)) {
			bcopy((void *)(intptr_t)i, &insn, sizeof insn);

			switch (insn.IType.op) {
			case OP_SPECIAL:
				if (insn.RType.func == OP_JR) {
					if (ra >= (u_register_t)(intptr_t)btext)
						break;
					if (insn.RType.rs != RA)
						break;
					ra = stack_register_fetch(sp, 
					    ra_stack_pos);
					if (!ra)
						goto done;
					ra -= 8;
				}
				break;
			default:
				break;
			}
			/* eret */
			if (insn.word == 0x42000018)
				goto done;
		}

		if (pc == ra && stacksize == 0)
			break;

		sp += stacksize;
		pc = ra;
		ra = 0;
	}
done:
	return;
}

void
stack_save_td(struct stack *st, struct thread *td)
{
	u_register_t pc, sp;

	if (TD_IS_SWAPPED(td))
		panic("stack_save_td: swapped");
	if (TD_IS_RUNNING(td))
		panic("stack_save_td: running");

	pc = td->td_pcb->pcb_regs.pc;
	sp = td->td_pcb->pcb_regs.sp;
	stack_capture(st, pc, sp);
}

int
stack_save_td_running(struct stack *st, struct thread *td)
{

	return (EOPNOTSUPP);
}

void
stack_save(struct stack *st)
{
	u_register_t pc, sp;

	if (curthread == NULL)
		panic("stack_save: curthread == NULL");

	pc = curthread->td_pcb->pcb_regs.pc;
	sp = curthread->td_pcb->pcb_regs.sp;
	stack_capture(st, pc, sp);
}
