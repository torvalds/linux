/*	$OpenBSD: process_machdep.c,v 1.30 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: process_machdep.c,v 1.22 1996/05/03 19:42:25 christos Exp $	*/

/*
 * Copyright (c) 1995, 1996 Charles M. Hannum.  All rights reserved.
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
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/ptrace.h>

#include <machine/psl.h>
#include <machine/reg.h>
#include <machine/segments.h>

#include "npx.h"

static __inline struct trapframe *process_frame(struct proc *);
static __inline union savefpu *process_fpframe(struct proc *);
void process_fninit_xmm(struct savexmm *);

static __inline struct trapframe *
process_frame(struct proc *p)
{

	return (p->p_md.md_regs);
}

static __inline union savefpu *
process_fpframe(struct proc *p)
{

	return (&p->p_addr->u_pcb.pcb_savefpu);
}

void
process_xmm_to_s87(const struct savexmm *sxmm, struct save87 *s87)
{
	int i;

	/* FPU control/status */
	s87->sv_env.en_cw = sxmm->sv_env.en_cw;
	s87->sv_env.en_sw = sxmm->sv_env.en_sw;
	/* tag word handled below */
	s87->sv_env.en_fip = sxmm->sv_env.en_fip;
	s87->sv_env.en_fcs = sxmm->sv_env.en_fcs;
	s87->sv_env.en_opcode = sxmm->sv_env.en_opcode;
	s87->sv_env.en_foo = sxmm->sv_env.en_foo;
	s87->sv_env.en_fos = sxmm->sv_env.en_fos;

	/* Tag word and registers. */
	for (i = 0; i < 8; i++) {
		if (sxmm->sv_env.en_tw & (1U << i))
			s87->sv_env.en_tw &= ~(3U << (i * 2));
		else
			s87->sv_env.en_tw |= (3U << (i * 2));

		if (sxmm->sv_ex_tw & (1U << i))
			s87->sv_ex_tw &= ~(3U << (i * 2));
		else
			s87->sv_ex_tw |= (3U << (i * 2));

		memcpy(&s87->sv_ac[i].fp_bytes, &sxmm->sv_ac[i].fp_bytes,
		    sizeof(s87->sv_ac[i].fp_bytes));
	}

	s87->sv_ex_sw = sxmm->sv_ex_sw;
}

void
process_fninit_xmm(struct savexmm *sxmm)
{
	memset(sxmm, 0, sizeof(*sxmm));
	sxmm->sv_env.en_cw = __INITIAL_NPXCW__;
	sxmm->sv_env.en_mxcsr = __INITIAL_MXCSR__;
	sxmm->sv_env.en_mxcsr_mask = fpu_mxcsr_mask;
	sxmm->sv_env.en_sw = 0x0000;
	sxmm->sv_env.en_tw = 0x00;
}

int
process_read_regs(struct proc *p, struct reg *regs)
{
	struct trapframe *tf = process_frame(p);

	regs->r_gs = tf->tf_gs & 0xffff;
	regs->r_fs = tf->tf_fs & 0xffff;
	regs->r_es = tf->tf_es & 0xffff;
	regs->r_ds = tf->tf_ds & 0xffff;
	regs->r_eflags = tf->tf_eflags;
	regs->r_edi = tf->tf_edi;
	regs->r_esi = tf->tf_esi;
	regs->r_ebp = tf->tf_ebp;
	regs->r_ebx = tf->tf_ebx;
	regs->r_edx = tf->tf_edx;
	regs->r_ecx = tf->tf_ecx;
	regs->r_eax = tf->tf_eax;
	regs->r_eip = tf->tf_eip;
	regs->r_cs = tf->tf_cs & 0xffff;
	regs->r_esp = tf->tf_esp;
	regs->r_ss = tf->tf_ss & 0xffff;

	return (0);
}

int
process_read_fpregs(struct proc *p, struct fpreg *regs)
{
	union savefpu *frame = process_fpframe(p);

	if (p->p_md.md_flags & MDP_USEDFPU) {
#if NNPX > 0
		npxsave_proc(p, 1);
#endif
	} else {
		/* Fake a FNINIT. */
		if (i386_use_fxsave) {
			process_fninit_xmm(&frame->sv_xmm);
		} else {
			memset(&frame->sv_87, 0, sizeof(frame->sv_87));
			frame->sv_87.sv_env.en_cw = __INITIAL_NPXCW__;
			frame->sv_87.sv_env.en_sw = 0x0000;
			frame->sv_87.sv_env.en_tw = 0xffff;
		}
		p->p_md.md_flags |= MDP_USEDFPU;
	}

	if (i386_use_fxsave) {
		struct save87 s87;

		/* XXX Yuck */
		process_xmm_to_s87(&frame->sv_xmm, &s87);
		memcpy(regs, &s87, sizeof(*regs));
	} else
		memcpy(regs, &frame->sv_87, sizeof(*regs));

	return (0);
}

#ifdef PTRACE

void
process_s87_to_xmm(const struct save87 *s87, struct savexmm *sxmm)
{
	int i;

	/* FPU control/status */
	sxmm->sv_env.en_cw = s87->sv_env.en_cw;
	sxmm->sv_env.en_sw = s87->sv_env.en_sw;
	/* tag word handled below */
	sxmm->sv_env.en_fip = s87->sv_env.en_fip;
	sxmm->sv_env.en_fcs = s87->sv_env.en_fcs;
	sxmm->sv_env.en_opcode = s87->sv_env.en_opcode;
	sxmm->sv_env.en_foo = s87->sv_env.en_foo;
	sxmm->sv_env.en_fos = s87->sv_env.en_fos;

	/* Tag word and registers. */
	for (i = 0; i < 8; i++) {
		if (((s87->sv_env.en_tw >> (i * 2)) & 3) == 3)
			sxmm->sv_env.en_tw &= ~(1U << i);
		else
			sxmm->sv_env.en_tw |= (1U << i);

		if (((s87->sv_ex_tw >> (i * 2)) & 3) == 3)
			sxmm->sv_ex_tw &= ~(1U << i);
		else
			sxmm->sv_ex_tw |= (1U << i);

		memcpy(&sxmm->sv_ac[i].fp_bytes, &s87->sv_ac[i].fp_bytes,
		    sizeof(sxmm->sv_ac[i].fp_bytes));
	}

	sxmm->sv_ex_sw = s87->sv_ex_sw;
}

int
process_write_regs(struct proc *p, struct reg *regs)
{
	struct trapframe *tf = process_frame(p);

	/*
	 * Check for security violations.
	 */
	if (((regs->r_eflags ^ tf->tf_eflags) & PSL_USERSTATIC) != 0 ||
	    !USERMODE(regs->r_cs, regs->r_eflags))
		return (EINVAL);

	tf->tf_gs = regs->r_gs & 0xffff;
	tf->tf_fs = regs->r_fs & 0xffff;
	tf->tf_es = regs->r_es & 0xffff;
	tf->tf_ds = regs->r_ds & 0xffff;
	tf->tf_eflags = regs->r_eflags;
	tf->tf_edi = regs->r_edi;
	tf->tf_esi = regs->r_esi;
	tf->tf_ebp = regs->r_ebp;
	tf->tf_ebx = regs->r_ebx;
	tf->tf_edx = regs->r_edx;
	tf->tf_ecx = regs->r_ecx;
	tf->tf_eax = regs->r_eax;
	tf->tf_eip = regs->r_eip;
	tf->tf_cs = regs->r_cs & 0xffff;
	tf->tf_esp = regs->r_esp;
	tf->tf_ss = regs->r_ss & 0xffff;

	return (0);
}

int
process_write_fpregs(struct proc *p, struct fpreg *regs)
{
	union savefpu *frame = process_fpframe(p);

	if (p->p_md.md_flags & MDP_USEDFPU) {
#if NNPX > 0
		npxsave_proc(p, 0);
#endif
	} else {
		/*
		 * Make sure MXCSR and the XMM registers are
		 * initialized to sane defaults.
		 */
		if (i386_use_fxsave)
			process_fninit_xmm(&frame->sv_xmm);
		p->p_md.md_flags |= MDP_USEDFPU;
	}

	if (i386_use_fxsave) {
		struct save87 s87;

		/* XXX Yuck. */
		memcpy(&s87, regs, sizeof(*regs));
		process_s87_to_xmm(&s87, &frame->sv_xmm);
	} else
		memcpy(&frame->sv_87, regs, sizeof(*regs));

	return (0);
}

int
process_read_xmmregs(struct proc *p, struct xmmregs *regs)
{
	union savefpu *frame = process_fpframe(p);

	if (!i386_use_fxsave)
		return (EINVAL);

	if (p->p_md.md_flags & MDP_USEDFPU) {
#if NNPX > 0
		npxsave_proc(p, 1);
#endif
	} else {
		/* Fake a FNINIT. */
		process_fninit_xmm(&frame->sv_xmm);
		p->p_md.md_flags |= MDP_USEDFPU;
	}

	memcpy(regs, &frame->sv_xmm, sizeof(*regs));
	return (0);
}

int
process_write_xmmregs(struct proc *p, const struct xmmregs *regs)
{
	union savefpu *frame = process_fpframe(p);

	if (!i386_use_fxsave)
		return (EINVAL);

	if (p->p_md.md_flags & MDP_USEDFPU) {
#if NNPX > 0
		npxsave_proc(p, 0);
#endif
	} else
		p->p_md.md_flags |= MDP_USEDFPU;

	memcpy(&frame->sv_xmm, regs, sizeof(*regs));
	frame->sv_xmm.sv_env.en_mxcsr &= fpu_mxcsr_mask;
	return (0);
}

int
process_sstep(struct proc *p, int sstep)
{
	struct trapframe *tf = process_frame(p);

	if (sstep)
		tf->tf_eflags |= PSL_T;
	else
		tf->tf_eflags &= ~PSL_T;
	
	return (0);
}

int
process_set_pc(struct proc *p, caddr_t addr)
{
	struct trapframe *tf = process_frame(p);

	tf->tf_eip = (int)addr;

	return (0);
}

#endif	/* PTRACE */
