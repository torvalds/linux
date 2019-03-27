/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
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
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/proc.h>
#include <sys/stack.h>
#include <sys/sysent.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>

#include <machine/cpu.h>
#include <machine/pcb.h>
#include <machine/stack.h>
#include <machine/trap.h>
#include <machine/vmparam.h>

#include <ddb/ddb.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_variables.h>
#include <ddb/db_watch.h>

static db_varfcn_t db_frame;

#define	DB_OFFSET(x)	(db_expr_t *)offsetof(struct trapframe, x)
struct	db_variable db_regs[] = {
	{ "g0",		DB_OFFSET(tf_global[0]),	db_frame },
	{ "g1",		DB_OFFSET(tf_global[1]),	db_frame },
	{ "g2",		DB_OFFSET(tf_global[2]),	db_frame },
	{ "g3",		DB_OFFSET(tf_global[3]),	db_frame },
	{ "g4",		DB_OFFSET(tf_global[4]),	db_frame },
	{ "g5",		DB_OFFSET(tf_global[5]),	db_frame },
	{ "g6",		DB_OFFSET(tf_global[6]),	db_frame },
	{ "g7",		DB_OFFSET(tf_global[7]),	db_frame },
	{ "i0",		DB_OFFSET(tf_out[0]),		db_frame },
	{ "i1",		DB_OFFSET(tf_out[1]),		db_frame },
	{ "i2",		DB_OFFSET(tf_out[2]),		db_frame },
	{ "i3",		DB_OFFSET(tf_out[3]),		db_frame },
	{ "i4",		DB_OFFSET(tf_out[4]),		db_frame },
	{ "i5",		DB_OFFSET(tf_out[5]),		db_frame },
	{ "i6",		DB_OFFSET(tf_out[6]),		db_frame },
	{ "i7",		DB_OFFSET(tf_out[7]),		db_frame },
	{ "tnpc",	DB_OFFSET(tf_tnpc),		db_frame },
	{ "tpc",	DB_OFFSET(tf_tpc),		db_frame },
	{ "tstate",	DB_OFFSET(tf_tstate),		db_frame },
};
struct db_variable *db_eregs = db_regs + nitems(db_regs);

static int
db_frame(struct db_variable *vp, db_expr_t *valuep, int op)
{
	uint64_t *reg;

	if (kdb_frame == NULL)
		return (0);
	reg = (uint64_t*)((uintptr_t)kdb_frame + (uintptr_t)vp->valuep);
	if (op == DB_VAR_GET)
		*valuep = *reg;
	else
		*reg = *valuep;
	return (1);
}

/*
 * User stack trace (debugging aid).
 */
static void
db_utrace(struct thread *td, struct trapframe *tf, int count)
{
	struct pcb *pcb;
	db_addr_t sp, rsp, o7, pc;
	int i, found;

	pcb = td->td_pcb;
	sp = db_get_value((db_addr_t)&tf->tf_sp, sizeof(tf->tf_sp), FALSE);
	o7 = db_get_value((db_addr_t)&tf->tf_out[7], sizeof(tf->tf_out[7]),
	    FALSE);
	pc = db_get_value((db_addr_t)&tf->tf_tpc, sizeof(tf->tf_tpc), FALSE);
	db_printf("user trace: trap %%o7=%#lx\n", o7);
	while (count-- && sp != 0 && !db_pager_quit) {
		db_printf("pc %#lx, sp %#lx\n", pc, sp);
		/* First, check whether the frame is in the pcb. */
		found = 0;
		for (i = 0; i < pcb->pcb_nsaved; i++) {
			if (pcb->pcb_rwsp[i] == sp) {
				found = 1;
				sp = pcb->pcb_rw[i].rw_in[6];
				pc = pcb->pcb_rw[i].rw_in[7];
				break;
			}
		}
		if (!found) {
			rsp = sp + SPOFF;
			sp = 0;
			if (copyin((void *)(rsp + offsetof(struct frame, fr_fp)),
			    &sp, sizeof(sp)) != 0 ||
			    copyin((void *)(rsp + offsetof(struct frame, fr_pc)),
			    &pc, sizeof(pc)) != 0)
				break;
		}
	}
	db_printf("done\n");
}

static int
db_print_trap(struct thread *td, struct trapframe *tf, int count)
{
	struct proc *p;
	const char *symname;
	c_db_sym_t sym;
	db_expr_t diff;
	db_addr_t func;
	db_addr_t tpc;
	u_long type;
	u_long sfar;
	u_long sfsr;
	u_long tar;
	u_long level;
	u_long pil;
	u_long code;
	u_long o7;
	int user;

	p = td->td_proc;
	type = db_get_value((db_addr_t)&tf->tf_type,
	    sizeof(tf->tf_type), FALSE);
	db_printf("-- %s", trap_msg[type & ~T_KERNEL]);
	switch (type & ~T_KERNEL) {
	case T_DATA_PROTECTION:
		tar = (u_long)db_get_value((db_addr_t)&tf->tf_tar,
		    sizeof(tf->tf_tar), FALSE);
		db_printf(" tar=%#lx", tar);
		/* fall through */
	case T_DATA_EXCEPTION:
	case T_INSTRUCTION_EXCEPTION:
	case T_MEM_ADDRESS_NOT_ALIGNED:
		sfar = (u_long)db_get_value((db_addr_t)&tf->tf_sfar,
		    sizeof(tf->tf_sfar), FALSE);
		sfsr = (u_long)db_get_value((db_addr_t)&tf->tf_sfsr,
		    sizeof(tf->tf_sfsr), FALSE);
		db_printf(" sfar=%#lx sfsr=%#lx", sfar, sfsr);
		break;
	case T_DATA_MISS:
	case T_INSTRUCTION_MISS:
		tar = (u_long)db_get_value((db_addr_t)&tf->tf_tar,
		    sizeof(tf->tf_tar), FALSE);
		db_printf(" tar=%#lx", tar);
		break;
	case T_SYSCALL:
		code = db_get_value((db_addr_t)&tf->tf_global[1],
		    sizeof(tf->tf_global[1]), FALSE);
		db_printf(" (%ld", code);
		if (code >= 0 && code < p->p_sysent->sv_size) {
			func = (db_addr_t)p->p_sysent->sv_table[code].sy_call;
			sym = db_search_symbol(func, DB_STGY_ANY, &diff);
			if (sym != DB_SYM_NULL && diff == 0) {
				db_symbol_values(sym, &symname, NULL);
				db_printf(", %s, %s", p->p_sysent->sv_name,
				    symname);
			}
			db_printf(")");
		}
		break;
	case T_INTERRUPT:
		level = (u_long)db_get_value((db_addr_t)&tf->tf_level,
		    sizeof(tf->tf_level), FALSE);
		pil = (u_long)db_get_value((db_addr_t)&tf->tf_pil,
		    sizeof(tf->tf_pil), FALSE);
		db_printf(" level=%#lx pil=%#lx", level, pil);
		break;
	default:
		break;
	}
	o7 = (u_long)db_get_value((db_addr_t)&tf->tf_out[7],
	    sizeof(tf->tf_out[7]), FALSE);
	db_printf(" %%o7=%#lx --\n", o7);
	user = (type & T_KERNEL) == 0;
	if (user) {
		tpc = db_get_value((db_addr_t)&tf->tf_tpc,
		    sizeof(tf->tf_tpc), FALSE);
		db_printf("userland() at ");
		db_printsym(tpc, DB_STGY_PROC);
		db_printf("\n");
		db_utrace(td, tf, count);
	}
	return (user);
}

static int
db_backtrace(struct thread *td, struct frame *fp, int count)
{
	struct trapframe *tf;
	const char *name;
	c_db_sym_t sym;
	db_expr_t offset;
	db_expr_t value;
	db_addr_t npc;
	db_addr_t pc;
	int trap;
	int user;

	if (count == -1)
		count = 1024;

	trap = 0;
	user = 0;
	npc = 0;
	while (count-- && !user && !db_pager_quit) {
		pc = (db_addr_t)db_get_value((db_addr_t)&fp->fr_pc,
		    sizeof(fp->fr_pc), FALSE);
		if (trap) {
			pc = npc;
			trap = 0;
		}
		if (!INKERNEL((vm_offset_t)pc))
			break;
		sym = db_search_symbol(pc, DB_STGY_ANY, &offset);
		if (sym == C_DB_SYM_NULL) {
			value = 0;
			name = NULL;
		} else
			db_symbol_values(sym, &name, &value);
		if (name == NULL)
			name = "(null)";
		fp = (struct frame *)(db_get_value((db_addr_t)&fp->fr_fp,
		   sizeof(fp->fr_fp), FALSE) + SPOFF);
		if ((value > (u_long)tl_trap_begin &&
		    value < (u_long)tl_trap_end) ||
		    (value > (u_long)tl_text_begin &&
		    value < (u_long)tl_text_end)) {
			tf = (struct trapframe *)(fp + 1);
			npc = db_get_value((db_addr_t)&tf->tf_tpc,
			    sizeof(tf->tf_tpc), FALSE);
			user = db_print_trap(td, tf, count);
			trap = 1;
		} else {
			db_printf("%s() at ", name);
			db_printsym(pc, DB_STGY_PROC);
			db_printf("\n");
		}
	}
	return (0);
}

void
db_trace_self(void)
{

	db_backtrace(curthread,
	    (struct frame *)__builtin_frame_address(1), -1);
}

int
db_trace_thread(struct thread *td, int count)
{
	struct pcb *ctx;

	ctx = kdb_thr_ctx(td);
	return (db_backtrace(td,
	    (struct frame *)(ctx->pcb_sp + SPOFF), count));
}
