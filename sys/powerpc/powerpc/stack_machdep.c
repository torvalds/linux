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

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/stack.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <machine/db_machdep.h>
#include <machine/pcb.h>
#include <machine/spr.h>
#include <machine/stack.h>
#include <machine/trap.h>

#ifdef __powerpc64__
#define CALLOFFSET 8 /* Account for the TOC reload slot */
#else
#define CALLOFFSET 4
#endif

static void
stack_capture(struct stack *st, vm_offset_t frame)
{
	vm_offset_t callpc;

	stack_zero(st);
	if (frame < PAGE_SIZE)
		return;
	while (1) {
		frame = *(vm_offset_t *)frame;
		if (frame < PAGE_SIZE)
			break;

	    #ifdef __powerpc64__
		callpc = *(vm_offset_t *)(frame + 16) - 4;
	    #else
		callpc = *(vm_offset_t *)(frame + 4) - 4;
	    #endif
		if ((callpc & 3) || (callpc < 0x100))
			break;

		/*
		 * Don't bother traversing trap-frames - there should
		 * be enough info down to the frame to work out where
		 * things are going wrong. Plus, prevents this shortened
		 * version of code from accessing user-space frames
		 */
		if (callpc + CALLOFFSET == (vm_offset_t) &trapexit ||
		    callpc + CALLOFFSET == (vm_offset_t) &asttrapexit)
			break;

		if (stack_put(st, callpc) == -1)
			break;
	}
}

void
stack_save_td(struct stack *st, struct thread *td)
{
	vm_offset_t frame;

	if (TD_IS_SWAPPED(td))
		panic("stack_save_td: swapped");
	if (TD_IS_RUNNING(td))
		panic("stack_save_td: running");

	frame = td->td_pcb->pcb_sp;
	stack_capture(st, frame);
}

int
stack_save_td_running(struct stack *st, struct thread *td)
{

	return (EOPNOTSUPP);
}

void
stack_save(struct stack *st)
{
	register_t frame;

	frame = (register_t)__builtin_frame_address(0);
	stack_capture(st, frame);
}
