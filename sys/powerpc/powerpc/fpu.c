/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
 *
 *	$NetBSD: fpu.c,v 1.5 2001/07/22 11:29:46 wiz Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/limits.h>

#include <machine/fpu.h>
#include <machine/pcb.h>
#include <machine/psl.h>

static void
save_fpu_int(struct thread *td)
{
	int	msr;
	struct	pcb *pcb;

	pcb = td->td_pcb;

	/*
	 * Temporarily re-enable floating-point during the save
	 */
	msr = mfmsr();
	if (pcb->pcb_flags & PCB_VSX)
		mtmsr(msr | PSL_FP | PSL_VSX);
	else
		mtmsr(msr | PSL_FP);

	/*
	 * Save the floating-point registers and FPSCR to the PCB
	 */
	if (pcb->pcb_flags & PCB_VSX) {
	#define SFP(n)   __asm ("stxvw4x " #n ", 0,%0" \
			:: "b"(&pcb->pcb_fpu.fpr[n]));
		SFP(0);		SFP(1);		SFP(2);		SFP(3);
		SFP(4);		SFP(5);		SFP(6);		SFP(7);
		SFP(8);		SFP(9);		SFP(10);	SFP(11);
		SFP(12);	SFP(13);	SFP(14);	SFP(15);
		SFP(16);	SFP(17);	SFP(18);	SFP(19);
		SFP(20);	SFP(21);	SFP(22);	SFP(23);
		SFP(24);	SFP(25);	SFP(26);	SFP(27);
		SFP(28);	SFP(29);	SFP(30);	SFP(31);
	#undef SFP
	} else {
	#define SFP(n)   __asm ("stfd " #n ", 0(%0)" \
			:: "b"(&pcb->pcb_fpu.fpr[n]));
		SFP(0);		SFP(1);		SFP(2);		SFP(3);
		SFP(4);		SFP(5);		SFP(6);		SFP(7);
		SFP(8);		SFP(9);		SFP(10);	SFP(11);
		SFP(12);	SFP(13);	SFP(14);	SFP(15);
		SFP(16);	SFP(17);	SFP(18);	SFP(19);
		SFP(20);	SFP(21);	SFP(22);	SFP(23);
		SFP(24);	SFP(25);	SFP(26);	SFP(27);
		SFP(28);	SFP(29);	SFP(30);	SFP(31);
	#undef SFP
	}
	__asm __volatile ("mffs 0; stfd 0,0(%0)" :: "b"(&pcb->pcb_fpu.fpscr));

	/*
	 * Disable floating-point again
	 */
	isync();
	mtmsr(msr);
}

void
enable_fpu(struct thread *td)
{
	int	msr;
	struct	pcb *pcb;
	struct	trapframe *tf;

	pcb = td->td_pcb;
	tf = trapframe(td);

	/*
	 * Save the thread's FPU CPU number, and set the CPU's current
	 * FPU thread
	 */
	td->td_pcb->pcb_fpcpu = PCPU_GET(cpuid);
	PCPU_SET(fputhread, td);

	/*
	 * Enable the FPU for when the thread returns from the exception.
	 * If this is the first time the FPU has been used by the thread,
	 * initialise the FPU registers and FPSCR to 0, and set the flag
	 * to indicate that the FPU is in use.
	 */
	pcb->pcb_flags |= PCB_FPU;
	if (pcb->pcb_flags & PCB_VSX)
		tf->srr1 |= PSL_FP | PSL_VSX;
	else
		tf->srr1 |= PSL_FP;
	if (!(pcb->pcb_flags & PCB_FPREGS)) {
		memset(&pcb->pcb_fpu, 0, sizeof pcb->pcb_fpu);
		pcb->pcb_flags |= PCB_FPREGS;
	}

	/*
	 * Temporarily enable floating-point so the registers
	 * can be restored.
	 */
	msr = mfmsr();
	if (pcb->pcb_flags & PCB_VSX)
		mtmsr(msr | PSL_FP | PSL_VSX);
	else
		mtmsr(msr | PSL_FP);

	/*
	 * Load the floating point registers and FPSCR from the PCB.
	 * (A value of 0xff for mtfsf specifies that all 8 4-bit fields
	 * of the saved FPSCR are to be loaded from the FPU reg).
	 */
	__asm __volatile ("lfd 0,0(%0); mtfsf 0xff,0"
			  :: "b"(&pcb->pcb_fpu.fpscr));

	if (pcb->pcb_flags & PCB_VSX) {
	#define LFP(n)   __asm ("lxvw4x " #n ", 0,%0" \
			:: "b"(&pcb->pcb_fpu.fpr[n]));
		LFP(0);		LFP(1);		LFP(2);		LFP(3);
		LFP(4);		LFP(5);		LFP(6);		LFP(7);
		LFP(8);		LFP(9);		LFP(10);	LFP(11);
		LFP(12);	LFP(13);	LFP(14);	LFP(15);
		LFP(16);	LFP(17);	LFP(18);	LFP(19);
		LFP(20);	LFP(21);	LFP(22);	LFP(23);
		LFP(24);	LFP(25);	LFP(26);	LFP(27);
		LFP(28);	LFP(29);	LFP(30);	LFP(31);
	#undef LFP
	} else {
	#define LFP(n)   __asm ("lfd " #n ", 0(%0)" \
			:: "b"(&pcb->pcb_fpu.fpr[n]));
		LFP(0);		LFP(1);		LFP(2);		LFP(3);
		LFP(4);		LFP(5);		LFP(6);		LFP(7);
		LFP(8);		LFP(9);		LFP(10);	LFP(11);
		LFP(12);	LFP(13);	LFP(14);	LFP(15);
		LFP(16);	LFP(17);	LFP(18);	LFP(19);
		LFP(20);	LFP(21);	LFP(22);	LFP(23);
		LFP(24);	LFP(25);	LFP(26);	LFP(27);
		LFP(28);	LFP(29);	LFP(30);	LFP(31);
	#undef LFP
	}

	isync();
	mtmsr(msr);
}

void
save_fpu(struct thread *td)
{
	struct	pcb *pcb;

	pcb = td->td_pcb;

	save_fpu_int(td);

	/*
	 * Clear the current fp thread and pcb's CPU id
	 * XXX should this be left clear to allow lazy save/restore ?
	 */
	pcb->pcb_fpcpu = INT_MAX;
	PCPU_SET(fputhread, NULL);
}

/*
 * Save fpu state without dropping ownership.  This will only save state if
 * the current fpu thread is `td'.
 */
void
save_fpu_nodrop(struct thread *td)
{
	struct thread *ftd;

	ftd = PCPU_GET(fputhread);
	if (td != ftd) {
		return;
	}

	save_fpu_int(td);
}
