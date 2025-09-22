/*	$OpenBSD: process_machdep.c,v 1.13 2007/09/09 20:49:18 kettenis Exp $	*/
/*	$NetBSD: process_machdep.c,v 1.1 1996/09/30 16:34:53 ws Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
	struct cpu_info *ci = curcpu();
	struct trapframe *tf = trapframe(p);
	struct pcb *pcb = &p->p_addr->u_pcb;

	bcopy(tf->fixreg, regs->gpr, sizeof(regs->gpr));

	if (!(pcb->pcb_flags & PCB_FPU)) {
		bzero(regs->fpr, sizeof(regs->fpr));
	} else {
		/* XXX What if the state is on the other cpu? */
		if (p == ci->ci_fpuproc)
			save_fpu();
		bcopy(pcb->pcb_fpu.fpr, regs->fpr, sizeof(regs->fpr));
	}

	regs->pc  = tf->srr0;
	regs->ps  = tf->srr1; /* is this the correct value for this ? */
	regs->cnd = tf->cr;
	regs->lr  = tf->lr;
	regs->cnt = tf->ctr;
	regs->xer = tf->xer;
	regs->mq  = 0; /*  what should this really be? */

	return (0);
}

int
process_read_fpregs(struct proc *p, struct fpreg *regs)
{
	struct cpu_info *ci = curcpu();
	struct pcb *pcb = &p->p_addr->u_pcb;

	if (!(pcb->pcb_flags & PCB_FPU)) {
		bzero(regs->fpr, sizeof(regs->fpr));
		regs->fpscr = 0;
	} else {
		/* XXX What if the state is on the other cpu? */
		if (p == ci->ci_fpuproc)
			save_fpu();
		bcopy(pcb->pcb_fpu.fpr, regs->fpr, sizeof(regs->fpr));
		regs->fpscr = *(u_int64_t *)&pcb->pcb_fpu.fpcsr;
	}

	return (0);
}

#ifdef PTRACE

/*
 * Set the process's program counter.
 */
int
process_set_pc(struct proc *p, caddr_t addr)
{
	struct trapframe *tf = trapframe(p);
	
	tf->srr0 = (u_int32_t)addr;
	return 0;
}

int
process_sstep(struct proc *p, int sstep)
{
	struct trapframe *tf = trapframe(p);
	
	if (sstep)
		tf->srr1 |= PSL_SE;
	else
		tf->srr1 &= ~PSL_SE;
	return 0;
}

int
process_write_regs(struct proc *p, struct reg *regs)
{
	struct cpu_info *ci = curcpu();
	struct trapframe *tf = trapframe(p);
	struct pcb *pcb = &p->p_addr->u_pcb;

	if ((regs->ps ^ tf->srr1) & PSL_USERSTATIC)
		return EINVAL;

	bcopy(regs->gpr, tf->fixreg, sizeof(regs->gpr));

	/* XXX What if the state is on the other cpu? */
	if (p == ci->ci_fpuproc) {	/* release the fpu */
		save_fpu();
		ci->ci_fpuproc = NULL;
	}

	bcopy(regs->fpr, pcb->pcb_fpu.fpr, sizeof(regs->fpr));
	if (!(pcb->pcb_flags & PCB_FPU)) {
		pcb->pcb_fpu.fpcsr = 0;
		pcb->pcb_flags |= PCB_FPU;
	}

	tf->srr0 = regs->pc;
	tf->srr1 = regs->ps;  /* is this the correct value for this ? */
	tf->cr   = regs->cnd;
	tf->lr   = regs->lr;
	tf->ctr  = regs->cnt;
	tf->xer  = regs->xer;
	/*  regs->mq = 0; what should this really be? */

	return (0);
}

int
process_write_fpregs(struct proc *p, struct fpreg *regs)
{
	struct cpu_info *ci = curcpu();
	struct pcb *pcb = &p->p_addr->u_pcb;
	u_int64_t fpscr = regs->fpscr;

	/* XXX What if the state is on the other cpu? */
	if (p == ci->ci_fpuproc) {	/* release the fpu */
		save_fpu();
		ci->ci_fpuproc = NULL;
	}

	bcopy(regs->fpr, pcb->pcb_fpu.fpr, sizeof(regs->fpr));
	pcb->pcb_fpu.fpcsr = *(double *)&fpscr;
	pcb->pcb_flags |= PCB_FPU;

	return (0);
}

#endif	/* PTRACE */
