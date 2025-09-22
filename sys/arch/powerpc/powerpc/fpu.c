/*	$OpenBSD: fpu.c,v 1.14 2014/07/09 08:34:49 deraadt Exp $	*/
/*	$NetBSD: fpu.c,v 1.1 1996/09/30 16:34:44 ws Exp $	*/

/*
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
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
#include <sys/user.h>

#include <machine/fpu.h>
#include <machine/psl.h>

void
enable_fpu(struct proc *p)
{
	struct cpu_info *ci = curcpu();
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct trapframe *tf = trapframe(p);
	int msr;
	
	if (!(pcb->pcb_flags & PCB_FPU)) {
		bzero(&pcb->pcb_fpu, sizeof pcb->pcb_fpu);
		pcb->pcb_flags |= PCB_FPU;
	}

	if (pcb->pcb_fpcpu != NULL || ci->ci_fpuproc != NULL) {
		printf("attempting to restore fpu state when in use pcb %p"
		    " fpproc %p\n", pcb->pcb_fpcpu, ci->ci_fpuproc);
	}
	msr = ppc_mfmsr();
	ppc_mtmsr((msr  & ~PSL_EE) | PSL_FP);
	__asm volatile("isync");

	asm volatile ("lfd 0,0(%0); mtfsf 0xff,0" :: "b"(&pcb->pcb_fpu.fpcsr));
	asm ("lfd 0,0(%0);"
	     "lfd 1,8(%0);"
	     "lfd 2,16(%0);"
	     "lfd 3,24(%0);"
	     "lfd 4,32(%0);"
	     "lfd 5,40(%0);"
	     "lfd 6,48(%0);"
	     "lfd 7,56(%0);"
	     "lfd 8,64(%0);"
	     "lfd 9,72(%0);"
	     "lfd 10,80(%0);"
	     "lfd 11,88(%0);"
	     "lfd 12,96(%0);"
	     "lfd 13,104(%0);"
	     "lfd 14,112(%0);"
	     "lfd 15,120(%0);"
	     "lfd 16,128(%0);"
	     "lfd 17,136(%0);"
	     "lfd 18,144(%0);"
	     "lfd 19,152(%0);"
	     "lfd 20,160(%0);"
	     "lfd 21,168(%0);"
	     "lfd 22,176(%0);"
	     "lfd 23,184(%0);"
	     "lfd 24,192(%0);"
	     "lfd 25,200(%0);"
	     "lfd 26,208(%0);"
	     "lfd 27,216(%0);"
	     "lfd 28,224(%0);"
	     "lfd 29,232(%0);"
	     "lfd 30,240(%0);"
	     "lfd 31,248(%0)" :: "b"(&pcb->pcb_fpu.fpr[0]));
	ci->ci_fpuproc = p;
	pcb->pcb_fpcpu = ci;
	tf->srr1 |= PSL_FP;
	ppc_mtmsr(msr);
	__asm volatile("isync");
}

void
save_fpu(void)
{
	struct cpu_info *ci = curcpu();
	struct pcb *pcb;
	struct proc *p;
	struct trapframe *tf;
	int msr;
		
	msr = ppc_mfmsr();
	ppc_mtmsr((msr  & ~PSL_EE) | PSL_FP);

	p = ci->ci_fpuproc;

	if (p == NULL) {
		ppc_mtmsr(msr);
		return;
	}

	pcb = &p->p_addr->u_pcb;
	
	__asm volatile("isync");

	asm ("stfd 0,0(%0);"
	     "stfd 1,8(%0);"
	     "stfd 2,16(%0);"
	     "stfd 3,24(%0);"
	     "stfd 4,32(%0);"
	     "stfd 5,40(%0);"
	     "stfd 6,48(%0);"
	     "stfd 7,56(%0);"
	     "stfd 8,64(%0);"
	     "stfd 9,72(%0);"
	     "stfd 10,80(%0);"
	     "stfd 11,88(%0);"
	     "stfd 12,96(%0);"
	     "stfd 13,104(%0);"
	     "stfd 14,112(%0);"
	     "stfd 15,120(%0);"
	     "stfd 16,128(%0);"
	     "stfd 17,136(%0);"
	     "stfd 18,144(%0);"
	     "stfd 19,152(%0);"
	     "stfd 20,160(%0);"
	     "stfd 21,168(%0);"
	     "stfd 22,176(%0);"
	     "stfd 23,184(%0);"
	     "stfd 24,192(%0);"
	     "stfd 25,200(%0);"
	     "stfd 26,208(%0);"
	     "stfd 27,216(%0);"
	     "stfd 28,224(%0);"
	     "stfd 29,232(%0);"
	     "stfd 30,240(%0);"
	     "stfd 31,248(%0)" :: "b"(&pcb->pcb_fpu.fpr[0]));
	asm volatile ("mffs 0; stfd 0,0(%0)" :: "b"(&pcb->pcb_fpu.fpcsr));
	asm ("lfd 0,0(%0);" :: "b"(&pcb->pcb_fpu.fpr[0]));

	tf = trapframe(ci->ci_fpuproc);
	tf->srr1 &= ~PSL_FP;
	ci->ci_fpuproc = NULL;
	pcb->pcb_fpcpu = NULL;

	ppc_mtmsr(msr);
	__asm volatile("isync");
}
