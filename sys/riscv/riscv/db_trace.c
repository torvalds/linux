/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * Copyright (c) 2016 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by Semihalf under
 * the sponsorship of the FreeBSD Foundation.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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
#include <sys/kdb.h>
#include <machine/pcb.h>
#include <ddb/ddb.h>
#include <ddb/db_sym.h>

#include <machine/riscvreg.h>
#include <machine/stack.h>

void
db_md_list_watchpoints()
{

}

int
db_md_clr_watchpoint(db_expr_t addr, db_expr_t size)
{

	return (0);
}

int
db_md_set_watchpoint(db_expr_t addr, db_expr_t size)
{

	return (0);
}

static void
db_stack_trace_cmd(struct unwind_state *frame)
{
	const char *name;
	db_expr_t offset;
	db_expr_t value;
	c_db_sym_t sym;
	uint64_t pc;

	while (1) {
		pc = frame->pc;

		if (unwind_frame(frame) < 0)
			break;

		sym = db_search_symbol(pc, DB_STGY_ANY, &offset);
		if (sym == C_DB_SYM_NULL) {
			value = 0;
			name = "(null)";
		} else
			db_symbol_values(sym, &name, &value);

		db_printf("%s() at ", name);
		db_printsym(frame->pc, DB_STGY_PROC);
		db_printf("\n");

		db_printf("\t pc = 0x%016lx ra = 0x%016lx\n",
		    pc, frame->pc);
		db_printf("\t sp = 0x%016lx fp = 0x%016lx\n",
		    frame->sp, frame->fp);
		db_printf("\n");
	}
}

int
db_trace_thread(struct thread *thr, int count)
{
	struct unwind_state frame;
	struct pcb *ctx;

	if (thr != curthread) {
		ctx = kdb_thr_ctx(thr);

		frame.sp = (uint64_t)ctx->pcb_sp;
		frame.fp = (uint64_t)ctx->pcb_s[0];
		frame.pc = (uint64_t)ctx->pcb_ra;
		db_stack_trace_cmd(&frame);
	} else
		db_trace_self();
	return (0);
}

void
db_trace_self(void)
{
	struct unwind_state frame;
	uint64_t sp;

	__asm __volatile("mv %0, sp" : "=&r" (sp));

	frame.sp = sp;
	frame.fp = (uint64_t)__builtin_frame_address(0);
	frame.pc = (uint64_t)db_trace_self;
	db_stack_trace_cmd(&frame);
}
