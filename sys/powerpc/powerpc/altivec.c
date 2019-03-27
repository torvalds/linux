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

#include <machine/altivec.h>
#include <machine/pcb.h>
#include <machine/psl.h>

static void
save_vec_int(struct thread *td)
{
	int	msr;
	struct	pcb *pcb;

	pcb = td->td_pcb;

	/*
	 * Temporarily re-enable the vector unit during the save
	 */
	msr = mfmsr();
	mtmsr(msr | PSL_VEC);

	/*
	 * Save the vector registers and VSCR to the PCB
	 */
#define STVX(n)   __asm ("stvx %1,0,%0" \
		:: "b"(pcb->pcb_vec.vr[n]), "n"(n));
	STVX(0);	STVX(1);	STVX(2);	STVX(3);
	STVX(4);	STVX(5);	STVX(6);	STVX(7);
	STVX(8);	STVX(9);	STVX(10);	STVX(11);
	STVX(12);	STVX(13);	STVX(14);	STVX(15);
	STVX(16);	STVX(17);	STVX(18);	STVX(19);
	STVX(20);	STVX(21);	STVX(22);	STVX(23);
	STVX(24);	STVX(25);	STVX(26);	STVX(27);
	STVX(28);	STVX(29);	STVX(30);	STVX(31);
#undef STVX

	__asm __volatile("mfvscr 0; stvewx 0,0,%0" :: "b"(&pcb->pcb_vec.vscr));

	/*
	 * Disable vector unit again
	 */
	isync();
	mtmsr(msr);

}

void
enable_vec(struct thread *td)
{
	int	msr;
	struct	pcb *pcb;
	struct	trapframe *tf;

	pcb = td->td_pcb;
	tf = trapframe(td);

	/*
	 * Save the thread's Altivec CPU number, and set the CPU's current
	 * vector thread
	 */
	td->td_pcb->pcb_veccpu = PCPU_GET(cpuid);
	PCPU_SET(vecthread, td);

	/*
	 * Enable the vector unit for when the thread returns from the
	 * exception. If this is the first time the unit has been used by
	 * the thread, initialise the vector registers and VSCR to 0, and
	 * set the flag to indicate that the vector unit is in use.
	 */
	tf->srr1 |= PSL_VEC;
	if (!(pcb->pcb_flags & PCB_VEC)) {
		memset(&pcb->pcb_vec, 0, sizeof pcb->pcb_vec);
		pcb->pcb_flags |= PCB_VEC;
	}

	/*
	 * Temporarily enable the vector unit so the registers
	 * can be restored.
	 */
	msr = mfmsr();
	mtmsr(msr | PSL_VEC);

	/*
	 * Restore VSCR by first loading it into a vector and then into VSCR.
	 * (this needs to done before loading the user's vector registers
	 * since we need to use a scratch vector register)
	 */
	__asm __volatile("vxor 0,0,0; lvewx 0,0,%0; mtvscr 0" \
			  :: "b"(&pcb->pcb_vec.vscr));

#define LVX(n)   __asm ("lvx " #n ",0,%0" \
		:: "b"(&pcb->pcb_vec.vr[n]));
	LVX(0);		LVX(1);		LVX(2);		LVX(3);
	LVX(4);		LVX(5);		LVX(6);		LVX(7);
	LVX(8);		LVX(9);		LVX(10);	LVX(11);
	LVX(12);	LVX(13);	LVX(14);	LVX(15);
	LVX(16);	LVX(17);	LVX(18);	LVX(19);
	LVX(20);	LVX(21);	LVX(22);	LVX(23);
	LVX(24);	LVX(25);	LVX(26);	LVX(27);
	LVX(28);	LVX(29);	LVX(30);	LVX(31);
#undef LVX

	isync();
	mtmsr(msr);
}

void
save_vec(struct thread *td)
{
	struct pcb *pcb;

	pcb = td->td_pcb;

	save_vec_int(td);

	/*
	 * Clear the current vec thread and pcb's CPU id
	 * XXX should this be left clear to allow lazy save/restore ?
	 */
	pcb->pcb_veccpu = INT_MAX;
	PCPU_SET(vecthread, NULL);
}

/*
 * Save altivec state without dropping ownership.  This will only save state if
 * the current vector-thread is `td'.
 */
void
save_vec_nodrop(struct thread *td)
{
	struct thread *vtd;

	vtd = PCPU_GET(vecthread);
	if (td != vtd) {
		return;
	}

	save_vec_int(td);
}
