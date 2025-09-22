/*	$OpenBSD: process_machdep.c,v 1.4 2010/06/26 23:24:44 guenther Exp $	*/
/*	$NetBSD: process_machdep.c,v 1.12 2006/01/21 04:12:22 uwe Exp $	*/

/*
 * Copyright (c) 2007 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
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
 * Copyright (c) 1995, 1996, 1997
 *	Charles M. Hannum.  All rights reserved.
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
 * process_read_fpregs(proc, fpregs)
 *	Same as the above, but for floating-point registers.
 *
 * process_write_regs(proc, regs)
 *	Update the current register set from the passed in regs
 *	structure.  Take care to avoid clobbering special CPU
 *	registers or privileged bits in the PSL.
 *	The process is stopped at the time write_regs is called.
 *
 * process_write_fpregs(proc, fpregs)
 *	Same as the above, but for floating-point registers.
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

#include <machine/cpu.h>
#include <machine/pcb.h>
#include <machine/psl.h>
#include <machine/reg.h>

static inline struct trapframe *
process_frame(struct proc *p)
{
	return (p->p_md.md_regs);
}

int
process_read_regs(struct proc *p, struct reg *regs)
{
	struct trapframe *tf = process_frame(p);

	regs->r_spc = tf->tf_spc;
	regs->r_ssr = tf->tf_ssr;
	regs->r_macl = tf->tf_macl;
	regs->r_mach = tf->tf_mach;
	regs->r_pr = tf->tf_pr;
	regs->r_r14 = tf->tf_r14;
	regs->r_r13 = tf->tf_r13;
	regs->r_r12 = tf->tf_r12;
	regs->r_r11 = tf->tf_r11;
	regs->r_r10 = tf->tf_r10;
	regs->r_r9 = tf->tf_r9;
	regs->r_r8 = tf->tf_r8;
	regs->r_r7 = tf->tf_r7;
	regs->r_r6 = tf->tf_r6;
	regs->r_r5 = tf->tf_r5;
	regs->r_r4 = tf->tf_r4;
	regs->r_r3 = tf->tf_r3;
	regs->r_r2 = tf->tf_r2;
	regs->r_r1 = tf->tf_r1;
	regs->r_r0 = tf->tf_r0;
	regs->r_r15 = tf->tf_r15;

	return (0);
}

int
process_read_fpregs(struct proc *p, struct fpreg *fpregs)
{
#ifdef SH4
	if (CPU_IS_SH4) {
		struct pcb *pcb = p->p_md.md_pcb;

		if (p == curproc)
			fpu_save(&pcb->pcb_fp);

		bcopy(&pcb->pcb_fp, fpregs, sizeof(*fpregs));
	}
#endif
#ifdef SH3
	if (CPU_IS_SH3)
		return (EINVAL);
#endif
	return (0);
}

#ifdef PTRACE

int
process_write_regs(struct proc *p, struct reg *regs)
{
	struct trapframe *tf = process_frame(p);

	/*
	 * Check for security violations.
	 */
	if (((regs->r_ssr ^ tf->tf_ssr) & PSL_USERSTATIC) != 0)
		return (EINVAL);

	tf->tf_spc = regs->r_spc;
	tf->tf_ssr = regs->r_ssr;
	tf->tf_pr = regs->r_pr;

	tf->tf_mach = regs->r_mach;
	tf->tf_macl = regs->r_macl;
	tf->tf_r14 = regs->r_r14;
	tf->tf_r13 = regs->r_r13;
	tf->tf_r12 = regs->r_r12;
	tf->tf_r11 = regs->r_r11;
	tf->tf_r10 = regs->r_r10;
	tf->tf_r9 = regs->r_r9;
	tf->tf_r8 = regs->r_r8;
	tf->tf_r7 = regs->r_r7;
	tf->tf_r6 = regs->r_r6;
	tf->tf_r5 = regs->r_r5;
	tf->tf_r4 = regs->r_r4;
	tf->tf_r3 = regs->r_r3;
	tf->tf_r2 = regs->r_r2;
	tf->tf_r1 = regs->r_r1;
	tf->tf_r0 = regs->r_r0;
	tf->tf_r15 = regs->r_r15;

	return (0);
}

int
process_write_fpregs(struct proc *p, struct fpreg *fpregs)
{
#ifdef SH4
	if (CPU_IS_SH4) {
		struct pcb *pcb = p->p_md.md_pcb;

		bcopy(fpregs, &pcb->pcb_fp, sizeof(*fpregs));

		/* force update of live cpu registers */
		if (p == curproc)
			fpu_restore(&pcb->pcb_fp);
	}
#endif
#ifdef SH3
	if (CPU_IS_SH3)
		return (EINVAL);
#endif
	return (0);
}

int
process_sstep(struct proc *p, int sstep)
{
	if (sstep)
		p->p_md.md_flags |= MDP_STEP;
	else
		p->p_md.md_flags &= ~MDP_STEP;

	return (0);
}

int
process_set_pc(struct proc *p, caddr_t addr)
{
	struct trapframe *tf = process_frame(p);

	tf->tf_spc = (int)addr;

	return (0);
}

#endif	/* PTRACE */
