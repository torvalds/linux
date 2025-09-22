/*	$OpenBSD: process_machdep.c,v 1.21 2012/08/29 18:58:45 kettenis Exp $	*/

/*
 * Copyright (c) 1999-2004 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/user.h>

#include <machine/cpufunc.h>
#include <machine/fpu.h>
#include <machine/frame.h>

int
process_read_regs(struct proc *p, struct reg *regs)
{
	struct trapframe *tf = p->p_md.md_regs;

	regs->r_regs[ 0] = tf->tf_ipsw;
	regs->r_regs[ 1] = tf->tf_r1;
	regs->r_regs[ 2] = tf->tf_rp;
	regs->r_regs[ 3] = tf->tf_r3;
	regs->r_regs[ 4] = tf->tf_r4;
	regs->r_regs[ 5] = tf->tf_r5;
	regs->r_regs[ 6] = tf->tf_r6;
	regs->r_regs[ 7] = tf->tf_r7;
	regs->r_regs[ 8] = tf->tf_r8;
	regs->r_regs[ 9] = tf->tf_r9;
	regs->r_regs[10] = tf->tf_r10;
	regs->r_regs[11] = tf->tf_r11;
	regs->r_regs[12] = tf->tf_r12;
	regs->r_regs[13] = tf->tf_r13;
	regs->r_regs[14] = tf->tf_r14;
	regs->r_regs[15] = tf->tf_r15;
	regs->r_regs[16] = tf->tf_r16;
	regs->r_regs[17] = tf->tf_r17;
	regs->r_regs[18] = tf->tf_r18;
	regs->r_regs[19] = tf->tf_t4;
	regs->r_regs[20] = tf->tf_t3;
	regs->r_regs[21] = tf->tf_t2;
	regs->r_regs[22] = tf->tf_t1;
	regs->r_regs[23] = tf->tf_arg3;
	regs->r_regs[24] = tf->tf_arg2;
	regs->r_regs[25] = tf->tf_arg1;
	regs->r_regs[26] = tf->tf_arg0;
	regs->r_regs[27] = tf->tf_dp;
	regs->r_regs[28] = tf->tf_ret0;
	regs->r_regs[29] = tf->tf_ret1;
	regs->r_regs[30] = tf->tf_sp;
	regs->r_regs[31] = tf->tf_r31;

	regs->r_sar	 = tf->tf_sar;

	regs->r_pcsqh	 = tf->tf_iisq_head;
	regs->r_pcsqt	 = tf->tf_iisq_tail;
	regs->r_pcoqh	 = tf->tf_iioq_head;
	regs->r_pcoqt	 = tf->tf_iioq_tail;

	regs->r_sr0	 = tf->tf_sr0;
	regs->r_sr1	 = tf->tf_sr1;
	regs->r_sr2	 = tf->tf_sr2;
	regs->r_sr3	 = tf->tf_sr3;
	regs->r_sr4	 = tf->tf_sr4;
	regs->r_sr5	 = tf->tf_sr5;
	regs->r_sr6	 = tf->tf_sr6;
	regs->r_sr7	 = tf->tf_sr7;

	regs->r_cr26	 = 0;
	regs->r_cr27	 = tf->tf_cr27;

	return (0);
}

int
process_read_fpregs(struct proc *p, struct fpreg *fpregs)
{
	fpu_proc_save(p);

	bcopy(&p->p_addr->u_pcb.pcb_fpstate->hfp_regs, fpregs, 32 * 8);

	return (0);
}

#ifdef PTRACE

int
process_write_regs(struct proc *p, struct reg *regs)
{
	struct trapframe *tf = p->p_md.md_regs;

	tf->tf_r1   = regs->r_regs[ 1];
	tf->tf_rp   = regs->r_regs[ 2];
	tf->tf_r3   = regs->r_regs[ 3];
	tf->tf_r4   = regs->r_regs[ 4];
	tf->tf_r5   = regs->r_regs[ 5];
	tf->tf_r6   = regs->r_regs[ 6];
	tf->tf_r7   = regs->r_regs[ 7];
	tf->tf_r8   = regs->r_regs[ 8];
	tf->tf_r9   = regs->r_regs[ 9];
	tf->tf_r10  = regs->r_regs[10];
	tf->tf_r11  = regs->r_regs[11];
	tf->tf_r12  = regs->r_regs[12];
	tf->tf_r13  = regs->r_regs[13];
	tf->tf_r14  = regs->r_regs[14];
	tf->tf_r15  = regs->r_regs[15];
	tf->tf_r16  = regs->r_regs[16];
	tf->tf_r17  = regs->r_regs[17];
	tf->tf_r18  = regs->r_regs[18];
	tf->tf_t4   = regs->r_regs[19];
	tf->tf_t3   = regs->r_regs[20];
	tf->tf_t2   = regs->r_regs[21];
	tf->tf_t1   = regs->r_regs[22];
	tf->tf_arg3 = regs->r_regs[23];
	tf->tf_arg2 = regs->r_regs[24];
	tf->tf_arg1 = regs->r_regs[25];
	tf->tf_arg0 = regs->r_regs[26];
	tf->tf_dp   = regs->r_regs[27];
	tf->tf_ret0 = regs->r_regs[28];
	tf->tf_ret1 = regs->r_regs[29];
	tf->tf_sp   = regs->r_regs[30];
	tf->tf_r31  = regs->r_regs[31];

	tf->tf_sar  = regs->r_sar;

	tf->tf_iioq_head = regs->r_pcoqh | HPPA_PC_PRIV_USER;
	tf->tf_iioq_tail = regs->r_pcoqt | HPPA_PC_PRIV_USER;

	return (0);
}

int
process_write_fpregs(struct proc *p, struct fpreg *fpregs)
{
	fpu_proc_flush(p);

	bcopy(fpregs, &p->p_addr->u_pcb.pcb_fpstate->hfp_regs, 32 * 8);

	return (0);
}

/* process_sstep() is in trap.c */

int
process_set_pc(struct proc *p, caddr_t addr)
{
	p->p_md.md_regs->tf_iioq_head = (register_t)addr | HPPA_PC_PRIV_USER;
	p->p_md.md_regs->tf_iioq_tail = p->p_md.md_regs->tf_iioq_head + 4;

	return (0);
}

#endif	/* PTRACE */
