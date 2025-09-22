/*	$OpenBSD: process_machdep.c,v 1.7 2021/03/25 04:12:00 jsg Exp $	*/
/*	$NetBSD: process_machdep.c,v 1.11 2003/08/07 16:26:52 agc Exp $	*/

/*
 * Copyright (c) 1993 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
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
 */

/*
 * Copyright (c) 1995 Frank Lancaster.  All rights reserved.
 * Copyright (c) 1995 Tools GmbH.  All rights reserved.
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
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
 * process_sstep(proc, sstep)
 *	Arrange for the process to trap or not trap depending on sstep
 *	after executing a single instruction.
 *
 * process_set_pc(proc)
 *	Set the process's program counter.
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/systm.h>
#include <sys/user.h>

#include <arm/armreg.h>
#include <arm/vfp.h>

static __inline struct trapframe *
process_frame(struct proc *p)
{

	return p->p_addr->u_pcb.pcb_tf;
}

int
process_read_regs(struct proc *p, struct reg *regs)
{
	struct trapframe *tf = process_frame(p);

	KASSERT(tf != NULL);
	bcopy((caddr_t)&tf->tf_r0, (caddr_t)regs->r, sizeof(regs->r));
	regs->r_sp = tf->tf_usr_sp;
	regs->r_lr = tf->tf_usr_lr;
	regs->r_pc = tf->tf_pc;
	regs->r_cpsr = tf->tf_spsr;

#ifdef DIAGNOSTIC
	if ((tf->tf_spsr & PSR_MODE) == PSR_USR32_MODE
	    && tf->tf_spsr & PSR_I)
		panic("process_read_regs: Interrupts blocked in user process");
#endif

	return(0);
}

int
process_read_fpregs(struct proc *p, struct fpreg *regs)
{
	if (p->p_addr->u_pcb.pcb_flags & PCB_FPU)
		memcpy(regs, &p->p_addr->u_pcb.pcb_fpstate, sizeof(*regs));
	else
		memset(regs, 0, sizeof(*regs));

	return(0);
}

#ifdef	PTRACE

int
process_write_regs(struct proc *p, struct reg *regs)
{
	struct trapframe *tf = process_frame(p);

	KASSERT(tf != NULL);
	bcopy((caddr_t)regs->r, (caddr_t)&tf->tf_r0, sizeof(regs->r));
	tf->tf_usr_sp = regs->r_sp;
	tf->tf_usr_lr = regs->r_lr;
	tf->tf_pc = regs->r_pc;
	tf->tf_spsr &=  ~PSR_FLAGS;
	tf->tf_spsr |= regs->r_cpsr & PSR_FLAGS;
#ifdef DIAGNOSTIC
	if ((tf->tf_spsr & PSR_MODE) == PSR_USR32_MODE
	    && tf->tf_spsr & PSR_I)
		panic("process_write_regs: Interrupts blocked in user process");
#endif

	return(0);
}

int
process_write_fpregs(struct proc *p, struct fpreg *regs)
{
	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		vfp_discard(p);

	memcpy(&p->p_addr->u_pcb.pcb_fpstate, regs, sizeof(*regs));
	p->p_addr->u_pcb.pcb_flags |= PCB_FPU;
	return(0);
}

int
process_sstep(struct proc *p, int sstep)
{
	if (sstep)
		return (EINVAL);
	return 0;
}

int
process_set_pc(struct proc *p, caddr_t addr)
{
	struct trapframe *tf = process_frame(p);

	KASSERT(tf != NULL);
	tf->tf_pc = (int)addr;

	return (0);
}

#endif	/* PTRACE */
