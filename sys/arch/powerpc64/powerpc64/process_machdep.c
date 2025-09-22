/*	$OpenBSD: process_machdep.c,v 1.5 2021/01/09 13:14:02 kettenis Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/user.h>

#include <machine/fpu.h>
#include <machine/pcb.h>
#include <machine/psl.h>
#include <machine/reg.h>

int
process_read_regs(struct proc *p, struct reg *regs)
{
	struct trapframe *tf = p->p_md.md_regs;

	memcpy(regs->r_reg, tf->fixreg, sizeof(regs->r_reg));

	regs->r_lr = tf->lr;
	regs->r_cr = tf->cr;
	regs->r_xer = tf->xer;
	regs->r_ctr = tf->ctr;
	regs->r_pc = tf->srr0;
	regs->r_ps = tf->srr1;

	return 0;
}

int
process_read_fpregs(struct proc *p, struct fpreg *regs)
{
	struct trapframe *tf = p->p_md.md_regs;
	struct pcb *pcb = &p->p_addr->u_pcb;

	if (tf->srr1 & (PSL_FPU|PSL_VEC|PSL_VSX)) {
		tf->srr1 &= ~(PSL_FPU|PSL_VEC|PSL_VSX);
		save_vsx(p);
	}

	if (pcb->pcb_flags & (PCB_FPU|PCB_VEC|PCB_VSX))
		memcpy(regs, &pcb->pcb_fpstate, sizeof(*regs));
	else
		memset(regs, 0, sizeof(*regs));

	regs->fp_vrsave = tf->vrsave;

	return 0;
}

#ifdef PTRACE

/*
 * Set the process's program counter.
 */
int
process_set_pc(struct proc *p, caddr_t addr)
{
	struct trapframe *tf = p->p_md.md_regs;
	
	tf->srr0 = (register_t)addr;

	return 0;
}

int
process_sstep(struct proc *p, int sstep)
{
	struct trapframe *tf = p->p_md.md_regs;
	
	if (sstep)
		tf->srr1 |= PSL_SE;
	else
		tf->srr1 &= ~PSL_SE;

	return 0;
}

int
process_write_regs(struct proc *p, struct reg *regs)
{
	struct trapframe *tf = p->p_md.md_regs;

	regs->r_ps &= ~(PSL_FPU|PSL_VEC|PSL_VSX);
	regs->r_ps |= (tf->srr1 & (PSL_FPU|PSL_VEC|PSL_VSX));

	if (regs->r_ps != tf->srr1)
		return EINVAL;

	memcpy(tf->fixreg, regs->r_reg, sizeof(regs->r_reg));

	tf->lr = regs->r_lr;
	tf->cr = regs->r_cr;
	tf->xer = regs->r_xer;
	tf->ctr = regs->r_ctr;
	tf->srr0 = regs->r_pc;
	tf->srr1 = regs->r_ps;

	return 0;
}

int
process_write_fpregs(struct proc *p, struct fpreg *regs)
{
	struct trapframe *tf = p->p_md.md_regs;
	struct pcb *pcb = &p->p_addr->u_pcb;

	tf->srr1 &= ~(PSL_FPU|PSL_VEC|PSL_VSX);
	memcpy(&pcb->pcb_fpstate, regs, sizeof(*regs));
	pcb->pcb_flags |= (PCB_FPU|PCB_VEC|PCB_VSX);

	tf->vrsave = regs->fp_vrsave;

	return 0;
}

#endif	/* PTRACE */
