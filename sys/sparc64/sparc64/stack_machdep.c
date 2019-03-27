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

#include "opt_kstack_pages.h"

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/stack.h>
#include <sys/systm.h>

#include <machine/pcb.h>
#include <machine/stack.h>
#include <machine/vmparam.h>

static void stack_capture(struct stack *st, struct frame *frame);

static void
stack_capture(struct stack *st, struct frame *frame)
{
	struct frame *fp;
	vm_offset_t callpc;

	stack_zero(st);
	fp = frame;
	for (;;) {
		if (!INKERNEL((vm_offset_t)fp) ||
		    !ALIGNED_POINTER(fp, uint64_t))
                        break;
		callpc = fp->fr_pc;
		if (!INKERNEL(callpc))
			break;
		/* Don't bother traversing trap frames. */
		if ((callpc > (uint64_t)tl_trap_begin &&
		    callpc < (uint64_t)tl_trap_end) ||
		    (callpc > (uint64_t)tl_text_begin &&
		    callpc < (uint64_t)tl_text_end))
			break;
		if (stack_put(st, callpc) == -1)
			break;
		if (v9next_frame(fp) <= fp ||
		    v9next_frame(fp) >= frame + KSTACK_PAGES * PAGE_SIZE)
			break;
		fp = v9next_frame(fp);
	}
}

void
stack_save_td(struct stack *st, struct thread *td)
{

	if (TD_IS_SWAPPED(td))
		panic("stack_save_td: swapped");
	if (TD_IS_RUNNING(td))
		panic("stack_save_td: running");

	stack_capture(st, (struct frame *)(td->td_pcb->pcb_sp + SPOFF));
}

int
stack_save_td_running(struct stack *st, struct thread *td)
{

	return (EOPNOTSUPP);
}

void
stack_save(struct stack *st)
{

	stack_capture(st, (struct frame *)__builtin_frame_address(0));
}
