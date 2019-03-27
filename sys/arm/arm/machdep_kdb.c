/*	$NetBSD: arm32_machdep.c,v 1.44 2004/03/24 15:34:47 atatat Exp $	*/

/*-
 * Copyright (c) 2004 Olivier Houchard
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
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

#include "opt_ddb.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <machine/cpu.h>
#include <machine/reg.h>

#ifdef DDB
#include <ddb/ddb.h>

#if __ARM_ARCH >= 6

DB_SHOW_COMMAND(cp15, db_show_cp15)
{
	u_int reg;

	reg = cp15_midr_get();
	db_printf("Cpu ID: 0x%08x\n", reg);
	reg = cp15_ctr_get();
	db_printf("Current Cache Lvl ID: 0x%08x\n",reg);

	reg = cp15_sctlr_get();
	db_printf("Ctrl: 0x%08x\n",reg);
	reg = cp15_actlr_get();
	db_printf("Aux Ctrl: 0x%08x\n",reg);

	reg = cp15_id_pfr0_get();
	db_printf("Processor Feat 0: 0x%08x\n", reg);
	reg = cp15_id_pfr1_get();
	db_printf("Processor Feat 1: 0x%08x\n", reg);
	reg = cp15_id_dfr0_get();
	db_printf("Debug Feat 0: 0x%08x\n", reg);
	reg = cp15_id_afr0_get();
	db_printf("Auxiliary Feat 0: 0x%08x\n", reg);
	reg = cp15_id_mmfr0_get();
	db_printf("Memory Model Feat 0: 0x%08x\n", reg);
	reg = cp15_id_mmfr1_get();
	db_printf("Memory Model Feat 1: 0x%08x\n", reg);
	reg = cp15_id_mmfr2_get();
	db_printf("Memory Model Feat 2: 0x%08x\n", reg);
	reg = cp15_id_mmfr3_get();
	db_printf("Memory Model Feat 3: 0x%08x\n", reg);
	reg = cp15_ttbr_get();
	db_printf("TTB0: 0x%08x\n", reg);
}

DB_SHOW_COMMAND(vtop, db_show_vtop)
{
	u_int reg;

	if (have_addr) {
		cp15_ats1cpr_set(addr);
		reg = cp15_par_get();
		db_printf("Physical address reg: 0x%08x\n",reg);
	} else
		db_printf("show vtop <virt_addr>\n");
}
#endif /* __ARM_ARCH >= 6 */
#endif /* DDB */

int
fill_regs(struct thread *td, struct reg *regs)
{
	struct trapframe *tf = td->td_frame;
	bcopy(&tf->tf_r0, regs->r, sizeof(regs->r));
	regs->r_sp = tf->tf_usr_sp;
	regs->r_lr = tf->tf_usr_lr;
	regs->r_pc = tf->tf_pc;
	regs->r_cpsr = tf->tf_spsr;
	return (0);
}

int
fill_fpregs(struct thread *td, struct fpreg *regs)
{
	bzero(regs, sizeof(*regs));
	return (0);
}

int
set_regs(struct thread *td, struct reg *regs)
{
	struct trapframe *tf = td->td_frame;

	bcopy(regs->r, &tf->tf_r0, sizeof(regs->r));
	tf->tf_usr_sp = regs->r_sp;
	tf->tf_usr_lr = regs->r_lr;
	tf->tf_pc = regs->r_pc;
	tf->tf_spsr &=  ~PSR_FLAGS;
	tf->tf_spsr |= regs->r_cpsr & PSR_FLAGS;
	return (0);
}

int
set_fpregs(struct thread *td, struct fpreg *regs)
{
	return (0);
}

int
fill_dbregs(struct thread *td, struct dbreg *regs)
{

	bzero(regs, sizeof(*regs));
	return (0);
}

int
set_dbregs(struct thread *td, struct dbreg *regs)
{
	return (0);
}
