/*	$OpenBSD: process_machdep.c,v 1.18 2017/09/01 13:16:47 visa Exp $	*/

/*
 * Copyright (c) 1994 Adam Glass
 * Copyright (c) 1993 The Regents of the University of California.
 * Copyright (c) 1993 Jan-Simon Pendry
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * From:
 *	Id: procfs_i386.c,v 4.1 1993/12/17 10:47:45 jsp Rel
 *
 *	$Id: process_machdep.c,v 1.18 2017/09/01 13:16:47 visa Exp $
 */

/*
 * This file may seem a bit stylized, but that so that it's easier to port.
 * Functions to be implemented here are:
 *
 * process_read_regs(proc, regs)
 *	Get the current user-visible register set from the process
 *	and copy it into the regs structure (<machine/reg.h>).
 *	The process is stopped at the time read_regs is called.
 *
 * process_write_regs(proc, regs)
 *	Update the current register set from the passed in regs
 *	structure.  Take care to avoid clobbering special CPU
 *	registers or privileged bits in the PSL.
 *	The process is stopped at the time write_regs is called.
 *
 * process_sstep(proc)
 *	Arrange for the process to trap after executing a single instruction.
 *
 * process_set_pc(proc)
 *	Set the process's program counter.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/ptrace.h>
#include <machine/fpu.h>
#include <machine/frame.h>
#include <machine/reg.h>

#define	REGSIZE (sizeof(struct trapframe) - sizeof(register_t))

int
process_read_regs(struct proc *p, struct reg *regs)
{
	struct cpu_info *ci = curcpu();

	if (p == ci->ci_fpuproc)
		save_fpu();

	bcopy(&p->p_md.md_regs->ast, &regs->r_regs[AST], REGSIZE);
	regs->r_regs[ZERO] = 0;
	return (0);
}

#ifdef	PTRACE

int
process_write_regs(struct proc *p, struct reg *regs)
{
	struct cpu_info *ci = curcpu();
	register_t sr, ic, ipl;

	if (p == ci->ci_fpuproc)
		save_fpu();

	sr = p->p_md.md_regs->sr;
	ic = p->p_md.md_regs->ic;
	ipl = p->p_md.md_regs->ipl;
	bcopy(&regs->r_regs[AST], &p->p_md.md_regs->ast, REGSIZE);
	p->p_md.md_regs->fsr &= ~FPCSR_C_MASK;
	p->p_md.md_regs->sr = sr;
	p->p_md.md_regs->ic = ic;
	p->p_md.md_regs->ipl = ipl;
	return (0);
}

/* process_sstep is in trap.c */

int
process_set_pc(struct proc *p, caddr_t addr)
{
	p->p_md.md_regs->pc = (register_t)addr;
	return (0);
}

#endif	/* PTRACE */
